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
#ifndef __AL_SOUNDHARDWARE_H__
#define __AL_SOUNDHARDWARE_H__

class idSoundSample_OpenAL;
class idSoundVoice_OpenAL;
class idSoundHardware_OpenAL;



/*
================================================
idSoundHardware_OpenAL
================================================
*/

class idSoundHardware_OpenAL
{
public:
	idSoundHardware_OpenAL();

	void			Init();
	void			Shutdown();

	void			BeginDeferredUpdates();
	void			EndDeferredUpdates();
	void 			Update();
	// Main-thread application suspension, independent of the player's mute.
	void			SetDevicePaused( bool paused );

	idSoundVoice* 	AllocateVoice( const idSoundSample* leadinSample, const idSoundSample* loopingSample );
	void			FreeVoice( idSoundVoice* voice );
	int				ListPlayingVoices() const;
	uint64			QueuedBufferBytes() const;
	void			PrintDistanceInfo( const idSoundVoice* voice ) const;

	static bool		IsDefaultDeviceChoiceValue( const char* deviceName );
	static void		BuildDeviceChoiceStrings( const char* requestedDeviceName, idStr& choiceNames, idStr& choiceValues );

	// listDevices needs this
	ALCdevice* 		GetOpenALDevice() const
	{
		return openalDevice;
	};
	ALCcontext*		GetOpenALContext() const
	{
		return openalContext;
	}

	int				GetNumZombieVoices() const
	{
		return zombieVoices.Num();
	}
	int				GetNumFreeVoices() const
	{
		return freeVoices.Num();
	}
	bool			HasEFX() const
	{
		return efxEnabled;
	}
	// true after Init() failed to open a device/context; the render loop uses
	// this to keep the periodic re-init retry alive while the engine-set
	// s_noSound silence is in effect
	bool			InitFailed() const
	{
		return initFailed;
	}
	// A deliberate release (a client that became a dedicated server) is not a
	// device failure: stop that retry, or it would reopen the device.
	void			ClearInitFailure()
	{
		initFailed = false;
	}
	bool			HasEFXFilters() const
	{
		return efxFiltersAvailable;
	}
	ALuint			GetAuxEffectSlot() const
	{
		return auxEffectSlot;
	}

	// Voice features found on the current context (DetectVoiceFeatures)
	bool			HasSourceRadius() const
	{
		return sourceRadiusAvailable;
	}
	bool			HasDirectChannels() const
	{
		return directChannelsAvailable;
	}
	ALint			GetDirectChannelsMode() const
	{
		return directChannelsMode;
	}
	bool			HasCenterChannelOutput() const;
	// Counts the output changes that can stop, move or re-lay-out every source: a
	// reopen, an in-place reset, a centre speaker appearing or going away, a lost
	// endpoint returning. Voices compare it with the count they started on
	// (SoundOutputRecovery.h).
	unsigned int	GetOutputSerial() const
	{
		return outputSerial;
	}
	// The endpoint is present (ALC_EXT_disconnect) and no simulated interruption runs.
	bool			IsOutputConnected() const;
	// s_testDeviceInterruption: stops every source as a disconnect does and refuses
	// new voices for msec, then reports the output returned.
	void			SimulateOutputInterruption( int msec );
	// listSoundVoices
	void			ListVoiceOutputState() const;
	// AL_SOURCE_RESAMPLER_SOFT index for every voice, -1 keeps the runtime default
	ALint			GetSourceResampler() const
	{
		return sourceResampler;
	}

	// Retail area reverb slots (idSoundReverb). Slot 0 is auxEffectSlot, whose
	// lifetime the checked settings path owns; slots 1-3 exist while it does.
	// Returns the number of usable slots, 0 when EFX reverb is off.
	int				SyncReverbSlots();
	void			SetReverbSlotProperties( int slot, const soundReverbEAX_t& properties );
	void			SetPrimaryReverbSlot( int slot );
	int				GetPrimaryReverbSlot() const
	{
		return primaryReverbSlot;
	}
	ALuint			GetReverbSlotHandle( int slot ) const;
	// the slot every mono voice's first send feeds: the one holding the listener's area
	ALuint			GetPrimaryAuxEffectSlot() const;
	// high-frequency gain per metre from the listener's reverb preset (0.994, -5 mB, in
	// every stock preset); 1 while no EFX reverb is playing
	float			GetAirAbsorptionGainHF() const;
	// the room rolloff factor of the reverb a slot holds (efxs "room rolloff factor"),
	// 0 while the slot holds none; every stock preset has 0
	float			GetReverbSlotRoomRolloff( int slot ) const;
	int				GetMaxAuxiliarySends() const
	{
		return maxAuxiliarySends;
	}
	// changes whenever slot handles are created, replaced or released
	unsigned int	GetReverbSlotGeneration() const
	{
		return reverbSlotGeneration;
	}
	bool			HasAreaReverbResources() const;
	// detaches every voice's reverb sends, then frees slots 1-3
	void			ReleaseAreaReverbSlots();
	// s_reverbIdleSlots: a slot no playing voice has fed for longer than its tail
	// holds the null effect, which OpenAL skips. A voice calls this before it
	// attaches a send to the slot or starts playing into it; the slot takes its
	// reverb back first and counts as fed this frame.
	void			NoteReverbSend( ALuint slotHandle );
	// the slot holds the null effect until a voice feeds it again
	bool			IsReverbSlotIdle( int slot ) const;

	// OpenAL info
	static void		PrintDeviceList( const char* list );
	static void		PrintALCInfo( ALCdevice* device );
	static void		PrintALInfo();

	static void		GetAvailablePlaybackDevices( idStrList& deviceNames, idStr& defaultDeviceName );
	static idStr		GetActivePlaybackDeviceName( ALCdevice* device );

protected:
	friend class idSoundSample_OpenAL;
	friend class idSoundVoice_OpenAL;

private:
	friend struct SoundSettingsAccess;
	/*
	IXAudio2* pXAudio2;
	IXAudio2MasteringVoice* pMasterVoice;
	IXAudio2SubmixVoice* pSubmixVoice;

	idSoundEngineCallback	soundEngineCallback;
	*/

	ALCdevice*			openalDevice;
	ALCcontext*			openalContext;
	typedef void ( ALC_APIENTRY *devicePauseFunction_t )( ALCdevice* device );
	bool				devicePauseRequested = false;
	devicePauseFunction_t deviceResume = NULL;

	int					lastResetTime;
	int					lastDeviceCheckTime;
	int					lastPerfPrintTime;
	bool				efxFiltersAvailable;
	bool				efxEnabled;
	ALuint				auxEffectSlot;
	ALuint				auxReverbEffect;
	ALuint				areaReverbSlots[SOUND_REVERB_SLOTS - 1];
	ALuint				areaReverbEffects[SOUND_REVERB_SLOTS - 1];
	bool				areaReverbUnavailable;
	int					primaryReverbSlot;
	int					maxAuxiliarySends;
	unsigned int		reverbSlotGeneration;
	ALuint				trackedReverbSlot;		// auxEffectSlot as the reverb slots last saw it
	soundReverbEFX_t	appliedReverb[SOUND_REVERB_SLOTS];
	ALuint				appliedReverbSlot[SOUND_REVERB_SLOTS];	// slot handle appliedReverb went to
	ALint				reverbEffectType[SOUND_REVERB_SLOTS];	// AL_EFFECT_EAXREVERB/REVERB, 0 = unqueried
	soundReverbEFX_t	requestedReverb[SOUND_REVERB_SLOTS];	// the last parameters asked for, applied or held back
	ALuint				requestedReverbSlot[SOUND_REVERB_SLOTS];
	bool				reverbSlotIdle[SOUND_REVERB_SLOTS];		// bound to the null effect by the idle sweep
	int					reverbSlotRingsUntil[SOUND_REVERB_SLOTS];	// reverbClock when the last input's tail ends
	int					reverbSlotLastFed[SOUND_REVERB_SLOTS];	// reverbClock of the last voice that fed the slot
	int					reverbClock;							// Sys_Milliseconds at this frame's slot sync
	int					ReverbSlotIndex( ALuint slotHandle ) const;
	int					ReverbSlotTailMs( int slot ) const;
	void				WakeReverbSlot( int slot );
	void				UpdateIdleReverbSlots( int numSlots );
	bool				deviceEventsEnabled;
	int					enabledDeviceEventFlags;
	bool				reopenDeviceAvailable;
	bool				deferredUpdatesAvailable;
	bool				deferredUpdatesActive;
	bool				initFailed;
	bool				openedWithDefaultFallback;
	int					openedHrtfMode;
	int					openedSpeakerCount;
	bool				openedOutputLimiter;
	bool				sourceRadiusAvailable;
	bool				directChannelsAvailable;
	ALint				directChannelsMode;
	ALint				sourceResampler;
	idStr				openedRequestedDeviceName;
	idStr				openedActiveDeviceName;
	idStr				openedDefaultDeviceName;
	unsigned int		outputSerial;
	bool				outputCenterObserved;		// HasCenterChannelOutput() at the last output check
	bool				centerRoutingAvailable;		// 5.1 buffers, direct channels and output mode queries
	bool				disconnectQueryAvailable;	// ALC_EXT_disconnect on the open device
	int					simulatedInterruptionEnd;	// Sys_Milliseconds() a simulated outage ends, 0 for none

	//int				outputChannels;
	//int				channelMask;

	//idDebugGraph* 	vuMeterRMS;
	//idDebugGraph* 	vuMeterPeak;
	//int				vuMeterPeakTimes[ 8 ];

	// Can't stop and start a voice on the same frame, so we have to double this to handle the worst case scenario of stopping all voices and starting a full new set
	idStaticList<idSoundVoice_OpenAL, MAX_HARDWARE_VOICES * 2 > voices;
	idStaticList<idSoundVoice_OpenAL*, MAX_HARDWARE_VOICES * 2 > zombieVoices;
	idStaticList<idSoundVoice_OpenAL*, MAX_HARDWARE_VOICES * 2 > freeVoices;

	static void		AppendChoiceItem( idStr& list, const char* item );
	static bool		DeviceListContains( const idStrList& deviceNames, const char* deviceName );
	static idStr		NormalizeRequestedDeviceName( const char* deviceName );
	static idStr		SanitizeDeviceLabel( const char* deviceName );

	void			CaptureOpenedDeviceState( const char* requestedDeviceName );
	void			EnableDeviceEventMonitoring();
	void			DisableDeviceEventMonitoring();
	bool			TryReopenDevice( const char* requestedDeviceName, const char* reason );
	bool			TryResetDevice( const char* reason );
	void			NoteOutputChanged( const char* reason );
	void			UpdateOutputState();
	bool			UpdateDeviceMonitoring();
	void			PrintPerformanceData();
	void			DetectVoiceFeatures();
	bool			CreateAreaReverbSlots();
	void			ResetReverbSlotState();
	ALuint			ReverbEffectHandle( int slot ) const;
};

/*
================================================
idSoundHardware
================================================
*/
class idSoundHardware : public idSoundHardware_OpenAL
{
};

#endif
