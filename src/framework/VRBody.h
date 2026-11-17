// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#ifndef __VRBODY_H__
#define __VRBODY_H__

/*
===============================================================================

	The player's own body in the headset (vr_body), for both game modules.

	A player who looks down in a headset expects a chest, legs and arms. The
	third-person model is posed for the people who look at the player, not
	for the player: it leans with the aim, stands taller than the camera and
	holds its arms where its animation puts them. The body drawn in the eyes
	is therefore a second render entity of the same model, seen in the
	player's own view only, posed here from the tracked head and hands:

	- the pelvis, spine and shoulders are the model's rest pose (its ragdoll
	  T-pose, upright and square), scaled so the model's neck reaches the
	  player's, and hung from the neck under the tracked head;
	- the legs keep the feet of the model's own animation (walking, running,
	  crouching) and bend at the knee to reach the pelvis wherever the head
	  has taken it;
	- each arm reaches for its hand. With a view weapon in the hands the
	  model's upper arm meets the view weapon's own forearm, so the gun keeps
	  the hands Raven modelled and animated on it; otherwise the model's own
	  forearm and hand follow the controller.

	Everything works on joint matrices in the model's own space, the form a
	render entity takes them in. A scale folded into a joint's rotation scales
	what is skinned to it, which is how the body is sized and how the parts
	that must not show are folded away (a rotation of zero puts every vertex of
	a joint at one point).

	Matrices follow the engine's row-vector convention: a joint's idMat3 has
	the joint's axes as rows, and a point moves as v * rotation.

===============================================================================
*/

// vr_body
typedef enum {
	VR_BODY_OFF = 0,		// no body: the view weapon's forearms float, as on a flat screen
	VR_BODY_TORSO = 1,		// legs and torso; the view weapon keeps its floating forearms
	VR_BODY_FULL = 2		// legs, torso and arms that reach the hands
} vrBody_t;

// The joints of a player model the body is posed by, as indices into the
// model's joint list; every one must exist for the body to be drawn.
typedef struct vrBodyRig_s {
	int						numJoints;
	int						pelvis;					// "body": the root of everything above the feet
	int						neck;					// "head_channel": where the head model sits
	int						upperArm[VR_NUM_HANDS];	// the shoulder
	int						elbow[VR_NUM_HANDS];
	int						forearm[VR_NUM_HANDS];	// the twist joint halfway down the forearm, or -1
	int						wrist[VR_NUM_HANDS];
	int						index[VR_NUM_HANDS];	// first knuckles, which tell how the hand lies
	int						ring[VR_NUM_HANDS];
	int						upperLeg[2];			// the hip
	int						lowerLeg[2];			// the knee
	int						ankle[2];
} vrBodyRig_t;

ID_INLINE bool VRBody_RigValid( const vrBodyRig_t &rig ) {
	if ( rig.numJoints <= 0 || rig.pelvis < 0 || rig.neck < 0 ) {
		return false;
	}
	for ( int side = 0; side < 2; side++ ) {
		if ( rig.upperArm[side] < 0 || rig.elbow[side] < 0 || rig.wrist[side] < 0 || rig.index[side] < 0 || rig.ring[side] < 0
				|| rig.upperLeg[side] < 0 || rig.lowerLeg[side] < 0 || rig.ankle[side] < 0 ) {
			return false;
		}
	}
	return true;
}

ID_INLINE bool VRBody_IsDescendant( const int *parents, int joint, int ancestor ) {
	for ( joint = parents[joint]; joint >= 0; joint = parents[joint] ) {
		if ( joint == ancestor ) {
			return true;
		}
	}
	return false;
}

// The shortest turn from one direction to another, as v * m. Opposite
// directions take half a turn about an axis square to them.
ID_INLINE idMat3 VRBody_Swing( const idVec3 &from, const idVec3 &to ) {
	idVec3 a = from;
	idVec3 b = to;
	if ( a.Normalize() < 1e-5f || b.Normalize() < 1e-5f ) {
		return mat3_identity;
	}
	if ( a * b < -0.9999f ) {
		idVec3 axis = a.Cross( idVec3( 0.0f, 0.0f, 1.0f ) );
		if ( axis.Normalize() < 1e-3f ) {
			axis = a.Cross( idVec3( 1.0f, 0.0f, 0.0f ) );
			axis.Normalize();
		}
		// a half turn about a unit axis: 2 * axis * axis^T - identity
		return idMat3( 2.0f * axis.x * axis.x - 1.0f, 2.0f * axis.x * axis.y, 2.0f * axis.x * axis.z,
			2.0f * axis.y * axis.x, 2.0f * axis.y * axis.y - 1.0f, 2.0f * axis.y * axis.z,
			2.0f * axis.z * axis.x, 2.0f * axis.z * axis.y, 2.0f * axis.z * axis.z - 1.0f );
	}
	vrVec3_t rows[3];
	VR_RotationBetween( VR_Vec3( a.x, a.y, a.z ), VR_Vec3( b.x, b.y, b.z ), rows );
	return idMat3( rows[0].x, rows[0].y, rows[0].z, rows[1].x, rows[1].y, rows[1].z, rows[2].x, rows[2].y, rows[2].z );
}

// The turn that takes one pair of directions onto another: the first of each
// pair exactly, the second as nearly as the angle between them allows.
ID_INLINE idMat3 VRBody_Align( const idVec3 &fromForward, const idVec3 &fromUp, const idVec3 &toForward, const idVec3 &toUp ) {
	idVec3 f0 = fromForward, f1 = toForward;
	if ( f0.Normalize() < 1e-5f || f1.Normalize() < 1e-5f ) {
		return mat3_identity;
	}
	idVec3 u0 = fromUp - f0 * ( fromUp * f0 );
	idVec3 u1 = toUp - f1 * ( toUp * f1 );
	if ( u0.Normalize() < 1e-5f || u1.Normalize() < 1e-5f ) {
		return VRBody_Swing( f0, f1 );
	}
	// forward, left, up as rows of each frame: from^T * to takes one onto the other
	const idMat3 from( f0, u0.Cross( f0 ), u0 );
	const idMat3 to( f1, u1.Cross( f1 ), u1 );
	return from.Transpose() * to;
}

// A joint carried by a turn about a pivot that itself moves to a new place.
ID_INLINE void VRBody_Carry( idJointMat &out, const idJointMat &in, const idMat3 &turn, const idVec3 &pivotFrom, const idVec3 &pivotTo ) {
	const idVec3 position = pivotTo + ( in.ToVec3() - pivotFrom ) * turn;
	out.SetRotation( in.ToMat3() * turn );
	out.SetTranslation( position );
}

// Every vertex skinned to the joint lands on one point: that part of the
// mesh folds away.
ID_INLINE void VRBody_Fold( idJointMat &joint, const idVec3 &point ) {
	joint.SetRotation( mat3_zero );
	joint.SetTranslation( point );
}

ID_INLINE void VRBody_ScaleJoint( idJointMat &out, const idJointMat &in, float scale, const idVec3 &offset ) {
	out.SetRotation( in.ToMat3() * scale );
	out.SetTranslation( in.ToVec3() * scale + offset );
}

/*
====================
VRBody_PoseTrunk

The pelvis, spine, shoulders and arms from the rest pose and the feet from
this tic's animation, both scaled, with the neck neckHeight above the
model's origin; the legs then reach from the hips to the feet. The neck
never rises above where straight legs put it, so the feet stay on the floor,
and never sinks below a deep squat.

Returns how far the trunk was lowered from standing.
====================
*/
ID_INLINE float VRBody_PoseTrunk( const vrBodyRig_t &rig, const int *parents, const idJointMat *rest, const idJointMat *animated,
		float scale, float neckHeight, idJointMat *out ) {
	const float standing = rest[rig.neck].ToVec3().z * scale;
	// a squat that leaves the hips a hand above the ankles
	const float lowest = ( rest[rig.neck].ToVec3().z - rest[rig.upperLeg[0]].ToVec3().z + rest[rig.ankle[0]].ToVec3().z + 4.0f ) * scale;
	const float drop = standing - idMath::ClampFloat( lowest, standing, neckHeight );
	const idVec3 lowered( 0.0f, 0.0f, -drop );

	for ( int i = 0; i < rig.numJoints; i++ ) {
		const bool leg = i == rig.upperLeg[0] || i == rig.upperLeg[1]
			|| VRBody_IsDescendant( parents, i, rig.upperLeg[0] ) || VRBody_IsDescendant( parents, i, rig.upperLeg[1] );
		const bool trunk = i == rig.pelvis || VRBody_IsDescendant( parents, i, rig.pelvis );
		if ( leg || !trunk ) {
			// the feet, and whatever the model roots them to, follow the animation
			VRBody_ScaleJoint( out[i], animated[i], scale, vec3_origin );
		} else {
			VRBody_ScaleJoint( out[i], rest[i], scale, lowered );
		}
	}

	for ( int side = 0; side < 2; side++ ) {
		const int upper = rig.upperLeg[side], lower = rig.lowerLeg[side], ankle = rig.ankle[side];
		const idVec3 hip = rest[upper].ToVec3() * scale + lowered;
		const idVec3 foot = animated[ankle].ToVec3() * scale;
		const float thigh = ( rest[lower].ToVec3() - rest[upper].ToVec3() ).Length() * scale;
		const float shin = ( rest[ankle].ToVec3() - rest[lower].ToVec3() ).Length() * scale;
		// the knee bends the way the animation bends it, or forwards
		const idVec3 animatedHip = animated[upper].ToVec3() * scale;
		idVec3 pole = animated[lower].ToVec3() * scale - ( animatedHip + foot ) * 0.5f;
		if ( pole.LengthSqr() < 1.0f ) {
			pole.Set( 1.0f, 0.0f, 0.0f );
		}
		pole.x += 4.0f * scale;
		const vrTwoBone_t leg = VR_SolveTwoBone( VR_Vec3( hip.x, hip.y, hip.z ), VR_Vec3( foot.x, foot.y, foot.z ),
			VR_Vec3( pole.x, pole.y, pole.z ), thigh, shin );
		const idVec3 knee( leg.joint.x, leg.joint.y, leg.joint.z );

		const idMat3 thighTurn = VRBody_Swing( animated[lower].ToVec3() - animated[upper].ToVec3(), knee - hip );
		out[upper].SetRotation( animated[upper].ToMat3() * thighTurn * scale );
		out[upper].SetTranslation( hip );
		const idMat3 shinTurn = VRBody_Swing( animated[ankle].ToVec3() - animated[lower].ToVec3(), foot - knee );
		for ( int i = 0; i < rig.numJoints; i++ ) {
			// the knee and anything that rides the shin, but not the foot
			if ( i == lower || ( VRBody_IsDescendant( parents, i, lower ) && i != ankle && !VRBody_IsDescendant( parents, i, ankle ) ) ) {
				idJointMat scaled;
				VRBody_ScaleJoint( scaled, animated[i], scale, vec3_origin );
				VRBody_Carry( out[i], scaled, shinTurn, animated[lower].ToVec3() * scale, knee );
			} else if ( i != upper && VRBody_IsDescendant( parents, i, upper ) && !VRBody_IsDescendant( parents, i, lower ) && i != lower ) {
				// anything that rides the thigh
				idJointMat scaled;
				VRBody_ScaleJoint( scaled, animated[i], scale, vec3_origin );
				VRBody_Carry( out[i], scaled, thighTurn, animatedHip, hip );
			}
		}
	}
	return drop;
}

// Which way an elbow bends: down and out to its own side, and a little back,
// in the body's axes (forward, left, up).
ID_INLINE idVec3 VRBody_ElbowPole( int hand ) {
	return idVec3( -0.35f, hand == VR_HAND_LEFT ? 0.55f : -0.55f, -0.75f );
}

/*
====================
VRBody_Elbow

Where the elbow goes for a wrist at the given place: the model's upper arm
from the shoulder, and a forearm of forearmLength from the wrist.
====================
*/
ID_INLINE idVec3 VRBody_Elbow( const vrBodyRig_t &rig, const idJointMat *rest, const idJointMat *out, float scale, int hand,
		const idVec3 &wrist, float forearmLength ) {
	const idVec3 shoulder = out[rig.upperArm[hand]].ToVec3();
	const float upperLength = ( rest[rig.elbow[hand]].ToVec3() - rest[rig.upperArm[hand]].ToVec3() ).Length() * scale;
	const idVec3 pole = VRBody_ElbowPole( hand );
	const vrTwoBone_t arm = VR_SolveTwoBone( VR_Vec3( shoulder.x, shoulder.y, shoulder.z ), VR_Vec3( wrist.x, wrist.y, wrist.z ),
		VR_Vec3( pole.x, pole.y, pole.z ), upperLength, forearmLength );
	return idVec3( arm.joint.x, arm.joint.y, arm.joint.z );
}

/*
====================
VRBody_PoseUpperArm

The model's upper arm from its shoulder to the elbow, and its elbow turned
along the forearm towards the wrist. With foldForearm the rest of the arm
folds away a little way down the forearm, inside the view weapon's own
forearm, which is drawn from there on; without it the caller poses the
forearm and hand (VRBody_PoseForearm).
====================
*/
ID_INLINE void VRBody_PoseUpperArm( const vrBodyRig_t &rig, const int *parents, const idJointMat *rest, float scale, int hand,
		const idVec3 &elbow, const idVec3 &wrist, bool foldForearm, idJointMat *out ) {
	const int upper = rig.upperArm[hand], joint = rig.elbow[hand];
	const idVec3 shoulder = out[upper].ToVec3();
	const idMat3 upperTurn = VRBody_Swing( rest[joint].ToVec3() - rest[upper].ToVec3(), elbow - shoulder );
	out[upper].SetRotation( rest[upper].ToMat3() * upperTurn * scale );
	const idMat3 forearmTurn = VRBody_Swing( rest[rig.wrist[hand]].ToVec3() - rest[joint].ToVec3(), wrist - elbow );
	out[joint].SetRotation( rest[joint].ToMat3() * forearmTurn * scale );
	out[joint].SetTranslation( elbow );

	idVec3 along = wrist - elbow;
	const float length = along.Normalize();
	const idVec3 fold = elbow + along * Min( length * 0.5f, 4.0f * scale );
	for ( int i = 0; i < rig.numJoints; i++ ) {
		if ( !VRBody_IsDescendant( parents, i, joint ) ) {
			continue;
		}
		if ( foldForearm ) {
			VRBody_Fold( out[i], fold );
		} else {
			idJointMat scaled;
			VRBody_ScaleJoint( scaled, rest[i], scale, vec3_origin );
			VRBody_Carry( out[i], scaled, forearmTurn, rest[joint].ToVec3() * scale, elbow );
		}
	}
}

// How far the model's wrist is behind the palm a controller is held in.
ID_INLINE float VRBody_PalmFromWrist( const vrBodyRig_t &rig, const idJointMat *rest, float scale, int hand ) {
	const idVec3 knuckles = ( rest[rig.index[hand]].ToVec3() + rest[rig.ring[hand]].ToVec3() ) * 0.5f;
	return ( knuckles - rest[rig.wrist[hand]].ToVec3() ).Length() * scale * 0.6f;
}

ID_INLINE float VRBody_ForearmLength( const vrBodyRig_t &rig, const idJointMat *rest, float scale, int hand ) {
	return ( rest[rig.wrist[hand]].ToVec3() - rest[rig.elbow[hand]].ToVec3() ).Length() * scale;
}

/*
====================
VRBody_PoseHand

The model's own hand at the wrist, after VRBody_PoseUpperArm without
foldForearm: its fingers lie along forward and its thumb side faces up, the
way a hand closes on a controller's handle, and the fingers curl as they
would around one. The forearm's twist joint turns halfway with the hand.
====================
*/
ID_INLINE void VRBody_PoseHand( const vrBodyRig_t &rig, const int *parents, const idJointMat *rest, float scale, int hand,
		const idVec3 &wrist, const idVec3 &forward, const idVec3 &up, idJointMat *out ) {
	const int joint = rig.wrist[hand];
	const idVec3 restWrist = rest[joint].ToVec3();
	const idVec3 restKnuckles = ( rest[rig.index[hand]].ToVec3() + rest[rig.ring[hand]].ToVec3() ) * 0.5f;
	const idVec3 restFingers = restKnuckles - restWrist;
	// the index finger is on the thumb's side of the hand
	const idVec3 restThumbSide = rest[rig.index[hand]].ToVec3() - rest[rig.ring[hand]].ToVec3();
	const idMat3 turn = VRBody_Align( restFingers, restThumbSide, forward, up );

	for ( int i = 0; i < rig.numJoints; i++ ) {
		if ( i == joint || VRBody_IsDescendant( parents, i, joint ) ) {
			idJointMat scaled;
			VRBody_ScaleJoint( scaled, rest[i], scale, vec3_origin );
			VRBody_Carry( out[i], scaled, turn, restWrist * scale, wrist );
		}
	}

	// curl the fingers a third of a turn towards the palm, about the line of the knuckles
	idVec3 fingers = forward;
	fingers.Normalize();
	idVec3 thumbSide = up - fingers * ( up * fingers );
	if ( thumbSide.Normalize() > 1e-4f ) {
		// the palm faces the body's middle: a right hand's palm is to its left
		const idVec3 palm = hand == VR_HAND_RIGHT ? thumbSide.Cross( fingers ) : fingers.Cross( thumbSide );
		float s, c;
		idMath::SinCos( DEG2RAD( 55.0f ), s, c );
		const idMat3 curl = VRBody_Swing( fingers, fingers * c + palm * s );
		const int knuckles[2] = { rig.index[hand], rig.ring[hand] };
		for ( int finger = 0; finger < 2; finger++ ) {
			// each joint of the finger turns again about itself, carrying the joints beyond it
			for ( int bend = knuckles[finger]; bend >= 0; ) {
				const idVec3 pivot = out[bend].ToVec3();
				int next = -1;
				for ( int i = 0; i < rig.numJoints; i++ ) {
					if ( i == bend || VRBody_IsDescendant( parents, i, bend ) ) {
						const idJointMat before = out[i];
						VRBody_Carry( out[i], before, curl, pivot, pivot );
						if ( parents[i] == bend && next < 0 ) {
							next = i;
						}
					}
				}
				bend = next;
			}
		}
	}

	// the forearm twists halfway from the elbow's lie to the hand's
	const int twist = rig.forearm[hand];
	if ( twist >= 0 && twist != joint && !VRBody_IsDescendant( parents, twist, joint ) ) {
		const idMat3 restAxis = rest[twist].ToMat3();
		idMat3 elbowAxis = out[twist].ToMat3() * ( 1.0f / scale );
		const idMat3 handAxis = restAxis * turn;
		idQuat between;
		between.Slerp( elbowAxis.ToQuat(), handAxis.ToQuat(), 0.5f );
		out[twist].SetRotation( between.ToMat3() * scale );
	}
}

// An arm that is not drawn (VR_BODY_TORSO): folded into the shoulder.
ID_INLINE void VRBody_FoldArm( const vrBodyRig_t &rig, const int *parents, int hand, idJointMat *out ) {
	const int upper = rig.upperArm[hand];
	const idVec3 point = parents[upper] >= 0 ? ( out[upper].ToVec3() + out[parents[upper]].ToVec3() ) * 0.5f : out[upper].ToVec3();
	for ( int i = 0; i < rig.numJoints; i++ ) {
		if ( i == upper || VRBody_IsDescendant( parents, i, upper ) ) {
			VRBody_Fold( out[i], point );
		}
	}
}

/*
===============================================================================

	The view weapon's own arms.

	Every stock view weapon carries the same pair of forearms and hands: a
	forearm of two joints (the elbow end, "lft_shldr", and a joint halfway,
	"lft_elbo") and a wrist with its fingers. The right arm has no elbow
	joint of its own; its forearm hangs off "rt_elbo" alone, the mirror image
	of the left one. The forearms are authored for a flat screen, pointing
	back past the camera wherever the gun is; in a headset each one swings
	about its wrist until its elbow end meets the body's elbow.

===============================================================================
*/

typedef struct vrViewArm_s {
	int						wrist;			// "lft_wrst" / "rt_wrst"
	int						forearm;		// "lft_elbo" / "rt_elbo": halfway along the forearm
	int						elbow;			// "lft_shldr": the forearm's elbow end, or -1 (the right arm)
	idVec3					elbowLocal;		// the elbow end in the forearm joint's axes
	float					length;			// from the wrist to the elbow end
	// the elbow end joint against the forearm joint in the rest pose (left arm)
	idMat3					elbowAxisLocal;
} vrViewArm_t;

// Where a view arm's elbow end is now.
ID_INLINE idVec3 VRBody_ViewArmElbow( const vrViewArm_t &arm, const idJointMat *joints ) {
	return joints[arm.forearm].ToVec3() + arm.elbowLocal * joints[arm.forearm].ToMat3();
}

// Swings a view arm's forearm about its wrist so that its elbow end points at
// elbow; the wrist, and the hand on the gun, stay as they are.
ID_INLINE void VRBody_AimViewArm( const vrViewArm_t &arm, const idVec3 &elbow, idJointMat *joints ) {
	const idVec3 wrist = joints[arm.wrist].ToVec3();
	const idMat3 turn = VRBody_Swing( VRBody_ViewArmElbow( arm, joints ) - wrist, elbow - wrist );
	const idJointMat forearm = joints[arm.forearm];
	VRBody_Carry( joints[arm.forearm], forearm, turn, wrist, wrist );
	if ( arm.elbow >= 0 ) {
		// the elbow end lies straight on from the forearm, as it was modelled
		joints[arm.elbow].SetRotation( arm.elbowAxisLocal * joints[arm.forearm].ToMat3() );
		joints[arm.elbow].SetTranslation( VRBody_ViewArmElbow( arm, joints ) );
	}
}

#endif /* !__VRBODY_H__ */
