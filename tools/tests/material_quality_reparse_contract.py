#!/usr/bin/env python3
"""Pin how a changed image_ignoreHighQuality reaches the running game.

The cvar is read while a material is parsed: it decides whether a stage that
asks for "highquality" or "uncompressed" gets the uncompressed image identity.
A level load re-parses only the materials that level owns, so menu and HUD
materials kept the old answer until the game was started again, and the
Display page said so.

The renderer now re-parses the materials that read the hint under the other
answer, at the start of a frame. This pins the pieces that make that safe and
complete:

- every parse site of the non-forced hint records that the material read it;
- a parse records the answer it used and registers the material;
- the check runs in BeginFrame before the frame's 2D model is cleared, stays
  pending during a level load or a checked image-policy attempt, and clears
  the cvar's modified flag only when it acts;
- replaced images inherit their level-load lifetime and dropped ones are
  purged only when no parsed material still holds them;
- the Display page no longer reports the setting as finishing at the next
  start.

It also pins the order of the in-game renderer restart: save the game, write
the settings, then queue the reload whose post command loads the save.
"""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8", errors="replace")


def body(source: str, signature: str) -> str:
    """The braced body after a signature, skipping braces inside literals and comments."""
    start = source.index(signature)
    index = brace = source.index("{", start + len(signature) - 1)
    depth = 0
    while index < len(source):
        char = source[index]
        if source.startswith("//", index):
            index = source.index("\n", index)
            continue
        if source.startswith("/*", index):
            index = source.index("*/", index) + 2
            continue
        if char in "\"'":
            index += 1
            while source[index] != char:
                index += 2 if source[index] == "\\" else 1
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[brace:index + 1]
        index += 1
    raise AssertionError(f"unterminated body: {signature}")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    material = read("src/renderer/Material.cpp")
    header = read("src/renderer/Material.h")

    # Every non-forced resolution of the hint marks the material as a reader.
    sites = [m.end() for m in re.finditer(
        r"td = R_ResolveMaterialHighQualityUsage\( pd->qualityInputs, td, false \);", material)]
    require(len(sites) == 3, f"expected three non-forced hint sites, found {len(sites)}")
    for end in sites:
        require(material[end:end + 60].lstrip().startswith("pd->readsHighQualityHint = true;"),
                "a non-forced hint site does not record that the material read it")
    forced = re.findall(r"R_ResolveMaterialHighQualityUsage\( pd->qualityInputs, td, true \);\s*(\S[^\n]*)", material)
    require(forced and all("readsHighQualityHint" not in line for line in forced),
            "forceHighQuality ignores the cvar and must not mark the material")

    parse = body(material, "bool idMaterial::Parse( const char *text, const int textLength ) {")
    record = parse.index("readsHighQualityHint = pd->readsHighQualityHint;")
    require(parse.index("parsedIgnoringHighQuality = qualityInputs.ignoreHighQuality;") > record,
            "a parse must record the answer it used")
    require(record < parse.index("registry.materials.insert( this );") < parse.index("pd = NULL;"),
            "a parse must register the material while its parse data is live")
    init = body(material, "void idMaterial::CommonInit() {")
    require("readsHighQualityHint = false;" in init, "CommonInit must clear the reader flag")
    require("R_ForgetParsedMaterial( this );" in body(material, "idMaterial::~idMaterial() {") and
            "registry.materials.erase( material );" in body(material, "static void R_ForgetParsedMaterial( idMaterial *material ) {"),
            "a destroyed material must leave the registry")
    require("static parsedMaterials_t *registry = new parsedMaterials_t;" in material,
            "the registry must outlive every material, so it is never destroyed")

    reparse = body(material, "bool idMaterial::ReparseForQualityInputs() {")
    require(reparse.index("FreeData();") < reparse.index("Parse( text.Ptr(), length );"),
            "the in-place parse frees the old stages first, as the declaration manager does")
    require("GetTextLength()" in reparse and "return false;" in reparse,
            "a material with no text left must be refused, unchanged")

    check = body(material, "void R_CheckMaterialQualityInputs( void ) {")
    modified = check.index("image_ignoreHighQuality.IsModified()")
    pending = check.index("globalImages->insideLevelLoad || R_ImagePolicyActive()")
    cleared = check.index("image_ignoreHighQuality.ClearModified();")
    require(modified < pending < cleared, "the change must stay pending during a level load or a checked attempt")
    require("material->GetState() != DS_PARSED || !material->ReadsHighQualityHint()" in check and
            "material->ParsedIgnoringHighQuality() != ignoring" in check,
            "only parsed materials that read the hint under the other answer are re-parsed")
    require("DeclByIndex" not in check, "walking the declaration list allocates a material for every declaration")
    sync = check.index('R_RenderThread_Sync( "material quality" );')
    require(sync < check.index("ReparseForQualityInputs()"), "the back end must be idle before stages are rebuilt")
    require("SetLevelLoadLifetime( replacement.second || wasOutside( replacement.first ) )" in check,
            "a replacement image inherits the level-load lifetime of what it replaced")
    purge = check.index("image->PurgeImage();")
    guard = check.rindex("held.find( image ) == held.end() && image->IsLoaded() && image->IsFileBacked()", 0, purge)
    require(check.index("material->CollectImages( images );") < guard < purge,
            "an image another parsed material still holds must not be purged")

    collect = body(material, "void idMaterial::CollectImages( idList<idImage *> &images ) const {")
    for slot in ("stages[i].texture.image", "newStage->fragmentProgramImages[j]", "newStage->shaderTextureImages[j]",
                 "portalImage", "lightFalloffImage", "pbrTextures[i]->image", "specularProbeInfo.cubeImage"):
        require(slot in collect, f"CollectImages misses {slot}")
    require("images.Append( stages[i].texture.image );" in collect,
            "empty slots are appended too, so two parses line up entry for entry")

    require("bool\t\t\t\tReadsHighQualityHint(void) const" in header and "void R_CheckMaterialQualityInputs( void );" in header,
            "Material.h must declare the reader flag and the frame check")

    begin = body(read("src/renderer/RenderSystem.cpp"), "void idRenderSystemLocal::BeginFrame( int windowWidth, int windowHeight ) {")
    require(begin.index('R_RenderThread_SyncFrontEnd( "frame" );') < begin.index("R_CheckMaterialQualityInputs();")
            < begin.index("guiModel->Clear();"),
            "the check belongs at the start of a frame, before any draw surface of it exists")

    host = read("src/ui/application/SystemSettingsHost.cpp")
    next_start = body(host, "bool SystemSettingsHost::ChangedNeedsNextStart(const StateValues& before, const StateValues& target) {")
    require('"s_maxSoundsPerShader"' in next_start and 'Changed(before,target,"image_ignoreHighQuality")' not in next_start,
            "the high-quality setting finishes in the session; only the sound shader limit waits")

    # The in-game renderer restart.
    session = read("src/framework/Session.cpp")
    can = body(session, "bool idSessionLocal::CanSaveForRendererRestart( void ) const {")
    for term in ("!mapSpawned", "insideExecuteMapChange", "loadingSaveGame", "IsDemoPlaybackActive()", "com_editorActive",
                 "idAsyncNetwork::IsActive()", "game->InCinematic()", "objectiveFailed", '"health"'):
        require(term in can, f"the restart must not be offered without checking {term}")
    save = body(session, "bool idSessionLocal::SaveForRendererRestart( void ) {")
    require("SaveGame( SESSION_RENDERER_RESTART_SAVE, ST_AUTO ) && FinishPendingSave( true )" in save,
            "the carried game is a finished autosave before the reload is queued")
    queue = body(session, "void idSessionLocal::QueueRendererRestart( void ) {")
    require('resume.AppendArg( "resumeAfterRendererRestart" );' in queue and "cmdSystem->SetupReloadEngineMenu( resume );" in queue,
            "the reload's post command loads the carried game")
    resume = body(session, "static void Session_ResumeAfterRendererRestart_f( const idCmdArgs &args ) {")
    require(resume.index("sessLocal.LoadGame( SESSION_RENDERER_RESTART_SAVE )") < resume.index("sessLocal.StartMenu();"),
            "a save that will not load leaves the player at the main menu")
    require('cmdSystem->AddCommand( "resumeAfterRendererRestart", Session_ResumeAfterRendererRestart_f' in session,
            "the post command must be registered")

    rendering = read("src/ui/RenderingSettings.cpp")
    restart = body(rendering, "bool RenderingSettings::Restart(std::string& error) {")
    order = [restart.index("sessLocal.SaveForRendererRestart()"), restart.index("Common_WriteSettingsConfiguration(false,error)"),
             restart.index("R_RendererModule_UseSavedSelectionOnReload();"), restart.index("sessLocal.QueueRendererRestart();")]
    require(order == sorted(order), "save the game, write the settings, then queue the reload")
    require("if(!menu && !CanCarryGame()) return Fail(" in restart,
            "a game that cannot be saved still has to be left through the menu")
    require('carry?"#str_231767":"#str_230149"' in rendering, "the page says when the restart keeps the game")

    for language in ("brazilian", "czech", "english", "french", "german", "hungarian", "italian", "polish", "russian",
                     "spanish", "turkish", "ukrainian"):
        table = read(f"content/baseoq4/pak0/strings/{language}_openq4.lang")
        require(table.count('"#str_231767"') == 1, f"{language} is missing the carried-restart notice")

    registry = ROOT / "tools" / "validation" / "openq4_validate.py"
    if registry.exists():
        require("material_quality_reparse_contract.py" in read("tools/validation/openq4_validate.py"),
                "register this test in tools/validation/openq4_validate.py")
    print("material_quality_reparse_contract: ok")


if __name__ == "__main__":
    main()
