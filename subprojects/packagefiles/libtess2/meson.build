project('libtess2', 'c', version: '8dbd6483', license: 'SGI-B-2.0',
  meson_version: '>=1.6', default_options: ['c_std=c11', 'warning_level=1'])
libtess2_includes = include_directories('Include')
libtess2_library = static_library('tess2', files(
  'Source/bucketalloc.c', 'Source/dict.c', 'Source/geom.c', 'Source/mesh.c',
  'Source/priorityq.c', 'Source/sweep.c', 'Source/tess.c'),
  include_directories: libtess2_includes, pic: true, install: false)
libtess2_dep = declare_dependency(link_with: libtess2_library,
  include_directories: libtess2_includes)
meson.override_dependency('libtess2', libtess2_dep)
