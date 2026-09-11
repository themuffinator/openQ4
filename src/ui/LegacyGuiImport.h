// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include <cstdint>
class idCmdArgs;
void RetainedUI_ExportLegacy(const idCmdArgs& args);

class idWindow;
class idParser;
class idToken;
class idWinVar;
class idUserInterfaceLocal;
void UI_ObserveLegacy(const idCmdArgs& args);
// Engine-thread diagnostic only. Inactive calls neither allocate nor perform
// lookups. These hooks copy original decisions; they never replay a lookup.
bool UI_LegacyObservationLoad(idUserInterfaceLocal*, idParser&, const char*);
void UI_LegacyObservationLoaded(idUserInterfaceLocal*, bool);
bool UI_LegacyObservationActive(idWindow*) noexcept;
void UI_LegacyObservationParse(idWindow*, bool begin, bool success) noexcept;
unsigned UI_LegacyObservationTerm(idWindow*, idParser*, const idToken&, int tableIndex) noexcept;
void UI_LegacyObservationOp(idWindow*, unsigned, int operation, int result, int type, intptr_t a, intptr_t b) noexcept;
void UI_LegacyObservationFixup(idWindow*, int operation, const char*, idWinVar*) noexcept;
void UI_LegacyObservationResolved(idWindow*, int operation, intptr_t, intptr_t) noexcept;
void UI_LegacyObservationFixed(idWindow*) noexcept;
void UI_LegacyObservationCache(idWindow*, idWindow**) noexcept;
void UI_LegacyObservationEvaluation(idWindow*, const float*, int count, idWinVar*, float alpha) noexcept;
void UI_LegacyObservationDestroyed(idWindow*) noexcept;
