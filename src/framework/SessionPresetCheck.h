// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
// The system check's screen (Session_presetcheck.cpp): the retained document
// guis/menu/presetcheck.rui while ui_retained is 1, else the stock
// guis/presetcheck.gui. It stands in front of the main menu at startup when
// Common_PerformancePresetCheckDue says so.

class idUserInterface;
class idCmdArgs;

// At startup, in place of the main menu: presents the check and returns true
// when it is due (or `force`d), else false and the caller opens the menu.
bool Session_StartPresetCheck( bool force = false );
// Whether `gui` is the retained check, whose verbs come here.
bool Session_PresetCheckOwns( const idUserInterface *gui );
// The retained check's verbs: presetCheckApply, presetCheckKeep, presetCheckLater.
void Session_PresetCheckRequest( idUserInterface *gui, const char *request );
// performancePresetCheck [status|open|apply|keep|later|reset]
void Session_PresetCheck_f( const idCmdArgs &args );
