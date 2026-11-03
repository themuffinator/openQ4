// Copyright (C) 2026 DarkMatter Productions
#ifdef OPENQ4_RENDERER_VK_MODULE
#include "../../idlib/precompiled.h"
#pragma hdrstop
#include "../tr_local.h"
#include "../RendererMetrics.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

/*
===============================================================================

	Render thread, stage 1 (HF-07, REN-07 in docs/dev/roadmap.md).

	EndFrame hands the finished command list to this thread, which runs
	RB_ExecuteBackEndCommands (Vulkan recording, submit and present) while
	the main thread returns to the next frame's events, console commands,
	sound and game tic. The next BeginFrame waits for the thread and then
	completes the frame: the steps EndFrame used to run after the backend
	(render-texture deletes, metrics, frame memory, vertex cache).

	Only the game tic overlaps the backend. The front end of the next frame
	still starts after the backend has finished, so frame memory, the command
	chain, scene packets and vertex-cache frame temps keep a single owner at
	every moment and need no double buffering. Main-thread renderer entries
	that are not safe during the tic wait first (R_RenderThread_Sync).

===============================================================================
*/

idCVar r_vkRenderThread( "r_vkRenderThread", "0", CVAR_RENDERER | CVAR_BOOL,
	"1 records and presents each frame on a render thread while the main thread "
	"runs the next game tic (prototype; see REN-07). 0 runs the backend inline." );

static std::thread					rt_thread;
static std::thread::id				rt_threadId;
static std::mutex					rt_mutex;
static std::condition_variable		rt_wake;
static std::condition_variable		rt_done;
static const emptyCommand_t *		rt_commands = NULL;	// owned by the render thread while rt_busy
static bool							rt_busy = false;
static bool							rt_quit = false;
static bool							rt_failed = false;
static char							rt_failure[1024];
static int							rt_backEndMsec = 0;
static std::atomic<bool>			rt_inFlight( false );	// readable without the mutex

// counters for renderThreadStats; main thread only
static int							rt_framesThreaded = 0;
static int							rt_framesInline = 0;
static int							rt_syncWaits = 0;
static long long					rt_syncWaitUsec = 0;
static idStrList					rt_syncReasons;
static idList<int>					rt_syncReasonCounts;

// vk_Backend.cpp: main-thread window poll and recovery around a threaded frame
void VK_Backend_PrepareThreadedFrame( void );
void VK_Backend_CompleteThreadedFrame( void );

static void R_RenderThread_Main( void ) {
	for ( ;; ) {
		const emptyCommand_t *cmds;
		{
			std::unique_lock<std::mutex> lock( rt_mutex );
			rt_wake.wait( lock, [] { return rt_quit || rt_commands != NULL; } );
			if ( rt_quit && rt_commands == NULL ) {
				return;
			}
			cmds = rt_commands;
		}

		bool failed = false;
		const int start = Sys_Milliseconds();
		try {
			RB_ExecuteBackEndCommands( cmds );
		} catch ( idException &ex ) {
			// common->Error queued its message for the main thread and unwound
			// this thread only; R_RenderThread_Sync re-raises it there
			failed = true;
			idStr::Copynz( rt_failure, ex.error, sizeof( rt_failure ) );
		}
		const int elapsed = Sys_Milliseconds() - start;

		{
			std::lock_guard<std::mutex> lock( rt_mutex );
			rt_commands = NULL;
			rt_busy = false;
			rt_failed = rt_failed || failed;
			rt_backEndMsec = elapsed;
			rt_inFlight.store( false, std::memory_order_release );
		}
		rt_done.notify_all();
	}
}

static void R_RenderThread_Start( void ) {
	if ( rt_thread.joinable() ) {
		return;
	}
	rt_quit = false;
	rt_thread = std::thread( R_RenderThread_Main );
	rt_threadId = rt_thread.get_id();
	common->Printf( "Render thread started (r_vkRenderThread 1)\n" );
}

/*
==================
R_RenderThread_OnRenderThread
==================
*/
bool R_RenderThread_OnRenderThread( void ) {
	return rt_thread.joinable() && std::this_thread::get_id() == rt_threadId;
}

/*
==================
R_RenderThread_InFlight

True while a frame's backend may be running on the render thread.
==================
*/
bool R_RenderThread_InFlight( void ) {
	return rt_inFlight.load( std::memory_order_acquire );
}

/*
==================
R_RenderThread_Begin

Main thread, from EndFrame. Returns false to have the caller execute the
commands inline: the prototype is off, a capture needs the frame now, or
the configuration is outside the audited stage-1 set.
==================
*/
bool R_RenderThread_Begin( const emptyCommand_t *cmds ) {
	if ( !r_vkRenderThread.GetBool() || r_skipBackEnd.GetBool() || !R_RenderThread_FrameQualifies() ) {
		rt_framesInline++;
		return false;
	}
	R_RenderThread_Start();
	VK_Backend_PrepareThreadedFrame();
	{
		std::lock_guard<std::mutex> lock( rt_mutex );
		assert( !rt_busy );
		rt_commands = cmds;
		rt_busy = true;
		rt_inFlight.store( true, std::memory_order_release );
	}
	rt_wake.notify_one();
	rt_framesThreaded++;
	return true;
}

static void R_RenderThread_CountReason( const char *reason ) {
	const int index = rt_syncReasons.FindIndex( reason );
	if ( index >= 0 ) {
		rt_syncReasonCounts[index]++;
		return;
	}
	if ( rt_syncReasons.Num() < 64 ) {
		rt_syncReasons.Append( reason );
		rt_syncReasonCounts.Append( 1 );
	}
}

/*
==================
R_RenderThread_Sync

Main thread. Waits for the frame on the render thread, if any, and completes
it. Every main-thread renderer entry that touches state the backend reads,
other than the stage-1 tic-safe set, calls this first. The render thread
itself never waits on itself.
==================
*/
void R_RenderThread_Sync( const char *reason ) {
	if ( !rt_inFlight.load( std::memory_order_acquire ) || R_RenderThread_OnRenderThread() ) {
		return;
	}
	const unsigned long long waitStart = R_RendererMetrics_CpuClock();
	bool failed;
	char failure[sizeof( rt_failure )];
	int backEndMsec;
	{
		std::unique_lock<std::mutex> lock( rt_mutex );
		rt_done.wait( lock, [] { return !rt_busy; } );
		failed = rt_failed;
		rt_failed = false;
		idStr::Copynz( failure, rt_failure, sizeof( failure ) );
		backEndMsec = rt_backEndMsec;
	}
	rt_syncWaits++;
	rt_syncWaitUsec += static_cast<long long>( R_RendererMetrics_CpuClock() - waitStart );	// microseconds
	if ( idStr::Cmp( reason, "frame" ) != 0 ) {
		R_RenderThread_CountReason( reason );
	}

	R_RendererMetrics_RecordSubmitMsec( backEndMsec );
	tr.CompleteBackEndFrame();
	VK_Backend_CompleteThreadedFrame();

	if ( failed ) {
		common->Error( "render thread: %s", failure );
	}
}

/*
==================
R_RenderThread_Shutdown

Main thread. Completes any frame in flight and joins the thread; the device
and frame memory may be torn down afterwards.
==================
*/
void R_RenderThread_Shutdown( void ) {
	if ( R_RenderThread_OnRenderThread() ) {
		// a fatal error raised by the backend; the process is exiting
		return;
	}
	R_RenderThread_Sync( "shutdown" );
	if ( !rt_thread.joinable() ) {
		return;
	}
	{
		std::lock_guard<std::mutex> lock( rt_mutex );
		rt_quit = true;
	}
	rt_wake.notify_all();
	rt_thread.join();
	rt_threadId = std::thread::id();
	common->Printf( "Render thread stopped\n" );
}

/*
==================
R_RenderThreadStats_f
==================
*/
static void R_RenderThreadStats_f( const idCmdArgs &args ) {
	if ( args.Argc() > 1 && idStr::Icmp( args.Argv( 1 ), "reset" ) == 0 ) {
		rt_framesThreaded = rt_framesInline = rt_syncWaits = rt_syncWaitUsec = 0;
		rt_syncReasons.Clear();
		rt_syncReasonCounts.Clear();
		return;
	}
	common->Printf( "Render thread: enabled=%d running=%d threaded=%d inline=%d waits=%d waitMs=%.2f\n",
		r_vkRenderThread.GetBool() ? 1 : 0, rt_thread.joinable() ? 1 : 0, rt_framesThreaded, rt_framesInline,
		rt_syncWaits, rt_syncWaitUsec * 0.001f );
	for ( int i = 0; i < rt_syncReasons.Num(); i++ ) {
		common->Printf( "Render thread early sync: %s=%d\n", rt_syncReasons[i].c_str(), rt_syncReasonCounts[i] );
	}
}

void R_RenderThread_Init( void ) {
	cmdSystem->AddCommand( "renderThreadStats", R_RenderThreadStats_f, CMD_FL_RENDERER,
		"prints render-thread frame counts and the main-thread entries that waited for it; 'reset' clears them" );
}

#endif // OPENQ4_RENDERER_VK_MODULE
