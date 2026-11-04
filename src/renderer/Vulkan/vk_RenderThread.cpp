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

// The worker must unwind before any common error path can tear down the
// renderer. Keep fatality in the receipt so main-thread retirement does not
// silently turn an invariant violation into a recoverable game drop.
class renderThreadException_t : public idException {
public:
	renderThreadException_t( const char *message, bool isFatal ) : idException( message ), fatal( isFatal ) {}
	bool fatal;
};

void R_RenderThread_Error( const char *message, bool fatal ) {
	if ( R_RenderThread_OnRenderThread() ) {
		throw renderThreadException_t( message, fatal );
	}
	if ( fatal ) {
		common->FatalError( "%s", message );
	} else {
		common->Error( "%s", message );
	}
}

static std::thread					rt_thread;
static std::thread::id				rt_threadId;
static std::mutex					rt_mutex;
static std::condition_variable		rt_wake;
static std::condition_variable		rt_done;
static const emptyCommand_t *		rt_commands = NULL;	// owned by the render thread while rt_busy
static bool							rt_busy = false;
static bool							rt_quit = false;
static bool							rt_failed = false;
static bool							rt_fatal = false;
static char							rt_failure[1024];
static int							rt_backEndMsec = 0;
// A frame stays owned until the main thread retires it, even when the worker
// has already finished. Clearing this on the worker loses the completion when
// a short frame finishes before the next Sync (including its errors/def ops).
static std::atomic<bool>			rt_inFlight( false );

// counters for renderThreadStats; main thread only
static int							rt_framesThreaded = 0;
static int							rt_framesInline = 0;
static int							rt_framesCompleted = 0;
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
		bool fatal = false;
		const int start = Sys_Milliseconds();
		try {
			RB_ExecuteBackEndCommands( cmds );
		} catch ( const renderThreadException_t &ex ) {
			failed = true;
			fatal = ex.fatal;
			idStr::Copynz( rt_failure, ex.error, sizeof( rt_failure ) );
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
			rt_fatal = rt_fatal || fatal;
			rt_backEndMsec = elapsed;
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

True until the main thread has collected the frame's backend result. Game-tic
updates must keep queuing after recording ends, preserving their call order.
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
		assert( !rt_inFlight.load( std::memory_order_relaxed ) );
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
	bool fatal;
	char failure[sizeof( rt_failure )];
	int backEndMsec;
	{
		std::unique_lock<std::mutex> lock( rt_mutex );
		if ( rt_busy ) {
			rt_syncWaits++;
		}
		rt_done.wait( lock, [] { return !rt_busy; } );
		failed = rt_failed;
		fatal = rt_fatal;
		rt_failed = rt_fatal = false;
		idStr::Copynz( failure, rt_failure, sizeof( failure ) );
		backEndMsec = rt_backEndMsec;
	}
	// Release ownership before completion: frame/cache retirement calls Sync,
	// and applying queued world updates must take their immediate paths.
	rt_inFlight.store( false, std::memory_order_release );
	rt_framesCompleted++;
	rt_syncWaitUsec += static_cast<long long>( R_RendererMetrics_CpuClock() - waitStart );	// microseconds
	if ( idStr::Cmp( reason, "frame" ) != 0 ) {
		R_RenderThread_CountReason( reason );
	}

	R_RendererMetrics_RecordSubmitMsec( backEndMsec );
	tr.CompleteBackEndFrame();
	VK_Backend_CompleteThreadedFrame();

	if ( fatal ) {
		common->FatalError( "render thread: %s", failure );
	}
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
		// Only the main thread owns thread lifetime; never join ourselves.
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
	// Report one closed interval, including a frame that finished before this
	// command. Reset must not orphan its handoff in the next interval's count.
	R_RenderThread_Sync( "stats" );
	if ( args.Argc() > 1 && idStr::Icmp( args.Argv( 1 ), "reset" ) == 0 ) {
		rt_framesThreaded = rt_framesInline = rt_framesCompleted = rt_syncWaits = 0;
		rt_syncWaitUsec = 0;
		rt_syncReasons.Clear();
		rt_syncReasonCounts.Clear();
		return;
	}
	common->Printf( "Render thread: enabled=%d running=%d threaded=%d inline=%d waits=%d waitMs=%.2f completed=%d\n",
		r_vkRenderThread.GetBool() ? 1 : 0, rt_thread.joinable() ? 1 : 0, rt_framesThreaded, rt_framesInline,
		rt_syncWaits, rt_syncWaitUsec * 0.001f, rt_framesCompleted );
	for ( int i = 0; i < rt_syncReasons.Num(); i++ ) {
		common->Printf( "Render thread early sync: %s=%d\n", rt_syncReasons[i].c_str(), rt_syncReasonCounts[i] );
	}
}

void R_RenderThread_Init( void ) {
	cmdSystem->AddCommand( "renderThreadStats", R_RenderThreadStats_f, CMD_FL_RENDERER,
		"prints render-thread frame counts and the main-thread entries that waited for it; 'reset' clears them" );
}

#endif // OPENQ4_RENDERER_VK_MODULE
