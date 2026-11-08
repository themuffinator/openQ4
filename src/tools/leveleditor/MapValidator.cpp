// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapValidator.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>

namespace oq4editor {

namespace {

std::string Lower( std::string text ) {
	for ( char &c : text ) { c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) ); }
	return text;
}

bool Numbers( const std::string &text, int count, double *values = nullptr ) {
	const char *scan = text.c_str();
	for ( int i = 0; i < count; ++i ) {
		char *end = nullptr;
		const double value = std::strtod( scan, &end );
		if ( end == scan || !std::isfinite( value ) ) { return false; }
		if ( values ) { values[i] = value; }
		scan = end;
	}
	while ( *scan == ' ' || *scan == '\t' ) { ++scan; }
	return *scan == '\0';
}

// Reads "<label> <number>" anywhere in a line, e.g. "entity 12".
bool FindNumber( const std::string &line, const char *label, int &value ) {
	const std::string lower = Lower( line );
	std::size_t at = 0;
	const std::string needle = std::string( label ) + " ";
	while ( ( at = lower.find( needle, at ) ) != std::string::npos ) {
		const bool boundary = at == 0 || !std::isalnum( static_cast<unsigned char>( lower[at - 1] ) );
		const char *digits = lower.c_str() + at + needle.size();
		if ( boundary && std::isdigit( static_cast<unsigned char>( *digits ) ) ) { value = std::atoi( digits ); return true; }
		at += needle.size();
	}
	return false;
}

}

const char *TypeName( PropertyType type ) {
	switch ( type ) {
		case PropertyType::Bool: return "bool";
		case PropertyType::Int: return "int";
		case PropertyType::Float: return "float";
		case PropertyType::Vector: return "vector";
		case PropertyType::Color: return "color";
		case PropertyType::Angle: return "angle";
		case PropertyType::Matrix: return "matrix";
		case PropertyType::Model: return "model";
		case PropertyType::Material: return "material";
		case PropertyType::Sound: return "sound";
		case PropertyType::Gui: return "gui";
		case PropertyType::Entity: return "entity";
		default: return "string";
	}
}

const char *SeverityName( Diagnostic::Severity severity ) {
	return severity == Diagnostic::Severity::Error ? "error" : severity == Diagnostic::Severity::Warning ? "warning" : "info";
}

bool TypeFromEditorKey( const std::string &editorType, PropertyType &type ) {
	const std::string t = Lower( editorType );
	if ( t == "var" ) { type = PropertyType::String; }
	else if ( t == "bool" ) { type = PropertyType::Bool; }
	else if ( t == "int" ) { type = PropertyType::Int; }
	else if ( t == "float" ) { type = PropertyType::Float; }
	else if ( t == "vector" ) { type = PropertyType::Vector; }
	else if ( t == "color" ) { type = PropertyType::Color; }
	else if ( t == "model" ) { type = PropertyType::Model; }
	else if ( t == "mat" || t == "material" ) { type = PropertyType::Material; }
	else if ( t == "snd" ) { type = PropertyType::Sound; }
	else if ( t == "gui" ) { type = PropertyType::Gui; }
	else { return false; }
	return true;
}

bool ImplicitType( const std::string &keyInput, PropertyType &type ) {
	const std::string key = Lower( keyInput );
	if ( key == "origin" || key == "light_radius" || key == "light_center" || key == "light_target" || key == "light_up" ||
		key == "light_right" || key == "light_start" || key == "light_end" || key == "editor_mins" || key == "editor_maxs" ) { type = PropertyType::Vector; }
	else if ( key == "_color" || key == "editor_color" ) { type = PropertyType::Color; }
	else if ( key == "angle" ) { type = PropertyType::Angle; }
	else if ( key == "rotation" ) { type = PropertyType::Matrix; }
	else if ( key == "noshadows" || key == "nospecular" || key == "nodiffuse" || key == "start_off" || key == "spawnclass_disabled" ) { type = PropertyType::Bool; }
	else if ( IsReferenceKey( key ) ) { type = PropertyType::Entity; }
	else if ( key.compare( 0, 4, "snd_" ) == 0 || key == "s_shader" ) { type = PropertyType::Sound; }
	else if ( key == "texture" ) { type = PropertyType::Material; }
	else if ( key == "model" ) { type = PropertyType::Model; }
	else if ( key == "gui" || ( key.compare( 0, 3, "gui" ) == 0 && key.size() == 4 && std::isdigit( static_cast<unsigned char>( key[3] ) ) ) ) { type = PropertyType::Gui; }
	else { return false; }
	return true;
}

bool ValidateValue( PropertyType type, const std::string &value, std::string &problem ) {
	problem.clear();
	switch ( type ) {
		case PropertyType::Bool:
			if ( value != "0" && value != "1" ) { problem = "expected 0 or 1"; return false; }
			return true;
		case PropertyType::Int: {
			if ( value.empty() ) { problem = "expected an integer"; return false; }
			char *end = nullptr;
			std::strtol( value.c_str(), &end, 10 );
			if ( *end ) { problem = "expected an integer"; return false; }
			return true;
		}
		case PropertyType::Float:
		case PropertyType::Angle:
			if ( !Numbers( value, 1 ) ) { problem = "expected a number"; return false; }
			return true;
		case PropertyType::Vector:
			if ( !Numbers( value, 3 ) ) { problem = "expected three numbers"; return false; }
			return true;
		case PropertyType::Color: {
			double c[3];
			if ( !Numbers( value, 3, c ) ) { problem = "expected three numbers from 0 to 1"; return false; }
			for ( double v : c ) { if ( v < 0 || v > 1 ) { problem = "colour components range from 0 to 1"; return false; } }
			return true;
		}
		case PropertyType::Matrix:
			if ( !Numbers( value, 9 ) ) { problem = "expected nine numbers"; return false; }
			return true;
		case PropertyType::Model: case PropertyType::Material: case PropertyType::Sound: case PropertyType::Gui: case PropertyType::Entity:
			if ( value.find( ' ' ) != std::string::npos && type != PropertyType::Sound ) { problem = "names cannot contain spaces"; return false; }
			return true;
		default:
			return true;
	}
}

const PropertySpec *ClassSchema::Find( const std::string &key ) const {
	for ( const auto &spec : properties ) { if ( EqualNoCase( spec.key, key ) ) { return &spec; } }
	return nullptr;
}

bool PropertyTypeFor( const ValidatorOptions &options, const Entity &entity, const std::string &key, PropertyType &type ) {
	if ( options.schema ) {
		const ClassSchema *schema = options.schema( entity.Value( "classname" ) );
		const PropertySpec *spec = schema ? schema->Find( key ) : nullptr;
		if ( spec ) { type = spec->type; return true; }
	}
	return ImplicitType( key, type );
}

std::vector<Diagnostic> ValidateMap( const MapDocument &document, const MapGeometryCache *geometry, const ValidatorOptions &options ) {
	std::vector<Diagnostic> result;
	const auto add = [&]( Diagnostic::Severity severity, EntityId id, std::size_t primitive, const char *code, const std::string &message ) {
		if ( result.size() < options.limit ) { result.push_back( { severity, id, primitive, code, message } ); }
	};
	if ( !document.IsOpen() ) { return result; }
	std::map<std::string, EntityId> names;
	for ( const auto &entity : document.Entities() ) {
		const std::string name = Lower( entity.Value( "name" ) );
		if ( name.empty() ) { continue; }
		if ( names.count( name ) ) { add( Diagnostic::Severity::Error, entity.id, WholeEntity, "duplicate-name", "name '" + entity.Value( "name" ) + "' is already used" ); }
		else { names[name] = entity.id; }
	}
	bool playerStart = false;
	for ( const auto &entity : document.Entities() ) {
		const std::string classname = entity.Value( "classname" );
		if ( Lower( classname ).compare( 0, 11, "info_player" ) == 0 ) { playerStart = true; }
		const ClassSchema *schema = options.schema ? options.schema( classname ) : nullptr;
		const bool world = IsWorld( document, entity );
		if ( !world && options.schema && ( !schema || !schema->known ) && !EqualNoCase( classname, "func_group" ) ) {
			add( Diagnostic::Severity::Error, entity.id, WholeEntity, "unknown-class", "no entityDef named '" + classname + "'" );
		}
		std::map<std::string, bool> seen;
		for ( auto it = entity.properties.rbegin(); it != entity.properties.rend(); ++it ) {
			const std::string key = Lower( it->key );
			if ( seen.count( key ) ) { continue; } // only the effective (last) value matters
			seen[key] = true;
			if ( key == "origin" ) { continue; } // checked below with its own code
			PropertyType type;
			if ( !PropertyTypeFor( options, entity, it->key, type ) ) { continue; }
			std::string problem;
			if ( !ValidateValue( type, it->value, problem ) ) {
				add( Diagnostic::Severity::Warning, entity.id, WholeEntity, "invalid-value", it->key + " (" + TypeName( type ) + "): " + problem );
				continue;
			}
			if ( type == PropertyType::Entity && !it->value.empty() && !names.count( Lower( it->value ) ) ) {
				add( Diagnostic::Severity::Warning, entity.id, WholeEntity, "missing-target", it->key + " names missing entity '" + it->value + "'" );
			}
			if ( options.assetExists && !it->value.empty() &&
				( type == PropertyType::Model || type == PropertyType::Material || type == PropertyType::Sound || type == PropertyType::Gui ) &&
				// Brush entities name their own inline model after themselves.
				!( type == PropertyType::Model && EqualNoCase( it->value, entity.Value( "name" ) ) ) &&
				!options.assetExists( type, it->value ) ) {
				add( Diagnostic::Severity::Warning, entity.id, WholeEntity, "missing-asset", it->key + " names missing " + TypeName( type ) + " '" + it->value + "'" );
			}
		}
		if ( !world && !entity.Value( "origin" ).empty() ) {
			Vec3 origin;
			if ( !ParseVector( entity.Value( "origin" ), origin ) ) { add( Diagnostic::Severity::Error, entity.id, WholeEntity, "invalid-origin", "origin is not three numbers" ); }
		}
		const auto *cached = geometry ? geometry->Find( entity.id ) : nullptr;
		for ( std::size_t i = 0; i < entity.primitiveRanges.size(); ++i ) {
			Primitive parsed;
			const Primitive *primitive = nullptr;
			std::string problem;
			if ( cached && i < cached->primitives.size() && cached->primitives[i].parsed ) {
				primitive = &cached->primitives[i].primitive;
			} else if ( parsed.Parse( document.PrimitiveText( entity.id, i ), problem ) ) {
				primitive = &parsed;
			} else {
				add( Diagnostic::Severity::Error, entity.id, i, "invalid-primitive", problem );
				continue;
			}
			if ( primitive->GetKind() == Primitive::Kind::Unsupported ) {
				add( Diagnostic::Severity::Info, entity.id, i, "legacy-primitive", "legacy '" + primitive->Header() + "' primitive is kept but not editable" );
			} else if ( !primitive->Validate( problem ) ) {
				add( Diagnostic::Severity::Error, entity.id, i, "invalid-geometry", problem );
			}
		}
	}
	if ( options.requirePlayerStart && !playerStart && !document.Entities().empty() ) {
		add( Diagnostic::Severity::Warning, document.Entities().front().id, WholeEntity, "no-player-start", "map has no info_player_* spawn point" );
	}
	return result;
}

void CompileIndex::Build( const MapDocument &document ) {
	entities.clear(); world.clear(); worldOwners.clear();
	if ( document.Entities().empty() ) { return; }
	const Entity &worldspawn = document.Entities().front();
	std::vector<std::string> removed;
	for ( const auto &property : worldspawn.properties ) {
		if ( Lower( property.key ).compare( 0, 14, "removeentities" ) == 0 ) { removed.push_back( property.value ); }
	}
	for ( std::size_t i = 0; i < worldspawn.primitiveRanges.size(); ++i ) { world.push_back( { worldspawn.id, i } ); }
	for ( const auto &entity : document.Entities() ) {
		const std::string classname = entity.Value( "classname" );
		bool drop = false;
		for ( const auto &cls : removed ) { if ( EqualNoCase( cls, classname ) ) { drop = true; } }
		if ( drop ) { continue; }
		if ( entity.id != worldspawn.id && EqualNoCase( classname, "func_group" ) ) {
			for ( std::size_t i = 0; i < entity.primitiveRanges.size(); ++i ) { world.push_back( { entity.id, i } ); }
			continue;
		}
		entities.push_back( entity.id );
	}
}

bool CompileIndex::EntityAt( int compiledEntity, EntityId &id ) const {
	if ( compiledEntity < 0 || static_cast<std::size_t>( compiledEntity ) >= entities.size() ) { return false; }
	id = entities[compiledEntity];
	return true;
}

bool CompileIndex::PrimitiveAt( int compiledEntity, int compiledPrimitive, SelectionItem &item ) const {
	if ( compiledPrimitive < 0 ) { return false; }
	if ( compiledEntity == 0 ) {
		if ( static_cast<std::size_t>( compiledPrimitive ) >= world.size() ) { return false; }
		item = world[compiledPrimitive];
		return true;
	}
	EntityId id;
	if ( !EntityAt( compiledEntity, id ) ) { return false; }
	item = { id, static_cast<std::size_t>( compiledPrimitive ) };
	return true;
}

bool ParseCompilerLine( const std::string &lineInput, const CompileIndex &index, Diagnostic &diagnostic ) {
	std::string line = lineInput;
	while ( !line.empty() && ( line.back() == '\r' || line.back() == '\n' || line.back() == ' ' ) ) { line.pop_back(); }
	const std::string lower = Lower( line );
	diagnostic = Diagnostic();
	if ( lower.find( "******* leaked *******" ) != std::string::npos || lower.find( "reached outside from entity" ) != std::string::npos ) {
		diagnostic.severity = Diagnostic::Severity::Error;
		diagnostic.code = "leak";
	} else if ( lower.compare( 0, 6, "error:" ) == 0 || lower.find( "fatal error" ) != std::string::npos || lower.compare( 0, 5, "error" ) == 0 ) {
		diagnostic.severity = Diagnostic::Severity::Error;
		diagnostic.code = "compiler-error";
	} else if ( lower.compare( 0, 8, "warning:" ) == 0 ) {
		diagnostic.severity = Diagnostic::Severity::Warning;
		diagnostic.code = "compiler-warning";
	} else {
		return false;
	}
	diagnostic.message = line;
	int entity = -1, primitive = -1;
	const bool hasEntity = FindNumber( line, "entity", entity );
	const bool hasPrimitive = FindNumber( line, "primitive", primitive ) || FindNumber( line, "brush", primitive );
	if ( hasEntity && hasPrimitive ) {
		SelectionItem item;
		if ( index.PrimitiveAt( entity, primitive, item ) ) { diagnostic.entity = item.entity; diagnostic.primitive = item.primitive; }
	} else if ( hasEntity ) {
		EntityId id;
		if ( index.EntityAt( entity, id ) ) { diagnostic.entity = id; }
	}
	return true;
}

}
