// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#ifndef __LOADING_PRESENTATION_H__
#define __LOADING_PRESENTATION_H__

// Frames presented while blocking work owns the main thread: the startup
// initialization screen and level loading screens. com_loadingFrameTrace
// reports every such period: frame count, interval percentiles and the longest
// gaps, and at 2 (Windows x64) the call path that held each gap open.

// Milliseconds on the steady clock that every presentation stamp here uses.
double	LoadingPresentation_Now( void );
// A blocking period starts ("init" or "map"). Ending it lets the next present
// of any kind close the report, so the hand-off to normal frames is measured.
void	LoadingPresentation_Begin( const char *kind );
void	LoadingPresentation_End( void );
// Every presented frame, blocking or not, with when its submission started
// and when the present returned.
void	LoadingPresentation_Presented( double startMsec, double endMsec );

#endif /* !__LOADING_PRESENTATION_H__ */
