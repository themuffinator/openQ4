/*
===========================================================================

openQ4 video capture
Copyright (C) 2026 DarkMatter Productions

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

===========================================================================
*/

/*
Video capture records the presented frame, HUD, menus and console included,
with the game's own sound.

Picture: the renderer reads every presented frame back without waiting for it
(idRenderSystem::RequestVideoFrame/PollVideoFrame, a ring of asynchronous
reads), and the frame comes back two or three frames later.

Sound: the sound system moves onto an OpenAL loopback device for the
recording (SoundLoopback.h), so the capture mixes exactly the samples each
video frame spans, and plays the same samples to the speakers.

Timing: a fixed-rate recording runs a whole number of game tics per video
frame, so each frame is one exact step of the game: the video is perfectly
smooth even when encoding cannot keep up (the game then slows down while
recording instead). Single-player and demos use it by default. Multiplayer
cannot slow the server down, so it records in real time: each frame is
stamped with the wall clock and the encoder repeats a frame when the game
draws slower than the video's rate.

Encoding runs off the main thread: worker threads convert and compress
frames in parallel, one writer thread hands them to the encoder in order.
*/

#include "VideoCapture.h"

#if !defined( ID_DEDICATED )

#include "VideoEncoder.h"
#include "Session_local.h"
#include "async/AsyncNetwork.h"
#include "../sound/SoundLoopback.h"

#include <atomic>
#include <cmath>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <mutex>
#include <thread>

#if defined( USE_SDL3 )
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>
#endif

static const char *videoFormatNames[] = { "mp4", "mkv", "webm", "mov", "avi", "gif", NULL };
static const char *videoCodecNames[] = { "auto", "h264", "hevc", "av1", "vp9", "prores", "mjpeg", NULL };
static const char *videoEncoderNames[] = { "auto", "mediafoundation", "ffmpeg", "builtin", NULL };
static const char *videoTimingNames[] = { "auto", "fixed", "realtime", NULL };
static const char *videoPresetNames[] = { "auto", "ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow", NULL };

idCVar com_videoFormat( "com_videoFormat", "mp4", CVAR_SYSTEM | CVAR_ARCHIVE,
	"video capture container: mp4, mkv, webm, mov, avi or gif. mkv, webm, mov and gif need FFmpeg (com_videoFFmpeg); "
	"without it a recording falls back to the built-in Motion JPEG AVI writer",
	videoFormatNames, idCmdSystem::ArgCompletion_String<videoFormatNames> );
idCVar com_videoCodec( "com_videoCodec", "auto", CVAR_SYSTEM | CVAR_ARCHIVE,
	"video capture codec: auto (H.264 for mp4/mkv/mov, VP9 for webm, Motion JPEG for avi), h264, hevc, av1, vp9, prores (mov) or mjpeg",
	videoCodecNames, idCmdSystem::ArgCompletion_String<videoCodecNames> );
idCVar com_videoEncoder( "com_videoEncoder", "auto", CVAR_SYSTEM | CVAR_ARCHIVE,
	"video capture encoder: auto (Media Foundation for MP4 on Windows, else FFmpeg, else the built-in AVI writer), "
	"mediafoundation, ffmpeg or builtin",
	videoEncoderNames, idCmdSystem::ArgCompletion_String<videoEncoderNames> );
idCVar com_videoFPS( "com_videoFPS", "60", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER,
	"video capture frame rate. Fixed-rate capture needs a divisor of the game's tic rate (60: 60, 30, 20, 15...); "
	"other rates record in real time", 1, 240 );
idCVar com_videoQuality( "com_videoQuality", "80", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER,
	"video capture quality, 1 (smallest file) to 100 (best picture); 80 is visually close to the screen", 1, 100 );
idCVar com_videoBitrate( "com_videoBitrate", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER,
	"video capture bitrate in kbit/s; 0 lets com_videoQuality choose", 0, 500000 );
idCVar com_videoHeight( "com_videoHeight", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER,
	"video capture height in pixels, scaled down from the window keeping its shape; 0 records at the window's size "
	"(GIFs at most 480)", 0, 8640 );
idCVar com_videoAudio( "com_videoAudio", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL,
	"record the game's sound with video captures" );
idCVar com_videoAudioBitrate( "com_videoAudioBitrate", "192", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER,
	"video capture sound bitrate in kbit/s (AAC or Opus; the AVI writer stores uncompressed PCM)", 64, 512 );
idCVar com_videoTiming( "com_videoTiming", "auto", CVAR_SYSTEM | CVAR_ARCHIVE,
	"video capture timing: fixed (every frame one exact step of the game; the game slows down if encoding falls behind), "
	"realtime (the wall clock; slow frames repeat), auto (fixed in single-player and demos, realtime online)",
	videoTimingNames, idCmdSystem::ArgCompletion_String<videoTimingNames> );
idCVar com_videoHardware( "com_videoHardware", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL,
	"let video capture encode on the graphics card's video encoder (NVENC, AMF, Quick Sync, VideoToolbox) when one works" );
idCVar com_videoPreset( "com_videoPreset", "auto", CVAR_SYSTEM | CVAR_ARCHIVE,
	"speed of FFmpeg's software H.264/HEVC encoders (x264 preset names); auto: veryfast in real time, fast at a fixed rate",
	videoPresetNames, idCmdSystem::ArgCompletion_String<videoPresetNames> );
idCVar com_videoFFmpeg( "com_videoFFmpeg", "", CVAR_SYSTEM | CVAR_ARCHIVE,
	"the ffmpeg program (or its folder) video capture uses; empty looks next to the game, then on PATH" );
idCVar com_videoFolder( "com_videoFolder", "videos", CVAR_SYSTEM | CVAR_ARCHIVE,
	"folder under the save path that video captures are written to" );
// Read by the game: a fixed-rate recording draws every frame on its tic's own pose.
idCVar com_videoFixedStep( "com_videoFixedStep", "0", CVAR_SYSTEM | CVAR_ROM | CVAR_BOOL,
	"1 while a fixed-rate video capture is recording" );

/*
===============================================================================

	Encoding pipeline

===============================================================================
*/

struct videoJob_t {
	bool audio = false;
	// video
	std::vector<unsigned char> raw;
	int rawWidth = 0;
	int rawHeight = 0;
	bool topDown = false;
	bool bgra = false;
	int count = 1;
	bool ready = false;
	void *prepared = NULL;
	// audio
	std::vector<float> samples;
};

/*
Top-down RGBA at the encoder's size: flips, swizzles and, when the window is
not the recording's size, box-filters it (each output pixel the average of
the source pixels it covers).
*/
static void Video_Normalize( const videoJob_t &job, int outWidth, int outHeight, std::vector<unsigned char> &out ) {
	out.resize( (size_t)outWidth * outHeight * 4 );
	const int srcWidth = job.rawWidth;
	const int srcHeight = job.rawHeight;
	const int r = job.bgra ? 2 : 0;
	const int b = job.bgra ? 0 : 2;
	const unsigned char *src = job.raw.data();
	if ( srcWidth >= outWidth && srcHeight >= outHeight && srcWidth - outWidth <= 1 && srcHeight - outHeight <= 1 ) {
		for ( int y = 0; y < outHeight; ++y ) {
			const int sy = job.topDown ? y : srcHeight - 1 - y;
			const unsigned char *in = src + (size_t)sy * srcWidth * 4;
			unsigned char *o = out.data() + (size_t)y * outWidth * 4;
			for ( int x = 0; x < outWidth; ++x ) {
				o[x * 4 + 0] = in[x * 4 + r];
				o[x * 4 + 1] = in[x * 4 + 1];
				o[x * 4 + 2] = in[x * 4 + b];
				o[x * 4 + 3] = 255;
			}
		}
		return;
	}
	std::vector<int> x0( outWidth ), x1( outWidth );
	for ( int x = 0; x < outWidth; ++x ) {
		x0[x] = (int)( (long long)x * srcWidth / outWidth );
		x1[x] = (int)( (long long)( x + 1 ) * srcWidth / outWidth );
		if ( x1[x] <= x0[x] ) {
			x1[x] = x0[x] + 1;
		}
	}
	for ( int y = 0; y < outHeight; ++y ) {
		int y0 = (int)( (long long)y * srcHeight / outHeight );
		int y1 = (int)( (long long)( y + 1 ) * srcHeight / outHeight );
		if ( y1 <= y0 ) {
			y1 = y0 + 1;
		}
		unsigned char *o = out.data() + (size_t)y * outWidth * 4;
		for ( int x = 0; x < outWidth; ++x ) {
			unsigned int sum[3] = { 0, 0, 0 };
			unsigned int count = 0;
			for ( int sy = y0; sy < y1; ++sy ) {
				const int row = job.topDown ? sy : srcHeight - 1 - sy;
				const unsigned char *in = src + ( (size_t)row * srcWidth + x0[x] ) * 4;
				for ( int sx = x0[x]; sx < x1[x]; ++sx, in += 4 ) {
					sum[0] += in[r];
					sum[1] += in[1];
					sum[2] += in[b];
					count++;
				}
			}
			o[x * 4 + 0] = (unsigned char)( ( sum[0] + count / 2 ) / count );
			o[x * 4 + 1] = (unsigned char)( ( sum[1] + count / 2 ) / count );
			o[x * 4 + 2] = (unsigned char)( ( sum[2] + count / 2 ) / count );
			o[x * 4 + 3] = 255;
		}
	}
}

class idVideoPipeline {
public:
	~idVideoPipeline() {
		Abort();
		Join();
	}

	void Start( idVideoEncoder *useEncoder, const videoEncodeSettings_t &useSettings, int workerCount ) {
		encoder = useEncoder;
		settings = useSettings;
		const size_t frameBytes = (size_t)settings.width * settings.height * 4;
		maxVideoInFlight = (int)( ( 384u * 1024u * 1024u ) / ( frameBytes > 0 ? frameBytes : 1 ) );
		maxVideoInFlight = maxVideoInFlight < 3 ? 3 : ( maxVideoInFlight > 12 ? 12 : maxVideoInFlight );
		writer = std::thread( &idVideoPipeline::WriterMain, this );
		for ( int i = 0; i < workerCount; ++i ) {
			workers.emplace_back( &idVideoPipeline::WorkerMain, this );
		}
	}

	// Blocks while the encoder is too far behind (the back pressure that
	// slows a fixed-rate recording down to the encoder's speed).
	void SubmitVideo( videoJob_t *job ) {
		std::unique_lock<std::mutex> hold( lock );
		wake.wait( hold, [&] { return failed || videoInFlight < maxVideoInFlight; } );
		if ( failed ) {
			delete job;
			return;
		}
		videoInFlight++;
		order.push_back( job );
		unprepared.push_back( job );
		wake.notify_all();
	}

	void SubmitAudio( std::vector<float> &samples ) {
		if ( samples.empty() ) {
			return;
		}
		videoJob_t *job = new videoJob_t();
		job->audio = true;
		job->ready = true;
		job->samples.swap( samples );
		std::lock_guard<std::mutex> hold( lock );
		if ( failed ) {
			delete job;
			return;
		}
		order.push_back( job );
		wake.notify_all();
	}

	void Finish() {
		std::lock_guard<std::mutex> hold( lock );
		finishing = true;
		wake.notify_all();
	}

	void Abort() {
		std::lock_guard<std::mutex> hold( lock );
		if ( !failed && !done ) {
			failed = true;
			if ( error.empty() ) {
				error = "cancelled";
			}
		}
		finishing = true;
		wake.notify_all();
	}

	bool Done() {
		std::lock_guard<std::mutex> hold( lock );
		return done;
	}

	bool Failed( std::string &why ) {
		std::lock_guard<std::mutex> hold( lock );
		why = error;
		return failed;
	}

	bool TakeOpenedDescription( std::string &text ) {
		std::lock_guard<std::mutex> hold( lock );
		if ( !opened || openReported ) {
			return false;
		}
		openReported = true;
		text = description;
		return true;
	}

	long long FramesWritten() const { return framesWritten.load(); }

	void Join() {
		if ( writer.joinable() ) {
			writer.join();
		}
		for ( std::thread &worker : workers ) {
			if ( worker.joinable() ) {
				worker.join();
			}
		}
		workers.clear();
	}

private:
	idVideoEncoder *encoder = NULL;
	videoEncodeSettings_t settings;
	std::mutex lock;
	std::condition_variable wake;
	std::deque<videoJob_t *> order;			// every job, in submission order
	std::deque<videoJob_t *> unprepared;	// video jobs no worker has taken
	int videoInFlight = 0;
	int maxVideoInFlight = 6;
	bool writerReady = false;	// the encoder is open (or failed to open)
	bool opened = false;
	bool openReported = false;
	bool finishing = false;
	bool failed = false;
	bool done = false;
	bool quit = false;
	std::string error;
	std::string description;
	std::atomic<long long> framesWritten { 0 };
	std::thread writer;
	std::vector<std::thread> workers;

	void Fail( const std::string &why ) {
		std::lock_guard<std::mutex> hold( lock );
		if ( !failed ) {
			failed = true;
			error = why;
		}
		wake.notify_all();
	}

	void WorkerMain() {
		std::vector<unsigned char> rgba;
		for ( ;; ) {
			videoJob_t *job = NULL;
			{
				std::unique_lock<std::mutex> hold( lock );
				wake.wait( hold, [&] { return quit || ( writerReady && !unprepared.empty() ); } );
				if ( quit ) {
					return;
				}
				job = unprepared.front();
				unprepared.pop_front();
				if ( failed ) {
					job->ready = true;
					wake.notify_all();
					continue;
				}
			}
			Video_Normalize( *job, settings.width, settings.height, rgba );
			job->raw.clear();
			job->raw.shrink_to_fit();
			void *prepared = encoder->PrepareFrame( rgba );
			std::lock_guard<std::mutex> hold( lock );
			job->prepared = prepared;
			job->ready = true;
			if ( prepared == NULL && !failed ) {
				failed = true;
				error = "could not prepare a frame";
			}
			wake.notify_all();
		}
	}

	void WriterMain() {
		std::string why;
		const bool ok = encoder->Open( settings, why );
		{
			std::lock_guard<std::mutex> hold( lock );
			writerReady = true;
			opened = ok;
			description = ok ? encoder->Description() : std::string();
			if ( !ok && !failed ) {
				failed = true;
				error = why.empty() ? std::string( "the encoder did not open" ) : why;
			}
			wake.notify_all();
		}
		for ( ;; ) {
			videoJob_t *job = NULL;
			bool skip = false;
			{
				std::unique_lock<std::mutex> hold( lock );
				wake.wait( hold, [&] { return ( !order.empty() && order.front()->ready ) || ( finishing && order.empty() ) ||
					( failed && finishing ); } );
				if ( order.empty() || ( failed && finishing && !order.front()->ready ) ) {
					if ( finishing ) {
						break;
					}
					continue;
				}
				job = order.front();
				order.pop_front();
				skip = failed;
			}
			if ( !skip ) {
				std::string writeError;
				bool written = true;
				if ( job->audio ) {
					written = encoder->WriteAudio( job->samples.data(), (int)( job->samples.size() / settings.audioChannels ), writeError );
				} else if ( job->prepared != NULL ) {
					written = encoder->WriteFrame( job->prepared, job->count, writeError );
					framesWritten += job->count;
				}
				if ( !written ) {
					Fail( writeError );
				}
			}
			if ( job->prepared != NULL ) {
				encoder->FreeFrame( job->prepared );
			}
			{
				std::lock_guard<std::mutex> hold( lock );
				if ( !job->audio ) {
					videoInFlight--;
				}
				wake.notify_all();
			}
			delete job;
		}
		// jobs the workers still hold finish preparing before the encoder closes
		{
			std::unique_lock<std::mutex> hold( lock );
			quit = true;
			wake.notify_all();
		}
		for ( std::thread &worker : workers ) {
			if ( worker.joinable() ) {
				worker.join();
			}
		}
		{
			std::lock_guard<std::mutex> hold( lock );
			for ( videoJob_t *job : order ) {
				if ( job->prepared != NULL ) {
					encoder->FreeFrame( job->prepared );
				}
				delete job;
			}
			order.clear();
			unprepared.clear();
		}
		std::string closeError;
		if ( !encoder->Close( closeError ) ) {
			Fail( closeError );
		}
		std::lock_guard<std::mutex> hold( lock );
		done = true;
		wake.notify_all();
	}
};

/*
===============================================================================

	Capture controller (main thread)

===============================================================================
*/

enum videoCaptureState_t {
	VIDEO_CAPTURE_IDLE,
	VIDEO_CAPTURE_WAITING_FOR_SOUND,	// the sound system is moving onto the loopback device
	VIDEO_CAPTURE_RECORDING,
	VIDEO_CAPTURE_FINISHING				// the writer is completing the file
};

enum videoEncoderChoice_t {
	VIDEO_ENCODER_MEDIA_FOUNDATION,
	VIDEO_ENCODER_FFMPEG,
	VIDEO_ENCODER_BUILTIN
};

static const int VIDEO_AUDIO_RATE = 48000;

static struct videoCaptureLocal_t {
	videoCaptureState_t state = VIDEO_CAPTURE_IDLE;
	videoEncodeSettings_t settings;
	videoEncoderChoice_t encoderChoice = VIDEO_ENCODER_BUILTIN;
	idVideoEncoder *encoder = NULL;
	idVideoPipeline *pipeline = NULL;
	bool fixed = false;
	int fixedTics = 0;
	int fps = 60;
	bool wantAudio = false;
	int waitStartMsec = 0;
	// timing
	double clockSeconds = 0.0;		// realtime: the recording's clock
	double lastRealSeconds = 0.0;
	long long nextFixedFrame = 0;
	long long lastRequestedSlot = -1;
	long long lastWrittenSlot = -1;
	long long samplesRendered = 0;
	int framesSinceArrival = 0;
	std::vector<float> pendingAudio;	// rendered before the first frame opened the pipeline
	std::string failure;
#if defined( USE_SDL3 )
	SDL_AudioStream *monitor = NULL;
	bool audioSubsystem = false;
#endif
} videoCapture;

static bool commandsAdded = false;

static double Video_RealSeconds() {
	return Sys_GetClockTicks() / Sys_ClockTicksPerSecond();
}

static idStr Video_SanitizeName( const char *name ) {
	idStr clean;
	for ( const char *c = name; *c != '\0'; ++c ) {
		const unsigned char ch = (unsigned char)*c;
		if ( ( ch >= 'a' && ch <= 'z' ) || ( ch >= 'A' && ch <= 'Z' ) || ( ch >= '0' && ch <= '9' ) || ch == '-' || ch == '_' || ch == '.' || ch >= 0x80 ) {
			clean.Append( (char)ch );
		} else {
			clean.Append( '_' );
		}
	}
	while ( clean.Length() > 0 && clean[clean.Length() - 1] == '.' ) {
		clean.CapLength( clean.Length() - 1 );
	}
	return clean;
}

static idStr Video_DefaultName() {
	idStr map = sessLocal.currentMapName;
	map.StripPath();
	map.StripFileExtension();
	if ( !sessLocal.mapSpawned || map.Length() == 0 ) {
		map = "openq4";
	}
	const time_t now = time( NULL );
	struct tm local;
#if defined( _WIN32 )
	localtime_s( &local, &now );
#else
	localtime_r( &now, &local );
#endif
	char stamp[32];
	strftime( stamp, sizeof( stamp ), "%Y%m%d-%H%M%S", &local );
	return map + "_" + stamp;
}

static bool Video_FileExists( const std::string &path ) {
	FILE *file = VideoEncoder_OpenFile( path, "rb" );
	if ( file != NULL ) {
		fclose( file );
		return true;
	}
	return false;
}

static void Video_CloseMonitor() {
#if defined( USE_SDL3 )
	if ( videoCapture.monitor != NULL ) {
		SDL_DestroyAudioStream( videoCapture.monitor );
		videoCapture.monitor = NULL;
	}
	if ( videoCapture.audioSubsystem ) {
		SDL_QuitSubSystem( SDL_INIT_AUDIO );
		videoCapture.audioSubsystem = false;
	}
#endif
}

// The loopback device plays nothing: the recording passes its sound on.
static void Video_OpenMonitor() {
#if defined( USE_SDL3 )
	if ( !SDL_InitSubSystem( SDL_INIT_AUDIO ) ) {
		common->Warning( "Video capture: cannot play the game's sound while recording (%s)", SDL_GetError() );
		return;
	}
	videoCapture.audioSubsystem = true;
	SDL_AudioSpec spec;
	spec.format = SDL_AUDIO_F32;
	spec.channels = 2;
	spec.freq = VIDEO_AUDIO_RATE;
	videoCapture.monitor = SDL_OpenAudioDeviceStream( SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL );
	if ( videoCapture.monitor == NULL ) {
		common->Warning( "Video capture: cannot play the game's sound while recording (%s)", SDL_GetError() );
		Video_CloseMonitor();
		return;
	}
	SDL_ResumeAudioStreamDevice( videoCapture.monitor );
#endif
}

static void Video_Monitor( const float *samples, int frames ) {
#if defined( USE_SDL3 )
	if ( videoCapture.monitor == NULL || frames <= 0 ) {
		return;
	}
	// a recording slower than real time must not let the speakers fall behind
	const int queued = SDL_GetAudioStreamQueued( videoCapture.monitor );
	if ( queued > VIDEO_AUDIO_RATE * 8 / 4 ) {
		SDL_ClearAudioStream( videoCapture.monitor );
	}
	SDL_PutAudioStreamData( videoCapture.monitor, samples, frames * 2 * (int)sizeof( float ) );
#else
	(void)samples;
	(void)frames;
#endif
}

static void Video_SubmitAudio( std::vector<float> &samples ) {
	if ( videoCapture.pipeline != NULL ) {
		videoCapture.pipeline->SubmitAudio( samples );
	} else {
		videoCapture.pendingAudio.insert( videoCapture.pendingAudio.end(), samples.begin(), samples.end() );
		samples.clear();
	}
}

static void Video_RenderAudio( long long targetSamples ) {
	long long count = targetSamples - videoCapture.samplesRendered;
	if ( !videoCapture.settings.audio || count <= 0 ) {
		videoCapture.samplesRendered = targetSamples > videoCapture.samplesRendered ? targetSamples : videoCapture.samplesRendered;
		return;
	}
	if ( count > VIDEO_AUDIO_RATE ) {
		count = VIDEO_AUDIO_RATE;
	}
	std::vector<float> samples( (size_t)count * 2 );
	Sound_RenderVideoLoopback( samples.data(), (int)count );
	videoCapture.samplesRendered = targetSamples;
	Video_Monitor( samples.data(), (int)count );
	Video_SubmitAudio( samples );
}

static idVideoEncoder *Video_CreateEncoder( videoEncoderChoice_t choice ) {
	switch ( choice ) {
		case VIDEO_ENCODER_MEDIA_FOUNDATION: return VideoEncoder_CreateMediaFoundation();
		case VIDEO_ENCODER_FFMPEG: return VideoEncoder_CreateFFmpeg();
		default: return VideoEncoder_CreateAVI();
	}
}

static bool Video_StartPipeline( int width, int height ) {
	int outHeight = height;
	if ( com_videoHeight.GetInteger() > 0 && com_videoHeight.GetInteger() < height ) {
		outHeight = com_videoHeight.GetInteger();
	} else if ( com_videoHeight.GetInteger() == 0 && videoCapture.settings.container == "gif" && height > 480 ) {
		outHeight = 480;
	}
	int outWidth = (int)floor( (double)width * outHeight / height + 0.5 );
	outWidth &= ~1;
	outHeight &= ~1;
	if ( outWidth < 16 || outHeight < 16 ) {
		videoCapture.failure = "the window is too small to record";
		return false;
	}
	videoCapture.settings.width = outWidth;
	videoCapture.settings.height = outHeight;
	videoCapture.encoder = Video_CreateEncoder( videoCapture.encoderChoice );
	if ( videoCapture.encoder == NULL ) {
		videoCapture.failure = "this build has no such encoder";
		return false;
	}
	int workers = (int)std::thread::hardware_concurrency() - 2;
	workers = workers < 1 ? 1 : ( workers > 6 ? 6 : workers );
	videoCapture.pipeline = new idVideoPipeline();
	videoCapture.pipeline->Start( videoCapture.encoder, videoCapture.settings, workers );
	if ( !videoCapture.pendingAudio.empty() ) {
		videoCapture.pipeline->SubmitAudio( videoCapture.pendingAudio );
		videoCapture.pendingAudio.clear();
	}
	return true;
}

static void Video_HandleFrame( const renderVideoFrame_t &frame ) {
	if ( frame.pixels == NULL || frame.width <= 0 || frame.height <= 0 ) {
		return;
	}
	videoCapture.framesSinceArrival = 0;
	if ( videoCapture.pipeline == NULL && videoCapture.failure.empty() ) {
		if ( !Video_StartPipeline( frame.width, frame.height ) ) {
			return;
		}
	}
	if ( videoCapture.pipeline == NULL ) {
		return;
	}
	const long long slot = frame.tag;
	if ( slot <= videoCapture.lastWrittenSlot ) {
		return;
	}
	long long count = slot - videoCapture.lastWrittenSlot;
	videoCapture.lastWrittenSlot = slot;
	if ( count > (long long)videoCapture.fps * 30 ) {
		count = (long long)videoCapture.fps * 30;
	}
	videoJob_t *job = new videoJob_t();
	job->rawWidth = frame.width;
	job->rawHeight = frame.height;
	job->topDown = frame.topDown;
	job->bgra = frame.bgra;
	job->count = (int)count;
	job->raw.assign( frame.pixels, frame.pixels + (size_t)frame.width * frame.height * 4 );
	videoCapture.pipeline->SubmitVideo( job );
}

// Hands every finished read to the encoder; wait drains them all (stop).
static void Video_CollectFrames( bool wait ) {
	renderVideoFrame_t frame;
	for ( int guard = 0; guard < 16; ++guard ) {
		const int result = renderSystem->PollVideoFrame( frame, wait );
		if ( result != 1 ) {
			return;
		}
		Video_HandleFrame( frame );
		renderSystem->ReleaseVideoFrame();
	}
}

static void Video_RequestFrame( long long tag ) {
	if ( renderSystem->RequestVideoFrame( tag ) ) {
		return;
	}
	// every read is still in flight: take the oldest one, waiting for it
	renderVideoFrame_t frame;
	if ( renderSystem->PollVideoFrame( frame, true ) == 1 ) {
		Video_HandleFrame( frame );
		renderSystem->ReleaseVideoFrame();
	}
	renderSystem->RequestVideoFrame( tag );
}

static const char *Video_CodecLabel( const std::string &codec ) {
	if ( codec == "h264" ) return "H.264";
	if ( codec == "hevc" ) return "HEVC";
	if ( codec == "av1" ) return "AV1";
	if ( codec == "vp9" ) return "VP9";
	if ( codec == "prores" ) return "ProRes";
	if ( codec == "mjpeg" ) return "Motion JPEG";
	if ( codec == "gif" ) return "GIF";
	return codec.c_str();
}

static void Video_BeginRecording( bool withAudio ) {
	videoCapture.settings.audio = withAudio;
	videoCapture.state = VIDEO_CAPTURE_RECORDING;
	videoCapture.clockSeconds = 0.0;
	videoCapture.lastRealSeconds = Video_RealSeconds();
	videoCapture.nextFixedFrame = 0;
	videoCapture.lastRequestedSlot = -1;
	videoCapture.lastWrittenSlot = -1;
	videoCapture.samplesRendered = 0;
	videoCapture.framesSinceArrival = 0;
	videoCapture.pendingAudio.clear();
	videoCapture.failure.clear();
	com_videoFixedStep.SetBool( videoCapture.fixed );
	if ( withAudio ) {
		Video_OpenMonitor();
	}
	common->Printf( "Recording video to %s (%s, %d fps, %s timing%s)\n", videoCapture.settings.path.c_str(),
		Video_CodecLabel( videoCapture.settings.codec ), videoCapture.fps, videoCapture.fixed ? "fixed" : "real-time",
		withAudio ? ", with sound" : ", no sound" );
	console->ClearNotifyLines();
}

static void Video_Cleanup() {
	if ( videoCapture.pipeline != NULL ) {
		videoCapture.pipeline->Join();
		delete videoCapture.pipeline;
		videoCapture.pipeline = NULL;
	}
	delete videoCapture.encoder;
	videoCapture.encoder = NULL;
	videoCapture.pendingAudio.clear();
	videoCapture.state = VIDEO_CAPTURE_IDLE;
}

static void Video_ReportFinished() {
	std::string why;
	const bool failed = videoCapture.pipeline->Failed( why );
	const long long frames = videoCapture.pipeline->FramesWritten();
	Video_Cleanup();
	if ( failed ) {
		common->Warning( "Video capture: %s", why.c_str() );
		if ( !Video_FileExists( videoCapture.settings.path ) ) {
			return;
		}
	}
	long long bytes = 0;
	FILE *file = VideoEncoder_OpenFile( videoCapture.settings.path, "rb" );
	if ( file != NULL ) {
#if defined( _WIN32 )
		_fseeki64( file, 0, SEEK_END );
		bytes = _ftelli64( file );
#else
		fseeko( file, 0, SEEK_END );
		bytes = (long long)ftello( file );
#endif
		fclose( file );
	}
	const int seconds = (int)( frames / ( videoCapture.fps > 0 ? videoCapture.fps : 60 ) );
	common->Printf( "Saved video %s (%lld frames, %d:%02d, %.1f MB)\n", videoCapture.settings.path.c_str(), frames,
		seconds / 60, seconds % 60, bytes / ( 1024.0 * 1024.0 ) );
}

bool VideoCapture_Start( const char *name, int fixedTics ) {
	if ( renderSystem == NULL || renderSystem->GetScreenWidth() <= 0 ) {
		common->Printf( "Video capture needs a running renderer.\n" );
		return false;
	}
	if ( videoCapture.state == VIDEO_CAPTURE_FINISHING ) {
		common->Printf( "Still saving the previous video; try again in a moment.\n" );
		return false;
	}
	if ( videoCapture.state != VIDEO_CAPTURE_IDLE ) {
		common->Printf( "Already recording video (stopVideo ends it).\n" );
		return false;
	}

	videoEncodeSettings_t settings;
	settings.container = com_videoFormat.GetString();
	std::string codec = com_videoCodec.GetString();
	const std::string container = settings.container;
	if ( codec == "auto" ) {
		codec = container == "webm" ? "vp9" : container == "avi" ? "mjpeg" : container == "gif" ? "gif" : "h264";
	}
	if ( container == "gif" ) {
		codec = "gif";
	} else if ( container == "avi" && codec != "mjpeg" ) {
		common->Printf( "Video capture: AVI recordings are Motion JPEG.\n" );
		codec = "mjpeg";
	} else if ( container == "webm" && codec != "vp9" && codec != "av1" ) {
		common->Printf( "Video capture: WebM holds VP9 or AV1; using VP9.\n" );
		codec = "vp9";
	} else if ( codec == "prores" && container != "mov" && container != "mkv" ) {
		common->Printf( "Video capture: ProRes needs MOV or MKV; using H.264.\n" );
		codec = "h264";
	} else if ( codec == "mjpeg" && container != "mkv" && container != "mov" ) {
		codec = "h264";
	} else if ( codec == "gif" ) {
		codec = "h264";
	}
	settings.codec = codec;

	// the encoder
	const std::string wanted = com_videoEncoder.GetString();
	videoEncoderChoice_t choice = VIDEO_ENCODER_FFMPEG;
	if ( settings.container == "avi" && wanted != "ffmpeg" ) {
		choice = VIDEO_ENCODER_BUILTIN;
	} else if ( wanted == "builtin" ) {
		common->Printf( "Video capture: the built-in encoder writes Motion JPEG AVI files.\n" );
		choice = VIDEO_ENCODER_BUILTIN;
	} else if ( ( wanted == "mediafoundation" || wanted == "auto" ) && settings.container == "mp4" &&
		( codec == "h264" || codec == "hevc" ) && VideoEncoder_MediaFoundationAvailable() ) {
		choice = VIDEO_ENCODER_MEDIA_FOUNDATION;
	} else if ( wanted == "mediafoundation" ) {
		common->Printf( "Video capture: Media Foundation records H.264 or HEVC MP4 on Windows only; trying FFmpeg.\n" );
	}
	if ( choice == VIDEO_ENCODER_FFMPEG ) {
		std::string ffmpeg;
#if !defined( USE_SDL3 )
		common->Printf( "Video capture: this build cannot run FFmpeg; recording Motion JPEG AVI instead.\n" );
		choice = VIDEO_ENCODER_BUILTIN;
#else
		if ( !VideoEncoder_FindFFmpeg( com_videoFFmpeg.GetString(), ffmpeg ) ) {
			common->Printf( "Video capture: %s needs FFmpeg, which was not found (set com_videoFFmpeg, or put ffmpeg next to the game "
				"or on PATH); recording Motion JPEG AVI instead.\n", container.c_str() );
			choice = VIDEO_ENCODER_BUILTIN;
		} else {
			settings.ffmpegPath = ffmpeg;
		}
#endif
	}
	if ( choice == VIDEO_ENCODER_BUILTIN ) {
		settings.container = "avi";
		settings.codec = "mjpeg";
	}

	// timing
	const int hz = common->GetUserCmdHz();
	int fps = com_videoFPS.GetInteger();
	const bool online = idAsyncNetwork::IsActive();
	bool fixed = false;
	int tics = 0;
	if ( fixedTics > 0 ) {
		fixed = true;
		tics = fixedTics;
		fps = Max( 1, hz / tics );
	} else {
		const idStr timing = com_videoTiming.GetString();
		if ( settings.container == "gif" && fps > 30 ) {
			fps = 30;
		}
		const bool divides = fps > 0 && hz % fps == 0;
		if ( timing == "fixed" && online ) {
			common->Printf( "Video capture: online games record in real time.\n" );
		} else if ( timing == "fixed" && !divides ) {
			common->Printf( "Video capture: %d fps is not a divisor of the game's %d Hz; recording in real time.\n", fps, hz );
		}
		fixed = timing != "realtime" && !online && divides;
		tics = fixed ? hz / fps : 0;
	}
	settings.fps = fps;
	settings.quality = com_videoQuality.GetInteger();
	settings.bitrateKbps = com_videoBitrate.GetInteger();
	settings.audioRate = VIDEO_AUDIO_RATE;
	settings.audioChannels = 2;
	settings.audioBitrateKbps = com_videoAudioBitrate.GetInteger();
	settings.hardware = com_videoHardware.GetBool();
	settings.preset = com_videoPreset.GetString();
	if ( settings.preset == "auto" ) {
		settings.preset = fixed ? "fast" : "veryfast";
	}

	// the file
	idStr fileName = Video_SanitizeName( name != NULL && name[0] != '\0' ? name : Video_DefaultName().c_str() );
	if ( fileName.Length() == 0 ) {
		fileName = Video_DefaultName();
	}
	idStr folder = com_videoFolder.GetString();
	folder.BackSlashesToSlashes();
	folder.StripTrailing( '/' );
	if ( folder.Length() == 0 || folder.Find( ".." ) >= 0 || folder[0] == '/' || folder.Find( ':' ) >= 0 ) {
		folder = "videos";
	}
	std::string path;
	for ( int attempt = 1; attempt < 1000; ++attempt ) {
		const idStr relative = attempt == 1 ? va( "%s/%s.%s", folder.c_str(), fileName.c_str(), settings.container.c_str() ) :
			va( "%s/%s_%d.%s", folder.c_str(), fileName.c_str(), attempt, settings.container.c_str() );
		path = fileSystem->RelativePathToOSPath( relative.c_str(), "fs_savepath" );
		if ( !Video_FileExists( path ) ) {
			break;
		}
	}
	fileSystem->CreateOSPath( path.c_str() );
	settings.path = path;

	videoCapture.settings = settings;
	videoCapture.encoderChoice = choice;
	videoCapture.fixed = fixed;
	videoCapture.fixedTics = tics;
	videoCapture.fps = fps;
	videoCapture.failure.clear();

	const bool wantAudio = com_videoAudio.GetBool() && settings.container != "gif";
	if ( wantAudio && Sound_SetVideoLoopback( true, VIDEO_AUDIO_RATE ) ) {
		videoCapture.state = VIDEO_CAPTURE_WAITING_FOR_SOUND;
		videoCapture.waitStartMsec = Sys_Milliseconds();
		return true;
	}
	if ( wantAudio ) {
		common->Printf( "Video capture: recording without sound (sound is off, or OpenAL cannot render to a loopback device).\n" );
	}
	Video_BeginRecording( false );
	return true;
}

void VideoCapture_Stop( void ) {
	switch ( videoCapture.state ) {
		case VIDEO_CAPTURE_IDLE:
		case VIDEO_CAPTURE_FINISHING:
			return;
		case VIDEO_CAPTURE_WAITING_FOR_SOUND:
			Sound_SetVideoLoopback( false, 0 );
			videoCapture.state = VIDEO_CAPTURE_IDLE;
			common->Printf( "Video capture cancelled.\n" );
			return;
		default:
			break;
	}
	// every frame already read goes into the file
	Video_CollectFrames( true );
	renderSystem->EndVideoCapture();
	if ( videoCapture.settings.audio ) {
		Sound_SetVideoLoopback( false, 0 );
	}
	Video_CloseMonitor();
	com_videoFixedStep.SetBool( false );
	if ( videoCapture.pipeline == NULL ) {
		Video_Cleanup();
		common->Warning( "Video capture: %s", videoCapture.failure.empty() ? "no frames were recorded" : videoCapture.failure.c_str() );
		return;
	}
	if ( !videoCapture.pendingAudio.empty() ) {
		videoCapture.pipeline->SubmitAudio( videoCapture.pendingAudio );
		videoCapture.pendingAudio.clear();
	}
	videoCapture.pipeline->Finish();
	videoCapture.state = VIDEO_CAPTURE_FINISHING;
	common->Printf( "Video recording stopped; finishing %s...\n", videoCapture.settings.path.c_str() );
}

bool VideoCapture_Active( void ) {
	return videoCapture.state == VIDEO_CAPTURE_WAITING_FOR_SOUND || videoCapture.state == VIDEO_CAPTURE_RECORDING;
}

int VideoCapture_FixedTicsPerFrame( void ) {
	return videoCapture.state == VIDEO_CAPTURE_RECORDING && videoCapture.fixed ? videoCapture.fixedTics : 0;
}

void VideoCapture_BeforePresent( void ) {
	switch ( videoCapture.state ) {
		case VIDEO_CAPTURE_IDLE:
			return;
		case VIDEO_CAPTURE_FINISHING:
			if ( videoCapture.pipeline == NULL || videoCapture.pipeline->Done() ) {
				Video_ReportFinished();
			}
			return;
		case VIDEO_CAPTURE_WAITING_FOR_SOUND: {
			const soundVideoLoopback_t loopback = Sound_VideoLoopbackState();
			if ( loopback == SOUND_VIDEO_LOOPBACK_ACTIVE ) {
				Video_BeginRecording( true );
			} else if ( loopback == SOUND_VIDEO_LOOPBACK_OFF && Sys_Milliseconds() - videoCapture.waitStartMsec > 100 ) {
				common->Printf( "Video capture: the sound device could not switch to recording; recording without sound.\n" );
				Sound_SetVideoLoopback( false, 0 );
				Video_BeginRecording( false );
			} else if ( Sys_Milliseconds() - videoCapture.waitStartMsec > 5000 ) {
				common->Printf( "Video capture: the sound device did not switch to recording in time; recording without sound.\n" );
				Sound_SetVideoLoopback( false, 0 );
				Video_BeginRecording( false );
			} else {
				return;
			}
			break;
		}
		default:
			break;
	}

	std::string why;
	if ( videoCapture.pipeline != NULL && videoCapture.pipeline->Failed( why ) ) {
		VideoCapture_Stop();
		return;
	}
	if ( !videoCapture.failure.empty() ) {
		VideoCapture_Stop();
		return;
	}
	std::string opened;
	if ( videoCapture.pipeline != NULL && videoCapture.pipeline->TakeOpenedDescription( opened ) ) {
		common->Printf( "Video encoder: %s, %dx%d\n", opened.c_str(), videoCapture.settings.width, videoCapture.settings.height );
		console->ClearNotifyLines();
	}
	// a renderer that never hands frames back cannot record (counted in
	// frames: a level load between two frames is no failure)
	if ( ++videoCapture.framesSinceArrival > 240 ) {
		common->Warning( "Video capture: the renderer is not returning frames (r_frontBuffer, or a backend without frame readback)" );
		VideoCapture_Stop();
		return;
	}

	if ( videoCapture.fixed ) {
		const long long frame = videoCapture.nextFixedFrame++;
		Video_RenderAudio( ( frame + 1 ) * VIDEO_AUDIO_RATE / videoCapture.fps );
		Video_RequestFrame( frame );
	} else {
		const double now = Video_RealSeconds();
		double delta = now - videoCapture.lastRealSeconds;
		videoCapture.lastRealSeconds = now;
		// a load or a stall pauses the recording instead of filling it with one frame
		if ( delta < 0.0 ) {
			delta = 0.0;
		} else if ( delta > 0.25 ) {
			delta = 1.0 / videoCapture.fps;
		}
		videoCapture.clockSeconds += delta;
		Video_RenderAudio( (long long)( videoCapture.clockSeconds * VIDEO_AUDIO_RATE ) );
		const long long slot = (long long)floor( videoCapture.clockSeconds * videoCapture.fps );
		if ( slot > videoCapture.lastRequestedSlot ) {
			videoCapture.lastRequestedSlot = slot;
			Video_RequestFrame( slot );
		}
	}
}

void VideoCapture_AfterPresent( void ) {
	if ( videoCapture.state != VIDEO_CAPTURE_RECORDING ) {
		return;
	}
	Video_CollectFrames( false );
}

static void Video_Record_f( const idCmdArgs &args ) {
	VideoCapture_Start( args.Argc() > 1 ? args.Argv( 1 ) : NULL, 0 );
}

static void Video_Stop_f( const idCmdArgs &args ) {
	if ( !VideoCapture_Active() ) {
		common->Printf( "Not recording video.\n" );
		return;
	}
	VideoCapture_Stop();
}

static void Video_Toggle_f( const idCmdArgs &args ) {
	if ( VideoCapture_Active() ) {
		VideoCapture_Stop();
	} else {
		VideoCapture_Start( args.Argc() > 1 ? args.Argv( 1 ) : NULL, 0 );
	}
}

static void Video_Encoders_f( const idCmdArgs &args ) {
	std::string ffmpeg;
	const bool haveFFmpeg = VideoEncoder_FindFFmpeg( com_videoFFmpeg.GetString(), ffmpeg );
	common->Printf( "Video capture encoders:\n" );
#if defined( _WIN32 )
	common->Printf( "  mediafoundation  %s\n", VideoEncoder_MediaFoundationAvailable() ? "available: H.264/HEVC + AAC in MP4" :
		"unavailable (Windows N editions need the Media Feature Pack)" );
#endif
#if defined( USE_SDL3 )
	common->Printf( "  ffmpeg           %s%s\n", haveFFmpeg ? "found at " : "not found (set com_videoFFmpeg, or put ffmpeg next to the game or on PATH)",
		haveFFmpeg ? ffmpeg.c_str() : "" );
#endif
	common->Printf( "  builtin          available: Motion JPEG + PCM in AVI\n" );
	common->Printf( "Current: %s, codec %s, encoder %s, %d fps, quality %d, timing %s\n", com_videoFormat.GetString(),
		com_videoCodec.GetString(), com_videoEncoder.GetString(), com_videoFPS.GetInteger(), com_videoQuality.GetInteger(),
		com_videoTiming.GetString() );
}

void VideoCapture_Init( void ) {
	if ( commandsAdded ) {
		return;
	}
	commandsAdded = true;
	cmdSystem->AddCommand( "recordVideo", Video_Record_f, CMD_FL_SYSTEM, "records a video of the game with its sound: recordVideo [name]" );
	cmdSystem->AddCommand( "stopVideo", Video_Stop_f, CMD_FL_SYSTEM, "stops recording video and saves the file" );
	cmdSystem->AddCommand( "toggleVideo", Video_Toggle_f, CMD_FL_SYSTEM, "starts or stops recording video (for a key binding)" );
	cmdSystem->AddCommand( "videoEncoders", Video_Encoders_f, CMD_FL_SYSTEM, "lists the video capture encoders this system has" );
}

void VideoCapture_Shutdown( void ) {
	VideoCapture_Stop();
	if ( videoCapture.state == VIDEO_CAPTURE_FINISHING && videoCapture.pipeline != NULL ) {
		common->Printf( "Waiting for the video file to finish...\n" );
		while ( !videoCapture.pipeline->Done() ) {
			Sys_Sleep( 10 );
		}
		Video_ReportFinished();
	}
	com_videoFixedStep.SetBool( false );
}

#else

// Dedicated servers draw nothing to record.
void VideoCapture_Init( void ) {}
void VideoCapture_Shutdown( void ) {}
bool VideoCapture_Start( const char *, int ) { return false; }
void VideoCapture_Stop( void ) {}
bool VideoCapture_Active( void ) { return false; }
int VideoCapture_FixedTicsPerFrame( void ) { return 0; }
void VideoCapture_BeforePresent( void ) {}
void VideoCapture_AfterPresent( void ) {}

#endif
