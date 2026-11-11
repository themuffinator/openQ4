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
 // Immutable original 53-field projection. Existing effect journals retain
 // this meaning; additional fields belong to a new catalog version.
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
const std::map<std::string,SettingsEffectField>& SettingsEffectCatalogV2() {
 static const auto catalog=[] {
  auto result=SettingsEffectCatalogV1();
  result.emplace("ui_retainedScale",SettingsEffectField{0,0});
  result.emplace("ui_retainedTextScale",SettingsEffectField{0,0});
  return result;
 }();
 return catalog;
}
const std::map<std::string,SettingsEffectField>& SettingsEffectCatalogV3() {
 static const auto catalog=[] {
  auto result=SettingsEffectCatalogV2();
  for (const auto* key:{"r_shadowMapPointSize","r_shadowMapFilterTaps","r_shadowMapPointFilterTaps",
   "r_shadowMapFilterMode","r_shadowMapPointFilterMode","r_shadowMapAtlasSize",
   "r_ssaoSamples","r_bloomMipCount","r_motionBlurSamples"}) result.emplace(key,SettingsEffectField{0,0});
  result.emplace("r_shadowMapPointLights",SettingsEffectField{1,0});
  result.emplace("r_hdrSceneTarget",SettingsEffectField{1,0});
  result.emplace("image_picmip",SettingsEffectField{0,2});
  result.emplace("r_shadowMapCascadeCount",SettingsEffectField{0,0});
  for (const auto* key:{"r_shadowMapCSM","r_shadowMapProjectedCSM","r_shadowMapCascadeStabilize",
   "r_motionBlurObjectVectors","r_shadowMapHashedAlpha","r_shadowMapTranslucentReceivers"})
   result.emplace(key,SettingsEffectField{1,0});
  return result;
 }();
 return catalog;
}
const std::map<std::string,SettingsEffectField>& SettingsEffectCatalogV4() {
 static const auto catalog=[] {
  auto result=SettingsEffectCatalogV3();
  result.emplace("r_renderApi",SettingsEffectField{2,64});
  result.emplace("r_renderDevice",SettingsEffectField{2,64});
  return result;
 }();
 return catalog;
}
const std::map<std::string,SettingsEffectField>& SettingsEffectCatalogV5() {
 static const auto catalog=[] {
  auto result=SettingsEffectCatalogV4();
  result.at("r_swapInterval")=SettingsEffectField{0,0};
  return result;
 }();
 return catalog;
}
bool BuildSettingsEffectPlan(const StateValues& baseline, const StateValues& target,
 const std::map<std::string,std::size_t>& catalog, SettingsEffectPlan& output, std::string& error, unsigned requested) {
 try {
  const unsigned newest=catalog.size()==SettingsEffectCatalogV1().size()?1u:
   catalog.size()==SettingsEffectCatalogV2().size()?2u:catalog.size()==SettingsEffectCatalogV3().size()?3u:5u;
  // V4 and V5 share their keys: only a plan that names V4 keeps its meaning.
  const unsigned version=requested==0?newest:requested;
  if (version!=newest && !(version==4 && newest==5)) return Fail(error,"Settings effect catalog version does not match its keys");
  const auto& audited=version==1?SettingsEffectCatalogV1():version==2?SettingsEffectCatalogV2():
   version==3?SettingsEffectCatalogV3():version==4?SettingsEffectCatalogV4():SettingsEffectCatalogV5();
  if (catalog.size()!=audited.size() || baseline.size()!=audited.size() || target.size()!=audited.size())
   return Fail(error,"Settings effect catalog or complete snapshots changed");
  SettingsEffectPlan candidate; candidate.version=version;
  bool changed=false; std::size_t baselineBytes=0,targetBytes=0;
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
 if (!BuildSettingsEffectPlan(baseline,target,catalog,expected,error,plan.version==4?4u:0u)) return false;
 if (plan!=expected) return Fail(error,"Settings effect plan contradicts its catalog changes");
 error.clear(); return true;
}
} // namespace openq4::ui
