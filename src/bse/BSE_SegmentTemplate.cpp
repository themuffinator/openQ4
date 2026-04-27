// BSE_SegmentTemplate.cpp
//




#include "BSE_Envelope.h"
#include "BSE_Particle.h"
#include "BSE.h"

void rvSegmentTemplate::CreateParticleTemplate(rvDeclEffect* effect, idParser* src, int particleType)
{
	mParticleTemplate.Init();
	mParticleTemplate.mType = particleType;
	mParticleTemplate.SetParameterCounts();
	mParticleTemplate.Parse(effect, src);
}

bool rvSegmentTemplate::GetSoundLooping()
{
	if (mSoundShader != NULL) {
		return ((unsigned int)mSoundShader->GetParms()->soundShaderFlags >> 5) & 1;
	}
	return false;
}

void rvSegmentTemplate::EvaluateTrailSegment(rvDeclEffect* et) {
	if (mParticleTemplate.mTrailInfo->mTrailType && !mParticleTemplate.mTrailInfo->mTrailTypeName.IsEmpty())
	{
		// jmarshall - WindowName isn't defined and doesn't make sense here
				// if (idStr::Cmp(v3->mTrailTypeName.data, (const char*)&WindowName))
		// jmarshall end
		{
			mTrailSegmentIndex = et->GetTrailSegmentIndex(mParticleTemplate.mTrailInfo->mTrailTypeName);
		}
	}
}

bool rvSegmentTemplate::GetSmoker()
{
	return mParticleTemplate.mTrailInfo->mTrailType == 3;
}

bool rvSegmentTemplate::DetailCull() const
{
	return (mDetail > 0.0f) && (cvarSystem->GetCVarFloat("bse_scale") < mDetail);
}

float rvSegmentTemplate::CalculateBounds(void)
{
	switch (mSegType)
	{
	case SEG_EMITTER:
	case SEG_SPAWNER:
	case SEG_TRAIL: {
		const float maxSize = mParticleTemplate.GetMaxSize();
		const float maxDist = mParticleTemplate.GetFurthestDistance();
		float maxLength = 0.0f;
		const int particleType = mParticleTemplate.GetType();
		if (particleType == PTYPE_LINE || particleType == PTYPE_ELECTRICITY) {
			maxLength = mParticleTemplate.GetMaxLength();
		}
		return maxSize + maxDist + maxLength + mParticleTemplate.GetMaxOffset();
	}
	case SEG_LIGHT:
		return mParticleTemplate.GetMaxSize();
	default:
		return 8.0f;
	}
}

bool rvSegmentTemplate::Compare(const rvSegmentTemplate& a) const
{
	if (mSegmentName.Icmp(a.mSegmentName) != 0) {
		return false;
	}

	if (((mFlags ^ a.mFlags) & ~STFLAG_LOCKED) != 0) {
		return false;
	}

	if (mSegType != a.mSegType) {
		return false;
	}

	// Sound segments intentionally ignore local time ranges.
	if (mSegType != SEG_SOUND) {
		if (mLocalStartTime != a.mLocalStartTime || mLocalDuration != a.mLocalDuration) {
			return false;
		}
	}

	if (mScale != a.mScale || mDetail != a.mDetail || mAttenuation != a.mAttenuation) {
		return false;
	}

	if (mDensity.y == 0.0f) {
		if (mCount != a.mCount) {
			return false;
		}
	}
	else {
		if (mDensity != a.mDensity || mParticleCap != a.mParticleCap) {
			return false;
		}
	}

	if (mTrailSegmentIndex != a.mTrailSegmentIndex || mNumEffects != a.mNumEffects) {
		return false;
	}

	for (int i = 0; i < mNumEffects; ++i) {
		if (mEffects[i] != a.mEffects[i]) {
			return false;
		}
	}

	if (mSoundShader != a.mSoundShader || mSoundVolume != a.mSoundVolume || mFreqShift != a.mFreqShift) {
		return false;
	}

	return mParticleTemplate == a.mParticleTemplate;
}

float rvSegmentTemplate::EvaluateCost(int activeParticles) const
{
	float v4; // [esp+8h] [ebp-4h]
	float activeParticlesa; // [esp+10h] [ebp+4h]

	if ((mFlags & 1) == 0)
		return 0.0;
	v4 = rvSegmentTemplate::mSegmentBaseCosts[mSegType];
	if (mParticleTemplate.mType)
	{
		activeParticlesa = (float)activeParticles;
		v4 = mParticleTemplate.CostTrail(activeParticlesa) + v4;
		if ((mParticleTemplate.mFlags & 0x200) != 0)
			v4 = activeParticlesa * 80.0 + v4;
	}
	return v4;
}

rvParticleTemplate& rvParticleTemplate::operator=(const rvParticleTemplate& __that)
{
	mFlags = __that.mFlags;
	mTraceModelIndex = __that.mTraceModelIndex;
	mType = __that.mType;
	mMaterial = __that.mMaterial;
	mModel = __that.mModel;
	mEntityDefName = __that.mEntityDefName;
	mGravity = __that.mGravity;
	mDuration = __that.mDuration;
	mCentre = __that.mCentre;
	mTiling = __that.mTiling;
	mBounce = __that.mBounce;
	mPhysicsDistance = __that.mPhysicsDistance;
	mWindDeviationAngle = __that.mWindDeviationAngle;
	mVertexCount = __that.mVertexCount;
	mIndexCount = __that.mIndexCount;
	mTrailRepeat = __that.mTrailRepeat;
	mNumSizeParms = __that.mNumSizeParms;
	mNumRotateParms = __that.mNumRotateParms;
	mNumFrames = __that.mNumFrames;
	mTrailInfo = __that.mTrailInfo;
	mElecInfo = __that.mElecInfo;
	mpSpawnPosition = __that.mpSpawnPosition;
	mpSpawnDirection = __that.mpSpawnDirection;
	mpSpawnVelocity = __that.mpSpawnVelocity;
	mpSpawnAcceleration = __that.mpSpawnAcceleration;
	mpSpawnFriction = __that.mpSpawnFriction;
	mpSpawnTint = __that.mpSpawnTint;
	mpSpawnFade = __that.mpSpawnFade;
	mpSpawnSize = __that.mpSpawnSize;
	mpSpawnRotate = __that.mpSpawnRotate;
	mpSpawnAngle = __that.mpSpawnAngle;
	mpSpawnOffset = __that.mpSpawnOffset;
	mpSpawnLength = __that.mpSpawnLength;
	mpSpawnWindStrength = __that.mpSpawnWindStrength;
	mpTintEnvelope = __that.mpTintEnvelope;
	mpFadeEnvelope = __that.mpFadeEnvelope;
	mpSizeEnvelope = __that.mpSizeEnvelope;
	mpRotateEnvelope = __that.mpRotateEnvelope;
	mpAngleEnvelope = __that.mpAngleEnvelope;
	mpOffsetEnvelope = __that.mpOffsetEnvelope;
	mpLengthEnvelope = __that.mpLengthEnvelope;
	mpDeathTint = __that.mpDeathTint;
	mpDeathFade = __that.mpDeathFade;
	mpDeathSize = __that.mpDeathSize;
	mpDeathRotate = __that.mpDeathRotate;
	mpDeathAngle = __that.mpDeathAngle;
	mpDeathOffset = __that.mpDeathOffset;
	mpDeathLength = __that.mpDeathLength;
	mNumImpactEffects = __that.mNumImpactEffects;
	mImpactEffects[0] = __that.mImpactEffects[0];
	mImpactEffects[1] = __that.mImpactEffects[1];
	mImpactEffects[2] = __that.mImpactEffects[2];
	mImpactEffects[3] = __that.mImpactEffects[3];
	mNumTimeoutEffects = __that.mNumTimeoutEffects;
	mTimeoutEffects[0] = __that.mTimeoutEffects[0];
	mTimeoutEffects[1] = __that.mTimeoutEffects[1];
	mTimeoutEffects[2] = __that.mTimeoutEffects[2];
	mTimeoutEffects[3] = __that.mTimeoutEffects[3];
	return *this;
}

void rvSegmentTemplate::Duplicate(const rvSegmentTemplate& copy)
{
	mDeclEffect = copy.mDeclEffect;
	mSegmentName = copy.mSegmentName;
	mFlags = copy.mFlags;
	mSegType = copy.mSegType;
	mLocalStartTime = copy.mLocalStartTime;
	mLocalDuration = copy.mLocalDuration;
	mScale = copy.mScale;
	mAttenuation = copy.mAttenuation;
	mParticleCap = copy.mParticleCap;
	mScale = copy.mScale;
	mDetail = copy.mDetail;
	mCount = copy.mCount;
	mDensity = copy.mDensity;
	mTrailSegmentIndex = copy.mTrailSegmentIndex;
	mNumEffects = copy.mNumEffects;
	mEffects[0] = copy.mEffects[0];
	mEffects[1] = copy.mEffects[1];
	mEffects[2] = copy.mEffects[2];
	mEffects[3] = copy.mEffects[3];
	mSoundShader = copy.mSoundShader;
	mSoundVolume = copy.mSoundVolume;
	mFreqShift = copy.mFreqShift;
	mParticleTemplate.Duplicate(copy.mParticleTemplate);
	mDecalAxis = copy.mDecalAxis;
}

void rvSegmentTemplate::Init(rvDeclEffect* decl)
{
	mDeclEffect = decl;
	mSegmentName = "";
	mFlags = STFLAG_ENABLED;
	mSegType = SEG_NONE;
	mLocalStartTime.Zero();
	mLocalDuration.Zero();
	mAttenuation.Zero();
	mParticleCap = 0.0f;
	mScale = 1.0f;
	mDetail = 0.0f;
	mCount.Set(1.0f, 1.0f);
	mDensity.Zero();
	mTrailSegmentIndex = -1;
	mNumEffects = 0;
	for (int i = 0; i < BSE_NUM_SPAWNABLE; ++i) {
		mEffects[i] = NULL;
	}
	mSoundShader = NULL;
	mSoundVolume.Zero();
	mFreqShift.Set(1.0f, 1.0f);
	mDecalAxis = 0;
	mParticleTemplate.Init();
}


void rvSegmentTemplate::SetMinDuration(rvDeclEffect* effect)
{
	const idSoundShader* v2; // eax
	float duration; // [esp+4h] [ebp-4h]

	if ((mFlags & 0x10) == 0)
	{
		v2 = mSoundShader;
		if (!v2 || (v2->GetParms()->soundShaderFlags & 0x20) == 0)
		{
			duration = mLocalDuration.x + mLocalStartTime.x;
			effect->SetMinDuration(duration);
		}
	}
}

bool rvSegmentTemplate::Finish(rvDeclEffect* effect)
{
	rvParticleTemplate* v3; // edi
	int v5; // ecx
	float v6; // [esp+4h] [ebp-4h]
	float v7; // [esp+4h] [ebp-4h]
	float v8; // [esp+4h] [ebp-4h]
	float v9; // [esp+4h] [ebp-4h]
	float v10; // [esp+4h] [ebp-4h]

	if (mLocalStartTime.y <= mLocalStartTime.x)
	{
		v6 = mLocalStartTime.x;
		mLocalStartTime.x = mLocalStartTime.y;
		mLocalStartTime.y = v6;
	}
	if (mLocalDuration.y <= mLocalDuration.x)
	{
		v7 = mLocalDuration.x;
		mLocalDuration.x = mLocalDuration.y;
		mLocalDuration.y = v7;
	}
	if (mCount.y <= mCount.x)
	{
		v8 = mCount.x;
		mCount.x = mCount.y;
		mCount.y = v8;
	}
	if (mDensity.y <= mDensity.x)
	{
		v9 = mDensity.x;
		mDensity.x = mDensity.y;
		mDensity.y = v9;
	}
	if (mAttenuation.y <= mAttenuation.x)
	{
		v10 = mAttenuation.x;
		mAttenuation.x = mAttenuation.y;
		mAttenuation.y = v10;
	}
	if (mParticleTemplate.mType)
	{
		v3 = &mParticleTemplate;
		mParticleTemplate.Finish();
		v3->mFlags &= 0xFFF7FFFF;
	}
	switch (mSegType)
	{
	case 2:
		mFlags |= 4u;
		if (mParticleTemplate.mType && (mFlags & 0x20) == 0)
			goto LABEL_25;
		return 0;
	case 3:
		mFlags |= 4u;
		if (mParticleTemplate.mType)
			goto LABEL_25;
		return 0;
	case 4:
		mFlags |= 4u;
		mLocalStartTime.y = 0.0;
		mLocalStartTime.x = 0.0;
		mLocalDuration.y = 0.0;
		mLocalDuration.x = 0.0;
		if (!mParticleTemplate.mType)
			return 0;
		mParticleTemplate.mFlags |= 0x80000u;
	LABEL_25:
		v5 = mParticleTemplate.mType;
		if (v5 != PTYPE_NONE) {
			mFlags |= STFLAG_HASPARTICLES;
		}
		if (v5 == 10)
			mFlags = mFlags & 0xFFFFFFFB | 0x100;
		if ((mFlags & 0x20) != 0
			|| mParticleTemplate.mTrailInfo->mTrailType == 3
			|| (mParticleTemplate.mFlags & 0x200) != 0
			|| mParticleTemplate.mNumTimeoutEffects)
		{
			mFlags |= 0x2000u;
		}
		if (v5 == 7 || v5 == 6)
			mFlags |= 0x2000u;
		return 1;
	case 5:
		mFlags |= 0x10u;
		goto LABEL_25;
	case 6:
		mFlags = mFlags & 0xFFFFFFFB | 0x100;
		goto LABEL_25;
	case SEG_SHAKE:
	case SEG_TUNNEL:
	case SEG_DOUBLEVISION:
		if (mAttenuation.y > 0.0)
			mFlags |= 0x40u;
		goto LABEL_24;
	default:
	LABEL_24:
		mFlags &= 0xFFFFFFFB;
		goto LABEL_25;
	}
}


void rvSegmentTemplate::operator=(const rvSegmentTemplate& copy)
{
	this->mDeclEffect = copy.mDeclEffect;
	this->mSegmentName = copy.mSegmentName;
	this->mFlags = copy.mFlags;
	this->mSegType = copy.mSegType;
	this->mLocalStartTime = copy.mLocalStartTime;
	this->mLocalDuration = copy.mLocalDuration;
	this->mScale = copy.mScale;
	this->mAttenuation = copy.mAttenuation;
	this->mParticleCap = copy.mParticleCap;
	this->mScale = copy.mScale;
	this->mDetail = copy.mDetail;
	this->mCount = copy.mCount;
	this->mDensity = copy.mDensity;
	this->mTrailSegmentIndex = copy.mTrailSegmentIndex;
	this->mNumEffects = copy.mNumEffects;
	this->mEffects[0] = copy.mEffects[0];
	this->mEffects[1] = copy.mEffects[1];
	this->mEffects[2] = copy.mEffects[2];
	this->mEffects[3] = copy.mEffects[3];
	this->mSoundShader = copy.mSoundShader;
	this->mSoundVolume = copy.mSoundVolume;
	this->mFreqShift = copy.mFreqShift;
	this->mParticleTemplate = copy.mParticleTemplate;
	this->mDecalAxis = copy.mDecalAxis;
}

void rvSegmentTemplate::SetMaxDuration(rvDeclEffect* effect)
{
	rvSegmentTemplate* v2; // esi
	rvDeclEffect* v3; // edi
	float duration; // ST0C_4
	float effecta; // [esp+14h] [ebp+4h]

	v2 = this;
	if (!(((unsigned int)this->mFlags >> 4) & 1))
	{
		v3 = effect;
		duration = this->mLocalDuration.x + this->mLocalStartTime.x;
		effect->SetMaxDuration(duration);
		if (v2->mParticleTemplate.mType)
		{
			effecta = v2->mLocalDuration.x + v2->mLocalStartTime.x + v2->mParticleTemplate.mDuration.y;
			v3->SetMaxDuration(effecta);
		}
	}
}

bool rvSegmentTemplate::Parse(rvDeclEffect* effect, int segmentType, idParser* lexer) {
	idToken token;

	if (!lexer->ReadToken(&token))
	{
		return false;
	}

	if (token != "{") {
		mSegmentName = token;
	}
	else {
		mSegmentName = va("unnamed%d", effect->GetNumSegmentTemplates());
		lexer->UnreadToken(&token);
	}

	mSegType = segmentType;

	if (!lexer->ExpectTokenString("{")) {
		return false;
	}

	while (lexer->ReadToken(&token))
	{
		if (token == "}") {
			return true;
		}
		if (!token.Icmp("decalAxis"))
		{
			mDecalAxis = lexer->ParseInt();
		}
		else if (!token.Icmp("orientateIdentity"))
		{
			mFlags |= STFLAG_ORIENTATE_IDENTITY;
		}
		else if (!token.Icmp("useMaterialColor"))
		{
			mFlags |= STFLAG_USEMATCOLOR;
		}
		else if (!token.Icmp("depthsort"))
		{
			mFlags |= STFLAG_DEPTH_SORT;
		}
		else if (!token.Icmp("inverseDrawOrder"))
		{
			mFlags |= STFLAG_INVERSE_DRAWORDER;
		}
		else if (!token.Icmp("calcDuration"))
		{
			mFlags |= STFLAG_CALCULATE_DURATION;
		}
		else if (!token.Icmp("constant"))
		{
			mFlags |= STFLAG_INFINITE_DURATION;
		}
		else if (!token.Icmp("looping"))
		{
			// Keep legacy token behavior aligned with constant duration segments.
			mFlags |= STFLAG_INFINITE_DURATION;
		}
		else if (!token.Icmp("locked"))
		{
			mFlags |= STFLAG_LOCKED;
		}
		else if (!token.Icmp("temporary"))
		{
			mFlags |= STFLAG_TEMPORARY;
		}
		else if (!token.Icmp("inverseAttenuateEmitter"))
		{
			// Stock parser behavior enables attenuation when inverse attenuation is requested.
			mFlags |= STFLAG_ATTENUATE_EMITTER | STFLAG_INVERSE_ATTENUATE;
		}
		else if (!token.Icmp("attenuateEmitter"))
		{
			mFlags |= STFLAG_ATTENUATE_EMITTER;
		}
		else if (!token.Icmp("scale"))
		{
			mScale = lexer->ParseFloat();
		}
		else if (!token.Icmp("channel"))
		{
			if (!lexer->ReadToken(&token)) {
				return false;
			}
		}
		else if (!token.Icmp("effect"))
		{
			if (!lexer->ReadToken(&token)) {
				return false;
			}
			if (mNumEffects >= 4)
			{
				common->Warning("Unable to add effect '%s' - too many effects", token.c_str());
			}
			else
			{
				mEffects[this->mNumEffects++] = declManager->FindEffect(token);
			}
		}
		else if (!token.Icmp("freqShift"))
		{
			mFreqShift.x = lexer->ParseFloat();
			lexer->ExpectTokenString(",");
			mFreqShift.y = lexer->ParseFloat();
		}
		else if (!token.Icmp("attenuation"))
		{
			mAttenuation.x = lexer->ParseFloat();
			lexer->ExpectTokenString(",");
			mAttenuation.y = lexer->ParseFloat();
		}
		else if (!token.Icmp("volume"))
		{
			mSoundVolume.x = lexer->ParseFloat();
			lexer->ExpectTokenString(",");
			mSoundVolume.y = lexer->ParseFloat();
		}
		else if (!token.Icmp("soundShader"))
		{
			if (!lexer->ReadToken(&token)) {
				return false;
			}
			mSoundShader = (idSoundShader*)declManager->FindSound(token);
			float effecta = 0.0f;
			if (mSoundShader) {
				effecta = mSoundShader->GetTimeLength();
			}
			// Preserve a sane non-zero fallback for missing/streamed metadata.
			if (effecta <= BSE_TIME_EPSILON) {
				effecta = 1.0f;
			}
			mLocalDuration.x = effecta;
			mLocalDuration.y = effecta;
		}
		else if (!token.Icmp("detail"))
		{
			mDetail = lexer->ParseFloat();
		}
		else if (!token.Icmp("duration"))
		{
			mLocalDuration.x = lexer->ParseFloat();
			lexer->ExpectTokenString(",");
			mLocalDuration.y = lexer->ParseFloat();
		}
		else if (!token.Icmp("start"))
		{
			mLocalStartTime.x = lexer->ParseFloat();
			lexer->ExpectTokenString(",");
			mLocalStartTime.y = lexer->ParseFloat();
		}
		else if (!token.Icmp("density"))
		{
			mDensity.x = lexer->ParseFloat();
			lexer->ExpectTokenString(",");
			mDensity.y = lexer->ParseFloat();
		}
		else if (!token.Icmp("count") || !token.Icmp("rate"))
		{
			mCount.x = lexer->ParseFloat();
			lexer->ExpectTokenString(",");
			mCount.y = lexer->ParseFloat();
		}
		else if (!token.Icmp("particleCap"))
		{
			mParticleCap = lexer->ParseFloat();
		}
		else if (!token.Icmp("debris"))
		{
			CreateParticleTemplate(effect, lexer, PTYPE_DEBRIS);
		}
		else if (!token.Icmp("orientedlinked"))
		{
			CreateParticleTemplate(effect, lexer, PTYPE_ORIENTEDLINKED);
		}
		else if (!token.Icmp("linked"))
		{
			CreateParticleTemplate(effect, lexer, PTYPE_LINKED);
		}
		else if (!token.Icmp("electricity"))
		{
			CreateParticleTemplate(effect, lexer, PTYPE_ELECTRICITY);
		}
		else if (!token.Icmp("light"))
		{
			CreateParticleTemplate(effect, lexer, PTYPE_LIGHT);
		}
		else if (!token.Icmp("model"))
		{
			CreateParticleTemplate(effect, lexer, PTYPE_MODEL);
		}
		else if (!token.Icmp("decal"))
		{
			CreateParticleTemplate(effect, lexer, PTYPE_DECAL);
		}
		else if (!token.Icmp("oriented"))
		{
			CreateParticleTemplate(effect, lexer, PTYPE_ORIENTED);
		}
		else if (!token.Icmp("line"))
		{
			CreateParticleTemplate(effect, lexer, PTYPE_LINE);
		}
		else if (!token.Icmp("sprite"))
		{
			CreateParticleTemplate(effect, lexer, PTYPE_SPRITE);
		}
		else
		{
			common->Warning("^4BSE:^1 Invalid segment parameter '%s' (file: %s, line: %d)", token.c_str(), lexer->GetFileName(), lexer->GetLineNum());
		}
	}
	return false;
}
