// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "LoadingPresentation.h"

#include "../sys/StallSampler.h"

#include <algorithm>
#include <chrono>
#include <vector>

static idCVar com_loadingFrameTrace( "com_loadingFrameTrace", "0", CVAR_SYSTEM | CVAR_INTEGER | CVAR_NOCHEAT,
	"report frames presented during startup and level loads: 1 = intervals and the longest gaps, "
	"2 = also the call path behind each gap over com_loadingStallMsec (Windows x64)", 0, 2,
	idCmdSystem::ArgCompletion_Integer<0, 2> );
static idCVar com_loadingStallMsec( "com_loadingStallMsec", "40", CVAR_SYSTEM | CVAR_FLOAT | CVAR_NOCHEAT,
	"com_loadingFrameTrace 2: the presentation gap, in milliseconds, after which the main thread is sampled", 5.0f, 1000.0f );

static idCVar com_loadingStallFocus( "com_loadingStallFocus", "", CVAR_SYSTEM | CVAR_NOCHEAT,
	"com_loadingFrameTrace 2: rank the hottest functions only among the stalled samples inside a function whose name contains this" );

namespace {

struct LoadingTrace {
	bool active = false;		// a period is collecting frames
	bool closing = false;		// ended; the next present closes the report
	bool sampling = false;
	idStr kind;
	double begin = 0, end = 0, lastFrame = 0, firstFrame = 0;
	std::vector<float> intervals, draws;
	struct Gap { float msec; float at; };
	std::vector<Gap> gaps;
};
LoadingTrace trace;

float Percentile( std::vector<float> values, float p ) {
	if ( values.empty() ) {
		return 0.0f;
	}
	std::sort( values.begin(), values.end() );
	const size_t index = ( std::min )( values.size() - 1, static_cast<size_t>( p * ( values.size() - 1 ) + 0.5f ) );
	return values[index];
}

void EmitLine( const char *line, void * ) {
	common->Printf( "%s", line );
}

void CloseTrace( double nextFrameEnd ) {
	const float tail = nextFrameEnd > 0 ? static_cast<float>( nextFrameEnd - trace.end ) : -1.0f;
	int over25 = 0, over50 = 0, over100 = 0;
	float maxGap = 0;
	for ( float v : trace.intervals ) {
		over25 += v > 25.0f;
		over50 += v > 50.0f;
		over100 += v > 100.0f;
		maxGap = Max( maxGap, v );
	}
	common->Printf( "LOADING_FRAMES kind=%s msec=%.0f frames=%zu interval p50=%.1f p95=%.1f p99=%.1f max=%.1f "
		"over25=%d over50=%d over100=%d draw p50=%.2f p95=%.2f first=%.1f tail=%.1f\n",
		trace.kind.c_str(), trace.end - trace.begin, trace.draws.size(),
		Percentile( trace.intervals, 0.5f ), Percentile( trace.intervals, 0.95f ), Percentile( trace.intervals, 0.99f ),
		maxGap, over25, over50, over100, Percentile( trace.draws, 0.5f ), Percentile( trace.draws, 0.95f ),
		trace.firstFrame > 0 ? static_cast<float>( trace.firstFrame - trace.begin ) : -1.0f, tail );
	std::sort( trace.gaps.begin(), trace.gaps.end(), []( const LoadingTrace::Gap &a, const LoadingTrace::Gap &b ) {
		return a.msec > b.msec;
	} );
	for ( size_t i = 0; i < trace.gaps.size() && i < 12; ++i ) {
		common->Printf( "LOADING_GAP kind=%s msec=%.1f at=+%.0f\n", trace.kind.c_str(), trace.gaps[i].msec, trace.gaps[i].at );
	}
	if ( trace.sampling ) {
		Sys_StallSampler_Report( 16, com_loadingStallFocus.GetString(), EmitLine, NULL );
	}
	trace = LoadingTrace();
}

} // namespace

double LoadingPresentation_Now( void ) {
	return std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now().time_since_epoch() ).count();
}

void LoadingPresentation_Begin( const char *kind ) {
	if ( trace.closing ) {
		CloseTrace( 0 );
	}
	if ( com_loadingFrameTrace.GetInteger() <= 0 ) {
		trace = LoadingTrace();
		return;
	}
	trace = LoadingTrace();
	trace.active = true;
	trace.kind = kind;
	trace.begin = LoadingPresentation_Now();
	trace.intervals.reserve( 4096 );
	trace.draws.reserve( 4096 );
	if ( com_loadingFrameTrace.GetInteger() >= 2 && Sys_StallSampler_Supported() ) {
		trace.sampling = true;
		Sys_StallSampler_Begin( com_loadingStallMsec.GetFloat() );
	}
}

void LoadingPresentation_End( void ) {
	if ( !trace.active ) {
		return;
	}
	trace.active = false;
	trace.closing = true;
	trace.end = LoadingPresentation_Now();
}

void LoadingPresentation_Presented( double startMsec, double endMsec ) {
	if ( !trace.active && !trace.closing ) {
		return;
	}
	if ( trace.sampling ) {
		// Closes a stall in progress, the hand-off's included.
		Sys_StallSampler_Presented();
	}
	if ( trace.closing ) {
		CloseTrace( endMsec );
		return;
	}
	if ( trace.lastFrame > 0 ) {
		const float interval = static_cast<float>( endMsec - trace.lastFrame );
		trace.intervals.push_back( interval );
		if ( interval > 25.0f ) {
			trace.gaps.push_back( { interval, static_cast<float>( trace.lastFrame - trace.begin ) } );
		}
	}
	trace.draws.push_back( static_cast<float>( endMsec - startMsec ) );
	if ( trace.firstFrame <= 0 ) {
		trace.firstFrame = endMsec;
	}
	trace.lastFrame = endMsec;
}
