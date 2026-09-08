# Use JsonCpp under its MIT option. Only the parser/value/writer library is
# needed; upstream tests, tools and installation targets are not engine content.
project('jsoncpp', 'cpp', version: '1.9.6', license: 'MIT',
  meson_version: '>=1.6', default_options: ['cpp_std=c++20', 'warning_level=1'])
jsoncpp_includes = include_directories('include')
jsoncpp_library = static_library('jsoncpp', files(
  'src/lib_json/json_reader.cpp',
  'src/lib_json/json_value.cpp',
  'src/lib_json/json_writer.cpp'),
  include_directories: jsoncpp_includes, pic: true, install: false)
jsoncpp_dep = declare_dependency(link_with: jsoncpp_library,
  include_directories: jsoncpp_includes)
meson.override_dependency('jsoncpp', jsoncpp_dep)
