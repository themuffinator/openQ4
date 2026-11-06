// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENQ4_EDITOR_MAP_VALIDATOR_H
#define OPENQ4_EDITOR_MAP_VALIDATOR_H

#include "MapEditing.h"

#include <functional>

namespace oq4editor {

// Property types come from entityDef "editor_<type> <key>" declarations
// (editor_var, editor_bool, editor_float, editor_color, editor_model,
// editor_snd, editor_mat/editor_material, editor_gui) plus well-known keys.
enum class PropertyType { String, Bool, Int, Float, Vector, Color, Angle, Matrix, Model, Material, Sound, Gui, Entity };
const char *TypeName( PropertyType type );
bool TypeFromEditorKey( const std::string &editorType, PropertyType &type );
// Returns false when the key has no implicit type.
bool ImplicitType( const std::string &key, PropertyType &type );
bool ValidateValue( PropertyType type, const std::string &value, std::string &problem );

struct PropertySpec {
	std::string key;
	PropertyType type = PropertyType::String;
	std::string description;
};

struct ClassSchema {
	bool known = false;
	std::string usage;
	std::vector<PropertySpec> properties;
	const PropertySpec *Find( const std::string &key ) const;
};

struct Diagnostic {
	enum class Severity { Info, Warning, Error };
	Severity severity = Severity::Warning;
	EntityId entity = 0;
	std::size_t primitive = WholeEntity;
	std::string code, message;
};
const char *SeverityName( Diagnostic::Severity severity );

struct ValidatorOptions {
	std::function<const ClassSchema *( const std::string &classname )> schema;
	// type is Model, Material, Sound or Gui; return false for a missing asset.
	std::function<bool( PropertyType type, const std::string &name )> assetExists;
	bool requirePlayerStart = true;
	std::size_t limit = 4096;
};

// Resolves the type of one key on one entity: entityDef schema first.
bool PropertyTypeFor( const ValidatorOptions &options, const Entity &entity, const std::string &key, PropertyType &type );

std::vector<Diagnostic> ValidateMap( const MapDocument &document, const MapGeometryCache *geometry, const ValidatorOptions &options );

// dmap and AAS address entities and primitives after idMapFile::Resolve:
// func_group primitives join worldspawn and removeEntities classes vanish.
class CompileIndex {
public:
	void Build( const MapDocument &document );
	bool EntityAt( int compiledEntity, EntityId &id ) const;
	bool PrimitiveAt( int compiledEntity, int compiledPrimitive, SelectionItem &item ) const;
	std::size_t Entities() const { return entities.size(); }
private:
	std::vector<EntityId> entities;
	std::vector<SelectionItem> world;
	std::vector<EntityId> worldOwners;
};

// Turns compiler log lines into source-linked diagnostics.
bool ParseCompilerLine( const std::string &line, const CompileIndex &index, Diagnostic &diagnostic );

}
#endif
