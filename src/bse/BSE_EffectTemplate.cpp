// BSE_EffectTemplate.cpp
//



#include "BSE_Envelope.h"
#include "BSE_Particle.h"
#include "BSE.h"
#include "BSE_SpawnDomains.h"

namespace {
int BSE_ParseSegmentType(const idToken& token) {
	if (token == "effect") {
		return SEG_EFFECT;
	}
	if (token == "emitter") {
		return SEG_EMITTER;
	}
	if (token == "spawner") {
		return SEG_SPAWNER;
	}
	if (token == "trail") {
		return SEG_TRAIL;
	}
	if (token == "sound") {
		return SEG_SOUND;
	}
	if (token == "decal") {
		return SEG_DECAL;
	}
	if (token == "light") {
		return SEG_LIGHT;
	}
	if (token == "delay") {
		return SEG_DELAY;
	}
	if (token == "shake") {
		return SEG_SHAKE;
	}
	if (token == "tunnel") {
		return SEG_TUNNEL;
	}
	if (token == "doubleVision" || token == "doublevision") {
		return SEG_DOUBLEVISION;
	}
	return SEG_NONE;
}
}

void rvDeclEffect::Init()
{
	mMinDuration = 0.0;
	mMaxDuration = 0.0;
	mSize = 512.0;
	mFlags = 0;
	mPlayCount = 0;
	mLoopCount = 0;
	mCutOffDistance = 0.0;
	mSegmentTemplates.Clear();
}

bool rvDeclEffect::SetDefaultText()
{
	char generated[1024]; // [esp+4h] [ebp-404h]

	idStr::snPrintf(generated, sizeof(generated), "effect %s // IMPLICITLY GENERATED\n%s", GetName(), DefaultDefinition());
	SetText(generated);
	return true;
}

size_t rvDeclEffect::Size(void) const {
	return sizeof(rvDeclEffect);
}

int rvDeclEffect::GetTrailSegmentIndex(const idStr& name)
{
	rvDeclEffect* v2; // esi
	int v3; // edi
	int v4; // ebx
	rvSegmentTemplate* v5; // eax
	int result; // eax

	v2 = this;
	v3 = 0;
	if (mSegmentTemplates.Num() <= 0)
	{
	LABEL_6:
		common->Warning("^4BSE:^1 Unable to find segment '%s'\n", name.c_str());
		result = -1;
	}
	else
	{
		v4 = 0;
		while (1)
		{
			v5 = &this->mSegmentTemplates[v4];
			if (v5)
			{
				if (name == v5->GetSegmentName())
					break;
			}
			++v3;
			++v4;
			if (v3 >= this->mSegmentTemplates.Num())
				goto LABEL_6;
		}
		result = v3;
	}
	return result;
}

bool rvDeclEffect::Compare(const rvDeclEffect& comp) const
{
	if (mSegmentTemplates.Num() != comp.mSegmentTemplates.Num()) {
		return false;
	}

	for (int i = 0; i < mSegmentTemplates.Num(); ++i) {
		if (!(mSegmentTemplates[i] == comp.mSegmentTemplates[i])) {
			return false;
		}
	}

	return true;
}

float rvDeclEffect::CalculateBounds(void)
{
	float size = 0.0f;
	for (int i = 0; i < mSegmentTemplates.Num(); ++i) {
		const float segmentSize = mSegmentTemplates[i].CalculateBounds();
		if (segmentSize > size) {
			size = segmentSize;
		}
	}
	return idMath::Ceil(size);
}

float rvDeclEffect::EvaluateCost(int activeParticles, int segment) const
{
	int v5; // edi
	int v6; // ebx
	double v7; // st7
	float cost; // [esp+Ch] [ebp+8h]

	if (segment != -1)
		return mSegmentTemplates[segment].EvaluateCost(activeParticles);
	v5 = 0;
	cost = 0.0;
	if (this->mSegmentTemplates.Num() > 0)
	{
		v6 = 0;
		do
		{
			v7 = mSegmentTemplates[v6].EvaluateCost(activeParticles);
			++v5;
			++v6;
			cost = v7 + cost;
		} while (v5 < this->mSegmentTemplates.Num());
	}
	return cost;
}

void rvDeclEffect::FreeData()
{
	int v2; // ebx
	int v3; // edi
	rvSegmentTemplate* v4; // eax
	int* v5; // edi

	v2 = 0;
	if (this->mSegmentTemplates.Num() > 0)
	{
		v3 = 0;
		do
		{
			mSegmentTemplates[v3].GetParticleTemplate()->Purge();
			mSegmentTemplates[v3].GetParticleTemplate()->PurgeTraceModel();
			++v2;
			++v3;
		} while (v2 < this->mSegmentTemplates.Num());
	}
	mSegmentTemplates.Clear();
}

const char* rvDeclEffect::DefaultDefinition() const
{
	return "{\n}\n";
}

void rvDeclEffect::SetMinDuration(float duration)
{
	if (this->mMinDuration < duration)
		this->mMinDuration = duration;
}

void rvDeclEffect::SetMaxDuration(float duration)
{
	if (this->mMaxDuration < duration)
		this->mMaxDuration = duration;
}

void rvDeclEffect::Finish() {
	rvSegmentTemplate* segment;
	const int preservedFlags = mFlags & ETFLAG_EDITOR_MODIFIED;
	mFlags = preservedFlags;

	for (int j = 0; j < mSegmentTemplates.Num(); j++)
	{
		segment = &mSegmentTemplates[j];
		if (segment)
		{
			segment->Finish(this);

			if (segment->GetType() == SEG_SOUND)
				mFlags |= ETFLAG_HAS_SOUND;

			if (segment->GetParticleTemplate()->UsesEndOrigin())
				mFlags |= ETFLAG_USES_ENDORIGIN;

			if (segment->GetParticleTemplate()->GetHasPhysics() || segment->GetHasPhysics())
				mFlags |= ETFLAG_HAS_PHYSICS;
			if (segment->GetAttenuateEmitter())
				mFlags |= ETFLAG_ATTENUATES;
			if (segment->GetUseMaterialColor())
				mFlags |= ETFLAG_USES_MATERIAL_COLOR;
			if (segment->GetOrientateIdentity())
				mFlags |= ETFLAG_ORIENTATE_IDENTITY;

			segment->EvaluateTrailSegment(this);
			segment->SetMinDuration(this);
			segment->SetMaxDuration(this);
		}
	}

	// Keep authored size as a lower bound but ensure runtime culling/spawn
	// has a sane envelope derived from actual segment parameters.
	mSize = Max(mSize, CalculateBounds());
}

bool rvDeclEffect::Parse(const char* text, const int textLength) {
	idParser src;
	idToken	token;

	FreeData();
	mFlags = 0;
	mMinDuration = 0.0f;
	mMaxDuration = 0.0f;
	mCutOffDistance = 0.0f;
	mSize = 512.0f;

	src.LoadMemory(text, textLength, GetFileName());
	src.SetFlags(DECL_LEXER_FLAGS);
	if (!src.SkipUntilString("{")) {
		src.Warning("^4BSE:^1 Expected '{' in effect '%s'", GetName());
		return false;
	}

	if (src.ReadToken(&token))
	{
		while (token != "}")
		{
			const int segmentType = BSE_ParseSegmentType(token);
			if (segmentType != SEG_NONE) {
				rvSegmentTemplate segment;
				segment.Init(this);
				segment.Parse(this, segmentType, &src);
				if (segment.Finish(this)) {
					mSegmentTemplates.Append(segment);
				}
			}
			else if (token == "cutOffDistance") {
				mCutOffDistance = src.ParseFloat();
			}
			else if (token == "size")
			{
				mSize = src.ParseFloat();
			}
			else
			{
				src.Warning("^4BSE:^1 Invalid segment type '%s' (file: %s, line: %d)\n", token.c_str(), GetFileName(), src.GetLineNum());
				if (src.CheckTokenString("{")) {
					src.SkipBracedSection(false);
				}
			}

			if (!src.ReadToken(&token)) {
				src.Warning("^4BSE:^1 Unexpected end of effect '%s'", GetName());
				break;
			}
		}
	}

	Finish();

	return true;
}
