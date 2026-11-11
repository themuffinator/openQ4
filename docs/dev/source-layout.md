# Source Layout and Naming

This is the contract for where code lives in the repository and how files and
folders are named. `tools/tests/repository_path_portability.py` enforces the
parts a script can check; the rest is review guidance.

## Repository Root

| Path | Contents |
| --- | --- |
| `src/` | Engine, game and tool sources |
| `src/external/` | Vendored third-party code, kept in its upstream layout |
| `subprojects/` | Meson wraps, with openQ4 patches under `subprojects/packagefiles/<project>/` |
| `content/baseoq4/` | Authored runtime overrides, packed into `pak0`/`pak1` |
| `assets/` | Packaging and branding inputs: icons, splash, desktop entries, cinematics, documentation images |
| `tools/` | Build, packaging, debugging, test, validation and asset scripts |
| `docs/dev/`, `docs/user/` | Developer and player documentation; superseded plans live in `docs/dev/archive/` |
| `LICENSES/` | Licence terms and third-party notices; mirrors the `licenses/` folder that packages install |
| `mobile/android/` | The Android launcher application |

The root itself holds only the project documents (`README.md`, `BUILDING.md`,
`TECHNICAL.md`, `LICENSING.md`, `CONTRIBUTORS.md`, `AGENTS.md`, `LICENSE`) and
the Meson entry points. Launch helpers belong in `tools/debug/`, and the single
development plan is `docs/dev/roadmap.md`. Build directories (`builddir*/`), the
staged package (`.install/`), save paths (`.home*/`) and scratch space (`.tmp/`)
are local and untracked.

## `src/`

| Directory | Contents |
| --- | --- |
| `aas` | AAS navigation runtime |
| `bse` | Basic Set of Effects, built into the client |
| `cm` | Collision model |
| `external` | Vendored code (see below) |
| `framework` | Common services, file system, cvars, declarations; `async` holds networking |
| `game`, `mpgame` | SDK-derived single-player and multiplayer game libraries; `game/awakening` holds the expansion |
| `idlib` | Core library |
| `imagetools` | CPU image codecs and colour handling shared by the engine and renderer modules |
| `mayaimport` | Interface to the optional Maya import DLL |
| `renderer` | Renderer front end and the `OpenGL`, `Vulkan`, `GLES` and `GLES_D3` backends |
| `rendergeo` | CPU render geometry shared by dmap and the renderer |
| `sound` | Sound system and its `OpenAL` backend |
| `sys` | Platform layer: `sdl3`, `posix`, `win32`, `linux`, `macos`, `android`, `openxr`, `stub` |
| `tools` | dmap and AAS compilers, the level editor, and the legacy MFC editors |
| `ui` | GUI system, the `retained` RmlUi interface and its `application` layer |

## Path Rules (Checked)

- Path components use only `A-Z a-z 0-9 . _ + -`, never end in a dot, and are
  never a Windows device name such as `con` or `aux`.
- No two tracked paths differ only by letter case. Windows and macOS keep one
  file per case-folded name, so a second spelling aliases the first.
- File extensions are lowercase.
- Paths stay within 160 characters, which leaves room for a worktree root under
  the Windows 260-character limit.
- First-level directories under `src/` are single lowercase words with no
  separators (`imagetools`, `rendergeo`, `mayaimport`).

## Naming Conventions

**C and C++ sources** follow the family the file belongs to. Do not rename
inherited files to a new style: the project compares them against the Quake 4
SDK, Doom 3 and decompiled references.

- Classes and subsystems use PascalCase named after their main type
  (`RenderWorld.cpp`, `LevelLoadPipeline.cpp`). Raven-era game files keep their
  `Prefix_Name` form (`AI_Manager.cpp`, `Monster_StroggFlyer.cpp`).
- Platform files carry a lowercase platform prefix: `win_`, `posix_`, `linux_`,
  `macos_` and `android_` (`win_main.cpp`, `macos_sdl3.cpp`).
- Inherited renderer files keep their lowercase stage prefixes (`tr_light.cpp`,
  `draw_arb2.cpp`). Backend translation units in renderer modules use an API
  prefix with a PascalCase name (`vk_Backend.cpp`, `gl_ContextSDL3.cpp`,
  `gles_Backend.cpp`). `GLES_D3` keeps the d3es layout it was ported from.
- Backend subdirectories are named for the API as it is written (`OpenGL`,
  `Vulkan`, `GLES`, `OpenAL`), following RBDOOM-3-BFG; other subdirectories are
  lowercase.
- Extensions are `.h`, `.cpp`, `.c`, `.mm` for Objective-C++ and `.inl` for
  inline bodies.

**Scripts and data**

- Python modules are snake_case. Tests in `tools/tests/` are named for the
  contract they check, and native test sources are `<Unit>Test.cpp` in
  `tools/tests/native/`.
- Entry points that exist for both shells share one snake_case name
  (`meson_setup.sh` and `meson_setup.ps1`). Windows-only PowerShell drivers may
  use Verb-Noun names (`Invoke-openQ4MacOSWorkflow.ps1`).
- Documentation and fixtures are lowercase-kebab (`source-layout.md`,
  `binding-smoke.rui`). Release notes are `docs/dev/releases/vX.Y.Z.md`.
- The project name is `openQ4` in mixed-case names and prose
  (`openQ4Installer.iss.in`), and `openq4` inside lowercase snake or kebab names
  (`openq4_pak.py`, `openq4.desktop.in`). Never `OpenQ4`.

## Vendored Code and Licences

Third-party code lives either in `src/external/<project>/` with its upstream
file names (`jpeg-6`, `stb_vorbis`, `openal-soft`, `smaa`, `vulkan`) or in
`subprojects/` as a Meson wrap. It follows only the checked path rules, not the
openQ4 naming conventions. Do not place vendored code inside first-party
directories.

`LICENSES/` holds the id Software terms (`DOOM-3-ADDITIONAL-TERMS.txt`,
`DOOM-3-BFG-ADDITIONAL-TERMS.txt`, `QUAKE-4-SDK-EULA.rtf`) and third-party
notices named after their project (`RmlUi.txt`, `glslang.txt`). Packages install
the same files into `licenses/`. Add a notice there when a vendored tree does
not carry its own.

## Unbuilt Legacy Sources

Some inherited sources stay in the tree although the Meson build does not
compile them, because tests still maintain their portability fixes:

- the Radiant and MFC editors under `src/tools/`, kept by the decision in
  [level-editor-modernization.md](level-editor-modernization.md);
- the native macOS backend in `src/sys/macos` (`DOOMController.mm`,
  `PreferencesDialog.cpp`, `macos_sound.cpp`);
- the OSS and ALSA sound backends in `src/sys/linux`;
- the Linux PK4 maintenance helpers in `src/sys/linux/pk4`, which
  `linux_pk4_legacy_tools.py` exercises.

The SCons build, the Loki and NSIS installers, the m4 GL generators and the
Doom 3 Xcode projects were removed in October 2026 because nothing referenced
them. Recover them from history with `git log --diff-filter=D -- src/sys`.
