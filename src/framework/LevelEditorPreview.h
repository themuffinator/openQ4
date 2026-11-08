// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENQ4_LEVEL_EDITOR_PREVIEW_H
#define OPENQ4_LEVEL_EDITOR_PREVIEW_H

#include "../tools/leveleditor/MapDocument.h"
#include <functional>
#include <memory>
#include <map>
#include <set>
#include <vector>

class idDeviceContext;
class idRectangle;

class idLevelEditorPreview {
public:
	struct Camera { idVec3 target = vec3_origin; float yaw = 225.0f, pitch = 25.0f, distance = 512.0f; };
	struct Projection { idVec3 origin = vec3_origin; idMat3 axis; float tanX = 1, tanY = 1; bool valid = false; };
	static constexpr std::size_t ChunkPrimitives = 128;
	idLevelEditorPreview();
	~idLevelEditorPreview();
	// hidden: entities on hidden layers stay out of the render world.
	// budgetMs > 0 stops tessellating once the budget is spent; call again
	// on later frames until it returns true. Unchanged entities are reused.
	bool Rebuild( const oq4editor::MapDocument &document, const std::function<bool( const oq4editor::Entity & )> &hidden = nullptr, double budgetMs = 0 );
	void Draw( idDeviceContext *dc, const idRectangle &rect, int time );
	void Focus( oq4editor::EntityId id );
	void FocusBounds( const idBounds &bounds );
	void Orbit( float yawDelta, float pitchDelta, float zoom );
	void Pan( float x, float y );
	// Nearest entity with a real model along the view ray (fraction of 2M units).
	oq4editor::EntityId Pick( float x, float y, float aspect, float *fraction = nullptr ) const;
	void Ray( float x, float y, float aspect, idVec3 &start, idVec3 &direction ) const;
	void SetSelection( const std::set<oq4editor::EntityId> &whole );
	// Replaces point markers by their entity models, a few per call, so that
	// opening a large map never waits for every model to load.
	int UpgradeModels( double budgetMs );
	int PendingModels() const;
	int Models() const { return models; }
	int Skipped() const { return skipped; }
	int Triangles() const { return triangles; }
	int Entities() const;
	Camera GetCamera() const { return camera; }
	void SetCamera( const Camera &value );
	const Projection &LastProjection() const { return projection; }
private:
	// Render entities are chunks of an entity's primitives.
	using Key = std::uint64_t;
	static Key MakeKey( oq4editor::EntityId id, std::size_t chunk ) { return ( static_cast<Key>( id ) << 20 ) | static_cast<Key>( chunk ); }
	static oq4editor::EntityId KeyEntity( Key key ) { return key >> 20; }
	struct MeshCache;
	std::unique_ptr<MeshCache> cache;
	struct Record {
		int handle = -1, version = 0, skipped = 0, triangles = 0;
		std::size_t vertices = 0;
		renderEntity_t entity = {};
		idVec3 color = idVec3( 1, 1, 1 );
		std::vector<std::string> primitives;
		std::string modelName, prefix;
		bool ownsModel = true, modelTried = false;
		idMat3 axis;
	};
	void Clear();
	void Release( Record &record );
	void Tint( Record &record, bool selected );
	idRenderWorld *world = nullptr;
	std::map<Key, Record> records;
	std::set<oq4editor::EntityId> selected;
	idBounds bounds;
	Camera camera;
	Projection projection;
	int lightHandle = -1, skipped = 0, triangles = 0, models = 0;
};

#endif
