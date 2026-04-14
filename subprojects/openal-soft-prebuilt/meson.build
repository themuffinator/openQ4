project('openal-soft-prebuilt', 'c')

if host_machine.system() != 'windows'
  openal_dep = dependency('openal', required: true)
  subdir_done()
endif

if host_machine.cpu_family() != 'x86_64'
  error(
    'Bundled OpenAL import library is only provided for x86_64 Windows. '
    + 'Provide a custom Windows OpenAL package with -Dopenal_root_override=<path>.'
  )
endif

fs_mod = import('fs')
cc = meson.get_compiler('c')
openal_include = include_directories('include')
openal_lib_name = 'OpenAL32'
openal_lib_dir = meson.current_source_dir() / 'lib' / 'win64'
openal_lib_path = openal_lib_dir / (openal_lib_name + '.lib')

if not fs_mod.exists(openal_lib_path)
  openal_def_path = openal_lib_dir / (openal_lib_name + '.def')
  if not fs_mod.exists(openal_def_path)
    error('Neither OpenAL32.lib nor OpenAL32.def was found in ' + openal_lib_dir)
  endif

  lib_tool = find_program('lib', required: true)
  generated_lib_path = meson.current_build_dir() / (openal_lib_name + '.lib')

  run_command(
    lib_tool,
    '/NOLOGO',
    '/MACHINE:X64',
    '/DEF:' + openal_def_path,
    '/OUT:' + generated_lib_path,
    check: true,
  )

  openal_lib_dir = meson.current_build_dir()
endif

openal_lib = cc.find_library(
  openal_lib_name,
  dirs: [openal_lib_dir],
  required: true,
)

openal_dep = declare_dependency(
  include_directories: openal_include,
  dependencies: [openal_lib],
)
