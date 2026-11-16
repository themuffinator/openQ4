// openQ4 additions to the SDK game sources; see src/game/LICENSE.
#pragma once
#include <cmath>
#include <cstring>

// Enhanced physics kernels (g_enhancedPhysics 1), shared by the single-player
// and multiplayer game trees and driven directly by the native solver test
// (tools/tests/physics_enhanced_solver.py). Everything here is plain float math
// on caller-owned arrays: the game glue in Physics_RigidBody.cpp and
// Physics_AF.cpp gathers contacts and constraint rows, calls these kernels and
// writes the results back. Nothing allocates and nothing touches game state.
//
// Matrices use idLib's convention: rows are stored, and Apply( v ) returns
// v.x * row0 + v.y * row1 + v.z * row2, the same as idMat3 * idVec3, so raw
// idMat3 floats can be copied in unchanged. The inertia matrices passed here
// are symmetric, where the distinction does not matter.
namespace OpenQ4Physics {

// ---------------------------------------------------------------------------
// small vector helpers
// ---------------------------------------------------------------------------

struct Vec3 {
	float x, y, z;

	Vec3( void ) : x( 0.0f ), y( 0.0f ), z( 0.0f ) {}
	Vec3( float x, float y, float z ) : x( x ), y( y ), z( z ) {}
	explicit Vec3( const float *v ) : x( v[0] ), y( v[1] ), z( v[2] ) {}

	Vec3	operator+( const Vec3 &o ) const { return Vec3( x + o.x, y + o.y, z + o.z ); }
	Vec3	operator-( const Vec3 &o ) const { return Vec3( x - o.x, y - o.y, z - o.z ); }
	Vec3	operator-( void ) const { return Vec3( -x, -y, -z ); }
	Vec3	operator*( float s ) const { return Vec3( x * s, y * s, z * s ); }
	Vec3 &	operator+=( const Vec3 &o ) { x += o.x; y += o.y; z += o.z; return *this; }
	Vec3 &	operator-=( const Vec3 &o ) { x -= o.x; y -= o.y; z -= o.z; return *this; }
	Vec3 &	operator*=( float s ) { x *= s; y *= s; z *= s; return *this; }
	float	Dot( const Vec3 &o ) const { return x * o.x + y * o.y + z * o.z; }
	Vec3	Cross( const Vec3 &o ) const { return Vec3( y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x ); }
	float	LengthSqr( void ) const { return x * x + y * y + z * z; }
	float	Length( void ) const { return std::sqrt( LengthSqr() ); }
	void	Store( float *v ) const { v[0] = x; v[1] = y; v[2] = z; }
	float	Normalize( void ) {
		const float length = Length();
		if ( length > 1e-12f ) {
			const float inv = 1.0f / length;
			x *= inv; y *= inv; z *= inv;
		} else {
			x = y = z = 0.0f;
		}
		return length;
	}
};

inline Vec3 operator*( float s, const Vec3 &v ) { return v * s; }

struct Mat3 {
	Vec3 row[3];

	Mat3( void ) {}
	explicit Mat3( const float *m ) { row[0] = Vec3( m ); row[1] = Vec3( m + 3 ); row[2] = Vec3( m + 6 ); }
	static Mat3 Zero( void ) { return Mat3(); }
	static Mat3 Identity( void ) {
		Mat3 m;
		m.row[0] = Vec3( 1.0f, 0.0f, 0.0f );
		m.row[1] = Vec3( 0.0f, 1.0f, 0.0f );
		m.row[2] = Vec3( 0.0f, 0.0f, 1.0f );
		return m;
	}
	Vec3 Apply( const Vec3 &v ) const { return row[0] * v.x + row[1] * v.y + row[2] * v.z; }
};

inline float Clamp( float v, float lo, float hi ) { return v < lo ? lo : ( v > hi ? hi : v ); }
inline float MaxF( float a, float b ) { return a > b ? a : b; }
inline float MinF( float a, float b ) { return a < b ? a : b; }

// Two unit tangents orthogonal to n (n must be unit length).
inline void TangentBasis( const Vec3 &n, Vec3 &t1, Vec3 &t2 ) {
	if ( std::fabs( n.z ) > 0.7071f ) {
		const float a = n.y * n.y + n.z * n.z;
		const float k = 1.0f / std::sqrt( a );
		t1 = Vec3( 0.0f, -n.z * k, n.y * k );
	} else {
		const float a = n.x * n.x + n.y * n.y;
		const float k = 1.0f / std::sqrt( a );
		t1 = Vec3( -n.y * k, n.x * k, 0.0f );
	}
	t2 = n.Cross( t1 );
}

// ---------------------------------------------------------------------------
// Boxed projected Gauss-Seidel (articulated figure auxiliary constraints)
// ---------------------------------------------------------------------------
//
// Solves A x = b + t with the idLCP box semantics: per row lo <= x <= hi with
// t = 0 inside the bounds, t >= 0 at lo and t <= 0 at hi; a row with
// boxIndex[i] >= 0 has its bounds scaled by |x[boxIndex[i]]| (friction rows
// bounded by their contact's normal force). x holds the warm start on entry.
// A is n x n with the given row stride and must be symmetric positive
// semi-definite with a positive diagonal for every active row; rows whose
// diagonal is not positive are held at zero. Returns the iterations run; the
// largest absolute change in the last sweep is stored in lastChange.
inline int SolveBoxedPGS( const float *A, int stride, float *x, const float *b, const float *lo, const float *hi,
						const int *boxIndex, int n, int maxIterations, float tolerance, float &lastChange ) {
	lastChange = 0.0f;
	if ( n <= 0 ) {
		return 0;
	}
	// project the warm start onto the bounds first
	for ( int i = 0; i < n; i++ ) {
		if ( !( A[i * stride + i] > 0.0f ) ) {
			x[i] = 0.0f;
		}
	}
	int iteration = 0;
	for ( ; iteration < maxIterations; iteration++ ) {
		float change = 0.0f;
		float scale = 0.0f;
		for ( int i = 0; i < n; i++ ) {
			const float *row = A + i * stride;
			const float diagonal = row[i];
			if ( !( diagonal > 0.0f ) ) {
				continue;
			}
			float sum = 0.0f;
			for ( int j = 0; j < n; j++ ) {
				sum += row[j] * x[j];
			}
			const float residual = b[i] - sum;		// want A x = b inside the bounds
			float value = x[i] + residual / diagonal;
			float low = lo[i], high = hi[i];
			if ( boxIndex && boxIndex[i] >= 0 ) {
				const float reference = std::fabs( x[boxIndex[i]] );
				low = -std::fabs( low * reference );
				high = std::fabs( high * reference );
			}
			value = Clamp( value, low, high );
			const float delta = value - x[i];
			x[i] = value;
			change = MaxF( change, std::fabs( delta ) );
			scale = MaxF( scale, std::fabs( value ) );
		}
		lastChange = change;
		if ( change <= tolerance * MaxF( 1.0f, scale ) ) {
			iteration++;
			break;
		}
	}
	return iteration;
}

// Largest violation of the LCP conditions for a solution, relative to the
// row scale; used by the native test and the solver fallback.
inline float BoxedLCPError( const float *A, int stride, const float *x, const float *b, const float *lo, const float *hi,
							const int *boxIndex, int n ) {
	float worst = 0.0f;
	for ( int i = 0; i < n; i++ ) {
		const float *row = A + i * stride;
		if ( !( row[i] > 0.0f ) ) {
			continue;
		}
		float sum = 0.0f;
		for ( int j = 0; j < n; j++ ) {
			sum += row[j] * x[j];
		}
		const float t = sum - b[i];		// A x = b + t
		float low = lo[i], high = hi[i];
		if ( boxIndex && boxIndex[i] >= 0 ) {
			const float reference = std::fabs( x[boxIndex[i]] );
			low = -std::fabs( low * reference );
			high = std::fabs( high * reference );
		}
		const float slack = 1e-4f * MaxF( 1.0f, std::fabs( x[i] ) );
		float error = 0.0f;
		if ( high - low <= slack ) {
			error = std::fabs( x[i] - low );	// a collapsed box (unloaded friction) accepts any residual
		} else if ( x[i] < low - slack || x[i] > high + slack ) {
			error = MaxF( low - x[i], x[i] - high );
		} else if ( x[i] <= low + slack ) {
			error = MaxF( 0.0f, -t );		// at the lower bound t must be >= 0
		} else if ( x[i] >= high - slack ) {
			error = MaxF( 0.0f, t );		// at the upper bound t must be <= 0
		} else {
			error = std::fabs( t );
		}
		worst = MaxF( worst, error / row[i] );
	}
	return worst;
}

// ---------------------------------------------------------------------------
// Rigid-body contact solver
// ---------------------------------------------------------------------------

struct rigidSolverSettings_t {
	float	timeStep;
	int		iterations;
	float	friction;			// Coulomb coefficient
	float	rollingFriction;	// rolling resistance, as a fraction of the normal impulse times the lever arm
	float	spinningFriction;	// resistance to spinning about the contact normal, same units
	float	restitution;		// the body's bouncyness
	float	bounceThreshold;	// approach speeds below this never bounce (units per second)
	float	slop;				// penetration tolerated without correction (units)
	float	recoveryRate;		// fraction of the penetration removed per step
	float	maxRecoverySpeed;	// cap on the correction speed (units per second)
	float	deepPenetration;	// overlap beyond this is corrected by position only (units)
	float	warmStart;			// fraction of last step's impulses applied up front
	// An impact is a contact the body approaches faster than the bounce threshold
	// and reaches within the step. impactFriction scales the friction (and the
	// rolling and spinning resistance) of such a contact: 1 is full Coulomb
	// friction on the impact's impulse, 0 a frictionless impact. bounceResting
	// lets an impact bounce on a contact the body already had, not only on a new
	// touch: a resting body shoved into its support rebounds off it. That holds
	// for fixed supports only (the world, scenery). Between two moving bodies a
	// kept contact never bounces: in a stack each rebound is handed to the next
	// body as a reaction and comes back as another impact, and the stack hops
	// for as long as it is awake.
	float	impactFriction = 1.0f;
	bool	bounceResting = false;
};

struct rigidBody_t {
	float	invMass;
	Mat3	invInertia;			// world space
	Vec3	centerOfMass;		// world space
	Vec3	linearVelocity;
	Vec3	angularVelocity;
	Vec3	pseudoLinear;		// position-only correction velocity (never kept as motion)
	Vec3	pseudoAngular;
};

struct rigidContact_t {
	// inputs
	Vec3	point;				// world space
	Vec3	normal;				// unit, from the obstacle toward the body
	float	separation;			// signed gap along the normal at the start of the step; negative is overlap
	float	otherInvMass;		// 0 for the world or a static/kinematic entity
	Mat3	otherInvInertia;	// world space
	Vec3	otherR;				// contact point relative to the other body's centre of mass
	Vec3	otherVelocity;		// velocity of the other body at the contact point
	bool	otherSleeping;		// the other body is dynamic but at rest
	bool	newContact;			// not carried over from the last step: only a new touch bounces
	// warm start in, accumulated impulses out (normal, two tangents, two rolling, spin)
	float	impulse[6];
	float	pseudoImpulse;
	// derived during the solve
	Vec3	r;
	Vec3	tangent[2];
	float	normalMass;
	float	tangentMass[2];
	float	angularMass[3];		// rolling about tangent[0], tangent[1], spinning about normal
	float	velocityBias;		// speculative approach allowance or bounce target
	float	approachSpeed;		// normal speed before the solve (negative when closing)
	bool	sharesMass;			// solved as a two-body contact: the caller must apply the reaction
	bool	velocityRows;		// false for deep overlap: position recovery only
	bool	impact;				// approached above the bounce threshold and reached within the step
	float	grip;				// share of the Coulomb coefficient this contact has in this step
};

inline float EffectiveMass( const rigidBody_t &body, const rigidContact_t &c, const Vec3 &direction, bool withOther ) {
	const Vec3 rn = c.r.Cross( direction );
	float k = body.invMass + direction.Dot( body.invInertia.Apply( rn ).Cross( c.r ) );
	if ( withOther && c.otherInvMass > 0.0f ) {
		const Vec3 on = c.otherR.Cross( direction );
		k += c.otherInvMass + direction.Dot( c.otherInvInertia.Apply( on ).Cross( c.otherR ) );
	}
	return k > 1e-12f ? 1.0f / k : 0.0f;
}

inline float AngularMass( const rigidBody_t &body, const Vec3 &axis ) {
	const float k = axis.Dot( body.invInertia.Apply( axis ) );
	return k > 1e-12f ? 1.0f / k : 0.0f;
}

inline Vec3 ContactVelocity( const rigidBody_t &body, const rigidContact_t &c ) {
	return body.linearVelocity + body.angularVelocity.Cross( c.r ) - c.otherVelocity;
}

inline void ApplyBodyImpulse( rigidBody_t &body, const Vec3 &r, const Vec3 &impulse ) {
	body.linearVelocity += impulse * body.invMass;
	body.angularVelocity += body.invInertia.Apply( r.Cross( impulse ) );
}

inline void ApplyBodyAngularImpulse( rigidBody_t &body, const Vec3 &angularImpulse ) {
	body.angularVelocity += body.invInertia.Apply( angularImpulse );
}

// Sequential impulses over a body's contacts with warm starting, Coulomb friction
// (a box approximation of the cone), rolling and spinning resistance, restitution
// above a threshold and speculative approach limits for gaps. Penetration is
// removed by a separate pseudo-velocity pass that moves the body without adding
// velocity. On return each contact's impulse[] holds the accumulated impulses for
// the next step's warm start; the caller applies their reactions to other bodies.
inline void SolveRigidContacts( rigidBody_t &body, rigidContact_t *contacts, int count, const rigidSolverSettings_t &s ) {
	const float dt = s.timeStep > 1e-6f ? s.timeStep : 1e-6f;
	const float invDt = 1.0f / dt;

	for ( int i = 0; i < count; i++ ) {
		rigidContact_t &c = contacts[i];
		c.r = c.point - body.centerOfMass;
		TangentBasis( c.normal, c.tangent[0], c.tangent[1] );
		c.approachSpeed = ContactVelocity( body, c ).Dot( c.normal );

		// restitution for impacts that land within this step, otherwise a
		// speculative allowance: a body may close a gap but never pass through it
		const bool impact = c.approachSpeed < -s.bounceThreshold && c.separation + c.approachSpeed * dt <= s.slop;

		// A contact with another dynamic body shares the impulse by both masses and
		// the caller hands the other body the reaction, which conserves momentum and
		// never adds energy. A sleeping body that only carries a resting load is
		// treated as fixed and left asleep, so supports stay quiet under stacks.
		// Treating an awake light body as fixed and then giving it the full reaction
		// would amplify energy by the mass ratio, so that is never done.
		c.sharesMass = c.otherInvMass > 0.0f && ( !c.otherSleeping || impact );
		// Bodies that start deeply inside one another (spawned overlapping) have no
		// meaningful contact normal: push them apart by position only.
		c.velocityRows = c.separation >= -s.deepPenetration;
		c.normalMass = c.velocityRows ? EffectiveMass( body, c, c.normal, c.sharesMass ) : 0.0f;
		c.tangentMass[0] = c.velocityRows ? EffectiveMass( body, c, c.tangent[0], c.sharesMass ) : 0.0f;
		c.tangentMass[1] = c.velocityRows ? EffectiveMass( body, c, c.tangent[1], c.sharesMass ) : 0.0f;
		c.angularMass[0] = AngularMass( body, c.tangent[0] );
		c.angularMass[1] = AngularMass( body, c.tangent[1] );
		c.angularMass[2] = AngularMass( body, c.normal );
		c.impact = impact;
		c.grip = impact ? s.impactFriction : 1.0f;
		if ( impact && ( c.newContact || ( s.bounceResting && c.otherInvMass <= 0.0f ) ) && s.restitution > 0.0f ) {
			c.velocityBias = -s.restitution * c.approachSpeed;
		} else if ( c.separation > s.slop ) {
			// close the gap down to the slop; a body resting within the slop does not
			// free-fall into it (that turned resting props into endless micro-bounces)
			c.velocityBias = -( c.separation - s.slop ) * invDt;
		} else {
			c.velocityBias = 0.0f;
		}

		// warm start
		if ( s.warmStart > 0.0f ) {
			for ( int k = 0; k < 6; k++ ) {
				c.impulse[k] *= s.warmStart;
			}
			const Vec3 linear = c.normal * c.impulse[0] + c.tangent[0] * c.impulse[1] + c.tangent[1] * c.impulse[2];
			ApplyBodyImpulse( body, c.r, linear );
			ApplyBodyAngularImpulse( body, c.tangent[0] * c.impulse[3] + c.tangent[1] * c.impulse[4] + c.normal * c.impulse[5] );
		} else {
			for ( int k = 0; k < 6; k++ ) {
				c.impulse[k] = 0.0f;
			}
		}
		c.pseudoImpulse = 0.0f;
	}

	for ( int iteration = 0; iteration < s.iterations; iteration++ ) {
		for ( int i = 0; i < count; i++ ) {
			rigidContact_t &c = contacts[i];

			if ( !c.velocityRows ) {
				continue;
			}

			// rolling and spinning resistance, bounded by the normal impulse
			{
				const float lever = MaxF( c.r.Length(), 1.0f );
				const float rollLimit = c.grip * s.rollingFriction * lever * c.impulse[0];
				const float spinLimit = c.grip * s.spinningFriction * lever * c.impulse[0];
				const Vec3 axes[3] = { c.tangent[0], c.tangent[1], c.normal };
				for ( int k = 0; k < 3; k++ ) {
					if ( c.angularMass[k] <= 0.0f ) {
						continue;
					}
					const float limit = k < 2 ? rollLimit : spinLimit;
					const float speed = body.angularVelocity.Dot( axes[k] );
					const float previous = c.impulse[3 + k];
					const float value = Clamp( previous - speed * c.angularMass[k], -limit, limit );
					c.impulse[3 + k] = value;
					ApplyBodyAngularImpulse( body, axes[k] * ( value - previous ) );
				}
			}

			// friction, bounded by the normal impulse
			const float frictionLimit = c.grip * s.friction * c.impulse[0];
			for ( int k = 0; k < 2; k++ ) {
				if ( c.tangentMass[k] <= 0.0f ) {
					continue;
				}
				const float speed = ContactVelocity( body, c ).Dot( c.tangent[k] );
				const float previous = c.impulse[1 + k];
				const float value = Clamp( previous - speed * c.tangentMass[k], -frictionLimit, frictionLimit );
				c.impulse[1 + k] = value;
				ApplyBodyImpulse( body, c.r, c.tangent[k] * ( value - previous ) );
			}

			// non-penetration
			if ( c.normalMass > 0.0f ) {
				const float speed = ContactVelocity( body, c ).Dot( c.normal );
				const float previous = c.impulse[0];
				const float value = MaxF( previous + ( c.velocityBias - speed ) * c.normalMass, 0.0f );
				c.impulse[0] = value;
				ApplyBodyImpulse( body, c.r, c.normal * ( value - previous ) );
			}
		}
	}

	// position-only penetration recovery
	body.pseudoLinear = Vec3();
	body.pseudoAngular = Vec3();
	bool penetrating = false;
	for ( int i = 0; i < count; i++ ) {
		if ( contacts[i].separation < -s.slop ) {
			penetrating = true;
			break;
		}
	}
	if ( penetrating ) {
		for ( int iteration = 0; iteration < s.iterations; iteration++ ) {
			for ( int i = 0; i < count; i++ ) {
				rigidContact_t &c = contacts[i];
				if ( c.separation >= -s.slop ) {
					continue;
				}
				const float pseudoMass = c.velocityRows ? c.normalMass : EffectiveMass( body, c, c.normal, false );
				if ( pseudoMass <= 0.0f ) {
					continue;
				}
				const float target = MinF( s.recoveryRate * ( -c.separation - s.slop ) * invDt, s.maxRecoverySpeed );
				const float speed = ( body.pseudoLinear + body.pseudoAngular.Cross( c.r ) ).Dot( c.normal );
				const float previous = c.pseudoImpulse;
				const float value = MaxF( previous + ( target - speed ) * pseudoMass, 0.0f );
				c.pseudoImpulse = value;
				const Vec3 impulse = c.normal * ( value - previous );
				body.pseudoLinear += impulse * body.invMass;
				body.pseudoAngular += body.invInertia.Apply( c.r.Cross( impulse ) );
			}
		}
	}
}

// Fraction of the step the body may move before any contact point would close
// past its gap plus the slop: the time of impact of the solved velocities
// (pseudo velocities included), so a solve that could not fully stop a fast body
// never carries it through a surface. 1 when every contact stays open.
inline float MotionFraction( const rigidBody_t &body, const rigidContact_t *contacts, int count, float timeStep, float slop ) {
	float fraction = 1.0f;
	for ( int i = 0; i < count; i++ ) {
		const rigidContact_t &c = contacts[i];
		// r from the point: contacts left out of the solve (non-leading) still limit the motion
		const Vec3 r = c.point - body.centerOfMass;
		const Vec3 velocity = body.linearVelocity + body.pseudoLinear + ( body.angularVelocity + body.pseudoAngular ).Cross( r ) - c.otherVelocity;
		const float closing = -velocity.Dot( c.normal ) * timeStep;
		if ( closing <= 1e-6f ) {
			continue;
		}
		const float room = MaxF( c.separation, 0.0f ) + slop;
		if ( closing > room ) {
			fraction = MinF( fraction, room / closing );
		}
	}
	return fraction;
}

// Marks the leading contacts against each surface: contacts on the same entity
// and clip model with nearly the same normal whose gap exceeds the smallest gap
// by more than depthTolerance cannot be reached first and only slow the solve.
// sameSurface( i, j ) compares the caller's keys; keep[] receives the verdicts.
template< typename keyFunc_t >
inline int MarkLeadingContacts( const rigidContact_t *contacts, int count, float depthTolerance, keyFunc_t sameSurface, bool *keep ) {
	int kept = 0;
	for ( int i = 0; i < count; i++ ) {
		float leading = contacts[i].separation;
		for ( int j = 0; j < count; j++ ) {
			if ( j != i && sameSurface( i, j ) && contacts[j].normal.Dot( contacts[i].normal ) > 0.99f ) {
				leading = MinF( leading, contacts[j].separation );
			}
		}
		keep[i] = contacts[i].separation <= leading + depthTolerance;
		kept += keep[i] ? 1 : 0;
	}
	return kept;
}

// Total impulse a contact delivered to the body (the other body receives its negative).
inline Vec3 ContactImpulse( const rigidContact_t &c ) {
	return c.normal * c.impulse[0] + c.tangent[0] * c.impulse[1] + c.tangent[1] * c.impulse[2];
}

// ---------------------------------------------------------------------------
// Warm-start cache: last step's impulses, matched by feature and position
// ---------------------------------------------------------------------------

struct cachedContact_t {
	Vec3	point;
	Vec3	normal;
	Vec3	localPoint;			// the contact in the other body's frame (its origin and axis
	Vec3	localNormal;		// when it was cached), so the plane moves with a moving body
	int		entityNum;
	int		id;
	int		body;				// articulated figure body index (0 for rigid bodies)
	int		age;				// steps since a sweep last reported it (persistent manifold)
	int		type;				// the collision model's contact type, 0 when unknown
	int		feature;			// the body feature that made the contact (vertex, edge or polygon), -1 when unknown
	float	impulse[6];
};

struct contactKey_t {
	Vec3	point;
	Vec3	normal;
	int		entityNum;
	int		id;
	int		body;
};

// Returns the cache index matching a new contact, or -1. A match needs the same
// entity, clip model and body, a point within mergeDistance and a normal within
// about 25 degrees.
inline int MatchCachedContact( const contactKey_t &key, const cachedContact_t *cache, int count, float mergeDistance ) {
	int best = -1;
	float bestDistance = mergeDistance * mergeDistance;
	for ( int i = 0; i < count; i++ ) {
		const cachedContact_t &c = cache[i];
		if ( c.entityNum != key.entityNum || c.id != key.id || c.body != key.body ) {
			continue;
		}
		if ( c.normal.Dot( key.normal ) < 0.9f ) {
			continue;
		}
		const float distance = ( c.point - key.point ).LengthSqr();
		if ( distance <= bestDistance ) {
			bestDistance = distance;
			best = i;
		}
	}
	return best;
}

// ---------------------------------------------------------------------------
// Speculative separation from trace-model vertices
// ---------------------------------------------------------------------------

// Signed distance from a convex vertex set to a contact plane (normal . x = dist
// at the contact point): the smallest vertex height above the plane. Negative
// means overlap. vertices are world-space x, y, z triples.
inline float PlaneSeparation( const float *vertices, int count, const Vec3 &normal, const Vec3 &point ) {
	if ( count <= 0 ) {
		return 0.0f;
	}
	const float planeDist = normal.Dot( point );
	float lowest = 1e30f;
	for ( int i = 0; i < count; i++ ) {
		const float h = normal.Dot( Vec3( vertices + i * 3 ) ) - planeDist;
		lowest = MinF( lowest, h );
	}
	return lowest;
}

// Gap for one contact of a translation sweep. A sweep reports where a feature of
// the moving body met a surface, after travelling some distance along the sweep
// direction. The body vertex that produced a vertex contact lies on the line
// back from the contact point along the sweep, so the vertex nearest that line
// (ahead of the point) gives the gap exactly. Edge contacts land near the line
// too; when no vertex is close, the lowest vertex within reach of the point is
// used. A plane alone is not enough: for a long body resting on a curved or
// faceted surface the lowest vertex overall can belong to a different contact.
inline float SweptContactSeparation( const float *vertices, int count, const Vec3 &normal, const Vec3 &point,
									const Vec3 &sweepDir, float reach, Vec3 *pointNow = nullptr );

// A swept contact moved to where the body is now. The sweep reports the point
// where the moving body meets the surface, which for a fast body lies far ahead
// of it (a 1600 units/s ragdoll limb's sweep reaches 27 units). A constraint
// row applied there has the wrong lever arm: it couples into rotation and its
// friction brakes motion the body never had. The row belongs at the foot, on
// the surface, of the body vertex that made the contact.
inline Vec3 ContactPointNow( const float *vertices, int count, const Vec3 &normal, const Vec3 &point,
							const Vec3 &sweepDir, float reach, float &separation ) {
	Vec3 now = point;
	separation = SweptContactSeparation( vertices, count, normal, point, sweepDir, reach, &now );
	return now;
}

inline float SweptContactSeparation( const float *vertices, int count, const Vec3 &normal, const Vec3 &point,
									const Vec3 &sweepDir, float reach, Vec3 *pointNow ) {
	if ( count <= 0 ) {
		return 0.0f;
	}
	const float planeDist = normal.Dot( point );
	int best = -1;
	float bestError = 1e30f;
	for ( int i = 0; i < count; i++ ) {
		const Vec3 rel = Vec3( vertices + i * 3 ) - point;
		const float travel = -rel.Dot( sweepDir );			// distance along the sweep to reach the point
		if ( travel < -0.5f ) {
			continue;										// past the point already: not the leading feature
		}
		const float error = ( rel + sweepDir * travel ).LengthSqr();
		if ( error < bestError ) {
			bestError = error;
			best = i;
		}
	}
	if ( best < 0 ) {
		const float separation = PlaneSeparation( vertices, count, normal, point );
		if ( pointNow ) {
			*pointNow = point;
		}
		return separation;
	}
	const Vec3 vertex( vertices + best * 3 );
	float separation = normal.Dot( vertex ) - planeDist;
	const float edgeTolerance = MaxF( 1.0f, 0.1f * reach );
	if ( bestError > edgeTolerance * edgeTolerance ) {
		// an edge or face contact: its point lies between vertices near the
		// line, so take the lowest of those (conservative: a smaller gap)
		const float edgeReach = MaxF( edgeTolerance, std::sqrt( bestError ) * 2.0f );
		for ( int i = 0; i < count; i++ ) {
			const Vec3 v( vertices + i * 3 );
			if ( ( v - vertex ).LengthSqr() <= edgeReach * edgeReach ) {
				separation = MinF( separation, normal.Dot( v ) - planeDist );
			}
		}
	}
	if ( pointNow ) {
		*pointNow = vertex - normal * ( normal.Dot( vertex ) - planeDist );		// the vertex's foot on the surface
	}
	return separation;
}

// Index of the vertex lowest above a plane (normal, point on it), or -1.
inline int LowestVertex( const float *vertices, int count, const Vec3 &normal, const Vec3 &point, float &height ) {
	int best = -1;
	height = 1e30f;
	const float planeDist = normal.Dot( point );
	for ( int i = 0; i < count; i++ ) {
		const float h = normal.Dot( Vec3( vertices + i * 3 ) ) - planeDist;
		if ( h < height ) {
			height = h;
			best = i;
		}
	}
	return best;
}

// Persistent manifold: a contact plane from an earlier step that this step's
// sweep did not report (the body now moves along or away from it, or turns a
// corner into it) is kept while the body stays near the plane and near where it
// touched. Returns true and fills point/separation when the contact survives.
inline bool RenewPersistentContact( const float *vertices, int count, const Vec3 &normal, const Vec3 &cachedPoint,
									float margin, float maxDrift, Vec3 &point, float &separation ) {
	// the lowest vertex whose foot on the plane stays near where the contact was
	const float planeDist = normal.Dot( cachedPoint );
	float height = 1e30f;
	int lowest = -1;
	for ( int i = 0; i < count; i++ ) {
		const Vec3 v( vertices + i * 3 );
		const float h = normal.Dot( v ) - planeDist;
		const Vec3 foot = v - normal * h;
		if ( ( foot - cachedPoint ).LengthSqr() > maxDrift * maxDrift ) {
			continue;
		}
		if ( h < height ) {
			height = h;
			lowest = i;
		}
	}
	if ( lowest < 0 || height > margin || height < -margin * 4.0f ) {
		return false;
	}
	point = Vec3( vertices + lowest * 3 ) - normal * height;
	separation = height;
	return true;
}

// ---------------------------------------------------------------------------
// Sleeping
// ---------------------------------------------------------------------------

struct sleepSettings_t {
	float	linearSpeed;		// below this a body is quiet (units per second)
	float	angularSpeed;		// radians per second
	int		quietFrames;		// consecutive quiet frames needed to sleep
};

// Counts consecutive quiet frames; returns true once a supported body has been
// quiet long enough. An unsupported (falling or flying) body never sleeps.
inline bool UpdateSleep( int &quietFrames, float linearSpeed, float angularSpeed, bool supported, const sleepSettings_t &s ) {
	if ( !supported || linearSpeed > s.linearSpeed || angularSpeed > s.angularSpeed ) {
		quietFrames = 0;
		return false;
	}
	quietFrames++;
	return quietFrames >= s.quietFrames;
}

// ---------------------------------------------------------------------------
// Energy guard
// ---------------------------------------------------------------------------

// Scale factor that limits kinetic energy to the allowed value (1 when within it).
inline float EnergyScale( float energy, float allowed ) {
	if ( !( energy > allowed ) || energy <= 0.0f ) {
		return 1.0f;
	}
	if ( allowed <= 0.0f ) {
		return 0.0f;
	}
	return std::sqrt( allowed / energy );
}

// The energy guard for a body set that moves together (an articulated figure).
// Scaling every velocity uniformly would also scale the centre-of-mass motion,
// which no internal force can change: a splashed corpse lost most of its
// momentum that way. Excess energy is taken from the motion relative to the
// centre of mass first; only an excess larger than that relative energy also
// scales the shared motion. energy is the set's kinetic energy, comEnergy the
// part carried by its centre of mass (0.5 M V^2). relativeScale multiplies
// every body's velocity relative to V (and its angular velocity); comScale
// multiplies V itself.
struct energySplit_t {
	float	relativeScale;
	float	comScale;
};

inline energySplit_t EnergyScaleKeepingMomentum( float energy, float comEnergy, float allowed ) {
	energySplit_t split;
	split.relativeScale = 1.0f;
	split.comScale = 1.0f;
	if ( !( energy > allowed ) || energy <= 0.0f ) {
		return split;
	}
	comEnergy = MinF( MaxF( comEnergy, 0.0f ), energy );
	const float relative = energy - comEnergy;
	if ( allowed >= comEnergy && relative > 0.0f ) {
		split.relativeScale = std::sqrt( MaxF( 0.0f, allowed - comEnergy ) / relative );
		return split;
	}
	// even the shared motion exceeds the allowance: remove the relative motion
	// and scale what remains
	split.relativeScale = 0.0f;
	split.comScale = ( allowed > 0.0f && comEnergy > 0.0f ) ? std::sqrt( allowed / comEnergy ) : 0.0f;
	return split;
}

// The friction a contact may apply during an impact. Legacy figures resolve a
// fast touch without friction (the collision impulse and the closing-velocity
// projection are both normal-only); Coulomb friction on an impact-sized normal
// force instead stops a ragdoll thrown along a slope dead within one step.
// During an impact the friction rows are therefore bounded by the friction
// coefficient times the body's share of its resting load on that surface,
// which is what friction does once the body slides.
inline float ImpactFrictionLoad( float mass, float gravity, float normalDotUp ) {
	return mass * gravity * MaxF( 0.0f, normalDotUp );
}

// ---------------------------------------------------------------------------
// Impulse guard (articulated figures)
// ---------------------------------------------------------------------------
//
// A hit on one body of a figure delivers the legacy linear momentum J. Legacy
// gives the struck body all of it, so a light limb can leave at J / m and drag
// the figure across the map. The guard keeps the struck body's velocity change
// at or below guardSpeed (never above the legacy J / m) by giving it a fraction
// f of the impulse at the hit point and spreading the rest over every body by
// mass: each body then gains (1 - f) J / M. The struck body's change becomes
// f J / m + (1 - f) J / M. Total linear momentum stays exactly J.
struct impulseSplit_t {
	float	struckFraction;		// f: share applied at the hit point
	float	struckDeltaV;		// resulting velocity change of the struck body
	float	legacyDeltaV;		// J / m, what legacy would have given it
	bool	clamped;
};

inline impulseSplit_t SplitImpulse( float impulse, float struckMass, float totalMass, float guardSpeed ) {
	impulseSplit_t split;
	split.struckFraction = 1.0f;
	split.clamped = false;
	split.legacyDeltaV = struckMass > 0.0f ? impulse / struckMass : 0.0f;
	split.struckDeltaV = split.legacyDeltaV;
	if ( !( impulse > 0.0f ) || !( struckMass > 0.0f ) || !( totalMass > struckMass * 1.0001f ) || !( guardSpeed > 0.0f ) ) {
		return split;
	}
	if ( split.legacyDeltaV <= guardSpeed ) {
		return split;
	}
	const float uniform = impulse / totalMass;		// every body's change if spread fully
	const float denominator = split.legacyDeltaV - uniform;
	float f = denominator > 0.0f ? ( guardSpeed - uniform ) / denominator : 0.0f;
	f = Clamp( f, 0.0f, 1.0f );
	split.struckFraction = f;
	split.struckDeltaV = f * split.legacyDeltaV + ( 1.0f - f ) * uniform;
	split.clamped = true;
	return split;
}

} // namespace OpenQ4Physics
