// BSE_Manager.cpp
//


#include "../idlib/precompiled.h"
#if defined(_MSC_VER)
#pragma hdrstop
#endif

#include "BSE_Envelope.h"
#include "BSE_Particle.h"
#include "BSE.h"
#include "BSE_SpawnDomains.h"

namespace {
ID_INLINE int BSE_GetFrameCounterMode(void) {
	return cvarSystem ? cvarSystem->GetCVarInteger("bse_frameCounters") : 0;
}
}

idCVar bse_speeds("bse_speeds", "0", CVAR_INTEGER, "print bse frame statistics");
idCVar bse_enabled("bse_enabled", "1", CVAR_BOOL, "set to false to disable all effects");
idCVar bse_render("bse_render", "1", CVAR_BOOL, "disable effect rendering");
idCVar bse_debug("bse_debug", "0", CVAR_INTEGER, "BSE debug level (0=off, 1=log, 2=log+onscreen)");
idCVar bse_showBounds("bse_showBounds", "0", CVAR_INTEGER, "display debug bounding boxes effect");
idCVar bse_physics("bse_physics", "1", CVAR_BOOL, "disable effect physics");
idCVar bse_debris("bse_debris", "1", CVAR_BOOL, "disable effect debris");
idCVar bse_singleEffect("bse_singleEffect", "", 0, "set to the name of the effect that is only played");
idCVar bse_rateLimit("bse_rateLimit", "6", CVAR_FLOAT, "rate limit for spawned effects");
idCVar bse_rateCost("bse_rateCost", "1", CVAR_FLOAT, "rate cost multiplier for spawned effects");
idCVar bse_scale("bse_scale", "1", CVAR_FLOAT | CVAR_ARCHIVE, "effect scalability amount");
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

// How much rate budget each category pays back per second.  The buckets used to
// drain one 0.1 step per StartFrame() call, and the game only reaches StartFrame
// from idGameLocal::RunFrame at the fixed 60Hz game tic, so 6.0/sec reproduced
// the drain the server has always had - without inheriting its dependence on
// who calls StartFrame, or on the caller's frame rate.
//
// That drain, and the matching bse_rateLimit of 1, were far too tight to survive
// multiplayer.  At 0.1 per effect they bought a burst of ten EC_IMPACT effects
// and sixty a second, and EC_IMPACT is charged once per hitscan impact by
// idGameLocal::HitScan: the multiplayer shotgun alone fires eleven pellets in a
// single frame, so one blast overran the whole burst budget and the rest of its
// impacts vanished silently.  Clients had it worse still - they replay every
// hitscan the server batched into one 50ms snapshot in a single frame, so the
// bursts arriving there are several server frames' worth at once.
//
// Sized instead against what the game can legitimately produce: a burst of ~60
// impacts covers two simultaneous shotgun blasts inside one client batch, and
// 250/sec covers a full server of hitscan weapons firing continuously.  The
// limiter is still there for genuinely pathological effect storms, which is what
// it is for; it no longer fires during ordinary combat.
static const float BSE_RATE_DECAY_PER_SEC = 25.0f;

idBlockAlloc<rvBSE, 256, 0>	rvBSEManagerLocal::effects;
idList<idTraceModel*>		rvBSEManagerLocal::mTraceModels;
int							rvBSEManagerLocal::mPerfCounters[NUM_PERF_COUNTERS];
float						rvBSEManagerLocal::mEffectRates[EC_MAX];
int							rvBSEManagerLocal::mEffectRateTime = 0;

/*
====================
BSE_Stats_f

Summarises the effect decls: how many are loaded, their segments, and how many of
those segments emit particles.  "all" parses every registered effect first.  As in
retail, decl 0 (_default) is skipped and the totals start at one so the averages
never divide by zero.
====================
*/
static void BSE_Stats_f(const idCmdArgs& args) {
	const bool parseAll = !idStr::Icmp(args.Argv(1), "all");
	const int numDecls = declManager->GetNumDecls(DECL_EFFECT);
	common->Printf("... processing %d registered effects\n", numDecls);

	int loaded = 1;
	int unreferenced = 0;
	int segments = 1;
	int particleSegments = 1;
	int particles = 1;
	for (int i = 1; i < numDecls; ++i) {
		const rvDeclEffect* effect = static_cast<const rvDeclEffect*>(declManager->DeclByIndex(DECL_EFFECT, i, parseAll));
		if (effect == NULL) {
			continue;
		}
		if (effect->GetState() != DS_PARSED) {
			if (!effect->EverReferenced()) {
				++unreferenced;
			}
			continue;
		}
		++loaded;
		for (int j = 0; j < effect->GetNumSegmentTemplates(); ++j) {
			const rvSegmentTemplate* st = effect->GetSegmentTemplate(j);
			if (st->GetHasParticles()) {
				particles += static_cast<int>(st->GetMaxCount());
				++particleSegments;
			}
		}
		segments += effect->GetNumSegmentTemplates();
	}

	common->Printf("%d segments in %d loaded effects (%d never referenced)\n", segments, loaded, unreferenced);
	common->Printf("%.2f segments per effect\n", (float)segments / (float)loaded);
	common->Printf("%.2f of segments have particles\n", (float)particleSegments / (float)segments);
	common->Printf("%.2f particles per segment with particles\n", (float)particles / (float)particleSegments);
}

/*
====================
BSE_Log_f

Lists every effect played or looped since the game started.
====================
*/
static void BSE_Log_f(const idCmdArgs& args) {
	const int numDecls = declManager->GetNumDecls(DECL_EFFECT);
	common->Printf("Processing %d effect decls....\n", numDecls);

	int active = 0;
	for (int i = 1; i < numDecls; ++i) {
		const rvDeclEffect* effect = static_cast<const rvDeclEffect*>(declManager->DeclByIndex(DECL_EFFECT, i, false));
		if (effect == NULL || (!effect->GetPlayCount() && !effect->GetLoopCount())) {
			continue;
		}
		common->Printf("%d plays (%d loops): '%s'\n", effect->GetPlayCount(), effect->GetLoopCount(), effect->GetName());
		++active;
	}
	common->Printf("%d effects played or looped out of %d\n", active, numDecls);
}

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

	cmdSystem->AddCommand("bseStats", BSE_Stats_f, CMD_FL_SYSTEM, "Dumps the stats of every registered effect - use all to force parse every effect");
	cmdSystem->AddCommand("bseLog", BSE_Log_f, CMD_FL_SYSTEM, "Dumps the number of times an effect has been played since game start");

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

	cmdSystem->RemoveCommand("bseStats");
	cmdSystem->RemoveCommand("bseLog");

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

bool rvBSEManagerLocal::ServiceEffect(class rvRenderEffectLocal* def, float ownerTime, float presentationTime) {
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
		presentationTime = pauseTime;
	}

	const char* effectName = def->parms.declEffect ? def->parms.declEffect->GetName() : "";
	if (Filtered(effectName, EC_IGNORE)) {
		def->expired = true;
		return true;
	}

	def->effect->SetRenderWorld( reinterpret_cast<idRenderWorld*>(def->world) );

	if (def->effect->Service(&def->parms, ownerTime, presentationTime, def->gameTime > def->serviceTime, forcePush)) {
		def->expired = true;
		return true;
	}

	def->expired = false;
	def->newEffect = false;
	def->serviceTime = def->gameTime;
	const idBounds currentLocalBounds = def->effect->GetCurrentLocalBounds();
	def->referenceBounds = currentLocalBounds;
	if (bse_speeds.GetBool())
		++rvBSEManagerLocal::mPerfCounters[PERF_NUM_BSE];
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

bool rvBSEManagerLocal::IsEffectStopped(const rvRenderEffectLocal* def) const {
	return ( def != NULL && def->effect != NULL ) ? def->effect->GetStopped() : false;
}

void rvBSEManagerLocal::SetEffectStopped(rvRenderEffectLocal* def, bool stopped) {
	if ( def != NULL && def->effect != NULL ) {
		def->effect->SetStopped( stopped );
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
	ResetRateTimes();
}

void rvBSEManagerLocal::EndLevelLoad(void) {
	ResetRateTimes();
}

void rvBSEManagerLocal::ResetRateTimes(void) {
	for (int i = 0; i < EC_MAX; ++i) {
		mEffectRates[i] = 0.0f;
	}
	mEffectRateTime = Sys_Milliseconds();
}

void rvBSEManagerLocal::StartFrame(void) {
	BSE_ResetFrameCounters();
	UpdateRateTimes();

	if (bse_speeds.GetInteger())
	{
		for (int i = 0; i < NUM_PERF_COUNTERS; ++i) {
			rvBSEManagerLocal::mPerfCounters[i] = 0;
		}
	}
}

void rvBSEManagerLocal::EndFrame(void) {
	if (BSE_GetFrameCounterMode() > 0) {
		common->Printf(
			"BSE manager frame counters: spawned=%d serviced=%d rendered=%d\n",
			bse_frameSpawned,
			bse_frameServiced,
			bse_frameRendered);
	}

	if (bse_speeds.GetInteger()) {
		// Active particle units include individual electricity bolts. Texel
		// coverage is not measured; do not report a misleading constant zero.
		common->Printf("bse_active: %i particles: %i traces: %i segments: %i\n",
			rvBSEManagerLocal::mPerfCounters[PERF_NUM_BSE],
			rvBSEManagerLocal::mPerfCounters[PERF_NUM_PARTICLES],
			rvBSEManagerLocal::mPerfCounters[PERF_NUM_TRACES],
			rvBSEManagerLocal::mPerfCounters[PERF_NUM_SEGMENTS]);
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

/*
====================
rvBSEManagerLocal::UpdateRateTimes

Drain the per-category rate buckets by however much wall time has passed.

This used to drain a fixed step per call, which quietly made the limiter depend
on someone calling it every frame.  Only the game does, and only from
idGameLocal::RunFrame - a path a multiplayer client never runs.  On a client the
buckets therefore only ever filled, and after ten bullet impacts EC_IMPACT was
over budget for the rest of the map and every hit effect was silently dropped.
Draining by elapsed time is correct no matter who calls this, or how often, so
the limiter can no longer ratchet shut on any frame loop.
====================
*/
void rvBSEManagerLocal::UpdateRateTimes(void) {
	const int now = Sys_Milliseconds();
	const int elapsed = now - mEffectRateTime;

	if (elapsed <= 0) {
		// same millisecond, or the timer went backwards
		if (elapsed < 0) {
			mEffectRateTime = now;
		}
		return;
	}
	mEffectRateTime = now;

	const float decay = Max(0.0f, bse_rateCost.GetFloat()) * BSE_RATE_DECAY_PER_SEC * (elapsed * 0.001f);
	for (int i = 0; i < EC_MAX; ++i) {
		mEffectRates[i] = Max(0.0f, mEffectRates[i] - decay);
	}
}

bool rvBSEManagerLocal::CanPlayRateLimited(effectCategory_t category) {
	if (category <= EC_IGNORE || category >= EC_MAX) {
		return true;
	}

	// Pay the buckets back before charging them.  Callers are spread across the
	// game frame, the client snapshot path and the renderer's effect service,
	// and only the first of those ever reaches StartFrame().
	UpdateRateTimes();

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
