// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENQ4_EDITOR_MAP_BUILD_H
#define OPENQ4_EDITOR_MAP_BUILD_H

#include "MapValidator.h"

#include <memory>

// Builds run dmap (and its AAS pass) in a separate engine process against a
// sandbox save path that holds only the saved map. The editor tails the
// child's log for progress and diagnostics, can cancel it at any time, and
// copies the outputs next to the saved map only after the whole compile
// succeeded, so a failed or cancelled build leaves the previous playable
// output untouched. The sandbox then doubles as an isolated play profile.
namespace oq4editor {

class BuildProcess {
public:
	virtual ~BuildProcess() = default;
	virtual bool Start( const std::vector<std::string> &arguments, std::string &error ) = 0;
	// Returns true while running; otherwise sets the exit code.
	virtual bool Running( int &exitCode ) = 0;
	virtual void Kill() = 0;
};

class BuildFiles {
public:
	virtual ~BuildFiles() = default;
	virtual bool Read( const std::string &path, std::string &text, bool &exists, std::string &error ) = 0;
	virtual bool ReadFrom( const std::string &path, std::size_t offset, std::size_t limit, std::string &text ) = 0;
	virtual bool WriteAndSync( const std::string &path, const std::string &text, std::string &error ) = 0;
	virtual bool Promote( const std::string &from, const std::string &to, std::string &error ) = 0;
	virtual void Remove( const std::string &path ) = 0;
	// Files directly inside a directory whose names start with prefix.
	virtual std::vector<std::string> List( const std::string &directory, const std::string &prefix ) = 0;
	// Absolute OS path for a relative save-path file, for child arguments.
	virtual std::string OSPath( const std::string &relative ) = 0;
};

struct BuildRequest {
	std::string mapPath;   // normalized "maps/....map"
	std::string mapText;   // the saved source compiled by this build
	std::string sandbox = "editor_experimental/build";
	std::vector<std::string> launch; // executable and fixed engine arguments
	bool aas = true;
	unsigned timeoutMs = 3600000;
};

class MapBuild {
public:
	enum class State { Idle, Running, Succeeded, Failed, Cancelled };
	static constexpr std::size_t MaxDiagnostics = 512;
	static constexpr std::size_t MaxOutputBytes = 512u * 1024u * 1024u;
	// After a compiler error the child gets this long to exit on its own
	// before the build stops it (a fatal error can leave a server waiting).
	static constexpr unsigned ErrorExitMs = 15000;

	MapBuild( BuildFiles &files, std::unique_ptr<BuildProcess> process ) : storage( files ), child( std::move( process ) ) {}
	bool Start( const BuildRequest &request, const CompileIndex &index, unsigned nowMs, std::string &error );
	// Advances the job; cheap enough to call every frame.
	void Poll( unsigned nowMs );
	void Cancel();
	State GetState() const { return state; }
	bool Active() const { return state == State::Running; }
	float Progress() const { return progress; }
	const std::string &Stage() const { return stage; }
	const std::string &Failure() const { return failure; }
	const std::vector<Diagnostic> &Diagnostics() const { return diagnostics; }
	const std::vector<std::string> &Outputs() const { return outputs; }
	// Save-path relative root of the play profile (the sandbox).
	std::string SandboxRoot() const { return request.sandbox; }
	std::string SandboxMaps() const;
	std::string LogPath() const;
	unsigned ElapsedMs( unsigned nowMs ) const { return nowMs - startMs; }
	static std::vector<std::string> ChildArguments( const BuildRequest &request, const std::string &sandboxOS );

private:
	void Consume( const std::string &line );
	bool PromoteOutputs( std::string &error );
	void Finish( State result, const std::string &reason );
	void ClearSandbox();

	BuildFiles &storage;
	std::unique_ptr<BuildProcess> child;
	BuildRequest request;
	CompileIndex compileIndex;
	State state = State::Idle;
	std::string stage, failure, partialLine;
	std::vector<Diagnostic> diagnostics;
	std::vector<std::string> outputs;
	std::size_t logOffset = 0;
	unsigned startMs = 0, lastPollMs = 0, lastOutputMs = 0;
	bool sawError = false;
	float progress = 0;
	bool geometryDone = false, leaked = false, aasStarted = false;
};

}
#endif
