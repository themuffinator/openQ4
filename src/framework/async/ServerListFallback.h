/*
===========================================================================

openQ4 GPL Source Code
Copyright (C) 2026 DarkMatter Productions

This file is part of the openQ4 GPL Source Code. It is free software: you can
redistribute it and/or modify it under the terms of the GNU General Public
License as published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.

===========================================================================
*/

// The browser's second source of Internet servers (roadmap MP-19): an HTTPS
// server list (net_serverListUrl, in the form tools/master/openq4_master.py
// --json writes and ServerListParse.h reads), fetched on a worker thread at
// each Internet refresh beside the master query. The two lists merge; the
// browser lists an address once. A dedicated server has no browser, and no
// HTTPS client, and builds none of this.

#ifndef __SERVERLISTFALLBACK_H__
#define __SERVERLISTFALLBACK_H__

#include <memory>
#include <string>
#include <vector>

class idServerListFallback {
public:
	// Starts a fetch of url; one still running is left to finish unheard.
	void				Start( const char *url );
	// True once, on the main thread, when the fetch has ended: its addresses,
	// or the reason there are none.
	bool				Poll( std::vector<std::string> &addresses, std::string &url, std::string &error );
	void				Clear( void );

	struct job_t;		// one fetch, shared with its worker thread

private:
	std::shared_ptr<job_t>	job;
};

#endif /* !__SERVERLISTFALLBACK_H__ */
