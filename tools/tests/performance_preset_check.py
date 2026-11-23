#!/usr/bin/env python3
"""The system check's contract (docs/dev/ui/performance-presets.md "System check").

Checks that the preset system version moves with the presets, that both
screens (the retained document and the stock GUI) read what the session
publishes and answer with verbs it handles, that the startup path shows the
check only in place of the default main menu, and that every string exists in
every shipped language with the English format arguments.

With a C++ compiler (clang++ or g++) it also builds the pure preset model
three times: as shipped, with one preset value changed and with one detection
rule changed, and requires a different version from each change.
"""
from __future__ import annotations

import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STRINGS = ROOT / "content/baseoq4/pak0/strings"
LANGUAGES = ("english", "french", "german", "italian", "spanish", "polish", "russian", "brazilian", "czech",
             "hungarian", "turkish", "ukrainian")
KEYS = [f"#str_{key}" for key in range(232000, 232028)]
VERBS = ("presetCheckApply", "presetCheckKeep", "presetCheckLater")

checks = 0


def check(condition: bool, message: str) -> None:
    global checks
    checks += 1
    if not condition:
        raise SystemExit(f"FAIL: {message}")


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def table(language: str) -> dict:
    found = {}
    for line in (STRINGS / f"{language}_openq4.lang").read_text(encoding="utf-8").splitlines():
        match = re.match(r'\s*"(#str_\d+)"\s+"(.*)"\s*$', line)
        if match:
            found[match.group(1)] = match.group(2)
    return found


def formats(text: str) -> list:
    return re.findall(r"%[-+ 0#]*\d*(?:\.\d+)?[a-zA-Z%]", text)


def strings() -> None:
    english = table("english")
    for language in LANGUAGES:
        found = table(language)
        for key in KEYS:
            check(key in found and found[key].strip() != "", f"{language} has {key}")
            check(formats(found[key]) == formats(english[key]), f"{language} {key} takes English's format arguments")


def gui_keys(text: str) -> set:
    return set(re.findall(r'"gui::(check_\w+)"', text))


def screens() -> None:
    session = read("src/framework/Session_presetcheck.cpp")
    published = set(re.findall(r'SetState(?:String|Int|Bool)\( "(check_\w+)"', session))
    check({"check_body", "check_primary", "check_busy", "check_recommendedTier", "check_currentTier"} <= published,
          "the session publishes the question, the primary label, the busy flag and both ladder cells")
    stock = read("content/baseoq4/pak0/guis/presetcheck.gui")
    check(gui_keys(stock) <= published, f"the stock screen reads only published state: {sorted(gui_keys(stock) - published)}")
    for verb in VERBS:
        check(f'; {verb}" ;' in stock, f"the stock screen sends {verb}")
        check(f'!idStr::Icmp( cmd, "{verb}" )' in session, f"the session takes the stock {verb}")
        check(f'!idStr::Icmp( request, "{verb}" )' in session, f"the session takes the retained {verb}")
    check('presetCheckLater" ;' in function(stock, "onESC {"), "Escape on the stock screen asks again next time")
    for key in set(re.findall(r'"(#str_\d+)"', stock)):
        check(key in table("english"), f"the stock screen's {key} exists")
    document = json.loads(re.sub(r"^\s*//.*$", "", read("content/baseoq4/pak0/guis/menu/presetcheck.rui"), flags=re.M))
    state = {name for name in document["state"] if name.startswith("check_")}
    check(state == published, f"the retained screen's state is what the session publishes: {sorted(state ^ published)}")
    actions = {action["arguments"]["command"] for action in document["actions"].values() if action["operation"] == "session.menu"}
    check(actions == set(VERBS), f"the retained screen answers with the three verbs: {sorted(actions)}")
    allowed = function(read("src/ui/UserInterfaceRetained.cpp"), "bool SessionMenuCommand(const std::string& command) {")
    for verb in VERBS:
        check(f'"{verb}"' in allowed, f"the retained runtime lets {verb} reach the session")
    check(document["events"]["onActivate"] == [{"op": "call", "event": "checkShow"}], "activation opens the dialog")
    check(document["events"]["onBack"] == [{"op": "call", "event": "checkPickLater"}], "Back asks again next time")
    # The document is generated: it must match its source.
    sys.path.insert(0, str(ROOT / "tools/ui"))
    import build_retained_screens as generator
    from retained_preset_check import preset_check_document
    built = preset_check_document()
    generator.safe_frame(built)
    generator.check_text_bindings(built)
    current = read("content/baseoq4/pak0/guis/menu/presetcheck.rui").replace("\r\n", "\n")
    check(generator.render(built).replace("\r\n", "\n") == current, "guis/menu/presetcheck.rui matches tools/ui/retained_preset_check.py")
    check(generator.OUTPUTS["presetcheck"] == ROOT / "content/baseoq4/pak0/guis/menu/presetcheck.rui",
          "build_retained_screens.py writes and checks the document")


def function(source: str, signature: str) -> str:
    start = source.index(signature)
    depth, index = 0, source.index("{", start)
    for index in range(index, len(source)):
        depth += {"{": 1, "}": -1}.get(source[index], 0)
        if depth == 0:
            return source[start:index + 1]
    raise ValueError(signature)


def session_paths() -> None:
    common = read("src/framework/Common.cpp")
    session = read("src/framework/Session_presetcheck.cpp")
    menu = read("src/framework/Session_menu.cpp")
    check('idCVar com_performancePresetCheck( "com_performancePresetCheck", "", CVAR_ARCHIVE' in common,
          "the version record is an archived setting that starts empty")
    init = common[common.index("void idCommonLocal::Init( int argc"):]
    startup = re.search(r"if \( !AddStartupCommands\(\) \) \{(.*?)\n\t\t\}\n", init, re.S)
    check(startup is not None and "if ( !Session_StartPresetCheck() ) {" in startup.group(1)
          and startup.group(1).index("Session_StartPresetCheck") < startup.group(1).index("session->StartMenu( true );"),
          "the check stands in front of the default main menu only, never a startup command's map")
    game = function(common, "void idCommonLocal::InitGame( void ) {")
    first = game.index("Common_ApplyStartupPerformancePreset( startupPresetBootstrap, startupPresetAssigned )")
    check(game.index('Common_RecordPerformancePresetCheck( "first launch" );') > first,
          "a first launch's detection records the version, so a new configuration is not asked")
    due = function(common, "bool Common_PerformancePresetCheckDue() {")
    for guard in ("net_serverDedicated", "UI_SettingsStartupActive()", "UI_SettingsRecoveryPending()", "PerformancePresetSystemVersion()"):
        check(guard in due, f"the check is due only when {guard} allows")
    apply = function(common, "bool Common_ApplyPerformancePresetCheck( bool &changed, std::string &error ) {")
    check(apply.index("Common_ApplyPerformancePreset( resolved, true )") < apply.index("Common_RecordPerformancePresetCheck("),
          "the version is recorded only after the preset applied")
    check('cmdSystem->AddCommand( "performancePresetCheck", Session_PresetCheck_f' in common, "the console command is registered")
    request = function(menu, "void idSessionLocal::HandleRetainedSessionRequest( idUserInterface *gui, const char *request ) {")
    check(request.index("Session_PresetCheckOwns( gui )") < request.index("guiMsgRetained"),
          "the retained check's verbs reach the session before any other screen's")
    start = function(session, "bool Session_StartPresetCheck( bool force ) {")
    check('cvarSystem->GetCVarBool( "ui_retained" )' in start and "sessLocal.FindRetainedGui( RETAINED_PRESET_CHECK_GUI, true, false )" in start,
          "the retained screen presents behind ui_retained and falls back through FindRetainedGui")
    check("uiManager->FindGui( STOCK_PRESET_CHECK_GUI" in start, "the stock screen stands in for a retained one that cannot load")
    check(not re.search(r'uiManager->FindGui\(\s*(?:RETAINED_|"[^"]+\.rui")', session), "retained documents load only through FindRetainedGui")
    check("sessLocal.mapSpawned" in start, "the check never covers a running map")
    answer = function(session, "void Answer( const char *answer ) {")
    check('CMD_EXEC_INSERT, "vid_restart\\ns_restart\\nperformancePresetCheck finish\\n"' in answer and "changed" in answer,
          "an apply that moved a setting restarts the renderer and sound before the main menu, ahead of queued commands")


def compile_version(compiler: str, out: Path, name: str, source: str) -> str:
    folder = out / name
    (folder / "src/framework").mkdir(parents=True)
    (folder / "src/framework/PerformancePreset.cpp").write_text(source, encoding="utf-8")
    shutil.copy(ROOT / "src/framework/PerformancePreset.h", folder / "src/framework/PerformancePreset.h")
    (folder / "src/renderer").mkdir(parents=True)
    shutil.copy(ROOT / "src/renderer/RendererHardware.h", folder / "src/renderer/RendererHardware.h")
    main = folder / "main.cpp"
    main.write_text('#include "src/framework/PerformancePreset.h"\n#include <cstdio>\n'
                    'int main(){std::printf("%s\\n",openq4::PerformancePresetSystemVersion().c_str());}\n', encoding="utf-8")
    binary = folder / "version.exe"
    subprocess.run([compiler, "-std=c++20", "-O1", "-I", str(folder), str(main), str(folder / "src/framework/PerformancePreset.cpp"),
                    "-o", str(binary)], check=True, capture_output=True, text=True)
    return subprocess.run([str(binary)], check=True, capture_output=True, text=True).stdout.strip()


def versions() -> None:
    compiler = shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        print("skipped the version mutations: no C++ compiler")
        return
    source = read("src/framework/PerformancePreset.cpp")
    preset = '{ "minimum", 0, "low",\n\t\t0, 0, 30,'
    rule = "tier=vram<=2048?Performance:Balanced;"
    check(source.replace("\r\n", "\n").count(preset) == 1 and source.count(rule) == 1, "the mutation anchors exist")
    out = Path(tempfile.mkdtemp(prefix="preset-check-", dir=ROOT / ".tmp"))
    try:
        shipped = compile_version(compiler, out, "shipped", source)
        normal = source.replace("\r\n", "\n")
        changed_preset = compile_version(compiler, out, "preset", normal.replace(preset, preset.replace("0, 0, 30,", "0, 0, 31,")))
        changed_rule = compile_version(compiler, out, "rule", normal.replace(rule, rule.replace("2048", "4096")))
        pinned = re.search(r'version=="(\d+\.[0-9a-f]{8})"', read("tools/tests/native/UiPerformancePresetTest.cpp"))
        check(pinned is not None and pinned.group(1) == shipped, f"the native test pins the shipped version {shipped}")
        check(changed_preset != shipped and changed_preset.split(".")[0] == shipped.split(".")[0],
              "a changed preset value changes the version by itself")
        check(changed_rule != shipped, "a changed detection rule changes the version by itself")
        print(f"preset system version {shipped}; a preset change gives {changed_preset}, a detection change {changed_rule}")
    finally:
        shutil.rmtree(out, ignore_errors=True)


def main() -> int:
    strings()
    screens()
    session_paths()
    versions()
    print(f"performance preset check: {checks} checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
