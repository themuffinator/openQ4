#!/usr/bin/env python3
"""Pin the campaign chapters and the Single Player menu levels.

Every level of a campaign is a chapter in campaign order; a level start
records it as reached, a start autosave on disk counts too, and starting a
chapter loads that autosave or starts the level fresh (docs/user/campaigns.md).
The retained menus reach them from Campaign's CHAPTERS, a fourth Single
Player level of its own (chapters.q4ui) beside NEW GAME's difficulty page;
the stock campaign_menu.gui keeps them on its campaign page.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]

# The levels' nextMap order in the shipped campaigns. Quake 4 plays storage1
# and process1 twice, once for each entity filter.
QUAKE4 = [
    ("game/airdefense1", ""), ("game/airdefense2", ""), ("game/hangar1", ""), ("game/hangar2", ""),
    ("game/mcc_landing", ""), ("game/mcc_1", ""), ("game/convoy1", ""), ("game/building_b", ""),
    ("game/convoy2", ""), ("game/convoy2b", ""), ("game/hub1", ""), ("game/hub2", ""),
    ("game/medlabs", ""), ("game/walker", ""), ("game/dispersal", ""), ("game/recomp", ""),
    ("game/putra", ""), ("game/waste", ""), ("game/mcc_2", ""), ("game/storage1", "first"),
    ("game/storage2", ""), ("game/storage1", "second"), ("game/tram1", ""), ("game/tram1b", ""),
    ("game/process1", "first"), ("game/process2", ""), ("game/process1", "second"), ("game/network1", ""),
    ("game/network2", ""), ("game/core1", ""), ("game/core2", ""),
]
AWAKENING = [
    ("game/m01_stranarus_trench1", ""), ("game/m01_stranarus_trench2", ""), ("game/m02_trianfac", ""),
    ("game/m03_airassault", ""), ("game/m04_prison", ""), ("game/m05_bio", ""), ("game/m06_mcc", ""),
    ("game/m06_mcc_invasion", ""), ("game/m07_race", ""), ("game/m07_race1", ""), ("game/m07_race2", ""),
    ("game/m08_cryofac", ""), ("game/m09_valkaryne", ""),
]
STRINGS = [f"#str_{key}" for key in (*range(231280, 231290), 231551)]
LANGUAGES = ("english", "french", "german", "italian", "spanish", "brazilian", "polish", "russian", "ukrainian",
             "czech", "hungarian", "turkish")


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")


def require(text: str, needle: str, what: str) -> None:
    if needle not in text:
        raise AssertionError(f"{what}: missing {needle!r}")


def table(source: str, name: str) -> list[tuple[str, str]]:
    start = source.index(f"static const sessionChapter_t {name}[] = {{")
    body = source[start:source.index("};", start)]
    return re.findall(r'\{ "([^"]+)", "([^"]*)" \}', body)


def main() -> int:
    session = read("src/framework/Session.cpp")
    menu = read("src/framework/Session_menu.cpp")

    # The chapters, in campaign order.
    if table(session, "SESSION_QUAKE4_CHAPTERS") != QUAKE4:
        raise AssertionError("the Quake 4 chapters must follow the campaign's nextMap order")
    if table(session, "SESSION_AWAKENING_CHAPTERS") != AWAKENING:
        raise AssertionError("The Awakening's chapters must follow the campaign's nextMap order")
    for cvar in ("ui_chaptersQuake4", "ui_chaptersAwakening"):
        require(session, f'static idCVar {cvar}( "{cvar}", "1", CVAR_GUI | CVAR_ARCHIVE | CVAR_INTEGER | CVAR_NOCHEAT,',
                "each campaign keeps its chapters in its own archived configuration")

    # Reaching a level records it; devmap and multiplayer do not.
    move = function_body(session, "void idSessionLocal::MoveToNewMap(")
    if not move.index("SaveGame( NULL, ST_AUTO );") < move.index("RecordCampaignChapter();") < move.index("SetGUI( NULL, NULL );"):
        raise AssertionError("a level start records its chapter after its start autosave")
    record = function_body(session, "void idSessionLocal::RecordCampaignChapter(")
    require(record, 'if ( IsMultiplayer() || mapSpawnData.serverInfo.GetBool( "devmap" ) ) {', "devmap and multiplayer starts")
    # A start autosave on disk counts, and the unlock outlives it.
    reached = function_body(session, "static int Session_CampaignChaptersReached(")
    require(reached, "reached |= 1 << i;", "a start autosave unlocks its chapter")
    require(reached, "mask.SetInteger( reached );", "the autosave's unlock is recorded")
    require(reached, "if ( !slots[j].Icmp( slot ) ) {", "saves match without case")

    # A chapter starts from its autosave, else fresh with its devmap arsenal,
    # then writes that autosave and plays on normally.
    start = function_body(session, "bool idSessionLocal::StartCampaignChapter(")
    if not start.index("LoadGame( saves[chapter].c_str() )") < start.index(
            "StartNewGame( chapters[chapter].map, chapter > 0, chapters[chapter].entityFilter );"):
        raise AssertionError("a chapter tries its autosave before a fresh start")
    require(start, '!( reached & ( 1 << chapter ) )', "a locked chapter is refused")
    fresh = start[start.index("StartNewGame( chapters[chapter].map"):]
    if not fresh.index('mapSpawnData.serverInfo.Delete( "devmap" );') < fresh.index("SaveGame( NULL, ST_AUTO );") < fresh.index(
            "RecordCampaignChapter();"):
        raise AssertionError("a fresh chapter drops devmap, then writes its autosave and is reached")
    for command in ('"campaignChapters", Session_CampaignChapters_f', '"campaignChapter", Session_CampaignChapter_f'):
        require(session, command, "the console lists and starts chapters")

    # Both campaign pages share one set of verbs.
    handler = function_body(menu, "static void Session_HandleCampaignPageRequest(")
    for verb in ("campaignSkillBack", "campaignSkillStart", "campaignChapterStart", "campaignChapterSelect", "campaignSkillTurbo",
                 "campaignChaptersBack"):
        require(handler, f'!idStr::Icmp( request, "{verb}" )', "the campaign page's verbs")
    require(handler, 'retained ? "chapter.selected" : "chapters_sel_0"', "both pages name the chosen chapter")
    stock = function_body(menu, "bool idSessionLocal::HandleStockCampaignCommand(")
    require(stock, "Session_HandleCampaignPageRequest( *this, gui, command, false );", "the stock page's commands")
    require(menu, "Session_HandleCampaignPageRequest( *this, gui, request, true );", "the retained page's requests")
    allowlist = read("src/ui/UserInterfaceRetained.cpp")
    require(allowlist, '"campaignChapterSelect","campaignChapterStart"', "the retained page may request the chapters' verbs")
    require(allowlist, '"campaignNew","campaignChapters"', "Campaign may open NEW GAME and CHAPTERS")
    require(allowlist, '"campaignChaptersBack"', "CHAPTERS may climb back")

    # The stock pages: CAMPAIGN and the campaign page with its chapters.
    # A constant condition never holds in a GUI script, so none guards a
    # button.
    gui = read("content/baseoq4/pak0/guis/campaign_menu.gui")
    for window in ("cm_quake4_card", "cm_awakening_card", "cm_skill0", "cm_skill4", "cm_start", "cm_turbo", "cm_chapters",
                   "cm_chapter_start", "cm_campaigns_back", "cm_campaign_back", "cm_chapter_back", "cm_blocker"):
        require(gui, f"windowDef {window}\n", "the stock campaign pages")
    require(gui, "listDef cm_chapter_list\n", "the stock chapter list")
    require(gui, "listname\tchapters", "the stock chapter list's state")
    if re.search(r"if \( 1 \)", gui):
        raise AssertionError("a constant condition never holds in a GUI script")
    for command in ("campaignQuake4", "campaignAwakening", "campaignBack", "campaignSkillBack", "campaignSkillStart",
                    "campaignSkillTurbo", "campaignChapterSelect", "campaignChapterStart"):
        require(gui, command, "the stock pages' commands")
    chapter_start = gui[gui.index("windowDef cm_chapter_start\n"):gui.index("windowDef cm_chapter_back\n")]
    require(chapter_start, 'if ( "gui::chapter_open" == 1 ) {', "a locked chapter's START stays dark")

    # The retained pages: Campaign's NEW GAME and CHAPTERS, and CHAPTERS'
    # own page with a row for every chapter; the difficulty page has none.
    campaigns = read("content/baseoq4/pak0/guis/menu/campaigns.q4ui")
    for needle in ('"id": "newgame"', '"id": "chapters"', '"campaignNew"', '"campaignChapters"', '"detailEnterChapters"'):
        require(campaigns, needle, "Campaign's NEW GAME and CHAPTERS")
    skill = read("content/baseoq4/pak0/guis/menu/campaign_skill.q4ui")
    if "chapters-plate" in skill or "campaignChapterStart" in skill:
        raise AssertionError("the difficulty page no longer holds the chapters")
    page = read("content/baseoq4/pak0/guis/menu/chapters.q4ui")
    rows = len(re.findall(r'"id": "row-\d+",\n\s*"type": "group"', page)) or len(set(re.findall(r'"row-(\d+)"', page)))
    if rows < len(QUAKE4):
        raise AssertionError(f"the CHAPTERS page holds {rows} chapter rows, fewer than Quake 4's {len(QUAKE4)} levels")
    for request in ("campaignChapterSelect", "campaignChapterStart", "campaignChaptersBack"):
        require(page, request, "the CHAPTERS page's requests")
    select = function_body(session, "static void Session_CampaignSelect_f(")
    require(select, '"chapters"', "an engine reload into a campaign continues onto its CHAPTERS")

    # Every new text in every shipped language.
    for language in LANGUAGES:
        strings = read(f"content/baseoq4/pak0/strings/{language}_openq4.lang")
        for key in STRINGS:
            require(strings, f'\t"{key}"\t"', f"{language}_openq4.lang")
    guide = read("docs/user/campaigns.md")
    for needle in ("## Chapters", "`campaignChapters`", "`campaignChapter <number>`"):
        require(guide, needle, "the campaign guide")
    print("campaign_chapters: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
