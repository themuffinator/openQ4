// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "StallSampler.h"

#if defined( _WIN32 ) && defined( _M_X64 )

#include <dbghelp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

constexpr int MaxFrames = 64;
constexpr size_t StackCopyBytes = 1u << 20;
constexpr size_t MaxSamplesPerStall = 1024;
constexpr size_t MaxStalls = 512;
constexpr double SampleIntervalMsec = 4.0;

struct Sample {
	int count = 0;
	DWORD64 pc[MaxFrames];
};

struct Stall {
	double start = 0, end = 0;
	std::vector<Sample> samples;
};

double NowMsec() {
	return std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now().time_since_epoch() ).count();
}

struct Sampler {
	std::mutex mutex;
	std::thread worker;
	std::atomic<bool> stop{ false };
	std::atomic<double> lastPresent{ 0 };
	HANDLE thread = nullptr;
	DWORD threadId = 0;
	DWORD64 stackLow = 0, stackHigh = 0;
	double threshold = 50, begin = 0;
	std::vector<Stall> stalls;
	Stall current;
	bool inStall = false;
	std::vector<unsigned char> copy;
};
Sampler *sampler = nullptr;

void FinishStallLocked( Sampler &s, double end ) {
	if ( !s.inStall ) {
		return;
	}
	s.current.end = end;
	if ( s.stalls.size() < MaxStalls ) {
		s.stalls.push_back( std::move( s.current ) );
	} else {
		auto shortest = std::min_element( s.stalls.begin(), s.stalls.end(), []( const Stall &a, const Stall &b ) {
			return a.end - a.start < b.end - b.start;
		} );
		if ( shortest->end - shortest->start < s.current.end - s.current.start ) {
			*shortest = std::move( s.current );
		}
	}
	s.current = Stall();
	s.inStall = false;
}

// Unwinds a copy of the sampled thread's stack taken while it was suspended.
// Nothing here may take a lock the sampled thread could hold, so the thread is
// resumed before any unwind-table lookup runs.
bool Capture( Sampler &s, Sample &out ) {
	if ( SuspendThread( s.thread ) == static_cast<DWORD>( -1 ) ) {
		return false;
	}
	CONTEXT ctx;
	std::memset( &ctx, 0, sizeof( ctx ) );
	ctx.ContextFlags = CONTEXT_FULL;
	const bool haveContext = GetThreadContext( s.thread, &ctx ) != FALSE;
	size_t size = 0;
	const DWORD64 rsp = ctx.Rsp;
	if ( haveContext && rsp >= s.stackLow && rsp < s.stackHigh ) {
		size = static_cast<size_t>( ( std::min<DWORD64> )( s.stackHigh - rsp, s.copy.size() ) );
		std::memcpy( s.copy.data(), reinterpret_cast<const void *>( rsp ), size );
	}
	ResumeThread( s.thread );
	if ( !haveContext || size < 8 ) {
		return false;
	}

	const DWORD64 lo = rsp, hi = rsp + size, base = reinterpret_cast<DWORD64>( s.copy.data() );
	const auto rebase = [&]( DWORD64 &r ) {
		if ( r >= lo && r < hi ) {
			r = r - lo + base;
		}
	};
	const auto rebaseAll = [&]() {
		rebase( ctx.Rbp ); rebase( ctx.Rbx ); rebase( ctx.Rsi ); rebase( ctx.Rdi );
		rebase( ctx.R12 ); rebase( ctx.R13 ); rebase( ctx.R14 ); rebase( ctx.R15 );
	};
	ctx.Rsp = base;
	rebaseAll();
	out.count = 0;
	while ( out.count < MaxFrames ) {
		const DWORD64 pc = ctx.Rip;
		if ( pc == 0 ) {
			break;
		}
		out.pc[out.count++] = pc;
		DWORD64 imageBase = 0;
		PRUNTIME_FUNCTION entry = RtlLookupFunctionEntry( pc, &imageBase, nullptr );
		if ( entry != nullptr ) {
			PVOID handlerData = nullptr;
			DWORD64 establisher = 0;
			RtlVirtualUnwind( UNW_FLAG_NHANDLER, imageBase, pc, entry, &ctx, &handlerData, &establisher, nullptr );
		} else {
			if ( ctx.Rsp < base || ctx.Rsp + 8 > base + size ) {
				break;
			}
			ctx.Rip = *reinterpret_cast<const DWORD64 *>( ctx.Rsp );
			ctx.Rsp += 8;
		}
		// Values restored from the copied stack are original-stack addresses.
		rebaseAll();
		if ( ctx.Rsp < base || ctx.Rsp >= base + size ) {
			break;
		}
	}
	return out.count > 0;
}

void Run( Sampler *s ) {
	while ( !s->stop.load( std::memory_order_acquire ) ) {
		Sleep( 1 );
		const double last = s->lastPresent.load( std::memory_order_acquire );
		const double now = NowMsec();
		if ( now - last < s->threshold ) {
			continue;
		}
		{
			std::lock_guard<std::mutex> lock( s->mutex );
			if ( s->inStall && !s->current.samples.empty() && now - s->current.end < SampleIntervalMsec ) {
				continue;
			}
		}
		Sample sample;
		if ( !Capture( *s, sample ) ) {
			continue;
		}
		std::lock_guard<std::mutex> lock( s->mutex );
		// A frame presented while this sample was taken: it belongs to no stall.
		if ( s->lastPresent.load( std::memory_order_acquire ) != last ) {
			continue;
		}
		if ( !s->inStall ) {
			s->current = Stall();
			s->current.start = last;
			s->inStall = true;
		}
		s->current.end = now;
		if ( s->current.samples.size() < MaxSamplesPerStall ) {
			s->current.samples.push_back( sample );
		}
	}
}

void StopSampler() {
	if ( sampler == nullptr ) {
		return;
	}
	sampler->stop.store( true, std::memory_order_release );
	if ( sampler->worker.joinable() ) {
		sampler->worker.join();
	}
	if ( sampler->thread != nullptr ) {
		CloseHandle( sampler->thread );
		sampler->thread = nullptr;
	}
}

// dbghelp is loaded on demand and only ever used from the reporting thread.
struct Symbols {
	typedef BOOL ( WINAPI *SymInitialize_t )( HANDLE, PCSTR, BOOL );
	typedef DWORD ( WINAPI *SymSetOptions_t )( DWORD );
	typedef BOOL ( WINAPI *SymFromAddr_t )( HANDLE, DWORD64, PDWORD64, PSYMBOL_INFO );
	SymFromAddr_t fromAddr = nullptr;
	bool tried = false, ready = false;
	std::unordered_map<DWORD64, std::pair<DWORD64, std::string>> cache;

	bool Init() {
		if ( tried ) {
			return ready;
		}
		tried = true;
		HMODULE dll = LoadLibraryA( "dbghelp.dll" );
		if ( dll == nullptr ) {
			return false;
		}
		auto init = reinterpret_cast<SymInitialize_t>( GetProcAddress( dll, "SymInitialize" ) );
		auto options = reinterpret_cast<SymSetOptions_t>( GetProcAddress( dll, "SymSetOptions" ) );
		fromAddr = reinterpret_cast<SymFromAddr_t>( GetProcAddress( dll, "SymFromAddr" ) );
		if ( init == nullptr || options == nullptr || fromAddr == nullptr ) {
			return false;
		}
		char exe[MAX_PATH] = {};
		GetModuleFileNameA( nullptr, exe, MAX_PATH );
		std::string dir = exe;
		dir = dir.substr( 0, dir.find_last_of( "\\/" ) );
		const std::string path = dir + ";" + dir + "\\baseoq4";
		options( SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_NO_PROMPTS | SYMOPT_FAIL_CRITICAL_ERRORS );
		ready = init( GetCurrentProcess(), path.c_str(), TRUE ) != FALSE;
		return ready;
	}

	// The function containing pc: its start address (the aggregation key) and
	// "module!name".
	const std::pair<DWORD64, std::string> &Resolve( DWORD64 pc ) {
		auto found = cache.find( pc );
		if ( found != cache.end() ) {
			return found->second;
		}
		char module[MAX_PATH] = "?";
		HMODULE handle = nullptr;
		if ( GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCSTR>( pc ), &handle ) && handle != nullptr ) {
			char full[MAX_PATH] = {};
			GetModuleFileNameA( handle, full, MAX_PATH );
			const char *slash = std::strrchr( full, '\\' );
			idStr::snPrintf( module, sizeof( module ), "%s", slash != nullptr ? slash + 1 : full );
			char *dot = std::strrchr( module, '.' );
			if ( dot != nullptr ) {
				*dot = '\0';
			}
		}
		std::pair<DWORD64, std::string> result( pc, std::string( module ) + "!" );
		alignas( SYMBOL_INFO ) unsigned char buffer[sizeof( SYMBOL_INFO ) + 512];
		SYMBOL_INFO *symbol = reinterpret_cast<SYMBOL_INFO *>( buffer );
		std::memset( buffer, 0, sizeof( buffer ) );
		symbol->SizeOfStruct = sizeof( SYMBOL_INFO );
		symbol->MaxNameLen = 511;
		DWORD64 displacement = 0;
		if ( ready && fromAddr( GetCurrentProcess(), pc, &displacement, symbol ) ) {
			result.first = symbol->Address;
			std::string name( symbol->Name );
			if ( name.size() > 72 ) {
				name = name.substr( 0, 72 ) + "~";
			}
			result.second += name;
		} else {
			char hex[32];
			idStr::snPrintf( hex, sizeof( hex ), "+%llx", static_cast<unsigned long long>( handle != nullptr
				? pc - reinterpret_cast<DWORD64>( handle ) : pc ) );
			result.second += hex;
		}
		return cache.emplace( pc, std::move( result ) ).first->second;
	}
};
Symbols symbols;

} // namespace

bool Sys_StallSampler_Supported( void ) {
	return true;
}

void Sys_StallSampler_Begin( double thresholdMsec ) {
	StopSampler();
	delete sampler;
	sampler = new Sampler();
	Sampler &s = *sampler;
	s.threshold = ( std::max )( 5.0, thresholdMsec );
	s.begin = NowMsec();
	s.lastPresent.store( s.begin );
	s.copy.resize( StackCopyBytes );
	const NT_TIB *tib = reinterpret_cast<const NT_TIB *>( NtCurrentTeb() );
	s.stackLow = reinterpret_cast<DWORD64>( tib->StackLimit );
	s.stackHigh = reinterpret_cast<DWORD64>( tib->StackBase );
	s.threadId = GetCurrentThreadId();
	if ( !DuplicateHandle( GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &s.thread,
			THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0 ) ) {
		s.thread = nullptr;
		return;
	}
	s.worker = std::thread( Run, sampler );
}

void Sys_StallSampler_Presented( void ) {
	if ( sampler == nullptr || sampler->threadId != GetCurrentThreadId() ) {
		return;
	}
	const double now = NowMsec();
	std::lock_guard<std::mutex> lock( sampler->mutex );
	FinishStallLocked( *sampler, now );
	sampler->lastPresent.store( now, std::memory_order_release );
}

void Sys_StallSampler_Report( int maxStalls, const char *focus, void ( *emit )( const char *line, void *context ), void *context ) {
	if ( sampler == nullptr ) {
		return;
	}
	StopSampler();
	FinishStallLocked( *sampler, NowMsec() );
	std::vector<Stall> stalls = std::move( sampler->stalls );
	const double begin = sampler->begin;
	delete sampler;
	sampler = nullptr;
	if ( stalls.empty() || emit == nullptr ) {
		return;
	}
	std::sort( stalls.begin(), stalls.end(), []( const Stall &a, const Stall &b ) {
		return a.end - a.start > b.end - b.start;
	} );
	symbols.Init();
	{
		// Across every stall: the functions most samples were inside, which
		// names the work behind many short stalls that no single one shows.
		// With a focus, only samples inside a function whose name contains it
		// count, and the list goes deeper.
		const bool focused = focus != nullptr && focus[0] != '\0';
		std::unordered_map<DWORD64, int> inclusive;
		size_t total = 0;
		std::vector<DWORD64> seen;
		for ( const Stall &stall : stalls ) {
			for ( const Sample &sample : stall.samples ) {
				if ( focused ) {
					bool inside = false;
					for ( int i = 0; i < sample.count && !inside; ++i ) {
						inside = symbols.Resolve( sample.pc[i] ).second.find( focus ) != std::string::npos;
					}
					if ( !inside ) {
						continue;
					}
				}
				++total;
				seen.clear();
				for ( int i = 0; i < sample.count; ++i ) {
					const DWORD64 fn = symbols.Resolve( sample.pc[i] ).first;
					if ( std::find( seen.begin(), seen.end(), fn ) == seen.end() ) {
						seen.push_back( fn );
						inclusive[fn]++;
					}
				}
			}
		}
		std::vector<std::pair<int, DWORD64>> ranked;
		for ( auto &entry : inclusive ) {
			// Everything a stall sits under (the frame loop, the load itself)
			// says nothing; keep what at most half the samples share.
			if ( focused || entry.second * 2 <= static_cast<int>( total ) ) {
				ranked.emplace_back( entry.second, entry.first );
			}
		}
		std::sort( ranked.rbegin(), ranked.rend() );
		std::string line = std::string( focused ? "LOADING_STALL_FOCUS focus=" : "LOADING_STALL_HOT" ) +
			( focused ? focus : "" ) + " samples=" + std::to_string( total ) + " top=";
		for ( size_t i = 0; i < ranked.size() && i < ( focused ? 48u : 24u ); ++i ) {
			char share[16];
			idStr::snPrintf( share, sizeof( share ), " %d%%", total ? ranked[i].first * 100 / static_cast<int>( total ) : 0 );
			line += ( i ? ", " : "" ) + symbols.Resolve( ranked[i].second ).second + share;
		}
		line += "\n";
		emit( line.c_str(), context );
	}
	if ( static_cast<int>( stalls.size() ) > maxStalls ) {
		stalls.resize( maxStalls );
	}
	for ( const Stall &stall : stalls ) {
		const size_t n = stall.samples.size();
		// Inclusive share and mean depth from the root of every function.
		struct Count { int samples = 0; double depth = 0; const std::string *name = nullptr; };
		std::unordered_map<DWORD64, Count> counts;
		std::unordered_map<DWORD64, int> leaves;
		for ( const Sample &sample : stall.samples ) {
			std::vector<DWORD64> seen;
			for ( int i = 0; i < sample.count; ++i ) {
				const auto &fn = symbols.Resolve( sample.pc[i] );
				if ( i == 0 ) {
					leaves[fn.first]++;
				}
				if ( std::find( seen.begin(), seen.end(), fn.first ) != seen.end() ) {
					continue;
				}
				seen.push_back( fn.first );
				Count &c = counts[fn.first];
				c.samples++;
				c.depth += sample.count - i;
				c.name = &fn.second;
			}
		}
		std::vector<Count> path;
		for ( auto &entry : counts ) {
			if ( entry.second.samples * 2 >= static_cast<int>( n ) ) {
				Count c = entry.second;
				c.depth /= c.samples;
				path.push_back( c );
			}
		}
		std::sort( path.begin(), path.end(), []( const Count &a, const Count &b ) { return a.depth < b.depth; } );
		std::string line;
		char head[160];
		idStr::snPrintf( head, sizeof( head ), "LOADING_STALL msec=%.1f at=+%.0f samples=%zu path=",
			stall.end - stall.start, stall.start - begin, n );
		line = head;
		const size_t keep = 9;
		for ( size_t i = path.size() > keep ? path.size() - keep : 0; i < path.size(); ++i ) {
			line += *path[i].name;
			if ( i + 1 < path.size() ) {
				line += " > ";
			}
		}
		std::vector<std::pair<int, DWORD64>> leafList;
		for ( auto &entry : leaves ) {
			leafList.emplace_back( entry.second, entry.first );
		}
		std::sort( leafList.rbegin(), leafList.rend() );
		line += " | leaf=";
		for ( size_t i = 0; i < leafList.size() && i < 2; ++i ) {
			char share[16];
			idStr::snPrintf( share, sizeof( share ), " %d%%", n ? leafList[i].first * 100 / static_cast<int>( n ) : 0 );
			line += ( i ? ", " : "" ) + symbols.Resolve( leafList[i].second ).second + share;
		}
		line += "\n";
		emit( line.c_str(), context );
	}
}

#else

bool Sys_StallSampler_Supported( void ) {
	return false;
}
void Sys_StallSampler_Begin( double ) {
}
void Sys_StallSampler_Presented( void ) {
}
void Sys_StallSampler_Report( int, const char *, void ( * )( const char *, void * ), void * ) {
}

#endif
