// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "SettingsEffectPlan.h"
#include "SettingsTransaction.h"

namespace openq4::ui {
namespace {
bool Fail(std::string& error, const char* message) noexcept {
 try { error=message; } catch (...) { error.clear(); } return false;
}
}
const std::map<std::string,SettingsEffectField>& SettingsEffectCatalogV1() {
 // Audited projection of SystemSettingsHost::Catalog. The native test compares
 // every entry against that actual catalog; a catalog change needs a deliberate
 // version/compatibility decision, not a silent recovery reinterpretation.
 static const std::map<std::string,SettingsEffectField> catalog = {
	{"com_performancePreset",{2,32}},
	{"r_bloom",{1,0}},
	{"r_borderless",{1,1}},
	{"r_brightness",{0,0}},
	{"r_crt",{1,0}},
	{"r_customHeight",{0,1}},
	{"r_customWidth",{0,1}},
	{"r_displayRefresh",{0,1}},
	{"r_forceAmbient",{0,0}},
	{"r_fullscreen",{1,1}},
	{"r_fullscreenDesktop",{1,1}},
	{"r_hdrToneMap",{1,0}},
	{"r_lightGridPreload",{1,8}},
	{"r_mode",{0,1}},
	{"r_multiSamples",{0,1}},
	{"r_multiScreen",{0,1}},
	{"r_postAA",{0,0}},
	{"r_renderer",{2,16}},
	{"r_screen",{0,1}},
	{"r_screenFraction",{0,0}},
	{"r_shadows",{1,0}},
	{"r_skipBump",{1,0}},
	{"r_skipSky",{1,0}},
	{"r_skipSpecular",{1,0}},
	{"r_ssao",{1,0}},
	{"r_swapInterval",{0,1}},
	{"r_useLightGrid",{1,0}},
	{"r_windowHeight",{0,1}},
	{"r_windowWidth",{0,1}},
	{"ui_aspectCorrection",{1,0}},
	{"com_machineSpec",{0,0}},
	{"com_maxfps",{0,0}},
	{"image_anisotropy",{0,0}},
	{"image_downSize",{1,2}},
	{"image_downSizeBump",{1,2}},
	{"image_downSizeBumpLimit",{0,2}},
	{"image_downSizeLimit",{0,2}},
	{"image_downSizeSpecular",{1,2}},
	{"image_downSizeSpecularLimit",{0,2}},
	{"image_ignoreHighQuality",{1,2}},
	{"image_usePrecompressedTextures",{0,2}},
	{"image_writeGeneratedImages",{1,8}},
	{"r_motionBlur",{1,0}},
	{"r_rendererBenchmarkPreset",{2,16}},
	{"r_rendererUploadFrameBuffers",{0,16}},
	{"r_rendererUploadMegs",{0,16}},
	{"r_shadowMapMaxUpdatesPerView",{0,0}},
	{"r_shadowMapSize",{0,0}},
	{"r_useShadowMap",{1,0}},
	{"s_maxEmitterChannels",{0,4}},
	{"s_maxSoundsPerShader",{0,8}},
	{"s_numberOfSpeakers",{0,4}},
	{"s_useEAXReverb",{1,4}},

 };
 return catalog;
}
bool BuildSettingsEffectPlan(const StateValues& baseline, const StateValues& target,
 const std::map<std::string,std::size_t>& catalog, SettingsEffectPlan& output, std::string& error) {
 try {
  const auto& audited=SettingsEffectCatalogV1();
  if (catalog.size()!=audited.size() || baseline.size()!=audited.size() || target.size()!=audited.size())
   return Fail(error,"Settings effect catalog or complete snapshots changed");
  SettingsEffectPlan candidate; bool changed=false; std::size_t baselineBytes=0,targetBytes=0;
  for (const auto& [key,field]:audited) {
   const auto declared=catalog.find(key); const auto old=baseline.find(key),next=target.find(key);
   if (declared==catalog.end() || declared->second!=field.type || old==baseline.end() || next==target.end() ||
    old->second.index()!=field.type || next->second.index()!=field.type || !ValidStateValue(old->second) || !ValidStateValue(next->second))
    return Fail(error,"Settings effect catalog keys, types or values changed");
   baselineBytes+=key.size()+(field.type==2?std::get<std::string>(old->second).size():sizeof(double));
   targetBytes+=key.size()+(field.type==2?std::get<std::string>(next->second).size():sizeof(double));
   if (baselineBytes>SettingsTransaction::MaxSnapshotBytes || targetBytes>SettingsTransaction::MaxSnapshotBytes)
    return Fail(error,"Settings effect snapshots exceed the byte budget");
   if (!SettingsValueEqual(old->second,next->second)) { changed=true; candidate.changeMask|=field.effects; }
  }
  if (!changed) return Fail(error,"Settings effect plan has no changed setting");
  candidate.domainMask=candidate.changeMask & 31u;
  candidate.completion=(candidate.domainMask & 1u)?SettingsEffectCompletion::DisplayConfirmed:SettingsEffectCompletion::Automatic;
  candidate.rendererStrategy=(candidate.domainMask & 19u)?SettingsRendererStrategy::CoalescedDevice:SettingsRendererStrategy::None;
  output=candidate; error.clear(); return true;
 } catch (...) { return Fail(error,"Settings effect plan allocation failed"); }
}
bool ValidateSettingsEffectPlan(const SettingsEffectPlan& plan, const StateValues& baseline,
 const StateValues& target, const std::map<std::string,std::size_t>& catalog, std::string& error) {
 SettingsEffectPlan expected;
 if (!BuildSettingsEffectPlan(baseline,target,catalog,expected,error)) return false;
 if (plan!=expected) return Fail(error,"Settings effect plan contradicts its catalog changes");
 error.clear(); return true;
}
} // namespace openq4::ui
