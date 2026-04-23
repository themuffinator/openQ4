project(
  'OpenQ4',
  ['c', 'cpp'],
  version: '0.1.010',
  meson_version: '>=1.2.0',
  default_options: ['c_std=c11', 'cpp_std=c++20', 'b_vscrt=static_from_buildtype'],
)

host_system = host_machine.system()
host_cpu_family = host_machine.cpu_family()
if not ['windows', 'linux', 'darwin'].contains(host_system)
  error('Unsupported host system for OpenQ4 Meson build: ' + host_system)
endif

binary_arch = host_cpu_family
if host_cpu_family == 'x86_64'
  binary_arch = 'x64'
elif host_cpu_family == 'x86'
  binary_arch = 'x86'
elif host_cpu_family == 'aarch64'
  binary_arch = 'arm64'
endif
game_sp_binary_name = 'game-sp_' + binary_arch
game_mp_binary_name = 'game-mp_' + binary_arch
engine_client_binary_name = meson.project_name() + '-client_' + binary_arch
engine_ded_binary_name = meson.project_name() + '-ded_' + binary_arch

if host_cpu_family != 'x86_64'
  warning(
    'OpenQ4 currently targets x64 as the active compatibility baseline. '
    + 'Non-x64 builds are considered experimental during platform bring-up.'
  )
endif

platform_backend_requested = get_option('platform_backend')
platform_backend = platform_backend_requested
if host_system == 'windows' and platform_backend_requested == 'native'
  error('platform_backend=native is only valid on non-Windows hosts.')
endif

if host_system == 'linux'
  if platform_backend_requested == 'legacy_win32'
    error('platform_backend=legacy_win32 is only valid on Windows hosts.')
  endif
elif host_system == 'darwin'
  if platform_backend_requested == 'legacy_win32'
    error('platform_backend=legacy_win32 is only valid on Windows hosts.')
  endif

  if platform_backend_requested == 'sdl3'
    message(
      'platform_backend=sdl3 on macOS currently maps to native platform sources while SDL3 migration work is implemented.'
    )
    platform_backend = 'native'
  endif
elif host_system != 'windows'
  error('Unsupported host system for platform backend selection: ' + host_system)
endif

use_sdl3_backend = platform_backend == 'sdl3'
if host_system == 'windows' and not use_sdl3_backend
  warning(
    'legacy_win32 backend is transitional. '
    + 'Use -Dplatform_backend=sdl3 for portability-focused development.'
  )
endif

python_mod = import('python')
py = python_mod.find_installation('python3', required: true)
fs_mod = import('fs')
if host_system == 'darwin'
  add_languages('objc', 'objcpp', required: true)
  macos_deployment_target = '11.0'
  add_project_arguments(
    '-mmacosx-version-min=' + macos_deployment_target,
    language: ['c', 'cpp', 'objc', 'objcpp'],
  )
  add_project_link_arguments(
    '-mmacosx-version-min=' + macos_deployment_target,
    language: ['c', 'cpp', 'objc', 'objcpp'],
  )
endif

version_track = get_option('version_track').strip()
version_iteration = get_option('version_iteration').strip()
version_base_override = get_option('version_base_override').strip()
openal_root_override = get_option('openal_root_override').strip()
generated_version_header = meson.current_build_dir() / 'openq4_version_generated.h'
version_metadata_command = [
  py,
  meson.project_source_root() / 'tools' / 'build' / 'openq4_version.py',
  '--source-root',
  meson.project_source_root(),
  '--track',
  version_track,
  '--header-out',
  generated_version_header,
]
if version_base_override != ''
  version_metadata_command += ['--base-version', version_base_override]
else
  version_metadata_command += ['--base-version', meson.project_version()]
endif
if version_iteration != ''
  version_metadata_command += ['--iteration', version_iteration]
endif
version_metadata_result = run_command(version_metadata_command, check: true)

openq4_version_short = meson.project_version()
openq4_version_full = meson.project_version()
openq4_version_tag = meson.project_version()
openq4_product_version = meson.project_name() + ' ' + meson.project_version()
openq4_product_version_full = openq4_product_version
openq4_version_resource_dotted = meson.project_version() + '.0'
openq4_git_sha = ''
openq4_git_dirty = '0'
openq4_commit_count = '0'
root_include_dir = include_directories('.')

foreach version_line : version_metadata_result.stdout().strip().split('\n')
  if version_line == ''
    continue
  endif

  version_pair = version_line.split('=')
  if version_pair.length() != 2
    continue
  endif

  version_key = version_pair[0]
  version_value = version_pair[1]
  if version_key == 'version_short'
    openq4_version_short = version_value
  elif version_key == 'version'
    openq4_version_full = version_value
  elif version_key == 'version_tag'
    openq4_version_tag = version_value
  elif version_key == 'product_version'
    openq4_product_version = version_value
  elif version_key == 'product_version_full'
    openq4_product_version_full = version_value
  elif version_key == 'resource_dotted'
    openq4_version_resource_dotted = version_value
  elif version_key == 'git_sha'
    openq4_git_sha = version_value
  elif version_key == 'git_dirty'
    openq4_git_dirty = version_value
  elif version_key == 'commit_count'
    openq4_commit_count = version_value
  endif
endforeach

build_engine = get_option('build_engine')
build_games = get_option('build_games')
build_game_sp = get_option('build_game_sp')
build_game_mp = get_option('build_game_mp')
needs_game_headers = build_engine or build_games
install_root_dir = meson.project_source_root() / '.install'
install_game_dir = install_root_dir / 'baseoq4'
game_libs_repo_root = ''
game_libs_source_root = ''
game_libs_repo_summary = 'not required'
game_stage_root = ''
game_stage_game_root = ''
game_stage_game_include_rel = '.tmp/gamelibs_stage/src/game'
bse_source_root = meson.project_source_root() / 'src' / 'bse'
game_module_defs_file = ''

if not build_engine and not build_games
  error('At least one build target must be enabled. Use -Dbuild_engine=true and/or -Dbuild_games=true.')
endif

if needs_game_headers
  game_libs_repo_discovery = run_command(
    py,
    '-c',
    'import os, pathlib, sys; root = pathlib.Path(sys.argv[1]); raw = os.environ.get("OPENQ4_GAMELIBS_REPO", "").strip(); repo = pathlib.Path(raw) if raw else (root / ".." / "OpenQ4-GameLibs"); print(repo.resolve().as_posix())',
    meson.project_source_root(),
    check: true,
  )
  game_libs_repo_root = game_libs_repo_discovery.stdout().strip()
  if game_libs_repo_root == ''
    error('Failed to resolve the OpenQ4-GameLibs repository path. Set OPENQ4_GAMELIBS_REPO or place it at ../OpenQ4-GameLibs.')
  endif

  game_libs_source_root = game_libs_repo_root / 'src'
  if not fs_mod.is_dir(game_libs_source_root / 'game')
    error('OpenQ4-GameLibs source directory not found: ' + game_libs_source_root / 'game')
  endif

  game_stage_result = run_command(
    py,
    meson.project_source_root() / 'tools' / 'build' / 'stage_gamelibs.py',
    meson.project_source_root(),
    game_libs_repo_root,
    meson.project_source_root() / '.tmp' / 'gamelibs_stage',
    check: true,
  )
  game_stage_root = game_stage_result.stdout().strip()
  if game_stage_root == ''
    error('Failed to stage game sources from OpenQ4-GameLibs.')
  endif

  game_stage_game_root = game_stage_root / 'src' / 'game'
  if not fs_mod.is_dir(game_stage_game_root)
    error('Staged game source directory not found: ' + game_stage_game_root)
  endif

  game_module_defs_file = game_stage_game_root / 'Game.def'
  if host_system == 'windows' and not fs_mod.is_file(game_module_defs_file)
    error('Staged Game.def not found: ' + game_module_defs_file)
  endif

  game_libs_repo_summary = game_libs_repo_root
endif

if build_engine and not fs_mod.is_dir(bse_source_root)
  error('Integrated BSE source directory not found: ' + bse_source_root)
endif

engine_source_discovery = run_command(
  py,
  meson.project_source_root() / 'tools' / 'build' / 'meson_sources.py',
  '--host-system',
  host_system,
  '--platform-backend',
  platform_backend,
  '--include-game',
  'false',
  check: true,
)

engine_source_paths = engine_source_discovery.stdout().strip().split('\n')
if engine_source_paths.length() == 0
  error('tools/build/meson_sources.py returned no engine source files.')
endif

openq4_engine_sources = files(engine_source_paths)

game_sources = []
bse_sources = []
game_idlib_sources = []
if build_games
  game_source_discovery = run_command(
    py,
    meson.project_source_root() / 'tools' / 'build' / 'list_sources.py',
    game_stage_root,
    'src/game',
    'src/game/Callbacks.cpp',
    'src/game/gamesys/Callbacks.cpp',
    check: true,
  )
  game_source_paths = game_source_discovery.stdout().strip().split('\n')
  if game_source_paths.length() == 0
    error('tools/build/list_sources.py returned no game source files.')
  endif

  game_absolute_paths = []
  foreach rel_path : game_source_paths
    game_absolute_paths += [game_stage_root / rel_path]
  endforeach

  game_sources = files(game_absolute_paths)
endif

if build_engine
  bse_source_discovery = run_command(
    py,
    meson.project_source_root() / 'tools' / 'build' / 'list_sources.py',
    meson.project_source_root(),
    'src/bse',
    check: true,
  )
  bse_source_paths = bse_source_discovery.stdout().strip().split('\n')
  if bse_source_paths.length() == 0
    error('tools/build/list_sources.py returned no BSE source files.')
  endif
  bse_sources = files(bse_source_paths)
endif

if build_engine or build_games
  game_idlib_discovery = run_command(
    py,
    meson.project_source_root() / 'tools' / 'build' / 'list_sources.py',
    meson.project_source_root(),
    'src/idlib',
    'src/idlib/math/Simd_AltiVec.cpp',
    check: true,
  )
  game_idlib_paths = game_idlib_discovery.stdout().strip().split('\n')
  if game_idlib_paths.length() == 0
    error('tools/build/list_sources.py returned no game idlib source files.')
  endif
  game_idlib_sources = files(game_idlib_paths)
endif

resource_compiler = find_program('rc', 'windres', required: false)
if host_system == 'windows' and resource_compiler.found()
  windows_mod = import('windows')
  openq4_engine_sources += windows_mod.compile_resources(
    'src/sys/win32/rc/OpenQ4Version.rc',
    include_directories: [root_include_dir],
  )
endif

cc = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')

is_msvc = cc.get_id() == 'msvc' and cpp.get_id() == 'msvc'
msvc_2026_min = '19.46.0'
if is_msvc
  if cpp.version().version_compare('<' + msvc_2026_min)
    msvc_2026_msg = 'MSVC 19.46+ (Visual Studio 2026 toolset baseline) is recommended. Detected: ' + cpp.version() + '.'
    if get_option('enforce_msvc_2026')
      error(msvc_2026_msg)
    else
      warning(msvc_2026_msg + ' Configure with -Denforce_msvc_2026=true to enforce this baseline.')
    endif
  endif
elif get_option('enforce_msvc_2026')
  warning('enforce_msvc_2026 is only meaningful when using the MSVC toolchain.')
endif

fallback_opt = ['default_library=static']

glew_dep = disabler()
stb_vorbis_dep = disabler()
openal_dep = disabler()
sdl3_dep = disabler()
deps = []
if build_engine
  glew_dep = dependency(
    'glew',
    required: true,
    fallback: ['glew', 'glew_dep'],
    default_options: fallback_opt,
  )

  stb_vorbis_dep = dependency(
    'stb_vorbis',
    required: true,
    fallback: ['stb_vorbis', 'stb_vorbis_dep'],
    default_options: fallback_opt,
  )

  if host_system == 'darwin'
    openal_dep = dependency('appleframeworks', modules: ['OpenAL'], required: true)
  else
    if host_system == 'windows' and openal_root_override != ''
      openal_override_root = openal_root_override
      openal_override_include_dir = openal_override_root / 'include'
      if cc.get_argument_syntax() == 'msvc'
        openal_override_include_args = ['/I', openal_override_include_dir]
      else
        openal_override_include_args = ['-I', openal_override_include_dir]
      endif
      openal_override_lib = cc.find_library(
        'OpenAL32',
        dirs: [openal_override_root / 'lib'],
        required: true,
      )
      openal_dep = declare_dependency(
        compile_args: openal_override_include_args,
        dependencies: [openal_override_lib],
      )
    else
      openal_dep = dependency(
        'openal',
        required: true,
        fallback: ['openal-soft-prebuilt', 'openal_dep'],
        default_options: fallback_opt,
      )
    endif
  endif

  if use_sdl3_backend
    sdl3_dep = dependency(
      'sdl3',
      version: '>=3.4.4',
      required: true,
      fallback: ['sdl3', 'sdl3_dep'],
      default_options: fallback_opt,
    )
  endif

  deps = [glew_dep, openal_dep, stb_vorbis_dep]

  if host_system == 'windows'
    deps += [
      cc.find_library('opengl32', required: true),
      cc.find_library('Dbghelp', required: true),
      cc.find_library('Iphlpapi', required: true),
      cc.find_library('Winmm', required: true),
      cc.find_library('ws2_32', required: true),
    ]

    if use_sdl3_backend
      deps += [sdl3_dep]
    else
      deps += [
        cc.find_library('dinput8', required: true),
        cc.find_library('dxguid', required: true),
      ]
    endif
  elif host_system == 'linux'
    deps += [
      dependency('threads', required: true),
      cc.find_library('dl', required: true),
      dependency('x11', required: true),
      dependency('xext', required: true),
      dependency('xxf86vm', required: true),
      cc.find_library('GL', required: true),
    ]
    if use_sdl3_backend
      deps += [sdl3_dep]
    endif
  elif host_system == 'darwin'
    deps += [
      dependency('threads', required: true),
      dependency(
        'appleframeworks',
        modules: ['Cocoa', 'OpenGL', 'ApplicationServices', 'Carbon'],
        required: true,
      ),
    ]
  endif
endif

shared_cpp_args = ['-DUSE_OPENAL', '-DGLEW_NO_GLU']
shared_c_args = ['-DGLEW_NO_GLU']
engine_link_args = []

if host_system == 'windows'
  shared_cpp_args += ['-D_WINDOWS', '-DWIN32']
  shared_c_args += ['-D_WINDOWS', '-DWIN32']
  engine_link_args += ['/STACK:16777216,16777216']
else
  shared_cpp_args += ['-DID_GL_HARDLINK']
  shared_c_args += ['-DID_GL_HARDLINK']
endif

if host_system == 'darwin'
  shared_cpp_args += ['-DMACOS_X=1']
  shared_c_args += ['-DMACOS_X=1']
endif

if use_sdl3_backend
  shared_cpp_args += ['-DUSE_SDL3=1']
  shared_c_args += ['-DUSE_SDL3=1']
endif

if cpp.get_argument_syntax() == 'msvc'
  shared_cpp_args += ['/J', '/FS', '/source-charset:windows-1252', '/execution-charset:windows-1252', '/Zc:strictStrings-']
  shared_c_args += ['/FS', '/source-charset:windows-1252', '/execution-charset:windows-1252']

  # Quake 4 legacy code (including integrated BSE sources) currently relies on
  # pointer-sized casts and old C/C++ constructs that trigger noisy warnings
  # under modern MSVC defaults.
  msvc_suppressed_warnings = [
    '/wd4091', # typedef enum pattern without declarator
    '/wd4302', # truncation on legacy pointer/int casts
    '/wd4305', # double -> float literal truncation
    '/wd4311', # pointer truncation in legacy alloc/index paths
    '/wd4312', # integer/pointer size widening in legacy alloc/index paths
    '/wd4838', # narrowing in legacy list constructors
    '/wd5033', # register keyword used in imported legacy code
    '/wd5055', # enum/float comparison in legacy declarations
  ]
  shared_cpp_args += msvc_suppressed_warnings
  shared_c_args += msvc_suppressed_warnings
else
  # Legacy idTech4-era code relies on implicit narrowing/pointer casts that
  # modern GCC/Clang reject as hard errors without permissive mode.
  shared_cpp_args += ['-fpermissive']
endif

pch_headers = []
if get_option('use_pch')
  pch_headers = ['src/idlib/precompiled.h']
endif
game_idlib_pch_headers = ['src/idlib/precompiled.h']
game_module_pch_headers = [meson.project_source_root() / 'src' / 'idlib' / 'precompiled.h']

src_include_dir = include_directories('src')
src_idlib_include_dir = include_directories('src/idlib')
src_renderer_include_dir = include_directories('src/renderer')
src_ui_include_dir = include_directories('src/ui')
src_sys_include_dir = include_directories('src/sys')
src_bse_include_dir = include_directories('src/bse')
src_maya_import_include_dir = include_directories('src/MayaImport')
game_include_dirs = [
  root_include_dir,
  src_include_dir,
  src_idlib_include_dir,
  src_renderer_include_dir,
  src_ui_include_dir,
  src_sys_include_dir,
  src_bse_include_dir,
  src_maya_import_include_dir,
]
engine_include_dirs = [root_include_dir, src_include_dir]
game_idlib_include_dirs = [root_include_dir, src_include_dir]
bse_client_include_dirs = [root_include_dir, src_include_dir, src_bse_include_dir]

if needs_game_headers
  staged_game_include_dir = include_directories(game_stage_game_include_rel)
  engine_include_dirs += [staged_game_include_dir]
  game_idlib_include_dirs += [staged_game_include_dir]
  game_include_dirs += [staged_game_include_dir]
  bse_client_include_dirs += [staged_game_include_dir]
endif

engine_cpp_args = shared_cpp_args + ['-D__DOOM_DLL__']
engine_c_args = shared_c_args
bse_cpp_args = shared_cpp_args
bse_c_args = shared_c_args
bse_library = disabler()

if build_engine
  bse_library = static_library(
    'openq4_bse',
    bse_sources,
    include_directories: bse_client_include_dirs,
    cpp_args: bse_cpp_args,
    c_args: bse_c_args,
    cpp_pch: pch_headers,
    install: false,
  )
endif

if build_engine
  client_link_with = [bse_library]

  if host_system == 'windows'
    executable(
      engine_client_binary_name,
      openq4_engine_sources,
      include_directories: engine_include_dirs,
      dependencies: deps,
      link_with: client_link_with,
      cpp_args: engine_cpp_args,
      c_args: engine_c_args,
      cpp_pch: pch_headers,
      win_subsystem: 'windows',
      link_args: engine_link_args,
      install: true,
      install_dir: install_root_dir,
    )
  else
    executable(
      engine_client_binary_name,
      openq4_engine_sources,
      include_directories: engine_include_dirs,
      dependencies: deps,
      link_with: client_link_with,
      cpp_args: engine_cpp_args,
      c_args: engine_c_args,
      cpp_pch: pch_headers,
      link_args: engine_link_args,
      install: true,
      install_dir: install_root_dir,
    )
  endif

  dedicated_cpp_args = engine_cpp_args + ['-DID_DEDICATED']
  dedicated_c_args = engine_c_args + ['-DID_DEDICATED']

  if host_system == 'windows'
    executable(
      engine_ded_binary_name,
      openq4_engine_sources,
      include_directories: engine_include_dirs,
      dependencies: deps,
      cpp_args: dedicated_cpp_args,
      c_args: dedicated_c_args,
      cpp_pch: pch_headers,
      win_subsystem: 'windows',
      link_args: engine_link_args,
      install: true,
      install_dir: install_root_dir,
    )
  else
    executable(
      engine_ded_binary_name,
      openq4_engine_sources,
      include_directories: engine_include_dirs,
      dependencies: deps,
      cpp_args: dedicated_cpp_args,
      c_args: dedicated_c_args,
      cpp_pch: pch_headers,
      link_args: engine_link_args,
      install: true,
      install_dir: install_root_dir,
    )
  endif
endif

game_common_cpp_args = shared_cpp_args + ['-DGAME_DLL']
game_common_c_args = shared_c_args
game_module_link_args = []
if host_system == 'windows'
  game_module_link_args = ['/FIXED:NO', '/LARGEADDRESSAWARE']
  if ['x86_64', 'aarch64'].contains(host_cpu_family)
    game_module_link_args += ['/BASE:0x180000000']
  else
    game_module_link_args += ['/BASE:0x20000000']
  endif
  release_like = ['release', 'debugoptimized', 'minsize'].contains(get_option('buildtype'))
  if release_like
    game_module_link_args += ['/STACK:4194304', '/OPT:REF', '/OPT:ICF', '/MAP']
  else
    game_module_link_args += ['/STACK:16000000,16000000']
  endif
endif

game_idlib_library = disabler()
if build_engine or build_games
  game_idlib_library = static_library(
    'openq4_game_idlib',
    game_idlib_sources,
    include_directories: game_idlib_include_dirs,
    cpp_args: game_common_cpp_args,
    c_args: game_common_c_args,
    cpp_pch: game_idlib_pch_headers,
    install: false,
  )
endif

if build_engine or (build_games and (build_game_sp or build_game_mp))
  subdir('content/baseoq4')
endif

if host_system == 'windows' and host_cpu_family == 'x86_64'
  openal_runtime_dll = 'src/external/openal-soft/bin/win64/OpenAL32.dll'
  if fs_mod.is_file(openal_runtime_dll)
    install_data(
      openal_runtime_dll,
      install_dir: install_root_dir,
    )
  endif
endif

install_data(
  'assets/icons/quake4.ico',
  install_dir: install_root_dir,
  rename: 'OpenQ4.ico',
)

install_data(
  'assets/icons/quake4.icns',
  install_dir: install_root_dir,
  rename: 'OpenQ4.icns',
)

if host_system == 'linux'
  linux_desktop_conf = configuration_data()
  linux_desktop_conf.set('OPENQ4_CLIENT_BINARY', engine_client_binary_name)
  linux_desktop_conf.set('OPENQ4_PROJECT_VERSION', openq4_version_short)
  linux_desktop_conf.set('OPENQ4_PROJECT_VERSION_FULL', openq4_version_full)
  linux_steamdeck_launcher = configure_file(
    input: 'assets/linux/OpenQ4-steamdeck.in',
    output: 'OpenQ4-steamdeck',
    configuration: linux_desktop_conf,
  )
  linux_desktop_entry = configure_file(
    input: 'assets/linux/openq4.desktop.in',
    output: 'openq4.desktop',
    configuration: linux_desktop_conf,
  )
  linux_steamdeck_desktop_entry = configure_file(
    input: 'assets/linux/openq4-steamdeck.desktop.in',
    output: 'openq4-steamdeck.desktop',
    configuration: linux_desktop_conf,
  )
  install_data(
    linux_steamdeck_launcher,
    install_dir: install_root_dir,
    install_mode: 'rwxr-xr-x',
  )
  install_data(
    linux_desktop_entry,
    install_dir: install_root_dir / 'share' / 'applications',
  )
  install_data(
    linux_steamdeck_desktop_entry,
    install_dir: install_root_dir / 'share' / 'applications',
  )

  if fs_mod.is_file('assets/icons/quake4.svg')
    install_data(
      'assets/icons/quake4.svg',
      install_dir: install_root_dir / 'share' / 'icons' / 'hicolor' / 'scalable' / 'apps',
      rename: 'openq4.svg',
    )
  endif

  icon_sizes = ['16', '20', '24', '32', '40', '48', '64', '128', '256', '512', '1024']
  foreach icon_size : icon_sizes
    icon_png = 'assets/icons/quake4_' + icon_size + '.png'
    if fs_mod.is_file(icon_png)
      install_data(
        icon_png,
        install_dir: install_root_dir / 'share' / 'icons' / 'hicolor' / (icon_size + 'x' + icon_size) / 'apps',
        rename: 'openq4.png',
      )
    endif
  endforeach
endif

version_iteration_summary = version_iteration
if version_iteration_summary == ''
  version_iteration_summary = 'none'
endif

openq4_git_sha_summary = openq4_git_sha
if openq4_git_sha_summary == ''
  openq4_git_sha_summary = 'unavailable'
endif

summary(
  {
    'Host system': host_system,
    'Host cpu family': host_cpu_family,
    'Version track': version_track,
    'Version iteration': version_iteration_summary,
    'Version short': openq4_version_short,
    'Version full': openq4_version_full,
    'Version tag': openq4_version_tag,
    'Version resource': openq4_version_resource_dotted,
    'Git short SHA': openq4_git_sha_summary,
    'Git dirty': openq4_git_dirty,
    'Git commit count': openq4_commit_count,
    'MSVC toolchain': is_msvc,
    'MSVC version': cpp.version(),
    'MSVC 2026 baseline enforced': get_option('enforce_msvc_2026'),
    'C++ standard': get_option('cpp_std'),
    'Platform backend requested': platform_backend_requested,
    'Platform backend': platform_backend,
    'OpenAL override root': openal_root_override == '' ? 'bundled/default' : openal_root_override,
    'Build engine': build_engine,
    'engine client binary': engine_client_binary_name,
    'engine dedicated binary': engine_ded_binary_name,
    'Integrated BSE in client': build_engine,
    'Integrated BSE source dir': bse_source_root,
    'Build games': build_games,
    'GameLibs source repo': game_libs_repo_summary,
    'Build game_sp': build_games and build_game_sp,
    'Build game_mp': build_games and build_game_mp,
    'game_sp binary': game_sp_binary_name,
    'game_mp binary': game_mp_binary_name,
    'Resource compiler': resource_compiler.found(),
    'SDL3': use_sdl3_backend and sdl3_dep.found(),
    'GLEW': glew_dep.found(),
    'OpenAL': openal_dep.found(),
    'stb_vorbis': stb_vorbis_dep.found(),
    'Precompiled headers': get_option('use_pch'),
    'Dedicated server target': build_engine,
    'Install root': install_root_dir,
  },
  section: 'Build Features',
  bool_yn: true,
)
