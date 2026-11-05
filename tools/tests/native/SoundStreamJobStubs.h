// Copyright (C) 2026 openQ4 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Stand-ins for the engine types the production idSoundOggStream uses
// (AL_SoundSample.h/.cpp extracted by audio_streaming.py and
// audio_stream_budget.py). The job system defers every accepted list until the
// test runs it, so a block is observably pending, and records cancellation.
#ifndef OPENQ4_TEST_SOUND_STREAM_JOB_STUBS_H
#define OPENQ4_TEST_SOUND_STREAM_JOB_STUBS_H

#include <algorithm>
#include <cstddef>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

enum { CVAR_SOUND = 1, CVAR_BOOL = 2, CVAR_ARCHIVE = 4, CVAR_INTEGER = 8 };
struct idCVar {
	std::string value;
	idCVar( const char*, const char* initial, int, const char*, float = 0, float = 0 ) : value( initial ) {}
	bool GetBool() const { return std::atoi( value.c_str() ) != 0; }
	int GetInteger() const { return std::atoi( value.c_str() ); }
	void SetBool( bool on ) { value = on ? "1" : "0"; }
	void SetInteger( int v ) { value = std::to_string( v ); }
};
struct idStr {
	std::string text;
	idStr() = default;
	idStr( const char* s ) : text( s ? s : "" ) {}
	idStr& operator=( const char* s ) { text = s ? s : ""; return *this; }
	const char* c_str() const { return text.c_str(); }
	operator const char*() const { return text.c_str(); }
	int Length() const { return static_cast<int>( text.size() ); }
};
template<class T> struct idList : std::vector<T> {
	int Num() const { return static_cast<int>( this->size() ); }
	const T* Ptr() const { return this->data(); }
	void Append( const T& item ) { this->push_back( item ); }
	void RemoveIndex( int index ) { this->erase( this->begin() + index ); }
	T& operator[]( int index ) { return std::vector<T>::operator[]( static_cast<std::size_t>( index ) ); }
	const T& operator[]( int index ) const { return std::vector<T>::operator[]( static_cast<std::size_t>( index ) ); }
};
inline const char* va( const char* format, ... ) {
	static char buffer[1024];
	va_list args;
	va_start( args, format );
	std::vsnprintf( buffer, sizeof( buffer ), format, args );
	va_end( args );
	return buffer;
}
static int stubMilliseconds = 1000;
inline int Sys_Milliseconds() { return stubMilliseconds; }

struct idJobContext {
	void* data;
	bool cancelled;
	bool IsCancellationRequested() const { return cancelled; }
};
typedef void ( *idJobFunction )( const idJobContext& context );
enum class idJobPriority { LOW, NORMAL, HIGH };
enum class idJobSubmitResult { ACCEPTED, EXECUTED_SYNCHRONOUSLY, QUEUE_FULL, SYSTEM_UNAVAILABLE };
struct StubJob {
	idJobFunction function = nullptr;
	void* data = nullptr;
	bool submitted = false, ran = false, cancelled = false, finished = false;
};
// Lists the stub accepted and has not run, oldest first.
static std::vector<std::shared_ptr<StubJob>> stubPendingJobs;
static int stubJobsRun = 0, stubJobsCancelled = 0;
static bool stubJobsInitialized = true, stubJobsSynchronous = false, stubQueueFull = false;
inline void StubRunJob( const std::shared_ptr<StubJob>& job ) {
	if( job->finished ) return;
	// A list cancelled before it starts never runs, as in ParallelJobSystem.
	if( !job->cancelled ) {
		const idJobContext context = { job->data, false };
		job->function( context );
		job->ran = true;
		++stubJobsRun;
	} else {
		++stubJobsCancelled;
	}
	job->finished = true;
	stubPendingJobs.erase( std::remove( stubPendingJobs.begin(), stubPendingJobs.end(), job ), stubPendingJobs.end() );
}
// Runs every accepted list, as the workers would between two main-thread frames.
inline int StubRunPendingJobs() {
	int count = 0;
	while( !stubPendingJobs.empty() ) { StubRunJob( stubPendingJobs.front() ); ++count; }
	return count;
}
class idJobList {
public:
	explicit idJobList( std::shared_ptr<StubJob> state ) : job( std::move( state ) ) {}
	~idJobList() { Cancel(); Wait(); }
	bool AddJob( idJobFunction function, void* data ) { job->function = function; job->data = data; return function != nullptr; }
	idJobSubmitResult Submit() {
		if( stubQueueFull ) return idJobSubmitResult::QUEUE_FULL;
		job->submitted = true;
		if( stubJobsSynchronous ) { StubRunJob( job ); return idJobSubmitResult::EXECUTED_SYNCHRONOUSLY; }
		stubPendingJobs.push_back( job );
		return idJobSubmitResult::ACCEPTED;
	}
	bool Cancel() { if( job->finished ) return false; job->cancelled = true; return true; }
	bool Wait() { if( job->submitted ) StubRunJob( job ); return job->ran; }
	bool TryWait() const { return !job->submitted || job->finished; }
private:
	std::shared_ptr<StubJob> job;
};
struct idJobSystem {
	bool IsInitialized() const { return stubJobsInitialized; }
	bool IsSynchronous() const { return stubJobsSynchronous; }
	std::unique_ptr<idJobList> CreateJobList( const char*, std::size_t, std::size_t, idJobPriority ) {
		return std::unique_ptr<idJobList>( new idJobList( std::make_shared<StubJob>() ) );
	}
};
static idJobSystem jobSystem;
#endif
