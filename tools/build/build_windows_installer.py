#!/usr/bin/env python3
"""Build an openQ4 Windows installer from a packaged release directory."""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import json  # noqa: E402
import hashlib  # noqa: E402

from game_layer import PACKAGED_LAYER_GAME_DIRS  # noqa: E402
from openq4_pak import OPENQ4_BULK_PACK_NAMES, OPENQ4_PACK_NAMES  # noqa: E402


PRODUCT_NAME = "openQ4"
PRODUCT_PUBLISHER = "DarkMatter Productions"
SETUP_ICON_RELATIVE = Path("assets") / "icons" / "quake4.ico"
TEMPLATE_RELATIVE = Path("tools") / "build" / "openQ4Installer.iss.in"
SUPPORTED_ARCHES = ("x64", "x86", "arm64")
VERSION_RE = re.compile(r"^[0-9]+(?:\.[0-9]+){2,3}$")
FILENAME_TOKEN_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")


ARCHITECTURE_DIRECTIVES = {
    "x64": (
        "ArchitecturesAllowed=x64compatible",
        "ArchitecturesInstallIn64BitMode=x64compatible",
    ),
    "x86": ("", ""),
    "arm64": (
        "ArchitecturesAllowed=arm64",
        "ArchitecturesInstallIn64BitMode=arm64",
    ),
}


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Compile an Inno Setup installer using an already-packaged openQ4 "
            "Windows release directory."
        )
    )
    parser.add_argument(
        "--package-dir",
        required=True,
        help="Prepared openQ4 Windows package directory to embed in the installer.",
    )
    parser.add_argument(
        "--version",
        required=True,
        help="Human-readable release version string.",
    )
    parser.add_argument(
        "--version-tag",
        required=True,
        help="File-safe release version tag.",
    )
    parser.add_argument(
        "--arch",
        default="x64",
        choices=SUPPORTED_ARCHES,
        help="Target binary architecture tag (default: x64).",
    )
    parser.add_argument(
        "--source-root",
        default=".",
        help="openQ4 repository root (used for installer assets).",
    )
    parser.add_argument(
        "--output-dir",
        required=True,
        help="Directory for the generated installer and temporary .iss script.",
    )
    parser.add_argument(
        "--iscc",
        default=None,
        help="Optional explicit path to ISCC.exe.",
    )
    parser.add_argument(
        "--content-packs-manifest",
        default="",
        help=(
            "content-packs.json for this release. The installer then leaves the bulk packs out "
            "and downloads each one only when the install folder lacks it or holds other bytes."
        ),
    )
    parser.add_argument(
        "--content-pack-base-url",
        default="",
        help="Release download URL the bulk packs are fetched from, for example https://github.com/OWNER/REPO/releases/download/TAG/.",
    )
    parser.add_argument(
        "--skip-compile",
        action="store_true",
        help="Only emit the generated .iss file and skip invoking ISCC.exe.",
    )
    return parser.parse_args(argv[1:])


def inno_string_literal(value: Path | str) -> str:
    return '"' + inno_string_contents(value) + '"'


def inno_string_contents(value: Path | str) -> str:
    return str(value).replace('"', '""')


def validate_version(value: str) -> str:
    normalized = value.strip()
    if VERSION_RE.fullmatch(normalized) is None:
        raise ValueError("version must be a dotted numeric release version such as 0.1.010")
    return normalized


def validate_filename_token(value: str, label: str) -> str:
    normalized = value.strip()
    if FILENAME_TOKEN_RE.fullmatch(normalized) is None:
        raise ValueError(f"{label} must be a file-name-safe token without slashes, spaces, or quotes")
    return normalized


def require_regular_file(path: Path, label: str) -> None:
    if path.is_symlink():
        raise FileNotFoundError(f"{label} must not be a symlink: {path}")
    if not path.is_file():
        raise FileNotFoundError(f"{label} not found: {path}")


def require_directory_arg(path: Path, label: str, *, must_exist: bool) -> Path:
    if path.is_symlink():
        raise FileNotFoundError(f"{label} must not be a symlink: {path}")
    if path.exists() and not path.is_dir():
        raise FileNotFoundError(f"{label} exists but is not a directory: {path}")
    if must_exist and not path.is_dir():
        raise FileNotFoundError(f"{label} not found: {path}")
    return path.resolve()


def validate_output_paths(output_dir: Path, script_path: Path, installer_path: Path) -> None:
    if output_dir.is_symlink():
        raise RuntimeError(f"installer output directory must not be a symlink: {output_dir}")
    if output_dir.exists() and not output_dir.is_dir():
        raise RuntimeError(f"installer output path exists but is not a directory: {output_dir}")
    for path in (script_path, installer_path):
        if path.is_symlink():
            raise RuntimeError(f"refusing to write installer output through symlink: {path}")
        if path.exists() and not path.is_file():
            raise RuntimeError(f"installer output path exists but is not a file: {path}")


def validate_package_dir(package_dir: Path, arch: str) -> None:
    required_paths = [
        package_dir / f"{PRODUCT_NAME}-client_{arch}.exe",
        package_dir / f"{PRODUCT_NAME}-ded_{arch}.exe",
        package_dir / "OpenAL32.dll",
        package_dir / "README.html",
        package_dir / "LICENSE",
        package_dir / "LICENSING.md",
        package_dir / "LICENSES" / "QUAKE-4-SDK-EULA.rtf",
        package_dir / "docs" / "index.html",
        package_dir / "baseoq4" / "mod.json",
        *(package_dir / "baseoq4" / pack_name for pack_name in OPENQ4_PACK_NAMES),
        package_dir / "baseoq4" / f"game-sp_{arch}.dll",
        package_dir / "baseoq4" / f"game-mp_{arch}.dll",
    ]
    # a packaged game-library layer (q4xbase) must be complete: its mod.json and modules.
    # Symbols are published separately (the -symbols.zip asset), never installed.
    for layer in PACKAGED_LAYER_GAME_DIRS:
        layer_dir = package_dir / layer
        if layer_dir.is_symlink():
            raise FileNotFoundError(f"packaged game layer must not be a symlink: {layer_dir}")
        if layer_dir.exists():
            required_paths.append(layer_dir / "mod.json")
            for module in (f"game-sp_{arch}", f"game-mp_{arch}"):
                required_paths.append(layer_dir / f"{module}.dll")

    missing = [path for path in required_paths if not path.is_file() and not path.is_symlink()]
    if missing:
        joined = "\n".join(f"  - {path}" for path in missing)
        raise FileNotFoundError(
            "required package payload files are missing:\n"
            f"{joined}"
        )
    symlinks = [path for path in required_paths if path.is_symlink()]
    if symlinks:
        joined = "\n".join(f"  - {path}" for path in symlinks)
        raise FileNotFoundError(
            "required package payload files must not be symlinks:\n"
            f"{joined}"
        )


def resolve_iscc_path(explicit_path: str | None) -> Path:
    candidates: list[Path] = []

    if explicit_path:
        candidates.append(Path(explicit_path))

    env_path = os.environ.get("INNO_SETUP_ISCC")
    if env_path:
        candidates.append(Path(env_path))

    for env_var in ("ProgramFiles(x86)", "ProgramFiles"):
        base = os.environ.get(env_var)
        if not base:
            continue
        candidates.append(Path(base) / "Inno Setup 7" / "ISCC.exe")
        candidates.append(Path(base) / "Inno Setup 6" / "ISCC.exe")

    for program in ("ISCC.exe", "iscc"):
        resolved = shutil.which(program)
        if resolved:
            candidates.append(Path(resolved))

    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()

    raise FileNotFoundError(
        "ISCC.exe was not found. Install Inno Setup or pass --iscc explicitly."
    )


CONTENT_PACK_NUMBERS = range(10)  # pak0.pk4 to pak9.pk4 are openQ4's own pack names
SHA256_HEX = re.compile(r"[0-9a-f]{64}")


def validate_content_pack_base_url(value: str) -> str:
    """Releases download over HTTPS; plain HTTP is accepted only from this machine, for tests."""
    if not value.endswith("/"):
        value += "/"
    if re.fullmatch(r"https://[A-Za-z0-9.-]+(?::[0-9]+)?/[A-Za-z0-9._~%/+-]*", value):
        return value
    if re.fullmatch(r"http://(?:127\.0\.0\.1|localhost)(?::[0-9]+)?/[A-Za-z0-9._~%/+-]*", value):
        return value
    raise ValueError(f"content pack base URL must be an https:// release URL: {value!r}")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_download_packs(manifest_path: Path, package_dir: Path, base_url: str) -> list[dict[str, object]]:
    """The bulk packs the installer downloads, pinned to the manifest's SHA-256.

    Each must equal the copy in the package directory, so the installer fetches
    exactly the bytes the zip package carries.
    """
    document = json.loads(manifest_path.read_text(encoding="utf-8"))
    records = {pack["name"]: pack for pack in document.get("packs", [])}
    packs = []
    for name in OPENQ4_BULK_PACK_NAMES:
        record = records.get(name)
        if record is None:
            raise RuntimeError(f"{manifest_path} does not list the bulk pack {name}")
        sha256 = str(record["sha256"]).lower()
        if not SHA256_HEX.fullmatch(sha256):
            raise RuntimeError(f"{manifest_path} records an invalid SHA-256 for {name}")
        packaged = package_dir / "baseoq4" / name
        if sha256_file(packaged) != sha256:
            raise RuntimeError(f"{packaged} does not match the manifest's {name}")
        packs.append({"name": name, "sha256": sha256, "size": int(record["size"]), "url": base_url + name})
    return packs


def pascal_string(value: str) -> str:
    return "'" + value.replace("'", "''") + "'"


def render_content_pack_code(download_packs: list[dict[str, object]]) -> str:
    """Pascal for the selective bulk-pack download (roadmap PKG-05).

    A pack already in the install folder with the pinned SHA-256 is kept; one
    saved beside setup.exe with that SHA-256 is copied; anything else is
    downloaded from the release URL compiled in here, and Inno Setup rejects a
    download whose SHA-256 differs. Nothing a server sends chooses what is
    downloaded.
    """
    count = len(download_packs)
    names = "\n".join(f"    {i}: Result := {pascal_string(str(p['name']))};" for i, p in enumerate(download_packs))
    digests = "\n".join(f"    {i}: Result := {pascal_string(str(p['sha256']))};" for i, p in enumerate(download_packs))
    urls = "\n".join(f"    {i}: Result := {pascal_string(str(p['url']))};" for i, p in enumerate(download_packs))
    if count == 0:
        return """procedure InitializeContentPacks;
begin
end;

function FetchContentPacks: Boolean;
begin
  Result := True;
end;

procedure InstallContentPacks;
begin
end;"""
    return f"""const
  ContentPackCount = {count};
  ContentPackKeep = 0;
  ContentPackFromSource = 1;
  ContentPackDownloaded = 2;

var
  ContentPackDownloadPage: TDownloadWizardPage;
  ContentPackAction: array of Integer;

function ContentPackName(Index: Integer): String;
begin
  case Index of
{names}
  end;
end;

function ContentPackSha256(Index: Integer): String;
begin
  case Index of
{digests}
  end;
end;

function ContentPackUrl(Index: Integer): String;
begin
  case Index of
{urls}
  end;
end;

function ContentPackTarget(Index: Integer): String;
begin
  Result := ExpandConstant('{{app}}\\baseoq4\\') + ContentPackName(Index);
end;

function ContentPackMatches(const Path, Sha256: String): Boolean;
begin
  Result := False;
  if FileExists(Path) then
  try
    Result := CompareText(GetSHA256OfFile(Path), Sha256) = 0;
  except
    Result := False;
  end;
end;

function OnContentPackProgress(const Url, FileName: String; const Progress, ProgressMax: Int64): Boolean;
begin
  Result := True;
end;

procedure InitializeContentPacks;
begin
  ContentPackDownloadPage := CreateDownloadPage(SetupMessage(msgWizardPreparing),
    'Downloading the openQ4 light-grid and loadscreen packs this installation needs.', @OnContentPackProgress);
end;

function FetchContentPacks: Boolean;
var
  I, Downloads: Integer;
begin
  Result := True;
  SetArrayLength(ContentPackAction, ContentPackCount);
  ContentPackDownloadPage.Clear;
  Downloads := 0;
  for I := 0 to ContentPackCount - 1 do
  begin
    if ContentPackMatches(ContentPackTarget(I), ContentPackSha256(I)) then
    begin
      ContentPackAction[I] := ContentPackKeep;
      Log('Keeping the installed ' + ContentPackName(I) + '; it is current.');
    end
    else if ContentPackMatches(ExpandConstant('{{src}}\\') + ContentPackName(I), ContentPackSha256(I)) then
    begin
      ContentPackAction[I] := ContentPackFromSource;
      Log('Using ' + ContentPackName(I) + ' saved beside setup.');
    end
    else
    begin
      ContentPackAction[I] := ContentPackDownloaded;
      ContentPackDownloadPage.Add(ContentPackUrl(I), ContentPackName(I) + '.download', ContentPackSha256(I));
      Downloads := Downloads + 1;
      Log('Downloading ' + ContentPackName(I) + ' from ' + ContentPackUrl(I));
    end;
  end;
  if Downloads = 0 then
    Exit;
  ContentPackDownloadPage.Show;
  try
    try
      ContentPackDownloadPage.Download;
    except
      if not ContentPackDownloadPage.AbortedByUser then
        SuppressibleMsgBox('openQ4 could not download its content packs: ' + GetExceptionMessage + #13#10#13#10 +
          'Check your connection and try again, or download the full openQ4 package instead.', mbCriticalError, MB_OK, IDOK);
      Result := False;
    end;
  finally
    ContentPackDownloadPage.Hide;
  end;
end;

procedure InstallContentPacks;
var
  I: Integer;
  Source: String;
begin
  if GetArrayLength(ContentPackAction) <> ContentPackCount then
    if not FetchContentPacks then
    begin
      SuppressibleMsgBox('openQ4 was installed without its light-grid and loadscreen packs. ' +
        'Run setup again, or download the full openQ4 package.', mbCriticalError, MB_OK, IDOK);
      Exit;
    end;
  for I := 0 to ContentPackCount - 1 do
  begin
    if ContentPackAction[I] = ContentPackKeep then
      Continue;
    if ContentPackAction[I] = ContentPackFromSource then
      Source := ExpandConstant('{{src}}\\') + ContentPackName(I)
    else
      Source := ExpandConstant('{{tmp}}\\') + ContentPackName(I) + '.download';
    if not FileCopy(Source, ContentPackTarget(I), False) then
      SuppressibleMsgBox('openQ4 could not install ' + ContentPackName(I) + ' into ' + ContentPackTarget(I) + '.',
        mbCriticalError, MB_OK, IDOK);
  end;
end;"""


def render_install_delete(download_packs: list[dict[str, object]]) -> str:
    lines = [
        'Type: files; Name: "{app}\\*.pdb"',
        'Type: files; Name: "{app}\\baseoq4\\*.pdb"',
        *(f'Type: files; Name: "{{app}}\\{layer}\\*.pdb"' for layer in PACKAGED_LAYER_GAME_DIRS),
    ]
    used = set(OPENQ4_PACK_NAMES)
    lines += [
        f'Type: files; Name: "{{app}}\\baseoq4\\pak{number}.pk4"'
        for number in CONTENT_PACK_NUMBERS
        if f"pak{number}.pk4" not in used
    ]
    return "\n".join(lines)


def render_uninstall_delete(download_packs: list[dict[str, object]]) -> str:
    # Downloaded packs are written by code, not [Files], so remove them explicitly.
    return "\n".join(f'Type: files; Name: "{{app}}\\baseoq4\\{pack["name"]}"' for pack in download_packs)


def render_installer_script(
    template_text: str,
    *,
    package_dir: Path,
    output_dir: Path,
    version: str,
    version_tag: str,
    arch: str,
    setup_icon_file: Path,
    download_packs: list[dict[str, object]] | None = None,
) -> str:
    arch_allowed, arch_install_mode = ARCHITECTURE_DIRECTIVES[arch]
    installer_basename = f"openq4-{version_tag}-windows-{arch}-setup"
    download_packs = download_packs or []
    excludes = ",".join(f"\\baseoq4\\{pack['name']}" for pack in download_packs)

    replacements = {
        "@@APP_ARCH@@": arch,
        "@@APP_VERSION@@": version,
        "@@PACKAGE_SOURCE@@": inno_string_contents(package_dir),
        "@@LICENSE_FILE@@": inno_string_literal(package_dir / "LICENSE"),
        "@@LICENSING_FILE@@": inno_string_literal(package_dir / "LICENSING.md"),
        "@@OUTPUT_DIR@@": inno_string_literal(output_dir),
        "@@OUTPUT_BASENAME@@": installer_basename,
        "@@SETUP_ICON_FILE@@": inno_string_literal(setup_icon_file),
        "@@ARCH_ALLOWED_DIRECTIVE@@": arch_allowed,
        "@@ARCH_INSTALL_MODE_DIRECTIVE@@": arch_install_mode,
        "@@INSTALL_DELETE@@": render_install_delete(download_packs),
        "@@FILES_EXCLUDES@@": excludes,
        "@@UNINSTALL_DELETE@@": render_uninstall_delete(download_packs),
        "@@CONTENT_PACK_CODE@@": render_content_pack_code(download_packs),
    }

    rendered = template_text
    for token, value in replacements.items():
        rendered = rendered.replace(token, value)
    return rendered


def compile_installer(iscc_path: Path, script_path: Path) -> None:
    completed = subprocess.run(
        [str(iscc_path), "/Qp", str(script_path)],
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(f"ISCC.exe failed with exit code {completed.returncode}")


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    try:
        version = validate_version(args.version)
        version_tag = validate_filename_token(args.version_tag, "version tag")
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    try:
        package_dir = require_directory_arg(Path(args.package_dir), "package directory", must_exist=True)
        source_root = require_directory_arg(Path(args.source_root), "source root", must_exist=True)
        output_dir = require_directory_arg(Path(args.output_dir), "installer output directory", must_exist=False)
    except FileNotFoundError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    try:
        validate_package_dir(package_dir, args.arch)
    except FileNotFoundError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    template_path = source_root / TEMPLATE_RELATIVE
    try:
        require_regular_file(template_path, "installer template")
    except FileNotFoundError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    setup_icon_file = source_root / SETUP_ICON_RELATIVE
    try:
        require_regular_file(setup_icon_file, "installer icon")
    except FileNotFoundError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    script_path = output_dir / f"openq4-{version_tag}-windows-{args.arch}-setup.iss"
    installer_path = output_dir / f"openq4-{version_tag}-windows-{args.arch}-setup.exe"
    try:
        validate_output_paths(output_dir, script_path, installer_path)
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    download_packs: list[dict[str, object]] = []
    if args.content_packs_manifest:
        try:
            base_url = validate_content_pack_base_url(args.content_pack_base_url)
            download_packs = read_download_packs(Path(args.content_packs_manifest), package_dir, base_url)
        except (OSError, ValueError, KeyError, RuntimeError) as exc:
            print(f"error: {exc}", file=sys.stderr)
            return 1

    output_dir.mkdir(parents=True, exist_ok=True)
    script_text = render_installer_script(
        template_path.read_text(encoding="utf-8"),
        package_dir=package_dir,
        output_dir=output_dir,
        version=version,
        version_tag=version_tag,
        arch=args.arch,
        setup_icon_file=setup_icon_file.resolve(),
        download_packs=download_packs,
    )
    script_path.write_text(script_text, encoding="utf-8", newline="\n")
    for pack in download_packs:
        print(f"Downloads {pack['name']} ({pack['size']} bytes) only when missing or different: {pack['url']}")

    if args.skip_compile:
        print(f"Generated installer script: {script_path}")
        print(f"Expected installer path: {installer_path}")
        return 0

    try:
        iscc_path = resolve_iscc_path(args.iscc)
    except FileNotFoundError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    try:
        compile_installer(iscc_path, script_path)
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    if not installer_path.is_file():
        print(
            f"error: expected installer executable not found: {installer_path}",
            file=sys.stderr,
        )
        return 1

    print(f"Built {PRODUCT_NAME} Windows installer {version}")
    print(f"Installer: {installer_path}")
    print(f"Installer script: {script_path}")
    print(f"Compiler: {iscc_path}")
    print(f"Publisher: {PRODUCT_PUBLISHER}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
