// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapBuild.h"

#include <algorithm>

namespace oq4editor {

namespace {

std::string Directory( const std::string &path ) {
	const auto slash = path.rfind( '/' );
	return slash == std::string::npos ? std::string() : path.substr( 0, slash );
}

std::string BaseName( const std::string &path ) {
	const auto slash = path.rfind( '/' );
	std::string name = slash == std::string::npos ? path : path.substr( slash + 1 );
	const auto dot = name.rfind( '.' );
	return dot == std::string::npos ? name : name.substr( 0, dot );
}

// Compiler outputs that travel with a map: render, collision, navigation.
bool IsOutput( const std::string &file, const std::string &base ) {
	if ( file.size() <= base.size() + 1 || !EqualNoCase( file.substr( 0, base.size() + 1 ), base + "." ) ) { return false; }
	const std::string extension = file.substr( base.size() + 1 );
	if ( extension.find( '.' ) != std::string::npos ) { return false; }
	return EqualNoCase( extension, "proc" ) || EqualNoCase( extension, "cm" ) ||
		( extension.size() > 3 && EqualNoCase( extension.substr( 0, 3 ), "aas" ) );
}

struct Marker { const char *text; const char *stage; float progress; };
const Marker markers[] = {
	{ "--- LoadDMapFile ---", "loading", 0.05f },
	{ "--- FaceBSP ---", "bsp", 0.15f },
	{ "----- MakeTreePortals", "portals", 0.25f },
	{ "--- FloodEntities ---", "flood", 0.3f },
	{ "--- FloodAreas ---", "areas", 0.35f },
	{ "----- ClipSidesByTree", "clip", 0.4f },
	{ "----- PutPrimitivesInAreas", "surfaces", 0.45f },
	{ "----- BuildLightShadows", "shadows", 0.5f },
	{ "----- FixGlobalTjunctions", "tjunctions", 0.55f },
	{ "----- WriteOutputFile", "writing", 0.62f },
	{ "seconds for dmap", "collision", 0.68f },
	{ "seconds to create collision map", "navigation", 0.75f },
	{ "seconds to create AAS", "finishing", 0.97f },
};

}

std::string MapBuild::SandboxMaps() const { return request.sandbox + "/baseoq4/" + Directory( request.mapPath ); }
std::string MapBuild::LogPath() const { return request.sandbox + "/baseoq4/logs/editor_build.log"; }

std::vector<std::string> MapBuild::ChildArguments( const BuildRequest &request, const std::string &sandboxOS ) {
	std::vector<std::string> args = {
		"+set", "fs_savepath", sandboxOS,
		// Write .proc/.aas beside the sandboxed map instead of fs_cdpath.
		"+set", "com_compilerOutput", "fs_savepath",
		"+set", "logFile", "2", "+set", "logFileName", "logs/editor_build.log",
		"+set", "developer", "0",
		"+dmap" };
	if ( !request.aas ) { args.push_back( "noAAS" ); }
	args.push_back( request.mapPath );
	args.push_back( "+quit" );
	return args;
}

void MapBuild::ClearSandbox() {
	const std::string base = BaseName( request.mapPath );
	for ( const auto &file : storage.List( SandboxMaps(), base + "." ) ) { storage.Remove( SandboxMaps() + "/" + file ); }
	storage.Remove( LogPath() );
}

bool MapBuild::Start( const BuildRequest &input, const CompileIndex &index, unsigned nowMs, std::string &error ) {
	error.clear();
	if ( Active() ) { error = "a build is already running"; return false; }
	if ( input.mapPath.size() < 9 || input.mapPath.compare( 0, 5, "maps/" ) != 0 || !EqualNoCase( input.mapPath.substr( input.mapPath.size() - 4 ), ".map" ) ) {
		error = "build needs a saved maps/*.map path"; return false;
	}
	if ( input.mapText.empty() ) { error = "build needs map source"; return false; }
	if ( input.launch.empty() ) { error = "no compiler executable is available"; return false; }
	request = input;
	compileIndex = index;
	diagnostics.clear(); outputs.clear(); failure.clear(); partialLine.clear();
	logOffset = 0; progress = 0; geometryDone = leaked = aasStarted = sawError = false;
	ClearSandbox();
	const std::string sandboxMap = request.sandbox + "/baseoq4/" + request.mapPath;
	if ( !storage.WriteAndSync( sandboxMap, request.mapText, error ) ) { error = "could not prepare the build sandbox: " + error; return false; }
	std::vector<std::string> arguments = request.launch;
	for ( const auto &argument : ChildArguments( request, storage.OSPath( request.sandbox ) ) ) { arguments.push_back( argument ); }
	if ( !child || !child->Start( arguments, error ) ) {
		if ( error.empty() ) { error = "could not start the compiler"; }
		return false;
	}
	state = State::Running;
	stage = "starting";
	startMs = lastPollMs = lastOutputMs = nowMs;
	return true;
}

void MapBuild::Consume( const std::string &line ) {
	for ( const auto &marker : markers ) {
		if ( line.find( marker.text ) != std::string::npos && marker.progress >= progress ) { stage = marker.stage; progress = marker.progress; }
	}
	if ( line.find( "seconds for dmap" ) != std::string::npos ) { geometryDone = true; }
	if ( line.find( "seconds to create collision map" ) != std::string::npos && request.aas ) { aasStarted = true; }
	Diagnostic diagnostic;
	if ( !ParseCompilerLine( line, compileIndex, diagnostic ) ) { return; }
	if ( diagnostic.code == "leak" ) { leaked = true; }
	if ( diagnostic.severity == Diagnostic::Severity::Error ) { sawError = true; }
	for ( const auto &existing : diagnostics ) { if ( existing.message == diagnostic.message ) { return; } }
	if ( diagnostics.size() < MaxDiagnostics ) { diagnostics.push_back( diagnostic ); }
}

void MapBuild::Finish( State result, const std::string &reason ) {
	state = result;
	failure = reason;
	stage = result == State::Succeeded ? "done" : result == State::Cancelled ? "cancelled" : "failed";
	if ( result == State::Succeeded ) { progress = 1.0f; }
}

bool MapBuild::PromoteOutputs( std::string &error ) {
	const std::string base = BaseName( request.mapPath ), destination = Directory( request.mapPath );
	std::vector<std::string> produced;
	for ( const auto &file : storage.List( SandboxMaps(), base + "." ) ) { if ( IsOutput( file, base ) ) { produced.push_back( file ); } }
	if ( std::none_of( produced.begin(), produced.end(), [&]( const std::string &f ) { return EqualNoCase( f, base + ".proc" ); } ) ) {
		error = "the compiler wrote no .proc file"; return false;
	}
	// Stage every new file first; nothing next to the map changes yet.
	std::vector<std::string> staged;
	const auto discard = [&]() { for ( const auto &path : staged ) { storage.Remove( path ); } };
	for ( const auto &file : produced ) {
		std::string text;
		bool exists = false;
		if ( !storage.Read( SandboxMaps() + "/" + file, text, exists, error ) || !exists ) { discard(); if ( error.empty() ) { error = "missing output " + file; } return false; }
		const std::string target = destination + "/" + file + ".editor-tmp";
		if ( !storage.WriteAndSync( target, text, error ) ) { storage.Remove( target ); discard(); return false; }
		staged.push_back( target );
	}
	// Set aside the previous outputs, including ones this build no longer makes.
	std::vector<std::string> previous, published;
	for ( const auto &file : storage.List( destination, base + "." ) ) { if ( IsOutput( file, base ) ) { previous.push_back( destination + "/" + file ); } }
	std::vector<std::string> backups;
	bool ok = true;
	for ( const auto &path : previous ) {
		if ( !storage.Promote( path, path + ".editor-prev", error ) ) { ok = false; break; }
		backups.push_back( path );
	}
	for ( std::size_t i = 0; ok && i < produced.size(); ++i ) {
		const std::string target = destination + "/" + produced[i];
		if ( !storage.Promote( staged[i], target, error ) ) { ok = false; break; }
		published.push_back( target );
	}
	if ( !ok ) {
		for ( const auto &path : published ) { storage.Remove( path ); }
		for ( const auto &path : backups ) { std::string ignored; storage.Promote( path + ".editor-prev", path, ignored ); }
		discard();
		error = "could not publish the build outputs (previous outputs restored): " + error;
		return false;
	}
	for ( const auto &path : backups ) { storage.Remove( path + ".editor-prev" ); }
	outputs = published;
	return true;
}

void MapBuild::Poll( unsigned nowMs ) {
	if ( state != State::Running ) { return; }
	if ( nowMs - lastPollMs < 100 && nowMs != lastPollMs ) { return; }
	lastPollMs = nowMs;
	int exitCode = 0;
	const bool running = child->Running( exitCode );
	// Drain everything the child has logged so far (all of it once it exits).
	for ( int chunk = 0; chunk < ( running ? 4 : 4096 ); ++chunk ) {
		std::string text;
		if ( !storage.ReadFrom( LogPath(), logOffset, 262144, text ) || text.empty() ) { break; }
		logOffset += text.size();
		lastOutputMs = nowMs;
		partialLine += text;
		std::size_t newline;
		while ( ( newline = partialLine.find( '\n' ) ) != std::string::npos ) {
			Consume( partialLine.substr( 0, newline ) );
			partialLine.erase( 0, newline + 1 );
		}
		if ( partialLine.size() > 65536 ) { Consume( partialLine ); partialLine.clear(); }
	}
	if ( running ) {
		if ( ElapsedMs( nowMs ) > request.timeoutMs ) {
			child->Kill();
			Finish( State::Failed, "the compiler exceeded its time limit" );
		} else if ( ( sawError || leaked ) && nowMs - lastOutputMs > ErrorExitMs ) {
			child->Kill();
			Finish( State::Failed, leaked ? "the map leaks: the void is reachable from an entity" : "the compiler stopped after an error" );
		}
		return;
	}
	if ( !partialLine.empty() ) { Consume( partialLine ); partialLine.clear(); }
	if ( leaked ) { Finish( State::Failed, "the map leaks: the void is reachable from an entity" ); return; }
	if ( exitCode != 0 ) { Finish( State::Failed, "the compiler exited with code " + std::to_string( exitCode ) ); return; }
	if ( !geometryDone ) { Finish( State::Failed, "the compiler stopped before finishing the geometry" ); return; }
	std::string error;
	if ( !PromoteOutputs( error ) ) { Finish( State::Failed, error ); return; }
	Finish( State::Succeeded, "" );
}

void MapBuild::Cancel() {
	if ( state != State::Running ) { return; }
	child->Kill();
	int exitCode = 0;
	child->Running( exitCode );
	Finish( State::Cancelled, "cancelled; previous outputs were kept" );
}

}
