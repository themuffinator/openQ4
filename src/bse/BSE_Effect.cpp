// BSE_Effect.cpp
//




#include "BSE_Envelope.h"
#include "BSE_Particle.h"
#include "BSE.h"
#include "BSE_SpawnDomains.h"

#include "../renderer/Model_local.h"
#include "../renderer/tr_local.h"
#include "../sound/sound.h"
//#include "../renderer/RenderCommon.h"

void rvBSE::Init(const rvDeclEffect* declEffect, renderEffect_s* parms, float time)
{
	int v6; // esi
	float* v7; // ecx
	float* v8; // eax
	double v9; // st3
	double v10; // st5
	float* v11; // esi
	float* v12; // eax
	int v13; // ecx
	double v14; // st5
	//float v15; // [esp+18h] [ebp-30h]
	//float v16; // [esp+1Ch] [ebp-2Ch]
	//float v17; // [esp+20h] [ebp-28h]
	//float v18; // [esp+24h] [ebp-24h]
	//float v19; // [esp+28h] [ebp-20h]
	//float v20; // [esp+2Ch] [ebp-1Ch]
	//float v21; // [esp+30h] [ebp-18h] BYREF
	//float v22[5]; // [esp+34h] [ebp-14h] BYREF
	float parmsa; // [esp+50h] [ebp+8h]

	this->mStartTime = time;
	this->mDeclEffect = declEffect;
	this->mLastTime = time;
	this->mFlags = 0;
	this->mDuration = 0.0;
	this->mAttenuation = 1.0;
	v6 = 2;
	this->mCost = 0.0;
	this->mFlags = parms->loop;
	this->mCurrentLocalBounds[1].z = 0.0;
	this->mCurrentLocalBounds[1].y = 0.0;
	v7 = &this->mLastRenderBounds[0].z;
	this->mCurrentLocalBounds[1].x = 0.0;
	this->mCurrentLocalBounds[0].z = 0.0;
	this->mCurrentLocalBounds[0].y = 0.0;
	this->mCurrentLocalBounds[0].x = 0.0;
	v8 = &this->mCurrentLocalBounds[0].z;
	v9 = declEffect->mSize;
	this->mCurrentLocalBounds[0].x = this->mCurrentLocalBounds[0].x - v9;
	this->mCurrentLocalBounds[0].y = this->mCurrentLocalBounds[0].y - v9;
	this->mCurrentLocalBounds[0].z = this->mCurrentLocalBounds[0].z - v9;
	this->mCurrentLocalBounds[1].x = this->mCurrentLocalBounds[1].x + v9;
	this->mCurrentLocalBounds[1].y = v9 + this->mCurrentLocalBounds[1].y;
	this->mCurrentLocalBounds[1].z = v9 + this->mCurrentLocalBounds[1].z;
	do
	{
		v10 = *(v8 - 2);
		v8 += 3;
		*(v7 - 2) = v10;
		v7 += 3;
		--v6;
		*(v7 - 4) = *(v8 - 4);
		*(v7 - 3) = *(v8 - 3);
	} while (v6);
	this->mGrownRenderBounds[0].z = 1.0e30;
	this->mGrownRenderBounds[0].y = 1.0e30;
	this->mGrownRenderBounds[0].x = 1.0e30;
	parmsa = -1.0e30;
	this->mGrownRenderBounds[1].z = parmsa;
	this->mGrownRenderBounds[1].y = parmsa;
	this->mGrownRenderBounds[1].x = parmsa;
	this->mForcePush = 0;
	this->mOriginalOrigin.x = parms->origin.x;
	this->mOriginalOrigin.y = parms->origin.y;
	this->mOriginalOrigin.z = parms->origin.z;
	this->mOriginalEndOrigin.z = 0.0;
	this->mOriginalEndOrigin.y = 0.0;
	this->mOriginalEndOrigin.x = 0.0;
	memcpy(&this->mOriginalAxis, &parms->axis, sizeof(this->mOriginalAxis));
	//v18 = this->mOriginalOrigin.x + this->mCurrentLocalBounds.b[1].x;
	//v19 = this->mCurrentLocalBounds[1].y + this->mOriginalOrigin.y;
	//v11 = &v21;
	//v20 = this->mCurrentLocalBounds[1].z + this->mOriginalOrigin.z;
	//v12 = &this->mCurrentWorldBounds[0].y;
	//v13 = 2;
	//v15 = this->mOriginalOrigin.x + this->mCurrentLocalBounds.b[0].x;
	//v16 = this->mCurrentLocalBounds[0].y + this->mOriginalOrigin.y;
	//v17 = this->mCurrentLocalBounds[0].z + this->mOriginalOrigin.z;
	//v21 = v15;
	//v22[0] = v16;
	//v22[1] = v17;
	//v22[2] = v18;
	//v22[3] = v19;
	//v22[4] = v20;
	//do
	//{
	//	v14 = *v11;
	//	v11 += 3;
	//	*(v12 - 1) = v14;
	//	v12 += 3;
	//	--v13;
	//	*(v12 - 3) = *(float*)((char*)v12 + (char*)&v21 - (char*)&this->mCurrentWorldBounds - 12);
	//	*(v12 - 2) = *(float*)((char*)v12 + (char*)v22 - (char*)&this->mCurrentWorldBounds - 12);
	//} while (v13);
	if (parms->hasEndOrigin)
	{
		this->mFlags |= 2u;
		this->mOriginalEndOrigin.x = parms->endOrigin.x;
		this->mOriginalEndOrigin.y = parms->endOrigin.y;
		this->mOriginalEndOrigin.z = parms->endOrigin.z;
		this->mCurrentEndOrigin.x = parms->endOrigin.x;
		this->mCurrentEndOrigin.y = parms->endOrigin.y;
		this->mCurrentEndOrigin.z = parms->endOrigin.z;
	}
	this->mCurrentTime = time;
	this->mCurrentOrigin.x = this->mOriginalOrigin.x;
	this->mCurrentOrigin.y = this->mOriginalOrigin.y;
	this->mCurrentOrigin.z = this->mOriginalOrigin.z;
	this->mCurrentVelocity.z = 0.0;
	this->mCurrentVelocity.y = 0.0;
	this->mCurrentVelocity.x = 0.0;
	this->mCurrentWorldBounds.Clear();
	this->mCurrentWorldBounds.AddPoint(this->mCurrentOrigin + this->mCurrentLocalBounds[0]);
	this->mCurrentWorldBounds.AddPoint(this->mCurrentOrigin + this->mCurrentLocalBounds[1]);
	this->mSpriteSize.Zero();
	UpdateFromOwner(parms, time, 1);
	this->mReferenceSound = 0;
	if (parms->referenceSoundHandle > 0) {
		this->mReferenceSound = soundSystem->EmitterForIndex(SOUNDWORLD_GAME, parms->referenceSoundHandle);
	}
	UpdateSegments(time);
	this->mOriginDistanceToCamera = 0.0;
	this->mShortestDistanceToCamera = 0.0;
}

float rvBSE::GetAttenuation(rvSegmentTemplate* st) const
{
	double result; // st7
	float sta; // [esp+4h] [ebp+4h]
	float stb; // [esp+4h] [ebp+4h]
	float stc; // [esp+4h] [ebp+4h]
	float std; // [esp+4h] [ebp+4h]

	result = 0.0;
	if (st->mAttenuation.x <= 0.0 && st->mAttenuation.y <= 0.0)
		return this->mAttenuation;
	sta = st->mAttenuation.x + 1.0;
	if (sta > (double)this->mShortestDistanceToCamera)
		return this->mAttenuation;
	stb = st->mAttenuation.y - 1.0;
	if (stb >= (double)this->mShortestDistanceToCamera)
	{
		stc = (this->mShortestDistanceToCamera - st->mAttenuation.x) / (st->mAttenuation.y - st->mAttenuation.x);
		std = (1.0 - stc) * this->mAttenuation;
		result = std;
	}
	return result;
}

float rvBSE::GetOriginAttenuation(rvSegmentTemplate* st) const
{
	float result = 0.0f;

	if (st->mAttenuation.x <= 0.0f && st->mAttenuation.y <= 0.0f) {
		return st->mScale * mAttenuation;
	}

	if (st->mAttenuation.x + 1.0f > mOriginDistanceToCamera) {
		return st->mScale * mAttenuation;
	}

	if (st->mAttenuation.y - 1.0f >= mOriginDistanceToCamera) {
		const float lerp = (mOriginDistanceToCamera - st->mAttenuation.x) / (st->mAttenuation.y - st->mAttenuation.x);
		result = (1.0f - lerp) * st->mScale * mAttenuation;
	}

	return result;
}

void rvBSE::UpdateSoundEmitter(rvSegmentTemplate* st, rvSegment* seg)
{
	if (!st || !seg || !mReferenceSound) {
		return;
	}

	if (GetStopped()) {
		// Stop looping sounds when the owning effect is stopped.
		if (st->GetSoundLooping() && (seg->mFlags & 2) != 0) {
			mReferenceSound->StopSound(static_cast<s_channelType>(seg->mSegmentTemplateHandle + SCHANNEL_ONE));
		}
		return;
	}

	soundShaderParms_t parms = {};
	parms.volume = seg->mSoundVolume;
	parms.frequencyShift = seg->mFreqShift;

	// Keep the emitter spatialized at the current effect origin so sound
	// tracks moving/attached effects correctly.
	mReferenceSound->UpdateEmitter(mCurrentOrigin, mCurrentVelocity, 0, &parms);
}
const idVec3 rvBSE::GetInterpolatedOffset(float time) const
{
	idVec3 result; // eax
	float v5; // [esp+0h] [ebp-18h]
	float v6; // [esp+4h] [ebp-14h]
	float v7; // [esp+8h] [ebp-10h]
	float v8; // [esp+Ch] [ebp-Ch]
	float v9; // [esp+10h] [ebp-8h]
	float v10; // [esp+14h] [ebp-4h]
	float deltaTime; // [esp+1Ch] [ebp+4h]
	float deltaTimea; // [esp+1Ch] [ebp+4h]

	result.z = 0.0;
	result.y = 0.0;
	result.x = 0.0;
	deltaTime = this->mCurrentTime - this->mLastTime;
	if (deltaTime >= 0.0020000001)
	{
		deltaTimea = 1.0 - (time - this->mLastTime) / deltaTime;
		v5 = this->mCurrentOrigin.x - this->mLastOrigin.x;
		v6 = this->mCurrentOrigin.y - this->mLastOrigin.y;
		v7 = this->mCurrentOrigin.z - this->mLastOrigin.z;
		v8 = v5 * deltaTimea;
		v9 = v6 * deltaTimea;
		v10 = deltaTimea * v7;
		result.x = v8;
		result.y = v9;
		result.z = v10;
	}
	return result;
}

void rvBSE::SetDuration(float time)
{
	double v2; // st7

	v2 = time;
	if (time < 0.0)
	{
		v2 = this->mDeclEffect->mMinDuration;
	LABEL_3:
		this->mDuration = v2;
		return;
	}
	if (this->mDuration < v2)
		goto LABEL_3;
}

const char* rvBSE::GetDeclName()
{
	return mDeclEffect->base->GetName();
}

void rvBSE::UpdateAttenuation()
{
	if ((this->mDeclEffect->mFlags & ETFLAG_ATTENUATES) == 0) {
		return;
	}

	idVec3 viewOrigin;
	idMat3 viewAxis;
	game->GetPlayerView(viewOrigin, viewAxis);

	const float originDistance = (mCurrentOrigin - viewOrigin).LengthFast();
	mOriginDistanceToCamera = idMath::ClampFloat(1.0f, 131072.0f, originDistance);

	// Transform the camera into effect local space and measure against local bounds.
	const idVec3 localView = mCurrentAxisTransposed * (viewOrigin - mCurrentOrigin);
	const float shortest = mCurrentLocalBounds.ShortestDistance(localView);
	mShortestDistanceToCamera = idMath::ClampFloat(1.0f, 131072.0f, shortest);
}

void rvBSE::LoopInstant(float time)
{
	rvBSE* v2; // esi
	int v3; // edi
	bool v4; // zf
	bool v5; // sf
	int v6; // ebx
	unsigned __int64 v7; // st7

	v2 = this;
	if (0.0 == this->mDuration)
	{
		v3 = 0;
		v4 = this->mSegments.Num() == 0;
		v5 = this->mSegments.Num() < 0;
		this->mStartTime = this->mDeclEffect->mMaxDuration + 0.5 + this->mStartTime;
		if (!v5 && !v4)
		{
			v6 = 0;
			do
			{
				v2->mSegments[v6].ResetTime(v2, v2->mStartTime);
				++v3;
				++v6;
			} while (v3 < v2->mSegments.Num());
		}
		if (bse_debug.GetInteger() == 2)
		{
			v7 = v2->mDeclEffect->mMaxDuration + 0.5;
			common->Printf("BSE: Looping duration %g\n", v7);
		}
		++v2->mDeclEffect->mLoopCount;
	}
}

void rvBSE::LoopLooping(float time)
{
	int v3; // edi
	bool v4; // cc
	int v5; // ebx

	if (0.0 != this->mDuration)
	{
		v3 = 0;
		v4 = this->mSegments.Num() <= 0;
		this->mStartTime = this->mStartTime + this->mDuration;
		this->mDuration = 0.0;
		if (!v4)
		{
			v5 = 0;
			do
			{
				mSegments[v5].ResetTime(this, this->mStartTime);
				++v3;
				++v5;
			} while (v3 < this->mSegments.Num());
		}
		if (bse_debug.GetInteger() == 2) {
			common->Printf("BSE: Looping duration: %g", this->mDuration);
		}

		++this->mDeclEffect->mLoopCount;
	}
}
bool rvBSE::Service(renderEffect_t* parms, float time, bool spawn, bool& forcePush)
{
	renderEffect_s* v5; // ebp
	int v7; // edi
	int v8; // ebx
	int v9; // edi
	char v10; // bl
	int v11; // ebp
	bool v12; // zf
	float spawna; // [esp+24h] [ebp+Ch]
	float spawnb; // [esp+24h] [ebp+Ch]
	float spawnc; // [esp+24h] [ebp+Ch]
	float spawnd; // [esp+24h] [ebp+Ch]

	v5 = parms;
	UpdateFromOwner(parms, time, 0);
	UpdateAttenuation();
	if (spawn)
	{
		v7 = 0;
		if (this->mSegments.Num() > 0)
		{
			v8 = 0;
			do
			{
				spawna = 0.0f;
				mSegments[v8].Check(this, time, spawna);
				++v7;
				++v8;
			} while (v7 < this->mSegments.Num());
		}
	}
	if ((this->mFlags & 8) == 0 && parms->loop)
	{
		spawnb = this->mDuration + this->mStartTime;
		if (spawnb < (double)time)
			LoopLooping(time);
	}
	v9 = 0;
	v10 = 0;
	if (this->mSegments.Num() > 0)
	{
		v11 = 0;
		do
		{
			if (mSegments[v11].UpdateParticles(this, time))
				v10 = 1;
			++v9;
			++v11;
		} while (v9 < this->mSegments.Num());
		v5 = parms;
	}
	this->mFlags &= 0xFFFFFFFB;
	forcePush = this->mForcePush;
	v12 = (this->mFlags & 8) == 0;
	this->mForcePush = 0;
	if (!v12)
		return v10 == 0;
	if (v5->loop)
	{
		spawnc = this->mDuration + this->mStartTime;
		if (spawnc < (double)time)
			LoopInstant(time);
		if (v5->loop)
			return 0;
	}
	spawnd = this->mDuration + this->mStartTime;
	return spawnd < (double)time;
}

float rvBSE::EvaluateCost(int segment)
{
	double result; // st7
	int v4; // edi
	int v5; // ebx
	double v6; // st7

	if (segment < 0)
	{
		v4 = 0;
		this->mCost = 0.0;
		if (this->mSegments.Num() > 0)
		{
			v5 = 0;
			do
			{
				v6 = mDeclEffect->EvaluateCost(
					this->mSegments[v5].mActiveCount,
					segment);
				++v4;
				++v5;
				this->mCost = v6 + this->mCost;
			} while (v4 < this->mSegments.Num());
		}
		result = this->mCost;
	}
	else
	{
		this->mCost = mDeclEffect->EvaluateCost(
			this->mSegments[segment].mActiveCount,
			segment);
		result = this->mCost;
	}
	return result;
}

void rvBSE::InitModel(idRenderModel* model)
{
	int v3; // edi
	int v4; // ebx

	v3 = 0;
	if (this->mSegments.Num() > 0)
	{
		v4 = 0;
		do
		{
			mSegments[v4].AllocateSurface(this, model);
			++v3;
			++v4;
		} while (v3 < this->mSegments.Num());
	}
}

idRenderModel* rvBSE::Render(idRenderModel* model, const struct renderEffect_s* owner, const struct viewDef_s* view)
{
	if (!bse_render.GetInteger() || !owner || !view) {
		return NULL;
	}

	idRenderModelStatic* renderModel = dynamic_cast<idRenderModelStatic*>(model);
	if (model && !renderModel) {
		delete model;
		model = NULL;
	}

	if (!renderModel) {
		renderModel = new idRenderModelStatic();
		renderModel->InitEmpty("_bse_runtime");
		InitModel(renderModel);
	}
	else {
		// Runtime particles mutate vertex/index data every frame.
		// Invalidate vertex/index caches so new data is uploaded.
		renderModel->FreeVertexCache();
		for (int i = 0; i < renderModel->NumSurfaces(); ++i) {
			const modelSurface_t* surf = renderModel->Surface(i);
			if (!surf || !surf->geometry) {
				continue;
			}
			srfTriangles_t* tri = surf->geometry;
			if (tri->indexCache) {
				vertexCache.Free(tri->indexCache);
				tri->indexCache = NULL;
			}
		}
	}

	mViewAxis = view->renderView.viewaxis;
	mViewOrg = view->renderView.vieworg;

	float time = view->floatTime;
	for (int i = 0; i < mSegments.Num(); ++i) {
		const bool active = mSegments[i].Active();

		mSegments[i].ClearSurface(this, renderModel);
		if (active) {
			mSegments[i].Render(this, owner, renderModel, time);
			mSegments[i].RenderTrail(this, owner, renderModel, time);
		}
	}

	// BSE runtime geometry is rebuilt every frame and includes strip-like particle
	// topology that can intentionally share points. Avoid full static-surface cleanup
	// here and only update per-surface/model bounds for culling.
	idBounds modelBounds;
	modelBounds.Clear();
	bool hasBounds = false;
	for (int i = 0; i < renderModel->NumSurfaces(); ++i) {
		const modelSurface_t* surf = renderModel->Surface(i);
		if (!surf || !surf->geometry) {
			continue;
		}

		srfTriangles_t* tri = surf->geometry;
		if (tri->numVerts <= 0 || tri->numIndexes <= 0) {
			tri->bounds.Clear();
			continue;
		}

		R_BoundTriSurf(tri);
		if (!hasBounds) {
			modelBounds = tri->bounds;
			hasBounds = true;
		}
		else {
			modelBounds.AddBounds(tri->bounds);
		}
	}

	if (!hasBounds) {
		modelBounds.Zero();
	}
	renderModel->bounds = modelBounds;
	DisplayDebugInfo(owner, view, renderModel->bounds);
	mLastRenderBounds = modelBounds;
	mGrownRenderBounds = mLastRenderBounds;
	mGrownRenderBounds.ExpandSelf(20.0f);
	mForcePush = true;
	return renderModel;
}

void rvBSE::Destroy()
{
	mSegments.Clear();
	mReferenceSound = NULL;
}


void rvBSE::UpdateSegments(float time)
{
	int v3; // ebx
	rvSegment* v4; // eax
	rvParticle** v5; // esi
	bool v6; // cc
	int v7; // ecx
	int* v8; // eax
	rvSegment* v9; // esi
	rvSegment* v10; // ecx
	int v11; // edx
	int v12; // eax
	rvParticle** v13; // esi
	int v14; // esi
	int v15; // edi
	int v16; // esi
	int v17; // edi
	int v18; // esi
	int v19; // edi
	rvSegment* ptr; // [esp+14h] [ebp-14h]

	v3 = this->mDeclEffect->mSegmentTemplates.Num();
	if (v3 > 0)
	{
		if (v3 != this->mSegments.Size())
		{
			mSegments.SetNum(v3);
		}
	}
	else
	{
		mSegments.Clear();
	}
	v14 = 0;
	//this->mSegments.num = v3; // Not sure why this gets set twice? compiler optimization gone wrong?
	if (v3 > 0)
	{
		v15 = 0;
		do
			mSegments[v15++].Init(this, this->mDeclEffect, v14++, time);
		while (v14 < this->mSegments.Num());
	}
	v16 = 0;
	if (this->mSegments.Num() > 0)
	{
		v17 = 0;
		do
		{
			mSegments[v17].CalcCounts(this, time);
			++v16;
			++v17;
		} while (v16 < this->mSegments.Num());
	}
	v18 = 0;
	if (this->mSegments.Num() > 0)
	{
		v19 = 0;
		do
		{

			mSegments[v19].InitParticles(this);
			++v18;
			++v19;
		} while (v18 < this->mSegments.Num());
	}
}


void rvBSE::DisplayDebugInfo(const struct renderEffect_s* parms, const struct viewDef_s* view, idBounds& bounds) {
	if (!parms || !view || !view->renderWorld) {
		return;
	}
	if (!bse_debug.GetInteger() && !bse_showBounds.GetBool()) {
		return;
	}

	idRenderWorld* rw = view->renderWorld;

	if (bse_debug.GetInteger()) {
		const char* effectName = "<unknown>";
		if (mDeclEffect && mDeclEffect->base) {
			effectName = mDeclEffect->base->GetName();
		}

		idStr txt = va(
			"(%g) (size %g) (bri %g)\n%s",
			mCost,
			mDeclEffect ? mDeclEffect->mSize : 0.0f,
			parms->shaderParms[6],
			effectName);

		rw->DebugAxis(parms->origin, parms->axis);
		rw->DrawText(txt.c_str(), parms->origin, 14.0f, colorCyan, view->renderView.viewaxis, 1, 0);
	}

	if (!bse_showBounds.GetBool()) {
		return;
	}

	rw->DebugBounds(colorGreen, bounds, vec3_origin);

	const idBox worldBox(mCurrentWorldBounds, parms->origin, parms->axis);
	rw->DebugBox(colorBlue, worldBox);

	if (!mCurrentWorldBounds.Contains(bounds)) {
		rw->DebugBounds(colorRed, bounds, vec3_origin);
	}
}

void __thiscall rvBSE::UpdateFromOwner(renderEffect_s* parms, float time, bool init)
{
	mLastTime = mCurrentTime;
	mLastOrigin = mCurrentOrigin;

	mCurrentTime = time;
	mCurrentOrigin = parms->origin;
	mCurrentAxis = parms->axis;
	mCurrentAxisTransposed = mCurrentAxis.Transpose();

	const float dt = mCurrentTime - mLastTime;
	if (dt > 0.002f) {
		mCurrentVelocity = (mCurrentOrigin - mLastOrigin) * (1.0f / dt);
	}
	else {
		mCurrentVelocity.Zero();
	}

	mGravity = parms->gravity;
	mGravityDir = mGravity;
	const float gravityLengthSqr = mGravityDir.LengthSqr();
	if (gravityLengthSqr > 0.0f) {
		mGravityDir *= idMath::InvSqrt(gravityLengthSqr);
	}

	SetLooping(parms->loop);
	SetHasEndOrigin(parms->hasEndOrigin);
	SetOrientateIdentity((mDeclEffect->mFlags & ETFLAG_ORIENTATE_IDENTITY) != 0);
	SetFlag(parms->ambient, EFLAG_AMBIENT);

	const idVec3 halfSize(mDeclEffect->mSize, mDeclEffect->mSize, mDeclEffect->mSize);

	mCurrentWorldBounds.AddPoint(mCurrentOrigin + halfSize);
	mCurrentWorldBounds.AddPoint(mCurrentOrigin - halfSize);

	if (parms->hasEndOrigin && (mDeclEffect->mFlags & ETFLAG_USES_ENDORIGIN) != 0) {
		const bool endOriginChanged = init || parms->endOrigin != mCurrentEndOrigin || mCurrentOrigin != mLastOrigin;
		mCurrentEndOrigin = parms->endOrigin;
		mCurrentWorldBounds.AddPoint(mCurrentEndOrigin + halfSize);
		mCurrentWorldBounds.AddPoint(mCurrentEndOrigin - halfSize);
		SetEndOriginChanged(endOriginChanged);
	}
	else {
		mCurrentEndOrigin = parms->endOrigin;
		SetEndOriginChanged(false);
	}

	mCurrentLocalBounds.Clear();
	for (int ix = 0; ix < 2; ++ix) {
		for (int iy = 0; iy < 2; ++iy) {
			for (int iz = 0; iz < 2; ++iz) {
				const idVec3 corner(
					mCurrentWorldBounds[ix].x,
					mCurrentWorldBounds[iy].y,
					mCurrentWorldBounds[iz].z);
				const idVec3 local = mCurrentAxisTransposed * (corner - mCurrentOrigin);
				mCurrentLocalBounds.AddPoint(local);
			}
		}
	}

	mLightningAxis = mCurrentAxis;
	if (GetHasEndOrigin()) {
		idVec3 forward = mCurrentEndOrigin - mCurrentOrigin;
		if (forward.Normalize() > BSE_TIME_EPSILON) {
			idVec3 up = mCurrentAxis[2];
			if (idMath::Fabs(forward * up) > 0.98f) {
				up = mCurrentAxis[1];
			}

			idVec3 right = up.Cross(forward);
			if (right.Normalize() > BSE_TIME_EPSILON) {
				up = forward.Cross(right);
				up.Normalize();
				mLightningAxis[0] = forward;
				mLightningAxis[1] = right;
				mLightningAxis[2] = up;
			}
		}
	}

	mCurrentWindVector.Zero();
	mTint.Set(parms->shaderParms[0], parms->shaderParms[1], parms->shaderParms[2], parms->shaderParms[3]);
	mBrightness = parms->shaderParms[6];
	mSpriteSize.x = parms->shaderParms[8];
	mSpriteSize.y = parms->shaderParms[9];
	if (mTint == vec4_zero && mBrightness == 0.0f) {
		// Some legacy/game-network paths can feed zero-initialized shader parms.
		// Match vanilla-visible defaults instead of rendering fully black.
		mTint.Set(1.0f, 1.0f, 1.0f, 1.0f);
		mBrightness = 1.0f;
	}
	mAttenuation = parms->attenuation;
	mSuppressLightsInViewID = parms->suppressSurfaceInViewID;
}
