// Copyright (C) 2007 Id Software, Inc.
//

#include "BSE_Envelope.h"
#include "BSE_Particle.h"
#include "BSE_SpawnDomains.h"
#include "BSE.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../framework/Session.h"

extern idCVar bse_frameCounters;

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
ID_INLINE float Clamp01(float x) {
	return idMath::ClampFloat(0.0f, 1.0f, x);
}

ID_INLINE byte ToByte(float x) {
	return static_cast<byte>(idMath::ClampInt(0, 255, idMath::FtoiFast(Clamp01(x) * 255.0f)));
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

ID_INLINE void SetDrawVert(idDrawVert& vert, const idVec3& xyz, float s, float t, dword rgba) {
	byte color[4];
	UnpackColor(rgba, color);

	vert.Clear();
	vert.xyz = xyz;
	vert.st[0] = s;
	vert.st[1] = t;
	vert.normal.Set(1.0f, 0.0f, 0.0f);
	vert.tangents[0].Set(0.0f, 1.0f, 0.0f);
	vert.tangents[1].Set(0.0f, 0.0f, 1.0f);
	vert.color[0] = color[0];
	vert.color[1] = color[1];
	vert.color[2] = color[2];
	vert.color[3] = color[3];
}

ID_INLINE void AppendQuad(srfTriangles_t* tri, const idVec3& p0, const idVec3& p1, const idVec3& p2, const idVec3& p3, dword rgba) {
	const int base = tri->numVerts;
	SetDrawVert(tri->verts[base + 0], p0, 0.0f, 0.0f, rgba);
	SetDrawVert(tri->verts[base + 1], p1, 1.0f, 0.0f, rgba);
	SetDrawVert(tri->verts[base + 2], p2, 1.0f, 1.0f, rgba);
	SetDrawVert(tri->verts[base + 3], p3, 0.0f, 1.0f, rgba);

	const int indexBase = tri->numIndexes;
	tri->indexes[indexBase + 0] = base + 0;
	tri->indexes[indexBase + 1] = base + 1;
	tri->indexes[indexBase + 2] = base + 3;
	tri->indexes[indexBase + 3] = base + 1;
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

ID_INLINE bool IsScalarDomain(const rvParticleParms* parms) {
	if (!parms) {
		return false;
	}
	return (parms->mSpawnType & 0x3) == 1;
}

ID_INLINE void BuildPerpBasis(const idVec3& forward, idVec3& right, idVec3& up) {
	if (idMath::Fabs(forward.x) < 0.99f) {
		right = idVec3(0.0f, 0.0f, 1.0f).Cross(forward);
	}
	else {
		right = idVec3(0.0f, 1.0f, 0.0f).Cross(forward);
	}

	if (right.LengthSqr() > 1e-8f) {
		right.NormalizeFast();
	}
	else {
		right.Set(0.0f, 1.0f, 0.0f);
	}

	up = forward.Cross(right);
	if (up.LengthSqr() > 1e-8f) {
		up.NormalizeFast();
	}
	else {
		up.Set(0.0f, 0.0f, 1.0f);
	}
}

ID_INLINE void BSETraceRenderDrop(const char* typeName, const rvParticle* particle, float time, const char* reason, float value0 = 0.0f, float value1 = 0.0f) {
	static int bseRenderDropTraceCount = 0;
	if (!particle || bse_frameCounters.GetInteger() < 2 || bseRenderDropTraceCount >= 256) {
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
	if (!pt || !GetTiled()) {
		return;
	}
	const float* len = GetInitLength();
	if (!len) {
		mTextureScale = 1.0f;
		return;
	}
	const idVec3 length(len[0], len[1], len[2]);
	mTextureScale = Max(0.001f, length.LengthFast() / Max(0.001f, pt->GetTiling()));
}

void rvLinkedParticle::HandleTiling(rvParticleTemplate* pt) {
	if (!pt || !GetTiled()) {
		return;
	}
	mTextureScale = Max(0.001f, pt->GetTiling());
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
	if (!effect || !pt || !pt->mpSpawnPosition) {
		mInitPos.Zero();
		return;
	}

	pt->mpSpawnPosition->Spawn(mInitPos.ToFloatPtr(), *pt->mpSpawnPosition, normal, centre);

	if (!effect->GetHasEndOrigin()) {
		return;
	}

	const idVec3 endLocal = effect->GetCurrentAxisTransposed() * (effect->GetCurrentEndOrigin() - effect->GetCurrentOrigin());
	const float t = Clamp01(mInitPos.x);
	idVec3 forward = endLocal;
	if (forward.LengthSqr() <= 1e-8f) {
		return;
	}
	forward.NormalizeFast();

	idVec3 right;
	idVec3 up;
	BuildPerpBasis(forward, right, up);

	const float lateralY = mInitPos.y;
	const float lateralZ = mInitPos.z;
	mInitPos = endLocal * t + right * lateralY + up * lateralZ;
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
	if (!length) {
		return;
	}
	parms.Spawn(length, parms, NULL, NULL);
	if (!effect || !effect->GetHasEndOrigin()) {
		return;
	}

	const idVec3 endLocal = effect->GetCurrentAxisTransposed() * (effect->GetCurrentEndOrigin() - effect->GetCurrentOrigin());
	idVec3 forward = endLocal;
	if (forward.LengthSqr() <= 1e-8f) {
		return;
	}
	forward.NormalizeFast();

	idVec3 right;
	idVec3 up;
	BuildPerpBasis(forward, right, up);

	// `useEndOrigin` lengths are authored as offsets around the baseline
	// vector from origin to end-origin.
	const idVec3 out = endLocal + forward * length[0] + right * length[1] + up * length[2];
	length[0] = out.x;
	length[1] = out.y;
	length[2] = out.z;
}

void rvParticle::HandleEndLength(rvBSE* effect, rvParticleTemplate* pt, rvParticleParms& parms, float* length) {
	if ((parms.mFlags & PPFLAG_USEENDORIGIN) != 0) {
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
	mStartTime = birthTime;
	mFraction = fraction;
	mTextureScale = 1.0f;
	mTextureOffset = 0.0f;
	mInitEffectPos = vec3_origin;
	mInitAxis = mat3_identity;
	mTrailRepeat = pt->GetTrailRepeat();

	idVec3 normal(vec3_origin);
	idVec3 centre = pt->mCentre;
	HandleEndOrigin(effect, pt, &normal, &centre);

	SetLocked(st->GetLocked());
	if (initOffset != vec3_origin) {
		mInitPos += initOffset;
	}
	if (GetLocked()) {
		mInitEffectPos = vec3_origin;
		mInitAxis = initAxis;
	}
	else {
		mInitEffectPos = effect->GetCurrentOrigin();
		mInitAxis = effect->GetCurrentAxis() * initAxis;
	}
	if (pt->mpSpawnOffset && pt->mpSpawnOffset->mSpawnType != SPF_NONE_0) {
		SetHasOffset(true);
	}
	if (pt->GetTiled()) {
		SetFlag(true, PTFLAG_TILED);
	}
	if (pt->GetGeneratedLine()) {
		SetFlag(true, PTFLAG_GENERATED_LINE);
	}

	idVec3 direction(1.0f, 0.0f, 0.0f);
	if (pt->mpSpawnDirection) {
		pt->mpSpawnDirection->Spawn(direction.ToFloatPtr(), *pt->mpSpawnDirection, &normal, &centre);
		if (direction.LengthSqr() > 1e-6f) {
			direction.NormalizeFast();
		}
	}

	if (pt->mpSpawnVelocity) {
		pt->mpSpawnVelocity->Spawn(mVelocity.ToFloatPtr(), *pt->mpSpawnVelocity, &normal, &centre);
		if (IsScalarDomain(pt->mpSpawnVelocity)) {
			mVelocity = direction * mVelocity.x;
		}
	}
	else {
		mVelocity.Zero();
	}

	if (pt->mpSpawnAcceleration) {
		pt->mpSpawnAcceleration->Spawn(mAcceleration.ToFloatPtr(), *pt->mpSpawnAcceleration, &normal, &centre);
		if (IsScalarDomain(pt->mpSpawnAcceleration)) {
			mAcceleration = direction * mAcceleration.x;
		}
	}
	else {
		mAcceleration.Zero();
	}

	if (pt->mpSpawnFriction) {
		float frictionParms[3] = { 0.0f, 0.0f, 0.0f };
		pt->mpSpawnFriction->Spawn(frictionParms, *pt->mpSpawnFriction, &normal, &centre);
		mFriction = Max(0.0f, frictionParms[0]);
	}
	else {
		mFriction = 0.0f;
	}

	if (pt->GetParentVelocity()) {
		mVelocity += effect->GetCurrentVelocity();
	}

	const float duration = idMath::ClampFloat(BSE_TIME_EPSILON, BSE_MAX_DURATION, pt->GetDuration());
	mEndTime = mStartTime + duration;

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

	if (float* initLength = GetInitLength()) {
		if (pt->mpSpawnLength) {
			HandleEndLength(effect, pt, *pt->mpSpawnLength, initLength);
		}
		if (float* destLength = GetDestLength()) {
			if (pt->mpDeathLength) {
				pt->mpDeathLength->Spawn(destLength, *pt->mpDeathLength, NULL, NULL);
				pt->mpDeathLength->HandleRelativeParms(destLength, initLength, 3);
			}
		}
	}

	mTrailTime = pt->GetTrailTime();
	mTrailCount = idMath::ClampInt(0, 128, idMath::FtoiFast(rvRandom::flrand(pt->mTrailInfo->mTrailCount.x, pt->mTrailInfo->mTrailCount.y)));
	SetModel(pt->GetModel());
	SetupElectricity(pt);

	const float attenuation = effect->GetAttenuation(st);
	if (pt->mpSpawnFade) {
		AttenuateFade(attenuation, *pt->mpSpawnFade);
	}
	if (pt->mpSpawnSize) {
		AttenuateSize(attenuation, *pt->mpSpawnSize);
	}
	if (pt->mpSpawnLength) {
		AttenuateLength(attenuation, *pt->mpSpawnLength);
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
	rvParticle::FinishSpawn(effect, segment, birthTime, fraction, initOffset, initAxis);
}

void rvLinkedParticle::FinishSpawn(rvBSE* effect, rvSegment* segment, float birthTime, float fraction, const idVec3& initOffset, const idMat3& initAxis) {
	rvParticle::FinishSpawn(effect, segment, birthTime, fraction, initOffset, initAxis);
}

void rvDebrisParticle::FinishSpawn(rvBSE* effect, rvSegment* segment, float birthTime, float fraction, const idVec3& initOffset, const idMat3& initAxis) {
	if (!bse_debris.GetBool() || !effect || !segment || !game || session->readDemo) {
		return;
	}

	rvParticle::FinishSpawn(effect, segment, birthTime, fraction, initOffset, initAxis);

	rvSegmentTemplate* st = segment->GetSegmentTemplate();
	if (!st) {
		return;
	}
	rvParticleTemplate* pt = st->GetParticleTemplate();
	if (!pt) {
		return;
	}

	const char* entityDefName = pt->GetEntityDefName();
	if (!entityDefName || entityDefName[0] == '\0') {
		// Keep compatibility with legacy data that may author debris as plain particles.
		return;
	}

	idVec3 localPosition;
	EvaluatePosition(effect, pt, localPosition, birthTime);
	idVec3 localVelocity;
	EvaluateVelocity(effect, localVelocity, birthTime);

	idVec3 angularVelocity(vec3_origin);
	if (float* initRotate = GetInitRotation()) {
		angularVelocity.Set(initRotate[0], initRotate[1], initRotate[2]);
	}

	idVec3 worldOrigin;
	idVec3 worldVelocity;
	idMat3 worldAxis;
	if (GetLocked()) {
		worldOrigin = effect->GetCurrentOrigin() + effect->GetCurrentAxis() * localPosition;
		worldVelocity = effect->GetCurrentAxis() * localVelocity;
		angularVelocity = effect->GetCurrentAxis() * angularVelocity;
		worldAxis = effect->GetCurrentAxis();
	}
	else {
		worldOrigin = mInitEffectPos + mInitAxis * localPosition;
		worldVelocity = mInitAxis * localVelocity;
		angularVelocity = mInitAxis * angularVelocity;
		worldAxis = mInitAxis;
	}

	const int maxLifetimeMs = idMath::FtoiFast(BSE_MAX_DURATION * 1000.0f);
	const int lifetimeMs = idMath::ClampInt(1, maxLifetimeMs, idMath::FtoiFast(GetDuration() * 1000.0f));

	game->SpawnClientMoveable(entityDefName, lifetimeMs, worldOrigin, worldAxis, worldVelocity, angularVelocity);

	// Debris is represented by spawned client entities, not by CPU-side BSE quads.
	mEndTime = mStartTime;
	mTrailTime = 0.0f;
	mTrailCount = 0;
}

void rvLineParticle::Refresh(rvBSE* effect, rvSegmentTemplate* st, rvParticleTemplate* pt) {
	if (!effect || !pt || !pt->UsesEndOrigin()) {
		return;
	}
	float* initLength = GetInitLength();
	if (initLength && pt->mpSpawnLength) {
		HandleEndLength(effect, pt, *pt->mpSpawnLength, initLength);
	}
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
	const float t = Max(0.0f, time - mStartTime);
	const float damp = Max(0.0f, 1.0f - mFriction * t);
	idVec3 localVelocity = (mVelocity + mAcceleration * t) * damp;
	if (effect && !GetLocked()) {
		const idVec3 worldVelocity = mInitAxis * localVelocity;
		velocity = effect->GetCurrentAxisTransposed() * worldVelocity;
	}
	else {
		velocity = localVelocity;
	}
}

void rvParticle::EvaluatePosition(const rvBSE* effect, rvParticleTemplate* pt, idVec3& pos, float time) {
	const float t = Max(0.0f, time - mStartTime);
	const float damp = Max(0.0f, 1.0f - mFriction * t);
	pos = mInitPos + (mVelocity * t + mAcceleration * (0.5f * t * t)) * damp;

	if (GetHasOffset() && pt && pt->mpOffsetEnvelope) {
		const float oneOverDuration = 1.0f / Max(BSE_TIME_EPSILON, GetDuration());
		idVec3 offs;
		EvaluateOffset(pt->mpOffsetEnvelope, t, oneOverDuration, offs);
		pos += offs;
	}

	if (effect && !GetLocked()) {
		const idVec3 worldPos = mInitEffectPos + mInitAxis * pos;
		pos = effect->GetCurrentAxisTransposed() * (worldPos - effect->GetCurrentOrigin());
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
	trace_t trace;
	idVec3 source = sourceWorld;
	idVec3 dest = destWorld;
	game->Translation(trace, source, dest, trm, CONTENTS_SOLID | CONTENTS_OPAQUE);
	if (trace.fraction >= 1.0f) {
		return false;
	}

	if (pt->mNumImpactEffects > 0 && bse->CanPlayRateLimited(EC_IMPACT_PARTICLES)) {
		idVec3 impactPos = trace.endpos;
		if (trm) {
			const idVec3 motion = (destWorld - sourceWorld) * trace.fraction;
			CalcImpactPoint(impactPos, trace.endpos, motion, trm->bounds, trace.c.normal);
		}

		const int idx = rvRandom::irand(0, pt->mNumImpactEffects - 1);
		const rvDeclEffect* impactEffect = pt->mImpactEffects[idx];
		if (impactEffect) {
			game->PlayEffect(
				impactEffect,
				impactPos,
				trace.c.normal.ToMat3(),
				false,
				vec3_origin,
				false,
				false,
				EC_IGNORE,
				vec4_one);
		}
	}

	if (pt->mBounce > 0.0f) {
		Bounce(effect, pt, trace.endpos, trace.c.normal, time);
	}

	return pt->GetDeleteOnImpact();
}

void rvParticle::Bounce(rvBSE* effect, rvParticleTemplate* pt, idVec3 endPos, idVec3 normal, float time) {
	if (!effect || !pt) {
		return;
	}

	const float sampleTime = Max(0.0f, time - mMotionStartTime);
	idVec3 oldVelocity;
	EvaluateVelocity(effect, oldVelocity, mStartTime + sampleTime);

	idVec3 worldVelocity = effect->GetCurrentAxis() * oldVelocity;
	const float proj = worldVelocity * normal;
	worldVelocity -= (proj + proj) * normal;
	worldVelocity *= pt->mBounce;

	const float speedSqr = worldVelocity.LengthSqr();
	if (speedSqr < BSE_BOUNCE_LIMIT * 0.01f) {
		SetStationary(true);
		worldVelocity.Zero();
	}

	mInitEffectPos = effect->GetCurrentOrigin();
	mInitAxis = effect->GetCurrentAxis();
	mVelocity = effect->GetCurrentAxisTransposed() * worldVelocity;
	mMotionStartTime = time;
	mInitPos = effect->GetCurrentAxisTransposed() * (endPos - effect->GetCurrentOrigin());
	mInitPos += normal * BSE_TRACE_OFFSET;
}

void rvParticle::CheckTimeoutEffect(rvBSE* effect, rvSegmentTemplate* st, float time) {
	if (!effect || !st || !game) {
		return;
	}
	rvParticleTemplate* pt = st->GetParticleTemplate();
	if (!pt || pt->GetNumTimeoutEffects() <= 0) {
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
			idVec3 position;
			idVec3 velocity;
			EvaluatePosition(effect, pt, position, mLastTrailTime);
			EvaluateVelocity(effect, velocity, mLastTrailTime);
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
	idVec4 out = colour;
	out[3] *= alpha;
	if (effect) {
		const float bright = effect->GetBrightness();
		out[0] *= effect->GetRed() * bright;
		out[1] *= effect->GetGreen() * bright;
		out[2] *= effect->GetBlue() * bright;
		out[3] *= effect->GetAlpha();
	}
	return PackColorLocal(out);
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

void rvParticle::RenderMotion(rvBSE* effect, rvParticleTemplate* pt, srfTriangles_t* tri, const renderEffect_s* owner, float time, float trailScale) {
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
	const float oneOverDuration = 1.0f / Max(BSE_TIME_EPSILON, GetDuration());
	idVec4 color;
	EvaluateTint(pt->mpTintEnvelope, pt->mpFadeEnvelope, evalTime, oneOverDuration, color);

	float width = 1.0f;
	EvaluateSize(pt->mpSizeEnvelope, evalTime, oneOverDuration, &width);

	idVec3 position;
	EvaluatePosition(effect, pt, position, time);

	idVec3 motionDir = (position - mInitPos).Cross(mVelocity);
	if (motionDir.LengthSqr() <= 1e-6f) {
		motionDir = idVec3(0.0f, 0.0f, 1.0f).Cross(position - mInitPos);
	}
	if (motionDir.LengthSqr() <= 1e-6f) {
		motionDir = effect->GetViewAxis()[2];
	}
	else {
		motionDir.NormalizeFast();
	}
	const idVec3 halfWidth = motionDir * (idMath::Fabs(width) * 0.5f * Max(0.001f, trailScale));

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
	size.Set(mSizeEnv.GetStart()[0], mSizeEnv.GetStart()[1], mSizeEnv.GetStart()[2]);
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

	const float oneOverDuration = 1.0f / Max(BSE_TIME_EPSILON, GetDuration());
	idVec4 color;
	EvaluateTint(pt->mpTintEnvelope, pt->mpFadeEnvelope, evalTime, oneOverDuration, color);
	color[3] *= override;
	if (color[3] <= 0.0f) {
		BSETraceRenderDrop("sprite", this, time, "alpha", color[3], override);
		return false;
	}

	float size[2] = { 1.0f, 1.0f };
	EvaluateSize(pt->mpSizeEnvelope, evalTime, oneOverDuration, size);
	const idVec2& spriteSize = effect->GetSpriteSize();
	if (idMath::Fabs(spriteSize.x) > BSE_TIME_EPSILON || idMath::Fabs(spriteSize.y) > BSE_TIME_EPSILON) {
		size[0] = spriteSize.x;
		size[1] = spriteSize.y;
	}
	float rotation = 0.0f;
	EvaluateRotation(pt->mpRotateEnvelope, evalTime, oneOverDuration, &rotation);

	float s, c;
	idMath::SinCos(rotation, s, c);
	idVec3 up = view[1] * c + view[2] * s;
	idVec3 right = view[1] * -s + view[2] * c;

	const idVec3 halfRight = right * (idMath::Fabs(size[0]) * 0.5f);
	const idVec3 halfUp = up * (idMath::Fabs(size[1]) * 0.5f);

	dword rgba = HandleTint(effect, color, 1.0f);
	AppendQuad(
		tri,
		pos - halfRight - halfUp,
		pos + halfRight - halfUp,
		pos + halfRight + halfUp,
		pos - halfRight + halfUp,
		rgba);
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

	const float oneOverDuration = 1.0f / Max(BSE_TIME_EPSILON, GetDuration());
	idVec4 color;
	EvaluateTint(pt->mpTintEnvelope, pt->mpFadeEnvelope, evalTime, oneOverDuration, color);
	color[3] *= override;
	if (color[3] <= 0.0f) {
		BSETraceRenderDrop("line", this, time, "alpha", color[3], override);
		return false;
	}

	float width = 1.0f;
	EvaluateSize(pt->mpSizeEnvelope, evalTime, oneOverDuration, &width);
	width = Max(0.01f, idMath::Fabs(width));

	idVec3 length(0.0f, 0.0f, 1.0f);
	EvaluateLength(pt->mpLengthEnvelope, evalTime, oneOverDuration, length);
	if (!GetLocked()) {
		length = mInitAxis * length;
	}
	if (GetGeneratedLine()) {
		idVec3 velocity;
		EvaluateVelocity(effect, velocity, time);
		const float velocitySqr = velocity.LengthSqr();
		if (velocitySqr > 1e-8f) {
			velocity.NormalizeFast();
			length = velocity * length.LengthFast();
		}
	}
	if (length.LengthSqr() < 1e-6f) {
		length = mVelocity;
	}
	if (length.LengthSqr() < 1e-6f) {
		length.Set(0.0f, 0.0f, 4.0f);
	}

	const idVec3 end = pos + length;
	idVec3 side = view[0].Cross(length);
	if (side.LengthSqr() < 1e-6f) {
		side = view[2];
	}
	else {
		side.NormalizeFast();
	}
	side *= width;

	dword rgba = HandleTint(effect, color, 1.0f);
	AppendQuad(tri, pos + side, pos - side, end - side, end + side, rgba);
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

	const float oneOverDuration = 1.0f / Max(BSE_TIME_EPSILON, GetDuration());
	idVec4 color;
	EvaluateTint(pt->mpTintEnvelope, pt->mpFadeEnvelope, evalTime, oneOverDuration, color);
	color[3] *= override;
	if (color[3] <= 0.0f) {
		return false;
	}

	float size = 1.0f;
	EvaluateSize(pt->mpSizeEnvelope, evalTime, oneOverDuration, &size);
	size = idMath::Fabs(size);

	idVec3 up = view[1] * size;
	dword rgba = HandleTint(effect, color, 1.0f);

	const int base = tri->numVerts;
	SetDrawVert(tri->verts[base + 0], pos + up, mFraction * mTextureScale, 0.0f, rgba);
	SetDrawVert(tri->verts[base + 1], pos - up, mFraction * mTextureScale, 1.0f, rgba);
	if (base > 0) {
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

	const float oneOverDuration = 1.0f / Max(BSE_TIME_EPSILON, GetDuration());
	idVec4 tint;
	EvaluateTint(pt->mpTintEnvelope, pt->mpFadeEnvelope, evalTime, oneOverDuration, tint);
	tint[3] *= override;
	if (tint[3] <= 0.0f) {
		BSETraceRenderDrop("oriented", this, time, "alpha", tint[3], override);
		return false;
	}

	float size[2] = { 1.0f, 1.0f };
	float rotation[3] = { 0.0f, 0.0f, 0.0f };
	EvaluateSize(pt->mpSizeEnvelope, evalTime, oneOverDuration, size);
	EvaluateRotation(pt->mpRotateEnvelope, evalTime, oneOverDuration, rotation);

	idMat3 transform;
	rvAngles(rotation[0], rotation[1], rotation[2]).ToMat3(transform);
	const idVec3 right = transform[0] * (idMath::Fabs(size[0]) * 0.5f);
	const idVec3 up = transform[1] * (idMath::Fabs(size[1]) * 0.5f);

	dword rgba = HandleTint(effect, tint, 1.0f);
	AppendQuad(
		tri,
		position - right - up,
		position + right - up,
		position + right + up,
		position - right + up,
		rgba);
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

	const float oneOverDuration = 1.0f / Max(BSE_TIME_EPSILON, GetDuration());
	idVec4 color;
	EvaluateTint(pt->mpTintEnvelope, pt->mpFadeEnvelope, evalTime, oneOverDuration, color);
	color[3] *= override;
	if (color[3] <= 0.0f) {
		return false;
	}

	float size[3] = { 1.0f, 1.0f, 1.0f };
	EvaluateSize(pt->mpSizeEnvelope, evalTime, oneOverDuration, size);

	float rotation[3] = { 0.0f, 0.0f, 0.0f };
	EvaluateRotation(pt->mpRotateEnvelope, evalTime, oneOverDuration, rotation);

	const modelSurface_t* surf = mModel->Surface(0);
	if (!surf || !surf->geometry) {
		return false;
	}

	const srfTriangles_t* src = surf->geometry;
	const int baseVert = tri->numVerts;
	const int baseIndex = tri->numIndexes;
	const dword rgba = HandleTint(effect, color, 1.0f);
	byte rgbaBytes[4];
	UnpackColor(rgba, rgbaBytes);

	idMat3 rotationMat;
	rvAngles(rotation[0], rotation[1], rotation[2]).ToMat3(rotationMat);
	idMat3 transform = mInitAxis * rotationMat;

	for (int i = 0; i < src->numVerts; ++i) {
		idDrawVert& dst = tri->verts[baseVert + i];
		dst = src->verts[i];

		idVec3 p = src->verts[i].xyz;
		p.x *= size[0];
		p.y *= size[1];
		p.z *= size[2];
		dst.xyz = position + transform * p;

		dst.normal = transform * dst.normal;
		dst.tangents[0] = transform * dst.tangents[0];
		dst.tangents[1] = transform * dst.tangents[1];
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

void rvElectricityParticle::RenderBranch(const rvBSE* effect, struct SElecWork* work, idVec3 start, idVec3 end) {
	if (!effect || !work || !work->tri || !work->coords) {
		return;
	}

	idVec3 forward = end - start;
	const float length = forward.Normalize();
	if (length < 1e-6f) {
		work->coordCount = 0;
		return;
	}

	idVec3 left;
	if (idMath::Fabs(forward.x) < 0.99f) {
		left = idVec3(0.0f, 0.0f, 1.0f).Cross(forward);
	}
	else {
		left = idVec3(0.0f, 1.0f, 0.0f).Cross(forward);
	}
	if (left.LengthSqr() > 1e-8f) {
		left.NormalizeFast();
	}
	else {
		left.Set(1.0f, 0.0f, 0.0f);
	}
	idVec3 up = forward.Cross(left);

	const int segmentVertStart = work->tri->numVerts;
	int outCount = 0;
	float fraction = 0.0f;
	idVec3 current = start;
	work->coords[outCount++] = current;

	while (fraction < 1.0f - work->step * 0.5f && outCount < 254) {
		fraction += work->step;
		const float noise = mJitterTable ? mJitterTable->TableLookup(fraction) : 0.0f;

		idVec3 jitter(
			rvRandom::flrand(-mJitterSize.x, mJitterSize.x),
			rvRandom::flrand(-mJitterSize.y, mJitterSize.y),
			rvRandom::flrand(-mJitterSize.z, mJitterSize.z));

		const idVec3 offset = forward * jitter.x + left * jitter.y + up * jitter.z;
		current = start + forward * (length * fraction) + offset * noise;
		work->coords[outCount++] = current;
	}

	work->coords[outCount++] = end;
	work->coordCount = outCount;

	float vCoord = 0.0f;
	for (int i = 0; i < outCount - 1; ++i) {
		RenderLineSegment(effect, work, work->coords[i], vCoord);
		vCoord += work->step;
	}

	for (int base = segmentVertStart; base < work->tri->numVerts - 2; base += 2) {
		const int indexBase = work->tri->numIndexes;
		work->tri->indexes[indexBase + 0] = base;
		work->tri->indexes[indexBase + 1] = base + 1;
		work->tri->indexes[indexBase + 2] = base + 2;
		work->tri->indexes[indexBase + 3] = base;
		work->tri->indexes[indexBase + 4] = base + 2;
		work->tri->indexes[indexBase + 5] = base + 3;
		work->tri->numIndexes += 6;
	}
}

void rvElectricityParticle::RenderLineSegment(const rvBSE* effect, struct SElecWork* work, idVec3 start, float startFraction) {
	if (!effect || !work || !work->tri) {
		return;
	}

	idVec3 offset = work->length.Cross(work->viewPos);
	const float len2 = offset.LengthSqr();
	if (len2 > 1e-8f) {
		offset *= idMath::InvSqrt(len2);
	}
	else {
		offset.Set(0.0f, 0.0f, 1.0f);
	}
	offset *= work->size;

	const dword color = HandleTint(effect, work->tint, work->alpha);
	const float s = startFraction * work->step + work->fraction;
	const int baseVert = work->tri->numVerts;
	SetDrawVert(work->tri->verts[baseVert + 0], start + offset, s, 0.0f, color);
	SetDrawVert(work->tri->verts[baseVert + 1], start - offset, s, 1.0f, color);
	work->tri->numVerts += 2;
}

void rvElectricityParticle::ApplyShape(const rvBSE* effect, struct SElecWork* work, idVec3 start, idVec3 end, int count, float startFraction, float endFraction) {
	if (!effect || !work) {
		return;
	}

	if (count <= 0) {
		RenderLineSegment(effect, work, start, startFraction);
		return;
	}

	const float randA = rvRandom::flrand(0.05f, 0.09f);
	const float randB = rvRandom::flrand(0.05f, 0.09f);
	const float shape = rvRandom::flrand(0.56f, 0.76f);

	const idVec3 dir = end - start;
	const float length = dir.LengthFast() * 0.7f;
	if (length <= 1e-6f) {
		RenderLineSegment(effect, work, start, startFraction);
		return;
	}

	idVec3 forward = dir;
	forward.NormalizeFast();

	idVec3 left = forward.Cross(idVec3(0.0f, 0.0f, 1.0f));
	if (left.LengthSqr() < 1e-6f) {
		left.Set(1.0f, 0.0f, 0.0f);
	}
	else {
		left.NormalizeFast();
	}
	const idVec3 down = forward.Cross(left);

	const float len1 = rvRandom::flrand(-randA - 0.02f, 0.02f - randA) * length;
	const float len2 = rvRandom::flrand(-randB - 0.02f, 0.02f - randB) * length;

	const idVec3 point1 =
		start * shape +
		end * (1.0f - shape) +
		left * len1 +
		down * rvRandom::flrand(0.23f, 0.43f) * length;

	const float t2 = rvRandom::flrand(0.23f, 0.43f);
	const idVec3 point2 =
		start * t2 +
		end * (1.0f - t2) +
		left * len2 +
		down * rvRandom::flrand(-0.02f, 0.02f) * length;

	const float mid0 = startFraction * 0.6666667f + endFraction * 0.3333333f;
	const float mid1 = startFraction * 0.3333333f + endFraction * 0.6666667f;

	ApplyShape(effect, work, start, point1, count - 1, startFraction, mid0);
	ApplyShape(effect, work, point1, point2, count - 1, mid0, mid1);
	ApplyShape(effect, work, point2, end, count - 1, mid1, endFraction);
}

int rvElectricityParticle::Update(rvParticleTemplate* pt, float time) {
	if (!pt || !pt->mpLengthEnvelope) {
		mNumBolts = 0;
		return 0;
	}

	const float evalTime = Max(0.0f, time - mStartTime);
	const float oneOverDuration = 1.0f / Max(BSE_TIME_EPSILON, GetDuration());
	idVec3 length;
	EvaluateLength(pt->mpLengthEnvelope, evalTime, oneOverDuration, length);
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

	const float oneOverDuration = 1.0f / Max(BSE_TIME_EPSILON, GetDuration());
	idVec4 tint;
	EvaluateTint(pt->mpTintEnvelope, pt->mpFadeEnvelope, evalTime, oneOverDuration, tint);
	tint[3] *= override;
	if (tint[3] <= 0.0f) {
		return false;
	}

	float width = 1.0f;
	EvaluateSize(pt->mpSizeEnvelope, evalTime, oneOverDuration, &width);
	idVec3 length;
	EvaluateLength(pt->mpLengthEnvelope, evalTime, oneOverDuration, length);

	idVec3 position;
	EvaluatePosition(effect, pt, position, time);

	if (!GetLocked()) {
		length = mInitAxis * length;
	}

	if (GetGeneratedLine()) {
		idVec3 velocity;
		EvaluateVelocity(effect, velocity, time);
		if (velocity.LengthSqr() > 1e-8f) {
			velocity.NormalizeFast();
			length = velocity * length.LengthFast();
		}
	}

	const float mainLength = length.LengthFast();
	if (mainLength < 0.1f) {
		return false;
	}

	if (mLastJitter + mJitterRate <= time) {
		mLastJitter = time;
		mSeed = rvRandom::Init();
	}

	if (mSeed != 0) {
		rvRandom::Init(static_cast<unsigned long>(mSeed));
	}

	const int boltCount = Max(1, (mNumBolts > 0) ? mNumBolts : GetBoltCount(mainLength));
	mNumBolts = boltCount;

	SElecWork work;
	memset(&work, 0, sizeof(work));
	idVec3 tmpCoords[256];
	work.tri = tri;
	work.coords = tmpCoords;
	work.coordCount = 0;
	work.tint = tint;
	work.size = idMath::Fabs(width);
	work.alpha = 1.0f;
	work.length = length;
	work.forward = length;
	work.viewPos = view[0];
	work.step = mTextureScale / static_cast<float>(boltCount);
	if (work.step <= BSE_TIME_EPSILON) {
		work.step = 1.0f / static_cast<float>(boltCount);
	}

	const idVec3 endPos = position + length;
	RenderBranch(effect, &work, position, endPos);

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

		work.length = dir;
		work.forward = dir;
		work.step = 1.0f / static_cast<float>(GetBoltCount(forkLength));
		RenderBranch(effect, &work, forkBases[i], forkEnd);
	}

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

	mNumBolts = 0;
	mNumForks = pt->mElecInfo->mNumForks;
	mSeed = rvRandom::irand(1, 0x7FFFFFFF);
	mForkSizeMins = pt->mElecInfo->mForkSizeMins;
	mForkSizeMaxs = pt->mElecInfo->mForkSizeMaxs;
	mJitterSize = pt->mElecInfo->mJitterSize;
	mLastJitter = 0.0f;
	mJitterRate = pt->mElecInfo->mJitterRate;
	mJitterTable = pt->mElecInfo->mJitterTable;
}

bool rvLightParticle::InitLight(rvBSE* effect, rvSegmentTemplate* st, float time) {
	if (!effect || !st || !session || !session->rw) {
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

	const float oneOverDuration = 1.0f / Max(BSE_TIME_EPSILON, GetDuration());
	idVec4 tint;
	idVec3 size;
	idVec3 position;
	EvaluateTint(pt->mpTintEnvelope, pt->mpFadeEnvelope, evalTime, oneOverDuration, tint);
	EvaluateSize(pt->mpSizeEnvelope, evalTime, oneOverDuration, size.ToFloatPtr());
	EvaluatePosition(effect, pt, position, time);

	memset(&mLight, 0, sizeof(mLight));
	mLight.origin = effect->GetCurrentOrigin() + effect->GetCurrentAxis() * position;
	mLight.lightRadius.x = Max(1.0f, idMath::Fabs(size.x));
	mLight.lightRadius.y = Max(1.0f, idMath::Fabs(size.y));
	mLight.lightRadius.z = Max(1.0f, idMath::Fabs(size.z));
	mLight.axis = effect->GetCurrentAxis();
	mLight.shaderParms[0] = tint.x;
	mLight.shaderParms[1] = tint.y;
	mLight.shaderParms[2] = tint.z;
	mLight.shaderParms[3] = tint.w;
	mLight.pointLight = true;
	mLight.detailLevel = 10.0f;
	mLight.noShadows = !pt->GetShadows();
	mLight.noSpecular = !pt->GetSpecular();
	mLight.suppressLightInViewID = effect->GetSuppressLightsInViewID();
	mLight.lightId = LIGHTID_EFFECT_LIGHT;
	mLight.shader = pt->GetMaterial() ? pt->GetMaterial() : declManager->FindMaterial("_default");

	mLightDefHandle = session->rw->AddLightDef(&mLight);
	return mLightDefHandle != -1;
}

bool rvLightParticle::PresentLight(rvBSE* effect, rvParticleTemplate* pt, float time, bool infinite) {
	if (!effect || !pt || !session || !session->rw) {
		return false;
	}

	float evalTime;
	if (!GetEvaluationTime(time, evalTime, infinite)) {
		return false;
	}

	if (mLightDefHandle == -1) {
		mLightDefHandle = session->rw->AddLightDef(&mLight);
		if (mLightDefHandle == -1) {
			return false;
		}
	}

	const float oneOverDuration = 1.0f / Max(BSE_TIME_EPSILON, GetDuration());
	idVec4 tint;
	idVec3 size;
	idVec3 position;
	EvaluateTint(pt->mpTintEnvelope, pt->mpFadeEnvelope, evalTime, oneOverDuration, tint);
	EvaluateSize(pt->mpSizeEnvelope, evalTime, oneOverDuration, size.ToFloatPtr());
	EvaluatePosition(effect, pt, position, time);

	mLight.origin = effect->GetCurrentOrigin() + effect->GetCurrentAxis() * position;
	mLight.lightRadius.x = Max(1.0f, idMath::Fabs(size.x));
	mLight.lightRadius.y = Max(1.0f, idMath::Fabs(size.y));
	mLight.lightRadius.z = Max(1.0f, idMath::Fabs(size.z));
	mLight.axis = effect->GetCurrentAxis();
	mLight.shaderParms[0] = tint.x;
	mLight.shaderParms[1] = tint.y;
	mLight.shaderParms[2] = tint.z;
	mLight.shaderParms[3] = tint.w;
	mLight.suppressLightInViewID = effect->GetSuppressLightsInViewID();
	session->rw->UpdateLightDef(mLightDefHandle, &mLight);
	return true;
}

bool rvLightParticle::Destroy(void) {
	if (mLightDefHandle != -1 && session && session->rw) {
		session->rw->FreeLightDef(mLightDefHandle);
	}
	mLightDefHandle = -1;
	memset(&mLight, 0, sizeof(mLight));
	return true;
}
