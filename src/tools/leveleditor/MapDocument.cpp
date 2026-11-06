// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapDocument.h"

#include <algorithm>
#include <utility>

namespace oq4editor {
namespace {

char Fold( char c ) { return c >= 'A' && c <= 'Z' ? c + ( 'a' - 'A' ) : c; }
bool Space( char c ) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::string TrimEnd( std::string value ) {
	while ( !value.empty() && static_cast<unsigned char>( value.back() ) <= ' ' ) { value.pop_back(); }
	return value;
}

struct Token {
	std::size_t begin = 0, end = 0;
	std::string text;
	bool quoted = false;
};

// Quake map strings use literal backslashes (LEXFL_NOSTRINGESCAPECHARS).
// Comments and strings must not contribute braces to structural scanning.
class Scanner {
public:
	explicit Scanner( const std::string &text ) : source( text ) {}
	bool SameLine( std::size_t begin, std::size_t end ) const {
		return source.find_first_of( "\r\n", begin ) >= end;
	}
	bool Next( Token &token ) {
		while ( position < source.size() ) {
			if ( Space( source[position] ) ) { ++position; continue; }
			if ( source.compare( position, 2, "//" ) == 0 ) {
				position = source.find( '\n', position );
				if ( position == std::string::npos ) { position = source.size(); }
				continue;
			}
			if ( source.compare( position, 2, "/*" ) == 0 ) {
				const auto end = source.find( "*/", position + 2 );
				if ( end == std::string::npos ) { error = "unterminated comment"; return false; }
				position = end + 2;
				continue;
			}
			break;
		}
		if ( position == source.size() ) { return false; }
		token = Token();
		token.begin = position;
		const char first = source[position++];
		if ( first == '"' ) {
			token.quoted = true;
			const auto end = source.find( '"', position );
			if ( end == std::string::npos ) { error = "unterminated string"; return false; }
			token.text = source.substr( position, end - position );
			if ( token.text.find_first_of( "\r\n" ) != std::string::npos ) {
				error = "newline inside a map string"; return false;
			}
			position = end + 1;
		} else if ( first == '{' || first == '}' || first == '(' || first == ')' ) {
			token.text.assign( 1, first );
		} else {
			while ( position < source.size() && !Space( source[position] ) &&
				source[position] != '{' && source[position] != '}' && source[position] != '(' &&
				source[position] != ')' && source[position] != '"' &&
				source.compare( position, 2, "//" ) != 0 && source.compare( position, 2, "/*" ) != 0 ) { ++position; }
			token.text = source.substr( token.begin, position - token.begin );
		}
		token.end = position;
		return true;
	}
	std::size_t position = 0;
	std::string error;
private:
	const std::string &source;
};

bool Is( const Token &token, const char *text ) { return !token.quoted && token.text == text; }

bool ReadEntity( Scanner &scanner, std::size_t start, Entity &entity, std::string &error,
	const std::vector<std::pair<std::size_t, std::size_t>> *knownPrimitives = nullptr ) {
	Token token;
	bool geometrySeen = false;
	while ( scanner.Next( token ) ) {
		if ( Is( token, "}" ) ) {
			if ( !geometrySeen ) { entity.propertyInsert = token.begin - start; }
			return true;
		}
		if ( Is( token, "{" ) ) {
			if ( !geometrySeen ) { entity.propertyInsert = token.begin - start; }
			geometrySeen = true;
			const auto primitiveStart = token.begin - start;
			// Property edits cannot alter primitive bytes. Reuse validated source
			// ranges so editing a worldspawn field does not re-lex the whole level.
			if ( knownPrimitives && entity.primitiveCount < knownPrimitives->size() &&
				( *knownPrimitives )[entity.primitiveCount].first == primitiveStart ) {
				const auto range = ( *knownPrimitives )[entity.primitiveCount++];
				scanner.position = start + range.second;
				entity.primitiveRanges.push_back( range );
				continue;
			}
			++entity.primitiveCount;
			std::vector<char> nesting( 1, '{' );
			while ( !nesting.empty() && scanner.Next( token ) ) {
				if ( token.quoted ) { continue; }
				if ( token.text == "{" || token.text == "(" ) {
					if ( nesting.size() >= 64 ) { error = "primitive nesting exceeds 64 levels"; return false; }
					nesting.push_back( token.text[0] );
				} else if ( token.text == "}" || token.text == ")" ) {
					const char expected = token.text == "}" ? '{' : '(';
					if ( nesting.back() != expected ) { error = "mismatched primitive delimiter"; return false; }
					nesting.pop_back();
				}
			}
			if ( !nesting.empty() ) { error = "unterminated primitive"; return false; }
			entity.primitiveRanges.emplace_back( primitiveStart, scanner.position - start );
			continue;
		}
		if ( !token.quoted || token.text.empty() ) { error = "expected a quoted entity key"; return false; }
		Property property;
		property.key = token.text;
		property.begin = token.begin - start;
		const auto keyEnd = token.end;
		if ( !scanner.Next( token ) || !token.quoted || !scanner.SameLine( keyEnd, token.begin ) ) {
			error = "expected a quoted entity value on the same line"; return false;
		}
		property.value = token.text;
		// The engine trims trailing whitespace from both entity keys and values.
		// Keep the original bytes in source, but expose the same effective values.
		property.key = TrimEnd( std::move( property.key ) );
		property.value = TrimEnd( std::move( property.value ) );
		if ( property.key.empty() ) { error = "empty entity key"; return false; }
		property.valueBegin = token.begin - start;
		property.end = token.end - start;
		entity.properties.push_back( std::move( property ) );
		if ( entity.properties.size() > 16384 ) { error = "entity has too many properties"; return false; }
	}
	error = scanner.error.empty() ? "unterminated entity" : scanner.error;
	return false;
}

void Refresh( Entity &entity, bool reuseGeometry = false ) {
	const auto knownPrimitives = reuseGeometry ? entity.primitiveRanges : std::vector<std::pair<std::size_t, std::size_t>>();
	entity.properties.clear();
	entity.primitiveRanges.clear();
	entity.primitiveCount = 0;
	Scanner scanner( entity.source );
	Token token;
	std::string error;
	scanner.Next( token );
	ReadEntity( scanner, 0, entity, error, &knownPrimitives ); // only validated edits reach here
}

bool ValidString( const std::string &value ) {
	return value.size() <= 4096 && value.find_first_of( "\"\r\n" ) == std::string::npos &&
		value.find( '\0' ) == std::string::npos;
}

}

bool EqualNoCase( const std::string &a, const std::string &b ) {
	return a.size() == b.size() && std::equal( a.begin(), a.end(), b.begin(),
		[]( char x, char y ) { return Fold( x ) == Fold( y ); } );
}

std::string Entity::Value( const std::string &key ) const {
	for ( auto it = properties.rbegin(); it != properties.rend(); ++it ) {
		if ( EqualNoCase( it->key, key ) ) { return it->value; }
	}
	return {};
}

void MapDocument::ResetHistory() {
	history.clear(); cursor = historyBytes = 0;
	pending = Step(); transactionDepth = 0;
}

bool MapDocument::OpenText( const std::string &source, std::string &error ) {
	error.clear();
	if ( source.size() > MaxSourceBytes || source.find( '\0' ) != std::string::npos ) {
		error = "map exceeds 64 MiB or contains an embedded NUL"; return false;
	}
	Scanner scanner( source );
	if ( source.compare( 0, 3, "\xEF\xBB\xBF" ) == 0 ) { scanner.position = 3; }
	Token token;
	if ( !scanner.Next( token ) ) { error = "empty map"; return false; }
	int newVersion = 1;
	if ( Is( token, "Version" ) ) {
		if ( !scanner.Next( token ) || token.quoted || ( token.text != "1" && token.text != "2" && token.text != "3" ) ) {
			error = "supported map versions are 1, 2 and 3"; return false;
		}
		newVersion = token.text[0] - '0';
		if ( !scanner.Next( token ) ) { error = "map has no worldspawn"; return false; }
	}
	std::vector<Entity> candidate;
	const std::string newHeader = source.substr( 0, token.begin );
	std::size_t previousEnd = token.begin;
	do {
		if ( !Is( token, "{" ) ) { error = "expected an entity opening brace"; break; }
		Entity entity;
		const auto start = token.begin;
		entity.prefix = source.substr( previousEnd, start - previousEnd );
		if ( !ReadEntity( scanner, start, entity, error ) ) { break; }
		entity.source = source.substr( start, scanner.position - start );
		previousEnd = scanner.position;
		const std::string classname = entity.Value( "classname" );
		if ( classname.empty() || ( candidate.empty() != EqualNoCase( classname, "worldspawn" ) ) ) {
			error = "map must start with exactly one worldspawn; every entity needs a classname"; break;
		}
		candidate.push_back( std::move( entity ) );
		if ( candidate.size() > 65536 ) { error = "map has too many entities"; break; }
	} while ( scanner.Next( token ) );
	if ( !scanner.error.empty() ) { error = scanner.error; }
	if ( !error.empty() ) {
		error += " near line " + std::to_string( 1 + std::count( source.begin(), source.begin() + scanner.position, '\n' ) );
		return false;
	}
	// Publish only after the entire input has passed structural validation.
	for ( Entity &entity : candidate ) { entity.id = nextId++; }
	entities = std::move( candidate );
	header = newHeader;
	tail = source.substr( previousEnd );
	sourceBytes = source.size();
	ResetHistory();
	open = true;
	version = newVersion;
	revision = savedRevision = nextRevision++;
	return true;
}

void MapDocument::New() {
	std::string error;
	OpenText( "Version 3\n{\n\"classname\" \"worldspawn\"\n}\n", error );
	savedRevision = 0;
}

void MapDocument::Close() {
	entities.clear(); header.clear(); tail.clear(); ResetHistory();
	open = false; sourceBytes = 0; revision = savedRevision = 0;
}

std::size_t MapDocument::IndexOf( EntityId id ) const {
	for ( std::size_t i = 0; i < entities.size(); ++i ) { if ( entities[i].id == id ) { return i; } }
	return entities.size();
}

const Entity *MapDocument::Find( EntityId id ) const {
	const auto index = IndexOf( id );
	return index == entities.size() ? nullptr : &entities[index];
}

std::string MapDocument::Serialize() const {
	if ( !open ) { return {}; }
	std::string text;
	text.reserve( sourceBytes );
	text += header;
	for ( const Entity &entity : entities ) { text += entity.prefix; text += entity.source; }
	text += tail;
	return text;
}

bool MapDocument::NameAvailable( const std::string &name, EntityId except ) const {
	if ( name.empty() ) { return true; }
	for ( const Entity &entity : entities ) {
		if ( entity.id != except && EqualNoCase( entity.Value( "name" ), name ) ) { return false; }
	}
	return true;
}

bool MapDocument::CheckProperty( const Entity &entity, const std::string &key, const std::string &value, std::string &error ) const {
	if ( key.empty() || !ValidString( key ) || !ValidString( value ) ) {
		error = "keys/values must be at most 4096 bytes and cannot contain quotes, newlines or NULs"; return false;
	}
	if ( EqualNoCase( key, "classname" ) && ( value.empty() ||
		( entity.id == entities.front().id ) != EqualNoCase( value, "worldspawn" ) ) ) {
		error = "cannot remove a classname or change worldspawn ownership"; return false;
	}
	if ( EqualNoCase( key, "name" ) && !NameAvailable( value, entity.id ) ) {
		error = "another entity already uses this name"; return false;
	}
	if ( !allowGeometryOrigin && entity.primitiveCount != 0 && EqualNoCase( key, "origin" ) && value != entity.Value( key ) ) {
		error = "geometry entity origins require a geometry transform tool"; return false;
	}
	return true;
}

bool MapDocument::SetProperty( EntityId id, const std::string &keyInput, const std::string &valueInput, std::string &error ) {
	error.clear();
	if ( !ValidString( keyInput ) || !ValidString( valueInput ) ) {
		error = "keys/values must be at most 4096 bytes and cannot contain quotes, newlines or NULs"; return false;
	}
	const std::string key = TrimEnd( keyInput ), value = TrimEnd( valueInput );
	const Entity *entity = Find( id );
	if ( !entity ) { error = "unknown entity handle"; return false; }
	if ( !CheckProperty( *entity, key, value, error ) ) { return false; }
	Change change;
	change.id = id;
	for ( auto it = entity->properties.rbegin(); it != entity->properties.rend(); ++it ) {
		if ( EqualNoCase( it->key, key ) ) {
			if ( it->value == value ) { return true; }
			change.offset = it->valueBegin;
			change.before = entity->source.substr( it->valueBegin, it->end - it->valueBegin );
			change.after = "\"" + value + "\"";
			return Commit( std::move( change ), error );
		}
	}
	if ( entity->properties.size() >= 16384 ) { error = "entity has too many properties"; return false; }
	const char *newline = entity->source.find( "\r\n" ) != std::string::npos ? "\r\n" : "\n";
	change.offset = entity->propertyInsert;
	change.after = "\"" + key + "\" \"" + value + "\"" + newline;
	return Commit( std::move( change ), error );
}

bool MapDocument::RemoveProperty( EntityId id, const std::string &keyInput, std::string &error ) {
	error.clear();
	const std::string key = TrimEnd( keyInput );
	const Entity *entity = Find( id );
	if ( !entity ) { error = "unknown entity handle"; return false; }
	if ( EqualNoCase( key, "classname" ) || ( entity->primitiveCount && EqualNoCase( key, "origin" ) ) ) {
		error = "cannot remove classname or a geometry entity origin"; return false;
	}
	// Removing duplicate keys must not reveal an older shadowed value. One
	// replacement range makes all occurrences a single undoable transaction.
	std::size_t begin = entity->source.size(), end = 0;
	for ( const Property &property : entity->properties ) {
		if ( EqualNoCase( property.key, key ) ) { begin = ( std::min )( begin, property.begin ); end = property.end; }
	}
	if ( end == 0 ) { return true; }
	Change change;
	change.id = id; change.offset = begin;
	change.before = entity->source.substr( begin, end - begin );
	change.after = change.before;
	for ( auto it = entity->properties.rbegin(); it != entity->properties.rend(); ++it ) {
		if ( EqualNoCase( it->key, key ) ) { change.after.erase( it->begin - begin, it->end - it->begin ); }
	}
	return Commit( std::move( change ), error );
}

bool MapDocument::AddEntity( const std::string &classnameInput, const std::string &nameInput, EntityId &id, std::string &error ) {
	error.clear();
	const std::string classname = TrimEnd( classnameInput ), name = TrimEnd( nameInput );
	if ( !open ) { error = "no open document"; return false; }
	if ( classname.empty() || name.empty() || !ValidString( classnameInput ) || !ValidString( nameInput ) || EqualNoCase( classname, "worldspawn" ) ) {
		error = "a point entity needs a valid classname and unique nonempty name"; return false;
	}
	if ( !NameAvailable( name, 0 ) ) { error = "another entity already uses this name"; return false; }
	if ( entities.size() >= 65536 ) { error = "map has too many entities"; return false; }
	Change change;
	change.kind = ChangeKind::Insert;
	change.id = nextId++; change.index = entities.size(); change.prefix = "\n";
	change.after = "{\n\"classname\" \"" + classname + "\"\n\"name\" \"" + name + "\"\n\"origin\" \"0 0 0\"\n}";
	const EntityId newId = change.id;
	if ( !Commit( std::move( change ), error ) ) { return false; }
	id = newId;
	return true;
}

bool MapDocument::DeleteEntity( EntityId id, std::string &error ) {
	error.clear();
	const auto index = IndexOf( id );
	if ( index == entities.size() ) { error = "unknown entity handle"; return false; }
	if ( index == 0 ) { error = "cannot delete worldspawn"; return false; }
	Change change;
	change.kind = ChangeKind::Erase; change.id = id; change.index = index;
	change.before = entities[index].source; change.prefix = entities[index].prefix;
	return Commit( std::move( change ), error );
}

namespace {

// One balanced "{ ... }" block with nothing after it. Strings and comments
// do not contribute delimiters, exactly as for loaded source.
bool ValidPrimitiveText( const std::string &text, std::string &error ) {
	if ( text.size() > MapDocument::MaxHistoryBytes || text.find( '\0' ) != std::string::npos ) {
		error = "primitive text exceeds the edit budget or contains a NUL"; return false;
	}
	Scanner scanner( text );
	Token token;
	if ( !scanner.Next( token ) || !Is( token, "{" ) || token.begin != 0 ) { error = "primitive text must start with '{'"; return false; }
	std::vector<char> nesting( 1, '{' );
	while ( !nesting.empty() && scanner.Next( token ) ) {
		if ( token.quoted ) { continue; }
		if ( token.text == "{" || token.text == "(" ) {
			if ( nesting.size() >= 64 ) { error = "primitive nesting exceeds 64 levels"; return false; }
			nesting.push_back( token.text[0] );
		} else if ( token.text == "}" || token.text == ")" ) {
			if ( nesting.back() != ( token.text == "}" ? '{' : '(' ) ) { error = "mismatched primitive delimiter"; return false; }
			nesting.pop_back();
		}
	}
	if ( !nesting.empty() || !scanner.error.empty() ) { error = scanner.error.empty() ? "unterminated primitive" : scanner.error; return false; }
	if ( scanner.position != text.size() ) { error = "primitive text must end with its closing '}'"; return false; }
	return true;
}

class ScopedFlag {
public:
	explicit ScopedFlag( bool &value ) : flag( value ) { flag = true; }
	~ScopedFlag() { flag = false; }
private:
	bool &flag;
};

}

std::string MapDocument::Newline() const {
	if ( header.find( "\r\n" ) != std::string::npos ) { return "\r\n"; }
	if ( !entities.empty() && entities.front().source.find( "\r\n" ) != std::string::npos ) { return "\r\n"; }
	return "\n";
}

bool MapDocument::InsertEntity( const std::string &input, EntityId &id, std::string &error ) {
	error.clear();
	if ( !open ) { error = "no open document"; return false; }
	std::size_t first = input.find_first_not_of( " \t\r\n" ), last = input.find_last_not_of( " \t\r\n" );
	if ( first == std::string::npos ) { error = "empty entity source"; return false; }
	const std::string source = input.substr( first, last - first + 1 );
	if ( source.size() > MaxHistoryBytes || source.find( '\0' ) != std::string::npos ) { error = "entity source exceeds the edit budget"; return false; }
	Scanner scanner( source );
	Token token;
	Entity entity;
	if ( !scanner.Next( token ) || !Is( token, "{" ) || !ReadEntity( scanner, 0, entity, error ) ) {
		if ( error.empty() ) { error = "entity source must start with '{'"; }
		return false;
	}
	if ( scanner.position != source.size() || scanner.Next( token ) ) { error = "entity source must contain exactly one entity"; return false; }
	const std::string classname = entity.Value( "classname" );
	if ( classname.empty() || EqualNoCase( classname, "worldspawn" ) ) { error = "inserted entities need a classname other than worldspawn"; return false; }
	if ( !NameAvailable( entity.Value( "name" ), 0 ) ) { error = "another entity already uses this name"; return false; }
	if ( entities.size() >= 65536 ) { error = "map has too many entities"; return false; }
	Change change;
	change.kind = ChangeKind::Insert;
	change.id = nextId++; change.index = entities.size(); change.prefix = Newline();
	change.after = source;
	const EntityId newId = change.id;
	if ( !Commit( std::move( change ), error ) ) { return false; }
	id = newId;
	return true;
}

std::string MapDocument::PrimitiveText( EntityId id, std::size_t index ) const {
	const Entity *entity = Find( id );
	if ( !entity || index >= entity->primitiveRanges.size() ) { return {}; }
	const auto &range = entity->primitiveRanges[index];
	return entity->source.substr( range.first, range.second - range.first );
}

bool MapDocument::SetPrimitive( EntityId id, std::size_t index, const std::string &text, std::string &error ) {
	error.clear();
	const Entity *entity = Find( id );
	if ( !entity || index >= entity->primitiveRanges.size() ) { error = "unknown primitive"; return false; }
	if ( !ValidPrimitiveText( text, error ) ) { return false; }
	const auto &range = entity->primitiveRanges[index];
	if ( entity->source.compare( range.first, range.second - range.first, text ) == 0 ) { return true; }
	Change change;
	change.id = id; change.offset = range.first;
	change.before = entity->source.substr( range.first, range.second - range.first );
	change.after = text;
	return Commit( std::move( change ), error );
}

bool MapDocument::SetPrimitives( EntityId id, const std::vector<std::pair<std::size_t, std::string>> &editsInput, std::string &error ) {
	error.clear();
	const Entity *entity = Find( id );
	if ( !entity ) { error = "unknown entity handle"; return false; }
	auto edits = editsInput;
	std::sort( edits.begin(), edits.end(), []( const auto &a, const auto &b ) { return a.first < b.first; } );
	Change change;
	change.kind = ChangeKind::Spans;
	change.id = id;
	for ( std::size_t i = 0; i < edits.size(); ++i ) {
		if ( edits[i].first >= entity->primitiveRanges.size() || ( i && edits[i].first == edits[i - 1].first ) ) { error = "unknown or repeated primitive"; return false; }
		if ( !ValidPrimitiveText( edits[i].second, error ) ) { return false; }
		const auto &range = entity->primitiveRanges[edits[i].first];
		if ( entity->source.compare( range.first, range.second - range.first, edits[i].second ) == 0 ) { continue; }
		Span span;
		span.offset = range.first;
		span.before = entity->source.substr( range.first, range.second - range.first );
		span.after = edits[i].second;
		change.spans.push_back( std::move( span ) );
	}
	if ( change.spans.empty() ) { return true; }
	return Commit( std::move( change ), error );
}

bool MapDocument::AddPrimitive( EntityId id, const std::string &text, std::size_t &index, std::string &error ) {
	error.clear();
	const Entity *entity = Find( id );
	if ( !entity ) { error = "unknown entity handle"; return false; }
	if ( !ValidPrimitiveText( text, error ) ) { return false; }
	if ( entity->primitiveRanges.size() >= 1048576 ) { error = "entity has too many primitives"; return false; }
	const std::size_t closing = entity->source.size() - 1; // validated entities end with '}'
	Change change;
	change.id = id; change.offset = closing;
	const std::string newline = entity->source.find( "\r\n" ) != std::string::npos ? "\r\n" : "\n";
	const bool lineStart = closing == 0 || entity->source[closing - 1] == '\n';
	change.after = ( lineStart ? "" : newline ) + text + newline;
	const std::size_t newIndex = entity->primitiveRanges.size();
	if ( !Commit( std::move( change ), error ) ) { return false; }
	index = newIndex;
	return true;
}

bool MapDocument::RemovePrimitive( EntityId id, std::size_t index, std::string &error ) {
	error.clear();
	const Entity *entity = Find( id );
	if ( !entity || index >= entity->primitiveRanges.size() ) { error = "unknown primitive"; return false; }
	const std::string &source = entity->source;
	std::size_t begin = entity->primitiveRanges[index].first, end = entity->primitiveRanges[index].second;
	// Remove the primitive's own line: its indentation and line break.
	std::size_t lineBegin = begin;
	while ( lineBegin > 0 && ( source[lineBegin - 1] == ' ' || source[lineBegin - 1] == '\t' ) ) { --lineBegin; }
	if ( lineBegin == 0 || source[lineBegin - 1] == '\n' ) {
		std::size_t lineEnd = end;
		while ( lineEnd < source.size() && ( source[lineEnd] == ' ' || source[lineEnd] == '\t' ) ) { ++lineEnd; }
		if ( source.compare( lineEnd, 2, "\r\n" ) == 0 ) { lineEnd += 2; }
		else if ( lineEnd < source.size() && source[lineEnd] == '\n' ) { ++lineEnd; }
		if ( lineEnd != end && lineEnd < source.size() ) { begin = lineBegin; end = lineEnd; }
	}
	Change change;
	change.id = id; change.offset = begin;
	change.before = source.substr( begin, end - begin );
	return Commit( std::move( change ), error );
}

bool MapDocument::SetGeometryOrigin( EntityId id, const std::string &value, std::string &error ) {
	ScopedFlag allow( allowGeometryOrigin );
	return SetProperty( id, "origin", value, error );
}

void MapDocument::Apply( const Change &change, bool forward ) {
	if ( change.kind == ChangeKind::Spans ) {
		// Rebuild the entity once. Forward spans sit at their recorded
		// offsets; when reverting, each sits shifted by the earlier spans.
		Entity &entity = entities[IndexOf( change.id )];
		std::string text;
		text.reserve( entity.source.size() + change.Growth() + 64 );
		std::vector<std::pair<std::size_t, std::size_t>> ranges;
		ranges.reserve( entity.primitiveRanges.size() );
		std::size_t cursor = 0, span = 0;
		long long delta = 0, shift = 0;
		for ( const auto &range : entity.primitiveRanges ) {
			const Span *edit = nullptr;
			if ( span < change.spans.size() ) {
				const Span &candidate = change.spans[span];
				const std::size_t at = forward ? candidate.offset : static_cast<std::size_t>( static_cast<long long>( candidate.offset ) + shift );
				const std::string &oldText = forward ? candidate.before : candidate.after;
				if ( range.first == at && range.second - range.first == oldText.size() ) { edit = &candidate; }
			}
			if ( !edit ) {
				ranges.emplace_back( static_cast<std::size_t>( range.first + delta ), static_cast<std::size_t>( range.second + delta ) );
				continue;
			}
			const std::string &oldText = forward ? edit->before : edit->after;
			const std::string &newText = forward ? edit->after : edit->before;
			text.append( entity.source, cursor, range.first - cursor );
			ranges.emplace_back( text.size(), text.size() + newText.size() );
			text += newText;
			cursor = range.second;
			delta += static_cast<long long>( newText.size() ) - static_cast<long long>( oldText.size() );
			shift += static_cast<long long>( edit->after.size() ) - static_cast<long long>( edit->before.size() );
			++span;
		}
		text.append( entity.source, cursor, std::string::npos );
		sourceBytes = sourceBytes - entity.source.size() + text.size();
		entity.source.swap( text );
		entity.primitiveRanges.swap( ranges );
		Refresh( entity, true );
		revision = forward ? change.afterRevision : change.beforeRevision;
		return;
	}
	if ( change.kind == ChangeKind::Text ) {
		Entity &entity = entities[IndexOf( change.id )];
		const std::string &oldText = forward ? change.before : change.after;
		const std::string &newText = forward ? change.after : change.before;
		const std::size_t begin = change.offset, end = begin + oldText.size();
		bool reuseGeometry = true;
		std::vector<std::pair<std::size_t, std::size_t>> ranges;
		ranges.reserve( entity.primitiveRanges.size() );
		for ( auto range : entity.primitiveRanges ) {
			if ( range.second <= begin ) { ranges.push_back( range ); continue; }
			if ( range.first >= end ) {
				ranges.emplace_back( range.first - oldText.size() + newText.size(), range.second - oldText.size() + newText.size() );
				continue;
			}
			if ( range.first == begin && range.second == end ) {
				// Exact replacement: SetPrimitive validated the new block.
				ranges.emplace_back( begin, begin + newText.size() );
				continue;
			}
			if ( range.first >= begin && range.second <= end && newText.empty() ) { continue; } // removed
			reuseGeometry = false;
			break;
		}
		if ( reuseGeometry ) { entity.primitiveRanges.swap( ranges ); }
		entity.source.replace( change.offset, oldText.size(), newText );
		sourceBytes = sourceBytes - oldText.size() + newText.size();
		Refresh( entity, reuseGeometry );
	} else if ( ( change.kind == ChangeKind::Insert ) == forward ) {
		Entity entity;
		entity.id = change.id; entity.prefix = change.prefix;
		entity.source = change.kind == ChangeKind::Insert ? change.after : change.before;
		Refresh( entity );
		sourceBytes += entity.source.size() + entity.prefix.size();
		entities.insert( entities.begin() + change.index, std::move( entity ) );
	} else {
		const auto index = IndexOf( change.id );
		sourceBytes -= entities[index].source.size() + entities[index].prefix.size();
		entities.erase( entities.begin() + index );
	}
	revision = forward ? change.afterRevision : change.beforeRevision;
}

void MapDocument::Push( Step step ) {
	while ( history.size() > cursor ) { historyBytes -= history.back().bytes; history.pop_back(); }
	historyBytes += step.bytes;
	history.push_back( std::move( step ) );
	while ( history.size() > MaxHistoryEntries || historyBytes > MaxHistoryBytes ) {
		historyBytes -= history.front().bytes; history.erase( history.begin() );
	}
	cursor = history.size();
}

bool MapDocument::Commit( Change change, std::string &error ) {
	if ( change.Bytes() > MaxHistoryBytes || ( transactionDepth && pending.bytes + change.Bytes() > MaxHistoryBytes ) ) {
		error = "edit exceeds the 16 MiB undo budget"; return false;
	}
	const std::size_t newBytes = sourceBytes - change.Shrink() + change.Growth() +
		( change.kind == ChangeKind::Insert ? change.prefix.size() : 0 );
	if ( newBytes > MaxSourceBytes ) { error = "edit would exceed the 64 MiB map limit"; return false; }
	change.beforeRevision = revision; change.afterRevision = nextRevision++;
	Apply( change, true );
	if ( transactionDepth ) {
		pending.bytes += change.Bytes();
		pending.changes.push_back( std::move( change ) );
		return true;
	}
	Step step;
	step.label = change.kind == ChangeKind::Insert ? "create entity" : change.kind == ChangeKind::Erase ? "delete entity" : "edit";
	step.beforeRevision = change.beforeRevision; step.afterRevision = change.afterRevision;
	step.bytes = change.Bytes();
	step.changes.push_back( std::move( change ) );
	Push( std::move( step ) );
	return true;
}

bool MapDocument::BeginTransaction( const std::string &label, std::string &error ) {
	error.clear();
	if ( !open ) { error = "no open document"; return false; }
	if ( transactionDepth == 0 ) {
		pending = Step();
		pending.label = label;
		pending.beforeRevision = revision;
	}
	++transactionDepth;
	return true;
}

bool MapDocument::CommitTransaction( std::string &error ) {
	error.clear();
	if ( transactionDepth == 0 ) { error = "no open transaction"; return false; }
	if ( --transactionDepth > 0 ) { return true; }
	if ( !pending.changes.empty() ) {
		pending.afterRevision = revision;
		Push( std::move( pending ) );
	}
	pending = Step();
	return true;
}

void MapDocument::RevertTransaction() {
	for ( auto it = pending.changes.rbegin(); it != pending.changes.rend(); ++it ) { Apply( *it, false ); }
	if ( !pending.changes.empty() ) { revision = pending.beforeRevision; }
	pending.changes.clear();
	pending.bytes = 0;
}

void MapDocument::CancelTransaction() {
	RevertTransaction();
	transactionDepth = 0;
	pending = Step();
}

bool MapDocument::Undo( std::string &error ) {
	error.clear();
	if ( transactionDepth ) { error = "finish or cancel the current edit first"; return false; }
	if ( !CanUndo() ) { error = "nothing to undo"; return false; }
	const Step &step = history[--cursor];
	for ( auto it = step.changes.rbegin(); it != step.changes.rend(); ++it ) { Apply( *it, false ); }
	revision = step.beforeRevision;
	return true;
}

bool MapDocument::Redo( std::string &error ) {
	error.clear();
	if ( transactionDepth ) { error = "finish or cancel the current edit first"; return false; }
	if ( !CanRedo() ) { error = "nothing to redo"; return false; }
	const Step &step = history[cursor++];
	for ( const auto &change : step.changes ) { Apply( change, true ); }
	revision = step.afterRevision;
	return true;
}

}
