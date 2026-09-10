// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include "../retained/Document.h"
#include "../../renderer/RendererModule.h"

namespace openq4::ui {

struct SystemDisplayMode {
	int width = 0, height = 0; // Physical pixels, including SDL mode density.
	double refresh = 0;
};
struct SystemDisplayDescriptor {
	unsigned id = 0; // Current SDL lifetime only; never serialized for recovery.
	std::string name;
	int x = 0, y = 0, width = 0, height = 0; // SDL display-coordinate bounds.
	SystemDisplayMode desktop;
	std::vector<SystemDisplayMode> modes;
};
struct SystemDisplayTopology {
	unsigned primary = 0;
	bool absolutePlacement = false;
	std::vector<SystemDisplayDescriptor> displays;
};
struct SystemDisplayPlan {
	renderWindowRequest_t request{};
	renderWindowState_t expected{};
	bool checkPixels = false, checkMode = false, checkRefresh = false, checkPosition = false;
	std::vector<SystemDisplayDescriptor> monitors; // Selected first; all spanned displays when applicable.
};

// Read-only SDL capture. All other helpers consume snapshots, read no CVars or
// SDL state, and leave their output unchanged on failure. Device generation,
// owner/request identity and fresh successful presentation remain coordinator
// responsibilities; MatchesDisplay verifies actual policy/parameters only.
bool CaptureDisplayTopology(SystemDisplayTopology& output, std::string& error);
bool BuildDisplayRequest(const StateValues& validatedCandidate, const rendererDisplayState_t& captured,
	const SystemDisplayTopology& topology, SystemDisplayPlan& output, std::string& error);
bool BuildDisplayRestore(const rendererDisplayState_t& captured, const SystemDisplayTopology& topology,
	SystemDisplayPlan& output, std::string& error);
bool MatchesDisplay(const SystemDisplayPlan& plan, const rendererDisplayState_t& observed, std::string& error);

// Strict portable recovery payload: no SDL IDs, module epochs or owner tokens.
// Resolution requires a unique exact name/bounds/desktop-mode descriptor; a
// missing or ambiguous monitor/topology is unresolved, never an index fallback.
// This identifies the observed configuration, not an EDID/serial hardware ID.
bool CaptureDisplayRecovery(const SystemDisplayPlan& plan, const SystemDisplayTopology& topology,
	StateValues& output, std::string& error);
// Validate saved records without consulting the current monitor set. IDs in
// these outputs are synthetic and valid only inside the returned historical
// topology; NEVER submit an inspected plan to a renderer. The mode list contains
// only recorded exclusive modes, not a claim about current device support.
bool InspectDisplayRecovery(const StateValues& saved, SystemDisplayPlan& output,
	SystemDisplayTopology& recordedTopology, std::string& error);
// Cross-check both saved records and the intended catalog using their recorded
// descriptors, even when the unused recovery direction is unavailable today.
// The selected recovery direction must still Resolve against fresh topology.
bool ValidateDisplayRecoveryPair(const StateValues& savedRestore, const StateValues& savedTarget,
	const StateValues& catalogTarget, std::string& error);
// Coalesced image/resource rebuild with no catalog display edit: both portable
// plans must preserve the same captured actual display. Archived intent may
// differ from that actual baseline (for example a prior legacy fallback).
// Placement metadata/catalog dimensions are checked by the journal envelope;
// selected-direction topology resolution and fresh readiness are still required.
bool ValidateDisplayPreserveActualPair(const StateValues& savedRestore, const StateValues& savedTarget,
	const StateValues& catalogBaseline, const StateValues& catalogTarget, std::string& error);
bool ResolveDisplayRecovery(const StateValues& saved, const SystemDisplayTopology& freshTopology,
	SystemDisplayPlan& output, std::string& error);

} // namespace openq4::ui
