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

	void					SetGain( float gain )
	{
		idSoundVoice_Base::SetGain( gain );
		ApplyWetDryRouting();
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
		ApplyWetDryRouting();
	}
	void		SetDryLevel( float dry ) override
	{
		idSoundVoice_Base::SetDryLevel( dry );
		ApplyWetDryRouting();
	}
	void		SetOcclusion( float f ) override
	{
		idSoundVoice_Base::SetOcclusion( f );
		ApplyWetDryRouting();
	}
	void		SetEnvironmentMuffle( float f ) override
	{
		idSoundVoice_Base::SetEnvironmentMuffle( f );
		ApplyWetDryRouting();
	}

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
	void					AdvanceQueuedBuffer();
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
	ALuint					openalAuxFilter;
	idSoundSample_OpenAL*	nextQueuedSample;
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
