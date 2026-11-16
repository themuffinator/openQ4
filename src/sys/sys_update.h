/*
===========================================================================

openQ4 platform services for the in-game updater
Copyright (C) 2026 DarkMatter Productions

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

===========================================================================
*/

#ifndef __SYS_UPDATE_H__
#define __SYS_UPDATE_H__

/*
===============================================================================

	The client's only HTTPS client, used by the updater (src/framework/
	Updater.cpp) and nothing else. It uses the platform's own TLS stack:
	WinHTTP on Windows, and on Linux and macOS the system libcurl, loaded when
	first needed so the game neither links nor ships a TLS library. A system
	without libcurl simply cannot check for updates.

	Requests are GET only, HTTPS only (redirects included), certificate and
	host checks stay on, and a response larger than maxBytes is abandoned.

	Implemented in sys/win32/win_update.cpp and sys/posix/posix_update.cpp;
	client builds only.

===============================================================================
*/

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

struct sysHttpsResult_t {
	bool		ok = false;			// a 2xx response arrived complete
	bool		cancelled = false;	// a callback asked to stop
	int			status = 0;			// the final HTTP status, 0 when none arrived
	std::string	error;				// English, for the console and log
};

// Called with each piece of the body; return false to stop.
using sysHttpsDataFn = std::function<bool( const void *data, std::size_t bytes )>;
// Called as the body arrives (total is 0 when the server did not say); return false to stop.
using sysHttpsProgressFn = std::function<bool( std::uint64_t received, std::uint64_t total )>;

// False, with the reason, when this system has no usable HTTPS client.
bool					Sys_UpdateHttpsAvailable( std::string &reason );
sysHttpsResult_t		Sys_UpdateHttpsGet( const std::string &url, std::uint64_t maxBytes,
							const sysHttpsDataFn &onData, const sysHttpsProgressFn &onProgress );

// The running executable, with full Unicode fidelity.
std::filesystem::path	Sys_UpdateExecutablePath( void );
std::filesystem::path	Sys_UpdateWorkingDirectory( void );
// What the game was started with, so the helper can start it the same way:
// POSIX fills arguments (argv[1..]); Windows fills commandTail (the command
// line after the program name, verbatim).
void					Sys_UpdateLaunchArguments( std::vector<std::string> &arguments, std::string &commandTail );
std::uint64_t			Sys_UpdateProcessId( void );
// Starts a program that outlives this process.
bool					Sys_UpdateStartDetached( const std::filesystem::path &executable,
							const std::vector<std::string> &arguments, std::string &error );
// Unpacks a .tar.xz with the system's tar (Linux update archives); false on
// Windows, whose update archives are zips.
bool					Sys_UpdateExtractTar( const std::filesystem::path &archive,
							const std::filesystem::path &destination, std::string &error );
// Marks a copied program runnable (POSIX); nothing to do on Windows.
bool					Sys_UpdateMakeExecutable( const std::filesystem::path &path );
// The packaging that owns this install's files and updates them itself, such
// as an AppImage, Flatpak, Snap, macOS app bundle or Android package; NULL
// when openQ4 manages its own files.
const char *			Sys_UpdateManagedBy( void );

#endif /* !__SYS_UPDATE_H__ */
