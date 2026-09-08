if build_games and build_game_sp
  if host_system == 'windows'
    game_sp_module_target = shared_module(
      game_sp_binary_name,
      game_sp_sources,
      include_directories: game_include_dirs,
      link_with: game_idlib_library,
      cpp_args: game_common_cpp_args,
      c_args: game_common_c_args,
      cpp_pch: game_module_pch_headers,
      override_options: game_target_override_options,
      link_args: game_module_link_args,
      vs_module_defs: game_sp_module_defs_file,
      name_prefix: module_filename_prefix,
      install: true,
      install_dir: install_module_dir,
    )
  elif host_system == 'darwin'
    game_sp_module_target = shared_library(
      game_sp_binary_name,
      game_sp_sources,
      include_directories: game_include_dirs,
      link_with: game_idlib_library,
      cpp_args: game_common_cpp_args,
      c_args: game_common_c_args,
      cpp_pch: game_module_pch_headers,
      override_options: game_target_override_options,
      link_args: game_module_link_args + ['-Wl,-install_name,@loader_path/' + game_sp_binary_name + '.dylib'],
      link_depends: game_module_link_depends,
      gnu_symbol_visibility: 'hidden',
      name_prefix: module_filename_prefix,
      name_suffix: 'dylib',
      install: true,
      install_dir: install_module_dir,
    )
  else
    game_sp_module_target = shared_module(
      game_sp_binary_name,
      game_sp_sources,
      include_directories: game_include_dirs,
      link_with: game_idlib_library,
      cpp_args: game_common_cpp_args,
      c_args: game_common_c_args,
      cpp_pch: game_module_pch_headers,
      override_options: game_target_override_options,
      link_args: game_module_link_args,
      link_depends: game_module_link_depends,
      gnu_symbol_visibility: 'hidden',
      name_prefix: module_filename_prefix,
      install: true,
      install_dir: install_module_dir,
    )
  endif
endif

if build_games and build_game_mp
  if host_system == 'windows'
    game_mp_module_target = shared_module(
      game_mp_binary_name,
      game_mp_sources,
      include_directories: game_include_dirs,
      link_with: game_idlib_library_mp,
      cpp_args: game_common_cpp_args + ['-DGAME_MPAPI'],
      c_args: game_common_c_args,
      cpp_pch: game_module_pch_headers,
      override_options: game_target_override_options,
      link_args: game_module_link_args,
      vs_module_defs: game_mp_module_defs_file,
      name_prefix: module_filename_prefix,
      install: true,
      install_dir: install_module_dir,
    )
  elif host_system == 'darwin'
    game_mp_module_target = shared_library(
      game_mp_binary_name,
      game_mp_sources,
      include_directories: game_include_dirs,
      link_with: game_idlib_library_mp,
      cpp_args: game_common_cpp_args + ['-DGAME_MPAPI'],
      c_args: game_common_c_args,
      cpp_pch: game_module_pch_headers,
      override_options: game_target_override_options,
      link_args: game_module_link_args + ['-Wl,-install_name,@loader_path/' + game_mp_binary_name + '.dylib'],
      link_depends: game_module_link_depends,
      gnu_symbol_visibility: 'hidden',
      name_prefix: module_filename_prefix,
      name_suffix: 'dylib',
      install: true,
      install_dir: install_module_dir,
    )
  else
    game_mp_module_target = shared_module(
      game_mp_binary_name,
      game_mp_sources,
      include_directories: game_include_dirs,
      link_with: game_idlib_library_mp,
      cpp_args: game_common_cpp_args + ['-DGAME_MPAPI'],
      c_args: game_common_c_args,
      cpp_pch: game_module_pch_headers,
      override_options: game_target_override_options,
      link_args: game_module_link_args,
      link_depends: game_module_link_depends,
      gnu_symbol_visibility: 'hidden',
      name_prefix: module_filename_prefix,
      install: true,
      install_dir: install_module_dir,
    )
  endif
endif

baseoq4_manifest_conf = configuration_data()
baseoq4_manifest_conf.set('OPENQ4_VERSION_BASE', openq4_version_base)
baseoq4_manifest = configure_file(
  input: 'mod.json.in',
  output: 'mod.json',
  configuration: baseoq4_manifest_conf,
)

install_data(
  baseoq4_manifest,
  install_dir: install_game_dir,
)

custom_target(
  'openq4_stage_mod_manifest_direct_run',
  input: baseoq4_manifest,
  output: 'stage-mod-manifest.stamp',
  command: [
    py,
    meson.project_source_root() / 'tools' / 'build' / 'stage_direct_run_game_module.py',
    '--source',
    '@INPUT@',
    '--destination',
    meson.project_build_root() / 'baseoq4' / 'mod.json',
    '--stamp',
    '@OUTPUT@',
  ],
  depend_files: files('../../tools/build/stage_direct_run_game_module.py'),
  build_always_stale: true,
  build_by_default: true,
)
