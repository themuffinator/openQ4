// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENQ4_EDITOR_MAP_EDITING_H
#define OPENQ4_EDITOR_MAP_EDITING_H

#include "MapDocument.h"
#include "MapGeometry.h"

#include <functional>
#include <map>
#include <set>

// Geometry-aware editing commands over MapDocument. Each command validates
// its whole result before the first change and records one undo step (or
// joins the caller's open transaction, so a drag stays one step).
namespace oq4editor {

constexpr std::size_t WholeEntity = static_cast<std::size_t>( -1 );

struct SelectionItem {
	EntityId entity = 0;
	std::size_t primitive = WholeEntity;
	bool operator<( const SelectionItem &o ) const { return entity == o.entity ? primitive < o.primitive : entity < o.entity; }
	bool operator==( const SelectionItem &o ) const { return entity == o.entity && primitive == o.primitive; }
};
using Selection = std::vector<SelectionItem>;

struct Reference {
	EntityId entity = 0;
	std::string key;
};

// Keys whose values name another entity. Quake 4 uses "target", "targetN",
// "bind" and friends; classes may declare more through IsEntityKey.
bool IsReferenceKey( const std::string &key );
bool ParseVector( const std::string &text, Vec3 &value );
std::string FormatVector( const Vec3 &value );
Vec3 EntityOrigin( const MapDocument &document, const Entity &entity, bool *present = nullptr );
bool IsWorld( const MapDocument &document, const Entity &entity );
std::string EntityLayer( const Entity &entity );

class MapEditor {
public:
	std::function<bool( const std::string &classname, const std::string &key )> IsEntityKey;

	// Moves selected entities and primitives. Point entities move their
	// origin (and turn their orientation keys for rotations); geometry
	// entities move their origin with their brushes and patches; selected
	// primitives move alone. The selection's bounds pivot is the caller's.
	bool Transform( MapDocument &document, const Selection &selection, const Affine &world, bool textureLock, std::string &error ) const;
	bool Snap( MapDocument &document, const Selection &selection, double grid, bool textureLock, std::string &error ) const;
	bool Bounds( const MapDocument &document, const Selection &selection, Vec3 &mins, Vec3 &maxs ) const;
	bool CreateBrush( MapDocument &document, EntityId owner, const Vec3 &mins, const Vec3 &maxs, const std::string &material, SelectionItem &created, std::string &error ) const;
	bool CreatePatch( MapDocument &document, EntityId owner, const Vec3 &mins, const Vec3 &maxs, const std::string &material, SelectionItem &created, std::string &error ) const;
	bool Delete( MapDocument &document, const Selection &selection, std::string &error ) const;
	bool Duplicate( MapDocument &document, const Selection &selection, const Vec3 &offset, Selection &created, std::string &error ) const;
	bool SetMaterial( MapDocument &document, const Selection &selection, const std::string &material, std::string &error ) const;
	// Groups are func_group entities, which Quake 4 merges into worldspawn
	// when it compiles or loads a map.
	bool Group( MapDocument &document, const Selection &selection, EntityId &group, std::string &error ) const;
	bool Ungroup( MapDocument &document, EntityId group, std::string &error ) const;
	// Layers are the "editor_layer" key; world primitives join a new group.
	bool SetLayer( MapDocument &document, const Selection &selection, const std::string &layer, Selection &result, std::string &error ) const;
	std::vector<Reference> References( const MapDocument &document, const std::string &name ) const;
	bool Rename( MapDocument &document, EntityId id, const std::string &name, std::size_t &updated, std::string &error ) const;
	bool ExportPrefab( const MapDocument &document, const Selection &selection, std::string &text, std::string &error ) const;
	bool InsertPrefab( MapDocument &document, const std::string &text, const Vec3 &at, Selection &created, std::string &error ) const;
	std::string UniqueName( const MapDocument &document, const std::string &base, const std::set<std::string> &reserved ) const;

private:
	bool ReferenceKey( const Entity &entity, const std::string &key ) const;
	bool MovePrimitives( MapDocument &document, const Selection &primitives, EntityId target, std::string &error ) const;
};

// Parsed primitives keyed by exact source bytes, refreshed incrementally.
// Views use it for wireframes, bounds and picking without re-lexing a map.
class MapGeometryCache {
public:
	struct Item {
		std::string text;
		Primitive primitive;
		Vec3 mins, maxs, origin;
		bool parsed = false, valid = false;
		std::vector<std::vector<Vec3>> polygons;
	};
	struct EntityGeometry {
		std::vector<Item> primitives;
		Vec3 origin;
		bool point = false;
	};
	// Returns the number of primitives re-parsed.
	std::size_t Update( const MapDocument &document );
	const std::map<EntityId, EntityGeometry> &Entities() const { return entities; }
	const EntityGeometry *Find( EntityId id ) const;
	// Nearest hit along a ray. Brushes are exact convex hulls; patches use
	// their control net; point entities a 16-unit box. Filter excludes items.
	bool Pick( const Vec3 &start, const Vec3 &direction, const std::function<bool( const SelectionItem & )> &accept,
		SelectionItem &hit, double &distance ) const;
	std::size_t Primitives() const { return primitiveCount; }
private:
	std::map<EntityId, EntityGeometry> entities;
	std::size_t primitiveCount = 0;
};

}
#endif
