# macOS Signoff Evidence Index

Updated: 2026-09-18

This index records accepted Apple macOS runtime signoff evidence for openQ4. It is intentionally lightweight: large `.tar.gz` signoff archives stay in `.tmp/` while work is active and should be attached to release evidence, issue discussion, or another external artifact store instead of being committed to the repository.

macOS stays a preview, not first-class, unless the current release entry below points to a completed-checklist archive that passes validation. The preview itself rests on hosted CI plus the community hardware reports recorded below; those reports are not accepted signoff archives and cannot promote macOS to first-class.

## Evidence Rules

- Use only compliant Apple hardware, a VM running on Apple hardware, or a hosted Apple Mac provider.
- Collect final evidence with `tools/macos/Invoke-openQ4MacOSWorkflow.ps1 -Action CollectResults -MacOSRunId <id> -MacOSGraphicsBridge both -RequireCompletedSignoffChecklist`.
- Keep `<id>` to 80 or fewer characters using only letters, digits, dots, underscores, or dashes, starting with a letter or digit.
- Validate the collected archive with `python tools/macos/validate_signoff_archive.py <archive> --require-completed-checklist`.
- Record accepted evidence with `python tools/macos/record_signoff_evidence.py <archive> --version vX.Y.Z --update-index` after adding package artifact names, signing status, and release-note limitations.
- Record the SHA-256 of the exact archive that passed validation.
- Record the openQ4 commit and game-source inventory used to build the modules.
  Reports retain the historical `openQ4-game` commit field for compatibility;
  current in-tree builds set both commit fields to the same openQ4 SHA.
- Record the architecture policy, actual CPU architecture, and OS matrix role for the run. Use `OPENQ4_MACOS_OS_MATRIX_ROLE=floor-candidate` for oldest-supported-version signoff and `OPENQ4_MACOS_OS_MATRIX_ROLE=latest-public-macos` for current public macOS signoff.
- Record Xcode and macOS SDK versions from the signoff report so CI/package evidence can be matched to the Apple toolchain that produced it.
- Record the OpenAL provider, package artifact names, signing/notarization status, and any user-facing limitation that release notes must mention.
- Record the matching macOS dSYM symbol archive names and confirm that `SYMBOLS.txt` from the package root matches the runtime artifacts under test.
- Do not promote macOS to first-class until evidence covers both the documented floor and the latest public macOS release, or the support matrix explicitly narrows the claim.
- Use the renderer/backend policy in `docs/dev/macos-renderer-backend-policy.md` and the containment policy in `docs/dev/macos-native-backend-containment-policy.md`: record OpenGL and Metal bridge evidence separately, do not treat the Metal bridge as native Metal, and do not treat `platform_backend=native` as release support evidence.
- Do not mark a release entry complete if either bridge report has open checklist items.
- Do not mark a release entry complete if SP, MP, Finder launch, terminal launch, input, audio, display, or package-layout checks were skipped without a documented exception.
- Use the package contract in `docs/dev/macos-package-layout-and-release-policy.md`: the client is a self-contained drag-installable app with data under `Contents/Resources/baseoq4` and signed modules under `Contents/Frameworks`.
- Record mounted-DMG launch, dragged-app launch, whole-package loose-tool launch, embedded path/module checks, path-resolution logs, and Gatekeeper assessment for the package artifacts under test.

## Current Status

- [ ] No completed macOS first-class support evidence is recorded yet.
- [ ] The next macOS signoff archive must include OpenGL and Metal bridge result directories.
- [ ] The next macOS signoff archive must include `renderer-smoke`, `renderer-mp-smoke`, and `renderer-matrix` evidence for each bridge.
- [ ] The next macOS signoff archive must include completed manual checklist items for SP, MP, Finder launch, terminal launch, input, audio, display, and package behavior.
- [ ] The next macOS signoff archive must include mounted-DMG, independently dragged-app, whole-package loose-tool, embedded resource/module path, `fs_basepath`/`fs_cdpath`/`fs_savepath`, and Gatekeeper evidence.
- [ ] The next macOS signoff archive must include architecture policy, CPU architecture, OS matrix role, Xcode version, and macOS SDK version.
- [ ] The next macOS signoff archive must include the engine and legacy game commit fields in each bridge report, both naming the same openQ4 SHA for an in-tree build.
- [ ] The next macOS signoff archive must keep OpenGL and Metal bridge renderer evidence separate and must not describe the Metal bridge as native Metal.
- [ ] Before first-class promotion, accepted evidence must include both a macOS floor-version signoff and a latest-public-macOS signoff for the current Apple Silicon/arm64 matrix.
- [ ] `docs/dev/release-completion.md` must reference the accepted evidence before macOS support claims are promoted.

## Current Release Evidence

Use this block for the active release candidate. Replace placeholder values only after the archive validates.

- Release/version:
- Support tier: preview
- Evidence status: pending
- Run ID:
- Archive path or external artifact URL:
- Archive SHA-256:
- Validator command:
- Validator result:
- openQ4 commit:
- `openQ4-game` commit:
- Package artifacts:
- Symbol artifacts:
- `SYMBOLS.txt` manifest:
- Signing/notarization status:
- Architecture policy:
- Architecture:
- CPU architecture:
- OS matrix role:
- macOS floor evidence:
- Latest public macOS evidence:
- macOS version:
- Xcode version:
- macOS SDK version:
- Kernel:
- Hardware model:
- CPU:
- GPU/display:
- OpenAL provider:
- Graphics bridges tested:
- SP coverage:
- MP coverage:
- Dedicated server coverage:
- Finder launch coverage:
- Terminal launch coverage:
- Package layout contract:
- Mounted DMG launch coverage:
- Copied package launch coverage:
- App-only move behavior:
- Path resolution log coverage:
- Gatekeeper assessment:
- Input devices:
- Audio devices:
- Display modes:
- Known exceptions:
- Required release-note limitations:

Current release checklist:

- [ ] Archive validates with `--require-completed-checklist`.
- [ ] OpenGL report is present and complete.
- [ ] Metal bridge report is present and complete.
- [ ] `renderer-smoke` output exists for OpenGL.
- [ ] `renderer-smoke` output exists for Metal.
- [ ] `renderer-mp-smoke` output exists for OpenGL.
- [ ] `renderer-mp-smoke` output exists for Metal.
- [ ] `renderer-matrix` output exists for OpenGL.
- [ ] `renderer-matrix` output exists for Metal.
- [ ] SP entered an in-game map on real Apple hardware.
- [ ] MP loaded the game module as `game_mp`, started `mp/q4dm1`, connected a local client, and exited cleanly.
- [ ] Dedicated server startup was covered or explicitly documented as unsupported for this release.
- [ ] Finder or Desktop launcher startup was checked.
- [ ] Terminal startup was checked.
- [ ] Mounted signed/notarized DMG launch was checked, or unsigned archive behavior was recorded as an unsigned-package exception.
- [ ] Dragging only `openQ4.app` to `/Applications` or another user-writable location was checked.
- [ ] Whole-package copied launch was checked for loose client, dedicated-server, and support-tool sibling-runtime discovery.
- [ ] `fs_basepath`, `fs_cdpath`, and `fs_savepath` were confirmed in logs for Finder/copied package and terminal launches.
- [ ] Gatekeeper assessment was checked for signed/notarized DMGs, or unsigned/unnotarized approval friction was recorded for development archives.
- [ ] Keyboard and mouse input were checked.
- [ ] Controller hotplug/rumble was checked, or unavailable hardware was recorded as an exception.
- [ ] Audio output, volume changes, and at least one device switch or reconnect were checked.
- [ ] Windowed, fullscreen, selected-display, and HiDPI/Retina behavior were checked.
- [ ] The app contained data under `Contents/Resources/baseoq4` and the signed game module under `Contents/Frameworks`, with no adjacent `baseoq4` duplicate.
- [ ] Matching `openq4-<version>-macos-arm64-<bridge>-symbols.tar.xz` dSYM archives were recorded and matched against package `SYMBOLS.txt` manifests.
- [ ] First-class macOS release artifacts are signed/notarized DMGs, or the release stays a preview and unsigned artifacts are labeled as fallback output.
- [ ] Architecture policy, CPU architecture, and OS matrix role were recorded.
- [ ] Xcode and macOS SDK versions were recorded.
- [ ] macOS floor-version signoff was covered or documented as still required.
- [ ] Latest public macOS signoff was covered or documented as still required.
- [ ] Release notes mention that the Metal package is a bridge, not native Metal.
- [ ] Release notes do not imply that the native Cocoa/OpenGL backend is supported for release packages.
- [ ] Release notes mention arm64-only support.
- [ ] Release notes mention unsigned/unnotarized package behavior if unsigned artifacts are published.
- [ ] Release notes mention any renderer, audio, input, package, or MP limitation found during signoff.

## Community Hardware Reports

These are reports from players on real Apple hardware, taken from GitHub issues. They are what moved macOS from experimental to preview on 2026-09-18. None of them is an accepted signoff archive: no checklist was run, no archive was validated, and none counts toward first-class promotion. Add newer reports at the top, and remove the preview if a current release stops reaching gameplay on the latest public macOS.

| Date | Release and package | Hardware and macOS | Result | Source |
| --- | --- | --- | --- | --- |
| 2026-09-13 | 0.13.1, `openq4-0.13.1-macos-arm64-opengl-unsigned` | MacBook Air (M2, `Mac14,15`), macOS 26.6 | New Game from the main menu played for about five minutes. Scripted loads of `game/mcc_1` and `game/airdefense1` reached gameplay with no errors and wrote autosaves. OpenAL Soft 1.25.1 loaded with EFX and an HRTF available, and sound was confirmed audible by ear. Exclusive fullscreen ran at the native 2880x1864. | [#122](https://github.com/themuffinator/openQ4/issues/122) |
| 2026-09-03 | 0.12.0, clean install | MacBook Pro (M4 Max), macOS 26.6 | Multiplayer started with sound and without the washed-out lighting of earlier builds, and the reporter said the Vulkan renderer worked too. Single-player hit the Apple OpenAL buffer exhaustion that 0.13.0 fixed. | [#98](https://github.com/themuffinator/openQ4/issues/98) |
| 2026-08-13 | 0.8.0 | Apple Silicon, two reporters | The launch `SIGSEGV` was gone, as summarized when the issue was closed. | [#73](https://github.com/themuffinator/openQ4/issues/73) |
| 2026-08-12 | 0.10.0, built from source | iMac (2020, Intel), macOS 26 (first reported on 15.7.7) | The launch `SIGABRT` reported since 0.8.0 was gone. Intel is still not a published target. | [#90](https://github.com/themuffinator/openQ4/issues/90) |

Still unreported on real hardware: any macOS older than 26, the Metal bridge package on a current release, multiplayer on 0.13.x, the dedicated server with stock maps, controllers, audio device switching, and signed-package Gatekeeper behavior.

## Evidence History

No accepted completed-checklist macOS signoff archive has been recorded yet.

Add new completed evidence records above this line, newest first, using the template below.

## Completed Evidence Template

```markdown
### vX.Y.Z - YYYY-MM-DD - <run-id>

- Release/version:
- Support tier:
- Run ID:
- Archive path or external artifact URL:
- Archive SHA-256:
- Validator command:
- Validator result:
- openQ4 commit:
- `openQ4-game` commit:
- Package artifacts:
- Symbol artifacts:
- `SYMBOLS.txt` manifest:
- Signing/notarization status:
- Architecture policy:
- Architecture:
- CPU architecture:
- OS matrix role:
- macOS floor evidence:
- Latest public macOS evidence:
- macOS version:
- Xcode version:
- macOS SDK version:
- Kernel:
- Hardware model:
- CPU:
- GPU/display:
- OpenAL provider:
- Graphics bridges tested:
- SP coverage:
- MP coverage:
- Dedicated server coverage:
- Finder launch coverage:
- Terminal launch coverage:
- Package layout contract:
- Mounted DMG launch coverage:
- Copied package launch coverage:
- App-only move behavior:
- Path resolution log coverage:
- Gatekeeper assessment:
- Input devices:
- Audio devices:
- Display modes:
- Known exceptions:
- Required release-note limitations:

Checklist:

- [ ] Archive validates with `--require-completed-checklist`.
- [ ] OpenGL and Metal bridge reports are present.
- [ ] `renderer-smoke`, `renderer-mp-smoke`, and `renderer-matrix` output exists for both bridges.
- [ ] Manual hardware checklist is complete in both bridge reports.
- [ ] Mounted-DMG, independently dragged-app, whole-package loose-tool, embedded resource/module path, path-resolution log, and Gatekeeper package UX checks are complete or documented as exceptions.
- [ ] Matching macOS dSYM symbol archives and `SYMBOLS.txt` manifests are recorded for each tested runtime package.
- [ ] Architecture policy, CPU architecture, OS matrix role, Xcode version, and macOS SDK version are recorded.
- [ ] macOS floor and latest-public-macOS coverage are recorded or called out as remaining blockers for first-class support.
- [ ] Renderer/backend limitations from `docs/dev/macos-renderer-backend-policy.md` are reflected in release completion and curated release notes.
- [ ] Release completion links to this evidence.
- [ ] Curated release notes reflect the tested support level and limitations.
```
