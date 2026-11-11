// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include "../retained/Document.h"
#include "../../renderer/RendererModule.h"
#include "../../sys/DisplayModeRule.h"

namespace openq4::ui {

struct SystemDisplayMode {
	int width = 0, height = 0; // Physical pixels, including SDL mode density.
	double refresh = 0;
	bool operator==(const SystemDisplayMode&) const = default;
};
struct SystemDisplayDescriptor {
	unsigned id = 0; // Current SDL lifetime only; never serialized for recovery.
	std::string name;
	int x = 0, y = 0, width = 0, height = 0; // SDL display-coordinate bounds.
	SystemDisplayMode desktop;
	std::vector<SystemDisplayMode> modes;
	bool operator==(const SystemDisplayDescriptor&) const = default;
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
// windowMultisampling: the back end multisamples its window (the OpenGL
// family), so r_multiSamples is the window's sample request. Otherwise the
// request is a single-sample window (Vulkan's swapchain), and r_multiSamples
// reaches only the scene targets the device restart re-creates.
bool BuildDisplayRequest(const StateValues& validatedCandidate, const rendererDisplayState_t& captured,
	const SystemDisplayTopology& topology, SystemDisplayPlan& output, std::string& error, bool windowMultisampling = true);
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
// A changed r_swapInterval is immediate, not a display edit: the target plan
// then carries the catalog's new interval and nothing else may differ.
bool ValidateDisplayPreserveActualPair(const StateValues& savedRestore, const StateValues& savedTarget,
	const StateValues& catalogBaseline, const StateValues& catalogTarget, std::string& error);
bool ResolveDisplayRecovery(const StateValues& saved, const SystemDisplayTopology& freshTopology,
	SystemDisplayPlan& output, std::string& error);

// The SYSTEM page's display lists, built purely from one topology snapshot and
// the draft. The device list is Auto (r_screen -1) and then one slot per
// r_screen index. The mode and refresh lists are slot indices: visible slots
// first, and the last slot reserved, never shown in the popup, for a current
// value the list does not offer, so the closed control can still name it.
enum class SystemDisplayList { Device, Mode, Refresh };
constexpr int SystemDisplayDeviceSlots = 8, SystemDisplayModeSlots = 40, SystemDisplayRefreshSlots = 16;
// Localized labels. A format takes exactly the placeholders noted, in order,
// and only %d and %s; any other shape falls back to the English text here.
struct SystemDisplayCatalogText {
	std::string automatic = "Auto";                       // #str_229914
	std::string desktop = "Desktop Native";               // #str_229973
	std::string custom = "Custom";                        // #str_229974
	std::string size = "%d \xC3\x97 %d";                  // #str_230083: width, height
	std::string qualified = "%s (%s)";                    // #str_230084: label, detail
	std::string rate = "%d Hz";                           // #str_230085: whole hertz
	std::string display = "%d: %s";                       // #str_230086: number, name
	std::string missing = "Display %d (not connected)";   // #str_230087: number
	bool operator==(const SystemDisplayCatalogText&) const = default;
};
// What decides whether a captured topology still serves the lists: the display
// topology generation, the window's display and the labels now in effect.
struct SystemDisplayCatalogStamp {
	std::uint64_t generation = 0;
	unsigned currentDisplay = 0; // 0 without a window.
	SystemDisplayCatalogText text;
	bool operator==(const SystemDisplayCatalogStamp&) const = default;
};
struct SystemDisplayCatalogInput {
	SystemDisplayTopology topology;
	unsigned currentDisplay = 0; // The window's display; the primary when unknown.
	SystemDisplayCatalogText text;
};
struct SystemDisplayModeSlot {
	enum class Kind { Desktop, Size, Custom, Unlisted };
	Kind kind = Kind::Desktop;
	int width = 0, height = 0, legacyMode = -1;
	std::string label;
	bool operator==(const SystemDisplayModeSlot&) const = default;
};
struct SystemDisplayRefreshSlot {
	int rate = 0; // Whole hertz; 0 is Auto.
	std::string label;
	bool operator==(const SystemDisplayRefreshSlot&) const = default;
};
struct SystemDisplayCatalog {
	bool available = false, spanAvailable = false;
	int count = 0;                                     // Connected displays.
	std::vector<std::string> devices;                  // r_screen 0..: connected, then stale up to the draft's.
	int deviceSelected = -1;                           // The draft's r_screen.
	std::vector<SystemDisplayDescriptor> descriptors;  // Connected displays in r_screen order.
	int autoDisplay = -1;                              // The descriptor Auto (r_screen -1) describes: the window's display, else the primary.
	std::vector<SystemDisplayModeSlot> modes;          // Visible slots: Desktop, sizes, Custom.
	std::vector<SystemDisplayModeSlot> unlistedMode;   // At most one: the reserved slot's current size.
	int modeSelected = 0;                              // A visible slot, or the reserved last slot.
	std::vector<SystemDisplayRefreshSlot> refresh;     // Visible slots: Auto, then rates.
	std::vector<SystemDisplayRefreshSlot> unlistedRefresh;
	int refreshSelected = 0;
	bool operator==(const SystemDisplayCatalog&) const = default;
};
bool BuildSystemDisplayCatalog(const SystemDisplayCatalogInput& input, const StateValues& draft,
	SystemDisplayCatalog& output, std::string& error);
// The settings one pick writes: r_screen; r_mode with the custom size; or
// r_displayRefresh. A display or size pick also returns what the new choice
// cannot offer to its automatic value: a listed or legacy size the new display
// lacks to Desktop Native (r_mode -2), and a rate the resulting size lacks to
// Auto (r_displayRefresh 0). A typed Custom size stays. Picking the current
// entry writes nothing, so a re-pick never turns a same-size setting into a
// display restart. A display that is not connected cannot be picked, nor can
// a reserved slot.
bool SystemDisplaySelectionPatch(const SystemDisplayCatalog& catalog, SystemDisplayList list, int index,
	StateValues& patch, std::string& error);
// Two descriptors name the same monitor: name, bounds and desktop mode.
bool SameSystemDisplay(const SystemDisplayDescriptor& a, const SystemDisplayDescriptor& b);

} // namespace openq4::ui
