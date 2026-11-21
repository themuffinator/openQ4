// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

// Diagnostic sampler for presentation stalls during blocking work (startup and
// level loads). While armed, a watcher thread samples the arming thread's call
// stack whenever no frame has been presented for longer than the threshold,
// so a report can name the work that starved presentation. Windows x64 only;
// elsewhere every call is a no-op and the report is empty.
//
// Begin, Presented and Report must all be called from the thread being
// sampled. Report symbolizes, so it may take a moment the first time.
void	Sys_StallSampler_Begin( double thresholdMsec );
// A frame was presented; a stall in progress ends here.
void	Sys_StallSampler_Presented( void );
// Stops sampling and emits one line per recorded stall, longest first, at most
// maxStalls, each naming the call path common to the stall's samples, after a
// ranking of the functions inside the most samples. A non-empty focus ranks
// only the samples inside a function whose name contains it.
void	Sys_StallSampler_Report( int maxStalls, const char *focus, void ( *emit )( const char *line, void *context ), void *context );
bool	Sys_StallSampler_Supported( void );
