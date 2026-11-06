// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapEditing.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace oq4editor {

namespace {

std::string Lower( std::string text ) {
	for ( char &c : text ) { if ( c >= 'A' && c <= 'Z' ) { c = static_cast<char>( c - 'A' + 'a' ); } }
	return text;
}

bool ParseNumbers( const std::string &text, double *values, int count ) {
	const char *scan = text.c_str();
	for ( int i = 0; i < count; ++i ) {
		char *end = nullptr;
		values[i] = std::strtod( scan, &end );
		if ( end == scan || !std::isfinite( values[i] ) ) { return false; }
		scan = end;
	}
	while ( *scan == ' ' || *scan == '\t' ) { ++scan; }
	return *scan == '\0';
}

std::string FormatNumbers( const double *values, int count ) {
	std::string text;
	for ( int i = 0; i < count; ++i ) { if ( i ) { text += ' '; } text += FormatNumber( values[i] ); }
	return text;
}

double NormalizeDegrees( double degrees ) {
	degrees = std::fmod( degrees, 360.0 );
	if ( degrees < 0 ) { degrees += 360.0; }
	if ( std::fabs( degrees - std::round( degrees ) ) < 1e-9 ) { degrees = std::round( degrees ); }
	return degrees >= 360.0 ? degrees - 360.0 : degrees;
}

bool IsZRotation( const Affine &a ) {
	return std::fabs( a.m[2][2] - 1.0 ) < 1e-9 && std::fabs( a.m[0][2] ) < 1e-9 && std::fabs( a.m[1][2] ) < 1e-9 &&
		std::fabs( a.m[2][0] ) < 1e-9 && std::fabs( a.m[2][1] ) < 1e-9;
}

bool LinearIdentity( const Affine &a ) {
	for ( int r = 0; r < 3; ++r ) { for ( int c = 0; c < 3; ++c ) { if ( a.m[r][c] != ( r == c ? 1.0 : 0.0 ) ) { return false; } } }
	return true;
}

struct PlannedProperty { EntityId id; std::string key, value; bool remove = false, geometryOrigin = false; };
struct PlannedPrimitive { EntityId id; std::size_t index; std::string text; };

class TransactionScope {
public:
	TransactionScope( MapDocument &doc, const std::string &label, std::string &error ) : document( doc ) {
		opened = document.BeginTransaction( label, error );
	}
	~TransactionScope() { if ( opened && !done ) { document.CancelTransaction(); } }
	bool Ok() const { return opened; }
	bool Commit( std::string &error ) { done = true; return document.CommitTransaction( error ); }
private:
	MapDocument &document;
	bool opened = false, done = false;
};

bool Execute( MapDocument &document, const std::vector<PlannedProperty> &properties, const std::vector<PlannedPrimitive> &primitives, std::string &error ) {
	for ( const auto &plan : properties ) {
		const bool ok = plan.remove ? document.RemoveProperty( plan.id, plan.key, error ) :
			plan.geometryOrigin ? document.SetGeometryOrigin( plan.id, plan.value, error ) :
			document.SetProperty( plan.id, plan.key, plan.value, error );
		if ( !ok ) { return false; }
	}
	std::map<EntityId, std::vector<std::pair<std::size_t, std::string>>> grouped;
	for ( const auto &plan : primitives ) { grouped[plan.id].emplace_back( plan.index, plan.text ); }
	for ( const auto &[id, edits] : grouped ) {
		if ( !document.SetPrimitives( id, edits, error ) ) { return false; }
	}
	return true;
}

bool RayBox( const Vec3 &start, const Vec3 &dir, const Vec3 &mins, const Vec3 &maxs, double &enter ) {
	double t0 = 0, t1 = ( std::numeric_limits<double>::max )();
	for ( int i = 0; i < 3; ++i ) {
		if ( std::fabs( dir[i] ) < 1e-12 ) {
			if ( start[i] < mins[i] || start[i] > maxs[i] ) { return false; }
			continue;
		}
		double a = ( mins[i] - start[i] ) / dir[i], b = ( maxs[i] - start[i] ) / dir[i];
		if ( a > b ) { std::swap( a, b ); }
		t0 = ( std::max )( t0, a ); t1 = ( std::min )( t1, b );
		if ( t0 > t1 ) { return false; }
	}
	enter = t0;
	return true;
}

bool RayTriangle( const Vec3 &start, const Vec3 &dir, const Vec3 &a, const Vec3 &b, const Vec3 &c, double &t ) {
	const Vec3 e1 = b - a, e2 = c - a, p = dir.Cross( e2 );
	const double det = e1.Dot( p );
	if ( std::fabs( det ) < 1e-12 ) { return false; }
	const double inv = 1.0 / det;
	const Vec3 s = start - a;
	const double u = s.Dot( p ) * inv;
	if ( u < 0 || u > 1 ) { return false; }
	const Vec3 q = s.Cross( e1 );
	const double v = dir.Dot( q ) * inv;
	if ( v < 0 || u + v > 1 ) { return false; }
	t = e2.Dot( q ) * inv;
	return t >= 0;
}

}

bool IsReferenceKey( const std::string &keyInput ) {
	const std::string key = Lower( keyInput );
	return key.compare( 0, 6, "target" ) == 0 || key == "bind" || key == "killtarget" || key == "cameratarget" ||
		key == "enemy" || key == "focus";
}

bool ParseVector( const std::string &text, Vec3 &value ) {
	double v[3];
	if ( !ParseNumbers( text, v, 3 ) ) { return false; }
	value = Vec3( v[0], v[1], v[2] );
	return true;
}

std::string FormatVector( const Vec3 &value ) {
	const double v[3] = { value.x, value.y, value.z };
	return FormatNumbers( v, 3 );
}

bool IsWorld( const MapDocument &document, const Entity &entity ) {
	return !document.Entities().empty() && document.Entities().front().id == entity.id;
}

Vec3 EntityOrigin( const MapDocument &document, const Entity &entity, bool *present ) {
	Vec3 origin;
	const std::string text = entity.Value( "origin" );
	// idMapEntity::Parse zeroes the origin of worldspawn primitives.
	const bool ok = !IsWorld( document, entity ) && !text.empty() && ParseVector( text, origin );
	if ( present ) { *present = ok; }
	return ok ? origin : Vec3();
}

std::string EntityLayer( const Entity &entity ) { return entity.Value( "editor_layer" ); }

bool MapEditor::ReferenceKey( const Entity &entity, const std::string &key ) const {
	return IsReferenceKey( key ) || ( IsEntityKey && IsEntityKey( entity.Value( "classname" ), key ) );
}

bool MapEditor::Transform( MapDocument &document, const Selection &selection, const Affine &world, bool textureLock, std::string &error ) const {
	error.clear();
	if ( selection.empty() ) { error = "nothing is selected"; return false; }
	if ( world.IsIdentity() ) { return true; }
	if ( !( world.Determinant() > 1e-9 ) ) { error = "transform must preserve orientation and volume"; return false; }
	std::set<EntityId> whole;
	std::map<EntityId, std::set<std::size_t>> parts;
	for ( const auto &item : selection ) {
		const Entity *entity = document.Find( item.entity );
		if ( !entity ) { error = "selection refers to a deleted entity"; return false; }
		if ( item.primitive == WholeEntity ) { whole.insert( item.entity ); }
		else if ( item.primitive >= entity->primitiveRanges.size() ) { error = "selection refers to a deleted primitive"; return false; }
		else { parts[item.entity].insert( item.primitive ); }
	}
	std::vector<PlannedProperty> properties;
	std::vector<PlannedPrimitive> primitives;
	const auto planPrimitive = [&]( const Entity &entity, std::size_t index, const Vec3 &oldOrigin, const Vec3 &newOrigin ) {
		Primitive primitive;
		const std::string text = document.PrimitiveText( entity.id, index );
		if ( !primitive.Parse( text, error ) ) { return false; }
		if ( !primitive.Transform( world, oldOrigin, newOrigin, textureLock, error ) ) { return false; }
		const std::string moved = primitive.Serialize();
		if ( moved != text ) { primitives.push_back( { entity.id, index, moved } ); }
		return true;
	};
	for ( const EntityId id : whole ) {
		const Entity &entity = *document.Find( id );
		bool hasOrigin = false;
		const Vec3 origin = EntityOrigin( document, entity, &hasOrigin );
		if ( IsWorld( document, entity ) ) {
			for ( std::size_t i = 0; i < entity.primitiveRanges.size(); ++i ) { if ( !planPrimitive( entity, i, Vec3(), Vec3() ) ) { return false; } }
			continue;
		}
		if ( entity.primitiveCount != 0 ) {
			const Vec3 moved = hasOrigin ? world.Apply( origin ) : Vec3();
			if ( hasOrigin && !moved.Finite() ) { error = "transform moves an origin outside the map limits"; return false; }
			if ( hasOrigin && FormatVector( moved ) != FormatVector( origin ) ) { properties.push_back( { id, "origin", FormatVector( moved ), false, true } ); }
			for ( std::size_t i = 0; i < entity.primitiveRanges.size(); ++i ) {
				if ( !planPrimitive( entity, i, origin, hasOrigin ? moved : Vec3() ) ) { return false; }
			}
			continue;
		}
		// Point entity: origin and, for rotations, its orientation keys.
		const Vec3 moved = world.Apply( origin );
		if ( !moved.Finite() || std::fabs( moved.x ) > 1e7 || std::fabs( moved.y ) > 1e7 || std::fabs( moved.z ) > 1e7 ) {
			error = "transform moves an origin outside the map limits"; return false;
		}
		if ( FormatVector( moved ) != FormatVector( origin ) || !hasOrigin ) { properties.push_back( { id, "origin", FormatVector( moved ) } ); }
		if ( LinearIdentity( world ) ) { continue; }
		Affine linear = world; linear.t = Vec3();
		if ( linear.IsRigid() ) {
			const std::string rotation = entity.Value( "rotation" ), angles = entity.Value( "angles" ), angle = entity.Value( "angle" );
			double rows[9];
			if ( !rotation.empty() ) {
				if ( !ParseNumbers( rotation, rows, 9 ) ) { error = "entity has a malformed 'rotation' key"; return false; }
			} else if ( !angles.empty() ) {
				double pyr[3];
				if ( !ParseNumbers( angles, pyr, 3 ) ) { error = "entity has a malformed 'angles' key"; return false; }
				if ( !IsZRotation( linear ) ) { error = "entities oriented by 'angles' rotate about Z only"; return false; }
				pyr[1] = NormalizeDegrees( pyr[1] + std::atan2( linear.m[1][0], linear.m[0][0] ) * 180.0 / 3.14159265358979323846 );
				properties.push_back( { id, "angles", FormatNumbers( pyr, 3 ) } );
				rows[0] = 1; // orientation handled; skip the matrix path
			}
			if ( rotation.empty() && angles.empty() ) {
				double yaw = 0;
				if ( !angle.empty() && !ParseNumbers( angle, &yaw, 1 ) ) { error = "entity has a malformed 'angle' key"; return false; }
				if ( IsZRotation( linear ) ) {
					const double turned = NormalizeDegrees( yaw + std::atan2( linear.m[1][0], linear.m[0][0] ) * 180.0 / 3.14159265358979323846 );
					properties.push_back( { id, "angle", FormatNumber( turned ) } );
				} else {
					const double r = yaw * 3.14159265358979323846 / 180.0;
					const double initial[9] = { std::cos( r ), std::sin( r ), 0, -std::sin( r ), std::cos( r ), 0, 0, 0, 1 };
					std::copy( initial, initial + 9, rows );
				}
			}
			if ( !rotation.empty() || ( angles.empty() && !IsZRotation( linear ) ) ) {
				double turned[9];
				for ( int axis = 0; axis < 3; ++axis ) {
					const Vec3 v = linear.Linear( Vec3( rows[axis * 3], rows[axis * 3 + 1], rows[axis * 3 + 2] ) );
					for ( int i = 0; i < 3; ++i ) { turned[axis * 3 + i] = std::fabs( v[i] ) < 1e-10 ? 0.0 : v[i]; }
				}
				properties.push_back( { id, "rotation", FormatNumbers( turned, 9 ) } );
				if ( !angle.empty() ) { properties.push_back( { id, "angle", "", true } ); }
			}
			for ( const char *key : { "light_target", "light_up", "light_right", "light_start", "light_end", "light_center" } ) {
				Vec3 v;
				if ( ParseVector( entity.Value( key ), v ) ) { properties.push_back( { id, key, FormatVector( linear.Linear( v ) ) } ); }
			}
		} else {
			// Scale: axis-aligned lights grow with the selection.
			Vec3 radius;
			const bool diagonal = std::fabs( linear.m[0][1] ) + std::fabs( linear.m[0][2] ) + std::fabs( linear.m[1][0] ) +
				std::fabs( linear.m[1][2] ) + std::fabs( linear.m[2][0] ) + std::fabs( linear.m[2][1] ) < 1e-12;
			if ( diagonal && entity.Value( "rotation" ).empty() && ParseVector( entity.Value( "light_radius" ), radius ) ) {
				properties.push_back( { id, "light_radius", FormatVector( Vec3( radius.x * linear.m[0][0], radius.y * linear.m[1][1], radius.z * linear.m[2][2] ) ) } );
			}
		}
	}
	for ( const auto &[id, indexes] : parts ) {
		if ( whole.count( id ) ) { continue; }
		const Entity &entity = *document.Find( id );
		const Vec3 origin = EntityOrigin( document, entity );
		for ( const std::size_t index : indexes ) { if ( !planPrimitive( entity, index, origin, origin ) ) { return false; } }
	}
	if ( properties.empty() && primitives.empty() ) { return true; }
	TransactionScope transaction( document, "transform", error );
	if ( !transaction.Ok() || !Execute( document, properties, primitives, error ) ) { return false; }
	return transaction.Commit( error );
}

bool MapEditor::Bounds( const MapDocument &document, const Selection &selection, Vec3 &mins, Vec3 &maxs ) const {
	bool any = false;
	const auto add = [&]( const Vec3 &lo, const Vec3 &hi ) {
		if ( !any ) { mins = lo; maxs = hi; any = true; return; }
		for ( int i = 0; i < 3; ++i ) { mins[i] = ( std::min )( mins[i], lo[i] ); maxs[i] = ( std::max )( maxs[i], hi[i] ); }
	};
	for ( const auto &item : selection ) {
		const Entity *entity = document.Find( item.entity );
		if ( !entity ) { continue; }
		const Vec3 origin = EntityOrigin( document, *entity );
		if ( entity->primitiveCount == 0 ) {
			if ( !IsWorld( document, *entity ) ) { add( origin - Vec3( 8, 8, 8 ), origin + Vec3( 8, 8, 8 ) ); }
			continue;
		}
		for ( std::size_t i = 0; i < entity->primitiveRanges.size(); ++i ) {
			if ( item.primitive != WholeEntity && item.primitive != i ) { continue; }
			Primitive primitive;
			std::string error;
			Vec3 lo, hi;
			if ( primitive.Parse( document.PrimitiveText( entity->id, i ), error ) && primitive.Bounds( origin, lo, hi ) ) { add( lo, hi ); }
		}
	}
	return any;
}

bool MapEditor::Snap( MapDocument &document, const Selection &selection, double grid, bool textureLock, std::string &error ) const {
	error.clear();
	if ( !( grid > 0 ) || grid > 4096 ) { error = "grid must be between 0 and 4096 units"; return false; }
	Vec3 reference, maxs;
	bool primitives = false;
	for ( const auto &item : selection ) {
		const Entity *entity = document.Find( item.entity );
		if ( entity && ( item.primitive != WholeEntity || entity->primitiveCount != 0 ) ) { primitives = true; }
	}
	if ( primitives ) {
		if ( !Bounds( document, selection, reference, maxs ) ) { error = "selection has no geometry"; return false; }
	} else {
		const Entity *entity = selection.empty() ? nullptr : document.Find( selection.front().entity );
		if ( !entity ) { error = "nothing is selected"; return false; }
		reference = EntityOrigin( document, *entity );
	}
	const Vec3 delta( SnapValue( reference.x, grid ) - reference.x, SnapValue( reference.y, grid ) - reference.y, SnapValue( reference.z, grid ) - reference.z );
	if ( std::fabs( delta.x ) < 1e-9 && std::fabs( delta.y ) < 1e-9 && std::fabs( delta.z ) < 1e-9 ) { return true; }
	return Transform( document, selection, Affine::Translation( delta ), textureLock, error );
}

bool MapEditor::CreateBrush( MapDocument &document, EntityId owner, const Vec3 &mins, const Vec3 &maxs, const std::string &material, SelectionItem &created, std::string &error ) const {
	error.clear();
	const Entity *entity = document.Find( owner );
	if ( !entity ) { error = "unknown entity handle"; return false; }
	if ( document.Version() < 2 ) { error = "brushDef3 needs a version 2 or 3 map"; return false; }
	for ( int i = 0; i < 3; ++i ) {
		if ( !std::isfinite( mins[i] ) || !std::isfinite( maxs[i] ) || maxs[i] - mins[i] < 1 || std::fabs( mins[i] ) > 1e6 || std::fabs( maxs[i] ) > 1e6 ) {
			error = "brush needs at least one unit of size on every axis within the map limits"; return false;
		}
	}
	Primitive check;
	const std::string text = BoxBrushSource( mins, maxs, material, EntityOrigin( document, *entity ), document.Newline().c_str() );
	if ( !check.Parse( text, error ) || !check.SetMaterial( material, error ) ) { return false; }
	std::size_t index = 0;
	if ( !document.AddPrimitive( owner, text, index, error ) ) { return false; }
	created = { owner, index };
	return true;
}

bool MapEditor::CreatePatch( MapDocument &document, EntityId owner, const Vec3 &mins, const Vec3 &maxs, const std::string &material, SelectionItem &created, std::string &error ) const {
	error.clear();
	const Entity *entity = document.Find( owner );
	if ( !entity ) { error = "unknown entity handle"; return false; }
	if ( !( maxs.x - mins.x >= 1 ) || !( maxs.y - mins.y >= 1 ) || !mins.Finite() || !maxs.Finite() ) { error = "patch needs at least one unit of width and depth"; return false; }
	Primitive check;
	const std::string text = FlatPatchSource( mins, maxs, material, document.Newline().c_str() );
	if ( !check.Parse( text, error ) || !check.SetMaterial( material, error ) ) { return false; }
	std::size_t index = 0;
	if ( !document.AddPrimitive( owner, text, index, error ) ) { return false; }
	created = { owner, index };
	return true;
}

bool MapEditor::Delete( MapDocument &document, const Selection &selection, std::string &error ) const {
	error.clear();
	std::set<EntityId> whole;
	std::map<EntityId, std::set<std::size_t>> parts;
	for ( const auto &item : selection ) {
		const Entity *entity = document.Find( item.entity );
		if ( !entity ) { continue; }
		if ( item.primitive == WholeEntity ) {
			if ( IsWorld( document, *entity ) ) { error = "worldspawn cannot be deleted; select its primitives"; return false; }
			whole.insert( item.entity );
		} else if ( item.primitive < entity->primitiveRanges.size() ) { parts[item.entity].insert( item.primitive ); }
	}
	if ( whole.empty() && parts.empty() ) { error = "nothing is selected"; return false; }
	TransactionScope transaction( document, "delete", error );
	if ( !transaction.Ok() ) { return false; }
	for ( const auto &[id, indexes] : parts ) {
		if ( whole.count( id ) ) { continue; }
		for ( auto it = indexes.rbegin(); it != indexes.rend(); ++it ) { if ( !document.RemovePrimitive( id, *it, error ) ) { return false; } }
	}
	for ( const EntityId id : whole ) { if ( !document.DeleteEntity( id, error ) ) { return false; } }
	return transaction.Commit( error );
}

std::string MapEditor::UniqueName( const MapDocument &document, const std::string &base, const std::set<std::string> &reserved ) const {
	std::string stem = base.empty() ? "entity" : base;
	// Reuse the trailing number: copies of "light_3" become "light_4", ...
	std::size_t digits = stem.size();
	while ( digits > 0 && stem[digits - 1] >= '0' && stem[digits - 1] <= '9' ) { --digits; }
	unsigned long long number = 1;
	if ( digits < stem.size() && digits > 0 && stem[digits - 1] == '_' ) {
		number = std::strtoull( stem.c_str() + digits, nullptr, 10 ) + 1;
		stem.resize( digits - 1 );
	}
	for ( ;; ++number ) {
		const std::string candidate = stem + "_" + std::to_string( number );
		if ( document.NameAvailable( candidate, 0 ) && !reserved.count( Lower( candidate ) ) ) { return candidate; }
	}
}

bool MapEditor::Duplicate( MapDocument &document, const Selection &selection, const Vec3 &offset, Selection &created, std::string &error ) const {
	error.clear();
	created.clear();
	std::vector<EntityId> wholes;
	std::map<EntityId, std::set<std::size_t>> parts;
	for ( const auto &item : selection ) {
		const Entity *entity = document.Find( item.entity );
		if ( !entity ) { continue; }
		if ( item.primitive == WholeEntity && !IsWorld( document, *entity ) ) {
			if ( std::find( wholes.begin(), wholes.end(), item.entity ) == wholes.end() ) { wholes.push_back( item.entity ); }
		} else if ( item.primitive != WholeEntity && item.primitive < entity->primitiveRanges.size() ) { parts[item.entity].insert( item.primitive ); }
	}
	if ( wholes.empty() && parts.empty() ) { error = "nothing to duplicate"; return false; }
	TransactionScope transaction( document, "duplicate", error );
	if ( !transaction.Ok() ) { return false; }
	// Copies keep their links to each other: references among the
	// duplicated entities point at the copies, others stay unchanged.
	std::map<std::string, std::string> names;
	std::set<std::string> reserved;
	for ( const EntityId id : wholes ) {
		const std::string name = document.Find( id )->Value( "name" );
		if ( name.empty() ) { continue; }
		const std::string copy = UniqueName( document, name, reserved );
		reserved.insert( Lower( copy ) );
		names[Lower( name )] = copy;
	}
	for ( const EntityId id : wholes ) {
		const Entity source = *document.Find( id );
		std::string text = source.source;
		// Insert under a temporary unique name, then rename via the document.
		const std::string name = source.Value( "name" );
		EntityId copy = 0;
		if ( !name.empty() ) {
			const auto &property = *std::find_if( source.properties.rbegin(), source.properties.rend(),
				[]( const Property &p ) { return EqualNoCase( p.key, "name" ); } );
			text.replace( property.valueBegin, property.end - property.valueBegin, "\"" + names[Lower( name )] + "\"" );
		}
		if ( !document.InsertEntity( text, copy, error ) ) { return false; }
		// Earlier duplicate-name definitions would shadow nothing: the
		// effective value is the last one, which was replaced above.
		const Entity copied = *document.Find( copy );
		for ( const auto &property : copied.properties ) {
			if ( !ReferenceKey( copied, property.key ) ) { continue; }
			const auto renamed = names.find( Lower( property.value ) );
			if ( renamed != names.end() && !EqualNoCase( property.key, "name" ) &&
				!document.SetProperty( copy, property.key, renamed->second, error ) ) { return false; }
		}
		created.push_back( { copy, WholeEntity } );
	}
	for ( const auto &[id, indexes] : parts ) {
		for ( const std::size_t index : indexes ) {
			std::size_t added = 0;
			if ( !document.AddPrimitive( id, document.PrimitiveText( id, index ), added, error ) ) { return false; }
			created.push_back( { id, added } );
		}
	}
	if ( !( offset.x == 0 && offset.y == 0 && offset.z == 0 ) && !Transform( document, created, Affine::Translation( offset ), true, error ) ) { return false; }
	return transaction.Commit( error );
}

bool MapEditor::SetMaterial( MapDocument &document, const Selection &selection, const std::string &material, std::string &error ) const {
	error.clear();
	std::vector<PlannedPrimitive> plans;
	for ( const auto &item : selection ) {
		const Entity *entity = document.Find( item.entity );
		if ( !entity ) { continue; }
		for ( std::size_t i = 0; i < entity->primitiveRanges.size(); ++i ) {
			if ( item.primitive != WholeEntity && item.primitive != i ) { continue; }
			Primitive primitive;
			const std::string text = document.PrimitiveText( entity->id, i );
			if ( !primitive.Parse( text, error ) ) { return false; }
			if ( primitive.GetKind() == Primitive::Kind::Unsupported ) { continue; }
			if ( !primitive.SetMaterial( material, error ) ) { return false; }
			if ( primitive.Serialize() != text ) { plans.push_back( { entity->id, i, primitive.Serialize() } ); }
		}
	}
	if ( plans.empty() ) { return true; }
	TransactionScope transaction( document, "material", error );
	if ( !transaction.Ok() || !Execute( document, {}, plans, error ) ) { return false; }
	return transaction.Commit( error );
}

bool MapEditor::MovePrimitives( MapDocument &document, const Selection &primitives, EntityId target, std::string &error ) const {
	const Entity *destination = document.Find( target );
	if ( !destination ) { error = "unknown group"; return false; }
	const Vec3 targetOrigin = EntityOrigin( document, *destination );
	std::map<EntityId, std::set<std::size_t>> removals;
	for ( const auto &item : primitives ) {
		const Entity &source = *document.Find( item.entity );
		const Vec3 sourceOrigin = EntityOrigin( document, source );
		std::string text = document.PrimitiveText( item.entity, item.primitive );
		if ( FormatVector( sourceOrigin ) != FormatVector( targetOrigin ) ) {
			Primitive primitive;
			if ( !primitive.Parse( text, error ) || !primitive.Transform( Affine(), sourceOrigin, targetOrigin, true, error ) ) { return false; }
			// An identity world transform still re-expresses local planes.
			text = primitive.Serialize();
		}
		std::size_t index = 0;
		if ( !document.AddPrimitive( target, text, index, error ) ) { return false; }
		removals[item.entity].insert( item.primitive );
	}
	for ( const auto &[id, indexes] : removals ) {
		for ( auto it = indexes.rbegin(); it != indexes.rend(); ++it ) { if ( !document.RemovePrimitive( id, *it, error ) ) { return false; } }
	}
	return true;
}

bool MapEditor::Group( MapDocument &document, const Selection &selection, EntityId &group, std::string &error ) const {
	error.clear();
	Selection primitives;
	for ( const auto &item : selection ) {
		const Entity *entity = document.Find( item.entity );
		if ( !entity || item.primitive == WholeEntity ) { continue; }
		if ( !IsWorld( document, *entity ) && !EqualNoCase( entity->Value( "classname" ), "func_group" ) ) {
			error = "only world and func_group primitives can be grouped"; return false;
		}
		if ( item.primitive < entity->primitiveRanges.size() &&
			std::find( primitives.begin(), primitives.end(), item ) == primitives.end() ) { primitives.push_back( item ); }
	}
	if ( primitives.empty() ) { error = "select world primitives to group"; return false; }
	TransactionScope transaction( document, "group", error );
	if ( !transaction.Ok() ) { return false; }
	const std::string newline = document.Newline();
	const std::string name = UniqueName( document, "func_group", {} );
	if ( !document.InsertEntity( "{" + newline + "\"classname\" \"func_group\"" + newline + "\"name\" \"" + name + "\"" + newline + "}", group, error ) ||
		!MovePrimitives( document, primitives, group, error ) ) { return false; }
	return transaction.Commit( error );
}

bool MapEditor::Ungroup( MapDocument &document, EntityId groupId, std::string &error ) const {
	error.clear();
	const Entity *group = document.Find( groupId );
	if ( !group || !EqualNoCase( group->Value( "classname" ), "func_group" ) ) { error = "select a func_group to ungroup"; return false; }
	Selection primitives;
	for ( std::size_t i = 0; i < group->primitiveRanges.size(); ++i ) { primitives.push_back( { groupId, i } ); }
	TransactionScope transaction( document, "ungroup", error );
	if ( !transaction.Ok() ) { return false; }
	if ( !primitives.empty() && !MovePrimitives( document, primitives, document.Entities().front().id, error ) ) { return false; }
	if ( !document.DeleteEntity( groupId, error ) ) { return false; }
	return transaction.Commit( error );
}

bool MapEditor::SetLayer( MapDocument &document, const Selection &selection, const std::string &layer, Selection &result, std::string &error ) const {
	error.clear();
	result.clear();
	if ( layer.size() > 64 || layer.find_first_of( "\"\r\n" ) != std::string::npos ) { error = "layer names are at most 64 bytes without quotes or newlines"; return false; }
	std::set<EntityId> targets;
	Selection world;
	for ( const auto &item : selection ) {
		const Entity *entity = document.Find( item.entity );
		if ( !entity ) { continue; }
		if ( IsWorld( document, *entity ) ) {
			if ( item.primitive != WholeEntity ) { world.push_back( item ); }
		} else {
			targets.insert( item.entity );
		}
	}
	if ( targets.empty() && world.empty() ) { error = "nothing is selected"; return false; }
	TransactionScope transaction( document, "layer", error );
	if ( !transaction.Ok() ) { return false; }
	if ( !world.empty() ) {
		EntityId group = 0;
		if ( !Group( document, world, group, error ) ) { return false; }
		targets.insert( group );
	}
	for ( const EntityId id : targets ) {
		const bool ok = layer.empty() ? document.RemoveProperty( id, "editor_layer", error ) : document.SetProperty( id, "editor_layer", layer, error );
		if ( !ok ) { return false; }
		result.push_back( { id, WholeEntity } );
	}
	return transaction.Commit( error );
}

std::vector<Reference> MapEditor::References( const MapDocument &document, const std::string &name ) const {
	std::vector<Reference> result;
	if ( name.empty() ) { return result; }
	for ( const auto &entity : document.Entities() ) {
		for ( const auto &property : entity.properties ) {
			if ( ReferenceKey( entity, property.key ) && EqualNoCase( property.value, name ) && EqualNoCase( entity.Value( property.key ), name ) ) {
				if ( std::none_of( result.begin(), result.end(), [&]( const Reference &r ) { return r.entity == entity.id && EqualNoCase( r.key, property.key ); } ) ) {
					result.push_back( { entity.id, property.key } );
				}
			}
		}
	}
	return result;
}

bool MapEditor::Rename( MapDocument &document, EntityId id, const std::string &name, std::size_t &updated, std::string &error ) const {
	error.clear();
	updated = 0;
	const Entity *entity = document.Find( id );
	if ( !entity ) { error = "unknown entity handle"; return false; }
	if ( IsWorld( document, *entity ) ) { error = "worldspawn has no name"; return false; }
	if ( name.empty() ) { error = "a renamed entity needs a name"; return false; }
	const std::string old = entity->Value( "name" );
	if ( old == name ) { return true; }
	const auto references = EqualNoCase( old, name ) || old.empty() ? std::vector<Reference>() : References( document, old );
	TransactionScope transaction( document, "rename", error );
	if ( !transaction.Ok() || !document.SetProperty( id, "name", name, error ) ) { return false; }
	for ( const auto &reference : references ) {
		if ( !document.SetProperty( reference.entity, reference.key, name, error ) ) { return false; }
		++updated;
	}
	return transaction.Commit( error );
}

bool MapEditor::ExportPrefab( const MapDocument &document, const Selection &selection, std::string &text, std::string &error ) const {
	error.clear();
	const std::string newline = document.Newline();
	std::string world, entities;
	std::set<EntityId> whole;
	for ( const auto &item : selection ) { if ( item.primitive == WholeEntity ) { whole.insert( item.entity ); } }
	std::set<SelectionItem> seen;
	for ( const auto &item : selection ) {
		const Entity *entity = document.Find( item.entity );
		if ( !entity || !seen.insert( item ).second ) { continue; }
		if ( item.primitive == WholeEntity ) {
			if ( IsWorld( document, *entity ) ) { error = "select world primitives, not worldspawn itself"; return false; }
			entities += entity->source + newline;
			continue;
		}
		if ( whole.count( item.entity ) || item.primitive >= entity->primitiveRanges.size() ) { continue; }
		if ( !IsWorld( document, *entity ) && !EqualNoCase( entity->Value( "classname" ), "func_group" ) ) {
			error = "export whole entities, or world and func_group primitives"; return false;
		}
		std::string primitive = document.PrimitiveText( item.entity, item.primitive );
		const Vec3 origin = EntityOrigin( document, *entity );
		if ( origin.x != 0 || origin.y != 0 || origin.z != 0 ) {
			Primitive parsed;
			if ( !parsed.Parse( primitive, error ) || !parsed.Transform( Affine(), origin, Vec3(), true, error ) ) { return false; }
			primitive = parsed.Serialize();
		}
		world += primitive + newline;
	}
	if ( world.empty() && entities.empty() ) { error = "nothing to export"; return false; }
	MapDocument prefab;
	if ( !prefab.OpenText( "Version 3" + newline + "{" + newline + "\"classname\" \"worldspawn\"" + newline + world + "}" + newline + entities, error ) ) {
		error = "prefab did not validate: " + error; return false;
	}
	// Recentre on the bottom centre so insertion places it on a floor point.
	Selection all;
	for ( const auto &entity : prefab.Entities() ) {
		if ( IsWorld( prefab, entity ) ) { for ( std::size_t i = 0; i < entity.primitiveRanges.size(); ++i ) { all.push_back( { entity.id, i } ); } }
		else { all.push_back( { entity.id, WholeEntity } ); }
	}
	Vec3 mins, maxs;
	if ( Bounds( prefab, all, mins, maxs ) ) {
		const Vec3 shift( -std::round( ( mins.x + maxs.x ) * 0.5 ), -std::round( ( mins.y + maxs.y ) * 0.5 ), -std::round( mins.z ) );
		if ( !Transform( prefab, all, Affine::Translation( shift ), true, error ) ) { return false; }
	}
	text = prefab.Serialize();
	return true;
}

bool MapEditor::InsertPrefab( MapDocument &document, const std::string &text, const Vec3 &at, Selection &created, std::string &error ) const {
	error.clear();
	created.clear();
	if ( !document.IsOpen() ) { error = "no open document"; return false; }
	MapDocument prefab;
	if ( !prefab.OpenText( text, error ) ) { error = "prefab is not a valid map: " + error; return false; }
	// Names must be unique in the target. Rename inside the prefab first so
	// its own references follow, then copy the renamed blocks across.
	std::set<std::string> reserved;
	for ( const auto &entity : prefab.Entities() ) { if ( !entity.Value( "name" ).empty() ) { reserved.insert( Lower( entity.Value( "name" ) ) ); } }
	std::vector<EntityId> ids;
	for ( const auto &entity : prefab.Entities() ) { ids.push_back( entity.id ); }
	for ( const EntityId id : ids ) {
		const std::string name = prefab.Find( id )->Value( "name" );
		if ( name.empty() || document.NameAvailable( name, 0 ) ) { continue; }
		std::string fresh;
		for ( ;; ) {
			fresh = UniqueName( document, name, reserved );
			if ( prefab.NameAvailable( fresh, 0 ) ) { break; }
			reserved.insert( Lower( fresh ) );
		}
		reserved.insert( Lower( fresh ) );
		std::size_t updated = 0;
		if ( !Rename( prefab, id, fresh, updated, error ) ) { return false; }
	}
	const std::string newline = document.Newline();
	TransactionScope transaction( document, "insert prefab", error );
	if ( !transaction.Ok() ) { return false; }
	for ( const auto &entity : prefab.Entities() ) {
		EntityId id = 0;
		if ( IsWorld( prefab, entity ) ) {
			if ( entity.primitiveRanges.empty() ) { continue; }
			std::string group = "{" + newline + "\"classname\" \"func_group\"" + newline + "\"name\" \"" + UniqueName( document, "prefab", reserved ) + "\"" + newline;
			for ( std::size_t i = 0; i < entity.primitiveRanges.size(); ++i ) { group += prefab.PrimitiveText( entity.id, i ) + newline; }
			group += "}";
			if ( !document.InsertEntity( group, id, error ) ) { return false; }
		} else if ( !document.InsertEntity( entity.source, id, error ) ) { return false; }
		created.push_back( { id, WholeEntity } );
	}
	if ( created.empty() ) { error = "prefab is empty"; return false; }
	if ( !( at.x == 0 && at.y == 0 && at.z == 0 ) && !Transform( document, created, Affine::Translation( at ), true, error ) ) { return false; }
	return transaction.Commit( error );
}

const MapGeometryCache::EntityGeometry *MapGeometryCache::Find( EntityId id ) const {
	const auto it = entities.find( id );
	return it == entities.end() ? nullptr : &it->second;
}

std::size_t MapGeometryCache::Update( const MapDocument &document ) {
	std::size_t parsed = 0;
	std::set<EntityId> live;
	primitiveCount = 0;
	for ( const auto &entity : document.Entities() ) {
		live.insert( entity.id );
		EntityGeometry &geometry = entities[entity.id];
		geometry.origin = EntityOrigin( document, entity );
		geometry.point = entity.primitiveCount == 0 && !IsWorld( document, entity );
		geometry.primitives.resize( entity.primitiveRanges.size() );
		for ( std::size_t i = 0; i < entity.primitiveRanges.size(); ++i ) {
			const auto &range = entity.primitiveRanges[i];
			Item &item = geometry.primitives[i];
			const bool same = !item.text.empty() && item.text.size() == range.second - range.first &&
				entity.source.compare( range.first, range.second - range.first, item.text ) == 0;
			const bool placed = item.origin.x == geometry.origin.x && item.origin.y == geometry.origin.y && item.origin.z == geometry.origin.z;
			if ( same && placed ) { continue; }
			if ( !same ) {
				item = Item();
				item.text = entity.source.substr( range.first, range.second - range.first );
				std::string error;
				item.parsed = item.primitive.Parse( item.text, error ) && item.primitive.GetKind() != Primitive::Kind::Unsupported;
				++parsed;
			}
			// Brush planes are entity-local, so an origin change moves them.
			item.origin = geometry.origin;
			item.valid = item.parsed;
			if ( item.valid ) {
				item.polygons = item.primitive.Polygons( geometry.origin );
				item.valid = item.primitive.Bounds( geometry.origin, item.mins, item.maxs );
			}
		}
		primitiveCount += geometry.primitives.size();
	}
	for ( auto it = entities.begin(); it != entities.end(); ) { it = live.count( it->first ) ? std::next( it ) : entities.erase( it ); }
	return parsed;
}

bool MapGeometryCache::Pick( const Vec3 &start, const Vec3 &directionInput, const std::function<bool( const SelectionItem & )> &accept,
	SelectionItem &hit, double &distance ) const {
	const double length = directionInput.Length();
	if ( !( length > 0 ) ) { return false; }
	const Vec3 direction = directionInput * ( 1.0 / length );
	bool found = false;
	distance = ( std::numeric_limits<double>::max )();
	for ( const auto &[id, geometry] : entities ) {
		if ( geometry.point ) {
			const SelectionItem item{ id, WholeEntity };
			double t;
			if ( ( !accept || accept( item ) ) && RayBox( start, direction, geometry.origin - Vec3( 8, 8, 8 ), geometry.origin + Vec3( 8, 8, 8 ), t ) && t < distance ) {
				distance = t; hit = item; found = true;
			}
			continue;
		}
		for ( std::size_t i = 0; i < geometry.primitives.size(); ++i ) {
			const Item &item = geometry.primitives[i];
			double t;
			if ( !item.valid || !RayBox( start, direction, item.mins - Vec3( 0.5, 0.5, 0.5 ), item.maxs + Vec3( 0.5, 0.5, 0.5 ), t ) || t >= distance ) { continue; }
			const SelectionItem candidate{ id, i };
			if ( accept && !accept( candidate ) ) { continue; }
			if ( item.primitive.GetKind() == Primitive::Kind::Brush ) {
				// Ray against the convex intersection of the outward planes.
				double enter = 0, leave = ( std::numeric_limits<double>::max )();
				bool inside = true;
				for ( const auto &side : item.primitive.Sides() ) {
					const Vec3 n = side.Normal();
					const double d = side.D() - n.Dot( geometry.origin );
					const double denom = n.Dot( direction ), dist = n.Dot( start ) + d;
					if ( std::fabs( denom ) < 1e-12 ) { if ( dist > 0 ) { inside = false; break; } continue; }
					const double s = -dist / denom;
					if ( denom < 0 ) { enter = ( std::max )( enter, s ); } else { leave = ( std::min )( leave, s ); }
					if ( enter > leave ) { inside = false; break; }
				}
				if ( inside && enter < distance ) { distance = enter; hit = candidate; found = true; }
			} else {
				for ( const auto &polygon : item.polygons ) {
					for ( std::size_t k = 1; k + 1 < polygon.size(); ++k ) {
						double s;
						if ( RayTriangle( start, direction, polygon[0], polygon[k], polygon[k + 1], s ) && s < distance ) { distance = s; hit = candidate; found = true; }
					}
				}
			}
		}
	}
	return found;
}

}
