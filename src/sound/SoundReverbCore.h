// Copyright (C) 2026 DarkMatter Productions
#ifndef __SOUND_REVERB_CORE_H__
#define __SOUND_REVERB_CORE_H__

#include <cfloat>
#include <cmath>

/*
Retail Quake 4 environmental audio, reduced to dependency-free math so the
engine and the native tests share one implementation.

The behaviour follows the EAX 4/5 path of Quake4.exe 1.4.2:
- efxs/<map>.efx (falling back to efxs/default.efx) holds EAX reverb property
  sets, and maps/<map>.reverb binds portal areas to them by name;
- four FX slots follow the listener's area plus the three areas nearest to the
  listener through its portals (idSoundSystemLocal::MixLoop, 0x101742d0). A
  slot stays pinned to its area while that area remains near, and the slot
  holding the listener's area is the primary slot;
- each slot's reverb is panned toward or away from the portal that leads to
  its area;
- every mono source feeds its own area's slot and the primary slot. The radio
  channel is kept out of the room (-10000 mB), voice-over is pulled back by
  500 mB, and each blocking portal on the source's path occludes by 1500 mB.

Creative's hardware that implemented EAX is gone. OpenAL Soft's EAX emulation
is what the retail game sounds like on current systems, so the EAX to EFX
mapping below is OpenAL Soft's: the reverb conversion of EaxReverbCommitter,
which reproduces the efx-presets.h tables, and the occlusion arithmetic of
al::Source::eax_create_direct_filter_param and eax_create_room_filter_param.
*/

static const int	SOUND_REVERB_SLOTS = 4;

// EAXSOURCE_ROOM values the retail mixer assigns
static const int	SOUND_REVERB_ROOM_RADIO_MB = -10000;	// SND_CHANNEL_RADIO (10)
static const int	SOUND_REVERB_ROOM_VOICEOVER_MB = -500;	// SSF_IS_VO
// EAXSOURCE_OCCLUSION per blocking (PS_BLOCK_VIEW | PS_BLOCK_AIR) portal
static const int	SOUND_REVERB_OCCLUSION_PER_PORTAL_MB = -1500;
static const float	SOUND_REVERB_OCCLUSION_LF_RATIO = 0.0f;
static const float	SOUND_REVERB_OCCLUSION_ROOM_RATIO = 1.5f;
static const float	SOUND_REVERB_OCCLUSION_DIRECT_RATIO = 1.0f;
// s_muteEAXReverb forces the listener slot's EAXREVERB_ROOM to this
static const int	SOUND_REVERB_MUTED_ROOM_MB = -10000;
// EAXREVERBFLAGS_DECAYHFLIMIT
static const unsigned int SOUND_REVERB_FLAG_DECAY_HF_LIMIT = 0x20;

// AL_EAXREVERB_* parameter ranges (AL/efx.h)
static const float	SOUND_REVERB_EFX_MAX_DENSITY = 1.0f;
static const float	SOUND_REVERB_EFX_MAX_DIFFUSION = 1.0f;
static const float	SOUND_REVERB_EFX_MAX_GAIN = 1.0f;
static const float	SOUND_REVERB_EFX_MIN_DECAY_TIME = 0.1f;
static const float	SOUND_REVERB_EFX_MAX_DECAY_TIME = 20.0f;
static const float	SOUND_REVERB_EFX_MIN_DECAY_RATIO = 0.1f;
static const float	SOUND_REVERB_EFX_MAX_DECAY_RATIO = 2.0f;
static const float	SOUND_REVERB_EFX_MAX_REFLECTIONS_GAIN = 3.16f;
static const float	SOUND_REVERB_EFX_MAX_REFLECTIONS_DELAY = 0.3f;
static const float	SOUND_REVERB_EFX_MAX_LATE_REVERB_GAIN = 10.0f;
static const float	SOUND_REVERB_EFX_MAX_LATE_REVERB_DELAY = 0.1f;
static const float	SOUND_REVERB_EFX_MIN_ECHO_TIME = 0.075f;
static const float	SOUND_REVERB_EFX_MAX_ECHO_TIME = 0.25f;
static const float	SOUND_REVERB_EFX_MIN_MODULATION_TIME = 0.04f;
static const float	SOUND_REVERB_EFX_MAX_MODULATION_TIME = 4.0f;
static const float	SOUND_REVERB_EFX_MIN_AIR_ABSORPTION_GAINHF = 0.892f;
static const float	SOUND_REVERB_EFX_MIN_HFREFERENCE = 1000.0f;
static const float	SOUND_REVERB_EFX_MAX_HFREFERENCE = 20000.0f;
static const float	SOUND_REVERB_EFX_MIN_LFREFERENCE = 20.0f;
static const float	SOUND_REVERB_EFX_MAX_LFREFERENCE = 1000.0f;
static const float	SOUND_REVERB_EFX_MAX_ROOM_ROLLOFF_FACTOR = 10.0f;

// EAXREVERBPROPERTIES, the property set each efxs/*.efx "reverb" block authors.
struct soundReverbEAX_t {
	unsigned int	environment;
	float			environmentSize;
	float			environmentDiffusion;
	int				room;
	int				roomHF;
	int				roomLF;
	float			decayTime;
	float			decayHFRatio;
	float			decayLFRatio;
	int				reflections;
	float			reflectionsDelay;
	float			reflectionsPan[3];
	int				reverb;
	float			reverbDelay;
	float			reverbPan[3];
	float			echoTime;
	float			echoDepth;
	float			modulationTime;
	float			modulationDepth;
	float			airAbsorptionHF;
	float			hfReference;
	float			lfReference;
	float			roomRolloffFactor;
	unsigned int	flags;
};

// AL_EFFECT_EAXREVERB parameters.
struct soundReverbEFX_t {
	float	density;
	float	diffusion;
	float	gain;
	float	gainHF;
	float	gainLF;
	float	decayTime;
	float	decayHFRatio;
	float	decayLFRatio;
	float	reflectionsGain;
	float	reflectionsDelay;
	float	reflectionsPan[3];
	float	lateReverbGain;
	float	lateReverbDelay;
	float	lateReverbPan[3];
	float	echoTime;
	float	echoDepth;
	float	modulationTime;
	float	modulationDepth;
	float	airAbsorptionGainHF;
	float	hfReference;
	float	lfReference;
	float	roomRolloffFactor;
	int		decayHFLimit;
};

// EAX's Generic environment, the state a fresh FX slot starts in. A property an
// .efx block leaves out keeps this value.
inline void SoundReverb_DefaultEAX( soundReverbEAX_t &p ) {
	p.environment = 0;
	p.environmentSize = 7.5f;
	p.environmentDiffusion = 1.0f;
	p.room = -1000;
	p.roomHF = -100;
	p.roomLF = 0;
	p.decayTime = 1.49f;
	p.decayHFRatio = 0.83f;
	p.decayLFRatio = 1.0f;
	p.reflections = -2602;
	p.reflectionsDelay = 0.007f;
	p.reflectionsPan[0] = p.reflectionsPan[1] = p.reflectionsPan[2] = 0.0f;
	p.reverb = 200;
	p.reverbDelay = 0.011f;
	p.reverbPan[0] = p.reverbPan[1] = p.reverbPan[2] = 0.0f;
	p.echoTime = 0.25f;
	p.echoDepth = 0.0f;
	p.modulationTime = 0.25f;
	p.modulationDepth = 0.0f;
	p.airAbsorptionHF = -5.0f;
	p.hfReference = 5000.0f;
	p.lfReference = 250.0f;
	p.roomRolloffFactor = 0.0f;
	p.flags = 0x3f;
}

// Millibels to linear gain. EAX's -10000 mB floor is silence.
inline float SoundReverb_MillibelsToGain( float mB ) {
	if ( !( mB > -10000.0f ) ) {
		return 0.0f;
	}
	return std::pow( 10.0f, mB / 2000.0f );
}

inline float SoundReverb_Clamp( float value, float low, float high ) {
	if ( !std::isfinite( value ) ) {
		return low;
	}
	return value < low ? low : value > high ? high : value;
}

inline float SoundReverb_FinitePan( float value ) {
	return std::isfinite( value ) ? value : 0.0f;
}

// EAX reverb properties to EFX EAX reverb parameters. Valid EAX values map into
// the EFX ranges; the clamps only keep malformed data from being rejected.
inline void SoundReverb_ConvertEAXToEFX( const soundReverbEAX_t &eax, soundReverbEFX_t &efx ) {
	const float size = eax.environmentSize;
	efx.density = SoundReverb_Clamp( ( size * size * size ) / 16.0f, 0.0f, SOUND_REVERB_EFX_MAX_DENSITY );
	efx.diffusion = SoundReverb_Clamp( eax.environmentDiffusion, 0.0f, SOUND_REVERB_EFX_MAX_DIFFUSION );
	efx.gain = SoundReverb_Clamp( SoundReverb_MillibelsToGain( (float)eax.room ), 0.0f, SOUND_REVERB_EFX_MAX_GAIN );
	efx.gainHF = SoundReverb_Clamp( SoundReverb_MillibelsToGain( (float)eax.roomHF ), 0.0f, SOUND_REVERB_EFX_MAX_GAIN );
	efx.gainLF = SoundReverb_Clamp( SoundReverb_MillibelsToGain( (float)eax.roomLF ), 0.0f, SOUND_REVERB_EFX_MAX_GAIN );
	efx.decayTime = SoundReverb_Clamp( eax.decayTime, SOUND_REVERB_EFX_MIN_DECAY_TIME, SOUND_REVERB_EFX_MAX_DECAY_TIME );
	efx.decayHFRatio = SoundReverb_Clamp( eax.decayHFRatio, SOUND_REVERB_EFX_MIN_DECAY_RATIO, SOUND_REVERB_EFX_MAX_DECAY_RATIO );
	efx.decayLFRatio = SoundReverb_Clamp( eax.decayLFRatio, SOUND_REVERB_EFX_MIN_DECAY_RATIO, SOUND_REVERB_EFX_MAX_DECAY_RATIO );
	efx.reflectionsGain = SoundReverb_Clamp( SoundReverb_MillibelsToGain( (float)eax.reflections ), 0.0f, SOUND_REVERB_EFX_MAX_REFLECTIONS_GAIN );
	efx.reflectionsDelay = SoundReverb_Clamp( eax.reflectionsDelay, 0.0f, SOUND_REVERB_EFX_MAX_REFLECTIONS_DELAY );
	efx.lateReverbGain = SoundReverb_Clamp( SoundReverb_MillibelsToGain( (float)eax.reverb ), 0.0f, SOUND_REVERB_EFX_MAX_LATE_REVERB_GAIN );
	efx.lateReverbDelay = SoundReverb_Clamp( eax.reverbDelay, 0.0f, SOUND_REVERB_EFX_MAX_LATE_REVERB_DELAY );
	// EAX and EFX reverb pans are both left-handed (x right, y up, z forward)
	for ( int i = 0; i < 3; i++ ) {
		efx.reflectionsPan[i] = SoundReverb_FinitePan( eax.reflectionsPan[i] );
		efx.lateReverbPan[i] = SoundReverb_FinitePan( eax.reverbPan[i] );
	}
	efx.echoTime = SoundReverb_Clamp( eax.echoTime, SOUND_REVERB_EFX_MIN_ECHO_TIME, SOUND_REVERB_EFX_MAX_ECHO_TIME );
	efx.echoDepth = SoundReverb_Clamp( eax.echoDepth, 0.0f, 1.0f );
	efx.modulationTime = SoundReverb_Clamp( eax.modulationTime, SOUND_REVERB_EFX_MIN_MODULATION_TIME, SOUND_REVERB_EFX_MAX_MODULATION_TIME );
	efx.modulationDepth = SoundReverb_Clamp( eax.modulationDepth, 0.0f, 1.0f );
	efx.airAbsorptionGainHF = SoundReverb_Clamp( SoundReverb_MillibelsToGain( eax.airAbsorptionHF ), SOUND_REVERB_EFX_MIN_AIR_ABSORPTION_GAINHF, 1.0f );
	efx.hfReference = SoundReverb_Clamp( eax.hfReference, SOUND_REVERB_EFX_MIN_HFREFERENCE, SOUND_REVERB_EFX_MAX_HFREFERENCE );
	efx.lfReference = SoundReverb_Clamp( eax.lfReference, SOUND_REVERB_EFX_MIN_LFREFERENCE, SOUND_REVERB_EFX_MAX_LFREFERENCE );
	efx.roomRolloffFactor = SoundReverb_Clamp( eax.roomRolloffFactor, 0.0f, SOUND_REVERB_EFX_MAX_ROOM_ROLLOFF_FACTOR );
	efx.decayHFLimit = ( eax.flags & SOUND_REVERB_FLAG_DECAY_HF_LIMIT ) != 0 ? 1 : 0;
}

inline bool SoundReverb_EFXEqual( const soundReverbEFX_t &a, const soundReverbEFX_t &b ) {
	for ( int i = 0; i < 3; i++ ) {
		if ( a.reflectionsPan[i] != b.reflectionsPan[i] || a.lateReverbPan[i] != b.lateReverbPan[i] ) {
			return false;
		}
	}
	return a.density == b.density && a.diffusion == b.diffusion && a.gain == b.gain && a.gainHF == b.gainHF &&
		a.gainLF == b.gainLF && a.decayTime == b.decayTime && a.decayHFRatio == b.decayHFRatio &&
		a.decayLFRatio == b.decayLFRatio && a.reflectionsGain == b.reflectionsGain &&
		a.reflectionsDelay == b.reflectionsDelay && a.lateReverbGain == b.lateReverbGain &&
		a.lateReverbDelay == b.lateReverbDelay && a.echoTime == b.echoTime && a.echoDepth == b.echoDepth &&
		a.modulationTime == b.modulationTime && a.modulationDepth == b.modulationDepth &&
		a.airAbsorptionGainHF == b.airAbsorptionGainHF && a.hfReference == b.hfReference &&
		a.lfReference == b.lfReference && a.roomRolloffFactor == b.roomRolloffFactor &&
		a.decayHFLimit == b.decayHFLimit;
}

/*
===============================================================================

	Reverb slot update economy (openQ4)

	OpenAL Soft runs every slot that holds a reverb on every mixer update, fed or
	not, and each new set of slot parameters makes it recompute every playing
	source as well. A slot no voice has fed for longer than its tail can hold the
	null effect until a voice needs it again, and a turning view need not resend a
	slot's panning for every fraction of a degree.

===============================================================================
*/

// The milliseconds a reverb still rings after its input stops: the reflection and
// late reverb delays, then the slowest of the low, mid and high bands decaying by
// 90 dB (1.5 x the 60 dB decay time), plus a quarter second of echo and margin.
inline int SoundReverb_TailHoldMs( const soundReverbEFX_t &efx ) {
	const float decay = SoundReverb_Clamp( efx.decayTime, SOUND_REVERB_EFX_MIN_DECAY_TIME, SOUND_REVERB_EFX_MAX_DECAY_TIME );
	float slowest = decay;
	const float ratios[2] = { efx.decayHFRatio, efx.decayLFRatio };
	for ( int i = 0; i < 2; i++ ) {
		const float band = SoundReverb_Clamp( decay * ratios[i], SOUND_REVERB_EFX_MIN_DECAY_TIME, SOUND_REVERB_EFX_MAX_DECAY_TIME );
		slowest = band > slowest ? band : slowest;
	}
	const float delays = SoundReverb_Clamp( efx.reflectionsDelay, 0.0f, SOUND_REVERB_EFX_MAX_REFLECTIONS_DELAY ) +
		SoundReverb_Clamp( efx.lateReverbDelay, 0.0f, SOUND_REVERB_EFX_MAX_LATE_REVERB_DELAY );
	return (int)std::ceil( ( delays + slowest * 1.5f + 0.25f ) * 1000.0f );
}

// The hold for a slot whose parameters are unknown: the longest a reverb can ring.
inline int SoundReverb_MaxTailHoldMs() {
	soundReverbEFX_t longest = {};
	longest.decayTime = SOUND_REVERB_EFX_MAX_DECAY_TIME;
	longest.decayHFRatio = longest.decayLFRatio = 1.0f;
	longest.reflectionsDelay = SOUND_REVERB_EFX_MAX_REFLECTIONS_DELAY;
	longest.lateReverbDelay = SOUND_REVERB_EFX_MAX_LATE_REVERB_DELAY;
	return SoundReverb_TailHoldMs( longest );
}

inline float SoundReverb_PanDistance( const float a[3], const float b[3] ) {
	const float d0 = a[0] - b[0];
	const float d1 = a[1] - b[1];
	const float d2 = a[2] - b[2];
	return std::sqrt( d0 * d0 + d1 * d1 + d2 * d2 );
}

// True when next differs from applied only in its panning, and by less than step
// (a unit-length pan moves 0.05 per 2.9 degrees). Exact equality still counts as
// a match; a step of 0 or less accepts nothing else.
inline bool SoundReverb_PanWithinStep( const soundReverbEFX_t &applied, const soundReverbEFX_t &next, float step ) {
	soundReverbEFX_t samePan = next;
	for ( int i = 0; i < 3; i++ ) {
		samePan.reflectionsPan[i] = applied.reflectionsPan[i];
		samePan.lateReverbPan[i] = applied.lateReverbPan[i];
	}
	if ( !SoundReverb_EFXEqual( applied, samePan ) ) {
		return false;
	}
	if ( SoundReverb_EFXEqual( applied, next ) ) {
		return true;
	}
	if ( !( step > 0.0f ) ) {
		return false;
	}
	return SoundReverb_PanDistance( applied.reflectionsPan, next.reflectionsPan ) < step &&
		SoundReverb_PanDistance( applied.lateReverbPan, next.lateReverbPan ) < step;
}

/*
===============================================================================

	Area slot planning

	The retail UpdateNearbyAreaEfx block, inlined into the mixer of Quake4.exe
	1.4.2. Index 0 of the closest list is the listener's own area; indices 1-3
	are the three areas nearest the listener through its area's portals, by the
	squared distance to each portal winding's center.

===============================================================================
*/

// One portal leaving the listener's area.
struct soundReverbPortal_t {
	int		area;			// the area on the far side
	float	center[3];		// idWinding::GetCenter
	float	mins[3];		// idWinding::GetBounds
	float	maxs[3];
};

struct soundReverbListener_t {
	float	origin[3];
	float	axis[3][3];		// rows: forward, left, up
	int		area;
};

struct soundReverbSlotPlan_t {
	bool	configured[SOUND_REVERB_SLOTS];	// the slot takes new parameters this update
	int		effect[SOUND_REVERB_SLOTS];		// effect index of a configured slot
	float	pan[SOUND_REVERB_SLOTS][3];		// EAX-space environment direction
	bool	muteRoom[SOUND_REVERB_SLOTS];	// s_muteEAXReverb on the listener's slot
	int		primarySlot;					// slot holding the listener's area, -1 if none did
};

// Returns the effect a slot plays for an area (retail GetReverbName(GetReverb(area))
// looked up by name, then "default"), or -1 when there is none.
typedef int ( *soundReverbEffectForArea_t )( void *context, int area );

inline void SoundReverb_Normalize( float v[3] ) {
	const float lengthSquared = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
	if ( lengthSquared == 0.0f || !std::isfinite( lengthSquared ) ) {
		return;
	}
	const float inverseLength = 1.0f / std::sqrt( lengthSquared );
	v[0] *= inverseLength;
	v[1] *= inverseLength;
	v[2] *= inverseLength;
}

// The slot's environment direction: toward the portal into a nearby area, and
// for the listener's own area away from its nearest portal. A portal that
// subtends a wide angle pans less sharply than a distant, narrow one.
inline void SoundReverb_PortalPan( const soundReverbListener_t &listener, const soundReverbPortal_t &portal, bool listenerArea, float pan[3] ) {
	float minDirection[3];
	float maxDirection[3];
	float worldDirection[3];
	for ( int i = 0; i < 3; i++ ) {
		minDirection[i] = portal.mins[i] - listener.origin[i];
		maxDirection[i] = portal.maxs[i] - listener.origin[i];
		worldDirection[i] = portal.center[i] - listener.origin[i];
	}
	SoundReverb_Normalize( minDirection );
	SoundReverb_Normalize( maxDirection );
	SoundReverb_Normalize( worldDirection );

	float listenerDirection[3];
	for ( int i = 0; i < 3; i++ ) {
		listenerDirection[i] = worldDirection[0] * listener.axis[i][0] + worldDirection[1] * listener.axis[i][1] + worldDirection[2] * listener.axis[i][2];
	}

	const float spread = maxDirection[0] * minDirection[0] + maxDirection[1] * minDirection[1] + maxDirection[2] * minDirection[2] + 1.0f;
	const float scale = listenerArea ? spread * 0.3185f - 0.637f : spread * 0.181f + 0.637f;

	// listener space is x forward, y left, z up; EAX space is x right, y up, z forward
	pan[0] = -scale * listenerDirection[1];
	pan[1] = scale * listenerDirection[2];
	pan[2] = scale * listenerDirection[0];
}

inline int SoundReverb_SlotForArea( const int slotToArea[SOUND_REVERB_SLOTS], int numSlots, int area ) {
	for ( int i = 0; i < numSlots && i < SOUND_REVERB_SLOTS; i++ ) {
		if ( slotToArea[i] == area ) {
			return i;
		}
	}
	return -1;
}

inline void SoundReverb_PlanSlots( const soundReverbListener_t &listener, const soundReverbPortal_t *portals, int numPortals,
		int numSlots, int slotToArea[SOUND_REVERB_SLOTS], soundReverbEffectForArea_t effectForArea, void *context,
		bool muteListenerRoom, soundReverbSlotPlan_t &plan ) {
	for ( int i = 0; i < SOUND_REVERB_SLOTS; i++ ) {
		plan.configured[i] = false;
		plan.effect[i] = -1;
		plan.pan[i][0] = plan.pan[i][1] = plan.pan[i][2] = 0.0f;
		plan.muteRoom[i] = false;
	}
	plan.primarySlot = -1;
	if ( numSlots > SOUND_REVERB_SLOTS ) {
		numSlots = SOUND_REVERB_SLOTS;
	}
	if ( numSlots <= 0 || effectForArea == nullptr ) {
		return;
	}

	// gather the listener's area and the three nearest areas through its portals
	int closestArea[SOUND_REVERB_SLOTS];
	float closestDistance[SOUND_REVERB_SLOTS];
	int closestPortal[SOUND_REVERB_SLOTS];
	closestArea[0] = listener.area;
	closestDistance[0] = 0.0f;
	closestPortal[0] = -1;
	for ( int i = 1; i < SOUND_REVERB_SLOTS; i++ ) {
		closestArea[i] = -1;
		closestDistance[i] = FLT_MAX;
		closestPortal[i] = -1;
	}
	for ( int p = 0; p < numPortals && portals != nullptr; p++ ) {
		int farthest = 0;
		float farthestDistance = closestDistance[0];
		for ( int i = 1; i < SOUND_REVERB_SLOTS; i++ ) {
			if ( farthestDistance < closestDistance[i] ) {
				farthest = i;
				farthestDistance = closestDistance[i];
			}
		}
		const float dx = portals[p].center[0] - listener.origin[0];
		const float dy = portals[p].center[1] - listener.origin[1];
		const float dz = portals[p].center[2] - listener.origin[2];
		const float distanceSquared = dx * dx + dy * dy + dz * dz;
		if ( distanceSquared > 0.0f && distanceSquared < farthestDistance ) {
			closestArea[farthest] = portals[p].area;
			closestDistance[farthest] = distanceSquared;
			closestPortal[farthest] = p;
		}
	}

	// the listener's own area pans away from its nearest portal
	int nearestPortalSlot = -1;
	float nearestDistance = FLT_MAX;
	for ( int i = 1; i < SOUND_REVERB_SLOTS; i++ ) {
		if ( closestDistance[i] < nearestDistance ) {
			nearestDistance = closestDistance[i];
			nearestPortalSlot = i;
		}
	}

	bool slotAvailable[SOUND_REVERB_SLOTS];
	for ( int i = 0; i < SOUND_REVERB_SLOTS; i++ ) {
		slotAvailable[i] = i < numSlots;
	}

	const auto configure = [&]( int slot, int areaSlot ) -> bool {
		const int area = closestArea[areaSlot];
		const int effect = effectForArea( context, area );
		if ( effect < 0 ) {
			return false;
		}
		const bool isListenerArea = ( area == listener.area );
		const int portalSlot = isListenerArea ? nearestPortalSlot : areaSlot;
		float pan[3] = { 0.0f, 0.0f, 0.0f };
		if ( portalSlot >= 0 && portalSlot < SOUND_REVERB_SLOTS && closestPortal[portalSlot] >= 0 ) {
			SoundReverb_PortalPan( listener, portals[closestPortal[portalSlot]], isListenerArea, pan );
		}
		slotToArea[slot] = area;
		plan.configured[slot] = true;
		plan.effect[slot] = effect;
		plan.pan[slot][0] = pan[0];
		plan.pan[slot][1] = pan[1];
		plan.pan[slot][2] = pan[2];
		plan.muteRoom[slot] = muteListenerRoom && isListenerArea;
		return true;
	};

	// keep slots that already hold one of the areas; 1.4.2 does not stop at the
	// first match, so an area reached through two portals takes the later one
	for ( int slot = 0; slot < numSlots; slot++ ) {
		for ( int areaSlot = 0; areaSlot < SOUND_REVERB_SLOTS; areaSlot++ ) {
			if ( closestArea[areaSlot] == -1 || slotToArea[slot] != closestArea[areaSlot] ) {
				continue;
			}
			if ( configure( slot, areaSlot ) ) {
				slotAvailable[slot] = false;
				closestArea[areaSlot] = -1;
			}
		}
	}

	// give the remaining areas the free slots in order
	for ( int areaSlot = 0; areaSlot < SOUND_REVERB_SLOTS; areaSlot++ ) {
		if ( closestArea[areaSlot] == -1 ) {
			continue;
		}
		int slot = 0;
		while ( slot < SOUND_REVERB_SLOTS && !slotAvailable[slot] ) {
			slot++;
		}
		if ( slot >= SOUND_REVERB_SLOTS ) {
			continue;
		}
		if ( configure( slot, areaSlot ) ) {
			slotAvailable[slot] = false;
			closestArea[areaSlot] = -1;
		}
	}

	plan.primarySlot = SoundReverb_SlotForArea( slotToArea, numSlots, listener.area );
}

/*
===============================================================================

	Source sends

	EAXSOURCE_ROOM and EAXSOURCE_OCCLUSION as OpenAL Soft's EAX emulation turns
	them into low-pass filters. Source occlusion applies to the direct path and
	to the send into the primary (listener) slot; the send into the source's own
	area slot carries only the room level.

===============================================================================
*/

struct soundReverbSourceSends_t {
	float	directGain;
	float	directGainHF;	// relative to directGain
	float	primaryGain;
	float	primaryGainHF;	// relative to primaryGain
	float	areaGain;
	float	areaGainHF;		// relative to areaGain
};

inline float SoundReverb_OcclusionLevel( float occlusionMB, float pathRatio, float lfRatio ) {
	const float ratio1 = pathRatio + lfRatio - 1.0f;
	const float ratio2 = pathRatio * lfRatio;
	return occlusionMB * ( ratio2 > ratio1 ? ratio2 : ratio1 );
}

inline void SoundReverb_SourceSends( int roomMB, int occlusionMB, soundReverbSourceSends_t &out ) {
	const float occlusion = (float)occlusionMB;

	float directMB = 0.0f;
	float directHFMB = 0.0f;
	float primaryMB = 0.0f;
	float primaryHFMB = 0.0f;
	if ( occlusionMB != 0 ) {
		directMB = SoundReverb_OcclusionLevel( occlusion, SOUND_REVERB_OCCLUSION_DIRECT_RATIO, SOUND_REVERB_OCCLUSION_LF_RATIO );
		directHFMB = occlusion * SOUND_REVERB_OCCLUSION_DIRECT_RATIO;
		primaryMB = SoundReverb_OcclusionLevel( occlusion, SOUND_REVERB_OCCLUSION_ROOM_RATIO, SOUND_REVERB_OCCLUSION_LF_RATIO );
		primaryHFMB = occlusion * SOUND_REVERB_OCCLUSION_ROOM_RATIO;
	}
	directHFMB -= directMB;
	primaryHFMB -= primaryMB;
	primaryMB += (float)roomMB;

	out.directGain = SoundReverb_Clamp( SoundReverb_MillibelsToGain( directMB ), 0.0f, 1.0f );
	out.directGainHF = SoundReverb_Clamp( SoundReverb_MillibelsToGain( directHFMB ), 0.0f, 1.0f );
	out.primaryGain = SoundReverb_Clamp( SoundReverb_MillibelsToGain( primaryMB ), 0.0f, 1.0f );
	out.primaryGainHF = SoundReverb_Clamp( SoundReverb_MillibelsToGain( primaryHFMB ), 0.0f, 1.0f );
	out.areaGain = SoundReverb_Clamp( SoundReverb_MillibelsToGain( (float)roomMB ), 0.0f, 1.0f );
	out.areaGainHF = 1.0f;
}

// The send attenuation OpenAL gives a spatialized source for a reverb's room
// rolloff (AL_INVERSE_DISTANCE_CLAMPED, no maximum distance): the reference
// distance over the reference plus the rolloff times the distance beyond it.
// OpenAL spatializes no multichannel buffer, so a voice playing direct 5.1
// channels to the centre speaker gets none of it; the engine applies this to
// that voice's sends. Stock presets have no room rolloff and return 1.
inline float SoundReverb_RoomRolloffGain( float distance, float reference, float roomRolloff ) {
	if ( !( reference > 0.0f ) || !( roomRolloff > 0.0f ) || reference > FLT_MAX || roomRolloff > FLT_MAX || distance != distance ) {
		return 1.0f;
	}
	const float clamped = distance > FLT_MAX ? FLT_MAX : distance;
	const float beyond = clamped > reference ? clamped - reference : 0.0f;
	return SoundReverb_Clamp( reference / ( reference + roomRolloff * beyond ), 0.0f, 1.0f );
}

#endif /* !__SOUND_REVERB_CORE_H__ */
