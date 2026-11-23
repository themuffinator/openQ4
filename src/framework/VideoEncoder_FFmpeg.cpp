/*
===========================================================================

openQ4 video capture: FFmpeg encoder
Copyright (C) 2026 DarkMatter Productions

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

===========================================================================
*/

/*
Every container and codec FFmpeg knows, through the player's own copy of the
ffmpeg program (openQ4 does not ship it): raw RGBA frames go down a pipe
(SDL3's process API), FFmpeg converts them to BT.709 YUV and encodes.

Sound cannot share the one pipe, so it is written beside the video as a WAV
file while recording; closing the recording muxes the two into the requested
file, copying the video and encoding the sound (AAC, or Opus for WebM).

GPU encoders are used only after a test encode proves them on this machine
(an FFmpeg build lists NVENC whether or not an NVIDIA card is present), and
the result is remembered for the session.
*/

#if !defined( ID_DEDICATED )

#include "VideoEncoder.h"

#if defined( USE_SDL3 )

#include <SDL3/SDL_process.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_error.h>

#include <cstring>
#include <cstdint>
#include <map>
#include <mutex>

namespace {

std::mutex ffmpegProbeLock;
std::map<std::string, bool> ffmpegProbeResults;

/*
A child ffmpeg with stdin (optional) and stdout+stderr as pipes. SDL3 makes
the pipes non-blocking, so writes loop until everything is taken and drain
FFmpeg's output meanwhile: unread output would fill its pipe and stall it.
*/
class idFFmpegProcess {
public:
	~idFFmpegProcess() { Kill(); }

	bool Start( const std::vector<std::string> &arguments, bool withInput, std::string &error ) {
		std::vector<const char *> argv;
		for ( const std::string &argument : arguments ) {
			argv.push_back( argument.c_str() );
		}
		argv.push_back( NULL );
		SDL_PropertiesID props = SDL_CreateProperties();
		SDL_SetPointerProperty( props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)argv.data() );
		SDL_SetNumberProperty( props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, withInput ? SDL_PROCESS_STDIO_APP : SDL_PROCESS_STDIO_NULL );
		SDL_SetNumberProperty( props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP );
		SDL_SetBooleanProperty( props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true );
#if defined( _WIN32 )
		// no console window; Windows keeps the exit code of a background child
		SDL_SetBooleanProperty( props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true );
#endif
		process = SDL_CreateProcessWithProperties( props );
		SDL_DestroyProperties( props );
		if ( process == NULL ) {
			error = std::string( "could not start FFmpeg: " ) + SDL_GetError();
			return false;
		}
		input = withInput ? SDL_GetProcessInput( process ) : NULL;
		output = SDL_GetProcessOutput( process );
		return true;
	}

	void Drain() {
		if ( output == NULL ) {
			return;
		}
		char buffer[4096];
		for ( ;; ) {
			const size_t got = SDL_ReadIO( output, buffer, sizeof( buffer ) );
			if ( got == 0 ) {
				break;
			}
			if ( log.size() < 64 * 1024 ) {
				log.append( buffer, got );
			}
		}
	}

	bool Exited( int &code ) {
		if ( process == NULL ) {
			code = -1;
			return true;
		}
		return SDL_WaitProcess( process, false, &code );
	}

	bool Write( const void *data, size_t size ) {
		const unsigned char *bytes = static_cast<const unsigned char *>( data );
		while ( size > 0 ) {
			if ( input == NULL ) {
				return false;
			}
			const size_t wrote = SDL_WriteIO( input, bytes, size );
			bytes += wrote;
			size -= wrote;
			if ( size == 0 ) {
				break;
			}
			if ( wrote == 0 ) {
				if ( SDL_GetIOStatus( input ) == SDL_IO_STATUS_ERROR ) {
					return false;
				}
				int code = 0;
				Drain();
				if ( Exited( code ) ) {
					return false;
				}
				SDL_Delay( 1 );
			}
		}
		return true;
	}

	// Closes stdin and waits; returns the exit code (-1 when killed or lost).
	int Finish( int timeoutMsec ) {
		if ( process == NULL ) {
			return -1;
		}
		if ( input != NULL ) {
			SDL_CloseIO( input );
			input = NULL;
		}
		const Uint64 deadline = SDL_GetTicks() + (Uint64)timeoutMsec;
		int code = -1;
		for ( ;; ) {
			Drain();
			if ( SDL_WaitProcess( process, false, &code ) ) {
				break;
			}
			if ( SDL_GetTicks() > deadline ) {
				SDL_KillProcess( process, true );
				SDL_WaitProcess( process, true, &code );
				code = -1;
				break;
			}
			SDL_Delay( 5 );
		}
		Drain();
		SDL_DestroyProcess( process );
		process = NULL;
		output = NULL;
		return code;
	}

	void Kill() {
		if ( process != NULL ) {
			SDL_KillProcess( process, true );
			int code = 0;
			SDL_WaitProcess( process, true, &code );
			SDL_DestroyProcess( process );
			process = NULL;
			input = output = NULL;
		}
	}

	std::string LastLine() const {
		size_t end = log.find_last_not_of( "\r\n \t" );
		if ( end == std::string::npos ) {
			return std::string();
		}
		const size_t start = log.find_last_of( "\r\n", end );
		return log.substr( start == std::string::npos ? 0 : start + 1, end - ( start == std::string::npos ? 0 : start + 1 ) + 1 );
	}

private:
	SDL_Process *process = NULL;
	SDL_IOStream *input = NULL;
	SDL_IOStream *output = NULL;
	std::string log;
};

std::string Int( int value ) {
	return std::to_string( value );
}

// Whether this FFmpeg can encode with encoder on this machine.
bool FFmpeg_EncoderWorks( const std::string &ffmpeg, const std::string &encoder, const std::vector<std::string> &options ) {
	const std::string key = ffmpeg + "|" + encoder;
	{
		std::lock_guard<std::mutex> lock( ffmpegProbeLock );
		std::map<std::string, bool>::const_iterator found = ffmpegProbeResults.find( key );
		if ( found != ffmpegProbeResults.end() ) {
			return found->second;
		}
	}
	std::vector<std::string> arguments = { ffmpeg, "-hide_banner", "-nostdin", "-loglevel", "error",
		"-f", "lavfi", "-i", "color=c=gray:s=640x360:r=30:d=0.5", "-frames:v", "8", "-c:v", encoder };
	arguments.insert( arguments.end(), options.begin(), options.end() );
	arguments.insert( arguments.end(), { "-f", "null", "-" } );
	idFFmpegProcess probe;
	std::string error;
	bool works = probe.Start( arguments, false, error ) && probe.Finish( 15000 ) == 0;
	std::lock_guard<std::mutex> lock( ffmpegProbeLock );
	ffmpegProbeResults[key] = works;
	return works;
}

class idVideoEncoderFFmpeg : public idVideoEncoder {
public:
	~idVideoEncoderFFmpeg() override {
		std::string ignored;
		Close( ignored );
	}

	const char *Name() const override { return "ffmpeg"; }
	std::string Description() const override {
		std::string text = codecLabel + " (" + encoderName + ", FFmpeg)";
		if ( settings.audio ) {
			text += settings.container == "webm" ? " + Opus" : " + AAC";
		}
		return text;
	}

	bool Open( const videoEncodeSettings_t &openSettings, std::string &error ) override {
		settings = openSettings;
		if ( settings.container == "gif" ) {
			settings.audio = false;
		}
		std::vector<std::string> codecArguments;
		if ( !ChooseVideoCodec( codecArguments, error ) ) {
			return false;
		}
		videoPath = settings.path;
		if ( settings.audio ) {
			const size_t dot = settings.path.find_last_of( '.' );
			const std::string stem = dot == std::string::npos ? settings.path : settings.path.substr( 0, dot );
			const std::string extension = dot == std::string::npos ? std::string() : settings.path.substr( dot );
			videoPath = stem + ".video-part" + extension;
			audioPath = stem + ".audio-part.wav";
			audioFile = VideoEncoder_OpenFile( audioPath, "wb" );
			if ( audioFile == NULL ) {
				error = "could not create " + audioPath;
				return false;
			}
			WriteWaveHeader();
		}
		std::vector<std::string> arguments = { settings.ffmpegPath, "-hide_banner", "-nostats", "-loglevel", "error", "-y",
			"-f", "rawvideo", "-pix_fmt", "rgba", "-s", Int( settings.width ) + "x" + Int( settings.height ),
			"-r", Int( settings.fps ), "-i", "pipe:0" };
		arguments.insert( arguments.end(), codecArguments.begin(), codecArguments.end() );
		arguments.push_back( "-an" );
		if ( !settings.audio && ( settings.container == "mp4" || settings.container == "mov" ) ) {
			arguments.insert( arguments.end(), { "-movflags", "+faststart" } );
		}
		arguments.push_back( videoPath );
		if ( !process.Start( arguments, true, error ) ) {
			return false;
		}
		return true;
	}

	void *PrepareFrame( std::vector<unsigned char> &rgba ) override {
		std::vector<unsigned char> *frame = new std::vector<unsigned char>();
		frame->swap( rgba );
		return frame;
	}

	void FreeFrame( void *prepared ) override {
		delete static_cast<std::vector<unsigned char> *>( prepared );
	}

	bool WriteFrame( void *prepared, int count, std::string &error ) override {
		const std::vector<unsigned char> &frame = *static_cast<std::vector<unsigned char> *>( prepared );
		for ( int i = 0; i < count; ++i ) {
			if ( !process.Write( frame.data(), frame.size() ) ) {
				process.Drain();
				const std::string reason = process.LastLine();
				error = "FFmpeg stopped encoding" + ( reason.empty() ? std::string() : ": " + reason );
				return false;
			}
		}
		process.Drain();
		return true;
	}

	bool WriteAudio( const float *interleaved, int frames, std::string &error ) override {
		if ( audioFile == NULL || frames <= 0 ) {
			return true;
		}
		pcm.resize( (size_t)frames * settings.audioChannels );
		for ( size_t i = 0; i < pcm.size(); ++i ) {
			float value = interleaved[i] * 32767.0f;
			value = value > 32767.0f ? 32767.0f : ( value < -32768.0f ? -32768.0f : value );
			pcm[i] = (int16_t)( value < 0.0f ? value - 0.5f : value + 0.5f );
		}
		const size_t bytes = pcm.size() * sizeof( int16_t );
		if ( fwrite( pcm.data(), 1, bytes, audioFile ) != bytes ) {
			error = "could not write " + audioPath + " (disk full?)";
			return false;
		}
		audioBytes += bytes;
		return true;
	}

	bool Close( std::string &error ) override {
		if ( closed ) {
			return true;
		}
		closed = true;
		bool ok = true;
		// a long recording can leave FFmpeg several seconds of queued encoding
		const int code = process.Finish( 10 * 60 * 1000 );
		if ( code != 0 ) {
			const std::string reason = process.LastLine();
			error = "FFmpeg failed (exit code " + Int( code ) + ")" + ( reason.empty() ? std::string() : ": " + reason );
			ok = false;
		}
		if ( audioFile != NULL ) {
			FinishWaveHeader();
			fclose( audioFile );
			audioFile = NULL;
			if ( ok ) {
				ok = Mux( error );
			}
			VideoEncoder_RemoveFile( audioPath );
		}
		return ok;
	}

private:
	videoEncodeSettings_t settings;
	idFFmpegProcess process;
	std::string encoderName;
	std::string codecLabel;
	std::string videoPath;
	std::string audioPath;
	FILE *audioFile = NULL;
	uint64_t audioBytes = 0;
	std::vector<int16_t> pcm;
	bool closed = false;

	void WriteU32( uint32_t value ) {
		const unsigned char bytes[4] = { (unsigned char)value, (unsigned char)( value >> 8 ), (unsigned char)( value >> 16 ), (unsigned char)( value >> 24 ) };
		fwrite( bytes, 1, 4, audioFile );
	}
	void WriteU16( uint16_t value ) {
		const unsigned char bytes[2] = { (unsigned char)value, (unsigned char)( value >> 8 ) };
		fwrite( bytes, 1, 2, audioFile );
	}

	void WriteWaveHeader() {
		const uint32_t blockAlign = (uint32_t)settings.audioChannels * 2;
		fwrite( "RIFF", 1, 4, audioFile );
		WriteU32( 0 );
		fwrite( "WAVEfmt ", 1, 8, audioFile );
		WriteU32( 16 );
		WriteU16( 1 );
		WriteU16( (uint16_t)settings.audioChannels );
		WriteU32( (uint32_t)settings.audioRate );
		WriteU32( (uint32_t)settings.audioRate * blockAlign );
		WriteU16( (uint16_t)blockAlign );
		WriteU16( 16 );
		fwrite( "data", 1, 4, audioFile );
		WriteU32( 0 );
	}

	void FinishWaveHeader() {
		// past 4 GiB the sizes saturate; FFmpeg reads a saturated WAV to its end
		const uint64_t data = audioBytes;
		const uint32_t dataSize = data > 0xFFFFFFF0ull ? 0xFFFFFFFFu : (uint32_t)data;
		const uint32_t riffSize = data > 0xFFFFFFF0ull ? 0xFFFFFFFFu : (uint32_t)( data + 36 );
		fseek( audioFile, 4, SEEK_SET );
		WriteU32( riffSize );
		fseek( audioFile, 40, SEEK_SET );
		WriteU32( dataSize );
	}

	bool Mux( std::string &error ) {
		std::vector<std::string> arguments = { settings.ffmpegPath, "-hide_banner", "-nostats", "-nostdin", "-loglevel", "error", "-y",
			"-i", videoPath, "-i", audioPath, "-map", "0:v:0", "-map", "1:a:0", "-c:v", "copy" };
		if ( settings.container == "webm" ) {
			arguments.insert( arguments.end(), { "-c:a", "libopus" } );
		} else {
			arguments.insert( arguments.end(), { "-c:a", "aac" } );
		}
		arguments.insert( arguments.end(), { "-b:a", Int( settings.audioBitrateKbps ) + "k" } );
		if ( settings.container == "mp4" || settings.container == "mov" ) {
			arguments.insert( arguments.end(), { "-movflags", "+faststart" } );
		}
		arguments.push_back( settings.path );
		idFFmpegProcess mux;
		if ( !mux.Start( arguments, false, error ) ) {
			return KeepVideoOnly( error );
		}
		const int code = mux.Finish( 10 * 60 * 1000 );
		if ( code != 0 ) {
			const std::string reason = mux.LastLine();
			error = "adding the sound failed" + ( reason.empty() ? std::string() : ": " + reason );
			return KeepVideoOnly( error );
		}
		VideoEncoder_RemoveFile( videoPath );
		return true;
	}

	// The sound could not be added: keep the picture rather than lose the take.
	bool KeepVideoOnly( std::string &error ) {
		VideoEncoder_RenameFile( videoPath, settings.path );
		error += " (saved without sound)";
		return false;
	}

	bool TryEncoder( const std::string &name, const std::vector<std::string> &options, std::vector<std::string> &codecArguments ) {
		if ( !FFmpeg_EncoderWorks( settings.ffmpegPath, name, options ) ) {
			return false;
		}
		encoderName = name;
		codecArguments = { "-c:v", name };
		codecArguments.insert( codecArguments.end(), options.begin(), options.end() );
		return true;
	}

	bool ChooseVideoCodec( std::vector<std::string> &codecArguments, std::string &error ) {
		const int crf = VideoEncoder_QualityToCRF( settings.quality );
		const std::string bitrate = Int( settings.bitrateKbps ) + "k";
		const bool useBitrate = settings.bitrateKbps > 0;
		std::vector<std::string> colour = { "-vf", "scale=out_color_matrix=bt709:out_range=tv,format=yuv420p",
			"-colorspace", "bt709", "-color_primaries", "bt709", "-color_trc", "bt709", "-color_range", "tv" };
		const std::string &codec = settings.codec;
		bool chosen = false;
		if ( codec == "gif" ) {
			codecLabel = "GIF";
			encoderName = "gif";
			codecArguments = { "-vf", "split[a][b];[a]palettegen=stats_mode=diff[p];[b][p]paletteuse=dither=sierra2_4a", "-loop", "0", "-c:v", "gif" };
			return true;
		}
		if ( codec == "prores" ) {
			codecLabel = "ProRes 422 HQ";
			chosen = TryEncoder( "prores_ks", { "-profile:v", "3", "-vendor", "apl0" }, codecArguments );
			if ( !chosen ) {
				error = "this FFmpeg cannot encode ProRes";
				return false;
			}
			codecArguments.insert( codecArguments.end(), { "-vf", "scale=out_color_matrix=bt709:out_range=tv,format=yuv422p10le",
				"-colorspace", "bt709", "-color_primaries", "bt709", "-color_trc", "bt709", "-color_range", "tv" } );
			return true;
		}
		if ( codec == "mjpeg" ) {
			codecLabel = "Motion JPEG";
			const int qscale = 2 + ( 100 - settings.quality ) / 8;
			chosen = TryEncoder( "mjpeg", { "-q:v", Int( qscale ), "-pix_fmt", "yuvj420p" }, codecArguments );
			if ( !chosen ) {
				error = "this FFmpeg cannot encode Motion JPEG";
			}
			return chosen;
		}
		std::vector<std::pair<std::string, std::vector<std::string>>> candidates;
		const std::string cq = Int( crf );
		if ( codec == "h264" ) {
			codecLabel = "H.264";
			if ( settings.hardware ) {
#if defined( __APPLE__ )
				candidates.push_back( { "h264_videotoolbox", { "-q:v", Int( settings.quality ), "-profile:v", "high" } } );
#else
				candidates.push_back( { "h264_nvenc", { "-preset", "p5", "-tune", "hq", "-rc", "vbr", "-cq", cq, "-b:v", "0", "-profile:v", "high" } } );
				candidates.push_back( { "h264_amf", { "-quality", "quality", "-rc", "cqp", "-qp_i", cq, "-qp_p", cq, "-qp_b", cq } } );
				candidates.push_back( { "h264_qsv", { "-preset", "medium", "-global_quality", cq } } );
#endif
			}
			candidates.push_back( { "libx264", { "-preset", settings.preset, "-crf", cq, "-profile:v", "high" } } );
			candidates.push_back( { "libopenh264", { "-b:v", Int( VideoEncoder_QualityToKbps( settings.quality, settings.width, settings.height, settings.fps, codec ) ) + "k" } } );
		} else if ( codec == "hevc" ) {
			codecLabel = "HEVC";
			const std::string hcq = Int( crf + 2 > 51 ? 51 : crf + 2 );
			if ( settings.hardware ) {
#if defined( __APPLE__ )
				candidates.push_back( { "hevc_videotoolbox", { "-q:v", Int( settings.quality ) } } );
#else
				candidates.push_back( { "hevc_nvenc", { "-preset", "p5", "-tune", "hq", "-rc", "vbr", "-cq", hcq, "-b:v", "0" } } );
				candidates.push_back( { "hevc_amf", { "-quality", "quality", "-rc", "cqp", "-qp_i", hcq, "-qp_p", hcq } } );
				candidates.push_back( { "hevc_qsv", { "-preset", "medium", "-global_quality", hcq } } );
#endif
			}
			candidates.push_back( { "libx265", { "-preset", settings.preset, "-crf", hcq } } );
		} else if ( codec == "av1" ) {
			codecLabel = "AV1";
			const int acrf = crf + 10 > 63 ? 63 : crf + 10;
			// only NVENC's AV1 takes the same quality scale; the others' are untested
			if ( settings.hardware ) {
				candidates.push_back( { "av1_nvenc", { "-preset", "p5", "-tune", "hq", "-rc", "vbr", "-cq", Int( acrf ), "-b:v", "0" } } );
			}
			candidates.push_back( { "libsvtav1", { "-preset", "8", "-crf", Int( acrf ) } } );
			candidates.push_back( { "libaom-av1", { "-cpu-used", "8", "-row-mt", "1", "-crf", Int( acrf ), "-b:v", "0" } } );
		} else if ( codec == "vp9" ) {
			codecLabel = "VP9";
			const int vcrf = crf + 13 > 63 ? 63 : crf + 13;
			candidates.push_back( { "libvpx-vp9", { "-crf", Int( vcrf ), "-b:v", "0", "-deadline", "realtime", "-cpu-used", "6", "-row-mt", "1" } } );
		} else {
			error = "unknown codec " + codec;
			return false;
		}
		for ( auto &candidate : candidates ) {
			std::vector<std::string> options = candidate.second;
			if ( useBitrate ) {
				// an explicit bitrate replaces the quality controls
				options.clear();
				options.insert( options.end(), { "-b:v", bitrate, "-maxrate", bitrate } );
				if ( candidate.first == "libx264" || candidate.first == "libx265" ) {
					options.insert( options.end(), { "-preset", settings.preset, "-bufsize", Int( settings.bitrateKbps * 2 ) + "k" } );
				}
			}
			if ( TryEncoder( candidate.first, options, codecArguments ) ) {
				chosen = true;
				break;
			}
		}
		if ( !chosen ) {
			error = "this FFmpeg has no working " + codecLabel + " encoder";
			return false;
		}
		codecArguments.insert( codecArguments.end(), colour.begin(), colour.end() );
		codecArguments.insert( codecArguments.end(), { "-g", Int( settings.fps * 2 ) } );
		if ( codec == "hevc" && ( settings.container == "mp4" || settings.container == "mov" ) ) {
			// Apple players want the hvc1 tag
			codecArguments.insert( codecArguments.end(), { "-tag:v", "hvc1" } );
		}
		return true;
	}
};

}	// namespace

idVideoEncoder *VideoEncoder_CreateFFmpeg( void ) {
	return new idVideoEncoderFFmpeg();
}

#else

idVideoEncoder *VideoEncoder_CreateFFmpeg( void ) {
	return NULL;
}

#endif

#endif
