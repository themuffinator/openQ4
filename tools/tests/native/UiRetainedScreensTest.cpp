// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// Retained screens: image nodes, path blends and view-height canvases in the
// canonical schema and runtime, then the production title, pause and loading
// documents (content/baseoq4/pak0/guis) loaded, laid out and driven by state.
#include "src/ui/retained/Runtime.h"
#include "src/ui/retained/PlayerSetupRequest.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iterator>
#include <tuple>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace openq4::ui;
static int checks = 0;
static void Check(bool condition, const char* message) {
	++checks;
	if (!condition) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
static bool Near(float a, float b, float tolerance = .75f) { return std::abs(a-b) <= tolerance; }

struct ScreenHost final : Host {
	struct Submission { std::vector<Vertex> vertices; std::uintptr_t material = 0; };
	std::vector<Submission> draws;
	std::vector<std::string> materials;
	std::set<std::string> missingMaterials;
	std::uintptr_t multiply = 7;
	bool reducedMotion = false;
	bool cacheLayers = false;
	bool CanCacheLayers() const override { return cacheLayers; }
	int errors = 0;
	std::uint32_t activeLayer = 0;
	std::uint64_t frame = 1;
	bool ReadFile(const std::string&, std::string&) override { return false; }
	std::string Translate(const std::string& text) override { return text.starts_with("#str_") ? "Label" : text; }
	// The engine reports soft focus through ui_retainedSoftFocus and draws it
	// through SoftenBackdrop; without it the runtime leaves the backdrop.
	bool softFocus = false;
	bool opaqueBacking = false;
	struct Softened { float sigma = 0, saturation = 1; Bounds region; };
	std::vector<Softened> softened;
	std::string language = "english";
	// The player settings the multiplayer card's Settings and Voice pages read back.
	std::map<std::string,StateValue> cvars = {{"ui_handicap",100.0},{"s_voiceVolume",1.0},{"s_micInputLevel",5.0},
		{"s_voiceChatCapture",false},{"s_voiceChatTest",false},{"s_voiceChatStatus",1.0},{"s_voiceChatLevel",0.0},
		{"s_voiceChatSend",true},{"s_voiceChatReceive",true},{"s_voiceChatEcho",false},{"cl_player_outline_width",std::string("2.0")},
		{"cl_player_outline_enemy",std::string("0")},{"cl_player_outline_team",std::string("0")},{"cl_player_rimlight_enemy",std::string("0")},
		{"cl_player_rimlight_team",std::string("0")},{"cl_player_brightskin_enemy",std::string("0")},{"cl_player_brightskin_team",std::string("0")},
		{"cl_player_visibility_enemy_color",std::string("1 0.12 0.05")},{"cl_player_visibility_team_color",std::string("1 0.12 0.05")},
		{"cl_player_brightskin_enemy_color",std::string("1 0.05 0.02")},{"cl_player_brightskin_team_color",std::string("1 0.05 0.02")},
		// The HUD settings (section 13.9) at their defaults.
		{"ui_hudOpacity",1.0},{"ui_hudDamageDirection",true},{"ui_hudPickups",true},{"ui_hudWeaponStrip",true},
		{"ui_reducedFlashing",false}};
	bool ReadCVar(const std::string& name, size_t type, StateValue& value) override {
		if (type == 2 && name == "sys_lang") { value = language; return true; }
		if (const auto found = cvars.find(name); found != cvars.end()) {
			if (found->second.index() != type) return false;
			value = found->second; return true;
		}
		if (type != 1) return false;
		if (name == "ui_retainedReducedMotion") { value = reducedMotion; return true; }
		if (name == "ui_retainedSoftFocus") { value = softFocus; return true; }
		if (name == "ui_retainedOpaqueBacking") { value = opaqueBacking; return true; }
		return false;
	}
	bool SoftenBackdrop(std::uint32_t destination, float sigma, float saturation, const Bounds& region) override {
		Check(destination != 0,"the soft focus is drawn into a pushed layer");
		softened.push_back({sigma,saturation,region}); return softFocus;
	}
	std::vector<float> streaks;
	bool StreakBackdrop(std::uint32_t destination, float strength, const Bounds&) override {
		Check(destination != 0,"the speed streak is drawn into a pushed layer");
		streaks.push_back(strength); return softFocus;
	}
	void Log(bool error, const std::string& message) override {
		if (error) { ++errors; std::fprintf(stderr,"retained: %s\n",message.c_str()); }
	}
	std::uintptr_t LoadMaterial(const std::string& name, int& width, int& height) override {
		materials.push_back(name);
		if (missingMaterials.contains(name)) { width=height=0; return 0; }
		// Fonts and pictures: a 4:3 picture, so cover and contain differ at 16:9.
		width = 640; height = 480;
		return 100+materials.size();
	}
	std::uintptr_t MultiplyMaterial() override { return multiply; }
	// A model preview (Player Setup's) renders into a stand-in picture.
	bool RenderModelScene(std::uint64_t, const ModelScene&, int, int, double, std::string& image, std::string&) override {
		image = "model-preview"; return true;
	}
	bool recordDraws = true;   // the pointer probe reads no submissions
	void Draw(const std::vector<Vertex>& vertices, const std::vector<int>& indices, std::uintptr_t material) override {
		Check(indices.size()%3 == 0,"triangle topology");
		for (int index : indices) Check(index >= 0 && static_cast<size_t>(index) < vertices.size(),"valid indices");
		if (recordDraws) draws.push_back({vertices,material});
	}
	std::uint64_t RenderFrame() const override { return frame; }
	std::vector<float> compositeOpacities;
	bool BeginLayer(std::uint32_t id, int, int) override { activeLayer = id; return true; }
	void CompositeLayer(std::uint32_t, std::uint32_t destination, float opacity, const Bounds&) override {
		compositeOpacities.push_back(opacity); activeLayer = destination;
	}
	void MaskLayer(std::uint32_t, std::uint32_t destination, const Bounds&) override { activeLayer = destination; }
	void EndLayer(std::uint32_t restore) override { activeLayer = restore; }
	struct LegacyRequest { std::string text,family; float scale,width,height; int align; bool clip; };
    std::vector<LegacyRequest> legacyRequests;
    bool LayoutLegacyText(const std::string& text,const std::string& family,float scale,float width,float height,int align,bool clip,
        std::vector<HudPrimitive>& output) override {
        legacyRequests.push_back({text,family,scale,width,height,align,clip});
        for (size_t i=0;i<text.size();++i) {
            HudPrimitive item; item.material="test-font"; item.glyph=true;
            const float x=width-float(text.size()-i)*10;
            item.quad={x,2,x+10,2,x+10,22,x,22}; output.push_back(item);
        }
        return true;
    }
    FontMetrics GetFontMetrics(const std::string&, int size) override { return {size*.8f,size*.2f,size*1.2f,size*.5f}; }
	std::set<std::uint32_t> glyphs;   // every code point the runtime asked to draw or measure
	Glyph GetGlyph(const std::string&, int size, std::uint32_t codepoint) override {
		glyphs.insert(codepoint);
		return {size*.6f,0,-size*.8f,size*.6f,static_cast<float>(size),0,0,1,1,"test-font"};
	}
};

static std::string Read(const char* path) {
	std::ifstream file(path, std::ios::binary);
	Check(file.good(),"read a production retained document");
	return std::string(std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>());
}

static bool Loads(const std::string& source) {
	Document document; std::vector<Diagnostic> diagnostics;
	return document.Load(source,diagnostics);
}

static std::string Wrap(const std::string& root, const std::string& extra = "") {
	return R"({"format":"openq4-ui","version":1,"id":"screens-schema")"+extra+R"(,"root":)"+root+"}";
}

static void CheckSchema() {
	const std::string picture = R"({"id":"root","type":"group","children":[{"id":"shot","type":"image","properties":{"image":{"type":"image","value":"gfx/guis/loadscreens/generic"},"image-fit":{"type":"keyword","value":"cover"},"image-blend":{"type":"keyword","value":"additive"},"image-color":{"type":"color","value":[1,1,1,0.5]},"width":{"type":"length","value":100,"unit":"%"}}}]})";
	Document document; std::vector<Diagnostic> diagnostics;
	Check(document.Load(Wrap(picture,R"(,"canvas":{"height":720})"),diagnostics),"image node, fit, blend and tint compile");
	Check(document.Model().canvasHeight == 720,"view-height canvas recorded");
	Check(Loads(Wrap(picture,R"(,"canvas":{"height":720,"minWidth":960})")) &&
		!Loads(Wrap(picture,R"(,"canvas":{"height":720,"minWidth":100})")),"optional minimum canvas width validates and is bounded");
	const auto markup = document.BuildMarkup();
	Check(markup.find("decorator:image(material:q4-add/gfx/guis/loadscreens/generic none cover center center)") != std::string::npos,
		"additive image decorator resolves from the VFS root with its fit and alignment");
	Check(markup.find("image-fit") == std::string::npos && markup.find("image-blend") == std::string::npos,"fit and blend fold into the decorator");
	Check(markup.find("image-color:rgba(255,255,255,128)") != std::string::npos,"image tint reaches the layout");
	for (const char* source : {"../gfx/x","/gfx/x","gfx//x","gfx/x/","gfx/x y","C:gfx","gfx\\\\x",".hidden"}) {
		std::string bad = picture; const auto at = bad.find("gfx/guis/loadscreens/generic");
		bad.replace(at,28,source);
		Check(!Loads(Wrap(bad)),"image sources stay relative VFS names");
	}
	Check(ValidImageSource("") && ValidImageSource("guis/assets/generated/loadscreens/airdefense1_1920x1080.tga"),"empty and generated levelshot sources are valid");
	Check(!Loads(Wrap(R"({"id":"root","type":"group","properties":{"image":{"type":"image","value":"gfx/x"}}})")),"image properties require an image node");
	Check(!Loads(Wrap(R"({"id":"root","type":"image","properties":{"width":{"type":"length","value":10,"unit":"dp"}}})")),"an image node requires its source property");
	Check(!Loads(Wrap(R"({"id":"root","type":"image","properties":{"image":{"type":"image","value":"x"},"image-fit":{"type":"keyword","value":"stretch"}}})")),"unknown fit rejected");
	Check(!Loads(Wrap(picture,R"(,"canvas":{"height":100})")) && !Loads(Wrap(picture,R"(,"canvas":{"height":720,"width":1280})")),"canvas height bounded, no other fields");
	const auto vector = [](const std::string& blend, bool mask) {
		const std::string shape = R"({"id":"p","commands":[{"id":"a","op":"move","points":[[0,0]]},{"id":"b","op":"line","points":[[10,0]]},{"id":"c","op":"line","points":[[10,10]]}],"fill":{"type":"solid","color":{"type":"color","value":[1,1,1,1]}},"blend":")"+blend+R"("})";
		return Wrap(mask ? R"({"id":"root","type":"group","mask":{"paths":[)"+shape+R"(]}})" : R"({"id":"root","type":"vector","paths":[)"+shape+R"(]})");
	};
	Check(Loads(vector("additive",false)) && Loads(vector("multiply",false)) && Loads(vector("normal",false)),"path blends compile");
	Check(!Loads(vector("screen",false)),"unknown blend rejected");
	Check(!Loads(vector("additive",true)) && Loads(vector("normal",true)),"mask coverage cannot blend");
	const std::string bound = R"({"id":"root","type":"group","children":[{"id":"shot","type":"image","properties":{"image":{"type":"image","value":""}}}]})";
	const std::string state = R"(,"state":{"shot":{"type":"string","initial":""}})";
	Check(Loads(Wrap(bound,state+R"(,"bindings":[{"id":"b","node":"shot","property":"image","value":{"state":"shot"}}])")),"image source binds application strings");
	Check(!Loads(Wrap(bound,state+R"(,"bindings":[{"id":"b","node":"shot","property":"image","value":"../escape"}])")),"literal image bindings are checked");
	Check(!Loads(Wrap(bound,R"(,"timelines":[{"id":"t","durationMs":10,"tracks":[{"node":"shot","property":"image","keys":[{"atMs":0,"value":{"type":"image","value":"a"}},{"atMs":10,"value":{"type":"image","value":"b"}}]}]}])")),
		"image sources are discrete, never animated");
	Viewport viewport; viewport.width = 1920; viewport.height = 1080; viewport.displayScale = 2; viewport.userScale = 1.5f;
	viewport.canvasHeight = 720;
	Check(Near(viewport.DpRatio(),1.5f,.001f),"a view-height canvas replaces density and user scale");
	viewport.canvasMinWidth = 960;
	viewport.width = 720; viewport.height = 1280;
	Check(Near(viewport.DpRatio(),.75f,.001f),"portrait fits the center screen's width without stretching");
	viewport.width = 2560; viewport.height = 1080;
	Check(Near(viewport.DpRatio(),1.5f,.001f),"wide views keep the center screen's height");
	// The window's safe area (section 13.3): the canvas fits what the insets
	// leave, and a malformed inset cannot collapse it.
	viewport.canvasHeight = 720; viewport.width = 1920; viewport.height = 1080; viewport.safeBottom = 60; viewport.safeLeft = 90;
	Check(Near(viewport.SafeWidth(),1830,.001f) && Near(viewport.SafeHeight(),1020,.001f) && Near(viewport.DpRatio(),1020.f/720,.001f),
		"a wide view's canvas fits the safe area's height");
	viewport.width = 1080; viewport.height = 2340; viewport.safeLeft = 0; viewport.safeTop = 140; viewport.safeBottom = 80;
	Check(Near(viewport.DpRatio(),1080.f/960,.001f),"a portrait canvas keeps the safe area's width");
	viewport.safeLeft = 600; viewport.safeRight = 600;
	Check(Near(viewport.SafeWidth(),1,.001f) && viewport.DpRatio() > 0,"insets wider than the view leave a pixel");
	viewport.safeLeft = viewport.safeRight = viewport.safeTop = viewport.safeBottom = 0;
	viewport.canvasHeight = 0; viewport.width = 1920; viewport.height = 1080;
	Check(Near(viewport.DpRatio(),3,.001f),"display density applies without a canvas");
	viewport.safeTop = 100;
	Check(Near(viewport.DpRatio(),3,.001f),"display density ignores the safe area");
}

// A view source: the runtime publishes the view's width over its height as a
// read-only number state, which a document reads to arrange itself for a tall
// view (the settings sub-pages' single column).
static void CheckViewSource(ScreenHost& host) {
	const std::string root = R"({"id":"root","type":"group","children":[{"id":"tall","type":"group","properties":{"display":{"type":"keyword","value":"none"}}}]})";
	const auto source = [&](const std::string& declaration, const std::string& extra = "") {
		return Wrap(root,R"(,"canvas":{"height":720,"minWidth":960},"state":{"view.aspect":)"+declaration+"}"+extra);
	};
	const std::string aspect = R"({"type":"number","initial":1.3333,"view":"aspect"})";
	const std::string bound = R"(,"bindings":[{"id":"tall.display","node":"tall","property":"display","value":{"op":"select","args":[{"op":"<","args":[{"state":"view.aspect"},0.8]},"block","none"]}}])";
	Check(Loads(source(aspect,bound)),"a number state takes the view's aspect");
	Check(!Loads(source(R"({"type":"number","initial":1,"view":"width"})")) && !Loads(source(R"({"type":"boolean","initial":false,"view":"aspect"})")) &&
		!Loads(source(R"({"type":"number","initial":1,"view":"aspect","cvar":"ui_scale"})")),"only a number takes the aspect, never with a CVar");
	Check(!Loads(source(aspect,R"(,"events":{"onActivate":[{"op":"setState","values":{"view.aspect":1}}]})")),"events cannot write a view source");
	Runtime runtime(host); std::vector<Diagnostic> diagnostics; std::string error;
	Check(runtime.Initialize() && runtime.LoadDocument(source(aspect,bound),"view-source.q4ui",diagnostics),"the view source loads");
	Viewport viewport; viewport.canvasHeight = 720; viewport.canvasMinWidth = 960; viewport.width = 1280; viewport.height = 720;
	runtime.Frame(viewport,0.1);
	const auto shown = [&]() { const auto found = runtime.PresentedValue("tall","display"); return found ? found->text : std::string("?"); };
	Check(Near(static_cast<float>(std::get<double>(runtime.GetState(true).at("view.aspect"))),1280.f/720,.0001f) && shown() == "none",
		"a 16:9 view reads its aspect and keeps the wide arrangement");
	viewport.width = 540; viewport.height = 960;
	runtime.Frame(viewport,0.2);
	Check(Near(static_cast<float>(std::get<double>(runtime.GetState(true).at("view.aspect"))),.5625f,.0001f) && shown() == "block",
		"a 9:16 view reads its aspect and takes the tall arrangement");
	Check(!runtime.GetState(false).contains("view.aspect") && !runtime.SetState({{"view.aspect",2.0}},error,0.3),
		"the application neither reads back nor writes the view source");
	// The safe area's insets in dp, one source per edge, and its shape.
	const auto sourced = [&](const std::string& states) {
		return Wrap(root,R"(,"canvas":{"height":720,"minWidth":960},"state":{)"+states+"}");
	};
	const std::string insets = R"("left":{"type":"number","initial":0,"view":"safeLeft"},"top":{"type":"number","initial":0,"view":"safeTop"},)"
		R"("right":{"type":"number","initial":0,"view":"safeRight"},"bottom":{"type":"number","initial":0,"view":"safeBottom"})";
	Check(Loads(sourced(insets)),"a number state takes each safe inset");
	Check(!Loads(sourced(R"("top":{"type":"boolean","initial":false,"view":"safeTop"})")) &&
		!Loads(sourced(R"("top":{"type":"number","initial":0,"view":"safe"})")) &&
		!Loads(sourced(R"("top":{"type":"number","initial":0,"view":"safeTop","cvar":"ui_scale"})")),
		"only a number takes an inset, by its edge's name, never with a CVar");
	Runtime safe(host);
	Check(safe.Initialize() && safe.LoadDocument(sourced(insets+R"(,"aspect":{"type":"number","initial":1.3333,"view":"aspect"})"),
		"safe-source.q4ui",diagnostics),"the safe sources load");
	const auto read = [&](const char* id) { return static_cast<float>(std::get<double>(safe.GetState(true).at(id))); };
	viewport.width = 540; viewport.height = 1170; viewport.safeTop = 72; viewport.safeBottom = 36;
	safe.Frame(viewport,0.4);
	Check(Near(read("top"),72/.5625f,.01f) && Near(read("bottom"),36/.5625f,.01f) && read("left") == 0 && read("right") == 0 &&
		Near(read("aspect"),540.f/1062,.0001f),"the insets read in dp and the aspect is the safe area's");
	viewport.safeTop = viewport.safeBottom = 0;
	safe.Frame(viewport,0.5);
	Check(read("top") == 0 && read("bottom") == 0 && Near(read("aspect"),540.f/1170,.0001f),"a view without insets reads none");
	Check(!safe.SetState({{"top",1.0}},error,0.6),"the application cannot write a safe inset");
}

static const std::set<std::string> SessionCommands = {"continue","singlePlayer","loadGame","saveGame","multiplayer","settings",
	"mods","updates","credits","quit","resume","menuResume","restartLevel","quitToMenu",
	"loadBack","loadSelect","loadSave","loadDelete","loadNewer","loadOlder","loadNewGame",
	"saveListRefresh","saveListSort","saveListFilterManual","saveListFilterAuto","saveListClear",
	"saveBack","saveSelect","saveSave","saveOverwrite","saveNewer","saveOlder","restartBack","restartGo",
	"demosBack","demosSelect","demosPlay","demosDelete","demosRefresh","demosFilter","demosNewer","demosOlder","modsSelect","modsLoad","deathRestart","deathLoad","deathMenu","creditsBack",
	"arenaBack","arenaTier0","arenaTier1","arenaTier2","arenaTier3","arenaTier4","arenaMatch0","arenaMatch1","arenaMatch2","arenaMatch3",
	"arenaTierPrevious","arenaTierNext","arenaEasier","arenaHarder","arenaStart","arenaReset","arenaResetConfirm","arenaResetCancel","arenaRetry","arenaDismiss",
	"multiplayerJoin","multiplayerCreate","multiplayerDemos","multiplayerSetup","multiplayerBack","playerSetupClose",
	"createBack","createMap","createStart","createName","createMapCycle","createPassword","createRconPassword","createMapsNewer",
	"createMapsOlder","createBanList","createBanPick","createBanRemove",
	"joinBack","joinPick","joinServer","joinSpectate","joinNewer","joinOlder","joinRefresh","joinNewList","joinSourceInternet","joinSourceLan",
	"joinClearSort","joinSortFavorite","joinSortLocked","joinSortName","joinSortPing","joinSortPlayers","joinSortGameType","joinSortMap",
	"joinSortDedicated","joinSortRepeater","joinFavorite","joinModPrevious","joinModNext","joinFilterFull","joinFilterEmpty","joinFilterPassword","joinGameTypePrevious","joinGameTypeNext","joinClearFilters","noticeAction0","noticeAction1","noticeAction2","audioBack","gameBack","controlsBind","controlsClear","controlsBack","controlsTouchLayout","settingsBack","settingsControls","settingsGame","settingsDisplay","settingsAudio","settingsDefaults","joinConnect","joinSearch","joinSearchClear","joinSortNext",
	"mpClose","mpMainMenu","mpDisconnect","mpStockPage","mpTeamAction","mpSelectPlayer","mpMute","mpFriend","mpWelcomeAction",
	"mpVoteYes","mpVoteNo","mpCallVote","mpRail","mpSettingsControls","mpSettingsGame","mpSettingsSystem","mpMatch","mpMatchSelect",
	"mpSummaryChat","mpAdminPass","mpAdminSelect","mpAdminKick","mpAdminBan","mpAdminSwitch","mpAdminBans","mpAdminBanPick","mpAdminUnban",
	"mpAdminApply","mpAdminConsole","mpRefereeLogin","mpSettingsName","mpSettingsClan"};
// The value controls' verbs, which carry the control's new value.
static const std::set<std::string> SessionValueCommands = {"mpVoteMap","mpVoteGameType","mpVoteTimeLimit","mpVoteFragLimit",
	"mpVoteCaptureLimit","mpVoteTourneyLimit","mpVoteControlTime","mpVoteBalance","mpVoteShuffle","mpVoteRestart","mpVoteBuying","mpVoteKick",
	"mpModelSelf","mpModelEnemy","mpModelTeam","mpCrosshair","mpMatchRole","mpMatchScope","mpMatchRuleValue",
	"mpMatchSeriesProfile","mpAdminMap","mpAdminGameType","mpAdminTimeLimit","mpAdminFragLimit","mpAdminCaptureLimit","mpAdminTourneyLimit",
	"mpAdminControlTime","mpAdminBalance","mpAdminShuffle","mpAdminRestart","mpAdminBuying",
	"createGameType","createLan","createDedicated","createAutoBalance","createFragLimit","createCaptureLimit","createControlTime",
	"createTimeLimit","createMaxPlayers","createMinPlayers","createVoting","createTeamDamage","createTourneyLimit","createUsePass",
	"createServerMods","createWeaponStay","createPure","createRate","createReady","createSpectators","createWarmup","createVoice",
	"createBuying","createBots","createBotSkill",
	"audioVolume","audioMusic","audioDialogue","audioBackend","audioDevice","audioEax","audioSpeakers","audioHrtf",
	"audioSubtitles","audioSubtitleSize","audioSubtitleBackground","audioSubtitleSpeakers",
	"gameGunPosition","gameForceModel","gameCrosshair","gameCorpseTime","gameLanguage"};
// The player settings the multiplayer card's Settings and Voice pages and the
// GAME OPTIONS page change.
static const std::set<std::string> PlayerSettings = {"ui_handicap","cl_player_outline_enemy","cl_player_outline_team",
	"cl_player_rimlight_enemy","cl_player_rimlight_team","cl_player_visibility_enemy_color","cl_player_visibility_team_color",
	"cl_player_brightskin_enemy","cl_player_brightskin_team","cl_player_brightskin_enemy_color","cl_player_brightskin_team_color",
	"cl_player_outline_width","s_voiceChatSend","s_voiceChatReceive","s_voiceChatEcho","s_voiceVolume","s_micInputLevel",
	"s_voiceChatCapture","s_voiceChatTest",
	"in_freeLook","ui_autoReload","ui_autoSwitch","in_toggleZoom","pm_zoomedSlow","g_decals","ui_showGun","g_simpleItems",
	"g_mpFlatOpponentWeapons","m_pitch","m_smooth","sensitivity","m_cpi","m_filter","cl_mouseAccel","cl_mouseAccelOffset",
	"cl_mouseAccelPower","cl_mouseSensCap","in_joystick","in_joystickSouthpaw","in_joystickDeadZone","in_joystickLookSensitivity",
	"in_weaponWheelSensitivity","ui_weaponWheelOffset","in_joystickLookCurve","in_joystickInvertLook","in_joystickTriggerThreshold",
	"in_joystickRumble","in_joystickRumbleScale","in_gyro","in_gyroSensitivity","in_touchpadMode","in_touchpadSensitivity",
	"in_touchscreen","in_joystickLowBatteryRumbleThreshold","in_joystickLowBatteryRumbleScale","in_promptStyle","in_swapConfirmBack",
	"g_crosshairCustom","g_crosshairSize","g_crosshairColor","hud_hitMarker","hud_crosshairAnimated","ui_hudLayout","ui_hudScale","ui_hudWidth",
	"ui_hudSafeInset","g_objectivesToggle","ui_hudOpacity","ui_hudDamageDirection","ui_hudPickups","ui_hudWeaponStrip",
	"ui_reducedFlashing","ui_retainedOpaqueBacking","g_corpseSink","con_allowConsole","g_autoSkipCinematics","ui_cinematicShake","cl_gunfov","cl_gunfov_adjust","cl_gun_x",
	"cl_gun_y","cl_gun_z","g_weaponMuzzleKick","vr_enable","vr_aimMode","vr_leftHanded","vr_turnMode","vr_snapTurnAngle",
	"vr_moveDirection","vr_roomScale","vr_aimLaser","vr_hapticStrength","vr_twoHanded","vr_comfortVignette","vr_physicalCrouch",
	"vr_playSpace"};

static void CheckSessionActions(const Document& document) {
	for (const auto& [id,action] : document.Model().actions) {
		if (action.operation == "player.setup.apply") {
			Check(ValidPlayerSetupAction(action),"Player Setup submits a typed, observed-value and catalog-bound proposal");
			continue;
		}
		// A text field writes the page's own state, then may run its event;
		// a page may return the focus to a field holding text not yet entered.
		if (action.operation == "ui.state.set") {
			const auto key = action.arguments.find("key"), event = action.arguments.find("event");
			Check(key != action.arguments.end() && document.Model().state.contains(std::get<std::string>(key->second.literal)) &&
				action.arguments.at("value").inputValue && action.inputType &&
				(event == action.arguments.end() || document.Model().events.contains(PresentationAliasKey(std::get<std::string>(event->second.literal)))),
				"a field writes the page's own state and names its own event");
			continue;
		}
		if (action.operation == "ui.numberDrafts.focus") {
			Check(action.arguments.empty(),"returning to a field takes no arguments");
			continue;
		}
		if (action.operation == "settings.player.set") {
			const auto cvar = action.arguments.find("cvar");
			if (id == "voiceTestStop") {
				Check(cvar != action.arguments.end() && action.arguments.size() == 2 &&
					std::get<std::string>(cvar->second.literal) == "s_voiceChatTest" &&
					!action.arguments.at("value").inputValue && !std::get<bool>(action.arguments.at("value").literal),
					"leaving the Voice page can only stop the local microphone test");
				continue;
			}
			Check(cvar != action.arguments.end() && action.arguments.size() == 2 && PlayerSettings.contains(std::get<std::string>(cvar->second.literal)) &&
				action.arguments.at("value").inputValue && action.inputType,"a setting control sets an allowlisted player setting");
			continue;
		}
		const bool value = action.operation == "session.menuValue";
		Check(action.operation == "session.menu" || value,"production screens request only session operations");
		const auto command = action.arguments.find("command");
		Check(command != action.arguments.end(),"session operation names a command");
		const auto& verb = std::get<std::string>(command->second.literal);
		const bool player = verb == "mpSelectPlayer" || verb == "mpMute" || verb == "mpFriend" ||
			verb == "mpAdminSelect" || verb == "mpAdminKick" || verb == "mpAdminBan" || verb == "mpAdminSwitch";
		const bool save = verb == "loadSelect" || verb == "saveSelect" || verb == "loadSave" || verb == "loadDelete";
		Check(!save || (action.arguments.at("index").type == 0 && action.arguments.at("generation").type == 0),"save command carries catalog operands");
		Check(action.arguments.size() == (player || save || verb == "mpVoteKick" ? 3u : value ? 2u : 1u),"command carries exactly its typed operands");
		Check(!player || (action.arguments.at("client").type == 0 && action.arguments.at("identity").type == 2),"player command carries client and connection identity");
		Check((value ? SessionValueCommands : SessionCommands).contains(std::get<std::string>(command->second.literal)),
			"every command is in the session allowlist");
		Check(!value || (action.arguments.at("value").inputValue && action.inputType),"a value verb carries its control's value");
	}
}

// Overflow under a transform: RmlUi clips with the clipping box's geometry
// instead of a scissor. The renderer applies it as an exact rectangle for
// translation and scale, and clips a rotated box to its bounds (counted).
static void CheckTransformedClip(ScreenHost& host) {
	const auto document = [](const char* transform) {
		return Wrap(std::string(R"({"id":"root","type":"group","properties":{"transform":{"type":"transform","unit":"dp","value":)")+transform+R"(}},"children":[)"
			R"({"id":"clip","type":"group","properties":{"position":{"type":"keyword","value":"absolute"},"left":{"type":"length","value":10,"unit":"dp"},"top":{"type":"length","value":0,"unit":"dp"},"width":{"type":"length","value":50,"unit":"dp"},"height":{"type":"length","value":10,"unit":"dp"},"overflow":{"type":"keyword","value":"hidden"}},"children":[)"
			R"({"id":"wide","type":"vector","properties":{"position":{"type":"keyword","value":"absolute"},"left":{"type":"length","value":0,"unit":"dp"},"top":{"type":"length","value":0,"unit":"dp"},"width":{"type":"length","value":200,"unit":"dp"},"height":{"type":"length","value":10,"unit":"dp"}},"paths":[)"
			R"({"id":"bar","commands":[{"id":"a","op":"move","points":[[0,0]]},{"id":"b","op":"line","points":[[{"fraction":1},0]]},{"id":"c","op":"line","points":[[{"fraction":1},{"fraction":1}]]},{"id":"d","op":"line","points":[[0,{"fraction":1}]]},{"id":"e","op":"close"}],"fill":{"type":"solid","color":{"type":"color","value":[1,1,1,1]}}}]}]}]})");
	};
	for (const bool rotated : {false,true}) {
		Runtime runtime(host);
		std::vector<Diagnostic> diagnostics;
		Check(runtime.Initialize() && runtime.LoadDocument(document(rotated ? "[10,20,1,1,10]" : "[10,20,1,1,0]"),"clip.q4ui",diagnostics),
			"transformed clip document loads");
		host.draws.clear();
		runtime.Frame(Viewport(),1);
		Bounds clip;
		Check(runtime.GetBounds("clip",clip),"clip box laid out");
		float minX = 1e9f, maxX = -1e9f;
		size_t vertices = 0;
		for (const auto& draw : host.draws) for (const auto& vertex : draw.vertices) {
			minX = std::min(minX,vertex.x); maxX = std::max(maxX,vertex.x); ++vertices;
		}
		const auto statistics = runtime.Statistics();
		if (!rotated) {
			Check(vertices > 0 && Near(minX,clip.x+10,.01f) && Near(maxX-minX,clip.width,.01f),"a translated overflow clip cuts its content exactly");
			Check(statistics.clipMasks > 0 && statistics.clipMaskFallbacks == 0,"a translated clip mask is an exact rectangle");
		} else {
			// A rotated box is a convex quadrilateral: every emitted vertex
			// lies inside all four of its rotated edges, not just its bounds.
			Check(vertices > 0 && maxX-minX < clip.width+2,"a rotated overflow clip bounds its content");
			Check(statistics.clipMasks > 0 && statistics.clipMaskFallbacks == 0,"a rotated clip mask is exact");
			const float angle = 10*3.14159265f/180, c = std::cos(angle), s = std::sin(angle);
			// The clip box (10..60 x 0..10 dp before the root's rotation about its centre).
			Bounds root; runtime.GetBounds("root",root);
			const float cx = root.x+root.width/2, cy = root.y+root.height/2;
			for (const auto& draw : host.draws) for (const auto& vertex : draw.vertices) {
				// Undo the root's translation (10, 20) and its rotation about
				// its centre, then compare with the laid-out box.
				const float dx = vertex.x-10-cx, dy = vertex.y-20-cy;
				const float ux = c*dx+s*dy+cx, uy = -s*dx+c*dy+cy;
				Check(ux >= clip.x-.05f && ux <= clip.x+clip.width+.05f && uy >= clip.y-.05f && uy <= clip.y+clip.height+.05f,
					"a rotated overflow clip cuts its content to the rotated box");
			}
		}
	}
}

static bool Additive(const ScreenHost& host) {
	for (const auto& draw : host.draws) if (!draw.material)
		for (const auto& vertex : draw.vertices) if (vertex.a == 0 && vertex.g > .01f) return true;
	return false;
}

// The multiplayer Escape card (section 14.18): centered at every view, motion
// and release, the tab programs, the leaving page, hand-offs and the header.
static void CheckEscape(ScreenHost& host, const char* path) {
	const auto source = Read(path);
	Document document; std::vector<Diagnostic> diagnostics;
	const bool valid = document.Load(source,diagnostics);
	for (const auto& diagnostic : diagnostics) std::fprintf(stderr,"mp_escape %s: %s\n",diagnostic.pointer.c_str(),diagnostic.message.c_str());
	Check(valid && document.Model().id == "openq4.mp_escape" && document.Model().canvasHeight == 720,"the Escape card validates");
	CheckSessionActions(document);
	// The softened view stands outside the card's composition: a node inside
	// a composited group gets no soft focus.
	const auto& root = document.Model().root;
	Check(root.children.size() == 3 && root.children[0].id == "scene-softfocus" && root.children[1].id == "scrim" &&
		root.children[2].id == "chrome","the softened view, its scrim stand-in, then the card and its modal");
	Runtime runtime(host);
	Check(runtime.Initialize() && runtime.LoadDocument(source,"guis/menu/mp_escape.q4ui",diagnostics),"the card loads into a runtime");
	Viewport viewport; viewport.canvasHeight = 720;
	std::string error;
	Runtime::EventEffects effects;
	const char* const tabs[] = {"team","players","vote","match","settings","voice","server","admin"};
	const auto bounds = [&](const std::string& id) { Bounds b; Check(runtime.GetBounds(id,b),"a card node is laid out"); return b; };
	const auto number = [&](const char* node, const char* property) {
		const auto value = runtime.PresentedValue(node,property); Check(value.has_value(),"a presented value"); return static_cast<float>(value->data[0]);
	};
	const auto text = [&](const char* node, const char* property) {
		const auto value = runtime.PresentedValue(node,property); Check(value.has_value(),"a presented keyword"); return value->text;
	};
	// Centered on the projection center at 16:9, 1080 p and 4:3, never closer
	// than 16 dp to the sides, the same size on every tab.
	float cardWidth = 0;
	for (const auto& [width,height] : std::vector<std::pair<int,int>>{{1280,720},{1920,1080},{1024,768}}) {
		viewport.width = width; viewport.height = height;
		runtime.Frame(viewport,1);
		const auto card = bounds("card");
		const float scale = height/720.f;
		if (!cardWidth) cardWidth = card.width/scale;
		Check(Near(card.x+card.width/2,width/2.f) && Near(card.y+card.height/2,height/2.f),"the card is centered");
		Check(Near(card.width,cardWidth*scale,1) && Near(card.height,477*scale,1),"the card keeps its size at every view");
		Check(card.x >= 16*scale-.5f,"the card clears the view's sides by 16 dp");
	}
	Check(cardWidth >= 648-.5f && cardWidth <= 928+.5f,"the card is at least the specified 648 dp and fits a 4:3 view");
	viewport.width = 1280; viewport.height = 720;
	// The game publishes the Team page before the card opens: on a team, a
	// switch it allows with the sizes it would leave, a spectate it refuses
	// with its reason, and no third action.
	Check(runtime.SetState({{"mp.team.band",std::string("MARINES")},{"mp.team.band_color",0.0},{"mp.team.band_score",std::string("12")},
		{"mp.team.band_detail",std::string("4 - 3")},
		{"mp.action0.shown",true},{"mp.action0.available",true},{"mp.action0.label",std::string("SWITCH TEAM")},
		{"mp.action0.detail",std::string("MARINES 3 - 4 STROGG")},
		{"mp.action1.shown",true},{"mp.action1.available",false},{"mp.action1.label",std::string("SPECTATE")},
		{"mp.action1.reason",std::string("Spectating is disabled on this server.")},
		{"mp.action2.shown",false},{"mp.chat0",std::string("Anderson: gg")}},error,1.5),"publish the Team page");
	// Opening: the softening ramps over 250 ms while the card rises 12 dp and
	// fades in over 150 ms; the current page's primary action takes focus.
	host.softFocus = true;
	Check(runtime.RunEvent("open",2,effects,error),"the card opens");
	// 30 ms in: the ease-out has covered most of the way but not all of it.
	runtime.Frame(viewport,2.03);
	Check(number("card","opacity") > .05f && number("card","opacity") < .99f,"part way through its fade");
	const auto rising = runtime.PresentedValue("card","transform");
	Check(rising && rising->data[1] > .05f && rising->data[1] < 12,"and rising from 12 dp");
	runtime.Frame(viewport,2.2);
	Check(Near(number("card","opacity"),1,.001f) && Near(static_cast<float>(runtime.PresentedValue("card","transform")->data[1]),0,.01f),
		"risen and shown at 150 ms");
	Check(number("scene-softfocus","backdrop-blur") < 7.4f,"the softening is still ramping");
	runtime.Frame(viewport,2.3);
	Check(Near(number("scene-softfocus","backdrop-blur"),7.5f,.01f) && Near(number("scene-softfocus","backdrop-saturate"),.8f,.001f),
		"softened at 250 ms with modal.softfocus's values");
	Check(text("scene-softfocus","display") == "block" && text("scrim","display") == "none","soft focus replaces the scrim");
	Check(runtime.FocusedControl() == "team-slot-0","the Team page's first action has focus");
	// The Team page: the band in the team's color, an available action with
	// its detail, an unavailable one dimmed with its lock and reason, and a
	// slot the game leaves empty hidden.
	Check(text("team-band-0","display") == "block" && text("team-band-1","display") == "none","the band takes the team's color");
	Check(text("team-slot-0-detail","display") == "block" && text("team-slot-0-reason","display") == "none" &&
		text("team-slot-0-lock","display") == "none" && Near(number("team-slot-0","opacity"),1,.001f),"an available action shows its detail");
	Check(text("team-slot-1-reason","display") == "block" && text("team-slot-1-lock","display") == "block" &&
		Near(number("team-slot-1","opacity"),.6f,.001f),"an unavailable one dims with its lock and reason");
	Check(text("team-slot-2-slot","display") == "none","an empty slot is hidden");
	const auto slot0 = bounds("team-slot-0-slot"), slot1 = bounds("team-slot-1-slot");
	Check(slot1.y >= slot0.y+slot0.height,"the actions stack without overlap");
	Check(runtime.RunEvent("team_slot_0",2.31,effects,error) && effects.actions.size() == 1 &&
		std::get<std::string>(effects.actions[0].arguments.at("command")) == "mpTeamAction" &&
		std::get<double>(runtime.GetState().at("card.team_action")) == 0,"an available action asks the game");
	Check(runtime.RunEvent("team_slot_1",2.32,effects,error) && effects.actions.empty(),"an unavailable one does not");
	runtime.Frame(viewport,2.37);
	const auto shaking = runtime.PresentedValue("team-slot-1-slot","transform");
	Check(shaking && std::abs(shaking->data[0]) > .5f,"it shakes instead");
	runtime.Frame(viewport,2.7);
	const auto settled = runtime.PresentedValue("team-slot-1-slot","transform");
	Check(settled && Near(static_cast<float>(settled->data[0]),0,.01f),"and settles after 300 ms");
	// Each tab: the strip lays its tabs in order without overlap inside the
	// card, and only the current tab rises.
	const auto card = bounds("card");
	float previousRight = card.x;
	for (const char* tab : tabs) {
		const auto box = bounds(std::string("tab-")+tab);
		Check(box.x >= previousRight-.5f && box.x+box.width <= card.x+card.width,"tabs keep their order inside the card");
		previousRight = box.x+box.width;
		const auto label = bounds(std::string("tab-")+tab+"-label");
		Check(label.x >= box.x && label.x+label.width <= box.x+box.width+.5f,"a label stays inside its tab");
	}
	Check(text("tab-team-active","display") == "block" && text("tab-players-active","display") == "none","only the current tab rises");
	// The game publishes the Players page: three Marines with the player's own
	// row second, two Strogg and a spectator; the player's statistics show.
	StateValues lists = {{"mp.players.a.shown",true},{"mp.players.a.title",std::string("MARINES")},{"mp.players.a.color",0.0},
		{"mp.players.a.score",std::string("12")},{"mp.players.a.count",3.0},{"mp.players.b.shown",true},
		{"mp.players.b.title",std::string("STROGG")},{"mp.players.b.color",1.0},{"mp.players.b.count",2.0},
		{"mp.players.s.shown",true},{"mp.players.s.title",std::string("SPECTATORS")},{"mp.players.s.color",2.0},{"mp.players.s.count",1.0},
		{"mp.stat.client",1.0},{"mp.stat.name",std::string("Kane")},{"mp.stat.color",0.0},{"mp.stat.kills",std::string("7")},
		{"mp.stat.acc0",std::string("57%")},{"mp.stat.acc1",std::string("-")},{"mp.stat.award0",std::string("2")},
		{"mp.stat.award1",std::string("0")},
		{"mp.mute.shown",true},{"mp.mute.available",false},{"mp.mute.label",std::string("MUTE PLAYER")},
		{"mp.mute.reason",std::string("You can't mute yourself.")},{"mp.friend.shown",true},{"mp.friend.available",false},
		{"mp.friend.label",std::string("ADD FRIEND")},{"mp.friend.reason",std::string("You can't add yourself as a friend.")}};
	const char* const names[] = {"Anderson","Kane","Rhodes","Makron","Gladiator","Voss"};
	for (int client = 0; client < 6; ++client) {
		const std::string row = client < 3 ? "mp.players.a"+std::to_string(client) : client < 5 ? "mp.players.b"+std::to_string(client-3) :
			"mp.players.s0";
		lists[row+".client"] = static_cast<double>(client); lists[row+".name"] = std::string(names[client]);
		lists[row+".score"] = std::to_string(10-client); lists[row+".ping"] = std::to_string(20+client);
		lists[row+".local"] = client == 1; lists[row+".friend"] = client == 0; lists[row+".muted"] = client == 2;
		// Rhodes is dead and Gladiator is a bot; the pings span the bars' steps.
		lists[row+".dead"] = client == 2; lists[row+".bot"] = client == 4;
		// The game publishes each row's step and its color.
		const double bars[] = {4,3,2,1,0,4};
		const double tints[][3] = {{0xB5,0xC7,0x84},{0xB5,0xC7,0x84},{0xE3,0xAD,0x36},{0xE4,0x6D,0x56},{1,1,1},{0xB5,0xC7,0x84}};
		lists[row+".bars"] = bars[client];
		lists[row+".bars_r"] = tints[client][0]/255; lists[row+".bars_g"] = tints[client][1]/255; lists[row+".bars_b"] = tints[client][2]/255;
	}
	Check(runtime.SetState(lists,error,2.9),"publish the Players page");
	// Q, E and the strip's keycaps: the next tab at once, its page cross-fading
	// over 150 ms, the leaving page shown but out of reach meanwhile.
	Check(runtime.RunEvent("onTabNext",3,effects,error),"E moves to the next tab");
	Check(std::get<double>(runtime.GetState().at("card.tab")) == 1 && std::get<double>(runtime.GetState().at("card.leaving")) == 0,
		"the players tab is current and the team page is leaving");
	runtime.Frame(viewport,3.075);
	Check(text("page-team","display") == "block" && text("page-players","display") == "block","both pages show while they cross-fade");
	Check(number("page-players","opacity") > .1f && number("page-players","opacity") < .9f && number("page-team","opacity") < .9f,
		"one fades in as the other fades out");
	Check(!runtime.CanActivateControl("team-slot-0",3.075) && runtime.CanActivateControl("players-a-1",3.075),
		"the leaving page takes no input");
	Check(runtime.FocusedControl() == "players-a-1","the arriving page focuses the player whose statistics show");
	runtime.Frame(viewport,3.2);
	Check(text("page-team","display") == "none" && Near(number("page-players","opacity"),1,.001f) &&
		std::get<double>(runtime.GetState().at("card.leaving")) == -1,"the cross-fade ends with one page");
	Check(text("tab-players-active","display") == "block" && text("tab-team-active","display") == "none","the new tab rises");
	// The Players page: each list's rows in its band's color, the player's own
	// row brighter with the marker, the selected player lit, the symbols.
	Check(text("players-a-2","display") == "block" && text("players-a-3","display") == "none" && text("players-b-1","display") == "block" &&
		text("players-b-2","display") == "none" && text("players-s-0","display") == "block" && text("players-s-1","display") == "none",
		"each list shows its players' rows");
	Check(text("players-a-band-0","display") == "block" && text("players-a-band-1","display") == "none" &&
		text("players-b-band-1","display") == "block" && text("players-s-band","display") == "block","the bands take their teams' colors");
	Check(text("players-a-band-mark0","display") == "block" && text("players-a-band-mark1","display") == "none" &&
		text("players-b-band-mark1","display") == "block" && text("players-b-band-mark0","display") == "none",
		"the Marine and Strogg bands carry their faction marks");
	Check(text("players-a-1-marker","display") == "block" && text("players-a-0-marker","display") == "none" &&
		Near(number("players-a-1-band","opacity"),.29f,.001f) && Near(number("players-a-0-band","opacity"),.08f,.001f),
		"the player's own row is brighter and marked");
	Check(text("players-a-1-chosen","display") == "block" && text("players-a-0-chosen","display") == "none","the selected player is lit");
	Check(text("players-a-2-speaker-slash","display") == "block" && text("players-a-0-speaker-slash","display") == "none" &&
		Near(number("players-a-0-friend","opacity"),1,.001f) && Near(number("players-a-2-friend","opacity"),.125f,.001f),
		"muted players' speakers are crossed and friends lit");
	struct Rgba { float r, g, b, a; };
	const auto color = [&](const char* node) { const auto value = runtime.PresentedValue(node,"background-color");
		Check(value.has_value(),"a presented color");
		return value.has_value() ? Rgba{static_cast<float>(value->data[0]),static_cast<float>(value->data[1]),
			static_cast<float>(value->data[2]),static_cast<float>(value->data[3])} : Rgba{-1,-1,-1,-1}; };
	// The held scoreboard's marks: a skull (its slot kept) and the row at half while dead, a
	// BOT tag beside a bot's name in place of its bars, and the bars by ping.
	Check(Near(number("players-a-2-skull","opacity"),1,.001f) && Near(number("players-a-0-skull","opacity"),0,.001f) &&
		Near(number("players-a-2-content","opacity"),.5f,.001f) && Near(number("players-a-0-content","opacity"),1,.001f),
		"a dead player's row shows the skull at half");
	Check(text("players-b-1-bot","display") == "block" && text("players-b-0-bot","display") == "none" &&
		Near(color("players-b-1-bar0").a,0,.001f) && Near(color("players-b-0-bar0").a,1,.001f),"a bot shows its tag, not bars");
	// Four steps lit, three, two and one, in the color the game gives; unlit bars at 0.20.
	Check(Near(color("players-a-0-bar3").a,1,.001f) && Near(color("players-a-1-bar3").a,.2f,.001f) &&
		Near(color("players-a-1-bar2").a,1,.001f) && Near(color("players-a-2-bar1").a,1,.001f) &&
		Near(color("players-a-2-bar2").a,.2f,.001f) && Near(color("players-b-0-bar1").a,.2f,.001f),"the bars light to the step");
	Check(Near(color("players-a-0-bar0").g,0xC7/255.f,.01f) && Near(color("players-a-2-bar0").r,0xE3/255.f,.01f) &&
		Near(color("players-a-2-bar0").g,0xAD/255.f,.01f) && Near(color("players-b-0-bar0").g,0x6D/255.f,.01f),
		"each row's bars take the color the game publishes");
	{
		const auto name = bounds("players-b-1-name"), tag = bounds("players-b-1-bot"), score = bounds("players-b-1-score");
		const auto bars = bounds("players-b-0-bars"), ping = bounds("players-b-0-ping"), score0 = bounds("players-b-0-score");
		Check(name.x+name.width <= tag.x+.5f && tag.x+tag.width <= score.x+.5f,"the BOT tag sits between the name and the score");
		Check(score0.x+score0.width <= bars.x+.5f && bars.x+bars.width <= ping.x+.5f,"the bars stand between the score and the ping");
		const auto head = bounds("players-head-score");
		Check(Near(head.x+head.width,score0.x+score0.width,1.5f),"the score heading stays over its column");
	}
	const auto lists0 = bounds("players-lists"), stats0 = bounds("players-stats");
	Check(lists0.x+lists0.width <= stats0.x,"the statistics stand beside the lists");
	Check(Near(bounds("players-a-0").height,24,.01f) && bounds("players-a-1").y >= bounds("players-a-0").y+23.9f,"few players take the widest pitch");
	// The statistics: a weapon that has not fired dims, flag awards only in flag modes.
	Check(Near(number("stat-acc-0-icon","opacity"),1,.001f) && Near(number("stat-acc-1-icon","opacity"),.35f,.001f),
		"a weapon that has not fired dims");
	Check(Near(number("stat-award-0-icon","opacity"),1,.001f) && Near(number("stat-award-1-icon","opacity"),.35f,.001f),
		"an award not earned dims");
	Check(text("stat-award-4-cell","display") == "block" && text("stat-award-5-cell","display") == "none","flag awards only in flag modes");
	Check(text("stat-band-0","display") == "block" && text("stat-band-2","display") == "none","the statistics take the player's team color");
	// The player's own row offers neither Mute nor Friend, and says why.
	Check(text("players-mute-reason","display") == "block" && text("players-mute-lock","display") == "block","Mute says why it is unavailable");
	Check(runtime.RunEvent("players_mute",3.3,effects,error) && effects.actions.empty(),"an unavailable Mute asks nothing");
	// Choosing a row asks the game for that player; the game answers with
	// their statistics and makes Mute and Friend available.
	Check(runtime.RunEvent("players_b1",3.4,effects,error) && effects.actions.size() == 1 &&
		std::get<std::string>(effects.actions[0].arguments.at("command")) == "mpSelectPlayer" &&
		std::get<double>(runtime.GetState().at("card.client")) == 4,"choosing a row asks for its player");
	Check(runtime.SetState({{"mp.stat.client",4.0},{"mp.stat.color",1.0},{"mp.stat.flag_mode",true},{"mp.mute.available",true},
		{"mp.friend.available",true}},error,3.45),"the game selects the player");
	runtime.Frame(viewport,3.5);
	Check(text("players-b-1-chosen","display") == "block" && text("players-a-1-chosen","display") == "none","the chosen player is lit");
	Check(text("stat-band-1","display") == "block" && text("stat-award-5-cell","display") == "block","their team's color, and flag awards in flag modes");
	Check(runtime.RunEvent("players_friend",3.6,effects,error) && effects.actions.size() == 1 &&
		std::get<std::string>(effects.actions[0].arguments.at("command")) == "mpFriend" &&
		std::get<double>(runtime.GetState().at("card.client")) == 4,"Friend asks for the selected player");
	Check(runtime.RunEvent("players_mute",3.7,effects,error) && effects.actions.size() == 1 &&
		std::get<std::string>(effects.actions[0].arguments.at("command")) == "mpMute","and so does Mute");
	// A full roster keeps readable rows and scrolls under fixed headings.
	StateValues full;
	for (const auto& [list,count] : std::vector<std::pair<std::string,int>>{{"a",8},{"b",7},{"s",1}}) {
		full["mp.players."+list+".count"] = static_cast<double>(count);
		for (int row = 0; row < count; ++row) full["mp.players."+list+std::to_string(row)+".client"] = static_cast<double>(row+10);
	}
	Check(runtime.SetState(full,error,3.75),"sixteen players");
	runtime.Frame(viewport,3.8);
	const auto fullScroll = runtime.GetWidgetState("players-scroll");
	Check(Near(bounds("players-a-0").height,24,.02f) && fullScroll && fullScroll->scroll && fullScroll->scroll->geometry.usable,
		"sixteen players retain their pitch with scrolling available");
	// More connections extend the scrollable list without shrinking text.
	StateValues over = full;
	over["mp.players.a.count"] = 16.0;
	for (int row = 8; row < 16; ++row) over["mp.players.a"+std::to_string(row)+".client"] = static_cast<double>(row+30);
	Check(runtime.SetState(over,error,3.8),"twenty-four clients");
	runtime.Frame(viewport,3.82);
	Check(Near(bounds("players-a-0").height,24,.02f),"long rosters preserve their row pitch");
	Check(runtime.SetState(lists,error,3.85),"back to six players");
	// The strip wraps both ways; choosing the current tab changes nothing.
	Check(runtime.RunEvent("tab_admin",4,effects,error) && runtime.RunEvent("onTabNext",4.01,effects,error),"E past the last tab");
	Check(std::get<double>(runtime.GetState().at("card.tab")) == 0,"wraps to the first");
	Check(runtime.RunEvent("onTabPrevious",4.02,effects,error) && std::get<double>(runtime.GetState().at("card.tab")) == 7,"Q wraps back");
	runtime.Frame(viewport,4.3);
	Check(runtime.RunEvent("tab_admin",4.4,effects,error) && std::get<double>(runtime.GetState().at("card.leaving")) == -1 &&
		effects.actions.empty(),"the current tab does nothing");
	// Every page is built: none hands off to its stock page.
	Check(!document.Model().events.contains(PresentationAliasKey("stock_admin")),"Admin no longer hands off to the stock Admin page");
	Check(runtime.RunEvent("onBack",5.1,effects,error) && effects.actions.size() == 1 &&
		std::get<std::string>(effects.actions[0].arguments.at("command")) == "mpClose","Back resumes the match");
	// The prompt bar sits inside the card's bottom inset, Disconnect last.
	const auto prompts = bounds("prompts"), disconnect = bounds("prompt-disconnect"), menu = bounds("prompt-mainmenu");
	Check(prompts.y+prompts.height <= card.y+card.height-15 && prompts.y > card.y+card.height-60,"the prompt bar closes the card");
	Check(menu.x < disconnect.x && disconnect.x+disconnect.width <= card.x+card.width-23,"Main Menu, then Disconnect, at the trailing end");
	Check(runtime.RunEvent("disconnectModalShow",6,effects,error),"Disconnect asks first");
	runtime.Frame(viewport,6.3);
	Check(runtime.FocusedControl() == "disconnectModal_no","the confirmation's NO has focus");
	Check(runtime.RunEvent("disconnectModalHide",6.4,effects,error),"NO closes it");
	runtime.Frame(viewport,6.8);
	// The Server page: its rules and rotation, the current map in orange and
	// the slots past the rotation hidden; its tab keeps the focus.
	Check(runtime.SetState({{"mp.server.name",std::string("openQ4 test")},{"mp.server.message",std::string("Welcome\nRules: be fair")},
		{"mp.rule0",std::string("Team Deathmatch")},{"mp.rotation0",std::string("The Fragging Yard")},{"mp.rotation1",std::string("Lost Fleet")},
		{"mp.rotation2",std::string("Bloodwork")},{"mp.rotation_count",3.0},{"mp.rotation_current",1.0}},error,6.85),"publish the Server page");
	Check(runtime.RunEvent("tab_server",6.9,effects,error),"to the Server tab");
	runtime.Frame(viewport,7.06);
	Check(runtime.FocusedControl() == "tab-server","the Server page's tab keeps the focus");
	const auto current = runtime.PresentedValue("server-rotation-1","color"), other = runtime.PresentedValue("server-rotation-0","color");
	Check(current && Near(static_cast<float>(current->data[0]),.89f,.01f) && other && Near(static_cast<float>(other->data[3]),.7f,.01f),
		"the current map is orange");
	Check(text("server-rotation-2","display") == "block" && text("server-rotation-3","display") == "none","only the rotation's maps show");
	const auto message = bounds("server-message"), rules = bounds("server-rule-0");
	Check(message.y+message.height <= rules.y+.5f,"the message stays above the rules");
	// A long map name clips before the header's trailing texts.
	Check(runtime.SetState({{"mp.title",std::string(60,'W')},{"mp.mode",std::string("Team Deathmatch")},{"mp.clock",std::string("9:48")},
		{"mp.score",std::string("MARINES 0 - 0 STROGG")}},error,7),"publish the header");
	runtime.Frame(viewport,7.1);
	const auto title = bounds("header-title"), mode = bounds("header-mode"), score = bounds("header-score");
	Check(title.x+title.width <= mode.x+.5f && mode.x+mode.width <= bounds("header-clock").x,"the title yields to the mode, clock and score");
	Check(score.x+score.width <= card.x+card.width-24+.5f,"the score keeps the card's inset");
	// Every way out hides the card at once and releases the softening over
	// 250 ms; the next activation brings it back.
	Check(runtime.RunEvent("release",8,effects,error),"the card closes");
	runtime.Frame(viewport,8.125);
	Check(text("chrome","display") == "none" && Near(number("scene-softfocus","backdrop-blur"),3.75f,.05f),"closed at once, releasing");
	runtime.Frame(viewport,8.3);
	Check(Near(number("scene-softfocus","backdrop-blur"),0,.01f),"released after 250 ms");
	Check(runtime.RunEvent("onActivate",9,effects,error) && runtime.RunEvent("open",9,effects,error),"the card opens again");
	runtime.Frame(viewport,9.3);
	Check(text("chrome","display") == "block" && Near(number("card","opacity"),1,.001f),"shown again");
	// Reduced motion: no rise, the softening and fade in 80 ms.
	host.reducedMotion = true;
	runtime.SetReducedMotion(true,10);
	Check(runtime.RunEvent("release",10,effects,error) && runtime.RunEvent("onActivate",10.5,effects,error) &&
		runtime.RunEvent("open",10.5,effects,error),"reopened under reduced motion");
	runtime.Frame(viewport,10.54);
	const auto still = runtime.PresentedValue("card","transform");
	Check(still && Near(static_cast<float>(still->data[1]),0,.01f),"no rise");
	Check(number("scene-softfocus","backdrop-blur") > 1 && number("scene-softfocus","backdrop-blur") < 7,"the softening ramps over 80 ms");
	runtime.Frame(viewport,10.59);
	Check(Near(number("card","opacity"),1,.001f) && Near(number("scene-softfocus","backdrop-blur"),7.5f,.01f),"in within 80 ms");
	host.reducedMotion = false;
	runtime.SetReducedMotion(false,11);
	host.softFocus = false;
}

// The Escape card's Vote page (section 14.18): the running vote first, then
// the rows of the call the drafted game type uses, a locked one dimmed, each
// row's control requesting its verb with its new value, and Call Vote.
static void CheckVote(ScreenHost& host, const char* path) {
	const auto source = Read(path);
	Runtime runtime(host);
	std::vector<Diagnostic> diagnostics;
	Check(runtime.Initialize() && runtime.LoadDocument(source,"guis/menu/mp_escape.q4ui",diagnostics),"the Escape card loads for its Vote page");
	{
		// The Vote and Admin pages' map choices hold the game's catalog of
		// 128 maps a game type (RETAINED_VOTE_MAPS).
		Document document; std::vector<Diagnostic> parsed;
		Check(document.Load(source,parsed),"the Escape card validates for its map choices");
		for (const char* id : {"vote-row-map","admin-row-map"}) {
			const auto* node = document.Model().FindNode(id);
			Check(node && node->control && std::get<ChoiceSpec>(node->control->widget).options.size() == 128,
				"each map choice holds 128 maps");
		}
	}
	Viewport viewport; viewport.canvasHeight = 720; viewport.width = 1280; viewport.height = 720;
	std::string error;
	Runtime::EventEffects effects;
	const auto bounds = [&](const std::string& id) { Bounds b; Check(runtime.GetBounds(id,b),"a vote node is laid out"); return b; };
	const auto text = [&](const std::string& node, const char* property) {
		const auto value = runtime.PresentedValue(node,property); Check(value.has_value(),"a presented keyword"); return value->text;
	};
	const auto number = [&](const std::string& node, const char* property) {
		const auto value = runtime.PresentedValue(node,property); Check(value.has_value(),"a presented value"); return static_cast<float>(value->data[0]);
	};
	// The value a control's request carries, resolved through its action.
	const auto request = [&](const char* verb) {
		const auto actions = runtime.TakeActions();
		Check(actions.size() == 1 && actions[0].action == verb && actions[0].proposal.has_value(),"a row requests its verb");
		ActionInvocation invocation;
		Check(runtime.ResolveAction(actions[0].action,invocation,error,&*actions[0].proposal) &&
			invocation.operation == "session.menuValue" && std::get<std::string>(invocation.arguments.at("command")) == verb,
			"with the value verb");
		if (std::string(verb)=="mpVoteKick") Check(std::get<std::string>(invocation.arguments.at("catalog"))=="0,1:4097,2:4098","kick row freezes its connection catalog");
		runtime.AcknowledgeControlProposal(actions[0].node,actions[0].proposalToken,true);
		return invocation.arguments.at("value");
	};
	const auto press = [&](MenuInput input, double at) { runtime.MenuAction(input,true,at); runtime.MenuAction(input,false,at+.01); };
	// An open list lays out the rows it shows; a row past its count takes no room.
	const auto Listed = [&](const char* option) { Bounds b; return runtime.GetBounds(option,b) && b.height > 0; };
	// No vote runs. The game publishes Team DM's rows (no capture, tournament
	// or control time limit), buying locked by the server, and Call Vote
	// unavailable until something changes.
	StateValues page = {{"mp.vote.running",false},{"mp.vote.call.shown",true},{"mp.vote.call.available",false},
		{"mp.vote.call.label",std::string("CALL VOTE")},{"mp.vote.call.reason",std::string("Change a setting to call a vote.")},
		{"mp.vote.map_count",3.0},{"mp.vote.map0",std::string("The Fragging Yard")},{"mp.vote.map1",std::string("Lost Fleet")},
		{"mp.vote.map2",std::string("Bloodwork")},{"mp.vote.map",1.0},{"mp.vote.gametype_count",12.0},
		{"mp.vote.gametype2",std::string("Team DM")},{"mp.vote.gametype",2.0},{"mp.vote.timelimit",10.0},{"mp.vote.fraglimit",20.0},
		{"mp.vote.balance",true},{"mp.vote.shuffle",false},{"mp.vote.restart",false},{"mp.vote.buying",false},
		{"mp.vote.kick_catalog",std::string("0,1:4097,2:4098")},{"mp.vote.kick_count",3.0},{"mp.vote.kick0",std::string("Anderson")},{"mp.vote.kick1",std::string("Rhodes")},{"mp.vote.kick",-1.0},
		{"mp.vote.locked",true}};
	const char* const keys[] = {"map","gametype","timelimit","fraglimit","capturelimit","tourneylimit","controltime","balance","shuffle",
		"restart","buying","kick"};
	for (const char* key : keys) {
		const std::string field = key;
		page["mp.vote."+field+".row_shown"] = field != "capturelimit" && field != "tourneylimit" && field != "controltime";
		page["mp.vote."+field+".row_allowed"] = field != "buying";
	}
	Check(runtime.SetState(page,error,1),"publish the Vote page");
	Check(runtime.RunEvent("open",1,effects,error) && runtime.RunEvent("tab_vote",1.01,effects,error),"open on the Vote tab");
	runtime.Frame(viewport,1.4);
	Check(text("vote-none","display") == "block" && text("vote-caller","display") == "none" && text("vote-yes-slot","display") == "none" &&
		text("vote-no-slot","display") == "none","with no vote running, the page says so and offers no ballot");
	Check(runtime.FocusedControl() == "vote-row-map","and opens on its first row");
	// The rows the drafted game type uses stack in order without gaps above
	// Call Vote; the locked one dims with its lock, and the heading says why.
	float previous = 0;
	int shown = 0;
	for (const char* key : keys) {
		const std::string row = std::string("vote-row-")+key;
		if (!std::get<bool>(page.at("mp.vote."+std::string(key)+".row_shown"))) {
			Check(text(row,"display") == "none","a row the drafted game type does not use is hidden");
			continue;
		}
		const auto box = bounds(row);
		Check(text(row,"display") == "block" && Near(box.height,26,.01f) && (!shown || Near(box.y,previous,.01f)),"shown rows stack");
		previous = box.y+box.height; ++shown;
	}
	Check(shown == 9 && previous <= bounds("vote-call-slot").y+.5f,"nine rows fit above Call Vote");
	Check(Near(number("vote-row-buying","opacity"),.38f,.001f) && text("vote-row-buying-lock","display") == "block" &&
		Near(number("vote-row-map","opacity"),1,.001f) && text("vote-row-map-lock","display") == "none","a locked row dims with its lock");
	Check(text("vote-locked","display") == "block" && !runtime.FocusControl("vote-row-buying",1.41),"it says why and takes no focus");
	const auto left = bounds("vote-heading-running"), right = bounds("vote-heading-call");
	Check(left.x+left.width <= right.x && bounds("vote-row-map").x >= right.x-.5f,"the running vote stands beside the call");
	// Call Vote has nothing to call yet: it shakes and asks nothing.
	Check(text("vote-call-reason","display") == "block" && text("vote-call-lock","display") == "block","Call Vote says why it waits");
	Check(runtime.RunEvent("vote_call",1.5,effects,error) && effects.actions.empty(),"and asks nothing");
	// Each row's control requests its verb with its new value: Right steps a
	// limit, accept checks a box, and a list unfolds its game's rows.
	Check(runtime.FocusControl("vote-row-timelimit",1.6),"focus the time limit");
	press(MenuInput::Right,1.61);
	Check(std::get<double>(request("mpVoteTimeLimit")) == 11,"Right asks for one more minute");
	Check(runtime.FocusControl("vote-row-shuffle",1.7),"focus shuffle");
	press(MenuInput::Accept,1.71);
	Check(std::get<bool>(request("mpVoteShuffle")),"accept asks to shuffle");
	Check(runtime.FocusControl("vote-row-map",1.8) && runtime.OpenChoicePopup("vote-row-map",1.8),"the map list unfolds");
	runtime.Frame(viewport,1.85);
	Check(Listed("vote-row-map-option-2") && !Listed("vote-row-map-option-3"),"it lists the game's maps");
	const auto popup = bounds("vote-row-map-popup"), card = bounds("card");
	Check(popup.y >= bounds("vote-row-map").y && popup.y+popup.height <= card.y+card.height,"under its row, inside the card");
	press(MenuInput::Down,1.9);
	press(MenuInput::Accept,1.95);
	Check(std::get<double>(request("mpVoteMap")) == 2,"choosing a map asks for its row");
	Check(runtime.FocusControl("vote-row-kick",2) && runtime.OpenChoicePopup("vote-row-kick",2),"the kick list unfolds");
	runtime.Frame(viewport,2.05);
	Check(Listed("vote-row-kick-option-0") && Listed("vote-row-kick-option-2") && !Listed("vote-row-kick-option-3"),
		"no one, then the other players");
	press(MenuInput::Down,2.1);
	press(MenuInput::Accept,2.15);
	Check(std::get<double>(request("mpVoteKick")) == 0,"choosing a player asks for their row");
	press(MenuInput::Right,2.2); const auto queued=runtime.TakeActions();
	Check(queued.size()==1 && runtime.CanDispatchControlAction(queued[0],2.21),"current kick choice can dispatch");
	Check(runtime.SetState({{"mp.vote.kick_catalog",std::string("0,1:8193,2:4098")}},error,2.22),"replace same-name player in catalog");
	Check(!runtime.CanDispatchControlAction(queued[0],2.23),"queued production kick choice cannot follow replaced player");
	Check(runtime.SetState({{"mp.vote.kick_catalog",std::string("0,1:4097,2:4098")}},error,2.24),"restore catalog text");
	Check(!runtime.CanDispatchControlAction(queued[0],2.25),"catalog ABA never restores old action authority");
	Check(runtime.OpenChoicePopup("vote-row-kick",2.3),"reopen kick choice"); runtime.Frame(viewport,2.31);
	runtime.MenuAction(MenuInput::Accept,true,2.32);
	Check(runtime.SetState({{"mp.vote.kick_catalog",std::string("0,1:12289,2:4098")}},error,2.33),"replace player during held choice");
	runtime.MenuAction(MenuInput::Accept,false,2.34); runtime.Frame(viewport,2.35);
	Check(runtime.TakeActions().empty() && !runtime.GetWidgetState("vote-row-kick")->popupOpen,"production popup closes and discards held choice");
	// A vote runs: who called it, its lines, the time left and the tally, and
	// Yes and No with the keys bound to them; the page opens on Yes.
	Check(runtime.SetState({{"mp.vote.running",true},{"mp.vote.caller",std::string("CALLED BY Anderson")},
		{"mp.vote.line0",std::string("MAP Lost Fleet")},{"mp.vote.line1",std::string("TIME LIMIT 15")},{"mp.vote.line_count",2.0},
		{"mp.vote.time",std::string("TIME LEFT 0:27")},{"mp.vote.tally",std::string("1 YES VOTES, 0 NO VOTES")},
		{"mp.vote.yes.shown",true},{"mp.vote.yes.available",true},{"mp.vote.yes.label",std::string("VOTE YES")},
		{"mp.keys.vote_yes",std::string("F1")},{"mp.keys.vote_yes_bound",true},{"mp.vote.no.shown",true},{"mp.vote.no.available",true},
		{"mp.vote.no.label",std::string("VOTE NO")},{"mp.keys.vote_no",std::string("F2")},{"mp.keys.vote_no_bound",true},
		{"mp.vote.call.reason",std::string("There is already a vote in progress")}},error,3),"a vote runs");
	Check(runtime.RunEvent("tab_team",3.01,effects,error) && runtime.RunEvent("tab_vote",3.02,effects,error),"back to the Vote tab");
	runtime.Frame(viewport,3.3);
	Check(text("vote-none","display") == "none" && text("vote-caller","display") == "block" && text("vote-line-1","display") == "block" &&
		text("vote-line-2","display") == "none" && text("vote-time","display") == "block" && text("vote-tally","display") == "block",
		"the caller, the vote's lines, the time left and the tally");
	Check(runtime.FocusedControl() == "vote-yes" && text("vote-yes-keycap","display") == "block" && text("vote-no-keycap","display") == "block",
		"the page opens on Yes, and each ballot shows its key");
	const auto yes = bounds("vote-yes-label"), keycap = bounds("vote-yes-keycap"), plate = bounds("vote-yes");
	Check(keycap.x >= yes.x+yes.width && keycap.x+keycap.width <= plate.x+plate.width,"the key follows the label on the plate");
	const auto lines = bounds("vote-tally"), ballots = bounds("vote-yes-slot");
	Check(lines.y+lines.height <= ballots.y && bounds("vote-no-slot").y >= ballots.y+45,"the ballots follow the tally");
	Check(runtime.RunEvent("vote_yes",3.4,effects,error) && effects.actions.size() == 1 &&
		std::get<std::string>(effects.actions[0].arguments.at("command")) == "mpVoteYes","Yes asks the game");
	// Once the player has voted the ballots lock and say why; the keys go.
	Check(runtime.SetState({{"mp.vote.yes.available",false},{"mp.vote.no.available",false},
		{"mp.vote.yes.reason",std::string("You have voted.")},{"mp.vote.no.reason",std::string("You have voted.")}},error,3.5),"voted");
	runtime.Frame(viewport,3.6);
	Check(text("vote-yes-keycap","display") == "none" && text("vote-yes-lock","display") == "block" && text("vote-no-reason","display") == "block",
		"a ballot the player cannot cast locks and says why");
	Check(runtime.RunEvent("vote_no",3.7,effects,error) && effects.actions.empty(),"and asks nothing");
	// Another tab takes the page's input away.
	Check(runtime.RunEvent("tab_team",3.8,effects,error),"to the Team tab");
	runtime.Frame(viewport,4.1);
	Check(!runtime.CanActivateControl("vote-row-map",4.1) && !runtime.CanActivateControl("vote-call",4.1),"the Vote page takes no input");
}

// The Escape card's Match page (section 14.18): Match Control's six sections
// as an inner strip the triggers page; Status with the game's states and its
// actions, each asking the game by its token's index and saying why it is
// unavailable; the confirmations; and the sections still handing off.
// The Escape card's Admin page (section 14.18): the remote console password
// first, then Players, Server and Console under their own strip, each asking
// the session through its mpAdmin verbs.
static void CheckAdmin(ScreenHost& host, const char* path) {
	const auto source = Read(path);
	Runtime runtime(host);
	std::vector<Diagnostic> diagnostics;
	const bool loaded = runtime.Initialize() && runtime.LoadDocument(source,"guis/menu/mp_escape.q4ui",diagnostics);
	for (const auto& diagnostic : diagnostics) std::fprintf(stderr,"mp_escape %s: %s\n",diagnostic.pointer.c_str(),diagnostic.message.c_str());
	Check(loaded,"the Escape card loads for its Admin page");
	Viewport viewport; viewport.canvasHeight = 720; viewport.width = 1280; viewport.height = 720;
	std::string error;
	Runtime::EventEffects effects;
	const auto text = [&](const std::string& node, const char* property) {
		const auto value = runtime.PresentedValue(node,property);
		Check(value.has_value(),("a presented keyword: "+node+"."+property).c_str()); return value->text;
	};
	const auto bounds = [&](const std::string& id) { Bounds b; Check(runtime.GetBounds(id,b),"an Admin node is laid out"); return b; };
	const auto asked = [&](const char* verb) {
		return effects.actions.size() == 1 && std::get<std::string>(effects.actions[0].arguments.at("command")) == verb;
	};
	Check(runtime.RunEvent("open",1,effects,error) && runtime.RunEvent("tab_admin",1.01,effects,error),"open on the Admin tab");
	runtime.Frame(viewport,1.4);
	// Until the player is an admin: the server, the masked password and ACCEPT.
	Check(text("admin-pass","display") == "block" && text("admin-live","display") == "none","the password first");
	Check(runtime.FocusedControl() == "admin-password","the page opens on the password");
	Check(runtime.RunEvent("admin_pass_entered",1.5,effects,error) && asked("mpAdminPass"),"Enter checks the password");
	Check(runtime.RunEvent("admin_pass_accept",1.55,effects,error) && asked("mpAdminPass"),"ACCEPT checks the entered password");
	Check(runtime.SetState({{"mp.admin.checking",true},{"mp.admin.notice",std::string("Checking the password...")}},error,1.6),"checking");
	runtime.Frame(viewport,1.65);
	Check(text("admin-checking","display") == "block" && text("admin-refused","display") == "none","it says it is checking");
	Check(runtime.SetState({{"mp.admin.checking",false},{"mp.admin.notice",std::string("Bad rcon password.")}},error,1.7),"refused");
	runtime.Frame(viewport,1.75);
	Check(text("admin-checking","display") == "none" && text("admin-refused","display") == "block" &&
		text("admin-refused","text") == "Bad rcon password.","it says the server refused it");
	// An admin: the players by client number, Kick and Ban unavailable until one is chosen.
	StateValues admin = {{"mp.admin.valid",true},{"mp.admin.notice",std::string()},{"mp.admin.player.count",3.0},
		{"mp.admin.player0.c0",std::string("Kane")},{"mp.admin.player0.c1",std::string("MARINES")},{"mp.admin.player0.client",0.0},
		{"mp.admin.player1.c0",std::string("Anderson")},{"mp.admin.player1.c1",std::string("STROGG")},{"mp.admin.player1.client",4.0},
		{"mp.admin.player1.identity",std::string("8196")},
		{"mp.admin.player2.c0",std::string("Rhino")},{"mp.admin.player2.c1",std::string("MARINES")},{"mp.admin.player2.client",9.0},
		{"mp.admin.player.selected",-1.0}};
	for (const char* action : {"kick","ban","switch","apply"}) {
		const std::string key = std::string("mp.admin.")+action;
		admin[key+".shown"] = true; admin[key+".available"] = false; admin[key+".label"] = std::string(action);
		admin[key+".reason"] = std::string("Choose a player first.");
	}
	Check(runtime.SetState(admin,error,2),"an admin");
	runtime.Frame(viewport,2.3);
	Check(text("admin-pass","display") == "none" && text("admin-live","display") == "block" &&
		text("admin-section-players","display") == "block" && text("admin-section-server","display") == "none","the Players section");
	Check(text("admin-player-2","display") == "block" && text("admin-player-3","display") == "none" &&
		text("admin-player-1-c0","text") == "Anderson" && text("admin-player-1-c1","text") == "STROGG","the players and their teams");
	const auto list = bounds("admin-players"), kick = bounds("admin-kick");
	Check(kick.x >= list.x+list.width,"the actions stand beside the list");
	Check(runtime.RunEvent("admin_player_1",2.4,effects,error) && asked("mpAdminSelect") &&
		std::get<double>(runtime.GetState().at("card.admin_row")) == 1,"a row names itself to the session");
	Check(std::get<double>(effects.actions[0].arguments.at("client")) == 4 &&
		std::get<std::string>(effects.actions[0].arguments.at("identity")) == "8196","the row captures its connection");
	Check(runtime.RunEvent("admin_kick",2.5,effects,error) && effects.actions.empty(),"an unavailable Kick asks nothing");
	runtime.Frame(viewport,2.55);
	Check(text("admin-kick-reason","display") == "block","and says why");
	Check(runtime.SetState({{"mp.admin.player.selected",1.0},{"mp.admin.kick.available",true},{"mp.admin.ban.available",true},
		{"mp.admin.switch.available",true},{"mp.admin.client",4.0},{"mp.admin.identity",std::string("8196")}},error,2.6),"a player chosen");
	runtime.Frame(viewport,2.7);
	Check(text("admin-player-1-chosen","display") == "block" && text("admin-player-0-chosen","display") == "none","the chosen row lights");
	Check(runtime.RunEvent("admin_switch",2.8,effects,error) && asked("mpAdminSwitch"),"Force team switch asks the session at once");
	Check(std::get<double>(effects.actions[0].arguments.at("client")) == 4 &&
		std::get<std::string>(effects.actions[0].arguments.at("identity")) == "8196","admin actions capture the selected connection");
	// Neither a row nor an armed player action follows a new occupant.
	// A later fresh press remains usable; this checks the production document's
	// contexts, not just the generic interaction implementation.
	double targetTime = 2.81;
	for (const char* control : {"admin-player-1","admin-kick","admin-ban","admin-switch"}) {
		const char* context = std::string(control) == "admin-player-1" ? "mp.admin.player1.identity" : "mp.admin.identity";
		Check(runtime.SetState({{context,std::string("8196")}},error,targetTime),"restore the original Admin target");
		Check(runtime.FocusControl(control,targetTime),"focus the Admin target control");
		runtime.TakeActions(); runtime.MenuAction(MenuInput::Accept,true,targetTime+.001);
		Check(runtime.SetState({{context,std::string("12292")}},error,targetTime+.002),"replace the Admin target while held");
		runtime.MenuAction(MenuInput::Accept,false,targetTime+.003);
		Check(runtime.TakeActions().empty(),"target replacement cancels the held Admin control");
		runtime.MenuAction(MenuInput::Accept,true,targetTime+.004);
		runtime.MenuAction(MenuInput::Accept,false,targetTime+.005);
		// A fresh press activates the control (Kick's and Ban's open their
		// confirmation when the activation runs, checked above).
		Check(runtime.TakeActions().size() == 1,"a fresh Admin activation still works");
		targetTime += .03;
	}
	// View ban list asks for the server's list and opens PLAYER BAN LIST.
	Check(runtime.RunEvent("admin_bans",3,effects,error) && effects.actions.size() == 1 &&
		std::get<std::string>(effects.actions[0].arguments.at("command")) == "mpAdminBans","View ban list asks for the list");
	runtime.Frame(viewport,3.4);
	Check(text("banModal","display") == "block" && runtime.GetControlState("banModal_yes") == ControlState::Disabled,
		"PLAYER BAN LIST opens with nothing to remove");
	Check(runtime.SetState({{"mp.admin.ban_count",2.0},{"mp.admin.ban0.name",std::string("Griefer")},{"mp.admin.ban0.guid",std::string("A1B2")},
		{"mp.admin.ban1.name",std::string("Spammer")},{"mp.admin.ban1.guid",std::string("C3D4")}},error,3.5),"two bans");
	runtime.Frame(viewport,3.55);
	Check(text("ban-row-1","display") == "block" && text("ban-row-1-name","text") == "Spammer","the server's bans list");
	Check(runtime.RunEvent("banRow1",3.6,effects,error) && effects.actions.size() == 1 && effects.actions[0].action == "mpAdminBanPick" &&
		std::get<double>(runtime.GetState().at("card.admin_ban_pick")) == 1,"a ban's row names itself");
	Check(runtime.SetState({{"mp.admin.ban_selected",1.0}},error,3.7) && runtime.RunEvent("banRemoveConfirmed",3.75,effects,error) &&
		asked("mpAdminUnban"),"REMOVE BAN lifts the chosen ban");
	Check(runtime.RunEvent("banModalHide",3.8,effects,error),"CLOSE");
	runtime.Frame(viewport,4.3);
	// Server: the drafted settings the game type uses, and Apply changes.
	Check(runtime.RunEvent("admin_section_server",4.4,effects,error),"to the Server section");
	StateValues server = {{"mp.admin.map.row_shown",true},{"mp.admin.gametype.row_shown",true},{"mp.admin.timelimit.row_shown",true},
		{"mp.admin.fraglimit.row_shown",true},{"mp.admin.capturelimit.row_shown",false},{"mp.admin.balance.row_shown",false},
		{"mp.admin.restart.row_shown",true},{"mp.admin.buying.row_shown",false},{"mp.admin.map_count",2.0},
		{"mp.admin.map0",std::string("Campgrounds Redux")},{"mp.admin.map1",std::string("The Edge")},{"mp.admin.map",0.0},
		{"mp.admin.gametype_count",2.0},{"mp.admin.gametype0",std::string("Deathmatch")},{"mp.admin.gametype1",std::string("Tourney")},
		{"mp.admin.gametype",0.0},{"mp.admin.timelimit",10.0},{"mp.admin.fraglimit",25.0}};
	Check(runtime.SetState(server,error,4.5),"a deathmatch server");
	runtime.Frame(viewport,4.8);
	Check(text("admin-section-server","display") == "block" && text("admin-section-players","display") == "none","the Server section");
	Check(text("admin-row-fraglimit","display") == "block" && text("admin-row-capturelimit","display") == "none" &&
		text("admin-row-restart","display") == "block" && text("admin-row-balance","display") == "none","the rows the game type uses");
	// A row's control requests its verb with its new value: Right steps the
	// drafted frag limit.
	const auto press = [&](MenuInput input, double at) { runtime.MenuAction(input,true,at); runtime.MenuAction(input,false,at+.01); };
	Check(runtime.FocusControl("admin-row-fraglimit",4.82),"focus the frag limit");
	press(MenuInput::Right,4.83);
	const auto actions = runtime.TakeActions();
	Check(actions.size() == 1 && actions[0].action == "mpAdminFragLimit" && actions[0].proposal.has_value(),"the row requests its verb");
	ActionInvocation invocation;
	Check(runtime.ResolveAction(actions[0].action,invocation,error,&*actions[0].proposal) && invocation.operation == "session.menuValue" &&
		std::get<double>(invocation.arguments.at("value")) == 26,"Right asks for one more frag");
	runtime.AcknowledgeControlProposal(actions[0].node,actions[0].proposalToken,true);
	Check(runtime.RunEvent("admin_apply",4.9,effects,error) && effects.actions.empty(),"Apply changes asks nothing while nothing changes");
	Check(runtime.SetState({{"mp.admin.apply.available",true}},error,5) && runtime.RunEvent("admin_apply",5.05,effects,error) &&
		asked("mpAdminApply"),"Apply changes sends the draft");
	// The Menu button presses Apply while the Server section has changes, and
	// otherwise closes the card as Back does (section 13.5).
	Check(runtime.RunEvent("onMenuButton",5.06,effects,error) && asked("mpAdminApply"),"Menu applies the server's changes");
	Check(runtime.SetState({{"mp.admin.apply.available",false}},error,5.07) && runtime.RunEvent("onMenuButton",5.08,effects,error) &&
		asked("mpClose"),"with nothing to apply Menu closes the card");
	Check(runtime.SetState({{"mp.admin.apply.available",true}},error,5.09),"changes to apply again");
	// Apply changes ends with the controller's Menu button, which presses it;
	// the keyboard has no key for it and shows none.
	runtime.Frame(viewport,5.1);
	for (const char* family : {"xbox","playstation","nintendo","steamdeck","generic"})
		Check(text(std::string("admin-apply-menu-")+family,"display") == "none","no Menu glyph for the keyboard");
	Check(runtime.SetState({{"prompt.glyphs",3.0}},error,5.11),"a Nintendo controller");
	runtime.Frame(viewport,5.12);
	const auto plate = bounds("admin-apply"), menuGlyph = bounds("admin-apply-menu"), applyLabel = bounds("admin-apply-label");
	Check(text("admin-apply-menu-nintendo","display") == "block" && text("admin-apply-menu-xbox","display") == "none" &&
		menuGlyph.x+menuGlyph.width <= plate.x+plate.width && applyLabel.x+applyLabel.width <= menuGlyph.x,
		"the Nintendo Menu glyph ends the Apply plate, after its label");
	Check(runtime.SetState({{"prompt.glyphs",0.0}},error,5.13),"the keyboard again");
	const auto apply = bounds("admin-apply"), page = bounds("page-admin");
	Check(apply.y+apply.height <= page.y+page.height+.5f,"Apply changes stands inside the page");
	// Console: the newest lines and the command field.
	Check(runtime.RunEvent("admin_section_console",5.2,effects,error) &&
		runtime.SetState({{"mp.admin.console0",std::string("]status")},{"mp.admin.console1",std::string("map: mp/q4dm1")}},error,5.25),
		"the console's lines");
	runtime.Frame(viewport,5.5);
	Check(text("admin-section-console","display") == "block" && text("admin-console-1","text") == "map: mp/q4dm1","the console shows them");
	Check(runtime.RunEvent("admin_command_entered",5.6,effects,error) && asked("mpAdminConsole"),"Enter runs the command");
	Check(runtime.RunEvent("onMenuButton",5.61,effects,error) && asked("mpClose"),"away from the Server section Menu closes the card");
	Check(runtime.SetState({{"prompt.glyphs",2.0}},error,5.62),"a PlayStation controller");
	runtime.Frame(viewport,5.65);
	const auto adminStrip = bounds("admin-strip"), adminTriggers = bounds("admin-triggers"), console = bounds("admin-tab-console");
	Check(text("admin-triggers-lt-playstation","display") == "block" && adminTriggers.x+adminTriggers.width <= adminStrip.x+adminStrip.width+.5f &&
		console.x+console.width <= adminTriggers.x,"the Admin strip ends with the PlayStation triggers");
	Check(runtime.SetState({{"prompt.glyphs",0.0}},error,5.66),"the keyboard again");
	// The triggers step the sections, wrapping.
	Check(runtime.RunEvent("onSectionNext",5.7,effects,error) && std::get<double>(runtime.GetState().at("card.admin_section")) == 0,
		"the next trigger wraps to Players");
	Check(runtime.RunEvent("onSectionPrevious",5.8,effects,error) && std::get<double>(runtime.GetState().at("card.admin_section")) == 2,
		"the previous trigger wraps to Console");
	Check(runtime.RunEvent("admin_section_players",5.9,effects,error),"back to the Players section");
	runtime.Frame(viewport,5.95);
	// Back on Players with the player chosen: Kick and Ban ask first in the stock confirmation, naming the player the
	// dialog captured; Yes acts on that player even if the selection changed
	// beneath it, No asks nothing.
	Check(runtime.SetState({{"mp.admin.kick.detail",std::string("Anderson")},{"mp.admin.ban.detail",std::string("Anderson")},
		{"mp.admin.client",4.0},{"mp.admin.identity",std::string("8196")}},error,6.0),
		"the chosen player's name");
	double confirmTime = 6.0;
	for (const auto& [event,modal,verb] : std::vector<std::tuple<std::string,std::string,std::string>>{
		{"admin_kick","adminKickModal","mpAdminKick"},{"admin_ban","adminBanModal","mpAdminBan"}}) {
		Check(runtime.RunEvent(event,confirmTime,effects,error) && effects.actions.empty(),"Kick and Ban ask first");
		Check(std::get<bool>(runtime.GetState().at(modal+".visible")) &&
			std::get<std::string>(runtime.GetState().at("card.admin_confirm_name")) == "Anderson","the confirmation names the player");
		runtime.Frame(viewport,confirmTime+.3);
		Check(text(modal+"-name","text") == "Anderson" && text(modal,"display") == "block","the dialog shows the name");
		Check(runtime.RunEvent(modal+"Hide",confirmTime+.31,effects,error) && effects.actions.empty() &&
			!std::get<bool>(runtime.GetState().at(modal+".contents")),"No asks nothing");
		runtime.Frame(viewport,confirmTime+.8);
		Check(runtime.RunEvent(event,confirmTime+.81,effects,error) && effects.actions.empty(),"asked again");
		Check(runtime.SetState({{"mp.admin.client",9.0},{"mp.admin.identity",std::string("16388")}},error,confirmTime+.82),
			"the selection changes beneath the dialog");
		Check(runtime.RunEvent(modal+"Yes",confirmTime+.83,effects,error) && asked(verb.c_str()),"Yes asks the session");
		Check(std::get<double>(effects.actions[0].arguments.at("client")) == 4 &&
			std::get<std::string>(effects.actions[0].arguments.at("identity")) == "8196","Yes acts on the player the dialog named");
		Check(runtime.SetState({{"mp.admin.client",4.0},{"mp.admin.identity",std::string("8196")}},error,confirmTime+.84),"restore the selection");
		runtime.Frame(viewport,confirmTime+1.3);
		confirmTime += 1.4;
	}
	// A long list: reopening the page on Players brings the chosen player's
	// row into view and focuses it.
	{
		StateValues many = {{"mp.admin.player.count",30.0},{"mp.admin.player.selected",28.0},{"card.admin_section",0.0}};
		for (int row = 0; row < 30; ++row) {
			const std::string key = "mp.admin.player"+std::to_string(row);
			many[key+".c0"] = std::string("Player ")+std::to_string(row+1); many[key+".client"] = static_cast<double>(row);
			many[key+".identity"] = std::to_string(4096+row);
		}
		Check(runtime.SetState(many,error,9.0),"thirty players, the 29th chosen");
		Check(runtime.RunEvent("tab_players",9.05,effects,error) && runtime.RunEvent("tab_admin",9.4,effects,error),"reopen the Admin page");
		runtime.Frame(viewport,9.8);
		runtime.Frame(viewport,9.9);
		const auto list = bounds("admin-players"), row = bounds("admin-player-28");
		Check(runtime.FocusedControl() == "admin-player-28","the chosen player's row takes the focus");
		Check(row.y >= list.y-.5f && row.y+row.height <= list.y+list.height+.5f,"and stands inside the list");
	}
	Check(host.errors == 0,"no runtime diagnostics across the Admin page");
}

// Section 13.4: the home menus wrap. Up from the top runs on to the
// plinth's links and Down from them back to the top, Left and Right wrap
// along the plinth, and CONTINUE without a save passes the move on.
static void CheckMenuWrap(ScreenHost& host, const char* titlePath, const char* pausePath, const char* restartPath = nullptr) {
	Viewport viewport; viewport.canvasHeight = 720; viewport.width = 1280; viewport.height = 720;
	std::string error; double t = 1;
	const auto press = [&](Runtime& runtime, MenuInput input) {
		runtime.MenuAction(input,true,t); runtime.MenuAction(input,false,t+.01); t += .05; runtime.Frame(viewport,t);
	};
	{
		Runtime runtime(host); std::vector<Diagnostic> diagnostics;
		Check(runtime.Initialize() && runtime.LoadDocument(Read(titlePath),"guis/menu/title.q4ui",diagnostics),"the title loads for its wrap");
		runtime.Frame(viewport,t);
		Check(runtime.FocusControl("nav_singleplayer",t+=.05),"focus SINGLE PLAYER without a save");
		runtime.Frame(viewport,t+=.05);
		press(runtime,MenuInput::Up);
		Check(runtime.FocusedControl() == "link_mods","Up from the top, CONTINUE hidden, wraps to the plinth's first link");
		press(runtime,MenuInput::Left);
		Check(runtime.FocusedControl() == "link_exit","Left wraps along the plinth");
		press(runtime,MenuInput::Right);
		Check(runtime.FocusedControl() == "link_mods","Right wraps back");
		press(runtime,MenuInput::Down);
		Check(runtime.FocusedControl() == "nav_singleplayer","Down from the plinth returns to the top, past the hidden CONTINUE");
		Check(runtime.SetState({{"menu_continue",true}},error,t),"a save to continue");
		runtime.Frame(viewport,t+=.05);
		press(runtime,MenuInput::Up);
		Check(runtime.FocusedControl() == "nav_continue","with a save Up reaches CONTINUE");
		press(runtime,MenuInput::Up);
		Check(runtime.FocusedControl() == "link_mods","and wraps on from it");
		press(runtime,MenuInput::Down);
		Check(runtime.FocusedControl() == "nav_continue","Down from the plinth reaches CONTINUE");
	}
	{
		Runtime runtime(host); std::vector<Diagnostic> diagnostics;
		Check(runtime.Initialize() && runtime.LoadDocument(Read(pausePath),"guis/menu/pause.q4ui",diagnostics),"the pause loads for its wrap");
		runtime.Frame(viewport,t+=.05);
		Check(runtime.FocusControl("nav_resume",t+=.05),"focus RESUME");
		runtime.Frame(viewport,t+=.05);
		press(runtime,MenuInput::Up);
		Check(runtime.FocusedControl() == "link_exit","Up from RESUME wraps to EXIT");
		press(runtime,MenuInput::Down);
		Check(runtime.FocusedControl() == "nav_resume","Down from EXIT wraps to RESUME");
		Check(runtime.FocusControl("nav_quit",t+=.05),"focus QUIT TO MENU");
		runtime.Frame(viewport,t+=.05);
		press(runtime,MenuInput::Down);
		Check(runtime.FocusedControl() == "link_exit","the last action steps down to EXIT");
		press(runtime,MenuInput::Up);
		Check(runtime.FocusedControl() == "nav_quit","and EXIT back up to it");
		// A dialog's two actions wrap: Left from YES reaches NO, Right from NO YES.
		Runtime::EventEffects effects;
		Check(runtime.RunEvent("quitModalShow",t+=.05,effects,error) && runtime.RunEvent("quitModalShown",t+=.05,effects,error),
			"open QUIT TO MENU's dialog");
		runtime.Frame(viewport,t+=.05);
		Check(runtime.FocusControl("quitModal_yes",t+=.05),"focus YES");
		runtime.Frame(viewport,t+=.05);
		press(runtime,MenuInput::Left);
		Check(runtime.FocusedControl() == "quitModal_no","Left from YES wraps to NO");
		press(runtime,MenuInput::Right);
		Check(runtime.FocusedControl() == "quitModal_yes","Right from NO wraps to YES");
	}
	if (restartPath != nullptr) {
		Runtime runtime(host); std::vector<Diagnostic> diagnostics;
		Check(runtime.Initialize() && runtime.LoadDocument(Read(restartPath),"guis/menu/restartlevel.q4ui",diagnostics),
			"Restart Level loads for its wrap");
		runtime.Frame(viewport,t+=.05);
		Check(runtime.FocusControl("skill-0",t+=.05),"focus the first difficulty");
		runtime.Frame(viewport,t+=.05);
		press(runtime,MenuInput::Up);
		Check(runtime.FocusedControl() == "restart_go","Up from the first difficulty wraps to RESTART LEVEL");
		press(runtime,MenuInput::Down);
		Check(runtime.FocusedControl() == "skill-0","Down from RESTART LEVEL returns to the first difficulty");
		Check(runtime.FocusControl("skill-4",t+=.05),"focus the last difficulty");
		runtime.Frame(viewport,t+=.05);
		press(runtime,MenuInput::Down);
		Check(runtime.FocusedControl() == "restart_go","the last difficulty steps down to RESTART LEVEL");
	}
	Check(host.errors == 0,"no runtime diagnostics across the menu wraps");
}

static void CheckMatch(ScreenHost& host, const char* path) {
	const auto source = Read(path);
	Runtime runtime(host);
	std::vector<Diagnostic> diagnostics;
	Check(runtime.Initialize() && runtime.LoadDocument(source,"guis/menu/mp_escape.q4ui",diagnostics),"the Escape card loads for its Match page");
	Viewport viewport; viewport.canvasHeight = 720; viewport.width = 1280; viewport.height = 720;
	std::string error;
	Runtime::EventEffects effects;
	const auto text = [&](const std::string& node, const char* property) {
		const auto value = runtime.PresentedValue(node,property);
		Check(value.has_value(),("a presented keyword: "+node+"."+property).c_str()); return value->text;
	};
	const auto number = [&](const std::string& node, const char* property) {
		const auto value = runtime.PresentedValue(node,property); Check(value.has_value(),"a presented value"); return static_cast<float>(value->data[0]);
	};
	const auto bounds = [&](const std::string& id) { Bounds b; Check(runtime.GetBounds(id,b),"a Match node is laid out"); return b; };
	const auto section = [&]() { return std::get<double>(runtime.GetState().at("card.match_section")); };
	// The session's RETAINED_MP_MATCH_TOKENS, by index.
	enum { REFRESH, READY, TEAM_READY, ARM_FORCE_READY, TIMEOUT, TECH_PAUSE, RESUME, ARM_FORFEIT, ARM_ABORT, REFEREE_LOGOUT,
		SIDE_A, SIDE_B, FOLLOW_PREV, FOLLOW_NEXT, FOLLOW_FREE, CONFIRM, CANCEL_CONFIRM, JOIN_MARINE, JOIN_STROGG, SPECTATE,
		QUEUE_JOIN, QUEUE_DEFER, QUEUE_LEAVE, ROSTER_ACCEPT, ROSTER_LEAVE, ROSTER_INVITE, ARM_ROSTER_REMOVE, ARM_ROSTER_SUBSTITUTE,
		ROLE_ASSIGN, TEAM_LOCK, BROADCASTER, ARM_PARTICIPANT_REMOVE, CONTESTANT_BIND, PROPOSAL_CREATE, PROPOSAL_YES, PROPOSAL_NO,
		PROPOSAL_ABSTAIN, PROPOSAL_CANCEL, RULES_SELECT_PROFILE, RULES_STAGE, ARM_RULES_COMMIT, RULES_DISCARD, SERIES_STAGE,
		ARM_SERIES_START, ARM_SERIES_CANCEL, ARM_SERIES_ADVANCE, ARM_VETO_BAN, ARM_VETO_PICK, ARM_VETO_DECIDER, ARM_VETO_SIDE_MARINE,
		ARM_VETO_SIDE_STROGG };
	const auto press = [&](MenuInput input, double at) { runtime.MenuAction(input,true,at); runtime.MenuAction(input,false,at+.01); };
	const auto asked = [&](int token) {
		return effects.actions.size() == 1 && std::get<std::string>(effects.actions[0].arguments.at("command")) == "mpMatch" &&
			std::get<double>(runtime.GetState().at("card.match_op")) == token;
	};
	// Before the game has an accepted view the page waits for one; opening it
	// asks the game to project its view again.
	Check(runtime.RunEvent("open",1,effects,error),"open the card");
	Check(runtime.RunEvent("tab_match",1.01,effects,error) && asked(REFRESH),"opening the Match page asks for a refresh");
	runtime.Frame(viewport,1.4);
	Check(text("match-waiting","display") == "block" && text("match-status-live","display") == "none","with no view the page waits");
	Check(runtime.FocusedControl() == "match-tab-status" && text("match-section-status","display") == "block" &&
		text("match-section-teams","display") == "none","the page opens on its Status section");
	// The triggers' glyphs end the strip, for a controller only (section 13.5).
	for (const char* family : {"xbox","playstation","nintendo","steamdeck","generic"})
		Check(text(std::string("match-triggers-lt-")+family,"display") == "none" &&
			text(std::string("match-triggers-rt-")+family,"display") == "none","no trigger glyphs for the keyboard");
	Check(runtime.SetState({{"prompt.glyphs",1.0}},error,1.45),"an Xbox controller");
	runtime.Frame(viewport,1.5);
	Check(text("match-triggers-lt-xbox","display") == "block" && text("match-triggers-rt-xbox","display") == "block" &&
		text("match-triggers-lt-playstation","display") == "none","the Xbox triggers");
	const auto strip = bounds("match-strip"), triggers = bounds("match-triggers"), last = bounds("match-tab-evidence");
	Check(triggers.x+triggers.width <= strip.x+strip.width+.5f && last.x+last.width <= triggers.x,
		"the glyphs end the strip, after its last section");
	Check(runtime.SetState({{"prompt.glyphs",5.0}},error,1.55),"a generic controller");
	runtime.Frame(viewport,1.6);
	Check(text("match-triggers-rt-generic","display") == "block" && text("match-triggers-rt-xbox","display") == "none","generic triggers");
	Check(runtime.SetState({{"prompt.glyphs",0.0}},error,1.65),"the keyboard again");
	runtime.Frame(viewport,1.7);
	// A warm-up a referee steers: Set ready and Abort unavailable, the rest not.
	StateValues page = {{"mp.match.available",true},{"mp.match.phase",std::string("Warm-up | Round 1")},
		{"mp.match.status0",std::string("Live | Round inactive")},{"mp.match.status1",std::string("Running | No pause")},
		{"mp.match.status2",std::string("You: Player | Marine")},{"mp.match.result",std::string("Ready accepted.")},
		{"mp.match.side.shown",true},{"mp.match.side.label",std::string("Action target")},
		{"mp.match.side0.label",std::string("Marine")},{"mp.match.side0.available",true},{"mp.match.side0.selected",true},
		{"mp.match.side1.label",std::string("Strogg")},{"mp.match.side1.available",true},{"mp.match.side1.selected",false},
		{"mp.match.follow",false}};
	const char* const operations[] = {"ready","team_ready","timeout","tech_pause","resume","force_ready","forfeit","abort",
		"referee_login","referee_logout"};
	for (const char* name : operations) {
		const std::string key = std::string("mp.match.op.")+name, id = name;
		page[key+".shown"] = id != "referee_logout";
		page[key+".available"] = id != "ready" && id != "abort";
		page[key+".label"] = id == "ready" ? std::string("Set ready") : id;
		page[key+".reason"] = id == "ready" ? std::string("Readiness opens in the warm-up.") : id == "abort" ? std::string("Referees only.") :
			std::string("");
	}
	Check(runtime.SetState(page,error,2),"publish the Match page");
	runtime.Frame(viewport,2.3);
	Check(text("match-waiting","display") == "none" && text("match-status-live","display") == "block","the page shows the match");
	Check(text("match-phase","text") == "Warm-up | Round 1" && text("match-status-0","text") == "Live | Round inactive" &&
		text("match-status-2","text") == "You: Player | Marine" && text("match-result","text") == "Ready accepted.",
		"its phase, state lines and last result");
	// The actions stack in a column beside the state, each in its row.
	const char* const rows[] = {"match-ready","match-team-ready","match-timeout","match-tech-pause","match-resume","match-force-ready",
		"match-forfeit","match-abort"};
	const auto column = bounds("match-status-column"), page_box = bounds("page-match");
	float previous = -1;
	for (const char* row : rows) {
		const auto box = bounds(row);
		Check(box.y > previous && box.x >= column.x+column.width && box.x+box.width <= page_box.x+page_box.width+0.5f &&
			box.y+box.height <= page_box.y+page_box.height+0.5f,"the actions stack beside the state, inside the page");
		previous = box.y;
	}
	Check(bounds("match-result").y >= bounds("match-abort").y+bounds("match-abort").height,"the result follows the actions");
	Check(Near(number("match-ready","opacity"),.6f,.001f) && text("match-ready-lock","display") == "block" &&
		text("match-ready-reason","display") == "block" && text("match-ready-reason","text") == "Readiness opens in the warm-up.",
		"an unavailable action dims with its lock and says why");
	Check(Near(number("match-timeout","opacity"),1,.001f) && text("match-timeout-lock","display") == "none" &&
		text("match-timeout-reason","display") == "none","an available one does not");
	const auto label = bounds("match-team-ready-label"), reason = bounds("match-ready-reason");
	Check(label.x+label.width <= reason.x,"the labels keep to their column");
	Check(runtime.RunEvent("match_ready",2.4,effects,error) && effects.actions.empty(),"an unavailable action asks nothing");
	Check(runtime.RunEvent("match_timeout",2.5,effects,error) && asked(TIMEOUT),"Request timeout asks the game");
	Check(runtime.RunEvent("match_team_ready",2.55,effects,error) && asked(TEAM_READY) &&
		runtime.RunEvent("match_tech_pause",2.56,effects,error) && asked(TECH_PAUSE) &&
		runtime.RunEvent("match_resume",2.57,effects,error) && asked(RESUME),"as do Team ready, Technical pause and Resume");
	// Forfeit arms the game's confirmation and asks first; Yes confirms it.
	Check(runtime.RunEvent("match_forfeit",2.6,effects,error) && asked(ARM_FORFEIT),"Forfeit arms the game's confirmation");
	runtime.Frame(viewport,2.9);
	Check(text("matchForfeitModal","display") == "block" && text("matchForfeitModal-body","text") == "#str_41798","and asks first");
	Check(runtime.RunEvent("matchForfeitModalYes",3,effects,error) && asked(CONFIRM),"Yes confirms it");
	runtime.Frame(viewport,3.4);
	Check(text("matchForfeitModal","display") == "none","and closes the modal");
	// Force ready asks too; No, as back does, cancels the game's confirmation.
	Check(runtime.RunEvent("match_force_ready",3.5,effects,error) && asked(ARM_FORCE_READY),"Force ready arms its confirmation");
	runtime.Frame(viewport,3.8);
	Check(text("matchForceReadyModal","display") == "block" && text("matchForceReadyModal-body","text") == "#str_41900","and asks first");
	Check(runtime.RunEvent("matchForceReadyModalNo",3.9,effects,error) && asked(CANCEL_CONFIRM),"No cancels it");
	runtime.Frame(viewport,4.3);
	Check(text("matchForceReadyModal","display") == "none","and closes the modal");
	Check(runtime.RunEvent("match_abort",4.4,effects,error) && effects.actions.empty(),"an unavailable Abort neither arms nor asks");
	runtime.Frame(viewport,4.7);
	Check(text("matchAbortModal","display") == "none","and no modal opens");
	// The side an action applies to.
	Check(text("match-side","display") == "block" && text("match-side-0-mark","display") == "block" &&
		text("match-side-1-mark","display") == "none" && text("match-side-1-label","text") == "Strogg","the action target, Marine chosen");
	Check(runtime.RunEvent("match_side_1",4.8,effects,error) && asked(SIDE_B),"choosing Strogg asks the game");
	Check(runtime.SetState({{"mp.match.side0.available",false}},error,4.9) && runtime.RunEvent("match_side_0",4.95,effects,error) &&
		effects.actions.empty(),"a side the player may not choose asks nothing");
	// Signing in takes the referee password in a masked field, whose Enter
	// signs in with it; signing out asks the game.
	runtime.Frame(viewport,5);
	Check(text("match-referee-login","display") == "block" && text("match-referee-logout","display") == "none","Sign in, not Sign out");
	Check(runtime.RunEvent("match_referee_entered",5.05,effects,error) && effects.actions.size() == 1 &&
		std::get<std::string>(effects.actions[0].arguments.at("command")) == "mpRefereeLogin","Enter signs in with the password");
	Check(runtime.SetState({{"mp.match.op.referee_login.available",false},{"mp.match.op.referee_login.reason",std::string("No referee password.")}},
		error,5.1),"no referee password on the server");
	runtime.Frame(viewport,5.15);
	Check(text("match-referee-reason","text") == "No referee password." && runtime.RunEvent("match_referee_entered",5.2,effects,error) &&
		effects.actions.empty(),"Sign in then says why and asks nothing");
	Check(runtime.SetState({{"mp.match.op.referee_login.shown",false},{"mp.match.op.referee_logout.shown",true},
		{"mp.match.op.referee_logout.available",true}},error,5.3),"signed in");
	runtime.Frame(viewport,5.35);
	Check(text("match-referee-login","display") == "none" && text("match-referee-logout","display") == "block" &&
		text("match-referee-reason","text").empty(),"Sign out, nothing to explain");
	Check(runtime.RunEvent("match_referee_logout",5.4,effects,error) && asked(REFEREE_LOGOUT),"Sign out asks the game");
	// A spectator's camera.
	Check(text("match-follow","display") == "none","no camera controls for a player");
	Check(runtime.SetState({{"mp.match.follow",true}},error,5.5),"spectating");
	runtime.Frame(viewport,5.55);
	Check(text("match-follow","display") == "flex","a spectator steps the camera");
	Check(runtime.RunEvent("match_follow_previous",5.6,effects,error) && asked(FOLLOW_PREV) &&
		runtime.RunEvent("match_follow_next",5.61,effects,error) && asked(FOLLOW_NEXT) &&
		runtime.RunEvent("match_follow_free",5.62,effects,error) && asked(FOLLOW_FREE),"through the players or free");
	// The triggers page the sections, around the strip.
	Check(runtime.RunEvent("onSectionNext",6,effects,error) && section() == 1 && runtime.FocusedControl() == "match-tab-teams","a trigger pages on");
	runtime.Frame(viewport,6.1);
	Check(text("match-section-teams","display") == "block" && text("match-section-status","display") == "none","to the Teams section");
	// Teams: each side's own row and its players, the player's own row chosen;
	// the participants a substitution can bring in; the role; the actions.
	StateValues teams = {{"mp.match.team.count",4.0},{"mp.match.team.more",false},{"mp.match.team.selected",1.0},
		{"mp.match.replacement.count",1.0},{"mp.match.replacement.selected",-1.0},{"mp.match.replacement0.c0",std::string("Player")},
		{"mp.match.role",2.0}};
	const char* const teamRows[][5] = {{"0","0","Marine","Ready","Lock team"},{"1","0","Player","Marine","Player"},
		{"0","1","Strogg","Not ready","Unlock team"},{"1","1","Bot","Strogg","Player"}};
	for (int row = 0; row < 4; ++row) {
		const std::string key = "mp.match.team"+std::to_string(row);
		teams[key+".kind"] = std::stod(teamRows[row][0]); teams[key+".side"] = std::stod(teamRows[row][1]);
		for (int column = 0; column < 3; ++column) teams[key+".c"+std::to_string(column)] = std::string(teamRows[row][2+column]);
	}
	const char* const teamOperations[] = {"join_marine","join_strogg","spectate","queue_join","queue_defer","queue_leave","roster_accept",
		"roster_leave","roster_invite","roster_remove","roster_substitute","role_assign","team_lock","broadcaster","participant_remove",
		"contestant_bind"};
	for (const char* name : teamOperations) {
		const std::string key = std::string("mp.match.op.")+name, id = name;
		teams[key+".shown"] = id != "broadcaster" && id != "contestant_bind";
		teams[key+".available"] = id == "join_strogg" || id == "spectate" || id == "roster_remove";
		teams[key+".label"] = id;
		teams[key+".reason"] = id == "join_marine" ? std::string("You are on that team.") : std::string("Captains only.");
	}
	Check(runtime.SetState(teams,error,6.15),"publish the Teams section");
	runtime.Frame(viewport,6.2);
	Check(text("match-teams-live","display") == "block" && text("match-teams-waiting","display") == "none","the Teams section shows the match");
	for (int row = 0; row < 4; ++row)
		Check(text("match-team-"+std::to_string(row),"display") == "block","each team row shows");
	Check(text("match-team-4","display") == "none" && text("match-team-more","display") == "none","and no others");
	Check(text("match-team-1-c0","text") == "Player" && text("match-team-1-c1","text") == "Marine" &&
		text("match-team-2-c2","text") == "Unlock team","each row's columns");
	const auto band = [&](const char* node) { const auto value = runtime.PresentedValue(node,"background-color");
		Check(value.has_value(),"a row's band"); return *value; };
	const auto marine = band("match-team-0-band"), strogg = band("match-team-2-band"), player = band("match-team-1-band");
	Check(Near(static_cast<float>(marine.data[3]),.35f,.01f) && Near(static_cast<float>(marine.data[0]),0x6A/255.f,.01f) &&
		Near(static_cast<float>(strogg.data[0]),1,.01f) && Near(static_cast<float>(strogg.data[1]),0x7B/255.f,.01f) &&
		Near(static_cast<float>(player.data[3]),.06f,.01f),"a side's own row takes its team's color, a player's row the plain band");
	Check(text("match-team-1-chosen","display") == "block" && text("match-team-0-chosen","display") == "none","the row chosen is lit");
	Check(text("match-replacement-0","display") == "block" && text("match-replacement-0-c0","text") == "Player" &&
		text("match-replacement-1","display") == "none","the participants a substitution can bring in");
	const auto selected = [&](int list, int row) {
		return effects.actions.size() == 1 && std::get<std::string>(effects.actions[0].arguments.at("command")) == "mpMatchSelect" &&
			std::get<double>(runtime.GetState().at("card.match_list")) == list && std::get<double>(runtime.GetState().at("card.match_row")) == row;
	};
	Check(text("match-teams-result","text") == "Ready accepted." &&
		bounds("match-teams-result").y >= bounds("match-replacement-list").y+bounds("match-replacement-list").height,
		"the latest result shows under the lists");
	Check(runtime.RunEvent("match_team_3",6.25,effects,error) && selected(0,3),"choosing a team row names it to the game");
	Check(runtime.RunEvent("match_replacement_0",6.26,effects,error) && selected(1,0),"as does choosing a participant");
	// The lists and the actions share the section, inside the page.
	const auto list = bounds("match-team-list"), actions = bounds("match-team-actions"), section_box = bounds("match-section-teams");
	Check(list.x+list.width <= actions.x && actions.x+actions.width <= section_box.x+section_box.width+.5f &&
		bounds("match-replacement-list").y >= list.y+list.height,"the lists lead, the actions beside them");
	// The actions flow in their column; a hidden one takes no room.
	const auto pitch = bounds("match-join-marine").height+6;
	Check(pitch >= 42 && Near(bounds("match-join-strogg").y,bounds("match-join-marine").y+pitch,.5f) &&
		Near(bounds("match-participant-remove").y,bounds("match-team-lock").y+pitch,.5f) &&
		text("match-broadcaster","display") == "none" && text("match-contestant-bind","display") == "none",
		"the team actions stack, the broadcaster's and the Duel binding only where they apply");
	Check(text("match-join-marine-lock","display") == "block" && text("match-join-marine-reason","text") == "You are on that team.",
		"an unavailable team action says why");
	Check(runtime.RunEvent("match_join_marine",6.3,effects,error) && effects.actions.empty(),"and asks nothing");
	Check(runtime.RunEvent("match_join_strogg",6.31,effects,error) && asked(JOIN_STROGG) &&
		runtime.RunEvent("match_spectate",6.32,effects,error) && asked(SPECTATE),"Join Strogg and Spectate ask the game");
	Check(runtime.RunEvent("match_roster_remove",6.35,effects,error) && asked(ARM_ROSTER_REMOVE),"Remove arms its confirmation");
	runtime.Frame(viewport,6.6);
	Check(text("matchRosterRemoveModal","display") == "block" && text("matchRosterRemoveModal-body","text") == "#str_41902","and asks first");
	Check(runtime.RunEvent("matchRosterRemoveModalNo",6.65,effects,error) && asked(CANCEL_CONFIRM),"No cancels it");
	runtime.Frame(viewport,7);
	Check(text("matchRosterRemoveModal","display") == "none" && text("matchRosterSubstituteModal","display") == "none" &&
		text("matchParticipantRemoveModal","display") == "none","no confirmation stays open");
	// The role: a choice whose new value reaches the session.
	Check(text("match-role","display") == "block" && text("match-bind-side","display") == "none","the role, not the Duel sides");
	Check(runtime.FocusControl("match-role",7.05) && runtime.OpenChoicePopup("match-role",7.05),"the role unfolds");
	runtime.Frame(viewport,7.1);
	press(MenuInput::Down,7.15);
	press(MenuInput::Accept,7.2);
	const auto roleActions = runtime.TakeActions();
	Check(roleActions.size() == 1 && roleActions[0].action == "mpMatchRole" && roleActions[0].proposal &&
		std::get<double>(*roleActions[0].proposal) == 3,"choosing Coach asks for role 3");
	if (roleActions.size() == 1 && roleActions[0].proposalToken) runtime.AcknowledgeControlProposal(roleActions[0].node,roleActions[0].proposalToken,true);
	// Where a Duel side can be bound, its sides take the role's place.
	Check(runtime.SetState({{"mp.match.op.contestant_bind.shown",true},{"mp.match.op.contestant_bind.available",true}},error,7.3),
		"a Duel side can be bound");
	runtime.Frame(viewport,7.4);
	Check(text("match-role","display") == "none" && text("match-bind-side","display") == "block" &&
		text("match-contestant-bind","display") == "block","the sides take the role's place, and Bind side shows");
	Check(runtime.RunEvent("match_contestant_bind",7.45,effects,error) && asked(CONTESTANT_BIND),"Bind side asks the game");
	// Past the card's rows the list offers the stock page.
	Check(runtime.SetState({{"mp.match.team.more",true}},error,7.5),"more rows than the card has");
	runtime.Frame(viewport,7.6);
	Check(text("match-team-more","display") == "block","the list offers the stock page");
	// Each list holds the game's RETAINED_MATCH_*_ROWS: a full server's team
	// rows, every replacement, proposal template and pool map.
	for (const auto& [list,rows] : std::vector<std::pair<std::string,int>>{{"team",64},{"replacement",32},{"proposal",7},
		{"series-map",32},{"series-history",24}}) {
		Check(runtime.PresentedValue("match-"+list+"-"+std::to_string(rows-1),"display").has_value() &&
			!runtime.PresentedValue("match-"+list+"-"+std::to_string(rows),"display").has_value(),("the "+list+" list's rows").c_str());
	}
	// Proposals: the running proposals, the ballot target and the ballots;
	// the proposals to make, Create proposal and the latest result.
	Check(runtime.RunEvent("onSectionNext",7.7,effects,error) && section() == 2,"on to Proposals");
	StateValues proposals = {{"mp.match.proposal.global",std::string("Pause match | Global | Yes 1/3, No 0 | 0:25")},
		{"mp.match.proposal.side",std::string("No additional details")},{"mp.match.scope",0.0},{"mp.match.proposal.count",2.0},
		{"mp.match.proposal.more",false},{"mp.match.proposal.selected",0.0},{"mp.match.proposal0.c0",std::string("Pause match")},
		{"mp.match.proposal0.c1",std::string("Global")},{"mp.match.proposal1.c0",std::string("Change rules")},
		{"mp.match.proposal1.c1",std::string("Global")}};
	for (const char* name : {"proposal_create","proposal_yes","proposal_no","proposal_abstain","proposal_cancel"}) {
		const std::string key = std::string("mp.match.op.")+name, id = name;
		proposals[key+".shown"] = true;
		proposals[key+".available"] = id == "proposal_create" || id == "proposal_yes";
		proposals[key+".label"] = id;
		proposals[key+".reason"] = std::string("You already voted.");
	}
	Check(runtime.SetState(proposals,error,7.75),"publish the Proposals section");
	runtime.Frame(viewport,7.8);
	Check(text("match-proposals-live","display") == "block" && text("match-proposal-global","text") == "Pause match | Global | Yes 1/3, No 0 | 0:25" &&
		text("match-proposal-side","text") == "No additional details","the running proposals");
	Check(text("match-proposal-0","display") == "block" && text("match-proposal-1-c0","text") == "Change rules" &&
		text("match-proposal-2","display") == "none" && text("match-proposal-0-chosen","display") == "block","the proposals to make");
	const auto ballots = bounds("match-proposal-yes"), templates = bounds("match-proposal-list"), create = bounds("match-proposal-create");
	Check(ballots.x+ballots.width <= templates.x && create.y >= templates.y+templates.height && Near(create.x,templates.x,.5f) &&
		bounds("match-proposal-cancel").y+bounds("match-proposal-cancel").height <= bounds("match-section-proposals").y+289.5f,
		"the ballots beside the proposals to make, Create proposal under them");
	Check(runtime.RunEvent("match_proposal_1",7.85,effects,error) && selected(2,1),"choosing a proposal names it to the game");
	Check(runtime.RunEvent("match_proposal_yes",7.86,effects,error) && asked(PROPOSAL_YES) &&
		runtime.RunEvent("match_proposal_create",7.87,effects,error) && asked(PROPOSAL_CREATE),"Vote yes and Create proposal ask the game");
	Check(runtime.RunEvent("match_proposal_no",7.88,effects,error) && effects.actions.empty() &&
		text("match-proposal-no-reason","text") == "You already voted.","a refused ballot says why and asks nothing");
	Check(text("match-proposals-result","text") == "Ready accepted.","the latest result");
	Check(runtime.FocusControl("match-scope",7.9) && runtime.OpenChoicePopup("match-scope",7.9),"the ballot target unfolds");
	runtime.Frame(viewport,7.95);
	press(MenuInput::Down,8);
	press(MenuInput::Accept,8.05);
	const auto scopeActions = runtime.TakeActions();
	Check(scopeActions.size() == 1 && scopeActions[0].action == "mpMatchScope" && scopeActions[0].proposal &&
		std::get<double>(*scopeActions[0].proposal) == 1,"choosing the team proposal asks for target 1");
	if (scopeActions.size() == 1 && scopeActions[0].proposalToken) runtime.AcknowledgeControlProposal(scopeActions[0].node,scopeActions[0].proposalToken,true);
	// Rules: the committed rules, the profiles and the rule fields; the value to
	// stage, the staged changes, the rule actions and the latest result.
	Check(runtime.RunEvent("onSectionNext",8.1,effects,error) && section() == 3,"on to Rules");
	StateValues rules = {{"mp.match.rules.summary",std::string("Competitive TDM | Next match | 3/ab12cd34")},
		{"mp.match.rules.staged",std::string("Competitive TDM | Customized | 4/ef56 | 1")},{"mp.match.rule_value",15.0},
		{"mp.match.profile.count",3.0},{"mp.match.profile.more",false},{"mp.match.profile.selected",1.0},
		{"mp.match.rule.count",5.0},{"mp.match.rule.more",false},{"mp.match.rule.selected",2.0}};
	for (int row = 0; row < 3; ++row) rules["mp.match.profile"+std::to_string(row)+".c0"] = std::string("Profile ")+std::to_string(row);
	for (int row = 0; row < 5; ++row) {
		const std::string key = "mp.match.rule"+std::to_string(row);
		rules[key+".c0"] = std::string("Rule ")+std::to_string(row); rules[key+".c1"] = std::string("Number");
		rules[key+".c2"] = std::string(row == 2 ? "10 -> 15" : "10");
	}
	for (const char* name : {"rules_select_profile","rules_stage","rules_commit","rules_discard"}) {
		const std::string key = std::string("mp.match.op.")+name;
		rules[key+".shown"] = true; rules[key+".available"] = true; rules[key+".label"] = std::string(name); rules[key+".reason"] = std::string("");
	}
	Check(runtime.SetState(rules,error,8.15),"publish the Rules section");
	runtime.Frame(viewport,8.2);
	Check(text("match-rules-live","display") == "block" && text("match-rules-summary","text") == "Competitive TDM | Next match | 3/ab12cd34" &&
		text("match-rules-staged","text") == "Competitive TDM | Customized | 4/ef56 | 1","the committed and staged rules");
	Check(text("match-profile-2","display") == "block" && text("match-profile-3","display") == "none" &&
		text("match-rule-4","display") == "block" && text("match-rule-2-c2","text") == "10 -> 15" &&
		text("match-rule-2-chosen","display") == "block","the profiles and the rule fields");
	const auto ruleValue = runtime.GetWidgetState("match-rule-value");
	Check(ruleValue && std::holds_alternative<double>(ruleValue->accepted) && std::get<double>(ruleValue->accepted) == 15 &&
		text("match-rules-result","text") == "Ready accepted.","the value to stage and the result");
	Check(runtime.RunEvent("match_profile_0",8.25,effects,error) && selected(3,0) &&
		runtime.RunEvent("match_rule_4",8.26,effects,error) && selected(4,4),"choosing a profile or a rule names it to the game");
	Check(runtime.RunEvent("match_rules_stage",8.27,effects,error) && asked(RULES_STAGE) &&
		runtime.RunEvent("match_rules_select_profile",8.28,effects,error) && asked(RULES_SELECT_PROFILE),"Stage value and Select profile ask the game");
	Check(runtime.RunEvent("match_rules_commit",8.3,effects,error) && asked(ARM_RULES_COMMIT),"Commit rules arms its confirmation");
	runtime.Frame(viewport,8.6);
	Check(text("matchRulesCommitModal","display") == "block" && text("matchRulesCommitModal-body","text") == "#str_41901","and asks first");
	Check(runtime.RunEvent("matchRulesCommitModalYes",8.65,effects,error) && asked(CONFIRM),"Yes confirms it");
	runtime.Frame(viewport,9);
	// The value: a whole number the player types and commits. One past the
	// largest a rule takes is held in the field as out of range and asks
	// nothing; a value in range reaches the session.
	const auto numberId = [&]() { return runtime.GetWidgetState("match-rule-value")->number->identity; };
	const auto type = [&](const char* value, double at) {
		const auto size = runtime.GetWidgetState("match-rule-value")->number->state.text.size();
		return runtime.SetNumberSelection("match-rule-value",numberId(),0,size,error,at) &&
			runtime.ReplaceNumberSelection("match-rule-value",numberId(),value,error,at);
	};
	Check(runtime.FocusControl("match-rule-value",9.05) && runtime.BeginNumberEdit("match-rule-value",error,9.05),"edit the value");
	Check(type("20000",9.1) && !runtime.CommitNumberEdit("match-rule-value",numberId(),error,9.1) && runtime.TakeActions().empty(),
		"a value past the largest asks nothing");
	const auto refused = runtime.GetWidgetState("match-rule-value");
	Check(refused && refused->number && refused->number->status == TextNumberStatus::OutOfRange,"and the field holds it as out of range");
	Check(type("25",9.2) && runtime.CommitNumberEdit("match-rule-value",numberId(),error,9.2),"type 25 and commit it");
	const auto valueActions = runtime.TakeActions();
	Check(valueActions.size() == 1 && valueActions[0].action == "mpMatchRuleValue" && valueActions[0].proposal &&
		std::get<double>(*valueActions[0].proposal) == 25,"the value reaches the session");
	// The value comes back through the game; acknowledged once it has, as the
	// adapter does for a number field, it settles without a conflict.
	Check(valueActions.size() == 1 && valueActions[0].proposalToken,"the value waits for the game");
	Check(runtime.SetState({{"mp.match.rule_value",25.0}},error,9.25),"the game takes the value");
	runtime.Frame(viewport,9.3);
	Check(runtime.AcknowledgeControlProposal(valueActions[0].node,valueActions[0].proposalToken,true),"and the field accepts it");
	runtime.Frame(viewport,9.35);
	const auto settled = runtime.GetWidgetState("match-rule-value");
	Check(settled && (!settled->number || !settled->number->conflict) && std::holds_alternative<double>(settled->accepted) &&
		std::get<double>(settled->accepted) == 25,"without a conflict");
	// Series: the series, the map pool and the history; the format and the
	// series and veto actions, each confirmed one asking first.
	Check(runtime.RunEvent("onSectionNext",9.5,effects,error) && section() == 4,"on to Series");
	StateValues series = {{"mp.match.series.summary",std::string("Map selection | Best of three | Score: 0-0")},{"mp.match.series_profile",1.0},
		{"mp.match.series_map.count",3.0},{"mp.match.series_map.more",false},{"mp.match.series_map.selected",0.0},
		{"mp.match.series_history.count",2.0},{"mp.match.series_history.more",false},{"mp.match.series_history.selected",-1.0}};
	for (int row = 0; row < 3; ++row) {
		const std::string key = "mp.match.series_map"+std::to_string(row);
		series[key+".c0"] = std::string("Map ")+std::to_string(row); series[key+".c1"] = std::string(row == 0 ? "Banned" : "Available");
		series[key+".c2"] = std::string(row == 0 ? "Marine" : "");
	}
	for (int row = 0; row < 2; ++row) {
		const std::string key = "mp.match.series_history"+std::to_string(row);
		series[key+".c0"] = std::string("Map ")+std::to_string(row); series[key+".c1"] = std::string("Ban");
		series[key+".c2"] = std::string("Marine");
	}
	for (const char* name : {"series_stage","series_start","series_cancel","series_advance","veto_ban","veto_pick","veto_decider",
			"veto_side_marine","veto_side_strogg"}) {
		const std::string key = std::string("mp.match.op.")+name, id = name;
		series[key+".shown"] = true;
		series[key+".available"] = id == "series_stage" || id == "series_start" || id == "veto_ban";
		series[key+".label"] = id;
		series[key+".reason"] = std::string("Not your turn.");
	}
	Check(runtime.SetState(series,error,9.55),"publish the Series section");
	runtime.Frame(viewport,9.6);
	Check(text("match-series-live","display") == "block" && text("match-series-summary","text") == "Map selection | Best of three | Score: 0-0",
		"the series");
	Check(text("match-series-map-2","display") == "block" && text("match-series-map-3","display") == "none" &&
		text("match-series-map-0-c1","text") == "Banned" && text("match-series-map-0-chosen","display") == "block" &&
		text("match-series-history-1","display") == "block" && text("match-series-history-2","display") == "none" &&
		text("match-series-history-0-c1","text") == "Ban","the map pool and the history");
	Check(runtime.RunEvent("match_series_map_2",9.65,effects,error) && selected(5,2),"choosing a map names it to the game");
	Check(runtime.RunEvent("match_series_history_0",9.66,effects,error) && effects.actions.empty(),"the history is only to read");
	Check(runtime.RunEvent("match_series_stage",9.67,effects,error) && asked(SERIES_STAGE),"Stage series asks the game");
	Check(runtime.RunEvent("match_series_start",9.7,effects,error) && asked(ARM_SERIES_START),"Start series arms its confirmation");
	runtime.Frame(viewport,10);
	Check(text("matchSeriesStartModal","display") == "block" && text("matchSeriesStartModal-body","text") == "#str_41905","and asks first");
	Check(runtime.RunEvent("matchSeriesStartModalNo",10.05,effects,error) && asked(CANCEL_CONFIRM),"No cancels it");
	runtime.Frame(viewport,10.4);
	Check(runtime.RunEvent("match_veto_ban",10.45,effects,error) && asked(ARM_VETO_BAN),"Ban map arms its confirmation");
	runtime.Frame(viewport,10.75);
	Check(text("matchVetoBanModal","display") == "block" && text("matchVetoBanModal-body","text") == "#str_41907","and asks first");
	Check(runtime.RunEvent("matchVetoBanModalYes",10.8,effects,error) && asked(CONFIRM),"Yes confirms it");
	runtime.Frame(viewport,11.15);
	Check(runtime.RunEvent("match_series_cancel",11.2,effects,error) && effects.actions.empty() &&
		text("match-series-cancel-reason","text") == "Not your turn.","a refused action says why and asks nothing");
	Check(runtime.FocusControl("match-series-format",11.25) && runtime.OpenChoicePopup("match-series-format",11.25),"the format unfolds");
	runtime.Frame(viewport,11.3);
	press(MenuInput::Down,11.35);
	press(MenuInput::Accept,11.4);
	const auto formatActions = runtime.TakeActions();
	Check(formatActions.size() == 1 && formatActions[0].action == "mpMatchSeriesProfile" && formatActions[0].proposal &&
		std::get<double>(*formatActions[0].proposal) == 2,"choosing best of five asks for format 2");
	if (formatActions.size() == 1 && formatActions[0].proposalToken) runtime.AcknowledgeControlProposal(formatActions[0].node,formatActions[0].proposalToken,true);
	// Evidence: the evidence's state and the recent evidence, with Refresh.
	Check(runtime.RunEvent("onSectionNext",11.5,effects,error) && section() == 5,"on to Evidence");
	Check(runtime.SetState({{"mp.match.evidence.summary",std::string("Recording | Report ready | No demo | 42")},
		{"mp.match.evidence.count",2.0},{"mp.match.evidence.more",false},{"mp.match.evidence.selected",-1.0},
		{"mp.match.evidence0.c0",std::string("Recording | Report ready | No demo | 42")},{"mp.match.evidence1.c0",std::string("Kill")}},error,11.55),
		"publish the Evidence section");
	runtime.Frame(viewport,11.6);
	Check(text("match-evidence-live","display") == "block" && text("match-evidence-summary","text") == "Recording | Report ready | No demo | 42" &&
		text("match-evidence-1","display") == "block" && text("match-evidence-1-c0","text") == "Kill" &&
		text("match-evidence-2","display") == "none","the evidence and the recent evidence");
	Check(runtime.RunEvent("match_evidence_1",11.65,effects,error) && effects.actions.empty(),"the evidence is only to read");
	Check(runtime.RunEvent("match_evidence_refresh",11.7,effects,error) && asked(REFRESH),"Refresh asks the game for its view again");
	Check(runtime.RunEvent("match_section_status",12,effects,error) && section() == 0 &&
		runtime.RunEvent("onSectionPrevious",12.1,effects,error) && section() == 5 && runtime.FocusedControl() == "match-tab-evidence",
		"back around the strip from Status to Evidence");
	Check(runtime.RunEvent("tab_vote",12.2,effects,error) && runtime.RunEvent("onSectionNext",12.3,effects,error) && section() == 5,
		"the triggers page sections only on the Match page");
	runtime.Frame(viewport,12.6);
	Check(!runtime.CanActivateControl("match-tab-status",12.6) && !runtime.CanActivateControl("match-evidence-refresh",12.6) &&
		!runtime.CanActivateControl("match-team-0",12.6) && !runtime.CanActivateControl("match-role",12.6) &&
		!runtime.CanActivateControl("match-rule-value",12.6) && !runtime.CanActivateControl("match-series-format",12.6),
		"the Match page takes no input from another tab");
}

// The multiplayer cards' Settings pages and the Escape card's Voice page
// (section 14.18): the player's rows, the rail color swatches, the
// appearance table with the teammates' column only in team modes, the hand-
// offs, and each control requesting its verb or player setting.
static void CheckSettings(ScreenHost& host, const char* escapePath, const char* welcomePath) {
	std::string error;
	Runtime::EventEffects effects;
	Viewport viewport; viewport.canvasHeight = 720; viewport.width = 1280; viewport.height = 720;
	const auto press = [](Runtime& runtime, MenuInput input, double at) { runtime.MenuAction(input,true,at); runtime.MenuAction(input,false,at+.01); };
	// The one action a control queued, resolved through its descriptor.
	const auto taken = [&](Runtime& runtime, const char* action) {
		const auto actions = runtime.TakeActions();
		if (actions.size() != 1 || actions[0].action != action) {
			std::fprintf(stderr,"Expected action %s; received %zu",action,actions.size());
			for (const auto& pending : actions) std::fprintf(stderr," %s",pending.action.c_str());
			std::fprintf(stderr,"\n");
		}
		Check(actions.size() == 1 && actions[0].action == action,"a control requests its action");
		ActionInvocation invocation;
		const StateValue* input = actions[0].proposal ? &*actions[0].proposal : nullptr;
		Check(runtime.ResolveAction(actions[0].action,invocation,error,input),"the action resolves");
		if (actions[0].proposalToken) runtime.AcknowledgeControlProposal(actions[0].node,actions[0].proposalToken,true);
		return invocation;
	};
	{
		Runtime runtime(host);
		std::vector<Diagnostic> diagnostics;
		Check(runtime.Initialize() && runtime.LoadDocument(Read(escapePath),"guis/menu/mp_escape.q4ui",diagnostics),"the Escape card loads for its Settings page");
		const auto bounds = [&](const std::string& id) { Bounds b; Check(runtime.GetBounds(id,b),"a settings node is laid out"); return b; };
		const auto text = [&](const std::string& node, const char* property) {
			const auto value = runtime.PresentedValue(node,property); Check(value.has_value(),"a presented keyword"); return value->text;
		};
		StateValues page = {{"mp.settings.name",std::string("Anderson")},{"mp.settings.clan",std::string("Q4")},{"mp.settings.rail",3.0},
			{"mp.model0_count",3.0},{"mp.model0.0",std::string("Marine")},{"mp.model0.1",std::string("Kane")},{"mp.model0.2",std::string("Rhodes")},
			{"mp.model0",1.0},{"mp.model0.row_shown",true},{"mp.model1_count",2.0},{"mp.model1.0",std::string("Off")},
			{"mp.model1.1",std::string("Kane")},{"mp.model1",0.0},{"mp.model1.row_shown",true},{"mp.model2_count",2.0},
			{"mp.model2.0",std::string("Off")},{"mp.model2.1",std::string("Kane")},{"mp.model2",0.0},{"mp.model2.row_shown",true}};
		Check(runtime.SetState(page,error,1),"publish the Settings page");
		Check(runtime.RunEvent("open",1,effects,error) && runtime.RunEvent("tab_settings",1.01,effects,error),"open on the Settings tab");
		runtime.Frame(viewport,1.4);
		Check(runtime.FocusedControl() == "settings-name","the page opens on the player's name");
		// The player's rows stack, the hand-off plates below them inside the page.
		float previous = bounds("settings-name").y;
		for (const char* row : {"settings-clan","settings-model","settings-rail","settings-handicap"}) {
			const auto box = bounds(row);
			Check(Near(box.y,previous+26,.01f) && Near(box.height,26,.01f),"the player's rows stack");
			previous = box.y;
		}
		const auto system = bounds("settings-mpSettingsSystem"), pageBox = bounds("page-settings");
		Check(bounds("settings-mpSettingsControls").y >= previous+26 && system.y+system.height <= pageBox.y+pageBox.height+.5f,
			"Controls, Game Options and System follow inside the page");
		// Name and clan are the card's own fields: Enter sets each.
		Check(runtime.RunEvent("settings_name_entered",1.5,effects,error) && effects.actions.size() == 1 &&
			std::get<std::string>(effects.actions[0].arguments.at("command")) == "mpSettingsName","Enter sets the name");
		Check(runtime.RunEvent("settings_clan_entered",1.52,effects,error) && effects.actions.size() == 1 &&
			std::get<std::string>(effects.actions[0].arguments.at("command")) == "mpSettingsClan","Enter sets the clan");
		// The swatch the player's tint matches stands taller; each asks the game for its color.
		const auto chosen = runtime.PresentedValue("settings-rail-3-chip","transform"), other = runtime.PresentedValue("settings-rail-0-chip","transform");
		Check(chosen && other && chosen->data[1] < -1 && Near(static_cast<float>(other->data[1]),0,.01f),"the player's rail color stands taller");
		Check(runtime.RunEvent("rail_5",1.6,effects,error) && effects.actions.size() == 1 &&
			std::get<std::string>(effects.actions[0].arguments.at("command")) == "mpRail" &&
			std::get<double>(runtime.GetState().at("card.rail")) == 5,"a swatch asks for its color");
		// The handicap slider and the appearance lists set their player settings.
		Check(runtime.FocusControl("settings-handicap",1.7),"focus the handicap");
		press(runtime,MenuInput::Left,1.71);
		auto invocation = taken(runtime,"set.ui_handicap");
		Check(invocation.operation == "settings.player.set" && std::get<std::string>(invocation.arguments.at("cvar")) == "ui_handicap" &&
			std::get<double>(invocation.arguments.at("value")) == 99,"Left lowers the handicap");
		Check(runtime.FocusControl("settings-row-1-opponents",1.8) && runtime.OpenChoicePopup("settings-row-1-opponents",1.8),"the opponents' outline unfolds");
		runtime.Frame(viewport,1.85);
		press(runtime,MenuInput::Down,1.9);
		press(runtime,MenuInput::Accept,1.95);
		invocation = taken(runtime,"set.cl_player_outline_enemy");
		Check(std::get<std::string>(invocation.arguments.at("value")) == "0.35","a level takes its stock value");
		Check(runtime.FocusControl("settings-row-0-teammates",2) && runtime.OpenChoicePopup("settings-row-0-teammates",2),"the teammates' model unfolds");
		runtime.Frame(viewport,2.05);
		press(runtime,MenuInput::Down,2.1);
		press(runtime,MenuInput::Accept,2.15);
		Check(std::get<double>(taken(runtime,"mpModelTeam").arguments.at("value")) == 1,"a model asks the game for its row");
		const auto opponents = bounds("settings-row-1-opponents"), teammates = bounds("settings-row-1-teammates");
		Check(opponents.x+opponents.width <= teammates.x && Near(opponents.y,teammates.y,.01f),"the teammates' cell stands beside the opponents'");
		Check(text("settings-team-note","display") == "none","no team-mode note in a team mode");
		// Outside team modes the teammates' column goes and the note says why.
		Check(runtime.SetState({{"mp.model2.row_shown",false}},error,2.2),"a deathmatch");
		runtime.Frame(viewport,2.3);
		Check(text("settings-row-1-teammates","display") == "none" && text("settings-heading-teammates","display") == "none" &&
			text("settings-team-note","display") == "block" && !runtime.FocusControl("settings-row-0-teammates",2.31),
			"outside team modes only the opponents' column shows");
		// System leaves for the main menu's page.
		Check(runtime.FocusControl("settings-mpSettingsSystem",2.4),"focus System");
		press(runtime,MenuInput::Accept,2.41);
		Check(taken(runtime,"mpSettingsSystem").operation == "session.menu","System leaves for the main menu's page");
		// The Voice page: its toggles and sliders set their settings; the
		// push-to-talk key shows when bound; testing requires separate consent.
		Check(runtime.RunEvent("tab_voice",3,effects,error),"to the Voice tab");
		runtime.Frame(viewport,3.3);
		Check(runtime.FocusedControl() == "voice-capture","the Voice page opens on microphone consent");
		Check(runtime.FocusControl("voice-send",3.30),"focus sending voice");
		press(runtime,MenuInput::Accept,3.31);
		invocation = taken(runtime,"set.s_voiceChatSend");
		Check(std::get<std::string>(invocation.arguments.at("cvar")) == "s_voiceChatSend" && !std::get<bool>(invocation.arguments.at("value")),
			"accept stops sending voice");
		Check(runtime.FocusControl("voice-volume",3.4),"focus the receive volume");
		press(runtime,MenuInput::Left,3.41);
		Check(std::abs(std::get<double>(taken(runtime,"set.s_voiceVolume").arguments.at("value"))-.95) < 1e-6,"Left lowers the volume a step");
		Check(text("voice-unbound","display") == "block" && text("voice-key","display") == "none","an unbound key says so");
		Check(runtime.SetState({{"mp.keys.voice_chat",std::string("V")},{"mp.keys.voice_chat_bound",true}},error,3.5),"bind push to talk");
		runtime.Frame(viewport,3.6);
		Check(text("voice-unbound","display") == "none" && text("voice-key","display") == "block","the bound key shows");
		Check(text("voice-test-lock","display") == "block","a microphone test is locked without consent");
		Check(!runtime.FocusControl("voice-test",3.7),"a locked test cannot acquire focus");
		host.cvars["s_voiceChatCapture"] = true;
		host.cvars["s_voiceChatStatus"] = 3.0;
		host.cvars["s_voiceChatLevel"] = 0.5;
		runtime.Frame(viewport,3.8);
		Check(text("voice-test-lock","display") == "none","explicit microphone consent enables the test");
		Check(Near(bounds("voice-meter-fill").width,bounds("voice-meter").width * .5f,.1f),"the meter follows the actual input peak");
		Check(runtime.FocusControl("voice-test",3.81),"focus the local microphone test");
		press(runtime,MenuInput::Accept,3.82);
		Check(std::get<bool>(taken(runtime,"set.s_voiceChatTest").arguments.at("value")),"the test requests only the local test setting");
		host.cvars["s_voiceChatTest"] = true;
		runtime.Frame(viewport,3.825);
		Check(runtime.RunEvent("tab_settings",3.83,effects,error),"leave the Voice tab");
		Check(effects.actions.size() == 1 && effects.actions[0].operation == "settings.player.set" &&
			std::get<std::string>(effects.actions[0].arguments.at("cvar")) == "s_voiceChatTest" &&
			!std::get<bool>(effects.actions[0].arguments.at("value")),"leaving the Voice tab stops capture testing");
		host.cvars["s_voiceChatTest"] = false;
		host.cvars["s_voiceChatCapture"] = false;
		host.cvars["s_voiceChatLevel"] = 0.0;
	}
	{
		Runtime runtime(host);
		std::vector<Diagnostic> diagnostics;
		Check(runtime.Initialize() && runtime.LoadDocument(Read(welcomePath),"guis/menu/mp_welcome.q4ui",diagnostics),"the Welcome card loads for its Settings page");
		const auto text = [&](const std::string& node, const char* property) {
			const auto value = runtime.PresentedValue(node,property); Check(value.has_value(),"a presented keyword"); return value->text;
		};
		Check(runtime.SetState({{"mp.crosshair",0.0},{"mp.model0_count",1.0},{"mp.model0.0",std::string("Marine")},{"mp.model0",0.0}},error,1),
			"publish Welcome's Settings page");
		Check(runtime.RunEvent("open",1,effects,error) && runtime.RunEvent("tab_settings",1.01,effects,error),"open on Welcome's Settings tab");
		runtime.Frame(viewport,1.4);
		Check(runtime.FocusedControl() == "settings-name","Welcome's Settings opens on the player's name");
		Check(text("settings-crosshair-weapon","display") == "block" && text("settings-crosshair-image","display") == "none",
			"each weapon's own crosshair, said in words");
		Check(runtime.SetState({{"mp.crosshair",3.0},{"mp.crosshair_image",std::string("gfx/guis/crosshairs/crosshair_lightninggun")}},error,1.5),
			"a custom crosshair");
		runtime.Frame(viewport,1.6);
		Check(text("settings-crosshair-weapon","display") == "none" && text("settings-crosshair-image","display") == "block","the custom one shows");
		Check(runtime.FocusControl("settings-crosshair",1.7),"focus the crosshair");
		press(runtime,MenuInput::Right,1.71);
		const auto invocation = taken(runtime,"mpCrosshair");
		Check(invocation.operation == "session.menuValue" && std::get<double>(invocation.arguments.at("value")) == 4,"Right asks for the next crosshair");
	}
}

// The multiplayer Welcome card (section 14.18): centered, each mode's join
// choices, a refused team card, the Players page without choices, the hand-off
// to the join panel's settings, Spectate on Back and Leave Server asking first.
static void CheckWelcome(ScreenHost& host, const char* path) {
	const auto source = Read(path);
	Document document; std::vector<Diagnostic> diagnostics;
	const bool valid = document.Load(source,diagnostics);
	for (const auto& diagnostic : diagnostics) std::fprintf(stderr,"mp_welcome %s: %s\n",diagnostic.pointer.c_str(),diagnostic.message.c_str());
	Check(valid && document.Model().id == "openq4.mp_welcome" && document.Model().canvasHeight == 720,"the Welcome card validates");
	CheckSessionActions(document);
	Runtime runtime(host);
	Check(runtime.Initialize() && runtime.LoadDocument(source,"guis/menu/mp_welcome.q4ui",diagnostics),"the Welcome card loads into a runtime");
	Viewport viewport; viewport.canvasHeight = 720; viewport.width = 1280; viewport.height = 720;
	std::string error;
	Runtime::EventEffects effects;
	const auto bounds = [&](const std::string& id) { Bounds b; Check(runtime.GetBounds(id,b),"a Welcome node is laid out"); return b; };
	const auto number = [&](const char* node, const char* property) {
		const auto value = runtime.PresentedValue(node,property); Check(value.has_value(),"a presented value"); return static_cast<float>(value->data[0]);
	};
	const auto text = [&](const char* node, const char* property) {
		const auto value = runtime.PresentedValue(node,property); Check(value.has_value(),"a presented keyword"); return value->text;
	};
	const auto asked = [&](const char* command) {
		return effects.actions.size() == 1 && std::get<std::string>(effects.actions[0].arguments.at("command")) == command;
	};
	// A team mode: the balance rule refuses the Marines, the Strogg are open,
	// and Auto join names the team it picks.
	Check(runtime.SetState({{"mp.welcome.title",std::string("WELCOME TO OPENQ4")},{"mp.welcome.players",std::string("6/12 PLAYERS")},
		{"mp.welcome.match",std::string("Team Deathmatch - The Fragging Yard")},{"mp.welcome.state",std::string("MATCH IN PROGRESS")},
		{"mp.clock",std::string("9:12")},{"mp.welcome.limit",std::string("Frag limit 50")},{"mp.welcome.team_mode",true},
		{"mp.welcome.team0.score",std::string("12")},{"mp.welcome.team0.count",std::string("4")},{"mp.welcome.team0.flag",std::string("FLAG AT BASE")},
		{"mp.welcome.team1.score",std::string("9")},{"mp.welcome.team1.count",std::string("2")},
		{"mp.join0.shown",true},{"mp.join0.available",false},{"mp.join0.label",std::string("MARINES")},
		{"mp.join0.reason",std::string("Too many players on the Marine team.")},
		{"mp.join1.shown",true},{"mp.join1.available",true},{"mp.join1.label",std::string("STROGG")},
		{"mp.join2.shown",true},{"mp.join2.available",true},{"mp.join2.label",std::string("AUTO JOIN")},{"mp.join2.detail",std::string("Joins STROGG.")},
		{"mp.join3.shown",true},{"mp.join3.available",true},{"mp.join3.label",std::string("SPECTATE")}},error,1),"publish the Join page");
	host.softFocus = true;
	Check(runtime.RunEvent("open",2,effects,error),"the Welcome card opens");
	runtime.Frame(viewport,2.3);
	const auto card = bounds("card");
	Check(Near(card.x+card.width/2,640) && Near(card.y+card.height/2,360),"the Welcome card is centered");
	Check(card.width >= 648-.5f && card.width <= 928+.5f && Near(card.height,477,1),"the Welcome card takes the specified Escape height and fits a 4:3 view");
	Check(runtime.FocusedControl() == "join-slot-2","Auto join has focus");
	Check(text("join-teams","display") == "block" && text("join-solo","display") == "none","team modes show the team cards");
	const auto marines = bounds("join-slot-0"), strogg = bounds("join-slot-1"), automatic = bounds("join-slot-2");
	Check(marines.x+marines.width <= strogg.x && Near(marines.y,strogg.y) && automatic.y >= marines.y+marines.height,
		"the team cards stand side by side above Auto join");
	{
		const auto mark = bounds("join-slot-0-mark"), name = bounds("join-slot-0-label"), band = bounds("join-slot-0-band");
		Check(mark.x >= band.x && mark.x+mark.width <= name.x+.5f && mark.y >= band.y-.5f && mark.y+mark.height <= band.y+band.height+.5f &&
			bounds("join-slot-1-mark").width > 0,"each team card's faction mark leads its band, before the name");
	}
	Check(Near(number("join-slot-0","opacity"),.6f,.001f) && text("join-slot-0-lock","display") == "block" &&
		text("join-slot-0-reason","display") == "block","a refused team dims with its lock and reason");
	Check(Near(number("join-slot-1","opacity"),1,.001f) && text("join-slot-1-lock","display") == "none" &&
		text("join-slot-1-reason","display") == "none","an open team has neither");
	Check(text("join-slot-2-detail","display") == "block","Auto join names the team it picks");
	const auto spectate = bounds("prompt-back-0"), leave = bounds("prompt-leave");
	Check(spectate.x >= card.x+23 && leave.x+leave.width <= card.x+card.width-23,"the prompt bar keeps the card's insets");
	Check(runtime.RunEvent("join_slot_1",2.4,effects,error) && asked("mpWelcomeAction") &&
		std::get<double>(runtime.GetState().at("card.welcome_action")) == 1,"an open team card joins its team");
	Check(runtime.RunEvent("join_slot_0",2.5,effects,error) && effects.actions.empty(),"a refused one asks nothing");
	runtime.Frame(viewport,2.56);
	const auto shaking = runtime.PresentedValue("join-slot-0-slot","transform");
	Check(shaking && std::abs(shaking->data[0]) > .5f,"it shakes instead");
	Check(runtime.RunEvent("join_slot_2",2.9,effects,error) && asked("mpWelcomeAction") &&
		std::get<double>(runtime.GetState().at("card.welcome_action")) == 2,"Auto join asks the game");
	// Deathmatch: the three leaders, then Join game focused and Spectate.
	Check(runtime.SetState({{"mp.welcome.team_mode",false},{"mp.welcome.leader0.name",std::string("Anderson")},
		{"mp.welcome.leader0.score",std::string("7")},{"mp.welcome.leader1.name",std::string("Kane")},{"mp.welcome.leader1.score",std::string("5")},
		{"mp.join0.available",true},{"mp.join0.label",std::string("JOIN GAME")},{"mp.join0.reason",std::string("")},
		{"mp.join1.label",std::string("SPECTATE")},{"mp.join2.shown",false},{"mp.join3.shown",false}},error,3),"publish deathmatch");
	Check(runtime.RunEvent("tab_server",3.1,effects,error) && runtime.RunEvent("tab_join",3.3,effects,error),"back to the Join tab");
	runtime.Frame(viewport,3.5);
	Check(text("join-solo","display") == "block" && text("join-teams","display") == "none" && text("join-leaders","display") == "block" &&
		text("join-arenas","display") == "none","deathmatch shows its leaders");
	Check(runtime.FocusedControl() == "join-solo-0","Join game has focus");
	const auto first = runtime.PresentedValue("join-leader-0-rank","text"), third = runtime.PresentedValue("join-leader-2-rank","text");
	Check(first && first->text == "1" && third && third->text.empty(),"leaders are ranked, an empty place unnumbered");
	Check(runtime.RunEvent("join_solo_0",3.6,effects,error) && asked("mpWelcomeAction") &&
		std::get<double>(runtime.GetState().at("card.welcome_action")) == 0,"Join game asks the game");
	// Tourney: the arenas in play instead of the leaders.
	Check(runtime.SetState({{"mp.welcome.arena_count",2.0},{"mp.welcome.arena0",std::string("Arena 1   Anderson 2 - 1 Kane")},
		{"mp.welcome.arena1",std::string("Arena 2   Voss 0 - 0 Rhodes")}},error,3.7),"publish Tourney's arenas");
	runtime.Frame(viewport,3.8);
	Check(text("join-arenas","display") == "block" && text("join-leaders","display") == "none","Tourney shows its arenas");
	// The Players page lists without choices; its tab keeps the focus.
	Check(runtime.SetState({{"mp.players.a.shown",true},{"mp.players.a.title",std::string("MARINES")},{"mp.players.a.count",2.0},
		{"mp.players.a0.name",std::string("Anderson")},{"mp.players.a1.name",std::string("Kane")},{"mp.players.a1.local",true}},error,3.9),
		"publish the Players page");
	Check(runtime.RunEvent("tab_players",4,effects,error),"to the Players tab");
	runtime.Frame(viewport,4.2);
	Check(runtime.FocusedControl() == "tab-players" && !runtime.CanActivateControl("players-a-0",4.2),"rows are not choices on Welcome");
	Check(text("players-a-1","display") == "block" && text("players-a-2","display") == "none" &&
		text("players-a-1-marker","display") == "block","the lists show their players and the player's own row");
	// Settings' name and clan are the card's own fields.
	Check(runtime.RunEvent("settings_name_entered",4.3,effects,error) && asked("mpSettingsName") &&
		runtime.RunEvent("settings_clan_entered",4.32,effects,error) && asked("mpSettingsClan"),"name and clan set from their fields");
	// Back spectates for now; Leave Server asks first.
	Check(runtime.RunEvent("onBack",4.4,effects,error) && asked("mpClose"),"Back closes the card and the player spectates");
	Check(runtime.RunEvent("leaveModalShow",4.5,effects,error),"Leave Server asks first");
	runtime.Frame(viewport,4.8);
	Check(runtime.FocusedControl() == "leaveModal_no","the confirmation's NO has focus");
	Check(runtime.RunEvent("leaveModalHide",4.9,effects,error),"NO closes it");
	host.softFocus = false;
}

static void CheckScopes(ScreenHost& host, char** paths) {
	host.softFocus = true;
	for (int kind = 0; kind < 3; ++kind) {
		std::fprintf(stderr,"Checking scope %d\n",kind);
		const auto source = Read(paths[kind]);
		Document document; std::vector<Diagnostic> diagnostics;
		const bool valid = document.Load(source,diagnostics);
		for (const auto& diagnostic : diagnostics) std::fprintf(stderr,"scope: %s\n",diagnostic.message.c_str());
		Check(valid && document.Model().canvasMinWidth == 960,"scope document validates with an isotropic center");
		Runtime runtime(host);
		Check(runtime.Initialize() && runtime.LoadDocument(source,"guis/weapons/scope.q4ui",diagnostics),"scope loads");
		Viewport viewport; viewport.canvasHeight = 720; viewport.canvasMinWidth = 960;
		for (const auto size : {std::pair{640,360},std::pair{1280,360},std::pair{360,640},std::pair{3840,2160}}) {
			viewport.width = size.first; viewport.height = size.second;
			host.softened.clear();
			runtime.Frame(viewport,1);
			Bounds center, surround;
			const float scale = viewport.DpRatio();
			Check(runtime.GetBounds("optical-center",center) && Near(center.width,960*scale) && Near(center.height,720*scale) &&
				Near(center.x+center.width*.5f,viewport.width*.5f) && Near(center.y+center.height*.5f,viewport.height*.5f),"wide and tall optical centers stay round and centered");
			Check(runtime.GetBounds(kind == 0 ? "veil" : "outer-haze",surround) && Near(surround.width,static_cast<float>(viewport.width)) && Near(surround.height,static_cast<float>(viewport.height)),"scope surround covers the full view");
			Check(!host.softened.empty() && Near(host.softened.back().sigma,13.5f*scale,.05f) && runtime.Statistics().maskApplications > 0,
				"every scope feathers Gaussian focus through an aperture mask");
			if (size.first == 3840) {
				size_t vertices = 0; for (const auto& draw : host.draws) vertices += draw.vertices.size();
				std::fprintf(stderr,"Scope %d at 4K: %zu vertices\n",kind,vertices);
				Check(vertices * 64 < 48 * 1024 * 1024,"4K optics leave at least half the Vulkan ring for gameplay and composition");
			}
			host.draws.clear();
		}
		std::string error;
		Check(runtime.SetState({{"playerYaw",37.0}},error,2),"scope accepts view yaw");
		runtime.Frame(viewport,2);
		if (kind != 1) {
			const auto compass = runtime.PresentedValue("compass","transform");
			Check(compass && Near(static_cast<float>(compass->data[4]),37,.01f),"scope compass follows view yaw");
		} else {
			Check(runtime.SetState({{"player_ammo",1.0}},error,2.1),"rail clip follows live ammo");
			runtime.Frame(viewport,2.1);
			Check(runtime.PresentedValue("clip-0","display")->text == "none" && runtime.PresentedValue("clip-1","display")->text == "none" && runtime.PresentedValue("clip-2","display")->text == "block","rail cells empty from the top");
		}
		if (kind == 2) {
			Runtime::EventEffects effects;
			Check(runtime.SetState({{"lockStatus",1.0},{"lockYaw",80.0},{"lockRange",std::string("12.4")},{"lockTime",std::string("2.0")}},error,3) && runtime.RunEvent("lockAquired",3,effects,error),"nailgun acquires a target");
			runtime.Frame(viewport,3.2);
			Check(runtime.PresentedValue("readouts","opacity")->data[0] == 1 && runtime.PresentedValue("range","text")->text == "12.4","locked readouts appear");
			Check(Near(float(runtime.PresentedValue("glow","opacity")->data[0]),1,.001f),"one glow reaches the locked tint");
			for (const auto size : {std::pair{1280,720},std::pair{3840,2160}}) {
				viewport.width = size.first; viewport.height = size.second; host.draws.clear(); runtime.Frame(viewport,3.3);
				size_t vertices = 0; for (const auto& draw : host.draws) vertices += draw.vertices.size();
				std::fprintf(stderr,"Locked nailgun at %dx%d: %zu vertices\n",size.first,size.second,vertices);
				Check(vertices * 64 < 48 * 1024 * 1024,"locked optic leaves geometry space for gameplay");
			}
			Check(runtime.RunEvent("lockStop",4,effects,error),"nailgun releases target");
			runtime.Frame(viewport,4.4);
			Check(runtime.PresentedValue("readouts","opacity")->data[0] == 0 && runtime.PresentedValue("lock-arrow","opacity")->data[0] == 0,"release clears lock furniture");
			Check(Near(float(runtime.PresentedValue("glow","opacity")->data[0]),.44f,.001f),"release restores the resting light");
		}
		// Continuous camera motion used to rebuild output-pixel coverage every
		// frame. Exercise actual scope documents, including the idle rail sweep,
		// changing lock opacity/yaw and lock/unlock line motion, at three DPIs.
		Runtime::EventEffects effects;
		if (runtime.HasEvent("onInit")) Check(runtime.RunEvent("onInit",5,effects,error),"scope idle motion starts");
		host.recordDraws=false;
		host.cacheLayers=true;
		double now=5;
		for (const auto size : {std::pair{1920,1080},std::pair{3840,2160},std::pair{720,1280}}) {
			viewport.width=size.first; viewport.height=size.second;
			runtime.Frame(viewport,now); // Resizing legitimately recompiles static coverage.
			std::uint64_t compiled=0, uploads=0, cachedLayers=0;
			for (int frame=1;frame<=240;++frame) {
				now+=1.0/60;
				Check(runtime.SetState({{"playerYaw",frame*.7}},error,now),"continuous scope yaw");
				if (kind==2) {
					Check(runtime.SetState({{"lockYaw",frame*1.3},{"lockStatus",.5+.5*std::sin(frame*.03)}},error,now),"continuous lock motion");
					if (frame==30 || frame==120) Check(runtime.RunEvent(frame==30 ? "lockAquired" : "lockStop",now,effects,error),"lock furniture moves during camera motion");
				}
				++host.frame;
				runtime.Frame(viewport,now);
				compiled+=runtime.Statistics().vectorPathsCompiled;
				uploads+=runtime.Statistics().vectorUploads;
				cachedLayers+=runtime.Statistics().layerCacheHits;
			}
			std::fprintf(stderr,"Scope %d moving at %dx%d: %llu paths, %llu uploads / 240 frames\n",kind,size.first,size.second,
				static_cast<unsigned long long>(compiled),static_cast<unsigned long long>(uploads));
			// Growing/shrinking sweeps may cross cached scale tiers; rotations,
			// translations and opacity must not cause compilation every frame.
			Check(compiled<=24 && uploads<=32,"moving scopes retain geometry across frames");
			if (kind==2 && size.first==1920) Check(cachedLayers>120,"nailgun glow reuses its large gradient layer as lock opacity changes");
		}
		host.recordDraws=true;
		host.cacheLayers=false;
	}
	host.softFocus = false;
}

static void CheckContinuity(ScreenHost& host, char** paths, const char* campaignPath, bool effectsOnly = false) {
	for (int kind = effectsOnly ? 2 : 0; kind < 3; ++kind) {
		Runtime runtime(host); std::vector<Diagnostic> diagnostics; std::string error; Runtime::EventEffects effects;
		const bool loaded = runtime.Initialize() && runtime.LoadDocument(Read(paths[kind]),"guis/continuity.q4ui",diagnostics);
		for (const auto& d : diagnostics) std::fprintf(stderr,"%s %s\n",d.pointer.c_str(),d.message.c_str());
		Check(loaded,"initialization, fourth-level and game effects load in the native runtime");
		Viewport viewport; viewport.canvasHeight = 720; viewport.canvasMinWidth = 960;
		if (kind == 0) {
			Check(runtime.RunEvent("onInit",0,effects,error),"initialization starts its presentation timelines");
			viewport.width = 1280; viewport.height = 720; runtime.Frame(viewport,.05);
			Check(runtime.PresentedValue("mechanism","transform")->data[2] > 1.4 && runtime.PresentedValue("veil","opacity")->data[0] > .5,
				"initialization opens on its mechanism scaled up behind a veil");
		}
		if (kind == 1) Check(runtime.RunEvent("onActivate",0,effects,error),"fourth-level arrival starts");
		if (kind == 2) { host.softFocus = true; Check(runtime.SetState({{"death_amount",1.0}},error,0),"death input accepts full amount"); }
		for (const auto size : {std::pair{1280,720},std::pair{2560,1080},std::pair{720,1280}}) {
			viewport.width = size.first; viewport.height = size.second; host.softened.clear(); host.draws.clear(); runtime.Frame(viewport,1);
			Bounds bounds; Check(runtime.GetBounds("screen",bounds) && Near(bounds.width,float(size.first)) && Near(bounds.height,float(size.second)),"all continuity screens fill wide and tall views");
			if (kind == 0) {
				Check(runtime.GetBounds("canvas",bounds) && Near(bounds.width,960*viewport.DpRatio()) && Near(bounds.x+bounds.width/2,size.first/2.f),"initialization keeps its isotropic center");
			} else if (kind == 1) {
				Check(runtime.GetBounds("back",bounds) && bounds.y >= 0 && bounds.y+bounds.height <= size.second,"the skill screen's last action remains in view");
				Check(runtime.FocusedControl() == "skill1","the fourth level opens on the chosen difficulty");
				Check(Near(float(runtime.PresentedValue("band-top","transform")->data[0]),775.5f,.01f),"fourth level uses the next 97 u band step");
			} else Check(!host.softened.empty() && Near(host.softened.back().saturation,.06f,.001f),"death grades the scene below the HUD");
		}
		if (kind == 0) {
			viewport.width = 3840; viewport.height = 2160; host.draws.clear(); runtime.Frame(viewport,1.5);
			size_t vertices = 0; for (const auto& draw : host.draws) vertices += draw.vertices.size();
			std::fprintf(stderr,"Initialization at 4K: %zu vertices\n",vertices);
			Check(vertices * 64 < 30 * 1024 * 1024,"initialization leaves room for the terminal fade inside Vulkan's geometry ring at 4K");
			Check(Near(float(runtime.PresentedValue("mechanism","transform")->data[2]),1,.0001f) && runtime.PresentedValue("veil","opacity")->data[0] == 0 &&
				runtime.PresentedValue("status-block","opacity")->data[0] == 1,
				"the entrance seats the mechanism and reveals the status line");
			Check(runtime.SetState({{"step",4.0}},error,1.6) && runtime.RunEvent("stepChanged",1.6,effects,error),"a later startup step arrives");
			runtime.Frame(viewport,1.62);
			const float glide = float(runtime.PresentedValue("progress-marker","transform")->data[0]);
			Check(runtime.PresentedValue("progress-3-done","opacity")->data[0] < .1 && glide > 0 && glide < 144,
				"a step jump fills its segments in turn behind a gliding marker");
			runtime.Frame(viewport,1.99);
			Check(Near(float(runtime.PresentedValue("progress-0-done","opacity")->data[0]),.55f,.001f) &&
				float(runtime.PresentedValue("progress-marker","transform")->data[0]) > 120,"the step marker nears the current step");
			runtime.SetReducedMotion(true,2); host.reducedMotion = true;
			Check(runtime.RunEvent("onInit",2,effects,error),"reduced-motion initialization starts"); runtime.Frame(viewport,2.2);
			Check(Near(float(runtime.PresentedValue("sweep","transform")->data[4]),-38,.01f) && runtime.PresentedValue("ember-0","display")->text == "none", "reduced motion holds a fixed key light and omits sparks");
			Check(runtime.PresentedValue("veil","display")->text == "none" && runtime.PresentedValue("progress-marker","display")->text == "none",
				"reduced motion has no entrance veil or gliding step marker");
			host.reducedMotion = false;
		} else if (kind == 1) {
			Check(runtime.RunEvent("detailLeave",2,effects,error),"fourth level returns to its parent"); runtime.Frame(viewport,2.3);
			Check(Near(float(runtime.PresentedValue("band-top","transform")->data[0]),630,.01f) && Near(float(runtime.PresentedValue("crumb1","transform")->data[2]),1,.001f),"return restores the parent title and band");
			Check(runtime.RunEvent("onDeactivate",3,effects,error) && runtime.RunEvent("onActivate",3.1,effects,error),"cached detail document can reopen"); runtime.Frame(viewport,3.3);
			Check(Near(float(runtime.PresentedValue("band-top","transform")->data[0]),775.5f,.01f) && runtime.PresentedValue("title","opacity")->data[0] == 1,"reopening has no stale departure pose");
		} else {
			Check(runtime.SetState({{"death_amount",.2},{"death_focus",1.0},{"death_tunnel",.5}},error,1.1),"death focus swell accepts its own envelope");
			host.softened.clear(); runtime.Frame(viewport,1.1);
			Check(host.softened.size() == 1 && Near(host.softened[0].sigma,5.18f*viewport.DpRatio(),.01f),"death swell shares one scene filter with desaturation");
			Check(runtime.SetState({{"death_amount",0.0},{"death_focus",0.0},{"death_tunnel",0.0},{"crash_focus",.6},{"crash_lids",.8}},error,1.2),"crash focus and eyelids follow independent fade envelopes");
			host.softened.clear(); runtime.Frame(viewport,1.2);
			Check(host.softened.size() == 1 && Near(host.softened[0].sigma,9.6f*viewport.DpRatio(),.01f) &&
				Near(host.softened[0].saturation,.772f,.001f),"crash uses warmer, less desaturated scene focus than death");
			Bounds eyelids; Check(runtime.GetBounds("crash-eyelids",eyelids) && Near(eyelids.width,float(viewport.width)) &&
				Near(eyelids.height,float(viewport.height)),"soft eyelids fill the viewport without a letterbox crop");
			Check(runtime.SetState({{"crash_focus",0.0},{"crash_lids",0.0}},error,1.3),"crash fades retire before recovery or gameplay");
			Check(runtime.SetState({{"death_amount",0.0},{"recovery_amount",1.0}},error,2),"recovery is independent of death"); host.softened.clear(); runtime.Frame(viewport,2);
			Check(!host.softened.empty() && Near(host.softened.back().sigma,20*viewport.DpRatio(),.01f),"recovery starts with soft acuity");
			Check(runtime.SetState({{"recovery_amount",0.0}},error,3),"recovery completes"); host.softened.clear(); runtime.Frame(viewport,3);
			Check(host.softened.empty(),"no inactive game-effect backdrop passes");
			for (const auto size : {std::pair{1280,720},std::pair{2560,720},std::pair{540,960}}) {
				viewport.width = size.first; viewport.height = size.second;
				Check(runtime.SetState({{"wheel_amount",.5}},error,4),"wheel focus fades independently of death and recovery");
				host.softened.clear(); runtime.Frame(viewport,4);
				Check(host.softened.size() == 1 && Near(host.softened[0].sigma,4*viewport.DpRatio(),.01f) &&
					Near(host.softened[0].saturation,.97f,.001f),"one gentle Gaussian scene pass at partial wheel blend");
				Bounds bounds; Check(runtime.GetBounds("wheel-focus",bounds) && Near(bounds.width,float(size.first)) &&
					Near(bounds.height,float(size.second)),"wheel focus covers the entire wide or tall viewport");
				Check(runtime.SetState({{"wheel_amount",1.0}},error,5),"wheel reaches full focus");
				host.softened.clear(); runtime.Frame(viewport,5);
				Check(host.softened.size() == 1 && Near(host.softened[0].sigma,8*viewport.DpRatio(),.01f),"full wheel uses an eight dp Gaussian sigma");
				Check(runtime.SetState({{"wheel_amount",0.0}},error,6),"wheel release clears focus");
				host.softened.clear(); runtime.Frame(viewport,6);
				Check(host.softened.empty(),"closed wheel costs no scene filter pass");
			}
			host.softFocus = false;
		}
	}
	if (effectsOnly) return;
	Runtime parent(host); std::vector<Diagnostic> diagnostics; std::string error; Runtime::EventEffects effects; Viewport viewport;
	Check(parent.Initialize() && parent.LoadDocument(Read(campaignPath),"guis/menu/campaigns.q4ui",diagnostics),"campaign parent loads for detail continuity");
	Check(parent.RunEvent("onActivate",0,effects,error),"campaign parent activates"); parent.Frame(viewport,.2);
	double at = 1;
	for (const char* event : {"detailEnterQuake4","detailEnterAwakening","detailEnterChapters"}) {
		Check(parent.RunEvent(event,at,effects,error),"NEW GAME for both campaigns and CHAPTERS descend through the same detail transition");
		parent.Frame(viewport,at+.15);
		const float partial = float(parent.PresentedValue("band-top","transform")->data[0]);
		Check(partial > 630 && partial < 775.5f,"detail band moves between its parent and child levels");
		Check(parent.RunEvent("detailReverse150",at+.15,effects,error),"Back reverses an in-flight fourth level"); parent.Frame(viewport,at+.31);
		Check(Near(float(parent.PresentedValue("band-top","transform")->data[0]),630,.01f) && parent.PresentedValue("detail-carry","opacity")->data[0] == 0,"reversal restores the parent without a carried title");
		at += 2;
	}
}

static void CheckWeaponWheels(ScreenHost& host, char** paths) {
	for (int variant = 0; variant < 4; ++variant) {
		Runtime wheel(host); std::vector<Diagnostic> diagnostics; std::string error; Runtime::EventEffects effects;
		const bool loaded = wheel.Initialize() && wheel.LoadDocument(Read(paths[variant]),"guis/hud/weapon_wheel.q4ui",diagnostics);
		for (const auto& d:diagnostics) std::fprintf(stderr,"wheel %s: %s\n",d.pointer.c_str(),d.message.c_str());
		Check(loaded,"native wheel document loads");
		const int slots = variant % 2 ? 13 : 11;
		const bool strogg = variant >= 2;
		StateValues state = {{"blend",1.0},{"selected",2.0},{"weaponName",std::string("Shotgun")},{"ammo",std::string("8 / 24")}};
		for (int i=0;i<slots;++i) {
			const std::string slot="slot"+std::to_string(i);
			state[slot+".owned"]=i!=4; state[slot+".count"]=i==3?0.0:24.0;
			state[slot+".reserve"]=std::string("24"); state[slot+".fill"]=.5; state[slot+".index"]=std::to_string(i+1);
		}
		Check(wheel.SetState(state,error,0),"player inventory and localized readouts bind");
		Viewport viewport; viewport.canvasHeight=720;
		for (const auto size : {std::pair{1280,720},std::pair{1920,540},std::pair{405,720},std::pair{3840,2160}}) {
			viewport.width=size.first; viewport.height=size.second;
			const double width=720.0*size.first/size.second, fit=std::min(1.0,(width-32)/560);
			const double reach=std::max(0.0,width*.5-264*fit-16);
			for (const double preference : {27.0,0.0,-40.0,40.0}) {
				const double offset=std::clamp(width*preference/100,-reach,reach);
				Check(wheel.SetState({{"offset",offset},{"fit",fit}},error,1),"viewport placement updates");
				host.draws.clear(); wheel.Frame(viewport,1);
				const auto placement=wheel.PresentedValue("wheel-center","transform");
				Check(placement && Near(float(placement->data[0]),float(offset),.01f) && Near(float(placement->data[2]),float(fit),.01f),"wheel preserves placement and isotropic safety fit");
				Check(wheel.PresentedValue("slot4-contents","opacity")->data[0]==0,"unowned weapon content stays hidden");
				Check(wheel.PresentedValue("slot3-empty","opacity")->data[0]==1,"empty ammo prohibition is shown");
				if (strogg) {
					Check(wheel.PresentedValue("slot2-cell3","opacity")->data[0]==1 && wheel.PresentedValue("slot2-cell4","opacity")->data[0]==0,"half reserve fills exactly four Strogg cells");
					Check(!wheel.PresentedValue("bezel","opacity") && wheel.PresentedValue("slot2-circuit","display")->text=="block","Strogg has circuit paths and no Marine tick bezel");
				} else {
					const auto fill = wheel.PresentedValue("slot2-band-fill","transform");
					Check(fill && fill->data[4]<0 && fill->data[4]>-180.0/slots,"half reserve crops the curved fill by length instead of fading an entire cell");
					Check(Near(float(wheel.PresentedValue("slot2-plate-selected","opacity")->data[0]),.7f,.001f) &&
						wheel.PresentedValue("slot2-plate-rest","display")->text=="none","the selected black plate has one 70-percent surface");
				}
				const auto band = wheel.PresentedValue("slot2-band","transform");
				Check(band && Near(float(std::hypot(band->data[0],band->data[1])),8,.001f),"selection moves the ammunition band outward with its plate");
				Check(wheel.PresentedValue("hub-ammo","text")->text=="8 / 24","live clip and reserve reach hub");
				size_t vertices=0; for(const auto& draw:host.draws) vertices+=draw.vertices.size();
				Check(vertices*64<48*1024*1024,"wheel leaves half of the Vulkan GUI ring for gameplay");
			}
		}
		// The direct weapon/chat GUIs receive the same independent HUD scale as
		// bridged HUDs. Enlarged Couch artwork and inset portrait fitting stay
		// isotropic; the game-source fixture tests the actual preference clamp.
		for (const auto size : {std::pair{1280,720},std::pair{405,720}}) {
			viewport.width=size.first; viewport.height=size.second;
			const double width=720.0*size.first/size.second, inset=.1;
			const double fit=std::min(1.25,std::min((width-2*std::max(16.0,width*inset))/560.0,(720-2*720*inset)/560.0));
			Check(wheel.SetState({{"offset",0.0},{"fit",fit}},error,1.5),"HUD scale and safe inset update the wheel");
			host.draws.clear();wheel.Frame(viewport,1.5);
			const auto placement=wheel.PresentedValue("wheel-center","transform");
			Check(placement && Near(float(placement->data[2]),float(fit),.001f) && Near(float(placement->data[3]),float(fit),.001f),"HUD scaling keeps the wheel round in wide and portrait views");
		}
		Check(wheel.RunEvent("open",2,effects,error),"wheel open transition runs"); wheel.Frame(viewport,2.02);
		if (strogg) {
			Check(wheel.PresentedValue("slot5-entry","opacity")->data[0]==1 && wheel.PresentedValue("slot1-entry","opacity")->data[0]==0,"Strogg plates boot in scrambled order");
			Check(wheel.PresentedValue("hub-name-rune","color")->data[3]>.7 && wheel.PresentedValue("hub-name","color")->data[3]==0,"Strogg opens in runes before translating");
		} else {
			const auto hub=wheel.PresentedValue("hub","transform");
			Check(hub && hub->data[2]>.55 && hub->data[2]<1 && hub->data[3]>.06 && hub->data[3]<.4,"Marine hub opens from its measured slit");
			Check(wheel.PresentedValue("slot0-entry","transform")->data[2]<1 && wheel.PresentedValue("slot0-entry","opacity")->data[0]<1,"Marine plates actually grow and fade from their authored start");
			wheel.Frame(viewport,2.15);
			Check(Near(float(wheel.PresentedValue("hub-torn","opacity")->data[0]),.4f,.001f),"Marine static reaches the written 0.40 peak at 150ms");
		}
		wheel.Frame(viewport,2.8);
		Check(wheel.PresentedValue("slot0-entry","opacity")->data[0]==1,"opening finishes fully visible");
		Check(wheel.SetState({{"selected",3.0},{"weaponName",std::string("HYPERBLASTER")}},error,2.9),"another weapon is selected");
		Check(wheel.RunEvent("selection",2.9,effects,error),"selection transition runs"); wheel.Frame(viewport,2.92);
		if (strogg) {
			Check(wheel.PresentedValue("hub-name-rune","color")->data[3]>.7 && wheel.PresentedValue("hub-name","color")->data[3]==0,"a changed Strogg name restarts its translation");
		} else {
			Check(wheel.PresentedValue("hub-name","opacity")->data[0]<.25 && wheel.PresentedValue("hub-name-clip","height")->data[0]<10,"Marine selection visibly retunes through a line rather than retargeting 1 to 1");
			Check(wheel.PresentedValue("selection-flash","opacity")->data[0]>.7,"Marine selection flashes the selected ring");
		}
		Check(wheel.RunEvent("close",3,effects,error),"wheel close transition runs"); wheel.Frame(viewport,3.2);
		Check(wheel.PresentedValue("slot0-entry","opacity")->data[0]==0,"closing removes the plates");
		wheel.SetReducedMotion(true,4); Check(wheel.RunEvent("open",4,effects,error),"reduced-motion open runs");wheel.Frame(viewport,4.09);
		Check(wheel.PresentedValue("slot0-entry","opacity")->data[0]==1,"reduced motion finishes within 80ms");
		Check(wheel.RunEvent("selection",4.1,effects,error),"reduced-motion selection runs"); wheel.Frame(viewport,4.19);
		if (strogg) {
			Check(wheel.PresentedValue("hub-name-rune","color")->data[3]==0 && wheel.PresentedValue("hub-name","color")->data[3]==1 &&
				wheel.PresentedValue("hub-scan","opacity")->data[0]==0,"reduced motion leaves the translated name without a scan bar");
		} else {
			Check(wheel.PresentedValue("hub-name","opacity")->data[0]==1 && wheel.PresentedValue("selection-flash","opacity")->data[0]==0,"reduced motion settles the name with no flash");
			Check(wheel.SetState({{"mp",true},{"team",1.0}},error,4.2),"team identity updates");wheel.Frame(viewport,4.2);
			Check(wheel.PresentedValue("bezel-team1","display")->text=="block" && wheel.PresentedValue("hub-rings-team1","display")->text=="block" &&
				wheel.PresentedValue("pointer-team1","display")->text=="block" && wheel.PresentedValue("bezel-olive","display")->text=="none","team colour reaches the bezel, hub and pointer together");
		}
		Check(wheel.SetState({{"ammo",std::string("\xE2\x88\x9E")},{"slot0.count",-1.0}},error,4.3),"infinite ammunition updates");wheel.Frame(viewport,4.3);
		Check(wheel.PresentedValue("slot0-band","display")->text=="none" && wheel.PresentedValue("hub-infinity","display")->text=="block","unlimited weapons have no false reserve gauge and an explicit infinity readout");
		wheel.SetReducedMotion(false,4.4);
		host.cvars["ui_reducedFlashing"]=true;
		Check(wheel.RunEvent("open",4.4,effects,error),"reduced flashing applies independently of reduced motion");wheel.Frame(viewport,4.55);
		Check(wheel.PresentedValue(strogg ? "slot0-boot-flash-safe" : "hub-ring-flash","display")->text=="none","reduced flashing suppresses the family's bright boot flash");
		host.cvars["ui_reducedFlashing"]=false;
		host.draws.clear();
	}
}

// The multiplayer quick-chat wheel (ui-visual-design.md 14.6 Quick chat): the
// weapon wheel's ring with eight phrases, the selection in the stock olive or
// your team's text color, the hub naming the channel and, in a team game, the
// zoom control's key that switches it.
static void CheckQuickChat(ScreenHost& host, const char* path) {
	Runtime chat(host); std::vector<Diagnostic> diagnostics; std::string error; Runtime::EventEffects effects;
	const bool loaded = chat.Initialize() && chat.LoadDocument(Read(path),"guis/hud/quick_chat.q4ui",diagnostics);
	for (const auto& d:diagnostics) std::fprintf(stderr,"quick chat %s: %s\n",d.pointer.c_str(),d.message.c_str());
	Check(loaded,"quick-chat document loads");
	const auto opacity = [&](const char* id) { const auto value = chat.PresentedValue(id,"opacity"); return value ? float(value->data[0]) : -1.f; };
	const auto shown = [&](const char* id) { const auto value = chat.PresentedValue(id,"display"); return value && value->text != "none"; };
	Check(chat.SetState({{"blend",1.0},{"selected",2.0},{"team",-1.0},{"channel",0.0},{"zoomKey",std::string("MOUSE2")}},error,0),error.c_str());
	Viewport viewport; viewport.canvasHeight=720; viewport.width=1280; viewport.height=720;
	const double width=720.0*16/9, fit=std::min(1.0,(width-32)/560), offset=std::clamp(width*.27,-(width*.5-264*fit-16),width*.5-264*fit-16);
	Check(chat.SetState({{"offset",offset},{"fit",fit}},error,0),error.c_str());
	host.draws.clear(); chat.Frame(viewport,.1);
	Check(host.errors == 0,"the quick-chat wheel frames without runtime errors");
	const auto placement=chat.PresentedValue("wheel-center","transform");
	Check(placement && Near(float(placement->data[0]),float(offset),.01f) && Near(float(placement->data[2]),float(fit),.01f),
		"the wheel takes the weapon wheel's placement");
	// Eight phrases clockwise from the top, the selected one at full alpha.
	for (int i = 0; i < 8; ++i) {
		const auto text = chat.PresentedValue("phrase"+std::to_string(i)+"-text","text");
		Check(text && text->text == "#str_2312"+std::to_string(61+i),"each segment names its phrase");
	}
	Check(Near(opacity("phrase2-text"),1) && Near(opacity("phrase0-text"),.6f),"the selected phrase reads at full alpha, the rest at 0.60");
	Bounds top, right, label;
	Check(chat.GetBounds("phrase0-label",top) && chat.GetBounds("phrase2-label",right) && Near(top.x+top.width*.5f,right.x-168*fit+right.width*.5f,2.f) &&
		right.x > top.x,"the first phrase sits at the top and the third on the right");
	Check(chat.GetBounds("phrase3-text",label) && label.height <= 48*fit+.5f && label.width <= 112*fit+.5f,"a phrase fits its segment");
	// Deathmatch: the stock olive selection and pointer, ALL, no channel switch.
	Check(shown("slot2-selected") && shown("slot2-selected-olive") && !shown("slot2-selected-team0") && !shown("slot0-selected"),
		"the selection lights its segment in the stock olive");
	Check(shown("pointer") && shown("pointer-olive") && !shown("pointer-team0"),"the olive pointer faces the selection");
	const auto pointer = chat.PresentedValue("pointer","transform");
	Check(pointer && Near(float(pointer->data[4]),90),"the pointer turns 45 degrees a phrase");
	Check(shown("hub-all") && !shown("hub-team0") && !shown("hub-team1") && !shown("hub-hint"),"deathmatch names ALL without a switch");
	// A team game: TEAM in your team's text color and the zoom key's switch.
	Check(chat.SetState({{"team",0.0},{"channel",1.0}},error,.2),error.c_str());
	chat.Frame(viewport,.25);
	const auto key = chat.PresentedValue("hub-key","text");
	Check(!shown("hub-all") && shown("hub-team0") && !shown("hub-team1") && shown("hub-hint") && key && key->text == "MOUSE2",
		"a Marine names TEAM in the Marine color with the switch on the zoom key");
	Check(shown("slot2-selected-team0") && !shown("slot2-selected-olive") && shown("pointer-team0"),
		"the selection takes the Marine text color");
	Check(chat.SetState({{"team",1.0}},error,.3),error.c_str());
	chat.Frame(viewport,.35);
	Check(shown("hub-team1") && !shown("hub-team0") && shown("slot2-selected-team1"),"a Strogg names TEAM in the Strogg color");
	Check(chat.SetState({{"channel",0.0}},error,.4),error.c_str());
	chat.Frame(viewport,.45);
	Check(shown("hub-all") && !shown("hub-team1") && shown("hub-hint"),"the team game can switch to ALL");
	// No selection: no lit segment and no pointer.
	Check(chat.SetState({{"selected",-1.0}},error,.5),error.c_str());
	chat.Frame(viewport,.55);
	Check(!shown("slot2-selected") && !shown("pointer") && Near(opacity("phrase2-text"),.6f),
		"an unselected wheel lights nothing");
	size_t vertices=0; for (const auto& draw:host.draws) vertices+=draw.vertices.size();
	Check(vertices*64<48*1024*1024,"the wheel leaves half of the Vulkan GUI ring for gameplay");
	// Transitions as the weapon wheel's; a new channel re-tunes the word.
	Check(chat.RunEvent("open",1,effects,error),"the open transition runs"); chat.Frame(viewport,1.4);
	Check(Near(opacity("slot7-entry"),1),"opening finishes fully visible");
	Check(chat.RunEvent("channel",1.5,effects,error),"the channel transition runs"); chat.Frame(viewport,1.55);
	Check(opacity("hub-name") < 1,"the channel word re-tunes");
	Check(chat.RunEvent("close",2,effects,error),"the close transition runs"); chat.Frame(viewport,2.2);
	Check(Near(opacity("slot0-entry"),0),"closing removes the plates");
	chat.SetReducedMotion(true,3); Check(chat.RunEvent("open",3,effects,error),"the reduced-motion open runs"); chat.Frame(viewport,3.09);
	Check(Near(opacity("slot7-entry"),1),"reduced motion finishes within 80 ms");
	Check(host.errors == 0,"no runtime diagnostics across the quick-chat exercise");
}

// The Strogg pause's copy of a page (loadgame_strogg.q4ui beside loadgame.q4ui):
// the same construction and verbs in the Strogg family.
static std::string StroggPage(const char* path) {
	std::string strogg = path; const auto dot = strogg.rfind(".q4ui");
	return dot == std::string::npos ? strogg : strogg.insert(dot,"_strogg");
}

// The Load Game page (sections 9 and 13.7, FLOW-004): the stock page's content
// in the stock composition, the session's rows and selection, a row's two
// Accepts, the delete confirmation, the failed and empty states and paging.
// `strogg` checks the Strogg pause's copy, which behaves the same.
static void CheckLoadPage(ScreenHost& host, const char* path, bool strogg = false) {
	const auto source = Read(path);
	Document document; std::vector<Diagnostic> diagnostics;
	if (!document.Load(source,diagnostics)) for (const auto& d : diagnostics) std::fprintf(stderr,"%u:%u %s %s\n",unsigned(d.line),unsigned(d.column),d.pointer.c_str(),d.message.c_str());
	Check(diagnostics.empty() && document.Model().id == (strogg ? "openq4.loadgame_strogg" : "openq4.loadgame") &&
		document.Model().canvasHeight == 720,"the Load Game page validates");
	CheckSessionActions(document);
	Check(!document.Model().FindNode("band-top") && !document.Model().FindNode("scene-softfocus") && !document.Model().FindNode("title"),
		"the page is its content alone: the home that handed off draws its frame and title behind it");
	Runtime runtime(host);
	Check(runtime.Initialize() && runtime.LoadDocument(source,strogg ? "guis/menu/loadgame_strogg.q4ui" : "guis/menu/loadgame.q4ui",diagnostics),
		"the page loads into a runtime");
	Viewport viewport; viewport.canvasHeight = 720; viewport.width = 1280; viewport.height = 720;
	std::string error; Runtime::EventEffects effects;
	const auto text = [&](const std::string& node, const char* property) {
		const auto value = runtime.PresentedValue(node,property); return value ? value->text : std::string("?");
	};
	const auto number = [&](const std::string& node, const char* property) {
		const auto value = runtime.PresentedValue(node,property); return value ? static_cast<float>(value->data[0]) : -1.f;
	};
	const auto bounds = [&](const std::string& id) { Bounds b; Check(runtime.GetBounds(id,b),"a page node is laid out"); return b; };
	const auto requested = [&](const char* action) {
		return effects.actions.size() == 1 && effects.actions[0].action == action && effects.actions[0].operation == "session.menu";
	};
	// Three saves, newest first, the second selected.
	StateValues values{{"load.count",3.0},{"load.first",0.0},{"load.selected",1.0},{"load.shot",std::string("savegames/bunker_door")},
		{"load.date",std::string("10/05/2026")},{"load.time",std::string("06:40pm")},{"load.error",std::string()},
		{"load.skill",std::string("Lieutenant")},{"load.played",std::string("2:13:05 played")}};
	const char* names[] = {"Medic","Bunker door","Trench entry"};
	for (int row = 0; row < 3; ++row) {
		values["load.row"+std::to_string(row)+".name"] = std::string(names[row]);
		values["load.row"+std::to_string(row)+".location"] = std::string("Air Defense Trenches");
	}
	host.materials.clear();
	Check(runtime.SetState(values,error,0),"publish three saves");
	runtime.Frame(viewport,1);
	// The 4:3 canvas is 960 dp wide, centered: x = 160 dp at 1280x720; 1 u = 1.5 dp.
	const float x0 = 160, u = 1.5f;
	const auto shot = bounds("load-shot-back");
	Check(Near(shot.x,x0+25*u) && Near(shot.y,78*u) && Near(shot.width,183*u) && Near(shot.height,137*u),
		"the picture keeps the stock 183x137 u at 25,78 u");
	const auto row0 = bounds("row-0"), row1 = bounds("row-1");
	Check(Near(row0.x,x0+217*u) && Near(row0.y,78*u) && Near(row1.y-row0.y,20*u) && Near(row0.width,394*u),
		"the rows run on the stock 20 u pitch from 217,78 u");
	const auto load = bounds("load_go-plate"), remove = bounds("load_delete-plate"), target = bounds("load_go");
	Check(Near(load.x,x0+21*u) && Near(load.y,256*u) && Near(remove.x,x0+20*u) && Near(remove.y,293*u),
		"LOAD GAME and DELETE GAME are the stock section plates");
	// A plate's target is its 30 u pitch around the band, ending where the
	// plate fades out: short of the list beside it (section 3).
	Check(Near(target.x,load.x) && Near(target.y,(256+10.2f)*u) && Near(target.height,30*u) && target.x+target.width < row0.x,
		"LOAD GAME's target is its 45 dp pitch, short of the list");
	Check(text("row-2","display") == "block" && text("row-3","display") == "none","the list shows the published saves");
	Check(text("row-1-selected","display") == "block" && text("row-0-selected","display") == "none","the selected save is lit");
	Check(text("row-0-name","text") == "Medic" && text("row-2-location","text") == "Air Defense Trenches","each row names its save and its level");
	Check(text("load-date","text") == "10/05/2026" && text("load-time","text") == "06:40pm","the date and time stand under the picture");
	// The date and time share the line under the picture; the difficulty and
	// the run's play time follow on the Demos page's detail lines, clear of
	// LOAD GAME's band (13 u into its plate).
	const auto date = bounds("load-date"), time = bounds("load-time"), skill = bounds("load-skill"), played = bounds("load-played");
	Check(Near(date.x,x0+37*u) && Near(date.y,215*u) && Near(time.y,date.y) && time.x >= date.x+date.width,
		"the time follows the date on its line");
	Check(Near(skill.x,date.x) && Near(skill.y,230*u) && Near(played.x,date.x) && Near(played.y,245*u) &&
		played.y+played.height <= load.y+13*u,"the difficulty and play time stand under them, clear of LOAD GAME's band");
	Check(text("load-skill","text") == "Lieutenant" && text("load-played","text") == "2:13:05 played","the save's difficulty and play time show");
	// The Menu button closes the menus and resumes play during a game; at
	// the title it goes back (section 13.5).
	Check(runtime.RunEvent("onMenuButton",1.01,effects,error) && effects.actions.size() == 1 && effects.actions[0].action == "back",
		"the Menu button goes back at the title");
	Check(runtime.SetState({{"load.ingame",true}},error,1.02) && runtime.RunEvent("onMenuButton",1.03,effects,error) &&
		effects.actions.size() == 1 && effects.actions[0].action == "menuResume","in game the Menu button resumes play");
	Check(runtime.SetState({{"load.ingame",false}},error,1.04),"back at the title");
	Check(text("load-error","display") == "none" && text("load-empty","display") == "none","no failure and no empty state");
	Check(text("page-newer","display") == "none" && text("page-older","display") == "none","one page holds three saves");
	Check(std::find(host.materials.begin(),host.materials.end(),"savegames/bunker_door") != host.materials.end(),"the selected save's picture shows");
	// Arriving: the panel fades in over 150 ms and the newest save has focus.
	Check(runtime.RunEvent("onActivate",2,effects,error),"the page arrives");
	runtime.Frame(viewport,2.001);
	Check(number("load-panel","opacity") < .05f,"the panel starts out");
	runtime.Frame(viewport,2.2);
	Check(Near(number("load-panel","opacity"),1,.001f) && runtime.FocusedControl() == "row-0","the panel has faded in and the newest save has focus");
	// Down walks the rows.
	runtime.MenuAction(MenuInput::Down,true,2.3); runtime.MenuAction(MenuInput::Down,false,2.31);
	runtime.Frame(viewport,2.32);
	Check(runtime.FocusedControl() == "row-1","Down moves to the next save");
	// The prompt bar names what Accept does (section 13.4): LOAD on the
	// chosen save and on LOAD GAME, SELECT on another save, DELETE on DELETE
	// GAME.
	const std::string select = "#str_231042";
	Check(runtime.AcceptPrompt(select,2.33) == "#str_231520","Accept loads the chosen save");
	Check(runtime.FocusControl("row-0",2.34) && runtime.AcceptPrompt(select,2.34) == select,"Accept selects another save");
	Check(runtime.FocusControl("load_go",2.35) && runtime.AcceptPrompt(select,2.35) == "#str_231520" &&
		runtime.FocusControl("load_delete",2.36) && runtime.AcceptPrompt(select,2.36) == "#str_231521","the plates name their verbs");
	// The list remembers its focus (section 13.4): from LOAD GAME, Right
	// returns to the save last focused, not the row nearest the plate.
	Check(runtime.FocusControl("row-0",2.361) && runtime.FocusControl("load_go",2.362),"leave the list from its first save");
	runtime.Frame(viewport,2.363);
	runtime.MenuAction(MenuInput::Right,true,2.364); runtime.MenuAction(MenuInput::Right,false,2.365);
	runtime.Frame(viewport,2.366);
	Check(runtime.FocusedControl() == "row-0","Right into the list returns to the save last focused");
	Check(runtime.FocusControl("load_delete",2.367),"back on DELETE GAME");
	Check(runtime.SetState({{"prompt.accept",std::string("#str_231521")}},error,2.37),"the adapter publishes DELETE");
	runtime.Frame(viewport,2.38);
	Check(text("prompt-231042-verb","text") == "#str_231521" && text("prompts-lead","display") == "flex","the bar shows the published verb");
	Check(runtime.SetState({{"prompt.accept",std::string()}},error,2.39),"nothing to accept");
	runtime.Frame(viewport,2.4);
	Check(text("prompts-lead","display") == "none","Accept leaves the bar while nothing focused can act");
	Check(runtime.SetState({{"prompt.accept",select}},error,2.41) && runtime.FocusControl("row-1",2.41),"back to the chosen save");
	runtime.Frame(viewport,2.42);
	// A row's first Accept selects its save; Accept on the selected save loads it.
	Check(runtime.RunEvent("loadRow0",3,effects,error) && requested("select") && effects.stateChanges.count("load.pick") &&
		std::get<double>(effects.stateChanges.at("load.pick")) == 0,"Accept on another save selects it");
	Check(runtime.RunEvent("loadRow1",3.1,effects,error) && requested("save") && !effects.stateChanges.count("load.pick"),
		"Accept on the selected save loads it");
	// The plates need a selection; DELETE GAME asks first.
	Check(runtime.CanActivateControl("load_go",3.2) && runtime.CanActivateControl("load_delete",3.2),"a selection enables the plates");
	Check(runtime.RunEvent("deleteRequested",3.3,effects,error) && effects.actions.empty(),"DELETE GAME asks first");
	runtime.Frame(viewport,3.6);
	Check(text("deleteModal","display") == "block" && runtime.FocusedControl() == "deleteModal_no","the confirmation opens on NO");
	// The Strogg page asks in the Strogg pause's family; the Marine page keeps
	// the stock dialog.
	Check(strogg ? text("deleteModal-title","font-family") == "r_strogg" && text("deleteModal_no-label","font-family") == "r_strogg" &&
			document.Model().FindNode("deleteModal_no-rune") && runtime.HasEvent("deleteModalShow") :
		text("deleteModal-title","font-family") == "marine" && !document.Model().FindNode("deleteModal_no-rune"),
		"the confirmation takes the page's family");
	Check(runtime.RunEvent("deleteConfirmed",3.7,effects,error) && requested("delete"),"YES deletes and closes the confirmation");
	runtime.Frame(viewport,4.2);
	Check(text("deleteModal","display") == "none","the confirmation has closed");
	// A save that cannot be loaded says why over the foot of its picture.
	Check(runtime.SetState({{"load.error",std::string("This save is damaged.")}},error,5),"a refused load");
	runtime.Frame(viewport,5.1);
	const auto strip = bounds("load-error");
	Check(text("load-error","display") == "block" && text("load-error-text","text") == "This save is damaged." &&
		Near(strip.x,x0+26*u) && Near(strip.y,177*u) && Near(strip.width,181*u),"the reason fills the stock strip at the picture's foot");
	// More saves than the list holds: the pager rows and the rows' window.
	Check(runtime.SetState({{"load.count",130.0},{"load.first",64.0},{"load.selected",64.0},{"load.error",std::string()}},error,6),"a second page");
	runtime.Frame(viewport,6.1);
	Check(text("page-newer","display") == "block" && text("page-older","display") == "block" && text("row-63","display") == "block",
		"a middle page offers both neighbours");
	Check(text("row-0-selected","display") == "block" && text("row-1-selected","display") == "none","the selection follows the window");
	// LOAD GAME's art reaches past the list's leading edge, its target never:
	// the pointer there, level with LOAD GAME, lights the row.
	const auto row9 = bounds("row-9");
	const float edgeX = row9.x+2*u, edgeY = row9.y+row9.height/2;
	runtime.PointerMove(edgeX,edgeY,6.15);
	Check(edgeX < load.x+load.width && edgeY > load.y && edgeY < load.y+load.height && runtime.GetControlState("row-9") == ControlState::Hover,
		"the list's leading edge beside LOAD GAME is the row's");
	Check(runtime.SetState({{"load.first",128.0},{"load.selected",128.0}},error,6.2),"the last page");
	runtime.Frame(viewport,6.3);
	Check(text("page-older","display") == "none" && text("row-1","display") == "block" && text("row-2","display") == "none",
		"the last page shows the remaining saves");
	Check(runtime.RunEvent("pagedOlder",6.4,effects,error),"paging focuses the new page");
	runtime.Frame(viewport,6.5);
	Check(runtime.FocusedControl() == "row-0","the older page's first save has focus");
	// No saves: the empty state, NEW GAME at the title, and the plates disabled.
	Check(runtime.SetState({{"load.count",0.0},{"load.first",0.0},{"load.selected",-1.0}},error,7),"no saves");
	runtime.Frame(viewport,7.1);
	Check(text("load-empty","display") == "block" && text("load_newgame","display") == "block" && text("row-0","display") == "none" &&
		text("load-empty-clear","display") == "none","the empty state offers NEW GAME at the title");
	Check(!runtime.CanActivateControl("load_go",7.1) && !runtime.CanActivateControl("load_delete",7.1),"without a selection the plates rest disabled");
	Check(runtime.RunEvent("onActivate",7.2,effects,error),"an empty page arrives");
	runtime.Frame(viewport,7.4);
	Check(runtime.FocusedControl() == "load_newgame","NEW GAME has focus");
	// In game NEW GAME is not offered.
	Check(runtime.SetState({{"load.ingame",true}},error,8),"the page in game");
	runtime.Frame(viewport,8.1);
	Check(text("load_newgame","display") == "none","in game the empty page offers no NEW GAME");
	// Settled, a frame walks none of the page's authored values; a state
	// change walks them once more, and the page settles again.
	for (double t = 9; t < 12; t += 1.0/60) runtime.Frame(viewport,t);
	runtime.Frame(viewport,12);
	Check(runtime.Statistics().motionInputsScanned == 0,"a settled page walks none of its authored values");
	Check(runtime.SetState({{"load.ingame",false}},error,12.1),"the page at the title again");
	runtime.Frame(viewport,12.2);
	Check(runtime.Statistics().motionInputsScanned > 0 && text("load_newgame","display") == "block","a state change applies its values again");
	for (double t = 12.3; t < 15; t += 1.0/60) runtime.Frame(viewport,t);
	runtime.Frame(viewport,15);
	Check(runtime.Statistics().motionInputsScanned == 0,"and the page settles once more");
	// Catalog controls work independently of the list, and a filtered empty
	// state explains how to recover without offering an unrelated New Game.
	Check(runtime.SetState({{"browser.filter",2.0}},error,16),"filter the catalog");
	runtime.Frame(viewport,16.1);
	Check(text("browser-automatic-cross","display") == "block" && text("browser-manual-cross","display") == "none" &&
		text("browser-clear","display") == "block" && text("load_newgame","display") == "none" &&
		text("load-empty-heading","text") == "#str_231423", "filtered empty state and clear action");
	// The empty state's own action row fixes it: CLEAR FILTERS under the
	// sentence, where NEW GAME was, asks the session to clear the filter.
	const auto emptyClear = bounds("load-empty-clear"), emptyText = bounds("load-empty-text");
	Check(text("load-empty-clear","display") == "block" && Near(emptyClear.x,emptyText.x,1) && emptyClear.y >= emptyText.y+emptyText.height-1,
		"a filtered empty state offers CLEAR FILTERS under its sentence");
	runtime.TakeActions();
	Check(runtime.FocusControl("load-empty-clear",16.12),"focus the empty state's CLEAR FILTERS");
	runtime.MenuAction(MenuInput::Accept,true,16.13); runtime.MenuAction(MenuInput::Accept,false,16.14);
	{
		const auto cleared = runtime.TakeActions();
		Check(cleared.size() == 1 && cleared[0].action == "clearFilters","Accept asks the session to clear the filter");
	}
	Check(runtime.RunEvent("onSecondary",16.2,effects,error) && requested("refresh"),"controller secondary refreshes");
	Check(runtime.RunEvent("onTertiary",16.3,effects,error) && requested("sort"),"controller tertiary sorts");
	for (int sort=0; sort<6; ++sort) {
		const double t=17+sort;
		Check(runtime.SetState({{"browser.sort",double(sort)}},error,t),"set each sort"); runtime.Frame(viewport,t+.1);
		Check(text("browser-sort-label","text") == (sort<2 ? "#str_41557" : sort<4 ? "#str_200925" : "#str_200926"),"sort names its key");
		const bool up=sort==1 || sort==2 || sort==4;
		Check(text("browser-sort-up","display") == (up ? "block":"none") && text("browser-sort-down","display") == (up ? "none":"block"),"sort marker shows direction");
	}
	Check(runtime.SetState({{"load.first",64.0},{"load.selected",65.0},{"browser.generation",7.0},
		{"load.count",67.0},{"load.row0.context",std::string("7:first")},{"load.context",std::string("7:selected")}},error,24),"publish paged identities");
	runtime.Frame(viewport,24.1);
	Check(runtime.RunEvent("loadRow0",24.2,effects,error) && requested("select") &&
		std::get<double>(effects.actions[0].arguments.at("index"))==64 && std::get<double>(effects.actions[0].arguments.at("generation"))==7,
		"row request freezes absolute index and catalog generation");
	Check(runtime.RunEvent("deleteRequested",24.3,effects,error),"capture delete target"); runtime.Frame(viewport,24.6);
	Check(runtime.SetState({{"load.selected",66.0},{"browser.generation",8.0}},error,24.7),"selection changes after confirmation opens");
	Check(runtime.RunEvent("deleteConfirmed",24.8,effects,error) && requested("delete") &&
		std::get<double>(effects.actions[0].arguments.at("index"))==65 && std::get<double>(effects.actions[0].arguments.at("generation"))==7,
		"confirmation retains the save it asked about");
	runtime.Frame(viewport,25.5);
	Check(runtime.FocusControl("row-0",25.6),"focus save before refresh");
	runtime.TakeActions(); runtime.MenuAction(MenuInput::Accept,true,25.7);
	Check(runtime.SetState({{"load.row0.context",std::string("8:replacement")}},error,25.8),"refresh while held");
	runtime.MenuAction(MenuInput::Accept,false,25.9);
	Check(runtime.TakeActions().empty(),"refresh cancels the held row");
}

// The Save Game page: the saves under SAVE NAME and LOCATION, the name in a
// text field on its plate, SAVE GAME beside it, and the overwrite and notice
// dialogs. Typing, entering and the session's verbs run through the runtime.
static void CheckSavePage(ScreenHost& host, const char* path, bool strogg = false) {
	const auto source = Read(path);
	Document document; std::vector<Diagnostic> diagnostics;
	if (!document.Load(source,diagnostics)) for (const auto& d : diagnostics) std::fprintf(stderr,"%u:%u %s %s\n",unsigned(d.line),unsigned(d.column),d.pointer.c_str(),d.message.c_str());
	Check(diagnostics.empty() && document.Model().id == (strogg ? "openq4.savegame_strogg" : "openq4.savegame") &&
		document.Model().canvasHeight == 720,"the Save Game page validates");
	CheckSessionActions(document);
	const auto* field = document.Model().FindNode("save-name");
	Check(field && field->control && field->control->role == ControlRole::Number && std::holds_alternative<NumberSpec>(field->control->widget) &&
		std::get<NumberSpec>(field->control->widget).textField && std::get<NumberSpec>(field->control->widget).maxBytes == 32,
		"the name is a text field of up to 32 bytes");
	Check(!document.Model().FindNode("band-top") && !document.Model().FindNode("title"),"the page is its content alone, as the Load Game page");
	Runtime runtime(host);
	Check(runtime.Initialize() && runtime.LoadDocument(source,strogg ? "guis/menu/savegame_strogg.q4ui" : "guis/menu/savegame.q4ui",diagnostics),
		"the page loads into a runtime");
	Viewport viewport; viewport.canvasHeight = 720; viewport.width = 1280; viewport.height = 720;
	std::string error; Runtime::EventEffects effects;
	const auto text = [&](const std::string& node, const char* property) {
		const auto value = runtime.PresentedValue(node,property); return value ? value->text : std::string("?");
	};
	const auto bounds = [&](const std::string& id) { Bounds b; Check(runtime.GetBounds(id,b),"a page node is laid out"); return b; };
	const auto requested = [&](const char* action, const char* operation = "session.menu") {
		return effects.actions.size() == 1 && effects.actions[0].action == action && effects.actions[0].operation == operation;
	};
	// Two saves, none selected, and no name yet.
	StateValues values{{"save.count",2.0},{"save.first",0.0},{"save.selected",-1.0},{"save.name",std::string()}};
	const char* names[] = {"Medic","Bunker door"};
	for (int row = 0; row < 2; ++row) {
		values["save.row"+std::to_string(row)+".name"] = std::string(names[row]);
		values["save.row"+std::to_string(row)+".location"] = std::string("Air Defense Trenches");
	}
	Check(runtime.SetState(values,error,0),"publish two saves");
	runtime.Frame(viewport,1);
	// The 4:3 canvas is 960 dp wide, centered: x = 160 dp at 1280x720; 1 u = 1.5 dp.
	const float x0 = 160, u = 1.5f;
	const auto row0 = bounds("row-0"), row1 = bounds("row-1");
	Check(Near(row0.x,x0+35*u) && Near(row0.y,78*u) && Near(row1.y-row0.y,20*u) && Near(row0.width,550*u),
		"the rows run on the stock 20 u pitch across the content column from 35,78 u");
	const auto plate = bounds("save-name-plate"), name = bounds("save-name"), save = bounds("save_go-plate");
	Check(Near(plate.x,x0+4*u) && Near(plate.y,370*u) && Near(plate.width,484*u) && Near(plate.height,52*u),"the name plate is the stock 484x52 u at 4,370 u");
	Check(Near(name.x,x0+49*u) && Near(name.y,382*u) && Near(name.width,344*u) && Near(name.height,20*u),"the field is the stock 344x20 u at 49,382 u");
	Check(Near(save.x,x0+397*u) && Near(save.y,370*u),"SAVE GAME is the stock section plate at 397,370 u");
	Check(text("row-1","display") == "block" && text("row-2","display") == "none" && text("save-empty","display") == "none","the list shows the saves");
	Check(text("save-name-hint","display") == "block" && !runtime.CanActivateControl("save_go",1),"without a name the field asks for one and SAVE GAME rests");
	// Arriving: the panel fades in and the name has focus, ready for typing.
	Check(runtime.RunEvent("onActivate",2,effects,error),"the page arrives");
	runtime.Frame(viewport,2.2);
	Check(runtime.FocusedControl() == "save-name","the name has focus");
	// A row's first Accept selects its save; Accept on the selected save saves.
	Check(runtime.RunEvent("saveRow1",3,effects,error) && requested("select") && effects.stateChanges.count("save.pick") &&
		std::get<double>(effects.stateChanges.at("save.pick")) == 1,"Accept on a save selects it");
	Check(runtime.SetState({{"save.selected",1.0},{"save.name",std::string("Bunker door")}},error,3.1),"the session selects it and names it");
	runtime.Frame(viewport,3.2);
	const auto filled = runtime.GetWidgetState("save-name");
	Check(text("row-1-selected","display") == "block" && text("save-name-hint","display") == "none" && filled &&
		std::get<std::string>(filled->accepted) == "Bunker door","the selected save is lit and its name fills the field");
	Check(runtime.CanActivateControl("save_go",3.2),"a name enables SAVE GAME");
	// The prompt bar names Accept SAVE on SAVE GAME and on the chosen save,
	// SELECT on another save (section 13.4).
	const std::string select = "#str_231042";
	Check(runtime.FocusControl("save_go",3.21) && runtime.AcceptPrompt(select,3.21) == "#str_231522" &&
		runtime.FocusControl("row-1",3.22) && runtime.AcceptPrompt(select,3.22) == "#str_231522" &&
		runtime.FocusControl("row-0",3.23) && runtime.AcceptPrompt(select,3.23) == select,"Accept's verb follows the focus");
	Check(runtime.RunEvent("saveRow1",3.3,effects,error) && requested("save"),"Accept on the selected save saves under its name");
	Check(runtime.RunEvent("saveRequested",3.4,effects,error) && requested("save"),"SAVE GAME saves under the name");
	// A name typed but not entered: SAVE GAME returns the focus to it.
	Check(runtime.SetState({{"ui.numberDraftsPending",true}},error,3.5) && runtime.RunEvent("saveRequested",3.6,effects,error) &&
		requested("editName","ui.numberDrafts.focus"),"SAVE GAME returns to a name not yet entered");
	Check(runtime.SetState({{"ui.numberDraftsPending",false}},error,3.7),"the draft settles");
	// Typing a name and entering it proposes it to the page's own state.
	runtime.Frame(viewport,3.8);
	Check(runtime.FocusControl("save-name",3.9) && runtime.BeginNumberEdit("save-name",error,3.9),"the field opens for typing");
	auto view = runtime.GetWidgetState("save-name");
	Check(view && view->number && view->number->state.text == "Bunker door" && view->number->state.caret == 11,"typing starts from the name, all of it selected");
	Check(runtime.ReplaceNumberSelection("save-name",view->number->identity,"Trench entry",error,4),"type a new name");
	view = runtime.GetWidgetState("save-name");
	Check(runtime.CommitNumberEdit("save-name",view->number->identity,error,4.1),"enter it");
	const auto actions = runtime.TakeActions();
	Check(actions.size() == 1 && actions[0].action == "rename" && actions[0].proposal &&
		std::get<std::string>(*actions[0].proposal) == "Trench entry","entering proposes the name to the page's state");
	Check(runtime.RunEvent("nameEntered",4.2,effects,error) && requested("save"),"the entered name saves");
	// Saving over a save asks first, naming it; the dialog opens on NO.
	Check(runtime.SetState({{"save.target",std::string("Bunker door")}},error,5) && runtime.RunEvent("overwriteModalShow",5,effects,error),"ask to save over");
	runtime.Frame(viewport,5.3);
	Check(text("overwriteModal","display") == "block" && runtime.FocusedControl() == "overwriteModal_no" &&
		text("overwriteModal-target","text") == "Bunker door","the confirmation names the save and opens on NO");
	Check(runtime.RunEvent("overwriteConfirmed",5.4,effects,error) && requested("overwrite"),"YES saves over it");
	runtime.Frame(viewport,5.9);
	Check(text("overwriteModal","display") == "none","the confirmation has closed");
	// A refusal: WARNING, the reason and CLOSE alone.
	Check(runtime.SetState({{"save.notice",std::string("You cannot save over an autosave.")}},error,6) &&
		runtime.RunEvent("noticeModalShow",6,effects,error),"refuse a save");
	runtime.Frame(viewport,6.3);
	Check(text("noticeModal","display") == "block" && runtime.FocusedControl() == "noticeModal_close" && text("noticeModal-body","text") == "You cannot save over an autosave." &&
		!document.Model().FindNode("noticeModal_yes") && !document.Model().FindNode("noticeModal_no"),"the notice says why and offers CLOSE alone");
	// Both dialogs take the page's family: the Strogg ones ask in R_Strogg
	// over runes on Strogg plates.
	Check(strogg ? text("overwriteModal-title","font-family") == "r_strogg" && text("noticeModal_close-label","font-family") == "r_strogg" &&
			document.Model().FindNode("overwriteModal_yes-rune") && document.Model().FindNode("noticeModal_close-rune") :
		text("overwriteModal-title","font-family") == "marine" && !document.Model().FindNode("noticeModal_close-rune"),
		"the dialogs take the page's family");
	Check(runtime.RunEvent("noticeModalHide",6.4,effects,error),"CLOSE closes it");
	runtime.Frame(viewport,6.9);
	Check(text("noticeModal","display") == "none","the notice has closed");
	// No saves yet: the empty state in the list's place.
	Check(runtime.SetState({{"save.count",0.0},{"save.selected",-1.0}},error,7),"no saves");
	runtime.Frame(viewport,7.1);
	Check(text("save-empty","display") == "block" && text("row-0","display") == "none" && text("save-empty-clear","display") == "none",
		"an empty list says so");
	Check(runtime.SetState({{"browser.filter",1.0}},error,7.2),"a filter hides every save");
	runtime.Frame(viewport,7.3);
	const auto saveClear = bounds("save-empty-clear"), saveList = bounds("save-list");
	Check(text("save-empty-clear","display") == "block" && saveClear.y+saveClear.height <= saveList.y+saveList.height,
		"the filtered empty state offers CLEAR FILTERS inside the list's place");
}

// The Restart Level page: five difficulty plates with the chosen one lit, its
// description beside them, RESTART LEVEL as the enlarged primary plate, and
// BACK. The choice stays on the page until RESTART LEVEL.
static void CheckRestartPage(ScreenHost& host, const char* path, bool strogg = false) {
	const auto source = Read(path);
	Document document; std::vector<Diagnostic> diagnostics;
	if (!document.Load(source,diagnostics)) for (const auto& d : diagnostics) std::fprintf(stderr,"%u:%u %s %s\n",unsigned(d.line),unsigned(d.column),d.pointer.c_str(),d.message.c_str());
	Check(diagnostics.empty() && document.Model().id == (strogg ? "openq4.restartlevel_strogg" : "openq4.restartlevel") &&
		document.Model().canvasHeight == 720,"the Restart Level page validates");
	CheckSessionActions(document);
	const auto go = document.Model().actions.find("go"), back = document.Model().actions.find("back");
	Check(go != document.Model().actions.end() && std::get<std::string>(go->second.arguments.at("command").literal) == "restartGo" &&
		back != document.Model().actions.end() && std::get<std::string>(back->second.arguments.at("command").literal) == "restartBack",
		"RESTART LEVEL and BACK ask the session");
	Runtime runtime(host);
	Check(runtime.Initialize() && runtime.LoadDocument(source,strogg ? "guis/menu/restartlevel_strogg.q4ui" : "guis/menu/restartlevel.q4ui",
		diagnostics),"the page loads into a runtime");
	Viewport viewport; viewport.canvasHeight = 720; viewport.width = 1280; viewport.height = 720;
	std::string error; Runtime::EventEffects effects;
	const auto text = [&](const std::string& node, const char* property) {
		const auto value = runtime.PresentedValue(node,property); return value ? value->text : std::string("?");
	};
	const auto bounds = [&](const std::string& id) { Bounds b; Check(runtime.GetBounds(id,b),"a page node is laid out"); return b; };
	Check(runtime.SetState({{"restart.skill",2.0}},error,0),"the level's difficulty");
	runtime.Frame(viewport,1);
	// The 4:3 canvas is 960 dp wide, centered: x = 160 dp at 1280x720; 1 u = 1.5 dp.
	const float x0 = 160, u = 1.5f;
	const auto first = bounds("skill-0-plate"), second = bounds("skill-1-plate"), primary = bounds("restart_go"), description = bounds("restart-description");
	Check(Near(first.x,x0+21*u) && Near(first.y,172*u) && Near(second.x,x0+20*u) && Near(second.y-first.y,30*u),
		"the difficulty plates keep the stock 30 u pitch from 21,172 u");
	// Each plate's target is its pitch around its band (section 3): the
	// targets meet, so no plate takes the pointer over its neighbour's band.
	const auto firstTarget = bounds("skill-0"), secondTarget = bounds("skill-1");
	Check(Near(firstTarget.y,(172+10.2f)*u) && Near(firstTarget.height,30*u) && Near(secondTarget.y,firstTarget.y+firstTarget.height),
		"the plates' 45 dp targets meet on their pitch");
	Check(Near(primary.x,x0+237*u) && Near(primary.y,222*u) && Near(primary.width,356*u) && Near(primary.height,43*u),
		"RESTART LEVEL is the stock 356x43 u primary plate at 237,222 u");
	Check(Near(description.x,x0+239*u) && Near(description.y,181*u),"the description stands at 239,181 u");
	Check(text("skill-2-plate-lit","display") == "block" && text("skill-2-label","display") == "none" &&
		text("skill-2-label-lit","display") == "block" && text("skill-1-plate-lit","display") == "none","the level's difficulty is lit");
	Check(text("restart-description","text") == "#str_200184","its description shows");
	// Arriving focuses the chosen difficulty.
	Check(runtime.RunEvent("onActivate",2,effects,error),"the page arrives");
	runtime.Frame(viewport,2.2);
	Check(runtime.FocusedControl() == "skill-2","the chosen difficulty has focus");
	// Choosing another lights and describes it; nothing reaches the session.
	Check(runtime.RunEvent("chooseSkill4",3,effects,error) && effects.actions.empty() && effects.stateChanges.count("restart.skill") &&
		std::get<double>(effects.stateChanges.at("restart.skill")) == 4,"choosing a difficulty keeps it on the page");
	runtime.Frame(viewport,3.1);
	Check(text("skill-4-plate-lit","display") == "block" && text("skill-2-plate-lit","display") == "none" &&
		text("restart-description","text") == "#str_41118","the new choice is lit and described");
	Check(runtime.CanActivateControl("restart_go",3.2) && runtime.CanActivateControl("restart_back",3.2),"RESTART LEVEL and BACK are live");
	// The pointer on the foot of a plate's band lights that plate, not the
	// next one, whose plate art starts above it.
	runtime.PointerMove(first.x+100*u,first.y+36*u,3.3);
	Check(first.y+36*u > second.y && runtime.GetControlState("skill-0") == ControlState::Hover,"the foot of PRIVATE's band is PRIVATE's");
}

// The Demos sub-page (FLOW-013, FLOW-050): the library under its filter tags,
// the selected demo's picture and lines, PLAY, DELETE and REFRESH, the
// confirmation that names the demo, and the empty states, in the sub-page
// frame under the MULTIPLAYER crumb. The session's verbs run through the
// runtime.
static void CheckDemosPage(ScreenHost& host, const char* path) {
	const auto source = Read(path);
	Document document; std::vector<Diagnostic> diagnostics;
	if (!document.Load(source,diagnostics)) for (const auto& d : diagnostics) std::fprintf(stderr,"%u:%u %s %s\n",unsigned(d.line),unsigned(d.column),d.pointer.c_str(),d.message.c_str());
	Check(diagnostics.empty() && document.Model().id == "openq4.demos" && document.Model().canvasHeight == 720,"the Demos page validates");
	CheckSessionActions(document);
	const std::pair<const char*,const char*> verbs[] = {{"back","demosBack"},{"select","demosSelect"},{"play","demosPlay"},
		{"delete","demosDelete"},{"refresh","demosRefresh"},{"filter","demosFilter"},{"newer","demosNewer"},{"older","demosOlder"}};
	for (const auto& [action, command] : verbs) {
		const auto found = document.Model().actions.find(action);
		Check(found != document.Model().actions.end() && std::get<std::string>(found->second.arguments.at("command").literal) == command,
			"each of the page's actions asks the session");
	}
	Check(document.Model().FindNode("band-top") && document.Model().FindNode("crumb") && document.Model().FindNode("title") &&
		!document.Model().FindNode("demo-title"),"the sub-page draws its own frame: the MULTIPLAYER crumb and its title at the step's foot");
	Runtime runtime(host);
	Check(runtime.Initialize() && runtime.LoadDocument(source,"guis/menu/demos.q4ui",diagnostics),"the page loads into a runtime");
	Viewport viewport; viewport.canvasHeight = 720; viewport.width = 1280; viewport.height = 720;
	std::string error; Runtime::EventEffects effects;
	const auto text = [&](const std::string& node, const char* property) {
		const auto value = runtime.PresentedValue(node,property); return value ? value->text : std::string("?");
	};
	const auto number = [&](const std::string& node, const char* property, size_t index = 0) {
		const auto value = runtime.PresentedValue(node,property); return value ? static_cast<float>(value->data[index]) : -1.f;
	};
	const auto bounds = [&](const std::string& id) { Bounds b; Check(runtime.GetBounds(id,b),"a page node is laid out"); return b; };
	const auto requested = [&](const char* action) {
		return effects.actions.size() == 1 && effects.actions[0].action == action && effects.actions[0].operation == "session.menu";
	};
	// Two demos, newest first, the newest selected.
	StateValues values{{"demos.count",2.0},{"demos.first",0.0},{"demos.selected",0.0},{"demos.filter",0.0},
		{"demos.shot",std::string("gfx/guis/loadscreens/generic")},{"demos.kind",std::string("Render demo v10")},
		{"demos.status",std::string("Playable")},{"demos.detail",std::string("Fixed recorded camera")},{"demos.playable",true},
		{"demos.canPlay",true},{"demos.canDelete",true},{"demos.error",std::string()},
		{"demos.confirm",std::string("Delete demo \"newer\"? This cannot be undone.")}};
	const char* names[] = {"newer","older"};
	for (int row = 0; row < 2; ++row) {
		const auto key = "demos.row"+std::to_string(row);
		values[key+".name"] = std::string(names[row]); values[key+".map"] = std::string("-");
		values[key+".date"] = std::string("10/06/2026"); values[key+".length"] = std::string("--:--");
	}
	Check(runtime.SetState(values,error,0),"publish two demos");
	runtime.Frame(viewport,1);
	// The 4:3 canvas is 960 dp wide, centered: x = 160 dp at 1280x720; 1 u = 1.5 dp.
	const float x0 = 160, u = 1.5f;
	const auto shot = bounds("demo-shot-back");
	Check(Near(shot.x,x0+25*u) && Near(shot.y,78*u) && Near(shot.width,183*u) && Near(shot.height,137*u),
		"the picture keeps the stock 183x137 u at 25,78 u");
	const auto play = bounds("demo_play-plate"), remove = bounds("demo_delete-plate"), refresh = bounds("demo_refresh-plate");
	Check(Near(play.x,x0+21*u) && Near(play.y,256*u) && Near(remove.y,293*u) && Near(refresh.x,x0+20*u) && Near(refresh.y,330*u),
		"PLAY, DELETE and REFRESH are section plates on the stock 37 u pitch");
	const auto row0 = bounds("row-0"), row1 = bounds("row-1");
	Check(Near(row0.x,x0+217*u) && Near(row0.y,78*u) && Near(row1.y-row0.y,20*u) && Near(row0.width,394*u),
		"the rows run on the stock 20 u pitch from 217,78 u");
	const auto mvd = bounds("tag-mvd"), render = bounds("tag-render"), incomplete = bounds("tag-incomplete");
	Check(Near(mvd.x,x0+217*u) && Near(mvd.y,43*u) && Near(mvd.height,15*u) && render.x > mvd.x + mvd.width &&
		incomplete.x + incomplete.width <= x0+(217+394)*u + .5f,"the filter tags line up over the headings within the list's width");
	const auto name = bounds("demo-head-name"), map = bounds("demo-head-map"), date = bounds("demo-head-date"), length = bounds("demo-head-length");
	Check(Near(name.y,63*u) && map.x > name.x && date.x > map.x && length.x > date.x,"NAME, MAP, DATE and DURATION head their columns");
	Check(text("row-1","display") == "block" && text("row-2","display") == "none" && text("row-0-selected","display") == "block",
		"the list shows the published demos with the newest selected");
	Check(text("row-0-name","text") == "newer" && text("row-1-date","text") == "10/06/2026" && text("row-0-length","text") == "--:--",
		"each row names its demo, its date and its duration");
	Check(text("demo-kind","text") == "Render demo v10" && text("demo-status","text") == "Playable" &&
		text("demo-detail","text") == "Fixed recorded camera" && Near(number("demo-status","color",1),1,.001f),
		"the kind, status and details stand under the picture; a playable demo's status is white");
	Check(text("demo-empty","display") == "none" && text("demo-error","display") == "none" && text("page-older","display") == "none",
		"no empty state, failure or pager");
	Check(text("tag-mvd-plate","display") == "block" && text("tag-mvd-plate-set","display") == "none" && text("tag-mvd-cross","display") == "none",
		"no filter is set");
	// Arriving: the panel fades in over 150 ms and the newest demo has focus.
	Check(runtime.RunEvent("onActivate",2,effects,error),"the page arrives");
	runtime.Frame(viewport,2.001);
	Check(number("demo-panel","opacity") < .05f,"the panel starts out");
	runtime.Frame(viewport,2.2);
	Check(Near(number("demo-panel","opacity"),1,.001f) && runtime.FocusedControl() == "row-0","the panel has faded in and the newest demo has focus");
	// A row's first Accept selects its demo; Accept on the selected demo plays it.
	Check(runtime.RunEvent("demosRow1",3,effects,error) && requested("select") && effects.stateChanges.count("demos.pick") &&
		std::get<double>(effects.stateChanges.at("demos.pick")) == 1,"Accept on another demo selects it");
	Check(runtime.RunEvent("demosRow0",3.1,effects,error) && requested("play"),"Accept on the selected demo plays it");
	Check(runtime.CanActivateControl("demo_play",3.2) && runtime.CanActivateControl("demo_delete",3.2) &&
		runtime.CanActivateControl("demo_refresh",3.2),"a playable, deletable selection enables the plates");
	// Accept's verb (section 13.4): PLAY on PLAY and on a chosen demo that can
	// play, DELETE on DELETE, SELECT on REFRESH.
	Check(runtime.FocusControl("demo_play",3.21) && runtime.AcceptPrompt("#str_231042",3.21) == "#str_231523" &&
		runtime.FocusControl("demo_delete",3.22) && runtime.AcceptPrompt("#str_231042",3.22) == "#str_231521" &&
		runtime.FocusControl("demo_refresh",3.23) && runtime.AcceptPrompt("#str_231042",3.23) == "#str_231042","the plates name their verbs");
	// A demo this build cannot play: PLAY rests disabled, Accept on it does
	// nothing, and its status takes the error color.
	Check(runtime.SetState({{"demos.canPlay",false},{"demos.playable",false},{"demos.canDelete",false},
		{"demos.status",std::string("Unsupported by this build")}},error,3.3),"an unplayable demo");
	runtime.Frame(viewport,3.4);
	Check(!runtime.CanActivateControl("demo_play",3.4) && !runtime.CanActivateControl("demo_delete",3.4),"PLAY and DELETE rest disabled");
	Check(runtime.RunEvent("demosRow0",3.5,effects,error) && effects.actions.empty(),"Accept on it does nothing");
	Check(Near(number("demo-status","color",0),.894f,.01f) && Near(number("demo-status","color",1),.4275f,.01f),"its status is in the error color");
	Check(runtime.SetState({{"demos.canPlay",true},{"demos.playable",true},{"demos.canDelete",true}},error,3.6),"playable again");
	// DELETE asks first, naming the demo.
	Check(runtime.RunEvent("deleteModalShow",4,effects,error) && effects.actions.empty(),"DELETE asks first");
	runtime.Frame(viewport,4.3);
	Check(text("deleteModal","display") == "block" && text("deleteModal-body","text") == "Delete demo \"newer\"? This cannot be undone." &&
		runtime.FocusedControl() == "deleteModal_no","the confirmation names the demo and opens on NO");
	Check(runtime.RunEvent("deleteConfirmed",4.4,effects,error) && requested("delete"),"YES deletes and closes the confirmation");
	runtime.Frame(viewport,4.9);
	Check(text("deleteModal","display") == "none","the confirmation has closed");
	// A filter tag sets its filter, and clears it while set.
	Check(runtime.RunEvent("filterTag1",5,effects,error) && requested("filter") && effects.stateChanges.count("demos.filter") &&
		std::get<double>(effects.stateChanges.at("demos.filter")) == 1,"MULTI-VIEW sets its filter");
	runtime.Frame(viewport,5.1);
	Check(text("tag-mvd-plate","display") == "none" && text("tag-mvd-plate-set","display") == "block" && text("tag-mvd-cross","display") == "block" &&
		text("tag-render-plate","display") == "block","the set tag rests brighter with its close cross");
	Check(Near(number("tag-mvd-plate-set","opacity"),.6f,.001f) && Near(number("tag-render-plate","opacity"),.28f,.001f),
		"set tags rest at 0.60, unset ones at 0.28");
	Check(runtime.RunEvent("filterTag1",5.2,effects,error) && requested("filter") &&
		std::get<double>(effects.stateChanges.at("demos.filter")) == 0,"Accept on the set tag clears it");
	Check(runtime.RunEvent("filterTag3",5.3,effects,error) && std::get<double>(effects.stateChanges.at("demos.filter")) == 3,
		"another tag replaces the filter");
	// A failure says why over the foot of the picture.
	Check(runtime.SetState({{"demos.error",std::string("#str_231119")}},error,6),"a failed deletion");
	runtime.Frame(viewport,6.1);
	const auto strip = bounds("demo-error");
	Check(text("demo-error","display") == "block" && text("demo-error-text","text") == "#str_231119" && Near(strip.y,177*u),
		"the reason fills the stock strip at the picture's foot");
	// More demos than the list holds: the pager rows.
	Check(runtime.SetState({{"demos.count",130.0},{"demos.first",64.0},{"demos.selected",64.0},{"demos.error",std::string()}},error,7),"a second page");
	runtime.Frame(viewport,7.1);
	Check(text("page-newer","display") == "block" && text("page-older","display") == "block","a middle page offers both neighbours");
	// No demos: the empty state, its sentence telling a filter apart, and the
	// plates disabled; REFRESH takes the focus.
	Check(runtime.SetState({{"demos.count",0.0},{"demos.first",0.0},{"demos.selected",-1.0},{"demos.filter",0.0},{"demos.canPlay",false},
		{"demos.canDelete",false}},error,8),"no demos");
	runtime.Frame(viewport,8.1);
	Check(text("demo-empty","display") == "block" && text("demo-empty-text","text") == "#str_231115" && text("row-0","display") == "none" &&
		text("demo_emptyclear","display") == "none","the empty state says where demos come from");
	Check(runtime.SetState({{"demos.filter",2.0}},error,8.2),"a filter hides them all");
	runtime.Frame(viewport,8.3);
	Check(text("demo-empty-text","text") == "#str_231118" && text("demo_emptyclear","display") == "block",
		"the empty state says the filter hides them and offers CLEAR FILTERS");
	Check(runtime.RunEvent("clearDemoFilter",8.32,effects,error) && requested("filter") && effects.stateChanges.count("demos.filter"),
		"CLEAR FILTERS clears the filter and asks the session for the list again");
	runtime.Frame(viewport,8.34);
	Check(text("demo_emptyclear","display") == "none" && text("demo-empty-text","text") == "#str_231115","with no filter it is not offered");
	Check(runtime.SetState({{"demos.filter",2.0}},error,8.36),"the filter again");
	runtime.Frame(viewport,8.38);
	Check(runtime.RunEvent("onActivate",8.4,effects,error),"an empty page arrives");
	runtime.Frame(viewport,8.6);
	Check(runtime.FocusedControl() == "demo_refresh","REFRESH has focus");
}

// The title's MODS dialog (section 6): the modal grown into a list dialog, the
// installed mods under NAME and VERSION, the selected mod's details, LOAD MOD
// and CANCEL, and the wordmark dimmed to 40% gray as for EXIT.
static void CheckModsDialog(ScreenHost& host, const char* path) {
	const auto source = Read(path);
	std::vector<Diagnostic> diagnostics;
	Runtime runtime(host);
	Check(runtime.Initialize() && runtime.LoadDocument(source,"guis/menu/title.q4ui",diagnostics),"the title loads into a runtime");
	Viewport viewport; viewport.canvasHeight = 720; viewport.width = 1280; viewport.height = 720;
	std::string error; Runtime::EventEffects effects;
	const auto text = [&](const std::string& node, const char* property) {
		const auto value = runtime.PresentedValue(node,property); return value ? value->text : std::string("?");
	};
	const auto number = [&](const std::string& node, const char* property) {
		const auto value = runtime.PresentedValue(node,property); return value ? static_cast<float>(value->data[0]) : -1.f;
	};
	const auto bounds = [&](const std::string& id) { Bounds b; Check(runtime.GetBounds(id,b),"a dialog node is laid out"); return b; };
	const auto requested = [&](const char* action) {
		return effects.actions.size() == 1 && effects.actions[0].action == action && effects.actions[0].operation == "session.menu";
	};
	StateValues values{{"mods.count",2.0},{"mods.selected",1.0},{"mods.author",std::string("Ritual")},{"mods.date",std::string("2005")},
		{"mods.engine",std::string("0.11")},{"mods.directory",std::string("q4xbase")},{"mods.row0.name",std::string("openQ4")},
		{"mods.row0.version",std::string("0.13")},{"mods.row1.name",std::string("The Awakening")},{"mods.row1.version",std::string("1.0")}};
	Check(runtime.SetState(values,error,0),"publish two mods");
	runtime.Frame(viewport,1);
	Check(text("modsModal","display") == "none","the dialog rests hidden");
	Check(runtime.RunEvent("modsModalShow",2,effects,error),"MODS opens the dialog");
	runtime.Frame(viewport,2.1);
	Check(text("modsModal","display") == "block" && text("modsModal-contents","display") == "none","its contents wait for the frame");
	runtime.Frame(viewport,2.25);
	Check(text("modsModal-contents","display") == "block" && runtime.FocusedControl() == "mods-row-1",
		"the contents show with the selected mod focused");
	// The 4:3 canvas starts at 160 dp at 1280x720; 1 u = 1.5 dp.
	const float x0 = 160, u = 1.5f;
	// The dialog is at least the stock 320 u and as wide as every language's
	// LOAD MOD and CANCEL plates need, centered, 280 u tall.
	const auto dialog = bounds("modsModal-dialog");
	Check(dialog.width >= 320*u-.01f && Near(dialog.x,x0+(960-dialog.width)/2) && Near(dialog.y,83*u) && Near(dialog.height,280*u),
		"the list dialog is the modal grown to 280 u, centred, its centre 17 u above the view's");
	const auto yes = bounds("modsModal_yes"), no = bounds("modsModal_no");
	Check(yes.width >= 180-.01f && Near(yes.width,no.width) && yes.x+yes.width < no.x && no.x+no.width <= dialog.x+dialog.width,
		"LOAD MOD and CANCEL share one width that fits every language, side by side inside the dialog");
	const auto row0 = bounds("mods-row-0"), row1 = bounds("mods-row-1");
	Check(Near(row0.x,dialog.x+22*u) && Near(row0.y,(83+25)*u) && Near(row1.y-row0.y,20*u),"the rows run on 20 u from the list's corner");
	Check(text("mods-row-1-selected","display") == "block" && text("mods-row-0-selected","display") == "none" &&
		text("mods-row-2","display") == "none","two rows show, the selected mod lit");
	Check(text("mods-row-1-name","text") == "The Awakening" && text("mods-row-0-version","text") == "0.13","each row names its mod and version");
	Check(text("mods-author","text") == "Ritual" && text("mods-date","text") == "2005" && text("mods-engine","text") == "0.11" &&
		text("mods-directory","text") == "q4xbase","the selected mod's details stand under the list");
	Check(Near(number("wordmark","image-color"),.4f,.01f),"the wordmark dims to 40% gray");
	Check(runtime.CanActivateControl("modsModal_yes",2.3) && runtime.CanActivateControl("modsModal_no",2.3),"LOAD MOD and CANCEL are live");
	Check(runtime.RunEvent("modsRow0",3,effects,error) && requested("modsSelect") && effects.stateChanges.count("mods.pick") &&
		std::get<double>(effects.stateChanges.at("mods.pick")) == 0,"Accept on another mod selects it");
	Check(runtime.RunEvent("modsRow1",3.1,effects,error) && requested("modsLoad"),"Accept on the selected mod loads it");
	runtime.Frame(viewport,3.6);
	Check(text("modsModal","display") == "none" && Near(number("wordmark","image-color"),1,.01f),"loading closes the dialog and the wordmark lights");
	// No mods: LOAD MOD rests disabled and CANCEL keeps the focus.
	Check(runtime.SetState({{"mods.count",0.0},{"mods.selected",-1.0}},error,4),"no mods");
	Check(runtime.RunEvent("modsModalShow",4.1,effects,error),"the dialog opens again");
	runtime.Frame(viewport,4.4);
	Check(!runtime.CanActivateControl("modsModal_yes",4.4) && runtime.FocusedControl() == "modsModal_no",
		"without mods LOAD MOD rests disabled and CANCEL has focus");
	Check(runtime.RunEvent("modsLoadConfirmed",4.5,effects,error) && effects.actions.empty(),"nothing loads without a selection");
}

// The restart screen after the player dies (FLOW-005): the wordmark over the
// popup art, four enlarged primary plates, the line that explains the plate
// under the pointer or focus, and the exit confirmation.
static void CheckRestartMenu(ScreenHost& host, const char* path) {
	const auto source = Read(path);
	Document document; std::vector<Diagnostic> diagnostics;
	if (!document.Load(source,diagnostics)) for (const auto& d : diagnostics) std::fprintf(stderr,"%u:%u %s %s\n",unsigned(d.line),unsigned(d.column),d.pointer.c_str(),d.message.c_str());
	Check(diagnostics.empty() && document.Model().id == "openq4.restartmenu" && document.Model().canvasHeight == 720,"the restart screen validates");
	CheckSessionActions(document);
	const std::pair<const char*,const char*> verbs[] = {{"restart","deathRestart"},{"load","deathLoad"},{"menu","deathMenu"},{"quit","quit"}};
	for (const auto& [action, command] : verbs) {
		const auto found = document.Model().actions.find(action);
		Check(found != document.Model().actions.end() && std::get<std::string>(found->second.arguments.at("command").literal) == command,
			"each plate asks the session for the stock screen's command");
	}
	Runtime runtime(host);
	Check(runtime.Initialize() && runtime.LoadDocument(source,"guis/menu/restartmenu.q4ui",diagnostics),"the screen loads into a runtime");
	Viewport viewport; viewport.canvasHeight = 720; viewport.width = 1280; viewport.height = 720;
	std::string error; Runtime::EventEffects effects;
	const auto text = [&](const std::string& node, const char* property) {
		const auto value = runtime.PresentedValue(node,property); return value ? value->text : std::string("?");
	};
	const auto number = [&](const std::string& node, const char* property) {
		const auto value = runtime.PresentedValue(node,property); return value ? static_cast<float>(value->data[0]) : -1.f;
	};
	const auto bounds = [&](const std::string& id) { Bounds b; Check(runtime.GetBounds(id,b),"a screen node is laid out"); return b; };
	runtime.Frame(viewport,1);
	const float x0 = 160, u = 1.5f;
	const auto dialog = bounds("death-dialog"), logo = bounds("wordmark");
	Check(Near(dialog.x,x0+140*u) && Near(dialog.y,129*u) && Near(dialog.width,360*u) && Near(dialog.height,212*u),
		"the popup art stands 360 u wide at 140,129 u");
	Check(Near(logo.x,x0+145*u) && Near(logo.y,91*u) && Near(logo.width,258*u) && Near(logo.height,63*u),"QUAKE 4 stands in q4text's box");
	const char* plates[] = {"death_restart","death_load","death_menu","death_exit"};
	for (int index = 0; index < 4; ++index) {
		const auto plate = bounds(plates[index]);
		Check(Near(plate.x,x0+159*u) && Near(plate.y,(162+40*index)*u) && Near(plate.width,324*u) && Near(plate.height,38*u),
			"the plates stand 324x38 u on a 40 u pitch from 159,162 u");
		const auto tip = bounds(std::string(plates[index])+"-tip");
		Check(Near(tip.x,x0+16) && Near(tip.y,362*u) && Near(tip.width,960-32),"each plate's line spans the canvas at 362 u");
	}
	Check(runtime.RunEvent("onActivate",2,effects,error),"the screen arrives");
	runtime.Frame(viewport,2.2);
	Check(runtime.FocusedControl() == "death_restart" && Near(number("death_restart-tip","opacity"),1,.01f) &&
		Near(number("death_load-tip","opacity"),0,.01f),"RESTART has focus and explains itself");
	runtime.MenuAction(MenuInput::Down,true,2.3); runtime.MenuAction(MenuInput::Down,false,2.31);
	runtime.Frame(viewport,2.8);
	Check(runtime.FocusedControl() == "death_load" && Near(number("death_load-tip","opacity"),1,.01f) &&
		Near(number("death_restart-tip","opacity"),0,.01f),"Down moves to LOAD GAME, whose line replaces RESTART's");
	Check(text("death_load-tip","text") == "#str_200914" && text("death_exit-tip","text") == "#str_200323","each plate has its stock line");
	// EXIT GAME asks with the stock confirmation.
	Check(runtime.RunEvent("exitModalShow",3,effects,error) && effects.actions.empty(),"EXIT GAME asks first");
	runtime.Frame(viewport,3.3);
	Check(text("exitModal","display") == "block" && runtime.FocusedControl() == "exitModal_no","the confirmation opens on NO");
}

// The Credits page: the stock credits' layout and choreography compiled into
// one repeating timeline (the Raven logo, then the thirteen screens in the
// stock order), its words in application state, its opaque backing, title and
// BACK.
static void CheckCreditsPage(ScreenHost& host, const char* path) {
	const auto source = Read(path);
	Document document; std::vector<Diagnostic> diagnostics;
	if (!document.Load(source,diagnostics)) for (const auto& d : diagnostics) std::fprintf(stderr,"%u:%u %s %s\n",unsigned(d.line),unsigned(d.column),d.pointer.c_str(),d.message.c_str());
	Check(diagnostics.empty() && document.Model().id == "openq4.credits" && document.Model().canvasHeight == 720,"the Credits page validates");
	CheckSessionActions(document);
	const auto back = document.Model().actions.find("back");
	Check(back != document.Model().actions.end() && std::get<std::string>(back->second.arguments.at("command").literal) == "creditsBack",
		"BACK asks the session");
	const auto loop = std::find_if(document.Model().timelines.begin(),document.Model().timelines.end(),
		[](const Timeline& item) { return item.id == "creditsLoop"; });
	Check(loop != document.Model().timelines.end() && loop->iterations == 0 && loop->durationMs > 100000,
		"the logo and thirteen screens repeat as one timeline over a minute and a half");
	size_t lines = 0;
	for (const auto& [name, declaration] : document.Model().state) lines += name.rfind("credits.",0) == 0;
	Check(lines > 300,"every credit line, rune and Latin, shows application text");
	Runtime runtime(host);
	Check(runtime.Initialize() && runtime.LoadDocument(source,"guis/menu/credits.q4ui",diagnostics),"the page loads into a runtime");
	Viewport viewport; viewport.canvasHeight = 720; viewport.width = 1280; viewport.height = 720;
	std::string error; Runtime::EventEffects effects;
	const auto value = [&](const std::string& node, const char* property, size_t index) {
		const auto found = runtime.PresentedValue(node,property); return found ? static_cast<float>(found->data[index]) : -999.f;
	};
	const auto bounds = [&](const std::string& id) { Bounds b; Check(runtime.GetBounds(id,b),"a credits node is laid out"); return b; };
	Check(runtime.SetState({{"credits.screen1_t_1",std::string("Project Lead")},{"credits.screen1_ts_1",std::string("Project Lead")}},error,0),
		"the session publishes the words");
	runtime.Frame(viewport,0.5);
	const float x0 = 160, u = 1.5f;
	const auto logo = bounds("raven"), heading = bounds("screen1_t_1");
	Check(Near(logo.x,x0+120*u) && Near(logo.y,40*u) && Near(logo.width,400*u) && Near(logo.height,400*u),"the logo stands in its stock box");
	Check(Near(heading.x,x0+103*u) && Near(heading.y,82*u),"a heading keeps its stock rect");
	Check(runtime.PresentedValue("screen1_t_1","text")->text == "Project Lead","the heading shows the published words");
	Check(runtime.RunEvent("onActivate",1,effects,error) && runtime.FocusedControl() == "credits_back","the page arrives with BACK focused");
	runtime.Frame(viewport,1.2);
	Check(Near(value("credits-panel","opacity",0),1,.01f),"the backing and panel fade in over 150 ms");
	// The logo lights from black over 1000 ms, holds, and goes out by 4000 ms.
	runtime.Frame(viewport,2.05);
	Check(Near(value("raven","image-color",0),.839f,.02f) && Near(value("raven","image-color",1),1,.02f),"the logo has lit");
	runtime.Frame(viewport,5.05);
	Check(Near(value("raven","image-color",1),0,.02f) && Near(value("p_screen1","opacity",0),1,.01f),"the logo is out and the first screen shows");
	// The first screen's runes fade in over 2000 ms; at 2000 ms the first
	// heading translates over 1000 ms behind its sliding scan bar.
	runtime.Frame(viewport,6.0);
	const float runes = value("screen1_ts_1","color",3);
	Check(runes > .1f && runes < .8f && Near(value("screen1_t_1","color",3),0,.01f),"the heading's runes are fading in, its Latin not yet");
	runtime.Frame(viewport,7.5);
	Check(value("screen1_t_1","color",3) > .2f && value("screen1_ts_1","color",3) < runes + .5f,"the heading translates from runes to Latin");
	Check(value("screen1_bar1","opacity",0) > 0 && value("screen1_bar1","transform",0) > 0,"its scan bar has slid in from the leading edge");
	// Leaving: the screen slides 640 u toward the trailing edge.
	runtime.Frame(viewport,1+4+7.25);
	Check(value("p_screen1","transform",0) > 600 || Near(value("p_screen1","opacity",0),0,.01f),"the first screen leaves toward the trailing edge");
	Check(runtime.RunEvent("onDeactivate",13,effects,error),"the page leaves the screen");
	runtime.Frame(viewport,13.1);
	Check(Near(value("p_screen2","opacity",0),0,.01f),"leaving stops the loop with every screen out");
}

// The Arena sub-page (FLOW-011, FLOW-026, FLOW-050): the Arena campaign's
// browser in Campaign's sub-page state. The tier ladder and the chosen tier's
// matches show the published locks, checks and choice; a plate's first Accept
// chooses it, Accept on the chosen tier moves to its chosen match and on the
// chosen match starts it; the bumpers switch tier; the entrance, the result
// ledger and RESET PROGRESS's confirmation; Back's half of the change. Single
// Player's ARENA leads deeper with its own carry and returns onto ARENA.
static void CheckArenaPage(ScreenHost& host, const char* pagePath, const char* path) {
	const auto source = Read(path);
	Document document; std::vector<Diagnostic> diagnostics;
	if (!document.Load(source,diagnostics)) for (const auto& d : diagnostics) std::fprintf(stderr,"%u:%u %s %s\n",unsigned(d.line),unsigned(d.column),d.pointer.c_str(),d.message.c_str());
	Check(diagnostics.empty() && document.Model().id == "openq4.arena" && document.Model().canvasHeight == 720,"the Arena page validates");
	CheckSessionActions(document);
	const std::pair<const char*,const char*> verbs[] = {{"back","arenaBack"},{"tier0","arenaTier0"},{"tier4","arenaTier4"},{"match0","arenaMatch0"},
		{"match3","arenaMatch3"},{"start","arenaStart"},{"tierPrevious","arenaTierPrevious"},{"tierNext","arenaTierNext"},{"easier","arenaEasier"},
		{"harder","arenaHarder"},{"reset","arenaReset"},{"resetConfirm","arenaResetConfirm"},{"resetCancel","arenaResetCancel"},
		{"retry","arenaRetry"},{"dismiss","arenaDismiss"}};
	for (const auto& [action, command] : verbs) {
		const auto found = document.Model().actions.find(action);
		Check(found != document.Model().actions.end() && std::get<std::string>(found->second.arguments.at("command").literal) == command,
			"each verb asks the session for the campaign's command");
	}
	Runtime runtime(host);
	Check(runtime.Initialize() && runtime.LoadDocument(source,"guis/menu/arena.q4ui",diagnostics),"the page loads into a runtime");
	Viewport viewport; viewport.canvasHeight = 720; viewport.width = 1280; viewport.height = 720;
	std::string error; Runtime::EventEffects effects;
	const auto text = [&](const std::string& node, const char* property) {
		const auto found = runtime.PresentedValue(node,property); return found ? found->text : std::string("?");
	};
	const auto value = [&](const std::string& node, const char* property, size_t index = 0) {
		const auto found = runtime.PresentedValue(node,property); return found ? static_cast<float>(found->data[index]) : -999.f;
	};
	const auto bounds = [&](const std::string& id) { Bounds b; Check(runtime.GetBounds(id,b),"an Arena node is laid out"); return b; };
	const auto acted = [&](const char* action) { return effects.actions.size() == 1 && effects.actions[0].action == action; };
	// The campaign publishes its browser: TIER 1 complete, TIER 2 chosen with
	// its first qualifier won and its second chosen, its boss still locked.
	StateValues campaign = {{"arena_loaded",true},{"arena_canStart",true},{"arena_difficulty",3.0},{"arenaCampaignProgress",5.0},
		{"arena_canReset",true},{"arena_selectedBestVisible",true},{"arena_selectedHasBest",true},
		{"arena_selectedBest",std::string("VICTORY   1 - 0   VETERAN")},
		{"arena_progress",std::string("CAMPAIGN PROGRESS  5 / 20")},{"arena_selectedTitle",std::string("Shifting Lines")},
		{"arena_selectedTierName",std::string("Ironworks")},{"arena_difficultyName",std::string("Veteran")},
		{"arena_levelshot",std::string("gfx/guis/loadscreens/generic")}};
	for (int tier = 0; tier < 5; ++tier) {
		const std::string prefix = "arena_tier" + std::to_string(tier);
		campaign[prefix+"_locked"] = tier > 1; campaign[prefix+"_selected"] = tier == 1; campaign[prefix+"_completed"] = tier == 0;
		campaign[prefix+"_status"] = std::string(tier == 0 ? "COMPLETE" : tier == 1 ? "AVAILABLE" : "LOCKED");
	}
	for (int match = 0; match < 4; ++match) {
		const std::string prefix = "arena_match" + std::to_string(match);
		campaign[prefix+"_locked"] = match == 3; campaign[prefix+"_selected"] = match == 1; campaign[prefix+"_completed"] = match == 0;
		campaign[prefix+"_status"] = std::string(match == 0 ? "COMPLETE" : match == 3 ? "LOCKED" : "AVAILABLE");
	}
	Check(runtime.SetState(campaign,error,0),"the campaign publishes its browser");
	runtime.Frame(viewport,0.5);
	// The ladder on the 4:3 canvas (x = 160 dp at 1280x720; 1 u = 1.5 dp).
	const float x0 = 160, u = 1.5f;
	const auto tier0 = bounds("tier-0"), tier4 = bounds("tier-4"), match0 = bounds("match-0"), boss = bounds("match-3"),
		start = bounds("arena_start"), back = bounds("arena_back"), title = bounds("title"), shot = bounds("arena-shot");
	Check(Near(tier0.x,x0+16*u) && Near(tier0.y,72*u) && Near(tier0.width,170*u) && Near(tier0.height,40*u) && Near(tier4.y,(72+4*44)*u),
		"the five tiers form a ladder of plates in the section navigation");
	Check(Near(match0.x,x0+196*u) && Near(match0.y,72*u) && Near(match0.height,36*u) && Near(boss.y,194*u) && Near(boss.height,50*u) &&
		boss.height > match0.height,"the matches stand beside it, the boss on a larger plate");
	Check(Near(start.x,x0+404*u) && Near(start.y,354*u) && Near(start.width,226*u) && Near(start.height,43*u),
		"START MATCH is the enlarged primary plate at the foot of the detail column");
	Check(Near(shot.x,x0+405*u) && Near(shot.y,59*u),"the levelshot leads the detail column");
	// The chosen match's best result across the foot of its levelshot.
	const auto best = bounds("arena-best");
	Check(Near(best.x,shot.x,.5f) && Near(best.width,shot.width,.5f) && Near(best.y+best.height,shot.y+shot.height,.5f) && Near(best.height,23*u,.5f) &&
		text("arena-best-value","display") == "block" && text("arena-best-none","display") == "none" &&
		text("arena-best-value","text") == "VICTORY   1 - 0   VETERAN","a record shows in amber across the levelshot's foot");
	Check(Near(back.x,x0+532*u) && Near(back.y,441*u) && Near(title.x,x0+136*u,1) && Near(title.y,19*u,1),
		"BACK stands in the bottom band and ARENA titles the page at the step's foot");
	Check(text("title","text") == "#str_42002" && text("crumb","text") == "#str_42000","SINGLE PLAYER is the crumb, ARENA the title");
	// Locks, checks and the choice, as published.
	Check(text("tier-0-check","display") == "block" && text("tier-0-done","display") == "block" && text("tier-0-lock","display") == "none",
		"a completed tier carries the check and the green edge");
	Check(text("tier-2-lock","display") == "block" && !runtime.CanActivateControl("tier-2",0.6) && runtime.CanActivateControl("tier-0",0.6),
		"a locked tier is dimmed with the lock and cannot be chosen");
	Check(text("tier-1-plate-lit","display") == "block" && text("tier-1-wash","display") == "block" && text("tier-1-label-lit","display") == "block" &&
		text("tier-1-label","display") == "none" && text("tier-0-wash","display") == "none","the chosen tier is lit over its orange wash");
	Check(text("match-3-lock","display") == "block" && text("match-3-ready","display") == "none" && !runtime.CanActivateControl("match-3",0.6),
		"the boss waits locked");
	Check(text("tier-1-status","text") == "AVAILABLE" && text("arena-match-title","text") == "Shifting Lines" &&
		text("arena-tier-name","text") == "Ironworks" && text("arena-difficulty","text") == "Veteran","the published words show");
	Check(Near(bounds("arena-progress-fill").width,5*166*u/20,.5f),"the progress bar fills a twentieth a match");
	campaign["arena_match3_locked"] = false; campaign["arena_match3_status"] = std::string("BOSS READY");
	Check(runtime.SetState(campaign,error,0.7),"the qualifiers open the boss");
	runtime.Frame(viewport,0.71);
	Check(text("match-3-ready","display") == "block" && text("match-3-lock","display") == "none" && runtime.CanActivateControl("match-3",0.72),
		"the waiting boss shows its orange edge");
	// Arriving: the content at once, the backing fading in, the focus on the chosen match.
	Check(runtime.RunEvent("onActivate",1,effects,error),"the page arrives");
	runtime.Frame(viewport,1.075);
	Check(Near(value("content","opacity"),1,.01f) && value("arena-backing","opacity") > .3f && value("arena-backing","opacity") < .7f &&
		runtime.FocusedControl() == "match-1","the content shows at once, the backing fades in, the chosen match has focus");
	// Accept: a plate's first Accept chooses it; on the chosen tier it moves to
	// the chosen match, on the chosen match it starts it.
	Check(runtime.RunEvent("tierPick0",1.2,effects,error) && acted("tier0"),"an unchosen tier asks to be chosen");
	Check(runtime.FocusControl("tier-1",1.3) && runtime.RunEvent("tierPick1",1.3,effects,error) && effects.actions.empty(),"the chosen tier asks nothing");
	runtime.Frame(viewport,1.35);
	Check(runtime.FocusedControl() == "match-1","and moves to its chosen match");
	Check(runtime.RunEvent("matchPick2",1.4,effects,error) && acted("match2"),"an unchosen match asks to be chosen");
	Check(runtime.RunEvent("matchPick1",1.5,effects,error) && acted("start"),"Accept on the chosen match starts it");
	campaign["arena_canStart"] = false;
	Check(runtime.SetState(campaign,error,1.6) && runtime.RunEvent("matchPick1",1.6,effects,error) && effects.actions.empty() &&
		!runtime.CanActivateControl("arena_start",1.61),"a match the campaign cannot start stays put");
	campaign["arena_canStart"] = true;
	Check(runtime.SetState(campaign,error,1.7),"startable again");
	// The bumpers switch tier.
	Check(runtime.RunEvent("onTabNext",1.8,effects,error) && acted("tierNext") && runtime.RunEvent("onTabPrevious",1.8,effects,error) &&
		acted("tierPrevious"),"the bumpers switch tier");
	// DIFFICULTY steps within 1-5.
	campaign["arena_difficulty"] = 1.0;
	Check(runtime.SetState(campaign,error,1.9) && !runtime.CanActivateControl("arena_easier",1.91) && runtime.CanActivateControl("arena_harder",1.91),
		"the easiest difficulty steps only up");
	campaign["arena_difficulty"] = 5.0;
	Check(runtime.SetState(campaign,error,1.95) && runtime.CanActivateControl("arena_easier",1.96) && !runtime.CanActivateControl("arena_harder",1.96),
		"the hardest only down");
	// RESET PROGRESS asks with the stock modal once the campaign holds the request.
	campaign["arena_canReset"] = false;
	Check(runtime.SetState(campaign,error,2) && !runtime.CanActivateControl("arena_reset",2.01),"nothing to reset without progress or a record");
	campaign["arena_canReset"] = true;
	Check(runtime.SetState(campaign,error,2.05) && runtime.CanActivateControl("arena_reset",2.06),"RESET PROGRESS is live with something to clear");
	Check(runtime.RunEvent("arenaResetReveal",2.1,effects,error),"the campaign asks for the confirmation");
	runtime.Frame(viewport,2.4);
	Check(text("resetModal","display") == "block" && runtime.FocusedControl() == "resetModal_no" &&
		text("resetModal-title","text") == "#str_42011" && text("resetModal-body","text") == "#str_42073","it asks on CANCEL, saying what it clears");
	Check(runtime.RunEvent("onTabNext",2.45,effects,error) && effects.actions.empty(),"the bumpers wait while it asks");
	Check(runtime.RunEvent("resetConfirmed",2.5,effects,error) && acted("resetConfirm"),"RESET clears the progress");
	runtime.Frame(viewport,2.9);
	Check(text("resetModal","display") == "none","and the modal closes");
	// The entrance: every control out while the campaign holds it.
	campaign["arena_transitioning"] = true;
	Check(runtime.SetState(campaign,error,3) && !runtime.CanActivateControl("arena_start",3.01) && !runtime.CanActivateControl("arena_back",3.01) &&
		!runtime.CanActivateControl("tier-0",3.01),"the entrance holds every control");
	Check(runtime.RunEvent("arenaMatchLaunch",3,effects,error),"the match launches");
	runtime.Frame(viewport,3.001);
	Check(text("launch","display") == "block" && value("launch-card","opacity") < .05f,"the entrance starts from the page");
	runtime.Frame(viewport,3.45);
	Check(Near(value("launch-shade","opacity"),.72f,.02f) && Near(value("launch-card","opacity"),1,.02f) &&
		text("launch-title","text") == "Shifting Lines","by 450 ms the screen is darkened under the card");
	runtime.Frame(viewport,4.26);
	Check(Near(value("launch-shade","opacity"),1,.01f) && Near(value("launch-card","opacity"),0,.01f),"by 1260 ms it is black for the hand-over");
	Check(runtime.RunEvent("onDeactivate",4.3,effects,error),"the match takes the screen");
	runtime.Frame(viewport,4.31);
	Check(text("launch","display") == "none","leaving closes the entrance");
	// The result ledger, revealed in the stock phases from black.
	campaign["arena_transitioning"] = false; campaign["arena_result"] = 1.0; campaign["arena_resultCanRetry"] = true;
	campaign["arena_resultTitle"] = std::string("VICTORY"); campaign["arena_resultHasScore"] = true; campaign["arena_resultScore"] = std::string("15  -  9");
	Check(runtime.SetState(campaign,error,5) && runtime.RunEvent("onActivate",5,effects,error) &&
		runtime.RunEvent("arenaResultReveal",5,effects,error),"back from the match the ledger reveals");
	runtime.Frame(viewport,5.001);
	Check(Near(value("arena-curtain","opacity"),1,.01f) && Near(value("result-phase1","opacity"),0,.01f),"from black");
	runtime.Frame(viewport,5.5);
	Check(Near(value("result-phase1","opacity"),1,.01f) && Near(value("result-phase2","opacity"),0,.01f) && text("result-actions","display") == "none" &&
		text("result-title","text") == "VICTORY" && text("result-accent-win","display") == "block","first the result over its accent");
	runtime.Frame(viewport,5.8);
	Check(Near(value("result-phase2","opacity"),1,.01f) && text("result-score-row","display") == "block" && text("result-score","text") == "15  -  9",
		"then the message and score");
	Check(text("result-new-best","display") == "none","no NEW BEST short of the record");
	campaign["arena_resultNewBest"] = true;
	Check(runtime.SetState(campaign,error,5.81),"a replay beat the record");
	runtime.Frame(viewport,5.82);
	Check(text("result-new-best","display") == "block" && bounds("result-new-best").x+bounds("result-new-best").width < bounds("result-score").x+
		bounds("result-score").width,"NEW BEST rides the score row");
	runtime.Frame(viewport,6.3);
	Check(text("result-actions","display") == "block" && runtime.FocusedControl() == "resultModal_no" &&
		Near(value("arena-curtain","opacity"),0,.01f),"at 1260 ms the actions, on the safe one");
	Check(text("resultModal_yes-label","text") == "#str_42076" && text("resultModal_no-label","text") == "#str_42032" &&
		text("resultModal_yes","display") == "block","after a win: REPLAY and NEXT");
	campaign["arena_result"] = 2.0;
	Check(runtime.SetState(campaign,error,6.4),"a defeat");
	runtime.Frame(viewport,6.41);
	Check(text("resultModal_yes-label","text") == "#str_42033" && text("resultModal_no-label","text") == "#str_42034" &&
		text("result-accent-loss","display") == "block","after a defeat: RETRY and RETURN");
	campaign["arena_result"] = 3.0;
	Check(runtime.SetState(campaign,error,6.5),"an aborted match");
	runtime.Frame(viewport,6.51);
	Check(text("resultModal_yes","display") == "block" && text("resultModal_yes-label","text") == "#str_42033" &&
		text("resultModal_no-label","text") == "#str_42034","an aborted match that can be played again: RETRY and RETURN");
	campaign["arena_resultCanRetry"] = false;
	Check(runtime.SetState(campaign,error,6.52),"one that cannot");
	runtime.Frame(viewport,6.53);
	Check(text("resultModal_yes","display") == "none" && text("resultModal_no-label","text") == "#str_42025","otherwise it only CONTINUEs");
	// The banner is the completing win's; a later win keeps its awards.
	campaign["arena_result"] = 1.0; campaign["arena_campaignComplete"] = true; campaign["arena_resultHasAwards"] = true;
	campaign["arena_resultAwardLine"] = std::string("Excellent x2");
	Check(runtime.SetState(campaign,error,6.54),"a win once the campaign is complete");
	runtime.Frame(viewport,6.55);
	Check(text("result-awards-row","display") == "block" && text("result-complete","display") == "none","a replay's awards show");
	campaign["arena_resultUnlockedCampaign"] = true;
	Check(runtime.SetState(campaign,error,6.56),"the win that completed it");
	runtime.Frame(viewport,6.57);
	Check(text("result-awards-row","display") == "none" && text("result-complete","display") == "block","its banner takes their place");
	Check(runtime.RunEvent("resultDismiss",6.6,effects,error) && acted("dismiss"),"CONTINUE dismisses the ledger");
	runtime.Frame(viewport,7);
	Check(text("resultModal","display") == "none","and it closes");
	// Back: the title drops, the page sweeps 640 u toward the leading edge,
	// the band steps back and the backing goes.
	Check(runtime.RunEvent("subpageLeave",8,effects,error),"Back climbs one level");
	runtime.Frame(viewport,8.3);
	Check(Near(value("content","transform",0),-640*u,.5f) && Near(value("title","color",3),0,.001f) && Near(value("arena-backing","opacity"),0,.001f) &&
		Near(value("band-top","transform",0),323*u,.05f),"by 300 ms the page has swept out and the band is docked");
	Check(runtime.RunEvent("onDeactivate",8.4,effects,error),"the page leaves the screen");
	runtime.Frame(viewport,8.41);
	Check(Near(value("content","transform",0),0,.01f) && Near(value("band-top","transform",0),(323+97)*u,.05f) &&
		Near(value("arena-backing","opacity"),0,.001f),"hidden, it waits at rest in the sub-page state");
	// Single Player: ARENA carries its label to the step's foot, and Back from
	// Arena returns onto ARENA.
	const auto pageSource = Read(pagePath);
	Runtime page(host);
	Check(page.Initialize() && page.LoadDocument(pageSource,"guis/menu/singleplayer.q4ui",diagnostics),"Single Player loads");
	Check(page.RunEvent("onActivate",9,effects,error) && page.RunEvent("subpageEnterArena",9.1,effects,error),"ARENA leads deeper");
	page.Frame(viewport,9.11);
	const auto from = page.PresentedValue("arena-carry","transform");
	Check(from && Near(static_cast<float>(from->data[1]),(190+30+2.8f-19)*u,.05f) && Near(static_cast<float>(page.PresentedValue("subpage-carry","opacity")->data[0]),0,.001f),
		"the ARENA label starts on its row, CAMPAIGN's stays out");
	page.Frame(viewport,9.45);
	Bounds carry;
	const auto carried = page.PresentedValue("arena-carry","transform");
	Check(carried && Near(static_cast<float>(carried->data[2]),.75f,.001f) && page.GetBounds("arena-carry",carry) && Near(carry.x,x0+136*u,1),
		"at 350 ms ARENA sits at the step's foot");
	Check(page.RunEvent("onDeactivate",9.5,effects,error) && page.SetState({{"from_arena",true}},error,10) &&
		page.RunEvent("onActivate",10,effects,error),"Single Player returns from Arena");
	page.Frame(viewport,10.05);
	Check(page.FocusedControl() == "arena" && Near(static_cast<float>(page.PresentedValue("arena-carry","opacity")->data[0]),0,.001f),
		"onto ARENA, with no carry");
	// Each section summarizes itself in the column while it is the subject
	// (section 13.7), over its levelshot once the session names one; BACK
	// keeps the description.
	const auto lit = [&](const char* node) { const auto found = page.PresentedValue(node,"opacity"); return found ? static_cast<float>(found->data[0]) : -1.f; };
	const auto shown = [&](const char* node) { const auto found = page.PresentedValue(node,"display"); return found ? found->text : std::string("?"); };
	Check(page.SetState({{"summary.arena.shot",std::string("gfx/guis/loadscreens/arena1")}},error,10.1),"the session names ARENA's levelshot");
	page.Frame(viewport,10.15);
	Check(Near(lit("summary-arena"),1,.001f) && Near(lit("summary-campaign"),0,.001f) && Near(lit("description"),0,.001f) &&
		shown("summary-arena-shots") == "block" && Near(lit("summary-arena-shots"),1,.001f),"ARENA summarizes itself over its match's levelshot");
	Check(page.FocusControl("campaign",10.2),"focus CAMPAIGN");
	page.Frame(viewport,10.25);
	Check(Near(lit("summary-campaign"),1,.001f) && Near(lit("summary-arena-shots"),0,.001f) && shown("summary-campaign-shots") == "none",
		"CAMPAIGN without a save shows no picture");
	Check(page.FocusControl("back",10.3),"focus BACK");
	page.Frame(viewport,10.35);
	Check(Near(lit("description"),1,.001f) && Near(lit("summary-campaign"),0,.001f),"BACK keeps the description");
}

// The Multiplayer page (FLOW-050): the stock Multiplayer page's level in the
// page state. JOIN GAME, CREATE SERVER, DEMOS and PLAYER SETUP ask the session
// for their pages and BACK for the title; a section's choice sends the page
// away as the stock page leaves for one (its plates fade over 150 ms while it
// sweeps 640 u toward the trailing edge over 300 ms), and hidden it rests.
static void CheckMultiplayerPage(ScreenHost& host, const char* path) {
	const auto source = Read(path);
	Document document; std::vector<Diagnostic> diagnostics;
	if (!document.Load(source,diagnostics)) for (const auto& d : diagnostics) std::fprintf(stderr,"%u:%u %s %s\n",unsigned(d.line),unsigned(d.column),d.pointer.c_str(),d.message.c_str());
	Check(diagnostics.empty() && document.Model().id == "openq4.multiplayer" && document.Model().canvasHeight == 720,"the Multiplayer page validates");
	CheckSessionActions(document);
	const std::pair<const char*,const char*> verbs[] = {{"back","multiplayerBack"},{"multiplayerJoin","multiplayerJoin"},
		{"multiplayerCreate","multiplayerCreate"},{"multiplayerDemos","multiplayerDemos"},{"multiplayerSetup","multiplayerSetup"}};
	for (const auto& [action, command] : verbs) {
		const auto found = document.Model().actions.find(action);
		Check(found != document.Model().actions.end() && std::get<std::string>(found->second.arguments.at("command").literal) == command,
			"each section asks the session for its page");
	}
	Runtime runtime(host);
	Check(runtime.Initialize() && runtime.LoadDocument(source,"guis/menu/multiplayer.q4ui",diagnostics),"the page loads into a runtime");
	Viewport viewport; viewport.canvasHeight = 720; viewport.width = 1280; viewport.height = 720;
	std::string error; Runtime::EventEffects effects;
	const auto text = [&](const std::string& node, const char* property) {
		const auto found = runtime.PresentedValue(node,property); return found ? found->text : std::string("?");
	};
	const auto value = [&](const std::string& node, const char* property, size_t index = 0) {
		const auto found = runtime.PresentedValue(node,property); return found ? static_cast<float>(found->data[index]) : -999.f;
	};
	const auto bounds = [&](const std::string& id) { Bounds b; Check(runtime.GetBounds(id,b),"a Multiplayer node is laid out"); return b; };
	runtime.Frame(viewport,0.5);
	// The 4:3 canvas at 1280x720: x = 160 dp, 1 u = 1.5 dp.
	const float x0 = 160, u = 1.5f;
	const char* const rows[] = {"join","create","demos","setup","back"};
	const char* const keys[] = {"#str_200187","#str_200026","#str_41500","#str_231122","#str_230045"};
	for (int row = 0; row < 5; ++row) {
		const std::string label = std::string(rows[row]) + "-label";
		const auto box = bounds(label);
		Check(Near(box.x,x0+44*u) && Near(box.y,(181+30*row+2.8f)*u,.05f) && text(label,"text") == keys[row],
			"the sections and BACK stand on the home navigation plates from 181 u, on their 30 u pitch");
	}
	const auto title = bounds("title"), details = bounds("details");
	Check(Near(title.x,x0+39*u,1) && Near(title.y,19*u,1) && text("title","text") == "#str_200002","MULTIPLAYER titles the page in the top band's slot");
	Check(Near(details.x,x0+395*u) && Near(details.y,190*u) && text("description","text") == "#str_231550",
		"the description stands in the content column");
	Check(Near(value("band-top","transform",0),323*u,.05f) && Near(value("band-top","transform",1),-63*u,.05f),"the top band is docked");
	// Arriving: the plates at once, the description fading in over 150 ms.
	Check(runtime.RunEvent("onActivate",1,effects,error),"the page arrives");
	runtime.Frame(viewport,1.075);
	Check(Near(value("content","opacity"),1,.01f) && value("details","opacity") > .3f && value("details","opacity") < .7f,
		"the plates show at once and the description fades in");
	runtime.Frame(viewport,1.2);
	Check(Near(value("details","opacity"),1,.01f),"by 150 ms the description is in");
	// Each section summarizes itself in the column while it is the subject
	// (section 13.7), CREATE SERVER over si_map's levelshot once the session
	// names one; BACK, or no subject, keeps the description.
	const char* const summaries[] = {"summary-join","summary-create","summary-demos","summary-setup"};
	for (int row = 0; row < 4; ++row) {
		Check(runtime.FocusControl(rows[row],1.21+row*.01),"focus a section");
		runtime.Frame(viewport,1.215+row*.01);
		bool alone = Near(value("description","opacity"),0,.001f);
		for (int other = 0; other < 4; ++other) alone = alone && Near(value(summaries[other],"opacity"),other == row ? 1.f : 0.f,.001f);
		Check(alone && Near(bounds(summaries[row]).y,details.y,.5f),"its summary takes the description's place");
		Check(text("summary-create-shots","display") == "none","no levelshot shows while the session names none");
	}
	Check(runtime.SetState({{"summary.create.shot",std::string("gfx/guis/loadscreens/q4dm5")}},error,1.26) && runtime.FocusControl("create",1.26),
		"the session names CREATE SERVER's levelshot");
	runtime.Frame(viewport,1.265);
	const auto shot = bounds("summary-create-shot-back");
	Check(text("summary-create-shots","display") == "block" && Near(value("summary-create-shots","opacity"),1,.001f) &&
		Near(shot.x,x0+395*u,.5f) && Near(shot.y,77*u,.5f) && shot.y+shot.height <= details.y+.5f,"its levelshot stands over the column");
	Check(runtime.FocusControl("back",1.27),"focus BACK");
	runtime.Frame(viewport,1.275);
	Check(Near(value("description","opacity"),1,.001f) && Near(value("summary-create","opacity"),0,.001f) &&
		Near(value("summary-create-shots","opacity"),0,.001f),"BACK keeps the description");
	Check(runtime.RunEvent("onBack",1.3,effects,error) && effects.actions.size() == 1 && effects.actions[0].action == "back",
		"Back asks for the title");
	// A section's choice: the page leaves as the stock page leaves for one.
	Check(runtime.RunEvent("depart",2,effects,error),"a section sends the page away");
	runtime.Frame(viewport,2.075);
	const float fading = value("content","opacity"), swept = value("content","transform",0);
	Check(fading > .3f && fading < .7f && swept > 100*u && swept < 400*u,"its plates fade over 150 ms while it sweeps");
	runtime.Frame(viewport,2.31);
	Check(Near(value("content","opacity"),0,.01f) && Near(value("content","transform",0),640*u,.05f) && Near(value("title","color",3),0,.01f) &&
		Near(value("prompts","opacity"),0,.01f),"by 300 ms it has swept 640 u toward the trailing edge, its title and prompts out");
	// Hidden, it rests, so it never presents moved, its content out so a
	// return fades it in from nothing.
	Check(runtime.RunEvent("onDeactivate",3,effects,error),"the page leaves the screen");
	runtime.Frame(viewport,3.05);
	Check(Near(value("content","transform",0),0,.05f) && Near(value("content","opacity"),0,.01f) && Near(value("details","opacity"),0,.01f) &&
		Near(value("title","color",3),.5f,.01f) && Near(value("prompts","opacity"),1,.01f) && Near(value("create-carry","opacity"),0,.01f),
		"hidden, it waits at rest with its content out");
	// Arriving again puts its plates back at once.
	Check(runtime.RunEvent("onActivate",3.1,effects,error),"the page arrives again");
	runtime.Frame(viewport,3.15);
	Check(Near(value("content","opacity"),1,.01f),"its plates show at once");
	// CREATE SERVER goes deeper: the plates fade over 150 ms while the page
	// sweeps 640 u; the title becomes the crumb; from 50 ms the band steps
	// one notch and CREATE SERVER carries to the step's foot.
	const float pageTop = 323*u, subTop = (323+97)*u;
	Check(runtime.RunEvent("subpageEnterCreate",4,effects,error),"CREATE SERVER leads deeper");
	runtime.Frame(viewport,4.075);
	Check(Near(value("content","opacity"),.5f,.02f) && Near(value("content","transform",0),640*u/4,.5f) &&
		value("band-top","transform",0) > pageTop && value("band-top","transform",0) < subTop,
		"at 75 ms the plates are half gone, the page a quarter swept and the band stepping");
	runtime.Frame(viewport,4.15);
	Check(Near(value("title","color",3),.4f,.01f) && value("title","transform",2) < .81f && Near(value("content","opacity"),0,.01f),
		"by 150 ms the title is the crumb and the plates are gone");
	runtime.Frame(viewport,4.35);
	Bounds carry;
	Check(Near(value("band-top","transform",0),subTop,.05f) && Near(value("content","transform",0),640*u,.5f) &&
		Near(value("create-carry","transform",2),.75f,.001f) && Near(value("create-carry","color",3),.5f,.01f) &&
		runtime.GetBounds("create-carry",carry) && Near(carry.x,x0+136*u,1),
		"at 350 ms the band has stepped, the page has swept and CREATE SERVER sits at the step's foot");
	// Back from Create Server: hidden at the hand-over, the page returns at
	// once docked and fades its content in over 150 ms, onto CREATE SERVER.
	Check(runtime.RunEvent("onDeactivate",4.4,effects,error),"the page left the screen at the hand-over");
	Check(runtime.SetState({{"from_create",true}},error,5) && runtime.RunEvent("onActivate",5,effects,error) &&
		runtime.RunEvent("subpageReturn",5,effects,error),"the page returns from Create Server");
	runtime.Frame(viewport,5.0005);
	Check(Near(value("band-top","transform",0),pageTop,.05f),"its first frame already shows it docked");
	runtime.Frame(viewport,5.075);
	Check(Near(value("content","opacity"),.5f,.02f) && Near(value("title","transform",2),1,.001f) && Near(value("create-carry","opacity"),0,.01f) &&
		runtime.FocusedControl() == "create","it fades back in place onto CREATE SERVER, its title in the slot and no carry");
	// Back while going deeper: every part returns from where it stands.
	runtime.Frame(viewport,5.3);
	Check(runtime.RunEvent("subpageEnterCreate",6,effects,error),"CREATE SERVER leads deeper again");
	runtime.Frame(viewport,6.12);
	const float partway = value("band-top","transform",0);
	Check(runtime.RunEvent("subpageReverse150",6.12,effects,error),"Back reverses the change");
	runtime.Frame(viewport,6.3);
	Check(partway > pageTop && Near(value("band-top","transform",0),pageTop,.05f) && Near(value("content","transform",0),0,.05f) &&
		Near(value("content","opacity"),1,.01f) && Near(value("title","transform",2),1,.001f) && Near(value("create-carry","opacity"),0,.01f),
		"by 150 ms the page is back as it was");
	// JOIN GAME goes deeper the same way, its own label carrying from its row,
	// one row above CREATE SERVER's; back from Join Game the page returns onto
	// JOIN GAME.
	runtime.Frame(viewport,6.5);
	Check(runtime.RunEvent("onDeactivate",6.5,effects,error) && runtime.RunEvent("onActivate",6.6,effects,error),
		"the page leaves and arrives again, its carries at rest on their rows");
	runtime.Frame(viewport,6.9);
	Check(runtime.RunEvent("subpageEnterJoin",7,effects,error),"JOIN GAME leads deeper");
	runtime.Frame(viewport,7.01);
	Check(Near(value("join-carry","opacity"),1,.01f) && Near(value("create-carry","opacity"),0,.01f) &&
		Near(value("create-carry","transform",1)-value("join-carry","transform",1),30*u,.05f),
		"JOIN GAME's label sets out from its row, one row above CREATE SERVER's");
	runtime.Frame(viewport,7.35);
	Bounds joinCarry;
	Check(Near(value("band-top","transform",0),subTop,.05f) && Near(value("content","opacity"),0,.01f) &&
		Near(value("join-carry","transform",2),.75f,.001f) && Near(value("join-carry","color",3),.5f,.01f) &&
		runtime.GetBounds("join-carry",joinCarry) && Near(joinCarry.x,x0+136*u,1) && text("join-carry","text") == "#str_200187",
		"at 350 ms JOIN GAME sits at the step's foot");
	Check(runtime.RunEvent("onDeactivate",7.4,effects,error),"the page left the screen at the hand-over");
	Check(runtime.SetState({{"from_create",false},{"from_join",true}},error,8) && runtime.RunEvent("onActivate",8,effects,error) &&
		runtime.RunEvent("subpageReturn",8,effects,error),"the page returns from Join Game");
	runtime.Frame(viewport,8.075);
	Check(runtime.FocusedControl() == "join" && Near(value("join-carry","opacity"),0,.01f) && Near(value("content","opacity"),.5f,.02f),
		"it fades back in place onto JOIN GAME, with no carry");
}

// The Join Game sub-page (FLOW-050), Multiplayer's server browser: the stock
// browser's content in the sub-page state under MULTIPLAYER's crumb. The
// session lists the servers (join.row<i>.*) a page at a time; each column
// heading sorts by its column, its chevron showing the sort's direction; the
// source tags light the list's source; a row's first Accept chooses its
// server, and Accept on the chosen one moves on to JOIN SERVER, which applies
// once a server is chosen; an empty list says so unless a scan runs.
static void CheckJoinGamePage(ScreenHost& host, const char* path) {
	const auto source = Read(path);
	Document document; std::vector<Diagnostic> diagnostics;
	if (!document.Load(source,diagnostics)) for (const auto& d : diagnostics) std::fprintf(stderr,"%u:%u %s %s\n",unsigned(d.line),unsigned(d.column),d.pointer.c_str(),d.message.c_str());
	Check(diagnostics.empty() && document.Model().id == "openq4.joingame" && document.Model().canvasHeight == 720,"the Join Game page validates");
	CheckSessionActions(document);
	const std::pair<const char*,const char*> menu[] = {{"back","joinBack"},{"joinPick","joinPick"},{"joinServer","joinServer"},
		{"joinSpectate","joinSpectate"},{"joinClearSort","joinClearSort"},{"serversNewer","joinNewer"},{"serversOlder","joinOlder"},{"joinRefresh","joinRefresh"},
		{"joinNewList","joinNewList"},{"joinSourceInternet","joinSourceInternet"},{"joinSourceLan","joinSourceLan"},
		{"joinSortFavorite","joinSortFavorite"},{"joinSortLocked","joinSortLocked"},{"joinSortName","joinSortName"},
		{"joinSortPing","joinSortPing"},{"joinSortPlayers","joinSortPlayers"},{"joinSortGameType","joinSortGameType"},
		{"joinSortMap","joinSortMap"},{"joinSortNext","joinSortNext"},{"joinFilterFull","joinFilterFull"},
		{"joinFilterEmpty","joinFilterEmpty"},{"joinFilterPassword","joinFilterPassword"},
		{"joinGameTypePrevious","joinGameTypePrevious"},{"joinGameTypeNext","joinGameTypeNext"},{"joinClearFilters","joinClearFilters"},
		{"noticeAction0","noticeAction0"},{"noticeAction1","noticeAction1"},{"noticeAction2","noticeAction2"}};
	for (const auto& [action, command] : menu) {
		const auto found = document.Model().actions.find(action);
		Check(found != document.Model().actions.end() && found->second.operation == "session.menu" &&
			std::get<std::string>(found->second.arguments.at("command").literal) == command,"the page's verbs reach the session");
	}
	Runtime runtime(host);
	Check(runtime.Initialize() && runtime.LoadDocument(source,"guis/menu/joingame.q4ui",diagnostics),"the page loads into a runtime");
	Viewport viewport; viewport.canvasHeight = 720; viewport.width = 1280; viewport.height = 720;
	std::string error; Runtime::EventEffects effects;
	const auto value = [&](const std::string& node, const char* property, size_t index = 0) {
		const auto found = runtime.PresentedValue(node,property); return found ? static_cast<float>(found->data[index]) : -999.f;
	};
	const auto text = [&](const std::string& node, const char* property) {
		const auto found = runtime.PresentedValue(node,property); return found ? found->text : std::string("?");
	};
	const auto bounds = [&](const std::string& id) { Bounds b; Check(runtime.GetBounds(id,b),"a Join Game node is laid out"); return b; };
	Check(runtime.RunEvent("onActivate",0.5,effects,error),"the page arrives");
	runtime.Frame(viewport,0.575);
	Check(value("join-panel","opacity") > .3f && value("join-panel","opacity") < .7f && runtime.FocusedControl() == "join_newlist",
		"its content fades in over 150 ms and GET NEW LIST takes the focus");
	runtime.Frame(viewport,0.7);
	// The 4:3 canvas at 1280x720: x = 160 dp, 1 u = 1.5 dp.
	const float x0 = 160, u = 1.5f;
	const auto list = bounds("servers-list"), name = bounds("heading-name"), join = bounds("join_server"), status = bounds("join-status");
	Check(Near(list.x,x0+23*u,1) && Near(list.y,83*u,1) && Near(list.height,180*u,1),
		"the server list stands where the stock list does, ten rows high over the filter row");
	Check(Near(name.x,x0+(23+68)*u,1) && Near(name.y,70*u,1) && text("heading-name-label","text") == "#str_200036",
		"SERVER NAME heads its column over the list, where the stock PunkBuster column stood");
	// MOD and its spinner at the stock places; the favorite toggle under JOIN
	// SERVER, ending where it ends; the levelshot frame between the list's
	// actions and JOIN SERVER.
	const auto mod = bounds("join-mod"), previous = bounds("join_mod_previous"), next = bounds("join_mod_next"), favorite = bounds("join_favorite");
	Check(Near(mod.x,x0+27*u,1) && Near(mod.y,46*u,1) && Near(previous.x,x0+88*u,1) && Near(next.x,x0+266*u,1) &&
		text("join-mod","text") == "#str_223005","MOD stands with its spinner where the stock filter did");
	Check(Near(favorite.x+favorite.width,x0+615*u,1) && Near(bounds("join-shot-back").x,x0+240*u,1) && bounds("join-shot-back").x+bounds("join-shot-back").width < favorite.x && Near(bounds("join-shot-back").y,337*u,1),
		"the favorite toggle ends with JOIN SERVER and clears the levelshot frame");
	Check(Near(join.x,x0+385*u,1) && Near(join.y,330*u,1) && Near(status.x,x0+41*u,1) && Near(status.y,291*u,1),
		"JOIN SERVER and the status line stand where the stock page's do");
	// SPECTATE between JOIN SERVER and the favorite toggle, ending where they
	// end (section 13.7's list pattern: Join, Spectate and Favorite).
	const auto spectate = bounds("join_spectate");
	Check(Near(spectate.x+spectate.width,x0+615*u,1) && spectate.y >= join.y+join.height-1 && spectate.y+spectate.height <= favorite.y+1 &&
		text("join_spectate-label","text") == "#str_200195","SPECTATE stands between JOIN SERVER and the favorite toggle");
	// The list's four actions down the leading column on a 26 u pitch, each as
	// wide as the longest label of any (FIND PLAYER/CLAN's Italian).
	const auto refresh = bounds("join_refresh"), joinIp = bounds("join_ip"), find = bounds("join_find");
	Check(Near(refresh.x,x0+19*u,1) && Near(joinIp.x,x0+19*u,1) && Near(find.y-joinIp.y,26*u,1) && Near(joinIp.y-refresh.y,52*u,1) &&
		Near(find.width,refresh.width,.5f) && find.width >= 208*u - .5f && find.x+find.width < bounds("join-shot-back").x &&
		text("join_ip-label","text") == "#str_200025" && text("join_find-label","text") == "#str_200182",
		"JOIN IP ADDRESS and FIND PLAYER/CLAN follow GET NEW LIST, clear of the levelshot");
	// The scan's progress under the list, where the stock load bar is: the
	// tube across the list's width, between CLEAR SORTING and REFRESH LIST,
	// empty until the session publishes a fraction, its notice over it.
	const auto track = bounds("join-load-track"), loadNotice = bounds("join-load-notice");
	Check(Near(track.x,x0+21*u,1) && Near(track.y,313*u,1) && Near(track.width,593*u,1) && Near(track.height,14*u,1) &&
		track.y > bounds("join_clearsort").y+bounds("join_clearsort").height-1 && track.y+track.height < refresh.y+refresh.height &&
		Near(loadNotice.x,x0+41*u,1) && Near(loadNotice.y,311*u,1) && Near(value("join-load-fill","width"),0,.5f),
		"the scan's progress bar and its notice stand where the stock load bar does");
	const auto internet = bounds("join_internet"), lan = bounds("join_lan");
	Check(Near(lan.x+lan.width,x0+613*u,1) && internet.x+internet.width < lan.x && Near(internet.y,46*u,1),
		"the source tags end where the stock LAN tag did, INTERNET before LAN");
	Check(text("crumb","text") == "#str_200002" && text("title","text") == "#str_200187" && Near(bounds("title").x,x0+136*u,1),
		"MULTIPLAYER is the crumb and JOIN GAME titles the sub-page at the step's foot");
	// The filter row under the list (section 7's filter tags): the game type's
	// spinner from the list's leading edge, ALL GAME TYPES while it shows every
	// type, and NOT FULL, NOT EMPTY and NO PASSWORD ending where the source
	// tags do, above CLEAR SORTING, unset without their lit plates and crosses.
	{
		const auto typePrevious = bounds("join_gametype_previous"), typeValue = bounds("join-gametype-value"),
			typeNext = bounds("join_gametype_next"), full = bounds("join_filter_full"), empty = bounds("join_filter_empty"),
			password = bounds("join_filter_password"), clearSort = bounds("join_clearsort");
		Check(Near(typePrevious.x,x0+23*u,1) && Near(typePrevious.y,265*u,1) && typeValue.x > typePrevious.x+typePrevious.width &&
			typeNext.x >= typeValue.x+typeValue.width && typeNext.x+typeNext.width < full.x && full.x+full.width < empty.x &&
			empty.x+empty.width < password.x && Near(password.x+password.width,x0+613*u,1) && Near(full.y,265*u,1) &&
			full.y > list.y+list.height && full.y+full.height <= clearSort.y+.5f && full.y+full.height < status.y,
			"the filter row stands between the list and the status line");
		Check(text("join-gametype-value","text") == "#str_231299" && text("join_filter_full-label","text") == "#str_231296" &&
			text("join_filter_empty-label","text") == "#str_231297" && text("join_filter_password-label","text") == "#str_231298" &&
			text("join_filter_full-lit","display") == "none" && text("join_filter_full-cross","display") == "none",
			"every game type shows, and no tag is set");
		Check(runtime.SetState({{"join.filterFull",true},{"join.filterPassword",true},{"join.gameType",4.0},
			{"join.gameTypeName",std::string("Capture the Flag")}},error,0.701),"the session sets two filters and a game type");
		runtime.Frame(viewport,0.702);
		Check(text("join_filter_full-lit","display") == "block" && text("join_filter_full-cross","display") == "block" &&
			text("join_filter_password-lit","display") == "block" && text("join_filter_empty-lit","display") == "none" &&
			text("join_filter_empty-cross","display") == "none" && text("join-gametype-value","text") == "Capture the Flag",
			"set tags light with their crosses, and the spinner names the game type");
		const auto press = [&](MenuInput input, double at) { runtime.MenuAction(input,true,at); runtime.MenuAction(input,false,at+.01); };
		runtime.TakeActions();
		Check(runtime.FocusControl("join_filter_empty",0.703),"focus NOT EMPTY");
		press(MenuInput::Accept,0.7031);
		auto actions = runtime.TakeActions();
		Check(actions.size() == 1 && actions[0].action == "joinFilterEmpty","Accept asks the session to toggle its filter");
		Check(runtime.FocusControl("join_gametype_next",0.705),"focus the game type's next square");
		press(MenuInput::Accept,0.7051);
		actions = runtime.TakeActions();
		Check(actions.size() == 1 && actions[0].action == "joinGameTypeNext","and the spinner asks for the next game type");
		Check(runtime.SetState({{"join.filterFull",false},{"join.filterPassword",false},{"join.gameType",0.0},{"join.gameTypeName",std::string()}},
			error,0.707) && runtime.FocusControl("join_newlist",0.707),"the filters clear");
		runtime.Frame(viewport,0.708);
		Check(text("join_filter_full-lit","display") == "none" && text("join-gametype-value","text") == "#str_231299","and the row shows them clear");
	}
	// Nothing listed: the empty line, unless a scan runs; nothing to join.
	Check(text("join-empty","display") == "block" && text("join-empty","text") == "#str_231133" && text("server-0","display") == "none" &&
		runtime.GetControlState("join_server") == ControlState::Disabled && runtime.GetControlState("join_favorite") == ControlState::Disabled,
		"an empty list says so, with nothing to join or favorite");
	// The empty state of section 13.13: a heading over the sentence, and the
	// action that fixes it: GET NEW LIST when the scan holds no server.
	const auto emptyHeading = bounds("join-empty-heading"), emptyLine = bounds("join-empty"), emptyNew = bounds("join_emptynew");
	Check(text("join-empty-heading","display") == "block" && text("join-empty-heading","text") == "#str_231537" &&
		text("join_emptynew","display") == "block" && text("join_emptyclear","display") == "none" &&
		emptyHeading.y+emptyHeading.height <= emptyLine.y+1 && emptyLine.y+emptyLine.height <= emptyNew.y+5*u+1 &&
		Near(emptyNew.x,emptyLine.x,1),"none found: NO SERVERS, the sentence and GET NEW LIST under it");
	{
		runtime.TakeActions();
		Check(runtime.FocusControl("join_emptynew",0.741),"focus GET NEW LIST");
		runtime.MenuAction(MenuInput::Accept,true,0.742); runtime.MenuAction(MenuInput::Accept,false,0.743);
		const auto fixed = runtime.TakeActions();
		Check(fixed.size() == 1 && fixed[0].action == "joinNewList","Accept asks for a new list");
	}
	Check(runtime.SetState({{"join.scanning",true},{"join.status",std::string("Scanning - Found: 0")}},error,0.75),"a scan starts");
	runtime.Frame(viewport,0.8);
	Check(text("join-empty","display") == "none" && text("join-status","text") == "Scanning - Found: 0" &&
		text("join-empty-heading","display") == "none" && text("join_emptynew","display") == "none" && text("join_emptyclear","display") == "none",
		"a running scan shows its status, not the empty state");
	Check(runtime.SetState({{"join.progress",0.5},{"join.notice",std::string("Scanning 3/6: Strogg Hive")}},error,0.76),"the scan's progress");
	runtime.Frame(viewport,0.77);
	Check(Near(bounds("join-load-fill").width,593*u/2,1) && text("join-load-notice","text") == "Scanning 3/6: Strogg Hive",
		"the fill follows the scan's fraction under its notice");
	Check(runtime.SetState({{"join.progress",1.5}},error,0.78),"a fraction past the end");
	runtime.Frame(viewport,0.79);
	Check(Near(bounds("join-load-fill").width,593*u,1),"the fill stops at the bar's end");
	// Servers the filters or a search all hide: the empty line says none matches.
	Check(runtime.SetState({{"join.scanning",false},{"join.listed",2.0},{"join.status",std::string("Showing 0 out of 2 servers")}},error,0.81),
		"a filter hides both servers the scan holds");
	runtime.Frame(viewport,0.82);
	Check(text("join-empty","display") == "block" && text("join-empty","text") == "#str_231134" &&
		text("join-empty-heading","text") == "#str_231538" && text("join_emptyclear","display") == "block" &&
		text("join_emptynew","display") == "none","the empty state tells a filtered list apart and offers CLEAR FILTERS");
	{
		runtime.TakeActions();
		Check(runtime.FocusControl("join_emptyclear",0.821),"focus CLEAR FILTERS");
		runtime.MenuAction(MenuInput::Accept,true,0.822); runtime.MenuAction(MenuInput::Accept,false,0.823);
		const auto fixed = runtime.TakeActions();
		Check(fixed.size() == 1 && fixed[0].action == "joinClearFilters","Accept asks the session to clear every filter");
	}
	StateValues servers = {{"join.scanning",false},{"join.count",3.0},{"join.first",0.0},{"join.selected",-1.0},{"join.sort",0.0},
		{"join.ascending",true},{"join.status",std::string("Showing 3 out of 3 servers")},
		{"join.row0.name",std::string("Refinement Arena")},{"join.row0.ping",std::string("12")},{"join.row0.players",std::string("1/8")},
		{"join.row0.type",std::string("Deathmatch")},{"join.row0.map",std::string("Fragging Yard")},{"join.row0.fav",true},
		{"join.row1.name",std::string("Strogg Hive")},{"join.row1.ping",std::string("40")},{"join.row1.lock",true},
		{"join.row2.name",std::string("Lost Fleet")},{"join.row2.ping",std::string("85")},{"join.row1.ded",true},{"join.row2.rep",true},
		{"join.mod",std::string("All")},{"join.listed",3.0}};
	Check(runtime.SetState(servers,error,0.85),"the session lists three servers");
	runtime.Frame(viewport,0.9);
	Check(text("join-empty","display") == "none" && text("server-2","display") == "block" && text("server-3","display") == "none" &&
		text("server-0-name","text") == "Refinement Arena" && text("server-0-players","text") == "1/8" && text("server-0-map","text") == "Fragging Yard" &&
		text("server-1-ping","text") == "40","the servers fill their rows at the stock tab stops");
	Check(text("server-0-name","text-overflow") == "ellipsis" && text("server-2-map","text-overflow") == "ellipsis" &&
		text("server-0-name","overflow") == "hidden","a name or map longer than its column ends in an ellipsis, not mid-letter");
	Value overflow; overflow.type = ValueType::Keyword; overflow.text = "clip";
	const bool clips = ValidProperty("text-overflow",overflow); overflow.text = "fade";
	Check(clips && !ValidProperty("text-overflow",overflow),"text-overflow takes clip or ellipsis only");
	Check(text("server-1-ded","display") == "block" && text("server-0-ded","display") == "none" && text("server-2-rep","display") == "block" &&
		text("server-1-rep","display") == "none" && text("join-mod-value","text") == "All",
		"a dedicated server and a repeater show their symbols, and MOD its filter");
	Check(Near(bounds("server-1-ded").x,bounds("server-1").x+47*u+(21*u-16)/2,1) && Near(bounds("server-2-rep").x,bounds("server-2").x+319*u+(21*u-16)/2,1) &&
		Near(bounds("server-1-ded").width,16,.5f),"the symbols' 16 dp tiles sit centered in their stock columns");
	Check(text("server-0-fav","display") == "block" && text("server-1-fav","display") == "none" && text("server-1-lock","display") == "block" &&
		text("server-0-lock","display") == "none","a favorite shows its star and a password its lock");
	// PING's column starts at the stock 279 u with its network quality bars;
	// the ping's text follows them at 292 u.
	Check(Near(bounds("server-0-bar-1").x,bounds("server-0").x+280*u,1) && Near(bounds("server-0-ping").x,bounds("server-0").x+292*u,1) &&
		Near(bounds("server-1").y,bounds("server-0").y+18*u,1),"the rows keep the stock 18 u pitch and their columns");
	Check(text("servers-newer","display") == "none" && text("servers-older","display") == "none","one page holds them all");
	// The details card: each row names its row; focusing one makes it the
	// subject (join.detail); the card shows once the session's details describe
	// that row (join.detail.row), 6 dp under the row at its leading edge, with
	// the address, players and settings under the stock headings; describing
	// another row hides it, and a control without a detail clears the subject.
	Check(document.Model().detail && document.Model().detail->card == "join-detail" && document.Model().detail->state == "join.detail" &&
		document.Model().FindNode("server-5")->control->detail == 5.0,"the rows name their rows as the card's subject");
	Check(text("join-detail","display") == "none","no subject, no card");
	Check(runtime.FocusControl("server-1",0.91),"focus the second row");
	runtime.Frame(viewport,0.911);
	StateValue subject;
	Check(runtime.GetStateValue("join.detail",subject) && std::get<double>(subject) == 1,"the focused row is the subject");
	Check(text("join-detail","display") == "none","the card waits for its server's details");
	Check(runtime.SetState({{"join.detail.row",1.0},{"join.detail.address",std::string("10.0.0.9:28004")},
		{"join.detail.name",std::string("Refinement Arena - Twenty-four Hour Instagib Rail Madness, Admins Online")},
		{"join.detail.player0",std::string("Grunt")},{"join.detail.setting0",std::string("Map: Fragging Yard")}},error,0.92),"the session describes it");
	runtime.Frame(viewport,0.921);
	const auto detailRow = bounds("server-1"), detailCard = bounds("join-detail");
	Check(text("join-detail","display") == "block" && Near(detailCard.x,detailRow.x,1) && Near(detailCard.y,detailRow.y+detailRow.height+6,1) &&
		Near(detailCard.width,270*u,1) && Near(detailCard.height,216*u,1),"the card sits 6 dp under the focused row at its leading edge");
	// Its name in full, over the stock address: a long one wraps to a second
	// line inside the card rather than ending in an ellipsis.
	const auto detailName = bounds("join-detail-name"), detailAddress = bounds("join-detail-address");
	Check(text("join-detail-name","text") == "Refinement Arena - Twenty-four Hour Instagib Rail Madness, Admins Online" &&
		text("join-detail-name","white-space") == "normal" && Near(detailName.y,detailCard.y+5*u,1) &&
		detailName.y+detailName.height <= detailAddress.y+1 && detailAddress.y+detailAddress.height <= detailCard.y+53*u+1,
		"the card names its server in full over the address, above the rule");
	Check(text("join-detail-address","text") == "10.0.0.9:28004" && text("join-detail-player0","text") == "Grunt" &&
		text("join-detail-setting0","text") == "Map: Fragging Yard" && text("join-detail-players","text") == "#str_200038" &&
		text("join-detail-settings","text") == "#str_200928","it names the server's address, players and settings under the stock headings");
	// The View button hides the card and shows it again (section 13.5).
	Runtime::EventEffects viewEffects;
	Check(runtime.RunEvent("onViewButton",0.922,viewEffects,error),"View hides the details");
	runtime.Frame(viewport,0.923);
	Check(text("join-detail","display") == "none","the card is hidden while its row stays the subject");
	Check(runtime.RunEvent("onViewButton",0.924,viewEffects,error),"View shows them again");
	runtime.Frame(viewport,0.925);
	Check(text("join-detail","display") == "block","the card is back");
	// Its top trailing corner names the View button for a controller, beside
	// the address and above the rule; no key presses it, so the keyboard
	// shows none.
	Check(text("join-detail-view-xbox","display") == "none" && text("join-detail-view-generic","display") == "none",
		"the keyboard shows no View button");
	Check(runtime.SetState({{"prompt.glyphs",1.0}},error,0.926),"an Xbox controller");
	runtime.Frame(viewport,0.927);
	const auto viewGlyph = bounds("join-detail-view-xbox"), viewCard = bounds("join-detail"), viewAddress = bounds("join-detail-address");
	Check(text("join-detail-view-xbox","display") == "block" && text("join-detail-view-playstation","display") == "none" &&
		Near(viewGlyph.x+viewGlyph.width,viewCard.x+viewCard.width-6*u,1) && Near(viewGlyph.y,viewCard.y+4*u,1) &&
		Near(viewGlyph.width,12*u,1) && viewGlyph.y+viewGlyph.height <= viewCard.y+23*u+1 &&
		viewAddress.x+viewAddress.width <= viewGlyph.x+1,"the card's corner shows the Xbox View button");
	Check(runtime.SetState({{"prompt.glyphs",4.0}},error,0.928),"a Steam Deck");
	runtime.Frame(viewport,0.929);
	Check(text("join-detail-view-steamdeck","display") == "block" && text("join-detail-view-xbox","display") == "none",
		"each family draws its own");
	// SPECTATE and the favorite toggle name their stick presses 8 dp past
	// their ends, centred on their bands, for a controller only.
	{
		const auto plate = bounds("join_spectate"), toggle = bounds("join_favorite");
		const auto left = bounds("join_spectate-stick-steamdeck"), right = bounds("join_favorite-stick-steamdeck");
		Check(text("join_spectate-stick-steamdeck","display") == "block" && text("join_favorite-stick-steamdeck","display") == "block" &&
			text("join_spectate-stick-xbox","display") == "none" && Near(left.x,plate.x+plate.width+8*u/1.5f,1) &&
			Near(right.x,toggle.x+toggle.width+8*u/1.5f,1) && Near(left.y+left.height/2,plate.y+plate.height/2,1) &&
			Near(left.width,22*u/1.5f,1) && right.x+right.width <= viewport.width,"each stick press stands past its plate");
	}
	Check(runtime.SetState({{"prompt.glyphs",2.0}},error,0.9291),"a PlayStation controller");
	runtime.Frame(viewport,0.9292);
	Check(text("join_spectate-stick-playstation","display") == "block" && text("join_favorite-stick-playstation","display") == "block" &&
		text("join_spectate-stick-steamdeck","display") == "none","its family's L3 and R3");
	Check(runtime.SetState({{"prompt.glyphs",0.0}},error,0.9295),"the keyboard again");
	runtime.Frame(viewport,0.9297);
	Check(text("join-detail-view-steamdeck","display") == "none","and the keyboard none");
	Check(text("join_spectate-stick-playstation","display") == "none" && text("join_favorite-stick-xbox","display") == "none",
		"no stick press for the keyboard");
	Check(runtime.SetState({{"join.detail.row",2.0}},error,0.93),"a stale description");
	runtime.Frame(viewport,0.931);
	Check(text("join-detail","display") == "none","describing another row hides it");
	Check(runtime.SetState({{"join.detail.row",-1.0}},error,0.94) && runtime.FocusControl("join_newlist",0.941),"the focus leaves the list");
	runtime.Frame(viewport,0.942);
	Check(runtime.GetStateValue("join.detail",subject) && std::get<double>(subject) == -1,"a control without a detail clears the subject");
	// The sort: ping ascending marks PING's heading with its up chevron.
	Check(text("heading-ping-up","display") == "block" && text("heading-ping-down","display") == "none" &&
		text("heading-name-up","display") == "none","the sort's column shows its direction");
	Check(runtime.SetState({{"join.sort",1.0},{"join.ascending",false}},error,0.95),"sorted by name, descending");
	runtime.Frame(viewport,1.0);
	Check(text("heading-name-down","display") == "block" && text("heading-name-up","display") == "none" &&
		text("heading-ping-up","display") == "none","the chevron follows the sort");
	Check(runtime.SetState({{"join.sort",11.0}},error,1.02),"CLEAR SORTING");
	runtime.Frame(viewport,1.03);
	Check(text("heading-name-down","display") == "none" && text("heading-ping-up","display") == "none","an unsorted list marks no column");
	// The source: the lit tag follows the list's.
	Check(text("join_internet-lit","display") == "block" && text("join_lan-lit","display") == "none","the Internet is the source");
	Check(runtime.SetState({{"join.lan",true}},error,1.04),"the LAN");
	runtime.Frame(viewport,1.05);
	Check(text("join_internet-lit","display") == "none" && text("join_lan-lit","display") == "block","the LAN tag lights");
	// Network quality (section 13.13) beside the ping: two bars lit in the
	// warning color, the other two faint; four in the success color; one in
	// the error color. The ping's text follows the bars in its column.
	const auto barIs = [&](const std::string& node, float r, float g, float b, float alpha) {
		const auto found = runtime.PresentedValue(node,"background-color");
		return found && Near(static_cast<float>(found->data[0]),r,.01f) && Near(static_cast<float>(found->data[1]),g,.01f) &&
			Near(static_cast<float>(found->data[2]),b,.01f) && Near(static_cast<float>(found->data[3]),alpha,.01f);
	};
	Check(runtime.SetState({{"join.row1.quality",2.0},{"join.row2.quality",4.0},{"join.row3.quality",1.0}},error,1.06),"three rows' quality");
	runtime.Frame(viewport,1.07);
	Check(barIs("server-1-bar-1",0xE3/255.f,0xAD/255.f,0x36/255.f,.9f) && barIs("server-1-bar-2",0xE3/255.f,0xAD/255.f,0x36/255.f,.9f) &&
		barIs("server-1-bar-3",1,1,1,.18f) && barIs("server-1-bar-4",1,1,1,.18f),"two bars lit in the warning color");
	Check(barIs("server-2-bar-4",0xB5/255.f,0xC7/255.f,0x84/255.f,.9f) && barIs("server-3-bar-1",0xE4/255.f,0x6D/255.f,0x56/255.f,.9f) &&
		barIs("server-3-bar-2",1,1,1,.18f),"four in the success color, one in the error color");
	{
		const auto first = bounds("server-1-bar-1"), last = bounds("server-1-bar-4"), ping = bounds("server-1-ping");
		Check(last.height > first.height && Near(last.y+last.height,first.y+first.height,.5f) && ping.x > last.x+last.width &&
			Near(first.x,bounds("server-1").x+280*u,1),"the bars rise from the column's start and the ping follows them");
	}
	// A row's first Accept asks for its server; once chosen it is lit, JOIN
	// SERVER applies and Accept on it moves on to JOIN SERVER.
	Check(runtime.RunEvent("serverRow1",1.1,effects,error) && effects.actions.size() == 1 && effects.actions[0].action == "joinPick",
		"a row asks for its server");
	Check(runtime.SetState({{"join.selected",1.0}},error,1.15),"the session chooses it");
	runtime.Frame(viewport,1.2);
	Check(text("server-1-selected","display") == "block" && text("server-0-selected","display") == "none" &&
		runtime.GetControlState("join_server") != ControlState::Disabled && runtime.GetControlState("join_favorite") != ControlState::Disabled &&
		text("join_favorite-label","text") == "#str_200292","the chosen server is lit, JOIN SERVER applies and ADD TO FAVORITES offers");
	Check(runtime.SetState({{"join.favorite",true},{"join.shot",std::string("gfx/guis/loadscreens/generic")}},error,1.21),
		"the session publishes its favorite state and levelshot");
	runtime.Frame(viewport,1.22);
	Check(text("join_favorite-label","text") == "#str_200293" && text("join-shot","image") == "gfx/guis/loadscreens/generic",
		"a favorite offers REMOVE FAVORITE, and its levelshot fills the frame");
	Check(runtime.RunEvent("serverRow1",1.25,effects,error) && effects.actions.empty() && runtime.FocusedControl() == "join_server",
		"Accept on the chosen server moves on to JOIN SERVER");
	Check(runtime.AcceptPrompt("#str_231042",1.26) == "#str_231033","and the prompt bar names Accept JOIN there (section 13.4)");
	// More servers than a page: the pagers.
	Check(runtime.SetState({{"join.count",70.0}},error,1.3),"seventy servers");
	runtime.Frame(viewport,1.35);
	Check(text("servers-older","display") == "block" && text("servers-newer","display") == "none","the list pages on to more servers");
	Check(runtime.SetState({{"join.first",64.0}},error,1.4),"the next page");
	runtime.Frame(viewport,1.45);
	Check(text("servers-newer","display") == "block" && text("servers-older","display") == "none" && text("server-5","display") == "block" &&
		text("server-6","display") == "none","the last page pages back and holds the rest");
	// A player search shows its tag after the status line; Accept ends it.
	Check(text("join_search","display") == "none","no search, no tag");
	Check(runtime.SetState({{"join.search",std::string("Grunt")}},error,1.46),"the session publishes a search");
	runtime.Frame(viewport,1.47);
	Check(text("join_search","display") == "block" && text("join-search-name","text") == "Grunt" &&
		bounds("join_search").x > bounds("join-status").x+bounds("join-status").width && Near(bounds("join_search").y,290*u,1.5f) &&
		bounds("join_search").x+bounds("join_search").width < bounds("join_clearsort").x,
		"the search's tag stands between the status line and CLEAR SORTING");
	const auto searchAction = document.Model().actions.find("joinSearchClear");
	Check(searchAction != document.Model().actions.end() && std::get<std::string>(searchAction->second.arguments.at("command").literal) == "joinSearchClear",
		"its Accept ends the search");
	// The controller's west refreshes the list and north sorts it by the next
	// column (section 13.5).
	Check(runtime.RunEvent("onSecondary",1.472,effects,error) && effects.actions.size() == 1 && effects.actions[0].action == "joinRefresh",
		"west asks for the list again");
	Check(runtime.RunEvent("onTertiary",1.474,effects,error) && effects.actions.size() == 1 && effects.actions[0].action == "joinSortNext",
		"north asks for the next column's sort");
	// A press of the left stick spectates the chosen server and of the right
	// one toggles its favorite; with no server chosen neither asks anything.
	{
		StateValue before; Check(runtime.GetStateValue("join.selected",before),"the chosen server reads back");
		Check(runtime.SetState({{"join.selected",-1.0}},error,1.4741) && runtime.RunEvent("onLeftStick",1.4742,effects,error) &&
			effects.actions.empty() && runtime.RunEvent("onRightStick",1.4743,effects,error) && effects.actions.empty(),
			"no server, no spectating and no favorite");
		Check(runtime.SetState({{"join.selected",0.0}},error,1.4744) && runtime.RunEvent("onLeftStick",1.4745,effects,error) &&
			effects.actions.size() == 1 && effects.actions[0].action == "joinSpectate","the left stick spectates the chosen server");
		Check(runtime.RunEvent("onRightStick",1.4746,effects,error) && effects.actions.size() == 1 && effects.actions[0].action == "joinFavorite",
			"the right stick toggles its favorite");
		Check(runtime.SetState({{"join.selected",before}},error,1.4747),"the chosen server restores");
	}
	// JOIN IP ADDRESS: the dialog opens on the address field; CONNECT applies
	// once there is an address and asks the session; a refusal shows its
	// reason; this machine's address shows; Back returns to the plate.
	Check(runtime.FocusControl("join_ip",1.48) && runtime.RunEvent("joinIpOpen",1.5,effects,error),"JOIN IP ADDRESS opens its dialog");
	runtime.Frame(viewport,1.75);
	Check(runtime.RunEvent("onSecondary",1.76,effects,error) && effects.actions.empty() &&
		runtime.RunEvent("onTertiary",1.77,effects,error) && effects.actions.empty(),"neither refreshes nor sorts under the dialog");
	Check(runtime.RunEvent("onLeftStick",1.771,effects,error) && effects.actions.empty() &&
		runtime.RunEvent("onRightStick",1.772,effects,error) && effects.actions.empty(),"nor spectates or marks a favorite");
	Check(text("joinIpModal","display") == "block" && runtime.FocusedControl() == "join-address" &&
		runtime.GetControlState("joinIpModal_yes") == ControlState::Disabled && text("joinIpModal_yes-label","text") == "#str_200171" &&
		text("join-yourip","text") == "#str_200180","the dialog opens on the address field, with nothing to connect to yet");
	const auto* addressNode = document.Model().FindNode("join-address");
	Check(addressNode && addressNode->control && std::holds_alternative<NumberSpec>(addressNode->control->widget) &&
		std::get<NumberSpec>(addressNode->control->widget).maxBytes == 99 && !std::get<NumberSpec>(addressNode->control->widget).masked,
		"the address takes the stock field's 99 bytes");
	Check(runtime.SetState({{"join.address",std::string("192.0.2.9:28004")},{"join.clientip",std::string("192.0.2.7")},
		{"join.addressError",std::string("Cannot join 192.0.2.9:28004")}},error,1.8),"an address, this machine's and a refusal");
	runtime.Frame(viewport,1.85);
	Check(runtime.GetControlState("joinIpModal_yes") != ControlState::Disabled && text("join-yourip-value","text") == "192.0.2.7" &&
		text("join-address-error","display") == "block" && text("join-address-error","text") == "Cannot join 192.0.2.9:28004",
		"CONNECT applies, this machine's address and the refusal's reason show");
	Check(runtime.RunEvent("joinIpConnect",1.9,effects,error) && effects.actions.size() == 1 && effects.actions[0].action == "joinConnect",
		"CONNECT asks the session to connect");
	Check(runtime.RunEvent("joinIpModalHide",2.0,effects,error),"the dialog closes");
	runtime.Frame(viewport,2.4);
	Check(text("joinIpModal","display") == "none" && runtime.FocusedControl() == "join_ip","closing it returns to JOIN IP ADDRESS");
	// FIND PLAYER/CLAN: the name field (31 bytes, a player name's, possibly
	// empty to end a search); SEARCH asks the session and closes the dialog.
	Check(runtime.FocusControl("join_find",2.45) && runtime.RunEvent("findOpen",2.5,effects,error),"FIND PLAYER/CLAN opens its dialog");
	runtime.Frame(viewport,2.75);
	Check(text("findModal","display") == "block" && runtime.FocusedControl() == "join-find" && text("join-find-label","text") == "#str_200254",
		"the dialog opens on SEARCH FOR:");
	Check(runtime.RunEvent("onSecondary",2.76,effects,error) && effects.actions.empty() &&
		runtime.RunEvent("onTertiary",2.77,effects,error) && effects.actions.empty(),"nor under this one");
	const auto* findNode = document.Model().FindNode("join-find");
	Check(findNode && findNode->control && std::holds_alternative<NumberSpec>(findNode->control->widget) &&
		std::get<NumberSpec>(findNode->control->widget).maxBytes == 31 && std::get<NumberSpec>(findNode->control->widget).allowEmpty,
		"a name takes 31 bytes and may be empty");
	Check(runtime.RunEvent("findSearch",2.8,effects,error) && effects.actions.size() == 1 && effects.actions[0].action == "joinSearch",
		"SEARCH asks the session to search");
	runtime.Frame(viewport,3.2);
	Check(text("findModal","display") == "none" && runtime.FocusedControl() == "join_find","and the dialog closes onto FIND PLAYER/CLAN");
	Check(runtime.RunEvent("onBack",3.3,effects,error) && effects.actions.size() == 1 && effects.actions[0].action == "back","Back asks to climb out");
	// Loading rows (section 13.13): a scan that has listed nothing yet shows
	// the visible rows at rest without labels, in the list's place, and no
	// empty state; the first servers replace them with row.reveal, each row
	// fading in over 120 ms, 15 ms after the one above, capped at 90 ms.
	Check(runtime.SetState({{"join.count",0.0},{"join.listed",0.0},{"join.scanning",true}},error,3.4),"a new scan finds nothing yet");
	runtime.Frame(viewport,3.41);
	const auto loadingRow0 = bounds("join-loading-0"), loadingRow9 = bounds("join-loading-9"), listBox = bounds("servers-list");
	Check(text("join-loading","display") == "block" && text("join-empty","display") == "none" && text("join-empty-heading","display") == "none" &&
		Near(loadingRow0.y,listBox.y,1) && Near(loadingRow0.height,18*u,1) && Near(loadingRow9.y,listBox.y+9*18*u,1) &&
		!document.Model().FindNode("join-loading-10"),"ten rows rest in the list's place, without labels");
	Check(runtime.SetState({{"join.count",3.0},{"join.listed",3.0},{"join.scanning",false}},error,3.5) &&
		runtime.RunEvent("joinRowsReveal",3.5,effects,error),"the scan's first servers arrive");
	runtime.Frame(viewport,3.501);
	Check(text("join-loading","display") == "none" && value("server-0","opacity") < .1f && value("server-2","opacity") < .1f,
		"they replace the loading rows from nothing");
	runtime.Frame(viewport,3.56);
	const float revealTop = value("server-0","opacity"), revealThird = value("server-2","opacity");
	Check(revealTop > .3f && revealTop < .7f && revealThird > .05f && revealThird < revealTop,"each row a little after the one above");
	runtime.Frame(viewport,3.75);
	Check(Near(value("server-0","opacity"),1,.001f) && Near(value("server-1","opacity"),1,.001f) && Near(value("server-2","opacity"),1,.001f),
		"all in by 210 ms");
	// Notices (section 13.13): header-band plates 400 dp wide in the top
	// band's trailing section, 24 dp from the view's edge, three stacked
	// 6 dp apart, each a status symbol, its text and an optional action.
	Check(text("notice-0","display") == "none" && text("notice-2","display") == "none" && !document.Model().FindNode("notice-3"),
		"no notice, nothing shows");
	Check(runtime.SetState({{"notice.0.shown",true},{"notice.0.text",std::string("#str_231540")},{"notice.0.action",std::string("#str_42904")},
		{"notice.0.kind",1.0},{"notice.1.shown",true},{"notice.1.text",std::string("#str_231539")},{"notice.1.kind",1.0}},error,3.8),
		"the session publishes two notices");
	runtime.Frame(viewport,3.81);
	const auto notice0 = bounds("notice-0"), notice1 = bounds("notice-1"), noticeAction = bounds("notice-0-action"),
		noticeText = bounds("notice-0-text");
	Check(text("notice-0","display") == "block" && text("notice-1","display") == "block" && text("notice-2","display") == "none" &&
		Near(notice0.x+notice0.width,1280-24,1) && Near(notice0.y,14,1) && Near(notice0.width,400,1) && Near(notice0.height,30,1) &&
		Near(notice1.y,notice0.y+36,1),"two plates stack from the top band's trailing section");
	// The session's layer: the favorite's star and a lost controller's symbol.
	Check(document.Model().FindNode("notice-0-symbol-2") && text("notice-0-symbol-2","display") == "none" &&
		text("notice-0-symbol-1","display") == "block" && text("notice-0-text","text") == "#str_231540" &&
		text("notice-0-action","display") == "block" && text("notice-0-action-label","text") == "#str_42904" &&
		text("notice-1-action","display") == "none" && noticeText.x+noticeText.width <= noticeAction.x+1 &&
		noticeAction.x+noticeAction.width <= notice0.x+notice0.width,"each its star and text, the removal its Undo inside the plate");
	runtime.TakeActions();
	Check(runtime.FocusControl("notice-0-action",3.82),"focus Undo");
	runtime.MenuAction(MenuInput::Accept,true,3.83); runtime.MenuAction(MenuInput::Accept,false,3.84);
	{
		const auto undone = runtime.TakeActions();
		Check(undone.size() == 1 && undone[0].action == "noticeAction0","Accept asks the session to undo");
	}
	// notice.enter: 160 ms, 12 dp in from the trailing edge with opacity.
	Check(runtime.RunEvent("noticeEnter0",3.9,effects,error),"a notice enters");
	runtime.Frame(viewport,3.901);
	const auto entering = runtime.PresentedValue("notice-0","transform");
	Check(value("notice-0","opacity") < .1f && entering && entering->data[0] > 11,"from nothing, 12 dp toward the trailing edge");
	runtime.Frame(viewport,4.07);
	const auto entered = runtime.PresentedValue("notice-0","transform");
	Check(Near(value("notice-0","opacity"),1,.001f) && entered && Near(static_cast<float>(entered->data[0]),0,.01f),"in place at 160 ms");
	// notice.leave: 200 ms opacity; the rest returns the slot.
	Check(runtime.RunEvent("noticeLeave0",4.1,effects,error),"it leaves");
	runtime.Frame(viewport,4.2);
	const float leavingOpacity = value("notice-0","opacity");
	runtime.Frame(viewport,4.31);
	Check(leavingOpacity > .3f && leavingOpacity < .7f && value("notice-0","opacity") < .01f,"fading out over 200 ms");
	Check(runtime.RunEvent("noticeRest0",4.32,effects,error),"the slot rests");
	runtime.Frame(viewport,4.33);
	Check(Near(value("notice-0","opacity"),1,.001f),"for the notice that closes up into it");
}

// The Create Server sub-page (FLOW-050): the stock Create Server page's
// content in the sub-page state under MULTIPLAYER's crumb. Every setting's
// control asks the session with its own verb; the game type picks the limit
// row and the slot under Allow Voting and locks Auto Balance and Team Damage
// outside team modes; the map list shows the maps the session publishes, the
// chosen one lit; ADVANCED OPTIONS opens the stock popup's options as a dialog.
// PLAYER BAN LIST: VIEW LOCAL SERVER BAN LIST asks for the list and opens it
// over ADVANCED OPTIONS, nested in its scope; a row's first Accept chooses
// its ban, REMOVE BAN (enabled once one is chosen) removes it, and closing the
// list returns the focus to the row that opened it.
static void CheckCreateServerBanList(ScreenHost& host, const char* path) {
	const auto source = Read(path);
	Runtime runtime(host); std::vector<Diagnostic> diagnostics;
	Check(runtime.Initialize() && runtime.LoadDocument(source,"guis/menu/createserver.q4ui",diagnostics),"the page loads for its ban list");
	Viewport viewport; viewport.canvasHeight = 720; viewport.width = 1280; viewport.height = 720;
	std::string error; Runtime::EventEffects effects;
	const auto text = [&](const std::string& node, const char* property) {
		const auto found = runtime.PresentedValue(node,property); return found ? found->text : std::string("?");
	};
	const auto bounds = [&](const std::string& id) { Bounds b; Check(runtime.GetBounds(id,b),"a ban list node is laid out"); return b; };
	Check(runtime.RunEvent("onActivate",0.5,effects,error),"the page arrives");
	runtime.Frame(viewport,0.7);
	Check(runtime.RunEvent("advancedModalShow",0.8,effects,error),"ADVANCED OPTIONS opens");
	runtime.Frame(viewport,1.05);
	// VIEW LOCAL SERVER BAN LIST is the dialog's last row, after the map cycle.
	Check(Near(bounds("create-banlist").y,bounds("create-mapcycle").y+36,1) && runtime.FocusControl("create-banlist",1.1),
		"VIEW LOCAL SERVER BAN LIST follows the map cycle on the row pitch");
	Check(runtime.RunEvent("banListOpen",1.2,effects,error) && effects.actions.size() == 1 && effects.actions[0].action == "createBanList",
		"the row asks the session for the list");
	runtime.Frame(viewport,1.45);
	Check(text("banModal","display") == "block" && text("banModal-contents","display") == "block" && runtime.FocusedControl() == "banModal_no",
		"the list opens over the dialog on CLOSE");
	Check(!runtime.FocusControl("create-usepass",1.46),"the dialog beneath takes no focus while the list stands over it");
	Check(text("ban-empty","display") == "block" && text("ban-row-0","display") == "none" &&
		runtime.GetControlState("banModal_yes") == ControlState::Disabled,"an empty list says so, with nothing to remove");
	Check(runtime.SetState({{"create.ban_count",2.0},{"create.ban0.name",std::string("Griefer")},{"create.ban0.guid",std::string("ABCDEFGHIJK")},
		{"create.ban1.name",std::string("Camper")},{"create.ban1.guid",std::string("BCDEFGHIJKL")}},error,1.5),"the session publishes two bans");
	runtime.Frame(viewport,1.55);
	Check(text("ban-empty","display") == "none" && text("ban-row-1","display") == "block" && text("ban-row-2","display") == "none" &&
		text("ban-row-1-name","text") == "Camper" && text("ban-row-1-guid","text") == "BCDEFGHIJKL","the bans fill their rows");
	Check(runtime.RunEvent("banRow1",1.6,effects,error) && effects.actions.size() == 1 && effects.actions[0].action == "createBanPick",
		"a row's first Accept asks for its ban");
	Check(runtime.SetState({{"create.ban_selected",1.0}},error,1.65),"the session chooses it");
	runtime.Frame(viewport,1.7);
	Check(text("ban-row-1-selected","display") == "block" && runtime.GetControlState("banModal_yes") != ControlState::Disabled,
		"the chosen ban is lit and REMOVE BAN applies");
	Check(runtime.RunEvent("banRow1",1.75,effects,error) && effects.actions.empty() && runtime.FocusedControl() == "banModal_yes",
		"Accept on the chosen ban moves on to REMOVE BAN");
	Check(runtime.RunEvent("banRemoveConfirmed",1.8,effects,error) && effects.actions.size() == 1 && effects.actions[0].action == "createBanRemove",
		"REMOVE BAN asks the session to remove it, the list staying");
	runtime.Frame(viewport,1.85);
	Check(text("banModal","display") == "block","REMOVE BAN keeps the list open");
	Check(runtime.RunEvent("banModalHide",1.9,effects,error),"CLOSE");
	runtime.Frame(viewport,2.3);
	Check(text("banModal","display") == "none" && text("advancedModal","display") == "block" && runtime.FocusedControl() == "create-banlist",
		"closing the list returns to the row that opened it, the dialog still open");
}

// A menu level stands in its column in a portrait view its column fills at
// its landscape size or more: the pause's from 0.827, the others' from a
// square view, so each in the 0.78 view the text-scale checks add.
static bool TallMenuLevel(int width, int height) { return width < .82f*height; }

// A list page at 100, 150 and 200 % text (section 2's reflow, LAY-004): its
// rows, their cells and lights are the 100 % row times the text size, so the
// enlarged text keeps its line; the selected item's detail lines stand in
// order on their own lines above the first section plate, the picture ending
// over the first; furniture (headings, tags, plates' labels, the prompt bar)
// keeps its size; every shown text stays on the screen; and at 100 % again
// every node is where it was.
static void CheckListTextScale(ScreenHost& host, const char* path, const char* name, const StateValues& states, const char* list,
	const std::vector<std::string>& details, const char* picture, const char* plate) {
	const auto source = Read(path);
	Document document; std::vector<Diagnostic> diagnostics;
	Check(document.Load(source,diagnostics) && diagnostics.empty(),"a list page validates for its text scale");
	Runtime runtime(host);
	Check(runtime.Initialize() && runtime.LoadDocument(source,name,diagnostics),"a list page loads for its text scale");
	std::string error; Runtime::EventEffects effects; double time = .1;
	Check(runtime.SetState(states,error,time) && runtime.RunEvent("onActivate",time,effects,error),"the list page opens with its rows");
	std::set<std::string> fixed;
	for (const auto& binding : document.Model().bindings)
		if (binding.property == "font-size" && binding.id.starts_with("text.") && !binding.values.empty() && binding.values[0].op == "/")
			fixed.insert(binding.node);
	Check(!fixed.empty(),"the page names its furniture");
	const auto bounds = [&](const std::string& id) { Bounds b; Check(runtime.GetBounds(id,b),"a list page node is laid out"); return b; };
	const auto shown = [&](const Node& node) { const auto display = runtime.PresentedValue(node.id,"display"); return !display || display->text != "none"; };
	const auto font = [&](const std::string& id) { const auto value = runtime.PresentedValue(id,"font-size"); return value ? float(value->data[0]) : -1.f; };
	const auto authored = [&](const std::string& id) { return float(document.Model().FindNode(id)->properties.at("font-size").data[0]); };
	std::map<std::string,Bounds> rest;
	for (const auto& [width, height] : {std::pair{1280,720},std::pair{960,720}}) for (const float text : {1.f,1.5f,2.f,1.f}) {
		Viewport view; view.canvasHeight = document.Model().canvasHeight; view.width = width; view.height = height; view.textScale = text;
		runtime.Frame(view,time += .01); runtime.Frame(view,time += 1);
		const float dp = view.DpRatio();
		// The rows: the 100 % row (20 u) times the text size, cells and lights alike.
		const auto row = bounds("row-0"), next = bounds("row-1");
		Check(Near(row.height,20*1.5f*dp*text,.6f) && Near(next.y-row.y,row.height,.6f),"each row is its 100 % height times the text size");
		for (const auto& child : document.Model().FindNode("row-0")->children) {
			if (child.id.ends_with("-marker") || child.id.ends_with("-focus") || !shown(child)) continue;
			const auto box = bounds(child.id);
			Check(Near(box.height,row.height,.6f),"a row's cells and lights grow with it");
		}
		const auto overflow = runtime.PresentedValue(document.Model().FindNode("row-0")->children.back().id,"text-overflow");
		Check(overflow && overflow->text == (text > 1 ? "ellipsis" : "clip"),"above 100 % a cell's value past its column ends in an ellipsis");
		// The details: in order, each on its own line, above the first plate,
		// under the picture, which keeps its aspect.
		const auto shot = bounds(picture);
		float last = shot.y + shot.height - .6f;
		for (const auto& id : details) {
			const auto line = bounds(id);
			Check(text <= 1 || line.y >= last - .6f,"above 100 % each detail line stands under the one before it, the first under the picture");
			last = text <= 1 ? last : line.y + line.height*15.f/16.f;
		}
		Check(text <= 1 || last <= bounds(plate).y + 5*1.5f*dp + .6f,"the detail lines end over the first section plate's band");
		Check(Near(shot.width/shot.height,183.f/137.f,.02f) || shot.height < 1,"the picture keeps its aspect");
		// Furniture keeps its size; every shown text stays on the screen.
		const Bounds screen{0,0,float(width),float(height)};
		std::vector<const Node*> pending{&document.Model().root};
		while (!pending.empty()) {
			const auto* node = pending.back(); pending.pop_back();
			if (!shown(*node)) continue;
			for (const auto& child : node->children) pending.push_back(&child);
			if (node->type != "text") continue;
			const auto box = bounds(node->id);
			Check(box.x >= -.5f && box.y >= -.5f && box.x+box.width <= screen.width+.5f && box.y+box.height <= screen.height+.5f,
				"each shown text of a list page stays on the screen");
			if (fixed.contains(node->id)) Check(Near(font(node->id)*text,authored(node->id),.01f),"furniture keeps its authored size");
		}
		// At 100 % again every node stands where it did.
		for (const auto& id : {std::string(list),std::string("row-0"),std::string(picture),details.front(),details.back()}) {
			const auto box = bounds(id); const auto key = id+"@"+std::to_string(width);
			if (text <= 1 && !rest.contains(key)) rest[key] = box;
			else if (text <= 1) Check(Near(box.x,rest[key].x,.01f) && Near(box.y,rest[key].y,.01f) && Near(box.height,rest[key].height,.01f),
				"at 100 % again a list page is as it was");
		}
	}
}

// A page at 100, 150 and 200 % text (section 2's reflow, LAY-004) in each of
// a case's views: the shown parts of each chain stand one under the other
// without overlapping (a chain may hold only above 100 %, or only in a
// landscape or a tall view); each part a case puts inside another stays in
// it; each named control stays on the screen, and so does every shown text
// outside a scrolling body or clip; furniture (whose font lengths fixed_text
// divides by the text size) keeps one size at every text size, its authored
// size in a landscape view; and back at 100 % every node is where it was.
// `extra` checks what else the page promises at each size.
struct TextChain { std::vector<std::string> ids; float above = 0; int view = 0; };   // view: 0 any, 1 landscape, 2 tall
struct TextScaleCase {
	const char* path = nullptr; const char* name = nullptr;
	StateValues states; std::vector<std::string> events;
	std::vector<TextChain> chains;
	std::vector<std::pair<std::string,std::string>> inside;
	std::vector<std::string> controls;
	// A view narrower than this times its height takes the tall arrangement,
	// at 100, 150 and 200 % text.
	std::vector<float> tallBelow = {.76f,.76f,.76f};
	bool textsOnScreen = true;   // Credits' stock boxes run past the canvas; its own check places its lines
	std::vector<std::pair<int,int>> sizes = {{1280,720},{960,720},{840,1080},{540,960}};
};
using TextScaleExtra = std::function<void(Runtime&,const Viewport&,float,bool)>;
static void CheckTextScaleCase(ScreenHost& host, const TextScaleCase& c, const TextScaleExtra& extra = {}) {
	const auto source = Read(c.path);
	Document document; std::vector<Diagnostic> diagnostics;
	Check(document.Load(source,diagnostics) && diagnostics.empty(),"a page validates for its text scale");
	Runtime runtime(host);
	Check(runtime.Initialize() && runtime.LoadDocument(source,c.name,diagnostics),"a page loads for its text scale");
	std::string error; Runtime::EventEffects effects; double time = .1;
	if (!c.states.empty()) Check(runtime.SetState(c.states,error,time),"the page's state is set");
	for (const auto& event : c.events) {
		Check(runtime.RunEvent(event,time,effects,error),"the page's events run");
		Viewport settle; settle.canvasHeight = document.Model().canvasHeight; settle.width = 1280; settle.height = 720;
		runtime.Frame(settle,time += 1);
	}
	std::set<std::string> fixed;
	for (const auto& binding : document.Model().bindings)
		if (binding.property == "font-size" && binding.id.starts_with("text.") && !binding.values.empty() && binding.values[0].op == "/")
			fixed.insert(binding.node);
	const auto bounds = [&](const std::string& id) { Bounds b; Check(runtime.GetBounds(id,b),"a text-scale node is laid out"); return b; };
	const auto hidden = [&](const Node& node) { const auto display = runtime.PresentedValue(node.id,"display"); return display && display->text == "none"; };
	const auto scrolls = [&](const Node& node) {
		const auto overflow = runtime.PresentedValue(node.id,"overflow"); return overflow && overflow->text != "visible";
	};
	const auto inside = [](const Bounds& a, const Bounds& b) {
		return a.x >= b.x-.6f && a.y >= b.y-.6f && a.x+a.width <= b.x+b.width+.6f && a.y+a.height <= b.y+b.height+.6f;
	};
	// The shown nodes, and those of them in no scrolling body or clip.
	const auto visible = [&](std::set<std::string>& open) {
		std::set<std::string> shown;
		std::vector<std::pair<const Node*,bool>> pending{{&document.Model().root,false}};
		while (!pending.empty()) {
			const auto [node, clipped] = pending.back(); pending.pop_back();
			if (hidden(*node)) continue;
			shown.insert(node->id);
			if (!clipped) open.insert(node->id);
			for (const auto& child : node->children) pending.push_back({&child,clipped || scrolls(*node)});
		}
		return shown;
	};
	std::map<std::string,float> furniture;
	std::map<std::string,Bounds> rest;
	const auto view = [&](int width, int height, float text) {
		Viewport result; result.canvasHeight = document.Model().canvasHeight; result.canvasMinWidth = document.Model().canvasMinWidth;
		result.width = width; result.height = height; result.textScale = text;
		runtime.Frame(result,time += .01); runtime.Frame(result,time += 1);
		return result;
	};
	for (const float text : {1.f,1.5f,2.f}) for (const auto& [width, height] : c.sizes) {
		const auto at = view(width,height,text);
		const bool tall = width < c.tallBelow[text > 1.5f ? 2 : text > 1 ? 1 : 0]*height;
		std::set<std::string> open;
		const auto shown = visible(open);
		if (text <= 1) for (const auto& id : shown) { Bounds b; if (runtime.GetBounds(id,b)) rest[id+"@"+std::to_string(width)] = b; }
		const Bounds screen{0,0,float(width),float(height)};
		for (const auto& chain : c.chains) {
			if (text <= chain.above || (chain.view == 1 && tall) || (chain.view == 2 && !tall)) continue;
			const std::string* last = nullptr; Bounds before{};
			for (const auto& id : chain.ids) {
				if (!shown.contains(id)) continue;
				const auto box = bounds(id);
				if (last && box.y < before.y+before.height-.6f)
					std::fprintf(stderr,"%s %.1f %dx%d %s at %.1f, above %s's foot %.1f\n",c.name,text,width,height,id.c_str(),box.y,last->c_str(),
						before.y+before.height);
				Check(!last || box.y >= before.y+before.height-.6f,"a chain's parts stand one under the other at every text size");
				last = &id; before = box;
			}
		}
		for (const auto& [child, container] : c.inside) {
			if (!shown.contains(child) || !shown.contains(container)) continue;
			const auto a = bounds(child), b = bounds(container);
			if (!inside(a,b)) std::fprintf(stderr,"%s %.1f %dx%d %s %.1f,%.1f %.1fx%.1f outside %s %.1f,%.1f %.1fx%.1f\n",c.name,text,width,height,
				child.c_str(),a.x,a.y,a.width,a.height,container.c_str(),b.x,b.y,b.width,b.height);
			Check(inside(a,b),"a part stays inside its container at every text size");
		}
		for (const auto& id : c.controls) {
			if (!shown.contains(id)) continue;
			const auto box = bounds(id);
			if (!inside(box,screen)) std::fprintf(stderr,"%s %.1f %dx%d control %s %.1f,%.1f %.1fx%.1f\n",c.name,text,width,height,id.c_str(),
				box.x,box.y,box.width,box.height);
			Check(inside(box,screen),"an essential control stays on the screen at every text size");
		}
		for (const auto& id : open) {
			const auto* node = document.Model().FindNode(id);
			if (!node || node->type != "text") continue;
			const auto box = bounds(id);
			if (!c.textsOnScreen && !fixed.contains(id)) continue;
			if (!inside(box,screen)) std::fprintf(stderr,"%s %.1f %dx%d text %s %.1f,%.1f %.1fx%.1f\n",c.name,text,width,height,id.c_str(),
				box.x,box.y,box.width,box.height);
			Check(inside(box,screen),"each shown text stays on the screen at every text size");
			if (!fixed.contains(id)) continue;
			const auto font = runtime.PresentedValue(id,"font-size");
			Check(font.has_value(),"a furniture text presents its size");
			const float size = float(font->data[0])*text, stock = float(node->properties.at("font-size").data[0]);
			const auto [found, added] = furniture.emplace(id+"@"+std::to_string(width),size);
			if (!Near(found->second,size,.01f) || (!tall && !Near(size,stock,.01f)))
				std::fprintf(stderr,"%s %.1f %dx%d furniture %s %.2f, before %.2f, authored %.2f\n",c.name,text,width,height,id.c_str(),size,
					found->second,stock);
			Check(Near(found->second,size,.01f),"furniture keeps one size at every text size");
			Check(tall || Near(size,stock,.01f),"furniture keeps its authored size in a landscape view");
		}
		if (extra) extra(runtime,at,text,tall);
	}
	for (const auto& [width, height] : c.sizes) {
		view(width,height,1);
		std::set<std::string> open;
		for (const auto& id : visible(open)) {
			// The runtime places a field's caret, selection and composition.
			if (id.ends_with("-caret") || id.ends_with("-selection") || id.ends_with("-composition")) continue;
			Bounds b; const auto found = rest.find(id+"@"+std::to_string(width));
			if (found == rest.end() || !runtime.GetBounds(id,b)) continue;
			if (!Near(b.x,found->second.x,.01f) || !Near(b.y,found->second.y,.01f) || !Near(b.width,found->second.width,.01f) ||
				!Near(b.height,found->second.height,.01f))
				std::fprintf(stderr,"%s %dx%d %s moved at 100%%\n",c.name,width,height,id.c_str());
			Check(Near(b.x,found->second.x,.01f) && Near(b.y,found->second.y,.01f) && Near(b.width,found->second.width,.01f) &&
				Near(b.height,found->second.height,.01f),"back at 100 % text every node of the page lies where it did");
		}
	}
}

// Section 2's text-scale contract on a page-level menu (title, pauses,
// SETTINGS, Single Player, Campaign, Multiplayer; build_retained_screens.py),
// at 100, 150 and 200% text on 16:9 and 4:3 views and the tall 0.78, 3:4 and
// 9:16 ones, where the page stands in one column (TallMenuLevel): each shown
// text keeps its box on the screen, and furniture text (whose font lengths
// fixed_text divides by the text size) keeps one size at every text size, its
// authored size in a view of 4:3 and wider and up to 1.6 times it in a tall
// view. `content` checks the page's reflowing parts at each size, with the box
// of `canvas` (the page's 4:3 canvas group, the view's height in a tall view),
// its dp in pixels and whether the view is tall. Each of `dialogs` opens in
// turn: its body stays in it above its actions, and it stays on the screen.
// Back at 100% every node lies where it lay before.
template <typename Content>
static void CheckPageTextScale(ScreenHost& host, const char* path, const char* name, const char* canvas,
	const std::vector<std::string>& events, const StateValues& states, const std::vector<std::string>& dialogs, Content content) {
	const auto source = Read(path);
	Document document; std::vector<Diagnostic> diagnostics;
	Check(document.Load(source,diagnostics) && diagnostics.empty(),"a page-level menu validates for its text scale");
	Runtime runtime(host);
	Check(runtime.Initialize() && runtime.LoadDocument(source,name,diagnostics),"a page-level menu loads for its text scale");
	std::string error; Runtime::EventEffects effects; double time = .1;
	if (!states.empty()) Check(runtime.SetState(states,error,time),"the page's state is set");
	for (const auto& event : events) Check(runtime.RunEvent(event,time,effects,error),"the page opens");
	std::set<std::string> fixed;
	for (const auto& binding : document.Model().bindings)
		if (binding.property == "font-size" && binding.id.starts_with("text.") && !binding.values.empty() && binding.values[0].op == "/")
			fixed.insert(binding.node);
	const auto bounds = [&](const std::string& id) { Bounds b; Check(runtime.GetBounds(id,b),"a page node is laid out"); return b; };
	const auto inside = [](const Bounds& a, const Bounds& b) {
		return a.x >= b.x-.5f && a.y >= b.y-.5f && a.x+a.width <= b.x+b.width+.5f && a.y+a.height <= b.y+b.height+.5f;
	};
	const auto shown = [&](const Node& node) { const auto display = runtime.PresentedValue(node.id,"display"); return !display || display->text != "none"; };
	const auto authored = [&](const std::string& id) { return document.Model().FindNode(id)->properties.at("font-size").data[0]; };
	const auto view = [&](int width, int height, float text) {
		Viewport result; result.canvasHeight = document.Model().canvasHeight; result.canvasMinWidth = document.Model().canvasMinWidth;
		result.width = width; result.height = height; result.textScale = text;
		runtime.Frame(result,time += .01); runtime.Frame(result,time += 1);
		return result;
	};
	// Each furniture text's presented size times the text size, by view.
	std::map<std::string,float> furniture;
	// Every shown text, under shown ancestors: on the screen, and furniture at
	// one size whatever the text size.
	const auto texts = [&](const Viewport& at, float text) {
		const Bounds screen{0,0,float(at.width),float(at.height)};
		const bool tall = TallMenuLevel(at.width,at.height);
		std::vector<const Node*> pending{&document.Model().root};
		while (!pending.empty()) {
			const auto* node = pending.back(); pending.pop_back();
			if (!shown(*node)) continue;
			for (const auto& child : node->children) pending.push_back(&child);
			if (node->type != "text") continue;
			const auto box = bounds(node->id);
			if (!inside(box,screen))
				std::fprintf(stderr,"%s %.1f %dx%d %s=%.1f,%.1f %.1fx%.1f\n",name,text,at.width,at.height,node->id.c_str(),box.x,box.y,box.width,box.height);
			Check(inside(box,screen),"each shown text of a page-level menu stays on the screen");
			if (!fixed.contains(node->id)) continue;
			const auto font = runtime.PresentedValue(node->id,"font-size");
			Check(font.has_value(),"a furniture text presents its size");
			const float size = static_cast<float>(font->data[0])*text, stock = static_cast<float>(authored(node->id));
			const auto [found, added] = furniture.emplace(node->id+"@"+std::to_string(at.width),size);
			if (!added && !Near(found->second,size,.01f))
				std::fprintf(stderr,"%s %dx%d %s furniture %.2f, before %.2f\n",name,at.width,at.height,node->id.c_str(),size,found->second);
			Check(Near(found->second,size,.01f),"a title, plate, plinth, prompt and card heading keep their size at every text size");
			Check(tall ? size >= stock-.01f && size <= 1.6f*stock+.01f : Near(size,stock,.01f),
				"furniture keeps its authored size, or the tall column's scale of it");
		}
	};
	const std::pair<int,int> sizes[] = {{1280,720},{960,720},{840,1080},{768,1024},{540,960}};
	// The nodes that show, under shown ancestors.
	const auto visible = [&]() {
		std::vector<std::string> ids;
		std::vector<const Node*> pending{&document.Model().root};
		while (!pending.empty()) {
			const auto* node = pending.back(); pending.pop_back();
			if (!shown(*node)) continue;
			ids.push_back(node->id);
			for (const auto& child : node->children) pending.push_back(&child);
		}
		return ids;
	};
	std::map<std::string,Bounds> rest;
	for (const float text : {1.f,1.5f,2.f}) for (const auto& [width, height] : sizes) {
		const auto at = view(width,height,text);
		if (text <= 1) for (const auto& id : visible()) { Bounds b; if (runtime.GetBounds(id,b)) rest[id+"@"+std::to_string(width)] = b; }
		texts(at,text);
		content(runtime,at,text,bounds(canvas),at.DpRatio(),TallMenuLevel(width,height));
		const Bounds screen{0,0,float(width),float(height)};
		for (const auto& dialog : dialogs) {
			Check(runtime.RunEvent(dialog+"Show",time,effects,error),"a page's dialog opens");
			view(width,height,text); view(width,height,text);
			texts(at,text);
			const auto frame = bounds(dialog+"-dialog"), body = bounds(dialog+"-body");
			Check(inside(frame,screen) && inside(body,frame),"the dialog and its body stay on the screen");
			for (const char* action : {"_yes","_no"}) {
				const auto plate = bounds(dialog+action);
				if (body.y+body.height > plate.y+.5f)
					std::fprintf(stderr,"%s %.1f %dx%d %s body ends %.1f, %s at %.1f\n",name,text,width,height,dialog.c_str(),body.y+body.height,action,plate.y);
				Check(inside(plate,frame) && body.y+body.height <= plate.y+.5f,"its body ends above its actions, inside it");
			}
			Check(runtime.RunEvent(dialog+"Hide",time,effects,error),"a page's dialog closes");
			view(width,height,text);
		}
	}
	for (const auto& [width, height] : sizes) {
		view(width,height,1);
		for (const auto& id : visible()) {
			Bounds b; const auto found = rest.find(id+"@"+std::to_string(width));
			if (found == rest.end() || !runtime.GetBounds(id,b)) continue;
			if (!Near(b.x,found->second.x,.01f) || !Near(b.y,found->second.y,.01f) || !Near(b.width,found->second.width,.01f) ||
				!Near(b.height,found->second.height,.01f))
				std::fprintf(stderr,"%s %dx%d %s moved at 100%%: %.2f,%.2f %.2fx%.2f\n",name,width,height,id.c_str(),b.x,b.y,b.width,b.height);
			Check(Near(b.x,found->second.x,.01f) && Near(b.y,found->second.y,.01f) && Near(b.width,found->second.width,.01f) &&
				Near(b.height,found->second.height,.01f),"back at 100% text every node lies where it did");
		}
	}
}

// The tall column's room: under the band's foot (`top` dp) and over the
// bottom band (`bottom` dp from the view's foot), in pixels.
static bool InTallRoom(const Bounds& box, const Viewport& view, float dp, float top, float bottom) {
	return box.y >= top*dp-.5f && box.y+box.height <= view.height-bottom*dp+.5f;
}

// A menu level's content column (section 9) at `text`. In a view of 4:3 and
// wider: at 100% where the stock column stands, 210x140 u at 395,190 u of the
// canvas; above it wider, to the 4:3 view's margin, and between the docked
// bands (60-420 u). In a tall view: under the plates, from the plates' label
// inset to the column's 66 dp margin, in the room between the bands. Each
// description fills it.
static void CheckContentColumn(Runtime& runtime, const Viewport& view, float text, const Bounds& canvas, float dp, bool tall,
	const std::vector<const char*>& labels) {
	const auto bounds = [&](const std::string& id) { Bounds b; Check(runtime.GetBounds(id,b),"a column node is laid out"); return b; };
	const float u = 1.5f*dp;
	const auto column = bounds("details");
	if (tall) {
		const auto plates = bounds("navigation"), back = bounds("back-label");
		if (!(column.y >= back.y+back.height-.5f && InTallRoom(plates,view,dp,110,100) && InTallRoom(column,view,dp,110,100)))
			std::fprintf(stderr,"tall column %.1f %dx%d plates %.1f-%.1f, column %.1f-%.1f\n",text,view.width,view.height,plates.y,
				plates.y+plates.height,column.y,column.y+column.height);
		Check(column.y >= back.y+back.height-.5f && Near(column.x,back.x,1.5f) && Near(column.x+column.width,view.width-66*dp,.5f),
			"a tall view stands the description under the plates, across the rest of the canvas");
		Check(InTallRoom(plates,view,dp,110,100) && InTallRoom(column,view,dp,110,100),"the tall column stands between the bands");
	} else if (text <= 1) Check(Near(column.x,canvas.x+395*u,.5f) && Near(column.y,canvas.y+190*u,.5f) && Near(column.width,210*u,.5f) &&
		Near(column.height,140*u,.5f),"at 100% the content column stands where it stood");
	else {
		if (!(Near(column.width,224*u,.5f) && column.y >= canvas.y+60*u-.5f && column.y+column.height <= canvas.y+420*u+.5f))
			std::fprintf(stderr,"column %.1f at %.1f,%.1f %.1fx%.1f, canvas y %.1f dp %.3f\n",text,column.x,column.y,column.width,column.height,canvas.y,dp);
		Check(Near(column.x,canvas.x+395*u,.5f) && Near(column.width,224*u,.5f) && column.y >= canvas.y+60*u-.5f &&
			column.y <= canvas.y+190*u+.5f && column.y+column.height <= canvas.y+420*u+.5f,
			"a larger text widens the column to the margin, between the docked bands");
	}
	for (const char* label : labels) {
		const auto box = bounds(label);
		Check(Near(box.x,column.x,.5f) && Near(box.y,column.y,.5f) && Near(box.width,column.width,.5f) && Near(box.height,column.height,.5f),
			"each description fills the column");
	}
}

// The title's text scale. In a view of 4:3 and wider each item's sentence on
// the message line starts at 364 u and ends above the plinth, which stands at
// 403.3 u at 100% and steps down only as far as the lines need, never into
// the prompt bar, and above 100% the sentence runs on to the 4:3 view's
// margin. In a tall view the wordmark, the plates, the message line and the
// plinth stand in that order between the bands. Either way CONTINUE's save
// title and detail stand on their own lines above the plinth, and the exit
// question grows.
static void CheckTitleTextScale(ScreenHost& host, const char* path) {
	CheckPageTextScale(host,path,"guis/menu/title.q4ui","home-content",{"open"},
		{{"menu_continue",true},{"menu_continue_title",std::string("Save")},{"menu_continue_detail",std::string("Level")}},{"exitModal"},
		[](Runtime& runtime, const Viewport& view, float text, const Bounds& canvas, float dp, bool tall) {
			const auto bounds = [&](const std::string& id) { Bounds b; Check(runtime.GetBounds(id,b),"a title node is laid out"); return b; };
			const float u = 1.5f*dp;
			const auto plinth = bounds("plinth"), prompts = bounds("prompts"), last = bounds("nav_settings");
			if (text <= 1 && !tall) Check(Near(plinth.y,canvas.y+403.3f*u,.5f),"at 100% the plinth stands where it stood");
			Check(plinth.y+plinth.height <= prompts.y+.5f && plinth.x+plinth.width <= view.width+.5f,
				"the plinth stays above the prompt bar and on the screen");
			if (tall) {
				const auto wordmark = bounds("wordmark"), first = bounds("nav_continue");
				Check(wordmark.y+wordmark.height <= first.y+.5f && InTallRoom(wordmark,view,dp,190,150) && InTallRoom(plinth,view,dp,190,150),
					"a tall view stands the wordmark over the plates, the column between the bands");
			}
			for (const char* id : {"detail-singleplayer-text","detail-loadgame-text","detail-multiplayer-text","detail-settings-text"}) {
				const auto box = bounds(id);
				const bool placed = tall ? box.y >= last.y+last.height-.5f : Near(box.y,canvas.y+364*u,.5f);
				if (!placed || box.y+box.height > plinth.y+.5f)
					std::fprintf(stderr,"title %.1f %dx%d %s at %.1f-%.1f, plinth %.1f\n",text,view.width,view.height,id,box.y,box.y+box.height,plinth.y);
				Check(placed && box.y+box.height <= plinth.y+.5f,"each item's sentence stands under the plates and ends above the plinth");
				Check(text <= 1 || tall || Near(box.x+box.width,canvas.x+619*u,.5f),"above 100% it runs on to the 4:3 view's margin");
			}
			const auto title = bounds("detail-continue-title"), detail = bounds("detail-continue-detail");
			Check(detail.y >= title.y+title.height-.5f && detail.y+detail.height <= plinth.y+.5f,
				"CONTINUE's save stands on its own lines above the plinth");
		});
}

// A pause's text scale (`path`, either family): the level block's lines stand
// in order inside the card, the picture above them until it gives way at
// 190%; in a tall view GAME PAUSED, the actions, EXIT and the card stand in
// that order between the bands. Both questions grow.
static void CheckPauseTextScale(ScreenHost& host, const char* path, const char* name) {
	StateValues states{{"pause_level",std::string("Level")},{"pause_detail",std::string("Corporal")},{"pause_objective_count",3.0},
		{"pause_stats",std::string("Stats")},{"pause_saved",std::string("Saved")}};
	for (int row = 0; row < 3; ++row) states["pause_objective_"+std::to_string(row)] = std::string("Objective");
	CheckPageTextScale(host,path,name,"home-content",{"open"},states,{"quitModal","exitModal"},
		[](Runtime& runtime, const Viewport& view, float text, const Bounds&, float dp, bool tall) {
			const auto bounds = [&](const std::string& id) { Bounds b; Check(runtime.GetBounds(id,b),"a level block node is laid out"); return b; };
			const auto card = bounds("level-block");
			Bounds last{};
			bool first = true;
			for (const char* id : {"level-name","level-detail","level-objectives-head","level-objective-0","level-objective-1","level-objective-2",
				"level-stats-rule","level-stats","level-saved"}) {
				const auto box = bounds(id);
				const bool inCard = box.x >= card.x-.5f && box.y >= card.y-.5f && box.x+box.width <= card.x+card.width+.5f &&
					box.y+box.height <= card.y+card.height+.5f;
				if (!inCard || (!first && box.y < last.y+last.height-.5f))
					std::fprintf(stderr,"level block %.1f %s at %.1f-%.1f after %.1f, card %.1f-%.1f\n",text,id,box.y,box.y+box.height,
						last.y+last.height,card.y,card.y+card.height);
				Check(inCard && (first || box.y >= last.y+last.height-.5f),"the level block's lines stand in order inside the card");
				last = box; first = false;
			}
			const auto shot = runtime.PresentedValue("level-shot","display");
			if (text < 1.9f) Check(shot && shot->text != "none" && bounds("level-shot").y+bounds("level-shot").height <= bounds("level-name").y+.5f,
				"the picture stands above the lines");
			else Check(shot && shot->text == "none","from 190% the picture has given its room to the lines");
			if (tall) {
				const auto nav = bounds("nav"), plinth = bounds("plinth");
				if (!(nav.y+nav.height <= plinth.y+.5f && plinth.y+plinth.height <= card.y+.5f && InTallRoom(card,view,dp,190,150)))
					std::fprintf(stderr,"tall pause %dx%d nav %.1f-%.1f plinth %.1f-%.1f card %.1f-%.1f\n",view.width,view.height,nav.y,
						nav.y+nav.height,plinth.y,plinth.y+plinth.height,card.y,card.y+card.height);
				Check(nav.y+nav.height <= plinth.y+.5f && plinth.y+plinth.height <= card.y+.5f && InTallRoom(card,view,dp,190,150) &&
					card.x >= -.5f && card.x+card.width <= view.width+.5f,"a tall view stands the actions, EXIT and the card in a column");
			}
		});
}

// The text scale of the menu levels with a content column: Single Player,
// Campaign (its available note), Multiplayer and SETTINGS (whose question
// grows).
static void CheckColumnPageTextScale(ScreenHost& host, const char* path, const char* name, const StateValues& states,
	const std::vector<std::string>& dialogs, std::vector<const char*> labels) {
	CheckPageTextScale(host,path,name,"content",{"onActivate"},states,dialogs,
		[labels](Runtime& runtime, const Viewport& view, float text, const Bounds& canvas, float dp, bool tall) {
			CheckContentColumn(runtime,view,text,canvas,dp,tall,labels);
		});
}

// The retained SETTINGS page (tools/ui/retained_settings_page.py): the stock
// settings screen's buttons on the home plates, each asking the session for
// its page; LOAD DEFAULTS asks first; a return lands on its section.
static void CheckSettingsPage(ScreenHost& host, const char* path) {
	const auto source = Read(path);
	Document document; std::vector<Diagnostic> diagnostics;
	if (!document.Load(source,diagnostics)) for (const auto& d : diagnostics) std::fprintf(stderr,"%u:%u %s %s\n",unsigned(d.line),unsigned(d.column),d.pointer.c_str(),d.message.c_str());
	Check(diagnostics.empty() && document.Model().id == "openq4.settings" && document.Model().canvasHeight == 720,
		"the SETTINGS page validates on the 4:3 canvas");
	CheckSessionActions(document);
	const std::pair<const char*,const char*> sections[] = {{"controls","settingsControls"},{"game","settingsGame"},
		{"display","settingsDisplay"},{"audio","settingsAudio"},{"back","back"}};
	for (const auto& [control, action] : sections) {
		const auto* node = document.Model().FindNode(control);
		Check(node && node->control && node->control->action == action,"each section asks the session for its page");
	}
	Check(std::get<std::string>(document.Model().actions.at("back").arguments.at("command").literal) == "settingsBack" &&
		std::get<std::string>(document.Model().actions.at("settingsDefaults").arguments.at("command").literal) == "settingsDefaults",
		"BACK and LOAD DEFAULTS' YES reach the session");
	for (const char* event : {"subpageentercontrols","subpageentergame","subpageenterdisplay","subpageenteraudio","subpagereturn","subpagereverse150","subpagereverse300"})
		Check(document.Model().events.contains(PresentationAliasKey(event)),"the page goes one level deeper to its sub-pages and back");
	Runtime runtime(host);
	Check(runtime.Initialize() && runtime.LoadDocument(source,"guis/menu/settings.q4ui",diagnostics),"the page loads into a runtime");
	Viewport viewport; viewport.canvasHeight = document.Model().canvasHeight; viewport.canvasMinWidth = document.Model().canvasMinWidth;
	viewport.width = 1280; viewport.height = 720;
	std::string error; Runtime::EventEffects effects;
	const auto press = [&](MenuInput input, double at) { runtime.MenuAction(input,true,at); runtime.MenuAction(input,false,at+.01); };
	const auto text = [&](const std::string& node, const char* property) {
		const auto found = runtime.PresentedValue(node,property); return found ? found->text : std::string("?");
	};
	Check(runtime.RunEvent("onActivate",0.1,effects,error),"the page arrives");
	runtime.Frame(viewport,0.4);
	Check(runtime.FocusedControl() == "controls","from the home it starts on CONTROLS");
	// The sections that go deeper summarize themselves in the column (section
	// 13.7); LOAD DEFAULTS and BACK keep the description, which names them.
	{
		const auto lit = [&](const char* node) { const auto found = runtime.PresentedValue(node,"opacity"); return found ? static_cast<float>(found->data[0]) : -1.f; };
		Check(Near(lit("summary-controls"),1,.001f) && Near(lit("description"),0,.001f) && text("summary-controls","text") == "#str_231571",
			"CONTROLS summarizes itself");
		Check(runtime.FocusControl("display",0.41),"focus DISPLAY");
		runtime.Frame(viewport,0.42);
		Check(Near(lit("summary-display"),1,.001f) && Near(lit("summary-controls"),0,.001f),"DISPLAY takes its place");
		Check(runtime.FocusControl("defaults",0.43),"focus LOAD DEFAULTS");
		runtime.Frame(viewport,0.44);
		Check(Near(lit("description"),1,.001f) && Near(lit("summary-display"),0,.001f),"LOAD DEFAULTS keeps the description");
		Check(runtime.FocusControl("controls",0.45),"back onto CONTROLS");
	}
	runtime.TakeActions();
	press(MenuInput::Down,0.5); press(MenuInput::Accept,0.6);
	{
		const auto actions = runtime.TakeActions();
		Check(actions.size() == 1 && actions[0].action == "settingsGame","GAME OPTIONS asks for its page");
	}
	// LOAD DEFAULTS asks first; NO asks nothing, YES asks the session.
	Check(runtime.FocusControl("defaults",0.7),"focus LOAD DEFAULTS");
	press(MenuInput::Accept,0.71);
	{
		// The plate's event, which the adapter runs.
		const auto actions = runtime.TakeActions();
		Check(actions.size() == 1 && actions[0].event == "defaultsmodalshow" && actions[0].action.empty() &&
			runtime.RunEvent(actions[0].event,0.72,effects,error) && effects.actions.empty(),"LOAD DEFAULTS asks the page, not the session");
	}
	runtime.Frame(viewport,1.0);
	Check(text("defaultsModal","display") == "block","LOAD DEFAULTS opens its question");
	Check(runtime.FocusedControl() == "defaultsModal_yes" || runtime.FocusedControl() == "defaultsModal_no","the question takes the focus");
	Check(runtime.FocusControl("defaultsModal_no",1.01),"focus NO");
	press(MenuInput::Accept,1.02);
	for (const auto& action : runtime.TakeActions()) {
		Check(!action.event.empty() && runtime.RunEvent(action.event,1.03,effects,error) && effects.actions.empty(),"NO asks nothing");
	}
	runtime.Frame(viewport,1.05); runtime.Frame(viewport,1.5);
	Check(text("defaultsModal","display") == "none","NO closes the question");
	Check(runtime.FocusControl("defaults",1.51),"focus LOAD DEFAULTS again");
	press(MenuInput::Accept,1.52);
	for (const auto& action : runtime.TakeActions()) Check(runtime.RunEvent(action.event,1.53,effects,error),"LOAD DEFAULTS asks again");
	runtime.Frame(viewport,1.6); runtime.Frame(viewport,2.0);
	Check(runtime.FocusControl("defaultsModal_yes",2.1),"focus YES");
	press(MenuInput::Accept,2.11);
	{
		// YES closes the question and asks the session to restore the defaults.
		const auto actions = runtime.TakeActions();
		Check(actions.size() == 1 && actions[0].event == "defaultsconfirmed" && runtime.RunEvent(actions[0].event,2.12,effects,error) &&
			effects.actions.size() == 1 && std::get<std::string>(effects.actions[0].arguments.at("command")) == "settingsDefaults",
			"YES restores the defaults");
	}
	// A return from a section lands on it.
	runtime.Frame(viewport,2.6); runtime.Frame(viewport,3.5);
	Check(text("defaultsModal","display") == "none","the question closes");
	Check(runtime.SetState({{"from_audio",true}},error,3.6) && runtime.RunEvent("onActivate",3.6,effects,error),"back from AUDIO");
	runtime.Frame(viewport,4.0);
	Check(runtime.FocusedControl() == "audio","the return lands on AUDIO");
	Check(runtime.RunEvent("onBack",4.1,effects,error) && effects.actions.size() == 1 &&
		std::get<std::string>(effects.actions[0].arguments.at("command")) == "settingsBack","Back asks the session to return");
	// A portrait view uses the same canvas contract as the engine adapter.
	// Navigation plates deliberately bleed off the leading edge (section 9);
	// their text and the description must stay wholly inside the view.
	const auto bounds = [&](const std::string& id) { Bounds b; Check(runtime.GetBounds(id,b),"a SETTINGS page node is laid out"); return b; };
	Viewport portrait = viewport; portrait.width = 540; portrait.height = 960;
	runtime.Frame(portrait,4.2);
	for (const char* id : {"controls-label","defaults-label","back-label","description"}) {
		const auto box = bounds(id);
		if (box.x < -.5f || box.x+box.width > 540.5f || box.y+box.height > 960.5f)
			std::fprintf(stderr,"SETTINGS portrait %s: x=%.2f y=%.2f width=%.2f height=%.2f\n",id,box.x,box.y,box.width,box.height);
		Check(box.x >= -.5f && box.x+box.width <= 540.5f && box.y+box.height <= 960.5f,"the page fits a portrait view");
	}
}

// The settings sub-pages (tools/ui/retained_settings_rows.py) lay out on the
// 4:3 canvas as Create Server does; a 16:9 view of it.
static Viewport SettingsViewport(const Document& document, int width = 1280, int height = 720) {
	Viewport viewport; viewport.canvasHeight = document.Model().canvasHeight; viewport.canvasMinWidth = document.Model().canvasMinWidth;
	viewport.width = width; viewport.height = height;
	return viewport;
}

// A settings sub-page in a 9:16 view: `above` and `below`, the last row of a
// tab's first column and the first of its second, stand one under the other
// in a single column across the content, at about twice the landscape size;
// `last`, focused, scrolls into the view; a 16:9 view sets the columns side by
// side again.
static void CheckSettingsPortrait(Runtime& runtime, const Document& document, const char* above, const char* below, const char* last,
	const char* rows, double at) {
	const auto bounds = [&](const std::string& id) { Bounds b; Check(runtime.GetBounds(id,b),"a settings row is laid out"); return b; };
	const auto wide = SettingsViewport(document), portrait = SettingsViewport(document,540,960);
	runtime.Frame(wide,at);
	const auto landscape = bounds(above);
	runtime.Frame(portrait,at+0.01);
	const auto first = bounds(above), second = bounds(below);
	if (!(Near(second.x,first.x) && second.y >= first.y+first.height-.5f))
		std::fprintf(stderr,"portrait %s x=%.1f y=%.1f h=%.1f, %s x=%.1f y=%.1f\n",above,first.x,first.y,first.height,below,second.x,second.y);
	Check(Near(second.x,first.x) && second.y >= first.y+first.height-.5f,"a 9:16 view stands the columns in one column");
	// 880 dp of content across a 960 dp view 540 pixels wide; the landscape
	// row is 36 dp tall at 1280x720.
	Check(first.width > 480 && first.width < 500 && first.x >= 0 && first.x+first.width <= 540.5f,"the column's rows cross the content");
	Check(first.height > landscape.height*1.1f && first.height < landscape.height*1.25f,"its rows grow to the column's width");
	Check(runtime.FocusControl(last,at+0.02),"focus the tab's last row");
	runtime.Frame(portrait,at+0.03); runtime.Frame(portrait,at+0.05);
	const auto shown = bounds(last), area = bounds(rows);
	Check(area.y+area.height > 860 && area.y+area.height < 960,"the rows run down to the bottom band");
	Check(shown.y >= area.y-.5f && shown.y+shown.height <= area.y+area.height+.5f,"the focused row scrolls into the view");
	runtime.Frame(wide,at+0.06);
	const auto left = bounds(above), right = bounds(below);
	Check(right.x > left.x+left.width && Near(left.width,landscape.width),"a 16:9 view sets the columns side by side again");
}

// A settings sub-page at 150% and 200% text (the runtime's view.text source),
// on 16:9, 4:3 and 9:16 views: the tab's (`page`) two columns stand one under
// the other in its scrolling rows, every row grown by the text size outside a
// tall view, and the focused `last` row scrolls into the view. Each text of a
// row that shows stays in its row, a one-line text's line (at this host's
// 0.6 em advance) inside its box. The tab strip and the prompt bar keep their
// authored text size in their fixed slots: every tab's label in its slot,
// every key cap, verb and BACK in the bar, all inside the screen. At 100% text
// in a 16:9 view again, the columns stand side by side.
static void CheckSettingsTextScale(ScreenHost& host, Runtime& runtime, const Document& document, const std::string& page, const char* above,
	const char* below, const char* last, double at) {
	const auto bounds = [&](const std::string& id) { Bounds b; Check(runtime.GetBounds(id,b),"a settings node is laid out"); return b; };
	const auto inside = [](const Bounds& a, const Bounds& b) {
		return a.x >= b.x-.5f && a.y >= b.y-.5f && a.x+a.width <= b.x+b.width+.5f && a.y+a.height <= b.y+b.height+.5f;
	};
	const auto shown = [&](const Node& node) { const auto display = runtime.PresentedValue(node.id,"display"); return !display || display->text != "none"; };
	const auto authored = [&](const std::string& id, const char* property) { return document.Model().FindNode(id)->properties.at(property).data[0]; };
	const auto font = [&](const std::string& id) {
		const auto found = runtime.PresentedValue(id,"font-size"); Check(found.has_value(),"a text presents its font size");
		return static_cast<float>(found->data[0]);
	};
	// The text nodes that show under `root` (a list's rows aside), each with
	// `holder`, the node that must contain it. A clipped viewport (a typed
	// number's) scrolls its text within itself: the viewport is held instead.
	const auto texts = [&](const Node& root, std::vector<std::pair<const Node*,std::string>>& out, const std::string& holder) {
		std::vector<const Node*> pending{&root};
		while (!pending.empty()) {
			const auto* node = pending.back(); pending.pop_back();
			if (node->id.find("-popup") != std::string::npos || !shown(*node)) continue;
			const auto overflow = node->properties.find("overflow");
			const bool clipped = overflow != node->properties.end() && overflow->second.text == "hidden";
			if (node->type == "text" || clipped) out.push_back({node,holder});
			if (!clipped) for (const auto& child : node->children) pending.push_back(&child);
		}
	};
	// One line of a nowrap text inside its box, at this host's glyph advance
	// for the whole pixel size RmlUi asks the font for.
	const auto oneLine = [&](const Node& node, const Bounds& box, float px) {
		const auto wrap = node.properties.find("white-space");
		if (node.type != "text" || wrap == node.properties.end() || wrap->second.text != "nowrap") return true;
		const auto text = runtime.PresentedValue(node.id,"text");
		std::size_t glyphs = 0;
		for (const unsigned char c : text ? host.Translate(text->text) : std::string()) glyphs += (c & 0xC0) != 0x80;
		if (glyphs*.6*std::floor(px+.001f) > box.width+1) std::fprintf(stderr,"%s: %zu glyphs at %.1f px in %.1f px\n",node.id.c_str(),glyphs,px,box.width);
		return glyphs*.6*std::floor(px+.001f) <= box.width+1;
	};
	auto view = SettingsViewport(document);
	runtime.Frame(view,at);
	const auto landscape = bounds(above);
	double time = at;
	for (const float text : {1.5f,2.f}) for (const auto& [width,height] : {std::pair{1280,720},std::pair{960,720},std::pair{540,960}}) {
		view = SettingsViewport(document,width,height); view.textScale = text;
		runtime.Frame(view,time += .01);
		const float ratio = view.DpRatio(); const bool tall = width < .76f*height;
		const Bounds screen{0,0,float(width),float(height)};
		const auto first = bounds(above), second = bounds(below);
		if (!(Near(second.x,first.x) && second.y >= first.y+first.height-.5f))
			std::fprintf(stderr,"text %.1f %dx%d %s x=%.1f y=%.1f h=%.1f, %s x=%.1f y=%.1f\n",text,width,height,above,first.x,first.y,first.height,below,second.x,second.y);
		Check(Near(second.x,first.x) && second.y >= first.y+first.height-.5f,"a larger text size stands the columns in one column");
		if (!tall) Check(Near(first.height,landscape.height*text*ratio,.5f) && Near(first.width,landscape.width*text*ratio,.5f),
			"its rows grow by the text size");
		// Each text of every row, heading and note of the tab.
		std::vector<std::pair<const Node*,std::string>> rows;
		for (const char* column : {"-column-0","-column-1"})
			for (const auto& row : document.Model().FindNode(page+column)->children) if (shown(row)) texts(row,rows,row.id);
		Check(!rows.empty(),"the tab's rows show text");
		for (const auto& [node,holder] : rows) {
			const auto box = bounds(node->id), owner = bounds(holder);
			const float px = node->type == "text" ? font(node->id)*ratio*text : 0;
			if (!inside(box,owner))
				std::fprintf(stderr,"text %.1f %dx%d %s=%.1f,%.1f %.1fx%.1f in %s=%.1f,%.1f %.1fx%.1f\n",text,width,height,node->id.c_str(),box.x,box.y,box.width,box.height,
					holder.c_str(),owner.x,owner.y,owner.width,owner.height);
			Check(inside(box,owner),"each text of a row stays in its row");
			Check(oneLine(*node,box,px),"a row's one-line text fits its box");
		}
		Check(runtime.FocusControl(last,time += .01),"focus the tab's last row");
		runtime.Frame(view,time += .01); runtime.Frame(view,time += .01);
		const auto revealed = bounds(last), area = bounds(page+"-rows");
		Check(inside(area,screen) && revealed.y >= area.y-.5f && revealed.y+revealed.height <= area.y+area.height+.5f,
			"the focused row scrolls into the view");
		// The strip and the prompt bar: fixed slots, authored text size.
		std::vector<std::pair<const Node*,std::string>> chrome;
		if (const auto* tabs = document.Model().FindNode("tabs")) {
			const auto strip = bounds(page.substr(0,page.find('-'))+"-strip");
			Check(inside(strip,screen),"the tab strip stays on the screen");
			for (const auto& slot : tabs->children) {
				Check(inside(bounds(slot.id),strip),"every tab's slot stays in the strip");
				texts(slot,chrome,slot.id);
			}
		}
		texts(*document.Model().FindNode("prompts"),chrome,"prompts");
		Check(inside(bounds("prompts"),screen),"the prompt bar stays on the screen");
		for (const auto& [node,holder] : chrome) {
			const auto box = bounds(node->id);
			const float dp = font(node->id);
			if (!inside(box,bounds(holder)) || !inside(box,screen) || !Near(dp*text,static_cast<float>(authored(node->id,"font-size")),.01f))
				std::fprintf(stderr,"chrome %.1f %dx%d %s=%.1f,%.1f %.1fx%.1f font=%.2f dp\n",text,width,height,node->id.c_str(),box.x,box.y,box.width,box.height,dp);
			Check(inside(box,bounds(holder)) && inside(box,screen),"every tab label, key cap, verb and BACK stays in its slot and on the screen");
			Check(Near(dp*text,static_cast<float>(authored(node->id,"font-size")),.01f),"the strip and the prompt bar keep their authored text size");
			Check(oneLine(*node,box,dp*ratio*text),"their one-line text fits its box");
		}
	}
	view = SettingsViewport(document); runtime.Frame(view,time += .01);
	const auto left = bounds(above), right = bounds(below);
	Check(right.x > left.x+left.width && Near(left.width,landscape.width),"100% text sets the columns side by side again");
}

// The retained CONTROLS page (tools/ui/retained_controls.py): the stock tabs
// as the page's tabs, each in two columns; each row shows the keys the
// session publishes for its action; Accept asks the session to wait for a
// key, the west face button to clear the focused row.
static void CheckControlsPage(ScreenHost& host, const char* path) {
	const auto source = Read(path);
	Document document; std::vector<Diagnostic> diagnostics;
	if (!document.Load(source,diagnostics)) for (const auto& d : diagnostics) std::fprintf(stderr,"%u:%u %s %s\n",unsigned(d.line),unsigned(d.column),d.pointer.c_str(),d.message.c_str());
	Check(diagnostics.empty() && document.Model().id == "openq4.controls" && document.Model().canvasHeight == 720,
		"the CONTROLS page validates on the 4:3 canvas");
	CheckSessionActions(document);
	for (int row = 0; row < 49; ++row) {
		const auto* node = document.Model().FindNode("controls_bind_"+std::to_string(row));
		Check(node && node->control && node->control->role == ControlRole::Button && node->control->action == "controlsBind",
			"each binding row asks the session to wait for a key");
	}
	Check(!document.Model().FindNode("controls_bind_49"),"the stock page's 49 bindings, no more");
	for (const char* tab : {"tab-move","tab-weapons","tab-attack","tab-other"}) Check(document.Model().FindNode(tab) != nullptr,"the stock tabs");
	Runtime runtime(host);
	Check(runtime.Initialize() && runtime.LoadDocument(source,"guis/menu/settings/controls.q4ui",diagnostics),"the page loads into a runtime");
	auto viewport = SettingsViewport(document); std::string error; Runtime::EventEffects effects;
	const auto press = [&](MenuInput input, double at) { runtime.MenuAction(input,true,at); runtime.MenuAction(input,false,at+.01); };
	const auto text = [&](const std::string& node, const char* property) {
		const auto found = runtime.PresentedValue(node,property); return found ? found->text : std::string("?");
	};
	const auto bounds = [&](const std::string& id) { Bounds b; Check(runtime.GetBounds(id,b),"a CONTROLS page node is laid out"); return b; };
	// Move Forward's keys as the adapter publishes them, a slot each: W, the
	// up arrow and a controller's A; the last row's V.
	Check(runtime.SetState({{"controls.0.keys.key0",std::string("W")},{"controls.0.keys.key0.glyph",0.0},
		{"controls.0.keys.key1",std::string("UPARROW")},{"controls.0.keys.key1.glyph",0.0},{"controls.0.keys.key1.arrow",1.0},
		{"controls.0.keys.keyCount",2.0},{"controls.0.keys.pad0",std::string("A")},{"controls.0.keys.pad0.glyph",1.0},
		{"controls.0.keys.padCount",1.0},{"controls.0.keys.more",std::string("+1")},
		{"controls.48.keys.key0",std::string("V")},{"controls.48.keys.keyCount",1.0}},error,0.1),
		"the session publishes the keys");
	Check(runtime.RunEvent("onActivate",0.2,effects,error),"the page arrives");
	runtime.Frame(viewport,0.5);
	Check(runtime.FocusedControl() == "controls_bind_0" && text("controls_bind_0-key0","display") == "flex" &&
		text("controls_bind_0-key0-cap","display") == "block" && text("controls_bind_0-key0-cap-name","text") == "W",
		"the page opens on Move Forward, its first key a keycap");
	Check(text("controls_bind_0-key1-cap","display") == "none" && text("controls_bind_0-key1-arrow-1","display") == "block" &&
		text("controls_bind_0-key1-arrow-2","display") == "none","an arrow key draws its arrow in place of its name");
	Check(text("controls_bind_0-pad0","display") == "flex" && text("controls_bind_0-pad0-kind-1","display") == "block" &&
		text("controls_bind_0-pad0-name","text") == "A" && text("controls_bind_0-pad0-name","display") == "block" &&
		text("controls_bind_0-pad0-kind-10","display") == "none" && text("controls_bind_0-pad1","display") == "none",
		"a controller button draws its family's glyph with the engine's label");
	Check(text("controls_bind_0-more","text") == "+1" && text("controls_bind_0-none","display") == "none" &&
		text("controls_bind_1-none","display") == "block" && text("controls_bind_1-key0","display") == "none",
		"a count stands for further keys, and a dash for an action with none");
	{
		// The glyphs' art is the document's symbols, drawn by every slot.
		const auto* arrow = document.Model().FindNode("controls_bind_0-key1-arrow-1");
		const auto* other = document.Model().FindNode("controls_bind_7-key0-arrow-1");
		Check(arrow && other && arrow->paths.size() == 2 && other->paths.size() == 2 && arrow->paths[1].id == other->paths[1].id,
			"each slot draws the shared symbol");
	}
	// While the session waits for the row's key the glyphs give way to the request.
	Check(runtime.SetState({{"controls.capture",0.0}},error,0.51),"the session waits for a key");
	runtime.Frame(viewport,0.52);
	Check(text("controls_bind_0-keys","display") == "none" && text("controls_bind_0-waiting","display") == "block" &&
		text("controls_bind_1-waiting","display") == "none","the waiting row asks for a key");
	Check(runtime.SetState({{"controls.capture",-1.0}},error,0.53),"the wait ends");
	runtime.Frame(viewport,0.54);
	Check(text("controls_bind_0-keys","display") == "flex" && text("controls_bind_0-waiting","display") == "none","the glyphs return");
	// The columns' outer edges hold the focus: Left from the leading column
	// has nowhere to go, and must not go looking for a tab.
	press(MenuInput::Left,0.55);
	Check(runtime.FocusedControl() == "controls_bind_0","Left at the leading column's edge keeps the focus");
	// Two columns: Move Forward leads the first, the sixth row the second.
	const auto first = bounds("controls_bind_0"), second = bounds("controls_bind_5");
	Check(second.x > first.x+first.width && std::abs(second.y-first.y) < .5f,"the tab's rows stand in two columns");
	runtime.TakeActions();
	press(MenuInput::Accept,0.6);
	{
		const auto actions = runtime.TakeActions();
		Check(actions.size() == 1 && actions[0].action == "controlsBind","Accept asks the session to wait for a key");
	}
	Check(runtime.RunEvent("onSecondary",0.7,effects,error) && effects.actions.size() == 1 &&
		std::get<std::string>(effects.actions[0].arguments.at("command")) == "controlsClear","the west face button clears the row");
	// Down reads the first column, then on down the second.
	Check(runtime.FocusControl("controls_bind_4",0.71),"focus the first column's last row");
	press(MenuInput::Down,0.72);
	Check(runtime.FocusedControl() == "controls_bind_5","Down goes on to the second column");
	press(MenuInput::Right,0.731);
	Check(runtime.FocusedControl() == "controls_bind_5","Right at the trailing column's edge keeps the focus");
	// The next tab: Weapons, its first row focused; Movement's rows rest.
	Check(runtime.RunEvent("onTabNext",0.75,effects,error),"the next tab");
	runtime.Frame(viewport,0.76);
	Check(std::get<double>(runtime.GetState().at("page.tab")) == 1 && runtime.FocusedControl() == "controls_bind_10",
		"the next tab opens on its first row");
	Check(!runtime.FocusControl("controls_bind_0",0.77),"another tab's rows take no focus");
	Check(runtime.RunEvent("onTabPrevious",0.78,effects,error) && runtime.RunEvent("onTabPrevious",0.79,effects,error),"back past the first tab");
	runtime.Frame(viewport,0.8);
	Check(std::get<double>(runtime.GetState().at("page.tab")) == 3 && runtime.FocusedControl() == "controls_bind_36",
		"the tabs wrap to the last");
	Check(text("controls_bind_48-key0-cap-name","text") == "V" && text("controls_bind_48-key0","display") == "flex",
		"the last tab shows its keys");
	// TOUCH LAYOUT closes the Other tab only where the session reports the
	// built-in touch controls (Android); it asks for the layout editor.
	{
		const auto* touch = document.Model().FindNode("controls_touch_layout");
		Check(touch && touch->control && touch->control->role == ControlRole::Button && touch->control->action == "controlsTouchLayout",
			"TOUCH LAYOUT asks the session for the layout editor");
		Check(text("controls_touch_layout","display") == "none","no TOUCH LAYOUT without the built-in touch controls");
		Check(!runtime.FocusControl("controls_touch_layout",0.81),"a hidden TOUCH LAYOUT takes no focus");
		Check(runtime.SetState({{"controls.touchLayout",true}},error,0.82),"the session reports the touch controls");
		runtime.Frame(viewport,0.83);
		Check(text("controls_touch_layout","display") == "block","TOUCH LAYOUT shows with the touch controls");
		const auto last = bounds("controls_bind_48"), touchRow = bounds("controls_touch_layout");
		Check(std::abs(touchRow.x-last.x) < .5f && touchRow.y > last.y,"TOUCH LAYOUT follows the last binding in its column");
		Check(runtime.FocusControl("controls_touch_layout",0.84),"TOUCH LAYOUT takes focus");
		runtime.TakeActions();
		press(MenuInput::Accept,0.85);
		const auto actions = runtime.TakeActions();
		Check(actions.size() == 1 && actions[0].action == "controlsTouchLayout","Accept on TOUCH LAYOUT asks for the editor");
		Check(runtime.SetState({{"controls.touchLayout",false}},error,0.86),"the touch controls go");
		runtime.Frame(viewport,0.87);
		Check(runtime.FocusControl("controls_bind_36",0.88),"focus returns to the tab");
	}
	Check(runtime.RunEvent("onBack",0.9,effects,error) && effects.actions.size() == 1 &&
		std::get<std::string>(effects.actions[0].arguments.at("command")) == "controlsBack","Back asks the session to return");
	const auto portrait = SettingsViewport(document,540,960);
	runtime.Frame(portrait,1.0);
	const auto panel = bounds("controls-panel"), row = bounds("controls_bind_48");
	Check(panel.x >= -.5f && panel.x+panel.width <= 540.5f && panel.y+panel.height <= 960.5f && row.x+row.width <= 540.5f,
		"the panel fits a portrait view");
	CheckSettingsPortrait(runtime,document,"controls_bind_42","controls_bind_43","controls_bind_48","controls-other-rows",1.1);
	CheckSettingsTextScale(host,runtime,document,"controls-other","controls_bind_42","controls_bind_43","controls_bind_48",1.2);
}

// The retained GAME OPTIONS page (tools/ui/retained_game.py): five tabs of
// at most two columns; a row naming a CVar reads it back and sets it with
// settings.player.set; the rows the stock page computes ask the session; the
// custom crosshair follows Crosshair's setting.
static void CheckGamePage(ScreenHost& host, const char* path) {
	const auto source = Read(path);
	Document document; std::vector<Diagnostic> diagnostics;
	if (!document.Load(source,diagnostics)) for (const auto& d : diagnostics) std::fprintf(stderr,"%u:%u %s %s\n",unsigned(d.line),unsigned(d.column),d.pointer.c_str(),d.message.c_str());
	Check(diagnostics.empty() && document.Model().id == "openq4.game" && document.Model().canvasHeight == 720,
		"the GAME page validates on the 4:3 canvas");
	CheckSessionActions(document);
	const std::pair<const char*,const char*> settings[] = {{"game_freelook","in_freeLook"},{"game_msensitivity","sensitivity"},
		{"game_invertmouse","m_pitch"},{"game_promptstyle","in_promptStyle"},{"game_crosshaircolor","g_crosshairColor"},
		{"game_hudscale","ui_hudScale"},{"game_console","con_allowConsole"},{"game_vrsnap","vr_snapTurnAngle"}};
	for (const auto& [control, cvar] : settings) {
		const auto* node = document.Model().FindNode(control);
		const auto found = document.Model().actions.find(std::string("set.")+cvar);
		Check(node && node->control && node->control->action == std::string("set.")+cvar && found != document.Model().actions.end() &&
			found->second.operation == "settings.player.set" && std::get<std::string>(found->second.arguments.at("cvar").literal) == cvar,
			"a setting row sets its own CVar");
	}
	for (const char* verb : {"gameGunPosition","gameForceModel","gameCrosshair","gameCorpseTime","gameLanguage"}) {
		const auto found = document.Model().actions.find(verb);
		Check(found != document.Model().actions.end() && found->second.operation == "session.menuValue" &&
			std::get<std::string>(found->second.arguments.at("command").literal) == verb,"a computed row asks the session");
	}
	Check(std::get<std::string>(document.Model().actions.at("back").arguments.at("command").literal) == "gameBack","BACK asks the session to return");
	Check(!document.Model().FindNode("game_section"),"tabs take the Section row's place");
	// Every row's CVar, at its type's initial value but for these.
	const auto saved = host.cvars;
	host.cvars.insert({{"in_freeLook",true},{"sensitivity",5.0},{"m_pitch",std::string("0.022")},{"g_crosshairCustom",std::string("0")},
		{"g_crosshairSize",32.0},{"g_crosshairColor",std::string("1 0 0 1")},{"in_promptStyle",0.0}});
	for (const auto& [name, declaration] : document.Model().state)
		if (!declaration.cvar.empty()) host.cvars.emplace(declaration.cvar,declaration.initial);
	Runtime runtime(host);
	Check(runtime.Initialize() && runtime.LoadDocument(source,"guis/menu/settings/game.q4ui",diagnostics),"the page loads into a runtime");
	auto viewport = SettingsViewport(document); std::string error; Runtime::EventEffects effects;
	const auto press = [&](MenuInput input, double at) { runtime.MenuAction(input,true,at); runtime.MenuAction(input,false,at+.01); };
	const auto bounds = [&](const std::string& id) { Bounds b; Check(runtime.GetBounds(id,b),"a GAME page node is laid out"); return b; };
	const auto tab = [&]() { return std::get<double>(runtime.GetState().at("page.tab")); };
	// The one action a control queued, resolved through its descriptor.
	const auto taken = [&](const char* action) {
		const auto actions = runtime.TakeActions();
		if (actions.size() != 1 || actions[0].action != action) {
			std::fprintf(stderr,"Expected action %s; received %zu",action,actions.size());
			for (const auto& pending : actions) std::fprintf(stderr," %s",pending.action.c_str());
			std::fprintf(stderr,"\n");
		}
		Check(actions.size() == 1 && actions[0].action == action,"a control requests its action");
		ActionInvocation invocation;
		const StateValue* input = actions.size() == 1 && actions[0].proposal ? &*actions[0].proposal : nullptr;
		Check(!actions.empty() && runtime.ResolveAction(actions[0].action,invocation,error,input),"the action resolves");
		if (!actions.empty() && actions[0].proposalToken) runtime.AcknowledgeControlProposal(actions[0].node,actions[0].proposalToken,true);
		return invocation;
	};
	Check(runtime.SetState({{"game.gunPosition",0.0},{"game.forceModel",false},{"game.crosshair",3.0},
		{"game.crosshairImage",std::string("gfx/guis/crosshairs/crosshair_blaster.tga")},{"game.corpseTime",6.0},{"game.language",0.0}},error,0.1),
		"the session publishes the computed rows");
	Check(runtime.RunEvent("onActivate",0.2,effects,error),"the page arrives");
	runtime.Frame(viewport,0.5);
	Check(tab() == 0 && runtime.FocusedControl() == "game_freelook","the page opens on GENERAL's first row");
	// GENERAL: its own rows in the first column, GAMEPLAY and VIEW WEAPON in
	// the second; Down from the first column's last row goes on to the second,
	// and Up from the first row reaches the tab.
	const auto general = bounds("game_freelook"), gameplay = bounds("game_corpsesink");
	Check(gameplay.x > general.x+general.width,"the sections stand in two columns");
	Check(runtime.FocusControl("game_forcemodel",0.51),"focus the first column's last row");
	press(MenuInput::Down,0.52);
	Check(runtime.FocusedControl() == "game_corpsesink","Down goes on to the second column");
	Check(runtime.FocusControl("game_freelook",0.53),"focus the first row");
	press(MenuInput::Up,0.54);
	Check(runtime.FocusedControl() == "tab-general","Up from the first row reaches the tab");
	// A setting row sets its CVar: Free Look off.
	Check(runtime.FocusControl("game_freelook",0.55),"focus Free Look");
	press(MenuInput::Accept,0.56);
	auto invocation = taken("set.in_freeLook");
	Check(std::get<std::string>(invocation.arguments.at("cvar")) == "in_freeLook" && !std::get<bool>(invocation.arguments.at("value")),
		"Accept turns Free Look off");
	// MOUSE INPUT: a sensitivity step.
	Check(runtime.RunEvent("onTabNext",0.6,effects,error),"the next tab");
	runtime.Frame(viewport,0.61);
	Check(tab() == 1 && runtime.FocusedControl() == "game_invertmouse","MOUSE INPUT opens on its first row");
	Check(!runtime.FocusControl("game_freelook",0.62),"GENERAL's rows rest while another tab is current");
	Check(runtime.FocusControl("game_msensitivity",1.2),"focus the mouse sensitivity");
	press(MenuInput::Right,1.21);
	Check(std::abs(std::get<double>(taken("set.sensitivity").arguments.at("value"))-5.01) < 1e-9,"Right raises the sensitivity a step");
	// Its typed number follows it: a third decimal place is refused, a value
	// to the slider's places sets the CVar, and it settles once the CVar
	// reads it back, as the adapter waits for it.
	press(MenuInput::Down,1.215);
	Check(runtime.FocusedControl() == "game_msensitivity_number","the typed sensitivity follows its slider");
	const auto entry = [&]() { return runtime.GetWidgetState("game_msensitivity_number")->number->identity; };
	const auto typed = [&](const char* value, double at) {
		const auto size = runtime.GetWidgetState("game_msensitivity_number")->number->state.text.size();
		return runtime.SetNumberSelection("game_msensitivity_number",entry(),0,size,error,at) &&
			runtime.ReplaceNumberSelection("game_msensitivity_number",entry(),value,error,at);
	};
	Check(runtime.BeginNumberEdit("game_msensitivity_number",error,1.216) && typed("7.255",1.217) &&
		!runtime.CommitNumberEdit("game_msensitivity_number",entry(),error,1.217) && runtime.TakeActions().empty(),
		"a third decimal place asks nothing");
	Check(typed("7.25",1.218) && runtime.CommitNumberEdit("game_msensitivity_number",entry(),error,1.218),"type 7.25 and commit it");
	{
		const auto actions = runtime.TakeActions();
		ActionInvocation invocation;
		Check(actions.size() == 1 && actions[0].action == "set.sensitivity" && actions[0].proposal && actions[0].proposalToken &&
			runtime.ResolveAction(actions[0].action,invocation,error,&*actions[0].proposal) &&
			std::get<double>(invocation.arguments.at("value")) == 7.25,"the typed value sets the sensitivity");
		host.cvars["sensitivity"] = 7.25;
		runtime.Frame(viewport,1.219);
		Check(runtime.AcknowledgeControlProposal(actions[0].node,actions[0].proposalToken,true),"the read-back value is acknowledged");
		const auto settled = runtime.GetWidgetState("game_msensitivity_number");
		Check(settled && std::get<double>(settled->accepted) == 7.25 && (!settled->number || !settled->number->conflict),
			"and the field settles on it without a conflict");
	}
	press(MenuInput::Down,1.2195);
	Check(runtime.FocusedControl() == "game_mcpi","Down goes on to the next row");
	// HUD: the crosshair colour list reads g_crosshairColor and offers the
	// next colour.
	Check(runtime.RunEvent("tab_hud",1.25,effects,error),"choose the HUD tab");
	runtime.Frame(viewport,1.26);
	Check(tab() == 3 && runtime.FocusedControl() == "game_hudlayout","HUD opens on its first row");
	Check(runtime.FocusControl("game_crosshaircolor",1.3),"focus the crosshair colour");
	press(MenuInput::Accept,1.31); runtime.Frame(viewport,1.35);
	press(MenuInput::Down,1.4); press(MenuInput::Accept,1.45);
	Check(std::get<std::string>(taken("set.g_crosshairColor").arguments.at("value")) == "1 0.5 0 1","a colour takes its stock value");
	// The custom crosshair waits for Crosshair's Custom setting.
	runtime.Frame(viewport,1.5);
	Check(!runtime.FocusControl("game_crosshair",1.5),"the crosshair is locked while the weapons' own crosshairs show");
	host.cvars["g_crosshairCustom"] = std::string("1");
	runtime.Frame(viewport,1.6);
	Check(runtime.FocusControl("game_crosshair",1.61),"the custom crosshair unlocks");
	press(MenuInput::Right,1.62);
	Check(std::get<double>(taken("gameCrosshair").arguments.at("value")) == 4,"Right asks the session for the next crosshair");
	// Corpse Time shows Custom but never offers it.
	Check(runtime.RunEvent("tab_general",1.65,effects,error),"back to GENERAL");
	runtime.Frame(viewport,1.66);
	Check(runtime.FocusControl("game_corpsetime",1.7),"focus Corpse Time");
	press(MenuInput::Accept,1.71); runtime.Frame(viewport,1.75);
	press(MenuInput::Down,1.8); runtime.Frame(viewport,1.85);
	Check(runtime.GetWidgetState("game_corpsetime")->highlight != "game_corpsetime-option-7","Custom cannot be highlighted");
	press(MenuInput::Back,1.9); runtime.Frame(viewport,1.95); runtime.TakeActions();
	// The language reload re-activates the page, which the session marks:
	// the page returns to GENERAL, focused on Language, from any tab.
	Check(runtime.RunEvent("tab_vr",1.955,effects,error),"choose VIRTUAL REALITY");
	runtime.Frame(viewport,1.958);
	Check(runtime.SetState({{"game.languageReloaded",true}},error,1.96) && runtime.RunEvent("onActivate",1.97,effects,error),"reload");
	runtime.Frame(viewport,1.98);
	Check(tab() == 0 && runtime.FocusedControl() == "game_language","after a language reload the page returns to Language");
	// The back input asks the session to return.
	Check(runtime.RunEvent("onBack",2.0,effects,error) && effects.actions.size() == 1 &&
		std::get<std::string>(effects.actions[0].arguments.at("command")) == "gameBack","Back asks the session to return");
	const auto portrait = SettingsViewport(document,540,960);
	runtime.Frame(portrait,2.1);
	const auto panel = bounds("game-panel"), row = bounds("game_weaponkick");
	Check(panel.x >= -.5f && panel.x+panel.width <= 540.5f && panel.y+panel.height <= 960.5f && row.x+row.width <= 540.5f,
		"the panel fits a portrait view");
	CheckSettingsPortrait(runtime,document,"game_forcemodel","game_corpsesink","game_weaponkick","game-general-rows",2.2);
	CheckSettingsTextScale(host,runtime,document,"game-general","game_forcemodel","game_corpsesink","game_weaponkick",2.3);
	host.cvars = saved;
}

// The retained AUDIO settings page (tools/ui/retained_audio.py): the stock
// page's rows in the settings sub-page, one screen with no tabs, each asking
// the session for its new value with its own verb, the session's readback
// setting what they show; BACK and the back input ask the session to return.
static void CheckAudioPage(ScreenHost& host, const char* path) {
	const auto source = Read(path);
	Document document; std::vector<Diagnostic> diagnostics;
	if (!document.Load(source,diagnostics)) for (const auto& d : diagnostics) std::fprintf(stderr,"%u:%u %s %s\n",unsigned(d.line),unsigned(d.column),d.pointer.c_str(),d.message.c_str());
	Check(diagnostics.empty() && document.Model().id == "openq4.audio" && document.Model().canvasHeight == 720,
		"the AUDIO page validates on the 4:3 canvas");
	CheckSessionActions(document);
	const std::pair<const char*,const char*> rows[] = {{"audio_volume","audioVolume"},{"audio_music","audioMusic"},
		{"audio_dialogue","audioDialogue"},{"audio_backend","audioBackend"},{"audio_device","audioDevice"},{"audio_eax","audioEax"},
		{"audio_speakers","audioSpeakers"},{"audio_hrtf","audioHrtf"},{"audio_subtitles","audioSubtitles"},
		{"audio_subtitle_size","audioSubtitleSize"},{"audio_subtitle_background","audioSubtitleBackground"},
		{"audio_subtitle_speakers","audioSubtitleSpeakers"}};
	for (const auto& [control, verb] : rows) {
		const auto* node = document.Model().FindNode(control);
		const auto found = document.Model().actions.find(verb);
		Check(node && node->control && node->control->action == verb && found != document.Model().actions.end() &&
			found->second.operation == "session.menuValue" && std::get<std::string>(found->second.arguments.at("command").literal) == verb,
			"each row asks the session with its own verb");
	}
	const auto back = document.Model().actions.find("back");
	Check(back != document.Model().actions.end() && std::get<std::string>(back->second.arguments.at("command").literal) == "audioBack",
		"BACK asks the session to return");
	Check(!document.Model().FindNode("tab-audio"),"one screen of rows has no tab strip");
	Runtime runtime(host);
	Check(runtime.Initialize() && runtime.LoadDocument(source,"guis/menu/settings/audio.q4ui",diagnostics),"the page loads into a runtime");
	auto viewport = SettingsViewport(document); std::string error; Runtime::EventEffects effects;
	const auto press = [&](MenuInput input, double at) { runtime.MenuAction(input,true,at); runtime.MenuAction(input,false,at+.01); };
	const auto bounds = [&](const std::string& id) { Bounds b; Check(runtime.GetBounds(id,b),"an AUDIO page node is laid out"); return b; };
	// The one action a control queued, resolved through its descriptor.
	const auto taken = [&](const char* action) {
		const auto actions = runtime.TakeActions();
		if (actions.size() != 1 || actions[0].action != action) {
			std::fprintf(stderr,"Expected action %s; received %zu",action,actions.size());
			for (const auto& pending : actions) std::fprintf(stderr," %s",pending.action.c_str());
			std::fprintf(stderr,"\n");
		}
		Check(actions.size() == 1 && actions[0].action == action,"a control requests its action");
		ActionInvocation invocation;
		const StateValue* input = actions.size() == 1 && actions[0].proposal ? &*actions[0].proposal : nullptr;
		Check(!actions.empty() && runtime.ResolveAction(actions[0].action,invocation,error,input),"the action resolves");
		if (!actions.empty() && actions[0].proposalToken) runtime.AcknowledgeControlProposal(actions[0].node,actions[0].proposalToken,true);
		return invocation;
	};
	// The session's readback: 70% volume, the second of three devices, EAX on,
	// 5.1 speakers and the subtitles at 150%.
	Check(runtime.SetState({{"audio.volume",70.0},{"audio.music",40.0},{"audio.dialogue",100.0},{"audio.backend",1.0},
		{"audio.device.count",3.0},{"audio.device.0.label",std::string("Default device")},{"audio.device.1.label",std::string("Speakers")},
		{"audio.device.2.label",std::string("Headset")},{"audio.device",1.0},{"audio.eax",true},{"audio.speakers",2.0},{"audio.hrtf",0.0},
		{"audio.subtitles",true},{"audio.subtitleSize",150.0},{"audio.subtitleBackground",30.0},{"audio.subtitleSpeakers",false}},error,0.1),
		"the session publishes the values");
	Check(runtime.RunEvent("onActivate",0.2,effects,error),"the page arrives");
	runtime.Frame(viewport,0.5);
	Check(runtime.FocusedControl() == "audio_volume","the first row takes the focus");
	const auto track = bounds("audio_volume-track"), fill = bounds("audio_volume-fill");
	Check(fill.width > track.width*.6f && fill.width < track.width*.8f,"the volume's fill runs to 70%");
	// LEVELS and DEVICES lead; EFFECTS stands in the second column.
	const auto volume = bounds("audio_volume"), eax = bounds("audio_eax");
	Check(eax.x > volume.x+volume.width && std::abs(eax.y-volume.y) < .5f,"EFFECTS stands beside LEVELS");
	// The device row unfolds its list; Back folds it without asking anything.
	Check(runtime.FocusControl("audio_device",0.51),"focus the device");
	press(MenuInput::Accept,0.52); runtime.Frame(viewport,0.55);
	Check(runtime.GetWidgetState("audio_device") && runtime.GetWidgetState("audio_device")->popupOpen && runtime.TakeActions().empty(),
		"the device row unfolds its list");
	press(MenuInput::Back,0.56); runtime.Frame(viewport,0.58);
	Check(!runtime.GetWidgetState("audio_device")->popupOpen && runtime.TakeActions().empty(),"Back folds the list without a request");
	Check(runtime.FocusControl("audio_volume",0.59),"focus the volume again");
	// A step to the right asks for 80%.
	runtime.TakeActions();
	press(MenuInput::Right,0.6);
	auto invocation = taken("audioVolume");
	Check(invocation.operation == "session.menuValue" && std::get<std::string>(invocation.arguments.at("command")) == "audioVolume" &&
		std::get<double>(invocation.arguments.at("value")) == 80,"Right asks the session for 80%");
	// The check box asks for the other setting.
	Check(runtime.FocusControl("audio_eax",0.7),"focus EAX");
	press(MenuInput::Accept,0.71);
	invocation = taken("audioEax");
	Check(!std::get<bool>(invocation.arguments.at("value")),"Accept asks the session to turn EAX off");
	Check(runtime.FocusControl("audio_subtitle_speakers",0.73),"focus Speaker Names");
	press(MenuInput::Accept,0.74);
	Check(std::get<bool>(taken("audioSubtitleSpeakers").arguments.at("value")),"Accept asks the session to name the speakers");
	// The back input asks the session to return.
	Check(runtime.RunEvent("onBack",0.8,effects,error) && effects.actions.size() == 1 &&
		std::get<std::string>(effects.actions[0].arguments.at("command")) == "audioBack","Back asks the session to return");
	// A portrait view: the panel and its rows stay inside the view.
	const auto portrait = SettingsViewport(document,540,960);
	runtime.Frame(portrait,1.0);
	const auto panel = bounds("audio-panel"), last = bounds("audio_subtitle_speakers");
	Check(panel.x >= -.5f && panel.x+panel.width <= 540.5f && panel.y+panel.height <= 960.5f && last.x+last.width <= 540.5f,
		"the panel fits a portrait view");
	CheckSettingsPortrait(runtime,document,"audio_device","audio_eax","audio_subtitle_speakers","audio-audio-rows",1.1);
	runtime.Frame(portrait,1.2);
	const auto band = runtime.PresentedValue("audio-header","display");
	Check(band && band->text == "none","the tall column's headings stand in its rows, not the header band");
	CheckSettingsTextScale(host,runtime,document,"audio-audio","audio_device","audio_eax","audio_subtitle_speakers",1.3);
	// The single column at a larger text size takes its headings into its rows too.
	auto larger = SettingsViewport(document); larger.textScale = 1.5f;
	runtime.Frame(larger,1.6);
	const auto stacked = runtime.PresentedValue("audio-header","display");
	runtime.Frame(SettingsViewport(document),1.61);
	const auto wide = runtime.PresentedValue("audio-header","display");
	Check(stacked && stacked->text == "none" && wide && wide->text == "block","from 150% text the headings stand in the rows, at 100% in the header band");
}

static void CheckCreateServerPage(ScreenHost& host, const char* path) {
	const auto source = Read(path);
	Document document; std::vector<Diagnostic> diagnostics;
	if (!document.Load(source,diagnostics)) for (const auto& d : diagnostics) std::fprintf(stderr,"%u:%u %s %s\n",unsigned(d.line),unsigned(d.column),d.pointer.c_str(),d.message.c_str());
	Check(diagnostics.empty() && document.Model().id == "openq4.createserver" && document.Model().canvasHeight == 720,"the Create Server page validates");
	CheckSessionActions(document);
	const char* const verbs[] = {"createLan","createDedicated","createGameType","createAutoBalance","createFragLimit","createCaptureLimit",
		"createControlTime","createTimeLimit","createMaxPlayers","createMinPlayers","createVoting","createTeamDamage","createTourneyLimit",
		"createUsePass","createServerMods","createWeaponStay","createPure","createRate","createReady","createSpectators","createWarmup",
		"createVoice","createBuying","createBots","createBotSkill"};
	for (const char* verb : verbs) {
		const auto found = document.Model().actions.find(verb);
		Check(found != document.Model().actions.end() && found->second.operation == "session.menuValue" &&
			std::get<std::string>(found->second.arguments.at("command").literal) == verb,"each setting asks the session with its own verb");
	}
	const std::pair<const char*,const char*> menu[] = {{"back","createBack"},{"createMap","createMap"},{"createStart","createStart"},
		{"createName","createName"},{"mapsNewer","createMapsNewer"},{"mapsOlder","createMapsOlder"},{"createMapCycle","createMapCycle"},
		{"createPassword","createPassword"},{"createRconPassword","createRconPassword"},{"createBanList","createBanList"},
		{"createBanPick","createBanPick"},{"createBanRemove","createBanRemove"}};
	for (const auto& [action, command] : menu) {
		const auto found = document.Model().actions.find(action);
		Check(found != document.Model().actions.end() && std::get<std::string>(found->second.arguments.at("command").literal) == command,
			"the page's verbs reach the session");
	}
	// The text fields: the passwords masked, 16 bytes and possibly empty as the
	// stock fields were; the map cycle may be empty, the server name not.
	const auto field = [&](const char* id) {
		const auto* node = document.Model().FindNode(id);
		Check(node && node->control && std::holds_alternative<NumberSpec>(node->control->widget),"a Create Server text field");
		return std::get<NumberSpec>(node->control->widget);
	};
	for (const char* id : {"create-password","create-rconpassword"}) {
		const auto spec = field(id);
		Check(spec.textField && spec.masked && spec.allowEmpty && spec.maxBytes == 16,"a password field is masked, may be empty and takes 16 bytes");
	}
	const auto nameField = field("create-name"), cycleField = field("create-mapcycle");
	Check(!nameField.masked && !nameField.allowEmpty && nameField.maxBytes == 25 && !cycleField.masked && cycleField.allowEmpty &&
		cycleField.maxBytes == 200,"the server name needs text; the map cycle may be empty");
	Runtime runtime(host);
	Check(runtime.Initialize() && runtime.LoadDocument(source,"guis/menu/createserver.q4ui",diagnostics),"the page loads into a runtime");
	Viewport viewport; viewport.canvasHeight = 720; viewport.width = 1280; viewport.height = 720;
	std::string error; Runtime::EventEffects effects;
	const auto value = [&](const std::string& node, const char* property, size_t index = 0) {
		const auto found = runtime.PresentedValue(node,property); return found ? static_cast<float>(found->data[index]) : -999.f;
	};
	const auto text = [&](const std::string& node, const char* property) {
		const auto found = runtime.PresentedValue(node,property); return found ? found->text : std::string("?");
	};
	const auto bounds = [&](const std::string& id) { Bounds b; Check(runtime.GetBounds(id,b),"a Create Server node is laid out"); return b; };
	StateValues maps = {{"create.map_count",3.0},{"create.map_first",0.0},{"create.map_selected",1.0},{"create.map0",std::string("THE FRAGGING YARD")},
		{"create.map1",std::string("SANDSTORM")},{"create.map2",std::string("THE LOST FLEET")},{"create.shot",std::string("gfx/guis/loadscreens/generic")},
		{"create.gametype",0.0},{"create.fraglimit",20.0},{"create.maxplayers",8.0}};
	Check(runtime.SetState(maps,error,0.4),"publish the page");
	Check(runtime.RunEvent("onActivate",0.5,effects,error),"the page arrives");
	runtime.Frame(viewport,0.575);
	Check(value("create-panel","opacity") > .3f && value("create-panel","opacity") < .7f && runtime.FocusedControl() == "create_start",
		"its content fades in over 150 ms and CREATE SERVER takes the focus");
	runtime.Frame(viewport,0.7);
	// The 4:3 canvas at 1280x720: x = 160 dp, 1 u = 1.5 dp.
	const float x0 = 160, u = 1.5f;
	const auto shot = bounds("create-shot-back"), list = bounds("maps-list"), name = bounds("create-name"), start = bounds("create_start");
	Check(Near(shot.x,x0+45*u,1) && Near(shot.y,75*u,1) && Near(shot.width,183*u,1) && Near(list.x,x0+42*u,1) && Near(list.y,219*u,1),
		"the levelshot and the map list stand where the stock page's do");
	Check(Near(name.x,x0+228*u,1) && Near(name.y,73*u,1) && Near(name.height,24*u,.5f) && Near(start.x,x0+244*u,1) && Near(start.y,397*u,1),
		"the options start at 228,73 u on the 24 u pitch and CREATE SERVER stands at 244,397 u");
	const auto lan = bounds("create-lan"), frag = bounds("create-fraglimit");
	Check(Near(lan.y,(73+24)*u,1) && Near(frag.y,(73+24*5)*u,1),"each option takes the next 24 u row");
	Check(text("map-1-name","text") == "SANDSTORM" && text("map-1-selected","display") == "block" && text("map-0-selected","display") == "none" &&
		text("map-3","display") == "none","the published maps fill the list with the chosen one lit");
	Check(text("crumb","text") == "#str_200002" && text("title","text") == "#str_200026" && Near(bounds("title").x,x0+136*u,1),
		"MULTIPLAYER is the crumb and CREATE SERVER titles the sub-page at the step's foot");
	Check(Near(value("band-top","transform",0),(323+97)*u,.05f),"the top band stands one notch on");
	// The game type picks the rows: DM shows Frag Limit and Team Damage,
	// locked with Auto Balance; CTF shows Capture Limit, unlocked; DeadZone
	// Control Time; Tourney its round limit in Team Damage's place.
	Check(text("create-fraglimit","display") == "block" && text("create-capturelimit","display") == "none" &&
		text("create-teamdamage","display") == "block" && text("create-tourneylimit","display") == "none" &&
		runtime.GetControlState("create-autobalance") == ControlState::Disabled &&
		runtime.GetControlState("create-teamdamage") == ControlState::Disabled,"Deathmatch: Frag Limit, and the team settings locked");
	Check(runtime.SetState({{"create.gametype",3.0}},error,0.8),"CTF");
	runtime.Frame(viewport,0.85);
	Check(text("create-fraglimit","display") == "none" && text("create-capturelimit","display") == "block" &&
		runtime.GetControlState("create-autobalance") != ControlState::Disabled,"CTF: Capture Limit, and Auto Balance applies");
	Check(runtime.SetState({{"create.gametype",5.0}},error,0.9),"DeadZone");
	runtime.Frame(viewport,0.95);
	Check(text("create-controltime","display") == "block" && text("create-capturelimit","display") == "none","DeadZone: Control Time");
	Check(runtime.SetState({{"create.gametype",1.0}},error,1.0),"Tourney");
	runtime.Frame(viewport,1.05);
	Check(text("create-tourneylimit","display") == "block" && text("create-teamdamage","display") == "none" &&
		text("create-fraglimit","display") == "block","Tourney: its round limit in Team Damage's place");
	// A map row's first Accept asks for its map; Accept on the chosen one moves
	// on to CREATE SERVER.
	Check(runtime.RunEvent("mapRow2",1.1,effects,error) && effects.actions.size() == 1 && effects.actions[0].action == "createMap",
		"a map row asks for its map");
	Check(runtime.RunEvent("mapRow1",1.2,effects,error) && effects.actions.empty() && runtime.FocusedControl() == "create_start",
		"Accept on the chosen map moves on to CREATE SERVER");
	// ADVANCED OPTIONS opens the stock popup's options, its contents at 200 ms.
	Check(runtime.RunEvent("advancedModalShow",1.3,effects,error),"ADVANCED OPTIONS opens its dialog");
	runtime.Frame(viewport,1.55);
	Check(text("advancedModal","display") == "block" && text("advancedModal-contents","display") == "block" &&
		text("create-bots","display") == "block" && text("create-mapcycle","display") == "block","the dialog shows the advanced options");
	// The password rows follow Use password, as the stock popup's do; Set
	// password dims and hides its text while Use password is off.
	const auto usepass = bounds("create-usepass"), password = bounds("create-password"), rcon = bounds("create-rconpassword");
	Check(Near(password.y,usepass.y+24*u,1) && Near(rcon.y,usepass.y+48*u,1) && Near(bounds("create-servermods").y,usepass.y+72*u,1),
		"Set password and the remote console password follow Use password");
	Check(runtime.GetControlState("create-password") == ControlState::Disabled && text("create-password-viewport","display") == "none" &&
		runtime.GetControlState("create-rconpassword") != ControlState::Disabled,"without Use password, Set password is locked and its text hidden");
	Check(runtime.SetState({{"create.usepass",true}},error,1.56),"Use password");
	runtime.Frame(viewport,1.57);
	Check(runtime.GetControlState("create-password") != ControlState::Disabled && text("create-password-viewport","display") == "block",
		"with Use password, Set password takes its text");
	Check(runtime.RunEvent("advancedModalHide",1.6,effects,error),"CLOSE ADVANCED OPTIONS");
	runtime.Frame(viewport,2.0);
	Check(text("advancedModal","display") == "none","the dialog closes");
	Check(runtime.RunEvent("onBack",2.1,effects,error) && effects.actions.size() == 1 && effects.actions[0].action == "back","Back asks to climb out");
	// Back: the title drops at once and the band steps back while the crumb
	// holds; the crumb returns over the last 150 ms; the page sweeps out.
	Check(runtime.RunEvent("subpageLeave",3,effects,error),"Back climbs one level");
	runtime.Frame(viewport,3.01);
	Check(Near(value("title","color",3),0,.01f) && value("band-top","transform",0) < (323+97)*u,"the title drops and the band steps back");
	runtime.Frame(viewport,3.3);
	Check(Near(value("band-top","transform",0),323*u,.05f) && Near(value("crumb","transform",2),1,.001f) &&
		Near(value("content","transform",0),-640*u,.5f) && Near(value("create-panel","opacity"),0,.01f),
		"by 300 ms the band is docked, the crumb is the title and the page has swept out");
	Check(runtime.RunEvent("onDeactivate",3.4,effects,error),"the page leaves the screen");
	runtime.Frame(viewport,3.45);
	Check(Near(value("title","color",3),.5f,.01f) && Near(value("content","transform",0),0,.05f) &&
		Near(value("band-top","transform",0),(323+97)*u,.05f) && Near(value("create-panel","opacity"),0,.01f),
		"hidden, it rests in the sub-page state with its content out");
}

// The prompt bar's glyphs follow prompt.glyphs (section 13.5): the keyboard's
// keycaps at 0, and at 1-5 each controller family's face button, 22 dp square
// and centred on its verb, the bar still ending where it did.
static void CheckPromptGlyphs(ScreenHost& host, const char* path) {
	const auto source = Read(path);
	std::vector<Diagnostic> diagnostics; std::string error;
	Runtime runtime(host);
	Check(runtime.Initialize() && runtime.LoadDocument(source,"guis/menu/title.q4ui",diagnostics),"the title loads for its prompts");
	Viewport viewport; viewport.canvasHeight = 720; viewport.width = 1280; viewport.height = 720;
	const char* const families[] = {"xbox","playstation","nintendo","steamdeck","generic"};
	const auto display = [&](const std::string& node) {
		const auto found = runtime.PresentedValue(node,"display"); return found ? found->text : std::string("?");
	};
	float end = -1;
	for (int glyphs = 0; glyphs <= 5; ++glyphs) {
		Check(runtime.SetState({{"prompt.glyphs",static_cast<double>(glyphs)}},error,1+glyphs),"publish the prompt glyphs");
		runtime.Frame(viewport,1.5+glyphs);
		for (const std::string id : {"prompt-231042","prompt-200013"}) {
			Check(display(id+"-cap") == (glyphs == 0 ? "block" : "none"),"the keycap shows only for the keyboard");
			for (int family = 0; family < 5; ++family)
				Check(display(id+"-"+families[family]) == (glyphs == family+1 ? "block" : "none"),"only the published family's button shows");
			Bounds glyph, verb;
			Check(runtime.GetBounds(id+"-glyph",glyph) && runtime.GetBounds(id+"-verb",verb),"the prompt is laid out");
			if (glyphs > 0) {
				Bounds face;
				Check(runtime.GetBounds(id+"-"+families[glyphs-1],face) && Near(face.width,22) && Near(face.height,22) &&
					Near(face.y+face.height/2,verb.y+verb.height/2,1),"a 22 dp face button centred on its verb");
			}
			Check(Near(verb.x,glyph.x+glyph.width+6,1),"the verb follows its glyph 6 dp on");
		}
		Bounds bar;
		Check(runtime.GetBounds("prompts",bar),"the bar is laid out");
		if (end < 0) end = bar.x+bar.width;
		Check(Near(bar.x+bar.width,end,1),"the bar ends where it did");
	}
	// Swap Confirm and Back: with confirm on east, SELECT shows each family's
	// east button and EXIT its south one, the same 22 dp.
	Check(runtime.SetState({{"prompt.swapped",true}},error,10),"confirm moves east");
	for (int glyphs = 1; glyphs <= 5; ++glyphs) {
		Check(runtime.SetState({{"prompt.glyphs",static_cast<double>(glyphs)}},error,10.1+glyphs),"publish the prompt glyphs");
		runtime.Frame(viewport,10.5+glyphs);
		const std::string family = families[glyphs-1];
		for (const std::string id : {"prompt-231042","prompt-200013"}) {
			Check(display(id+"-"+family) == "none" && display(id+"-"+family+"-swapped") == "block","the other position's button shows");
			Bounds face;
			Check(runtime.GetBounds(id+"-"+family+"-swapped",face) && Near(face.width,22) && Near(face.height,22),"at the same size");
		}
	}
}

// The Escape card's keys follow prompt.glyphs too (section 13.5): the strip's
// Q and E keycaps become the family's shoulders in the same 20x22 dp, and the
// prompt bar's Escape, Q, E and Enter its east button, shoulders and south
// button, the bar still inside the card.
static void CheckCardPromptGlyphs(ScreenHost& host, const char* path) {
	const auto source = Read(path);
	std::vector<Diagnostic> diagnostics; std::string error;
	Runtime runtime(host);
	Check(runtime.Initialize() && runtime.LoadDocument(source,"guis/menu/mp_escape.q4ui",diagnostics),"the card loads for its prompts");
	Viewport viewport; viewport.canvasHeight = 720; viewport.width = 1280; viewport.height = 720;
	const char* const families[] = {"xbox","playstation","nintendo","steamdeck","generic"};
	const auto display = [&](const std::string& node) {
		const auto found = runtime.PresentedValue(node,"display"); return found ? found->text : std::string("?");
	};
	const auto bounds = [&](const std::string& id) { Bounds b; Check(runtime.GetBounds(id,b),"a card key is laid out"); return b; };
	Bounds keyCap;
	for (int glyphs = 0; glyphs <= 5; ++glyphs) {
		Check(runtime.SetState({{"prompt.glyphs",static_cast<double>(glyphs)}},error,1+glyphs),"publish the prompt glyphs");
		runtime.Frame(viewport,1.5+glyphs);
		for (const std::string id : {"tab-previous","tab-next","prompt-back-0","prompt-tabs-0","prompt-tabs-1","prompt-select-0"}) {
			Check(display(id+"-cap") == (glyphs == 0 ? "block" : "none") && display(id+"-pad") == (glyphs == 0 ? "none" : "block"),
				"the keycap for the keyboard, the pad for a controller");
			for (int family = 0; family < 5; ++family)
				Check(display(id+"-"+families[family]) == (glyphs == family+1 ? "block" : "none"),"only the published family's button shows");
		}
		if (glyphs == 0) { keyCap = bounds("tab-next-cap"); continue; }
		const std::string family = families[glyphs-1];
		const auto shoulder = bounds("tab-next-"+family);
		Check(Near(shoulder.x,keyCap.x,.5f) && Near(shoulder.y,keyCap.y,.5f) && Near(shoulder.width,20) && Near(shoulder.height,22),
			"the strip's shoulder takes the keycap's 20x22 dp");
		for (const std::string id : {"prompt-back-0","prompt-tabs-0","prompt-tabs-1","prompt-select-0"}) {
			const auto glyph = bounds(id+"-"+family);
			Check(Near(glyph.width,22) && Near(glyph.height,22),"a 22 dp button in the prompt bar");
		}
		const auto card = bounds("card"), disconnect = bounds("prompt-disconnect");
		Check(disconnect.x+disconnect.width <= card.x+card.width-23,"the bar still ends inside the card");
	}
	// Swap Confirm and Back moves Resume's and Select's buttons, not the tabs'.
	Check(runtime.SetState({{"prompt.swapped",true},{"prompt.glyphs",3.0}},error,10),"Nintendo, confirm east");
	runtime.Frame(viewport,10.5);
	Check(display("prompt-back-0-nintendo-swapped") == "block" && display("prompt-select-0-nintendo-swapped") == "block" &&
		display("prompt-back-0-nintendo") == "none" && display("tab-next-nintendo") == "block","Resume and Select trade buttons");
}

// A page's prompt bar (section 13.4) in the docked band's raised trailing
// section, at the same canvas place in every view: Back's 45 dp target where
// the stock BACK plate's stood (532,441 u), its glyph 8 dp in and its verb
// 6 dp on, ending inside the 4:3 view, and SELECT ending 22 dp before it
// where Accept has something to do. Back's glyph follows prompt.glyphs; the
// pointer lights Back, whose click runs the page's Back, and passes over
// SELECT, a legend. With `back` empty both are legends (Single Player,
// Campaign and Multiplayer, whose own BACK is in the content). `bar` names
// the bar, `open` an event that shows it.
static void CheckPagePrompts(ScreenHost& host, const char* path, const char* name, const std::string& back, const char* action,
	const char* event, bool select, const char* open = nullptr, const std::string& bar = "prompts") {
	const auto source = Read(path);
	std::vector<Diagnostic> diagnostics; std::string error;
	Runtime runtime(host);
	const bool loaded = runtime.Initialize() && runtime.LoadDocument(source,name,diagnostics);
	for (const auto& diagnostic : diagnostics) std::fprintf(stderr,"%s %s: %s\n",name,diagnostic.pointer.c_str(),diagnostic.message.c_str());
	Check(loaded,"a page loads for its prompt bar");
	Runtime::EventEffects effects;
	if (open) Check(runtime.RunEvent(open,1,effects,error),"the page opens");
	const char* const families[] = {"xbox","playstation","nintendo","steamdeck","generic"};
	const auto display = [&](const std::string& node) {
		const auto found = runtime.PresentedValue(node,"display"); return found ? found->text : std::string("?");
	};
	const auto bounds = [&](const std::string& id) { Bounds b; Check(runtime.GetBounds(id,b),"a prompt is laid out"); return b; };
	const std::string glyphId = back.empty() ? "prompt-230045" : back;
	const std::string verbId = back.empty() ? "prompt-230045-verb" : back+"-label";
	Viewport viewport; viewport.canvasHeight = 720; viewport.height = 720;
	double now = 2;
	for (const int width : {1280,1680}) {
		viewport.width = width;
		runtime.Frame(viewport,now += .5);
		const float canvas = (width-960)*.5f;
		const auto glyph = bounds(glyphId+"-glyph"), verb = bounds(verbId);
		Check(Near(glyph.x,canvas+532*1.5f+8,.5f) && Near(glyph.y+glyph.height/2,441*1.5f+22.5f,.5f),"Back's glyph stands where the stock plate's target begins");
		Check(Near(verb.x,glyph.x+glyph.width+6,1) && verb.x+verb.width <= canvas+960-8,"its verb follows it and ends inside the 4:3 view");
		if (!back.empty()) {
			const auto target = bounds(back);
			Check(Near(target.x,canvas+532*1.5f,.5f) && Near(target.y,441*1.5f,.5f) && Near(target.height,45,.5f) &&
				Near(target.x+target.width,verb.x+verb.width+8,1),"Back's target is the stock plate's 45 dp, 8 dp past its glyph and verb");
		}
		Check((bounds(bar+"-lead").width > 0) == select,"SELECT shows where Accept has something to do");
		if (select) {
			const auto selectGlyph = bounds("prompt-231042-glyph"), selectVerb = bounds("prompt-231042-verb");
			Check(Near(selectVerb.x+selectVerb.width,glyph.x-22,1) && selectGlyph.x >= canvas+380*1.5f-.5f &&
				Near(selectGlyph.y,glyph.y,.5f),"SELECT ends 22 dp before Back, inside the raised section");
		}
	}
	for (int glyphs = 0; glyphs <= 5; ++glyphs) {
		Check(runtime.SetState({{"prompt.glyphs",static_cast<double>(glyphs)}},error,now += .1),"publish the prompt glyphs");
		runtime.Frame(viewport,now += .1);
		Check(display(glyphId+"-cap") == (glyphs == 0 ? "block" : "none"),"Back's keycap shows only for the keyboard");
		for (int family = 0; family < 5; ++family)
			Check(display(glyphId+"-"+families[family]) == (glyphs == family+1 ? "block" : "none"),"Back shows the published family's east button");
		if (glyphs > 0) {
			const auto face = bounds(glyphId+"-"+families[glyphs-1]);
			Check(Near(face.width,22) && Near(face.height,22),"a 22 dp button");
		}
	}
	Check(runtime.SetState({{"prompt.glyphs",0.0}},error,now += .1),"back to the keyboard");
	runtime.Frame(viewport,now += .1);
	if (back.empty()) {
		Check(!runtime.CanActivateControl("prompts-back",now) && !runtime.CanActivateControl("prompt-230045-glyph",now),"the legends take no input");
		return;
	}
	// The pointer over SELECT lights nothing; over Back it lights Back, and a
	// click runs the page's Back.
	if (select) {
		const auto legend = bounds("prompt-231042-verb");
		runtime.PointerMove(legend.x+legend.width/2,legend.y+legend.height/2,now += .1);
		runtime.Frame(viewport,now += .1);
		Check(runtime.GetControlState(back) == ControlState::Default,"SELECT is a legend");
		runtime.PointerButton(true,now); runtime.PointerButton(false,now += .02);
		Check(runtime.TakeActions().empty(),"a click on SELECT does nothing");
	}
	const auto target = bounds(back);
	runtime.PointerMove(target.x+target.width-3,target.y+target.height-3,now += .1);
	runtime.Frame(viewport,now += .1);
	const auto lit = runtime.PresentedValue(back+"-label","color");
	const auto glyphLit = runtime.PresentedValue(back+"-glyph","opacity");
	// Orange (#E38900) or the Strogg #FFCC00, where the rest is white or #FCFFC8.
	Check(runtime.GetControlState(back) == ControlState::Hover && lit && lit->data[0] > .85f && lit->data[2] < .1f && glyphLit &&
		Near(static_cast<float>(glyphLit->data[0]),1,.01f),"the pointer anywhere in the target lights Back and its glyph");
	runtime.PointerButton(true,now); runtime.PointerButton(false,now += .02);
	const auto actions = runtime.TakeActions();
	// Event names are canonical, in lower case.
	std::string canonical = event ? event : "";
	std::transform(canonical.begin(),canonical.end(),canonical.begin(),[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	Check(actions.size() == 1 && actions[0].node == back && (action ? actions[0].action == action : actions[0].event == canonical),
		"a click runs the page's Back");
}

// The controls under `node`, each with the nodes over it and the clipping
// groups among them.
struct ReachTarget { std::string id; std::vector<std::string> ancestors, clips; };
static void ReachTargets(const Node& node, std::vector<std::string>& ancestors, std::vector<std::string>& clips,
	std::vector<ReachTarget>& targets) {
	if (node.control) targets.push_back({node.id,ancestors,clips});
	const auto overflow = node.properties.find("overflow");
	const bool clipping = overflow != node.properties.end() && overflow->second.text != "visible";
	ancestors.push_back(node.id);
	if (clipping) clips.push_back(node.id);
	for (const auto& child : node.children) ReachTargets(child,ancestors,clips,targets);
	if (clipping) clips.pop_back();
	ancestors.pop_back();
}

// Pointer reach (the menu audit): the pointer at the centre of every control
// that can take input lights that control. Hit testing takes the topmost box
// under the pointer, so a full-size group later in a document takes the hits
// of every control under it, even at opacity 0; the keyboard, controllers and
// engine captures never hit-test, so only the pointer shows it. `values`
// publish the page (its rows shown, its plates enabled) and `arrive` brings it
// in; then each tab and up to `dialogs` of its dialogs (the Objectives page
// among them) are probed over the page at rest. A control a list clips out of
// view is reached by scrolling it in, and is left out. Returns the controls
// the pointer reached.
static std::set<std::string> CheckPointerReach(ScreenHost& host, const char* path, const char* name, const StateValues& values,
	const char* arrive, size_t dialogs = 64) {
	Document document; std::vector<Diagnostic> diagnostics;
	Check(document.Load(Read(path),diagnostics),"a page loads for the pointer probe");
	Runtime runtime(host);
	Check(runtime.Initialize() && runtime.LoadDocument(document,name,diagnostics),"the page loads into a runtime for the pointer probe");
	Viewport viewport; viewport.canvasHeight = 720; viewport.width = 1280; viewport.height = 720;
	std::string error; Runtime::EventEffects effects;
	double now = 1;
	Check(values.empty() || runtime.SetState(values,error,now),"publish the page for the pointer probe");
	std::vector<std::string> ancestors, clips; std::vector<ReachTarget> targets;
	ReachTargets(document.Model().root,ancestors,clips,targets);
	std::set<std::string> reached;
	std::vector<std::string> missed;
	const auto settle = [&](const std::string& event) {
		const bool ran = runtime.RunEvent(event,now += .1,effects,error);
		if (!ran) std::fprintf(stderr,"%s: %s did not run: %s\n",name,event.c_str(),error.c_str());
		Check(ran,"a pointer probe step runs");
		runtime.Frame(viewport,now += 1);   // every arrival, tab and dialog settles within a second
	};
	const auto probe = [&]() {
		// A focused field, list or slider keeps its focus under the pointer, a
		// focused button lets it go: hand the focus to a button first.
		const auto* focused = document.Model().FindNode(runtime.FocusedControl());
		if (focused && focused->control && focused->control->role != ControlRole::Button && focused->control->role != ControlRole::Toggle)
			for (const auto& target : targets)
				if (document.Model().FindNode(target.id)->control->role == ControlRole::Button && runtime.CanActivateControl(target.id,now) &&
					runtime.FocusControl(target.id,now)) break;
		const auto hidden = [&](const std::string& id) {
			const auto display = runtime.PresentedValue(id,"display"); return display && display->text == "none";
		};
		for (const auto& target : targets) {
			if (hidden(target.id) || std::any_of(target.ancestors.begin(),target.ancestors.end(),hidden)) continue;
			// The centre of what shows of it: in the view and inside every
			// clipping group over it (a plate may bleed off the view's edge).
			Bounds box;
			if (!runtime.GetBounds(target.id,box)) continue;
			float left = std::max(box.x,0.f), top = std::max(box.y,0.f);
			float right = std::min(box.x+box.width,static_cast<float>(viewport.width)), bottom = std::min(box.y+box.height,static_cast<float>(viewport.height));
			for (const auto& clip : target.clips) {
				Bounds view;
				if (!runtime.GetBounds(clip,view)) { right = left; break; }
				left = std::max(left,view.x); top = std::max(top,view.y);
				right = std::min(right,view.x+view.width); bottom = std::min(bottom,view.y+view.height);
			}
			if (right-left < 1 || bottom-top < 1 || !runtime.CanActivateControl(target.id,now)) continue;
			// The hit test runs as the pointer moves; the state is current at once.
			runtime.PointerMove((left+right)*.5f,(top+bottom)*.5f,now += .01);
			if (runtime.GetControlState(target.id) == ControlState::Hover) reached.insert(target.id);
			else missed.push_back(target.id);
		}
	};
	settle(arrive);
	probe();
	// Each tab, then each dialog or page that shows over the screen.
	for (const bool tabs : {true,false})
		for (const auto& [event,program] : document.Model().events) {
			if (tabs ? !event.starts_with("tab_") : !event.ends_with("show")) continue;
			if (!tabs && dialogs-- == 0) break;
			settle(event);
			probe();
			const auto hide = event.substr(0,event.size()-4)+"hide";
			if (!tabs && runtime.HasEvent(hide)) settle(hide);
		}
	for (const auto& id : missed) std::fprintf(stderr,"%s: the pointer at the centre of %s does not reach it\n",name,id.c_str());
	if (reached.empty()) std::fprintf(stderr,"%s: the pointer probe found no control to reach\n",name);
	Check(missed.empty() && !reached.empty(),"the pointer reaches every control at its centre");
	return reached;
}

// Every retained menu screen under the pointer probe. The pages are published
// so that their rows show and their plates are enabled, and the probe must
// reach the plates a later full-size group once covered: Load Game's, Restart
// Level's, Demos' and Save Game's.
static void CheckMenusPointerReach(ScreenHost& host, char** argv) {
	host.recordDraws = false;
	const auto probed = [](const std::set<std::string>& reached, std::initializer_list<const char*> ids) {
		bool all = true;
		for (const char* id : ids) if (!reached.contains(id)) { std::fprintf(stderr,"pointer probe: %s was not probed\n",id); all = false; }
		return all;
	};
	Check(probed(CheckPointerReach(host,argv[19],"guis/menu/loadgame.q4ui",{{"load.count",1.0},{"load.first",0.0},{"load.selected",0.0}},
		"onActivate"),{"row-0","load_go","load_delete","load_back","deleteModal_yes","deleteModal_no"}),
		"Load Game's plates, save, BACK and confirmation take the pointer");
	Check(probed(CheckPointerReach(host,argv[19],"guis/menu/loadgame.q4ui",{},"onActivate"),{"load_newgame","load_back"}),
		"the empty Load Game page's NEW GAME takes the pointer");
	Check(probed(CheckPointerReach(host,argv[20],"guis/menu/savegame.q4ui",{{"save.count",1.0},{"save.first",0.0},{"save.selected",0.0},
		{"save.name",std::string("Medic")}},"onActivate"),{"row-0","save-name","save_go","save_back","overwriteModal_yes","overwriteModal_no",
		"noticeModal_close"}),"Save Game's plate, name, save, BACK and dialogs take the pointer");
	Check(probed(CheckPointerReach(host,argv[21],"guis/menu/restartlevel.q4ui",{},"onActivate"),
		{"skill-0","skill-1","skill-2","skill-3","skill-4","restart_go","restart_back"}),"Restart Level's plates take the pointer");
	// The Strogg pause's pages: the same targets in the Strogg family.
	Check(probed(CheckPointerReach(host,StroggPage(argv[19]).c_str(),"guis/menu/loadgame_strogg.q4ui",{{"load.count",1.0},{"load.first",0.0},
		{"load.selected",0.0},{"load.ingame",true}},"onActivate"),{"row-0","load_go","load_delete","load_back","deleteModal_yes","deleteModal_no"}),
		"the Strogg Load Game's plates, save, BACK and confirmation take the pointer");
	Check(probed(CheckPointerReach(host,StroggPage(argv[20]).c_str(),"guis/menu/savegame_strogg.q4ui",{{"save.count",1.0},{"save.first",0.0},
		{"save.selected",0.0},{"save.name",std::string("Medic")}},"onActivate"),{"row-0","save-name","save_go","save_back","overwriteModal_yes",
		"overwriteModal_no","noticeModal_close"}),"the Strogg Save Game's plate, name, save, BACK and dialogs take the pointer");
	Check(probed(CheckPointerReach(host,StroggPage(argv[21]).c_str(),"guis/menu/restartlevel_strogg.q4ui",{},"onActivate"),
		{"skill-0","skill-1","skill-2","skill-3","skill-4","restart_go","restart_back"}),"the Strogg Restart Level's plates take the pointer");
	Check(probed(CheckPointerReach(host,argv[22],"guis/menu/demos.q4ui",{{"demos.count",1.0},{"demos.first",0.0},{"demos.selected",0.0},
		{"demos.canPlay",true},{"demos.canDelete",true}},"onActivate"),{"row-0","tag-mvd","tag-render","tag-legacy","tag-incomplete","demo_play",
		"demo_delete","demo_refresh","demo_back","deleteModal_yes","deleteModal_no"}),"Demos' plates, tags, demo, BACK and confirmation take the pointer");
	Check(probed(CheckPointerReach(host,argv[24],"guis/menu/credits.q4ui",{},"onActivate"),{"credits_back"}),"Credits' BACK takes the pointer");
	Check(probed(CheckPointerReach(host,argv[27],"guis/menu/createserver.q4ui",{{"create.map_count",3.0},{"create.map_first",0.0},
		{"create.map_selected",1.0}},"onActivate"),{"create_start","create_back","create-name","map-0","map-2"}),
		"Create Server's options, maps and dialogs take the pointer");
	Check(probed(CheckPointerReach(host,argv[28],"guis/menu/joingame.q4ui",{{"join.count",3.0},{"join.first",0.0},{"join.selected",0.0},
		{"join.listed",3.0}},"onActivate"),{"server-0","server-2","join_server","join_newlist","join_refresh","join_back"}),
		"Join Game's servers, actions and dialogs take the pointer");
	// The homes, sub-pages and cards: every control they show. The Escape
	// card's sixteen confirmations are one construction over the same card, and
	// each costs two of its 90 ms frames: one stands for them.
	CheckPointerReach(host,argv[1],"guis/menu/title.q4ui",{{"menu_continue",true}},"open");
	CheckPointerReach(host,argv[2],"guis/menu/pause.q4ui",{},"open");
	CheckPointerReach(host,argv[4],"guis/menu/pause_strogg.q4ui",{},"open");
	CheckPointerReach(host,argv[5],"guis/menu/singleplayer.q4ui",{},"onActivate");
	CheckPointerReach(host,argv[6],"guis/menu/campaigns.q4ui",{},"onActivate");
	CheckPointerReach(host,argv[7],"guis/menu/mp_escape.q4ui",{{"mp.action0.shown",true},{"mp.action0.available",true},
		{"mp.action1.shown",true},{"mp.action1.available",true}},"open",1);
	CheckPointerReach(host,argv[8],"guis/menu/mp_welcome.q4ui",{},"open");
	CheckPointerReach(host,argv[23],"guis/menu/restartmenu.q4ui",{},"onActivate");
	CheckPointerReach(host,argv[25],"guis/menu/arena.q4ui",{{"arena_loaded",true},{"arena_canStart",true},{"arena_difficulty",3.0},
		{"arenaCampaignProgress",5.0},{"arena_canReset",true}},"onActivate");
	CheckPointerReach(host,argv[26],"guis/menu/multiplayer.q4ui",{},"onActivate");
	host.recordDraws = true;
}

// The window's safe area (section 13.3) on a phone, upright and turned: the
// insets of a status bar and camera cutout, a gesture bar and rounded
// corners. The root is the safe area and the canvas fits it, so every text
// and control that shows (inside the lists that clip it) stands in the safe
// area, as do the page's dialogs, while the backdrops, scrims and soft focus
// still fill the view. With no inset the root and backdrops fill the view
// and the sources read nothing, and a view that loses its insets lays every
// part out as a twin that never had them, driven alike.
struct SafeShape { int width, height; float left, top, right, bottom; };
static void CheckSafeArea(ScreenHost& host, const char* path, const StateValues& values, size_t dialogs = 1) {
	std::string name = path;
	std::replace(name.begin(),name.end(),'\\','/');
	name = name.substr(name.rfind("guis/"));
	Document document; std::vector<Diagnostic> diagnostics;
	Check(document.Load(Read(path),diagnostics),"a menu loads for the safe-area probe");
	const auto& model = document.Model();
	Check(model.canvasHeight == 720 && model.canvasMinWidth > 0,"a safe-area menu keeps a view-height canvas");
	const std::string root = model.root.id;
	for (const char* view : {"safeLeft","safeTop","safeRight","safeBottom"}) {
		const auto found = model.state.find(std::string("view.")+view);
		Check(found != model.state.end() && found->second.view == view,"each safe inset is a view source");
	}
	// The backdrops that reach back out: every node but the root that the
	// generator anchored to the insets.
	std::vector<std::string> bleeds;
	bool framed = false;
	for (const auto& binding : model.bindings) {
		if (binding.id != "safe."+binding.node+".left") continue;
		if (binding.node == root) framed = true; else bleeds.push_back(binding.node);
	}
	Check(framed && !bleeds.empty(),"the root is the safe area and the backdrops reach out");
	std::vector<std::string> ancestors, clips; std::vector<ReachTarget> controls, texts;
	std::map<std::string,std::vector<std::string>> lineage;   // each node's ancestors
	ReachTargets(model.root,ancestors,clips,controls);
	const std::function<void(const Node&)> collect = [&](const Node& node) {
		if (node.type == "text") texts.push_back({node.id,ancestors,clips});
		lineage[node.id] = ancestors;
		const auto overflow = node.properties.find("overflow");
		const bool clipping = overflow != node.properties.end() && overflow->second.text != "visible";
		ancestors.push_back(node.id);
		if (clipping) clips.push_back(node.id);
		for (const auto& child : node.children) collect(child);
		if (clipping) clips.pop_back();
		ancestors.pop_back();
	};
	collect(model.root);
	// Each CVar source at its initial value, unless the host has the CVar.
	const auto savedCVars = host.cvars;
	for (const auto& [id,declaration] : model.state)
		if (!declaration.cvar.empty()) host.cvars.emplace(declaration.cvar,declaration.initial);
	// `runtime` takes the insets; `twin` is driven alike and never has them.
	Runtime runtime(host), twin(host); std::string error; Runtime::EventEffects effects;
	Check(runtime.Initialize() && runtime.LoadDocument(document,name,diagnostics) && twin.Initialize() && twin.LoadDocument(document,name,diagnostics),
		"the menu loads into a runtime and its twin for the safe-area probe");
	double now = 1;
	StateValues published = values;
	if (model.state.contains("notice.0.shown"))   // the top band's notices
		published.insert({{"notice.0.shown",true},{"notice.0.text",std::string("Saved")},{"notice.0.kind",1.0}});
	Check(published.empty() || (runtime.SetState(published,error,now) && twin.SetState(published,error,now)),"publish the menu for the safe-area probe");
	Viewport viewport; viewport.canvasHeight = static_cast<float>(model.canvasHeight); viewport.canvasMinWidth = static_cast<float>(model.canvasMinWidth);
	const auto frame = [&](double seconds) {
		Viewport plain = viewport; plain.safeLeft = plain.safeTop = plain.safeRight = plain.safeBottom = 0;
		runtime.Frame(viewport,seconds); twin.Frame(plain,seconds);
	};
	const auto run = [&](const std::string& event, double seconds) {
		return runtime.RunEvent(event,seconds,effects,error) && twin.RunEvent(event,seconds,effects,error);
	};
	const auto hidden = [](Runtime& view, const std::string& id) {
		const auto display = view.PresentedValue(id,"display");
		const auto opacity = view.PresentedValue(id,"opacity");
		return (display && display->text == "none") || (opacity && opacity->type == ValueType::Number && opacity->data[0] <= 0);
	};
	const auto concealed = [&](Runtime& view, const std::string& id, const std::vector<std::string>& over) {
		return hidden(view,id) || std::any_of(over.begin(),over.end(),[&](const auto& parent) { return hidden(view,parent); });
	};
	// What shows of `target`: its box inside every clipping group over it.
	const auto shown = [&](Runtime& view, const ReachTarget& target, Bounds& box) {
		if (concealed(view,target.id,target.ancestors) || !view.GetBounds(target.id,box)) return false;
		float left = box.x, top = box.y, right = box.x+box.width, bottom = box.y+box.height;
		for (const auto& clip : target.clips) {
			Bounds frame;
			if (!view.GetBounds(clip,frame)) return false;
			left = std::max(left,frame.x); top = std::max(top,frame.y);
			right = std::min(right,frame.x+frame.width); bottom = std::min(bottom,frame.y+frame.height);
		}
		box = {left,top,right-left,bottom-top};
		return box.width >= 1 && box.height >= 1;
	};
	const auto filled = [&](const std::string& id, float x, float y, float width, float height) {
		Bounds box;
		return runtime.GetBounds(id,box) && Near(box.x,x,1) && Near(box.y,y,1) && Near(box.width,width,1) && Near(box.height,height,1);
	};
	// A backdrop reaches every edge of the view (a montage under its parallax
	// transform reaches past them).
	const auto covers = [&](const std::string& id, float width, float height) {
		Bounds box;
		const bool reached = concealed(runtime,id,lineage.at(id)) ||
			(runtime.GetBounds(id,box) && box.x <= 1 && box.y <= 1 && box.x+box.width >= width-1 && box.y+box.height >= height-1);
		if (!reached) std::fprintf(stderr,"%s %gx%g: %s at %.1f,%.1f %.1fx%.1f does not reach the view's edges\n",name.c_str(),width,height,
			id.c_str(),box.x,box.y,box.width,box.height);
		return reached;
	};
	const auto source = [&](const char* id) {
		const auto state = runtime.GetState(true); const auto found = state.find(id);
		return found != state.end() && std::holds_alternative<double>(found->second) ? static_cast<float>(std::get<double>(found->second)) : NAN;
	};
	// A document that does not arrange itself by its shape declares no aspect.
	const bool shaped = model.state.contains("view.aspect");
	// Every text and every control that shows, inside the safe area. A plate
	// that runs off the view's edge (the navigation plates) has its label
	// checked instead.
	const auto inside = [&](const char* when) {
		const float left = viewport.safeLeft-1, top = viewport.safeTop-1;
		const float right = viewport.width-viewport.safeRight+1, bottom = viewport.height-viewport.safeBottom+1;
		bool all = true;
		for (const auto* list : {&texts,&controls}) for (const auto& target : *list) {
			Bounds box, whole;
			if (!shown(runtime,target,box) || !runtime.GetBounds(target.id,whole)) continue;
			if (list == &controls && (whole.x < -.5f || whole.y < -.5f || whole.x+whole.width > viewport.width+.5f ||
				whole.y+whole.height > viewport.height+.5f)) continue;
			if (box.x >= left && box.y >= top && box.x+box.width <= right && box.y+box.height <= bottom) continue;
			std::fprintf(stderr,"%s %dx%d %s: %s at %.1f,%.1f %.1fx%.1f leaves the safe area %.0f,%.0f-%.0f,%.0f\n",name.c_str(),viewport.width,
				viewport.height,when,target.id.c_str(),box.x,box.y,box.width,box.height,left+1,top+1,right-1,bottom-1);
			all = false;
		}
		return all;
	};
	const auto snapshot = [&](Runtime& view) {
		std::map<std::string,Bounds> boxes;
		for (const auto* list : {&texts,&controls}) for (const auto& target : *list) {
			Bounds box;
			if (shown(view,target,box)) boxes[target.id] = box;
		}
		return boxes;
	};
	for (const char* arrive : {"open","onActivate"}) if (runtime.HasEvent(arrive)) { Check(run(arrive,now += .1),"the menu arrives"); break; }
	for (const auto& shape : {SafeShape{540,1170,6,72,6,36},SafeShape{1170,540,72,0,30,24}}) {
		viewport.width = shape.width; viewport.height = shape.height;
		viewport.safeLeft = viewport.safeTop = viewport.safeRight = viewport.safeBottom = 0;
		frame(now += 1);   // every arrival settles within a second
		const float w = static_cast<float>(shape.width), h = static_cast<float>(shape.height);
		Check(Near(viewport.DpRatio(),std::min(h/720,w/static_cast<float>(model.canvasMinWidth)),.0001f),"with no inset the canvas fits the view");
		Check(filled(root,0,0,w,h),"with no inset the root fills the view");
		Check(std::all_of(bleeds.begin(),bleeds.end(),[&](const auto& id) { return covers(id,w,h); }),"with no inset the backdrops fill the view");
		Check(source("view.safeLeft") == 0 && source("view.safeTop") == 0 && source("view.safeRight") == 0 && source("view.safeBottom") == 0 &&
			(!shaped || Near(source("view.aspect"),w/h,.0001f)),"with no inset the sources read nothing and the view's aspect");
		viewport.safeLeft = shape.left; viewport.safeTop = shape.top; viewport.safeRight = shape.right; viewport.safeBottom = shape.bottom;
		frame(now += 1);
		const float safeWidth = w-shape.left-shape.right, safeHeight = h-shape.top-shape.bottom, dp = viewport.DpRatio();
		Check(Near(dp,std::min(safeHeight/720,safeWidth/static_cast<float>(model.canvasMinWidth)),.0001f),"the canvas fits the safe area");
		Check(Near(source("view.safeLeft"),shape.left/dp,.01f) && Near(source("view.safeTop"),shape.top/dp,.01f) &&
			Near(source("view.safeRight"),shape.right/dp,.01f) && Near(source("view.safeBottom"),shape.bottom/dp,.01f) &&
			(!shaped || Near(source("view.aspect"),safeWidth/safeHeight,.0001f)),"the sources read the insets in dp and the safe area's aspect");
		Check(filled(root,shape.left,shape.top,safeWidth,safeHeight),"the root is the safe area");
		Check(std::all_of(bleeds.begin(),bleeds.end(),[&](const auto& id) { return covers(id,w,h); }),"the backdrops still fill the view");
		Check(inside("page"),"every text and control stands in the safe area");
		size_t shownDialogs = 0;
		for (const auto& [event,program] : model.events) {
			if (shownDialogs == dialogs || !event.ends_with("modalshow")) continue;
			++shownDialogs;
			Check(run(event,now += .1),"a dialog shows");
			frame(now += 1);
			Check(inside(event.c_str()),"a dialog stands in the safe area");
			Check(std::all_of(bleeds.begin(),bleeds.end(),[&](const auto& id) { return covers(id,w,h); }),"a dialog's scrim fills the view");
			const auto hide = event.substr(0,event.size()-4)+"hide";
			if (runtime.HasEvent(hide)) { Check(run(hide,now += .1),"a dialog hides"); frame(now += 1); }
		}
		viewport.safeLeft = viewport.safeTop = viewport.safeRight = viewport.safeBottom = 0;
		frame(now += 1);
		const auto after = snapshot(runtime), still = snapshot(twin);
		for (const auto& [id,box] : after) if (!still.contains(id)) std::fprintf(stderr,"%s %dx%d: %s shows only after the insets\n",name.c_str(),
			shape.width,shape.height,id.c_str());
		Check(!still.empty() && after.size() == still.size() && std::all_of(still.begin(),still.end(),[&](const auto& entry) {
			const auto found = after.find(entry.first);
			const Bounds box = found != after.end() ? found->second : Bounds{-1,-1,-1,-1};
			const bool same = Near(box.x,entry.second.x,.5f) && Near(box.y,entry.second.y,.5f) && Near(box.width,entry.second.width,.5f) &&
				Near(box.height,entry.second.height,.5f);
			if (!same) std::fprintf(stderr,"%s %dx%d: %s stands at %.1f,%.1f %.1fx%.1f, not at %.1f,%.1f %.1fx%.1f\n",name.c_str(),shape.width,
				shape.height,entry.first.c_str(),box.x,box.y,box.width,box.height,entry.second.x,entry.second.y,entry.second.width,entry.second.height);
			return same;
		}),"a view that loses its insets lays every part out as one that never had them");
	}
	Check(host.errors == 0,"no safe-area diagnostics");
	host.cvars = savedCVars;
}

// The menus under the safe-area probe: a menu level and its dialogs (the
// title), the pause screen's chrome, a column page, a list page, a fixed
// page, the Arena, a settings hub and sub-page, a multiplayer card and the
// loading screen.
static void CheckSafeAreas(ScreenHost& host, char** paths, int count) {
	host.recordDraws = false;
	for (int i = 0; i < count; ++i) {
		const std::string path = paths[i];
		const auto named = [&](const char* file) { return path.ends_with(file); };
		StateValues values;
		if (named("title.q4ui")) values = {{"menu_continue",true}};
		else if (named("loadgame.q4ui")) values = {{"load.count",1.0},{"load.first",0.0},{"load.selected",0.0}};
		else if (named("demos.q4ui")) values = {{"demos.count",1.0},{"demos.first",0.0},{"demos.selected",0.0},{"demos.canPlay",true},{"demos.canDelete",true}};
		else if (named("arena.q4ui")) values = {{"arena_loaded",true},{"arena_canStart",true},{"arena_difficulty",3.0},{"arenaCampaignProgress",5.0},
			{"arena_canReset",true}};
		else if (named("joingame.q4ui")) values = {{"join.count",3.0},{"join.first",0.0},{"join.selected",0.0},{"join.listed",3.0}};
		CheckSafeArea(host,paths[i],values);
	}
	host.recordDraws = true;
}

#include "UiStatusBarChecks.inl"

#include "UiHudChecks.inl"
#include "UiRemasteredHudChecks.inl"
#include "UiRetainedSummaryChecks.inl"
// Each group of documents is its own meson test, so the suite runs the
// groups side by side: together the checks take about 35 s of CPU, most of
// it parsing and loading the documents (the 8 MB Escape card above all), and
// as one test they timed out on loaded machines. A group keeps its checks'
// order; with no group every check runs.
static void CheckSubtitles(ScreenHost& host, const char* path) {
	const auto source = Read(path);
	Document document; std::vector<Diagnostic> diagnostics;
	const bool valid = document.Load(source,diagnostics);
	for (const auto& d : diagnostics) std::fprintf(stderr,"subtitles %s: %s\n",d.pointer.c_str(),d.message.c_str());
	Check(valid && document.Model().id == "openq4.subtitles", "subtitle document validates");
	Runtime runtime(host);
	Check(runtime.Initialize() && runtime.LoadDocument(source,"guis/hud/subtitles.q4ui",diagnostics), "subtitle runtime loads");
	std::string error;
	for (const auto [width,height] : std::vector<std::pair<int,int>>{{1280,720},{1024,768},{3440,1440},{720,1280}}) {
		Viewport viewport; viewport.width=width; viewport.height=height; viewport.canvasHeight=720;
		const double panelWidth=std::min(1040.,720.*width/height*.84);
		for (const double size : {17.,34.}) for (const double lines : {1.,2.}) {
			Check(runtime.SetState({{"x",(720.*width/height-panelWidth)*.5},{"width",panelWidth},
				{"size",size},{"pitch",size*1.4},{"lines",lines},{"background",.6},
				{"line0",std::string("A caption.")},{"line1",std::string("A second caption.")},
				{"speaker0",std::string("Radio: ")},{"indent0",45.}},error,1),"subtitle state publishes");
			runtime.Frame(viewport,1);
			Bounds panel, first, second;
			Check(runtime.GetBounds("panel",panel)&&runtime.GetBounds("line0",first),"subtitle bounds exist");
			Check(panel.x>=0 && panel.x+panel.width<=width+.5 && panel.y>height*.5 && panel.y+panel.height<height*.85,
				"subtitle panel fits below the crosshair across aspects and scales");
			Check(first.x>=panel.x && first.x+first.width<=panel.x+panel.width+.5,"caption fits its backing");
			const auto display=runtime.PresentedValue("line1","display");
			Check(display && display->text==(lines==2 ? "block":"none"),"only requested lines display");
			if(lines==2) {
				Check(runtime.GetBounds("line1",second)&&second.y>=first.y+first.height-.5,"two speakers never overlap");
			}
			const auto aboveHidden=runtime.PresentedValue("abovePanel","display");
			Check(aboveHidden && aboveHidden->text=="none","the panel above the artwork stays hidden unless split");
		}
	}
	// Portrait cinematic at the largest text, two speakers: one line below the artwork and
	// one above it (canvas 720 high; the artwork spans 360 +/- 270 * aspect).
	{
		const int width=720, height=1280; const double aspect=double(width)/height, pitch=17*2*2*1.4;
		Viewport viewport; viewport.width=width; viewport.height=height; viewport.canvasHeight=720;
		const double panelWidth=720.*aspect*.84, scale=height/720.;
		Check(runtime.SetState({{"x",(720.*aspect-panelWidth)*.5},{"width",panelWidth},{"size",68.},{"pitch",pitch},
			{"lines",1.},{"bottom",360+270*aspect+24+pitch},{"line0",std::string("Older")},{"line1",std::string("")},
			{"aboveLines",1.},{"aboveBottom",360-270*aspect-12},{"aboveLine",std::string("Newer")},
			{"aboveSpeaker",std::string("")},{"aboveIndent",0.}},error,1),"split subtitle state publishes");
		runtime.Frame(viewport,1);
		Bounds panel, above;
		Check(runtime.GetBounds("panel",panel)&&runtime.GetBounds("abovePanel",above),"split subtitle bounds exist");
		const auto shown=runtime.PresentedValue("abovePanel","display");
		Check(shown && shown->text=="block","the panel above the artwork shows when split");
		Check(above.y>=0 && above.y+above.height<=(360-270*aspect)*scale+.5,"the upper caption sits above the artwork on screen");
		Check(panel.y>=(360+270*aspect)*scale-.5 && panel.y+panel.height<=height+.5,"the lower caption sits below the artwork on screen");
		Check(runtime.SetState({{"aboveLines",0.}},error,1),"split subtitle state clears");
	}
	Check(host.errors==0,"subtitle rendering has no diagnostics");
}

#include "UiRetainedRosterChecks.inl"

// Restart Level at larger text: the description grows under the plates in a
// tall view and beside them otherwise, RESTART LEVEL under it, over the
// prompt bar.
static TextScaleCase RestartTextScale(const char* path, const char* name) {
	return {path,name,{{"restart.skill",4.0}},{"onActivate"},
		{{{"skill-4","restart-description","restart_go","restart_back"},0,2},{{"restart-description","restart_go","restart_back"},1}},
		{},{"skill-0","skill-4","restart_go","restart_back"},{1.f,1.f,1.f}};
}

// A document beside another one: CHAPTERS beside the campaign's difficulty page.
static std::string Sibling(const char* path, const char* name) {
	const std::string text = path;
	const auto slash = text.find_last_of("/\\");
	return (slash == std::string::npos ? std::string() : text.substr(0,slash+1))+name;
}

// The single-player pages' remaining text scale (LAY-004): Restart Level, the
// campaign's difficulty, its CHAPTERS page, Credits and the Arena with its
// ledger, reset question and entrance.
static void CheckPagesTextScale(ScreenHost& host, char** argv) {
	CheckTextScaleCase(host,RestartTextScale(argv[21],"guis/menu/restartlevel.q4ui"));
	// The difficulty page grows a fixed amount, so it takes its column in
	// every portrait view that holds it (spec 1.36); CHAPTERS keeps 3:4.
	CheckTextScaleCase(host,{argv[13],"guis/menu/campaign_skill.q4ui",{{"skill",1.0},{"turbo",true}},{"onActivate"},
		{{{"back","description"},0,2},{{"description","prompts"},0,1}},{},{"skill0-label","start-label","back-label"},
		{1.f,1.f,.906f}});
	const std::vector<float> chaptersTall = {.886f,.7995f,.7653f};
	static const std::string chaptersPath = Sibling(argv[13],"chapters.q4ui");
	StateValues chapters{{"chapter.count",3.0},{"chapter.first",0.0},{"chapter.selected",0.0},{"chapter.open",true},
		{"chapter.name",std::string("AIR DEFENSE BUNKER")},
		{"chapter.detail",std::string("Starts from this level's autosave, with the arsenal and difficulty you reached it with.")}};
	for (int row = 0; row < 3; ++row) {
		chapters["chapter.row"+std::to_string(row)+".open"] = true;
		chapters["chapter.row"+std::to_string(row)+".name"] = std::string("Air Defense Bunker");
		chapters["chapter.row"+std::to_string(row)+".location"] = std::string("Reached");
	}
	CheckTextScaleCase(host,{chaptersPath.c_str(),"guis/menu/chapters.q4ui",chapters,{"onActivate"},
		{{{"chapter-shot-back","chapter-name","chapter-detail"}},{{"chapter-list","chapter_start-label"},1,1},
		 {{"chapter-list","chapter_back-label"},1,1}},
		{{"row-0-name","row-0"},{"row-0-location","row-0"}},{"chapter_start","chapter_back","row-0"},chaptersTall});
	// Credits: each screen's lines stand between the title and the prompt bar
	// and grow with the text, by no more than it.
	{
		const auto source = Read(argv[24]);
		Document document; std::vector<Diagnostic> diagnostics;
		Check(document.Load(source,diagnostics),"Credits validate for their text scale");
		std::vector<std::string> lines;
		std::vector<const Node*> pending{&document.Model().root};
		while (!pending.empty()) {
			const auto* node = pending.back(); pending.pop_back();
			if (node->type == "text" && node->id.starts_with("screen")) lines.push_back(node->id);
			for (const auto& child : node->children) pending.push_back(&child);
		}
		Check(!lines.empty(),"the credits' lines are found");
		TextScaleCase credits{argv[24],"guis/menu/credits.q4ui",{},{},{},{},{"credits_back"},{.8481f,.8481f,.8481f}};
		credits.textsOnScreen = false;
		CheckTextScaleCase(host,credits,
			[&](Runtime& runtime, const Viewport& view, float text, bool tall) {
				if (tall) return;
				Bounds title, bar; Check(runtime.GetBounds("credits-title",title) && runtime.GetBounds("prompts",bar),"Credits' chrome is laid out");
				for (const auto& id : lines) {
					Bounds box; Check(runtime.GetBounds(id,box),"a credits line is laid out");
					if (box.y < title.y+title.height-.6f || box.y+box.height > bar.y+.6f)
						std::fprintf(stderr,"credits %.1f %dx%d %s %.1f-%.1f\n",text,view.width,view.height,id.c_str(),box.y,box.y+box.height);
					Check(box.y >= title.y+title.height-.6f && box.y+box.height <= bar.y+.6f,"a credits line stands between the title and the prompt bar");
					const auto font = runtime.PresentedValue(id,"font-size");
					const float stock = float(document.Model().FindNode(id)->properties.at("font-size").data[0]);
					Check(font && float(font->data[0])*text >= stock-.01f && float(font->data[0])*text <= stock*text+.01f,
						"a credits line grows with the text, by no more than it");
				}
			});
	}
	StateValues arena{{"arena_loaded",true},{"arena_canStart",true},{"arena_canReset",true},{"arena_difficulty",3.0},
		{"arena_difficultyName",std::string("Hard")},{"arena_selectedTitle",std::string("The Edge")},
		{"arena_selectedDescription",std::string("A first taste of the arena against three bots on a tight map with no cover.")},
		{"arena_selectedMap",std::string("q4dm1")},{"arena_selectedGameType",std::string("Deathmatch")},
		{"arena_selectedRoster",std::string("You against three bots")},{"arena_selectedLimits",std::string("10 frags, 10 minutes")},
		{"arena_selectedSkill",std::string("Corporal")},{"arena_selectedBestVisible",true},{"arena_selectedHasBest",true},
		{"arena_selectedBest",std::string("10 - 4")},{"arena_selectedTierName",std::string("Tier 1")},
		{"arena_selectedTierDescription",std::string("The opening tier: three qualifiers and a boss on the smaller maps.")},
		{"arena_progress",std::string("3 of 20 matches")},{"arenaCampaignProgress",3.0},{"arena_tier0_selected",true},
		{"arena_match0_selected",true},{"arena_tier0_status",std::string("AVAILABLE")},{"arena_match0_status",std::string("AVAILABLE")}};
	const std::vector<std::string> arenaControls{"tier-0","tier-4","match-0","match-3","arena_easier","arena_harder","arena_start",
		"arena_back-label","arena_reset"};
	CheckTextScaleCase(host,{argv[25],"guis/menu/arena.q4ui",arena,{"onActivate"},
		{{{"arena-shot-back","arena-match-title","arena-match-description","arena-map","arena-gametype","arena-roster","arena-limits",
		   "arena-skill"}},{{"arena-skill","arena-difficulty-label"},0,1},{{"arena-difficulty-label","arena_easier","arena_start"}},
		 {{"arena_start","arena_back"},0,1},{{"arena-tier-name","arena-tier-description","arena-tier-path","arena-tier-rule"}},
		 {{"arena-progress","arena-progress-track","arena-complete","arena-reset-row"}}},
		{},arenaControls});
	StateValues ledger = arena;
	for (const auto& [key, value] : StateValues{{"arena_result",1.0},{"arena_resultTitle",std::string("VICTORY")},
		{"arena_resultMatchName",std::string("The Edge")},{"arena_resultMessage",std::string("You took the match with time to spare and three frags clear.")},
		{"arena_resultHasScore",true},{"arena_resultScore",std::string("10 - 7")},{"arena_resultHasUnlock",true},
		{"arena_resultUnlock",std::string("Qualifier 2")},{"arena_resultHasAwards",true},{"arena_resultAwardLine",std::string("Excellent, Impressive")},
		{"arena_resultCanRetry",true}}) ledger[key] = value;
	CheckTextScaleCase(host,{argv[25],"guis/menu/arena.q4ui",ledger,{"onActivate","arenaResultReveal"},
		{{{"result-title","result-match","result-message","result-score","result-unlock","result-awards","resultModal_no"}}},
		{{"result-awards","resultModal-dialog"},{"result-title","resultModal-dialog"}},{"resultModal_yes","resultModal_no"}});
	CheckTextScaleCase(host,{argv[25],"guis/menu/arena.q4ui",arena,{"onActivate","arenaResetReveal"},
		{{{"resetModal-body","resetModal_yes"}}},{{"resetModal-body","resetModal-dialog"}},{"resetModal_yes","resetModal_no"}});
	CheckTextScaleCase(host,{argv[25],"guis/menu/arena.q4ui",arena,{"onActivate","arenaMatchLaunch"},
		{{{"launch-kicker","launch-title","launch-gametype","launch-map","launch-ready"}}},
		{{"launch-title","launch-card"},{"launch-ready","launch-card"}},{"launch-card"}});
}

// The menus' remaining text scale: the title's MODS dialog and the pause's
// OBJECTIVES page, with its objectives and empty.
static void CheckMenusTextScale(ScreenHost& host, char** argv) {
	StateValues mods{{"mods.count",3.0},{"mods.selected",0.0},{"mods.author",std::string("DarkMatter Productions")},
		{"mods.date",std::string("11/03/2026")},{"mods.engine",std::string("openQ4 0.13")},{"mods.directory",std::string("baseoq4")}};
	for (int row = 0; row < 3; ++row) {
		mods["mods.row"+std::to_string(row)+".name"] = std::string("A modification");
		mods["mods.row"+std::to_string(row)+".version"] = std::string("1.0");
	}
	CheckTextScaleCase(host,{argv[1],"guis/menu/title.q4ui",mods,{"open","modsModalShow"},
		{{{"mods-list","mods-author","mods-date","mods-engine","mods-directory","modsModal_yes"}}},
		{{"mods-row-0-name","mods-row-0"},{"mods-directory","modsModal-dialog"},{"mods-author-label","modsModal-dialog"}},
		{"modsModal_yes","modsModal_no"},{1.f,1.f,1.f}});
	StateValues objectives{{"pause_objective_count",2.0},{"pause_completed_count",2.0}};
	for (int row = 0; row < 2; ++row) {
		objectives["pause_objective_"+std::to_string(row)] = std::string("Reach the hangar and open the bay doors");
		objectives["pause_objective_text_"+std::to_string(row)] =
			std::string("The bay doors open from the control room above the hangar floor. Strogg forces hold the stairs.");
		objectives["pause_completed_"+std::to_string(row)] = std::string("Find the medic");
	}
	CheckTextScaleCase(host,{argv[2],"guis/menu/pause.q4ui",objectives,{"open","objectivesShow"},
		{{{"objectives-entry-0","objectives-entry-1","objectives-completed","objectives-serial"}}},
		{{"objectives-entry-0-title","objectives-entry-0"},{"objectives-entry-0-text","objectives-entry-0"},{"objectives-done-0-title","objectives-done-0"}},
		{"objectives-list","objectives_back"},{.827f,.827f,.827f}});
	CheckTextScaleCase(host,{argv[2],"guis/menu/pause.q4ui",{},{"open","objectivesShow"},{},
		{{"objectives-empty-text","objectives-empty"}},{"objectives-list","objectives_back"},{.827f,.827f,.827f}});
}

// The multiplayer pages' remaining text scale: Create Server's options and
// its advanced dialog, Join Game's status, empty state and entry dialogs,
// and Player Setup, whose value stands under its label above 100 %.
static void CheckMultiplayerTextScale(ScreenHost& host, char** argv) {
	StateValues create{{"create.map_count",3.0},{"create.map_first",0.0},{"create.map_selected",0.0},{"create.gametype",0.0}};
	CheckTextScaleCase(host,{argv[27],"guis/menu/createserver.q4ui",create,{"onActivate"},
		{{{"create-gametype-head","create-gametype-value"},1},{{"create-name-head","create-name-viewport"},1},
		 {{"create-dedicated-head","create-dedicated-box"},1},{{"create-fraglimit-head","create-fraglimit-viewport"},1}},
		{{"create-gametype-value","create-gametype"},{"create-gametype-chevron","create-gametype"},{"create-dedicated-box","create-dedicated"},
		 {"create-fraglimit-viewport","create-fraglimit"},{"create-name-viewport","create-name"},{"create-name-head","create-name"}},
		{"create_start","create_advanced","create_back","create-options"}});
	CheckTextScaleCase(host,{argv[27],"guis/menu/createserver.q4ui",create,{"onActivate","advancedModalShow"},
		{{{"advanced-rows","advancedModal_close"}},{{"create-rate-head","create-rate-value"},1}},
		{{"create-rate-value","create-rate"},{"create-banlist-label","create-banlist"},{"create-banlist-chevron","create-banlist"},
		 {"advanced-rows","advancedModal-dialog"},{"create-mapcycle-viewport","create-mapcycle"}},{"advancedModal_close"}});
	StateValues join{{"join.count",0.0},{"join.listed",0.0},{"join.scanning",false},{"join.status",std::string("No servers found")}};
	CheckTextScaleCase(host,{argv[28],"guis/menu/joingame.q4ui",join,{"onActivate"},
		{{{"servers-list","join-gametype-value","join-status"},0,1},{{"join-empty-heading","join-empty","join_emptynew"}}},
		{},{"join_refresh","join_newlist","join_server","join_back"}});
	CheckTextScaleCase(host,{argv[28],"guis/menu/joingame.q4ui",join,{"onActivate","joinIpOpen"},
		{{{"join-ip-rows","join-yourip-value","joinIpModal_yes"}}},
		{{"join-address-viewport","join-address"},{"join-ip-rows","joinIpModal-dialog"},{"join-yourip-value","joinIpModal-dialog"}},
		{"joinIpModal_yes","joinIpModal_no"}});
	CheckTextScaleCase(host,{argv[28],"guis/menu/joingame.q4ui",join,{"onActivate","findOpen"},
		{{{"join-find-rows","findModal_yes"}},{{"join-find-head","join-find-viewport"},1}},
		{{"join-find-viewport","join-find"},{"join-find-head","join-find"},{"join-find-rows","findModal-dialog"}},{"findModal_yes","findModal_no"}});
	StateValues setup{{"setup.model_count",2.0},{"setup.model_row",0.0},{"setup.model.0.name",std::string("Kane")},
		{"setup.model.1.name",std::string("Voss")},{"setup.model_name",std::string("Kane")},{"setup.name",std::string("Player")},
		{"setup.notice",std::string("Saved")},{"setup.rail",2.0}};
	CheckTextScaleCase(host,{argv[26],"guis/menu/multiplayer.q4ui",setup,{"onActivate","setupOpen"},
		{{{"setup-name-head","setup-name-viewport"},1},{{"setup-model-head","setup-model-value"},1},
		 {{"setup-rotation-head","setup-rotation-track"},1},{{"setup-rail-label","setup-rail-0"},1},
		 {{"setup-heading","setup-fields","setup-notice","setup-current-model","prompts"},0,2}},
		{{"setup-name-viewport","setup-name"},{"setup-model-value","setup-model"},{"setup-rotation-track","setup-rotation"},
		 {"setup-rail-6","setup-rail"},{"setup-rail-label","setup-rail"}},{"setup-fields"},{1.f,1.f,1.f}});
}

int main(int argc, char** argv) {
 if (argc == 5 && std::string(argv[1]) == "--rosters") {
     ScreenHost host;
     CheckRosterCapacity(host,argv[2],"players","players",true);
     CheckRosterCapacity(host,argv[3],"players","players",false);
     CheckRosterCapacity(host,argv[4],"statistics","statistics",true);
     CheckRosterCapacity(host,argv[4],"scoreboard","board",false);
     std::printf("retained rosters: %d checks passed\n",checks); return 0;
 }
 if (argc == 3 && std::string(argv[1]) == "--subtitles") {
     ScreenHost host; CheckSubtitles(host,argv[2]);
     std::printf("subtitles: %d checks passed\n",checks); return 0;
 }
 if (argc == 3 && std::string(argv[1]) == "--view-effects") {
     ScreenHost host; char* paths[] = {nullptr,nullptr,argv[2]};
     CheckContinuity(host,paths,nullptr,true);
     Check(host.errors == 0,"no retained view-effect diagnostics");
     std::printf("retained view effects: %d checks passed\n",checks); return 0;
 }
 if (argc == 6 && std::string(argv[1]) == "--weapon-wheels") {
     ScreenHost host; CheckWeaponWheels(host,argv+2);
     Check(host.errors == 0,"no retained wheel diagnostics");
     std::printf("weapon wheels: %d checks passed\n",checks); return 0;
 }
 if (argc == 3 && std::string(argv[1]) == "--summary") {
     ScreenHost host; CheckSummary(host,argv[2]);
     std::printf("retained summary: %d checks passed\n",checks); return 0;
 }
 if (argc == 3 && std::string(argv[1]) == "--settings") {
     ScreenHost host; CheckSettingsPage(host,argv[2]);
     CheckColumnPageTextScale(host,argv[2],"guis/menu/settings.q4ui",{},{"defaultsModal"},
		{"description","summary-controls","summary-game","summary-display","summary-audio"});
     std::printf("retained SETTINGS page: %d checks passed\n",checks); return 0;
 }
 if (argc == 3 && std::string(argv[1]) == "--controls") {
     ScreenHost host; CheckControlsPage(host,argv[2]);
     std::printf("retained CONTROLS page: %d checks passed\n",checks); return 0;
 }
 if (argc == 3 && std::string(argv[1]) == "--game") {
     ScreenHost host; CheckGamePage(host,argv[2]);
     std::printf("retained GAME page: %d checks passed\n",checks); return 0;
 }
 if (argc == 3 && std::string(argv[1]) == "--audio") {
     ScreenHost host; CheckAudioPage(host,argv[2]);
     std::printf("retained AUDIO page: %d checks passed\n",checks); return 0;
 }
 if (argc >= 3 && std::string(argv[1]) == "--safe-area") {
     ScreenHost host; CheckSafeAreas(host,argv+2,argc-2);
     std::printf("retained safe area: %d checks passed\n",checks); return 0;
 }
 if (argc == 3 && std::string(argv[1]) == "--quick-chat") {
     ScreenHost host; CheckQuickChat(host,argv[2]);
     std::printf("quick chat: %d checks passed\n",checks); return 0;
 }
 if (argc >= 3 && std::string(argv[1]) == "--hud-bench") {
     for (int i = 2; i < argc; ++i) BenchRemasteredHud(argv[i]);
     return 0;
 }
 if (argc >= 3 && std::string(argv[1]) == "--hud-audio-bench") {
     for (int i = 2; i < argc; ++i) BenchHudAudio(argv[i]);
     return 0;
 }
 if (argc >= 3 && std::string(argv[1]) == "--remastered") {
     for (int i = 2; i < argc; ++i) {
         const std::string path = argv[i];
         const bool strogg = path.find("strogg") != std::string::npos, awakening = path.find("awakening") != std::string::npos;
         ScreenHost host;
         CheckRemasteredHudSafeArea(host,argv[i]);
         if (path.find("_mp") != std::string::npos) { CheckRemasteredMpHud(host,argv[i]); CheckRemasteredCompetitiveHud(host,argv[i]); continue; }
         if (path.find("_vehicle") != std::string::npos) { CheckRemasteredVehicleHud(host,argv[i]); continue; }
         if (path.find("_airdef") != std::string::npos) { CheckRemasteredAirdefHud(host,argv[i]); continue; }
         if (path.find("_chaingun_turret") != std::string::npos) { CheckRemasteredChaingunTurretHud(host,argv[i]); continue; }
         if (path.find("_turret") != std::string::npos) { CheckRemasteredTurretHud(host,argv[i]); continue; }
         if (path.find("_mcc") != std::string::npos) { CheckRemasteredMccHud(host,argv[i]); continue; }
         if (path.find("_speeder") != std::string::npos) { CheckRemasteredSpeederHud(host,argv[i]); continue; }
         if (path.find("_gev") != std::string::npos) { CheckRemasteredGevHud(host,argv[i]); continue; }
         if (path.find("_medchange") != std::string::npos) { CheckRemasteredMedchangeHud(host,argv[i]); continue; }
         if (path.find("_objectives") != std::string::npos) { CheckRemasteredObjectivesHud(host,argv[i],strogg); continue; }
         if (path.find("_scoreboard") != std::string::npos) { CheckRemasteredScoreboardHud(host,argv[i]); continue; }
         CheckRemasteredHud(host,argv[i],std::string("openq4.hud.remastered.")+(strogg ? "strogg" : "marine")+(awakening ? ".awakening" : ""),strogg);
     }
     std::printf("Remastered HUD: %d checks passed\n",checks); return 0;
 }
 if (argc == 2) { CheckHud(argv[1]); std::printf("HUD: %d checks passed\n",checks); return 0; }
 if (argc == 3) {
     ScreenHost host; CheckStatusBars(host,argv+1);
     Check(host.errors == 0,"no retained status diagnostics");
     std::printf("retained status bar: %d checks passed\n",checks); return 0;
 }
	Check(argc == 29 || argc == 30,"usage: title.q4ui pause.q4ui loading.q4ui pause_strogg.q4ui singleplayer.q4ui campaigns.q4ui mp_escape.q4ui mp_welcome.q4ui machinegun_scope.q4ui railgun_scope.q4ui nailgun_scope.q4ui initialization.q4ui campaign_skill.q4ui view_effects.q4ui weapon_wheel_marine.q4ui weapon_wheel_marine_awakening.q4ui weapon_wheel_strogg.q4ui weapon_wheel_strogg_awakening.q4ui loadgame.q4ui savegame.q4ui restartlevel.q4ui demos.q4ui restartmenu.q4ui credits.q4ui arena.q4ui multiplayer.q4ui createserver.q4ui joingame.q4ui [group]");
	const std::string group = argc == 30 ? argv[29] : "";
	bool known = group.empty();
	const auto runs = [&](const char* name) { const bool selected = group.empty() || group == name; known = known || selected; return selected; };
	if (runs("menus")) CheckSchema();
	ScreenHost host;
	if (runs("menus")) CheckTransformedClip(host);
	if (runs("menus")) CheckViewSource(host);
	if (runs("menus")) CheckPromptGlyphs(host,argv[1]);
	if (runs("mp-card")) CheckCardPromptGlyphs(host,argv[7]);
	if (runs("pages")) CheckPagePrompts(host,argv[19],"guis/menu/loadgame.q4ui","load_back","back",nullptr,true);
	if (runs("pages")) CheckPagePrompts(host,argv[20],"guis/menu/savegame.q4ui","save_back","back",nullptr,true);
	if (runs("pages")) CheckPagePrompts(host,argv[21],"guis/menu/restartlevel.q4ui","restart_back","back",nullptr,true);
	if (runs("strogg-pages")) CheckPagePrompts(host,StroggPage(argv[19]).c_str(),"guis/menu/loadgame_strogg.q4ui","load_back","back",nullptr,true);
	if (runs("strogg-pages")) CheckPagePrompts(host,StroggPage(argv[20]).c_str(),"guis/menu/savegame_strogg.q4ui","save_back","back",nullptr,true);
	if (runs("strogg-pages")) CheckPagePrompts(host,StroggPage(argv[21]).c_str(),"guis/menu/restartlevel_strogg.q4ui","restart_back","back",nullptr,true);
	if (runs("pages")) CheckPagePrompts(host,argv[22],"guis/menu/demos.q4ui","demo_back","back",nullptr,true);
	if (runs("pages")) CheckPagePrompts(host,argv[24],"guis/menu/credits.q4ui","credits_back","back",nullptr,false);
	if (runs("mp-pages")) CheckPagePrompts(host,argv[27],"guis/menu/createserver.q4ui","create_back","back",nullptr,true);
	if (runs("mp-pages")) CheckPagePrompts(host,argv[28],"guis/menu/joingame.q4ui","join_back","back",nullptr,true);
	if (runs("pause")) CheckPagePrompts(host,argv[2],"guis/menu/pause.q4ui","objectives_back",nullptr,"objectivesHide",false,"objectivesShow","objectives-prompts");
	if (runs("strogg-pause")) CheckPagePrompts(host,argv[4],"guis/menu/pause_strogg.q4ui","objectives_back",nullptr,"objectivesHide",false,"objectivesShow",
		"objectives-prompts");
	// The legends: Single Player, Campaign, the campaign difficulty page and
	// Multiplayer, whose own BACK is in the content.
	for (const auto& [page,name] : {std::pair{5,"guis/menu/singleplayer.q4ui"},std::pair{6,"guis/menu/campaigns.q4ui"},
		std::pair{13,"guis/menu/campaign_skill.q4ui"},std::pair{26,"guis/menu/multiplayer.q4ui"}})
		if (runs(page == 26 ? "mp-pages" : "pages")) CheckPagePrompts(host,argv[page],name,"",nullptr,nullptr,true);
	if (runs("pages")) CheckPagePrompts(host,Sibling(argv[13],"chapters.q4ui").c_str(),"guis/menu/chapters.q4ui","",nullptr,nullptr,true);
	if (runs("menus")) CheckMenuWrap(host,argv[1],argv[2],argv[21]);
	if (runs("menus")) CheckTitleTextScale(host,argv[1]);
	if (runs("pause")) CheckPauseTextScale(host,argv[2],"guis/menu/pause.q4ui");
	if (runs("strogg-pause")) CheckPauseTextScale(host,argv[4],"guis/menu/pause_strogg.q4ui");
	if (runs("menus")) CheckColumnPageTextScale(host,argv[5],"guis/menu/singleplayer.q4ui",{},{},{"description","summary-campaign","summary-arena"});
	if (runs("menus")) CheckColumnPageTextScale(host,argv[6],"guis/menu/campaigns.q4ui",{{"awakening_ready",true}},{},
		{"available","summary-newgame","summary-chapters"});
	// Title: canvas scaling, the lit field, the multiplied emblem and CONTINUE.
	if (runs("menus")) {
		const auto source = Read(argv[1]);
		Document document; std::vector<Diagnostic> diagnostics;
		if (!document.Load(source,diagnostics)) for (const auto& d : diagnostics) std::fprintf(stderr,"%u:%u %s %s\n",unsigned(d.line),unsigned(d.column),d.pointer.c_str(),d.message.c_str());
		Check(diagnostics.empty() && document.Model().id == "openq4.title" && document.Model().canvasHeight == 720,"title document validates");
		CheckSessionActions(document);
		Runtime runtime(host);
		Check(runtime.Initialize(),"retained runtime");
		Check(runtime.LoadDocument(source,"guis/menu/title.q4ui",diagnostics),"title loads into a runtime");
		Viewport viewport; viewport.canvasHeight = 720;
		runtime.Frame(viewport,1);
		Bounds label, focus;
		Check(runtime.GetBounds("nav_singleplayer-label",label),"navigation label laid out");
		// 1280x720: the 4:3 canvas starts at 160 px; labels at 44 u (66 dp).
		Check(Near(label.x,160+66),"labels sit at 44 u on the canvas at 16:9");
		Check(Additive(host),"the light band and focus light submit additive light");
		Check(std::any_of(host.draws.begin(),host.draws.end(),[&](const auto& draw) { return draw.material == host.multiply; }),
			"the emblem multiplies the lit field through the host material");
		Check(std::find(host.materials.begin(),host.materials.end(),"gfx/guis/mainmenu/level_mcc.dds") != host.materials.end(),"the backdrop montage loads its levelshots");
		Check(std::find(host.materials.begin(),host.materials.end(),"q4-add/gfx/guis/mainmenu/q4text") != host.materials.end(),"the wordmark is additive light");
		Bounds wordmarkBox;
		Check(runtime.GetBounds("wordmark",wordmarkBox) && Near(wordmarkBox.x,160+9) && Near(wordmarkBox.y,178.5f) &&
			Near(wordmarkBox.width,564) && Near(wordmarkBox.height,138),"the wordmark keeps q4text's 376x92 u box at 6,119 u");
		// Without a save CONTINUE collapses: SINGLE PLAYER takes the first row.
		const float firstRow = label.y;
		std::string error;
		Check(runtime.SetState({{"menu_continue",true},{"menu_continue_title",std::string("Air Defense Bunker")},
			{"menu_continue_shot",std::string("savegames/quick.tga")}},error,2),"publish the newest save");
		runtime.Frame(viewport,2.5);
		Check(runtime.GetBounds("nav_singleplayer-label",label) && Near(label.y-firstRow,45),"CONTINUE leads when a save exists");
		viewport.width = 1920; viewport.height = 1080;
		runtime.Frame(viewport,3);
		Check(runtime.GetBounds("nav_singleplayer-label",label) && Near(label.x,240+99) && Near(label.y,(firstRow+45)*1.5f,1.5f),
			"canvas follows the 1080 p view height");
		ActionInvocation invocation;
		Check(runtime.ResolveAction("continue",invocation,error) && std::get<std::string>(invocation.arguments.at("command")) == "continue",
			"CONTINUE requests the session's continue operation");
		Check(!runtime.SetState({{"menu_continue_shot",std::string("../escape.tga")}},error,4),"an unsafe picture source rejects the whole batch");
		Check(runtime.FocusControl("nav_settings",5),"keyboard focus reaches a navigation plate");
		runtime.Frame(viewport,5.5);
		Check(runtime.GetBounds("nav_settings-focus",focus),"focus rail laid out");
		Check(runtime.HasEvent("exitModalShow") && runtime.HasEvent("onBack"),"Back asks before leaving the game");
		// The emblem's glint turns once every 9 s under its wedge mask while
		// the rim inside turns back; reduced motion keeps only the rim.
		Runtime::EventEffects init;
		Check(runtime.RunEvent("onInit",6,init,error),"the title starts its ambient loops");
		runtime.Frame(viewport,6+4.5);
		const auto glint = runtime.PresentedValue("emblem-glint","transform");
		const auto rim = runtime.PresentedValue("emblem-glint-rim","transform");
		Check(glint && rim && Near(static_cast<float>(glint->data[4]),180,1) && Near(static_cast<float>(rim->data[4]),-180,1),
			"half way round at 4.5 s, the rim held in place");
		// Statistics are per frame, and the glint is the title's only mask.
		Check(runtime.Statistics().maskApplications == 1,"the wedge masks the glint");
		// A settled title draws its artwork from cache: only the turning wedge
		// tessellates again (its counter-turned rim stays put within float
		// jitter), and the zero-opacity rails, markers and lines take no layers.
		runtime.Frame(viewport,6+4.6);
		Check(runtime.Statistics().vectorPathsCompiled == 1,"a settled title tessellates only the turning wedge");
		Check(runtime.Statistics().layerElisions > 0,"zero-opacity controls take no composition layers");
		const auto shown = runtime.PresentedValue("emblem-glint","display");
		host.reducedMotion = true;
		runtime.Frame(viewport,11);
		const auto hidden = runtime.PresentedValue("emblem-glint","display");
		Check(shown && shown->text == "block" && hidden && hidden->text == "none" && runtime.Statistics().maskApplications == 0,
			"reduced motion hides the glint");
		host.reducedMotion = false;
		// Depth (section 13.8): the backdrop leans 6 dp away from the pointer and
		// the frame a third of that; reduced motion holds both still.
		Check(runtime.SetState({{"pointer_x",1.0},{"pointer_y",-0.5}},error,11.5),"publish the pointer");
		runtime.Frame(viewport,11.6);
		const auto backdrop = runtime.PresentedValue("depth-backdrop","transform");
		Check(backdrop && Near(static_cast<float>(backdrop->data[0]),-6,.01f) && Near(static_cast<float>(backdrop->data[1]),3,.01f) &&
			!runtime.PresentedValue("depth-frame","transform"),"the scene leans while vector furniture stays anchored");
		Check(runtime.SetState({{"pointer_x",.913},{"pointer_y",-.417}},error,11.61),"move scene by a fractional pixel");
		runtime.Frame(viewport,11.62);
		Check(runtime.Statistics().vectorPathsCompiled == 1,"pointer motion does not retessellate vector furniture");
		host.reducedMotion = true;
		runtime.Frame(viewport,11.7);
		const auto still = runtime.PresentedValue("depth-backdrop","transform");
		Check(still && Near(static_cast<float>(still->data[0]),0,.001f) && Near(static_cast<float>(still->data[1]),0,.001f),
			"reduced motion holds the layers still");
		host.reducedMotion = false;
		// Title carry (section 13.8): SETTINGS travels from its row into the
		// page's title slot at 39,19 u and settles at the 18 dp title size.
		// With a save shown its row is the fifth: 212.2 + 4 * 30 u.
		Runtime::EventEffects carry;
		Check(runtime.RunEvent("carry_settings",12,carry,error),"a page hand-off carries its label");
		runtime.Frame(viewport,12.002);
		const auto begin = runtime.PresentedValue("title-carry","transform");
		const auto carried = runtime.PresentedValue("title-carry","text");
		Check(begin && Near(static_cast<float>(begin->data[0]),7.5f,.1f) && Near(static_cast<float>(begin->data[1]),(212.2f+120+2.8f-19)*1.5f,.2f),
			"the carry starts on the chosen row");
		Check(carried && carried->text == "#str_200009","the carried title is the chosen item's label");
		runtime.Frame(viewport,12.6);
		const auto landed = runtime.PresentedValue("title-carry","transform");
		const auto visible = runtime.PresentedValue("title-carry","opacity");
		Check(landed && Near(static_cast<float>(landed->data[1]),-(1-.75f)*24.4f*1.5f/2,.1f) && Near(static_cast<float>(landed->data[2]),.75f,.001f) &&
			visible && Near(static_cast<float>(visible->data[0]),1,.001f),"it lands in the title slot at the title size");
		Runtime::EventEffects home;
		Check(runtime.RunEvent("returnHome",13,home,error) || runtime.PlayTimeline("returnHome",13),"back home");
		runtime.Frame(viewport,13.7);
		const auto gone = runtime.PresentedValue("title-carry","opacity");
		Check(gone && Near(static_cast<float>(gone->data[0]),0,.001f),"home starts without a carried title");
		// The exit confirmation (section 6) at 1080p, 1.5 px a dp: the stock
		// art's silhouette sits 6.5625 dp inside the glow column, the title
		// starts at the slot's foot, and its outline is eight copies extruded
		// 1 dp and composited once at 0.85, behind the fill. modal.enter brings
		// the scrim, glow and frame in over 200 ms and shows the contents at its
		// end; modal.leave hides them at once and closes the modal 300 ms later.
		// This host cannot soften the screen, so the scrim form stands in.
		{
			Runtime::EventEffects shown;
			Check(runtime.RunEvent("exitModalShow",14,shown,error),"the exit confirmation opens");
			runtime.Frame(viewport,14.1);
			const auto scrim = runtime.PresentedValue("exitModal-scrim","background-color");
			const auto frameIn = runtime.PresentedValue("exitModal-frame","opacity");
			const auto waiting = runtime.PresentedValue("exitModal-contents","display");
			Check(scrim && Near(static_cast<float>(scrim->data[3]),.47f,.02f) && frameIn && Near(static_cast<float>(frameIn->data[0]),.5f,.02f) &&
				waiting && waiting->text == "none","half way in, the scrim and frame rise while the contents wait");
			const auto fallbackFocus = runtime.PresentedValue("exitModal-softfocus","display");
			const auto column = runtime.PresentedValue("exitModal-glow","display");
			const auto cut = runtime.PresentedValue("exitModal-glow-soft","display");
			Check(fallbackFocus && fallbackFocus->text == "none" && column && column->text == "block" && cut && cut->text == "none" &&
				host.softened.empty() && runtime.Statistics().backdropFallbacks == 0,
				"without soft focus the scrim and the stock glow column stand in, and no backdrop pass runs");
			Check(runtime.FocusedControl().empty(),"focus waits for the contents");
			host.draws.clear(); host.compositeOpacities.clear();
			runtime.Frame(viewport,14.25);
			const auto appeared = runtime.PresentedValue("exitModal-contents","display");
			Check(appeared && appeared->text == "block" && runtime.FocusedControl() == "exitModal_no",
				"the contents appear together at 200 ms and NO takes focus");
			const auto dimmed = runtime.PresentedValue("wordmark","image-color");
			Check(dimmed && Near(static_cast<float>(dimmed->data[0]),.4f,.01f) && Near(static_cast<float>(dimmed->data[3]),1,.001f),
				"the Exit modal dims the wordmark to 40% gray");
			Bounds dialog, title, outline, left, below, body;
			Check(runtime.GetBounds("exitModal-dialog",dialog) && runtime.GetBounds("exitModal-title",title) &&
				runtime.GetBounds("exitModal-title-outline",outline) && runtime.GetBounds("exitModal-title-outline-1",left) &&
				runtime.GetBounds("exitModal-title-outline-4",below) && runtime.GetBounds("exitModal-body",body),"the confirmation is laid out");
			Check(Near(dialog.x,240+240*1.5f) && Near(dialog.width,480*1.5f),"the dialog is the stock 320 u rect");
			Check(Near(title.x-dialog.x,30.125f*1.5f) && Near(title.y-dialog.y,-4*1.5f),"the title hangs from the slot's foot");
			Check(Near(left.x-title.x,-1.5f,.01f) && Near(left.y-title.y,0,.01f) && Near(below.y-title.y,1.5f,.01f),
				"the outline copies extrude 1 dp");
			Check(outline.x <= left.x && outline.y <= title.y-1.5f && outline.x+outline.width >= title.x+title.width+1.5f,
				"the outline group encloses its copies, since its composite is clipped to it");
			Check(Near(body.y-dialog.y,71*1.5f) && Near(body.x-dialog.x,33*1.5f),"the body sits where the stock body does");
			Check(std::any_of(host.compositeOpacities.begin(),host.compositeOpacities.end(),[](float value) { return Near(value,.85f,.001f); }),
				"the outline composites once at 0.85");
			// Vector fills arrive as pixel coverage spans: the first fully covered
			// column and row start at the next pixel boundary past each edge.
			float silhouetteLeft = 1e9f, silhouetteTop = 1e9f;
			for (const auto& draw : host.draws) for (const auto& vertex : draw.vertices)
				if (!draw.material && vertex.r == 0 && vertex.g == 0 && vertex.b == 0 && Near(vertex.a,.7f,.005f) &&
					vertex.x >= dialog.x-1 && vertex.x <= dialog.x+dialog.width+1 && vertex.y >= dialog.y-1 && vertex.y <= dialog.y+dialog.height+1) {
					silhouetteLeft = std::min(silhouetteLeft,vertex.x); silhouetteTop = std::min(silhouetteTop,vertex.y);
				}
			Check(Near(silhouetteLeft,std::ceil(dialog.x+480*7/512.f*1.5f),.01f) && Near(silhouetteTop,std::ceil(dialog.y+3.5625f*1.5f),.01f),
				"the silhouette leaves the 6 dp lit margin and its raised top line");
			Runtime::EventEffects hidden;
			Check(runtime.RunEvent("exitModalHide",14.3,hidden,error),"the exit confirmation closes");
			runtime.Frame(viewport,14.32);
			const auto held = runtime.PresentedValue("exitModal-scrim","background-color");
			const auto gone = runtime.PresentedValue("exitModal-contents","display");
			const auto open = runtime.PresentedValue("exitModal","display");
			Check(gone && gone->text == "none" && open && open->text == "block" && held && Near(static_cast<float>(held->data[3]),.94f,.01f),
				"leaving hides the contents at once and holds the scrim for 50 ms");
			runtime.Frame(viewport,14.56);
			const auto frameOut = runtime.PresentedValue("exitModal-frame","opacity");
			const auto stillOpen = runtime.PresentedValue("exitModal","display");
			Check(frameOut && Near(static_cast<float>(frameOut->data[0]),0,.001f) && stillOpen && stillOpen->text == "block",
				"Exit releases its frame over 200 ms while the scrim is still going");
			const auto restoredMark = runtime.PresentedValue("wordmark","image-color");
			Check(restoredMark && Near(static_cast<float>(restoredMark->data[0]),1,.001f),"the wordmark is restored from 50 ms over 150 ms");
			runtime.Frame(viewport,14.61);
			const auto closed = runtime.PresentedValue("exitModal","display");
			Check(closed && closed->text == "none" && runtime.FocusedControl() != "exitModal_no","the modal closes when the scrim is gone");
			// A Hide just after enter ended (with no frame between) runs enter's
			// completion first, so the contents stay hidden. A Show while leaving
			// then takes over the leave, which never completes.
			Check(runtime.RunEvent("exitModalShow",15,shown,error) && runtime.RunEvent("exitModalHide",15.25,hidden,error),"hide after enter ended");
			runtime.Frame(viewport,15.27);
			const auto ordered = runtime.PresentedValue("exitModal-contents","display");
			Check(ordered && ordered->text == "none" && hidden.stateChanges.contains("exitModal.contents") &&
				!std::get<bool>(hidden.stateChanges.at("exitModal.contents")),"a completion due before an event runs before its program");
			Check(runtime.RunEvent("exitModalShow",15.4,shown,error),"reopen while leaving");
			runtime.Frame(viewport,15.7);
			const auto reopened = runtime.PresentedValue("exitModal","display");
			const auto restored = runtime.PresentedValue("exitModal-contents","display");
			Check(reopened && reopened->text == "block" && restored && restored->text == "block","a reopened modal stays open");
			Check(runtime.RunEvent("exitModalHide",16,hidden,error),"close again");
			runtime.Frame(viewport,16.4);
		}
		// Soft focus (REN-016): where the renderer softens the screen, the modal
		// blurs it 5 u (7.5 dp, 11.25 px at 1080p) at 0.80 saturation without
		// dimming it, ramping with modal.enter and released after leave's 50 ms
		// hold; the scrim and the stock column give way to the glow cut to the
		// dialog, whose side fade is the screen's second mask.
		{
			host.softFocus = true;
			Runtime::EventEffects shown, hidden;
			Check(runtime.RunEvent("exitModalShow",16.42,shown,error),"the exit confirmation opens over soft focus");
			host.softened.clear();
			runtime.Frame(viewport,16.52);
			const auto blur = runtime.PresentedValue("exitModal-softfocus","backdrop-blur");
			const auto saturate = runtime.PresentedValue("exitModal-softfocus","backdrop-saturate");
			Check(blur && Near(static_cast<float>(blur->data[0]),3.75f,.05f) && saturate && Near(static_cast<float>(saturate->data[0]),.9f,.005f),
				"half way in, the soft focus is at half strength");
			const auto focus = runtime.PresentedValue("exitModal-softfocus","display");
			const auto scrim = runtime.PresentedValue("exitModal-scrim","display");
			const auto column = runtime.PresentedValue("exitModal-glow","display");
			const auto cut = runtime.PresentedValue("exitModal-glow-soft","display");
			Check(focus && focus->text == "block" && scrim && scrim->text == "none" && column && column->text == "none" && cut && cut->text == "block",
				"soft focus replaces the scrim and the stock glow column");
			Check(host.softened.size() == 1 && Near(host.softened[0].sigma,3.75f*1.5f,.05f) && Near(host.softened[0].saturation,.9f,.005f),
				"the host softens the screen once a frame, the sigma in view pixels");
			const auto& region = host.softened[0].region;
			Check(region.x <= 0 && region.y <= 0 && region.x+region.width >= 1920 && region.y+region.height >= 1080,
				"the whole view beneath the modal is softened");
			Check(runtime.Statistics().backdropComposites == 1 && runtime.Statistics().backdropFallbacks == 0,"the backdrop pass is counted");
			runtime.Frame(viewport,16.64);
			Check(host.softened.size() == 2 && Near(host.softened[1].sigma,11.25f,.01f) && Near(host.softened[1].saturation,.8f,.001f),
				"modal.enter ends at a 7.5 dp blur and 0.80 saturation");
			Check(runtime.Statistics().maskApplications == 2,"the glow's side fade masks it, beside the emblem glint");
			Check(runtime.RunEvent("exitModalHide",16.65,hidden,error),"the exit confirmation closes");
			runtime.Frame(viewport,16.67);
			Check(host.softened.size() == 3 && Near(host.softened[2].sigma,11.25f,.01f),"leave holds the soft focus for 50 ms");
			runtime.Frame(viewport,16.825);
			Check(host.softened.size() == 4 && Near(host.softened[3].sigma,11.25f/2,.05f) && Near(host.softened[3].saturation,.9f,.005f),
				"and releases it over 250 ms");
			runtime.Frame(viewport,16.96);
			const auto closed = runtime.PresentedValue("exitModal","display");
			Check(closed && closed->text == "none" && host.softened.size() == 4 && runtime.Statistics().backdropComposites == 0,
				"a closed modal softens nothing");
			host.softFocus = false;
		}
		// content.out (section 8): departing home plates fade over 250 ms, but
		// the plinth and its secondary links take 50 ms; content.in brings
		// them back together over 150 ms after the bands return.
		{
			Check(runtime.PlayTimeline("depart",17),"depart toward a page");
			runtime.Frame(viewport,17.05);
			const auto plinth = runtime.PresentedValue("plinth","opacity");
			const auto homeOut = runtime.PresentedValue("home","opacity");
			Check(plinth && Near(static_cast<float>(plinth->data[0]),0,.001f) && homeOut && Near(static_cast<float>(homeOut->data[0]),.8f,.01f),
				"the plinth is gone at 50 ms while the home plates are still fading");
			Check(runtime.PlayTimeline("returnHome",18),"return home");
			runtime.Frame(viewport,18.7);
			const auto plinthBack = runtime.PresentedValue("plinth","opacity");
			const auto homeBack = runtime.PresentedValue("home","opacity");
			Check(plinthBack && Near(static_cast<float>(plinthBack->data[0]),1,.001f) && homeBack && Near(static_cast<float>(homeBack->data[0]),1,.001f),
				"the plinth returns with the home content");
		}
	}
	// Pause: the level block binds the mission, difficulty, objectives and shot.
	if (runs("pause")) {
		const auto source = Read(argv[2]);
		Document document; std::vector<Diagnostic> diagnostics;
		const bool valid = document.Load(source,diagnostics);
		for (const auto& diagnostic : diagnostics) std::fprintf(stderr,"pause %s: %s\n",diagnostic.pointer.c_str(),diagnostic.message.c_str());
		Check(valid && document.Model().id == "openq4.pause","pause document validates");
		CheckSessionActions(document);
		Runtime runtime(host);
		Check(runtime.Initialize() && runtime.LoadDocument(source,"guis/menu/pause.q4ui",diagnostics),"pause loads into a runtime");
		std::string error;
		Check(runtime.SetState({{"pause_level",std::string("Air Defense Trenches")},{"pause_shot",std::string("gfx/guis/loadscreens/airdefense2")}},error,1),
			"publish the level block");
		Viewport viewport; viewport.canvasHeight = 720;
		host.materials.clear();
		runtime.Frame(viewport,1.2);
		Check(std::find(host.materials.begin(),host.materials.end(),"gfx/guis/loadscreens/airdefense2") != host.materials.end(),"the level block shows the levelshot");
		const auto hidden = runtime.PresentedValue("level-objectives-head","display");
		Check(hidden && hidden->text == "none","a map without objectives shows no objectives heading");
		Check(runtime.SetState({{"pause_objectives",std::string("Reach the anti-aircraft battery.")}},error,1.3),"publish objectives");
		runtime.Frame(viewport,1.4);
		const auto shown = runtime.PresentedValue("level-objectives-head","display");
		Check(shown && shown->text == "block","objectives bring their heading");
		// The objectives the game publishes take the summary's place, a row
		// each, with the time in the mission under them (section 13.7).
		const auto stillHidden = runtime.PresentedValue("level-stats-rule","display");
		Check(stillHidden && stillHidden->text == "none","no time line before the game publishes one");
		Check(runtime.SetState({{"pause_objective_count",2.0},{"pause_objective_0",std::string("Destroy the anti-aircraft battery")},
			{"pause_objective_1",std::string("Reach the bunker")},{"pause_stats",std::string("0:42:10 in mission")}},error,1.5),
			"publish the open objectives and the time in the mission");
		runtime.Frame(viewport,1.6);
		const auto first = runtime.PresentedValue("level-objective-0","display");
		const auto second = runtime.PresentedValue("level-objective-1","display");
		const auto third = runtime.PresentedValue("level-objective-2","display");
		const auto summary = runtime.PresentedValue("level-objectives","display");
		const auto firstText = runtime.PresentedValue("level-objective-0-text","text");
		Check(first && first->text == "block" && second && second->text == "block" && third && third->text == "none",
			"one row for each open objective");
		Check(summary && summary->text == "none" && firstText && firstText->text == "Destroy the anti-aircraft battery",
			"the open objectives replace the map's summary, newest first");
		const auto stats = runtime.PresentedValue("level-stats","text");
		const auto rule = runtime.PresentedValue("level-stats-rule","display");
		Check(stats && stats->text == "0:42:10 in mission" && rule && rule->text == "block","the time in the mission shows under a rule");
		// The newest save's age takes the next line; the rule stays while
		// either line has text.
		Check(runtime.SetState({{"pause_stats",std::string()},{"pause_saved",std::string("saved 4 min ago")}},error,1.61),
			"publish only the save's age");
		runtime.Frame(viewport,1.62);
		const auto saved = runtime.PresentedValue("level-saved","text");
		const auto kept = runtime.PresentedValue("level-stats-rule","display");
		Check(saved && saved->text == "saved 4 min ago" && kept && kept->text == "block","the save's age shows under the rule");
		Bounds statsBox, savedBox, ruleBox, firstRow;
		Check(runtime.GetBounds("level-stats",statsBox) && runtime.GetBounds("level-saved",savedBox) &&
			runtime.GetBounds("level-stats-rule",ruleBox) && runtime.GetBounds("level-objective-0",firstRow),"the level block's foot is laid out");
		Check(savedBox.y >= statsBox.y+statsBox.height-.01f && statsBox.y >= ruleBox.y && firstRow.y+3*firstRow.height <= ruleBox.y,
			"the two time lines stack under the rule, below the third objective row");
		Check(runtime.SetState({{"pause_stats",std::string("0:42:10 in mission")}},error,1.63),"restore the time line");
		ActionInvocation invocation;
		Check(runtime.ResolveAction("resume",invocation,error) && std::get<std::string>(invocation.arguments.at("command")) == "resume","RESUME returns to the game");
		// SAVE GAME's label carries from its row at 212.2 u into the title slot.
		Runtime::EventEffects carry;
		Check(runtime.RunEvent("carry_saveGame",2,carry,error),"a pause page hand-off carries its label");
		runtime.Frame(viewport,2.002);
		const auto begin = runtime.PresentedValue("title-carry","transform");
		const auto carried = runtime.PresentedValue("title-carry","text");
		Check(begin && Near(static_cast<float>(begin->data[1]),(212.2f+2.8f-19)*1.5f,.2f) && carried && carried->text == "#str_200003",
			"SAVE GAME starts on its row");
		// The paused view (sections 9 and 13.7) at 1280x720, a pixel a dp:
		// without soft focus the darkening scrim stands in; with it the view
		// is softened by modal.softfocus, never dimmed, ramping in with
		// menu.fade, and the quit modal softens the pause screen above it.
		{
			runtime.Frame(viewport,2.5);
			const auto dimmed = runtime.PresentedValue("scrim","display");
			const auto plain = runtime.PresentedValue("scene-softfocus","display");
			Check(dimmed && dimmed->text == "block" && plain && plain->text == "none" && runtime.Statistics().backdropComposites == 0,
				"without soft focus the darkening scrim stands in for the softened view");
			host.softFocus = true;
			Check(runtime.PlayTimeline("open",3),"the pause fades in");
			host.softened.clear();
			runtime.Frame(viewport,3.125);
			const auto blur = runtime.PresentedValue("scene-softfocus","backdrop-blur");
			const auto scrim = runtime.PresentedValue("scrim","display");
			Check(blur && Near(static_cast<float>(blur->data[0]),3.75f,.05f) && scrim && scrim->text == "none",
				"the softening ramps in with menu.fade and replaces the darkening scrim");
			Check(host.softened.size() == 1 && Near(host.softened[0].sigma,3.75f,.05f) && Near(host.softened[0].saturation,.9f,.005f) &&
				host.softened[0].region.x <= 0 && host.softened[0].region.x+host.softened[0].region.width >= 1280,
				"the whole paused view is softened");
			runtime.Frame(viewport,3.4);
			Check(host.softened.size() == 2 && Near(host.softened[1].sigma,7.5f,.01f) && Near(host.softened[1].saturation,.8f,.001f),
				"at rest the paused view stays in modal.softfocus");
			Runtime::EventEffects quit;
			Check(runtime.RunEvent("quitModalShow",3.5,quit,error),"the quit confirmation opens");
			runtime.Frame(viewport,3.8);
			Check(host.softened.size() == 4 && Near(host.softened[2].sigma,7.5f,.01f) && Near(host.softened[3].sigma,7.5f,.01f) &&
				runtime.Statistics().backdropComposites == 2,"the quit modal softens the pause screen above the softened view");
			Check(runtime.RunEvent("quitModalHide",3.9,quit,error),"the quit confirmation closes");
			runtime.Frame(viewport,4.3);
			// The pause document outlives each pause: a page's return keeps the
			// view softened, and the next pause ramps in from nothing again.
			Check(runtime.PlayTimeline("returnHome",4.4),"a page returns home");
			runtime.Frame(viewport,4.5);
			const auto kept = runtime.PresentedValue("scene-softfocus","backdrop-blur");
			Check(kept && Near(static_cast<float>(kept->data[0]),7.5f,.01f),"returning from a page keeps the softened view");
			Check(runtime.PlayTimeline("open",5),"the game pauses again");
			runtime.Frame(viewport,5.125);
			const auto again = runtime.PresentedValue("scene-softfocus","backdrop-blur");
			Check(again && Near(static_cast<float>(again->data[0]),3.75f,.05f),"each pause ramps the softening in from nothing");
			// Returning to the game hides the screen at once (section 8) and
			// releases the softened view over 250 ms, as the modal backdrop
			// does (section 4).
			runtime.Frame(viewport,5.5);
			Runtime::EventEffects release;
			Check(runtime.RunEvent("release",5.6,release,error),"the game resumes");
			host.softened.clear();
			runtime.Frame(viewport,5.6);
			const auto closed = runtime.PresentedValue("chrome","display");
			Check(closed && closed->text == "none" && host.softened.size() == 1 && Near(host.softened[0].sigma,7.5f,.01f) &&
				runtime.Statistics().backdropComposites == 1,"the screen closes at once over the still-softened view");
			runtime.Frame(viewport,5.725);
			const auto releasing = runtime.PresentedValue("scene-softfocus","backdrop-blur");
			const auto resaturating = runtime.PresentedValue("scene-softfocus","backdrop-saturate");
			Check(releasing && Near(static_cast<float>(releasing->data[0]),3.75f,.05f) && resaturating &&
				Near(static_cast<float>(resaturating->data[0]),.9f,.005f),"the softened view releases over 250 ms");
			runtime.Frame(viewport,5.9);
			const auto clear = runtime.PresentedValue("scene-softfocus","backdrop-blur");
			const auto saturated = runtime.PresentedValue("scene-softfocus","backdrop-saturate");
			Check(clear && Near(static_cast<float>(clear->data[0]),0,.001f) && saturated && Near(static_cast<float>(saturated->data[0]),1,.001f),
				"and is clear by 250 ms");
			// Without soft focus the scrim standing in for it fades out instead,
			// and each pause opens with its screen and scrim again.
			host.softFocus = false;
			Check(runtime.RunEvent("open",6,release,error),"the game pauses again");
			runtime.Frame(viewport,6.01);
			const auto reopened = runtime.PresentedValue("chrome","display");
			const auto scrimBack = runtime.PresentedValue("scrim","opacity");
			Check(reopened && reopened->text == "block" && scrimBack && Near(static_cast<float>(scrimBack->data[0]),1,.001f),
				"every pause opens with its screen");
			Check(runtime.RunEvent("release",6.5,release,error),"the game resumes without soft focus");
			runtime.Frame(viewport,6.625);
			const auto fading = runtime.PresentedValue("scrim","opacity");
			Check(fading && Near(static_cast<float>(fading->data[0]),.5f,.01f),"the scrim fades out over 250 ms");
			runtime.Frame(viewport,6.8);
			const auto gone = runtime.PresentedValue("scrim","opacity");
			Check(gone && Near(static_cast<float>(gone->data[0]),0,.001f),"and is gone by 250 ms");
			Check(runtime.RunEvent("open",7,release,error),"the game pauses once more");
			runtime.Frame(viewport,7.3);
			const auto restored = runtime.PresentedValue("scrim","opacity");
			const auto screen = runtime.PresentedValue("chrome","display");
			Check(restored && Near(static_cast<float>(restored->data[0]),1,.001f) && screen && screen->text == "block",
				"the scrim and screen return with the pause");
			// The pause also comes back through returnHome (a page left with Back
			// before the legacy menu reached home): activation shows the screen
			// and ramps the softened view back in.
			host.softFocus = true;
			Check(runtime.RunEvent("release",7.5,release,error),"the game resumes from a page's return");
			runtime.Frame(viewport,7.8);
			Check(runtime.RunEvent("onActivate",8,release,error) && runtime.PlayTimeline("returnHome",8),"the pause returns through returnHome");
			runtime.Frame(viewport,8.01);
			const auto back = runtime.PresentedValue("chrome","display");
			const auto backScrim = runtime.PresentedValue("scrim","opacity");
			Check(back && back->text == "block" && backScrim && Near(static_cast<float>(backScrim->data[0]),1,.001f),
				"a pause activated after a release shows its screen");
			runtime.Frame(viewport,8.3);
			const auto resoftened = runtime.PresentedValue("scene-softfocus","backdrop-blur");
			Check(resoftened && Near(static_cast<float>(resoftened->data[0]),7.5f,.01f),"and softens the view again");
			// An activation that follows no release changes nothing.
			Check(runtime.RunEvent("onActivate",8.4,release,error),"the pause activates again");
			runtime.Frame(viewport,8.5);
			const auto steady = runtime.PresentedValue("scene-softfocus","backdrop-blur");
			Check(steady && Near(static_cast<float>(steady->data[0]),7.5f,.01f),"without a release the softened view stays");
			host.softFocus = false;
		}
		// OBJECTIVES (sections 13.7 and 14.11) is a page of the pause: it docks
		// the bands and carries its label, then the stock entry motion shows
		// every open objective, newest first, with its description and
		// screenshot, and the completed ones under COMPLETED. Back fades the
		// page and closes it when the bands are home.
		{
			const auto objectivesRow = runtime.PresentedValue("nav_objectives-label","text");
			Check(objectivesRow && objectivesRow->text == "#str_200380","OBJECTIVES is the fifth action");
			Bounds restartRow, objectivesBox;
			Check(runtime.GetBounds("nav_restart-label",restartRow) && runtime.GetBounds("nav_objectives-label",objectivesBox) &&
				Near(objectivesBox.y-restartRow.y,45),"OBJECTIVES follows RESTART LEVEL on the 30 u pitch");
			std::string longText; for (int word = 0; word < 120; ++word) longText += "squad ";
			Check(runtime.SetState({{"pause_objective_count",3.0},{"pause_objective_0",std::string("Destroy the battery")},
				{"pause_objective_text_0",std::string("Strogg forces hold the hangar.")},
				{"pause_objective_shot_0",std::string("gfx/objectives/airdefense_obj_1")},
				{"pause_objective_1",std::string("Reach the bunker")},{"pause_objective_text_1",longText},
				{"pause_objective_2",std::string("Regroup with Rhino squad")},
				{"pause_completed_count",1.0},{"pause_completed_0",std::string("Retrieve the medic")}},error,10),
				"publish the objectives page");
			Runtime::EventEffects page;
			host.materials.clear();
			Check(runtime.RunEvent("objectivesShow",10.1,page,error),"OBJECTIVES opens its page");
			runtime.Frame(viewport,10.2);
			const auto open = runtime.PresentedValue("objectives","display");
			const auto waiting = runtime.PresentedValue("objectives-content","opacity");
			const auto carried = runtime.PresentedValue("title-carry","text");
			const auto docking = runtime.PresentedValue("band-top","transform");
			Check(open && open->text == "block" && waiting && Near(static_cast<float>(waiting->data[0]),0,.001f) &&
				carried && carried->text == "#str_200380" && docking && docking->data[0] > 1,
				"the bands dock and OBJECTIVES carries before the page appears");
			runtime.Frame(viewport,11.0);
			const auto shown = runtime.PresentedValue("objectives-content","opacity");
			const auto barIn = runtime.PresentedValue("objectives-bar","transform");
			const auto prompts = runtime.PresentedValue("prompts","opacity");
			Check(shown && Near(static_cast<float>(shown->data[0]),1,.001f) && barIn && Near(static_cast<float>(barIn->data[0]),0,.01f) &&
				prompts && Near(static_cast<float>(prompts->data[0]),0,.001f),"by 900 ms the page is in with its back bar, and the prompts give way");
			const auto first = runtime.PresentedValue("objectives-entry-0","display");
			const auto third = runtime.PresentedValue("objectives-entry-2","display");
			const auto fourth = runtime.PresentedValue("objectives-entry-3","display");
			const auto empty = runtime.PresentedValue("objectives-empty","display");
			const auto done = runtime.PresentedValue("objectives-done-0","display");
			const auto notDone = runtime.PresentedValue("objectives-done-1","display");
			const auto count = runtime.PresentedValue("objectives-completed-count","text");
			Check(first && first->text == "block" && third && third->text == "block" && fourth && fourth->text == "none" &&
				empty && empty->text == "none" && done && done->text == "block" && notDone && notDone->text == "none" && count && count->text == "1",
				"a plate for each open objective and a row for each completed one");
			const auto title = runtime.PresentedValue("objectives-entry-0-title","text");
			const auto text = runtime.PresentedValue("objectives-entry-0-text","text");
			Check(title && title->text == "Destroy the battery" && text && text->text == "Strogg forces hold the hangar.","the plates carry each objective's title and description");
			Bounds titleBox;
			Check(runtime.GetBounds("objectives-entry-0-title",titleBox) && Near(titleBox.width,308*1.5f,1),"a title wraps at the stock 308 u");
			Check(std::find(host.materials.begin(),host.materials.end(),"gfx/objectives/airdefense_obj_1") != host.materials.end(),
				"the plates show each objective's screenshot");
			// Descriptions wrap without a limit, so a long one grows its plate.
			Bounds shortPlate, longPlate, frame;
			Check(runtime.GetBounds("objectives-entry-0",shortPlate) && runtime.GetBounds("objectives-entry-1",longPlate) &&
				runtime.GetBounds("objectives-entry-1-frame",frame) && Near(shortPlate.height,132*1.5f,1) &&
				longPlate.height > shortPlate.height+30 && Near(frame.height,longPlate.height,1),"a long description grows its plate and frame");
			// The list scrolls: the bar holds focus, and the down input moves it.
			Check(runtime.FocusedControl() == "objectives_scroll","the page opens with its list's scrollbar in focus");
			const auto before = runtime.GetWidgetState("objectives_scroll");
			Check(before && before->scroll && before->scroll->geometry.usable && before->scroll->geometry.range > 0,"the list overflows and can scroll");
			runtime.MenuAction(MenuInput::Down,true,11.1); runtime.MenuAction(MenuInput::Down,false,11.12);
			runtime.Frame(viewport,11.2);
			const auto after = runtime.GetWidgetState("objectives_scroll");
			Check(after && after->scroll && after->scroll->geometry.offset > before->scroll->geometry.offset,"the down input scrolls the list");
			// A snapshot restored with the page open keeps its scroll: the restored
			// page is open already, not newly opened.
			{
				std::string saved; std::vector<Diagnostic> restoredDiagnostics;
				Check(runtime.SaveSnapshot(saved,error,11.2),"save the open Objectives page");
				Runtime restored(host);
				Check(restored.Initialize() && restored.LoadDocument(source,"guis/menu/pause.q4ui",restoredDiagnostics) &&
					restored.RestoreSnapshot(saved,error,11.2),"restore it into a fresh runtime");
				restored.Frame(viewport,11.21); restored.Frame(viewport,11.22);
				const auto kept = restored.GetWidgetState("objectives_scroll");
				Check(kept && kept->scroll && Near(static_cast<float>(kept->scroll->geometry.offset),static_cast<float>(after->scroll->geometry.offset),.5f),
					"a restored page keeps its scroll");
			}
			// Back: the page fades, the bands come home, and the page closes.
			Check(runtime.RunEvent("objectivesHide",12,page,error),"Back leaves the page");
			runtime.Frame(viewport,12.2);
			const auto fading = runtime.PresentedValue("objectives-content","opacity");
			const auto still = runtime.PresentedValue("objectives","display");
			Check(fading && Near(static_cast<float>(fading->data[0]),0,.001f) && still && still->text == "block","the page fades while the bands return");
			Check(runtime.RunEvent("objectivesHide",12.3,page,error),"a second Back during the leave");
			runtime.Frame(viewport,12.8);
			const auto closed = runtime.PresentedValue("objectives","display");
			const auto back = runtime.PresentedValue("prompts","opacity");
			Check(closed && closed->text == "none" && back && Near(static_cast<float>(back->data[0]),1,.001f) &&
				runtime.FocusedControl() != "objectives_scroll","the page closes with the bands home and the prompts back, the second Back ignored");
			// Reopened, the list starts at its first entry again.
			Check(runtime.RunEvent("objectivesShow",12.9,page,error),"OBJECTIVES opens again");
			runtime.Frame(viewport,13.8);
			const auto reopened = runtime.GetWidgetState("objectives_scroll");
			Check(reopened && reopened->scroll && Near(static_cast<float>(reopened->scroll->geometry.offset),0,.01f),"a reopened page starts at the top of its list");
			// A pause that closed without Back (a console load) opens at home
			// without the page, its prompts showing.
			Check(runtime.RunEvent("open",14,page,error),"the game pauses again");
			runtime.Frame(viewport,14.3);
			const auto reset = runtime.PresentedValue("objectives","display");
			const auto resetBar = runtime.PresentedValue("objectives-bar","display");
			const auto resetPrompts = runtime.PresentedValue("prompts","opacity");
			Check(reset && reset->text == "none" && resetBar && resetBar->text == "none" && resetPrompts &&
				Near(static_cast<float>(resetPrompts->data[0]),1,.001f) && runtime.FocusedControl() != "objectives_scroll",
				"every pause opens without the Objectives page");
			// No open objective: the stock static screenshot and line in one plate.
			Check(runtime.SetState({{"pause_objective_count",0.0},{"pause_completed_count",0.0}},error,14.5),"no objectives");
			host.materials.clear();
			Check(runtime.RunEvent("objectivesShow",14.6,page,error),"OBJECTIVES opens on an empty list");
			runtime.Frame(viewport,15.5);
			const auto none = runtime.PresentedValue("objectives-empty","display");
			const auto noneDone = runtime.PresentedValue("objectives-completed","display");
			Bounds noneText;
			Check(none && none->text == "block" && noneDone && noneDone->text == "none" &&
				std::find(host.materials.begin(),host.materials.end(),"gfx/objectives/none") != host.materials.end() &&
				runtime.GetBounds("objectives-empty-text",noneText) && Near(noneText.width,164*1.5f,1),
				"without objectives the page shows the stock static and line");
			// The level block lists the objectives with their state: the open ones,
			// then the completed ones behind a check at 0.5.
			Check(runtime.SetState({{"pause_objective_count",1.0},{"pause_objective_0",std::string("Escort Anderson")},
				{"pause_completed_count",2.0},{"pause_completed_0",std::string("Retrieve the medic")},
				{"pause_completed_1",std::string("Regroup")}},error,16),"one open and two completed objectives");
			runtime.Frame(viewport,16.1);
			const auto openRow = runtime.PresentedValue("level-objective-0-text","text");
			const auto openMark = runtime.PresentedValue("level-objective-0-mark","display");
			const auto doneRow = runtime.PresentedValue("level-objective-1-text","text");
			const auto doneCheck = runtime.PresentedValue("level-objective-1-check","display");
			const auto doneMark = runtime.PresentedValue("level-objective-1-mark","display");
			const auto doneColour = runtime.PresentedValue("level-objective-1-text","color");
			const auto lastRow = runtime.PresentedValue("level-objective-2-text","text");
			Check(openRow && openRow->text == "Escort Anderson" && openMark && openMark->text == "block" &&
				doneRow && doneRow->text == "Retrieve the medic" && doneCheck && doneCheck->text == "block" && doneMark && doneMark->text == "none" &&
				doneColour && Near(static_cast<float>(doneColour->data[3]),.5f,.001f) && lastRow && lastRow->text == "Regroup",
				"the level block lists the open objective, then the completed ones behind a check");
		}
	}
	// Strogg pause (section 13.7): the Marine pause's level block and verbs in
	// the Strogg family, every label arriving in runes and translating.
	if (runs("strogg-pause")) {
		const auto source = Read(argv[4]);
		Document document; std::vector<Diagnostic> diagnostics;
		const bool valid = document.Load(source,diagnostics);
		for (const auto& diagnostic : diagnostics) std::fprintf(stderr,"strogg pause %s: %s\n",diagnostic.pointer.c_str(),diagnostic.message.c_str());
		Check(valid && document.Model().id == "openq4.pause_strogg","the Strogg pause validates");
		CheckSessionActions(document);
		Runtime runtime(host);
		Check(runtime.Initialize() && runtime.LoadDocument(source,"guis/menu/pause_strogg.q4ui",diagnostics),"the Strogg pause loads into a runtime");
		std::string error;
		Check(runtime.SetState({{"pause_level",std::string("Strogg Medical Facilities")},{"pause_shot",std::string("gfx/guis/loadscreens/medlabs")},
			{"pause_objective_count",1.0},{"pause_objective_0",std::string("Escape the medical facility")},
			{"pause_stats",std::string("0:18:40 in mission")}},error,1),"publish the level block");
		Viewport viewport; viewport.canvasHeight = 720;
		host.materials.clear();
		runtime.Frame(viewport,1.1);
		const auto face = runtime.PresentedValue("nav_resume-label","font-family");
		const auto runeFace = runtime.PresentedValue("nav_resume-rune","font-family");
		Check(face && face->text == "r_strogg" && runeFace && runeFace->text == "strogg","labels are R_Strogg over a rune copy");
		const auto strogStats = runtime.PresentedValue("level-stats","text");
		const auto strogSaved = runtime.PresentedValue("level-saved","text");
		Check(strogStats && strogStats->text == "0:18:40 in mission" && strogSaved && strogSaved->text.empty(),
			"the Strogg level block keeps the Marine block's two time lines");
		const auto rowRunes = runtime.PresentedValue("level-objective-0-text-rune","text");
		const auto nameRunes = runtime.PresentedValue("level-name-rune","text");
		Check(rowRunes && rowRunes->text == "Escape the medical facility" && nameRunes && nameRunes->text == "Strogg Medical Facilities",
			"the rune copies follow the published lines");
		Check(std::find(host.materials.begin(),host.materials.end(),"gfx/guis/loadscreens/medlabs") != host.materials.end(),
			"the level block shows the levelshot");
		Check(std::find(host.materials.begin(),host.materials.end(),"q4-add/gfx/guis/hud/s_static") != host.materials.end(),
			"the level block adds the Strogg HUD's grain");
		// The label's wrapper fits the label, so the scan bar's head rests
		// 12 u past it whatever the language: the test face advances 0.6 of
		// the 24 dp size per glyph, and labels read "Label".
		Bounds fit, labelBox, bar;
		Check(runtime.GetBounds("nav_resume-fit",fit) && runtime.GetBounds("nav_resume-label",labelBox) &&
			runtime.GetBounds("nav_resume-scan",bar),"the Strogg plate is laid out");
		Check(Near(labelBox.width,5*.6f*24) && Near(fit.width,labelBox.width+26*1.5f) && Near(bar.x,fit.x) && Near(bar.width,fit.width),
			"the wrapper fits the label and the scan bar spans it");
		Check(Near(labelBox.x,160+66),"Strogg labels sit at 44 u like the Marine ones");
		// Opening: the actions translate 120 + 45 ms per row apart over 200 ms,
		// EXIT after them (390 ms), GAME PAUSED from 60 ms over 500 ms under
		// the scan bar, arriving white, and the level block's lines last (435 ms).
		Check(runtime.HasEvent("open") && runtime.HasEvent("returnHome"),"opening and returning play the translation");
		Runtime::EventEffects open;
		Check(runtime.RunEvent("open",2,open,error),"the Strogg pause opens");
		runtime.Frame(viewport,2.05);
		const auto resumeIn = runtime.PresentedValue("nav_resume-latin","opacity");
		const auto resumeRunes = runtime.PresentedValue("nav_resume-rune","color");
		Check(resumeIn && Near(static_cast<float>(resumeIn->data[0]),0,.001f) && resumeRunes && Near(static_cast<float>(resumeRunes->data[3]),.8f,.001f),
			"RESUME arrives in runes");
		runtime.Frame(viewport,2.33);
		const auto resumeDone = runtime.PresentedValue("nav_resume-latin","opacity");
		const auto resumeGone = runtime.PresentedValue("nav_resume-rune","color");
		const auto quitWaits = runtime.PresentedValue("nav_quit-latin","opacity");
		const auto heading = runtime.PresentedValue("level-heading","color");
		Check(resumeDone && Near(static_cast<float>(resumeDone->data[0]),1,.001f) && resumeGone && Near(static_cast<float>(resumeGone->data[3]),0,.001f),
			"RESUME has translated by 320 ms");
		const auto exitWaits = runtime.PresentedValue("link_exit-latin","opacity");
		Check(quitWaits && Near(static_cast<float>(quitWaits->data[0]),0,.001f) && heading && Near(static_cast<float>(heading->data[3]),0,.001f) &&
			exitWaits && Near(static_cast<float>(exitWaits->data[0]),0,.001f),"QUIT TO MENU, EXIT and the level block wait their turn");
		const auto titleMid = runtime.PresentedValue("paused-title","color");
		const auto scanMid = runtime.PresentedValue("paused-title-scan","opacity");
		Check(titleMid && Near(static_cast<float>(titleMid->data[0]),1,.001f) && Near(static_cast<float>(titleMid->data[3]),.54f,.02f) &&
			scanMid && Near(static_cast<float>(scanMid->data[0]),.46f,.02f),"GAME PAUSED fades in white while the scan bar dims");
		runtime.Frame(viewport,3.2);
		const auto titleRest = runtime.PresentedValue("paused-title","color");
		const auto headingRest = runtime.PresentedValue("level-heading","color");
		const auto rowRest = runtime.PresentedValue("level-objective-0-text","color");
		const auto quitDone = runtime.PresentedValue("nav_quit-latin","opacity");
		Check(titleRest && Near(static_cast<float>(titleRest->data[1]),1,.001f) && Near(static_cast<float>(titleRest->data[2]),200/255.f,.002f) &&
			Near(static_cast<float>(titleRest->data[3]),1,.002f),"GAME PAUSED settles to #FCFFC8");
		const auto exitDone = runtime.PresentedValue("link_exit-latin","opacity");
		const auto exitRunes = runtime.PresentedValue("link_exit-rune","color");
		Check(exitDone && Near(static_cast<float>(exitDone->data[0]),1,.001f) && exitRunes && Near(static_cast<float>(exitRunes->data[3]),0,.001f),
			"EXIT has translated");
		Check(headingRest && Near(static_cast<float>(headingRest->data[3]),1,.001f) && rowRest && Near(static_cast<float>(rowRest->data[3]),1,.001f) &&
			quitDone && Near(static_cast<float>(quitDone->data[0]),1,.001f),"every line has translated by 1.2 s");
		// Focus runs the credits scan bar under the label: its head sweeps in
		// from the wrapper's leading edge over 300 ms, by its right inset, so
		// it crosses any label whole, and the bar dims over a second.
		Check(runtime.FocusControl("nav_savegame",4),"focus reaches a Strogg plate");
		runtime.Frame(viewport,4.002);
		Bounds parked, half, rest;
		Check(runtime.GetBounds("nav_savegame-fit",fit) && runtime.GetBounds("nav_savegame-scan",parked) &&
			parked.x+parked.width <= fit.x+2,"the bar's head starts at the label wrapper's leading edge");
		runtime.Frame(viewport,4.15);
		const auto sliding = runtime.PresentedValue("nav_savegame-scan","right");
		const auto lit = runtime.PresentedValue("nav_savegame-scan","opacity");
		Check(sliding && sliding->unit == "%" && sliding->data[0] > 1 && sliding->data[0] < 99 && lit && lit->data[0] > .8 &&
			runtime.GetBounds("nav_savegame-scan",half) && half.x+half.width > fit.x+2 && half.x+half.width < fit.x+fit.width-2,
			"the scan bar sweeps across the focused label");
		runtime.Frame(viewport,5.2);
		const auto rested = runtime.PresentedValue("nav_savegame-scan","right");
		const auto dimmed = runtime.PresentedValue("nav_savegame-scan","opacity");
		Check(rested && Near(static_cast<float>(rested->data[0]),0,.01f) && dimmed && Near(static_cast<float>(dimmed->data[0]),0,.001f) &&
			runtime.GetBounds("nav_savegame-scan",rest) && Near(rest.x+rest.width,fit.x+fit.width),
			"the bar rests 12 u past the label and dims away");
		const auto focusColour = runtime.PresentedValue("nav_savegame-label","color");
		Check(focusColour && Near(static_cast<float>(focusColour->data[0]),1,.001f) && Near(static_cast<float>(focusColour->data[1]),.8f,.002f) &&
			Near(static_cast<float>(focusColour->data[2]),0,.001f),"a focused label turns #FFCC00");
		// Reduced motion shows the translated labels at once.
		runtime.SetReducedMotion(true,6);
		Check(runtime.RunEvent("open",6,open,error),"the Strogg pause opens again with reduced motion");
		runtime.Frame(viewport,6.09);
		const auto quitAtOnce = runtime.PresentedValue("nav_quit-latin","opacity");
		const auto runesGone = runtime.PresentedValue("nav_quit-rune","color");
		const auto titleAtOnce = runtime.PresentedValue("paused-title","color");
		Check(quitAtOnce && Near(static_cast<float>(quitAtOnce->data[0]),1,.001f) && runesGone && Near(static_cast<float>(runesGone->data[3]),0,.001f) &&
			titleAtOnce && Near(static_cast<float>(titleAtOnce->data[3]),1,.002f),"reduced motion shows the translated labels at once");
		runtime.SetReducedMotion(false,6.1);
		// The paused view softens as the Marine pause's does.
		host.softFocus = true; host.softened.clear();
		Check(runtime.RunEvent("open",7,open,error),"the Strogg pause opens over the softened view");
		runtime.Frame(viewport,7.4);
		Check(!host.softened.empty() && Near(host.softened.back().sigma,7.5f,.01f) && Near(host.softened.back().saturation,.8f,.001f),
			"the Strogg pause rests in modal.softfocus");
		host.softFocus = false;
		ActionInvocation invocation;
		Check(runtime.ResolveAction("resume",invocation,error) && std::get<std::string>(invocation.arguments.at("command")) == "resume",
			"RESUME returns to the game");
		// The Strogg OBJECTIVES page keeps the construction in the family's colors
		// and returns through the translation.
		Runtime::EventEffects page;
		Check(runtime.RunEvent("objectivesShow",20,page,error),"the Strogg OBJECTIVES page opens");
		runtime.Frame(viewport,20.9);
		const auto strogged = runtime.PresentedValue("objectives-heading","font-family");
		const auto serial = runtime.PresentedValue("objectives-serial","text");
		Check(strogged && strogged->text == "r_strogg" && serial && serial->text == "#str_200280","an R_Strogg heading and the STROGG NET serial");
		const auto backFace = runtime.PresentedValue("objectives_back-label","font-family");
		const auto backColour = runtime.PresentedValue("objectives_back-label","color");
		Check(backFace && backFace->text == "r_strogg" && backColour && Near(static_cast<float>(backColour->data[0]),0xFC/255.f,.002f) &&
			Near(static_cast<float>(backColour->data[2]),0xC8/255.f,.002f),"BACK is a Strogg plate with its label in R_Strogg");
		Check(runtime.RunEvent("objectivesHide",21,page,error),"Back leaves the Strogg page");
		runtime.Frame(viewport,21.05);
		const auto translating = runtime.PresentedValue("nav_resume-latin","opacity");
		Check(translating && Near(static_cast<float>(translating->data[0]),0,.001f),"returning plays the translation again");
		// The quit confirmation asks in the Strogg family: R_Strogg labels over
		// rune copies on Strogg plates, arriving in runes as the contents show
		// at modal.enter's end and translating, the title over 300 ms and the
		// actions 45 ms apart after it.
		const auto opacity = [&](const char* node, const char* property, int channel) {
			const auto value = runtime.PresentedValue(node,property); return value ? static_cast<float>(value->data[channel]) : -1.f;
		};
		const auto family = [&](const char* node) { const auto value = runtime.PresentedValue(node,"font-family"); return value ? value->text : std::string(); };
		Check(document.Model().FindNode("quitModal-title-rune") && document.Model().FindNode("quitModal_yes-rune") &&
			document.Model().FindNode("exitModal_no-rune") && runtime.HasEvent("quitModalShow") && runtime.HasEvent("exitModalShow"),
			"both confirmations carry rune copies");
		Runtime::EventEffects modal;
		Check(runtime.RunEvent("quitModalShow",30,modal,error),"QUIT TO MENU asks first");
		runtime.Frame(viewport,30.1);
		Check(Near(opacity("quitModal-title-latin","opacity",0),0,.001f) && Near(opacity("quitModal-title-rune","color",3),.8f,.001f) &&
			Near(opacity("quitModal_no-latin","opacity",0),0,.001f),"the hidden contents wait in runes");
		Check(family("quitModal-title") == "r_strogg" && family("quitModal-title-rune") == "strogg" && family("quitModal-body") == "r_strogg" &&
			family("quitModal_yes-label") == "r_strogg" && family("quitModal_no-rune") == "strogg","the dialog sets R_Strogg over runes");
		runtime.Frame(viewport,30.21);
		const auto shownContents = runtime.PresentedValue("quitModal-contents","display");
		Check(shownContents && shownContents->text == "block" && opacity("quitModal-title-latin","opacity",0) < .1f &&
			opacity("quitModal-title-rune","color",3) > .7f,"the contents show at modal.enter's end, the title still in runes");
		runtime.Frame(viewport,30.33);
		Check(opacity("quitModal_yes-latin","opacity",0) > .02f && opacity("quitModal_yes-latin","opacity",0) < .9f &&
			Near(opacity("quitModal_no-latin","opacity",0),0,.001f),"YES translates while NO waits its turn");
		runtime.Frame(viewport,30.7);
		Check(Near(opacity("quitModal-title-latin","opacity",0),1,.001f) && Near(opacity("quitModal-title-rune","color",3),0,.001f) &&
			Near(opacity("quitModal_yes-latin","opacity",0),1,.001f) && Near(opacity("quitModal_no-latin","opacity",0),1,.001f) &&
			Near(opacity("quitModal_no-rune","color",3),0,.001f) && Near(opacity("quitModal-body","color",3),.85f,.002f),
			"every label has translated and the body has faded in");
		const auto noColour = runtime.PresentedValue("quitModal_no-label","color");
		Check(runtime.FocusedControl() == "quitModal_no" && noColour && Near(static_cast<float>(noColour->data[0]),1,.001f) &&
			Near(static_cast<float>(noColour->data[1]),.8f,.002f) && Near(static_cast<float>(noColour->data[2]),0,.001f),
			"the confirmation opens on NO, its label #FFCC00");
		ActionInvocation quitInvocation;
		Check(runtime.ResolveAction("quitToMenu",quitInvocation,error),"YES still quits to the menu");
		Check(runtime.RunEvent("quitModalHide",31,modal,error),"NO closes the confirmation");
		runtime.Frame(viewport,31.4);
		const auto closedModal = runtime.PresentedValue("quitModal","display");
		Check(closedModal && closedModal->text == "none","the confirmation has closed");
		Check(runtime.RunEvent("quitModalShow",32,modal,error),"the confirmation opens again");
		runtime.Frame(viewport,32.1);
		Check(Near(opacity("quitModal-title-latin","opacity",0),0,.001f) && Near(opacity("quitModal-title-rune","color",3),.8f,.001f),
			"reopening starts in runes again");
		Check(runtime.RunEvent("quitModalHide",32.5,modal,error),"and closes");
		runtime.Frame(viewport,33);
		runtime.SetReducedMotion(true,33.1);
		Check(runtime.RunEvent("exitModalShow",33.1,modal,error),"EXIT asks with reduced motion");
		runtime.Frame(viewport,33.2); runtime.Frame(viewport,33.32);
		Check(Near(opacity("exitModal-title-latin","opacity",0),1,.001f) && Near(opacity("exitModal-title-rune","color",3),0,.001f) &&
			Near(opacity("exitModal_no-latin","opacity",0),1,.001f),"reduced motion shows the confirmation translated at once");
		runtime.SetReducedMotion(false,33.4);
	}
	// Loading: progress, the continue prompt and the multiplayer server card.
	if (runs("loading-hud")) {
		const auto source = Read(argv[3]);
		Document document; std::vector<Diagnostic> diagnostics;
		Check(document.Load(source,diagnostics) && document.Model().id == "openq4.loading" && document.Model().actions.empty(),"loading document validates");
		Runtime runtime(host);
		Check(runtime.Initialize() && runtime.LoadDocument(source,"guis/loading/loading.q4ui",diagnostics),"loading loads into a runtime");
		Viewport viewport; viewport.canvasHeight = 720;
		viewport.canvasMinWidth = 960;
		for (const auto size : {std::pair{1280,360},std::pair{360,640}}) {
			viewport.width = size.first; viewport.height = size.second; runtime.Frame(viewport,.5);
			Bounds objectives, identity, progress;
			const float scale = viewport.DpRatio();
			Check(runtime.GetBounds("objectives",objectives) && Near(objectives.x,27*scale),"loading objectives stay on the leading virtual screen");
			Check(runtime.GetBounds("identity",identity) && Near(identity.x+identity.width,static_cast<float>(viewport.width)),"loading identity stays on the trailing virtual screen");
			Check(runtime.GetBounds("progress",progress) && Near(progress.x+progress.width,static_cast<float>(viewport.width)) &&
				Near(progress.y+progress.height,static_cast<float>(viewport.height)),"loading progress follows the bottom trailing corner in wide and tall views");
			host.draws.clear();
		}
		viewport.width = 1280; viewport.height = 720;
		std::string error;
		Check(runtime.SetState({{"map_loading",0.5},{"loading_levelshot",std::string("gfx/guis/loadscreens/airdefense1")}},error,1),"publish progress");
		runtime.Frame(viewport,1);
		Bounds fill, labelSp, labelMp;
		// Remastered: a 240 u bar (360 dp), 8 u from the right virtual edge.
		Check(runtime.GetBounds("progress-fill-clip",fill) && Near(fill.width,360*.5f,1.5f) && Near(fill.x,1280-960+588,1.5f),
			"the bar fills with map_loading");
		const auto percent = runtime.PresentedValue("progress-percent-value","text");
		Check(percent && percent->text == "50","the percentage follows map_loading");
		Check(runtime.SetState({{"loading_phase",std::string("ASSETS")},{"loading_count",std::string("412/1630")}},error,1.1),
			"publish the loader's phase");
		runtime.Frame(viewport,1.1);
		const auto phase = runtime.PresentedValue("progress-phase","text");
		const auto count = runtime.PresentedValue("progress-count","text");
		Check(phase && phase->text == "ASSETS" && count && count->text == "412/1630","the phase line shows the phase and its count");
		Bounds phaseBox, countBox;
		Check(runtime.GetBounds("progress-phase",phaseBox) && runtime.GetBounds("progress-count",countBox) &&
			countBox.x >= phaseBox.x+phaseBox.width,"the count follows its phase");
		// The fill's clip is exact: no clip is approximated by its bounds.
		Check(runtime.Statistics().clipMaskFallbacks == 0,"the fill clips exactly");
		// Remastered: the levelshot drifts in by 3% over the load, unless
		// motion is reduced.
		const auto drift = runtime.PresentedValue("load-shot","transform");
		Check(drift && Near(static_cast<float>(drift->data[2]),1.015f,.0005f) && Near(static_cast<float>(drift->data[3]),1.015f,.0005f),
			"half way through the load the levelshot has drifted 1.5%");
		host.reducedMotion = true;
		runtime.Frame(viewport,1.5);
		const auto held = runtime.PresentedValue("load-shot","transform");
		Check(held && Near(static_cast<float>(held->data[2]),1,.0005f),"reduced motion holds the levelshot still");
		host.reducedMotion = false;
		Check(runtime.GetBounds("progress-prompt-text",labelSp) && labelSp.y+labelSp.height <= fill.y,"LOADING stands above the bar");
		const auto hiddenGlyph = runtime.PresentedValue("progress-prompt-glyph","display");
		Check(hiddenGlyph && hiddenGlyph->text == "none","no device glyph while loading");
		Runtime::EventEffects effects;
		Check(runtime.RunEvent("FinishedLoading",2,effects,error),"FinishedLoading turns LOADING into the continue prompt");
		const auto prompt = runtime.PresentedValue("progress-prompt-text","text");
		Check(prompt && prompt->text == "#str_200937","the desktop continue prompt replaces LOADING");
		Check(runtime.SetState({{"prompt.glyphs",1.0}},error,2.1),"an Xbox controller was used last");
		runtime.Frame(viewport,2.1);
		const auto controllerPrompt = runtime.PresentedValue("progress-prompt-text","text");
		const auto glyph = runtime.PresentedValue("progress-prompt-glyph","display");
		const auto xboxSouth = runtime.PresentedValue("progress-prompt-glyph-xbox","display");
		const auto genericSouth = runtime.PresentedValue("progress-prompt-glyph-generic","display");
		Check(controllerPrompt && controllerPrompt->text == "#str_200984" && glyph && glyph->text == "block" &&
			xboxSouth && xboxSouth->text == "block" && genericSouth && genericSouth->text == "none",
			"a controller shows its family's south button and CONTINUE");
		Check(runtime.SetState({{"prompt.glyphs",5.0}},error,2.15),"a controller of no known family");
		runtime.Frame(viewport,2.15);
		const auto diamond = runtime.PresentedValue("progress-prompt-glyph-generic","display");
		const auto noXbox = runtime.PresentedValue("progress-prompt-glyph-xbox","display");
		Check(diamond && diamond->text == "block" && noXbox && noXbox->text == "none","a generic controller shows the diamond");
		Check(runtime.SetState({{"prompt.glyphs",0.0}},error,2.2),"back to the desktop prompt");
		const auto spCard = runtime.PresentedValue("server","display");
		Check(spCard && spCard->text == "none","single player shows no server card");
		Check(runtime.SetState({{"loading_mp",true},{"loading_ready",false},{"map_loading",1.0},{"server_name",std::string("openQ4 Test Server")},
			{"server_gametype",std::string("Deathmatch")}},error,3),"multiplayer composition");
		runtime.Frame(viewport,3);
		// Remastered multiplayer keeps the band low, with the server card on the
		// leading side, clear of the lower corner bracket (388 u, 582 dp).
		const auto card = runtime.PresentedValue("server","display");
		const auto mode = runtime.PresentedValue("server-mode","text");
		Bounds cardBox;
		Check(card && card->text == "block" && mode && mode->text == "Deathmatch","multiplayer shows the server card");
		const auto joining = runtime.PresentedValue("progress-prompt-text","text");
		Check(joining && joining->text == "#str_230037","a finished multiplayer load reads JOINING");
		Check(runtime.GetBounds("server-card",cardBox) && Near(cardBox.x,27) && cardBox.y+cardBox.height <= 582,
			"the card stands on the leading side above the bracket and the band");
		Check(runtime.GetBounds("progress-prompt-text",labelMp) && Near(labelSp.y,labelMp.y),"the band and the bar stay low");
		// The card stands on 384 u (576 dp) and grows upward with what the
		// session publishes: the players by team under their heads on a 16 dp
		// pitch, a spectators line, then the server's message, wrapped and
		// clipped at three 20 dp lines (the test face advances 0.6 em).
		const auto noRoster = runtime.PresentedValue("server-roster","display");
		const auto noMessage = runtime.PresentedValue("server-message","display");
		Check(Near(cardBox.y+cardBox.height,576) && Near(cardBox.height,120) && noRoster && noRoster->text == "none" &&
			noMessage && noMessage->text == "none","with no players known and no message the card keeps its base height");
		const auto profont = runtime.PresentedValue("server-address","font-family");
		Check(profont && profont->text == "profont","the address is tabular detail in Profont");
		Check(runtime.SetState({{"server_roster_known",true},{"server_team_mode",true},{"server_roster_rows",3.0},
			{"server_team_a",std::string("Kane\nRhodes\nStrauss")},{"server_team_b",std::string("Makron\nGladiator")},
			{"server_roster_extra",std::string("Spectating: 1")}},error,3.1),"three marines, two strogg and a spectator");
		runtime.Frame(viewport,3.1);
		Bounds headA, headB, teamA, extraLine;
		const auto byTeam = runtime.PresentedValue("server-teams","display");
		const auto everyone = runtime.PresentedValue("server-all","display");
		Check(byTeam && byTeam->text == "block" && everyone && everyone->text == "none","a team mode lists the players by team");
		Check(!host.glyphs.count('\n'),"a list's line breaks draw no glyph");
		Check(runtime.GetBounds("server-card",cardBox) && Near(cardBox.y+cardBox.height,576) && Near(cardBox.height,120+22+3*16+16),
			"the card grows upward by the heads, three rows and the spectators line");
		Check(runtime.GetBounds("server-team-a-head",headA) && runtime.GetBounds("server-team-b-head",headB) &&
			runtime.GetBounds("server-team-a",teamA) && runtime.GetBounds("server-roster-extra",extraLine) &&
			Near(headA.y,cardBox.y+108+4) && Near(headA.x,27+15) && Near(headB.x,27+15+(375-42)/2.f+12) && Near(headB.y,headA.y) &&
			Near(teamA.y,cardBox.y+108+22) && Near(extraLine.y,cardBox.y+108+22+3*16),
			"the lists stand side by side under their heads, the spectators line beneath them");
		const auto marineHead = runtime.PresentedValue("server-team-a-head","color");
		const auto stroggHead = runtime.PresentedValue("server-team-b-head","color");
		Check(marineHead && Near(static_cast<float>(marineHead->data[0]),0x6a/255.f,.002f) && stroggHead &&
			Near(static_cast<float>(stroggHead->data[0]),1,.002f),"the heads read in the team colors");
		Check(runtime.SetState({{"server_message",std::string("Welcome to the openQ4 test server")}},error,3.2),"a one-line message");
		runtime.Frame(viewport,3.2);
		Bounds message;
		Check(runtime.GetBounds("server-card",cardBox) && runtime.GetBounds("server-message",message) && Near(message.height,20) &&
			Near(cardBox.height,120+86+6+20) && Near(cardBox.y+cardBox.height,576) && Near(message.y,cardBox.y+108+86+6),
			"the message takes a line under the roster");
		Check(runtime.SetState({{"server_message",std::string("Frag limit 30 and no camping near the rail. Teams are balanced every map. "
			"Vote kicks need a majority. Be excellent to each other and have fun out there.")}},error,3.3),"a long message");
		runtime.Frame(viewport,3.3);
		Check(runtime.GetBounds("server-card",cardBox) && runtime.GetBounds("server-message",message) && Near(message.height,60) &&
			Near(cardBox.height,120+86+6+60) && cardBox.y >= 178.5f,"a long message wraps and clips at three lines, the card clear of the top band");
		Check(runtime.SetState({{"server_team_mode",false},{"server_roster_rows",7.0},
			{"server_players",std::string("One\nTwo\nThree\nFour\nFive\nSix\n+3 more")},{"server_roster_extra",std::string("")},
			{"server_message",std::string("")}},error,3.4),"seven rows of free-for-all players");
		runtime.Frame(viewport,3.4);
		const auto allShown = runtime.PresentedValue("server-all","display");
		const auto teamsHidden = runtime.PresentedValue("server-teams","display");
		Check(allShown && allShown->text == "block" && teamsHidden && teamsHidden->text == "none" && runtime.GetBounds("server-card",cardBox) &&
			Near(cardBox.height,120+22+7*16),"outside team modes one list holds every player");
		Check(runtime.SetState({{"server_roster_rows",0.0},{"server_players",std::string("")},
			{"server_roster_extra",std::string("Connecting: 2")}},error,3.5),"only players still connecting");
		runtime.Frame(viewport,3.5);
		const auto allEmpty = runtime.PresentedValue("server-all","display");
		Check(allEmpty && allEmpty->text == "none" && runtime.GetBounds("server-card",cardBox) && runtime.GetBounds("server-roster-extra",extraLine) &&
			Near(cardBox.height,120+4+16) && Near(extraLine.y,cardBox.y+108+4),"with no names the line stands alone, without heads");
		Check(runtime.SetState({{"server_roster_known",false},{"server_roster_extra",std::string("")}},error,3.6),"a fresh connection");
		runtime.Frame(viewport,3.6);
		Check(runtime.GetBounds("server-card",cardBox) && Near(cardBox.height,120),"a load that knows no players shows none");
		// The arsenal: each kind of item fades in over 150 ms in its color code,
		// two rows of ten in the thick band's leading part.
		host.materials.clear();
		Check(runtime.SetState({{"load_icon_1",1.0},{"load_icon_src_1",std::string("gfx/guis/hud/icons/item_railgun")},
			{"load_icon_r_1",0.0},{"load_icon_g_1",1.0},{"load_icon_b_1",0.0},
			{"load_icon_11",1.0},{"load_icon_src_11",std::string("gfx/guis/hud/icons/item_armorlarge")}},error,4),"two items spawn");
		Check(runtime.PlayTimeline("arsenal1",4) && runtime.PlayTimeline("arsenal11",4),"their icons fade in");
		runtime.Frame(viewport,4.075);
		const auto half = runtime.PresentedValue("arsenal-1","opacity");
		const auto tint = runtime.PresentedValue("arsenal-1","image-color");
		const auto unspawned = runtime.PresentedValue("arsenal-2","display");
		Check(half && Near(static_cast<float>(half->data[0]),.5f,.02f) && tint && Near(static_cast<float>(tint->data[0]),0,.001f) &&
			Near(static_cast<float>(tint->data[1]),1,.001f) && unspawned && unspawned->text == "none",
			"an icon is half in at 75 ms, tinted in the item's color code; an unspawned kind shows nothing");
		runtime.Frame(viewport,4.2);
		const auto full = runtime.PresentedValue("arsenal-11","opacity");
		Bounds first, eleventh;
		Check(full && Near(static_cast<float>(full->data[0]),1,.001f) && runtime.GetBounds("arsenal-1",first) &&
			runtime.GetBounds("arsenal-11",eleventh) && Near(first.x,216*1.5f,1) && Near(eleventh.x,first.x,1) &&
			Near(eleventh.y-first.y,18*1.5f,1) && Near(first.width,24,1) && first.y >= 426*1.5f,
			"the second row starts under the first, inside the thick band");
		Check(std::find(host.materials.begin(),host.materials.end(),"gfx/guis/hud/icons/item_railgun") != host.materials.end(),
			"the icon draws its image");
		Check(runtime.SetState({{"loading_mp",false}},error,4.3),"single player again");
		runtime.Frame(viewport,4.3);
		const auto spArsenal = runtime.PresentedValue("arsenal","display");
		Check(spArsenal && spArsenal->text == "none","single player has no arsenal");
		// Single player tips (section 14.17): under a TIP tag in the band's
		// leading part, each cross-fading in over 250 ms in the other slot.
		const auto noTips = runtime.PresentedValue("tips","display");
		Check(noTips && noTips->text == "none","no tip is shown until the session publishes one");
		Check(runtime.SetState({{"loading_tip_a",std::string("#str_230052")}},error,4.4) && runtime.PlayTimeline("tipA",4.4),"the first tip");
		runtime.Frame(viewport,4.525);
		const auto tipsShown = runtime.PresentedValue("tips","display");
		const auto halfTip = runtime.PresentedValue("tip-a","color");
		const auto tipText = runtime.PresentedValue("tip-a","text");
		Bounds tipBox, tagBox;
		Check(tipsShown && tipsShown->text == "block" && halfTip && Near(static_cast<float>(halfTip->data[3]),.35f,.02f) && tipText &&
			tipText->text == "#str_230052","the first tip fades in over 250 ms");
		Check(runtime.GetBounds("tip-a",tipBox) && runtime.GetBounds("tip-tag",tagBox) && Near(tipBox.x,216*1.5f,1) &&
			Near(tipBox.width,168*1.5f,1) && tagBox.y < tipBox.y && tipBox.y+tipBox.height <= 480*1.5f && tipBox.y >= 426*1.5f,
			"the tip sits under its tag in the thick band, from the riser to the bar");
		Check(runtime.SetState({{"loading_tip_b",std::string("#str_230053")}},error,10.4) && runtime.PlayTimeline("tipB",10.4),"the next tip");
		runtime.Frame(viewport,10.65);
		const auto outgoing = runtime.PresentedValue("tip-a","color");
		const auto incoming = runtime.PresentedValue("tip-b","color");
		Check(outgoing && Near(static_cast<float>(outgoing->data[3]),0,.001f) && incoming && Near(static_cast<float>(incoming->data[3]),.7f,.001f),
			"the next tip cross-fades in the other slot");
		Check(runtime.SetState({{"loading_ready",true}},error,10.66),"the level is ready");
		runtime.Frame(viewport,10.66);
		const auto readyTips = runtime.PresentedValue("tips","display");
		Check(readyTips && readyTips->text == "none","the continue prompt takes the band once the level is ready");
		Check(runtime.SetState({{"loading_ready",false},{"loading_mp",true}},error,10.7),"multiplayer");
		runtime.Frame(viewport,10.7);
		const auto mpTips = runtime.PresentedValue("tips","display");
		Check(mpTips && mpTips->text == "none","multiplayer shows its arsenal instead of tips");
		Check(runtime.SetState({{"loading_mp",false},{"loading_tip_a",std::string("")},{"loading_tip_b",std::string("")}},error,10.8),"reset");
		// A multiplayer join hands the screen to the Welcome card: handoff fades
		// it whole over 250 ms (80 ms under reduced motion), and present makes
		// it whole again for the next load.
		const auto opacity = [&]() {
			const auto value = runtime.PresentedValue("screen","opacity");
			Check(value.has_value(),"the screen's opacity");
			return static_cast<float>(value->data[0]);
		};
		runtime.Frame(viewport,11);
		Check(Near(opacity(),1,.001f),"the screen presents whole");
		Check(runtime.RunEvent("handoff",11,effects,error),"handoff plays");
		runtime.Frame(viewport,11.125);
		Check(opacity() > .3f && opacity() < .7f,"half way through the hand-off");
		runtime.Frame(viewport,11.26);
		Check(Near(opacity(),0,.001f),"handed off after 250 ms");
		Check(runtime.RunEvent("present",11.5,effects,error),"present plays");
		runtime.Frame(viewport,11.51);
		Check(Near(opacity(),1,.001f),"whole again for the next load");
		host.reducedMotion = true;
		runtime.SetReducedMotion(true,12);
		Check(runtime.RunEvent("handoff",12,effects,error),"handoff under reduced motion");
		runtime.Frame(viewport,12.04);
		Check(opacity() > .3f && opacity() < .7f,"half way after 40 ms");
		runtime.Frame(viewport,12.09);
		Check(Near(opacity(),0,.001f),"handed off within 80 ms");
		Check(runtime.RunEvent("present",12.2,effects,error),"present under reduced motion");
		runtime.Frame(viewport,12.21);
		Check(Near(opacity(),1,.001f),"whole again");
		host.reducedMotion = false;
		runtime.SetReducedMotion(false,12.3);
	}
	// The Single Player page and its Campaign sub-page (sections 8 and 9, the
	// sub-page level of 1.10): their rest states, going deeper and Back.
	if (runs("pages")) {
		const auto pageSource = Read(argv[5]);
		const auto subSource = Read(argv[6]);
		Document pageDocument, subDocument; std::vector<Diagnostic> diagnostics;
		Check(pageDocument.Load(pageSource,diagnostics) && subDocument.Load(subSource,diagnostics),"the selector documents validate");
		for (const auto& diagnostic : diagnostics) std::fprintf(stderr,"selectors %s: %s\n",diagnostic.pointer.c_str(),diagnostic.message.c_str());
		Runtime page(host), sub(host);
		Check(page.Initialize() && page.LoadDocument(pageSource,"guis/menu/singleplayer.q4ui",diagnostics) &&
			sub.Initialize() && sub.LoadDocument(subSource,"guis/menu/campaigns.q4ui",diagnostics),"the selectors load into runtimes");
		Viewport viewport; viewport.canvasHeight = 720;
		std::string error;
		Runtime::EventEffects effects;
		Check(page.RunEvent("onActivate",1,effects,error) && sub.RunEvent("onActivate",1,effects,error),"activation places both at rest");
		page.Frame(viewport,1.1); sub.Frame(viewport,1.1);
		// At rest: the page title in the band's slot; on the sub-page the top
		// band one notch pitch on, the bottom band docked, the crumb 7 u up at
		// 80 % and 0.40 in the band's thin span, and the sub-page title at the
		// step's foot.
		Bounds pageTitle, subTitle, crumbClip;
		Check(page.GetBounds("title",pageTitle) && Near(pageTitle.x,160+39*1.5f,1) && Near(pageTitle.y,19*1.5f,1),"the page titles itself at 39,19 u");
		const auto pageTop = page.PresentedValue("band-top","transform");
		const auto subTop = sub.PresentedValue("band-top","transform");
		const auto pageBottom = page.PresentedValue("band-bottom","transform");
		const auto subBottom = sub.PresentedValue("band-bottom","transform");
		Check(pageTop && subTop && pageBottom && subBottom && Near(static_cast<float>(subTop->data[0]-pageTop->data[0]),97*1.5f,.05f) &&
			Near(static_cast<float>(subTop->data[1]),static_cast<float>(pageTop->data[1]),.01f) &&
			Near(static_cast<float>(subBottom->data[0]),static_cast<float>(pageBottom->data[0]),.01f),
			"the sub-page's top band sits one notch pitch on; the bottom band stays docked");
		// SINGLE PLAYER would overrun the band's thin span at 80 %, so the crumb
		// shrinks to fit it (0.714 of the title, measured from marine.ttf).
		const auto crumb = sub.PresentedValue("crumb","transform");
		const auto crumbColour = sub.PresentedValue("crumb","color");
		const auto crumbText = sub.PresentedValue("crumb","text");
		Check(crumb && Near(static_cast<float>(crumb->data[2]),.7224f,.002f) && crumbColour && Near(static_cast<float>(crumbColour->data[3]),.4f,.001f) &&
			crumbText && crumbText->text == "#str_42000","the parent title is the crumb at 0.40, shrunk to fit the span");
		// The crumb's top: 19 u less 7 u, with the scale about the box center.
		Bounds crumbBox;
		Check(sub.GetBounds("crumb",crumbBox) && Near(crumbBox.y+static_cast<float>(crumb->data[1])+crumbBox.height*(1-static_cast<float>(crumb->data[2]))/2,
			12*1.5f,.5f),"the crumb rises 7 u into the band's thin span");
		// Italian's GIOCATORE SINGOLO stops shrinking at the 13 dp type floor.
		host.language = "italian";
		Check(sub.RunEvent("onActivate",1.2,effects,error),"activation in Italian");
		sub.Frame(viewport,1.3);
		const auto floored = sub.PresentedValue("crumb","transform");
		Check(floored && Near(static_cast<float>(floored->data[2])*18,13,.05f),"a crumb never shrinks below the 13 dp type floor");
		// French's UN JOUEUR fits the span at the full 80 %.
		host.language = "french";
		Check(sub.RunEvent("onActivate",1.32,effects,error),"activation in French");
		sub.Frame(viewport,1.35);
		const auto french = sub.PresentedValue("crumb","transform");
		Check(french && Near(static_cast<float>(french->data[2]),.8f,.001f),"a crumb that fits keeps 80 % of the title");
		host.language = "english";
		Check(sub.RunEvent("onActivate",1.4,effects,error),"activation in English");
		sub.Frame(viewport,1.5);
		Check(sub.GetBounds("crumb-clip",crumbClip) && Near(crumbClip.x,160+39*1.5f,1) && Near(crumbClip.width,83*1.5f,1),
			"the crumb is cut at the band's step");
		Check(sub.GetBounds("title",subTitle) && Near(subTitle.x,160+136*1.5f,1) && Near(subTitle.y,19*1.5f,1),
			"the sub-page title starts at the step's foot");
		// NEW GAME and CHAPTERS summarize themselves (section 13.7) over the
		// active campaign's levelshots while the campaign chosen is the active
		// one; QUAKE 4, THE AWAKENING and BACK keep the column's word on the
		// campaigns.
		const auto lit = [&](const char* node) { const auto found = sub.PresentedValue(node,"opacity"); return found ? static_cast<float>(found->data[0]) : -1.f; };
		const auto shown = [&](const char* node) { const auto found = sub.PresentedValue(node,"display"); return found ? found->text : std::string("?"); };
		Check(sub.SetState({{"awakening_ready",true},{"summary.newgame.shot",std::string("gfx/guis/loadscreens/e3_load")},
			{"summary.chapters.shot",std::string("gfx/guis/loadscreens/airdefense")}},error,1.55),"the session names the active campaign's levelshots");
		sub.Frame(viewport,1.56);
		Check(sub.FocusedControl() == "newgame" && Near(lit("summary-newgame"),1,.001f) && Near(lit("available"),0,.001f) &&
			shown("summary-newgame-shots") == "block","NEW GAME summarizes itself over the first level's levelshot");
		Check(sub.FocusControl("chapters",1.57),"focus CHAPTERS");
		sub.Frame(viewport,1.58);
		Check(Near(lit("summary-chapters"),1,.001f) && Near(lit("summary-newgame"),0,.001f) && shown("summary-chapters-shots") == "block",
			"CHAPTERS summarizes itself over the furthest level's");
		Check(sub.SetState({{"campaign.awakening",true}},error,1.59),"choose the campaign that is not active");
		sub.Frame(viewport,1.6);
		Check(shown("summary-chapters-shots") == "none","no levelshot shows for a campaign that is not mounted");
		Check(sub.FocusControl("quake4",1.61),"focus QUAKE 4");
		sub.Frame(viewport,1.62);
		Check(Near(lit("available"),1,.001f) && Near(lit("summary-chapters"),0,.001f),"QUAKE 4 keeps the column's word on the campaigns");
		Check(sub.SetState({{"campaign.awakening",false}},error,1.63) && sub.FocusControl("newgame",1.63),"back as it was");
		// Going deeper: the plates fade over 150 ms while the page sweeps
		// 640 u; the title becomes the crumb in 150 ms; from 50 ms the band
		// steps and CAMPAIGN carries into the slot at the step's foot.
		Check(page.RunEvent("subpageEnter",2,effects,error),"Campaign leads deeper");
		page.Frame(viewport,2.075);
		const auto fading = page.PresentedValue("content","opacity");
		const auto stepping = page.PresentedValue("band-top","transform");
		const auto sweeping = page.PresentedValue("content","transform");
		Check(sweeping && Near(static_cast<float>(sweeping->data[0]),640*1.5f/4,.5f),"the page sweeps linearly (a quarter at 75 ms)");
		Check(fading && Near(static_cast<float>(fading->data[0]),.5f,.02f) && stepping &&
			stepping->data[0] > pageTop->data[0] && stepping->data[0] < subTop->data[0],"at 75 ms the plates are half gone and the band is stepping");
		page.Frame(viewport,2.15);
		const auto crumbed = page.PresentedValue("title","transform");
		const auto crumbedColour = page.PresentedValue("title","color");
		const auto gone = page.PresentedValue("content","opacity");
		Check(crumbed && Near(static_cast<float>(crumbed->data[2]),static_cast<float>(crumb->data[2]),.001f) &&
			Near(static_cast<float>(crumbed->data[0]),static_cast<float>(crumb->data[0]),.01f) &&
			Near(static_cast<float>(crumbed->data[1]),static_cast<float>(crumb->data[1]),.01f) && crumbedColour &&
			Near(static_cast<float>(crumbedColour->data[3]),.4f,.001f) && gone && Near(static_cast<float>(gone->data[0]),0,.001f),
			"by 150 ms the title is the crumb and the plates are gone");
		page.Frame(viewport,2.35);
		const auto stepped = page.PresentedValue("band-top","transform");
		const auto swept = page.PresentedValue("content","transform");
		const auto carried = page.PresentedValue("subpage-carry","transform");
		const auto carriedColour = page.PresentedValue("subpage-carry","color");
		Bounds carry;
		Check(stepped && Near(static_cast<float>(stepped->data[0]),static_cast<float>(subTop->data[0]),.05f) && swept &&
			Near(static_cast<float>(swept->data[0]),640*1.5f,.5f) && carried && Near(static_cast<float>(carried->data[2]),.75f,.001f) &&
			carriedColour && Near(static_cast<float>(carriedColour->data[3]),.5f,.001f) && page.GetBounds("subpage-carry",carry) &&
			Near(carry.x,160+136*1.5f,1),"at 350 ms the band has stepped, the page has swept and CAMPAIGN sits at the step's foot");
		// The session presents the sub-page at 350 ms: plates and title at once,
		// the backing fading in over 150 ms (its activation).
		Check(sub.RunEvent("onDeactivate",2.9,effects,error) && sub.RunEvent("onActivate",3,effects,error),"the sub-page appears");
		sub.Frame(viewport,3.075);
		const auto backing = sub.PresentedValue("details","opacity");
		const auto plates = sub.PresentedValue("content","opacity");
		const auto shownTitle = sub.PresentedValue("title","color");
		Check(backing && Near(static_cast<float>(backing->data[0]),.5f,.02f) && plates && Near(static_cast<float>(plates->data[0]),1,.001f) &&
			shownTitle && Near(static_cast<float>(shownTitle->data[3]),.5f,.001f),"its plates and title show at once, its backing fades in");
		// Back: the sub-page title drops at once, the sub-page sweeps 640 u
		// toward the leading edge and the band steps back from the start; the
		// crumb returns over the last 150 ms.
		Check(sub.RunEvent("subpageLeave",4,effects,error),"Back climbs one level");
		sub.Frame(viewport,4.01);
		const auto dropped = sub.PresentedValue("title","color");
		const auto leaving = sub.PresentedValue("band-top","transform");
		const auto heldCrumb = sub.PresentedValue("crumb","transform");
		Check(dropped && Near(static_cast<float>(dropped->data[3]),0,.001f) && leaving && leaving->data[0] < subTop->data[0] &&
			heldCrumb && Near(static_cast<float>(heldCrumb->data[2]),static_cast<float>(crumb->data[2]),.001f),
			"the title drops at once and the band steps back while the crumb holds");
		sub.Frame(viewport,4.225);
		const auto returning = sub.PresentedValue("crumb","transform");
		Check(returning && returning->data[2] > crumb->data[2] && returning->data[2] < 1,"the crumb returns over the last 150 ms");
		sub.Frame(viewport,4.3);
		const auto home = sub.PresentedValue("band-top","transform");
		const auto returned = sub.PresentedValue("crumb","transform");
		const auto sweptLeft = sub.PresentedValue("content","transform");
		const auto backingOut = sub.PresentedValue("details","opacity");
		Check(home && Near(static_cast<float>(home->data[0]),static_cast<float>(pageTop->data[0]),.05f) && returned &&
			Near(static_cast<float>(returned->data[2]),1,.001f) && Near(static_cast<float>(returned->data[1]),0,.01f) && sweptLeft &&
			Near(static_cast<float>(sweptLeft->data[0]),-640*1.5f,.5f) && backingOut && Near(static_cast<float>(backingOut->data[0]),0,.001f),
			"by 300 ms the band is docked, the crumb is the title, the backing is out and the sub-page has swept out");
		// Hidden, the sub-page goes back to rest, so it never presents moved.
		Check(sub.RunEvent("onDeactivate",4.4,effects,error),"the sub-page leaves the screen");
		sub.Frame(viewport,4.45);
		const auto restedTitle = sub.PresentedValue("title","color");
		const auto restedContent = sub.PresentedValue("content","transform");
		const auto restedCrumb = sub.PresentedValue("crumb","transform");
		const auto subRestBand = sub.PresentedValue("band-top","transform");
		const auto restedBacking = sub.PresentedValue("details","opacity");
		Check(restedTitle && Near(static_cast<float>(restedTitle->data[3]),.5f,.001f) && restedContent && Near(static_cast<float>(restedContent->data[0]),0,.01f) &&
			restedCrumb && Near(static_cast<float>(restedCrumb->data[2]),static_cast<float>(crumb->data[2]),.001f) && subRestBand &&
			Near(static_cast<float>(subRestBand->data[0]),static_cast<float>(subTop->data[0]),.05f) && restedBacking &&
			Near(static_cast<float>(restedBacking->data[0]),0,.001f),"hidden, the sub-page rests in the sub-page state with its backing out");
		// The page returns at 300 ms: its plates and content fade in over 150 ms.
		Check(page.RunEvent("onDeactivate",3,effects,error),"the page left the screen at the hand-over");
		Check(page.RunEvent("onActivate",5,effects,error) && page.RunEvent("subpageReturn",5,effects,error),"the page returns");
		page.Frame(viewport,5.0005);
		const auto firstFrame = page.PresentedValue("band-top","transform");
		Check(firstFrame && Near(static_cast<float>(firstFrame->data[0]),static_cast<float>(pageTop->data[0]),.05f),
			"its first frame already shows it docked, never moved");
		page.Frame(viewport,5.075);
		const auto back = page.PresentedValue("content","opacity");
		const auto placed = page.PresentedValue("content","transform");
		const auto restored = page.PresentedValue("title","transform");
		const auto restedBand = page.PresentedValue("band-top","transform");
		const auto noCarry = page.PresentedValue("subpage-carry","opacity");
		Check(back && Near(static_cast<float>(back->data[0]),.5f,.02f) && placed && Near(static_cast<float>(placed->data[0]),0,.01f) &&
			restored && Near(static_cast<float>(restored->data[2]),1,.001f) && restedBand &&
			Near(static_cast<float>(restedBand->data[0]),static_cast<float>(pageTop->data[0]),.05f) && noCarry && Near(static_cast<float>(noCarry->data[0]),0,.001f),
			"the page fades back in place with its title in the slot and no carry");
		// Back while going deeper: every part returns from where it stands.
		page.Frame(viewport,5.3);
		Check(page.RunEvent("subpageEnter",6,effects,error),"Campaign leads deeper again");
		page.Frame(viewport,6.12);
		const auto partway = page.PresentedValue("band-top","transform");
		Check(page.RunEvent("subpageReverse150",6.12,effects,error),"Back reverses the change");
		page.Frame(viewport,6.125);
		const auto reversing = page.PresentedValue("band-top","transform");
		Check(partway && reversing && reversing->data[0] <= partway->data[0] + .01f && reversing->data[0] > pageTop->data[0],
			"the band starts back from where it stood");
		page.Frame(viewport,6.3);
		const auto reversed = page.PresentedValue("band-top","transform");
		const auto reversedContent = page.PresentedValue("content","transform");
		const auto reversedOpacity = page.PresentedValue("content","opacity");
		const auto reversedTitle = page.PresentedValue("title","transform");
		const auto reversedCarry = page.PresentedValue("subpage-carry","opacity");
		Check(reversed && Near(static_cast<float>(reversed->data[0]),static_cast<float>(pageTop->data[0]),.05f) && reversedContent &&
			Near(static_cast<float>(reversedContent->data[0]),0,.01f) && reversedOpacity && Near(static_cast<float>(reversedOpacity->data[0]),1,.001f) &&
			reversedTitle && Near(static_cast<float>(reversedTitle->data[2]),1,.001f) && reversedCarry && Near(static_cast<float>(reversedCarry->data[0]),0,.001f),
			"by 150 ms the page is back as it was");
		// Reduced motion: the session hands over at once and the arrival fades
		// in within 80 ms.
		sub.SetReducedMotion(true,6.9);
		Check(sub.RunEvent("onDeactivate",6.9,effects,error) && sub.RunEvent("onActivate",7,effects,error),"the sub-page arrives at once");
		sub.Frame(viewport,7.04);
		const auto halfIn = sub.PresentedValue("details","opacity");
		sub.Frame(viewport,7.09);
		const auto fullIn = sub.PresentedValue("details","opacity");
		Check(halfIn && halfIn->data[0] > 0 && halfIn->data[0] < 1 && fullIn && Near(static_cast<float>(fullIn->data[0]),1,.001f),
			"reduced motion fades the arrival in within 80 ms");
		sub.SetReducedMotion(false,7.2);
	}
	if (runs("mp-card")) CheckEscape(host,argv[7]);
	if (runs("mp-vote-match")) CheckVote(host,argv[7]);
	if (runs("mp-vote-match")) CheckMatch(host,argv[7]);
	if (runs("mp-admin")) CheckAdmin(host,argv[7]);
	if (runs("mp-settings-welcome")) CheckSettings(host,argv[7],argv[8]);
	if (runs("mp-settings-welcome")) CheckWelcome(host,argv[8]);
	if (runs("optics")) CheckScopes(host,argv+9);
	if (runs("optics")) CheckContinuity(host,argv+12,argv[6]);
	if (runs("optics")) CheckWeaponWheels(host,argv+15);
	if (runs("pages")) CheckLoadPage(host,argv[19]);
	if (runs("pages")) CheckSavePage(host,argv[20]);
	if (runs("pages")) CheckRestartPage(host,argv[21]);
	if (runs("strogg-pages")) CheckLoadPage(host,StroggPage(argv[19]).c_str(),true);
	if (runs("strogg-pages")) CheckSavePage(host,StroggPage(argv[20]).c_str(),true);
	if (runs("strogg-pages")) CheckRestartPage(host,StroggPage(argv[21]).c_str(),true);
	if (runs("pages")) CheckDemosPage(host,argv[22]);
	if (runs("pages")) {
		// The list pages at larger text, with two rows and a selection.
		StateValues load{{"load.count",2.0},{"load.first",0.0},{"load.selected",0.0},{"load.date",std::string("11/02/2026")},
			{"load.time",std::string("09:26am")},{"load.skill",std::string("CORPORAL")},{"load.played",std::string("0:03:13 played")},
			{"load.row0.name",std::string("Medic")},{"load.row0.location",std::string("AIR DEFENSE BUNKER")},
			{"load.row1.name",std::string("Bunker door")},{"load.row1.location",std::string("AIR DEFENSE BUNKER")}};
		CheckListTextScale(host,argv[19],"guis/menu/loadgame.q4ui",load,"load-list",{"load-date","load-time","load-skill","load-played"},
			"load-shot-back","load_go-plate");
		StateValues demos{{"demos.count",2.0},{"demos.first",0.0},{"demos.selected",0.0},{"demos.kind",std::string("Render demo v10")},
			{"demos.status",std::string("Playable")},{"demos.detail",std::string("Fixed recorded camera")},{"demos.playable",true},
			{"demos.row0.name",std::string("newer")},{"demos.row0.map",std::string("-")},{"demos.row0.date",std::string("11/02/2026")},
			{"demos.row0.length",std::string("--:--")},{"demos.row1.name",std::string("older")},{"demos.row1.length",std::string("--:--")}};
		CheckListTextScale(host,argv[22],"guis/menu/demos.q4ui",demos,"demo-list",{"demo-kind","demo-status","demo-detail"},
			"demo-shot-back","demo_play-plate");
	}
	if (runs("pages")) CheckPagesTextScale(host,argv);
	if (runs("strogg-pages")) {
		const std::string path = StroggPage(argv[21]);
		CheckTextScaleCase(host,RestartTextScale(path.c_str(),"guis/menu/restartlevel_strogg.q4ui"));
	}
	if (runs("menus")) CheckMenusTextScale(host,argv);
	if (runs("mp-pages")) CheckMultiplayerTextScale(host,argv);
	if (runs("menus")) CheckModsDialog(host,argv[1]);
	if (runs("pages")) CheckRestartMenu(host,argv[23]);
	if (runs("pages")) CheckCreditsPage(host,argv[24]);
	if (runs("pages")) CheckArenaPage(host,argv[5],argv[25]);
	if (runs("mp-pages")) CheckMultiplayerPage(host,argv[26]);
	if (runs("mp-pages")) CheckColumnPageTextScale(host,argv[26],"guis/menu/multiplayer.q4ui",{},{},
		{"description","summary-join","summary-create","summary-demos","summary-setup"});
	if (runs("mp-pages")) CheckCreateServerPage(host,argv[27]);
	if (runs("mp-pages")) CheckCreateServerBanList(host,argv[27]);
	if (runs("mp-pages")) CheckJoinGamePage(host,argv[28]);
	if (runs("pointer-reach")) CheckMenusPointerReach(host,argv);
	Check(known,"a group the test knows");
	Check(host.errors == 0,"no retained diagnostics");
	std::printf("retained screens%s%s: %d checks passed\n",group.empty() ? "" : " ",group.c_str(),checks);
	return 0;
}
