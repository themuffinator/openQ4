project(
  'openQ4',
  ['c', 'cpp'],
  version: '0.11.0',
  meson_version: '>=1.6.0',
  default_options: ['c_std=c11', 'cpp_std=c++20', 'b_vscrt=static_from_buildtype'],
)

host_system = host_machine.system()
host_cpu_family = host_machine.cpu_family()
if not ['windows', 'linux', 'darwin', 'android'].contains(host_system)
  error('Unsupported host system for openQ4 Meson build: ' + host_system)
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
game_module_filename_suffix = '.so'
if host_system == 'windows'
  game_module_filename_suffix = '.dll'
elif host_system == 'darwin'
  game_module_filename_suffix = '.dylib'
endif
engine_client_binary_name = meson.project_name() + '-client_' + binary_arch
engine_ded_binary_name = meson.project_name() + '-ded_' + binary_arch
module_filename_prefix = host_system == 'android' ? 'lib' : ''
if host_system == 'android'
  if host_cpu_family != 'aarch64'
    error('Android currently supports the arm64-v8a ABI only.')
  endif
  engine_client_binary_name = 'quake4'
endif

if host_cpu_family != 'x86_64' and not (host_system == 'linux' and host_cpu_family == 'aarch64')
  warning(
    'openQ4 currently targets x64 as the active compatibility baseline. '
    + 'Non-x64 builds are considered experimental during platform bring-up.'
  )
endif

platform_backend_requested = get_option('platform_backend')
platform_backend = platform_backend_requested
macos_graphics_bridge = get_option('macos_graphics_bridge')
macos_openal_provider = get_option('macos_openal_provider')
if host_system == 'windows' and platform_backend_requested == 'native'
  error('platform_backend=native is only valid on non-Windows hosts.')
endif

if host_system != 'darwin' and macos_graphics_bridge != 'opengl'
  error('macos_graphics_bridge=' + macos_graphics_bridge + ' is only valid on macOS hosts.')
endif
if host_system != 'darwin' and macos_openal_provider != 'apple_framework'
  error('macos_openal_provider=' + macos_openal_provider + ' is only valid on macOS hosts.')
endif

if host_system == 'android'
  if platform_backend_requested != 'sdl3'
    error('Android requires platform_backend=sdl3.')
  endif
elif host_system == 'linux'
  if platform_backend_requested == 'legacy_win32'
    error('platform_backend=legacy_win32 is only valid on Windows hosts.')
  endif
elif host_system == 'darwin'
  if platform_backend_requested == 'legacy_win32'
    error('platform_backend=legacy_win32 is only valid on Windows hosts.')
  endif
  if macos_graphics_bridge == 'metal' and platform_backend_requested != 'sdl3'
    error('macos_graphics_bridge=metal requires platform_backend=sdl3 so the bridge stays on the shared SDL3 path instead of the native Cocoa backend.')
  endif
  if platform_backend_requested == 'native'
    warning(
      'platform_backend=native on macOS is comparison-only diagnostic infrastructure. '
      + 'Release packages use platform_backend=sdl3; do not treat the native Cocoa/OpenGL backend as supported release coverage.'
    )
  endif
elif host_system != 'windows'
  error('Unsupported host system for platform backend selection: ' + host_system)
endif

use_sdl3_backend = platform_backend == 'sdl3'
use_macos_metal_bridge = host_system == 'darwin' and macos_graphics_bridge == 'metal'
if host_system == 'windows' and not use_sdl3_backend
  warning(
    'legacy_win32 backend is transitional. '
    + 'Use -Dplatform_backend=sdl3 for portability-focused development.'
  )
endif

if host_system == 'windows'
  # Windows commonly enables a non-functional Microsoft Store python3.exe
  # application alias. Prefer the real python.exe launcher there.
  py = find_program('python', 'python3', native: true, required: true)
else
  py = find_program('python3', 'python', native: true, required: true)
endif
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
generated_version_header_path = meson.current_build_dir() / 'openq4_version_generated.h'
version_metadata_command = [
  py,
  meson.project_source_root() / 'tools' / 'build' / 'openq4_version.py',
  '--source-root',
  meson.project_source_root(),
  '--track',
  version_track,
]
# Release builds pin the exact version. Everything else lets the helper resolve
# it: project(version:) above is only a floor, raised to the newest published v*
# tag when git history is available, so an unpinned build never advertises a
# version that was never released.
if version_base_override != ''
  version_metadata_command += ['--base-version', version_base_override]
endif
if version_iteration != ''
  version_metadata_command += ['--iteration', version_iteration]
endif
version_metadata_result = run_command(
  version_metadata_command + ['--header-out', generated_version_header_path],
  check: true,
)

# Git metadata changes whenever HEAD or the worktree changes, without changing
# meson.build. Re-run the lightweight generator for every build invocation;
# write_if_changed keeps downstream recompilation limited to real version
# changes. The configure-time invocation above ensures the header also exists
# before a backend performs its first dependency scan.
generated_version_header = custom_target(
  'openq4_version_generated',
  output: 'openq4_version_generated.h',
  command: version_metadata_command + ['--header-out', '@OUTPUT@'],
  depend_files: files('tools/build/openq4_version.py'),
  build_always_stale: true,
  build_by_default: true,
  install: false,
)

openq4_version_base = meson.project_version()
openq4_version_short = meson.project_version()
openq4_version_full = meson.project_version()
openq4_version_tag = meson.project_version()
openq4_version_date = ''
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
  if version_key == 'base_version'
    openq4_version_base = version_value
  elif version_key == 'version_short'
    openq4_version_short = version_value
  elif version_key == 'version'
    openq4_version_full = version_value
  elif version_key == 'version_tag'
    openq4_version_tag = version_value
  elif version_key == 'version_date'
    openq4_version_date = version_value
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
install_module_dir = install_game_dir
install_engine_dir = install_root_dir
if host_system == 'android'
  # Keep mobile assets/native libraries together without replacing a staged
  # desktop installation. The host app copies these into assets/ and jniLibs/.
  install_root_dir = install_root_dir / 'android' / 'arm64-v8a'
  install_game_dir = install_root_dir / 'baseoq4'
  install_module_dir = install_root_dir / 'lib'
  install_engine_dir = install_module_dir
endif
android_deps_root = get_option('android_deps_root').strip()
sigmatouch_root = get_option('sigmatouch_root').strip()
sigmatouch_controls_root = get_option('sigmatouch_controls_root').strip()
build_sigmatouch = sigmatouch_root != ''
if host_system != 'android' and (android_deps_root != '' or build_sigmatouch or sigmatouch_controls_root != '')
  error('Android dependency and Sigma Touch options require an Android cross build.')
endif
if build_sigmatouch and sigmatouch_controls_root == ''
  error('sigmatouch_root requires sigmatouch_controls_root.')
endif
game_libs_repo_root = ''
game_libs_source_root = ''
game_libs_repo_summary = 'not required'
game_stage_root = ''
game_stage_game_root = ''
game_stage_mpgame_root = ''
game_stage_game_include_rel = '.tmp/gamelibs_stage/src/game'
game_stage_mpgame_include_rel = '.tmp/gamelibs_stage/src/mpgame'
game_stage_output_rel = '.tmp/gamelibs_stage'
if host_system != 'windows'
  # A cross configure must not refresh files an active desktop build reads.
  game_stage_output_rel = '.tmp/gamelibs_stage-' + host_system + '-' + binary_arch
  game_stage_game_include_rel = game_stage_output_rel / 'src/game'
  game_stage_mpgame_include_rel = game_stage_output_rel / 'src/mpgame'
endif
bse_source_root = meson.project_source_root() / 'src' / 'bse'
game_sp_module_defs_file = ''
game_mp_module_defs_file = ''

if not build_engine and not build_games
  error('At least one build target must be enabled. Use -Dbuild_engine=true and/or -Dbuild_games=true.')
endif

if get_option('build_native_tests')
  openq4_file_memory_under_test = custom_target(
    'openq4-file-memory-under-test',
    input: files('src/framework/File.h', 'src/framework/File.cpp'),
    output: 'FileMemoryUnderTest.inc',
    command: [py, files('tools/tests/generate_file_memory_test.py'), '@INPUT0@', '@INPUT1@', '@OUTPUT@'],
  )
  openq4_file_memory_test = executable(
    'openq4-file-memory-test',
    [files('tools/tests/native/FileMemoryTest.cpp'), openq4_file_memory_under_test],
    include_directories: root_include_dir,
    install: false,
  )
  test('openq4-file-memory', openq4_file_memory_test, timeout: 30)

  openq4_core_safety_test = executable(
    'openq4-core-safety-test',
    files(
      'tools/tests/native/CoreSafetyTest.cpp',
      'src/idlib/CryptoHash.cpp',
      'src/framework/async/Rcon2Protocol.cpp',
    ),
    include_directories: root_include_dir,
    install: false,
  )
  test(
    'openq4-core-safety',
    openq4_core_safety_test,
    timeout: 30,
  )

  openq4_level_load_cache_format_test = executable(
    'openq4-level-load-cache-format-test',
    files(
      'tools/tests/native/LevelLoadCacheFormatTest.cpp',
      'src/framework/LevelLoadCacheFormat.cpp',
    ),
    include_directories: root_include_dir,
    install: false,
  )
  test(
    'openq4-level-load-cache-format',
    openq4_level_load_cache_format_test,
    timeout: 30,
  )

  openq4_level_load_pipeline_test = executable(
    'openq4-level-load-pipeline-test',
    files(
      'tools/tests/native/LevelLoadPipelineTest.cpp',
      'src/framework/LevelLoadPipeline.cpp',
      'src/framework/ParallelJobSystem.cpp',
    ),
    include_directories: root_include_dir,
    install: false,
  )
  test(
    'openq4-level-load-pipeline',
    openq4_level_load_pipeline_test,
    timeout: 30,
  )

  openq4_job_system_test = executable(
    'openq4-job-system-test',
    files(
      'tools/tests/native/ParallelJobSystemTest.cpp',
      'src/framework/ParallelJobSystem.cpp',
    ),
    include_directories: root_include_dir,
    dependencies: dependency('threads', required: true),
    install: false,
  )
  test(
    'openq4-job-system',
    openq4_job_system_test,
    timeout: 30,
  )

  openq4_gpu_frame_timing_test = executable(
    'openq4-gpu-frame-timing-test',
    files('tools/tests/native/GpuFrameTimingTest.cpp'),
    include_directories: root_include_dir,
    install: false,
  )
  test(
    'openq4-gpu-frame-timing',
    openq4_gpu_frame_timing_test,
    timeout: 30,
  )

  openq4_temporal_presentation_core_test = executable(
    'openq4-temporal-presentation-core-test',
    files('tools/tests/native/TemporalPresentationCoreTest.cpp'),
    include_directories: root_include_dir,
    install: false,
  )
  test(
    'openq4-temporal-presentation-core',
    openq4_temporal_presentation_core_test,
    timeout: 30,
  )

  openq4_temporal_history_core_test = executable(
    'openq4-temporal-history-core-test',
    files('tools/tests/native/TemporalHistoryCoreTest.cpp'),
    include_directories: root_include_dir,
    install: false,
  )
  test(
    'openq4-temporal-history-core',
    openq4_temporal_history_core_test,
    timeout: 30,
  )

  openq4_renderer_contracts_test = executable(
    'openq4-renderer-contracts-test',
    files(
      'tools/tests/native/RendererContractsTest.cpp',
      'src/renderer/RendererContracts.cpp',
    ),
    include_directories: root_include_dir,
    install: false,
  )
  test(
    'openq4-renderer-contracts',
    openq4_renderer_contracts_test,
    timeout: 30,
  )

  openq4_advanced_lighting_core_test = executable(
    'openq4-advanced-lighting-core-test',
    files('tools/tests/native/AdvancedLightingCoreTest.cpp'),
    include_directories: root_include_dir,
    install: false,
  )
  test(
    'openq4-advanced-lighting-core',
    openq4_advanced_lighting_core_test,
    timeout: 30,
  )

  openq4_advanced_screen_space_core_test = executable(
    'openq4-advanced-screen-space-core-test',
    files('tools/tests/native/AdvancedScreenSpaceCoreTest.cpp'),
    include_directories: root_include_dir,
    install: false,
  )
  test(
    'openq4-advanced-screen-space-core',
    openq4_advanced_screen_space_core_test,
    timeout: 30,
  )

  openq4_specular_probe_atlas_packing_test = executable(
    'openq4-specular-probe-atlas-packing-test',
    files('tools/tests/native/ModernSpecularProbeAtlasPackingTest.cpp'),
    include_directories: root_include_dir,
    install: false,
  )
  test(
    'openq4-specular-probe-atlas-packing',
    openq4_specular_probe_atlas_packing_test,
    timeout: 30,
  )
endif

# The retained runtime is isolated from engine PCH/platform/render headers.
# It can be tested with the same layout/geometry code without creating a window.
if build_engine or get_option('build_native_tests')
  rmlui_project = subproject('rmlui')
  jsoncpp_project = subproject('jsoncpp')
  libtess2_project = subproject('libtess2')
  openq4_retained_library = static_library(
    'openq4-retained-ui',
    files('src/ui/retained/Runtime.cpp', 'src/ui/retained/Document.cpp', 'src/ui/retained/Presentation.cpp', 'src/ui/retained/Behavior.cpp', 'src/ui/retained/State.cpp', 'src/ui/retained/Motion.cpp', 'src/ui/retained/Interaction.cpp', 'src/ui/retained/Input.cpp', 'src/ui/retained/Vector.cpp', 'src/ui/retained/VectorElement.cpp'),
    dependencies: [rmlui_project.get_variable('rmlui_dep'), jsoncpp_project.get_variable('jsoncpp_dep'), libtess2_project.get_variable('libtess2_dep')],
    pic: true,
    install: false,
  )
  openq4_retained_dep = declare_dependency(link_with: openq4_retained_library)
  if get_option('build_native_tests')
    openq4_ui_behavior_test = executable(
      'openq4-ui-behavior-test',
      files('tools/tests/native/UiBehaviorTest.cpp'),
      include_directories: root_include_dir,
      dependencies: openq4_retained_dep,
      install: false,
    )
    test('openq4-ui-behavior', openq4_ui_behavior_test, timeout: 30)
    openq4_ui_presentation_test = executable(
      'openq4-ui-presentation-test',
      files('tools/tests/native/UiPresentationTest.cpp'),
      include_directories: root_include_dir,
      dependencies: openq4_retained_dep,
      install: false,
    )
    test('openq4-ui-presentation', openq4_ui_presentation_test, timeout: 30)
    openq4_ui_presentation_runtime_test = executable(
      'openq4-ui-presentation-runtime-test',
      files('tools/tests/native/UiPresentationRuntimeTest.cpp'),
      include_directories: root_include_dir,
      dependencies: [openq4_retained_dep, jsoncpp_project.get_variable('jsoncpp_dep')],
      install: false,
    )
    test('openq4-ui-presentation-runtime', openq4_ui_presentation_runtime_test, timeout: 30)
    openq4_retained_test = executable(
      'openq4-retained-ui-test',
      files('tools/tests/native/RetainedUiTest.cpp'),
      include_directories: root_include_dir,
      dependencies: openq4_retained_dep,
      install: false,
    )
    test('openq4-retained-ui', openq4_retained_test,
      args: [meson.project_source_root() / 'tools/ui/fixtures/interaction-smoke.q4ui',
             meson.project_source_root() / 'tools/ui/fixtures/binding-smoke.q4ui'], timeout: 30)
    openq4_ui_document_test = executable(
      'openq4-ui-document-test',
      files('tools/tests/native/UiDocumentTest.cpp'),
      include_directories: root_include_dir,
      dependencies: openq4_retained_dep,
      install: false,
    )
    test('openq4-ui-document', openq4_ui_document_test, timeout: 30)
    openq4_ui_state_test = executable(
      'openq4-ui-state-test',
      files('tools/tests/native/UiStateTest.cpp'),
      include_directories: root_include_dir,
      dependencies: openq4_retained_dep,
      install: false,
    )
    test('openq4-ui-state', openq4_ui_state_test,
      args: [meson.project_source_root() / 'tools/ui/fixtures/binding-smoke.q4ui'], timeout: 30)
    openq4_ui_vector_test = executable(
      'openq4-ui-vector-test',
      files('tools/tests/native/UiVectorTest.cpp'),
      include_directories: root_include_dir,
      dependencies: openq4_retained_dep,
      install: false,
    )
    test('openq4-ui-vector', openq4_ui_vector_test, timeout: 30)
    executable(
      'openq4-retained-ui-benchmark',
      files('tools/tests/native/RetainedUiBenchmark.cpp'),
      include_directories: root_include_dir,
      dependencies: openq4_retained_dep,
      install: false,
    )
  endif
endif

openq4_pak_files = []
openq4_paks_generated_header = []
if build_engine or build_games
  openq4_pak_script_files = files(
    'tools/build/openq4_pak.py',
  )
  openq4_pak_manifest_script_files = files(
    'tools/build/write_pak_manifest.py',
  )

  openq4_pak0_source_manifest = custom_target(
    'openq4_pak0_source_manifest',
    output: 'pak0.sources',
    command: [
      py,
      meson.project_source_root() / 'tools' / 'build' / 'write_pak_manifest.py',
      meson.project_source_root(),
      'pak0.pk4',
      'content/baseoq4/pak0',
      '@OUTPUT@',
    ],
    depend_files: openq4_pak_script_files + openq4_pak_manifest_script_files,
    build_always_stale: true,
    build_by_default: true,
  )
  openq4_pak1_source_manifest = custom_target(
    'openq4_pak1_source_manifest',
    output: 'pak1.sources',
    command: [
      py,
      meson.project_source_root() / 'tools' / 'build' / 'write_pak_manifest.py',
      meson.project_source_root(),
      'pak1.pk4',
      'content/baseoq4/pak1',
      '@OUTPUT@',
    ],
    depend_files: openq4_pak_script_files + openq4_pak_manifest_script_files,
    build_always_stale: true,
    build_by_default: true,
  )

  openq4_pak0_output = custom_target(
    'openq4_pak0',
    input: openq4_pak0_source_manifest,
    output: 'pak0.pk4',
    command: [
      py,
      meson.project_source_root() / 'tools' / 'build' / 'build_openq4_pack.py',
      '--pak-name',
      'pak0.pk4',
      '--source-dir',
      meson.project_source_root() / 'content' / 'baseoq4' / 'pak0',
      '--manifest',
      '@INPUT@',
      '--out',
      '@OUTPUT@',
      '--stage-out',
      meson.current_build_dir() / 'baseoq4' / 'pak0.pk4',
    ],
    depend_files: openq4_pak_script_files + files(
      'tools/build/build_openq4_pack.py',
    ),
    build_by_default: true,
    install: true,
    install_dir: install_game_dir,
  )
  openq4_pak1_output = custom_target(
    'openq4_pak1',
    input: openq4_pak1_source_manifest,
    output: 'pak1.pk4',
    command: [
      py,
      meson.project_source_root() / 'tools' / 'build' / 'build_openq4_pack.py',
      '--pak-name',
      'pak1.pk4',
      '--source-dir',
      meson.project_source_root() / 'content' / 'baseoq4' / 'pak1',
      '--manifest',
      '@INPUT@',
      '--out',
      '@OUTPUT@',
      '--stage-out',
      meson.current_build_dir() / 'baseoq4' / 'pak1.pk4',
    ],
    depend_files: openq4_pak_script_files + files(
      'tools/build/build_openq4_pack.py',
    ),
    build_by_default: true,
    install: true,
    install_dir: install_game_dir,
  )
  openq4_paks_header_output = custom_target(
    'openq4_paks_generated_header',
    input: [openq4_pak0_output, openq4_pak1_output],
    output: 'openq4_paks_generated.h',
    command: [
      py,
      meson.project_source_root() / 'tools' / 'build' / 'generate_pak_header.py',
      '--pak0',
      '@INPUT0@',
      '--pak1',
      '@INPUT1@',
      '--header-out',
      '@OUTPUT@',
    ],
    depend_files: openq4_pak_script_files + files(
      'tools/build/generate_pak_header.py',
    ),
    build_by_default: true,
    install: false,
  )
  openq4_pak_files = [openq4_pak0_output, openq4_pak1_output]
  openq4_paks_generated_header = [openq4_paks_header_output]
endif

if needs_game_headers
  game_libs_repo_discovery = run_command(
    py,
    '-c',
    'import os, pathlib, sys; root = pathlib.Path(sys.argv[1]); raw = os.environ.get("OPENQ4_GAMELIBS_REPO", "").strip(); repo = pathlib.Path(raw) if raw else (root / ".." / "openQ4-game"); print(repo.resolve().as_posix())',
    meson.project_source_root(),
    check: true,
  )
  game_libs_repo_root = game_libs_repo_discovery.stdout().strip()
  if game_libs_repo_root == ''
    error('Failed to resolve the openQ4-game repository path. Set OPENQ4_GAMELIBS_REPO or place it at ../openQ4-game.')
  endif

  game_libs_source_root = game_libs_repo_root / 'src'
  if not fs_mod.is_dir(game_libs_source_root / 'game')
    error('openQ4-game source directory not found: ' + game_libs_source_root / 'game')
  endif
  if not fs_mod.is_dir(game_libs_source_root / 'mpgame')
    error('openQ4-game multiplayer source directory not found: ' + game_libs_source_root / 'mpgame')
  endif

  game_stage_result = run_command(
    py,
    meson.project_source_root() / 'tools' / 'build' / 'stage_gamelibs.py',
    meson.project_source_root(),
    game_libs_repo_root,
    meson.project_source_root() / game_stage_output_rel,
    check: true,
  )
  game_stage_root = game_stage_result.stdout().strip()
  if game_stage_root == ''
    error('Failed to stage game sources from openQ4-game.')
  endif

  game_stage_game_root = game_stage_root / 'src' / 'game'
  if not fs_mod.is_dir(game_stage_game_root)
    error('Staged game source directory not found: ' + game_stage_game_root)
  endif
  game_stage_mpgame_root = game_stage_root / 'src' / 'mpgame'
  if not fs_mod.is_dir(game_stage_mpgame_root)
    error('Staged multiplayer game source directory not found: ' + game_stage_mpgame_root)
  endif
  game_stage_manifest = game_stage_root / 'openq4_gamelibs_stage_manifest.json'
  if not fs_mod.is_file(game_stage_manifest)
    error('Staged openQ4-game source manifest not found: ' + game_stage_manifest)
  endif

  game_sp_module_defs_file = game_stage_game_root / 'Game.def'
  game_mp_module_defs_file = game_stage_mpgame_root / 'mpgame.def'
  if host_system == 'windows' and not fs_mod.is_file(game_sp_module_defs_file)
    error('Staged Game.def not found: ' + game_sp_module_defs_file)
  endif
  if host_system == 'windows' and not fs_mod.is_file(game_mp_module_defs_file)
    error('Staged mpgame.def not found: ' + game_mp_module_defs_file)
  endif

  game_libs_repo_summary = game_libs_repo_root
endif

if build_engine and not fs_mod.is_dir(bse_source_root)
  error('Integrated BSE source directory not found: ' + bse_source_root)
endif

cc = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')

linux_x11_dep = disabler()
linux_xext_dep = disabler()
linux_xxf86vm_dep = disabler()
linux_gl_dep = disabler()
linux_x11_helpers_enabled = false
linux_x11_option = get_option('linux_x11')
if build_engine and host_system == 'linux'
  if linux_x11_option.disabled()
    linux_gl_dep = cc.find_library('OpenGL', required: true)
  else
    linux_gl_dep = cc.find_library('OpenGL', required: false)
    if not linux_gl_dep.found()
      linux_gl_dep = cc.find_library('GL', required: true)
    endif
  endif
  if use_sdl3_backend
    if not linux_x11_option.disabled()
      linux_x11_dep = dependency('x11', required: linux_x11_option)
      linux_xext_dep = dependency('xext', required: linux_x11_option)
      linux_x11_helpers_enabled = linux_x11_dep.found() and linux_xext_dep.found()
    endif
  else
    if linux_x11_option.disabled()
      error('linux_x11=disabled requires the SDL3 platform backend.')
    endif
    linux_x11_dep = dependency('x11', required: true)
    linux_xext_dep = dependency('xext', required: true)
    linux_xxf86vm_dep = dependency('xxf86vm', required: true)
    linux_x11_helpers_enabled = true
  endif
endif

# Phase B8 F3: on SDL3 Windows/Linux the client executable sheds the
# statically linked renderer and always loads the renderer-gl module
# ('gl' resolves to the module there). macOS and the legacy backends keep
# the static renderer; the dedicated targets keep it everywhere until the
# server front-end story changes.
renderer_module_only_client = build_engine and use_sdl3_backend and (host_system == 'android' or (get_option('build_renderer_gl') and (host_system == 'windows' or host_system == 'linux')))
# Phase B7: the Windows SDL3 ded keeps the renderer front-end but links no GL
windows_gl_free_ded = build_engine and host_system == 'windows' and use_sdl3_backend

engine_source_discovery = run_command(
  py,
  meson.project_source_root() / 'tools' / 'build' / 'meson_sources.py',
  '--host-system',
  host_system,
  '--platform-backend',
  platform_backend,
  '--linux-x11-helpers',
  linux_x11_helpers_enabled ? 'true' : 'false',
  '--include-game',
  'false',
  '--renderer',
  renderer_module_only_client ? 'module' : 'static',
  check: true,
)

engine_source_paths = engine_source_discovery.stdout().strip().split('\n')
if engine_source_paths.length() == 0
  error('tools/build/meson_sources.py returned no engine source files.')
endif

dedicated_engine_source_paths = engine_source_paths
if windows_gl_free_ded
  # the Windows ded target keeps the static renderer (minus the GL link);
  # discover its own list
  dedicated_static_source_discovery = run_command(
    py,
    meson.project_source_root() / 'tools' / 'build' / 'meson_sources.py',
    '--host-system',
    host_system,
    '--platform-backend',
    platform_backend,
    '--linux-x11-helpers',
    'false',
    '--include-game',
    'false',
    '--renderer',
    'static',
    '--target-kind',
    'dedicated',
    check: true,
  )
  dedicated_engine_source_paths = dedicated_static_source_discovery.stdout().strip().split('\n')
  if dedicated_engine_source_paths.length() == 0
    error('tools/build/meson_sources.py returned no Windows dedicated source files.')
  endif
endif
if host_system == 'linux'
  dedicated_engine_source_discovery = run_command(
    py,
    meson.project_source_root() / 'tools' / 'build' / 'meson_sources.py',
    '--host-system',
    host_system,
    '--platform-backend',
    platform_backend,
    '--linux-x11-helpers',
    'false',
    '--include-game',
    'false',
    '--target-kind',
    'dedicated',
    check: true,
  )
  dedicated_engine_source_paths = dedicated_engine_source_discovery.stdout().strip().split('\n')
  if dedicated_engine_source_paths.length() == 0
    error('tools/build/meson_sources.py returned no Linux dedicated-server source files.')
  endif
endif

imagetools_source_discovery = run_command(
  py,
  meson.project_source_root() / 'tools' / 'build' / 'meson_sources.py',
  '--emit',
  'imagetools',
  check: true,
)
imagetools_source_paths = imagetools_source_discovery.stdout().strip().split('\n')
if imagetools_source_paths.length() == 0
  error('tools/build/meson_sources.py returned no imagetools source files.')
endif
openq4_imagetools_sources = files(imagetools_source_paths)

render_geo_source_discovery = run_command(
  py,
  meson.project_source_root() / 'tools' / 'build' / 'meson_sources.py',
  '--emit',
  'render_geo',
  check: true,
)
render_geo_source_paths = render_geo_source_discovery.stdout().strip().split('\n')
if render_geo_source_paths.length() == 0
  error('tools/build/meson_sources.py returned no render_geo source files.')
endif
openq4_render_geo_sources = files(render_geo_source_paths)

renderer_gl_source_discovery = run_command(
  py,
  meson.project_source_root() / 'tools' / 'build' / 'meson_sources.py',
  '--emit',
  'renderer_gl',
  check: true,
)
renderer_gl_source_paths = renderer_gl_source_discovery.stdout().strip().split('\n')
if renderer_gl_source_paths.length() == 0
  error('tools/build/meson_sources.py returned no renderer_gl source files.')
endif
openq4_renderer_gl_sources = files(renderer_gl_source_paths)

openq4_engine_sources = files(engine_source_paths)
if build_engine
  openq4_engine_sources += [openq4_paks_generated_header, generated_version_header]
endif

game_sp_sources = []
game_mp_sources = []
game_sources = []
bse_sources = []
game_idlib_sources = []
if build_games
  game_sp_source_discovery = run_command(
    py,
    meson.project_source_root() / 'tools' / 'build' / 'list_sources.py',
    game_stage_root,
    'src/game',
    'src/game/Callbacks.cpp',
    'src/game/gamesys/Callbacks.cpp',
    check: true,
  )
  game_sp_source_paths = game_sp_source_discovery.stdout().strip().split('\n')
  if game_sp_source_paths.length() == 0
    error('tools/build/list_sources.py returned no single-player game source files.')
  endif

  game_sp_absolute_paths = []
  foreach rel_path : game_sp_source_paths
    game_sp_absolute_paths += [game_stage_root / rel_path]
  endforeach
  game_sp_sources = files(game_sp_absolute_paths)

  game_mp_source_discovery = run_command(
    py,
    meson.project_source_root() / 'tools' / 'build' / 'list_sources.py',
    game_stage_root,
    'src/mpgame',
    'src/mpgame/Callbacks.cpp',
    'src/mpgame/gamesys/Callbacks.cpp',
    check: true,
  )
  game_mp_source_paths = game_mp_source_discovery.stdout().strip().split('\n')
  if game_mp_source_paths.length() == 0
    error('tools/build/list_sources.py returned no multiplayer game source files.')
  endif

  game_mp_absolute_paths = []
  foreach rel_path : game_mp_source_paths
    game_mp_absolute_paths += [game_stage_root / rel_path]
  endforeach
  game_mp_sources = files(game_mp_absolute_paths)
  game_sources = game_sp_sources + game_mp_sources

  savegame_compat_header = custom_target(
    'openq4_savegame_compat_generated',
    output: 'openq4_savegame_compat_generated.h',
    command: [
      py,
      meson.project_source_root() / 'tools' / 'build' / 'generate_savegame_compat_header.py',
      '--project-root',
      meson.project_source_root(),
      '--game-stage-root',
      game_stage_root,
      '--header-out',
      '@OUTPUT@',
    ],
    depend_files: files(engine_source_paths) + game_sources + files(dedicated_engine_source_paths) + files(
      'tools/build/generate_savegame_compat_header.py',
    ),
    install: false,
  )
  game_sp_sources += [savegame_compat_header, generated_version_header]
  game_mp_sources += [savegame_compat_header, generated_version_header]
  if build_engine
    # Session preflights the exact GameLibs source stamp before tearing down
    # the active map, so the engine target must see the same generated header.
    openq4_engine_sources += [savegame_compat_header]
  endif
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
openq4_version_resources = []
if host_system == 'windows' and resource_compiler.found()
  windows_mod = import('windows')
  openq4_version_resources = windows_mod.compile_resources(
    'src/sys/win32/rc/openQ4Version.rc',
    include_directories: [root_include_dir],
  )
  openq4_engine_sources += openq4_version_resources
endif

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
glew_default_options = fallback_opt
sdl3_default_options = fallback_opt
if host_system == 'linux' and use_sdl3_backend
  glew_default_options += ['openq4_sdl3_loader=true']
  if linux_x11_option.disabled()
    sdl3_default_options += ['x11=disabled']
  elif linux_x11_option.enabled()
    sdl3_default_options += ['x11=enabled']
  else
    sdl3_default_options += ['x11=auto']
  endif
endif

glew_dep = disabler()
glew_dedicated_dep = disabler()
stb_vorbis_dep = disabler()
openal_dep = disabler()
sdl3_dep = disabler()
macos_graphics_bridge_dep = disabler()
deps = []
if build_engine
  if host_system == 'android'
    # Shared engine headers still declare desktop GL types, but this client
    # loads GLES as a module and must not build/link desktop GLEW or OpenGL.
    glew_dep = declare_dependency()
  elif host_system == 'linux' and use_sdl3_backend
    glew_sp = subproject('glew', default_options: glew_default_options)
    glew_dep = glew_sp.get_variable('glew_dep')
    glew_dedicated_dep = glew_sp.get_variable('glew_dedicated_dep')
  else
    glew_dep = dependency(
      'glew',
      required: true,
      fallback: ['glew', 'glew_dep'],
      default_options: glew_default_options,
    )
    if host_system == 'linux' or host_system == 'windows' or host_system == 'darwin'
      # darwin needs it for the MoltenVK-backed Vulkan renderer module, which
      # links no GL but shares front-end translation units that reference it
      glew_dedicated_sp = subproject('glew', default_options: glew_default_options)
      glew_dedicated_dep = glew_dedicated_sp.get_variable('glew_dedicated_dep')
    endif
  endif

  stb_vorbis_dep = dependency(
    'stb_vorbis',
    required: true,
    fallback: ['stb_vorbis', 'stb_vorbis_dep'],
    default_options: fallback_opt,
  )

  if host_system == 'android'
    if android_deps_root == ''
      error('Android requires -Dandroid_deps_root=<target-prefix>; see docs/dev/android-build.md.')
    endif
    foreach header : ['AL/al.h', 'SDL3/SDL.h']
      if not fs_mod.is_file(android_deps_root / 'include' / header)
        error('Missing Android target dependency header: ' + android_deps_root / 'include' / header)
      endif
    endforeach
    foreach runtime : ['libSDL3.so', 'libopenal.so', 'libc++_shared.so']
      if not fs_mod.is_file(android_deps_root / 'lib' / runtime)
        error('Missing Android target runtime: ' + android_deps_root / 'lib' / runtime)
      endif
      install_data(android_deps_root / 'lib' / runtime, install_dir: install_engine_dir)
    endforeach
    # Khronos' notice also creates licenses/, so testing just that directory
    # would allow an APK to silently omit its shared dependency notices.
    foreach notice : ['sdl3/LICENSE.txt', 'openal-soft/COPYING', 'android-ndk/NOTICE.toolchain']
      if not fs_mod.is_file(android_deps_root / 'licenses' / notice)
        error('Missing Android dependency notice: ' + notice + '; prepare the target prefix with tools/build/prepare_android_deps.py.')
      endif
    endforeach
    install_subdir(android_deps_root / 'licenses', install_dir: install_root_dir)
    android_include_args = ['-I' + android_deps_root / 'include']
    # Record the compiler/header values actually used, rather than relying on
    # the host application's independently configured SDK and SDL defaults.
    android_native_api = cpp.get_define('__ANDROID_API__').to_int()
    if android_native_api < 24
      error('openQ4 requires Android API 24 or later.')
    endif
    run_command(
      py, meson.project_source_root() / 'tools/build/android_elf.py',
      '--api', android_native_api.to_string(),
      android_deps_root / 'lib/libSDL3.so',
      android_deps_root / 'lib/libopenal.so',
      android_deps_root / 'lib/libc++_shared.so',
      check: true,
    )
    android_sdl_version = []
    foreach part : ['MAJOR', 'MINOR', 'MICRO']
      android_sdl_version += cpp.get_define('SDL_' + part + '_VERSION',
        prefix: '#include <SDL3/SDL_version.h>', args: android_include_args)
    endforeach
    android_build_config = configuration_data()
    android_build_config.set('ANDROID_API', android_native_api)
    android_build_config.set('SDL_VERSION', '.'.join(android_sdl_version))
    configure_file(
      input: 'tools/build/android-build.json.in',
      output: 'android-build.json',
      configuration: android_build_config,
      install: true,
      install_dir: install_root_dir,
    )
    openal_dep = declare_dependency(
      compile_args: android_include_args,
      dependencies: cc.find_library('openal', dirs: [android_deps_root / 'lib'], required: true),
    )
  elif host_system == 'darwin'
    if macos_openal_provider == 'apple_framework'
      openal_dep = dependency('appleframeworks', modules: ['OpenAL'], required: true)
    else
      # The release provider specifically means an OpenAL Soft-style package.
      # Meson's generic OpenAL lookup may otherwise fall back to Apple's
      # framework when pkg-config cannot see the pinned dependency, while the
      # compile defines below still select AL/... headers. Fail during
      # configure instead of combining framework libraries with incompatible
      # OpenAL Soft headers (GitHub issues #94 and #122).
      openal_dep = dependency('openal', required: true, method: 'pkg-config')
    endif
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
    if host_system == 'android'
      sdl3_dep = declare_dependency(
        compile_args: android_include_args,
        dependencies: cc.find_library('SDL3', dirs: [android_deps_root / 'lib'], required: true),
      )
    elif host_system == 'linux' and not linux_x11_option.auto()
      # A system SDL's compiled video-driver set cannot be changed by a Meson
      # feature option. Explicit linux_x11 modes therefore select the bundled
      # SDL so enabled/disabled is a real build contract rather than a hint
      # that silently depends on the distribution's SDL configuration.
      sdl3_subproject = subproject('sdl3', default_options: sdl3_default_options)
      sdl3_dep = sdl3_subproject.get_variable('sdl3_dep')
    else
      sdl3_dep = dependency(
        'sdl3',
        version: '>=3.4.4',
        required: true,
        fallback: ['sdl3', 'sdl3_dep'],
        default_options: sdl3_default_options,
      )
    endif
  endif

  deps = [glew_dep, openal_dep, stb_vorbis_dep]

  if host_system == 'windows'
    windows_shell_dep = cc.find_library('shell32', required: true)
    windows_ole_dep = cc.find_library('ole32', required: true)
    deps += [
      cc.find_library('opengl32', required: true),
      cc.find_library('Dbghelp', required: true),
      cc.find_library('bcrypt', required: true),
      cc.find_library('Iphlpapi', required: true),
      cc.find_library('Winmm', required: true),
      cc.find_library('ws2_32', required: true),
      windows_shell_dep,
      windows_ole_dep,
    ]

    if use_sdl3_backend
      deps += [sdl3_dep]
    else
      deps += [
        cc.find_library('dinput8', required: true),
        cc.find_library('dxguid', required: true),
      ]
    endif
  elif host_system == 'android'
    deps += [
      sdl3_dep,
      cc.find_library('android', required: true),
      cc.find_library('log', required: true),
      cc.find_library('dl', required: true),
      cc.find_library('m', required: true),
    ]
  elif host_system == 'linux'
    deps += [
      dependency('threads', required: true),
      cc.find_library('dl', required: true),
      linux_gl_dep,
    ]
    if use_sdl3_backend
      deps += [sdl3_dep]
      if linux_x11_helpers_enabled
        deps += [linux_x11_dep, linux_xext_dep]
      endif
    else
      deps += [linux_x11_dep, linux_xext_dep, linux_xxf86vm_dep]
    endif
  elif host_system == 'darwin'
    if use_macos_metal_bridge
      macos_graphics_bridge_dep = dependency(
        'appleframeworks',
        modules: ['Metal', 'QuartzCore'],
        required: true,
      )
    endif
    macos_framework_modules = ['Cocoa', 'OpenGL', 'ApplicationServices']
    if not use_sdl3_backend
      macos_framework_modules += ['Carbon']
    endif
    deps += [
      dependency('threads', required: true),
      dependency(
        'appleframeworks',
        modules: macos_framework_modules,
        required: true,
      ),
    ]
    if use_sdl3_backend
      deps += [sdl3_dep]
    endif
    if use_macos_metal_bridge
      deps += [macos_graphics_bridge_dep]
    endif
  endif
endif

client_deps = deps
if renderer_module_only_client
  # the module-only client compiles against GLEW declarations (via qgl.h in
  # the precompiled header) but links no GL: the renderer-gl module carries
  # GLEW/opengl32/libGL itself
  glew_headers_dep = glew_dep.partial_dependency(
    compile_args: true,
    includes: true,
  )
  client_deps = [glew_headers_dep, openal_dep, stb_vorbis_dep]
  if host_system == 'windows'
    client_deps += [
      cc.find_library('Dbghelp', required: true),
      cc.find_library('bcrypt', required: true),
      cc.find_library('Iphlpapi', required: true),
      cc.find_library('Winmm', required: true),
      cc.find_library('ws2_32', required: true),
      windows_shell_dep,
      windows_ole_dep,
      sdl3_dep,
    ]
  elif host_system == 'android'
    client_deps = deps
  else
    client_deps += [
      dependency('threads', required: true),
      cc.find_library('dl', required: true),
      sdl3_dep,
    ]
    if linux_x11_helpers_enabled
      client_deps += [linux_x11_dep, linux_xext_dep]
    endif
  endif
endif

if build_engine
  client_deps += openq4_retained_dep
endif

dedicated_deps = deps
if windows_gl_free_ded
  # Phase B7: the Windows ded keeps the renderer front-end but links no GL:
  # opengl32 is dropped, GLEW builds in its hook-resolving dedicated flavor,
  # and src/sys/stub/stub_gl_win.cpp satisfies the surviving GL 1.1 refs
  dedicated_deps = [
    glew_dedicated_dep,
    openal_dep,
    stb_vorbis_dep,
    cc.find_library('Dbghelp', required: true),
    cc.find_library('bcrypt', required: true),
    cc.find_library('Iphlpapi', required: true),
    cc.find_library('Winmm', required: true),
    cc.find_library('ws2_32', required: true),
    windows_shell_dep,
    windows_ole_dep,
    sdl3_dep,
  ]
endif
if build_engine and host_system == 'linux'
  # Keep compile-time OpenAL declarations and the static GLEW/vorbis code used
  # by the common engine sources, but give the dedicated target no client-side
  # audio, graphics, window-system, or SDL link dependency.
  openal_headers_dep = openal_dep.partial_dependency(
    compile_args: true,
    includes: true,
  )
  dedicated_deps = [
    glew_dedicated_dep,
    openal_headers_dep,
    stb_vorbis_dep,
    dependency('threads', required: true),
    cc.find_library('dl', required: true),
  ]
endif

shared_cpp_args = ['-DUSE_OPENAL', '-DGLEW_NO_GLU']
shared_c_args = ['-DGLEW_NO_GLU']
shared_objcpp_args = []
shared_objc_args = []
engine_link_args = []
macos_openal_install_rpath = ''
linux_executable_pie = false
linux_module_link_hardening_args = []
linux_cross_dso_sanitizer_cpp_args = []

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
  if macos_openal_provider == 'system'
    shared_cpp_args += ['-DUSE_OPENAL_SOFT_INCLUDES=1']
    shared_c_args += ['-DUSE_OPENAL_SOFT_INCLUDES=1']
    # One installed executable supports all three sanctioned layouts: direct
    # runs from .install, the copied app executable, and the loose diagnostic
    # binary retained beside openQ4.app in release packages.
    macos_openal_install_rpath = '@loader_path/Frameworks:@loader_path/../Frameworks:@loader_path/openQ4.app/Contents/Frameworks'
  endif
  macos_compile_hardening_args = []
  foreach hardening_arg : ['-fstack-protector-strong', '-D_FORTIFY_SOURCE=2']
    if cpp.has_argument(hardening_arg) and cc.has_argument(hardening_arg)
      macos_compile_hardening_args += hardening_arg
    endif
  endforeach
  shared_cpp_args += macos_compile_hardening_args
  shared_c_args += macos_compile_hardening_args

  macos_link_hardening_args = []
  foreach hardening_arg : ['-Wl,-pie', '-Wl,-dead_strip', '-Wl,-headerpad_max_install_names']
    if cpp.has_link_argument(hardening_arg)
      macos_link_hardening_args += hardening_arg
    endif
  endforeach
  engine_link_args += macos_link_hardening_args

  if use_macos_metal_bridge
    shared_cpp_args += ['-DOPENQ4_MACOS_METAL_BRIDGE=1']
    shared_c_args += ['-DOPENQ4_MACOS_METAL_BRIDGE=1']
    shared_objcpp_args += ['-DOPENQ4_MACOS_METAL_BRIDGE=1']
    shared_objc_args += ['-DOPENQ4_MACOS_METAL_BRIDGE=1']
  endif
endif

if host_system == 'linux' or host_system == 'android'
  linux_compile_hardening_args = []
  foreach hardening_arg : ['-fstack-protector-strong', '-D_FORTIFY_SOURCE=2']
    if cpp.has_argument(hardening_arg) and cc.has_argument(hardening_arg)
      linux_compile_hardening_args += hardening_arg
    endif
  endforeach
  shared_cpp_args += linux_compile_hardening_args
  shared_c_args += linux_compile_hardening_args

  if cpp.has_argument('-fno-sanitize=vptr')
    # Engine, game, and in-process BSE translation units consume polymorphic
    # interfaces whose key functions and RTTI live on the opposite side of a
    # runtime-loaded module boundary. UBSan's vptr metadata cannot be resolved
    # while preserving the executable/module ABI and each module's -z defs contract.
    # Keep AddressSanitizer and every other requested UBSan check enabled.
    linux_cross_dso_sanitizer_cpp_args += ['-fno-sanitize=vptr']
  endif

  linux_link_hardening_args = []
  foreach hardening_arg : ['-Wl,-z,relro', '-Wl,-z,now', '-Wl,-z,noexecstack']
    if cpp.has_link_argument(hardening_arg)
      linux_link_hardening_args += hardening_arg
    endif
  endforeach
  engine_link_args += linux_link_hardening_args
  linux_module_link_hardening_args += linux_link_hardening_args
  linux_executable_pie = cpp.has_argument('-fPIE') and cc.has_argument('-fPIE') and cpp.has_link_argument('-pie')
endif

if use_sdl3_backend
  shared_cpp_args += ['-DUSE_SDL3=1']
  shared_c_args += ['-DUSE_SDL3=1']
endif

if host_system == 'android'
  shared_cpp_args += ['-DID_GL_HARDLINK']
  shared_c_args += ['-DID_GL_HARDLINK']
  if build_engine
    shared_cpp_args += android_include_args
    shared_c_args += android_include_args
  endif
  if build_sigmatouch
    shared_cpp_args += ['-DOPENQ4_SIGMATOUCH=1']
    shared_c_args += ['-DOPENQ4_SIGMATOUCH=1']
  endif
endif

if host_system == 'linux' and use_sdl3_backend and linux_x11_helpers_enabled
  shared_cpp_args += ['-DOPENQ4_HAVE_X11_HELPERS=1']
  shared_c_args += ['-DOPENQ4_HAVE_X11_HELPERS=1']
endif

if cpp.get_argument_syntax() == 'msvc'
  shared_cpp_args += [
    '/J',
    '/FS',
    '/source-charset:windows-1252',
    '/execution-charset:windows-1252',
    '/Zc:strictStrings-',
    '/we4302', # pointer truncation
    '/we4311', # pointer truncation through an integer
    '/we4312', # integer widened to a pointer
  ]
  # MSVC warning numbers are not portable to clang-cl even though it accepts
  # MSVC-style arguments. Keep this narrow gate on the validated MSVC x64 lane.
  if is_msvc and host_cpu_family == 'x86_64'
    cleaned_windows_x64_warning_errors = [
      '/we4101', # unreferenced local variable
      '/we4189', # local variable initialized but not referenced
      '/we4267', # size_t conversion may lose data
      '/we4324', # structure padded due to alignment
      '/we4505', # unreferenced local function removed
    ]
    shared_cpp_args += cleaned_windows_x64_warning_errors
  endif
  shared_c_args += ['/FS', '/source-charset:windows-1252', '/execution-charset:windows-1252']

  # Keep narrowly scoped suppressions for legacy C/C++ constructs. Pointer/
  # integer size diagnostics remain enabled so Win64 truncations cannot hide.
  msvc_suppressed_warnings = [
    '/wd4091', # typedef enum pattern without declarator
    '/wd4305', # double -> float literal truncation
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
  foreach aliasing_arg : ['-fno-strict-aliasing']
    if cpp.has_argument(aliasing_arg) and cc.has_argument(aliasing_arg)
      shared_cpp_args += aliasing_arg
      shared_c_args += aliasing_arg
    endif
  endforeach
  # GCC/Clang default to -ffp-contract=fast, which fuses a*b+c into an FMA
  # wherever the target has one. AArch64 always does; the x86-64 SSE2 baseline
  # openQ4 is validated against does not. That makes identical source produce
  # different float results per architecture in collision, AAS and physics
  # code, which is exactly the class of divergence behind arm64-only map and
  # savegame failures. MSVC already defaults to precise contraction.
  foreach fp_arg : ['-ffp-contract=off']
    if cpp.has_argument(fp_arg) and cc.has_argument(fp_arg)
      shared_cpp_args += fp_arg
      shared_c_args += fp_arg
    endif
  endforeach
  # src/idlib/Lib.h keys assert()/verify() off _DEBUG, which MSVC supplies via
  # /MTd but GCC and Clang never do here, so idlib's ownership and alignment
  # checks are dead on Linux and macOS unless this is asked for.
  if get_option('idlib_asserts')
    shared_cpp_args += ['-DOPENQ4_ENABLE_IDLIB_ASSERTS=1']
    shared_c_args += ['-DOPENQ4_ENABLE_IDLIB_ASSERTS=1']
  endif
endif

if host_system == 'darwin'
  shared_objcpp_args = shared_cpp_args
  shared_objc_args = shared_c_args
endif

pch_headers = []
game_idlib_pch_headers = []
game_module_pch_headers = []
common_header_cpp_args = []
common_header_path = meson.project_source_root() / 'src' / 'idlib' / 'precompiled.h'
if get_option('use_pch')
  pch_headers = ['src/idlib/precompiled.h']
  game_idlib_pch_headers = ['src/idlib/precompiled.h']
  game_module_pch_headers = [common_header_path]
else
  # The legacy engine and SDK sources use precompiled.h as their common
  # translation-unit prefix. Disabling compiler PCH generation must include
  # the same declarations normally rather than silently changing the source
  # contract and producing hundreds of order-dependent compile failures.
  if cpp.get_argument_syntax() == 'msvc'
    common_header_cpp_args = ['/FI' + common_header_path]
  else
    common_header_cpp_args = ['-include', common_header_path]
  endif
endif

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
game_idlib_mp_include_dirs = [root_include_dir, src_include_dir]
bse_client_include_dirs = [root_include_dir, src_include_dir, src_bse_include_dir]

if needs_game_headers
  staged_game_include_dir = include_directories(game_stage_game_include_rel)
  staged_mpgame_include_dir = include_directories(game_stage_mpgame_include_rel)
  engine_include_dirs += [staged_game_include_dir]
  game_idlib_include_dirs += [staged_game_include_dir]
  # the multiplayer idlib flavour resolves precompiled.h's
  # "../mpgame/Game_local.h" through the staged mpgame tree
  game_idlib_mp_include_dirs += [staged_mpgame_include_dir, staged_game_include_dir]
  game_include_dirs += [staged_game_include_dir, staged_mpgame_include_dir]
  bse_client_include_dirs += [staged_game_include_dir]
endif

engine_cpp_args = shared_cpp_args + common_header_cpp_args + ['-D__DOOM_DLL__'] + linux_cross_dso_sanitizer_cpp_args
engine_c_args = shared_c_args
# client-only flavor: module-only clients compile the loader/backend for the
# shed-renderer shape (dedicated and library targets stay static-shaped)
client_engine_cpp_args = engine_cpp_args
if renderer_module_only_client
  client_engine_cpp_args += ['-DOPENQ4_RENDERER_MODULE_ONLY']
endif
engine_objcpp_args = shared_objcpp_args + ['-D__DOOM_DLL__']
engine_objc_args = shared_objc_args
bse_cpp_args = shared_cpp_args + common_header_cpp_args + linux_cross_dso_sanitizer_cpp_args
bse_c_args = shared_c_args
bse_library = disabler()

if build_engine
  openq4_dedicated_sources = openq4_engine_sources
  if windows_gl_free_ded
    # ded keeps the static renderer while the client sheds it
    openq4_dedicated_sources = files(dedicated_engine_source_paths)
    openq4_dedicated_sources += [openq4_paks_generated_header, generated_version_header]
    if build_games
      openq4_dedicated_sources += [savegame_compat_header]
    endif
    openq4_dedicated_sources += openq4_version_resources
  endif
  if host_system == 'linux'
    openq4_dedicated_sources = files(dedicated_engine_source_paths)
    openq4_dedicated_sources += [openq4_paks_generated_header, generated_version_header]
    if build_games
      openq4_dedicated_sources += [savegame_compat_header]
    endif
  endif

  bse_library = static_library(
    'openq4_bse',
    bse_sources,
    include_directories: bse_client_include_dirs,
    cpp_args: bse_cpp_args,
    c_args: bse_c_args,
    cpp_pch: pch_headers,
    pic: host_system == 'linux' or host_system == 'android',
    install: false,
  )

  # CPU image utilities shared between the engine and (post module-split) the
  # renderer modules; compiled with engine args so codegen matches the
  # pre-carve statically-linked behavior
  imagetools_library = static_library(
    'openq4_imagetools',
    openq4_imagetools_sources,
    include_directories: engine_include_dirs,
    cpp_args: engine_cpp_args,
    c_args: engine_c_args,
    cpp_pch: pch_headers,
    pic: host_system == 'linux' or host_system == 'android',
    install: false,
  )

  # CPU render-geometry shared between dmap (engine) and the renderer;
  # compiled with engine args so codegen matches the pre-carve build
  render_geo_library = static_library(
    'openq4_render_geo',
    openq4_render_geo_sources,
    include_directories: engine_include_dirs,
    cpp_args: engine_cpp_args,
    c_args: engine_c_args,
    cpp_pch: pch_headers,
    pic: host_system == 'linux' or host_system == 'android',
    install: false,
  )
endif

if build_engine
  client_link_with = [bse_library, imagetools_library, render_geo_library]
  dedicated_link_with = [bse_library, imagetools_library, render_geo_library]

  if build_sigmatouch
    # The external host uses its patched SDL callback/input API. Stock SDL3
    # cannot link this glue, even though it is enough for standalone Android.
    if not fs_mod.is_file(sigmatouch_root / '..' / '..' / 'SDL_beloko_extra.h')
      error('Sigma Touch requires its host SDL_beloko_extra.h two levels above sigmatouch_root, as included by Clibs_OpenTouch.')
    endif
    foreach callback : ['SDL_SetSwapBufferCallBack', 'SDL_ShowMouseCallBack', 'SDL_MouseMoveCallBack', 'SDL_SetShowKeyboardCallBack', 'SDL_NewEGLCreated', 'SDL_SendKeyboardText', 'SDL_SendKeyboardKey', 'Android_JNI_SendMessage']
      if not cpp.has_function(callback, prefix: 'extern "C" void ' + callback + '();', dependencies: sdl3_dep)
        error('Sigma Touch requires a compatible host SDL3 exporting ' + callback + '; the standard standalone SDL3 build cannot be used.')
      endif
    endforeach
    sigmatouch_sources = files(
      'mobile/game_interface.cpp',
      'mobile/touch_interface_quake4.cpp',
      sigmatouch_root / 'sigma/android_jni.cpp',
      sigmatouch_root / 'touch_interface_base.cpp',
      sigmatouch_root / 'LogWritter.c',
      sigmatouch_root / 'fts.c',
      sigmatouch_root / 'rand.c',
    )
    sigmatouch_deps = [
      sdl3_dep,
      cc.find_library('touchcontrols', dirs: [android_deps_root / 'lib'], required: true),
      cc.find_library('saffal', dirs: [android_deps_root / 'lib'], required: true),
      cc.find_library('GLESv3', required: true),
      cc.find_library('EGL', required: true),
      cc.find_library('log', required: true),
    ]
    # The host's headers have their own types; do not inject the engine PCH.
    sigmatouch_library = static_library(
      'openq4_sigmatouch',
      sigmatouch_sources,
      include_directories: [include_directories('mobile'), include_directories(sigmatouch_root), include_directories(sigmatouch_controls_root)],
      dependencies: sigmatouch_deps,
      cpp_args: ['-DOPENQ4_SIGMATOUCH=1', '-DUSE_SDL3=1', '-DENGINE_NAME="quake4"'],
      c_args: ['-DOPENQ4_SIGMATOUCH=1', '-DUSE_SDL3=1', '-DENGINE_NAME="quake4"'],
      pic: true,
      install: false,
    )
    # JNI entry points are discovered by the VM, not by linker references.
    # link_whole keeps that glue in the application shared library.
  endif

  if host_system == 'android'
    android_link_whole = []
    if build_sigmatouch
      android_link_whole += sigmatouch_library
      client_deps += sigmatouch_deps
    endif
    shared_library(
      engine_client_binary_name,
      openq4_engine_sources,
      include_directories: engine_include_dirs,
      dependencies: client_deps,
      link_with: client_link_with,
      link_whole: android_link_whole,
      cpp_args: client_engine_cpp_args,
      c_args: engine_c_args,
      cpp_pch: pch_headers,
      link_args: engine_link_args + ['-Wl,-z,defs'],
      name_prefix: 'lib',
      install: true,
      install_dir: install_engine_dir,
    )
  elif host_system == 'windows'
    executable(
      engine_client_binary_name,
      openq4_engine_sources,
      include_directories: engine_include_dirs,
      dependencies: client_deps,
      link_with: client_link_with,
      cpp_args: client_engine_cpp_args,
      c_args: engine_c_args,
      cpp_pch: pch_headers,
      win_subsystem: 'windows',
      link_args: engine_link_args,
      install: true,
      install_dir: install_root_dir,
    )
  elif host_system == 'darwin'
    executable(
      engine_client_binary_name,
      openq4_engine_sources,
      include_directories: engine_include_dirs,
      dependencies: client_deps,
      link_with: client_link_with,
      cpp_args: client_engine_cpp_args,
      c_args: engine_c_args,
      objcpp_args: engine_objcpp_args,
      objc_args: engine_objc_args,
      cpp_pch: pch_headers,
      link_args: engine_link_args,
      install_rpath: macos_openal_install_rpath,
      install: true,
      install_dir: install_root_dir,
    )
  else
    executable(
      engine_client_binary_name,
      openq4_engine_sources,
      include_directories: engine_include_dirs,
      dependencies: client_deps,
      link_with: client_link_with,
      cpp_args: client_engine_cpp_args,
      c_args: engine_c_args,
      cpp_pch: pch_headers,
      link_args: engine_link_args,
      pie: linux_executable_pie,
      install: true,
      install_dir: install_root_dir,
    )
  endif

  dedicated_engine_cpp_args = engine_cpp_args
  dedicated_engine_c_args = engine_c_args
  if host_system == 'linux'
    dedicated_engine_cpp_args = []
    foreach arg : engine_cpp_args
      if not ['-DUSE_SDL3=1', '-DOPENQ4_HAVE_X11_HELPERS=1'].contains(arg)
        dedicated_engine_cpp_args += arg
      endif
    endforeach
    dedicated_engine_c_args = []
    foreach arg : engine_c_args
      if not ['-DUSE_SDL3=1', '-DOPENQ4_HAVE_X11_HELPERS=1'].contains(arg)
        dedicated_engine_c_args += arg
      endif
    endforeach
  endif

  dedicated_cpp_args = dedicated_engine_cpp_args + ['-DID_DEDICATED']
  dedicated_c_args = dedicated_engine_c_args + ['-DID_DEDICATED']
  if windows_gl_free_ded
    # GLAPI=extern strips the dllimport decoration from GLEW's GL 1.1
    # declarations so the dedicated GL stub's plain definitions link cleanly
    dedicated_cpp_args += ['-DGLAPI=extern']
    dedicated_c_args += ['-DGLAPI=extern']
  endif
  dedicated_objcpp_args = engine_objcpp_args + ['-DID_DEDICATED']
  dedicated_objc_args = engine_objc_args + ['-DID_DEDICATED']

  if host_system == 'windows'
    executable(
      engine_ded_binary_name,
      openq4_dedicated_sources,
      include_directories: engine_include_dirs,
      dependencies: dedicated_deps,
      link_with: dedicated_link_with,
      cpp_args: dedicated_cpp_args,
      c_args: dedicated_c_args,
      cpp_pch: pch_headers,
      win_subsystem: 'windows',
      link_args: engine_link_args,
      install: true,
      install_dir: install_root_dir,
    )
  elif host_system == 'darwin'
    executable(
      engine_ded_binary_name,
      openq4_dedicated_sources,
      include_directories: engine_include_dirs,
      dependencies: dedicated_deps,
      link_with: dedicated_link_with,
      cpp_args: dedicated_cpp_args,
      c_args: dedicated_c_args,
      objcpp_args: dedicated_objcpp_args,
      objc_args: dedicated_objc_args,
      cpp_pch: pch_headers,
      link_args: engine_link_args,
      install_rpath: macos_openal_install_rpath,
      install: true,
      install_dir: install_root_dir,
    )
  elif host_system != 'android'
    executable(
      engine_ded_binary_name,
      openq4_dedicated_sources,
      include_directories: engine_include_dirs,
      dependencies: dedicated_deps,
      link_with: dedicated_link_with,
      cpp_args: dedicated_cpp_args,
      c_args: dedicated_c_args,
      cpp_pch: pch_headers,
      link_args: engine_link_args,
      pie: linux_executable_pie,
      install: true,
      install_dir: install_root_dir,
    )
  endif
endif

game_common_cpp_args = shared_cpp_args + common_header_cpp_args + ['-DGAME_DLL'] + linux_cross_dso_sanitizer_cpp_args
game_common_c_args = shared_c_args
game_target_override_options = ['cpp_std=c++17']
game_module_link_args = []
game_module_link_depends = []
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
elif host_system == 'darwin'
  foreach hardening_arg : ['-Wl,-dead_strip', '-Wl,-headerpad_max_install_names']
    if cpp.has_link_argument(hardening_arg)
      game_module_link_args += hardening_arg
    endif
  endforeach
  # Darwin peer of the Linux version script and the Windows .def files: a game
  # module carries its own idlib and its own flavour of the game types, so
  # exporting anything beyond the entry point makes two disagreeing definitions
  # of the same symbol visible to the loader at once (issue #90).
  darwin_game_module_export_list = files('tools/build/darwin_game_module.exp')
  game_module_link_args += '-Wl,-exported_symbols_list,' + darwin_game_module_export_list[0].full_path()
  game_module_link_depends += darwin_game_module_export_list
elif host_system == 'linux' or host_system == 'android'
  game_module_link_args += linux_module_link_hardening_args
  if cpp.has_link_argument('-Wl,-z,defs')
    game_module_link_args += '-Wl,-z,defs'
  endif
  linux_game_module_export_map = files('tools/build/linux_game_module.map')
  game_module_link_args += '-Wl,--version-script=' + linux_game_module_export_map[0].full_path()
  game_module_link_depends += linux_game_module_export_map
endif

game_idlib_library = disabler()
game_idlib_library_mp = disabler()
if build_engine or build_games
  # One archive per game flavour. src/idlib/precompiled.h includes
  # mpgame/Game_local.h under -DGAME_MPAPI and game/Game_local.h otherwise, and
  # every idlib translation unit includes precompiled.h. Linking one
  # single-player-flavoured archive into both modules therefore put two
  # different definitions of the same game types into each multiplayer binary,
  # which is an ODR violation the linker resolves however it likes.
  game_idlib_library = static_library(
    'openq4_game_idlib',
    game_idlib_sources,
    include_directories: game_idlib_include_dirs,
    cpp_args: game_common_cpp_args,
    c_args: game_common_c_args,
    cpp_pch: game_idlib_pch_headers,
    override_options: game_target_override_options,
    pic: host_system == 'linux' or host_system == 'android',
    gnu_symbol_visibility: host_system == 'windows' ? 'default' : 'hidden',
    install: false,
  )
  game_idlib_library_mp = static_library(
    'openq4_game_idlib_mp',
    game_idlib_sources,
    include_directories: game_idlib_mp_include_dirs,
    cpp_args: game_common_cpp_args + ['-DGAME_MPAPI'],
    c_args: game_common_c_args,
    cpp_pch: game_idlib_pch_headers,
    override_options: game_target_override_options,
    pic: host_system == 'linux' or host_system == 'android',
    gnu_symbol_visibility: host_system == 'windows' ? 'default' : 'hidden',
    install: false,
  )
endif

# OpenGL renderer module (Phase B8, docs/dev/plans/2026-07-16-vulkan-renderer-phase-b.md).
# Built from the same renderer sources the engine still links statically; the
# engine keeps its static renderer until the module soak completes. Requires
# the SDL3 window-services seam (gl_ContextSDL3.cpp compiles standalone via
# OPENQ4_SDL3_CONTEXT_IMPL and drives the engine window through
# renderWindowServices_t).
build_renderer_gl = get_option('build_renderer_gl')
if build_renderer_gl and (host_system == 'darwin' or host_system == 'android' or platform_backend != 'sdl3')
  build_renderer_gl = false
endif

# Vulkan module availability needs the SDL3 window-services seam on every host.
# macOS is admitted through MoltenVK, a Vulkan-on-Metal translation layer, per
# docs/dev/macos-moltenvk-decision.md; the module is built and shipped there but
# stays opt-in behind r_renderApi vulkan with OpenGL as the default renderer.
# MoltenVK is not a native Metal renderer and must never be described as one.
build_renderer_vk = get_option('build_renderer_vk')
if build_renderer_vk and (platform_backend != 'sdl3' or host_system == 'android')
  build_renderer_vk = false
endif
renderer_vk_binary_name = 'renderer-vk_' + binary_arch

renderer_gl_binary_name = 'renderer-gl_' + binary_arch

# emileb's GLES renderer is required on Android and explicitly selectable on
# desktop. Never silently ship an ANGLE copied from another installed product.
renderer_gles_binary_name = 'renderer-gles_' + binary_arch
gles_option = get_option('build_renderer_gles')
build_renderer_gles = build_engine and (gles_option.enabled() or (host_system == 'android' and not gles_option.disabled()))
if build_engine and host_system == 'android' and gles_option.disabled()
  error('Android requires the GLES renderer module.')
endif
if build_renderer_gles and not use_sdl3_backend
  error('build_renderer_gles requires platform_backend=sdl3.')
endif
gles_root = get_option('gles_root').strip()
angle_staged_dir = meson.project_source_root() / '.tmp' / 'angle-macos'
angle_gles_lib = disabler()
gles_deps = []
gles_development_angle = false
if build_renderer_gles
  if host_system == 'android'
    gles_deps = [cc.find_library('GLESv3', required: true), cc.find_library('EGL', required: true), cc.find_library('log', required: true)]
  else
    gles_library_dirs = []
    if gles_root != ''
      gles_library_dirs += [gles_root / 'lib']
    elif host_system == 'darwin'
      gles_library_dirs += [angle_staged_dir]
      gles_development_angle = true
    endif
    angle_gles_lib = cpp.find_library('GLESv2', dirs: gles_library_dirs, required: true)
    gles_deps = [angle_gles_lib, cpp.find_library('EGL', dirs: gles_library_dirs, required: true)]
  endif
endif
if build_engine and (build_renderer_gl or build_renderer_vk or build_renderer_gles)
  # the modules' own idlib copy, compiled with engine flavor so codegen and
  # interface layout match the renderer sources
  renderer_idlib_library = static_library(
    'openq4_renderer_idlib',
    game_idlib_sources,
    include_directories: engine_include_dirs,
    cpp_args: engine_cpp_args,
    c_args: engine_c_args,
    cpp_pch: pch_headers,
    pic: host_system == 'linux' or host_system == 'android',
    install: false,
  )
endif
if build_renderer_gl and build_engine
  renderer_gl_module_args = engine_cpp_args + [
    '-DOPENQ4_RENDERER_MODULE',
    '-DOPENQ4_RENDERER_GL_MODULE',
    '-DOPENQ4_SDL3_CONTEXT_IMPL',
  ]
  renderer_gl_link_args = []
  renderer_gl_link_depends = []
  renderer_gl_deps = [glew_dep]
  if host_system == 'windows'
    # GLEW resolves GL procs through wglGetProcAddress on Windows
    renderer_gl_deps += [cc.find_library('opengl32', required: true)]
    renderer_gl_link_args += ['/FIXED:NO', '/LARGEADDRESSAWARE']
  elif host_system == 'linux'
    # direct GL 1.x entry points (glReadBuffer/glDrawBuffer/...) link against
    # libGL; everything newer resolves through GLEW -> window services
    renderer_gl_deps += [
      cc.find_library('GL', required: true),
      cc.find_library('dl', required: false),
      cc.find_library('m', required: false),
    ]
    renderer_gl_link_args += linux_module_link_hardening_args
    if cpp.has_link_argument('-Wl,-z,defs')
      renderer_gl_link_args += '-Wl,-z,defs'
    endif
    renderer_gl_export_map = files('tools/build/linux_renderer_module.map')
    renderer_gl_link_args += '-Wl,--version-script=' + renderer_gl_export_map[0].full_path()
    renderer_gl_link_depends += renderer_gl_export_map
  endif

  renderer_gl_module_target = shared_module(
    renderer_gl_binary_name,
    openq4_renderer_gl_sources,
    include_directories: engine_include_dirs,
    cpp_args: renderer_gl_module_args,
    c_args: engine_c_args,
    cpp_pch: pch_headers,
    link_with: [renderer_idlib_library, imagetools_library, render_geo_library],
    dependencies: renderer_gl_deps,
    link_args: renderer_gl_link_args,
    link_depends: renderer_gl_link_depends,
    gnu_symbol_visibility: host_system == 'linux' ? 'hidden' : 'default',
    name_prefix: '',
    install: true,
    install_dir: install_root_dir,
  )
endif

# Experimental macOS OpenGL ES clear probe (Android GLES backend bring-up).
#
# openQ4's GL 1.x entry points bind at link time, so on macOS they resolve from
# OpenGL.framework and an EGL/ANGLE ES context is never actually driven. This
# target is the minimal proof of the opposite arrangement: link ANGLE's
# libGLESv2 and no desktop GL, then clear and present through it. It is the
# link-shape a future renderer-gles module has to use.
#
# Local development only: it needs an ANGLE staged by
# tools/build/prepare_macos_angle.sh and is never installed or packaged.
build_gles_probe = host_system == 'darwin' and build_renderer_gles
if build_gles_probe
  if not angle_gles_lib.found()
    warning('ANGLE not staged; skipping the GLES clear probe. Run tools/build/prepare_macos_angle.sh to enable it.')
  else
    executable(
      'openq4-gles-probe',
      files('src/renderer/GLES/gles_clear_probe.cpp'),
      include_directories: engine_include_dirs,
      dependencies: [sdl3_dep, angle_gles_lib],
      cpp_args: engine_cpp_args,
      # @loader_path so the staged ANGLE resolves next to the probe binary,
      # matching how a shipped module would find its own runtime.
      link_args: ['-Wl,-rpath,@loader_path'],
      install: false,
    )
  endif
endif

# Experimental OpenGL ES renderer module.
#
# The shared renderer front-end again, but with GL resolved through
# renderer/GLES/qgl_gles.h against a real ES driver instead of GLEW against
# desktop GL. Unlike renderer-vk this KEEPS the ModernGL translation units --
# GLES is still GL, so the executor, shader library, state cache, upload
# manager and render-graph resources are reused rather than rewritten -- and
# keeps tr_backend.cpp, whose five references into the dropped legacy TUs are
# supplied by renderer/GLES/gles_Backend.cpp.
#
# Never installed: the ANGLE it links is a local development aid.
if build_renderer_gles
  renderer_gles_source_discovery = run_command(
    py,
    meson.project_source_root() / 'tools' / 'build' / 'meson_sources.py',
    '--emit',
    'renderer_gles',
    check: true,
  )
  renderer_gles_source_paths = renderer_gles_source_discovery.stdout().strip().split('\n')
  if renderer_gles_source_paths.length() == 0
    error('tools/build/meson_sources.py returned no renderer_gles source files.')
  endif
  renderer_gles_sources = files(renderer_gles_source_paths)

  renderer_gles_module_args = engine_cpp_args + [
    '-DOPENQ4_RENDERER_MODULE',
    '-DOPENQ4_RENDERER_GLES_MODULE',
    '-DOPENQ4_SDL3_CONTEXT_IMPL',
    '-DOPENQ4_RENDERER_BACKEND_NAME="gles"',
    '-DOPENQ4_RENDERER_BACKEND_DESC="openQ4 OpenGL ES renderer module"',
  ]

  renderer_gles_export_list = []
  renderer_gles_link_args = []
  if host_system == 'darwin'
    renderer_gles_export_list = files('tools/build/macos_renderer_module.exp')
    renderer_gles_link_args = [
      '-Wl,-exported_symbols_list,' + renderer_gles_export_list[0].full_path(),
      '-Wl,-dead_strip',
      '-Wl,-headerpad_max_install_names',
      '-Wl,-install_name,@loader_path/' + renderer_gles_binary_name + '.dylib',
      '-Wl,-rpath,@loader_path',
    ]
  elif host_system == 'linux' or host_system == 'android'
    renderer_gles_export_list = files('tools/build/linux_renderer_module.map')
    renderer_gles_link_args = linux_module_link_hardening_args + [
      '-Wl,--version-script=' + renderer_gles_export_list[0].full_path(),
      '-Wl,-z,defs',
    ]
    gles_deps += [cc.find_library('m', required: true), cc.find_library('dl', required: true)]
  endif

  # shared_library, not shared_module: meson links shared_module on darwin as an
  # MH_BUNDLE with -undefined dynamic_lookup, which drops the undefined-symbol
  # closure. That closure is the point here -- it is what proves the module has
  # no desktop GL left in it.
  renderer_gles_module_target = shared_library(
    renderer_gles_binary_name,
    renderer_gles_sources,
    include_directories: [
      engine_include_dirs,
      include_directories('src/external/gles'),
    ],
    cpp_args: renderer_gles_module_args,
    c_args: engine_c_args,
    cpp_pch: pch_headers,
    link_with: [renderer_idlib_library, imagetools_library, render_geo_library],
    dependencies: gles_deps,
    link_args: renderer_gles_link_args,
    link_depends: renderer_gles_export_list,
    gnu_symbol_visibility: 'hidden',
    name_prefix: module_filename_prefix,
    install: not gles_development_angle,
    install_dir: install_engine_dir,
  )
endif

# Native Vulkan renderer module, full-module shape (Phase C,
# docs/dev/plans/2026-07-17-vulkan-phase-c.md): the shared renderer front-end
# with the GL backend replaced by the Vulkan backend. Mixed front-end TUs keep
# their GL call sites satisfied by the hook-resolving GLEW flavor + the
# module's GL stub TU; no GL and no SDL are linked.
if build_renderer_vk and build_engine
  renderer_vk_source_discovery = run_command(
    py,
    meson.project_source_root() / 'tools' / 'build' / 'meson_sources.py',
    '--emit',
    'renderer_vk',
    check: true,
  )
  renderer_vk_source_paths = renderer_vk_source_discovery.stdout().strip().split('\n')
  if renderer_vk_source_paths.length() == 0
    error('tools/build/meson_sources.py returned no renderer_vk source files.')
  endif
  renderer_vk_sources = files(renderer_vk_source_paths)
  renderer_vk_sources += files('src/external/volk/volk.c')

  renderer_vk_include_dirs = [
    engine_include_dirs,
    include_directories(
      'src/external/vulkan/include',
      'src/external/volk',
      'src/external/vma',
    ),
  ]
  renderer_vk_module_args = engine_cpp_args + [
    '-DOPENQ4_RENDERER_MODULE',
    '-DOPENQ4_RENDERER_VK_MODULE',
    '-DOPENQ4_RENDERER_BACKEND_NAME="vulkan"',
    '-DOPENQ4_RENDERER_BACKEND_DESC="openQ4 native Vulkan renderer module"',
    '-DVMA_STATIC_VULKAN_FUNCTIONS=0',
    '-DVMA_DYNAMIC_VULKAN_FUNCTIONS=1',
    # VK_KHR_portability_subset lives in vulkan_beta.h. Defined for every host so
    # volk.h and the C++ translation units agree on the header set (volk exposes
    # no beta entry points, so this adds declarations only).
    '-DVK_ENABLE_BETA_EXTENSIONS',
  ]
  renderer_vk_c_args = engine_c_args + ['-DVK_ENABLE_BETA_EXTENSIONS']
  if host_system == 'windows'
    renderer_vk_module_args += ['-DVK_USE_PLATFORM_WIN32_KHR', '-DGLAPI=extern']
    renderer_vk_c_args += ['-DGLAPI=extern']
  endif
  renderer_vk_link_args = []
  renderer_vk_link_depends = []
  renderer_vk_deps = [glew_dedicated_dep]
  if host_system == 'windows'
    renderer_vk_link_args += ['/FIXED:NO', '/LARGEADDRESSAWARE']
  elif host_system == 'linux'
    renderer_vk_deps += [
      cc.find_library('dl', required: false),
      cc.find_library('m', required: false),
    ]
    renderer_vk_link_args += linux_module_link_hardening_args
    if cpp.has_link_argument('-Wl,-z,defs')
      renderer_vk_link_args += '-Wl,-z,defs'
    endif
    renderer_vk_export_map = files('tools/build/linux_renderer_module.map')
    renderer_vk_link_args += '-Wl,--version-script=' + renderer_vk_export_map[0].full_path()
    renderer_vk_link_depends += renderer_vk_export_map
  elif host_system == 'darwin'
    # The macOS client keeps its statically linked OpenGL renderer, so this
    # module ships alongside a second copy of the renderer/idlib symbols.
    # Exporting only GetRenderAPI keeps the two from interposing, mirroring the
    # Linux version script. -headerpad_max_install_names leaves room for the
    # packaging step to rewrite the install name inside the app bundle.
    renderer_vk_export_list = files('tools/build/macos_renderer_module.exp')
    renderer_vk_link_args += [
      '-Wl,-exported_symbols_list,' + renderer_vk_export_list[0].full_path(),
      '-Wl,-dead_strip',
      '-Wl,-headerpad_max_install_names',
      '-Wl,-install_name,@loader_path/' + renderer_vk_binary_name + '.dylib',
    ]
    renderer_vk_link_depends += renderer_vk_export_list
  endif

  if host_system == 'darwin'
    # shared_library, not shared_module: meson links shared_module on darwin as
    # an MH_BUNDLE with -undefined dynamic_lookup, which drops the
    # undefined-symbol closure the other hosts get and breaks the otool -D /
    # install_name_tool expectations the universal2 assembler relies on.
    # Mirrors the game modules in content/baseoq4/meson.build.
    renderer_vk_module_target = shared_library(
      renderer_vk_binary_name,
      renderer_vk_sources,
      include_directories: renderer_vk_include_dirs,
      cpp_args: renderer_vk_module_args,
      c_args: renderer_vk_c_args,
      cpp_pch: pch_headers,
      link_with: [renderer_idlib_library, imagetools_library, render_geo_library],
      dependencies: renderer_vk_deps,
      link_args: renderer_vk_link_args,
      link_depends: renderer_vk_link_depends,
      gnu_symbol_visibility: 'hidden',
      name_prefix: '',
      name_suffix: 'dylib',
      install: true,
      install_dir: install_root_dir,
    )
  else
    renderer_vk_module_target = shared_module(
      renderer_vk_binary_name,
      renderer_vk_sources,
      include_directories: renderer_vk_include_dirs,
      cpp_args: renderer_vk_module_args,
      c_args: renderer_vk_c_args,
      cpp_pch: pch_headers,
      link_with: [renderer_idlib_library, imagetools_library, render_geo_library],
      dependencies: renderer_vk_deps,
      link_args: renderer_vk_link_args,
      link_depends: renderer_vk_link_depends,
      gnu_symbol_visibility: host_system == 'linux' ? 'hidden' : 'default',
      name_prefix: '',
      install: true,
      install_dir: install_root_dir,
    )
  endif
endif

if build_engine or (build_games and (build_game_sp or build_game_mp))
  subdir('content/baseoq4')
endif

if build_games and build_game_sp
  custom_target(
    'openq4_stage_game_sp_direct_run',
    input: game_sp_module_target,
    output: 'stage-game-sp-' + binary_arch + '.stamp',
    command: [
      py,
      meson.project_source_root() / 'tools' / 'build' / 'stage_direct_run_game_module.py',
      '--source',
      '@INPUT@',
      '--destination',
      meson.current_build_dir() / 'baseoq4' / (module_filename_prefix + game_sp_binary_name + game_module_filename_suffix),
      '--stamp',
      '@OUTPUT@',
    ],
    depend_files: files('tools/build/stage_direct_run_game_module.py'),
    build_always_stale: true,
    build_by_default: true,
  )
endif

if build_games and build_game_mp
  custom_target(
    'openq4_stage_game_mp_direct_run',
    input: game_mp_module_target,
    output: 'stage-game-mp-' + binary_arch + '.stamp',
    command: [
      py,
      meson.project_source_root() / 'tools' / 'build' / 'stage_direct_run_game_module.py',
      '--source',
      '@INPUT@',
      '--destination',
      meson.current_build_dir() / 'baseoq4' / (module_filename_prefix + game_mp_binary_name + game_module_filename_suffix),
      '--stamp',
      '@OUTPUT@',
    ],
    depend_files: files('tools/build/stage_direct_run_game_module.py'),
    build_always_stale: true,
    build_by_default: true,
  )
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
  rename: 'openQ4.ico',
)

install_data('LICENSE', 'CONTRIBUTORS.md', install_dir: install_root_dir)
install_data('LICENSES/KHRONOS-GLES-MIT.txt', install_dir: install_root_dir / 'licenses')
install_data('docs/licenses/RmlUi.txt', install_dir: install_root_dir / 'licenses')
install_data('docs/licenses/JsonCpp.txt', install_dir: install_root_dir / 'licenses')
install_data('docs/licenses/libtess2.txt', install_dir: install_root_dir / 'licenses')

install_data(
  'assets/icons/quake4.icns',
  install_dir: install_root_dir,
  rename: 'openQ4.icns',
)

if host_system != 'windows'
  install_data(
    'assets/splash/quake4_rt_bitmap_4001.bmp',
    install_dir: install_root_dir / 'assets' / 'splash',
  )
endif

if host_system == 'darwin'
  install_data(
    'tools/macos/collect_macos_support_info.sh',
    install_dir: install_root_dir,
    install_mode: 'rwxr-xr-x',
  )
endif

if host_system == 'linux'
  linux_desktop_conf = configuration_data()
  linux_desktop_conf.set('OPENQ4_CLIENT_BINARY', engine_client_binary_name)
  linux_desktop_conf.set('OPENQ4_PROJECT_VERSION', openq4_version_short)
  linux_desktop_conf.set('OPENQ4_PROJECT_VERSION_FULL', openq4_version_full)
  linux_steamdeck_launcher = configure_file(
    input: 'assets/linux/openQ4-steamdeck.in',
    output: 'openQ4-steamdeck',
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
    'Version base': openq4_version_base,
    'Version short': openq4_version_short,
    'Version full': openq4_version_full,
    'Version tag': openq4_version_tag,
    'Version date': openq4_version_date,
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
    'macOS graphics bridge': macos_graphics_bridge,
    'macOS OpenAL provider': macos_openal_provider,
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
    'Build Vulkan renderer module': build_renderer_vk and build_engine,
    'Vulkan renderer binary': renderer_vk_binary_name,
    'Resource compiler': resource_compiler.found(),
    'SDL3': use_sdl3_backend and sdl3_dep.found(),
    'Linux X11 helpers': linux_x11_helpers_enabled,
    'Linux executable PIE': linux_executable_pie,
    'GLEW': glew_dep.found(),
    'OpenAL': openal_dep.found(),
    'stb_vorbis': stb_vorbis_dep.found(),
    'Precompiled headers': get_option('use_pch'),
    'Dedicated server target': build_engine and host_system != 'android',
    'GLES renderer module': build_renderer_gles,
    'Sigma Touch host adapter': build_sigmatouch,
    'Linux dedicated headless link split': build_engine and host_system == 'linux',
    'Install root': install_root_dir,
  },
  section: 'Build Features',
  bool_yn: true,
)
