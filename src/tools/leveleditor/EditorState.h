// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENQ4_EDITOR_STATE_H
#define OPENQ4_EDITOR_STATE_H

#include "MapGeometry.h"

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace oq4editor {

// Docked panels in the 640x480 GUI space. The browser, inspector and tools
// need a tall column; the problems/build panel also fits under the views.
// Up to two panels share a dock as tabs, so every title keeps its width in
// every language. Layouts persist as a compact string.
class DockLayout {
public:
	enum Panel { Browser, Inspector, Tools, Problems, PanelCount };
	enum Dock { Left, Right, Bottom, Hidden, DockCount };
	struct Rect { float x = 0, y = 0, w = 0, h = 0; };
	struct PanelPlacement { Rect rect, tab; bool visible = false, tabVisible = false; };
	struct Result {
		PanelPlacement panels[PanelCount];
		Rect views, leftSplitter, rightSplitter, bottomSplitter;
	};
	static constexpr float MinSide = 130, MaxSide = 230, MinBottom = 80, MaxBottom = 180;
	static constexpr int MaxPerDock = 2;
	static constexpr float TabHeight = 28; // title line plus the active tab's dock buttons

	DockLayout();
	static const char *PanelName( Panel panel );
	static const char *DockName( Dock dock );
	static bool Allowed( Panel panel, Dock dock );
	bool Parse( const std::string &text );
	std::string Serialize() const;
	bool Move( Panel panel, Dock dock );
	void Activate( Panel panel );
	void Toggle( Panel panel );
	void Resize( Dock dock, float delta );
	Dock DockOf( Panel panel ) const { return docks[panel]; }
	float Size( Dock dock ) const { return sizes[dock]; }
	Panel Active( Dock dock ) const { return active[dock]; }
	Result Compute( const Rect &area ) const;
private:
	Dock docks[PanelCount];
	Dock lastDock[PanelCount];
	Panel active[DockCount];
	float sizes[DockCount];
	void FixActive();
};

// Per-map view state: cameras, grid, layer visibility and selection,
// stored beside the recovery files as "key value" lines.
struct ViewState {
	Vec3 target;
	double yaw = 225, pitch = 25, distance = 512;
	int mode = 0;          // 0 perspective, 1 top, 2 front, 3 side
	bool quad = false;
	Vec3 orthoCenter;
	double orthoScale = 4; // world units per virtual pixel
	double grid = 8;
	bool textureLock = true;
	std::set<std::string> hiddenLayers, lockedLayers;
	std::vector<std::string> selection; // "entity" or "entity:primitive" document indexes
	bool valid = false;

	std::string Serialize() const;
	bool Parse( const std::string &text );
};

std::string ViewStatePath( const std::string &mapPath );

// Bounded, time-sliced catalog of asset names. Sources are polled a little
// per frame, so building it never blocks presentation, and it can be
// cancelled or restarted. Searches are substring matches without case.
class AssetIndex {
public:
	enum Kind { EntityDef, Material, Model, Sound, Map, KindCount };
	using Source = std::function<bool( std::size_t index, std::string &name )>; // false = exhausted
	struct Stats { std::size_t entries = 0, bytes = 0, steps = 0; bool complete = false, truncated = false, cancelled = false; };
	static constexpr std::size_t MaxEntries = 262144;
	static constexpr std::size_t MaxBytes = 16u * 1024u * 1024u;

	void Begin( const std::vector<std::pair<Kind, Source>> &sources );
	// Processes up to budget items; returns true when indexing is finished.
	bool Step( std::size_t budget );
	void Cancel();
	bool Complete() const { return stats.complete; }
	const Stats &GetStats() const { return stats; }
	std::size_t Count( Kind kind ) const { return names[kind].size(); }
	std::vector<std::string> Search( Kind kind, const std::string &filter, std::size_t limit ) const;
	bool Contains( Kind kind, const std::string &name ) const;
	static const char *KindName( Kind kind );
private:
	std::vector<std::pair<Kind, Source>> pending;
	std::size_t sourceIndex = 0, itemIndex = 0;
	std::vector<std::string> names[KindCount];
	std::set<std::string> lookup[KindCount];
	Stats stats;
};

}
#endif
