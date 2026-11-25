// Copyright (C) 2026 DarkMatter Productions
//
// PosedBoundsCore.h: a skinned mesh's bounds in one pose from its exact
// bounds in another, widened by the distance the joints travelled. The
// renderer answers the bounds of poses drawn between game tics this way
// (idRenderModelMD5::BoundsFromReferencePose), so the answer must always hold
// the skinned mesh.

#include "src/renderer/PosedBoundsCore.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

static int failures = 0;

static void Check( bool condition, const char *message ) {
	if ( condition ) {
		return;
	}
	std::fprintf( stderr, "PosedBoundsCoreTest: %s\n", message );
	failures++;
}

// A small deterministic generator, so a failure reproduces.
struct Random {
	unsigned int state;
	explicit Random( unsigned int seed ) : state( seed ) {}
	float Unit() {
		state = state * 1664525u + 1013904223u;
		return static_cast<float>( ( state >> 8 ) & 0xffffff ) / 16777216.0f;
	}
	float Range( float lo, float hi ) { return lo + ( hi - lo ) * Unit(); }
};

struct Joint {
	float m[POSED_BOUNDS_JOINT_FLOATS];
};

struct Weight {
	int		joint;
	float	weight;
	float	offset[3];
};

struct Mesh {
	std::vector<Weight>	weights;
	std::vector<int>	firstWeight;	// per vertex, plus one past the end
};

// Rotation about an arbitrary axis by 'angle' with translation t, row major.
static Joint MakeJoint( float ax, float ay, float az, float angle, float tx, float ty, float tz ) {
	const float length = std::sqrt( ax * ax + ay * ay + az * az );
	ax /= length; ay /= length; az /= length;
	const float c = std::cos( angle ), s = std::sin( angle ), k = 1.0f - c;
	Joint joint;
	joint.m[0] = c + ax * ax * k;		joint.m[1] = ax * ay * k - az * s;	joint.m[2] = ax * az * k + ay * s;	joint.m[3] = tx;
	joint.m[4] = ay * ax * k + az * s;	joint.m[5] = c + ay * ay * k;		joint.m[6] = ay * az * k - ax * s;	joint.m[7] = ty;
	joint.m[8] = az * ax * k - ay * s;	joint.m[9] = az * ay * k + ax * s;	joint.m[10] = c + az * az * k;		joint.m[11] = tz;
	return joint;
}

static Joint RandomJoint( Random &random, float reach ) {
	return MakeJoint( random.Range( -1.0f, 1.0f ), random.Range( -1.0f, 1.0f ), random.Range( 0.1f, 1.0f ),
		random.Range( -3.1f, 3.1f ), random.Range( -reach, reach ), random.Range( -reach, reach ), random.Range( -reach, reach ) );
}

// A pose near 'from': each joint turned and moved a little further.
static Joint Nudge( Random &random, const Joint &from, float turn, float move ) {
	const Joint delta = MakeJoint( random.Range( -1.0f, 1.0f ), random.Range( -1.0f, 1.0f ), random.Range( 0.1f, 1.0f ),
		random.Range( -turn, turn ), 0.0f, 0.0f, 0.0f );
	Joint to;
	for ( int row = 0; row < 3; row++ ) {
		for ( int column = 0; column < 3; column++ ) {
			to.m[row * 4 + column] = delta.m[row * 4 + 0] * from.m[0 * 4 + column]
				+ delta.m[row * 4 + 1] * from.m[1 * 4 + column] + delta.m[row * 4 + 2] * from.m[2 * 4 + column];
		}
		to.m[row * 4 + 3] = from.m[row * 4 + 3] + random.Range( -move, move );
	}
	return to;
}

static Mesh RandomMesh( Random &random, int numJoints, int numVerts, bool normalized ) {
	Mesh mesh;
	for ( int vertex = 0; vertex < numVerts; vertex++ ) {
		mesh.firstWeight.push_back( static_cast<int>( mesh.weights.size() ) );
		const int count = 1 + static_cast<int>( random.Unit() * 3.99f );
		float total = 0.0f;
		const size_t first = mesh.weights.size();
		for ( int i = 0; i < count; i++ ) {
			Weight weight;
			weight.joint = static_cast<int>( random.Unit() * numJoints ) % numJoints;
			weight.weight = random.Range( 0.05f, 1.0f );
			weight.offset[0] = random.Range( -30.0f, 30.0f );
			weight.offset[1] = random.Range( -30.0f, 30.0f );
			weight.offset[2] = random.Range( -30.0f, 30.0f );
			total += weight.weight;
			mesh.weights.push_back( weight );
		}
		if ( normalized ) {
			for ( size_t i = first; i < mesh.weights.size(); i++ ) {
				mesh.weights[i].weight /= total;
			}
		}
	}
	mesh.firstWeight.push_back( static_cast<int>( mesh.weights.size() ) );
	return mesh;
}

// The boxes as the renderer builds them: from weighted offsets divided by
// their weight, which is how the skinning stream stores them.
static float BuildBoxes( const Mesh &mesh, std::vector<posedBoundsJointBox_t> &boxes ) {
	for ( size_t i = 0; i < boxes.size(); i++ ) {
		PosedBoundsCore_ClearBox( boxes[i] );
	}
	float weightSum = 1.0f;
	for ( size_t vertex = 0; vertex + 1 < mesh.firstWeight.size(); vertex++ ) {
		float sum = 0.0f;
		for ( int i = mesh.firstWeight[vertex]; i < mesh.firstWeight[vertex + 1]; i++ ) {
			const Weight &weight = mesh.weights[i];
			const float scaled[3] = { weight.offset[0] * weight.weight, weight.offset[1] * weight.weight, weight.offset[2] * weight.weight };
			const float inverse = 1.0f / weight.weight;
			const float offset[3] = { scaled[0] * inverse, scaled[1] * inverse, scaled[2] * inverse };
			PosedBoundsCore_AddOffset( boxes[weight.joint], offset );
			sum += std::fabs( weight.weight );
		}
		if ( sum > weightSum ) {
			weightSum = sum;
		}
	}
	for ( size_t i = 0; i < boxes.size(); i++ ) {
		PosedBoundsCore_FinishBox( boxes[i] );
	}
	return weightSum;
}

// The skinned mesh's exact bounds, in double precision.
static void Skin( const Mesh &mesh, const std::vector<Joint> &pose, double mins[3], double maxs[3] ) {
	for ( int axis = 0; axis < 3; axis++ ) {
		mins[axis] = 1.0e30;
		maxs[axis] = -1.0e30;
	}
	for ( size_t vertex = 0; vertex + 1 < mesh.firstWeight.size(); vertex++ ) {
		double position[3] = { 0.0, 0.0, 0.0 };
		for ( int i = mesh.firstWeight[vertex]; i < mesh.firstWeight[vertex + 1]; i++ ) {
			const Weight &weight = mesh.weights[i];
			const float *m = pose[weight.joint].m;
			for ( int axis = 0; axis < 3; axis++ ) {
				position[axis] += static_cast<double>( weight.weight ) * ( static_cast<double>( m[axis * 4 + 0] ) * weight.offset[0]
					+ static_cast<double>( m[axis * 4 + 1] ) * weight.offset[1] + static_cast<double>( m[axis * 4 + 2] ) * weight.offset[2]
					+ static_cast<double>( m[axis * 4 + 3] ) );
			}
		}
		for ( int axis = 0; axis < 3; axis++ ) {
			if ( position[axis] < mins[axis] ) { mins[axis] = position[axis]; }
			if ( position[axis] > maxs[axis] ) { maxs[axis] = position[axis]; }
		}
	}
}

// The widened bounds of the 'from' pose must hold the mesh in the 'to' pose.
static bool Holds( const Mesh &mesh, const std::vector<posedBoundsJointBox_t> &boxes, float weightSum,
		const std::vector<Joint> &from, const std::vector<Joint> &to, float *furthestOut = NULL ) {
	double fromMins[3], fromMaxs[3], toMins[3], toMaxs[3];
	Skin( mesh, from, fromMins, fromMaxs );
	Skin( mesh, to, toMins, toMaxs );
	float travel[3];
	const float furthest = PosedBoundsCore_Travel( boxes.data(), static_cast<int>( boxes.size() ),
		from[0].m, to[0].m, weightSum, travel );
	if ( furthestOut != NULL ) {
		*furthestOut = furthest;
	}
	for ( int axis = 0; axis < 3; axis++ ) {
		if ( toMins[axis] < fromMins[axis] - travel[axis] || toMaxs[axis] > fromMaxs[axis] + travel[axis] ) {
			return false;
		}
	}
	return true;
}

static void TestRandomPoses() {
	Random random( 20261125u );
	int checked = 0;
	for ( int trial = 0; trial < 400; trial++ ) {
		const int numJoints = 1 + static_cast<int>( random.Unit() * 40.0f );
		const bool normalized = ( trial % 5 ) != 4;
		const Mesh mesh = RandomMesh( random, numJoints, 60, normalized );
		std::vector<posedBoundsJointBox_t> boxes( numJoints );
		const float weightSum = BuildBoxes( mesh, boxes );
		std::vector<Joint> from( numJoints ), to( numJoints );
		for ( int i = 0; i < numJoints; i++ ) {
			from[i] = RandomJoint( random, 80.0f );
		}
		// a tic of ordinary motion, a violent tic and an unrelated pose
		const float turns[3] = { 0.05f, 0.6f, 3.1f };
		const float moves[3] = { 0.5f, 8.0f, 200.0f };
		for ( int kind = 0; kind < 3; kind++ ) {
			for ( int i = 0; i < numJoints; i++ ) {
				to[i] = Nudge( random, from[i], turns[kind], moves[kind] );
			}
			if ( !Holds( mesh, boxes, weightSum, from, to ) ) {
				Check( false, "a skinned mesh left its widened bounds" );
				return;
			}
			checked++;
		}
	}
	Check( checked == 1200, "every random pose pair was checked" );
}

static void TestRotationNeedsTheBox() {
	// One joint, two vertices on opposite sides of it, a quarter turn: both
	// swap axes, so the travel is the offsets' length, which only the box's
	// half size knows (its centre is the joint itself).
	Mesh mesh;
	for ( int i = 0; i < 2; i++ ) {
		Weight weight;
		weight.joint = 0;
		weight.weight = 1.0f;
		weight.offset[0] = i == 0 ? 10.0f : -10.0f;
		weight.offset[1] = 0.0f;
		weight.offset[2] = 0.0f;
		mesh.firstWeight.push_back( i );
		mesh.weights.push_back( weight );
	}
	mesh.firstWeight.push_back( 2 );
	std::vector<posedBoundsJointBox_t> boxes( 1 );
	const float weightSum = BuildBoxes( mesh, boxes );
	Check( weightSum == 1.0f, "a normalized mesh has weight sum 1" );
	const std::vector<Joint> from( 1, MakeJoint( 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f ) );
	const std::vector<Joint> to( 1, MakeJoint( 0.0f, 0.0f, 1.0f, 1.5707963f, 0.0f, 0.0f, 0.0f ) );
	float travel[3];
	PosedBoundsCore_Travel( boxes.data(), 1, from[0].m, to[0].m, weightSum, travel );
	Check( travel[0] >= 10.0f && travel[1] >= 10.0f, "a quarter turn carries a 10 unit offset 10 units on both axes" );
	Check( travel[0] < 10.2f && travel[1] < 10.2f && travel[2] < 0.1f, "the travel is no wider than the motion plus slack" );
	Check( Holds( mesh, boxes, weightSum, from, to ), "the quarter turn stays inside the widened bounds" );
}

static void TestOffCentreBox() {
	// Every vertex of the joint sits about 50 units out along its x axis, so
	// the box's centre, not its half size, carries the travel of a quarter
	// turn: 50 units on x and on y.
	Random random( 11u );
	Mesh mesh;
	for ( int i = 0; i < 24; i++ ) {
		Weight weight;
		weight.joint = 0;
		weight.weight = 1.0f;
		weight.offset[0] = random.Range( 48.0f, 52.0f );
		weight.offset[1] = random.Range( -1.0f, 1.0f );
		weight.offset[2] = random.Range( -1.0f, 1.0f );
		mesh.firstWeight.push_back( i );
		mesh.weights.push_back( weight );
	}
	mesh.firstWeight.push_back( 24 );
	std::vector<posedBoundsJointBox_t> boxes( 1 );
	const float weightSum = BuildBoxes( mesh, boxes );
	const std::vector<Joint> from( 1, MakeJoint( 0.0f, 0.0f, 1.0f, 0.0f, 3.0f, -2.0f, 1.0f ) );
	const std::vector<Joint> to( 1, MakeJoint( 0.0f, 0.0f, 1.0f, 1.5707963f, 3.0f, -2.0f, 1.0f ) );
	float travel[3];
	PosedBoundsCore_Travel( boxes.data(), 1, from[0].m, to[0].m, weightSum, travel );
	Check( travel[0] >= 48.0f && travel[1] >= 48.0f, "a quarter turn carries an off-centre box by its distance from the joint" );
	Check( travel[0] < 56.0f && travel[1] < 56.0f && travel[2] < 0.1f, "the off-centre travel is no wider than the box's far corner" );
	Check( Holds( mesh, boxes, weightSum, from, to ), "the off-centre box stays inside the widened bounds" );
}

static void TestTravelIsTight() {
	Random random( 7u );
	const int numJoints = 12;
	const Mesh mesh = RandomMesh( random, numJoints, 80, true );
	std::vector<posedBoundsJointBox_t> boxes( numJoints );
	const float weightSum = BuildBoxes( mesh, boxes );
	std::vector<Joint> from( numJoints );
	for ( int i = 0; i < numJoints; i++ ) {
		from[i] = RandomJoint( random, 50.0f );
	}

	// the same pose travels nowhere
	float travel[3];
	float furthest = PosedBoundsCore_Travel( boxes.data(), numJoints, from[0].m, from[0].m, weightSum, travel );
	Check( furthest <= POSED_BOUNDS_SLACK * 1.01f, "an unchanged pose widens the bounds by the slack alone" );

	// every joint moved by the same vector: the mesh moves by exactly that
	std::vector<Joint> to = from;
	for ( int i = 0; i < numJoints; i++ ) {
		to[i].m[3] += 3.0f;
		to[i].m[7] -= 5.0f;
	}
	furthest = PosedBoundsCore_Travel( boxes.data(), numJoints, from[0].m, to[0].m, weightSum, travel );
	Check( travel[0] >= 3.0f && travel[0] < 3.05f, "a 3 unit shift widens x by 3 units" );
	Check( travel[1] >= 5.0f && travel[1] < 5.05f, "a 5 unit shift widens y by 5 units" );
	Check( travel[2] < 0.05f, "an axis nothing moved along is widened by the slack alone" );
	Check( furthest == travel[1], "the largest axis is returned" );

	// a joint no weight names can do anything
	std::vector<posedBoundsJointBox_t> withSpare = boxes;
	posedBoundsJointBox_t spare;
	PosedBoundsCore_ClearBox( spare );
	withSpare.push_back( spare );
	std::vector<Joint> fromSpare = from, toSpare = from;
	fromSpare.push_back( RandomJoint( random, 10.0f ) );
	toSpare.push_back( RandomJoint( random, 5000.0f ) );
	furthest = PosedBoundsCore_Travel( withSpare.data(), numJoints + 1, fromSpare[0].m, toSpare[0].m, weightSum, travel );
	Check( furthest <= POSED_BOUNDS_SLACK * 1.01f, "a joint without weights does not widen the bounds" );
}

static void TestWeightSum() {
	// Two weights of 1 on one vertex: the vertex moves twice as far as either
	// joint carries its offset.
	Mesh mesh;
	for ( int i = 0; i < 2; i++ ) {
		Weight weight;
		weight.joint = i;
		weight.weight = 1.0f;
		weight.offset[0] = weight.offset[1] = weight.offset[2] = 1.0f;
		mesh.weights.push_back( weight );
	}
	mesh.firstWeight.push_back( 0 );
	mesh.firstWeight.push_back( 2 );
	std::vector<posedBoundsJointBox_t> boxes( 2 );
	const float weightSum = BuildBoxes( mesh, boxes );
	Check( weightSum == 2.0f, "the weight sum is the largest over a vertex" );
	std::vector<Joint> from( 2, MakeJoint( 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f ) );
	std::vector<Joint> to = from;
	to[0].m[3] += 4.0f;
	to[1].m[3] += 4.0f;
	float travel[3];
	PosedBoundsCore_Travel( boxes.data(), 2, from[0].m, to[0].m, weightSum, travel );
	Check( travel[0] >= 8.0f && travel[0] < 8.1f, "unnormalized weights scale the travel" );
	Check( Holds( mesh, boxes, weightSum, from, to ), "the unnormalized vertex stays inside the widened bounds" );
}

static float FromBits( unsigned int bits ) {
	float value;
	std::memcpy( &value, &bits, sizeof( value ) );
	return value;
}

static void TestNotFinite() {
	Random random( 99u );
	const Mesh mesh = RandomMesh( random, 3, 10, true );
	std::vector<posedBoundsJointBox_t> boxes( 3 );
	const float weightSum = BuildBoxes( mesh, boxes );
	std::vector<Joint> from( 3 );
	for ( int i = 0; i < 3; i++ ) {
		from[i] = RandomJoint( random, 10.0f );
	}
	std::vector<Joint> to = from;
	const float notANumber = FromBits( 0x7fc00000u );
	const float infinity = FromBits( 0x7f800000u );
	to[1].m[5] = notANumber;
	float travel[3];
	float furthest = PosedBoundsCore_Travel( boxes.data(), 3, from[0].m, to[0].m, weightSum, travel );
	Check( furthest == POSED_BOUNDS_UNBOUNDED && travel[0] == POSED_BOUNDS_UNBOUNDED, "a NaN joint cannot be bounded" );
	to = from;
	to[2].m[11] = infinity;
	furthest = PosedBoundsCore_Travel( boxes.data(), 3, from[0].m, to[0].m, weightSum, travel );
	Check( furthest == POSED_BOUNDS_UNBOUNDED, "an infinite joint cannot be bounded" );
	Check( !PosedBoundsCore_IsFinite( notANumber ) && !PosedBoundsCore_IsFinite( -infinity ) && PosedBoundsCore_IsFinite( 3.0e38f ),
		"the finite test reads the exponent" );
}

int main() {
	TestRandomPoses();
	TestRotationNeedsTheBox();
	TestOffCentreBox();
	TestTravelIsTight();
	TestWeightSum();
	TestNotFinite();
	if ( failures > 0 ) {
		std::fprintf( stderr, "PosedBoundsCoreTest: %d failure(s)\n", failures );
		return 1;
	}
	std::printf( "PosedBoundsCoreTest: ok\n" );
	return 0;
}
