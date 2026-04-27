// BSE_ParseParticle2.cpp
//



#include "BSE_Envelope.h"
#include "BSE_Particle.h"
#include "BSE.h"
#include "BSE_SpawnDomains.h"

rvTrailInfo rvParticleTemplate::sTrailInfo;
rvElectricityInfo rvParticleTemplate::sElectricityInfo;
rvEnvParms rvParticleTemplate::sDefaultEnvelope;
rvEnvParms rvParticleTemplate::sEmptyEnvelope;
rvParticleParms rvParticleTemplate::sSPF_ONE_1;
rvParticleParms rvParticleTemplate::sSPF_ONE_2;
rvParticleParms rvParticleTemplate::sSPF_ONE_3;
rvParticleParms rvParticleTemplate::sSPF_NONE_0;
rvParticleParms rvParticleTemplate::sSPF_NONE_1;
rvParticleParms rvParticleTemplate::sSPF_NONE_3;
bool rvParticleTemplate::sInited = false;

float rvSegmentTemplate::mSegmentBaseCosts[SEG_COUNT];

namespace {
static rvParticleParms* BSE_DuplicateParm(rvParticleParms* source) {
	if (source == NULL || source->mStatic) {
		return source;
	}

	rvParticleParms* copy = new rvParticleParms();
	*copy = *source;
	copy->mStatic = 0;
	return copy;
}

static rvEnvParms* BSE_DuplicateEnv(rvEnvParms* source) {
	if (source == NULL || source->mStatic) {
		return source;
	}

	rvEnvParms* copy = new rvEnvParms();
	*copy = *source;
	copy->mStatic = 0;
	return copy;
}

static void BSE_AssignParmPtr(rvParticleParms*& dest, rvParticleParms* source) {
	if (dest != source && dest != NULL && !dest->mStatic) {
		delete dest;
	}
	dest = source;
}

static void BSE_AssignEnvPtr(rvEnvParms*& dest, rvEnvParms* source) {
	if (dest != source && dest != NULL && !dest->mStatic) {
		delete dest;
	}
	dest = source;
}

static void BSE_DeleteOwnedParm(rvParticleParms*& value, rvParticleParms** seen, int& seenCount) {
	if (value == NULL || value->mStatic) {
		value = NULL;
		return;
	}

	for (int i = 0; i < seenCount; ++i) {
		if (seen[i] == value) {
			value = NULL;
			return;
		}
	}

	seen[seenCount++] = value;
	delete value;
	value = NULL;
}

static void BSE_DeleteOwnedEnv(rvEnvParms*& value, rvEnvParms** seen, int& seenCount) {
	if (value == NULL || value->mStatic) {
		value = NULL;
		return;
	}

	for (int i = 0; i < seenCount; ++i) {
		if (seen[i] == value) {
			value = NULL;
			return;
		}
	}

	seen[seenCount++] = value;
	delete value;
	value = NULL;
}
}

namespace {
ID_INLINE const char* BSE_ResolveEffectCompatAlias(const char* effectName) {
	// Stock splash FX reference "effects/ambient/drip_ring", but the shipped
	// data only provides "effects/ambient/drip_splash". Remap at parse-time to
	// preserve vanilla secondary splash behavior without shipping replacement data.
	if (!idStr::Icmp(effectName, "effects/ambient/drip_ring")) {
		return "effects/ambient/drip_splash";
	}
	return effectName;
}

ID_INLINE void BSE_UpdateExtents(const idVec3& sample, idVec3& mins, idVec3& maxs) {
	mins.x = Min(mins.x, sample.x);
	mins.y = Min(mins.y, sample.y);
	mins.z = Min(mins.z, sample.z);
	maxs.x = Max(maxs.x, sample.x);
	maxs.y = Max(maxs.y, sample.y);
	maxs.z = Max(maxs.z, sample.z);
}

ID_INLINE const char* BSE_EffectName(const rvDeclEffect* effect) {
	return effect != NULL ? effect->GetName() : "<unknown>";
}

static void BSE_ParticleWarning(const idParser* src, const rvDeclEffect* effect, const char* fmt, ...) {
	char detail[4096];
	va_list argptr;

	va_start(argptr, fmt);
	idStr::vsnPrintf(detail, sizeof(detail), fmt, argptr);
	va_end(argptr);
	detail[sizeof(detail) - 1] = '\0';

	if (effect != NULL) {
		common->Warning("^4BSE:^1 %s in '%s' (file: %s, line: %d)",
			detail,
			BSE_EffectName(effect),
			src != NULL ? src->GetFileName() : "",
			src != NULL ? src->GetLineNum() : 0);
	}
	else {
		common->Warning("^4BSE:^1 %s (file: %s, line: %d)",
			detail,
			src != NULL ? src->GetFileName() : "",
			src != NULL ? src->GetLineNum() : 0);
	}
}
}

float rvParticleTemplate::GetSpawnVolume(rvBSE* effect) {
	if (!mpSpawnPosition) {
		return 1.0f;
	}

	float xExtent = 0.0f;
	if ((mpSpawnPosition->mFlags & PPFLAG_USEENDORIGIN) && effect) {
		const idVec3 delta = effect->GetOriginalEndOrigin() - effect->GetOriginalOrigin();
		xExtent = delta.Length() - mpSpawnPosition->mMins.x;
	}
	else {
		xExtent = mpSpawnPosition->mMaxs.x - mpSpawnPosition->mMins.x;
	}

	const float yExtent = mpSpawnPosition->mMaxs.y - mpSpawnPosition->mMins.y;
	const float zExtent = mpSpawnPosition->mMaxs.z - mpSpawnPosition->mMins.z;
	const float volume = (idMath::Fabs(xExtent) + idMath::Fabs(yExtent) + idMath::Fabs(zExtent)) * 0.01f;
	return Max(0.0f, volume);
}

float rvParticleTemplate::GetMaxParmValue(rvParticleParms& spawn, rvParticleParms& death, rvEnvParms& envelope) {
	idVec3 spawnMins;
	idVec3 spawnMaxs;
	idVec3 deathMins;
	idVec3 deathMaxs;
	spawn.GetMinsMaxs(spawnMins, spawnMaxs);
	death.GetMinsMaxs(deathMins, deathMaxs);

	idBounds bounds;
	bounds.Clear();

	float envMin = 0.0f;
	float envMax = 0.0f;
	if (envelope.GetMinMax(envMin, envMax)) {
		const idVec3 samples[] = {
			spawnMins * envMin, spawnMaxs * envMin, spawnMins * envMax, spawnMaxs * envMax,
			deathMins * envMin, deathMaxs * envMin, deathMins * envMax, deathMaxs * envMax
		};
		for (int i = 0; i < 8; ++i) {
			bounds.AddPoint(samples[i]);
		}
	}
	else {
		bounds.AddPoint(spawnMins);
		bounds.AddPoint(spawnMaxs);
		bounds.AddPoint(deathMins);
		bounds.AddPoint(deathMaxs);
	}

	return Max(bounds[0].Length(), bounds[1].Length());
}

float rvParticleTemplate::GetMaxSize(void) {
	if (!mpSpawnSize || !mpDeathSize || !mpSizeEnvelope) {
		return 0.0f;
	}
	return GetMaxParmValue(*mpSpawnSize, *mpDeathSize, *mpSizeEnvelope);
}

float rvParticleTemplate::GetMaxOffset(void) {
	if (!mpSpawnOffset || !mpDeathOffset || !mpOffsetEnvelope) {
		return 0.0f;
	}
	return GetMaxParmValue(*mpSpawnOffset, *mpDeathOffset, *mpOffsetEnvelope);
}

float rvParticleTemplate::GetMaxLength(void) {
	if (!mpSpawnLength || !mpDeathLength || !mpLengthEnvelope) {
		return 0.0f;
	}
	return GetMaxParmValue(*mpSpawnLength, *mpDeathLength, *mpLengthEnvelope);
}

void rvParticleTemplate::EvaluateSimplePosition(
	idVec3& pos,
	float time,
	float lifeTime,
	idVec3& initPos,
	idVec3& velocity,
	idVec3& acceleration,
	idVec3& friction) {
	const float t = time;
	const float halfT2 = 0.5f * t * t;

	pos.x = initPos.x + velocity.x * t + acceleration.x * halfT2;
	pos.y = initPos.y + velocity.y * t + acceleration.y * halfT2;
	pos.z = initPos.z + velocity.z * t + acceleration.z * halfT2;

	const float safeLifetime = Max(BSE_TIME_EPSILON, lifeTime);
	const float expFactor = idMath::Exp((safeLifetime - halfT2) / safeLifetime) - 1.0f;
	const float frictionScale = (halfT2 * halfT2 * expFactor) / 3.0f;

	pos += friction * frictionScale;
}

float rvParticleTemplate::GetFurthestDistance(void) {
	if (!mpSpawnPosition || !mpSpawnVelocity || !mpSpawnAcceleration || !mpSpawnFriction) {
		return 0.0f;
	}

	idVec3 minPos;
	idVec3 maxPos;
	idVec3 minVel;
	idVec3 maxVel;
	idVec3 minAccel;
	idVec3 maxAccel;
	idVec3 minFriction;
	idVec3 maxFriction;
	mpSpawnPosition->GetMinsMaxs(minPos, maxPos);
	mpSpawnVelocity->GetMinsMaxs(minVel, maxVel);
	mpSpawnAcceleration->GetMinsMaxs(minAccel, maxAccel);
	mpSpawnFriction->GetMinsMaxs(minFriction, maxFriction);

	const bool multiplayer = (game != NULL) ? game->IsMultiplayer() : false;
	const float gravityMagnitude = cvarSystem->GetCVarFloat(multiplayer ? "g_mp_gravity" : "g_gravity");
	const float gravityScale = Max(idMath::Fabs(mGravity.x), idMath::Fabs(mGravity.y));
	const idVec3 gravityVec(0.0f, 0.0f, -gravityMagnitude * gravityScale);
	minAccel -= gravityVec;
	maxAccel -= gravityVec;

	const float duration = Max(BSE_TIME_EPSILON, mDuration.y);
	const float step = duration * 0.125f;

	idVec3 overallMins(1.0e30f, 1.0e30f, 1.0e30f);
	idVec3 overallMaxs(-1.0e30f, -1.0e30f, -1.0e30f);
	idVec3 pos;

	for (int i = 0; i < 8; ++i) {
		const float sampleTime = i * step;
		for (int p = 0; p < 2; ++p) {
			idVec3 initPos = p ? maxPos : minPos;
			for (int v = 0; v < 2; ++v) {
				idVec3 velocity = v ? maxVel : minVel;
				for (int a = 0; a < 2; ++a) {
					idVec3 accel = a ? maxAccel : minAccel;
					for (int f = 0; f < 2; ++f) {
						idVec3 friction = f ? maxFriction : minFriction;
						EvaluateSimplePosition(pos, sampleTime, duration, initPos, velocity, accel, friction);
						BSE_UpdateExtents(pos, overallMins, overallMaxs);
					}
				}
			}
		}
	}

	return Max(overallMins.Length() * 0.5f, overallMaxs.Length() * 0.5f);
}

void rvParticleTemplate::AllocTrail()
{
	if (mTrailInfo != NULL && !mTrailInfo->mStatic) {
		return;
	}

	rvTrailInfo* info = new rvTrailInfo();
	*info = sTrailInfo;
	info->mStatic = 0;
	mTrailInfo = info;
}

void rvParticleTemplate::AllocElectricityInfo() {
	if (mElecInfo != NULL && !mElecInfo->mStatic) {
		return;
	}

	rvElectricityInfo* info = new rvElectricityInfo();
	*info = sElectricityInfo;
	info->mStatic = 0;
	mElecInfo = info;
}

bool rvParticleTemplate::ParseBlendParms(rvDeclEffect* effect, idParser* src)
{
	rvParticleTemplate* v3; // edi
	char result; // al
	idLexer* v5; // eax
	int v6; // edi
	idBitMsg** v7; // esi
	int v8; // eax
	idToken token; // [esp+0h] [ebp-60h]
	short v10; // [esp+50h] [ebp-10h]
	int v11; // [esp+5Ch] [ebp-4h]

	v3 = this;
	v10 = 0;
	v11 = 0;
	if (src->ReadToken(&token))
	{
		if (idStr::Icmp(token, "add"))
		{
			BSE_ParticleWarning(src, effect, "Invalid blend type '%s'", token.c_str());
		}
		else
		{
			v3->mFlags |= 0x8000u;
		}
		v11 = -1;
		//idStr::FreeData((idStr*)&token.data);
		result = 1;
	}
	else
	{
		v11 = -1;
	//	idStr::FreeData((idStr*)&token.data);
		result = 0;
	}
	return result;
}

bool rvParticleTemplate::ParseImpact(rvDeclEffect* effect, idParser* src)
{
	int v3; // ebp
	rvParticleTemplate* v4; // esi
	int v6; // edi
	//sdDeclTypeHolder* v7; // eax
	idLexer* v8; // eax
	idBitMsg** v9; // edi
	int v10; // ST0C_4
	idLexer* v11; // eax
	idBitMsg** v12; // edi
	int v13; // ST0C_4
	idToken token; // [esp+4h] [ebp-60h]
	short v15; // [esp+54h] [ebp-10h]
	int v16; // [esp+60h] [ebp-4h]

	v3 = 0;
	v4 = this;
	v15 = 0;
	v16 = 0;
	if (!src->ExpectTokenString("{"))
	{
		v16 = -1;
		return false;
	}
	v4->mFlags |= 0x200u;
	if (!src->ReadToken(&token))
	{
	LABEL_30:
		v16 = -1;	
		return 0;
	}
	while (idStr::Cmp(token, "}"))
	{
		if (idStr::Icmp(token, "effect"))
		{
			if (!idStr::Icmp(token, "remove"))
			{
				if (src->ParseInt() != 0)
					v4->mFlags |= 0x400u;
				else
					v4->mFlags &= 0xFFFFFBFF;
				goto LABEL_29;
			}
			if (idStr::Icmp(token, "bounce"))
			{
				if (idStr::Icmp(token, "physicsDistance"))
				{
					//v11 = src->scriptstack;
					//if (v11)
					//	v3 = v11->line;
					//if (v11)
					//	v12 = (idBitMsg**)v11->filename.data;
					//else
					//	v12 = &s2;
					//v13 = (*(int (**)(void))effect->base->vfptr->gap4)();
					//(*(void(__cdecl**)(netadrtype_t, const char*, int, int, idBitMsg**, int))(*(_DWORD*)common.type + 68))(
					//	common.type,
					//	"^4BSE:^1 Invalid impact parameter '%s' in '%s' (file: %s, line: %d)",
					//	token.alloced,
					//	v13,
					//	v12,
					//	v3);
					BSE_ParticleWarning(src, effect, "Invalid impact parameter '%s'", token.c_str());
					goto LABEL_29;
				}
				v4->mPhysicsDistance = src->ParseFloat();
			}
			else
			{
				v4->mBounce = src->ParseFloat();
			}
		}
		else
		{
			//idParser::ReadToken(src, (idToken*)((char*)&token + 4));
			if (!src->ReadToken(&token)) {
				return false;
			}
			if (v4->mNumImpactEffects >= 4)
			{
				BSE_ParticleWarning(src, effect, "too many impact effects '%s'", token.c_str());
				goto LABEL_29;
			}
			//v7 = sdSingleton<sdDeclTypeHolder>::GetInstance();
			//v4->mImpactEffects[v4->mNumImpactEffects++] = (rvDeclEffect*)((int(__stdcall*)(int, int, signed int))declManager->vfptr->FindType)(
			//	v7->declEffectsType.declTypeHandle,
			//	v6,
			//	1);

			v4->mImpactEffects[v4->mNumImpactEffects++] = declManager->FindEffect(BSE_ResolveEffectCompatAlias(token));
		}
	LABEL_29:
		if (!src->ReadToken(&token))
			goto LABEL_30;
	}
	v16 = -1;
	return true;
}

bool rvParticleTemplate::ParseTimeout(rvDeclEffect* effect, idParser* src)
{
	rvParticleTemplate* v3; // edi
	idParser* v4; // ebp
	char result; // al
	int v6; // esi
	//sdDeclTypeHolder* v7; // eax
	idLexer* v8; // eax
	int v9; // ebp
	idBitMsg** v10; // esi
	int v11; // ST0C_4
	idLexer* v12; // eax
	int v13; // ebp
	idBitMsg** v14; // esi
	int v15; // ST0C_4
	idToken token; // [esp+4h] [ebp-60h]
	short v17; // [esp+54h] [ebp-10h]
	int v18; // [esp+60h] [ebp-4h]

	v3 = this;
	v17 = 0;
	v4 = src;
	v18 = 0;
	if (src->ExpectTokenString("{"))
	{
		if (src->ReadToken(&token))
		{
			while (idStr::Cmp(token, "}"))
			{
				if (idStr::Icmp(token, "effect"))
				{
					//v12 = v4->scriptstack;
					//if (v12)
					//	v13 = v12->line;
					//else
					//	v13 = 0;
					//if (v12)
					//	v14 = (idBitMsg**)v12->filename.data;
					//else
					//	v14 = &s2;
					//v15 = (*(int (**)(void))effect->base->vfptr->gap4)();
					//(*(void(__cdecl**)(netadrtype_t, const char*, int, int, idBitMsg**, int))(*(_DWORD*)common.type + 68))(
					//	common.type,
					//	"^4BSE:^1 Invalid timeout parameter '%s' in '%s' (file: %s, line: %d)",
					//	token.alloced,
					//	v15,
					//	v14,
					//	v13);
					BSE_ParticleWarning(src, effect, "Invalid timeout parameter '%s'", token.c_str());
				}
				else
				{
					if (!src->ReadToken(&token)) {
						return false;
					}
					if (v3->mNumTimeoutEffects >= 4)
					{
						//v8 = v4->scriptstack;
						//if (v8)
						//	v9 = v8->line;
						//else
						//	v9 = 0;
						//if (v8)
						//	v10 = (idBitMsg**)v8->filename.data;
						//else
						//	v10 = &s2;
						//v11 = (*(int (**)(void))effect->base->vfptr->gap4)();
						//(*(void(__cdecl**)(netadrtype_t, const char*, int, int, idBitMsg**, int))(*(_DWORD*)common.type + 68))(
						//	common.type,
						//	"^4BSE:^1 Too many timeout effects '%s' in '%s' (file: %s, line: %d)",
						//	token.alloced,
						//	v11,
						//	v10,
						//	v9);
						BSE_ParticleWarning(src, effect, "Too many timeout effects '%s'", token.c_str());
					}
					else
					{
						//v6 = token.alloced;
						//v7 = sdSingleton<sdDeclTypeHolder>::GetInstance();
						//v3->mTimeoutEffects[v3->mNumTimeoutEffects++] = (rvDeclEffect*)((int(__stdcall*)(int, int, signed int))declManager->vfptr->FindType)(
						//	v7->declEffectsType.declTypeHandle,
						//	v6,
						//	1);

						v3->mTimeoutEffects[v3->mNumTimeoutEffects++] = declManager->FindEffect(token);
					}
				}
				if (!src->ReadToken(&token))
					goto LABEL_25;
				v4 = src;
			}
			v18 = -1;
			//idStr::FreeData((idStr*)&token.data);
			result = 1;
		}
		else
		{
		LABEL_25:
			v18 = -1;
			//idStr::FreeData((idStr*)&token.data);
			result = 0;
		}
	}
	else
	{
		v18 = -1;
	//	idStr::FreeData((idStr*)&token.data);
		result = 0;
	}
	return result;
}

rvEnvParms* rvParticleTemplate::ParseMotionParms(idParser* src, int count, rvEnvParms* def)
{
	rvEnvParms* result; // eax
	//sdDetails::sdPoolAlloc<rvEnvParms, 128>* v4; // ecx
	//rvEnvParms* v5; // eax
	rvEnvParms* v6; // esi
	int v7; // ebp
	//sdDeclTypeHolder* v8; // eax
	idLexer* v9; // eax
	int v10; // edx
	idBitMsg** v11; // ecx
	//sdDetails::sdPoolAlloc<rvEnvParms, 128>* v12; // eax
	//sdDetails::sdPoolAlloc<rvEnvParms, 128>* v13; // eax
	idToken token; // [esp+4h] [ebp-60h]
	short v15; // [esp+54h] [ebp-10h]
	int v16; // [esp+60h] [ebp-4h]

	v15 = 0;
	v16 = 0;
	if (src->ExpectTokenString("{"))
	{
		//v4 = sdPoolAllocator<rvEnvParms, &char const* const sdPoolAllocator_rvEnvParms, 128, sdLockingPolicy_None>::GetMemoryManager()->allocator;
		//if (v4 && (v5 = sdDetails::sdPoolAlloc<rvEnvParms, 128>::Alloc(v4)) != 0)
		//{
		//	v5->mStatic = 0;
		//	v5->mFastLookUp = 0;
		//	v6 = v5;
		//}
		//else
		//{
		//	v6 = 0;
		//}
		v6 = new rvEnvParms(); // jmarshall: mem leak
		v6->Init();
		if (src->ReadToken(&token))
		{
			while (idStr::Cmp(token, "}"))
			{
				if (idStr::Icmp(token, "envelope"))
				{
					if (idStr::Icmp(token, "rate"))
					{
						if (idStr::Icmp(token, "count"))
						{
							if (idStr::Icmp(token, "offset"))
							{
								//v9 = src->scriptstack;
								//if (v9)
								//	v10 = v9->line;
								//else
								//	v10 = 0;
								//if (v9)
								//	v11 = (idBitMsg**)v9->filename.data;
								//else
								//	v11 = &s2;
								//(*(void (**)(netadrtype_t, const char*, ...))(*(_DWORD*)common.type + 68))(
								//	common.type,
								//	"^4BSE:^1 Invalid motion parameter '%s' (file: %s, line: %d)",
								//	token.alloced,
								//	v11,
								//	v10);

								BSE_ParticleWarning(src, NULL, "Invalid motion parameter '%s'", token.c_str());
								src->SkipBracedSection(1);
							}
							else
							{
							//	rvParticleTemplate::GetVector(src, count, v6->mEnvOffset);

								src->Parse1DMatrix(count, v6->mEnvOffset.ToFloatPtr(), true);
							}
						}
						else
						{
							//rvParticleTemplate::GetVector(src, count, v6->mRate);
							src->Parse1DMatrix(count, v6->mRate.ToFloatPtr(), true);
							v6->mIsCount = 1;
						}
					}
					else
					{
						//rvParticleTemplate::GetVector(src, count, v6->mRate);
						src->Parse1DMatrix(count, v6->mRate.ToFloatPtr(), true);
						v6->mIsCount = 0;
					}
				}
				else
				{
					//idParser::ReadToken(src, (idToken*)((char*)&token + 4));
					//v7 = token.alloced;
					//v8 = sdSingleton<sdDeclTypeHolder>::GetInstance();
					//v6->mTable = (idDeclTable*)((int(__stdcall*)(int, int, signed int))declManager->vfptr->FindType)(
					//	v8->declTableType.declTypeHandle,
					//	v7,
					//	1);

					if (!src->ReadToken(&token)) {
						goto LABEL_25;
					}
					v6->mTable = declManager->FindTable(token);
				}
				if (!src->ReadToken(&token))
					goto LABEL_25;
			}
			v6->Finalize();
			if (v6->Compare(*def))
			{
				delete v6;
				v16 = -1;
				//idStr::FreeData((idStr*)&token.data);
				result = def;
			}
			else
			{
				v16 = -1;
				//idStr::FreeData((idStr*)&token.data);
				result = v6;
			}
		}
		else
		{
		LABEL_25:
			if (v6)
			{
				delete v6;
			}
			v16 = -1;
		//	idStr::FreeData((idStr*)&token.data);
			result = def;
		}
	}
	else
	{
		v16 = -1;
		//idStr::FreeData((idStr*)&token.data);
		result = def;
	}
	return result;
}


bool rvParticleTemplate::ParseMotionDomains(rvDeclEffect* effect, idParser* src)
{
	rvParticleTemplate* v3; // esi
	idParser* v4; // edi
	char result; // al
	idLexer* v6; // eax
	idBitMsg** v7; // ebp
	int v8; // eax
	idToken token; // [esp+0h] [ebp-60h]
	short v10; // [esp+50h] [ebp-10h]
	int v11; // [esp+5Ch] [ebp-4h]
	idParser* srca; // [esp+68h] [ebp+8h]

	v3 = this;
	v10 = 0;
	v4 = src;
	v11 = 0;
	if (src->ExpectTokenString("{"))
	{
		if (src->ReadToken(&token))
		{
			while (token.Cmp("}"))
			{
				if (token.Icmp("tint"))
				{
					if (token.Icmp("fade"))
					{
						if (token.Icmp("size"))
						{
							if (token.Icmp("rotate"))
							{
								if (token.Icmp("angle"))
								{
									if (token.Icmp("offset"))
									{
										if (token.Icmp("length"))
										{
											//v6 = v4->scriptstack;
											//if (v6)
											//	srca = (idParser*)v6->line;
											//else
											//	srca = 0;
											//if (v6)
											//	v7 = (idBitMsg**)v6->filename.data;
											//else
											//	v7 = &s2;
											//v8 = (*(int (**)(void))effect->base->vfptr->gap4)();
											//(*(void (**)(netadrtype_t, const char*, ...))(*(_DWORD*)common.type + 68))(
											//	common.type,
											//	"^4BSE:^1 Invalid motion domain '%s' in %s (file: %s, line: %d)",
											//	token.alloced,
											//	v8,
											//	v7,
											//	srca);

											BSE_ParticleWarning(src, effect, "Invalid motion domain '%s'", token.c_str());
											src->SkipBracedSection(1);
										}
										else
										{
											BSE_AssignEnvPtr(v3->mpLengthEnvelope, rvParticleTemplate::ParseMotionParms(
												v4,
												3,
												&rvParticleTemplate::sDefaultEnvelope));
										}
									}
									else
									{
										BSE_AssignEnvPtr(v3->mpOffsetEnvelope, rvParticleTemplate::ParseMotionParms(
											v4,
											3,
											&rvParticleTemplate::sDefaultEnvelope));
									}
								}
								else
								{
									BSE_AssignEnvPtr(v3->mpAngleEnvelope, rvParticleTemplate::ParseMotionParms(
										v4,
										3,
										&rvParticleTemplate::sDefaultEnvelope));
								}
							}
							else
							{
								BSE_AssignEnvPtr(v3->mpRotateEnvelope, rvParticleTemplate::ParseMotionParms(
									v4,
									(unsigned char)v3->mNumRotateParms,
									&rvParticleTemplate::sDefaultEnvelope));
							}
						}
						else
						{
							BSE_AssignEnvPtr(v3->mpSizeEnvelope, rvParticleTemplate::ParseMotionParms(
								v4,
								(unsigned char)v3->mNumSizeParms,
								&rvParticleTemplate::sDefaultEnvelope));
						}
					}
					else
					{
						BSE_AssignEnvPtr(v3->mpFadeEnvelope, rvParticleTemplate::ParseMotionParms(v4, 1, &rvParticleTemplate::sDefaultEnvelope));
					}
				}
				else
				{
					BSE_AssignEnvPtr(v3->mpTintEnvelope, rvParticleTemplate::ParseMotionParms(v4, 3, &rvParticleTemplate::sDefaultEnvelope));
				}
				if (!src->ReadToken(&token))
					goto LABEL_27;
			}
			v11 = -1;
			//idStr::FreeData((idStr*)&token.data);
			result = 1;
		}
		else
		{
		LABEL_27:
			v11 = -1;
			//idStr::FreeData((idStr*)&token.data);
			result = 0;
		}
	}
	else
	{
		v11 = -1;
		//idStr::FreeData((idStr*)&token.data);
		result = 0;
	}
	return result;
}

void rvParticleTemplate::FixupParms(rvParticleParms* parms)
{
	if (!parms) {
		return;
	}

	const int axisBits = parms->mSpawnType & 3;
	const int shapeBits = parms->mSpawnType & ~3;

	// Preserve explicit point/unit and tapered legacy variants.
	if (shapeBits == 0 || shapeBits == 4 || parms->mSpawnType == 43 || parms->mSpawnType == 47) {
		return;
	}

	const idVec3& mins = parms->mMins;
	const idVec3& maxs = parms->mMaxs;

	const bool minsEqualY = (axisBits < 2) || (mins.y == mins.x);
	const bool minsEqualZ = (axisBits < 3) || (mins.z == mins.x);
	const bool maxsEqualX = (maxs.x == mins.x);
	const bool maxsEqualY = (axisBits < 2) || (maxs.y == mins.x);
	const bool maxsEqualZ = (axisBits < 3) || (maxs.z == mins.x);

	if (shapeBits == 8 && minsEqualY && minsEqualZ && maxsEqualX && maxsEqualY && maxsEqualZ) {
		if (mins.x == 0.0f) {
			parms->mSpawnType = axisBits;
		}
		else if (idMath::Fabs(mins.x - 1.0f) < idMath::FLOAT_EPSILON) {
			parms->mSpawnType = axisBits + 4;
		}
		else {
			parms->mSpawnType = axisBits + 8;
		}
	}
	else if (shapeBits == 8) {
		parms->mSpawnType = axisBits + 8;
	}

	if (parms->mSpawnType >= 8) {
		if (axisBits == 1) {
			parms->mMins.y = 0.0f;
			parms->mMaxs.y = 0.0f;
			parms->mMins.z = 0.0f;
			parms->mMaxs.z = 0.0f;
		}
		else if (axisBits == 2) {
			parms->mMins.z = 0.0f;
			parms->mMaxs.z = 0.0f;
		}
	}
	else {
		parms->mMins = vec3_origin;
		parms->mMaxs = vec3_origin;
	}

	if (parms->mSpawnType <= 11) {
		parms->mMaxs = parms->mMins;
	}

	if ((parms->mFlags & 2) && parms->mSpawnType <= 12) {
		parms->mSpawnType = axisBits + 12;
	}
}

bool rvParticleTemplate::CheckCommonParms(idParser* src, rvParticleParms& parms)
{
	bool result; // al
	idToken token; // [esp+0h] [ebp-60h]
	short v4; // [esp+50h] [ebp-10h]
	int v5; // [esp+5Ch] [ebp-4h]

	if (src->ReadToken(&token))
	{
		while (idStr::Cmp((const char*)token, "}"))
		{
			if (idStr::Icmp((const char*)token, "surface"))
			{
				if (idStr::Icmp((const char*)token, "useEndOrigin"))
				{
					if (idStr::Icmp((const char*)token, "cone"))
					{
						if (idStr::Icmp((const char*)token, "relative"))
						{
							if (idStr::Icmp((const char*)token, "linearSpacing"))
							{
								if (idStr::Icmp((const char*)token, "attenuate"))
								{
									if (!idStr::Icmp((const char*)token, "inverseAttenuate"))
										parms.mFlags |= 0x40u;
								}
								else
								{
									parms.mFlags |= 0x20u;
								}
							}
							else
							{
								parms.mFlags |= 0x10u;
							}
						}
						else
						{
							parms.mFlags |= 8u;
						}
					}
					else
					{
						parms.mFlags |= 4u;
					}
				}
				else
				{
					parms.mFlags |= 2u;
				}
			}
			else
			{
				parms.mFlags |= 1u;
			}
			if (!src->ReadToken(&token))
				goto LABEL_18;
		}
		v5 = -1;
	//	idStr::FreeData((idStr*)&token.data);
		result = 1;
	}
	else
	{
	LABEL_18:
		v5 = -1;
	//	idStr::FreeData((idStr*)&token.data);
		result = 0;
	}
	return result;
}

rvParticleParms* rvParticleTemplate::ParseSpawnParms(rvDeclEffect* effect, idParser* src, int count, rvParticleParms* def) {
	idToken token;
	rvParticleParms* v8; 

	if (!src->ExpectTokenString("{")) {
		return def;
	}

	if (!src->ReadToken(&token)) {
		return def;
	}

	if (token == "}") {
		return def;
	}

	v8 = new rvParticleParms();
	v8->Init();

	if (!token.Icmp("box")) {
		v8->mSpawnType = count + 16;
		src->Parse1DMatrix(count, v8->mMins.ToFloatPtr(), true);
		src->ExpectTokenString(",");
		src->Parse1DMatrix(count, v8->mMaxs.ToFloatPtr(), true);

		if (!rvParticleTemplate::CheckCommonParms(src, *v8))
		{
			BSE_ParticleWarning(src, effect, "Invalid box parameter '%s'", token.c_str());
		}

		if (v8->mFlags & 1)
			v8->mSpawnType = count + 20;
		FixupParms(v8);
		return v8;
	}
	else if (!token.Icmp("sphere")) {
		v8->mSpawnType = count + 24;

		src->Parse1DMatrix(count, v8->mMins.ToFloatPtr(), true);
		src->ExpectTokenString(",");
		src->Parse1DMatrix(count, v8->mMaxs.ToFloatPtr(), true);

		if (!rvParticleTemplate::CheckCommonParms(src, *v8))
		{
			BSE_ParticleWarning(src, effect, "Invalid sphere parameter '%s'", token.c_str());
		}

		if (v8->mFlags & 1)
			v8->mSpawnType = count + 28;
		FixupParms(v8);

		return v8;
	}
	else if (!token.Icmp("cylinder")) {
		v8->mSpawnType = count + 32;

		src->Parse1DMatrix(count, v8->mMins.ToFloatPtr(), true);
		src->ExpectTokenString(",");
		src->Parse1DMatrix(count, v8->mMaxs.ToFloatPtr(), true);

		if (!rvParticleTemplate::CheckCommonParms(src, *v8))
		{
			BSE_ParticleWarning(src, effect, "Invalid cylinder parameter '%s'", token.c_str());
		}

		if (v8->mFlags & 1)
			v8->mSpawnType = count + 36;
		FixupParms(v8);

		return v8;
	}
	else if (!token.Icmp("model")) {
		v8->mSpawnType = count + 44;
		if (!src->ReadToken(&token)) {
			delete v8;
			return def;
		}

		idRenderModel* model = renderModelManager->FindModel(token);
		if (model == NULL || model->NumSurfaces() <= 0) {
			BSE_ParticleWarning(src, effect, "No surfaces defined in model '%s'", token.c_str());
			model = renderModelManager->FindModel("_default");
		}

		v8->mModelInfo = new sdModelInfo();
		v8->mModelInfo->model = model;

		src->ExpectTokenString(",");
		src->Parse1DMatrix(count, v8->mMins.ToFloatPtr(), true);
		src->ExpectTokenString(",");
		src->Parse1DMatrix(count, v8->mMaxs.ToFloatPtr(), true);

		if (!rvParticleTemplate::CheckCommonParms(src, *v8))
		{
			BSE_ParticleWarning(src, effect, "Invalid model parameter '%s'", token.c_str());
		}
		FixupParms(v8);
		return v8;
	}
	else if (!token.Icmp("spiral")) {
		v8->mSpawnType = count + 40;

		src->Parse1DMatrix(count, v8->mMins.ToFloatPtr(), true);
		src->ExpectTokenString(",");
		src->Parse1DMatrix(count, v8->mMaxs.ToFloatPtr(), true);
		src->CheckTokenString(",");

		v8->mRange = src->ParseFloat();

		if (!rvParticleTemplate::CheckCommonParms(src, *v8))
		{
			BSE_ParticleWarning(src, effect, "Invalid spiral parameter '%s'", token.c_str());
		}
		FixupParms(v8);
		return v8;
	}
	else if (!token.Icmp("line")) {
		v8->mSpawnType = count + 12;

		src->Parse1DMatrix(count, v8->mMins.ToFloatPtr(), true);
		src->ExpectTokenString(",");
		src->Parse1DMatrix(count, v8->mMaxs.ToFloatPtr(), true);

		if (!rvParticleTemplate::CheckCommonParms(src, *v8))
		{
			BSE_ParticleWarning(src, effect, "Invalid line parameter '%s'", token.c_str());
		}
		FixupParms(v8);
		return v8;
	}
	else if (!token.Icmp("point")) {
		v8->mSpawnType = count + 8;

		src->Parse1DMatrix(count, v8->mMins.ToFloatPtr(), true);

		if (!rvParticleTemplate::CheckCommonParms(src, *v8))
		{
			BSE_ParticleWarning(src, effect, "Invalid point parameter '%s'", token.c_str());
		}
		FixupParms(v8);
		return v8;
	}

	BSE_ParticleWarning(src, effect, "Invalid spawn type '%s'", token.c_str());
	while (token != "}") {
		if (!src->ReadToken(&token)) {
			break;
		}
	}
	delete v8;

	return def;
}

bool rvParticleTemplate::ParseSpawnDomains(rvDeclEffect* effect, idParser* src) {
	idToken token;

	if (!src->ExpectTokenString("{")) {
		return false;
	}

	while (src->ReadToken(&token)) {
		if (token == "}") {
			return true;
		}

		if (!token.Icmp("windStrength")) {
			BSE_AssignParmPtr(mpSpawnWindStrength, ParseSpawnParms(effect, src, 1, &rvParticleTemplate::sSPF_NONE_1));
		}
		else if (!token.Icmp("length")) {
			BSE_AssignParmPtr(mpSpawnLength, ParseSpawnParms(effect, src, 3, &rvParticleTemplate::sSPF_NONE_3));
		}
		else if (!token.Icmp("offset")) {
			BSE_AssignParmPtr(mpSpawnOffset, ParseSpawnParms(effect, src, 3, &rvParticleTemplate::sSPF_NONE_3));
		}
		else if (!token.Icmp("angle")) {
			BSE_AssignParmPtr(mpSpawnAngle, ParseSpawnParms(effect, src, 3, &rvParticleTemplate::sSPF_NONE_3));
		}
		else if (!token.Icmp("rotate")) {
			BSE_AssignParmPtr(mpSpawnRotate, ParseSpawnParms(effect, src, mNumRotateParms, &rvParticleTemplate::sSPF_NONE_3));
		}
		else if (!token.Icmp("size")) {
			BSE_AssignParmPtr(mpSpawnSize, ParseSpawnParms(effect, src, mNumSizeParms, &rvParticleTemplate::sSPF_ONE_3));
		}
		else if (!token.Icmp("fade")) {
			BSE_AssignParmPtr(mpSpawnFade, ParseSpawnParms(effect, src, 1, &rvParticleTemplate::sSPF_ONE_1));
		}
		else if (!token.Icmp("tint")) {
			BSE_AssignParmPtr(mpSpawnTint, ParseSpawnParms(effect, src, 3, &rvParticleTemplate::sSPF_ONE_3));
		}
		else if (!token.Icmp("friction")) {
			BSE_AssignParmPtr(mpSpawnFriction, ParseSpawnParms(effect, src, 3, &rvParticleTemplate::sSPF_NONE_3));
		}
		else if (!token.Icmp("acceleration")) {
			BSE_AssignParmPtr(mpSpawnAcceleration, ParseSpawnParms(effect, src, 3, &rvParticleTemplate::sSPF_NONE_3));
		}
		else if (!token.Icmp("velocity")) {
			BSE_AssignParmPtr(mpSpawnVelocity, ParseSpawnParms(effect, src, 3, &rvParticleTemplate::sSPF_NONE_3));
		}
		else if (!token.Icmp("direction")) {
			BSE_AssignParmPtr(mpSpawnDirection, ParseSpawnParms(effect, src, 3, &rvParticleTemplate::sSPF_NONE_3));
			mFlags |= 0x4000u;
		}
		else if (!token.Icmp("position")) {
			BSE_AssignParmPtr(mpSpawnPosition, ParseSpawnParms(effect, src, 3, &rvParticleTemplate::sSPF_NONE_3));
		}
		else {
			BSE_ParticleWarning(src, effect, "Invalid spawn type '%s'", token.c_str());
		}
	}

	return false;
}

bool rvParticleTemplate::ParseDeathDomains(rvDeclEffect* effect, idParser* src) {
	idToken token;

	if (!src->ExpectTokenString("{")) {
		return false;
	}

	while (src->ReadToken(&token)) {
		if (token == "}") {
			return true;
		}

		if (!token.Icmp("length")) {
			rvParticleParms* v13 = ParseSpawnParms(effect, src, 3, &rvParticleTemplate::sSPF_NONE_3);
			bool v7 = mpLengthEnvelope == &rvParticleTemplate::sEmptyEnvelope;
			BSE_AssignParmPtr(mpDeathLength, v13);
			if (v7) {
				BSE_AssignEnvPtr(mpLengthEnvelope, &rvParticleTemplate::sDefaultEnvelope);
			}
		}
		else if (!token.Icmp("offset")) {
			rvParticleParms* v13 = ParseSpawnParms(effect, src, 3, &rvParticleTemplate::sSPF_NONE_3);
			bool v7 = mpOffsetEnvelope == &rvParticleTemplate::sEmptyEnvelope;
			BSE_AssignParmPtr(mpDeathOffset, v13);
			if (v7) {
				BSE_AssignEnvPtr(mpOffsetEnvelope, &rvParticleTemplate::sDefaultEnvelope);
			}
		}
		else if (!token.Icmp("angle")) {
			rvParticleParms* v13 = ParseSpawnParms(effect, src, 3, &rvParticleTemplate::sSPF_NONE_3);
			bool v7 = mpAngleEnvelope == &rvParticleTemplate::sEmptyEnvelope;
			BSE_AssignParmPtr(mpDeathAngle, v13);
			if (v7) {
				BSE_AssignEnvPtr(mpAngleEnvelope, &rvParticleTemplate::sDefaultEnvelope);
			}
		}
		else if (!token.Icmp("rotate")) {
			rvParticleParms* v10 = ParseSpawnParms(effect, src, mNumRotateParms, &rvParticleTemplate::sSPF_NONE_3);
			BSE_AssignParmPtr(mpDeathRotate, v10);
			if (mpRotateEnvelope == &rvParticleTemplate::sEmptyEnvelope) {
				BSE_AssignEnvPtr(mpRotateEnvelope, &rvParticleTemplate::sDefaultEnvelope);
			}
		}
		else if (!token.Icmp("size")) {
			rvParticleParms* v9 = ParseSpawnParms(effect, src, mNumSizeParms, &rvParticleTemplate::sSPF_ONE_3);
			BSE_AssignParmPtr(mpDeathSize, v9);
			if (mpSizeEnvelope == &rvParticleTemplate::sEmptyEnvelope) {
				BSE_AssignEnvPtr(mpSizeEnvelope, &rvParticleTemplate::sDefaultEnvelope);
			}
		}
		else if (!token.Icmp("fade")) {
			rvParticleParms* v8 = ParseSpawnParms(effect, src, 1, &rvParticleTemplate::sSPF_NONE_1);
			BSE_AssignParmPtr(mpDeathFade, v8);
			if (mpFadeEnvelope == &rvParticleTemplate::sEmptyEnvelope) {
				BSE_AssignEnvPtr(mpFadeEnvelope, &rvParticleTemplate::sDefaultEnvelope);
			}
		}
		else if (!token.Icmp("tint")) {
			rvParticleParms* v6 = ParseSpawnParms(effect, src, 3, &rvParticleTemplate::sSPF_NONE_3);
			BSE_AssignParmPtr(mpDeathTint, v6);
			if (mpTintEnvelope == &rvParticleTemplate::sEmptyEnvelope) {
				BSE_AssignEnvPtr(mpTintEnvelope, &rvParticleTemplate::sDefaultEnvelope);
			}
		}
		else {
			BSE_ParticleWarning(src, effect, "Invalid end type '%s'", token.c_str());
		}
	}

	return false;
}

bool rvParticleTemplate::Parse(rvDeclEffect* effect, idParser* src) {
	idToken token;

	if (!src->ExpectTokenString("{")) {
		return false;
	}
	while (src->ReadToken(&token)) {
		if (token == "}") {
			Finish();
			return true;
		}

		if (!token.Icmp("windDeviationAngle")) {
			mWindDeviationAngle = src->ParseFloat();
		}
		else if (!token.Icmp("timeout")) {
			if (!ParseTimeout(effect, src)) {
				return false;
			}
		}
		else if (!token.Icmp("impact")) {
			if (!ParseImpact(effect, src)) {
				return false;
			}
		}
		else if (!token.Icmp("model")) {
			if (!src->ReadToken(&token)) {
				return false;
			}
			mModel = renderModelManager->FindModel(token);

			if (mModel == NULL || mModel->NumSurfaces() <= 0) {
				mModel = renderModelManager->FindModel("_default");

				BSE_ParticleWarning(src, effect, "No surfaces defined in model '%s'", token.c_str());
			}
		}
		else if (!token.Icmp("numFrames")) {
			mNumFrames = src->ParseInt();
		}
		else if (!token.Icmp("fadeIn")) {
			mFlags |= PTFLAG_FADE_IN;
		}
		else if (!token.Icmp("useLightningAxis")) {
			mFlags |= 0x400000u;
		}
		else if (!token.Icmp("specular")) {
			mFlags |= 0x40000u;
		}
		else if (!token.Icmp("shadows")) {
			mFlags |= 0x20000u;
		}
		else if (!token.Icmp("blend")) {
			if (!ParseBlendParms(effect, src)) {
				return false;
			}
		}
		else if (!token.Icmp("entityDef")) {
			if (!src->ReadToken(&token)) {
				return false;
			}
			mEntityDefName = token;
		}
		else if (!token.Icmp("material")) {
			if (!src->ReadToken(&token)) {
				return false;
			}
			mMaterial = declManager->FindMaterial(token);
		}
		else if (!token.Icmp("trailScale")) {
			AllocTrail();
			mTrailInfo->mTrailScale = src->ParseFloat();
		}
		else if (!token.Icmp("trailCount")) {
			AllocTrail();
			mTrailInfo->mTrailCount.x = src->ParseFloat();
			src->ExpectTokenString(",");
			mTrailInfo->mTrailCount.y = src->ParseFloat();
		}
		else if (!token.Icmp("trailRepeat")) {
			AllocTrail();
			mTrailRepeat = src->ParseInt();
		}
		else if (!token.Icmp("trailTime")) {
			AllocTrail();
			mTrailInfo->mTrailTime.x = src->ParseFloat();
			src->ExpectTokenString(",");
			mTrailInfo->mTrailTime.y = src->ParseFloat();
		}
		else if (!token.Icmp("trailMaterial")) {
			AllocTrail();
			if (!src->ReadToken(&token)) {
				return false;
			}
			mTrailInfo->mTrailMaterial = declManager->FindMaterial(token);
		}
		else if (!token.Icmp("trailType")) {
			AllocTrail();
			if (!src->ReadToken(&token)) {
				return false;
			}

			if (!token.Icmp("burn")) {
				mTrailInfo->mTrailType = 1;
				mTrailInfo->mTrailTypeName = "";
			}
			else if (!token.Icmp("motion")) {
				mTrailInfo->mTrailType = 2;
				mTrailInfo->mTrailTypeName = "";
			}
			else {
				mTrailInfo->mTrailType = 3;
				mTrailInfo->mTrailTypeName = token;
			}
		}
		else if (!token.Icmp("fork")) {
			AllocElectricityInfo();
			int forks = src->ParseInt();
			if (forks < 0) {
				forks = 0;
			} else if (forks > BSE_MAX_FORKS) {
				forks = BSE_MAX_FORKS;
			}
			mElecInfo->mNumForks = forks;
		}
		else if (!token.Icmp("forkMins")) {
			AllocElectricityInfo();
			src->Parse1DMatrix(3, mElecInfo->mForkSizeMins.ToFloatPtr(), true);
		}
		else if (!token.Icmp("forkMaxs")) {
			AllocElectricityInfo();
			src->Parse1DMatrix(3, mElecInfo->mForkSizeMaxs.ToFloatPtr(), true);
		}
		else if (!token.Icmp("jitterSize")) {
			AllocElectricityInfo();
			src->Parse1DMatrix(3, mElecInfo->mJitterSize.ToFloatPtr(), true);
		}
		else if (!token.Icmp("jitterRate")) {
			AllocElectricityInfo();
			mElecInfo->mJitterRate = src->ParseFloat();
		}
		else if (!token.Icmp("jitterTable")) {
			AllocElectricityInfo();
			if (!src->ReadToken(&token)) {
				return false;
			}
			const idDeclTable* table = declManager->FindTable(token, false);
			if (table != NULL && !table->IsImplicit()) {
				mElecInfo->mJitterTableName = token;
				mElecInfo->mJitterTable = table;
			}
			else {
				mElecInfo->mJitterTableName = "";
			}
		}
		else if (!token.Icmp("gravity")) {
			mGravity.x = src->ParseFloat();
			src->ExpectTokenString(",");
			mGravity.y = src->ParseFloat();
		}
		else if (!token.Icmp("duration")) {
			float srcb = src->ParseFloat();
			float v8 = 0.0020000001;
			if (srcb >= 0.0020000001)
			{
				v8 = srcb;
				if (srcb > 300.0)
					v8 = 300.0;
			}
			float srcg = v8;
			mDuration.x = srcg;
			src->ExpectTokenString(",");

			float srcc = src->ParseFloat();
			float v9 = 0.0020000001;
			if (srcc < 0.0020000001 || (v9 = srcc, srcc <= 300.0))
			{
				float srch = v9;
				mDuration.y = srch;
			}
			else
			{
				mDuration.y = 300.0;
			}
		}
		else if (!token.Icmp("parentvelocity")) {
			mFlags |= 0x2000000u;
		}
		else if (!token.Icmp("tiling")) {
			mFlags |= 0x100000u;
			float srca = src->ParseFloat();
			float v7 = 0.0020000001;
			if (srca < 0.0020000001 || (v7 = srca, srca <= 1024.0))
			{
				float srcf = v7;
				mTiling = srcf;
			}
			else
			{
				mTiling = 1024.0;
			}
		}
		else if (!token.Icmp("persist")) {
			mFlags |= 0x200000u;
		}
		else if (!token.Icmp("generatedLine")) {
			mFlags |= 0x10000u;
		}
		else if (!token.Icmp("flipNormal")) {
			mFlags |= 0x2000u;
		}
		else if (!token.Icmp("lineHit")) {
			mFlags |= 0x4000000u;
		}
		else if (!token.Icmp("generatedOriginNormal")) {
			mFlags |= 0x1000u;
		}
		else if (!token.Icmp("generatedNormal")) {
			mFlags |= 0x800u;
		}
		else if (!token.Icmp("motion")) {
			if (!ParseMotionDomains(effect, src)) {
				return false;
			}
		}
		else if (!token.Icmp("end")) {
			if (!ParseDeathDomains(effect, src)) {
				return false;
			}
		}
		else if (!token.Icmp("start")) {
			if (!ParseSpawnDomains(effect, src)) {
				return false;
			}
		}
		else {
			BSE_ParticleWarning(src, effect, "Invalid particle keyword '%s'", token.c_str());
		}
	}
	return false;
}

void rvParticleTemplate::Duplicate(rvParticleTemplate const& copy) {
	if (this == &copy) {
		return;
	}

	Purge();
	PurgeTraceModel();
	*this = copy;

	if (copy.mTrailInfo != NULL && !copy.mTrailInfo->mStatic) {
		mTrailInfo = new rvTrailInfo(*copy.mTrailInfo);
		mTrailInfo->mStatic = 0;
	}

	if (copy.mElecInfo != NULL && !copy.mElecInfo->mStatic) {
		mElecInfo = new rvElectricityInfo(*copy.mElecInfo);
		mElecInfo->mStatic = 0;
	}

	mpSpawnPosition = BSE_DuplicateParm(copy.mpSpawnPosition);
	mpSpawnDirection = BSE_DuplicateParm(copy.mpSpawnDirection);
	mpSpawnVelocity = BSE_DuplicateParm(copy.mpSpawnVelocity);
	mpSpawnAcceleration = BSE_DuplicateParm(copy.mpSpawnAcceleration);
	mpSpawnFriction = BSE_DuplicateParm(copy.mpSpawnFriction);
	mpSpawnTint = BSE_DuplicateParm(copy.mpSpawnTint);
	mpSpawnFade = BSE_DuplicateParm(copy.mpSpawnFade);
	mpSpawnSize = BSE_DuplicateParm(copy.mpSpawnSize);
	mpSpawnRotate = BSE_DuplicateParm(copy.mpSpawnRotate);
	mpSpawnAngle = BSE_DuplicateParm(copy.mpSpawnAngle);
	mpSpawnOffset = BSE_DuplicateParm(copy.mpSpawnOffset);
	mpSpawnLength = BSE_DuplicateParm(copy.mpSpawnLength);
	mpSpawnWindStrength = BSE_DuplicateParm(copy.mpSpawnWindStrength);
	mpDeathTint = BSE_DuplicateParm(copy.mpDeathTint);
	mpDeathFade = BSE_DuplicateParm(copy.mpDeathFade);
	mpDeathSize = BSE_DuplicateParm(copy.mpDeathSize);
	mpDeathRotate = BSE_DuplicateParm(copy.mpDeathRotate);
	mpDeathAngle = BSE_DuplicateParm(copy.mpDeathAngle);
	mpDeathOffset = BSE_DuplicateParm(copy.mpDeathOffset);
	mpDeathLength = BSE_DuplicateParm(copy.mpDeathLength);

	mpTintEnvelope = BSE_DuplicateEnv(copy.mpTintEnvelope);
	mpFadeEnvelope = BSE_DuplicateEnv(copy.mpFadeEnvelope);
	mpSizeEnvelope = BSE_DuplicateEnv(copy.mpSizeEnvelope);
	mpRotateEnvelope = BSE_DuplicateEnv(copy.mpRotateEnvelope);
	mpAngleEnvelope = BSE_DuplicateEnv(copy.mpAngleEnvelope);
	mpOffsetEnvelope = BSE_DuplicateEnv(copy.mpOffsetEnvelope);
	mpLengthEnvelope = BSE_DuplicateEnv(copy.mpLengthEnvelope);

	if (mpSpawnPosition == NULL) { mpSpawnPosition = &rvParticleTemplate::sSPF_NONE_3; }
	if (mpSpawnDirection == NULL) { mpSpawnDirection = &rvParticleTemplate::sSPF_NONE_3; }
	if (mpSpawnVelocity == NULL) { mpSpawnVelocity = &rvParticleTemplate::sSPF_NONE_3; }
	if (mpSpawnAcceleration == NULL) { mpSpawnAcceleration = &rvParticleTemplate::sSPF_NONE_3; }
	if (mpSpawnFriction == NULL) { mpSpawnFriction = &rvParticleTemplate::sSPF_NONE_3; }
	if (mpSpawnTint == NULL) { mpSpawnTint = &rvParticleTemplate::sSPF_ONE_3; }
	if (mpSpawnFade == NULL) { mpSpawnFade = &rvParticleTemplate::sSPF_ONE_1; }
	if (mpSpawnSize == NULL) { mpSpawnSize = &rvParticleTemplate::sSPF_ONE_3; }
	if (mpSpawnRotate == NULL) { mpSpawnRotate = &rvParticleTemplate::sSPF_NONE_3; }
	if (mpSpawnAngle == NULL) { mpSpawnAngle = &rvParticleTemplate::sSPF_NONE_3; }
	if (mpSpawnOffset == NULL) { mpSpawnOffset = &rvParticleTemplate::sSPF_NONE_3; }
	if (mpSpawnLength == NULL) { mpSpawnLength = &rvParticleTemplate::sSPF_NONE_3; }
	if (mpSpawnWindStrength == NULL) { mpSpawnWindStrength = &rvParticleTemplate::sSPF_NONE_1; }
	if (mpDeathTint == NULL) { mpDeathTint = &rvParticleTemplate::sSPF_NONE_3; }
	if (mpDeathFade == NULL) { mpDeathFade = &rvParticleTemplate::sSPF_NONE_1; }
	if (mpDeathSize == NULL) { mpDeathSize = &rvParticleTemplate::sSPF_ONE_3; }
	if (mpDeathRotate == NULL) { mpDeathRotate = &rvParticleTemplate::sSPF_NONE_3; }
	if (mpDeathAngle == NULL) { mpDeathAngle = &rvParticleTemplate::sSPF_NONE_3; }
	if (mpDeathOffset == NULL) { mpDeathOffset = &rvParticleTemplate::sSPF_NONE_3; }
	if (mpDeathLength == NULL) { mpDeathLength = &rvParticleTemplate::sSPF_NONE_3; }

	if (mpTintEnvelope == NULL) { mpTintEnvelope = &rvParticleTemplate::sEmptyEnvelope; }
	if (mpFadeEnvelope == NULL) { mpFadeEnvelope = &rvParticleTemplate::sEmptyEnvelope; }
	if (mpSizeEnvelope == NULL) { mpSizeEnvelope = &rvParticleTemplate::sEmptyEnvelope; }
	if (mpRotateEnvelope == NULL) { mpRotateEnvelope = &rvParticleTemplate::sEmptyEnvelope; }
	if (mpAngleEnvelope == NULL) { mpAngleEnvelope = &rvParticleTemplate::sEmptyEnvelope; }
	if (mpOffsetEnvelope == NULL) { mpOffsetEnvelope = &rvParticleTemplate::sEmptyEnvelope; }
	if (mpLengthEnvelope == NULL) { mpLengthEnvelope = &rvParticleTemplate::sEmptyEnvelope; }
}

void  rvParticleTemplate::Finish()
{
	double v2; // st7
	rvParticleTemplate* v3; // esi
	rvTrailInfo* v4; // eax
	float* v5; // eax
	const modelSurface_t* v6; // eax
	const modelSurface_t* v7; // ebp
	idTraceModel* v8; // eax
	idTraceModel* v9; // edi
	idBounds* v10; // ebp
	rvTrailInfo* v11; // ecx
	rvElectricityInfo* v12; // eax
	float v13; // ST10_4
	float v14; // ST10_4
	rvTrailInfo* v15; // ecx
	float v16; // ST10_4
	rvTrailInfo* v17; // ecx
	double v18; // st6
	float* v19; // ecx
	float v20; // ST10_4
	float* v21; // eax
	float v22; // ST14_4
	float v23; // ST18_4
	float v24; // ST1C_4
	float v25; // ST20_4
	float v26; // ST24_4
	float v27; // ST28_4
	signed int retaddr; // [esp+2Ch] [ebp+0h]

	v2 = 0.0;
	v3 = this;
	v3->mFlags |= 0x100u;
	v4 = this->mTrailInfo;
	if ((!v4->mTrailType || v4->mTrailType == 3) && !v4->mStatic)
	{
		v4->mTrailTime.y = 0.0;
		v4->mTrailTime.x = 0.0;
		v5 = &this->mTrailInfo->mTrailCount.x;
		v5[1] = 0.0;
		*v5 = 0.0;
	}
	switch (this->mType)
	{
	case 1:
	case 2:
		v11 = this->mTrailInfo;
		v3->mVertexCount = 4;
		v3->mIndexCount = 6;
		if (0.0 != v11->mTrailCount.y && v11->mTrailType == 1)
		{
			v3->mVertexCount *= (unsigned short)v3->GetMaxTrailCount();
			v2 = 0.0;
			v3->mIndexCount *= (unsigned short)v3->GetMaxTrailCount();
		}
		break;
	case 4:
	case 6:
	case 8:
	case 9:
		this->mVertexCount = 4;
		this->mIndexCount = 6;
		break;
	case 5:
	{
		idBounds modelBounds;
		bool hasValidBounds = false;

		v6 = ( this->mModel != NULL ) ? this->mModel->Surface( 0 ) : NULL;
		v7 = v6;
		if ( v6 != NULL && v6->geometry != NULL ) {
			v3->mVertexCount = v6->geometry->numVerts;
			v3->mIndexCount = v6->geometry->numIndexes;
			v3->mMaterial = v6->shader;
			modelBounds = v6->geometry->bounds;
			hasValidBounds = !modelBounds.IsCleared();
		} else if ( this->mModel != NULL ) {
			modelBounds = this->mModel->Bounds();
			hasValidBounds = !modelBounds.IsCleared();
		}

		// Corrupt/missing model bounds can come from partially loaded surfaces.
		// Fall back to a small cube so trace model creation remains safe.
		if ( !hasValidBounds ||
			idMath::Fabs( modelBounds[0].x ) > 1000000.0f || idMath::Fabs( modelBounds[0].y ) > 1000000.0f || idMath::Fabs( modelBounds[0].z ) > 1000000.0f ||
			idMath::Fabs( modelBounds[1].x ) > 1000000.0f || idMath::Fabs( modelBounds[1].y ) > 1000000.0f || idMath::Fabs( modelBounds[1].z ) > 1000000.0f ) {
			modelBounds[0].Set( -8.0f, -8.0f, -8.0f );
			modelBounds[1].Set( 8.0f, 8.0f, 8.0f );
		}

		v3->PurgeTraceModel();
		v8 = new idTraceModel();
		v9 = v8;
		retaddr = 0;
		if ( v8 ) {
			v8->InitBox();
			v9->SetupBox( modelBounds );
		}
		retaddr = -1;
		v2 = 0.0;
		v3->mTraceModelIndex = bse->AddTraceModel( v8 );
	}
		break;
	case 7:
		v12 = this->mElecInfo;
		this->mVertexCount = 20 * ((unsigned short)v12->mNumForks + 1);
		this->mIndexCount = 60 * ((unsigned short)v12->mNumForks + 1);
		break;
	case 0xA:
		this->mVertexCount = 0;
		this->mIndexCount = 0;
		break;
	default:
		break;
	}
	if (v3->mDuration.y <= (double)v3->mDuration.x)
	{
		v13 = v3->mDuration.x;
		v3->mDuration.x = v3->mDuration.y;
		v3->mDuration.y = v13;
	}
	if (v3->mGravity.y <= (double)v3->mGravity.x)
	{
		v14 = v3->mGravity.x;
		v3->mGravity.x = v3->mGravity.y;
		v3->mGravity.y = v14;
	}
	v15 = v3->mTrailInfo;
	if (!v15->mStatic)
	{
		if (v15->mTrailTime.y <= (double)v15->mTrailTime.x)
		{
			v16 = v15->mTrailTime.x;
			v15->mTrailTime.x = v15->mTrailTime.y;
			v15->mTrailTime.y = v16;
		}
		v17 = v3->mTrailInfo;
		v18 = v17->mTrailCount.x;
		v19 = &v17->mTrailCount.x;
		if (v19[1] <= v18)
		{
			v20 = *v19;
			*v19 = v19[1];
			v19[1] = v20;
		}
	}
	v3->mCentre.z = v2;
	v3->mCentre.y = v2;
	v3->mCentre.x = v2;
	if (!(((unsigned int)v3->mFlags >> 12) & 1))
	{
		v21 = (float*)&v3->mpSpawnPosition->mSpawnType;
		switch (*reinterpret_cast<unsigned char*>(v21))
		{
		case 0xBu:
			v3->mCentre.x = v21[3];
			v3->mCentre.y = v21[4];
			v3->mCentre.z = v21[5];
			break;
		case 0xFu:
		case 0x13u:
		case 0x17u:
		case 0x1Bu:
		case 0x1Fu:
		case 0x23u:
		case 0x27u:
		case 0x2Bu:
		case 0x2Fu:
			v22 = v21[6] + v21[3];
			v23 = v21[7] + v21[4];
			v24 = v21[8] + v21[5];
			v25 = v22 * 0.5;
			v26 = v23 * 0.5;
			v27 = 0.5 * v24;
			v3->mCentre.x = v25;
			v3->mCentre.y = v26;
			v3->mCentre.z = v27;
			break;
		default:
			return;
		}
	}
}

void rvParticleTemplate::InitStatic()
{
	//sdDeclTypeHolder* v0; // eax
	int v1; // eax
	//sdDeclTypeHolder* v2; // eax

	if (!rvParticleTemplate::sInited)
	{
		rvParticleTemplate::sInited = 1;
		rvParticleTemplate::sTrailInfo.mTrailType = 0;
		rvParticleTemplate::sTrailInfo.mStatic = 1;
		rvParticleTemplate::sTrailInfo.mPad = 0;
		rvParticleTemplate::sTrailInfo.mTrailTypeName = "";
		// Match stock Q4 behavior: motion trails default to the motionblur
		// material when trailMaterial isn't authored.
		const idMaterial* defaultTrailMaterial = declManager->FindMaterial("gfx/effects/particles_shapes/motionblur", false);
		if (defaultTrailMaterial == NULL) {
			defaultTrailMaterial = declManager->FindMaterial("_default");
		}
		rvParticleTemplate::sTrailInfo.mTrailMaterial = defaultTrailMaterial;
		rvParticleTemplate::sTrailInfo.mTrailTime.x = 0.0f;
		rvParticleTemplate::sTrailInfo.mTrailTime.y = 0.0f;
		rvParticleTemplate::sTrailInfo.mTrailCount.x = 0.0f;
		rvParticleTemplate::sTrailInfo.mTrailCount.y = 0.0f;
		rvParticleTemplate::sTrailInfo.mTrailScale = 0.0f;
		//unk_7E672A = 1;
		//v0 = sdSingleton<sdDeclTypeHolder>::GetInstance();
		//v1 = ((int(__stdcall*)(int, const char*, signed int))declManager->vfptr->FindType)(
		//	v0->declMaterialType.declTypeHandle,
		//	"_default",
		//	1);
		//unk_7E6754 = 0.0;
		//unk_7E674C = v1;
		//unk_7E6750 = 0.0;
		rvParticleTemplate::sElectricityInfo.mNumForks = 0;
		//unk_7E675C = 0.0;
		rvParticleTemplate::sElectricityInfo.mStatic = 1;
		//unk_7E6758 = 0.0;
		//unk_7E6760 = 0.5;
		rvParticleTemplate::sElectricityInfo.mForkSizeMins.x = -20.0;
		rvParticleTemplate::sElectricityInfo.mForkSizeMins.y = -20.0;
		rvParticleTemplate::sElectricityInfo.mForkSizeMins.z = -20.0;
		rvParticleTemplate::sElectricityInfo.mForkSizeMaxs.x = 20.0;
		rvParticleTemplate::sElectricityInfo.mForkSizeMaxs.y = 20.0;
		rvParticleTemplate::sElectricityInfo.mForkSizeMaxs.z = 20.0;
		rvParticleTemplate::sElectricityInfo.mJitterSize.x = 3.0;
		rvParticleTemplate::sElectricityInfo.mJitterSize.y = 7.0;
		rvParticleTemplate::sElectricityInfo.mJitterSize.z = 7.0;
		rvParticleTemplate::sElectricityInfo.mJitterRate = 0.0;
		rvParticleTemplate::sElectricityInfo.mJitterTableName = "halfsintable";
		//v2 = sdSingleton<sdDeclTypeHolder>::GetInstance();
		rvParticleTemplate::sElectricityInfo.mJitterTable = declManager->FindTable("halfsintable", false);
		rvParticleTemplate::sDefaultEnvelope.Init(); //rvEnvParms::Init(&rvParticleTemplate::sDefaultEnvelope);		
		rvParticleTemplate::sDefaultEnvelope.SetDefaultType(); // rvEnvParms::SetDefaultType(&rvParticleTemplate::sDefaultEnvelope);
		rvParticleTemplate::sDefaultEnvelope.mStatic = 1;
		rvParticleTemplate::sEmptyEnvelope.Init(); // rvEnvParms::Init(&rvParticleTemplate::sEmptyEnvelope);
		rvParticleTemplate::sSPF_ONE_1.mRange = 0.0;
		rvParticleTemplate::sEmptyEnvelope.mStatic = 1;
		rvParticleTemplate::sSPF_ONE_1.mMins.z = 0.0;
		rvParticleTemplate::sSPF_ONE_1.mSpawnType = 5;
		rvParticleTemplate::sSPF_ONE_1.mMins.y = 0.0;
		rvParticleTemplate::sSPF_ONE_1.mFlags = 0;
		rvParticleTemplate::sSPF_ONE_1.mMins.x = 0.0;
		rvParticleTemplate::sSPF_ONE_1.mModelInfo = 0;
		rvParticleTemplate::sSPF_ONE_1.mMaxs.z = 0.0;
		rvParticleTemplate::sSPF_ONE_1.mStatic = 1;
		rvParticleTemplate::sSPF_ONE_1.mMaxs.y = 0.0;
		rvParticleTemplate::sSPF_ONE_2.mSpawnType = 6;
		rvParticleTemplate::sSPF_ONE_1.mMaxs.x = 0.0;
		rvParticleTemplate::sSPF_ONE_2.mFlags = 0;
		rvParticleTemplate::sSPF_ONE_2.mRange = 0.0;
		rvParticleTemplate::sSPF_ONE_2.mModelInfo = 0;
		rvParticleTemplate::sSPF_ONE_2.mMins.z = 0.0;
		rvParticleTemplate::sSPF_ONE_2.mStatic = 1;
		rvParticleTemplate::sSPF_ONE_2.mMins.y = 0.0;
		rvParticleTemplate::sSPF_ONE_3.mSpawnType = 7;
		rvParticleTemplate::sSPF_ONE_2.mMins.x = 0.0;
		rvParticleTemplate::sSPF_ONE_3.mFlags = 0;
		rvParticleTemplate::sSPF_ONE_2.mMaxs.z = 0.0;
		rvParticleTemplate::sSPF_ONE_2.mMaxs.y = 0.0;
		rvParticleTemplate::sSPF_ONE_2.mMaxs.x = 0.0;
		rvParticleTemplate::sSPF_ONE_3.mRange = 0.0;
		rvParticleTemplate::sSPF_ONE_3.mMins.z = 0.0;
		rvParticleTemplate::sSPF_ONE_3.mModelInfo = 0;
		rvParticleTemplate::sSPF_ONE_3.mMins.y = 0.0;
		rvParticleTemplate::sSPF_ONE_3.mStatic = 1;
		rvParticleTemplate::sSPF_ONE_3.mMins.x = 0.0;
		rvParticleTemplate::sSPF_NONE_0.mSpawnType = 0;
		rvParticleTemplate::sSPF_ONE_3.mMaxs.z = 0.0;
		rvParticleTemplate::sSPF_NONE_0.mFlags = 0;
		rvParticleTemplate::sSPF_ONE_3.mMaxs.y = 0.0;
		rvParticleTemplate::sSPF_NONE_0.mModelInfo = 0;
		rvParticleTemplate::sSPF_ONE_3.mMaxs.x = 0.0;
		rvParticleTemplate::sSPF_NONE_0.mStatic = 1;
		rvParticleTemplate::sSPF_NONE_0.mRange = 0.0;
		rvParticleTemplate::sSPF_NONE_1.mSpawnType = 1;
		rvParticleTemplate::sSPF_NONE_0.mMins.z = 0.0;
		rvParticleTemplate::sSPF_NONE_1.mFlags = 0;
		rvParticleTemplate::sSPF_NONE_0.mMins.y = 0.0;
		rvParticleTemplate::sSPF_NONE_1.mModelInfo = 0;
		rvParticleTemplate::sSPF_NONE_0.mMins.x = 0.0;
		rvParticleTemplate::sSPF_NONE_1.mStatic = 1;
		rvParticleTemplate::sSPF_NONE_0.mMaxs.z = 0.0;
		rvParticleTemplate::sSPF_NONE_3.mSpawnType = 3;
		rvParticleTemplate::sSPF_NONE_0.mMaxs.y = 0.0;
		rvParticleTemplate::sSPF_NONE_3.mFlags = 0;
		rvParticleTemplate::sSPF_NONE_0.mMaxs.x = 0.0;
		rvParticleTemplate::sSPF_NONE_3.mModelInfo = 0;
		rvParticleTemplate::sSPF_NONE_1.mRange = 0.0;
		rvParticleTemplate::sSPF_NONE_3.mStatic = 1;
		rvParticleTemplate::sSPF_NONE_1.mMins.z = 0.0;
		rvParticleTemplate::sSPF_NONE_1.mMins.y = 0.0;
		rvParticleTemplate::sSPF_NONE_1.mMins.x = 0.0;
		rvParticleTemplate::sSPF_NONE_1.mMaxs.z = 0.0;
		rvParticleTemplate::sSPF_NONE_1.mMaxs.y = 0.0;
		rvParticleTemplate::sSPF_NONE_1.mMaxs.x = 0.0;
		rvParticleTemplate::sSPF_NONE_3.mRange = 0.0;
		rvParticleTemplate::sSPF_NONE_3.mMins.z = 0.0;
		rvParticleTemplate::sSPF_NONE_3.mMins.y = 0.0;
		rvParticleTemplate::sSPF_NONE_3.mMins.x = 0.0;
		rvParticleTemplate::sSPF_NONE_3.mMaxs.z = 0.0;
		rvParticleTemplate::sSPF_NONE_3.mMaxs.y = 0.0;
		rvParticleTemplate::sSPF_NONE_3.mMaxs.x = 0.0;
	}
}

void rvParticleTemplate::Init(void)
{
	rvParticleTemplate* v1; // esi
	//sdDeclTypeHolder* v2; // eax
	rvDeclEffect** v3; // eax
	signed int v4; // ecx

	v1 = this;
	rvParticleTemplate::InitStatic();
	v1->mFlags = 0;
	v1->mType = 0;
	//v2 = sdSingleton<sdDeclTypeHolder>::GetInstance();
	v1->SetMaterial((idMaterial *)declManager->FindMaterial("_default"));
	v1->mModel = renderModelManager->FindModel("_default");
	//v1->mMaterial = (idMaterial*)((int(__stdcall*)(int, const char*, signed int))declManager->vfptr->FindType)(
	//	v2->declMaterialType.declTypeHandle,
	//	"_default",
	//	1);
	//v1->mModel = (idRenderModel*)((int(__stdcall*)(const char*))renderModelManager->vfptr->FindModel)("_default");
	v1->mTraceModelIndex = -1;
	v1->mGravity.y = 0.0;
	v1->mGravity.x = 0.0;
	v1->mTiling = 8.0;
	v1->mPhysicsDistance = 0.0;
	v1->mBounce = 0.0;
	v1->mDuration.x = 0.0020000001;
	v1->mDuration.y = 0.0020000001;
	v1->mCentre.z = 0.0;
	v1->mCentre.y = 0.0;
	v1->mCentre.x = 0.0;
	v1->mFlags |= 0x4000000u;
	v1->mpSpawnPosition = &rvParticleTemplate::sSPF_NONE_3;
	v1->mWindDeviationAngle = 0.0;
	v1->mpSpawnDirection = &rvParticleTemplate::sSPF_NONE_3;
	v1->mpSpawnVelocity = &rvParticleTemplate::sSPF_NONE_3;
	v1->mpSpawnAcceleration = &rvParticleTemplate::sSPF_NONE_3;
	v1->mpSpawnFriction = &rvParticleTemplate::sSPF_NONE_3;
	v1->mpSpawnRotate = &rvParticleTemplate::sSPF_NONE_3;
	v1->mpSpawnAngle = &rvParticleTemplate::sSPF_NONE_3;
	v1->mpSpawnOffset = &rvParticleTemplate::sSPF_NONE_3;
	v1->mpSpawnLength = &rvParticleTemplate::sSPF_NONE_3;
	v1->mpDeathTint = &rvParticleTemplate::sSPF_NONE_3;
	v1->mpDeathRotate = &rvParticleTemplate::sSPF_NONE_3;
	v1->mpDeathAngle = &rvParticleTemplate::sSPF_NONE_3;
	v1->mpDeathOffset = &rvParticleTemplate::sSPF_NONE_3;
	v1->mpDeathLength = &rvParticleTemplate::sSPF_NONE_3;
	v1->mNumSizeParms = 2;
	v1->mNumRotateParms = 1;
	v1->mVertexCount = 4;
	v1->mIndexCount = 6;
	v1->mTrailInfo = &rvParticleTemplate::sTrailInfo;
	v1->mElecInfo = &rvParticleTemplate::sElectricityInfo;
	v1->mpSpawnTint = &rvParticleTemplate::sSPF_ONE_3;
	v1->mpSpawnFade = &rvParticleTemplate::sSPF_ONE_1;
	v1->mpSpawnSize = &rvParticleTemplate::sSPF_ONE_3;
	v1->mpSpawnWindStrength = &rvParticleTemplate::sSPF_NONE_1;
	v1->mpTintEnvelope = &rvParticleTemplate::sEmptyEnvelope;
	v1->mpFadeEnvelope = &rvParticleTemplate::sEmptyEnvelope;
	v1->mpSizeEnvelope = &rvParticleTemplate::sEmptyEnvelope;
	v1->mpRotateEnvelope = &rvParticleTemplate::sEmptyEnvelope;
	v1->mpAngleEnvelope = &rvParticleTemplate::sEmptyEnvelope;
	v1->mpOffsetEnvelope = &rvParticleTemplate::sEmptyEnvelope;
	v1->mpLengthEnvelope = &rvParticleTemplate::sEmptyEnvelope;
	v1->mpDeathFade = &rvParticleTemplate::sSPF_NONE_1;
	v1->mpDeathSize = &rvParticleTemplate::sSPF_ONE_3;
	v1->mTrailRepeat = 1;
	v1->mNumFrames = 0;
	v1->mNumImpactEffects = 0;
	v1->mNumTimeoutEffects = 0;
	v1->mImpactEffects[0] = NULL;
	v1->mImpactEffects[1] = NULL;
	v1->mImpactEffects[2] = NULL;
	v1->mImpactEffects[3] = NULL;
	v1->mTimeoutEffects[0] = NULL;
	v1->mTimeoutEffects[1] = NULL;
	v1->mTimeoutEffects[2] = NULL;
	v1->mTimeoutEffects[3] = NULL;
	// jmarshall: no idea
	//v3 = &v1->mTimeoutEffects[0];
	// jmarshall end
	v4 = 4;

	// jmarshall: no idea
	//do
	//{
	//	*(v3 - 5) = 0;
	//	*v3 = 0;
	//	++v3;
	//	--v4;
	//} while (v4);
	// jmarshall end
	v1->mFlags |= 0x8000000u;
}

void rvParticleTemplate::SetParameterCounts()
{
	rvParticleParms* v1; // eax
	rvParticleParms* v2; // edx

	switch (this->mType)
	{
	case 1:
	case 4:
		this->mNumSizeParms = 2;
		this->mNumRotateParms = 1;
		v1 = &rvParticleTemplate::sSPF_ONE_2;
		v2 = &rvParticleTemplate::sSPF_NONE_1;
		goto LABEL_11;
	case 2:
	case 7:
	case 8:
	case 9:
		this->mNumSizeParms = 1;
		this->mNumRotateParms = 0;
		v1 = &rvParticleTemplate::sSPF_ONE_1;
		v2 = &rvParticleTemplate::sSPF_NONE_0;
		goto LABEL_11;
	case 3:
		this->mNumSizeParms = 2;
		this->mNumRotateParms = 3;
		v1 = &rvParticleTemplate::sSPF_ONE_2;
		v2 = &rvParticleTemplate::sSPF_NONE_3;
		goto LABEL_11;
	case 5:
		this->mNumSizeParms = 3;
		this->mNumRotateParms = 3;
		goto LABEL_9;
	case 6:
		this->mNumSizeParms = 3;
		this->mNumRotateParms = 0;
		v2 = &rvParticleTemplate::sSPF_NONE_0;
		goto LABEL_10;
	case 0xA:
		this->mNumSizeParms = 0;
		this->mNumRotateParms = 3;
	LABEL_9:
		v2 = &rvParticleTemplate::sSPF_NONE_3;
	LABEL_10:
		v1 = &rvParticleTemplate::sSPF_ONE_3;
	LABEL_11:
		this->mpSpawnSize = v1;
		this->mpSpawnRotate = v2;
		this->mpDeathSize = v1;
		this->mpDeathRotate = v2;
		break;
	default:
		return;
	}
}

void rvParticleTemplate::PurgeTraceModel(void) {
	rvParticleTemplate* v1; // esi
	int v2; // eax

	v1 = this;
	v2 = this->mTraceModelIndex;
	if (v2 != -1)
	{
		bse->FreeTraceModel(v2);
		v1->mTraceModelIndex = -1;
	}
}

void rvParticleTemplate::Purge() {
	PurgeTraceModel();

	rvParticleParms* seenParms[20];
	int seenParmCount = 0;
	BSE_DeleteOwnedParm(mpSpawnPosition, seenParms, seenParmCount);
	BSE_DeleteOwnedParm(mpSpawnDirection, seenParms, seenParmCount);
	BSE_DeleteOwnedParm(mpSpawnVelocity, seenParms, seenParmCount);
	BSE_DeleteOwnedParm(mpSpawnAcceleration, seenParms, seenParmCount);
	BSE_DeleteOwnedParm(mpSpawnFriction, seenParms, seenParmCount);
	BSE_DeleteOwnedParm(mpSpawnTint, seenParms, seenParmCount);
	BSE_DeleteOwnedParm(mpSpawnFade, seenParms, seenParmCount);
	BSE_DeleteOwnedParm(mpSpawnSize, seenParms, seenParmCount);
	BSE_DeleteOwnedParm(mpSpawnRotate, seenParms, seenParmCount);
	BSE_DeleteOwnedParm(mpSpawnAngle, seenParms, seenParmCount);
	BSE_DeleteOwnedParm(mpSpawnOffset, seenParms, seenParmCount);
	BSE_DeleteOwnedParm(mpSpawnLength, seenParms, seenParmCount);
	BSE_DeleteOwnedParm(mpSpawnWindStrength, seenParms, seenParmCount);
	BSE_DeleteOwnedParm(mpDeathTint, seenParms, seenParmCount);
	BSE_DeleteOwnedParm(mpDeathFade, seenParms, seenParmCount);
	BSE_DeleteOwnedParm(mpDeathSize, seenParms, seenParmCount);
	BSE_DeleteOwnedParm(mpDeathRotate, seenParms, seenParmCount);
	BSE_DeleteOwnedParm(mpDeathAngle, seenParms, seenParmCount);
	BSE_DeleteOwnedParm(mpDeathOffset, seenParms, seenParmCount);
	BSE_DeleteOwnedParm(mpDeathLength, seenParms, seenParmCount);

	rvEnvParms* seenEnvs[7];
	int seenEnvCount = 0;
	BSE_DeleteOwnedEnv(mpTintEnvelope, seenEnvs, seenEnvCount);
	BSE_DeleteOwnedEnv(mpFadeEnvelope, seenEnvs, seenEnvCount);
	BSE_DeleteOwnedEnv(mpSizeEnvelope, seenEnvs, seenEnvCount);
	BSE_DeleteOwnedEnv(mpRotateEnvelope, seenEnvs, seenEnvCount);
	BSE_DeleteOwnedEnv(mpAngleEnvelope, seenEnvs, seenEnvCount);
	BSE_DeleteOwnedEnv(mpOffsetEnvelope, seenEnvs, seenEnvCount);
	BSE_DeleteOwnedEnv(mpLengthEnvelope, seenEnvs, seenEnvCount);

	if (mTrailInfo != NULL && !mTrailInfo->mStatic) {
		delete mTrailInfo;
	}
	if (mElecInfo != NULL && !mElecInfo->mStatic) {
		delete mElecInfo;
	}

	mTrailInfo = &rvParticleTemplate::sTrailInfo;
	mElecInfo = &rvParticleTemplate::sElectricityInfo;

	mpSpawnPosition = &rvParticleTemplate::sSPF_NONE_3;
	mpSpawnDirection = &rvParticleTemplate::sSPF_NONE_3;
	mpSpawnVelocity = &rvParticleTemplate::sSPF_NONE_3;
	mpSpawnAcceleration = &rvParticleTemplate::sSPF_NONE_3;
	mpSpawnFriction = &rvParticleTemplate::sSPF_NONE_3;
	mpSpawnRotate = &rvParticleTemplate::sSPF_NONE_3;
	mpSpawnAngle = &rvParticleTemplate::sSPF_NONE_3;
	mpSpawnOffset = &rvParticleTemplate::sSPF_NONE_3;
	mpSpawnLength = &rvParticleTemplate::sSPF_NONE_3;
	mpSpawnTint = &rvParticleTemplate::sSPF_ONE_3;
	mpSpawnFade = &rvParticleTemplate::sSPF_ONE_1;
	mpSpawnSize = &rvParticleTemplate::sSPF_ONE_3;
	mpSpawnWindStrength = &rvParticleTemplate::sSPF_NONE_1;

	mpTintEnvelope = &rvParticleTemplate::sEmptyEnvelope;
	mpFadeEnvelope = &rvParticleTemplate::sEmptyEnvelope;
	mpSizeEnvelope = &rvParticleTemplate::sEmptyEnvelope;
	mpRotateEnvelope = &rvParticleTemplate::sEmptyEnvelope;
	mpAngleEnvelope = &rvParticleTemplate::sEmptyEnvelope;
	mpOffsetEnvelope = &rvParticleTemplate::sEmptyEnvelope;
	mpLengthEnvelope = &rvParticleTemplate::sEmptyEnvelope;

	mpDeathTint = &rvParticleTemplate::sSPF_NONE_3;
	mpDeathFade = &rvParticleTemplate::sSPF_NONE_1;
	mpDeathSize = &rvParticleTemplate::sSPF_ONE_3;
	mpDeathRotate = &rvParticleTemplate::sSPF_NONE_3;
	mpDeathAngle = &rvParticleTemplate::sSPF_NONE_3;
	mpDeathOffset = &rvParticleTemplate::sSPF_NONE_3;
	mpDeathLength = &rvParticleTemplate::sSPF_NONE_3;

	mNumImpactEffects = 0;
	mNumTimeoutEffects = 0;
	mImpactEffects[0] = NULL;
	mImpactEffects[1] = NULL;
	mImpactEffects[2] = NULL;
	mImpactEffects[3] = NULL;
	mTimeoutEffects[0] = NULL;
	mTimeoutEffects[1] = NULL;
	mTimeoutEffects[2] = NULL;
	mTimeoutEffects[3] = NULL;
}

float rvParticleTemplate::CostTrail(float cost) const
{
	rvTrailInfo* v2; // eax
	double result; // st7
	float costa; // [esp+4h] [ebp+4h]
	float costb; // [esp+4h] [ebp+4h]

	v2 = this->mTrailInfo;
	switch (v2->mTrailType)
	{
	case 1:
		costa = v2->mTrailCount.y * (cost + cost);
		result = costa;
		break;
	case 2:
		costb = v2->mTrailCount.y * (cost * 1.5) + 20.0;
		result = costb;
		break;
	default:
		result = cost;
		break;
	}
	return result;
}

bool rvParticleTemplate::UsesEndOrigin(void) {
	bool result; // al

	if (this->mpSpawnPosition->mFlags & 2 || this->mpSpawnLength->mFlags & 2)
		result = 1;
	else
		result = ((unsigned int)this->mFlags >> 22) & 1;
	return result;
}

idTraceModel* rvParticleTemplate::GetTraceModel(void) const {
	if (mTraceModelIndex < 0) {
		return NULL;
	}
	return bse->GetTraceModel(mTraceModelIndex);
}

int rvParticleTemplate::GetTrailCount(void) const {
	const int count = idMath::FtoiFast(rvRandom::flrand(mTrailInfo->mTrailCount.x, mTrailInfo->mTrailCount.y));
	return Max(0, count);
}

bool rvParticleTemplate::Compare(const rvParticleTemplate& a) const {
	if (this == &a) {
		return true;
	}

	if (mFlags != a.mFlags ||
		mTraceModelIndex != a.mTraceModelIndex ||
		mType != a.mType ||
		mMaterial != a.mMaterial ||
		mModel != a.mModel ||
		mEntityDefName != a.mEntityDefName ||
		mGravity != a.mGravity ||
		mDuration != a.mDuration ||
		mCentre != a.mCentre ||
		mTiling != a.mTiling ||
		mBounce != a.mBounce ||
		mPhysicsDistance != a.mPhysicsDistance ||
		mWindDeviationAngle != a.mWindDeviationAngle ||
		mVertexCount != a.mVertexCount ||
		mIndexCount != a.mIndexCount ||
		mTrailRepeat != a.mTrailRepeat ||
		mNumSizeParms != a.mNumSizeParms ||
		mNumRotateParms != a.mNumRotateParms ||
		mNumFrames != a.mNumFrames ||
		mNumImpactEffects != a.mNumImpactEffects ||
		mNumTimeoutEffects != a.mNumTimeoutEffects) {
		return false;
	}

	if (mTrailInfo != a.mTrailInfo) {
		if (!mTrailInfo || !a.mTrailInfo) {
			return false;
		}
		if (mTrailInfo->mTrailType != a.mTrailInfo->mTrailType ||
			mTrailInfo->mTrailTypeName != a.mTrailInfo->mTrailTypeName ||
			mTrailInfo->mTrailMaterial != a.mTrailInfo->mTrailMaterial ||
			mTrailInfo->mTrailTime != a.mTrailInfo->mTrailTime ||
			mTrailInfo->mTrailCount != a.mTrailInfo->mTrailCount ||
			mTrailInfo->mTrailScale != a.mTrailInfo->mTrailScale) {
			return false;
		}
	}

	if (mElecInfo != a.mElecInfo) {
		if (!mElecInfo || !a.mElecInfo) {
			return false;
		}
		if (mElecInfo->mNumForks != a.mElecInfo->mNumForks ||
			mElecInfo->mForkSizeMins != a.mElecInfo->mForkSizeMins ||
			mElecInfo->mForkSizeMaxs != a.mElecInfo->mForkSizeMaxs ||
			mElecInfo->mJitterSize != a.mElecInfo->mJitterSize ||
			mElecInfo->mJitterRate != a.mElecInfo->mJitterRate ||
			mElecInfo->mJitterTableName != a.mElecInfo->mJitterTableName) {
			return false;
		}
	}

	auto parmEqual = [](const rvParticleParms* lhs, const rvParticleParms* rhs) {
		if (lhs == rhs) {
			return true;
		}
		if (!lhs || !rhs) {
			return false;
		}
		return (*lhs) == (*rhs);
	};

	auto envEqual = [](const rvEnvParms* lhs, const rvEnvParms* rhs) {
		if (lhs == rhs) {
			return true;
		}
		if (!lhs || !rhs) {
			return false;
		}
		return lhs->Compare(*rhs);
	};

	if (!parmEqual(mpSpawnPosition, a.mpSpawnPosition) ||
		!parmEqual(mpSpawnDirection, a.mpSpawnDirection) ||
		!parmEqual(mpSpawnVelocity, a.mpSpawnVelocity) ||
		!parmEqual(mpSpawnAcceleration, a.mpSpawnAcceleration) ||
		!parmEqual(mpSpawnFriction, a.mpSpawnFriction) ||
		!parmEqual(mpSpawnTint, a.mpSpawnTint) ||
		!parmEqual(mpSpawnFade, a.mpSpawnFade) ||
		!parmEqual(mpSpawnSize, a.mpSpawnSize) ||
		!parmEqual(mpSpawnRotate, a.mpSpawnRotate) ||
		!parmEqual(mpSpawnAngle, a.mpSpawnAngle) ||
		!parmEqual(mpSpawnOffset, a.mpSpawnOffset) ||
		!parmEqual(mpSpawnLength, a.mpSpawnLength) ||
		!parmEqual(mpSpawnWindStrength, a.mpSpawnWindStrength) ||
		!parmEqual(mpDeathTint, a.mpDeathTint) ||
		!parmEqual(mpDeathFade, a.mpDeathFade) ||
		!parmEqual(mpDeathSize, a.mpDeathSize) ||
		!parmEqual(mpDeathRotate, a.mpDeathRotate) ||
		!parmEqual(mpDeathAngle, a.mpDeathAngle) ||
		!parmEqual(mpDeathOffset, a.mpDeathOffset) ||
		!parmEqual(mpDeathLength, a.mpDeathLength) ||
		!envEqual(mpTintEnvelope, a.mpTintEnvelope) ||
		!envEqual(mpFadeEnvelope, a.mpFadeEnvelope) ||
		!envEqual(mpSizeEnvelope, a.mpSizeEnvelope) ||
		!envEqual(mpRotateEnvelope, a.mpRotateEnvelope) ||
		!envEqual(mpAngleEnvelope, a.mpAngleEnvelope) ||
		!envEqual(mpOffsetEnvelope, a.mpOffsetEnvelope) ||
		!envEqual(mpLengthEnvelope, a.mpLengthEnvelope)) {
		return false;
	}

	for (int i = 0; i < BSE_NUM_SPAWNABLE; ++i) {
		if (mImpactEffects[i] != a.mImpactEffects[i] ||
			mTimeoutEffects[i] != a.mTimeoutEffects[i]) {
			return false;
		}
	}

	return true;
}

void rvParticleTemplate::ShutdownStatic(void) {
	if (!sInited) {
		return;
	}

	sInited = false;
	sTrailInfo.mTrailTypeName = "";
	sTrailInfo.mTrailMaterial = NULL;
	sTrailInfo.mTrailTime.Zero();
	sTrailInfo.mTrailCount.Zero();
	sTrailInfo.mTrailScale = 0.0f;

	sElectricityInfo.mNumForks = 0;
	sElectricityInfo.mForkSizeMins.Zero();
	sElectricityInfo.mForkSizeMaxs.Zero();
	sElectricityInfo.mJitterSize.Zero();
	sElectricityInfo.mJitterRate = 0.0f;
	sElectricityInfo.mJitterTableName = "";
	sElectricityInfo.mJitterTable = NULL;

	sDefaultEnvelope.Init();
	sEmptyEnvelope.Init();
	sSPF_ONE_1 = rvParticleParms();
	sSPF_ONE_2 = rvParticleParms();
	sSPF_ONE_3 = rvParticleParms();
	sSPF_NONE_0 = rvParticleParms();
	sSPF_NONE_1 = rvParticleParms();
	sSPF_NONE_3 = rvParticleParms();
}
