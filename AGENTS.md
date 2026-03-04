# OpenQ4 Agent Guide

This file describes project goals, rules, and upstream credits for anyone working on OpenQ4.

**Project Metadata**
- Name: OpenQ4
- Author: themuffinator
- Company: DarkMatter Productions
- Version: 0.0.1
- Website: `www.darkmatter-quake.com`
- Repository: `https://github.com/themuffinator/OpenQ4`
- Companion GameLibs Repo (local): `E:\Repositories\OpenQ4-GameLibs`
- Companion BSE Repo (local, closed-source): `E:\Repositories\OpenQ4-BSE`

**Goals**
- Deliver a complete, open-source code replacement for Quake 4 (engine + game code).
- Preserve behavior required by the shipped Quake 4 assets (base PK4s) where practical.
- Maintain full single-player and multiplayer parity with in-tree game code.
- Modernize the engine and game code while keeping stock-asset compatibility as a guiding constraint.
- Package both SP/MP under one unified game directory (`openq4/`) with `game_sp` + `game_mp`.
- Establish a cross-platform foundation targeting modern systems (Windows, Linux, macOS; x64 first) through SDL3 and Meson.
- Keep Quake4SDK-derived game-library source ownership in `OpenQ4-GameLibs`, with OpenQ4 consuming synchronized mirrors for local engine/game builds.
- Keep closed-source BSE implementation ownership in `OpenQ4-BSE`, with OpenQ4 consuming sources directly from that companion repo at build time.

**Rules**
- Do not target compatibility with the proprietary Quake 4 game DLLs; OpenQ4 ships its own game modules and keeps full freedom to evolve the project.
- Treat `E:\Repositories\OpenQ4-GameLibs` as part of the same development workspace for planning, edits, and validation.
- For SDK/game-library work, make canonical source edits in `OpenQ4-GameLibs` first; OpenQ4 `src/game` is synchronized from that repo via `tools/build/sync_gamelibs.ps1`.
- Treat `E:\Repositories\OpenQ4-BSE` as the canonical BSE source location.
- For BSE work, make canonical edits in `OpenQ4-BSE` first; OpenQ4 builds BSE directly from that repo and does not mirror BSE implementation sources in-tree.
- OpenQ4 may only contain BSE API wiring (`src/bse_api/`); do not add closed-source BSE implementation code to this repository.
- Use Meson option `-Dbuild_libbse=true|false` (default `true`) to control whether `OpenQ4-BSE_<arch>` is built in OpenQ4.
- Use `OPENQ4_BSE_REPO=<path>` to override the default companion BSE repository location (`../OpenQ4-BSE`) when configuring/building OpenQ4.
- Keep `openq4/` as the single unified game directory; do not split SP/MP into separate mod folders.
- Prefer changes that match Quake 4 SDK expectations and shipped content behavior.
- Document significant changes in the documentation and keep `README.md` accurate.
- Use `builddir/` as the standard Meson build output directory for local builds, VS Code tasks, and launch configurations.
- Treat `.install/` as the release-style package root; stage built binaries into `.install/` and `.install/openq4/`.
- Keep game-module outputs available under both `builddir/openq4/` (direct run) and `.install/openq4/` (staged package); keep `OpenQ4-BSE_<arch>` next to engine executables in `builddir/` and `.install/`.
- Keep `.install/` focused on runtime/staged content: engine executables and `OpenQ4-BSE_<arch>` in `.install/`, game DLLs and staged overrides/assets in `.install/openq4/`.
- Do not rely on `.install/` as a linker artifact store; keep compiler/linker intermediates and development-only outputs in `builddir/`.
- MSVC import libraries (`*.lib`) are not runtime requirements for OpenQ4 execution; prefer keeping them in `builddir/` (or other developer artifact output), not in release-style `.install/` packages.
- Use `meson install -C builddir --no-rebuild --skip-subprojects` (via `tools/build/meson_setup.ps1`) when staging `.install/` to avoid third-party subproject installs outside the package tree.
- `tools/build/meson_setup.ps1` now coordinates companion repo workflows: it syncs game sources from `../OpenQ4-GameLibs`, and can trigger SDK/game-library builds there during `compile` when `OPENQ4_BUILD_GAMELIBS=1`.
- On Windows, do not invoke raw `meson ...` from an arbitrary shell; use `tools/build/meson_setup.ps1 ...` (or run `tools/build/openq4_devcmd.cmd` first) so `cl.exe`/MSVC tools are always available.
- Prefer platform abstractions through SDL3 and avoid introducing new platform-specific dependencies in shared engine code when an SDL3 path exists.
- Keep Meson as the primary build entry point and keep dependency management through Meson subprojects.
- Treat x64 as the baseline architecture for active support while staging additional modern architectures incrementally.
- Keep credits accurate and add new attributions when incorporating upstream work.
- Localize all user-facing UI text: never introduce hardcoded display strings in GUIs/UI code when a `#str_*` lookup is possible; add/update language-table entries whenever new text is needed.
- Avoid adding engine-side content files (e.g., custom material scripts) unless absolutely required for compatibility; the goal is to run with the original game assets and only OpenQ4 binaries (engine + game modules, plus minimal external libs).
- Any existing custom `q4base/` content is treated as an expedient bootstrap, not a long-term solution. The goal is to remove this reliance by fixing engine compatibility issues rather than shipping replacement assets.
- For investigations, reference the log file written by `logFileName` (VS Code launch uses `logs/openq4.log`), located under `fs_savepath\<gameDir>\` (e.g. `${workspaceFolder}\\.home\\openq4\\logs\\openq4.log`).
- For runtime validation, use mode-specific launch tasks: use the SP launch task for single-player testing and the MP launch task for multiplayer testing.
- Never launch a multiplayer map while running in single-player mode; multiplayer maps must only be launched via multiplayer mode/tasks.
- Do not treat main-menu startup as sufficient validation; enter in-game/map gameplay relevant to the change before concluding tests.
- Use `.tmp/` directory in repository for any temporary files required for tasks.

**.install/ Folder Layout (Staging Target)**
- `.install/` is the runtime package root used by local staging and `fs_cdpath` overlays.
- Keep executable/runtime artifacts here (for example `.install/OpenQ4-client_x64.exe`, `.install/OpenQ4-ded_x64.exe`, `.install/OpenQ4-BSE_x64.dll`, `.install/openq4/game-sp_x64.dll`, `.install/openq4/game-mp_x64.dll`).
- Stage editable override content under `.install/openq4/` (for example GUI scripts in `.install/openq4/guis/`).
- Avoid shipping build-only linker artifacts in `.install/`; keep `*.lib` in `builddir/` unless intentionally producing a developer SDK artifact set.

**Development Procedure (Correct Direction)**
1. Develop against the installed Quake 4 assets only (base PK4s), not repo `q4base/` content.
2. Prefer launching from the repo `.install/` directory so locked `fs_cdpath` targets staged OpenQ4 overlays; use a different working directory only when intentionally testing stock-only behavior.
3. If something is missing or broken, fix the engine/game/loader/parser rather than shipping new material/decl/shader assets.
4. If engine-side shaders are needed, prefer internal defaults or generated resources that ship with the executable.
5. Re-run Procedure 1 after each fix to verify clean initialization without custom content.

**Procedure 1 (Debug Loop)**
1. Launch using the correct mode-specific task (`SP` launch task for single-player, `MP` launch task for multiplayer).
2. Close the game after 3 seconds.
3. Read `fs_savepath\<gameDir>\logs\openq4.log` (commonly `${workspaceFolder}\\.home\\openq4\\logs\\openq4.log`).
4. Identify errors and warnings to resolve.
5. Resolve the errors and warnings.
6. Repeat until clean.

**Planned Review**
- Strong preference to review and reduce `q4base/` usage, file-by-file, until the engine runs cleanly without any repo content overrides.

**References (Local, Not Included In Repo)**
- Quake 4 SDK: `E:\_SOURCE\_CODE\Quake4-1.4.2-SDK`
- Upstream engine base (local folder name retained): `E:\_SOURCE\_CODE\Quake4Doom-master`
- Quake 4 BSE (Basic Set of Effects): `E:\_SOURCE\_CODE\Quake4BSE-master`
- Quake 4 engine decompiled (Hex-Rays): `E:\_SOURCE\_CODE\Quake4Decompiled-main`
- Quake 4 installation (Steam): `C:\Program Files (x86)\Steam\steamapps\common\Quake 4`

**Upstream Credits**
- Justin Marshall.
- Robert Backebans.
- id Software.
- Raven Software.

