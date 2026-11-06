// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#include "LevelEditorViews.h"
#include "LevelEditorPreview.h"
#include "../ui/DeviceContext.h"

#include <cmath>
#include <vector>

using namespace oq4editor;

namespace leveleditorviews {

Vec3 OrthoFrame::ToWorld( float nx, float ny ) const {
	return center + right * ( ( nx - 0.5 ) * width * scale ) - up * ( ( ny - 0.5 ) * height * scale );
}

void OrthoFrame::ToScreen( const Vec3 &p, float &x, float &y ) const {
	const Vec3 d = p - center;
	x = static_cast<float>( d.Dot( right ) / scale + width * 0.5 );
	y = static_cast<float>( height * 0.5 - d.Dot( up ) / scale );
}

OrthoFrame Frame( int mode, const ViewState &view, float width, float height ) {
	OrthoFrame frame;
	if ( mode == 2 ) { frame.right = Vec3( 1, 0, 0 ); frame.up = Vec3( 0, 0, 1 ); }
	else if ( mode == 3 ) { frame.right = Vec3( 0, 1, 0 ); frame.up = Vec3( 0, 0, 1 ); }
	else { frame.right = Vec3( 1, 0, 0 ); frame.up = Vec3( 0, 1, 0 ); }
	frame.out = frame.right.Cross( frame.up );
	frame.center = view.orthoCenter;
	frame.scale = view.orthoScale;
	frame.width = width > 1 ? width : 1;
	frame.height = height > 1 ? height : 1;
	return frame;
}

namespace {

struct RecordedBatch {
	idVec4 color;
	std::vector<idDrawVert> verts;
	std::vector<glIndex_t> indexes;
};

void Replay( const std::vector<RecordedBatch> &batches ) {
	const idMaterial *material = declManager->FindMaterial( "gfx/guis/white" );
	for ( const auto &batch : batches ) {
		renderSystem->SetColor( batch.color );
		renderSystem->DrawStretchPic( batch.verts.data(), batch.indexes.data(), static_cast<int>( batch.verts.size() ),
			static_cast<int>( batch.indexes.size() ), material, false );
	}
}

// Thin lines batched as quads in device-context space, one batch per colour.
// With a recording, batches are kept for replay instead of drawn.
class LineBatch {
public:
	LineBatch( idDeviceContext *context, const idRectangle &clip, std::vector<RecordedBatch> *record = nullptr ) : dc( context ), rect( clip ), recording( record ) {
		float x = 0, y = 0, w = 1, h = 1;
		dc->AdjustCoords( &x, &y, &w, &h );
		offsetX = x; offsetY = y; scaleX = w; scaleY = h;
		const float pixelsX = engineWindowState.vidWidth > 0 ? 640.0f / engineWindowState.vidWidth : 1.0f;
		const float pixelsY = engineWindowState.vidHeight > 0 ? 480.0f / engineWindowState.vidHeight : 1.0f;
		halfWidth = 0.5f * ( pixelsX > pixelsY ? pixelsX : pixelsY ) / ( scaleX > 0 ? scaleX : 1 );
		material = declManager->FindMaterial( "gfx/guis/white" ); // the device context's own fill
	}
	void Begin( const idVec4 &value ) { Flush(); color = value; }
	bool Line( float x0, float y0, float x1, float y1 ) {
		// Liang-Barsky clip to the view rectangle (virtual coordinates).
		float t0 = 0, t1 = 1;
		const float dx = x1 - x0, dy = y1 - y0;
		const float p[4] = { -dx, dx, -dy, dy };
		const float q[4] = { x0 - rect.x, rect.x + rect.w - x0, y0 - rect.y, rect.y + rect.h - y0 };
		for ( int i = 0; i < 4; ++i ) {
			if ( p[i] == 0 ) { if ( q[i] < 0 ) { return false; } continue; }
			const float r = q[i] / p[i];
			if ( p[i] < 0 ) { if ( r > t1 ) { return false; } if ( r > t0 ) { t0 = r; } }
			else { if ( r < t0 ) { return false; } if ( r < t1 ) { t1 = r; } }
		}
		const float ax = x0 + dx * t0, ay = y0 + dy * t0, bx = x0 + dx * t1, by = y0 + dy * t1;
		float nx = -( by - ay ), ny = bx - ax;
		const float length = idMath::Sqrt( nx * nx + ny * ny );
		if ( length < 1e-6f ) { nx = halfWidth; ny = 0; }
		else { nx *= halfWidth / length; ny *= halfWidth / length; }
		const float corners[4][2] = { { ax + nx, ay + ny }, { bx + nx, by + ny }, { bx - nx, by - ny }, { ax - nx, ay - ny } };
		const int base = static_cast<int>( verts.size() );
		for ( const auto &c : corners ) {
			idDrawVert v; v.Clear();
			v.xyz.Set( c[0] * scaleX + offsetX, c[1] * scaleY + offsetY, 0 );
			v.st.Set( 0, 0 );
			v.SetColor( 0xffffffff );
			verts.push_back( v );
		}
		// GUI surfaces are back-face culled and a line's corner order depends
		// on its direction, so emit both windings.
		for ( int i : { 0, 1, 2, 0, 2, 3, 0, 2, 1, 0, 3, 2 } ) { indexes.push_back( static_cast<glIndex_t>( base + i ) ); }
		++lines;
		if ( verts.size() >= 12000 ) { Flush(); }
		return true;
	}
	void Rect( float x, float y, float w, float h ) {
		Line( x, y, x + w, y ); Line( x + w, y, x + w, y + h ); Line( x + w, y + h, x, y + h ); Line( x, y + h, x, y );
	}
	void Flush() {
		if ( verts.empty() ) { return; }
		if ( recording ) {
			recording->push_back( { color, std::move( verts ), std::move( indexes ) } );
			verts.clear(); indexes.clear();
			return;
		}
		renderSystem->SetColor( color );
		renderSystem->DrawStretchPic( verts.data(), indexes.data(), static_cast<int>( verts.size() ), static_cast<int>( indexes.size() ), material, false );
		verts.clear(); indexes.clear();
	}
	~LineBatch() { Flush(); }
	int lines = 0;
private:
	idDeviceContext *dc;
	idRectangle rect;
	std::vector<RecordedBatch> *recording;
	const idMaterial *material = nullptr;
	float offsetX = 0, offsetY = 0, scaleX = 1, scaleY = 1, halfWidth = 0.5f;
	idVec4 color = colorWhite;
	std::vector<idDrawVert> verts;
	std::vector<glIndex_t> indexes;
};

const char *Text( const char *key ) { return common->GetLanguageDict()->GetString( key ); }

// A view's grid and wireframe are rebuilt only when something they show
// changes: the document, selection, layers, camera, grid or view size.
struct OrthoCache {
	std::uint64_t key = 0;
	bool valid = false;
	std::vector<RecordedBatch> batches;
	DrawStats stats;
};
OrthoCache orthoCaches[4];

class KeyHash {
public:
	template<typename T> KeyHash &Add( const T &value ) {
		const unsigned char *bytes = reinterpret_cast<const unsigned char *>( &value );
		for ( std::size_t i = 0; i < sizeof( T ); ++i ) { hash = ( hash ^ bytes[i] ) * 1099511628211ULL; }
		return *this;
	}
	KeyHash &Add( const std::string &text ) {
		for ( const unsigned char c : text ) { hash = ( hash ^ c ) * 1099511628211ULL; }
		return Add( text.size() );
	}
	std::uint64_t hash = 1469598103934665603ULL;
};

}

void DrawLabel( idDeviceContext *dc, const idRectangle &rect, const char *text ) {
	dc->SetFont( dc->FindFont( "fonts/lowpixel" ) );
	idRectangle label( rect.x + 4, rect.y + 2, rect.w - 8, 14 );
	dc->DrawText( text, 0.15f, 0, idVec4( 0.72f, 0.76f, 0.63f, 1.0f ), label, false );
}

DrawStats DrawOrtho( idDeviceContext *dc, const idRectangle &rect, int mode, idLevelEditorSession &session, const Overlay &overlay ) {
	DrawStats stats;
	dc->DrawFilledRect( rect.x, rect.y, rect.w, rect.h, idVec4( 0.035f, 0.045f, 0.03f, 1.0f ) );
	const OrthoFrame frame = Frame( mode, session.view, rect.w, rect.h );
	const auto screen = [&]( const Vec3 &p, float &x, float &y ) { frame.ToScreen( p, x, y ); x += rect.x; y += rect.y; };
	const MapGeometryCache &geometry = session.Geometry();
	const MapDocument &document = session.Document();
	float ax = 0, ay = 0, aw = 1, ah = 1;
	dc->AdjustCoords( &ax, &ay, &aw, &ah );
	KeyHash key;
	key.Add( mode ).Add( rect.x ).Add( rect.y ).Add( rect.w ).Add( rect.h ).Add( ax ).Add( ay ).Add( aw ).Add( ah )
		.Add( engineWindowState.vidWidth ).Add( engineWindowState.vidHeight ).Add( frame.center.x ).Add( frame.center.y ).Add( frame.center.z )
		.Add( frame.scale ).Add( session.view.grid ).Add( document.Revision() ).Add( session.Workspace().Path() ).Add( geometry.Primitives() );
	for ( const auto &item : session.selection ) { key.Add( item.entity ).Add( item.primitive ); }
	for ( const auto &layer : session.view.hiddenLayers ) { key.Add( layer ); }
	OrthoCache &cache = orthoCaches[mode & 3];
	if ( cache.valid && cache.key == key.hash ) {
		Replay( cache.batches );
		stats = cache.stats;
	} else {
		cache.batches.clear();
		cache.valid = false;
	}
	std::vector<RecordedBatch> *record = cache.valid ? nullptr : &cache.batches;
	if ( record ) {
	{
		// Grid: at least 6 px apart; every eighth line brighter; world axes.
		LineBatch grid( dc, rect, record );
		double spacing = session.view.grid > 0 ? session.view.grid : 8;
		while ( spacing / frame.scale < 6 && spacing < 1e6 ) { spacing *= 8; }
		const Vec3 lo = frame.ToWorld( 0, 1 ), hi = frame.ToWorld( 1, 0 );
		for ( int pass = 0; pass < 2; ++pass ) {
			grid.Begin( pass == 0 ? idVec4( 0.12f, 0.15f, 0.09f, 1 ) : idVec4( 0.2f, 0.25f, 0.13f, 1 ) );
			const double step = pass == 0 ? spacing : spacing * 8;
			for ( int axis = 0; axis < 2; ++axis ) {
				const Vec3 &dir = axis == 0 ? frame.right : frame.up;
				const double a = lo.Dot( dir ), b = hi.Dot( dir );
				for ( double w = std::floor( ( std::min )( a, b ) / step ) * step; w <= ( std::max )( a, b ); w += step ) {
					if ( pass == 0 && std::fabs( std::fmod( w, spacing * 8 ) ) < 1e-6 ) { continue; }
					float x0, y0, x1, y1;
					const Vec3 p = frame.center + dir * ( w - frame.center.Dot( dir ) );
					const Vec3 other = axis == 0 ? frame.up : frame.right;
					screen( p - other * 1e7, x0, y0 ); screen( p + other * 1e7, x1, y1 );
					grid.Line( x0, y0, x1, y1 );
				}
			}
		}
		grid.Begin( idVec4( 0.38f, 0.45f, 0.2f, 1 ) );
		float x0, y0, x1, y1;
		screen( frame.right * -1e7, x0, y0 ); screen( frame.right * 1e7, x1, y1 ); grid.Line( x0, y0, x1, y1 );
		screen( frame.up * -1e7, x0, y0 ); screen( frame.up * 1e7, x1, y1 ); grid.Line( x0, y0, x1, y1 );
		stats.lines += grid.lines;
	}
	const EntityId world = document.Entities().empty() ? 0 : document.Entities().front().id;
	const idVec4 colours[] = { idVec4( 0.62f, 0.66f, 0.55f, 1 ), idVec4( 0.35f, 0.62f, 0.9f, 1 ), idVec4( 0.85f, 0.78f, 0.35f, 1 ), idVec4( 1.0f, 0.55f, 0.05f, 1 ) };
	const std::size_t lineBudget = 300000;
	for ( int pass = 0; pass < 4; ++pass ) {
		// 0 world, 1 entities, 2 patches, 3 selection (drawn last, on top)
		LineBatch batch( dc, rect, record );
		batch.Begin( colours[pass] );
		for ( const Entity &entityRef : document.Entities() ) {
			const Entity *entity = &entityRef;
			const EntityId id = entity->id;
			const auto *found = geometry.Find( id );
			if ( !found || session.LayerHidden( *entity ) ) { continue; }
			const auto &cached = *found;
			const bool wholeSelected = session.IsSelected( { id, WholeEntity } );
			if ( cached.point ) {
				if ( pass != ( wholeSelected ? 3 : 1 ) ) { continue; }
				float x, y;
				screen( cached.origin, x, y );
				const float half = static_cast<float>( ( std::max )( 3.0, 8.0 / frame.scale ) );
				batch.Rect( x - half, y - half, half * 2, half * 2 );
				++stats.points;
				continue;
			}
			for ( std::size_t i = 0; i < cached.primitives.size(); ++i ) {
				const auto &item = cached.primitives[i];
				if ( !item.valid ) { continue; }
				const bool chosen = wholeSelected || session.IsSelected( { id, i } );
				const bool patch = item.primitive.GetKind() == Primitive::Kind::Patch;
				const int wanted = chosen ? 3 : patch ? 2 : id == world || EqualNoCase( entity->Value( "classname" ), "func_group" ) ? 0 : 1;
				if ( wanted != pass ) { continue; }
				// The projected box of the bounds' eight corners along right/up.
				const double lu = item.mins.Dot( frame.right ), hu = item.maxs.Dot( frame.right );
				const double lv = item.mins.Dot( frame.up ), hv = item.maxs.Dot( frame.up );
				const float extentX = static_cast<float>( std::fabs( hu - lu ) / frame.scale ), extentY = static_cast<float>( std::fabs( hv - lv ) / frame.scale );
				float sx0, sy0, sx1, sy1;
				screen( frame.center + frame.right * ( ( std::min )( lu, hu ) - frame.center.Dot( frame.right ) ) + frame.up * ( ( std::max )( lv, hv ) - frame.center.Dot( frame.up ) ), sx0, sy0 );
				screen( frame.center + frame.right * ( ( std::max )( lu, hu ) - frame.center.Dot( frame.right ) ) + frame.up * ( ( std::min )( lv, hv ) - frame.center.Dot( frame.up ) ), sx1, sy1 );
				if ( sx1 < rect.x || sx0 > rect.x + rect.w || sy1 < rect.y || sy0 > rect.y + rect.h ) { continue; }
				++stats.primitives;
				if ( ( extentX < 0.75f && extentY < 0.75f ) && !chosen ) { continue; }
				if ( ( extentX < 5 && extentY < 5 ) || static_cast<std::size_t>( stats.lines + batch.lines ) > lineBudget ) {
					batch.Rect( sx0, sy0, sx1 - sx0, sy1 - sy0 );
					++stats.boxes;
					continue;
				}
				++stats.outlines;
				for ( const auto &polygon : item.polygons ) {
					for ( std::size_t k = 0; k < polygon.size(); ++k ) {
						float x0, y0, x1, y1;
						screen( polygon[k], x0, y0 ); screen( polygon[( k + 1 ) % polygon.size()], x1, y1 );
						if ( std::fabs( x1 - x0 ) + std::fabs( y1 - y0 ) < 0.3f ) { continue; }
						batch.Line( x0, y0, x1, y1 );
					}
				}
			}
		}
		stats.lines += batch.lines;
	}
		cache.key = key.hash;
		cache.valid = true;
		cache.stats = stats;
		Replay( cache.batches );
	}
	if ( overlay.rubber ) {
		LineBatch rubber( dc, rect );
		rubber.Begin( idVec4( 1.0f, 0.85f, 0.3f, 1 ) );
		float x0, y0, x1, y1;
		screen( overlay.a, x0, y0 ); screen( overlay.b, x1, y1 );
		rubber.Rect( ( std::min )( x0, x1 ), ( std::min )( y0, y1 ), std::fabs( x1 - x0 ), std::fabs( y1 - y0 ) );
	}
	static const char *names[] = { "#str_42950", "#str_42951", "#str_42952", "#str_42953" };
	DrawLabel( dc, rect, va( "%s  %s %g", Text( names[mode & 3] ), Text( "#str_42954" ), session.view.grid ) );
	return stats;
}

int DrawSelection3D( idDeviceContext *dc, const idRectangle &rect, const idLevelEditorPreview &preview, idLevelEditorSession &session ) {
	const auto &projection = preview.LastProjection();
	if ( !projection.valid || session.selection.empty() ) { return 0; }
	const MapGeometryCache &geometry = session.Geometry();
	LineBatch batch( dc, rect );
	batch.Begin( idVec4( 1.0f, 0.55f, 0.05f, 1 ) );
	const auto project = [&]( const idVec3 &p, float &x, float &y ) {
		const idVec3 d = p - projection.origin;
		const float forward = d * projection.axis[0], left = d * projection.axis[1], up = d * projection.axis[2];
		x = rect.x + ( 0.5f - 0.5f * left / ( forward * projection.tanX ) ) * rect.w;
		y = rect.y + ( 0.5f - 0.5f * up / ( forward * projection.tanY ) ) * rect.h;
	};
	const float nearPlane = 4.0f;
	for ( const auto &item : session.selection ) {
		const auto *cached = geometry.Find( item.entity );
		if ( !cached || cached->point ) { continue; }
		for ( std::size_t i = 0; i < cached->primitives.size(); ++i ) {
			if ( item.primitive != WholeEntity && item.primitive != i ) { continue; }
			for ( const auto &polygon : cached->primitives[i].polygons ) {
				for ( std::size_t k = 0; k < polygon.size(); ++k ) {
					idVec3 a( static_cast<float>( polygon[k].x ), static_cast<float>( polygon[k].y ), static_cast<float>( polygon[k].z ) );
					const auto &nb = polygon[( k + 1 ) % polygon.size()];
					idVec3 b( static_cast<float>( nb.x ), static_cast<float>( nb.y ), static_cast<float>( nb.z ) );
					// Clip against the near plane before the perspective divide.
					float fa = ( a - projection.origin ) * projection.axis[0], fb = ( b - projection.origin ) * projection.axis[0];
					if ( fa < nearPlane && fb < nearPlane ) { continue; }
					if ( fa < nearPlane ) { a += ( b - a ) * ( ( nearPlane - fa ) / ( fb - fa ) ); }
					else if ( fb < nearPlane ) { b += ( a - b ) * ( ( nearPlane - fb ) / ( fa - fb ) ); }
					float x0, y0, x1, y1;
					project( a, x0, y0 ); project( b, x1, y1 );
					batch.Line( x0, y0, x1, y1 );
				}
			}
		}
	}
	return batch.lines;
}

}
