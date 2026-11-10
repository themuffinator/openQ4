#!/usr/bin/env python3
"""The ui_retained gate and the stock fallback of every retained screen.

Compiles the production retained-screen session code (title and pause home
screens, hand-offs, session verbs, loading selection, the campaign selectors
and the status report) against counted engine/UI doubles, and checks the
source contracts that keep the gate complete:

* ui_retained defaults to 1 and is archived; while it is 0 no retained
  screen is reachable. ui_retainedSystem opts into the SYSTEM page, which
  ui_retained includes only once it offers every setting of the stock SYSTEM
  page and each one works there: the list of settings it lacks matches the
  two pages (a control counts for a setting only when its action writes it),
  and the list of settings it cannot yet apply, take typing for or change on
  every renderer matches those reasons as the code states them;
* each screen presents its stock GUI instead when its retained document is
  not installed (quietly) or cannot load (reported once), when the legacy
  main menu lacks a page the home screens hand off to, or when its view
  fails; the fallback holds for the session and ui_retainedStatus lists it;
* every retained document path in the session is reached only behind the gate;
* the retained documents request only session verbs the adapter allowlists
  and the session handles, and they are up to date with their generator;
* no legacy content names a retained document; the shared, separately tested
  scope router is the only game-code selection of retained scope documents.

Renderer output, physical input and in-game presentation are qualified by the
engine captures in docs/dev/ui/retained-screens.md, not by this test.
"""
from __future__ import annotations

import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from filesystem_case_segments import function_body
from lang_table_encoding import font_code_points

ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "src/ui/retained/PlayerSetupRequest.h"
static int checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "check failed line %d: %s\n", __LINE__, #x); std::abort(); } } while (false)
#define ID_TIME_T long long
struct idStr : std::string {
    using std::string::string;
    idStr() = default;
    idStr(const std::string& value) : std::string(value) {}
    idStr& operator=(const char* value) { assign(value); return *this; }
    operator const char*() const { return c_str(); }
    void Clear() { clear(); }
    int Length() const { return static_cast<int>(size()); }
    static int Icmp(const char* a, const char* b) {
        while (*a && *b && std::tolower(*a) == std::tolower(*b)) { ++a; ++b; }
        return std::tolower(*a) - std::tolower(*b);
    }
    static int Icmpn(const char* a, const char* b, int n) {
        for (int i = 0; i < n; ++i, ++a, ++b) { if (std::tolower(*a) != std::tolower(*b) || !*a) return std::tolower(*a) - std::tolower(*b); }
        return 0;
    }
    void StripLeading(char c) { while (!empty() && front() == c) erase(begin()); }
    bool IsEmpty() const { return empty(); }
    int Find(char c) const { const auto at = find(c); return at == npos ? -1 : static_cast<int>(at); }
    idStr Left(int n) const { return idStr(substr(0, n)); }
    idStr Right(int n) const { return idStr(substr(size() - n)); }
    // Color escapes: a caret and a digit or colon (idStr::IsEscape's color
    // indices), or a caret, c and three characters (its RGB color).
    void RemoveColors() {
        std::string out;
        for (size_t i = 0; i < size(); ++i) {
            if ((*this)[i] == '^' && i + 4 < size() && std::tolower(static_cast<unsigned char>((*this)[i + 1])) == 'c') { i += 4; continue; }
            if ((*this)[i] == '^' && i + 1 < size() && (std::isdigit(static_cast<unsigned char>((*this)[i + 1])) || (*this)[i + 1] == ':')) { ++i; continue; }
            out += (*this)[i];
        }
        assign(out);
    }
    void StripTrailingWhitespace() { while (!empty() && std::isspace(static_cast<unsigned char>(back()))) pop_back(); }
    void StripFileExtension() { const auto dot=find_last_of('.'); if(dot!=npos) erase(dot); }
    void CapLength(int length) { if (length < Length()) resize(length); }
    void Append(const char* text, int length) { append(text, length); }
    int Icmp(const char* other) const { return Icmp(c_str(), other); }
    int Cmp(const char* other) const { return std::strcmp(c_str(), other); }
};
template<typename T> static T Min(T a, T b) { return a < b ? a : b; }
namespace openq4 { static int sessionChanges = 0; static void NativeInputBeforeSessionChange() { ++sessionChanges; } }
template<typename T> struct idList : std::vector<T> {
    using std::vector<T>::vector;
    int Num() const { return static_cast<int>(this->size()); }
    void Append(const T& v) { this->push_back(v); }
    int FindIndex(const T& v) const { for (size_t i = 0; i < this->size(); ++i) if ((*this)[i] == v) return static_cast<int>(i); return -1; }
    int AddUnique(const T& v) { const int i = FindIndex(v); if (i >= 0) return i; Append(v); return Num() - 1; }
    void Clear() { this->clear(); }
    void SetNum(int count) { this->resize(count); }
    bool RemoveIndex(int index) {
        if (index < 0 || index >= Num()) return false;
        this->erase(this->begin() + index); return true;
    }
};
using idStrList = idList<idStr>;
// The demo library: what each entry offers, and the session's text for it
// (the production text lives in Session_demo.cpp, outside this harness).
enum { DEMO_CAP_PLAY = 1 << 0, DEMO_CAP_DELETE = 1 << 7 };
enum demoLibraryType_t { DEMO_LIBRARY_MVD, DEMO_LIBRARY_RENDER };
struct demoLibraryEntry_t {
    demoLibraryType_t type = DEMO_LIBRARY_RENDER; int capabilities = 0; bool playable = false;
    idStr path, displayName, mapName, status;
};
struct sessionDemoText_t { idStr name, map, date, length, kind, status, detail; };
static bool Session_DemoEntryMatchesFilter(const demoLibraryEntry_t& entry, const char* filter) {
    const std::string wanted = filter;
    return wanted == "all" || (wanted == "mvd" && entry.type == DEMO_LIBRARY_MVD) || (wanted == "render" && entry.type == DEMO_LIBRARY_RENDER);
}
static void Session_DemoEntryText(const demoLibraryEntry_t& entry, sessionDemoText_t& out) {
    out.name = entry.displayName; out.map = entry.mapName.empty() ? idStr("-") : entry.mapName; out.date = "10/06/2026";
    out.length = "--:--"; out.kind = entry.type == DEMO_LIBRARY_MVD ? "Native MVD" : "Render demo"; out.status = entry.status;
    out.detail = "detail";
}
struct fileTIME_T { int index; ID_TIME_T timeStamp; };
struct Common {
    std::string output; std::vector<std::string> warnings, developer; int time = 1000, quits = 0;
    int GetPresentationTime() const { return time; }
    void Printf(const char* fmt, ...) { char text[2048]; va_list args; va_start(args, fmt); std::vsnprintf(text, sizeof(text), fmt, args); va_end(args); output += text; }
    void DPrintf(const char* fmt, ...) { char text[2048]; va_list args; va_start(args, fmt); std::vsnprintf(text, sizeof(text), fmt, args); va_end(args); developer.push_back(text); }
    void Warning(const char* fmt, ...) { char text[2048]; va_list args; va_start(args, fmt); std::vsnprintf(text, sizeof(text), fmt, args); va_end(args); warnings.push_back(text); }
    void Quit() { ++quits; }
    const struct LanguageDict* GetLanguageDict() const;
} commonObject, *common = &commonObject;
// The installed files the session can find, and the Awakening content probe.
struct idCampaignContentInfo { bool ready = true, present = true; idStr missing; };
#define MAX_STRING_CHARS 1024
static const int MAX_ASYNC_CLIENTS = 32;
#define BASE_GAMEDIR "q4base"
#define OPENQ4_GAMEDIR "baseoq4"
struct idFile {};
struct idModInfo { idStr directory, displayName, version, releaseDate, website, author, requiredopenQ4Version, listLabel; };
struct idDict {
    std::map<std::string, std::string> values;
    const char* GetString(const char* key, const char* fallback = "") const { const auto found = values.find(key); return found == values.end() ? fallback : found->second.c_str(); }
};
struct CVarSystem {
    std::map<std::string, std::string> strings;
    const char* GetCVarString(const char* name) const { const auto found = strings.find(name); return found == strings.end() ? "" : found->second.c_str(); }
    bool GetCVarBool(const char* name) const { return std::string(GetCVarString(name)) == "1"; }
    int GetCVarInteger(const char* name) const { return std::atoi(GetCVarString(name)); }
    void SetCVarInteger(const char* name, int value) { strings[name] = std::to_string(value); }
    void SetCVarBool(const char* name, bool value) { strings[name] = value ? "1" : "0"; }
    void SetCVarString(const char* name, const char* value) { strings[name] = value; }
    float GetCVarFloat(const char* name) const { return static_cast<float>(std::atof(GetCVarString(name))); }
    void SetCVarFloat(const char* name, float value) { char text[32]; std::snprintf(text, sizeof(text), "%g", value); strings[name] = text; }
} cvarSystemObject, *cvarSystem = &cvarSystemObject;
// The sound system's devices, as the stock AUDIO page lists them.
static idStr audioDeviceNames, audioDeviceValues;
static void BuildMainMenuAudioDeviceChoices(idStr& names, idStr& values) { names = audioDeviceNames; values = audioDeviceValues; }
typedef void* HandleGuiCommand_t;
// Retained documents are the .q4ui ones (UI_IsRetainedPath).
static bool UI_IsRetainedPath(const char* path) {
    const std::string name = path ? path : "";
    return name.size() > 5 && name.compare(name.size() - 5, 5, ".q4ui") == 0;
}
enum { RETAINED_NOTICE_FAVORITE = 1, RETAINED_NOTICE_CONTROLLER = 2 };
enum { RETAINED_NOTICE_UNDO_NONE, RETAINED_NOTICE_UNDO_FAVORITE };
struct FileSystem {
    std::set<std::string> files;
    std::map<std::string, std::set<std::string>> gameDirFiles; // what each game directory itself supplies
    idFile file; int opens = 0, closes = 0;
    idFile* OpenFileRead(const char* path, bool, const char* gameDir) {
        ++opens; const auto found = gameDirFiles.find(gameDir ? gameDir : "");
        return found != gameDirFiles.end() && found->second.count(path) ? &file : nullptr;
    }
    void CloseFile(idFile* opened) { if (opened == &file) ++closes; }
    std::map<std::string, std::string> screenshots; // a map's own levelshot; the stock search ends at the generic .tga
    int ReadFile(const char* path, void**, ID_TIME_T*) { return files.count(path) ? 64 : -1; }
    void FindMapScreenshot(const char* map, char* buffer, int length) {
        const auto found = screenshots.find(map);
        std::snprintf(buffer, length, "%s", found == screenshots.end() ? "gfx/guis/loadscreens/generic.tga" : found->second.c_str());
    }
    idCampaignContentInfo GetAwakeningContentInfo() { return {}; }
    std::string activeGameDir = "baseoq4";
    const char* GetActiveGameDir() const { return activeGameDir.c_str(); }
    // The installed mods, and how often their list was made and freed.
    struct ModList {
        std::vector<idModInfo> mods;
        int GetNumMods() const { return static_cast<int>(mods.size()); }
        const idModInfo& GetInfo(int index) const { return mods[index]; }
    } modList; int modLists = 0, modFrees = 0;
    ModList* ListMods() { ++modLists; return &modList; }
    void FreeModList(ModList*) { ++modFrees; }
    // The installed maps' decls, in the file system's order.
    std::vector<idDict> maps;
    int GetNumMaps() const { return static_cast<int>(maps.size()); }
    const idDict* GetMapDecl(int index) const { return index >= 0 && index < GetNumMaps() ? &maps[index] : nullptr; }
} fileSystemObject, *fileSystem = &fileSystemObject;
using idModList = FileSystem::ModList;
struct ArenaCampaign {
    int selectors = 0; void OpenSelector() { ++selectors; }
    std::vector<std::string> commands; bool HandleGuiCommand(const char* command) { commands.push_back(command); return true; }
    // The browser opening (the Arena page where it can), and the entrance holding it.
    int browsers = 0; bool entering = false; void OpenBrowser() { ++browsers; } bool IsEntering() const { return entering; }
    // The levelshot of the match the browser would open on.
    std::string levelshot; idStr SelectedLevelshot() const { return idStr(levelshot.c_str()); }
} arenaCampaign;
struct MapSpawnData { idDict serverInfo; };
// A map decl plays the game types it names with "1"; the production MAPScan
// rules (MapDeclSupports and its aliases) live outside this harness.
static bool MapSupportsStartServerGameType(const idDict* dict, const char* gametype) { return dict && std::string(dict->GetString(gametype)) == "1"; }
static bool MapSupportsAnyMPGameType(const idDict* dict) {
    for (const char* type : {"DM", "Team DM", "CTF", "Tourney", "Arena CTF", "DeadZone"}) if (MapSupportsStartServerGameType(dict, type)) return true;
    return false;
}
static std::map<std::string, idDict> mapDecls;
static bool Session_GetMapDeclDict(const char* map, const char*, idDict& out) {
    const auto found = mapDecls.find(map); if (found == mapDecls.end()) return false; out = found->second; return true;
}
static bool Session_FileExistsInSearchPaths(const char* path) { return fileSystemObject.files.count(path) != 0; }
struct LanguageDict {
    const char* GetString(const char* key) const {
        return !std::strcmp(key, "#str_230046") ? "in mission" : !std::strcmp(key, "#str_230048") ? "saved %d min ago" :
               !std::strcmp(key, "#str_230049") ? "saved %d h ago" : !std::strcmp(key, "#str_230050") ? "saved just now" :
               !std::strcmp(key, "#str_231524") ? "%s total" : !std::strcmp(key, "#str_231525") ? "%s played" :
               !std::strcmp(key, "#str_231532") ? "%s in mission" : !std::strcmp(key, "#str_231533") ? "%s \xc2\xb7 %s" :
               !std::strcmp(key, "#str_231535") ? "%s %s" : key;
    }
} languageObject;
const LanguageDict* Common::GetLanguageDict() const { return &languageObject; }
static const char* Session_GetSkillName() { return "Corporal"; }
// A difficulty's name, and the play time (Session.cpp, extracted below).
static const char* Session_SkillName(int skill) {
    static const char* names[] = {"Private", "Corporal", "Lieutenant", "General", "Warlord"};
    return names[skill < 0 ? 0 : skill > 4 ? 4 : skill];
}
struct idUserInterface;
struct Game {
    std::vector<std::string> commands; std::function<void(idUserInterface*)> publish;
    void HandleMainMenuCommands(const char* command, idUserInterface* gui) { commands.push_back(command); if (publish) publish(gui); }
    // The multiplayer menu's commands, and its answer to each (nullptr: close).
    std::vector<std::string> guiCommands; std::function<const char*(const char*)> answer;
    const char* HandleGuiCommands(const char* command) { guiCommands.push_back(command); return answer ? answer(command) : "continue"; }
    // The player saving: in a cinematic, and their health.
    bool cinematic = false; int health = 100;
    bool InCinematic() const { return cinematic; }
    struct PlayerInfo { int health; int GetInt(const char*) const { return health; } };
    PlayerInfo GetPersistentPlayerInfo(int) const { return {health}; }
    // The local server's ban list (name, GUID), published on a GUI as
    // idGameLocal::PopulateBanList does, and the GUIDs removed.
    std::vector<std::pair<std::string, std::string>> bans; std::vector<std::string> unbanned; int populated = 0;
    void PopulateBanList(idUserInterface* gui);
    void RemoveGuidFromBanList(const char* guid) {
        unbanned.push_back(guid);
        for (auto it = bans.begin(); it != bans.end(); ++it) if (it->second == guid) { bans.erase(it); break; }
    }
} gameObject, *game = &gameObject;
struct CVar { bool value = false; bool GetBool() const { return value; } } ui_retained, ui_retainedSystem, ui_retainedMultiplayer;
// Key bindings: a bound command's key as the key lists name it ("^ikHH"),
// and a key's name by number.
struct idKeyInput {
    static inline bool swapped = false;
    static int MenuControllerKey(int key);
    static std::map<std::string, std::string>& Bound() { static std::map<std::string, std::string> bound; return bound; }
    static const char* KeysFromBinding(const char* bind) { const auto found = Bound().find(bind); return found != Bound().end() ? found->second.c_str() : "Unbound"; }
    static const char* KeyNumToString(int key, bool) { static std::string name; name = key == 0x8b ? "F1" : "#str_07133"; return name.c_str(); }
};
enum { SE_NONE = 0, CMD_EXEC_APPEND = 1, SE_KEY = 2, SE_MOUSE = 3 };
enum { CMD_EXEC_NOW = 4 };
enum { K_ESCAPE = 27, K_ENTER = 13, K_JOY3 = 199, K_JOY4 = 200, K_JOY7 = 203, K_JOY8 = 204 };
int idKeyInput::MenuControllerKey(int key) { return swapped && (key == K_JOY3 || key == K_JOY4) ? (key == K_JOY3 ? K_JOY4 : K_JOY3) : key; }
struct sysEvent_t { int evType = SE_NONE, evValue = 0, evValue2 = 0; };
struct idKeyValue { idStr value; const idStr& GetValue() const { return value; } };
struct idUserInterface {
    std::string source, focused; bool active = false, failed = false; int activations = 0, deactivations = 0;
    std::vector<std::string> named; std::map<std::string, std::string> state;
    explicit idUserInterface(const char* name) : source(name) {}
    // A legacy GUI's window values, as presentation queries read them.
    std::map<std::string, std::string> presentation;
    bool GetPresentationValue(const char* name, idStr& value) const {
        const auto found = presentation.find(name); if (found == presentation.end()) return false; value = found->second; return true;
    }
    const char* Name() const { return source.c_str(); }
    const char* Activate(bool value, int) { active = value; ++(value ? activations : deactivations); return ""; }
    // A legacy GUI's event script, where a case needs its effect.
    std::function<void(const char*)> onNamed;
    void HandleNamedEvent(const char* name) { named.push_back(name); if (onNamed) onNamed(name); }
    bool SetPresentationValue(const char* name, const char* value, bool overrideExpression = true);
    std::vector<sysEvent_t> events;
    const char* HandleEvent(const sysEvent_t* event, int) { events.push_back(*event); return ""; }
    void SetStateBool(const char* key, bool value) { state[key] = value ? "1" : "0"; }
    void SetStateInt(const char* key, int value) { state[key] = std::to_string(value); }
    struct StateView {
        const std::map<std::string, std::string>& values;
        int GetInt(const char* key, const char* fallback = "0") const { const auto found = values.find(key); return std::atoi(found == values.end() ? fallback : found->second.c_str()); }
        bool GetBool(const char* key, const char* fallback = "0") const { return GetInt(key, fallback) != 0; }
        const char* GetString(const char* key, const char* fallback = "") const { const auto found = values.find(key); return found == values.end() ? fallback : found->second.c_str(); }
        std::map<std::string, idKeyValue>* pairs;
        const idKeyValue* FindKey(const char* key) const {
            const auto found = values.find(key); if (found == values.end()) return nullptr;
            auto& pair = (*pairs)[key]; pair.value = found->second; return &pair;
        }
    };
    mutable std::map<std::string, idKeyValue> pairs;
    StateView State() const { return {state, &pairs}; }
    void SetStateString(const char* key, const char* value) { state[key] = value; }
    void SetStateFloat(const char* key, float value) { state[key] = std::to_string(value); }
    float cursorX = 320, cursorY = 240;
    float CursorX() { return cursorX; }
    float CursorY() { return cursorY; }
    void SetCursor(float x, float y) { cursorX = x; cursorY = y; }
    int stateChanges = 0;
    void StateChanged(int) { ++stateChanges; }
};
void Game::PopulateBanList(idUserInterface* gui) {
    ++populated;
    for (size_t i = 0; i < bans.size(); ++i)
        gui->state["sa_banList_item_" + std::to_string(i)] = std::to_string(i + 1) + ":  " + bans[i].first + "\t" + bans[i].second;
    gui->state.erase("sa_banList_item_" + std::to_string(bans.size()));
    gui->state["sa_banList_sel_0"] = "-1";
}
enum { AXIS_SIDE = 0, AXIS_FORWARD = 1 };
static int stickX = 0, stickY = 0;
static bool vrInputFocus = false;
static bool VR_HasInputFocus() { return vrInputFocus; }
static bool Sys_GetJoystickAxisState(int axis, int& value) { value = axis == AXIS_SIDE ? stickX : stickY; return true; }
static int MenuControllerAbs(int value) { return value < 0 ? -value : value; }
struct idMath {
    static float ClampFloat(float low, float high, float value) { return value < low ? low : value > high ? high : value; }
    static float Fabs(float value) { return value < 0 ? -value : value; }
    static int ClampInt(int low, int high, int value) { return value < low ? low : value > high ? high : value; }
    static int Ftoi(float value) { return static_cast<int>(value); }
};
static int sysMilliseconds = 0;
static int Sys_Milliseconds() { return sysMilliseconds; }
struct Manager {
    std::vector<std::unique_ptr<idUserInterface>> storage; std::vector<std::string> loads; bool fail = false;
    struct Flags { bool autoLoad, unique, shared; }; std::vector<Flags> flags;
    idUserInterface* FindGui(const char* path, bool autoLoad, bool unique, bool shared) {
        loads.push_back(path); flags.push_back({autoLoad, unique, shared});
        if (fail) return nullptr;
        if (shared && !unique)  // the manager keeps one shared instance per path
            for (const auto& gui : storage) if (gui->source == path) return gui.get();
        storage.push_back(std::make_unique<idUserInterface>(path)); return storage.back().get();
    }
} managerObject, *uiManager = &managerObject;
struct idCmdArgs {
    std::vector<std::string> args;
    idCmdArgs() = default;
    idCmdArgs(const char* text, bool) { std::istringstream in(text); std::string word; while(in>>word) args.push_back(word); }
    int Argc() const { return static_cast<int>(args.size()); }
    const char* Argv(int index) const { return index>=0 && index<Argc() ? args[index].c_str() : ""; }
    void AppendArg(const char* arg) { args.push_back(arg); }
};
struct CommandSystem {
    std::vector<std::string> buffered;
    void BufferCommandText(int, const char* text) { buffered.push_back(text); }
    // A command's arguments, as one line.
    void BufferCommandArgs(int, const idCmdArgs& line) {
        std::string joined; for (const auto& arg : line.args) joined += (joined.empty() ? "" : " ") + arg; buffered.push_back(joined);
    }
} commands, *cmdSystem = &commands;
// JOIN IP ADDRESS's connect (Session_ConnectToTypedAddress, which the stock
// inetConnect shares and check_join_game pins): an address with a space or a
// semicolon is refused with its reason; any other connects.
static std::vector<std::string> typedAddresses;
static bool Session_ConnectToTypedAddress(const char* text, idStr& error) {
    typedAddresses.push_back(text);
    if (std::strchr(text, ' ') || std::strchr(text, ';')) { error = std::string("Cannot join ") + text; return false; }
    commands.buffered.push_back(std::string("connect ") + text); return true;
}
// This machine's address, as Sys_GetLocalIPAddress gives it.
static idStr Session_LocalAddressText() { return idStr("192.0.2.7"); }
// The legacy main menu's desktop variables, as presentation queries read them.
static std::map<std::string, int> legacy;
// Only the GUI's own `set` leaves a variable's declared expression (the
// session's writes must, or the next evaluation restores the declared value).
static int expressionWrites = 0;
bool idUserInterface::SetPresentationValue(const char* name, const char* value, bool overrideExpression) {
    presentation[name] = value; legacy[name] = std::atoi(value); expressionWrites += overrideExpression ? 0 : 1; return true;
}
static bool MainMenuWindowStateIsNonZero(idUserInterface*, const char* name) { return legacy[name] != 0; }
static bool MainMenuWindowStateEqualsInt(idUserInterface*, const char* name, int value) { return legacy[name] == value; }
static bool preview = false;
static bool RetainedUI_IsOpen() { return preview; }
static int RetainedUI_ViewCount() { return static_cast<int>(managerObject.storage.size()); }
static std::vector<std::string> legacyActions;
static bool welcomeAnswer = false;   // the game names the Welcome card
static bool legacyActionAvailable = true;
static bool UI_RunLegacyWindowAction(idUserInterface*, const char* window, bool back, idStr& command) {
    CHECK(!back); legacyActions.push_back(window); command = "play main_menu_selection"; return legacyActionAvailable;
}
static bool UI_RetainedImageSource(const char* source) { return source && !std::strstr(source, ".."); }
static std::vector<std::string> precached, reloaded;
struct DeclManager {
    bool insideLoad = false; std::vector<std::string> materials;
    bool GetInsideLoad() const { return insideLoad; }
    void SetInsideLoad(bool value) { insideLoad = value; }
    const void* FindMaterial(const char* name) { materials.push_back(name); return nullptr; }
} declManagerObject, *declManager = &declManagerObject;
static int precacheCost = 0, latePrecaches = 0;
static void UI_RetainedPrecacheImage(const char* source) {
    precached.push_back(source); sysMilliseconds += precacheCost;
    if (!declManagerObject.insideLoad) ++latePrecaches;
}
static void UI_RetainedReloadImage(const char* source) { reloaded.push_back(source); }
// The documents handed to the job workers to parse ahead of their load.
static std::vector<std::string> prefetched;
static void UI_RetainedPrefetchDocuments(const char* const* paths, int count) {
    for (int i = 0; i < count; ++i) if (paths[i]) prefetched.push_back(paths[i]);
}
// Windows a mod's own main menu lacks, and a retained view that stopped drawing.
static std::set<std::string> legacyMissing, legacyEventsMissing;
static bool UI_LegacyWindowExists(idUserInterface*, const char* window) { return !legacyMissing.count(window); }
static bool UI_LegacyNamedEventExists(idUserInterface* gui, const char* event) { return gui && !legacyEventsMissing.count(event); }
static std::string UI_RetainedFocusedControl(idUserInterface* gui) { return gui->focused; }
static bool UI_RetainedViewFailed(idUserInterface* gui) { return gui->failed; }
// The legacy menu's timelines run to a time (each GUI, in order).
static std::vector<std::string> timeRuns;
static void UI_RunTimeEvents(idUserInterface* gui, int) { timeRuns.push_back(gui->source); }
// Each retained document's declared state names, by path.
static std::map<std::string, std::vector<std::string>> declaredStates;
static int UI_RetainedStateNames(idUserInterface* gui, const char* prefix, std::vector<std::string>& names) {
    names.clear();
    for (const auto& name : declaredStates[gui->source]) if (name.rfind(prefix, 0) == 0) names.push_back(name);
    return static_cast<int>(names.size());
}
static const char* Sys_TimeStampToStr(ID_TIME_T) { return "09/29/2026\t12:40pm"; }
// Whether a save holds a name's slot, and whether it is an autosave; the
// space left for saves.
enum sessionMenuSaveSlot_t { SESSION_SAVE_SLOT_FREE, SESSION_SAVE_SLOT_TAKEN, SESSION_SAVE_SLOT_AUTOSAVE };
static std::map<std::string, sessionMenuSaveSlot_t> saveSlots;
static sessionMenuSaveSlot_t Session_MenuSaveSlotState(const char* name) {
    const auto found = saveSlots.find(name); return found == saveSlots.end() ? SESSION_SAVE_SLOT_FREE : found->second;
}
static int driveSpace = 1000;
static int Sys_GetDriveFreeSpace(const char*) { return driveSpace; }
enum saveType_t { ST_REGULAR, ST_QUICK, ST_AUTO, ST_CHECKPOINT };
struct sessionMenuSaveDescription_t { idStr saveName, description, screenshot; bool noOverwrite = false; };
static std::map<std::string, sessionMenuSaveDescription_t> saveDescriptions;
// The difficulty and play time each save's header records; none for a save
// from an earlier build.
struct sessionMenuSaveDetails_t { int skill = -1, playSeconds = -1; };
static std::map<std::string, sessionMenuSaveDetails_t> saveDetails;
static bool Session_MenuReadSaveDetails(const idStr& slot, sessionMenuSaveDetails_t& out) {
    out = saveDetails.count(slot) ? saveDetails.at(slot) : sessionMenuSaveDetails_t{}; return true;
}
idStr Session_RetainedPlayTime(int seconds);
idStr Session_RetainedList(const char* first, const char* second);
static std::string describedShot; // an autosave names its loadscreen; other saves name none
// Each save's menu label as SaveGame writes it: "^:name\t^0level".
static bool Session_MenuReadSaveDescription(const idStr& slot, sessionMenuSaveDescription_t& out) {
    if (saveDescriptions.count(slot)) { out=saveDescriptions.at(slot); return true; }
    out.saveName = slot; out.description = "^:" + slot + "\t^0Air Defense Bunker"; out.screenshot = describedShot; return true;
}
// As the engine's, eight buffers in turn, so a va() may format another's text.
static const char* va(const char* fmt, ...) {
    static char buffers[8][1024]; static int index = 0; char* text = buffers[index++ & 7];
    va_list args; va_start(args, fmt); std::vsnprintf(text, sizeof(buffers[0]), fmt, args); va_end(args); return text;
}
typedef enum { RETAINED_SUBPAGE_CAMPAIGN, RETAINED_SUBPAGE_ARENA, RETAINED_SUBPAGE_CREATE_SERVER, RETAINED_SUBPAGE_JOIN_GAME,
    RETAINED_SUBPAGE_DEMOS, RETAINED_SUBPAGE_SETTINGS_CONTROLS, RETAINED_SUBPAGE_SETTINGS_GAME, RETAINED_SUBPAGE_SETTINGS_AUDIO,
    RETAINED_SUBPAGE_SETTINGS_SYSTEM } retainedSubpage_t;
// Whether the SYSTEM page's draft lets it leave (its own compiled cases,
// ui_system_session_route.py, cover a draft not applied).
static bool systemCanReturn = true;
static bool UI_RetainedSettingsCanReturn(idUserInterface*) { return systemCanReturn; }
template<typename T> static T Max(T a, T b) { return a > b ? a : b; }
// The server scan as the Join Game page reads and drives it: the servers it
// found, those the filter passes in the sort's order (visible), the
// favorites, the sort, the scan's state, and a revision each change moves;
// the stock browser's operations are recorded.
enum serverSort_t { SORT_PING, SORT_SERVERNAME, SORT_PLAYERS, SORT_GAMETYPE, SORT_MAP, SORT_GAME, SORT_FAVORITE, SORT_PASSWORD,
                    SORT_DEDICATED, SORT_PUNKBUSTER, SORT_REPEATER, SORT_NONE };
struct netadr_t { std::string address; };
static const char* Sys_NetAdrToString(const netadr_t& adr) { return adr.address.c_str(); }
struct ServerInfo : idDict {
    int GetInt(const char* key) const { return std::atoi(GetString(key, "0")); }
    bool GetBool(const char* key) const { return GetInt(key) != 0; }
};
struct networkServer_t { netadr_t adr; ServerInfo serverInfo; int ping = 0, clients = 0; };
struct idServerScan {
    enum scan_state_t { IDLE, WAIT_ON_INIT, LAN_SCAN, NET_SCAN };
    std::vector<networkServer_t> servers; std::vector<int> visible; std::set<std::string> favorites;
    serverSort_t sort = SORT_PING; bool ascending = true; int revision = 0; scan_state_t state = IDLE;
    std::vector<std::string> calls;
    int Num() const { return static_cast<int>(servers.size()); }
    const networkServer_t& operator[](int index) const { return servers[index]; }
    void VisibleServers(idList<int>& out) { out.Clear(); for (int index : visible) out.Append(index); }
    bool IsFavoriteServer(int index) const { return index >= 0 && index < Num() && favorites.count(servers[index].adr.address) != 0; }
    serverSort_t GetSorting(bool& up) const { up = ascending; return sort; }
    int GetRevision() const { return revision; }
    scan_state_t GetState() const { return state; }
    static const char* GameTypeName(const char* type) { return !std::strcmp(type, "DM") ? "Deathmatch" : type; }
    void GUIInit() { calls.push_back("GUIInit"); ++revision; }
    void AddFavoriteServers() { calls.push_back("AddFavoriteServers"); }
    void NetScan() { calls.push_back("NetScan"); state = NET_SCAN; ++revision; }
    void SetSorting(serverSort_t to) { calls.push_back("SetSorting " + std::to_string(to)); ascending = to == sort ? !ascending : true; sort = to; ++revision; }
    void ResetSorting() { calls.push_back("ResetSorting"); sort = SORT_NONE; ascending = true; ++revision; }
    // The favorites file, which the scan writes; the mod filter, which it
    // writes on the legacy menu (filterMod).
    bool ToggleFavoriteServer(int index) {
        calls.push_back("ToggleFavoriteServer " + std::to_string(index));
        if (index < 0 || index >= Num()) return false;
        const std::string& address = servers[index].adr.address;
        if (!favorites.erase(address)) favorites.insert(address);
        ++revision; return true;
    }
    idUserInterface* menu = nullptr; std::vector<std::string> mods = {"All", "baseoq4"}; int mod = 0;
    void UpdateFilterByMod(int direction) {
        calls.push_back("UpdateFilterByMod " + std::to_string(direction));
        mod = (mod + static_cast<int>(mods.size()) + direction) % static_cast<int>(mods.size());
        if (menu) menu->state["filterMod"] = mods[mod];
        ++revision;
    }
    static bool ValidMapPath(const char* path) { return path && *path && !std::strstr(path, "..") && path[0] != '/'; }
    // The filter row's filters: the full, empty and password tags, and the
    // game type (0 for every type, else 1 past its index in the registry's
    // twelve).
    enum browserFilter_t { BROWSER_FILTER_FULL, BROWSER_FILTER_EMPTY, BROWSER_FILTER_PASSWORD };
    bool filters[3] = {}; int gameType = 0;
    bool IsFilterSet(browserFilter_t filter) const { return filters[filter]; }
    void ToggleFilter(browserFilter_t filter) { calls.push_back("ToggleFilter " + std::to_string(filter)); filters[filter] = !filters[filter]; ++revision; }
    int GetGameTypeFilter() const { return gameType; }
    void StepGameTypeFilter(int direction) {
        calls.push_back("StepGameTypeFilter " + std::to_string(direction)); gameType = (gameType + 13 + direction) % 13; ++revision;
    }
    static const char* GameTypeFilterName(int filter) { return filter == 1 ? "Deathmatch" : filter == 12 ? "Arena One Flag CTF" : filter > 0 ? "Duel" : ""; }
    // FIND PLAYER/CLAN's search, which the scan filters by.
    std::string search;
    void SetPlayerSearch(const char* text) { calls.push_back(std::string("SetPlayerSearch ") + text); search = text; ++revision; }
    // An empty list's CLEAR FILTERS: every filter and the search at once.
    void ClearFilters() {
        calls.push_back("ClearFilters"); filters[0] = filters[1] = filters[2] = false; gameType = 0; mod = 0; search.clear();
        if (menu) menu->state["filterMod"] = mods[mod];
        ++revision;
    }
    const char* GetPlayerSearch() const { return search.c_str(); }
    // The scan's progress, as the stock browser's load bar shows it.
    std::string notice; float progress = 0;
    const char* GetLoadNotice() const { return notice.c_str(); }
    float GetLoadProgress() const { return progress; }
    // A server's details, as the stock tooltip shows them: its address, the
    // double's players for it, and one setting naming its server.
    std::map<std::string, std::vector<std::string>> players;
    bool GetServerDetails(int index, idStr& address, idStrList& names, idStrList& settings) const {
        if (index < 0 || index >= Num()) return false;
        address = servers[index].adr.address.c_str(); names.clear(); settings.clear();
        const auto found = players.find(servers[index].adr.address);
        if (found != players.end()) for (const auto& name : found->second) names.push_back(idStr(name.c_str()));
        settings.push_back(idStr((std::string("Map: ") + servers[index].serverInfo.GetString("si_mapName")).c_str()));
        return true;
    }
};
struct AsyncClient { idServerScan serverList; };
struct idAsyncNetwork {
    static AsyncClient client;
    static void GetNETServers() { client.serverList.calls.push_back("GetNETServers"); client.serverList.state = idServerScan::NET_SCAN; }
};
AsyncClient idAsyncNetwork::client;
// Server text as Session.cpp reads it (check_server_card compiles the
// production one): colors dropped, at most maxBytes bytes.
static idStr Session_RetainedPlayerText(const char* text, int maxBytes, int) {
    idStr out(text ? text : ""); out.RemoveColors(); out.CapLength(maxBytes); return out;
}
class idSessionLocal {
public:
    idUserInterface *guiActive = nullptr, *guiMainMenu = nullptr, *guiTest = nullptr;
    idUserInterface *guiRetainedHome = nullptr, *guiRetainedTitle = nullptr, *guiRetainedPause = nullptr, *guiRetainedPauseStrogg = nullptr;
    int retainedPauseStrogg = -1;
    ID_TIME_T retainedNewestSave = 0;
    idUserInterface* guiRetainedReleasing = nullptr; int retainedReleaseUntil = 0;
    idUserInterface* retainedSubpageFrom = nullptr; bool retainedSubpageDeeper = false; int retainedSubpageBegan = 0, retainedSubpageUntil = 0;
    int retainedDetailTarget = 0; idStr retainedDetailCampaign;
    bool OpenRetainedCampaignSkill(); bool OpenRetainedCampaignChapters(); void BeginRetainedCampaignSkill(idUserInterface*, const char*, bool = false, bool = false);
    idUserInterface* guiLoading = nullptr; bool retainedLoadingActive = false;
    int retainedLoadingTip = -1, retainedLoadingTipSlot = 0, retainedLoadingTipAt = 0; void PublishRetainedLoadingTip(bool);
    void BeginRetainedSubpage(idUserInterface*, bool, retainedSubpage_t = RETAINED_SUBPAGE_CAMPAIGN); void UpdateRetainedSubpage();
    bool RetainedSubpageEvent(const sysEvent_t*);
    void PresentRetainedSubpage(bool, retainedSubpage_t); retainedSubpage_t retainedSubpageKind = RETAINED_SUBPAGE_CAMPAIGN;
    // Single Player's Arena sub-page.
    idUserInterface* guiRetainedArena = nullptr; idUserInterface* RetainedArenaPage();
    idUserInterface* RetainedArenaGui() const { return guiRetainedArena; } void HandleRetainedArenaRequest(idUserInterface*, const char*);
    // The maps a new game started on.
    std::vector<std::string> started; void StartNewGame(const char* map) { started.push_back(map); }
    bool retainedHomeReturning = false;
    // A stock message box (guiMsg) over the GUI it covers (guiMsgRestore).
    idUserInterface *guiMsg = nullptr, *guiMsgRestore = nullptr; bool retainedHomeUnderMessage = false;
    // A join from Join Game under way: a return to the menu finds its list.
    bool retainedJoinReturn = false;
    idStrList retainedStock;
    int retainedHandoffUntil = 0;
    float retainedPointerX = 0, retainedPointerY = 0;
    bool mapSpawned = false, multiplayer = false;
    int exits = 0, pauseStates = 0, drains = 0, saveLists = 0;
    std::vector<std::string> dispatched, loadedGames, played;
    std::vector<std::string> saves;
    bool IsMultiplayer() { return multiplayer; }
    void ExitMenu() { ++exits; guiActive = nullptr; }
    void PumpApplicationActions(idUserInterface*) { ++drains; }
    // Home hand-offs run on the legacy main menu; a card hand-off runs on the
    // game's menu, and the card's own pump on the card.
    void DispatchCommand(idUserInterface* gui, const char* command) {
        CHECK(gui == guiMainMenu || gui == guiActive || gui == guiRetainedMultiplayer); dispatched.push_back(command);
    }
    void HandleMainMenuCommands(const char* command) { played.push_back(command); }
    // LoadGame refuses a save with loadRefusal's reason, or loads it.
    enum loadGameFailure_t { LOADGAME_FAILURE_NONE, LOADGAME_FAILURE_UNREADABLE, LOADGAME_FAILURE_DAMAGED, LOADGAME_FAILURE_MAP, LOADGAME_FAILURE_PLATFORM,
                             LOADGAME_FAILURE_RESTORE };
    loadGameFailure_t loadGameFailure = LOADGAME_FAILURE_NONE, loadRefusal = LOADGAME_FAILURE_NONE;
    bool LoadGame(const char* slot) { loadedGames.push_back(slot); loadGameFailure = loadRefusal; return loadRefusal == LOADGAME_FAILURE_NONE; }
    std::vector<std::string> deleted; bool deleteFails = false;
    bool DeleteGame(const char* slot) {
        deleted.push_back(slot);
        if (!deleteFails) saves.erase(std::remove(saves.begin(), saves.end(), std::string(slot)), saves.end());
        return true;
    }
    // The Load Game page.
    idUserInterface *guiRetainedLoad = nullptr, *retainedPageHome = nullptr, *retainedPagePending = nullptr;
    int retainedPageAt = 0; bool retainedPageReturn = false;
    idStrList retainedLoadSlots, retainedLoadNames, retainedLoadLocations; int retainedSaveGeneration=0; idList<ID_TIME_T> retainedLoadTimes; int retainedLoadFirst = 0, retainedLoadSelected = -1;
    bool BeginRetainedPage(const char*); void UpdateRetainedPage(); void ReturnRetainedPage(); void ReturnRetainedSinglePlayerHome();
    void BuildRetainedSaveList(idUserInterface*, int); bool RefreshRetainedSaveList(idUserInterface*, const char*);
    void ScrubSaveGameFileName(idStr& name) { name.RemoveColors(); for(char& c:name) if(c==' ') c='_'; }
    void PublishRetainedLoadPage(idUserInterface*, int, const char*); void PublishRetainedLoadRows(idUserInterface*);
    void PublishRetainedLoadSave(idUserInterface*, const char*); void HandleRetainedLoadRequest(idUserInterface*, const char*);
    // The Save Game page, which shares the list. SaveGame saves under a name,
    // the newest save, or refuses while saveRefuses is set.
    idUserInterface* guiRetainedSave = nullptr; idStr retainedSaveTarget;
    bool RetainedPageActive() const; void PublishRetainedSavePage(idUserInterface*, int); void PublishRetainedSaveSelection(idUserInterface*, bool);
    void HandleRetainedSaveRequest(idUserInterface*, const char*); bool RetainedSaveGame(idUserInterface*, const char*);
    std::vector<std::string> savedGames; bool saveRefuses = false, objectiveFailed = false;
    bool SaveGame(const char* name, saveType_t) {
        savedGames.push_back(name); if (saveRefuses) return false;
        idStr slot=name; ScrubSaveGameFileName(slot); slot.StripFileExtension();
        saves.erase(std::remove(saves.begin(),saves.end(),slot),saves.end());
        saves.insert(saves.begin(), slot);
        saveDescriptions[slot]={name,std::string("^:")+name+"\t^0Air Defense Bunker","",false}; return true;
    }
    // The Restart Level page; the difficulties a level restarted at.
    idUserInterface* guiRetainedRestart = nullptr; void HandleRetainedRestartRequest(idUserInterface*, const char*);
    // The Strogg pause's pages.
    idUserInterface *guiRetainedLoadStrogg = nullptr, *guiRetainedSaveStrogg = nullptr, *guiRetainedRestartStrogg = nullptr;
    bool IsRetainedLoadPage(const idUserInterface* gui) const { return gui && (gui == guiRetainedLoad || gui == guiRetainedLoadStrogg); }
    bool IsRetainedSavePage(const idUserInterface* gui) const { return gui && (gui == guiRetainedSave || gui == guiRetainedSaveStrogg); }
    bool IsRetainedRestartPage(const idUserInterface* gui) const {
        return gui && (gui == guiRetainedRestart || gui == guiRetainedRestartStrogg);
    }
    // The Demos page; the library its scan finds (demoFiles), what was played
    // and deleted, and refusals.
    idUserInterface* guiRetainedDemos = nullptr; idList<int> retainedDemoRows;
    int retainedDemoFirst = 0, retainedDemoSelected = -1, retainedDemoFilter = 0;
    void PublishRetainedDemosPage(idUserInterface*, const char*, int, const char*); void PublishRetainedDemoRows(idUserInterface*);
    void PublishRetainedDemoSelection(idUserInterface*, const char*); void HandleRetainedDemosRequest(idUserInterface*, const char*);
    idList<demoLibraryEntry_t> demoLibrary; std::vector<demoLibraryEntry_t> demoFiles;
    std::vector<std::string> playedDemos, deletedDemos; bool playRefuses = false, deleteRefuses = false, demoPlaying = false;
    void ScanDemoLibrary() { demoLibrary.Clear(); for (const auto& e : demoFiles) demoLibrary.Append(e); }
    bool PlayDemoLibraryEntry(int index) {
        playedDemos.push_back(demoLibrary[index].path); if (playRefuses) { guiActive = nullptr; return false; }
        guiActive = nullptr; return true;
    }
    bool DeleteDemoLibraryFile(const demoLibraryEntry_t& entry) {
        if (deleteRefuses) return false;
        deletedDemos.push_back(entry.path);
        demoFiles.erase(std::remove_if(demoFiles.begin(), demoFiles.end(), [&](const demoLibraryEntry_t& e) { return e.path == entry.path; }),
                        demoFiles.end());
        return true;
    }
    bool IsDemoPlaybackActive() const { return demoPlaying; }
    // The Demos sub-page, and the legacy demo browser where it cannot present.
    idUserInterface* RetainedDemosPage(); void OpenRetainedDemos();
    std::vector<bool> demoMenus; void OpenDemoMenu(bool browser) { demoMenus.push_back(browser); }
    // The title's MODS dialog.
    idList<idModInfo> modsList; int retainedModSelected = -1;
    void PublishRetainedMods(idUserInterface*); void SelectRetainedMod(idUserInterface*, int);
    // The restart screen after a death; the stock screen's commands it ran.
    idUserInterface *guiRestartMenu = nullptr, *guiRetainedRestartMenu = nullptr; void OpenRestartMenu();
    // A disconnect to the main menu leaves the game for the legacy menu (Stop, StartMenu).
    std::vector<std::string> restartCommands;
    void HandleRestartMenuCommands(const char* command) {
        restartCommands.push_back(command);
        if (std::string(command).rfind("disconnect mainmenu", 0) == 0) { mapSpawned = false; guiActive = guiMainMenu; }
    }
    // The Credits page.
    idUserInterface* guiRetainedCredits = nullptr; void PublishRetainedCredits(idUserInterface*);
    // The Multiplayer page; the stock page it leaves for, and when.
    idUserInterface* guiRetainedMpPage = nullptr; int retainedMpPageDest = 0, retainedMpPageLeft = 0, retainedMpPageAt = 0;
    idUserInterface* RetainedMultiplayerPage(); bool OpenRetainedMultiplayerPage(const char* = nullptr);
    void HandleRetainedMultiplayerPageRequest(idUserInterface*, const char*); void UpdateRetainedMultiplayerPage();
    // Count the player-setup service boundary; its codec and document have
    // dedicated tests. Keep availability controllable to exercise both routes.
    bool playerSetupAvailable = false, playerSetupPublishOk = true;
    int playerSetupOpens = 0, playerSetupPublishes = 0;
    std::vector<std::string> playerSetupRequests;
    bool OpenRetainedPlayerSetup(idUserInterface* gui) {
        ++playerSetupOpens;
        if (playerSetupAvailable) gui->SetStateBool("setup.open", true);
        return playerSetupAvailable;
    }
    bool PublishRetainedPlayerSetup(idUserInterface*) { ++playerSetupPublishes; return playerSetupPublishOk; }
    void HandleRetainedPlayerSetupRequest(idUserInterface*, const char* request) { playerSetupRequests.push_back(request); }
    // Its Create Server sub-page and the maps it lists; the servers started.
    idUserInterface* guiRetainedCreate = nullptr; idList<int> retainedCreateMaps; int retainedCreateMapFirst = 0, retainedCreateMapSelected = -1;
    struct retainedLevelshot_t { idStr name; bool map = false, material = false; };
    idList<retainedLevelshot_t> retainedLevelshots; int retainedLevelshotsFrom = 0; void PrecacheRetainedLevelshots(bool, bool = true);
    void QueueRetainedLevelshot(const char*, bool);
    idUserInterface* RetainedCreateServerPage(); void OpenRetainedCreateServer(); void PublishRetainedCreateServer(idUserInterface*);
    void PublishRetainedCreateMaps(idUserInterface*, bool); void HandleRetainedCreateServerRequest(idUserInterface*, const char*);
    idStrList retainedCreateBanGuids; int retainedCreateBanSelected = -1; void PublishRetainedCreateBans(idUserInterface*);
    // Its Join Game sub-page and the servers it lists.
    // The settings screen's AUDIO page, and the screen and handler it returns to.
    idUserInterface *guiRetainedAudio = nullptr, *retainedAudioParent = nullptr;
    HandleGuiCommand_t guiHandle = nullptr, retainedAudioParentHandle = nullptr;
    bool RetainedAudioAvailable() const; bool OpenRetainedAudio(); void OpenRetainedAudioRoute(); void ReturnRetainedAudio();
    void PublishRetainedAudio(idUserInterface*); void HandleRetainedAudioRequest(idUserInterface*, const char*);
    // The GAME OPTIONS page; its readback and verbs have their own source
    // contracts (the stock choices they apply live outside this harness).
    idUserInterface *guiRetainedGame = nullptr, *retainedGameParent = nullptr; HandleGuiCommand_t retainedGameParentHandle = nullptr;
    bool RetainedGameAvailable() const; bool OpenRetainedGame(); void OpenRetainedGameRoute(bool = false); void ReturnRetainedGame();
    std::vector<idUserInterface*> gamePublishes; std::vector<std::string> gameRequests;
    void PublishRetainedGame(idUserInterface* gui) { gamePublishes.push_back(gui); }
    void HandleRetainedGameRequest(idUserInterface*, const char* request) { gameRequests.push_back(request); }
    // The CONTROLS page; its readback, verbs and key capture have their own
    // source contracts (the key bindings live outside this harness).
    idUserInterface *guiRetainedControls = nullptr, *retainedControlsParent = nullptr;
    HandleGuiCommand_t retainedControlsParentHandle = nullptr; int retainedControlsCapture = -1, retainedControlsCaptured = -1;
    bool RetainedControlsAvailable() const; bool OpenRetainedControls(); void OpenRetainedControlsRoute(bool = false);
    void ReturnRetainedControls();
    std::vector<idUserInterface*> controlsPublishes; std::vector<std::string> controlsRequests;
    void PublishRetainedControls(idUserInterface* gui) { controlsPublishes.push_back(gui); }
    void HandleRetainedControlsRequest(idUserInterface*, const char* request) { controlsRequests.push_back(request); }
    // The SETTINGS page, and the retained SYSTEM page its DISPLAY goes one
    // level deeper to. Opening and closing that page have their own compiled
    // cases (ui_system_session_route.py); here a counted double opens it over
    // the screen that asks, as the production one does, while systemOpensPage
    // holds, and SetGUI of another screen closes it. Its Back
    // (ReturnSystemSettings) is the production one.
    idUserInterface* guiRetainedSettingsPage = nullptr; int retainedSettingsDest = 0, retainedSettingsLeft = 0, retainedSettingsAt = 0;
    idUserInterface* RetainedSettingsPage(); bool OpenRetainedSettingsPage(const char* = nullptr);
    void HandleRetainedSettingsPageRequest(idUserInterface*, const char*); void UpdateRetainedSettingsPage(); bool RetainedSettingsParent() const;
    idUserInterface *guiSystem = nullptr, *guiSystemParent = nullptr; HandleGuiCommand_t guiSystemParentHandle = nullptr;
    bool systemGuiTransition = false, systemGuiBackEvent = false, systemOpensPage = false; int systemOpens = 0, systemCloses = 0;
    bool OpenSystemSettings() {
        ++systemOpens;
        if (guiSystem != nullptr) return guiActive == guiSystem;
        if (!systemOpensPage || !RetainedSystemAvailable() || !RetainedSettingsParent() || guiMsgRestore != nullptr) return false;
        idUserInterface* child = FindRetainedGui("guis/menu/settings/system.q4ui", false, false);
        if (child == nullptr) return false;
        guiSystemParent = guiActive; guiSystemParentHandle = guiHandle; guiSystem = child;
        SetGUI(child, nullptr); return guiActive == child;
    }
    bool ReturnSystemSettings(); void SetPlayingSoundWorld() {}
    idUserInterface* guiRetainedJoin = nullptr; idList<int> retainedJoinServers; int retainedJoinFirst = 0, retainedJoinRevision = -1;
    idStr retainedJoinAddress, retainedJoinShotMap, retainedJoinShot; idStrList retainedJoinRowAddresses;
    idUserInterface* RetainedJoinGamePage(); void OpenRetainedJoinGame(); void PublishRetainedJoinGame(idUserInterface*);
    void PublishRetainedJoinDetail(idUserInterface*); int retainedJoinDetailRow = -1;
    void UpdateRetainedJoinGame(); void HandleRetainedJoinGameRequest(idUserInterface*, const char*);
    // The notices over the retained menus (section 13.13): shown oldest
    // first, then those waiting; the menu they were published to; the
    // controller family last seen.
    struct retainedNotice_t {
        idStr text, action; int kind = 0; idStr subject; int undo = 0; bool persistent = false, resolved = false;
        int shownAt = -1, leavingAt = -1;
    };
    idList<retainedNotice_t> retainedNotices; bool retainedNoticesDirty = false; idUserInterface* retainedNoticesGui = nullptr;
    idStr retainedNoticeGamepad;
    void PushRetainedNotice(const char*, int, const char*, const char*, int, bool); void ResolveRetainedNotices(int);
    void UpdateRetainedNotices(); void HandleRetainedNoticeAction(idUserInterface*, int);
    int serverStarts = 0; bool StartMenuServer() { ++serverStarts; return true; }
    std::vector<int> restarts; bool RestartLevelAtSkill(int skill) { restarts.push_back(skill); return true; }
    void GetSaveGameList(idStrList& files, idList<fileTIME_T>& times) {
        ++saveLists;
        for (size_t i = 0; i < saves.size(); ++i) { files.Append(idStr(saves[i])); times.Append({static_cast<int>(i), 100 - static_cast<ID_TIME_T>(i)}); }
    }
    idStr currentMapName = idStr("game/airdefense1");
    void PublishRetainedPauseState(idUserInterface*);
    MapSpawnData mapSpawnData;
    idStr RetainedPauseShot(const char*, const char* = nullptr) const;
    // The campaign page's chapters: the picks published and the starts.
    std::vector<int> chapterPublishes, chapterStarts;
    void PublishCampaignChapters(idUserInterface*, int select = -1) { chapterPublishes.push_back(select); }
    // The active campaign's first and furthest reached levelshots.
    void CampaignSummaryShots(idStr& first, idStr& furthest) { first = "gfx/guis/loadscreens/e3_load"; furthest = "gfx/guis/loadscreens/airdefense"; }
    bool StartCampaignChapter(int chapter) { chapterStarts.push_back(chapter); return chapter >= 0; }
    void UpdateRetainedHome(); bool RetainedHomeInputBlocked() const; void RetainedHomeFrameEvent();
    void HandleRetainedSessionRequest(idUserInterface*, const char*);
    void OpenCampaignSelector(bool, bool = false, bool = false);
    std::vector<std::string> selected; int menus = 0;
    int starts = 0;
    void SelectCampaign(const char* campaign, bool start = false, bool chapters = false) { selected.push_back(campaign); if (start) ++starts; else if (chapters) OpenRetainedCampaignChapters(); else OpenRetainedCampaignSkill(); }
    void StartMenu() { ++menus; guiActive = guiMainMenu; }
    // Another screen closes the SYSTEM page (CloseSystemSettings).
    void SetGUI(idUserInterface* gui, HandleGuiCommand_t handler) {
        if (guiSystem != nullptr && gui != guiSystem) { guiSystem = guiSystemParent = nullptr; guiSystemParentHandle = nullptr; ++systemCloses; }
        guiActive = gui; guiHandle = handler;
    }
    idUserInterface* SelectRetainedLoadingGui(idUserInterface*, bool);
    idUserInterface* FindRetainedGui(const char*, bool, bool);
    idUserInterface* RetainedHomeDocument(idUserInterface*&, const char*);
    bool RetainedPauseIsStrogg();
    bool RetainedSystemAvailable() const;
    void PreloadRetainedScreens(); void PrepareRetainedLevel(const char*, bool); void PrecacheRetainedLevelImages();
    void ReportRetainedScreens();
    idUserInterface *guiRetainedEscape = nullptr, *guiRetainedWelcome = nullptr, *guiRetainedMultiplayer = nullptr;
    idUserInterface *guiRetainedSummary = nullptr, *retainedMultiplayerTarget = nullptr, *guiRetainedArenaPause = nullptr;
    bool retainedMultiplayerWelcome = false, retainedMultiplayerSummary = false, retainedSummaryTyping = false;
    int retainedSummaryCaret = 0, summaryChats = 0, summaryChatEnds = 0;
    bool RetainedSummaryCovers() const;
    // The chat field's typing has its own source contracts (check_retained_multiplayer).
    void BeginRetainedSummaryChat(bool) { ++summaryChats; retainedSummaryTyping = true; }
    void EndRetainedSummaryChat(bool) { ++summaryChatEnds; retainedSummaryTyping = false; }
    bool retainedMultiplayerUncovered = false; int retainedMultiplayerRevision = -1;
    int retainedMultiplayerHandoff = -1, retainedMultiplayerHandoffUntil = 0;
    bool RetainedMultiplayerCovers() const; void UpdateRetainedMultiplayer(); void RetainedMultiplayerFrameEvent();
    void RetireRetainedMultiplayer(bool); void HandleRetainedMultiplayerRequest(idUserInterface*, const char*);
    void HandleGameMenuReturn(const char*);
    bool guiReleasingNone() const { return guiRetainedReleasing == nullptr; }
    // The loading hold has its own compiled case (check_loading_hold).
    void FadeRetainedLoadingHold(const char*) {}
};
static struct {
    bool ready = true;
    bool FinishPendingSave(bool) const { return ready; }
} sessLocal;
'''

MAIN = r'''
static const char* const DOCUMENTS[] = {"guis/menu/title.q4ui", "guis/menu/pause.q4ui", "guis/menu/pause_strogg.q4ui", "guis/loading/loading.q4ui",
    "guis/menu/singleplayer.q4ui", "guis/menu/campaigns.q4ui", "guis/menu/settings/system.q4ui", "guis/menu/mp_escape.q4ui",
    "guis/menu/mp_welcome.q4ui", "guis/menu/campaign_skill.q4ui", "guis/menu/chapters.q4ui", "guis/menu/mp_summary.q4ui",
    "guis/menu/loadgame.q4ui", "guis/menu/savegame.q4ui", "guis/menu/restartlevel.q4ui",
    "guis/menu/loadgame_strogg.q4ui", "guis/menu/savegame_strogg.q4ui", "guis/menu/restartlevel_strogg.q4ui",
    "guis/menu/demos.q4ui", "guis/menu/restartmenu.q4ui", "guis/menu/credits.q4ui",
    "guis/menu/arena.q4ui", "guis/menu/multiplayer.q4ui", "guis/menu/createserver.q4ui", "guis/menu/joingame.q4ui",
    "guis/menu/arena_pause.q4ui", "guis/menu/settings/audio.q4ui", "guis/menu/settings/game.q4ui",
    "guis/menu/settings/controls.q4ui", "guis/menu/settings.q4ui"};
// The documents that fell back to their stock screens, in order.
static std::vector<std::string> Stock(const idSessionLocal& session) {
    return std::vector<std::string>(session.retainedStock.begin(), session.retainedStock.end());
}
static idSessionLocal Session(bool gate, bool inGame = false) {
    managerObject = Manager{}; commonObject = Common{}; commands = CommandSystem{}; legacy.clear(); legacyActions.clear(); precached.clear(); stickX = stickY = 0;
    prefetched.clear();
    declManagerObject = DeclManager{}; precacheCost = 0; latePrecaches = 0;
    timeRuns.clear(); expressionWrites = 0; fileSystemObject.maps.clear();
    legacyActionAvailable = true; preview = false; ui_retained.value = gate; ui_retainedSystem.value = false; ui_retainedMultiplayer.value = false;
    systemCanReturn = true;
    fileSystemObject.files = std::set<std::string>(std::begin(DOCUMENTS), std::end(DOCUMENTS)); arenaCampaign = ArenaCampaign{};
    fileSystemObject.gameDirFiles.clear(); fileSystemObject.opens = fileSystemObject.closes = 0; cvarSystemObject = CVarSystem{};
    fileSystemObject.activeGameDir = "baseoq4"; fileSystemObject.modList = {}; fileSystemObject.modLists = fileSystemObject.modFrees = 0;
    legacyMissing.clear(); legacyEventsMissing.clear(); reloaded.clear(); describedShot = "savegames/quick.tga"; mapDecls.clear();
    fileSystemObject.screenshots.clear();
    saveSlots.clear(); driveSpace = 1000;
    gameObject = Game{}; idAsyncNetwork::client = AsyncClient{};
    static idUserInterface menu("guis/mainmenu.gui"); menu = idUserInterface("guis/mainmenu.gui");
    idSessionLocal session; session.guiActive = session.guiMainMenu = &menu; session.mapSpawned = inGame;
    legacy["desktop::curr"] = 0; legacy["desktop::active"] = 0; legacy["desktop::dest"] = 0;
    return session;
}
static std::string SaveRequest(idSessionLocal& s, idUserInterface* gui, const char* verb) {
    const int index = std::string(verb)=="loadSelect" || std::string(verb)=="saveSelect" ?
        s.retainedLoadFirst+gui->State().GetInt(verb[0]=='l' ? "load.pick":"save.pick","-1") : s.retainedLoadSelected;
    return std::string(verb)+" "+std::to_string(index)+" "+std::to_string(s.retainedSaveGeneration);
}

static void CheckSaveBrowser() {
    auto s=Session(true); s.saves={"zeta","alpha","beta","auto"};
    saveDescriptions["auto"]={"auto","^:Checkpoint\t^0Storage","gfx/guis/loadscreens/generic",true};
    saveDescriptions["zeta"]={"zeta","^1Zeta\t^0Bunker","",false};
    saveDescriptions["alpha"]={"alpha","^2Alpha\t^0Trenches","",false};
    saveDescriptions["beta"]={"beta","^3Beta\t^0Bunker","",false};
    s.PreloadRetainedScreens(); s.UpdateRetainedHome();
    s.HandleRetainedSessionRequest(s.guiRetainedHome,"loadGame"); commonObject.time+=550; s.UpdateRetainedHome(); s.UpdateRetainedPage();
    auto* page=s.guiActive;
    page->state["load.pick"]="2"; s.HandleRetainedSessionRequest(page,SaveRequest(s,page,"loadSelect").c_str());
    const std::string stale=SaveRequest(s,page,"loadDelete");
    page->focused="row-2";
    s.HandleRetainedSessionRequest(page,"saveListSort");
    CHECK(s.retainedLoadSlots[0]=="auto" && s.retainedLoadSlots[3]=="zeta");
    CHECK(s.retainedLoadSlots[s.retainedLoadSelected]=="beta" && page->named.back()=="browserFocus1");
    s.HandleRetainedSessionRequest(page,stale.c_str()); CHECK(s.deleted.empty());
    s.HandleRetainedSessionRequest(page,"saveListSort"); // name ascending, ignoring colors
    CHECK((s.retainedLoadSlots==idStrList{"alpha","beta","auto","zeta"}));
    s.HandleRetainedSessionRequest(page,"saveListSort");
    CHECK((s.retainedLoadSlots==idStrList{"zeta","auto","beta","alpha"}));
    s.HandleRetainedSessionRequest(page,"saveListSort"); // location, deterministic filename tie
    CHECK((s.retainedLoadSlots==idStrList{"beta","zeta","auto","alpha"}));
    s.HandleRetainedSessionRequest(page,"saveListSort");
    CHECK((s.retainedLoadSlots==idStrList{"alpha","auto","beta","zeta"}));
    s.HandleRetainedSessionRequest(page,"saveListSort"); CHECK(page->state["browser.sort"]=="0");
    page->focused="browser-manual";
    s.HandleRetainedSessionRequest(page,"saveListFilterManual");
    CHECK(page->state["load.count"]=="3" && page->state["browser.total"]=="4" && s.retainedLoadSlots.FindIndex("auto")==-1);
    s.HandleRetainedSessionRequest(page,"saveListFilterAuto");
    CHECK(page->state["load.count"]=="1" && s.retainedLoadSelected==-1 && s.retainedLoadSlots[0]=="auto");
    s.HandleRetainedSessionRequest(page,"saveListFilterAuto"); CHECK(page->state["load.count"]=="4" && page->state["browser.filter"]=="0");
    page->state["load.pick"]="2"; s.HandleRetainedSessionRequest(page,SaveRequest(s,page,"loadSelect").c_str());
    page->focused="row-2";
    s.saves.insert(s.saves.begin(),"new");
    s.HandleRetainedSessionRequest(page,"saveListRefresh");
    CHECK(s.retainedLoadSelected==3 && page->named.back()=="browserFocus3");
    s.saves.erase(std::remove(s.saves.begin(),s.saves.end(),"beta"),s.saves.end());
    page->focused="row-3";
    s.HandleRetainedSessionRequest(page,"saveListRefresh");
    CHECK(s.retainedLoadSelected==-1 && page->named.back()=="browserFocusRefresh");
    int index=-1;
    for(const auto* bad:{"loadDelete", "loadDelete 1", "loadDelete -1 1", "loadDelete 0 2147483648", "loadDelete 4294967296 1", "loadDelete 1 1 trailing"})
        CHECK(!Session_RetainedSaveOperand(bad,"loadDelete",1,4,index));
    CHECK(Session_RetainedSaveOperand("loadDelete 3 1","loadDelete",1,4,index) && index==3);
    CHECK(!Session_RetainedSaveOperand("loadDelete 4 1","loadDelete",1,4,index));
    // Focus and selected save survive crossing the catalog's page boundary.
    s.saves.clear(); for(int i=0;i<70;++i) s.saves.push_back("entry"+std::to_string(i));
    page->state["browser.sort"]="0"; page->state["browser.filter"]="0";
    s.PublishRetainedLoadPage(page,63,nullptr); page->focused="row-63";
    s.saves.insert(s.saves.begin(),"newest"); s.HandleRetainedSessionRequest(page,"saveListRefresh");
    CHECK(s.retainedLoadSelected==64 && s.retainedLoadFirst==64 && page->named.back()=="browserFocus0");
    // Save refresh preserves a draft and selects a newly written file under a name sort.
    auto t=Session(true,true); t.saves={"alpha","zeta"}; t.PreloadRetainedScreens(); t.PrepareRetainedLevel("game/airdefense1",false); t.UpdateRetainedHome();
    t.HandleRetainedSessionRequest(t.guiRetainedHome,"saveGame"); commonObject.time+=550; t.UpdateRetainedHome(); t.UpdateRetainedPage();
    auto* save=t.guiActive; save->state["save.name"]="typed name"; save->state["ui.numberDraftsPending"]="1";
    t.HandleRetainedSessionRequest(save,"saveListRefresh"); CHECK(save->state["save.name"]=="typed name");
    save->state["ui.numberDraftsPending"]="0"; save->state["browser.sort"]="2";
    save->state["browser.filter"]="2"; save->state["save.name"]="Middle name";
    t.HandleRetainedSessionRequest(save,"saveSave");
    CHECK(save->state["browser.sort"]=="2" && save->state["browser.filter"]=="0");
    CHECK(t.retainedLoadSelected==1 && t.retainedLoadSlots[1]=="Middle_name" && save->state["save.name"]=="Middle name");
    saveDescriptions.clear();
}

int main() {
    CheckSaveBrowser();
    {   // Gate off: the stock interface presents everything and nothing retained loads.
        auto s = Session(false);
        for (int frame = 0; frame < 4; ++frame) s.UpdateRetainedHome();
        CHECK(s.guiRetainedHome == nullptr && managerObject.loads.empty());
        idUserInterface stock("guis/loading/generic.gui");
        CHECK(s.SelectRetainedLoadingGui(&stock, false) == &stock && managerObject.loads.empty());
        s.PreloadRetainedScreens(); s.PrepareRetainedLevel("game/airdefense1", false);
        CHECK(managerObject.loads.empty() && precached.empty());
        s.HandleRetainedSessionRequest(s.guiMainMenu, "quit");
        CHECK(commonObject.quits == 0 && legacyActions.empty());
        s.ReportRetainedScreens();
        CHECK(commonObject.output.find("OPENQ4_RETAINED enabled=0 system=0 multiplayer=0 welcome=0 home=- title=0 pause=0 strogg=0 handoff=0 release=0 mp=- views=0 stock=-") != std::string::npos);
        // Off, the campaign selectors are the stock ones and nothing retained loads.
        s.OpenCampaignSelector(false); CHECK(arenaCampaign.selectors == 1 && managerObject.loads.empty());
        s.OpenCampaignSelector(true); CHECK((managerObject.loads == std::vector<std::string>{"guis/campaign_menu.gui"}));
        CHECK(!s.RetainedSystemAvailable());
        managerObject.loads.clear(); s.guiActive = s.guiMainMenu;
        ui_retainedSystem.value = true;
        CHECK(Session_RetainedSystemEnabled() && !Session_RetainedScreensEnabled() && s.RetainedSystemAvailable());
        s.UpdateRetainedHome(); CHECK(s.guiRetainedHome == nullptr && managerObject.loads.empty());
    }
    {   // Title: presents over the legacy home state, describes the newest save.
        auto s = Session(true); s.saves = {"quick", "autosave"};
        legacy["desktop::video_check"] = 1; s.UpdateRetainedHome();
        CHECK(s.guiRetainedHome == nullptr && managerObject.loads.empty()); // logo videos stay legacy
        legacy["desktop::video_check"] = 0; s.UpdateRetainedHome();
        CHECK(s.guiRetainedHome && std::string(s.guiRetainedHome->Name()) == "guis/menu/title.q4ui");
        CHECK(managerObject.loads.size() == 1 && managerObject.flags[0].autoLoad && managerObject.flags[0].unique && !managerObject.flags[0].shared);
        CHECK(s.guiRetainedHome->active && s.guiRetainedHome->named.back() == "open");
        // The save's name, then its level and, for a save from an earlier build,
        // which records no difficulty or play time, when it was written; color
        // codes removed.
        CHECK(s.guiRetainedHome->state["menu_continue"] == "1" && s.guiRetainedHome->state["menu_continue_title"] == "quick");
        CHECK(s.guiRetainedHome->state["menu_continue_detail"] == "Air Defense Bunker \xc2\xb7 09/29/2026 12:40pm");
        CHECK(s.guiRetainedHome->state["menu_continue_shot"] == "savegames/quick.tga");
        s.UpdateRetainedHome(); CHECK(managerObject.loads.size() == 1 && s.guiRetainedHome->activations == 1);
        auto* title = s.guiRetainedHome;
        // Depth: the pointer reaches the title in -1..1, and only when it moves.
        title->cursorX = 640; title->cursorY = 120; s.RetainedHomeFrameEvent();
        CHECK(title->state["pointer_x"] == std::to_string(1.0f) && title->state["pointer_y"] == std::to_string(-0.5f));
        title->state.erase("pointer_x"); s.RetainedHomeFrameEvent(); CHECK(!title->state.count("pointer_x"));
        // A held look stick leads; released, the lean returns to the pointer.
        stickX = -127; s.RetainedHomeFrameEvent(); CHECK(title->state["pointer_x"] == std::to_string(-1.0f));
        stickX = 10; s.RetainedHomeFrameEvent(); CHECK(title->state["pointer_x"] == std::to_string(1.0f));
        // The VR pointer moves by its offset from the covered menu's cursor:
        // in VR that cursor follows the screen's, and only in VR.
        CHECK(s.guiMainMenu->cursorX == 320 && s.guiMainMenu->cursorY == 240);
        vrInputFocus = true; s.RetainedHomeFrameEvent(); vrInputFocus = false;
        CHECK(s.guiMainMenu->cursorX == 640 && s.guiMainMenu->cursorY == 120);
        // A stock page hand-off runs the legacy home button's own action: here
        // SETTINGS over a main menu without the route to its settings screen,
        // which keeps its own (the SETTINGS page has its own case).
        legacyEventsMissing = {"openSettingsPage"};
        s.HandleRetainedSessionRequest(title, "settings");
        legacyEventsMissing.clear();
        CHECK(!legacyActions.empty() && legacyActions.back() == "main_b_settings" && title->named.back() == "depart");
        // Its label carries into the page's title slot as the bands dock.
        CHECK(title->named.size() >= 2 && title->named[title->named.size() - 2] == "carry_settings");
        CHECK(s.dispatched.back() == "play main_menu_selection" && s.RetainedHomeInputBlocked());
        legacy["desktop::active"] = 1; legacy["desktop::dest"] = 4;
        commonObject.time += 549; s.UpdateRetainedHome(); CHECK(s.guiRetainedHome == title);
        commonObject.time += 2; s.UpdateRetainedHome(); CHECK(s.guiRetainedHome == nullptr && !title->active);
        legacy["desktop::curr"] = 4; legacy["desktop::active"] = 0; s.UpdateRetainedHome(); CHECK(s.guiRetainedHome == nullptr);
        // Back on the page: the retained title returns with the bands at once.
        legacy["desktop::active"] = 1; legacy["desktop::dest"] = 0; s.UpdateRetainedHome();
        CHECK(s.guiRetainedHome == title && title->named.back() == "returnHome" && !s.RetainedHomeInputBlocked());
        CHECK(managerObject.loads.size() == 1);
        // UPDATES runs its check in place: no pop-up opens, so the title keeps
        // its content and its input; the answer comes as a message box.
        {
            const size_t before = title->named.size();
            s.HandleRetainedSessionRequest(title, "updates");
            CHECK(legacyActions.back() == "main_b_updates" && title->named.size() == before && !s.RetainedHomeInputBlocked());
            CHECK(s.dispatched.back() == "play main_menu_selection");
            s.UpdateRetainedHome(); CHECK(s.guiRetainedHome == title && title->active);
        }
        // The box covers the title, which stays behind it without input, and
        // takes the screen back as it was when the box closes.
        {
            idUserInterface box("guis/msg.gui"); s.guiMsg = &box;
            const size_t before = title->named.size(); const int deactivations = title->deactivations;
            s.guiMsgRestore = s.guiMainMenu; s.guiActive = &box; s.UpdateRetainedHome();
            CHECK(s.guiRetainedHome == title && !title->active && title->deactivations == deactivations + 1 && s.retainedHomeUnderMessage);
            s.UpdateRetainedHome(); CHECK(s.guiRetainedHome == title && title->deactivations == deactivations + 1);
            s.guiActive = s.guiMainMenu; s.guiMsgRestore = nullptr; s.UpdateRetainedHome();
            CHECK(s.guiRetainedHome == title && title->active && !s.retainedHomeUnderMessage && title->named.size() == before);
            s.guiMsg = nullptr;
        }
        // Closing a stock pop-up over the home only reveals the content.
        legacy["desktop::curr"] = 5; legacy["desktop::active"] = 0; s.UpdateRetainedHome(); CHECK(s.guiRetainedHome == nullptr);
        legacy["desktop::active"] = 1; legacy["desktop::dest"] = 0; s.UpdateRetainedHome(); CHECK(s.guiRetainedHome == title && title->named.back() == "open");
        // Session verbs never become console text; quit uses the stock path.
        s.HandleRetainedSessionRequest(title, "continue"); CHECK(s.loadedGames == std::vector<std::string>{"quick"});
        idUserInterface stranger("guis/other.q4ui"); s.HandleRetainedSessionRequest(&stranger, "quit"); CHECK(commonObject.quits == 0);
        s.HandleRetainedSessionRequest(title, "resume"); CHECK(s.exits == 0); // the title has no game to resume
        s.HandleRetainedSessionRequest(title, "unknown"); CHECK(commonObject.warnings.back().find("unhandled session request") != std::string::npos);
        legacyActionAvailable = false; legacyEventsMissing = {"openSettingsPage"};
        s.HandleRetainedSessionRequest(title, "settings"); CHECK(!s.RetainedHomeInputBlocked()); legacyEventsMissing.clear();
        CHECK(commonObject.warnings.back().find("unavailable from the current menu") != std::string::npos);
        s.HandleRetainedSessionRequest(title, "quit"); CHECK(commonObject.quits == 1 && s.exits == 1);
        s.UpdateRetainedHome(); CHECK(s.guiRetainedHome == nullptr && !title->active); // the menu closed
    }
    {   // The Load Game page (FLOW-004). LOAD GAME hands off as to a stock page (the
        // bands dock, the label carries) without loading anything at the click,
        // and the page presents at 550 ms over the home, which keeps drawing
        // behind it. Its verbs act on the session's saves, newest first.
        auto s = Session(true); s.saves = {"quick", "autosave", "medic"};
        s.PreloadRetainedScreens(); s.UpdateRetainedHome();
        auto* title = s.guiRetainedHome; auto* page = s.guiRetainedLoad;
        CHECK(title && page && std::string(page->Name()) == "guis/menu/loadgame.q4ui");
        const size_t loads = managerObject.loads.size();
        s.HandleRetainedSessionRequest(title, "loadGame");
        CHECK(legacyActions.empty() && managerObject.loads.size() == loads && s.retainedPagePending == page);
        CHECK(title->named[title->named.size() - 2] == "carry_loadGame" && title->named.back() == "depart");
        CHECK(s.played.back() == "play main_menu_selection" && s.RetainedHomeInputBlocked());
        CHECK(page->state["load.count"] == "3" && page->state["load.first"] == "0" && page->state["load.selected"] == "0");
        CHECK(page->state["load.row0.name"] == "quick" && page->state["load.row0.location"] == "Air Defense Bunker" &&
              page->state["load.row2.name"] == "medic" && page->state["load.row3.name"].empty());
        CHECK(page->state["load.date"] == "09/29/2026" && page->state["load.time"] == "12:40pm" && page->state["load.error"].empty());
        CHECK(page->state["load.shot"] == "savegames/quick.tga");
        CHECK(page->state["load.skill"].empty() && page->state["load.played"].empty());  // a save from an earlier build
        s.HandleRetainedSessionRequest(title, "loadGame"); CHECK(legacyActions.empty());  // one hand-off at a time
        commonObject.time += 549; s.UpdateRetainedHome(); s.UpdateRetainedPage();
        CHECK(s.guiActive == s.guiMainMenu && s.guiRetainedHome == title);
        commonObject.time += 1; s.UpdateRetainedHome(); s.UpdateRetainedPage();
        CHECK(s.guiActive == page && s.retainedPageHome == title && s.guiRetainedHome == nullptr && !title->active);
        CHECK(page->state["load.ingame"] == "0" && s.retainedPagePending == nullptr);
        // A row reports itself; the session checks it against the saves it shows.
        page->state["load.pick"] = "2"; s.HandleRetainedSessionRequest(page, SaveRequest(s, page, "loadSelect").c_str());
        CHECK(s.retainedLoadSelected == 2 && page->state["load.selected"] == "2");
        page->state["load.pick"] = "7"; s.HandleRetainedSessionRequest(page, SaveRequest(s, page, "loadSelect").c_str()); CHECK(s.retainedLoadSelected == 2);
        // A refused load says why over the picture, and the page stays.
        s.loadRefusal = idSessionLocal::LOADGAME_FAILURE_MAP; s.HandleRetainedSessionRequest(page, SaveRequest(s, page, "loadSave").c_str());
        CHECK(s.loadedGames.back() == "medic" && page->state["load.error"] == "#str_231102" && s.guiActive == page);
        s.loadRefusal = idSessionLocal::LOADGAME_FAILURE_DAMAGED; s.HandleRetainedSessionRequest(page, SaveRequest(s, page, "loadSave").c_str());
        CHECK(page->state["load.error"] == "#str_231103");
        s.loadRefusal = idSessionLocal::LOADGAME_FAILURE_UNREADABLE; s.HandleRetainedSessionRequest(page, SaveRequest(s, page, "loadSave").c_str());
        CHECK(page->state["load.error"] == "#str_231104");
        s.loadRefusal = idSessionLocal::LOADGAME_FAILURE_PLATFORM; s.HandleRetainedSessionRequest(page, SaveRequest(s, page, "loadSave").c_str());
        CHECK(page->state["load.error"] == "#str_231192");  // another platform's build wrote it
        page->state["load.pick"] = "1"; s.HandleRetainedSessionRequest(page, SaveRequest(s, page, "loadSelect").c_str());
        CHECK(s.retainedLoadSelected == 1 && page->state["load.error"].empty());  // another choice clears the reason
        // DELETE GAME (the page asks first): the next save takes the deleted one's place.
        s.HandleRetainedSessionRequest(page, SaveRequest(s, page, "loadDelete").c_str());
        CHECK(s.deleted.back() == "autosave" && page->state["load.count"] == "2" && page->state["load.selected"] == "1" &&
              page->state["load.row1.name"] == "medic" && page->state["load.row2.name"].empty());
        // A save that is still there stays selected and says so.
        s.deleteFails = true; s.HandleRetainedSessionRequest(page, SaveRequest(s, page, "loadDelete").c_str());
        CHECK(s.deleted.back() == "medic" && page->state["load.count"] == "2" && page->state["load.selected"] == "1" &&
              page->state["load.error"] == "#str_231105");
        s.deleteFails = false;
        s.loadRefusal = idSessionLocal::LOADGAME_FAILURE_NONE; s.HandleRetainedSessionRequest(page, SaveRequest(s, page, "loadSave").c_str());
        CHECK(s.loadedGames.back() == "medic");
        // Deleting the last save selects the one before it; deleting every save leaves none selected.
        s.HandleRetainedSessionRequest(page, SaveRequest(s, page, "loadDelete").c_str()); CHECK(page->state["load.selected"] == "0" && page->state["load.count"] == "1");
        s.HandleRetainedSessionRequest(page, SaveRequest(s, page, "loadDelete").c_str());
        CHECK(page->state["load.count"] == "0" && page->state["load.selected"] == "-1" && page->state["load.shot"] == "gfx/guis/loadscreens/generic");
        const size_t deletes = s.deleted.size(), attempts = s.loadedGames.size();
        s.HandleRetainedSessionRequest(page, SaveRequest(s, page, "loadDelete").c_str()); s.HandleRetainedSessionRequest(page, SaveRequest(s, page, "loadSave").c_str());
        CHECK(s.deleted.size() == deletes && s.loadedGames.size() == attempts);  // nothing selected, nothing to do
        // Back returns home with the bands, at once.
        s.HandleRetainedSessionRequest(page, "loadBack");
        CHECK(s.guiActive == s.guiMainMenu && s.guiRetainedHome == title && title->active && title->named.back() == "returnHome");
        CHECK(s.retainedPageHome == nullptr && !s.retainedPageReturn && !s.RetainedHomeInputBlocked());
        CHECK(title->state["menu_continue"] == "0");  // the home reads the saves again
        // A page that is not presenting takes no request.
        const size_t played = s.played.size(); s.HandleRetainedSessionRequest(page, "loadBack"); s.HandleRetainedSessionRequest(page, "loadNewGame");
        CHECK(s.played.size() == played && s.guiActive == s.guiMainMenu);
        // Opening it again lists the saves afresh; NEW GAME opens the Single Player page.
        s.HandleRetainedSessionRequest(title, "loadGame"); commonObject.time += 550; s.UpdateRetainedHome(); s.UpdateRetainedPage();
        CHECK(s.guiActive == page && page->state["load.count"] == "0");
        s.HandleRetainedSessionRequest(page, "loadNewGame"); CHECK(std::string(s.guiActive->Name()) == "guis/menu/singleplayer.q4ui");
        s.HandleRetainedSessionRequest(page, "unknown"); CHECK(commonObject.warnings.empty());  // not presenting: ignored
    }
    {   // More saves than the page's rows: it pages, selecting the older page's first
        // save or the newer page's last.
        auto s = Session(true);
        for (int i = 0; i < 70; ++i) s.saves.push_back("save" + std::to_string(i));
        s.PreloadRetainedScreens(); s.UpdateRetainedHome(); auto* page = s.guiRetainedLoad;
        s.HandleRetainedSessionRequest(s.guiRetainedHome, "loadGame"); commonObject.time += 550; s.UpdateRetainedHome(); s.UpdateRetainedPage();
        CHECK(s.guiActive == page && page->state["load.count"] == "70" && page->state["load.row63.name"] == "save63");
        s.HandleRetainedSessionRequest(page, "loadOlder");
        CHECK(page->state["load.first"] == "64" && page->state["load.selected"] == "64" && page->state["load.row0.name"] == "save64" &&
              page->state["load.row5.name"] == "save69" && page->state["load.row6.name"].empty() && page->named.back() == "pagedOlder");
        s.HandleRetainedSessionRequest(page, "loadOlder"); CHECK(page->state["load.first"] == "64");  // the last page
        page->state["load.pick"] = "5"; s.HandleRetainedSessionRequest(page, SaveRequest(s, page, "loadSelect").c_str()); CHECK(s.retainedLoadSelected == 69);
        page->state["load.pick"] = "6"; s.HandleRetainedSessionRequest(page, SaveRequest(s, page, "loadSelect").c_str()); CHECK(s.retainedLoadSelected == 69);
        // Deleting on the second page keeps its window around the selection.
        s.HandleRetainedSessionRequest(page, SaveRequest(s, page, "loadDelete").c_str());
        CHECK(page->state["load.first"] == "64" && page->state["load.selected"] == "68" && page->state["load.count"] == "69");
        s.HandleRetainedSessionRequest(page, "loadNewer");
        CHECK(page->state["load.first"] == "0" && page->state["load.selected"] == "63" && page->named.back() == "pagedNewer");
        s.HandleRetainedSessionRequest(page, "loadNewer"); CHECK(page->state["load.first"] == "0");  // the first page
    }
    {   // In game the Marine pause hands off the same way, and NEW GAME is not
        // offered; the Strogg pause hands off to the Strogg family's page. A
        // page that is not installed, cannot load or stopped drawing falls back
        // to the stock page, never to the other family's.
        auto s = Session(true, true); s.saves = {"quick"}; s.PreloadRetainedScreens(); s.UpdateRetainedHome();
        auto* pause = s.guiRetainedHome; CHECK(pause && std::string(pause->Name()) == "guis/menu/pause.q4ui");
        s.HandleRetainedSessionRequest(pause, "loadGame"); commonObject.time += 550; s.UpdateRetainedHome(); s.UpdateRetainedPage();
        auto* page = s.guiActive;
        CHECK(page == s.guiRetainedLoad && page->state["load.ingame"] == "1" && s.retainedPageHome == pause && legacyActions.empty());
        s.HandleRetainedSessionRequest(page, "loadNewGame"); CHECK(s.guiActive == page);
        s.HandleRetainedSessionRequest(page, "loadBack");
        CHECK(s.guiRetainedHome == pause && pause->named.back() == "returnHome" && s.guiReleasingNone());
        // The Menu button on the page closes the menus and resumes play: the
        // page returns the pause, whose RESUME closes it (section 13.5).
        s.HandleRetainedSessionRequest(pause, "loadGame"); commonObject.time += 550; s.UpdateRetainedHome(); s.UpdateRetainedPage();
        CHECK(s.guiActive == page && s.RetainedPageActive());
        const int exits = s.exits; s.HandleRetainedSessionRequest(page, "menuResume");
        CHECK(s.exits == exits + 1 && !s.RetainedPageActive() && s.retainedPageHome == nullptr);
        // At the title there is no game to resume: nothing happens there.
        auto title = Session(true); title.saves = {"quick"}; title.PreloadRetainedScreens(); title.UpdateRetainedHome();
        title.HandleRetainedSessionRequest(title.guiRetainedHome, "loadGame"); commonObject.time += 550;
        title.UpdateRetainedHome(); title.UpdateRetainedPage();
        auto* titlePage = title.guiActive; CHECK(titlePage == title.guiRetainedLoad && titlePage->state["load.ingame"] == "0");
        title.HandleRetainedSessionRequest(titlePage, "menuResume");
        CHECK(title.exits == 0 && title.guiActive == titlePage && title.RetainedPageActive());
        auto strogg = Session(true, true); strogg.PreloadRetainedScreens();
        gameObject.publish = [](idUserInterface* gui) { gui->SetStateBool("pause_strogg", true); };
        strogg.UpdateRetainedHome(); CHECK(std::string(strogg.guiRetainedHome->Name()) == "guis/menu/pause_strogg.q4ui");
        // The first Strogg answer of a level starts parsing the Strogg pages.
        CHECK((prefetched == std::vector<std::string>{"guis/menu/loadgame_strogg.q4ui", "guis/menu/savegame_strogg.q4ui",
                                                     "guis/menu/restartlevel_strogg.q4ui"}));
        auto* stroggPause = strogg.guiRetainedHome; const size_t stroggActions = legacyActions.size();
        strogg.HandleRetainedSessionRequest(stroggPause, "loadGame");
        CHECK(legacyActions.size() == stroggActions && strogg.guiRetainedLoadStrogg != nullptr &&
              strogg.retainedPagePending == strogg.guiRetainedLoadStrogg &&
              std::string(strogg.guiRetainedLoadStrogg->Name()) == "guis/menu/loadgame_strogg.q4ui");
        CHECK(stroggPause->named[stroggPause->named.size() - 2] == "carry_loadGame" && stroggPause->named.back() == "depart");
        commonObject.time += 550; strogg.UpdateRetainedHome(); strogg.UpdateRetainedPage();
        auto* stroggPage = strogg.guiActive;
        CHECK(stroggPage == strogg.guiRetainedLoadStrogg && stroggPage->state["load.ingame"] == "1" && strogg.RetainedPageActive() &&
              strogg.retainedPageHome == stroggPause && stroggPage->state["load.count"] == "0");
        strogg.HandleRetainedSessionRequest(stroggPage, "loadBack");
        CHECK(strogg.guiRetainedHome == stroggPause && stroggPause->named.back() == "returnHome" && !strogg.RetainedPageActive());
        auto stroggMissing = Session(true, true); fileSystemObject.files.erase("guis/menu/loadgame_strogg.q4ui");
        gameObject.publish = [](idUserInterface* gui) { gui->SetStateBool("pause_strogg", true); };
        stroggMissing.PreloadRetainedScreens(); stroggMissing.UpdateRetainedHome();
        stroggMissing.HandleRetainedSessionRequest(stroggMissing.guiRetainedHome, "loadGame");
        CHECK(legacyActions.back() == "main_b_loadgame" && stroggMissing.retainedPagePending == nullptr &&
              (Stock(stroggMissing) == std::vector<std::string>{"guis/menu/loadgame_strogg.q4ui"}));
        gameObject.publish = nullptr;
        auto missing = Session(true); fileSystemObject.files.erase("guis/menu/loadgame.q4ui");
        missing.PreloadRetainedScreens(); missing.UpdateRetainedHome();
        CHECK(missing.guiRetainedLoad == nullptr && commonObject.warnings.empty() && precached.empty());
        missing.HandleRetainedSessionRequest(missing.guiRetainedHome, "loadGame");
        CHECK(legacyActions.back() == "main_b_loadgame" && (Stock(missing) == std::vector<std::string>{"guis/menu/loadgame.q4ui"}));
        auto late = Session(true); late.UpdateRetainedHome();  // the gate came on after startup: the page loads at the click
        late.HandleRetainedSessionRequest(late.guiRetainedHome, "loadGame");
        CHECK(late.guiRetainedLoad && managerObject.loads.back() == "guis/menu/loadgame.q4ui" && legacyActions.empty());
        auto failed = Session(true); failed.PreloadRetainedScreens(); failed.UpdateRetainedHome();
        failed.guiRetainedLoad->failed = true;
        failed.HandleRetainedSessionRequest(failed.guiRetainedHome, "loadGame");
        CHECK(legacyActions.back() == "main_b_loadgame" && failed.guiRetainedLoad == nullptr && commonObject.warnings.size() == 1);
        CHECK(commonObject.warnings[0].find("stopped drawing") != std::string::npos);
        auto off = Session(false); off.PreloadRetainedScreens(); CHECK(off.guiRetainedLoad == nullptr && managerObject.loads.empty());
    }
    {   // The Save Game page (FLOW-004): the Marine pause's SAVE GAME hands off to it
        // as LOAD GAME does, with no name and no save selected. A row fills the
        // name unless one is being typed; SAVE GAME saves under the name, asks
        // first over a save, refuses an autosave and says why a save failed.
        auto s = Session(true, true); s.saves = {"quick", "medic"};
        s.PreloadRetainedScreens(); s.PrepareRetainedLevel("game/airdefense1", false); s.UpdateRetainedHome();
        auto* pause = s.guiRetainedHome; auto* page = s.guiRetainedSave;
        CHECK(pause && page && std::string(page->Name()) == "guis/menu/savegame.q4ui");
        s.HandleRetainedSessionRequest(pause, "saveGame");
        CHECK(legacyActions.empty() && s.retainedPagePending == page && s.played.back() == "play main_menu_selection");
        CHECK(pause->named[pause->named.size() - 2] == "carry_saveGame" && pause->named.back() == "depart");
        CHECK(page->state["save.count"] == "2" && page->state["save.selected"] == "-1" && page->state["save.name"].empty() &&
              page->state["save.row1.name"] == "medic" && page->state["save.row1.location"] == "Air Defense Bunker" &&
              page->state["save.row2.name"].empty() && !page->state.count("load.count"));
        commonObject.time += 550; s.UpdateRetainedHome(); s.UpdateRetainedPage();
        CHECK(s.guiActive == page && s.retainedPageHome == pause && s.RetainedPageActive() && !page->state.count("load.ingame"));
        // A row fills the name, unless a name is being typed there.
        page->state["save.pick"] = "1"; s.HandleRetainedSessionRequest(page, SaveRequest(s, page, "saveSelect").c_str());
        CHECK(s.retainedLoadSelected == 1 && page->state["save.selected"] == "1" && page->state["save.name"] == "medic");
        page->state["ui.numberDraftsPending"] = "1"; page->state["save.pick"] = "0"; s.HandleRetainedSessionRequest(page, SaveRequest(s, page, "saveSelect").c_str());
        CHECK(page->state["save.selected"] == "0" && page->state["save.name"] == "medic");
        page->state["ui.numberDraftsPending"] = "0";
        page->state["save.pick"] = "5"; s.HandleRetainedSessionRequest(page, SaveRequest(s, page, "saveSelect").c_str()); CHECK(s.retainedLoadSelected == 0);
        // A new name saves at once; the save is the newest, selected, and the name stays.
        page->state["save.name"] = "Bunker door"; s.HandleRetainedSessionRequest(page, "saveSave");
        CHECK(s.savedGames.back() == "Bunker door" && page->state["save.count"] == "3" && page->state["save.selected"] == "0" &&
              page->state["save.row0.name"] == "Bunker door" && page->state["save.name"] == "Bunker door");
        const size_t attempts = s.savedGames.size();
        page->state["save.name"] = "   "; s.HandleRetainedSessionRequest(page, "saveSave"); CHECK(s.savedGames.size() == attempts);
        // Over a save the page asks first, naming it; YES saves over that save, once.
        saveSlots["medic"] = SESSION_SAVE_SLOT_TAKEN; page->state["save.name"] = "medic"; s.HandleRetainedSessionRequest(page, "saveSave");
        CHECK(s.savedGames.size() == attempts && page->named.back() == "overwriteModalShow" && page->state["save.target"] == "medic");
        page->state["save.name"] = "changed"; s.HandleRetainedSessionRequest(page, "saveOverwrite");
        CHECK(s.savedGames.back() == "medic" && s.retainedSaveTarget.empty());
        s.HandleRetainedSessionRequest(page, "saveOverwrite"); CHECK(s.savedGames.size() == attempts + 1);
        // An autosave is refused, and so is a save the session cannot make, with its reason.
        saveSlots["quick"] = SESSION_SAVE_SLOT_AUTOSAVE; page->state["save.name"] = "quick"; s.HandleRetainedSessionRequest(page, "saveSave");
        CHECK(page->state["save.notice"] == "#str_200982" && page->named.back() == "noticeModalShow" && s.savedGames.size() == attempts + 1);
        page->state["save.name"] = "fresh";
        gameObject.health = 0; s.HandleRetainedSessionRequest(page, "saveSave"); CHECK(page->state["save.notice"] == "#str_231111");
        gameObject.health = 100; driveSpace = 10; s.HandleRetainedSessionRequest(page, "saveSave"); CHECK(page->state["save.notice"] == "#str_231112");
        driveSpace = 1000; s.objectiveFailed = true; s.HandleRetainedSessionRequest(page, "saveSave"); CHECK(page->state["save.notice"] == "#str_231113");
        s.objectiveFailed = false; gameObject.cinematic = true; s.HandleRetainedSessionRequest(page, "saveSave");
        CHECK(page->state["save.notice"] == "#str_231110" && s.savedGames.size() == attempts + 1);
        gameObject.cinematic = false; s.saveRefuses = true; s.HandleRetainedSessionRequest(page, "saveSave");
        CHECK(page->state["save.notice"] == "#str_231110" && s.savedGames.back() == "fresh" && s.savedGames.size() == attempts + 2);
        s.saveRefuses = false;
        // Back returns to the pause with the bands; each arrival starts afresh.
        s.HandleRetainedSessionRequest(page, "saveBack");
        CHECK(s.guiActive == s.guiMainMenu && s.guiRetainedHome == pause && pause->named.back() == "returnHome" && !s.RetainedPageActive());
        const size_t played = s.played.size(); s.HandleRetainedSessionRequest(page, "saveSave"); CHECK(s.played.size() == played);
        s.HandleRetainedSessionRequest(pause, "saveGame"); commonObject.time += 550; s.UpdateRetainedHome(); s.UpdateRetainedPage();
        CHECK(s.guiActive == page && page->state["save.name"].empty() && page->state["save.selected"] == "-1");
        s.HandleRetainedSessionRequest(page, "unknown");
        CHECK(commonObject.warnings.size() == 1 && commonObject.warnings[0].find("Save Game") != std::string::npos);
    }
    {   // SAVE GAME needs a game to save, and the Strogg pause hands off to the
        // Strogg family's page; a missing page falls back to the stock one.
        auto title = Session(true); title.PreloadRetainedScreens(); title.UpdateRetainedHome();
        title.HandleRetainedSessionRequest(title.guiRetainedHome, "saveGame");
        CHECK(legacyActions.back() == "main_b_savegame" && title.retainedPagePending == nullptr);
        auto strogg = Session(true, true); strogg.PreloadRetainedScreens();
        gameObject.publish = [](idUserInterface* gui) { gui->SetStateBool("pause_strogg", true); };
        strogg.UpdateRetainedHome(); const size_t stroggActions = legacyActions.size();
        strogg.HandleRetainedSessionRequest(strogg.guiRetainedHome, "saveGame");
        CHECK(legacyActions.size() == stroggActions && strogg.guiRetainedSaveStrogg != nullptr &&
              strogg.retainedPagePending == strogg.guiRetainedSaveStrogg && strogg.guiRetainedSave == nullptr &&
              std::string(strogg.guiRetainedSaveStrogg->Name()) == "guis/menu/savegame_strogg.q4ui");
        commonObject.time += 550; strogg.UpdateRetainedHome(); strogg.UpdateRetainedPage();
        auto* stroggPage = strogg.guiActive;
        CHECK(stroggPage == strogg.guiRetainedSaveStrogg && strogg.RetainedPageActive());
        stroggPage->state["save.name"] = "Strogg door"; strogg.HandleRetainedSessionRequest(stroggPage, "saveSave");
        CHECK(strogg.savedGames.back() == "Strogg door" && stroggPage->state["save.row0.name"] == "Strogg door" &&
              !stroggPage->state.count("load.row0.name"));
        gameObject.publish = nullptr;
        auto missing = Session(true, true); fileSystemObject.files.erase("guis/menu/savegame.q4ui");
        missing.PreloadRetainedScreens(); missing.UpdateRetainedHome();
        missing.HandleRetainedSessionRequest(missing.guiRetainedHome, "saveGame");
        CHECK(missing.guiRetainedSave == nullptr && legacyActions.back() == "main_b_savegame" &&
              (Stock(missing) == std::vector<std::string>{"guis/menu/savegame.q4ui"}));
    }
    {   // The Restart Level page (FLOW-050): the Marine pause's RESTART LEVEL hands off to
        // it as LOAD GAME does, at the level's difficulty. The choice stays on the
        // page; RESTART LEVEL restarts the level at it and Back changes nothing.
        auto s = Session(true, true); cvarSystemObject.strings["g_skill"] = "3";
        s.PreloadRetainedScreens(); s.PrepareRetainedLevel("game/airdefense1", false); s.UpdateRetainedHome();
        auto* pause = s.guiRetainedHome; auto* page = s.guiRetainedRestart;
        CHECK(pause && page && std::string(page->Name()) == "guis/menu/restartlevel.q4ui");
        s.HandleRetainedSessionRequest(pause, "restartLevel");
        CHECK(legacyActions.empty() && s.retainedPagePending == page && page->state["restart.skill"] == "3");
        CHECK(pause->named[pause->named.size() - 2] == "carry_restartLevel" && pause->named.back() == "depart");
        commonObject.time += 550; s.UpdateRetainedHome(); s.UpdateRetainedPage();
        CHECK(s.guiActive == page && s.retainedPageHome == pause && s.RetainedPageActive());
        s.HandleRetainedSessionRequest(page, "restartBack");
        CHECK(s.guiActive == s.guiMainMenu && s.guiRetainedHome == pause && pause->named.back() == "returnHome" && s.restarts.empty());
        cvarSystemObject.strings["g_skill"] = "9";
        s.HandleRetainedSessionRequest(pause, "restartLevel"); commonObject.time += 550; s.UpdateRetainedHome(); s.UpdateRetainedPage();
        CHECK(s.guiActive == page && page->state["restart.skill"] == "4");  // clamped to the difficulties
        page->state["restart.skill"] = "0"; s.HandleRetainedSessionRequest(page, "restartGo");
        CHECK((s.restarts == std::vector<int>{0}) && s.played.back() == "play main_menu_selection");
        page->state["restart.skill"] = "7"; s.HandleRetainedSessionRequest(page, "restartGo"); CHECK(s.restarts.back() == 4);
        s.HandleRetainedSessionRequest(page, "unknown");
        CHECK(commonObject.warnings.size() == 1 && commonObject.warnings[0].find("Restart Level") != std::string::npos);
    }
    {   // The Demos sub-page (FLOW-013, FLOW-050): the Multiplayer page's DEMOS
        // goes deeper to it as CREATE SERVER does. The library newest first
        // under its filter, the newest selected; the verbs select, page, play,
        // delete, refresh and filter; Back climbs out onto DEMOS.
        auto s = Session(true); s.PreloadRetainedScreens(); s.UpdateRetainedHome();
        auto* title = s.guiRetainedHome; auto* page = s.guiRetainedDemos; auto* mp = s.guiRetainedMpPage;
        CHECK(title && mp && page && std::string(page->Name()) == "guis/menu/demos.q4ui" && s.RetainedDemosPage() == page);
        auto demo = [](const char* name, demoLibraryType_t type, bool playable, const char* map = "") {
            demoLibraryEntry_t e; e.path = std::string("demos/") + name; e.displayName = name; e.type = type; e.playable = playable;
            e.capabilities = (playable ? DEMO_CAP_PLAY : 0) | DEMO_CAP_DELETE; e.mapName = map; e.status = playable ? "Playable" : "Unsupported";
            return e;
        };
        s.demoFiles = {demo("newest", DEMO_LIBRARY_RENDER, true), demo("middle", DEMO_LIBRARY_MVD, false, "mp/q4dm1"),
                       demo("oldest", DEMO_LIBRARY_RENDER, true)};
        // The title offers no DEMOS of its own: it is a Multiplayer section.
        s.HandleRetainedSessionRequest(title, "demos");
        CHECK(legacyActions.empty() && s.retainedPagePending == nullptr && !s.BeginRetainedPage("demos") &&
              commonObject.warnings.back().find("unhandled session request 'demos'") != std::string::npos);
        s.HandleRetainedSessionRequest(title, "multiplayer"); commonObject.time += 550; s.UpdateRetainedHome(); s.UpdateRetainedPage();
        CHECK(s.guiActive == mp);
        s.HandleRetainedSessionRequest(mp, "multiplayerDemos");
        CHECK(legacyActions.empty() && s.retainedSubpageFrom == mp && mp->named.back() == "subpageEnterDemos" && s.retainedSubpageDeeper &&
              s.retainedSubpageKind == RETAINED_SUBPAGE_DEMOS && s.retainedSubpageUntil == commonObject.time + 350 && s.demoMenus.empty());
        // The library is published while the page leaves.
        CHECK(page->state["demos.count"] == "3" && page->state["demos.selected"] == "0" && page->state["demos.filter"] == "0" &&
              page->state["demos.row0.name"] == "newest" && page->state["demos.row2.name"] == "oldest" &&
              page->state["demos.row3.name"].empty() && page->state["demos.canPlay"] == "1" && page->state["demos.kind"] == "Render demo");
        s.HandleRetainedSessionRequest(mp, "multiplayerCreate"); CHECK(s.retainedSubpageKind == RETAINED_SUBPAGE_DEMOS);  // nothing else meanwhile
        commonObject.time += 350; s.UpdateRetainedSubpage();
        CHECK(s.guiActive == page && s.retainedSubpageFrom == nullptr && !s.RetainedPageActive());
        // A row selects its demo, which brings its map's picture and status.
        page->state["demos.pick"] = "1"; s.HandleRetainedSessionRequest(page, "demosSelect");
        CHECK(page->state["demos.selected"] == "1" && page->state["demos.canPlay"] == "0" && page->state["demos.playable"] == "0" &&
              page->state["demos.status"] == "Unsupported" && page->state["demos.shot"] == s.RetainedPauseShot("mp/q4dm1"));
        page->state["demos.pick"] = "7"; s.HandleRetainedSessionRequest(page, "demosSelect"); CHECK(page->state["demos.selected"] == "1");
        // PLAY needs a demo that can play.
        s.HandleRetainedSessionRequest(page, "demosPlay"); CHECK(s.playedDemos.empty());
        // A filter: MULTI-VIEW keeps the selected demo; RENDER drops it and selects the first.
        page->state["demos.filter"] = "1"; s.HandleRetainedSessionRequest(page, "demosFilter");
        CHECK(page->state["demos.count"] == "1" && page->state["demos.row0.name"] == "middle" && page->state["demos.selected"] == "0");
        page->state["demos.filter"] = "2"; s.HandleRetainedSessionRequest(page, "demosFilter");
        CHECK(page->state["demos.count"] == "2" && page->state["demos.row1.name"] == "oldest" && page->state["demos.selected"] == "0" &&
              page->state["demos.filter"] == "2");
        page->state["demos.filter"] = "9"; s.HandleRetainedSessionRequest(page, "demosFilter");
        CHECK(page->state["demos.filter"] == "4" && page->state["demos.count"] == "0" && page->state["demos.selected"] == "-1");
        page->state["demos.filter"] = "0"; s.HandleRetainedSessionRequest(page, "demosFilter");
        CHECK(page->state["demos.count"] == "3" && page->state["demos.filter"] == "0");
        // REFRESH rescans and keeps the selected demo, wherever it now stands.
        page->state["demos.pick"] = "2"; s.HandleRetainedSessionRequest(page, "demosSelect");
        s.demoFiles.insert(s.demoFiles.begin(), demo("fresh", DEMO_LIBRARY_RENDER, true));
        s.HandleRetainedSessionRequest(page, "demosRefresh");
        CHECK(page->state["demos.count"] == "4" && page->state["demos.selected"] == "3" && page->state["demos.row3.name"] == "oldest" &&
              s.played.back() == "play main_menu_selection");
        // DELETE (the page asked first): the next demo takes the deleted one's
        // place; one that could not be deleted stays selected and says so.
        page->state["demos.pick"] = "1"; s.HandleRetainedSessionRequest(page, "demosSelect");
        s.HandleRetainedSessionRequest(page, "demosDelete");
        CHECK((s.deletedDemos == std::vector<std::string>{"demos/newest"}) && page->state["demos.count"] == "3" &&
              page->state["demos.selected"] == "1" && page->state["demos.row1.name"] == "middle" && page->state["demos.error"].empty());
        s.deleteRefuses = true; s.HandleRetainedSessionRequest(page, "demosDelete");
        CHECK(page->state["demos.count"] == "3" && page->state["demos.selected"] == "1" && page->state["demos.error"] == "#str_231119");
        s.deleteRefuses = false;
        // PLAY plays the selected demo, and playback takes the screen; a demo
        // that cannot start leaves the page here, saying so.
        page->state["demos.pick"] = "0"; s.HandleRetainedSessionRequest(page, "demosSelect");
        s.playRefuses = true; s.HandleRetainedSessionRequest(page, "demosPlay");
        CHECK((s.playedDemos == std::vector<std::string>{"demos/fresh"}) && s.guiActive == page && page->state["demos.error"] == "#str_231120");
        s.playRefuses = false; s.HandleRetainedSessionRequest(page, "demosPlay");
        CHECK(s.playedDemos.size() == 2 && s.guiActive == nullptr);
        s.guiActive = page; s.HandleRetainedSessionRequest(page, "unknown");
        CHECK(!commonObject.warnings.empty() && commonObject.warnings.back().find("Demos") != std::string::npos);
        // Back climbs out: at 300 ms the Multiplayer page returns onto DEMOS.
        s.HandleRetainedSessionRequest(page, "demosBack");
        CHECK(s.retainedSubpageFrom == page && page->named.back() == "subpageLeave" && !s.retainedSubpageDeeper &&
              s.retainedSubpageKind == RETAINED_SUBPAGE_DEMOS && s.retainedSubpageUntil == commonObject.time + 300);
        commonObject.time += 300; s.UpdateRetainedSubpage();
        CHECK(s.guiActive == mp && mp->state["from_demos"] == "1" && mp->state["from_join"] == "0" && mp->state["from_create"] == "0" &&
              mp->named.back() == "subpageReturn");
        // Pages of 64: more demos page through, selecting the first of the older
        // page or the last of the newer one.
        s.demoFiles.clear();
        for (int i = 0; i < 70; ++i) s.demoFiles.push_back(demo(("d" + std::to_string(i)).c_str(), DEMO_LIBRARY_RENDER, true));
        s.HandleRetainedSessionRequest(mp, "multiplayerDemos"); commonObject.time += 350; s.UpdateRetainedSubpage();
        CHECK(s.guiActive == page && page->state["demos.count"] == "70" && page->state["demos.first"] == "0");
        s.HandleRetainedSessionRequest(page, "demosOlder");
        CHECK(page->state["demos.first"] == "64" && page->state["demos.selected"] == "64" && page->state["demos.row5.name"] == "d69" &&
              page->named.back() == "pagedOlder");
        s.HandleRetainedSessionRequest(page, "demosOlder"); CHECK(page->state["demos.first"] == "64");
        s.HandleRetainedSessionRequest(page, "demosNewer");
        CHECK(page->state["demos.first"] == "0" && page->state["demos.selected"] == "63" && page->named.back() == "pagedNewer");
    }
    {   // DEMOS opens the legacy demo browser over the Multiplayer page, which
        // it returns to, during playback, in game and when the sub-page is not
        // installed.
        auto playing = Session(true); playing.PreloadRetainedScreens(); playing.UpdateRetainedHome();
        playing.HandleRetainedSessionRequest(playing.guiRetainedHome, "multiplayer"); commonObject.time += 550;
        playing.UpdateRetainedHome(); playing.UpdateRetainedPage(); CHECK(playing.guiActive == playing.guiRetainedMpPage);
        playing.demoPlaying = true; CHECK(playing.RetainedDemosPage() == nullptr);
        playing.HandleRetainedSessionRequest(playing.guiRetainedMpPage, "multiplayerDemos");
        CHECK(playing.retainedSubpageFrom == nullptr && (playing.demoMenus == std::vector<bool>{true}) &&
              playing.played.back() == "play main_menu_selection");
        playing.demoPlaying = false; playing.mapSpawned = true; CHECK(playing.RetainedDemosPage() == nullptr); playing.mapSpawned = false;
        auto missing = Session(true); fileSystemObject.files.erase("guis/menu/demos.q4ui");
        missing.PreloadRetainedScreens(); missing.UpdateRetainedHome();
        missing.HandleRetainedSessionRequest(missing.guiRetainedHome, "multiplayer"); commonObject.time += 550;
        missing.UpdateRetainedHome(); missing.UpdateRetainedPage(); CHECK(missing.guiActive == missing.guiRetainedMpPage);
        missing.HandleRetainedSessionRequest(missing.guiRetainedMpPage, "multiplayerDemos");
        CHECK(missing.guiRetainedDemos == nullptr && missing.retainedSubpageFrom == nullptr && (missing.demoMenus == std::vector<bool>{true}) &&
              (Stock(missing) == std::vector<std::string>{"guis/menu/demos.q4ui"}));
        // Where it stops presenting between the choice and the hand-over, the
        // legacy browser opens over the Multiplayer page in its place.
        missing.guiActive = missing.guiRetainedMpPage; missing.OpenRetainedDemos();
        CHECK((missing.demoMenus == std::vector<bool>{true, true}));
    }
    {   // MODS opens the title's own dialog with the installed mods, the current
        // game's selected; a row selects its mod and LOAD MOD restarts into it.
        auto s = Session(true); s.PreloadRetainedScreens(); s.UpdateRetainedHome(); auto* title = s.guiRetainedHome;
        auto mod = [](const char* dir, const char* name, const char* version) {
            idModInfo m; m.directory = dir; m.displayName = name; m.version = version; m.author = "Raven"; m.releaseDate = "2005";
            m.requiredopenQ4Version = "0.11"; return m;
        };
        fileSystemObject.modList.mods = {mod("baseoq4", "openQ4", "0.13"), mod("q4xbase", "", "1.0"), mod("dmplus", "DM Plus", "2.1")};
        cvarSystemObject.strings["fs_game"] = "q4xbase";
        s.HandleRetainedSessionRequest(title, "mods");
        CHECK(legacyActions.empty() && title->named.back() == "modsModalShow" && s.played.back() == "play main_menu_selection");
        CHECK(title->state["mods.count"] == "3" && title->state["mods.selected"] == "1" && title->state["mods.row1.name"] == "q4xbase" &&
              title->state["mods.row2.version"] == "2.1" && title->state["mods.row3.name"].empty() &&
              title->state["mods.directory"] == "q4xbase" && title->state["mods.author"] == "Raven" && fileSystemObject.modFrees == 1);
        title->state["mods.pick"] = "2"; s.HandleRetainedSessionRequest(title, "modsSelect");
        CHECK(title->state["mods.selected"] == "2" && title->state["mods.directory"] == "dmplus");
        title->state["mods.pick"] = "9"; s.HandleRetainedSessionRequest(title, "modsSelect"); CHECK(title->state["mods.selected"] == "2");
        s.HandleRetainedSessionRequest(title, "modsLoad");
        CHECK(cvarSystemObject.strings["fs_game"] == "dmplus" && commands.buffered.back() == "reloadEngine menu\n");
        // Without mods there is nothing to select or load.
        fileSystemObject.modList.mods.clear(); s.HandleRetainedSessionRequest(title, "mods");
        CHECK(title->state["mods.count"] == "0" && title->state["mods.selected"] == "-1" && title->state["mods.directory"].empty());
        const size_t buffered = commands.buffered.size(); s.HandleRetainedSessionRequest(title, "modsLoad");
        CHECK(commands.buffered.size() == buffered);
    }
    {   // After the player dies, the retained restart screen (FLOW-005) presents;
        // its plates run the stock screen's own commands.
        auto s = Session(true, true); s.PreloadRetainedScreens(); s.PrepareRetainedLevel("game/airdefense1", false);
        auto* page = s.guiRetainedRestartMenu;
        CHECK(page && std::string(page->Name()) == "guis/menu/restartmenu.q4ui");
        idUserInterface stock("guis/restart.gui"); s.guiRestartMenu = &stock;
        s.OpenRestartMenu(); CHECK(s.guiActive == page);
        s.HandleRetainedSessionRequest(page, "deathRestart");
        CHECK(s.restartCommands.back() == "restart" && s.played.back() == "play main_menu_selection");
        // LOAD GAME leaves for the title, which opens its retained Load Game
        // page as its own LOAD GAME does, never the legacy menu's stock page.
        s.HandleRetainedSessionRequest(page, "deathLoad");
        CHECK(s.restartCommands.back() == "disconnect mainmenu" && s.guiRetainedHome == s.guiRetainedTitle);
        CHECK(s.retainedPagePending == s.guiRetainedLoad && s.guiRetainedLoad != nullptr && s.RetainedHomeInputBlocked());
        CHECK(std::find(s.guiMainMenu->named.begin(), s.guiMainMenu->named.end(), "fromRestart_toLoad") == s.guiMainMenu->named.end());
        s.mapSpawned = true; s.OpenRestartMenu(); CHECK(s.guiActive == page);
        s.HandleRetainedSessionRequest(page, "deathMenu"); CHECK(s.restartCommands.back() == "disconnect mainmenu");
        s.mapSpawned = true; s.OpenRestartMenu();
        s.HandleRetainedSessionRequest(page, "quit"); CHECK(commonObject.quits == 1 && s.restartCommands.size() == 3);
        {   // A Load Game page that cannot present: the stock page, as before.
            auto stockLoad = Session(true, true); stockLoad.PreloadRetainedScreens(); stockLoad.guiRestartMenu = &stock;
            fileSystemObject.files.erase("guis/menu/loadgame.q4ui"); stockLoad.retainedStock.Append(idStr("guis/menu/loadgame.q4ui"));
            stockLoad.guiRetainedLoad = nullptr; stockLoad.OpenRestartMenu();
            stockLoad.HandleRetainedSessionRequest(stockLoad.guiActive, "deathLoad");
            CHECK(stockLoad.retainedPagePending == nullptr && stockLoad.guiMainMenu->named.back() == "fromRestart_toLoad");
            fileSystemObject.files.insert("guis/menu/loadgame.q4ui");
        }
        // The gate off, the screen not installed, or a view that stopped drawing:
        // the stock screen, the last for good.
        auto off = Session(false, true); off.guiRestartMenu = &stock; off.OpenRestartMenu(); CHECK(off.guiActive == &stock);
        auto missing = Session(true, true); fileSystemObject.files.erase("guis/menu/restartmenu.q4ui"); missing.guiRestartMenu = &stock;
        missing.PreloadRetainedScreens(); missing.OpenRestartMenu();
        CHECK(missing.guiActive == &stock && missing.guiRetainedRestartMenu == nullptr);
        auto failed = Session(true, true); failed.guiRestartMenu = &stock; failed.PreloadRetainedScreens();
        failed.PrepareRetainedLevel("game/airdefense1", false);
        failed.guiRetainedRestartMenu->failed = true; failed.OpenRestartMenu();
        CHECK(failed.guiActive == &stock && failed.guiRetainedRestartMenu == nullptr &&
              commonObject.warnings.back().find("stopped drawing") != std::string::npos);
        failed.OpenRestartMenu(); CHECK(failed.guiActive == &stock && failed.guiRetainedRestartMenu == nullptr);
    }
    {   // The Credits page: CREDITS hands off to it from the title, every line it
        // declares read from the legacy credits window of the same name; Back
        // returns the title.
        auto s = Session(true); s.PreloadRetainedScreens(); s.UpdateRetainedHome();
        auto* title = s.guiRetainedHome; auto* page = s.guiRetainedCredits;
        CHECK(page && std::string(page->Name()) == "guis/menu/credits.q4ui");
        declaredStates["guis/menu/credits.q4ui"] = {"credits.screen1_t_1", "credits.screen1_n_1", "credits.screen9_n_56a", "menu.other"};
        s.guiMainMenu->presentation["screen1_t_1::text"] = "Project Lead"; s.guiMainMenu->presentation["screen1_n_1::text"] = "A Name";
        s.HandleRetainedSessionRequest(title, "credits");
        CHECK(legacyActions.empty() && s.retainedPagePending == page && title->named.back() == "depart");
        CHECK(page->state["credits.screen1_t_1"] == "Project Lead" && page->state["credits.screen1_n_1"] == "A Name" &&
              page->state.count("credits.screen9_n_56a") == 1 && page->state["credits.screen9_n_56a"].empty() &&
              page->state.count("menu.other") == 0);
        commonObject.time += 550; s.UpdateRetainedHome(); s.UpdateRetainedPage();
        CHECK(s.guiActive == page && s.RetainedPageActive());
        s.HandleRetainedSessionRequest(page, "unknown"); CHECK(s.guiActive == page);
        s.HandleRetainedSessionRequest(page, "creditsBack");
        CHECK(s.guiActive == s.guiMainMenu && title->named.back() == "returnHome");
        // A page that cannot present leaves the stock page.
        auto missing = Session(true); fileSystemObject.files.erase("guis/menu/credits.q4ui");
        missing.PreloadRetainedScreens(); missing.UpdateRetainedHome();
        missing.HandleRetainedSessionRequest(missing.guiRetainedHome, "credits");
        CHECK(legacyActions.back() == "main_b_credits" && (Stock(missing) == std::vector<std::string>{"guis/menu/credits.q4ui"}));
        // In game there is no Credits page.
        auto ingame = Session(true, true); ingame.PreloadRetainedScreens(); ingame.UpdateRetainedHome();
        CHECK(!ingame.BeginRetainedPage("credits"));
    }
    {   // The Multiplayer page (FLOW-050): MULTIPLAYER hands off to it from the
        // title. JOIN GAME, CREATE SERVER and PLAYER SETUP send it away and at
        // 500 ms the legacy menu takes the screen in its Multiplayer page state
        // (desktop::curr 3, which keeps its onActivate from resetting it), the
        // destination set and that stock page arriving (openMultiplayerPage).
        // A stock page's Back returns the page (multiplayerOpen); BACK returns
        // the title, from the page dock after a stock page.
        auto s = Session(true); s.PreloadRetainedScreens(); s.UpdateRetainedHome();
        auto* title = s.guiRetainedHome; auto* page = s.guiRetainedMpPage;
        CHECK(page && std::string(page->Name()) == "guis/menu/multiplayer.q4ui" && s.RetainedMultiplayerPage() == page);
        page->state["from_create"] = page->state["from_join"] = page->state["from_demos"] = page->state["from_setup"] = "1";
        s.HandleRetainedSessionRequest(title, "multiplayer");
        CHECK(legacyActions.empty() && s.retainedPagePending == page && title->named.back() == "depart" &&
              title->named[title->named.size() - 2] == "carry_multiplayer");
        // From the title the page starts on its primary action: no section to return onto.
        CHECK(page->state["from_create"] == "0" && page->state["from_join"] == "0" && page->state["from_demos"] == "0" &&
              page->state["from_setup"] == "0");
        commonObject.time += 550; s.UpdateRetainedHome(); s.UpdateRetainedPage();
        CHECK(s.guiActive == page && s.RetainedPageActive() && s.retainedPageHome == title);
        s.HandleRetainedSessionRequest(page, "unknown"); CHECK(s.guiActive == page && s.retainedMpPageDest == 0);
        // BACK before any stock page: the title returns as from any page.
        s.HandleRetainedSessionRequest(page, "multiplayerBack");
        CHECK(s.guiActive == s.guiMainMenu && title->named.back() == "returnHome" && s.guiRetainedHome == title);
        // Available player setup stays on the retained page; its typed request
        // is dispatched without interpreting the payload as a session verb.
        s.HandleRetainedSessionRequest(title, "multiplayer"); commonObject.time += 550; s.UpdateRetainedHome(); s.UpdateRetainedPage();
        s.playerSetupAvailable = true;
        s.HandleRetainedSessionRequest(page, "multiplayerSetup");
        CHECK(s.playerSetupOpens == 1 && page->State().GetBool("setup.open") && s.guiActive == page && s.retainedMpPageDest == 0);
        s.UpdateRetainedMultiplayerPage();
        CHECK(s.playerSetupPublishes == 1 && s.guiActive == page && s.retainedMpPageDest == 0);
        s.HandleRetainedMultiplayerPageRequest(page, "playerSetupApply {\"name\":\"multiplayerBack\"}");
        CHECK((s.playerSetupRequests == std::vector<std::string>{"{\"name\":\"multiplayerBack\"}"}) && s.guiActive == page);
        s.HandleRetainedSessionRequest(page, "playerSetupClose");
        CHECK(page->named.back() == "setupClose" && s.retainedMpPageDest == 0);
        page->SetStateBool("setup.open", false); s.playerSetupAvailable = false;
        s.HandleRetainedSessionRequest(page, "multiplayerBack");
        // Again, with PLAYER SETUP unavailable: the page leaves for the stock
        // fallback, and takes no other choice
        // meanwhile, the sub-pages' changes of level among them.
        s.HandleRetainedSessionRequest(title, "multiplayer"); commonObject.time += 550; s.UpdateRetainedHome(); s.UpdateRetainedPage();
        CHECK(s.guiActive == page);
        const size_t sounds = s.played.size();
        s.HandleRetainedSessionRequest(page, "multiplayerSetup");
        CHECK(page->named.back() == "depart" && s.retainedMpPageDest == 18 && s.played.size() == sounds + 1);
        s.HandleRetainedSessionRequest(page, "multiplayerCreate"); s.HandleRetainedSessionRequest(page, "multiplayerJoin");
        s.HandleRetainedSessionRequest(page, "multiplayerBack");
        CHECK(s.retainedMpPageDest == 18 && s.played.size() == sounds + 1 && s.guiActive == page && s.retainedSubpageFrom == nullptr);
        commonObject.time += 499; s.UpdateRetainedMultiplayerPage(); CHECK(s.guiActive == page && timeRuns.empty());
        commonObject.time += 1; s.UpdateRetainedMultiplayerPage();
        CHECK(s.guiActive == s.guiMainMenu && s.retainedMpPageDest == 0 && s.retainedPageHome == nullptr && s.retainedMpPageLeft == 18);
        CHECK(legacy["desktop::curr"] == 3 && legacy["desktop::active"] == 1 && legacy["desktop::dest"] == 18 && expressionWrites == 0 &&
              s.guiMainMenu->named.back() == "openMultiplayerPage" &&
              (timeRuns == std::vector<std::string>{"guis/mainmenu.gui"}));  // its timelines caught up first
        s.UpdateRetainedHome(); CHECK(s.guiRetainedHome == nullptr);  // the stock page presents alone
        // Its Back: the stock page leaves for the Multiplayer page and the retained one presents at once.
        legacy["desktop::curr"] = 18; legacy["desktop::dest"] = 3;
        CHECK(s.OpenRetainedMultiplayerPage() && s.guiActive == page && s.retainedPageHome == nullptr && s.retainedMpPageDest == 0);
        // Onto the section whose stock page it left (multiplayerOpen names it).
        CHECK(s.OpenRetainedMultiplayerPage("setup") && page->state["from_setup"] == "1" && page->state["from_join"] == "0" &&
              page->state["from_create"] == "0");
        CHECK(s.OpenRetainedMultiplayerPage("join") && page->state["from_setup"] == "0" && page->state["from_join"] == "1");
        // PLAYER SETUP chooses the Settings page again.
        for (const auto& [verb, dest] : {std::pair<const char*, int>{"multiplayerSetup", 18}}) {
            s.HandleRetainedSessionRequest(page, verb); CHECK(s.retainedMpPageDest == dest);
            commonObject.time += 500; s.UpdateRetainedMultiplayerPage();
            CHECK(s.guiActive == s.guiMainMenu && legacy["desktop::dest"] == dest && s.guiMainMenu->named.back() == "openMultiplayerPage");
            legacy["desktop::curr"] = dest; legacy["desktop::dest"] = 3; CHECK(s.OpenRetainedMultiplayerPage() && s.guiActive == page);
        }
        // BACK after a stock page: the legacy menu goes home as its own
        // Multiplayer page does (leaveMultiplayerPage clears desktop::dest)
        // and the title follows it from the page dock.
        s.guiMainMenu->onNamed = [](const char* name) { if (!std::strcmp(name, "leaveMultiplayerPage")) legacy["desktop::dest"] = 0; };
        s.HandleRetainedSessionRequest(page, "multiplayerBack");
        CHECK(s.guiActive == s.guiMainMenu && s.guiMainMenu->named.back() == "leaveMultiplayerPage" && legacy["desktop::curr"] == 3 &&
              legacy["desktop::active"] == 1 && s.guiRetainedHome == title && title->named.back() == "returnHome" && expressionWrites == 0);
        // Anything else taking the screen while the page leaves abandons it.
        s.HandleRetainedSessionRequest(title, "multiplayer"); commonObject.time += 550; s.UpdateRetainedHome(); s.UpdateRetainedPage();
        s.HandleRetainedSessionRequest(page, "multiplayerSetup"); idUserInterface other("guis/other.gui"); s.guiActive = &other;
        const size_t events = s.guiMainMenu->named.size();
        commonObject.time += 500; s.UpdateRetainedMultiplayerPage();
        CHECK(s.guiActive == &other && s.retainedMpPageDest == 0 && s.guiMainMenu->named.size() == events);
        // Where the page cannot present, multiplayerOpen opens the stock one:
        // the gate off, in a game, not installed (quietly), or stopped drawing (reported once).
        auto off = Session(false); CHECK(!off.OpenRetainedMultiplayerPage() && off.RetainedMultiplayerPage() == nullptr && managerObject.loads.empty());
        auto ingame = Session(true, true); CHECK(!ingame.OpenRetainedMultiplayerPage() && managerObject.loads.empty());
        CHECK(!ingame.BeginRetainedPage("multiplayer"));
        auto missing = Session(true); fileSystemObject.files.erase("guis/menu/multiplayer.q4ui");
        CHECK(!missing.OpenRetainedMultiplayerPage() && commonObject.warnings.empty() &&
              (Stock(missing) == std::vector<std::string>{"guis/menu/multiplayer.q4ui"}));
        missing.PreloadRetainedScreens(); missing.UpdateRetainedHome();
        missing.HandleRetainedSessionRequest(missing.guiRetainedHome, "multiplayer");
        CHECK(legacyActions.back() == "main_b_multiplayer" && missing.retainedPagePending == nullptr);
        auto failed = Session(true); failed.PreloadRetainedScreens(); failed.guiRetainedMpPage->failed = true;
        CHECK(!failed.OpenRetainedMultiplayerPage() && failed.guiRetainedMpPage == nullptr && commonObject.warnings.size() == 1 &&
              (Stock(failed) == std::vector<std::string>{"guis/menu/multiplayer.q4ui"}));
        CHECK(!failed.OpenRetainedMultiplayerPage() && commonObject.warnings.size() == 1);
        // A main menu without each route to its Multiplayer pages (a mod's copy
        // of an older one) keeps its own Multiplayer page, quietly: the title
        // still presents, MULTIPLAYER runs the stock button and the page never loads.
        for (const char* route : {"openMultiplayerPage", "leaveMultiplayerPage", "openMultiplayerStock"}) {
            auto old = Session(true); legacyEventsMissing = {route};
            old.PreloadRetainedScreens(); old.UpdateRetainedHome(); auto* home = old.guiRetainedHome;
            CHECK(home == old.guiRetainedTitle && old.guiRetainedMpPage == nullptr && old.RetainedMultiplayerPage() == nullptr);
            old.HandleRetainedSessionRequest(home, "multiplayer");
            CHECK(legacyActions.back() == "main_b_multiplayer" && old.retainedPagePending == nullptr && !old.OpenRetainedMultiplayerPage());
            CHECK(commonObject.warnings.empty() && commonObject.developer.size() == 1 &&
                  commonObject.developer[0].find("no routes to its Multiplayer pages") != std::string::npos &&
                  (Stock(old) == std::vector<std::string>{"guis/menu/multiplayer.q4ui"}));
            CHECK(std::find(managerObject.loads.begin(), managerObject.loads.end(), "guis/menu/multiplayer.q4ui") == managerObject.loads.end());
        }
    }
    {   // A preview that stops publishing hands the active setup edit to stock.
        auto s = Session(true); s.PreloadRetainedScreens(); s.UpdateRetainedHome();
        CHECK(s.OpenRetainedMultiplayerPage());
        auto* page = s.guiRetainedMpPage; s.playerSetupAvailable = true;
        s.HandleRetainedSessionRequest(page, "multiplayerSetup");
        s.playerSetupPublishOk = false; s.UpdateRetainedMultiplayerPage();
        CHECK(s.playerSetupPublishes == 1 && s.guiActive == s.guiMainMenu && legacy["desktop::dest"] == 18 &&
              s.guiMainMenu->named.back() == "openMultiplayerPage");
    }
    {   // The Create Server sub-page (FLOW-050) offers all the stock page does
        // (RETAINED_CREATE_SERVER_MISSING is empty), so it is the default:
        // without ui_retainedMultiplayer it preloads and CREATE SERVER goes deeper.
        auto s = Session(true); s.PreloadRetainedScreens(); s.UpdateRetainedHome();
        CHECK(s.guiRetainedCreate != nullptr && s.RetainedCreateServerPage() == s.guiRetainedCreate);
        s.HandleRetainedSessionRequest(s.guiRetainedHome, "multiplayer"); commonObject.time += 550; s.UpdateRetainedHome(); s.UpdateRetainedPage();
        auto* page = s.guiRetainedMpPage; CHECK(s.guiActive == page);
        s.HandleRetainedSessionRequest(page, "multiplayerCreate");
        CHECK(s.retainedMpPageDest == 0 && page->named.back() == "subpageEnterCreate" && s.retainedSubpageFrom == page);
    }
    {   // CREATE SERVER goes deeper as Campaign does, and at 350 ms the
        // sub-page presents with the settings, the game type's maps (si_map's
        // own chosen) and its levelshot published.
        auto s = Session(true);
        fileSystemObject.maps = {idDict{{{"path", "mp/q4dm1"}, {"name", "#str_q4dm1"}, {"DM", "1"}, {"Team DM", "1"}}},
                                 idDict{{{"path", "mp/q4ctf1"}, {"name", "^c560[^c870CMP^c560]^c998 Lost ^1Fleet"}, {"CTF", "1"}}},
                                 idDict{{{"path", "mp/q4dm5"}, {"DM", "1"}}}};
        fileSystemObject.screenshots["mp/q4dm5"] = "gfx/guis/loadscreens/q4dm5";
        fileSystemObject.files.insert("gfx/guis/loadscreens/q4dm5.tga");
        cvarSystemObject.strings = {{"si_gameType", "DM"}, {"si_map", "mp/q4dm5"}, {"si_name", "openQ4 Server"}, {"si_fragLimit", "20"},
                                    {"si_maxPlayers", "8"}, {"si_minPlayers", "2"}, {"si_teamDamage", "1"}, {"g_mapCycle", "mapcycle"},
                                    {"bot_skill", "9"}, {"g_password", "hunter2"}};
        s.PreloadRetainedScreens(); s.UpdateRetainedHome();
        auto* create = s.guiRetainedCreate;
        CHECK(create && std::string(create->Name()) == "guis/menu/createserver.q4ui" && s.RetainedCreateServerPage() == create);
        // Each map's levelshot waits for the title's idle frames, or for the page.
        CHECK(s.retainedLevelshots.Num() == 3 && std::count(precached.begin(), precached.end(), std::string("gfx/guis/loadscreens/q4dm5")) == 0);
        const int opening = latePrecaches;
        s.HandleRetainedSessionRequest(s.guiRetainedHome, "multiplayer"); commonObject.time += 550; s.UpdateRetainedHome(); s.UpdateRetainedPage();
        auto* page = s.guiRetainedMpPage; CHECK(s.guiActive == page);
        // CREATE SERVER's summary (section 13.7) shows si_map's levelshot,
        // read as the page opens and so no longer waiting for the idle frames.
        CHECK(page->state["summary.create.shot"] == "gfx/guis/loadscreens/q4dm5");
        CHECK(s.retainedLevelshots.Num() == 2 && latePrecaches == opening);
        CHECK(std::count(precached.begin(), precached.end(), std::string("gfx/guis/loadscreens/q4dm5")) == 1);
        s.HandleRetainedSessionRequest(page, "multiplayerCreate");
        CHECK(s.retainedSubpageFrom == page && page->named.back() == "subpageEnterCreate" && s.retainedSubpageDeeper &&
              s.retainedSubpageKind == RETAINED_SUBPAGE_CREATE_SERVER && s.retainedSubpageUntil == commonObject.time + 350 &&
              s.retainedMpPageDest == 0);
        s.HandleRetainedSessionRequest(page, "multiplayerJoin"); CHECK(s.retainedMpPageDest == 0);  // nothing else meanwhile
        const int late = latePrecaches; commonObject.time += 350; s.UpdateRetainedSubpage();
        CHECK(s.guiActive == create && s.retainedSubpageFrom == nullptr);
        // The page read every levelshot still waiting, each once, as precaches.
        CHECK(s.retainedLevelshots.Num() == 0 && latePrecaches == late);
        CHECK(std::count(precached.begin(), precached.end(), std::string("gfx/guis/loadscreens/q4dm5")) == 1);
        CHECK(create->state["create.gametype"] == "0" && create->state["create.fraglimit"] == "20" && create->state["create.maxplayers"] == "8" &&
              create->state["create.teamdamage"] == "1" && create->state["create.botskill"] == "5" && create->state["create.name"] == "openQ4 Server");
        CHECK(create->state["create.password"] == "hunter2" && create->state["create.rconpassword"] == "" && create->state["create.mapcycle"] == "mapcycle");
        CHECK(create->state["create.map_count"] == "2" && create->state["create.map0"] == "#str_q4dm1" && create->state["create.map1"] == "mp/q4dm5" &&
              create->state["create.map2"] == "" && create->state["create.map_selected"] == "1" &&
              create->state["create.shot"] == "gfx/guis/loadscreens/q4dm5" && cvarSystemObject.strings["si_map"] == "mp/q4dm5");
        // Settings: in range a verb writes its CVar and the page follows; out of
        // range it is refused and said; the player counts keep their order.
        s.HandleRetainedSessionRequest(create, "createFragLimit 35"); CHECK(cvarSystemObject.strings["si_fragLimit"] == "35" && create->state["create.fraglimit"] == "35");
        s.HandleRetainedSessionRequest(create, "createTimeLimit 61");
        CHECK(cvarSystemObject.strings.count("si_timeLimit") == 0 && commonObject.warnings.back().find("si_timeLimit 61") != std::string::npos);
        s.HandleRetainedSessionRequest(create, "createDedicated 1"); CHECK(cvarSystemObject.strings["net_serverMenuDedicated"] == "1" &&
                                                                        create->state["create.dedicated"] == "1");
        s.HandleRetainedSessionRequest(create, "createMaxPlayers 1"); CHECK(cvarSystemObject.strings["si_maxPlayers"] == "1" &&
                                                                         cvarSystemObject.strings["si_minPlayers"] == "1");
        s.HandleRetainedSessionRequest(create, "createMinPlayers 6"); CHECK(cvarSystemObject.strings["si_minPlayers"] == "6" &&
                                                                         cvarSystemObject.strings["si_maxPlayers"] == "6");
        s.HandleRetainedSessionRequest(create, "createBots 999"); CHECK(cvarSystemObject.strings.count("bot_minPlayers") == 0);
        // The game type lists its maps again: CTF plays only q4ctf1, its name
        // without the color codes the stock list drew.
        s.HandleRetainedSessionRequest(create, "createGameType 3");
        CHECK(cvarSystemObject.strings["si_gameType"] == "CTF" && create->state["create.gametype"] == "3" && create->state["create.map_count"] == "1" &&
              create->state["create.map0"] == "[CMP] Lost Fleet" &&
              create->state["create.map_selected"] == "0" && cvarSystemObject.strings["si_map"] == "mp/q4ctf1" &&
              create->state["create.shot"] == "gfx/guis/loadscreens/generic");
        s.HandleRetainedSessionRequest(create, "createGameType 12"); CHECK(cvarSystemObject.strings["si_gameType"] == "CTF");
        // A game type no map declares lists every multiplayer map.
        s.HandleRetainedSessionRequest(create, "createGameType 5");
        CHECK(cvarSystemObject.strings["si_gameType"] == "DeadZone" && create->state["create.map_count"] == "3");
        // A map row chooses its map; a row past the list does nothing.
        create->state["create.pick"] = "2"; s.HandleRetainedSessionRequest(create, "createMap");
        CHECK(create->state["create.map_selected"] == "2" && cvarSystemObject.strings["si_map"] == "mp/q4dm5" && s.played.back() == "play main_menu_selection");
        create->state["create.pick"] = "7"; s.HandleRetainedSessionRequest(create, "createMap"); CHECK(create->state["create.map_selected"] == "2");
        s.HandleRetainedSessionRequest(create, "createMapsOlder"); CHECK(s.retainedCreateMapFirst == 0);  // one page holds them all
        // The fields hand over their text, at most the stock fields' lengths.
        create->state["create.name"] = std::string(30, 'n'); s.HandleRetainedSessionRequest(create, "createName");
        CHECK(cvarSystemObject.strings["si_name"] == std::string(25, 'n') && create->state["create.name"] == std::string(25, 'n'));
        create->state["create.mapcycle"] = "duelcycle"; s.HandleRetainedSessionRequest(create, "createMapCycle");
        CHECK(cvarSystemObject.strings["g_mapCycle"] == "duelcycle");
        // The passwords take 16 bytes, never cut inside a character, and may be
        // empty.
        create->state["create.password"] = std::string(15, 'p') + "\xc3\xa9x"; s.HandleRetainedSessionRequest(create, "createPassword");
        CHECK(cvarSystemObject.strings["g_password"] == std::string(15, 'p') && create->state["create.password"] == std::string(15, 'p'));
        create->state["create.rconpassword"] = ""; s.HandleRetainedSessionRequest(create, "createRconPassword");
        CHECK(cvarSystemObject.strings.count("net_serverRemoteConsolePassword") == 1 && cvarSystemObject.strings["net_serverRemoteConsolePassword"].empty());
        create->state["create.rconpassword"] = "letmein"; s.HandleRetainedSessionRequest(create, "createRconPassword");
        CHECK(cvarSystemObject.strings["net_serverRemoteConsolePassword"] == "letmein" && create->state["create.rconpassword"] == "letmein");
        // PLAYER BAN LIST: the game's bans as the stock popup reads them, names
        // without colors; a row chosen; REMOVE BAN removing the chosen row's
        // GUID as the session kept it, never the page's copy.
        gameObject.bans = {{"^1Griefer", "ABCDEFGHIJK"}, {"Camper", "BCDEFGHIJKL"}};
        s.HandleRetainedSessionRequest(create, "createBanList");
        CHECK(create->state["create.ban_count"] == "2" && create->state["create.ban0.name"] == "Griefer" && create->state["create.ban0.guid"] == "ABCDEFGHIJK" &&
              create->state["create.ban1.guid"] == "BCDEFGHIJKL" && create->state["create.ban_selected"] == "-1" && gameObject.populated == 1);
        s.HandleRetainedSessionRequest(create, "createBanRemove"); CHECK(gameObject.unbanned.empty());  // nothing chosen yet
        create->state["create.ban_pick"] = "2"; s.HandleRetainedSessionRequest(create, "createBanPick"); CHECK(create->state["create.ban_selected"] == "-1");
        create->state["create.ban_pick"] = "1"; s.HandleRetainedSessionRequest(create, "createBanPick");
        CHECK(create->state["create.ban_selected"] == "1" && s.played.back() == "play main_menu_selection");
        create->state["create.ban1.guid"] = "FORGED00000";
        s.HandleRetainedSessionRequest(create, "createBanRemove");
        CHECK((gameObject.unbanned == std::vector<std::string>{"BCDEFGHIJKL"}) && create->state["create.ban_count"] == "1" &&
              create->state["create.ban0.name"] == "Griefer" && create->state["create.ban_selected"] == "-1" && gameObject.populated == 2);
        // The list stops at the first row without a GUID, and at 32 rows.
        gameObject.bans.clear();
        for (int i = 0; i < 40; ++i) gameObject.bans.push_back({"Player" + std::to_string(i), "GUID" + std::to_string(1000000 + i)});
        s.HandleRetainedSessionRequest(create, "createBanList"); CHECK(create->state["create.ban_count"] == "32" && create->state["create.ban31.name"] == "Player31");
        gameObject.bans = {{"Valid", "ABCDEFGHIJK"}}; s.guiMainMenu->state["sa_banList_item_1"] = "2:  NoGuid";
        s.HandleRetainedSessionRequest(create, "createBanList"); CHECK(create->state["create.ban_count"] == "1");
        // Two-digit rows keep their names.
        idStr name, guid;
        CHECK(Session_BanListRow("12:  ^2Name:  with colons\tGUIDGUIDGUID", name, guid) && name == "^2Name:  with colons" && guid == "GUIDGUIDGUID");
        CHECK(!Session_BanListRow("3:  NoTab", name, guid) && !Session_BanListRow("3:  Trailing\t", name, guid) && !Session_BanListRow(nullptr, name, guid));
        s.HandleRetainedSessionRequest(create, "unknown"); CHECK(commonObject.warnings.back().find("unhandled Create Server request") != std::string::npos);
        // CREATE SERVER starts it as the stock page's does.
        s.HandleRetainedSessionRequest(create, "createStart"); CHECK(s.serverStarts == 1);
        // BACK climbs out: at 300 ms the Multiplayer page returns onto CREATE SERVER.
        s.HandleRetainedSessionRequest(create, "createBack");
        CHECK(s.retainedSubpageFrom == create && create->named.back() == "subpageLeave" && !s.retainedSubpageDeeper &&
              s.retainedSubpageUntil == commonObject.time + 300);
        s.HandleRetainedSessionRequest(create, "createStart"); CHECK(s.serverStarts == 1);  // nothing else meanwhile
        commonObject.time += 300; s.UpdateRetainedSubpage();
        CHECK(s.guiActive == page && page->state["from_create"] == "1" && page->named.back() == "subpageReturn" && s.retainedSubpageFrom == nullptr);
        // Back while going deeper reverses the change on the page.
        s.HandleRetainedSessionRequest(page, "multiplayerCreate"); commonObject.time += 120;
        sysEvent_t back; back.evType = SE_KEY; back.evValue = K_ESCAPE; back.evValue2 = 1;
        CHECK(s.RetainedSubpageEvent(&back) && page->named.back() == "subpageReverse150" && s.retainedSubpageFrom == nullptr && s.guiActive == page);
        // A Create Server that stopped drawing hands CREATE SERVER back to the stock page.
        create->failed = true; s.HandleRetainedSessionRequest(page, "multiplayerCreate");
        CHECK(s.guiRetainedCreate == nullptr && s.retainedMpPageDest == 17 && commonObject.warnings.back().find("stopped drawing") != std::string::npos &&
              (Stock(s) == std::vector<std::string>{"guis/menu/createserver.q4ui"}));
        ui_retainedMultiplayer.value = false;
    }
    {   // The Join Game sub-page (FLOW-050), Multiplayer's server browser,
        // offers all the stock browser does (RETAINED_JOIN_GAME_MISSING is
        // empty), so it is the default: without ui_retainedMultiplayer it
        // preloads and JOIN GAME goes deeper.
        auto s = Session(true); s.PreloadRetainedScreens(); s.UpdateRetainedHome();
        CHECK(s.guiRetainedJoin != nullptr && s.RetainedJoinGamePage() == s.guiRetainedJoin);
        s.HandleRetainedSessionRequest(s.guiRetainedHome, "multiplayer"); commonObject.time += 550; s.UpdateRetainedHome(); s.UpdateRetainedPage();
        s.HandleRetainedSessionRequest(s.guiRetainedMpPage, "multiplayerJoin");
        CHECK(s.retainedMpPageDest == 0 && s.retainedSubpageFrom == s.guiRetainedMpPage && s.guiRetainedMpPage->named.back() == "subpageEnterJoin");
    }
    {   // JOIN GAME goes deeper as CREATE SERVER does (ui_retainedMultiplayer
        // set here changes nothing for it), and at 350 ms the sub-page
        // presents as the stock browser opens (InitServerBrowser), listing the
        // servers the scan found and the filter passes, in its sort's order.
        auto s = Session(true); ui_retainedMultiplayer.value = true;
        auto& scan = idAsyncNetwork::client.serverList;
        const auto server = [](const std::string& address, const std::string& name, int ping, int clients, const char* type = "DM") {
            networkServer_t found; found.adr.address = address; found.ping = ping; found.clients = clients;
            found.serverInfo.values = {{"si_name", name}, {"si_gameType", type}, {"si_mapName", "^1Fragging ^7Yard"}, {"si_maxPlayers", "8"}};
            return found;
        };
        scan.servers = {server("10.0.0.1:28004", "^2Refinement ^7Arena", 12, 1), server("10.0.0.2:28004", "", -5, 40, "Freeze Tag"),
                        server("10.0.0.3:28004", "Lost Fleet", 85, 0)};
        scan.servers[1].serverInfo.values["si_usePass"] = "1";
        scan.servers[0].serverInfo.values["net_serverDedicated"] = "1"; scan.servers[2].serverInfo.values["si_repeater"] = "1";
        scan.servers[0].serverInfo.values["si_map"] = "mp/q4dm5"; scan.servers[2].serverInfo.values["si_map"] = "../escape";
        fileSystemObject.screenshots["mp/q4dm5"] = "gfx/guis/loadscreens/q4dm5"; fileSystemObject.files.insert("gfx/guis/loadscreens/q4dm5.tga");
        scan.visible = {2, 0, 1}; scan.favorites = {"10.0.0.1:28004"};
        s.guiMainMenu->state["sortNotice"] = "Showing 3 out of 3 servers"; s.guiMainMenu->state["filterMod"] = "^1All"; scan.menu = s.guiMainMenu;
        s.PreloadRetainedScreens(); s.UpdateRetainedHome();
        auto* join = s.guiRetainedJoin;
        CHECK(join && std::string(join->Name()) == "guis/menu/joingame.q4ui" && s.RetainedJoinGamePage() == join);
        s.HandleRetainedSessionRequest(s.guiRetainedHome, "multiplayer"); commonObject.time += 550; s.UpdateRetainedHome(); s.UpdateRetainedPage();
        auto* page = s.guiRetainedMpPage; CHECK(s.guiActive == page);
        s.HandleRetainedSessionRequest(page, "multiplayerJoin");
        CHECK(s.retainedSubpageFrom == page && page->named.back() == "subpageEnterJoin" && s.retainedSubpageDeeper &&
              s.retainedSubpageKind == RETAINED_SUBPAGE_JOIN_GAME && s.retainedSubpageUntil == commonObject.time + 350 && s.retainedMpPageDest == 0);
        s.HandleRetainedSessionRequest(page, "multiplayerCreate"); CHECK(s.retainedSubpageKind == RETAINED_SUBPAGE_JOIN_GAME);  // nothing else meanwhile
        commonObject.time += 350; s.UpdateRetainedSubpage();
        CHECK(s.guiActive == join && s.retainedSubpageFrom == nullptr && (scan.calls == std::vector<std::string>{"GUIInit"}) &&
              join->state["join.clientip"] == "192.0.2.7" && join->state["join.search"] == "" &&
              join->state["join.progress"] == std::to_string(0.0f) && join->state["join.notice"] == "");
        CHECK(join->state["join.count"] == "3" && join->state["join.listed"] == "3" && join->state["join.first"] == "0" &&
              join->state["join.selected"] == "-1" && join->state["join.sort"] == "0" && join->state["join.ascending"] == "1" && join->state["join.lan"] == "0" &&
              join->state["join.scanning"] == "0" && join->state["join.status"] == "Showing 3 out of 3 servers");
        // The rows in the list's order, the servers' text read as players'
        // text: a nameless server by its address, a negative ping as 0, the
        // players capped, the game type named.
        CHECK(join->state["join.row0.name"] == "Lost Fleet" && join->state["join.row0.ping"] == "85" && join->state["join.row0.players"] == "0/8" &&
              join->state["join.row1.name"] == "Refinement Arena" && join->state["join.row1.fav"] == "1" && join->state["join.row1.lock"] == "0" &&
              join->state["join.row1.type"] == "Deathmatch" && join->state["join.row1.map"] == "Fragging Yard" && join->state["join.row0.fav"] == "0");
        CHECK(join->state["join.row2.name"] == "10.0.0.2:28004" && join->state["join.row2.ping"] == "0" && join->state["join.row2.players"] == "32/8" &&
              join->state["join.row2.lock"] == "1" && join->state["join.row2.type"] == "Freeze Tag" && join->state["join.row3.name"] == "" &&
              join->state["join.row3.lock"] == "0");
        // Network quality (section 13.13) beside each ping: 85 ms three bars,
        // 12 ms and the floored 0 four, an empty row none.
        CHECK(join->state["join.row0.quality"] == "3" && join->state["join.row1.quality"] == "4" && join->state["join.row2.quality"] == "4" &&
              join->state["join.row3.quality"] == "0");
        CHECK(Session_NetworkQualityBars(60, 0) == 4 && Session_NetworkQualityBars(61, 0) == 3 && Session_NetworkQualityBars(120, 0) == 3 &&
              Session_NetworkQualityBars(121, 0) == 2 && Session_NetworkQualityBars(200, 0) == 2 && Session_NetworkQualityBars(201, 0) == 1 &&
              Session_NetworkQualityBars(10, 1.9f) == 4 && Session_NetworkQualityBars(10, 2.0f) == 3 && Session_NetworkQualityBars(10, 6.0f) == 1 &&
              Session_NetworkQualityBars(500, 20.0f) == 1);
        // The dedicated server's and the repeater's symbols; the mod filter as
        // the scan wrote it, read as players' text; nothing chosen, so the
        // generic art in the levelshot's frame and no favorite.
        CHECK(join->state["join.row1.ded"] == "1" && join->state["join.row0.ded"] == "0" && join->state["join.row0.rep"] == "1" &&
              join->state["join.row1.rep"] == "0" && join->state["join.row3.ded"] == "0" && join->state["join.mod"] == "All" &&
              join->state["join.shot"] == "gfx/guis/loadscreens/generic" && join->state["join.favorite"] == "0");
        // Nothing chosen: nothing to join.
        const size_t buffered = commands.buffered.size();
        s.HandleRetainedSessionRequest(join, "joinServer"); CHECK(commands.buffered.size() == buffered);
        // A row chooses its server, a row past the list nothing; the choice
        // follows its server into a new order, and an unchanged scan publishes
        // nothing.
        join->state["join.pick"] = "5"; s.HandleRetainedSessionRequest(join, "joinPick");
        CHECK(join->state["join.selected"] == "-1" && s.retainedJoinAddress.empty());
        join->state["join.pick"] = "1"; s.HandleRetainedSessionRequest(join, "joinPick");
        CHECK(join->state["join.selected"] == "1" && s.retainedJoinAddress == "10.0.0.1:28004" && s.played.back() == "play main_menu_selection");
        // The chosen server's levelshot, precached, and its favorite state.
        CHECK(join->state["join.shot"] == "gfx/guis/loadscreens/q4dm5" && precached.back() == "gfx/guis/loadscreens/q4dm5" &&
              join->state["join.favorite"] == "1");
        // The favorite toggle takes it off the favorites and puts it back.
        s.HandleRetainedSessionRequest(join, "joinFavorite");
        CHECK(scan.calls.back() == "ToggleFavoriteServer 0" && scan.favorites.empty() && join->state["join.favorite"] == "0" &&
              join->state["join.row1.fav"] == "0");
        s.HandleRetainedSessionRequest(join, "joinFavorite"); CHECK(scan.favorites.count("10.0.0.1:28004") && join->state["join.favorite"] == "1");
        // Section 13.13's notices, over whichever retained menu shows (here
        // Join Game): each toggle says what it did, newest at the top,
        // entering there; the removal offers Undo. The first publish to a menu
        // rests its slots.
        const auto named = [&](const char* event) { return std::count(join->named.begin(), join->named.end(), std::string(event)); };
        CHECK(s.retainedNoticesGui == join && s.retainedNotices.Num() == 2 && join->state["notice.0.shown"] == "1" &&
              join->state["notice.0.text"] == "#str_231539" && join->state["notice.0.action"] == "" && join->state["notice.0.kind"] == "1" &&
              join->state["notice.1.text"] == "#str_231540" && join->state["notice.1.action"] == "#str_42904" &&
              join->state["notice.2.shown"] == "0" && named("noticeEnter0") == 2 && named("noticeRest2") == 1);
        const auto rests = named("noticeRest0");
        // The plain notice holds 4 s, then leaves over 200 ms; its slot rests
        // and the older notice closes up into it.
        commonObject.time += 3999; s.UpdateRetainedNotices(); CHECK(named("noticeLeave0") == 0);
        commonObject.time += 1; s.UpdateRetainedNotices(); CHECK(named("noticeLeave0") == 1 && join->state["notice.0.shown"] == "1");
        commonObject.time += 200; s.UpdateRetainedNotices();
        CHECK(named("noticeRest0") == rests + 1 && s.retainedNotices.Num() == 1 && join->state["notice.0.text"] == "#str_231540" &&
              join->state["notice.1.shown"] == "0");
        // Removed again: a second Undo enters at the top. Its Undo puts the
        // server back among the favorites and the notice leaves.
        s.HandleRetainedSessionRequest(join, "joinFavorite");
        CHECK(scan.favorites.empty() && named("noticeEnter0") == 3 && join->state["notice.0.action"] == "#str_42904" &&
              join->state["notice.1.text"] == "#str_231540");
        s.HandleRetainedSessionRequest(join, "noticeAction0");
        CHECK(scan.favorites.count("10.0.0.1:28004") && join->state["join.favorite"] == "1" && named("noticeLeave0") == 2);
        // An action already taken, a slot without a notice, or another menu's
        // request, does nothing.
        const auto calls = scan.calls.size();
        s.HandleRetainedSessionRequest(join, "noticeAction0"); s.HandleRetainedSessionRequest(join, "noticeAction2");
        s.HandleRetainedSessionRequest(s.guiRetainedHome, "noticeAction1");
        CHECK(scan.calls.size() == calls && scan.favorites.count("10.0.0.1:28004"));
        commonObject.time += 200; s.UpdateRetainedNotices();
        CHECK(named("noticeRest0") == rests + 2 && s.retainedNotices.Num() == 1 && join->state["notice.0.text"] == "#str_231540");
        // A notice with an action holds 8 s.
        commonObject.time += 3599; s.UpdateRetainedNotices(); CHECK(named("noticeLeave0") == 2);
        commonObject.time += 1; s.UpdateRetainedNotices(); CHECK(named("noticeLeave0") == 3);
        commonObject.time += 200; s.UpdateRetainedNotices();
        CHECK(s.retainedNotices.empty() && join->state["notice.0.shown"] == "0");
        // At most three show; a fourth waits until one has gone.
        for (int i = 0; i < 4; ++i) s.PushRetainedNotice("#str_231539", 1, "", "", RETAINED_NOTICE_UNDO_NONE, false);
        for (int i = 0; i < 4; ++i) s.UpdateRetainedNotices();
        CHECK(join->state["notice.2.shown"] == "1" && s.retainedNotices[3].shownAt < 0);
        commonObject.time += 4000; s.UpdateRetainedNotices(); commonObject.time += 200; s.UpdateRetainedNotices();
        s.UpdateRetainedNotices();
        CHECK(s.retainedNotices.Num() == 1 && s.retainedNotices[0].shownAt >= 0 && join->state["notice.1.shown"] == "0");
        commonObject.time += 4000; s.UpdateRetainedNotices(); commonObject.time += 200; s.UpdateRetainedNotices();
        CHECK(s.retainedNotices.empty());
        // A controller's loss (section 13.7): a controller connecting says
        // nothing; its loss is a notice with the controller's symbol that
        // stays past any hold until one connects again.
        cvarSystemObject.strings["in_gamepadFamily"] = "xbox"; s.UpdateRetainedNotices(); CHECK(s.retainedNotices.empty());
        cvarSystemObject.strings["in_gamepadFamily"] = "none"; s.UpdateRetainedNotices();
        CHECK(s.retainedNotices.Num() == 1 && join->state["notice.0.text"] == "#str_231541" && join->state["notice.0.kind"] == "2" &&
              join->state["notice.0.action"] == "");
        const auto leaves = named("noticeLeave0");
        commonObject.time += 60000; s.UpdateRetainedNotices(); CHECK(named("noticeLeave0") == leaves && join->state["notice.0.shown"] == "1");
        cvarSystemObject.strings["in_gamepadFamily"] = "playstation"; s.UpdateRetainedNotices(); CHECK(named("noticeLeave0") == leaves + 1);
        commonObject.time += 200; s.UpdateRetainedNotices(); CHECK(s.retainedNotices.empty() && join->state["notice.0.shown"] == "0");
        // A loss and a quick return leave nothing behind.
        cvarSystemObject.strings["in_gamepadFamily"] = "none"; s.UpdateRetainedNotices();
        cvarSystemObject.strings["in_gamepadFamily"] = "xbox"; s.UpdateRetainedNotices(); s.UpdateRetainedNotices();
        commonObject.time += 200; s.UpdateRetainedNotices(); CHECK(s.retainedNotices.empty());
        // (A controller stays connected from here: dropping the CVar would read as a loss.)
        // The stack moves with the menu that shows; the home, drawn behind a
        // page as its frame, drops it while the page shows.
        // (The title shows over the legacy menu as the home; a page clears it.)
        s.PushRetainedNotice("#str_231539", RETAINED_NOTICE_FAVORITE, "", "", RETAINED_NOTICE_UNDO_NONE, false);
        auto* home = s.guiRetainedTitle; CHECK(home != nullptr);
        auto* homeBefore = s.guiRetainedHome; s.guiRetainedHome = home; s.guiActive = s.guiMainMenu; s.UpdateRetainedNotices();
        CHECK(s.retainedNoticesGui == home && home->state["notice.0.shown"] == "1" && home->state["notice.0.text"] == "#str_231539");
        s.guiRetainedHome = homeBefore; s.guiActive = join; s.UpdateRetainedNotices();
        CHECK(s.retainedNoticesGui == join && home->state["notice.0.shown"] == "0" && join->state["notice.0.shown"] == "1");
        commonObject.time += 4000; s.UpdateRetainedNotices(); commonObject.time += 200; s.UpdateRetainedNotices();
        CHECK(s.retainedNotices.empty() && join->state["notice.0.shown"] == "0");
        // MOD's spinner shows the next and the previous mod.
        s.HandleRetainedSessionRequest(join, "joinModNext"); CHECK(scan.calls.back() == "UpdateFilterByMod 1" && join->state["join.mod"] == "baseoq4");
        s.HandleRetainedSessionRequest(join, "joinModPrevious"); CHECK(scan.calls.back() == "UpdateFilterByMod -1" && join->state["join.mod"] == "All");
        // The filter row: each tag toggles its filter, which the session
        // publishes as the tag's state; the spinner steps the game type round
        // every type, publishing its name, none for every type.
        CHECK(join->state["join.filterFull"] == "0" && join->state["join.filterEmpty"] == "0" && join->state["join.filterPassword"] == "0" &&
              join->state["join.gameType"] == "0" && join->state["join.gameTypeName"] == "");
        s.HandleRetainedSessionRequest(join, "joinFilterFull");
        CHECK(scan.calls.back() == "ToggleFilter 0" && join->state["join.filterFull"] == "1" && join->state["join.filterEmpty"] == "0" &&
              s.played.back() == "play main_menu_selection");
        s.HandleRetainedSessionRequest(join, "joinFilterEmpty"); CHECK(scan.calls.back() == "ToggleFilter 1" && join->state["join.filterEmpty"] == "1");
        s.HandleRetainedSessionRequest(join, "joinFilterPassword");
        CHECK(scan.calls.back() == "ToggleFilter 2" && join->state["join.filterPassword"] == "1");
        for (const char* verb : {"joinFilterFull", "joinFilterEmpty", "joinFilterPassword"}) s.HandleRetainedSessionRequest(join, verb);
        CHECK(join->state["join.filterFull"] == "0" && join->state["join.filterEmpty"] == "0" && join->state["join.filterPassword"] == "0");
        s.HandleRetainedSessionRequest(join, "joinGameTypeNext");
        CHECK(scan.calls.back() == "StepGameTypeFilter 1" && join->state["join.gameType"] == "1" && join->state["join.gameTypeName"] == "Deathmatch");
        s.HandleRetainedSessionRequest(join, "joinGameTypePrevious"); s.HandleRetainedSessionRequest(join, "joinGameTypePrevious");
        CHECK(scan.calls.back() == "StepGameTypeFilter -1" && join->state["join.gameType"] == "12" &&
              join->state["join.gameTypeName"] == "Arena One Flag CTF");
        s.HandleRetainedSessionRequest(join, "joinGameTypeNext"); CHECK(join->state["join.gameType"] == "0" && join->state["join.gameTypeName"] == "");
        // The scan's progress and its notice, which can name a server: read as
        // players' text, its colors dropped and its length capped.
        scan.notice = "Scanning 2/5: ^1Red Arena"; scan.progress = 0.4f; ++scan.revision; s.UpdateRetainedJoinGame();
        CHECK(join->state["join.progress"] == std::to_string(0.4f) && join->state["join.notice"] == "Scanning 2/5: Red Arena");
        scan.notice = std::string(300, 'n'); scan.progress = 1.0f; ++scan.revision; s.UpdateRetainedJoinGame();
        CHECK(join->state["join.notice"] == std::string(160, 'n') && join->state["join.progress"] == std::to_string(1.0f));
        scan.notice.clear(); scan.progress = 0; ++scan.revision; s.UpdateRetainedJoinGame();
        // The details card: no row under the pointer or the focus, none; the
        // row the runtime writes names its server, whose address, players and
        // settings the session publishes as players' text with the row they
        // describe; a row past the list, none; a new order, the row's new
        // server.
        CHECK(join->state["join.detail.row"] == "-1" && join->state["join.detail.address"] == "" && join->state["join.detail.player0"] == "");
        scan.players[scan.servers[scan.visible[1]].adr.address] = {"^1Grunt", "Kane"};
        join->state["join.detail"] = "1"; s.UpdateRetainedJoinGame();
        CHECK(join->state["join.detail.row"] == "1" && join->state["join.detail.address"] == scan.servers[scan.visible[1]].adr.address &&
              join->state["join.detail.name"] == "Refinement Arena" &&
              join->state["join.detail.player0"] == "Grunt" && join->state["join.detail.player1"] == "Kane" && join->state["join.detail.player2"] == "" &&
              join->state["join.detail.setting0"].rfind("Map: ", 0) == 0 && join->state["join.detail.setting1"] == "");
        int detailChanges = join->stateChanges; s.UpdateRetainedJoinGame(); CHECK(join->stateChanges == detailChanges);   // unchanged
        join->state["join.detail"] = "9"; s.UpdateRetainedJoinGame();
        CHECK(join->state["join.detail.row"] == "-1" && join->state["join.detail.address"] == "" && join->state["join.detail.player0"] == "" &&
              join->state["join.detail.name"] == "");
        join->state["join.detail"] = "0"; s.UpdateRetainedJoinGame();
        const std::string firstAddress = join->state["join.detail.address"];
        std::swap(scan.visible[0], scan.visible[1]); ++scan.revision; s.UpdateRetainedJoinGame();
        CHECK(join->state["join.detail.row"] == "0" && join->state["join.detail.address"] != firstAddress &&
              join->state["join.detail.player0"] == "Grunt");
        std::swap(scan.visible[0], scan.visible[1]); join->state["join.detail"] = "-1"; ++scan.revision; s.UpdateRetainedJoinGame();
        scan.players.clear();
        // JOIN IP ADDRESS: nothing entered, nothing to join; an address it
        // cannot join keeps the dialog with the reason; one it can connects and
        // closes the dialog.
        const size_t connects = commands.buffered.size();
        join->state["join.address"] = ""; s.HandleRetainedSessionRequest(join, "joinConnect");
        CHECK(typedAddresses.empty() && commands.buffered.size() == connects);
        join->state["join.address"] = "bad address"; s.HandleRetainedSessionRequest(join, "joinConnect");
        CHECK(typedAddresses.back() == "bad address" && join->state["join.addressError"] == "Cannot join bad address" &&
              commands.buffered.size() == connects && (join->named.empty() || join->named.back() != "joinIpModalHide"));
        CHECK(!s.retainedJoinReturn);  // nothing joined, nothing to come back to
        join->state["join.address"] = std::string(120, 'a'); s.HandleRetainedSessionRequest(join, "joinConnect");
        CHECK(typedAddresses.back() == std::string(99, 'a') && join->named.back() == "joinIpModalHide");  // the stock field's 99
        CHECK(s.retainedJoinReturn); s.retainedJoinReturn = false;  // a typed join comes back to the list too
        commands.buffered.resize(connects);
        // FIND PLAYER/CLAN searches the list for the name entered; its tag
        // ends the search.
        join->state["join.find"] = "Gr^1unt"; s.HandleRetainedSessionRequest(join, "joinSearch");
        CHECK(scan.calls.back() == "SetPlayerSearch Gr^1unt" && join->state["join.search"] == "Grunt");
        s.HandleRetainedSessionRequest(join, "joinSearchClear"); CHECK(scan.calls.back() == "SetPlayerSearch " && join->state["join.search"] == "");
        // The empty state's CLEAR FILTERS clears the tags, the game type, the
        // mod and the search in one request, and the page shows them clear.
        s.HandleRetainedSessionRequest(join, "joinFilterFull"); s.HandleRetainedSessionRequest(join, "joinGameTypeNext");
        s.HandleRetainedSessionRequest(join, "joinModNext"); join->state["join.find"] = "Grunt"; s.HandleRetainedSessionRequest(join, "joinSearch");
        CHECK(join->state["join.filterFull"] == "1" && join->state["join.gameType"] != "0" && join->state["join.search"] == "Grunt");
        s.HandleRetainedSessionRequest(join, "joinClearFilters");
        CHECK(scan.calls.back() == "ClearFilters" && join->state["join.filterFull"] == "0" && join->state["join.filterEmpty"] == "0" &&
              join->state["join.filterPassword"] == "0" && join->state["join.gameType"] == "0" && join->state["join.search"] == "" &&
              join->state["join.mod"] == "All");
        // A map path the browser may not look up shows the generic art.
        join->state["join.pick"] = "0"; s.HandleRetainedSessionRequest(join, "joinPick");
        CHECK(join->state["join.selected"] == "0" && join->state["join.shot"] == "gfx/guis/loadscreens/generic" && join->state["join.favorite"] == "0");
        join->state["join.pick"] = "1"; s.HandleRetainedSessionRequest(join, "joinPick");
        CHECK(join->state["join.selected"] == "1" && join->state["join.shot"] == "gfx/guis/loadscreens/q4dm5");
        scan.visible = {0, 1, 2}; ++scan.revision; s.UpdateRetainedJoinGame();
        CHECK(join->state["join.selected"] == "0" && join->state["join.row0.name"] == "Refinement Arena");
        int changes = join->stateChanges; s.UpdateRetainedJoinGame(); CHECK(join->stateChanges == changes);
        // A heading sorts by its column, and again the other way; CLEAR
        // SORTING lists the servers as they answered.
        s.HandleRetainedSessionRequest(join, "joinSortName");
        CHECK(scan.calls.back() == "SetSorting 1" && join->state["join.sort"] == "1" && join->state["join.ascending"] == "1");
        s.HandleRetainedSessionRequest(join, "joinSortName"); CHECK(join->state["join.ascending"] == "0");
        for (const auto& [verb, sort] : {std::pair<const char*, int>{"joinSortFavorite", 6}, {"joinSortLocked", 7}, {"joinSortPing", 0},
                                         {"joinSortPlayers", 2}, {"joinSortGameType", 3}, {"joinSortMap", 4}, {"joinSortDedicated", 8},
                                         {"joinSortRepeater", 10}}) {
            s.HandleRetainedSessionRequest(join, verb); CHECK(scan.calls.back() == "SetSorting " + std::to_string(sort) && join->state["join.sort"] == std::to_string(sort));
        }
        s.HandleRetainedSessionRequest(join, "joinClearSort"); CHECK(scan.calls.back() == "ResetSorting" && join->state["join.sort"] == "11");
        // The controller's north: the next text column, ascending, from PING
        // when the list is unsorted or sorted by a symbol.
        for (const int sort : {0, 1, 2, 3, 4, 0}) {
            s.HandleRetainedSessionRequest(join, "joinSortNext");
            CHECK(scan.calls.back() == "SetSorting " + std::to_string(sort) && join->state["join.sort"] == std::to_string(sort) &&
                  join->state["join.ascending"] == "1");
        }
        s.HandleRetainedSessionRequest(join, "joinSortName"); s.HandleRetainedSessionRequest(join, "joinSortName");
        CHECK(join->state["join.ascending"] == "0");
        s.HandleRetainedSessionRequest(join, "joinSortNext");
        CHECK(scan.calls.back() == "SetSorting 2" && join->state["join.ascending"] == "1");
        s.HandleRetainedSessionRequest(join, "joinSortLocked"); s.HandleRetainedSessionRequest(join, "joinSortNext");
        CHECK(scan.calls.back() == "SetSorting 0" && join->state["join.sort"] == "0");
        s.HandleRetainedSessionRequest(join, "joinClearSort");
        // The Internet: GET NEW LIST asks the master server and the favorites,
        // REFRESH LIST the listed servers again.
        scan.calls.clear(); s.HandleRetainedSessionRequest(join, "joinNewList");
        CHECK((scan.calls == std::vector<std::string>{"SetPlayerSearch ", "GUIInit", "GetNETServers", "AddFavoriteServers"}) &&
              join->state["join.scanning"] == "1");
        s.HandleRetainedSessionRequest(join, "joinRefresh"); CHECK(scan.calls.back() == "NetScan" && commands.buffered.size() == buffered);
        // While a scan runs, a chosen server it has not listed again stays
        // chosen; so does one the scan holds and a filter or search hides;
        // once the scan no longer holds it, the choice is gone.
        scan.visible = {2}; ++scan.revision; s.UpdateRetainedJoinGame();
        CHECK(join->state["join.selected"] == "-1" && s.retainedJoinAddress == "10.0.0.1:28004");
        scan.visible = {2, 0}; ++scan.revision; s.UpdateRetainedJoinGame(); CHECK(join->state["join.selected"] == "1");
        scan.state = idServerScan::IDLE; scan.visible = {2}; ++scan.revision; s.UpdateRetainedJoinGame();
        CHECK(join->state["join.selected"] == "-1" && s.retainedJoinAddress == "10.0.0.1:28004" && join->state["join.scanning"] == "0");
        scan.servers[0].adr.address = "10.0.0.9:28004"; ++scan.revision; s.UpdateRetainedJoinGame();
        CHECK(join->state["join.selected"] == "-1" && s.retainedJoinAddress.empty());
        scan.servers[0].adr.address = "10.0.0.1:28004";
        // Filters or a search that hide every server: none shown, three held.
        scan.visible.clear(); ++scan.revision; s.UpdateRetainedJoinGame();
        CHECK(join->state["join.count"] == "0" && join->state["join.listed"] == "3");
        join->named.clear();
        scan.visible = {2}; ++scan.revision; s.UpdateRetainedJoinGame();
        // An idle list a filter emptied comes back without the reveal...
        CHECK(std::count(join->named.begin(), join->named.end(), std::string("joinRowsReveal")) == 0);
        // ...which plays once when a scan's first servers replace its loading
        // rows, and not again as more arrive.
        scan.state = idServerScan::NET_SCAN; scan.visible.clear(); ++scan.revision; s.UpdateRetainedJoinGame();
        CHECK(join->state["join.count"] == "0" && join->state["join.scanning"] == "1");
        scan.visible = {2}; ++scan.revision; s.UpdateRetainedJoinGame();
        CHECK(join->state["join.count"] == "1" && std::count(join->named.begin(), join->named.end(), std::string("joinRowsReveal")) == 1);
        scan.visible = {2, 0}; ++scan.revision; s.UpdateRetainedJoinGame();
        CHECK(std::count(join->named.begin(), join->named.end(), std::string("joinRowsReveal")) == 1);
        scan.state = idServerScan::IDLE; scan.visible = {2}; ++scan.revision; s.UpdateRetainedJoinGame();
        // The LAN, which the legacy menu's scans read too: both ask the LAN.
        s.HandleRetainedSessionRequest(join, "joinSourceLan");
        CHECK(s.guiMainMenu->state["lanSet"] == "1" && join->state["join.lan"] == "1" && commands.buffered.back() == "LANScan" &&
              commands.buffered.size() == buffered + 1 && scan.calls.back() == "GUIInit");
        s.HandleRetainedSessionRequest(join, "joinRefresh"); CHECK(commands.buffered.size() == buffered + 2 && commands.buffered.back() == "LANScan");
        s.HandleRetainedSessionRequest(join, "joinSourceInternet");
        CHECK(s.guiMainMenu->state["lanSet"] == "0" && join->state["join.lan"] == "0" && scan.calls.back() == "AddFavoriteServers");
        // Seventy servers: the list pages on and back, the last page holding
        // the rest.
        scan.state = idServerScan::IDLE; scan.servers.clear(); scan.visible.clear();
        for (int i = 0; i < 70; ++i) {
            scan.servers.push_back(server("10.0.1." + std::to_string(i) + ":28004", "Server " + std::to_string(i), i, 0)); scan.visible.push_back(i);
        }
        ++scan.revision; s.UpdateRetainedJoinGame();
        CHECK(join->state["join.count"] == "70" && join->state["join.listed"] == "70" && join->state["join.row63.name"] == "Server 63");
        s.HandleRetainedSessionRequest(join, "joinNewer"); CHECK(s.retainedJoinFirst == 0);
        s.HandleRetainedSessionRequest(join, "joinOlder");
        CHECK(s.retainedJoinFirst == 64 && join->state["join.first"] == "64" && join->state["join.row5.name"] == "Server 69" && join->state["join.row6.name"] == "");
        s.HandleRetainedSessionRequest(join, "joinOlder"); CHECK(s.retainedJoinFirst == 64);
        // Row focus follows its server through a new order onto its page, and
        // stays where it is when its server leaves the list.
        join->focused = "server-5"; std::swap(scan.visible[0], scan.visible[69]); ++scan.revision; s.UpdateRetainedJoinGame();
        CHECK(s.retainedJoinFirst == 0 && join->named.back() == "joinFocus0" && join->state["join.row0.name"] == "Server 69");
        join->focused = "server-0"; const size_t focusEvents = join->named.size();
        const int moved = scan.visible[0]; scan.visible.erase(scan.visible.begin()); ++scan.revision; s.UpdateRetainedJoinGame();
        CHECK(join->named.size() == focusEvents && s.retainedJoinFirst == 0 && join->state["join.row0.name"] == "Server 1");
        scan.visible.insert(scan.visible.begin(), moved); std::swap(scan.visible[0], scan.visible[69]); join->focused.clear();
        ++scan.revision; s.UpdateRetainedJoinGame(); CHECK(join->state["join.row0.name"] == "Server 0");
        s.HandleRetainedSessionRequest(join, "joinOlder"); CHECK(s.retainedJoinFirst == 64);
        join->state["join.pick"] = "5"; s.HandleRetainedSessionRequest(join, "joinPick"); CHECK(join->state["join.selected"] == "69");
        // JOIN SERVER connects to the chosen server by the address the session
        // kept, never the page's.
        cvarSystemObject.strings["ui_spectate"] = "Spectate";
        join->state["join.row5.name"] = "forged"; s.HandleRetainedSessionRequest(join, "joinServer");
        CHECK(commands.buffered.size() == buffered + 3 && commands.buffered.back() == "connect 10.0.1.69:28004");
        // JOIN plays, whatever the last join chose; SPECTATE connects the same
        // way, watching (ui_spectate, the player's userinfo).
        CHECK(std::string(cvarSystemObject.GetCVarString("ui_spectate")) == "Play");
        s.HandleRetainedSessionRequest(join, "joinSpectate");
        const size_t joined = commands.buffered.size();
        CHECK(joined == buffered + 4 && commands.buffered.back() == "connect 10.0.1.69:28004" &&
              std::string(cvarSystemObject.GetCVarString("ui_spectate")) == "Spectate");
        // A join that fails or is aborted comes back to this list: when the
        // menu next rests at home, Join Game presents instead of the title,
        // once. A join that loads its map forgets the list.
        CHECK(s.retainedJoinReturn && s.guiRetainedHome == nullptr);
        s.guiActive = s.guiMainMenu; s.UpdateRetainedHome();
        CHECK(s.guiActive == join && !s.retainedJoinReturn && s.guiRetainedHome == nullptr);
        s.retainedJoinReturn = true; s.mapSpawned = true; s.UpdateRetainedHome(); CHECK(!s.retainedJoinReturn); s.mapSpawned = false;
        s.HandleRetainedSessionRequest(join, "unknown"); CHECK(commonObject.warnings.back().find("unhandled Join Game request") != std::string::npos);
        // BACK climbs out: at 300 ms the Multiplayer page returns onto JOIN
        // GAME, and the list no longer follows the scan.
        s.HandleRetainedSessionRequest(join, "joinBack");
        CHECK(s.retainedSubpageFrom == join && join->named.back() == "subpageLeave" && !s.retainedSubpageDeeper &&
              s.retainedSubpageUntil == commonObject.time + 300);
        s.HandleRetainedSessionRequest(join, "joinServer"); s.HandleRetainedSessionRequest(join, "joinSpectate");
        CHECK(commands.buffered.size() == joined);  // nothing else meanwhile
        commonObject.time += 300; s.UpdateRetainedSubpage();
        CHECK(s.guiActive == page && page->state["from_join"] == "1" && page->state["from_create"] == "0" && page->named.back() == "subpageReturn");
        changes = join->stateChanges; ++scan.revision; s.UpdateRetainedJoinGame(); CHECK(join->stateChanges == changes);
        // JOIN GAME again opens on the page holding the chosen server.
        s.HandleRetainedSessionRequest(page, "multiplayerJoin"); commonObject.time += 350; s.UpdateRetainedSubpage();
        CHECK(s.guiActive == join && join->state["join.first"] == "64" && join->state["join.selected"] == "69");
        // Back where the Multiplayer page stopped drawing finds the stock
        // Multiplayer page instead of staying on the sub-page.
        page->failed = true; s.PresentRetainedSubpage(false, RETAINED_SUBPAGE_JOIN_GAME);
        CHECK(s.guiActive == s.guiMainMenu && s.guiMainMenu->named.back() == "openMultiplayerStock" && s.guiRetainedMpPage == nullptr);
        page->failed = false; s.retainedStock.Clear(); s.guiRetainedMpPage = page; s.OpenRetainedJoinGame(); CHECK(s.guiActive == join);
        // Back while going deeper reverses the change on the page.
        s.HandleRetainedSessionRequest(join, "joinBack"); commonObject.time += 300; s.UpdateRetainedSubpage(); CHECK(s.guiActive == page);
        s.HandleRetainedSessionRequest(page, "multiplayerJoin"); commonObject.time += 120;
        sysEvent_t back; back.evType = SE_KEY; back.evValue = K_ESCAPE; back.evValue2 = 1;
        CHECK(s.RetainedSubpageEvent(&back) && page->named.back() == "subpageReverse150" && s.retainedSubpageFrom == nullptr && s.guiActive == page);
        // A Join Game that stopped drawing hands JOIN GAME back to the stock browser.
        join->failed = true; s.HandleRetainedSessionRequest(page, "multiplayerJoin");
        CHECK(s.guiRetainedJoin == nullptr && s.retainedMpPageDest == 14 && commonObject.warnings.back().find("stopped drawing") != std::string::npos &&
              (Stock(s) == std::vector<std::string>{"guis/menu/joingame.q4ui"}));
        ui_retainedMultiplayer.value = false;
    }
    {   // The title has no level to restart, and the Strogg pause hands off to
        // the Strogg family's page.
        auto title = Session(true); title.PreloadRetainedScreens(); title.UpdateRetainedHome();
        title.HandleRetainedSessionRequest(title.guiRetainedHome, "restartLevel");
        CHECK(legacyActions.back() == "main_b_difficulty" && title.retainedPagePending == nullptr);
        auto strogg = Session(true, true); strogg.PreloadRetainedScreens();
        gameObject.publish = [](idUserInterface* gui) { gui->SetStateBool("pause_strogg", true); };
        cvarSystemObject.strings["g_skill"] = "2";
        strogg.UpdateRetainedHome(); const size_t stroggActions = legacyActions.size();
        strogg.HandleRetainedSessionRequest(strogg.guiRetainedHome, "restartLevel");
        CHECK(legacyActions.size() == stroggActions && strogg.guiRetainedRestartStrogg != nullptr &&
              strogg.retainedPagePending == strogg.guiRetainedRestartStrogg && strogg.restarts.empty() &&
              strogg.guiRetainedRestartStrogg->state["restart.skill"] == "2" &&
              std::string(strogg.guiRetainedRestartStrogg->Name()) == "guis/menu/restartlevel_strogg.q4ui");
        commonObject.time += 550; strogg.UpdateRetainedHome(); strogg.UpdateRetainedPage();
        CHECK(strogg.guiActive == strogg.guiRetainedRestartStrogg && strogg.RetainedPageActive());
        strogg.HandleRetainedSessionRequest(strogg.guiActive, "restartBack");
        CHECK(strogg.guiActive == strogg.guiMainMenu && strogg.restarts.empty());
        gameObject.publish = nullptr;
    }
    {   // No save: CONTINUE stays hidden and has nothing to load.
        auto s = Session(true); s.UpdateRetainedHome();
        CHECK(s.guiRetainedHome->state["menu_continue"] == "0");
        s.HandleRetainedSessionRequest(s.guiRetainedHome, "continue"); CHECK(s.loadedGames.empty());
    }
    {   // A newest save LoadGame refuses: CONTINUE opens the Load Game page on
        // it and says why, instead of nothing happening.
        auto s = Session(true); s.saves = {"quick", "autosave"}; s.PreloadRetainedScreens(); s.UpdateRetainedHome();
        s.loadRefusal = idSessionLocal::LOADGAME_FAILURE_DAMAGED;
        s.HandleRetainedSessionRequest(s.guiRetainedHome, "continue");
        CHECK(s.loadedGames == std::vector<std::string>{"quick"} && s.guiRetainedLoad != nullptr && s.retainedPagePending == s.guiRetainedLoad);
        CHECK(s.guiRetainedLoad->state["load.selected"] == "0" && s.guiRetainedLoad->state["load.error"] == "#str_231103");
        // A save that loads leaves the title as before, with no page.
        auto t = Session(true); t.saves = {"quick"}; t.PreloadRetainedScreens(); t.UpdateRetainedHome();
        t.HandleRetainedSessionRequest(t.guiRetainedHome, "continue");
        CHECK(t.loadedGames == std::vector<std::string>{"quick"} && t.retainedPagePending == nullptr);
    }
    {   // Single-player pause and its verbs; multiplayer keeps the stock in-game menu.
        auto s = Session(true, true); s.UpdateRetainedHome();
        CHECK(s.guiRetainedHome && std::string(s.guiRetainedHome->Name()) == "guis/menu/pause.q4ui" && s.guiRetainedHome->state["pause_level"] == "game/airdefense1");
        auto* pause = s.guiRetainedHome;
        s.HandleRetainedSessionRequest(pause, "quitToMenu"); CHECK(commands.buffered == std::vector<std::string>{"disconnect\n"});
        // (SETTINGS over a main menu without the route to its settings screen: its own.)
        legacyEventsMissing = {"openSettingsPage"};
        s.HandleRetainedSessionRequest(pause, "settings"); CHECK(!legacyActions.empty() && legacyActions.back() == "main_b_settings");
        legacyEventsMissing.clear();
        commonObject.time += 600; legacy["desktop::active"] = 0; legacy["desktop::dest"] = 0; s.UpdateRetainedHome();
        s.HandleRetainedSessionRequest(pause, "resume"); CHECK(s.exits == 1);
        s.UpdateRetainedHome(); CHECK(s.guiRetainedHome == nullptr && !pause->active);
        auto mp = Session(true, true); mp.multiplayer = true; mp.UpdateRetainedHome();
        CHECK(mp.guiRetainedHome == nullptr && managerObject.loads.empty());
        auto test = Session(true); idUserInterface probe("guis/probe.gui"); test.guiTest = &probe; test.UpdateRetainedHome();
        CHECK(test.guiRetainedHome == nullptr && managerObject.loads.empty());
        auto open = Session(true); preview = true; open.UpdateRetainedHome(); CHECK(open.guiRetainedHome == nullptr);
    }
    {   // Startup resolves the screens the title reaches; a single-player level
        // load, the pause screens and the pages only they reach, and the pause
        // levelshot.
        auto s = Session(true); s.PreloadRetainedScreens();
        CHECK((managerObject.loads == std::vector<std::string>{"guis/menu/title.q4ui", "guis/menu/loadgame.q4ui",
                                                            "guis/menu/credits.q4ui", "guis/menu/demos.q4ui", "guis/menu/arena.q4ui",
                                                            "guis/menu/multiplayer.q4ui", "guis/menu/createserver.q4ui",
                                                            "guis/menu/joingame.q4ui"}) &&
              s.guiRetainedHome == nullptr && s.guiRetainedLoad != nullptr && s.guiRetainedPause == nullptr && s.guiRetainedSave == nullptr);
        CHECK(!managerObject.flags[1].unique && managerObject.flags[1].shared);  // the page is one shared document
        CHECK((precached == std::vector<std::string>{"gfx/guis/loadscreens/generic"}));  // a save without a picture shows it
        s.UpdateRetainedHome(); CHECK(managerObject.loads.size() == 8 && s.guiRetainedHome == s.guiRetainedTitle);
        // A multiplayer level loads each card whose pages are all built, the
        // summary card among them.
        const size_t cards = (RETAINED_MP_ESCAPE_MISSING_PAGES[0] == NULL ? 1 : 0) + (RETAINED_MP_WELCOME_MISSING_PAGES[0] == NULL ? 1 : 0) + 1;
        s.PrepareRetainedLevel("mp/q4dm1", true); CHECK(precached.size() == 1 && managerObject.loads.size() == 8 + cards);
        s.PrepareRetainedLevel("game/airdefense1", false);
        CHECK((precached == std::vector<std::string>{"gfx/guis/loadscreens/generic", "gfx/guis/loadscreens/generic"}) &&
              (std::vector<std::string>(managerObject.loads.begin() + 8 + cards, managerObject.loads.end()) ==
               std::vector<std::string>{"guis/menu/pause.q4ui", "guis/menu/pause_strogg.q4ui",  // either family may pause the level
                                        "guis/menu/savegame.q4ui", "guis/menu/restartlevel.q4ui", "guis/menu/restartmenu.q4ui"}));
        s.PrepareRetainedLevel("game/airdefense1", false); CHECK(managerObject.loads.size() == 13 + cards);  // once a session
        auto late = Session(true); late.PrepareRetainedLevel("game/airdefense1", false); // the gate switched on after startup
        CHECK((managerObject.loads == std::vector<std::string>{"guis/menu/pause.q4ui", "guis/menu/pause_strogg.q4ui", "guis/menu/loadgame.q4ui",
                                                            "guis/menu/savegame.q4ui", "guis/menu/restartlevel.q4ui",
                                                            "guis/menu/restartmenu.q4ui"}) && precached.size() == 2);
    }
    {   // The multiplayer levelshots: queued at startup, read on the front end's
        // idle frames once the title has presented for a second, as many as fit
        // 4 ms a frame (at least one), each as a precache; none while a level
        // plays, and Create Server reads all still waiting.
        auto s = Session(true);
        fileSystemObject.maps = {idDict{{{"path", "mp/q4dm1"}, {"DM", "1"}}}, idDict{{{"path", "mp/q4dm2"}, {"DM", "1"}}},
                                 idDict{{{"path", "mp/q4dm3"}, {"DM", "1"}}}, idDict{{{"path", "mp/q4dm4"}, {"DM", "1"}}},
                                 idDict{{{"path", "game/airdefense1"}}}};
        declManagerObject.insideLoad = true; s.PreloadRetainedScreens(); declManagerObject.insideLoad = false;  // as Init does
        s.UpdateRetainedHome();
        CHECK(s.retainedLevelshots.Num() == 4 && precached.size() == 1);  // the Load page's generic shot only
        precacheCost = 3; sysMilliseconds = 100;
        s.PrecacheRetainedLevelshots(false); CHECK(precached.size() == 1 && s.retainedLevelshotsFrom == 1100);
        sysMilliseconds = 1099; s.PrecacheRetainedLevelshots(false); CHECK(precached.size() == 1);
        sysMilliseconds = 1100; s.PrecacheRetainedLevelshots(false);  // 3 ms, then 6: two fit
        CHECK(precached.size() == 3 && s.retainedLevelshots.Num() == 2 && latePrecaches == 0 && !declManagerObject.insideLoad);
        precacheCost = 10; s.PrecacheRetainedLevelshots(false); CHECK(precached.size() == 4 && s.retainedLevelshots.Num() == 1);
        s.mapSpawned = true; s.PrecacheRetainedLevelshots(false); CHECK(precached.size() == 4);
        s.mapSpawned = false; s.guiActive = nullptr; s.PrecacheRetainedLevelshots(false); CHECK(precached.size() == 4);
        s.PrecacheRetainedLevelshots(true); CHECK(precached.size() == 5 && s.retainedLevelshots.Num() == 0 && latePrecaches == 0);
        s.guiActive = s.guiMainMenu; s.PrecacheRetainedLevelshots(false); CHECK(precached.size() == 5);
        // A precache inside a load stays inside it.
        auto inside = Session(true); fileSystemObject.maps = {idDict{{{"path", "mp/q4dm1"}, {"DM", "1"}}}};
        declManagerObject.insideLoad = true; inside.PreloadRetainedScreens(); inside.PrecacheRetainedLevelshots(true);
        CHECK(declManagerObject.insideLoad && latePrecaches == 0 && inside.retainedLevelshots.Num() == 0);
        // A queued picture, as the Arena campaign queues its levelshots: its
        // stock material and its retained picture, after the maps.
        auto arena = Session(true); fileSystemObject.maps = {idDict{{{"path", "mp/q4dm1"}, {"DM", "1"}}}};
        declManagerObject.insideLoad = true; arena.PreloadRetainedScreens(); declManagerObject.insideLoad = false;
        arena.QueueRetainedLevelshot("gfx/guis/loadscreens/arena_q4dm1", true);
        CHECK(arena.retainedLevelshots.Num() == 2 && declManagerObject.materials.empty());
        // Each page reads only its own kind: the Arena page its pictures,
        // Create Server the maps.
        precached.clear(); arena.PrecacheRetainedLevelshots(true, false);
        CHECK((precached == std::vector<std::string>{"gfx/guis/loadscreens/arena_q4dm1"}) &&
              (declManagerObject.materials == std::vector<std::string>{"gfx/guis/loadscreens/arena_q4dm1"}) &&
              arena.retainedLevelshots.Num() == 1 && arena.retainedLevelshots[0].map && latePrecaches == 0);
        arena.PrecacheRetainedLevelshots(true);
        CHECK(precached.size() == 2 && precached.back() == "gfx/guis/loadscreens/generic" && arena.retainedLevelshots.Num() == 0);
        precacheCost = 0; sysMilliseconds = 0;
    }
    {   // Loading: stock screens only, one shared instance, and a failed load reports once.
        auto s = Session(true); idUserInterface generic("guis/loading/generic.gui"), intro("guis/loading/intro.gui"), custom("guis/map/mylevel.gui");
        CHECK(s.SelectRetainedLoadingGui(&custom, false) == &custom && managerObject.loads.empty());
        auto* retained = s.SelectRetainedLoadingGui(&intro, true);
        CHECK(retained != &intro && std::string(retained->Name()) == "guis/loading/loading.q4ui");
        CHECK(!managerObject.flags[0].unique && managerObject.flags[0].shared);
        CHECK(retained->state["loading_mp"] == "1" && retained->state["loading_intro"] == "1" && retained->state["loading_ready"] == "0");
        auto failing = Session(true); managerObject.fail = true;
        CHECK(failing.SelectRetainedLoadingGui(&generic, false) == &generic && failing.SelectRetainedLoadingGui(&generic, false) == &generic);
        CHECK(managerObject.loads.size() == 1 && commonObject.warnings.size() == 1);
        auto title = Session(true); managerObject.fail = true; title.UpdateRetainedHome(); title.UpdateRetainedHome();
        CHECK(title.guiRetainedHome == nullptr && managerObject.loads.size() == 1 && commonObject.warnings.size() == 1);
        CHECK((Stock(title) == std::vector<std::string>{"guis/menu/title.q4ui"}));
    }
    {   // A mod's own copy of a stock loading screen stays; the stock screens of
        // openQ4, the stock game and the Awakening are replaced.
        auto s = Session(true); idUserInterface splevel("guis/loading/splevel.gui"), mplevel("guis/loading/mplevel.gui");
        cvarSystemObject.strings["fs_game"] = "mymod";
        fileSystemObject.gameDirFiles["mymod"] = {"guis/loading/splevel.gui"};
        CHECK(s.SelectRetainedLoadingGui(&splevel, false) == &splevel && managerObject.loads.empty() && commonObject.warnings.empty());
        CHECK(fileSystemObject.closes == 1 && commonObject.developer.size() == 1 &&
              commonObject.developer[0].find("'guis/loading/splevel.gui'") != std::string::npos);
        CHECK(s.SelectRetainedLoadingGui(&mplevel, true) != &mplevel);  // a stock screen the mod does not ship
        auto based = Session(true); idUserInterface intro("guis/loading/intro.gui");
        cvarSystemObject.strings["fs_game"] = "mymod"; cvarSystemObject.strings["fs_game_base"] = "mybase";
        fileSystemObject.gameDirFiles["mybase"] = {"guis/loading/intro.gui"};
        CHECK(based.SelectRetainedLoadingGui(&intro, false) == &intro);  // the mod's own base supplies it
        for (const char* firstParty : {"baseoq4", "q4base", "q4xbase", "Q4XBASE"}) {
            auto own = Session(true); idUserInterface generic("guis/loading/generic.gui");
            cvarSystemObject.strings["fs_game"] = firstParty; fileSystemObject.gameDirFiles[firstParty] = {"guis/loading/generic.gui"};
            CHECK(own.SelectRetainedLoadingGui(&generic, false) != &generic && fileSystemObject.opens == 0);
        }
    }
    {   // A document that is not installed falls back quietly, screen by screen.
        auto s = Session(true); fileSystemObject.files.erase("guis/menu/title.q4ui");
        s.UpdateRetainedHome(); s.UpdateRetainedHome();
        CHECK(s.guiRetainedHome == nullptr && managerObject.loads.empty() && commonObject.warnings.empty());
        CHECK(commonObject.developer.size() == 1 && commonObject.developer[0].find("not installed") != std::string::npos);
        s.ReportRetainedScreens(); CHECK(commonObject.output.find("views=0 stock=guis/menu/title.q4ui\n") != std::string::npos);
        s.mapSpawned = true; s.UpdateRetainedHome(); // the pause screen is installed and still presents
        CHECK(s.guiRetainedHome && std::string(s.guiRetainedHome->Name()) == "guis/menu/pause.q4ui");
        idUserInterface generic("guis/loading/generic.gui"); fileSystemObject.files.erase("guis/loading/loading.q4ui");
        CHECK(s.SelectRetainedLoadingGui(&generic, false) == &generic && commonObject.warnings.empty());
        CHECK(s.retainedStock.Num() == 2 && managerObject.loads.size() == 1);
    }
    {   // A main menu without every page the home screens hand off to keeps its own
        // home screen, as a mod's own menu would; the loading screens still present.
        auto s = Session(true); legacyMissing = {"main_b_credits"};
        s.PreloadRetainedScreens(); s.UpdateRetainedHome(); s.mapSpawned = true; s.UpdateRetainedHome();
        CHECK(s.guiRetainedHome == nullptr && managerObject.loads.empty() && commonObject.warnings.empty());
        CHECK((Stock(s) == std::vector<std::string>{"guis/menu/title.q4ui", "guis/menu/pause.q4ui"}));
        CHECK(commonObject.developer.size() == 2 && commonObject.developer[0].find("'main_b_credits'") != std::string::npos);
        idUserInterface generic("guis/loading/generic.gui");
        CHECK(s.SelectRetainedLoadingGui(&generic, false) != &generic);
    }
    {   // A view that failed to come back after a renderer restart hands the screen back for good.
        auto s = Session(true); s.UpdateRetainedHome(); auto* title = s.guiRetainedHome; CHECK(title && title->active);
        title->failed = true; s.UpdateRetainedHome();
        CHECK(s.guiRetainedHome == nullptr && !title->active && s.guiRetainedTitle == nullptr);
        CHECK(commonObject.warnings.size() == 1 && commonObject.warnings[0].find("stopped drawing") != std::string::npos);
        s.UpdateRetainedHome(); s.PreloadRetainedScreens();
        // Only the title loaded: the pages it reaches wait for a title, and the
        // pause screens and their pages for a level.
        CHECK(s.guiRetainedHome == nullptr && managerObject.loads.size() == 1 && commonObject.warnings.size() == 1);
    }
    {   // Back on the Single Player page the title's SINGLE PLAYER opened: the
        // legacy home button's choreography left the legacy menu off its home
        // (the hand-off's dest 37); its own return puts it back, unseen, and the
        // title takes the screen back at once with the bands coming home.
        auto s = Session(true); s.OpenCampaignSelector(false); auto* page = s.guiActive;
        CHECK(page && std::string(page->Name()) == "guis/menu/singleplayer.q4ui");
        legacy["desktop::curr"] = 0; legacy["desktop::active"] = 1; legacy["desktop::dest"] = 37;
        const size_t played = s.played.size();
        s.HandleRetainedSessionRequest(page, "campaignHome");
        CHECK(s.guiActive == s.guiMainMenu && legacy["desktop::curr"] == 0 && legacy["desktop::active"] == 0 && legacy["desktop::dest"] == 0);
        CHECK(std::find(s.guiMainMenu->named.begin(), s.guiMainMenu->named.end(), "returnFromSinglePlayer") != s.guiMainMenu->named.end());
        CHECK(s.guiRetainedHome != nullptr && s.guiRetainedHome == s.guiRetainedTitle && s.guiRetainedHome->active &&
              s.guiRetainedHome->named.back() == "returnHome" && !s.retainedPageReturn);
        CHECK(s.played.size() == played + 1 && s.played.back() == "play main_menu_selection" && s.menus == 0);
    }
    {   // The Single Player page's section summaries (section 13.7): CAMPAIGN
        // over the newest save's picture (CONTINUE's), ARENA over the match its
        // browser would open on, each read as a precache; a levelshot still
        // waiting for the idle frames is read there instead, once. Without a
        // save or a loaded campaign, neither shows a picture.
        auto s = Session(true); s.saves = {"quick", "autosave"}; arenaCampaign.levelshot = "gfx/guis/loadscreens/arena1";
        idSessionLocal::retainedLevelshot_t queued; queued.name = "gfx/guis/loadscreens/arena1"; queued.material = true;
        s.retainedLevelshots.Append(queued);
        s.OpenCampaignSelector(false); auto* page = s.guiActive;
        CHECK(page->state["summary.campaign.shot"] == "savegames/quick.tga" && page->state["summary.arena.shot"] == "gfx/guis/loadscreens/arena1");
        CHECK(std::count(precached.begin(), precached.end(), std::string("savegames/quick.tga")) == 1 &&
              std::count(precached.begin(), precached.end(), std::string("gfx/guis/loadscreens/arena1")) == 1);
        CHECK(latePrecaches == 0 && !declManagerObject.insideLoad && s.retainedLevelshots.Num() == 0 &&
              declManagerObject.materials.back() == "gfx/guis/loadscreens/arena1");
        auto fresh = Session(true); fresh.OpenCampaignSelector(false);
        CHECK(fresh.guiActive->state["summary.campaign.shot"] == "" && fresh.guiActive->state["summary.arena.shot"] == "" && precached.empty());
    }
    {   // The campaign selectors: the retained ones while installed, the stock ones otherwise.
        auto s = Session(true); s.OpenCampaignSelector(false);
        CHECK(s.guiActive && std::string(s.guiActive->Name()) == "guis/menu/singleplayer.q4ui" && arenaCampaign.selectors == 0);
        s.OpenCampaignSelector(true);
        CHECK(std::string(s.guiActive->Name()) == "guis/menu/campaigns.q4ui" && s.guiActive->state["awakening_ready"] == "1");
        // NEW GAME and CHAPTERS summarize themselves (section 13.7) over the
        // active campaign's first and furthest reached levels, read as precaches.
        CHECK(s.guiActive->state["summary.newgame.shot"] == "gfx/guis/loadscreens/e3_load" &&
              s.guiActive->state["summary.chapters.shot"] == "gfx/guis/loadscreens/airdefense" && latePrecaches == 0 &&
              std::count(precached.begin(), precached.end(), std::string("gfx/guis/loadscreens/airdefense")) == 1);
        CHECK(!managerObject.flags[0].unique && managerObject.flags[0].shared);
        auto missing = Session(true);
        fileSystemObject.files.erase("guis/menu/singleplayer.q4ui"); fileSystemObject.files.erase("guis/menu/campaigns.q4ui");
        missing.OpenCampaignSelector(false); CHECK(arenaCampaign.selectors == 1 && managerObject.loads.empty());
        missing.OpenCampaignSelector(true); CHECK(std::string(missing.guiActive->Name()) == "guis/campaign_menu.gui");
        missing.OpenCampaignSelector(false); CHECK(arenaCampaign.selectors == 2 && commonObject.warnings.empty());
        auto broken = Session(true); managerObject.fail = true; broken.OpenCampaignSelector(false);
        CHECK(arenaCampaign.selectors == 1 && commonObject.warnings.size() == 1);
        // The Single Player page, found each time it opens, whose view stopped
        // drawing: the stock selector, reported once.
        auto stopped = Session(true); stopped.OpenCampaignSelector(false); auto* drawn = stopped.guiActive;
        CHECK(drawn != nullptr && std::string(drawn->Name()) == "guis/menu/singleplayer.q4ui");
        drawn->failed = true; stopped.OpenCampaignSelector(false);
        CHECK(arenaCampaign.selectors == 1 && Stock(stopped) == std::vector<std::string>{"guis/menu/singleplayer.q4ui"} &&
              commonObject.warnings.size() == 1 && commonObject.warnings[0].find("stopped drawing") != std::string::npos);
        stopped.OpenCampaignSelector(false); CHECK(arenaCampaign.selectors == 2 && commonObject.warnings.size() == 1);
    }
    {   // Single player loading tips: the first when the screen presents, then the
        // next every 6 s in the other slot; multiplayer runs the arsenal instead.
        auto s = Session(true); idUserInterface loading("guis/loading/loading.q4ui"); s.guiLoading = &loading;
        s.PublishRetainedLoadingTip(true); CHECK(loading.named.empty());  // not presenting the load
        s.retainedLoadingActive = true; sysMilliseconds = 5;
        s.PublishRetainedLoadingTip(true);
        const std::string first = loading.state["loading_tip_a"];
        CHECK(loading.named.back() == "tipA" && first.rfind("#str_", 0) == 0 && loading.state["loading_tip_b"].empty());
        commonObject.time += 5999; s.PublishRetainedLoadingTip(false); CHECK(loading.named.size() == 1);
        commonObject.time += 1; s.PublishRetainedLoadingTip(false);
        const std::string second = loading.state["loading_tip_b"];
        CHECK(loading.named.back() == "tipB" && second.rfind("#str_", 0) == 0 && second != first && loading.state["loading_tip_a"] == first);
        commonObject.time += 6000; s.PublishRetainedLoadingTip(false);
        CHECK(loading.named.back() == "tipA" && loading.state["loading_tip_a"] != first && loading.state["loading_tip_a"] != second);
        sysMilliseconds = 6; s.PublishRetainedLoadingTip(true);  // a new load starts elsewhere in the list
        CHECK(loading.state["loading_tip_a"] != first && loading.state["loading_tip_b"].empty() && loading.named.back() == "tipA");
        loading.state["loading_mp"] = "1"; const size_t events = loading.named.size(); commonObject.time += 6000;
        s.PublishRetainedLoadingTip(false); s.PublishRetainedLoadingTip(true); CHECK(loading.named.size() == events);
    }
    {   // Fourth level: Campaign's choice and its two children, NEW GAME and
        // CHAPTERS; cancellation, draft settings and Start.
        auto s = Session(true); s.OpenCampaignSelector(true); auto* parent = s.guiActive;
        CHECK(parent->state["campaign.awakening"] == "0" && parent->state["from_chapters"] == "0");
        cvarSystemObject.SetCVarInteger("g_skill",1);
        s.HandleRetainedSessionRequest(parent,"campaignNew");
        CHECK(s.retainedDetailTarget == 1 && s.guiActive == parent && parent->named.back() == "detailEnterQuake4");
        sysEvent_t back; back.evType=SE_KEY; back.evValue=K_ESCAPE; back.evValue2=1;
        commonObject.time += 120; CHECK(s.RetainedSubpageEvent(&back));
        CHECK(s.retainedDetailTarget == 0 && s.guiActive == parent && parent->named.back() == "detailReverse150");
        // The chosen campaign decides NEW GAME's difficulty page.
        parent->state["campaign.awakening"] = "1"; s.HandleRetainedSessionRequest(parent,"campaignNew");
        CHECK(parent->named.back() == "detailEnterAwakening");
        commonObject.time += 349; s.UpdateRetainedSubpage(); CHECK(s.guiActive == parent);
        commonObject.time += 1; s.UpdateRetainedSubpage(); auto* skill = s.guiActive;
        CHECK(std::string(skill->Name()) == "guis/menu/campaign_skill.q4ui" && s.selected.back() == "awakening");
        s.HandleRetainedSessionRequest(skill,"campaignSkill4"); s.HandleRetainedSessionRequest(skill,"campaignSkillTurbo");
        CHECK(skill->State().GetInt("skill") == 4 && skill->State().GetBool("turbo") && cvarSystemObject.GetCVarInteger("g_skill") == 1);
        s.HandleRetainedSessionRequest(skill,"campaignSkillBack"); CHECK(s.retainedDetailTarget == 2);
        commonObject.time += 300; s.UpdateRetainedSubpage();
        CHECK(s.guiActive == parent && parent->state["from_chapters"] == "0" && cvarSystemObject.GetCVarInteger("g_skill") == 1);
        // CHAPTERS: the chosen campaign's levels, published as the page opens;
        // a row's pick publishes them again, and START CHAPTER starts the
        // chosen one at the current difficulty (the page has none of its own).
        const size_t published = s.chapterPublishes.size();
        s.HandleRetainedSessionRequest(parent,"campaignChapters");
        CHECK(s.retainedDetailTarget == 3 && parent->named.back() == "detailEnterChapters");
        commonObject.time += 350; s.UpdateRetainedSubpage(); auto* chapters = s.guiActive;
        CHECK(std::string(chapters->Name()) == "guis/menu/chapters.q4ui" && s.selected.back() == "quake4");
        CHECK(s.chapterPublishes.size() == published + 1 && s.chapterPublishes.back() == -1);
        chapters->state["chapter.pick"] = "6"; s.HandleRetainedSessionRequest(chapters,"campaignChapterSelect");
        CHECK(s.chapterPublishes.back() == 6);
        chapters->state["chapter.selected"] = "6"; s.HandleRetainedSessionRequest(chapters,"campaignChapterStart");
        CHECK(s.chapterStarts.size() == 1 && s.chapterStarts.back() == 6 && cvarSystemObject.GetCVarInteger("g_skill") == 1);
        // BACK climbs to Campaign onto CHAPTERS.
        s.HandleRetainedSessionRequest(chapters,"campaignChaptersBack");
        CHECK(s.retainedDetailTarget == 4 && chapters->named.back() == "detailLeave");
        commonObject.time += 300; s.UpdateRetainedSubpage(); CHECK(s.guiActive == parent && parent->state["from_chapters"] == "1");
        cvarSystemObject.strings["ui_retainedReducedMotion"]="1";
        s.HandleRetainedSessionRequest(parent,"campaignNew"); CHECK(s.guiActive == skill && !s.retainedSubpageFrom);
        CHECK(skill->State().GetInt("skill") == 1 && !skill->State().GetBool("turbo"));
        s.HandleRetainedSessionRequest(skill,"campaignSkill3"); s.HandleRetainedSessionRequest(skill,"campaignSkillTurbo");
        s.HandleRetainedSessionRequest(skill,"campaignSkillStart");
        CHECK(s.starts == 1 && cvarSystemObject.GetCVarInteger("g_skill") == 3 && cvarSystemObject.GetCVarBool("g_turboMode"));
        auto disabled = Session(false); CHECK(!disabled.OpenRetainedCampaignSkill() && !disabled.OpenRetainedCampaignChapters());
        auto missing = Session(true); fileSystemObject.files.erase("guis/menu/campaign_skill.q4ui");
        fileSystemObject.files.erase("guis/menu/chapters.q4ui");
        CHECK(!missing.OpenRetainedCampaignSkill() && !missing.OpenRetainedCampaignChapters());
    }
    {   // Single Player and its Campaign sub-page (spec 1.10): the leaving document
        // plays its half, the other presents at the hand-over (350 ms deeper, 300 ms
        // on Back), and input waits for it.
        auto s = Session(true); s.OpenCampaignSelector(false); auto* page = s.guiActive;
        s.HandleRetainedSessionRequest(page, "campaigns");
        CHECK(s.guiActive == page && page->named.back() == "subpageEnter" && s.retainedSubpageFrom == page && s.retainedSubpageDeeper);
        CHECK(s.retainedSubpageUntil == commonObject.time + 350);
        const size_t enterEvents = page->named.size();
        s.HandleRetainedSessionRequest(page, "campaignArena"); s.HandleRetainedSessionRequest(page, "campaigns");
        CHECK(s.guiActive == page && s.selected.empty() && page->named.size() == enterEvents);  // no second request mid-change
        // Input waits: a press is held, a release still reaches the page.
        sysEvent_t press; press.evType = SE_KEY; press.evValue = K_ENTER; press.evValue2 = 1;
        sysEvent_t release = press; release.evValue2 = 0;
        CHECK(s.RetainedSubpageEvent(&press) && page->events.empty());
        CHECK(s.RetainedSubpageEvent(&release) && page->events.size() == 1 && page->events[0].evValue2 == 0);
        commonObject.time += 349; s.UpdateRetainedSubpage(); CHECK(s.guiActive == page);
        commonObject.time += 1; s.UpdateRetainedSubpage(); auto* sub = s.guiActive;
        CHECK(sub && std::string(sub->Name()) == "guis/menu/campaigns.q4ui" && s.retainedSubpageFrom == nullptr);
        CHECK(!s.RetainedSubpageEvent(&press));  // the sub-page takes input again
        s.HandleRetainedSessionRequest(sub, "campaignBack");
        CHECK(s.guiActive == sub && sub->named.back() == "subpageLeave" && !s.retainedSubpageDeeper && s.retainedSubpageUntil == commonObject.time + 300);
        sysEvent_t back = press; back.evValue = K_ESCAPE;
        CHECK(s.RetainedSubpageEvent(&back) && s.retainedSubpageFrom == sub);  // Back does not reverse a Back
        commonObject.time += 300; s.UpdateRetainedSubpage();
        CHECK(s.guiActive == page && page->named.back() == "subpageReturn");
        // Back while going deeper reverses the change from where it stands, by
        // the distance the band has stepped since 50 ms: 150 ms at least ...
        s.HandleRetainedSessionRequest(page, "campaigns"); commonObject.time += 120;
        CHECK(s.RetainedSubpageEvent(&back) && page->named.back() == "subpageReverse150" && s.retainedSubpageFrom == nullptr && s.guiActive == page);
        s.UpdateRetainedSubpage(); CHECK(s.guiActive == page);
        // ... and 250 ms once it has stepped most of the way.
        s.HandleRetainedSessionRequest(page, "campaigns"); commonObject.time += 280;
        sysEvent_t pad = back; pad.evValue = K_JOY4;
        CHECK(s.RetainedSubpageEvent(&pad) && page->named.back() == "subpageReverse250");
        // With confirm on east (Swap Confirm and Back), east does not go back
        // and south does.
        idKeyInput::swapped = true;
        s.HandleRetainedSessionRequest(page, "campaigns"); commonObject.time += 280;
        const auto named = page->named.size();
        CHECK(s.RetainedSubpageEvent(&pad) && page->named.size() == named && s.retainedSubpageFrom != nullptr);
        sysEvent_t south = back; south.evValue = K_JOY3;
        CHECK(s.RetainedSubpageEvent(&south) && page->named.back() == "subpageReverse250" && s.retainedSubpageFrom == nullptr);
        idKeyInput::swapped = false;
        // Reduced motion hands over at once: the arrival's 80 ms fade is the change.
        cvarSystemObject.strings["ui_retainedReducedMotion"] = "1";
        s.HandleRetainedSessionRequest(page, "campaigns");
        CHECK(std::string(s.guiActive->Name()) == "guis/menu/campaigns.q4ui" && s.retainedSubpageFrom == nullptr && page->named.back() != "subpageEnter");
        s.HandleRetainedSessionRequest(s.guiActive, "campaignBack");
        CHECK(s.guiActive == page && page->named.back() == "subpageReturn" && s.retainedSubpageFrom == nullptr);
        cvarSystemObject.strings["ui_retainedReducedMotion"] = "0";
        // Anything else taking the screen abandons the change.
        s.HandleRetainedSessionRequest(page, "campaigns");
        idUserInterface other("guis/msg.gui"); s.guiActive = &other; s.UpdateRetainedSubpage();
        CHECK(s.retainedSubpageFrom == nullptr && s.guiActive == &other);
        // A sub-page that cannot present: its stock selector opens at once.
        auto stock = Session(true); stock.OpenCampaignSelector(false); auto* alone = stock.guiActive;
        fileSystemObject.files.erase("guis/menu/campaigns.q4ui");
        stock.HandleRetainedSessionRequest(alone, "campaigns");
        CHECK(std::string(stock.guiActive->Name()) == "guis/campaign_menu.gui" && stock.retainedSubpageFrom == nullptr && alone->named.empty());
    }
    {   // Single Player's Arena sub-page (FLOW-011, FLOW-050): ARENA plays the change
        // with its own carry and the campaign's browser presents at 350 ms; the
        // page's verbs run the campaign's own commands; Back plays the page's half
        // and Single Player returns at 300 ms onto ARENA.
        // In the campaign's game directory and module: no reload first.
        const auto home = [] { cvarSystemObject.strings["fs_game"] = "baseoq4"; cvarSystemObject.strings["com_activeGameModule"] = "game_sp"; };
        auto s = Session(true); home(); s.OpenCampaignSelector(false); auto* page = s.guiActive;
        CHECK(page->state["from_arena"] == "0");
        s.HandleRetainedSessionRequest(page, "campaignArena");
        CHECK(s.guiActive == page && page->named.back() == "subpageEnterArena" && s.retainedSubpageFrom == page &&
              s.retainedSubpageDeeper && s.retainedSubpageKind == RETAINED_SUBPAGE_ARENA && s.retainedSubpageUntil == commonObject.time + 350 &&
              s.selected.empty());
        commonObject.time += 349; s.UpdateRetainedSubpage(); CHECK(arenaCampaign.browsers == 0);
        commonObject.time += 1; s.UpdateRetainedSubpage();
        CHECK(arenaCampaign.browsers == 1 && s.retainedSubpageFrom == nullptr && s.retainedSubpageKind != RETAINED_SUBPAGE_ARENA &&
              commonObject.output.find("CAMPAIGN_SELECTED id=arena gameDir=baseoq4") != std::string::npos);
        // The campaign presents the page (idArenaCampaign::OpenBrowser).
        auto* arena = s.RetainedArenaPage();
        CHECK(arena && std::string(arena->Name()) == "guis/menu/arena.q4ui" && s.RetainedArenaGui() == arena);
        s.guiActive = arena;
        const std::pair<const char*, const char*> verbs[] = {{"arenaTier2", "arenaSelectTier 2"}, {"arenaMatch3", "arenaSelectMatch 3"},
            {"arenaTierNext", "arenaStepTier 1"}, {"arenaTierPrevious", "arenaStepTier -1"}, {"arenaEasier", "arenaDifficulty -1"},
            {"arenaHarder", "arenaDifficulty 1"}, {"arenaStart", "arenaStart"}, {"arenaReset", "arenaReset"},
            {"arenaResetConfirm", "arenaResetConfirm"}, {"arenaResetCancel", "arenaResetCancel"}, {"arenaRetry", "arenaRetry"},
            {"arenaDismiss", "arenaDismissResult"}};
        for (const auto& [verb, command] : verbs) {
            const size_t played = s.played.size();
            s.HandleRetainedSessionRequest(arena, verb);
            CHECK(arenaCampaign.commands.back() == command && s.played.size() == played + 1);
        }
        // The entrance holds the page: nothing reaches the campaign, not even Back.
        arenaCampaign.entering = true; const size_t held = arenaCampaign.commands.size();
        s.HandleRetainedSessionRequest(arena, "arenaStart"); s.HandleRetainedSessionRequest(arena, "arenaBack");
        CHECK(arenaCampaign.commands.size() == held && s.retainedSubpageFrom == nullptr && s.guiActive == arena);
        arenaCampaign.entering = false;
        // Only while it presents.
        idUserInterface other("guis/msg.gui"); s.guiActive = &other;
        s.HandleRetainedSessionRequest(arena, "arenaStart"); CHECK(arenaCampaign.commands.size() == held);
        s.guiActive = arena;
        s.HandleRetainedSessionRequest(arena, "arenaBack");
        CHECK(arena->named.back() == "subpageLeave" && s.retainedSubpageFrom == arena && !s.retainedSubpageDeeper && s.retainedSubpageKind == RETAINED_SUBPAGE_ARENA &&
              s.retainedSubpageUntil == commonObject.time + 300);
        s.HandleRetainedSessionRequest(arena, "arenaTier0"); CHECK(arenaCampaign.commands.size() == held);  // none mid-change
        commonObject.time += 300; s.UpdateRetainedSubpage();
        CHECK(s.guiActive == page && page->state["from_arena"] == "1" && page->named.back() == "subpageReturn" && s.retainedSubpageKind != RETAINED_SUBPAGE_ARENA);
        s.HandleRetainedSessionRequest(page, "campaigns"); commonObject.time += 350; s.UpdateRetainedSubpage();
        s.OpenCampaignSelector(false); CHECK(page->state["from_arena"] == "0");  // any other return lands on the first row
        // Back while going deeper reverses the change, as Campaign's does.
        s.HandleRetainedSessionRequest(page, "campaignArena"); commonObject.time += 120;
        sysEvent_t back; back.evType = SE_KEY; back.evValue = K_ESCAPE; back.evValue2 = 1;
        CHECK(s.RetainedSubpageEvent(&back) && page->named.back() == "subpageReverse150" && s.retainedSubpageFrom == nullptr);
        CHECK(arenaCampaign.browsers == 1);
        // Reduced motion hands over at once.
        cvarSystemObject.strings["ui_retainedReducedMotion"] = "1";
        s.HandleRetainedSessionRequest(page, "campaignArena");
        CHECK(arenaCampaign.browsers == 2 && s.retainedSubpageFrom == nullptr && page->named.back() != "subpageEnterArena");
        s.guiActive = arena; s.HandleRetainedSessionRequest(arena, "arenaBack");
        CHECK(s.guiActive == page && page->state["from_arena"] == "1" && s.retainedSubpageFrom == nullptr);
        cvarSystemObject.strings["ui_retainedReducedMotion"] = "0";
        // A campaign that must reload into its game directory first is selected
        // as before; the browser presents after the reload.
        auto reload = Session(true); home(); reload.OpenCampaignSelector(false); auto* first = reload.guiActive;
        cvarSystemObject.strings["com_activeGameModule"] = "game_mp";
        reload.HandleRetainedSessionRequest(first, "campaignArena");
        CHECK((reload.selected == std::vector<std::string>{"arena"}) && reload.retainedSubpageFrom == nullptr && first->named.empty());
        // Not installed, cannot load, stopped drawing, or the gate off: the
        // stock browser, selected as before.
        auto missing = Session(true); home(); fileSystemObject.files.erase("guis/menu/arena.q4ui");
        missing.OpenCampaignSelector(false); auto* alone = missing.guiActive;
        missing.HandleRetainedSessionRequest(alone, "campaignArena");
        CHECK((missing.selected == std::vector<std::string>{"arena"}) && missing.RetainedArenaPage() == nullptr && commonObject.warnings.empty());
        auto failed = Session(true); home();
        auto* view = failed.RetainedArenaPage(); CHECK(view != nullptr); view->failed = true;
        CHECK(failed.RetainedArenaPage() == nullptr && failed.RetainedArenaGui() == nullptr && commonObject.warnings.size() == 1 &&
              Stock(failed) == std::vector<std::string>{"guis/menu/arena.q4ui"});
        auto off = Session(false); CHECK(off.RetainedArenaPage() == nullptr && managerObject.loads.empty());
    }
    {   // The gate alone includes the SYSTEM page only once it lacks no stock setting and
        // each one works there; ui_retainedSystem opts into it until it falls back.
        const bool complete = RETAINED_SYSTEM_MISSING_SETTINGS[0] == NULL && RETAINED_SYSTEM_INCOMPLETE_SETTINGS[0] == NULL;
        auto s = Session(true); CHECK(s.RetainedSystemAvailable() == complete);
        s.ReportRetainedScreens(); CHECK(commonObject.output.find(complete ? "OPENQ4_RETAINED enabled=1 system=1 multiplayer=" : "OPENQ4_RETAINED enabled=1 system=0 multiplayer=") != std::string::npos);
        auto off = Session(false); CHECK(!off.RetainedSystemAvailable());
        ui_retainedSystem.value = true; CHECK(s.RetainedSystemAvailable() && off.RetainedSystemAvailable());
        s.retainedStock.Append("guis/menu/settings/system.q4ui"); CHECK(!s.RetainedSystemAvailable());
    }
    {   // The AUDIO button opens the retained page over the settings screen,
        // its values read back: the volumes and the subtitle size and
        // background as whole percents, the device as its row in the sound
        // system's list. Each verb applies at once, the device rows through
        // the stock page's commands; BACK returns to the screen and handler
        // that opened it.
        auto s = Session(true); CHECK(s.RetainedAudioAvailable());
        // The sound system's list, its items quoted with backslash escapes.
        audioDeviceNames = "\"Default device (Realtek)\";\"Speakers\";\"Headset \\\"Pro\\\"\"";
        audioDeviceValues = "\"__OPENQ4_DEFAULT_AUDIO_DEVICE__\";\"Speakers (Realtek)\";\"Headset \\\"Pro\\\";USB\"";
        for (const auto& [cvar, value] : std::map<std::string, std::string>{{"s_volume", "0.7"}, {"s_musicVolume", "0.4"},
                 {"s_dialogueVolume", "1"}, {"s_useOpenAL", "1"}, {"s_deviceName", "Headset \"Pro\";USB"}, {"s_useEAXReverb", "1"},
                 {"s_numberOfSpeakers", "6"}, {"s_openALHRTF", "2"}, {"ui_subtitles", "1"}, {"ui_subtitleScale", "1.5"},
                 {"ui_subtitleBackground", "0.3"}, {"ui_subtitleSpeakers", "0"}}) cvarSystemObject.strings[cvar] = value;
        auto* settings = s.guiMainMenu; s.guiHandle = reinterpret_cast<HandleGuiCommand_t>(&audioDeviceNames);
        s.OpenRetainedAudioRoute();
        auto* page = s.guiActive;
        CHECK(page != settings && page == s.guiRetainedAudio && std::string(page->Name()) == "guis/menu/settings/audio.q4ui" &&
              s.retainedAudioParent == settings && s.guiHandle == nullptr && legacyActions.empty());
        CHECK(page->state["audio.volume"] == "70" && page->state["audio.music"] == "40" && page->state["audio.dialogue"] == "100" &&
              page->state["audio.backend"] == "1" && page->state["audio.device.count"] == "3" && page->state["audio.device"] == "2" &&
              page->state["audio.device.1.label"] == "Speakers" && page->state["audio.device.2.label"] == "Headset \"Pro\"" &&
              page->state["audio.device.3.label"] == "" &&
              page->state["audio.eax"] == "1" && page->state["audio.speakers"] == "2" && page->state["audio.hrtf"] == "2" &&
              page->state["audio.subtitles"] == "1" && page->state["audio.subtitleSize"] == "150" &&
              page->state["audio.subtitleBackground"] == "30" && page->state["audio.subtitleSpeakers"] == "0");
        s.played.clear();
        s.HandleRetainedSessionRequest(page, "audioVolume 80");
        CHECK(cvarSystemObject.strings["s_volume"] == "0.8" && page->state["audio.volume"] == "80" &&
              (s.played == std::vector<std::string>{"play main_menu_selection"}));
        s.HandleRetainedSessionRequest(page, "audioDevice 1");
        CHECK(cvarSystemObject.strings["s_deviceName"] == "Speakers (Realtek)" && s.played.back() == "sound drivar" && page->state["audio.device"] == "1");
        s.HandleRetainedSessionRequest(page, "audioDevice 0");
        CHECK(cvarSystemObject.strings["s_deviceName"] == "__OPENQ4_DEFAULT_AUDIO_DEVICE__" && page->state["audio.device"] == "0");
        s.played.clear(); s.HandleRetainedSessionRequest(page, "audioDevice 3"); s.HandleRetainedSessionRequest(page, "audioDevice -1");
        CHECK(cvarSystemObject.strings["s_deviceName"] == "__OPENQ4_DEFAULT_AUDIO_DEVICE__" && (s.played == std::vector<std::string>{"play main_menu_selection", "play main_menu_selection"}));
        s.HandleRetainedSessionRequest(page, "audioEax 0");
        CHECK(cvarSystemObject.strings["s_useEAXReverb"] == "0" && s.played.back() == "sound eax" && page->state["audio.eax"] == "0");
        s.HandleRetainedSessionRequest(page, "audioSpeakers 1");
        CHECK(cvarSystemObject.strings["s_numberOfSpeakers"] == "2" && s.played.back() == "sound speakers" && page->state["audio.speakers"] == "1");
        s.HandleRetainedSessionRequest(page, "audioBackend 0");
        CHECK(cvarSystemObject.strings["s_useOpenAL"] == "0" && s.played.back() == "sound drivar" && page->state["audio.backend"] == "0");
        s.HandleRetainedSessionRequest(page, "audioHrtf 1"); CHECK(cvarSystemObject.strings["s_openALHRTF"] == "1" && s.played.back() == "sound drivar");
        s.HandleRetainedSessionRequest(page, "audioSubtitleSize 250"); CHECK(cvarSystemObject.strings["ui_subtitleScale"] == "2");
        s.HandleRetainedSessionRequest(page, "audioSubtitleBackground 50"); CHECK(cvarSystemObject.strings["ui_subtitleBackground"] == "0.5");
        s.HandleRetainedSessionRequest(page, "audioSubtitles 0"); s.HandleRetainedSessionRequest(page, "audioSubtitleSpeakers 1");
        CHECK(cvarSystemObject.strings["ui_subtitles"] == "0" && cvarSystemObject.strings["ui_subtitleSpeakers"] == "1" &&
              page->state["audio.subtitles"] == "0" && page->state["audio.subtitleSpeakers"] == "1");
        // A malformed request, or the page's verbs from another screen, does nothing.
        s.played.clear(); s.HandleRetainedSessionRequest(page, "audioVolume"); s.HandleRetainedSessionRequest(page, "audioVolume x");
        s.HandleRetainedSessionRequest(page, "audioLoudness 5"); CHECK(s.played.empty() && cvarSystemObject.strings["s_volume"] == "0.8");
        s.HandleRetainedSessionRequest(page, "audioBack");
        CHECK(s.guiActive == settings && s.guiHandle == reinterpret_cast<HandleGuiCommand_t>(&audioDeviceNames) && s.retainedAudioParent == nullptr);
        s.HandleRetainedSessionRequest(page, "audioVolume 10"); CHECK(cvarSystemObject.strings["s_volume"] == "0.8");
        // The page reopens on the screen's next press; one that stopped
        // drawing, or a document that is missing, opens the stock page, and
        // the AUDIO button keeps doing so.
        s.OpenRetainedAudioRoute(); CHECK(s.guiActive == page); s.HandleRetainedSessionRequest(page, "audioBack");
        page->failed = true; s.OpenRetainedAudioRoute();
        CHECK(s.guiActive == settings && s.guiRetainedAudio == nullptr && (legacyActions == std::vector<std::string>{"set_b_audio"}) &&
              s.dispatched.back() == "play main_menu_selection" && !s.RetainedAudioAvailable() &&
              Stock(s) == std::vector<std::string>{"guis/menu/settings/audio.q4ui"} && settings->state["retainedAudio"] == "0");
        auto missing = Session(true); fileSystemObject.files.erase("guis/menu/settings/audio.q4ui");
        missing.OpenRetainedAudioRoute();
        CHECK(missing.guiActive == missing.guiMainMenu && missing.guiRetainedAudio == nullptr && !missing.RetainedAudioAvailable() &&
              legacyActions.size() == 1);
        // Without the retained screens, or over a message box, the page stays shut.
        auto off = Session(false); CHECK(!off.RetainedAudioAvailable() && !off.OpenRetainedAudio());
        auto boxed = Session(true); idUserInterface box("guis/msg.gui"); boxed.guiMsgRestore = &box; CHECK(!boxed.OpenRetainedAudio());
        cvarSystemObject = CVarSystem{}; audioDeviceNames.clear(); audioDeviceValues.clear();
    }
    {   // The GAME OPTIONS button opens the retained page over the settings
        // screen, its computed rows read back; the page's verbs reach the
        // session only while it presents; BACK returns to the screen and
        // handler that opened it; a page that stopped drawing or is missing
        // opens the stock page.
        auto s = Session(true); CHECK(s.RetainedGameAvailable());
        auto* settings = s.guiMainMenu; s.guiHandle = reinterpret_cast<HandleGuiCommand_t>(&audioDeviceNames);
        s.OpenRetainedGameRoute();
        auto* page = s.guiActive;
        CHECK(page != settings && page == s.guiRetainedGame && std::string(page->Name()) == "guis/menu/settings/game.q4ui" &&
              s.retainedGameParent == settings && s.guiHandle == nullptr && legacyActions.empty() &&
              (s.gamePublishes == std::vector<idUserInterface*>{page}));
        s.HandleRetainedSessionRequest(page, "gameLanguage 2");
        CHECK((s.gameRequests == std::vector<std::string>{"gameLanguage 2"}));
        s.HandleRetainedSessionRequest(settings, "gameLanguage 3"); CHECK(s.gameRequests.size() == 1);
        s.ReturnRetainedGame();
        CHECK(s.guiActive == settings && s.guiHandle == reinterpret_cast<HandleGuiCommand_t>(&audioDeviceNames) && s.retainedGameParent == nullptr);
        s.HandleRetainedSessionRequest(page, "gameLanguage 4"); CHECK(s.gameRequests.size() == 1);
        s.OpenRetainedGameRoute(); CHECK(s.guiActive == page && s.gamePublishes.size() == 2); s.ReturnRetainedGame();
        page->failed = true; s.OpenRetainedGameRoute();
        CHECK(s.guiActive == settings && s.guiRetainedGame == nullptr && (legacyActions == std::vector<std::string>{"set_b_gameoptions"}) &&
              !s.RetainedGameAvailable() && Stock(s) == std::vector<std::string>{"guis/menu/settings/game.q4ui"} &&
              settings->state["retainedGame"] == "0");
        auto missing = Session(true); fileSystemObject.files.erase("guis/menu/settings/game.q4ui");
        missing.OpenRetainedGameRoute();
        CHECK(missing.guiActive == missing.guiMainMenu && missing.guiRetainedGame == nullptr && !missing.RetainedGameAvailable() &&
              legacyActions.size() == 1);
        auto off = Session(false); CHECK(!off.RetainedGameAvailable() && !off.OpenRetainedGame());
        // The multiplayer menu's route opens the page the same way; where it
        // cannot, the main menu's own stock event shows the stock page, the
        // button keeping the retained page while it is available.
        auto mp = Session(true); mp.OpenRetainedGameRoute(true);
        CHECK(mp.guiActive == mp.guiRetainedGame && mp.guiRetainedGame != nullptr && mp.retainedGameParent == mp.guiMainMenu);
        mp.ReturnRetainedGame(); mp.guiRetainedGame->failed = true; mp.OpenRetainedGameRoute(true);
        CHECK(mp.guiActive == mp.guiMainMenu && legacyActions.empty() && mp.guiMainMenu->named.back() == "fromMp_toGameoptionsStock" &&
              mp.guiMainMenu->state["retainedGame"] == "0");
        auto boxed = Session(true); idUserInterface box("guis/msg.gui"); boxed.guiMsgRestore = &box; boxed.OpenRetainedGameRoute(true);
        CHECK(boxed.guiMainMenu->named.back() == "fromMp_toGameoptionsStock" && boxed.guiMainMenu->state["retainedGame"] != "0");
    }
    {   // The SETTINGS page: SETTINGS hands off to it from the title, which
        // carries its label; CONTROLS, GAME OPTIONS and AUDIO go one level
        // deeper to their retained sub-pages, the page playing its half and
        // the sub-page presenting over it at 350 ms, and each sub-page's Back
        // plays its half and returns the page onto its section at 300 ms;
        // DISPLAY,
        // with the SYSTEM page off, sends it away and at 500 ms the stock
        // settings screen arrives on its panel (openSettingsPage), whose Back
        // returns the page (settingsOpen); LOAD DEFAULTS' YES runs the stock
        // dialog's commands; BACK returns the title, from the page dock after
        // the stock screen.
        auto s = Session(true); s.PreloadRetainedScreens(); s.UpdateRetainedHome();
        auto* title = s.guiRetainedHome;
        s.HandleRetainedSessionRequest(title, "settings");
        auto* page = s.guiRetainedSettingsPage;
        CHECK(page && std::string(page->Name()) == "guis/menu/settings.q4ui" && legacyActions.empty() && s.retainedPagePending == page &&
              title->named.back() == "depart" && title->named[title->named.size() - 2] == "carry_settings" &&
              page->state["from_controls"] == "0" && page->state["from_audio"] == "0");
        commonObject.time += 550; s.UpdateRetainedHome(); s.UpdateRetainedPage();
        CHECK(s.guiActive == page && s.RetainedPageActive() && s.retainedPageHome == title);
        s.HandleRetainedSessionRequest(page, "settingsControls");
        CHECK(s.guiActive == page && page->named.back() == "subpageEnterControls" && s.retainedSubpageFrom == page &&
              s.retainedSubpageKind == RETAINED_SUBPAGE_SETTINGS_CONTROLS && s.retainedSubpageUntil == commonObject.time + 350 &&
              page->state["from_controls"] == "1" && page->state["from_game"] == "0");
        s.HandleRetainedSessionRequest(page, "settingsGame"); CHECK(s.retainedSubpageKind == RETAINED_SUBPAGE_SETTINGS_CONTROLS);  // nothing else meanwhile
        commonObject.time += 350; s.UpdateRetainedSubpage();
        CHECK(s.guiActive == s.guiRetainedControls && s.guiActive != nullptr && s.retainedControlsParent == page && s.retainedSubpageFrom == nullptr);
        auto* controls = s.guiActive;
        s.ReturnRetainedControls();
        CHECK(s.guiActive == controls && !controls->named.empty() && controls->named.back() == "subpageLeave" &&
              s.retainedSubpageFrom == controls);
        commonObject.time += 300; s.UpdateRetainedSubpage();
        CHECK(s.guiActive == page && page->named.back() == "subpageReturn" && s.retainedControlsParent == nullptr &&
              page->state["from_controls"] == "1");
        s.HandleRetainedSessionRequest(page, "settingsGame");
        CHECK(page->named.back() == "subpageEnterGame" && page->state["from_game"] == "1" && page->state["from_controls"] == "0");
        commonObject.time += 350; s.UpdateRetainedSubpage();
        CHECK(s.guiActive == s.guiRetainedGame && s.retainedGameParent == page);
        s.ReturnRetainedGame();
        CHECK(s.guiActive == s.guiRetainedGame && !s.guiActive->named.empty() && s.guiActive->named.back() == "subpageLeave");
        commonObject.time += 300; s.UpdateRetainedSubpage(); CHECK(s.guiActive == page && s.retainedGameParent == nullptr);
        s.HandleRetainedSessionRequest(page, "settingsAudio");
        CHECK(page->named.back() == "subpageEnterAudio" && page->state["from_audio"] == "1");
        commonObject.time += 350; s.UpdateRetainedSubpage();
        CHECK(s.guiActive == s.guiRetainedAudio && s.retainedAudioParent == page);
        s.ReturnRetainedAudio(); commonObject.time += 300; s.UpdateRetainedSubpage();
        CHECK(s.guiActive == page && page->named.back() == "subpageReturn" && s.retainedAudioParent == nullptr);
        const size_t sounds = s.played.size();
        // DISPLAY where the SYSTEM page fell back to its stock screen: the
        // page leaves for the stock panel (dest 22).
        s.retainedStock.Append("guis/menu/settings/system.q4ui");
        s.HandleRetainedSessionRequest(page, "settingsDisplay");
        CHECK(s.systemOpens == 0 && page->named.back() == "depart" && s.retainedSettingsDest == 22 && page->state["from_display"] == "1" &&
              s.played.size() == sounds + 1);
        s.HandleRetainedSessionRequest(page, "settingsControls"); s.HandleRetainedSessionRequest(page, "settingsBack");
        CHECK(s.guiActive == page && s.retainedSettingsDest == 22 && s.played.size() == sounds + 1);
        commonObject.time += 499; s.UpdateRetainedSettingsPage(); CHECK(s.guiActive == page && timeRuns.empty());
        commonObject.time += 1; s.UpdateRetainedSettingsPage();
        CHECK(s.guiActive == s.guiMainMenu && s.retainedSettingsDest == 0 && s.retainedPageHome == nullptr && s.retainedSettingsLeft == 22);
        CHECK(legacy["desktop::curr"] == 9 && legacy["desktop::active"] == 1 && legacy["desktop::dest"] == 22 && expressionWrites == 0 &&
              s.guiMainMenu->named.back() == "openSettingsPage" && s.guiMainMenu->state["settingsFromRetained"] == "1" &&
              (timeRuns == std::vector<std::string>{"guis/mainmenu.gui"}));
        s.UpdateRetainedHome(); CHECK(s.guiRetainedHome == nullptr);  // the stock screen presents alone
        // Its Back (settingsOpen names the panel): the page returns onto DISPLAY.
        CHECK(s.OpenRetainedSettingsPage("display") && s.guiActive == page && page->state["from_display"] == "1" &&
              page->state["from_controls"] == "0" && s.guiMainMenu->state["settingsFromRetained"] == "0" && s.retainedPageHome == nullptr);
        // LOAD DEFAULTS' YES: the stock dialog's commands.
        s.HandleRetainedSessionRequest(page, "settingsDefaults");
        CHECK(s.played.back() == "exec cvar_restart ; play main_menu_selection ; resetdefaults ; exec s_restart" && s.guiActive == page);
        // BACK after the stock screen: the legacy menu goes home as that
        // screen's Back does (leaveSettingsPage clears desktop::dest) and the
        // title follows it from the page dock.
        s.guiMainMenu->onNamed = [](const char* name) { if (!std::strcmp(name, "leaveSettingsPage")) legacy["desktop::dest"] = 0; };
        s.HandleRetainedSessionRequest(page, "settingsBack");
        CHECK(s.guiActive == s.guiMainMenu && s.guiMainMenu->named.back() == "leaveSettingsPage" && legacy["desktop::curr"] == 9 &&
              legacy["desktop::active"] == 1 && s.guiMainMenu->state["settingsFromRetained"] == "0" && s.guiRetainedHome == title &&
              title->named.back() == "returnHome" && expressionWrites == 0);
        // BACK before any stock screen: the title returns as from any page.
        auto fresh = Session(true); fresh.PreloadRetainedScreens(); fresh.UpdateRetainedHome(); auto* home = fresh.guiRetainedHome;
        fresh.HandleRetainedSessionRequest(home, "settings"); commonObject.time += 550; fresh.UpdateRetainedHome(); fresh.UpdateRetainedPage();
        fresh.HandleRetainedSessionRequest(fresh.guiRetainedSettingsPage, "settingsBack");
        CHECK(fresh.guiActive == fresh.guiMainMenu && home->named.back() == "returnHome");
        // SETTINGS is shared by both pause families. It must never reuse the
        // Strogg Load Game slot or publish save-browser state into that page.
        auto strogg = Session(true, true);
        gameObject.publish = [](idUserInterface* gui) { gui->SetStateBool("pause_strogg", true); };
        strogg.PreloadRetainedScreens(); strogg.UpdateRetainedHome();
        auto* stroggHome = strogg.guiRetainedHome;
        strogg.HandleRetainedSessionRequest(stroggHome, "settings");
        CHECK(strogg.retainedPagePending == strogg.guiRetainedSettingsPage && strogg.guiRetainedSettingsPage != nullptr &&
              std::string(strogg.retainedPagePending->Name()) == "guis/menu/settings.q4ui" &&
              strogg.guiRetainedSettingsPage != strogg.guiRetainedLoadStrogg &&
              strogg.guiRetainedSettingsPage->state.count("load.count") == 0);
        commonObject.time += 550; strogg.UpdateRetainedHome(); strogg.UpdateRetainedPage();
        CHECK(strogg.guiActive == strogg.guiRetainedSettingsPage && strogg.retainedPageHome == stroggHome);
        strogg.HandleRetainedSessionRequest(strogg.guiRetainedSettingsPage, "settingsBack");
        CHECK(strogg.guiActive == strogg.guiMainMenu && strogg.guiRetainedHome == stroggHome &&
              stroggHome->named.back() == "returnHome");
        // A page that cannot open its section leaves for the stock panel: a
        // CONTROLS page that stopped drawing hands off to its panel (dest 4).
        auto broken = Session(true); broken.PreloadRetainedScreens(); broken.UpdateRetainedHome();
        broken.HandleRetainedSessionRequest(broken.guiRetainedHome, "settings"); commonObject.time += 550;
        broken.UpdateRetainedHome(); broken.UpdateRetainedPage();
        broken.HandleRetainedSessionRequest(broken.guiRetainedSettingsPage, "settingsControls"); commonObject.time += 350;
        broken.UpdateRetainedSubpage(); broken.ReturnRetainedControls(); commonObject.time += 300; broken.UpdateRetainedSubpage();
        CHECK(broken.guiActive == broken.guiRetainedSettingsPage && broken.guiRetainedControls != nullptr);
        broken.guiRetainedControls->failed = true;
        broken.HandleRetainedSessionRequest(broken.guiRetainedSettingsPage, "settingsControls"); commonObject.time += 350;
        broken.UpdateRetainedSubpage();
        CHECK(broken.retainedSettingsDest == 4 && broken.guiActive == broken.guiRetainedSettingsPage);
        // Where the page cannot present, SETTINGS runs the stock button: the
        // gate off, or a main menu without the route (quietly).
        auto off = Session(false); CHECK(off.RetainedSettingsPage() == nullptr && !off.OpenRetainedSettingsPage());
        auto missingSettings = Session(true); fileSystemObject.files.erase("guis/menu/settings.q4ui");
        missingSettings.PreloadRetainedScreens(); missingSettings.UpdateRetainedHome();
        missingSettings.HandleRetainedSessionRequest(missingSettings.guiRetainedHome, "settings");
        CHECK(missingSettings.retainedPagePending == nullptr && legacyActions.back() == "main_b_settings" &&
              commonObject.warnings.empty());
        auto old = Session(true); legacyEventsMissing = {"openSettingsPage"}; old.PreloadRetainedScreens(); old.UpdateRetainedHome();
        old.HandleRetainedSessionRequest(old.guiRetainedHome, "settings");
        CHECK(legacyActions.back() == "main_b_settings" && old.retainedPagePending == nullptr && commonObject.warnings.empty() &&
              (Stock(old) == std::vector<std::string>{"guis/menu/settings.q4ui"}));
        legacyEventsMissing.clear();
    }
    {   // DISPLAY with the SYSTEM page on goes one level deeper to it as the
        // other sections do, without the shared document probe (the page loads
        // as it opens): the page plays its half and the SYSTEM page presents
        // over it at 350 ms; Back while going deeper reverses. The SYSTEM
        // page's Back plays its half, once its draft lets it leave, and at
        // 300 ms the page returns onto DISPLAY, closing it. Where the SYSTEM
        // page is off, or cannot open, the stock panel stands in, and the
        // SYSTEM page opened over the stock settings screen returns there at once.
        auto s = Session(true); ui_retainedSystem.value = true; s.systemOpensPage = true;
        s.PreloadRetainedScreens(); s.UpdateRetainedHome();
        s.HandleRetainedSessionRequest(s.guiRetainedHome, "settings"); commonObject.time += 550; s.UpdateRetainedHome(); s.UpdateRetainedPage();
        auto* page = s.guiRetainedSettingsPage;
        CHECK(page != nullptr && s.guiActive == page && s.RetainedSystemAvailable());
        const size_t loads = managerObject.loads.size(), sounds = s.played.size();
        s.HandleRetainedSessionRequest(page, "settingsDisplay");
        CHECK(s.guiActive == page && page->named.back() == "subpageEnterDisplay" && s.retainedSubpageFrom == page && s.retainedSubpageDeeper &&
              s.retainedSubpageKind == RETAINED_SUBPAGE_SETTINGS_SYSTEM && s.retainedSubpageUntil == commonObject.time + 350 &&
              page->state["from_display"] == "1" && page->state["from_audio"] == "0" && s.systemOpens == 0 && s.retainedSettingsDest == 0 &&
              managerObject.loads.size() == loads && s.played.size() == sounds + 1);
        s.HandleRetainedSessionRequest(page, "settingsGame"); CHECK(s.retainedSubpageKind == RETAINED_SUBPAGE_SETTINGS_SYSTEM);  // nothing else meanwhile
        commonObject.time += 350; s.UpdateRetainedSubpage();
        auto* system = s.guiSystem;
        CHECK(system != nullptr && s.guiActive == system && std::string(system->Name()) == "guis/menu/settings/system.q4ui" &&
              s.guiSystemParent == page && s.systemOpens == 1 && s.retainedSubpageFrom == nullptr && s.retainedSettingsDest == 0);
        CHECK(s.ReturnSystemSettings());
        CHECK(s.guiActive == system && s.guiSystem == system && !system->named.empty() && system->named.back() == "subpageLeave" &&
              s.retainedSubpageFrom == system && !s.retainedSubpageDeeper && s.retainedSubpageKind == RETAINED_SUBPAGE_SETTINGS_SYSTEM &&
              s.retainedSubpageUntil == commonObject.time + 300 && s.systemCloses == 0);
        commonObject.time += 300; s.UpdateRetainedSubpage();
        CHECK(s.guiActive == page && page->named.back() == "subpageReturn" && page->state["from_display"] == "1" &&
              page->state["from_controls"] == "0" && s.guiSystem == nullptr && s.guiSystemParent == nullptr && s.systemCloses == 1);
        CHECK(!s.ReturnSystemSettings() && s.retainedSubpageFrom == nullptr);
        // Back while going deeper reverses, the SYSTEM page never opening.
        s.HandleRetainedSessionRequest(page, "settingsDisplay"); CHECK(page->named.back() == "subpageEnterDisplay");
        sysEvent_t back; back.evType = SE_KEY; back.evValue = K_ESCAPE; back.evValue2 = 1;
        CHECK(s.RetainedSubpageEvent(&back) && page->named.back() == "subpageReverse150" && s.retainedSubpageFrom == nullptr && s.guiActive == page);
        commonObject.time += 350; s.UpdateRetainedSubpage(); CHECK(s.systemOpens == 1 && s.guiActive == page);
        // A draft not applied answers Back itself (its onBack asks first); once
        // it lets the page leave, Back climbs as before.
        s.HandleRetainedSessionRequest(page, "settingsDisplay"); commonObject.time += 350; s.UpdateRetainedSubpage();
        system = s.guiSystem; CHECK(system != nullptr && s.guiActive == system && s.systemOpens == 2);
        systemCanReturn = false;
        CHECK(!s.ReturnSystemSettings() && system->named.back() == "onBack" && s.retainedSubpageFrom == nullptr && s.guiActive == system &&
              !s.systemGuiBackEvent);
        systemCanReturn = true;
        CHECK(s.ReturnSystemSettings() && system->named.back() == "subpageLeave" && s.retainedSubpageFrom == system);
        commonObject.time += 300; s.UpdateRetainedSubpage(); CHECK(s.guiActive == page && s.guiSystem == nullptr && s.systemCloses == 2);
        // A SYSTEM page that cannot open after the descent leaves the stock
        // panel to stand in, at once.
        s.systemOpensPage = false;
        s.HandleRetainedSessionRequest(page, "settingsDisplay"); commonObject.time += 350; s.UpdateRetainedSubpage();
        CHECK(s.systemOpens == 3 && s.guiActive == page && s.guiSystem == nullptr && s.retainedSettingsDest == 22 &&
              s.retainedSettingsAt == commonObject.time);
        s.UpdateRetainedSettingsPage();
        CHECK(s.guiActive == s.guiMainMenu && legacy["desktop::dest"] == 22 && s.guiMainMenu->named.back() == "openSettingsPage");
        // Over the stock settings screen the SYSTEM page's Back returns there
        // at once, the SETTINGS page loaded or not.
        s.systemOpensPage = true; const size_t played = s.played.size();
        CHECK(s.OpenSystemSettings() && s.guiSystemParent == s.guiMainMenu);
        system = s.guiSystem;
        CHECK(s.ReturnSystemSettings() && s.guiActive == s.guiMainMenu && s.guiSystem == nullptr && s.retainedSubpageFrom == nullptr &&
              system->named.empty() && s.played.size() == played && s.systemCloses == 3);
        // With the SYSTEM page fallen back DISPLAY leaves for the stock panel
        // as before.
        auto stock = Session(true); ui_retainedSystem.value = true; stock.PreloadRetainedScreens(); stock.UpdateRetainedHome();
        stock.HandleRetainedSessionRequest(stock.guiRetainedHome, "settings"); commonObject.time += 550;
        stock.UpdateRetainedHome(); stock.UpdateRetainedPage();
        stock.retainedStock.Append("guis/menu/settings/system.q4ui"); CHECK(!stock.RetainedSystemAvailable());
        stock.HandleRetainedSessionRequest(stock.guiRetainedSettingsPage, "settingsDisplay");
        CHECK(stock.guiRetainedSettingsPage->named.back() == "depart" && stock.retainedSettingsDest == 22 && stock.retainedSubpageFrom == nullptr &&
              stock.systemOpens == 0);
    }
    {   // The CONTROLS button opens the retained page as GAME OPTIONS does, a
        // wait for a key ended; the page's verbs reach the session only while
        // it presents; BACK returns; a page that stopped drawing opens the
        // stock page, and the multiplayer route its own stock event.
        auto s = Session(true); CHECK(s.RetainedControlsAvailable());
        auto* settings = s.guiMainMenu; s.retainedControlsCapture = 7; s.retainedControlsCaptured = 9;
        s.OpenRetainedControlsRoute();
        auto* page = s.guiActive;
        CHECK(page == s.guiRetainedControls && std::string(page->Name()) == "guis/menu/settings/controls.q4ui" &&
              s.retainedControlsParent == settings && s.retainedControlsCapture == -1 && s.retainedControlsCaptured == -1 &&
              (s.controlsPublishes == std::vector<idUserInterface*>{page}));
        s.HandleRetainedSessionRequest(page, "controlsBind"); s.HandleRetainedSessionRequest(settings, "controlsClear");
        CHECK((s.controlsRequests == std::vector<std::string>{"controlsBind"}));
        s.retainedControlsCapture = 3; s.ReturnRetainedControls();
        CHECK(s.guiActive == settings && s.retainedControlsParent == nullptr && s.retainedControlsCapture == -1);
        page->failed = true; s.OpenRetainedControlsRoute();
        CHECK(s.guiActive == settings && s.guiRetainedControls == nullptr && (legacyActions == std::vector<std::string>{"set_b_controls"}) &&
              !s.RetainedControlsAvailable() && settings->state["retainedControls"] == "0");
        auto mp = Session(true); mp.OpenRetainedControlsRoute(true); CHECK(mp.guiActive == mp.guiRetainedControls);
        mp.ReturnRetainedControls(); mp.guiRetainedControls->failed = true; mp.OpenRetainedControlsRoute(true);
        CHECK(mp.guiActive == mp.guiMainMenu && legacyActions.empty() && mp.guiMainMenu->named.back() == "fromMp_toControlsStock");
    }
    {   // CONTINUE shows a save's own screenshot, freshly read, only when it exists.
        auto s = Session(true); s.saves = {"quick"}; describedShot = ""; fileSystemObject.files.insert("savegames/quick.tga");
        s.UpdateRetainedHome();
        CHECK(s.guiRetainedHome->state["menu_continue_shot"] == "savegames/quick.tga");
        CHECK((reloaded == std::vector<std::string>{"savegames/quick.tga"}));
        auto bare = Session(true); bare.saves = {"quick"}; describedShot = ""; bare.UpdateRetainedHome();
        CHECK(bare.guiRetainedHome->state["menu_continue"] == "1" && bare.guiRetainedHome->state["menu_continue_shot"].empty() && reloaded.empty());
        auto autosave = Session(true); autosave.saves = {"autosave"}; describedShot = "gfx/guis/loadscreens/airdefense";
        autosave.UpdateRetainedHome(); CHECK(autosave.guiRetainedHome->state["menu_continue_shot"] == "gfx/guis/loadscreens/airdefense");
    }
    {   // A save that records them names the difficulty and the run's play time
        // (section 13.7) in CONTINUE's detail and under the Load Game picture.
        saveDetails["quick"] = {1, 3725};
        auto s = Session(true); s.saves = {"quick", "older"}; s.PreloadRetainedScreens(); s.UpdateRetainedHome();
        CHECK(s.guiRetainedHome->state["menu_continue_detail"] == "Air Defense Bunker \xc2\xb7 Corporal \xc2\xb7 1:02:05 played");
        s.HandleRetainedSessionRequest(s.guiRetainedHome, "loadGame");
        auto* page = s.guiRetainedLoad;
        CHECK(page->state["load.skill"] == "Corporal" && page->state["load.played"] == "1:02:05 played");
        // Each detail stands alone when a save records only one; selecting
        // another save replaces both.
        saveDetails["older"] = {4, -1}; commonObject.time += 550; s.UpdateRetainedHome(); s.UpdateRetainedPage();
        page->state["load.pick"] = "1"; s.HandleRetainedSessionRequest(page, SaveRequest(s, page, "loadSelect").c_str());
        CHECK(page->state["load.skill"] == "Warlord" && page->state["load.played"].empty());
        saveDetails["quick"] = {-1, 59};
        auto timed = Session(true); timed.saves = {"quick"}; timed.UpdateRetainedHome();
        CHECK(timed.guiRetainedHome->state["menu_continue_detail"] == "Air Defense Bunker \xc2\xb7 0:00:59 played");
        saveDetails.clear();
    }
    {   // The level block's levelshot follows the loading screen's choice and falls through missing pictures.
        auto s = Session(true, true);
        mapDecls["game/airdefense2"] = idDict{{{"loadimage", "gfx/guis/loadscreens/airdefense"}}};
        mapDecls["game/airdefense1"] = idDict{{{"loadgui", "guis/loading/intro.gui"}}};
        mapDecls["game/building_b"] = idDict{{{"loadimage", "gfx/guis/loadscreens/missing"}}};
        for (const char* file : {"gfx/guis/loadscreens/airdefense.tga", "gfx/guis/loadscreens/e3_load.tga", "gfx/guis/loadscreens/defstation.tga"})
            fileSystemObject.files.insert(file);
        fileSystemObject.screenshots["game/building_b"] = "gfx/guis/loadscreens/defstation.tga";
        CHECK(s.RetainedPauseShot("game/airdefense2") == "gfx/guis/loadscreens/airdefense");
        CHECK(s.RetainedPauseShot("game/airdefense1") == "gfx/guis/loadscreens/e3_load");
        CHECK(s.RetainedPauseShot("game/building_b") == "gfx/guis/loadscreens/defstation.tga"); // its loadimage is not installed
        CHECK(s.RetainedPauseShot("game/unknown") == "gfx/guis/loadscreens/generic");
    }
    {   // The level block asks the game for the open objectives and the time in the mission.
        auto s = Session(true, true);
        mapDecls["game/airdefense1"] = idDict{{{"name", "Air Defense Bunker"}, {"objectives", "Reach the bunker."}}};
        gameObject.publish = [](idUserInterface* gui) {
            gui->SetStateInt("pause_objective_count", 2); gui->SetStateString("pause_objective_0", "Destroy the battery");
            gui->SetStateString("pause_objective_1", "Reach the bunker"); gui->SetStateInt("pause_mission_seconds", 2530);
            gui->SetStateInt("pause_total_seconds", 11106);
        };
        s.UpdateRetainedHome(); auto* pause = s.guiRetainedHome;
        CHECK(pause && (gameObject.commands == std::vector<std::string>{"retainedPauseFamily", "retainedPauseState"}));
        CHECK(pause->state["pause_level"] == "Air Defense Bunker" && pause->state["pause_detail"] == "Corporal");
        CHECK(pause->state["pause_objective_count"] == "2" && pause->state["pause_objective_0"] == "Destroy the battery");
        CHECK(pause->state["pause_stats"] == "0:42:10 in mission \xc2\xb7 3:05:06 total" && pause->state["pause_saved"].empty());
        // With a save, the next line says how long ago the newest one was written:
        // read once inside the level load, so opening the pause lists no files. A
        // run begun from a save of an earlier build has no total.
        auto saved = Session(true, true); saved.saves = {"quick", "older"}; mapDecls["game/airdefense1"] = idDict{};
        gameObject.publish = [](idUserInterface* gui) { gui->SetStateInt("pause_mission_seconds", 2530); };
        saved.PrepareRetainedLevel("game/airdefense1", false);
        CHECK(saved.saveLists == 1 && saved.retainedNewestSave == 100);
        saved.UpdateRetainedHome();
        CHECK(saved.guiRetainedHome->state["pause_stats"] == "0:42:10 in mission");
        CHECK(saved.guiRetainedHome->state["pause_saved"].rfind("saved ", 0) == 0);
        saved.ExitMenu(); saved.UpdateRetainedHome(); saved.guiActive = saved.guiMainMenu; saved.UpdateRetainedHome();
        CHECK(saved.saveLists == 1);
        // A level without saves, or a load with the gate off, shows no save age.
        auto unsaved = Session(true, true); unsaved.PrepareRetainedLevel("game/airdefense1", false);
        gameObject.publish = [](idUserInterface* gui) { gui->SetStateInt("pause_mission_seconds", 2530); };
        unsaved.UpdateRetainedHome();
        CHECK(unsaved.retainedNewestSave == 0 && unsaved.guiRetainedHome->state["pause_stats"] == "0:42:10 in mission");
        CHECK(unsaved.guiRetainedHome->state["pause_saved"].empty());
        auto gateOff = Session(false, true); gateOff.saves = {"quick"}; gateOff.PrepareRetainedLevel("game/airdefense1", false);
        CHECK(gateOff.saveLists == 0 && gateOff.retainedNewestSave == 0);
        CHECK(std::string(Session_RetainedSaveAge(1000, 1030).c_str()) == "saved just now");
        CHECK(std::string(Session_RetainedSaveAge(1000, 1000 + 4 * 60 + 59).c_str()) == "saved 4 min ago");
        CHECK(std::string(Session_RetainedSaveAge(1000, 1000 + 3 * 3600 + 10).c_str()) == "saved 3 h ago");
        CHECK(std::string(Session_RetainedSaveAge(2000, 1000).c_str()) == "saved just now"); // a clock set back
        // A game module that publishes nothing leaves the map's summary and no time line.
        auto quiet = Session(true, true); mapDecls["game/airdefense1"] = idDict{{{"objectives", "Reach the bunker."}}};
        quiet.UpdateRetainedHome();
        CHECK(quiet.guiRetainedHome->state["pause_objective_count"] == "0" && quiet.guiRetainedHome->state["pause_stats"].empty());
        CHECK(quiet.guiRetainedHome->state["pause_saved"].empty());
        CHECK(quiet.guiRetainedHome->state["pause_objectives"] == "Reach the bunker.");
    }
    {   // After Kane's stroggification the pause takes the Strogg family. The game
        // answers once a level, and the Marine pause stands in when the Strogg
        // pause cannot present.
        auto s = Session(true, true);
        gameObject.publish = [](idUserInterface* gui) { gui->SetStateBool("pause_strogg", true); };
        s.UpdateRetainedHome(); auto* strogg = s.guiRetainedHome;
        CHECK(strogg && std::string(strogg->Name()) == "guis/menu/pause_strogg.q4ui" && strogg == s.guiRetainedPauseStrogg);
        CHECK(strogg->active && strogg->named.back() == "open" && strogg->state["pause_level"] == "game/airdefense1");
        CHECK((gameObject.commands == std::vector<std::string>{"retainedPauseFamily", "retainedPauseState"}));
        CHECK((managerObject.loads == std::vector<std::string>{"guis/menu/pause_strogg.q4ui"}));
        s.HandleRetainedSessionRequest(strogg, "resume"); CHECK(s.exits == 1);
        s.UpdateRetainedHome(); CHECK(s.guiRetainedHome == nullptr && !strogg->active);
        CHECK(strogg->named.back() == "release" && s.guiRetainedReleasing == strogg);  // either family releases the softened view
        s.guiActive = s.guiMainMenu; s.UpdateRetainedHome();
        CHECK(s.guiRetainedHome == strogg && std::count(gameObject.commands.begin(), gameObject.commands.end(), "retainedPauseFamily") == 1);
        CHECK(s.guiRetainedReleasing == nullptr && strogg->named.back() == "open");  // pausing again during the release reopens it
        s.ReportRetainedScreens(); CHECK(commonObject.output.find("pause=0 strogg=1 handoff=0") != std::string::npos);
        // Each level asks again: a Marine level pauses in the Marine family.
        s.ExitMenu(); s.UpdateRetainedHome(); s.PrepareRetainedLevel("game/airdefense1", false);
        gameObject.publish = nullptr; s.guiActive = s.guiMainMenu; s.UpdateRetainedHome();
        CHECK(s.guiRetainedHome && std::string(s.guiRetainedHome->Name()) == "guis/menu/pause.q4ui");
        CHECK(std::count(gameObject.commands.begin(), gameObject.commands.end(), "retainedPauseFamily") == 2);
        // A Strogg view that stopped drawing hands over to the Marine pause.
        s.ExitMenu(); s.UpdateRetainedHome(); s.PrepareRetainedLevel("game/recomp", false);
        gameObject.publish = [](idUserInterface* gui) { gui->SetStateBool("pause_strogg", true); };
        strogg->failed = true; s.guiActive = s.guiMainMenu; s.UpdateRetainedHome();
        CHECK(s.guiRetainedHome && std::string(s.guiRetainedHome->Name()) == "guis/menu/pause.q4ui" && s.guiRetainedPauseStrogg == nullptr);
        CHECK(commonObject.warnings.size() == 1 && commonObject.warnings[0].find("stopped drawing") != std::string::npos);
        CHECK((Stock(s) == std::vector<std::string>{"guis/menu/pause_strogg.q4ui"}));
        // Not installed, it falls back quietly to the Marine pause.
        auto missing = Session(true, true); fileSystemObject.files.erase("guis/menu/pause_strogg.q4ui");
        gameObject.publish = [](idUserInterface* gui) { gui->SetStateBool("pause_strogg", true); };
        missing.UpdateRetainedHome();
        CHECK(missing.guiRetainedHome && std::string(missing.guiRetainedHome->Name()) == "guis/menu/pause.q4ui" && commonObject.warnings.empty());
        CHECK((Stock(missing) == std::vector<std::string>{"guis/menu/pause_strogg.q4ui"}));
        // The title never asks the game.
        auto title = Session(true); title.UpdateRetainedHome(); CHECK(gameObject.commands.empty());
    }
    {   // RESUME closes the pause screen at once, but the closed screen keeps
        // drawing over the running game for 250 ms while the softened view
        // releases; anything else taking the screen ends the release.
        auto s = Session(true, true); s.UpdateRetainedHome(); auto* pause = s.guiRetainedHome;
        CHECK(pause && std::string(pause->Name()) == "guis/menu/pause.q4ui" && s.guiRetainedReleasing == nullptr);
        s.HandleRetainedSessionRequest(pause, "resume"); s.UpdateRetainedHome();
        CHECK(s.guiRetainedHome == nullptr && !pause->active && pause->deactivations == 1);
        CHECK(pause->named.back() == "release" && s.guiRetainedReleasing == pause && s.retainedReleaseUntil == commonObject.time + 250);
        s.ReportRetainedScreens(); CHECK(commonObject.output.find("home=- title=0 pause=1 strogg=0 handoff=0 release=1 mp=-") != std::string::npos);
        commonObject.time += 249; s.UpdateRetainedHome(); CHECK(s.guiRetainedReleasing == pause && !pause->active);
        commonObject.time += 1; s.UpdateRetainedHome(); CHECK(s.guiRetainedReleasing == nullptr && pause->activations == 1);
        // A disconnect or a level load ends it at once.
        s.guiActive = s.guiMainMenu; s.UpdateRetainedHome(); s.ExitMenu(); s.UpdateRetainedHome(); CHECK(s.guiRetainedReleasing == pause);
        s.mapSpawned = false; s.UpdateRetainedHome(); CHECK(s.guiRetainedReleasing == nullptr);
        s.mapSpawned = true; s.guiActive = s.guiMainMenu; s.UpdateRetainedHome(); s.ExitMenu(); s.UpdateRetainedHome();
        s.PrepareRetainedLevel("game/airdefense1", false); CHECK(s.guiRetainedReleasing == nullptr);
        // The pause handing over to another screen, and the title closing, release nothing.
        s.guiActive = s.guiMainMenu; s.UpdateRetainedHome(); idUserInterface other("guis/msg.gui"); s.guiActive = &other;
        s.UpdateRetainedHome(); CHECK(s.guiRetainedHome == nullptr && s.guiRetainedReleasing == nullptr && pause->named.back() != "release");
        auto title = Session(true); title.UpdateRetainedHome(); auto* home = title.guiRetainedHome;
        title.mapSpawned = true; title.ExitMenu(); title.UpdateRetainedHome();  // CONTINUE: the title closes into a level
        CHECK(home && std::string(home->Name()) == "guis/menu/title.q4ui" && title.guiRetainedReleasing == nullptr && home->named.back() != "release");
    }
    {   // The Objectives page: the game lists every objective screenshot of the level,
        // which resolves inside the load, and a published screenshot shows only when
        // it is a plain, installed image name.
        auto s = Session(true, true);
        for (const char* file : {"gfx/objectives/airdefense_obj_1.tga", "gfx/objectives/airdefense_obj_2.tga"}) fileSystemObject.files.insert(file);
        gameObject.publish = [](idUserInterface* gui) {
            gui->SetStateInt("level_image_count", 3); gui->SetStateString("level_image_0", "gfx/objectives/airdefense_obj_1");
            gui->SetStateString("level_image_1", "gfx/objectives/missing"); gui->SetStateString("level_image_2", "../escape");
            gui->SetStateInt("pause_objective_count", 3);
            gui->SetStateString("pause_objective_shot_0", "gfx/objectives/airdefense_obj_2");
            gui->SetStateString("pause_objective_shot_1", "gfx/objectives/missing");
            gui->SetStateString("pause_objective_shot_2", "../escape");
        };
        s.PrecacheRetainedLevelImages(); CHECK(precached.empty() && gameObject.commands.empty()); // no pause document loaded yet
        s.PrepareRetainedLevel("game/airdefense1", false); precached.clear();
        s.PrecacheRetainedLevelImages();
        CHECK((gameObject.commands == std::vector<std::string>{"retainedLevelImages"}));
        CHECK((precached == std::vector<std::string>{"gfx/objectives/airdefense_obj_1"}));
        s.UpdateRetainedHome(); auto* pause = s.guiRetainedHome;
        CHECK(pause && pause->state["pause_objective_shot_0"] == "gfx/objectives/airdefense_obj_2");
        CHECK(pause->state["pause_objective_shot_1"].empty() && pause->state["pause_objective_shot_2"].empty());
        auto off = Session(false, true); off.PrepareRetainedLevel("game/airdefense1", false); off.PrecacheRetainedLevelImages();
        CHECK(precached.empty() && gameObject.commands.empty()); // the gate off loads no pause, so nothing resolves
    }
    {   // The multiplayer card covers the game's in-match menu only through its
        // opt-in while pages still hand off; the game answers the cover protocol,
        // and anything it does not, or a menu other than mpmain.gui, keeps stock.
        static idUserInterface mpMenu("guis/mpmain.gui"), chat("guis/mpchat.gui");
        auto Mp = [&](bool gate, bool optIn) {
            auto s = Session(gate, true); s.multiplayer = true; ui_retainedMultiplayer.value = optIn;
            mpMenu = idUserInterface("guis/mpmain.gui"); mpMenu.state["gameDraw"] = "1"; s.guiActive = &mpMenu;
            chat = idUserInterface("guis/mpchat.gui"); chat.state["gameDraw"] = "1";
            gameObject.publish = [](idUserInterface* gui) {
                if (gameObject.commands.back() == "retainedMultiplayerVariant") gui->SetStateBool("mp.welcome", welcomeAnswer);
                if (gameObject.commands.back() == "retainedMultiplayerCover") { gui->SetStateInt("mp.protocol", 4); gui->SetStateBool("mp.cover_allowed", true); }
                if (gameObject.commands.back() != "retainedMultiplayerUncover") gui->SetStateInt("mp.revision", static_cast<int>(gameObject.commands.size()));
            };
            return s;
        };
        const bool complete = RETAINED_MP_ESCAPE_MISSING_PAGES[0] == NULL;
        {   auto s = Mp(true, false); s.UpdateRetainedMultiplayer();
            CHECK((s.guiRetainedMultiplayer != nullptr) == complete);  // the gate alone waits for every page
            auto off = Mp(false, false); off.UpdateRetainedMultiplayer(); off.PrepareRetainedLevel("mp/q4dm1", true);
            CHECK(off.guiRetainedMultiplayer == nullptr && managerObject.loads.empty() && gameObject.commands.empty());
        }
        auto s = Mp(true, true);
        idKeyInput::Bound()["_impulse28"] = "^iK8b"; idKeyInput::Bound()["_impulse29"] = "^ik8c";
        s.PrepareRetainedLevel("mp/q4dm1", true);  // loaded inside the level load
        CHECK((managerObject.loads == std::vector<std::string>{"guis/menu/mp_escape.q4ui", "guis/menu/mp_welcome.q4ui",
                                                               "guis/menu/mp_summary.q4ui"}) &&
              s.guiRetainedEscape && s.guiRetainedEscape->state["card.tab"] == "0" && s.guiRetainedWelcome &&
              s.guiRetainedWelcome->state["card.tab"] == "0" && s.guiRetainedSummary && s.guiRetainedSummary->state["card.tab"] == "0");
        CHECK(managerObject.flags[0].unique && !managerObject.flags[0].shared);
        s.UpdateRetainedMultiplayer();
        auto* card = s.guiRetainedMultiplayer;
        CHECK(card == s.guiRetainedEscape && s.RetainedMultiplayerCovers() && card->active && card->named.back() == "open");
        CHECK((gameObject.commands == std::vector<std::string>{"retainedMultiplayerVariant", "retainedMultiplayerCover"}) &&
              card->stateChanges == 1 && s.drains == 1 && !s.retainedMultiplayerWelcome);
        // The Vote page's ballots show their keys by name: one bound to a key the
        // engine names, one to a key it does not, and none unbound.
        CHECK(card->state["mp.keys.vote_yes"] == "F1" && card->state["mp.keys.vote_yes_bound"] == "1" &&
              card->state["mp.keys.vote_no"].empty() && card->state["mp.keys.vote_no_bound"] == "0" &&
              card->state["mp.keys.voice_chat"].empty() && card->state["mp.keys.voice_chat_bound"] == "0");
        s.ReportRetainedScreens(); CHECK(commonObject.output.find("multiplayer=1 ") != std::string::npos && commonObject.output.find(" mp=escape ") != std::string::npos);
        // Each frame asks the game for what changed; only a new revision publishes.
        s.UpdateRetainedMultiplayer(); CHECK(gameObject.commands.back() == "retainedMultiplayerState" && card->stateChanges == 2);
        gameObject.publish = nullptr; s.UpdateRetainedMultiplayer(); CHECK(card->stateChanges == 2 && card->activations == 1);
        // Home verbs from the card, and card verbs from elsewhere, are refused.
        s.HandleRetainedSessionRequest(card, "quit"); CHECK(commonObject.quits == 0 && commonObject.warnings.back().find("unhandled multiplayer request") != std::string::npos);
        idUserInterface stranger("guis/other.q4ui"); s.HandleRetainedSessionRequest(&stranger, "mpClose"); CHECK(gameObject.guiCommands.empty());
        // Resume: the game's own close, with its sound; the card releases the
        // softened view over the running game in the same frame.
        gameObject.answer = [](const char*) -> const char* { return nullptr; };
        s.HandleRetainedSessionRequest(card, "mpClose");
        CHECK((gameObject.guiCommands == std::vector<std::string>{"play main_menu_selection ; close"}) && s.guiActive == nullptr);
        CHECK(s.guiRetainedMultiplayer == nullptr && !card->active && card->named.back() == "release");
        CHECK(s.guiRetainedReleasing == card && s.retainedReleaseUntil == commonObject.time + 250 && gameObject.commands.back() == "retainedMultiplayerUncover");
        s.UpdateRetainedMultiplayer(); CHECK(s.guiRetainedMultiplayer == nullptr && card->deactivations == 1);
        // Main Menu leaves for the main menu with the match running: no release.
        s.guiActive = &mpMenu; gameObject.publish = [](idUserInterface* gui) { gui->SetStateInt("mp.protocol", 4); gui->SetStateBool("mp.cover_allowed", true); };
        s.UpdateRetainedMultiplayer(); CHECK(s.guiRetainedMultiplayer == card && card->activations == 2 && card->named.back() == "open");
        gameObject.answer = [](const char*) -> const char* { return "main"; };
        s.HandleRetainedSessionRequest(card, "mpMainMenu");
        CHECK(gameObject.guiCommands.back() == "play main_menu_selection ; mainMenu" && s.menus == 1 && s.guiActive == s.guiMainMenu);
        CHECK(s.guiRetainedMultiplayer == nullptr && card->named.back() == "open" && s.guiRetainedReleasing == card);  // the earlier release, not a new one
        // Disconnect, confirmed on the card, is the game's own disconnect.
        s.guiActive = &mpMenu; s.guiRetainedReleasing = nullptr; s.UpdateRetainedMultiplayer();
        gameObject.answer = [](const char*) -> const char* { return nullptr; };
        s.HandleRetainedSessionRequest(card, "mpDisconnect"); CHECK(gameObject.guiCommands.back() == "play main_menu_selection ; disconnect");
        // Opening the menu again covers it again.
        s.guiActive = &mpMenu; s.UpdateRetainedMultiplayer(); CHECK(s.guiRetainedMultiplayer == card);
        // A page in development hands off: the stock menu presents at once and
        // presses the page's button, and the card stays away until the menu closes.
        gameObject.answer = nullptr; card->state["card.stock_page"] = "2";
        s.HandleRetainedSessionRequest(card, "mpStockPage");
        CHECK(s.guiRetainedMultiplayer == nullptr && card->named.back() != "release" && s.retainedMultiplayerUncovered);
        CHECK(legacyActions.back() == "main_b_vote" && s.dispatched.back() == "play main_menu_selection" && s.retainedMultiplayerHandoff == -1);
        s.UpdateRetainedMultiplayer(); CHECK(s.guiRetainedMultiplayer == nullptr);
        s.guiActive = nullptr; s.UpdateRetainedMultiplayer(); CHECK(!s.retainedMultiplayerUncovered);
        s.guiActive = &mpMenu; s.UpdateRetainedMultiplayer(); CHECK(s.guiRetainedMultiplayer == card);
        // Before the menu's opening shows its column, the hand-off waits up to 500 ms.
        legacyActionAvailable = false; card->state["card.stock_page"] = "7"; s.HandleRetainedSessionRequest(card, "mpStockPage");
        CHECK(s.retainedMultiplayerHandoff == 7 && legacyActions.back() == "main_b_admin");
        commonObject.time += 499; s.UpdateRetainedMultiplayer(); CHECK(s.retainedMultiplayerHandoff == 7 && commonObject.warnings.size() == 1);
        legacyActionAvailable = true; s.UpdateRetainedMultiplayer(); CHECK(s.retainedMultiplayerHandoff == -1 && legacyActions.size() >= 3);
        legacyActionAvailable = false; s.guiActive = nullptr; s.UpdateRetainedMultiplayer(); s.guiActive = &mpMenu; s.UpdateRetainedMultiplayer();
        s.HandleRetainedSessionRequest(card, "mpStockPage"); commonObject.time += 500; s.UpdateRetainedMultiplayer();
        CHECK(s.retainedMultiplayerHandoff == -1 && commonObject.warnings.back().find("did not become available") != std::string::npos);
        legacyActionAvailable = true;
        // Out-of-range pages are refused and change nothing.
        s.guiActive = nullptr; s.UpdateRetainedMultiplayer(); s.guiActive = &mpMenu; s.UpdateRetainedMultiplayer();
        // The Team page's actions: the game derives the slot's action again and
        // closes the menu when it acts; an action it refuses now keeps it open.
        gameObject.answer = [](const char*) -> const char* { return "continue"; };
        card->state["card.team_action"] = "1"; s.HandleRetainedSessionRequest(card, "mpTeamAction");
        CHECK(gameObject.guiCommands.back() == "play main_menu_selection ; retained team 1" && s.guiRetainedMultiplayer == card);
        gameObject.answer = [](const char*) -> const char* { return nullptr; };
        s.HandleRetainedSessionRequest(card, "mpTeamAction");
        CHECK(s.guiActive == nullptr && s.guiRetainedMultiplayer == nullptr && card->named.back() == "release");
        s.guiActive = &mpMenu; s.guiRetainedReleasing = nullptr; s.UpdateRetainedMultiplayer(); CHECK(s.guiRetainedMultiplayer == card);
        const size_t asked = gameObject.guiCommands.size();
        for (const char* slot : {"-1", "3"}) {
            card->state["card.team_action"] = slot; s.HandleRetainedSessionRequest(card, "mpTeamAction");
            CHECK(gameObject.guiCommands.size() == asked && s.guiRetainedMultiplayer == card &&
                  commonObject.warnings.back().find("team action") != std::string::npos);
        }
        // The Match page names a Match Control action by index; the session sends
        // the game's own fixed token and the menu stays open. The refresh the
        // page asks as it opens makes no sound; an index past the table is refused.
        gameObject.answer = [](const char*) -> const char* { return "continue"; };
        card->state["card.match_op"] = "1"; s.HandleRetainedSessionRequest(card, "mpMatch");
        CHECK(gameObject.guiCommands.back() == "play main_menu_selection ; matchControl ready_toggle" && s.guiRetainedMultiplayer == card);
        card->state["card.match_op"] = "0"; s.HandleRetainedSessionRequest(card, "mpMatch");
        CHECK(gameObject.guiCommands.back() == "matchControl refresh");
        card->state["card.match_op"] = "7"; s.HandleRetainedSessionRequest(card, "mpMatch");
        CHECK(gameObject.guiCommands.back() == "play main_menu_selection ; matchControl arm_forfeit");
        card->state["card.match_op"] = "15"; s.HandleRetainedSessionRequest(card, "mpMatch");
        CHECK(gameObject.guiCommands.back() == "play main_menu_selection ; matchControl confirm");
        card->state["card.match_op"] = "16"; s.HandleRetainedSessionRequest(card, "mpMatch");
        CHECK(gameObject.guiCommands.back() == "play main_menu_selection ; matchControl cancel_confirm" && s.guiRetainedMultiplayer == card);
        card->state["card.match_op"] = "17"; s.HandleRetainedSessionRequest(card, "mpMatch");
        CHECK(gameObject.guiCommands.back() == "play main_menu_selection ; matchControl team_join_marine");
        card->state["card.match_op"] = "26"; s.HandleRetainedSessionRequest(card, "mpMatch");
        CHECK(gameObject.guiCommands.back() == "play main_menu_selection ; matchControl arm_roster_remove");
        card->state["card.match_op"] = "32"; s.HandleRetainedSessionRequest(card, "mpMatch");
        CHECK(gameObject.guiCommands.back() == "play main_menu_selection ; matchControl series_contestant_bind" && s.guiRetainedMultiplayer == card);
        card->state["card.match_op"] = "34"; s.HandleRetainedSessionRequest(card, "mpMatch");
        CHECK(gameObject.guiCommands.back() == "play main_menu_selection ; matchControl proposal_yes");
        card->state["card.match_op"] = "40"; s.HandleRetainedSessionRequest(card, "mpMatch");
        CHECK(gameObject.guiCommands.back() == "play main_menu_selection ; matchControl arm_rules_commit");
        card->state["card.match_op"] = "41"; s.HandleRetainedSessionRequest(card, "mpMatch");
        CHECK(gameObject.guiCommands.back() == "play main_menu_selection ; matchControl rules_discard" && s.guiRetainedMultiplayer == card);
        card->state["card.match_op"] = "42"; s.HandleRetainedSessionRequest(card, "mpMatch");
        CHECK(gameObject.guiCommands.back() == "play main_menu_selection ; matchControl series_stage");
        card->state["card.match_op"] = "46"; s.HandleRetainedSessionRequest(card, "mpMatch");
        CHECK(gameObject.guiCommands.back() == "play main_menu_selection ; matchControl arm_veto_ban");
        card->state["card.match_op"] = "50"; s.HandleRetainedSessionRequest(card, "mpMatch");
        CHECK(gameObject.guiCommands.back() == "play main_menu_selection ; matchControl arm_veto_side_strogg" && s.guiRetainedMultiplayer == card);
        const size_t matched = gameObject.guiCommands.size();
        for (const char* action : {"-1", "51", "99"}) {
            card->state["card.match_op"] = action; s.HandleRetainedSessionRequest(card, "mpMatch");
            CHECK(gameObject.guiCommands.size() == matched && s.guiRetainedMultiplayer == card &&
                  commonObject.warnings.back().find("match action") != std::string::npos);
        }
        // A Match Control list's row: the session sets the stock list's own
        // selection on the game's menu to it and sends the list's token, as the
        // stock list does, and the menu stays open. A list or a row outside
        // the tables is refused and selects nothing.
        card->state["card.match_list"] = "0"; card->state["card.match_row"] = "3"; s.HandleRetainedSessionRequest(card, "mpMatchSelect");
        CHECK(mpMenu.state["match_team_rows_sel_0"] == "3" &&
              gameObject.guiCommands.back() == "play main_menu_selection ; matchControl select_team_row" && s.guiRetainedMultiplayer == card);
        card->state["card.match_list"] = "1"; card->state["card.match_row"] = "0"; s.HandleRetainedSessionRequest(card, "mpMatchSelect");
        CHECK(mpMenu.state["match_replacement_rows_sel_0"] == "0" &&
              gameObject.guiCommands.back() == "play main_menu_selection ; matchControl select_replacement_row");
        card->state["card.match_list"] = "5"; card->state["card.match_row"] = "127"; s.HandleRetainedSessionRequest(card, "mpMatchSelect");
        CHECK(mpMenu.state["match_series_map_rows_sel_0"] == "127" &&
              gameObject.guiCommands.back() == "play main_menu_selection ; matchControl select_series_map");
        const size_t selected = gameObject.guiCommands.size();
        for (const auto& [list, row] : std::vector<std::pair<const char*, const char*>>{{"-1", "0"}, {"6", "0"}, {"0", "-1"}, {"0", "128"}}) {
            card->state["card.match_list"] = list; card->state["card.match_row"] = row; s.HandleRetainedSessionRequest(card, "mpMatchSelect");
            CHECK(gameObject.guiCommands.size() == selected && mpMenu.state["match_team_rows_sel_0"] == "3" && s.guiRetainedMultiplayer == card &&
                  commonObject.warnings.back().find("of match list") != std::string::npos);
        }
        // The role an invitation or an assignment gives waits on the game's
        // menu, as the stock choice's value does: nothing reaches the game.
        s.HandleRetainedSessionRequest(card, "mpMatchRole 3");
        CHECK(mpMenu.state["match_role_choice"] == "3" && gameObject.guiCommands.size() == selected && s.guiRetainedMultiplayer == card);
        for (const char* bad : {"mpMatchRole 0", "mpMatchRole 5", "mpMatchRole -1"}) {
            s.HandleRetainedSessionRequest(card, bad);
            CHECK(mpMenu.state["match_role_choice"] == "3" && gameObject.guiCommands.size() == selected &&
                  commonObject.warnings.back().find("match role") != std::string::npos);
        }
        s.HandleRetainedSessionRequest(card, "mpMatchRole x");
        CHECK(mpMenu.state["match_role_choice"] == "3" && commonObject.warnings.back().find("unhandled multiplayer request") != std::string::npos);
        // The proposal a ballot or a cancellation goes to waits on the game's
        // menu in the stock choice's own words; a target outside the two is
        // refused.
        s.HandleRetainedSessionRequest(card, "mpMatchScope 1");
        CHECK(mpMenu.state["match_proposal_scope_choice"] == "side" && gameObject.guiCommands.size() == selected);
        s.HandleRetainedSessionRequest(card, "mpMatchScope 0");
        CHECK(mpMenu.state["match_proposal_scope_choice"] == "global" && gameObject.guiCommands.size() == selected);
        for (const char* bad : {"mpMatchScope 2", "mpMatchScope -1"}) {
            s.HandleRetainedSessionRequest(card, bad);
            CHECK(mpMenu.state["match_proposal_scope_choice"] == "global" && gameObject.guiCommands.size() == selected &&
                  commonObject.warnings.back().find("ballot target") != std::string::npos);
        }
        // The format a staged series takes, in the stock choice's own words; a
        // format outside the three is refused.
        s.HandleRetainedSessionRequest(card, "mpMatchSeriesProfile 2");
        CHECK(mpMenu.state["match_series_profile_choice"] == "best_of_five" && gameObject.guiCommands.size() == selected);
        s.HandleRetainedSessionRequest(card, "mpMatchSeriesProfile 0");
        CHECK(mpMenu.state["match_series_profile_choice"] == "best_of_one" && gameObject.guiCommands.size() == selected);
        for (const char* bad : {"mpMatchSeriesProfile 3", "mpMatchSeriesProfile -1"}) {
            s.HandleRetainedSessionRequest(card, bad);
            CHECK(mpMenu.state["match_series_profile_choice"] == "best_of_one" && gameObject.guiCommands.size() == selected &&
                  commonObject.warnings.back().find("series format") != std::string::npos);
        }
        // The value to stage for a rule waits on the game's menu, a whole number
        // up to the largest a rule takes; anything else is refused.
        s.HandleRetainedSessionRequest(card, "mpMatchRuleValue 10000");
        CHECK(mpMenu.state["match_rule_value"] == "10000" && gameObject.guiCommands.size() == selected && s.guiRetainedMultiplayer == card);
        s.HandleRetainedSessionRequest(card, "mpMatchRuleValue 0");
        CHECK(mpMenu.state["match_rule_value"] == "0" && gameObject.guiCommands.size() == selected);
        for (const char* bad : {"mpMatchRuleValue 10001", "mpMatchRuleValue -1", "mpMatchRuleValue 123456", "mpMatchRuleValue x",
                                "mpMatchRuleValue ", "mpMatchRuleValue 1 2"}) {
            s.HandleRetainedSessionRequest(card, bad);
            CHECK(mpMenu.state["match_rule_value"] == "0" && gameObject.guiCommands.size() == selected &&
                  commonObject.warnings.back().find("unhandled multiplayer request") != std::string::npos);
        }
        // The Players page: the client a row or the statistics name reaches the
        // game as "retained select|mute|friend <client> <identity>", none of which closes
        // the menu; a client outside the server's slots is refused.
        gameObject.answer = [](const char*) -> const char* { return "continue"; };
        card->state["card.client"] = "19"; s.HandleRetainedSessionRequest(card, "mpSelectPlayer 4 8196");
        CHECK(gameObject.guiCommands.back() == "play main_menu_selection ; retained select 4 8196" && s.guiRetainedMultiplayer == card);
        s.HandleRetainedSessionRequest(card, "mpMute 4 8196");
        CHECK(gameObject.guiCommands.back() == "play main_menu_selection ; retained mute 4 8196" && s.guiRetainedMultiplayer == card);
        card->state["card.client"] = "0"; s.HandleRetainedSessionRequest(card, "mpFriend 31 4294967295");
        CHECK(gameObject.guiCommands.back() == "play main_menu_selection ; retained friend 31 4294967295" && s.guiRetainedMultiplayer == card);
        const size_t named = gameObject.guiCommands.size();
        for (const char* bad : {"mpSelectPlayer", "mpSelectPlayer -1 100", "mpSelectPlayer 32 100", "mpMute 4", "mpFriend 4 x", "mpFriend 4 100;quit", "mpFriend 4 100 2"}) {
            s.HandleRetainedSessionRequest(card, bad);
            CHECK(gameObject.guiCommands.size() == named && s.guiRetainedMultiplayer == card &&
                  commonObject.warnings.back().find("player identity") != std::string::npos);
        }
        // The Vote page: a drafted field's value control requests "<verb> <value>",
        // which reaches the game quietly as "retained voteSet <field> <value>" and
        // keeps the menu open; a ballot reaches it as "retained vote yes|no" and
        // Call Vote as "retained callVote", each closing the menu when the game
        // acts. A malformed or unknown request is refused.
        gameObject.answer = [](const char*) -> const char* { return "continue"; };
        s.HandleRetainedSessionRequest(card, "mpVoteMap 3");
        CHECK(gameObject.guiCommands.back() == "retained voteSet 0 3" && s.guiRetainedMultiplayer == card);
        s.HandleRetainedSessionRequest(card, "mpVoteKick -1 0"); CHECK(gameObject.guiCommands.back() == "retained voteKick -1 \"0\"");
        s.HandleRetainedSessionRequest(card, "mpVoteKick 1 0,1:4294967295,31:2031647");
        CHECK(gameObject.guiCommands.back() == "retained voteKick 1 \"0,1:4294967295,31:2031647\"");
        s.HandleRetainedSessionRequest(card, "mpVoteControlTime 600"); CHECK(gameObject.guiCommands.back() == "retained voteSet 6 600");
        s.HandleRetainedSessionRequest(card, "mpVoteBalance 0"); CHECK(gameObject.guiCommands.back() == "retained voteSet 7 0");
        const size_t drafted = gameObject.guiCommands.size();
        for (const char* bad : {"mpVoteMap", "mpVoteMap 3.5", "mpVoteMap x", "mpVoteMap 1000", "mpVoteMap -2", "mpVoteNope 1",
                                "mpVoteMap  3", "mpVoteMap -", "mpVoteMap 3 4", "mpvotemap", "mpVote 3", "mpVoteMapX 3"}) {
            s.HandleRetainedSessionRequest(card, bad);
            CHECK(gameObject.guiCommands.size() == drafted && s.guiRetainedMultiplayer == card &&
                  commonObject.warnings.back().find("unhandled multiplayer request") != std::string::npos);
        }
        s.HandleRetainedSessionRequest(card, "mpVoteYes");
        CHECK(gameObject.guiCommands.back() == "play main_menu_selection ; retained vote yes" && s.guiRetainedMultiplayer == card);
        s.HandleRetainedSessionRequest(card, "mpCallVote");
        CHECK(gameObject.guiCommands.back() == "play main_menu_selection ; retained callVote" && s.guiRetainedMultiplayer == card);
        gameObject.answer = [](const char*) -> const char* { return nullptr; };
        s.HandleRetainedSessionRequest(card, "mpVoteNo");
        CHECK(gameObject.guiCommands.back() == "play main_menu_selection ; retained vote no" && s.guiActive == nullptr &&
              s.guiRetainedMultiplayer == nullptr && card->named.back() == "release");
        s.guiActive = &mpMenu; s.guiRetainedReleasing = nullptr; s.UpdateRetainedMultiplayer(); CHECK(s.guiRetainedMultiplayer == card);
        // The Settings pages: a model list's row and the crosshair reach the game
        // quietly as "retained appearance <slot> <row>" and "retained crosshair
        // <index>"; a swatch as "retained rail <row>"; Controls, Game Options and
        // System leave for the main menu's pages. A row below 0, and a swatch
        // outside the seven, are refused.
        gameObject.answer = [](const char*) -> const char* { return "continue"; };
        s.HandleRetainedSessionRequest(card, "mpModelSelf 3"); CHECK(gameObject.guiCommands.back() == "retained appearance 0 3");
        s.HandleRetainedSessionRequest(card, "mpModelTeam 0"); CHECK(gameObject.guiCommands.back() == "retained appearance 2 0");
        s.HandleRetainedSessionRequest(card, "mpCrosshair 20"); CHECK(gameObject.guiCommands.back() == "retained crosshair 20");
        card->state["card.rail"] = "6"; s.HandleRetainedSessionRequest(card, "mpRail");
        CHECK(gameObject.guiCommands.back() == "play main_menu_selection ; retained rail 6" && s.guiRetainedMultiplayer == card);
        const size_t styled = gameObject.guiCommands.size();
        s.HandleRetainedSessionRequest(card, "mpModelEnemy -1"); s.HandleRetainedSessionRequest(card, "mpCrosshair x");
        for (const char* rail : {"-1", "7"}) { card->state["card.rail"] = rail; s.HandleRetainedSessionRequest(card, "mpRail"); }
        CHECK(gameObject.guiCommands.size() == styled && s.guiRetainedMultiplayer == card);
        gameObject.answer = [](const char* command) -> const char* {
            static std::string menu; menu = std::string("main ") + (std::strrchr(command, ' ') + 1); return menu.c_str();
        };
        for (const auto& [verb, page] : std::vector<std::pair<const char*, const char*>>{{"mpSettingsControls", "fromMp_toControls"},
                                                                                         {"mpSettingsGame", "fromMp_toGameoptions"},
                                                                                         {"mpSettingsSystem", "fromMp_toSystem"}}) {
            s.guiActive = &mpMenu; s.guiRetainedReleasing = nullptr; s.UpdateRetainedMultiplayer(); CHECK(s.guiRetainedMultiplayer == card);
            s.HandleRetainedSessionRequest(card, verb);
            CHECK(gameObject.guiCommands.back() == std::string("play main_menu_selection ; mainMenu ") + page && s.guiActive == s.guiMainMenu &&
                  s.guiRetainedMultiplayer == nullptr);
        }
        s.guiActive = &mpMenu; s.guiRetainedReleasing = nullptr; s.UpdateRetainedMultiplayer(); CHECK(s.guiRetainedMultiplayer == card);
        gameObject.answer = nullptr;
        for (const char* page : {"-1", "8"}) {
            card->state["card.stock_page"] = page; s.HandleRetainedSessionRequest(card, "mpStockPage");
            CHECK(s.guiRetainedMultiplayer == card && !s.retainedMultiplayerUncovered);
        }
        // The Admin page: the password the masked field entered moves to the
        // remote console's CVar and leaves the card at once, then the game checks
        // it; nothing entered, or too long, sends nothing. A row names its client
        // by the game's own list; the player actions, the ban list and Apply
        // reach the game; a drafted setting reaches it quietly as "retained
        // adminSet <field> <value>"; the console's command goes through the stock
        // console's input. The referee credential moves to the game's menu, the
        // name and clan to their settings.
        gameObject.answer = [](const char*) -> const char* { return "continue"; };
        card->state["card.admin.password"] = "correct horse battery"; s.HandleRetainedSessionRequest(card, "mpAdminPass");
        CHECK(cvarSystemObject.strings["net_clientRemoteConsolePassword"] == "correct horse battery" && card->state["card.admin.password"].empty() &&
              gameObject.guiCommands.back() == "play main_menu_selection ; retained adminPass" && s.guiRetainedMultiplayer == card);
        const size_t checked = gameObject.guiCommands.size();
        s.HandleRetainedSessionRequest(card, "mpAdminPass");
        card->state["card.admin.password"] = std::string(65, 'x'); s.HandleRetainedSessionRequest(card, "mpAdminPass");
        CHECK(gameObject.guiCommands.size() == checked && card->state["card.admin.password"].empty() &&
              cvarSystemObject.strings["net_clientRemoteConsolePassword"] == "correct horse battery");
        card->state["card.admin_row"] = "2"; card->state["mp.admin.player2.client"] = "7"; s.HandleRetainedSessionRequest(card, "mpAdminSelect 7 8199");
        CHECK(gameObject.guiCommands.back() == "play main_menu_selection ; retained adminSelect 7 8199" && s.guiRetainedMultiplayer == card);
        for (const auto& [verb, command] : std::vector<std::pair<const char*, const char*>>{
                 {"mpAdminKick 7 8199", "retained adminKick 7 8199"}, {"mpAdminBan 7 8199", "retained adminBan 7 8199"}, {"mpAdminSwitch 7 8199", "retained adminSwitch 7 8199"},
                 {"mpAdminBans", "retained adminBans"}, {"mpAdminUnban", "retained adminUnban"}, {"mpAdminApply", "retained adminApply"}}) {
            s.HandleRetainedSessionRequest(card, verb);
            CHECK(gameObject.guiCommands.back() == std::string("play main_menu_selection ; ") + command && s.guiRetainedMultiplayer == card);
        }
        card->state["card.admin_ban_pick"] = "3"; s.HandleRetainedSessionRequest(card, "mpAdminBanPick");
        CHECK(gameObject.guiCommands.back() == "play main_menu_selection ; retained adminBanSelect 3");
        s.HandleRetainedSessionRequest(card, "mpAdminMap 3"); CHECK(gameObject.guiCommands.back() == "retained adminSet 0 3");
        s.HandleRetainedSessionRequest(card, "mpAdminControlTime 600"); CHECK(gameObject.guiCommands.back() == "retained adminSet 6 600");
        s.HandleRetainedSessionRequest(card, "mpAdminBuying 1"); CHECK(gameObject.guiCommands.back() == "retained adminSet 10 1");
        const size_t administered = gameObject.guiCommands.size();
        for (const char* row : {"-1", "32"}) {
            card->state["card.admin_row"] = row; s.HandleRetainedSessionRequest(card, "mpAdminSelect");
            card->state["card.admin_ban_pick"] = row; s.HandleRetainedSessionRequest(card, "mpAdminBanPick");
        }
        for (const char* bad : {"mpAdminKick 1", "mpAdminMap", "mpAdminMap x", "mpAdminKickPlayer", "mpAdminNope 1"}) {
            s.HandleRetainedSessionRequest(card, bad);
        }
        CHECK(gameObject.guiCommands.size() == administered && s.guiRetainedMultiplayer == card);
        card->state["card.admin.command"] = "status"; s.HandleRetainedSessionRequest(card, "mpAdminConsole");
        CHECK(mpMenu.state["admin_console_input"] == "status" && card->state["card.admin.command"].empty() &&
              gameObject.guiCommands.back() == "retained adminConsole");
        s.HandleRetainedSessionRequest(card, "mpAdminConsole"); CHECK(gameObject.guiCommands.back() == "retained adminConsole" &&
                                                                     gameObject.guiCommands.size() == administered + 1);
        card->state["card.referee_credential"] = "whistle"; s.HandleRetainedSessionRequest(card, "mpRefereeLogin");
        CHECK(mpMenu.state["match_referee_credential"] == "whistle" && card->state["card.referee_credential"].empty() &&
              gameObject.guiCommands.back() == "play main_menu_selection ; matchControl referee_login");
        card->state["mp.settings.name_edit"] = "^1Kane"; s.HandleRetainedSessionRequest(card, "mpSettingsName");
        CHECK(cvarSystemObject.strings["ui_name"] == "^1Kane" && cvarSystemObject.strings["gui_ui_name"] == "^1Kane");
        card->state["mp.settings.name_edit"] = ""; s.HandleRetainedSessionRequest(card, "mpSettingsName");
        card->state["mp.settings.clan_edit"] = std::string(13, 'c'); s.HandleRetainedSessionRequest(card, "mpSettingsClan");
        CHECK(cvarSystemObject.strings["ui_name"] == "^1Kane" && cvarSystemObject.strings.count("ui_clan") == 0);
        card->state["mp.settings.clan_edit"] = ""; s.HandleRetainedSessionRequest(card, "mpSettingsClan");
        CHECK(cvarSystemObject.strings.count("ui_clan") == 1 && cvarSystemObject.strings["ui_clan"].empty() &&
              gameObject.guiCommands.back() == "play main_menu_selection ; matchControl referee_login");
        gameObject.answer = nullptr;
        // A map change retires the card at once and opens the next map's card on
        // its first page.
        card->state["card.tab"] = "5"; s.PrepareRetainedLevel("mp/q4dm2", true);
        CHECK(s.guiRetainedMultiplayer == nullptr && card->state["card.tab"] == "0" && s.guiRetainedReleasing == nullptr);
        // The match summary takes the summary card, which opens on its first page
        // with the chat field at rest; its chat field asks the session to type,
        // and Back is the summary's own close. The menu replacing the summary
        // retires the summary card and covers the menu with its own.
        static idUserInterface summary("guis/summary.gui"); summary = idUserInterface("guis/summary.gui"); summary.state["gameDraw"] = "1";
        gameObject.publish = [](idUserInterface* gui) { gui->SetStateInt("mp.protocol", 4); gui->SetStateBool("mp.cover_allowed", true); };
        s.guiRetainedSummary->state["card.tab"] = "2"; s.guiActive = &summary; s.UpdateRetainedMultiplayer();
        auto* sheet = s.guiRetainedMultiplayer;
        CHECK(sheet == s.guiRetainedSummary && s.retainedMultiplayerSummary && s.RetainedSummaryCovers() && sheet->named.back() == "open" &&
              sheet->state["card.tab"] == "0" && s.summaryChatEnds == 1 && gameObject.commands.back() == "retainedMultiplayerCover");
        s.ReportRetainedScreens(); CHECK(commonObject.output.find(" mp=summary ") != std::string::npos);
        s.HandleRetainedSessionRequest(sheet, "mpSummaryChat"); CHECK(s.summaryChats == 1 && s.retainedSummaryTyping);
        gameObject.answer = [](const char*) -> const char* { return "continue"; };
        sheet->state["card.client"] = "0"; s.HandleRetainedSessionRequest(sheet, "mpSelectPlayer 3 8195");
        CHECK(gameObject.guiCommands.back() == "play main_menu_selection ; retained select 3 8195" && s.guiRetainedMultiplayer == sheet);
        s.guiActive = &mpMenu; s.UpdateRetainedMultiplayer(); s.UpdateRetainedMultiplayer();
        CHECK(s.guiRetainedMultiplayer == card && !s.retainedMultiplayerSummary && !s.retainedSummaryTyping && !sheet->active);
        s.guiActive = &summary; s.UpdateRetainedMultiplayer(); CHECK(s.guiRetainedMultiplayer == sheet);
        gameObject.answer = [](const char*) -> const char* { return nullptr; };
        s.HandleRetainedSessionRequest(sheet, "mpClose");
        CHECK(gameObject.guiCommands.back() == "play main_menu_selection ; close" && s.guiActive == nullptr && s.guiRetainedMultiplayer == nullptr &&
              sheet->named.back() == "release");
        s.HandleRetainedSessionRequest(card, "mpSummaryChat"); CHECK(s.summaryChats == 1);  // only over the summary
        s.guiRetainedReleasing = nullptr;
        // An Arena match's pause menu takes the Arena pause card, without the
        // Welcome question. Restart match and Leave match (after the card's own
        // questions) keep the menu up for the campaign; Resume closes it with
        // the release.
        static idUserInterface arenaMenu("guis/arena_pause.gui"); arenaMenu = idUserInterface("guis/arena_pause.gui"); arenaMenu.state["gameDraw"] = "1";
        gameObject.publish = [](idUserInterface* gui) { gui->SetStateInt("mp.protocol", 4); gui->SetStateBool("mp.cover_allowed", true); };
        const size_t arenaAsked = gameObject.commands.size();
        s.guiActive = &arenaMenu; s.UpdateRetainedMultiplayer();
        auto* arenaPause = s.guiRetainedMultiplayer;
        CHECK(arenaPause && arenaPause == s.guiRetainedArenaPause && std::string(arenaPause->Name()) == "guis/menu/arena_pause.q4ui" &&
              arenaPause->named.back() == "open" && gameObject.commands.size() == arenaAsked + 1 && gameObject.commands.back() == "retainedMultiplayerCover");
        gameObject.answer = [](const char*) -> const char* { return "continue"; };
        for (const auto& [verb, action] : std::vector<std::pair<const char*, const char*>>{{"arenaRestart", "restart"}, {"arenaLeave", "leave"}}) {
            s.HandleRetainedSessionRequest(arenaPause, verb);
            CHECK(gameObject.guiCommands.back() == std::string("play main_menu_selection ; arenaPause ") + action && s.guiRetainedMultiplayer == arenaPause);
        }
        gameObject.answer = [](const char*) -> const char* { return nullptr; };
        s.HandleRetainedSessionRequest(arenaPause, "resume");
        CHECK(gameObject.guiCommands.back() == "play main_menu_selection ; arenaPause resume" && s.guiActive == nullptr &&
              s.guiRetainedMultiplayer == nullptr && arenaPause->named.back() == "release");
        s.guiRetainedReleasing = nullptr;
        // The chat and buy GUIs are never covered; neither is a test GUI or a preview.
        s.guiActive = &chat; s.UpdateRetainedMultiplayer(); CHECK(s.guiRetainedMultiplayer == nullptr);
        s.guiActive = &mpMenu; idUserInterface probe("guis/probe.gui"); s.guiTest = &probe; s.UpdateRetainedMultiplayer(); CHECK(s.guiRetainedMultiplayer == nullptr);
        s.guiTest = nullptr; preview = true; s.UpdateRetainedMultiplayer(); CHECK(s.guiRetainedMultiplayer == nullptr);
        preview = false; s.multiplayer = false; s.UpdateRetainedMultiplayer(); CHECK(s.guiRetainedMultiplayer == nullptr);
        // The game may refuse now (the join offer, an Arena Campaign match): the
        // stock menu keeps this opening, and the next asks again.
        auto refused = Mp(true, true);
        gameObject.publish = [](idUserInterface* gui) { gui->SetStateInt("mp.protocol", 4); gui->SetStateBool("mp.cover_allowed", false); };
        refused.UpdateRetainedMultiplayer(); refused.UpdateRetainedMultiplayer();
        CHECK(refused.guiRetainedMultiplayer == nullptr && refused.retainedMultiplayerUncovered && gameObject.commands.size() == 2 && refused.retainedStock.Num() == 0);
        refused.guiActive = nullptr; refused.UpdateRetainedMultiplayer(); refused.guiActive = &mpMenu; refused.UpdateRetainedMultiplayer();
        CHECK(gameObject.commands.size() == 4);
        // A game module without the protocol keeps the stock menu for the session.
        auto old = Mp(true, true); gameObject.publish = nullptr;
        old.UpdateRetainedMultiplayer(); old.UpdateRetainedMultiplayer();
        CHECK(old.guiRetainedMultiplayer == nullptr && (Stock(old) == std::vector<std::string>{"guis/menu/mp_escape.q4ui"}) && gameObject.commands.size() == 2);
        // So does a mod's own multiplayer menu, and a card that stopped drawing.
        auto mod = Mp(true, true); cvarSystemObject.strings["fs_game"] = "mymod"; fileSystemObject.gameDirFiles["mymod"] = {"guis/mpmain.gui"};
        mod.UpdateRetainedMultiplayer();
        CHECK(mod.guiRetainedMultiplayer == nullptr && managerObject.loads.empty() && (Stock(mod) == std::vector<std::string>{"guis/menu/mp_escape.q4ui"}));
        auto failed = Mp(true, true); failed.UpdateRetainedMultiplayer(); auto* stopped = failed.guiRetainedMultiplayer; CHECK(stopped);
        failed.guiActive = nullptr; failed.UpdateRetainedMultiplayer(); stopped->failed = true; failed.guiActive = &mpMenu; failed.UpdateRetainedMultiplayer();
        CHECK(failed.guiRetainedMultiplayer == nullptr && commonObject.warnings.back().find("stopped drawing") != std::string::npos);
        auto missing = Mp(true, true); fileSystemObject.files.erase("guis/menu/mp_escape.q4ui"); missing.UpdateRetainedMultiplayer();
        CHECK(missing.guiRetainedMultiplayer == nullptr && commonObject.warnings.empty() &&
              (gameObject.commands == std::vector<std::string>{"retainedMultiplayerVariant"}));
        // Welcome: while the player has not answered the join offer the game
        // names the Welcome card, which covers the menu with its own verbs. A
        // Join page action reaches the game as "retained welcome <slot>" and
        // closes the menu when the game acts; its Settings tab hands off to the
        // join panel's settings button.
        welcomeAnswer = true;
        auto w = Mp(true, true); w.PrepareRetainedLevel("mp/q4dm1", true); w.UpdateRetainedMultiplayer();
        auto* welcome = w.guiRetainedMultiplayer;
        CHECK(welcome && welcome == w.guiRetainedWelcome && w.retainedMultiplayerWelcome && welcome->named.back() == "open");
        w.ReportRetainedScreens();
        CHECK(commonObject.output.find(" welcome=1 ") != std::string::npos && commonObject.output.find(" mp=welcome ") != std::string::npos);
        gameObject.answer = [](const char*) -> const char* { return nullptr; };
        welcome->state["card.welcome_action"] = "2"; w.HandleRetainedSessionRequest(welcome, "mpWelcomeAction");
        CHECK(gameObject.guiCommands.back() == "play main_menu_selection ; retained welcome 2" && w.guiRetainedMultiplayer == nullptr &&
              welcome->named.back() == "release");
        w.guiActive = &mpMenu; w.guiRetainedReleasing = nullptr; w.UpdateRetainedMultiplayer(); CHECK(w.guiRetainedMultiplayer == welcome);
        const size_t joined = gameObject.guiCommands.size();
        for (const char* slot : {"-1", "4"}) {
            welcome->state["card.welcome_action"] = slot; w.HandleRetainedSessionRequest(welcome, "mpWelcomeAction");
            CHECK(gameObject.guiCommands.size() == joined && w.guiRetainedMultiplayer == welcome &&
                  commonObject.warnings.back().find("join action") != std::string::npos);
        }
        gameObject.answer = nullptr;
        welcome->state["card.stock_page"] = "0"; w.HandleRetainedSessionRequest(welcome, "mpStockPage");
        CHECK(w.guiRetainedMultiplayer == welcome && !w.retainedMultiplayerUncovered);   // the Join page has no stock page
        mpMenu.state["initial_join"] = "1";   // the join offer stands: the join panel's settings
        welcome->state["card.stock_page"] = "3"; w.HandleRetainedSessionRequest(welcome, "mpStockPage");
        CHECK(legacyActions.back() == "qj_b_settings" && w.retainedMultiplayerUncovered && w.guiRetainedMultiplayer == nullptr);
        mpMenu.state["initial_join"] = "0";   // once it has closed: the menu column's settings
        w.guiActive = nullptr; w.UpdateRetainedMultiplayer(); w.guiActive = &mpMenu; w.UpdateRetainedMultiplayer();
        CHECK(w.guiRetainedMultiplayer == welcome);
        welcome->state["card.stock_page"] = "3"; w.HandleRetainedSessionRequest(welcome, "mpStockPage");
        CHECK(legacyActions.back() == "main_b_settings" && w.retainedMultiplayerUncovered);
        // Once the player answers, the game names Escape again.
        welcomeAnswer = false; w.guiActive = nullptr; w.UpdateRetainedMultiplayer(); w.guiActive = &mpMenu; w.UpdateRetainedMultiplayer();
        CHECK(w.guiRetainedMultiplayer == w.guiRetainedEscape && !w.retainedMultiplayerWelcome);
        // Each card has its own gate: with only ui_retained on, a card whose
        // pages still hand off leaves the stock menu to its openings.
        welcomeAnswer = true; auto gated = Mp(true, false); gated.UpdateRetainedMultiplayer();
        CHECK((gated.guiRetainedMultiplayer != nullptr) == (RETAINED_MP_WELCOME_MISSING_PAGES[0] == NULL));
        welcomeAnswer = false;
    }
    std::printf("ui_retained gate: %d checks passed\n", checks);
}
'''


# Stock SYSTEM rows that drive a setting through the session or a GUI
# variable rather than a cvar binding.
STOCK_SYSTEM_SESSION_ROWS = {'set_sys_screensize': 'r_mode', 'set_sys_specular': 'r_skipSpecular', 'set_sys_bump': 'r_skipBump',
                             'set_sys_sky': 'r_skipSky', 'set_sys_ambient': 'r_forceAmbient'}
# Stock SYSTEM controls that navigate the page instead of changing a setting.
STOCK_SYSTEM_PAGE_CONTROLS = {'set_sys_section_choice'}


def stock_system_settings() -> set[str]:
    """Every setting the stock SYSTEM page offers a control for."""
    gui = (ROOT / 'content/baseoq4/pak0/guis/menu/settings/system.gui').read_text(encoding='utf-8', errors='replace')
    gui = re.sub(r'/\*.*?\*/', '', re.sub(r'//[^\n]*', '', gui), flags=re.S)
    settings = set()
    definitions = list(re.finditer(r'\b(?:windowDef|choiceDef|sliderDef|editDef|listDef|bindDef)\s+(\w+)', gui))
    for index, definition in enumerate(definitions):
        if not re.match(r'(?:choiceDef|sliderDef|editDef)', definition.group(0)) or definition.group(1) in STOCK_SYSTEM_PAGE_CONTROLS:
            continue
        span = gui[definition.end():definitions[index + 1].start() if index + 1 < len(definitions) else len(gui)]
        cvar = re.search(r'\bcvar\s+"?(\w+)"?', span)
        row = re.match(r'(set_sys_\w+?)_val', definition.group(1))
        if cvar:
            if cvar.group(1) != 'gui_set_sys_scroll':
                settings.add(cvar.group(1))
        elif row and row.group(1) in STOCK_SYSTEM_SESSION_ROWS:
            settings.add(STOCK_SYSTEM_SESSION_ROWS[row.group(1)])
        else:
            raise AssertionError(f'stock SYSTEM control {definition.group(1)} binds no cvar; map it in STOCK_SYSTEM_SESSION_ROWS')
    return settings


# Retained controls whose value is a list's selection rather than the draft
# setting itself; the pick operation their action names writes the setting.
RETAINED_SYSTEM_DERIVED_STATE = {'settings.display.mode.selected': 'r_mode', 'settings.display.refresh.selected': 'r_displayRefresh',
                               'settings.render.backend': 'r_renderApi', 'settings.render.selected': 'r_renderDevice'}
# What each pick operation writes as its own choice: SystemDisplaySelectionPatch
# for the display lists, the preset expansion for its name. A plain edit writes
# its arguments. A control counts only for its operation's own writes.
OPERATION_WRITES = {'settings.system.display': {'r_screen'},
                    'settings.system.displayMode': {'r_mode', 'r_customWidth', 'r_customHeight'},
                    'settings.system.displayRefresh': {'r_displayRefresh'},
                    'settings.system.backend': {'r_renderApi'},
                    'settings.system.renderDevice': {'r_renderDevice', 'r_renderApi'},
                    'settings.system.preset': {'com_performancePreset'}}
# The automatic values a display or size pick also returns: a size the new
# display lacks to Desktop Native, a rate the new choice lacks to Auto.
OPERATION_RESETS = {'settings.system.display': {'r_mode', 'r_displayRefresh'},
                    'settings.system.displayMode': {'r_displayRefresh'}}


def retained_system_document() -> dict:
    text = (ROOT / 'content/baseoq4/pak0/guis/menu/settings/system.q4ui').read_text(encoding='utf-8')
    return json.loads(re.sub(r'^\s*//.*$', '', text, flags=re.M))


def control_settings(value) -> set[str]:
    """The settings a control value reads: drafts, and list selections standing for one."""
    found = set()

    def expressions(item):
        if isinstance(item, dict):
            state = item.get('state')
            if isinstance(state, str) and state.startswith('settings.draft.'):
                found.add(state[len('settings.draft.'):])
            elif isinstance(state, str) and state in RETAINED_SYSTEM_DERIVED_STATE:
                found.add(RETAINED_SYSTEM_DERIVED_STATE[state])
            for child in item.values():
                expressions(child)
        elif isinstance(item, list):
            for child in item:
                expressions(child)

    expressions(value)
    return found


def retained_system_controls() -> set[str]:
    """Every setting the retained SYSTEM page has a control for: the settings the
    values of controls that act read."""
    found = set()

    def walk(node):
        control = node.get('control')
        if isinstance(control, dict) and 'value' in control and (control.get('action') or control.get('event')):
            found.update(control_settings(control['value']))
        for child in node.get('children', []):
            walk(child)

    walk(retained_system_document()['root'])
    return found


def check_retained_system_control_writes() -> None:
    """A control counts for a setting only when its action writes that setting."""
    document = retained_system_document()

    def walk(node):
        control = node.get('control')
        if isinstance(control, dict) and 'value' in control and control.get('action'):
            action = document['actions'][control['action']]
            operation = action['operation']
            writes = set(action.get('arguments', {})) if operation == 'settings.system.edit' else OPERATION_WRITES.get(operation)
            assert writes is not None, f'{node["id"]}: unknown write set for {operation}'
            missing = control_settings(control['value']) - writes
            assert not missing, f'{node["id"]} reads {sorted(missing)} but {control["action"]} ({operation}) does not write them'
        for child in node.get('children', []):
            walk(child)

    walk(document['root'])
    # The service's display picks write exactly the settings named above.
    display = (ROOT / 'src/ui/application/SystemDisplay.cpp').read_text(encoding='utf-8')
    patch = display[display.rindex('bool SystemDisplaySelectionPatch('):]
    patch = patch[:patch.index('\n}\n')]
    for key in sorted(set().union(*(OPERATION_WRITES[o] | OPERATION_RESETS.get(o, set()) for o in
                                   ('settings.system.display', 'settings.system.displayMode', 'settings.system.displayRefresh')))):
        assert f'result["{key}"]' in patch, f'SystemDisplaySelectionPatch no longer writes {key}'
    rendering = (ROOT / 'src/ui/RenderingSettings.cpp').read_text(encoding='utf-8')
    for operation, method in (('settings.system.backend', 'Backend'), ('settings.system.renderDevice', 'Select')):
        body = function_body(rendering, f'bool RenderingSettings::{method}(')
        for key in OPERATION_WRITES[operation]:
            assert f'patch["{key}"]' in body, f'RenderingSettings::{method} no longer writes {key}'
    # Each branch writes what its operation is counted for: its own pick, and
    # the automatic values a display or size pick returns.
    device = patch[patch.index('if (list==SystemDisplayList::Device) {'):patch.index('} else if (list==SystemDisplayList::Mode) {')]
    mode = patch[patch.index('} else if (list==SystemDisplayList::Mode) {'):patch.rindex('} else {')]
    rate = patch[patch.rindex('} else {'):]
    assert 'result["r_screen"]=double(index);' in device, 'a display pick no longer writes r_screen'
    assert 'result["r_mode"]=-2.0;' in device and 'result["r_displayRefresh"]=0.0;' in device, 'a display pick no longer resets what the new display lacks'
    for key in ('r_mode', 'r_customWidth', 'r_customHeight'):
        assert f'result["{key}"]' in mode, f'a size pick no longer writes {key}'
    assert 'result["r_displayRefresh"]=0.0;' in mode, 'a size pick no longer resets a rate the new size lacks'
    assert 'result["r_displayRefresh"]=double(catalog.refresh[size_t(index)].rate);' in rate, 'a rate pick no longer writes its rate'


# ApplyClassOf, statement by statement: a change rides along when its setting
# is Immediate, persists an EngineRestart preference or is the preset marker;
# a display restart, r_renderer and r_lightGridPreload each have an executor of
# their own; any other effect, and any two of those classes together, make the
# batch Combined. retained_system_incomplete mirrors it.
APPLY_CLASS_STATEMENTS = ('if (item.effects==SystemSettingImmediate || item.effects==SystemSettingEngineRestart) continue;',
                          'if (item.effects==SystemSettingPresetExpansion) continue;',
                          'if (item.effects==SystemSettingDisplayRestart) display=true;',
                          'else if (item.key=="r_renderer") renderer=true;',
                          'else if (item.key=="r_lightGridPreload") deferred=true;',
                          'else combined=true;',
                          'if (combined || int(display)+int(renderer)+int(deferred)>1) return SystemApplyClass::Combined;')
# The combined executor. A Combined batch applies only while the service admits
# and dispatches it and the host prepares and recovers it; an effect applies
# through it only while CombinedPolicy gives that effect a domain in the
# record. A missing route or domain is a reason again, as Unsupported was.
COMBINED_ROUTE = {'src/ui/SettingsService.cpp': (
                      ('bool Supported(Service& service,std::uint64_t owner,bool* invalidDraft = nullptr) {',
                       'case SystemApplyClass::Combined:\n        if (!service.confirmationOwners.contains(owner) ||\n'
                       '            !service.device.SupportsCombined(service.transaction.Baseline(),service.transaction.Draft())) return false;\n        break;'),
                      ('bool UI_SettingsDispatch(std::uint64_t owner, const ActionInvocation& action, std::string& error) {',
                       'else if (kind == SystemApplyClass::Combined)\n                result = service.display.Apply(owner,Now(),')),
                  'src/ui/SettingsDisplayService.cpp': (
                      ('bool EngineSettingsDisplayHost::Prepare(const SettingsAttempt& attempt, std::string& error) {',
                       'if (combined) return PrepareCombined(attempt,error);'),
                      ('bool EngineSettingsDisplayHost::Startup(std::string& error) {',
                       'case SystemApplyClass::Combined: return StartupCombined(bytes,effect,error);'),
                      ('bool EngineSettingsDisplayHost::Restart(bool restoring, SettingsDisplayObservation& output, std::string& error) {',
                       'if (!R_RendererModule_TryDeviceRestart(&plan.request,diagnostic,sizeof(diagnostic))) return Fail(error,diagnostic);'),
                      ('bool EngineSettingsDisplayHost::Restart(bool restoring, SettingsDisplayObservation& output, std::string& error) {',
                       'RestartSound(restoring);'))}
COMBINED_DOMAINS = {'ImageReload': 'case SystemSettingImageReload: restore=&record.imageRestore; target=&record.imageTarget; break;',
                    'AudioRestart': 'case SystemSettingAudioRestart: restore=&record.audioRestore; target=&record.audioTarget; break;',
                    'NextMap': 'case SystemSettingNextMap: restore=&record.deferredRestore; target=&record.deferredTarget; break;',
                    'RendererResources': 'case SystemSettingRendererResources: restore=&record.resourceRestore; target=&record.resourceTarget; break;'}


def combined_effects() -> set[str]:
    """The effects the combined executor applies: none unless every route statement
    holds, then each effect CombinedPolicy still gives a domain."""
    for path, statements in COMBINED_ROUTE.items():
        source = (ROOT / path).read_text(encoding='utf-8')
        for signature, statement in statements:
            if statement not in function_body(source, signature):
                return set()
    policy = function_body((ROOT / 'src/ui/SettingsDisplayService.cpp').read_text(encoding='utf-8'),
                           'void CombinedPolicy(SettingsEffectRecoveryJournal& record) {')
    return {effect for effect, statement in COMBINED_DOMAINS.items() if statement in policy}
# Page states that enable a control only on some renderers, with the function
# that publishes each one and the statement that makes it hold on every renderer
# the stock page's control worked on (ui_settings_display_service.py proves the
# statement's behaviour). Any other form is a reason the control is incomplete.
RENDERER_LIMITED_STATES = {'settings.msaaAvailable': ('src/ui/SettingsDisplayService.cpp',
                                                      'bool EngineSettingsDisplayHost::SupportsMultisampling() const {',
                                                      'return WindowMultisampling() || R_RendererModule_GetStatus().activeApi == RENDER_MODULE_API_VULKAN;',
                                                      'Vulkan')}
# The engine reaches a Number field's text through the native text owner;
# while no engine code constructs one, typed characters never arrive.
NATIVE_TEXT_OWNER = ('src/ui/application/ManagedNativeTextOwner.h', 'src/ui/application/ManagedNativeTextOwner.cpp')


def typed_text_reaches_numbers(adapter: str) -> bool:
    """Whether typed characters can reach a retained Number field: an event-queue
    character route in the adapter (in HandleEvent itself or the Impl's
    TypeCharacter it calls), or engine code that drives the native text owner."""
    handle = function_body(adapter, 'const char* idUserInterfaceRetained::HandleEvent(')
    if 'SE_CHAR' in handle and ('ApplyNumberInput' in handle or 'ApplyTextInput' in handle):
        return True
    if 'SE_CHAR' in handle and 'TypeCharacter(' in handle and 'ApplyNumberInput' in function_body(adapter, 'void TypeCharacter('):
        return True
    owners = {ROOT / path for path in NATIVE_TEXT_OWNER}
    for folder in ('src/framework', 'src/sys', 'src/ui'):
        for path in (ROOT / folder).rglob('*'):
            if path.suffix in ('.cpp', '.h') and path not in owners and 'ManagedNativeTextOwner' in path.read_text(encoding='utf-8', errors='replace'):
                return True
    return False


def preset_targets() -> list[str]:
    """The settings a Performance Preset writes (PerformancePreset.cpp Targets)."""
    source = (ROOT / 'src/framework/PerformancePreset.cpp').read_text(encoding='utf-8')
    block = source[source.index('PerformancePresetTargetCount> Targets{{'):]
    return re.findall(r'"(\w+)"', block[:block.index('}};')])


def retained_system_catalog_effects() -> dict[str, str]:
    """Each catalog setting's effect as SystemSettingsHost::Catalog() declares it (lower-case keys)."""
    host = (ROOT / 'src/ui/application/SystemSettingsHost.cpp').read_text(encoding='utf-8')
    body = function_body(host, 'const std::vector<SystemSettingDescriptor>& SystemSettingsHost::Catalog() {')
    starts = list(re.finditer(r'\b(?:String|Boolean|Number)\("(\w+)"', body))
    assert starts, 'SystemSettingsHost::Catalog() declares no settings'
    effects = {}
    for index, start in enumerate(starts):
        span = body[start.end():starts[index + 1].start() if index + 1 < len(starts) else len(body)]
        named = re.findall(r'\bSystemSetting(\w+)\b', span)
        assert len(named) <= 1, f'{start.group(1)} declares more than one effect'
        effects[start.group(1).lower()] = named[0] if named else 'Immediate'
    return effects


def retained_system_incomplete(adapter: str) -> dict[str, str]:
    """The stock settings the retained SYSTEM page has a control for that cannot yet
    do what the stock page did, each with its reason, as the code states them."""
    document = retained_system_document()
    stock = {name.lower() for name in stock_system_settings()}
    enabled = {binding['node']: binding['value'] for binding in document['bindings'] if binding.get('property') == 'enabled'}
    roles: dict[str, set[str]] = {}
    limited: dict[str, set[str]] = {}

    def states(value) -> set[str]:
        found = set()
        if isinstance(value, dict):
            if isinstance(value.get('state'), str):
                found.add(value['state'])
            for child in value.values():
                found |= states(child)
        elif isinstance(value, list):
            for child in value:
                found |= states(child)
        return found

    def walk(node):
        control = node.get('control')
        if isinstance(control, dict) and 'value' in control:
            assert enabled.get(node['id'], True) is not False, f'{node["id"]} is never enabled'
            for key in control_settings(control['value']):
                key = key.lower()
                roles.setdefault(key, set()).add(control['role'])
                for state in states(enabled.get(node['id'])) & set(RENDERER_LIMITED_STATES):
                    limited.setdefault(key, set()).add(state)
        for child in node.get('children', []):
            walk(child)

    walk(document['root'])
    host = (ROOT / 'src/ui/application/SystemSettingsHost.cpp').read_text(encoding='utf-8')
    apply_class = function_body(host, 'SystemApplyClass SystemSettingsHost::ApplyClassOf(')
    for statement in APPLY_CLASS_STATEMENTS:
        assert statement in apply_class, f'ApplyClassOf changed ({statement}); update APPLY_CLASS_STATEMENTS and the mirror below'
    effects = retained_system_catalog_effects()
    typed = typed_text_reaches_numbers(adapter)
    combined = combined_effects()

    def applies(key: str) -> bool:
        return (effects[key] in ('Immediate', 'EngineRestart', 'PresetExpansion', 'DisplayRestart') or
                key in ('r_renderer', 'r_lightgridpreload') or effects[key] in combined)

    def apply_class(key: str) -> str:
        if effects[key] in ('Immediate', 'EngineRestart', 'PresetExpansion'):
            return 'immediate'
        return 'display' if effects[key] == 'DisplayRestart' else key if key in ('r_renderer', 'r_lightgridpreload') else 'combined'

    targets = [target.lower() for target in preset_targets()]
    reasons = {}
    for key in sorted(stock & set(roles)):
        assert key in effects, f'{key} has a retained control but no SystemSettingsHost catalog entry'
        if key == 'com_performancepreset':
            # A preset writes its targets: Apply needs every one to apply, and
            # the combined executor for the classes they mix.
            assert targets and all(target in effects for target in targets), 'a preset target has no SystemSettingsHost catalog entry'
            blocked = sorted(target for target in targets if not applies(target))
            classes = {apply_class(target) for target in targets if applies(target)} - {'immediate'}
            if blocked or not applies(key) or (len(classes) > 1 and not combined):
                reasons[key] = (f'a preset writes {", ".join(blocked) or "settings"} whose effects have no apply path, '
                                'or mixes apply classes without the combined executor')
        elif not applies(key):
            reasons[key] = f'its {effects[key]} effect has no apply path (no executor of its own and no combined domain)'
        elif not typed and roles[key] == {'number'}:
            reasons[key] = 'only number fields offer it, and typed characters reach no retained Number field'
        else:
            for state in sorted(limited.get(key, ())):
                path, signature, statement, renderer = RENDERER_LIMITED_STATES[state]
                if statement not in function_body((ROOT / path).read_text(encoding='utf-8'), signature):
                    reasons[key] = f'its control needs {state}, which {signature.split("::")[-1].split("(")[0]} does not publish for {renderer}'
    # The stock page's Auto-Detect is a button, not a setting row; the retained
    # page keeps its own.
    assert any(action['operation'] == 'settings.system.autodetect' for action in document['actions'].values()), 'the retained SYSTEM page lost Auto-Detect'
    return reasons


def player_setting_rules(adapter: str) -> dict:
    """The adapter's player settings (FindPlayerSetting): each setting's list
    of values, by the name its rule gives (empty for a range or a Boolean)."""
    body = function_body(adapter, 'const PlayerSetting* FindPlayerSetting(')
    lists = {name: re.findall(r'"([^"]*)"', items) for name, items in re.findall(r'static const std::vector<std::string> (\w+) = \{([^}]*)\};', body)}
    rules = {}
    for cvar, rule in re.findall(r'\{"(\w+)",\{([^{}]*(?:\{[^}]*\}[^{}]*){0,2})\}\}', body):
        named = re.search(r',(\w+)$', rule.strip())
        inline = re.search(r'\{([^}]*)\}', rule)
        rules[cvar] = lists[named.group(1)] if named and named.group(1) in lists else re.findall(r'"([^"]*)"', inline.group(1)) if inline else []
    return rules


def cpp_allowlist(text: str, function: str = 'bool SessionMenuCommand(') -> set[str]:
    block = function_body(text, function)
    return set(re.findall(r'"([A-Za-z][A-Za-z0-9]*)"', block.split('{', 2)[2]))


FACES = r'''
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
'''

FACES_MAIN = r'''
int main() {
    for (const char* family : {"marine", "lowpixel", "profont", "r_strogg", "strogg", "chain", "Marine", "unknown"})
        std::printf("family %s %s\n", family, FontFamily(family).c_str());
    unsigned scalar = 0;
    while (std::cin >> scalar) std::printf("%u %u\n", scalar, static_cast<unsigned>(RuneScalar(scalar)));
    return 0;
}
'''


def check_strogg_faces(compiler: str, directory: Path) -> int:
    """The Strogg pause names the r_strogg and strogg faces, and every label's
    rune copy draws through the host's fold. Compile both host functions and
    check that the faces resolve to themselves, and that every code point of
    the shipped string tables, and every code point below U+FFFF, folds to a
    code point the rune face covers or to a space."""
    host = (ROOT / 'src/ui/RetainedUI.cpp').read_text(encoding='utf-8')
    source = directory / 'faces.cpp'
    binary = directory / 'faces.exe'
    source.write_text(FACES + function_body(host, 'static std::string FontFamily(') + '\n' +
                      function_body(host, 'static std::uint32_t RuneScalar(') + FACES_MAIN, encoding='utf-8')
    subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', str(source), '-o', str(binary)], check=True)
    shipped = set()
    for table in (ROOT / 'content/baseoq4/pak0/strings').glob('*.lang'):
        shipped.update(ord(ch) for ch in table.read_text(encoding='utf-8'))
    scalars = sorted(shipped | {s for s in range(0x20, 0x10000) if not 0xD800 <= s <= 0xDFFF})
    result = subprocess.run([str(binary)], input='\n'.join(map(str, scalars)), capture_output=True, text=True, timeout=60, check=True)
    lines = result.stdout.splitlines()
    families = dict(line.split()[1:] for line in lines if line.startswith('family '))
    assert families == {'marine': 'marine', 'lowpixel': 'lowpixel', 'profont': 'profont', 'r_strogg': 'r_strogg', 'strogg': 'strogg',
                        'chain': 'chain', 'Marine': 'chain', 'unknown': 'chain'}, families
    runes = font_code_points(ROOT / 'content/baseoq4/pak0/fonts/strogg.ttf')
    folded = dict(tuple(map(int, line.split())) for line in lines if not line.startswith('family '))
    assert set(folded) == set(scalars)
    missing = sorted(hex(s) for s, f in folded.items() if f != 0x20 and f not in runes)
    assert not missing, f'rune copies would draw the face\'s ? for {missing[:16]}'
    assert all(folded[s] == s for s in range(0x41, 0x5B)) and all(folded[s] == s for s in range(0x30, 0x3A)), 'letters and digits keep their runes'
    for blank in (0x21, 0x2019, 0x2026, 0x300, 0x3000):
        assert folded[blank] == 0x20, f'punctuation U+{blank:04X} must be blank in the runes'
    return len(scalars)


CARD = r"""
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
static int checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "check failed line %d: %s\n", __LINE__, #x); std::abort(); } } while (false)
#define BIT(n) (1 << (n))
#define S_ESCAPE_UNKNOWN BIT(0)
#define S_ESCAPE_COLOR BIT(1)
#define S_ESCAPE_COLORINDEX BIT(2)
#define S_ESCAPE_ICON BIT(3)
#define S_ESCAPE_COMMAND BIT(4)
#define S_ESCAPE_ALL ( S_ESCAPE_COLOR | S_ESCAPE_COLORINDEX | S_ESCAPE_ICON | S_ESCAPE_COMMAND )
const int C_COLOR_ESCAPE = '^';
class idStr {
public:
    std::string s;
    idStr() = default;
    idStr(const char* text) : s(text ? text : "") {}
    int Length() const { return static_cast<int>(s.size()); }
    const char* c_str() const { return s.c_str(); }
    char operator[](int i) const { return i >= 0 && i < Length() ? s[i] : '\0'; }
    idStr& operator+=(char c) { s += c; return *this; }
    idStr& operator+=(const char* text) { s += text; return *this; }
    idStr& operator+=(const idStr& text) { s += text.s; return *this; }
    void Append(const char* text, int length) { s.append(text, length); }
    void CapLength(int length) { if (length < Length()) s.resize(length); }
    void Insert(char c, int index) { s.insert(s.begin() + index, c); }
    static int IsEscape(const char* s, int* type = nullptr);
    static char* RemoveEscapes(char* string, int escapes = S_ESCAPE_ALL);
    // As Str.h: the static form over the buffer, then the length again.
    idStr& RemoveEscapes(int escapes = S_ESCAPE_ALL) {
        std::vector<char> buffer(s.begin(), s.end()); buffer.push_back('\0');
        RemoveEscapes(buffer.data(), escapes); s = buffer.data(); return *this;
    }
    static int Icmpn(const char* a, const char* b, int n) {
        for (int i = 0; i < n; ++i) {
            const int x = std::tolower(static_cast<unsigned char>(a[i])), y = std::tolower(static_cast<unsigned char>(b[i]));
            if (x != y) return x - y;
            if (!x) return 0;
        }
        return 0;
    }
    static int Icmp(const char* a, const char* b) { return Icmpn(a, b, 1 << 30); }
};
struct idDict {
    std::map<std::string, std::string> values;
    void Set(const char* key, const char* value) { values[key] = value; }
    const char* GetString(const char* key, const char* fallback = "") const {
        const auto found = values.find(key); return found == values.end() ? fallback : found->second.c_str();
    }
};
struct idUserInterface {
    std::map<std::string, std::string> state; int changes = 0;
    void SetStateString(const char* key, const char* value) { state[key] = value; }
    void SetStateBool(const char* key, bool value) { state[key] = value ? "1" : "0"; }
    void SetStateInt(const char* key, int value) { state[key] = std::to_string(value); }
    struct View {
        const std::map<std::string, std::string>& values;
        bool GetBool(const char* key) const { const auto found = values.find(key); return found != values.end() && std::atoi(found->second.c_str()) != 0; }
    };
    View State() const { return {state}; }
    void StateChanged(int) { ++changes; }
};
// The game module: game_mp answers the verb from the server info it is given.
struct Game {
    bool answers = true; std::vector<std::string> verbs;
    void HandleMainMenuCommands(const char* verb, idUserInterface* gui) {
        verbs.push_back(verb);
        if (!answers || std::strcmp(verb, "retainedLoadingServer")) return;
        const std::string mode = gui->state["query_si_gameType"];
        const bool teams = mode == "Team DM" || mode == "CTF";
        gui->SetStateString("server_gametype", teams ? "Team Deathmatch" : "Deathmatch");
        gui->SetStateString("server_limit", ("Frag Limit " + gui->state["query_si_fragLimit"]).c_str());
        gui->SetStateBool("server_team_mode", teams);
        gui->SetStateBool("server_answered", true);
    }
} gameObject, *game = &gameObject;
struct LangDict {
    const char* GetString(const char* key) const {
        static const std::map<std::string, std::string> table = {
            {"#str_230070", "+%d more"}, {"#str_230071", "Connecting: %d"}, {"#str_230072", "Spectating: %d"},
            {"#str_231533", "%s \xc2\xb7 %s"}};
        const auto found = table.find(key); return found == table.end() ? key : found->second.c_str();
    }
} languageObject;
struct Common {
    std::string output;
    const LangDict* GetLanguageDict() { return &languageObject; }
    int GetPresentationTime() { return 0; }
    void Printf(const char* fmt, ...) { char text[1024]; va_list args; va_start(args, fmt); std::vsnprintf(text, sizeof(text), fmt, args); va_end(args); output += text; }
} commonObject, *common = &commonObject;
struct CVarSystem { bool GetCVarBool(const char*) { return true; } } cvarSystemObject, *cvarSystem = &cvarSystemObject;
// idlib's va: four rotating buffers, so one call may nest another.
static const char* va(const char* fmt, ...) {
    static char buffers[4][1024]; static int index = 0; char* text = buffers[index++ & 3];
    va_list args; va_start(args, fmt); std::vsnprintf(text, 1024, fmt, args); va_end(args); return text;
}
class idSessionLocal {
public:
    idUserInterface* guiLoading = nullptr;
    static const int RETAINED_ROSTER_SLOTS = @SLOTS@;
    idStr retainedRosterName[RETAINED_ROSTER_SLOTS];
    int retainedRosterTeam[RETAINED_ROSTER_SLOTS] = {};
    int retainedRosterCount = 0, retainedRosterConnecting = 0;
    bool retainedRosterKnown = false;
    void ClearRetainedLoadingRoster(); void NoteRetainedLoadingPlayer(const idDict&); void NoteRetainedLoadingConnecting(int);
    void PublishRetainedLoadingServer(const idDict&);
};
"""

CARD_MAIN = r"""
static idDict Player(const char* name, const char* team = "Marine", const char* spectate = "Play") {
    idDict info; info.Set("ui_name", name); info.Set("ui_team", team); info.Set("ui_spectate", spectate); return info;
}
static std::string Clean(const char* text, int bytes = 256, int lines = 3) { return Session_RetainedPlayerText(text, bytes, lines).s; }
int main() {
    // The sanitizer: Quake 4's own escape grammar, then lines, bytes and UTF-8.
    CHECK(Clean("^1Red^7Name") == "RedName");
    CHECK(Clean("^c683Marine^iabcX^nqY^r") == "MarineXY");
    CHECK(Clean("100^^ fun") == "100^ fun");
    CHECK(Clean("One\nTwo\nThree\nFour", 256, 3) == "One\nTwo\nThree Four");
    CHECK(Clean("\n\nHello\n\n") == "Hello");
    CHECK(Clean("C:\\maps /n") == "C:\\maps /n");  // a backslash is text, as the console never sends one
    CHECK(Clean("Tab\there\x01\x7f!") == "Tab here!");
    CHECK(Clean("Line\none\ntwo", 256, 1) == "Line one two");
    CHECK(Clean("\xc3\x84\xc3\x84\xc3\x84", 5) == "\xc3\x84\xc3\x84");
    CHECK(Clean("a\xc3(b\xe2\x82") == "a(b");
    CHECK(Clean("\x80\xbf ok \xf8!") == "ok !");
    CHECK(Clean("#str_200197") == " #str_200197");
    CHECK(Clean("^2#str_1") == " #str_1");
    CHECK(Clean(nullptr).empty() && Clean("^7").empty());
    // At most the bytes asked for, never a split character, never more lines.
    for (int bytes = 0; bytes < 12; ++bytes) {
        const std::string text = Clean("Zo\xc3\xab \xd0\x96\xe2\x82\xac\xf0\x9f\x98\x80!", bytes);
        CHECK(static_cast<int>(text.size()) <= bytes);
        for (size_t i = 0; i < text.size(); ) {
            const unsigned char lead = static_cast<unsigned char>(text[i]);
            const size_t length = lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : lead >= 0xc0 ? 2 : 1;
            CHECK(lead < 0x80 || lead >= 0xc0);
            CHECK(i + length <= text.size());
            i += length;
        }
    }
    idUserInterface loading; idSessionLocal session; session.guiLoading = &loading;
    idDict server; server.Set("si_gameType", "Team DM"); server.Set("si_fragLimit", "30");
    server.Set("si_motd", "^3Welcome^7 to the ^1test^7 server\nBe excellent");
    // A team match: three marines, two strogg, a spectator and two connecting.
    session.ClearRetainedLoadingRoster();
    for (const char* name : {"^1Kane", "Rhodes", "Strauss"}) session.NoteRetainedLoadingPlayer(Player(name));
    session.NoteRetainedLoadingPlayer(Player("Makron", "Strogg"));
    session.NoteRetainedLoadingPlayer(Player("Gladiator", "strogg"));
    session.NoteRetainedLoadingPlayer(Player("Watcher", "Marine", "Spectate"));
    session.NoteRetainedLoadingPlayer(Player("^7"));  // a name that is only escapes
    session.NoteRetainedLoadingConnecting(2);
    session.PublishRetainedLoadingServer(server);
    CHECK(gameObject.verbs.size() == 1 && gameObject.verbs[0] == "retainedLoadingServer");
    CHECK(loading.state["query_si_gameType"] == "Team DM" && loading.state["query_si_fragLimit"] == "30");
    CHECK(loading.state["server_gametype"] == "Team Deathmatch" && loading.state["server_limit"] == "Frag Limit 30");
    CHECK(loading.state["server_roster_known"] == "1" && loading.state["server_team_mode"] == "1");
    CHECK(loading.state["server_team_a"] == "Kane\nRhodes\nStrauss" && loading.state["server_team_b"] == "Makron\nGladiator");
    CHECK(loading.state["server_players"].empty() && loading.state["server_roster_rows"] == "3");
    CHECK(loading.state["server_roster_extra"] == "Spectating: 1 \xc2\xb7 Connecting: 2");
    CHECK(loading.state["server_message"] == "Welcome to the test server\nBe excellent");
    CHECK(loading.changes == 1);
    CHECK(commonObject.output.find("RETAINED_LOADING_SERVER answered=1 teams=1 players=5 spectators=1 connecting=2 rows=3 message=") != std::string::npos);
    // Each load captures its own roster.
    CHECK(session.retainedRosterCount == 0 && session.retainedRosterConnecting == 0 && !session.retainedRosterKnown);
    // Eight marines: six names, then "+2 more" as the seventh row.
    for (int i = 0; i < 8; ++i) session.NoteRetainedLoadingPlayer(Player(("M" + std::to_string(i)).c_str()));
    session.PublishRetainedLoadingServer(server);
    CHECK(loading.state["server_team_a"] == "M0\nM1\nM2\nM3\nM4\nM5\n+2 more" && loading.state["server_team_b"].empty());
    CHECK(loading.state["server_roster_rows"] == "7" && loading.state["server_roster_extra"].empty());
    // Outside team modes one list holds everyone, whatever their ui_team.
    server.Set("si_gameType", "DM");
    session.NoteRetainedLoadingPlayer(Player("A")); session.NoteRetainedLoadingPlayer(Player("B", "Strogg"));
    session.PublishRetainedLoadingServer(server);
    CHECK(loading.state["server_team_mode"] == "0" && loading.state["server_players"] == "A\nB" && loading.state["server_roster_rows"] == "2");
    // A game that does not answer keeps the session's strings and one list.
    gameObject.answers = false; loading.state["server_gametype"] = "Team DM (session)";
    server.Set("si_gameType", "Team DM");
    session.NoteRetainedLoadingPlayer(Player("A")); session.NoteRetainedLoadingPlayer(Player("B", "Strogg"));
    session.PublishRetainedLoadingServer(server);
    CHECK(loading.state["server_gametype"] == "Team DM (session)" && loading.state["server_team_mode"] == "0");
    CHECK(loading.state["server_players"] == "A\nB" && loading.state["server_team_a"].empty());
    gameObject.answers = true;
    // A load that knows no players (a fresh connection) shows none.
    session.PublishRetainedLoadingServer(server);
    CHECK(loading.state["server_roster_known"] == "0" && loading.state["server_roster_rows"] == "0");
    // Only players still connecting: the line stands alone.
    session.NoteRetainedLoadingConnecting(3);
    session.PublishRetainedLoadingServer(server);
    CHECK(loading.state["server_roster_known"] == "1" && loading.state["server_roster_rows"] == "0" &&
          loading.state["server_roster_extra"] == "Connecting: 3");
    // The store keeps at most its slots, and long names stop at 48 bytes.
    for (int i = 0; i < 40; ++i) session.NoteRetainedLoadingPlayer(Player(std::string(60, 'x').c_str()));
    CHECK(session.retainedRosterCount == idSessionLocal::RETAINED_ROSTER_SLOTS && session.retainedRosterName[0].Length() == 48);
    session.ClearRetainedLoadingRoster();
    // No message, no line.
    server.Set("si_motd", "^7");
    session.PublishRetainedLoadingServer(server);
    CHECK(loading.state["server_message"].empty());
    std::printf("ui_retained gate: %d server card checks passed\n", checks);
    return 0;
}
"""


def check_server_card(compiler: str, directory: Path) -> None:
    """The loading server card's players and message (section 14.17): compile
    the production sanitizer, roster store and publish with idlib's own escape
    parser, then pin where the network code captures the roster and where the
    game resolves the mode."""
    session = (ROOT / 'src/framework/Session.cpp').read_text(encoding='utf-8')
    header = (ROOT / 'src/framework/Session_local.h').read_text(encoding='utf-8')
    strings = (ROOT / 'src/idlib/Str.cpp').read_text(encoding='latin-1')
    slots = re.search(r'static const int\s+RETAINED_ROSTER_SLOTS = (\d+);', header).group(1)
    rows = re.search(r'static const int RETAINED_ROSTER_ROWS = \d+;', session).group(0)
    bodies = [function_body(strings, 'int idStr::IsEscape( const char *s, int* type )'),
              function_body(strings, 'char *idStr::RemoveEscapes( char *string, int escapes )'),
              function_body(session, 'idStr Session_RetainedPlayerText('), function_body(session, 'idStr Session_RetainedList('), rows]
    bodies += [function_body(session, f'void idSessionLocal::{name}(') for name in (
        'ClearRetainedLoadingRoster', 'NoteRetainedLoadingPlayer', 'NoteRetainedLoadingConnecting', 'PublishRetainedLoadingServer')]
    source = directory / 'card.cpp'
    binary = directory / 'card.exe'
    source.write_text(CARD.replace('@SLOTS@', slots) + '\n'.join(bodies) + CARD_MAIN, encoding='utf-8')
    subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Wno-unused-function', str(source), '-o', str(binary)], check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    assert result.returncode == 0, 'the server card checks failed'
    # The generator's roster rows match the session's.
    generator = (ROOT / 'tools/ui/build_retained_screens.py').read_text(encoding='utf-8')
    assert re.search(r'^SERVER_ROSTER_ROWS = (\d+)$', generator, re.M).group(1) == re.search(r'= (\d+);', rows).group(1)
    # Every language has the roster lines, each with one count, and the
    # longest forms fit: "+N more" in a team column, spectators and players
    # connecting together across the card (Lowpixel, as the card sets them).
    sys.path.insert(0, str(ROOT / 'tools/ui'))
    import build_retained_screens as layout
    card_width = layout.U * 250 - 30
    for table in sorted((ROOT / 'content/baseoq4/pak0/strings').glob('*_openq4.lang')):
        text = table.read_text(encoding='utf-8')
        lines = {key: re.search(rf'"{key}"\s+"([^"]*)"', text) for key in ('#str_230070', '#str_230071', '#str_230072')}
        assert all(lines.values()), f'{table.name} lacks the server card roster lines'
        more, connecting, spectating = (lines[key].group(1) for key in ('#str_230070', '#str_230071', '#str_230072'))
        assert all(line.count('%d') == 1 and line.count('%') == 1 for line in (more, connecting, spectating)), table.name
        assert layout.text_width('lowpixel', more % 26, 14) <= (card_width - 12) / 2, f'{table.name}: "+N more" overflows a team column'
        extra = f'{spectating % 31} \u00b7 {connecting % 31}'
        assert layout.text_width('lowpixel', extra, 13) <= card_width, f'{table.name}: the spectators line overflows the card'
        # The formats the session fills in: two parts of a line as the
        # language joins a list (Session_RetainedList), the time in the
        # mission, the load time's whole and tenth seconds, and a save's date
        # and time. Each takes exactly its arguments, in order.
        for key, arguments in (('#str_231532', ['%s']), ('#str_231533', ['%s', '%s']), ('#str_231534', ['%d', '%d']),
                               ('#str_231535', ['%s', '%s'])):
            found = re.search(rf'"{key}"\s+"([^"]*)"', text)
            assert found and re.findall(r'%.', found.group(1)) == arguments, f'{table.name}: {key} must take {arguments}'
    # Where the roster comes from. The listen host notes everyone in the game,
    # bots included, before InitClient wipes their user info and before the
    # map loads; a client notes the players it knows on a map change before
    # InitGame clears them, and a fresh connection starts from none.
    server = (ROOT / 'src/framework/async/AsyncServer.cpp').read_text(encoding='utf-8')
    change = function_body(server, 'void idAsyncServer::ExecuteMapChange(')
    assert change.index('sessLocal.ClearRetainedLoadingRoster();') < change.index('InitClient( i, clients[i].clientId, clients[i].clientRate );')
    assert change.index('sessLocal.NoteRetainedLoadingConnecting( retainedConnecting );') < change.index('sessLocal.ExecuteMapChange();')
    assert change.index('InitLocalClient( 0, false );') < change.index('sessLocal.ClearRetainedLoadingRoster();')
    assert 'if ( i == localClientNum || clients[i].clientState == SCS_INGAME ) {' in change
    client = (ROOT / 'src/framework/async/AsyncClient.cpp').read_text(encoding='utf-8')
    unreliable = function_body(client, 'void idAsyncClient::ProcessUnreliableServerMessage(')
    gameinit = unreliable[unreliable.index('case SERVER_UNRELIABLE_MESSAGE_GAMEINIT:'):]
    assert gameinit.index('sessLocal.NoteRetainedLoadingPlayer( sessLocal.mapSpawnData.userInfo[ slot ] );') < \
        gameinit.index('InitGame( serverGameInitId, serverGameFrame, serverGameTime, serverSI );') < gameinit.index('sessLocal.ExecuteMapChange();')
    response = function_body(client, 'void idAsyncClient::ProcessConnectResponseMessage(')
    assert response.index('msg.ReadDeltaDict( serverSI, NULL );') < response.index('msg.IsReadOverflowed() || serverClientNum < 0') < \
        response.index('sessLocal.ClearRetainedLoadingRoster();') < response.index('AsyncClient_ReadConnectRoster( msg );') < \
        response.index('InitGame( serverGameInitId, serverGameFrame, serverGameTime, serverSI );') < response.index('sessLocal.ExecuteMapChange();')
    # A fresh connection learns the players from an optional block after the
    # server info: the server writes it before sending, and the client reads
    # it only behind its tag and length, so neither side breaks the other.
    connect = function_body(server, 'void idAsyncServer::ProcessConnectMessage(')
    assert connect.index('outMsg.WriteDeltaDict( sessLocal.mapSpawnData.serverInfo, NULL );') < \
        connect.index('AsyncServer_WriteConnectRoster( outMsg, clients, clientNum );') < connect.rindex('serverPort.SendPacket( from, outMsg.GetData(), outMsg.GetSize() );')
    writer = function_body(server, 'static void AsyncServer_WriteConnectRoster(')
    for token in ('if ( i == joiner ) {', 'clients[ i ].clientState == SCS_INGAME', 'NA_BOT ? idConnectRoster::FLAG_BOT',
                  'msg.GetRemainingSpace() >= size + 6', 'msg.WriteLong( static_cast<int>( idConnectRoster::TAG ) );'):
        assert token in writer, token
    reader = function_body(client, 'static void AsyncClient_ReadConnectRoster(')
    for token in ('msg.GetRemainingReadBits() < 48 || msg.ReadLong() != static_cast<int>( idConnectRoster::TAG )',
                  'size > static_cast<int>( idConnectRoster::MAX_BYTES ) || msg.GetRemainingReadBits() < size * 8',
                  '!idConnectRoster::Decode( block, size, roster )', 'sessLocal.NoteRetainedLoadingConnecting( roster.connecting );'):
        assert token in reader, token
    assert 'DisconnectFromServer' not in reader and 'disconnect' not in reader, 'a bad roster block must never end the connection'
    # The session publishes the card only on the retained screen, after the
    # stock strings it falls back to.
    loading = function_body(session, 'void idSessionLocal::LoadLoadingGui(')
    assert loading.index('guiLoading->SetStateString( "server_limit", limitText.c_str() );') < loading.index(
        'PublishRetainedLoadingServer( mapSpawnData.serverInfo );')
    assert 'if ( retainedLoading ) {\n\t\t\t\tPublishRetainedLoadingServer( mapSpawnData.serverInfo );' in loading.replace('\r\n', '\n')
    # game_mp names the mode and its limits from its own table, with the
    # rules the scoreboard uses, and the message is server info.
    mp = (ROOT / 'src/mpgame/Game_local.cpp').read_text(encoding='utf-8')
    verb = mp[mp.index('if ( !idStr::Icmp( menuCommand, "retainedLoadingServer" ) ) {'):]
    verb = verb[:verb.index('} else if ( !idStr::Icmp( menuCommand, "initCreateServerSettings" ) ) {')]
    for token in ('MPGameTypeByName( query.GetString( "si_gameType" ) )', 'MPResolveMatchLimitFor( query, info->type, limitLabel, limitValue );',
                  'MPGameTypeHasAny( info->type, GTF_TEAM )', 'common->GetLocalizedString( info->localizedName )',
                  'gui->SetStateBool( "server_answered", true );'):
        assert token in verb, token
    rules = (ROOT / 'src/mpgame/MultiplayerGame.cpp').read_text(encoding='utf-8')
    assert 'MPResolveMatchLimitFor( gameLocal.serverInfo, gameLocal.gameType, limitLabel, limitValue );' in \
        function_body(rules, 'static void MPResolveMatchLimit(')
    for module in ('src/mpgame/gamesys/SysCvar.cpp', 'src/game/gamesys/SysCvar.cpp'):
        cvars = (ROOT / module).read_text(encoding='utf-8')
        assert re.search(r'idCVar si_motd\(\s+"si_motd",\s+"",\s+CVAR_GAME \| CVAR_SERVERINFO \| PC_CVAR_ARCHIVE', cvars), module


HOLD = r"""
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
static int checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "check failed line %d: %s\n", __LINE__, #x); std::abort(); } } while (false)
struct idStr {
    static int Icmp(const char* a, const char* b) {
        for (;; ++a, ++b) {
            const int x = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a, y = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
            if (x != y) return x - y;
            if (!x) return 0;
        }
    }
};
struct idUserInterface {
    std::string name; bool gameDraw = false; std::vector<std::string> events;
    idUserInterface(const char* path, bool draw = false) : name(path), gameDraw(draw) {}
    struct View { bool gameDraw; bool GetBool(const char* key) const { return !std::strcmp(key, "gameDraw") && gameDraw; } };
    View State() const { return {gameDraw}; }
    const char* Name() const { return name.c_str(); }
    void HandleNamedEvent(const char* event) { events.push_back(event); }
};
struct idStrList {
    std::vector<std::string> items;
    int FindIndex(const char* text) const {
        for (size_t i = 0; i < items.size(); ++i) if (items[i] == text) return static_cast<int>(i);
        return -1;
    }
};
static const char* const RETAINED_MP_WELCOME_GUI = "guis/menu/mp_welcome.q4ui";
static bool welcomeEnabled = true, modMenu = false, retainedOpen = false;
static bool Session_RetainedMultiplayerEnabled(bool welcome) { return welcome && welcomeEnabled; }
static bool Session_ModSuppliesFile(const char* path) { return modMenu && !std::strcmp(path, "guis/mpmain.gui"); }
static bool RetainedUI_IsOpen() { return retainedOpen; }
struct Common {
    int now = 1000; std::string output;
    int GetPresentationTime() const { return now; }
    void Printf(const char* fmt, ...) { char text[512]; va_list args; va_start(args, fmt); std::vsnprintf(text, sizeof(text), fmt, args); va_end(args); output += text; }
} commonObject, *common = &commonObject;
struct CVarSystem {
    bool autoJoin = false;
    bool GetCVarBool(const char* name) const { return !std::strcmp(name, "ui_autoJoin") ? autoJoin : !std::strcmp(name, "ui_retainedTrace"); }
} cvarObject, *cvarSystem = &cvarObject;
struct MultiViewDemo { bool playing = false; bool IsPlaying() const { return playing; } };
struct idAsyncNetwork { static MultiViewDemo multiViewDemo; };
MultiViewDemo idAsyncNetwork::multiViewDemo;
class idSessionLocal {
public:
    bool multiplayer = true; void* readDemo = nullptr;
    bool IsMultiplayer() { return multiplayer; }
    bool retainedLoadingActive = true, mapSpawned = false, insideExecuteMapChange = false, retainedMultiplayerUncovered = false;
    idUserInterface *guiLoading = nullptr, *guiLoadingHold = nullptr, *guiRetainedMultiplayer = nullptr, *guiActive = nullptr;
    int retainedLoadingHoldBegan = 0, retainedLoadingHoldLive = 0, retainedLoadingHoldUntil = 0;
    bool retainedLoadingHoldFading = false;
    idStrList retainedStock;
    bool BeginRetainedLoadingHold(); void FadeRetainedLoadingHold(const char* reason);
    void UpdateRetainedLoadingHold(); void ClearRetainedLoadingHold();
};
"""

HOLD_MAIN = r"""
static idUserInterface loading{"guis/loading/loading.q4ui"}, menu{"guis/mpmain.gui", true}, card{"guis/menu/mp_welcome.q4ui"},
    chat{"guis/mpchat.gui", true};
// A session at the end of a multiplayer load that the retained screen presented.
static idSessionLocal Loaded() {
    idSessionLocal session; session.guiLoading = &loading; loading.events.clear(); common->output.clear(); common->now = 1000;
    return session;
}
// One frame: the clock, then the hold's update at the top of the frame.
static void Frame(idSessionLocal& session, int now) { common->now = now; session.UpdateRetainedLoadingHold(); }
static bool Held(const idSessionLocal& session) { return session.guiLoadingHold == &loading; }
static bool Traced(const char* line) { return common->output.find(line) != std::string::npos; }

int main() {
    // Only a Welcome join keeps the screen: the retained screen presented the
    // load, it is multiplayer and no demo, ui_autoJoin is off and the card can
    // present (its gate, no fallback, no mod menu).
    struct Refusal { const char* why; void (*set)(idSessionLocal&, bool); };
    const Refusal refusals[] = {
        {"stock screen", [](idSessionLocal& s, bool on) { s.retainedLoadingActive = !on; }},
        {"no screen", [](idSessionLocal& s, bool on) { s.guiLoading = on ? nullptr : &loading; }},
        {"single player", [](idSessionLocal& s, bool on) { s.multiplayer = !on; }},
        {"demo", [](idSessionLocal& s, bool on) { static int demo; s.readDemo = on ? &demo : nullptr; }},
        {"multiview demo", [](idSessionLocal&, bool on) { idAsyncNetwork::multiViewDemo.playing = on; }},
        {"auto join", [](idSessionLocal&, bool on) { cvarSystem->autoJoin = on; }},
        {"card off", [](idSessionLocal&, bool on) { welcomeEnabled = !on; }},
        {"card fell back", [](idSessionLocal& s, bool on) { s.retainedStock.items = on ? std::vector<std::string>{RETAINED_MP_WELCOME_GUI} : std::vector<std::string>{}; }},
        {"mod menu", [](idSessionLocal&, bool on) { modMenu = on; }},
    };
    for (const Refusal& refusal : refusals) {
        idSessionLocal session = Loaded();
        refusal.set(session, true);
        CHECK(!session.BeginRetainedLoadingHold() && session.guiLoadingHold == nullptr);
        refusal.set(session, false);
        CHECK(session.BeginRetainedLoadingHold() && Held(session));
    }
    // A listen server: the card presents in the frame the load ends, on the
    // clock the load left standing; the fade waits for the clock to move.
    {
        idSessionLocal session = Loaded();
        CHECK(session.BeginRetainedLoadingHold() && Traced("RETAINED_LOADING_HOLD begin"));
        session.mapSpawned = true;
        session.guiActive = &menu; session.guiRetainedMultiplayer = &card;
        Frame(session, 1000);
        CHECK(Held(session) && loading.events.empty() && !session.retainedLoadingHoldFading);
        session.FadeRetainedLoadingHold("card");
        CHECK(loading.events.empty());
        Frame(session, 3700);
        CHECK(loading.events == std::vector<std::string>{"handoff"} && session.retainedLoadingHoldFading);
        CHECK(Traced("RETAINED_LOADING_HOLD fade=card waited=0"));
        Frame(session, 3700 + RETAINED_LOADING_HANDOFF_MSEC - 1);
        CHECK(Held(session));
        Frame(session, 3700 + RETAINED_LOADING_HANDOFF_MSEC);
        CHECK(session.guiLoadingHold == nullptr && loading.events.size() == 1 && Traced("end faded="));
    }
    // A remote client: the card comes seconds later; its first frame draws
    // under the screen and the fade starts on the next.
    {
        idSessionLocal session = Loaded();
        CHECK(session.BeginRetainedLoadingHold());
        session.mapSpawned = true;
        Frame(session, 1000);
        for (int now = 1016; now < 3016; now += 16) {
            Frame(session, now);
            CHECK(Held(session) && loading.events.empty());
        }
        Frame(session, 3016);
        session.guiActive = &menu; session.guiRetainedMultiplayer = &card;
        CHECK(loading.events.empty());
        Frame(session, 3270);
        CHECK(loading.events == std::vector<std::string>{"handoff"} && Traced("fade=card waited=2254"));
    }
    // No card: the hold gives up after its ceiling, counted from the first
    // frame the clock moves, however far it jumped.
    {
        idSessionLocal session = Loaded();
        CHECK(session.BeginRetainedLoadingHold());
        session.mapSpawned = true;
        const int live = 1000 + RETAINED_LOADING_HOLD_MSEC + 500;
        Frame(session, live);
        CHECK(Held(session) && loading.events.empty());
        Frame(session, live + RETAINED_LOADING_HOLD_MSEC - 1);
        CHECK(loading.events.empty());
        Frame(session, live + RETAINED_LOADING_HOLD_MSEC);
        CHECK(loading.events == std::vector<std::string>{"handoff"} && Traced("fade=expired"));
        Frame(session, live + RETAINED_LOADING_HOLD_MSEC + RETAINED_LOADING_HANDOFF_MSEC);
        CHECK(session.guiLoadingHold == nullptr);
    }
    // The game's menu waiting for its card keeps the screen; anything else
    // that takes the screen ends the hold: another GUI, the stock menu
    // presenting in the card's place, a retained modal.
    {
        idSessionLocal session = Loaded();
        CHECK(session.BeginRetainedLoadingHold());
        session.mapSpawned = true;
        session.guiActive = &menu;
        Frame(session, 1100);
        CHECK(Held(session) && loading.events.empty());
        struct Screen { idUserInterface* active; bool uncovered; bool modal; };
        for (const Screen& screen : {Screen{&chat, false, false}, Screen{&menu, true, false}, Screen{nullptr, false, true}}) {
            idSessionLocal other = Loaded();
            CHECK(other.BeginRetainedLoadingHold());
            other.mapSpawned = true;
            other.guiActive = screen.active; other.retainedMultiplayerUncovered = screen.uncovered; retainedOpen = screen.modal;
            Frame(other, 1100);
            CHECK(loading.events == std::vector<std::string>{"handoff"} && Traced("fade=screen"));
            retainedOpen = false;
        }
    }
    // A new load or a stop drops the hold at once, without a fade.
    {
        idSessionLocal session = Loaded();
        CHECK(session.BeginRetainedLoadingHold());
        session.mapSpawned = true;
        session.insideExecuteMapChange = true;
        Frame(session, 1100);
        CHECK(session.guiLoadingHold == nullptr && loading.events.empty());
        idSessionLocal stopped = Loaded();
        CHECK(stopped.BeginRetainedLoadingHold());
        Frame(stopped, 1100);
        CHECK(stopped.guiLoadingHold == nullptr && loading.events.empty());
        stopped.mapSpawned = true;
        CHECK(stopped.BeginRetainedLoadingHold() && Held(stopped));
        stopped.ClearRetainedLoadingHold();
        CHECK(stopped.guiLoadingHold == nullptr && stopped.retainedLoadingHoldBegan == 0 && stopped.retainedLoadingHoldLive == 0 &&
              stopped.retainedLoadingHoldUntil == 0 && !stopped.retainedLoadingHoldFading);
    }
    std::printf("ui_retained gate: %d loading hold checks passed\n", checks);
    return 0;
}
"""


PROMPT_GLYPHS_SUPPORT = r"""
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
static int checks=0;
static void Check(bool ok,const char *what){++checks;if(!ok){std::printf("FAIL: %s\n",what);std::exit(1);}}
struct idStr {
    static int Icmp(const char *a,const char *b){
        for(;;++a,++b){const int x=std::tolower(static_cast<unsigned char>(*a)),y=std::tolower(static_cast<unsigned char>(*b));
            if(x!=y||!x)return x-y;}
    }
};
static int glyphFamily=0; // what the HUD's family function returns
int Sys_SDL_GamepadGlyphFamily(int faceLabels[4]){for(int i=0;i<4;++i)faceLabels[i]=0;return glyphFamily;}
struct idKeyInput {
    static inline bool controller=false;static bool LastInputWasController(){return controller;}
    static inline bool touch=false;static bool LastPointerWasTouch(){return touch;}
    static bool MenuConfirmSwapped(void);static int MenuControllerKey(int keynum);
};
enum {K_JOY3=3,K_JOY4=4};
struct {int value=0;int GetInteger()const{return value;}} in_swapConfirmBack;
std::map<std::string,std::string> cvars;
struct CVars {const char *GetCVarString(const char *name){const auto found=cvars.find(name);return found==cvars.end()?"":found->second.c_str();}
    int GetCVarInteger(const char *name){return std::atoi(GetCVarString(name));}} cvarValue,*cvarSystem=&cvarValue;
enum SDL_GamepadButton {SDL_GAMEPAD_BUTTON_SOUTH};
enum SDL_GamepadButtonLabel {SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN,SDL_GAMEPAD_BUTTON_LABEL_A,SDL_GAMEPAD_BUTTON_LABEL_B,SDL_GAMEPAD_BUTTON_LABEL_X,
    SDL_GAMEPAD_BUTTON_LABEL_Y,SDL_GAMEPAD_BUTTON_LABEL_CROSS,SDL_GAMEPAD_BUTTON_LABEL_CIRCLE,SDL_GAMEPAD_BUTTON_LABEL_SQUARE,
    SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE};
struct SDL_Gamepad {SDL_GamepadButtonLabel south;};
SDL_GamepadButtonLabel SDL_GetGamepadButtonLabel(SDL_Gamepad *pad,SDL_GamepadButton){return pad->south;}
"""

PROMPT_GLYPHS_MAIN = r"""
int main(){
    // The family by the label printed on the south button.
    SDL_Gamepad xbox{SDL_GAMEPAD_BUTTON_LABEL_A},ps{SDL_GAMEPAD_BUTTON_LABEL_CROSS},nintendo{SDL_GAMEPAD_BUTTON_LABEL_B},
        unknown{SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN},odd{SDL_GAMEPAD_BUTTON_LABEL_X};
    Check(!std::strcmp(SDL3_GamepadFamily(&xbox),"xbox"),"A on the south: Xbox");
    Check(!std::strcmp(SDL3_GamepadFamily(&ps),"playstation"),"the cross on the south: PlayStation");
    Check(!std::strcmp(SDL3_GamepadFamily(&nintendo),"nintendo"),"B on the south: Nintendo");
    Check(!std::strcmp(SDL3_GamepadFamily(&unknown),"generic") && !std::strcmp(SDL3_GamepadFamily(&odd),"generic"),"no known label: generic");
    cvars["com_platformProfile"]="SteamDeck";
    Check(!std::strcmp(SDL3_GamepadFamily(&xbox),"steamdeck") && !std::strcmp(SDL3_GamepadFamily(&ps),"playstation"),
        "the steamdeck profile's A pad is the Steam Deck");
    cvars["com_platformProfile"]="default";
    // The glyphs: keycaps unless a controller was used last, then the family
    // the HUD's prompts use (Sys_SDL_GamepadGlyphFamily, which in_promptStyle
    // forces): a family's buttons at 1-4, the generic diamonds at 5. The face
    // layout never picks the glyphs.
    for(int family=0;family<=4;++family){
        glyphFamily=family;
        for(const char *layout:{"xbox","nintendo","none"}){
            cvars["in_gamepadFamily"]=layout;
            idKeyInput::controller=false;Check(RetainedUI_PromptGlyphs()==0,"the keyboard or mouse last: keycaps");
            idKeyInput::controller=true;Check(RetainedUI_PromptGlyphs()==(family?family:5),"a controller last: the HUD's family");
        }
    }
    glyphFamily=0;idKeyInput::controller=false;
    // Swap Confirm and Back: automatic swaps a Nintendo layout only, 1 keeps
    // confirm south and 2 moves it east; only the south and east keys trade.
    // A prompt style forced to a family decides the layout, the generic one
    // leaves it to the controller.
    for(const char *family:{"nintendo","Nintendo","xbox","playstation","steamdeck","generic","none",""}){
        cvars["in_gamepadFamily"]=family;
        const bool nintendo=!idStr::Icmp(family,"nintendo");
        in_swapConfirmBack.value=0;
        for(const char *style:{"0","5",""}){
            cvars["in_promptStyle"]=style;
            Check(idKeyInput::MenuConfirmSwapped()==nintendo,"automatic swaps a Nintendo layout only");
            Check(idKeyInput::MenuControllerKey(K_JOY3)==(nintendo?K_JOY4:K_JOY3) && idKeyInput::MenuControllerKey(K_JOY4)==(nintendo?K_JOY3:K_JOY4),
                "south and east trade places while swapped");
        }
        for(int style=1;style<=4;++style){
            cvars["in_promptStyle"]=std::to_string(style);
            Check(idKeyInput::MenuConfirmSwapped()==(style==3),"a style forced to a family decides the layout");
            in_swapConfirmBack.value=1;Check(!idKeyInput::MenuConfirmSwapped(),"1 keeps confirm south whatever the style");
            in_swapConfirmBack.value=2;Check(idKeyInput::MenuConfirmSwapped(),"2 moves confirm east whatever the style");
            in_swapConfirmBack.value=0;
        }
        cvars["in_promptStyle"]="0";
        in_swapConfirmBack.value=1;Check(!idKeyInput::MenuConfirmSwapped() && idKeyInput::MenuControllerKey(K_JOY3)==K_JOY3,"1 keeps confirm south");
        in_swapConfirmBack.value=2;Check(idKeyInput::MenuConfirmSwapped() && idKeyInput::MenuControllerKey(K_JOY4)==K_JOY3,"2 moves confirm east");
        Check(idKeyInput::MenuControllerKey(7)==7 && idKeyInput::MenuControllerKey(13)==13,"every other key is itself");
    }
    in_swapConfirmBack.value=0;
    std::printf("ui_retained gate: %d prompt glyph checks passed\n",checks);
    return 0;
}
"""


def check_prompt_glyphs(compiler: str, directory: Path) -> None:
    """Prompts follow the active device (section 13.5): compile the production
    family mapping and glyph choice against stand-ins, then pin where the
    platform sets the family and the adapter publishes the glyphs, and check
    every document's glyph alternatives against the generator's families."""
    retained = (ROOT / 'src/ui/RetainedUI.cpp').read_text(encoding='utf-8')
    backend = (ROOT / 'src/sys/sdl3/sdl3_backend.cpp').read_text(encoding='utf-8')
    adapter = (ROOT / 'src/ui/UserInterfaceRetained.cpp').read_text(encoding='utf-8', errors='replace').replace('\r\n', '\n')
    keys = (ROOT / 'src/framework/KeyInput.cpp').read_text(encoding='utf-8', errors='replace')
    bodies = [function_body(backend, 'static const char *SDL3_GamepadFamily('), function_body(retained, 'int RetainedUI_PromptGlyphs('),
              function_body(keys, 'static bool Key_MenuConfirmSwapped('), function_body(keys, 'bool idKeyInput::MenuConfirmSwapped('),
              function_body(keys, 'int idKeyInput::MenuControllerKey(')]
    source = directory / 'glyphs.cpp'
    binary = directory / 'glyphs.exe'
    source.write_text(PROMPT_GLYPHS_SUPPORT + '\n'.join(bodies) + PROMPT_GLYPHS_MAIN, encoding='utf-8')
    subprocess.run([compiler, '-std=c++20', '-DUSE_SDL3', '-Wall', '-Wextra', '-Wno-unused-function', str(source), '-o', str(binary)],
                   check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    assert result.returncode == 0, 'the prompt glyph checks failed'
    # The platform: a ROM face layout the backend sets as a controller opens
    # and clears as it closes, kept through a join without cheats; one
    # archived style with six values, the HUD's, which the menus share.
    assert re.search(r'static idCVar in_gamepadFamily\("in_gamepadFamily", "none", CVAR_SYSTEM \| CVAR_ROM \| CVAR_NOCHEAT,', backend)
    assert re.search(r'static idCVar in_promptStyle\("in_promptStyle", "0", CVAR_SYSTEM \| CVAR_ARCHIVE \| CVAR_INTEGER,[^;]*, 0, 5\);',
                     backend, re.S)
    assert 'ui_promptStyle' not in retained, 'the menus take the HUD\'s in_promptStyle'
    assert re.search(r'#if defined\(USE_SDL3\)\n[^#]*int Sys_SDL_GamepadGlyphFamily\(int faceLabels\[4\]\);\n#endif', retained)
    assert 'cvarSystem->GetCVarInteger( "in_promptStyle" )' in function_body(keys, 'bool idKeyInput::MenuConfirmSwapped(')
    assert 'in_gamepadFamily.SetString(SDL3_GamepadFamily(pad));' in function_body(backend, 'static bool SDL3_OpenGamepad(')
    assert 'in_gamepadFamily.SetString("generic");' in function_body(backend, 'static bool SDL3_OpenJoystick(')
    for signature in ('static void SDL3_CloseGamepad(', 'static void SDL3_CloseJoystick('):
        assert 'in_gamepadFamily.SetString("none");' in function_body(backend, signature), signature
    # The adapter publishes before the frame draws, so the frame that follows
    # a change shows it.
    redraw = function_body(adapter, 'void idUserInterfaceRetained::Redraw(')
    assert redraw.index('impl->PublishPromptGlyphs();') < redraw.index('RetainedUI_DrawViewRoot(')
    publish = function_body(adapter, 'void PublishPromptGlyphs() {')
    assert 'RetainedUI_PromptGlyphs()' in publish and 'RetainedUI_PromptSwapped()' in publish
    # Swap Confirm and Back: one archived setting, and every menu input path
    # translates the south and east buttons through it.
    assert re.search(r'static idCVar in_swapConfirmBack\( "in_swapConfirmBack", "0", CVAR_SYSTEM \| CVAR_INTEGER \| CVAR_ARCHIVE,[^;]*, 0, 2 \);',
                     keys, re.S)
    window = (ROOT / 'src/ui/Window.cpp').read_text(encoding='utf-8', errors='replace')
    assert 'switch ( idKeyInput::MenuControllerKey( sourceEvent->evValue ) ) {' in function_body(window, 'static bool TranslateControllerMenuKey(')
    assert 'switch (idKeyInput::MenuControllerKey(key)) {' in function_body(retained, 'bool MapMenuKey(')
    assert adapter.count('idKeyInput::MenuControllerKey(') == 3, 'the menu keys, the text commit and the failure close'
    menu_source = (ROOT / 'src/framework/Session_menu.cpp').read_text(encoding='utf-8', errors='replace')
    assert 'idKeyInput::MenuControllerKey( event->evValue ) == K_JOY4' in function_body(menu_source, 'bool idSessionLocal::RetainedSubpageEvent(')
    # Every document with prompts: the state, and per glyph group each
    # family's button at its index, the keyboard's keycap at 0, and a pad
    # shown for any controller.
    sys.path.insert(0, str(ROOT / 'tools' / 'ui'))
    import build_retained_screens as generator
    glyphs_state, swapped_state = {'state': generator.PROMPT_GLYPHS}, {'state': generator.PROMPT_SWAPPED}

    def glyph_condition(condition: dict):
        """(index, swapped) for a glyph binding's condition, swapped None
        unless the binding depends on prompt.swapped; None for others."""
        if condition.get('op') == '==' and condition['args'][0] == glyphs_state:
            return condition['args'][1], None
        if condition.get('op') == '&&' and len(condition['args']) == 2:
            first, second = condition['args']
            if first.get('op') == '==' and first['args'][0] == glyphs_state:
                if second == swapped_state:
                    return first['args'][1], True
                if second == {'op': '!', 'args': [swapped_state]}:
                    return first['args'][1], False
        return None
    documents, shoulders, swapping = 0, 0, 0
    for path in sorted((ROOT / 'content/baseoq4/pak0/guis').rglob('*.q4ui')):
        document = json.loads(re.sub(r'^\s*//.*$', '', path.read_text(encoding='utf-8'), flags=re.M))
        if generator.PROMPT_GLYPHS not in document.get('state', {}):
            continue
        documents += 1
        assert document['state'][generator.PROMPT_GLYPHS] == {'type': 'number', 'initial': 0}, path.name
        shown, pads = {}, set()
        for binding in document.get('bindings', []):
            value = binding['value']
            if binding['property'] == 'display' and isinstance(value, dict) and value.get('op') == 'select':
                parsed = glyph_condition(value['args'][0])
                if parsed is None:
                    continue
                if value['args'][1:] == ['block', 'none']:
                    shown[binding['node']] = parsed
                else:
                    assert value['args'][1:] == ['none', 'block'] and parsed == (0, None), (path.name, binding['id'])
                    pads.add(binding['node'])
        groups = {}
        for node, (index, swapped) in shown.items():
            if index < 1:
                continue
            suffix = f'-{generator.GLYPH_FAMILIES[index - 1]}' + ('-swapped' if swapped else '')
            assert node.endswith(suffix), (path.name, node)
            groups.setdefault(node[:-len(suffix)], set()).add(swapped)
        assert groups, path.name
        for group_id, kinds in groups.items():
            for index, family in enumerate(generator.GLYPH_FAMILIES, 1):
                if kinds == {None}:
                    assert shown.get(f'{group_id}-{family}') == (index, None), (path.name, group_id, family)
                else:
                    # An Accept or Back prompt: its own position unless
                    # confirm moved east, the other position while it has.
                    assert kinds == {False, True}, (path.name, group_id, kinds)
                    assert shown.get(f'{group_id}-{family}') == (index, False), (path.name, group_id, family)
                    assert shown.get(f'{group_id}-{family}-swapped') == (index, True), (path.name, group_id, family)
            swapping += kinds == {False, True}
        if any(kinds == {False, True} for kinds in groups.values()):
            assert document['state'][generator.PROMPT_SWAPPED] == {'type': 'boolean', 'initial': False}, path.name
        for pad in pads:
            assert pad.endswith('-pad') and pad[:-len('-pad')] in groups, (path.name, pad)
        shoulders += sum(1 for group_id in groups if group_id in ('tab-previous', 'tab-next', 'prompt-tabs-0', 'prompt-tabs-1'))
    # The title, both pauses, Single Player, Campaign, the campaign difficulty
    # page and CHAPTERS, Multiplayer and SETTINGS, the seven pages with their own
    # prompt bars and the Strogg pause's three, the loading screen, the Escape,
    # Welcome and summary cards, the Arena pause card and the CONTROLS, GAME
    # OPTIONS, DISPLAY (SYSTEM) and AUDIO sub-pages.
    assert documents == 28, documents
    # The cards' and the settings sub-pages' tab keys take the shoulders, as do
    # the Escape and Welcome cards' Tabs prompts; every Accept and Back prompt
    # follows Swap Confirm and Back.
    assert shoulders == 16, shoulders
    assert swapping == 55, swapping
    print(f'ui_retained gate: prompt glyphs follow the active device in {documents} documents')


def check_loading_hold(compiler: str, directory: Path) -> None:
    """The loading screen held over a Welcome join (section 14.17): compile the
    production hold against a stand-in session and drive the listen server's
    and the remote client's joins, the ceiling, the screens that end it and
    the drops; then pin where the session calls it, and the loading
    document's hand-off."""
    menu = (ROOT / 'src/framework/Session_menu.cpp').read_text(encoding='utf-8')
    session = (ROOT / 'src/framework/Session.cpp').read_text(encoding='utf-8')
    constants = '\n'.join(re.search(rf'static const int {name} = \d+;', menu).group(0)
                          for name in ('RETAINED_LOADING_HOLD_MSEC', 'RETAINED_LOADING_HANDOFF_MSEC'))
    bodies = [constants, function_body(menu, 'static bool Session_IsGameMenu(')]
    bodies += [function_body(menu, f'{kind} idSessionLocal::{name}(') for kind, name in (
        ('bool', 'BeginRetainedLoadingHold'), ('void', 'FadeRetainedLoadingHold'), ('void', 'UpdateRetainedLoadingHold'),
        ('void', 'ClearRetainedLoadingHold'))]
    source = directory / 'hold.cpp'
    binary = directory / 'hold.exe'
    source.write_text(HOLD + '\n'.join(bodies) + HOLD_MAIN, encoding='utf-8')
    subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Wno-unused-function', str(source), '-o', str(binary)], check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    assert result.returncode == 0, 'the loading hold checks failed'
    menu_lf, session_lf = menu.replace('\r\n', '\n'), session.replace('\r\n', '\n')
    # The load ends with the hold or, without it, the wipe; the hold must ask
    # while the retained screen still presents the load.
    change = function_body(session_lf, 'void idSessionLocal::ExecuteMapChange(')
    assert '\tif ( !BeginRetainedLoadingHold() ) {\n\t\tStartWipe( "gfx/wipes/fade_blend" );\n\t}' in change
    assert change.index('ClearRetainedLoadingHold();') < change.index('BeginRetainedLoadingHold()') < change.index('retainedLoadingActive = false;\n\n\tSys_SetPhysicalWorkMemory')
    # Drawn above the game and its menu, under the wipe and the console.
    draw = function_body(session_lf, 'void idSessionLocal::Draw(')
    held = draw.index('guiLoadingHold->Redraw( presentationTime );')
    assert draw.index('} else if ( mapSpawned ) {') < held < draw.index('RetainedUI_Draw();') < draw.index('DrawWipeModel();')
    # Updated at the top of every frame, before the card's update, and user
    # commands wait while it holds.
    frame = function_body(menu_lf, 'void idSessionLocal::GuiFrameEvents(')
    assert frame.index('UpdateRetainedLoadingHold();') < frame.index('usercmdGen->InhibitUsercmd') < frame.index('UpdateRetainedMultiplayer();')
    assert 'guiTest || RetainedUI_IsOpen() || guiLoadingHold != NULL ) {' in frame
    assert 'FadeRetainedLoadingHold' not in function_body(menu, 'void idSessionLocal::UpdateRetainedMultiplayer('), \
        'the card fades the hold from the next frame, not its own'
    assert 'ClearRetainedLoadingHold();' in function_body(session, 'void idSessionLocal::Clear(')
    assert 'ClearRetainedLoadingHold();' in function_body(session, 'void idSessionLocal::StopInternal(')
    assert 'retained->HandleNamedEvent( "present" );' in function_body(menu, 'idUserInterface *idSessionLocal::SelectRetainedLoadingGui(')
    # The loading document's hand-off: the whole screen fades over the
    # session's hand-off time, and present makes it whole again.
    text = (ROOT / 'content/baseoq4/pak0/guis/loading/loading.q4ui').read_text(encoding='utf-8')
    document = json.loads(re.sub(r'^\s*//.*$', '', text, flags=re.M))
    handoff_ms = int(re.search(r'RETAINED_LOADING_HANDOFF_MSEC = (\d+);', menu).group(1))
    timelines = {timeline['id']: timeline for timeline in document['timelines']}
    fade = timelines['handoff']
    assert fade['durationMs'] == handoff_ms and len(fade['tracks']) == 1
    track = fade['tracks'][0]
    assert track['node'] == document['root']['id'] == 'screen' and track['property'] == 'opacity'
    assert [key['value']['value'] for key in track['keys']] == [1, 0] and track['keys'][-1]['atMs'] == handoff_ms
    assert [key['value']['value'] for key in timelines['present']['tracks'][0]['keys']] == [1, 1]
    assert document['events']['handoff'] == [{'op': 'playTimeline', 'timeline': 'handoff'}]
    assert document['events']['present'] == [{'op': 'playTimeline', 'timeline': 'present'}]
    assert document['root']['properties']['opacity']['value'] == 1



def check_retained_match(menu: str) -> None:
    """The Match page's contracts that the compiled cases cannot see: the card's
    action indices, the session's tokens and the game's own Match Control
    commands agree; the game mirrors the operations the page shows, with the
    page's status lines, labels and list rows; the session's lists are the
    game's own selection commands and its roles the protocol's; the page asks
    first exactly where Match Control confirms, with the stock modal's text;
    the triggers page its sections."""
    sys.path.insert(0, str(ROOT / 'tools/ui'))
    import retained_mp_menus as cards
    tokens = tuple(re.findall(r'"([a-z_]+)"', re.search(r'RETAINED_MP_MATCH_TOKENS\[\] = \{([^}]*)\};', menu).group(1)))
    assert tokens == cards.MATCH_TOKENS, 'the session and the Match page name its actions alike'
    mp_game = (ROOT / 'src/mpgame/MultiplayerGame.cpp').read_text(encoding='utf-8')
    model = (ROOT / 'src/mpgame/mp/match/MatchControlModel.cpp').read_text(encoding='utf-8')
    handler = function_body(mp_game, 'bool idMultiplayerGame::HandleMatchControlCommand(')
    for token in tokens:
        assert f'"{token}"' in mp_game or f'"{token}"' in model, f'Match Control has no "{token}" command'
    assert '"refresh"' in handler and '"confirm"' in handler and '"cancel_confirm"' in handler
    # The game's mirrored operations, by name, prefix and label: each section's
    # actions in the page's order, the referee's between them. Where the
    # projection names an action, it names it with the page's alternatives.
    table = mp_game[mp_game.index('static const retainedMatchOperation_t RETAINED_MATCH_OPERATIONS[] = {'):]
    operations = re.findall(r'\{ "(\w+)", "(\w+)", "(#str_\d+)", (NULL|"\w+") \},', table[:table.index('};')])
    names = [name for name, _prefix, _label, _state in operations]
    assert names == [name for name, *_ in cards.MATCH_STATUS_ACTIONS] + ['referee_login', 'referee_logout'] + \
        [name for name, *_ in cards.MATCH_TEAM_ACTIONS + cards.MATCH_PROPOSAL_ACTIONS + cards.MATCH_RULE_ACTIONS +
         cards.MATCH_SERIES_ACTIONS], names
    prefixes = {name: prefix for name, prefix, _label, _state in operations}
    projection = (ROOT / 'src/mpgame/mp/match/MatchControlProjection.cpp').read_text(encoding='utf-8')
    named = {'ready': ('match_ready_action', '#str_41714'), 'team_lock': ('match_team_lock_action', '#str_41735'),
             'broadcaster': ('match_broadcaster_action', '#str_41796')}
    for name, _token, accessible, _body in cards.MATCH_ACTIONS:
        _prefix, label, state = next(entry[1:] for entry in operations if entry[0] == name)
        assert label == accessible, (name, label)
        assert state == (f'"{named[name][0]}"' if name in named else 'NULL'), (name, state)
        if name in named:
            # The projection writes the stock label or its alternative, both of
            # which the page's label column fits.
            assert f'"{named[name][1]}" : "{accessible}"' in projection, name
            assert {accessible, named[name][1]} <= set(cards.MATCH_STATUS_LABELS + cards.MATCH_TEAM_LABELS), name
            assert f'gui.SetStateString( "{named[name][0]}"' in projection, name
    header = (ROOT / 'src/mpgame/MultiplayerGame.h').read_text(encoding='utf-8')
    assert int(re.search(r'RETAINED_MATCH_STATUS_LINES = (\d+);', header).group(1)) == cards.MATCH_STATUS_LINES
    # The lists: the session's stock lists and tokens are the game's own
    # selection commands, in the card's order; a row index spans the longest
    # list; the game mirrors as many rows as the page has.
    lists = re.findall(r'\{ "(match_\w+_rows)", "(select_\w+)" \}',
                       menu[menu.index('RETAINED_MP_MATCH_LISTS[][2] = {'):menu.index('RETAINED_MP_MATCH_LIST_COUNT')])
    assert [list_name for list_name, _token in lists] == [f'match_{key}_rows' for key in cards.MATCH_LISTS], lists
    for list_name, token in lists:
        assert f'{{ "{token}", "{list_name}_sel_0",' in handler, (list_name, token)
    model_header = (ROOT / 'src/mpgame/mp/match/MatchControlModel.h').read_text(encoding='utf-8')
    longest = max(int(value) for value in re.findall(r'MP_MATCH_CONTROL_MAX_\w+_ROWS = (\d+);', model_header))
    assert int(re.search(r'RETAINED_MP_MATCH_ROWS = (\d+);', menu).group(1)) == longest == 128
    assert int(re.search(r'RETAINED_MATCH_TEAM_ROWS = (\d+);', header).group(1)) == cards.MATCH_TEAM_ROWS
    assert int(re.search(r'RETAINED_MATCH_REPLACEMENT_ROWS = (\d+);', header).group(1)) == cards.MATCH_REPLACEMENT_ROWS
    # Proposals, profiles and rule fields: as many rows as Match Control has.
    assert int(re.search(r'RETAINED_MATCH_PROPOSAL_ROWS = (\d+);', header).group(1)) == cards.MATCH_PROPOSAL_ROWS == \
        int(re.search(r'MP_MATCH_CONTROL_MAX_PROPOSAL_TEMPLATE_ROWS = (\d+);', model_header).group(1))
    assert int(re.search(r'RETAINED_MATCH_PROFILE_ROWS = (\d+);', header).group(1)) == cards.MATCH_PROFILE_ROWS == \
        int(re.search(r'MP_MATCH_CONTROL_MAX_PROFILE_ROWS = (\d+);', model_header).group(1))
    rules_header = (ROOT / 'src/mpgame/mp/match/MatchRules.h').read_text(encoding='utf-8')
    fields = re.search(r'typedef enum \{\s*MP_RULE_GAME_TYPE = 0,(.*?)MP_RULE_FIELD_COUNT', rules_header, re.S).group(1)
    assert int(re.search(r'RETAINED_MATCH_RULE_ROWS = (\d+);', header).group(1)) == cards.MATCH_RULE_ROWS == 1 + fields.count('MP_RULE_')
    # The ballot target in the stock choice's own words, which the game reads;
    # the rule value up to the largest value a rule takes, which the adapter
    # and the session both allow.
    assert re.search(r'RETAINED_MP_MATCH_SCOPES\[\] = \{ "global", "side" \};', menu)
    stock_page = (ROOT / 'content/baseoq4/pak0/guis/matchcontrol.gui').read_text(encoding='utf-8')
    assert 'choices\t"#str_41794"' in stock_page and 'values\t"global;side"' in stock_page
    assert 'strcmp( scope, "global" ) == 0' in handler and 'strcmp( scope, "side" ) == 0' in handler
    assert [key for key, _value in cards.MATCH_SCOPES] == ['#str_41738', '#str_41739']
    rules = (ROOT / 'src/mpgame/mp/match/MatchRules.cpp').read_text(encoding='utf-8')
    maxima = [int(value) for value in re.findall(r'MP_RULE_FIELD\( MP_RULE_\w+, "\w+", MP_RULE_TYPE_\w+,\s*-?\d+, (\d+),', rules)]
    assert len(maxima) >= 20 and max(maxima) == cards.MATCH_RULE_VALUE_MAX == int(re.search(r'RETAINED_MP_MATCH_RULE_VALUE_MAX = (\d+);', menu).group(1))
    assert 'number > (rule ? 10000 : 999)' in (ROOT / 'src/ui/UserInterfaceRetained.cpp').read_text(encoding='utf-8')
    assert 'guiActive->SetStateInt( "match_rule_value", voteValue );' in menu and 'parseStateInteger( "match_rule_value",' in handler
    assert 'mainGui->GetStateString(\n\t\t\t"match_proposal_scope_choice", "" );' in handler.replace('\r\n', '\n')
    # The reasons the generator fits into each section's reason column are
    # every reason a row can show: the protocol's, by its own key or its
    # localization id, the wait for a view and the unknown value.
    localization_source = (ROOT / 'src/mpgame/mp/match/MatchControlLocalization.cpp').read_text(encoding='utf-8')
    reason_keys = re.findall(r'"(#str_\d+)"', function_body(localization_source, 'const char *MPMatchControlProtocolReasonKey('))
    localized_reasons = re.findall(r'case MP_MATCH_LOCALIZATION_REASON_\w+: return "(#str_\d+)";', localization_source)
    assert reason_keys == localized_reasons and set(reason_keys) | {'#str_41774', '#str_42301'} == set(cards.MATCH_REASON_KEYS), reason_keys
    assert 'static const char UNKNOWN_KEY[] = "#str_42301";' in localization_source
    # The generator refuses a reason column that cannot hold every reason in
    # its lines, in every language, and accepts the sections' own.
    for lines, width in ((2, 300), (3, 330)):
        try:
            cards.check_match_labels(width, cards.MATCH_TEAM_LABELS, lines)
        except SystemExit:
            continue
        raise AssertionError(f'a {width} dp action column passed with reasons in {lines} lines')
    cards.check_match_labels(859 - 336, cards.MATCH_STATUS_LABELS)
    assert 'return Localized( "#str_41774" );' in function_body(projection, 'static const char *AvailabilityReason(')
    # Series and Evidence: the map pool, history and recent evidence, as many
    # rows as the card holds (past them the stock page); the history and the
    # evidence only to read, with no selection of their own on either side.
    view_header = (ROOT / 'src/mpgame/mp/match/MatchView.h').read_text(encoding='utf-8')
    assert 'MP_MATCH_CONTROL_MAX_SERIES_MAP_ROWS = MP_MATCH_VIEW_MAX_SERIES_MAP_POOL;' in model_header
    assert int(re.search(r'RETAINED_MATCH_SERIES_MAP_ROWS = (\d+);', header).group(1)) == cards.MATCH_SERIES_MAP_ROWS <= \
        int(re.search(r'MP_MATCH_VIEW_MAX_SERIES_MAP_POOL = (\d+);', view_header).group(1))
    assert int(re.search(r'RETAINED_MATCH_HISTORY_ROWS = (\d+);', header).group(1)) == cards.MATCH_HISTORY_ROWS
    assert int(re.search(r'RETAINED_MATCH_EVIDENCE_ROWS = (\d+);', header).group(1)) == cards.MATCH_EVIDENCE_ROWS == \
        1 + int(re.search(r'MP_MATCH_VIEW_MAX_RECENT_EVIDENCE_EVENTS = (\d+);', view_header).group(1))
    assert 'MP_MATCH_CONTROL_MAX_EVIDENCE_ROWS =\n\t1 + MP_MATCH_VIEW_MAX_RECENT_EVIDENCE_EVENTS;' in model_header.replace('\r\n', '\n')
    assert 'series_history' not in cards.MATCH_LISTS and 'evidence' not in cards.MATCH_LISTS
    assert '"match_series_history_rows"' not in menu and '"match_evidence_rows"' not in menu
    # The series formats in the stock choice's own words, which the game reads,
    # under the projection's own names for them.
    assert re.search(r'RETAINED_MP_MATCH_SERIES_PROFILES\[\] = \{ "best_of_one", "best_of_three", "best_of_five" \};', menu)
    assert 'values\t"best_of_one;best_of_three;best_of_five"' in stock_page
    for word in ('best_of_one', 'best_of_three', 'best_of_five'):
        assert f'strcmp( profile, "{word}" ) == 0' in handler, word
    profile_keys = function_body((ROOT / 'src/mpgame/mp/match/MatchControlLocalization.cpp').read_text(encoding='utf-8'),
                                 'const char *MPMatchControlSeriesProfileKey(')
    for (key, _value), profile in zip(cards.MATCH_SERIES_PROFILES, ('BEST_OF_ONE', 'BEST_OF_THREE', 'BEST_OF_FIVE')):
        assert f'case MP_SERIES_PROFILE_{profile}: return "{key}";' in profile_keys, (key, profile)
    # The role choice: the protocol's roster roles, 1 to 4, by the projection's
    # own names for them; the game reads the stock choice's value.
    assert int(re.search(r'RETAINED_MP_MATCH_ROLES = (\d+);', menu).group(1)) == len(cards.MATCH_ROLES)
    protocol_header = (ROOT / 'src/mpgame/mp/match/MatchProtocol.h').read_text(encoding='utf-8')
    localization = (ROOT / 'src/mpgame/mp/match/MatchControlLocalization.cpp').read_text(encoding='utf-8')
    role_keys = function_body(localization, 'const char *MPMatchControlProtocolRosterRoleKey(')
    for key, value in cards.MATCH_ROLES:
        role = re.search(rf'MP_MATCH_PROTOCOL_ROSTER_ROLE_(\w+) = {value}', protocol_header).group(1)
        assert f'case MP_MATCH_PROTOCOL_ROSTER_ROLE_{role}: return "{key}";' in role_keys, (key, value)
    assert 'parseStateInteger( "match_role_choice",' in handler
    # Players' names reach the page only as plain text, which never translates.
    lister = function_body(mp_game, 'void idMultiplayerGame::PublishRetainedMatchList(')
    matcher = function_body(mp_game, 'void idMultiplayerGame::PublishRetainedMatch(')
    assert 'MPRetainedPlainText( cells[ column ].c_str(), 128, 1 )' in lister, 'list cells are plain text'
    assert 'MPRetainedPlainText( lines[ i ].c_str(), 1024, 1 )' in matcher and \
        'MPRetainedPlainText( state.GetString( "match_result_message" ), 1024, 4 )' in matcher, 'state lines and the result are plain text'
    publisher = function_body(mp_game, 'void idMultiplayerGame::PublishRetainedMenu(').replace('\r\n', '\n')
    assert '\tif ( !RetainedMenuWelcome() ) {\n\t\tPublishRetainedMatch( card, changed );' in publisher, 'only the Escape card has a Match page'
    # Each prefix is a protocol operation the projection decides; the page asks
    # first exactly where its descriptor carries a confirmation.
    protocol = (ROOT / 'src/mpgame/mp/match/MatchProtocol.cpp').read_text(encoding='utf-8')
    descriptors = dict(re.findall(r'\{ MP_MATCH_OP_\w+, "(\w+)",\s+MP_MATCH_LOCALIZATION_OPERATION_\w+,\s+(MP_MATCH_LOCALIZATION_\w+),', protocol))
    assert len(descriptors) == protocol.count('{ MP_MATCH_OP_'), 'every protocol descriptor reads'
    stock = (ROOT / 'content/baseoq4/pak0/guis/matchcontrol.gui').read_text(encoding='utf-8')
    for name, token, _accessible, body in cards.MATCH_ACTIONS:
        confirmed = descriptors[prefixes[name]] != 'MP_MATCH_LOCALIZATION_NONE'
        assert (body is not None) == confirmed == token.startswith('arm_'), (name, token, confirmed)
        if body is not None:
            # The stock button arms the token, then shows its modal with the body.
            after = stock[stock.index(f'"matchControl {token}"'):]
            assert re.search(r'"match_confirm_body::text" "(#str_\d+)"', after).group(1) == body, (token, body)
    for name in ('referee_login', 'referee_logout'):
        assert descriptors[prefixes[name]] == 'MP_MATCH_LOCALIZATION_NONE'
    adapter = (ROOT / 'src/ui/UserInterfaceRetained.cpp').read_text(encoding='utf-8')
    assert 'key == K_JOY16 ? "onSectionPrevious" : key == K_JOY15 ? "onSectionNext"' in adapter, 'the triggers page sections'
    assert 'key == K_JOY6 ? "onSecondary" : key == K_JOY5 ? "onTertiary"' in adapter, 'west and north are secondary and tertiary'
    assert {'mpMatch', 'mpMatchSelect'} <= cpp_allowlist(adapter)
    assert 'mpMatchRole' in cpp_allowlist(adapter, 'bool SessionMenuValueCommand(')
    # A number field's value goes through the session and comes back a frame
    # later; the runtime accepts a number only once it reads it back, so the
    # adapter acknowledges the field's proposal then (or refuses it after a
    # second), never as it sends the request.
    dispatch = function_body(adapter, 'bool idUserInterfaceRetained::DispatchApplicationActions(')
    value_branch = dispatch[dispatch.index('if (invocation.operation == "session.menuValue") {'):]
    value_branch = value_branch[:value_branch.index('continue;')]
    assert 'widget->role == ControlRole::Number && impl->sessionNumbers.size() < 64' in value_branch
    assert 'impl->sessionNumbers.push_back({pending.control,pending.proposalToken,static_cast<double>(*value),RetainedUI_PresentationTime()+1});' in value_branch
    settle = function_body(adapter, 'void SettleSessionNumbers() {')
    assert 'runtime->AcknowledgeControlProposal(it->control,it->token,readBack);' in settle and 'now < it->deadline' in settle
    assert 'impl->SettleSessionNumbers();' in function_body(adapter, 'void idUserInterfaceRetained::Redraw(')


def prompt_bar_fits(document: dict, name: str) -> None:
    """The card's prompt bar, measured from the document itself in every
    language's shipped faces (its texts, sizes, paddings and margins), fits
    inside the card's insets: the generator fits the card to the bar, and the
    native test's host cannot see real text widths."""
    import build_retained_screens as screens
    import retained_mp_menus

    def find(node, ident):
        if node['id'] == ident:
            return node
        for child in node.get('children', []):
            found = find(child, ident)
            if found:
                return found
        return None

    def dp(node, key):
        value = node.get('properties', {}).get(key)
        return float(value['value']) if value and value.get('unit') == 'dp' else 0.0

    def width(node, language):
        props = node.get('properties', {})
        if props.get('position', {}).get('value') == 'absolute':
            return 0.0
        outer = dp(node, 'margin-left') + dp(node, 'margin-right')
        if node['type'] == 'text':
            text = retained_mp_menus.any_text(props['text']['value'])[language]
            return outer + screens.text_width(props['font-family']['value'], text, props['font-size']['value'])
        inner = dp(node, 'padding-left') + dp(node, 'padding-right')
        return outer + inner + sum(width(child, language) for child in node.get('children', []))

    card = find(document['root'], 'card')
    room = card['properties']['width']['value'] - 2 * retained_mp_menus.INSET
    bar = find(document['root'], 'prompts')
    items = [child for child in bar['children'] if child['id'] != 'prompt-spacer']
    for language in screens.LANGUAGES:
        needed = sum(width(item, language) for item in items)
        assert needed <= room, f'{name}: the {language} prompt bar needs {needed:.0f} dp, past the card\'s {room:g} dp'


def check_create_server(menu: str) -> None:
    """The Create Server sub-page's tables against its generator, its opt-in
    and what it does not offer yet."""
    sys.path.insert(0, str(ROOT / 'tools' / 'ui'))
    import retained_create_server as create
    text = menu.replace('\r\n', '\n')
    assert re.search(r'RETAINED_CREATE_MAP_ROWS = (\d+);', text).group(1) == str(create.MAP_ROWS)
    gametypes = re.findall(r'"([^"]+)"', re.search(r'RETAINED_CREATE_GAMETYPES\[\] = \{([^}]*)\};', text).group(1))
    assert tuple(gametypes) == create.GAMETYPES, gametypes
    # Every game type has exactly one limit row (the page chooses it).
    assert sorted([*create.FRAG_TYPES, *create.CAPTURE_TYPES, *create.CONTROL_TYPES]) == list(range(len(create.GAMETYPES)))
    rows = re.findall(r'\{ "(create[A-Za-z]+)",\s+"([a-z]+)",\s+"([A-Za-z_]+)",\s+(-?\d+), (\d+),\s+(true|false) \}',
                      re.search(r'RETAINED_CREATE_SETTINGS\[\] = \{(.*?)\n\};', text, re.S).group(1))
    offered = [(verb, state, minimum, maximum, kind) for state, verb, _label, kind, minimum, maximum, _message in create.SETTINGS + create.ADVANCED
               if state != 'gametype']
    assert [(verb, state, int(low), int(high)) for verb, state, _cvar, low, high, _boolean in rows] == \
        [(verb, state, low, high) for verb, state, low, high, _kind in offered], 'the session offers the generator\'s settings'
    assert [boolean == 'true' for *_rest, boolean in rows] == [kind == 'toggle' for *_rest, kind in offered]
    # The adapter takes exactly the page's verbs.
    adapter = (ROOT / 'src/ui/UserInterfaceRetained.cpp').read_text(encoding='utf-8', errors='replace')
    assert {verb for verb in cpp_allowlist(adapter, 'bool SessionMenuValueCommand(') if verb.startswith('create')} == \
        {'createGameType'} | {verb for verb, *_rest in rows}
    assert {verb for verb in cpp_allowlist(adapter, 'bool SessionMenuCommand(') if verb.startswith('create')} == \
        {'createBack', 'createMap', 'createStart', 'createName', 'createMapCycle', 'createPassword', 'createRconPassword',
         'createMapsNewer', 'createMapsOlder', 'createBanList', 'createBanPick', 'createBanRemove'}
    # The text fields against the generator: the name, the map cycle and the
    # two passwords, each capped at its stock field's length.
    texts = re.findall(r'\{ "(create[A-Za-z]+)",\s+"(create\.[a-z]+)",\s+"([A-Za-z_]+)",\s+(\d+) \}',
                       re.search(r'RETAINED_CREATE_TEXTS\[\] = \{(.*?)\n\};', text, re.S).group(1))
    assert [(verb, state, int(limit)) for verb, state, _cvar, limit in texts] == \
        [('createName', 'create.name', create.NAME_BYTES), ('createMapCycle', 'create.mapcycle', create.MAPCYCLE_BYTES)] + \
        [(verb, f'create.{state}', create.PASSWORD_BYTES) for state, verb, _label, _event in create.PASSWORDS], texts
    assert [cvar for _verb, _state, cvar, _limit in texts] == ['si_name', 'g_mapCycle', 'g_password', 'net_serverRemoteConsolePassword']
    # Nothing of the stock page is missing, so the page is the default.
    missing = re.search(r'RETAINED_CREATE_SERVER_MISSING\[\] = \{([^}]*)\};', text).group(1)
    assert missing.strip() == 'NULL', missing
    document = json.loads(re.sub(r'^\s*//.*$', '', (ROOT / 'content/baseoq4/pak0/guis/menu/createserver.q4ui').read_text(encoding='utf-8'), flags=re.M))
    assert {'createBanList', 'createBanPick', 'createBanRemove'} <= set(document['actions'])
    assert re.search(r'RETAINED_CREATE_BAN_ROWS = (\d+);', text).group(1) == str(create.BAN_ROWS)
    # Retail's session commands for the stock page: the player counts' guards
    # and the advanced popup's ban list, which the game fills and empties.
    main_menu = function_body(text, 'void idSessionLocal::HandleMainMenuCommands(')
    for command in ('checkMinPlayers', 'checkMaxPlayers', 'populateBanList', 'handleServerAdminRemoveBan'):
        assert f'if ( !idStr::Icmp( cmd, "{command}" ) ) {{' in main_menu, command
    assert main_menu.count('game->PopulateBanList( guiActive );') == 2 and 'game->RemoveGuidFromBanList( guid.c_str() );' in main_menu
    assert 'cvarSystem->SetCVarInteger( "si_minPlayers", maxPlayers );' in main_menu and 'cvarSystem->SetCVarInteger( "si_maxPlayers", minPlayers );' in main_menu
    # Each text field's control: the passwords masked and possibly empty, the
    # map cycle possibly empty, the name neither.
    controls, pending = {}, [document['root']]
    while pending:
        node = pending.pop()
        if 'control' in node:
            controls[node['id']] = node['control']
        pending += node.get('children', [])
    policy = {ident: (control.get('masked', False), control.get('allowEmpty', False), control['maxBytes'])
              for ident, control in controls.items() if control['role'] == 'text'}
    assert policy == {'create-name': (False, False, 25), 'create-mapcycle': (False, True, 200),
                      'create-password': (True, True, 16), 'create-rconpassword': (True, True, 16)}, policy
    assert ('static bool Session_RetainedCreateServerEnabled( void ) {\n\treturn Session_RetainedScreensEnabled() && '
            '( ui_retainedMultiplayer.GetBool() || RETAINED_CREATE_SERVER_MISSING[0] == NULL );') in text
    # The stock page and this one start a server the same way.
    assert 'CommitStartServerMapSelection( guiMainMenu );\n\t\t\tif ( StartMenuServer() ) {\n\t\t\t\treturn;\n\t\t\t}\n\t\t\tcontinue;' in text
    assert '\tStartMenuServer();' in function_body(text, 'void idSessionLocal::HandleRetainedCreateServerRequest(')


# The stock browser's controls (p_mp_browse's buttons) and what the retained
# Join Game page offers for each: a session verb, a node of its own, or the
# name RETAINED_JOIN_GAME_MISSING gives what it lacks. The stock list's rows
# are the page's (joinPick, joinServer).
STOCK_BROWSER = {
    'mp_b_browse_sortFavorite': 'joinSortFavorite', 'mp_b_browse_sortLocked': 'joinSortLocked', 'mp_b_browse_sortServer': 'joinSortName',
    'mp_b_browse_sortPing': 'joinSortPing', 'mp_b_browse_sortPlayers': 'joinSortPlayers', 'mp_b_browse_sortGametype': 'joinSortGameType',
    'mp_b_browse_sortMap': 'joinSortMap', 'mp_b_browse_src_internet': 'joinSourceInternet', 'mp_b_browse_src_lan': 'joinSourceLan',
    'mp_b_browse_clearSort': 'joinClearSort', 'mp_b_browse_refreshlist': 'joinRefresh', 'mp_b_browse_getnewlist': 'joinNewList',
    'mp_b_browse_joinserver': 'joinServer', 'mp_b_browse_back': 'joinBack',
    'mp_b_browse_uparrow': 'servers_scroll', 'mp_b_browse_dnarrow': 'servers_scroll',
    'mp_b_browse_sortDed': 'joinSortDedicated', 'mp_b_browse_sortRepeater': 'joinSortRepeater',
    'mp_b_browse_filter_prev_mod': 'joinModPrevious', 'mp_b_browse_filter_next_mod': 'joinModNext',
    'mp_b_browse_joinipaddress': 'join_ip', 'mp_b_browse_findplayerclan': 'join_find', 'mp_b_browse_favsel': 'joinFavorite',
}
# What the stock page shows beyond its buttons: the chosen server's levelshot.
STOCK_BROWSER_PANELS = {'mp_browse_info_mapshot': 'join-shot'}
# Stock controls the page need not offer: PunkBuster is a discontinued service
# (ui-visual-design.md's symbol table: no replacement column required).
STOCK_BROWSER_EXEMPT = {'mp_b_browse_sortPb'}


def check_join_game(menu: str) -> None:
    """The Join Game sub-page's tables against its generator and the server
    scan, its opt-in, and what it does not offer yet: each of the stock
    browser's controls is the page's, or RETAINED_JOIN_GAME_MISSING names it."""
    sys.path.insert(0, str(ROOT / 'tools' / 'ui'))
    import retained_join_game as join
    text = menu.replace('\r\n', '\n')
    assert re.search(r'RETAINED_JOIN_ROWS = (\d+);', text).group(1) == str(join.JOIN_ROWS)
    header = (ROOT / 'src/framework/async/ServerScan.h').read_text(encoding='utf-8')
    order = re.findall(r'(SORT_[A-Z]+)', re.search(r'typedef enum \{(.*?)\} serverSort_t;', header, re.S).group(1))
    sorts = re.findall(r'\{ "(join[A-Za-z]+)",\s+(SORT_[A-Z]+) \}', re.search(r'RETAINED_JOIN_SORTS\[\] = \{(.*?)\n\};', text, re.S).group(1))
    assert [(verb, order.index(sort)) for verb, sort in sorts] == [(verb, sort) for _c, sort, _k, _l, _w, verb in join.COLUMNS], sorts
    assert order.index('SORT_NONE') == 11  # the page marks no column for it
    adapter = (ROOT / 'src/ui/UserInterfaceRetained.cpp').read_text(encoding='utf-8', errors='replace')
    document = json.loads(re.sub(r'^\s*//.*$', '', (ROOT / 'content/baseoq4/pak0/guis/menu/joingame.q4ui').read_text(encoding='utf-8'), flags=re.M))
    commands = {action['arguments']['command'] for action in document['actions'].values() if action['operation'] == 'session.menu'}
    # The page's own verbs, and the session's notice layer's (section 13.13).
    shared = {f'noticeAction{slot}' for slot in range(3)}
    allowed = cpp_allowlist(adapter, 'bool SessionMenuCommand(')
    assert shared <= commands and shared <= set(allowed), sorted(commands)
    assert {verb for verb in allowed if verb.startswith('join')} == commands - shared, sorted(commands)
    nodes, pending = set(), [document['root']]
    while pending:
        node = pending.pop()
        nodes.add(node['id'])
        pending += node.get('children', [])
    legacy_menu = (ROOT / 'content/baseoq4/pak0/guis/mainmenu.gui').read_text(encoding='utf-8', errors='replace')
    stock = set()
    for match in re.finditer(r'windowDef (mp_b_browse_\w+)\s*\{', legacy_menu):
        following = legacy_menu.find('windowDef ', match.end())
        if 'onAction' in legacy_menu[match.end():following if following >= 0 else len(legacy_menu)]:
            stock.add(match.group(1))
    assert stock == set(STOCK_BROWSER) | STOCK_BROWSER_EXEMPT, sorted(stock ^ (set(STOCK_BROWSER) | STOCK_BROWSER_EXEMPT))
    for name in STOCK_BROWSER_PANELS:
        assert re.search(rf'windowDef {name}\s*\{{', legacy_menu), name
    targets = {*STOCK_BROWSER.values(), *STOCK_BROWSER_PANELS.values()}
    lacking = {target for target in targets if target not in commands and target not in nodes}
    missing = set(re.findall(r'"([a-z]+)"', re.search(r'RETAINED_JOIN_GAME_MISSING\[\] = \{([^}]*)\};', text).group(1)))
    assert missing == lacking, f'RETAINED_JOIN_GAME_MISSING lists {sorted(missing)}; the page lacks {sorted(lacking)}'
    assert ('static bool Session_RetainedJoinGameEnabled( void ) {\n\treturn Session_RetainedScreensEnabled() && '
            '( ui_retainedMultiplayer.GetBool() || RETAINED_JOIN_GAME_MISSING[0] == NULL );') in text
    # The page drives the scan as the stock browser does: the same scans on
    # the legacy menu's source, its sorts, and a connect to the address.
    scans = function_body(text, 'static void Session_ScanServers(')
    main_menu = re.sub(r'\(\s+\)', '()', function_body(text, 'void idSessionLocal::HandleMainMenuCommands('))
    for line in ('servers.GUIInit();', 'cmdSystem->BufferCommandText( CMD_EXEC_NOW, "LANScan" );', 'idAsyncNetwork::GetNETServers();',
                 'servers.AddFavoriteServers();', 'servers.NetScan();'):
        assert line in scans, line
    assert ('if ( !idStr::Icmp( cmd, "UpdateServers" ) || !idStr::Icmp( cmd, "RefreshServers" ) ) {\n'
            '\t\t\tSession_ScanServers( guiActive->State().GetBool( "lanSet" ), !idStr::Icmp( cmd, "UpdateServers" ) );') in main_menu
    handler = function_body(text, 'void idSessionLocal::HandleRetainedJoinGameRequest(')
    assert 'connectArgs.AppendArg( retainedJoinAddress.c_str() );' in handler and 'cmdSystem->BufferCommandArgs( CMD_EXEC_NOW, connectArgs );' in handler
    assert 'guiMainMenu->SetStateBool( "lanSet",' in handler and 'servers.ResetSorting();' in handler
    # Each frame the page presents, its list follows the scan.
    assert 'UpdateRetainedMultiplayerPage();\n\tUpdateRetainedJoinGame();' in text
    # JOIN IP ADDRESS: the page and the stock popup connect through one check
    # (no control, quote, semicolon or backslash; resolved; IPv4, IPv6 or
    # loopback; the default port), and the stock popup's "your ip address:"
    # and FIND PLAYER/CLAN work again (retail's getClientIP; a local search,
    # as no master answers retail's). A new list ends a search.
    typed = function_body(text, 'static bool Session_ConnectToTypedAddress(')
    for line in ("valid = *p > 32 && *p != 127 && *p != '\"' && *p != ';' && *p != '\\\\';", 'Sys_StringToNetAdr( text, &address, true )',
                 '( address.type != NA_IP && address.type != NA_IP6 && address.type != NA_LOOPBACK )', 'address.port = PORT_SERVER;'):
        assert line in typed, line
    assert 'if ( !Session_ConnectToTypedAddress( s, error ) ) {' in main_menu
    assert 'guiMainMenu->SetStateString( "net_ip", Session_LocalAddressText().c_str() );' in main_menu
    assert 'idAsyncNetwork::client.serverList.SetPlayerSearch( guiActive->State().GetString( "playerclan_search" ) );' in main_menu
    assert scans.index('servers.SetPlayerSearch( "" );') < scans.index('servers.GUIInit();')
    # The details card: the session's line counts are the generator's, the
    # document defines the card on join.detail, and each row names its row.
    assert re.search(r'RETAINED_JOIN_DETAIL_PLAYERS = (\d+);', text).group(1) == str(join.DETAIL_PLAYERS)
    assert re.search(r'RETAINED_JOIN_DETAIL_SETTINGS = (\d+);', text).group(1) == str(join.DETAIL_SETTINGS)
    page = json.loads(re.sub(r'^\s*//.*$', '', (ROOT / 'content/baseoq4/pak0/guis/menu/joingame.q4ui').read_text(encoding='utf-8'), flags=re.M))
    assert page['detail'] == {'state': 'join.detail', 'card': 'join-detail', 'none': -1}, page['detail']
    rows = {}
    stack = [page['root']]
    while stack:
        node = stack.pop()
        stack += node.get('children', [])
        if node['id'].startswith('server-') and node['id'][7:].isdigit():
            rows[int(node['id'][7:])] = node['control'].get('detail')
    assert len(rows) == join.JOIN_ROWS and all(rows[row] == row for row in rows), 'every row names its row'
    # The scan's progress reaches the page; a server's name in the notice is
    # read as a player's text.
    publish = function_body(text, 'void idSessionLocal::PublishRetainedJoinGame(')
    assert 'gui->SetStateFloat( "join.progress", servers.GetLoadProgress() );' in publish
    assert 'gui->SetStateString( "join.notice", Session_RetainedPlayerText( servers.GetLoadNotice(), 160, 1 ).c_str() );' in publish


def check_retained_multiplayer(menu: str, session: str) -> None:
    """The multiplayer card's contracts that the compiled cases cannot see:
    its opt-in and missing pages, the game's side of the cover protocol, the
    frame, input and draw order, and the document the generator writes."""
    assert 'idCVar ui_retainedMultiplayer( "ui_retainedMultiplayer", "0", CVAR_GUI | CVAR_BOOL' in menu
    assert ('const char *const *missing = welcome ? RETAINED_MP_WELCOME_MISSING_PAGES : RETAINED_MP_ESCAPE_MISSING_PAGES;\n'
            '\treturn ui_retainedMultiplayer.GetBool() || ( Session_RetainedScreensEnabled() && missing[0] == NULL );') in menu.replace('\r\n', '\n')
    sys.path.insert(0, str(ROOT / 'tools' / 'ui'))
    import retained_mp_menus
    text = (ROOT / 'content/baseoq4/pak0/guis/menu/mp_escape.q4ui').read_text(encoding='utf-8')
    document = json.loads(re.sub(r'^\s*//.*$', '', text, flags=re.M))
    # A page is missing while it hands off to its stock page.
    handoffs = {name[len('stock_'):] for name in document['events'] if name.startswith('stock_')}
    missing = re.search(r'RETAINED_MP_ESCAPE_MISSING_PAGES\[\] = \{([^}]*)\};', menu).group(1)
    assert missing.strip().endswith('NULL'), 'the missing-pages list must stay NULL-terminated'
    listed = set(re.findall(r'"([a-z]+)"', missing))
    assert listed == handoffs, f'RETAINED_MP_ESCAPE_MISSING_PAGES lists {sorted(listed)}; the card hands off {sorted(handoffs)}'
    pages = re.search(r'RETAINED_MP_STOCK_PAGES\[\] = \{([^}]*)\};', menu).group(1)
    assert re.findall(r'"([a-z_]+)"', pages) == [window for _ident, _key, window in retained_mp_menus.ESCAPE_TABS]
    stock_menu = (ROOT / 'content/baseoq4/pak0/guis/mpmain.gui').read_text(encoding='utf-8', errors='replace')
    for window in re.findall(r'"([a-z_]+)"', pages):
        assert f'windowDef {window}\n' in stock_menu.replace('\r\n', '\n'), f'the stock menu has no {window}'
    for index, (ident, _key, _window) in enumerate(retained_mp_menus.ESCAPE_TABS):
        event = document['events'].get(f'stock_{ident}')
        if event is not None:
            assert event == [{'op': 'setState', 'values': {'card.stock_page': index}}, {'op': 'action', 'action': 'mpStockPage'}], ident
    # The Welcome card: its missing pages are its hand-offs, and each presses
    # the join panel's button for its page.
    welcome_text = (ROOT / 'content/baseoq4/pak0/guis/menu/mp_welcome.q4ui').read_text(encoding='utf-8')
    welcome_document = json.loads(re.sub(r'^\s*//.*$', '', welcome_text, flags=re.M))
    welcome_handoffs = {name[len('stock_'):] for name in welcome_document['events'] if name.startswith('stock_')}
    welcome_missing = re.search(r'RETAINED_MP_WELCOME_MISSING_PAGES\[\] = \{([^}]*)\};', menu).group(1)
    assert welcome_missing.strip().endswith('NULL') and set(re.findall(r'"([a-z]+)"', welcome_missing)) == welcome_handoffs
    welcome_pages = re.search(r'RETAINED_MP_WELCOME_STOCK_PAGES\[\] = \{([^}]*)\};', menu).group(1)
    assert [None if item.strip() == 'NULL' else item.strip().strip('"') for item in welcome_pages.split(',')] == \
        [window for _ident, _key, window in retained_mp_menus.WELCOME_TABS]
    # Once the join panel has closed, the same pages hand off to the menu
    # column's buttons, the Escape card's.
    menu_pages = re.search(r'RETAINED_MP_WELCOME_MENU_PAGES\[\] = \{([^}]*)\};', menu).group(1)
    escape_windows = {ident: window for ident, _key, window in retained_mp_menus.ESCAPE_TABS}
    assert [None if item.strip() == 'NULL' else item.strip().strip('"') for item in menu_pages.split(',')] == \
        [None if window is None else escape_windows[ident] for ident, _key, window in retained_mp_menus.WELCOME_TABS]
    for index, (ident, _key, window) in enumerate(retained_mp_menus.WELCOME_TABS):
        if window is not None:
            assert f'windowDef {window}\n' in stock_menu.replace('\r\n', '\n'), f'the stock menu has no {window}'
            # A page that still hands off presses the page's button.
            handoff = welcome_document['events'].get(f'stock_{ident}', welcome_document['events'].get(f'classic_{ident}'))
            assert handoff in (None, [{'op': 'setState', 'values': {'card.stock_page': index}}, {'op': 'action', 'action': 'mpStockPage'}]), ident
    # Name and clan are the card's own text fields on both Settings pages, the
    # Match page's referee credential a masked one; none hands off.
    def card_controls(node):
        found = {node['id']: node['control']} if 'control' in node else {}
        for child in node.get('children', ()):
            found.update(card_controls(child))
        return found
    for card_document in (document, welcome_document):
        assert 'classic_settings' not in card_document['events']
        fields = card_controls(card_document['root'])
        for ident, state, verb, size, empty in (('settings-name', 'mp.settings.name_edit', 'mpSettingsName', 23, False),
                                                ('settings-clan', 'mp.settings.clan_edit', 'mpSettingsClan', 12, True)):
            field = fields[ident]
            assert field['role'] == 'text' and field['value'] == {'state': state} and field['maxBytes'] == size and \
                field.get('allowEmpty', False) == empty, ident
            assert card_document['actions'][field['action']]['arguments']['key'] == state
            assert card_document['events'][card_document['actions'][field['action']]['arguments']['event']] == [{'op': 'action', 'action': verb}]
    escape_fields = card_controls(document['root'])
    referee = escape_fields['match-referee-login']
    assert referee['role'] == 'text' and referee.get('masked') and referee['value'] == {'state': 'card.referee_credential'}
    assert document['events']['match_referee_entered'] == [{'op': 'if', 'condition': {'state': 'mp.match.op.referee_login.available'},
                                                            'then': [{'op': 'action', 'action': 'mpRefereeLogin'}]}]
    password = escape_fields['admin-password']
    assert password['role'] == 'text' and password.get('masked') and password['value'] == {'state': 'card.admin.password'}
    assert escape_fields['admin-command']['role'] == 'text' and escape_fields['admin-command']['value'] == {'state': 'card.admin.command'}
    # The Match page is built; the rows past the card's open the stock page.
    escape_match = [index for index, (ident, _key, _window) in enumerate(retained_mp_menus.ESCAPE_TABS) if ident == 'match'][0]
    assert document['events']['classic_match'] == [{'op': 'setState', 'values': {'card.stock_page': escape_match}},
                                                   {'op': 'action', 'action': 'mpStockPage'}]
    assert 'match' not in handoffs and 'admin' not in handoffs
    assert welcome_document['events']['onBack'] == [{'op': 'action', 'action': 'mpClose'}]
    prompt_bar_fits(document, 'mp_escape')
    prompt_bar_fits(welcome_document, 'mp_welcome')
    # The game answers the protocol the session expects, and its menu stops
    # drawing while covered.
    protocol = int(re.search(r'static const int RETAINED_MP_PROTOCOL = (\d+);', menu).group(1))
    mp_game = (ROOT / 'src/mpgame/MultiplayerGame.cpp').read_text(encoding='utf-8', errors='replace')
    assert f'static const int RETAINED_MENU_PROTOCOL = {protocol};' in mp_game
    cover = function_body(mp_game, 'void idMultiplayerGame::SetRetainedMenuCover(')
    assert 'card->SetStateInt( "mp.protocol", RETAINED_MENU_PROTOCOL );' in cover
    # The join offer is the Welcome card's; the card's softening replaces the join panel's blur.
    assert ('const bool allowed = ( ( currentMenu == 1 && mainGui != NULL ) || ( currentMenu == 3 && statSummary != NULL ) ) &&\n'
            '\t\t!IsArenaCampaignMatch();') in cover.replace('\r\n', '\n')
    assert cover.index('retainedMenuCovered = true;') < cover.index('SetJoinScreenSoftFocus( false );')
    assert ('return joinScreenPending || ( player != NULL && player->spectating && !cvarSystem->GetCVarBool( "ui_joined" ) &&\n'
            '\t\t!cvarSystem->GetCVarBool( "ui_autoJoin" ) );' in function_body(mp_game, 'bool idMultiplayerGame::RetainedMenuWelcome(').replace('\r\n', '\n'))
    draw = function_body(mp_game, 'bool idMultiplayerGame::Draw(')
    assert 'if ( !retainedMenuCovered ) {\n\t\t\t\tmainGui->Redraw( gameLocal.time );' in draw.replace('\r\n', '\n')
    assert 'retainedMenuCovered = false;' in function_body(mp_game, 'void idMultiplayerGame::DisableMenu(')
    mp_local = (ROOT / 'src/mpgame/Game_local.cpp').read_text(encoding='utf-8', errors='replace')
    for verb, call in (('retainedMultiplayerVariant', 'gui->SetStateBool( "mp.welcome", mpGame.RetainedMenuWelcome() );'),
                       ('retainedMultiplayerCover', 'mpGame.SetRetainedMenuCover( true, gui );'),
                       ('retainedMultiplayerUncover', 'mpGame.SetRetainedMenuCover( false, gui );'),
                       ('retainedMultiplayerState', 'mpGame.PublishRetainedMenu( gui );')):
        answer = mp_local[mp_local.index(f'!idStr::Icmp( menuCommand, "{verb}" )'):]
        assert call in answer[:answer.index('} else if')], verb
    # The frame pump updates the card before its no-GUI return, input reaches
    # the card after the home screen's, and the card draws in the menu's place.
    frame = function_body(menu, 'void idSessionLocal::GuiFrameEvents(')
    assert frame.index('UpdateRetainedSubpage();') < frame.index('UpdateRetainedMultiplayer();') < frame.index('RetainedMultiplayerFrameEvent();') < \
        frame.index('ClearMenuControllerRepeatState();\n\t\treturn;\n\t}\n\n\tif ( guiActive ) {')
    menu_event = function_body(menu, 'void idSessionLocal::MenuEvent(')
    assert menu_event.index('if ( guiRetainedHome != NULL && guiActive == guiMainMenu ) {') < menu_event.index('if ( RetainedMultiplayerCovers() ) {') < \
        menu_event.index('menuCommand = guiActive->HandleEvent(')
    dispatch = function_body(menu, 'void idSessionLocal::DispatchCommand(')
    assert 'else if ( gui == guiRetainedMultiplayer ) HandleRetainedMultiplayerRequest( gui, "mpClose" );' in dispatch
    assert 'HandleGameMenuReturn( game->HandleGuiCommands( menuCommand ) );' in dispatch
    drawing = function_body(session, 'void idSessionLocal::Draw(')
    assert ('} else if ( RetainedMultiplayerCovers() ) {' in drawing and
            drawing.index('UI_RunTimeEvents( guiActive, presentationTime );') < drawing.index('guiRetainedMultiplayer->Redraw( presentationTime );'))
    assert 'guiRetainedEscape = guiRetainedWelcome = guiRetainedMultiplayer = NULL;' in function_body(session, 'void idSessionLocal::Clear(')
    # Each card loads inside the level load, and the game names the card
    # before the session loads or covers with it.
    prepare = function_body(menu, 'void idSessionLocal::PrepareRetainedLevel(')
    assert 'guiRetainedEscape = FindRetainedGui( RETAINED_MP_ESCAPE_GUI, false, false );' in prepare
    assert 'guiRetainedWelcome = FindRetainedGui( RETAINED_MP_WELCOME_GUI, false, false );' in prepare
    update = function_body(menu, 'void idSessionLocal::UpdateRetainedMultiplayer(')
    assert update.index('game->HandleMainMenuCommands( "retainedMultiplayerVariant", guiActive );') < \
        update.index('const bool enabled = arena ? Session_RetainedArenaPauseEnabled() :') < \
        update.index('if ( !enabled || retainedStock.FindIndex( path ) >= 0 ) {') < update.index('document = FindRetainedGui( path, false, false );') < \
        update.index('game->HandleMainMenuCommands( "retainedMultiplayerCover", card );')
    # The summary card: loaded in the level load, the summary's own draw
    # gives way to it, and its chat field's typing takes the keyboard before
    # the card while the player types.
    assert 'guiRetainedSummary = FindRetainedGui( RETAINED_MP_SUMMARY_GUI, false, false );' in prepare
    summary_draw = function_body(mp_game, 'void idMultiplayerGame::DrawStatSummary(')
    assert 'if ( !retainedMenuCovered ) {\n\t\tstatSummary->Redraw( gameLocal.time );' in summary_draw.replace('\r\n', '\n')
    assert menu_event.index('if ( RetainedMultiplayerCovers() ) {') < menu_event.index('if ( RetainedSummaryChatEvent( event ) ) {') < \
        menu_event.index('menuCommand = card->HandleEvent(')
    typing = function_body(menu, 'bool idSessionLocal::RetainedSummaryChatEvent(')
    assert 'DispatchCommand( guiActive, "chatmessage" );' in typing and 'RETAINED_SUMMARY_CHAT_LIMIT' in typing
    gui_text = (ROOT / 'content/baseoq4/pak0/guis/summary.gui').read_text(encoding='utf-8', errors='replace').replace('\r\n', '\n')
    assert 'onNamedEvent oq4_chatFocus {\n\t\tsetfocus "chat_enter" ;' in gui_text and 'onNamedEvent oq4_chatBlur {' in gui_text
    summary_text = (ROOT / 'content/baseoq4/pak0/guis/menu/mp_summary.q4ui').read_text(encoding='utf-8')
    summary_document = json.loads(re.sub(r'^\s*//.*$', '', summary_text, flags=re.M))
    assert summary_document['events']['onBack'] == [{'op': 'action', 'action': 'mpClose'}]
    assert summary_document['events']['summary_chat'] == [{'op': 'action', 'action': 'mpSummaryChat'}]
    prompt_bar_fits(summary_document, 'mp_summary')
    release_ms = int(re.search(r'static const int RETAINED_RELEASE_MSEC = (\d+);', menu).group(1))
    timelines = {item['id']: item for item in document['timelines']}
    assert timelines['release']['durationMs'] == release_ms
    reset = document['events']['onActivate'][0]
    assert reset['op'] == 'if' and reset['condition'] == {'state': 'card.released'}
    assert document['events']['onBack'] == [{'op': 'action', 'action': 'mpClose'}]
    # The Team page's slots: the session bounds the slot, the game derives its
    # action again from the player's state, and the document offers as many.
    slots = int(re.search(r'static const int RETAINED_MP_TEAM_SLOTS = (\d+);', menu).group(1))
    header = (ROOT / 'src/mpgame/MultiplayerGame.h').read_text(encoding='utf-8', errors='replace')
    assert f'static const int RETAINED_TEAM_SLOTS = {slots};' in header
    assert slots == retained_mp_menus.TEAM_SLOTS and all(f'team_slot_{slot}' in document['events'] for slot in range(slots))
    command = function_body(mp_game, 'bool idMultiplayerGame::HandleRetainedMenuCommand(')
    assert command.index('RetainedTeamSlots( slots );') < command.index('if ( slot.action == RTA_NONE || !slot.available ) {') < \
        command.index('return RunRetainedAction( slot.action );')
    welcome_branch = command[command.index('if ( !sub.Icmp( "welcome" )'):]
    assert welcome_branch.index('RetainedWelcomeSlots( slots );') < welcome_branch.index('if ( slot.action == RTA_NONE || !slot.available ) {') < \
        welcome_branch.index('return RunRetainedAction( slot.action );')
    runner = function_body(mp_game, 'bool idMultiplayerGame::RunRetainedAction(')
    assert runner.index('default: return false;') < runner.index('DisableMenu();') < runner.index('return true;')
    # The Welcome card's Join page slots: the session bounds them, and the
    # session, the game and the document agree on four.
    welcome_slots = int(re.search(r'static const int RETAINED_MP_WELCOME_SLOTS = (\d+);', menu).group(1))
    assert f'static const int RETAINED_WELCOME_SLOTS = {welcome_slots};' in header
    assert welcome_slots == retained_mp_menus.WELCOME_SLOTS and all(f'join_slot_{slot}' in welcome_document['events'] for slot in range(welcome_slots))
    assert 'if ( slot < 0 || slot >= RETAINED_MP_WELCOME_SLOTS ) {' in function_body(menu, 'void idSessionLocal::HandleRetainedMultiplayerRequest(')
    # The Vote page is built, so it no longer hands off. The adapter's value
    # verbs are the session's field table, in the game's field order, which the
    # document's rows and the game's publisher follow; each row requests its
    # verb with its control's value.
    assert 'vote' not in handoffs
    adapter = (ROOT / 'src/ui/UserInterfaceRetained.cpp').read_text(encoding='utf-8', errors='replace')
    # (The Create Server page's verbs are check_create_server's.)
    value_verbs = {verb for verb in cpp_allowlist(adapter, 'bool SessionMenuValueCommand(') if not verb.startswith(('create', 'audio', 'game', 'controls'))}
    fields = re.findall(r'"(mpVote[A-Za-z]+)"', re.search(r'RETAINED_MP_VOTE_FIELDS\[\] = \{([^}]*)\};', menu).group(1))
    appearance = re.findall(r'"(mp[A-Za-z]+)"', re.search(r'RETAINED_MP_APPEARANCE_VALUES\[\] = \{([^}]*)\};', menu).group(1))
    choices = re.findall(r'"(mp[A-Za-z]+)"', re.search(r'RETAINED_MP_MATCH_CHOICES\[\] = \{([^}]*)\};', menu).group(1))
    admin_fields = re.findall(r'"(mpAdmin[A-Za-z]+)"', re.search(r'RETAINED_MP_ADMIN_FIELDS\[\] = \{([^}]*)\};', menu).group(1))
    assert set(fields) | set(appearance) | set(choices) | set(admin_fields) | {'mpMatchRuleValue'} == value_verbs and \
        len(fields) + len(appearance) + len(choices) + len(admin_fields) + 1 == len(value_verbs), (fields, appearance, choices, value_verbs)
    assert appearance == [*retained_mp_menus.MODEL_VERBS, 'mpCrosshair'] and choices == ['mpMatchRole', 'mpMatchScope', 'mpMatchSeriesProfile']
    enum = [name.strip() for name in re.search(r'enum retainedVoteField_t \{([^}]*)\};', header).group(1).split(',') if name.strip()]
    assert enum[-1] == 'RVF_COUNT'
    keys = [name[len('RVF_'):].lower() for name in enum[:-1]]
    assert keys == [verb[len('mpVote'):].lower() for verb in fields]
    assert [(key, verb) for key, verb, _label, _control in retained_mp_menus.VOTE_ROWS] == list(zip(keys, fields))
    # The Admin page's Server section drafts the same fields without the
    # kick: the session's table, the document's rows and the game's publisher
    # follow the game's order.
    import retained_mp_admin
    assert keys[-1] == 'kick' and [(key, verb) for key, verb, _label, _control in retained_mp_admin.ROWS] == list(zip(keys[:-1], admin_fields))
    for key, verb, _label, control in retained_mp_admin.ROWS:
        arguments = {'command': verb, 'value': {'input': 'value'}}
        assert document['actions'][verb] == {'operation': 'session.menuValue', 'input': 'boolean' if control[0] == 'toggle' else 'number',
                                             'arguments': arguments}, verb
    admin_publisher = function_body(mp_game, 'void idMultiplayerGame::PublishRetainedAdmin(')
    assert re.findall(r'"([a-z]+)"', re.search(r'static const char \*const fields\[ RVF_COUNT \] = \{([^}]*)\};', admin_publisher).group(1)) == keys
    for constant, value in (('PLAYERS', retained_mp_admin.PLAYERS), ('BANS', 32), ('CONSOLE_LINES', retained_mp_admin.CONSOLE_LINES)):
        assert f'static const int RETAINED_ADMIN_{constant} = {value};' in header, constant
    assert f'static const int RETAINED_MP_PASSWORD_BYTES = {retained_mp_admin.PASSWORD_BYTES};' in menu
    assert f'static const int RETAINED_MP_COMMAND_BYTES = {retained_mp_admin.COMMAND_BYTES};' in menu
    # The game: a client becomes an admin only by the server's answer, every
    # admin command but the password check needs it, and Apply closes the menu
    # only once the server takes the changes.
    admin_command = function_body(mp_game, 'bool idMultiplayerGame::HandleRetainedAdminCommand(')
    assert admin_command.index('if ( !sub.Icmp( "adminPass" ) ) {') < admin_command.index('if ( !RetainedAdmin() ) {') < \
        admin_command.index('if ( !sub.Icmp( "adminSelect" ) ) {')
    apply_branch = admin_command[admin_command.index('if ( !sub.Icmp( "adminApply" ) ) {'):admin_command.index('if ( !sub.Icmp( "adminConsole" ) ) {')]
    assert apply_branch.index('RetainedAdminRefusal( RAA_APPLY ) != NULL') < apply_branch.index('if ( HandleServerAdminCommands( data ) ) {') < \
        apply_branch.index('DisableMenu();')
    assert 'retainedAdminValid = success;' in function_body(mp_game, 'void idMultiplayerGame::ProcessRconReturn(')
    assert 'return gameLocal.isServer || retainedAdminValid;' in function_body(mp_game, 'bool idMultiplayerGame::RetainedAdmin(')
    assert 'retainedAdminValid = false;' in function_body(mp_game, 'void idMultiplayerGame::Clear(')
    assert cover.index('ResetRetainedVoteDraft();') < cover.index('ResetRetainedAdminDraft();') < cover.index('PublishRetainedMenu( card );')
    publisher = function_body(mp_game, 'void idMultiplayerGame::PublishRetainedVote(')
    assert re.findall(r'"([a-z]+)"', re.search(r'static const char \*const fields\[ RVF_COUNT \] = \{([^}]*)\};', publisher).group(1)) == keys
    for key, verb, _label, control in retained_mp_menus.VOTE_ROWS:
        arguments = {'command': verb, 'value': {'input': 'value'}}
        if key == 'kick':
            arguments['catalog'] = {'state': 'mp.vote.kick_catalog'}
            assert card_controls(document['root'])['vote-row-kick']['context'] == {'state': 'mp.vote.kick_catalog'}
        assert document['actions'][verb] == {'operation': 'session.menuValue', 'input': 'boolean' if control[0] == 'toggle' else 'number',
                                             'arguments': arguments}, verb
    assert f'static const int RETAINED_VOTE_LINES = {retained_mp_menus.VOTE_LINES};' in header
    request = function_body(menu, 'void idSessionLocal::HandleRetainedMultiplayerRequest(')
    assert request.index('} else if ( Session_RetainedValueRequest( request, RETAINED_MP_VOTE_FIELDS,') < \
        request.index('gameCommand = va( "retained voteSet %d %d", voteField, voteValue );') < \
        request.index('} else if ( Session_RetainedValueRequest( request, RETAINED_MP_APPEARANCE_VALUES,') < \
        request.index('gameCommand = voteField < 3 ? va( "retained appearance %d %d", voteField, voteValue ) : va( "retained crosshair %d", voteValue );')
    # The Settings pages: the game, the session and the document agree on the
    # model lists and the swatches; the adapter allows the document's
    # settings with the lists' own values; the appearance commands keep the
    # menu open.
    assert f'static const int RETAINED_MODEL_SLOTS = {retained_mp_menus.MODEL_SLOTS};' in header
    assert f'static const int RETAINED_MODEL_ROWS = {retained_mp_menus.MODEL_ROWS};' in header
    rails = len(retained_mp_menus.RAIL_SWATCHES)
    assert f'static const int RETAINED_RAIL_COLORS = {rails};' in header and f'static const int RETAINED_MP_RAIL_COLORS = {rails};' in menu
    assert len(re.findall(r'"\d+ [\d.]+ 1"', function_body(mp_game, 'const char *idMultiplayerGame::RetainedRailColor('))) == rails
    adapter_rules = player_setting_rules(adapter)
    for _label, enemy, team, _key, values in retained_mp_menus.APPEARANCE_ROWS:
        assert adapter_rules[enemy] == list(values) and adapter_rules[team] == list(values), enemy
        assert document['actions'][f'set.{enemy}']['operation'] == 'settings.player.set'
    assert adapter_rules['cl_player_outline_width'] == list(retained_mp_menus.OUTLINE_WIDTHS)
    for verb in ('appearance', 'rail', 'crosshair'):
        branch = command[command.index(f'if ( !sub.Icmp( "{verb}" )'):]
        branch = branch[:branch.index('return false;')]
        assert 'retainedMenuCovered' in branch and 'DisableMenu' not in branch, verb
    # The game: a ballot only while the player can cast one, a drafted field
    # checked against its rules without closing the menu, and a call only of
    # the fields that differ from the server's, closing the menu.
    vote_branch = command[command.index('if ( !sub.Icmp( "vote" )'):command.index('if ( !sub.Icmp( "voteSet" )')]
    assert vote_branch.index('vote == VOTE_NONE || RetainedBallotRefusal() != NULL') < \
        vote_branch.index('CastVote( gameLocal.localClientNum, yes );') < vote_branch.index('DisableMenu();')
    set_branch = command[command.index('if ( !sub.Icmp( "voteSet" )'):command.index('if ( !sub.Icmp( "callVote" )')]
    assert 'retainedMenuCovered' in set_branch and 'SetRetainedVoteField( atoi( fieldText.c_str() ), atoi( valueText.c_str() ) );' in set_branch
    assert 'DisableMenu' not in set_branch and 'return true' not in set_branch
    call_branch = command[command.index('if ( !sub.Icmp( "callVote" )'):command.index('if ( ( !sub.Icmp( "select" )')]
    assert call_branch.index('RetainedVoteRefusal() != NULL || RetainedVoteChanges( data ) == 0') < \
        call_branch.index('ClientCallPackedVote( data, retainedVoteKickIdentity );') < call_branch.index('DisableMenu();')
    changes = function_body(mp_game, 'int idMultiplayerGame::RetainedVoteChanges(')
    assert 'retainedVoteDraft[ limit.field ] != si.GetInt( limit.key )' in changes and 'retainedVoteMap.Icmp( current ) != 0' in changes
    assert cover.index('retainedMenuCovered = true;') < cover.index('ResetRetainedVoteDraft();') < cover.index('PublishRetainedMenu( card );')
    # HandleGuiCommands' _XENON branches defeat a brace count; pin the statement.
    assert mp_game.replace('\r\n', '\n').count('if ( HandleRetainedMenuCommand( args, icmd ) ) {\n\t\t\t\treturn NULL;') == 1
    # The Players page: the session bounds the client to the server's slots,
    # and the game acts only on a client with a row now, never mutes or
    # befriends the player's own row, and keeps the menu open.
    assert 'static const int RETAINED_MP_CLIENTS = MAX_ASYNC_CLIENTS;' in menu
    assert 'if ( client < 0 || client >= RETAINED_MP_CLIENTS || !digits( identity, 10 ) ) {' in function_body(menu, 'void idSessionLocal::HandleRetainedMultiplayerRequest(')
    listed = command.index('if ( client < 0 || client >= MAX_CLIENTS || !RetainedListed( client ) ||')
    assert listed < command.index('retainedStatClient = client;')
    own = command.index('} else if ( local != NULL && client != local->entityNumber ) {')
    assert listed < own < command.index('ClientVoiceMute( client, !local->IsPlayerMuted( client ) );') < command.index('ToggleFriend( client );')
    players = command[listed:]
    assert 'DisableMenu' not in players and 'return true' not in players, 'the Players page commands keep the menu open'
    # The game and the document agree on the lists, the weapons and the awards.
    assert f'static const int RETAINED_PLAYER_LISTS = {len(retained_mp_menus.PLAYER_LISTS)};' in header
    assert f'static const int RETAINED_PLAYER_ROWS = {retained_mp_menus.PLAYER_ROWS};' in header
    weapons = re.search(r'RETAINED_ACCURACY_WEAPONS\[\] = \{([^}]*)\};', mp_game).group(1)
    assert re.findall(r'"([a-z_]+)"', weapons) == [weapon for weapon, _icon, _tint in retained_mp_menus.WEAPONS]
    awards = re.search(r'RETAINED_AWARDS\[\] = \{([^}]*)\};', mp_game).group(1)
    assert re.findall(r'IGA_[A-Z_]+', awards) == ['IGA_' + award.upper() for award in retained_mp_menus.AWARDS]
    for kind, _tints in retained_mp_menus.PLAYER_LISTS:
        assert all(f'players_{kind}{row}' in document['events'] for row in range(retained_mp_menus.PLAYER_ROWS)), kind
    publish = function_body(mp_game, 'void idMultiplayerGame::PublishRetainedMenu(')
    assert publish.index('PublishRetainedPlayers( card, changed );') < publish.index('if ( changed ) {')
    # The deathmatch header's rank text drops the HUD's colour codes.
    assert 'publish( "mp.score", RetainedScoreLine().c_str() );' in publish
    assert 'score = MPRetainedPlainText( GetPlayerRankText( player ), 96, 1 );' in function_body(mp_game, 'idStr idMultiplayerGame::RetainedScoreLine(')
    # The stock Friend button keeps the same mark the page shows.
    assert mp_game.count('ToggleFriend( client );') == 2


def main() -> int:
    menu = (ROOT / 'src/framework/Session_menu.cpp').read_text(encoding='utf-8')
    session = (ROOT / 'src/framework/Session.cpp').read_text(encoding='utf-8')
    adapter = (ROOT / 'src/ui/UserInterfaceRetained.cpp').read_text(encoding='utf-8')

    # The gate itself: enabled by default in current source builds;
    # archive an explicit player preference for either interface.
    assert 'idCVar ui_retained( "ui_retained", "1", CVAR_GUI | CVAR_BOOL | CVAR_ARCHIVE, ' in menu, 'ui_retained must default to 1 and be archived'
    assert 'ui_retainedSystem( "ui_retainedSystem", "0", CVAR_GUI | CVAR_BOOL' in menu
    # The opt-ins and the runtime's test settings last until the player quits,
    # and survive a multiplayer join: any other unarchived CVar is a cheat,
    # which joining or hosting a server without cheats resets to its default
    # (ResetFlaggedVariables( CVAR_CHEAT )) and a player cannot set in a match.
    host = (ROOT / 'src/ui/RetainedUI.cpp').read_text(encoding='utf-8')
    for text, name in ((menu, 'ui_retainedSystem'), (menu, 'ui_retainedMultiplayer'),
                       (host, 'ui_retainedDensity'), (host, 'ui_retainedTrace')):
        declared = re.search(rf'idCVar {name}\(\s*"{name}",\s*"[^"]*",\s*([A-Z_ |]+),', text)
        assert declared, f'{name} is no longer declared where the gate looks for it'
        flags = {flag.strip() for flag in declared.group(1).split('|')}
        assert 'CVAR_NOCHEAT' in flags and 'CVAR_ARCHIVE' not in flags, f'{name} must stay unarchived and CVAR_NOCHEAT: {sorted(flags)}'
    assert ('return ui_retainedSystem.GetBool() || ( Session_RetainedScreensEnabled() &&\n'
            '\t\tRETAINED_SYSTEM_MISSING_SETTINGS[0] == NULL && RETAINED_SYSTEM_INCOMPLETE_SETTINGS[0] == NULL );') in menu
    # The SYSTEM page joins the gate once it has a control for every setting of
    # the stock page and each one works there; the two lists must say exactly that.
    missing = re.search(r'RETAINED_SYSTEM_MISSING_SETTINGS\[\] = \{([^}]*)\};', menu).group(1)
    listed = {name.lower() for name in re.findall(r'"([A-Za-z_0-9]+)"', missing)}
    assert missing.strip().endswith('NULL'), 'the missing-settings list must stay NULL-terminated'
    # CVar names are case-insensitive (the stock page binds r_multisamples).
    actual = {name.lower() for name in stock_system_settings()} - {name.lower() for name in retained_system_controls()}
    assert listed == actual, f'RETAINED_SYSTEM_MISSING_SETTINGS lists {sorted(listed)}; the retained SYSTEM page lacks {sorted(actual)}'
    check_retained_system_control_writes()
    incomplete = re.search(r'RETAINED_SYSTEM_INCOMPLETE_SETTINGS\[\] = \{([^}]*)\};', menu).group(1)
    assert incomplete.strip().endswith('NULL'), 'the incomplete-settings list must stay NULL-terminated'
    listed_incomplete = {name.lower() for name in re.findall(r'"([A-Za-z_0-9]+)"', incomplete)}
    reasons = retained_system_incomplete(adapter)
    assert listed_incomplete == set(reasons), (
        f'RETAINED_SYSTEM_INCOMPLETE_SETTINGS lists {sorted(listed_incomplete)}; the code gives these settings a reason: '
        + '; '.join(f'{key}: {reason}' for key, reason in sorted(reasons.items())))
    assert 'if ( !Session_RetainedSystemEnabled() || systemGuiTransition' in function_body(menu, 'bool idSessionLocal::OpenSystemSettings(')
    assert 'guiMainMenu->SetStateBool( "retainedSystem", RetainedSystemAvailable() );' in menu
    # Every retained document path in the session is behind the gate.
    for source, name in ((menu, 'Session_menu.cpp'), (session, 'Session.cpp')):
        for match in re.finditer(r'"([^"]+\.q4ui)"', source):
            path = match.group(1)
            allowed = {'guis/menu/settings/system.q4ui', 'guis/menu/title.q4ui', 'guis/menu/pause.q4ui', 'guis/menu/pause_strogg.q4ui',
                       'guis/loading/loading.q4ui', 'guis/menu/singleplayer.q4ui', 'guis/menu/campaigns.q4ui', 'guis/menu/campaign_skill.q4ui', 'guis/menu/chapters.q4ui',
                       'guis/menu/mp_escape.q4ui', 'guis/menu/mp_welcome.q4ui', 'guis/menu/mp_summary.q4ui', 'guis/menu/loadgame.q4ui',
                       'guis/menu/savegame.q4ui', 'guis/menu/restartlevel.q4ui', 'guis/menu/loadgame_strogg.q4ui',
                       'guis/menu/savegame_strogg.q4ui', 'guis/menu/restartlevel_strogg.q4ui',
                       'guis/menu/demos.q4ui', 'guis/menu/restartmenu.q4ui', 'guis/menu/credits.q4ui', 'guis/menu/arena.q4ui',
                       'guis/menu/multiplayer.q4ui', 'guis/menu/createserver.q4ui', 'guis/menu/joingame.q4ui',
                       'guis/menu/arena_pause.q4ui', 'guis/menu/settings/audio.q4ui', 'guis/menu/settings/game.q4ui',
                       'guis/menu/settings/controls.q4ui', 'guis/menu/settings.q4ui'}
            assert path in allowed, f'{name} names an ungated retained document {path}'
    # Every retained document loads through FindRetainedGui, which falls back to
    # the stock screen; the SYSTEM page and the campaign selectors fall back too.
    assert not re.search(r'uiManager->FindGui\(\s*(?:RETAINED_|"[^"]+\.q4ui")', menu + session), 'retained documents load only through FindRetainedGui'
    find_retained = function_body(menu, 'idUserInterface *idSessionLocal::FindRetainedGui(')
    assert 'uiManager->FindGui( path, true, !shared, shared )' in find_retained and 'retainedStock.Append( path );' in find_retained
    system_open = function_body(menu, 'bool idSessionLocal::OpenSystemSettings(')
    assert 'FindRetainedGui( RETAINED_SYSTEM_GUI, false, false )' in system_open and 'retainedStock.Append( RETAINED_SYSTEM_GUI );' in system_open
    main_menu = function_body(menu, 'void idSessionLocal::HandleMainMenuCommands(')
    for command, multiplayer in (('openRetainedSystem', 'false'), ('openRetainedSystemFromMp', 'true')):
        assert f'if ( !idStr::Icmp( cmd, "{command}" ) ) {{\n\t\t\tOpenSystemSettingsRoute( {multiplayer} );\n\t\t\treturn;' in main_menu
    system_route = function_body(menu, 'void idSessionLocal::OpenSystemSettingsRoute(')
    # The AUDIO page: the AUDIO button asks for it while the retained screens
    # are on (the main menu's retainedAudio), the stock page's action runs
    # where it cannot open; its value verbs are the session's table, which the
    # adapter allows and the document's rows request; its rows apply through
    # the stock page's commands.
    assert 'if ( !idStr::Icmp( cmd, "openRetainedAudio" ) ) {\n\t\t\tOpenRetainedAudioRoute();\n\t\t\treturn;' in main_menu
    assert 'guiMainMenu->SetStateBool( "retainedAudio", RetainedAudioAvailable() );' in menu
    audio_route = function_body(menu, 'void idSessionLocal::OpenRetainedAudioRoute(')
    assert 'UI_RunLegacyWindowAction( guiMainMenu, "set_b_audio", false, command )' in audio_route
    stock_menu = (ROOT / 'content/baseoq4/pak0/guis/mainmenu.gui').read_text(encoding='utf-8', errors='replace')
    audio_button = stock_menu[stock_menu.index('windowDef set_b_audio'):]
    audio_action = audio_button[audio_button.index('onAction'):audio_button.index('windowDef', 1)]
    assert 'if ("gui::retainedAudio" == 1) {' in audio_action and 'set "cmd" "openRetainedAudio" ;' in audio_action
    audio_verbs = re.findall(r'"(audio[A-Za-z]+)"', re.search(r'RETAINED_AUDIO_VERBS\[\] = \{([^}]*)\};', menu).group(1))
    sys.path.insert(0, str(ROOT / 'tools' / 'ui'))
    import retained_audio
    adapter_audio = [verb for verb in cpp_allowlist(adapter, 'bool SessionMenuValueCommand(') if verb.startswith('audio')]
    assert set(adapter_audio) == set(audio_verbs) and len(adapter_audio) == len(audio_verbs) == 12
    assert [verb for _section, _heading, rows in retained_audio.SECTIONS for _kind, _ident, _label, verb, _state, _extra in rows] == audio_verbs
    audio_document = json.loads(re.sub(r'^\s*//.*$', '', (ROOT / 'content/baseoq4/pak0/guis/menu/settings/audio.q4ui').read_text(encoding='utf-8'), flags=re.M))
    assert audio_document['canvas']['height'] == 720 and audio_document['actions']['back'] == {'operation': 'session.menu', 'arguments': {'command': 'audioBack'}}
    for verb in audio_verbs:
        assert audio_document['actions'][verb]['operation'] == 'session.menuValue' and \
            audio_document['actions'][verb]['arguments'] == {'command': verb, 'value': {'input': 'value'}}, verb
    assert f'static const int RETAINED_AUDIO_DEVICES = {retained_audio.DEVICES};' in menu
    audio_request = function_body(menu, 'void idSessionLocal::HandleRetainedAudioRequest(')
    for command in ('sound drivar', 'sound eax', 'sound speakers'):
        assert f'HandleMainMenuCommands( "{command}" );' in audio_request
    # The GAME OPTIONS page: the button's route as AUDIO's; the computed rows'
    # verbs are the session's table, which the adapter allows and the document
    # requests, each applied through the stock main menu's own choice; the
    # rows that name a CVar set it with settings.player.set, whose rules are
    # the stock controls' ranges and values; the language list is the stock
    # one, in its order.
    for command, multiplayer in (('openRetainedGame', 'false'), ('openRetainedGameFromMp', 'true')):
        assert f'if ( !idStr::Icmp( cmd, "{command}" ) ) {{\n\t\t\tOpenRetainedGameRoute( {multiplayer} );\n\t\t\treturn;' in main_menu
    route_event = stock_menu[stock_menu.index('onNamedEvent fromMp_toGameoptions {'):stock_menu.index('onNamedEvent fromMp_toGameoptionsStock {')]
    assert 'if ("gui::retainedGame" == 1) {' in route_event and 'resettime "anim_retainedGameFromMp" "0" ;' in route_event
    assert 'set "cmd" "openRetainedGameFromMp" ;' in stock_menu[stock_menu.index('windowDef anim_retainedGameFromMp'):]
    assert 'guiMainMenu->SetStateBool( "retainedGame", RetainedGameAvailable() );' in menu
    assert 'UI_RunLegacyWindowAction( guiMainMenu, "set_b_gameoptions", false, command )' in function_body(menu, 'void idSessionLocal::OpenRetainedGameRoute(')
    game_button = stock_menu[stock_menu.index('windowDef set_b_gameoptions'):]
    game_action = game_button[game_button.index('onAction'):game_button.index('windowDef', 1)]
    assert 'if ("gui::retainedGame" == 1) {' in game_action and 'set "cmd" "openRetainedGame" ;' in game_action
    import retained_game
    game_verbs = re.findall(r'"(game[A-Za-z]+)"', re.search(r'RETAINED_GAME_VERBS\[\] = \{([^}]*)\};', menu).group(1))
    adapter_game = [verb for verb in cpp_allowlist(adapter, 'bool SessionMenuValueCommand(') if verb.startswith('game')]
    assert game_verbs == list(retained_game.VERBS) and sorted(adapter_game) == sorted(game_verbs)
    game_document = json.loads(re.sub(r'^\s*//.*$', '', (ROOT / 'content/baseoq4/pak0/guis/menu/settings/game.q4ui').read_text(encoding='utf-8'), flags=re.M))
    assert game_document['canvas']['height'] == 720 and game_document['actions']['back'] == {'operation': 'session.menu', 'arguments': {'command': 'gameBack'}}
    for verb in game_verbs:
        assert game_document['actions'][verb]['arguments'] == {'command': verb, 'value': {'input': 'value'}}, verb
    game_request = function_body(menu, 'void idSessionLocal::HandleRetainedGameRequest(')
    for applied in ('ApplyMainMenuGunPositionChoice( guiMainMenu );', 'ApplyMainMenuForceModelChoice( guiMainMenu );',
                    'MainMenuPublishCrosshair( guiMainMenu, 0 );', 'ApplyMainMenuCorpseTimeChoice( guiMainMenu );',
                    'HandleMainMenuCommands( "reloadLanguage ; reloadGuis" );'):
        assert applied in game_request, applied
    stock_game = (ROOT / 'content/baseoq4/pak0/guis/menu/settings/game.gui').read_text(encoding='utf-8', errors='replace')
    stock_languages = re.search(r'values\s+"(english;[^"]+)"', stock_game).group(1).split(';')
    assert re.findall(r'"([a-z]+)"', re.search(r'RETAINED_GAME_LANGUAGES\[\] = \{([^}]*)\};', menu).group(1)) == stock_languages
    assert len(stock_languages) == retained_game.LANGUAGES
    rules = player_setting_rules(adapter)
    # A setting only the retained interface honors has no stock row: the game
    # must gate it on ui_retained, or the stock page owes players the row.
    RETAINED_ONLY_GAME_SETTINGS = {'hud_crosshairAnimated': ('src/game/gamesys/AnimatedCrosshair.inl',
                                                             'hud_crosshairAnimated.GetBool() && retained != NULL && retained->GetBool()')}
    for cvar, (source, gate) in RETAINED_ONLY_GAME_SETTINGS.items():
        gated = (ROOT / source).read_text(encoding='utf-8', errors='replace')
        assert gate in gated and 'cvarSystem->Find( "ui_retained" )' in gated, f'{cvar} is not gated on ui_retained'
        assert not re.search(rf'(cvar\s+"?|consoleCMD\s+"){cvar}\b', stock_game), f'{cvar} has a stock row: drop its exemption'
    for cvar, kind, extra in retained_game.setting_rows():
        action = game_document['actions'][f'set.{cvar}']
        assert action['operation'] == 'settings.player.set' and action['arguments'] == {'cvar': cvar, 'value': {'input': 'value'}}, cvar
        assert cvar in rules and (kind != 'choice' or not isinstance(extra[1][0], str) or rules[cvar] == list(extra[1])), cvar
        assert cvar in RETAINED_ONLY_GAME_SETTINGS or re.search(rf'(cvar\s+"?|consoleCMD\s+"){cvar}\b', stock_game), f'{cvar} is not a stock GAME OPTIONS setting'
    # The CONTROLS page: the button's and the multiplayer menu's routes as
    # GAME OPTIONS'; its rows are the stock page's bindings in its order, the
    # session's table follows them; a binding keeps at most two keys, as the
    # stock bindunbindtwo did, and the wait cancels on Escape, Start or Back.
    for command, multiplayer in (('openRetainedControls', 'false'), ('openRetainedControlsFromMp', 'true')):
        assert f'if ( !idStr::Icmp( cmd, "{command}" ) ) {{\n\t\t\tOpenRetainedControlsRoute( {multiplayer} );\n\t\t\treturn;' in main_menu
    assert 'guiMainMenu->SetStateBool( "retainedControls", RetainedControlsAvailable() );' in menu
    assert 'UI_RunLegacyWindowAction( guiMainMenu, "set_b_controls", false, command )' in function_body(menu, 'void idSessionLocal::OpenRetainedControlsRoute(')
    controls_button = stock_menu[stock_menu.index('windowDef set_b_controls\n'):]
    controls_action = controls_button[controls_button.index('onAction'):controls_button.index('windowDef', 1)]
    assert 'if ("gui::retainedControls" == 1) {' in controls_action and 'set "cmd" "openRetainedControls" ;' in controls_action
    controls_event = stock_menu[stock_menu.index('onNamedEvent fromMp_toControls {'):stock_menu.index('onNamedEvent fromMp_toControlsStock {')]
    assert 'resettime "anim_retainedControlsFromMp" "0" ;' in controls_event
    assert 'set "cmd" "openRetainedControlsFromMp" ;' in stock_menu[stock_menu.index('windowDef anim_retainedControlsFromMp'):]
    # With the retained CONTROLS page the settings screen arrives on no panel,
    # titled SETTINGS, its CONTROLS button showing and the stock rows' targets
    # hidden, so its buttons choose a page.
    arrival = stock_menu[stock_menu.index('windowDef anim_settingsIn'):]
    arrival = arrival[:arrival.index('windowDef', 1)]
    assert re.search(r'if \( "gui::retainedControls" == 1 \) \{\s*set "p_settings_ctrls::visible" "0" ;\s*'
                     r'set "t_settings_title::text" "#str_200009" ;', arrival)
    assert re.search(r'if \( "gui::retainedControls" == 1 \) \{\s*set "set_b_controls::visible" "1" ;\s*'
                     r'namedevent "p_btns::hideSetControls" ;', arrival)
    import retained_controls
    stock_controls = (ROOT / 'content/baseoq4/pak0/guis/menu/settings/controls.gui').read_text(encoding='utf-8', errors='replace')
    stock_bindings = [binding.strip('"') for binding in re.findall(r'bindDef\s+\w+\s*\{[^}]*?\bbind\s+("[^"]+"|\S+)', stock_controls)]
    session_bindings = re.findall(r'"([^"]+)"', re.search(r'RETAINED_CONTROLS_BINDINGS\[\] = \{([^}]*)\};', menu).group(1))
    assert list(retained_controls.BINDINGS) == stock_bindings == session_bindings and len(stock_bindings) == 49
    controls_document = json.loads(re.sub(r'^\s*//.*$', '', (ROOT / 'content/baseoq4/pak0/guis/menu/settings/controls.q4ui').read_text(encoding='utf-8'), flags=re.M))
    assert [controls_document['state'][f'controls.{row}.keys']['keyBinding'] for row in range(49)] == session_bindings
    controls_publish = function_body(menu, 'void idSessionLocal::PublishRetainedControls(')
    assert controls_publish.index('gui->SetKeyBindingNames();') < controls_publish.index('retainedControlsCapture >= 0') < controls_publish.index('gui->StateChanged(')
    assert {name: action['arguments']['command'] for name, action in controls_document['actions'].items()} == \
        {'controlsBind': 'controlsBind', 'controlsClear': 'controlsClear', 'controlsTouchLayout': 'controlsTouchLayout',
         'back': 'controlsBack'}
    # A new key joins its action's keys through the menus' shared rule, two
    # keys of each device family (idKeyInput::BindForMenu): a keyboard key
    # never clears the action's controller buttons. The stock page binds the
    # same way.
    controls_key = function_body(menu, 'bool idSessionLocal::RetainedControlsKey(')
    assert 'idKeyInput::BindForMenu( key, binding );' in controls_key and 'NumBinds' not in controls_key
    assert 'key != K_ESCAPE && key != K_JOY7 && key != K_JOY8' in controls_key and 'idKeyInput::UnbindBinding( binding );' in controls_key
    key_input = (ROOT / 'src/framework/KeyInput.cpp').read_text(encoding='utf-8')
    bind_for_menu = function_body(key_input, 'void idKeyInput::BindForMenu(')
    assert 'Key_IsControllerKey( i ) == controller && BindingsEquivalent( keys[i].binding.c_str(), binding )' in bind_for_menu
    assert 'if ( family >= 2 )' in bind_for_menu and bind_for_menu.rstrip(' }' + chr(10) + chr(13) + chr(9)).endswith('SetBinding( keynum, binding );')
    assert 'idKeyInput::BindForMenu( key, bind );' in main_menu, 'the stock CONTROLS page binds by the same rule'
    # Each row's keys reach the page a slot at a time, as glyphs.
    assert all(f'controls.{row}.keys.{slot}' in controls_document['state'] and f'controls.{row}.keys.{slot}.glyph' in controls_document['state']
               for row in range(49) for slot in ('key0', 'key1', 'pad0', 'pad1'))
    assert {'key-arrow-1', 'key-mouse-23', 'key-pad-1', 'key-pad-10', 'key-pad-12'} <= set(controls_document['symbols'])
    # The SETTINGS page: its sections are the session's table and the
    # document's plates, each with the stock panel that stands in for it; the
    # stock main menu's routes: openSettingsPage presents the screen on the
    # panel, its Back returns the page (settingsOpen) while
    # gui::settingsFromRetained holds, leaveSettingsPage and openSettingsHome
    # go home; LOAD DEFAULTS' YES runs the stock dialog's commands.
    import retained_settings_page
    session_sections = re.findall(r'\{ "(settings[A-Za-z]+)",\s*"([a-z]+)",\s*(\d+) \}',
                                  re.search(r'RETAINED_SETTINGS_SECTIONS\[\] = \{(.*?)\n\};', menu, re.S).group(1))
    page_sections = [(verb, ident) for ident, _key, verb in retained_settings_page.SECTIONS if verb]
    assert [(verb, ident) for verb, ident, _dest in session_sections] == page_sections
    assert {ident: int(dest) for _verb, ident, dest in session_sections} == {'controls': 4, 'game': 21, 'display': 22, 'audio': 36}
    settings_document = json.loads(re.sub(r'^\s*//.*$', '', (ROOT / 'content/baseoq4/pak0/guis/menu/settings.q4ui').read_text(encoding='utf-8'), flags=re.M))
    assert {name: action['arguments']['command'] for name, action in settings_document['actions'].items()
         if action['operation'] == 'session.menu' and not name.startswith('notice')} == \
        {**{verb: verb for verb, _ident in page_sections}, 'back': 'settingsBack', 'settingsDefaults': 'settingsDefaults'}
    for event in ('openSettingsPage', 'leaveSettingsPage', 'openSettingsHome'):
        assert f'onNamedEvent {event} {{' in stock_menu, event
    settings_out = stock_menu[stock_menu.index('windowDef anim_settingsOut'):]
    settings_out = settings_out[:settings_out.index('windowDef', 1)]
    assert re.search(r'if \( "gui::settingsFromRetained" == 1 \) \{\s*set "cmd" "settingsOpen" ;', settings_out)
    assert 'if ( !idStr::Icmp( cmd, "settingsOpen" ) ) {' in main_menu
    assert 'HandleMainMenuCommands( "exec cvar_restart ; play main_menu_selection ; resetdefaults ; exec s_restart" );' in \
        function_body(menu, 'void idSessionLocal::HandleRetainedSettingsPageRequest(')
    defaults_yes = re.search(r'windowDef pop_b_defaults_yes.*?onAction \{(.*?)\n\t\t\t\}', stock_menu, re.S).group(1)
    assert re.findall(r'set "cmd" "([^"]+)"', defaults_yes) == ['exec cvar_restart ; play main_menu_selection', 'resetdefaults', 'exec s_restart']
    assert function_body(menu, 'void idSessionLocal::MenuEvent(').index('RetainedControlsKey( event )') < \
        function_body(menu, 'void idSessionLocal::MenuEvent(').index('guiActive->HandleEvent( event')
    assert 'UI_RunLegacyWindowAction( guiMainMenu, "set_b_system", false, command )' in system_route, 'a failed SYSTEM page must open the stock one'
    assert 'guiMainMenu->HandleNamedEvent( "fromMp_toSystemStock" );' in system_route, 'a failed multiplayer SYSTEM route must open the stock page'
    home_document = function_body(menu, 'idUserInterface *idSessionLocal::RetainedHomeDocument(')
    assert 'UI_RetainedViewFailed( home )' in home_document and 'retainedStock.AddUnique( path );' in home_document
    assert 'retainedStock.Clear();' in function_body(session, 'void idSessionLocal::Clear(')
    # Back from a stock Multiplayer page asks the session, which presents the
    # retained page where it can, onto the section that opened that page,
    # and the stock one otherwise; the legacy menu supplies the three events
    # the page's routes run.
    assert ('if ( !idStr::Icmp( cmd, "multiplayerOpen" ) ) {\n\t\t\tconst int left = retainedMpPageLeft;\n'
            '\t\t\tretainedMpPageLeft = 0;\n'
            '\t\t\tconst char *section = left == 14 ? "join" : left == 17 ? "create" : left == 18 ? "setup" : NULL;\n'
            '\t\t\tif ( !OpenRetainedMultiplayerPage( section ) && guiMainMenu != NULL ) {\n'
            '\t\t\t\tguiMainMenu->HandleNamedEvent( "openMultiplayerStock" );') in main_menu
    legacy_menu = (ROOT / 'content/baseoq4/pak0/guis/mainmenu.gui').read_text(encoding='utf-8', errors='replace')
    assert legacy_menu.count('set "cmd" "multiplayerOpen" ;') == 4
    for leaving in ('anim_mpSetOut', 'anim_mpBrowseOut', 'anim_mpFriendsOut', 'anim_mpCreateOut'):
        start = legacy_menu.index(f'windowDef {leaving}\n')
        steps = legacy_menu[start:legacy_menu.index('windowDef ', start + len(leaving) + 10)]
        assert 'set "cmd" "multiplayerOpen" ;' in steps and 'anim_mpIn' not in steps, leaving
    for event in ('openMultiplayerStock', 'openMultiplayerPage', 'leaveMultiplayerPage'):
        assert legacy_menu.count(f'onNamedEvent {event} {{') == 1, event
    campaign_selector = function_body(menu, 'void idSessionLocal::OpenCampaignSelector(')
    assert 'Session_RetainedScreensEnabled() ?' in campaign_selector and 'arenaCampaign.OpenSelector();' in campaign_selector
    assert 'FindGui( STOCK_CAMPAIGN_GUI' in campaign_selector and 'gui->SetStateInt( "campaign_view", 0 )' in campaign_selector
    assert 'static const char *const STOCK_CAMPAIGN_GUI = "guis/campaign_menu.gui";' in menu
    # The stock campaign page stands in for the retained one: the same verbs,
    # one handler (Session_HandleCampaignPageRequest).
    select_campaign = function_body(menu, 'void idSessionLocal::SelectCampaign(')
    assert select_campaign.index('OpenRetainedCampaignSkill()') < select_campaign.index('page->SetStateInt( "campaign_view", 1 );')
    assert 'chapters ? OpenRetainedCampaignChapters() : OpenRetainedCampaignSkill()' in select_campaign
    assert 'PublishCampaignChapters( page );' in select_campaign
    # The CHAPTERS page publishes the levels; the difficulty page has none now.
    assert 'PublishCampaignChapters( gui );' in function_body(menu, 'bool idSessionLocal::OpenRetainedCampaignChapters(')
    assert 'PublishCampaignChapters' not in function_body(menu, 'bool idSessionLocal::OpenRetainedCampaignSkill(')
    assert 'Session_HandleCampaignPageRequest( *this, gui, request, true );' in menu
    assert 'Session_HandleCampaignPageRequest( *this, gui, command, false );' in menu
    # The pause screens are named once more by PrefetchRetainedLevel, which
    # parses them on workers as a map change begins; their loads stay in
    # PrepareRetainedLevel.
    assert menu.count('RETAINED_TITLE_GUI') == 4 and menu.count('RETAINED_PAUSE_GUI') == 4 and menu.count('RETAINED_LOADING_GUI') == 2
    assert menu.count('RETAINED_PAUSE_STROGG_GUI') == 4
    assert 'documents.Append( RETAINED_PAUSE_GUI );' in function_body(menu, 'void idSessionLocal::PrefetchRetainedLevel(')
    check_retained_multiplayer(menu, session)
    check_create_server(menu)
    check_join_game(menu)
    check_retained_match(menu)
    for signature in ('void idSessionLocal::PreloadRetainedScreens(', 'void idSessionLocal::PrepareRetainedLevel('):
        assert 'if ( !Session_RetainedScreensEnabled()' in function_body(menu, signature), f'{signature} must be gated'
    assert 'PreloadRetainedScreens();' in function_body(session, 'void idSessionLocal::Init(')
    # The loading phase line publishes only while the retained loading screen
    # presents the load, which only the gate can select.
    for signature in ('void idSessionLocal::SetRetainedLoadingPhase(', 'void idSessionLocal::PublishRetainedLoadingCount(',
                      'void idSessionLocal::PublishRetainedLoadingTip('):
        assert 'if ( !retainedLoadingActive || guiLoading == NULL' in function_body(session, signature), signature
    # Its continue prompt follows prompt.glyphs, which the adapter owns.
    assert 'loading_controller' not in session and 'PublishRetainedLoadingDevice' not in session
    loading = function_body(session, 'void idSessionLocal::LoadLoadingGui(')
    assert session.count('retainedLoadingActive = true;') == 1 and loading.index('if ( retainedLoading ) {') < loading.index('retainedLoadingActive = true;')
    # Tips: the first as the screen presents, then from the load's redraws;
    # every tip, in every shipped language, fits two lines.
    assert loading.index('retainedLoadingActive = true;') < loading.index('PublishRetainedLoadingTip( true );')
    assert 'PublishRetainedLoadingTip( false );' in function_body(session, 'void idSessionLocal::PacifierUpdate(')
    assert session.count('PublishRetainedLoadingTip( false );') == 1
    sys.path.insert(0, str(ROOT / 'tools' / 'ui'))
    import build_retained_screens as generator
    tips_table = session[session.index('static const char *const RETAINED_LOADING_TIPS[] = {'):]
    tips_table = tips_table[:tips_table.index('static const int RETAINED_LOADING_TIP_MSEC = 6000;') + len('static const int RETAINED_LOADING_TIP_MSEC = 6000;')]
    tip_keys = re.findall(r'"(#str_\d+)"', tips_table)
    assert len(tip_keys) == len(set(tip_keys)) >= 12, tip_keys
    for key in tip_keys + ['#str_230051']:
        texts = generator.localized(key)
        assert sorted(texts) == sorted(generator.LANGUAGES), (key, sorted(texts))
        if key != '#str_230051':
            for language, text in texts.items():
                assert len(generator.tip_lines(text)) <= 2, (key, language, generator.tip_lines(text))
    assert 'PrepareRetainedLevel( spawnMapPath, isMultiplayerLoad );' in function_body(session, 'void idSessionLocal::LoadLoadingGui(')
    update = function_body(menu, 'void idSessionLocal::UpdateRetainedHome(')
    assert 'const bool context = Session_RetainedScreensEnabled() &&' in update
    # The home is updated before the frame pump's no-GUI return, so closing
    # the menu (resume) retires the pause screen instead of leaving it drawn.
    frame = function_body(menu, 'void idSessionLocal::GuiFrameEvents(')
    assert frame.index('UpdateRetainedHome();') < frame.index('ClearMenuControllerRepeatState();\n\t\treturn;\n\t}\n\n\tif ( guiActive ) {'), \
        'GuiFrameEvents must update the retained home before returning without a GUI'
    assert 'if ( !Session_RetainedScreensEnabled() || legacy == NULL ) {' in function_body(menu, 'idUserInterface *idSessionLocal::SelectRetainedLoadingGui(')
    assert 'guiLoading = SelectRetainedLoadingGui( guiLoading, isMultiplayerLoad );' in function_body(session, 'void idSessionLocal::LoadLoadingGui(')
    assert 'if ( guiRetainedHome != NULL && guiActive == guiMainMenu ) {' in function_body(menu, 'void idSessionLocal::MenuEvent(')
    assert '"ui_retainedStatus", Session_RetainedStatus_f' in session
    # The Strogg pause: the session asks the game the command the game answers.
    game = (ROOT / 'src/game/Game_local.cpp').read_text(encoding='utf-8')
    assert 'game->HandleMainMenuCommands( "retainedPauseFamily", guiMainMenu );' in function_body(menu, 'bool idSessionLocal::RetainedPauseIsStrogg(')
    family = game[game.index('!idStr::Icmp( menuCommand, "retainedPauseFamily" )'):]
    family = family[:family.index('}')]
    assert 'gui->SetStateBool( "pause_strogg", player != NULL && player->spawnArgs.GetBool( "strogg" ) );' in family
    assert 'retainedPauseStrogg = -1;' in function_body(menu, 'void idSessionLocal::PrepareRetainedLevel(')
    # The Objectives page's screenshots resolve inside the load, after the game
    # spawns the map and before the media are finished.
    change = function_body(session, 'void idSessionLocal::ExecuteMapChange(')
    assert change.index('game->SpawnPlayer( i, false, NULL );') < change.index('PrecacheRetainedLevelImages();') < \
        change.index('renderSystem->EndLevelLoad();') < change.index('declManager->EndLevelLoad();')
    # A map change keeps an unchanged retained document's view. Inside the
    # load, after its purge, the document's pictures resolve again, and its
    # input, programs and session requests drop, as a new view's would.
    assert change.index('declManager->BeginLevelLoad();') < change.index('uiManager->BeginLevelLoad();') < \
        change.index('UI_ReloadForLevelLoad();') < change.index('PrimeMainMenuGuiResources();') < change.index('LoadLoadingGui( mapString );')
    assert 'uiManager->Reload(' not in change
    keep = function_body(adapter, 'bool idUserInterfaceRetained::KeepForLevelLoad(')
    assert keep.index('std::memcmp(data,source.data(),source.size()) == 0') < keep.index('impl->Quarantine(false,true,true);') < \
        keep.index('PrecacheImages(impl->document.Model().root);')
    images = game[game.index('!idStr::Icmp( menuCommand, "retainedLevelImages" )'):]
    assert 'ent->IsType( idObjective::GetClassType() )' in images[:images.index('gui->SetStateInt( "level_image_count", images );')]
    assert 'player->inventory.objectiveNames[ i ].screenshot' in images[:images.index('gui->SetStateInt( "level_image_count", images );')]
    draw = session[session.index('} else if ( mapSpawned ) {'):]
    draw = draw[:draw.index('} else {')]
    assert draw.index('gameDraw = game->Draw( GetLocalClientNum() );') < draw.index('renderSystem->WriteDemoPics();') < \
        draw.index('guiRetainedReleasing->Redraw( presentationTime );')
    assert session.count('guiRetainedReleasing->Redraw(') == 1
    release_ms = int(re.search(r'static const int RETAINED_RELEASE_MSEC = (\d+);', menu).group(1))
    for name in ('pause', 'pause_strogg'):
        text = (ROOT / f'content/baseoq4/pak0/guis/menu/{name}.q4ui').read_text(encoding='utf-8')
        document = json.loads(re.sub(r'^\s*//.*$', '', text, flags=re.M))
        release = next(item for item in document['timelines'] if item['id'] == 'release')
        assert release['durationMs'] == release_ms, name
        # Every activation after a release shows the screen again, whether
        # open or returnHome follows.
        reset = document['events']['onActivate'][0]
        assert reset['op'] == 'if' and reset['condition'] == {'state': 'pause.released'}, name
        assert {'op': 'setState', 'values': {'pause.released': False}} in reset['then'], name
    # The arsenal: the network system hands each spawned kind of item to the
    # retained loading screen, which shows as many as the stock row.
    network = (ROOT / 'src/framework/async/NetworkSystem.cpp').read_text(encoding='utf-8', errors='replace')
    icon_body = function_body(network, 'void idNetworkSystem::AddLoadingIcon(')
    assert icon_body.index('SetStateString( va( "load_icon_img_%d", numIcons ), icon );') < \
        icon_body.index('sessLocal.PublishRetainedLoadingIcon( numIcons, icon );')
    arsenal_count = int(re.search(r'static const int RETAINED_ARSENAL_ICONS = (\d+);', session).group(1))
    assert arsenal_count == 20 and 'if ( numIcons >= 20 ) {' in icon_body
    publish = function_body(session, 'void idSessionLocal::PublishRetainedLoadingIcon(')
    assert 'guiLoading->HandleNamedEvent( va( "arsenal%d", index ) );' in publish and '!retainedLoadingActive' in publish
    loading_text = (ROOT / 'content/baseoq4/pak0/guis/loading/loading.q4ui').read_text(encoding='utf-8')
    loading_doc = json.loads(re.sub(r'^\s*//.*$', '', loading_text, flags=re.M))
    fades = [item for item in loading_doc['timelines'] if item['id'].startswith('arsenal')]
    assert sorted(item['id'] for item in fades) == sorted(f'arsenal{index}' for index in range(1, arsenal_count + 1))
    assert all(item['durationMs'] == 150 for item in fades)
    # The sub-page change: the frame pump runs the hand-over, MenuEvent holds
    # input meanwhile, and the documents' halves last as long as the session
    # waits.
    pump = function_body(menu, 'void idSessionLocal::GuiFrameEvents(')
    assert pump.index('UpdateRetainedHome();') < pump.index('UpdateRetainedSubpage();') < pump.index('UpdateRetainedPage();') < \
        pump.index('if ( guiTest ) {')
    # The Load Game page draws over the home that handed off to it, and its
    # rows match the session's.
    assert 'DrawRetainedPageHome( presentationTime );' in function_body(session, 'void idSessionLocal::Draw(')
    load_rows = int(re.search(r'static const int RETAINED_LOAD_ROWS = (\d+);', menu).group(1))
    sys.path.insert(0, str(ROOT / 'tools' / 'ui'))
    import retained_saves
    assert load_rows == retained_saves.ROWS, (load_rows, retained_saves.ROWS)
    menu_event = function_body(menu, 'void idSessionLocal::MenuEvent(')
    assert menu_event.index('if ( RetainedSubpageEvent( event ) ) {') < menu_event.index('HandleEvent(')
    waits = {name: int(re.search(rf'static const int RETAINED_SUBPAGE_{name}_MSEC = (\d+);', menu).group(1)) for name in ('DEEPER', 'BACK')}
    # The SETTINGS page's sub-pages are the generator's DEEPER, DISPLAY's the
    # SYSTEM page.
    sys.path.insert(0, str(ROOT / 'tools' / 'ui'))
    import retained_settings_page
    settings_subpages = re.findall(r'\{ (RETAINED_SUBPAGE_SETTINGS_[A-Z]+),\s*"([a-z]+)",\s*(RETAINED_[A-Z]+_GUI),\s*"(subpageEnter[A-Za-z]+)" \}',
                                   re.search(r'RETAINED_SETTINGS_SUBPAGES\[\] = \{(.*?)\n\};', menu, re.S).group(1))
    assert {section: enter for _kind, section, _gui, enter in settings_subpages} == \
        {ident: timeline for ident, _row, _key, timeline in retained_settings_page.DEEPER} and len(settings_subpages) == 4
    assert ('RETAINED_SUBPAGE_SETTINGS_SYSTEM', 'display', 'RETAINED_SYSTEM_GUI', 'subpageEnterDisplay') in settings_subpages
    for name, event in (('singleplayer', 'subpageEnter'), ('campaigns', 'subpageLeave'), ('multiplayer', 'subpageEnterCreate'),
                        ('multiplayer', 'subpageEnterJoin'), ('multiplayer', 'subpageEnterDemos'), ('createserver', 'subpageLeave'),
                        ('joingame', 'subpageLeave'), ('demos', 'subpageLeave'),
                        *[('settings', enter) for *_rest, enter in settings_subpages],
                        *[(f'settings/{page}', 'subpageLeave') for page in ('controls', 'game', 'system', 'audio')]):
        text = (ROOT / f'content/baseoq4/pak0/guis/menu/{name}.q4ui').read_text(encoding='utf-8')
        document = json.loads(re.sub(r'^\s*//.*$', '', text, flags=re.M))
        timelines = {item['id']: item for item in document['timelines']}
        assert timelines[event]['durationMs'] == waits['DEEPER' if event.startswith('subpageEnter') else 'BACK'], name
        deactivate = document['events']['onDeactivate']
        if name == 'multiplayer':
            assert deactivate[0] == {'op': 'setState', 'values': {'setup.open': False}}, name
            deactivate = deactivate[1:]
        assert deactivate[0] == {'op': 'playTimeline', 'timeline': 'rest'}, name
        # Rest returns everything a change moves, so a hidden document never
        # presents moved (the crumb's fitted scale rests in crumbRest).
        moved = {(track['node'], track['property']) for item in timelines.values()
                 if item['id'].startswith(('subpage', 'crumbEnter', 'crumbReturn')) for track in item['tracks']}
        rested = {(track['node'], track['property']) for item in timelines.values()
                  if item['id'] == 'rest' or item['id'].startswith('crumbRest') for track in item['tracks']}
        assert moved <= rested, (name, sorted(moved - rested))
    reversals = sorted(int(item['id'][len('subpageReverse'):]) for item in json.loads(re.sub(r'^\s*//.*$', '',
        (ROOT / 'content/baseoq4/pak0/guis/menu/singleplayer.q4ui').read_text(encoding='utf-8'), flags=re.M))['timelines']
        if item['id'].startswith('subpageReverse'))
    assert reversals == [150, 200, 250, 300], reversals
    saving = function_body(session, 'bool idSessionLocal::SaveGame(')
    completed = re.search(r'operationGuard(?:\.|->)Complete\(\);', saving)
    assert completed is not None
    assert completed.start() < saving.index('retainedNewestSave = time( NULL );') < saving.index('return true;', completed.start())

    # Documents: current with their generator, verbs allowlisted and handled.
    generator = subprocess.run([sys.executable, str(ROOT / 'tools/ui/build_retained_screens.py'), '--check'], capture_output=True, text=True)
    assert generator.returncode == 0, generator.stderr
    allowlist = cpp_allowlist(adapter)
    value_allowlist = cpp_allowlist(adapter, 'bool SessionMenuValueCommand(')
    player_settings = player_setting_rules(adapter)
    handled = set(re.findall(r'\{ "([A-Za-z][A-Za-z0-9]*)",\s+"main_b_', menu)) | set(re.findall(r'!idStr::Icmp\(\s*request,\s*"([A-Za-z][A-Za-z0-9]*)"\s*\)', menu))
    assert 'for ( int i = 0; i < 5; ++i ) {\n            if ( !idStr::Icmp( request, va( "campaignSkill%d", i ) ) ) {' in menu.replace('\r\n', '\n')
    handled |= set(re.findall(r'!idStr::Icmpn\( request, \"(loadSelect|saveSelect|loadSave|loadDelete)\",', menu))
    handled.update(f'campaignSkill{i}' for i in range(5))
    # The Arena page's verbs, each mapped to the campaign's own command.
    arena_verbs = menu[menu.index('RETAINED_ARENA_VERBS[][2] = {'):menu.index('RETAINED_ARENA_VERB_COUNT')]
    handled |= set(re.findall(r'\{ "(arena[A-Za-z0-9]*)", "arena', arena_verbs))
    # The Create Server page's text verbs, each handing over its field's text.
    create_texts = menu[menu.index('RETAINED_CREATE_TEXTS[] = {'):menu.index('RETAINED_CREATE_TEXT_COUNT')]
    handled |= set(re.findall(r'\{ "(create[A-Za-z]*)",', create_texts))
    # The Join Game page's column headings, each asking for its sort.
    join_sorts = menu[menu.index('RETAINED_JOIN_SORTS[] = {'):menu.index('RETAINED_JOIN_SORT_COUNT')]
    handled |= set(re.findall(r'\{ "(join[A-Za-z]*)",', join_sorts))
    # The SETTINGS page's sections, each asking for its page.
    settings_sections = menu[menu.index('RETAINED_SETTINGS_SECTIONS[] = {'):menu.index('RETAINED_SETTINGS_SECTION_COUNT')]
    handled |= set(re.findall(r'\{ "(settings[A-Za-z]*)",', settings_sections))
    assert allowlist == handled, f'allowlist {sorted(allowlist)} differs from session handlers {sorted(handled)}'
    for relative in ('content/baseoq4/pak0/guis/menu/title.q4ui', 'content/baseoq4/pak0/guis/menu/pause.q4ui',
                     'content/baseoq4/pak0/guis/menu/pause_strogg.q4ui', 'content/baseoq4/pak0/guis/loading/loading.q4ui',
                     'content/baseoq4/pak0/guis/menu/singleplayer.q4ui', 'content/baseoq4/pak0/guis/menu/campaigns.q4ui',
                     'content/baseoq4/pak0/guis/menu/mp_escape.q4ui', 'content/baseoq4/pak0/guis/menu/campaign_skill.q4ui',
                     'content/baseoq4/pak0/guis/menu/chapters.q4ui',
                     'content/baseoq4/pak0/guis/menu/mp_summary.q4ui', 'content/baseoq4/pak0/guis/menu/loadgame.q4ui'):
        text = (ROOT / relative).read_text(encoding='utf-8')
        document = json.loads(re.sub(r'^\s*//.*$', '', text, flags=re.M))
        # The 4:3 canvas shrinks to a narrower view's width: every menu fits
        # narrow views (fit_page, fit_narrow, its page mode); a card fits its
        # own width and side margins (fit_card).
        canvas = document.get('canvas')
        if 'mp_escape' in relative or 'mp_summary' in relative:
            assert canvas.get('height') == 720 and 900 <= canvas.get('minWidth', 0) <= 1000, relative
        else:
            assert canvas == {'height': 720, 'minWidth': 960}, relative
        if 'loadgame' in relative:
            # A page's prompt bar keeps the view's bottom edge (fit_narrow's
            # page mode); its canvas group runs the view's full height.
            def find_node(node, wanted):
                if node.get('id') == wanted:
                    return node
                return next((found for child in node.get('children', []) if (found := find_node(child, wanted))), None)
            bar = find_node(document['root'], 'prompts')['properties']
            assert 'bottom' in bar and 'top' not in bar, relative
            assert find_node(document['root'], 'load-panel')['properties']['height'] == {'type': 'length', 'value': 100, 'unit': '%'}, relative
        if 'pause' in relative or 'mp_escape' in relative or 'mp_summary' in relative:
            # The pause carries the session's notice slots over everything.
            notices = ['notice-0', 'notice-1', 'notice-2'] if relative.endswith('/pause.q4ui') else []
            assert [child['id'] for child in document['root']['children']] == ['scene-softfocus', 'scrim', 'chrome', *notices], relative
        if 'pause' in relative:
            layers = [child['id'] for child in document['root']['children'][2]['children']]
            assert layers.index('objectives-bar') < layers.index('band-top') < layers.index('objectives'), relative
        for action in document.get('actions', {}).values():
            if action['operation'] == 'settings.player.set':
                # Only the Voice page's tab-exit stop may write a literal: it
                # switches the local microphone test off, never on.
                stop_test = action['arguments']['cvar'] == 's_voiceChatTest' and action['arguments']['value'] is False
                assert action['arguments']['cvar'] in player_settings and (
                    action['arguments']['value'] == {'input': 'value'} or stop_test), relative
            elif action['operation'] == 'session.menuValue':
                assert action['arguments']['command'] in value_allowlist and action['arguments']['value'] == {'input': 'value'}, relative
            elif action['operation'] == 'ui.state.set':
                # A text field's Enter writes its text into the document's own
                # state and runs an event, which asks the session.
                assert action['input'] == 'string' and action['arguments']['value'] == {'input': 'value'} and                     action['arguments']['event'] in document['events'], relative
            elif action['operation'] == 'ui.numberDrafts.focus':
                assert action['arguments'] == {}, relative
            else:
                assert action['operation'] == 'session.menu' and action['arguments']['command'] in allowlist, relative
    # Only the scoped, preference-gated optical overlay adapter in game code
    # may choose retained documents. Legacy content never names one.
    for folder in (ROOT / 'content', ROOT / 'src/game', ROOT / 'src/mpgame'):
        if not folder.exists():
            continue
        for path in folder.rglob('*'):
            if path == ROOT / 'src/game/gamesys/RetainedScope.h':
                continue
            if path.suffix.lower() in {'.gui', '.def', '.cfg', '.script', '.mtr', '.cpp', '.h'} and path.is_file():
                assert '.q4ui' not in path.read_text(encoding='utf-8', errors='replace'), f'{path} names a retained document'

    # Behaviour: the production session code against counted doubles.
    start = menu.index('static const char *const RETAINED_TITLE_GUI')
    tables = menu[start:menu.index('// Legacy pop-ups drawn over the home state', start)]
    gate_start = menu.index('static bool Session_RetainedScreensEnabled( void ) {')
    gate_functions = menu[gate_start:menu.index('static const int MENU_CONTROLLER_AXIS_THRESHOLD', gate_start)]
    assert 'RETAINED_SYSTEM_GUI' in gate_functions
    page_start = menu.index('// The stock campaign pages: the campaigns (campaign_view 0)')
    campaign_page = menu[page_start:menu.index('#endif', menu.index('static void Session_HandleCampaignPageRequest(', page_start))]
    bodies = [gate_functions, tables, campaign_page] + [function_body(menu, signature) for signature in (
        'static bool Session_RetainedHomePopup(', 'static const char *Session_RetainedHomeMissingPage(',
        'static bool Session_RetainedMultiplayerRoutes(',
        'static void Session_RetainedSaveLabel(', 'static idStr Session_RetainedSaveShot(', 'static void Session_RetainedSaveStamp(',
        'static void Session_RetainedSaveDetails(', 'static void Session_PublishRetainedTitleState(',
        'static void Session_PublishSummaryShot(', 'static void Session_PublishSinglePlayerSummaries(',
        'static void Session_PublishMultiplayerSummaries(', 'static void Session_PublishCampaignSummaries(', 'idUserInterface *idSessionLocal::FindRetainedGui(',
        'bool idSessionLocal::RetainedSystemAvailable(', 'bool idSessionLocal::ReturnSystemSettings(', 'void idSessionLocal::OpenCampaignSelector(',
        'bool idSessionLocal::OpenRetainedCampaignSkill(', 'bool idSessionLocal::OpenRetainedCampaignChapters(', 'void idSessionLocal::BeginRetainedCampaignSkill(',
        'bool idSessionLocal::RetainedHomeInputBlocked(',
        'void idSessionLocal::UpdateRetainedHome(', 'idUserInterface *idSessionLocal::RetainedHomeDocument(',
        'bool idSessionLocal::RetainedPauseIsStrogg(', 'void idSessionLocal::RetainedHomeFrameEvent(',
        'static bool Session_CampaignNeedsReload(',
        'static const char *Session_LoadFailureText(', 'void idSessionLocal::HandleRetainedSessionRequest(', 'static bool Session_ModSuppliesFile(',
        'void idSessionLocal::BeginRetainedSubpage(', 'void idSessionLocal::UpdateRetainedSubpage(',
        'void idSessionLocal::PresentRetainedSubpage(',
        'bool idSessionLocal::RetainedSubpageEvent(',
        'bool idSessionLocal::BeginRetainedPage(', 'void idSessionLocal::UpdateRetainedPage(', 'void idSessionLocal::ReturnRetainedPage(',
        'void idSessionLocal::ReturnRetainedSinglePlayerHome(',
        'static bool Session_RetainedSaveOperand(', 'void idSessionLocal::BuildRetainedSaveList(',
        'bool idSessionLocal::RefreshRetainedSaveList(', 'void idSessionLocal::PublishRetainedLoadPage(',
        'void idSessionLocal::PublishRetainedLoadRows(', 'void idSessionLocal::PublishRetainedLoadSave(',
        'void idSessionLocal::HandleRetainedLoadRequest(', 'bool idSessionLocal::RetainedPageActive(',
        'void idSessionLocal::PublishRetainedSavePage(', 'void idSessionLocal::PublishRetainedSaveSelection(',
        'bool idSessionLocal::RetainedSaveGame(', 'void idSessionLocal::HandleRetainedSaveRequest(',
        'void idSessionLocal::HandleRetainedRestartRequest(', 'idUserInterface *idSessionLocal::RetainedDemosPage(',
        'void idSessionLocal::OpenRetainedDemos(', 'void idSessionLocal::PublishRetainedDemosPage(',
        'void idSessionLocal::PublishRetainedDemoRows(', 'void idSessionLocal::PublishRetainedDemoSelection(',
        'void idSessionLocal::HandleRetainedDemosRequest(', 'void idSessionLocal::PublishRetainedMods(',
        'void idSessionLocal::SelectRetainedMod(', 'void idSessionLocal::OpenRestartMenu(',
        'void idSessionLocal::PublishRetainedCredits(', 'idUserInterface *idSessionLocal::RetainedArenaPage(',
        'void idSessionLocal::HandleRetainedArenaRequest(', 'idUserInterface *idSessionLocal::RetainedMultiplayerPage(',
        'bool idSessionLocal::OpenRetainedMultiplayerPage(', 'void idSessionLocal::HandleRetainedMultiplayerPageRequest(',
        'void idSessionLocal::UpdateRetainedMultiplayerPage(', 'static int Session_RetainedCreateGameType(',
        'static bool Session_RetainedCreateSettingRequest(', 'static idStr Session_RetainedCreateText(',
        'idUserInterface *idSessionLocal::RetainedCreateServerPage(', 'void idSessionLocal::OpenRetainedCreateServer(',
        'void idSessionLocal::PublishRetainedCreateServer(', 'void idSessionLocal::PublishRetainedCreateMaps(',
        'static bool Session_BanListRow(', 'void idSessionLocal::PublishRetainedCreateBans(',
        'void idSessionLocal::HandleRetainedCreateServerRequest(', 'static void Session_ScanServers(',
        'idUserInterface *idSessionLocal::RetainedJoinGamePage(', 'void idSessionLocal::OpenRetainedJoinGame(',
        'static int Session_NetworkQualityBars(',
        'void idSessionLocal::PublishRetainedJoinGame(', 'void idSessionLocal::PublishRetainedJoinDetail(',
        'void idSessionLocal::PushRetainedNotice(', 'void idSessionLocal::ResolveRetainedNotices(',
        'void idSessionLocal::UpdateRetainedNotices(', 'void idSessionLocal::HandleRetainedNoticeAction(',
        'void idSessionLocal::UpdateRetainedJoinGame(',
        'void idSessionLocal::HandleRetainedJoinGameRequest(',
        'static void Session_ChoiceListItems(', 'bool idSessionLocal::RetainedAudioAvailable(', 'bool idSessionLocal::OpenRetainedAudio(',
        'void idSessionLocal::OpenRetainedAudioRoute(', 'void idSessionLocal::ReturnRetainedAudio(',
        'void idSessionLocal::PublishRetainedAudio(', 'void idSessionLocal::HandleRetainedAudioRequest(',
        'bool idSessionLocal::RetainedGameAvailable(', 'bool idSessionLocal::OpenRetainedGame(',
        'void idSessionLocal::OpenRetainedGameRoute(', 'void idSessionLocal::ReturnRetainedGame(',
        'bool idSessionLocal::RetainedSettingsParent(', 'idUserInterface *idSessionLocal::RetainedSettingsPage(',
        'bool idSessionLocal::OpenRetainedSettingsPage(', 'void idSessionLocal::HandleRetainedSettingsPageRequest(',
        'void idSessionLocal::UpdateRetainedSettingsPage(',
        'bool idSessionLocal::RetainedControlsAvailable(', 'bool idSessionLocal::OpenRetainedControls(',
        'void idSessionLocal::OpenRetainedControlsRoute(', 'void idSessionLocal::ReturnRetainedControls(',
        'idUserInterface *idSessionLocal::SelectRetainedLoadingGui(',
        'void idSessionLocal::PreloadRetainedScreens(', 'void idSessionLocal::PrepareRetainedLevel(',
        'void idSessionLocal::PrecacheRetainedLevelshots(', 'void idSessionLocal::QueueRetainedLevelshot(',
        'static bool Session_IsGameMenu(', 'static bool Session_IsGameSummary(', 'static bool Session_IsArenaPause(',
        'static bool Session_IsCoveredGameGui(',
        'bool idSessionLocal::RetainedMultiplayerCovers(', 'bool idSessionLocal::RetainedSummaryCovers(', 'void idSessionLocal::UpdateRetainedMultiplayer(',
        'void idSessionLocal::RetireRetainedMultiplayer(', 'void idSessionLocal::RetainedMultiplayerFrameEvent(',
        'void idSessionLocal::HandleGameMenuReturn(', 'void idSessionLocal::HandleRetainedMultiplayerRequest(',
        'void idSessionLocal::ReportRetainedScreens(')]
    bodies.append(tips_table)
    bodies += [function_body(session, signature) for signature in (
        'static bool Session_ImageInstalled(', 'idStr idSessionLocal::RetainedPauseShot(',
        'void idSessionLocal::PrecacheRetainedLevelImages(', 'static idStr Session_RetainedSaveAge(', 'idStr Session_RetainedPlayTime(',
        'idStr Session_RetainedList(',
        'void idSessionLocal::PublishRetainedLoadingTip(',
        'void idSessionLocal::PublishRetainedPauseState(')]
    compiler = next((found for name in ('clang++', 'g++', 'c++') if (found := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    (ROOT / '.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='ui-retained-gate-', dir=ROOT / '.tmp') as directory:
        source = Path(directory) / 'gate.cpp'
        binary = Path(directory) / 'gate.exe'
        source.write_text(SUPPORT + '\n'.join(bodies) + MAIN, encoding='utf-8')
        subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Wno-unused-function', '-I', str(ROOT), str(source), '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
        sys.stdout.write(result.stdout)
        sys.stderr.write(result.stderr)
        if result.returncode == 0:
            print(f'ui_retained gate: the Strogg faces resolve and {check_strogg_faces(compiler, Path(directory))} code points fold onto the rune face')
            check_server_card(compiler, Path(directory))
            check_loading_hold(compiler, Path(directory))
            check_prompt_glyphs(compiler, Path(directory))
        return result.returncode


if __name__ == '__main__':
    raise SystemExit(main())
