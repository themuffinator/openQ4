// Copyright (C) 2026 DarkMatter Productions
//

#ifndef __TEMPORAL_PRESENTATION_CORE_H__
#define __TEMPORAL_PRESENTATION_CORE_H__

#include <cmath>
#include <cstdint>

// Device-independent dynamic-resolution policy.  The renderer feeds this only
// retired whole-frame GPU samples; no function in this contract can wait on a
// graphics API query or fence.  Keeping the policy free of idlib, GL, and
// Vulkan types lets the native test exercise every transition deterministically.

typedef enum temporalResolutionDecision_e {
	TEMPORAL_RESOLUTION_DECISION_DISABLED = 0,
	TEMPORAL_RESOLUTION_DECISION_FORCED_NATIVE,
	TEMPORAL_RESOLUTION_DECISION_FROZEN,
	TEMPORAL_RESOLUTION_DECISION_WAITING_FOR_SAMPLE,
	TEMPORAL_RESOLUTION_DECISION_DISCONTINUITY_RESET,
	TEMPORAL_RESOLUTION_DECISION_HOLD,
	TEMPORAL_RESOLUTION_DECISION_DROP,
	TEMPORAL_RESOLUTION_DECISION_RAISE
} temporalResolutionDecision_t;

typedef struct temporalResolutionConfig_s {
	bool		enabled;
	int		minimumScalePercent;
	int		maximumScalePercent;
	int		dropStepPercent;
	int		raiseStepPercent;
	int		raiseFrames;
	int		raiseThresholdPercent;
	int		missingSampleResetFrames;
	int		maximumSampleAgeFrames;
	std::uint64_t	targetMicroseconds;
	int		dimensionAlignment;
} temporalResolutionConfig_t;

typedef struct temporalResolutionSample_s {
	bool		supported;
	bool		valid;
	int		backend;
	int		frameNumber;
	std::uint32_t	generation;
	std::uint64_t	elapsedMicroseconds;
	int		scalePercent;
	int		ageFrames;
} temporalResolutionSample_t;

typedef struct temporalResolutionState_s {
	bool		initialized;
	bool		sampleIdentityValid;
	int		backend;
	int		lastSampleFrame;
	std::uint32_t	generation;
	std::uint64_t	configSignature;
	int		nativeWidth;
	int		nativeHeight;
	int		currentScalePercent;
	int		belowBudgetFrames;
	int		missingSampleFrames;
	std::uint64_t	processedSamples;
	std::uint64_t	rejectedSamples;
	std::uint64_t	droppedScaleChanges;
	std::uint64_t	raisedScaleChanges;
	std::uint64_t	discontinuityResets;
} temporalResolutionState_t;

typedef struct temporalResolutionOutput_s {
	temporalResolutionDecision_t decision;
	bool		sampleConsumed;
	int		scalePercent;
	int		width;
	int		height;
} temporalResolutionOutput_t;

inline int TemporalResolutionCore_ClampInt( int minimum, int maximum, int value ) {
	return value < minimum ? minimum : ( value > maximum ? maximum : value );
}

inline void TemporalResolutionCore_Initialize( temporalResolutionState_t &state ) {
	state.initialized = false;
	state.sampleIdentityValid = false;
	state.backend = 0;
	state.lastSampleFrame = -1;
	state.generation = 0;
	state.configSignature = 0;
	state.nativeWidth = 0;
	state.nativeHeight = 0;
	state.currentScalePercent = 100;
	state.belowBudgetFrames = 0;
	state.missingSampleFrames = 0;
	state.processedSamples = 0;
	state.rejectedSamples = 0;
	state.droppedScaleChanges = 0;
	state.raisedScaleChanges = 0;
	state.discontinuityResets = 0;
}

inline std::uint64_t TemporalResolutionCore_MixSignature(
		std::uint64_t signature, std::uint64_t value ) {
	// A small deterministic combiner is sufficient here: this is change
	// detection, not an externally supplied hash table key.
	return signature ^ ( value + UINT64_C( 0x9e3779b97f4a7c15 )
		+ ( signature << 6 ) + ( signature >> 2 ) );
}

inline std::uint64_t TemporalResolutionCore_ConfigSignature(
		const temporalResolutionConfig_t &config ) {
	std::uint64_t signature = UINT64_C( 0xcbf29ce484222325 );
	signature = TemporalResolutionCore_MixSignature( signature,
		static_cast<std::uint64_t>( config.minimumScalePercent ) );
	signature = TemporalResolutionCore_MixSignature( signature,
		static_cast<std::uint64_t>( config.maximumScalePercent ) );
	signature = TemporalResolutionCore_MixSignature( signature,
		static_cast<std::uint64_t>( config.dropStepPercent ) );
	signature = TemporalResolutionCore_MixSignature( signature,
		static_cast<std::uint64_t>( config.raiseStepPercent ) );
	signature = TemporalResolutionCore_MixSignature( signature,
		static_cast<std::uint64_t>( config.raiseFrames ) );
	signature = TemporalResolutionCore_MixSignature( signature,
		static_cast<std::uint64_t>( config.raiseThresholdPercent ) );
	signature = TemporalResolutionCore_MixSignature( signature,
		static_cast<std::uint64_t>( config.missingSampleResetFrames ) );
	signature = TemporalResolutionCore_MixSignature( signature,
		static_cast<std::uint64_t>( config.maximumSampleAgeFrames ) );
	signature = TemporalResolutionCore_MixSignature( signature,
		config.targetMicroseconds );
	signature = TemporalResolutionCore_MixSignature( signature,
		static_cast<std::uint64_t>( config.dimensionAlignment ) );
	return signature;
}

inline bool TemporalResolutionCore_SampleIsNewer( int candidate, int previous ) {
	if ( previous < 0 ) {
		return true;
	}
	const std::uint32_t delta = static_cast<std::uint32_t>( candidate )
		- static_cast<std::uint32_t>( previous );
	return delta != 0u && delta < UINT32_C( 0x80000000 );
}

inline std::uint64_t TemporalResolutionCore_PercentOf(
		std::uint64_t value, int percent ) {
	percent = TemporalResolutionCore_ClampInt( 0, 100, percent );
	// Splitting quotient and remainder avoids overflowing value * percent.
	return ( value / 100u ) * static_cast<std::uint64_t>( percent )
		+ ( ( value % 100u ) * static_cast<std::uint64_t>( percent ) ) / 100u;
}

inline int TemporalResolutionCore_AlignedDimension( int nativeDimension,
		int scalePercent, int alignment ) {
	if ( nativeDimension <= 0 ) {
		return 0;
	}
	// The controller's ceiling is the real output extent.  Alignment is only a
	// constraint on reduced scene targets; applying it at 100% would quietly
	// turn common odd-sized windows (for example 1366 wide) into an upscale.
	if ( scalePercent >= 100 ) {
		return nativeDimension;
	}
	alignment = alignment < 1 ? 1 : alignment;
	const std::int64_t scaledNumerator =
		static_cast<std::int64_t>( nativeDimension ) * scalePercent;
	int scaled = static_cast<int>( ( scaledNumerator + 50 ) / 100 );
	scaled = scaled < 1 ? 1 : ( scaled > nativeDimension ? nativeDimension : scaled );
	if ( scaled >= alignment ) {
		scaled -= scaled % alignment;
	}
	return scaled < 1 ? 1 : scaled;
}

inline temporalResolutionOutput_t TemporalResolutionCore_Update(
		temporalResolutionState_t &state,
		const temporalResolutionConfig_t &requestedConfig,
		const temporalResolutionSample_t &sample,
		int nativeWidth, int nativeHeight, bool freezeScale, bool forceNative ) {
	temporalResolutionConfig_t config = requestedConfig;
	config.minimumScalePercent = TemporalResolutionCore_ClampInt(
		10, 100, config.minimumScalePercent );
	config.maximumScalePercent = TemporalResolutionCore_ClampInt(
		config.minimumScalePercent, 100, config.maximumScalePercent );
	config.dropStepPercent = TemporalResolutionCore_ClampInt(
		1, 50, config.dropStepPercent );
	config.raiseStepPercent = TemporalResolutionCore_ClampInt(
		1, 25, config.raiseStepPercent );
	config.raiseFrames = TemporalResolutionCore_ClampInt( 1, 1000, config.raiseFrames );
	config.raiseThresholdPercent = TemporalResolutionCore_ClampInt(
		1, 99, config.raiseThresholdPercent );
	config.missingSampleResetFrames = TemporalResolutionCore_ClampInt(
		1, 1000000, config.missingSampleResetFrames );
	config.maximumSampleAgeFrames = TemporalResolutionCore_ClampInt(
		1, 1000000, config.maximumSampleAgeFrames );
	config.dimensionAlignment = TemporalResolutionCore_ClampInt(
		1, 256, config.dimensionAlignment );

	temporalResolutionOutput_t output;
	output.decision = TEMPORAL_RESOLUTION_DECISION_WAITING_FOR_SAMPLE;
	output.sampleConsumed = false;
	output.scalePercent = 100;
	output.width = nativeWidth > 0 ? nativeWidth : 0;
	output.height = nativeHeight > 0 ? nativeHeight : 0;

	if ( !config.enabled || config.targetMicroseconds == 0 ) {
		state.initialized = false;
		state.sampleIdentityValid = false;
		state.currentScalePercent = config.maximumScalePercent;
		state.belowBudgetFrames = 0;
		state.missingSampleFrames = 0;
		output.decision = TEMPORAL_RESOLUTION_DECISION_DISABLED;
		output.scalePercent = state.currentScalePercent;
		output.width = TemporalResolutionCore_AlignedDimension(
			nativeWidth, output.scalePercent, config.dimensionAlignment );
		output.height = TemporalResolutionCore_AlignedDimension(
			nativeHeight, output.scalePercent, config.dimensionAlignment );
		return output;
	}

	if ( forceNative ) {
		output.decision = TEMPORAL_RESOLUTION_DECISION_FORCED_NATIVE;
		output.scalePercent = 100;
		return output;
	}

	if ( freezeScale && state.initialized ) {
		output.decision = TEMPORAL_RESOLUTION_DECISION_FROZEN;
		output.scalePercent = TemporalResolutionCore_ClampInt(
			config.minimumScalePercent, config.maximumScalePercent,
			state.currentScalePercent );
		output.width = TemporalResolutionCore_AlignedDimension(
			nativeWidth, output.scalePercent, config.dimensionAlignment );
		output.height = TemporalResolutionCore_AlignedDimension(
			nativeHeight, output.scalePercent, config.dimensionAlignment );
		return output;
	}

	const std::uint64_t configSignature =
		TemporalResolutionCore_ConfigSignature( config );
	const bool configurationChanged = state.initialized
		&& state.configSignature != configSignature;
	const bool dimensionsChanged = state.initialized
		&& ( state.nativeWidth != nativeWidth || state.nativeHeight != nativeHeight );
	const bool identityChanged = state.sampleIdentityValid && sample.supported
		&& ( state.backend != sample.backend || state.generation != sample.generation );

	if ( !state.initialized || configurationChanged || dimensionsChanged
			|| identityChanged ) {
		state.initialized = true;
		state.sampleIdentityValid = sample.supported;
		state.backend = sample.supported ? sample.backend : 0;
		state.generation = sample.supported ? sample.generation : 0;
		state.lastSampleFrame = sample.supported && sample.valid
			? sample.frameNumber : -1;
		state.configSignature = configSignature;
		state.nativeWidth = nativeWidth;
		state.nativeHeight = nativeHeight;
		state.currentScalePercent = config.maximumScalePercent;
		state.belowBudgetFrames = 0;
		state.missingSampleFrames = 0;
		state.discontinuityResets++;
		output.decision = TEMPORAL_RESOLUTION_DECISION_DISCONTINUITY_RESET;
	} else if ( !state.sampleIdentityValid && sample.supported ) {
		state.sampleIdentityValid = true;
		state.backend = sample.backend;
		state.generation = sample.generation;
		state.lastSampleFrame = sample.valid ? sample.frameNumber : -1;
		state.currentScalePercent = config.maximumScalePercent;
		state.belowBudgetFrames = 0;
		state.missingSampleFrames = 0;
		state.discontinuityResets++;
		output.decision = TEMPORAL_RESOLUTION_DECISION_DISCONTINUITY_RESET;
	} else if ( !sample.supported || !sample.valid
			|| sample.elapsedMicroseconds == 0
			|| sample.scalePercent < config.minimumScalePercent
			|| sample.scalePercent > config.maximumScalePercent
			|| sample.ageFrames < 0
			|| sample.ageFrames > config.maximumSampleAgeFrames
			|| !TemporalResolutionCore_SampleIsNewer(
				sample.frameNumber, state.lastSampleFrame ) ) {
		if ( sample.valid && state.sampleIdentityValid
				&& sample.frameNumber != state.lastSampleFrame ) {
			state.rejectedSamples++;
		}
		state.missingSampleFrames++;
		if ( state.missingSampleFrames >= config.missingSampleResetFrames ) {
			state.currentScalePercent = config.maximumScalePercent;
			state.belowBudgetFrames = 0;
			state.missingSampleFrames = 0;
			state.discontinuityResets++;
			output.decision = TEMPORAL_RESOLUTION_DECISION_DISCONTINUITY_RESET;
		} else {
			output.decision = TEMPORAL_RESOLUTION_DECISION_WAITING_FOR_SAMPLE;
		}
	} else {
		state.lastSampleFrame = sample.frameNumber;
		state.missingSampleFrames = 0;
		state.processedSamples++;
		output.sampleConsumed = true;

		const int previousScale = state.currentScalePercent;
		if ( sample.elapsedMicroseconds > config.targetMicroseconds ) {
			const int sampledScaleTarget = TemporalResolutionCore_ClampInt(
				config.minimumScalePercent, config.maximumScalePercent,
				sample.scalePercent - config.dropStepPercent );
			state.currentScalePercent = state.currentScalePercent < sampledScaleTarget
				? state.currentScalePercent : sampledScaleTarget;
			state.belowBudgetFrames = 0;
			if ( state.currentScalePercent < previousScale ) {
				state.droppedScaleChanges++;
				output.decision = TEMPORAL_RESOLUTION_DECISION_DROP;
			} else {
				output.decision = TEMPORAL_RESOLUTION_DECISION_HOLD;
			}
		} else {
			const std::uint64_t raiseThreshold = TemporalResolutionCore_PercentOf(
				config.targetMicroseconds, config.raiseThresholdPercent );
			if ( sample.elapsedMicroseconds < raiseThreshold
					&& sample.scalePercent == state.currentScalePercent ) {
				state.belowBudgetFrames++;
				if ( state.belowBudgetFrames >= config.raiseFrames ) {
					state.belowBudgetFrames = 0;
					state.currentScalePercent = TemporalResolutionCore_ClampInt(
						config.minimumScalePercent, config.maximumScalePercent,
						state.currentScalePercent + config.raiseStepPercent );
					if ( state.currentScalePercent > previousScale ) {
						state.raisedScaleChanges++;
						output.decision = TEMPORAL_RESOLUTION_DECISION_RAISE;
					} else {
						output.decision = TEMPORAL_RESOLUTION_DECISION_HOLD;
					}
				} else {
					output.decision = TEMPORAL_RESOLUTION_DECISION_HOLD;
				}
			} else {
				state.belowBudgetFrames = 0;
				output.decision = TEMPORAL_RESOLUTION_DECISION_HOLD;
			}
		}
	}

	state.currentScalePercent = TemporalResolutionCore_ClampInt(
		config.minimumScalePercent, config.maximumScalePercent,
		state.currentScalePercent );
	output.scalePercent = state.currentScalePercent;
	output.width = TemporalResolutionCore_AlignedDimension(
		nativeWidth, output.scalePercent, config.dimensionAlignment );
	output.height = TemporalResolutionCore_AlignedDimension(
		nativeHeight, output.scalePercent, config.dimensionAlignment );
	return output;
}

// Texture LOD bias for a temporally upscaled scene (REN-04).  A scene drawn at
// a fraction of the output resolution selects its texture mips for that
// smaller target, so the upscaled picture carries about a mip less detail per
// halving.  Temporal upscaling accumulates jittered samples and can resolve
// the detail a sharper mip adds, so material textures are biased by
// strength * log2( scale ) while it presents.  Samplers hold the bias in
// quarter-mip steps; the step moves only once the exact value has left the
// applied step by three quarters of a step, so a dynamic resolution that
// hovers between two scales does not rebuild samplers every few frames.
const int TEMPORAL_LOD_BIAS_STEPS_PER_MIP = 4;
const int TEMPORAL_LOD_BIAS_MIN_STEP = -2 * TEMPORAL_LOD_BIAS_STEPS_PER_MIP;

inline float TemporalResolutionCore_UpscaleLodBias( int scalePercent,
		bool temporalUpscale, float strength ) {
	if ( !temporalUpscale || scalePercent <= 0 || scalePercent >= 100
			|| !( strength > 0.0f ) ) {
		return 0.0f;
	}
	return strength * std::log2( static_cast<float>( scalePercent ) / 100.0f );
}

inline int TemporalResolutionCore_LodBiasStep( float exactBias, int appliedStep ) {
	if ( !( exactBias < 0.0f ) ) {
		// native resolution, or no temporal resolve: exactly unbiased
		return 0;
	}
	const float exactSteps = exactBias * TEMPORAL_LOD_BIAS_STEPS_PER_MIP;
	appliedStep = TemporalResolutionCore_ClampInt(
		TEMPORAL_LOD_BIAS_MIN_STEP, 0, appliedStep );
	if ( std::fabs( exactSteps - static_cast<float>( appliedStep ) ) < 0.75f ) {
		return appliedStep;
	}
	return TemporalResolutionCore_ClampInt( TEMPORAL_LOD_BIAS_MIN_STEP, 0,
		static_cast<int>( std::floor( exactSteps + 0.5f ) ) );
}

#endif /* !__TEMPORAL_PRESENTATION_CORE_H__ */
