// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "../../idlib/precompiled.h"
#ifndef ID_DEDICATED
#include "SystemSettingsHost.h"
#include "../../framework/CVarDefaults.h"
#include <algorithm>
#include <bit>
#include <charconv>
#include <cstdint>
#include <cmath>
#include <limits>
#include <utility>
#if defined(USE_SDL3)
#include <SDL3/SDL_video.h>
#include "../../renderer/RenderModuleAPI.h"
#endif

namespace openq4::ui {
namespace {
SystemSettingDescriptor Number(const char* key, double minimum, double maximum,
	const char* label = "", unsigned effects = SystemSettingImmediate,
	std::vector<double> choices = {}, bool integer = true) {
	return {key, 0, minimum, maximum, integer, std::move(choices), {}, label, effects};
}
SystemSettingDescriptor Boolean(const char* key, const char* label = "", unsigned effects = SystemSettingImmediate) {
	return {key, 1, 0, 1, false, {}, {}, label, effects};
}
SystemSettingDescriptor String(const char* key, std::vector<std::string> choices,
	const char* label = "", unsigned effects = SystemSettingImmediate) {
	return {key, 2, 0, 0, false, {}, std::move(choices), label, effects};
}
bool Fail(std::string& error, const std::string& key, const char* reason) {
	error = key + ": " + reason;
	return false;
}
const SystemSettingDescriptor* Descriptor(const std::string& key) {
	const auto& catalog = SystemSettingsHost::Catalog();
	const auto found = std::find_if(catalog.begin(), catalog.end(), [&](const auto& item) { return item.key == key; });
	return found == catalog.end() ? nullptr : &*found;
}
bool SameValue(const StateValue& a, const StateValue& b) {
	return SettingsValueEqual(a,b);
}
bool Changed(const StateValues& before, const StateValues& target, const std::string& key) {
	const auto old = before.find(key), next = target.find(key);
	return old == before.end() || next == target.end() || !SameValue(old->second, next->second);
}
bool Typed(const SystemSettingDescriptor& item, const StateValue& value, std::string& error) {
	if (value.index() != item.type || !ValidStateValue(value)) return Fail(error, item.key, "invalid setting type or value");
	if (item.integer) {
		const double number = std::get<double>(value);
		if (std::floor(number) != number || number < (std::numeric_limits<int>::min)() || number > (std::numeric_limits<int>::max)())
			return Fail(error, item.key, "expected a representable integer");
	}
	return true;
}
idCVar* Registered(const SystemSettingDescriptor& item, std::string& error) {
	idCVar* variable = cvarSystem ? cvarSystem->Find(item.key.c_str()) : nullptr;
	if (!variable) { Fail(error, item.key, "setting is not registered on this host"); return nullptr; }
	const int flags = variable->GetFlags();
	if ((flags & CVAR_PRIVATE) != 0) { Fail(error, item.key, "private settings are unavailable"); return nullptr; }
	const bool compatible = item.type == 1 ? (flags & CVAR_BOOL) != 0 :
		item.type == 0 ? (flags & (item.integer ? CVAR_INTEGER : CVAR_FLOAT)) != 0 && (flags & CVAR_BOOL) == 0 :
		(flags & (CVAR_BOOL | CVAR_INTEGER | CVAR_FLOAT)) == 0;
	if (!compatible) { Fail(error, item.key, "registered type does not match the settings catalog"); return nullptr; }
	return variable;
}
bool Parse(const SystemSettingDescriptor& item, const char* text, StateValue& output, std::string& error) {
	PresentationValue parsed;
	const PresentationType type = item.type == 0 ? PresentationType::Number :
		item.type == 1 ? PresentationType::Boolean : PresentationType::String;
	if (!text || !ParsePresentationValue(type, text, parsed, error)) {
		error = item.key + ": invalid registered setting value"; return false;
	}
	StateValue candidate;
	if (item.type == 0) candidate = parsed.data[0];
	else if (item.type == 1) candidate = parsed.data[0] != 0;
	else candidate = std::move(parsed.text);
	if (!Typed(item, candidate, error)) return false;
	output = std::move(candidate);
	return true;
}
std::string Serialize(const StateValue& value) {
	PresentationValue converted;
	if (const double* number = std::get_if<double>(&value)) {
		// CVar's legacy IsNumeric grammar excludes exponents. Passing scientific
		// notation would replace the string with a six-decimal float rendering,
		// losing small/custom values before exact readback. Fixed shortest form
		// retains the requested binary64 decimal and does not change CVar policy.
		std::string text;
		return SettingsNumberText(*number,SettingsNumberFormat::FixedShortest,text) ? text : std::string{};
	}
	else if (const bool* boolean = std::get_if<bool>(&value)) {
		converted.type = PresentationType::Boolean; converted.data[0] = *boolean ? 1 : 0;
	} else { converted.type = PresentationType::String; converted.text = std::get<std::string>(value); }
	return FormatPresentationValue(converted);
}
bool RegisteredValue(const SystemSettingDescriptor& item, idCVar& variable, const StateValue& value, std::string& error) {
	if (!Typed(item, value, error)) return false;
	if (item.type == 0) {
		const double number = std::get<double>(value);
		if (variable.GetMinValue() < variable.GetMaxValue() &&
			(number < variable.GetMinValue() || number > variable.GetMaxValue()))
			return Fail(error, item.key, "outside the registered CVar range");
	} else if (item.type == 2) {
		const char** choices = variable.GetValueStrings();
		if (choices && choices[0]) {
			bool matched = false;
			for (size_t i = 0; choices[i]; ++i) if (std::get<std::string>(value) == choices[i]) { matched = true; break; }
			if (!matched) return Fail(error, item.key, "not a registered CVar choice");
		}
	}
	return true;
}
bool Writable(const SystemSettingDescriptor& item, idCVar& variable, std::string& error) {
	if (variable.GetFlags() & (CVAR_ROM | CVAR_INIT | CVAR_NETWORKSYNC | CVAR_CHEAT | CVAR_PRIVATE))
		return Fail(error, item.key, "setting is protected or unavailable to local settings writes");
	return true;
}
bool EditorValue(const SystemSettingDescriptor& item, const StateValue& value, std::string& error) {
	if (item.type == 0) {
		const double number = std::get<double>(value);
		if (number < item.minimum || number > item.maximum) return Fail(error, item.key, "outside the settings range");
		if (!item.integer) {
			// New edits must survive the actual float conversion, including the
			// engine's FTZ/DAZ mode. Inspect double zero by representation so DAZ
			// cannot make a nonzero binary64 subnormal look like an explicit zero.
			// Observe both values explicitly: optimizers otherwise assume gradual
			// underflow and may fold the integer test back into an FP comparison.
			static_assert(sizeof(double)==sizeof(std::uint64_t) && std::numeric_limits<double>::is_iec559);
			const volatile std::uint64_t magnitude=std::bit_cast<std::uint64_t>(number)&0x7fffffffffffffffULL;
			const bool nonzero=magnitude!=0;
			const volatile float converted=static_cast<float>(number);
			const float actual=converted;
			if (!std::isfinite(actual) || (nonzero && actual==0))
				return Fail(error, item.key, "value is not representable by the float setting");
		}

		if (!item.numberChoices.empty() && std::find(item.numberChoices.begin(), item.numberChoices.end(), number) == item.numberChoices.end())
			return Fail(error, item.key, "not a supported settings choice");
	} else if (item.type == 2 && !item.stringChoices.empty() &&
		std::find(item.stringChoices.begin(), item.stringChoices.end(), std::get<std::string>(value)) == item.stringChoices.end())
		return Fail(error, item.key, "not a supported settings choice");
	return true;
}
bool Complete(const StateValues& values, std::string& error) {
	if (values.size() != SystemSettingsHost::Catalog().size()) return Fail(error, "settings", "expected the complete SYSTEM catalog");
	for (const auto& item : SystemSettingsHost::Catalog()) {
		const auto value = values.find(item.key);
		if (value == values.end() || !Typed(item, value->second, error)) return Fail(error, item.key, "missing or invalid catalog value");
	}
	return true;
}

// Indexed by the persistent legacy mode number from RenderSystem_init.cpp.
// Keep this lookup independent of renderer-module linkage; the host only
// validates requests and does not initialize or call a rendering backend.
constexpr int LegacyModes[][2] = {
	{1280,720},{1366,768},{1600,900},{1920,1080},{1920,1200},{2560,1080},{2560,1440},{3440,1440},{3840,2160},{5120,1440},{5120,2880},
	{640,480},{720,480},{720,576},{800,480},{800,600},{854,480},{960,540},{960,600},{1024,576},{1024,600},{1024,768},
	{1152,648},{1152,720},{1152,768},{1152,864},{1280,768},{1280,800},{1280,854},{1280,960},{1280,1024},{1360,768},
	{1400,900},{1400,1050},{1440,900},{1440,960},{1536,864},{1600,1200},{1680,1050},{1920,1280},{2048,1080},{2048,1152},
	{2048,1536},{2160,1440},{2240,1260},{2240,1400},{2256,1504},{2304,1440},{2400,1350},{2400,1600},{2520,1680},
	{2560,1600},{2560,1664},{2736,1824},{2880,1620},{2880,1800},{2880,1864},{2880,1920},{3000,2000},{3024,1964},
	{3200,1800},{3200,2000},{3240,2160},{3456,2160},{3456,2234},{3840,1080},{3840,1200},{3840,1600},{3840,2400},{3840,2560},
	{4096,2160},{5120,2160},{6016,3384},{7680,2160},{7680,4320}
};
#if defined(USE_SDL3)
bool DisplayModePixels(const SDL_DisplayMode* mode, int& width, int& height) {
	if (!mode || mode->w <= 0 || mode->h <= 0 || !std::isfinite(mode->pixel_density) || mode->pixel_density <= 0) return false;
	// Match the strict SDL window service: exclusive requests are pixels,
	// including native mode on displays whose logical mode has higher density.
	const double w = std::floor(static_cast<double>(mode->w) * mode->pixel_density + 0.5);
	const double h = std::floor(static_cast<double>(mode->h) * mode->pixel_density + 0.5);
	if (!std::isfinite(w) || !std::isfinite(h) || w < 1 || h < 1 ||
		w > (std::numeric_limits<int>::max)() || h > (std::numeric_limits<int>::max)()) return false;
	width = static_cast<int>(w); height = static_cast<int>(h);
	return true;
}
#endif
bool DisplayTuple(const StateValues& current, const StateValues& candidate, std::string& error) {
	const bool windowChanged = Changed(current, candidate, "r_windowWidth") || Changed(current, candidate, "r_windowHeight") ||
		Changed(current, candidate, "r_fullscreen");
	if (windowChanged && !std::get<bool>(candidate.at("r_fullscreen"))) {
		const double width = std::get<double>(candidate.at("r_windowWidth"));
		const double height = std::get<double>(candidate.at("r_windowHeight"));
		if (width < 320 || width > 16384 || height < 240 || height > 16384)
			return Fail(error, "window", "window dimensions are outside the supported bounds");
	}
	const char* coupled[] = {"r_screen", "r_multiScreen", "r_fullscreen", "r_fullscreenDesktop", "r_mode",
		"r_customWidth", "r_customHeight", "r_displayRefresh"};
	bool changed = false;
	for (const char* key : coupled) changed = changed || Changed(current, candidate, key);
	if (!changed) return true;
	const auto integer = [&](const char* key) { return static_cast<int>(std::get<double>(candidate.at(key))); };
	const int mode = integer("r_mode");
	if (mode < -2 || mode >= static_cast<int>(sizeof(LegacyModes) / sizeof(LegacyModes[0])))
		return Fail(error, "r_mode", "unknown legacy display mode");
#if defined(USE_SDL3)
	int displayCount = 0;
	SDL_DisplayID* displays = SDL_GetDisplays(&displayCount);
	if (!displays || displayCount <= 0 || displayCount > 1024) {
		SDL_free(displays); return Fail(error, "r_screen", "display enumeration is unavailable");
	}
	const int screen = integer("r_screen");
	if (screen < -1 || screen >= displayCount) {
		SDL_free(displays); return Fail(error, "r_screen", "selected display is no longer available");
	}
	SDL_DisplayID display = screen >= 0 ? displays[screen] : 0;
	if (display == 0) {
		const renderWindowServices_t* services = Sys_GetRenderWindowServices();
		renderWindowState_t observed{};
		if (services && services->QueryWindowState && services->QueryWindowState(&observed)) display = observed.displayId;
		// RefreshNativeWindowHandles also persists visible geometry. Validation
		// must only observe, and use the same current/primary Auto policy as the
		// strict window request when the current display is unavailable.
		if (std::find(displays, displays + displayCount, display) == displays + displayCount)
			display = SDL_GetPrimaryDisplay();
	}
	if (!display || std::find(displays, displays + displayCount, display) == displays + displayCount) {
		SDL_free(displays); return Fail(error, "r_screen", "selected display identity is unavailable");
	}
	SDL_free(displays);
	if (!std::get<bool>(candidate.at("r_fullscreen")) || std::get<bool>(candidate.at("r_fullscreenDesktop"))) return true;
	if (integer("r_multiScreen") != 0) return Fail(error, "r_multiScreen", "exclusive fullscreen cannot span displays");
	int width = 0, height = 0;
	if (mode == -2) {
		const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(display);
		if (!DisplayModePixels(desktop, width, height)) return Fail(error, "r_mode", "desktop pixel mode is unavailable or invalid");
	} else if (mode == -1) { width = integer("r_customWidth"); height = integer("r_customHeight"); }
	else { width = LegacyModes[mode][0]; height = LegacyModes[mode][1]; }
	if (width < 320 || width > 16384 || height < 240 || height > 16384)
		return Fail(error, "r_mode", "exclusive pixel dimensions are outside the supported bounds");
	int modeCount = 0;
	SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display, &modeCount);
	bool supported = false;
	const int refresh = integer("r_displayRefresh");
	for (int i = 0; modes && i < modeCount; ++i) {
		const SDL_DisplayMode* option = modes[i];
		int pixelWidth = 0, pixelHeight = 0;
		if (!DisplayModePixels(option, pixelWidth, pixelHeight) ||
			(option->displayID && option->displayID != display)) continue;
		// SYSTEM choices use integer refresh labels (59.94 is shown as 60).
		if (pixelWidth == width && pixelHeight == height && std::isfinite(option->refresh_rate) && option->refresh_rate >= 0 &&
			(refresh == 0 || std::floor(option->refresh_rate + .5f) == refresh)) { supported = true; break; }
	}
	SDL_free(modes);
	if (!supported) return Fail(error, "r_mode", "exclusive display size and refresh are unsupported");
	return true;
#else
	return Fail(error, "display", "typed display validation requires the SDL3 host route");
#endif
}
bool ValidateCandidate(const StateValues* original, const StateValues& current,
	const StateValues& candidate, std::string& error, bool checkDisplay = true) {
	if (!Complete(current, error) || !Complete(candidate, error) || (original && !Complete(*original, error))) return false;
	for (const auto& item : SystemSettingsHost::Catalog()) {
		idCVar* variable = Registered(item, error);
		if (!variable) return false;
		if (!Changed(current, candidate, item.key)) continue;
		const StateValue& value = candidate.at(item.key);
		if (!Writable(item, *variable, error) || !RegisteredValue(item, *variable, value, error)) return false;
		// Recovery may restore an observed custom value outside UI-only choices.
		const bool restoring = original && SameValue(original->at(item.key), value);
		if (!restoring && !EditorValue(item, value, error)) return false;
	}
	if (checkDisplay && !DisplayTuple(current, candidate, error)) return false;
	error.clear(); return true;
}
} // namespace

const std::vector<SystemSettingDescriptor>& SystemSettingsHost::Catalog() {
	static const std::vector<SystemSettingDescriptor> catalog = {
		String("com_performancePreset", {"minimum","lowpower","performance","balanced","quality","ultra"}, "#str_229976", SystemSettingPresetExpansion),
		Boolean("r_bloom", "#str_41082"), Boolean("r_borderless", "#str_229909", SystemSettingDisplayRestart),
		Number("r_brightness", .5, 2, "#str_200148", SystemSettingImmediate, {}, false), Boolean("r_crt", "#str_41090"),
		Number("r_customHeight", 240, 16384, "#str_229949", SystemSettingDisplayRestart),
		Number("r_customWidth", 320, 16384, "#str_229948", SystemSettingDisplayRestart),
		Number("r_displayRefresh", 0, 1000, "#str_229945", SystemSettingDisplayRestart),
		Number("r_forceAmbient", 0, 1, "#str_223003", SystemSettingImmediate, {}, false),
		Boolean("r_fullscreen", "#str_200147", SystemSettingDisplayRestart),
		Boolean("r_fullscreenDesktop", "#str_229910", SystemSettingDisplayRestart),
		Boolean("r_hdrToneMap", "#str_41084"), Boolean("r_lightGridPreload", "#str_42820", SystemSettingNextMap),
		Number("r_mode", -2, sizeof(LegacyModes) / sizeof(LegacyModes[0]) - 1, "#str_229975", SystemSettingDisplayRestart),
		Number("r_multiSamples", 0, 16, "#str_41093", SystemSettingDisplayRestart, {0,2,4,8,16}),
		Number("r_multiScreen", 0, 1, "#str_229915", SystemSettingDisplayRestart, {0,1}),
		Number("r_postAA", 0, 4, "#str_41094"),
		String("r_renderer", {"best","arb2"}, "#str_41103", SystemSettingRendererResources),
		Number("r_screen", -1, 1024, "#str_229912", SystemSettingDisplayRestart),
		Number("r_screenFraction", 10, 200, "#str_41091", SystemSettingImmediate, {10,25,50,75,85,100,125,150,200}),
		Boolean("r_shadows", "#str_200160"), Boolean("r_skipBump", "#str_200162"),
		Boolean("r_skipSky", "#str_223009"), Boolean("r_skipSpecular", "#str_200161"), Boolean("r_ssao", "#str_41088"),
		Number("r_swapInterval", 0, 1, "#str_41092", SystemSettingDisplayRestart, {0,1}),
		Boolean("r_useLightGrid", "#str_41107"),
		Number("r_windowHeight", 240, 16384, "#str_229947", SystemSettingDisplayRestart),
		Number("r_windowWidth", 320, 16384, "#str_229946", SystemSettingDisplayRestart),
		Boolean("ui_aspectCorrection", "#str_229944"),
		Number("com_machineSpec", -1, 3), Number("com_maxfps", 0, 1000), Number("image_anisotropy", 1, 16),
		Boolean("image_downSize", "", SystemSettingImageReload), Boolean("image_downSizeBump", "", SystemSettingImageReload),
		Number("image_downSizeBumpLimit", 0, 32768, "", SystemSettingImageReload),
		Number("image_downSizeLimit", 0, 32768, "", SystemSettingImageReload),
		Boolean("image_downSizeSpecular", "", SystemSettingImageReload),
		Number("image_downSizeSpecularLimit", 0, 32768, "", SystemSettingImageReload),
		Boolean("image_ignoreHighQuality", "", SystemSettingImageReload),
		Number("image_usePrecompressedTextures", 0, 2, "", SystemSettingImageReload),
		Boolean("image_writeGeneratedImages", "", SystemSettingNextMap), Boolean("r_motionBlur"),
		String("r_rendererBenchmarkPreset", {"low","baseline","modern","high-end"}, "", SystemSettingRendererResources),
		Number("r_rendererUploadFrameBuffers", 3, 8, "", SystemSettingRendererResources),
		Number("r_rendererUploadMegs", 1, 128, "", SystemSettingRendererResources),
		Number("r_shadowMapMaxUpdatesPerView", 0, 1024), Number("r_shadowMapSize", 128, 4096), Boolean("r_useShadowMap"),
		Number("s_maxEmitterChannels", 1, 48, "", SystemSettingAudioRestart),
		Number("s_maxSoundsPerShader", 0, 32, "", SystemSettingNextMap),
		Number("s_numberOfSpeakers", 2, 6, "", SystemSettingAudioRestart, {2,6}),
		Boolean("s_useEAXReverb", "", SystemSettingAudioRestart)
	};
	return catalog;
}
const std::map<std::string, size_t>& SystemSettingsHost::Schema() {
	static const std::map<std::string, size_t> schema = [] {
		std::map<std::string, size_t> result;
		for (const auto& item : Catalog()) result.emplace(item.key, item.type);
		return result;
	}();
	return schema;
}
bool SystemSettingsHost::ChangedRequiresDisplayRestart(const StateValues& before, const StateValues& target) {
	return (ChangedEffects(before,target) & SystemSettingDisplayRestart) != 0;
}
bool SystemSettingsHost::RequiresDeviceWork(const StateValues& before, const StateValues& target) {
	return ChangedEffects(before,target) != 0;
}
unsigned SystemSettingsHost::ChangedEffects(const StateValues& before, const StateValues& target) {
	unsigned result=0;
	for (const auto& item:Catalog()) if (Changed(before,target,item.key)) result|=item.effects;
	return result;
}
bool SystemSettingsHost::ResolveModeDimensions(int mode, int customWidth, int customHeight,
	int desktopPixelWidth, int desktopPixelHeight, int& width, int& height) {
	if (mode < -2 || mode >= int(sizeof(LegacyModes)/sizeof(LegacyModes[0]))) return false;
	const int w=mode==-2?desktopPixelWidth:mode==-1?customWidth:LegacyModes[mode][0];
	const int h=mode==-2?desktopPixelHeight:mode==-1?customHeight:LegacyModes[mode][1];
	if (w<320 || w>16384 || h<240 || h>16384) return false;
	width=w; height=h; return true;
}
bool SystemSettingsHost::Read(StateValues& values, std::string& error) {
	StateValues candidate;
	for (const auto& item : Catalog()) {
		idCVar* variable = Registered(item, error);
		StateValue value;
		if (!variable || !Parse(item, variable->GetString(), value, error)) return false;
		candidate.emplace(item.key, std::move(value));
	}
	values = std::move(candidate); error.clear(); return true;
}
bool SystemSettingsHost::Defaults(StateValues& values, std::string& error) {
	StateValues candidate;
	for (const auto& item : Catalog()) {
		if (!Registered(item, error)) return false;
		idStr text;
		if (!CVar_ReadDefault(item.key.c_str(), text)) return Fail(error, item.key, "registered default is unavailable");
		StateValue value;
		if (!Parse(item, text.c_str(), value, error)) return false;
		candidate.emplace(item.key, std::move(value));
	}
	values = std::move(candidate); error.clear(); return true;
}
bool SystemSettingsHost::Validate(const StateValues& baseline, const StateValues& candidate, std::string& error) {
	return ValidateCandidate(nullptr, baseline, candidate, error);
}
bool SystemSettingsHost::ValidateSavedTarget(const StateValues& baseline, const StateValues& candidate, std::string& error) {
	return ValidateCandidate(nullptr,baseline,candidate,error,false);
}
bool SystemSettingsHost::ValidateRollback(const StateValues& original, const StateValues& current,
	const StateValues& target, std::string& error) {
	return ValidateCandidate(&original, current, target, error);
}
bool SystemSettingsHost::Write(const StateValues& changes, std::string& error) {
	struct WriteItem { const SystemSettingDescriptor* item; StateValue value; std::string text; };
	std::vector<WriteItem> pending;
	// Validate every key before the first setter. UI-only constraints live in
	// Validate: rollback must still be able to restore a custom baseline.
	for (const auto& [key, value] : changes) {
		const auto* item = Descriptor(key);
		if (!item) return Fail(error, key, key == "r_renderApi" ? "renderer API changes require an engine restart" : "unknown SYSTEM setting");
		idCVar* variable = Registered(*item, error);
		if (!variable || !RegisteredValue(*item, *variable, value, error)) return false;
		StateValue previous;
		if (!Parse(*item, variable->GetString(), previous, error)) return false;
		if (SameValue(previous, value)) continue;
		if (!Writable(*item, *variable, error)) return false;
		std::string text=Serialize(value);
		if (item->type==0 && text.empty()) return Fail(error,key,"cannot serialize numeric setting");
		pending.push_back({item, value, std::move(text)});
	}
	for (const auto& write : pending) {
		// Reacquire the registered owner; never create unknown CVars or retain a
		// variable pointer across a write. A later refusal is a partial failure
		// and is restored by SettingsTransaction's ownership-aware rollback.
		idCVar* variable = Registered(*write.item, error);
		if (!variable || !Writable(*write.item, *variable, error)) return false;
		variable->SetString(write.text.c_str());
		variable = Registered(*write.item, error);
		StateValue actual;
		if (!variable || !Parse(*write.item, variable->GetString(), actual, error)) return false;
		if (!SameValue(actual, write.value)) return Fail(error, write.item->key, "CVar rejected or normalized the requested value");
	}
	error.clear(); return true;
}
bool SystemSettingsHost::NeedsConfirmation(const StateValues& before, const StateValues& target) const {
	return ChangedRequiresDisplayRestart(before, target);
}
} // namespace openq4::ui
#endif // !ID_DEDICATED
