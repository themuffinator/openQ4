# Linux ARM64 First-Class Support Evidence

Linux ARM64 release archives remain preview packages until a physical ARM64
signoff is bound to the exact release candidate and exact package bytes. Hosted
native runners and the x64-to-AArch64 cross-build prove build, package, ELF, and
assetless compositor startup properties, but they do not prove a player's GPU,
audio, input, or stock-map gameplay experience.

The machine-readable authority is the TOML record defined by
`docs/dev/linux-arm64-first-class-evidence.toml`.
The copy on the release-candidate branch intentionally stays `status =
"pending"`; an accepted record cannot live in the commit it names because a Git
commit cannot contain its own final SHA. Accepted records are committed on a
separate evidence branch or tag and supplied to the manual release workflow by
the immutable `linux_arm64_evidence_ref` input.

## Acceptance Requirements

First-class evidence must come from a physical 64-bit little-endian AArch64
machine, not emulation, using the exact first-class-named candidate archive and
unmodified retail Quake 4 PK4s. The machine must provide a desktop OpenGL compatibility driver;
GLES-only systems are outside the current renderer
contract.

- Launch the packaged client through native Wayland and record the selected SDL
  video driver, compositor, desktop session, GPU, driver, OpenGL
  vendor/renderer/version, kernel, distribution, CPU, and memory.
- Enter representative single-player gameplay and complete a save/load cycle.
  Run `tools/tests/linux_wayland_stock_sp_smoke.py` against the extracted
  candidate with explicit `--install-root`, `--basepath`, and `--output-root`
  paths plus `--physical-hardware`. The automated result proves software audio initialization;
  add `--human-audio-playback-verified` only when the operator actually hears audible playback.
  Retain `report.json`,
  `report.md`, the engine log, save set, stdout/stderr, and post-restore TGA.
- Host and join a stock multiplayer map with two clients, reach active gameplay,
  and record matching declaration checksums.
- Host a stock map with the packaged dedicated server and connect a packaged
  client. Run `tools/tests/linux_dedicated_stock_map_smoke.py` with explicit
  `--dedicated-executable`, `--client-executable`, `--basepath`, and
  `--output-root` arguments plus `--physical-hardware`; retain its
  `report.json`, both runtime logs, and active-gameplay TGA.
- Verify keyboard, relative mouse, controller hotplug/gameplay, audio playback,
  audio-device hotplug, focus loss/recovery, windowed/fullscreen transitions,
  and at least one display-mode change relevant to the hardware.
- Preserve `openq4.log`, dedicated/client logs, screenshots from active SP and MP
  gameplay, and a concise description of every warning or limitation reviewed.
- Keep the generated candidate values unchanged: the full openQ4 and
  openQ4-game SHAs, release version and tags, exact archive filename, archive
  SHA-256, and the client, dedicated-server, and game-module SHA-256s.
  Every SHA-256 is a canonical 64-character lowercase hexadecimal value.

Both stock-media harnesses record a best-effort host inspection alongside the
operator attestation. Supplying `--physical-hardware` stops the run when a
known VM/emulator is identified by `systemd-detect-virt`, hypervisor sysfs or
device-tree data, DMI identity, the kernel release, or CPU flags. No software
probe can prove that a machine is bare metal, so a clean inspection supplements
the operator's attestation rather than replacing it. Container detection is
recorded separately. When either bound report records a container, describe it
explicitly in `[review].hardware_and_os` or `[review].accepted_limitations`;
the verifier rejects accepted evidence that leaves that context undisclosed.

## Required Two-Pass Workflow

The two passes deliberately use the same triggering openQ4 commit, resolved
openQ4-game commit, explicit release version, and Linux build configuration.
Prepare a pushed candidate branch that descends from the current `main` and
already contains every prospective first-class source, front-door support, and
release-note change. Do not merge it yet: a merge, squash, rebase, amend, or
follow-up documentation edit changes the candidate SHA and invalidates the
evidence.

1. Keep `linux_arm64_support_tier=preview`, set
   `generate_linux_arm64_evidence_candidate=true`, and dispatch the manual
   release from that pushed candidate branch. Supply an explicit
   `version_override` and preferably a full openQ4-game commit SHA. Candidate
   mode runs only the native ARM64 job, does not require the release webhook,
   and creates no tag or GitHub release. Its dedicated workflow artifact
   contains the exact no-`-preview` first-class-named archive and a pending TOML
   record populated with candidate identity and hashes. The `preview` selector
   here keeps accepted-evidence resolution disabled; it does not publish a
   preview package.
2. Download that evidence-candidate artifact. Perform all physical-hardware
   tests against that exact archive, not `.install/`, a preview-named archive,
   or a later local rebuild.
3. Change only the generated record's `status` to `accepted` and fill every
   `[review]` value with concrete evidence references. Copy the exact,
   unedited SP `report.json` to
   `docs/dev/linux-arm64-evidence/stock-sp-report.json` and the exact,
   unedited dedicated/client `report.json` to
   `docs/dev/linux-arm64-evidence/dedicated-report.json`. Compute each file's
   canonical lowercase SHA-256 and replace the matching `pending` value in
   `[runtime_reports]`. Do not edit any `[candidate]` or `[sha256]` value, and
   do not reformat or annotate either JSON report after hashing it. Commit the
   accepted TOML at `docs/dev/linux-arm64-first-class-evidence.toml` together
   with both canonical report paths on a separate evidence branch or tag; do
   not merge that evidence-only commit into the frozen candidate branch before
   release.
4. Fast-forward the exact tested candidate commit onto `main`; do not merge,
   squash, rebase, or amend it. If `main` advanced and cannot be fast-forwarded
   to the candidate unchanged, regenerate and retest a new candidate. Then
   re-dispatch from that same commit on `main` with the same explicit release
   version and full openQ4-game SHA, select
   `linux_arm64_support_tier=first-class`, and set
   `linux_arm64_evidence_ref` to the evidence branch, tag, or preferably its full
   commit SHA.
5. The metadata job resolves the evidence ref once and rejects any candidate
   identity mismatch before builds start. It reads both canonical report paths
   from that same immutable evidence commit, verifies their exact SHA-256s,
   rejects duplicate/unknown/missing JSON fields, and requires the generated
   report type, passing native ARM64/Wayland state, physical-host attestation
   plus clean virtualization inspection, and candidate client/game-module
   hashes. The dedicated report must also match the candidate server and MP
   module hashes and a canonical matching declaration checksum; both reports
   must name the same retail `pak001.pk4` bytes. Report acceptance requires the
   exact harness marker sets and complete nontrivial screenshot/save result
   structures, not arbitrary true-valued markers or a status-only result.
   The native ARM64 job then hashes the actual post-strip staged binaries,
   their packaged copies, and the final archive before upload. After release
   artifacts are downloaded into the publishing job, the workflow requires the
   exact expected artifact-name set and hashes the final ARM64 archive against
   the accepted record again. Publication stops unless every expected byte
   matches and no unapproved file is present; the uploaded GitHub release asset
   names are checked again after creation or update.

Linux release builds set `SOURCE_DATE_EPOCH` from the frozen openQ4 commit, and
the runtime tar writer normalizes archive ordering, ownership, permissions, and
timestamps. Toolchain or runner drift can still change binary bytes. Such a
change is intentionally a hard failure: generate a new candidate, repeat the
physical signoff, and accept its new record. Never weaken the record or copy new
hashes into an old acceptance merely to make a release pass.

The machine checks deliberately do not turn manual evidence into automated
claims. SP software audio initialization is required in the bound report, but
human audible playback, keyboard/mouse/controller and device hotplug, focus and
display transitions, and the two-client listen-server MP session remain
explicit `[review]` attestations with retained logs/screenshots. A false or
missing human-audio flag in the SP JSON does not fail report validation because
automation cannot hear the output.

## Manifest Rules

The verifier accepts schema version 3 only, rejects missing or unknown fields,
and treats empty, placeholder, or `pending` review values as incomplete.
`reviewed_at` must be a real ISO 8601 calendar date in `YYYY-MM-DD` form;
reviewer and evidence descriptions must meet field-specific minimum lengths so
single-character or similarly content-free attestations cannot unlock a
release. Placeholder markers such as `TODO`, `TBD`, `unknown`, `n/a`, and
`not recorded` are rejected. An accepted record contains:

- `[review]`: reviewer/date plus hardware, graphics/compositor, audio/input,
  SP, MP, dedicated-server, device/display, logs/screenshots, and accepted-
  limitation evidence.
- `[candidate]`: exact full source SHAs, release version, version tag, release
  tag, and first-class ARM64 archive filename.
- `[sha256]`: the archive and all three runtime ELF hashes (client, dedicated
  server, and the one game module both the SP and MP reports load).
- `[runtime_reports]`: SHA-256s of the exact canonical stock-SP and
  dedicated/client JSON report blobs committed beside the accepted record.

The in-tree pending template is also a format example. Use the workflow-generated
candidate record as the starting point for acceptance so the candidate byte
identities are not transcribed by hand. Candidate generation deliberately leaves
both runtime-report hashes as `pending`; only the physical test pass can produce
those files.
