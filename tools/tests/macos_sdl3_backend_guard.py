#!/usr/bin/env python3
"""Regression checks for default macOS SDL3 backend crash guards."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


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


def require_before(haystack: str, first: str, second: str, context: str) -> None:
    first_index = haystack.find(first)
    second_index = haystack.find(second)
    if first_index == -1 or second_index == -1 or first_index >= second_index:
        raise AssertionError(f"Expected {first!r} before {second!r} in {context}")


def validate_macos_sdl3_source_selection() -> None:
    wrapper = read("src/sys/osx/macosx_sdl3.cpp")
    main = read("src/sys/osx/macosx_sdl3_main.cpp")
    sources = read("tools/build/meson_sources.py")
    display = function_body(wrapper, "CGDirectDisplayID Sys_DisplayToUse(void) {")
    openq4_main = function_body(main, "static int SDLCALL OpenQ4_Main(int argc, char **argv) {")
    entrypoint = function_body(main, "int main(int argc, char **argv) {")

    require(wrapper, "#define OPENQ4_SDL3_DARWIN_HOST 1", "macOS SDL3 wrapper host marker")
    require(wrapper, '#include "../sdl3/sdl3_backend.cpp"', "macOS SDL3 wrapper shared backend")
    require(sources, "SDL3_DARWIN_SOURCES", "macOS SDL3 source manifest")
    require(sources, '"sys/osx/macosx_sdl3.cpp"', "macOS SDL3 source manifest")
    require(sources, '"sys/osx/macosx_sdl3_main.cpp"', "macOS SDL3 source manifest")

    for token in (
        "CGGetActiveDisplayList",
        "return displays[requestedScreen];",
        "const CGDirectDisplayID mainDisplay = CGMainDisplayID();",
        "return displays[0];",
        "return 0;",
    ):
        require(display, token, "macOS SDL3 display fallback")

    require(openq4_main, "argc > 1 && argv != NULL", "macOS SDL3 app argument guard")
    require(entrypoint, 'static char emptyArg0[] = "openQ4";', "macOS SDL3 app argument guard")
    require(entrypoint, "char *emptyArgv[] = { emptyArg0, NULL };", "macOS SDL3 app argument guard")
    require(entrypoint, "if (argc <= 0 || argv == NULL)", "macOS SDL3 app argument guard")
    require(entrypoint, "argc = 1;", "macOS SDL3 app argument guard")
    require(entrypoint, "argv = emptyArgv;", "macOS SDL3 app argument guard")
    require(entrypoint, "SDL_RunApp(argc, argv, OpenQ4_Main, NULL)", "macOS SDL3 app argument guard")


def validate_macos_process_handoff_guards() -> None:
    source = read("src/sys/osx/macosx_misc.mm")
    start_process = function_body(source, "static bool OSX_StartProcessArgs( char *const argv[], bool dofork ) {")
    drop_environment = function_body(source, "static bool OSX_ShouldDropProcessEnvironmentEntry( const char *entry ) {")
    filtered_environment = function_body(source, "static char **OSX_CreateFilteredProcessEnvironment() {")
    do_start_process = function_body(source, "void Sys_DoStartProcess( const char *exeName, bool dofork ) {")
    allowed_url = function_body(source, "static bool OSX_IsAllowedURL( NSURL *url ) {")
    open_url = function_body(source, "void idSysLocal::OpenURL( const char *url, bool doexit ) {")

    require(source, "static bool OSX_IsAbsolutePath( const char *path )", "macOS process handoff absolute-path helper")
    require(source, "static bool OSX_EnsureOwnerExecutable( const char *path )", "macOS process handoff executable-bit helper")
    require(start_process, "argv == NULL || argv[0] == NULL || argv[0][0] == '\\0'", "macOS process handoff argument guard")
    require(start_process, "!OSX_IsAbsolutePath( argv[0] ) || !OSX_IsRegularFile( argv[0] )", "macOS process handoff absolute executable guard")
    require(start_process, "if ( !OSX_EnsureOwnerExecutable( argv[0] ) )", "macOS process handoff executable-bit verification")
    require(start_process, "executable bit is not set and could not be applied", "macOS process handoff executable-bit diagnostic")
    require(start_process, "char **environment = OSX_CreateFilteredProcessEnvironment();", "macOS process handoff filtered environment")
    require(start_process, "process environment allocation failed", "macOS process handoff filtered environment")
    require(start_process, "execve( argv[0], argv, environment )", "macOS process handoff no PATH exec")
    require(start_process, "posix_spawn( &childPid, argv[0], NULL, NULL, argv, environment )", "macOS process handoff no PATH spawn")
    reject(start_process, "execvp(", "macOS process handoff PATH exec")
    reject(start_process, "posix_spawnp(", "macOS process handoff PATH spawn")
    reject(start_process, "execv( argv[0], argv )", "macOS process handoff inherited environment exec")
    require(start_process, "free( environment );", "macOS process handoff filtered environment cleanup")
    require(filtered_environment, "char ***environmentPointer = _NSGetEnviron();", "macOS process handoff environment guard")
    require(filtered_environment, "environmentPointer != NULL && *environmentPointer != NULL", "macOS process handoff environment guard")
    require(filtered_environment, "calloc( keepCount + 1, sizeof( char * ) )", "macOS process handoff filtered environment allocation")
    require(filtered_environment, "OSX_ShouldDropProcessEnvironmentEntry( sourceEnvironment[sourceCount] )", "macOS process handoff filtered environment policy")
    require(drop_environment, 'idStr::Cmpn( entry, "DYLD_", 5 ) == 0', "macOS process handoff DYLD environment filter")
    require(drop_environment, 'OSX_EnvironmentEntryHasName( entry, "LD_PRELOAD" )', "macOS process handoff loader environment filter")
    require(drop_environment, 'OSX_EnvironmentEntryHasName( entry, "LD_LIBRARY_PATH" )', "macOS process handoff loader environment filter")
    require(drop_environment, 'OSX_EnvironmentEntryHasName( entry, "LD_AUDIT" )', "macOS process handoff loader environment filter")
    require(do_start_process, 'common->Printf( "Sys_DoStartProcess: invalid command line\\n" );', "macOS invalid process command diagnostic")
    reject(do_start_process, 'invalid command line \'%s\'', "macOS invalid process command diagnostic")

    require(source, '#include "../URLPolicy.h"', "shared macOS URL policy")
    require(open_url, "idURLPolicy::IsAllowedHTTPURL( url )", "macOS HTTP(S)-only URL guard")
    require(open_url, "OpenURL rejected: expected a bounded HTTP or HTTPS URL with a host", "macOS unsafe URL diagnostic")
    require(open_url, "OpenURL rejected: URL is not valid UTF-8", "macOS unsafe URL diagnostic")
    require(open_url, "OpenURL rejected: Foundation could not parse URL", "macOS unsafe URL diagnostic")
    require(open_url, "OpenURL rejected: Foundation did not preserve the required HTTP(S) host", "macOS unsafe URL diagnostic")
    require(open_url, "OpenURL failed after validation", "macOS URL handoff diagnostic")
    require_before(open_url, "idURLPolicy::IsAllowedHTTPURL( url )", 'NSString *urlString = [ NSString stringWithUTF8String: url ];', "macOS URL policy before UTF-8 bridge")
    require_before(open_url, "OSX_IsAllowedURL( nsURL )", 'common->Printf( "Open URL: %s\\n", url );', "macOS URL logging after allowlist guard")
    require_before(open_url, 'common->Printf( "Open URL: %s\\n", url );', "openURL: nsURL", "macOS approved URL logging before AppKit handoff")
    require(open_url, "OSX_IsAllowedURL( nsURL )", "macOS URL allowlist guard")
    reject(open_url, "OpenURL '%s' rejected", "macOS rejected URL raw logging")
    reject(open_url, "OpenURL '%s' failed", "macOS rejected URL raw logging")
    require(allowed_url, '[scheme caseInsensitiveCompare:@"https"]', "macOS URL allowlist")
    require(allowed_url, '[scheme caseInsensitiveCompare:@"http"]', "macOS URL allowlist")
    require(allowed_url, "return host != nil && [host length] > 0;", "macOS HTTP URL host guard")
    reject(allowed_url, '[scheme caseInsensitiveCompare:@"file"]', "macOS file URL policy")
    reject(source, "OSX_FileURLIsLocalRuntimeFile", "macOS file URL launcher")
    reject(source, "OSX_URLHasSafeSchemeSyntax", "macOS broad URL syntax-only policy")


def validate_sdl3_context_teardown_guards() -> None:
    # Phase B5b: the GL context half lives in the renderer-owned seam TU,
    # compiled into the SDL3 backend until the module split (Phase B8)
    source = read("src/renderer/OpenGL/gl_ContextSDL3.cpp")
    ensure_current = function_body(source, "static bool SDL3_EnsureGLContextCurrent(const char *operation) {")
    apply_swap = function_body(source, "static bool SDL3_ApplySwapInterval(void) {")
    screen_parms = function_body(source, "bool GLimp_SetScreenParms(glimpParms_t parms) {")
    shutdown = function_body(source, "void GLimp_Shutdown(void) {")
    swap = function_body(source, "void GLimp_SwapBuffers(void) {")
    activate = function_body(source, "void GLimp_ActivateContext(void) {")
    deactivate = function_body(source, "void GLimp_DeactivateContext(void) {")
    extension = function_body(source, "void *GLimp_ExtensionPointer(const char *name) {")

    # Post-F0 the seam TU drives every context operation through
    # renderWindowServices_t; the SDL-level semantics are asserted on the
    # backend's service implementations below.
    for token in (
        "if (!s_glWindow || !s_glContext) {",
        "s_glWindowServices->IsGLContextCurrent(s_glContext)",
        "s_glWindowServices->MakeGLContextCurrent(s_glContext)",
        "s_glWindowServices->CountContextError();",
        "return false;",
    ):
        require(ensure_current, token, "SDL3 current-context helper")

    require(apply_swap, 'SDL3_EnsureGLContextCurrent("swap interval update")', "SDL3 swap-interval current-context guard")
    require_before(
        apply_swap,
        'SDL3_EnsureGLContextCurrent("swap interval update")',
        "SetGLSwapInterval(requestedInterval)",
        "SDL3 swap-interval current-context guard",
    )
    require(screen_parms, 'SDL3_EnsureGLContextCurrent("screen parm change")', "SDL3 screen-parm current-context guard")
    require(shutdown, "if (s_glWindow) {\n\t\t\t(void)windowServices->MakeGLContextCurrent(NULL);", "SDL3 shutdown context detach guard")
    reject(shutdown, "(void)windowServices->MakeGLContextCurrent(NULL);\n\t\twindowServices->DestroyGLContext", "SDL3 shutdown unguarded context detach")

    current_guard = 'if (!SDL3_EnsureGLContextCurrent("swap buffers")) {'
    guard_body = function_body(swap, current_guard)
    require(guard_body, "return;", "SDL3 failed current-context stops presentation")
    require_before(swap, current_guard, "s_glWindowServices->SwapGLWindow()", "SDL3 swap current-context guard")
    require_before(swap, current_guard, "RB_GLES_RestoreStateAfterOverlay();", "GLES overlay restore current-context guard")
    reject(swap, "if (s_glWindow && !s_glWindowServices->SwapGLWindow())", "SDL3 swap window-only guard")

    require(activate, 'SDL3_EnsureGLContextCurrent("activate context")', "SDL3 activate current-context guard")
    require(deactivate, 'if (!SDL3_EnsureGLContextCurrent("deactivate context")) {', "SDL3 deactivate current-context guard")
    require_before(deactivate, 'if (!SDL3_EnsureGLContextCurrent("deactivate context")) {', "glFinish();", "SDL3 deactivate glFinish guard")

    require(extension, "if (name == NULL || name[0] == '\\0') {", "SDL3 extension null-name guard")
    require_before(extension, "if (name == NULL || name[0] == '\\0') {", "s_glWindowServices->GetGLProcAddress(name)", "SDL3 extension lookup guard")

    # the engine-side service implementations carry the SDL-level semantics
    # the seam used to hold: current-ness compares BOTH window and context,
    # and make-current targets the game window
    backend = read("src/sys/sdl3/sdl3_backend.cpp")
    is_current = function_body(backend, "static bool SDL3_WindowServices_IsGLContextCurrent(void *context) {")
    require(is_current, "SDL_GL_GetCurrentWindow() == s_sdlWindow", "SDL3 service current-window comparison")
    require(is_current, "SDL_GL_GetCurrentContext() == (SDL_GLContext)context", "SDL3 service current-context comparison")
    make_current = function_body(backend, "static bool SDL3_WindowServices_MakeGLContextCurrent(void *context) {")
    require(make_current, "SDL_GL_MakeCurrent(s_sdlWindow, (SDL_GLContext)context)", "SDL3 service make-current target")


def validate_posix_glue_allocation_guards() -> None:
    source = read("src/sys/posix/posix_main.cpp")
    clipboard = function_body(source, "char *Sys_GetClipboardData(void) {")
    tty_input_state = function_body(source, "static const char *tty_InputState( int &inputLength, int &inputCursor ) {")
    tty_hide = function_body(source, "void tty_Hide() {")
    tty_show = function_body(source, "void tty_Show() {")
    console_input = function_body(source, "char *Posix_ConsoleInput( void ) {")
    events = function_body(source, "void Sys_GenerateEvents( void ) {")
    debug_printf = function_body(source, "void Sys_DebugPrintf( const char *fmt, ... ) {")
    debug_vprintf = function_body(source, "void Sys_DebugVPrintf( const char *fmt, va_list arg ) {")
    sys_printf = function_body(source, "void Sys_Printf(const char *msg, ...) {")
    sys_vprintf = function_body(source, "void Sys_VPrintf(const char *msg, va_list arg) {")
    sys_error = function_body(source, "void Sys_Error(const char *error, ...) {")

    require(clipboard, "char *data = static_cast<char *>( Mem_Alloc", "macOS SDL3 clipboard allocation")
    require(clipboard, "if ( data == NULL ) {", "macOS SDL3 clipboard allocation guard")
    require(clipboard, "SDL_free( clipboardText );\n\t\treturn NULL;", "macOS SDL3 clipboard allocation cleanup")
    require_before(clipboard, "if ( data == NULL ) {", "memcpy( data, clipboardText", "macOS SDL3 clipboard copy guard")

    require(tty_input_state, "if ( buffer == NULL ) {", "macOS SDL3 POSIX tty input buffer guard")
    require(tty_input_state, "rawLength > static_cast<size_t>( idMath::INT_MAX )", "macOS SDL3 POSIX tty input length guard")
    require(tty_input_state, "inputCursor = idMath::ClampInt( 0, inputLength, input_field.GetCursor() );", "macOS SDL3 POSIX tty cursor guard")
    require(tty_hide, "tty_InputState( buf_len, cursor )", "macOS SDL3 POSIX tty clear guard")
    require(tty_hide, "len = buf_len - cursor;", "macOS SDL3 POSIX tty clear guard")
    reject(tty_hide, "strlen( input_field.GetBuffer() ) - input_field.GetCursor()", "macOS SDL3 POSIX tty clear guard")
    require(tty_show, "const char *buf = tty_InputState( bufferLength, cursor );", "macOS SDL3 POSIX tty show guard")
    require(tty_show, "int back = bufferLength - cursor;", "macOS SDL3 POSIX tty show guard")
    require(console_input, "input_field.SetCursor( inputLength );", "macOS SDL3 POSIX tty End-key guard")
    require(console_input, "const char *inputBuffer = tty_InputState( inputLength, inputCursor );", "macOS SDL3 POSIX tty submit guard")
    reject(console_input, "strlen( input_field.GetBuffer() )", "macOS SDL3 POSIX tty raw input length")

    require(events, "commandLength = strlen( s );", "macOS SDL3 console event size guard")
    require(events, "commandLength > idMath::INT_MAX - 1", "macOS SDL3 console event size guard")
    require(events, "b = (char *)Mem_Alloc( len );", "macOS SDL3 console event allocation")
    require(events, "if ( b == NULL ) {", "macOS SDL3 console event allocation guard")
    require_before(events, "if ( b == NULL ) {", "idStr::Copynz( b, s, len );", "macOS SDL3 console event copy guard")
    reject(events, "strcpy( b, s );", "macOS SDL3 console event copy guard")

    require(debug_printf, "if ( fmt == NULL ) {", "macOS SDL3 debug printf null-format guard")
    require(debug_vprintf, "if ( fmt == NULL ) {", "macOS SDL3 debug vprintf null-format guard")
    require(sys_printf, "if ( msg == NULL ) {", "macOS SDL3 Sys_Printf null-format guard")
    require(sys_printf, "msg = \"\";", "macOS SDL3 Sys_Printf null-format guard")
    require(sys_vprintf, "if ( msg == NULL ) {", "macOS SDL3 Sys_VPrintf null-format guard")
    require(sys_vprintf, "msg = \"\";", "macOS SDL3 Sys_VPrintf null-format guard")

    require(sys_error, "if ( error == NULL ) {", "macOS SDL3 Sys_Error null-format guard")
    require(sys_error, 'idStr::Copynz( text, "Unknown error", sizeof( text ) );', "macOS SDL3 Sys_Error fallback text")
    require_before(sys_error, "if ( error == NULL ) {", "va_start( argptr, error );", "macOS SDL3 Sys_Error va_start guard")


def validate_posix_thread_guards() -> None:
    source = read("src/sys/posix/posix_threads.cpp")
    enter = function_body(source, "void Sys_EnterCriticalSection( int index ) {")
    leave = function_body(source, "void Sys_LeaveCriticalSection( int index ) {")
    wait = function_body(source, "void Sys_WaitForEvent( int index ) {")
    trigger = function_body(source, "void Sys_TriggerEvent( int index ) {")
    create_thread = function_body(source, "void Sys_CreateThread( xthread_t function, void *parms, xthreadPriority priority, xthreadInfo& info, const char *name, xthreadInfo **threads, int *thread_count ) {")
    init_threads = function_body(source, "void Posix_InitPThreads( ) {")

    require(source, "static bool Sys_IsValidCriticalSectionIndex( int index ) {", "POSIX critical-section guard helper")
    require(source, "static bool Sys_IsValidTriggerEventIndex( int index ) {", "POSIX trigger-event guard helper")
    require(source, "static bool global_lock_initialized[ MAX_LOCAL_CRITICAL_SECTIONS ];", "POSIX pthread mutex init-state guard")
    require(source, "static bool\t\tevent_cond_initialized[ MAX_TRIGGER_EVENTS ];", "POSIX pthread cond init-state guard")
    require(source, "static bool Sys_IsCriticalSectionReady( int index, const char *operation ) {", "POSIX critical-section readiness guard")
    require(source, "!global_lock_initialized[ index ]", "POSIX critical-section readiness guard")
    require(source, "static bool Sys_LockCriticalSection( int index, const char *operation ) {", "POSIX critical-section lock helper")
    require(source, "static bool Sys_UnlockCriticalSection( int index, const char *operation ) {", "POSIX critical-section unlock helper")
    require(source, "static bool Sys_IsTriggerEventReady( int index, const char *operation ) {", "POSIX trigger-event readiness guard")
    require(source, "!event_cond_initialized[ index ]", "POSIX trigger-event readiness guard")
    require(enter, 'Sys_LockCriticalSection( index, "Sys_EnterCriticalSection" )', "POSIX critical-section enter guard")
    require(leave, 'Sys_UnlockCriticalSection( index, "Sys_LeaveCriticalSection" )', "POSIX critical-section leave guard")
    require(wait, 'Sys_IsTriggerEventReady( index, "Sys_WaitForEvent" )', "POSIX wait-event guard")
    require(wait, 'Sys_LockCriticalSection( MAX_LOCAL_CRITICAL_SECTIONS - 1, "Sys_WaitForEvent" )', "POSIX wait-event lock guard")
    require(wait, "pthread_cond_wait failed for event", "POSIX wait-event cond result guard")
    require(trigger, 'Sys_IsTriggerEventReady( index, "Sys_TriggerEvent" )', "POSIX trigger-event guard")
    require(trigger, 'Sys_LockCriticalSection( MAX_LOCAL_CRITICAL_SECTIONS - 1, "Sys_TriggerEvent" )', "POSIX trigger-event lock guard")
    require(trigger, "pthread_cond_signal failed for event", "POSIX trigger-event cond result guard")

    require(create_thread, "const char *threadName = name != NULL && name[0] != '\\0' ? name : \"unnamed\";", "POSIX thread create name guard")
    require(create_thread, "function == NULL || threads == NULL || thread_count == NULL", "POSIX thread create argument guard")
    require(create_thread, "if ( *thread_count < 0 ) {", "POSIX thread create tracking-count guard")
    require(create_thread, "*thread_count = 0;", "POSIX thread create tracking-count guard")
    require(create_thread, "pthread_attr_init( &attr );", "POSIX thread create attr guard")
    require(create_thread, "Sys_LeaveCriticalSection( );\n\t\tcommon->Error", "POSIX thread create unlocked fatal path")
    require(create_thread, "pthread_attr_destroy( &attr );\n\t\tSys_LeaveCriticalSection( );", "POSIX thread create attr cleanup")

    require(init_threads, "pthread_mutexattr_t *attrPtr = NULL;", "POSIX pthread init attr fallback")
    require(init_threads, "bool attrInitialized = false;", "POSIX pthread init attr lifetime guard")
    require(init_threads, "if ( result == 0 ) {", "POSIX pthread init attr fallback")
    require(init_threads, "attrInitialized = true;", "POSIX pthread init attr lifetime guard")
    require(init_threads, "critical section disabled", "POSIX pthread init error-check fallback")
    require(init_threads, "attrPtr = &attr;", "POSIX pthread init error-check success")
    require_before(init_threads, "if ( attrPtr != NULL ) {", "pthread_mutex_init( &global_lock[i], attrPtr );", "POSIX pthread init mutex fallback")
    require(init_threads, "global_lock_initialized[i] = false;", "POSIX pthread init mutex failure guard")
    require(init_threads, "global_lock_initialized[i] = true;", "POSIX pthread init mutex success guard")
    require(init_threads, "if ( attrInitialized ) {", "POSIX pthread init attr cleanup guard")
    require(init_threads, "pthread_cond_init( &event_cond[ i ], NULL );", "POSIX pthread init cond result guard")
    require(init_threads, "pthread_cond_init failed for event", "POSIX pthread init cond result guard")
    require(init_threads, "event_cond_initialized[i] = false;", "POSIX pthread init cond failure guard")
    require(init_threads, "event_cond_initialized[i] = true;", "POSIX pthread init cond success guard")


def validate_posix_console_guards() -> None:
    source = read("src/sys/posix/posix_syscon.cpp")
    input_state = function_body(source, "static const char *Posix_ConsoleInputState( int &inputLength, int &inputCursor ) {")
    start_text_input = function_body(source, "static void Posix_ConsoleStartTextInput( void ) {")
    handle_key = function_body(source, "static bool Posix_ConsoleHandleKey( const SDL_KeyboardEvent &keyEvent ) {")
    console_create = function_body(source, "static bool Posix_ConsoleCreateWindow( void ) {")
    process_event = function_body(source, "bool Posix_ConsoleProcessEvent( const void *eventData ) {")
    draw_button = function_body(source, "static void Posix_ConsoleDrawButton( int button ) {")
    render = function_body(source, "static void Posix_ConsoleRender( void ) {")
    queue = function_body(source, "static void Posix_ConsoleQueueCommand( const char *command ) {")
    splash_create = function_body(source, "void Sys_ShowSplash( void ) {")
    splash_drain = function_body(source, "static void Posix_SplashDrainEvents( SDL_WindowID windowID ) {")

    require(input_state, "if ( inputBuffer == NULL ) {", "POSIX console input-state null guard")
    require(input_state, "rawInputLength > static_cast<size_t>( idMath::INT_MAX )", "POSIX console input-state length guard")
    require(input_state, "inputCursor = idMath::ClampInt( 0, inputLength, s_consoleWindow.inputField.GetCursor() );", "POSIX console input-state cursor guard")
    require(start_text_input, "Posix_ConsoleInputState( inputLength, inputCursor )", "POSIX console text-input cursor guard")
    require(handle_key, "s_consoleWindow.inputField.SetCursor( inputLength );", "POSIX console End-key cursor guard")
    require(source, "static const int POSIX_CONSOLE_MAX_WHEEL_STEPS_PER_EVENT = 64;", "POSIX console wheel flood guard")
    require(process_event, "if ( !std::isfinite( event.wheel.y ) )", "POSIX console wheel finite guard")
    require(process_event, "idMath::ClampFloat(", "POSIX console wheel delta clamp")
    require(process_event, "idMath::INT_MAX - scrollDelta", "POSIX console wheel scroll saturation")
    require(draw_button, "const char *buttonLabel = label != NULL ? label : \"\";", "POSIX console button label guard")
    require(render, "const char *inputBuffer = Posix_ConsoleInputState( inputLength, inputCursor );", "POSIX console render input-state guard")
    reject(render, "s_consoleWindow.inputField.GetBuffer();\n\tconst int inputLength = strlen( inputBuffer );", "POSIX console render raw input-state use")

    require(queue, "command == NULL || command[0] == '\\0'", "POSIX console command guard")
    require(queue, "const size_t commandLength = strlen( command );", "POSIX console command size guard")
    require(queue, "commandLength > static_cast<size_t>( idMath::INT_MAX - 1 )", "POSIX console command size guard")
    require(queue, "if ( buffer == NULL ) {", "POSIX console command allocation guard")
    require_before(queue, "if ( buffer == NULL ) {", "idStr::Copynz( buffer, command, len );", "POSIX console command copy guard")

    require(console_create, "if ( s_consoleWindow.videoInitializedByConsole )", "POSIX console owned-video cleanup")
    require(console_create, "if ( s_consoleWindow.windowID == 0 )", "POSIX console window-id guard")
    require_before(console_create, "if ( s_consoleWindow.windowID == 0 )", "Posix_CreateSupportRenderer( s_consoleWindow.window", "POSIX console window-id guard")
    require(splash_create, "if ( s_splashWindow.windowID == 0 )", "POSIX splash window-id guard")
    require_before(splash_create, "if ( s_splashWindow.windowID == 0 )", "Posix_CreateSupportRenderer( s_splashWindow.window", "POSIX splash window-id guard")
    require(splash_drain, "if ( !SDL_PushEvent( &event ) ) {", "POSIX splash event requeue failure guard")
    require(splash_drain, "failed to requeue non-splash event", "POSIX splash event requeue failure guard")


def validate_sdl3_video_ownership() -> None:
    console = read("src/sys/posix/posix_syscon.cpp")
    public = read("src/sys/posix/posix_public.h")
    backend = read("src/sys/sdl3/sdl3_backend.cpp")

    splash_acquire = function_body(console, "static bool Posix_SplashEnsureVideo( void ) {")
    splash_release = function_body(console, "static void Posix_SplashDestroy( void ) {")
    console_acquire = function_body(console, "static bool Posix_ConsoleEnsureVideo( void ) {")
    console_release = function_body(console, "void Posix_ShutdownConsole( void ) {")
    destroy_splash = function_body(console, "void Sys_DestroySplash( void ) {")
    # Phase B5b: SDL video-subsystem ownership moved into the engine-side
    # window services; the context TU's GLimp_Init/Shutdown drive them
    gl_prepare = function_body(backend, "static bool SDL3_WindowServices_PrepareWindowSystem(void) {")
    gl_finish_teardown = function_body(backend, "static void SDL3_WindowServices_FinishWindowTeardown(void) {")

    for body, owner, init_call, context in (
        (splash_acquire, "s_splashWindow.videoInitializedBySplash", "SDL_InitSubSystem( SDL_INIT_VIDEO )", "POSIX splash SDL video ownership"),
        (console_acquire, "s_consoleWindow.videoInitializedByConsole", "SDL_InitSubSystem( SDL_INIT_VIDEO )", "POSIX console SDL video ownership"),
        (gl_prepare, "s_sdlVideoReferenceHeld", "SDL_InitSubSystem(SDL_INIT_VIDEO)", "SDL3 game-window video ownership"),
    ):
        require(body, owner, context)
        require(body, init_call, context)

    require_before(splash_acquire, "Sys_SDL_ApplyVideoHintDefaults();", "SDL_InitSubSystem( SDL_INIT_VIDEO )", "POSIX splash pre-video SDL defaults")
    require_before(console_acquire, "Sys_SDL_ApplyVideoHintDefaults();", "SDL_InitSubSystem( SDL_INIT_VIDEO )", "POSIX console pre-video SDL defaults")

    reject(gl_prepare, "SDL_WasInit(SDL_INIT_VIDEO)", "SDL3 game window must not borrow a support-window video reference")
    reject(backend, "adopting video subsystem", "SDL3 game window ownership transfer")
    reject(console, "Posix_ReleaseStartupSDLVideoOwnership", "POSIX startup video ownership transfer")
    reject(public, "Posix_ReleaseStartupSDLVideoOwnership", "POSIX startup video ownership API")

    for token in ("SDL_DestroyTexture", "SDL_DestroyRenderer", "SDL_DestroyWindow"):
        require_before(splash_release, token, "SDL_QuitSubSystem( SDL_INIT_VIDEO )", "POSIX splash resource-before-video teardown")
    require(destroy_splash, "Posix_SplashDestroy();", "POSIX splash releases its SDL video reference")

    require_before(console_release, "SDL_DestroyRenderer", "SDL_QuitSubSystem( SDL_INIT_VIDEO )", "POSIX console renderer-before-video teardown")
    require_before(console_release, "SDL_DestroyWindow", "SDL_QuitSubSystem( SDL_INIT_VIDEO )", "POSIX console window-before-video teardown")
    require(gl_finish_teardown, "if (s_sdlVideoReferenceHeld && !s_teardownPreserveWindow)", "SDL3 game video release ownership guard")
    require_before(gl_finish_teardown, "SDL_DestroyWindow(s_sdlWindow)", "SDL_QuitSubSystem(SDL_INIT_VIDEO)", "SDL3 game window-before-video teardown")


def validate_posix_signal_guards() -> None:
    source = read("src/sys/posix/posix_signal.cpp")
    write_number = function_body(source, "static void Posix_WriteSignalNumber( int value ) {")

    require(write_number, "static_cast<unsigned int>( -( value + 1 ) ) + 1U;", "POSIX signal number INT_MIN guard")
    reject(write_number, "static_cast<unsigned int>( -value )", "POSIX signal number INT_MIN guard")


def validate_native_vram_probe_guards() -> None:
    source = read("src/sys/osx/macosx_compat.mm")
    video_ram = function_body(source, "int Sys_GetVideoRam( void ) {")

    require(video_ram, "const CGDirectDisplayID display = Sys_DisplayToUse();", "native macOS VRAM display guard")
    require(video_ram, "if ( display == 0 ) {", "native macOS VRAM display guard")
    require(video_ram, "CGDisplayIDToOpenGLDisplayMask( display )", "native macOS VRAM display guard")
    require(video_ram, "unsigned long maxVRAM = 0;", "native macOS VRAM overflow guard")
    require(video_ram, "err == kCGLNoError && vramMB > 0", "native macOS VRAM megabyte guard")
    require(video_ram, "err == kCGLNoError && vramBytes > 0", "native macOS VRAM byte guard")
    require(video_ram, "1024UL * 1024UL", "native macOS VRAM byte conversion")
    require(video_ram, "idMath::INT_MAX", "native macOS VRAM return clamp")


def validate_validation_wiring() -> None:
    validator = read("tools/validation/openq4_validate.py")
    commit = read(".github/workflows/commit-validation.yml")
    push = read(".github/workflows/push-verification.yml")

    for haystack, context in (
        (validator, "validation runner"),
        (commit, "commit validation workflow"),
        (push, "push verification workflow"),
    ):
        require(haystack, "macos_sdl3_backend_guard.py", context)


def main() -> None:
    validate_macos_sdl3_source_selection()
    validate_macos_process_handoff_guards()
    validate_sdl3_context_teardown_guards()
    validate_posix_glue_allocation_guards()
    validate_posix_thread_guards()
    validate_posix_console_guards()
    validate_sdl3_video_ownership()
    validate_posix_signal_guards()
    validate_native_vram_probe_guards()
    validate_validation_wiring()
    print("macos_sdl3_backend_guard: ok")


if __name__ == "__main__":
    main()
