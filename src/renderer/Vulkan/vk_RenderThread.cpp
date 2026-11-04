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

	Render thread (HF-07, REN-07 in docs/dev/roadmap.md).

	EndFrame hands the finished command list to this thread, which runs
	RB_ExecuteBackEndCommands (Vulkan recording, submit and present) while
	the main thread returns to the next frame's events, console commands,
	sound and game tic. Mode 1 joins at BeginFrame. Mode 2 also builds the
	next front end in a second arena, with immutable submitted geometry/defs,
	separate scene packets, cache headers and metrics. It joins at EndFrame
	before handing off another frame. Resource mutation and readback retain
	explicit barriers; neither mode permits a second backend in flight.

===============================================================================
*/

idCVar r_vkRenderThread( "r_vkRenderThread", "0", CVAR_RENDERER | CVAR_INTEGER,
	"0 runs inline; 1 overlaps the game tic; 2 also overlaps the next front end (experimental; REN-07).", 0, 2 );

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

static frameData_t *rt_frame = NULL;
static glconfig_t rt_config;
static viewDef_t *rt_primaryView = NULL;
static bool rt_takingScreenshot = false;
static bool rt_frontEndOverlap = false;
static int rt_frontEndsOverlapped = 0;
static int rt_frontEndsFinishedWhileBusy = 0;
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
		R_SetBackEndFrameData( rt_frame );
		glConfig = rt_config;
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
		R_RendererMetrics_MarkBackEndFinished();
		R_SetBackEndFrameData( NULL );
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
	common->Printf( "Render thread started (r_vkRenderThread %d)\n", r_vkRenderThread.GetInteger() );
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

bool R_RenderThread_OverlapRequested( void ) {
	if ( R_RenderThread_OnRenderThread() ) return rt_frontEndOverlap;
	return r_vkRenderThread.GetInteger() == 2 && !r_lockSurfaces.GetBool();
}

bool R_RenderThread_FrontEndMayOverlap( void ) {
	return R_RenderThread_InFlight() && rt_frontEndOverlap;
}

void R_RenderThread_SyncFrontEnd( const char *reason ) {
	if ( !R_RenderThread_FrontEndMayOverlap() ) {
		R_RenderThread_Sync( reason );
	} else if ( idStr::Cmp( reason, "frame" ) == 0 ) {
		std::lock_guard<std::mutex> lock( rt_mutex );
		if ( rt_busy ) ++rt_frontEndsOverlapped;
	}
}

viewDef_t *R_SubmittedPrimaryView( void ) {
	return R_RenderThread_OnRenderThread() ? rt_primaryView : tr.primaryView;
}

bool R_SubmittedTakingScreenshot( void ) {
	return R_RenderThread_OnRenderThread() ? rt_takingScreenshot : tr.takingScreenshot;
}

/*
==================
R_RenderThread_Begin

Main thread, from EndFrame. Returns false to have the caller execute the
commands inline: the prototype is off, a capture needs the frame now, or
the configuration requires synchronous rendering.
==================
*/
bool R_RenderThread_Begin( const emptyCommand_t *cmds ) {
	if ( !r_vkRenderThread.GetBool() || r_skipBackEnd.GetBool() || !R_RenderThread_FrameQualifies() ) {
		rt_framesInline++;
		return false;
	}
	R_RenderThread_Start();
	VK_Backend_PrepareThreadedFrame();
	// All views are frozen and the preceding backend has retired. Earlier
	// resource syncs can occur inside a still-building view, so they must not
	// drain the producer's deferred geometry. Do it only at this handoff.
	if ( R_RenderThread_OverlapRequested() ) R_FreeDeferredFrameSurfaces();
	R_CountFrameData();
	R_TemporalPresentation_SubmitFrame();
	rt_frame = frameData;
	rt_config = glConfig;
	rt_primaryView = tr.primaryView;
	rt_takingScreenshot = tr.takingScreenshot;
	rt_frontEndOverlap = R_RenderThread_OverlapRequested();
	backEnd.postProcessTexelSize = tr.postProcessTexelSize;
	backEnd.postProcessSourceColorSpace = tr.postProcessSourceColorSpace;
	backEnd.postProcessSMAAQuality = tr.postProcessSMAAQuality;
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
it. Resource mutations call this first; producer-only entries use the
mode-aware SyncFrontEnd barrier. The render thread never waits on itself.
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
			if ( rt_frontEndOverlap && idStr::Cmp( reason, "handoff" ) == 0 ) ++rt_frontEndsFinishedWhileBusy;
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

	R_SetBackEndFrameData( rt_frame );
	R_RendererMetrics_RecordSubmitMsec( backEndMsec );
	tr.CompleteBackEndFrame();
	R_SetBackEndFrameData( NULL );
	rt_frame = NULL;
	rt_frontEndOverlap = false;
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
		rt_frontEndsOverlapped = rt_frontEndsFinishedWhileBusy = 0;
		rt_syncWaitUsec = 0;
		rt_syncReasons.Clear();
		rt_syncReasonCounts.Clear();
		return;
	}
	common->Printf( "Render thread: enabled=%d running=%d threaded=%d inline=%d waits=%d waitMs=%.2f completed=%d\n",
		r_vkRenderThread.GetBool() ? 1 : 0, rt_thread.joinable() ? 1 : 0, rt_framesThreaded, rt_framesInline,
		rt_syncWaits, rt_syncWaitUsec * 0.001f, rt_framesCompleted );
	common->Printf( "Render thread overlap: mode=%d frontEndsWhileBusy=%d frontEndsFinishedWhileBusy=%d\n", r_vkRenderThread.GetInteger(), rt_frontEndsOverlapped, rt_frontEndsFinishedWhileBusy );
	for ( int i = 0; i < rt_syncReasons.Num(); i++ ) {
		common->Printf( "Render thread early sync: %s=%d\n", rt_syncReasons[i].c_str(), rt_syncReasonCounts[i] );
	}
}

void R_RenderThread_Init( void ) {
	cmdSystem->AddCommand( "renderThreadStats", R_RenderThreadStats_f, CMD_FL_RENDERER,
		"prints render-thread frame counts and the main-thread entries that waited for it; 'reset' clears them" );
}

#endif // OPENQ4_RENDERER_VK_MODULE
