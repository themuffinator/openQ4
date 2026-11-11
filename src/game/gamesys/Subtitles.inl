// Shared SP/MP presentation; Quake 4 SDK EULA, see src/game/LICENSE.

void idPlayerView::ResetSubtitles() {
	if (subtitleGui) uiManager->DeAlloc(subtitleGui);
	subtitleGui = NULL;
	for (int i = 0; i < 2; ++i) { subtitleCacheText[i].Clear(); subtitlePages[i].clear(); }
}

void idPlayerView::DrawSubtitles() {
	if (!player || !cvarSystem->GetCVarBool("ui_subtitles")) return;
	idSoundWorld* world = soundSystem->GetSoundWorldFromId(SOUNDWORLD_GAME);
	if (!world) return;
	soundSubtitle_t cues[2];
	const int count = world->GetSubtitles(cues, 2);
	if (!count) return;
	if (!subtitleGui) subtitleGui = uiManager->FindGui("guis/hud/subtitles.rui", true, true);
	if (!subtitleGui) return;
	const float textScale = idMath::ClampFloat(0.75f, 2.0f, cvarSystem->GetCVarFloat("ui_retainedTextScale"));
	const float size = 17.0f * idMath::ClampFloat(1.0f, 2.0f, cvarSystem->GetCVarFloat("ui_subtitleScale"));
	// Match the document's 720-high canvas without a minimum width, including
	// a portrait display and the headset's common virtual HUD surface.
	const float width = Min(1040.0f, 720.0f * renderSystem->GetScreenWidth() / Max(1, renderSystem->GetScreenHeight()) * 0.84f);
	const float pitch = size * textScale * 1.4f;
	const float aspect = static_cast<float>(renderSystem->GetScreenWidth()) / Max(1, renderSystem->GetScreenHeight());
	const bool cinematic = gameLocal.GetCamera() || player->GetPrivateCameraView();
	// Where one line fits under a portrait cinematic's artwork, a lone caption pages a
	// line at a time to stay clear of the title text drawn in the picture, and with two
	// speakers the newer one takes the matching line above the artwork.
	const bool oneLineBelow = cinematic && OpenQ4Subtitles::LinesBelowArtwork(aspect, pitch) == 1;
	const int singleRows = oneLineBelow ? 1 : 2;
	const bool split = oneLineBelow && count == 2;
	subtitleGui->SetStateFloat("width", width);
	subtitleGui->SetStateFloat("x", (720.0f * renderSystem->GetScreenWidth() / Max(1, renderSystem->GetScreenHeight()) - width) * 0.5f);
	subtitleGui->SetStateFloat("size", size);
	subtitleGui->SetStateFloat("pitch", pitch);
	subtitleGui->SetStateFloat("background", idMath::ClampFloat(0, 1, cvarSystem->GetCVarFloat("ui_subtitleBackground")));
	renderFontMetrics_t metrics;
	idStr fontName = va("fonts/%s/lowpixel", cvarSystem->GetCVarString("sys_lang"));
	if (!renderSystem->GetRetainedFontMetrics(fontName.c_str(), 48, metrics)) fontName = "fonts/english/lowpixel";
	for (int line = 0; line < 2; ++line) {
		subtitleGui->SetStateString(va("line%d", line), "");
		subtitleGui->SetStateString(va("speaker%d", line), "");
		subtitleGui->SetStateFloat(va("indent%d", line), 0);
	}
	subtitleGui->SetStateString("aboveLine", "");
	subtitleGui->SetStateString("aboveSpeaker", "");
	subtitleGui->SetStateFloat("aboveIndent", 0);
	subtitleGui->SetStateFloat("aboveLines", 0);
	int visibleLines = 1;
	for (int i = 0; i < count; ++i) {
		const soundSubtitle_t& cue = cues[i];
		idStr speaker;
		if (cvarSystem->GetCVarBool("ui_subtitleSpeakers")) {
			const int entity = cue.speakerId - 1;
			if (entity >= 0 && entity < MAX_GENTITIES && gameLocal.entities[entity] &&
				gameLocal.entities[entity]->GetSoundEmitter() == cue.emitterIndex) {
				const char* key = gameLocal.entities[entity]->spawnArgs.GetString("npc_name");
				const char* name = !idStr::Cmpn(key, "#str_", 5) ? common->GetLanguageDict()->FindString(key) : key;
				if (name) speaker = name;
			}
			if (speaker.IsEmpty() && cue.radio) speaker = common->GetLocalizedString("#str_231312");
		}
		auto measure = [&](const char* p, int available, float& advance) {
			unsigned int scalar = 0;
			int bytes = LangDict_NextCodePoint(p, available, scalar);
			renderFontGlyph_t glyph;
			advance = renderSystem->GetRetainedFontGlyph(fontName.c_str(), 48, scalar, glyph) ? glyph.advance * size * textScale / 48.0f : size * textScale;
			return bytes;
		};
		float speakerWidth = 0;
		if (!speaker.IsEmpty()) {
			speaker.RemoveColors(); speaker += ": ";
			for (int p = 0; p < speaker.Length();) { float advance; p += Max(1, measure(speaker.c_str() + p, speaker.Length() - p, advance)); speakerWidth += advance; }
			speakerWidth = OpenQ4Subtitles::SpeakerIndent(speakerWidth + 4, width);
			if (speakerWidth <= 0) speaker.Clear();
		}
		const float available = Max(16.0f, width - 24 - speakerWidth);
		const int rows = count == 1 ? singleRows : 1;
		idStr cacheKey(cue.text); cacheKey += fontName;
		if (subtitleCacheText[i] != cacheKey || subtitleCacheWidth[i] != available || subtitleCacheSize[i] != size * textScale || subtitleCacheRows[i] != rows) {
			subtitleCacheText[i] = cacheKey;
			subtitlePages[i] = OpenQ4Subtitles::Paginate(cue.text, available, rows, measure);
			subtitleCacheWidth[i] = available; subtitleCacheSize[i] = size * textScale; subtitleCacheRows[i] = rows;
		}
		const int page = OpenQ4Subtitles::PageAt(subtitlePages[i], cue.timeMsec, cue.durationMsec);
		if (page < 0) continue;
		const auto& shown = subtitlePages[i][page];
		if (split && i == 0) {
			subtitleGui->SetStateString("aboveSpeaker", speaker.c_str());
			subtitleGui->SetStateFloat("aboveIndent", speakerWidth);
			subtitleGui->SetStateString("aboveLine", shown.lines[0].c_str());
			subtitleGui->SetStateFloat("aboveLines", 1);
			continue;
		}
		const int row = split ? 0 : i;
		subtitleGui->SetStateString(va("speaker%d", row), speaker.c_str());
		subtitleGui->SetStateFloat(va("indent%d", row), speakerWidth);
		subtitleGui->SetStateString(va("line%d", row), shown.lines[0].c_str());
		visibleLines = Max(visibleLines, row + 1);
		if (count == 1 && !shown.lines[1].empty()) {
			subtitleGui->SetStateString("line1", shown.lines[1].c_str());
			subtitleGui->SetStateFloat("indent1", speakerWidth);
			visibleLines = 2;
		}
	}
	subtitleGui->SetStateFloat("lines", static_cast<float>(visibleLines));
	// Placed once the line count is known: below a portrait cinematic's artwork it hangs from its top.
	subtitleGui->SetStateFloat("bottom", OpenQ4Subtitles::CanvasBottom(aspect, cinematic, pitch, visibleLines));
	subtitleGui->SetStateFloat("aboveBottom", OpenQ4Subtitles::CanvasAboveBottom(aspect));
	subtitleGui->StateChanged(gameLocal.time);
	const bool previous = renderSystem->GetUseUIViewportFor2D();
	renderSystem->SetUseUIViewportFor2D(true);
	subtitleGui->Redraw(gameLocal.time);
	renderSystem->SetUseUIViewportFor2D(previous);
}
