// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "RetainedHud.h"
#include "UserInterfaceRetained.h"
#include "retained/Document.h"
#include <chrono>
#include "DeviceContext.h"
#include "Window.h"
#include "SimpleWindow.h"
#include "UserInterfaceLocal.h"
#include "RetainedUI.h"
extern idUserInterfaceManagerLocal uiManagerLocal;
extern idCVar r_skipGuiShaders;
#ifndef ID_DEDICATED
#include "../framework/KeyInput.h"
#if defined(__ANDROID__)
#include "../sys/android/android_public.h"
#endif
#include "retained/Runtime.h"
#include <json/json.h>
#include <cmath>
#include <cstdlib>
#include <array>
#include <map>
#include <set>
#include <string_view>
#include <unordered_map>

namespace {
constexpr const char* Artwork="guis/hud/artwork.q4ui";
constexpr const char* Registry="guis/hud/coverage.json";
bool Read(const char* path,std::string& result) {
	void* data=nullptr; const int size=fileSystem->ReadFile(path,&data);
	if (size<=0 || !data) return false;
	result.assign(static_cast<const char*>(data),size); fileSystem->FreeFile(data); return true;
}
std::string Key(std::string key) {
	for (auto& c:key) { if (c=='\\') c='/'; else if (c>='A' && c<='Z') c+=32; }
	return key;
}
}
struct idRetainedHud::Impl {
	retainedUIView_t* view=nullptr;
	std::map<std::string,std::string> materials;
	std::map<std::string,std::string> imageAliases;
	std::set<std::string> images,reported;
	// How each material this HUD has drawn maps (its artwork, an image alias
	// or itself), by the name pointer the capture hands over, so a frame folds
	// and looks up a name once per material rather than once per primitive.
	struct Mapping { std::string artwork,alias; bool image=false; };
	std::unordered_map<std::string,Mapping> mappings;
	bool attempted=false,announced=false,drawn=false;
	~Impl() { RetainedUI_DestroyView(view); }
	void Failure(const char* source,const std::string& error) {
		if (reported.insert(error).second) common->Warning("HUD coverage: %s: %s; using stock presentation",source,error.c_str());
	}
};
idRetainedHud::idRetainedHud() : impl(std::make_unique<Impl>()) {}
idRetainedHud::~idRetainedHud() = default;
void idRetainedHud::Reset() { impl=std::make_unique<Impl>(); }
bool idRetainedHud::DrawSucceeded() const { return impl->drawn; }
bool idRetainedHud::Usable() const { return !impl->attempted || impl->view!=nullptr; }
void idRetainedHud::Precache() {
	if (!cvarSystem->GetCVarBool("ui_retained")) return;
	// Game-owned damage numbers use the console atlas outside the GUI font
	// loader. Prepare its retained alias while image loading is deferred too.
	RetainedUI_PrecacheImage("fonts/english/bigchars",false);
	// The Remastered ECG is loaded from the installation and first appears
	// after map loading. Register its additive wrappers during level precache.
	for (const char* image : {"gfx/guis/hud/ekg.tga","gfx/guis/hud/s_ekg.tga","gfx/guis/hud/s_ekg2.tga"})
		RetainedUI_PrecacheImage(std::string("_repeat/")+image,true);
	RetainedUI_PrecacheImage("_repeat/gfx/guis/hud/waveform.tga",true);
	std::string source,error; if (!Read(Registry,source)) return;
	Json::Value data; Json::CharReaderBuilder builder; std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
	if (!reader->parse(source.data(),source.data()+source.size(),&data,&error) || !data.isObject() || !data["images"].isArray() ||
		(!data["imageAliases"].isNull() && !data["imageAliases"].isObject())) return;
	std::set<std::string> precached;
	for (const auto& image:data["images"]) {
		if (!image.isString()) continue;
		std::string name=image.asString();
		if (data["imageAliases"].isMember(name)) {
			if (!data["imageAliases"][name].isString()) continue;
			name=data["imageAliases"][name].asString();
		}
		if (!precached.insert(name).second) continue;
		// These three bitmaps are supplied by The Awakening, never by q4base.
		// Keep their aliases available without loading absent optional media.
		if ((name=="gfx/guis/hud/icons/gun_corecannon" || name=="gfx/guis/hud/icons/gun_freezegun" ||
			name=="gfx/guis/hud/icons/gun_flamegun") &&
			fileSystem->ReadFile((name+".tga").c_str(),nullptr)<0 &&
			fileSystem->ReadFile(("dds/"+name+".dds").c_str(),nullptr)<0) continue;
		const bool additive=name.starts_with("q4-add/");
		RetainedUI_PrecacheImage(additive?name.substr(7):name,additive);
	}
	// Objective photos are chosen by map scripts, not by the HUD document.
	// Register their retained aliases during the engine's deferred image load.
	idFileList* photos=fileSystem->ListFiles("gfx/objectives",".tga",false,true);
	if (photos) {
		for (int i=0;i<photos->GetNumFiles();++i) {
			idStr name=photos->GetFile(i); name.StripFileExtension();
			RetainedUI_PrecacheImage(name.c_str(),false);
		}
		fileSystem->FreeFileList(photos);
	}
}
bool idRetainedHud::Eligible(const char* source) {
	return source && cvarSystem->GetCVarBool("ui_retained") && Covers(source);
}
bool idRetainedHud::Covers(const char* source) {
	if (!source) return false;
	static const std::set<std::string> roots={
		"guis/hud.gui","guis/hud_strogg.gui","guis/cursor.gui","guis/mphud.gui",
		"guis/wristcomm.gui","guis/wristcomm_strogg.gui","guis/scoreboard.gui","guis/summary.gui","guis/mpmsgmode.gui",
		"guis/cinematic.gui","guis/e3cinematic.gui","guis/vehicles/hud.gui","guis/vehicles/airdef_cannon.gui",
		"guis/vehicles/medchange.gui","guis/vehicles/mdisp.gui","guis/vehicles/strogg_noise.gui","guis/vehicles/blank.gui",
		"guis/q4xhud.gui","guis/q4xhud_strogg.gui","guis/q4x_cannon_hud.gui","guis/dropshiphealth.gui","guis/mcchealth.gui","guis/introcinematic.gui",
		"guis/vehicles/speederplayerhud.gui","guis/vehicles/gev_hud.gui","guis/vehicles/mcc/cannonplayerhud.gui","guis/vehicles/airassault/turretplayerhud.gui",
		"guis/vehicles/turret_hud.gui"};
	return roots.contains(Key(source));
}
bool idRetainedHud::Draw(const char* source,std::vector<openq4::ui::HudPrimitive>&& captured) {
	using namespace openq4::ui;
	impl->drawn=false;
	if (!impl->attempted) {
		impl->attempted=true;
		// A mod's replacement owns its artwork and scripts, even at a stock name.
		if (idLegacyHudLayout::ModOwns(source)) return false;
		std::string registry,artwork,error;
		if (!Read(Registry,registry) || !Read(Artwork,artwork)) { impl->Failure(source,"missing retained artwork catalog"); return false; }
		Json::Value data; Json::CharReaderBuilder builder; std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
		if (!reader->parse(registry.data(),registry.data()+registry.size(),&data,&error) || !data.isObject() ||
			!data["version"].isInt() || data["version"].asInt()!=1 || !data["materials"].isObject() || !data["images"].isArray() ||
			(!data["imageAliases"].isNull() && !data["imageAliases"].isObject())) {
			impl->Failure(source,"invalid retained HUD coverage registry"); return false;
		}
		for (const auto& name:data["materials"].getMemberNames()) if (!data["materials"][name].isString()) {
			impl->Failure(source,"invalid HUD material mapping"); return false;
		}
		for (const auto& name:data["images"]) if (!name.isString()) { impl->Failure(source,"invalid HUD image mapping"); return false; }
		for (const auto& name:data["imageAliases"].getMemberNames()) if (!data["imageAliases"][name].isString()) {
			impl->Failure(source,"invalid HUD image alias"); return false;
		}
		for (const auto& name:data["materials"].getMemberNames()) impl->materials.emplace(Key(name),data["materials"][name].asString());
		for (const auto& name:data["images"]) impl->images.insert(Key(name.asString()));
		for (const auto& name:data["imageAliases"].getMemberNames()) impl->imageAliases.emplace(Key(name),data["imageAliases"][name].asString());
		impl->view=RetainedUI_CreateView(); std::vector<Diagnostic> diagnostics;
		if (!impl->view || !RetainedUI_LoadView(impl->view,artwork,Artwork,diagnostics)) {
			impl->Failure(source,diagnostics.empty()?"retained HUD view unavailable":diagnostics.front().message); return false;
		}
	}
	if (!impl->view || !RetainedUI_PrepareView(impl->view)) return false;
	Viewport viewport; if (!RetainedUI_DefaultViewport(viewport)) return false;
	auto items=std::move(captured);
	int vectors=0,glyphs=0,images=0;
	for (auto& item:items) {
		if (item.glyph) { ++glyphs; continue; }
		auto mapped=impl->mappings.find(item.material);
		if (mapped==impl->mappings.end()) {
			const auto name=Key(item.material);
			Impl::Mapping mapping;
			const auto found=impl->materials.find(name);
			if (found!=impl->materials.end()) mapping.artwork=found->second;
			else if (impl->images.contains(name) || name.starts_with("gfx/objectives/") || name.starts_with("levelshots/")) {
				mapping.image=true; const auto alias=impl->imageAliases.find(name);
				if (alias!=impl->imageAliases.end()) mapping.alias=alias->second;
			}
			else { impl->Failure(source,"unmapped material "+name); return false; }
			mapped=impl->mappings.emplace(item.material,std::move(mapping)).first;
		}
		const Impl::Mapping& mapping=mapped->second;
		if (mapping.image) { ++images; if (!mapping.alias.empty()) item.material=mapping.alias; }
		else { item.artwork=mapping.artwork; ++vectors; }
	}
	std::string error;
	if (!RetainedUI_ViewRuntime(impl->view)->SetHudPresentation(items,viewport,error)) { impl->Failure(source,error); return false; }
	const bool drawn=RetainedUI_DrawViewRoot(impl->view,viewport);
	impl->drawn=drawn;
	if (drawn && !impl->announced && !items.empty()) {
		common->Printf("HUD_COVERAGE source=%s renderer=retained vectors=%d glyphs=%d images=%d legacy=0\n",source,vectors,glyphs,images);
		impl->announced=true;
	}
	return drawn;
}
idCVar ui_hudLayout("ui_hudLayout","1",CVAR_GUI|CVAR_INTEGER|CVAR_ARCHIVE,
	"HUD layout: 0 Classic (the stock rectangles), 1 Remastered (anchored to the safe area), "
	"2 Minimal (health, armor, ammunition and the crosshair), 3 Competitive (multiplayer: the match bar, "
	"team status and network quality; elsewhere Remastered)",0,3);
idCVar ui_hudWidth("ui_hudWidth","1",CVAR_GUI|CVAR_INTEGER|CVAR_ARCHIVE,
	"how far edge HUD groups spread on wide views: 0 16:9, 1 21:9, 2 the full view",0,2);
// The HUD settings of section 13.9 the Remastered and Minimal layouts read
// (Classic keeps the stock HUD).
idCVar ui_hudOpacity("ui_hudOpacity","1",CVAR_GUI|CVAR_FLOAT|CVAR_ARCHIVE,"Remastered HUD opacity",0.25f,1.0f);
idCVar ui_hudDamageDirection("ui_hudDamageDirection","1",CVAR_GUI|CVAR_BOOL|CVAR_ARCHIVE,
	"Remastered HUD: show the damage direction indicator");
idCVar ui_hudPickups("ui_hudPickups","1",CVAR_GUI|CVAR_BOOL|CVAR_ARCHIVE,"Remastered HUD: show pickup messages");
idCVar ui_hudWeaponStrip("ui_hudWeaponStrip","1",CVAR_GUI|CVAR_BOOL|CVAR_ARCHIVE,"Remastered HUD: show the weapon strip on a weapon change");
// The accessibility baseline's reduced flashing (section 13.10): every HUD
// layout caps the damage direction indicator and the vehicle hit glow at 0.40,
// and the game dims its full-screen white flashes and drops the death
// afterimage.
idCVar ui_reducedFlashing("ui_reducedFlashing","0",CVAR_GUI|CVAR_BOOL|CVAR_ARCHIVE,
	"reduce flashing: cap hit indicators at 0.40, dim full-screen white flashes and drop the death afterimage");
idCVar ui_cinematicShake("ui_cinematicShake","1",CVAR_GUI|CVAR_FLOAT|CVAR_ARCHIVE,
	"added cinematic camera movement: 0 disables, 1 full; disabled in VR",0.0f,1.0f);

namespace {
struct RemasteredRoute { const char* source; const char* document; };
// Awakening's on-foot HUDs carry the stock groups plus their own expansion
// panels, which their documents cover behind the Marine/Strogg family.
constexpr RemasteredRoute RemasteredRoutes[]={
	{"guis/hud.gui","guis/hud/remastered_marine.q4ui"},
	{"guis/q4xhud.gui","guis/hud/remastered_marine_awakening.q4ui"},
	{"guis/hud_strogg.gui","guis/hud/remastered_strogg.q4ui"},
	{"guis/q4xhud_strogg.gui","guis/hud/remastered_strogg_awakening.q4ui"},
	{"guis/mphud.gui","guis/hud/remastered_mp.q4ui"},
	// The walker's and the hover tank's shared vehicle HUD (14.12).
	{"guis/vehicles/hud.gui","guis/hud/remastered_vehicle.q4ui"},
	// The air-defence cannon's optic, transmission display and HUD prompt (14.12).
	{"guis/vehicles/airdef_cannon.gui","guis/hud/remastered_airdef.q4ui"},
	// The Awakening's space cannon with its ship-status panel (14.12).
	{"guis/vehicles/airassault/turretplayerhud.gui","guis/hud/remastered_turret.q4ui"},
	// The Awakening's MCC cannon with the MCC's hull readout (14.12).
	{"guis/vehicles/mcc/cannonplayerhud.gui","guis/hud/remastered_mcc.q4ui"},
	// openQ4's HUD for the Awakening's chaingun turret: the gunner's gauges and the barrel heat (14.12).
	{"guis/vehicles/turret_hud.gui","guis/hud/remastered_chaingun_turret.q4ui"},
	// The Awakening's speeder bike with the m07 race display (14.12).
	{"guis/vehicles/speederplayerhud.gui","guis/hud/remastered_speeder.q4ui"},
	// The Awakening's GEV, drawn from scratch since its art never shipped (14.12).
	{"guis/vehicles/gev_hud.gui","guis/hud/remastered_gev.q4ui"},
	// Medical Labs' implant activation at the end of stroggification (14.19).
	{"guis/vehicles/medchange.gui","guis/hud/remastered_medchange.q4ui"},
	// The objectives display and its notices (14.11).
	{"guis/wristcomm.gui","guis/hud/remastered_objectives.q4ui"},
	{"guis/wristcomm_strogg.gui","guis/hud/remastered_objectives_strogg.q4ui"},
	// The scoreboard (14.14), Tourney's bracket tree included.
	{"guis/scoreboard.gui","guis/hud/remastered_scoreboard.q4ui"},
};
constexpr int AudioColumns=40;   // the playback graph's 650 ms history (section 14.8)
// ui_hudProfile N: where a Remastered HUD's frame goes, averaged over N frames
// per document: forwarded writes and events, the state batch, the document's
// draw and the stock windows' unseen evaluation.
idCVar ui_hudProfile("ui_hudProfile","0",CVAR_GUI|CVAR_INTEGER,"print Remastered HUD timings averaged over this many frames",0,1000);
double ProfileNow() { return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
}

struct idRemasteredHud::Impl {
	idStr document;
	idUserInterface* gui=nullptr;
	// The document's declared application state: 'n'umber, 'b'oolean, 's'tring.
	// Only these keys cross over, normalized, because one loose legacy value
	// would otherwise reject the document's whole state batch.
	struct StateKeyHash {
		using is_transparent=void;
		size_t operator()(std::string_view key) const {
			std::uint64_t hash=14695981039346656037ull;
			for (unsigned char c:key) { if (c>='A' && c<='Z') c+='a'-'A'; hash=(hash^c)*1099511628211ull; }
			return static_cast<size_t>(hash);
		}
	};
	struct StateKeyEqual {
		using is_transparent=void;
		bool operator()(std::string_view a,std::string_view b) const {
			if (a.size()!=b.size()) return false;
			for (size_t i=0;i<a.size();++i) {
				unsigned char x=a[i],y=b[i];
				if (x>='A' && x<='Z') x+='a'-'A';
				if (y>='A' && y<='Z') y+='a'-'A';
				if (x!=y) return false;
			}
			return true;
		}
	};
	struct ForwardValue { char type='s'; bool valid=false,icon=false,weapon=false; std::string raw,normalized; };
	std::unordered_map<std::string,ForwardValue,StateKeyHash,StateKeyEqual> types;
	openq4::ui::HudAmmoReadout ammoReadout;
	bool failed=false,reported=false;
	// The stock dictionary joins the document on its first draw; until then
	// forwarded writes are covered by that join, and events wait for it.
	bool joined=false;
	// The multiplayer document, whose chat box stands in for the stock one.
	bool chat=false;
	// The playback graphs' key names ("radio.peak.0" ...), built once, and
	// whether each graph last published flat.
	struct AudioGraph { std::vector<std::string> keys; bool declared=false,resolved=false,flat=false; };
	AudioGraph audioGraphs[3];
	int failures=0;
	// Named events sent before the document loads (a vehicle's boarding
	// events precede its HUD's first frame), replayed in order after the join.
	std::vector<std::string> pending;
	// Events whose effect outlasts them (extensions.openq4.joinEvents): when a
	// stock window variable holds at the join ("!" inverts the test), the event
	// replays, so a peer rebuilt mid-game - the layout switched to Classic and
	// back while a vehicle is crewed or a transmission plays - shows what the
	// stock windows show. A variable the stock GUI lacks is skipped.
	std::vector<std::pair<std::string,std::string>> joinEvents;
	// Layout readbacks (extensions.openq4.readbacks), applied the next frame;
	// none feeds back into the layout it measures:
	// [state, node, view] sets a boolean state when the node lays out wider
	// than the view (the chat entry's mark of text before its view);
	// [state, node, "@height"] a number state to the node's laid-out height in
	// canvas dp while it has one (the chat prompts, wrapped or not);
	// [state, view, "@firstInside", prefix, count] a number state to the first
	// of nodes prefix0..prefix<count-1> laid out with its top inside the
	// view, or count (the open chat's history fades the lines its room cuts).
	struct Readback { std::string state,node,view,prefix; int count=0; };
	std::vector<Readback> readbacks;
	std::map<std::string,float> readbackValues;
	// ui_hudProfile accumulators
	double forwardMs=0,eventMs=0,stateMs=0,drawMs=0,unseenMs=0,prepareMs=0,readbackMs=0;
	int forwards=0,events=0,frames=0;
	std::map<std::string,std::pair<int,double>> eventCost;
	void Report() {
		const int wanted=ui_hudProfile.GetInteger();
		if (wanted<=0) {
			forwardMs=eventMs=stateMs=drawMs=unseenMs=prepareMs=readbackMs=0;
			forwards=events=frames=0; eventCost.clear(); return;
		}
		if (++frames<wanted) return;
		common->Printf("HUD_PROFILE document=%s frames=%d forward_ms=%.3f forwards=%.1f event_ms=%.3f events=%.1f state_ms=%.3f draw_ms=%.3f unseen_ms=%.3f prepare_ms=%.3f readback_ms=%.3f\n",
			document.c_str(),frames,forwardMs/frames,double(forwards)/frames,eventMs/frames,double(events)/frames,stateMs/frames,drawMs/frames,unseenMs/frames,prepareMs/frames,readbackMs/frames);
		for (const auto& [name,cost]:eventCost) common->Printf("HUD_PROFILE_EVENT document=%s name=%s count=%d ms=%.3f\n",document.c_str(),name.c_str(),cost.first,cost.second);
		forwardMs=eventMs=stateMs=drawMs=unseenMs=prepareMs=readbackMs=0; forwards=events=frames=0; eventCost.clear();
	}
	~Impl() { if (gui) uiManager->DeAlloc(gui); }
	bool ReadTypes(const openq4::ui::DocumentModel& model) {
		// The generator's join events and readbacks ride in the document's
		// extension object, which the loaded model keeps as compact JSON.
		if (!model.extensions.empty()) {
			Json::Value data; std::string error;
			Json::CharReaderBuilder builder;
			std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
			if (!reader->parse(model.extensions.data(),model.extensions.data()+model.extensions.size(),&data,&error) || !data.isObject()) return false;
			const Json::Value& joins=data["openq4"]["joinEvents"];
			if (joins.isArray()) for (const auto& pair:joins) {
				if (pair.isArray() && pair.size()==2 && pair[0].isString() && pair[1].isString())
					joinEvents.emplace_back(pair[0].asString(),pair[1].asString());
			}
			const Json::Value& measured=data["openq4"]["readbacks"];
			if (measured.isArray()) for (const auto& entry:measured) {
				if (!entry.isArray() || (entry.size()!=3 && entry.size()!=5)) continue;
				bool strings=true;
				for (const auto& part:entry) strings=strings && part.isString();
				if (!strings) continue;
				Readback readback{entry[0].asString(),entry[1].asString(),entry[2].asString()};
				if (entry.size()==5) { readback.prefix=entry[3].asString(); readback.count=atoi(entry[4].asString().c_str()); }
				readbacks.push_back(std::move(readback));
			}
		}
		for (const auto& [name,declaration]:model.state) {
			if (declaration.HostSourced()) continue;
			auto& field=types[name]; field.type=declaration.initial.index()==0 ? 'n' : declaration.initial.index()==1 ? 'b' : 's';
			const auto key=Key(name); field.icon=key.size()>=4 && key.ends_with("icon"); field.weapon=key=="weapicon";
		}
		return true;
	}
	// The document, loaded once. A mod's own stock-named HUD keeps its layout
	// and scripts; the loaded model supplies the declared state types and the
	// generator's join events and readbacks, so the source parses once.
	bool Load(const char* source) {
		if (idLegacyHudLayout::ModOwns(source)) { failed=true; return false; }
		if (fileSystem->ReadFile(document.c_str(),nullptr)<0) { Fail("the document is not installed"); return false; }
		gui=uiManager->FindGui(document.c_str(),true,true);
		if (!gui) { Fail("the document did not load"); return false; }
		const auto* model=UI_RetainedDocumentModel(gui);
		if (!model || !ReadTypes(*model)) {
			uiManager->DeAlloc(gui); gui=nullptr;
			Fail("the document's state declarations do not parse"); return false;
		}
		return true;
	}
	// An icon material reaches the document as its base name ("item_machinegun"
	// for gfx/guis/hud/icons/item_machinegun_colored.tga), so one comparison
	// names a glyph whatever spelling the item definition used.
	static std::string IconName(const char* value) {
		std::string name=Key(value);
		const auto slash=name.find_last_of('/');
		if (slash!=std::string::npos) name.erase(0,slash+1);
		const auto dot=name.find_last_of('.');
		if (dot!=std::string::npos) name.erase(dot);
		if (name.size()>8 && name.ends_with("_colored")) name.resize(name.size()-8);
		return name;
	}
	// HUD text reaches the stock GUI with its color and icon escapes. A key
	// escape (^ikHH or ^iKHH, HH the bound key's number in hex) reaches the
	// document whole: the retained state names the key, or gives a prompt's
	// chip its glyph (UserInterfaceRetained.cpp). Every other escape goes.
	static idStr PlainText(const char* value) {
		idStr text=value;
		text.Replace("^ik","\x01k");
		text.Replace("^iK","\x01K");
		text.RemoveEscapes();
		text.Replace("\x01k","^ik");
		text.Replace("\x01K","^iK");
		return text;
	}
	void Forward(const char* name,const char* value) {
		const auto found=types.find(name);
		if (found==types.end() || !value) return;
		auto& field=found->second;
		if (!field.valid || field.raw!=value) {
			if (field.type=='s') field.normalized=field.icon ? IconName(value) : std::string(PlainText(value).c_str());
			else {
				char* end=nullptr;
				const double number=std::strtod(value,&end);
				const bool parsed=end!=value && std::isfinite(number);
				if (field.type=='n') field.normalized=parsed ? va("%.9g",number) : "0";
				else field.normalized=(parsed ? number!=0 : !idStr::Icmp(value,"true")) ? "1" : "0";
			}
			field.raw=value; field.valid=true;
		}
		// Cache conversion, not publication: an event may have changed the
		// retained value since the last identical application write.
		if (field.weapon) gui->SetStateString("oq4.weaponImage",value);
		gui->SetStateString(found->first.c_str(),field.normalized.c_str());
	}
	void Fail(const char* reason) {
		failed=true;
		if (!reported) { common->Warning("Remastered HUD %s: %s; using the stock layout",document.c_str(),reason); reported=true; }
		// The stock layout draws from here on: stop feeding the document.
		if (gui) { uiManager->DeAlloc(gui); gui=nullptr; }
		pending.clear();
	}
	// Decoded peaks of a playing channel, oldest first, from its emitter's
	// own history. Missing, stopped and finished audio stays flat.
	void PublishAudio(const idDict& state,int graph,const char* emitterKey,const char* channelKey,const char* target) {
		// Preset graphs declare no peak fields. Avoid decoding/copying history
		// and forwarding forty unused state writes for those documents.
		AudioGraph& audio=audioGraphs[graph];
		if (!audio.resolved) {
			audio.resolved=true;
			audio.declared=types.contains(std::string(target)+".0");
			if (audio.declared) for (int i=0;i<AudioColumns;++i) audio.keys.push_back(std::string(target)+"."+std::to_string(i));
		}
		if (!audio.declared) return;
		float peaks[AudioColumns]={};
		const int emitterIndex=state.GetInt(emitterKey);
		const int channel=state.GetInt(channelKey);
		idSoundWorld* world=soundSystem->GetSoundWorldFromId(SOUNDWORLD_GAME);
		idSoundEmitter* emitter=world && emitterIndex>0 ? world->EmitterForIndex(emitterIndex) : nullptr;
		if (emitter && channel>SCHANNEL_ANY) emitter->AmplitudeHistory(channel,peaks,AudioColumns);
		// Reduced motion keeps the instrument's level without horizontal travel.
		static idCVar* const reduced=cvarSystem->Find("ui_retainedReducedMotion");
		if (reduced && reduced->GetBool()) for (int i=0;i<AudioColumns-1;++i) peaks[i]=peaks[AudioColumns-1];
		// A silent graph stays as published: forty unchanged writes a frame
		// for each of three graphs bought nothing.
		bool flat=true;
		for (int i=0;i<AudioColumns && flat;++i) flat=!(peaks[i]>0);
		if (flat && audio.flat) return;
		audio.flat=flat;
		for (int i=0;i<AudioColumns;++i) gui->SetStateFloat(audio.keys[i].c_str(),idMath::ClampFloat(0,1,peaks[i]));
	}
};

namespace {
// When the multiplayer document last presented, in presentation time.
int chatPresentedTime=-100000;
}
idRemasteredHud::idRemasteredHud(const char* document) : impl(std::make_unique<Impl>()) {
	impl->document=document;
	impl->chat=!idStr::Icmp(document,"guis/hud/remastered_mp.q4ui");
}
idRemasteredHud::~idRemasteredHud() = default;
bool idRemasteredHud::Failed() const { return impl->failed; }
const char* idRemasteredHud::Document() const { return impl->document.c_str(); }
const char* idRemasteredHud::DocumentFor(const char* source) {
	if (!source) return nullptr;
	const auto key=Key(source);
	for (const auto& route:RemasteredRoutes) if (key==route.source) return route.document;
	return nullptr;
}
bool idRemasteredHud::Enabled() {
	static idCVar* const retained=cvarSystem->Find("ui_retained");
	return retained && retained->GetBool() && ui_hudLayout.GetInteger()!=0;
}
bool idRemasteredHud::PresentsChat() {
	// A second without a frame of it (a load, a failed document, a mod's own
	// HUD) hands chat back to the stock surfaces.
	return Enabled() && common->GetPresentationTime()-chatPresentedTime<1000;
}
void idRemasteredHud::Rejoin() {
	impl->joined=false;
	impl->pending.clear();
	impl->ammoReadout=openq4::ui::HudAmmoReadout();
	impl->readbackValues.clear();
	for (auto& audio:impl->audioGraphs) audio.flat=false;
}
const char* idRemasteredHud::AirDocumentFor(const char* source) {
	if (!source) return nullptr;
	const auto key=Key(source);
	if (key=="guis/hud.gui" || key=="guis/q4xhud.gui") return "guis/hud/air_marine.q4ui";
	if (key=="guis/hud_strogg.gui" || key=="guis/q4xhud_strogg.gui") return "guis/hud/air_strogg.q4ui";
	return nullptr;
}
void idRemasteredHud::Preload(const char* source) {
	if (impl->gui || impl->failed || (session && session->IsLoadingSaveGame())) return;
	impl->Load(source);
}
void idRemasteredHud::ForwardState(const char* name,const char* value) {
	if (!impl->gui || !impl->joined || impl->failed) return;
	if (!ui_hudProfile.GetInteger()) { impl->Forward(name,value); return; }
	const double started=ProfileNow();
	impl->Forward(name,value);
	impl->forwardMs+=ProfileNow()-started; ++impl->forwards;
}
void idRemasteredHud::ForwardDelete(const char* name) {
	if (!impl->gui || !impl->joined || impl->failed) return;
	if (idStr::Icmp(name,"weapicon")==0) impl->gui->DeleteStateVar("oq4.weaponImage");
	if (impl->types.count(name)) impl->gui->DeleteStateVar(name);
}
void idRemasteredHud::ForwardEvent(const char* name) {
	if (impl->failed) return;
	if (!impl->gui || !impl->joined) {
		// The newest 32: a HUD left undrawn for a stretch replays its latest
		// events at the join, not its oldest.
		if (impl->pending.size()>=32) impl->pending.erase(impl->pending.begin());
		impl->pending.emplace_back(name);
		return;
	}
	if (!ui_hudProfile.GetInteger()) { impl->gui->HandleNamedEvent(name); return; }
	const double started=ProfileNow();
	impl->gui->HandleNamedEvent(name);
	const double cost=ProfileNow()-started;
	impl->eventMs+=cost; ++impl->events;
	auto& entry=impl->eventCost[name]; ++entry.first; entry.second+=cost;
}
void idRemasteredHud::ProfileUnseen(double milliseconds) { impl->unseenMs+=milliseconds; impl->Report(); }

bool idRemasteredHud::Draw(const idUserInterfaceLocal& legacy,int time) {
	if (impl->failed) return false;
	const bool profiling=ui_hudProfile.GetInteger()>0;
	const double prepareStarted=profiling ? ProfileNow() : 0;
	if (!impl->gui) {
		// A restore must never create GUIs outside its serialized stream.
		if (session && session->IsLoadingSaveGame()) return false;
		if (!impl->Load(legacy.GetSourceFile())) return false;
	}
	if (!impl->joined) {
		// Join the stock GUI's dictionary as the game left it; later writes and
		// named events reach both, so nothing older than this frame is lost.
		impl->joined=true;
		const idDict& state=legacy.State();
		for (int i=0;i<state.GetNumKeyVals();++i) {
			const idKeyValue* entry=state.GetKeyVal(i);
			impl->Forward(entry->GetKey().c_str(),entry->GetValue().c_str());
		}
		impl->gui->HandleNamedEvent("hudJoin");
		for (const auto& [variable,event]:impl->joinEvents) {
			const bool invert=variable.starts_with('!');
			idStr value;
			if (legacy.GetPresentationValue(variable.c_str()+(invert ? 1 : 0),value) && (atof(value.c_str())!=0)!=invert)
				impl->gui->HandleNamedEvent(event.c_str());
		}
		for (const auto& name:impl->pending) impl->gui->HandleNamedEvent(name.c_str());
		impl->pending.clear();
	}
	// The layout of section 14.4 in canvas dp: the 720 dp high (960 dp wide
	// below 4:3) canvas, its side screens capped by the HUD width setting, the
	// safe inset or the window's safe area on every edge and the independent
	// HUD scale. The canvas fits the safe area, as the document's runtime does.
	openq4::ui::Viewport viewport;
	if (!RetainedUI_DefaultViewport(viewport)) return false;
	viewport.canvasHeight=720; viewport.canvasMinWidth=960; viewport.fitScale=1;
	const float ratio=viewport.DpRatio();
	if (!(ratio>0)) return false;
	static idCVar* const safeInset=cvarSystem->Find("ui_hudSafeInset");
	static idCVar* const hudScale=cvarSystem->Find("ui_hudScale");
	const auto layout=openq4::ui::HudLayoutFor(float(viewport.width),float(viewport.height),ratio,
		{viewport.safeLeft,viewport.safeTop,viewport.safeRight,viewport.safeBottom},ui_hudWidth.GetInteger(),
		safeInset ? safeInset->GetFloat() : 0.f,hudScale ? hudScale->GetFloat() : 1.f);
	float top=layout.top,bottom=layout.bottom;
#if defined(__ANDROID__)
	// Touch plates span the SDL window; the HUD may occupy an inset, scaled
	// drawable viewport. Keep top readouts below the active targets without
	// changing the player's saved safe-area preference.
	const float touchTop=AndroidTouchControls_HudTop()*viewport.pixelDensityY;
	top=Max(top,Max(0.f,touchTop-viewport.originY)/ratio);
	// In portrait the bottom readouts rise above the thumb deck (section 13.6).
	const float deckEdge=AndroidTouchControls_HudBottomEdge()*viewport.pixelDensityY;
	if (deckEdge>0) bottom=Max(bottom,Max(0.f,viewport.originY+float(viewport.height)-deckEdge)/ratio);
#endif
	impl->gui->SetStateFloat("hud.width",layout.width);
	impl->gui->SetStateFloat("hud.height",layout.height);
	impl->gui->SetStateFloat("hud.left",layout.left);
	impl->gui->SetStateFloat("hud.right",layout.right);
	impl->gui->SetStateFloat("hud.top",top);
	impl->gui->SetStateFloat("hud.bottom",bottom);
	impl->gui->SetStateFloat("hud.scale",layout.scale);
	impl->gui->SetStateFloat("hud.centerScale",layout.centerScale);
	const idDict& state=legacy.State();
	if (impl->types.contains("player_totalammo")) {
		using namespace openq4::ui;
		HudAmmoReadout current;
		// Rebuilt only when the weapon changes: the readout compares every frame.
		const char* icon=state.GetString("weapicon"); const char* weapon=state.GetString("player_weapon");
		const size_t iconLength=strlen(icon);
		const std::string& last=impl->ammoReadout.weapon;
		if (last.size()==iconLength+1+strlen(weapon) && !last.compare(0,iconLength,icon) && last[iconLength]==':' &&
			!last.compare(iconLength+1,std::string::npos,weapon)) current.weapon=last;
		else current.weapon=std::string(icon)+":"+weapon;
		current.clip=state.GetInt("player_ammo","-1");
		current.total=state.GetInt("player_totalammo","-1");
		current.clipped=!state.GetBool("mp") && state.GetInt("player_clip_size")>=2 && current.clip>=0;
		const int gains=HudAmmoGains(impl->ammoReadout,current);
		impl->ammoReadout=current;
		if (gains & HudAmmoGainClip) impl->gui->HandleNamedEvent("ammoClipGain");
		if (gains & HudAmmoGainReserve) impl->gui->HandleNamedEvent("ammoReserveGain");
		if (gains & HudAmmoGainTotal) impl->gui->HandleNamedEvent("ammoTotalGain");
	}
	if (state.FindKey("radio_audio_emitter")) impl->PublishAudio(state,0,"radio_audio_emitter","radio_audio_channel","radio.peak");
	if (state.FindKey("voice_self_audio_emitter")) impl->PublishAudio(state,1,"voice_self_audio_emitter","voice_self_audio_channel","voice.self.peak");
	if (state.FindKey("voice_audio_emitter")) impl->PublishAudio(state,2,"voice_audio_emitter","voice_audio_channel","voice.other.peak");
	const double stateStarted=profiling ? ProfileNow() : 0;
	impl->gui->StateChanged(time,false);
	const double drawStarted=profiling ? ProfileNow() : 0;
	impl->gui->Redraw(time);
	const double readbackStarted=profiling ? ProfileNow() : 0;
	if (profiling) {
		impl->prepareMs+=stateStarted-prepareStarted;
		impl->stateMs+=drawStarted-stateStarted;
		impl->drawMs+=readbackStarted-drawStarted;
	}
	idStr drawn;
	if (impl->gui->GetPresentationValue("__retained::drawSucceeded",drawn) && drawn=="1") {
		impl->failures=0;
		if (impl->chat) chatPresentedTime=common->GetPresentationTime();
		// Width/height/visibility readbacks only change when presentation
		// relays out. A scrolling bitmap alone cannot change these metrics.
		idStr layoutChanged;
		if (impl->readbacks.empty() || (impl->gui->GetPresentationValue("__retained::layoutChanged",layoutChanged) && layoutChanged=="0")) {
			if (profiling) impl->readbackMs+=ProfileNow()-readbackStarted;
			return true;
		}
		const auto measure=[&](const std::string& node,const char* field,float& value) {
			idStr text;
			if (!impl->gui->GetPresentationValue(va("%s::@%s",node.c_str(),field),text)) return false;
			value=atof(text); return true;
		};
		for (const auto& [name,node,view,prefix,count]:impl->readbacks) {
			if (view=="@firstInside") {
				float top=0,y=0,height=0;
				if (!measure(node,"y",top)) continue;
				int first=count;
				for (int i=0;i<count;++i) {
					// A node out of the layout keeps its last box: skip it.
					const std::string child=prefix+std::to_string(i);
					idStr display;
					if (impl->gui->GetPresentationValue(va("%s::display",child.c_str()),display) && display=="none") continue;
					if (measure(child,"height",height) && height>0 && measure(child,"y",y) && y>=top-.5f) { first=i; break; }
				}
				const auto last=impl->readbackValues.find(name);
				if (last==impl->readbackValues.end() || last->second!=first) {
					impl->readbackValues[name]=float(first); impl->gui->SetStateFloat(name.c_str(),float(first));
				}
				continue;
			}
			if (view=="@height") {
				idStr height;
				if (!impl->gui->GetPresentationValue(va("%s::@height",node.c_str()),height)) continue;
				const float value=atof(height);
				const auto last=impl->readbackValues.find(name);
				if (value>0 && (last==impl->readbackValues.end() || idMath::Fabs(last->second-value)>.25f)) {
					impl->readbackValues[name]=value; impl->gui->SetStateFloat(name.c_str(),value);
				}
				continue;
			}
			idStr nodeWidth,viewWidth;
			const bool wider=impl->gui->GetPresentationValue(va("%s::@width",node.c_str()),nodeWidth) &&
				impl->gui->GetPresentationValue(va("%s::@width",view.c_str()),viewWidth) && atof(nodeWidth)>atof(viewWidth)+.5;
			auto& last=impl->readbackValues[name];
			if ((last!=0)!=wider) { last=wider ? 1.f : 0.f; impl->gui->SetStateBool(name.c_str(),wider); }
		}
		if (profiling) impl->readbackMs+=ProfileNow()-readbackStarted;
		return true;
	}
	// A frame that cannot present falls back at once; a persistent failure
	// stops retrying until the gate, layout or document changes.
	if (++impl->failures>=30) impl->Fail("the document failed to draw");
	return false;
}
#else
struct idRetainedHud::Impl {};
idRetainedHud::idRetainedHud() = default;
idRetainedHud::~idRetainedHud() = default;
void idRetainedHud::Reset() {}
bool idRetainedHud::DrawSucceeded() const { return false; }
void idRetainedHud::Precache() {}
bool idRetainedHud::Eligible(const char*) { return false; }
bool idRetainedHud::Covers(const char*) { return false; }
bool idRetainedHud::Usable() const { return false; }
bool idRetainedHud::Draw(const char*,std::vector<openq4::ui::HudPrimitive>&&) { return false; }
struct idRemasteredHud::Impl {};
idRemasteredHud::idRemasteredHud(const char*) {}
idRemasteredHud::~idRemasteredHud() = default;
const char* idRemasteredHud::DocumentFor(const char*) { return nullptr; }
const char* idRemasteredHud::AirDocumentFor(const char*) { return nullptr; }
bool idRemasteredHud::Enabled() { return false; }
bool idRemasteredHud::PresentsChat() { return false; }
void idRemasteredHud::Rejoin() {}
bool idRemasteredHud::Draw(const idUserInterfaceLocal&,int) { return false; }
void idRemasteredHud::Preload(const char*) {}
void idRemasteredHud::ForwardState(const char*,const char*) {}
void idRemasteredHud::ForwardDelete(const char*) {}
void idRemasteredHud::ForwardEvent(const char*) {}
void idRemasteredHud::ProfileUnseen(double) {}
bool idRemasteredHud::Failed() const { return true; }
const char* idRemasteredHud::Document() const { return ""; }
#endif

// The stock interface's Remastered layout (idLegacyHudLayout). The cvars are
// found by name, once: the dedicated server compiles none of the HUD's own.
namespace {
struct NamedCVar {
	const char* name; idCVar* cvar=nullptr; bool sought=false;
	idCVar* Get() { if (!sought) { sought=true; cvar=cvarSystem->Find(name); } return cvar; }
	int Integer(int fallback=0) { idCVar* found=Get(); return found ? found->GetInteger() : fallback; }
	float Float(float fallback=0) { idCVar* found=Get(); return found ? found->GetFloat() : fallback; }
	bool Bool(bool fallback=false) { idCVar* found=Get(); return found ? found->GetBool() : fallback; }
};
NamedCVar hudLayoutVar{"ui_hudLayout"},hudWidthVar{"ui_hudWidth"},hudInsetVar{"ui_hudSafeInset"},hudScaleVar{"ui_hudScale"},
	hudOpacityVar{"ui_hudOpacity"},hudPickupsVar{"ui_hudPickups"},hudStripVar{"ui_hudWeaponStrip"},
	hudDamageVar{"ui_hudDamageDirection"},hudReducedVar{"ui_retainedReducedMotion"},retainedVar{"ui_retained"};
bool SourceIs(const char* source,std::initializer_list<const char*> names) {
	if (!source) return false;
	idStr key=source; key.BackSlashesToSlashes();
	for (const char* name:names) if (!key.Icmp(name)) return true;
	return false;
}
}

bool idLegacyHudLayout::Covers(const char* source) {
	return idRemasteredHud::DocumentFor(source)!=nullptr;
}

bool idLegacyHudLayout::Fades(const char* source) {
	return Covers(source) && !SourceIs(source,{"guis/wristcomm.gui","guis/wristcomm_strogg.gui","guis/scoreboard.gui"});
}

namespace {
// The Awakening's HUDs and openQ4's definitions of them (tools/ui/build_awakening_legacy_hud.py):
// on foot, the dropship's guns, the MCC's side gun and the speeder bike.
struct HudDefinition { const char* source; const char* definition; };
constexpr HudDefinition AwakeningDefinitions[]={
	{"guis/q4xhud.gui","guis/openq4/awakening/q4xhud.gui"},
	{"guis/vehicles/airassault/turretplayerhud.gui","guis/openq4/awakening/turretplayerhud.gui"},
	{"guis/vehicles/mcc/cannonplayerhud.gui","guis/openq4/awakening/cannonplayerhud.gui"},
	{"guis/vehicles/speederplayerhud.gui","guis/openq4/awakening/speederplayerhud.gui"},
};
}

bool idLegacyHudLayout::Scales(const char* source) {
	if (SourceIs(source,{"guis/hud.gui","guis/hud_strogg.gui","guis/mphud.gui","guis/vehicles/hud.gui"})) return true;
	// By definition: the expansion's own scripts name no scale anchors.
	for (const HudDefinition& entry:AwakeningDefinitions) if (SourceIs(source,{entry.definition})) return true;
	return false;
}

const char* idLegacyHudLayout::DefinitionFor(const char* source) {
	for (const HudDefinition& entry:AwakeningDefinitions) {
		if (!SourceIs(source,{entry.source})) continue;
		// Only beside the expansion's own HUD, whose content the definition draws.
		if (ModOwns(source) || fileSystem->ReadFile(source,nullptr)<0 || fileSystem->ReadFile(entry.definition,nullptr)<0) return source;
		return entry.definition;
	}
	return source;
}

bool idLegacyHudLayout::ModOwns(const char* source) {
	if (!source || !source[0]) return false;
	for (const char* cvar:{"fs_game","fs_game_base"}) {
		const char* mod=cvarSystem->GetCVarString(cvar);
		if (!mod[0] || !idStr::Icmp(mod,"baseoq4") || !idStr::Icmp(mod,"q4base") || !idStr::Icmp(mod,"q4xbase")) continue;
		idFile* file=fileSystem->OpenFileRead(source,false,mod);
		if (file) { fileSystem->CloseFile(file); return true; }
	}
	return false;
}

int idLegacyHudLayout::Layout() {
	return idMath::ClampInt(0,3,hudLayoutVar.Integer(1));
}

idLegacyHudLayout::Settings idLegacyHudLayout::Read(int layout) {
	Settings settings;
	settings.layout=layout;
	if (layout==0) return settings;
	const bool minimal=layout==2;
	// The HUD settings (13.9) and Minimal trim Remastered's groups.
	settings.pickups=!minimal && hudPickupsVar.Bool(true);
	settings.strip=!minimal && hudStripVar.Bool(true);
	settings.damage=!minimal && hudDamageVar.Bool(true);
	settings.reducedMotion=hudReducedVar.Bool(false);
	settings.scale=idMath::ClampFloat(0.5f,2.0f,hudScaleVar.Float(1.0f));
	settings.opacity=idMath::ClampFloat(0.25f,1.0f,hudOpacityVar.Float(1.0f));
	return settings;
}

void idLegacyHudLayout::Publish(idUserInterfaceLocal& gui,const Settings& settings) {
	const bool remastered=settings.layout!=0,minimal=settings.layout==2;
	gui.SetStateInt("oq4_hudLayout",settings.layout);
	gui.SetStateBool("oq4_hudRemastered",remastered);
	gui.SetStateBool("oq4_hudMinimal",minimal);
	// Classic keeps every stock group.
	gui.SetStateBool("oq4_hudShowExtras",!minimal);
	gui.SetStateBool("oq4_hudShowPickups",settings.pickups);
	gui.SetStateBool("oq4_hudShowStrip",settings.strip);
	gui.SetStateBool("oq4_hudShowDamage",settings.damage);
	// Reduced motion (13.10) holds the looping motion a script starts itself.
	gui.SetStateBool("oq4_hudReducedMotion",settings.reducedMotion);
	gui.SetStateFloat("oq4_hudScale",settings.scale);
}

void idLegacyHudLayout::Edges(float xExpand,float yExpand,float edges[4],float scale,float* centerScale) {
	// The view as the Remastered documents' dp (1.5 dp a unit on the 720 dp
	// high, at least 960 dp wide canvas), so both interfaces share one rule.
	const auto layout=openq4::ui::HudLayoutFor((640+2*xExpand)*1.5f,(480+2*yExpand)*1.5f,1.f,{0,0,0,0},
		hudWidthVar.Integer(1),hudInsetVar.Float(0),scale);
	edges[0]=xExpand-layout.left/1.5f;
	edges[1]=yExpand-layout.top/1.5f;
	edges[2]=xExpand-layout.right/1.5f;
	edges[3]=yExpand-layout.bottom/1.5f;
	if (centerScale) *centerScale=layout.centerScale;
}

const idUserInterfaceLocal::HudRoute& idUserInterfaceLocal::ResolveHudRoute() {
	if (!hudRoute.resolved) {
		hudRoute=HudRoute();
		hudRoute.resolved=true;
		hudRoute.document=idRemasteredHud::DocumentFor(source.c_str());
		hudRoute.air=idRemasteredHud::AirDocumentFor(source.c_str());
		hudRoute.coverage=idRetainedHud::Covers(source.c_str());
		if (hudRoute.document || hudRoute.coverage) hudRoute.modOwned=idLegacyHudLayout::ModOwns(source.c_str());
		hudRoute.fades=hudRoute.document && idLegacyHudLayout::Fades(source.c_str());
		hudRoute.scales=hudRoute.document && idLegacyHudLayout::Scales(definition.c_str());
	}
	return hudRoute;
}

void idUserInterfaceLocal::PublishLegacyHud(float xExpand,float yExpand) {
	const HudRoute& route=ResolveHudRoute();
	legacyEdges=false; classicCanvas=false; legacyScale=legacyCenterScale=legacyOpacity=1.0f;
	if (!route.document) return;
	// A mod's own copy keeps its layout: Classic, on the stock anchors.
	const idLegacyHudLayout::Settings settings=idLegacyHudLayout::Read(route.modOwned ? 0 : idLegacyHudLayout::Layout());
	// The retained interface's Classic keeps the centered 4:3 canvas.
	classicCanvas=settings.layout==0 && !route.modOwned && retainedVar.Bool(false);
	float edges[4]={0,0,0,0},centerScale=1.0f;
	// Classic keeps the stock anchors (the retained Classic its 4:3 canvas).
	if (settings.layout>0) {
		idLegacyHudLayout::Edges(xExpand,yExpand,edges,settings.scale,&centerScale);
		for (int i=0;i<4;++i) legacyEdge[i]=edges[i];
		legacyEdges=true;
		if (route.scales) { legacyScale=settings.scale; legacyCenterScale=centerScale; }
		if (route.fades) legacyOpacity=settings.opacity;
	}
	// The stock windows read these every frame; the dictionary takes them
	// only when one changes (each was eleven lookups and forwards a frame).
	const float published[11]={float(settings.layout),float(settings.pickups),float(settings.strip),float(settings.damage),
		float(settings.reducedMotion),settings.scale,settings.opacity,edges[0],edges[1],edges[2],edges[3]};
	if (legacyPublishedValid && !memcmp(published,legacyPublished,sizeof(published))) return;
	legacyPublishedValid=true;
	memcpy(legacyPublished,published,sizeof(published));
	idLegacyHudLayout::Publish(*this,settings);
	// The same edges for windows the stock anchors at the centre, which
	// Remastered moves to an edge (a vehicle's prompt and status bar).
	static const char* const EdgeKeys[4]={"oq4_hudEdgeLeft","oq4_hudEdgeTop","oq4_hudEdgeRight","oq4_hudEdgeBottom"};
	for (int i=0;i<4;++i) SetStateFloat(EdgeKeys[i],edges[i]);
}

void idUserInterfaceLocal::ApplyLegacyGroup(idDeviceContext& dc) const {
	// "hudanchorx none": a layer outside the HUD's groups keeps its size.
	if (legacyGroupX==3 || legacyGroupY==3) { dc.SetGroupScale(0.0f,0.0f,1.0f); return; }
	const float x=legacyGroupX==1 ? -legacyEdge[0] : legacyGroupX==2 ? 640.0f+legacyEdge[2] : 320.0f;
	const float y=legacyGroupY==1 ? -legacyEdge[1] : legacyGroupY==2 ? 480.0f+legacyEdge[3] : 240.0f;
	// The centre column on the bottom edge (the status bar, the strip and the
	// pickup lines) takes the scale a narrow view leaves room for.
	dc.SetGroupScale(x,y,legacyGroupX==0 && legacyGroupY==2 ? legacyCenterScale : legacyScale);
}

idLegacyHudGroup::idLegacyHudGroup(idUserInterfaceLocal* gui,idDeviceContext* dc,int anchorX,int anchorY)
	: gui(gui),dc(dc),active(gui!=nullptr && dc!=nullptr && gui->legacyScale!=1.0f) {
	if (!active) return;
	oldX=gui->legacyGroupX; oldY=gui->legacyGroupY;
	dc->GetGroupScale(oldAnchorX,oldAnchorY,oldScale);
	if (anchorX>=0) gui->legacyGroupX=anchorX;
	if (anchorY>=0) gui->legacyGroupY=anchorY;
	gui->ApplyLegacyGroup(*dc);
}

idLegacyHudGroup::~idLegacyHudGroup() {
	if (!active) return;
	gui->legacyGroupX=oldX; gui->legacyGroupY=oldY;
	dc->SetGroupScale(oldAnchorX,oldAnchorY,oldScale);
}

bool idUserInterfaceLocal::LegacyEdges(float edges[4]) const {
	if (!legacyEdges) return false;
	for (int i=0;i<4;++i) edges[i]=legacyEdge[i];
	return true;
}

float idUserInterfaceLocal::MeasureWindowText(const char* windowName,const char* text) const {
	if (!desktop || !text || !text[0]) return 0;
	drawWin_t* found=desktop->FindChildByName(windowName);
	if (!found) return 0;
	int font=0; float scale=0;
	if (found->win) { font=found->win->fontNum; scale=found->win->textScale; }
	else if (found->simp) { font=found->simp->fontNum; scale=found->simp->textScale; }
	else return 0;
	uiManagerLocal.dc.SetFont(font);
	return static_cast<float>(uiManagerLocal.dc.TextWidth(text,scale,0));
}

// itemPickup in the Remastered layout (14.8): up to two lines, the newest
// nearest the gauges, and a repeat of the newest line's item while it shows
// merged into it with a count. The scripts show and time the lines; the
// engine compares the names and measures each line for its fitted plate.
void idUserInterfaceLocal::PublishLegacyPickup() {
	if (!ResolveHudRoute().document) return;
	const idStr text=state.GetString("itemtext");
	const idStr icon=state.GetString("itemicon");
	const auto live=[&](const LegacyPickupLine& line) {
		return line.used && time>=line.start && time-line.start<idLegacyHudLayout::PickupLife;
	};
	int slot=legacyPickupNewest;
	if (live(legacyPickups[slot]) && legacyPickups[slot].text==text) {
		++legacyPickups[slot].count;
	} else {
		slot=1-slot;
		legacyPickupNewest=slot;
		legacyPickups[slot].text=text;
		legacyPickups[slot].count=1;
		legacyPickups[slot].used=true;
	}
	LegacyPickupLine& line=legacyPickups[slot];
	line.start=time;
	idStr shown=line.text;
	if (line.count>1) shown+=va(" %s%d",common->GetLanguageDict()->GetString("#str_231060"),line.count);
	SetStateString(va("oq4_pick%d_text",slot),shown.c_str());
	SetStateString(va("oq4_pick%d_icon",slot),icon.c_str());
	SetStateFloat(va("oq4_pick%d_width",slot),MeasureWindowText(va("pickup%d_text",slot),shown.c_str()));
	SetStateInt("oq4_pick_newest",slot);
}

bool idUserInterfaceLocal::DrawRemasteredHud(int drawTime) {
	const char* document=ResolveHudRoute().document;
	if (!document || !idRemasteredHud::Enabled()) { delete remastered; remastered=nullptr; return false; }
	delete essentialHud; essentialHud=nullptr;
	if (!remastered) remastered=new idRemasteredHud(document);
	if (remastered->Failed() || !remastered->Draw(*this,drawTime)) return false;
	// The stock windows keep evaluating unseen, so their scripts, timers and
	// authored presentation variables stay where Classic would find them.
	// RunTimeEvents visits every child and updates expressions/transitions.
	// Layout and paint belong to the replacement; Classic recomputes them on
	// its next Redraw if the gate changes or the replacement fails.
	static idCVar* const profile=cvarSystem->Find("ui_hudProfile");
	const bool profiling=profile && profile->GetInteger()>0;
	const auto started=profiling ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
	if (r_skipGuiShaders.GetInteger()!=1 && r_skipGuiShaders.GetInteger()!=3) RunTimeEvents(drawTime);
	remastered->ProfileUnseen(profiling ? std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count() : 0);
	return true;
}

void idUserInterfaceLocal::DrawEssentialHud(int drawTime) {
	const char* document=ResolveHudRoute().air;
	if (!document || idRemasteredHud::Enabled()) { delete essentialHud; essentialHud=nullptr; return; }
	if (state.GetInt("player_air","100")>=100 || state.GetInt("player_health")<=0) return;
	if (!essentialHud) essentialHud=new idRemasteredHud(document);
	// Overlay only: its failure never suppresses the stock desktop.
	if (!essentialHud->Failed()) essentialHud->Draw(*this,drawTime);
}

bool idUserInterfaceLocal::RemasteredActive() const {
	// Answers before the first draw too, so a caller never pairs the stock
	// gauges with the Remastered layout for a frame.
	if (!idRemasteredHud::Enabled() || (remastered && remastered->Failed())) return false;
	return hudRoute.resolved ? hudRoute.document!=nullptr : idRemasteredHud::DocumentFor(source.c_str())!=nullptr;
}

bool idUserInterfaceLocal::DrawRetainedHud() {
	if (!ResolveHudRoute().coverage || !retainedVar.Bool(false)) {
		delete retainedHud; retainedHud=nullptr; return false;
	}
	if (!retainedHud) retainedHud=new idRetainedHud;
	// A mod's own GUI, or a missing catalog, never draws through the bridge:
	// capturing its whole frame only to discard it doubled the HUD's draw.
	if (!retainedHud->Usable()) return false;
	std::vector<openq4::ui::HudPrimitive> items;
	uiManagerLocal.dc.BeginHudCapture(items);
	uiManagerLocal.dc.PushClipRect(uiManagerLocal.screenRect);
	desktop->Redraw(0,0);
	uiManagerLocal.dc.PopClipRect();
	uiManagerLocal.dc.EndHudCapture();
	// RunTimeEvents is time-guarded: fallback redraw cannot replay script actions.
	return retainedHud->Draw(source.c_str(),std::move(items));
}

void idUserInterfaceManagerLocal::ClearHudPrimitives() {
	generatedHuds.DeleteContents(true); generatedHudNames.Clear();
}

void idUserInterfaceManagerLocal::BuildHudText(const char* text,float scale,float width,float height,
	idList<uiHudPrimitive_t>& output) {
	output.Clear();
	if (!text || !text[0] || scale <= 0 || width <= 4 || height <= 4) return;
	std::vector<openq4::ui::HudPrimitive> glyphs;
	CaptureLegacyText(text,"fonts/lowpixel",scale,width,height,1,true,glyphs);
	for (const auto& glyph : glyphs) {
		uiHudPrimitive_t item;
		for (int n=0;n<4;++n) item.points[n].Set(glyph.quad[2*n],glyph.quad[2*n+1]);
		item.uv.Set(glyph.uv[0],glyph.uv[1],glyph.uv[2],glyph.uv[3]);
		item.color=colorWhite; item.glyph=true;
		item.material=declManager->FindMaterial(glyph.material.c_str());
		output.Append(item);
	}
}

bool idUserInterfaceManagerLocal::DrawHudPrimitives(const char* source,const idList<uiHudPrimitive_t>& packet) {
	if (!cvarSystem->GetCVarBool("ui_retained")) { ClearHudPrimitives(); return false; }
	if (!source || packet.Num()>32768) return false;
	int index=generatedHudNames.FindIndex(source);
	if (index<0) {
		if (generatedHuds.Num()>=16) return false;
		index=generatedHudNames.Append(source); generatedHuds.Append(new idRetainedHud);
	}
	std::vector<openq4::ui::HudPrimitive> items;
	items.reserve(packet.Num());
	for (int i=0;i<packet.Num();++i) {
		const auto& p=packet[i]; if (!p.material) return false;
		openq4::ui::HudPrimitive item;item.material=p.material->GetName();item.glyph=p.glyph;
		for (int n=0;n<4;++n) {
			item.quad[2*n]=p.points[n].x;item.quad[2*n+1]=p.points[n].y;
			item.colour[n]=p.color[n];item.uv[n]=p.uv[n];
		}
		items.push_back(std::move(item));
	}
	return generatedHuds[index]->Draw(source,std::move(items));
}

void idUserInterfaceManagerLocal::HudTest(const idCmdArgs& args) {
	if (args.Argc()<3) { common->Printf("ui_hudTest <source.gui> report | event <name> | state <key> <value> | get <key> | presentation <alias> <value>\n"); return; }
	int count=0;
	for (int i=0;i<allocations.Num();++i) {
		auto* gui=allocations[i]; if (idStr::Icmp(gui->Name(),args.Argv(1))) continue;
		if (!idStr::Icmp(args.Argv(2),"report")) {
			idStr value; gui->GetPresentationValue("__retained::hudDrawSucceeded",value);
			common->Printf("HUD_REPORT source=%s drawn=%s\n",gui->Name(),value.c_str());
		} else if (args.Argc()==4 && !idStr::Icmp(args.Argv(2),"event")) gui->HandleNamedEvent(args.Argv(3));
		else if (args.Argc()==5 && !idStr::Icmp(args.Argv(2),"state")) gui->SetStateString(args.Argv(3),args.Argv(4));
		else if (args.Argc()==4 && !idStr::Icmp(args.Argv(2),"get")) {
			common->Printf("HUD_STATE source=%s key=%s value=%s\n",gui->Name(),args.Argv(3),gui->State().GetString(args.Argv(3)));
			++count; continue;
		}
		else if (args.Argc()==5 && !idStr::Icmp(args.Argv(2),"presentation")) {
			if (!gui->SetPresentationValue(args.Argv(3),args.Argv(4))) common->Warning("HUD probe: unknown presentation alias %s",args.Argv(3));
		} else continue;
		// The GUI's own clock: a stock HUD runs on game time, and the
		// presentation clock would start its timelines far from it.
		const auto* local=dynamic_cast<idUserInterfaceLocal*>(gui);
		gui->StateChanged(local ? local->GetTime() : common->GetPresentationTime()); ++count;
	}
	if (!count) common->Warning("HUD probe: no matching GUI or operation: %s",args.Argv(1));
}
