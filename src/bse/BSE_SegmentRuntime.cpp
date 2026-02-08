// Copyright (C) 2007 Id Software, Inc.
//

#include "BSE_Envelope.h"
#include "BSE_Particle.h"
#include "BSE.h"
#include "../renderer/tr_local.h"

extern idCVar bse_frameCounters;

namespace {
rvParticle* AllocateParticleArray(int particleType, int count) {
	if (count <= 0) {
		return NULL;
	}

	switch (particleType) {
	case PTYPE_LINE:
		return static_cast<rvParticle*>(new rvLineParticle[count]);
	case PTYPE_ORIENTED:
		return static_cast<rvParticle*>(new rvOrientedParticle[count]);
	case PTYPE_DECAL:
		return static_cast<rvParticle*>(new rvDecalParticle[count]);
	case PTYPE_MODEL:
		return static_cast<rvParticle*>(new rvModelParticle[count]);
	case PTYPE_LIGHT:
		return static_cast<rvParticle*>(new rvLightParticle[count]);
	case PTYPE_ELECTRICITY:
		return static_cast<rvParticle*>(new rvElectricityParticle[count]);
	case PTYPE_LINKED:
		return static_cast<rvParticle*>(new rvLinkedParticle[count]);
	case PTYPE_ORIENTEDLINKED:
		return static_cast<rvParticle*>(new sdOrientedLinkedParticle[count]);
	case PTYPE_DEBRIS:
		return static_cast<rvParticle*>(new rvDebrisParticle[count]);
	default:
		return static_cast<rvParticle*>(new rvSpriteParticle[count]);
	}
}

ID_INLINE void InsertByEndTime(rvParticle*& head, rvParticle* particle) {
	rvParticle* prev = NULL;
	rvParticle* cur = head;
	while (cur && particle->GetEndTime() > cur->GetEndTime()) {
		prev = cur;
		cur = cur->GetNext();
	}
	particle->SetNext(cur);
	if (prev) {
		prev->SetNext(particle);
	}
	else {
		head = particle;
	}
}

ID_INLINE int GetSegmentParticleCap() {
	return idMath::ClampInt(0, MAX_PARTICLES, bse_maxParticles.GetInteger());
}

ID_INLINE bool BSESpawnTraceEnabled() {
	return bse_frameCounters.GetInteger() >= 2;
}

void BSESpawnTrace(const char* fmt, ...) {
	static int spawnTraceCount = 0;
	if (spawnTraceCount >= 256) {
		return;
	}
	va_list ap;
	va_start(ap, fmt);
	common->VPrintf(fmt, ap);
	va_end(ap);
	++spawnTraceCount;
}
}

void rvSegment::PlayEffect(rvBSE* effect, rvSegmentTemplate* st, float depthOffset) {
	if (!effect || !st || st->mNumEffects <= 0 || !game) {
		return;
	}

	const int index = rvRandom::irand(0, st->mNumEffects - 1);
	const rvDeclEffect* nested = st->mEffects[index];
	if (!nested) {
		return;
	}

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

rvParticle* rvSegment::InitParticleArray(rvBSE* effect) {
	if (!effect) {
		return NULL;
	}

	if (mParticles) {
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
	}
	mParticles = NULL;
	mFreeHead = NULL;
	mUsedHead = NULL;

	const rvSegmentTemplate* st = GetSegmentTemplate();
	const int requestedCount = effect->GetLooping() ? mLoopParticleCount : mParticleCount;
	int particleCount = requestedCount;
	const int particleCap = GetSegmentParticleCap();
	if (particleCount > particleCap) {
		common->Warning("^4BSE:^1 '%s' exceeded particle cap (%d > %d), clamping", effect->GetDeclName(), particleCount, particleCap);
	}
	particleCount = idMath::ClampInt(0, particleCap, particleCount);
	if (particleCount <= 0) {
		if (BSESpawnTraceEnabled()) {
			BSESpawnTrace(
				"BSE spawn init: decl=%s segType=%d handle=%d particleCount=%d loopCount=%d (clamped<=0)\n",
				effect ? effect->GetDeclName() : "<null>",
				st ? st->GetType() : -1,
				mSegmentTemplateHandle,
				mParticleCount,
				mLoopParticleCount);
		}
		return NULL;
	}

	mParticles = AllocateParticleArray(mParticleType, particleCount);
	if (!mParticles) {
		return NULL;
	}

	for (int i = 0; i < particleCount - 1; ++i) {
		rvParticle* cur = mParticles->GetArrayEntry(i);
		rvParticle* next = mParticles->GetArrayEntry(i + 1);
		cur->SetNext(next);
	}
	mParticles->GetArrayEntry(particleCount - 1)->SetNext(NULL);

	mFreeHead = mParticles;
	mUsedHead = NULL;
	return mParticles;
}

rvParticle* rvSegment::GetFreeParticle(rvBSE* effect) {
	rvSegmentTemplate* st = GetSegmentTemplate();
	if (!st) {
		return NULL;
	}

	rvParticle* particle = NULL;
	if (st->GetTemporary()) {
		particle = mParticles;
		if (particle) {
			particle->SetNext(NULL);
		}
	}
	else {
		particle = mFreeHead;
		if (!particle) {
			return NULL;
		}
		mFreeHead = particle->GetNext();
		particle->SetNext(NULL);
	}

	return particle;
}

rvParticle* rvSegment::SpawnParticle(rvBSE* effect, rvSegmentTemplate* st, float birthTime, const idVec3& initOffset, const idMat3& initAxis) {
	if (!effect || !st) {
		return NULL;
	}

	rvParticle* particle = GetFreeParticle(effect);
	if (!particle) {
		if (BSESpawnTraceEnabled()) {
			BSESpawnTrace(
				"BSE spawn miss: decl=%s segType=%d handle=%d birth=%.4f freeHead=null particleCount=%d loopCount=%d\n",
				effect ? effect->GetDeclName() : "<null>",
				st ? st->GetType() : -1,
				mSegmentTemplateHandle,
				birthTime,
				mParticleCount,
				mLoopParticleCount);
		}
		return NULL;
	}

	const float fraction = birthTime - effect->GetStartTime();
	particle->FinishSpawn(effect, this, birthTime, fraction, initOffset, initAxis);
	BSE_AddSpawned(1);
	if (BSESpawnTraceEnabled()) {
		BSESpawnTrace(
			"BSE spawn ok: decl=%s segType=%d handle=%d birth=%.4f end=%.4f dur=%.4f fraction=%.4f\n",
			effect ? effect->GetDeclName() : "<null>",
			st ? st->GetType() : -1,
			mSegmentTemplateHandle,
			birthTime,
			particle->GetEndTime(),
			particle->GetDuration(),
			fraction);
	}

	const int type = st->mParticleTemplate.GetType();
	const bool linked = (type == PTYPE_LINKED || type == PTYPE_ORIENTEDLINKED);
	if (st->GetTemporary()) {
		mUsedHead = particle;
		particle->SetNext(NULL);
	}
	else if (linked || !st->GetComplexParticle()) {
		InsertByEndTime(mUsedHead, particle);
	}
	else {
		particle->SetNext(mUsedHead);
		mUsedHead = particle;
	}

	return particle;
}

void rvSegment::SpawnParticles(rvBSE* effect, rvSegmentTemplate* st, float birthTime, int count) {
	if (!effect || !st || count <= 0) {
		return;
	}

	for (int i = 0; i < count; ++i) {
		rvParticle* particle = GetFreeParticle(effect);
		if (!particle) {
			if (BSESpawnTraceEnabled()) {
				BSESpawnTrace(
					"BSE spawn batch miss: decl=%s segType=%d handle=%d birth=%.4f idx=%d count=%d freeHead=null particleCount=%d loopCount=%d\n",
					effect ? effect->GetDeclName() : "<null>",
					st ? st->GetType() : -1,
					mSegmentTemplateHandle,
					birthTime,
					i,
					count,
					mParticleCount,
					mLoopParticleCount);
			}
			break;
		}

		float fraction = 0.0f;
		if (count > 0) {
			fraction = static_cast<float>(i) / static_cast<float>(count);
		}
		particle->FinishSpawn(effect, this, birthTime, fraction, vec3_origin, mat3_identity);
		BSE_AddSpawned(1);
		if (BSESpawnTraceEnabled()) {
			BSESpawnTrace(
				"BSE spawn batch ok: decl=%s segType=%d handle=%d birth=%.4f idx=%d count=%d end=%.4f dur=%.4f fraction=%.4f\n",
				effect ? effect->GetDeclName() : "<null>",
				st ? st->GetType() : -1,
				mSegmentTemplateHandle,
				birthTime,
				i,
				count,
				particle->GetEndTime(),
				particle->GetDuration(),
				fraction);
		}

		const int type = st->mParticleTemplate.GetType();
		const bool linked = (type == PTYPE_LINKED || type == PTYPE_ORIENTEDLINKED);
		if (st->GetTemporary()) {
			mUsedHead = particle;
			particle->SetNext(NULL);
		}
		else if (linked || !st->GetComplexParticle()) {
			InsertByEndTime(mUsedHead, particle);
		}
		else {
			particle->SetNext(mUsedHead);
			mUsedHead = particle;
		}
	}
}

void rvSegment::UpdateSimpleParticles(float time) {
	rvSegmentTemplate* st = GetSegmentTemplate();
	if (!st) {
		mActiveCount = 0;
		return;
	}

	rvParticle* prev = NULL;
	rvParticle* p = mUsedHead;
	mActiveCount = 0;

	while (p) {
		rvParticle* next = p->GetNext();
		if (p->GetEndTime() - BSE_TIME_EPSILON > time) {
			mActiveCount += p->Update(&st->mParticleTemplate, time);
			prev = p;
		}
		else {
			if (prev) {
				prev->SetNext(next);
			}
			else {
				mUsedHead = next;
			}
			p->SetNext(mFreeHead);
			p->Destroy();
			mFreeHead = p;
		}
		p = next;
	}
}

void rvSegment::UpdateGenericParticles(rvBSE* effect, rvSegmentTemplate* st, float time) {
	if (!effect || !st) {
		mActiveCount = 0;
		return;
	}

	const bool infinite = st->GetInfiniteDuration();
	const bool smoker = st->GetSmoker();

	rvParticle* prev = NULL;
	rvParticle* p = mUsedHead;
	mActiveCount = 0;

	while (p) {
		rvParticle* next = p->GetNext();
		bool remove = false;

		if (infinite) {
			p->RunPhysics(effect, st, time);
			remove = effect->GetStopped();
		}
		else {
			if (p->GetEndTime() - BSE_TIME_EPSILON <= time) {
				p->CheckTimeoutEffect(effect, st, time);
				remove = true;
			}
			else {
				remove = p->RunPhysics(effect, st, time);
			}
		}

		if (effect->GetStopped() && (p->GetFlags() & PTFLAG_PERSIST) == 0) {
			remove = true;
		}

		if (smoker && st->mTrailSegmentIndex >= 0) {
			rvSegment* trail = effect->GetTrailSegment(st->mTrailSegmentIndex);
			if (trail) {
				p->EmitSmokeParticles(effect, trail, &st->mParticleTemplate, time);
			}
		}

		if (remove) {
			if (prev) {
				prev->SetNext(next);
			}
			else {
				mUsedHead = next;
			}
			p->SetNext(mFreeHead);
			p->Destroy();
			mFreeHead = p;
		}
		else {
			mActiveCount += p->Update(st->GetParticleTemplate(), time);
			prev = p;
		}

		p = next;
	}
}

void rvSegment::AddToParticleCount(rvBSE* effect, int count, int loopCount, float duration) {
	rvSegmentTemplate* st = GetSegmentTemplate();
	if (!st) {
		return;
	}

	if (duration < st->mParticleTemplate.GetMaxDuration()) {
		duration = st->mParticleTemplate.GetMaxDuration();
	}

	const float countDuration = duration + 0.016f;
	if (mSecondsPerParticle.y > BSE_TIME_EPSILON) {
		const int mult = static_cast<int>(ceilf(countDuration / mSecondsPerParticle.y)) + 1;
		mLoopParticleCount += loopCount * mult;
		mParticleCount += count * mult;
	}
	else {
		mLoopParticleCount += loopCount;
		mParticleCount += count;
	}

	const int particleCap = GetSegmentParticleCap();
	mParticleCount = idMath::ClampInt(0, particleCap, mParticleCount);
	mLoopParticleCount = idMath::ClampInt(0, particleCap, mLoopParticleCount);
}

void rvSegment::CalcTrailCounts(rvBSE* effect, rvSegmentTemplate* st, rvParticleTemplate* pt, float duration) {
	if (!effect || !st || !pt) {
		return;
	}
	if (st->mTrailSegmentIndex >= 0) {
		rvSegment* trail = effect->GetTrailSegment(st->mTrailSegmentIndex);
		if (trail) {
			trail->AddToParticleCount(effect, mParticleCount, mLoopParticleCount, duration);
		}
	}
}

void rvSegment::RenderMotion(rvBSE* effect, const renderEffect_s* owner, idRenderModel* model, rvParticleTemplate* pt, float time) {
	if (!effect || !owner || !model || !pt || mSurfaceIndex < 0) {
		return;
	}
	if (mSurfaceIndex + 1 >= model->NumSurfaces()) {
		return;
	}

	srfTriangles_t* tri = model->Surface(mSurfaceIndex + 1)->geometry;
	if (!tri) {
		return;
	}

	for (rvParticle* p = mUsedHead; p; p = p->GetNext()) {
		p->RenderMotion(effect, pt, tri, owner, time, pt->GetTrailScale());
	}

	R_BoundTriSurf(tri);
}

void rvSegment::Sort(const idVec3& eyePos) {
	if (!mUsedHead || !mUsedHead->GetNext()) {
		return;
	}

	rvParticle* sorted = NULL;
	rvParticle* particle = mUsedHead;
	while (particle) {
		rvParticle* next = particle->GetNext();
		const float dist = particle->UpdateViewDist(eyePos);

		if (!sorted || dist > sorted->UpdateViewDist(eyePos)) {
			particle->SetNext(sorted);
			sorted = particle;
		}
		else {
			rvParticle* insertAfter = sorted;
			while (insertAfter->GetNext()) {
				rvParticle* compare = insertAfter->GetNext();
				if (dist > compare->UpdateViewDist(eyePos)) {
					break;
				}
				insertAfter = compare;
			}
			particle->SetNext(insertAfter->GetNext());
			insertAfter->SetNext(particle);
		}

		particle = next;
	}

	mUsedHead = sorted;
}
