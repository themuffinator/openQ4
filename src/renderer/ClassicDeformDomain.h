// Copyright (C) 2026 DarkMatter Productions
//

#ifndef __CLASSIC_DEFORM_DOMAIN_H__
#define __CLASSIC_DEFORM_DOMAIN_H__

#include <cstdint>

class idMaterial;
struct drawSurf_s;
struct srfTriangles_s;
struct vertCache_s;

/*
===============================================================================

	Sealed front-end contract for classic material deformation.

	The established CPU deformers remain authoritative. This contract records
	the source and published draw geometry around that call so scene-packet
	consumers can distinguish completed material deformation from unrelated
	dynamic-model ownership. Pointer fields are frame-local identity checks; the
	semantic hash deliberately excludes addresses, frame tokens, and backend
	buffer names.

===============================================================================
*/

const int CLASSIC_DEFORM_MAX_PARAMETERS = 4;

enum classicDeformRole_t {
	CLASSIC_DEFORM_ROLE_UNKNOWN = 0,
	CLASSIC_DEFORM_ROLE_FINALIZED_DRAW,
	CLASSIC_DEFORM_ROLE_INTERACTION_RECEIVER,
	CLASSIC_DEFORM_ROLE_FOG_RECEIVER,
	CLASSIC_DEFORM_ROLE_SHADOW_VOLUME,
	CLASSIC_DEFORM_ROLE_OTHER,
	CLASSIC_DEFORM_ROLE_COUNT
};

enum classicDeformKind_t {
	CLASSIC_DEFORM_KIND_NONE = 0,
	CLASSIC_DEFORM_KIND_SPRITE,
	CLASSIC_DEFORM_KIND_RECTSPRITE,
	CLASSIC_DEFORM_KIND_TUBE,
	CLASSIC_DEFORM_KIND_FLARE,
	CLASSIC_DEFORM_KIND_EXPAND,
	CLASSIC_DEFORM_KIND_MOVE,
	CLASSIC_DEFORM_KIND_TURBULENT,
	CLASSIC_DEFORM_KIND_EYEBALL,
	CLASSIC_DEFORM_KIND_PARTICLE,
	CLASSIC_DEFORM_KIND_PARTICLE2,
	CLASSIC_DEFORM_KIND_UNKNOWN,
	CLASSIC_DEFORM_KIND_COUNT
};

enum classicDeformOutcome_t {
	CLASSIC_DEFORM_OUTCOME_NONE = 0,
	CLASSIC_DEFORM_OUTCOME_NOT_APPLICABLE,
	CLASSIC_DEFORM_OUTCOME_SKIPPED,
	CLASSIC_DEFORM_OUTCOME_COMPLETED,
	CLASSIC_DEFORM_OUTCOME_EMPTY,
	CLASSIC_DEFORM_OUTCOME_FAILED,
	CLASSIC_DEFORM_OUTCOME_UNSUPPORTED,
	CLASSIC_DEFORM_OUTCOME_COUNT
};

enum classicDeformCacheLifetime_t {
	CLASSIC_DEFORM_CACHE_UNKNOWN = 0,
	CLASSIC_DEFORM_CACHE_STATIC,
	CLASSIC_DEFORM_CACHE_FRAME_TEMP,
	CLASSIC_DEFORM_CACHE_CLIENT_MEMORY,
	CLASSIC_DEFORM_CACHE_DYNAMIC_BRIDGE,
	CLASSIC_DEFORM_CACHE_COUNT
};

typedef struct classicDeformGeometryState_s {
	const srfTriangles_s *geometry;
	const vertCache_s *ambientCache;
	const vertCache_s *indexCache;
	int vertexCount;
	int indexCount;
	unsigned int ambientBuffer;
	unsigned int indexBuffer;
	int ambientOffset;
	int indexOffset;
	int ambientBytes;
	int indexBytes;
	int ambientTag;
	int indexTag;
	classicDeformCacheLifetime_t lifetime;
	bool captured;
	bool hasAmbientCache;
	bool hasIndexCache;
	bool ambientCacheHasBacking;
	bool indexCacheHasBacking;
	bool hasClientVertexData;
	bool hasClientIndexData;
	bool hasPrimBatchMesh;
	bool cacheStateValid;
} classicDeformGeometryState_t;

typedef struct classicDeformRecord_s {
	classicDeformRole_t role;
	classicDeformKind_t kind;
	classicDeformOutcome_t outcome;
	const idMaterial *sourceMaterial;
	const idMaterial *resultMaterial;
	classicDeformGeometryState_t sourceGeometry;
	classicDeformGeometryState_t resultGeometry;
	int sourceMaterialIndex;
	int resultMaterialIndex;
	std::uint64_t frameToken;
	std::uint64_t sourceMaterialNameHash;
	std::uint64_t resultMaterialNameHash;
	std::uint64_t deformDeclNameHash;
	std::uint64_t inputSemanticHash;
	int parameterCount;
	int parameterRegisters[ CLASSIC_DEFORM_MAX_PARAMETERS ];
	float parameterValues[ CLASSIC_DEFORM_MAX_PARAMETERS ];
	float flareScale;
	bool parametersValid;
	bool cpuFinalized;
	bool initialized;
	bool valid;
	std::uint64_t semanticHash;
} classicDeformRecord_t;

typedef struct classicDeformDomainStats_s {
	std::uint64_t frameToken;
	int records;
	int none;
	int notApplicable;
	int completed;
	int empty;
	int skipped;
	int failed;
	int unsupported;
	int invalid;
	std::uint64_t hash;
} classicDeformDomainStats_t;

// Begin/End bracket only R_FinalizeDrawSurf's authoritative CPU call. Passes
// which reuse an unfinalized receiver seal NOT_APPLICABLE through Snapshot.
void R_ClassicDeformDomain_BeginDrawSurf( drawSurf_s *drawSurf );
void R_ClassicDeformDomain_EndDrawSurf( drawSurf_s *drawSurf );

std::uint64_t R_ClassicDeformDomain_CurrentFrameToken( void );
std::uint64_t R_ClassicDeformDomain_BackEndFrameToken( void );
void R_ClassicDeformDomain_SnapshotDrawSurf( const drawSurf_s *drawSurf,
	classicDeformRole_t role, std::uint64_t frameToken,
	classicDeformRecord_t &record );
void R_ClassicDeformDomain_SnapshotDrawSurf( const drawSurf_s *drawSurf,
	classicDeformRole_t role, classicDeformRecord_t &record );

std::uint64_t R_ClassicDeformDomain_ComputeSemanticHash(
	const classicDeformRecord_t &record );
bool R_ClassicDeformDomain_ValidateRecord(
	const classicDeformRecord_t &record );
bool R_ClassicDeformDomain_ValidateRecordForFrame(
	const classicDeformRecord_t &record, std::uint64_t frameToken );
bool R_ClassicDeformDomain_RecordFreshForFrame(
	const classicDeformRecord_t &record, std::uint64_t frameToken );
bool R_ClassicDeformDomain_RecordMatchesDrawSurf(
	const classicDeformRecord_t &record, const drawSurf_s *drawSurf, std::uint64_t frameToken );
bool R_ClassicDeformDomain_SameProvenance(
	const classicDeformRecord_t &a, const classicDeformRecord_t &b );
bool R_ClassicDeformDomain_HasCompletedOutput(
	const classicDeformRecord_t &record );
bool R_ClassicDeformDomain_HasEmptyOutput(
	const classicDeformRecord_t &record );
bool R_ClassicDeformDomain_IsFailClosed(
	const classicDeformRecord_t &record );

// Scene-packet diagnostics retain the last completed front-end frame so
// gfxInfo remains useful after the transient packet arena has been released.
void R_ClassicDeformDomain_BeginPacketFrame( std::uint64_t frameToken );
void R_ClassicDeformDomain_RecordPacket(
	const classicDeformRecord_t &record );
const classicDeformDomainStats_t &R_ClassicDeformDomain_Stats( void );

const char *ClassicDeformRole_Name( classicDeformRole_t role );
const char *ClassicDeformKind_Name( classicDeformKind_t kind );
const char *ClassicDeformOutcome_Name( classicDeformOutcome_t outcome );
const char *ClassicDeformCacheLifetime_Name(
	classicDeformCacheLifetime_t lifetime );

bool RendererClassicDeformDomain_RunSelfTest( void );

#endif /* !__CLASSIC_DEFORM_DOMAIN_H__ */

void R_ClassicDeformDomain_RebaseFrameCopy( classicDeformRecord_t &record, const srfTriangles_s *source, const srfTriangles_s *result );
