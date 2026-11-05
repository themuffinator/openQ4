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
#ifndef __AL_SOUNDVOICE_H__
#define __AL_SOUNDVOICE_H__

#include "../SoundSettings.h"

static const int MAX_QUEUED_BUFFERS = 3;

/*
================================================
idSoundVoice_OpenAL
================================================
*/
class idSoundVoice_OpenAL : public idSoundVoice_Base
{
public:
	idSoundVoice_OpenAL();
	~idSoundVoice_OpenAL();

	void					SetPosition( const idVec3& p )
	{
		idSoundVoice_Base::SetPosition( p );

		if( alIsSource( openalSource ) )
		{
			alSource3f( openalSource, AL_POSITION, -position.y, position.z, -position.x );
		}
	}

	void					SetVelocity( const idVec3& v ) override
	{
		idSoundVoice_Base::SetVelocity( v );

		if( alIsSource( openalSource ) )
		{
			alSource3f( openalSource, AL_VELOCITY, -velocity.y, velocity.z, -velocity.x );
		}
	}

	// The gain, wet/dry, occlusion and reverb setters only record their value; the
	// emitter calls CommitMix once all of them have run, so the filters and sends go
	// out once a frame instead of once per setter.
	void					SetGain( float gain )
	{
		idSoundVoice_Base::SetGain( gain );
	}

	// The sound's minDistance. BFG's XAudio2 mixer blended a voice toward every
	// speaker inside it; here it becomes the source's size (AL_EXT_SOURCE_RADIUS).
	void		SetInnerRadius( float r ) override
	{
		idSoundVoice_Base::SetInnerRadius( r );
		if( alIsSource( openalSource ) )
		{
			// Sources and authored minDistance share map units. EFX room rolloff
			// uses this even though the engine applies dry attenuation itself.
			alSourcef( openalSource, AL_REFERENCE_DISTANCE, Max( innerRadius, 0.001f ) );
		}
		ApplySourceRadius();
	}

	void		SetPitch( float p )
	{
		idSoundVoice_Base::SetPitch( p );

		if( alIsSource( openalSource ) )
		{
			alSourcef( openalSource, AL_PITCH, pitch );
		}
	}
	void		SetWetLevel( float wet ) override
	{
		idSoundVoice_Base::SetWetLevel( wet );
	}
	void		SetDryLevel( float dry ) override
	{
		idSoundVoice_Base::SetDryLevel( dry );
	}
	void		SetOcclusion( float f ) override
	{
		idSoundVoice_Base::SetOcclusion( f );
	}
	void		SetEnvironmentMuffle( float f ) override
	{
		idSoundVoice_Base::SetEnvironmentMuffle( f );
	}
	bool		SetReverbSource( int areaSlot, int roomMB, int occlusionMB, bool mono ) override
	{
		return idSoundVoice_Base::SetReverbSource( areaSlot, roomMB, occlusionMB, mono );
	}
	// Every frame, not only on change: the listener's reverb slot moves between
	// areas and the sends have to follow it.
	void		CommitMix() override
	{
		ApplyWetDryRouting();
	}

	// Drops both reverb sends so the hardware can release its effect slots.
	void					DetachReverbSends();

	void					Create( const idSoundSample* leadinSample, const idSoundSample* loopingSample );

	// Start playing at a particular point in the buffer.  Does an Update() too
	bool					Start( int offsetMS, int ssFlags );

	// Stop playing.
	void					Stop();

	// Stop consuming buffers
	void					Pause();

	// Start consuming buffers again
	bool					UnPause();

	// Sends new position/volume/pitch information to the hardware
	bool					Update();
	bool					UsesBoundedQueue() const;
	// A voice that hands itself back is restarted at the scene clock when a bounded
	// queue ran dry or its output, not the sound, stopped it (SoundOutputRecovery.h).
	bool					RecoversAtClock() const;

	// returns the RMS levels of the most recently processed block of audio, SSF_FLICKER must have been passed to Start
	float					GetAmplitude();

	// returns true if we can re-use this voice
	bool					CompatibleFormat( idSoundSample_OpenAL* s );

	uint32					GetSampleRate() const
	{
		return sampleRate;
	}
	bool					GetPlaybackLatencyMS( float& offsetMS, float& latencyMS ) const override;
	static bool				SourceLatencyQueriesAvailable();

	// callback function
	void					OnBufferStart( idSoundSample_OpenAL* sample, int bufferNumber );

private:
	friend struct SoundSettingsAccess;
	friend class idSoundHardware_OpenAL;

	// Returns true when all the buffers are finished processing
	bool					IsPlaying();

	// The routing Start would choose on the current output.
	bool					WantsCenterRouting() const;
	// False once the output this voice started on was lost, replaced or re-laid out
	// in a way it cannot keep playing through.
	bool					CheckOutput();

	// Called after the voice has been stopped
	void					FlushSourceBuffers();

	// Destroy the internal hardware resource
	void					DestroyInternal();

	// Helper function used by the initial start as well as for looping a streamed buffer
	int						RestartAt( int offsetSamples );
	void					FailCreate();

	// Helper function to submit a buffer
	int						SubmitBuffer( idSoundSample_OpenAL* sample, int bufferNumber, int offset, ALuint streamBuffer );
	bool					QueueNextBuffer( ALuint streamBuffer );
	// Frames per buffer for a centre-routed queue; claims a deep-queue slot while one is free.
	int						RoutedQueueFrames( int sampleRate );
	void					AdvanceQueuedBuffer();
	// A streamed sample's blocks decode on job workers (idSoundOggStream). The frames
	// one queue buffer takes at offset; opening the stream and requesting that block.
	int						StreamBlockFrames( idSoundSample_OpenAL* sample, int offset );
	bool					PrepareStreamBlock( idSoundSample_OpenAL* sample, int offset );
	// The next queue buffer is still decoding: the voice is starting or refilling, not done.
	bool					StreamBlockPending() const;
	bool					SetNextQueuedSampleStart( idSoundSample_OpenAL* sample );
	static bool				GetPlayableBufferRange( const idSoundSample_OpenAL* sample, int bufferNumber, int& bufferStart, int& bufferSamples, int& playableOffset, int& playableSamples );
	void					ResetQueuedBufferState();
	void					DeleteStreamingBuffers();
	bool					EnsureStreamingBuffers();
	bool					UsesStreamingBuffers() const;
	int						StreamingBufferIndex( ALuint buffer ) const;
	bool					IsStreamingBuffer( ALuint buffer ) const;
	void					ResetStreamingBufferQueueState();
	bool					HasQueuedBufferState() const;
	static bool				SampleHasPlayableOpenALPayload( const idSoundSample_OpenAL* sample );
	bool					SourceHasPlayableBuffer() const;

	// Adjust the voice frequency based on the new sample rate for the buffer
	void					SetSampleRate( uint32 newSampleRate, uint32 operationSet );
	void					ResetSourceMixState();
	void					ApplySourceRadius();
	void					ApplyWetDryRouting();
	bool ApplyWetDryRoutingChecked(bool filters, bool wet, ALuint slot, SoundSettingsSourceReceipt& out);
	void					CreateWetDryFilters();
	void					DestroyWetDryFilters();

	//IXAudio2SourceVoice* 	pSourceVoice;
	ALuint					openalSource;
	std::uint64_t soundSettingsSourceGeneration;
	void (AL_APIENTRY *soundSettingsDeleteFilters)(ALsizei, const ALuint*);
	ALuint					openalStreamingBuffer[3];
	bool					openalStreamingBufferQueued[3];
	ALuint					openalDirectFilter;
	ALuint					openalAuxFilter;		// send 0: the listener's (primary) reverb slot
	ALuint					openalAreaAuxFilter;	// send 1: the sound's own area slot
	idSoundSample_OpenAL*	nextQueuedSample;
	idSoundOggStream*		oggStream;
	idSoundSample_OpenAL*	oggStreamSample;
	int						nextQueuedBuffer;
	int						nextQueuedOffset;
	bool					queuedBufferPlaybackActive;

	idSoundSample_OpenAL*	leadinSample;
	idSoundSample_OpenAL*	loopingSample;

	// These are the fields from the sample format that matter to us for voice reuse
	uint16					formatTag;
	uint16					numChannels;

	uint32					sourceVoiceRate;
	uint32					sampleRate;

	bool					hasVUMeter;
	bool					paused;
	bool					centerRouting;
	bool					outputRecovery;			// the output, not the sound, ended playback
	unsigned int			outputSerial;			// hardware output serial this voice plays on
	int						startFlags;				// the shader flags Start was given
	bool					deepRoutedQueue;		// holds one of SOUND_QUEUE_DEEP_ROUTED_VOICES until its buffers are deleted
	float					appliedSourceRadius;	// last AL_SOURCE_RADIUS sent, -1 = none yet
};

/*
================================================
idSoundVoice
================================================
*/
class idSoundVoice : public idSoundVoice_OpenAL
{
};

#endif
