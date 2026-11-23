/*
===========================================================================

openQ4 video capture: built-in Motion JPEG AVI writer
Copyright (C) 2026 DarkMatter Productions

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

===========================================================================
*/

/*
The encoder that needs nothing beyond the game: every frame a JPEG (the JPEG
library the engine already carries), sound as 16-bit PCM, in an AVI that
every player and editor opens. Files are large -- that is the price of
needing no other program -- so the writer follows OpenDML: the file is a
chain of RIFF segments of at most 1 GiB, each indexed by its own standard
index chunks, which a super index in the header lists. The first segment also
carries the legacy idx1 index for old players. Frames compress on the
capture's worker threads; this writer only appends.
*/

#if !defined( ID_DEDICATED )

#include "VideoEncoder.h"

#include <cstring>
#include <cstdint>

extern "C" {
#include "../external/jpeg-6/jpeglib.h"
}

namespace {

const uint64_t AVI_SEGMENT_LIMIT = 1024ull * 1024ull * 1024ull - 64ull * 1024ull * 1024ull;
const int AVI_MAX_SUPER_ENTRIES = 512;	// 512 segments: about half a terabyte
const uint32_t AVIIF_KEYFRAME = 0x10;
// RGB_PIXELSIZE in jmorecfg.h, which jpeglib.h keeps internal
const int JPEG_RGB_SAMPLES = 4;

uint32_t FourCC( const char *code ) {
	return (uint32_t)(unsigned char)code[0] | ( (uint32_t)(unsigned char)code[1] << 8 ) |
		( (uint32_t)(unsigned char)code[2] << 16 ) | ( (uint32_t)(unsigned char)code[3] << 24 );
}

/*
JPEG into memory. jpeg-6 predates jpeg_mem_dest, so the destination manager
grows a vector by hand.
*/
struct jpegMemoryDestination_t {
	jpeg_destination_mgr manager;
	std::vector<unsigned char> *output;
};

void JPEG_InitDestination( j_compress_ptr cinfo ) {
	jpegMemoryDestination_t *dest = reinterpret_cast<jpegMemoryDestination_t *>( cinfo->dest );
	dest->output->resize( 256 * 1024 );
	dest->manager.next_output_byte = dest->output->data();
	dest->manager.free_in_buffer = dest->output->size();
}

boolean JPEG_EmptyOutputBuffer( j_compress_ptr cinfo ) {
	jpegMemoryDestination_t *dest = reinterpret_cast<jpegMemoryDestination_t *>( cinfo->dest );
	const size_t used = dest->output->size();
	dest->output->resize( used * 2 );
	dest->manager.next_output_byte = dest->output->data() + used;
	dest->manager.free_in_buffer = dest->output->size() - used;
	return TRUE;
}

void JPEG_TermDestination( j_compress_ptr cinfo ) {
	jpegMemoryDestination_t *dest = reinterpret_cast<jpegMemoryDestination_t *>( cinfo->dest );
	dest->output->resize( dest->output->size() - dest->manager.free_in_buffer );
}

struct aviIndexEntry_t {
	uint64_t offset;	// of the chunk header
	uint32_t size;		// of the data
};

struct aviSuperEntry_t {
	uint64_t offset;
	uint32_t size;
	uint32_t duration;
};

class idVideoEncoderAVI : public idVideoEncoder {
public:
	~idVideoEncoderAVI() override {
		std::string ignored;
		Close( ignored );
	}

	const char *Name() const override { return "builtin"; }
	std::string Description() const override {
		return settings.audio ? "Motion JPEG + PCM (built-in AVI writer)" : "Motion JPEG (built-in AVI writer)";
	}

	bool Open( const videoEncodeSettings_t &openSettings, std::string &error ) override {
		settings = openSettings;
		jpegQuality = settings.quality < 30 ? 30 : ( settings.quality > 98 ? 98 : settings.quality + 12 );
		if ( jpegQuality > 98 ) {
			jpegQuality = 98;
		}
		file = VideoEncoder_OpenFile( settings.path, "wb" );
		if ( file == NULL ) {
			error = "could not create " + settings.path;
			return false;
		}
		WriteHeaders();
		BeginMovi( true );
		if ( failed ) {
			error = "could not write " + settings.path;
			return false;
		}
		return true;
	}

	void *PrepareFrame( std::vector<unsigned char> &rgba ) override {
		std::vector<unsigned char> *jpeg = new std::vector<unsigned char>();
		jpeg_compress_struct cinfo;
		jpeg_error_mgr jerr;
		memset( &cinfo, 0, sizeof( cinfo ) );
		cinfo.err = jpeg_std_error( &jerr );
		jpeg_create_compress( &cinfo );
		jpegMemoryDestination_t dest;
		dest.output = jpeg;
		dest.manager.init_destination = JPEG_InitDestination;
		dest.manager.empty_output_buffer = JPEG_EmptyOutputBuffer;
		dest.manager.term_destination = JPEG_TermDestination;
		cinfo.dest = &dest.manager;
		cinfo.image_width = settings.width;
		cinfo.image_height = settings.height;
		// openQ4's jpeg-6 is Doom 3's RGBA build (RGB_PIXELSIZE 4): an RGB
		// scanline is four samples a pixel, so the frame goes in as it is
		cinfo.input_components = JPEG_RGB_SAMPLES;
		cinfo.in_color_space = JCS_RGB;
		jpeg_set_defaults( &cinfo );
		jpeg_set_quality( &cinfo, jpegQuality, TRUE );
		jpeg_start_compress( &cinfo, TRUE );
		std::vector<unsigned char> row( (size_t)settings.width * JPEG_RGB_SAMPLES );
		while ( cinfo.next_scanline < cinfo.image_height ) {
			const unsigned char *src = rgba.data() + (size_t)cinfo.next_scanline * settings.width * 4;
			for ( int x = 0; x < settings.width; ++x ) {
				for ( int c = 0; c < JPEG_RGB_SAMPLES; ++c ) {
					row[x * JPEG_RGB_SAMPLES + c] = src[x * 4 + ( c < 4 ? c : 3 )];
				}
			}
			JSAMPROW rows[1] = { row.data() };
			jpeg_write_scanlines( &cinfo, rows, 1 );
		}
		jpeg_finish_compress( &cinfo );
		jpeg_destroy_compress( &cinfo );
		return jpeg;
	}

	void FreeFrame( void *prepared ) override {
		delete static_cast<std::vector<unsigned char> *>( prepared );
	}

	bool WriteFrame( void *prepared, int count, std::string &error ) override {
		const std::vector<unsigned char> &jpeg = *static_cast<std::vector<unsigned char> *>( prepared );
		for ( int i = 0; i < count && !failed; ++i ) {
			WriteChunk( 0, jpeg.data(), (uint32_t)jpeg.size() );
		}
		if ( failed ) {
			error = "could not write " + settings.path + " (disk full?)";
		}
		return !failed;
	}

	bool WriteAudio( const float *interleaved, int frames, std::string &error ) override {
		if ( !settings.audio || frames <= 0 ) {
			return true;
		}
		const int channels = settings.audioChannels;
		pcm.resize( (size_t)frames * channels );
		for ( size_t i = 0; i < pcm.size(); ++i ) {
			float value = interleaved[i] * 32767.0f;
			value = value > 32767.0f ? 32767.0f : ( value < -32768.0f ? -32768.0f : value );
			pcm[i] = (int16_t)( value < 0.0f ? value - 0.5f : value + 0.5f );
		}
		WriteChunk( 1, pcm.data(), (uint32_t)( pcm.size() * sizeof( int16_t ) ) );
		if ( failed ) {
			error = "could not write " + settings.path + " (disk full?)";
		}
		return !failed;
	}

	bool Close( std::string &error ) override {
		if ( file == NULL ) {
			return !failed;
		}
		EndSegment();
		PatchHeaders();
		fflush( file );
		if ( ferror( file ) ) {
			failed = true;
		}
		fclose( file );
		file = NULL;
		if ( failed ) {
			error = "could not finish " + settings.path;
		}
		return !failed;
	}

private:
	videoEncodeSettings_t settings;
	int jpegQuality = 90;
	FILE *file = NULL;
	bool failed = false;
	std::vector<int16_t> pcm;

	// header field positions, patched at the end
	uint64_t riffSizeAt = 0, avihFramesAt = 0, videoLengthAt = 0, audioLengthAt = 0,
		videoSuperAt = 0, audioSuperAt = 0, dmlhFramesAt = 0, avihBufferAt = 0;
	uint32_t largestChunk = 0;

	// the open segment
	bool firstSegment = true;
	uint64_t segmentStart = 0, moviStart = 0;
	std::vector<aviIndexEntry_t> segmentEntries[2];
	std::vector<aviIndexEntry_t> legacyEntries[2];	// first segment, interleaved order kept below
	std::vector<uint8_t> legacyOrder;				// stream of each legacy entry
	uint32_t firstSegmentFrames = 0;

	std::vector<aviSuperEntry_t> superEntries[2];
	uint64_t totalFrames = 0, totalSamples = 0;

	uint64_t Tell() {
#if defined( _WIN32 )
		return (uint64_t)_ftelli64( file );
#else
		return (uint64_t)ftello( file );
#endif
	}
	void Seek( uint64_t offset ) {
#if defined( _WIN32 )
		_fseeki64( file, (long long)offset, SEEK_SET );
#else
		fseeko( file, (off_t)offset, SEEK_SET );
#endif
	}
	void Bytes( const void *data, size_t size ) {
		if ( !failed && size > 0 && fwrite( data, 1, size, file ) != size ) {
			failed = true;
		}
	}
	void U32( uint32_t value ) {
		const unsigned char bytes[4] = { (unsigned char)value, (unsigned char)( value >> 8 ), (unsigned char)( value >> 16 ), (unsigned char)( value >> 24 ) };
		Bytes( bytes, 4 );
	}
	void U16( uint16_t value ) {
		const unsigned char bytes[2] = { (unsigned char)value, (unsigned char)( value >> 8 ) };
		Bytes( bytes, 2 );
	}
	void U64( uint64_t value ) {
		U32( (uint32_t)value );
		U32( (uint32_t)( value >> 32 ) );
	}
	void Tag( const char *code ) { Bytes( code, 4 ); }
	void PatchU32( uint64_t at, uint32_t value ) {
		const uint64_t back = Tell();
		Seek( at );
		U32( value );
		Seek( back );
	}
	void Zeros( size_t count ) {
		static const unsigned char zero[256] = {};
		while ( count > 0 ) {
			const size_t step = count > sizeof( zero ) ? sizeof( zero ) : count;
			Bytes( zero, step );
			count -= step;
		}
	}

	void WriteSuperIndex( const char *chunkId, uint64_t &at ) {
		Tag( "indx" );
		U32( 24 + 16 * AVI_MAX_SUPER_ENTRIES );
		at = Tell();
		U16( 4 );			// wLongsPerEntry
		Bytes( "\0", 1 );	// bIndexSubType
		Bytes( "\0", 1 );	// bIndexType: AVI_INDEX_OF_INDEXES
		U32( 0 );			// nEntriesInUse
		Tag( chunkId );
		Zeros( 12 + 16 * AVI_MAX_SUPER_ENTRIES );
	}

	void WriteHeaders() {
		const int streams = settings.audio ? 2 : 1;
		const uint32_t blockAlign = (uint32_t)settings.audioChannels * 2;

		Tag( "RIFF" );
		riffSizeAt = Tell();
		U32( 0 );
		Tag( "AVI " );

		Tag( "LIST" );
		const uint64_t hdrlSizeAt = Tell();
		U32( 0 );
		Tag( "hdrl" );

		Tag( "avih" );
		U32( 56 );
		U32( (uint32_t)( 1000000 / settings.fps ) );	// dwMicroSecPerFrame
		U32( 0 );										// dwMaxBytesPerSec
		U32( 0 );										// dwPaddingGranularity
		U32( 0x10 | 0x100 );							// AVIF_HASINDEX | AVIF_ISINTERLEAVED
		avihFramesAt = Tell();
		U32( 0 );										// dwTotalFrames (first segment)
		U32( 0 );										// dwInitialFrames
		U32( (uint32_t)streams );
		avihBufferAt = Tell();
		U32( 0 );										// dwSuggestedBufferSize
		U32( (uint32_t)settings.width );
		U32( (uint32_t)settings.height );
		Zeros( 16 );

		// video stream
		Tag( "LIST" );
		const uint64_t videoListAt = Tell();
		U32( 0 );
		Tag( "strl" );
		Tag( "strh" );
		U32( 56 );
		Tag( "vids" );
		Tag( "MJPG" );
		U32( 0 );					// dwFlags
		U32( 0 );					// wPriority, wLanguage
		U32( 0 );					// dwInitialFrames
		U32( 1 );					// dwScale
		U32( (uint32_t)settings.fps );	// dwRate
		U32( 0 );					// dwStart
		videoLengthAt = Tell();
		U32( 0 );					// dwLength
		U32( 0 );					// dwSuggestedBufferSize
		U32( 0xFFFFFFFFu );			// dwQuality
		U32( 0 );					// dwSampleSize
		U16( 0 ); U16( 0 ); U16( (uint16_t)settings.width ); U16( (uint16_t)settings.height );
		Tag( "strf" );
		U32( 40 );
		U32( 40 );
		U32( (uint32_t)settings.width );
		U32( (uint32_t)settings.height );
		U16( 1 );
		U16( 24 );
		Tag( "MJPG" );
		U32( (uint32_t)settings.width * settings.height * 3 );
		Zeros( 16 );
		WriteSuperIndex( "00dc", videoSuperAt );
		PatchU32( videoListAt, (uint32_t)( Tell() - videoListAt - 4 ) );

		if ( settings.audio ) {
			Tag( "LIST" );
			const uint64_t audioListAt = Tell();
			U32( 0 );
			Tag( "strl" );
			Tag( "strh" );
			U32( 56 );
			Tag( "auds" );
			U32( 0 );
			U32( 0 );
			U32( 0 );
			U32( 0 );
			U32( blockAlign );								// dwScale
			U32( (uint32_t)settings.audioRate * blockAlign );	// dwRate: one tick is one sample frame
			U32( 0 );
			audioLengthAt = Tell();
			U32( 0 );
			U32( 0 );
			U32( 0xFFFFFFFFu );
			U32( blockAlign );								// dwSampleSize
			Zeros( 8 );
			Tag( "strf" );
			U32( 18 );
			U16( 1 );										// WAVE_FORMAT_PCM
			U16( (uint16_t)settings.audioChannels );
			U32( (uint32_t)settings.audioRate );
			U32( (uint32_t)settings.audioRate * blockAlign );
			U16( (uint16_t)blockAlign );
			U16( 16 );
			U16( 0 );
			WriteSuperIndex( "01wb", audioSuperAt );
			PatchU32( audioListAt, (uint32_t)( Tell() - audioListAt - 4 ) );
		}

		Tag( "LIST" );
		U32( 4 + 8 + 248 );
		Tag( "odml" );
		Tag( "dmlh" );
		U32( 248 );
		dmlhFramesAt = Tell();
		U32( 0 );
		Zeros( 244 );

		PatchU32( hdrlSizeAt, (uint32_t)( Tell() - hdrlSizeAt - 4 ) );
	}

	void BeginMovi( bool first ) {
		if ( !first ) {
			segmentStart = Tell();
			Tag( "RIFF" );
			U32( 0 );
			Tag( "AVIX" );
		} else {
			segmentStart = riffSizeAt - 4;
		}
		moviStart = Tell();
		Tag( "LIST" );
		U32( 0 );
		Tag( "movi" );
		firstSegment = first;
		segmentEntries[0].clear();
		segmentEntries[1].clear();
	}

	void WriteStandardIndex( int stream ) {
		const std::vector<aviIndexEntry_t> &entries = segmentEntries[stream];
		if ( entries.empty() ) {
			return;
		}
		const uint64_t at = Tell();
		const uint32_t size = 24 + 8 * (uint32_t)entries.size();
		Tag( stream == 0 ? "ix00" : "ix01" );
		U32( size );
		U16( 2 );			// wLongsPerEntry
		Bytes( "\0", 1 );	// bIndexSubType
		Bytes( "\1", 1 );	// bIndexType: AVI_INDEX_OF_CHUNKS
		U32( (uint32_t)entries.size() );
		Tag( stream == 0 ? "00dc" : "01wb" );
		U64( moviStart );	// qwBaseOffset
		U32( 0 );
		uint64_t duration = 0;
		const uint32_t blockAlign = (uint32_t)settings.audioChannels * 2;
		for ( const aviIndexEntry_t &entry : entries ) {
			U32( (uint32_t)( entry.offset + 8 - moviStart ) );
			U32( entry.size );
			duration += stream == 0 ? 1 : entry.size / blockAlign;
		}
		aviSuperEntry_t super;
		super.offset = at;
		super.size = size + 8;
		super.duration = (uint32_t)duration;
		if ( superEntries[stream].size() < (size_t)AVI_MAX_SUPER_ENTRIES ) {
			superEntries[stream].push_back( super );
		} else {
			failed = true;
		}
	}

	void EndSegment() {
		WriteStandardIndex( 0 );
		if ( settings.audio ) {
			WriteStandardIndex( 1 );
		}
		const uint64_t moviEnd = Tell();
		PatchU32( moviStart + 4, (uint32_t)( moviEnd - moviStart - 8 ) );
		if ( firstSegment ) {
			// legacy index: offsets from the 'movi' fourcc
			Tag( "idx1" );
			U32( (uint32_t)legacyOrder.size() * 16 );
			size_t next[2] = { 0, 0 };
			for ( uint8_t stream : legacyOrder ) {
				const aviIndexEntry_t &entry = legacyEntries[stream][next[stream]++];
				Tag( stream == 0 ? "00dc" : "01wb" );
				U32( AVIIF_KEYFRAME );
				U32( (uint32_t)( entry.offset - ( moviStart + 8 ) ) );
				U32( entry.size );
			}
			legacyEntries[0].clear();
			legacyEntries[1].clear();
			legacyOrder.clear();
		}
		const uint64_t end = Tell();
		PatchU32( segmentStart + 4, (uint32_t)( end - segmentStart - 8 ) );
	}

	void WriteChunk( int stream, const void *data, uint32_t size ) {
		if ( failed || file == NULL ) {
			return;
		}
		const uint64_t projected = Tell() + size + 16 + 8 * ( segmentEntries[0].size() + segmentEntries[1].size() + 2 ) +
			( firstSegment ? 16 * ( legacyOrder.size() + 1 ) : 0 );
		if ( projected - segmentStart > AVI_SEGMENT_LIMIT ) {
			EndSegment();
			BeginMovi( false );
		}
		aviIndexEntry_t entry;
		entry.offset = Tell();
		entry.size = size;
		Tag( stream == 0 ? "00dc" : "01wb" );
		U32( size );
		Bytes( data, size );
		if ( size & 1 ) {
			Bytes( "\0", 1 );
		}
		segmentEntries[stream].push_back( entry );
		if ( firstSegment ) {
			legacyEntries[stream].push_back( entry );
			legacyOrder.push_back( (uint8_t)stream );
			if ( stream == 0 ) {
				firstSegmentFrames++;
			}
		}
		if ( stream == 0 ) {
			totalFrames++;
		} else {
			totalSamples += size / ( (uint32_t)settings.audioChannels * 2 );
		}
		if ( size > largestChunk ) {
			largestChunk = size;
		}
	}

	void PatchSuperIndex( uint64_t at, const std::vector<aviSuperEntry_t> &entries ) {
		const uint64_t back = Tell();
		Seek( at + 4 );
		U32( (uint32_t)entries.size() );
		Seek( at + 24 );
		for ( const aviSuperEntry_t &entry : entries ) {
			U64( entry.offset );
			U32( entry.size );
			U32( entry.duration );
		}
		Seek( back );
	}

	void PatchHeaders() {
		PatchU32( avihFramesAt, firstSegmentFrames );
		PatchU32( avihBufferAt, largestChunk + 8 );
		PatchU32( videoLengthAt, (uint32_t)totalFrames );
		PatchU32( dmlhFramesAt, (uint32_t)totalFrames );
		PatchSuperIndex( videoSuperAt, superEntries[0] );
		if ( settings.audio ) {
			PatchU32( audioLengthAt, (uint32_t)totalSamples );
			PatchSuperIndex( audioSuperAt, superEntries[1] );
		}
	}
};

}	// namespace

idVideoEncoder *VideoEncoder_CreateAVI( void ) {
	return new idVideoEncoderAVI();
}

#endif
