// Copyright (C) 2026 DarkMatter Productions
//
// Bounds of a skinned mesh in one pose from its exact bounds in another.
//
// A skinned vertex is the weighted sum of what each of its joints makes of a
// joint-space offset: v = sum( w * ( R * o + t ) ). Between two poses of the
// skeleton the vertex therefore moves by sum( w * ( dR * o + dt ) ), and on
// every model axis that is no further than the largest distance any offset of
// any joint moves, times the vertex's summed weights. Each joint's offsets lie
// in a box known when the mesh loads, and the largest of a linear function
// over a box is found from its centre and half size. So the mesh in the new
// pose lies inside its exact bounds in the old one, widened on each axis by
// the distance the joints travelled: a dozen multiplies a joint where exact
// bounds skin every vertex.
//
// The result is conservative, never exact: it holds the mesh and is wider by
// at most twice the travel on each axis. Callers bound how much travel they
// accept (PosedBoundsCore_Travel returns it) and ask for exact bounds beyond.

#ifndef __POSED_BOUNDS_CORE_H__
#define __POSED_BOUNDS_CORE_H__

#include <cmath>
#include <cstring>

// The box, in a joint's own space, of the offsets of every weight that names
// the joint. extent is the half size; a joint no weight names has used false.
struct posedBoundsJointBox_t {
	float	center[3];
	float	extent[3];
	bool	used;
};

// Floats per joint matrix: three rows of rotation and translation, row major
// (idJointMat::ToFloatPtr).
static const int POSED_BOUNDS_JOINT_FLOATS = 12;

// Added to every axis on top of the travel. The skinning kernels and this
// arithmetic round differently; a mesh a few hundred units across is skinned
// to within a few thousandths of a unit.
static const float POSED_BOUNDS_SLACK = 1.0f / 64.0f;
static const float POSED_BOUNDS_RELATIVE_SLACK = 1.0f / 4096.0f;

inline void PosedBoundsCore_ClearBox( posedBoundsJointBox_t &box ) {
	box.center[0] = box.center[1] = box.center[2] = 0.0f;
	box.extent[0] = box.extent[1] = box.extent[2] = 0.0f;
	box.used = false;
}

// Grows a box under construction to hold one offset. While it is built the
// box keeps its minimum in center and its maximum in extent;
// PosedBoundsCore_FinishBox turns them into centre and half size.
inline void PosedBoundsCore_AddOffset( posedBoundsJointBox_t &box, const float offset[3] ) {
	if ( !box.used ) {
		for ( int axis = 0; axis < 3; axis++ ) {
			box.center[axis] = box.extent[axis] = offset[axis];
		}
		box.used = true;
		return;
	}
	for ( int axis = 0; axis < 3; axis++ ) {
		if ( offset[axis] < box.center[axis] ) {
			box.center[axis] = offset[axis];
		}
		if ( offset[axis] > box.extent[axis] ) {
			box.extent[axis] = offset[axis];
		}
	}
}

// The offsets are recovered by dividing weighted offsets by their weight, so
// the box is padded by more than that division can lose.
inline void PosedBoundsCore_FinishBox( posedBoundsJointBox_t &box ) {
	if ( !box.used ) {
		return;
	}
	for ( int axis = 0; axis < 3; axis++ ) {
		const float lo = box.center[axis];
		const float hi = box.extent[axis];
		const float reach = std::fabs( lo ) > std::fabs( hi ) ? std::fabs( lo ) : std::fabs( hi );
		box.center[axis] = ( lo + hi ) * 0.5f;
		box.extent[axis] = ( hi - lo ) * 0.5f + reach * POSED_BOUNDS_RELATIVE_SLACK + POSED_BOUNDS_SLACK;
	}
}

// Neither infinite nor a NaN, read from the exponent bits.
inline bool PosedBoundsCore_IsFinite( const float value ) {
	unsigned int bits;
	std::memcpy( &bits, &value, sizeof( bits ) );
	return ( bits & 0x7f800000u ) != 0x7f800000u;
}

// What PosedBoundsCore_Travel returns for a pose it cannot bound.
static const float POSED_BOUNDS_UNBOUNDED = 3.0e38f;

// The furthest, on each model axis, that any vertex of the mesh moves between
// the poses 'fromJoints' and 'toJoints' (numJoints matrices each). weightSum
// is the largest sum of weight magnitudes over one vertex: 1 for a mesh whose
// weights are normalized. travel[] receives the three distances, slack
// included; the return value is the largest of them, POSED_BOUNDS_UNBOUNDED
// when a joint of either pose is not finite.
inline float PosedBoundsCore_Travel( const posedBoundsJointBox_t *boxes, const int numJoints,
		const float *fromJoints, const float *toJoints, const float weightSum, float travel[3] ) {
	float reach[3] = { 0.0f, 0.0f, 0.0f };
	bool finite = PosedBoundsCore_IsFinite( weightSum );
	for ( int joint = 0; joint < numJoints; joint++ ) {
		const posedBoundsJointBox_t &box = boxes[joint];
		if ( !box.used ) {
			continue;
		}
		const float *from = fromJoints + joint * POSED_BOUNDS_JOINT_FLOATS;
		const float *to = toJoints + joint * POSED_BOUNDS_JOINT_FLOATS;
		for ( int axis = 0; axis < 3; axis++ ) {
			const float *a = from + axis * 4;
			const float *b = to + axis * 4;
			const float d0 = b[0] - a[0];
			const float d1 = b[1] - a[1];
			const float d2 = b[2] - a[2];
			const float d3 = b[3] - a[3];
			// the largest magnitude of d . offset + d3 over the box
			const float moved = std::fabs( d0 * box.center[0] + d1 * box.center[1] + d2 * box.center[2] + d3 )
				+ std::fabs( d0 ) * box.extent[0] + std::fabs( d1 ) * box.extent[1] + std::fabs( d2 ) * box.extent[2];
			finite = finite && PosedBoundsCore_IsFinite( moved );
			if ( moved > reach[axis] ) {
				reach[axis] = moved;
			}
		}
	}
	float furthest = 0.0f;
	for ( int axis = 0; axis < 3; axis++ ) {
		travel[axis] = reach[axis] * weightSum * ( 1.0f + POSED_BOUNDS_RELATIVE_SLACK ) + POSED_BOUNDS_SLACK;
		finite = finite && PosedBoundsCore_IsFinite( travel[axis] );
		if ( travel[axis] > furthest ) {
			furthest = travel[axis];
		}
	}
	if ( !finite ) {
		travel[0] = travel[1] = travel[2] = POSED_BOUNDS_UNBOUNDED;
		return POSED_BOUNDS_UNBOUNDED;
	}
	return furthest;
}

#endif // !__POSED_BOUNDS_CORE_H__
