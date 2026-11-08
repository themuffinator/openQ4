// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
// Native qualification of the portable level editor authoring core: typed
// geometry adapters, transforms and texture lock, grouped undo, editing
// tools, diagnostics, sandboxed builds, layouts and the asset index.
#include "src/tools/leveleditor/MapBuild.h"
#include "src/tools/leveleditor/EditorState.h"
#include "src/tools/leveleditor/MapWorkspace.h"

#include <chrono>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <string>

using namespace oq4editor;
static int failures = 0, checks = 0;
static void Expect( bool value, const char *label ) {
	++checks;
	if ( !value ) { std::fprintf( stderr, "authoring test failed: %s\n", label ); ++failures; }
}
static bool Near( double a, double b, double tolerance = 1e-3 ) { return std::fabs( a - b ) <= tolerance; }

static const char *box = "{\n brushDef3\n {\n"
	"  ( 0 0 -1 0 ) ( ( 0.0078125 0 0 ) ( 0 0.0078125 0 ) ) \"textures/base_wall/a\"\n"
	"  ( 0 0 1 -64 ) ( ( 0.0078125 0 0.25 ) ( 0 0.0078125 0.5 ) ) \"textures/base_wall/a\"\n"
	"  ( 0 -1 0 0 ) ( ( 0.0078125 0 0 ) ( 0 0.0078125 0 ) ) \"textures/base_wall/a\"\n"
	"  ( 1 0 0 -128 ) ( ( 0.0078125 0 0 ) ( 0 0.0078125 0 ) ) \"textures/base_wall/a\"\n"
	"  ( 0 1 0 -64 ) ( ( 0.0078125 0 0 ) ( 0 0.0078125 0 ) ) \"textures/base_wall/a\"\n"
	"  ( -1 0 0 0 ) ( ( 0.0078125 0 0 ) ( 0 0.0078125 0 ) ) \"textures/base_wall/a\"\n"
	" }\n}";

static const char *patch = "{\n patchDef2\n {\n  \"textures/decals/splat14\"\n  ( 3 3 0 0 0 )\n  (\n"
	"   ( ( 0 0 8 0 0 ) ( 0 32 8 0 0.5 ) ( 0 64 8 0 1 ) )\n"
	"   ( ( 32 0 8 0.5 0 ) ( 32 32 8 0.5 0.5 ) ( 32 64 8 0.5 1 ) )\n"
	"   ( ( 64 0 8 1 0 ) ( 64 32 8 1 0.5 ) ( 64 64 8 1 1 ) )\n  )\n }\n}";

static std::string MapWith( const std::string &world, const std::string &entities ) {
	return "Version 3\n{\n\"classname\" \"worldspawn\"\n" + world + "\n}\n" + entities;
}

// Texture coordinate of a world point on a brush face, as dmap computes it.
static void FaceST( const BrushSide &side, const Vec3 &origin, const Vec3 &world, double &s, double &t ) {
	Vec3 axisS, axisT;
	const Vec3 n = side.Normal();
	TextureAxisBase( n, axisS, axisT );
	const Vec3 local = world - origin;
	const Vec3 v0 = axisS * side.texture[0].value + axisT * side.texture[1].value;
	const Vec3 v1 = axisS * side.texture[3].value + axisT * side.texture[4].value;
	s = local.Dot( v0 ) + side.texture[2].value + origin.Dot( v0 );
	t = local.Dot( v1 ) + side.texture[5].value + origin.Dot( v1 );
}

static void Geometry() {
	Expect( FormatNumber( 0.1 ) == "0.1" && FormatNumber( -64 ) == "-64" && FormatNumber( 1e-12 ) == "0" && FormatNumber( 0.0078125 ) == "0.0078125", "numbers use the shortest fixed spelling" );
	const std::string root2 = FormatNumber( 0.70710678118654757 );
	Expect( static_cast<float>( std::strtod( root2.c_str(), nullptr ) ) == static_cast<float>( 0.70710678118654757 ) && root2.size() <= 10 &&
		root2.find( 'e' ) == std::string::npos && FormatNumber( -0.0 ) == "0", "rotated values keep float precision without exponents" );
	Primitive brush;
	std::string error;
	Expect( brush.Parse( box, error ) && brush.GetKind() == Primitive::Kind::Brush && brush.Sides().size() == 6, "brushDef3 adapter parses sides" );
	Expect( brush.Serialize() == box && !brush.Modified(), "unmodified brush serializes byte for byte" );
	std::string problem;
	Expect( brush.Validate( problem ), "axial box validates" );
	Vec3 mins, maxs;
	Expect( brush.Bounds( Vec3(), mins, maxs ) && Near( mins.x, 0 ) && Near( maxs.x, 128 ) && Near( maxs.y, 64 ) && Near( maxs.z, 64 ), "brush windings give bounds" );
	Expect( brush.Transform( Affine::Translation( Vec3( 16, 0, 0 ) ), Vec3(), Vec3(), true, error ), "translate brush" );
	Expect( brush.Bounds( Vec3(), mins, maxs ) && Near( mins.x, 16 ) && Near( maxs.x, 144 ), "translated bounds" );
	const std::string moved = brush.Serialize();
	Expect( moved.find( "( 1 0 0 -144 )" ) != std::string::npos && moved.find( "\"textures/base_wall/a\"" ) != std::string::npos, "only changed numbers are rewritten" );
	Expect( moved.find( "( 0 0 1 -64 )" ) != std::string::npos, "unchanged plane spelling survives" );
	// Texture lock: every corner keeps its texture coordinate on every face.
	Primitive original, locked, unlocked;
	original.Parse( box, error );
	locked.Parse( box, error );
	unlocked.Parse( box, error );
	const Affine turn = Affine::Rotation( 2, 90, Vec3( 64, 32, 32 ) ) * Affine::Translation( Vec3( 24, -8, 4 ) );
	Expect( locked.Transform( turn, Vec3(), Vec3(), true, error ), "rotate with texture lock" );
	Expect( unlocked.Transform( turn, Vec3(), Vec3(), false, error ), "rotate without texture lock" );
	bool lockHolds = true, unlockedSlides = false;
	for ( std::size_t i = 0; i < original.Sides().size(); ++i ) {
		const auto polygon = PlaneWinding( original.Sides(), i, Vec3() );
		for ( const auto &corner : polygon ) {
			const Vec3 movedCorner = turn.Apply( corner );
			// Find the moved face with the same moved normal.
			const Vec3 n = turn.Linear( original.Sides()[i].Normal() );
			for ( std::size_t j = 0; j < locked.Sides().size(); ++j ) {
				if ( ( locked.Sides()[j].Normal() - n ).Length() > 1e-6 ) { continue; }
				double s0, t0, s1, t1, s2, t2;
				FaceST( original.Sides()[i], Vec3(), corner, s0, t0 );
				FaceST( locked.Sides()[j], Vec3(), movedCorner, s1, t1 );
				FaceST( unlocked.Sides()[j], Vec3(), movedCorner, s2, t2 );
				const double ds = s1 - s0 - std::round( s1 - s0 ), dt = t1 - t0 - std::round( t1 - t0 );
				if ( std::fabs( ds ) > 1e-4 || std::fabs( dt ) > 1e-4 ) { lockHolds = false; }
				if ( std::fabs( s2 - s0 ) > 1e-3 || std::fabs( t2 - t0 ) > 1e-3 ) { unlockedSlides = true; }
			}
		}
	}
	Expect( lockHolds, "texture lock keeps every face's texture attached through rotation and translation" );
	Expect( unlockedSlides, "without texture lock the texture stays in world space" );
	Primitive spun;
	spun.Parse( box, error );
	for ( int i = 0; i < 4; ++i ) { spun.Transform( Affine::Rotation( 2, 90, Vec3( 64, 32, 0 ) ), Vec3(), Vec3(), true, error ); }
	bool full = true;
	for ( std::size_t i = 0; i < 6; ++i ) {
		for ( int k = 0; k < 4; ++k ) { if ( !Near( spun.Sides()[i].plane[k].value, spun.Sides()[i].plane[k].original, 1e-6 ) ) { full = false; } }
		for ( int k = 0; k < 6; ++k ) { if ( !Near( spun.Sides()[i].texture[k].value, spun.Sides()[i].texture[k].original, 1e-4 ) ) { full = false; } }
	}
	Expect( full, "four locked quarter turns restore planes and texture matrices" );
	Primitive scaled;
	scaled.Parse( box, error );
	Expect( scaled.Transform( Affine::Scale( Vec3( 2, 1, 0.5 ), Vec3() ), Vec3(), Vec3(), true, error ) && scaled.Bounds( Vec3(), mins, maxs ) &&
		Near( maxs.x, 256 ) && Near( maxs.z, 32 ), "non-uniform scale" );
	Expect( !scaled.Transform( Affine::Scale( Vec3( -1, 1, 1 ), Vec3() ), Vec3(), Vec3(), true, error ), "mirroring is rejected (it would invert faces)" );
	// Entity-local brushes: moving the owning entity's origin.
	Primitive local;
	local.Parse( box, error );
	const Vec3 o( 100, 0, 0 );
	Expect( local.Transform( Affine::Translation( Vec3( 0, 50, 0 ) ), o, o + Vec3( 0, 50, 0 ), false, error ) && local.Serialize() == box, "origin moves carry entity-local brush planes unchanged" );
	Expect( local.Transform( Affine::Translation( Vec3( 0, 50, 0 ) ), o, o + Vec3( 0, 50, 0 ), true, error ) && local.Serialize() != box, "texture lock compensates world-space texturing for origin moves" );
	Primitive curve;
	Expect( curve.Parse( patch, error ) && curve.GetKind() == Primitive::Kind::Patch && curve.PatchWidth() == 3 && curve.Points().size() == 9, "patch adapter parses the control net" );
	Expect( curve.Serialize() == patch && curve.Validate( problem ), "unmodified patch is byte identical and valid" );
	Expect( curve.Transform( Affine::Translation( Vec3( 0, 0, 8 ) ), o, o, true, error ) && curve.Bounds( Vec3(), mins, maxs ) && Near( maxs.z, 16 ), "patches are world space" );
	Expect( curve.Serialize().find( "( 0 0 16 0 0 )" ) != std::string::npos, "patch texture coordinates are untouched" );
	Primitive legacy;
	Expect( legacy.Parse( "{ ( 0 0 0 ) ( 1 0 0 ) ( 0 1 0 ) common/caulk 0 0 0 0 0 }", error ) && legacy.GetKind() == Primitive::Kind::Unsupported, "Quake 3 brushes stay opaque" );
	Expect( !legacy.Transform( Affine::Translation( Vec3( 1, 0, 0 ) ), Vec3(), Vec3(), true, error ), "opaque primitives refuse transforms" );
	Primitive bad;
	Expect( bad.Parse( "{\n brushDef3\n {\n  ( 0 0 1 0 ) ( ( 1 0 0 ) ( 0 1 0 ) ) \"x\"\n  ( 0 0 -1 0 ) ( ( 1 0 0 ) ( 0 1 0 ) ) \"x\"\n }\n}", error ) && !bad.Validate( problem ), "degenerate brush fails validation" );
	Expect( !bad.Parse( "{ brushDef3 { ( 0 0 1 ) } }", error ), "malformed plane rejected" );
	Expect( !bad.Parse( "{ patchDef2 { \"m\" ( 3 3 0 0 0 ) ( ( ( 0 0 0 0 0 ) ) ) } }", error ), "short patch net rejected" );
	Primitive material;
	material.Parse( box, error );
	Expect( material.SetMaterial( "textures/common/caulk", error ) && material.Serialize().find( "base_wall" ) == std::string::npos, "material replacement" );
	Expect( !material.SetMaterial( "bad\"name", error ), "material quote injection rejected" );
	const std::string created = BoxBrushSource( Vec3( -8, -8, 0 ), Vec3( 8, 8, 16 ), "textures/common/caulk", Vec3(), "\n" );
	Primitive made;
	Expect( made.Parse( created, error ) && made.Validate( problem ) && made.Bounds( Vec3(), mins, maxs ) && Near( mins.x, -8 ) && Near( maxs.z, 16 ), "created box brush is valid" );
	Expect( made.Parse( FlatPatchSource( Vec3( 0, 0, 0 ), Vec3( 64, 64, 8 ), "textures/common/caulk", "\n" ), error ) && made.Validate( problem ), "created patch is valid" );
}

static void Transactions() {
	MapDocument document;
	std::string error;
	Expect( document.OpenText( MapWith( box, "{\n\"classname\" \"light\"\n\"name\" \"l1\"\n\"origin\" \"0 0 0\"\n}\n" ), error ), "open transaction fixture" );
	const std::string original = document.Serialize();
	const EntityId world = document.Entities()[0].id, light = document.Entities()[1].id;
	Expect( document.BeginTransaction( "gesture", error ), "begin" );
	Expect( !document.Undo( error ), "undo refused during an open edit" );
	document.SetProperty( light, "origin", "8 0 0", error );
	document.SetProperty( light, "origin", "16 0 0", error );
	std::size_t added = 0;
	Expect( document.AddPrimitive( world, box, added, error ) && added == 1, "add primitive inside transaction" );
	Expect( document.PendingChanges() == 3 && document.IsDirty(), "pending changes apply live" );
	document.RevertTransaction();
	Expect( document.Serialize() == original && !document.IsDirty() && document.InTransaction(), "revert restores source and keeps the gesture open" );
	document.SetProperty( light, "origin", "32 0 0", error );
	Expect( document.CommitTransaction( error ) && document.HistorySteps() == 1 && document.UndoLabel() == "gesture", "gesture commits one step" );
	Expect( document.Undo( error ) && document.Serialize() == original && !document.IsDirty(), "one undo reverts the gesture" );
	Expect( document.Redo( error ) && document.Entities()[1].Value( "origin" ) == "32 0 0", "redo the gesture" );
	Expect( document.BeginTransaction( "outer", error ) && document.BeginTransaction( "inner", error ), "nested begin" );
	document.SetProperty( light, "spawnflags", "1", error );
	Expect( document.CommitTransaction( error ) && document.InTransaction(), "inner commit keeps the outer open" );
	document.CancelTransaction();
	Expect( !document.InTransaction() && document.Entities()[1].Value( "spawnflags" ).empty(), "cancel discards nested work" );
	Expect( document.BeginTransaction( "empty", error ) && document.CommitTransaction( error ) && document.HistorySteps() == 1, "empty transaction records nothing" );
	// Primitive edits keep cached ranges in step with a full re-parse.
	const auto consistent = [&]() {
		MapDocument fresh;
		std::string e;
		if ( !fresh.OpenText( document.Serialize(), e ) || fresh.Entities().size() != document.Entities().size() ) { return false; }
		for ( std::size_t i = 0; i < fresh.Entities().size(); ++i ) {
			if ( fresh.Entities()[i].primitiveRanges != document.Entities()[i].primitiveRanges ||
				fresh.Entities()[i].propertyInsert != document.Entities()[i].propertyInsert ) { return false; }
		}
		return true;
	};
	const std::string before = document.Serialize();
	Expect( document.AddPrimitive( world, patch, added, error ) && consistent(), "added primitive ranges match a re-parse" );
	Expect( document.SetPrimitive( world, 0, BoxBrushSource( Vec3( 0, 0, 0 ), Vec3( 32, 32, 32 ), "textures/common/caulk", Vec3(), "\n" ), error ) && consistent(), "replaced primitive ranges match a re-parse" );
	Expect( document.RemovePrimitive( world, 0, error ) && consistent() && document.Entities()[0].primitiveRanges.size() == 1, "removed primitive ranges match a re-parse" );
	Expect( document.SetProperty( world, "message", "x", error ) && consistent(), "property insert stays before geometry" );
	for ( int i = 0; i < 4; ++i ) { document.Undo( error ); }
	Expect( document.Serialize() == before && consistent(), "primitive edits undo byte for byte" );
	Expect( !document.SetPrimitive( world, 0, "{ brushDef3 { ( } }", error ), "unbalanced primitive text rejected" );
	Expect( !document.SetPrimitive( world, 0, "{ a } }", error ), "trailing text after a primitive rejected" );
	Expect( !document.SetPrimitive( world, 9, box, error ), "unknown primitive index rejected" );
	EntityId inserted = 0;
	Expect( document.InsertEntity( "{\n\"classname\" \"func_static\"\n\"name\" \"fs\"\n" + std::string( box ) + "\n}", inserted, error ) &&
		document.Find( inserted )->primitiveCount == 1, "insert a geometry entity" );
	Expect( !document.InsertEntity( "{\n\"classname\" \"light\"\n\"name\" \"FS\"\n}", inserted, error ), "inserted names stay unique" );
	Expect( !document.InsertEntity( "{\n\"classname\" \"worldspawn\"\n}", inserted, error ), "a second worldspawn cannot be inserted" );
	Expect( !document.InsertEntity( "{\n\"classname\" \"light\"\n}\n{\n\"classname\" \"light\"\n}", inserted, error ), "exactly one entity per insert" );
}

static void Editing() {
	MapDocument document;
	std::string error;
	const std::string entities =
		"{\n\"classname\" \"trigger_once\"\n\"name\" \"trigger_1\"\n\"target\" \"door_1\"\n" + std::string( box ) + "\n}\n"
		"{\n\"classname\" \"func_door\"\n\"name\" \"door_1\"\n\"origin\" \"64 0 0\"\n\"target2\" \"trigger_1\"\n" + std::string( box ) + "\n" + patch + "\n}\n"
		"{\n\"classname\" \"info_player_start\"\n\"name\" \"start\"\n\"origin\" \"0 0 16\"\n\"angle\" \"90\"\n}\n"
		"{\n\"classname\" \"light\"\n\"name\" \"light_1\"\n\"origin\" \"0 0 128\"\n\"light_radius\" \"100 100 50\"\n\"target\" \"missing_thing\"\n}\n";
	Expect( document.OpenText( MapWith( std::string( box ) + "\n" + patch, entities ), error ), "open editing fixture" );
	MapEditor editor;
	const auto &e = document.Entities();
	const EntityId world = e[0].id, trigger = e[1].id, door = e[2].id, start = e[3].id, light = e[4].id;
	const std::string original = document.Serialize();
	// Mixed selection: a point entity, a geometry entity, a world primitive.
	Selection mixed = { { start, WholeEntity }, { door, WholeEntity }, { world, 1 } };
	Expect( editor.Transform( document, mixed, Affine::Translation( Vec3( 32, 0, 0 ) ), true, error ), "translate a mixed selection" );
	Expect( document.HistorySteps() == 1, "mixed transform is one undo step" );
	Expect( document.Find( start )->Value( "origin" ) == "32 0 16" && document.Find( door )->Value( "origin" ) == "96 0 0", "origins move" );
	Vec3 lo, hi;
	Expect( editor.Bounds( document, { { door, 0 } }, lo, hi ) && Near( lo.x, 96 ), "entity-local brush follows its origin" );
	Expect( document.PrimitiveText( world, 0 ) == box, "unselected world primitive untouched" );
	Expect( document.PrimitiveText( door, 1 ).find( "( 32 0 8 0 0 )" ) != std::string::npos, "world-space patch in a geometry entity follows the move" );
	Expect( document.Undo( error ) && document.Serialize() == original, "mixed transform undoes exactly" );
	Expect( editor.Transform( document, { { start, WholeEntity } }, Affine::Rotation( 2, 90, Vec3( 0, 0, 16 ) ), true, error ) &&
		document.Find( start )->Value( "angle" ) == "180", "Z rotation turns 'angle'" );
	Expect( editor.Transform( document, { { start, WholeEntity } }, Affine::Rotation( 0, 90, Vec3( 0, 0, 16 ) ), true, error ) &&
		!document.Find( start )->Value( "rotation" ).empty() && document.Find( start )->Value( "angle" ).empty(), "other rotations write 'rotation'" );
	Expect( editor.Transform( document, { { light, WholeEntity } }, Affine::Scale( Vec3( 2, 2, 2 ), Vec3( 0, 0, 128 ) ), true, error ) &&
		document.Find( light )->Value( "light_radius" ) == "200 200 100", "scaling a light scales its radius" );
	document.Undo( error ); document.Undo( error ); document.Undo( error );
	Expect( document.Serialize() == original, "orientation edits undo" );
	Expect( editor.Snap( document, { { world, 0 } }, 64, true, error ), "snap world brush" );
	Expect( editor.Snap( document, { { start, WholeEntity } }, 32, true, error ) && document.Find( start )->Value( "origin" ) == "0 0 32", "snap point entity origin" );
	document.Undo( error ); document.Undo( error );
	SelectionItem brush;
	Expect( editor.CreateBrush( document, world, Vec3( 0, 0, 0 ), Vec3( 64, 64, 8 ), "textures/common/caulk", brush, error ) && brush.primitive == 2, "create world brush" );
	Expect( editor.CreateBrush( document, door, Vec3( 64, 0, 0 ), Vec3( 128, 64, 64 ), "textures/common/caulk", brush, error ), "create brush in an entity with an origin" );
	Vec3 mins, maxs;
	Expect( editor.Bounds( document, { brush }, mins, maxs ) && Near( mins.x, 64 ) && Near( maxs.x, 128 ), "entity brush created in world coordinates" );
	Expect( !editor.CreateBrush( document, world, Vec3( 0, 0, 0 ), Vec3( 0.5, 64, 8 ), "textures/common/caulk", brush, error ), "zero-size brush rejected" );
	document.Undo( error ); document.Undo( error );
	Expect( document.Serialize() == original, "creation undoes" );
	Selection copies;
	Expect( editor.Duplicate( document, { { trigger, WholeEntity }, { door, WholeEntity }, { world, 0 } }, Vec3( 0, 256, 0 ), copies, error ) && copies.size() == 3, "duplicate mixed selection" );
	const Entity *triggerCopy = document.Find( copies[0].entity ), *doorCopy = document.Find( copies[1].entity );
	Expect( triggerCopy && doorCopy && triggerCopy->Value( "name" ) == "trigger_2" && doorCopy->Value( "name" ) == "door_2", "copies get unique names" );
	Expect( triggerCopy->Value( "target" ) == "door_2" && doorCopy->Value( "target2" ) == "trigger_2", "links among copies follow the copies" );
	Expect( document.Find( trigger )->Value( "target" ) == "door_1", "originals keep their links" );
	Expect( document.HistorySteps() >= 1 && document.UndoLabel() == "duplicate", "duplicate is one undo step" );
	document.Undo( error );
	Expect( document.Serialize() == original, "duplicate undoes" );
	Expect( editor.Delete( document, { { light, WholeEntity }, { world, 0 }, { door, 1 } }, error ) && document.UndoLabel() == "delete", "delete mixed selection" );
	Expect( !document.Find( light ) && document.Find( door )->primitiveRanges.size() == 1 && document.Entities()[0].primitiveRanges.size() == 1, "mixed deletion" );
	Expect( !editor.Delete( document, { { world, WholeEntity } }, error ), "worldspawn deletion rejected" );
	document.Undo( error );
	Expect( document.Serialize() == original, "delete undoes" );
	EntityId group = 0;
	Expect( editor.Group( document, { { world, 0 }, { world, 1 } }, group, error ) && document.Find( group )->primitiveCount == 2 &&
		document.Entities()[0].primitiveRanges.empty(), "group world primitives into func_group" );
	Expect( !editor.Group( document, { { door, 0 } }, group, error ), "entity geometry cannot be grouped" );
	CompileIndex index;
	index.Build( document );
	SelectionItem mapped;
	Expect( index.PrimitiveAt( 0, 1, mapped ) && mapped.entity == group && mapped.primitive == 1, "compiled world primitives include func_group primitives" );
	EntityId compiled = 0;
	Expect( index.EntityAt( 1, compiled ) && compiled == trigger && index.Entities() == 5, "compiled entity numbers skip func_group" );
	Expect( editor.Ungroup( document, group, error ) && !document.Find( group ) && document.Entities()[0].primitiveRanges.size() == 2, "ungroup returns primitives to the world" );
	document.Undo( error ); document.Undo( error );
	Expect( document.Serialize() == original, "group and ungroup undo" );
	Selection layered;
	Expect( editor.SetLayer( document, { { world, 1 }, { start, WholeEntity } }, "detail", layered, error ) && layered.size() == 2, "assign layer" );
	Expect( document.Find( start )->Value( "editor_layer" ) == "detail", "entity layer key" );
	bool groupLayer = false;
	for ( const auto &item : layered ) {
		const Entity *entity = document.Find( item.entity );
		if ( entity && EqualNoCase( entity->Value( "classname" ), "func_group" ) && entity->Value( "editor_layer" ) == "detail" ) { groupLayer = true; }
	}
	Expect( groupLayer, "world primitives join a layered group" );
	document.Undo( error );
	Expect( document.Serialize() == original, "layer assignment undoes" );
	const auto references = editor.References( document, "door_1" );
	Expect( references.size() == 1 && references[0].entity == trigger, "reference search" );
	std::size_t updated = 0;
	Expect( editor.Rename( document, door, "door_main", updated, error ) && updated == 1 && document.Find( trigger )->Value( "target" ) == "door_main", "rename repairs references" );
	Expect( !editor.Rename( document, door, "START", updated, error ), "rename refuses a used name" );
	Expect( document.UndoLabel() == "rename" && document.Undo( error ) && document.Serialize() == original, "rename undoes in one step" );
	Expect( editor.SetMaterial( document, { { world, WholeEntity } }, "textures/common/caulk", error ) &&
		document.PrimitiveText( world, 0 ).find( "base_wall" ) == std::string::npos && document.PrimitiveText( world, 1 ).find( "splat14" ) == std::string::npos, "material applies to brushes and patches" );
	document.Undo( error );
	std::string prefab;
	Expect( editor.ExportPrefab( document, { { trigger, WholeEntity }, { door, WholeEntity }, { world, 0 } }, prefab, error ), "export prefab" );
	MapDocument prefabDoc;
	Expect( prefabDoc.OpenText( prefab, error ) && prefabDoc.Entities().size() == 3 && prefabDoc.Entities()[0].primitiveCount == 1, "prefab is a valid map" );
	Selection inserted;
	Expect( editor.InsertPrefab( document, prefab, Vec3( 1024, 0, 0 ), inserted, error ) && inserted.size() == 3, "insert prefab" );
	const Entity *pt = nullptr, *pd = nullptr;
	for ( const auto &item : inserted ) {
		const Entity *entity = document.Find( item.entity );
		if ( EqualNoCase( entity->Value( "classname" ), "trigger_once" ) ) { pt = entity; }
		if ( EqualNoCase( entity->Value( "classname" ), "func_door" ) ) { pd = entity; }
	}
	Expect( pt && pd && pt->Value( "name" ) != "trigger_1" && pd->Value( "name" ) != "door_1" && pt->Value( "target" ) == pd->Value( "name" ), "prefab names are unique and its links repaired" );
	Expect( editor.Bounds( document, inserted, mins, maxs ) && mins.x > 900, "prefab placed at the insertion point" );
	Expect( document.UndoLabel() == "insert prefab" && document.Undo( error ) && document.Serialize() == original, "prefab insertion undoes" );
	// Unsupported primitives make the whole transform fail atomically.
	MapDocument legacy;
	legacy.OpenText( "Version 1\n{\n\"classname\" \"worldspawn\"\n{ ( 0 0 0 ) ( 1 0 0 ) ( 0 1 0 ) caulk 0 0 0 0 0 }\n" + std::string( box ) + "\n}\n", error );
	const std::string legacyText = legacy.Serialize();
	Expect( !editor.Transform( legacy, { { legacy.Entities()[0].id, WholeEntity } }, Affine::Translation( Vec3( 1, 0, 0 ) ), true, error ) &&
		legacy.Serialize() == legacyText && !legacy.CanUndo(), "a failed transform changes nothing" );
	// Cancellation of a live gesture.
	Expect( document.BeginTransaction( "drag", error ), "begin drag" );
	for ( int step = 1; step <= 10; ++step ) {
		document.RevertTransaction();
		editor.Transform( document, { { world, 0 }, { start, WholeEntity } }, Affine::Translation( Vec3( step * 8.0, 0, 0 ) ), true, error );
	}
	Expect( document.Find( start )->Value( "origin" ) == "80 0 16", "drag applies its total offset" );
	document.CancelTransaction();
	Expect( document.Serialize() == original && !document.IsDirty(), "cancelled drag leaves the source untouched" );
	MapGeometryCache cache;
	Expect( cache.Update( document ) == 5 && cache.Primitives() == 5, "geometry cache parses each primitive once" );
	Expect( cache.Update( document ) == 0, "unchanged source re-parses nothing" );
	SelectionItem hit;
	double distance;
	Expect( cache.Pick( Vec3( 64, 32, 1000 ), Vec3( 0, 0, -1 ), nullptr, hit, distance ) && hit.entity == world && hit.primitive == 0 && Near( distance, 936 ), "pick a world brush from above" );
	Expect( cache.Pick( Vec3( 0, 0, 1000 ), Vec3( 0, 0, -1 ), [&]( const SelectionItem &item ) { return item.entity != world; }, hit, distance ) && hit.entity == light, "pick filter skips hidden items" );
	editor.Transform( document, { { door, WholeEntity } }, Affine::Translation( Vec3( 0, 0, 512 ) ), false, error );
	Expect( cache.Update( document ) == 1, "origin move re-parses only the world-space patch" );
	const auto *doorGeometry = cache.Find( door );
	Expect( doorGeometry && Near( doorGeometry->primitives[0].mins.z, 512 ), "origin move re-places cached local brushes" );
	document.Undo( error );
}

static void Validation() {
	MapDocument document;
	std::string error;
	document.OpenText( MapWith( "{\n brushDef3\n {\n  ( 0 0 1 0 ) ( ( 1 0 0 ) ( 0 1 0 ) ) \"x\"\n }\n}",
		"{\n\"classname\" \"light\"\n\"name\" \"a\"\n\"target\" \"nowhere\"\n\"noshadows\" \"yes\"\n\"_color\" \"2 0 0\"\n}\n"
		"{\n\"classname\" \"monster_x\"\n\"name\" \"A\"\n\"origin\" \"1 2\"\n\"model\" \"models/none.lwo\"\n}\n" ), error );
	ClassSchema light;
	light.known = true;
	light.properties.push_back( { "count", PropertyType::Int, "" } );
	ValidatorOptions options;
	options.schema = [&]( const std::string &classname ) -> const ClassSchema * { return EqualNoCase( classname, "light" ) ? &light : nullptr; };
	options.assetExists = []( PropertyType, const std::string &name ) { return name.find( "none" ) == std::string::npos; };
	const auto diagnostics = ValidateMap( document, nullptr, options );
	std::map<std::string, int> codes;
	for ( const auto &d : diagnostics ) { ++codes[d.code]; }
	Expect( codes["duplicate-name"] == 1 && codes["missing-target"] == 1 && codes["unknown-class"] == 1, "names, targets and classes" );
	Expect( codes["invalid-value"] == 2 && codes["invalid-origin"] == 1 && codes["missing-asset"] == 1, "typed values, origins and assets" );
	Expect( codes["invalid-geometry"] == 1 && codes["no-player-start"] == 1, "geometry and spawn checks" );
	bool linked = false;
	for ( const auto &d : diagnostics ) { if ( d.code == "invalid-geometry" && d.entity == document.Entities()[0].id && d.primitive == 0 ) { linked = true; } }
	Expect( linked, "geometry diagnostics link to their primitive" );
	std::string problem;
	Expect( ValidateValue( PropertyType::Color, "1 0.5 0", problem ) && !ValidateValue( PropertyType::Vector, "1 2", problem ) &&
		!ValidateValue( PropertyType::Bool, "2", problem ) && ValidateValue( PropertyType::Matrix, "1 0 0 0 1 0 0 0 1", problem ), "value types" );
	PropertyType type;
	Expect( TypeFromEditorKey( "snd", type ) && type == PropertyType::Sound && TypeFromEditorKey( "mat", type ) && type == PropertyType::Material, "editor_ declarations map to types" );
	CompileIndex index;
	MapDocument compiled;
	compiled.OpenText( MapWith( box, "{\n\"classname\" \"func_group\"\n" + std::string( box ) + "\n" + box + "\n}\n{\n\"classname\" \"light\"\n}\n{\n\"classname\" \"func_static\"\n\"name\" \"s\"\n" + box + "\n}\n" ) +
		"", error );
	index.Build( compiled );
	Diagnostic diagnostic;
	Expect( ParseCompilerLine( "WARNING: brush primitive 2 on entity 0 is degenerate", index, diagnostic ) &&
		diagnostic.entity == compiled.Entities()[1].id && diagnostic.primitive == 1, "dmap world primitive maps into its func_group" );
	Expect( ParseCompilerLine( "WARNING: brush 0 on entity 2 is unbounded", index, diagnostic ) && diagnostic.entity == compiled.Entities()[3].id && diagnostic.primitive == 0, "dmap entity numbers skip func_group" );
	Expect( ParseCompilerLine( "WARNING: ******* leaked *******", index, diagnostic ) && diagnostic.code == "leak" && diagnostic.severity == Diagnostic::Severity::Error, "leak" );
	Expect( !ParseCompilerLine( "--- FloodAreas ---", index, diagnostic ), "progress lines are not diagnostics" );
	MapDocument removed;
	removed.OpenText( "Version 3\n{\n\"classname\" \"worldspawn\"\n\"removeEntities\" \"light\"\n}\n{\n\"classname\" \"light\"\n}\n{\n\"classname\" \"info_player_start\"\n}\n", error );
	index.Build( removed );
	EntityId id;
	Expect( index.EntityAt( 1, id ) && id == removed.Entities()[2].id, "removeEntities classes vanish from compiled numbering" );
}

class FakeFiles : public BuildFiles {
public:
	std::map<std::string, std::string> files;
	std::string failPromote, failWrite;
	bool Read( const std::string &path, std::string &text, bool &exists, std::string & ) override {
		const auto it = files.find( path ); exists = it != files.end(); text = exists ? it->second : ""; return true;
	}
	bool ReadFrom( const std::string &path, std::size_t offset, std::size_t limit, std::string &text ) override {
		const auto it = files.find( path );
		text.clear();
		if ( it == files.end() ) { return false; }
		if ( offset < it->second.size() ) { text = it->second.substr( offset, limit ); }
		return true;
	}
	bool WriteAndSync( const std::string &path, const std::string &text, std::string &error ) override {
		if ( !failWrite.empty() && path.find( failWrite ) != std::string::npos ) { error = "injected write failure"; return false; }
		files[path] = text; return true;
	}
	bool Promote( const std::string &from, const std::string &to, std::string &error ) override {
		if ( !failPromote.empty() && to.find( failPromote ) != std::string::npos ) { error = "injected promotion failure"; return false; }
		if ( !files.count( from ) ) { error = "missing"; return false; }
		files[to] = files[from]; files.erase( from ); return true;
	}
	void Remove( const std::string &path ) override { files.erase( path ); }
	std::vector<std::string> List( const std::string &directory, const std::string &prefix ) override {
		std::vector<std::string> result;
		for ( const auto &[path, text] : files ) {
			if ( path.compare( 0, directory.size() + 1, directory + "/" ) != 0 ) { continue; }
			const std::string name = path.substr( directory.size() + 1 );
			if ( name.find( '/' ) == std::string::npos && name.compare( 0, prefix.size(), prefix ) == 0 ) { result.push_back( name ); }
		}
		return result;
	}
	std::string OSPath( const std::string &relative ) override { return "C:/save/baseoq4/" + relative; }
};

// A scripted child: appends its log over a few polls, writes outputs, exits.
class FakeProcess : public BuildProcess {
public:
	FakeProcess( FakeFiles &f, std::vector<std::string> log, std::vector<std::string> outputs, int exit ) : files( f ), lines( log ), produce( outputs ), exitCode( exit ) {}
	std::vector<std::string> arguments;
	bool killed = false, failStart = false;
	bool Start( const std::vector<std::string> &args, std::string &error ) override {
		if ( failStart ) { error = "injected spawn failure"; return false; }
		arguments = args; step = 0; return true;
	}
	bool Running( int &code ) override {
		if ( killed ) { code = -1; return false; }
		if ( step < lines.size() ) { files.files[log] += lines[step++] + "\n"; return true; }
		for ( const auto &file : produce ) { files.files[file] = "output " + file; }
		produce.clear();
		code = exitCode;
		return false;
	}
	void Kill() override { killed = true; }
	std::string log = "editor_experimental/build/baseoq4/logs/editor_build.log";
private:
	FakeFiles &files;
	std::vector<std::string> lines, produce;
	int exitCode;
	std::size_t step = 0;
};

static void Builds() {
	const std::string sandboxMaps = "editor_experimental/build/baseoq4/maps/custom/";
	const std::vector<std::string> goodLog = { "---- dmap ----", "--- LoadDMapFile ---", "--- FloodEntities ---", "WARNING: brush primitive 0 on entity 1 is degenerate",
		"----- WriteOutputFile -----", "   12 seconds for dmap", "   1 seconds to create collision map", "   4 seconds to create AAS" };
	const std::vector<std::string> outputs = { sandboxMaps + "room.proc", sandboxMaps + "room.cm", sandboxMaps + "room.aas32", sandboxMaps + "room.aas48" };
	MapDocument document;
	std::string error;
	document.OpenText( MapWith( box, "{\n\"classname\" \"func_static\"\n\"name\" \"s\"\n" + std::string( box ) + "\n}\n" ), error );
	CompileIndex index;
	index.Build( document );
	BuildRequest request;
	request.mapPath = "maps/custom/room.map";
	request.mapText = document.Serialize();
	request.launch = { "openQ4-ded_x64.exe" };
	{
		FakeFiles files;
		files.files["maps/custom/room.proc"] = "old proc";
		files.files["maps/custom/room.aas96"] = "stale aas";
		files.files["maps/custom/room.map"] = "saved map";
		auto process = std::make_unique<FakeProcess>( files, goodLog, outputs, 0 );
		FakeProcess *child = process.get();
		MapBuild build( files, std::move( process ) );
		Expect( build.Start( request, index, 0, error ), "start build" );
		Expect( files.files[sandboxMaps + "room.map"] == request.mapText, "sandbox receives the saved source" );
		bool savepathArg = false, outputArg = false;
		for ( std::size_t i = 0; i + 1 < child->arguments.size(); ++i ) {
			if ( child->arguments[i] == "fs_savepath" && child->arguments[i + 1] == "C:/save/baseoq4/editor_experimental/build" ) { savepathArg = true; }
			if ( child->arguments[i] == "com_compilerOutput" && child->arguments[i + 1] == "fs_savepath" ) { outputArg = true; }
		}
		Expect( savepathArg && outputArg && child->arguments.back() == "+quit", "child runs in the sandbox and writes outputs there" );
		float lastProgress = 0;
		bool monotonic = true;
		for ( unsigned t = 100; build.Active() && t < 100000; t += 100 ) {
			build.Poll( t );
			if ( build.Progress() < lastProgress ) { monotonic = false; }
			lastProgress = build.Progress();
			if ( build.Active() ) { Expect( files.files["maps/custom/room.proc"] == "old proc", "previous output stays while compiling" ); }
		}
		Expect( monotonic && build.GetState() == MapBuild::State::Succeeded && build.Progress() == 1.0f, "build succeeds with monotonic progress" );
		Expect( files.files["maps/custom/room.proc"] == "output " + sandboxMaps + "room.proc" && files.files.count( "maps/custom/room.aas48" ), "outputs published beside the map" );
		Expect( !files.files.count( "maps/custom/room.aas96" ), "stale outputs from earlier builds are retired" );
		Expect( files.files["maps/custom/room.map"] == "saved map" && files.files.count( sandboxMaps + "room.proc" ), "map untouched; sandbox kept as the play profile" );
		Expect( build.Diagnostics().size() == 1 && build.Diagnostics()[0].entity == document.Entities()[1].id && build.Diagnostics()[0].primitive == 0, "compiler warning links to its source primitive" );
		bool leftovers = false;
		for ( const auto &[path, text] : files.files ) { if ( path.find( ".editor-" ) != std::string::npos ) { leftovers = true; } }
		Expect( !leftovers, "no staging or backup files remain" );
	}
	const auto failing = [&]( std::vector<std::string> log, int exit, const std::string &failPromote, const char *label ) {
		FakeFiles files;
		files.files["maps/custom/room.proc"] = "old proc";
		files.files["maps/custom/room.cm"] = "old cm";
		files.failPromote = failPromote;
		MapBuild build( files, std::make_unique<FakeProcess>( files, log, outputs, exit ) );
		build.Start( request, index, 0, error );
		for ( unsigned t = 100; build.Active() && t < 100000; t += 100 ) { build.Poll( t ); }
		Expect( build.GetState() == MapBuild::State::Failed && !build.Failure().empty(), label );
		Expect( files.files["maps/custom/room.proc"] == "old proc" && files.files["maps/custom/room.cm"] == "old cm" && !files.files.count( "maps/custom/room.aas32" ), "failed build keeps the previous playable output" );
		bool leftovers = false;
		for ( const auto &[path, text] : files.files ) { if ( path.find( ".editor-" ) != std::string::npos ) { leftovers = true; } }
		Expect( !leftovers, "failed build leaves no staging files" );
	};
	failing( { "--- LoadDMapFile ---", "WARNING: ******* leaked *******", "   3 seconds for dmap" }, 0, "", "leak fails the build" );
	failing( goodLog, 3, "", "nonzero exit fails the build" );
	failing( { "--- LoadDMapFile ---" }, 0, "", "incomplete compile fails the build" );
	failing( goodLog, 0, "room.aas48", "promotion failure rolls back" );
	{
		FakeFiles files;
		files.files["maps/custom/room.proc"] = "old proc";
		auto process = std::make_unique<FakeProcess>( files, goodLog, outputs, 0 );
		FakeProcess *child = process.get();
		MapBuild build( files, std::move( process ) );
		build.Start( request, index, 0, error );
		build.Poll( 100 ); build.Poll( 200 );
		Expect( build.Active() && build.Progress() > 0 && build.Progress() < 1, "cancellable mid-build" );
		Expect( !build.Start( request, index, 250, error ), "one build at a time" );
		build.Cancel();
		Expect( child->killed && build.GetState() == MapBuild::State::Cancelled && files.files["maps/custom/room.proc"] == "old proc", "cancel kills the compiler and keeps old output" );
		Expect( build.Start( request, index, 300, error ), "a cancelled build can restart" );
	}
	{
		// A child that reports an error and then never exits is stopped.
		FakeFiles files;
		class Stuck : public FakeProcess {
		public:
			using FakeProcess::FakeProcess;
			bool Running( int &code ) override { if ( calls++ == 0 ) { FakeProcess::Running( code ); } return !killedNow && ( code = 0, true ); }
			void Kill() override { killedNow = true; FakeProcess::Kill(); }
			int calls = 0;
			bool killedNow = false;
		};
		auto process = std::make_unique<Stuck>( files, std::vector<std::string>{ "ERROR: Couldn't open maps/custom/room.lin" }, outputs, 0 );
		Stuck *child = process.get();
		MapBuild build( files, std::move( process ) );
		build.Start( request, index, 0, error );
		unsigned t = 0;
		for ( t = 100; build.Active() && t < 60000; t += 100 ) { build.Poll( t ); }
		Expect( child->killedNow && build.GetState() == MapBuild::State::Failed && t < 20000, "a compiler stuck after an error is stopped promptly" );
	}
	{
		FakeFiles files;
		std::vector<std::string> endless( 100000, "working" );
		request.timeoutMs = 1000;
		MapBuild build( files, std::make_unique<FakeProcess>( files, endless, outputs, 0 ) );
		build.Start( request, index, 0, error );
		for ( unsigned t = 100; build.Active() && t < 10000; t += 100 ) { build.Poll( t ); }
		Expect( build.GetState() == MapBuild::State::Failed && build.Failure().find( "time" ) != std::string::npos, "timed-out compiler is stopped" );
		request.timeoutMs = 3600000;
	}
	{
		FakeFiles files;
		auto process = std::make_unique<FakeProcess>( files, goodLog, outputs, 0 );
		process->failStart = true;
		MapBuild build( files, std::move( process ) );
		Expect( !build.Start( request, index, 0, error ) && !build.Active(), "spawn failure is reported" );
		files.failWrite = "editor_experimental/build";
		Expect( !build.Start( request, index, 0, error ), "sandbox write failure is reported" );
		BuildRequest unsaved = request;
		unsaved.mapPath = "";
		Expect( !build.Start( unsaved, index, 0, error ), "builds need a saved map path" );
	}
}

static void Layout() {
	DockLayout layout;
	auto result = layout.Compute( { 8, 108, 624, 330 } );
	Expect( result.panels[DockLayout::Browser].visible && result.panels[DockLayout::Inspector].visible && result.panels[DockLayout::Problems].visible &&
		!result.panels[DockLayout::Tools].visible && result.panels[DockLayout::Tools].tabVisible, "default docks show the browser, inspector and problems, tools as a tab" );
	Expect( Near( result.views.x, 8 + 154 + 8 ) && Near( result.views.w, 624 - 154 - 188 - 16 ) && result.views.h < 330, "views fill the centre above the problems dock" );
	Expect( !layout.Move( DockLayout::Inspector, DockLayout::Bottom ), "tall panels cannot dock at the bottom" );
	Expect( !layout.Move( DockLayout::Tools, DockLayout::Right ) || layout.DockOf( DockLayout::Tools ) == DockLayout::Right, "moving within a dock is a no-op" );
	Expect( layout.Move( DockLayout::Tools, DockLayout::Left ), "dock tools left" );
	Expect( !layout.Move( DockLayout::Problems, DockLayout::Left ), "a side dock holds at most two panels" );
	Expect( layout.Move( DockLayout::Browser, DockLayout::Hidden ) && layout.Move( DockLayout::Inspector, DockLayout::Left ), "dock inspector left" );
	layout.Toggle( DockLayout::Browser );
	Expect( layout.DockOf( DockLayout::Browser ) == DockLayout::Hidden, "a panel cannot return to a full dock" );
	layout.Move( DockLayout::Tools, DockLayout::Right );
	layout.Toggle( DockLayout::Browser );
	result = layout.Compute( { 8, 108, 624, 330 } );
	Expect( result.panels[DockLayout::Browser].visible && !result.panels[DockLayout::Inspector].visible && result.panels[DockLayout::Inspector].tabVisible &&
		Near( result.panels[DockLayout::Inspector].tab.w, 77 ), "shared dock becomes tabs" );
	Expect( layout.Move( DockLayout::Tools, DockLayout::Hidden ) && Near( layout.Compute( { 8, 108, 624, 330 } ).views.w, 624 - 154 - 8 ), "empty right dock gives the views its width" );
	result = layout.Compute( { 8, 108, 624, 330 } );
	layout.Activate( DockLayout::Browser );
	layout.Toggle( DockLayout::Problems );
	layout.Resize( DockLayout::Left, 500 );
	const std::string text = layout.Serialize();
	DockLayout restored;
	Expect( restored.Parse( text ) && restored.Serialize() == text && restored.Size( DockLayout::Left ) == DockLayout::MaxSide, "layout persists and clamps" );
	restored.Toggle( DockLayout::Problems );
	Expect( restored.DockOf( DockLayout::Problems ) == DockLayout::Bottom, "a hidden panel returns to its last dock" );
	Expect( !restored.Parse( "inspector=bottom;" ) && !restored.Parse( "garbage" ) && !restored.Parse( "browser=left;inspector=left;tools=left;" ) &&
		restored.Serialize() != "", "invalid and over-full layouts are rejected without change" );
	ViewState view;
	view.target = Vec3( 1, 2, 3 ); view.yaw = 90; view.mode = 1; view.quad = true; view.grid = 16; view.textureLock = false;
	view.hiddenLayers.insert( "detail" ); view.selection = { "3", "0:12" };
	ViewState parsed;
	Expect( parsed.Parse( view.Serialize() ) && parsed.Serialize() == view.Serialize() && parsed.valid, "view state round trip" );
	Expect( !parsed.Parse( "camera 1 2 nan 0 0 1\n" ) && !parsed.Parse( "mode 2\n" ), "invalid view state rejected" );
	Expect( ViewStatePath( "maps/game/airdefense1.map" ) == "editor_experimental/views/maps/game/airdefense1.map.view", "view state lives in the editor directory" );
	AssetIndex index;
	std::size_t polls = 0;
	index.Begin( {
		{ AssetIndex::Material, [&]( std::size_t i, std::string &name ) { ++polls; if ( i >= 5000 ) { return false; } name = "textures/base/m" + std::to_string( i ); return true; } },
		{ AssetIndex::Sound, []( std::size_t i, std::string &name ) { if ( i >= 3 ) { return false; } name = i == 2 ? "Door_Open" : "door_open"; return true; } } } );
	int steps = 0;
	while ( !index.Step( 256 ) ) { ++steps; Expect( polls <= static_cast<std::size_t>( steps + 1 ) * 256, "each step is bounded" ); }
	Expect( steps >= 19 && index.Count( AssetIndex::Material ) == 5000 && index.Count( AssetIndex::Sound ) == 1, "index completes over many steps and de-duplicates" );
	Expect( index.Search( AssetIndex::Material, "M49", 10 ).size() == 10 && index.Contains( AssetIndex::Sound, "DOOR_OPEN" ), "case-insensitive search and lookup" );
	index.Begin( { { AssetIndex::Model, []( std::size_t i, std::string &name ) { name = std::string( 200, 'x' ) + std::to_string( i ); return true; } } } );
	while ( !index.Step( 100000 ) ) {}
	Expect( index.GetStats().truncated && index.GetStats().bytes <= AssetIndex::MaxBytes, "endless sources stop at the memory bound" );
	index.Begin( { { AssetIndex::Model, []( std::size_t, std::string &name ) { name = "same"; return true; } } } );
	while ( !index.Step( 100000 ) ) {}
	Expect( index.GetStats().truncated && index.Count( AssetIndex::Model ) == 1, "a source repeating itself forever is cut off" );
	index.Begin( { { AssetIndex::Map, []( std::size_t, std::string &name ) { name = "x"; return true; } } } );
	index.Step( 10 );
	index.Cancel();
	Expect( index.Complete() && index.GetStats().cancelled, "indexing can be cancelled" );
}

class WorkspaceFiles : public MapStorage {
public:
	std::map<std::string, std::string> files;
	bool Read( const std::string &path, std::string &text, bool &exists, std::string & ) override {
		const auto it = files.find( path ); exists = it != files.end(); text = exists ? it->second : ""; return true;
	}
	bool WriteAndSync( const std::string &path, const std::string &text, std::string & ) override { files[path] = text; return true; }
	bool Promote( const std::string &from, const std::string &to, std::string & ) override { files[to] = files[from]; files.erase( from ); return true; }
	void Remove( const std::string &path ) override { files.erase( path ); }
};

static void ExternalEdits() {
	WorkspaceFiles files;
	files.files["maps/room.map"] = MapWith( box, "" );
	MapWorkspace workspace( files );
	std::string error;
	bool changed = true;
	Expect( workspace.Open( "room", error ) && workspace.ExternalChange( changed, error ) && !changed, "freshly opened map is unchanged on disk" );
	files.files["maps/room.map"] += "// edited elsewhere\n";
	Expect( workspace.ExternalChange( changed, error ) && changed, "another program's edit is detected while open" );
	MapEditor editor;
	editor.Transform( workspace.Document(), { { workspace.Document().Entities()[0].id, 0 } }, Affine::Translation( Vec3( 8, 0, 0 ) ), true, error );
	Expect( !workspace.Save( "", false, error ) && files.files["maps/room.map"].find( "edited elsewhere" ) != std::string::npos, "saving over an external edit is refused" );
	Expect( workspace.Save( "room_mine", false, error ) && !workspace.Document().IsDirty(), "the work can be saved to a new path" );
	Expect( workspace.ExternalChange( changed, error ) && !changed, "after saving, the new path is the baseline" );
	files.files.erase( "maps/room_mine.map" );
	Expect( workspace.ExternalChange( changed, error ) && changed, "deletion by another program is detected" );
	Expect( workspace.Open( "room", error, true ) && workspace.Document().Serialize().find( "edited elsewhere" ) != std::string::npos, "reload picks up the external version" );
}

static void LargeMapsAndStress() {
	std::string world;
	for ( int i = 0; i < 20000; ++i ) {
		world += BoxBrushSource( Vec3( ( i % 200 ) * 64.0, ( i / 200 ) * 64.0, 0 ), Vec3( ( i % 200 ) * 64.0 + 48, ( i / 200 ) * 64.0 + 48, 32 ), "textures/common/caulk", Vec3(), "\n" ) + "\n";
	}
	std::string entities;
	for ( int i = 0; i < 4000; ++i ) {
		entities += "{\n\"classname\" \"light\"\n\"name\" \"light_" + std::to_string( i ) + "\"\n\"origin\" \"" + std::to_string( i * 8 ) + " 0 64\"\n\"target\" \"light_" + std::to_string( ( i + 1 ) % 4000 ) + "\"\n}\n";
	}
	MapDocument document;
	std::string error;
	const auto t0 = std::chrono::steady_clock::now();
	Expect( document.OpenText( MapWith( world, entities ), error ), "open a 20,000 brush map" );
	const auto t1 = std::chrono::steady_clock::now();
	const std::string original = document.Serialize();
	MapEditor editor;
	Selection selection;
	for ( std::size_t i = 0; i < 5000; ++i ) { selection.push_back( { document.Entities()[0].id, i * 4 } ); }
	for ( std::size_t i = 1; i < 1001; ++i ) { selection.push_back( { document.Entities()[i].id, WholeEntity } ); }
	Expect( editor.Transform( document, selection, Affine::Rotation( 2, 90, Vec3( 6400, 3200, 0 ) ), true, error ), "rotate 5,000 brushes and 1,000 entities" );
	const auto t2 = std::chrono::steady_clock::now();
	Expect( document.HistorySteps() == 1, "large transform is one step" );
	MapGeometryCache cache;
	const std::size_t parsed = cache.Update( document );
	const auto t3 = std::chrono::steady_clock::now();
	Expect( parsed == 20000, "cache parses the large map" );
	Expect( document.Undo( error ) && document.Serialize() == original, "large transform undoes exactly" );
	Expect( cache.Update( document ) == 5000, "cache re-parses only the restored brushes" );
	std::size_t updated = 0;
	Expect( editor.Rename( document, document.Entities()[10].id, "renamed", updated, error ) && updated == 1, "rename with references in a large map" );
	document.Undo( error );
	const auto ms = []( auto a, auto b ) { return std::chrono::duration<double, std::milli>( b - a ).count(); };
	std::printf( "large map: open %.1f ms, transform %.1f ms, cache %.1f ms\n", ms( t0, t1 ), ms( t1, t2 ), ms( t2, t3 ) );
	// Randomized edits and undo/redo, including grouped gestures and
	// cancellation, must return to the exact original source.
	MapDocument small;
	small.OpenText( MapWith( std::string( box ) + "\n" + patch, "{\n\"classname\" \"light\"\n\"name\" \"l\"\n\"origin\" \"0 0 0\"\n}\n" ), error );
	const std::string base = small.Serialize();
	std::mt19937 random( 209 );
	std::size_t operations = 0;
	for ( int round = 0; round < 2000; ++round ) {
		const int op = static_cast<int>( random() % 9 );
		const EntityId world = small.Entities()[0].id;
		const std::size_t primitives = small.Entities()[0].primitiveRanges.size();
		Selection pick = { { world, primitives ? random() % primitives : WholeEntity } };
		if ( op == 0 ) { editor.Transform( small, pick, Affine::Translation( Vec3( static_cast<double>( random() % 64 ) - 32, 8, 0 ) ), random() % 2, error ); }
		else if ( op == 1 ) { editor.Transform( small, pick, Affine::Rotation( random() % 3, 15.0 * ( random() % 24 ), Vec3() ), true, error ); }
		else if ( op == 2 ) { SelectionItem c; editor.CreateBrush( small, world, Vec3( 0, 0, 0 ), Vec3( 16, 16, 16 ), "textures/common/caulk", c, error ); }
		else if ( op == 3 && primitives > 1 ) { editor.Delete( small, pick, error ); }
		else if ( op == 4 ) { small.Undo( error ); }
		else if ( op == 5 ) { small.Redo( error ); }
		else if ( op == 6 ) {
			small.BeginTransaction( "gesture", error );
			for ( int k = 0; k < 3; ++k ) { small.RevertTransaction(); editor.Transform( small, pick, Affine::Translation( Vec3( 0, 0, 4.0 * k ) ), true, error ); }
			if ( random() % 2 ) { small.CommitTransaction( error ); } else { small.CancelTransaction(); }
		} else if ( op == 7 ) { small.SetProperty( small.Entities().back().id, "light_radius", std::to_string( random() % 500 ) + " 1 1", error ); }
		else if ( op == 8 ) { EntityId g; if ( editor.Group( small, pick, g, error ) && random() % 2 ) { editor.Ungroup( small, g, error ); } }
		++operations;
	}
	while ( small.CanUndo() ) { small.Undo( error ); }
	// History is bounded: compare against the oldest retained state instead
	// when the budget evicted early steps.
	if ( small.HistorySteps() < MapDocument::MaxHistoryEntries ) { Expect( small.Serialize() == base, "randomized history undoes to the original bytes" ); }
	while ( small.CanRedo() ) { small.Redo( error ); }
	MapDocument reparsed;
	Expect( reparsed.OpenText( small.Serialize(), error ), "randomized result is still a valid document" );
	std::printf( "randomized operations: %zu\n", operations );
}

static void StockMaps( int argc, char **argv ) {
	std::size_t maps = 0, primitives = 0, brushes = 0, patches = 0, invalid = 0;
	for ( int i = 1; i < argc; ++i ) {
		std::ifstream input( argv[i], std::ios::binary );
		const std::string text( ( std::istreambuf_iterator<char>( input ) ), std::istreambuf_iterator<char>() );
		MapDocument document;
		std::string error;
		if ( !document.OpenText( text, error ) ) { Expect( false, "stock map loads" ); continue; }
		++maps;
		bool identical = true, valid = true;
		MapEditor editor;
		for ( const auto &entity : document.Entities() ) {
			for ( std::size_t p = 0; p < entity.primitiveRanges.size(); ++p ) {
				Primitive primitive;
				const std::string source = document.PrimitiveText( entity.id, p );
				if ( !primitive.Parse( source, error ) ) {
					if ( valid ) { std::fprintf( stderr, "%s entity %zu primitive %zu: %s\n", argv[i], static_cast<std::size_t>( entity.id ), p, error.c_str() ); }
					valid = false; continue;
				}
				++primitives;
				brushes += primitive.GetKind() == Primitive::Kind::Brush;
				patches += primitive.GetKind() == Primitive::Kind::Patch;
				if ( primitive.Serialize() != source ) { identical = false; }
				// A locked translation and its inverse succeed on every valid
				// shape. Degenerate retail brushes are reported, not moved.
				std::string problem;
				if ( !primitive.Validate( problem ) ) {
					++invalid;
					std::printf( "invalid retail primitive: %s entity %zu primitive %zu: %s\n", argv[i], static_cast<std::size_t>( entity.id ), p, problem.c_str() );
					continue;
				}
				Primitive moved = primitive;
				if ( primitive.GetKind() != Primitive::Kind::Unsupported &&
					( !moved.Transform( Affine::Translation( Vec3( 64, -32, 16 ) ), Vec3(), Vec3(), true, error ) ||
						!moved.Transform( Affine::Translation( Vec3( -64, 32, -16 ) ), Vec3(), Vec3(), true, error ) ) ) {
					if ( valid ) { std::fprintf( stderr, "%s entity %zu primitive %zu: %s\n", argv[i], static_cast<std::size_t>( entity.id ), p, error.c_str() ); }
					valid = false;
				}
			}
		}
		Expect( identical, "every stock primitive serializes byte for byte" );
		Expect( valid, "every stock primitive parses and transforms" );
		// Whole-map locked round trip through a gesture leaves the bytes.
		Selection all;
		for ( std::size_t p = 0; p < document.Entities()[0].primitiveRanges.size(); ++p ) { all.push_back( { document.Entities()[0].id, p } ); }
		const std::string original = document.Serialize();
		document.BeginTransaction( "stock", error );
		editor.Transform( document, all, Affine::Translation( Vec3( 128, 0, 0 ) ), true, error );
		document.CancelTransaction();
		Expect( document.Serialize() == original, "cancelled whole-map move restores a stock map" );
	}
	if ( maps ) { std::printf( "stock maps: %zu, primitives: %zu (brushes %zu, patches %zu, invalid %zu)\n", maps, primitives, brushes, patches, invalid ); }
}

int main( int argc, char **argv ) {
	const auto timed = []( const char *name, void ( *section )() ) {
		const auto start = std::chrono::steady_clock::now();
		section();
		std::printf( "section %s: %.1f ms\n", name, std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - start ).count() );
		std::fflush( stdout );
	};
	timed( "geometry", Geometry );
	timed( "transactions", Transactions );
	timed( "editing", Editing );
	timed( "validation", Validation );
	timed( "builds", Builds );
	timed( "layout", Layout );
	timed( "external", ExternalEdits );
	timed( "large", LargeMapsAndStress );
	StockMaps( argc, argv );
	std::printf( "editor authoring: %d checks, %d failures\n", checks, failures );
	return failures == 0 ? 0 : 1;
}
