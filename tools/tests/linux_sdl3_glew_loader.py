#!/usr/bin/env python3
"""Regression checks for the Linux SDL3 GLEW loader path."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def require_before(haystack: str, first: str, second: str, context: str) -> None:
    first_index = haystack.find(first)
    second_index = haystack.find(second)
    if first_index == -1 or second_index == -1 or first_index >= second_index:
        raise AssertionError(f"Expected {first!r} before {second!r} in {context}")


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start == -1:
        raise AssertionError(f"Missing function signature {signature!r}")

    depth = 0
    for index in range(start, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]

    raise AssertionError(f"Could not find end of function {signature!r}")


def validate_meson_selection() -> None:
    root_meson = read("meson.build")
    glew_meson = read("subprojects/glew/meson.build")
    glew_options = read("subprojects/glew/meson_options.txt")

    require(root_meson, "glew_default_options = fallback_opt", "root Meson GLEW defaults")
    require(root_meson, "if host_system == 'linux' and use_sdl3_backend", "root Meson Linux SDL3 branch")
    require(root_meson, "glew_default_options += ['openq4_sdl3_loader=true']", "root Meson GLEW SDL3 loader option")
    require(root_meson, "glew_sp = subproject('glew', default_options: glew_default_options)", "root Meson bundled GLEW selection")
    require(root_meson, "glew_dep = glew_sp.get_variable('glew_dep')", "root Meson bundled GLEW dependency")
    require_before(root_meson, "glew_default_options += ['openq4_sdl3_loader=true']", "glew_sp = subproject('glew'", "root Meson GLEW option order")

    require(glew_options, "'openq4_sdl3_loader'", "GLEW subproject option")
    require(glew_options, "Use openQ4 SDL3 OpenGL proc-address lookup", "GLEW subproject option description")
    require(glew_meson, "glew_c_args = ['-DGLEW_NO_GLU']", "GLEW subproject c_args")
    require(glew_meson, "if get_option('openq4_sdl3_loader')", "GLEW subproject SDL3 option")
    require(glew_meson, "glew_c_args += ['-DOPENQ4_GLEW_SDL3_LOADER']", "GLEW subproject SDL3 define")
    require(glew_meson, "c_args: glew_c_args", "GLEW subproject c_args use")


def validate_glew_loader_hook() -> None:
    for relative_path in ("subprojects/glew/src/glew.c", "src/external/glew/glew.c"):
        source = read(relative_path)
        glew_init = function_body(source, "GLenum GLEWAPIENTRY glewInit (void)")

        require(source, "typedef void ( *openQ4GlewProcAddress_t ) (void);", f"{relative_path} openQ4 resolver typedef")
        require(source, "extern openQ4GlewProcAddress_t OpenQ4_GlewGetProcAddress(const unsigned char *name);", f"{relative_path} openQ4 resolver declaration")
        require(source, "#elif defined(OPENQ4_GLEW_SDL3_LOADER)\n#  define glewGetProcAddress(name) OpenQ4_GlewGetProcAddress(name)", f"{relative_path} openQ4 resolver macro")
        require(glew_init, "#elif defined(OPENQ4_GLEW_SDL3_LOADER)\n  return r;", f"{relative_path} GLXEW skip for SDL3 loader")
        require_before(glew_init, "#elif defined(OPENQ4_GLEW_SDL3_LOADER)\n  return r;", "#elif defined(_WIN32)", f"{relative_path} SDL3 loader avoids GLXEW init")


def validate_sdl3_backend_hook() -> None:
    # post-B5b the GLEW hook lives in the renderer-owned context seam TU and
    # resolves through the window services rather than direct SDL calls
    source = read("src/renderer/OpenGL/gl_ContextSDL3.cpp")
    hook = function_body(source, 'extern "C" openQ4GlewProcAddress_t OpenQ4_GlewGetProcAddress(const unsigned char *name) {')
    extension_pointer = function_body(source, "void *GLimp_ExtensionPointer(const char *name) {")

    require(source, "#if defined(OPENQ4_SDL3_LINUX_HOST)", "SDL3 Linux-only GLEW hook guard")
    require(source, "typedef void ( *openQ4GlewProcAddress_t ) (void);", "SDL3 GLEW hook typedef")
    require(hook, "if (name == NULL || name[0] == '\\0') {", "SDL3 GLEW hook null-name guard")
    require(hook, 'SDL3_EnsureGLContextCurrent("GLEW proc lookup")', "SDL3 GLEW hook current-context guard")
    require(hook, "s_glWindowServices->GetGLProcAddress(reinterpret_cast<const char *>(name))", "SDL3 GLEW hook resolver")
    require(extension_pointer, "if (name == NULL || name[0] == '\\0') {", "SDL3 extension pointer null-name guard")
    require(extension_pointer, 'SDL3_EnsureGLContextCurrent("extension proc lookup")', "SDL3 extension pointer current-context guard")

    # the Windows dedicated GL-free link relies on the SDL3-loader define
    # compiling out both wglew halves (declarations and implementation)
    for relative_path in ("subprojects/glew/src/glew.c", "src/external/glew/glew.c"):
        glew_source = read(relative_path)
        require(glew_source, "#elif defined(_WIN32) && !defined(OPENQ4_GLEW_SDL3_LOADER)", f"{relative_path} wglew gating")


def validate_renderer_context_guard(source: str | None = None) -> None:
    if source is None:
        source = read("src/renderer/RenderSystem_init.cpp")
    header = read("src/renderer/tr_local.h")
    extensions = function_body(source, "static bool R_InitOpenGLExtensionLoader(")
    portable = function_body(source, "static bool R_CheckPortableExtensions(")
    init = function_body(source, "static bool R_CreateOpenGLContext(")
    internal = function_body(source, "static bool R_InitOpenGLInternal(")
    wrapper = function_body(source, "void R_InitOpenGL( void ) {")

    # Follow the shared production route; checking detached helpers would miss
    # a startup path that stopped invoking a context/loader guard.
    require(wrapper, 'if ( !R_InitOpenGLInternal( true, false, error, sizeof( error ) ) ) common->FatalError( "%s", error );', "legacy startup failure policy")
    require(internal, "if ( !R_CreateOpenGLContext( legacyPolicy, forceWindow, error, errorSize ) ) return false;", "context failure propagation")
    require(internal, "if ( !R_CheckPortableExtensions( legacyPolicy, error, errorSize ) ) return false;", "extension failure propagation")
    require_before(internal, "R_CreateOpenGLContext(", "R_CheckPortableExtensions(", "context creation before extension loading")
    require(portable, "if ( !R_InitOpenGLExtensionLoader( error, errorSize ) ) return false;", "loader failure propagation")
    require_before(portable, "R_InitOpenGLExtensionLoader(", "GLCapabilityProbe_Build(", "loader before capability probing")

    require(header, "bool\t\tGLimp_EnsureActiveContext( const char *operation );", "renderer GL context contract declaration")
    require_before(extensions, 'GLimp_EnsureActiveContext( "GLEW initialization" )', 'common->Printf("Init Glew...', "renderer GLEW context activation")
    require(extensions, "Unable to make OpenGL context current for GLEW initialization", "renderer GLEW context diagnostic")
    require(extensions, "const GLenum glewResult = glewInit();", "renderer GLEW result capture")
    require(extensions, "const GLubyte *glewError = glewGetErrorString( glewResult );", "renderer GLEW error string guard")
    require(extensions, "Failed to init GLEW: %s", "renderer GLEW error diagnostics")
    require(init, "// get our config strings", "renderer GL string query block")
    require_before(init, 'GLimp_EnsureActiveContext( "OpenGL startup string query" )', "glConfig.vendor_string", "renderer GL string context activation")
    require(init, "Unable to make OpenGL context current after window creation", "renderer startup context diagnostic")
    require(init, "OpenGL context did not report GL_VERSION after window creation", "renderer current-context diagnostic")


def validate_context_mutation_sensitivity() -> None:
    source = read("src/renderer/RenderSystem_init.cpp")
    for anchor in (
        'GLimp_EnsureActiveContext( "GLEW initialization" )',
        'GLimp_EnsureActiveContext( "OpenGL startup string query" )',
        "if ( !R_CreateOpenGLContext( legacyPolicy, forceWindow, error, errorSize ) ) return false;",
        "if ( !R_CheckPortableExtensions( legacyPolicy, error, errorSize ) ) return false;",
        "if ( !R_InitOpenGLExtensionLoader( error, errorSize ) ) return false;",
    ):
        if source.count(anchor) != 1:
            raise AssertionError(f"Context mutation anchor is not unique: {anchor!r}")
        try:
            validate_renderer_context_guard(source.replace(anchor, "true", 1))
        except AssertionError:
            continue
        raise AssertionError(f"Context regression escaped detection: {anchor!r}")


def validate_platform_context_contract() -> None:
    linux_dedicated = read("src/sys/linux/dedicated.cpp")
    backends = {
        # post-B5b the SDL3 context half lives in the renderer-owned seam TU
        "SDL3 backend": read("src/renderer/OpenGL/gl_ContextSDL3.cpp"),
        "native GLX backend": read("src/sys/linux/glimp.cpp"),
        "native WGL backend": read("src/sys/win32/win_glimp.cpp"),
        "native macOS backend": read("src/sys/osx/macosx_glimp.mm"),
        "Linux dedicated GL stub": read("src/sys/stub/stub_gl.cpp"),
    }

    for name, source in backends.items():
        require(source, "GLimp_EnsureActiveContext", name)

    require(backends["SDL3 backend"], "return SDL3_EnsureGLContextCurrent(operation);", "SDL3 context contract")
    require(backends["native GLX backend"], "glXMakeCurrent( dpy, win, ctx )", "native GLX context contract")
    require(backends["native WGL backend"], "qwglMakeCurrent( win32.hDC, win32.hGLRC )", "native WGL context contract")
    require(backends["native macOS backend"], "return OSX_EnsureGLContextCurrent( operation );", "native macOS context contract")
    require(linux_dedicated, "Sys_GetDesktopResolution", "Linux dedicated desktop-resolution stub")
    require(
        backends["Linux dedicated GL stub"],
        "OpenQ4_GlewGetProcAddress",
        "Linux dedicated GLEW resolver stub",
    )


def validate_ci_smoke() -> None:
    push = read(".github/workflows/push-verification.yml")
    commit = read(".github/workflows/commit-validation.yml")
    runner = read("tools/validation/openq4_validate.py")

    require(push, "tools/tests/linux_sdl3_glew_loader.py", "push verification workflow")
    require(push, "python tools/tests/linux_sdl3_glew_loader.py", "push script-smoke regression check")
    require(commit, "tools/tests/linux_sdl3_glew_loader.py", "commit validation workflow")
    require(commit, "python tools/tests/linux_sdl3_glew_loader.py", "commit script-smoke regression check")
    require(runner, "linux_sdl3_glew_loader.py", "validation Python tests")


def main() -> None:
    validate_meson_selection()
    validate_glew_loader_hook()
    validate_sdl3_backend_hook()
    validate_renderer_context_guard()
    validate_context_mutation_sensitivity()
    validate_platform_context_contract()
    validate_ci_smoke()
    print("linux_sdl3_glew_loader: ok")


if __name__ == "__main__":
    main()
