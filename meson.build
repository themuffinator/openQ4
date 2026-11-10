project(
  'openQ4',
  ['c', 'cpp'],
  version: '0.13.2',
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
  if host_cpu_family == 'aarch64'
    android_abi = 'arm64-v8a'
    # The Quest flavor is the phone build declared as a headset app; its stage
    # carries the marker that verify_android_apk.py --variant quest requires.
    android_variant = get_option('android_headset') ? 'quest' : 'phone'
  elif host_cpu_family == 'x86_64'
    if get_option('android_headset')
      error('The Meta Quest headset flavor is arm64-v8a only.')
    endif
    android_abi = 'x86_64'
    android_variant = 'emulator'
  else
    error('Android supports arm64-v8a phones and diagnostic x86_64 emulators only.')
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
  install_root_dir = install_root_dir / 'android' / android_abi
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
game_libs_repo_root = meson.project_source_root()
game_libs_source_root = game_libs_repo_root / 'src'
game_libs_repo_summary = 'in-tree (src/game and src/mpgame)'
game_stage_root = meson.project_source_root()
game_stage_game_root = game_stage_root / 'src' / 'game'
game_stage_mpgame_root = game_stage_root / 'src' / 'mpgame'
game_stage_game_include_rel = 'src/game'
game_stage_mpgame_include_rel = 'src/mpgame'
bse_source_root = meson.project_source_root() / 'src' / 'bse'
game_sp_module_defs_file = ''
game_mp_module_defs_file = ''

if not build_engine and not build_games
  error('At least one build target must be enabled. Use -Dbuild_engine=true and/or -Dbuild_games=true.')
endif

if get_option('build_native_tests')
  frame_work_profile_test = executable('openq4-frame-work-profile-test',
    files('tools/tests/native/FrameWorkProfileTest.cpp'),
    include_directories: root_include_dir, install: false)
  test('openq4-frame-work-profile', frame_work_profile_test, timeout: 30)
  bse_particle_order_test = executable('openq4-bse-particle-order-test',
    files('tools/tests/native/BSEParticleOrderTest.cpp'),
    include_directories: root_include_dir, install: false)
  test('openq4-bse-particle-order', bse_particle_order_test, timeout: 30)
  objective_marker_layout_test = executable('openq4-objective-marker-layout-test',
    files('tools/tests/native/ObjectiveMarkerLayoutTest.cpp'),
    include_directories: root_include_dir, install: false)
  test('openq4-objective-marker-layout', objective_marker_layout_test, timeout: 30)
  test('openq4-vulkan-material-sources', py,
    args: [files('tools/build/vulkan_material_sources.py'), '--check'], timeout: 30)
  test('openq4-vulkan-qualification-report', py,
    args: [files('tools/tests/renderer_vulkan_qualification.py'), 'self-test'], timeout: 30)
  test('openq4-vulkan-presentation-report', py,
    args: [files('tools/tests/renderer_vulkan_presentation.py'), '--self-test'], timeout: 30)
  test('openq4-vulkan-material-cache', py,
    args: [files('tools/tests/renderer_vulkan_material_cache.py')], timeout: 60)
  openq4_editor_document_test = executable(
    'openq4-editor-document-test',
    files(
      'tools/tests/native/EditorDocumentTest.cpp',
      'src/tools/leveleditor/MapDocument.cpp',
      'src/tools/leveleditor/MapWorkspace.cpp',
    ),
    include_directories: root_include_dir,
    install: false,
  )
  test('openq4-editor-document', openq4_editor_document_test, timeout: 30)
  openq4_editor_authoring_test = executable(
    'openq4-editor-authoring-test',
    files(
      'tools/tests/native/EditorAuthoringTest.cpp',
      'src/tools/leveleditor/MapDocument.cpp',
      'src/tools/leveleditor/MapWorkspace.cpp',
      'src/tools/leveleditor/MapGeometry.cpp',
      'src/tools/leveleditor/MapEditing.cpp',
      'src/tools/leveleditor/MapValidator.cpp',
      'src/tools/leveleditor/MapBuild.cpp',
      'src/tools/leveleditor/EditorState.cpp',
    ),
    include_directories: root_include_dir,
    install: false,
  )
  test('openq4-editor-authoring', openq4_editor_authoring_test, timeout: 120)

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

  openq4_crc32_test = executable(
    'openq4-crc32-test', files('tools/tests/native/Crc32Test.cpp'),
    include_directories: root_include_dir, install: false,
  )
  test('openq4-crc32', openq4_crc32_test, timeout: 30)

  openq4_save_dict_under_test = custom_target(
    'openq4-save-dict-under-test', input: files('src/game/gamesys/SaveGame.cpp', 'src/mpgame/gamesys/SaveGame.cpp'),
    output: 'SaveDictUnderTest.inc',
    command: [py, files('tools/tests/generate_save_dict_test.py'), '@INPUT0@', '@INPUT1@', '@OUTPUT@'],
  )
  openq4_save_dict_test = executable(
    'openq4-save-dict-test', [files('tools/tests/native/SaveDictTest.cpp'), openq4_save_dict_under_test],
    include_directories: root_include_dir, install: false,
  )
  test('openq4-save-dict', openq4_save_dict_test, timeout: 30)

  dict_identity_source = custom_target(
    'openq4-dict-identity-source', input: files('src/idlib/Dict.cpp'),
    output: 'DictIdentityUnderTest.inc',
    command: [py, files('tools/tests/generate_dict_identity_test.py'), '@INPUT@', '@OUTPUT@'],
  )
  dict_identity_test = executable(
    'openq4-dict-identity-test', [files('tools/tests/native/DictIdentityTest.cpp'), dict_identity_source],
    include_directories: root_include_dir, install: false,
  )
  test('openq4-dict-identity', dict_identity_test, timeout: 30)

  openq4_capture_resample_under_test = custom_target(
    'openq4-capture-resample-under-test', input: files('src/renderer/RenderSystem.cpp'),
    output: 'CaptureResampleUnderTest.inc',
    command: [py, files('tools/tests/generate_capture_resample_test.py'), '@INPUT@', '@OUTPUT@'],
  )
  openq4_capture_resample_test = executable(
    'openq4-capture-resample-test',
    [files('tools/tests/native/CaptureResampleTest.cpp'), openq4_capture_resample_under_test],
    include_directories: root_include_dir, install: false,
  )
  test('openq4-capture-resample', openq4_capture_resample_test, timeout: 30)
  openq4_checkpoint_preview_under_test = custom_target(
    'openq4-checkpoint-preview-source',
    input: files('src/framework/Session.cpp'), output: 'CheckpointPreviewUnderTest.inc',
    command: [py, files('tools/tests/generate_checkpoint_preview_test.py'), '@INPUT@', '@OUTPUT@'],
  )
  openq4_checkpoint_preview_test = executable(
    'openq4-checkpoint-preview-test',
    [files('tools/tests/native/CheckpointPreviewStorageTest.cpp'), openq4_checkpoint_preview_under_test],
    include_directories: root_include_dir, install: false,
  )
  test('openq4-checkpoint-preview', openq4_checkpoint_preview_test, timeout: 30)

  checkpoint_storage_source = custom_target(
    'openq4-checkpoint-storage-source',
    input: files('src/framework/Session.cpp'), output: 'CheckpointStorageUnderTest.inc',
    command: [py, files('tools/tests/generate_checkpoint_storage_test.py'), '@INPUT@', '@OUTPUT@'],
  )
  checkpoint_storage_test = executable(
    'openq4-checkpoint-storage-test',
    [files('tools/tests/native/CheckpointStorageTest.cpp'), checkpoint_storage_source],
    include_directories: root_include_dir, install: false,
  )
  test('openq4-checkpoint-storage', checkpoint_storage_test, timeout: 30)

  checkpoint_commit_source = custom_target(
    'openq4-checkpoint-commit-source', input: files('src/framework/Session.cpp'),
    output: ['CheckpointCommitUnderTest.inc', 'CheckpointRecoveryUnderTest.inc', 'CheckpointPendingUnderTest.inc'],
    command: [py, files('tools/tests/generate_checkpoint_commit_test.py'), '@INPUT@', '@OUTPUT0@', '@OUTPUT1@', '@OUTPUT2@'],
  )
  checkpoint_commit_test = executable(
    'openq4-checkpoint-commit-test', [files('tools/tests/native/CheckpointCommitTest.cpp', 'src/framework/DurableFile.cpp'), checkpoint_commit_source],
    include_directories: root_include_dir, install: false,
  )
  test('openq4-checkpoint-commit', checkpoint_commit_test,
    args: [meson.project_source_root() / '.tmp' / 'checkpoint-commit-test'], timeout: 30)


  openq4_idlist_test = executable(
    'openq4-idlist-test', files('tools/tests/native/IdListTest.cpp'),
    include_directories: root_include_dir, install: false,
  )
  test('openq4-idlist', openq4_idlist_test, timeout: 30)

  openq4_cmdargs_under_test = custom_target(
    'openq4-cmdargs-under-test',
    input: files('src/idlib/CmdArgs.cpp', 'src/idlib/CmdArgs.h', 'src/idlib/Lib.h'),
    output: 'CmdArgsUnderTest.inc',
    command: [py, files('tools/tests/cmdargs_append_contract.py'), '--generate-native', '@OUTPUT@'],
  )
  openq4_cmdargs_test = executable(
    'openq4-cmdargs-test',
    [files('tools/tests/native/CmdArgsTest.cpp'), openq4_cmdargs_under_test],
    include_directories: root_include_dir, install: false,
  )
  test('openq4-cmdargs', openq4_cmdargs_test, timeout: 30)

  openq4_arena_sound_source = custom_target(
    'openq4-arena-sound-source',
    input: files('src/mpgame/MultiplayerGame.cpp', 'src/sound/snd_world.cpp', 'src/framework/Session.cpp', 'src/framework/Session_menu.cpp'),
    output: 'ArenaSoundPauseTest.cpp',
    command: [py, files('tools/tests/arena_sound_pause.py'), '--generate-native', '@OUTPUT@'],
  )
  openq4_arena_sound_test = executable(
    'openq4-arena-sound-test', openq4_arena_sound_source,
    cpp_args: ['-UNDEBUG'], install: false,
  )
  test('openq4-arena-sound', openq4_arena_sound_test, timeout: 30)

  ui_engine_thread_source = custom_target(
    'openq4-ui-engine-thread-source',
    output: 'UiEngineThread.cpp',
    depend_files: files('src/ui/RetainedUI.cpp', 'src/ui/UserInterface.cpp', 'src/ui/UserInterfaceLocal.h'),
    command: [py, files('tools/tests/ui_engine_thread.py'), '--generate-native', '@OUTPUT@'],
  )
  ui_engine_thread_test = executable(
    'openq4-ui-engine-thread', ui_engine_thread_source, dependencies: dependency('threads'), install: false,
  )
  test('openq4-ui-engine-thread', ui_engine_thread_test, timeout: 30)

  openal_pause_source = custom_target(
    'openq4-openal-device-pause-source',
    output: 'OpenALDevicePause.cpp',
    depend_files: files('src/sound/OpenAL/AL_SoundHardware.cpp', 'src/sound/OpenAL/AL_SoundHardware.h'),
    command: [py, files('tools/tests/openal_device_pause.py'), '--generate-native', '@OUTPUT@'],
  )
  openal_pause_test = executable('openq4-openal-device-pause', openal_pause_source, install: false)
  test('openq4-openal-device-pause', openal_pause_test, timeout: 30)
  android_lifecycle_source = custom_target(
    'openq4-android-lifecycle-source',
    output: 'AndroidLifecycle.cpp',
    depend_files: files('src/sys/sdl3/sdl3_backend.cpp', 'src/framework/Session.cpp', 'src/framework/KeyInput.cpp'),
    command: [py, files('tools/tests/android_lifecycle.py'), '--generate-native', '@OUTPUT@'],
  )
  android_async_source = custom_target(
    'openq4-android-async-source',
    output: 'AndroidAsync.cpp',
    depend_files: files('src/sys/sdl3/sdl3_backend.cpp', 'src/sys/android/android_main.cpp',
      'src/framework/Common.cpp', 'src/framework/Common.h', 'tools/tests/presentation_clock_ownership.py'),
    command: [py, files('tools/tests/android_async.py'), '--generate-native', '@OUTPUT@'],
  )
  android_savepath_source = custom_target(
    'openq4-android-savepath-source',
    output: 'AndroidSavePath.cpp',
    depend_files: files('src/sys/android/android_main.cpp', 'src/sys/android/android_public.h',
      'src/sys/posix/posix_main.cpp'),
    command: [py, files('tools/tests/android_savepath.py'), '--generate-native', '@OUTPUT@'],
  )
  android_savepath_test = executable('openq4-android-savepath', android_savepath_source, install: false)
  test('openq4-android-savepath', android_savepath_test, timeout: 30)
  android_touch_controls_test = executable(
    'openq4-android-touch-controls', files('tools/tests/native/AndroidTouchControlsTest.cpp'),
    include_directories: root_include_dir, install: false,
  )
  test('openq4-android-touch-controls', android_touch_controls_test, timeout: 30)
  android_touch_integration_source = custom_target(
    'openq4-android-touch-integration-source',
    input: files('src/sys/android/android_touch_controls.cpp'),
    output: 'AndroidTouchIntegration.cpp',
    command: [py, files('tools/tests/android_touch_controls.py'), '--generate-native', '@OUTPUT@'],
  )
  android_touch_integration_test = executable(
    'openq4-android-touch-integration', android_touch_integration_source,
    include_directories: root_include_dir, install: false,
  )
  test('openq4-android-touch-integration', android_touch_integration_test, timeout: 30)
  android_interface_setup_test = executable(
    'openq4-android-interface-setup', files('tools/tests/native/AndroidInterfaceSetupTest.cpp'),
    include_directories: root_include_dir, install: false,
  )
  test('openq4-android-interface-setup', android_interface_setup_test, timeout: 30)
  foreach platform : ['android', 'desktop']
    android_async_test = executable(
      'openq4-android-async-' + platform, android_async_source,
      cpp_args: platform == 'android' ? ['-D__ANDROID__=1'] : [], install: false,
    )
    test('openq4-android-async-' + platform, android_async_test, timeout: 30)

    android_lifecycle_test = executable(
      'openq4-android-lifecycle-' + platform, android_lifecycle_source,
      cpp_args: platform == 'android' ? ['-D__ANDROID__=1'] : [], install: false,
    )
    test('openq4-android-lifecycle-' + platform, android_lifecycle_test, timeout: 30)

    android_window_source = custom_target(
      'openq4-android-window-' + platform + '-source',
      input: files('src/sys/posix/posix_syscon.cpp', 'src/sys/sdl3/sdl3_backend.cpp'),
      output: 'AndroidWindow-' + platform + '.cpp',
      command: [py, files('tools/tests/android_window_lifecycle.py'), '--generate-native', platform, '@OUTPUT@'],
    )
    android_window_test = executable(
      'openq4-android-window-' + platform, android_window_source, install: false,
    )
    test('openq4-android-window-' + platform, android_window_test, timeout: 30)

    android_basepath_source = custom_target(
      'openq4-android-basepath-' + platform + '-source',
      input: files('src/framework/FileSystem.cpp'),
      output: 'AndroidBasePath-' + platform + '.cpp',
      command: [py, files('tools/tests/android_basepath.py'), '--generate-native', platform, '@OUTPUT@'],
    )
    android_basepath_test = executable(
      'openq4-android-basepath-' + platform, android_basepath_source, install: false,
    )
    test('openq4-android-basepath-' + platform, android_basepath_test, timeout: 30)
  endforeach

  restore_recovery_source = custom_target(
    'openq4-restore-recovery-source',
    input: files('src/framework/Common.cpp', 'src/framework/Session.cpp', 'src/framework/Session_menu.cpp'),
    output: 'RestoreRecovery.cpp',
    command: [py, files('tools/tests/savegame_restore_recovery.py'), '--generate-native', '@OUTPUT@'],
  )
  restore_recovery_test = executable(
    'openq4-restore-recovery-test', restore_recovery_source,
    cpp_args: ['-UNDEBUG'], install: false,
  )
  test('openq4-restore-recovery', restore_recovery_test, timeout: 30)

  foreach game_mode : ['game', 'mpgame']
    restore_events_source = custom_target(
      'openq4-restore-events-' + game_mode + '-source',
      input: files('src' / game_mode / 'gamesys' / 'Event.cpp', 'src/idlib/containers/LinkList.h', 'src/cm/CollisionModel.h'),
      output: 'RestoreEvents-' + game_mode + '.cpp',
      command: [py, files('tools/tests/savegame_restore_events.py'), '--generate-native', game_mode, '@OUTPUT@'],
    )
    restore_events_test = executable(
      'openq4-restore-events-' + game_mode, restore_events_source, cpp_args: ['-UNDEBUG'], install: false,
    )
    test('openq4-restore-events-' + game_mode, restore_events_test, timeout: 30)
    restore_af_source = custom_target(
      'openq4-restore-af-' + game_mode + '-source',
      input: files('src' / game_mode / 'AF.cpp', 'src' / game_mode / 'physics' / 'Physics_AF.cpp'),
      output: 'RestoreAF-' + game_mode + '.cpp',
      command: [py, files('tools/tests/savegame_restore_af.py'), '--generate-native', game_mode, '@OUTPUT@'],
    )
    restore_af_test = executable(
      'openq4-restore-af-' + game_mode, restore_af_source, cpp_args: ['-UNDEBUG'], install: false,
    )
    test('openq4-restore-af-' + game_mode, restore_af_test, timeout: 30)
    vehicle_hud_source = custom_target(
      'openq4-vehicle-hud-' + game_mode + '-source',
      input: files('src' / game_mode / 'vehicle' / 'Vehicle.cpp'),
      output: 'VehicleHud-' + game_mode + '.cpp',
      command: [py, files('tools/tests/vehicle_hud.py'), '--generate-native', game_mode, '@OUTPUT@'],
    )
    vehicle_hud_test = executable(
      'openq4-vehicle-hud-' + game_mode, vehicle_hud_source, cpp_args: ['-UNDEBUG'], install: false,
    )
    test('openq4-vehicle-hud-' + game_mode, vehicle_hud_test, timeout: 30)
    restore_animation_source = custom_target(
      'openq4-restore-animation-' + game_mode + '-source',
      input: files('src' / game_mode / 'Actor.cpp', 'src' / game_mode / 'IK.cpp',
                   'src' / game_mode / 'gamesys' / 'State.cpp'),
      output: 'RestoreAnimation-' + game_mode + '.cpp',
      command: [py, files('tools/tests/savegame_restore_animation.py'), '--generate-native', game_mode, '@OUTPUT@'],
    )
    restore_animation_test = executable(
      'openq4-restore-animation-' + game_mode, restore_animation_source,
      cpp_args: ['-UNDEBUG'], install: false,
    )
    test('openq4-restore-animation-' + game_mode, restore_animation_test, timeout: 30)
    restore_physics_source = custom_target(
      'openq4-restore-physics-' + game_mode + '-source',
      input: files('src' / game_mode / 'gamesys' / 'SaveGame.cpp',
                   'src' / game_mode / 'physics' / 'Physics_Base.cpp',
                   'src' / game_mode / 'physics' / 'Physics_Static.cpp',
                   'src' / game_mode / 'physics' / 'Physics_StaticMulti.cpp'),
      output: 'RestorePhysics-' + game_mode + '.cpp',
      command: [py, files('tools/tests/savegame_restore_physics.py'), '--generate-native', game_mode, '@OUTPUT@'],
    )
    restore_physics_test = executable(
      'openq4-restore-physics-' + game_mode, restore_physics_source,
      cpp_args: ['-UNDEBUG'], install: false,
    )
    test('openq4-restore-physics-' + game_mode, restore_physics_test, timeout: 30)
    restore_resources_source = custom_target(
      'openq4-restore-resources-' + game_mode + '-source',
      input: files('src' / game_mode / 'gamesys' / 'SaveGame.cpp',
                   'src' / game_mode / 'Player.cpp', 'src' / game_mode / 'physics' / 'Clip.cpp',
                   'src' / game_mode / 'physics' / 'Physics_StaticMulti.cpp',
                   'src' / game_mode / 'physics' / 'Physics_Particle.cpp',
                   'src' / game_mode / 'vehicle' / 'VehiclePosition.cpp',
                   'src' / game_mode / 'vehicle' / 'VehicleParts.cpp'),
      output: 'RestoreResources-' + game_mode + '.cpp',
      command: [py, files('tools/tests/savegame_restore_resources.py'), '--generate-native', game_mode, '@OUTPUT@'],
    )
    restore_resources_test = executable(
      'openq4-restore-resources-' + game_mode, restore_resources_source,
      cpp_args: ['-UNDEBUG'], install: false,
    )
    test('openq4-restore-resources-' + game_mode, restore_resources_test, timeout: 30)
    restore_identity_source = custom_target(
      'openq4-restore-identity-' + game_mode + '-source',
      input: files('src' / game_mode / 'Entity.cpp', 'src' / game_mode / 'client' / 'ClientEntity.cpp',
                   'src' / game_mode / 'Game_local.cpp'),
      output: 'RestoreIdentity-' + game_mode + '.cpp',
      command: [py, files('tools/tests/savegame_restore_identity.py'), '--generate-native', game_mode, '@OUTPUT@'],
    )
    restore_identity_test = executable(
      'openq4-restore-identity-' + game_mode, restore_identity_source,
      cpp_args: ['-UNDEBUG'], install: false,
    )
    test('openq4-restore-identity-' + game_mode, restore_identity_test, timeout: 30)
    client_registration_source = custom_target(
      'openq4-client-registration-' + game_mode + '-source',
      input: files('src' / game_mode / 'Game_local.cpp'),
      output: 'ClientRegistration-' + game_mode + '.cpp',
      command: [py, files('tools/tests/client_entity_registration.py'), '--generate-native', game_mode, '@OUTPUT@'],
    )
    client_registration_test = executable(
      'openq4-client-registration-' + game_mode, client_registration_source,
      cpp_args: ['-UNDEBUG'], install: false,
    )
    test('openq4-client-registration-' + game_mode, client_registration_test, timeout: 30)
    restore_cleanup_source = custom_target(
      'openq4-restore-cleanup-' + game_mode + '-source',
      input: files('src' / game_mode / 'gamesys' / 'SaveGame.cpp', 'src' / game_mode / 'gamesys' / 'Class.cpp',
                   'src' / game_mode / 'Game_local.cpp'),
      output: 'RestoreCleanup-' + game_mode + '.cpp',
      command: [py, files('tools/tests/savegame_restore_cleanup.py'), '--generate-native', game_mode, '@OUTPUT@'],
    )
    restore_cleanup_test = executable(
      'openq4-restore-cleanup-' + game_mode, restore_cleanup_source,
      cpp_args: ['-UNDEBUG'], install: false,
    )
    foreach scenario : ['independent', 'cascade', 'external', 'commit', 'rollback', 'exception']
      test('openq4-restore-cleanup-' + game_mode + '-' + scenario, restore_cleanup_test, args: [scenario], timeout: 30)
    endforeach
    restore_bindings_source = custom_target(
      'openq4-restore-bindings-' + game_mode + '-source',
      input: files('src' / game_mode / 'Entity.cpp', 'src' / game_mode / 'gamesys' / 'SaveGame.cpp',
                   'src' / game_mode / 'Game_local.cpp'),
      output: 'RestoreBindings-' + game_mode + '.cpp',
      command: [py, files('tools/tests/savegame_restore_bindings.py'), '--generate-native', game_mode, '@OUTPUT@'],
    )
    restore_bindings_test = executable(
      'openq4-restore-bindings-' + game_mode, restore_bindings_source,
      cpp_args: ['-UNDEBUG'], install: false,
    )
    test('openq4-restore-bindings-' + game_mode, restore_bindings_test, timeout: 30)
    restore_movers_source = custom_target(
      'openq4-restore-movers-' + game_mode + '-source',
      input: files('src' / game_mode / 'Mover.cpp', 'src' / game_mode / 'Mover.h',
                   'src' / game_mode / 'Entity.h', 'src' / game_mode / 'gamesys' / 'SaveGame.cpp'),
      output: 'RestoreMovers-' + game_mode + '.cpp',
      command: [py, files('tools/tests/savegame_restore_movers.py'), '--generate-native', game_mode, '@OUTPUT@'],
    )
    restore_movers_test = executable(
      'openq4-restore-movers-' + game_mode, restore_movers_source,
      cpp_args: ['-UNDEBUG'], install: false,
    )
    test('openq4-restore-movers-' + game_mode, restore_movers_test, timeout: 30)
    restore_threads_source = custom_target(
      'openq4-restore-threads-' + game_mode + '-source',
      input: files('src' / game_mode / 'Misc.cpp', 'src' / game_mode / 'Game_local.cpp',
                   'src' / game_mode / 'script' / 'Script_Thread.cpp',
                   'src' / game_mode / 'script' / 'Script_Interpreter.cpp',
                   'src' / game_mode / 'gamesys' / 'SaveGame.cpp'),
      output: 'RestoreThreads-' + game_mode + '.cpp',
      command: [py, files('tools/tests/savegame_restore_threads.py'), '--generate-native', game_mode, '@OUTPUT@'],
    )
    restore_threads_test = executable(
      'openq4-restore-threads-' + game_mode, restore_threads_source,
      cpp_args: ['-UNDEBUG'], install: false,
    )
    test('openq4-restore-threads-' + game_mode, restore_threads_test, timeout: 30)
    restore_classes_source = custom_target(
      'openq4-restore-classes-' + game_mode + '-source',
      input: files('src' / game_mode / 'gamesys' / 'SaveGame.cpp', 'src' / game_mode / 'gamesys' / 'Class.h'),
      output: 'RestoreClasses-' + game_mode + '.cpp',
      command: [py, files('tools/tests/savegame_restore_classes.py'), '--generate-native', game_mode, '@OUTPUT@'],
    )
    restore_classes_test = executable(
      'openq4-restore-classes-' + game_mode, restore_classes_source,
      cpp_args: ['-UNDEBUG'], install: false,
    )
    test('openq4-restore-classes-' + game_mode, restore_classes_test, timeout: 30)
    restore_gui_source = custom_target(
      'openq4-restore-gui-' + game_mode + '-source',
      input: files('src' / game_mode / 'gamesys' / 'SaveGame.cpp'),
      output: 'RestoreGui-' + game_mode + '.cpp',
      command: [py, files('tools/tests/savegame_restore_gui.py'), '--generate-native', game_mode, '@OUTPUT@'],
    )
    restore_gui_test = executable(
      'openq4-restore-gui-' + game_mode, restore_gui_source, install: false,
    )
    test('openq4-restore-gui-' + game_mode, restore_gui_test, timeout: 30)
    restore_spawn_refs_source = custom_target(
      'openq4-restore-spawn-refs-' + game_mode + '-source',
      input: files('src' / game_mode / 'Game_local.h', 'src' / game_mode / 'client' / 'ClientEntity.h'),
      output: 'RestoreSpawnRefs-' + game_mode + '.cpp',
      command: [py, files('tools/tests/savegame_restore_spawn_refs.py'), '--generate-native', game_mode, '@OUTPUT@'],
    )
    restore_spawn_refs_test = executable(
      'openq4-restore-spawn-refs-' + game_mode, restore_spawn_refs_source,
      cpp_args: ['-UNDEBUG'], install: false,
    )
    test('openq4-restore-spawn-refs-' + game_mode, restore_spawn_refs_test, timeout: 30)
    restore_render_handles_source = custom_target(
      'openq4-restore-render-handles-' + game_mode + '-source',
      input: files('src' / game_mode / 'Entity.cpp', 'src' / game_mode / 'Actor.cpp',
                   'src' / game_mode / 'Projectile.cpp', 'src' / game_mode / 'Moveable.cpp',
                   'src' / game_mode / 'client' / 'ClientModel.cpp', 'src' / game_mode / 'client' / 'ClientMoveable.cpp',
                   'src' / game_mode / 'vehicle' / 'VehicleParts.cpp', 'src' / game_mode / 'Weapon.cpp',
                   'src' / game_mode / 'ai' / 'Monster_StroggHover.cpp'),
      output: 'RestoreRenderHandles-' + game_mode + '.cpp',
      command: [py, files('tools/tests/savegame_restore_render_handles.py'), '--generate-native', game_mode, '@OUTPUT@'],
    )
    restore_render_handles_test = executable(
      'openq4-restore-render-handles-' + game_mode, restore_render_handles_source,
      cpp_args: ['-UNDEBUG'], install: false,
    )
    foreach scenario : ['entity', 'actor', 'projectile', 'barrel', 'client-model', 'client-moveable', 'vehicle-light', 'weapon-lights', 'hover-light']
      test('openq4-restore-render-handles-' + game_mode + '-' + scenario, restore_render_handles_test, args: [scenario], timeout: 30)
    endforeach
  endforeach

  openq4_interpolation_test = executable(
    'openq4-interpolation-test', files('tools/tests/native/InterpolationTest.cpp'),
    include_directories: root_include_dir, install: false,
  )
  test('openq4-interpolation', openq4_interpolation_test, timeout: 30)

  presentation_clock_test = executable(
    'openq4-presentation-clock-test', files('tools/tests/native/PresentationClockTest.cpp'),
    include_directories: root_include_dir, install: false,
  )
  test('openq4-presentation-clock', presentation_clock_test, timeout: 30)
  presentation_look_test = executable(
    'openq4-presentation-look-test', files('tools/tests/native/PresentationLookTest.cpp'),
    include_directories: root_include_dir, install: false,
  )
  test('openq4-presentation-look', presentation_look_test, timeout: 30)
  presentation_pacing_test = executable(
    'openq4-presentation-pacing-test', files('tools/tests/native/PresentationPacingTest.cpp'),
    include_directories: root_include_dir, install: false,
  )
  test('openq4-presentation-pacing', presentation_pacing_test, timeout: 30)
  test('openq4-presentation-hf-contract', py,
    args: [files('tools/tests/presentation_hf_contract.py'), '--mutations'], timeout: 90)
  test('openq4-clock-ownership', py,
    args: [files('tools/tests/presentation_clock_ownership.py')], timeout: 90)
  test('openq4-render-thread-contract', py,
    args: [files('tools/tests/renderer_render_thread_contract.py')], timeout: 30)
  test('openq4-render-thread-runtime', py,
    args: [files('tools/tests/renderer_render_thread_runtime.py')], timeout: 90)
  test('openq4-render-input-latency', py,
    args: [files('tools/tests/renderer_input_latency.py')], timeout: 90)
  test('openq4-render-special-effect-snapshot', py,
    args: [files('tools/tests/renderer_special_effect_snapshot.py')], timeout: 90)
  test('openq4-render-frame-ownership', py,
    args: [files('tools/tests/renderer_frame_ownership.py')], timeout: 90)
  test('openq4-render-snapshot-copy', py,
    args: [files('tools/tests/renderer_snapshot_copy.py')], timeout: 90)
  test('openq4-render-overlap-benchmark', py,
    args: [files('tools/tests/renderer_overlap_benchmark_test.py')], timeout: 30)
  test('openq4-render-frame-identity', py,
    args: [files('tools/tests/renderer_frame_identity.py')], timeout: 90)
  test('openq4-presentation-reference-suite', py,
    args: [files('tools/tests/presentation_reference_suite_test.py')], timeout: 30)

  test('openq4-imported-game-regressions', py,
    args: [files('tools/tests/game_regression_suite.py'), '--log-dir', meson.project_build_root() / 'game-regression-logs'],
    timeout: 600, is_parallel: false)
  test('openq4-mvd-seek-weapon', py,
    args: [files('tools/tests/mvd_seek_weapon_state.py')], timeout: 60)
  test('openq4-aas-liquid-area', py,
    args: [files('tools/tests/aas_liquid_area_state.py')], timeout: 60)
  test('openq4-player-air-readout', py,
    args: [files('tools/tests/player_air_readout_state.py')], timeout: 60)
  bot_frame_profile_test = executable(
    'openq4-bot-frame-profile-test', files('tools/tests/native/BotFrameProfileTest.cpp'),
    include_directories: root_include_dir, install: false,
  )
  test('openq4-bot-frame-profile', bot_frame_profile_test, timeout: 30)
  mvd_storage_budget_test = executable(
    'openq4-mvd-storage-budget-test', files('tools/tests/native/MvdStorageBudgetTest.cpp'),
    include_directories: root_include_dir, install: false,
  )
  test('openq4-mvd-storage-budget', py,
    args: [files('tools/tests/mvd_storage_budget.py'), mvd_storage_budget_test.full_path()], timeout: 60)
  game_probe_cpp = meson.get_compiler('cpp')
  foreach probe : ['string_allocation', 'binary_token_signed_char']
    probe_args = []
    if probe == 'binary_token_signed_char'
      probe_args = game_probe_cpp.get_id() == 'msvc' ? ['/J', '/DOPENQ4_EXPECT_UNSIGNED_PLAIN_CHAR=1'] : ['-funsigned-char', '-DOPENQ4_EXPECT_UNSIGNED_PLAIN_CHAR=1']
    endif
    probe_exe = executable('openq4-' + probe + '-probe', files('tools/tests/' + probe + '_probe.cpp'),
      cpp_args: probe_args, install: false)
    test('openq4-' + probe, probe_exe, timeout: 30)
  endforeach
  if host_machine.system() == 'linux'
    foreach probe : ['linux_portability', 'lp64_semantics', 'md4_checksum', 'md5_checksum', 'crc_checksum']
      probe_exe = executable('openq4-' + probe + '-probe', files('tools/tests/' + probe + '_probe.cpp'),
        cpp_args: game_probe_cpp.get_supported_arguments(['-Wno-unknown-pragmas', '-Wno-register']), install: false)
      test('openq4-' + probe, probe_exe, timeout: 30)
    endforeach
  endif

  openq4_bitmsg_under_test = custom_target(
    'openq4-bitmsg-under-test',
    input: files('src/idlib/BitMsg.h', 'src/idlib/BitMsg.cpp'),
    output: 'BitMsgUnderTest.inc',
    command: [py, files('tools/tests/generate_bitmsg_test.py'), '@INPUT0@', '@INPUT1@', '@OUTPUT@'],
    depend_files: files('tools/tests/async_drop_client_contract.py'),
  )
  openq4_bitmsg_test = executable(
    'openq4-bitmsg-test',
    [files('tools/tests/native/BitMsgTest.cpp'), openq4_bitmsg_under_test],
    include_directories: root_include_dir, install: false,
  )
  test('openq4-bitmsg', openq4_bitmsg_test, timeout: 30)

  openq4_compressor_under_test = custom_target(
    'openq4-compressor-under-test',
    input: files('src/framework/Compressor.cpp'),
    output: 'CompressorUnderTest.inc',
    command: [py, files('tools/tests/generate_compressor_test.py'), '@INPUT@', '@OUTPUT@'],
  )
  openq4_compressor_test = executable(
    'openq4-compressor-test',
    [files('tools/tests/native/CompressorTest.cpp'), openq4_compressor_under_test],
    include_directories: root_include_dir, install: false,
  )
  test('openq4-compressor', openq4_compressor_test, timeout: 30)

  foreach game_source : ['game', 'mpgame']
    tactical_include = 'AasTactical-' + game_source + 'UnderTest.inc'
    tactical_under_test = custom_target(
      'openq4-aas-tactical-' + game_source + '-under-test',
      input: files('src' / game_source / 'ai/AAS_tactical.cpp'),
      output: tactical_include,
      command: [py, files('tools/tests/generate_aas_tactical_test.py'), '@INPUT@', '@OUTPUT@'],
      depend_files: files('tools/tests/async_drop_client_contract.py'),
    )
    tactical_test = executable(
      'openq4-aas-tactical-' + game_source + '-test',
      [files('tools/tests/native/AasTacticalTest.cpp'), tactical_under_test],
      include_directories: root_include_dir,
      cpp_args: ['-DAAS_TACTICAL_TEST_INCLUDE="' + tactical_include + '"'],
      install: false,
    )
    test('openq4-aas-tactical-' + game_source, tactical_test, timeout: 30)
  endforeach

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

  openq4_connect_roster_test = executable(
    'openq4-connect-roster-test',
    files(
      'tools/tests/native/ConnectRosterTest.cpp',
      'src/framework/async/ConnectRoster.cpp',
    ),
    include_directories: root_include_dir,
    install: false,
  )
  test(
    'openq4-connect-roster',
    openq4_connect_roster_test,
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

  openq4_hdr_exposure_core_test = executable(
    'openq4-hdr-exposure-core-test',
    files('tools/tests/native/HDRExposureCoreTest.cpp'),
    include_directories: root_include_dir,
    install: false,
  )
  test('openq4-hdr-exposure-core', openq4_hdr_exposure_core_test, timeout: 30)

  openq4_vr_math_core_test = executable(
    'openq4-vr-math-core-test',
    files('tools/tests/native/VRMathCoreTest.cpp'),
    include_directories: root_include_dir,
    install: false,
  )
  test('openq4-vr-math-core', openq4_vr_math_core_test, timeout: 30)

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

  openq4_motion_blur_core_test = executable(
    'openq4-motion-blur-core-test',
    files('tools/tests/native/MotionBlurCoreTest.cpp'),
    include_directories: root_include_dir,
    install: false,
  )
  test(
    'openq4-motion-blur-core',
    openq4_motion_blur_core_test,
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

  openq4_pbr_math_test = executable(
    'openq4-pbr-math-test',
    files('tools/tests/native/PBRMathTest.cpp'),
    include_directories: root_include_dir,
    # PrefilterTiles and AsyncPrefilter run on std::thread.
    dependencies: dependency('threads'),
    install: false,
  )
  test('openq4-pbr-math', openq4_pbr_math_test, timeout: 30)

  openq4_sound_reverb_core_test = executable(
    'openq4-sound-reverb-core-test',
    files('tools/tests/native/SoundReverbCoreTest.cpp'),
    include_directories: root_include_dir,
    install: false,
  )
  test('openq4-sound-reverb-core', openq4_sound_reverb_core_test, timeout: 30)

  openq4_sound_dialogue_test = executable(
    'openq4-sound-dialogue-test',
    files('tools/tests/native/SoundDialogueTest.cpp'),
    include_directories: root_include_dir,
    install: false,
  )
  test('openq4-sound-dialogue', openq4_sound_dialogue_test, timeout: 30)

  openq4_sound_output_recovery_test = executable(
    'openq4-sound-output-recovery-test',
    files('tools/tests/native/SoundOutputRecoveryTest.cpp'),
    include_directories: root_include_dir,
    install: false,
  )
  test('openq4-sound-output-recovery', openq4_sound_output_recovery_test, timeout: 30)

  openq4_sound_memory_trim_test = executable(
    'openq4-sound-memory-trim-test',
    files('tools/tests/native/SoundMemoryTrimTest.cpp'),
    include_directories: root_include_dir,
    install: false,
  )
  test('openq4-sound-memory-trim', openq4_sound_memory_trim_test, timeout: 30)

  openq4_voice_packet_test = executable(
    'openq4-voice-packet-test',
    files('tools/tests/native/VoicePacketTest.cpp'),
    include_directories: root_include_dir,
    install: false,
  )
  test('openq4-voice-packet', openq4_voice_packet_test, timeout: 30)

  openq4_display_mode_rule_test = executable(
    'openq4-display-mode-rule-test',
    files('tools/tests/native/DisplayModeRuleTest.cpp'),
    include_directories: root_include_dir,
    install: false,
  )
  test('openq4-display-mode-rule', openq4_display_mode_rule_test, timeout: 30)

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

  openq4_console_completion_test = executable(
    'openq4-console-completion-test',
    files('tools/tests/native/ConsoleCompletionTest.cpp'),
    include_directories: root_include_dir,
    install: false,
  )
  test(
    'openq4-console-completion',
    openq4_console_completion_test,
    timeout: 30,
  )

  openq4_console_input_test = executable(
    'openq4-console-input-test',
    files('tools/tests/native/ConsoleInputTest.cpp'),
    include_directories: root_include_dir,
    install: false,
  )
  test(
    'openq4-console-input',
    openq4_console_input_test,
    timeout: 30,
  )

  openq4_arb_color_zero_floor_test = executable(
    'openq4-arb-color-zero-floor-test',
    files('tools/tests/native/ARBColorZeroFloorTest.cpp'),
    include_directories: root_include_dir,
    install: false,
  )
  test(
    'openq4-arb-color-zero-floor',
    openq4_arb_color_zero_floor_test,
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
    files('src/ui/retained/Runtime.cpp', 'src/ui/retained/Document.cpp', 'src/ui/retained/DocumentEdit.cpp', 'src/ui/retained/Presentation.cpp', 'src/ui/retained/Behavior.cpp', 'src/ui/retained/State.cpp', 'src/ui/retained/Motion.cpp', 'src/ui/retained/Interaction.cpp', 'src/ui/retained/ValueControlView.cpp', 'src/ui/retained/NumberControlView.cpp', 'src/ui/retained/Input.cpp', 'src/ui/retained/TextInput.cpp', 'src/ui/retained/TextEdit.cpp', 'src/ui/retained/TextEditCommand.cpp', 'src/ui/retained/TextInputBroker.cpp', 'src/ui/retained/TextRun.cpp', 'src/ui/retained/Vector.cpp', 'src/ui/retained/VectorElement.cpp', 'src/ui/retained/HudElement.cpp', 'src/ui/application/SettingsTransaction.cpp', 'src/ui/application/SettingsJournal.cpp', 'src/ui/application/SettingsEffectPlan.cpp', 'src/ui/application/SettingsDisplayController.cpp'),
    files('src/ui/retained/TextInputBrokerWire.cpp'),
    files('src/ui/retained/ModelScene.cpp'),
    files('src/ui/retained/PlayerSetupRequest.cpp'),
    files('src/ui/retained/NativeTextDocument.cpp'),
    files('src/ui/retained/NativeTextEditor.cpp'),
    files('src/ui/retained/ScrollGeometry.cpp'),
    files('src/ui/retained/ScrollbarView.cpp'),
    files('src/ui/retained/PopupPlacement.cpp'),
    dependencies: [rmlui_project.get_variable('rmlui_dep'), jsoncpp_project.get_variable('jsoncpp_dep'), libtess2_project.get_variable('libtess2_dep')],
    pic: true,
    install: false,
  )
  openq4_retained_dep = declare_dependency(link_with: openq4_retained_library)
  # Actual Windows SDK interfaces stay outside the portable runtime and engine
  # PCH. This object has no native activation, SDL or dedicated-server dependency.
  if host_system == 'windows'
    openq4_windows_text_store_library = static_library('openq4-windows-text-store',
      files('src/sys/sdl3/WindowsTextStore.cpp'),
      dependencies: openq4_retained_dep, install: false)
    openq4_windows_text_store_dep = declare_dependency(link_with: openq4_windows_text_store_library)
    if get_option('build_native_tests')
      openq4_windows_text_store_test = executable('openq4-ui-windows-text-store-test',
        files('tools/tests/native/WindowsTextStoreTest.cpp'),
        include_directories: include_directories('src'),
        dependencies: [openq4_windows_text_store_dep, openq4_retained_dep], install: false)
      test('openq4-ui-windows-text-store', openq4_windows_text_store_test, timeout: 30)
    endif
  endif
  if get_option('build_native_tests')
    openq4_ui_scroll_geometry_test = executable('openq4-ui-scroll-geometry-test',
      files('tools/tests/native/UiScrollGeometryTest.cpp'),
      dependencies: openq4_retained_dep, install: false)
    test('openq4-ui-scroll-geometry', openq4_ui_scroll_geometry_test, timeout: 30)
    openq4_ui_scrollbar_interaction_test = executable('openq4-ui-scrollbar-interaction-test',
      files('tools/tests/native/UiScrollbarInteractionTest.cpp'),
      include_directories: [root_include_dir, include_directories('src/ui/retained')],
      dependencies: [openq4_retained_dep, jsoncpp_project.get_variable('jsoncpp_dep')], install: false)
    test('openq4-ui-scrollbar-interaction', openq4_ui_scrollbar_interaction_test, timeout: 30)
    openq4_ui_scrollbar_runtime_test = executable('openq4-ui-scrollbar-runtime-test',
      files('tools/tests/native/UiScrollbarRuntimeTest.cpp'),
      include_directories: root_include_dir,
      dependencies: [openq4_retained_dep, rmlui_project.get_variable('rmlui_dep'), jsoncpp_project.get_variable('jsoncpp_dep')], install: false)
    test('openq4-ui-scrollbar-runtime', openq4_ui_scrollbar_runtime_test,
      args: [meson.project_source_root() / 'content/baseoq4/pak0/guis/menu/settings/system.q4ui'], timeout: 30)
    openq4_ui_choice_scroll_interaction_test = executable('openq4-ui-choice-scroll-interaction-test',
      files('tools/tests/native/UiChoiceScrollInteractionTest.cpp'),
      include_directories: [root_include_dir, include_directories('src/ui/retained')],
      dependencies: [openq4_retained_dep, jsoncpp_project.get_variable('jsoncpp_dep')], install: false)
    test('openq4-ui-choice-scroll-interaction', openq4_ui_choice_scroll_interaction_test, timeout: 30)
    openq4_ui_choice_scroll_runtime_test = executable('openq4-ui-choice-scroll-runtime-test',
      files('tools/tests/native/UiChoiceScrollRuntimeTest.cpp'),
      include_directories: root_include_dir,
      dependencies: [openq4_retained_dep, rmlui_project.get_variable('rmlui_dep'), jsoncpp_project.get_variable('jsoncpp_dep')], install: false)
    test('openq4-ui-choice-scroll-runtime', openq4_ui_choice_scroll_runtime_test,
      args: [meson.project_source_root() / 'content/baseoq4/pak0/guis/menu/settings/system.q4ui'], timeout: 180)
    openq4_ui_text_input_test = executable(
      'openq4-ui-text-input-test',
      files('tools/tests/native/UiTextInputTest.cpp'),
      include_directories: root_include_dir,
      dependencies: openq4_retained_dep,
      install: false,
    )
    test('openq4-ui-text-input', openq4_ui_text_input_test, timeout: 30)
    openq4_ui_text_edit_test = executable(
      'openq4-ui-text-edit-test',
      files('tools/tests/native/UiTextEditTest.cpp'),
      include_directories: root_include_dir,
      dependencies: openq4_retained_dep,
      install: false,
    )
    test('openq4-ui-text-edit', openq4_ui_text_edit_test, timeout: 30)
    openq4_ui_text_edit_command_test = executable(
      'openq4-ui-text-edit-command-test',
      files('tools/tests/native/UiTextEditCommandTest.cpp'),
      include_directories: root_include_dir,
      dependencies: openq4_retained_dep,
      install: false,
    )
    test('openq4-ui-text-edit-command', openq4_ui_text_edit_command_test, timeout: 30)
    openq4_ui_text_input_broker_test = executable(
      'openq4-ui-text-input-broker-test',
      files('tools/tests/native/UiTextInputBrokerTest.cpp'),
      include_directories: root_include_dir,
      dependencies: openq4_retained_dep,
      install: false,
    )
    test('openq4-ui-text-input-broker', openq4_ui_text_input_broker_test, timeout: 30)
    openq4_ui_text_input_broker_wire_test = executable(
      'openq4-ui-text-input-broker-wire-test',
      files('tools/tests/native/UiTextInputBrokerWireTest.cpp'),
      include_directories: root_include_dir,
      dependencies: openq4_retained_dep,
      install: false,
    )
    test('openq4-ui-text-input-broker-wire', openq4_ui_text_input_broker_wire_test, timeout: 30)
    openq4_ui_native_text_document_test = executable(
      'openq4-ui-native-text-document-test',
      files('tools/tests/native/UiNativeTextDocumentTest.cpp'),
      include_directories: include_directories('src'),
      dependencies: openq4_retained_dep,
      install: false,
    )
    test('openq4-ui-native-text-document', openq4_ui_native_text_document_test, timeout: 30)
    openq4_ui_native_text_editor_test = executable(
      'openq4-ui-native-text-editor-test',
      files('tools/tests/native/UiNativeTextEditorTest.cpp'),
      include_directories: include_directories('src'),
      dependencies: openq4_retained_dep,
      install: false,
    )
    test('openq4-ui-native-text-editor', openq4_ui_native_text_editor_test, timeout: 30)
    openq4_ui_native_text_collection_test = executable(
      'openq4-ui-native-text-collection-test',
      files('tools/tests/native/UiNativeTextCollectionTest.cpp'),
      include_directories: include_directories('src'),
      dependencies: openq4_retained_dep,
      install: false,
    )
    test('openq4-ui-native-text-collection', openq4_ui_native_text_collection_test, timeout: 30)
    openq4_ui_number_draft_test = executable(
      'openq4-ui-number-draft-test',
      files('tools/tests/native/UiNumberDraftTest.cpp'),
      include_directories: root_include_dir,
      dependencies: openq4_retained_dep,
      install: false,
    )
    test('openq4-ui-number-draft', openq4_ui_number_draft_test, timeout: 30)
    openq4_ui_number_native_test = executable(
      'openq4-ui-number-native-test',
      files('tools/tests/native/UiNumberNativeTest.cpp'),
      include_directories: root_include_dir,
      dependencies: openq4_retained_dep,
      install: false,
    )
    test('openq4-ui-number-native', openq4_ui_number_native_test, timeout: 30)
    openq4_ui_slider_decimal_test = executable(
      'openq4-ui-slider-decimal-test',
      files('tools/tests/native/UiSliderDecimalTest.cpp'),
      include_directories: root_include_dir,
      dependencies: openq4_retained_dep,
      install: false,
    )
    test('openq4-ui-slider-decimal', openq4_ui_slider_decimal_test, timeout: 30)
    openq4_ui_float_chars_test = executable(
      'openq4-ui-float-chars-test',
      files('tools/tests/native/UiFloatCharsTest.cpp'),
      include_directories: root_include_dir,
      install: false,
    )
    test('openq4-ui-float-chars', openq4_ui_float_chars_test, args: ['500'], timeout: 60)
    openq4_ui_json_number_test = executable(
      'openq4-ui-json-number-test',
      files('tools/tests/native/UiJsonNumberTest.cpp'),
      dependencies: jsoncpp_project.get_variable('jsoncpp_dep'),
      install: false,
    )
    test('openq4-ui-json-number', openq4_ui_json_number_test, timeout: 30)
    openq4_ui_number_native_view_test = executable(
      'openq4-ui-number-native-view-test',
      files('tools/tests/native/UiNumberNativeViewTest.cpp'),
      include_directories: root_include_dir,
      dependencies: [openq4_retained_dep, rmlui_project.get_variable('rmlui_dep'), jsoncpp_project.get_variable('jsoncpp_dep')],
      install: false,
    )
    test('openq4-ui-number-native-view', openq4_ui_number_native_view_test, timeout: 30)
    openq4_ui_text_run_test = executable(
      'openq4-ui-text-run-test',
      files('tools/tests/native/UiTextRunTest.cpp'),
      include_directories: root_include_dir,
      dependencies: [openq4_retained_dep, rmlui_project.get_variable('rmlui_dep')],
      install: false,
    )
    test('openq4-ui-text-run', openq4_ui_text_run_test, timeout: 30)
    openq4_ui_number_control_test = executable(
      'openq4-ui-number-control-test',
      files('tools/tests/native/UiNumberControlTest.cpp'),
      include_directories: root_include_dir,
      dependencies: [openq4_retained_dep, jsoncpp_project.get_variable('jsoncpp_dep')],
      install: false,
    )
    test('openq4-ui-number-control', openq4_ui_number_control_test, timeout: 30)
    openq4_ui_number_snapshot_test = executable(
      'openq4-ui-number-snapshot-test',
      files('tools/tests/native/UiNumberSnapshotTest.cpp'),
      include_directories: root_include_dir,
      dependencies: [openq4_retained_dep, jsoncpp_project.get_variable('jsoncpp_dep')],
      install: false,
    )
    test('openq4-ui-number-snapshot', openq4_ui_number_snapshot_test, timeout: 30)
    openq4_ui_number_runtime_test = executable(
      'openq4-ui-number-runtime-test',
      files('tools/tests/native/UiNumberRuntimeTest.cpp'),
      include_directories: root_include_dir,
      dependencies: [openq4_retained_dep, rmlui_project.get_variable('rmlui_dep'), jsoncpp_project.get_variable('jsoncpp_dep')],
      install: false,
    )
    test('openq4-ui-number-runtime', openq4_ui_number_runtime_test,
      args: [meson.project_source_root() / 'tools/ui/fixtures/number-edit-smoke.q4ui'], timeout: 30)
    openq4_ui_text_field_test = executable(
      'openq4-ui-text-field-test',
      files('tools/tests/native/UiTextFieldTest.cpp'),
      include_directories: root_include_dir,
      dependencies: [openq4_retained_dep, rmlui_project.get_variable('rmlui_dep'), jsoncpp_project.get_variable('jsoncpp_dep')],
      install: false,
    )
    test('openq4-ui-text-field', openq4_ui_text_field_test, timeout: 30)
    openq4_ui_focus_reveal_test = executable(
      'openq4-ui-focus-reveal-test',
      files('tools/tests/native/UiFocusRevealTest.cpp'),
      include_directories: root_include_dir,
      dependencies: [openq4_retained_dep, rmlui_project.get_variable('rmlui_dep'), jsoncpp_project.get_variable('jsoncpp_dep')],
      install: false,
    )
    test('openq4-ui-focus-reveal', openq4_ui_focus_reveal_test, timeout: 30)
    openq4_ui_number_command_test = executable(
      'openq4-ui-number-command-test',
      files('tools/tests/native/UiNumberCommandTest.cpp'),
      include_directories: root_include_dir,
      dependencies: [openq4_retained_dep, rmlui_project.get_variable('rmlui_dep'), jsoncpp_project.get_variable('jsoncpp_dep')],
      install: false,
    )
    test('openq4-ui-number-command', openq4_ui_number_command_test, timeout: 30)
    openq4_ui_settings_transaction_test = executable(
      'openq4-ui-settings-transaction-test',
      files('tools/tests/native/UiSettingsTransactionTest.cpp'),
      include_directories: root_include_dir,
      dependencies: openq4_retained_dep,
      install: false,
    )
    test('openq4-ui-settings-transaction', openq4_ui_settings_transaction_test, timeout: 30)
    ui_test_cpp = meson.get_compiler('cpp')
    ui_native_owner_args = [meson.project_source_root() / 'tools/tests/ui_managed_native_owner.py',
      '--compiler', ui_test_cpp.cmd_array()[-1], '--no-mutations']
    if ui_test_cpp.get_id() == 'msvc'
      ui_native_owner_args += ['--msvc', '--debug-crt']
    endif
    test('openq4-ui-managed-native-owner', py, args: ui_native_owner_args, timeout: 180)
    foreach consumed_backend : ['gl', 'vulkan']
      consumed_args = [meson.project_source_root() / 'tools/tests/renderer_consumed_policy.py',
        '--compiler', ui_test_cpp.cmd_array()[-1]]
      if consumed_backend == 'vulkan'
        consumed_args += ['--vulkan']
      endif
      test('openq4-ui-consumed-policy-' + consumed_backend, py, args: consumed_args, timeout: 120)
    endforeach
    test('openq4-ui-consumed-upload-batches', py,
      args: [meson.project_source_root() / 'tools/tests/renderer_consumed_upload_batches.py',
        '--compiler', ui_test_cpp.cmd_array()[-1]], timeout: 120)
    test('openq4-ui-upload-storage', py,
      args: [meson.project_source_root() / 'tools/tests/renderer_upload_storage.py',
        '--compiler', ui_test_cpp.cmd_array()[-1]], timeout: 120)
    test('openq4-ui-image-reduction', py,
      args: [meson.project_source_root() / 'tools/tests/renderer_image_reduction.py',
        '--compiler', ui_test_cpp.cmd_array()[-1]], timeout: 180)
    test('openq4-ui-image-content', py,
      args: [meson.project_source_root() / 'tools/tests/renderer_image_content.py',
        '--compiler', ui_test_cpp.cmd_array()[-1]], timeout: 180)
    foreach recovery_suite : ['owner', 'module', 'loader', 'codec', 'cold', 'lifecycle']
      test('openq4-ui-image-recovery-' + recovery_suite, py,
        args: [meson.project_source_root() / ('tools/tests/renderer_image_recovery_' + recovery_suite + '.py'),
          '--compiler', ui_test_cpp.cmd_array()[-1]], timeout: 180)
    endforeach
    test('openq4-ui-native-input-route', py,
      args: [meson.project_source_root() / 'tools/tests/native_input_route.py',
        '--compiler', ui_test_cpp.cmd_array()[-1], '--no-mutations'], timeout: 120)
    retirement_args = [meson.project_source_root() / 'tools/tests/native_event_retirement.py',
      '--compiler', ui_test_cpp.cmd_array()[-1], '--no-mutations']
    if ui_test_cpp.get_id() == 'msvc'
      retirement_args += ['--msvc-debug']
    endif
    test('openq4-ui-native-event-retirement', py, args: retirement_args, timeout: 180)
    lifecycle_args = [meson.project_source_root() / 'tools/tests/native_input_lifecycle.py',
      '--compiler', ui_test_cpp.cmd_array()[-1], '--no-mutations']
    if ui_test_cpp.get_id() == 'msvc'
      lifecycle_args += ['--msvc-debug']
    endif
    test('openq4-ui-native-input-lifecycle', py, args: lifecycle_args, timeout: 180)
    driver_args = [meson.project_source_root() / 'tools/tests/native_input_driver.py',
      '--compiler', ui_test_cpp.cmd_array()[-1]]
    if ui_test_cpp.get_id() == 'msvc'
      driver_args += ['--msvc-debug']
    endif
    test('openq4-ui-native-input-driver', py, args: driver_args, timeout: 180)
    test('openq4-ui-legacy-observation', py,
      args: [meson.project_source_root() / 'tools/tests/ui_legacy_observation.py',
        '--compiler', ui_test_cpp.cmd_array()[-1]], timeout: 180)
    test('openq4-ui-sound-recovery', py,
      args: [meson.project_source_root() / 'tools/tests/sound_recovery.py',
        '--compiler', ui_test_cpp.cmd_array()[-1], '--no-mutations'], timeout: 180)
    foreach image_policy_suite : ['restart', 'metadata']
      test('openq4-ui-image-policy-' + image_policy_suite, py,
        args: [meson.project_source_root() / ('tools/tests/renderer_image_policy_' + image_policy_suite + '.py'),
          '--compiler', ui_test_cpp.cmd_array()[-1]], timeout: 180)
    endforeach
    test('openq4-ui-input-disposition', py,
      args: [meson.project_source_root() / 'tools/tests/sdl3_input_disposition.py',
        '--compiler', ui_test_cpp.cmd_array()[-1], '--no-mutations'], timeout: 120)
    test('openq4-ui-settings-effect-journal', py,
      args: [meson.project_source_root() / 'tools/tests/ui_settings_effect_journal.py',
        '--compiler', ui_test_cpp.cmd_array()[-1], '--no-mutations'], timeout: 180)
    openq4_ui_performance_presets_test = executable(
      'openq4-ui-performance-presets-test',
      files('tools/tests/native/UiPerformancePresetTest.cpp', 'src/framework/PerformancePreset.cpp'),
      include_directories: root_include_dir,
      dependencies: openq4_retained_dep,
      install: false,
    )
    test('openq4-ui-performance-presets', openq4_ui_performance_presets_test, timeout: 30)
    openq4_ui_system_presets_test = executable(
      'openq4-ui-system-presets-test',
      files('tools/tests/native/UiSystemPresetRuntimeTest.cpp', 'src/framework/PerformancePreset.cpp'),
      include_directories: root_include_dir,
      dependencies: [openq4_retained_dep, rmlui_project.get_variable('rmlui_dep'), jsoncpp_project.get_variable('jsoncpp_dep')],
      install: false,
    )
    test('openq4-ui-system-presets', openq4_ui_system_presets_test,
      args: [meson.project_source_root() / 'content/baseoq4/pak0/guis/menu/settings/system.q4ui',
        meson.project_source_root() / 'content/baseoq4/pak0/strings'], timeout: 900)
    test('openq4-ui-system-presets-source', py,
      args: [meson.project_source_root() / 'tools/tests/ui_system_presets.py'], timeout: 30)
    test('openq4-ui-rendering-settings', py,
      args: [meson.project_source_root() / 'tools/tests/ui_rendering_settings.py'], timeout: 120)
    test('openq4-renderer-selection-startup', py,
      args: [meson.project_source_root() / 'tools/tests/renderer_selection_startup.py'], timeout: 120)
    openq4_ui_settings_exact_test = executable(
      'openq4-ui-settings-exact-test', files('tools/tests/native/UiSettingsExactValueTest.cpp'),
      include_directories: root_include_dir, dependencies: openq4_retained_dep, install: false,
    )
    test('openq4-ui-settings-exact', openq4_ui_settings_exact_test, timeout: 30)
    openq4_ui_settings_journal_test = executable(
      'openq4-ui-settings-journal-test',
      files('tools/tests/native/UiSettingsJournalTest.cpp'),
      include_directories: root_include_dir,
      dependencies: openq4_retained_dep,
      install: false,
    )
    test('openq4-ui-settings-journal', openq4_ui_settings_journal_test, timeout: 30)
    openq4_ui_settings_journal_exact_test = executable(
      'openq4-ui-settings-journal-exact-test', files('tools/tests/native/UiSettingsJournalExactTest.cpp'),
      include_directories: root_include_dir, dependencies: openq4_retained_dep, install: false,
    )
    test('openq4-ui-settings-journal-exact', openq4_ui_settings_journal_exact_test, timeout: 30)
    openq4_ui_settings_display_test = executable(
      'openq4-ui-settings-display-test',
      files('tools/tests/native/UiSettingsDisplayControllerTest.cpp'),
      dependencies: openq4_retained_dep,
      include_directories: root_include_dir,
      install: false,
    )
    test('openq4-ui-settings-display', openq4_ui_settings_display_test, timeout: 30)
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
    openq4_ui_generated_texture_test = executable(
      'openq4-ui-generated-texture-test',
      files('tools/tests/native/UiGeneratedTextureTest.cpp'),
      include_directories: root_include_dir,
      dependencies: [openq4_retained_dep, rmlui_project.get_variable('rmlui_dep')],
      install: false,
    )
    test('openq4-ui-generated-texture', openq4_ui_generated_texture_test, timeout: 30)
    openq4_ui_generated_adapter_source = custom_target(
      'openq4-ui-generated-adapter-source',
      input: files('src/ui/RetainedUI.cpp', 'src/renderer/RenderSystem.cpp'),
      output: 'UiGeneratedAdapterTest.cpp',
      command: [py, files('tools/tests/ui_generated_images.py'), '--generate-native', '@OUTPUT@'],
    )
    openq4_ui_generated_adapter_test = executable(
      'openq4-ui-generated-adapter-test', openq4_ui_generated_adapter_source,
      include_directories: root_include_dir, cpp_args: ['-UNDEBUG'], install: false,
    )
    test('openq4-ui-generated-adapter', openq4_ui_generated_adapter_test, timeout: 30)
    # Image nodes, path blends and view-height canvases, then the production
    # retained title, pause and loading screens (ui_retained).
    openq4_ui_retained_screens_test = executable(
      'openq4-ui-retained-screens-test',
      files('tools/tests/native/UiRetainedScreensTest.cpp'),
      include_directories: root_include_dir,
      dependencies: openq4_retained_dep,
      install: false,
    )
    # One test per group of documents (the groups in the test's main), so the
    # suite runs them side by side.
    retained_screens_documents = [meson.project_source_root() / 'content/baseoq4/pak0/guis/menu/title.q4ui',
      meson.project_source_root() / 'content/baseoq4/pak0/guis/menu/pause.q4ui',
      meson.project_source_root() / 'content/baseoq4/pak0/guis/loading/loading.q4ui',
      meson.project_source_root() / 'content/baseoq4/pak0/guis/menu/pause_strogg.q4ui',
      meson.project_source_root() / 'content/baseoq4/pak0/guis/menu/singleplayer.q4ui',
      meson.project_source_root() / 'content/baseoq4/pak0/guis/menu/campaigns.q4ui',
      meson.project_source_root() / 'content/baseoq4/pak0/guis/menu/mp_escape.q4ui',
      meson.project_source_root() / 'content/baseoq4/pak0/guis/menu/mp_welcome.q4ui',
      meson.project_source_root() / 'content/baseoq4/pak0/guis/weapons/machinegun_scope.q4ui',
      meson.project_source_root() / 'content/baseoq4/pak0/guis/weapons/railgun_scope.q4ui',
      meson.project_source_root() / 'content/baseoq4/pak0/guis/weapons/nailgun_scope.q4ui',
      meson.project_source_root() / 'content/baseoq4/pak0/guis/loading/initialization.q4ui',
      meson.project_source_root() / 'content/baseoq4/pak0/guis/menu/campaign_skill.q4ui',
      meson.project_source_root() / 'content/baseoq4/pak0/guis/hud/view_effects.q4ui',
      meson.project_source_root() / 'content/baseoq4/pak0/guis/hud/weapon_wheel_marine.q4ui',
      meson.project_source_root() / 'content/baseoq4/pak0/guis/hud/weapon_wheel_marine_awakening.q4ui',
      meson.project_source_root() / 'content/baseoq4/pak0/guis/hud/weapon_wheel_strogg.q4ui',
      meson.project_source_root() / 'content/baseoq4/pak0/guis/hud/weapon_wheel_strogg_awakening.q4ui',
      meson.project_source_root() / 'content/baseoq4/pak0/guis/menu/loadgame.q4ui',
      meson.project_source_root() / 'content/baseoq4/pak0/guis/menu/savegame.q4ui',
      meson.project_source_root() / 'content/baseoq4/pak0/guis/menu/restartlevel.q4ui',
      meson.project_source_root() / 'content/baseoq4/pak0/guis/menu/demos.q4ui',
      meson.project_source_root() / 'content/baseoq4/pak0/guis/menu/restartmenu.q4ui',
      meson.project_source_root() / 'content/baseoq4/pak0/guis/menu/credits.q4ui',
      meson.project_source_root() / 'content/baseoq4/pak0/guis/menu/arena.q4ui',
      meson.project_source_root() / 'content/baseoq4/pak0/guis/menu/multiplayer.q4ui',
      meson.project_source_root() / 'content/baseoq4/pak0/guis/menu/createserver.q4ui',
      meson.project_source_root() / 'content/baseoq4/pak0/guis/menu/joingame.q4ui']
    # The two full page matrices cost 43-57 s on the fastest hosted runner and
    # 100-175 s on the slowest (macOS Intel, Windows) and under ASan+UBSan, at
    # an unchanged case count, so their bound is a hang guard for every host.
    retained_screens_groups = {'menus': 120, 'pause': 120, 'strogg-pause': 120, 'loading-hud': 120, 'pages': 300, 'strogg-pages': 120, 'mp-pages': 300, 'mp-card': 120,
      'mp-vote-match': 120, 'mp-admin': 120, 'mp-settings-welcome': 120, 'pointer-reach': 120, 'optics': 240}
    foreach group, timeout : retained_screens_groups
      test('openq4-ui-retained-screens-' + group, openq4_ui_retained_screens_test,
        args: retained_screens_documents + [group], timeout: timeout)
    endforeach
    # The menus inside the window's safe area (ui-visual-design.md 13.3), in
    # two groups that run side by side.
    test('openq4-ui-retained-safe-area-menus', openq4_ui_retained_screens_test,
      args: ['--safe-area', retained_screens_documents[0], retained_screens_documents[1], retained_screens_documents[2],
        retained_screens_documents[4], retained_screens_documents[12], retained_screens_documents[7],
        meson.project_source_root() / 'content/baseoq4/pak0/guis/menu/settings.q4ui'], timeout: 120)
    test('openq4-ui-retained-safe-area-pages', openq4_ui_retained_screens_test,
      args: ['--safe-area', retained_screens_documents[18], retained_screens_documents[21], retained_screens_documents[23],
        retained_screens_documents[24], retained_screens_documents[27],
        meson.project_source_root() / 'content/baseoq4/pak0/guis/menu/settings/game.q4ui'], timeout: 120)
    test('openq4-ui-view-effects', openq4_ui_retained_screens_test,
      args: ['--view-effects', meson.project_source_root() / 'content/baseoq4/pak0/guis/hud/view_effects.q4ui'], timeout: 60)
    test('openq4-ui-subtitles', openq4_ui_retained_screens_test,
      args: ['--subtitles', meson.project_source_root() / 'content/baseoq4/pak0/guis/hud/subtitles.q4ui'], timeout: 60)
    # The retained SETTINGS page (UI-01).
    test('openq4-ui-retained-settings', openq4_ui_retained_screens_test,
      args: ['--settings', meson.project_source_root() / 'content/baseoq4/pak0/guis/menu/settings.q4ui'], timeout: 120)
    # The retained CONTROLS settings page (UI-01).
    test('openq4-ui-retained-controls', openq4_ui_retained_screens_test,
      args: ['--controls', meson.project_source_root() / 'content/baseoq4/pak0/guis/menu/settings/controls.q4ui'], timeout: 120)
    # The retained GAME OPTIONS settings page (UI-01).
    test('openq4-ui-retained-game', openq4_ui_retained_screens_test,
      args: ['--game', meson.project_source_root() / 'content/baseoq4/pak0/guis/menu/settings/game.q4ui'], timeout: 120)
    # The retained AUDIO settings page (UI-01).
    test('openq4-ui-retained-audio', openq4_ui_retained_screens_test,
      args: ['--audio', meson.project_source_root() / 'content/baseoq4/pak0/guis/menu/settings/audio.q4ui'], timeout: 120)
    # The match summary card (ui-visual-design.md 14.14).
    test('openq4-ui-retained-summary', openq4_ui_retained_screens_test,
      args: ['--summary', meson.project_source_root() / 'content/baseoq4/pak0/guis/menu/mp_summary.q4ui'], timeout: 240)
    test('openq4-ui-retained-rosters', openq4_ui_retained_screens_test,
      args: ['--rosters', retained_screens_documents[6], retained_screens_documents[7],
        meson.project_source_root() / 'content/baseoq4/pak0/guis/menu/mp_summary.q4ui'], timeout: 120)
    # The multiplayer quick-chat wheel (ui-visual-design.md 14.6).
    test('openq4-ui-quick-chat', openq4_ui_retained_screens_test,
      args: ['--quick-chat', meson.project_source_root() / 'content/baseoq4/pak0/guis/hud/quick_chat.q4ui'], timeout: 120)
    test('openq4-ui-status-bar', openq4_ui_retained_screens_test,
      args: [meson.project_source_root() / 'content/baseoq4/pak0/guis/hud/status_bar_marine.q4ui',
             meson.project_source_root() / 'content/baseoq4/pak0/guis/hud/status_bar_strogg.q4ui'], timeout: 240)
    test('openq4-ui-hud', openq4_ui_retained_screens_test,
      args: [meson.project_source_root() / 'content/baseoq4/pak0/guis/hud/artwork.q4ui'], timeout: 120)
    # The Remastered HUD documents (ui-visual-design.md 14.4-14.12) on their anchors, answering the stock events.
    test('openq4-ui-remastered-hud', openq4_ui_retained_screens_test,
      args: ['--remastered',
             meson.project_source_root() / 'content/baseoq4/pak0/guis/hud/remastered_marine.q4ui',
             meson.project_source_root() / 'content/baseoq4/pak0/guis/hud/remastered_strogg.q4ui',
             meson.project_source_root() / 'content/baseoq4/pak0/guis/hud/remastered_marine_awakening.q4ui',
             meson.project_source_root() / 'content/baseoq4/pak0/guis/hud/remastered_strogg_awakening.q4ui',
             meson.project_source_root() / 'content/baseoq4/pak0/guis/hud/remastered_mp.q4ui',
             meson.project_source_root() / 'content/baseoq4/pak0/guis/hud/remastered_vehicle.q4ui',
             meson.project_source_root() / 'content/baseoq4/pak0/guis/hud/remastered_airdef.q4ui',
             meson.project_source_root() / 'content/baseoq4/pak0/guis/hud/remastered_turret.q4ui',
             meson.project_source_root() / 'content/baseoq4/pak0/guis/hud/remastered_mcc.q4ui',
             meson.project_source_root() / 'content/baseoq4/pak0/guis/hud/remastered_chaingun_turret.q4ui',
             meson.project_source_root() / 'content/baseoq4/pak0/guis/hud/remastered_speeder.q4ui',
             meson.project_source_root() / 'content/baseoq4/pak0/guis/hud/remastered_gev.q4ui',
             meson.project_source_root() / 'content/baseoq4/pak0/guis/hud/remastered_medchange.q4ui',
             meson.project_source_root() / 'content/baseoq4/pak0/guis/hud/remastered_objectives.q4ui',
             meson.project_source_root() / 'content/baseoq4/pak0/guis/hud/remastered_objectives_strogg.q4ui',
             meson.project_source_root() / 'content/baseoq4/pak0/guis/hud/remastered_scoreboard.q4ui'], timeout: 240)
    test('openq4-ui-remastered-hud-generated', py,
      args: [meson.project_source_root() / 'tools/ui/build_remastered_hud.py', '--check'], timeout: 120)
    # The stock interface's HUD for The Awakening, generated from guis/hud.gui.
    test('openq4-ui-awakening-legacy-hud-generated', py,
      args: [meson.project_source_root() / 'tools/ui/build_awakening_legacy_hud.py', '--check'], timeout: 60)
    test('openq4-ui-hud-coverage', py,
      args: [meson.project_source_root() / 'tools/tests/ui_hud_coverage.py'], timeout: 30)
    openq4_ui_value_schema_test = executable(
      'openq4-ui-value-schema-test',
      files('tools/tests/native/UiValueControlSchemaTest.cpp'),
      include_directories: root_include_dir,
      dependencies: [openq4_retained_dep, jsoncpp_project.get_variable('jsoncpp_dep')],
      install: false,
    )
    test('openq4-ui-value-schema', openq4_ui_value_schema_test, timeout: 30)
    openq4_ui_value_interaction_test = executable(
      'openq4-ui-value-interaction-test',
      files('tools/tests/native/UiValueInteractionTest.cpp'),
      include_directories: root_include_dir,
      dependencies: [openq4_retained_dep, jsoncpp_project.get_variable('jsoncpp_dep')],
      install: false,
    )
    test('openq4-ui-value-interaction', openq4_ui_value_interaction_test, timeout: 30)
    openq4_ui_modal_interaction_test = executable(
      'openq4-ui-modal-interaction-test',
      files('tools/tests/native/UiModalInteractionTest.cpp'),
      include_directories: root_include_dir,
      dependencies: [openq4_retained_dep, jsoncpp_project.get_variable('jsoncpp_dep')],
      install: false,
    )
    test('openq4-ui-modal-interaction', openq4_ui_modal_interaction_test, timeout: 30)
    openq4_ui_modal_runtime_test = executable(
      'openq4-ui-modal-runtime-test',
      files('tools/tests/native/UiModalRuntimeTest.cpp'),
      include_directories: root_include_dir,
      dependencies: [openq4_retained_dep, jsoncpp_project.get_variable('jsoncpp_dep')],
      install: false,
    )
    test('openq4-ui-modal-runtime', openq4_ui_modal_runtime_test, timeout: 30)
    openq4_ui_mask_input_test = executable('openq4-ui-mask-input-test',
      files('tools/tests/native/UiMaskInputTest.cpp'),
      include_directories: root_include_dir,
      dependencies: [openq4_retained_dep, rmlui_project.get_variable('rmlui_dep'), jsoncpp_project.get_variable('jsoncpp_dep')],
      install: false)
    test('openq4-ui-mask-input', openq4_ui_mask_input_test, timeout: 30)
    openq4_ui_text_scale_test = executable('openq4-ui-text-scale-test',
      files('tools/tests/native/UiTextScaleRuntimeTest.cpp'),
      include_directories: root_include_dir,
      dependencies: [openq4_retained_dep, rmlui_project.get_variable('rmlui_dep'), jsoncpp_project.get_variable('jsoncpp_dep')],
      install: false)
    test('openq4-ui-text-scale', openq4_ui_text_scale_test, timeout: 30)
    openq4_ui_font_cache_test = executable('openq4-ui-font-cache-test',
      files('tools/tests/native/RetainedFontCacheTest.cpp', 'src/renderer/RetainedFontCache.cpp'),
      include_directories: root_include_dir, install: false)
    test('openq4-ui-font-cache', openq4_ui_font_cache_test, timeout: 60)
    openq4_ui_system_text_scale_test = executable('openq4-ui-system-text-scale-test',
      files('tools/tests/native/UiSystemTextScaleTest.cpp', 'src/framework/PerformancePreset.cpp'),
      include_directories: root_include_dir,
      dependencies: [openq4_retained_dep, rmlui_project.get_variable('rmlui_dep'), jsoncpp_project.get_variable('jsoncpp_dep')],
      install: false)
    system_page_locales = ['english', 'spanish', 'polish', 'russian', 'french', 'italian', 'german']
    foreach locale : system_page_locales
      test('openq4-ui-system-text-scale-' + locale, openq4_ui_system_text_scale_test,
        args: [files('content/baseoq4/pak0/guis/menu/settings/system.q4ui'), meson.current_source_dir() / 'content/baseoq4/pak0/strings', locale], timeout: 900)
    endforeach
    openq4_ui_system_interface_settings_test = executable('openq4-ui-system-interface-settings-test',
      files('tools/tests/native/UiSystemInterfaceSettingsTest.cpp', 'src/framework/PerformancePreset.cpp'),
      include_directories: root_include_dir,
      dependencies: [openq4_retained_dep, rmlui_project.get_variable('rmlui_dep'), jsoncpp_project.get_variable('jsoncpp_dep')],
      install: false)
    foreach locale : system_page_locales
      test('openq4-ui-system-interface-settings-' + locale, openq4_ui_system_interface_settings_test,
        args: [files('content/baseoq4/pak0/guis/menu/settings/system.q4ui'), meson.current_source_dir() / 'content/baseoq4/pak0/strings', locale], timeout: 600)
    endforeach
    openq4_ui_system_render_options_test = executable('openq4-ui-system-render-options-test',
      files('tools/tests/native/UiSystemRenderOptionsTest.cpp', 'src/framework/PerformancePreset.cpp'),
      include_directories: root_include_dir,
      dependencies: [openq4_retained_dep, rmlui_project.get_variable('rmlui_dep'), jsoncpp_project.get_variable('jsoncpp_dep')],
      install: false)
    foreach locale : system_page_locales
      test('openq4-ui-system-render-options-' + locale, openq4_ui_system_render_options_test,
        args: [files('content/baseoq4/pak0/guis/menu/settings/system.q4ui'), meson.current_source_dir() / 'content/baseoq4/pak0/strings', locale], timeout: 600)
    endforeach
    openq4_ui_system_display_controls_test = executable('openq4-ui-system-display-controls-test',
      files('tools/tests/native/UiSystemDisplayControlsTest.cpp', 'src/framework/PerformancePreset.cpp'),
      include_directories: root_include_dir,
      dependencies: [openq4_retained_dep, rmlui_project.get_variable('rmlui_dep'), jsoncpp_project.get_variable('jsoncpp_dep')],
      install: false)
    foreach locale : system_page_locales
      test('openq4-ui-system-display-controls-' + locale, openq4_ui_system_display_controls_test,
        args: [files('content/baseoq4/pak0/guis/menu/settings/system.q4ui'), meson.current_source_dir() / 'content/baseoq4/pak0/strings', locale], timeout: 600)
    endforeach
    openq4_ui_system_display_catalog_test = executable('openq4-ui-system-display-catalog-test',
      files('tools/tests/native/UiSystemDisplayCatalogTest.cpp', 'src/framework/PerformancePreset.cpp'),
      include_directories: root_include_dir,
      dependencies: [openq4_retained_dep, rmlui_project.get_variable('rmlui_dep'), jsoncpp_project.get_variable('jsoncpp_dep')],
      install: false)
    foreach locale : system_page_locales
      test('openq4-ui-system-display-catalog-' + locale, openq4_ui_system_display_catalog_test,
        args: [files('content/baseoq4/pak0/guis/menu/settings/system.q4ui'), meson.current_source_dir() / 'content/baseoq4/pak0/strings', locale], timeout: 900)
    endforeach
    openq4_ui_system_dimensions_test = executable('openq4-ui-system-dimensions-test',
      files('tools/tests/native/UiSystemDimensionsTest.cpp', 'src/framework/PerformancePreset.cpp'),
      include_directories: root_include_dir,
      dependencies: [openq4_retained_dep, rmlui_project.get_variable('rmlui_dep'), jsoncpp_project.get_variable('jsoncpp_dep')],
      install: false)
    foreach locale : system_page_locales
      test('openq4-ui-system-dimensions-' + locale, openq4_ui_system_dimensions_test,
        args: [files('content/baseoq4/pak0/guis/menu/settings/system.q4ui'), meson.current_source_dir() / 'content/baseoq4/pak0/strings', locale], timeout: 600)
    endforeach
    openq4_ui_model_scene_test = executable('openq4-ui-model-scene-test',
      files('tools/tests/native/UiModelSceneTest.cpp'),
      include_directories: root_include_dir,
      dependencies: [openq4_retained_dep, jsoncpp_project.get_variable('jsoncpp_dep')], install: false)
    test('openq4-ui-model-scene', openq4_ui_model_scene_test, timeout: 30)
    openq4_ui_player_setup_request_test = executable('openq4-ui-player-setup-request-test',
      files('tools/tests/native/UiPlayerSetupRequestTest.cpp'),
      include_directories: root_include_dir,
      dependencies: [openq4_retained_dep, jsoncpp_project.get_variable('jsoncpp_dep')], install: false)
    test('openq4-ui-player-setup-request', openq4_ui_player_setup_request_test, timeout: 30)
    openq4_ui_value_runtime_test = executable(
      'openq4-ui-value-runtime-test',
      files('tools/tests/native/UiValueRuntimeTest.cpp'),
      include_directories: root_include_dir,
      dependencies: [openq4_retained_dep, rmlui_project.get_variable('rmlui_dep'), jsoncpp_project.get_variable('jsoncpp_dep')],
      install: false,
    )
    test('openq4-ui-value-runtime', openq4_ui_value_runtime_test, timeout: 30)
    openq4_ui_document_test = executable(
      'openq4-ui-document-test',
      files('tools/tests/native/UiDocumentTest.cpp'),
      include_directories: root_include_dir,
      dependencies: openq4_retained_dep,
      install: false,
    )
    test('openq4-ui-document', openq4_ui_document_test, timeout: 30)
    openq4_ui_document_edit_test = executable('openq4-ui-document-edit-test',
      files('tools/tests/native/UiDocumentEditTest.cpp'),
      include_directories: [root_include_dir, include_directories('src/ui/retained')],
      dependencies: openq4_retained_dep, install: false)
    test('openq4-ui-document-edit', openq4_ui_document_edit_test, timeout: 30)
    openq4_ui_document_canvas_test = executable('openq4-ui-document-canvas-test',
      files('tools/tests/native/UiDocumentCanvasTest.cpp'),
      include_directories: root_include_dir,
      dependencies: [openq4_retained_dep, rmlui_project.get_variable('rmlui_dep'), jsoncpp_project.get_variable('jsoncpp_dep')],
      install: false)
    test('openq4-ui-document-canvas', openq4_ui_document_canvas_test, timeout: 120)
    openq4_ui_document_view_methods = custom_target('openq4-ui-document-view-methods',
      input: 'src/ui/RetainedUI.cpp', output: 'ViewMethods.inc',
      command: [py, files('tools/tests/ui_document_view_projection.py'), '@INPUT@', '@OUTPUT@'],
      depend_files: files('tools/tests/filesystem_case_segments.py'))
    openq4_ui_document_view_test = executable('openq4-ui-document-view-test',
      files('tools/tests/native/UiDocumentViewTest.cpp'), openq4_ui_document_view_methods,
      include_directories: root_include_dir,
      dependencies: [openq4_retained_dep, rmlui_project.get_variable('rmlui_dep'), jsoncpp_project.get_variable('jsoncpp_dep')],
      install: false)
    test('openq4-ui-document-view', openq4_ui_document_view_test, timeout: 120)
    openq4_ui_popup_placement_test = executable('openq4-ui-popup-placement-test',
      files('tools/tests/native/UiPopupPlacementTest.cpp'),
      include_directories: [root_include_dir, include_directories('src/ui/retained')],
      dependencies: openq4_retained_dep, install: false)
    test('openq4-ui-popup-placement', openq4_ui_popup_placement_test, timeout: 30)
    openq4_ui_popup_placement_runtime_test = executable('openq4-ui-popup-placement-runtime-test',
      files('tools/tests/native/UiPopupPlacementRuntimeTest.cpp'),
      include_directories: root_include_dir,
      dependencies: [openq4_retained_dep, rmlui_project.get_variable('rmlui_dep'), jsoncpp_project.get_variable('jsoncpp_dep')],
      install: false)
    test('openq4-ui-popup-placement-runtime', openq4_ui_popup_placement_runtime_test,
      args: [meson.project_source_root() / 'content/baseoq4/pak0/guis/menu/settings/system.q4ui',
             meson.project_source_root() / 'content/baseoq4/pak0/strings'], timeout: 900)
    # Windows ARM64 write-through I/O can exceed three minutes. Let the
    # harness's 600-second command timeout report the failure before Meson
    # kills it, allowing time for its three compiler invocations as well.
    durable_file_timeout = host_machine.system() == 'windows' ? 900 : 180
    test('openq4-ui-document-files', py,
      args: [meson.project_source_root() / 'tools/tests/durable_file.py',
        '--compiler', ui_test_cpp.cmd_array()[-1]], timeout: durable_file_timeout)
    # Each CI platform's own filesystem: save-slot commit and recovery across
    # killed processes.
    test('openq4-savegame-durability', py,
      args: [meson.project_source_root() / 'tools/tests/savegame_durability.py',
        '--compiler', ui_test_cpp.cmd_array()[-1]], timeout: durable_file_timeout)
    # Portable save codecs (roadmap Q4-06): golden bytes with this platform's
    # compiler, and source mutations the golden checks must catch.
    test('openq4-save-codec', py,
      args: [meson.project_source_root() / 'tools/tests/save_codec.py',
        '--compiler', ui_test_cpp.cmd_array()[-1]], timeout: durable_file_timeout)
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
    foreach scope : ['machinegun', 'railgun', 'nailgun']
      test('openq4-ui-scope-paint-' + scope, openq4_ui_vector_test,
        args: [meson.project_source_root() / ('content/baseoq4/pak0/guis/weapons/' + scope + '_scope.q4ui')], timeout: 30)
    endforeach
    executable(
      'openq4-retained-ui-benchmark',
      files('tools/tests/native/RetainedUiBenchmark.cpp'),
      include_directories: root_include_dir,
      dependencies: [openq4_retained_dep, jsoncpp_project.get_variable('jsoncpp_dep')],
      install: false,
    )
  endif
endif

openq4_pak_files = []
openq4_paks_generated_header = []
if build_engine or build_games
  openq4_pak_script_files = files(
    'tools/build/openq4_pak.py',
    'tools/build/content_packs.json',
  )
  openq4_pak_manifest_script_files = files(
    'tools/build/write_pak_manifest.py',
  )

  # tools/build/content_packs.json lists the runtime packs; each gets a source
  # manifest target and a pack target, and the generated header embeds every
  # pack's digest into the engine.
  # Reading the table through fs makes Meson reconfigure when it changes, so an
  # overlay added or removed there gains or loses its target.
  fs_mod.read('tools/build/content_packs.json')
  openq4_content_pack_table = run_command(
    py,
    meson.project_source_root() / 'tools' / 'build' / 'content_packs.py',
    'meson-list',
    check: true,
  ).stdout().strip().split('\n')
  openq4_pak_header_args = []
  # A release passes -Dprebuilt_content_packs=<dir>: every platform then copies
  # the release's single build of each pack instead of repacking it.
  openq4_prebuilt_pack_args = []
  if get_option('prebuilt_content_packs') != ''
    openq4_prebuilt_pack_args = ['--prebuilt-dir', get_option('prebuilt_content_packs')]
  endif
  foreach openq4_pack_row : openq4_content_pack_table
    openq4_pack_fields = openq4_pack_row.strip().split('|')
    openq4_pack_name = openq4_pack_fields[0]
    openq4_pack_source = openq4_pack_fields[1]
    openq4_pack_stem = openq4_pack_fields[2]
    openq4_pack_manifest = custom_target(
      'openq4_' + openq4_pack_stem + '_source_manifest',
      output: openq4_pack_stem + '.sources',
      command: [
        py,
        meson.project_source_root() / 'tools' / 'build' / 'write_pak_manifest.py',
        meson.project_source_root(),
        openq4_pack_name,
        openq4_pack_source,
        '@OUTPUT@',
      ],
      depend_files: openq4_pak_script_files + openq4_pak_manifest_script_files,
      build_always_stale: true,
      build_by_default: true,
    )
    openq4_pack_output = custom_target(
      'openq4_' + openq4_pack_stem,
      input: openq4_pack_manifest,
      output: openq4_pack_name,
      command: [
        py,
        meson.project_source_root() / 'tools' / 'build' / 'build_openq4_pack.py',
        '--pak-name',
        openq4_pack_name,
        '--source-dir',
        meson.project_source_root() / openq4_pack_source,
        '--manifest',
        '@INPUT@',
        '--out',
        '@OUTPUT@',
        '--stage-out',
        meson.current_build_dir() / 'baseoq4' / openq4_pack_name,
        '--source-root',
        meson.project_source_root(),
      ] + openq4_prebuilt_pack_args,
      depend_files: openq4_pak_script_files + files(
        'tools/build/build_openq4_pack.py',
      ),
      build_by_default: true,
      install: true,
      install_dir: install_game_dir,
    )
    openq4_pak_files += [openq4_pack_output]
    openq4_pak_header_args += ['--pack', openq4_pack_name + '=@INPUT' + (openq4_pak_files.length() - 1).to_string() + '@']
  endforeach
  openq4_paks_header_output = custom_target(
    'openq4_paks_generated_header',
    input: openq4_pak_files,
    output: 'openq4_paks_generated.h',
    command: [
      py,
      meson.project_source_root() / 'tools' / 'build' / 'generate_pak_header.py',
    ] + openq4_pak_header_args + [
      '--header-out',
      '@OUTPUT@',
    ],
    depend_files: openq4_pak_script_files + files(
      'tools/build/generate_pak_header.py',
      'tools/build/content_packs.json',
    ),
    build_by_default: true,
    install: false,
  )
  openq4_paks_generated_header = [openq4_paks_header_output]
endif

if needs_game_headers
  foreach game_tree : ['game', 'mpgame']
    if not fs_mod.is_dir(game_libs_source_root / game_tree)
      error('Canonical game source directory not found: ' + game_libs_source_root / game_tree)
    endif
  endforeach
  game_sp_module_defs_file = game_stage_game_root / 'Game.def'
  game_mp_module_defs_file = game_stage_mpgame_root / 'mpgame.def'
  game_source_inventory = configure_file(
    output: 'openq4_game_sources.json',
    command: [py, files('tools/build/game_source_inventory.py'), meson.project_source_root()],
    capture: true,
  )
endif

# Awakening SP is built into game_sp; q4xbase now supplies content only.
awakening_summary = 'built into game_sp (user-supplied campaign content)'

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
engine_source_paths += ['src/ui/application/SystemSettingsHost.cpp', 'src/ui/application/SystemDisplay.cpp']
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
    # Session records the canonical game source stamp before tearing down
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
    # rc.exe reports no header dependencies, so without this the version
    # strings stay at whichever commit first compiled the resource.
    depends: generated_version_header,
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

  # openQ4's VFS callback entry point keeps compressed streams bounded without
  # extracting PK4 members. A system copy does not contain that small extension.
  stb_vorbis_dep = subproject('stb_vorbis', default_options: fallback_opt).get_variable('stb_vorbis_dep')

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
      '--api', android_native_api.to_string(), '--abi', android_abi,
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
    android_build_config.set('ANDROID_ABI', android_abi)
    android_build_config.set('ANDROID_VARIANT', android_variant)
    android_build_config.set('ANDROID_API', android_native_api)
    android_build_config.set('SDL_VERSION', '.'.join(android_sdl_version))
    configure_file(
      input: 'tools/build/android-build.json.in',
      output: 'android-build.json',
      configuration: android_build_config,
      install: true,
      install_dir: install_root_dir,
    )
    if android_variant == 'quest'
      install_data('mobile/android/app/src/quest/assets/openq4-headset', install_dir: install_root_dir)
    endif
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
  # RetainedHud parses the coverage catalog directly, including on platforms
  # where the optional OpenXR loader does not supply JsonCpp transitively.
  client_deps += jsoncpp_project.get_variable('jsoncpp_dep')
  if host_system == 'windows'
    client_deps += openq4_windows_text_store_dep
  endif
endif

# OpenXR virtual reality (docs/dev/archive/2026-10-18/plans/2026-10-02-openxr-vr.md). The Khronos
# loader links statically into the SDL3 client on Windows and Linux, built from
# the pinned SDK against the jsoncpp subproject above; nothing ships beside the
# executable and dedicated servers never see it.
openxr_option = get_option('openxr')
build_openxr = build_engine and use_sdl3_backend and not openxr_option.disabled() \
  and (host_system == 'windows' or host_system == 'linux')
if openxr_option.enabled() and not build_openxr
  error('openxr requires the SDL3 client on Windows or Linux.')
endif
openxr_loader_dep = disabler()
openxr_headers_dep = disabler()
if build_openxr
  openxr_project = subproject('openxr', required: openxr_option)
  if openxr_project.found()
    openxr_loader_dep = openxr_project.get_variable('openxr_loader_dep')
    openxr_headers_dep = openxr_project.get_variable('openxr_headers_dep')
    client_deps += openxr_loader_dep
  else
    build_openxr = false
  endif
endif

# Native-record translation is separate from the portable runtime and engine
# PCH. A checked package capability, not SDL's version, enables the importer.
if build_engine or get_option('build_native_tests')
  text_ingress_args = []
  text_ingress_headers = []
  # openQ4's private SDL extension headers. The bundled SDL subproject carries
  # them on its own include path; a system or Android SDL needs the overlay's
  # copy, which Meson reports as reaching into the subproject directory.
  sdl3_openq4_extension_includes = []
  if build_engine and use_sdl3_backend
    text_ingress_headers = [sdl3_dep.partial_dependency(compile_args: true, includes: true)]
    if host_system == 'android' or sdl3_dep.type_name() != 'internal'
      sdl3_openq4_extension_includes = [include_directories('subprojects/packagefiles/sdl3/include')]
    endif
    if cpp.compiles('''
      #if !defined(OPENQ4_SDL3_TEXT_PROVENANCE) || OPENQ4_SDL3_TEXT_PROVENANCE != 1
      #error No verified private provider capability
      #endif
      #include <SDL3/SDL_openq4_text_provenance.h>
      int main() { OQ4_TextRecord record{}; return record.version; }
      ''', dependencies: text_ingress_headers, name: 'verified SDL native text provenance header')
      text_ingress_args = ['-DOPENQ4_SDL_TEXT_PROVENANCE=1']
    endif
  endif
  text_ingress_library = static_library('openq4-text-ingress',
    files('src/sys/sdl3/TextProvenanceIngress.cpp'),
    dependencies: [openq4_retained_dep] + text_ingress_headers,
    cpp_args: text_ingress_args, pic: true, install: false)
  text_ingress_dep = declare_dependency(link_with: text_ingress_library)
  if build_engine
    client_deps += text_ingress_dep
  endif
  if build_engine and use_sdl3_backend
    native_queue_library = static_library('openq4-native-queue',
      files('src/sys/sdl3/NativeQueueBatch.cpp', 'src/sys/sdl3/NativeQueueSource.cpp',
        'src/sys/sdl3/NativeEventDisposition.cpp'),
      files('src/ui/application/NativeTextCollectionCoordinator.cpp'),
      files('src/ui/application/ManagedNativeTextOwner.cpp'),
      include_directories: [root_include_dir] + sdl3_openq4_extension_includes,
      dependencies: [openq4_retained_dep] + text_ingress_headers,
      cpp_args: ['-DUSE_SDL3'], pic: true, install: false)
    native_queue_dep = declare_dependency(link_with: native_queue_library)
    client_deps += native_queue_dep
    if host_system == 'windows'
      # Exact store/provider collection adapter; creation never enables input.
      windows_collection_bridge_library = static_library('openq4-windows-text-collection-bridge',
        files('src/sys/sdl3/WindowsTextCollectionBridge.cpp'),
        include_directories: [root_include_dir] + sdl3_openq4_extension_includes,
        dependencies: [openq4_windows_text_store_dep, openq4_retained_dep] + text_ingress_headers,
        cpp_args: ['-DUSE_SDL3'], install: false)
      windows_collection_bridge_dep = declare_dependency(link_with: windows_collection_bridge_library)
      client_deps += windows_collection_bridge_dep
      # Native construction/cleanup stay outside the engine PCH. The controller
      # requires explicit checked Session admission; linking never enables it.
      windows_text_session_sdk_deps = [windows_ole_dep, cc.find_library('uuid', required: true)]
      windows_text_session_library = static_library('openq4-windows-text-session',
        files('src/sys/sdl3/WindowsTextSession.cpp', 'src/sys/sdl3/WindowsNativeInputRouteSource.cpp'),
        include_directories: [root_include_dir] + sdl3_openq4_extension_includes,
        dependencies: [windows_collection_bridge_dep, openq4_windows_text_store_dep, openq4_retained_dep, native_queue_dep] + text_ingress_headers + windows_text_session_sdk_deps,
        cpp_args: ['-DUSE_SDL3'], install: false)
      windows_text_session_dep = declare_dependency(link_with: windows_text_session_library, dependencies: windows_text_session_sdk_deps)
      client_deps += windows_text_session_dep
    endif
  endif
  if get_option('build_native_tests') and build_engine and use_sdl3_backend
    text_ingress_test_headers = [root_include_dir] + sdl3_openq4_extension_includes
    android_console_source = custom_target(
      'openq4-android-console-source',
      output: 'AndroidConsoleInput.cpp',
      depend_files: files('src/sys/posix/posix_syscon.cpp', 'src/sys/sys_console_theme.h',
        'src/sys/sdl3/sdl3_backend.cpp', 'src/framework/KeyInput.h'),
      command: [py, files('tools/tests/android_console_input.py'), '--generate-native', '@OUTPUT@'],
    )
    foreach platform : ['android', 'desktop']
      android_console_test = executable(
        'openq4-android-console-' + platform, android_console_source,
        include_directories: include_directories('src'),
        dependencies: text_ingress_headers,
        cpp_args: platform == 'android' ? ['-D__ANDROID__=1'] : [], install: false,
      )
      test('openq4-android-console-' + platform, android_console_test, timeout: 30)
    endforeach
    # Follow the pinned source tree used by SDK CI. A stale version literal
    # silently omitted these tests after the SDL dependency was updated.
    native_facts_sdl_directory = run_command(py, '-c',
      'import configparser,sys; c=configparser.ConfigParser(); c.read(sys.argv[1]); print(c["wrap-file"]["directory"])',
      meson.project_source_root() / 'subprojects/sdl3.wrap', check: true).stdout().strip()
    native_facts_sdl_headers = meson.project_source_root() / 'subprojects' / native_facts_sdl_directory / 'include'
    if fs_mod.is_file(native_facts_sdl_headers / 'SDL3/SDL_events.h')
      native_facts_args = ['--compiler', cpp.cmd_array()[-1], '--no-mutations',
        '--sdl-include', native_facts_sdl_headers]
      if cpp.get_id() == 'msvc'
        native_facts_args += ['--msvc-debug']
      endif
      test('openq4-ui-native-input-facts', py,
        args: [meson.project_source_root() / 'tools/tests/native_input_facts.py'] + native_facts_args, timeout: 180)
      test('openq4-ui-native-input-terminal', py,
        args: [meson.project_source_root() / 'tools/tests/native_input_terminal.py'] + native_facts_args, timeout: 180)
      if host_system == 'windows'
        test('openq4-ui-windows-native-input-facts', py,
          args: [meson.project_source_root() / 'tools/tests/windows_text_session.py', '--route-facts'] + native_facts_args,
          timeout: 180)
      endif
    endif
    native_collection_coordinator_test = executable('openq4-ui-native-collection-coordinator-test',
      files('tools/tests/native/UiNativeCollectionCoordinatorTest.cpp'),
      include_directories: text_ingress_test_headers,
      dependencies: [openq4_retained_dep, native_queue_dep] + text_ingress_headers, install: false)
    test('openq4-ui-native-collection-coordinator', native_collection_coordinator_test, timeout: 30)
    native_event_disposition_test = executable('openq4-ui-native-event-disposition-test',
      files('tools/tests/native/NativeRouteAccountingTest.cpp'),
      include_directories: text_ingress_test_headers,
      dependencies: [openq4_retained_dep, native_queue_dep] + text_ingress_headers, install: false)
    test('openq4-ui-native-event-disposition', native_event_disposition_test, timeout: 30)
    if host_system == 'windows'
      windows_collection_bridge_test = executable('openq4-ui-windows-text-collection-bridge-test',
        files('tools/tests/native/WindowsTextCollectionBridgeTest.cpp'),
        include_directories: text_ingress_test_headers + [include_directories('src')],
        dependencies: [windows_collection_bridge_dep, openq4_windows_text_store_dep, openq4_retained_dep] + text_ingress_headers,
        cpp_args: ['-DUSE_SDL3'], install: false)
      test('openq4-ui-windows-text-collection-bridge', windows_collection_bridge_test, timeout: 30)
      windows_text_session_test = executable('openq4-ui-windows-text-session-test',
        files('tools/tests/native/WindowsTextSessionTest.cpp'),
        include_directories: text_ingress_test_headers + [include_directories('src')],
        dependencies: [windows_text_session_dep, windows_collection_bridge_dep, openq4_windows_text_store_dep, openq4_retained_dep, native_queue_dep] + text_ingress_headers,
        cpp_args: ['-DUSE_SDL3'], install: false)
      test('openq4-ui-windows-text-session', windows_text_session_test, timeout: 30)
    endif
    text_ingress_test = executable('openq4-ui-text-ingress-test',
      files('tools/tests/native/UiTextProvenanceIngressTest.cpp'),
      include_directories: text_ingress_test_headers,
      dependencies: [openq4_retained_dep, text_ingress_dep] + text_ingress_headers,
      cpp_args: text_ingress_args, install: false)
    test('openq4-ui-text-ingress', text_ingress_test, timeout: 30)
    text_ingress_unsupported_test = executable('openq4-ui-text-ingress-unsupported-test',
      files('tools/tests/native/UiTextProvenanceIngressTest.cpp', 'src/sys/sdl3/TextProvenanceIngress.cpp'),
      include_directories: text_ingress_test_headers,
      dependencies: [openq4_retained_dep] + text_ingress_headers, install: false)
    test('openq4-ui-text-ingress-unsupported', text_ingress_unsupported_test, timeout: 30)
  endif
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
src_maya_import_include_dir = include_directories('src/mayaimport')
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
  dedicated_shares_client_sources = true
  if windows_gl_free_ded
    # ded keeps the static renderer while the client sheds it
    dedicated_shares_client_sources = false
    openq4_dedicated_sources = files(dedicated_engine_source_paths)
    openq4_dedicated_sources += [openq4_paks_generated_header, generated_version_header]
    if build_games
      openq4_dedicated_sources += [savegame_compat_header]
    endif
    openq4_dedicated_sources += openq4_version_resources
  endif
  if host_system == 'linux'
    dedicated_shares_client_sources = false
    openq4_dedicated_sources = files(dedicated_engine_source_paths)
    openq4_dedicated_sources += [openq4_paks_generated_header, generated_version_header]
    if build_games
      openq4_dedicated_sources += [savegame_compat_header]
    endif
  endif

  # The inventory and transfers call the client owned-queue ledger. Keep them out of dedicated
  # sources, which deliberately do not link the client native queue library.
  if use_sdl3_backend
    # The SDL client's list drops the callback-free input facade for this
    # driver, so a dedicated target sharing that list (macOS) takes it back.
    if dedicated_shares_client_sources
      openq4_dedicated_sources += files('src/framework/NativeInputDispatchDisabled.cpp')
    endif
    openq4_engine_sources += files(
      'src/sys/sdl3/NativeInputEmissionInventory.cpp',
      'src/sys/sdl3/NativeInputTransfers.cpp',
      'src/sys/sdl3/NativeInputDriver.cpp')
  endif

  # The OpenXR session, input and frame loop are client-only for the same
  # reason: the dedicated snapshot above never contains them.
  if build_openxr
    openq4_engine_sources += files('src/sys/openxr/OpenXRSystem.cpp')
    client_engine_cpp_args += ['-DOPENQ4_OPENXR=1']
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

# OpenGL renderer module (Phase B8, docs/dev/archive/2026-10-18/plans/2026-07-16-vulkan-renderer-phase-b.md).
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
# docs/dev/archive/2026-10-18/macos-moltenvk-decision.md; the module is built and shipped there but
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
# docs/dev/archive/2026-10-18/plans/2026-07-17-vulkan-phase-c.md): the shared renderer front-end
# with the GL backend replaced by the Vulkan backend. Mixed front-end TUs keep
# their GL call sites satisfied by the hook-resolving GLEW flavor + the
# module's GL stub TU; no GL and no SDL are linked.
if build_renderer_vk and build_engine
  # Keep device admission independent of renderer globals and the engine PCH.
  # Native tests exercise this exact implementation through Vulkan query PFNs.
  vk_device_selection = static_library('openq4-vulkan-device-selection',
    'src/renderer/Vulkan/VulkanDeviceSelection.cpp',
    include_directories: include_directories('src/external/vulkan/include'),
    install: false)
  vk_device_selection_dep = declare_dependency(link_with: vk_device_selection,
    include_directories: include_directories('src/external/vulkan/include'))
  if get_option('build_native_tests')
    vk_device_selection_test = executable('openq4-vulkan-device-selection-test',
      'tools/tests/native/VulkanDeviceSelectionTest.cpp',
      dependencies: vk_device_selection_dep, install: false)
    test('openq4-vulkan-device-selection', vk_device_selection_test, timeout: 30)
  endif

  # Compile authored material shaders in process. The pinned, static compiler
  # follows the project's CRT/architecture settings; no SDK executable is used
  # or installed at runtime. Keep optional tools and external dependencies out.
  glslang_project = subproject('glslang')
  glslang_deps = [glslang_project.get_variable('glslang_dep')]
  install_data('LICENSES/glslang.txt', install_dir: install_root_dir / 'licenses')

  material_compiler = static_library('openq4-material-compiler',
    'src/renderer/materialprogram/GLSLCompiler.cpp',
    'src/renderer/materialprogram/ARBTranslator.cpp',
    dependencies: glslang_deps, install: false)
  material_compiler_dep = declare_dependency(link_with: material_compiler,
    dependencies: glslang_deps)
  if get_option('build_native_tests')
    material_compiler_test = executable('openq4-material-compiler-test',
      'tools/tests/native/MaterialCompilerTest.cpp',
      dependencies: material_compiler_dep, install: false)
    test('openq4-material-compiler', material_compiler_test,
      args: ['--retained', meson.project_source_root() / 'content/baseoq4/pak0/glprogs/glsl'], timeout: 60)
    arb_translator_test = executable('openq4-arb-translator-test',
      'tools/tests/native/ARBTranslatorTest.cpp',
      dependencies: material_compiler_dep, install: false)
    test('openq4-arb-translator', arb_translator_test, timeout: 60)
  endif

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
  renderer_vk_deps = [glew_dedicated_dep, material_compiler_dep, vk_device_selection_dep]
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

install_data('LICENSE', 'LICENSING.md', 'CONTRIBUTORS.md', install_dir: install_root_dir)
install_data('LICENSES/QUAKE-4-SDK-EULA.rtf',
  'LICENSES/DOOM-3-ADDITIONAL-TERMS.txt', 'LICENSES/DOOM-3-BFG-ADDITIONAL-TERMS.txt',
  install_dir: install_root_dir / 'licenses')
meson.add_install_script(py, files('tools/build/retire_game_layers.py'), install_root_dir)
install_data('LICENSES/KHRONOS-GLES-MIT.txt', install_dir: install_root_dir / 'licenses')
install_data('LICENSES/RmlUi.txt', install_dir: install_root_dir / 'licenses')
install_data('LICENSES/JsonCpp.txt', install_dir: install_root_dir / 'licenses')
install_data('LICENSES/libtess2.txt', install_dir: install_root_dir / 'licenses')
install_data('LICENSES/MD4.txt', install_dir: install_root_dir / 'licenses')
if build_openxr
  # the client links the Khronos OpenXR loader statically
  install_data('LICENSES/openxr-sdk.txt', install_dir: install_root_dir / 'licenses')
endif

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

# OpenXR test runtime: a simulated headset the VR smoke runs the client against
# through XR_RUNTIME_JSON, so no system runtime registration is touched. It is a
# test fixture, never installed or packaged.
if build_openxr
  openxr_test_runtime_gl_deps = host_system == 'windows' \
    ? [cc.find_library('opengl32', required: true)] : [dependency('gl', required: true)]
  openxr_test_runtime = shared_module(
    'openq4-xr-test-runtime',
    files('tools/tests/openxr/OpenXRTestRuntime.cpp'),
    # XR_KHR_vulkan_enable2 through the application's loader: headers only
    include_directories: include_directories('src/external/vulkan/include'),
    dependencies: [openxr_headers_dep] + openxr_test_runtime_gl_deps,
    name_prefix: '',
    gnu_symbol_visibility: 'hidden',
    install: false,
  )
  openxr_test_runtime_manifest = configuration_data()
  openxr_test_runtime_manifest.set('LIBRARY', 'openq4-xr-test-runtime.' + (host_system == 'windows' ? 'dll' : 'so'))
  configure_file(
    input: 'tools/tests/openxr/openq4_xr_test_runtime.json.in',
    output: 'openq4_xr_test_runtime.json',
    configuration: openxr_test_runtime_manifest,
  )
  if get_option('build_native_tests')
    openq4_xr_lifecycle_source = custom_target(
      'openq4-xr-lifecycle-source',
      input: files('src/sys/openxr/OpenXRSystem.cpp'),
      output: 'OpenXRLifecycleTest.cpp',
      command: [py, files('tools/tests/openxr_lifecycle.py'), '--generate-native', '@OUTPUT@'],
    )
    openq4_xr_lifecycle_test = executable(
      'openq4-xr-lifecycle-test', openq4_xr_lifecycle_source,
      dependencies: openxr_headers_dep, cpp_args: ['-UNDEBUG'], install: false,
    )
    foreach fault : ['normal', 'teardown', 'input_teardown', 'partial_acquire', 'wait_loss', 'wait_timeout', 'wait_error',
                     'release_loss', 'release_error', 'index_large', 'index_end', 'resize_loss', 'resize_failure',
                     'count_large', 'count_zero', 'count_changed', 'image_zero', 'instance_loss', 'session_pending']
      test('openq4-xr-lifecycle-' + fault, openq4_xr_lifecycle_test, args: [fault], timeout: 30)
    endforeach
  endif
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
    'Awakening SP campaign': awakening_summary,
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
    'OpenXR VR (client)': build_openxr,
    'Sigma Touch host adapter': build_sigmatouch,
    'Linux dedicated headless link split': build_engine and host_system == 'linux',
    'Install root': install_root_dir,
  },
  section: 'Build Features',
  bool_yn: true,
)
