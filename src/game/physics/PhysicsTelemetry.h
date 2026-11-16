// openQ4 additions to the SDK game sources; see src/game/LICENSE.
#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

// idlib's Str.h maps snprintf to a compile error for portability; this header
// formats into fixed buffers with the C library directly.
#pragma push_macro( "snprintf" )
#undef snprintf

// Physics telemetry shared by the single-player and multiplayer game trees.
// Recording is off by default and costs one branch per physics evaluation; the
// clock is only read while a capture runs. A capture keeps one sample per game
// frame so a report can give per-frame CPU distributions (median, P95, P99 and
// the worst frame), not just totals, for rigid bodies and articulated figures.
// Counters for the enhanced solver (sleeps, wakes, swept bodies, impulse
// clamps) stay zero in legacy mode, which keeps one report format for both.
namespace OpenQ4Physics {

enum evaluationKind_t {
	EVAL_RIGID_BODY = 0,
	EVAL_ARTICULATED,
	EVAL_KIND_COUNT
};

enum counter_t {
	COUNTER_IMPULSES = 0,		// ApplyImpulse calls on rigid bodies and articulated figures
	COUNTER_IMPULSE_CLAMPS,		// impulses the enhanced guard redistributed
	COUNTER_SLEEPS,				// bodies or figures that came to rest
	COUNTER_WAKES,				// bodies or figures woken from rest
	COUNTER_SWEPT,				// enhanced swept (continuous) collision tests
	COUNTER_ENERGY_CLAMPS,		// enhanced solver steps whose kinetic energy had to be limited
	COUNTER_SOLVER_FALLBACKS,	// enhanced articulated steps that fell back to the legacy LCP
	COUNTER_CLIENT_RIGID,		// rigid-body evaluations of client-side debris (brass, gibs)
	COUNTER_COUNT
};

// a joint whose anchors are further apart than this shows as a stretched limb
const float VISIBLE_JOINT_STRETCH = 8.0f;

struct frameSample_t {
	int			frame;
	float		ms[EVAL_KIND_COUNT];
	int			evaluations[EVAL_KIND_COUNT];
	int			moved[EVAL_KIND_COUNT];
	float		peakLinearSpeed;
	float		peakJointStretch;	// largest joint anchor separation of any figure that moved this frame
	int			counters[COUNTER_COUNT];
};

struct distribution_t {
	int			frames;
	double		totalMs;
	double		meanMs;
	double		p50Ms;
	double		p95Ms;
	double		p99Ms;
	double		maxMs;
	int			maxFrame;
};

inline double Percentile( std::vector<double> values, double fraction ) {
	if ( values.empty() ) {
		return 0.0;
	}
	std::sort( values.begin(), values.end() );
	// nearest-rank: the smallest value at or above the requested fraction of frames
	const double rank = std::ceil( fraction * (double)values.size() );
	size_t index = rank <= 1.0 ? 0 : (size_t)rank - 1;
	if ( index >= values.size() ) {
		index = values.size() - 1;
	}
	return values[index];
}

inline distribution_t Summarize( const std::vector<double> &values, const std::vector<int> &frames ) {
	distribution_t d;
	std::memset( &d, 0, sizeof( d ) );
	d.frames = (int)values.size();
	if ( values.empty() ) {
		return d;
	}
	for ( size_t i = 0; i < values.size(); i++ ) {
		d.totalMs += values[i];
		if ( values[i] > d.maxMs || i == 0 ) {
			d.maxMs = values[i];
			d.maxFrame = i < frames.size() ? frames[i] : 0;
		}
	}
	d.meanMs = d.totalMs / (double)values.size();
	d.p50Ms = Percentile( values, 0.50 );
	d.p95Ms = Percentile( values, 0.95 );
	d.p99Ms = Percentile( values, 0.99 );
	return d;
}

class Telemetry {
public:
	static const int MAX_SAMPLES = 1 << 17;	// about 36 minutes at 60 Hz

				Telemetry( void ) { Clear(); }

	bool		IsRecording( void ) const { return recording; }

	void		Clear( void ) {
		recording = false;
		frameOpen = false;
		std::memset( &current, 0, sizeof( current ) );
		samples.clear();
		label[0] = '\0';
		peakImpulseDeltaV = 0.0f;
		maxImpulse = 0.0f;
		fallbackMs = 0.0;
	}

	void		Start( const char *name ) {
		Clear();
		if ( name ) {
			std::snprintf( label, sizeof( label ), "%s", name );
		}
		recording = true;
	}

	void		Stop( void ) {
		CloseFrame();
		recording = false;
	}

	// called once per game frame (server/SP frame or predicted client frame)
	void		FrameTick( int frame ) {
		if ( !recording ) {
			return;
		}
		if ( frameOpen && current.frame == frame ) {
			return;
		}
		CloseFrame();
		std::memset( &current, 0, sizeof( current ) );
		current.frame = frame;
		frameOpen = true;
	}

	void		AddEvaluation( evaluationKind_t kind, double ms, bool moved ) {
		if ( !recording || kind < 0 || kind >= EVAL_KIND_COUNT ) {
			return;
		}
		if ( !frameOpen ) {
			FrameTick( current.frame + 1 );
		}
		current.ms[kind] += (float)ms;
		current.evaluations[kind]++;
		if ( moved ) {
			current.moved[kind]++;
		}
	}

	void		NoteLinearSpeed( float speed ) {
		if ( recording && frameOpen && speed > current.peakLinearSpeed ) {
			current.peakLinearSpeed = speed;
		}
	}

	void		NoteJointStretch( float stretch ) {
		if ( recording && frameOpen && stretch > current.peakJointStretch ) {
			current.peakJointStretch = stretch;
		}
	}

	void		NoteCounter( counter_t counter, int amount = 1 ) {
		if ( recording && frameOpen && counter >= 0 && counter < COUNTER_COUNT ) {
			current.counters[counter] += amount;
		}
	}

	// impulse: magnitude delivered; deltaV: velocity change of the struck body
	void		NoteImpulse( float impulse, float deltaV, bool clamped ) {
		if ( !recording ) {
			return;
		}
		NoteCounter( COUNTER_IMPULSES );
		if ( clamped ) {
			NoteCounter( COUNTER_IMPULSE_CLAMPS );
		}
		if ( impulse > maxImpulse ) {
			maxImpulse = impulse;
		}
		if ( deltaV > peakImpulseDeltaV ) {
			peakImpulseDeltaV = deltaV;
		}
	}

	// time the enhanced articulated step spent in an exact LCP solve after its
	// iterations did not settle (already part of that evaluation's time)
	void		NoteFallbackMs( double ms ) {
		if ( recording ) {
			fallbackMs += ms;
		}
	}

	const std::vector<frameSample_t> &Samples( void ) const { return samples; }
	const char *Label( void ) const { return label; }

	distribution_t	Distribution( int kind ) const {	// kind < 0: both kinds together
		std::vector<double> values;
		std::vector<int> frames;
		values.reserve( samples.size() );
		frames.reserve( samples.size() );
		for ( size_t i = 0; i < samples.size(); i++ ) {
			double ms = 0.0;
			for ( int k = 0; k < EVAL_KIND_COUNT; k++ ) {
				if ( kind < 0 || kind == k ) {
					ms += samples[i].ms[k];
				}
			}
			values.push_back( ms );
			frames.push_back( samples[i].frame );
		}
		return Summarize( values, frames );
	}

	// One machine-readable line; tools/tests/physics_workloads.py parses it from the log.
	template< typename printFunc_t >
	void		Report( printFunc_t print, const char *mode ) const {
		const distribution_t all = Distribution( -1 );
		const distribution_t rb = Distribution( EVAL_RIGID_BODY );
		const distribution_t af = Distribution( EVAL_ARTICULATED );
		long long evaluations[EVAL_KIND_COUNT] = { 0, 0 };
		long long moved[EVAL_KIND_COUNT] = { 0, 0 };
		long long counters[COUNTER_COUNT];
		float peakSpeed = 0.0f;
		float peakStretch = 0.0f;
		int stretchedFrames = 0;
		std::memset( counters, 0, sizeof( counters ) );
		for ( size_t i = 0; i < samples.size(); i++ ) {
			for ( int k = 0; k < EVAL_KIND_COUNT; k++ ) {
				evaluations[k] += samples[i].evaluations[k];
				moved[k] += samples[i].moved[k];
			}
			for ( int c = 0; c < COUNTER_COUNT; c++ ) {
				counters[c] += samples[i].counters[c];
			}
			peakSpeed = (std::max)( peakSpeed, samples[i].peakLinearSpeed );
			peakStretch = (std::max)( peakStretch, samples[i].peakJointStretch );
			if ( samples[i].peakJointStretch > VISIBLE_JOINT_STRETCH ) {
				stretchedFrames++;
			}
		}
		char line[2048];
		std::snprintf( line, sizeof( line ),
			"PHYSICS_STATS {\"label\":\"%s\",\"mode\":\"%s\",\"frames\":%d,"
			"\"all\":{\"total_ms\":%.4f,\"mean_ms\":%.5f,\"p50_ms\":%.5f,\"p95_ms\":%.5f,\"p99_ms\":%.5f,\"max_ms\":%.5f,\"max_frame\":%d},"
			"\"rigid\":{\"total_ms\":%.4f,\"p50_ms\":%.5f,\"p95_ms\":%.5f,\"p99_ms\":%.5f,\"max_ms\":%.5f,\"evaluations\":%lld,\"moved\":%lld},"
			"\"af\":{\"total_ms\":%.4f,\"p50_ms\":%.5f,\"p95_ms\":%.5f,\"p99_ms\":%.5f,\"max_ms\":%.5f,\"evaluations\":%lld,\"moved\":%lld},"
			"\"peak_speed\":%.2f,\"impulses\":%lld,\"impulse_clamps\":%lld,\"max_impulse\":%.1f,\"peak_impulse_dv\":%.2f,"
			"\"sleeps\":%lld,\"wakes\":%lld,\"swept\":%lld,\"energy_clamps\":%lld,\"solver_fallbacks\":%lld,\"client_rigid\":%lld,"
			"\"fallback_ms\":%.4f,\"peak_joint_stretch\":%.2f,\"stretched_frames\":%d}\n",
			label, mode ? mode : "", all.frames,
			all.totalMs, all.meanMs, all.p50Ms, all.p95Ms, all.p99Ms, all.maxMs, all.maxFrame,
			rb.totalMs, rb.p50Ms, rb.p95Ms, rb.p99Ms, rb.maxMs, evaluations[EVAL_RIGID_BODY], moved[EVAL_RIGID_BODY],
			af.totalMs, af.p50Ms, af.p95Ms, af.p99Ms, af.maxMs, evaluations[EVAL_ARTICULATED], moved[EVAL_ARTICULATED],
			peakSpeed, counters[COUNTER_IMPULSES], counters[COUNTER_IMPULSE_CLAMPS], maxImpulse, peakImpulseDeltaV,
			counters[COUNTER_SLEEPS], counters[COUNTER_WAKES], counters[COUNTER_SWEPT], counters[COUNTER_ENERGY_CLAMPS],
			counters[COUNTER_SOLVER_FALLBACKS], counters[COUNTER_CLIENT_RIGID], fallbackMs, peakStretch, stretchedFrames );
		print( line );
	}

	// Every frame's physics time, in capture order: PHYSICS_FRAMES <label> <mode>
	// <first sample> <count>, then one value per frame (ms, both kinds together).
	// A deterministic workload does the same work in the same frame of every
	// run, so the smallest value a frame shows over repeated runs is what that
	// frame costs without the machine's noise.
	template< typename printFunc_t >
	void		ReportFrames( printFunc_t print, const char *mode ) const {
		const size_t perLine = 32;
		for ( size_t first = 0; first < samples.size(); first += perLine ) {
			const size_t count = (std::min)( perLine, samples.size() - first );
			char line[1024];
			int length = std::snprintf( line, sizeof( line ), "PHYSICS_FRAMES %s %s %d %d", label, mode ? mode : "", (int)first, (int)count );
			for ( size_t i = 0; i < count && length < (int)sizeof( line ) - 16; i++ ) {
				const frameSample_t &sample = samples[first + i];
				length += std::snprintf( line + length, sizeof( line ) - length, " %.4f", sample.ms[EVAL_RIGID_BODY] + sample.ms[EVAL_ARTICULATED] );
			}
			std::snprintf( line + length, sizeof( line ) - length, "\n" );
			print( line );
		}
	}

private:
	void		CloseFrame( void ) {
		if ( frameOpen && samples.size() < (size_t)MAX_SAMPLES ) {
			samples.push_back( current );
		}
		frameOpen = false;
	}

	bool						recording;
	bool						frameOpen;
	frameSample_t				current;
	std::vector<frameSample_t>	samples;
	char						label[64];
	float						peakImpulseDeltaV;
	float						maxImpulse;
	double						fallbackMs;
};

// One instance per game module: the SP and MP libraries each record their own frames.
inline Telemetry &GetTelemetry( void ) {
	static Telemetry telemetry;
	return telemetry;
}

// Times one physics evaluation while a capture runs.
class ScopedEvaluation {
public:
	explicit ScopedEvaluation( evaluationKind_t kind ) : kind( kind ), moved( false ), timing( GetTelemetry().IsRecording() ) {
		if ( timing ) {
			start = std::chrono::steady_clock::now();
		}
	}
	~ScopedEvaluation( void ) {
		if ( timing ) {
			const std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - start;
			GetTelemetry().AddEvaluation( kind, elapsed.count(), moved );
		}
	}
	bool	Moved( bool value ) { moved = value; return value; }
	// time so far, or 0 when no capture is recording
	double	ElapsedMs( void ) const {
		if ( !timing ) {
			return 0.0;
		}
		const std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - start;
		return elapsed.count();
	}

private:
	evaluationKind_t						kind;
	bool									moved;
	bool									timing;
	std::chrono::steady_clock::time_point	start;
};

// 64-bit FNV-1a over raw state bytes; physicsDigest prints it so two runs of
// the same workload (or a legacy run against the pre-change binary) can be
// compared exactly.
class StateDigest {
public:
			StateDigest( void ) : hash( 1469598103934665603ULL ), count( 0 ) {}
	void	Add( const void *data, size_t size ) {
		const unsigned char *bytes = (const unsigned char *)data;
		for ( size_t i = 0; i < size; i++ ) {
			hash ^= bytes[i];
			hash *= 1099511628211ULL;
		}
	}
	void	AddFloats( const float *values, int n ) {
		for ( int i = 0; i < n; i++ ) {
			float v = values[i];
			if ( v == 0.0f ) {
				v = 0.0f;	// fold -0 into +0
			}
			Add( &v, sizeof( v ) );
		}
	}
	void	CountEntity( void ) { count++; }
	uint64_t	Hash( void ) const { return hash; }
	int		Count( void ) const { return count; }

private:
	uint64_t	hash;
	int			count;
};

} // namespace OpenQ4Physics

#pragma pop_macro( "snprintf" )
