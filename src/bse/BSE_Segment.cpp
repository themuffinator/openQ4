// BSE_Segment.cpp
//




#include "BSE_Envelope.h"
#include "BSE_Particle.h"
#include "BSE_SpawnDomains.h"
#include "BSE.h"

#include <math.h>
#include <stdint.h>

#include "../renderer/Model_local.h"
#include "../renderer/tr_local.h"
#include "../framework/Session.h"

namespace {
ID_INLINE int GetSegmentParticleCap() {
	return idMath::ClampInt(0, MAX_PARTICLES, bse_maxParticles.GetInteger());
}

ID_INLINE uint32_t BSEHash32(uint32_t seed) {
	seed ^= seed >> 16;
	seed *= 0x7feb352dU;
	seed ^= seed >> 15;
	seed *= 0x846ca68bU;
	seed ^= seed >> 16;
	return seed;
}

ID_INLINE float HashUnit(uint32_t seed) {
	return (BSEHash32(seed) & 0x00FFFFFF) * (1.0f / 16777215.0f);
}

ID_INLINE float HashRange(uint32_t seed, float minValue, float maxValue) {
	return minValue + (maxValue - minValue) * HashUnit(seed);
}

ID_INLINE float LerpFloat(float a, float b, float t) {
	return a + (b - a) * t;
}

ID_INLINE void ApplyRelative(const rvParticleParms* parms, float* value, const float* initValue, int count) {
	if (!parms || (parms->mFlags & PPFLAG_RELATIVE) == 0) {
		return;
	}
	for (int i = 0; i < count; ++i) {
		value[i] += initValue[i];
	}
}

ID_INLINE void SampleUnitSphere(uint32_t seed, idVec3& outDir) {
	const float z = HashRange(seed + 1, -1.0f, 1.0f);
	const float phi = HashRange(seed + 2, 0.0f, idMath::TWO_PI);
	const float r = idMath::Sqrt(Max(0.0f, 1.0f - z * z));
	outDir.x = r * idMath::Cos(phi);
	outDir.y = r * idMath::Sin(phi);
	outDir.z = z;
}

ID_INLINE void SampleParticleParms(const rvParticleParms* parms, int parmCount, uint32_t seed, float* outValue) {
	outValue[0] = 0.0f;
	outValue[1] = 0.0f;
	outValue[2] = 0.0f;
	if (!parms || parmCount <= 0) {
		return;
	}

	const idVec3& mins = parms->mMins;
	const idVec3& maxs = parms->mMaxs;
	const int spawnBase = parms->mSpawnType & ~0x3;

	switch (spawnBase) {
	case SPF_NONE_0:
		return;
	case SPF_ONE_0:
		outValue[0] = 1.0f;
		outValue[1] = (parmCount > 1) ? 1.0f : 0.0f;
		outValue[2] = (parmCount > 2) ? 1.0f : 0.0f;
		return;
	case SPF_POINT_0:
		outValue[0] = mins.x;
		if (parmCount > 1) {
			outValue[1] = mins.y;
		}
		if (parmCount > 2) {
			outValue[2] = mins.z;
		}
		return;
	case SPF_LINEAR_0: {
		const float t = HashUnit(seed + 17);
		outValue[0] = LerpFloat(mins.x, maxs.x, t);
		if (parmCount > 1) {
			outValue[1] = LerpFloat(mins.y, maxs.y, t);
		}
		if (parmCount > 2) {
			outValue[2] = LerpFloat(mins.z, maxs.z, t);
		}
		return;
	}
	case SPF_BOX_0:
	case SPF_MODEL_0:
		outValue[0] = HashRange(seed + 11, mins.x, maxs.x);
		if (parmCount > 1) {
			outValue[1] = HashRange(seed + 13, mins.y, maxs.y);
		}
		if (parmCount > 2) {
			outValue[2] = HashRange(seed + 15, mins.z, maxs.z);
		}
		return;
	case SPF_SURFACE_BOX_0:
		outValue[0] = HashRange(seed + 11, mins.x, maxs.x);
		if (parmCount > 1) {
			outValue[1] = HashRange(seed + 13, mins.y, maxs.y);
		}
		if (parmCount > 2) {
			outValue[2] = HashRange(seed + 15, mins.z, maxs.z);
		}
		switch (static_cast<int>(HashUnit(seed + 19) * 6.0f)) {
		case 0: outValue[0] = mins.x; break;
		case 1: outValue[0] = maxs.x; break;
		case 2: outValue[1] = mins.y; break;
		case 3: outValue[1] = maxs.y; break;
		case 4: outValue[2] = mins.z; break;
		default: outValue[2] = maxs.z; break;
		}
		return;
	case SPF_SPHERE_0:
	case SPF_SURFACE_SPHERE_0: {
		const idVec3 center((mins.x + maxs.x) * 0.5f, (mins.y + maxs.y) * 0.5f, (mins.z + maxs.z) * 0.5f);
		const idVec3 radius((maxs.x - mins.x) * 0.5f, (maxs.y - mins.y) * 0.5f, (maxs.z - mins.z) * 0.5f);
		idVec3 dir;
		SampleUnitSphere(seed, dir);
		float radial = 1.0f;
		if (spawnBase == SPF_SPHERE_0) {
			radial = HashUnit(seed + 21);
		}
		outValue[0] = center.x + radius.x * dir.x * radial;
		if (parmCount > 1) {
			outValue[1] = center.y + radius.y * dir.y * radial;
		}
		if (parmCount > 2) {
			outValue[2] = center.z + radius.z * dir.z * radial;
		}
		return;
	}
	case SPF_CYLINDER_0:
	case SPF_SURFACE_CYLINDER_0: {
		const float t = HashUnit(seed + 23);
		const float phi = HashRange(seed + 25, 0.0f, idMath::TWO_PI);
		const float radial = (spawnBase == SPF_SURFACE_CYLINDER_0) ? 1.0f : HashUnit(seed + 27);
		const float cx = LerpFloat(mins.x, maxs.x, t);
		const float cy = (mins.y + maxs.y) * 0.5f;
		const float cz = (mins.z + maxs.z) * 0.5f;
		const float ry = (maxs.y - mins.y) * 0.5f;
		const float rz = (maxs.z - mins.z) * 0.5f;
		outValue[0] = cx;
		if (parmCount > 1) {
			outValue[1] = cy + idMath::Cos(phi) * ry * radial;
		}
		if (parmCount > 2) {
			outValue[2] = cz + idMath::Sin(phi) * rz * radial;
		}
		return;
	}
	case SPF_SPIRAL_0: {
		const float x = HashRange(seed + 29, mins.x, maxs.x);
		const float range = (parms->mRange > BSE_TIME_EPSILON) ? parms->mRange : 1.0f;
		const float theta = idMath::TWO_PI * x / range;
		const float ry = HashRange(seed + 31, mins.y, maxs.y);
		const float rz = HashRange(seed + 33, mins.z, maxs.z);
		outValue[0] = x;
		if (parmCount > 1) {
			outValue[1] = idMath::Cos(theta) * ry;
		}
		if (parmCount > 2) {
			outValue[2] = idMath::Sin(theta) * rz;
		}
		return;
	}
	default:
		outValue[0] = HashRange(seed + 11, mins.x, maxs.x);
		if (parmCount > 1) {
			outValue[1] = HashRange(seed + 13, mins.y, maxs.y);
		}
		if (parmCount > 2) {
			outValue[2] = HashRange(seed + 15, mins.z, maxs.z);
		}
		return;
	}
}

ID_INLINE int GetSizeParmCount(int particleType) {
	switch (particleType) {
	case PTYPE_LINE:
	case PTYPE_ELECTRICITY:
	case PTYPE_LINKED:
	case PTYPE_ORIENTEDLINKED:
		return 1;
	case PTYPE_MODEL:
	case PTYPE_LIGHT:
	case PTYPE_DEBRIS:
		return 3;
	default:
		return 2;
	}
}

ID_INLINE void SetDrawVert(idDrawVert& vert, const idVec3& xyz, float s, float t, const byte rgba[4]) {
	vert.Clear();
	vert.xyz = xyz;
	vert.st[0] = s;
	vert.st[1] = t;
	vert.normal.Set(1.0f, 0.0f, 0.0f);
	vert.tangents[0].Set(0.0f, 1.0f, 0.0f);
	vert.tangents[1].Set(0.0f, 0.0f, 1.0f);
	vert.color[0] = rgba[0];
	vert.color[1] = rgba[1];
	vert.color[2] = rgba[2];
	vert.color[3] = rgba[3];
}

ID_INLINE void AppendQuad(srfTriangles_t* tri, const idVec3& p0, const idVec3& p1, const idVec3& p2, const idVec3& p3, const byte rgba[4]) {
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
}

rvSegment::~rvSegment() {
	if (!mParticles) {
		return;
	}

	switch (mParticleType) {
	case PTYPE_LINE:
		delete[] static_cast<rvLineParticle*>(mParticles);
		break;
	case PTYPE_ORIENTED:
		delete[] static_cast<rvOrientedParticle*>(mParticles);
		break;
	case PTYPE_DECAL:
		delete[] static_cast<rvDecalParticle*>(mParticles);
		break;
	case PTYPE_MODEL:
		delete[] static_cast<rvModelParticle*>(mParticles);
		break;
	case PTYPE_LIGHT:
		delete[] static_cast<rvLightParticle*>(mParticles);
		break;
	case PTYPE_ELECTRICITY:
		delete[] static_cast<rvElectricityParticle*>(mParticles);
		break;
	case PTYPE_LINKED:
		delete[] static_cast<rvLinkedParticle*>(mParticles);
		break;
	case PTYPE_ORIENTEDLINKED:
		delete[] static_cast<sdOrientedLinkedParticle*>(mParticles);
		break;
	case PTYPE_DEBRIS:
		delete[] static_cast<rvDebrisParticle*>(mParticles);
		break;
	default:
		delete[] static_cast<rvSpriteParticle*>(mParticles);
		break;
	}

	mParticles = NULL;
	mUsedHead = NULL;
	mFreeHead = NULL;
}

rvSegmentTemplate* rvSegment::GetSegmentTemplate(void) {
	if (!mEffectDecl) {
		return NULL;
	}
	return (rvSegmentTemplate*)mEffectDecl->GetSegmentTemplate(mSegmentTemplateHandle);
}

void rvSegment::ValidateSpawnRates() {
	mSecondsPerParticle.y = idMath::ClampFloat(0.002f, 300.0f, mSecondsPerParticle.y);
	mSecondsPerParticle.x = idMath::ClampFloat(mSecondsPerParticle.y, 300.0f, mSecondsPerParticle.x);
}

void rvSegment::GetSecondsPerParticle(rvBSE* effect, rvSegmentTemplate* st, rvParticleTemplate* pt) {
	if (st->mDensity.y == 0.0f) {
		mCount = st->mCount;
	}
	else {
		float volume = 1.0f;
		if (pt) {
			volume = pt->GetSpawnVolume(effect);
		}
		volume = idMath::ClampFloat(BSE_TIME_EPSILON, 1000.0f, volume);
		mCount.x = st->mDensity.x * volume;
		mCount.y = st->mDensity.y * volume;
		if (st->mParticleCap > 0.0f) {
			mCount.x = idMath::ClampFloat(1.0f, st->mParticleCap, mCount.x);
			mCount.y = idMath::ClampFloat(1.0f, st->mParticleCap, mCount.y);
		}
	}

	if (st->mSegType == SEG_EMITTER || st->mSegType == SEG_TRAIL) {
		if (mCount.x > 0.0f) {
			mSecondsPerParticle.x = 1.0f / mCount.x;
		}
		if (mCount.y > 0.0f) {
			mSecondsPerParticle.y = 1.0f / mCount.y;
		}
		ValidateSpawnRates();
	}
}

void rvSegment::InitTime(rvBSE* effect, rvSegmentTemplate* st, float time) {
	SetExpired(false);
	mSegStartTime = rvRandom::flrand(st->mLocalStartTime.x, st->mLocalStartTime.y) + time;
	mSegEndTime = rvRandom::flrand(st->mLocalDuration.x, st->mLocalDuration.y) + mSegStartTime;
	mLastTime = mSegStartTime;
	if (!st->GetIgnoreDuration() || (!effect->GetLooping() && !st->GetSoundLooping())) {
		effect->SetDuration(mSegEndTime - time);
	}
}

float rvSegment::AttenuateDuration(rvBSE* effect, rvSegmentTemplate* st) {
	return effect->GetAttenuation(st) * (mSegEndTime - mSegStartTime);
}

float rvSegment::AttenuateInterval(rvBSE* effect, rvSegmentTemplate* st) {
	const float scale = idMath::ClampFloat(0.0f, 1.0f, bse_scale.GetFloat());
	float interval = idMath::Lerp(mSecondsPerParticle.y, mSecondsPerParticle.x, scale);
	interval = idMath::ClampFloat(mSecondsPerParticle.y, mSecondsPerParticle.x, interval);
	if (!st->GetAttenuateEmitter()) {
		return interval;
	}

	float attenuation = effect->GetAttenuation(st);
	if (st->GetInverseAttenuate()) {
		attenuation = 1.0f - attenuation;
	}
	if (attenuation < 0.002f) {
		return 1.0f;
	}
	return Max(1.1920929e-7f, interval / attenuation);
}

float rvSegment::AttenuateCount(rvBSE* effect, rvSegmentTemplate* st, float min, float max) {
	const float scale = idMath::ClampFloat(0.0f, 1.0f, bse_scale.GetFloat());
	const float scaledMax = idMath::Lerp(min, max, scale);
	float count = rvRandom::flrand(min, scaledMax);
	count = idMath::ClampFloat(min, max, count);
	if (!st->GetAttenuateEmitter()) {
		return count;
	}
	float attenuation = effect->GetAttenuation(st);
	if (st->GetInverseAttenuate()) {
		attenuation = 1.0f - attenuation;
	}
	return attenuation * count;
}

void rvSegment::RefreshParticles(rvBSE* effect, rvSegmentTemplate* st) {
	if (!effect || !st || !st->mParticleTemplate.UsesEndOrigin()) {
		return;
	}

	for (rvParticle* p = mUsedHead; p; p = p->GetNext()) {
		p->Refresh(effect, st, &st->mParticleTemplate);
	}
}

void rvSegment::Init(rvBSE* effect, const rvDeclEffect* effectDecl, int segmentTemplateHandle, float time) {
	mSegmentTemplateHandle = segmentTemplateHandle;
	mFlags = 0;
	mEffectDecl = effectDecl;
	mSurfaceIndex = -1;
	mParticleCount = 0;
	mLoopParticleCount = 0;
	mParticles = NULL;
	mUsedHead = NULL;
	mFreeHead = NULL;
	mActiveCount = 0;
	mLastTime = time;
	mSecondsPerParticle.Zero();
	mCount.Set(1.0f, 1.0f);
	mSoundVolume = 0.0f;
	mFreqShift = 1.0f;

	const rvSegmentTemplate* st = effectDecl->GetSegmentTemplate(segmentTemplateHandle);
	if (!st) {
		mParticleType = PTYPE_NONE;
		return;
	}

	mParticleType = st->mParticleTemplate.GetType();
	InitTime(effect, const_cast<rvSegmentTemplate*>(st), time);
	GetSecondsPerParticle(effect, const_cast<rvSegmentTemplate*>(st), const_cast<rvParticleTemplate*>(&st->mParticleTemplate));
}

void rvSegment::ResetTime(rvBSE* effect, float time) {
	rvSegmentTemplate* st = (rvSegmentTemplate*)mEffectDecl->GetSegmentTemplate(mSegmentTemplateHandle);
	if (st && !st->GetInfiniteDuration()) {
		InitTime(effect, st, time);
	}

}

void rvSegment::InitParticles(rvBSE* effect) {
	if (!effect) {
		return;
	}

	InitParticleArray(effect);
	mActiveCount = 0;
}

bool rvSegment::Check(rvBSE* effect, float time, float offset) {
	rvSegmentTemplate* st = (rvSegmentTemplate*)mEffectDecl->GetSegmentTemplate(mSegmentTemplateHandle);
	if (!st || !st->GetEnabled()) {
		return false;
	}
	if (st->DetailCull()) {
		return false;
	}
	if (GetExpired() || effect->GetStopped()) {
		return true;
	}
	if (time < mSegStartTime) {
		return false;
	}

	const bool infinite = st->GetInfiniteDuration() || st->GetSoundLooping();

	switch (st->mSegType) {
	case SEG_EMITTER: {
		if (!st->GetHasParticles()) {
			return true;
		}
		if (!effect->CanInterpolate()) {
			return true;
		}

		float spawnTime = mLastTime;
		mLastTime = time;

		float spawnEnd = time + BSE_FUTURE;
		if (!infinite && mSegEndTime - BSE_TIME_EPSILON <= spawnEnd) {
			spawnEnd = mSegEndTime;
		}

		int spawned = 0;
		const int maxSpawnPerService = GetSegmentParticleCap();
		if (spawnTime < spawnEnd) {
			while (spawnTime < spawnEnd && spawned < maxSpawnPerService) {
				if (spawnTime >= mSegStartTime) {
					SpawnParticle(effect, st, spawnTime, vec3_origin, mat3_identity);
				}

				const float interval = Max(BSE_TIME_EPSILON, AttenuateInterval(effect, st));
				spawnTime += interval;
				++spawned;
			}
		}

		if (!infinite && mSegEndTime - BSE_TIME_EPSILON <= spawnEnd) {
			SetExpired(true);
		}
		mLastTime = spawnTime;
		if (spawned >= maxSpawnPerService && spawnTime < spawnEnd && bse_debug.GetInteger() > 0) {
			common->Warning("^4BSE:^1 spawn service cap hit for '%s' segment %d", effect->GetDeclName(), mSegmentTemplateHandle);
		}
		return true;
	}
	case SEG_TRAIL:
		SetExpired(true);
		return true;
	case SEG_SPAWNER:
		if (!GetExpired() && st->GetHasParticles()) {
			const int particleCap = GetSegmentParticleCap();
			const int count = idMath::ClampInt(
				0,
				particleCap,
				static_cast<int>(idMath::Ceil(AttenuateCount(effect, st, mCount.x, mCount.y))));
			if (count > 0) {
				SpawnParticles(effect, st, mSegStartTime, count);
			}
			SetExpired(true);
		}
		return true;
	case SEG_EFFECT:
		if (!GetExpired() && st->mNumEffects > 0 && game) {
			const int index = rvRandom::irand(0, st->mNumEffects - 1);
			const rvDeclEffect* nested = st->mEffects[index];
			if (nested) {
				game->PlayEffect(
					nested,
					effect->GetCurrentOrigin(),
					effect->GetCurrentAxis(),
					false,
					effect->GetHasEndOrigin() ? effect->GetCurrentEndOrigin() : vec3_origin,
					false,
					false,
					EC_IGNORE,
					vec4_one);
			}
			SetExpired(true);
		}
		return true;
	case SEG_SOUND:
		if (effect->GetReferenceSound() && st->mSoundShader) {
			if (!GetSoundPlaying()) {
				effect->GetReferenceSound()->StartSound(
					st->mSoundShader,
					static_cast<s_channelType>(mSegmentTemplateHandle + SCHANNEL_ONE),
					0.0f,
					0,
					false);
				SetSoundPlaying(true);
			}
			mSoundVolume = st->GetSoundVolume();
			mFreqShift = st->GetFreqShift();
			effect->UpdateSoundEmitter(st, this);
		}
		SetExpired(true);
		return true;
	case SEG_DECAL:
		if (!GetExpired()) {
			CreateDecal(effect, mSegStartTime);
			SetExpired(true);
		}
		return true;
	case SEG_DELAY:
		SetExpired(true);
		return true;
	case SEG_SHAKE:
	case SEG_TUNNEL:
	case SEG_DOUBLEVISION:
		if (!GetExpired() && game) {
			int viewEffect = VIEWEFFECT_SHAKE;
			if (st->mSegType == SEG_TUNNEL) {
				viewEffect = VIEWEFFECT_TUNNEL;
			}
			else if (st->mSegType == SEG_DOUBLEVISION) {
				viewEffect = VIEWEFFECT_DOUBLEVISION;
			}
			const float finishTime = mSegStartTime + AttenuateDuration(effect, st);
			const float scale = Max(0.0f, effect->GetOriginAttenuation(st));
			game->StartViewEffect(viewEffect, finishTime, scale);
			SetExpired(true);
		}
		return true;
	case SEG_LIGHT:
		if (!GetExpired() && st->GetEnabled()) {
			InitLight(effect, st, mSegStartTime);
			SetExpired(true);
		}
		return true;
	default:
		return true;
	}
}

bool rvSegment::UpdateParticles(rvBSE* effect, float time) {
	rvSegmentTemplate* st = (rvSegmentTemplate*)mEffectDecl->GetSegmentTemplate(mSegmentTemplateHandle);
	if (!st || !st->GetEnabled()) {
		return false;
	}

	Handle(effect, time);

	if (st->GetInfiniteDuration() || st->GetSmoker() || st->GetHasPhysics() || st->mParticleTemplate.GetNumTimeoutEffects() > 0) {
		UpdateGenericParticles(effect, st, time);
	}
	else {
		UpdateSimpleParticles(time);
	}

	return mUsedHead != NULL;
}

void rvSegment::CalcCounts(rvBSE* effect, float time) {
	mParticleCount = 0;
	mLoopParticleCount = 0;

	rvSegmentTemplate* st = (rvSegmentTemplate*)mEffectDecl->GetSegmentTemplate(mSegmentTemplateHandle);
	if (!st || !st->GetHasParticles()) {
		return;
	}
	if (st->DetailCull()) {
		return;
	}

	const int particleType = st->mParticleTemplate.GetType();
	const float particleMaxDuration = st->mParticleTemplate.GetMaxDuration() + 0.016f;
	const float effectMinDuration = mEffectDecl ? mEffectDecl->GetMinDuration() : 0.0f;
	const int particleCap = GetSegmentParticleCap();
	float trailDuration = 0.0f;

	int count = 0;
	int loopCount = 0;
	switch (st->mSegType) {
	case SEG_EMITTER:
		if (particleType == PTYPE_DEBRIS) {
			count = 1;
			loopCount = 1;
		}
		else if (mSecondsPerParticle.y > BSE_TIME_EPSILON) {
			const float baseDuration = Min(particleMaxDuration, st->mLocalDuration.y) + 0.016f;
			trailDuration = baseDuration;
			count = static_cast<int>(idMath::Ceil(baseDuration / mSecondsPerParticle.y)) + 1;
			loopCount = count;
			if (effectMinDuration > BSE_TIME_EPSILON && effectMinDuration < particleMaxDuration) {
				loopCount = static_cast<int>(idMath::Ceil((static_cast<float>(count) / effectMinDuration) * particleMaxDuration)) + 1;
			}
		}
		break;
	case SEG_SPAWNER:
		if (particleType == PTYPE_DEBRIS) {
			count = 1;
			loopCount = 1;
		}
		else {
			count = static_cast<int>(idMath::Ceil(mCount.y));
			loopCount = count;
			if (!st->GetInfiniteDuration() && effectMinDuration > BSE_TIME_EPSILON && effectMinDuration < particleMaxDuration) {
				const int extraLoops = static_cast<int>(idMath::Ceil(particleMaxDuration / effectMinDuration)) + 1;
				loopCount = count * extraLoops + 1;
			}
		}
		break;
	case SEG_DECAL:
	case SEG_LIGHT:
		count = 1;
		loopCount = 1;
		break;
	default:
		break;
	}

	mParticleCount = idMath::ClampInt(0, particleCap, count);
	mLoopParticleCount = idMath::ClampInt(0, particleCap, loopCount);

	if (st->mSegType == SEG_EMITTER || st->mSegType == SEG_SPAWNER) {
		CalcTrailCounts(effect, st, &st->mParticleTemplate, trailDuration);
	}

	if (!effect->GetLooping()) {
		effect->SetDuration((mSegEndTime - time) + st->mParticleTemplate.GetMaxDuration());
	}

}

void rvSegment::AllocateSurface(rvBSE* effect, idRenderModel* model) {
	mSurfaceIndex = -1;
	if (!effect || !model) {
		return;
	}

	rvSegmentTemplate* st = GetSegmentTemplate();
	if (!st || !st->GetEnabled() || !st->GetHasParticles() || st->DetailCull()) {
		return;
	}

	rvParticleTemplate* pt = st->GetParticleTemplate();
	if (!pt || pt->GetType() == PTYPE_NONE) {
		return;
	}

	int particleCount = effect->GetLooping() ? mLoopParticleCount : mParticleCount;
	if (particleCount <= 0) {
		// Some effects end up with zero precomputed counts at init time but still
		// spawn particles at runtime. Allocate a minimal surface lazily so render
		// has writable geometry for active particles.
		particleCount = Max(1, mActiveCount);
	}
	if (particleCount <= 0) {
		return;
	}

	const int maxVerts = idMath::ClampInt(4, 10000, particleCount * pt->GetVertexCount());
	const int maxIndexes = idMath::ClampInt(6, 30000, particleCount * pt->GetIndexCount());
	srfTriangles_t* tri = model->AllocSurfaceTriangles(maxVerts, maxIndexes);
	if (!tri) {
		return;
	}
	tri->numVerts = 0;
	tri->numIndexes = 0;

	modelSurface_t surf;
	surf.id = 0;
	surf.geometry = tri;
	surf.shader = pt->GetMaterial() ? pt->GetMaterial() : declManager->FindMaterial("_default");
	model->AddSurface(surf);
	mSurfaceIndex = model->NumSurfaces() - 1;

	const bool motionTrail = (pt->GetTrailType() == TRAIL_MOTION) && (pt->GetMaxTrailCount() > 0 || pt->GetMaxTrailTime() >= BSE_TIME_EPSILON);
	if (motionTrail) {
		const int trailVerts = idMath::ClampInt(4, 10000, particleCount * pt->GetMaxTrailCount() * 2 + 2);
		const int trailIndexes = idMath::ClampInt(6, 30000, particleCount * pt->GetMaxTrailCount() * 12);
		srfTriangles_t* trailTri = model->AllocSurfaceTriangles(trailVerts, trailIndexes);
		if (trailTri) {
			trailTri->numVerts = 0;
			trailTri->numIndexes = 0;

			modelSurface_t trailSurf;
			trailSurf.id = 0;
			trailSurf.geometry = trailTri;
			trailSurf.shader = pt->GetTrailMaterial() ? pt->GetTrailMaterial() : surf.shader;
			model->AddSurface(trailSurf);
			SetHasMotionTrail(true);
		}
	}
}

void rvSegment::CreateDecal(rvBSE* effect, float time) {
	if (!effect || !session || !session->rw) {
		return;
	}

	rvSegmentTemplate* st = GetSegmentTemplate();
	if (!st || !st->GetHasParticles()) {
		return;
	}

	rvParticleTemplate* pt = st->GetParticleTemplate();
	if (!pt || !pt->GetMaterial()) {
		return;
	}

	float sizeParms[3] = { 32.0f, 32.0f, 8.0f };
	if (pt->mpSpawnSize) {
		pt->mpSpawnSize->Spawn(sizeParms, *pt->mpSpawnSize, NULL, NULL);
	}

	sizeParms[0] = Max(1.0f, idMath::Fabs(sizeParms[0]));
	sizeParms[1] = Max(1.0f, idMath::Fabs(sizeParms[1]));
	sizeParms[2] = Max(1.0f, idMath::Fabs(sizeParms[2]));

	float rotateParms[3] = { 0.0f, 0.0f, 0.0f };
	if (pt->mpSpawnRotate) {
		pt->mpSpawnRotate->Spawn(rotateParms, *pt->mpSpawnRotate, NULL, NULL);
	}
	const float rotateRad = rotateParms[0] * idMath::TWO_PI;

	idVec3 origin = effect->GetCurrentOrigin();
	idMat3 axis = effect->GetCurrentAxis();

	idVec3 normal = axis[0];
	idVec3 right = axis[1];
	idVec3 up = axis[2];

	if (rotateRad != 0.0f) {
		const float s = idMath::Sin(rotateRad);
		const float c = idMath::Cos(rotateRad);
		const idVec3 rotatedRight = right * c - up * s;
		const idVec3 rotatedUp = right * s + up * c;
		right = rotatedRight;
		up = rotatedUp;
	}

	const idVec3 halfRight = right * (sizeParms[0] * 0.5f);
	const idVec3 halfUp = up * (sizeParms[1] * 0.5f);

	idFixedWinding winding;
	winding.Clear();
	winding += origin + halfRight + halfUp;
	winding += origin - halfRight + halfUp;
	winding += origin - halfRight - halfUp;
	winding += origin + halfRight - halfUp;

	const idVec3 projectionOrigin = origin - normal * (sizeParms[2] * 0.5f);
	const int startTimeMs = static_cast<int>(time * 1000.0f);
	session->rw->ProjectDecalOntoWorld(
		winding,
		projectionOrigin,
		false,
		sizeParms[2],
		pt->GetMaterial(),
		startTimeMs);
}

bool rvSegment::GetLocked(void) {
	rvSegmentTemplate* st = (rvSegmentTemplate*)mEffectDecl->GetSegmentTemplate(mSegmentTemplateHandle);
	return st ? st->GetLocked() : false;
}

void rvSegment::Handle(rvBSE* effect, float time) {
	rvSegmentTemplate* st = (rvSegmentTemplate*)mEffectDecl->GetSegmentTemplate(mSegmentTemplateHandle);
	if (!st || time < mSegStartTime) {
		return;
	}

	switch (st->mSegType) {
	case SEG_EMITTER:
	case SEG_SPAWNER:
		if (effect->GetEndOriginChanged()) {
			RefreshParticles(effect, st);
		}
		break;
	case SEG_SOUND:
		effect->UpdateSoundEmitter(st, this);
		break;
	case SEG_LIGHT:
		if (st->GetEnabled()) {
			HandleLight(effect, st, time);
		}
		break;
	default:
		break;
	}
}

bool rvSegment::Active() {
	rvSegmentTemplate* st = GetSegmentTemplate();
	if (!st || !st->GetEnabled() || !st->GetHasParticles()) {
		return false;
	}
	return mUsedHead != NULL;
}

void rvSegment::InitLight(rvBSE* effect, rvSegmentTemplate* st, float time) {
	if (!effect || !st || !st->GetHasParticles()) {
		return;
	}
	if (!mUsedHead) {
		SpawnParticle(effect, st, time);
	}
	if (mUsedHead) {
		mUsedHead->InitLight(effect, st, time);
		mActiveCount = 1;
	}
}

bool rvSegment::HandleLight(rvBSE* effect, rvSegmentTemplate* st, float time) {
	if (!effect || !st || !st->GetEnabled()) {
		return false;
	}
	if (!mUsedHead) {
		InitLight(effect, st, time);
		if (!mUsedHead) {
			return false;
		}
	}

	const bool infinite = st->GetInfiniteDuration() || st->GetSoundLooping();
	const bool presented = mUsedHead->PresentLight(effect, st->GetParticleTemplate(), time, infinite);

	if (!infinite && mUsedHead && mUsedHead->GetEndTime() - BSE_TIME_EPSILON <= time) {
		rvParticle* expired = mUsedHead;
		expired->Destroy();
		expired->SetNext(mFreeHead);
		mFreeHead = expired;
		mUsedHead = NULL;
		mActiveCount = 0;
		return false;
	}

	return presented;
}

void rvSegment::ClearSurface(rvBSE* effect, idRenderModel* model) {
	if (!model || mSurfaceIndex < 0 || mSurfaceIndex >= model->NumSurfaces()) {
		return;
	}

	srfTriangles_t* tri = model->Surface(mSurfaceIndex)->geometry;
	if (tri) {
		tri->numVerts = 0;
		tri->numIndexes = 0;
	}

	if (GetHasMotionTrail() && mSurfaceIndex + 1 < model->NumSurfaces()) {
		srfTriangles_t* trailTri = model->Surface(mSurfaceIndex + 1)->geometry;
		if (trailTri) {
			trailTri->numVerts = 0;
			trailTri->numIndexes = 0;
		}
	}
}

void rvSegment::RenderTrail(rvBSE* effect, const renderEffect_s* owner, idRenderModel* model, float time) {
	rvSegmentTemplate* st = GetSegmentTemplate();
	if (!st || !GetHasMotionTrail()) {
		return;
	}
	rvParticleTemplate* pt = st->GetParticleTemplate();
	if (!pt || pt->GetTrailType() != TRAIL_MOTION) {
		return;
	}
	RenderMotion(effect, owner, model, pt, time);
}

void rvSegment::Render(rvBSE* effect, const renderEffect_s* owner, idRenderModel* model, float time) {
	if (!effect || !owner || !model) {
		return;
	}

	rvSegmentTemplate* st = GetSegmentTemplate();
	if (!st || !st->GetEnabled() || !st->GetHasParticles() || st->DetailCull()) {
		return;
	}

	rvParticleTemplate* pt = st->GetParticleTemplate();
	if (!pt) {
		return;
	}
	if (mSurfaceIndex < 0 || mSurfaceIndex >= model->NumSurfaces()) {
		AllocateSurface(effect, model);
		if (mSurfaceIndex < 0 || mSurfaceIndex >= model->NumSurfaces()) {
			return;
		}
	}

	srfTriangles_t* tri = model->Surface(mSurfaceIndex)->geometry;
	if (!tri) {
		return;
	}

	const int maxVerts = tri->numAllocedVerts > 0 ? tri->numAllocedVerts : 0;
	const int maxIndexes = tri->numAllocedIndices > 0 ? tri->numAllocedIndices : 0;
	if (maxVerts <= 0 || maxIndexes <= 0) {
		return;
	}
	tri->numVerts = 0;
	tri->numIndexes = 0;
	if (!mUsedHead) {
		return;
	}

	idMat3 view;
	const idMat3 ownerAxisTranspose = owner->axis.Transpose();
	view[1] = ownerAxisTranspose * effect->GetViewAxis()[1];
	view[2] = ownerAxisTranspose * effect->GetViewAxis()[2];
	view[0] = ownerAxisTranspose * (effect->GetViewOrg() - owner->origin);

	int rendered = 0;
	const int particleType = pt->GetType();
	const bool linkedStrip = (particleType == PTYPE_LINKED || particleType == PTYPE_ORIENTEDLINKED);
	const bool depthSort = st->GetDepthSort() && !linkedStrip;
	const bool inverseOrder = st->GetInverseDrawOrder();

	auto renderParticle = [&](rvParticle* p) -> bool {
		if (!p) {
			return false;
		}
		if (st->GetInfiniteDuration()) {
			// Vanilla behavior keeps looping segment particles renderable by
			// continuously pushing end-time forward each frame.
			p->ExtendLife(time + 1.0f);
		}

		int requiredIndexes = pt->GetIndexCount();
		if (linkedStrip && tri->numVerts == 0) {
			requiredIndexes = 0;
		}

		if (tri->numVerts + pt->GetVertexCount() > maxVerts || tri->numIndexes + requiredIndexes > maxIndexes) {
			return false;
		}
		if (p->Render(effect, pt, view, tri, time, 1.0f)) {
			++rendered;
			if (pt->GetTrailType() == TRAIL_BURN) {
				p->RenderBurnTrail(effect, pt, view, tri, time);
			}
		}
		return true;
	};

	if (!depthSort && !inverseOrder) {
		for (rvParticle* p = mUsedHead; p; p = p->GetNext()) {
			if (!renderParticle(p)) {
				break;
			}
		}
	}
	else {
		idList<rvParticle*> drawList;
		drawList.Clear();
		drawList.SetGranularity(64);
		for (rvParticle* p = mUsedHead; p; p = p->GetNext()) {
			drawList.Append(p);
		}
		const int drawCount = drawList.Num();

		if (depthSort) {
			const idVec3 eyePos = view[0];
			for (int i = 1; i < drawCount; ++i) {
				rvParticle* key = drawList[i];
				const float keyDist = key->UpdateViewDist(eyePos);
				int j = i - 1;

				while (j >= 0) {
					const float curDist = drawList[j]->UpdateViewDist(eyePos);
					if (curDist >= keyDist) {
						break;
					}
					drawList[j + 1] = drawList[j];
					--j;
				}

				drawList[j + 1] = key;
			}
		}

		if (inverseOrder) {
			for (int i = drawCount - 1; i >= 0; --i) {
				if (!renderParticle(drawList[i])) {
					break;
				}
			}
		}
		else {
			for (int i = 0; i < drawCount; ++i) {
				if (!renderParticle(drawList[i])) {
					break;
				}
			}
		}
	}
	R_BoundTriSurf(tri);
	BSE_AddRendered(rendered);
	mActiveCount = rendered;
}

