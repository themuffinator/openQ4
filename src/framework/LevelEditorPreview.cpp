// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#include "LevelEditor.h"
#include "LevelEditorPreview.h"
#include "../ui/DeviceContext.h"
#include <cmath>
#include <map>
#include <memory>
#include <vector>
#include <unordered_map>

namespace {
constexpr int LexerFlags = LEXFL_NOSTRINGCONCAT | LEXFL_NOSTRINGESCAPECHARS | LEXFL_ALLOWPATHNAMES | LEXFL_NOFATALERRORS;
constexpr std::size_t MaxPreviewVertices = 2000000;
struct Mesh { std::vector<idDrawVert> verts; std::vector<int> indexes; };
struct SurfaceKey {
	const idMaterial *material;
	std::size_t part;
	bool operator<( const SurfaceKey &other ) const {
		return material == other.material ? part < other.part : std::less<const idMaterial *>()( material, other.material );
	}
};
using Surfaces = std::map<SurfaceKey, Mesh>;
Mesh &Batch( Surfaces &surfaces, const idMaterial *material ) {
	// Flare, autosprite and other discrete shaders must retain their individual
	// faces. Merging them violates their quad/primitive deformation contract.
	return surfaces[{ material, material->IsDiscrete() ? surfaces.size() + 1 : 0 }];
}

bool Finite( const idVec3 &v ) {
	return std::isfinite( v.x ) && std::isfinite( v.y ) && std::isfinite( v.z ) &&
		idMath::Fabs( v.x ) < 1000000.0f && idMath::Fabs( v.y ) < 1000000.0f && idMath::Fabs( v.z ) < 1000000.0f;
}

idVec3 Origin( const oq4editor::Entity &entity ) {
	idVec3 origin = vec3_origin;
	const std::string text = entity.Value( "origin" );
	if ( sscanf( text.c_str(), "%f %f %f", &origin.x, &origin.y, &origin.z ) != 3 || !Finite( origin ) ) { return vec3_origin; }
	return origin;
}

idVec3 MarkerColor( const oq4editor::Entity &entity ) {
	const auto classname = entity.Value( "classname" );
	if ( oq4editor::EqualNoCase( classname, "light" ) ) { return idVec3( 0.8f, 0.7f, 0.3f ); }
	if ( classname.find( "info_player" ) == 0 ) { return idVec3( 0.3f, 0.8f, 0.45f ); }
	return idVec3( 0.35f, 0.6f, 0.85f );
}

bool PatchHeaderSafe( const std::string &source, bool explicitSubdivisions ) {
	Lexer lexer( source.c_str(), static_cast<int>( source.size() ), "editor patch header", LexerFlags );
	idToken token;
	float info[7] = {};
	if ( !lexer.ExpectTokenString( "{" ) || !lexer.ReadToken( &token ) || !lexer.ExpectTokenString( "{" ) ||
		!lexer.ReadToken( &token ) || !lexer.Parse1DMatrix( explicitSubdivisions ? 7 : 5, info ) ) { return false; }
	// Validate before the legacy parser allocates width * height control points.
	for ( int i = 0; i < 2; ++i ) {
		if ( !std::isfinite( info[i] ) || info[i] < 3 || info[i] > 65 ||
			info[i] != std::floor( info[i] ) || static_cast<int>( info[i] ) % 2 == 0 ) { return false; }
	}
	return !explicitSubdivisions || ( std::isfinite( info[2] ) && std::isfinite( info[3] ) &&
		info[2] >= 1 && info[2] <= 32 && info[3] >= 1 && info[3] <= 32 &&
		info[2] == std::floor( info[2] ) && info[3] == std::floor( info[3] ) );
}

bool AppendBrush( const idMapBrush &brush, Surfaces &surfaces, std::size_t &vertexCount ) {
	if ( brush.GetNumSides() < 4 || brush.GetNumSides() > 128 ) { return false; }
	for ( int side = 0; side < brush.GetNumSides(); ++side ) {
		const auto &plane = brush.GetSide( side )->GetPlane();
		if ( !Finite( plane.Normal() ) || !std::isfinite( plane[3] ) || idMath::Fabs( plane[3] ) > 1000000.0f ||
			plane.Normal().LengthSqr() < 0.5f || plane.Normal().LengthSqr() > 1.5f ) { return false; }
	}
	for ( int side = 0; side < brush.GetNumSides(); ++side ) {
		const auto *face = brush.GetSide( side );
		const idMaterial *material = declManager->FindMaterial( face->GetMaterial() );
		if ( !material->IsDrawn() ) { continue; }
		idWinding winding( face->GetPlane() );
		for ( int clip = 0; clip < brush.GetNumSides() && winding.GetNumPoints(); ++clip ) {
			if ( clip != side ) { winding.ClipInPlace( -brush.GetSide( clip )->GetPlane(), 0.01f ); }
		}
		const int count = winding.GetNumPoints();
		if ( count < 3 ) { continue; }
		if ( material->Deform() == DFRM_FLARE && count != 4 ) { return false; }
		if ( vertexCount + count > MaxPreviewVertices ) { return false; }
		idVec4 tex[2]; face->GetTextureVectors( tex );
		for ( const auto &vector : tex ) { for ( int i = 0; i < 4; ++i ) { if ( !std::isfinite( vector[i] ) ) { return false; } } }
		// Validate the whole face before appending any vertices. A rejected
		// face must not leave uncounted allocations or nonfinite GPU attributes.
		for ( int i = 0; i < count; ++i ) {
			const idVec3 point = winding[i].ToVec3();
			if ( !Finite( point ) || !std::isfinite( point * tex[0].ToVec3() + tex[0][3] ) ||
				!std::isfinite( point * tex[1].ToVec3() + tex[1][3] ) ) { return false; }
		}
		Mesh &mesh = Batch( surfaces, material );
		const int start = static_cast<int>( mesh.verts.size() );
		for ( int i = 0; i < count; ++i ) {
			idDrawVert vertex = {}; vertex.Clear();
			vertex.xyz = winding[i].ToVec3();
			vertex.SetNormal( face->GetPlane().Normal() );
			vertex.st.Set( vertex.xyz * tex[0].ToVec3() + tex[0][3], vertex.xyz * tex[1].ToVec3() + tex[1][3] );
			vertex.SetColor( 0xffffffff ); mesh.verts.push_back( vertex );
		}
		for ( int i = 1; i + 1 < count; ++i ) {
			mesh.indexes.insert( mesh.indexes.end(), { start, start + i, start + i + 1 } );
		}
		vertexCount += count;
	}
	return true;
}

bool AppendPrimitive( const std::string &source, const idVec3 &origin, int version, Surfaces &surfaces, std::size_t &vertexCount ) {
	Lexer lexer( source.c_str(), static_cast<int>( source.size() ), "editor primitive", LexerFlags );
	idToken kind;
	if ( !lexer.ExpectTokenString( "{" ) || !lexer.ReadToken( &kind ) ) { return false; }
	if ( kind == "patchDef2" || kind == "patchDef3" ) {
		if ( !PatchHeaderSafe( source, kind == "patchDef3" ) ) { return false; }
		std::unique_ptr<idMapPatch> patch( idMapPatch::Parse( lexer, origin, kind == "patchDef3", version ) );
		if ( !patch ) { return false; }
		for ( int i = 0; i < patch->GetNumVertices(); ++i ) {
			const idVec3 xyz = (*patch)[i].xyz;
			const idVec2 st = (*patch)[i].st;
			if ( !Finite( xyz ) || !std::isfinite( st.x ) || !std::isfinite( st.y ) || idMath::Fabs( st.x ) > 1000000 || idMath::Fabs( st.y ) > 1000000 ) { return false; }
			(*patch)[i] = idDrawVert();
			(*patch)[i].Clear(); (*patch)[i].xyz = xyz; (*patch)[i].st = st; (*patch)[i].SetColor( 0xffffffff );
		}
		// Preview tessellation is bounded; dmap retains responsibility for the
		// final adaptive render/collision mesh. Never change authoring controls.
		patch->SubdivideExplicit( 4, 4, true, true );
		if ( vertexCount + patch->GetNumVertices() > MaxPreviewVertices ) { return false; }
		const idMaterial *material = declManager->FindMaterial( patch->GetMaterial() );
		if ( !material->IsDrawn() ) { return true; }
		if ( material->Deform() == DFRM_FLARE && ( patch->GetNumVertices() != 4 || patch->GetNumIndexes() != 6 ) ) { return false; }
		Mesh &mesh = Batch( surfaces, material );
		const int start = static_cast<int>( mesh.verts.size() );
		for ( int i = 0; i < patch->GetNumVertices(); ++i ) {
			// The legacy subdivider writes positions, UVs and normals only.
			idDrawVert vertex = {}; vertex.Clear();
			vertex.xyz = (*patch)[i].xyz; vertex.st = (*patch)[i].st; vertex.SetNormal( (*patch)[i].GetNormal() );
			vertex.SetColor( 0xffffffff ); mesh.verts.push_back( vertex );
		}
		for ( int i = 0; i < patch->GetNumIndexes(); ++i ) { mesh.indexes.push_back( start + patch->GetIndexes()[i] ); }
		vertexCount += patch->GetNumVertices();
		return true;
	}
	std::unique_ptr<idMapBrush> brush;
	if ( kind == "brushDef" || kind == "brushDef2" || kind == "brushDef3" ) {
		brush.reset( idMapBrush::Parse( lexer, origin, kind != "brushDef", version ) );
	} else if ( kind == "(" ) {
		lexer.UnreadToken( &kind ); brush.reset( idMapBrush::ParseQ3( lexer, origin ) );
	} else { return false; }
	return brush && AppendBrush( *brush, surfaces, vertexCount );
}

bool AppendMarker( Surfaces &surfaces, std::size_t &vertexCount ) {
	if ( vertexCount + 24 > MaxPreviewVertices ) { return false; }
	Mesh &mesh = Batch( surfaces, declManager->FindMaterial( "_white" ) );
	static const int corners[6][4] = { {0,2,3,1}, {4,5,7,6}, {0,1,5,4}, {2,6,7,3}, {0,4,6,2}, {1,3,7,5} };
	for ( const auto &face : corners ) {
		const int start = static_cast<int>( mesh.verts.size() );
		for ( int corner : face ) {
			idDrawVert vertex = {}; vertex.Clear();
			vertex.xyz.Set( corner & 1 ? 4.0f : -4.0f, corner & 2 ? 4.0f : -4.0f, corner & 4 ? 4.0f : -4.0f );
			vertex.SetColor( 0xffffffff ); mesh.verts.push_back( vertex );
		}
		mesh.indexes.insert( mesh.indexes.end(), { start, start + 1, start + 2, start, start + 2, start + 3 } );
	}
	vertexCount += 24;
	return true;
}
}

namespace {

// The model a point entity shows: its own "model" key, unless that names
// its inline brush model, otherwise (resolved later, see UpgradeModels) the
// model its entityDef declares. "@" marks the deferred entityDef lookup.
std::string PointModel( const oq4editor::Entity &entity ) {
	const std::string own = entity.Value( "model" );
	if ( !own.empty() && !oq4editor::EqualNoCase( own, entity.Value( "name" ) ) ) { return own; }
	const std::string classname = entity.Value( "classname" );
	return classname.empty() ? std::string() : "@" + classname;
}

std::string DefinitionModel( const std::string &classname ) {
	idLevelEditorLookup lookup;
	const idDecl *decl = declManager->FindType( DECL_ENTITYDEF, classname.c_str(), false );
	if ( !decl || decl->GetState() == DS_DEFAULTED ) { return {}; }
	return static_cast<const idDeclEntityDef *>( decl )->dict.GetString( "model" );
}

idMat3 EntityAxis( const oq4editor::Entity &entity ) {
	idMat3 axis;
	const std::string rotation = entity.Value( "rotation" );
	float m[9];
	if ( !rotation.empty() && sscanf( rotation.c_str(), "%f %f %f %f %f %f %f %f %f", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5], &m[6], &m[7], &m[8] ) == 9 ) {
		axis = idMat3( m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8] );
		return axis;
	}
	float yaw = 0;
	sscanf( entity.Value( "angle" ).c_str(), "%f", &yaw );
	return idAngles( 0, std::isfinite( yaw ) ? yaw : 0, 0 ).ToMat3();
}

}

struct CachedMesh {
	Surfaces surfaces;
	std::size_t vertices = 0;
	int references = 0; // chunk records assembled from this tessellation
	bool ok = false, done = false;
};
struct idLevelEditorPreview::MeshCache { std::unordered_map<std::string, CachedMesh> meshes; };

idLevelEditorPreview::idLevelEditorPreview() : cache( std::make_unique<MeshCache>() ) {}
idLevelEditorPreview::~idLevelEditorPreview() { Clear(); }
void idLevelEditorPreview::Clear() {
	for ( auto &[id, record] : records ) { Release( record ); }
	if ( world ) { renderSystem->FreeRenderWorld( world ); world = nullptr; }
	records.clear(); bounds.Clear(); lightHandle = -1; skipped = triangles = models = 0; selected.clear();
	if ( cache ) { cache->meshes.clear(); }
}
void idLevelEditorPreview::Release( Record &record ) {
	if ( cache ) {
		for ( const auto &text : record.primitives ) {
			const auto found = cache->meshes.find( record.prefix + text );
			if ( found != cache->meshes.end() && --found->second.references <= 0 ) { cache->meshes.erase( found ); }
		}
	}
	if ( record.handle >= 0 ) { world->FreeEntityDef( record.handle ); }
	if ( record.entity.hModel && record.ownsModel ) { renderModelManager->FreeModel( record.entity.hModel ); }
	if ( !record.ownsModel ) { --models; }
	record = Record();
}

void idLevelEditorPreview::Tint( Record &record, bool isSelected ) {
	const idVec3 color = isSelected && record.primitives.empty() && record.ownsModel ? idVec3( 1.0f, 0.55f, 0.05f ) : record.color;
	for ( int i = 0; i < 3; ++i ) { record.entity.shaderParms[i] = color[i]; }
	record.entity.shaderParms[3] = 1.0f;
	record.entity.outlineColor = isSelected ? idVec4( 1.0f, 0.55f, 0.05f, 1.0f ) : vec4_zero;
	record.entity.outlineWidth = 2.0f;
}

bool idLevelEditorPreview::Rebuild( const oq4editor::MapDocument &document, const std::function<bool( const oq4editor::Entity & )> &hidden, double budgetMs ) {
	const int startTime = Sys_Milliseconds();
	bool complete = true;
	if ( !world ) { world = renderSystem->AllocRenderWorld(); world->InitFromMap( nullptr ); }
	// Entities are split into chunks of primitives, each its own render
	// entity, and every primitive's tessellation is cached by its exact
	// source bytes. Opening a map can therefore spread its work over frames,
	// and editing one world brush re-tessellates that brush and reassembles
	// one chunk instead of the whole world.
	auto &meshes = cache->meshes;
	std::set<Key> live;
	bounds.Clear(); skipped = triangles = 0;
	int rebuilt = 0, tessellated = 0;
	std::size_t vertexCount = 0;
	for ( const auto &sourceEntity : document.Entities() ) {
		if ( hidden && hidden( sourceEntity ) ) { continue; }
		const bool isWorld = sourceEntity.id == document.Entities().front().id;
		const idVec3 origin = isWorld ? vec3_origin : Origin( sourceEntity );
		const idVec3 color = sourceEntity.primitiveCount ? idVec3( 1, 1, 1 ) : MarkerColor( sourceEntity );
		const std::string modelName = sourceEntity.primitiveCount || isWorld ? std::string() : PointModel( sourceEntity );
		const idMat3 axis = sourceEntity.primitiveCount || isWorld ? mat3_identity : EntityAxis( sourceEntity );
		const std::size_t primitives = sourceEntity.primitiveRanges.size();
		const std::size_t chunks = primitives == 0 ? ( isWorld ? 0 : 1 ) : ( primitives + ChunkPrimitives - 1 ) / ChunkPrimitives;
		const std::string prefix = va( "%d|%g %g %g|", document.Version(), origin.x, origin.y, origin.z );
		for ( std::size_t chunk = 0; chunk < chunks; ++chunk ) {
			const Key key = MakeKey( sourceEntity.id, chunk );
			live.insert( key );
			Record &record = records[key];
			const std::size_t first = chunk * ChunkPrimitives, last = ( std::min )( primitives, first + ChunkPrimitives );
			bool changed = record.version != document.Version() || record.primitives.size() != last - first || record.modelName != modelName;
			for ( std::size_t i = first; !changed && i < last; ++i ) {
				const auto &range = sourceEntity.primitiveRanges[i];
				if ( sourceEntity.source.compare( range.first, range.second - range.first, record.primitives[i - first] ) != 0 ) { changed = true; }
			}
			if ( !record.primitives.empty() && record.entity.origin != origin ) { changed = true; }
			if ( !changed ) {
				triangles += record.triangles; skipped += record.skipped; vertexCount += record.vertices;
				if ( record.handle >= 0 ) {
					if ( record.entity.origin != origin || record.color != color || record.axis != axis ) {
						record.entity.origin = origin; record.color = color;
						record.axis = axis; record.entity.axis = record.ownsModel ? mat3_identity : axis;
						Tint( record, selected.count( sourceEntity.id ) != 0 );
						world->UpdateEntityDef( record.handle, &record.entity );
					}
					bounds.AddBounds( record.entity.bounds.Rotate( record.entity.axis ) + record.entity.origin );
				}
				continue;
			}
			// Tessellate what the cache lacks, within the frame budget. A
			// chunk that cannot finish keeps its previous model for now; once
			// the budget is spent, remaining chunks are not even inspected.
			const bool spent = budgetMs > 0 && tessellated > 0 && Sys_Milliseconds() - startTime >= budgetMs;
			bool ready = !spent;
			if ( spent ) {
				complete = false;
				triangles += record.triangles; skipped += record.skipped; vertexCount += record.vertices;
				if ( record.handle >= 0 ) { bounds.AddBounds( record.entity.bounds.Rotate( record.entity.axis ) + record.entity.origin ); }
				continue;
			}
			std::vector<std::string> texts;
			for ( std::size_t i = first; i < last; ++i ) {
				const auto &range = sourceEntity.primitiveRanges[i];
				texts.push_back( sourceEntity.source.substr( range.first, range.second - range.first ) );
				auto &mesh = meshes[prefix + texts.back()];
				if ( mesh.done ) { continue; }
				if ( budgetMs > 0 && tessellated > 0 && Sys_Milliseconds() - startTime >= budgetMs ) { ready = false; continue; }
				std::size_t count = 0;
				mesh.ok = AppendPrimitive( texts.back(), origin, document.Version(), mesh.surfaces, count );
				mesh.vertices = count;
				mesh.done = true;
				++tessellated;
			}
			if ( ready && primitives == 0 && budgetMs > 0 && tessellated > 0 && Sys_Milliseconds() - startTime >= budgetMs ) { ready = false; }
			if ( !ready ) {
				complete = false;
				triangles += record.triangles; skipped += record.skipped; vertexCount += record.vertices;
				if ( record.handle >= 0 ) { bounds.AddBounds( record.entity.bounds.Rotate( record.entity.axis ) + record.entity.origin ); }
				continue;
			}
			const bool tinted = selected.count( sourceEntity.id ) != 0;
			// Reference the new tessellations before releasing the old ones,
			// so primitives the chunk keeps are not evicted.
			for ( const auto &text : texts ) { ++meshes[prefix + text].references; }
			Release( record ); ++rebuilt;
			if ( primitives == 0 ) { ++tessellated; }
			record.version = document.Version(); record.entity.origin = origin; record.prefix = prefix;
			record.modelName = modelName; record.axis = axis; record.primitives = std::move( texts );
			Surfaces surfaces;
			std::size_t vertices = 0;
			for ( const auto &text : record.primitives ) {
				const auto &mesh = meshes[prefix + text];
				if ( !mesh.ok || vertexCount + vertices + mesh.vertices > MaxPreviewVertices ) { ++record.skipped; continue; }
				for ( const auto &[part, piece] : mesh.surfaces ) {
					Mesh &target = Batch( surfaces, part.material );
					const int base = static_cast<int>( target.verts.size() );
					target.verts.insert( target.verts.end(), piece.verts.begin(), piece.verts.end() );
					for ( const int index : piece.indexes ) { target.indexes.push_back( base + index ); }
				}
				vertices += mesh.vertices;
			}
			if ( primitives == 0 && !AppendMarker( surfaces, vertices ) ) { ++record.skipped; }
			record.vertices = vertices; vertexCount += vertices; skipped += record.skipped;
			if ( surfaces.empty() ) { continue; }
			idRenderModel *model = renderModelManager->AllocModel();
			// Owned exclusively by this preview: never register or replace _areaN
			// models in the global model catalog used by the running game.
			model->InitEmpty( va( "_editor_entity_%llu_%zu", static_cast<unsigned long long>( sourceEntity.id ), chunk ) );
			int surfaceId = 0;
			for ( auto &[part, mesh] : surfaces ) {
				if ( mesh.indexes.empty() ) { continue; }
				modelSurface_t surface = {};
				surface.id = surfaceId++; surface.shader = part.material;
				surface.geometry = model->AllocSurfaceTriangles( static_cast<int>( mesh.verts.size() ), static_cast<int>( mesh.indexes.size() ) );
				surface.geometry->numVerts = static_cast<int>( mesh.verts.size() );
				surface.geometry->numIndexes = static_cast<int>( mesh.indexes.size() );
				for ( std::size_t i = 0; i < mesh.verts.size(); ++i ) { surface.geometry->verts[i] = mesh.verts[i]; }
				for ( std::size_t i = 0; i < mesh.indexes.size(); ++i ) { surface.geometry->indexes[i] = mesh.indexes[i]; }
				record.triangles += surface.geometry->numIndexes / 3;
				model->AddSurface( surface );
			}
			if ( !surfaceId ) { renderModelManager->FreeModel( model ); continue; }
			model->FinishSurfaces(); triangles += record.triangles;
			record.entity.hModel = model; record.entity.axis.Identity(); record.entity.origin = origin;
			record.entity.bounds = model->Bounds(); record.entity.noShadow = true;
			record.color = color; Tint( record, tinted );
			record.handle = world->AddEntityDef( &record.entity );
			bounds.AddBounds( record.entity.bounds + origin );
		}
	}
	for ( auto it = records.begin(); it != records.end(); ) {
		if ( !live.count( it->first ) ) { Release( it->second ); it = records.erase( it ); }
		else { ++it; }
	}
	if ( complete ) {
		// Every chunk is assembled: unreferenced tessellations are stale.
		for ( auto it = meshes.begin(); it != meshes.end(); ) { it = it->second.references > 0 ? std::next( it ) : meshes.erase( it ); }
	}
	if ( bounds.IsCleared() ) { bounds = idBounds( idVec3( -64, -64, -64 ), idVec3( 64, 64, 64 ) ); }
	if ( complete ) {
		common->Printf( "editor preview: entities=%d triangles=%d skipped=%d\n", Entities(), triangles, skipped );
	}
	common->Printf( "editor preview update: rebuilt=%d tessellated=%d elapsed=%dms complete=%d\n", rebuilt, tessellated, Sys_Milliseconds() - startTime, complete );
	return complete;
}

int idLevelEditorPreview::PendingModels() const {
	int pending = 0;
	for ( const auto &[id, record] : records ) { if ( !record.modelName.empty() && !record.modelTried ) { ++pending; } }
	return pending;
}

int idLevelEditorPreview::UpgradeModels( double budgetMs ) {
	if ( !world ) { return 0; }
	const double start = Sys_Milliseconds();
	int upgraded = 0;
	for ( auto &[id, record] : records ) {
		if ( record.modelName.empty() || record.modelTried || record.handle < 0 ) { continue; }
		if ( Sys_Milliseconds() - start >= budgetMs ) { break; }
		record.modelTried = true;
		const std::string name = record.modelName[0] == '@' ? DefinitionModel( record.modelName.substr( 1 ) ) : record.modelName;
		if ( name.empty() ) { continue; }
		idLevelEditorLookup lookup;
		idRenderModel *model = renderModelManager->CheckModel( name.c_str() );
		// Skinned models need an animation pose the editor does not run; they
		// keep their marker. Static meshes render as the game shows them.
		if ( !model || model->IsDefaultModel() || model->IsDynamicModel() != DM_STATIC || model->NumSurfaces() == 0 ) { continue; }
		if ( record.ownsModel && record.entity.hModel ) { renderModelManager->FreeModel( record.entity.hModel ); }
		record.entity.hModel = model;
		record.ownsModel = false;
		record.entity.axis = record.axis;
		record.entity.bounds = model->Bounds();
		record.color = idVec3( 1, 1, 1 );
		Tint( record, selected.count( KeyEntity( id ) ) != 0 );
		world->UpdateEntityDef( record.handle, &record.entity );
		++models; ++upgraded;
	}
	return upgraded;
}

int idLevelEditorPreview::Entities() const {
	int count = 0;
	for ( const auto &[id, record] : records ) { if ( record.handle >= 0 ) { ++count; } }
	return count;
}

void idLevelEditorPreview::FocusBounds( const idBounds &focus ) {
	if ( focus.IsCleared() ) { return; }
	camera.target = focus.GetCenter();
	camera.distance = idMath::ClampFloat( 96.0f, 60000.0f, ( focus[1] - focus[0] ).Length() * 0.8f + 64.0f );
}

void idLevelEditorPreview::Focus( oq4editor::EntityId id ) {
	idBounds focus;
	focus.Clear();
	for ( auto it = records.lower_bound( MakeKey( id, 0 ) ); it != records.end() && KeyEntity( it->first ) == id; ++it ) {
		if ( it->second.handle >= 0 ) { focus.AddBounds( it->second.entity.bounds.Rotate( it->second.entity.axis ) + it->second.entity.origin ); }
	}
	FocusBounds( focus.IsCleared() ? bounds : focus );
}
void idLevelEditorPreview::Orbit( float yawDelta, float pitchDelta, float zoom ) {
	camera.yaw = idMath::AngleNormalize360( camera.yaw + yawDelta ); camera.pitch = idMath::ClampFloat( -85.0f, 85.0f, camera.pitch + pitchDelta );
	camera.distance = idMath::ClampFloat( 32.0f, 60000.0f, camera.distance * zoom );
}
void idLevelEditorPreview::SetCamera( const Camera &value ) {
	camera.target = value.target;
	camera.yaw = idMath::AngleNormalize360( value.yaw );
	camera.pitch = idMath::ClampFloat( -85.0f, 85.0f, value.pitch );
	camera.distance = idMath::ClampFloat( 32.0f, 60000.0f, value.distance );
}
void idLevelEditorPreview::SetSelection( const std::set<oq4editor::EntityId> &whole ) {
	if ( whole == selected ) { return; }
	std::set<oq4editor::EntityId> changed;
	for ( const auto id : selected ) { if ( !whole.count( id ) ) { changed.insert( id ); } }
	for ( const auto id : whole ) { if ( !selected.count( id ) ) { changed.insert( id ); } }
	selected = whole;
	for ( const auto id : changed ) {
		for ( auto it = records.lower_bound( MakeKey( id, 0 ) ); it != records.end() && KeyEntity( it->first ) == id; ++it ) {
			if ( it->second.handle < 0 ) { continue; }
			Tint( it->second, selected.count( id ) != 0 );
			world->UpdateEntityDef( it->second.handle, &it->second.entity );
		}
	}
}

void idLevelEditorPreview::Pan( float x, float y ) {
	const idMat3 axis = idAngles( camera.pitch, camera.yaw, 0 ).ToMat3();
	camera.target += ( axis[1] * x + axis[2] * y ) * camera.distance * 1.53465f;
}

void idLevelEditorPreview::Ray( float x, float y, float aspect, idVec3 &start, idVec3 &direction ) const {
	const idMat3 axis = idAngles( camera.pitch, camera.yaw, 0 ).ToMat3();
	start = camera.target - axis[0] * camera.distance;
	direction = axis[0] + axis[1] * ( ( 1.0f - 2.0f * x ) * 0.767327f ) + axis[2] * ( ( 1.0f - 2.0f * y ) * 0.767327f / aspect );
	direction.Normalize();
}

oq4editor::EntityId idLevelEditorPreview::Pick( float x, float y, float aspect, float *fraction ) const {
	if ( !world || aspect <= 0 ) { return 0; }
	idVec3 start, ray;
	Ray( x, y, aspect, start, ray );
	const idVec3 end = start + ray * 2000000.0f;
	float nearest = 1.0f;
	oq4editor::EntityId hit = 0;
	for ( const auto &[id, record] : records ) {
		if ( record.handle < 0 || !record.primitives.empty() || record.ownsModel ) { continue; }
		modelTrace_t trace = {};
		if ( world->ModelTrace( trace, record.handle, start, end, 0.0f ) && trace.fraction < nearest ) { nearest = trace.fraction; hit = KeyEntity( id ); }
	}
	if ( fraction ) { *fraction = nearest; }
	return hit;
}

void idLevelEditorPreview::Draw( idDeviceContext *dc, const idRectangle &rect, int time ) {
	if ( !world || rect.w <= 0 || rect.h <= 0 ) { return; }
	float x = rect.x, y = rect.y, width = rect.w, height = rect.h;
	if ( dc->ClippedCoords( &x, &y, &width, &height, nullptr, nullptr, nullptr, nullptr ) || width <= 0 || height <= 0 ) { return; }
	renderView_t view = {};
	view.fov_x = 75.0f;
	view.fov_y = 2.0f * atan( tan( view.fov_x * idMath::M_DEG2RAD * 0.5f ) * height / width ) * idMath::M_RAD2DEG;
	dc->AdjustCoords( &x, &y, &width, &height );
	const float sx = engineWindowState.uiViewportWidth / 640.0f, sy = engineWindowState.uiViewportHeight / 480.0f;
	view.x = idMath::Ftoi( ( engineWindowState.uiViewportX + x * sx ) * 640.0f / engineWindowState.vidWidth );
	view.y = idMath::Ftoi( ( engineWindowState.uiViewportY + y * sy ) * 480.0f / engineWindowState.vidHeight );
	view.width = Max( 1, idMath::Ftoi( width * sx * 640.0f / engineWindowState.vidWidth ) );
	view.height = Max( 1, idMath::Ftoi( height * sy * 480.0f / engineWindowState.vidHeight ) );
	view.viewaxis = idAngles( camera.pitch, camera.yaw, 0 ).ToMat3();
	view.vieworg = camera.target - view.viewaxis[0] * camera.distance;
	view.time = time;
	for ( int i = 0; i < 4; ++i ) { view.shaderParms[i] = 1.0f; }
	projection.origin = view.vieworg;
	projection.axis = view.viewaxis;
	projection.tanX = tan( view.fov_x * idMath::M_DEG2RAD * 0.5f );
	projection.tanY = tan( view.fov_y * idMath::M_DEG2RAD * 0.5f );
	projection.valid = true;
	renderLight_t light = {}; light.axis.Identity(); light.origin = view.vieworg;
	light.pointLight = true; light.noShadows = true;
	light.lightRadius.Set( 65536, 65536, 65536 );
	for ( int i = 0; i < 4; ++i ) { light.shaderParms[i] = 1.0f; }
	if ( lightHandle < 0 ) { lightHandle = world->AddLightDef( &light ); } else { world->UpdateLightDef( lightHandle, &light ); }
	world->RenderScene( &view );
}
