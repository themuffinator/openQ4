// Native qualification of the enhanced physics kernels (src/game/physics/EnhancedSolver.h)
// and the telemetry header. The rigid-body scenes step bodies the way the game glue does:
// semi-implicit integration, contacts from a translation sweep along the motion with a
// speculative margin, warm-started sequential impulses, pseudo-velocity penetration
// recovery and quiet-frame sleeping. The world is a set of finite boxes (faces with
// extents) so thin walls and stacks can be expressed.
#include "src/game/physics/EnhancedSolver.h"
#include "src/game/physics/PhysicsTelemetry.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

using namespace OpenQ4Physics;

static int failures = 0;
#define CHECK( cond, ... ) do { if ( !( cond ) ) { std::printf( "FAIL %s:%d: ", __FILE__, __LINE__ ); std::printf( __VA_ARGS__ ); std::printf( "\n" ); failures++; } } while ( 0 )

// ---------------------------------------------------------------------------
// standard (column-vector) 3x3 helpers for the test world
// ---------------------------------------------------------------------------
struct M3 {
	float m[3][3];
	static M3 I() { M3 r{}; r.m[0][0] = r.m[1][1] = r.m[2][2] = 1.0f; return r; }
	Vec3 operator*( const Vec3 &v ) const {
		return Vec3( m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
			m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
			m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z );
	}
	M3 operator*( const M3 &o ) const {
		M3 r{};
		for ( int i = 0; i < 3; i++ ) for ( int j = 0; j < 3; j++ ) for ( int k = 0; k < 3; k++ ) r.m[i][j] += m[i][k] * o.m[k][j];
		return r;
	}
	M3 T() const { M3 r{}; for ( int i = 0; i < 3; i++ ) for ( int j = 0; j < 3; j++ ) r.m[i][j] = m[j][i]; return r; }
	Mat3 ToSolver() const {		// symmetric matrices only: rows == columns
		Mat3 s;
		s.row[0] = Vec3( m[0][0], m[0][1], m[0][2] );
		s.row[1] = Vec3( m[1][0], m[1][1], m[1][2] );
		s.row[2] = Vec3( m[2][0], m[2][1], m[2][2] );
		return s;
	}
};

static M3 AxisAngle( Vec3 axis, float angle ) {
	const float len = axis.Normalize();
	if ( len < 1e-9f || std::fabs( angle ) < 1e-12f ) return M3::I();
	const float c = std::cos( angle ), s = std::sin( angle ), t = 1.0f - c;
	M3 r;
	r.m[0][0] = t * axis.x * axis.x + c;          r.m[0][1] = t * axis.x * axis.y - s * axis.z; r.m[0][2] = t * axis.x * axis.z + s * axis.y;
	r.m[1][0] = t * axis.x * axis.y + s * axis.z; r.m[1][1] = t * axis.y * axis.y + c;          r.m[1][2] = t * axis.y * axis.z - s * axis.x;
	r.m[2][0] = t * axis.x * axis.z - s * axis.y; r.m[2][1] = t * axis.y * axis.z + s * axis.x; r.m[2][2] = t * axis.z * axis.z + c;
	return r;
}

static void Orthonormalize( M3 &r ) {
	Vec3 c0( r.m[0][0], r.m[1][0], r.m[2][0] ), c1( r.m[0][1], r.m[1][1], r.m[2][1] );
	c0.Normalize();
	c1 = c1 - c0 * c0.Dot( c1 );
	c1.Normalize();
	const Vec3 c2 = c0.Cross( c1 );
	r.m[0][0] = c0.x; r.m[1][0] = c0.y; r.m[2][0] = c0.z;
	r.m[0][1] = c1.x; r.m[1][1] = c1.y; r.m[2][1] = c1.z;
	r.m[0][2] = c2.x; r.m[1][2] = c2.y; r.m[2][2] = c2.z;
}

// ---------------------------------------------------------------------------
// test world: axis-aligned static boxes and dynamic boxes
// ---------------------------------------------------------------------------
struct StaticBox { Vec3 mins, maxs; };

struct Body {
	Vec3 half;					// box half extents
	float mass;
	Vec3 x, v, w;				// centre (= centre of mass), velocities
	M3 R;						// columns are local axes in world space
	int quiet = 0;
	bool asleep = false;
	std::vector<cachedContact_t> cache;

	M3 InvInertiaWorld() const {
		const float a = 2 * half.x, b = 2 * half.y, c = 2 * half.z;
		M3 inv{};
		inv.m[0][0] = 12.0f / ( mass * ( b * b + c * c ) );
		inv.m[1][1] = 12.0f / ( mass * ( a * a + c * c ) );
		inv.m[2][2] = 12.0f / ( mass * ( a * a + b * b ) );
		return R * inv * R.T();
	}
	M3 InertiaWorld() const {
		const float a = 2 * half.x, b = 2 * half.y, c = 2 * half.z;
		M3 in{};
		in.m[0][0] = mass * ( b * b + c * c ) / 12.0f;
		in.m[1][1] = mass * ( a * a + c * c ) / 12.0f;
		in.m[2][2] = mass * ( a * a + b * b ) / 12.0f;
		return R * in * R.T();
	}
	void Vertices( float out[24] ) const {
		int k = 0;
		for ( int sx = -1; sx <= 1; sx += 2 ) for ( int sy = -1; sy <= 1; sy += 2 ) for ( int sz = -1; sz <= 1; sz += 2 ) {
			const Vec3 p = x + R * Vec3( sx * half.x, sy * half.y, sz * half.z );
			out[k++] = p.x; out[k++] = p.y; out[k++] = p.z;
		}
	}
	float Energy() const {
		return 0.5f * mass * v.LengthSqr() + 0.5f * w.Dot( InertiaWorld() * w );
	}
};

struct Face { Vec3 normal; float dist; Vec3 mins, maxs; int owner; };	// owner: -1 world, else body index

static void BoxFaces( const Vec3 &mins, const Vec3 &maxs, int owner, std::vector<Face> &faces ) {
	const float e = 0.01f;
	faces.push_back( { Vec3( 1, 0, 0 ), maxs.x, Vec3( maxs.x - e, mins.y, mins.z ), Vec3( maxs.x + e, maxs.y, maxs.z ), owner } );
	faces.push_back( { Vec3( -1, 0, 0 ), -mins.x, Vec3( mins.x - e, mins.y, mins.z ), Vec3( mins.x + e, maxs.y, maxs.z ), owner } );
	faces.push_back( { Vec3( 0, 1, 0 ), maxs.y, Vec3( mins.x, maxs.y - e, mins.z ), Vec3( maxs.x, maxs.y + e, maxs.z ), owner } );
	faces.push_back( { Vec3( 0, -1, 0 ), -mins.y, Vec3( mins.x, mins.y - e, mins.z ), Vec3( maxs.x, mins.y + e, maxs.z ), owner } );
	faces.push_back( { Vec3( 0, 0, 1 ), maxs.z, Vec3( mins.x, mins.y, maxs.z - e ), Vec3( maxs.x, maxs.y, maxs.z + e ), owner } );
	faces.push_back( { Vec3( 0, 0, -1 ), -mins.z, Vec3( mins.x, mins.y, mins.z - e ), Vec3( maxs.x, maxs.y, mins.z + e ), owner } );
}

static bool Inside( const Vec3 &p, const Vec3 &mins, const Vec3 &maxs, float tol ) {
	return p.x >= mins.x - tol && p.x <= maxs.x + tol && p.y >= mins.y - tol && p.y <= maxs.y + tol && p.z >= mins.z - tol && p.z <= maxs.z + tol;
}

struct Scene {
	std::vector<StaticBox> statics;
	std::vector<Body> bodies;
	rigidSolverSettings_t settings;
	sleepSettings_t sleep;
	Vec3 gravity = Vec3( 0, 0, -1066.0f );	// Quake 4 gravity
	float dt = 0.016f;
	bool speculative = true;
	bool persistent = true;
	float maxPenetration = 0.0f;

	Scene() {
		settings.timeStep = dt;
		settings.iterations = 8;
		settings.friction = 0.5f;
		settings.rollingFriction = 0.02f;
		settings.spinningFriction = 0.05f;
		settings.restitution = 0.0f;
		settings.bounceThreshold = 20.0f;
		settings.slop = 0.1f;
		settings.recoveryRate = 0.4f;
		settings.maxRecoverySpeed = 40.0f;
		settings.deepPenetration = 4.0f;
		settings.warmStart = 0.85f;
		sleep.linearSpeed = 1.5f;
		sleep.angularSpeed = 0.05f;
		sleep.quietFrames = 30;
	}

	// translation sweep of the body's vertices along its motion plus a margin,
	// collecting every face the path reaches (like idClip::Contacts)
	int Gather( int self, rigidContact_t *out, int max, Vec3 *points = nullptr ) {
		Body &b = bodies[self];
		std::vector<Face> faces;
		for ( const StaticBox &s : statics ) BoxFaces( s.mins, s.maxs, -1, faces );
		for ( size_t i = 0; i < bodies.size(); i++ ) {
			if ( (int)i == self ) continue;
			const Body &o = bodies[i];
			// other bodies as axis-aligned boxes of their current bounds (test simplification:
			// stacked boxes stay axis aligned)
			BoxFaces( o.x - o.half, o.x + o.half, (int)i, faces );
		}
		float verts[24];
		b.Vertices( verts );
		// like the game glue: sweep along the velocity after gravity, so a body
		// resting or sliding on a floor still reaches it
		Vec3 dir = b.v;		// gravity was already integrated into v this step
		float speed = dir.Normalize();
		if ( speed < 1e-3f ) { dir = gravity; dir.Normalize(); }
		const float depth = speculative ? MaxF( 0.25f, speed * dt + 1.0f ) : 0.25f;
		int n = 0;
		for ( int vi = 0; vi < 8 && n < max; vi++ ) {
			const Vec3 p( verts + vi * 3 );
			for ( const Face &f : faces ) {
				const float h0 = f.normal.Dot( p ) - f.dist;
				const float along = f.normal.Dot( dir );
				// a translation sweep only reports faces it moves toward and reaches
				if ( along >= -1e-4f ) continue;
				if ( h0 + along * depth > 0.0f ) continue;
				if ( h0 < -1.0f ) continue;					// already well behind: the far side of a box
				Vec3 hit = p;
				if ( h0 > 0.0f ) hit = p + dir * ( h0 / -along );
				const Vec3 onPlane = hit - f.normal * ( f.normal.Dot( hit ) - f.dist );
				if ( !Inside( onPlane, f.mins - Vec3( 0.1f, 0.1f, 0.1f ), f.maxs + Vec3( 0.1f, 0.1f, 0.1f ), 0.0f ) ) continue;
				rigidContact_t &c = out[n++];
				std::memset( &c, 0, sizeof( c ) );
				c.point = p - f.normal * h0;	// where the body is now (the game glue does the same)
				c.normal = f.normal;
				c.separation = h0;
				if ( f.owner >= 0 ) {
					const Body &o = bodies[f.owner];
					c.otherInvMass = 1.0f / o.mass;
					c.otherInvInertia = o.InvInertiaWorld().ToSolver();
					c.otherSleeping = o.asleep;
					c.otherR = c.point - o.x;
					c.otherVelocity = o.v + o.w.Cross( c.otherR );
				}
				c.impulse[0] = 0;
				if ( points ) points[n - 1] = Vec3( (float)f.owner, 0, 0 );
				if ( n >= max ) break;
			}
		}
		return n;
	}

	void Step() {
		for ( size_t bi = 0; bi < bodies.size(); bi++ ) {
			Body &b = bodies[bi];
			if ( b.asleep ) continue;
			b.v += gravity * dt;
			rigidContact_t contacts[32];
			Vec3 owners[32];
			int n = Gather( (int)bi, contacts, 32, owners );
			std::vector<int> ages( 32, 0 );
			if ( persistent ) {	// persistent manifold: static planes the sweep no longer reports
				float verts[24];
				b.Vertices( verts );
				for ( const cachedContact_t &cached : b.cache ) {
					if ( cached.entityNum != -1 || cached.age >= 8 || n >= 32 ) continue;
					contactKey_t key{ cached.point, cached.normal, cached.entityNum, 0, 0 };
					bool reported = false;
					for ( int i = 0; i < n && !reported; i++ ) {
						reported = (int)owners[i].x == cached.entityNum && contacts[i].normal.Dot( cached.normal ) > 0.9f &&
							( contacts[i].point - cached.point ).LengthSqr() < 4.0f;
					}
					Vec3 point; float separation;
					if ( !reported && RenewPersistentContact( verts, 8, cached.normal, cached.point, 1.0f, 8.0f, point, separation ) ) {
						rigidContact_t &c = contacts[n];
						std::memset( &c, 0, sizeof( c ) );
						c.point = point; c.normal = cached.normal; c.separation = separation;
						owners[n] = Vec3( -1, 0, 0 );
						ages[n] = cached.age + 1;
						n++;
					}
					(void)key;
				}
			}
			rigidContact_t all[32];
			const int allCount = n;
			std::memcpy( all, contacts, sizeof( rigidContact_t ) * n );
			{	// leading contacts per surface, as the game glue does
				bool keep[32];
				MarkLeadingContacts( contacts, n, 1.0f, [&owners]( int i, int j ) { return owners[i].x == owners[j].x; }, keep );
				int kept = 0;
				for ( int i = 0; i < n; i++ ) if ( keep[i] ) { contacts[kept] = contacts[i]; owners[kept] = owners[i]; ages[kept] = ages[i]; kept++; }
				n = kept;
			}
			// warm start from the cache
			for ( int i = 0; i < n; i++ ) {
				contactKey_t key{ contacts[i].point, contacts[i].normal, (int)owners[i].x, 0, 0 };
				const int match = MatchCachedContact( key, b.cache.data(), (int)b.cache.size(), 2.0f );
				if ( match >= 0 ) std::memcpy( contacts[i].impulse, b.cache[match].impulse, sizeof( contacts[i].impulse ) );
				contacts[i].newContact = match < 0;
			}
			rigidBody_t rb;
			rb.invMass = 1.0f / b.mass;
			rb.invInertia = b.InvInertiaWorld().ToSolver();
			rb.centerOfMass = b.x;
			rb.linearVelocity = b.v;
			rb.angularVelocity = b.w;
			SolveRigidContacts( rb, contacts, n, settings );
			b.v = rb.linearVelocity;
			b.w = rb.angularVelocity;
			// reactions onto other dynamic bodies
			bool supported = false;
			for ( int i = 0; i < n; i++ ) {
				const int owner = (int)owners[i].x;
				if ( contacts[i].normal.z > 0.7f && contacts[i].impulse[0] > 0.0f ) supported = true;
				if ( owner >= 0 && contacts[i].sharesMass ) {
					if ( bodies[owner].asleep ) { bodies[owner].asleep = false; bodies[owner].quiet = 0; }
					Body &o = bodies[owner];
					const Vec3 J = ContactImpulse( contacts[i] ) * -1.0f;
					o.v += J * ( 1.0f / o.mass );
					o.w += o.InvInertiaWorld() * contacts[i].otherR.Cross( J );
				}
			}
			b.cache.clear();
			for ( int i = 0; i < n; i++ ) {
				cachedContact_t c;
				c.point = contacts[i].point; c.normal = contacts[i].normal; c.entityNum = (int)owners[i].x; c.id = 0; c.body = 0; c.age = ages[i];
				std::memcpy( c.impulse, contacts[i].impulse, sizeof( c.impulse ) );
				b.cache.push_back( c );
			}
			// integrate positions with the pseudo velocity, stopping at the time of impact
			const float fraction = MotionFraction( rb, all, allCount, dt, settings.slop );
			const Vec3 lin = b.v + rb.pseudoLinear;
			const Vec3 ang = b.w + rb.pseudoAngular;
			b.x += lin * ( dt * fraction );
			Vec3 axis = ang;
			const float angle = axis.Normalize() * dt * fraction;
			b.R = AxisAngle( axis, angle ) * b.R;
			Orthonormalize( b.R );
			if ( UpdateSleep( b.quiet, b.v.Length(), b.w.Length(), supported, sleep ) ) {
				b.asleep = true;
				b.v = Vec3(); b.w = Vec3();
			}
		}
		// worst penetration against the static world
		for ( const Body &b : bodies ) {
			float verts[24];
			b.Vertices( verts );
			for ( int vi = 0; vi < 8; vi++ ) {
				const Vec3 p( verts + vi * 3 );
				for ( const StaticBox &s : statics ) {
					if ( Inside( p, s.mins, s.maxs, 0.0f ) ) {
						const float d = MinF( MinF( MinF( p.x - s.mins.x, s.maxs.x - p.x ), MinF( p.y - s.mins.y, s.maxs.y - p.y ) ), MinF( p.z - s.mins.z, s.maxs.z - p.z ) );
						maxPenetration = MaxF( maxPenetration, d );
					}
				}
			}
		}
	}
};

static Body MakeBox( Vec3 half, float mass, Vec3 x ) {
	Body b;
	b.half = half; b.mass = mass; b.x = x; b.R = M3::I();
	return b;
}

static void TestDroppedBoxSettles() {
	Scene scene;
	scene.statics.push_back( { Vec3( -512, -512, -64 ), Vec3( 512, 512, 0 ) } );
	scene.bodies.push_back( MakeBox( Vec3( 16, 16, 16 ), 40.0f, Vec3( 0, 0, 96 ) ) );
	scene.bodies[0].w = Vec3( 1.5f, 0.7f, 0.0f );		// land on an edge
	float maxLateWobble = 0.0f;
	int sleptAt = -1;
	for ( int frame = 0; frame < 300; frame++ ) {
		scene.Step();
		if ( frame > 150 ) maxLateWobble = MaxF( maxLateWobble, scene.bodies[0].v.Length() );
		if ( sleptAt < 0 && scene.bodies[0].asleep ) sleptAt = frame;
	}
	const Body &b = scene.bodies[0];
	CHECK( sleptAt >= 0 && sleptAt < 200, "dropped box did not sleep within 200 frames (slept at %d)", sleptAt );
	CHECK( maxLateWobble < 2.0f, "dropped box still moving %.3f u/s after 2.4 s", maxLateWobble );
	CHECK( scene.maxPenetration < 1.0f, "dropped box penetrated the floor by %.3f", scene.maxPenetration );
	float verts[24]; b.Vertices( verts );
	float lowest = 1e9f; for ( int i = 0; i < 8; i++ ) lowest = MinF( lowest, verts[i * 3 + 2] );
	CHECK( std::fabs( lowest ) < 0.6f, "dropped box rests %.3f off the floor", lowest );
	std::printf( "dropped box: slept at frame %d, wobble %.3f, penetration %.3f, rest height %.3f\n", sleptAt, maxLateWobble, scene.maxPenetration, lowest );
}

static void TestStackStaysStacked() {
	Scene scene;
	scene.statics.push_back( { Vec3( -512, -512, -64 ), Vec3( 512, 512, 0 ) } );
	// a pyramid stack: every upper corner lands on the face below (the test world
	// only makes vertex-face contacts, so coincident side planes are avoided)
	float base = 0.0f;
	for ( int i = 0; i < 4; i++ ) {
		const float h = 16.0f - 1.5f * i;
		scene.bodies.push_back( MakeBox( Vec3( h, h, h ), 40.0f, Vec3( 0.5f * i, 0, base + h + 0.05f ) ) );
		base += 2.0f * h + 0.1f;
	}
	float drift = 0.0f;
	for ( int frame = 0; frame < 600; frame++ ) {
		scene.Step();
	}
	base = 0.0f;
	for ( int i = 0; i < 4; i++ ) {
		const Body &b = scene.bodies[i];
		const float h = 16.0f - 1.5f * i;
		drift = MaxF( drift, std::sqrt( ( b.x.x - 0.5f * i ) * ( b.x.x - 0.5f * i ) + b.x.y * b.x.y ) );
		CHECK( std::fabs( b.x.z - ( base + h ) ) < 1.0f, "stack box %d at height %.2f, want %.2f", i, b.x.z, base + h );
		base += 2.0f * h;
	}
	int asleep = 0; for ( const Body &b : scene.bodies ) asleep += b.asleep ? 1 : 0;
	CHECK( drift < 2.0f, "stack drifted %.3f units", drift );
	CHECK( asleep == 4, "only %d of 4 stacked boxes slept", asleep );
	std::printf( "stack of 4: drift %.3f, asleep %d, floor penetration %.3f\n", drift, asleep, scene.maxPenetration );
}

static void TestNoTunnelling() {
	// a small fast body (4 unit cube at 4000 u/s, 64 units per step) at a 2 unit wall
	Scene scene;
	scene.gravity = Vec3();
	scene.statics.push_back( { Vec3( 100, -256, -256 ), Vec3( 102, 256, 256 ) } );
	float deepest = -1e9f;
	for ( int trial = 0; trial < 64; trial++ ) {
		scene.bodies.clear();
		Body b = MakeBox( Vec3( 2, 2, 2 ), 2.0f, Vec3( -50.0f + trial * 1.37f, 0, 0 ) );
		b.v = Vec3( 4000.0f, 30.0f * std::sin( (float)trial ), 0 );
		b.w = Vec3( 0, 0, 5.0f * std::cos( (float)trial ) );
		scene.bodies.push_back( b );
		for ( int frame = 0; frame < 60; frame++ ) {
			scene.Step();
			float verts[24];
			scene.bodies[0].Vertices( verts );
			for ( int i = 0; i < 8; i++ ) deepest = MaxF( deepest, verts[i * 3] - 100.0f );
		}
		CHECK( scene.bodies[0].x.x < 100.0f, "trial %d: fast body tunnelled to x=%.2f", trial, scene.bodies[0].x.x );
	}
	CHECK( deepest < 1.0f, "a corner of the fast body reached %.3f units into the 2-unit wall", deepest );
	std::printf( "fast small body vs 2-unit wall: 64 trials, deepest corner %.3f units into the wall\n", deepest );
}

static void TestSolverNeverAddsEnergy() {
	std::mt19937 rng( 1234 );
	std::uniform_real_distribution<float> u( -1.0f, 1.0f );
	float worstGain = 0.0f;
	rigidSolverSettings_t s;
	s.timeStep = 0.016f; s.iterations = 8; s.friction = 0.8f; s.rollingFriction = 0.05f; s.spinningFriction = 0.1f;
	s.restitution = 0.0f; s.bounceThreshold = 20.0f; s.slop = 0.1f; s.recoveryRate = 0.4f; s.maxRecoverySpeed = 40.0f; s.deepPenetration = 4.0f; s.warmStart = 0.0f;
	for ( int trial = 0; trial < 20000; trial++ ) {
		Body b = MakeBox( Vec3( 4 + 12 * std::fabs( u( rng ) ), 4 + 12 * std::fabs( u( rng ) ), 4 + 12 * std::fabs( u( rng ) ) ), 5 + 50 * std::fabs( u( rng ) ), Vec3() );
		b.R = AxisAngle( Vec3( u( rng ), u( rng ), u( rng ) ), 3.0f * u( rng ) );
		b.v = Vec3( u( rng ), u( rng ), u( rng ) ) * 400.0f;
		b.w = Vec3( u( rng ), u( rng ), u( rng ) ) * 6.0f;
		rigidContact_t c[8];
		const int n = 1 + trial % 8;
		for ( int i = 0; i < n; i++ ) {
			std::memset( &c[i], 0, sizeof( c[i] ) );
			Vec3 nrm( u( rng ), u( rng ), u( rng ) ); nrm.Normalize();
			c[i].normal = nrm;
			c[i].point = b.R * Vec3( b.half.x * u( rng ), b.half.y * u( rng ), b.half.z * u( rng ) );
			c[i].separation = ( trial & 1 ) ? -2.0f * std::fabs( u( rng ) ) : 0.0f;		// overlap is position-only
			if ( trial % 5 == 0 ) {
				c[i].otherInvMass = 0.05f;
				c[i].otherInvInertia = Mat3::Identity();
				c[i].otherInvInertia.row[0] *= 0.001f; c[i].otherInvInertia.row[1] *= 0.001f; c[i].otherInvInertia.row[2] *= 0.001f;
				c[i].otherR = Vec3( u( rng ), u( rng ), u( rng ) ) * 10.0f;
			}
		}
		const float before = b.Energy();
		rigidBody_t rb;
		rb.invMass = 1.0f / b.mass; rb.invInertia = b.InvInertiaWorld().ToSolver(); rb.centerOfMass = Vec3();
		rb.linearVelocity = b.v; rb.angularVelocity = b.w;
		SolveRigidContacts( rb, c, n, s );
		b.v = rb.linearVelocity; b.w = rb.angularVelocity;
		const float after = b.Energy();
		// other bodies (trial % 5 == 0) are moving at zero velocity: the reaction can only take energy
		worstGain = MaxF( worstGain, ( after - before ) / MaxF( before, 1.0f ) );
	}
	CHECK( worstGain < 1e-3f, "contact solve raised kinetic energy by %.5f of the initial value", worstGain );
	std::printf( "20000 random contact solves: worst relative energy change %+.6f\n", worstGain );
}

// An impact (a contact reached within the step above the bounce speed) slides
// and bounces as the original collision impulse did: no friction, and a
// rebound by the body's bouncyness even off a surface it already rested on. A
// slower touch keeps full Coulomb friction and does not bounce.
static void TestImpactsSlideAndBounce() {
	rigidSolverSettings_t s;
	s.timeStep = 0.016f; s.iterations = 8; s.friction = 0.6f; s.rollingFriction = 0.0f; s.spinningFriction = 0.0f;
	s.restitution = 0.2f; s.bounceThreshold = 35.0f; s.slop = 0.1f; s.recoveryRate = 0.4f; s.maxRecoverySpeed = 40.0f; s.deepPenetration = 4.0f; s.warmStart = 0.0f;
	struct Case { float approach; float impactFriction; bool bounceResting; float wantX; float wantZ; const char *what; };
	const Case cases[] = {
		{ 100.0f, 1.0f, false, 300.0f - 0.6f * 100.0f, 0.0f, "impact with full friction on a kept contact" },
		{ 100.0f, 0.0f, true, 300.0f, 20.0f, "frictionless impact that bounces on a kept contact" },
		{ 100.0f, 0.0f, false, 300.0f, 0.0f, "frictionless impact that only bounces on a new touch" },
		{ 20.0f, 0.0f, true, 300.0f - 0.6f * 20.0f, 0.0f, "slow touch: full friction, no bounce" },
	};
	for ( const Case &k : cases ) {
		s.impactFriction = k.impactFriction;
		s.bounceResting = k.bounceResting;
		rigidBody_t rb;
		rb.invMass = 1.0f / 30.0f;
		rb.invInertia = Mat3::Zero();		// translation only: the row is not coupled into spin
		rb.centerOfMass = Vec3();
		rb.linearVelocity = Vec3( 300.0f, 0.0f, -k.approach );
		rb.angularVelocity = Vec3();
		rigidContact_t c;
		std::memset( &c, 0, sizeof( c ) );
		c.normal = Vec3( 0.0f, 0.0f, 1.0f );
		c.point = Vec3( 0.0f, 0.0f, -8.0f );
		c.separation = 0.0f;
		c.newContact = false;
		SolveRigidContacts( rb, &c, 1, s );
		CHECK( std::fabs( rb.linearVelocity.x - k.wantX ) < 0.5f && std::fabs( rb.linearVelocity.z - k.wantZ ) < 0.5f,
			"%s: velocity %.2f %.2f, want %.2f %.2f", k.what, rb.linearVelocity.x, rb.linearVelocity.z, k.wantX, k.wantZ );
		CHECK( c.impact == ( k.approach > s.bounceThreshold ), "%s: impact flag", k.what );
	}
	{
		// a kept contact with another moving body never bounces: stacks would hop
		s.impactFriction = 0.0f;
		s.bounceResting = true;
		rigidBody_t rb;
		rb.invMass = 1.0f / 30.0f;
		rb.invInertia = Mat3::Zero();
		rb.centerOfMass = Vec3();
		rb.linearVelocity = Vec3( 0.0f, 0.0f, -100.0f );
		rb.angularVelocity = Vec3();
		rigidContact_t c;
		std::memset( &c, 0, sizeof( c ) );
		c.normal = Vec3( 0.0f, 0.0f, 1.0f );
		c.point = Vec3( 0.0f, 0.0f, -8.0f );
		c.otherInvMass = 1.0f / 30.0f;
		c.otherInvInertia = Mat3::Zero();
		c.newContact = false;
		SolveRigidContacts( rb, &c, 1, s );
		CHECK( rb.linearVelocity.z <= 0.5f && rb.linearVelocity.z > -100.0f, "kept contact between two bodies: velocity %.2f, want it stopped with no rebound", rb.linearVelocity.z );
		// the same contact against a fixed support does rebound
		rb.linearVelocity = Vec3( 0.0f, 0.0f, -100.0f );
		std::memset( c.impulse, 0, sizeof( c.impulse ) );
		c.otherInvMass = 0.0f;
		SolveRigidContacts( rb, &c, 1, s );
		CHECK( std::fabs( rb.linearVelocity.z - 20.0f ) < 0.5f, "kept contact with a fixed support: velocity %.2f, want 20", rb.linearVelocity.z );
	}
	std::printf( "impacts: frictionless and bouncing on kept contacts, slow touches keep friction\n" );
}

static void TestBoxedPGS() {
	std::mt19937 rng( 99 );
	std::uniform_real_distribution<float> u( -1.0f, 1.0f );
	float worst = 0.0f;
	int worstIterations = 0;
	for ( int trial = 0; trial < 500; trial++ ) {
		const int n = 3 + trial % 30;
		// A = J M^-1 J^T for random Jacobians: symmetric positive semi-definite like the AF rows
		const int bodies = 2 + n / 3;
		std::vector<float> J( n * bodies * 6 ), A( n * n ), x( n, 0.0f ), b( n ), lo( n ), hi( n );
		std::vector<int> box( n, -1 );
		for ( float &v : J ) v = u( rng );
		std::vector<float> invM( bodies * 6 );
		for ( float &v : invM ) v = 0.05f + std::fabs( u( rng ) );
		for ( int i = 0; i < n; i++ ) for ( int j = 0; j < n; j++ ) {
			float sum = 0.0f;
			for ( int k = 0; k < bodies * 6; k++ ) sum += J[i * bodies * 6 + k] * invM[k] * J[j * bodies * 6 + k];
			A[i * n + j] = sum + ( i == j ? 1e-3f : 0.0f );
		}
		for ( int i = 0; i < n; i++ ) {
			b[i] = 10.0f * u( rng );
			const int kind = i % 3;
			if ( kind == 0 ) { lo[i] = 0.0f; hi[i] = 1e30f; }				// contact
			else if ( kind == 1 && i > 0 ) { lo[i] = -0.5f; hi[i] = 0.5f; box[i] = i - 1; }	// friction on the previous contact
			else { lo[i] = -1e30f; hi[i] = 1e30f; }						// equality
		}
		// box rows must reference a contact row (bounded below by 0)
		for ( int i = 0; i < n; i++ ) if ( box[i] >= 0 && box[box[i]] != -1 ) box[i] = -1;
		float change = 0.0f;
		const int iterations = SolveBoxedPGS( A.data(), n, x.data(), b.data(), lo.data(), hi.data(), box.data(), n, 400, 1e-6f, change );
		const float error = BoxedLCPError( A.data(), n, x.data(), b.data(), lo.data(), hi.data(), box.data(), n );
		float scale = 1.0f; for ( float v : x ) scale = MaxF( scale, std::fabs( v ) );
		worst = MaxF( worst, error / scale );
		worstIterations = iterations > worstIterations ? iterations : worstIterations;
	}
	CHECK( worst < 2e-2f, "boxed PGS left a relative LCP error of %.5f", worst );
	std::printf( "boxed PGS: 500 random problems, worst relative error %.6f, most iterations %d\n", worst, worstIterations );

	// warm starting from a converged solution converges at once
	{
		const int n = 12;
		std::vector<float> A( n * n, 0.0f ), x( n, 0.0f ), b( n ), lo( n, 0.0f ), hi( n, 1e30f );
		for ( int i = 0; i < n; i++ ) { A[i * n + i] = 2.0f; if ( i + 1 < n ) { A[i * n + i + 1] = A[( i + 1 ) * n + i] = -0.9f; } b[i] = 1.0f + 0.1f * i; }
		float change;
		SolveBoxedPGS( A.data(), n, x.data(), b.data(), lo.data(), hi.data(), nullptr, n, 2000, 1e-7f, change );
		const int again = SolveBoxedPGS( A.data(), n, x.data(), b.data(), lo.data(), hi.data(), nullptr, n, 2000, 1e-6f, change );
		CHECK( again <= 2, "warm-started PGS took %d iterations from a converged start", again );
	}
}

static void TestImpulseGuard() {
	// light limb hit: J = 5000 on a 4 kg hand of a 120 kg figure, guard 300 u/s
	impulseSplit_t s = SplitImpulse( 5000.0f, 4.0f, 120.0f, 300.0f );
	CHECK( s.clamped, "light-limb hit was not guarded" );
	CHECK( std::fabs( s.struckDeltaV - 300.0f ) < 0.5f, "guarded struck speed %.2f, want 300", s.struckDeltaV );
	CHECK( s.struckDeltaV <= s.legacyDeltaV, "guard raised the struck speed" );
	// momentum: f J at the hit point plus (1 - f) J spread by mass is J in total
	const float momentum = s.struckFraction * 5000.0f + ( 1.0f - s.struckFraction ) * 5000.0f;
	CHECK( std::fabs( momentum - 5000.0f ) < 1e-3f, "guard changed the linear momentum" );
	// a heavy hit whose uniform share is already above the guard is spread fully
	s = SplitImpulse( 90000.0f, 30.0f, 120.0f, 300.0f );
	CHECK( s.clamped && s.struckFraction == 0.0f, "heavy hit split %.3f", s.struckFraction );
	CHECK( std::fabs( s.struckDeltaV - 750.0f ) < 0.5f, "heavy hit struck speed %.2f, want 750 (= J / M)", s.struckDeltaV );
	// below the guard nothing changes
	s = SplitImpulse( 600.0f, 4.0f, 120.0f, 300.0f );
	CHECK( !s.clamped && s.struckFraction == 1.0f && std::fabs( s.struckDeltaV - 150.0f ) < 1e-3f, "light hit below the guard was changed" );
	// a single-body figure is never redistributed
	s = SplitImpulse( 5000.0f, 4.0f, 4.0f, 300.0f );
	CHECK( !s.clamped, "single-body figure was redistributed" );
	std::printf( "impulse guard: checks passed\n" );
}

static void TestEnergyGuardKeepsMomentum() {
	// two 10 kg bodies at 100 and 300 u/s along x: V = 200, KE = 500000,
	// centre-of-mass part 400000, relative part 100000
	const float m = 10.0f, v1 = 100.0f, v2 = 300.0f;
	const float energy = 0.5f * m * ( v1 * v1 + v2 * v2 );
	const float V = ( v1 + v2 ) * 0.5f;
	const float comEnergy = 0.5f * 2.0f * m * V * V;
	// allow 450000: half the relative energy goes, the shared motion stays
	energySplit_t s = EnergyScaleKeepingMomentum( energy, comEnergy, 450000.0f );
	CHECK( s.comScale == 1.0f, "guard scaled the centre-of-mass motion (%.4f)", s.comScale );
	const float n1 = V + s.relativeScale * ( v1 - V ), n2 = V + s.relativeScale * ( v2 - V );
	CHECK( std::fabs( m * ( n1 + n2 ) - m * ( v1 + v2 ) ) < 1e-2f, "guard changed the momentum" );
	CHECK( std::fabs( 0.5f * m * ( n1 * n1 + n2 * n2 ) - 450000.0f ) < 1.0f, "guarded energy %.1f, want 450000", 0.5f * m * ( n1 * n1 + n2 * n2 ) );
	// allowance below the shared motion: relative motion removed, V scaled to fit
	s = EnergyScaleKeepingMomentum( energy, comEnergy, 100000.0f );
	CHECK( s.relativeScale == 0.0f && std::fabs( 0.5f * 2.0f * m * ( V * s.comScale ) * ( V * s.comScale ) - 100000.0f ) < 1.0f,
		"over-budget split rel %.3f com %.3f", s.relativeScale, s.comScale );
	// within the allowance nothing changes
	s = EnergyScaleKeepingMomentum( energy, comEnergy, energy + 1.0f );
	CHECK( s.relativeScale == 1.0f && s.comScale == 1.0f, "guard changed an allowed step" );
	// impact friction load: the resting share on a 30-degree slope
	CHECK( std::fabs( ImpactFrictionLoad( 10.0f, 1066.0f, 0.866f ) - 9231.56f ) < 0.5f, "impact friction load" );
	CHECK( ImpactFrictionLoad( 10.0f, 1066.0f, -0.2f ) == 0.0f, "a ceiling carries no load" );
	std::printf( "energy guard momentum: checks passed\n" );
}

static void TestSweptSeparation() {
	// a long bar (64 x 4 x 4) tilted so its left end is 0.5 above the floor and its right end 6 above:
	// a contact under the left end must report 0.5, not the lowest vertex of the whole body
	Body bar = MakeBox( Vec3( 32, 2, 2 ), 10.0f, Vec3( 0, 0, 0 ) );
	bar.R = AxisAngle( Vec3( 0, 1, 0 ), -std::atan2( 5.5f, 64.0f ) );
	float verts[24];
	bar.Vertices( verts );
	float lowestZ = 1e9f; for ( int i = 0; i < 8; i++ ) lowestZ = MinF( lowestZ, verts[i * 3 + 2] );
	for ( int i = 0; i < 8; i++ ) verts[i * 3 + 2] += 0.5f - lowestZ;	// lift so the lowest corner is 0.5 up
	// the right end's lowest corner
	int rightLow = -1; for ( int i = 0; i < 8; i++ ) if ( verts[i * 3] > 0 && ( rightLow < 0 || verts[i * 3 + 2] < verts[rightLow * 3 + 2] ) ) rightLow = i;
	const Vec3 down( 0, 0, -1 ), up( 0, 0, 1 );
	// a floor contact under the right end, as a downward sweep would report it
	const Vec3 point( verts[rightLow * 3], verts[rightLow * 3 + 1], 0.0f );
	const float swept = SweptContactSeparation( verts, 8, up, point, down, 33.0f );
	const float global = PlaneSeparation( verts, 8, up, point );
	CHECK( std::fabs( swept - verts[rightLow * 3 + 2] ) < 1e-3f, "swept separation %.3f, want %.3f", swept, verts[rightLow * 3 + 2] );
	CHECK( std::fabs( global - 0.5f ) < 1e-3f, "plane separation %.3f", global );
	std::printf( "swept separation: right-end contact %.3f (plane estimate %.3f)\n", swept, global );
}

static void TestSleepCounter() {
	sleepSettings_t s{ 1.5f, 0.05f, 10 };
	int quiet = 0;
	bool slept = false;
	for ( int i = 0; i < 9; i++ ) slept = UpdateSleep( quiet, 0.5f, 0.01f, true, s );
	CHECK( !slept, "slept before the quiet window" );
	slept = UpdateSleep( quiet, 0.5f, 0.01f, true, s );
	CHECK( slept, "did not sleep after the quiet window" );
	quiet = 5;
	slept = UpdateSleep( quiet, 0.5f, 0.01f, false, s );
	CHECK( !slept && quiet == 0, "an unsupported body counted as quiet" );
	CHECK( EnergyScale( 4.0f, 1.0f ) == 0.5f && EnergyScale( 1.0f, 4.0f ) == 1.0f, "energy scale" );
}

static void TestTelemetry() {
	Telemetry t;
	t.Start( "unit" );
	for ( int frame = 1; frame <= 100; frame++ ) {
		t.FrameTick( frame );
		t.AddEvaluation( EVAL_RIGID_BODY, frame * 0.01, true );
		if ( frame % 2 == 0 ) t.AddEvaluation( EVAL_ARTICULATED, 0.5, false );
		// joints apart by more than the visible stretch on ten frames, at most 20 units
		t.NoteJointStretch( frame <= 10 ? 10.0f + frame : 2.0f );
		t.NoteJointStretch( 1.0f );
		if ( frame == 7 ) t.NoteFallbackMs( 0.25 );
	}
	t.Stop();
	t.NoteFallbackMs( 9.0 );	// not recording: ignored
	CHECK( t.Samples().size() == 100, "telemetry kept %d samples", (int)t.Samples().size() );
	const distribution_t rb = t.Distribution( EVAL_RIGID_BODY );
	CHECK( std::fabs( rb.p50Ms - 0.50 ) < 1e-6 && std::fabs( rb.p95Ms - 0.95 ) < 1e-6 && std::fabs( rb.p99Ms - 0.99 ) < 1e-6, "percentiles %.3f %.3f %.3f", rb.p50Ms, rb.p95Ms, rb.p99Ms );
	CHECK( std::fabs( rb.maxMs - 1.0 ) < 1e-6 && rb.maxFrame == 100, "max %.3f at %d", rb.maxMs, rb.maxFrame );
	const distribution_t all = t.Distribution( -1 );
	CHECK( std::fabs( all.totalMs - ( 50.5 + 25.0 ) ) < 1e-3, "total %.3f", all.totalMs );
	std::string line;
	t.Report( [&line]( const char *s ) { line += s; }, "legacy" );
	CHECK( line.find( "PHYSICS_STATS {\"label\":\"unit\",\"mode\":\"legacy\",\"frames\":100," ) == 0, "report: %s", line.c_str() );
	CHECK( line.find( "\"fallback_ms\":0.2500,\"peak_joint_stretch\":20.00,\"stretched_frames\":10}" ) != std::string::npos, "report tail: %s", line.c_str() );
	// every frame's cost, in capture order, 32 to a line
	std::vector<std::string> frames;
	t.ReportFrames( [&frames]( const char *s ) { frames.push_back( s ); }, "legacy" );
	CHECK( frames.size() == 4, "%d frame lines", (int)frames.size() );
	CHECK( !frames.empty() && frames[0].find( "PHYSICS_FRAMES unit legacy 0 32 0.0100 0.5200 0.0300" ) == 0, "frame line: %s", frames.empty() ? "" : frames[0].c_str() );
	CHECK( frames.size() == 4 && frames[3].find( "PHYSICS_FRAMES unit legacy 96 4 0.9700 1.4800 0.9900 1.5000" ) == 0, "last frame line: %s", frames.size() == 4 ? frames[3].c_str() : "" );
	StateDigest a, b;
	const float values[3] = { 1.0f, -0.0f, 2.5f };
	const float same[3] = { 1.0f, 0.0f, 2.5f };
	a.AddFloats( values, 3 );
	b.AddFloats( same, 3 );
	CHECK( a.Hash() == b.Hash(), "digest distinguishes -0 from +0" );
}

int main() {
	TestTelemetry();
	TestSleepCounter();
	TestSweptSeparation();
	TestImpulseGuard();
	TestEnergyGuardKeepsMomentum();
	TestBoxedPGS();
	TestSolverNeverAddsEnergy();
	TestImpactsSlideAndBounce();
	TestDroppedBoxSettles();
	TestStackStaysStacked();
	TestNoTunnelling();
	if ( failures ) {
		std::printf( "%d check(s) failed\n", failures );
		return 1;
	}
	std::printf( "PhysicsEnhancedSolverTest: all checks passed\n" );
	return 0;
}
