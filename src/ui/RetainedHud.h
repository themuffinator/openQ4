// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "retained/Hud.h"

// A presentation snapshot of the game's evaluated HUD. Scripts, input, saves
// and named events remain owned by their existing GUI; no device is polled.
class idRetainedHud {
public:
	idRetainedHud();
	~idRetainedHud();
	static bool Eligible(const char* source);
	// One of the GUIs the coverage bridge draws, whatever the retained gate says.
	static bool Covers(const char* source);
	// A covered GUI is worth capturing: until its first attempt, and after one
	// that loaded the artwork. A mod-owned GUI or a missing catalog never is.
	bool Usable() const;
	static void Precache();
	// Takes the frame's captured primitives: it maps them in place.
	bool Draw(const char* source, std::vector<openq4::ui::HudPrimitive>&& items);
	void Reset();
	bool DrawSucceeded() const;
private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};

class idUserInterface;
class idUserInterfaceLocal;

// The stock interface's HUD (ui_retained 0) in the Remastered layout (visual
// specification 14.4-14.9). No retained peer draws there, so the stock HUD
// GUIs lay themselves out: the engine publishes the HUD settings as state
// their scripts read (oq4_hud*), moves their edge anchors by the HUD width
// cap and the safe inset, and keeps the two pickup lines' slots, which a GUI
// script cannot compare or measure. Classic (ui_hudLayout 0) keeps the stock
// rectangles and anchors, as does the retained interface's own Classic. The
// HUD scale, opacity and reduced motion settings apply here too: the engine
// scales each group about its screen anchor and fades what the windows draw.
class idLegacyHudLayout {
public:
	// Whether a GUI is one of the HUDs this layout covers (the Remastered routes).
	static bool Covers(const char* source);
	// Whether the HUD opacity fades a covered GUI, as it fades the Remastered
	// documents with a gameplay root: every HUD but the objectives display and
	// the scoreboard.
	static bool Fades(const char* source);
	// Whether the HUD scale sizes a covered GUI: the HUDs whose windows follow
	// the Remastered layout and name their scale anchors.
	static bool Scales(const char* source);
	// The definition a GUI is parsed from. The Awakening's HUDs are the stock
	// HUDs of their day and know no layout, so its on-foot HUD and those of
	// the dropship's guns, the MCC's side gun and the speeder bike are read
	// from openQ4's own definitions (tools/ui/build_awakening_legacy_hud.py)
	// unless a mod owns the GUI. Every other GUI is its own definition.
	static const char* DefinitionFor(const char* source);
	// A mod that ships its own copy of a stock-named GUI owns its artwork,
	// layout and scripts: Classic, with no peer and no bridge. A mod without
	// one gets the HUD its base game has. Opens files: resolve once per GUI.
	static bool ModOwns(const char* source);
	// The layout setting: 0 Classic, 1 Remastered, 2 Minimal, 3 Competitive
	// (Remastered outside its retained match bar).
	static int Layout();
	// The HUD settings (13.9, 13.10) the stock windows and the engine follow.
	struct Settings {
		int layout = 0;
		bool pickups = true, strip = true, damage = true, reducedMotion = false;
		float scale = 1.0f, opacity = 1.0f;
	};
	// The settings for a GUI's layout; Classic takes none of them.
	static Settings Read(int layout);
	// Publish oq4_hudLayout, oq4_hudRemastered, oq4_hudMinimal, whether the
	// optional groups show (oq4_hudShowPickups, _Strip, _Damage, _Extras),
	// oq4_hudReducedMotion and oq4_hudScale; the GUI also publishes the edges
	// (oq4_hudEdgeLeft, _Top, _Right, _Bottom).
	static void Publish(idUserInterfaceLocal& gui, const Settings& settings);
	// Where the edge anchors sit outside the 4:3 canvas, in the GUI's virtual
	// units (left, top, right, bottom): the side screens capped by the HUD
	// width setting and every edge moved in by the safe inset. xExpand and
	// yExpand are the view's full expansion beyond the canvas on each side.
	// centerScale, when asked for, is the scale the centre column takes: the
	// HUD scale, less on a view too narrow for the status bar at that scale.
	static void Edges(float xExpand, float yExpand, float edges[4], float scale = 1.0f, float* centerScale = nullptr);
	// The pickup lines (14.8): their life, matching the scripts' hold and fade.
	static constexpr int PickupLife = 3250;
};

// The Remastered layout of a stock HUD GUI (visual specification 14.4-14.9).
// The stock GUI keeps its scripts, saves, named events and Classic drawing.
// This peer receives the same state and events, and draws the generated
// document in place of the stock layout; any failure leaves the stock one.
class idRemasteredHud {
public:
	explicit idRemasteredHud(const char* document);
	~idRemasteredHud();
	// The Remastered document for a stock source, or null when it has none.
	static const char* DocumentFor(const char* source);
	// A small vector instrument drawn after Classic, without replacing it.
	static const char* AirDocumentFor(const char* source);
	// The layout setting and the retained gate both select Remastered.
	static bool Enabled();
	// Whether the multiplayer document presented chat within the last moments:
	// the message-mode input, a GUI of its own, defers to it only then. A
	// document that is missing, failed or replaced by a mod leaves the stock
	// input and the stock lines in charge.
	static bool PresentsChat();
	bool Draw(const idUserInterfaceLocal& legacy, int time);
	// Load the document now, as the stock HUD loads inside a level load, so
	// the first gameplay frame draws it without loading it then. A save
	// restore leaves the load to the first draw, outside its serialized stream.
	void Preload(const char* source);
	void ForwardState(const char* name, const char* value);
	void ForwardDelete(const char* name);
	void ForwardEvent(const char* name);
	// The stock windows went back to their authored defaults (a level load
	// kept this GUI): join their dictionary and replay their state again, and
	// drop what was measured or queued against the previous map.
	void Rejoin();
	// ui_hudProfile: the stock windows' unseen evaluation this frame.
	void ProfileUnseen(double milliseconds);
	bool Failed() const;
	const char* Document() const;
private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
