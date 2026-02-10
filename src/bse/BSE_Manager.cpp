// BSE_Manager.cpp
//


#include "BSE_Envelope.h"
#include "BSE_Particle.h"
#include "BSE.h"
#include "BSE_SpawnDomains.h"

rvBSEManagerLocal bseLocal;
rvBSEManager* bse = &bseLocal;

extern idCVar bse_frameCounters;

idCVar bse_speeds("bse_speeds", "0", CVAR_INTEGER, "print bse frame statistics");
idCVar bse_enabled("bse_enabled", "1", CVAR_BOOL, "set to false to disable all effects");
idCVar bse_render("bse_render", "1", CVAR_BOOL, "disable effect rendering");
idCVar bse_debug("bse_debug", "0", CVAR_INTEGER, "display debug info about effect");
idCVar bse_showBounds("bse_showbounds", "0", CVAR_BOOL, "display debug bounding boxes effect");
idCVar bse_physics("bse_physics", "1", CVAR_BOOL, "disable effect physics");
idCVar bse_debris("bse_debris", "1", CVAR_BOOL, "disable effect debris");
idCVar bse_singleEffect("bse_singleEffect", "", 0, "set to the name of the effect that is only played");
idCVar bse_rateLimit("bse_rateLimit", "1", CVAR_FLOAT, "rate limit for spawned effects");
idCVar bse_rateCost("bse_rateCost", "1", CVAR_FLOAT, "rate cost multiplier for spawned effects");
idCVar bse_scale("bse_scale", "1", CVAR_FLOAT, "global BSE scaling for spawn/detail");
idCVar bse_maxParticles("bse_maxParticles", "2048", CVAR_INTEGER, "maximum per-segment particle allocation");

int bse_frameSpawned = 0;
int bse_frameServiced = 0;
int bse_frameRendered = 0;

void BSE_ResetFrameCounters(void) {
	bse_frameSpawned = 0;
	bse_frameServiced = 0;
	bse_frameRendered = 0;
}

void BSE_AddSpawned(int count) {
	if (count > 0) {
		bse_frameSpawned += count;
	}
}

void BSE_AddServiced(int count) {
	if (count > 0) {
		bse_frameServiced += count;
	}
}

void BSE_AddRendered(int count) {
	if (count > 0) {
		bse_frameRendered += count;
	}
}

float effectCosts[EC_MAX] = { 0.0f, 0.1f, 0.1f };

idBlockAlloc<rvBSE, 256, 0>	rvBSEManagerLocal::effects;
idVec3						rvBSEManagerLocal::mCubeNormals[6];
idMat3						rvBSEManagerLocal::mModelToBSE;
idList<idTraceModel*>		rvBSEManagerLocal::mTraceModels;
const char* rvBSEManagerLocal::mSegmentNames[SEG_COUNT];
int							rvBSEManagerLocal::mPerfCounters[NUM_PERF_COUNTERS];
float						rvBSEManagerLocal::mEffectRates[EC_MAX];
/*
====================
rvBSEManagerLocal::Init
====================
*/
bool rvBSEManagerLocal::Init(void) {
	common->Printf("----------------- BSE Init ------------------\n");

	declManager->FindEffect("_default");
	declManager->FindMaterial("_default");
	declManager->FindMaterial("gfx/effects/particles_shapes/motionblur");
	declManager->FindType(DECL_TABLE, "halfsintable", true);
	renderModelManager->FindModel("_default");
	pauseTime = -1.0f;

	common->Printf("--------- BSE Created Successfully ----------\n");
	return true;
}

/*
====================
rvBSEManagerLocal::Shutdown
====================
*/
bool rvBSEManagerLocal::Shutdown(void) {
	common->Printf("--------------- BSE Shutdown ----------------\n");

	for (int i = 0; i < mTraceModels.Num(); ++i) {
		delete mTraceModels[i];
	}
	mTraceModels.Clear();
	mEffectRates[0] = 0.0f;
	mEffectRates[1] = 0.0f;
	mEffectRates[2] = 0.0f;
	pauseTime = -1.0f;

	common->Printf("---------------------------------------------\n");
	return true;
}

/*
=======================
rvBSEManagerLocal::AddTraceModel
=======================
*/
int rvBSEManagerLocal::AddTraceModel(idTraceModel* model)
{
	mTraceModels.Append(model);
	return mTraceModels.Num() - 1;
}

/*
=======================
rvBSEManagerLocal::GetTraceModel
=======================
*/
idTraceModel* rvBSEManagerLocal::GetTraceModel(int index)
{
	idTraceModel* result; // eax

	if (index < 0 || index >= mTraceModels.Num())
		result = 0;
	else
		result = mTraceModels[index];
	return result;
}

/*
=======================
rvBSEManagerLocal::FreeTraceModel
=======================
*/
void rvBSEManagerLocal::FreeTraceModel(int index)
{
	if (index >= 0 && index < mTraceModels.Num())
	{
		delete mTraceModels[index];
		mTraceModels[index] = NULL;
	}
}

bool rvBSEManagerLocal::PlayEffect(class rvRenderEffectLocal* def, float time) {
	if (!bse_enabled.GetBool() || !def || !def->parms.declEffect) {
		return false;
	}

	const rvDeclEffect* v3 = (const rvDeclEffect*)def->parms.declEffect;
	const idStr effectName = def->parms.declEffect->GetName();

	if (Filtered(effectName, EC_IGNORE)) {
		def->effect = NULL;
		def->expired = true;
		return false;
	}
	if (bse_debug.GetInteger())
	{
		common->Printf("Playing effect: %s at %g\n", effectName.c_str(), time);
	}
	++v3->mPlayCount;
	rvBSE* effectState = effects.Alloc();
	if (!effectState) {
		def->effect = NULL;
		def->expired = true;
		return false;
	}
	def->effect = effectState;
	def->expired = false;
	def->newEffect = true;
	effectState->SetRenderWorld( reinterpret_cast<idRenderWorld*>(def->world) );
	effectState->Init(v3, &def->parms, time);
	return true;
}

bool rvBSEManagerLocal::ServiceEffect(class rvRenderEffectLocal* def, float time) {
	BSE_AddServiced(1);

	if (!def || !def->effect) {
		if (def) {
			def->expired = true;
		}
		return true;
	}

	if (!bse_enabled.GetBool()) {
		def->expired = true;
		return true;
	}

	bool forcePush = false;

	if (pauseTime >= 0.0f) {
		time = pauseTime;
	}

	const idStr effectName = def->parms.declEffect ? def->parms.declEffect->GetName() : "";
	if (Filtered(effectName.c_str(), EC_IGNORE)) {
		def->expired = true;
		return true;
	}

	def->effect->SetRenderWorld( reinterpret_cast<idRenderWorld*>(def->world) );

	if (def->effect->Service(&def->parms, time, def->gameTime > def->serviceTime, forcePush)) {
		def->expired = true;
		return true;
	}

	def->expired = false;
	def->newEffect = false;
	def->serviceTime = def->gameTime;
	const idBounds currentLocalBounds = def->effect->GetCurrentLocalBounds();
	def->referenceBounds = currentLocalBounds;
	if (bse_speeds.GetBool())
		++rvBSEManagerLocal::mPerfCounters[0];
	if (bse_debug.GetInteger())
		def->effect->EvaluateCost();

	return false;
}

idRenderModel* rvBSEManagerLocal::RenderEffect(class rvRenderEffectLocal* def, const struct viewDef_s* view) {
	if (!def || !def->effect) {
		if (def && def->dynamicModel) {
			delete def->dynamicModel;
			def->dynamicModel = NULL;
			def->dynamicModelFrameCount = 0;
		}
		return NULL;
	}

	def->dynamicModel = def->effect->Render(def->dynamicModel, &def->parms, view);
	if (!def->dynamicModel) {
		def->dynamicModelFrameCount = 0;
	}
	return def->dynamicModel;
}

void rvBSEManagerLocal::StopEffect(rvRenderEffectLocal* def) {
	if (def && def->index >= 0 && def->effect)
	{
		if (bse_debug.GetInteger())
		{
			idStr effectName = def->parms.declEffect->GetName();
			common->Printf("Stopping effect %s\n", effectName.c_str());
		}
		def->effect->SetStopped(true);
		def->newEffect = false;
	}
	else if (def)
	{
		def->newEffect = false;
		def->expired = true;
	}
}

void rvBSEManagerLocal::FreeEffect(rvRenderEffectLocal* def) {
	if (def && def->index >= 0 && def->effect)
	{
		if (bse_debug.GetInteger())
		{
			idStr effectName = def->parms.declEffect->GetName();
			common->Printf("Freeing effect %s\n", effectName.c_str());
		}
		def->effect->Destroy();
		effects.Free(def->effect);
		def->effect = NULL;
	}
	if (def) {
		def->newEffect = false;
		def->expired = true;
	}
}

float rvBSEManagerLocal::EffectDuration(const rvRenderEffectLocal* def) {
	if (!def) {
		return 0.0f;
	}
	if (def->effect) {
		return def->effect->GetDuration();
	}
	if (def->parms.declEffect) {
		const rvDeclEffect* decl = (const rvDeclEffect*)def->parms.declEffect;
		return decl->GetMaxDuration();
	}
	return 0.0f;
}

bool rvBSEManagerLocal::CheckDefForSound(const renderEffect_t* def) {
	if (!def || !def->declEffect) {
		return false;
	}
	const rvDeclEffect* decl = (const rvDeclEffect*)def->declEffect;
	return decl->GetHasSound();
}

void rvBSEManagerLocal::BeginLevelLoad(void) {
	mEffectRates[0] = 0.0f;
	mEffectRates[1] = 0.0f;
	mEffectRates[2] = 0.0f;
}

void rvBSEManagerLocal::EndLevelLoad(void) {
	mEffectRates[0] = 0.0f;
	mEffectRates[1] = 0.0f;
	mEffectRates[2] = 0.0f;
}

void rvBSEManagerLocal::StartFrame(void) {
	BSE_ResetFrameCounters();
	UpdateRateTimes();

	if (bse_speeds.GetInteger())
	{
		rvBSEManagerLocal::mPerfCounters[0] = 0;
		rvBSEManagerLocal::mPerfCounters[1] = 0;
		rvBSEManagerLocal::mPerfCounters[2] = 0;
		rvBSEManagerLocal::mPerfCounters[3] = 0;
		rvBSEManagerLocal::mPerfCounters[4] = 0;
	}
}

void rvBSEManagerLocal::EndFrame(void) {
	if (bse_frameCounters.GetInteger() > 0) {
		common->Printf(
			"BSE manager frame counters: spawned=%d serviced=%d rendered=%d\n",
			bse_frameSpawned,
			bse_frameServiced,
			bse_frameRendered);
	}

	if (bse_speeds.GetInteger()) {
		common->Printf("bse_active: %i particles: %i traces: %i texels: %i\n",
			rvBSEManagerLocal::mPerfCounters[0],
			rvBSEManagerLocal::mPerfCounters[2],
			rvBSEManagerLocal::mPerfCounters[1],
			(double)rvBSEManagerLocal::mPerfCounters[3] * 0.00000095367431640625);
	}
}

bool rvBSEManagerLocal::Filtered(const char* name, effectCategory_t category) {
	if (!name) {
		return true;
	}

	const char* singleFilter = bse_singleEffect.GetString();
	if (singleFilter && singleFilter[0] != '\0') {
		if (idStr::FindText(name, singleFilter, false) < 0) {
			return true;
		}
	}

	return !CanPlayRateLimited(category);
}

void rvBSEManagerLocal::UpdateRateTimes(void) {
	const float decay = Max(0.0f, bse_rateCost.GetFloat()) * 0.1f;
	for (int i = 0; i < EC_MAX; ++i) {
		mEffectRates[i] = Max(0.0f, mEffectRates[i] - decay);
	}
}

bool rvBSEManagerLocal::CanPlayRateLimited(effectCategory_t category) {
	if (category <= EC_IGNORE || category >= EC_MAX) {
		return true;
	}

	const float limit = Max(0.0f, bse_rateLimit.GetFloat());
	if (limit <= 0.0f) {
		return true;
	}

	const float cost = effectCosts[category] * Max(0.0f, bse_rateCost.GetFloat());
	if (mEffectRates[category] + cost > limit) {
		return false;
	}

	mEffectRates[category] += cost;
	return true;
}
