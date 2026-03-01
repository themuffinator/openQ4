if build_games and build_game_sp
  if host_system == 'windows'
    shared_module(
      game_sp_binary_name,
      game_sp_sources,
      include_directories: src_include_dir,
      link_with: game_idlib_library,
      cpp_args: game_common_cpp_args,
      c_args: game_common_c_args,
      cpp_pch: ['../src/idlib/precompiled.h'],
      link_args: game_module_link_args,
      vs_module_defs: '../src/game/Game.def',
      name_prefix: '',
      install: true,
      install_dir: install_openq4_dir,
    )
  else
    shared_module(
      game_sp_binary_name,
      game_sp_sources,
      include_directories: src_include_dir,
      link_with: game_idlib_library,
      cpp_args: game_common_cpp_args,
      c_args: game_common_c_args,
      cpp_pch: ['../src/idlib/precompiled.h'],
      link_args: game_module_link_args,
      name_prefix: '',
      install: true,
      install_dir: install_openq4_dir,
    )
  endif
endif

if build_games and build_game_mp
  if host_system == 'windows'
    shared_module(
      game_mp_binary_name,
      game_sources,
      include_directories: src_include_dir,
      link_with: game_idlib_library,
      cpp_args: game_common_cpp_args + ['-DGAME_MPAPI'],
      c_args: game_common_c_args,
      cpp_pch: ['../src/idlib/precompiled.h'],
      link_args: game_module_link_args,
      vs_module_defs: '../src/game/Game.def',
      name_prefix: '',
      install: true,
      install_dir: install_openq4_dir,
    )
  else
    shared_module(
      game_mp_binary_name,
      game_sources,
      include_directories: src_include_dir,
      link_with: game_idlib_library,
      cpp_args: game_common_cpp_args + ['-DGAME_MPAPI'],
      c_args: game_common_c_args,
      cpp_pch: ['../src/idlib/precompiled.h'],
      link_args: game_module_link_args,
      name_prefix: '',
      install: true,
      install_dir: install_openq4_dir,
    )
  endif
endif

install_subdir(
  'materials',
  install_dir: install_openq4_dir,
)

install_subdir(
  'glprogs',
  install_dir: install_openq4_dir,
)
