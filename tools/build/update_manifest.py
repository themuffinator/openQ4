#!/usr/bin/env python3
"""Build, sign and verify openq4-update.json, the manifest the in-game updater reads.

Every release publishes two assets the game fetches from
https://github.com/<repo>/releases/latest/download/:

  openq4-update.json      which version is current, each platform's update
                          archive (name, size, SHA-256) and the content packs
  openq4-update.json.sig  an Ed25519 signature of those exact bytes, as hex

The game trusts a manifest only when the signature verifies against the public
key compiled from src/updater/UpdateSigningKey.h; without one it can still say
that a release is newer, and opens the release page instead of installing.

Commands:

  build    --version 0.15.0 --tag v0.15.0 --artifact-dir DIR --content-packs FILE --out FILE
           [--require windows-x64 ...]
  sign     --manifest FILE [--out FILE] --key-env NAME [--check-header FILE]
  verify   --manifest FILE [--signature FILE] [--header FILE]
  keygen   --private-key FILE [--header FILE]

sign, verify and keygen need the `cryptography` package. The private key is
the OPENQ4_UPDATE_SIGNING_KEY secret of the release workflow: a PEM (PKCS#8)
Ed25519 key, or its 32-byte seed as 64 hex digits. Keep it out of the
repository.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_HEADER = ROOT / "src" / "updater" / "UpdateSigningKey.h"
MANIFEST_NAME = "openq4-update.json"
SIGNATURE_NAME = MANIFEST_NAME + ".sig"
PLATFORMS = ("windows-x64", "windows-arm64", "linux-x64", "linux-arm64")
KEY_DEFINE_RE = re.compile(r'(#define OPENQ4_UPDATE_SIGNING_PUBLIC_KEY )"([0-9a-fA-F]*)"')
VERSION_RE = re.compile(r"^\d{1,5}\.\d{1,5}\.\d{1,5}$")
SAFE_ASSET_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._+-]{0,127}$")
PACK_NAME_RE = re.compile(r"^pak[0-9]\.pk4$")


class ManifestError(RuntimeError):
    pass


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def update_archive_pattern(version: str, platform: str) -> re.Pattern[str]:
    # Linux ARM64 ships as "-preview" until it is first-class.
    return re.compile(rf"^openq4-{re.escape(version)}-{re.escape(platform)}(-preview)?-update\.(zip|tar\.xz)$")


def build_manifest(version: str, tag: str, artifact_dir: Path, content_packs: Path, required: list[str]) -> bytes:
    if not VERSION_RE.fullmatch(version):
        raise ManifestError(f"version must be MAJOR.MINOR.PATCH: {version!r}")
    if tag != "v" + version or not SAFE_ASSET_RE.fullmatch(tag):
        raise ManifestError(f"tag {tag!r} must be 'v' + the version")
    packs_document = json.loads(content_packs.read_text(encoding="utf-8"))
    if packs_document.get("version_tag") != version:
        raise ManifestError(
            f"{content_packs.name} describes {packs_document.get('version_tag')!r}, not {version!r}")

    packages = {}
    names = sorted(path.name for path in artifact_dir.iterdir() if path.is_file() and not path.is_symlink())
    for platform in PLATFORMS:
        pattern = update_archive_pattern(version, platform)
        matches = [name for name in names if pattern.fullmatch(name)]
        if len(matches) > 1:
            raise ManifestError(f"several update archives for {platform}: {matches}")
        if not matches:
            if platform in required:
                raise ManifestError(f"no update archive for {platform} in {artifact_dir}")
            continue
        archive = artifact_dir / matches[0]
        packages[platform] = {
            "archive": archive.name,
            "size": archive.stat().st_size,
            "sha256": sha256_file(archive),
        }

    packs = []
    for entry in packs_document.get("packs", []):
        name = entry["name"]
        if not PACK_NAME_RE.fullmatch(name):
            raise ManifestError(f"unexpected pack name {name!r}")
        pack = {
            "name": name,
            "md5": entry["md5"],
            "sha256": entry["sha256"],
            "size": int(entry["size"]),
            "bulk": bool(entry["bulk"]),
        }
        if pack["bulk"]:
            # The game downloads a changed bulk pack from this release, so it
            # must be published here with exactly these bytes.
            published = artifact_dir / name
            if not published.is_file():
                raise ManifestError(f"bulk pack {name} is not among the release assets")
            if published.stat().st_size != pack["size"] or sha256_file(published) != pack["sha256"]:
                raise ManifestError(f"published {name} does not match {content_packs.name}")
        packs.append(pack)

    document = {
        "schema": 1,
        "product": "openQ4",
        "version": version,
        "tag": tag,
        "packages": packages,
        "packs": packs,
    }
    return (json.dumps(document, indent=2, sort_keys=True) + "\n").encode("utf-8")


def load_private_key(text: str):
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

    text = text.strip()
    if re.fullmatch(r"[0-9a-fA-F]{64}", text):
        return Ed25519PrivateKey.from_private_bytes(bytes.fromhex(text))
    key = serialization.load_pem_private_key(text.encode("ascii"), password=None)
    if not isinstance(key, Ed25519PrivateKey):
        raise ManifestError("the signing key is not an Ed25519 key")
    return key


def public_hex(private_key) -> str:
    from cryptography.hazmat.primitives import serialization

    return private_key.public_key().public_bytes(
        serialization.Encoding.Raw, serialization.PublicFormat.Raw).hex()


def header_public_key(header: Path) -> str:
    match = KEY_DEFINE_RE.search(header.read_text(encoding="utf-8"))
    if match is None:
        raise ManifestError(f"{header} has no OPENQ4_UPDATE_SIGNING_PUBLIC_KEY definition")
    return match.group(2).lower()


def verify_signature(manifest: bytes, signature_text: str, public_key_hex: str) -> bool:
    from cryptography.exceptions import InvalidSignature
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey

    key = Ed25519PublicKey.from_public_bytes(bytes.fromhex(public_key_hex))
    try:
        key.verify(bytes.fromhex(signature_text.strip()), manifest)
    except (InvalidSignature, ValueError):
        return False
    return True


def command_build(args: argparse.Namespace) -> int:
    data = build_manifest(args.version, args.tag, Path(args.artifact_dir), Path(args.content_packs), args.require)
    Path(args.out).write_bytes(data)
    document = json.loads(data)
    print(f"wrote {args.out}: {document['version']}, packages {sorted(document['packages'])}")
    return 0


def command_sign(args: argparse.Namespace) -> int:
    secret = os.environ.get(args.key_env, "")
    if not secret.strip():
        raise ManifestError(f"{args.key_env} is empty")
    private_key = load_private_key(secret)
    if args.check_header:
        compiled = header_public_key(Path(args.check_header))
        if compiled != public_hex(private_key):
            raise ManifestError(
                "the signing key does not match the public key compiled into this release "
                f"({args.check_header}); games would reject the manifest")
    manifest_path = Path(args.manifest)
    signature = private_key.sign(manifest_path.read_bytes()).hex()
    out = Path(args.out) if args.out else manifest_path.with_name(manifest_path.name + ".sig")
    out.write_text(signature + "\n", encoding="ascii", newline="\n")
    print(f"wrote {out}")
    return 0


def command_verify(args: argparse.Namespace) -> int:
    manifest_path = Path(args.manifest)
    signature_path = Path(args.signature) if args.signature else manifest_path.with_name(manifest_path.name + ".sig")
    public = header_public_key(Path(args.header))
    if not public:
        raise ManifestError(f"{args.header} holds no public key")
    if not verify_signature(manifest_path.read_bytes(), signature_path.read_text(encoding="ascii"), public):
        raise ManifestError(f"{signature_path.name} does not verify {manifest_path.name}")
    print(f"{signature_path.name} verifies {manifest_path.name}")
    return 0


def command_keygen(args: argparse.Namespace) -> int:
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

    private_path = Path(args.private_key).resolve()
    if private_path.exists():
        raise ManifestError(f"{private_path} exists; refusing to overwrite a signing key")
    try:
        private_path.relative_to(ROOT)
        raise ManifestError("keep the private key outside the repository")
    except ValueError:
        pass
    key = Ed25519PrivateKey.generate()
    pem = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                            serialization.NoEncryption())
    private_path.parent.mkdir(parents=True, exist_ok=True)
    descriptor = os.open(private_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "wb") as handle:
        handle.write(pem)
    public = public_hex(key)
    header = Path(args.header)
    text = header.read_text(encoding="utf-8")
    if KEY_DEFINE_RE.search(text) is None:
        raise ManifestError(f"{header} has no OPENQ4_UPDATE_SIGNING_PUBLIC_KEY definition")
    header.write_text(KEY_DEFINE_RE.sub(lambda m: f'{m.group(1)}"{public}"', text), encoding="utf-8", newline="\n")
    print(f"private key: {private_path} (add it as the OPENQ4_UPDATE_SIGNING_KEY secret, then delete or vault it)")
    print(f"public key:  {public} (written to {header})")
    return 0


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)

    build = commands.add_parser("build")
    build.add_argument("--version", required=True)
    build.add_argument("--tag", required=True)
    build.add_argument("--artifact-dir", required=True)
    build.add_argument("--content-packs", required=True)
    build.add_argument("--out", required=True)
    build.add_argument("--require", action="append", default=[], choices=PLATFORMS)
    build.set_defaults(handler=command_build)

    sign = commands.add_parser("sign")
    sign.add_argument("--manifest", required=True)
    sign.add_argument("--out", default="")
    sign.add_argument("--key-env", default="OPENQ4_UPDATE_SIGNING_KEY")
    sign.add_argument("--check-header", default="")
    sign.set_defaults(handler=command_sign)

    verify = commands.add_parser("verify")
    verify.add_argument("--manifest", required=True)
    verify.add_argument("--signature", default="")
    verify.add_argument("--header", default=str(DEFAULT_HEADER))
    verify.set_defaults(handler=command_verify)

    keygen = commands.add_parser("keygen")
    keygen.add_argument("--private-key", required=True)
    keygen.add_argument("--header", default=str(DEFAULT_HEADER))
    keygen.set_defaults(handler=command_keygen)
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    try:
        return args.handler(args)
    except ManifestError as exc:
        print(f"update_manifest: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
