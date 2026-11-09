// Copyright (C) 2026 DarkMatter Productions
//

#include "tr_local.h"
#include "GpuSkinning.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>

namespace {
	std::recursive_mutex rg_gpuSkinningStatsMutex;
	gpuSkinningStats_t rg_gpuSkinningStats;
	uint32 rg_gpuSkinningGeneration = 1;
	bool rg_gpuSkinningInitialized = false;

	// the caller holds rg_gpuSkinningStatsMutex
	static ID_INLINE void R_GpuSkinning_AddLocked( uint64 &value, uint64 amount ) {
		const uint64 maxValue = ~static_cast<uint64>( 0 );
		value = amount > maxValue - value ? maxValue : value + amount;
	}

	static ID_INLINE void R_GpuSkinning_SaturatingAdd( uint64 &value, uint64 amount ) {
		std::lock_guard<std::recursive_mutex> lock( rg_gpuSkinningStatsMutex );
		R_GpuSkinning_AddLocked( value, amount );
	}

	static ID_INLINE idVec3 R_GpuSkinning_TransformVector( const float *matrix,
		const idVec3 &vector, float w ) {
		return idVec3(
			matrix[0] * vector.x + matrix[1] * vector.y + matrix[2] * vector.z + matrix[3] * w,
			matrix[4] * vector.x + matrix[5] * vector.y + matrix[6] * vector.z + matrix[7] * w,
			matrix[8] * vector.x + matrix[9] * vector.y + matrix[10] * vector.z + matrix[11] * w );
	}

	static ID_INLINE bool R_GpuSkinning_BasisClose( const idVec3 &a, const idVec3 &b,
		float epsilon ) {
		return ( a - b ).LengthSqr() <= epsilon * epsilon;
	}
}

const char *R_GpuSkinning_FallbackName( gpuSkinningFallbackReason_t reason ) {
	static const char *names[ GPU_SKINNING_FALLBACK_COUNT ] = {
		"none", "disabled", "backendUnavailable", "missingBindPose",
		"missingSkinVertices", "vertexCount", "residualWeights", "jointCount",
		"jointIndex", "malformedWeights", "skinScale", "unsupportedMaterial",
		"unsupportedPass", "stencilVolume", "decalOverlay", "paletteAllocation",
		"stalePalette"
	};
	return reason >= GPU_SKINNING_FALLBACK_NONE && reason < GPU_SKINNING_FALLBACK_COUNT
		? names[ reason ] : "invalid";
}

uint32 R_GpuSkinning_ContractGeneration( void ) {
	return rg_gpuSkinningGeneration;
}

uint64 R_GpuSkinning_ReadMicroseconds( void ) {
	const std::chrono::steady_clock::duration elapsed =
		std::chrono::steady_clock::now().time_since_epoch();
	return static_cast<uint64>( std::chrono::duration_cast<std::chrono::microseconds>(
		elapsed ).count() );
}

bool R_GpuSkinning_PackVertexExact( const gpuSkinningInfluence_t *influences,
	int numInfluences, int numJoints, bool allowSignedWeights,
	gpuSkinningVertex_t &packed, gpuSkinningPackResult_t &result ) {
	memset( &packed, 0, sizeof( packed ) );
	memset( &result, 0, sizeof( result ) );
	result.reason = GPU_SKINNING_FALLBACK_NONE;

	R_GpuSkinning_SaturatingAdd( rg_gpuSkinningStats.packedVertices, 1 );
	if ( influences == NULL || numInfluences <= 0 ) {
		result.reason = GPU_SKINNING_FALLBACK_MALFORMED_WEIGHTS;
		return false;
	}
	if ( numJoints <= 0 || numJoints > GPU_SKINNING_MAX_JOINTS ) {
		result.reason = GPU_SKINNING_FALLBACK_JOINT_COUNT;
		return false;
	}

	int packedIndex = 0;
	for ( int i = 0; i < numInfluences; ++i ) {
		const float weight = influences[i].weight;
		if ( !std::isfinite( weight ) || ( !allowSignedWeights && weight < 0.0f ) ) {
			result.reason = GPU_SKINNING_FALLBACK_MALFORMED_WEIGHTS;
			return false;
		}
		const float absoluteWeight = idMath::Fabs( weight );
		result.totalAbsoluteWeight += absoluteWeight;
		if ( absoluteWeight == 0.0f ) {
			continue;
		}
		if ( influences[i].jointIndex >= static_cast<uint32>( numJoints ) ) {
			result.reason = GPU_SKINNING_FALLBACK_JOINT_INDEX;
			return false;
		}

		++result.meaningfulInfluences;
		result.maxJointIndex = Max( result.maxJointIndex, influences[i].jointIndex );
		if ( packedIndex < GPU_SKINNING_INFLUENCES ) {
			packed.jointIndices[ packedIndex ] = influences[i].jointIndex;
			packed.jointWeights[ packedIndex ] = weight;
			++packedIndex;
		} else {
			result.residualAbsoluteWeight += absoluteWeight;
		}
	}

	if ( result.totalAbsoluteWeight <= 0.0f ) {
		result.reason = GPU_SKINNING_FALLBACK_MALFORMED_WEIGHTS;
		return false;
	}
	if ( result.meaningfulInfluences > GPU_SKINNING_INFLUENCES ) {
		result.reason = GPU_SKINNING_FALLBACK_RESIDUAL_WEIGHTS;
		return false;
	}

	R_GpuSkinning_SaturatingAdd( rg_gpuSkinningStats.exactVertices, 1 );
	return true;
}

bool R_GpuSkinning_DeformVertexCPU( const idDrawVert &bindPose,
	const gpuSkinningVertex_t &skinVertex, const gpuSkinningJointPalette_t &palette,
	idDrawVert &deformed ) {
	if ( palette.matrices == NULL || palette.numJoints <= 0
		|| palette.matrixStrideFloats != GPU_SKINNING_JOINT_FLOATS ) {
		return false;
	}

	deformed = bindPose;
	idVec3 position( 0.0f, 0.0f, 0.0f );
	idVec3 normal( 0.0f, 0.0f, 0.0f );
	idVec3 tangent0( 0.0f, 0.0f, 0.0f );
	idVec3 tangent1( 0.0f, 0.0f, 0.0f );
	float totalAbsoluteWeight = 0.0f;

	for ( int i = 0; i < GPU_SKINNING_INFLUENCES; ++i ) {
		const float weight = skinVertex.jointWeights[i];
		if ( weight == 0.0f ) {
			continue;
		}
		if ( !std::isfinite( weight ) || skinVertex.jointIndices[i] >= static_cast<uint32>( palette.numJoints ) ) {
			return false;
		}
		const float *matrix = palette.matrices
			+ skinVertex.jointIndices[i] * palette.matrixStrideFloats;
		for ( int valueIndex = 0; valueIndex < GPU_SKINNING_JOINT_FLOATS; ++valueIndex ) {
			if ( !std::isfinite( matrix[valueIndex] ) ) {
				return false;
			}
		}
		position += weight * R_GpuSkinning_TransformVector( matrix, bindPose.xyz, 1.0f );
		normal += weight * R_GpuSkinning_TransformVector( matrix, bindPose.normal, 0.0f );
		tangent0 += weight * R_GpuSkinning_TransformVector( matrix, bindPose.tangents[0], 0.0f );
		tangent1 += weight * R_GpuSkinning_TransformVector( matrix, bindPose.tangents[1], 0.0f );
		totalAbsoluteWeight += idMath::Fabs( weight );
	}
	if ( totalAbsoluteWeight <= 0.0f ) {
		return false;
	}

	deformed.xyz = position;
	deformed.normal = normal;
	deformed.tangents[0] = tangent0;
	deformed.tangents[1] = tangent1;
	return true;
}

bool R_GpuSkinning_CompareVertexCPU( const idDrawVert &expected,
	const idDrawVert &actual, float positionEpsilon, float basisEpsilon ) {
	return positionEpsilon >= 0.0f && basisEpsilon >= 0.0f
		&& R_GpuSkinning_BasisClose( expected.xyz, actual.xyz, positionEpsilon )
		&& R_GpuSkinning_BasisClose( expected.normal, actual.normal, basisEpsilon )
		&& R_GpuSkinning_BasisClose( expected.tangents[0], actual.tangents[0], basisEpsilon )
		&& R_GpuSkinning_BasisClose( expected.tangents[1], actual.tangents[1], basisEpsilon );
}

void R_GpuSkinning_ClearSurfaceContract( srfTriangles_s *tri,
	gpuSkinningFallbackReason_t reason ) {
	if ( tri == NULL ) {
		return;
	}
	R_ClearStaticGpuSkinningJointPalette( tri );
	tri->gpuSkinningFallbackReason = reason;
}

bool R_GpuSkinning_AttachSurfaceContract( srfTriangles_s *tri,
	const idDrawVert *bindPoseVerts, const gpuSkinningVertex_t *skinVerts,
	int numVerts, const float *jointMatrices, int numJoints,
	int sourceMatrixStrideFloats, bool signedWeights,
	gpuSkinningFallbackReason_t sourceFallback, int sidecarJointBound ) {
	if ( tri == NULL ) {
		return false;
	}
	R_ClearStaticGpuSkinningJointPalette( tri );
	if ( sourceFallback != GPU_SKINNING_FALLBACK_NONE ) {
		R_GpuSkinning_ClearSurfaceContract( tri, sourceFallback );
		return false;
	}
	if ( bindPoseVerts == NULL ) {
		R_GpuSkinning_ClearSurfaceContract( tri, GPU_SKINNING_FALLBACK_MISSING_BIND_POSE );
		return false;
	}
	if ( skinVerts == NULL ) {
		R_GpuSkinning_ClearSurfaceContract( tri, GPU_SKINNING_FALLBACK_MISSING_SKIN_VERTICES );
		return false;
	}
	if ( numVerts <= 0 || tri->numVerts != numVerts ) {
		R_GpuSkinning_ClearSurfaceContract( tri, GPU_SKINNING_FALLBACK_VERTEX_COUNT );
		return false;
	}
	if ( jointMatrices == NULL || numJoints <= 0 || numJoints > GPU_SKINNING_MAX_JOINTS ) {
		R_GpuSkinning_ClearSurfaceContract( tri, GPU_SKINNING_FALLBACK_JOINT_COUNT );
		return false;
	}
	// A prevalidated sidecar names the highest joint any of its influences
	// uses; a palette that cannot cover it fails here once instead of on
	// every per-vertex validation of every use.
	if ( sidecarJointBound > numJoints ) {
		R_GpuSkinning_ClearSurfaceContract( tri, GPU_SKINNING_FALLBACK_JOINT_INDEX );
		return false;
	}
	if ( sourceMatrixStrideFloats < GPU_SKINNING_JOINT_FLOATS
		|| !R_AllocStaticGpuSkinningJointPalette( tri, numJoints ) ) {
		R_GpuSkinning_ClearSurfaceContract( tri, GPU_SKINNING_FALLBACK_PALETTE_ALLOCATION );
		return false;
	}

	for ( int jointIndex = 0; jointIndex < numJoints; ++jointIndex ) {
		const float *source = jointMatrices + jointIndex * sourceMatrixStrideFloats;
		float *dest = tri->gpuSkinningJointPalette + jointIndex * GPU_SKINNING_JOINT_FLOATS;
		for ( int valueIndex = 0; valueIndex < GPU_SKINNING_JOINT_FLOATS; ++valueIndex ) {
			if ( !std::isfinite( source[valueIndex] ) ) {
				R_GpuSkinning_ClearSurfaceContract( tri, GPU_SKINNING_FALLBACK_MALFORMED_WEIGHTS );
				return false;
			}
			dest[valueIndex] = source[valueIndex];
		}
	}

	tri->gpuSkinningBindPoseVerts = bindPoseVerts;
	tri->gpuSkinningVerts = skinVerts;
	tri->numGpuSkinningVerts = numVerts;
	tri->gpuSkinningPaletteGeneration = rg_gpuSkinningGeneration;
	tri->gpuSkinningFallbackReason = GPU_SKINNING_FALLBACK_NONE;
	tri->gpuSkinningSignedWeights = signedWeights;
	tri->gpuSkinningPrevalidatedJoints = Max( sidecarJointBound, 0 );
	return true;
}

void R_GpuSkinning_ReferenceSurfaceContract( srfTriangles_s *tri,
	const srfTriangles_s *reference ) {
	if ( tri == reference ) {
		return;
	}
	R_ReferenceStaticGpuSkinning( tri, reference );
}

gpuSkinningFallbackReason_t R_GpuSkinning_ValidateSurface(
	const gpuSkinningSurface_t &surface ) {
	if ( surface.fallbackReason != GPU_SKINNING_FALLBACK_NONE ) {
		return surface.fallbackReason;
	}
	if ( surface.bindPoseVerts == NULL ) {
		return GPU_SKINNING_FALLBACK_MISSING_BIND_POSE;
	}
	if ( surface.skinVerts == NULL ) {
		return GPU_SKINNING_FALLBACK_MISSING_SKIN_VERTICES;
	}
	if ( surface.numVerts <= 0 ) {
		return GPU_SKINNING_FALLBACK_VERTEX_COUNT;
	}
	if ( surface.palette.matrices == NULL || surface.palette.numJoints <= 0
		|| surface.palette.numJoints > GPU_SKINNING_MAX_JOINTS
		|| surface.palette.matrixStrideFloats != GPU_SKINNING_JOINT_FLOATS ) {
		return GPU_SKINNING_FALLBACK_JOINT_COUNT;
	}
	if ( surface.palette.generation == 0 || surface.palette.generation != rg_gpuSkinningGeneration ) {
		return GPU_SKINNING_FALLBACK_STALE_PALETTE;
	}
	if ( surface.prevalidatedJoints > 0 ) {
		// The model validated every influence of this immutable sidecar when it
		// built it, and the attach copied only finite joints. A surface is
		// checked several times per frame (admission, scene packets, stencil
		// volumes, backend dispatch), so only prove the palette still covers it.
		return surface.prevalidatedJoints <= surface.palette.numJoints
			? GPU_SKINNING_FALLBACK_NONE : GPU_SKINNING_FALLBACK_JOINT_INDEX;
	}
	for ( int jointIndex = 0; jointIndex < surface.palette.numJoints; ++jointIndex ) {
		const float *matrix = surface.palette.matrices
			+ jointIndex * surface.palette.matrixStrideFloats;
		for ( int valueIndex = 0; valueIndex < GPU_SKINNING_JOINT_FLOATS; ++valueIndex ) {
			if ( !std::isfinite( matrix[valueIndex] ) ) {
				return GPU_SKINNING_FALLBACK_MALFORMED_WEIGHTS;
			}
		}
	}

	for ( int vertexIndex = 0; vertexIndex < surface.numVerts; ++vertexIndex ) {
		float totalAbsoluteWeight = 0.0f;
		for ( int influenceIndex = 0; influenceIndex < GPU_SKINNING_INFLUENCES; ++influenceIndex ) {
			const float weight = surface.skinVerts[vertexIndex].jointWeights[influenceIndex];
			if ( !std::isfinite( weight ) || ( !surface.signedWeights && weight < 0.0f ) ) {
				return GPU_SKINNING_FALLBACK_MALFORMED_WEIGHTS;
			}
			if ( weight != 0.0f
				&& surface.skinVerts[vertexIndex].jointIndices[influenceIndex]
					>= static_cast<uint32>( surface.palette.numJoints ) ) {
				return GPU_SKINNING_FALLBACK_JOINT_INDEX;
			}
			totalAbsoluteWeight += idMath::Fabs( weight );
		}
		if ( totalAbsoluteWeight <= 0.0f ) {
			return GPU_SKINNING_FALLBACK_MALFORMED_WEIGHTS;
		}
	}
	return GPU_SKINNING_FALLBACK_NONE;
}

bool R_GpuSkinning_GetSurface( const srfTriangles_s *tri,
	gpuSkinningSurface_t &surface ) {
	memset( &surface, 0, sizeof( surface ) );
	if ( tri == NULL ) {
		surface.fallbackReason = GPU_SKINNING_FALLBACK_VERTEX_COUNT;
		return false;
	}
	surface.bindPoseVerts = tri->gpuSkinningBindPoseVerts;
	surface.skinVerts = reinterpret_cast<const gpuSkinningVertex_t *>( tri->gpuSkinningVerts );
	surface.numVerts = tri->numGpuSkinningVerts;
	surface.palette.matrices = tri->gpuSkinningJointPalette;
	surface.palette.numJoints = tri->numGpuSkinningJoints;
	surface.palette.matrixStrideFloats = GPU_SKINNING_JOINT_FLOATS;
	surface.palette.generation = tri->gpuSkinningPaletteGeneration;
	memset( &surface.palette.buffer, 0, sizeof( surface.palette.buffer ) );
	surface.fallbackReason = static_cast<gpuSkinningFallbackReason_t>( tri->gpuSkinningFallbackReason );
	surface.signedWeights = tri->gpuSkinningSignedWeights;
	surface.prevalidatedJoints = tri->gpuSkinningPrevalidatedJoints;
	surface.fallbackReason = R_GpuSkinning_ValidateSurface( surface );
	return surface.fallbackReason == GPU_SKINNING_FALLBACK_NONE;
}

bool R_GpuSkinning_IsCandidate( const srfTriangles_s *tri ) {
	return tri != NULL && ( tri->gpuSkinningBindPoseVerts != NULL
		|| tri->gpuSkinningVerts != NULL || tri->numGpuSkinningVerts > 0
		|| tri->gpuSkinningFallbackReason != GPU_SKINNING_FALLBACK_NONE );
}

void R_GpuSkinning_RecordCpuSkinning( uint64 elapsedMicroseconds,
	int numVerts, bool positionsOnly ) {
	if ( numVerts <= 0 ) {
		return;
	}
	// openQ4: one lock for the call. This runs for every skinned surface every
	// frame, and each counter used to take the mutex for itself.
	std::lock_guard<std::recursive_mutex> lock( rg_gpuSkinningStatsMutex );
	R_GpuSkinning_AddLocked( rg_gpuSkinningStats.cpuSkinVertices, numVerts );
	R_GpuSkinning_AddLocked( rg_gpuSkinningStats.cpuSkinMicroseconds, elapsedMicroseconds );
	if ( positionsOnly ) {
		R_GpuSkinning_AddLocked( rg_gpuSkinningStats.cpuPositionOnlyVertices, numVerts );
		R_GpuSkinning_AddLocked( rg_gpuSkinningStats.cpuPositionOnlyMicroseconds, elapsedMicroseconds );
	}
}

void R_GpuSkinning_RecordFallback( gpuSkinningFallbackReason_t reason ) {
	if ( reason > GPU_SKINNING_FALLBACK_NONE && reason < GPU_SKINNING_FALLBACK_COUNT ) {
		R_GpuSkinning_SaturatingAdd( rg_gpuSkinningStats.fallbacks[ reason ], 1 );
	}
}

void R_GpuSkinning_RecordBackendPrepared( int numVerts ) {
	R_GpuSkinning_RecordBackendPrepared( numVerts, 0, 0 );
}

void R_GpuSkinning_RecordBackendPrepared( int numVerts, int numJoints,
	uint64 paletteBytes ) {
	std::lock_guard<std::recursive_mutex> lock( rg_gpuSkinningStatsMutex );
	R_GpuSkinning_AddLocked( rg_gpuSkinningStats.backendPrepared, 1 );
	R_GpuSkinning_AddLocked( rg_gpuSkinningStats.surfacesAdmitted, 1 );
	if ( numVerts > 0 ) {
		R_GpuSkinning_AddLocked( rg_gpuSkinningStats.preparedVertices, numVerts );
	}
	if ( numJoints > 0 ) {
		R_GpuSkinning_AddLocked( rg_gpuSkinningStats.paletteUploads, 1 );
		R_GpuSkinning_AddLocked( rg_gpuSkinningStats.paletteJoints, numJoints );
		R_GpuSkinning_AddLocked( rg_gpuSkinningStats.paletteBytes, paletteBytes );
	}
}

const gpuSkinningStats_t &R_GpuSkinning_GetStats( void ) {
	std::lock_guard<std::recursive_mutex> lock( rg_gpuSkinningStatsMutex );
	static thread_local gpuSkinningStats_t snapshot;
	snapshot = rg_gpuSkinningStats;
	return snapshot;
}

void R_GpuSkinning_ContractInit( const renderBackendCaps_t &caps ) {
	memset( &rg_gpuSkinningStats, 0, sizeof( rg_gpuSkinningStats ) );
	if ( ++rg_gpuSkinningGeneration == 0 ) {
		rg_gpuSkinningGeneration = 1;
	}
	rg_gpuSkinningInitialized = true;
	R_BackendGpuSkinning_Init( caps );
}

void R_GpuSkinning_ContractShutdown( void ) {
	if ( rg_gpuSkinningInitialized ) {
		R_BackendGpuSkinning_Shutdown();
	}
	rg_gpuSkinningInitialized = false;
	if ( ++rg_gpuSkinningGeneration == 0 ) {
		rg_gpuSkinningGeneration = 1;
	}
}

bool R_GpuSkinning_PrepareAmbientCache( srfTriangles_s *tri, bool needsLighting ) {
	if ( !R_GpuSkinning_IsCandidate( tri ) ) {
		return false;
	}
	if ( !r_gpuSkinning.GetBool() ) {
		// the default path, taken for every skinned surface: count the attempt
		// and its fallback under one lock
		std::lock_guard<std::recursive_mutex> lock( rg_gpuSkinningStatsMutex );
		R_GpuSkinning_AddLocked( rg_gpuSkinningStats.surfaceAttempts, 1 );
		R_GpuSkinning_AddLocked( rg_gpuSkinningStats.fallbacks[ GPU_SKINNING_FALLBACK_DISABLED ], 1 );
		return false;
	}
	R_GpuSkinning_SaturatingAdd( rg_gpuSkinningStats.surfaceAttempts, 1 );
	gpuSkinningSurface_t surface;
	if ( !R_GpuSkinning_GetSurface( tri, surface ) ) {
		R_GpuSkinning_RecordFallback( surface.fallbackReason );
		return false;
	}
	if ( !R_BackendGpuSkinning_PrepareAmbientCache( tri, needsLighting ) ) {
		// False may mean a backend intentionally retains the CPU ambient cache
		// and performs GPU deformation later (Vulkan). The backend records a
		// classified failure itself when it is genuinely unavailable.
		return false;
	}
	R_GpuSkinning_RecordBackendPrepared( surface.numVerts,
		surface.palette.numJoints,
		static_cast<uint64>( surface.palette.numJoints )
			* GPU_SKINNING_JOINT_FLOATS * sizeof( float ) );
	return true;
}

void R_GpuSkinning_PrintGfxInfo( void ) {
	std::lock_guard<std::recursive_mutex> lock( rg_gpuSkinningStatsMutex );
	common->Printf(
		"GPU skinning: enabled=%d generation=%u packed=%llu exact=%llu attempts=%llu admitted=%llu prepared=%llu cpuSkinVerts=%llu cpuSkinUs=%llu positionOnlyVerts=%llu positionOnlyUs=%llu preparedVertices=%llu paletteUploads=%llu paletteJoints=%llu paletteBytes=%llu cumulative=1\n",
		r_gpuSkinning.GetBool() ? 1 : 0, rg_gpuSkinningGeneration,
		rg_gpuSkinningStats.packedVertices, rg_gpuSkinningStats.exactVertices,
		rg_gpuSkinningStats.surfaceAttempts, rg_gpuSkinningStats.surfacesAdmitted,
		rg_gpuSkinningStats.backendPrepared, rg_gpuSkinningStats.cpuSkinVertices,
		rg_gpuSkinningStats.cpuSkinMicroseconds, rg_gpuSkinningStats.cpuPositionOnlyVertices,
		rg_gpuSkinningStats.cpuPositionOnlyMicroseconds,
		rg_gpuSkinningStats.preparedVertices, rg_gpuSkinningStats.paletteUploads,
		rg_gpuSkinningStats.paletteJoints, rg_gpuSkinningStats.paletteBytes );
	common->Printf(
		"GPU skinning fallbacks: disabled=%llu backendUnavailable=%llu missingBindPose=%llu missingSkinVertices=%llu vertexCount=%llu residualWeights=%llu jointCount=%llu jointIndex=%llu malformedWeights=%llu skinScale=%llu unsupportedMaterial=%llu unsupportedPass=%llu stencilVolume=%llu decalOverlay=%llu paletteAllocation=%llu stalePalette=%llu\n",
		rg_gpuSkinningStats.fallbacks[ GPU_SKINNING_FALLBACK_DISABLED ],
		rg_gpuSkinningStats.fallbacks[ GPU_SKINNING_FALLBACK_BACKEND_UNAVAILABLE ],
		rg_gpuSkinningStats.fallbacks[ GPU_SKINNING_FALLBACK_MISSING_BIND_POSE ],
		rg_gpuSkinningStats.fallbacks[ GPU_SKINNING_FALLBACK_MISSING_SKIN_VERTICES ],
		rg_gpuSkinningStats.fallbacks[ GPU_SKINNING_FALLBACK_VERTEX_COUNT ],
		rg_gpuSkinningStats.fallbacks[ GPU_SKINNING_FALLBACK_RESIDUAL_WEIGHTS ],
		rg_gpuSkinningStats.fallbacks[ GPU_SKINNING_FALLBACK_JOINT_COUNT ],
		rg_gpuSkinningStats.fallbacks[ GPU_SKINNING_FALLBACK_JOINT_INDEX ],
		rg_gpuSkinningStats.fallbacks[ GPU_SKINNING_FALLBACK_MALFORMED_WEIGHTS ],
		rg_gpuSkinningStats.fallbacks[ GPU_SKINNING_FALLBACK_SKIN_SCALE ],
		rg_gpuSkinningStats.fallbacks[ GPU_SKINNING_FALLBACK_UNSUPPORTED_MATERIAL ],
		rg_gpuSkinningStats.fallbacks[ GPU_SKINNING_FALLBACK_UNSUPPORTED_PASS ],
		rg_gpuSkinningStats.fallbacks[ GPU_SKINNING_FALLBACK_STENCIL_VOLUME ],
		rg_gpuSkinningStats.fallbacks[ GPU_SKINNING_FALLBACK_DECAL_OVERLAY ],
		rg_gpuSkinningStats.fallbacks[ GPU_SKINNING_FALLBACK_PALETTE_ALLOCATION ],
		rg_gpuSkinningStats.fallbacks[ GPU_SKINNING_FALLBACK_STALE_PALETTE ] );
	R_BackendGpuSkinning_PrintGfxInfo();
}

bool R_GpuSkinning_RunSelfTest( void ) {
	gpuSkinningInfluence_t influences[5];
	memset( influences, 0, sizeof( influences ) );
	gpuSkinningVertex_t packed;
	gpuSkinningPackResult_t result;
	for ( int i = 0; i < GPU_SKINNING_INFLUENCES; ++i ) {
		influences[i].jointIndex = i;
		influences[i].weight = 0.25f;
	}
	if ( !R_GpuSkinning_PackVertexExact( influences, 4, 4, false, packed, result )
		|| result.meaningfulInfluences != 4 ) {
		return false;
	}
	for ( int influenceCount = 1; influenceCount <= GPU_SKINNING_INFLUENCES; ++influenceCount ) {
		memset( influences, 0, sizeof( influences ) );
		for ( int i = 0; i < influenceCount; ++i ) {
			influences[i].jointIndex = i;
			influences[i].weight = 1.0f / influenceCount;
		}
		if ( !R_GpuSkinning_PackVertexExact( influences, influenceCount, 4,
			false, packed, result ) || result.meaningfulInfluences != influenceCount ) {
			return false;
		}
		for ( int i = 0; i < influenceCount; ++i ) {
			if ( packed.jointIndices[i] != static_cast<uint32>( i )
				|| packed.jointWeights[i] != influences[i].weight ) {
				return false;
			}
		}
	}

	memset( influences, 0, sizeof( influences ) );
	for ( int i = 0; i < 5; ++i ) {
		influences[i].jointIndex = i % 4;
		influences[i].weight = 0.2f;
	}
	influences[4].jointIndex = 0; influences[4].weight = 0.01f;
	if ( R_GpuSkinning_PackVertexExact( influences, 5, 4, false, packed, result )
		|| result.reason != GPU_SKINNING_FALLBACK_RESIDUAL_WEIGHTS ) {
		return false;
	}

	memset( influences, 0, sizeof( influences ) );
	if ( R_GpuSkinning_PackVertexExact( influences, 4, 4, false, packed, result )
		|| result.reason != GPU_SKINNING_FALLBACK_MALFORMED_WEIGHTS
		|| R_GpuSkinning_PackVertexExact( influences, 0, 4, false, packed, result )
		|| result.reason != GPU_SKINNING_FALLBACK_MALFORMED_WEIGHTS ) {
		return false;
	}

	influences[0].weight = std::numeric_limits<float>::quiet_NaN();
	if ( R_GpuSkinning_PackVertexExact( influences, 1, 4, false, packed, result )
		|| result.reason != GPU_SKINNING_FALLBACK_MALFORMED_WEIGHTS ) {
		return false;
	}
	influences[0].weight = -0.5f;
	if ( R_GpuSkinning_PackVertexExact( influences, 1, 4, false, packed, result )
		|| result.reason != GPU_SKINNING_FALLBACK_MALFORMED_WEIGHTS ) {
		return false;
	}
	if ( !R_GpuSkinning_PackVertexExact( influences, 1, 4, true, packed, result ) ) {
		return false;
	}
	influences[0].jointIndex = 4;
	influences[0].weight = 1.0f;
	if ( R_GpuSkinning_PackVertexExact( influences, 1, 4, false, packed, result )
		|| result.reason != GPU_SKINNING_FALLBACK_JOINT_INDEX ) {
		return false;
	}
	influences[0].jointIndex = 0;
	if ( R_GpuSkinning_PackVertexExact( influences, 1, GPU_SKINNING_MAX_JOINTS + 1,
		false, packed, result ) || result.reason != GPU_SKINNING_FALLBACK_JOINT_COUNT ) {
		return false;
	}

	// MD5R stores the fourth weight implicitly and permits it to be signed.
	influences[0].jointIndex = 0; influences[0].weight = 0.6f;
	influences[1].jointIndex = 1; influences[1].weight = 0.5f;
	influences[2].jointIndex = 2; influences[2].weight = 0.2f;
	influences[3].jointIndex = 3;
	influences[3].weight = 1.0f - influences[0].weight
		- influences[1].weight - influences[2].weight;
	if ( !R_GpuSkinning_PackVertexExact( influences, 4, 4, true, packed, result )
		|| packed.jointWeights[3] >= 0.0f || packed.jointWeights[3] != influences[3].weight ) {
		return false;
	}

	float matrices[ GPU_SKINNING_JOINT_FLOATS * 4 ];
	memset( matrices, 0, sizeof( matrices ) );
	for ( int jointIndex = 0; jointIndex < 4; ++jointIndex ) {
		float *matrix = matrices + jointIndex * GPU_SKINNING_JOINT_FLOATS;
		matrix[0] = matrix[5] = matrix[10] = 1.0f;
		matrix[3] = static_cast<float>( jointIndex + 1 );
	}
	gpuSkinningJointPalette_t palette;
	memset( &palette, 0, sizeof( palette ) );
	palette.matrices = matrices;
	palette.numJoints = 4;
	palette.matrixStrideFloats = GPU_SKINNING_JOINT_FLOATS;
	palette.generation = rg_gpuSkinningGeneration;
	idDrawVert bindPose;
	bindPose.Clear();
	memset( bindPose.color2, 0, sizeof( bindPose.color2 ) );
	bindPose.xyz.Set( 1.0f, 2.0f, 3.0f );
	bindPose.normal.Set( 0.0f, 0.0f, 1.0f );
	bindPose.tangents[0].Set( 1.0f, 0.0f, 0.0f );
	bindPose.tangents[1].Set( 0.0f, 1.0f, 0.0f );
	bindPose.st.Set( 0.375f, 0.625f );
	bindPose.SetColor( 0x12345678u );
	bindPose.color2[0] = 0x12;
	bindPose.color2[1] = 0x34;
	bindPose.color2[2] = 0x56;
	bindPose.color2[3] = 0x78;
	memset( &packed, 0, sizeof( packed ) );
	packed.jointIndices[0] = 1;
	packed.jointWeights[0] = 1.0f;
	idDrawVert deformed;
	if ( !R_GpuSkinning_DeformVertexCPU( bindPose, packed, palette, deformed )
		|| !R_GpuSkinning_BasisClose( deformed.xyz, idVec3( 3.0f, 2.0f, 3.0f ), 0.00001f )
		|| deformed.GetColor() != bindPose.GetColor()
		|| memcmp( deformed.color2, bindPose.color2, sizeof( bindPose.color2 ) ) != 0
		|| deformed.st != bindPose.st ) {
		return false;
	}

	gpuSkinningSurface_t surface;
	memset( &surface, 0, sizeof( surface ) );
	surface.bindPoseVerts = &bindPose;
	surface.skinVerts = &packed;
	surface.numVerts = 1;
	surface.palette = palette;
	surface.fallbackReason = GPU_SKINNING_FALLBACK_NONE;
	surface.signedWeights = false;
	if ( R_GpuSkinning_ValidateSurface( surface ) != GPU_SKINNING_FALLBACK_NONE ) {
		return false;
	}
	surface.palette.generation = rg_gpuSkinningGeneration + 1;
	if ( R_GpuSkinning_ValidateSurface( surface ) != GPU_SKINNING_FALLBACK_STALE_PALETTE ) {
		return false;
	}
	surface.palette.generation = rg_gpuSkinningGeneration;
	matrices[GPU_SKINNING_JOINT_FLOATS] = std::numeric_limits<float>::quiet_NaN();
	if ( R_GpuSkinning_ValidateSurface( surface ) != GPU_SKINNING_FALLBACK_MALFORMED_WEIGHTS
		|| R_GpuSkinning_DeformVertexCPU( bindPose, packed, palette, deformed ) ) {
		return false;
	}
	matrices[GPU_SKINNING_JOINT_FLOATS] = 1.0f;
	surface.palette.matrixStrideFloats = GPU_SKINNING_JOINT_FLOATS - 1;
	if ( R_GpuSkinning_ValidateSurface( surface ) != GPU_SKINNING_FALLBACK_JOINT_COUNT ) {
		return false;
	}
	surface.palette.matrixStrideFloats = GPU_SKINNING_JOINT_FLOATS;

	// A prevalidated sidecar is proven once at build: per use, only the bound
	// against the palette counts, and the stale-palette gate still applies.
	gpuSkinningVertex_t outOfRange = packed;
	outOfRange.jointIndices[0] = 7;
	surface.skinVerts = &outOfRange;
	if ( R_GpuSkinning_ValidateSurface( surface ) != GPU_SKINNING_FALLBACK_JOINT_INDEX ) {
		return false;
	}
	surface.skinVerts = &packed;
	surface.prevalidatedJoints = 2;
	if ( R_GpuSkinning_ValidateSurface( surface ) != GPU_SKINNING_FALLBACK_NONE ) {
		return false;
	}
	surface.prevalidatedJoints = 5;
	if ( R_GpuSkinning_ValidateSurface( surface ) != GPU_SKINNING_FALLBACK_JOINT_INDEX ) {
		return false;
	}
	surface.prevalidatedJoints = 2;
	surface.palette.generation = rg_gpuSkinningGeneration + 1;
	if ( R_GpuSkinning_ValidateSurface( surface ) != GPU_SKINNING_FALLBACK_STALE_PALETTE ) {
		return false;
	}
	surface.palette.generation = rg_gpuSkinningGeneration;

	return R_GpuSkinning_CompareVertexCPU( bindPose, bindPose, 0.0f, 0.0f );
}
