// BSE_EffectTemplate.cpp
//



#include "BSE_Envelope.h"
#include "BSE_Particle.h"
#include "BSE.h"
#include "BSE_SpawnDomains.h"

namespace {
int BSE_ParseSegmentType(const idToken& token) {
	if (!token.Icmp("effect")) {
		return SEG_EFFECT;
	}
	if (!token.Icmp("emitter")) {
		return SEG_EMITTER;
	}
	if (!token.Icmp("spawner")) {
		return SEG_SPAWNER;
	}
	if (!token.Icmp("trail")) {
		return SEG_TRAIL;
	}
	if (!token.Icmp("sound")) {
		return SEG_SOUND;
	}
	if (!token.Icmp("decal")) {
		return SEG_DECAL;
	}
	if (!token.Icmp("light")) {
		return SEG_LIGHT;
	}
	if (!token.Icmp("delay")) {
		return SEG_DELAY;
	}
	if (!token.Icmp("doubleVision")) {
		return SEG_DOUBLEVISION;
	}
	if (!token.Icmp("shake")) {
		return SEG_SHAKE;
	}
	if (!token.Icmp("tunnel")) {
		return SEG_TUNNEL;
	}
	return SEG_NONE;
}
}

void rvDeclEffect::Init()
{
	mEditorOriginal = NULL;
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
	return sizeof(rvDeclEffect) + mSegmentTemplates.Allocated();
}

rvDeclEffect& rvDeclEffect::operator=(const rvDeclEffect& copy) {
	CopyData(copy);
	return *this;
}

void rvDeclEffect::CopyData(const rvDeclEffect& copy) {
	if (&copy == this) {
		return;
	}

	const int playCount = mPlayCount;
	const int loopCount = mLoopCount;
	FreeData();

	mFlags = copy.mFlags;
	mMinDuration = copy.mMinDuration;
	mMaxDuration = copy.mMaxDuration;
	mCutOffDistance = copy.mCutOffDistance;
	mSize = copy.mSize;
	mPlayCount = playCount;
	mLoopCount = loopCount;

	for (int i = 0; i < copy.mSegmentTemplates.Num(); ++i) {
		rvSegmentTemplate& segment = mSegmentTemplates.Alloc();
		segment.Duplicate(copy.mSegmentTemplates[i]);
		segment.mDeclEffect = this;
	}
}

int rvDeclEffect::AddSegment(const rvSegmentTemplate& add) {
	rvSegmentTemplate& segment = mSegmentTemplates.Alloc();
	segment.Duplicate(add);
	segment.mDeclEffect = this;
	return mSegmentTemplates.Num() - 1;
}

void rvDeclEffect::DeleteSegment(int index) {
	if (index < 0 || index >= mSegmentTemplates.Num()) {
		return;
	}

	mSegmentTemplates.RemoveIndex(index);
}

void rvDeclEffect::CreateEditorOriginal(void) {
	DeleteEditorOriginal();
	mEditorOriginal = new rvDeclEffect(*this);
}

void rvDeclEffect::DeleteEditorOriginal(void) {
	delete mEditorOriginal;
	mEditorOriginal = NULL;
}

bool rvDeclEffect::CompareToEditorOriginal(void) const {
	return (mEditorOriginal != NULL) && Compare(*mEditorOriginal);
}

void rvDeclEffect::RevertToEditorOriginal(void) {
	if (mEditorOriginal == NULL) {
		return;
	}

	CopyData(*mEditorOriginal);
	CreateEditorOriginal();
}

const rvSegmentTemplate* rvDeclEffect::GetSegmentTemplate(const char* name) const {
	if (name == NULL) {
		return NULL;
	}

	for (int i = mSegmentTemplates.Num() - 1; i >= 0; --i) {
		if (mSegmentTemplates[i].GetSegmentName().Icmp(name) == 0) {
			return &mSegmentTemplates[i];
		}
	}

	return NULL;
}

rvSegmentTemplate* rvDeclEffect::GetSegmentTemplate(const char* name) {
	return const_cast<rvSegmentTemplate*>(static_cast<const rvDeclEffect*>(this)->GetSegmentTemplate(name));
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
				if (name.Icmp(v5->GetSegmentName()) == 0)
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

	if (segment != -1) {
		if (segment < 0 || segment >= mSegmentTemplates.Num()) {
			return 0.0f;
		}
		return mSegmentTemplates[segment].EvaluateCost(activeParticles);
	}
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
	mMinDuration = 0.0f;
	mMaxDuration = 0.0f;
	mSegmentTemplates.SetNum(mSegmentTemplates.Num(), false);

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

	mSize = CalculateBounds();
}

bool rvDeclEffect::Parse(const char* text, const int textLength) {
	return Parse(text, textLength, false);
}

bool rvDeclEffect::Parse(const char* text, const int textLength, bool noCaching) {
	idParser src;
	idToken	token;
	bool parsed = false;

	(void)noCaching;

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

	while (src.ReadToken(&token))
	{
		if (token == "}") {
			parsed = true;
			break;
		}

		const int segmentType = BSE_ParseSegmentType(token);
		if (segmentType != SEG_NONE) {
			rvSegmentTemplate segment;
			segment.Init(this);
			segment.Parse(this, segmentType, &src);
			if (segment.Finish(this)) {
				mSegmentTemplates.Append(segment);
			}
		}
		else if (!token.Icmp("cutOffDistance")) {
			mCutOffDistance = src.ParseFloat();
		}
		else if (!token.Icmp("size"))
		{
			mSize = src.ParseFloat();
		}
		else
		{
			src.Warning("^4BSE:^1 Invalid segment type '%s' (file: %s, line: %d)\n", token.c_str(), GetFileName(), src.GetLineNum());
			src.SkipBracedSection(true);
		}
	}

	if (!parsed) {
		src.Warning("^4BSE:^1 Unexpected end of effect '%s'", GetName());
		return false;
	}

	Finish();

	return true;
}
