// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "../idlib/precompiled.h"
#ifndef ID_DEDICATED
#include "RenderingSettings.h"
#include "../renderer/RendererModule.h"
#include "../framework/SettingsPersistence.h"
#include "../framework/Session_local.h"
#include <limits>

namespace openq4::ui {
namespace {
std::uint64_t catalogSerial=0;
bool Desktop() {
#ifdef __ANDROID__
	return false;
#else
	return true;
#endif
}
bool Fail(std::string& error,const char* text) { error=text; return false; }
bool AtMainMenu() { return session && !sessLocal.IsMapSpawned() && !session->IsLoadingSaveGame(); }
// A single-player game the restart can save and load again around the reload.
bool CanCarryGame() { return session && sessLocal.CanSaveForRendererRestart(); }
bool SameCatalog(const renderDeviceCatalog_t& a,const renderDeviceCatalog_t& b) {
	if (a.count!=b.count) return false;
	for (unsigned i=0; i<a.count; ++i)
		if (strcmp(a.devices[i].id,b.devices[i].id) || strcmp(a.devices[i].name,b.devices[i].name) ||
			a.devices[i].usable!=b.devices[i].usable || a.devices[i].preferred!=b.devices[i].preferred) return false;
	return true;
}
}
const std::map<std::string,size_t>& RenderingSettings::Schema() {
	static const auto schema=[] {
		std::map<std::string,size_t> result{{"settings.render.catalog",2},{"settings.render.count",0},
			{"settings.render.selected",0},{"settings.render.active",2},{"settings.render.pending",1},
			{"settings.render.autoDetect",1},{"settings.render.canRestart",1},{"settings.render.canChoose",1},
			{"settings.render.vulkan",1},{"settings.render.gl",1},{"settings.render.gles",1},
			{"settings.render.notice",2},{"settings.render.backend",2}};
		for(unsigned i=0; i<RENDER_DEVICE_LIMIT; ++i) {
			result.emplace("settings.render."+std::to_string(i)+".label",2);
			result.emplace("settings.render."+std::to_string(i)+".usable",1);
		}
		return result;
	}();
	return schema;
}
void RenderingSettings::Capture() {
	if (captured) return;
	available=R_RendererModule_QueryRenderingDevices(catalog);
	captured=true;
	if (catalogSerial!=(std::numeric_limits<std::uint64_t>::max)()) token=++catalogSerial;
	else { token=0; available=false; }
}
bool RenderingSettings::Pending(const StateValues& draft) {
	const auto api=draft.find("r_renderApi"), device=draft.find("r_renderDevice");
	return api==draft.end() || device==draft.end() ||
		R_RendererModule_RenderSelectionPending(std::get<std::string>(api->second).c_str(),std::get<std::string>(device->second).c_str());
}
void RenderingSettings::Read(const StateValues& draft,bool clean,StateValues& values) {
	Capture();
	const bool overrideDevice=cvarSystem->GetCVarInteger("r_vkDevice")>=0;
	bool vulkan=false;
	int selected=-1;
	const auto device=draft.find("r_renderDevice");
	const std::string preference=device==draft.end()?"auto":std::get<std::string>(device->second);
	if (preference!="auto") selected=RENDER_DEVICE_LIMIT; // Reserved unavailable choice.
	for(unsigned i=0; i<RENDER_DEVICE_LIMIT; ++i) {
		const auto prefix="settings.render."+std::to_string(i);
		const bool usable=available && i<catalog.count && catalog.devices[i].usable;
		values[prefix+".label"]=i<catalog.count?std::string(catalog.devices[i].name):std::string();
		values[prefix+".usable"]=usable;
		vulkan|=usable;
		if(i<catalog.count && preference==catalog.devices[i].id) selected=static_cast<int>(i);
	}
	const bool pending=Pending(draft);
	const auto* config=renderSystem?&renderSystem->GetGLConfig():nullptr;
	std::string active=cvarSystem->GetCVarString("r_actualRenderApi");
	if(config && config->renderer_string) active+=" / "+std::string(config->renderer_string);
	const std::string api=std::get<std::string>(draft.at("r_renderApi"));
	const auto& status=R_RendererModule_GetStatus();
	const bool fallback=status.activeApi!=status.requestedApi &&
		!(status.requestedApi==RENDER_MODULE_API_GL_MODULE && status.activeApi==RENDER_MODULE_API_GL);
	values["settings.render.backend"]=api=="gl-module"?std::string("gl"):api;
	values["settings.render.active"]=std::move(active);
	values["settings.render.catalog"]=std::to_string(token);
	values["settings.render.count"]=double(catalog.count);
	values["settings.render.selected"]=double(selected);
	values["settings.render.pending"]=pending;
	values["settings.render.autoDetect"]=!pending;
	values["settings.render.canChoose"]=!overrideDevice; // Auto also repairs a missing saved GPU on GL-only hosts.
	const bool menu=AtMainMenu(), carry=!menu && CanCarryGame();
	values["settings.render.canRestart"]=clean && pending && (menu || carry);
	values["settings.render.vulkan"]=vulkan;
	values["settings.render.gl"]=Desktop();
	values["settings.render.gles"]=!Desktop();
	values["settings.render.notice"]=std::string(overrideDevice?"#str_230151":pending?
		(clean?(menu?"#str_230148":carry?"#str_231767":"#str_230149"):"#str_230147"):
		selected==RENDER_DEVICE_LIMIT?"#str_230150":fallback?"#str_230156":
		status.activeApi==RENDER_MODULE_API_GL || status.activeApi==RENDER_MODULE_API_GL_MODULE || status.activeApi==RENDER_MODULE_API_GLES?
		"#str_230146":"#str_230145");
}
bool RenderingSettings::Select(double index,const std::string& seen,StateValues& patch,std::string& error) {
	Capture();
	if (!token || seen!=std::to_string(token)) return Fail(error,"The GPU list changed; choose again");
	if(cvarSystem->GetCVarInteger("r_vkDevice")>=0) return Fail(error,"A diagnostic GPU override is active");
	if (!std::isfinite(index) || std::floor(index)!=index || index < -1 || index>=catalog.count)
		return Fail(error,"Invalid rendering-device choice");
	if(index==-1) {
		patch["r_renderDevice"]=std::string("auto");
		patch["r_renderApi"]=std::string("best");
		error.clear(); return true;
	}
	renderDeviceCatalog_t fresh{};
	if (!R_RendererModule_QueryRenderingDevices(fresh) || !SameCatalog(catalog,fresh)) {
		Reset(); return Fail(error,"The GPU list changed; choose again");
	}
	if(index>=0 && !fresh.devices[static_cast<unsigned>(index)].usable) return Fail(error,"This GPU cannot run the Vulkan renderer");
	patch["r_renderDevice"]=std::string(fresh.devices[static_cast<unsigned>(index)].id);
	patch["r_renderApi"]=std::string("vulkan");
	error.clear(); return true;
}
bool RenderingSettings::Backend(const std::string& api,StateValues& patch,std::string& error) {
	Capture(); bool vulkan=false;
	for(unsigned i=0; i<catalog.count; ++i) vulkan|=available && catalog.devices[i].usable;
	if(api!="best" && !(api=="gl" && Desktop()) && !(api=="gles" && !Desktop()) && !(api=="vulkan" && vulkan))
		return Fail(error,"Rendering backend is unavailable");
	patch["r_renderApi"]=api;
	error.clear(); return true;
}
bool RenderingSettings::Restart(std::string& error) {
	// No game is discarded by a settings action. A single-player game is saved
	// first and loaded again by the reload; anything that cannot be saved has
	// to be left through the ordinary menu flow before the restart is offered.
	const bool menu=AtMainMenu();
	if(!menu && !CanCarryGame()) return Fail(error,"Return to the main menu before restarting the renderer");
	if(!menu && !sessLocal.SaveForRendererRestart()) return Fail(error,"The game could not be saved before restarting the renderer");
	if(!Common_WriteSettingsConfiguration(false,error)) return false;
	R_RendererModule_UseSavedSelectionOnReload();
	if(menu) cmdSystem->BufferCommandText(CMD_EXEC_APPEND,"reloadEngine menu\n");
	else sessLocal.QueueRendererRestart();
	error.clear(); return true;
}
}
#endif
