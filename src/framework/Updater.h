/*
===========================================================================

openQ4 in-game updater
Copyright (C) 2026 DarkMatter Productions

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

===========================================================================
*/

#ifndef __UPDATER_H__
#define __UPDATER_H__

/*
===============================================================================

	Finds, downloads and stages openQ4 updates from the project's GitHub
	releases; see src/updater/UpdateCore.h for the trust model and
	src/updater/UpdatePlan.h for the hand-off to openQ4-updater.

	Dedicated servers build these as no-ops.

===============================================================================
*/

// Registers the update commands.
void	Updater_Init( void );
// Runs from the common frame: reports a finished check, an update the helper
// just installed, or the startup check's result, once the title is idle.
void	Updater_Frame( void );
// Starts a check in the background. fromMenu answers even when there is no
// news ("up to date", "could not check"); the startup check stays quiet then.
void	Updater_Check( bool fromMenu );

#endif /* !__UPDATER_H__ */
