// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "../idlib/precompiled.h"
#include "../framework/NativeInputPublications.h"
#include "UserInterfaceRetained.h"
#include "RetainedHud.h"
#include <span>
#include "GuiStateNumber.h"
#include "Rectangle.h"
#ifndef ID_DEDICATED
#include "RetainedUI.h"
#include "PointerCursor.h"
#include "SettingsService.h"
#include "retained/Runtime.h"
#include "retained/Input.h"
#include "retained/TextEditCommand.h"
#include "retained/PlayerSetupRequest.h"
#include "../sys/KeyEventMetadata.h"
#include "application/SettingsTransaction.h"
#include "../framework/ParallelJobSystem.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <set>

#if defined(USE_SDL3)
bool Sys_SDL_IsGameWindowFocused(void);
int Sys_SDL_GamepadGlyphFamily(int faceLabels[4]);
#endif

namespace {
using namespace openq4::ui;
constexpr const char* ActionMarker = "openq4-retained-actions";
constexpr unsigned SaveTag = 0x49553451; // Q4UI, little endian through idFile.
constexpr int MaxStateEntries = 4096, MaxStringBytes = 65536;
constexpr int MaxStateBytes = 16 * 1024 * 1024;
constexpr int MaxFrameBytes = static_cast<int>(Runtime::MaxSnapshotBytes) + MaxStateBytes + MaxStateEntries*8 + 64;
std::vector<idUserInterfaceRetained*> diagnosticViews;

constexpr const char* NumberDraftPending = "ui.numberDraftsPending";
constexpr const char* NumberDraftMessage = "ui.numberDraftMessage";
bool NumberDraftState(const char* name) {
	return name && (!idStr::Icmp(name,NumberDraftPending) || !idStr::Icmp(name,NumberDraftMessage));
}

// A bound key's escape (^ikHH or ^iKHH, HH its key number in hex), which
// common->KeysFromBinding writes for the stock GUIs' key plates.
int KeyEscapeDigit(char c) {
	return c >= '0' && c <= '9' ? c - '0' : (c | 0x20) >= 'a' && (c | 0x20) <= 'f' ? (c | 0x20) - 'a' + 10 : -1;
}
int KeyEscape(const char* p) {
	return p[0] == '^' && p[1] == 'i' && (p[2] == 'k' || p[2] == 'K') && p[3] && KeyEscapeDigit(p[3]) >= 0 && p[4] &&
		KeyEscapeDigit(p[4]) >= 0 ? KeyEscapeDigit(p[3]) * 16 + KeyEscapeDigit(p[4]) : -1;
}

// In a retained document's string state a key escape reads as the key's name.
std::string KeyEscapeNames(const char* text) {
	if (!std::strstr(text,"^i")) return text;
	std::string result;
	for (const char* p = text; *p; ) {
		const int key = KeyEscape(p);
		if (key >= 0) {
			result += idKeyInput::KeyNumToString(key,true);
			p += 5;
		} else {
			result += *p++;
		}
	}
	return result;
}

// A string the stock interface built can carry bytes that are not UTF-8: text
// typed into one of its fields is one byte a character, so an accented letter
// in a chat line or a player's name arrives as a lone high byte. A document
// refuses a whole batch of state over one such value, which froze the
// Remastered HUD for everyone who was sent the line. Read such text the way
// the text layer draws it and hand the document UTF-8.
std::string StateText(std::string text) {
	if (LangDict_IsUtf8(text.c_str(),static_cast<int>(text.size()))) return text;
	std::string repaired;
	for (size_t i = 0; i < text.size();) {
		unsigned int codePoint = 0;
		const int used = LangDict_NextCodePoint(text.c_str() + i,static_cast<int>(text.size() - i),codePoint);
		if (used <= 0) break;
		char encoded[UTF8_MAX_BYTES];
		int count = LangDict_EncodeUtf8(codePoint,encoded);
		if (count <= 0) count = LangDict_EncodeUtf8(UNICODE_REPLACEMENT,encoded);
		repaired.append(encoded,static_cast<size_t>(count));
		i += static_cast<size_t>(used);
	}
	return repaired;
}

// The glyph a prompt's chip draws for a bound key (ui-visual-design.md 13.4,
// 13.5, 14.8), in the family of the controller in use, and its label. A
// document that declares "<key>.glyph" beside a key state receives the kind;
// the key state then holds the label. The kinds are the documents' contract
// (tools/ui/remastered_hud_base.py, GLYPH_*): 0 keycap (the key's name), 1 a
// face button's letter, 2-5 the cross, circle, square and triangle, 6-9 the
// south, east, west and north face buttons by position, 10 bumper, 11
// trigger, 12-15 the d-pad's up, down, left and right, 16 stick click, 17
// menu, 18 view, 19 home, 20 touchpad, 21 paddle, 22 another labelled button,
// 23-25 the mouse's left, right and middle buttons, 26-27 the wheel up and
// down, 28 a side button.
struct KeyGlyph { int kind = 0; std::string label; };
KeyGlyph PromptGlyph(int key) {
	KeyGlyph glyph;
	if (key >= K_MOUSE1 && key <= K_MOUSE8) {
		glyph.kind = key == K_MOUSE1 ? 23 : key == K_MOUSE2 ? 24 : key == K_MOUSE3 ? 25 : 28;
		if (glyph.kind == 28) glyph.label = std::to_string(key - K_MOUSE1 + 1);
		return glyph;
	}
	if (key == K_MWHEELUP || key == K_MWHEELDOWN) {
		glyph.kind = key == K_MWHEELUP ? 26 : 27;
		return glyph;
	}
	const char* code = idKeyInput::KeyNumToString(key,false);
	if (idStr::Icmpn(code,"JOY",3)) {
		glyph.kind = idStr::Icmpn(code,"AUX",3) ? 0 : 22;
		glyph.label = idKeyInput::KeyNumToString(key,true);
		if (glyph.label.size() == 1) glyph.label[0] = static_cast<char>(idStr::ToUpper(glyph.label[0]));
		return glyph;
	}
	// 0 generic, 1 Xbox, 2 PlayStation, 3 Nintendo, 4 Steam Deck.
	int face[4] = {0,0,0,0}, family = 0;
#if defined(USE_SDL3)
	family = idMath::ClampInt(0,4,Sys_SDL_GamepadGlyphFamily(face));
#endif
	static const char* const bumpers[5][2] = {{"L1","R1"},{"LB","RB"},{"L1","R1"},{"L","R"},{"L1","R1"}};
	static const char* const triggers[5][2] = {{"L2","R2"},{"LT","RT"},{"L2","R2"},{"ZL","ZR"},{"L2","R2"}};
	static const char* const sticks[5][2] = {{"L3","R3"},{"LS","RS"},{"L3","R3"},{"LS","RS"},{"L3","R3"}};
	static const char* const letters[5] = {"","A","B","X","Y"};
	const int button = std::atoi(code + 3);
	switch (button) {
		case 1: case 2: glyph.kind = 10; glyph.label = bumpers[family][button - 1]; break;
		case 3: case 4: case 5: case 6: {
			// JOY3-6 are the south, east, north and west buttons.
			const int position = button == 3 ? 0 : button == 4 ? 1 : button == 5 ? 3 : 2;
			const int label = face[position];
			if (label >= 1 && label <= 4) { glyph.kind = 1; glyph.label = letters[label]; }
			else if (label >= 5 && label <= 8) glyph.kind = label - 3;
			else glyph.kind = 6 + position;
			break;
		}
		case 7: case 8:
			if (family == 2) { glyph.kind = 22; glyph.label = button == 7 ? "OPTIONS" : "CREATE"; }
			else if (family == 3) { glyph.kind = 22; glyph.label = button == 7 ? "+" : "-"; }
			else glyph.kind = button == 7 ? 17 : 18;
			break;
		case 9: glyph.kind = 12; break;
		case 10: glyph.kind = 13; break;
		case 11: glyph.kind = 15; break;
		case 12: glyph.kind = 14; break;
		case 13: case 14: glyph.kind = 16; glyph.label = sticks[family][button - 13]; break;
		case 15: glyph.kind = 11; glyph.label = triggers[family][1]; break;
		case 16: glyph.kind = 11; glyph.label = triggers[family][0]; break;
		case 17: glyph.kind = 19; break;
		case 18: glyph.kind = 20; break;
		case 19: case 20: case 21: case 22: {
			// The right and left upper paddles, then the lower ones.
			static const char* const deck[4] = {"R4","L4","R5","L5"};
			static const char* const elite[4] = {"P1","P3","P2","P4"};
			glyph.kind = 21; glyph.label = family == 4 ? deck[button - 19] : elite[button - 19];
			break;
		}
		default: glyph.kind = 22; glyph.label = idKeyInput::KeyNumToString(key,true); break;
	}
	return glyph;
}

// The first bound key a key state names, or -1.
int FirstKeyEscape(const char* text) {
	for (const char* p = text ? std::strstr(text,"^i") : nullptr; p; p = std::strstr(p + 1,"^i")) {
		const int key = KeyEscape(p);
		if (key >= 0) return key;
	}
	return -1;
}

bool ConvertState(const char* text, size_t type, StateValue& value) {
	if (!text) return false;
	if (type == 2) value = StateText(KeyEscapeNames(text));
	else if (type == 1) {
		if (!idStr::Cmp(text,"1") || !idStr::Cmp(text,"true")) value = true;
		else if (!idStr::Cmp(text,"0") || !idStr::Cmp(text,"false")) value = false;
		else return false;
	} else {
		char* end = nullptr;
		const double number = std::strtod(text,&end);
		if (end == text || *end || !std::isfinite(number)) return false;
		value = number;
	}
	return ValidStateValue(value);
}

bool ApplicationState(const DocumentModel& model, const idDict& dictionary, StateValues& result, std::string& error,
	const std::set<std::string>* fields = nullptr) {
	const auto read = [&](const std::string& name, const StateDeclaration& declaration) {
		if (declaration.HostSourced() || name.starts_with("settings.") || NumberDraftState(name.c_str())) return true;
		if (model.detail && name == model.detail->state) return true; // The runtime writes the detail subject.
		StateValue value = declaration.initial;
		// A key state's glyph, and the key state as its label.
		if (name.ends_with(".glyph") && declaration.initial.index() == 0) {
			const int key = FirstKeyEscape(dictionary.GetString(name.substr(0,name.size() - 6).c_str()));
			result.emplace(name,static_cast<double>(key >= 0 ? PromptGlyph(key).kind : 0));
			return true;
		}
		const auto* entry = dictionary.FindKey(name.c_str());
		if (entry && declaration.initial.index() == 2 && model.state.contains(name + ".glyph")) {
			const int key = FirstKeyEscape(entry->GetValue().c_str());
			if (key >= 0) {
				result.emplace(name,PromptGlyph(key).label);
				return true;
			}
		}
		if (entry && !ConvertState(entry->GetValue().c_str(),declaration.initial.index(),value)) {
			error = "Invalid application state: " + name; return false;
		}
		result.emplace(name,std::move(value));
		return true;
	};
	if (fields) {
		for (const auto& name : *fields) {
			const auto found = model.state.find(name);
			if (found != model.state.end() && !read(found->first,found->second)) return false;
		}
	} else for (const auto& [name,declaration] : model.state) if (!read(name,declaration)) return false;
	return true;
}

// Session navigation verbs a retained screen may request. They name session
// operations, never console commands; the session maps each one explicitly.
bool SessionMenuCommand(const std::string& command) {
	static const std::set<std::string> commands = {"continue","singlePlayer","loadGame","saveGame","multiplayer","settings",
		"mods","updates","credits","quit","resume","menuResume","restartLevel","quitToMenu",
        "campaigns","campaignNew","campaignChapters","campaignArena","campaignBack","campaignHome",
        "campaignSkill0","campaignSkill1","campaignSkill2","campaignSkill3","campaignSkill4",
        "campaignSkillTurbo","campaignSkillStart","campaignSkillBack","campaignChapterSelect","campaignChapterStart",
        "campaignChaptersBack",
        "loadBack","loadSelect","loadSave","loadDelete","loadNewer","loadOlder","loadNewGame",
        "saveBack","saveSelect","saveSave","saveOverwrite","saveNewer","saveOlder","restartBack","restartGo",
        "saveListRefresh","saveListSort","saveListFilterManual","saveListFilterAuto","saveListClear",
        "demosBack","demosSelect","demosPlay","demosDelete","demosRefresh","demosFilter",
        "demosNewer","demosOlder","modsSelect","modsLoad","deathRestart","deathLoad","deathMenu","creditsBack",
        "arenaBack","arenaTier0","arenaTier1","arenaTier2","arenaTier3","arenaTier4","arenaMatch0","arenaMatch1",
        "arenaMatch2","arenaMatch3","arenaTierPrevious","arenaTierNext","arenaEasier","arenaHarder","arenaStart",
        "arenaReset","arenaResetConfirm","arenaResetCancel","arenaRetry","arenaDismiss",
        "multiplayerJoin","multiplayerCreate","multiplayerDemos","multiplayerSetup","multiplayerBack","playerSetupClose",
        "createBack","createMap","createStart","createName","createMapCycle","createPassword","createRconPassword",
        "createMapsNewer","createMapsOlder","createBanList","createBanPick","createBanRemove",
        "joinBack","joinPick","joinServer","joinSpectate","joinNewer","joinOlder","joinRefresh","joinNewList","joinSourceInternet",
        "joinSourceLan","joinClearSort","joinSortFavorite","joinSortLocked","joinSortName","joinSortPing","joinSortPlayers",
        "joinSortGameType","joinSortMap","joinSortDedicated","joinSortRepeater","joinFavorite","joinModPrevious","joinModNext",
        "joinFilterFull","joinFilterEmpty","joinFilterPassword","joinGameTypePrevious","joinGameTypeNext","joinClearFilters",
        "noticeAction0","noticeAction1","noticeAction2","audioBack","gameBack","controlsBind","controlsClear","controlsBack","controlsTouchLayout",
        "settingsBack","settingsControls","settingsGame","settingsDisplay","settingsAudio","settingsDefaults",
        "joinConnect","joinSearch","joinSearchClear","joinSortNext",
        "mpClose","mpMainMenu","mpDisconnect","mpStockPage","mpTeamAction",
        "mpSelectPlayer","mpMute","mpFriend","mpWelcomeAction","mpVoteYes","mpVoteNo","mpCallVote",
        "mpRail","mpSettingsControls","mpSettingsGame","mpSettingsSystem","mpMatch","mpMatchSelect","mpSummaryChat",
        "mpAdminPass","mpAdminSelect","mpAdminKick","mpAdminBan","mpAdminSwitch","mpAdminBans","mpAdminBanPick","mpAdminUnban",
        "mpAdminApply","mpAdminConsole","mpRefereeLogin","mpSettingsName","mpSettingsClan",
        "arenaRestart","arenaLeave"};
	return commands.contains(command);
}

bool SessionMenuPlayerCommand(const std::string& command) {
	return command == "mpSelectPlayer" || command == "mpMute" || command == "mpFriend" ||
		command == "mpAdminSelect" || command == "mpAdminKick" || command == "mpAdminBan" || command == "mpAdminSwitch";
}

bool SessionMenuSaveCommand(const std::string& command) {
	return command == "loadSelect" || command == "saveSelect" || command == "loadSave" || command == "loadDelete";
}

// Catalog indices only mean something in the generation that published them.
// Freeze both before the session drains requests (including an earlier refresh).
std::optional<std::string> SessionMenuSaveRequest(const ActionInvocation& invocation) {
	const auto command = invocation.arguments.find("command"), index = invocation.arguments.find("index"),
		generation = invocation.arguments.find("generation");
	if (invocation.arguments.size() != 3 || command == invocation.arguments.end() || index == invocation.arguments.end() ||
		generation == invocation.arguments.end() || !std::holds_alternative<std::string>(command->second) ||
		!SessionMenuSaveCommand(std::get<std::string>(command->second)) || !std::holds_alternative<double>(index->second) ||
		!std::holds_alternative<double>(generation->second)) return {};
	const double row = std::get<double>(index->second), serial = std::get<double>(generation->second);
	if (!std::isfinite(row) || row < 0 || row > 2147483647.0 || std::floor(row) != row ||
		!std::isfinite(serial) || serial < 1 || serial > 2147483647.0 || std::floor(serial) != serial) return {};
	return std::get<std::string>(command->second)+" "+std::to_string(static_cast<int>(row))+" "+std::to_string(static_cast<int>(serial));
}

// Freeze a player operand in the typed queue. Reading card.client when Session
// drains several events would silently redirect every request to the last row.
std::optional<std::string> SessionMenuPlayerRequest(const ActionInvocation& invocation) {
	const auto command = invocation.arguments.find("command"), client = invocation.arguments.find("client"),
		identity = invocation.arguments.find("identity");
	if (invocation.arguments.size() != 3 || command == invocation.arguments.end() || client == invocation.arguments.end() ||
		identity == invocation.arguments.end() || !std::holds_alternative<std::string>(command->second) ||
		!SessionMenuPlayerCommand(std::get<std::string>(command->second)) || !std::holds_alternative<double>(client->second) ||
		!std::holds_alternative<std::string>(identity->second)) return {};
	const double slot = std::get<double>(client->second);
	const auto& token = std::get<std::string>(identity->second);
	if (!std::isfinite(slot) || slot < 0 || slot >= 32 || std::floor(slot) != slot || token.empty() || token.size() > 10 ||
		(token.size() > 1 && token[0] == '0') || std::any_of(token.begin(),token.end(),[](char c) { return c < '0' || c > '9'; })) return {};
	std::uint32_t packed = 0;
	const auto parsed = std::from_chars(token.data(),token.data()+token.size(),packed);
	if (parsed.ec != std::errc{} || parsed.ptr != token.data()+token.size()) return {};
	return std::get<std::string>(command->second)+" "+std::to_string(static_cast<int>(slot))+" "+token;
}

// Session verbs a value control may request with its new value: the
// multiplayer card's Vote page fields, its Settings pages' model lists and
// crosshair, its Match page's role, ballot target, rule value and series
// format and its Admin page's server settings, and the Create Server page's
// settings. The
// session maps each verb to its field and the game, or for Create Server the
// session, checks the value against the field's rules.
bool SessionMenuValueCommand(const std::string& command) {
	static const std::set<std::string> commands = {"mpVoteMap","mpVoteGameType","mpVoteTimeLimit","mpVoteFragLimit",
		"mpVoteCaptureLimit","mpVoteTourneyLimit","mpVoteControlTime","mpVoteBalance","mpVoteShuffle","mpVoteRestart",
		"mpVoteBuying","mpVoteKick","mpModelSelf","mpModelEnemy","mpModelTeam","mpCrosshair","mpMatchRole","mpMatchScope","mpMatchRuleValue",
		"mpMatchSeriesProfile",
		"mpAdminMap","mpAdminGameType","mpAdminTimeLimit","mpAdminFragLimit","mpAdminCaptureLimit","mpAdminTourneyLimit",
		"mpAdminControlTime","mpAdminBalance","mpAdminShuffle","mpAdminRestart","mpAdminBuying",
		"createGameType","createLan","createDedicated","createAutoBalance","createFragLimit","createCaptureLimit","createControlTime",
		"createTimeLimit","createMaxPlayers","createMinPlayers","createVoting","createTeamDamage","createTourneyLimit","createUsePass",
		"createServerMods","createWeaponStay","createPure","createRate","createReady","createSpectators","createWarmup","createVoice",
		"createBuying","createBots","createBotSkill",
		"audioVolume","audioMusic","audioDialogue","audioBackend","audioDevice","audioEax","audioSpeakers","audioHrtf",
		"audioSubtitles","audioSubtitleSize","audioSubtitleBackground","audioSubtitleSpeakers",
		"gameGunPosition","gameForceModel","gameCrosshair","gameCorpseTime","gameLanguage"};
	return commands.contains(command);
}

// The player settings a retained page may change ("settings.player.set"):
// the multiplayer card's Settings and Voice pages and the GAME OPTIONS page
// (tools/ui/retained_game.py). Each takes what its stock control offers: a
// slider's range (whole numbers for an integer setting), a Boolean, or one
// of a list's values, as text (which pages read back as the setting's text,
// so a float list matches exactly) or as numbers.
struct PlayerSetting {
	size_t type; double minimum = 0, maximum = 0; bool integer = false; std::vector<std::string> choices; std::vector<double> values;
};
const PlayerSetting* FindPlayerSetting(const std::string& cvar) {
	static const std::vector<std::string> levels = {"0","0.35","0.7","1"};
	static const std::vector<std::string> effects = {"1 0.12 0.05","1 0.45 0.05","1 0.9 0.15","0.1 0.85 0.25","0.1 0.9 0.95","0.3 0.45 1",
		"1 0.15 0.85","1 1 1"};
	static const std::vector<std::string> bright = {"1 0.05 0.02","1 0.4 0.02","1 0.88 0.1","0.05 1 0.22","0.05 0.9 1","0.25 0.4 1",
		"1 0.1 0.85","1 1 1"};
	static const std::map<std::string,PlayerSetting> settings = {
		{"ui_handicap",{0,1,100,true}},
		{"cl_player_outline_enemy",{2,0,0,false,levels}},{"cl_player_outline_team",{2,0,0,false,levels}},
		{"cl_player_rimlight_enemy",{2,0,0,false,levels}},{"cl_player_rimlight_team",{2,0,0,false,levels}},
		{"cl_player_brightskin_enemy",{2,0,0,false,levels}},{"cl_player_brightskin_team",{2,0,0,false,levels}},
		{"cl_player_visibility_enemy_color",{2,0,0,false,effects}},{"cl_player_visibility_team_color",{2,0,0,false,effects}},
		{"cl_player_brightskin_enemy_color",{2,0,0,false,bright}},{"cl_player_brightskin_team_color",{2,0,0,false,bright}},
		{"cl_player_outline_width",{2,0,0,false,{"1.0","1.5","2.0","3.0","4.0","6.0"}}},
		{"s_voiceChatSend",{1}},{"s_voiceChatReceive",{1}},{"s_voiceChatEcho",{1}},
		{"s_voiceChatCapture",{1}},{"s_voiceChatTest",{1}},
		{"s_voiceVolume",{0,0,1}},{"s_micInputLevel",{0,0,10}},
		// The GAME OPTIONS page, in its order.
		{"in_freeLook",{1}},{"ui_autoReload",{1}},{"ui_autoSwitch",{1}},{"in_toggleZoom",{1}},{"pm_zoomedSlow",{0,1,100,true}},
		{"g_decals",{1}},{"ui_showGun",{1}},{"g_simpleItems",{0,0,3,true,{},{0,1,2,3}}},
		{"g_mpFlatOpponentWeapons",{0,0,1,true,{},{0,1}}},{"m_pitch",{2,0,0,false,{"0.022","-0.022"}}},{"m_smooth",{0,1,8,true}},
		{"sensitivity",{0,0.01,30,false}},{"m_cpi",{0,0,32000,false}},{"m_filter",{0,0,31,true}},{"cl_mouseAccel",{0,-5,5,false}},
		{"cl_mouseAccelOffset",{0,0,50,false}},{"cl_mouseAccelPower",{0,1,8,false}},{"cl_mouseSensCap",{0,0,100,false}},
		{"in_joystick",{1}},{"in_joystickSouthpaw",{0,0,1,true,{},{0,1}}},{"in_joystickDeadZone",{0,0,0.95,false}},
		{"in_joystickLookSensitivity",{0,0.1,4,false}},{"in_weaponWheelSensitivity",{0,0.25,16,false}},
		{"ui_weaponWheelOffset",{0,-40,40,true}},{"in_joystickLookCurve",{0,1,3,false}},{"in_joystickInvertLook",{1}},
		{"in_joystickTriggerThreshold",{0,0,1,false}},{"in_joystickRumble",{1}},{"in_joystickRumbleScale",{0,0,2,false}},{"in_gyro",{1}},
		{"in_gyroSensitivity",{0,0,8,false}},{"in_touchpadMode",{0,0,3,true,{},{0,1,2,3}}},{"in_touchpadSensitivity",{0,0,8,false}},
		{"in_touchscreen",{1}},{"in_joystickLowBatteryRumbleThreshold",{0,0,100,true}},
		{"in_joystickLowBatteryRumbleScale",{0,0,2,false}},{"in_promptStyle",{0,0,5,true,{},{0,1,2,3,4,5}}},
		{"in_swapConfirmBack",{0,0,2,true,{},{0,1,2}}},{"g_crosshairCustom",{2,0,0,false,{"0","1"}}},
		{"g_crosshairSize",{0,16,48,true,{},{16,24,32,40,48}}},
		{"g_crosshairColor",{2,0,0,false,{"1 1 1 1","1 0 0 1","1 0.5 0 1","1 1 0 1","0 1 0 1","0 1 1 1","0 0 1 1","1 0 0.5 1"}}},
		{"hud_hitMarker",{1}},{"hud_crosshairAnimated",{1}},{"ui_hudLayout",{0,0,3,true,{},{0,1,2,3}}},{"ui_hudScale",{0,0.5,2,false}},
		{"ui_hudWidth",{0,0,2,true,{},{0,1,2}}},{"ui_hudSafeInset",{0,0,10,false}},{"g_objectivesToggle",{1}},
		{"ui_hudOpacity",{0,0.25,1,false}},{"ui_hudDamageDirection",{1}},{"ui_hudPickups",{1}},{"ui_hudWeaponStrip",{1}},
		{"ui_reducedFlashing",{1}},{"ui_retainedOpaqueBacking",{1}},{"g_corpseSink",{0,0,2,true,{},{0,1,2}}},{"con_allowConsole",{0,0,1,true,{},{0,1}}},
		{"g_autoSkipCinematics",{1}},{"ui_cinematicShake",{0,0,1,false}},
		{"cl_gunfov",{0,0,179,false}},{"cl_gunfov_adjust",{0,0,1,true,{},{0,1}}},
		{"cl_gun_x",{0,-5,5,false}},{"cl_gun_y",{0,-5,5,false}},{"cl_gun_z",{0,-5,5,false}},{"g_weaponMuzzleKick",{1}},
		{"vr_enable",{0,0,1,true,{},{0,1}}},{"vr_aimMode",{0,0,1,true,{},{1,0}}},{"vr_leftHanded",{0,0,1,true,{},{0,1}}},
		{"vr_turnMode",{0,0,1,true,{},{0,1}}},{"vr_snapTurnAngle",{0,30,90,true,{},{30,45,60,90}}},
		{"vr_moveDirection",{0,0,1,true,{},{0,1}}},{"vr_roomScale",{0,0,1,true,{},{0,1}}},{"vr_aimLaser",{0,0,2,true,{},{0,1,2}}},
		{"vr_hapticStrength",{2,0,0,false,{"0","0.5","1"}}},{"vr_twoHanded",{0,0,1,true,{},{0,1}}},
		{"vr_comfortVignette",{2,0,0,false,{"0","0.5","1"}}},{"vr_physicalCrouch",{0,0,1,true,{},{0,1}}},
		{"vr_playSpace",{0,0,2,true,{},{0,1,2}}}};
	const auto found = settings.find(cvar);
	return found == settings.end() ? nullptr : &found->second;
}

bool PlayerSettingValue(const std::string& cvar, const StateValue& value) {
	const auto* setting = FindPlayerSetting(cvar);
	if (!setting || value.index() != setting->type) return false;
	if (setting->type == 1) return true;
	if (setting->type == 2) return std::find(setting->choices.begin(),setting->choices.end(),std::get<std::string>(value)) != setting->choices.end();
	const double number = std::get<double>(value);
	if (!setting->values.empty()) return std::find(setting->values.begin(),setting->values.end(),number) != setting->values.end();
	return std::isfinite(number) && number >= setting->minimum && number <= setting->maximum && (!setting->integer || number == std::floor(number));
}

// A value a session verb carries: a whole number from -1 to 999, or a
// Boolean as 1 or 0.
// A value verb's operand: a whole number from -1 to 999, or for a Match
// Control rule's value from 0 to 10000, the largest value a rule takes.
std::optional<int> SessionMenuValue(const std::string& command, const StateValue& value) {
	if (std::holds_alternative<bool>(value)) return std::get<bool>(value) ? 1 : 0;
	if (!std::holds_alternative<double>(value)) return {};
	const double number = std::get<double>(value);
	const bool rule = command == "mpMatchRuleValue";
	if (!std::isfinite(number) || number != std::floor(number) || number < (rule ? 0 : -1) || number > (rule ? 10000 : 999)) return {};
	return static_cast<int>(number);
}

// A kick choice carries the exact ordered connection catalog that produced its row.
bool ValidVoteCatalog(const std::string& text) {
	return !text.empty() && text.size() <= 512 && std::all_of(text.begin(),text.end(),[](char c) {
		return (c >= '0' && c <= '9') || c == ':' || c == ',';
	});
}

bool ValidOperation(const Action& action) {
	if (action.operation == "player.setup.apply") return ValidPlayerSetupAction(action);
	if (action.operation.starts_with("settings.system.")) { std::string error; return UI_SettingsOperation(action,error); }
	if (action.operation == "ui.dismiss" || action.operation == "ui.numberDrafts.focus") return action.arguments.empty();
	if (action.operation == "ui.state.set") {
		// A field writes its own document state: a literal key, which
		// ValidateApplication checks against the document, the control's own
		// value and, optionally, a literal event to run once it is written.
		const auto key = action.arguments.find("key"), value = action.arguments.find("value"), event = action.arguments.find("event");
		const auto literal = [](const Expression& expression) {
			return expression.type == 2 && expression.op.empty() && expression.state.empty() && expression.presentation.empty() &&
				!expression.inputValue;
		};
		if (key == action.arguments.end() || value == action.arguments.end() || !value->second.inputValue ||
			action.arguments.size() != (event == action.arguments.end() ? 2u : 3u)) return false;
		return literal(key->second) && (event == action.arguments.end() || literal(event->second));
	}
	if (action.operation == "session.menu") {
		const auto command = action.arguments.find("command");
		if (command == action.arguments.end() || command->second.type != 2) return false;
		const auto& expression = command->second;
		const bool literal = expression.op.empty() && expression.state.empty() && expression.presentation.empty() && !expression.inputValue;
		if (literal && SessionMenuSaveCommand(std::get<std::string>(expression.literal))) {
			const auto index = action.arguments.find("index"), generation = action.arguments.find("generation");
			return action.arguments.size() == 3 && index != action.arguments.end() && index->second.type == 0 &&
				generation != action.arguments.end() && generation->second.type == 0;
		}
		if (literal && SessionMenuPlayerCommand(std::get<std::string>(expression.literal))) {
			const auto client = action.arguments.find("client"), identity = action.arguments.find("identity");
			return action.arguments.size() == 3 && client != action.arguments.end() && client->second.type == 0 &&
				identity != action.arguments.end() && identity->second.type == 2;
		}
		return action.arguments.size() == 1 && (!literal || SessionMenuCommand(std::get<std::string>(expression.literal)));
	}
	if (action.operation == "settings.player.set") {
		// A literal setting, and the control's value or a literal one it takes.
		const auto cvar = action.arguments.find("cvar"), value = action.arguments.find("value");
		if (action.arguments.size() != 2 || cvar == action.arguments.end() || value == action.arguments.end() || cvar->second.type != 2) return false;
		const auto literal = [](const Expression& expression) {
			return expression.op.empty() && expression.state.empty() && expression.presentation.empty() && !expression.inputValue;
		};
		const auto* setting = literal(cvar->second) ? FindPlayerSetting(std::get<std::string>(cvar->second.literal)) : nullptr;
		if (!setting || value->second.type != setting->type) return false;
		return value->second.inputValue || (literal(value->second) && PlayerSettingValue(std::get<std::string>(cvar->second.literal),value->second.literal));
	}
	if (action.operation == "session.menuValue") {
		// A literal value verb, and the control's own value as its operand.
		const auto command = action.arguments.find("command"), value = action.arguments.find("value");
		if (command == action.arguments.end() || value == action.arguments.end() ||
			command->second.type != 2 || !value->second.inputValue || (value->second.type != 0 && value->second.type != 1)) return false;
		const auto& expression = command->second;
		if (!expression.op.empty() || !expression.state.empty() || !expression.presentation.empty() || expression.inputValue ||
			!SessionMenuValueCommand(std::get<std::string>(expression.literal))) return false;
		const auto catalog = action.arguments.find("catalog");
		return std::get<std::string>(expression.literal) == "mpVoteKick" ?
			action.arguments.size() == 3 && catalog != action.arguments.end() && catalog->second.type == 2 : action.arguments.size() == 2;
	}
	const auto value = action.arguments.find("value");
	if (action.arguments.size() != 1 || value == action.arguments.end()) return false;
	return (action.operation == "settings.brightness.set" && value->second.type == 0) ||
		(action.operation == "settings.shadows.set" && value->second.type == 1);
}

bool ValidInvocation(const ActionInvocation& invocation, std::string& error) {
	if (invocation.operation == "player.setup.apply") {
		PlayerSetupRequest request; return ResolvePlayerSetupRequest(invocation.arguments,request,error);
	}
	if (invocation.operation.starts_with("settings.system.")) return UI_SettingsInvocation(invocation,error);
	if ((invocation.operation == "ui.dismiss" || invocation.operation == "ui.numberDrafts.focus") && invocation.arguments.empty()) return true;
	if (invocation.operation == "ui.state.set") {
		const auto key = invocation.arguments.find("key"), value = invocation.arguments.find("value"), event = invocation.arguments.find("event");
		if (key != invocation.arguments.end() && value != invocation.arguments.end() &&
			invocation.arguments.size() == (event == invocation.arguments.end() ? 2u : 3u) &&
			std::holds_alternative<std::string>(key->second) && ValidStateValue(value->second) &&
			(event == invocation.arguments.end() || std::holds_alternative<std::string>(event->second))) return true;
		error = "Unsupported field state write: "+invocation.action; return false;
	}
	if (invocation.operation == "session.menu") {
		const auto command = invocation.arguments.find("command");
		if (command != invocation.arguments.end() && std::holds_alternative<std::string>(command->second) &&
			SessionMenuSaveCommand(std::get<std::string>(command->second))) {
			if (SessionMenuSaveRequest(invocation)) return true;
			error = "Invalid save catalog identity: "+invocation.action; return false;
		}
		if (command != invocation.arguments.end() && std::holds_alternative<std::string>(command->second) &&
			SessionMenuPlayerCommand(std::get<std::string>(command->second))) {
			if (SessionMenuPlayerRequest(invocation)) return true;
			error = "Invalid multiplayer player identity: "+invocation.action; return false;
		}
		if (invocation.arguments.size() == 1 && command != invocation.arguments.end() &&
			std::holds_alternative<std::string>(command->second) && SessionMenuCommand(std::get<std::string>(command->second))) return true;
		error = "Unsupported session menu request: "+invocation.action; return false;
	}
	if (invocation.operation == "settings.player.set") {
		const auto cvar = invocation.arguments.find("cvar"), value = invocation.arguments.find("value");
		if (invocation.arguments.size() == 2 && cvar != invocation.arguments.end() && value != invocation.arguments.end() &&
			std::holds_alternative<std::string>(cvar->second) && PlayerSettingValue(std::get<std::string>(cvar->second),value->second)) return true;
		error = "Unsupported player setting request: "+invocation.action; return false;
	}
	if (invocation.operation == "session.menuValue") {
		const auto command = invocation.arguments.find("command"), value = invocation.arguments.find("value");
		if (command != invocation.arguments.end() && value != invocation.arguments.end() &&
			std::holds_alternative<std::string>(command->second) && SessionMenuValueCommand(std::get<std::string>(command->second)) &&
			SessionMenuValue(std::get<std::string>(command->second),value->second)) {
			const auto catalog = invocation.arguments.find("catalog");
			if (std::get<std::string>(command->second) == "mpVoteKick") {
				if (invocation.arguments.size() == 3 && catalog != invocation.arguments.end() &&
					std::holds_alternative<std::string>(catalog->second) && ValidVoteCatalog(std::get<std::string>(catalog->second))) return true;
			} else if (invocation.arguments.size() == 2) return true;
		}
		error = "Unsupported session value request: "+invocation.action; return false;
	}
	const auto value = invocation.arguments.find("value");
	if (invocation.arguments.size() == 1 && value != invocation.arguments.end() && ValidStateValue(value->second)) {
		if (invocation.operation == "settings.brightness.set" && std::holds_alternative<double>(value->second)) {
			const double number = std::get<double>(value->second);
			if (number >= .5 && number <= 2) return true;
			error = "Brightness request must be between 0.5 and 2.0"; return false;
		}
		if (invocation.operation == "settings.shadows.set" && std::holds_alternative<bool>(value->second)) return true;
	}
	error = "Unsupported application invocation or arguments: "+invocation.action; return false;
}

void TraceInvocation(const char* path, const ActionInvocation& invocation, bool close) {
	if (!cvarSystem->GetCVarBool("ui_retainedTrace")) return;
	std::string text = "-";
	const auto value = invocation.arguments.find("value");
	if (value != invocation.arguments.end()) {
		PresentationValue number;
		if (std::holds_alternative<double>(value->second)) number.data[0] = std::get<double>(value->second);
		else if (std::holds_alternative<bool>(value->second)) { number.type = PresentationType::Boolean; number.data[0] = std::get<bool>(value->second) ? 1 : 0; }
		text = FormatPresentationValue(number);
	}
	common->Printf("RETAINED_GUI_DISPATCH path=%s operation=%s value=%s brightness=%.6f shadows=%d close=%d\n",
		path,invocation.operation.c_str(),text.c_str(),cvarSystem->GetCVarFloat("r_brightness"),
		cvarSystem->GetCVarBool("r_shadows") ? 1 : 0,close ? 1 : 0);
}

bool ValidateApplication(const DocumentModel& model, std::string& error) {
	idDict names;
	for (const auto& [name,declaration] : model.state) {
		if (!idStr::Icmp(name.c_str(),"name")) { error = "State ID is reserved by the GUI source contract: " + name; return false; }
		if (names.FindKey(name.c_str())) { error = "State IDs collide in the game dictionary: " + name; return false; }
		if (NumberDraftState(name.c_str()) && (declaration.HostSourced() ||
			(name != NumberDraftPending && name != NumberDraftMessage) ||
			declaration.initial.index() != (name == NumberDraftPending ? 1u : 2u))) {
			error = "Invalid adapter-owned number draft declaration: " + name; return false;
		}
		if (!idStr::Icmpn(name.c_str(),"settings.",9)) {
			const auto& schema = UI_SettingsStateSchema(); const auto field = schema.find(name);
			if (field == schema.end() || field->second != declaration.initial.index() || declaration.HostSourced()) {
				error = "Invalid service-owned settings state declaration: " + name; return false;
			}
		}
		names.Set(name.c_str(),"1");
	}
	for (const auto& [name,event] : model.events) {
		std::vector<const EventStep*> steps;
		for (const auto& step : event.steps) steps.push_back(&step);
		while (!steps.empty()) {
			const auto* step = steps.back(); steps.pop_back();
			for (const auto& [key,value] : step->values) if (!idStr::Icmpn(key.c_str(),"settings.",9) || NumberDraftState(key.c_str())) {
				error = "Programs cannot overwrite engine-owned state: " + key; return false;
			}
			for (const auto& child : step->thenSteps) steps.push_back(&child);
			for (const auto& child : step->elseSteps) steps.push_back(&child);
		}
	}
	for (const auto& [name,action] : model.actions) {
		if (!ValidOperation(action)) { error = "Unsupported application operation or arguments: " + name; return false; }
		if (action.operation == "ui.state.set") {
			// The document's own declared state of the field's type, never
			// engine-owned or CVar-bound state, and an event it declares.
			const auto& key = std::get<std::string>(action.arguments.at("key").literal);
			const auto declaration = model.state.find(key);
			const auto event = action.arguments.find("event");
			if (declaration == model.state.end() || declaration->second.HostSourced() || !idStr::Icmpn(key.c_str(),"settings.",9) ||
				NumberDraftState(key.c_str()) || !action.inputType || *action.inputType != declaration->second.initial.index() ||
				(event != action.arguments.end() && !model.events.contains(PresentationAliasKey(std::get<std::string>(event->second.literal))))) {
				error = "A field state write needs the document's own state of the field's type, and its event: " + name; return false;
			}
		}
	}
	std::vector<const Node*> pending{&model.root};
	while (!pending.empty()) {
		const auto* node = pending.back(); pending.pop_back();
		if (node->control && node->control->role == ControlRole::Scrollbar) {
			// Scrolling changes the runtime viewport; it never dispatches an application action.
			const auto& control = *node->control;
			if (!control.action.empty() || !control.event.empty() || control.value ||
				!std::holds_alternative<ScrollSpec>(control.widget)) {
				error = "Scrollbar has invalid application output: " + node->id; return false;
			}
		} else if (node->control && (node->control->event.empty() ? !model.actions.contains(node->control->action) :
			!model.events.contains(PresentationAliasKey(node->control->event)))) {
			error = "Control has no typed application action or event: " + node->id; return false;
		}
		for (const auto& child : node->children) pending.push_back(&child);
	}
	return true;
}

bool HasControls(const Node& node) {
	if (node.control) return true;
	for (const auto& child : node.children) if (HasControls(child)) return true;
	return false;
}

// Pictures the document names itself resolve when it loads; bound sources
// are precached by whoever publishes them (UI_RetainedPrecacheImage).
void PrecacheImages(const Node& node) {
	const auto image = node.properties.find("image");
	if (image != node.properties.end() && image->second.type == ValueType::Image && !image->second.text.empty()) {
		const auto blend = node.properties.find("image-blend");
		RetainedUI_PrecacheImage((HasImageCoordinates(node) ? "_repeat/" : "")+image->second.text,
			blend != node.properties.end() && blend->second.text == "additive");
	}
	for (const auto& child : node.children) PrecacheImages(child);
}

bool NonInteractive(const idDict& dictionary) {
	StateValue value;
	if (ConvertState(dictionary.GetString("noninteractive","0"),1,value)) return std::get<bool>(value);
	// Undeclared legacy caller flags retain their numeric dictionary semantics.
	return dictionary.GetBool("noninteractive");
}

// The existing game/SDL interface transports a 640x480 aspect-corrected
// cursor. Invert that transform once at the retained window-unit boundary.
struct CursorTransform {
	float sx = 1, sy = 1, ox = 0, oy = 0;
	explicit CursorTransform(const Viewport& viewport) {
		sx = viewport.width / 640.f; sy = viewport.height / 480.f;
		if (cvarSystem->GetCVarBool("ui_aspectCorrection")) {
			sx = sy = Min(sx,sy);
			ox = (viewport.width-640.f*sx)*.5f; oy = (viewport.height-480.f*sy)*.5f;
		}
	}
};

// A Unicode scalar value as UTF-8; empty for a surrogate or past U+10FFFF.
std::string Utf8Scalar(int scalar) {
	std::string out;
	if (scalar < 0 || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff)) return out;
	const auto byte = [&](int value) { out += static_cast<char>(value); };
	if (scalar < 0x80) byte(scalar);
	else if (scalar < 0x800) { byte(0xc0 | scalar >> 6); byte(0x80 | (scalar & 0x3f)); }
	else if (scalar < 0x10000) { byte(0xe0 | scalar >> 12); byte(0x80 | (scalar >> 6 & 0x3f)); byte(0x80 | (scalar & 0x3f)); }
	else { byte(0xf0 | scalar >> 18); byte(0x80 | (scalar >> 12 & 0x3f)); byte(0x80 | (scalar >> 6 & 0x3f)); byte(0x80 | (scalar & 0x3f)); }
	return out;
}

bool ReadString(idFile& file, std::string& value, int limit, int& budget) {
	int length = 0;
	if (file.ReadInt(length) != 4 || length < 0 || length > limit || length > budget ||
		length > file.Length()-file.Tell()) return false;
	std::string candidate(static_cast<size_t>(length),'\0');
	if (length && file.Read(candidate.data(),length) != length) return false;
	if (candidate.find('\0') != std::string::npos) return false;
	budget -= length; value = std::move(candidate); return true;
}
bool WriteString(idFile& file, const std::string& value) {
	return value.size() <= static_cast<size_t>((std::numeric_limits<int>::max)()) &&
		file.WriteInt(static_cast<int>(value.size())) == 4 &&
		(value.empty() || file.Write(value.data(),static_cast<int>(value.size())) == static_cast<int>(value.size()));
}
}

struct idUserInterfaceRetained::Impl {
	idDict state;
	// The public dictionary is a pending batch. Keep it intact for saves and
	// legacy callers, but only parse fields written since the last commit.
	std::map<std::string,std::string> applicationNames;
	std::set<std::string> dirtyApplication, promptApplication;
	std::array<int,5> promptAppearance{-1,0,0,0,0};
	bool fullApplication = true, hasControls = false;
	void IndexApplication() {
		applicationNames.clear(); promptApplication.clear(); dirtyApplication.clear(); fullApplication = true;
		promptAppearance[0] = -1;
		for (const auto& [name,declaration] : document.Model().state) {
			idStr folded(name.c_str()); folded.ToLower(); applicationNames.emplace(folded.c_str(),name);
			// Device glyphs can change without the binding string changing.
			if (name.ends_with(".glyph")) { promptApplication.insert(name); promptApplication.insert(name.substr(0,name.size()-6)); }
		}
		hasControls = HasControls(document.Model().root);
	}
	void DirtyApplication(const char* name) {
		if (fullApplication) return;
		idStr folded(name); folded.ToLower();
		const auto found = applicationNames.find(folded.c_str());
		if (found != applicationNames.end()) {
			dirtyApplication.insert(found->second);
			// A binding string and its generated glyph form one input. Either
			// write must refresh both, including deletion and program writes.
			if (promptApplication.contains(found->second)) dirtyApplication.insert(found->second.ends_with(".glyph") ?
				found->second.substr(0,found->second.size()-6) : found->second+".glyph");
		}
	}
	void SetDictionary(const char* name, const char* value) {
		const auto* before = state.FindKey(name);
		if (before && before->GetValue() == value) return;
		state.Set(name,value); DirtyApplication(name);
	}
	bool ReadApplication(StateValues& values, std::string& error) {
		if (!promptApplication.empty()) {
			std::array<int,5> appearance{};
#if defined(USE_SDL3)
			appearance[0] = idMath::ClampInt(0,4,Sys_SDL_GamepadGlyphFamily(appearance.data()+1));
#else
			// Older platform backends can map printable keys through a live OS
			// layout without publishing a generation. Keep their original polling.
			dirtyApplication.insert(promptApplication.begin(),promptApplication.end());
#endif
			if (appearance != promptAppearance) {
				dirtyApplication.insert(promptApplication.begin(),promptApplication.end());
				promptAppearance = appearance;
			}
		}
		return ApplicationState(document.Model(),state,values,error,fullApplication ? nullptr : &dirtyApplication);
	}
	void ApplicationCommitted() { fullApplication = false; dirtyApplication.clear(); }
	idStr path;
	ID_TIME_T stamp = 0;
	Document document;
	retainedUIView_t* view = nullptr;
	const std::uint64_t textBackend = UI_NextTextLifetime();
	std::uint64_t textDocument = 0;
	Input input;
	std::set<int> held;
	struct PendingAction {
		ActionInvocation invocation;
		bool cancellable = true;
		std::string control;
		std::uint64_t proposalToken = 0;
		ControlAction source;
		std::optional<uiClipboardRequest_t> clipboard;
	};
	std::vector<PendingAction> actions;
	// Accepted session.menu verbs, in order, for the session to take after a
	// dispatch; they never survive a source, save or resource replacement.
	std::vector<std::string> sessionRequests;
	// A number or text field's value sent to the session comes back through the
	// field's bound state a frame or two later. The runtime accepts a number
	// only once it reads it back, so its proposal is acknowledged then, and
	// refused if the value has not come back within a second.
	struct SessionNumberProposal { std::string control; std::uint64_t token = 0; StateValue value = 0.0; double deadline = 0; };
	std::vector<SessionNumberProposal> sessionNumbers;
	void SettleSessionNumbers() {
		auto* runtime = RuntimeView();
		if (!runtime) return;
		const double now = RetainedUI_PresentationTime();
		for (auto it = sessionNumbers.begin(); it != sessionNumbers.end();) {
			const auto widget = runtime->GetWidgetState(it->control);
			const bool current = widget && widget->proposalToken == it->token;
			const bool readBack = current && widget->accepted == it->value;
			if (current && !readBack && now < it->deadline) { ++it; continue; }
			if (current) runtime->AcknowledgeControlProposal(it->control,it->token,readBack);
			it = sessionNumbers.erase(it);
		}
	}
	bool interactive = true, interactiveSet = false, unique = false, active = false;
	bool suspended = false, close = false, worldReported = false;
	// openq4_retainedGui pointer placed the cursor: it shows although the
	// hidden capture window reads as unfocused.
	bool pointerPlaced = false;
	bool unavailable = false;
	bool drawSucceeded = false;
	bool initialized = false;
	bool settingsFields = false, settingsClosePending = false;
	std::uint64_t settingsOwner = UI_SettingsCreateOwner();
	float cursorX = 320, cursorY = 240;
	float routedPointerX = 0, routedPointerY = 0;
	bool routedPointerValid = false;
	std::string lastError;
	std::string checkpoint;

	~Impl() { openq4::NativeInputBeforeUiChange(0,textBackend); UI_SettingsReleaseOwner(settingsOwner); RetainedUI_DestroyView(view); }
	Runtime* RuntimeView() const { return RetainedUI_ViewRuntime(view); }
	bool NativeOwnerMatches(const TextEditorIdentity& owner, bool eligible) const noexcept {
		return owner.allocation && owner.backend && owner.document &&
			owner.backend == textBackend && owner.document == textDocument &&
			(!eligible || (initialized && active && interactive && !suspended && !unavailable && !close));
	}
	void Error(const std::string& message) {
		if (message != lastError) common->Warning("retained GUI %s: %s",path.c_str(),message.c_str());
		lastError = message;
	}
	bool CallerState(const char* name) {
		if (!NumberDraftState(name)) return true;
		Error("Callers cannot overwrite adapter-owned number draft state"); return false;
	}
	void Quarantine(bool forget = false, bool cancelRuntime = true, bool discardPrograms = false) {
		openq4::NativeInputBeforeUiChange(0,textBackend);
		input.Cancel(forget); input.Take(); held.clear(); close = false; pointerPlaced = false;
		routedPointerValid = false;
		// Completed programs retain their immutable invocations through input
		// suspension. Save/resource/source replacement explicitly discards them.
		actions.erase(std::remove_if(actions.begin(),actions.end(),[&](const PendingAction& action) {
			const bool discard = discardPrograms || action.cancellable;
			if (discard && action.proposalToken) if (auto* runtime = RuntimeView())
				runtime->AcknowledgeControlProposal(action.control,action.proposalToken,false);
			return discard;
		}),actions.end());
		if (discardPrograms) {
			sessionRequests.clear();
			if (auto* runtime = RuntimeView())
				for (const auto& number : sessionNumbers) runtime->AcknowledgeControlProposal(number.control,number.token,false);
			sessionNumbers.clear();
		}
		if (auto* runtime = RuntimeView()) {
			if (cancelRuntime) runtime->CancelInput(RetainedUI_PresentationTime());
			runtime->ReleaseInputSources(); runtime->TakeActions();
		}
	}
	static void ResourceEvent(void* owner, retainedUIViewEvent_t event) {
		auto& self = *static_cast<Impl*>(owner);
		// Allocate before teardown. Exhaustion permanently disables text ownership
		// for this document; restored editors cannot inherit its old native lease.
		if (event == retainedUIViewEvent_t::BeforeResourceReset) {
			openq4::NativeInputBeforeUiChange(0,self.textBackend);
			self.textDocument = UI_NextTextLifetime();
			if (!self.textDocument) self.Error("GUI text lifetime exhausted");
		}
		if (event == retainedUIViewEvent_t::BeforeResourceReset) self.Quarantine(false,false,true);
		else if (event == retainedUIViewEvent_t::Failed) self.Quarantine(false,false,true);
		if (event == retainedUIViewEvent_t::Restored) self.promptAppearance[0] = -1; // Localized labels may have changed.
		if (event != retainedUIViewEvent_t::BeforeResourceReset) common->Printf("RETAINED_GUI_RESOURCE path=%s event=%s\n",
			self.path.c_str(),event == retainedUIViewEvent_t::Restored ? "restored" : "failed");
	}
	bool Prepare() {
		const bool ready = view && RetainedUI_PrepareView(view);
		if (!ready && !unavailable) Quarantine();
		unavailable = !ready;
		return ready && SyncSettings();
	}
	bool SyncSettings() {
		if (!settingsFields) return SyncNumberDrafts();
		StateValues current;
		if (!UI_SettingsRead(settingsOwner,current)) return false;
		// One lookup per key. This runs on every Prepare - each redraw, event and
		// value query - and copying the document's whole state to compare these
		// few keys cost a map of every variable it declares each time.
		StateValues updates, published;
		for (const auto& [key,declaration] : document.Model().state) if (key.starts_with("settings.")) {
			const auto source = current.find(key);
			const auto& value = source == current.end() ? declaration.initial : source->second;
			published.emplace(key,value);
			StateValue before;
			if (declaration.HostSourced() || !RuntimeView()->GetStateValue(key,before) || before != value) updates.emplace(key,value);
		}
		std::string error;
		if (!updates.empty() && !RuntimeView()->SetState(updates,error,RetainedUI_PresentationTime())) { Error(error); return false; }
		for (const auto& [key,value] : published) {
			PresentationValue text;
			if (std::holds_alternative<std::string>(value)) { text.type = PresentationType::String; text.text = std::get<std::string>(value); }
			else if (std::holds_alternative<bool>(value)) { text.type = PresentationType::Boolean; text.data[0] = std::get<bool>(value) ? 1 : 0; }
			else text.data[0] = std::get<double>(value);
			state.Set(key.c_str(),FormatPresentationValue(text).c_str());
		}
		return SyncNumberDrafts();
	}
	bool QueryNumberDrafts(NumberDraftSummary& summary) {
		std::string error;
		if (!RuntimeView()->QueryNumberDrafts(summary,error,RetainedUI_PresentationTime())) { Error(error); return false; }
		return true;
	}
	bool SyncNumberDrafts() {
		const auto& declarations = document.Model().state;
		if (!declarations.contains(NumberDraftPending) && !declarations.contains(NumberDraftMessage)) return true;
		NumberDraftSummary summary; if (!QueryNumberDrafts(summary)) return false;
		StateValues values;
		if (declarations.contains(NumberDraftPending)) values.emplace(NumberDraftPending,!summary.blocking.empty());
		if (declarations.contains(NumberDraftMessage)) values.emplace(NumberDraftMessage,
			std::string(summary.blocking.empty() ? "" : "#str_230006"));
		StateValues changes;
		for (const auto& [key,value] : values) {
			StateValue live;
			if (declarations.at(key).HostSourced() || !RuntimeView()->GetStateValue(key,live) || live != value) changes.emplace(key,value);
		}
		std::string error;
		if (!changes.empty() && !RuntimeView()->SetState(changes,error,RetainedUI_PresentationTime())) { Error(error); return false; }
		for (const auto& [key,value] : values) {
			if (std::holds_alternative<bool>(value)) state.SetBool(key.c_str(),std::get<bool>(value));
			else state.Set(key.c_str(),std::get<std::string>(value).c_str());
		}
		return true;
	}
	bool FocusNumberDraft() {
		NumberDraftSummary summary; if (!QueryNumberDrafts(summary)) return false;
		if (summary.blocking.empty()) return true;
		std::string error;
		if (!RuntimeView()->FocusNumberDraft(summary.barrier,summary.blocking.front().control,error,RetainedUI_PresentationTime())) {
			Error(error); return false;
		}
		return SyncNumberDrafts();
	}
	bool ConflictsWithNumberDraft(const PendingAction& pending, const NumberDraftSummary& summary) const {
		if (summary.blocking.empty()) return false;
		const auto& operation = pending.invocation.operation;
		if (operation == "settings.system.apply" || operation == "settings.system.applyExit" || operation == "settings.system.defaults" ||
            operation == "settings.system.preset" || operation == "settings.system.autodetect" ||
            operation == "settings.system.backend" || operation == "settings.system.renderDevice" || operation == "settings.system.restartRenderer" ||
            operation == "settings.system.display" || operation == "settings.system.displayMode") return true;
		if (operation != "settings.system.edit") return false;
		// A sibling slider or toggle shares the Number's typed setting keys.
		// Leave the local text intact until its owner commits or discards it.
		for (const auto& draft : summary.blocking) {
			if (pending.source.editSession && draft.control == pending.control) continue;
			const auto* node = document.Model().FindNode(draft.control);
			if (!node || !node->control) return true;
			const auto action = document.Model().actions.find(node->control->action);
			if (action == document.Model().actions.end()) return true;
			for (const auto& [key,value] : action->second.arguments) if (pending.invocation.arguments.contains(key)) return true;
		}
		return false;
	}
	void ApplyInput() {
		for (const auto& event : input.Take()) {
			if (event.kind == RoutedInput::Kind::Cancel) RuntimeView()->CancelInput(RetainedUI_PresentationTime());
			else if (event.kind == RoutedInput::Kind::PointerButton) RuntimeView()->PointerButton(event.down,RetainedUI_PresentationTime());
			else RuntimeView()->MenuAction(event.menu,event.down,RetainedUI_PresentationTime());
		}
	}
	// Prompts follow the active device (section 13.5): a document that declares
	// prompt.glyphs gets RetainedUI_PromptGlyphs in the frame it changes, the
	// keyboard's keycaps or a controller family's face buttons.
	void PublishPromptGlyphs() {
		static const char* const name = "prompt.glyphs";
		static const char* const swapped = "prompt.swapped";
		StateValues changed;
		if (document.Model().state.contains(name)) {
			const int glyphs = RetainedUI_PromptGlyphs();
			if (!state.FindKey(name) || state.GetInt(name) != glyphs) {
				state.SetInt(name,glyphs); changed.emplace(name,static_cast<double>(glyphs));
			}
		}
		// Whether east confirms: an Accept prompt shows the east button then.
		if (document.Model().state.contains(swapped)) {
			const bool swap = RetainedUI_PromptSwapped();
			if (!state.FindKey(swapped) || state.GetBool(swapped) != swap) {
				state.SetBool(swapped,swap); changed.emplace(swapped,swap);
			}
		}
		// Accept's verb follows focus in the same frame (section 13.4): the
		// focused button's own, the document's declared verb for any other
		// control, or empty while nothing focused can be activated.
		static const char* const accept = "prompt.accept";
		if (const auto declared = document.Model().state.find(accept); declared != document.Model().state.end()) {
			const auto* fallback = std::get_if<std::string>(&declared->second.initial);
			const auto verb = RuntimeView()->AcceptPrompt(fallback ? *fallback : std::string(),RetainedUI_PresentationTime());
			if (!state.FindKey(accept) || verb != state.GetString(accept)) {
				state.Set(accept,verb.c_str()); changed.emplace(accept,verb);
			}
		}
		std::string error;
		if (!changed.empty() && !RuntimeView()->SetState(changed,error,RetainedUI_PresentationTime())) Error(error);
	}
	// The right stick's vertical deflection (section 13.5), -1..1 held for
	// `seconds`: full deflection scrolls the detail area 720 dp a second.
	bool DetailStick(float y, float seconds) {
		auto* runtime = RuntimeView();
		if (!runtime || !std::isfinite(y) || y == 0 || !std::isfinite(seconds) || seconds <= 0) return false;
		return runtime->ScrollDetail(idMath::ClampFloat(-1,1,y)*720.0f*seconds,RetainedUI_PresentationTime());
	}
	// Preserve undeclared and unrelated pending dictionary keys. Publish only
	// the programs' explicit application writes, using round-trip numbers.
	void PublishWrites(const StateValues& writes) {
		for (const auto& [id,value] : writes) {
			PresentationValue text;
			if (std::holds_alternative<std::string>(value)) { text.type = PresentationType::String; text.text = std::get<std::string>(value); }
			else if (std::holds_alternative<bool>(value)) { text.type = PresentationType::Boolean; text.data[0] = std::get<bool>(value) ? 1 : 0; }
			else text.data[0] = std::get<double>(value);
			state.Set(id.c_str(),FormatPresentationValue(text).c_str());
			if (promptApplication.contains(id)) DirtyApplication(id.c_str());
		}
	}
	bool RunEvent(const std::string& name) {
		// A click can detach a field after Prepare. Programs must see its fresh
		// local draft status before deciding whether Back may close the page.
		if (!SyncNumberDrafts()) return false;
		StateValues application; std::string error;
		if (!ReadApplication(application,error)) { Error(error); return false; }
		Runtime::EventEffects effects;
		if (!RuntimeView()->RunEvent(name,RetainedUI_PresentationTime(),effects,error,application,ValidInvocation,256-actions.size())) {
			Error(error); return false;
		}
		ApplicationCommitted();
		PublishWrites(effects.stateChanges);
		const auto count = effects.actions.size();
		for (auto& action : effects.actions) actions.push_back({std::move(action),false});
		if (!interactiveSet) {
			const bool nextInteractive = hasControls && !NonInteractive(state);
			if (nextInteractive != interactive) openq4::NativeInputBeforeUiChange(0,textBackend);
			interactive = nextInteractive;
			if (!interactive) Quarantine();
		}
		lastError.clear();
		if (cvarSystem->GetCVarBool("ui_retainedTrace")) common->Printf("RETAINED_GUI_EVENT name=%s actions=%u writes=%u\n",name.c_str(),
			static_cast<unsigned>(count),static_cast<unsigned>(effects.stateChanges.size()));
		return true;
	}
	void CollectActions(bool semantic = false) {
		for (const auto& event : RuntimeView()->TakeActions()) {
			const auto reject = [&] { if (event.proposalToken)
				RuntimeView()->AcknowledgeControlProposal(event.node,event.proposalToken,false); };
			if (!interactive || event.document != document.Model().id) { reject(); continue; }
			if (event.kind == ControlAction::Kind::Back) {
				if (!RuntimeView()->CanDispatchModalBack(event,RetainedUI_PresentationTime())) continue;
				if (!event.event.empty()) {
					RunEvent(event.event);
					continue;
				}
				if (!RuntimeView()->PopModal(RetainedUI_PresentationTime())) {
					if (RuntimeView()->HasEvent("onBack")) RunEvent("onBack");
					else if (semantic && actions.size() < 256) actions.push_back({{"","ui.dismiss",{}},false});
					else if (!semantic) {
						openq4::NativeInputBeforeUiChange(0,textBackend);
						close = true;
					}
				}
				continue;
			}
			if (!RuntimeView()->CanDispatchControlAction(event,RetainedUI_PresentationTime())) { reject(); continue; }
			if (!event.event.empty()) { RunEvent(event.event); continue; }
			ActionInvocation invocation; std::string error;
			if (!RuntimeView()->ResolveAction(event.action,invocation,error,event.proposal ? &*event.proposal : nullptr) ||
				!ValidInvocation(invocation,error)) { reject(); Error(error); continue; }
			if (actions.size() >= 256) { reject(); Quarantine(); Error("Application action queue exceeded 256 requests"); return; }
			actions.push_back({std::move(invocation),!semantic,event.node,event.proposalToken,event});
		}
	}
	bool AcceptInput() {
		bool focus = true;
#if defined(USE_SDL3)
		// a headset session's controllers drive menus the desktop window can't see
		focus = Sys_SDL_IsGameWindowFocused() || VR_HasInputFocus();
#endif
		const bool pause = !active || !interactive || !focus || console->Active();
		if (pause && !suspended) Quarantine(!focus);
		suspended = pause;
		return !pause;
	}
	// A focused field that takes typed text: a number or text field.
	std::optional<std::string> FocusedField() const {
		auto* runtime = RuntimeView();
		if (!runtime) return {};
		auto id = runtime->FocusedControl();
		const auto widget = runtime->GetWidgetState(id);
		if (!widget || widget->role != ControlRole::Number) return {};
		return id;
	}
	// A committed character from the platform's text input (SE_CHAR, queued
	// in order with the keys around it) types into the focused field, opening
	// an edit if none is open, as typing into a stock edit field does. Its
	// keys keep control characters, and a space accepts a field that is not
	// being edited rather than replacing the text that edit then selects.
	void TypeCharacter(int character) {
		if (character < 0x20 || (character >= 0x7f && character < 0xa0)) return;
		const auto text = Utf8Scalar(character);
		const auto id = FocusedField();
		if (text.empty() || !id) return;
		auto* runtime = RuntimeView(); const double now = RetainedUI_PresentationTime();
		std::string error;
		auto widget = runtime->GetWidgetState(*id);
		if (!widget->number || !widget->number->active) {
			if (character == ' ' || !runtime->BeginNumberEdit(*id,error,now)) return;
			widget = runtime->GetWidgetState(*id);
			if (!widget || !widget->number || !widget->number->active) return;
		}
		TextInputEvent input;
		if (!(MakeTextInputCommit(text,input,error) && runtime->ApplyNumberInput(*id,widget->number->identity,input,error,now)) &&
			cvarSystem->GetCVarBool("ui_retainedTrace")) common->Printf("RETAINED_GUI_TEXT control=%s refused=%s\n",id->c_str(),error.c_str());
	}
	// Q and E, and the shoulder buttons, page a document's tabs where it
	// declares onTabPrevious and onTabNext; the triggers page the lists within
	// a page where it declares onSectionPrevious and onSectionNext; the west
	// face button runs its secondary action (reset, remove, refresh) where it
	// declares onSecondary, and north its tertiary one (details, search, sort,
	// filters) where it declares onTertiary, the Menu button its
	// onMenuButton and the View button its onViewButton (section 13.5), and a
	// press of the left or right stick its onLeftStick or onRightStick (Join
	// Game's SPECTATE and FAVORITE). A field being edited keeps its keys, and
	// Ctrl or Alt leave them to shortcuts.
	bool TabKey(int key, const openq4::KeyEventMetadata* metadata) {
		const char* name = key == 'q' || key == K_JOY1 ? "onTabPrevious" : key == 'e' || key == K_JOY2 ? "onTabNext" :
			key == K_JOY16 ? "onSectionPrevious" : key == K_JOY15 ? "onSectionNext" :
			key == K_JOY6 ? "onSecondary" : key == K_JOY5 ? "onTertiary" : key == K_JOY7 ? "onMenuButton" :
			key == K_JOY8 ? "onViewButton" : key == K_JOY13 ? "onLeftStick" : key == K_JOY14 ? "onRightStick" : nullptr;
		if (!name || !RuntimeView()->HasEvent(name)) return false;
		const bool control = metadata ? metadata->control : held.contains(K_CTRL) || idKeyInput::IsDown(K_CTRL);
		const bool alt = metadata ? metadata->alt : held.contains(K_ALT) || held.contains(K_RIGHT_ALT) || idKeyInput::IsDown(K_ALT) || idKeyInput::IsDown(K_RIGHT_ALT);
		if (control || alt) return false;
		const auto widget = RuntimeView()->GetWidgetState(RuntimeView()->FocusedControl());
		if (widget && widget->role == ControlRole::Number && widget->number && widget->number->active) return false;
		RunEvent(name); return true;
	}
	bool TextCommandKey(int key, bool down, bool repeated, const openq4::KeyEventMetadata* metadata) {
		auto* runtime = RuntimeView();
		const auto id = runtime->FocusedControl();
		const auto widget = runtime->GetWidgetState(id);
		const bool editing = widget && widget->role == ControlRole::Number && widget->number && widget->number->active;
		const bool control = metadata ? metadata->control : held.contains(K_CTRL) || idKeyInput::IsDown(K_CTRL);
		const bool shift = metadata ? metadata->shift : held.contains(K_SHIFT) || idKeyInput::IsDown(K_SHIFT);
		const bool alt = metadata ? metadata->alt : held.contains(K_ALT) || held.contains(K_RIGHT_ALT) || idKeyInput::IsDown(K_ALT) || idKeyInput::IsDown(K_RIGHT_ALT);
		std::optional<TextEditCommand> command;
		std::optional<uiClipboardOperation_t> clipboard;
		bool commit = false, undo = false, redo = false, mapped = true;
		switch (key) {
			case K_LEFTARROW: command = control ? TextEditCommand::WordLeft : TextEditCommand::Left; break;
			case K_RIGHTARROW: command = control ? TextEditCommand::WordRight : TextEditCommand::Right; break;
			case K_HOME: command = TextEditCommand::Home; break;
			case K_END: command = TextEditCommand::End; break;
			case K_BACKSPACE: if (!control) command = TextEditCommand::Backspace; break;
			case K_DEL:
				if (!control && shift) clipboard = uiClipboardOperation_t::Cut;
				else if (!control && !shift) command = TextEditCommand::Delete;
				break;
			case K_INS:
				if (control && !shift) clipboard = uiClipboardOperation_t::Copy;
				else if (shift && !control) clipboard = uiClipboardOperation_t::Paste;
				break;
			case 'c': if (control && !shift) clipboard = uiClipboardOperation_t::Copy; mapped = control; break;
			case 'x': if (control && !shift) clipboard = uiClipboardOperation_t::Cut; mapped = control; break;
			case 'v': if (control && !shift) clipboard = uiClipboardOperation_t::Paste; mapped = control; break;
			case K_ENTER: case K_KP_ENTER: commit = true; break;
			// The confirm button commits, south or east (section 13.5).
			case K_JOY3: case K_JOY4: commit = idKeyInput::MenuControllerKey(key) == K_JOY3; mapped = commit; break;
			// Space belongs to native text delivery while a field is editing.
			// It must not also activate the field as an ordinary menu button.
			case K_SPACE: break;
			case 'a': if (control) command = TextEditCommand::SelectAll; else mapped = false; break;
			case 'z': undo = control && !shift; redo = control && shift; mapped = control; break;
			case 'y': redo = control; mapped = control; break;
			default: mapped = false; break;
		}
		const auto identity = editing ? widget->number->identity : NumberEditIdentity{};
		const auto claim = input.ClaimTextKey(key,mapped ? identity.session : 0,down,repeated);
		if (claim == Input::TextKey::Unclaimed) return false;
		if (claim == Input::TextKey::Consumed || !editing || alt) return true;
		std::string error; const double now = RetainedUI_PresentationTime();
		// The runtime refreshes readback and validates this exact identity again.
		// A refused edit leaves the local draft and accepted setting untouched.
		if (command) {
			const bool movement = *command <= TextEditCommand::WordRight;
			runtime->NumberCommand(id,identity,*command,movement && shift,error,now);
		} else if (claim == Input::TextKey::Press) {
			if (clipboard) {
				// Fresh host/readback query cannot start or rebase an inactive draft.
				const auto current = runtime->QueryNumberEditor(error,now);
				if (current && current->control == id && current->editor.identity == identity &&
					!current->editor.composition && textBackend && textDocument) {
					if (actions.size() >= 256) { Error("Application action queue exceeded 256 requests"); return true; }
					PendingAction pending;
					pending.clipboard = uiClipboardRequest_t{*clipboard,
						{textBackend,textDocument,current->modalToken,id,identity}};
					actions.push_back(std::move(pending));
				}
			} else if (undo || redo) runtime->UndoNumberEdit(id,identity,redo,error,now);
			else if (commit) runtime->CommitNumberEdit(id,identity,error,now);
		}
		return true;
	}
	void Pointer(bool force = true) {
		Viewport viewport;
		if (!RetainedUI_DefaultViewport(viewport)) return;
		const CursorTransform transform(viewport);
		const float x = (cursorX*transform.sx+transform.ox+viewport.originX)/viewport.pixelDensityX;
		const float y = (cursorY*transform.sy+transform.oy+viewport.originY)/viewport.pixelDensityY;
		// Wheel input needs current coordinates, but an unchanged position must
		// not reclaim hover from the popup's previous wheel selection. Actual
		// pointer motion and button events still reclaim it even at this point.
		if (!force && routedPointerValid && x == routedPointerX && y == routedPointerY) return;
		RuntimeView()->PointerMove(x,y,RetainedUI_PresentationTime());
		routedPointerX = x; routedPointerY = y; routedPointerValid = true;
	}
};

idUserInterfaceRetained::idUserInterfaceRetained(bool managed) : idUserInterfaceManaged(managed), impl(std::make_unique<Impl>()) { diagnosticViews.push_back(this); }
idUserInterfaceRetained::~idUserInterfaceRetained() { MarkNativeInputClosing(); openq4::NativeInputBeforeUiChange(0,impl->textBackend); diagnosticViews.erase(std::remove(diagnosticViews.begin(),diagnosticViews.end(),this),diagnosticViews.end()); }
const char* idUserInterfaceRetained::Name() const { return impl->path.c_str(); }
const char* idUserInterfaceRetained::Comment() const { return "Canonical retained UI"; }
const char* idUserInterfaceRetained::GetSourceFile() const { return Name(); }
ID_TIME_T idUserInterfaceRetained::GetTimeStamp() const { return impl->stamp; }
bool idUserInterfaceRetained::Active() const { return impl->active; }
bool idUserInterfaceRetained::HasInteractiveOverride() const { return impl->interactiveSet; }
bool idUserInterfaceRetained::IsMenuGui() const { return true; }
bool idUserInterfaceRetained::AlwaysThink() const { return false; }
bool idUserInterfaceRetained::IsInteractive() const { return impl->interactive; }
void idUserInterfaceRetained::SetInteractive(bool value) { if (value != impl->interactive) openq4::NativeInputBeforeUiChange(0,impl->textBackend); impl->interactiveSet = true; impl->interactive = value; if (!value) impl->Quarantine(); }
bool idUserInterfaceRetained::IsUniqued() const { return impl->unique; }
void idUserInterfaceRetained::SetUniqued(bool value) { impl->unique = value; }
size_t idUserInterfaceRetained::Size() { return sizeof(*this)+sizeof(Impl)+impl->state.Allocated()+impl->document.Source().size(); }
int idUserInterfaceRetained::NumTransitions() { return static_cast<int>(impl->document.Model().timelines.size()); }

// The parse UI_RetainedPrefetchDocuments made of this source, if it made one.
bool UI_RetainedTakePrefetched(const std::string& path, const std::string& source, Document& document);

bool idUserInterfaceRetained::InitFromFile(const char* qpath, bool rebuild, bool cache) {
	if (!UI_IsRetainedPath(qpath)) return false;
	const std::string path(qpath);
	if (path.find("..") != std::string::npos || path.find(':') != std::string::npos || path[0] == '/' || path[0] == '\\') return false;
	void* data = nullptr; ID_TIME_T stamp = 0;
	const int expectedSize = fileSystem->ReadFile(path.c_str(),nullptr);
	if (expectedSize <= 0 || expectedSize > 16*1024*1024) return false;
	const int size = fileSystem->ReadFile(path.c_str(),&data,&stamp);
	if (size < 0 || !data) return false;
	if (size > 16*1024*1024) { fileSystem->FreeFile(data); return false; }
	const std::string source(static_cast<const char*>(data),static_cast<size_t>(size));
	fileSystem->FreeFile(data);
	Document candidate; std::vector<Diagnostic> diagnostics; std::string error;
	if (!UI_RetainedTakePrefetched(path,source,candidate) && !candidate.Load(source,diagnostics)) {
		for (const auto& diagnostic : diagnostics) common->Warning("retained GUI %s:%u:%u %s: %s",path.c_str(),
			static_cast<unsigned>(diagnostic.line),static_cast<unsigned>(diagnostic.column),diagnostic.pointer.c_str(),diagnostic.message.c_str());
		return false;
	}
	if (!ValidateApplication(candidate.Model(),error)) { impl->Error(error); return false; }
	StateValues application;
	std::string snapshot;
	const bool same = impl->Prepare() && path == impl->path.c_str() && source == impl->document.Source();
	if (!same && !ApplicationState(candidate.Model(),impl->state,application,error)) { impl->Error(error); return false; }
	if (same && !impl->RuntimeView()->SaveSnapshot(snapshot,error,RetainedUI_PresentationTime())) { impl->Error(error); return false; }
	const auto textDocument = UI_NextTextLifetime();
	if (!impl->textBackend || !textDocument) { impl->Error("GUI text lifetime exhausted"); return false; }
	auto* view = RetainedUI_CreateView(Impl::ResourceEvent,impl.get());
	if (!view) return false;
	// The view shares the document validated above instead of parsing it again.
	bool valid = RetainedUI_LoadParsedView(view,candidate,path,diagnostics);
	if (valid) valid = snapshot.empty() ? RetainedUI_ViewRuntime(view)->SetState(application,error,RetainedUI_PresentationTime()) :
		RetainedUI_ViewRuntime(view)->RestoreSnapshot(snapshot,error,RetainedUI_PresentationTime());
	if (!valid) {
		RetainedUI_DestroyView(view);
		if (!error.empty()) impl->Error(error);
		for (const auto& diagnostic : diagnostics) impl->Error(diagnostic.message);
		return false;
	}
	impl->Quarantine(false,false,true);
	openq4::NativeInputBeforeUiChange(0,impl->textBackend);
	impl->textDocument = textDocument;
	if (same && impl->settingsClosePending) UI_SettingsCloseOwner(impl->settingsOwner);
	impl->settingsClosePending = false;
	RetainedUI_DestroyView(impl->view); impl->view = view;
	impl->RuntimeView()->ReleaseInputSources();
	impl->document = std::move(candidate); impl->path = path.c_str(); impl->stamp = stamp;
	impl->IndexApplication();
	if (!same) {
		impl->initialized = false;
		UI_SettingsReleaseOwner(impl->settingsOwner); impl->settingsOwner = UI_SettingsCreateOwner();
	}
	impl->settingsFields = std::any_of(impl->document.Model().state.begin(),impl->document.Model().state.end(),
		[](const auto& field) { return field.first.starts_with("settings."); });
	UI_SettingsConfirmationDocument(impl->settingsOwner,impl->document.Model());
	impl->state.Set("name",path.c_str()); impl->lastError.clear();
	if (!impl->interactiveSet) impl->interactive = impl->hasControls && !NonInteractive(impl->state);
	RegisterLoaded(); RefreshThinking();
	PrecacheImages(impl->document.Model().root);
	common->Printf("RETAINED_GUI_LOADED %s\n",path.c_str());
	return true;
}

// A map change's reload of an unchanged source keeps the view: a new one
// would restore the same snapshot, after the startup's document work again.
// The load needs the pictures the document names, whose materials its purge
// defaulted, to resolve again inside it, as a reload's would. And as a new
// view starts, input, programs and session requests from before it are
// dropped. Bound pictures resolve when their publishers precache them.
bool idUserInterfaceRetained::KeepForLevelLoad() {
	if (!impl->view || !impl->Prepare()) return false;
	void* data = nullptr;
	const int size = fileSystem->ReadFile(impl->path.c_str(),&data);
	const auto& source = impl->document.Source();
	const bool same = size >= 0 && data && static_cast<size_t>(size) == source.size() &&
		std::memcmp(data,source.data(),source.size()) == 0;
	if (data) fileSystem->FreeFile(data);
	const auto textDocument = same ? UI_NextTextLifetime() : 0;
	if (!textDocument) return false;
	impl->Quarantine(false,true,true);
	impl->textDocument = textDocument;
	if (impl->settingsClosePending) UI_SettingsCloseOwner(impl->settingsOwner);
	impl->settingsClosePending = false;
	impl->lastError.clear();
	PrecacheImages(impl->document.Model().root);
	common->Printf("RETAINED_GUI_KEPT %s\n",impl->path.c_str());
	return true;
}

const idDict& idUserInterfaceRetained::State() const { return impl->state; }
void idUserInterfaceRetained::DeleteStateVar(const char* name) { if (impl->CallerState(name)) { impl->state.Delete(name); impl->DirtyApplication(name); } }
void idUserInterfaceRetained::SetStateString(const char* name, const char* value) { if (impl->CallerState(name)) impl->SetDictionary(name,value); }
void idUserInterfaceRetained::SetStateBool(const char* name, bool value) { SetStateString(name,value ? "1" : "0"); }
void idUserInterfaceRetained::SetStateInt(const char* name, int value) {
	char text[64]; SetStateString(name,GuiStateInteger(value,text));
}
void idUserInterfaceRetained::SetStateFloat(const char* name, float value) {
	char text[64]; const char* integral=GuiStateIntegralFloat(value,text);
	SetStateString(name,integral ? integral : va("%f",value));
}
void idUserInterfaceRetained::SetStateVec4(const char* name, const idVec4& value) { SetStateString(name,value.ToString()); }
const char* idUserInterfaceRetained::GetStateString(const char* name, const char* fallback) const { return impl->state.GetString(name,fallback); }
bool idUserInterfaceRetained::GetStateBool(const char* name, const char* fallback) const { return impl->state.GetBool(name,fallback); }
int idUserInterfaceRetained::GetStateInt(const char* name, const char* fallback) const { return impl->state.GetInt(name,fallback); }
float idUserInterfaceRetained::GetStateFloat(const char* name, const char* fallback) const { return impl->state.GetFloat(name,fallback); }
void idUserInterfaceRetained::StateChanged(int time, bool redraw) {
	if (!impl->Prepare()) return;
	StateValues values; std::string error;
	if (!impl->ReadApplication(values,error) ||
		!impl->RuntimeView()->SetState(values,error,RetainedUI_PresentationTime())) impl->Error(error);
	else {
		impl->ApplicationCommitted();
		impl->lastError.clear();
		if (!impl->interactiveSet) {
			const bool nextInteractive = impl->hasControls && !NonInteractive(impl->state);
			if (nextInteractive != impl->interactive) openq4::NativeInputBeforeUiChange(0,impl->textBackend);
			impl->interactive = nextInteractive;
			if (!impl->interactive) impl->Quarantine();
		}
	}
	if (redraw) Redraw(time);
}

bool idUserInterfaceRetained::GetPresentationValue(const char* name, idStr& value) const {
	if (!name || !impl->Prepare()) return false;
	// Read-only renderer receipt for game-owned overlays. A failed draw must
	// leave the legacy presentation available in the very same frame.
	if (!idStr::Icmp(name,"__retained::drawSucceeded")) { value = impl->drawSucceeded ? "1" : "0"; return true; }
	if (!idStr::Icmp(name,"__retained::layoutChanged")) {
		value = impl->RuntimeView()->Statistics().layoutUpdates ? "1" : "0"; return true;
	}
	std::string exported;
	if (impl->RuntimeView()->GetPresentationAlias(name,exported)) { value = exported.c_str(); return true; }
	const std::string alias(name); const auto separator = alias.find("::");
	if (separator == std::string::npos) return false;
	if (idStr::Icmp(alias.substr(0,separator).c_str(),"gui") == 0) return false;
	// openQ4: "node::@width" (or @height, @x, @y) reads the node's laid-out
	// border box in canvas dp, for a HUD peer's layout readbacks.
	if (alias.compare(separator+2,1,"@") == 0) {
		Bounds bounds;
		if (!impl->RuntimeView()->GetBounds(alias.substr(0,separator),bounds)) return false;
		const std::string field = alias.substr(separator+3);
		const float* number = field == "width" ? &bounds.width : field == "height" ? &bounds.height :
			field == "x" ? &bounds.x : field == "y" ? &bounds.y : nullptr;
		if (!number || !std::isfinite(*number)) return false;
		value = va("%.3f",*number); return true;
	}
	const auto property = impl->RuntimeView()->PresentedValue(alias.substr(0,separator),alias.substr(separator+2));
	if (!property) return false;
	value = (property->type == ValueType::Text ? property->text : property->Css()).c_str(); return true;
}
bool idUserInterfaceRetained::SetPresentationValue(const char* name, const char* value, bool overrideExpression) {
	if (!name || !value || !impl->Prepare()) return false;
	std::string error;
	if (!impl->RuntimeView()->SetPresentationAlias(name,value,overrideExpression,error)) { impl->Error(error); return false; }
	impl->lastError.clear(); return true;
}
// A focused number or text field takes the platform's text input, editing or
// not, since typing opens it. The candidate area is the field's box in
// virtual 640x480 units, as a stock edit field reports its own.
bool idUserInterfaceRetained::GetTextInputState(idRectangle& area, float& cursorOffset) const {
	if (!impl->view || !impl->active || !impl->interactive || impl->suspended) return false;
	const auto id = impl->FocusedField();
	Viewport viewport; Bounds bounds;
	if (!id || !RetainedUI_DefaultViewport(viewport) || !impl->RuntimeView()->GetBounds(*id,bounds)) return false;
	const CursorTransform transform(viewport);
	if (transform.sx <= 0 || transform.sy <= 0 || bounds.width <= 0 || bounds.height <= 0) return false;
	area = idRectangle((bounds.x-transform.ox)/transform.sx,(bounds.y-transform.oy)/transform.sy,bounds.width/transform.sx,bounds.height/transform.sy);
	cursorOffset = 0;
	return true;
}

TextBrokerContext idUserInterfaceRetained::QueryTextContext(std::uint64_t allocation,
	std::uint64_t window, std::uint64_t session) {
	TextBrokerContext result{TextBrokerRoute::Retained,window,session,{}};
	if (!allocation || !window || !session || !impl->Prepare() || !impl->AcceptInput() ||
		!impl->textBackend || !impl->textDocument) return result;
	std::string error;
	const auto current = impl->RuntimeView()->QueryNumberEditor(error,RetainedUI_PresentationTime());
	if (!error.empty()) impl->Error(error);
	if (!current) return result;
	result.editor = TextEditorIdentity{allocation,impl->textBackend,impl->textDocument,current->modalToken,
		window,current->editor.identity.session,current->editor.identity.revision,current->control};
	return result;
}

bool idUserInterfaceRetained::ApplyTextInput(const TextBrokerContext& expected,
	const TextInputEvent& input, std::string& error) {
	error.clear();
	if (expected.route != TextBrokerRoute::Retained || !expected.editor ||
		QueryTextContext(expected.editor->allocation,expected.nativeWindow,expected.nativeSession) != expected) {
		error = "Retained text editor identity changed before delivery"; return false;
	}
	const auto& target = *expected.editor;
	return impl->RuntimeView()->ApplyNumberInput(target.control,{target.session,target.revision},
		input,error,RetainedUI_PresentationTime());
}
bool idUserInterfaceRetained::GetMaxTextIndex(const char* name, const char* text, wrapInfo_t& info) const {
	if (!name || !*name || !text || !impl->Prepare()) return false;
	TextFit fit;
	if (!impl->RuntimeView()->QueryTextFit(name,text,fit)) return false;
	info.maxIndex = fit.maxIndex; info.lastWhitespace = fit.lastWhitespace;
	return fit.overflow;
}
// A binding state's keys, each in a slot of its own for a page that draws them
// as glyphs (ui-visual-design.md 13.5). Beside a state with a keyBinding, a
// document may declare, all optional:
//   <state>.key0, .key1, ...   the action's keyboard and mouse keys in key
//                              order, each a key state: with "<slot>.glyph"
//                              it receives the glyph's kind and its label, as
//                              a prompt's key does, and "" past the last key;
//   <state>.pad0, .pad1, ...   its controller buttons, the same way;
//   <slot>.arrow               1-4 for the keyboard's up, down, left and right
//                              arrows, which a keycap draws, else 0;
//   <state>.keyCount, .padCount how many of each family the action has;
//   <state>.more               "+N" for the keys past the declared slots, else "".
void idUserInterfaceRetained::SetKeyBindingNames() {
	if (!impl->Prepare()) return;
	const auto& states = impl->document.Model().state;
	for (const auto& [name,declaration] : states) {
		if (declaration.keyBinding.empty()) continue;
		idStr names;
		std::vector<int> keys[2];
		for (int key = 0; key < K_LAST_KEY; ++key) {
			if (!idKeyInput::KeyIsBoundTo(key,declaration.keyBinding.c_str())) continue;
			idStr label = idKeyInput::KeyNumToString(key,true);
			if (label.Length() == 1) label.ToUpper();
			names = names.IsEmpty() ? label : idStr(va(common->GetLanguageDict()->GetString("#str_231533"),names.c_str(),label.c_str()));
			keys[idKeyInput::IsControllerKey(key) ? 1 : 0].push_back(key);
		}
		SetStateString(name.c_str(),names.IsEmpty() ? common->GetLanguageDict()->GetString("#str_07133") : names.c_str());
		static const char* const families[2] = {".key",".pad"};
		static const char* const counts[2] = {".keyCount",".padCount"};
		size_t hidden = 0;
		for (int family = 0; family < 2; ++family) {
			size_t slot = 0;
			for (;; ++slot) {
				const std::string slotName = name + families[family] + std::to_string(slot);
				if (!states.contains(slotName)) break;
				const int key = slot < keys[family].size() ? keys[family][slot] : -1;
				SetStateString(slotName.c_str(),key >= 0 ? va("^ik%02x",key & 0xff) : "");
				if (states.contains(slotName + ".arrow")) SetStateInt((slotName + ".arrow").c_str(),
					key == K_UPARROW ? 1 : key == K_DOWNARROW ? 2 : key == K_LEFTARROW ? 3 : key == K_RIGHTARROW ? 4 : 0);
			}
			if (keys[family].size() > slot) hidden += keys[family].size() - slot;
			if (states.contains(name + counts[family])) SetStateInt((name + counts[family]).c_str(),static_cast<int>(keys[family].size()));
		}
		if (states.contains(name + ".more")) SetStateString((name + ".more").c_str(),hidden ? va("+%d",static_cast<int>(hidden)) : "");
	}
}

void idUserInterfaceRetained::SetCursor(float x, float y) {
	Viewport viewport; RetainedUI_DefaultViewport(viewport);
	const CursorTransform transform(viewport);
	if (transform.sx <= 0 || transform.sy <= 0) return;
	impl->cursorX = std::isfinite(x) ? idMath::ClampFloat(-transform.ox/transform.sx,(viewport.width-transform.ox)/transform.sx,x) : 0;
	impl->cursorY = std::isfinite(y) ? idMath::ClampFloat(-transform.oy/transform.sy,(viewport.height-transform.oy)/transform.sy,y) : 0;
}
float idUserInterfaceRetained::CursorX() { return impl->cursorX; }
float idUserInterfaceRetained::CursorY() { return impl->cursorY; }

const char* idUserInterfaceRetained::HandleEvent(const sysEvent_t* event, int time, bool* updateVisuals) {
	if (updateVisuals) *updateVisuals = false;
	if (!event) return "";
	if (!impl->Prepare()) {
		if (event->evType == SE_KEY && !event->evValue2) impl->input.ReleaseQuarantined(event->evValue);
		// Failure must never strand the session behind an unavailable menu.
		if (impl->active && event->evType == SE_KEY && event->evValue2 &&
			(event->evValue == K_ESCAPE || idKeyInput::MenuControllerKey(event->evValue) == K_JOY4 || event->evValue == K_JOY7 ||
			event->evValue == K_JOY8)) {
			openq4::NativeInputBeforeUiChange(0,impl->textBackend);
			impl->close = true;
		}
		return impl->close ? ActionMarker : "";
	}
	if (!impl->AcceptInput()) {
		if (event->evType == SE_KEY && !event->evValue2) impl->input.ReleaseQuarantined(event->evValue);
		return "";
	}
	// A finger drags lists, swipes tab strips and long-presses for details
	// (section 13.6); a mouse keeps the desktop rules. A lifted finger leaves
	// no pointer behind, so the control it touched does not stay lit.
	bool lifted = false;
	if (event->evType == SE_MOUSE || (event->evType == SE_KEY && event->evValue == K_MOUSE1))
		impl->RuntimeView()->SetTouchPointer(idKeyInput::LastPointerWasTouch());
	if (event->evType == SE_MOUSE) {
		SetCursor(impl->cursorX+event->evValue,impl->cursorY+event->evValue2);
		impl->Pointer();
	} else if (event->evType == SE_KEY && event->evValue > 0 && event->evValue < K_LAST_KEY) {
		const int key = event->evValue; const bool down = event->evValue2 != 0;
		openq4::KeyEventMetadata metadata;
		const bool hasMetadata = event->evPtrLength > 0 && openq4::DecodeKeyEventMetadata(event->evPtr,static_cast<size_t>(event->evPtrLength),metadata);
		// Malformed optional metadata must not degrade into a shortcut with
		// frame-global modifiers. Releases still retire quarantined sources.
		if (event->evPtrLength && !hasMetadata) { if (!down) impl->input.ReleaseQuarantined(key); return ""; }
		const bool repeated = down && (hasMetadata ? metadata.repeated : impl->held.contains(key));
		if (down) impl->held.insert(key); else impl->held.erase(key);
		if (impl->TextCommandKey(key,down,repeated,hasMetadata ? &metadata : nullptr)) {
			// The source remains bound to its original editor until release.
		} else if (key == K_MOUSE1) {
			impl->Pointer(); impl->input.Pointer(key,down,RetainedUI_PresentationTime());
			lifted = !down && idKeyInput::LastPointerWasTouch();
		} else if (key == K_MWHEELUP || key == K_MWHEELDOWN) {
			if (down && !repeated) {
				impl->Pointer(false);
				impl->RuntimeView()->PointerWheel(key == K_MWHEELUP ? -1 : 1,RetainedUI_PresentationTime());
			}
		} else {
			MenuInput action; bool mapped = true;
			// Confirm and Back may trade the south and east buttons (section
			// 13.5); the physical key stays the source, so a release matches
			// its press.
			switch (idKeyInput::MenuControllerKey(key)) {
				case K_TAB: action = (hasMetadata ? metadata.shift : impl->held.contains(K_SHIFT) || idKeyInput::IsDown(K_SHIFT)) ? MenuInput::Previous : MenuInput::Next; break;
				case K_UPARROW: case K_JOY9: action = MenuInput::Up; break;
				case K_DOWNARROW: case K_JOY10: action = MenuInput::Down; break;
				case K_LEFTARROW: case K_JOY12: action = MenuInput::Left; break;
				case K_RIGHTARROW: case K_JOY11: action = MenuInput::Right; break;
				case K_HOME: action = MenuInput::Home; break;
				case K_END: action = MenuInput::End; break;
				case K_PGUP: action = MenuInput::PageUp; break;
				case K_PGDN: action = MenuInput::PageDown; break;
				// The triggers page lists and step a slider coarsely (section
				// 13.5) in a document that declares no section events for them;
				// on a slider the right trigger raises the value, as Right does.
				case K_JOY16: case K_JOY15: {
					auto* runtime = impl->RuntimeView();
					const auto widget = runtime ? runtime->GetWidgetState(runtime->FocusedControl()) : std::nullopt;
					const bool forward = (key == K_JOY15) != (widget && widget->role == ControlRole::Slider);
					action = forward ? MenuInput::PageDown : MenuInput::PageUp;
					mapped = !runtime || !runtime->HasEvent(key == K_JOY16 ? "onSectionPrevious" : "onSectionNext");
					break;
				}
				case K_ENTER: case K_KP_ENTER: case K_SPACE: case K_JOY3: action = MenuInput::Accept; break;
				case K_ESCAPE: case K_JOY4: action = MenuInput::Back; break;
				// The Menu button applies pending changes or closes the menus and
				// resumes play, and the View button shows or hides a page's
				// details (section 13.5), where the document declares them;
				// elsewhere, and while a dialog or an open list holds the input,
				// each is Back, which closes them.
				case K_JOY7: case K_JOY8: {
					auto* runtime = impl->RuntimeView();
					const auto widget = runtime ? runtime->GetWidgetState(runtime->FocusedControl()) : std::nullopt;
					action = MenuInput::Back;
					mapped = !runtime || !runtime->HasEvent(key == K_JOY7 ? "onMenuButton" : "onViewButton") || runtime->ModalOpen() ||
						(widget && widget->popupOpen);
					break;
				}
				default: mapped = false; break;
			}
			if (mapped) { impl->input.Menu(key,action,down,repeated,RetainedUI_PresentationTime()); }
			else if (down && !repeated) impl->TabKey(key,hasMetadata ? &metadata : nullptr);
		}
	} else if (event->evType == SE_CHAR) {
		impl->TypeCharacter(event->evValue);
	}
	impl->input.Advance(RetainedUI_PresentationTime()); impl->ApplyInput();
	if (lifted) {
		impl->RuntimeView()->PointerMove(std::numeric_limits<float>::quiet_NaN(),0,RetainedUI_PresentationTime());
		impl->routedPointerValid = false;
	}
	// A swipe across a tab strip runs onTabPrevious or onTabNext, as Q and E do.
	for (const auto& name : impl->RuntimeView()->TakeGestureEvents())
		if (impl->RuntimeView()->HasEvent(name)) impl->RunEvent(name);
	impl->CollectActions();
	if (updateVisuals) *updateVisuals = true;
	return impl->close || !impl->actions.empty() ? ActionMarker : "";
}

void idUserInterfaceRetained::HandleNamedEvent(const char* name) {
	if (!name || !impl->Prepare()) return;
	if (impl->RuntimeView()->HasEvent(name)) impl->RunEvent(name);
	else impl->RuntimeView()->PlayTimeline(name,RetainedUI_PresentationTime());
}
const char* idUserInterfaceRetained::Activate(bool value, int time) {
	if (impl->active != value) impl->Quarantine();
	impl->active = value;
	impl->settingsClosePending = !value;
	HandleNamedEvent(value ? "onActivate" : "onDeactivate");
	if (!value && impl->actions.empty()) {
		UI_SettingsCloseOwner(impl->settingsOwner); impl->settingsClosePending = false;
		if (impl->view) impl->Prepare();
	}
	return PendingApplicationCommand();
}
void idUserInterfaceRetained::Trigger(int time) { HandleNamedEvent("onTrigger"); }
void idUserInterfaceRetained::RunTimeEvents(int time) { /* Retained motion advances on the presentation clock in Frame. */ }
void idUserInterfaceRetained::Redraw(int time, bool useAspectCorrection) {
	impl->drawSucceeded = false;
	if (!useAspectCorrection) {
		if (!impl->worldReported) { impl->Error("World-surface output is not implemented for retained menu documents"); impl->worldReported = true; }
		return;
	}
	Viewport viewport;
	if (!RetainedUI_DefaultViewport(viewport) || !impl->Prepare()) return;
	// A view-height canvas (title, pause and loading screens) follows the view
	// height instead of display density; the safety fit is irrelevant to it.
	if (impl->document.Model().canvasHeight > 0) {
		viewport.canvasHeight = static_cast<float>(impl->document.Model().canvasHeight); viewport.fitScale = 1;
		viewport.canvasMinWidth = static_cast<float>(impl->document.Model().canvasMinWidth);
	}
	if (!impl->initialized) {
		if (!impl->RuntimeView()->HasEvent("onInit") || impl->RunEvent("onInit")) impl->initialized = true;
	}
	impl->AcceptInput();
	impl->PublishPromptGlyphs();
	const bool drawn = RetainedUI_DrawViewRoot(impl->view,viewport);
	impl->drawSucceeded = drawn;
	// Timeline completion programs run inside the frame; publish their writes.
	if (auto* runtime = impl->RuntimeView()) impl->PublishWrites(runtime->TakeCompletionWrites());
	// The detail subject the frame settled on, for the session to describe.
	if (auto* runtime = impl->RuntimeView(); runtime && impl->document.Model().detail) {
		StateValue subject;
		if (runtime->GetStateValue(impl->document.Model().detail->state,subject))
			impl->PublishWrites({{impl->document.Model().detail->state,subject}});
	}
	// The frame read the bound state back; settle the number fields it answers.
	impl->SettleSessionNumbers();
	if (drawn && impl->active && impl->interactive) {
		// A restored world frame alone cannot arm Keep. Require this owner and
		// its actually activatable Revert control to have reached the draw path.
		if (impl->settingsFields && impl->RuntimeView()->CanActivateControl("settings_revert",RetainedUI_PresentationTime()))
			UI_SettingsOwnerDrawn(impl->settingsOwner,impl->state.GetString("settings.request"));
		DrawCursor();
	}
}
// The cursor shows whenever the menus hold the mouse, whatever was pressed
// last, as the stock menus' does; only controller navigation hides it, as the
// stock menus' controllerNavigation did, until the mouse moves again.
void idUserInterfaceRetained::DrawCursor() {
	if ((impl->suspended && !impl->pointerPlaced) || idKeyInput::LastInputWasController() || idKeyInput::LastPointerWasTouch()) return;
	Viewport viewport;
	if (!RetainedUI_DefaultViewport(viewport)) return;
	const CursorTransform transform(viewport);
	const float px = impl->cursorX*transform.sx+transform.ox, py = impl->cursorY*transform.sy+transform.oy;
	if (impl->document.Model().canvasHeight > 0) viewport.canvasHeight = static_cast<float>(impl->document.Model().canvasHeight);
	viewport.canvasMinWidth = static_cast<float>(impl->document.Model().canvasMinWidth);
	// A texel of the stock cursor's 32-texel box is 1.5 dp. Over a number or
	// text field, which a click would edit, the pointer is the I-beam; over
	// anything else, a control a press would reach included, it is the arrow.
	auto* runtime = impl->RuntimeView();
	const auto hovered = runtime ? runtime->HoveredControl() : std::string();
	const auto widget = hovered.empty() ? std::nullopt : runtime->GetWidgetState(hovered);
	const bool oldViewport = renderSystem->GetUseUIViewportFor2D();
	renderSystem->SetUseUIViewportFor2D(true);
	UI_DrawPointerCursor(widget && widget->role == ControlRole::Number ? POINTER_TEXT : POINTER_ARROW,px,py,
		viewport.width,viewport.height,1.5f*viewport.DpRatio());
	renderSystem->FlushGui(); renderSystem->SetUseUIViewportFor2D(oldViewport); renderSystem->SetColor4(1,1,1,1);
}

const char* idUserInterfaceRetained::PendingApplicationCommand() const {
	const bool settingsExit = impl->active && impl->settingsFields && UI_SettingsExitReady(impl->settingsOwner);
	return impl->close || impl->settingsClosePending || !impl->actions.empty() || settingsExit ? ActionMarker : "";
}
bool idUserInterfaceRetained::DispatchApplicationActions(const char* command, bool& closeRequested) {
	closeRequested = false;
	// The marker carries no untrusted parameters. Only this live instance's
	// typed queue can request host operations; all other commands are consumed.
	if (!command || idStr::Cmp(command,ActionMarker)) return true;
	if (!impl->Prepare()) {
		closeRequested = impl->close; impl->close = false; impl->actions.clear();
		if (impl->settingsClosePending) { UI_SettingsCloseOwner(impl->settingsOwner); impl->settingsClosePending = false; }
		return true;
	}
	// The session pump precedes ordinary input delivery. Observe focus/console
	// suspension now so it cannot dispatch a stale physical activation first.
	// Completed programs and explicit semantic diagnostics remain committed.
	impl->AcceptInput();
	auto actions = std::move(impl->actions); impl->actions.clear();
	closeRequested = impl->close; impl->close = false;
	for (size_t index = 0; index < actions.size(); ++index) {
		const auto& pending = actions[index];
		if (pending.clipboard) {
			// A preceding close skips native access, but committed actions later
			// in this batch still run in order before Session receives the close.
			if (closeRequested) continue;
			// Return before the native callback. Preserve this immutable suffix
			// ahead of any new actions queued while processing the earlier prefix.
			std::vector<Impl::PendingAction> suffix;
			std::move(actions.begin()+index,actions.end(),std::back_inserter(suffix));
			std::move(impl->actions.begin(),impl->actions.end(),std::back_inserter(suffix));
			impl->actions = std::move(suffix); return true;
		}
		if (pending.cancellable || pending.source.editSession) {
			if (!impl->RuntimeView()->CanDispatchControlAction(pending.source,RetainedUI_PresentationTime())) {
				if (pending.proposalToken) impl->RuntimeView()->AcknowledgeControlProposal(pending.control,pending.proposalToken,false);
				continue;
			}
		}
		const auto& invocation = pending.invocation;
		if (invocation.operation == "ui.numberDrafts.focus") {
			const bool accepted = impl->FocusNumberDraft();
			if (pending.proposalToken) impl->RuntimeView()->AcknowledgeControlProposal(pending.control,pending.proposalToken,accepted);
			continue;
		}
		if (invocation.operation == "ui.dismiss") {
			NumberDraftSummary drafts;
			if (!impl->QueryNumberDrafts(drafts) || !drafts.blocking.empty()) {
				if (pending.proposalToken) impl->RuntimeView()->AcknowledgeControlProposal(pending.control,pending.proposalToken,false);
				impl->SyncNumberDrafts(); continue;
			}
			closeRequested = true;
			if (pending.proposalToken) impl->RuntimeView()->AcknowledgeControlProposal(pending.control,pending.proposalToken,true);
			TraceInvocation(Name(),invocation,true); continue;
		}
		if (invocation.operation == "session.menu") {
			// Validated again at resolution; the session maps the verb itself.
			const auto& command = std::get<std::string>(invocation.arguments.at("command"));
			const auto request = SessionMenuPlayerCommand(command) ? SessionMenuPlayerRequest(invocation) :
				SessionMenuSaveCommand(command) ? SessionMenuSaveRequest(invocation) : std::optional<std::string>(command);
			const bool accepted = request && impl->sessionRequests.size() < 64;
			if (accepted) impl->sessionRequests.push_back(*request);
			else impl->Error(request ? "Session menu request queue exceeded 64 requests" : "Invalid session target identity");
			if (pending.proposalToken) impl->RuntimeView()->AcknowledgeControlProposal(pending.control,pending.proposalToken,accepted);
			if (cvarSystem->GetCVarBool("ui_retainedTrace")) common->Printf("RETAINED_GUI_SESSION path=%s command=%s accepted=%d\n",
				Name(),command.c_str(),accepted ? 1 : 0);
			continue;
		}
		if (invocation.operation == "settings.player.set") {
			// Validated again at resolution; the setting reads back through the
			// page's CVar state.
			const auto& cvar = std::get<std::string>(invocation.arguments.at("cvar"));
			const auto& value = invocation.arguments.at("value");
			const auto* setting = FindPlayerSetting(cvar);
			bool accepted = setting && PlayerSettingValue(cvar,value);
			if (accepted && std::holds_alternative<bool>(value)) {
				cvarSystem->SetCVarBool(cvar.c_str(),std::get<bool>(value));
				accepted = cvarSystem->GetCVarBool(cvar.c_str()) == std::get<bool>(value);
			} else if (accepted && std::holds_alternative<std::string>(value)) {
				cvarSystem->SetCVarString(cvar.c_str(),std::get<std::string>(value).c_str());
				accepted = std::get<std::string>(value) == cvarSystem->GetCVarString(cvar.c_str());
			} else if (accepted && setting->integer) {
				cvarSystem->SetCVarInteger(cvar.c_str(),static_cast<int>(std::get<double>(value)));
				accepted = cvarSystem->GetCVarInteger(cvar.c_str()) == static_cast<int>(std::get<double>(value));
			} else if (accepted) {
				cvarSystem->SetCVarFloat(cvar.c_str(),static_cast<float>(std::get<double>(value)));
				accepted = cvarSystem->GetCVarFloat(cvar.c_str()) == static_cast<float>(std::get<double>(value));
			}
			if (!accepted) impl->Error("Player setting request was not applied: "+cvar);
			if (pending.proposalToken) {
				// A typed number reads back through the page's CVar state on a
				// later frame: it waits for that, as a session's does.
				const auto widget = impl->RuntimeView()->GetWidgetState(pending.control);
				if (accepted && widget && widget->role == ControlRole::Number && impl->sessionNumbers.size() < 64)
					impl->sessionNumbers.push_back({pending.control,pending.proposalToken,value,RetainedUI_PresentationTime()+1});
				else impl->RuntimeView()->AcknowledgeControlProposal(pending.control,pending.proposalToken,accepted);
			}
			if (cvarSystem->GetCVarBool("ui_retainedTrace")) common->Printf("RETAINED_GUI_SETTING path=%s cvar=%s value=%s accepted=%d\n",
				Name(),cvar.c_str(),cvarSystem->GetCVarString(cvar.c_str()),accepted ? 1 : 0);
			continue;
		}
		if (invocation.operation == "ui.state.set") {
			// A field's committed value into its document's own state, checked
			// at load. The runtime reads it back at once, so the field accepts
			// it before anything later in the batch can change that state; then
			// the event the action names, if any, runs.
			const auto& key = std::get<std::string>(invocation.arguments.at("key"));
			const auto& value = invocation.arguments.at("value");
			const auto declaration = impl->document.Model().state.find(key);
			std::string error;
			const bool accepted = declaration != impl->document.Model().state.end() && declaration->second.initial.index() == value.index() &&
				impl->RuntimeView()->SetState({{key,value}},error,RetainedUI_PresentationTime());
			if (accepted) impl->PublishWrites({{key,value}});
			else impl->Error(error.empty() ? "Field state write was not applied: "+key : error);
			if (pending.proposalToken) impl->RuntimeView()->AcknowledgeControlProposal(pending.control,pending.proposalToken,accepted);
			const auto event = invocation.arguments.find("event");
			if (accepted && event != invocation.arguments.end()) impl->RunEvent(std::get<std::string>(event->second));
			if (cvarSystem->GetCVarBool("ui_retainedTrace")) common->Printf("RETAINED_GUI_STATE path=%s key=%s accepted=%d\n",
				Name(),key.c_str(),accepted ? 1 : 0);
			continue;
		}
		if (invocation.operation == "player.setup.apply") {
			std::string payload,error;
			const bool encoded=EncodePlayerSetupRequest(invocation.arguments,payload,error);
			const auto widget = pending.proposalToken ? impl->RuntimeView()->GetWidgetState(pending.control) : std::optional<WidgetViewState>{};
			const bool awaitReadback = pending.proposalToken && widget && widget->role==ControlRole::Number;
			const bool accepted=encoded && impl->sessionRequests.size()<64 && (!awaitReadback || impl->sessionNumbers.size()<64);
			if (accepted) impl->sessionRequests.push_back(std::string(PlayerSetupRequestPrefix)+payload);
			else impl->Error(error.empty() ? "Session menu request queue exceeded 64 requests" : error);
			if (accepted && awaitReadback) impl->sessionNumbers.push_back({pending.control,pending.proposalToken,
				invocation.arguments.at("value"),RetainedUI_PresentationTime()+1});
			else if (pending.proposalToken) impl->RuntimeView()->AcknowledgeControlProposal(pending.control,pending.proposalToken,accepted);
			if (cvarSystem->GetCVarBool("ui_retainedTrace")) common->Printf("RETAINED_GUI_PLAYER_SETUP field=%s accepted=%d\n",
				std::get<std::string>(invocation.arguments.at("field")).c_str(),accepted ? 1 : 0);
			continue;
		}
		if (invocation.operation == "session.menuValue") {
			// Validated again at resolution; the session reads "<verb> <value>".
			const auto value = SessionMenuValue(std::get<std::string>(invocation.arguments.at("command")),invocation.arguments.at("value"));
			auto request = std::get<std::string>(invocation.arguments.at("command"))+" "+std::to_string(value.value_or(0));
			if (const auto catalog = invocation.arguments.find("catalog"); catalog != invocation.arguments.end())
				request += " " + std::get<std::string>(catalog->second);
			const bool accepted = value && impl->sessionRequests.size() < 64;
			if (accepted) impl->sessionRequests.push_back(request);
			else impl->Error(value ? "Session menu request queue exceeded 64 requests" : "Session value request out of range");
			if (pending.proposalToken) {
				// A number field waits for its value to come back (SettleSessionNumbers).
				const auto widget = impl->RuntimeView()->GetWidgetState(pending.control);
				if (accepted && widget && widget->role == ControlRole::Number && impl->sessionNumbers.size() < 64)
					impl->sessionNumbers.push_back({pending.control,pending.proposalToken,static_cast<double>(*value),RetainedUI_PresentationTime()+1});
				else impl->RuntimeView()->AcknowledgeControlProposal(pending.control,pending.proposalToken,accepted);
			}
			if (cvarSystem->GetCVarBool("ui_retainedTrace")) common->Printf("RETAINED_GUI_SESSION path=%s command=%s accepted=%d\n",
				Name(),request.c_str(),accepted ? 1 : 0);
			continue;
		}
		if (invocation.operation.starts_with("settings.system.")) {
			std::string error;
			NumberDraftSummary drafts;
			if (!impl->QueryNumberDrafts(drafts) || impl->ConflictsWithNumberDraft(pending,drafts)) {
				if (pending.proposalToken) impl->RuntimeView()->AcknowledgeControlProposal(pending.control,pending.proposalToken,false);
				impl->SyncNumberDrafts(); continue;
			}
			bool accepted = UI_SettingsDispatch(impl->settingsOwner,invocation,error);
			// Cancel is the explicit discard decision. A failed service rollback
			// preserves field text; a callback changing a field invalidates the
			// whole captured inventory before any local draft can be discarded.
			if (accepted && invocation.operation == "settings.system.cancel") {
				StateValues completed;
				if (!UI_SettingsRead(impl->settingsOwner,completed)) {
					error = "Cannot verify completed settings cancellation"; accepted = false;
				} else if (!std::get<bool>(completed.at("settings.open")) && !std::get<bool>(completed.at("settings.busy")) &&
					std::get<double>(completed.at("settings.phase")) == static_cast<double>(SettingsPhase::Closed)) {
					accepted = impl->RuntimeView()->DiscardNumberDrafts(drafts.barrier,error,RetainedUI_PresentationTime());
				}
				// Accepted display Revert may only queue a rollback. Preserve local
				// text until that operation settles and a later explicit discard
				// closes the service; never replay a deferred discard implicitly.
			}
			if (!accepted) impl->Error(error);
			else impl->lastError.clear();
			const bool synchronized = impl->SyncSettings();
			if (pending.proposalToken) impl->RuntimeView()->AcknowledgeControlProposal(pending.control,pending.proposalToken,accepted && synchronized);
			continue;
		}
		const auto value = invocation.arguments.find("value");
		bool accepted = false;
		if (invocation.arguments.size() != 1 || value == invocation.arguments.end()) {
			if (pending.proposalToken) impl->RuntimeView()->AcknowledgeControlProposal(pending.control,pending.proposalToken,false);
			continue;
		}
		if (invocation.operation == "settings.brightness.set" && std::holds_alternative<double>(value->second)) {
			const double number = std::get<double>(value->second);
			if (!std::isfinite(number) || number < .5 || number > 2) impl->Error("Brightness request must be between 0.5 and 2.0");
			else {
				cvarSystem->SetCVarFloat("r_brightness",static_cast<float>(number));
				accepted = cvarSystem->GetCVarFloat("r_brightness") == static_cast<float>(number);
				if (!accepted) impl->Error("Brightness request was not applied");
			}
		} else if (invocation.operation == "settings.shadows.set" && std::holds_alternative<bool>(value->second)) {
			cvarSystem->SetCVarBool("r_shadows",std::get<bool>(value->second));
			accepted = cvarSystem->GetCVarBool("r_shadows") == std::get<bool>(value->second);
			if (!accepted) impl->Error("Shadows request was not applied");
		}
		if (pending.proposalToken) impl->RuntimeView()->AcknowledgeControlProposal(pending.control,pending.proposalToken,accepted);
		TraceInvocation(Name(),invocation,closeRequested);
	}
	if (!impl->actions.empty() && !closeRequested) return true;
	if (impl->settingsClosePending) {
		UI_SettingsCloseOwner(impl->settingsOwner); impl->settingsClosePending = false;
		impl->SyncSettings();
	}
	// The settings service alone completes Apply-and-exit. Consume after this
	// ordered batch so a later Begin/Cancel cannot leave a stale close receipt.
	// Session still rechecks the actual owner before returning to its parent.
	if (impl->active && impl->settingsFields && UI_SettingsConsumeExit(impl->settingsOwner)) {
		closeRequested = true;
		if (cvarSystem->GetCVarBool("ui_retainedTrace")) common->Printf("RETAINED_GUI_EXIT path=%s owner=%llu source=applyExit\n",
			Name(),static_cast<unsigned long long>(impl->settingsOwner));
	}
	if (closeRequested) {
		NumberDraftSummary drafts;
		if (!impl->QueryNumberDrafts(drafts) || !drafts.blocking.empty()) closeRequested = false;
	}
	return true;
}

bool idUserInterfaceRetained::TakeSessionRequest(const char* command, idStr& out) {
	if (!command || idStr::Cmp(command,ActionMarker) || impl->sessionRequests.empty()) return false;
	out = impl->sessionRequests.front().c_str(); impl->sessionRequests.erase(impl->sessionRequests.begin()); return true;
}
bool idUserInterfaceRetained::TakeClipboardRequest(const char* command, uiClipboardRequest_t& out) {
	if (!command || idStr::Cmp(command,ActionMarker) || impl->actions.empty() || !impl->actions.front().clipboard) return false;
	out = *impl->actions.front().clipboard; impl->actions.erase(impl->actions.begin()); return true;
}
bool idUserInterfaceRetained::QueryClipboardEditor(uiNumberEditorSnapshot_t& out, std::string& error) {
	if (!impl->Prepare() || !impl->AcceptInput() || !impl->textBackend || !impl->textDocument) return false;
	const auto current = impl->RuntimeView()->QueryNumberEditor(error,RetainedUI_PresentationTime());
	if (!current || !current->modalToken || current->editor.composition) return false;
	out = {{impl->textBackend,impl->textDocument,current->modalToken,current->control,current->editor.identity},current->editor};
	return true;
}
bool idUserInterfaceRetained::ReplaceClipboardSelection(const uiNumberEditorTarget_t& expected,
	std::string_view text, std::string& error) {
	uiNumberEditorSnapshot_t current;
	if (!QueryClipboardEditor(current,error) || current.target != expected) return false;
	return impl->RuntimeView()->ReplaceNumberSelection(expected.control,expected.edit,text,error,RetainedUI_PresentationTime());
}
bool idUserInterfaceRetained::SetClipboardNotice(const uiNumberEditorTarget_t& expected,
	NumberEditNotice notice, std::string& error) {
	uiNumberEditorSnapshot_t current;
	if (!QueryClipboardEditor(current,error) || current.target != expected) return false;
	return impl->RuntimeView()->SetNumberNotice(expected.control,expected.edit,notice,error,RetainedUI_PresentationTime());
}

bool idUserInterfaceRetained::WriteToSaveGame(idFile* file) const {
	if (!file || !impl->Prepare() || impl->state.GetNumKeyVals() > MaxStateEntries) return false;
	std::string snapshot, error;
	if (!impl->RuntimeView()->SaveSnapshot(snapshot,error,RetainedUI_PresentationTime())) { impl->Error(error); return false; }
	idFile_Memory payload; int budget = MaxStateBytes;
	if (payload.WriteInt(impl->state.GetNumKeyVals()) != 4) return false;
	for (int i = 0; i < impl->state.GetNumKeyVals(); ++i) {
		const auto* entry = impl->state.GetKeyVal(i);
		if (entry->GetKey().IsEmpty()) return false;
		for (const auto* string : {&entry->GetKey(),&entry->GetValue()}) {
			if (string->Length() > MaxStringBytes || string->Length() > budget) return false;
			budget -= string->Length();
			if (!WriteString(payload,string->c_str())) return false;
		}
	}
	if (payload.WriteInt((impl->active ? 1 : 0) | (impl->interactive ? 2 : 0) | (impl->unique ? 4 : 0) | (impl->interactiveSet ? 8 : 0)) != 4 ||
		payload.WriteFloat(impl->cursorX) != 4 || payload.WriteFloat(impl->cursorY) != 4 || !WriteString(payload,snapshot)) return false;
	if (payload.Length() > MaxFrameBytes) return false;
	return file->WriteUnsignedInt(SaveTag) == 4 && file->WriteInt(1) == 4 && file->WriteInt(payload.Length()) == 4 &&
		file->Write(payload.GetDataPtr(),payload.Length()) == payload.Length();
}

bool idUserInterfaceRetained::ReadFromSaveGame(idFile* file) {
	openq4::NativeInputBeforeUiChange(0,impl->textBackend);
	if (!file || !impl->Prepare()) return false;
	unsigned tag = 0; int version = 0, length = 0;
	if (file->ReadUnsignedInt(tag) != 4 || file->ReadInt(version) != 4 || file->ReadInt(length) != 4 ||
		tag != SaveTag || version != 1 || length < 0 || length > MaxFrameBytes || length > file->Length()-file->Tell()) return false;
	std::string bytes(static_cast<size_t>(length),'\0');
	if (length && file->Read(bytes.data(),length) != length) return false;
	idFile_Memory payload("retained-gui-save",static_cast<const char*>(bytes.data()),length);
	idDict state; int count = 0, budget = MaxStateBytes;
	if (payload.ReadInt(count) != 4 || count < 0 || count > MaxStateEntries) return false;
	for (int i = 0; i < count; ++i) {
		std::string key,value;
		if (!ReadString(payload,key,MaxStringBytes,budget) || key.empty() || state.FindKey(key.c_str()) ||
			!ReadString(payload,value,MaxStringBytes,budget)) return false;
		state.Set(key.c_str(),value.c_str());
	}
	int flags = 0; float x = 0, y = 0; std::string snapshot,error;
	budget = static_cast<int>(Runtime::MaxSnapshotBytes);
	if (payload.ReadInt(flags) != 4 || flags < 0 || flags > 15 || payload.ReadFloat(x) != 4 || payload.ReadFloat(y) != 4 ||
		!std::isfinite(x) || !std::isfinite(y) || !ReadString(payload,snapshot,budget,budget) || payload.Tell() != payload.Length()) return false;
	// The public dictionary may contain a pending SetState* batch that the
	// caller has not committed with StateChanged yet. Preserve that distinction:
	// the snapshot is committed presentation state; the dictionary is pending
	// caller input. Restoring must not implicitly commit it or replay actions.
	// Snapshot validation is atomic; outer dictionary/flags follow on success.
	const auto textDocument = UI_NextTextLifetime();
	if (!textDocument) { impl->Error("GUI text lifetime exhausted"); return false; }
	if (!impl->RuntimeView()->RestoreSnapshot(snapshot,error,RetainedUI_PresentationTime())) { impl->Error(error); return false; }
	openq4::NativeInputBeforeUiChange(0,impl->textBackend);
	impl->textDocument = textDocument;
	impl->Quarantine(false,false,true); impl->state = state; impl->state.Set("name",Name());
	impl->fullApplication = true;
	impl->settingsClosePending = false;
	// Save restoration suppresses automatic initialization just as legacy load
	// does. Lifecycle/program side effects never replay while restoring a GUI.
	impl->initialized = true;
	impl->active = (flags & 1) != 0; impl->interactive = (flags & 2) != 0; impl->unique = (flags & 4) != 0;
	impl->interactiveSet = (flags & 8) != 0;
	// A snapshot cannot rewind an engine-owned settings draft. An inactive
	// restore does close the existing session, even when this cached GUI never
	// receives another Activate(false); failed restores leave it untouched.
	if (!impl->active) UI_SettingsCloseOwner(impl->settingsOwner);
	SetCursor(x,y); return true;
}

bool UI_RetainedSettingsDocument(idUserInterface* gui) {
	const auto found = std::find(diagnosticViews.begin(),diagnosticViews.end(),gui);
	if (found == diagnosticViews.end()) return false;
	auto& impl = *(*found)->impl;
	if (!impl.Prepare()) return false;
	const auto& model = impl.document.Model();
	if (model.id != "openq4.system" || !model.events.contains("onactivate") || !model.events.contains("onback")) return false;
	for (const auto& [key,type] : UI_SettingsStateSchema()) {
		const auto declaration = model.state.find(key);
		if (declaration == model.state.end() || declaration->second.initial.index() != type || declaration->second.HostSourced()) return false;
	}
	for (const auto* operation : {"settings.system.begin","settings.system.edit","settings.system.apply","settings.system.cancel"}) {
		if (std::none_of(model.actions.begin(),model.actions.end(),[&](const auto& entry) { return entry.second.operation == operation; })) return false;
	}
	return true;
}

bool UI_RetainedSettingsCanReturn(idUserInterface* gui) {
	const auto found = std::find(diagnosticViews.begin(),diagnosticViews.end(),gui);
	if (found == diagnosticViews.end()) return false;
	NumberDraftSummary drafts;
	if (!(*found)->impl->QueryNumberDrafts(drafts) || !drafts.blocking.empty()) return false;
	StateValues live;
	if (!UI_SettingsRead((*found)->impl->settingsOwner,live)) return false;
	// Read the actual service owner, never pending GUI dictionary values.
	if (!std::get<bool>(live.at("settings.open"))) return true;
	return !std::get<bool>(live.at("settings.dirty")) && !std::get<bool>(live.at("settings.busy")) &&
		std::get<double>(live.at("settings.phase")) == static_cast<double>(openq4::ui::SettingsPhase::Editing);
}

// Native collection endpoints have no authored action or state-dictionary path.
bool idUserInterfaceRetained::PrepareNativeText(const TextEditorIdentity& owner) {
    if (!impl->NativeOwnerMatches(owner,false) || !impl->initialized || !impl->active || !impl->interactive || impl->close) return false;
    // Tail return: manager must re-resolve the outer allocation and backend after
    // this resource/host preparation before invoking any further backend method.
    return impl->Prepare();
}
bool idUserInterfaceRetained::AttachNativeText(const TextEditorIdentity& owner, NativeTextIdentity native,
    NativeTextEditorBarrier& out, std::string& error) {
    if (!impl->NativeOwnerMatches(owner,false) || !impl->AcceptInput() || !impl->NativeOwnerMatches(owner,true)) return false;
    auto* runtime=impl->RuntimeView();
    return runtime && runtime->AttachNumberNative(owner,native,out,error,RetainedUI_PresentationTime());
}
bool idUserInterfaceRetained::RefreshNativeText(const NativeTextEditorBarrier& expected,
    NativeTextEditorView& out, std::string& error) {
    if (!impl->NativeOwnerMatches(expected.editor,false) || !impl->AcceptInput() || !impl->NativeOwnerMatches(expected.editor,true)) return false;
    auto* runtime=impl->RuntimeView();
    return runtime && runtime->RefreshNumberNative(expected,out,error,RetainedUI_PresentationTime());
}
bool idUserInterfaceRetained::CurrentNativeText(const NativeTextEditorBarrier& expected) const noexcept {
    if (!impl->NativeOwnerMatches(expected.editor,true)) return false;
    auto* runtime=impl->RuntimeView();
    return runtime && runtime->IsNumberNativeCurrent(expected);
}
bool idUserInterfaceRetained::BeginNativeText(const NativeTextEditorBarrier& expected, const NativeTextCollection& collection,
    NativeTextEditorBarrier& out, std::string& error) {
    if (!CurrentNativeText(expected)) return false;
    return impl->RuntimeView()->BeginNumberNativeCollection(expected,collection,out,error);
}
bool idUserInterfaceRetained::ApplyNativeText(const NativeTextEditorBarrier& expected, const NativeTextOffer& offer,
    NativeTextEditorReceipt& out, std::string& error) {
    if (!CurrentNativeText(expected)) return false;
    return impl->RuntimeView()->ApplyNumberNative(expected,offer,out,error);
}
bool idUserInterfaceRetained::CompleteNativeText(const NativeTextEditorBarrier& expected, const NativeTextCollection& collection,
    NativeTextEditorBarrier& out, std::string& error) {
    if (!CurrentNativeText(expected)) return false;
    return impl->RuntimeView()->CompleteNumberNativeCollection(expected,collection,out,error);
}
std::unique_ptr<Interaction::NativeSettlement> idUserInterfaceRetained::PrepareNativeTextSettlement(
    const NativeTextEditorBarrier& expected, std::string& error) {
    if (!CurrentNativeText(expected)) return {};
    return impl->RuntimeView()->PrepareNumberNativeSettlement(expected,error);
}
bool idUserInterfaceRetained::PublishNativeTextSettlement(Interaction::NativeSettlement& prepared,
    NativeTextEditorReceipt& out) noexcept {
    if (!CurrentNativeText(prepared.Receipt().before)) return false;
    return impl->RuntimeView()->PublishNumberNativeSettlement(prepared,out);
}
NativeTextPresence idUserInterfaceRetained::QueryNativeTextPresence(NativeTextIdentity native, const TextEditorIdentity& owner) const noexcept {
    // BeforeResourceReset advances textDocument before the old Runtime model
    // is destroyed. Only the actual stored barrier can prove lease presence.
    auto* runtime=impl->RuntimeView();
    return runtime?runtime->QueryNumberNativePresence(native,owner):NativeTextPresence::BusyOrUnknown;
}
bool idUserInterfaceRetained::RetireNativeTextExact(NativeTextIdentity native, const TextEditorIdentity& owner) noexcept {
    if (!impl->NativeOwnerMatches(owner,false)) return false;
    auto* runtime=impl->RuntimeView();
    return runtime && runtime->RetireNumberNativeExact(native,owner);
}

bool UI_RetainedDiagnostic(idUserInterface* gui, const idCmdArgs& args) {
	const auto found = std::find(diagnosticViews.begin(),diagnosticViews.end(),gui);
	if (found == diagnosticViews.end() || args.Argc() < 2) return false;
	auto& owner = **found; auto& impl = *owner.impl;
	if (!impl.Prepare()) return false;
	const std::string verb(args.Argv(1)); bool okay = false;
	if (verb == "report" && args.Argc() == 2) {
		common->Printf("RETAINED_GUI path=%s focus=%s revision=%llu active=%d brightness=%.6f shadows=%d contexts=%llu\n",
			owner.Name(),impl.RuntimeView()->FocusedControl().c_str(),static_cast<unsigned long long>(impl.RuntimeView()->StateRevision()),
			impl.active ? 1 : 0,cvarSystem->GetCVarFloat("r_brightness"),cvarSystem->GetCVarBool("r_shadows") ? 1 : 0,
			static_cast<unsigned long long>(impl.RuntimeView()->Statistics().activeContexts));
		return true;
	} else if (verb == "textfit" && args.Argc() == 4) {
		const std::string id(args.Argv(2));
		if (!impl.document.Model().FindNode(id)) return false;
		wrapInfo_t fit; fit.maxIndex = fit.lastWhitespace = -2;
		const bool overflow = owner.GetMaxTextIndex(id.c_str(),args.Argv(3),fit);
		common->Printf("RETAINED_GUI_TEXT_FIT id=%s available=%d overflow=%d maxIndex=%d lastWhitespace=%d\n",
			id.c_str(),fit.maxIndex != -2 ? 1 : 0,overflow ? 1 : 0,fit.maxIndex,fit.lastWhitespace);
		return true;
	} else if (verb == "inspect" && args.Argc() == 3) {
		const std::string id(args.Argv(2));
		if (id.empty() || id.size() > 128 || std::any_of(id.begin(),id.end(),[](unsigned char c) {
			return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-');
		})) return false;
		const auto* node = impl.document.Model().FindNode(id);
		Bounds bounds;
		if (!node || !impl.RuntimeView()->GetBounds(id,bounds)) return false;
		const auto opacity = impl.RuntimeView()->PresentedValue(id,"opacity");
		const auto display = impl.RuntimeView()->PresentedValue(id,"display");
		const double alpha = opacity && opacity->type == ValueType::Number ? opacity->data[0] : 1;
		if (!std::isfinite(alpha) || !std::isfinite(bounds.x) || !std::isfinite(bounds.y) ||
			!std::isfinite(bounds.width) || !std::isfinite(bounds.height)) return false;
		const std::set<std::string> displays{"block","none","flex","inline","inline-block"};
		const char* shown = !display ? "default" : display->type == ValueType::Keyword && displays.contains(display->text) ? display->text.c_str() : "invalid";
		const auto stats = impl.RuntimeView()->Statistics();
		// Bounds/properties are this node's current layout and canonical state;
		// counters describe the last complete view frame, not this node's ink.
		common->Printf("RETAINED_GUI_NODE id=%s bounds=%.9g,%.9g,%.9g,%.9g opacity=%.9g display=%s authoredPaths=%llu statistics=view vectorElements=%llu pathsCompiled=%llu uploads=%llu cacheHits=%llu vertices=%llu triangles=%llu\n",
			id.c_str(),bounds.x,bounds.y,bounds.width,bounds.height,alpha,shown,static_cast<unsigned long long>(node->paths.size()),
			static_cast<unsigned long long>(stats.vectorElements),static_cast<unsigned long long>(stats.vectorPathsCompiled),
			static_cast<unsigned long long>(stats.vectorUploads),static_cast<unsigned long long>(stats.vectorCacheHits),
			static_cast<unsigned long long>(stats.submittedVertices),static_cast<unsigned long long>(stats.submittedIndices/3));
		return true;
	} else if (verb == "widget" && args.Argc() == 3) {
		const std::string id(args.Argv(2));
		const auto widget = impl.RuntimeView()->GetWidgetState(id);
		if (!widget) return false;
		if (widget->role == ControlRole::Scrollbar) {
			if (!widget->scroll) return false;
			const auto& view = *widget->scroll;
			const auto& geometry = view.geometry;
			common->Printf("RETAINED_GUI_SCROLL id=%s available=%d usable=%d density=%.9g viewport=%.9g range=%.9g offset=%.9g track=%.9g thumb=%.9g position=%.9g travel=%.9g token=%llu\n",
				id.c_str(),view.available ? 1 : 0,geometry.usable ? 1 : 0,view.dpRatio,
				geometry.viewport,geometry.range,geometry.offset,geometry.track,geometry.thumb,geometry.position,geometry.travel,
				static_cast<unsigned long long>(view.geometryToken));
			return true;
		}
		const auto number = [](const StateValue& value) {
			if (const auto* numeric = std::get_if<double>(&value)) return *numeric;
			if (const auto* boolean = std::get_if<bool>(&value)) return *boolean ? 1.0 : 0.0;
			return 0.0;
		};
		// String content stays out of the record; type identifies numeric/bool
		// readbacks, and authored strings remain available through aliases.
		common->Printf("RETAINED_GUI_WIDGET id=%s role=%u type=%u accepted=%.17g pending=%d proposed=%.17g rejected=%d token=%llu popup=%d firstVisible=%u\n",
			id.c_str(),static_cast<unsigned>(widget->role),static_cast<unsigned>(widget->accepted.index()),number(widget->accepted),
			widget->pending ? 1 : 0,widget->pending ? number(*widget->pending) : 0,widget->rejected ? 1 : 0,
			static_cast<unsigned long long>(widget->proposalToken),widget->popupOpen ? 1 : 0,widget->firstVisible);
        if (widget->role == ControlRole::Choice) {
            const ScrollReadback view = widget->scroll.value_or(ScrollReadback{});
            const auto& geometry = view.geometry;
            common->Printf("RETAINED_GUI_CHOICE_SCROLL id=%s open=%d opening=%llu revision=%llu offsetDp=%.9g available=%d usable=%d density=%.9g viewport=%.9g range=%.9g offset=%.9g track=%.9g thumb=%.9g position=%.9g travel=%.9g geometry=%llu\n",
                id.c_str(),widget->popupOpen ? 1 : 0,static_cast<unsigned long long>(widget->popupToken),
                static_cast<unsigned long long>(widget->popupRevision),widget->popupOffsetDp,view.available ? 1 : 0,
                geometry.usable ? 1 : 0,view.dpRatio,geometry.viewport,geometry.range,geometry.offset,geometry.track,
                geometry.thumb,geometry.position,geometry.travel,static_cast<unsigned long long>(view.geometryToken));
        }
		if (widget->number) {
			const auto& edit = *widget->number; const auto geometry = impl.RuntimeView()->GetNumberGeometry(id);
			common->Printf("RETAINED_GUI_NUMBER id=%s active=%d bytes=%llu anchor=%llu caret=%llu status=%u dirty=%d conflict=%d undo=%d redo=%d session=%llu revision=%llu geometry=%d\n",
				id.c_str(),edit.active ? 1 : 0,static_cast<unsigned long long>(edit.state.text.size()),
				static_cast<unsigned long long>(edit.state.anchor),static_cast<unsigned long long>(edit.state.caret),static_cast<unsigned>(edit.status),
				edit.dirty ? 1 : 0,edit.conflict ? 1 : 0,edit.canUndo ? 1 : 0,edit.canRedo ? 1 : 0,
				static_cast<unsigned long long>(edit.identity.session),static_cast<unsigned long long>(edit.identity.revision),geometry ? 1 : 0);
		}
		return true;
    } else if (verb == "choice" && args.Argc() >= 4) {
        // Direct local presentation operations; never synthesize user input.
        if (!impl.active || !owner.IsInteractive()) return false;
        const std::string operation(args.Argv(2)), id(args.Argv(3));
        auto* runtime = impl.RuntimeView(); const auto widget = runtime->GetWidgetState(id);
        if (!widget || widget->role != ControlRole::Choice) return false;
        const auto token = widget->popupToken; const double now = RetainedUI_PresentationTime();
        if (operation == "open" && args.Argc() == 4) okay = runtime->OpenChoicePopup(id,now);
        else if (operation == "close" && args.Argc() == 4) okay = runtime->CloseChoicePopup(id,token,now);
        else if (operation == "scroll" && args.Argc() == 5) {
            const std::map<std::string,ScrollStep> steps = {{"line-back",ScrollStep::LineBackward},{"line-forward",ScrollStep::LineForward},
                {"page-back",ScrollStep::PageBackward},{"page-forward",ScrollStep::PageForward},{"start",ScrollStep::Start},{"end",ScrollStep::End}};
            const auto step = steps.find(args.Argv(4));
            if (step != steps.end()) okay = runtime->ScrollChoicePopup(id,token,step->second,now);
        }
	} else if (verb == "number" && args.Argc() >= 4) {
		// Semantic diagnostics use the same guarded editor operations as normal
		// input. They do not synthesize device events or access the clipboard.
		const std::string operation(args.Argv(2)), id(args.Argv(3)); std::string error;
		auto* runtime = impl.RuntimeView(); const double now = RetainedUI_PresentationTime();
		const auto widget = runtime->GetWidgetState(id);
		const auto identity = widget && widget->number ? widget->number->identity : NumberEditIdentity{};
		const auto integer = [](const char* text, std::int64_t& value) {
			const auto end = text+std::strlen(text); const auto result = std::from_chars(text,end,value);
			return result.ec == std::errc{} && result.ptr == end;
		};
		if (operation == "begin" && args.Argc() == 4) okay = runtime->BeginNumberEdit(id,error,now);
		else if (operation == "replace" && args.Argc() == 5) okay = runtime->ReplaceNumberSelection(id,identity,args.Argv(4),error,now);
		else if (operation == "notice" && args.Argc() == 5) {
			const std::map<std::string,NumberEditNotice> notices = {{"none",NumberEditNotice::None},
				{"read",NumberEditNotice::ClipboardReadFailed},{"write",NumberEditNotice::ClipboardWriteFailed},
				{"rejected",NumberEditNotice::ClipboardRejected},{"masked",NumberEditNotice::ClipboardMasked}};
			const auto notice = notices.find(args.Argv(4));
			if (notice != notices.end()) okay = runtime->SetNumberNotice(id,identity,notice->second,error,now);
		}
		else if (operation == "command" && (args.Argc() == 5 || (args.Argc() == 6 && !idStr::Cmp(args.Argv(5),"extend")))) {
			const std::map<std::string,TextEditCommand> commands = {{"left",TextEditCommand::Left},{"right",TextEditCommand::Right},
				{"home",TextEditCommand::Home},{"end",TextEditCommand::End},{"word-left",TextEditCommand::WordLeft},{"word-right",TextEditCommand::WordRight},
				{"select-all",TextEditCommand::SelectAll},{"backspace",TextEditCommand::Backspace},{"delete",TextEditCommand::Delete}};
			const auto command = commands.find(args.Argv(4));
			if (command != commands.end()) okay = runtime->NumberCommand(id,identity,command->second,args.Argc() == 6,error,now);
		}
		else if (operation == "select" && args.Argc() == 6) {
			std::int64_t anchor = 0, caret = 0;
			if (integer(args.Argv(4),anchor) && integer(args.Argv(5),caret) && anchor >= 0 && caret >= 0 &&
				anchor <= static_cast<std::int64_t>(TextInputMaxBytes) && caret <= static_cast<std::int64_t>(TextInputMaxBytes))
				okay = runtime->SetNumberSelection(id,identity,static_cast<std::size_t>(anchor),static_cast<std::size_t>(caret),error,now);
		} else if (operation == "preedit" && args.Argc() == 7) {
			std::int64_t start = 0, length = 0; TextInputEvent event;
			if (integer(args.Argv(5),start) && integer(args.Argv(6),length) &&
				MakeTextInputPreedit(args.Argv(4),TextIndexUnit::Utf8Bytes,start,length,event,error))
				okay = runtime->ApplyNumberInput(id,identity,event,error,now);
		} else if (operation == "input" && args.Argc() == 5) {
			TextInputEvent event;
			if (MakeTextInputCommit(args.Argv(4),event,error)) okay = runtime->ApplyNumberInput(id,identity,event,error,now);
		} else if (operation == "undo" && args.Argc() == 4) okay = runtime->UndoNumberEdit(id,identity,false,error,now);
		else if (operation == "redo" && args.Argc() == 4) okay = runtime->UndoNumberEdit(id,identity,true,error,now);
		else if (operation == "commit" && args.Argc() == 4) {
			okay = runtime->CommitNumberEdit(id,identity,error,now); if (okay) impl.CollectActions(true);
		} else if (operation == "keep" && args.Argc() == 4) okay = runtime->ResolveNumberConflict(id,identity,true,error,now);
		else if (operation == "reload" && args.Argc() == 4) okay = runtime->ResolveNumberConflict(id,identity,false,error,now);
		else if (operation == "cancel" && args.Argc() == 4) okay = runtime->CancelNumberEdit(id,identity,now);
		if (!okay && !error.empty()) impl.Error(error);
	} else if (verb == "focus" && args.Argc() == 3) {
		okay = impl.RuntimeView()->FocusControl(args.Argv(2),RetainedUI_PresentationTime());
	} else if (verb == "pointer" && args.Argc() == 4) {
		// The cursor at a point of the 640x480 screen, as the mouse would put
		// it, without a device: hover follows, and it shows in a capture.
		owner.SetCursor(static_cast<float>(atof(args.Argv(2))),static_cast<float>(atof(args.Argv(3))));
		impl.pointerPlaced = true; impl.Pointer(); okay = true;
	} else if (verb == "menu" && args.Argc() == 4 && (!idStr::Cmp(args.Argv(3),"0") || !idStr::Cmp(args.Argv(3),"1"))) {
		const std::map<std::string,MenuInput> inputs = {{"next",MenuInput::Next},{"previous",MenuInput::Previous},
			{"up",MenuInput::Up},{"down",MenuInput::Down},{"left",MenuInput::Left},{"right",MenuInput::Right},{"accept",MenuInput::Accept},{"back",MenuInput::Back},
			{"home",MenuInput::Home},{"end",MenuInput::End},{"pageUp",MenuInput::PageUp},{"pageDown",MenuInput::PageDown}};
		const auto input = inputs.find(args.Argv(2));
		if (input != inputs.end()) {
			impl.RuntimeView()->MenuAction(input->second,args.Argv(3)[0] == '1',RetainedUI_PresentationTime());
			impl.CollectActions(true); okay = true;
		}
	} else if (verb == "state" && args.Argc() == 4) {
		if (NumberDraftState(args.Argv(2))) return false;
		owner.SetStateString(args.Argv(2),args.Argv(3)); owner.StateChanged(common->GetPresentationTime()); okay = impl.lastError.empty();
	} else if (verb == "pending" && args.Argc() == 4) {
		if (NumberDraftState(args.Argv(2))) return false;
		owner.SetStateString(args.Argv(2),args.Argv(3)); okay = true;
	} else if (verb == "lines" && args.Argc() >= 4) {
		// A multi-line value, one argument per line: the console turns every
		// backslash into a slash, so no escape can carry a line break.
		if (NumberDraftState(args.Argv(2))) return false;
		idStr value = args.Argv(3);
		for (int line = 4; line < args.Argc(); ++line) { value += "\n"; value += args.Argv(line); }
		owner.SetStateString(args.Argv(2),value.c_str()); owner.StateChanged(common->GetPresentationTime()); okay = impl.lastError.empty();
	} else if (verb == "stick" && args.Argc() == 5) {
		// The right stick held at x,y (-1..1) for the given milliseconds,
		// without a device: here it scrolls the detail area, and the session
		// turns its preview (Session_RetainedGui_f).
		okay = impl.DetailStick(static_cast<float>(atof(args.Argv(3))),static_cast<float>(atof(args.Argv(4)))*0.001f);
	} else if (verb == "event" && args.Argc() == 3) {
		owner.HandleNamedEvent(args.Argv(2)); okay = impl.lastError.empty();
	} else if (verb == "trigger" && args.Argc() == 2) {
		owner.Trigger(common->GetPresentationTime()); okay = impl.lastError.empty();
	} else if (verb == "presentation" && args.Argc() == 5 && (!idStr::Cmp(args.Argv(4),"0") || !idStr::Cmp(args.Argv(4),"1"))) {
		okay = owner.SetPresentationValue(args.Argv(2),args.Argv(3),args.Argv(4)[0] == '1');
	} else if (verb == "update" && args.Argc() == 2) {
		owner.StateChanged(common->GetPresentationTime()); okay = impl.lastError.empty();
	} else if (verb == "save" && args.Argc() == 2) {
		idFile_Memory save;
		if (owner.WriteToSaveGame(&save) && save.WriteUnsignedInt(0x53454e54) == 4) {
			impl.checkpoint.assign(save.GetDataPtr(),save.Length()); okay = true;
		}
	} else if (verb == "restore" && args.Argc() == 2 && !impl.checkpoint.empty()) {
		idFile_Memory save("retained-gui-checkpoint",static_cast<const char*>(impl.checkpoint.data()),static_cast<int>(impl.checkpoint.size()));
		unsigned sentinel = 0;
		okay = owner.ReadFromSaveGame(&save) && save.ReadUnsignedInt(sentinel) == 4 && sentinel == 0x53454e54 && save.Tell() == save.Length();
	}
	common->Printf("RETAINED_GUI_OPERATION %s %s\n",verb.c_str(),okay ? "passed" : "failed");
	return okay;
}

bool UI_RetainedImageSource( const char *source ) {
	return source != NULL && openq4::ui::ValidImageSource( source );
}

void UI_RetainedPrecacheImage( const char *source ) {
	if ( source != NULL ) RetainedUI_PrecacheImage( source, false );
}

void UI_RetainedReloadImage( const char *source ) {
	if ( source != NULL ) RetainedUI_ReloadImage( source, false );
}

bool UI_RetainedViewFailed(idUserInterface* gui) {
	const auto found = std::find(diagnosticViews.begin(),diagnosticViews.end(),gui);
	return found == diagnosticViews.end() || RetainedUI_ViewFailed((*found)->impl->view);
}

const openq4::ui::DocumentModel* UI_RetainedDocumentModel(idUserInterface* gui) {
	const auto found = std::find(diagnosticViews.begin(),diagnosticViews.end(),gui);
	return found == diagnosticViews.end() || (*found)->impl->document.Source().empty() ? nullptr : &(*found)->impl->document.Model();
}

std::string UI_RetainedFocusedControl(idUserInterface* gui) {
	const auto found = std::find(diagnosticViews.begin(),diagnosticViews.end(),gui);
	if (found == diagnosticViews.end()) return {};
	const auto* runtime = (*found)->impl->RuntimeView();
	return runtime ? runtime->FocusedControl() : std::string();
}

bool UI_RetainedDetailStick(idUserInterface* gui, float y, float seconds) {
	const auto found = std::find(diagnosticViews.begin(),diagnosticViews.end(),gui);
	if (found == diagnosticViews.end()) return false;
	auto& impl = *(*found)->impl;
	return impl.Prepare() && impl.AcceptInput() && impl.DetailStick(y,seconds);
}

int UI_RetainedStateNames(idUserInterface* gui, const char* prefix, std::vector<std::string>& names) {
	names.clear();
	const auto found = std::find(diagnosticViews.begin(),diagnosticViews.end(),gui);
	if (found == diagnosticViews.end() || prefix == nullptr) return 0;
	const std::string start(prefix);
	for (const auto& [name,declaration] : (*found)->impl->document.Model().state)
		if (name.compare(0,start.size(),start) == 0) names.push_back(name);
	return static_cast<int>(names.size());
}

namespace {
// A document a load will ask for, parsed on a worker meanwhile. The job reads
// only its entry, and the entry's job is its last member, so destroying an
// entry first cancels or waits for the parse.
struct PrefetchedDocument {
	std::string path, source;
	Document document;
	std::vector<Diagnostic> diagnostics;
	bool loaded = false;
	std::unique_ptr<idJobList> job;
};
std::vector<std::unique_ptr<PrefetchedDocument>> prefetchedDocuments;
void ParsePrefetchedDocument(const idJobContext& context) {
	auto& entry = *static_cast<PrefetchedDocument*>(context.data);
	if (!context.IsCancellationRequested()) entry.loaded = entry.document.Load(entry.source,entry.diagnostics);
}
}

void UI_RetainedPrefetchDocuments( const char *const *paths, int count ) {
	// Parsed inline, they would only move the wait.
	if ( paths == NULL || !jobSystem.IsInitialized() || jobSystem.IsSynchronous() ) return;
	for ( int i = 0; i < count; ++i ) {
		const char *path = paths[i];
		if ( path == NULL || !UI_IsRetainedPath( path ) ||
				std::any_of( prefetchedDocuments.begin(), prefetchedDocuments.end(), [&]( const auto &entry ) { return entry->path == path; } ) ) {
			continue;
		}
		// The file system stays on this thread; the worker gets the bytes.
		void *data = nullptr;
		const int size = fileSystem->ReadFile( path, &data );
		if ( size <= 0 || data == nullptr || size > 16 * 1024 * 1024 ) {
			if ( data != nullptr ) fileSystem->FreeFile( data );
			continue;
		}
		auto entry = std::make_unique<PrefetchedDocument>();
		entry->path = path;
		entry->source.assign( static_cast<const char *>( data ), static_cast<size_t>( size ) );
		fileSystem->FreeFile( data );
		entry->job = jobSystem.CreateJobList( "retained-document-parse", 1 );
		if ( !entry->job || !entry->job->AddJob( ParsePrefetchedDocument, entry.get() ) ) continue;
		const idJobSubmitResult submitted = entry->job->Submit();
		if ( submitted != idJobSubmitResult::ACCEPTED && submitted != idJobSubmitResult::EXECUTED_SYNCHRONOUSLY ) continue;
		prefetchedDocuments.push_back( std::move( entry ) );
	}
}

bool UI_RetainedTakePrefetched(const std::string& path, const std::string& source, Document& document) {
	const auto found = std::find_if(prefetchedDocuments.begin(),prefetchedDocuments.end(),[&](const auto& entry) { return entry->path == path; });
	if (found == prefetchedDocuments.end()) return false;
	const auto entry = std::move(*found);
	prefetchedDocuments.erase(found);
	// A parse that failed, or of a source that has changed since, is made again
	// by the load, which reports its diagnostics.
	if (!entry->job->Wait() || !entry->loaded || entry->source != source) return false;
	document = std::move(entry->document);
	return true;
}

void UI_RetainedPrefetchClear() {
	prefetchedDocuments.clear();
}

namespace {
// A parse no load took, dropped.
void DropPrefetched(const char* path) {
	const auto found = std::find_if(prefetchedDocuments.begin(),prefetchedDocuments.end(),[&](const auto& entry) { return path != nullptr && entry->path == path; });
	if (found != prefetchedDocuments.end()) prefetchedDocuments.erase(found);
}
bool AwakeningGame() {
	for (const char* cvar : {"fs_game","fs_game_base"})
		if (!idStr::Icmp(cvarSystem->GetCVarString(cvar),"q4xbase")) return true;
	return false;
}
// The weapon wheels and quick chat the game names (WeaponWheel.inl,
// QuickChat.inl), by family and campaign; the HUD documents come from the
// Remastered routes of the stock HUD sources a spawn creates.
constexpr const char* WheelDocuments[] = {"guis/hud/weapon_wheel_marine.q4ui","guis/hud/weapon_wheel_strogg.q4ui"};
constexpr const char* AwakeningWheelDocuments[] = {"guis/hud/weapon_wheel_marine_awakening.q4ui","guis/hud/weapon_wheel_strogg_awakening.q4ui"};
// Single player precaches the scoreboard but never draws it, so its document
// is not expected there; multiplayer draws the Marine HUD under its own.
constexpr const char* SinglePlayerHudSources[] = {"guis/hud.gui","guis/hud_strogg.gui","guis/wristcomm.gui","guis/wristcomm_strogg.gui","guis/vehicles/hud.gui"};
constexpr const char* AwakeningHudSources[] = {"guis/q4xhud.gui","guis/q4xhud_strogg.gui","guis/wristcomm.gui","guis/wristcomm_strogg.gui","guis/vehicles/hud.gui"};
constexpr const char* MultiplayerHudSources[] = {"guis/mphud.gui","guis/hud.gui","guis/scoreboard.gui"};
constexpr const char* QuickChatDocument = "guis/hud/quick_chat.q4ui";
constexpr const char* ViewEffectsDocument = "guis/hud/view_effects.q4ui";
std::vector<const char*> GameplayDocuments(bool multiplayer) {
	std::vector<const char*> documents;
	const bool awakening = !multiplayer && AwakeningGame();
	if (idRemasteredHud::Enabled()) {
		for (const char* source : multiplayer ? std::span<const char* const>(MultiplayerHudSources) :
				awakening ? std::span<const char* const>(AwakeningHudSources) : std::span<const char* const>(SinglePlayerHudSources))
			if (const char* document = idRemasteredHud::DocumentFor(source)) documents.push_back(document);
	}
	if (multiplayer) { documents.push_back(WheelDocuments[0]); documents.push_back(QuickChatDocument); }
	else for (const char* wheel : awakening ? std::span<const char* const>(AwakeningWheelDocuments) : std::span<const char* const>(WheelDocuments)) documents.push_back(wheel);
	documents.push_back(ViewEffectsDocument);
	return documents;
}
}

bool UI_RetainedPrefetched( const char *path ) {
	return path != nullptr && std::any_of(prefetchedDocuments.begin(),prefetchedDocuments.end(),[&](const auto& entry) { return entry->path == path; });
}

void UI_RetainedPrefetchGameplayDocuments( bool multiplayer ) {
	if (!cvarSystem->GetCVarBool("ui_retained")) return;
	const auto documents = GameplayDocuments(multiplayer);
	UI_RetainedPrefetchDocuments(documents.data(),static_cast<int>(documents.size()));
}

void UI_RetainedSettleGameplayDocuments( bool multiplayer ) {
	// The single-player game spawned its player inside the load and took the
	// family's documents; the other family's parses go. Multiplayer clients
	// spawn after the load, so their documents wait for them.
	if (multiplayer) return;
	for (const char* document : GameplayDocuments(false)) DropPrefetched(document);
}

#else
void UI_RetainedPrefetchDocuments( const char *const *, int ) {}
void UI_RetainedPrefetchClear() {}
void UI_RetainedPrefetchGameplayDocuments( bool ) {}
void UI_RetainedSettleGameplayDocuments( bool ) {}
bool UI_RetainedPrefetched( const char * ) { return false; }
bool UI_RetainedImageSource( const char * ) { return false; }
void UI_RetainedPrecacheImage( const char * ) {}
void UI_RetainedReloadImage( const char * ) {}
#endif
