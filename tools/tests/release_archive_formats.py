#!/usr/bin/env python3
"""Release archive formats and the release workflow's fast paths (23 November 2026).

* Linux packages ship as .tar.xz and, for sites that do not take xz (ModDB),
  the same tar as .tar.gz: package_nightly.py writes both from one tar stream.
  Both must hold exactly the tar the previous single-format writer produced,
  the .tar.gz header carries no name or time, and a rerun is byte-identical.
* tar.xz is compressed by `xz -T0` with a fixed block size where xz exists
  (one Linux packaging step took 7 to 10 minutes in single-threaded lzma), and
  by Python's lzma without it.
* verify_release_asset_set.py checks every archive in one decompression pass
  and still rejects a truncated stream; verify_linux_release_artifacts.py
  accepts both Linux suffixes.
* manual-release.yml packages, validates, uploads and expects the .tar.gz for
  both Linux architectures and tiers; uploads already-compressed archives
  without recompression; caches the prepared OpenAL Soft under the same key as
  push-verification.yml; and a rehearsal builds and verifies everything but
  creates no release and posts no announcement.
"""

from __future__ import annotations

import gzip
import hashlib
import lzma
import os
import shutil
import sys
import tarfile
import tempfile
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "build"))

import package_nightly as packager  # noqa: E402
import verify_linux_release_artifacts as linux_verifier  # noqa: E402
import verify_release_asset_set as asset_verifier  # noqa: E402

checks = 0


def require(condition: bool, message: str) -> None:
    global checks
    checks += 1
    if not condition:
        raise AssertionError(message)


def reference_tar(package: Path, executables: set[str]) -> bytes:
    """The tar the single-format writer produced before the streaming writer."""
    out = package.parent / "reference.tar"
    with tarfile.open(out, "w") as archive:
        for path in sorted(package.rglob("*")):
            if not path.is_file():
                continue
            relative = path.relative_to(package)
            info = archive.gettarinfo(path, arcname=(Path(package.name) / relative).as_posix())
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            info.mtime = packager.DETERMINISTIC_TAR_MTIME
            info.mode = 0o755 if relative.as_posix() in executables else 0o644
            with path.open("rb") as handle:
                archive.addfile(info, handle)
    data = out.read_bytes()
    out.unlink()
    return data


def check_archives(scratch: Path) -> None:
    package = scratch / "openq4-0.0.1-linux-x64"
    (package / "baseoq4").mkdir(parents=True)
    (package / "openQ4-client_x64").write_bytes(os.urandom(150_000) + b"\x7fELF" * 40_000)
    (package / "baseoq4" / "pak0.pk4").write_bytes(os.urandom(600_000))
    (package / "README.txt").write_text("openQ4\n" * 500, encoding="utf-8")
    out = scratch / "out"
    out.mkdir()
    xz_path, gz_path = out / f"{package.name}.tar.xz", out / f"{package.name}.tar.gz"
    executables = {"openQ4-client_x64"}
    expected = reference_tar(package, executables)

    def build() -> tuple[str, str]:
        packager.create_release_archive(package, xz_path, "tar.xz", {Path(name) for name in executables},
                                        extra_archives={"tar.gz": gz_path})
        return hashlib.sha256(xz_path.read_bytes()).hexdigest(), hashlib.sha256(gz_path.read_bytes()).hexdigest()

    first = build()
    require(lzma.decompress(xz_path.read_bytes()) == expected, "the .tar.xz holds the reference tar")
    require(gzip.decompress(gz_path.read_bytes()) == expected, "the .tar.gz holds the same tar")
    header = gz_path.read_bytes()[:10]
    require(header[4:8] == b"\0\0\0\0" and not header[3] & 0x08, "the gzip header has no time and no file name")
    require(build() == first, "a rerun writes byte-identical archives")
    with tarfile.open(xz_path) as archive:
        modes = {member.name.split("/", 1)[1]: member.mode for member in archive.getmembers()}
    require(modes["openQ4-client_x64"] == 0o755 and modes["README.txt"] == 0o644, "executable modes survive")

    with mock.patch.object(packager.shutil, "which", return_value=None):
        fallback = out / "fallback.tar.xz"
        packager.create_release_archive(package, fallback, "tar.xz", {Path(name) for name in executables})
        require(lzma.decompress(fallback.read_bytes()) == expected, "without xz, Python's lzma writes the same tar")

    for archive_format, extra in (("tar.xz", {"zip": out / "x.zip"}), ("tar.xz", {"tar.xz": out / "y.tar.xz"}),
                                  ("zip", {"tar.gz": out / "z.tar.gz"})):
        try:
            packager.create_release_archive(package, out / f"bad.{archive_format}", archive_format, extra_archives=extra)
        except RuntimeError:
            require(True, "refused")
        else:
            raise AssertionError(f"{archive_format} with {extra} was accepted")

    # Both Linux suffixes name the same package stem; other suffixes are refused.
    require(linux_verifier.expected_archive_root_name(xz_path) == package.name, ".tar.xz stem")
    require(linux_verifier.expected_archive_root_name(gz_path) == package.name, ".tar.gz stem")
    try:
        linux_verifier.expected_archive_root_name(out / f"{package.name}.zip")
    except linux_verifier.ReleaseArtifactError:
        require(True, "a .zip Linux package is refused")
    else:
        raise AssertionError("a .zip Linux package was accepted")

    # One-pass integrity: good archives pass, truncated ones fail.
    asset_verifier.verify_archive_integrity(xz_path)
    asset_verifier.verify_archive_integrity(gz_path)
    for good in (xz_path, gz_path):
        broken = out / ("truncated-" + good.name)
        broken.write_bytes(good.read_bytes()[:-12])
        try:
            asset_verifier.verify_archive_integrity(broken)
        except RuntimeError:
            require(True, "a truncated stream is refused")
        else:
            raise AssertionError(f"truncated {good.name} passed")
    asset_set = out / "assets"
    asset_set.mkdir()
    shutil.copy(xz_path, asset_set / "openq4-0.0.1-linux-x64.tar.xz")
    shutil.copy(gz_path, asset_set / "openq4-0.0.1-linux-x64.tar.gz")
    require(asset_verifier.verify_asset_set(asset_set, ["openq4-0.0.1-linux-x64.tar.xz", "openq4-0.0.1-linux-x64.tar.gz"])
            == ("openq4-0.0.1-linux-x64.tar.xz", "openq4-0.0.1-linux-x64.tar.gz"), "parallel asset verification")


def check_workflows() -> None:
    release = (ROOT / ".github/workflows/manual-release.yml").read_text(encoding="utf-8")
    push = (ROOT / ".github/workflows/push-verification.yml").read_text(encoding="utf-8")
    tag = "${{ needs.metadata.outputs.version_tag }}"
    require("package_args+=(--extra-archive-format tar.gz)" in release, "Linux packages also as .tar.gz")
    require('echo "gz_archive_path=${gz_archive_path}" >> "$GITHUB_OUTPUT"' in release, "the .tar.gz path is an output")
    require('for archive in "${{ steps.package.outputs.archive_path }}" "${{ steps.package.outputs.gz_archive_path }}"; do'
            in release, "both Linux archives are extracted and validated")
    require(f"name: openq4-release-{tag}-linux-${{{{ matrix.binary_arch }}}}${{{{ matrix.package_suffix }}}}-targz" in release,
            "the .tar.gz is uploaded with the release artifacts")
    for name in ("linux-x64.tar.gz", "linux-arm64-preview.tar.gz", "linux-arm64.tar.gz"):
        require(release.count(f'"openq4-{tag}-{name}"') >= 1, f"publish expects {name}")
    require(release.count(f'"openq4-{tag}-linux-arm64.tar.gz"') == 2 and
            release.count(f'"openq4-{tag}-linux-arm64-preview.tar.gz"') == 2,
            "both ARM64 tiers expect their .tar.gz and drop the other tier's")
    require(release.count("compression-level: 0") >= 10, "compressed release archives upload without recompression")
    require('XZ_OPT="-T0 --block-size=24MiB" tar -C "${symbols_dir}" -cJf' in release, "debug symbols use every core")
    key = "key: openal-soft-windows-${{ matrix.binary_arch }}-${{ hashFiles('tools/build/prepare_windows_openal.ps1') }}"
    require(key in release, "the release caches the prepared OpenAL Soft")
    for arch in ("x64", "arm64"):
        require(f"key: openal-soft-windows-{arch}-${{{{ hashFiles('tools/build/prepare_windows_openal.ps1') }}}}" in push and
                f"path: .tmp/openal-soft-{arch}" in push, f"push verification warms the {arch} OpenAL cache")
    require("      rehearsal:\n" in release and "INPUT_REHEARSAL: ${{ inputs.rehearsal }}" in release, "rehearsal input")
    create = release.index("      - name: Create or update release\n")
    require(release[create:create + 120].count("if: inputs.rehearsal == false") == 1, "a rehearsal creates no release")
    require("needs.release.result == 'success' && inputs.draft == false && inputs.rehearsal == false" in release,
            "a rehearsal posts no announcement")
    require('[ "${rehearsal}" = "false" ] && [ "${GITHUB_REF}" != "refs/heads/main" ]' in release,
            "only a rehearsal may run from another branch")
    require("verify_update_proof \"${platform}\" &" in release and 'wait "${pid}" || proof_failed=1' in release,
            "the update proofs run at once and both must pass")


def main() -> int:
    scratch_root = ROOT / ".tmp"
    scratch_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="release-archive-formats-", dir=scratch_root) as temp:
        check_archives(Path(temp))
    check_workflows()
    print(f"release_archive_formats: {checks} checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
