#!/usr/bin/env python3
"""Public headers of a pinned Meson wrap, without Meson having provisioned it.

CI's script smoke job runs native-harness checks before any subproject exists.
Those checks need only a dependency's public headers, so read them from the
wrap's pinned archive: the package cache or an HTTPS download, bounded in size
and checked against the wrap's source_hash. Only regular .h members under
<directory>/include/<subdirectory>/ are written, each bounded, into the
caller's scratch directory; the archive is never extracted wholesale. A tree
Meson has already provisioned is used as it is.
"""

from __future__ import annotations

import configparser
import hashlib
import re
import tarfile
import urllib.request
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MAX_ARCHIVE_BYTES = 128 * 1024 * 1024
MAX_HEADER_BYTES = 4 * 1024 * 1024
MAX_HEADERS = 256


def wrap_specification(wrap_name: str, root: Path = ROOT) -> dict[str, str]:
    wrap = configparser.ConfigParser()
    if not wrap.read(root / "subprojects" / f"{wrap_name}.wrap"):
        raise RuntimeError(f"Missing subprojects/{wrap_name}.wrap")
    specification = dict(wrap["wrap-file"])
    for key in ("directory", "source_filename"):
        if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", specification[key]):
            raise RuntimeError(f"{wrap_name}.wrap {key} must be a plain name")
    if not re.fullmatch(r"[0-9a-f]{64}", specification["source_hash"]):
        raise RuntimeError(f"{wrap_name}.wrap source_hash must be a SHA-256 digest")
    if not specification["source_url"].startswith("https://"):
        raise RuntimeError(f"{wrap_name}.wrap must download over HTTPS")
    return specification


def pinned_archive(specification: dict[str, str], scratch: Path, root: Path = ROOT) -> Path:
    cached = root / "subprojects" / "packagecache" / specification["source_filename"]
    archive = cached if cached.is_file() else scratch / specification["source_filename"]
    if archive is not cached:
        with urllib.request.urlopen(specification["source_url"], timeout=60) as response, archive.open("wb") as output:
            total = 0
            while chunk := response.read(1024 * 1024):
                total += len(chunk)
                if total > MAX_ARCHIVE_BYTES:
                    raise RuntimeError("Pinned archive exceeds the download bound")
                output.write(chunk)
    if archive.stat().st_size > MAX_ARCHIVE_BYTES:
        raise RuntimeError("Pinned archive exceeds the size bound")
    if hashlib.sha256(archive.read_bytes()).hexdigest() != specification["source_hash"]:
        raise RuntimeError(f"Pinned archive SHA-256 does not match source_hash: {archive}")
    return archive


def pinned_include(wrap_name: str, subdirectory: str, scratch: Path, root: Path = ROOT) -> Path:
    """An include directory holding <subdirectory>/*.h from the wrap's pinned release."""
    if not re.fullmatch(r"[A-Za-z0-9_]+", subdirectory):
        raise RuntimeError("Header subdirectory must be a plain name")
    specification = wrap_specification(wrap_name, root)
    provisioned = root / "subprojects" / specification["directory"] / "include"
    if (provisioned / subdirectory).is_dir():
        return provisioned

    prefix = f"{specification['directory']}/include/{subdirectory}/"
    target = scratch / f"{wrap_name}-include"
    count = 0
    with tarfile.open(pinned_archive(specification, scratch, root), "r:gz") as package:
        for member in package:
            if not member.name.startswith(prefix) or not member.isfile():
                continue
            name = member.name[len(prefix):]
            if not name.endswith(".h"):
                continue
            if not re.fullmatch(r"[A-Za-z0-9_.-]+\.h", name) or member.size < 0 or member.size > MAX_HEADER_BYTES:
                raise RuntimeError(f"Unexpected pinned header: {member.name}")
            count += 1
            if count > MAX_HEADERS:
                raise RuntimeError("Pinned archive has more headers than expected")
            stream = package.extractfile(member)
            if stream is None:
                raise RuntimeError(f"Cannot read pinned header: {member.name}")
            with stream:
                data = stream.read(member.size + 1)
            if len(data) != member.size:
                raise RuntimeError(f"Pinned header size mismatch: {member.name}")
            (target / subdirectory).mkdir(parents=True, exist_ok=True)
            (target / subdirectory / name).write_bytes(data)
    if count == 0:
        raise RuntimeError(f"Pinned archive has no headers under {prefix}")
    return target


def self_test() -> None:
    """Offline checks: the real wraps' pins, then a synthetic archive end to end."""
    import io
    import shutil
    import tempfile

    for wrap_name in ("sdl3", "openxr"):
        wrap_specification(wrap_name)

    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="pinned-headers-", dir=ROOT / ".tmp") as raw:
        fake = Path(raw) / "root"
        (fake / "subprojects" / "packagecache").mkdir(parents=True)
        buffer = io.BytesIO()
        with tarfile.open(fileobj=buffer, mode="w:gz") as package:
            for name, data in (
                ("Demo-1.0/include/demo/demo.h", b"#define DEMO 1\n"),
                ("Demo-1.0/include/demo/CMakeLists.txt", b"ignored\n"),
                ("Demo-1.0/include/other/other.h", b"ignored\n"),
                ("Demo-1.0/src/demo.c", b"ignored\n"),
            ):
                info = tarfile.TarInfo(name)
                info.size = len(data)
                package.addfile(info, io.BytesIO(data))
        archive = buffer.getvalue()
        (fake / "subprojects" / "packagecache" / "Demo-1.0.tar.gz").write_bytes(archive)
        (fake / "subprojects" / "demo.wrap").write_text(
            "[wrap-file]\ndirectory = Demo-1.0\nsource_url = https://example.invalid/Demo-1.0.tar.gz\n"
            f"source_filename = Demo-1.0.tar.gz\nsource_hash = {hashlib.sha256(archive).hexdigest()}\n",
            encoding="utf-8",
        )

        include = pinned_include("demo", "demo", Path(raw) / "a", fake)
        written = sorted(p.relative_to(include).as_posix() for p in include.rglob("*") if p.is_file())
        if written != ["demo/demo.h"]:
            raise AssertionError(f"only the requested headers may be written, got {written}")

        tampered = bytearray(archive)
        tampered[len(tampered) // 2] ^= 1
        (fake / "subprojects" / "packagecache" / "Demo-1.0.tar.gz").write_bytes(bytes(tampered))
        try:
            pinned_include("demo", "demo", Path(raw) / "b", fake)
        except RuntimeError:
            pass
        else:
            raise AssertionError("an archive that does not match source_hash must be refused")

        provisioned = fake / "subprojects" / "Demo-1.0" / "include"
        (provisioned / "demo").mkdir(parents=True)
        if pinned_include("demo", "demo", Path(raw) / "c", fake) != provisioned:
            raise AssertionError("a provisioned tree must be used as it is")
        shutil.rmtree(provisioned)

    print("pinned_archive_headers: ok")


if __name__ == "__main__":
    self_test()
