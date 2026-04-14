if build_games and build_game_sp
  if host_system == 'windows'
    shared_module(
      game_sp_binary_name,
      game_sources,
      include_directories: game_include_dirs,
      link_with: game_idlib_library,
      cpp_args: game_common_cpp_args,
      c_args: game_common_c_args,
      cpp_pch: game_module_pch_headers,
      link_args: game_module_link_args,
      vs_module_defs: game_module_defs_file,
      name_prefix: '',
      install: true,
      install_dir: install_game_dir,
    )
  else
    shared_module(
      game_sp_binary_name,
      game_sources,
      include_directories: game_include_dirs,
      link_with: game_idlib_library,
      cpp_args: game_common_cpp_args,
      c_args: game_common_c_args,
      cpp_pch: game_module_pch_headers,
      link_args: game_module_link_args,
      name_prefix: '',
      install: true,
      install_dir: install_game_dir,
    )
  endif
endif

if build_games and build_game_mp
  if host_system == 'windows'
    shared_module(
      game_mp_binary_name,
      game_sources,
      include_directories: game_include_dirs,
      link_with: game_idlib_library,
      cpp_args: game_common_cpp_args + ['-DGAME_MPAPI'],
      c_args: game_common_c_args,
      cpp_pch: game_module_pch_headers,
      link_args: game_module_link_args,
      vs_module_defs: game_module_defs_file,
      name_prefix: '',
      install: true,
      install_dir: install_game_dir,
    )
  else
    shared_module(
      game_mp_binary_name,
      game_sources,
      include_directories: game_include_dirs,
      link_with: game_idlib_library,
      cpp_args: game_common_cpp_args + ['-DGAME_MPAPI'],
      c_args: game_common_c_args,
      cpp_pch: game_module_pch_headers,
      link_args: game_module_link_args,
      name_prefix: '',
      install: true,
      install_dir: install_game_dir,
    )
  endif
endif

install_data(
  'mod.json',
  'default.cfg',
  'openq4_defaults.cfg',
  'openq4_profile_steamdeck.cfg',
  install_dir: install_game_dir,
)

install_subdir(
  'materials',
  install_dir: install_game_dir,
)

install_subdir(
  'def',
  install_dir: install_game_dir,
)

install_subdir(
  'botfiles',
  install_dir: install_game_dir,
)

install_subdir(
  'glprogs',
  install_dir: install_game_dir,
)

install_subdir(
  'guis',
  install_dir: install_game_dir,
)

install_subdir(
  'gfx',
  install_dir: install_game_dir,
)

install_subdir(
  'strings',
  install_dir: install_game_dir,
)

install_subdir(
  'maps',
  install_dir: install_game_dir,
)
