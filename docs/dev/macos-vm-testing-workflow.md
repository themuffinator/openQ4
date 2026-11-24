# macOS VM Testing Workflow

This workflow is the macOS counterpart to `docs/dev/linux-mint-vmware-workflow.md`.
It is intentionally SSH-based instead of VMware-on-Windows based: macOS test VMs
must run on Apple-branded hardware or a compliant Apple-hosted service.

The YouTube guide in issue/debugging discussions (`TD3H2J65u90`) describes
installing macOS Sequoia in VMware on a Windows PC. Do not use that path for
openQ4 automation. Keep this workflow on Apple hardware through Virtualization
framework, UTM, VMware Fusion, Parallels, or a hosted Mac provider.

## Host And Media Layout

- Windows-side macOS installer/restore-image inventory: `E:\ISO\macos\`
- Guest VM name recommendation: `openQ4-macOS`
- Guest user recommendation: `codex`
- Guest workspace: `~/openq4-work/`
- Guest source checkout: `~/openq4-work/openQ4`
- Guest game sources: `~/openq4-work/openQ4/src/game`
- Guest Quake 4 assets: `~/openq4-work/Quake4`
- Guest results: `~/openq4-work/results/`

The guest scripts resolve `~` through `HOME` by default. If an SSH service,
launch agent, or hosted runner starts the scripts without `HOME`, pass
`-MacHome /Users/<name>` to the host workflow or set `OPENQ4_GUEST_HOME` when
running a guest script directly. The value must be an absolute macOS home
directory without control characters; otherwise the scripts fail before
creating work directories or launchers.
The host workflow applies the same control-character check inside its remote
archive-expansion and result-collection snippets when `-MacHome` is omitted and
they fall back to the SSH session's `HOME`.

`E:\ISO\macos\` is only an inventory/staging location on the Windows host. The
actual VM creation still happens on the Apple host with the hypervisor you use
there. Store Apple restore images, IPSW files, installer app archives, or a
short README that points to the Apple host's VM storage in this directory.

## Create The VM

On Apple hardware:

1. Create a macOS VM with Virtualization framework, UTM, VMware Fusion, or
   Parallels.
2. Allocate at least 4 vCPUs, 8 GB RAM, and 96 GB disk. Use 16 GB RAM if
   shader and packaging validation will run often.
3. Create a sudo-capable `codex` user.
4. Enable Remote Login:

   ```bash
   sudo systemsetup -setremotelogin on
   ```

5. Install Xcode Command Line Tools:

   ```bash
   xcode-select --install
   ```

6. Optional but recommended: install Homebrew from `https://brew.sh/`.

From the Windows host, check connectivity:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/macos/Invoke-openQ4MacOSWorkflow.ps1 `
  -Action Probe `
  -MacHost <mac-vm-ip-or-hostname> `
  -MacUser codex
```

## Bootstrap, Assets, Build, And Test

Run the full setup from the Windows host:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/macos/Invoke-openQ4MacOSWorkflow.ps1 `
  -Action All `
  -MacHost <mac-vm-ip-or-hostname> `
  -MacUser codex
```

`-Action All` does the following:

- verifies the macOS command-line toolchain, including `xcrun`, `plutil`,
  `lipo`, `otool`, and `codesign`
- installs or updates a user-local Meson/Ninja venv
- copies the Windows Quake 4 install into `~/openq4-work/Quake4`
- copies `openQ4`, including its canonical SP/MP sources, into the guest workspace
- configures, builds, and stages openQ4 through `tools/build/meson_setup.sh`
- runs a renderer smoke profile against the copied Quake 4 assets
- runs a multiplayer `mp/q4dm1` listen-server smoke profile against the copied Quake 4 assets
- runs the experimental macOS-facing GL 4.1 renderer validation set
- creates `~/Desktop/openQ4.command` pointing at the staged runtime and assets
- writes a bridge-specific runtime signoff report when running `All` or `Signoff`

Run narrower actions as needed:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/macos/Invoke-openQ4MacOSWorkflow.ps1 -Action Bootstrap -MacHost <host>
powershell -NoProfile -ExecutionPolicy Bypass -File tools/macos/Invoke-openQ4MacOSWorkflow.ps1 -Action Assets -MacHost <host>
powershell -NoProfile -ExecutionPolicy Bypass -File tools/macos/Invoke-openQ4MacOSWorkflow.ps1 -Action Sync -MacHost <host>
powershell -NoProfile -ExecutionPolicy Bypass -File tools/macos/Invoke-openQ4MacOSWorkflow.ps1 -Action Build -MacHost <host>
powershell -NoProfile -ExecutionPolicy Bypass -File tools/macos/Invoke-openQ4MacOSWorkflow.ps1 -Action Smoke -MacHost <host>
powershell -NoProfile -ExecutionPolicy Bypass -File tools/macos/Invoke-openQ4MacOSWorkflow.ps1 -Action Renderer -MacHost <host>
powershell -NoProfile -ExecutionPolicy Bypass -File tools/macos/Invoke-openQ4MacOSWorkflow.ps1 -Action Launcher -MacHost <host>
powershell -NoProfile -ExecutionPolicy Bypass -File tools/macos/Invoke-openQ4MacOSWorkflow.ps1 -Action Signoff -MacHost <host> -MacOSOSMatrixRole latest-public-macos
powershell -NoProfile -ExecutionPolicy Bypass -File tools/macos/Invoke-openQ4MacOSWorkflow.ps1 -Action CollectResults -MacHost <host> -MacOSRunId <run-id>
```

To test only the Metal bridge package path:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/macos/Invoke-openQ4MacOSWorkflow.ps1 `
  -Action Sync,Build,Smoke,Renderer `
  -MacHost <host> `
  -MacOSGraphicsBridge metal
```

For a self-contained hardware signoff pass after syncing, run both bridge
variants:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/macos/Invoke-openQ4MacOSWorkflow.ps1 `
  -Action Signoff `
  -MacHost <host> `
  -MacOSGraphicsBridge both `
  -MacOSOSMatrixRole latest-public-macos
```

The signoff action builds and stages each selected bridge before running the
single-player smoke profile, the multiplayer `mp/q4dm1` listen-server smoke
profile, and the experimental macOS-facing renderer matrix, installs the Desktop launcher,
records `sw_vers`, Xcode/macOS SDK, kernel, display, audio, USB, and Bluetooth inventory, and
writes bridge-specific reports under
`~/openq4-work/results/<timestamp>-signoff-opengl/macos-runtime-signoff.md` and
`~/openq4-work/results/<timestamp>-signoff-metal/macos-runtime-signoff.md`.
When `-MacOSGraphicsBridge both` is selected, OpenGL uses `builddir-opengl` and
Metal uses `builddir-metal` so Meson configuration state does not bleed between
variants. Use those reports for the manual hardware checklist: Finder/launcher
startup, real keyboard/mouse/controller input, audio output/device switching,
windowed and fullscreen display modes, HiDPI/Retina behavior, SP gameplay, MP
listen-server gameplay, dedicated-server startup where supported, and in-game
OpenGL or Metal bridge coverage beyond hosted CI before macOS support can move
from preview to first-class. The package UX part of that checklist follows
`docs/dev/macos-package-layout-and-release-policy.md`: launch `openQ4.app` from
the mounted signed/notarized DMG when one is available, drag only the app to
`/Applications` or another user-writable location and launch it there, copy the
whole payload separately to check loose client/server/support-tool sibling
discovery, confirm Terminal launch from an unrelated working directory, verify
embedded `Contents/Resources` and `Contents/Frameworks` paths plus
`fs_basepath`, `fs_cdpath`, and `fs_savepath` in runtime logs, and record
Gatekeeper behavior for the exact signed/notarized DMG or unsigned development
archive under test.

The `metal` bridge remains the OpenGL renderer path hosted through the SDL3/Cocoa
Metal bridge integration; it is not a native Metal renderer. Native
Cocoa/OpenGL backend runs with `-Dplatform_backend=native` are comparison-only
diagnostics and are not a supported release backend; they do not replace SDL3
OpenGL/Metal bridge package signoff.

Use `-MacOSOSMatrixRole floor-candidate` when validating the oldest documented
macOS floor, currently macOS 11, and `-MacOSOSMatrixRole latest-public-macos`
when validating the latest public macOS release. Hosted CI package runs may use
`current-hosted-ci-runner`, but hosted runner success is not a substitute for
floor/latest runtime signoff on compliant Apple hardware. Always pass an
explicit `-MacOSOSMatrixRole` on `Signoff` runs: omitting it leaves the guest
default `current-manual-signoff`, which counts toward neither the macOS floor
nor latest-public promotion evidence (see
`docs/dev/macos-support-matrix-policy.md`).

Guest paths passed through `-MacWorkspace` and `-MacBasePath` must be absolute POSIX paths or use a leading `~/`; the host workflow rejects relative, dot-segment, empty-segment, control-character, and backslash paths, and the guest scripts recheck that the paths are absolute and control-character-free after `~` expansion before syncing source trees, installing assets, building, or collecting results. Keep `-MacBasePath` out of the workflow-reserved `openQ4/`, `openQ4-game/`, `incoming-quake4/`, and `results/` children under `-MacWorkspace`; when `-MacHome` is provided, host preflight expands `~/` before that reserved-child comparison, and the guest scripts repeat the check after resolving the actual home directory. The asset installer refuses those targets because it stages extraction under the workspace and installs assets with `rsync --delete`. Source sync also trims trailing slashes before appending `openQ4/`, so a workspace written as `~/openq4-work/` does not create double-slash remote extraction targets.
Keep `-BuildDir` pointed at a dedicated build output directory such as
`builddir`, `builddir-opengl`, or `builddir-metal`; the guest script refuses the
repo root, source/content/tool trees, `.install`, symlinks, and files as Meson
build directories. Host-side validation now also requires `-BuildDir` to be a
relative `builddir*` path or a path under `.tmp/`, matching the local validation
profile's build-output convention.

Source and retail asset transfer archives are rechecked on the Apple
host before extraction. The workflow rejects malformed paths, control
characters, duplicate members, case-insensitive member collisions, macOS
metadata/debug sidecars, symlinks, hardlinks, and special files, then extracts
with `COPYFILE_DISABLE=1` so AppleDouble sidecars do not appear during sync.

After `Signoff` or `All`, the host workflow automatically copies the matching
guest result directories into a compressed archive at
`.tmp/macos-vm/results/openq4-macos-results-<run-id>.tar.gz`. Use
`-MacOSRunId <id>` to choose a stable collection prefix, `-ResultCollectionDir`
to store archives elsewhere, or `-SkipResultCollection` when the results should
remain only on the Apple host. The local copy-back is downloaded to a
same-directory temporary file, rejected if empty, and published without
overwriting an existing archive for that run ID. On the Apple host, the remote
tarball is also written to a same-directory temporary file, checked with
`tar -tzf`, and hard-linked into the final copy-back name without replacing an
existing target. The copied archive is validated automatically
unless `-SkipResultArchiveValidation` is set. The validator also requires the
archive filename to keep the same `<run-id>` as the result directories, so do
not rename the collected `.tar.gz` before recording signoff evidence.
Guest result directories must be new or empty before logging starts; if a
previous run left files under `<run-id>-<action>-<bridge>`, choose a fresh
`-MacOSRunId` or remove the stale directory before rerunning.

The automatic validation checks structure, both bridge reports, workflow logs,
staged payload evidence, binary architecture evidence, macOS system/device
inventory sections, OS matrix role, Xcode/macOS SDK evidence, asset-basepath evidence, single-player smoke output,
multiplayer `renderer-mp-smoke` output, and renderer matrix output.
Collection is intentionally limited to the expected
`<run-id>-signoff-<bridge>` directories so older build/smoke result directories
with the same run ID cannot be mixed into final signoff evidence. To rerun it
manually:

```powershell
python tools/macos/validate_signoff_archive.py .tmp/macos-vm/results/openq4-macos-results-<run-id>.tar.gz
```

After the manual hardware checklist has been completed in both bridge reports,
add `-RequireCompletedSignoffChecklist` to the host workflow, or
`--require-completed-checklist` to the manual validator command, to fail on any
remaining unchecked item.

If the manual checklist is completed on the Apple host after the original run,
copy and validate the existing results without rebuilding:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/macos/Invoke-openQ4MacOSWorkflow.ps1 `
  -Action CollectResults `
  -MacHost <host> `
  -MacOSRunId <run-id> `
  -MacOSGraphicsBridge both `
  -RequireCompletedSignoffChecklist
```

After the archive validates, generate and record the evidence index entry from
the Windows host:

```powershell
python tools/macos/record_signoff_evidence.py `
  .tmp/macos-vm/results/openq4-macos-results-<run-id>.tar.gz `
  --version vX.Y.Z `
  --package-artifact openq4-vX.Y.Z-macos-arm64-opengl.dmg `
  --package-artifact openq4-vX.Y.Z-macos-arm64-metal.dmg `
  --signing-status "signed and notarized DMGs" `
  --release-note-limitation "macOS support remains a preview for Apple Silicon/arm64" `
  --update-index
```

Use `--artifact-url` when the validated `.tar.gz` evidence lives in an issue,
release candidate, or external artifact store rather than only under `.tmp/`.
The recorder re-runs `tools/macos/validate_signoff_archive.py` with
`--require-completed-checklist`, computes the archive SHA-256, extracts the
bridge reports, verifies that package artifact names are safe `openQ4` macOS
arm64 filenames covering every requested graphics bridge, records the openQ4
and legacy game commit fields (the same openQ4 SHA for in-tree builds), updates `docs/dev/macos-signoff-evidence.md`, and
leaves the release-completion evidence gate open until curated release notes
link to the accepted record.

OpenAL Soft is the signoff and release audio provider. Pass
`-MacOSOpenALProvider system` and prepare the pinned dependency with
`tools/build/prepare_macos_openal_soft.sh`; packaged evidence must show the
signed `openQ4.app/Contents/Frameworks/libopenal.1.dylib`, package-relative
dependency, LGPL notice, and corresponding source archive. The Apple framework
option is compatibility-diagnostic coverage only. The selected provider is
recorded in the signoff report, and the complete provider policy is tracked in
`docs/dev/macos-openal-provider-policy.md`.

## Expected Validation

For macOS debugging, do not stop at static checks. Use this VM workflow for:

- `renderer_gameplay_benchmark.py --profile smoke`
- `renderer_gameplay_benchmark.py --profile smoke --cases mp-q4dm1-listen`
- `renderer_validation_matrix.py --tiers auto,gl41`
- staged launch from `~/Desktop/openQ4.command`
- `macos-runtime-signoff.md` from `-Action Signoff -MacOSGraphicsBridge both` when collecting hardware signoff evidence
- `.tmp/macos-vm/results/openq4-macos-results-<run-id>.tar.gz` copied back by the host workflow
- `tools/macos/validate_signoff_archive.py` run against the collected archive
- `-RequireCompletedSignoffChecklist` once manual hardware checks are filled in
- `-MacOSOSMatrixRole floor-candidate` for macOS floor evidence and `-MacOSOSMatrixRole latest-public-macos` for latest public macOS evidence
- `-Action CollectResults -MacOSRunId <run-id>` when re-collecting completed manual evidence
- mounted-DMG, independently dragged-app, whole-package loose-tool, terminal, embedded resource/module path, path-resolution log, and Gatekeeper checks from `docs/dev/macos-package-layout-and-release-policy.md`
- log inspection under the guest `~/openq4-work/results/` run directory

The remaining stock Quake 4 duplicate material warnings from retail assets are
not macOS VM setup failures. Investigate missing images, shader/link errors, GL
errors, PK4 mount errors, app bundle failures, dylib load errors, input/audio
device failures, and crashes.

## No Persistent Mac Yet

If no Apple VM or hosted Mac is available yet, use the manual GitHub Actions
workflow `.github/workflows/macos-debug.yml` as the interim macOS debug target.
It builds and stages the macOS OpenGL and/or Metal bridge variants on Apple's
hosted macOS runner, uploads `.install`, Meson logs, host diagnostics, and
optional assetless renderer-probe logs. Each selected artifact also includes
`macos-debug-evidence-scope.txt`, which records the requested bridge, the
artifact's actual bridge, exact engine and legacy game commit fields (both the openQ4 SHA), clean-tree
state, and whether the optional probes were enabled.
It explicitly marks hosted output as build/package evidence rather than a
completed manual Apple-hardware gameplay signoff.

Run it from GitHub Actions as **macOS Debug**. Use `bridge=both` for broad
coverage, or pick `opengl`/`metal` while chasing a focused issue. Enable
`run_runtime_assetless` only when startup/runtime logs are needed; hosted macOS
runner display capabilities can vary, so that step is allowed to fail while
still publishing whatever logs were produced. A successful artifact from one
selected bridge does not prove the other bridge; a `bridge=both` dispatch still
publishes one scoped artifact per bridge.
