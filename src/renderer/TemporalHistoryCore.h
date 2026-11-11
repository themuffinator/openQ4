// Copyright (C) 2026 DarkMatter Productions
//

#ifndef __TEMPORAL_HISTORY_CORE_H__
#define __TEMPORAL_HISTORY_CORE_H__

#include <cstdint>
#include <cmath>

// Dependency-free temporal-history policy shared by the renderer front end,
// both graphics backends, and the native regression test. The core owns only
// deterministic identities and decisions; image allocation and synchronization
// remain backend responsibilities.

typedef enum temporalHistoryResetReason_e {
	TEMPORAL_HISTORY_RESET_NONE = 0,
	TEMPORAL_HISTORY_RESET_FIRST_SAMPLE,
	TEMPORAL_HISTORY_RESET_GENERATION,
	TEMPORAL_HISTORY_RESET_VIEW_IDENTITY,
	TEMPORAL_HISTORY_RESET_OUTPUT_EXTENT,
	TEMPORAL_HISTORY_RESET_SCENE_EXTENT,
	TEMPORAL_HISTORY_RESET_TIME_DISCONTINUITY,
	TEMPORAL_HISTORY_RESET_CAMERA_TRANSLATION,
	TEMPORAL_HISTORY_RESET_CAMERA_ROTATION,
	TEMPORAL_HISTORY_RESET_PROJECTION,
	TEMPORAL_HISTORY_RESET_EXPLICIT_CUT,
	TEMPORAL_HISTORY_RESET_CAPTURE,
	TEMPORAL_HISTORY_RESET_RESOURCE_LOSS
} temporalHistoryResetReason_t;

typedef enum temporalMotionDomain_e {
	TEMPORAL_MOTION_DOMAIN_STATIC_WORLD = 0,
	TEMPORAL_MOTION_DOMAIN_RIGID,
	TEMPORAL_MOTION_DOMAIN_SKINNED,
	TEMPORAL_MOTION_DOMAIN_PARTICLE,
	TEMPORAL_MOTION_DOMAIN_DEFORM,
	TEMPORAL_MOTION_DOMAIN_SUBVIEW,
	TEMPORAL_MOTION_DOMAIN_IN_WORLD_GUI,
	TEMPORAL_MOTION_DOMAIN_VIEW_MODEL,
	TEMPORAL_MOTION_DOMAIN_COUNT
} temporalMotionDomain_t;

typedef enum temporalMotionSource_e {
	TEMPORAL_MOTION_SOURCE_ZERO = 0,
	TEMPORAL_MOTION_SOURCE_CAMERA_DEPTH,
	TEMPORAL_MOTION_SOURCE_RIGID_TRANSFORM,
	TEMPORAL_MOTION_SOURCE_SKINNED_PALETTE,
	TEMPORAL_MOTION_SOURCE_DEFORM_VERTEX,
	TEMPORAL_MOTION_SOURCE_INDEPENDENT_VIEW
} temporalMotionSource_t;

enum temporalMotionOwnershipFlags_t {
	TEMPORAL_MOTION_OWNERSHIP_NONE = 0,
	TEMPORAL_MOTION_OWNERSHIP_HAS_PREVIOUS_TRANSFORM = 1 << 0,
	TEMPORAL_MOTION_OWNERSHIP_REACTIVE = 1 << 1,
	TEMPORAL_MOTION_OWNERSHIP_DISOCCLUSION_TEST = 1 << 2,
	TEMPORAL_MOTION_OWNERSHIP_SEPARATE_HISTORY = 1 << 3,
	TEMPORAL_MOTION_OWNERSHIP_DEPTH_HACK = 1 << 4
};

typedef struct temporalJitterSample_s {
	float x;
	float y;
} temporalJitterSample_t;

typedef struct temporalCameraState_s {
	bool valid;
	bool projectionValid;
	std::uint64_t viewIdentity;
	std::uint32_t historyGeneration;
	int frameNumber;
	int renderTimeMsec;
	int outputWidth;
	int outputHeight;
	int sceneWidth;
	int sceneHeight;
	float viewOrigin[3];
	float viewAxis[9];
	float fovX;
	float fovY;
	float projectInfo[4];
	float depthProjection[2];
	float jitterPixels[2];
} temporalCameraState_t;

typedef struct temporalCameraPolicy_s {
	int maximumFrameGap;
	int maximumTimeGapMsec;
	float maximumTranslation;
	float minimumAxisDot;
	float maximumFovDelta;
} temporalCameraPolicy_t;

typedef struct temporalMotionInput_s {
	bool hasEntity;
	bool hasPreviousTransform;
	bool skinned;
	bool hasPreviousSkinningPalette;
	bool particle;
	bool deform;
	bool hasPreviousDeformedVertices;
	bool subview;
	bool inWorldGui;
	bool viewModel;
	bool translucent;
} temporalMotionInput_t;

typedef struct temporalMotionOwnership_s {
	temporalMotionDomain_t domain;
	temporalMotionSource_t source;
	unsigned int flags;
} temporalMotionOwnership_t;

// Two conservative screen-space regions fit in the Vulkan temporal uniform
// slice while avoiding the pathological full-screen rejection caused by a
// single union around unrelated moving objects. Coordinates are normalized
// renderer scissor coordinates (bottom-left origin, inclusive-exclusive).
const int TEMPORAL_MAX_REACTIVE_REGIONS = 2;

typedef struct temporalReactiveRegion_s {
	float x1;
	float y1;
	float x2;
	float y2;
	unsigned int domainMask;
} temporalReactiveRegion_t;

typedef struct temporalViewMotionPolicy_s {
	unsigned int presentDomainMask;
	unsigned int reactiveDomainMask;
	unsigned int exactMotionDomainMask;
	temporalReactiveRegion_t reactiveRegions[TEMPORAL_MAX_REACTIVE_REGIONS];
	int reactiveRegionCount;
	bool reactiveRegionsMerged;
} temporalViewMotionPolicy_t;

inline unsigned int TemporalHistoryCore_MotionDomainBit(
		temporalMotionDomain_t domain ) {
	return domain >= TEMPORAL_MOTION_DOMAIN_STATIC_WORLD
		&& domain < TEMPORAL_MOTION_DOMAIN_COUNT
		? 1u << static_cast<unsigned int>( domain ) : 0u;
}

inline temporalViewMotionPolicy_t TemporalHistoryCore_BeginViewMotionPolicy(
		void ) {
	temporalViewMotionPolicy_t policy = {};
	return policy;
}

inline float TemporalHistoryCore_ClampUnit( float value ) {
	return value < 0.0f ? 0.0f : ( value > 1.0f ? 1.0f : value );
}

inline float TemporalHistoryCore_ReactiveRegionArea(
		const temporalReactiveRegion_t &region ) {
	const float width = region.x2 > region.x1 ? region.x2 - region.x1 : 0.0f;
	const float height = region.y2 > region.y1 ? region.y2 - region.y1 : 0.0f;
	return width * height;
}

inline temporalReactiveRegion_t TemporalHistoryCore_UnionReactiveRegions(
		const temporalReactiveRegion_t &a,
		const temporalReactiveRegion_t &b ) {
	temporalReactiveRegion_t result;
	result.x1 = a.x1 < b.x1 ? a.x1 : b.x1;
	result.y1 = a.y1 < b.y1 ? a.y1 : b.y1;
	result.x2 = a.x2 > b.x2 ? a.x2 : b.x2;
	result.y2 = a.y2 > b.y2 ? a.y2 : b.y2;
	result.domainMask = a.domainMask | b.domainMask;
	return result;
}

inline bool TemporalHistoryCore_ReactiveRegionsTouch(
		const temporalReactiveRegion_t &a,
		const temporalReactiveRegion_t &b ) {
	return a.x1 <= b.x2 && b.x1 <= a.x2
		&& a.y1 <= b.y2 && b.y1 <= a.y2;
}

inline void TemporalHistoryCore_AddReactiveRegion(
		temporalViewMotionPolicy_t &policy, unsigned int domainMask,
		float x1, float y1, float x2, float y2 ) {
	temporalReactiveRegion_t incoming;
	incoming.x1 = TemporalHistoryCore_ClampUnit( x1 );
	incoming.y1 = TemporalHistoryCore_ClampUnit( y1 );
	incoming.x2 = TemporalHistoryCore_ClampUnit( x2 );
	incoming.y2 = TemporalHistoryCore_ClampUnit( y2 );
	incoming.domainMask = domainMask;
	if ( incoming.x2 <= incoming.x1 || incoming.y2 <= incoming.y1 ) {
		// Missing or degenerate packet scissors must fail safe. A full-screen
		// region is expensive for one frame, but it cannot retain stale history.
		incoming.x1 = 0.0f;
		incoming.y1 = 0.0f;
		incoming.x2 = 1.0f;
		incoming.y2 = 1.0f;
	}

	for ( int i = 0; i < policy.reactiveRegionCount; ++i ) {
		if ( !TemporalHistoryCore_ReactiveRegionsTouch(
				policy.reactiveRegions[i], incoming ) ) {
			continue;
		}
		policy.reactiveRegions[i] = TemporalHistoryCore_UnionReactiveRegions(
			policy.reactiveRegions[i], incoming );
		// The expanded region may now connect the two retained islands.
		if ( policy.reactiveRegionCount == 2
				&& TemporalHistoryCore_ReactiveRegionsTouch(
					policy.reactiveRegions[0], policy.reactiveRegions[1] ) ) {
			policy.reactiveRegions[0] = TemporalHistoryCore_UnionReactiveRegions(
				policy.reactiveRegions[0], policy.reactiveRegions[1] );
			policy.reactiveRegionCount = 1;
			policy.reactiveRegionsMerged = true;
		}
		return;
	}

	if ( policy.reactiveRegionCount < TEMPORAL_MAX_REACTIVE_REGIONS ) {
		policy.reactiveRegions[policy.reactiveRegionCount++] = incoming;
		return;
	}

	// Retain two conservative islands. Of the three possible pairings, merge
	// the pair with the least additional covered area and keep the third island.
	const temporalReactiveRegion_t merge0 =
		TemporalHistoryCore_UnionReactiveRegions( policy.reactiveRegions[0], incoming );
	const temporalReactiveRegion_t merge1 =
		TemporalHistoryCore_UnionReactiveRegions( policy.reactiveRegions[1], incoming );
	const temporalReactiveRegion_t mergeExisting =
		TemporalHistoryCore_UnionReactiveRegions(
			policy.reactiveRegions[0], policy.reactiveRegions[1] );
	const float cost0 = TemporalHistoryCore_ReactiveRegionArea( merge0 )
		- TemporalHistoryCore_ReactiveRegionArea( policy.reactiveRegions[0] )
		- TemporalHistoryCore_ReactiveRegionArea( incoming );
	const float cost1 = TemporalHistoryCore_ReactiveRegionArea( merge1 )
		- TemporalHistoryCore_ReactiveRegionArea( policy.reactiveRegions[1] )
		- TemporalHistoryCore_ReactiveRegionArea( incoming );
	const float costExisting = TemporalHistoryCore_ReactiveRegionArea( mergeExisting )
		- TemporalHistoryCore_ReactiveRegionArea( policy.reactiveRegions[0] )
		- TemporalHistoryCore_ReactiveRegionArea( policy.reactiveRegions[1] );
	if ( costExisting <= cost0 && costExisting <= cost1 ) {
		policy.reactiveRegions[0] = mergeExisting;
		policy.reactiveRegions[1] = incoming;
	} else if ( cost0 <= cost1 ) {
		policy.reactiveRegions[0] = merge0;
	} else {
		policy.reactiveRegions[1] = merge1;
	}
	policy.reactiveRegionsMerged = true;
}

inline bool TemporalHistoryCore_MotionSourceHasPreviousGeometry(
		temporalMotionSource_t source ) {
	return source == TEMPORAL_MOTION_SOURCE_RIGID_TRANSFORM
		|| source == TEMPORAL_MOTION_SOURCE_SKINNED_PALETTE
		|| source == TEMPORAL_MOTION_SOURCE_DEFORM_VERTEX;
}

inline void TemporalHistoryCore_AddMotionOwnership(
		temporalViewMotionPolicy_t &policy,
		const temporalMotionOwnership_t &ownership,
		unsigned int backendExactMotionDomainMask,
		float x1, float y1, float x2, float y2 ) {
	const unsigned int domainBit =
		TemporalHistoryCore_MotionDomainBit( ownership.domain );
	if ( domainBit == 0u ) {
		return;
	}
	policy.presentDomainMask |= domainBit;
	const bool exactMotion = ( backendExactMotionDomainMask & domainBit ) != 0u
		&& TemporalHistoryCore_MotionSourceHasPreviousGeometry( ownership.source );
	if ( exactMotion ) {
		policy.exactMotionDomainMask |= domainBit;
	}
	const bool explicitlyReactive = ( ownership.flags
		& ( TEMPORAL_MOTION_OWNERSHIP_REACTIVE
			| TEMPORAL_MOTION_OWNERSHIP_SEPARATE_HISTORY ) ) != 0u;
	const bool unsupportedObjectMotion =
		ownership.domain != TEMPORAL_MOTION_DOMAIN_STATIC_WORLD && !exactMotion;
	if ( !explicitlyReactive && !unsupportedObjectMotion ) {
		return;
	}
	policy.reactiveDomainMask |= domainBit;
	TemporalHistoryCore_AddReactiveRegion( policy, domainBit,
		x1, y1, x2, y2 );
}

inline std::uint64_t TemporalHistoryCore_MixIdentity(
		std::uint64_t hash, std::uint64_t value ) {
	// FNV-1a over caller-normalized fields. This is an in-process identity,
	// not a security boundary or serialized asset hash.
	for ( int byteIndex = 0; byteIndex < 8; ++byteIndex ) {
		hash ^= ( value >> ( byteIndex * 8 ) ) & UINT64_C( 0xff );
		hash *= UINT64_C( 1099511628211 );
	}
	return hash;
}

inline std::uint64_t TemporalHistoryCore_BeginIdentity( void ) {
	return UINT64_C( 14695981039346656037 );
}

inline float TemporalHistoryCore_RadicalInverse(
		std::uint32_t index, std::uint32_t base ) {
	const float inverseBase = 1.0f / static_cast<float>( base );
	float fraction = inverseBase;
	float result = 0.0f;
	while ( index != 0u ) {
		result += static_cast<float>( index % base ) * fraction;
		index /= base;
		fraction *= inverseBase;
	}
	return result;
}

inline temporalJitterSample_t TemporalHistoryCore_Jitter(
		std::uint32_t frameIndex ) {
	// An eight-sample Halton(2,3) cycle is deterministic across GL/Vulkan and
	// bounded to a half-pixel footprint. Index zero intentionally starts at
	// Halton sample one rather than the unhelpful (0,0) sequence origin.
	const std::uint32_t sequenceIndex = ( frameIndex & 7u ) + 1u;
	temporalJitterSample_t sample;
	sample.x = TemporalHistoryCore_RadicalInverse( sequenceIndex, 2u ) - 0.5f;
	sample.y = TemporalHistoryCore_RadicalInverse( sequenceIndex, 3u ) - 0.5f;
	return sample;
}

// History lives on the unjittered output grid, so nothing that reprojects into
// it may see either frame's sub-pixel jitter. The current frame is drawn (and
// its depth reconstructed) with its jittered projection; giving the previous
// camera its own jitter-free offsets plus the jitter applied to this frame
// makes every reprojection - camera/depth, rigid, posed, view weapon - return
// (current position + motion) exactly: a still camera reprojects each pixel
// onto itself. Offsets are projectionMatrix[8]/[9].
inline void TemporalHistoryCore_AlignPreviousProjectionOffsets(
		const float currentApplied[2], const float currentNeutral[2],
		const float previousNeutral[2], float out[2] ) {
	for ( int i = 0; i < 2; ++i ) {
		out[i] = previousNeutral[i] + ( currentApplied[i] - currentNeutral[i] );
	}
}

inline float TemporalHistoryCore_Dot3( const float *a, const float *b ) {
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline float TemporalHistoryCore_DistanceSquared3(
		const float *a, const float *b ) {
	const float x = a[0] - b[0];
	const float y = a[1] - b[1];
	const float z = a[2] - b[2];
	return x * x + y * y + z * z;
}

inline temporalCameraPolicy_t TemporalHistoryCore_DefaultCameraPolicy( void ) {
	temporalCameraPolicy_t policy;
	policy.maximumFrameGap = 2;
	policy.maximumTimeGapMsec = 100;
	policy.maximumTranslation = 512.0f;
	// About 20 degrees. Normal continuous camera motion remains reprojectable;
	// teleports and hard authored cuts reject history immediately.
	policy.minimumAxisDot = 0.93969262f;
	policy.maximumFovDelta = 0.25f;
	return policy;
}

inline temporalHistoryResetReason_t TemporalHistoryCore_ValidateCameraHistory(
		const temporalCameraState_t &current,
		const temporalCameraState_t &previous,
		const temporalCameraPolicy_t &policy ) {
	if ( !previous.valid ) {
		return TEMPORAL_HISTORY_RESET_FIRST_SAMPLE;
	}
	if ( current.historyGeneration != previous.historyGeneration ) {
		return TEMPORAL_HISTORY_RESET_GENERATION;
	}
	if ( current.viewIdentity == 0 || current.viewIdentity != previous.viewIdentity ) {
		return TEMPORAL_HISTORY_RESET_VIEW_IDENTITY;
	}
	if ( current.outputWidth != previous.outputWidth
			|| current.outputHeight != previous.outputHeight ) {
		return TEMPORAL_HISTORY_RESET_OUTPUT_EXTENT;
	}
	// History images live at the native output extent. A dynamic-resolution
	// change only alters the current input sampling footprint, so rejecting the
	// prior native history here would defeat temporal upscaling exactly when the
	// controller changes scale.
	const std::uint32_t frameDelta = static_cast<std::uint32_t>( current.frameNumber )
		- static_cast<std::uint32_t>( previous.frameNumber );
	if ( frameDelta == 0u || frameDelta > static_cast<std::uint32_t>( policy.maximumFrameGap )
			|| current.renderTimeMsec < previous.renderTimeMsec
			|| current.renderTimeMsec - previous.renderTimeMsec > policy.maximumTimeGapMsec ) {
		return TEMPORAL_HISTORY_RESET_TIME_DISCONTINUITY;
	}
	if ( TemporalHistoryCore_DistanceSquared3( current.viewOrigin,
			previous.viewOrigin ) > policy.maximumTranslation * policy.maximumTranslation ) {
		return TEMPORAL_HISTORY_RESET_CAMERA_TRANSLATION;
	}
	for ( int axisIndex = 0; axisIndex < 3; ++axisIndex ) {
		if ( TemporalHistoryCore_Dot3( current.viewAxis + axisIndex * 3,
				previous.viewAxis + axisIndex * 3 ) < policy.minimumAxisDot ) {
			return TEMPORAL_HISTORY_RESET_CAMERA_ROTATION;
		}
	}
	if ( std::fabs( current.fovX - previous.fovX ) > policy.maximumFovDelta
			|| std::fabs( current.fovY - previous.fovY ) > policy.maximumFovDelta ) {
		return TEMPORAL_HISTORY_RESET_PROJECTION;
	}
	return TEMPORAL_HISTORY_RESET_NONE;
}

inline temporalMotionOwnership_t TemporalHistoryCore_ClassifyMotion(
		const temporalMotionInput_t &input ) {
	temporalMotionOwnership_t ownership;
	ownership.domain = TEMPORAL_MOTION_DOMAIN_STATIC_WORLD;
	ownership.source = TEMPORAL_MOTION_SOURCE_CAMERA_DEPTH;
	ownership.flags = TEMPORAL_MOTION_OWNERSHIP_DISOCCLUSION_TEST;

	if ( input.subview ) {
		ownership.domain = TEMPORAL_MOTION_DOMAIN_SUBVIEW;
		ownership.source = TEMPORAL_MOTION_SOURCE_INDEPENDENT_VIEW;
		ownership.flags |= TEMPORAL_MOTION_OWNERSHIP_SEPARATE_HISTORY
			| TEMPORAL_MOTION_OWNERSHIP_REACTIVE;
	} else if ( input.inWorldGui ) {
		ownership.domain = TEMPORAL_MOTION_DOMAIN_IN_WORLD_GUI;
		ownership.source = input.hasPreviousTransform
			? TEMPORAL_MOTION_SOURCE_RIGID_TRANSFORM
			: TEMPORAL_MOTION_SOURCE_CAMERA_DEPTH;
		ownership.flags |= TEMPORAL_MOTION_OWNERSHIP_REACTIVE;
	} else if ( input.viewModel ) {
		ownership.domain = TEMPORAL_MOTION_DOMAIN_VIEW_MODEL;
		ownership.source = input.hasPreviousTransform
			? TEMPORAL_MOTION_SOURCE_RIGID_TRANSFORM
			: TEMPORAL_MOTION_SOURCE_CAMERA_DEPTH;
		ownership.flags |= TEMPORAL_MOTION_OWNERSHIP_REACTIVE
			| TEMPORAL_MOTION_OWNERSHIP_DEPTH_HACK;
	} else if ( input.particle ) {
		ownership.domain = TEMPORAL_MOTION_DOMAIN_PARTICLE;
		ownership.source = TEMPORAL_MOTION_SOURCE_CAMERA_DEPTH;
		ownership.flags |= TEMPORAL_MOTION_OWNERSHIP_REACTIVE;
	} else if ( input.deform ) {
		ownership.domain = TEMPORAL_MOTION_DOMAIN_DEFORM;
		ownership.source = input.hasPreviousDeformedVertices
			? TEMPORAL_MOTION_SOURCE_DEFORM_VERTEX
			: TEMPORAL_MOTION_SOURCE_CAMERA_DEPTH;
		if ( !input.hasPreviousDeformedVertices ) {
			ownership.flags |= TEMPORAL_MOTION_OWNERSHIP_REACTIVE;
		}
	} else if ( input.skinned ) {
		ownership.domain = TEMPORAL_MOTION_DOMAIN_SKINNED;
		ownership.source = input.hasPreviousSkinningPalette
			? TEMPORAL_MOTION_SOURCE_SKINNED_PALETTE
			: TEMPORAL_MOTION_SOURCE_CAMERA_DEPTH;
		if ( !input.hasPreviousSkinningPalette ) {
			ownership.flags |= TEMPORAL_MOTION_OWNERSHIP_REACTIVE;
		}
	} else if ( input.hasEntity ) {
		ownership.domain = TEMPORAL_MOTION_DOMAIN_RIGID;
		ownership.source = input.hasPreviousTransform
			? TEMPORAL_MOTION_SOURCE_RIGID_TRANSFORM
			: TEMPORAL_MOTION_SOURCE_CAMERA_DEPTH;
	}

	if ( input.hasPreviousTransform ) {
		ownership.flags |= TEMPORAL_MOTION_OWNERSHIP_HAS_PREVIOUS_TRANSFORM;
	}
	if ( input.translucent ) {
		ownership.flags |= TEMPORAL_MOTION_OWNERSHIP_REACTIVE;
	}
	return ownership;
}

#endif /* !__TEMPORAL_HISTORY_CORE_H__ */
