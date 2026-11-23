# Release qualification

> **Development status:** This document describes the current source tree and its evidence, not the contents of the latest available 0.15.0 package. The 0.14.0 release was withdrawn. See [release status](../user/release-status.md).

This is the technical reference for the release-qualification tasks REL-01 through
REL-04 in the [roadmap](roadmap.md#release-qualification-and-completion-rules). It
says how a release candidate is built, frozen and tested, which harnesses make up
the matrix, and what each candidate's run proved and did not prove. The roadmap
keeps the task state; this page keeps the procedure and the evidence.

## The rule this procedure enforces

Every result that qualifies a release must come from the same bytes. A test on a
different revision, on a staged tree that changed between cases, or on a build with
uncommitted source is evidence for that other build, not for the candidate. So:

1. **Freeze one candidate.** Build a clean commit in the release configuration,
   package it with the release packager, and extract the archive. That extracted
   package is the candidate. `freeze` hashes every file in it and records its
   version line, the commit that line names, the retail packs and the host.
2. **Test only that candidate.** Each matrix case runs against the frozen runtime.
   The runner re-checks the runtime against its manifest before and after every
   case and stops on any change; a case that ran on other bytes is recorded as
   `invalid`, not as a pass.
3. **Keep every result.** Attempts are appended, never replaced. A failure stays
   on record after a fix; after a fix the candidate is a new candidate, frozen and
   run again. A rerun of an unchanged candidate (for example after a driver
   startup hang) is a second attempt of the same case, and the history shows both.
4. **Separate machine evidence from human evidence.** Motion, latency, audible
   sound, accessibility, VR comfort and matched visual acceptance need a person
   and, for some, physical devices. The runner lists them as reviews; only
   `record-human` with the reviewer's own verdict and notes fills them in.
5. **Promote only what was proven.** A scoped pass on one platform does not
   qualify another platform, renderer or device class, and an optional feature's
   gap does not block an unrelated supported configuration.

## Building and freezing a candidate

The candidate mirrors the Windows job of `manual-release.yml`:

```powershell
git worktree add --detach .tmp/rel/wt <commit>
# copy subprojects/packagecache and src/external/openal-soft/bin/win64/OpenAL32.dll in
tools/build/meson_setup.ps1 setup --wipe <abs build> <abs wt> --backend ninja `
  --buildtype=debugoptimized -Db_ndebug=true --wrap-mode=forcefallback `
  -Dversion_track=stable -Dversion_base_override=<version>
tools/build/meson_setup.ps1 compile -C <abs build>
tools/build/meson_setup.ps1 install -C <abs build> --no-rebuild --skip-subprojects
python tools/build/verify_release_source_provenance.py --project-root <wt> `
  --expected-project-commit <commit> --build-dir <abs build>
python tools/build/package_release.py --platform windows --arch x64 --version <version> `
  --version-tag <version> --archive-format zip --source-root <wt> --output-dir .tmp/rel/pkg
```

Extract the runtime archive into `.tmp/runtime/candidate/` (a stable path whose
dedicated server already has a firewall rule; the client has none, so every
multiplayer case binds loopback only). Then freeze it:

```powershell
python tools/validation/release_qualification.py freeze --runtime .tmp/runtime/candidate `
  --output .tmp/rel/evidence-<label> --basepath .tmp/rel/q4-packs --awakening .home `
  --previous-release .tmp/runtime/release-0150 --package <zip> --source-root .tmp/rel/wt
python tools/validation/release_qualification.py run --output .tmp/rel/evidence-<label>
python tools/validation/release_qualification.py report --output .tmp/rel/evidence-<label>
```

- **Retail assets.** On Windows use a packs-only view of the retail install
  (hard links to `q4base/*.pk4`). A Steam `q4base` may hold loose files that
  override the packs, and then a run is not stock-asset evidence.
- **Previous release.** Use the official archive of the latest available
  release, checked against the GitHub release asset digest.
- **Machine.** Run one client at a time and only with enough free commit memory
  (the runner waits for `--min-free-gb`, 8 by default). Hidden OpenGL clients on
  NVIDIA can hang in driver shader compilation at startup; a timeout whose log
  ends at `R_ARB2_Init` is that hang, and the case is rerun as a new attempt.
- **Source commit.** A dev or nightly build's version line ends in `+g<sha>`. A
  clean stable release build's line is only `openQ4 <version>` by design, so pass
  `--source-commit <full SHA>`; `freeze` accepts it only when the client's
  Windows version resource carries that commit's history length as its build
  field (the official 0.15.0 client carries 2636, which is
  `git rev-list --count v0.15.0`). `renderer_vulkan_qualification.py` takes the
  same `--source-commit` for a stable line and records it as operator-supplied.
- **Freshness guard.** The multiplayer smokes compare a stage with this
  checkout's `builddir`. The runner sets `OPENQ4_ALLOW_STALE_RUNTIME=1` for every
  case because its manifest check is stricter: it proves the bytes themselves.

## The matrix

`release_qualification.py list` prints the current matrix. Every automated case
is an existing input-free harness pointed at the candidate: hidden windowed
clients, engine commands and configs, engine `screenshot` render-target captures,
loopback networking. None synthesises device input or captures the desktop.

| Case | Task | What it proves |
| --- | --- | --- |
| `rel01.package.layout` | REL-01 | One engine, both renderer and game modules for the architecture, pk4s, OpenAL, licence; no linker artefacts; every module names the engine's commit. |
| `rel01.ded.startup` | REL-01 | Asset-free dedicated server start, loopback self-test, clean exit. |
| `rel01.mp.dedicated-client` | REL-01 | LAN discovery and three joins of a pure dedicated server on a stock map. |
| `rel01.mp.listen-gl`, `-vk` | REL-01 | Loopback listen servers host bot matches on all 20 stock MP maps; Vulkan compared with OpenGL. |
| `rel01.sp.stock-maps-gl`, `-vk` | REL-01 | All 29 stock SP maps load and draw their opening view; Vulkan compared with OpenGL. |
| `rel01.sp.save-load` | REL-01 | Save after play, restore in a new process, resave before and after time resumes; portable v5 saves. |
| `rel01.sp.demo` | REL-01 | A demo recorded in gameplay plays back with `timeDemoQuit` in a new process. |
| `rel01.sp.campaign-transitions` | REL-01 | All 31 stock stages in order: save, load, and the map's own end-level change. |
| `rel01.sp.campaign-saves` | REL-01 | Saves on five campaign maps reload in a fresh process. |
| `rel01.campaign.switch` | REL-01 | Quake 4 and The Awakening are discovered and switched in place. |
| `rel01.install.clean` | REL-01 | A player's first starts: the menu detects a preset silently and writes the config, a level drops nothing, the next menu start changes nothing; then multiplayer-only settings survive a level and a menu start. |
| `rel01.install.upgrade` | REL-01 | A profile made by the previous release keeps every player choice; its save restores and resaves. |
| `rel01.content-packs` | REL-01 | A missing or stale pack is reported, localized, naming the pack. |
| `rel01.localization` | REL-01 | A complete non-English language drives the front end and gameplay text. |
| `rel03.soak-gl`, `-vk` | REL-03 | One process tours eight SP maps for several cycles with a `vid_restart`; memory sampled at every stop. |
| `rel03.lifecycle` | REL-03 | Cold start, warm start, start after generated-cache damage, a session with resize and renderer restarts, pause, slow motion, save, map change and quit, then the same profile relaunched on Vulkan (the API changes only at engine start) and back on OpenGL. |
| `rel03.network.reconnect-impaired` | REL-03 | Joins and reconnects through simulated loss and latency. |
| `rel03.network.ipv6` | REL-03 | The same joins over IPv6 loopback. |
| `rel03.renderer.vk-recovery` | REL-03 | Same-process Vulkan startup recovery, then gameplay. |
| `rel03.effects.lifecycle` | REL-03 | Effects across map changes, restores and renderer restarts. |
| `rel02.captures.generated-images` | REL-02 | Engine render-target captures of generated images on both renderers. |
| `rel02.human.*` | REL-02 | Matched visual acceptance, motion, latency, audible sound, accessibility and VR comfort reviews. |
| `rel04.package.symbols` | REL-04 | Every openQ4 image has a PDB in the release's symbols archive whose GUID matches its CodeView record. |
| `rel04.package.signing` | REL-04 | Records each Windows image's Authenticode status; fails only on a signature that does not verify. |

The profile and lifecycle cases come from
`tools/tests/release_profile_runtime.py`. The
upgrade profile sets values that differ from every shipped default across input,
audio, video and gameplay, three key bindings and the Awakening location, all
made by the previous release itself. The run fails only on a lost player choice;
every other archived value the candidate changed is listed in its report as a
migration to review.

### Not in the matrix yet

These REL rows have no automated case and stay open whatever the matrix shows:

- **Multiplayer demos (MVD) and match recordings.** `mp_mvd_smoke.py` replays a
  match report made by a listen-server match, and those harnesses bind every
  interface, which raises a firewall prompt for a client path without a rule.
- **Device, surface and audio-device loss on real hardware,** background and
  suspend, and high-DPI displays. The audio recovery harness drives the staged
  `.install` only, and an unplugged device needs a person.
- **Other platforms.** Linux, Steam Deck, macOS, Windows ARM64 and Android need
  their own candidate packages and runs on their hardware.

## Human and physical reviews

`report` lists them with their procedures. Record each with:

```powershell
python tools/validation/release_qualification.py record-human --output <evidence> `
  --case rel02.human.audio --verdict pass --reviewer <name> --device "<headphones>" `
  --notes "<what was checked and anything that differed>"
```

Fullscreen and input-control tests need the project owner's prior permission for
that session; a historical plan or an earlier approval is not permission.

## Package checks for promotion (REL-04)

- **Provenance.** `verify_release_source_provenance.py` against the candidate's
  build directory, and `freeze`'s commit binding.
- **Exact package.** `freeze --package` proves the runtime holds exactly the
  archive's files.
- **Layout.** `rel01.package.layout`.
- **Symbols.** `rel04.package.symbols` against the archive beside the package.
- **Signing.** `rel04.package.signing`. Windows packages are unsigned, so
  SmartScreen may warn on first launch; a signed release needs a code-signing
  certificate, which is a project decision.
- **Licences.** The package carries `LICENSE`, `LICENSING.md`, the
  `licenses/` notices and the documentation's `LICENSES/` copies.


## Candidates

### C1: 0.16.0 candidate from `174cb31a4`, 23 November 2026

**Rejected: it loses players' settings.** C1 is the Windows x64 package of
`174cb31a41408f6ed398307f0075908086b8e752`, built in the release configuration
(stable track, version 0.16.0) in a detached worktree. Its source provenance
was verified, `package_release.py` made the runtime, update and symbols
archives, and the extracted runtime holds exactly the 439 files of
`openq4-0.16.0-windows-x64.zip` (manifest
`d88dd3f76fc2f8c58bebc7016a5a8cf22691ce7f83dd7af007390094969399f6`). Its version
line is `openQ4 0.16.0` with no commit, and the client's version resource
carries build 2662, the commit's history length. It ran on the NVIDIA GeForce
RTX 4060 Laptop GPU (driver 32.0.15.9174) with Intel Iris Xe beside it, against
a packs-only view of the Steam retail install.

| Case | Attempts | Result |
| --- | --- | --- |
| `rel01.package.layout` | pass | Layout, architecture and one version across engine and modules. |
| `rel01.ded.startup` | pass | 2 s, asset-free, loopback self-test. |
| `rel01.sp.save-load` | pass | airdefense1 save, restore, frozen and played resaves, all portable v5. |
| `rel01.sp.demo` | pass | Recorded in gameplay and played back with `timeDemoQuit`. |
| `rel01.install.clean` | fail, fail, fail | Attempts 1 and 2 were harness faults (a direct map start skips the menu; a `waitMsec` quit before the menu drew). Attempt 3, the player's order: the level start dropped all 21 multiplayer menu settings the engine declares, and the next menu start re-declared them at their defaults. |
| `rel01.install.upgrade` | fail | The official 0.15.0 package made the profile and save. Every player choice the profile could store survived, but 0.15.0 itself never saved the profile's `g_fov`, and the candidate replaced the default `ui_model "marine"` with `model_player_failed_transfer`. The restore of the 0.15.0 save was stopped by a harness fault (an informational "produced by build 2636" line matched its failure pattern). |

Three defects, all also in 0.15.0, came out of these runs:

1. `seta` never archived a cvar that no loaded code declares, so every
   multiplayer-only setting in a config (field of view, HUD options, bot and
   server options) was dropped by the next config write in a session that had
   not loaded the multiplayer module. A probe with `g_fov`, `hud_damageNumbers`
   and `hud_showSpeed` lost all three on the candidate and on 0.15.0.
2. The engine declared the multiplayer menu's 21 settings only after the
   configs ran. A saved value made a bare console cvar first, the declaration
   skipped it, and the retained Player Setup logged "Unavailable or invalid CVar
   source" for its rows.
3. An invalid stored player model fell back to the first model in the list,
   Quake 4's Failed Transfer monster, instead of the menu def's default.

The fix (`seta` archives cvars no code declares; the menu settings are
declared before the configs; the model falls back to the def's default) is in
`8016bc922`, pinned by `archived_settings_across_modules.py`.

The remaining quick cases also ran on C1, with fixes to the harnesses rather
than the product: `localization` passed; the dedicated-client, reconnect, IPv6,
pack-report, campaign-switch, Vulkan-recovery and generated-image cases stopped
at once because the runner created their output directory and they insist on
making it; `lifecycle` passed every phase but asked for an in-game switch from
OpenGL to Vulkan, which `r_renderApi` only makes at engine start;
`campaign-saves` passed eight of ten legs, and the other two (and
`effects.lifecycle`) timed out in the NVIDIA OpenGL driver's startup shader
compile (logs end at `R_ARB2_Init ... Available.`) while free commit memory sat
at 3 to 7 GB under other sessions' clients.

Two REL-04 package checks came out of C1 as well. Its symbols archive held the
PDBs of the client, dedicated server and game modules, but none for
`renderer-gl_x64.dll`, `renderer-vk_x64.dll` or `openQ4-updater_x64.exe`, so a
crash in a renderer module could not be symbolized from a release. The
packager and the stage validator now require all seven. Every Windows image is
unsigned (`NotSigned`), which the promotion record has to state.

C1 stays rejected, and every case runs again on the next candidate.
