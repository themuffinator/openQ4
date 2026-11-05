/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company.
Copyright (C) 2013 Robert Beckebans

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/
#ifndef __AL_SOUNDSAMPLE_H__
#define __AL_SOUNDSAMPLE_H__

/*
================================================
idSoundSample_OpenAL
================================================
*/
class idSampleInfo;
struct idSoundSampleDecode;
class SoundVorbisStream;
class idJobList;
struct idJobContext;
// What a streamed sample's load recorded about its file: the decode must agree.
struct idSoundOggStreamShape {
	int				channels = 0;
	int				rate = 0;
	unsigned int	frames = 0;
	// A stream without length metadata: the length and last positioned page its
	// sample's envelope pass found (SoundVorbisStream::SetCountedLength).
	bool			countedLength = false;
	unsigned int	pageStart = 0, pageEnd = 0, pageGranule = 0;
};
// One voice's cursor in a streamed Ogg. The main thread admits the stream and opens
// its file; parsing the Vorbis headers, seeking and decoding run a block at a time on
// a job worker, which alone touches the decoder and file while a block is pending.
class idSoundOggStream {
public:
	// Interleaved samples of one queue buffer (one 4096-frame 5.1 block), which also
	// holds a centre-routed mono block (checked against SoundQueue.h).
	static const int BLOCK_SAMPLES = 24576;
	enum blockState_t { BLOCK_IDLE, BLOCK_PENDING, BLOCK_READY, BLOCK_FAILED };
	idSoundOggStream();
	~idSoundOggStream();
	// Main thread: admission and the file open only. A failure leaves BLOCK_FAILED.
	bool Open( const char* filename, const idSoundOggStreamShape& shape );
	// Main thread: frees the decoder, file and reservation, joining a pending block.
	void Close();
	// Main thread: decode frames (at most BLOCK_SAMPLES / channels) from frame. It runs
	// on a job worker, or here when none accepts it. False when it failed at once or a
	// block is still outstanding.
	bool RequestBlock( unsigned int frame, int frames );
	// Main thread: joins a finished block without waiting.
	blockState_t PollBlock();
	blockState_t BlockState() const { return state; }
	bool IsBlockPending() const { return state == BLOCK_PENDING; }
	// A ready block: what was asked for, and the frames it holds (fewer only at a damaged end).
	unsigned int BlockFrame() const { return requestFrame; }
	int BlockRequestFrames() const { return requestFrames; }
	int BlockFrames() const { return state == BLOCK_READY ? decodedFrames : 0; }
	const short* Block() const { return state == BLOCK_READY ? block : NULL; }
	void ConsumeBlock();
	int Channels() const { return shape.channels; }
	// Main thread: deletes the stream, or cancels its pending block and keeps it until
	// the worker has let go (ReapReleased).
	static void Release( idSoundOggStream* stream );
	static void ReapReleased( bool wait );
	static int ReleasedCount();
	static void PrintMemoryInfo();
private:
	idSoundOggStream( const idSoundOggStream& ) = delete;
	idSoundOggStream& operator=( const idSoundOggStream& ) = delete;
	void Decode( const idJobContext* context );
	void DecodeBlock( const idJobContext* context );
	void FinishBlock();
	static void DecodeJob( const idJobContext& context );
	enum decodeOutcome_t { DECODE_NONE, DECODE_READY, DECODE_OPEN_FAILED, DECODE_SHAPE_MISMATCH, DECODE_SEEK_FAILED, DECODE_EMPTY };
	idStr name;
	idFile* file;
	SoundVorbisStream* decoder;
	idJobList* list;
	idSoundOggStreamShape shape;
	blockState_t state;
	unsigned int fileLength;
	int requestTime;
	// Written by the decode, read by the main thread only once it is joined.
	bool decoderOpen;
	decodeOutcome_t outcome;
	int decoderError;
	unsigned int position;
	unsigned int requestFrame;
	int requestFrames;
	int decodedFrames;
	int decodeMs;		// worker time of the last block, opening and seeking included
	short block[BLOCK_SAMPLES];
};
class idSoundSample_OpenAL
{
public:
	idSoundSample_OpenAL();

	// Loads and initializes the resource based on the name.
	virtual void	 LoadResource();
	// LoadResource for a level load: an Ogg's PCM decodes on a job worker (see the .cpp).
	void			LoadResourceAsync();
	// Waits for an outstanding decode and uploads the sample; nothing without one.
	void			FinishDecode();
	bool			IsDecoding() const
	{
		return pendingDecode != NULL;
	}
	// a worker has finished the outstanding decode; FinishDecode will not wait
	bool			IsDecodeComplete() const;
	static void		PrintDecodeMemoryInfo();

	void			SetName( const char* n )
	{
		name = n;
	}
	const char* 	GetName() const
	{
		return name;
	}
	ID_TIME_T		GetTimestamp() const
	{
		return timestamp;
	}

	// turns it into a beep
	void			MakeDefault();

	// frees all data
	void			FreeData();

	int				LengthInMsec() const
	{
		return SamplesToMsec( NumSamples(), SampleRate() );
	}
	int				SampleRate() const
	{
		return format.basic.samplesPerSec;
	}
	int				NumSamples() const
	{
		return playLength;
	}
	int				NumChannels() const
	{
		return format.basic.numChannels;
	}
	int				BufferSize() const
	{
		return totalBufferSize;
	}
	// What this sample still costs in CPU memory. Diverges from BufferSize once
	// the payload has been handed to OpenAL and released, and the two must not
	// be conflated: listSounds reports both so the saving is visible.
	int				ResidentBufferSize() const
	{
		return payloadReleased || IsStreamed() ? 0 : totalBufferSize;
	}
	bool IsStreamed() const { return streamName.Length() > 0; }
	bool			PayloadReleased() const
	{
		return payloadReleased;
	}
	// Drops the CPU copy of an uploaded sample, keeping the buffer sizes.
	void			ReleaseCpuPayload();
	// CPU PCM retained beside a complete OpenAL copy: restored for a reader, or kept
	// with s_releaseSamplePayload 0.
	bool			HasCpuCopy() const
	{
		return !payloadReleased && openalBuffer != 0 && pendingDecode == NULL &&
			buffers.Num() == 1 && buffers[0].buffer != NULL;
	}
	// Memory pressure: frees that copy even when a reader restored it; the next
	// reader restores it again. Returns the bytes freed.
	int				ReleaseCpuCopy();
	// Reloads a released payload. Needed before any path that reads the PCM
	// bytes rather than just their sizes. Returns false if it cannot be had.
	bool			EnsureCpuPayload();

	const byte*		GetNonCacheData() const
	{
		// Callers want the bytes, so restoring a released payload is part of
		// answering; returning NULL instead would be a silent wrong answer.
		if( !const_cast< idSoundSample_OpenAL* >( this )->EnsureCpuPayload() )
		{
			return NULL;
		}
		return buffers.Num() > 0 ? reinterpret_cast<const byte*>( buffers[0].buffer ) : NULL;
	}

	bool			IsCompressed() const
	{
		return IsStreamed() || ( format.basic.formatTag != idWaveFile::FORMAT_PCM );
	}

	bool			IsDefault() const
	{
		return timestamp == FILE_NOT_FOUND_TIMESTAMP;
	}
	bool			IsLoaded() const
	{
		return loaded;
	}

	void			SetNeverPurge()
	{
		neverPurge = true;
	}
	bool			GetNeverPurge() const
	{
		return neverPurge;
	}

	void			SetLevelLoadReferenced()
	{
		levelLoadReferenced = true;
	}
	void			ResetLevelLoadReferenced()
	{
		levelLoadReferenced = false;
	}
	bool			GetLevelLoadReferenced() const
	{
		return levelLoadReferenced;
	}

	int				GetLastPlayedTime() const
	{
		return lastPlayedTime;
	}
	void			SetLastPlayedTime( int t )
	{
		lastPlayedTime = t;
	}

	float			GetAmplitude( int timeMS ) const;

#if 0 //defined(AL_SOFT_buffer_samples)
	const char*		OpenALSoftChannelsName( ALenum chans ) const;

	const char*		OpenALSoftTypeName( ALenum type ) const;

	ALsizei			FramesToBytes( ALsizei size, ALenum channels, ALenum type ) const;
	ALsizei			BytesToFrames( ALsizei size, ALenum channels, ALenum type ) const;

	/* Retrieves a compatible buffer format given the channel configuration and
	 * sample type. If an alIsBufferFormatSupportedSOFT-compatible function is
	 * provided, it will be called to find the closest-matching format from
	 * AL_SOFT_buffer_samples. Returns AL_NONE (0) if no supported format can be
	 * found. */
	ALenum			GetOpenALSoftFormat( ALenum channels, ALenum type ) const;
#endif

	ALenum			GetOpenALBufferFormat() const;

	void			CreateOpenALBuffer();

protected:
	friend class idSoundHardware_OpenAL;
	friend class idSoundVoice_OpenAL;

	~idSoundSample_OpenAL();

	bool			LoadWav( const idStr& name );
	bool			LoadOgg( const idStr& name );
	bool			BeginOggLoad( const idStr& name, idSoundSampleDecode** asyncDecode );
	bool			FinishOggDecode( const idStr& name, unsigned int decodedFrames );
	bool			FinishOggLoad( int channels, int sampleRate, unsigned int frames );
	bool			LoadOggStream( const idStr& filename );
	// LoadOggStream for a level load: the envelope pass runs on a job worker.
	bool			BeginOggStreamEnvelope( const idStr& filename, idFile* file, int channels, int rate, unsigned int frames, idSoundSampleDecode** asyncDecode );
	void			CancelDecode();
	bool			LoadRoQ( const idStr& name );
	bool			LoadAmplitude( const idStr& name );
	void			WriteAllSamples( const idStr& sampleName );
	bool			LoadGeneratedSample( const idStr& name );
	void			WriteGeneratedSample( idFile* fileOut );

	struct MS_ADPCM_decodeState_t
	{
		uint8 hPredictor;
		int16 coef1;
		int16 coef2;

		uint16 iDelta;
		int16 iSamp1;
		int16 iSamp2;
	};

	int32			MS_ADPCM_nibble( MS_ADPCM_decodeState_t* state, int8 nybble );
	int				MS_ADPCM_decode( uint8** audio_buf, uint32* audio_len );

	struct sampleBuffer_t
	{
		void* buffer;
		int bufferSize;
		int numSamples;
	};

	idStr			name;
	idStr			streamName;
	// A stream without length metadata: its envelope pass counted the length, and
	// each voice's decoder seeks with it and the last page that carries a position.
	bool			streamCountedLength;
	unsigned int	streamPageStart;
	unsigned int	streamPageEnd;
	unsigned int	streamPageGranule;

	ID_TIME_T		timestamp;
	bool			loaded;

	bool			neverPurge;
	bool			levelLoadReferenced;
	bool			usesMapHeap;

	uint32			lastPlayedTime;

	int				totalBufferSize;	// total size of all the buffers
	idList<sampleBuffer_t> buffers;

	// alBufferData copies the PCM into OpenAL's own storage, so once a sample
	// has been uploaded to openalBuffer the copy in buffers[] is a second,
	// identical resident copy of every byte. Releasing it frees the payload
	// while keeping the sampleBuffer_t sizes, which the playback paths still
	// need to resolve offsets. See ReleaseCpuPayload.
	bool			payloadReleased;

	// Sticky: set once a sample has been found to need its PCM bytes after all,
	// so the reload that restores them is not undone by the upload at the end of
	// LoadResource, and so the same sample cannot cost a reload twice.
	bool			keepPayload;

	// OpenAL buffer that contains all buffers
	ALuint			openalBuffer;
	bool			openalBufferUploadFailed;
	uint64			openalReserved;

	int				playBegin;
	int				playLength;

	idWaveFile::waveFmt_t	format;

	idList<byte> amplitude;
	uint64			amplitudeReserved;

	// An Ogg decoding on a job worker into buffers[0] (LoadResourceAsync), and its file
	idSoundSampleDecode*	pendingDecode;
	idStr					pendingDecodeName;
};

/*
================================================
idSoundSample

This reverse-inheritance purportedly makes working on
multiple platforms easier.
================================================
*/
class idSoundSample : public idSoundSample_OpenAL
{
public:
};

#endif
