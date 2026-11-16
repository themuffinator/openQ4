#!/usr/bin/env python3
"""Exercise tools/build/update_manifest.py against a fake release.

Builds a manifest from fake update archives and packs, generates a throwaway
key into a copy of UpdateSigningKey.h, signs, verifies, and checks the
refusals: a missing required platform, a published pack that does not match
content-packs.json, a key that does not match the compiled public key, and a
tampered manifest. Skips (exit 0) when `cryptography` is not installed.
"""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools" / "build" / "update_manifest.py"
HEADER = ROOT / "src" / "updater" / "UpdateSigningKey.h"


def run(*args: str, env: dict | None = None) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(TOOL), *args], capture_output=True, text=True, env=env)


def main() -> int:
    try:
        import cryptography  # noqa: F401
    except ImportError:
        print("update manifest tool: skipped (cryptography is not installed)")
        return 0

    failures: list[str] = []

    def check(condition: bool, message: str) -> None:
        if not condition:
            failures.append(message)
            print("FAIL:", message)

    scratch = Path(tempfile.mkdtemp(prefix="openq4-update-manifest-"))
    try:
        artifacts = scratch / "artifacts"
        artifacts.mkdir()
        version = "0.15.0"
        for name in (
            f"openq4-{version}-windows-x64-update.zip",
            f"openq4-{version}-windows-arm64-update.zip",
            f"openq4-{version}-linux-x64-update.tar.xz",
            f"openq4-{version}-linux-arm64-preview-update.tar.xz",
            f"openq4-{version}-windows-x64-symbols.zip",
        ):
            (artifacts / name).write_bytes(name.encode() * 10)
        pak1 = b"light grids" * 100
        (artifacts / "pak1.pk4").write_bytes(pak1)
        packs = {
            "version_tag": version,
            "packs": [
                {"name": "pak0.pk4", "md5": "0" * 32, "sha256": "1" * 64, "size": 5, "bulk": False},
                {"name": "pak1.pk4", "md5": hashlib.md5(pak1).hexdigest(), "sha256": hashlib.sha256(pak1).hexdigest(),
                 "size": len(pak1), "bulk": True},
            ],
        }
        content_packs = artifacts / f"openq4-{version}-content-packs.json"
        content_packs.write_text(json.dumps(packs), encoding="utf-8")
        manifest = artifacts / "openq4-update.json"
        build = ["build", "--version", version, "--tag", "v" + version, "--artifact-dir", str(artifacts),
                 "--content-packs", str(content_packs), "--out", str(manifest),
                 "--require", "windows-x64", "--require", "linux-x64"]

        result = run(*build)
        check(result.returncode == 0, f"build succeeds: {result.stderr}")
        document = json.loads(manifest.read_text(encoding="utf-8"))
        check(sorted(document["packages"]) == ["linux-arm64", "linux-x64", "windows-arm64", "windows-x64"],
              f"all four platforms found ({sorted(document['packages'])})")
        check(document["packages"]["linux-arm64"]["archive"].endswith("-linux-arm64-preview-update.tar.xz"),
              "the preview Linux ARM64 archive is used")
        win = artifacts / f"openq4-{version}-windows-x64-update.zip"
        check(document["packages"]["windows-x64"]["sha256"] == hashlib.sha256(win.read_bytes()).hexdigest(),
              "archive digest")
        check(document["tag"] == "v" + version and document["schema"] == 1 and document["product"] == "openQ4", "header fields")

        bad = run("build", "--version", version, "--tag", "v" + version, "--artifact-dir", str(artifacts),
                  "--content-packs", str(content_packs), "--out", str(scratch / "x.json"), "--require", "windows-x64",
                  "--require", "linux-x64", "--require", "linux-arm64", "--require", "windows-arm64")
        check(bad.returncode == 0, "every platform can be required when present")
        (artifacts / "pak1.pk4").write_bytes(b"not the pack")
        bad = run(*build[:-4], "--out", str(scratch / "x.json"))
        check(bad.returncode != 0 and "does not match" in bad.stderr, "a published pack that differs is refused")
        (artifacts / "pak1.pk4").write_bytes(pak1)
        (artifacts / f"openq4-{version}-linux-x64-update.tar.xz").unlink()
        bad = run(*build)
        check(bad.returncode != 0 and "linux-x64" in bad.stderr, "a missing required platform is refused")
        bad = run("build", "--version", version, "--tag", "v0.15.1", "--artifact-dir", str(artifacts),
                  "--content-packs", str(content_packs), "--out", str(scratch / "x.json"))
        check(bad.returncode != 0, "a tag that is not v + version is refused")

        header = scratch / "UpdateSigningKey.h"
        shutil.copy2(HEADER, header)
        private = scratch / "keys" / "signing.pem"
        result = run("keygen", "--private-key", str(private), "--header", str(header))
        check(result.returncode == 0 and private.is_file(), f"keygen: {result.stderr}")
        check(run("keygen", "--private-key", str(private), "--header", str(header)).returncode != 0,
              "keygen refuses to overwrite a key")
        check(run("keygen", "--private-key", str(ROOT / ".tmp" / "never.pem"), "--header", str(header)).returncode != 0,
              "keygen refuses a key inside the repository")
        compiled = header.read_text(encoding="utf-8")
        check('OPENQ4_UPDATE_SIGNING_PUBLIC_KEY ""' not in compiled, "public key written to the header")

        env = dict(os.environ, OPENQ4_UPDATE_SIGNING_KEY=private.read_text(encoding="ascii"))
        result = run("sign", "--manifest", str(manifest), "--check-header", str(header), env=env)
        check(result.returncode == 0, f"sign: {result.stderr}")
        signature = manifest.with_name(manifest.name + ".sig")
        check(len(signature.read_text(encoding="ascii").strip()) == 128, "hex signature")
        check(run("verify", "--manifest", str(manifest), "--header", str(header)).returncode == 0, "verify")

        other = dict(os.environ, OPENQ4_UPDATE_SIGNING_KEY="ab" * 32)
        result = run("sign", "--manifest", str(manifest), "--out", str(scratch / "other.sig"), "--check-header",
                     str(header), env=other)
        check(result.returncode != 0 and "does not match" in result.stderr, "a key the build does not trust is refused")

        manifest.write_bytes(manifest.read_bytes().replace(b'"schema": 1', b'"schema": 1 '))
        check(run("verify", "--manifest", str(manifest), "--header", str(header)).returncode != 0,
              "a changed manifest no longer verifies")
        check(run("verify", "--manifest", str(manifest), "--header", str(HEADER)).returncode != 0
              if 'OPENQ4_UPDATE_SIGNING_PUBLIC_KEY ""' in HEADER.read_text(encoding="utf-8") else True,
              "an empty compiled key verifies nothing")
    finally:
        shutil.rmtree(scratch, ignore_errors=True)

    if failures:
        print(f"{len(failures)} update manifest tool checks failed")
        return 1
    print("update manifest tool: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
