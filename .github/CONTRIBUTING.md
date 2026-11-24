# Contributing to openQ4

Thank you for your interest in helping make openQ4 a complete, open-source
replacement for the Quake 4 engine and game code. Bug reports, compatibility
reports, testing feedback, documentation improvements, and code contributions are
all welcome.

If you just want to play, you do **not** need to build from source — download the
latest available release, [0.15.0](https://github.com/themuffinator/openQ4/releases/tag/v0.15.0).
The withdrawn 0.14.0 release is not a supported download; see
[release status](../docs/user/release-status.md).

---

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [How to Contribute](#how-to-contribute)
  - [Reporting Bugs](#reporting-bugs)
  - [When an Issue Waits for Your Reply](#when-an-issue-waits-for-your-reply)
  - [Reporting Compatibility Issues](#reporting-compatibility-issues)
  - [Suggesting Features or Improvements](#suggesting-features-or-improvements)
  - [Contributing Code](#contributing-code)
- [Development Workflow](#development-workflow)
  - [Prerequisites](#prerequisites)
  - [Building](#building)
  - [Project Structure Notes](#project-structure-notes)
  - [Before Submitting a Pull Request](#before-submitting-a-pull-request)
- [Style and Conventions](#style-and-conventions)
- [Licensing and Attribution](#licensing-and-attribution)
- [Getting Help](#getting-help)

---

## Code of Conduct

This project is governed by our [Code of Conduct](CODE_OF_CONDUCT.md). By
participating, you are expected to uphold it. Please report unacceptable behavior
to the maintainers.

---

## How to Contribute

### Reporting Bugs

Before opening a new issue, please search [existing
issues](https://github.com/themuffinator/openQ4/issues) to avoid duplicates.

[Open an issue](https://github.com/themuffinator/openQ4/issues/new/choose) with
the form that fits. Each asks for only a few required answers and says where to
find the rest:

- **Bug report**: something looks, sounds or behaves wrong in the game.
- **Crash or won't start**: openQ4 closes, freezes or never reaches the menu.
- **Low frame rate or stutter**: performance and loading times.
- **Idea or feature request**.
- **Building, mapping tools or modding**: source builds, dmap, AAS, the level
  editor and mod support.
- **VR headset**, **macOS crash**, **Linux ARM64** and **Windows ARM64**
  reports. Linux ARM64 and macOS are previews and Windows ARM64 is
  experimental; a report that everything worked is useful evidence for them,
  not only bug reports.

What helps most, whichever form you use:

- **The version from the log.** The first line of `openq4.log` names the exact
  build, which isn't always the one you think you installed.
- **A log.** openQ4 writes one only when asked: start with
  `+set logFile 2 +set logFileName logs/openq4.log`. It lands in
  `baseoq4/logs/` inside your save folder (`%LOCALAPPDATA%\openQ4` on Windows,
  `~/.local/share/openq4` on Linux and Steam Deck,
  `~/Library/Application Support/openQ4` on macOS). When launching from the
  repository, that is usually `.home/baseoq4/logs/openq4.log`.
- **Where it happens.** The level, the spot, and a screenshot or short video.
- **Mods and texture packs.** Whether the problem also happens without them.
- **For crashes**, `fatal.log` and the files in the `crashes` folder next to the
  executable on Windows. For a save that won't load, a zip of its `.save`, `.txt`
  and `.tga` files.

### When an Issue Waits for Your Reply

If we need more from you, such as a log, a retest or an answer, the issue is
labelled `status: awaiting reporter`. If six weeks pass without a reply from
you, and at least one new release has come out since we asked, we close the
issue. By then the problem may already be fixed, and a report against an old
version can't tell us either way.

That isn't a judgement on the report. If the problem still happens in the
current release, please open a new issue, link the old one, and attach a log
from the current version.

Maintainers can list the issues this applies to with
`python .github/scripts/stale-issues.py`. The script only reads; each issue is
checked by hand before it is closed. One that other players have since
confirmed, for example, stays open.

### Reporting Compatibility Issues

openQ4 targets the official Quake 4 retail assets and ships its own engine and
game modules. When reporting a compatibility problem:

- Confirm the issue reproduces with the original base PK4s, not only with custom
  content or third-party mods.
- Note whether the problem is specific to single-player, multiplayer, dedicated
  server, or a particular map.
- Avoid relying on repo-side `q4base/` overrides when validating; the long-term
  goal is to run cleanly with the original assets alone.

### Suggesting Features or Improvements

Feature suggestions are best opened with the **Idea or feature request** form.
To help discussion:

- Explain the player or developer benefit.
- Describe how it relates to stock-asset compatibility or project goals.
- Mention any prior art from Quake 4, the SDK, or other id Tech 4-based projects.

Please understand that openQ4 values compatibility with the shipped Quake 4
assets. Proposals that would break stock-map behavior or fragment the player base
will need extra justification.

### Contributing Code

1. Fork the repository and create a topic branch for your change.
2. Make focused commits with clear messages.
3. Follow the [Development Workflow](#development-workflow) and [Style and
   Conventions](#style-and-conventions) below.
4. Open a pull request referencing any related issue.

---

## Development Workflow

Use the [development roadmap](../docs/dev/roadmap.md) as the single active plan.
Relate work to its stable task IDs and update their scope, evidence and acceptance
when needed. Earlier plans are [archived](../docs/dev/archive/2026-10-18/README.md)
for reference; do not create a competing plan or TODO backlog. Technical contracts,
UI requirement registers and recorded release evidence retain their specific roles.

### Prerequisites

See [BUILDING.md](../BUILDING.md) for the full platform-specific requirements.
In short:

- **Windows:** MSVC 19.46+ and Meson 1.6.0+. Use `tools/build/meson_setup.ps1`
  from a regular PowerShell window instead of invoking `meson` directly.
- **Linux:** GCC 13+ or Clang 17+, Meson 1.6.0+, and Ninja.
- **macOS (preview):** Xcode 16+ / Clang 17+, Meson 1.6.0+, and Ninja.

### Building

See [BUILDING.md](../BUILDING.md) for detailed instructions. A typical local
build on Linux looks like:

```bash
meson setup builddir
meson compile -C builddir
meson install -C builddir --no-rebuild --skip-subprojects
```

On Windows, use the wrapper to ensure the MSVC toolchain is available:

```powershell
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 setup builddir
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 compile -C builddir
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects
```

### Project Structure Notes

- **Engine code:** lives under `src/`.
- **Game code:** maintained in `src/game/` and `src/game/` in this repository.
  Awakening single-player additions live under `src/game/awakening/` and apply
  only while that campaign's `q4xbase` content is active. The former companion
  repositories are historical references.
- **BSE code:** lives under `src/bse/` and is treated as first-party openQ4
  code. BSE is built into the client executable.
- **Runtime overrides:** authored overrides belong under `content/baseoq4/`. The
  `.install/baseoq4/` directory is the staged output, not an editing target.
- **Build output:** use `builddir/` for local development artifacts and
  `.install/` for staged runtime packages.
- **Layout and naming:** [Source Layout and Naming](../docs/dev/source-layout.md)
  describes every top-level directory, where vendored code and licence notices
  go, and how new files are named.

### Before Submitting a Pull Request

- Build your change locally and confirm the project still compiles.
- Run the relevant mode-specific launch task (SP for single-player, MP for
  multiplayer) and enter gameplay on a map affected by your change; main-menu
  startup alone is not sufficient validation.
- Check `fs_savepath/<gameDir>/logs/openq4.log` for new warnings or errors.
- Update documentation if your change affects player-facing behavior, build
  steps, or technical assumptions.
- Add or update release-note material in `docs/dev/release-completion.md` when
  the change is user-facing, packaging-related, or platform-related. Curated
  release notes for a version cut belong in `docs/dev/releases/vX.Y.Z.md`.
- Keep commits focused and rebased on the latest default branch when practical.

---

## Style and Conventions

- Match the existing style of the file you are editing.
- Prefer cross-platform abstractions through SDL3 rather than introducing new
  platform-specific code in shared engine paths.
- Do not add hardcoded user-facing UI strings when a `#str_*` localization lookup
  is possible.
- Avoid adding engine-side content files (materials, shaders, GUIs) unless
  absolutely required for compatibility. Prefer engine-side defaults or
  generated resources.
- Keep `baseoq4/` as the single unified game directory; do not split
  single-player and multiplayer into separate mod folders.
- Do not reintroduce an external `openQ4-BSE_<arch>` runtime module; BSE stays
  linked into the client executable.
- Maintain accurate credits and add new upstream attributions whenever you
  incorporate external work.

---

## Licensing and Attribution

- openQ4 engine code is licensed under the [GNU General Public License
  v3.0](../LICENSE).
- Game-library code under `src/game/` and `src/game/` is derived from the
  Quake 4 SDK and remains subject to id Software's SDK EULA.
- Quake 4 assets remain the property of id Software and ZeniMax Media.

Contributions must follow the licence of the component they modify. See
[LICENSING.md](../LICENSING.md) for the component boundaries, original notices,
and distribution considerations.

---

## Getting Help

- [Discord](https://discord.gg/T32mFejwR4): the best place for informal
  questions and community discussion.
- [Issue tracker](https://github.com/themuffinator/openQ4/issues): for bug
  reports, feature requests, and structured technical discussion.
- [Website](https://www.darkmatter-quake.com): project news and releases.

Thank you for helping keep Quake 4 playable on modern systems.
