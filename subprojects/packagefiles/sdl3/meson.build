project(
  'sdl3',
  'c',
  version: '3.4.10',
  license: 'zlib',
  meson_version: '>=1.6',
  default_options: ['c_std=none'],
)

cc = meson.get_compiler('c')

varr = meson.project_version().split('.')

sdl_major = varr[0].to_int()
sdl_minor = varr[1].to_int()
sdl_patch = varr[2].to_int()

cdata = configuration_data()

cdata.set('HAVE_LIBC', 1)
cdata.set('SDL_DEFAULT_ASSERT_LEVEL', true)

sdl_deps = [
  dependency('threads'),
  dependency(
    'dl',
    required: false,
  ),
  cc.find_library(
    'm',
    required: false,
  ),
]

add_project_arguments(
  '-DUSING_GENERATED_CONFIG_H',
  language: 'c',
)
add_project_arguments(
  '-DSDL_BUILD_MAJOR_VERSION=@0@'.format(sdl_major),
  '-DSDL_BUILD_MINOR_VERSION=@0@'.format(sdl_minor),
  '-DSDL_BUILD_MICRO_VERSION=@0@'.format(sdl_patch),
  language: 'c',
)

basic_headers = [
  ['float.h', 'HAVE_FLOAT_H'],
  ['stdarg.h', 'HAVE_STDARG_H'],
  ['stddef.h', 'HAVE_STDDEF_H'],
  ['stdint.h', 'HAVE_STDINT_H'],
  ['iconv.h', 'HAVE_ICONV_H'],
  ['inttypes.h', 'HAVE_INTTYPES_H'],
  ['limits.h', 'HAVE_LIMITS_H'],
  ['malloc.h', 'HAVE_MALLOC_H'],
  ['math.h', 'HAVE_MATH_H'],
  ['memory.h', 'HAVE_MEMORY_H'],
  ['signal.h', 'HAVE_SIGNAL_H'],
  ['stdio.h', 'HAVE_STDIO_H'],
  ['stdlib.h', 'HAVE_STDLIB_H'],
  ['strings.h', 'HAVE_STRINGS_H'],
  ['string.h', 'HAVE_STRING_H'],
  ['sys/types.h', 'HAVE_SYS_TYPES_H'],
  ['wchar.h', 'HAVE_WCHAR_H'],
  ['pthread_np.h', 'HAVE_PTHREAD_NP_H'],
  ['linux/input.h', 'HAVE_LINUX_INPUT_H'],
  ['ddraw.h', 'HAVE_DDRAW_H'],
  ['dsound.h', 'HAVE_DSOUND_H'],
  ['dinput.h', 'HAVE_DINPUT_H'],
  ['signal.h', 'HAVE_SIGTIMEDWAIT'],
]

foreach i : basic_headers
  hname = i[0]
  define = i[1]
  cdata.set10(define, cc.has_header(hname))
endforeach

check_functions = [
  # check token ['HAVE_GCC_ATOMICS']
  # check token ['HAVE_GCC_SYNC_LOCK_TEST_AND_SET']
  # check token ['HAVE_LIBC']
  # check token ['HAVE_LIBC']
  ['HAVE_DLOPEN', 'dlopen', '#include<dlfcn.h>'],
  ['HAVE_MALLOC', 'malloc', '#include<stdlib.h>'],
  ['HAVE_FDATASYNC', 'fdatasync', '#include<unistd.h>'],
  ['HAVE_GETENV', 'getenv', '#include<stdlib.h>'],
  # check token ['HAVE_GETHOSTNAME']
  ['HAVE_SETENV', 'setenv', '#include<stdlib.h>'],
  ['HAVE_PUTENV', 'putenv', '#include<stdlib.h>'],
  ['HAVE_UNSETENV', 'unsetenv', '#include<stdlib.h>'],
  ['HAVE_ABS', 'abs', '#include<stdlib.h>'],
  ['HAVE_BCOPY', 'bcopy', '#include<strings.h>'],
  ['HAVE_MEMSET', 'memset', '#include<string.h>'],
  ['HAVE_MEMCPY', 'memcpy', '#include<string.h>'],
  ['HAVE_MEMMOVE', 'memmove', '#include<string.h>'],
  ['HAVE_MEMCMP', 'memcmp', '#include<string.h>'],
  # check token ['HAVE_WCSLEN']
  # check token ['HAVE_WCSNLEN']
  # check token ['HAVE_WCSLCPY']
  # check token ['HAVE_WCSLCAT']
  # check token ['HAVE_WCSSTR']
  # check token ['HAVE_WCSCMP']
  # check token ['HAVE_WCSNCMP']
  # check token ['HAVE_WCSTOL']
  ['HAVE_STRLEN', 'strlen', '#include<string.h>'],
  ['HAVE_STRNLEN', 'strnlen', '#include<string.h>'],
  ['HAVE_STRLCPY', 'strlcpy', '#include<string.h>'],
  ['HAVE_STRLCAT', 'strlcat', '#include<string.h>'],
  # check token ['HAVE_STRPBRK']
  ['HAVE__STRREV', '_strrev', '#include<string.h>'],
  ['HAVE_INDEX', 'index', '#include<strings.h>'],
  ['HAVE_RINDEX', 'rindex', '#include<strings.h>'],
  ['HAVE_STRCHR', 'strchr', '#include<string.h>'],
  ['HAVE_STRRCHR', 'strrchr', '#include<string.h>'],
  ['HAVE_STRSTR', 'strstr', '#include<string.h>'],
  # check token ['HAVE_STRNSTR']
  ['HAVE_STRTOK_R', 'strtok_r', '#include<string.h>'],
  # check token ['HAVE_ITOA']
  # check token ['HAVE__LTOA']
  # check token ['HAVE__UITOA']
  # check token ['HAVE__ULTOA']
  ['HAVE_STRTOL', 'strtol', '#include<stdlib.h>'],
  ['HAVE_STRTOUL', 'strtoul', '#include<stdlib.h>'],
  # check token ['HAVE__I64TOA']
  # check token ['HAVE__UI64TOA']
  ['HAVE_STRTOLL', 'strtoll', '#include<stdlib.h>'],
  ['HAVE_STRTOULL', 'strtoull', '#include<stdlib.h>'],
  ['HAVE_STRTOD', 'strtod', '#include<stdlib.h>'],
  ['HAVE_ATOI', 'atoi', '#include<stdlib.h>'],
  ['HAVE_ATOF', 'atof', '#include<stdlib.h>'],
  ['HAVE_STRCMP', 'strcmp', '#include<string.h>'],
  ['HAVE_STRNCMP', 'strncmp', '#include<string.h>'],
  ['HAVE_VSSCANF', 'vsscanf', '#include<stdio.h>'],
  ['HAVE_VSNPRINTF', 'vsnprintf', '#include<stdio.h>'],
  ['HAVE_ACOS', 'acos', '#include<math.h>'],
  ['HAVE_ACOSF', 'acosf', '#include<math.h>'],
  ['HAVE_ASIN', 'asin', '#include<math.h>'],
  ['HAVE_ASINF', 'asinf', '#include<math.h>'],
  ['HAVE_ATAN', 'atan', '#include<math.h>'],
  ['HAVE_ATANF', 'atanf', '#include<math.h>'],
  ['HAVE_ATAN2', 'atan2', '#include<math.h>'],
  ['HAVE_ATAN2F', 'atan2f', '#include<math.h>'],
  ['HAVE_CEIL', 'ceil', '#include<math.h>'],
  ['HAVE_CEILF', 'ceilf', '#include<math.h>'],
  ['HAVE_COPYSIGN', 'copysign', '#include<math.h>'],
  ['HAVE_COPYSIGNF', 'copysignf', '#include<math.h>'],
  # check token ['HAVE__COPYSIGN']
  ['HAVE_COS', 'cos', '#include<math.h>'],
  ['HAVE_COSF', 'cosf', '#include<math.h>'],
  ['HAVE_EXP', 'exp', '#include<math.h>'],
  ['HAVE_EXPF', 'expf', '#include<math.h>'],
  ['HAVE_FABS', 'fabs', '#include<math.h>'],
  ['HAVE_FABSF', 'fabsf', '#include<math.h>'],
  ['HAVE_FLOOR', 'floor', '#include<math.h>'],
  ['HAVE_FLOORF', 'floorf', '#include<math.h>'],
  ['HAVE_FMOD', 'fmod', '#include<math.h>'],
  ['HAVE_FMODF', 'fmodf', '#include<math.h>'],
  ['HAVE_ISINF', 'isinf', '#include<math.h>'],
  ['HAVE_ISINFF', 'isinff', '#include<math.h>'],
  # check token ['HAVE_ISINF_FLOAT_MACRO']
  ['HAVE_ISNAN', 'isnan', '#include<math.h>'],
  ['HAVE_ISNANF', 'isnanf', '#include<math.h>'],
  # check token ['HAVE_ISNAN_FLOAT_MACRO']
  ['HAVE_LOG', 'log', '#include<math.h>'],
  ['HAVE_LOGF', 'logf', '#include<math.h>'],
  ['HAVE_LOG10', 'log10', '#include<math.h>'],
  ['HAVE_LOG10F', 'log10f', '#include<math.h>'],
  # check token ['HAVE_LROUND']
  # check token ['HAVE_LROUNDF']
  # check token ['HAVE_MODF']
  # check token ['HAVE_MODFF']
  ['HAVE_POW', 'pow', '#include<math.h>'],
  # check token ['HAVE_POWF']
  ['HAVE_ROUND', 'round', '#include<math.h>'],
  # check token ['HAVE_ROUNDF']
  ['HAVE_SCALBN', 'scalbn', '#include<math.h>'],
  # check token ['HAVE_SCALBNF']
  ['HAVE_SIN', 'sin', '#include<math.h>'],
  ['HAVE_SINF', 'sinf', '#include<math.h>'],
  ['HAVE_SQRT', 'sqrt', '#include<math.h>'],
  ['HAVE_SQRTF', 'sqrtf', '#include<math.h>'],
  # check token ['HAVE_TAN']
  # check token ['HAVE_TANF']
  # check token ['HAVE_TRUNC']
  # check token ['HAVE_TRUNCF']
  # check token ['HAVE__FSEEKI64']
  # check token ['HAVE_FOPEN64']
  ['HAVE_FSEEKO', 'fseeko', '#include<stdio.h>'],
  ['HAVE_FSEEKO64', 'fseeko64', '#include<stdio.h>'],
  # check token ['HAVE_MEMFD_CREATE']
  # check token ['HAVE_POSIX_FALLOCATE']
  ['HAVE_SIGACTION', 'sigaction', '#include<signal.h>'],
  # check token ['HAVE_SA_SIGACTION']
  # check token ['HAVE_ST_MTIM']
  ['HAVE_SETJMP', 'setjmp', '#include<setjmp.h>'],
  ['HAVE_NANOSLEEP', 'nanosleep', '#include<time.h>'],
  ['HAVE_GMTIME_R', 'gmtime_r', '#include<time.h>'],
  # check token ['HAVE_LOCALTIME_R']
  # check token ['HAVE_NL_LANGINFO']
  ['HAVE_SYSCONF', 'sysconf', '#include<unistd.h>'],
  ['HAVE_SYSCTLBYNAME', 'sysctlbyname', '#include<sys/sysctl.h>'],
  ['HAVE_CLOCK_GETTIME', 'clock_gettime', '#include<time.h>'],
  ['HAVE_GETPAGESIZE', 'getpagesize', '#include<unistd.h>'],
  # check token ['HAVE_ICONV']
  ['HAVE_PTHREAD_SETNAME_NP', 'pthread_setname_np', '#include<pthread.h>'],
  ['HAVE_PTHREAD_SET_NAME_NP', 'pthread_set_name_np', '#include<pthread.h>'],
  ['HAVE_SEM_TIMEDWAIT', 'sem_timedwait', '#include<semaphore.h>'],
  # check token ['HAVE_GETAUXVAL']
  # check token ['HAVE_ELF_AUX_INFO']
  ['HAVE_POLL', 'poll', '#include<poll.h>'],
  # check token ['HAVE__EXIT']
  # check token ['HAVE_FCITX']
  ['HAVE_INOTIFY_INIT1', 'inotify_init1', '#include<sys/inotify.h>'],
  # check token ['HAVE_INOTIFY']
  # check token ['HAVE_LIBUSB']
  # check token ['HAVE_O_CLOEXEC']
]

foreach f : check_functions
  if cc.has_function(
    f.get(1),
    prefix: f.get(2),
    dependencies: [sdl_deps],
  )
    cdata.set(f.get(0), 1)
  endif
endforeach

sources = []

cdata.set('SDL_XKBCOMMON_VERSION_MAJOR', 0)
cdata.set('SDL_XKBCOMMON_VERSION_MINOR', 0)
cdata.set('SDL_XKBCOMMON_VERSION_PATCH', 0)
cdata.set('SDL_LIBDECOR_VERSION_MAJOR', 0)
cdata.set('SDL_LIBDECOR_VERSION_MINOR', 0)
cdata.set('SDL_LIBDECOR_VERSION_PATCH', 0)

if host_machine.system() == 'darwin'
  add_languages(
    'objc',
    native: false,
  )
  add_project_arguments(
    '-fobjc-arc',
    language: 'objc',
  )
endif

if host_machine.system() == 'linux'

  ## some of these checks could be done via have include
  ## but it is done this way to be consistent from what CMake does
  ## additionally these values are not defined in the config file

  # check Kronos EGL support
  have_opengl_egl_code = '''
    #define EGL_API_FB
    #define MESA_EGL_NO_X11_HEADERS
    #define EGL_NO_X11
    #include <EGL/egl.h>
    #include <EGL/eglext.h>
    int main (int argc, char** argv) { return 0; }
  '''
  have_opengl_egl = cc.compiles(
    have_opengl_egl_code,
    name: 'have OpenGL EGL support',
  )
  # check OpenGL GLX support
  have_opengl_glx_code = '''
    #include <GL/glx.h>
    int main(int argc, char** argv) { return 0; }
  '''
  have_opengl_glx = cc.compiles(
    have_opengl_glx_code,
    name: 'have OpenGL GLX support',
  )

  # check OpenGL support
  have_opengl_code = '''
    #include <GL/gl.h>
    #include <GL/glext.h>
    int main(int argc, char** argv) { return 0; }
  '''
  have_opengl = cc.compiles(
    have_opengl_code,
    name: 'have OpenGL support',
  )


  # ideally this should have a check for compile like the cmake configuration, see sdlchecks.cmake
  cdata.set('SDL_PLATFORM_LINUX', 1)
  cdata.set('SDL_THREAD_PTHREAD', 1)
  cdata.set('HAVE_STRLEN', 1)
  cdata.set('SDL_LOADSO_DLOPEN', 1)
  cdata.set('SDL_PROCESS_POSIX', 1)
  cdata.set('SDL_SENSOR_DUMMY', 1)
  cdata.set('SDL_POWER_LINUX', 1)
  cdata.set('SDL_INPUT_LINUXEV', 1)
  if have_opengl_egl
    cdata.set('SDL_VIDEO_OPENGL_EGL', 1)
  endif
  if have_opengl_glx
    cdata.set('SDL_VIDEO_OPENGL_GLX', 1)
  endif
  if have_opengl
    cdata.set('SDL_VIDEO_OPENGL', 1)
    cdata.set('SDL_VIDEO_RENDER_OGL', 1)
  endif
  cdata.set('SDL_GPU_VULKAN', 1)
  cdata.set('SDL_VIDEO_VULKAN', 1)
  cdata.set('SDL_VIDEO_RENDER_VULKAN', 1)
  cdata.set('SDL_TIMER_UNIX', 1)
  sdl_deps += dependency('libudev')
  dbus_dep = dependency('dbus-1')
  sdl_deps += dbus_dep
  # SDL's PipeWire backend already supports runtime dispatch. Keep the
  # development dependency for its headers, but do not make every Linux
  # client require PipeWire before SDL can try its other audio backends.
  pipewire_dep = dependency('libpipewire-0.3')
  sdl_deps += pipewire_dep.partial_dependency(
    compile_args: true,
    includes: true,
  )
  cdata.set_quoted('SDL_AUDIO_DRIVER_PIPEWIRE_DYNAMIC', 'libpipewire-0.3.so.0')
  # Match SDL's supported Linux release configuration: compile against the
  # Wayland interfaces, but load the compositor libraries by SONAME at runtime.
  # This keeps native Wayland available without turning libdecor into a hard
  # package dependency and makes SDL's ALLOW/PREFER_LIBDECOR hints effective.
  wayland_client_dep = dependency('wayland-client', version: '>=1.18')
  wayland_egl_dep = dependency('wayland-egl')
  wayland_cursor_dep = dependency('wayland-cursor')
  xkbcommon_dep = dependency('xkbcommon', version: '>=0.5.0')
  egl_dep = dependency('egl')
  foreach wayland_compile_dep : [
    wayland_client_dep,
    wayland_egl_dep,
    wayland_cursor_dep,
    xkbcommon_dep,
    egl_dep,
  ]
    sdl_deps += wayland_compile_dep.partial_dependency(
      compile_args: true,
      includes: true,
    )
  endforeach
  cdata.set_quoted('SDL_VIDEO_DRIVER_WAYLAND_DYNAMIC', 'libwayland-client.so.0')
  cdata.set_quoted('SDL_VIDEO_DRIVER_WAYLAND_DYNAMIC_EGL', 'libwayland-egl.so.1')
  cdata.set_quoted('SDL_VIDEO_DRIVER_WAYLAND_DYNAMIC_CURSOR', 'libwayland-cursor.so.0')
  cdata.set_quoted('SDL_VIDEO_DRIVER_WAYLAND_DYNAMIC_XKBCOMMON', 'libxkbcommon.so.0')

  xkbcommon_version = xkbcommon_dep.version().split('.')
  if xkbcommon_version.length() >= 3
    cdata.set('SDL_XKBCOMMON_VERSION_MAJOR', xkbcommon_version[0].to_int())
    cdata.set('SDL_XKBCOMMON_VERSION_MINOR', xkbcommon_version[1].to_int())
    cdata.set('SDL_XKBCOMMON_VERSION_PATCH', xkbcommon_version[2].to_int())
  else
    cdata.set('SDL_XKBCOMMON_VERSION_MAJOR', 0)
    cdata.set('SDL_XKBCOMMON_VERSION_MINOR', 5)
    cdata.set('SDL_XKBCOMMON_VERSION_PATCH', 0)
  endif

  libdecor_dep = dependency('libdecor-0', required: false)
  if libdecor_dep.found()
    sdl_deps += libdecor_dep.partial_dependency(
      compile_args: true,
      includes: true,
    )
    cdata.set('HAVE_LIBDECOR_H', 1)
    cdata.set_quoted('SDL_VIDEO_DRIVER_WAYLAND_DYNAMIC_LIBDECOR', 'libdecor-0.so.0')
    libdecor_version = libdecor_dep.version().split('.')
    if libdecor_version.length() >= 3
      cdata.set('SDL_LIBDECOR_VERSION_MAJOR', libdecor_version[0].to_int())
      cdata.set('SDL_LIBDECOR_VERSION_MINOR', libdecor_version[1].to_int())
      cdata.set('SDL_LIBDECOR_VERSION_PATCH', libdecor_version[2].to_int())
    else
      cdata.set('SDL_LIBDECOR_VERSION_MAJOR', 0)
      cdata.set('SDL_LIBDECOR_VERSION_MINOR', 1)
      cdata.set('SDL_LIBDECOR_VERSION_PATCH', 0)
    endif
  endif
  cdata.set('HAVE_DBUS_DBUS_H', 1)
  # Fcitx uses SDL's dynamically-dispatched DBus layer and does not require a
  # separate client library. IBus additionally needs its protocol headers.
  cdata.set('HAVE_FCITX', 1)
  cdata.set('SDL_USE_IME', 1)
  ibus_dep = dependency('ibus-1.0', required: false)
  if ibus_dep.found() and cc.has_header('sys/inotify.h')
    sdl_deps += ibus_dep.partial_dependency(
      compile_args: true,
      includes: true,
    )
    cdata.set('HAVE_IBUS_IBUS_H', 1)
  endif
  wayland_scanner = find_program('wayland-scanner', native: true)
  subdir('wayland-protocols')
elif host_machine.system() == 'windows'

  # XInput(legacy) is provided by the Microsoft DirectX SDK and requires windows header include
  have_xinput_header_code = '''
    #include <windows.h>
    #include <xinput.h>
    int main(int argc, char **argv) { }
  '''
  have_xinput_header = cc.compiles(
    have_xinput_header_code,
    name: 'have XInput support',
  )

  # Microsoft Windows gaming input header is provided by the Windows SDK (winrt)
  have_windows_gaming_input_header_code = '''
    #define COBJMACROS
    #include <windows.gaming.input.h>
    static __x_ABI_CWindows_CGaming_CInput_CIGamepadStatics2 *s2;
    int main(int argc, char **argv) {  }
  '''
  have_windows_gaming_input_header = cc.compiles(
    have_windows_gaming_input_header_code,
    name: 'have Windows Gaming Input support',
  )

  # Microsoft Windows GameInput is provided via the Microsoft Game Development Kit (GDK)
  have_gameinput_header_code = '''
    #include <stdbool.h>
    #define COBJMACROS
    #include <gameinput.h>
    int main(int argc, char **argv) {  }
  '''
  have_gameinput_header = cc.compiles(
    have_gameinput_header_code,
    name: 'have GameInput support',
  )
  add_languages(
    'cpp',
    native: false,
  )


  cdata.set('SDL_PLATFORM_WIN32', 1)
  cdata.set('SDL_THREAD_WINDOWS', 1)
  cdata.set('SDL_THREAD_GENERIC_COND_SUFFIX', 1)
  cdata.set('SDL_THREAD_GENERIC_RWLOCK_SUFFIX', 1)
  cdata.set('SDL_LOADSO_WINDOWS', 1)
  cdata.set('SDL_PROCESS_WINDOWS', 1)
  cdata.set('SDL_SENSOR_WINDOWS', 1)
  cdata.set('SDL_POWER_WINDOWS', 1)
  cdata.set('SDL_VIDEO_RENDER_D3D12', 1)
  cdata.set('SDL_GPU_VULKAN', 1)
  cdata.set('SDL_GPU_D3D12', 1)
  cdata.set('SDL_VIDEO_VULKAN', 1)
  cdata.set('SDL_VIDEO_RENDER_VULKAN', 1)
  cdata.set('SDL_VIDEO_OPENGL', 1)
  cdata.set('SDL_VIDEO_OPENGL_WGL', 1)
  cdata.set('SDL_VIDEO_RENDER_OGL', 1)

  if have_xinput_header
    cdata.set('SDL_JOYSTICK_XINPUT', 1)
    cdata.set('HAVE_XINPUT', true)
  endif
  if have_windows_gaming_input_header
    cdata.set('SDL_JOYSTICK_WGI', 1)
  endif
  if have_gameinput_header
    cdata.set('HAVE_GAMEINPUT_H', 1)
    cdata.set('SDL_JOYSTICK_GAMEINPUT', 1)
  endif

  cdata.set('SDL_TIMER_WINDOWS', 1)
  sdl_deps += cc.find_library('version')
  sdl_deps += cc.find_library('imm32')
  sdl_deps += cc.find_library('setupapi')
  sdl_deps += cc.find_library('winmm')
  # no opengl32: SDL3 loads the GL driver dynamically (WIN_GL_LoadLibrary);
  # a static import lib here would drag GL back into openQ4's GL-free
  # dedicated/module-only targets (verified: libSDL3.a has no wgl/gl imports)
elif host_machine.system() == 'darwin'
  cdata.set('SDL_PLATFORM_APPLE', 1)
  cdata.set('SDL_THREAD_PTHREAD', 1)
  cdata.set('SDL_THREAD_PTHREAD_RECURSIVE_MUTEX', 1)
  cdata.set('SDL_LOADSO_DLOPEN', 1)
  cdata.set('SDL_PROCESS_POSIX', 1)
  cdata.set('SDL_FILESYSTEM_COCOA', 1)
  cdata.set('SDL_VIDEO_DRIVER_COCOA', 1)
  cdata.set('SDL_VIDEO_OPENGL_CGL', 1)
  cdata.set('SDL_VIDEO_OPENGL', 1)
  # macOS ships no OpenGL ES: these drive SDL_egl.c and SDL_cocoaopengles.m,
  # which dlopen an ANGLE (GLES-on-Metal) libEGL/libGLESv2 at runtime. Both
  # files are already in the source lists but compile to empty translation
  # units without these defines. Nothing is linked at build time, so the CGL
  # desktop-GL path and GL-free targets are unaffected.
  cdata.set('SDL_VIDEO_OPENGL_EGL', 1)
  cdata.set('SDL_VIDEO_OPENGL_ES2', 1)
  cdata.set('SDL_FSOPS_POSIX', 1)
  cdata.set('SDL_SENSOR_DUMMY', 1)
  cdata.set('SDL_POWER_MACOSX', 1)
  cdata.set('SDL_VIDEO_RENDER_GPU', 1)
  cdata.set('SDL_GPU_VULKAN', 1)
  cdata.set('SDL_GPU_METAL', 1)
  cdata.set('SDL_VIDEO_VULKAN', 1)
  cdata.set('SDL_VIDEO_METAL', 1)
  # The metal render sources are compiled via src/render/meson.build but stay
  # empty translation units unless the driver define is set; the macOS Metal
  # bridge selects this driver by name for splash/console renderers.
  cdata.set('SDL_VIDEO_RENDER_METAL', 1)
  cdata.set('SDL_VIDEO_RENDER_VULKAN', 1)
  cdata.set('SDL_TIMER_UNIX', 1)
  sdl_deps += dependency(
    'appleframeworks',
    modules: [
      'AudioToolbox',
      'CoreFoundation',
      'Cocoa',
      'CoreAudio',
      'CoreGraphics',
      'CoreHaptics',
      'GameController',
      'ForceFeedback',
      'IOKit',
      'Foundation',
      'QuartzCore',
      'Metal',
      'Carbon',
      'UniformTypeIdentifiers',
    ],
  )
else
  error('Platform not yet supported.')
endif

if get_option('default_library') == 'both' and cc.get_argument_syntax() == 'msvc'
  error('default_library=both not supported when compiling with MSVC')
endif

subdir('src')

configure_file(
  input: 'include/build_config/SDL_build_config.h.cmake',
  output: 'SDL_build_config.h',
  configuration: cdata,
  format: 'cmake',
)

if get_option('default_library') == 'shared'
  extra_args = ['-DDLL_EXPORT']
else
  extra_args = []
endif

sdl_include_dirs = ['include', 'src']
if host_machine.system() == 'darwin'
  # macOS ships no system EGL/GLES headers, so the ES2/EGL paths enabled above
  # compile against SDL's own bundled Khronos copies. Scoped to darwin so it
  # cannot shadow the system EGL headers the Linux Wayland/X11 build uses.
  # Vulkan is unaffected: SDL includes it as "./khronos/vulkan/vulkan.h".
  sdl_include_dirs += 'src/video/khronos'
endif

sdl_lib = library(
  'SDL3',
  sources,
  include_directories: sdl_include_dirs,
  c_args: extra_args,
  dependencies: sdl_deps,
  install: true,
)

sdl3_dep = declare_dependency(
  include_directories: 'include',
  link_with: sdl_lib,
  # Only the patched Windows native status paths are currently qualified.
  # The same version or another native backend does not imply this contract.
  compile_args: host_machine.system() == 'windows' ? ['-DOPENQ4_SDL3_CHECKED_CLIPBOARD=1', '-DOPENQ4_SDL3_TEXT_PROVENANCE=1'] : [],
)

meson.override_dependency('sdl3', sdl3_dep)

