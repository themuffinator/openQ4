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

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "ServerListFallback.h"
#include "ServerListParse.h"

#ifndef ID_DEDICATED

#include <atomic>
#include <thread>

#include "../../sys/sys_update.h"

// The user's decision of 23 November 2026: empty until the project's hosts are chosen.
idCVar net_serverListUrl( "net_serverListUrl", "", CVAR_SYSTEM | CVAR_ARCHIVE,
	"an https:// address of an openQ4 server list (tools/master/openq4_master.py --json) the browser also reads at each Internet refresh; empty reads none" );

static const std::uint64_t SERVER_LIST_MAX_BYTES = 2 * 1024 * 1024;

struct idServerListFallback::job_t {
	std::atomic<bool>			done { false };
	std::string					url;
	std::string					error;
	std::vector<std::string>	addresses;
};

static void RunServerListFetch( std::shared_ptr<idServerListFallback::job_t> job );

void idServerListFallback::Start( const char *url ) {
	if ( url == NULL || url[ 0 ] == '\0' ) {
		return;
	}
	if ( idStr::Icmpn( url, "https://", 8 ) != 0 ) {
		common->Printf( "server list: net_serverListUrl must start with https://; not read\n" );
		return;
	}
	job = std::make_shared<job_t>();
	job->url = url;
	std::thread( RunServerListFetch, job ).detach();
}

// Worker thread: touches only its own job.
static void RunServerListFetch( std::shared_ptr<idServerListFallback::job_t> job ) {
	std::string reason;
	if ( !Sys_UpdateHttpsAvailable( reason ) ) {
		job->error = reason;
		job->done = true;
		return;
	}
	std::string body;
	const sysHttpsResult_t result = Sys_UpdateHttpsGet( job->url, SERVER_LIST_MAX_BYTES,
		[&body]( const void *data, std::size_t bytes ) {
			body.append( static_cast<const char *>( data ), bytes );
			return true;
		}, sysHttpsProgressFn() );
	if ( !result.ok ) {
		job->error = result.error.empty() ? "the list could not be fetched" : result.error;
	} else if ( !serverListParse::ParseServerList( body, job->addresses, job->error ) ) {
		job->addresses.clear();
	}
	job->done = true;
}

bool idServerListFallback::Poll( std::vector<std::string> &addresses, std::string &url, std::string &error ) {
	if ( !job || !job->done ) {
		return false;
	}
	addresses = job->addresses;
	url = job->url;
	error = job->error;
	job.reset();
	return true;
}

void idServerListFallback::Clear( void ) {
	job.reset();
}

#else

void idServerListFallback::Start( const char * ) {
}

bool idServerListFallback::Poll( std::vector<std::string> &, std::string &, std::string & ) {
	return false;
}

void idServerListFallback::Clear( void ) {
}

#endif /* !ID_DEDICATED */
