# macOS Universal2 Design

Updated: 2026-07-15

This document defines how openQ4 can produce one native macOS application for
Apple Silicon and Intel without weakening the current evidence gates. It is an
implementation design, not a user-facing support claim. Current release
downloads remain Apple Silicon/arm64-only until the universal package and both
architecture signoff paths pass.

Apple's [universal-binary guidance](https://developer.apple.com/documentation/apple-silicon/building-a-universal-macos-binary)
requires compiled code—not only the outer app executable—to contain both
`arm64` and `x86_64` slices. The universal2 lane therefore must merge the
client, dedicated server, SP module, and MP module independently and verify
each final Mach-O file with `lipo -archs`.

## Runtime Module Contract

Thin builds compile architecture-specific module selection into each engine
slice:

- the arm64 slice prefers `game_arm64.dylib`;
- the x86_64 slice prefers `game_x64.dylib`.

One module serves both logical modules, `game_sp` and `game_mp`. Those names
remain authoritative for thin packages. If the preferred thin module is absent
on macOS, the engine now tries `game_universal2.dylib` from the same trusted
module roots. Self-contained app integrity checks accept the universal2 module
only when the architecture-specific module is unavailable. The loader still
rejects modules outside the executable/package root and does not fall back to
PK4 or save-path code.

This fallback lets both slices of a merged client converge on a single merged
module path. It also avoids shipping duplicate fat modules under arm64 and x64
filenames.

## Implemented Assembly Contract

`tools/build/assemble_macos_universal2.py` records each clean thin staging tree
before it leaves its native runner, then assembles the matching ARM64 and x64
trees. The record includes the openQ4 and `openQ4-game` commits, canonical
GameLibs source hash, build type, graphics bridge, OpenAL provider, deployment
floor, byte hashes/modes, dependencies, Mach-O slices, module IDs, and shared
payload hash. The assembler refuses a mismatch rather than selecting one input
as authoritative. After download, it re-inspects each thin Mach-O before
merging: deployment floor, install name, dependencies, and `GetGameAPI` export
must still match the recorded metadata, so a modified artifact manifest cannot
weaken the merge checks.

Thin staging trees cross GitHub Actions job boundaries as tar archives. Direct
artifact ZIP transfers normalize executable modes, which would invalidate both
the recorded mode hashes and the final runtime. The merge job checks and
extracts each tar payload before it inspects or combines the trees.

Pull requests no longer run a `macos-universal2` assembly job. Releases are
arm64-only, so commit validation keeps only the arm64 thin payload and its
provenance record. The manual candidate lane below builds both slices, records
the final merged manifest, starts the assetless dedicated server through
`game_universal2.dylib`, and runs the normal package/signing/symbol
validation.

## Non-publishing Release Candidate

`.github/workflows/macos-universal2-candidate.yml` is a manually dispatched,
non-publishing release-grade candidate lane. It resolves one clean, pinned
openQ4 and `openQ4-game` source pair, builds both bridge variants on native
ARM64 and Intel runners, records the thin inputs, preserves their modes across
the artifact boundary, and assembles exact universal2 staging trees. The default ad-hoc mode uploads retained evidence
archives only. Developer ID mode requires the signing and notary credentials,
then re-signs after `lipo`, notarizes, staples, runs Gatekeeper assessment, and
checks each final slice and module install name.

Neither mode creates a GitHub release or changes the public package policy:
user-facing macOS downloads remain an arm64-only preview. A successful
candidate is evidence for a future expansion, not Intel or universal2 support
evidence by itself.

The assembler and packager must:

1. Build thin ARM64 and x64 inputs from the exact same openQ4 and
   `openQ4-game` commits, version metadata, deployment target, build type,
   graphics bridge, and OpenAL provider.
2. Reject symlinks, missing files, architecture swaps, stale extra modules,
   unequal non-code payloads, and mismatched repository metadata before
   merging.
3. Normalize each thin game dylib ID to
   `@loader_path/game-<variant>_universal2.dylib`, then use `lipo -create` for
   the client, dedicated server, SP module, and MP module.
4. Require the exact slice set `arm64 x86_64` for all four merged outputs and
   re-run dependency, deployment-floor, exported-API, and install-name checks.
5. Generate dSYMs from the final merged binaries and compare the complete
   UUID/architecture set from each dSYM with its distributed binary, so every
   `arm64` and `x86_64` slice has matching symbols.
6. Construct the self-contained app, sign nested modules inside-out, sign the
   outer app last, and only then notarize, staple, assess with Gatekeeper, and
   build the DMG. Apple requires nested code to be signed from the inside out;
   signatures from thin inputs are not reusable after `lipo` changes the code.

## Evidence Boundary

Hosted assembly may prove deterministic merging, both slices, loader startup,
and signing-tool behavior. It cannot prove stock-asset gameplay, audio, input,
display transitions, package UX, or Gatekeeper behavior on both CPU families.
Publishing `macos-universal2` requires accepted Apple Silicon and physical or
compliant Intel Mac signoff records tied to the exact final artifact.
