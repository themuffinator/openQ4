# openQ4 integration for the pinned Khronos OpenXR SDK loader. Only the loader
# library is built, statically, for the client executable; the API layers,
# samples, tests and conformance suite are not engine content. The loader keeps
# the runtime-negotiation and manifest-discovery behaviour of the upstream CMake
# build: XR_RUNTIME_JSON, the Windows ActiveRuntime registry value and the XDG
# active_runtime.json search all work unchanged. On Android (Meta Quest and
# other standalone headsets) it asks the Khronos runtime broker through JNI,
# as upstream's Android loader does; there the application initializes it with
# xrInitializeLoaderKHR, and loader properties replace environment variables.
project('openxr', ['c', 'cpp'], version: '1.1.65', license: 'Apache-2.0',
  meson_version: '>=1.6', default_options: ['cpp_std=c++17', 'warning_level=0'])

cpp = meson.get_compiler('cpp')
host_system = host_machine.system()

openxr_includes = include_directories('include')
openxr_headers_dep = declare_dependency(include_directories: openxr_includes)

common_config = configuration_data()
if host_system != 'windows'
  common_config.set('HAVE_SECURE_GETENV',
    cpp.has_function('secure_getenv', prefix: '#define _GNU_SOURCE\n#include <stdlib.h>'))
  common_config.set('HAVE___SECURE_GETENV',
    cpp.has_function('__secure_getenv', prefix: '#define _GNU_SOURCE\n#include <stdlib.h>'))
endif
configure_file(input: 'src/common_config.h.in', output: 'common_config.h',
  format: 'cmake', configuration: common_config)

# The upstream static-loader configuration: no XRAPI_DLL_EXPORT, exception
# handling on (jsoncpp reports malformed manifests by throwing).
loader_args = ['-DOPENXR_HAVE_COMMON_CONFIG']
loader_deps = [dependency('jsoncpp'), dependency('threads')]

if host_system == 'windows'
  loader_args += ['-DXR_OS_WINDOWS', '-DNOMINMAX', '-DXR_USE_PLATFORM_WIN32',
    '-D_CRT_SECURE_NO_WARNINGS']
  loader_deps += cpp.find_library('advapi32')
elif host_system == 'linux'
  loader_args += ['-DXR_OS_LINUX',
    '-DFALLBACK_CONFIG_DIRS="/etc/xdg"',
    '-DFALLBACK_DATA_DIRS="/usr/local/share:/usr/share"',
    '-DSYSCONFDIR="/etc"']
  loader_deps += cpp.find_library('dl', required: false)
elif host_system == 'darwin'
  loader_args += ['-DXR_OS_APPLE', '-DXR_USE_PLATFORM_MACOS']
elif host_system == 'android'
  # upstream's Android loader: XR_OS_ANDROID alone (not Linux), the runtime
  # broker queried through jnipp and the android.* JNI wrappers
  loader_args += ['-DXR_OS_ANDROID', '-DXR_USE_PLATFORM_ANDROID']
  loader_deps += [cpp.find_library('log'), cpp.find_library('android')]
else
  error('The openQ4 OpenXR loader build supports Windows, Linux, macOS and Android hosts only.')
endif

loader_platform_sources = []
loader_platform_includes = []
if host_system == 'android'
  loader_platform_sources = files(
    'src/loader/android_utilities.cpp',
    'src/external/android-jni-wrappers/wrap/android.content.cpp',
    'src/external/android-jni-wrappers/wrap/android.database.cpp',
    'src/external/android-jni-wrappers/wrap/android.net.cpp',
    'src/external/jnipp/jnipp.cpp')
  loader_platform_includes = include_directories('src/external/jnipp', 'src/external/android-jni-wrappers')
endif

openxr_loader_library = static_library('openxr_loader', loader_platform_sources + files(
  'src/common/filesystem_utils.cpp',
  'src/common/object_info.cpp',
  'src/loader/api_layer_interface.cpp',
  'src/loader/loader_core.cpp',
  'src/loader/loader_init_data.cpp',
  'src/loader/loader_instance.cpp',
  'src/loader/loader_logger.cpp',
  'src/loader/loader_logger_recorders.cpp',
  'src/loader/loader_properties.cpp',
  'src/loader/manifest_file.cpp',
  'src/loader/runtime_interface.cpp',
  'src/loader/xr_generated_loader.cpp',
  'src/xr_generated_dispatch_table_core.c'),
  c_args: loader_args, cpp_args: loader_args,
  # '.' reaches the configured common_config.h in the build directory
  include_directories: [openxr_includes, include_directories('.', 'src', 'src/common', 'src/loader')]
    + loader_platform_includes,
  dependencies: loader_deps, pic: true, install: false)

openxr_loader_dep = declare_dependency(link_with: openxr_loader_library,
  include_directories: openxr_includes, dependencies: loader_deps)
meson.override_dependency('openxr_loader', openxr_loader_dep)
