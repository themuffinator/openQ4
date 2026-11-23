// First-party openQ4 video capture. GPL-3.0-or-later.
#ifndef OPENQ4_VIDEO_CAPTURE_H
#define OPENQ4_VIDEO_CAPTURE_H

/*
Records what the window shows, sound included, to a video file
(recordVideo / stopVideo / toggleVideo; settings under com_video*; see
docs/user/video-capture.md).

The session calls BeforePresent and AfterPresent around every in-sequence
screen update. A fixed-rate recording also sets the game's pace: the session
runs exactly FixedTicsPerFrame game tics per frame, so every video frame is
one exact step of the game whatever the encoder costs.
*/

void	VideoCapture_Init( void );
// Finishes a recording and waits for its file.
void	VideoCapture_Shutdown( void );

// name: the file name without folder or extension; NULL or empty picks one
// from the map and the time. fixedTics > 0 forces fixed-rate capture of that
// many game tics per frame (aviGame, aviDemo); 0 follows com_videoTiming.
bool	VideoCapture_Start( const char *name, int fixedTics );
void	VideoCapture_Stop( void );
// starting or recording; a finished recording still writing its file is not active
bool	VideoCapture_Active( void );
// > 0 while a fixed-rate recording runs
int		VideoCapture_FixedTicsPerFrame( void );

void	VideoCapture_BeforePresent( void );
void	VideoCapture_AfterPresent( void );

#endif
