// openQ4 additions to the SDK game sources; see src/game/LICENSE.
//
// Contact gaps for the enhanced physics step (g_enhancedPhysics 1). Included by
// EnhancedRigidBody.inl and EnhancedArticulated.inl in both game trees.
//
// A contact query sweeps a body along its motion and reports every feature pair
// that would meet within the sweep: a vertex of the body on a polygon of the
// obstacle, an edge of the body on an edge of the obstacle, or a vertex of the
// obstacle on a polygon of the body. The collision model names the features, so
// the gap between them where the body is now is exact:
//
//   body vertex on a polygon    the vertex's height above the polygon's plane;
//                               the row acts at the vertex's foot on the plane
//   body edge on an edge        the contact normal is orthogonal to both edges,
//                               so the body edge's height above the obstacle's
//                               point is the same all along it; the row acts at
//                               the point on the obstacle's edge
//   obstacle vertex on a body   the vertex's depth below the body polygon's
//   polygon                     plane; the row acts at the obstacle's vertex
//
// Before 16 November 2026 the gap was guessed from the body vertex nearest the
// line back along the sweep. On faceted ground that picked the wrong vertex: a
// wall five units ahead of a crate measured 33 units away (the crate then went
// three units into it), a contact kept from the last step measured against
// every vertex of a barrel reported six units of overlap on a concave floor,
// and a limb lying across a ledge was held at its end instead of at the ledge.

#ifndef __OPENQ4_ENHANCED_CONTACTS_H__
#define __OPENQ4_ENHANCED_CONTACTS_H__

struct enhancedShape_t {
	const idTraceModel *	trm;
	int						numVertices;
	float					radius;
	float					vertices[MAX_TRACEMODEL_VERTS * 3];		// world space

	void Set( const idClipModel *clipModel, const idVec3 &origin, const idMat3 &bodyAxis ) {
		trm = clipModel->GetTraceModel();
		numVertices = 0;
		radius = clipModel->GetBounds().GetRadius();
		if ( trm ) {
			numVertices = Min( trm->numVerts, MAX_TRACEMODEL_VERTS );
			for ( int i = 0; i < numVertices; i++ ) {
				const idVec3 world = origin + trm->verts[i] * bodyAxis;
				vertices[i * 3 + 0] = world.x;
				vertices[i * 3 + 1] = world.y;
				vertices[i * 3 + 2] = world.z;
			}
		}
	}

	idVec3 Vertex( int i ) const {
		return idVec3( vertices[i * 3 + 0], vertices[i * 3 + 1], vertices[i * 3 + 2] );
	}

	// a vertex of trace-model polygon p, or false
	bool PolygonVertex( int p, idVec3 &vertex ) const {
		if ( !trm || p < 0 || p >= trm->numPolys || trm->polys[p].numEdges < 1 ) {
			return false;
		}
		const int edge = abs( trm->polys[p].edges[0] );
		if ( edge < 1 || edge > trm->numEdges || trm->edges[edge].v[0] < 0 || trm->edges[edge].v[0] >= numVertices ) {
			return false;
		}
		vertex = Vertex( trm->edges[edge].v[0] );
		return true;
	}

	// the ends of trace-model edge e, or false
	bool Edge( int e, idVec3 &start, idVec3 &end ) const {
		if ( !trm || e < 1 || e > trm->numEdges ) {
			return false;
		}
		const int v0 = trm->edges[e].v[0];
		const int v1 = trm->edges[e].v[1];
		if ( v0 < 0 || v0 >= numVertices || v1 < 0 || v1 >= numVertices ) {
			return false;
		}
		start = Vertex( v0 );
		end = Vertex( v1 );
		return true;
	}
};

/*
================
EnhancedObstacleVertex

  World position of the obstacle vertex a CONTACT_MODELVERTEX contact names.
================
*/
static bool EnhancedObstacleVertex( const idEntity *self, int entityNum, int id, int vertexNum, idVec3 &world ) {
	if ( entityNum < 0 || entityNum >= MAX_GENTITIES || vertexNum < 0 ) {
		return false;
	}
	idVec3 local;
	if ( entityNum == ENTITYNUM_WORLD ) {
		const idClip *clip = gameLocal.GetEntityClipWorld( self );
		idCollisionModel *model = clip ? clip->GetWorldCollisionModel() : NULL;
		if ( !model || !model->GetVertex( vertexNum, local ) ) {
			return false;
		}
		world = local;
		return true;
	}
	const idEntity *other = gameLocal.entities[entityNum];
	const idPhysics *physics = other ? other->GetPhysics() : NULL;
	if ( !physics || id < 0 || id >= physics->GetNumClipModels() ) {
		return false;
	}
	const idClipModel *clipModel = physics->GetClipModel( id );
	if ( !clipModel || clipModel->IsRenderModel() ) {
		return false;
	}
	idCollisionModel *model = clipModel->GetCollisionModel();
	if ( !model || !model->GetVertex( vertexNum, local ) ) {
		return false;
	}
	world = clipModel->GetOrigin() + local * clipModel->GetAxis();
	return true;
}

/*
================
EnhancedContactGap

  The gap of a swept contact where the body is now, and the point its row acts
  at, from the features the collision model reported. feature receives what a
  kept contact needs to be measured again (EnhancedRenewContact). Falls back to
  the nearest-vertex estimate when a feature cannot be resolved, or when exact
  is false (rb_enhancedFeatureGaps / af_enhancedFeatureGaps 0, for comparison).
================
*/
static float EnhancedContactGap( const idEntity *self, const contactInfo_t &info, const enhancedShape_t &shape, const idVec3 &sweepDir,
								bool exact, idVec3 &pointNow, int &feature ) {
	const idVec3 &normal = info.normal;
	feature = -1;
	switch ( exact ? info.type : CONTACT_NONE ) {
		case CONTACT_TRMVERTEX: {
			if ( info.trmFeature >= 0 && info.trmFeature < shape.numVertices ) {
				const idVec3 vertex = shape.Vertex( info.trmFeature );
				const float gap = normal * ( vertex - info.point );
				pointNow = vertex - gap * normal;
				feature = info.trmFeature;
				return gap;
			}
			break;
		}
		case CONTACT_EDGE: {
			idVec3 start, end;
			if ( shape.Edge( abs( info.trmFeature ), start, end ) ) {
				pointNow = info.point;
				feature = abs( info.trmFeature );
				return Min( normal * ( start - info.point ), normal * ( end - info.point ) );
			}
			break;
		}
		case CONTACT_MODELVERTEX: {
			idVec3 onPolygon, obstacle;
			if ( shape.PolygonVertex( info.trmFeature, onPolygon ) &&
				 EnhancedObstacleVertex( self, info.entityNum, info.id, info.modelFeature, obstacle ) ) {
				pointNow = obstacle;
				feature = info.trmFeature;
				return normal * ( onPolygon - obstacle );
			}
			break;
		}
		default: {
			break;
		}
	}
	if ( !shape.numVertices ) {
		pointNow = info.point;
		return 0.0f;
	}
	float separation = 0.0f;
	const OpenQ4Physics::Vec3 now = OpenQ4Physics::ContactPointNow( shape.vertices, shape.numVertices, OpenQ4Physics::Vec3( normal.ToFloatPtr() ),
								OpenQ4Physics::Vec3( info.point.ToFloatPtr() ), OpenQ4Physics::Vec3( sweepDir.ToFloatPtr() ), shape.radius, separation );
	pointNow.Set( now.x, now.y, now.z );
	return separation;
}

/*
================
EnhancedRenewContact

  A contact kept from an earlier step that this step's sweep did not report,
  measured again on the body feature that made it. The contact survives while
  that feature stays within margin above its plane, no deeper than four margins
  below it, and within maxDrift of where it touched. type and feature are what
  EnhancedContactGap returned. When the feature has left the plane, or the
  contact has none, the lowest vertex near the point is measured instead: a
  body that rolls on a surface brings another vertex down onto the same plane,
  and one already below the plane is never reported by a sweep.
================
*/
static bool EnhancedRenewContact( const enhancedShape_t &shape, int type, int feature, const idVec3 &cachedPoint, const idVec3 &cachedNormal,
								float margin, float maxDrift, idVec3 &point, float &separation ) {
	bool measured = false;
	if ( feature >= 0 && type == CONTACT_TRMVERTEX && feature < shape.numVertices ) {
		const idVec3 vertex = shape.Vertex( feature );
		separation = cachedNormal * ( vertex - cachedPoint );
		point = vertex - separation * cachedNormal;
		measured = true;
	} else if ( feature >= 0 && type == CONTACT_EDGE ) {
		// the point of the body's edge nearest the obstacle's point
		idVec3 start, end;
		if ( shape.Edge( feature, start, end ) ) {
			const idVec3 along = end - start;
			const float lengthSqr = along.LengthSqr();
			const float t = lengthSqr > 1e-6f ? idMath::ClampFloat( 0.0f, 1.0f, ( ( cachedPoint - start ) * along ) / lengthSqr ) : 0.0f;
			const idVec3 nearest = start + t * along;
			separation = cachedNormal * ( nearest - cachedPoint );
			point = nearest - separation * cachedNormal;
			measured = true;
		}
	} else if ( feature >= 0 && type == CONTACT_MODELVERTEX ) {
		// the obstacle's vertex against the body polygon where it is now
		idVec3 onPolygon;
		if ( shape.PolygonVertex( feature, onPolygon ) ) {
			separation = cachedNormal * ( onPolygon - cachedPoint );
			point = cachedPoint;
			if ( separation <= margin && separation >= -margin * 4.0f ) {
				return true;
			}
		}
	}
	if ( measured && separation <= margin && separation >= -margin * 4.0f && ( point - cachedPoint ).LengthSqr() <= Square( maxDrift ) ) {
		return true;
	}
	OpenQ4Physics::Vec3 renewed;
	if ( !OpenQ4Physics::RenewPersistentContact( shape.vertices, shape.numVertices, OpenQ4Physics::Vec3( cachedNormal.ToFloatPtr() ),
			OpenQ4Physics::Vec3( cachedPoint.ToFloatPtr() ), margin, maxDrift, renewed, separation ) ) {
		return false;
	}
	point.Set( renewed.x, renewed.y, renewed.z );
	return true;
}

#endif /* !__OPENQ4_ENHANCED_CONTACTS_H__ */
