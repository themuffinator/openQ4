# macOS Symbolication Workflow

Updated: 2026-06-30

This document records the no-hardware symbolication contract for preview
macOS openQ4 packages. It helps maintainers pair a user `.ips` or `.crash`
report with the matching dSYM archive even when they cannot reproduce the crash
on macOS.

## Release Artifacts

Every macOS release package must have a separate dSYM archive beside it:

- Runtime package: `openq4-<version>-macos-arm64-opengl.dmg`
- Symbol archive: `openq4-<version>-macos-arm64-opengl-symbols.tar.xz`
- Runtime package: `openq4-<version>-macos-arm64-metal.dmg`
- Symbol archive: `openq4-<version>-macos-arm64-metal-symbols.tar.xz`

Unsigned fallback packages keep the same rule with
`-unsigned-symbols.tar.xz`.

The runtime DMG or tarball must not contain `.dSYM` bundles. The symbol archive
is a separate diagnostic artifact and is not part of normal installation.

## Covered Binaries

The dSYM archive must cover each Mach-O target that can appear in a crash log:

- `openQ4.app/Contents/MacOS/openQ4`
- `openQ4-client_arm64`
- `openQ4-ded_arm64`
- `openQ4.app/Contents/Frameworks/game_arm64.dylib`

No additional Mach-O helper tools are shipped in the current package layout. If
one is added later, add it to `tools/build/package_nightly.py`,
`SYMBOLS.txt`, and the static symbolication policy test in the same change.

## Manifest

macOS runtime packages include `SYMBOLS.txt` at the package root. The support
collector copies it into `package/SYMBOLS.txt` when a user runs
`collect_macos_support_info.sh`.

The support collector copies it into `package/SYMBOLS.txt` so issue reports can
carry the symbol manifest without bundling `.dSYM` debug payloads.

`SYMBOLS.txt` records:

- openQ4 version and version tag
- platform and architecture
- runtime package archive name
- matching dSYM symbol archive name
- each covered binary path
- SHA-256 and size of each covered binary
- `dwarfdump --uuid` output for each covered binary when generated on macOS
- the dSYM path inside the symbol archive

## Pairing A Crash Report

1. Ask the reporter for the exact runtime artifact name or collect
   `package/SYMBOLS.txt` from their support archive.
2. Use the `symbol_archive=` line in `SYMBOLS.txt` to pick the dSYM archive.
3. Match the crashing image in the `.ips` report against one of the listed
   binary paths, SHA-256 values, or Mach-O UUID lines.
4. Use the matching dSYM bundle from the archive for symbolication.

When the runtime package name and `SYMBOLS.txt` disagree, treat the report as
mixed-package evidence and ask for a fresh support archive from the package root
that actually launched.

## Static Enforcement

The no-macOS-access guard is `tools/tests/macos_symbolication_policy.py`. It
checks:

- dSYM generation and manifest logic in `tools/build/package_nightly.py`
- runtime package filtering for `.dSYM` bundles
- manual release upload and verification of macOS symbol archives
- support collector copying of `SYMBOLS.txt`
- Phase 4 checklist status and release-note wording

These checks run without requiring any macOS platform test.
