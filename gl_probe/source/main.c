//
//	gl_probe - risk R11: can SDL2 give us a GLES2 context (EGL) on the Switch?
//
//	P2 / T2.3 requires an OpenGL ES path: hgiox.cpp only has two branches, and
//	the Switch cannot use the desktop-GL one, so the GLES/EGL branch (currently
//	rewritten for raspbian) has to be opened for the Switch.  Before writing any
//	of that, this probe answers the single blocking question:
//
//	    does SDL_GL_CreateContext() succeed on real hardware, and can we
//	    present a frame to the screen?
//
//	Scope is deliberately minimal - create context, query GL strings, clear and
//	present, animate for a few seconds.  No shaders, no textures, no VBOs: those
//	belong to T2.3, this probe only removes R11 as an unknown.
//
//	Two hard rules learned from the official devkitPro OpenGL examples
//	(switchbrew/switch-examples :: graphics/opengl/README.md):
//
//	  1. "It is not possible to use the libnx console and the GPU at the same
//	     time."  So this program never calls consoleInit() - all diagnostics go
//	     out over nxlink, and the visible evidence is the animated screen itself.
//	  2. mesa/nouveau need their debugging output configured; setMesaConfig()
//	     below is the same hook the official examples use.
//
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <switch.h>
#include <switch/runtime/nxlink.h>

#include <SDL2/SDL.h>
#include <GLES2/gl2.h>

//
//	These are fetched through SDL_GL_GetProcAddress() rather than linked, which
//	is exactly how SDL's own Switch EGL loader binds them (see
//	SDL_egl.c: LOAD_FUNC + eglGetProcAddress).  Fetching them ourselves also
//	keeps this probe independent of the glapi/GLESv2 link order question.
//
typedef const GLubyte *(*PFN_glGetString)( GLenum name );
typedef void (*PFN_glClearColor)( GLclampf r, GLclampf g, GLclampf b, GLclampf a );
typedef void (*PFN_glClear)( GLbitfield mask );
typedef GLenum (*PFN_glGetError)( void );

static PFN_glGetString      p_glGetString;
static PFN_glClearColor     p_glClearColor;
static PFN_glClear          p_glClear;
static PFN_glGetError       p_glGetError;

static void say( const char *fmt, ... )
{
	va_list ap;
	va_start( ap, fmt );
	vprintf( fmt, ap );
	va_end( ap );
	//	stdout is a socket once nxlinkStdio() has dup2'd it - fully buffered, so
	//	nothing reaches the host without an explicit flush.
	//
	fflush( stdout );
}

//
//	Same purpose as the official examples' setMesaConfig(): keep mesa's own
//	diagnostics on so a failing EGL/GLES setup explains itself instead of
//	silently returning NULL.
//
static void setMesaConfig( void )
{
#ifdef MESA_DEBUG
	setenv( "MESA_DEBUG", "1", 1 );
#endif
	setenv( "MESA_NO_ERROR", "0", 1 );
	setenv( "LIBGL_DEBUG", "verbose", 1 );
}

static void reportRenderDrivers( void )
{
	int n = SDL_GetNumRenderDrivers();
	int i;

	say( "gl_probe: SDL render drivers: %d\n", n );
	for ( i = 0; i < n; i++ ) {
		SDL_RendererInfo info;
		if ( SDL_GetRenderDriverInfo( i, &info ) == 0 ) {
			say( "gl_probe:   [%d] %s%s%s\n", i, info.name,
				( info.flags & SDL_RENDERER_ACCELERATED ) ? " accelerated" : "",
				( info.flags & SDL_RENDERER_SOFTWARE ) ? " software" : "" );
		}
	}
}

int main( int argc, char *argv[] )
{
	SDL_Window *window = NULL;
	SDL_GLContext ctx = NULL;
	SDL_version linked;
	int w = 1280, h = 720;
	int dw = 0, dh = 0;
	int frames;
	int nxlink_fd;
	int res = 0;

	(void)argc;
	(void)argv;

	//	nxlink is the only output channel here (no console - see the header).
	//
	socketInitializeDefault();
	nxlink_fd = nxlinkStdio();

	say( "gl_probe: boot (nxlink fd = %d)\n", nxlink_fd );

	setMesaConfig();

	if ( SDL_Init( SDL_INIT_VIDEO | SDL_INIT_TIMER ) != 0 ) {
		say( "gl_probe: SDL_Init failed: %s\n", SDL_GetError() );
		goto done;
	}

	SDL_GetVersion( &linked );
	say( "gl_probe: SDL %d.%d.%d\n", linked.major, linked.minor, linked.patch );
	say( "gl_probe: video driver = %s\n", SDL_GetCurrentVideoDriver() );

	reportRenderDrivers();

	//	Ask for ES2 explicitly.  The Switch port's default profile is already
	//	ES 2.0 (SWITCH_GLES_DefaultProfileConfig), but stating it makes the
	//	intent explicit and will fail loudly if that ever changes.
	//
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES );
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 2 );
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 0 );
	SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
	SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 16 );

	window = SDL_CreateWindow( "gl_probe", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
	                           w, h, SDL_WINDOW_OPENGL );
	if ( window == NULL ) {
		say( "gl_probe: SDL_CreateWindow(OPENGL) failed: %s\n", SDL_GetError() );
		res = 1;
		goto done;
	}

	say( "gl_probe: SDL_WINDOW_OPENGL window created (%dx%d)\n", w, h );
	SDL_GL_GetDrawableSize( window, &dw, &dh );
	say( "gl_probe: drawable size = %dx%d\n", dw, dh );

	//	*** The R11 question ***
	//
	ctx = SDL_GL_CreateContext( window );
	if ( ctx == NULL ) {
		say( "gl_probe: *** SDL_GL_CreateContext FAILED: %s\n", SDL_GetError() );
		res = 2;
		goto done;
	}

	say( "gl_probe: *** SDL_GL_CreateContext OK\n" );

	{
		int major = 0, minor = 0, profile = 0;
		SDL_GL_GetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, &major );
		SDL_GL_GetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, &minor );
		SDL_GL_GetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, &profile );
		say( "gl_probe: context = ES %d.%d (profile mask 0x%x)\n", major, minor, profile );
	}

	p_glGetString  = (PFN_glGetString)SDL_GL_GetProcAddress( "glGetString" );
	p_glClearColor = (PFN_glClearColor)SDL_GL_GetProcAddress( "glClearColor" );
	p_glClear      = (PFN_glClear)SDL_GL_GetProcAddress( "glClear" );
	p_glGetError   = (PFN_glGetError)SDL_GL_GetProcAddress( "glGetError" );

	if ( !p_glGetString || !p_glClearColor || !p_glClear ) {
		say( "gl_probe: glGetString=%p glClearColor=%p glClear=%p\n",
			(void *)p_glGetString, (void *)p_glClearColor, (void *)p_glClear );
		say( "gl_probe: *** GL entry points could not be resolved\n" );
		res = 3;
		goto done;
	}

	say( "gl_probe: GL_VENDOR   = %s\n", (const char *)p_glGetString( GL_VENDOR ) );
	say( "gl_probe: GL_RENDERER = %s\n", (const char *)p_glGetString( GL_RENDERER ) );
	say( "gl_probe: GL_VERSION  = %s\n", (const char *)p_glGetString( GL_VERSION ) );
	say( "gl_probe: GLSL        = %s\n",
		(const char *)p_glGetString( GL_SHADING_LANGUAGE_VERSION ) );

	//	Animate for ~3 seconds so the result is visible on the panel itself, not
	//	just in the nxlink log.  Colour cycling makes it obvious the frames are
	//	actually being presented rather than a stale buffer.
	//
	SDL_GL_SetSwapInterval( 1 );
	for ( frames = 0; frames < 180; frames++ ) {
		float t = (float)frames / 180.0f;
		p_glClearColor( t, 1.0f - t, 0.35f, 1.0f );
		p_glClear( GL_COLOR_BUFFER_BIT );
		SDL_GL_SwapWindow( window );
		SDL_Delay( 16 );
	}

	say( "gl_probe: presented %d frames, glGetError = 0x%x\n",
		frames, p_glGetError ? p_glGetError() : 0 );
	say( "gl_probe: *** R11 probe finished OK\n" );

done:
	if ( ctx ) {
		SDL_GL_DeleteContext( ctx );
	}
	if ( window ) {
		SDL_DestroyWindow( window );
	}
	SDL_Quit();

	say( "gl_probe: exit code %d\n", res );

	//	When launched from hbmenu (no nxlink host) just quit - there is no output
	//	to keep on screen, and keeping the console up would fight the GPU.
	//
	socketExit();
	return res;
}