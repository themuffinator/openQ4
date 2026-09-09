// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include <stdint.h>

struct renderWindowState_s;

// Engine-only settings lease; never crosses the renderer/game module ABI.
// Positions are frame coordinates, matching archived win_xpos/win_ypos.
struct sysWindowPlacementSnapshot_t {
	int x = 0, y = 0, width = 0, height = 0;
	int normalX = 0, normalY = 0, normalWidth = 0, normalHeight = 0;
	bool normalValid = false;
};

#if defined(USE_SDL3) && !defined(ID_DEDICATED)
bool Sys_BeginWindowPlacementLease(uint64_t token, sysWindowPlacementSnapshot_t* baseline, char* error, int errorSize);
bool Sys_ReadWindowPlacementLease(uint64_t token, sysWindowPlacementSnapshot_t* current, char* error, int errorSize);
bool Sys_BuildWindowPlacementCommit(uint64_t token, const sysWindowPlacementSnapshot_t* expectedCurrent,
	const renderWindowState_s* actual, sysWindowPlacementSnapshot_t* committed, char* error, int errorSize);
bool Sys_ApplyWindowPlacementLease(uint64_t token, const sysWindowPlacementSnapshot_t* expectedCurrent,
	const sysWindowPlacementSnapshot_t* finalState, char* error, int errorSize);
// Compare every live field before writes, preserve divergent external changes,
// and keep the lease active on failure. Never clears any CVar modified flags.
// Caller supplies expected owned values after its catalog writes and either the
// captured baseline (restore) or BuildWindowPlacementCommit result (Keep).
bool Sys_FinishWindowPlacementLease(uint64_t token, const sysWindowPlacementSnapshot_t* expectedCurrent,
	const sysWindowPlacementSnapshot_t* finalState, char* error, int errorSize);
bool Sys_WindowPlacementLeaseActive();
#else
inline bool Sys_BeginWindowPlacementLease(uint64_t, sysWindowPlacementSnapshot_t*, char*, int) { return false; }
inline bool Sys_ReadWindowPlacementLease(uint64_t, sysWindowPlacementSnapshot_t*, char*, int) { return false; }
inline bool Sys_BuildWindowPlacementCommit(uint64_t, const sysWindowPlacementSnapshot_t*, const renderWindowState_s*, sysWindowPlacementSnapshot_t*, char*, int) { return false; }
inline bool Sys_ApplyWindowPlacementLease(uint64_t, const sysWindowPlacementSnapshot_t*, const sysWindowPlacementSnapshot_t*, char*, int) { return false; }
inline bool Sys_FinishWindowPlacementLease(uint64_t, const sysWindowPlacementSnapshot_t*, const sysWindowPlacementSnapshot_t*, char*, int) { return false; }
inline bool Sys_WindowPlacementLeaseActive() { return false; }
#endif
