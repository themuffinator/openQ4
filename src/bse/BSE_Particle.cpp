// Copyright (C) 2007 Id Software, Inc.
//

#include "../idlib/precompiled.h"
#if defined(_MSC_VER)
#pragma hdrstop
#endif

#include "BSE_Envelope.h"
#include "BSE_Particle.h"
#include "BSE_SpawnDomains.h"
#include "BSE.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../framework/Session.h"

struct SElecWork {
	srfTriangles_t* tri;
	idVec3* coords;
	int coordCount;
	idVec3 length;
	idVec3 forward;
	idVec3 viewPos;
	idVec4 tint;
	float size;
	float alpha;
	float step;
	float fraction;
};

namespace {
ID_INLINE int BSE_GetFrameCounterMode() {
	return cvarSystem ? cvarSystem->GetCVarInteger("bse_frameCounters") : 0;
}

// Low byte of the rounded value: colours outside 0..1 wrap instead of clamping.
ID_INLINE byte ToByte(float x) {
	return static_cast<byte>(lrintf(x * 255.0f));
}

ID_INLINE dword PackColorLocal(const idVec4& color) {
	const dword r = static_cast<dword>(ToByte(color[0]));
	const dword g = static_cast<dword>(ToByte(color[1])) << 8;
	const dword b = static_cast<dword>(ToByte(color[2])) << 16;
	const dword a = static_cast<dword>(ToByte(color[3])) << 24;
	return r | g | b | a;
}

ID_INLINE void UnpackColor(dword rgba, byte out[4]) {
	out[0] = static_cast<byte>(rgba & 0xFF);
	out[1] = static_cast<byte>((rgba >> 8) & 0xFF);
	out[2] = static_cast<byte>((rgba >> 16) & 0xFF);
	out[3] = static_cast<byte>((rgba >> 24) & 0xFF);
}

ID_INLINE void SetDrawVert(idDrawVert& vert, const idVec3& xyz, float s, float t, dword rgba, const idVec3* normal = NULL) {
	byte color[4];
	UnpackColor(rgba, color);

	vert.Clear();
	vert.xyz = xyz;
	vert.st[0] = s;
	vert.st[1] = t;
	vert.normal = normal ? *normal : idVec3(1.0f, 0.0f, 0.0f);
	vert.tangents[0].Set(0.0f, 1.0f, 0.0f);
	vert.tangents[1].Set(0.0f, 0.0f, 1.0f);
	vert.color[0] = color[0];
	vert.color[1] = color[1];
	vert.color[2] = color[2];
	vert.color[3] = color[3];
}

ID_INLINE bool HasTriCapacity(const srfTriangles_t* tri, int addVerts, int addIndexes) {
	if (!tri || addVerts < 0 || addIndexes < 0) {
		return false;
	}
	if (tri->numVerts < 0 || tri->numIndexes < 0) {
		return false;
	}
	if (tri->numAllocedVerts > 0 &&
		(tri->numVerts > tri->numAllocedVerts || addVerts > tri->numAllocedVerts - tri->numVerts)) {
		return false;
	}
	if (tri->numAllocedIndices > 0 &&
		(tri->numIndexes > tri->numAllocedIndices || addIndexes > tri->numAllocedIndices - tri->numIndexes)) {
		return false;
	}
	return true;
}

ID_INLINE void AppendQuad(srfTriangles_t* tri, const idVec3& p0, const idVec3& p1, const idVec3& p2, const idVec3& p3, dword rgba, const idVec3* normal = NULL) {
	const int base = tri->numVerts;
	SetDrawVert(tri->verts[base + 0], p0, 0.0f, 0.0f, rgba, normal);
	SetDrawVert(tri->verts[base + 1], p1, 1.0f, 0.0f, rgba, normal);
	SetDrawVert(tri->verts[base + 2], p2, 1.0f, 1.0f, rgba, normal);
	SetDrawVert(tri->verts[base + 3], p3, 0.0f, 1.0f, rgba, normal);

	const int indexBase = tri->numIndexes;
	tri->indexes[indexBase + 0] = base + 0;
	tri->indexes[indexBase + 1] = base + 1;
	tri->indexes[indexBase + 2] = base + 2;
	tri->indexes[indexBase + 3] = base + 0;
	tri->indexes[indexBase + 4] = base + 2;
	tri->indexes[indexBase + 5] = base + 3;
	tri->numVerts += 4;
	tri->numIndexes += 6;
}

ID_INLINE idVec3 WorldFromLocal(const rvBSE* effect, const idVec3& local) {
	if (!effect) {
		return local;
	}
	return effect->GetCurrentOrigin() + effect->GetCurrentAxis() * local;
}

// Bolt side axis: horizontal and square to the bolt, +x when the bolt is vertical.
ID_INLINE idVec3 BoltLeft(const idVec3& forward) {
	const float planar = forward.x * forward.x + forward.y * forward.y;
	if (planar == 0.0f) {
		return idVec3(1.0f, 0.0f, 0.0f);
	}
	const float inv = idMath::InvSqrt(planar);
	return idVec3(-forward.y * inv, forward.x * inv, 0.0f);
}

ID_INLINE idMat3 BuildInitToCurrentAxis(const rvBSE* effect, const idMat3& initAxis) {
	if (!effect) {
		return initAxis;
	}
	// Vanilla converts unlocked particle axes with matrix-divide semantics.
	return initAxis / effect->GetCurrentAxis();
}

ID_INLINE void SpawnVector(rvParticleParms* parms, idVec3& dest) {
	if (parms) {
		parms->Spawn(dest.ToFloatPtr(), *parms, NULL, NULL);
	}
	else {
		dest.Zero();
	}
}

ID_INLINE void BSETraceRenderDrop(const char* typeName, const rvParticle* particle, float time, const char* reason, float value0 = 0.0f, float value1 = 0.0f) {
	static int bseRenderDropTraceCount = 0;
	if (!particle || BSE_GetFrameCounterMode() < 2 || bseRenderDropTraceCount >= 256) {
		return;
	}
	const float duration = particle->GetDuration();
	const float endTime = particle->GetEndTime();
	const float startTime = endTime - duration;

	common->Printf(
		"BSE render drop %d: type=%s reason=%s time=%.4f start=%.4f end=%.4f dur=%.4f v0=%.4f v1=%.4f\n",
		bseRenderDropTraceCount,
		typeName ? typeName : "<null>",
		reason ? reason : "<null>",
		time,
		startTime,
		endTime,
		duration,
		value0,
		value1);
	++bseRenderDropTraceCount;
}

}

// ---------------------------------------------------------------------------
//  attenuation helpers
// ---------------------------------------------------------------------------
void rvParticle::Attenuate(float atten, rvParticleParms& parms, rvEnvParms1& result) {
	if ((parms.mFlags & PPFLAG_ATTENUATE) == 0) {
		return;
	}
	if (parms.mFlags & PPFLAG_INV_ATTENUATE) {
		atten = 1.0f - atten;
	}
	result.Scale(atten);
}

void rvParticle::Attenuate(float atten, rvParticleParms& parms, rvEnvParms2& result) {
	if ((parms.mFlags & PPFLAG_ATTENUATE) == 0) {
		return;
	}
	if (parms.mFlags & PPFLAG_INV_ATTENUATE) {
		atten = 1.0f - atten;
	}
	result.Scale(atten);
}

void rvParticle::Attenuate(float atten, rvParticleParms& parms, rvEnvParms3& result) {
	if ((parms.mFlags & PPFLAG_ATTENUATE) == 0) {
		return;
	}
	if (parms.mFlags & PPFLAG_INV_ATTENUATE) {
		atten = 1.0f - atten;
	}
	result.Scale(atten);
}

void rvParticle::Attenuate(float atten, rvParticleParms& parms, rvEnvParms1Particle& result) {
	if ((parms.mFlags & PPFLAG_ATTENUATE) == 0) {
		return;
	}
	if (parms.mFlags & PPFLAG_INV_ATTENUATE) {
		atten = 1.0f - atten;
	}
	result.Scale(atten);
}

void rvParticle::Attenuate(float atten, rvParticleParms& parms, rvEnvParms2Particle& result) {
	if ((parms.mFlags & PPFLAG_ATTENUATE) == 0) {
		return;
	}
	if (parms.mFlags & PPFLAG_INV_ATTENUATE) {
		atten = 1.0f - atten;
	}
	result.Scale(atten);
}

void rvParticle::Attenuate(float atten, rvParticleParms& parms, rvEnvParms3Particle& result) {
	if ((parms.mFlags & PPFLAG_ATTENUATE) == 0) {
		return;
	}
	if (parms.mFlags & PPFLAG_INV_ATTENUATE) {
		atten = 1.0f - atten;
	}
	result.Scale(atten);
}

void rvLineParticle::HandleTiling(rvParticleTemplate* pt) {
	// One texture repeat per tiling units of the spawn length.
	if (pt && GetTiled() && pt->GetTiling() != 0.0f) {
		const float* len = GetInitLength();
		mTextureScale = idVec3(len[0], len[1], len[2]).Length() / pt->GetTiling();
	}
}

void rvLinkedParticle::HandleTiling(rvParticleTemplate* pt) {
	if (pt && GetTiled()) {
		mTextureScale = pt->GetTiling();
	}
}

// ---------------------------------------------------------------------------
//  array helpers
// ---------------------------------------------------------------------------
#define DEFINE_ARRAY_ENTRY(TYPE) \
	rvParticle* TYPE::GetArrayEntry(int i) const { \
		return (i < 0) ? NULL : const_cast<TYPE*>(this) + i; \
	}

#define DEFINE_ARRAY_INDEX(TYPE) \
	int TYPE::GetArrayIndex(rvParticle* p) const { \
		if (!p) { return -1; } \
		const ptrdiff_t diff = reinterpret_cast<const byte*>(p) - reinterpret_cast<const byte*>(this); \
		return static_cast<int>(diff / sizeof(TYPE)); \
	}

DEFINE_ARRAY_ENTRY(rvSpriteParticle)
DEFINE_ARRAY_INDEX(rvSpriteParticle)
DEFINE_ARRAY_ENTRY(rvLineParticle)
DEFINE_ARRAY_INDEX(rvLineParticle)
DEFINE_ARRAY_ENTRY(rvOrientedParticle)
DEFINE_ARRAY_INDEX(rvOrientedParticle)
DEFINE_ARRAY_ENTRY(rvElectricityParticle)
DEFINE_ARRAY_INDEX(rvElectricityParticle)
DEFINE_ARRAY_ENTRY(rvDecalParticle)
DEFINE_ARRAY_INDEX(rvDecalParticle)
DEFINE_ARRAY_ENTRY(rvModelParticle)
DEFINE_ARRAY_INDEX(rvModelParticle)
DEFINE_ARRAY_ENTRY(rvLightParticle)
DEFINE_ARRAY_INDEX(rvLightParticle)
DEFINE_ARRAY_ENTRY(rvLinkedParticle)
DEFINE_ARRAY_INDEX(rvLinkedParticle)
DEFINE_ARRAY_ENTRY(rvDebrisParticle)
DEFINE_ARRAY_INDEX(rvDebrisParticle)

// ---------------------------------------------------------------------------
//  spawning helpers
// ---------------------------------------------------------------------------
void rvParticle::SetOriginUsingEndOrigin(rvBSE* effect, rvParticleTemplate* pt, idVec3* normal, idVec3* centre) {
	// Sample once, then resample in the effect frame with x stretched from that first
	// sample out to the distance from the original origin to the current end origin.
	rvParticleParms& parms = *pt->mpSpawnPosition;
	parms.Spawn(mInitPos.ToFloatPtr(), parms, NULL, NULL);

	rvParticleParms endParms;
	endParms = parms;
	endParms.mMins.x = mInitPos.x;
	endParms.mMaxs.x = (effect->GetCurrentEndOrigin() - effect->GetOriginalOrigin()).Length();

	mInitPos.x = mFraction;
	endParms.Spawn(mInitPos.ToFloatPtr(), endParms, normal, centre);
}

void rvParticle::HandleEndOrigin(rvBSE* effect, rvParticleTemplate* pt, idVec3* normal, idVec3* centre) {
	if (!pt || !pt->mpSpawnPosition) {
		mInitPos.Zero();
		return;
	}

	// Preserve per-particle spawn fraction for end-origin domains.
	mInitPos.x = mFraction;

	if (effect && effect->GetHasEndOrigin() && (pt->mpSpawnPosition->mFlags & PPFLAG_USEENDORIGIN)) {
		SetOriginUsingEndOrigin(effect, pt, normal, centre);
		return;
	}

	pt->mpSpawnPosition->Spawn(mInitPos.ToFloatPtr(), *pt->mpSpawnPosition, normal, centre);
}

void rvParticle::SetLengthUsingEndOrigin(rvBSE* effect, rvParticleParms& parms, float* length) {
	// The x range is pushed out by the current origin to end origin distance; the
	// copy drops the parm flags.
	const float distance = (effect->GetCurrentEndOrigin() - effect->GetCurrentOrigin()).Length();
	rvParticleParms endParms;
	endParms = parms;
	endParms.mFlags = 0;
	endParms.mMins.x += distance;
	endParms.mMaxs.x += distance;
	endParms.Spawn(length, endParms, NULL, NULL);
}

void rvParticle::HandleEndLength(rvBSE* effect, rvParticleTemplate* pt, rvParticleParms& parms, float* length) {
	// Init and dest lengths both follow the spawn length's useEndOrigin flag.
	if (effect && effect->GetHasEndOrigin() && pt->mpSpawnLength && (pt->mpSpawnLength->mFlags & PPFLAG_USEENDORIGIN)) {
		SetLengthUsingEndOrigin(effect, parms, length);
	}
	else {
		parms.Spawn(length, parms, NULL, NULL);
	}
}

void rvParticle::FinishSpawn(rvBSE* effect, rvSegment* segment, float birthTime, float fraction, const idVec3& initOffset, const idMat3& initAxis) {
	if (!effect || !segment) {
		return;
	}

	rvSegmentTemplate* st = segment->GetSegmentTemplate();
	if (!st) {
		return;
	}
	rvParticleTemplate* pt = st->GetParticleTemplate();
	if (!pt) {
		return;
	}

	mNext = NULL;
	mMotionStartTime = birthTime;
	mLastTrailTime = birthTime;
	mFlags = pt->GetFlags();
	SetLocked(st->GetLocked());
	mStartTime = birthTime;
	mFraction = fraction;
	mTextureScale = 1.0f;
	mTextureOffset = 0.0f;
	mInitEffectPos = vec3_origin;
	mInitAxis = mat3_identity;
	mTrailRepeat = pt->GetTrailRepeat();

	SpawnVector(pt->mpSpawnVelocity, mVelocity);
	SpawnVector(pt->mpSpawnAcceleration, mAcceleration);
	SpawnVector(pt->mpSpawnFriction, mFriction);

	const bool generatedOriginNormal = pt->GetGeneratedOriginNormal();
	const bool generatedNormal = pt->GetGeneratedNormal();

	idVec3 normal(1.0f, 0.0f, 0.0f);
	if (generatedOriginNormal) {
		HandleEndOrigin(effect, pt, &normal, NULL);
	}
	else if (generatedNormal) {
		idVec3 centre = pt->mCentre;
		HandleEndOrigin(effect, pt, &normal, &centre);
	}
	else {
		if (pt->GetCalculatedNormal() && pt->mpSpawnDirection) {
			pt->mpSpawnDirection->Spawn(normal.ToFloatPtr(), *pt->mpSpawnDirection, NULL, NULL);
		}
		HandleEndOrigin(effect, pt, NULL, NULL);
	}

	// Trail children (PTFLAG_LINKED) move in the frame of the particle that emitted them.
	const bool trailChild = (pt->GetFlags() & PTFLAG_LINKED) != 0;
	if (trailChild) {
		mVelocity = initAxis * mVelocity;
	}

	// Orientation follows the sampled normal; generated normals also carry velocity and length.
	rvAngles orientation = normal.ToRadians();
	if (generatedOriginNormal || generatedNormal) {
		normal.Normalize();
		mVelocity = normal.ToMat3() * mVelocity;
		TransformLength(normal);
	}
	if (pt->GetFlippedNormal()) {
		mVelocity = -mVelocity;
		ScaleLength(-1.0f);
	}

	// Acceleration is authored relative to the direction of travel and friction relative to
	// the direction of acceleration; a zero vector keeps the previous frame.
	if (mVelocity.LengthSqr() != 0.0f) {
		normal = mVelocity;
		normal.Normalize();
	}
	mAcceleration = normal.ToMat3() * mAcceleration;
	if (mAcceleration.LengthSqr() != 0.0f) {
		normal = mAcceleration;
		normal.Normalize();
	}
	mFriction = normal.ToMat3() * mFriction;

	// Unlocked particles keep the spawn-time effect frame, placed where the effect was
	// at the birth time within the frame.
	if (GetLocked()) {
		mInitAxis = initAxis;
	}
	else {
		mInitEffectPos = effect->GetCurrentOrigin();
		mInitAxis = effect->GetCurrentAxis();
		mInitPos -= mInitAxis.Transpose() * effect->GetInterpolatedOffset(birthTime);
	}
	if (trailChild) {
		mInitPos += initOffset;
	}
	if (pt->GetParentVelocity()) {
		mVelocity += effect->GetCurrentVelocity();
	}

	// The offset envelope only runs when either end of the offset is authored.
	if ((pt->mpSpawnOffset && pt->mpSpawnOffset->mSpawnType != SPF_NONE_3) || (pt->mpDeathOffset && pt->mpDeathOffset->mSpawnType != SPF_NONE_3)) {
		SetHasOffset(true);
	}

	const float duration = idMath::ClampFloat(BSE_TIME_EPSILON, BSE_MAX_DURATION, pt->GetDuration());
	mEndTime = mStartTime + duration;
	mOneOverDuration = 1.0f / duration;

	if (pt->mpSpawnTint) {
		pt->mpSpawnTint->Spawn(mTintEnv.GetStart(), *pt->mpSpawnTint, NULL, NULL);
	}
	if (pt->mpDeathTint) {
		pt->mpDeathTint->Spawn(mTintEnv.GetEnd(), *pt->mpDeathTint, NULL, NULL);
		pt->mpDeathTint->HandleRelativeParms(mTintEnv.GetEnd(), mTintEnv.GetStart(), 3);
	}

	if (pt->mpSpawnFade) {
		pt->mpSpawnFade->Spawn(mFadeEnv.GetStart(), *pt->mpSpawnFade, NULL, NULL);
	}
	if (pt->mpDeathFade) {
		pt->mpDeathFade->Spawn(mFadeEnv.GetEnd(), *pt->mpDeathFade, NULL, NULL);
		pt->mpDeathFade->HandleRelativeParms(mFadeEnv.GetEnd(), mFadeEnv.GetStart(), 1);
	}

	if (pt->mpSpawnAngle) {
		pt->mpSpawnAngle->Spawn(mAngleEnv.GetStart(), *pt->mpSpawnAngle, NULL, NULL);
	}
	if (pt->mpDeathAngle) {
		pt->mpDeathAngle->Spawn(mAngleEnv.GetEnd(), *pt->mpDeathAngle, NULL, NULL);
		pt->mpDeathAngle->HandleRelativeParms(mAngleEnv.GetEnd(), mAngleEnv.GetStart(), 3);
	}

	if (pt->mpSpawnOffset) {
		pt->mpSpawnOffset->Spawn(mOffsetEnv.GetStart(), *pt->mpSpawnOffset, NULL, NULL);
	}
	if (pt->mpDeathOffset) {
		pt->mpDeathOffset->Spawn(mOffsetEnv.GetEnd(), *pt->mpDeathOffset, NULL, NULL);
		pt->mpDeathOffset->HandleRelativeParms(mOffsetEnv.GetEnd(), mOffsetEnv.GetStart(), 3);
	}

	if (float* initSize = GetInitSize()) {
		if (pt->mpSpawnSize) {
			pt->mpSpawnSize->Spawn(initSize, *pt->mpSpawnSize, NULL, NULL);
		}
		if (float* destSize = GetDestSize()) {
			if (pt->mpDeathSize) {
				pt->mpDeathSize->Spawn(destSize, *pt->mpDeathSize, NULL, NULL);
				pt->mpDeathSize->HandleRelativeParms(destSize, initSize, pt->mNumSizeParms);
			}
		}
	}

	if (float* initRotate = GetInitRotation()) {
		if (pt->mpSpawnRotate) {
			pt->mpSpawnRotate->Spawn(initRotate, *pt->mpSpawnRotate, NULL, NULL);
		}
		if (float* destRotate = GetDestRotation()) {
			if (pt->mpDeathRotate) {
				pt->mpDeathRotate->Spawn(destRotate, *pt->mpDeathRotate, NULL, NULL);
				pt->mpDeathRotate->HandleRelativeParms(destRotate, initRotate, pt->mNumRotateParms);
			}
		}
	}

	// Angles/rotation in decls are specified in turns; runtime evaluates radians.
	ScaleRotation(idMath::TWO_PI);
	ScaleAngle(idMath::TWO_PI);
	HandleOrientation(orientation);

	mTrailTime = pt->GetTrailTime();
	mTrailCount = pt->GetTrailCount();
	SetModel(pt->GetModel());
	SetupElectricity(pt);

	const float attenuation = effect->GetAttenuation(st);
	if (pt->mpSpawnFade) {
		AttenuateFade(attenuation, *pt->mpSpawnFade);
	}
	if (pt->mpSpawnSize) {
		AttenuateSize(attenuation, *pt->mpSpawnSize);
	}
	const float gravityScale = pt->GetGravity();
	if (gravityScale != 0.0f) {
		// Particle gravity is authored in effect-space and must be reprojected
		// into the current local frame used by the particle simulation.
		const idVec3 gravityWorld = effect->GetGravity() * gravityScale;
		mAcceleration += effect->GetCurrentAxisTransposed() * gravityWorld;
	}

	HandleTiling(pt);
	mPosition = mInitPos;
}

void rvLineParticle::FinishSpawn(rvBSE* effect, rvSegment* segment, float birthTime, float fraction, const idVec3& initOffset, const idMat3& initAxis) {
	rvSegmentTemplate* st = segment ? segment->GetSegmentTemplate() : NULL;
	rvParticleTemplate* pt = st ? st->GetParticleTemplate() : NULL;
	if (!effect || !pt || !pt->mpSpawnLength || !pt->mpDeathLength) {
		rvParticle::FinishSpawn(effect, segment, birthTime, fraction, initOffset, initAxis);
		return;
	}

	// Lengths are sampled first so the base spawn's normal and flip transforms apply to
	// both; tiling (in the base spawn) sees the length before attenuation.
	float* initLength = GetInitLength();
	float* destLength = GetDestLength();
	HandleEndLength(effect, pt, *pt->mpSpawnLength, initLength);
	HandleEndLength(effect, pt, *pt->mpDeathLength, destLength);
	rvParticle::FinishSpawn(effect, segment, birthTime, fraction, initOffset, initAxis);
	pt->mpDeathLength->HandleRelativeParms(destLength, initLength, 3);
	AttenuateLength(effect->GetAttenuation(st), *pt->mpSpawnLength);
}

void rvLinkedParticle::FinishSpawn(rvBSE* effect, rvSegment* segment, float birthTime, float fraction, const idVec3& initOffset, const idMat3& initAxis) {
	rvParticle::FinishSpawn(effect, segment, birthTime, fraction, initOffset, initAxis);
}

void rvDebrisParticle::FinishSpawn(rvBSE* effect, rvSegment* segment, float birthTime, float fraction, const idVec3& initOffset, const idMat3& initAxis) {
	rvSegmentTemplate* st = segment ? segment->GetSegmentTemplate() : NULL;
	rvParticleTemplate* pt = st ? st->GetParticleTemplate() : NULL;

	// Debris only hands off to a client moveable; the particle itself dies at birth.
	mNext = NULL;
	mFlags = pt ? pt->GetFlags() : 0;
	mStartTime = mMotionStartTime = mLastTrailTime = mEndTime = birthTime;
	mTrailTime = 0.0f;
	mTrailCount = 0;
	mFraction = fraction;
	mTextureScale = 1.0f;
	mInitEffectPos.Zero();
	mInitAxis = mat3_identity;
	mInitPos.Zero();
	mVelocity.Zero();
	mAcceleration.Zero();
	mFriction.Zero();
	mPosition.Zero();
	if (!effect || !pt || !game || !bse_debris.GetBool() || session->readDemo) {
		return;
	}

	SpawnVector(pt->mpSpawnVelocity, mVelocity);
	idVec3 normal(1.0f, 0.0f, 0.0f);
	if (pt->GetGeneratedOriginNormal()) {
		HandleEndOrigin(effect, pt, &normal, NULL);
	}
	else if (pt->GetGeneratedNormal()) {
		idVec3 centre = pt->mCentre;
		HandleEndOrigin(effect, pt, &normal, &centre);
	}
	else {
		HandleEndOrigin(effect, pt, NULL, NULL);
	}
	if (pt->GetGeneratedOriginNormal() || pt->GetGeneratedNormal()) {
		normal.Normalize();
		mVelocity = normal.ToMat3() * mVelocity;
	}
	if (pt->GetFlippedNormal()) {
		mVelocity = -mVelocity;
	}

	// Placed like an unlocked particle, but handed over through the effect's original frame.
	mInitEffectPos = effect->GetCurrentOrigin();
	mInitAxis = effect->GetCurrentAxis();
	mInitPos -= mInitAxis.Transpose() * effect->GetInterpolatedOffset(birthTime);
	mPosition = mInitPos;

	float* destRotate = GetDestRotation();
	if (pt->mpSpawnRotate) {
		pt->mpSpawnRotate->Spawn(GetInitRotation(), *pt->mpSpawnRotate, NULL, NULL);
	}
	if (pt->mpDeathRotate) {
		pt->mpDeathRotate->Spawn(destRotate, *pt->mpDeathRotate, NULL, NULL);
	}
	ScaleRotation(idMath::TWO_PI);

	// The end rotation is the spin; a zero lifetime lets the entityDef's duration apply.
	const idVec3 origin = effect->GetOriginalOrigin() + effect->GetOriginalAxis() * mInitPos;
	const idVec3 velocity = effect->GetCurrentAxis() * mVelocity;
	const idVec3 angularVelocity(destRotate[0], destRotate[1], destRotate[2]);
	game->SpawnClientMoveable(pt->GetEntityDefName(), 0, origin, effect->GetCurrentAxis(), velocity, angularVelocity);
}

void rvLineParticle::Refresh(rvBSE* effect, rvSegmentTemplate* st, rvParticleTemplate* pt) {
	if (!effect || !pt || !pt->mpSpawnLength || !pt->mpDeathLength) {
		return;
	}

	// Resample both lengths against the moved end origin; unlike spawning, no
	// attenuation or normal/flip transform is applied.
	float* initLength = GetInitLength();
	float* destLength = GetDestLength();
	HandleEndLength(effect, pt, *pt->mpSpawnLength, initLength);
	HandleEndLength(effect, pt, *pt->mpDeathLength, destLength);
	pt->mpDeathLength->HandleRelativeParms(destLength, initLength, 3);
	HandleTiling(pt);
}

// ---------------------------------------------------------------------------
//  simulation
// ---------------------------------------------------------------------------
bool rvParticle::GetEvaluationTime(float time, float& evalTime, bool infinite) {
	evalTime = time - mStartTime;
	if (time >= mEndTime - BSE_TIME_EPSILON) {
		evalTime = (mEndTime - mStartTime) - BSE_TIME_EPSILON;
	}
	if (infinite) {
		return true;
	}
	return (time > mStartTime - BSE_TIME_EPSILON) && (time < mEndTime);
}

void rvParticle::EvaluateVelocity(const rvBSE* effect, idVec3& velocity, float time) {
	(void)effect;

	const float t = Max(0.0f, time - mMotionStartTime);
	if (GetStationary()) {
		velocity.Set(1.0f, 0.0f, 0.0f);
		return;
	}

	velocity = mVelocity + mAcceleration * t;
	if (mFriction.LengthSqr() != 0.0f) {
		const float duration = Max(BSE_TIME_EPSILON, GetDuration());
		velocity += mFriction * (0.5f * t * t * (1.0f + idMath::Exp((duration - t) / duration) * (1.0f - t * (1.0f / 3.0f))));
	}
}

void rvParticle::EvaluatePosition(const rvBSE* effect, rvParticleTemplate* pt, idVec3& pos, float time) {
	const float t = Max(0.0f, time - mMotionStartTime);
	if (GetStationary()) {
		pos = mInitPos;
	}
	else {
		pos = mInitPos + mVelocity * t;

		if (GetHasOffset() && pt && pt->mpAngleEnvelope && pt->mpOffsetEnvelope) {
			rvAngles angle;
			idVec3 offset;
			EvaluateAngle(pt->mpAngleEnvelope, t, mOneOverDuration, angle);
			EvaluateOffset(pt->mpOffsetEnvelope, t, mOneOverDuration, offset);

			idMat3 rotation;
			angle.ToMat3(rotation);
			pos += rotation * offset;
		}

		// Friction is an exponential drift that pushes along its vector early in life and
		// pulls back once half t squared passes the duration.
		const float halfT2 = 0.5f * t * t;
		pos += mAcceleration * halfT2;
		if (mFriction.LengthSqr() != 0.0f) {
			const float duration = Max(BSE_TIME_EPSILON, GetDuration());
			pos += mFriction * ((idMath::Exp((duration - halfT2) / duration) - 1.0f) * halfT2 * halfT2 * (1.0f / 3.0f));
		}
	}

	// Unlocked particles, resting ones included, stay in the effect frame they were born in.
	if (effect && !GetLocked()) {
		const idMat3 initToCurrent = BuildInitToCurrentAxis(effect, mInitAxis);
		pos = initToCurrent * pos;

		const idVec3 delta = mInitEffectPos - effect->GetCurrentOrigin();
		pos += effect->GetCurrentAxisTransposed() * delta;
	}

	mPosition = pos;
}

bool rvParticle::RunPhysics(rvBSE* effect, rvSegmentTemplate* st, float time) {
	if (!effect || !st || !bse_physics.GetBool() || session->readDemo) {
		return false;
	}

	if (GetStationary()) {
		return false;
	}

	rvParticleTemplate* pt = st->GetParticleTemplate();
	if (!pt || !pt->GetHasPhysics()) {
		return false;
	}

	if (time - mMotionStartTime < BSE_PHYSICS_TIME_SAMPLE) {
		return false;
	}

	float sourceTime = time - BSE_PHYSICS_TIME_SAMPLE;
	if (sourceTime < mMotionStartTime) {
		sourceTime = mMotionStartTime;
	}

	idVec3 sourceLocal;
	idVec3 destLocal;
	EvaluatePosition(effect, pt, sourceLocal, sourceTime);
	EvaluatePosition(effect, pt, destLocal, time);

	const idVec3 sourceWorld = effect->GetCurrentOrigin() + effect->GetCurrentAxis() * sourceLocal;
	const idVec3 destWorld = effect->GetCurrentOrigin() + effect->GetCurrentAxis() * destLocal;

	idTraceModel* trm = NULL;
	if (pt->mTraceModelIndex >= 0) {
		trm = bse->GetTraceModel(pt->mTraceModelIndex);
	}
	// The game's shot mask: world solids plus render-model clip (actors, moveables, items).
	trace_t trace;
	idVec3 source = sourceWorld;
	idVec3 dest = destWorld;
	rvBSEManagerLocal::AddPerfCounter(PERF_NUM_TRACES);
	game->Translation(trace, source, dest, trm, CONTENTS_SOLID | CONTENTS_RENDERMODEL);
	if (trace.fraction >= 1.0f) {
		return false;
	}

	if (pt->mNumImpactEffects > 0 && bse->CanPlayRateLimited(EC_IMPACT_PARTICLES)) {
		idVec3 impactPos = trace.endpos;
		if (trm) {
			const idVec3 motion = (destWorld - sourceWorld) * trace.fraction;
			CalcImpactPoint(impactPos, trace.endpos, motion, trm->bounds, trace.c.normal);
		}

		// Impact effects only start while the owner is in an area connected to the player.
		if (effect->GetInConnectedArea()) {
			const rvDeclEffect* impactEffect = pt->mImpactEffects[rvRandom::irand(0, pt->mNumImpactEffects - 1)];
			if (impactEffect) {
				game->PlayEffect(impactEffect, impactPos, trace.c.normal.ToMat3(), false, vec3_origin, false, false, EC_IGNORE, vec4_one);
			}
		}
	}

	if (pt->mBounce != 0.0f) {
		Bounce(effect, pt, trace.endpos, trace.c.normal, time);
	}

	return pt->GetDeleteOnImpact();
}

void rvParticle::Bounce(rvBSE* effect, rvParticleTemplate* pt, idVec3 endPos, idVec3 normal, float time) {
	if (!effect || !pt) {
		return;
	}

	idVec3 velocity;
	EvaluateVelocity(effect, velocity, time);
	velocity = effect->GetCurrentAxis() * velocity;
	velocity -= normal * (2.0f * (velocity * normal));
	velocity *= pt->mBounce;

	// Motion restarts from the impact point, stored in the current effect frame whether
	// or not the particle is locked.
	mVelocity = effect->GetCurrentAxisTransposed() * velocity;
	mInitPos = effect->GetCurrentAxisTransposed() * (endPos - effect->GetCurrentOrigin());
	mMotionStartTime = time;

	// A slow bounce off a floor-like surface (normal against gravity) comes to rest.
	if (mVelocity.LengthSqr() < BSE_BOUNCE_LIMIT && normal * effect->GetGravityDir() < -idMath::SQRT_1OVER2) {
		SetStationary(true);
		mVelocity.Zero();
	}
}

void rvParticle::CheckTimeoutEffect(rvBSE* effect, rvSegmentTemplate* st, float time) {
	if (!effect || !st || !game) {
		return;
	}
	// Timeout effects only start while the owner is in an area connected to the player.
	rvParticleTemplate* pt = st->GetParticleTemplate();
	if (!pt || pt->GetNumTimeoutEffects() <= 0 || !effect->GetInConnectedArea()) {
		return;
	}

	const int idx = rvRandom::irand(0, pt->GetNumTimeoutEffects() - 1);
	const rvDeclEffect* timeoutEffect = pt->mTimeoutEffects[idx];
	if (!timeoutEffect) {
		return;
	}

	idVec3 position;
	idVec3 velocity;
	EvaluatePosition(effect, pt, position, time);
	EvaluateVelocity(effect, velocity, time);
	if (velocity.LengthSqr() > 1e-8f) {
		velocity.NormalizeFast();
	}
	else {
		velocity.Set(1.0f, 0.0f, 0.0f);
	}

	const idVec3 worldPos = effect->GetCurrentOrigin() + effect->GetCurrentAxis() * position;
	const idVec3 worldDir = effect->GetCurrentAxis() * velocity;
	game->PlayEffect(
		timeoutEffect,
		worldPos,
		worldDir.ToMat3(),
		false,
		vec3_origin,
		false,
		false,
		EC_IGNORE,
		vec4_one);
}

void rvParticle::CalcImpactPoint(idVec3& endPos, const idVec3& origin, const idVec3& motion, const idBounds& bounds, const idVec3& normal) {
	endPos = origin;

	if (motion.LengthSqr() <= 1e-8f || bounds.IsCleared()) {
		return;
	}

	idVec3 work = motion;
	const idVec3 size = bounds[1] - bounds[0];
	if (idMath::Fabs(size.x) > BSE_TIME_EPSILON) {
		work.x /= size.x;
	}
	if (idMath::Fabs(size.y) > BSE_TIME_EPSILON) {
		work.y /= size.y;
	}
	if (idMath::Fabs(size.z) > BSE_TIME_EPSILON) {
		work.z /= size.z;
	}

	if (work.LengthSqr() > 1e-8f) {
		work.NormalizeFast();
	}

	int axis = 0;
	const idVec3 absWork(idMath::Fabs(work.x), idMath::Fabs(work.y), idMath::Fabs(work.z));
	if (absWork.y >= absWork.x && absWork.y >= absWork.z) {
		axis = 1;
	}
	else if (absWork.z >= absWork.x && absWork.z >= absWork.y) {
		axis = 2;
	}

	const float dominant = Max(idMath::Fabs(work[axis]), 1e-6f);
	const float invLen = 0.5f / dominant;
	const idVec3 push((size.x * invLen) * work.x, (size.y * invLen) * work.y, (size.z * invLen) * work.z);
	endPos += normal + normal + push;
}

void rvParticle::EmitSmokeParticles(rvBSE* effect, rvSegment* child, rvParticleTemplate* pt, float time) {
	if (!effect || !child || !pt) {
		return;
	}

	rvSegmentTemplate* childTemplate = child->GetSegmentTemplate();
	if (!childTemplate) {
		return;
	}

	const float timeEnd = time + 0.016000001f;
	while (mLastTrailTime < timeEnd) {
		if (mLastTrailTime >= mStartTime && mLastTrailTime < mEndTime) {
			// Trail history is timed from the particle's birth, even after a bounce has
			// restarted its motion.
			const float sampleTime = mMotionStartTime + (mLastTrailTime - mStartTime);
			idVec3 position;
			idVec3 velocity;
			EvaluatePosition(effect, pt, position, sampleTime);
			EvaluateVelocity(effect, velocity, sampleTime);
			if (velocity.LengthSqr() > 1e-8f) {
				velocity.NormalizeFast();
			}
			else {
				velocity.Set(1.0f, 0.0f, 0.0f);
			}

			child->SpawnParticle(effect, childTemplate, mLastTrailTime, position, velocity.ToMat3());
		}

		const float interval = child->AttenuateInterval(effect, childTemplate);
		if (interval <= BSE_TIME_EPSILON) {
			break;
		}
		mLastTrailTime += interval;
	}
}

// ---------------------------------------------------------------------------
//  render helpers
// ---------------------------------------------------------------------------
dword rvParticle::HandleTint(const rvBSE* effect, idVec4& colour, float alpha) {
	// Additive particles fade through RGB and stay opaque, ignoring the owner's alpha;
	// the rest fade through alpha. Brightness only scales RGB.
	const float bright = effect->GetBrightness();
	const float rgbScale = GetAdditive() ? colour[3] * alpha * bright : bright;
	const float outAlpha = GetAdditive() ? 1.0f : effect->GetAlpha() * colour[3] * alpha;
	return PackColorLocal(idVec4(
		effect->GetRed() * colour[0] * rgbScale,
		effect->GetGreen() * colour[1] * rgbScale,
		effect->GetBlue() * colour[2] * rgbScale,
		outAlpha));
}

void rvParticle::RenderQuadTrail(const rvBSE* effect, srfTriangles_t* tri, idVec3 offset, float fraction, idVec4& colour, idVec3& pos, bool first) {
	if (!tri) {
		return;
	}
	if (tri->numAllocedVerts > 0 && tri->numVerts + 2 > tri->numAllocedVerts) {
		return;
	}
	if (!first && tri->numAllocedIndices > 0 && tri->numIndexes + 6 > tri->numAllocedIndices) {
		return;
	}

	const dword rgba = HandleTint(effect, colour, 1.0f);
	const int base = tri->numVerts;
	SetDrawVert(tri->verts[base + 0], pos + offset, 0.0f, fraction, rgba);
	SetDrawVert(tri->verts[base + 1], pos - offset, 1.0f, fraction, rgba);

	if (!first) {
		const int indexBase = tri->numIndexes;
		tri->indexes[indexBase + 0] = base - 2;
		tri->indexes[indexBase + 1] = base - 1;
		tri->indexes[indexBase + 2] = base + 0;
		tri->indexes[indexBase + 3] = base - 1;
		tri->indexes[indexBase + 4] = base + 0;
		tri->indexes[indexBase + 5] = base + 1;
		tri->numIndexes += 6;
	}

	tri->numVerts += 2;
}

void rvParticle::RenderMotion(rvBSE* effect, rvParticleTemplate* pt, srfTriangles_t* tri, const renderEffect_s* owner, float time) {
	if (!effect || !pt || !tri || !owner) {
		return;
	}
	if (mTrailCount <= 0) {
		return;
	}

	const float startTime = Max(time - mTrailTime, mStartTime);
	const float delta = time - startTime;
	if (delta <= BSE_TIME_EPSILON) {
		return;
	}

	const float evalTime = Max(0.0f, time - mStartTime);
	idVec4 color;
	EvaluateTint(pt->mpTintEnvelope, pt->mpFadeEnvelope, evalTime, mOneOverDuration, color);

	idVec3 size(1.0f, 1.0f, 1.0f);
	EvaluateSize(pt->mpSizeEnvelope, evalTime, mOneOverDuration, size.ToFloatPtr());
	const float width = size.x;

	idVec3 position;
	EvaluatePosition(effect, pt, position, time);

	const idMat3 ownerAxisTranspose = owner->axis.Transpose();
	const idVec3 localView = ownerAxisTranspose * (effect->GetViewOrg() - owner->origin);
	const idVec3 toView = localView - mInitPos;
	idVec3 motionDir = mVelocity.Cross(toView);
	if (motionDir.LengthSqr() <= 1e-6f) {
		// Vanilla fallback uses world up in owner-local space when velocity and view vectors align.
		motionDir = idVec3(0.0f, 1.0f, 0.0f).Cross(toView);
	}
	if (motionDir.LengthSqr() > 1e-6f) {
		motionDir.NormalizeFast();
	}
	const idVec3 halfWidth = motionDir * (width * 0.5f);

	for (int segment = 0; segment < mTrailCount; ++segment) {
		const float t = static_cast<float>(segment) / static_cast<float>(mTrailCount);
		idVec4 segmentColor = color;
		segmentColor.w *= (1.0f - t);
		RenderQuadTrail(effect, tri, halfWidth, t, segmentColor, position, segment == 0);

		const float sampleTime = time - t * delta;
		EvaluatePosition(effect, pt, position, sampleTime);
	}

	idVec4 endColor = color;
	endColor.w = 0.0f;
	RenderQuadTrail(effect, tri, halfWidth, 1.0f, endColor, position, false);
}

void rvParticle::DoRenderBurnTrail(rvBSE* effect, rvParticleTemplate* pt, const idMat3& view, srfTriangles_t* tri, float time) {
	if (mTrailCount <= 0 || mTrailTime <= 0.0f) {
		return;
	}

	const float delta = mTrailTime / Max(1, mTrailCount);
	for (int i = 1; i <= mTrailCount; ++i) {
		const float trailTime = time - static_cast<float>(i) * delta;
		if (trailTime < mStartTime || trailTime >= mEndTime) {
			continue;
		}
		const float fade = static_cast<float>(mTrailCount - i) / Max(1, mTrailCount);
		Render(effect, pt, view, tri, trailTime, fade);
	}
}

// ---------------------------------------------------------------------------
//  per-type spawn data
// ---------------------------------------------------------------------------
void rvSpriteParticle::GetSpawnInfo(idVec4& tint, idVec3& size, idVec3& rotate) {
	const float* tintStart = mTintEnv.GetStart();
	tint.Set(tintStart[0], tintStart[1], tintStart[2], mFadeEnv.GetStart()[0]);
	const float* sizeStart = mSizeEnv.GetStart();
	size.Set(sizeStart[0], sizeStart[1], 0.0f);
	rotate.Set(mRotationEnv.GetStart()[0], 0.0f, 0.0f);
}

void rvLineParticle::GetSpawnInfo(idVec4& tint, idVec3& size, idVec3& rotate) {
	const float* tintStart = mTintEnv.GetStart();
	tint.Set(tintStart[0], tintStart[1], tintStart[2], mFadeEnv.GetStart()[0]);
	size.Set(mSizeEnv.GetStart()[0], 0.0f, 0.0f);
	rotate.Zero();
}

void rvOrientedParticle::GetSpawnInfo(idVec4& tint, idVec3& size, idVec3& rotate) {
	const float* tintStart = mTintEnv.GetStart();
	tint.Set(tintStart[0], tintStart[1], tintStart[2], mFadeEnv.GetStart()[0]);
	const float* sizeStart = mSizeEnv.GetStart();
	size.Set(sizeStart[0], sizeStart[1], 0.0f);
	rotate.Set(mRotationEnv.GetStart()[0], mRotationEnv.GetStart()[1], mRotationEnv.GetStart()[2]);
}

void rvModelParticle::GetSpawnInfo(idVec4& tint, idVec3& size, idVec3& rotate) {
	const float* tintStart = mTintEnv.GetStart();
	tint.Set(tintStart[0], tintStart[1], tintStart[2], mFadeEnv.GetStart()[0]);
	size.Set(mSizeEnv.GetStart()[0], mSizeEnv.GetStart()[1], mSizeEnv.GetStart()[2]);
	rotate.Set(mRotationEnv.GetStart()[0], mRotationEnv.GetStart()[1], mRotationEnv.GetStart()[2]);
}

void rvLightParticle::GetSpawnInfo(idVec4& tint, idVec3& size, idVec3& rotate) {
	const float* tintStart = mTintEnv.GetStart();
	tint.Set(tintStart[0], tintStart[1], tintStart[2], mFadeEnv.GetStart()[0]);
	size.Set(mSizeEnv.GetStart()[0], mSizeEnv.GetStart()[1], mSizeEnv.GetStart()[2]);
	rotate.Zero();
}

void rvDecalParticle::GetSpawnInfo(idVec4& tint, idVec3& size, idVec3& rotate) {
	const float* tintStart = mTintEnv.GetStart();
	tint.Set(tintStart[0], tintStart[1], tintStart[2], mFadeEnv.GetStart()[0]);
	const float* sizeStart = mSizeEnv.GetStart();
	size.Set(sizeStart[0], sizeStart[1], 0.0f);
	rotate.Set(mRotationEnv.GetStart()[0], 0.0f, 0.0f);
}

// ---------------------------------------------------------------------------
//  per-type render
// ---------------------------------------------------------------------------
bool rvSpriteParticle::Render(const rvBSE* effect, rvParticleTemplate* pt, const idMat3& view, srfTriangles_t* tri, float time, float override) {
	if (!effect || !pt || !tri) {
		return false;
	}

	float evalTime;
	if (!GetEvaluationTime(time, evalTime, false)) {
		BSETraceRenderDrop("sprite", this, time, "eval", evalTime, GetEndTime() - time);
		return false;
	}

	idVec3 pos;
	EvaluatePosition(effect, pt, pos, time);

	idVec4 color;
	EvaluateTint(pt->mpTintEnvelope, pt->mpFadeEnvelope, evalTime, mOneOverDuration, color);

	float size[2] = { 1.0f, 1.0f };
	EvaluateSize(pt->mpSizeEnvelope, evalTime, mOneOverDuration, size);
	float rotation = 0.0f;
	EvaluateRotation(pt->mpRotateEnvelope, evalTime, mOneOverDuration, &rotation);

	float s, c;
	idMath::SinCos(rotation, s, c);
	idVec3 right = (view[1] * c - view[2] * s) * size[0];
	idVec3 up = (view[1] * s + view[2] * c) * size[1];

	// No alpha test: a fully faded particle still emits its quad (and so its burn trail).
	dword rgba = HandleTint(effect, color, override);
	AppendQuad(
		tri,
		pos - right,
		pos - up,
		pos + right,
		pos + up,
		rgba,
		&pos);
	return true;
}

bool rvLineParticle::Render(const rvBSE* effect, rvParticleTemplate* pt, const idMat3& view, srfTriangles_t* tri, float time, float override) {
	if (!effect || !pt || !tri) {
		return false;
	}

	float evalTime;
	if (!GetEvaluationTime(time, evalTime, false)) {
		BSETraceRenderDrop("line", this, time, "eval", evalTime, GetEndTime() - time);
		return false;
	}

	idVec3 pos;
	EvaluatePosition(effect, pt, pos, time);

	idVec4 color;
	EvaluateTint(pt->mpTintEnvelope, pt->mpFadeEnvelope, evalTime, mOneOverDuration, color);

	float width = 1.0f;
	EvaluateSize(pt->mpSizeEnvelope, evalTime, mOneOverDuration, &width);

	idVec3 length(0.0f, 0.0f, 1.0f);
	EvaluateLength(pt->mpLengthEnvelope, evalTime, mOneOverDuration, length);
	if (!GetLocked()) {
		const idMat3 initToCurrent = BuildInitToCurrentAxis(effect, mInitAxis);
		length = initToCurrent * length;
	}
	if (GetGeneratedLine()) {
		// The length is laid along the direction of travel; without motion it collapses.
		idVec3 velocity;
		EvaluateVelocity(effect, velocity, time);
		velocity.Normalize();
		length = velocity * length.Length();
	}

	const idVec3 end = pos + length;
	const idVec3 toView = view[0] - (pos + length * 0.5f);
	idVec3 side = length.Cross(toView);
	float sideLenSqr = side.LengthSqr();
	if (sideLenSqr > 1e-8f) {
		side *= idMath::InvSqrt(sideLenSqr);
	}
	side *= width;

	dword rgba = HandleTint(effect, color, override);
	const int base = tri->numVerts;
	SetDrawVert(tri->verts[base + 0], pos + side, 0.0f, 0.0f, rgba);
	SetDrawVert(tri->verts[base + 1], pos - side, 0.0f, 1.0f, rgba);
	SetDrawVert(tri->verts[base + 2], end - side, mTextureScale, 1.0f, rgba);
	SetDrawVert(tri->verts[base + 3], end + side, mTextureScale, 0.0f, rgba);

	tri->verts[base + 0].normal = pos;
	tri->verts[base + 1].normal = pos;
	tri->verts[base + 2].normal = pos;
	tri->verts[base + 3].normal = pos;

	const int indexBase = tri->numIndexes;
	tri->indexes[indexBase + 0] = base + 0;
	tri->indexes[indexBase + 1] = base + 1;
	tri->indexes[indexBase + 2] = base + 2;
	tri->indexes[indexBase + 3] = base + 0;
	tri->indexes[indexBase + 4] = base + 2;
	tri->indexes[indexBase + 5] = base + 3;
	tri->numVerts += 4;
	tri->numIndexes += 6;
	return true;
}

bool rvLinkedParticle::Render(const rvBSE* effect, rvParticleTemplate* pt, const idMat3& view, srfTriangles_t* tri, float time, float override) {
	if (!effect || !pt || !tri) {
		return false;
	}

	float evalTime;
	if (!GetEvaluationTime(time, evalTime, false)) {
		return false;
	}

	idVec3 pos;
	EvaluatePosition(effect, pt, pos, time);

	idVec4 color;
	EvaluateTint(pt->mpTintEnvelope, pt->mpFadeEnvelope, evalTime, mOneOverDuration, color);

	float size = 1.0f;
	EvaluateSize(pt->mpSizeEnvelope, evalTime, mOneOverDuration, &size);

	idVec3 up = view[1] * size;
	dword rgba = HandleTint(effect, color, override);

	const int base = tri->numVerts;
	SetDrawVert(tri->verts[base + 0], pos + up, mFraction * mTextureScale, 0.0f, rgba);
	SetDrawVert(tri->verts[base + 1], pos - up, mFraction * mTextureScale, 1.0f, rgba);
	tri->verts[base + 0].normal = pos;
	tri->verts[base + 1].normal = pos;
	if (base > 0) {
		const int indexBase = tri->numIndexes;
		tri->indexes[indexBase + 0] = base - 2;
		tri->indexes[indexBase + 1] = base - 1;
		tri->indexes[indexBase + 2] = base + 0;
		tri->indexes[indexBase + 3] = base - 1;
		tri->indexes[indexBase + 4] = base + 1;
		tri->indexes[indexBase + 5] = base + 0;
		tri->numIndexes += 6;
	}
	tri->numVerts += 2;
	return true;
}

bool sdOrientedLinkedParticle::Render(const rvBSE* effect, rvParticleTemplate* pt, const idMat3& view, srfTriangles_t* tri, float time, float override) {
	return rvLinkedParticle::Render(effect, pt, view, tri, time, override);
}

bool rvOrientedParticle::Render(const rvBSE* effect, rvParticleTemplate* pt, const idMat3& view, srfTriangles_t* tri, float time, float override) {
	if (!effect || !pt || !tri) {
		return false;
	}

	float evalTime;
	if (!GetEvaluationTime(time, evalTime, false)) {
		BSETraceRenderDrop("oriented", this, time, "eval", evalTime, GetEndTime() - time);
		return false;
	}

	idVec3 position;
	EvaluatePosition(effect, pt, position, time);

	idVec4 tint;
	EvaluateTint(pt->mpTintEnvelope, pt->mpFadeEnvelope, evalTime, mOneOverDuration, tint);

	float size[2] = { 1.0f, 1.0f };
	float rotation[3] = { 0.0f, 0.0f, 0.0f };
	EvaluateSize(pt->mpSizeEnvelope, evalTime, mOneOverDuration, size);
	EvaluateRotation(pt->mpRotateEnvelope, evalTime, mOneOverDuration, rotation);

	idMat3 transform;
	rvAngles(rotation[0], rotation[1], rotation[2]).ToMat3(transform);
	const idVec3 right = transform[1] * -size[0];
	const idVec3 up = transform[2] * size[1];

	dword rgba = HandleTint(effect, tint, override);
	AppendQuad(
		tri,
		position - right,
		position - up,
		position + right,
		position + up,
		rgba,
		&position);
	return true;
}

bool rvModelParticle::Render(const rvBSE* effect, rvParticleTemplate* pt, const idMat3& view, srfTriangles_t* tri, float time, float override) {
	if (!effect || !pt || !tri || !mModel || mModel->NumSurfaces() <= 0) {
		return false;
	}

	float evalTime;
	if (!GetEvaluationTime(time, evalTime, false)) {
		return false;
	}

	idVec3 position;
	EvaluatePosition(effect, pt, position, time);

	idVec4 color;
	EvaluateTint(pt->mpTintEnvelope, pt->mpFadeEnvelope, evalTime, mOneOverDuration, color);

	float size[3] = { 1.0f, 1.0f, 1.0f };
	EvaluateSize(pt->mpSizeEnvelope, evalTime, mOneOverDuration, size);

	float rotation[3] = { 0.0f, 0.0f, 0.0f };
	EvaluateRotation(pt->mpRotateEnvelope, evalTime, mOneOverDuration, rotation);

	const modelSurface_t* surf = mModel->Surface(0);
	if (!surf || !surf->geometry) {
		return false;
	}

	const srfTriangles_t* src = surf->geometry;
	const int baseVert = tri->numVerts;
	const int baseIndex = tri->numIndexes;
	const dword rgba = HandleTint(effect, color, override);
	byte rgbaBytes[4];
	UnpackColor(rgba, rgbaBytes);

	idMat3 rotationMat;
	rvAngles(rotation[0], rotation[1], rotation[2]).ToMat3(rotationMat);
	idMat3 transform = rotationMat;
	if (!GetLocked()) {
		const idMat3 initToCurrent = BuildInitToCurrentAxis(effect, mInitAxis);
		transform = transform * initToCurrent;
	}

	for (int i = 0; i < src->numVerts; ++i) {
		idDrawVert& dst = tri->verts[baseVert + i];
		dst = src->verts[i];

		idVec3 p = transform * src->verts[i].xyz;
		p.x *= size[0];
		p.y *= size[1];
		p.z *= size[2];
		dst.xyz = position + p;

		// Retail rvModelParticle::Render (0x101c8d90) transforms only xyz and
		// keeps the model's normal and tangents as copied above.
		dst.color[0] = rgbaBytes[0];
		dst.color[1] = rgbaBytes[1];
		dst.color[2] = rgbaBytes[2];
		dst.color[3] = rgbaBytes[3];
	}

	for (int i = 0; i < src->numIndexes; ++i) {
		tri->indexes[baseIndex + i] = baseVert + src->indexes[i];
	}

	tri->numVerts += src->numVerts;
	tri->numIndexes += src->numIndexes;
	return true;
}

int rvElectricityParticle::GetBoltCount(float length) {
	const int bolts = static_cast<int>(ceilf(length * 0.0625f));
	return idMath::ClampInt(3, static_cast<int>(BSE_ELEC_MAX_BOLTS), bolts);
}

static const idDeclTable* ResolveElectricityJitterTable(const idStr& tableName, const idDeclTable* fallbackTable) {
	const idDeclTable* table = NULL;

	if (!tableName.IsEmpty()) {
		table = declManager->FindTable(tableName, false);
		if (table != NULL && table->IsImplicit()) {
			table = NULL;
		}
	}

	if (table == NULL) {
		table = fallbackTable;
		if (table != NULL && table->IsImplicit()) {
			table = NULL;
		}
	}

	if (table == NULL) {
		table = declManager->FindTable("halfsintable", false);
	}

	return table;
}

void rvElectricityParticle::RenderBranch(const rvBSE* effect, struct SElecWork* work, idVec3 start, idVec3 end, const idDeclTable* jitterTable) {
	if (!effect || !work || !work->tri || !work->coords) {
		return;
	}

	const idDeclTable* resolvedJitterTable = jitterTable ? jitterTable : mJitterTable;

	work->forward.Normalize();
	const idVec3 left = BoltLeft(work->forward);
	const idVec3 down = left.Cross(work->forward);

	const int segmentVertStart = work->tri->numVerts;
	float fraction = work->step;
	idVec3 old = start;
	idVec3 current = start;
	idVec3 accumulatedOffset(vec3_origin);
	int pointCount = 0;
	bool evaluate = true;

	while (true) {
		if (pointCount >= 255) {
			break;
		}
		work->coords[pointCount++] = old;

		if (1.0f - work->step * 0.5f <= fraction) {
			fraction = 1.0f;
			evaluate = false;
		}

		accumulatedOffset += work->forward * rvRandom::flrand(-mJitterSize.x, mJitterSize.x);
		accumulatedOffset += left * rvRandom::flrand(-mJitterSize.y, mJitterSize.y);
		accumulatedOffset += down * rvRandom::flrand(-mJitterSize.z, mJitterSize.z);

		const float noise = resolvedJitterTable ? resolvedJitterTable->TableLookup(fraction) : 0.0f;
		current = start + (end - start) * fraction + accumulatedOffset * noise;

		work->fraction = fraction - work->step;
		ApplyShape(effect, work, old, current, 2, 0.0f, 1.0f);

		old = current;
		fraction += work->step;
		if (!evaluate) {
			break;
		}
	}

	if (segmentVertStart != work->tri->numVerts) {
		if (pointCount < 256) {
			work->coords[pointCount] = current;
			work->coordCount = pointCount + 1;
		}
		else {
			work->coordCount = pointCount;
		}

		RenderLineSegment(effect, work, current, 1.0f);

		// Vertices come in (+side, -side) pairs; each quad splits along base..base+3.
		for (int base = segmentVertStart; base < work->tri->numVerts - 2; base += 2) {
			if (!HasTriCapacity(work->tri, 0, 6)) {
				break;
			}

			const int indexBase = work->tri->numIndexes;
			work->tri->indexes[indexBase + 0] = base;
			work->tri->indexes[indexBase + 1] = base + 1;
			work->tri->indexes[indexBase + 2] = base + 3;
			work->tri->indexes[indexBase + 3] = base;
			work->tri->indexes[indexBase + 4] = base + 3;
			work->tri->indexes[indexBase + 5] = base + 2;
			work->tri->numIndexes += 6;
		}
	}
	else {
		work->coordCount = 0;
	}
}

bool rvElectricityParticle::RenderLineSegment(const rvBSE* effect, struct SElecWork* work, idVec3 start, float startFraction) {
	if (!effect || !work || !work->tri) {
		return false;
	}
	if (!HasTriCapacity(work->tri, 2, 0)) {
		return false;
	}

	idVec3 offset = work->length.Cross(work->viewPos);
	const float len2 = offset.LengthSqr();
	if (len2 != 0.0f) {
		offset *= idMath::InvSqrt(len2);
	}
	offset *= work->size;

	const dword color = HandleTint(effect, work->tint, work->alpha);
	const float s = startFraction * work->step + work->fraction;
	const int baseVert = work->tri->numVerts;
	SetDrawVert(work->tri->verts[baseVert + 0], start + offset, s, 0.0f, color);
	SetDrawVert(work->tri->verts[baseVert + 1], start - offset, s, 1.0f, color);
	work->tri->numVerts += 2;
	return true;
}

void rvElectricityParticle::ApplyShape(const rvBSE* effect, struct SElecWork* work, idVec3 start, idVec3 end, int count, float startFraction, float endFraction) {
	if (!effect || !work) {
		return;
	}

	// Each pass kinks the segment at a point about a third of the way along, pushed out
	// sideways and down, and at one about two thirds along, pushed back by roughly the
	// same amount, so the bolt zigzags evenly about its line.
	while (count >= 1) {
		const float down1 = rvRandom::flrand(0.05f, 0.09f);
		const float left1 = rvRandom::flrand(0.05f, 0.09f);
		const float weight1 = rvRandom::flrand(0.56f, 0.76f);
		const float down2 = rvRandom::flrand(-down1 - 0.02f, 0.02f - down1);
		const float left2 = rvRandom::flrand(-left1 - 0.02f, 0.02f - left1);
		const float weight2 = rvRandom::flrand(0.23f, 0.43f);

		idVec3 forward = end - start;
		const float length = forward.Normalize() * 0.7f;
		const idVec3 left = BoltLeft(forward);
		const idVec3 down = left.Cross(forward);

		const idVec3 point1 = start * weight1 + end * (1.0f - weight1) + left * (left1 * length) + down * (down1 * length);
		const idVec3 point2 = start * weight2 + end * (1.0f - weight2) + left * (left2 * length) + down * (down2 * length);

		const float mid0 = startFraction * 0.6666667f + endFraction * 0.3333333f;
		const float mid1 = startFraction * 0.3333333f + endFraction * 0.6666667f;

		ApplyShape(effect, work, start, point1, count - 1, startFraction, mid0);
		ApplyShape(effect, work, point1, point2, count - 1, mid0, mid1);

		--count;
		start = point2;
		startFraction = mid1;
	}

	RenderLineSegment(effect, work, start, startFraction);
}

int rvElectricityParticle::Update(rvParticleTemplate* pt, float time) {
	if (!pt || !pt->mpLengthEnvelope) {
		mNumBolts = 0;
		return 0;
	}

	const float evalTime = Max(0.0f, time - mStartTime);
	idVec3 length;
	EvaluateLength(pt->mpLengthEnvelope, evalTime, mOneOverDuration, length);
	mNumBolts = GetBoltCount(length.LengthFast());
	return mNumBolts;
}

bool rvElectricityParticle::Render(const rvBSE* effect, rvParticleTemplate* pt, const idMat3& view, srfTriangles_t* tri, float time, float override) {
	if (!effect || !pt || !tri) {
		return false;
	}

	float evalTime;
	if (!GetEvaluationTime(time, evalTime, false)) {
		return false;
	}

	idVec4 tint;
	EvaluateTint(pt->mpTintEnvelope, pt->mpFadeEnvelope, evalTime, mOneOverDuration, tint);

	float width = 1.0f;
	EvaluateSize(pt->mpSizeEnvelope, evalTime, mOneOverDuration, &width);
	idVec3 length;
	EvaluateLength(pt->mpLengthEnvelope, evalTime, mOneOverDuration, length);

	idVec3 position;
	EvaluatePosition(effect, pt, position, time);

	if (!GetLocked()) {
		const idMat3 initToCurrent = BuildInitToCurrentAxis(effect, mInitAxis);
		length = initToCurrent * length;
	}

	if (GetGeneratedLine()) {
		const float generatedLength = length.LengthFast();
		idVec3 velocity;
		EvaluateVelocity(effect, velocity, time);
		if (velocity.LengthSqr() != 0.0f) {
			velocity.NormalizeFast();
		}
		else {
			velocity.Zero();
		}
		length = velocity * generatedLength;
	}

	const float mainLength = length.LengthFast();
	if (mainLength < 0.1f) {
		return false;
	}

	if (mLastJitter + mJitterRate <= time) {
		mLastJitter = time;
		mSeed = rvRandom::Init();
	}

	const unsigned int previousSeed = rvRandom::GetSeed();
	rvRandom::Init(mSeed);

	const int boltCount = Max(1, (mNumBolts > 0) ? mNumBolts : GetBoltCount(mainLength));
	mNumBolts = boltCount;

	SElecWork work;
	memset(&work, 0, sizeof(work));
	idVec3 tmpCoords[256];
	work.tri = tri;
	work.coords = tmpCoords;
	work.coordCount = 0;
	work.tint = tint;
	work.size = width;
	work.alpha = override;
	work.length = length;
	work.forward = length;
	work.viewPos = view[0];
	work.step = mTextureScale / static_cast<float>(boltCount);
	if (work.step <= BSE_TIME_EPSILON) {
		work.step = 1.0f / static_cast<float>(boltCount);
	}

	const idVec3 endPos = position + length;
	const idDeclTable* jitterTable = (pt->mElecInfo != NULL)
		? ResolveElectricityJitterTable(pt->mElecInfo->mJitterTableName, mJitterTable)
		: ResolveElectricityJitterTable(idStr(), mJitterTable);
	mJitterTable = jitterTable;
	RenderBranch(effect, &work, position, endPos, jitterTable);

	idVec3 forkBases[BSE_MAX_FORKS];
	const int forks = idMath::ClampInt(0, BSE_MAX_FORKS, mNumForks);
	for (int i = 0; i < forks; ++i) {
		if (work.coordCount > 2) {
			const int idx = rvRandom::irand(1, work.coordCount - 2);
			forkBases[i] = work.coords[idx];
		}
		else {
			forkBases[i] = position;
		}
	}

	for (int i = 0; i < forks; ++i) {
		const idVec3 mid = (forkBases[i] + endPos) * 0.5f;
		const idVec3 forkEnd(
			mid.x + rvRandom::flrand(mForkSizeMins.x, mForkSizeMaxs.x),
			mid.y + rvRandom::flrand(mForkSizeMins.y, mForkSizeMaxs.y),
			mid.z + rvRandom::flrand(mForkSizeMins.z, mForkSizeMaxs.z));

		const idVec3 dir = forkEnd - forkBases[i];
		const float forkLength = dir.LengthFast();
		if (forkLength <= 1.0f || forkLength >= mainLength) {
			continue;
		}

		// Forks keep the main bolt's length, so their ribbons share its width direction.
		work.forward = dir;
		work.step = 1.0f / static_cast<float>(GetBoltCount(forkLength));
		RenderBranch(effect, &work, forkBases[i], forkEnd, jitterTable);
	}

	rvRandom::Init(previousSeed);
	return true;
}

void rvElectricityParticle::SetupElectricity(rvParticleTemplate* pt) {
	if (!pt || !pt->mElecInfo) {
		mNumBolts = 0;
		mNumForks = 0;
		mSeed = 0;
		mForkSizeMins.Zero();
		mForkSizeMaxs.Zero();
		mJitterSize.Zero();
		mLastJitter = 0.0f;
		mJitterRate = 0.0f;
		mJitterTable = NULL;
		return;
	}

	const rvElectricityInfo* info = pt->mElecInfo;
	mNumBolts = 0;
	mNumForks = info->mNumForks;
	mSeed = rvRandom::Init();
	mForkSizeMins = info->mForkSizeMins;
	mForkSizeMaxs = info->mForkSizeMaxs;
	mJitterSize = info->mJitterSize;
	mLastJitter = 0.0f;
	mJitterRate = info->mJitterRate;

	mJitterTable = ResolveElectricityJitterTable(info->mJitterTableName, info->mJitterTable);
}

bool rvLightParticle::InitLight(rvBSE* effect, rvSegmentTemplate* st, float time) {
	idRenderWorld* renderWorld = effect ? effect->GetRenderWorld() : NULL;
	if (!renderWorld && session) {
		renderWorld = session->rw;
	}
	if (!effect || !st || !renderWorld) {
		return false;
	}

	rvParticleTemplate* pt = st->GetParticleTemplate();
	if (!pt) {
		return false;
	}

	float evalTime;
	if (!GetEvaluationTime(time, evalTime, false)) {
		return false;
	}

	idVec4 tint;
	idVec3 size;
	idVec3 position;
	EvaluateTint(pt->mpTintEnvelope, pt->mpFadeEnvelope, evalTime, mOneOverDuration, tint);
	EvaluateSize(pt->mpSizeEnvelope, evalTime, mOneOverDuration, size.ToFloatPtr());
	EvaluatePosition(effect, pt, position, time);

	// The light colour is the raw tint: fade and the owner's colour and brightness are not
	// applied, and the alpha parm stays zero. A missing material uses the default light.
	memset(&mLight, 0, sizeof(mLight));
	mLight.origin = effect->GetCurrentOrigin() + effect->GetCurrentAxis() * position;
	mLight.lightRadius.x = Max(1.0f, size.x);
	mLight.lightRadius.y = Max(1.0f, size.y);
	mLight.lightRadius.z = Max(1.0f, size.z);
	mLight.axis = effect->GetCurrentAxis();
	mLight.shaderParms[ SHADERPARM_RED ] = tint.x;
	mLight.shaderParms[ SHADERPARM_GREEN ] = tint.y;
	mLight.shaderParms[ SHADERPARM_BLUE ] = tint.z;
	mLight.pointLight = true;
	mLight.detailLevel = 10.0f;
	mLight.noShadows = !pt->GetShadows();
	mLight.noSpecular = !pt->GetSpecular();
	mLight.shader = pt->GetMaterial();

	mLightDefHandle = renderWorld->AddLightDef(&mLight);
	mLightRenderWorld = renderWorld;
	return mLightDefHandle != -1;
}

bool rvLightParticle::PresentLight(rvBSE* effect, rvParticleTemplate* pt, float time, bool infinite) {
	// A light that InitLight could not register is not added again here.
	if (mLightDefHandle == -1 || !mLightRenderWorld || !effect || !pt) {
		return false;
	}

	float evalTime;
	if (!GetEvaluationTime(time, evalTime, infinite)) {
		return false;
	}

	idVec4 tint;
	idVec3 size;
	idVec3 position;
	EvaluateTint(pt->mpTintEnvelope, pt->mpFadeEnvelope, evalTime, mOneOverDuration, tint);
	EvaluateSize(pt->mpSizeEnvelope, evalTime, mOneOverDuration, size.ToFloatPtr());
	EvaluatePosition(effect, pt, position, time);

	// Same raw-tint colour as InitLight.
	mLight.origin = effect->GetCurrentOrigin() + effect->GetCurrentAxis() * position;
	mLight.lightRadius.x = Max(1.0f, size.x);
	mLight.lightRadius.y = Max(1.0f, size.y);
	mLight.lightRadius.z = Max(1.0f, size.z);
	mLight.axis = effect->GetCurrentAxis();
	mLight.shaderParms[ SHADERPARM_RED ] = tint.x;
	mLight.shaderParms[ SHADERPARM_GREEN ] = tint.y;
	mLight.shaderParms[ SHADERPARM_BLUE ] = tint.z;
	mLightRenderWorld->UpdateLightDef(mLightDefHandle, &mLight);
	return true;
}

bool rvLightParticle::Destroy(void) {
	idRenderWorld* renderWorld = mLightRenderWorld;
	if (!renderWorld && session) {
		renderWorld = session->rw;
	}
	if (mLightDefHandle != -1 && renderWorld) {
		renderWorld->FreeLightDef(mLightDefHandle);
	}
	mLightDefHandle = -1;
	mLightRenderWorld = NULL;
	memset(&mLight, 0, sizeof(mLight));
	return true;
}
