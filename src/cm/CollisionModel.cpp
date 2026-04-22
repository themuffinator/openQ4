// CollisionModel.cpp
//




#include "CollisionModel_local.h"

/*
==========================
idCollisionModelLocal::GetName
==========================
*/
const char* idCollisionModelLocal::GetName(void) const {
	return name.c_str();
}

/*
==========================
idCollisionModelLocal::GetBounds
==========================
*/
bool idCollisionModelLocal::GetBounds(idBounds& bounds) const {
	bounds = this->bounds;
	return true;
}

/*
==========================
idCollisionModelLocal::GetBounds
==========================
*/
bool idCollisionModelLocal::GetContents(int& contents) const {
	contents = this->contents;
	return true;
}

/*
==========================
idCollisionModelLocal::GetBounds
==========================
*/
bool idCollisionModelLocal::GetVertex(int vertexNum, idVec3& vertex) const {
	if (vertexNum < 0 || vertexNum >= numVertices) {
		common->Printf("idCollisionModelManagerLocal::GetModelVertex: invalid vertex number\n");
		return false;
	}

	vertex = vertices[vertexNum].p;

	return true;
}

/*
==========================
idCollisionModelLocal::GetBounds
==========================
*/
bool idCollisionModelLocal::GetEdge(int edgeNum, idVec3& start, idVec3& end) const {
	edgeNum = abs(edgeNum);
	if (edgeNum >= numEdges) {
		common->Printf("idCollisionModelManagerLocal::GetModelEdge: invalid edge number\n");
		return false;
	}

	start = vertices[edges[edgeNum].vertexNum[0]].p;
	end = vertices[edges[edgeNum].vertexNum[1]].p;

	return true;
}

/*
==========================
idCollisionModelLocal::GetPolygon
==========================
*/
bool idCollisionModelLocal::GetPolygon(int polygonNum, idFixedWinding& winding) const {
	int i, edgeNum;
	cm_polygon_t* poly;

	poly = *reinterpret_cast<cm_polygon_t**>(&polygonNum);
	winding.Clear();
	for (i = 0; i < poly->numEdges; i++) {
		edgeNum = poly->edges[i];
		winding += vertices[edges[abs(edgeNum)].vertexNum[INTSIGNBITSET(edgeNum)]].p;
	}

	return true;
}

/*
================
idCollisionModelLocal::DrawModel
================
*/
void idCollisionModelLocal::DrawModel(const idVec3& modelOrigin, const idMat3& modelAxis, const idVec3& viewOrigin, const float radius) {
	idVec3 viewPos;

	if (cm_drawColor.IsModified()) {
		sscanf(cm_drawColor.GetString(), "%f %f %f %f", &cm_color.x, &cm_color.y, &cm_color.z, &cm_color.w);
		cm_drawColor.ClearModified();
	}

	viewPos = (viewOrigin - modelOrigin) * modelAxis.Transpose();
	collisionModelManagerLocal.DrawNodePolygons(this, node, modelOrigin, modelAxis, viewPos, radius);
}


/*
================
idCollisionModelLocal::ModelInfo
================
*/
void idCollisionModelLocal::ModelInfo(void) {
	common->Printf( "%6i vertices (%i KB)\n", numVertices, ( numVertices * sizeof( cm_vertex_t ) ) >> 10 );
	common->Printf( "%6i edges (%i KB)\n", numEdges, ( numEdges * sizeof( cm_edge_t ) ) >> 10 );
	common->Printf( "%6i polygons (%i KB)\n", numPolygons, polygonMemory >> 10 );
	common->Printf( "%6i brushes (%i KB)\n", numBrushes, brushMemory >> 10 );
	common->Printf( "%6i nodes (%i KB)\n", numNodes, ( numNodes * sizeof( cm_node_t ) ) >> 10 );
	common->Printf( "%6i polygon refs (%i KB)\n", numPolygonRefs, ( numPolygonRefs * sizeof( cm_polygonRef_t ) ) >> 10 );
	common->Printf( "%6i brush refs (%i KB)\n", numBrushRefs, ( numBrushRefs * sizeof( cm_brushRef_t ) ) >> 10 );
	common->Printf( "%6i internal edges\n", numInternalEdges );
	common->Printf( "%6i sharp edges\n", numSharpEdges );
	common->Printf( "%6i contained polygons removed\n", numRemovedPolys );
	common->Printf( "%6i polygons merged\n", numMergedPolys );
	common->Printf( "%6i KB total memory used\n", usedMemory >> 10 );
}
