// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "../idlib/precompiled.h"
#include "PointerCursor.h"

#ifndef ID_DEDICATED
#include "retained/Vector.h"
#include <cmath>
#include <string>
#include <vector>

namespace {
using namespace openq4::ui;

// The art is in texels of the stock 32-texel cursor box, hotspot at the
// origin, and is lit from above and to the right as the stock arrow is: a
// black outline a texel wide, a mid-grey body darkening downwards, a bright
// facet along the lit edge with a white ridge, a dark tail and a soft shadow
// cast down and to the right. Each shape is a simple polygon, so the outline
// is the silhouette and the body its 1-texel inset (0.9 for the I-beam).
struct CursorPoint { float x, y; };
struct CursorLayer {
	std::vector<CursorPoint> points;
	// Grey levels at the top and bottom of a vertical gradient, and its extent.
	float from, to, top, bottom, alpha;
};
struct CursorArt {
	std::vector<CursorPoint> silhouette;
	std::vector<CursorLayer> layers;
};

const CursorArt& Art( pointerCursor_t shape ) {
	// The arrow keeps the stock 45-degree lit edge, its notch and its tail.
	static const CursorArt arrow = {
		{{0,0},{12,12},{7.2f,12},{9.6f,16.8f},{7,18.1f},{4.6f,13.3f},{0,17.9f}},
		{
			{{{1,2.414f},{1,15.486f},{4.874f,11.612f},{7.447f,16.758f},{8.258f,16.353f},{5.582f,11},{9.586f,11}},.745f,.439f,0,17,1},
			{{{1,2.414f},{9.586f,11},{5.582f,11}},.98f,.769f,0,12,1},
			{{{4.874f,11.612f},{7.447f,16.758f},{8.258f,16.353f},{5.582f,11}},.471f,.275f,11,17,1},
			{{{1.25f,3.05f},{1.6f,3},{9.1f,10.5f},{8.6f,10.5f}},1,.922f,0,12,.9f},
		}
	};
	// The I-beam: chamfered serifs stepping into the stem at 45 degrees, the
	// stem's lit right half bright and its left half in shade.
	static const CursorArt text = {
		{{-3.4f,-9},{3.4f,-9},{4.2f,-8.2f},{4.2f,-6.4f},{2.65f,-6.4f},{1.65f,-5.4f},{1.65f,5.4f},{2.65f,6.4f},{4.2f,6.4f},{4.2f,8.2f},
			{3.4f,9},{-3.4f,9},{-4.2f,8.2f},{-4.2f,6.4f},{-2.65f,6.4f},{-1.65f,5.4f},{-1.65f,-5.4f},{-2.65f,-6.4f},{-4.2f,-6.4f},{-4.2f,-8.2f}},
		{
			{{{-3.027f,-8.1f},{-3.3f,-7.827f},{-3.3f,-7.3f},{-2.277f,-7.3f},{-.75f,-5.773f},{-.75f,5.773f},{-2.277f,7.3f},{-3.3f,7.3f},
				{-3.3f,7.827f},{-3.027f,8.1f},{3.027f,8.1f},{3.3f,7.827f},{3.3f,7.3f},{2.277f,7.3f},{.75f,5.773f},{.75f,-5.773f},
				{2.277f,-7.3f},{3.3f,-7.3f},{3.3f,-7.827f},{3.027f,-8.1f}},.98f,.706f,-9,9,1},
			{{{-3.3f,-7.827f},{-3.3f,-7.3f},{-2.277f,-7.3f},{-.75f,-5.773f},{-.75f,5.773f},{-2.277f,7.3f},{-3.3f,7.3f},{-3.3f,7.827f},
				{-3.027f,8.1f},{0,8.1f},{0,-8.1f},{-3.027f,-8.1f}},.769f,.463f,-9,9,1},
		}
	};
	return shape == POINTER_TEXT ? text : arrow;
}

VectorPath Polygon( const std::vector<CursorPoint>& points, float dx, float dy ) {
	VectorPath path;
	for ( size_t i = 0; i < points.size(); ++i ) {
		PathCommand command;
		command.op = i ? PathOperation::Line : PathOperation::Move;
		command.points[0].x.dp = points[i].x+dx; command.points[0].y.dp = points[i].y+dy;
		path.commands.push_back( command );
	}
	PathCommand close; close.op = PathOperation::Close;
	path.commands.push_back( close );
	return path;
}

// The tessellated art at one size, in pixels about a hotspot on a pixel
// corner, so that placing it at whole pixels keeps its edge coverage exact.
struct CursorMesh {
	float unit = 0;
	std::vector<idDrawVert> vertices;
	std::vector<glIndex_t> indices;
};

void Append( const VectorPath& path, float unit, CursorMesh& mesh ) {
	VectorOptions options;
	options.widthDp = options.heightDp = 32;
	options.transform.a = options.transform.d = unit;
	VectorMesh tessellated; std::string error;
	if ( !TessellatePath( path, options, tessellated, error ) ) {
		common->DPrintf( "pointer cursor: %s\n", error.c_str() );
		return;
	}
	const int base = static_cast<int>( mesh.vertices.size() );
	for ( const auto& source : tessellated.vertices ) {
		idDrawVert v;
		v.Clear();
		v.xyz.Set( static_cast<float>( source.x ), static_cast<float>( source.y ), 0 );
		v.normal.Set( 0, 0, 1 ); v.tangents[0].Set( 1, 0, 0 ); v.tangents[1].Set( 0, 1, 0 );
		// _retainedSolid blends premultiplied colour, which the tessellator emits.
		const double colour[4] = { source.r, source.g, source.b, source.a };
		for ( int channel = 0; channel < 4; ++channel ) {
			v.color[channel] = static_cast<byte>( idMath::ClampFloat( 0, 1, static_cast<float>( colour[channel] ) ) * 255.f + .5f );
			v.color2[channel] = 255;
		}
		mesh.vertices.push_back( v );
	}
	for ( const int index : tessellated.indices ) mesh.indices.push_back( static_cast<glIndex_t>( base+index ) );
}

const CursorMesh& Mesh( pointerCursor_t shape, float unit ) {
	static CursorMesh meshes[2];
	CursorMesh& mesh = meshes[shape == POINTER_TEXT ? 1 : 0];
	if ( mesh.unit == unit ) return mesh;
	mesh = {}; mesh.unit = unit;
	const CursorArt& art = Art( shape );
	// The shadow: the silhouette cast 0.8 texel right and 1.2 down, sampled at
	// its centre and eight points 0.7 texel around it, which blurs it over
	// about a texel and a half and darkens the backdrop by at most 37%.
	for ( int i = 0; i < 9; ++i ) {
		const float angle = i * idMath::TWO_PI / 8.f, radius = i < 8 ? .7f : 0.f;
		VectorPath path = Polygon( art.silhouette, .8f+radius*idMath::Cos( angle ), 1.2f+radius*idMath::Sin( angle ) );
		path.fill.type = PaintType::Solid; path.fill.colour = { 0, 0, 0, .05 };
		Append( path, unit, mesh );
	}
	VectorPath outline = Polygon( art.silhouette, 0, 0 );
	outline.fill.type = PaintType::Solid; outline.fill.colour = { 0, 0, 0, 1 };
	Append( outline, unit, mesh );
	for ( const CursorLayer& layer : art.layers ) {
		VectorPath path = Polygon( layer.points, 0, 0 );
		path.fill.type = PaintType::Linear;
		path.fill.from.y.dp = layer.top; path.fill.to.y.dp = layer.bottom;
		path.fill.stops = { { 0, { layer.from, layer.from, layer.from, layer.alpha } }, { 1, { layer.to, layer.to, layer.to, layer.alpha } } };
		Append( path, unit, mesh );
	}
	return mesh;
}

} // namespace

void UI_DrawPointerCursor( pointerCursor_t shape, float x, float y, float frameWidth, float frameHeight, float unit ) {
	if ( !( frameWidth > 0 ) || !( frameHeight > 0 ) || !( unit > 0 ) || !std::isfinite( x ) || !std::isfinite( y ) ) {
		return;
	}
	const idMaterial *material = declManager->FindMaterial( "_retainedSolid" );
	const CursorMesh& mesh = Mesh( shape, unit );
	if ( material == NULL || mesh.vertices.empty() || mesh.indices.empty() ) {
		return;
	}
	static std::vector<idDrawVert> placed;
	placed = mesh.vertices;
	const float px = idMath::Floor( x+.5f ), py = idMath::Floor( y+.5f );
	const float sx = SCREEN_WIDTH / frameWidth, sy = SCREEN_HEIGHT / frameHeight;
	for ( idDrawVert& v : placed ) v.xyz.Set( ( v.xyz.x+px ) * sx, ( v.xyz.y+py ) * sy, 0 );
	renderSystem->SetColor4( 1, 1, 1, 1 );
	renderSystem->DrawStretchPic( placed.data(), mesh.indices.data(), static_cast<int>( placed.size() ),
		static_cast<int>( mesh.indices.size() ), material, false );
}

#else

void UI_DrawPointerCursor( pointerCursor_t, float, float, float, float, float ) {}

#endif
