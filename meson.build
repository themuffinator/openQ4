project(
  'OpenQ4',
  ['c', 'cpp'],
  version: '0.0.1',
  meson_version: '>=1.2.0',
  default_options: ['c_std=c11', 'cpp_std=c++20'],
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

platform_backend_option = get_option('platform_backend')
platform_backend = platform_backend_option
if host_system == 'windows' and platform_backend_option == 'native'
  error('platform_backend=native is only valid on non-Windows hosts.')
endif

if host_system != 'windows'
  if platform_backend_option != 'native'
    warning(
      'Non-Windows hosts force the native platform source set; overriding platform_backend='
      + platform_backend_option + '.'
    )
  endif
  platform_backend = 'native'
endif

use_sdl3_backend = host_system == 'windows' and platform_backend_option == 'sdl3'
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
endif

build_engine = get_option('build_engine')
build_games = get_option('build_games')
build_game_sp = get_option('build_game_sp')
build_game_mp = get_option('build_game_mp')
build_libbse = build_engine and get_option('build_libbse')
needs_bse_sources = build_libbse
install_root_dir = meson.project_source_root() / '.install'
install_openq4_dir = install_root_dir / 'openq4'
bse_repo_root = ''
bse_source_root = ''
bse_repo_summary = 'not required'

if not build_engine and not build_games
  error('At least one build target must be enabled. Use -Dbuild_engine=true and/or -Dbuild_games=true.')
endif

if needs_bse_sources
  bse_repo_discovery = run_command(
    py,
    '-c',
    'import os, pathlib, sys; root = pathlib.Path(sys.argv[1]); raw = os.environ.get("OPENQ4_BSE_REPO", "").strip(); repo = pathlib.Path(raw) if raw else (root / ".." / "OpenQ4-BSE"); print(repo.resolve().as_posix())',
    meson.project_source_root(),
    check: true,
  )
  bse_repo_root = bse_repo_discovery.stdout().strip()
  if bse_repo_root == ''
    error('Failed to resolve the OpenQ4-BSE repository path. Set OPENQ4_BSE_REPO or place it at ../OpenQ4-BSE.')
  endif

  bse_source_root = bse_repo_root / 'src' / 'bse'
  if not fs_mod.is_dir(bse_source_root)
    error('OpenQ4-BSE source directory not found: ' + bse_source_root)
  endif

  bse_repo_summary = bse_repo_root
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
game_sp_sources = []
bse_sources = []
game_idlib_sources = []
if build_games
  game_source_discovery = run_command(
    py,
    meson.project_source_root() / 'tools' / 'build' / 'list_sources.py',
    meson.project_source_root(),
    'src/game',
    'src/game/Callbacks.cpp',
    'src/game/gamesys/Callbacks.cpp',
    check: true,
  )
  game_source_paths = game_source_discovery.stdout().strip().split('\n')
  if game_source_paths.length() == 0
    error('tools/build/list_sources.py returned no game source files.')
  endif
  game_sources = files(game_source_paths)

  game_sp_source_discovery = run_command(
    py,
    meson.project_source_root() / 'tools' / 'build' / 'list_sources.py',
    meson.project_source_root(),
    'src/game',
    '--exclude-subdirs=bots',
    'src/game/Callbacks.cpp',
    'src/game/gamesys/Callbacks.cpp',
    check: true,
  )
  game_sp_source_paths = game_sp_source_discovery.stdout().strip().split('\n')
  if game_sp_source_paths.length() == 0
    error('tools/build/list_sources.py returned no SP game source files.')
  endif
  game_sp_sources = files(game_sp_source_paths)
endif

if build_libbse
  bse_source_discovery = run_command(
    py,
    meson.project_source_root() / 'tools' / 'build' / 'list_sources.py',
    bse_repo_root,
    'src/bse',
    check: true,
  )
  bse_source_paths = bse_source_discovery.stdout().strip().split('\n')
  if bse_source_paths.length() == 0
    error('tools/build/list_sources.py returned no BSE source files.')
  endif

  bse_absolute_paths = []
  foreach rel_path : bse_source_paths
    bse_absolute_paths += [bse_repo_root / rel_path]
  endforeach

  bse_sources = files(bse_absolute_paths)
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
  openq4_engine_sources += windows_mod.compile_resources('src/sys/win32/rc/OpenQ4Version.rc')
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
  if host_system == 'windows'
    glew_dep = dependency(
      'glew',
      required: true,
      fallback: ['glew', 'glew_dep'],
      default_options: fallback_opt,
    )
  else
    glew_dep = dependency('glew', required: true)
  endif

  stb_vorbis_dep = dependency(
    'stb_vorbis',
    required: true,
    fallback: ['stb_vorbis', 'stb_vorbis_dep'],
    default_options: fallback_opt,
  )

  if host_system == 'darwin'
    openal_dep = dependency('appleframeworks', modules: ['OpenAL'], required: true)
  else
    openal_dep = dependency(
      'openal',
      required: true,
      fallback: ['openal-soft-prebuilt', 'openal_dep'],
      default_options: fallback_opt,
    )
  endif

  if use_sdl3_backend
    sdl3_dep = dependency(
      'sdl3',
      version: '>=3.4.0',
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

  # Quake 4 legacy code (and companion BSE sources) currently relies on
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
game_pch_headers = ['src/idlib/precompiled.h']

src_include_dir = include_directories('src')
engine_include_dirs = [src_include_dir]
bse_module_include_dirs = [src_include_dir]

if needs_bse_sources
  bse_source_include_dir = include_directories(bse_source_root)
  # Compatibility include anchor for legacy relative includes inside BSE sources
  # (for example: "../renderer/ModelManager.h").
  bse_legacy_include_anchor = include_directories('src/bse_api')
  if build_libbse
    bse_module_include_dirs += [bse_source_include_dir, bse_legacy_include_anchor]
  endif
endif

engine_cpp_args = shared_cpp_args + ['-D__DOOM_DLL__']
engine_c_args = shared_c_args

if build_engine
  if host_system == 'windows'
    executable(
      engine_client_binary_name,
      openq4_engine_sources,
      include_directories: engine_include_dirs,
      dependencies: deps,
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
bse_common_cpp_args = shared_cpp_args + ['-DGAME_DLL', '-DBSE_DLL']
bse_common_c_args = shared_c_args
game_module_link_args = []
if host_system == 'windows'
  game_module_link_args = ['/FIXED:NO', '/LARGEADDRESSAWARE']
  if host_cpu_family == 'x86_64'
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
    include_directories: src_include_dir,
    cpp_args: game_common_cpp_args,
    c_args: game_common_c_args,
    cpp_pch: game_pch_headers,
    install: false,
  )
endif

if build_libbse
  shared_module(
    'libbse-q4',
    bse_sources,
    include_directories: bse_module_include_dirs,
    link_with: game_idlib_library,
    cpp_args: bse_common_cpp_args,
    c_args: bse_common_c_args,
    cpp_pch: game_pch_headers,
    link_args: game_module_link_args,
    name_prefix: '',
    install: true,
    install_dir: install_root_dir,
  )
endif

if build_engine or (build_games and (build_game_sp or build_game_mp))
  subdir('openq4')
endif

summary(
  {
    'Host system': host_system,
    'Host cpu family': host_cpu_family,
    'MSVC toolchain': is_msvc,
    'MSVC version': cpp.version(),
    'MSVC 2026 baseline enforced': get_option('enforce_msvc_2026'),
    'C++ standard': get_option('cpp_std'),
    'Platform backend': platform_backend,
    'Build engine': build_engine,
    'engine client binary': engine_client_binary_name,
    'engine dedicated binary': engine_ded_binary_name,
    'Build BSE DLL': build_libbse,
    'BSE source repo': bse_repo_summary,
    'Build games': build_games,
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
