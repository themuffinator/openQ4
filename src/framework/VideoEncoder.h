// First-party openQ4 video capture encoders. GPL-3.0-or-later.
#ifndef OPENQ4_VIDEO_ENCODER_H
#define OPENQ4_VIDEO_ENCODER_H

#include <cstdio>
#include <string>
#include <vector>

/*
Video capture hands finished frames to one encoder at a time (VideoCapture.cpp).

Threads: Open, WriteFrame, WriteAudio and Close run in order on the capture's
writer thread, which owns the encoder. PrepareFrame runs on the capture's
worker threads, several frames at once, so it touches nothing but the frame it
is given and the settings Open received; it does the per-frame work that can
run in parallel (colour conversion, JPEG compression). Nothing here uses idlib
containers or the engine's allocator, which belong to the main thread.

Frames reach PrepareFrame as top-down RGBA8 at exactly the opened size, in the
display's sRGB encoding. Audio is interleaved float at the opened rate.
*/

struct videoEncodeSettings_t {
	std::string	path;				// output file, OS path, UTF-8
	std::string	container;			// mp4, mkv, webm, mov, avi or gif
	std::string	codec;				// h264, hevc, av1, vp9, prores, mjpeg or gif; never "auto"
	int			width = 0;			// even
	int			height = 0;			// even
	int			fps = 60;
	int			quality = 80;		// 1 (smallest) to 100 (best)
	int			bitrateKbps = 0;	// 0: chosen from quality
	bool		audio = false;
	int			audioRate = 48000;
	int			audioChannels = 2;
	int			audioBitrateKbps = 192;
	bool		hardware = true;	// a GPU encoder may be used when one works
	std::string	preset;				// software encoder speed (x264 names); never "auto"
	std::string	ffmpegPath;			// resolved program, for the FFmpeg encoder
};

class idVideoEncoder {
public:
	virtual					~idVideoEncoder() {}

	virtual const char *	Name() const = 0;
	// What actually encodes, once Open succeeded ("H.264 (h264_nvenc) + AAC").
	virtual std::string		Description() const = 0;

	virtual bool			Open( const videoEncodeSettings_t &settings, std::string &error ) = 0;
	// May take the frame's storage (swap) instead of copying it. NULL on failure.
	virtual void *			PrepareFrame( std::vector<unsigned char> &rgba ) = 0;
	virtual void			FreeFrame( void *prepared ) = 0;
	// The prepared frame shown count times in a row (a capture that fell behind
	// real time repeats its last frame rather than shortening the video).
	virtual bool			WriteFrame( void *prepared, int count, std::string &error ) = 0;
	virtual bool			WriteAudio( const float *interleaved, int frames, std::string &error ) = 0;
	// Finishes the file. Called once, also after a failure, to release everything.
	virtual bool			Close( std::string &error ) = 0;
};

// NULL where the platform has no such encoder.
idVideoEncoder *	VideoEncoder_CreateMediaFoundation( void );
idVideoEncoder *	VideoEncoder_CreateFFmpeg( void );
idVideoEncoder *	VideoEncoder_CreateAVI( void );

// Whether Media Foundation's MP4 writer can load on this system (Windows N
// editions ship without it).
bool				VideoEncoder_MediaFoundationAvailable( void );
// The FFmpeg program: the configured path when it exists, else ffmpeg next to
// the game executable, else the first on PATH. False when none is found.
bool				VideoEncoder_FindFFmpeg( const char *configured, std::string &path );

// Helpers shared by the encoders.
FILE *				VideoEncoder_OpenFile( const std::string &utf8Path, const char *mode );
bool				VideoEncoder_RemoveFile( const std::string &utf8Path );
bool				VideoEncoder_RenameFile( const std::string &from, const std::string &to );
// The H.264/HEVC constant-rate-factor scale (0 best .. 51) for a quality.
int					VideoEncoder_QualityToCRF( int quality );
// A mean bitrate for the bitrate-driven encoders, from quality and frame size.
int					VideoEncoder_QualityToKbps( int quality, int width, int height, int fps, const std::string &codec );

#endif
