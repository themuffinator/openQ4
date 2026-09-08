/*
===========================================================================
openQ4 GLES clear probe.

Proves the one thing that blocks an OpenGL ES render path on a development
Mac: that openQ4's *link-time* GL entry points can be made to resolve to an
OpenGL ES driver instead of Apple's desktop OpenGL.framework.

Why this exists
---------------
openQ4 calls GL 1.x core entry points (glClear, glGetString, glDrawArrays,
...) as ordinary linked symbols -- glew.h declares them as plain externs and
only *extension* entry points go through GLEW's function-pointer dispatch.
The client therefore imports _glClear/_glGetString/_glDrawArrays from
/System/Library/Frameworks/OpenGL.framework. Creating an EGL/ANGLE ES context
does not change that: the context is current, but every core GL call still
lands in Apple's desktop GL. Measured symptom: with a valid ES 3.0 context
current, glGetString(GL_VERSION) returned "4.1 Metal - 90.5".

This probe links libGLESv2 (ANGLE) and does NOT link OpenGL.framework, so the
same symbol names bind to the ES driver. If GL_VERSION comes back as an
"OpenGL ES ..." string, the dispatch boundary works and a renderer-gles module
built the same way will reach ANGLE.

It deliberately declares the GL functions it uses rather than including a GL
header, so the binding under test is explicit and no desktop-GL header can be
pulled in by accident.
===========================================================================
*/

#include <SDL3/SDL.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

// GL entry points under test. These must resolve from the ES driver.
typedef unsigned int GLenum_t;
typedef unsigned int GLbitfield_t;
typedef float GLfloat_t;
typedef int GLint_t;
typedef int GLsizei_t;

extern "C" {
	const unsigned char *glGetString( GLenum_t name );
	void glClearColor( GLfloat_t r, GLfloat_t g, GLfloat_t b, GLfloat_t a );
	void glClear( GLbitfield_t mask );
	void glViewport( GLint_t x, GLint_t y, GLsizei_t w, GLsizei_t h );
	GLenum_t glGetError( void );
}

#define GL_VENDOR					0x1F00
#define GL_RENDERER					0x1F01
#define GL_VERSION					0x1F02
#define GL_SHADING_LANGUAGE_VERSION	0x8B8C
#define GL_COLOR_BUFFER_BIT			0x00004000
#define GL_DEPTH_BUFFER_BIT			0x00000100
#define GL_NO_ERROR					0

static const char *ProbeString( GLenum_t name ) {
	const unsigned char *value = glGetString( name );
	return value != NULL ? reinterpret_cast<const char *>( value ) : "(null)";
}

// Point SDL at exactly the libGLESv2 our own linked symbols resolved to.
//
// This must be the same file, not merely an identical copy. Two loaded copies
// of ANGLE each keep their own current-context state, so SDL would create the
// context in one while our linked glClear/glGetString call into the other --
// which then reports no current context and returns NULL for every string.
// Ask dyld where the linked glGetString actually came from and hand SDL that
// path, so there is provably one driver instance.
static void ProbeSetANGLEHints( void ) {
	char driverPath[1024] = { 0 };

	// dlsym gives the implementation address inside the dylib; taking &glGetString
	// directly could yield a stub in this executable instead.
	void *symbol = dlsym( RTLD_DEFAULT, "glGetString" );
	Dl_info info;
	if ( symbol != NULL && dladdr( symbol, &info ) != 0 && info.dli_fname != NULL ) {
		SDL_snprintf( driverPath, sizeof( driverPath ), "%s", info.dli_fname );
	}

	if ( driverPath[0] == '\0' ) {
		printf( "warning: could not locate the linked libGLESv2; SDL will search by name\n" );
		return;
	}

	printf( "linked GLES driver: %s\n", driverPath );
	SDL_SetHint( SDL_HINT_OPENGL_LIBRARY, driverPath );

	// libEGL sits beside libGLESv2 in every ANGLE layout.
	char eglPath[1024];
	SDL_snprintf( eglPath, sizeof( eglPath ), "%s", driverPath );
	char *lastSlash = strrchr( eglPath, '/' );
	if ( lastSlash != NULL ) {
		SDL_snprintf( lastSlash + 1, sizeof( eglPath ) - ( lastSlash + 1 - eglPath ), "libEGL.dylib" );
		SDL_SetHint( SDL_HINT_EGL_LIBRARY, eglPath );
	}
}

int main( int argc, char **argv ) {
	const bool headless = argc > 1 && strcmp( argv[1], "--headless" ) == 0;
	int exitCode = 0;

	ProbeSetANGLEHints();

	if ( !SDL_Init( SDL_INIT_VIDEO ) ) {
		printf( "FAIL: SDL_Init: %s\n", SDL_GetError() );
		return 1;
	}

	SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES );
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 3 );
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 0 );
	SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 8 );
	SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 8 );
	SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 8 );
	SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 24 );
	SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );

	SDL_WindowFlags flags = SDL_WINDOW_OPENGL;
	if ( headless ) {
		flags |= SDL_WINDOW_HIDDEN;
	}

	SDL_Window *window = SDL_CreateWindow( "openQ4 GLES clear probe", 640, 360, flags );
	if ( window == NULL ) {
		printf( "FAIL: SDL_CreateWindow: %s\n", SDL_GetError() );
		SDL_Quit();
		return 1;
	}

	SDL_GLContext context = SDL_GL_CreateContext( window );
	if ( context == NULL ) {
		printf( "FAIL: SDL_GL_CreateContext: %s\n", SDL_GetError() );
		SDL_DestroyWindow( window );
		SDL_Quit();
		return 1;
	}

	int major = 0;
	int minor = 0;
	int profileMask = 0;
	SDL_GL_GetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, &major );
	SDL_GL_GetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, &minor );
	SDL_GL_GetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, &profileMask );

	printf( "SDL context      : %d.%d profile=%s\n", major, minor,
		profileMask == SDL_GL_CONTEXT_PROFILE_ES ? "es"
			: profileMask == SDL_GL_CONTEXT_PROFILE_CORE ? "core" : "compatibility" );

	const char *versionString = ProbeString( GL_VERSION );
	printf( "GL_VENDOR        : %s\n", ProbeString( GL_VENDOR ) );
	printf( "GL_RENDERER      : %s\n", ProbeString( GL_RENDERER ) );
	printf( "GL_VERSION       : %s\n", versionString );
	printf( "GL_SL            : %s\n", ProbeString( GL_SHADING_LANGUAGE_VERSION ) );

	// The actual assertion: the linked glGetString reached an ES driver rather
	// than OpenGL.framework, which would answer with a bare "4.1 Metal - ...".
	const bool reachedES = strncmp( versionString, "OpenGL ES", 9 ) == 0;
	if ( !reachedES ) {
		printf( "\nFAIL: linked GL entry points did not reach an OpenGL ES driver.\n"
			"      GL_VERSION should start with \"OpenGL ES\"; a desktop version\n"
			"      string means the symbols still bind to OpenGL.framework.\n" );
		exitCode = 1;
	}

	// Clear through the ES driver and present, twice, so the swap chain is
	// exercised rather than only the first acquired image.
	int width = 0;
	int height = 0;
	SDL_GetWindowSizeInPixels( window, &width, &height );
	glViewport( 0, 0, width, height );

	for ( int frame = 0; frame < 2 && exitCode == 0; ++frame ) {
		glClearColor( frame == 0 ? 0.15f : 0.45f, 0.30f, frame == 0 ? 0.55f : 0.20f, 1.0f );
		glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

		const GLenum_t clearError = glGetError();
		if ( clearError != GL_NO_ERROR ) {
			printf( "FAIL: glClear reported GL error 0x%04x on frame %d\n", clearError, frame );
			exitCode = 1;
			break;
		}

		if ( !SDL_GL_SwapWindow( window ) ) {
			printf( "FAIL: SDL_GL_SwapWindow: %s\n", SDL_GetError() );
			exitCode = 1;
			break;
		}
		SDL_Delay( headless ? 0 : 250 );
	}

	if ( exitCode == 0 ) {
		printf( "\nPASS: cleared and presented %dx%d through ANGLE (OpenGL ES).\n", width, height );
	}

	SDL_GL_DestroyContext( context );
	SDL_DestroyWindow( window );
	SDL_Quit();
	return exitCode;
}
