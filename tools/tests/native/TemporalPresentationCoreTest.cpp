// Copyright (C) 2026 DarkMatter Productions
//

#include "src/renderer/TemporalPresentationCore.h"

#include <cmath>
#include <cstdio>

static int failures = 0;

static void Check( bool condition, const char *message ) {
	if ( condition ) {
		return;
	}
	std::fprintf( stderr, "TemporalPresentationCoreTest: %s\n", message );
	failures++;
}

static temporalResolutionConfig_t DefaultConfig() {
	temporalResolutionConfig_t config = {};
	config.enabled = true;
	config.minimumScalePercent = 50;
	config.maximumScalePercent = 100;
	config.dropStepPercent = 10;
	config.raiseStepPercent = 2;
	config.raiseFrames = 3;
	config.raiseThresholdPercent = 85;
	config.missingSampleResetFrames = 120;
	config.maximumSampleAgeFrames = 32;
	config.targetMicroseconds = 16000;
	config.dimensionAlignment = 8;
	return config;
}

static temporalResolutionSample_t Sample( int frame, std::uint32_t generation,
		std::uint64_t microseconds, int scalePercent = 100, int ageFrames = 4 ) {
	temporalResolutionSample_t sample = {};
	sample.supported = true;
	sample.valid = true;
	sample.backend = 1;
	sample.frameNumber = frame;
	sample.generation = generation;
	sample.elapsedMicroseconds = microseconds;
	sample.scalePercent = scalePercent;
	sample.ageFrames = ageFrames;
	return sample;
}

int main() {
	temporalResolutionState_t state;
	TemporalResolutionCore_Initialize( state );
	temporalResolutionConfig_t config = DefaultConfig();

	temporalResolutionSample_t sample = Sample( 10, 3, 17000 );
	temporalResolutionOutput_t output = TemporalResolutionCore_Update(
		state, config, sample, 1920, 1080, false, false );
	Check( output.decision == TEMPORAL_RESOLUTION_DECISION_DISCONTINUITY_RESET
			&& output.scalePercent == 100 && output.width == 1920
			&& output.height == 1080,
		"the first retired sample must establish a native-resolution generation" );

	sample = Sample( 11, 3, 20000 );
	output = TemporalResolutionCore_Update( state, config, sample, 1920, 1080, false, false );
	Check( output.sampleConsumed
			&& output.decision == TEMPORAL_RESOLUTION_DECISION_DROP
			&& output.scalePercent == 90 && output.width == 1728
			&& output.height == 968,
		"an over-budget sample must drop quickly and align both dimensions" );

	output = TemporalResolutionCore_Update( state, config, sample, 1920, 1080, false, false );
	Check( !output.sampleConsumed
			&& output.decision == TEMPORAL_RESOLUTION_DECISION_WAITING_FOR_SAMPLE
			&& output.scalePercent == 90,
		"a delayed timing sample must be consumed at most once" );

	for ( int frame = 12; frame <= 13; frame++ ) {
		output = TemporalResolutionCore_Update(
			state, config, Sample( frame, 3, 10000, 90 ), 1920, 1080, false, false );
		Check( output.scalePercent == 90
				&& output.decision == TEMPORAL_RESOLUTION_DECISION_HOLD,
			"under-budget hysteresis must hold before its full streak" );
	}
	output = TemporalResolutionCore_Update(
		state, config, Sample( 14, 3, 10000, 90 ), 1920, 1080, false, false );
	Check( output.decision == TEMPORAL_RESOLUTION_DECISION_RAISE
			&& output.scalePercent == 92,
		"a complete under-budget streak must raise slowly" );

	output = TemporalResolutionCore_Update(
		state, config, Sample( 15, 3, 15000, 92 ), 1920, 1080, false, false );
	Check( output.decision == TEMPORAL_RESOLUTION_DECISION_HOLD
			&& state.belowBudgetFrames == 0,
		"the target deadband must break a pending raise streak" );

	output = TemporalResolutionCore_Update(
		state, config, Sample( 16, 4, 30000 ), 1920, 1080, false, false );
	Check( output.decision == TEMPORAL_RESOLUTION_DECISION_DISCONTINUITY_RESET
			&& output.scalePercent == 100 && state.discontinuityResets == 2,
		"a timing-generation change must reset rather than consume an outlier" );

	output = TemporalResolutionCore_Update(
		state, config, Sample( 17, 4, 30000 ), 1920, 1080, true, false );
	Check( output.decision == TEMPORAL_RESOLUTION_DECISION_FROZEN
			&& output.scalePercent == 100 && output.width == 1920
			&& output.height == 1080 && state.lastSampleFrame == 16,
		"ordinary captures must freeze rather than consume timing or mutate scale" );

	output = TemporalResolutionCore_Update(
		state, config, Sample( 17, 4, 30000 ), 1920, 1080, true, true );
	Check( output.decision == TEMPORAL_RESOLUTION_DECISION_FORCED_NATIVE
			&& output.scalePercent == 100 && output.width == 1920
			&& output.height == 1080,
		"an explicit capture-quality override must force native scene resolution" );

	// Return to a reduced scale, then prove that capture freezing preserves it.
	output = TemporalResolutionCore_Update(
		state, config, Sample( 17, 4, 30000, 100 ), 1920, 1080, false, false );
	Check( output.scalePercent == 90, "post-capture timing must remain consumable" );
	output = TemporalResolutionCore_Update(
		state, config, Sample( 18, 4, 30000, 90 ), 320, 240, true, false );
	Check( output.decision == TEMPORAL_RESOLUTION_DECISION_FROZEN
			&& output.scalePercent == 90 && output.width == 288
			&& output.height == 216 && state.nativeWidth == 1920,
		"save-preview dimensions must not reset the gameplay controller" );

	output = TemporalResolutionCore_Update(
		state, config, Sample( 15, 4, 1000, 90 ), 1920, 1080, false, false );
	Check( !output.sampleConsumed && output.scalePercent == 90
			&& state.rejectedSamples == 1,
		"an out-of-order retired sample must never perturb the controller" );

	config.minimumScalePercent = 80;
	output = TemporalResolutionCore_Update(
		state, config, Sample( 19, 4, 30000 ), 1920, 1080, false, false );
	Check( output.decision == TEMPORAL_RESOLUTION_DECISION_DISCONTINUITY_RESET
			&& output.scalePercent == 100,
		"a live controller configuration change must restart at its safe ceiling" );

	config.enabled = false;
	output = TemporalResolutionCore_Update(
		state, config, Sample( 20, 4, 30000 ), 1920, 1080, false, false );
	Check( output.decision == TEMPORAL_RESOLUTION_DECISION_DISABLED
			&& output.scalePercent == 100,
		"the default-off controller must preserve native resolution" );

	Check( TemporalResolutionCore_AlignedDimension( 13, 50, 8 ) == 7,
		"alignment must never collapse a small dimension to zero" );
	Check( TemporalResolutionCore_AlignedDimension( 1920, 75, 8 ) == 1440,
		"aligned dimension calculation mismatch" );
	Check( TemporalResolutionCore_AlignedDimension( 1366, 100, 8 ) == 1366
			&& TemporalResolutionCore_AlignedDimension( 767, 100, 8 ) == 767,
		"100 percent scale must preserve the exact native extent" );

	// Texture LOD bias while temporally upscaling (REN-04).
	Check( TemporalResolutionCore_UpscaleLodBias( 100, true, 1.0f ) == 0.0f
			&& TemporalResolutionCore_UpscaleLodBias( 150, true, 1.0f ) == 0.0f
			&& TemporalResolutionCore_UpscaleLodBias( 50, false, 1.0f ) == 0.0f
			&& TemporalResolutionCore_UpscaleLodBias( 50, true, 0.0f ) == 0.0f,
		"native, supersampled, non-temporal and disabled frames must stay unbiased" );
	Check( std::fabs( TemporalResolutionCore_UpscaleLodBias( 50, true, 1.0f ) + 1.0f ) < 1.0e-5f
			&& std::fabs( TemporalResolutionCore_UpscaleLodBias( 25, true, 0.5f ) + 1.0f ) < 1.0e-5f,
		"the bias must be strength * log2( scale )" );
	Check( TemporalResolutionCore_LodBiasStep( 0.0f, -4 ) == 0,
		"an unbiased frame must return to exactly zero whatever was applied" );
	Check( TemporalResolutionCore_LodBiasStep( -1.0f, 0 ) == -4
			&& TemporalResolutionCore_LodBiasStep( -0.415f, 0 ) == -2
			&& TemporalResolutionCore_LodBiasStep( -5.0f, 0 ) == TEMPORAL_LOD_BIAS_MIN_STEP,
		"a new bias must round to the nearest quarter mip inside the two-mip bound" );
	Check( TemporalResolutionCore_LodBiasStep( -0.415f, -2 ) == -2
			&& TemporalResolutionCore_LodBiasStep( -0.60f, -2 ) == -2
			&& TemporalResolutionCore_LodBiasStep( -0.33f, -2 ) == -2
			&& TemporalResolutionCore_LodBiasStep( -0.70f, -2 ) == -3
			&& TemporalResolutionCore_LodBiasStep( -0.60f, -3 ) == -3,
		"a scale hovering between two steps must keep the applied one" );
	Check( TemporalResolutionCore_LodBiasStep( -0.10f, 0 ) == 0
			&& TemporalResolutionCore_LodBiasStep( -0.20f, 0 ) == -1,
		"a slight upscale must not bias until it reaches most of a step" );

	if ( failures != 0 ) {
		return 1;
	}
	std::printf( "TemporalPresentationCoreTest: PASS\n" );
	return 0;
}
