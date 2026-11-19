// Copyright (C) 2026 DarkMatter Productions
// Retail Quake 4 area reverb math (SoundReverbCore.h): the EAX to EFX reverb
// conversion against OpenAL Soft's preset table, the four-slot area planner
// of Quake4.exe 1.4.2, its portal panning and the EAX source send levels.
#include "../../../src/sound/SoundReverbCore.h"
#include "../../../subprojects/openal-soft-prebuilt/include/AL/efx.h"
#include "../../../subprojects/openal-soft-prebuilt/include/AL/efx-presets.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int checks = 0;

static void Check( bool condition, const char *message, int line ) {
	++checks;
	if ( !condition ) {
		std::fprintf( stderr, "SoundReverbCore line %d: %s\n", line, message );
		std::exit( 1 );
	}
}
#define CHECK( x ) Check( ( x ), #x, __LINE__ )

static bool Near( float a, float b, float tolerance ) {
	return std::fabs( a - b ) <= tolerance;
}

// efxs/default.efx as Quake 4 ships it: Creative's EAX environment presets.
struct retailPreset_t {
	const char *name;
	soundReverbEAX_t eax;
};

static const retailPreset_t retailPresets[] = {
	{ "Generic", { 0, 7.5000f, 1.0000f, -1000, -100, 0, 1.4900f, 0.8300f, 1.0000f, -2602, 0.0070f, { 0.0000f, 0.0000f, 0.0000f }, 200, 0.0110f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 0.2500f, 0.0000f, -5.0000f, 5000.0000f, 250.0000f, 0.0000f, 63 } },
	{ "Paddedcell", { 1, 1.4000f, 1.0000f, -1000, -6000, 0, 0.1700f, 0.1000f, 1.0000f, -1204, 0.0010f, { 0.0000f, 0.0000f, 0.0000f }, 207, 0.0020f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 0.2500f, 0.0000f, -5.0000f, 5000.0000f, 250.0000f, 0.0000f, 63 } },
	{ "Room", { 2, 1.9000f, 1.0000f, -1000, -454, 0, 0.4000f, 0.8300f, 1.0000f, -1646, 0.0020f, { 0.0000f, 0.0000f, 0.0000f }, 53, 0.0030f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 0.2500f, 0.0000f, -5.0000f, 5000.0000f, 250.0000f, 0.0000f, 63 } },
	{ "Bathroom", { 3, 1.4000f, 1.0000f, -1000, -1200, 0, 1.4900f, 0.5400f, 1.0000f, -370, 0.0070f, { 0.0000f, 0.0000f, 0.0000f }, 1030, 0.0110f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 0.2500f, 0.0000f, -5.0000f, 5000.0000f, 250.0000f, 0.0000f, 63 } },
	{ "Livingroom", { 4, 2.5000f, 1.0000f, -1000, -6000, 0, 0.5000f, 0.1000f, 1.0000f, -1376, 0.0030f, { 0.0000f, 0.0000f, 0.0000f }, -1104, 0.0040f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 0.2500f, 0.0000f, -5.0000f, 5000.0000f, 250.0000f, 0.0000f, 63 } },
	{ "Stoneroom", { 5, 11.6000f, 1.0000f, -1000, -300, 0, 2.3100f, 0.6400f, 1.0000f, -711, 0.0120f, { 0.0000f, 0.0000f, 0.0000f }, 83, 0.0170f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 0.2500f, 0.0000f, -5.0000f, 5000.0000f, 250.0000f, 0.0000f, 63 } },
	{ "Auditorium", { 6, 21.6000f, 1.0000f, -1000, -476, 0, 4.3200f, 0.5900f, 1.0000f, -789, 0.0200f, { 0.0000f, 0.0000f, 0.0000f }, -289, 0.0300f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 0.2500f, 0.0000f, -5.0000f, 5000.0000f, 250.0000f, 0.0000f, 63 } },
	{ "Concerthall", { 7, 19.6000f, 1.0000f, -1000, -500, 0, 3.9200f, 0.7000f, 1.0000f, -1230, 0.0200f, { 0.0000f, 0.0000f, 0.0000f }, -2, 0.0290f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 0.2500f, 0.0000f, -5.0000f, 5000.0000f, 250.0000f, 0.0000f, 63 } },
	{ "Cave", { 8, 14.6000f, 1.0000f, -1000, 0, 0, 2.9100f, 1.3000f, 1.0000f, -602, 0.0150f, { 0.0000f, 0.0000f, 0.0000f }, -302, 0.0220f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 0.2500f, 0.0000f, -5.0000f, 5000.0000f, 250.0000f, 0.0000f, 31 } },
	{ "Arena", { 9, 36.2000f, 1.0000f, -1000, -698, 0, 7.2400f, 0.3300f, 1.0000f, -1166, 0.0200f, { 0.0000f, 0.0000f, 0.0000f }, 16, 0.0300f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 0.2500f, 0.0000f, -5.0000f, 5000.0000f, 250.0000f, 0.0000f, 63 } },
	{ "Hangar", { 10, 50.3000f, 1.0000f, -1000, -1000, 0, 10.0500f, 0.2300f, 1.0000f, -602, 0.0200f, { 0.0000f, 0.0000f, 0.0000f }, 198, 0.0300f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 0.2500f, 0.0000f, -5.0000f, 5000.0000f, 250.0000f, 0.0000f, 63 } },
	{ "Carpetedhallway", { 11, 1.9000f, 1.0000f, -1000, -4000, 0, 0.3000f, 0.1000f, 1.0000f, -1831, 0.0020f, { 0.0000f, 0.0000f, 0.0000f }, -1630, 0.0300f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 0.2500f, 0.0000f, -5.0000f, 5000.0000f, 250.0000f, 0.0000f, 63 } },
	{ "Hallway", { 12, 1.8000f, 1.0000f, -1000, -300, 0, 1.4900f, 0.5900f, 1.0000f, -1219, 0.0070f, { 0.0000f, 0.0000f, 0.0000f }, 441, 0.0110f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 0.2500f, 0.0000f, -5.0000f, 5000.0000f, 250.0000f, 0.0000f, 63 } },
	{ "Stonecorridor", { 13, 13.5000f, 1.0000f, -1000, -237, 0, 2.7000f, 0.7900f, 1.0000f, -1214, 0.0130f, { 0.0000f, 0.0000f, 0.0000f }, 395, 0.0200f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 0.2500f, 0.0000f, -5.0000f, 5000.0000f, 250.0000f, 0.0000f, 63 } },
	{ "Alley", { 14, 7.5000f, 0.3000f, -1000, -270, 0, 1.4900f, 0.8600f, 1.0000f, -1204, 0.0070f, { 0.0000f, 0.0000f, 0.0000f }, -4, 0.0110f, { 0.0000f, 0.0000f, 0.0000f }, 0.1250f, 0.9500f, 0.2500f, 0.0000f, -5.0000f, 5000.0000f, 250.0000f, 0.0000f, 63 } },
	{ "Forest", { 15, 38.0000f, 0.3000f, -1000, -3300, 0, 1.4900f, 0.5400f, 1.0000f, -2560, 0.1620f, { 0.0000f, 0.0000f, 0.0000f }, -229, 0.0880f, { 0.0000f, 0.0000f, 0.0000f }, 0.1250f, 1.0000f, 0.2500f, 0.0000f, -5.0000f, 5000.0000f, 250.0000f, 0.0000f, 63 } },
	{ "City", { 16, 7.5000f, 0.5000f, -1000, -800, 0, 1.4900f, 0.6700f, 1.0000f, -2273, 0.0070f, { 0.0000f, 0.0000f, 0.0000f }, -1691, 0.0110f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 0.2500f, 0.0000f, -5.0000f, 5000.0000f, 250.0000f, 0.0000f, 63 } },
	{ "Mountains", { 17, 100.0000f, 0.2700f, -1000, -2500, 0, 1.4900f, 0.2100f, 1.0000f, -2780, 0.3000f, { 0.0000f, 0.0000f, 0.0000f }, -1434, 0.1000f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 1.0000f, 0.2500f, 0.0000f, -5.0000f, 5000.0000f, 250.0000f, 0.0000f, 31 } },
	{ "Quarry", { 18, 17.5000f, 1.0000f, -1000, -1000, 0, 1.4900f, 0.8300f, 1.0000f, -10000, 0.0610f, { 0.0000f, 0.0000f, 0.0000f }, 500, 0.0250f, { 0.0000f, 0.0000f, 0.0000f }, 0.1250f, 0.7000f, 0.2500f, 0.0000f, -5.0000f, 5000.0000f, 250.0000f, 0.0000f, 63 } },
	{ "Plain", { 19, 42.5000f, 0.2100f, -1000, -2000, 0, 1.4900f, 0.5000f, 1.0000f, -2466, 0.1790f, { 0.0000f, 0.0000f, 0.0000f }, -1926, 0.1000f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 1.0000f, 0.2500f, 0.0000f, -5.0000f, 5000.0000f, 250.0000f, 0.0000f, 31 } },
	{ "Parkinglot", { 20, 8.3000f, 1.0000f, -1000, 0, 0, 1.6500f, 1.5000f, 1.0000f, -1363, 0.0080f, { 0.0000f, 0.0000f, 0.0000f }, -1153, 0.0120f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 0.2500f, 0.0000f, -5.0000f, 5000.0000f, 250.0000f, 0.0000f, 31 } },
	{ "Sewerpipe", { 21, 1.7000f, 0.8000f, -1000, -1000, 0, 2.8100f, 0.1400f, 1.0000f, 429, 0.0140f, { 0.0000f, 0.0000f, 0.0000f }, 1023, 0.0210f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 0.2500f, 0.0000f, -5.0000f, 5000.0000f, 250.0000f, 0.0000f, 63 } },
	{ "Underwater", { 22, 1.8000f, 1.0000f, -1000, -4000, 0, 1.4900f, 0.1000f, 1.0000f, -449, 0.0070f, { 0.0000f, 0.0000f, 0.0000f }, 1700, 0.0110f, { 0.0000f, 0.0000f, 0.0000f }, 0.2500f, 0.0000f, 1.1800f, 0.3480f, -5.0000f, 5000.0000f, 250.0000f, 0.0000f, 63 } },
};

static const EFXEAXREVERBPROPERTIES efxPresets[] = {
	EFX_REVERB_PRESET_GENERIC, EFX_REVERB_PRESET_PADDEDCELL, EFX_REVERB_PRESET_ROOM, EFX_REVERB_PRESET_BATHROOM,
	EFX_REVERB_PRESET_LIVINGROOM, EFX_REVERB_PRESET_STONEROOM, EFX_REVERB_PRESET_AUDITORIUM, EFX_REVERB_PRESET_CONCERTHALL,
	EFX_REVERB_PRESET_CAVE, EFX_REVERB_PRESET_ARENA, EFX_REVERB_PRESET_HANGAR, EFX_REVERB_PRESET_CARPETEDHALLWAY,
	EFX_REVERB_PRESET_HALLWAY, EFX_REVERB_PRESET_STONECORRIDOR, EFX_REVERB_PRESET_ALLEY, EFX_REVERB_PRESET_FOREST,
	EFX_REVERB_PRESET_CITY, EFX_REVERB_PRESET_MOUNTAINS, EFX_REVERB_PRESET_QUARRY, EFX_REVERB_PRESET_PLAIN,
	EFX_REVERB_PRESET_PARKINGLOT, EFX_REVERB_PRESET_SEWERPIPE, EFX_REVERB_PRESET_UNDERWATER,
};

static void TestRanges() {
	CHECK( SOUND_REVERB_EFX_MAX_DENSITY == AL_EAXREVERB_MAX_DENSITY );
	CHECK( SOUND_REVERB_EFX_MAX_DIFFUSION == AL_EAXREVERB_MAX_DIFFUSION );
	CHECK( SOUND_REVERB_EFX_MAX_GAIN == AL_EAXREVERB_MAX_GAIN );
	CHECK( SOUND_REVERB_EFX_MAX_GAIN == AL_EAXREVERB_MAX_GAINHF && SOUND_REVERB_EFX_MAX_GAIN == AL_EAXREVERB_MAX_GAINLF );
	CHECK( SOUND_REVERB_EFX_MIN_DECAY_TIME == AL_EAXREVERB_MIN_DECAY_TIME && SOUND_REVERB_EFX_MAX_DECAY_TIME == AL_EAXREVERB_MAX_DECAY_TIME );
	CHECK( SOUND_REVERB_EFX_MIN_DECAY_RATIO == AL_EAXREVERB_MIN_DECAY_HFRATIO && SOUND_REVERB_EFX_MAX_DECAY_RATIO == AL_EAXREVERB_MAX_DECAY_HFRATIO );
	CHECK( SOUND_REVERB_EFX_MIN_DECAY_RATIO == AL_EAXREVERB_MIN_DECAY_LFRATIO && SOUND_REVERB_EFX_MAX_DECAY_RATIO == AL_EAXREVERB_MAX_DECAY_LFRATIO );
	CHECK( SOUND_REVERB_EFX_MAX_REFLECTIONS_GAIN == AL_EAXREVERB_MAX_REFLECTIONS_GAIN );
	CHECK( SOUND_REVERB_EFX_MAX_REFLECTIONS_DELAY == AL_EAXREVERB_MAX_REFLECTIONS_DELAY );
	CHECK( SOUND_REVERB_EFX_MAX_LATE_REVERB_GAIN == AL_EAXREVERB_MAX_LATE_REVERB_GAIN );
	CHECK( SOUND_REVERB_EFX_MAX_LATE_REVERB_DELAY == AL_EAXREVERB_MAX_LATE_REVERB_DELAY );
	CHECK( SOUND_REVERB_EFX_MIN_ECHO_TIME == AL_EAXREVERB_MIN_ECHO_TIME && SOUND_REVERB_EFX_MAX_ECHO_TIME == AL_EAXREVERB_MAX_ECHO_TIME );
	CHECK( SOUND_REVERB_EFX_MIN_MODULATION_TIME == AL_EAXREVERB_MIN_MODULATION_TIME && SOUND_REVERB_EFX_MAX_MODULATION_TIME == AL_EAXREVERB_MAX_MODULATION_TIME );
	CHECK( SOUND_REVERB_EFX_MIN_AIR_ABSORPTION_GAINHF == AL_EAXREVERB_MIN_AIR_ABSORPTION_GAINHF );
	CHECK( SOUND_REVERB_EFX_MIN_HFREFERENCE == AL_EAXREVERB_MIN_HFREFERENCE && SOUND_REVERB_EFX_MAX_HFREFERENCE == AL_EAXREVERB_MAX_HFREFERENCE );
	CHECK( SOUND_REVERB_EFX_MIN_LFREFERENCE == AL_EAXREVERB_MIN_LFREFERENCE && SOUND_REVERB_EFX_MAX_LFREFERENCE == AL_EAXREVERB_MAX_LFREFERENCE );
	CHECK( SOUND_REVERB_EFX_MAX_ROOM_ROLLOFF_FACTOR == AL_EAXREVERB_MAX_ROOM_ROLLOFF_FACTOR );
}

// Every retail preset converts to the parameters OpenAL Soft's efx-presets.h
// lists for the same EAX environment (rounded there to four places).
static void TestConversion() {
	CHECK( sizeof( retailPresets ) / sizeof( retailPresets[0] ) == 23 );
	CHECK( sizeof( efxPresets ) / sizeof( efxPresets[0] ) == 23 );
	const float tolerance = 0.0006f;
	for ( int i = 0; i < 23; i++ ) {
		soundReverbEFX_t efx;
		SoundReverb_ConvertEAXToEFX( retailPresets[i].eax, efx );
		const EFXEAXREVERBPROPERTIES &expected = efxPresets[i];
		const bool same =
			Near( efx.density, expected.flDensity, tolerance ) &&
			Near( efx.diffusion, expected.flDiffusion, tolerance ) &&
			Near( efx.gain, expected.flGain, tolerance ) &&
			Near( efx.gainHF, expected.flGainHF, tolerance ) &&
			Near( efx.gainLF, expected.flGainLF, tolerance ) &&
			Near( efx.decayTime, expected.flDecayTime, tolerance ) &&
			Near( efx.decayHFRatio, expected.flDecayHFRatio, tolerance ) &&
			Near( efx.decayLFRatio, expected.flDecayLFRatio, tolerance ) &&
			Near( efx.reflectionsGain, expected.flReflectionsGain, tolerance ) &&
			Near( efx.reflectionsDelay, expected.flReflectionsDelay, tolerance ) &&
			Near( efx.lateReverbGain, expected.flLateReverbGain, tolerance * 4.0f ) &&
			Near( efx.lateReverbDelay, expected.flLateReverbDelay, tolerance ) &&
			Near( efx.echoTime, expected.flEchoTime, tolerance ) &&
			Near( efx.echoDepth, expected.flEchoDepth, tolerance ) &&
			Near( efx.modulationTime, expected.flModulationTime, tolerance ) &&
			Near( efx.modulationDepth, expected.flModulationDepth, tolerance ) &&
			Near( efx.airAbsorptionGainHF, expected.flAirAbsorptionGainHF, tolerance ) &&
			Near( efx.hfReference, expected.flHFReference, tolerance ) &&
			Near( efx.lfReference, expected.flLFReference, tolerance ) &&
			Near( efx.roomRolloffFactor, expected.flRoomRolloffFactor, tolerance ) &&
			efx.decayHFLimit == ( ( retailPresets[i].eax.flags & SOUND_REVERB_FLAG_DECAY_HF_LIMIT ) != 0 ? 1 : 0 ) &&
			// efx-presets.h sets the decay HF limit on Plain; Creative's EAX table, and
			// so Quake 4's file, clears it (flags 31). The file wins.
			( efx.decayHFLimit == expected.iDecayHFLimit || std::strcmp( retailPresets[i].name, "Plain" ) == 0 );
		if ( !same ) {
			std::fprintf( stderr, "preset %s: density %f/%f gainHF %f/%f refl %f/%f late %f/%f hflimit %d/%d\n",
				retailPresets[i].name, efx.density, expected.flDensity, efx.gainHF, expected.flGainHF,
				efx.reflectionsGain, expected.flReflectionsGain, efx.lateReverbGain, expected.flLateReverbGain,
				efx.decayHFLimit, expected.iDecayHFLimit );
		}
		CHECK( same );
	}

	// EAX's -10000 mB floor is silence, as in OpenAL Soft
	CHECK( SoundReverb_MillibelsToGain( -10000.0f ) == 0.0f );
	CHECK( SoundReverb_MillibelsToGain( 0.0f ) == 1.0f );
	CHECK( Near( SoundReverb_MillibelsToGain( -2000.0f ), 0.1f, 0.00001f ) );

	// malformed data lands inside the EFX ranges instead of being rejected
	soundReverbEAX_t broken = retailPresets[0].eax;
	broken.environmentSize = 1000.0f;
	broken.room = 5000;
	broken.decayTime = 0.0f;
	broken.reverb = 9000;
	broken.reflectionsPan[1] = NAN;
	soundReverbEFX_t clamped;
	SoundReverb_ConvertEAXToEFX( broken, clamped );
	CHECK( clamped.density == 1.0f && clamped.gain == 1.0f && clamped.decayTime == SOUND_REVERB_EFX_MIN_DECAY_TIME );
	CHECK( clamped.lateReverbGain == SOUND_REVERB_EFX_MAX_LATE_REVERB_GAIN && clamped.reflectionsPan[1] == 0.0f );

	// pans pass through unchanged: EAX and EFX share the left-handed frame
	soundReverbEAX_t panned = retailPresets[12].eax;
	panned.reflectionsPan[0] = 0.25f;
	panned.reverbPan[2] = -0.5f;
	soundReverbEFX_t pannedEfx;
	SoundReverb_ConvertEAXToEFX( panned, pannedEfx );
	CHECK( pannedEfx.reflectionsPan[0] == 0.25f && pannedEfx.lateReverbPan[2] == -0.5f );

	soundReverbEAX_t defaults;
	SoundReverb_DefaultEAX( defaults );
	CHECK( std::memcmp( &defaults, &retailPresets[0].eax, sizeof( defaults ) ) == 0 );
	soundReverbEFX_t a, b;
	SoundReverb_ConvertEAXToEFX( retailPresets[3].eax, a );
	b = a;
	CHECK( SoundReverb_EFXEqual( a, b ) );
	b.lateReverbPan[1] = 0.1f;
	CHECK( !SoundReverb_EFXEqual( a, b ) );
}

// ---------------------------------------------------------------------------
// slot planning

struct areaEffects_t {
	int effectForArea[64];
};

static int TestEffectForArea( void *context, int area ) {
	const areaEffects_t *table = static_cast<const areaEffects_t *>( context );
	if ( area < 0 || area >= 64 ) {
		return -1;
	}
	return table->effectForArea[area];
}

static soundReverbListener_t MakeListener( float x, float y, float z, int area ) {
	soundReverbListener_t listener;
	listener.origin[0] = x;
	listener.origin[1] = y;
	listener.origin[2] = z;
	std::memset( listener.axis, 0, sizeof( listener.axis ) );
	listener.axis[0][0] = 1.0f;	// forward +x
	listener.axis[1][1] = 1.0f;	// left +y
	listener.axis[2][2] = 1.0f;	// up +z
	listener.area = area;
	return listener;
}

// A square portal centered at (x, y, z), facing x, with the given half size.
static soundReverbPortal_t MakePortal( int area, float x, float y, float z, float halfSize ) {
	soundReverbPortal_t portal;
	portal.area = area;
	portal.center[0] = x;
	portal.center[1] = y;
	portal.center[2] = z;
	portal.mins[0] = x;
	portal.mins[1] = y - halfSize;
	portal.mins[2] = z - halfSize;
	portal.maxs[0] = x;
	portal.maxs[1] = y + halfSize;
	portal.maxs[2] = z + halfSize;
	return portal;
}

static void ResetSlots( int slotToArea[SOUND_REVERB_SLOTS] ) {
	for ( int i = 0; i < SOUND_REVERB_SLOTS; i++ ) {
		slotToArea[i] = -1;
	}
}

static void TestPlanner() {
	areaEffects_t effects;
	for ( int i = 0; i < 64; i++ ) {
		effects.effectForArea[i] = i % 23;
	}
	int slotToArea[SOUND_REVERB_SLOTS];
	soundReverbSlotPlan_t plan;

	// no portals: the listener's area takes the first slot and is primary, unpanned
	ResetSlots( slotToArea );
	soundReverbListener_t listener = MakeListener( 0.0f, 0.0f, 0.0f, 5 );
	SoundReverb_PlanSlots( listener, nullptr, 0, SOUND_REVERB_SLOTS, slotToArea, TestEffectForArea, &effects, false, plan );
	CHECK( slotToArea[0] == 5 && slotToArea[1] == -1 && plan.primarySlot == 0 );
	CHECK( plan.configured[0] && !plan.configured[1] && plan.effect[0] == 5 );
	CHECK( plan.pan[0][0] == 0.0f && plan.pan[0][1] == 0.0f && plan.pan[0][2] == 0.0f );

	// five portals: the three nearest by winding center are kept, in fill order
	soundReverbPortal_t portals[5] = {
		MakePortal( 10, 400.0f, 0.0f, 0.0f, 32.0f ),	// farthest
		MakePortal( 11, 100.0f, 0.0f, 0.0f, 32.0f ),	// nearest
		MakePortal( 12, 200.0f, 0.0f, 0.0f, 32.0f ),
		MakePortal( 13, 300.0f, 0.0f, 0.0f, 32.0f ),
		MakePortal( 14, 250.0f, 0.0f, 0.0f, 32.0f ),
	};
	ResetSlots( slotToArea );
	SoundReverb_PlanSlots( listener, portals, 5, SOUND_REVERB_SLOTS, slotToArea, TestEffectForArea, &effects, false, plan );
	// replacement always evicts the farthest entry: 10,11,12 fill 1-3, 13 replaces 10, 14 replaces 13
	CHECK( slotToArea[0] == 5 && slotToArea[1] == 14 && slotToArea[2] == 11 && slotToArea[3] == 12 );
	CHECK( plan.primarySlot == 0 );
	for ( int i = 0; i < SOUND_REVERB_SLOTS; i++ ) {
		CHECK( plan.configured[i] );
	}

	// remote areas pan toward their portal (straight ahead: +z in EAX space), with a
	// narrow, distant portal focused near 1 and the listener's own area pulled away
	// from its nearest portal
	CHECK( Near( plan.pan[2][0], 0.0f, 0.0001f ) && Near( plan.pan[2][1], 0.0f, 0.0001f ) );
	const float spread = 1.0f + ( 100.0f * 100.0f - 32.0f * 32.0f * 2.0f ) / ( 100.0f * 100.0f + 32.0f * 32.0f * 2.0f );
	CHECK( Near( plan.pan[2][2], spread * 0.181f + 0.637f, 0.0005f ) );
	CHECK( Near( plan.pan[0][2], spread * 0.3185f - 0.637f, 0.0005f ) );
	CHECK( plan.pan[0][2] < 0.0f && plan.pan[0][2] > -0.637f );

	// the listener walks into area 12: its slot stays pinned and becomes primary,
	// area 5 stays in slot 0 as a neighbour, nothing else moves
	soundReverbPortal_t fromTwelve[3] = {
		MakePortal( 5, 150.0f, 0.0f, 0.0f, 32.0f ),
		MakePortal( 11, 160.0f, 50.0f, 0.0f, 32.0f ),
		MakePortal( 20, 500.0f, 0.0f, 0.0f, 32.0f ),
	};
	listener = MakeListener( 210.0f, 0.0f, 0.0f, 12 );
	SoundReverb_PlanSlots( listener, fromTwelve, 3, SOUND_REVERB_SLOTS, slotToArea, TestEffectForArea, &effects, false, plan );
	CHECK( slotToArea[0] == 5 && slotToArea[2] == 11 && slotToArea[3] == 12 && plan.primarySlot == 3 );
	// area 14 dropped out of the nearest set; the freed slot 1 takes area 20
	CHECK( slotToArea[1] == 20 );
	// the listener now faces away from area 5's portal, so its pan points backward
	CHECK( plan.pan[0][2] < 0.0f );

	// an area reached through two portals: 1.4.2 keeps scanning after a match, so the
	// pinned slot takes the later portal's direction
	soundReverbPortal_t twice[2] = {
		MakePortal( 30, 0.0f, 100.0f, 0.0f, 16.0f ),	// to the left
		MakePortal( 30, 0.0f, -90.0f, 0.0f, 16.0f ),	// to the right, nearer
	};
	ResetSlots( slotToArea );
	slotToArea[2] = 30;
	listener = MakeListener( 0.0f, 0.0f, 0.0f, 6 );
	SoundReverb_PlanSlots( listener, twice, 2, SOUND_REVERB_SLOTS, slotToArea, TestEffectForArea, &effects, false, plan );
	CHECK( slotToArea[2] == 30 && slotToArea[0] == 6 && slotToArea[1] == -1 && slotToArea[3] == -1 );
	CHECK( plan.pan[2][0] > 0.0f );	// the second portal is on the listener's right (+x in EAX)

	// an area with no effect is not given a slot
	ResetSlots( slotToArea );
	effects.effectForArea[40] = -1;
	soundReverbPortal_t noEffect[1] = { MakePortal( 40, 100.0f, 0.0f, 0.0f, 16.0f ) };
	SoundReverb_PlanSlots( listener, noEffect, 1, SOUND_REVERB_SLOTS, slotToArea, TestEffectForArea, &effects, false, plan );
	CHECK( slotToArea[0] == 6 && slotToArea[1] == -1 && !plan.configured[1] );

	// a listener area without an effect leaves the primary slot unchanged
	ResetSlots( slotToArea );
	listener = MakeListener( 0.0f, 0.0f, 0.0f, 40 );
	SoundReverb_PlanSlots( listener, nullptr, 0, SOUND_REVERB_SLOTS, slotToArea, TestEffectForArea, &effects, false, plan );
	CHECK( plan.primarySlot == -1 && slotToArea[0] == -1 );

	// a single slot follows the listener alone
	ResetSlots( slotToArea );
	listener = MakeListener( 0.0f, 0.0f, 0.0f, 7 );
	SoundReverb_PlanSlots( listener, portals, 5, 1, slotToArea, TestEffectForArea, &effects, false, plan );
	CHECK( slotToArea[0] == 7 && slotToArea[1] == -1 && plan.primarySlot == 0 && !plan.configured[1] );

	// s_muteEAXReverb mutes only the listener's slot
	ResetSlots( slotToArea );
	SoundReverb_PlanSlots( listener, portals, 5, SOUND_REVERB_SLOTS, slotToArea, TestEffectForArea, &effects, true, plan );
	CHECK( plan.muteRoom[0] && !plan.muteRoom[1] && !plan.muteRoom[2] && !plan.muteRoom[3] );

	CHECK( SoundReverb_SlotForArea( slotToArea, SOUND_REVERB_SLOTS, 11 ) == 2 );
	CHECK( SoundReverb_SlotForArea( slotToArea, SOUND_REVERB_SLOTS, 99 ) == -1 );
	CHECK( SoundReverb_SlotForArea( slotToArea, 1, 11 ) == -1 );
}

// ---------------------------------------------------------------------------
// source sends

static void TestSends() {
	soundReverbSourceSends_t sends;

	SoundReverb_SourceSends( 0, 0, sends );
	CHECK( sends.directGain == 1.0f && sends.directGainHF == 1.0f && sends.primaryGain == 1.0f && sends.primaryGainHF == 1.0f );
	CHECK( sends.areaGain == 1.0f && sends.areaGainHF == 1.0f );

	// radio chatter is kept out of every room
	SoundReverb_SourceSends( SOUND_REVERB_ROOM_RADIO_MB, 0, sends );
	CHECK( sends.primaryGain == 0.0f && sends.areaGain == 0.0f && sends.directGain == 1.0f );

	// voice-over sits 5 dB back in the room
	SoundReverb_SourceSends( SOUND_REVERB_ROOM_VOICEOVER_MB, 0, sends );
	CHECK( Near( sends.primaryGain, 0.56234f, 0.0001f ) && Near( sends.areaGain, 0.56234f, 0.0001f ) );

	// one blocking portal: direct loses 15 dB of highs only, the listener-room send
	// 7.5 dB overall and 15 dB more in the highs, the source's own room nothing
	SoundReverb_SourceSends( 0, SOUND_REVERB_OCCLUSION_PER_PORTAL_MB, sends );
	CHECK( sends.directGain == 1.0f && Near( sends.directGainHF, 0.17783f, 0.0001f ) );
	CHECK( Near( sends.primaryGain, 0.42170f, 0.0001f ) && Near( sends.primaryGainHF, 0.17783f, 0.0001f ) );
	CHECK( sends.areaGain == 1.0f && sends.areaGainHF == 1.0f );

	// occlusion and room level add
	SoundReverb_SourceSends( SOUND_REVERB_ROOM_VOICEOVER_MB, 2 * SOUND_REVERB_OCCLUSION_PER_PORTAL_MB, sends );
	CHECK( Near( sends.primaryGain, SoundReverb_MillibelsToGain( -500.0f - 1500.0f ), 0.00001f ) );
	CHECK( Near( sends.directGainHF, SoundReverb_MillibelsToGain( -3000.0f ), 0.00001f ) );

	// seven doors reach the -10000 mB floor
	SoundReverb_SourceSends( 0, 7 * SOUND_REVERB_OCCLUSION_PER_PORTAL_MB, sends );
	CHECK( sends.directGainHF == 0.0f && sends.directGain == 1.0f );
}

// A centre-routed voice's sends take the room rolloff OpenAL gives a mono source
// (audio_mod_reverb.py renders both and compares them).
static void TestRoomRolloff() {
	// stock presets have none, and inside the reference distance nothing changes
	CHECK( SoundReverb_RoomRolloffGain( 400.0f, 100.0f, 0.0f ) == 1.0f );
	CHECK( SoundReverb_RoomRolloffGain( 0.0f, 100.0f, 1.0f ) == 1.0f );
	CHECK( SoundReverb_RoomRolloffGain( 100.0f, 100.0f, 1.0f ) == 1.0f );
	// inverse distance: half at twice the reference, a quarter at four times
	CHECK( Near( SoundReverb_RoomRolloffGain( 200.0f, 100.0f, 1.0f ), 0.5f, 1e-6f ) );
	CHECK( Near( SoundReverb_RoomRolloffGain( 400.0f, 100.0f, 1.0f ), 0.25f, 1e-6f ) );
	CHECK( Near( SoundReverb_RoomRolloffGain( 200.0f, 100.0f, 2.0f ), 1.0f / 3.0f, 1e-6f ) );
	CHECK( Near( SoundReverb_RoomRolloffGain( 150.0f, 100.0f, 0.5f ), 100.0f / 125.0f, 1e-6f ) );
	// the units cancel: map units and metres give the same gain
	CHECK( Near( SoundReverb_RoomRolloffGain( 2.0f, 1.0f, 1.0f ), SoundReverb_RoomRolloffGain( 200.0f, 100.0f, 1.0f ), 1e-6f ) );
	// the most the EFX range allows, far away, stays a finite gain above zero
	const float far = SoundReverb_RoomRolloffGain( 1.0e6f, 1.0f, SOUND_REVERB_EFX_MAX_ROOM_ROLLOFF_FACTOR );
	CHECK( far > 0.0f && far < 1.0e-6f );
	// monotonic in distance
	float previous = 1.0f;
	for ( float distance = 0.0f; distance <= 4096.0f; distance += 16.0f ) {
		const float gain = SoundReverb_RoomRolloffGain( distance, 64.0f, 1.0f );
		CHECK( gain <= previous && gain > 0.0f );
		previous = gain;
	}
	// no reference, invalid or infinite inputs leave the send alone or silence it, never NaN
	CHECK( SoundReverb_RoomRolloffGain( 200.0f, 0.0f, 1.0f ) == 1.0f );
	CHECK( SoundReverb_RoomRolloffGain( 200.0f, -5.0f, 1.0f ) == 1.0f );
	CHECK( SoundReverb_RoomRolloffGain( 200.0f, 100.0f, -1.0f ) == 1.0f );
	CHECK( SoundReverb_RoomRolloffGain( std::nanf( "" ), 100.0f, 1.0f ) == 1.0f );
	CHECK( SoundReverb_RoomRolloffGain( 200.0f, std::nanf( "" ), 1.0f ) == 1.0f );
	CHECK( SoundReverb_RoomRolloffGain( 200.0f, 100.0f, std::nanf( "" ) ) == 1.0f );
	CHECK( SoundReverb_RoomRolloffGain( 200.0f, INFINITY, 1.0f ) == 1.0f );
	const float infinite = SoundReverb_RoomRolloffGain( INFINITY, 100.0f, 1.0f );
	CHECK( std::isfinite( infinite ) && infinite >= 0.0f && infinite < 1.0e-30f );
}

// s_reverbIdleSlots and s_reverbPanStep (openQ4)
static void TestSlotEconomy() {
	soundReverbEAX_t eax;
	SoundReverb_DefaultEAX( eax );
	soundReverbEFX_t efx;
	SoundReverb_ConvertEAXToEFX( eax, efx );
	// Generic: 1.49 s decay, HF ratio 0.83, LF 1.0, delays 7 ms + 11 ms
	const int generic = SoundReverb_TailHoldMs( efx );
	CHECK( generic >= 1490 * 3 / 2 + 18 + 250 && generic <= 1490 * 3 / 2 + 18 + 252 );
	// the slowest band sets the hold
	soundReverbEFX_t slowLow = efx;
	slowLow.decayLFRatio = 2.0f;
	CHECK( SoundReverb_TailHoldMs( slowLow ) > generic + 2000 );
	// every retail preset is covered by the unknown-parameter hold
	const int longest = SoundReverb_MaxTailHoldMs();
	CHECK( longest >= 30000 && longest <= 31000 );
	for ( const retailPreset_t &preset : retailPresets ) {
		soundReverbEFX_t p;
		SoundReverb_ConvertEAXToEFX( preset.eax, p );
		const int hold = SoundReverb_TailHoldMs( p );
		CHECK( hold > (int)( p.decayTime * 1500.0f ) && hold <= longest );
	}
	soundReverbEFX_t broken = efx;
	broken.decayTime = std::nanf( "" );
	broken.reflectionsDelay = INFINITY;
	CHECK( SoundReverb_TailHoldMs( broken ) > 0 && SoundReverb_TailHoldMs( broken ) <= longest );

	// pan steps: only a pan-only change under the step is held
	soundReverbEFX_t turned = efx;
	turned.reflectionsPan[0] = 0.03f;
	turned.lateReverbPan[0] = 0.03f;
	CHECK( SoundReverb_PanWithinStep( efx, efx, 0.0f ) );
	CHECK( SoundReverb_PanWithinStep( efx, turned, 0.05f ) );
	CHECK( !SoundReverb_PanWithinStep( efx, turned, 0.0f ) );
	CHECK( !SoundReverb_PanWithinStep( efx, turned, 0.02f ) );
	soundReverbEFX_t lateOnly = efx;
	lateOnly.lateReverbPan[2] = 0.06f;
	CHECK( !SoundReverb_PanWithinStep( efx, lateOnly, 0.05f ) );
	soundReverbEFX_t louder = turned;
	louder.gain = efx.gain * 0.5f;
	CHECK( !SoundReverb_PanWithinStep( efx, louder, 0.5f ) );
	CHECK( !SoundReverb_PanWithinStep( efx, turned, std::nanf( "" ) ) );
}

int main() {
	TestRanges();
	TestConversion();
	TestPlanner();
	TestSends();
	TestRoomRolloff();
	TestSlotEconomy();
	std::printf( "SoundReverbCore: %d checks passed\n", checks );
	return 0;
}
