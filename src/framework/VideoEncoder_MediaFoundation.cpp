/*
===========================================================================

openQ4 video capture: Windows Media Foundation MP4 writer
Copyright (C) 2026 DarkMatter Productions

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

===========================================================================
*/

/*
The MP4 encoder every Windows installation already has: Media Foundation's
sink writer muxes H.264 (or HEVC, where its extension is installed) with AAC
sound, and with hardware transforms enabled it encodes on the GPU's own
encoder (NVENC, AMF, Quick Sync) when the driver registers one.

Media Foundation is loaded when a recording opens, not linked: Windows N
editions without the Media Feature Pack lack mfplat.dll, and the game must
still start there (the capture then falls back to FFmpeg or the built-in AVI
writer). mfuuid is a static library of GUIDs and is safe to link.

Frames are converted to NV12 on the capture's worker threads with the BT.709
limited-range matrix the file is tagged with, so the colours do not depend on
which colour converter the sink writer would otherwise insert.
*/

#if !defined( ID_DEDICATED )

#include "VideoEncoder.h"

#if defined( _WIN32 )

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// idlib pins WINVER to XP; the Media Foundation headers gate the sink writer on
// Windows 7 and later and must agree with the SDK's NTDDI_VERSION
#if defined( WINVER ) && WINVER < 0x0A00
#undef WINVER
#define WINVER 0x0A00
#endif
#if defined( _WIN32_WINNT ) && _WIN32_WINNT < 0x0A00
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <codecapi.h>

#include <cstring>
#include <cstdint>

#if defined( _MSC_VER )
#pragma comment( lib, "mfuuid.lib" )
#endif

namespace {

typedef HRESULT ( STDAPICALLTYPE *MFStartup_t )( ULONG, DWORD );
typedef HRESULT ( STDAPICALLTYPE *MFShutdown_t )( void );
typedef HRESULT ( STDAPICALLTYPE *MFCreateAttributes_t )( IMFAttributes **, UINT32 );
typedef HRESULT ( STDAPICALLTYPE *MFCreateMediaType_t )( IMFMediaType ** );
typedef HRESULT ( STDAPICALLTYPE *MFCreateMemoryBuffer_t )( DWORD, IMFMediaBuffer ** );
typedef HRESULT ( STDAPICALLTYPE *MFCreateSample_t )( IMFSample ** );
typedef HRESULT ( STDAPICALLTYPE *MFCreateSinkWriterFromURL_t )( LPCWSTR, IMFByteStream *, IMFAttributes *, IMFSinkWriter ** );

struct mfProcs_t {
	HMODULE plat = NULL;
	HMODULE readwrite = NULL;
	MFStartup_t Startup = NULL;
	MFShutdown_t Shutdown = NULL;
	MFCreateAttributes_t CreateAttributes = NULL;
	MFCreateMediaType_t CreateMediaType = NULL;
	MFCreateMemoryBuffer_t CreateMemoryBuffer = NULL;
	MFCreateSample_t CreateSample = NULL;
	MFCreateSinkWriterFromURL_t CreateSinkWriterFromURL = NULL;
};

bool MF_Load( mfProcs_t &mf ) {
	if ( mf.plat == NULL ) {
		mf.plat = LoadLibraryW( L"mfplat.dll" );
	}
	if ( mf.readwrite == NULL ) {
		mf.readwrite = LoadLibraryW( L"mfreadwrite.dll" );
	}
	if ( mf.plat == NULL || mf.readwrite == NULL ) {
		return false;
	}
	mf.Startup = (MFStartup_t)GetProcAddress( mf.plat, "MFStartup" );
	mf.Shutdown = (MFShutdown_t)GetProcAddress( mf.plat, "MFShutdown" );
	mf.CreateAttributes = (MFCreateAttributes_t)GetProcAddress( mf.plat, "MFCreateAttributes" );
	mf.CreateMediaType = (MFCreateMediaType_t)GetProcAddress( mf.plat, "MFCreateMediaType" );
	mf.CreateMemoryBuffer = (MFCreateMemoryBuffer_t)GetProcAddress( mf.plat, "MFCreateMemoryBuffer" );
	mf.CreateSample = (MFCreateSample_t)GetProcAddress( mf.plat, "MFCreateSample" );
	mf.CreateSinkWriterFromURL = (MFCreateSinkWriterFromURL_t)GetProcAddress( mf.readwrite, "MFCreateSinkWriterFromURL" );
	return mf.Startup && mf.Shutdown && mf.CreateAttributes && mf.CreateMediaType && mf.CreateMemoryBuffer &&
		mf.CreateSample && mf.CreateSinkWriterFromURL;
}

void MF_Unload( mfProcs_t &mf ) {
	if ( mf.readwrite != NULL ) {
		FreeLibrary( mf.readwrite );
	}
	if ( mf.plat != NULL ) {
		FreeLibrary( mf.plat );
	}
	mf = mfProcs_t();
}

template<class T> void MF_Release( T *&object ) {
	if ( object != NULL ) {
		object->Release();
		object = NULL;
	}
}

std::wstring MF_Wide( const std::string &utf8 ) {
	const int length = MultiByteToWideChar( CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), NULL, 0 );
	std::wstring wide( length > 0 ? length : 0, L'\0' );
	if ( length > 0 ) {
		MultiByteToWideChar( CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &wide[0], length );
	}
	return wide;
}

std::string MF_Error( const char *what, HRESULT hr ) {
	char text[160];
	_snprintf_s( text, sizeof( text ), _TRUNCATE, "%s failed (0x%08lx)", what, (unsigned long)hr );
	return text;
}

void MF_SetColour( IMFMediaType *type ) {
	type->SetUINT32( MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709 );
	type->SetUINT32( MF_MT_VIDEO_PRIMARIES, MFVideoPrimaries_BT709 );
	type->SetUINT32( MF_MT_TRANSFER_FUNCTION, MFVideoTransFunc_709 );
	type->SetUINT32( MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235 );
}

class idVideoEncoderMF : public idVideoEncoder {
public:
	~idVideoEncoderMF() override {
		std::string ignored;
		Close( ignored );
	}

	const char *Name() const override { return "mediafoundation"; }
	std::string Description() const override {
		std::string text = settings.codec == "hevc" ? "HEVC" : "H.264";
		text += settings.hardware ? " (Media Foundation, hardware when available)" : " (Media Foundation, software)";
		if ( audioStream != (DWORD)-1 ) {
			text += " + AAC";
		}
		return text;
	}

	bool Open( const videoEncodeSettings_t &openSettings, std::string &error ) override {
		settings = openSettings;
		if ( settings.container != "mp4" || ( settings.codec != "h264" && settings.codec != "hevc" ) ) {
			error = "Media Foundation writes only H.264 or HEVC in MP4";
			return false;
		}
		if ( !MF_Load( mf ) ) {
			error = "Media Foundation is not installed (Windows N editions need the Media Feature Pack)";
			MF_Unload( mf );
			return false;
		}
		const HRESULT com = CoInitializeEx( NULL, COINIT_MULTITHREADED );
		comInitialized = SUCCEEDED( com );
		HRESULT hr = mf.Startup( MF_VERSION, MFSTARTUP_LITE );
		if ( FAILED( hr ) ) {
			error = MF_Error( "MFStartup", hr );
			return false;
		}
		started = true;

		IMFAttributes *attributes = NULL;
		hr = mf.CreateAttributes( &attributes, 4 );
		if ( SUCCEEDED( hr ) ) {
			attributes->SetUINT32( MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, settings.hardware ? TRUE : FALSE );
			attributes->SetGUID( MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4 );
			hr = mf.CreateSinkWriterFromURL( MF_Wide( settings.path ).c_str(), NULL, attributes, &writer );
		}
		MF_Release( attributes );
		if ( FAILED( hr ) ) {
			error = MF_Error( "creating the MP4 file", hr );
			return false;
		}
		if ( !AddVideoStream( error ) ) {
			return false;
		}
		if ( settings.audio && !AddAudioStream( error ) ) {
			return false;
		}
		hr = writer->BeginWriting();
		if ( FAILED( hr ) ) {
			error = MF_Error( settings.codec == "hevc" ? "starting the HEVC encoder (is the HEVC extension installed?)" : "starting the H.264 encoder", hr );
			return false;
		}
		frameBytes = (DWORD)settings.width * settings.height * 3 / 2;
		return true;
	}

	void *PrepareFrame( std::vector<unsigned char> &rgba ) override {
		const int width = settings.width;
		const int height = settings.height;
		std::vector<unsigned char> *nv12 = new std::vector<unsigned char>( (size_t)width * height * 3 / 2 );
		unsigned char *luma = nv12->data();
		unsigned char *chroma = luma + (size_t)width * height;
		const unsigned char *src = rgba.data();
		// BT.709, limited range, fixed point (x256)
		for ( int y = 0; y < height; y += 2 ) {
			const unsigned char *row0 = src + (size_t)y * width * 4;
			const unsigned char *row1 = row0 + (size_t)width * 4;
			unsigned char *y0 = luma + (size_t)y * width;
			unsigned char *y1 = y0 + width;
			unsigned char *uv = chroma + (size_t)( y / 2 ) * width;
			for ( int x = 0; x < width; x += 2 ) {
				int r = 0, g = 0, b = 0;
				const unsigned char *pixels[4] = { row0 + x * 4, row0 + x * 4 + 4, row1 + x * 4, row1 + x * 4 + 4 };
				unsigned char *outs[4] = { y0 + x, y0 + x + 1, y1 + x, y1 + x + 1 };
				for ( int i = 0; i < 4; ++i ) {
					const int pr = pixels[i][0], pg = pixels[i][1], pb = pixels[i][2];
					*outs[i] = (unsigned char)( ( ( 47 * pr + 157 * pg + 16 * pb + 128 ) >> 8 ) + 16 );
					r += pr;
					g += pg;
					b += pb;
				}
				// averaged over the 2x2 block (sum / 4, folded into the shift)
				const int cb = ( ( -26 * r - 87 * g + 112 * b + 512 ) >> 10 ) + 128;
				const int cr = ( ( 112 * r - 102 * g - 10 * b + 512 ) >> 10 ) + 128;
				uv[x] = (unsigned char)( cb < 16 ? 16 : ( cb > 240 ? 240 : cb ) );
				uv[x + 1] = (unsigned char)( cr < 16 ? 16 : ( cr > 240 ? 240 : cr ) );
			}
		}
		return nv12;
	}

	void FreeFrame( void *prepared ) override {
		delete static_cast<std::vector<unsigned char> *>( prepared );
	}

	bool WriteFrame( void *prepared, int count, std::string &error ) override {
		const std::vector<unsigned char> &nv12 = *static_cast<std::vector<unsigned char> *>( prepared );
		IMFMediaBuffer *buffer = NULL;
		HRESULT hr = mf.CreateMemoryBuffer( frameBytes, &buffer );
		if ( SUCCEEDED( hr ) ) {
			BYTE *data = NULL;
			hr = buffer->Lock( &data, NULL, NULL );
			if ( SUCCEEDED( hr ) ) {
				memcpy( data, nv12.data(), frameBytes );
				buffer->Unlock();
				hr = buffer->SetCurrentLength( frameBytes );
			}
		}
		for ( int i = 0; i < count && SUCCEEDED( hr ); ++i ) {
			const LONGLONG start = (LONGLONG)( (int64_t)framesWritten * 10000000 / settings.fps );
			const LONGLONG end = (LONGLONG)( (int64_t)( framesWritten + 1 ) * 10000000 / settings.fps );
			IMFSample *sample = NULL;
			hr = mf.CreateSample( &sample );
			if ( SUCCEEDED( hr ) ) hr = sample->AddBuffer( buffer );
			if ( SUCCEEDED( hr ) ) hr = sample->SetSampleTime( start );
			if ( SUCCEEDED( hr ) ) hr = sample->SetSampleDuration( end - start );
			if ( SUCCEEDED( hr ) ) hr = writer->WriteSample( videoStream, sample );
			MF_Release( sample );
			framesWritten++;
		}
		MF_Release( buffer );
		if ( FAILED( hr ) ) {
			error = MF_Error( "encoding a frame", hr );
			return false;
		}
		return true;
	}

	bool WriteAudio( const float *interleaved, int frames, std::string &error ) override {
		if ( audioStream == (DWORD)-1 || frames <= 0 ) {
			return true;
		}
		const DWORD bytes = (DWORD)frames * settings.audioChannels * 2;
		IMFMediaBuffer *buffer = NULL;
		HRESULT hr = mf.CreateMemoryBuffer( bytes, &buffer );
		if ( SUCCEEDED( hr ) ) {
			BYTE *data = NULL;
			hr = buffer->Lock( &data, NULL, NULL );
			if ( SUCCEEDED( hr ) ) {
				int16_t *pcm = reinterpret_cast<int16_t *>( data );
				const int count = frames * settings.audioChannels;
				for ( int i = 0; i < count; ++i ) {
					float value = interleaved[i] * 32767.0f;
					value = value > 32767.0f ? 32767.0f : ( value < -32768.0f ? -32768.0f : value );
					pcm[i] = (int16_t)( value < 0.0f ? value - 0.5f : value + 0.5f );
				}
				buffer->Unlock();
				hr = buffer->SetCurrentLength( bytes );
			}
		}
		const LONGLONG start = (LONGLONG)( (int64_t)samplesWritten * 10000000 / settings.audioRate );
		samplesWritten += frames;
		const LONGLONG end = (LONGLONG)( (int64_t)samplesWritten * 10000000 / settings.audioRate );
		IMFSample *sample = NULL;
		if ( SUCCEEDED( hr ) ) hr = mf.CreateSample( &sample );
		if ( SUCCEEDED( hr ) ) hr = sample->AddBuffer( buffer );
		if ( SUCCEEDED( hr ) ) hr = sample->SetSampleTime( start );
		if ( SUCCEEDED( hr ) ) hr = sample->SetSampleDuration( end - start );
		if ( SUCCEEDED( hr ) ) hr = writer->WriteSample( audioStream, sample );
		MF_Release( sample );
		MF_Release( buffer );
		if ( FAILED( hr ) ) {
			error = MF_Error( "encoding sound", hr );
			return false;
		}
		return true;
	}

	bool Close( std::string &error ) override {
		bool ok = true;
		if ( writer != NULL ) {
			if ( framesWritten > 0 ) {
				const HRESULT hr = writer->Finalize();
				if ( FAILED( hr ) ) {
					error = MF_Error( "finishing the MP4 file", hr );
					ok = false;
				}
			}
			MF_Release( writer );
		}
		if ( started ) {
			mf.Shutdown();
			started = false;
		}
		if ( comInitialized ) {
			CoUninitialize();
			comInitialized = false;
		}
		MF_Unload( mf );
		return ok;
	}

private:
	videoEncodeSettings_t settings;
	mfProcs_t mf;
	bool comInitialized = false;
	bool started = false;
	IMFSinkWriter *writer = NULL;
	DWORD videoStream = (DWORD)-1;
	DWORD audioStream = (DWORD)-1;
	DWORD frameBytes = 0;
	int64_t framesWritten = 0;
	int64_t samplesWritten = 0;

	bool AddVideoStream( std::string &error ) {
		const int kbps = settings.bitrateKbps > 0 ? settings.bitrateKbps :
			VideoEncoder_QualityToKbps( settings.quality, settings.width, settings.height, settings.fps, settings.codec );
		IMFMediaType *output = NULL;
		IMFMediaType *input = NULL;
		HRESULT hr = mf.CreateMediaType( &output );
		if ( SUCCEEDED( hr ) ) {
			output->SetGUID( MF_MT_MAJOR_TYPE, MFMediaType_Video );
			output->SetGUID( MF_MT_SUBTYPE, settings.codec == "hevc" ? MFVideoFormat_HEVC : MFVideoFormat_H264 );
			output->SetUINT32( MF_MT_AVG_BITRATE, (UINT32)kbps * 1000 );
			output->SetUINT32( MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive );
			if ( settings.codec == "h264" ) {
				output->SetUINT32( MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High );
			}
			MFSetAttributeSize( output, MF_MT_FRAME_SIZE, settings.width, settings.height );
			MFSetAttributeRatio( output, MF_MT_FRAME_RATE, settings.fps, 1 );
			MFSetAttributeRatio( output, MF_MT_PIXEL_ASPECT_RATIO, 1, 1 );
			MF_SetColour( output );
			hr = writer->AddStream( output, &videoStream );
		}
		if ( SUCCEEDED( hr ) ) hr = mf.CreateMediaType( &input );
		if ( SUCCEEDED( hr ) ) {
			input->SetGUID( MF_MT_MAJOR_TYPE, MFMediaType_Video );
			input->SetGUID( MF_MT_SUBTYPE, MFVideoFormat_NV12 );
			input->SetUINT32( MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive );
			input->SetUINT32( MF_MT_DEFAULT_STRIDE, (UINT32)settings.width );
			MFSetAttributeSize( input, MF_MT_FRAME_SIZE, settings.width, settings.height );
			MFSetAttributeRatio( input, MF_MT_FRAME_RATE, settings.fps, 1 );
			MFSetAttributeRatio( input, MF_MT_PIXEL_ASPECT_RATIO, 1, 1 );
			MF_SetColour( input );
			// Quality-led variable bitrate around the mean, a keyframe every
			// second for editors and seeking; encoders that refuse the hints
			// take the plain mean bitrate below.
			IMFAttributes *parameters = NULL;
			if ( SUCCEEDED( mf.CreateAttributes( &parameters, 4 ) ) ) {
				parameters->SetUINT32( CODECAPI_AVEncCommonRateControlMode, eAVEncCommonRateControlMode_UnconstrainedVBR );
				parameters->SetUINT32( CODECAPI_AVEncCommonMeanBitRate, (UINT32)kbps * 1000 );
				parameters->SetUINT32( CODECAPI_AVEncMPVGOPSize, (UINT32)settings.fps );
				parameters->SetUINT32( CODECAPI_AVEncCommonQualityVsSpeed, 70 );
			}
			hr = writer->SetInputMediaType( videoStream, input, parameters );
			if ( FAILED( hr ) && parameters != NULL ) {
				hr = writer->SetInputMediaType( videoStream, input, NULL );
			}
			MF_Release( parameters );
		}
		MF_Release( input );
		MF_Release( output );
		if ( FAILED( hr ) ) {
			error = MF_Error( settings.codec == "hevc" ? "setting up HEVC (is the HEVC extension installed?)" : "setting up H.264", hr );
			return false;
		}
		return true;
	}

	bool AddAudioStream( std::string &error ) {
		// the AAC encoder takes four bitrates, in bytes per second
		const int kbps = settings.audioBitrateKbps;
		const UINT32 bytesPerSecond = kbps <= 96 ? 12000 : ( kbps <= 128 ? 16000 : ( kbps <= 160 ? 20000 : 24000 ) );
		const UINT32 blockAlign = (UINT32)settings.audioChannels * 2;
		IMFMediaType *output = NULL;
		IMFMediaType *input = NULL;
		DWORD stream = (DWORD)-1;
		HRESULT hr = mf.CreateMediaType( &output );
		if ( SUCCEEDED( hr ) ) {
			output->SetGUID( MF_MT_MAJOR_TYPE, MFMediaType_Audio );
			output->SetGUID( MF_MT_SUBTYPE, MFAudioFormat_AAC );
			output->SetUINT32( MF_MT_AUDIO_BITS_PER_SAMPLE, 16 );
			output->SetUINT32( MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32)settings.audioRate );
			output->SetUINT32( MF_MT_AUDIO_NUM_CHANNELS, (UINT32)settings.audioChannels );
			output->SetUINT32( MF_MT_AUDIO_AVG_BYTES_PER_SECOND, bytesPerSecond );
			output->SetUINT32( MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29 );
			hr = writer->AddStream( output, &stream );
		}
		if ( SUCCEEDED( hr ) ) hr = mf.CreateMediaType( &input );
		if ( SUCCEEDED( hr ) ) {
			input->SetGUID( MF_MT_MAJOR_TYPE, MFMediaType_Audio );
			input->SetGUID( MF_MT_SUBTYPE, MFAudioFormat_PCM );
			input->SetUINT32( MF_MT_AUDIO_BITS_PER_SAMPLE, 16 );
			input->SetUINT32( MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32)settings.audioRate );
			input->SetUINT32( MF_MT_AUDIO_NUM_CHANNELS, (UINT32)settings.audioChannels );
			input->SetUINT32( MF_MT_AUDIO_BLOCK_ALIGNMENT, blockAlign );
			input->SetUINT32( MF_MT_AUDIO_AVG_BYTES_PER_SECOND, blockAlign * (UINT32)settings.audioRate );
			hr = writer->SetInputMediaType( stream, input, NULL );
		}
		MF_Release( input );
		MF_Release( output );
		if ( FAILED( hr ) ) {
			error = MF_Error( "setting up AAC sound", hr );
			return false;
		}
		audioStream = stream;
		return true;
	}
};

}	// namespace

idVideoEncoder *VideoEncoder_CreateMediaFoundation( void ) {
	return new idVideoEncoderMF();
}

bool VideoEncoder_MediaFoundationAvailable( void ) {
	mfProcs_t mf;
	const bool available = MF_Load( mf );
	MF_Unload( mf );
	return available;
}

#else

idVideoEncoder *VideoEncoder_CreateMediaFoundation( void ) {
	return NULL;
}

bool VideoEncoder_MediaFoundationAvailable( void ) {
	return false;
}

#endif

#endif
