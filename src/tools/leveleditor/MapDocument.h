// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENQ4_EDITOR_MAP_DOCUMENT_H
#define OPENQ4_EDITOR_MAP_DOCUMENT_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Authoring state deliberately has no dependency on a window, renderer, live
// game world, or idMapFile::Resolve. Primitive source remains lossless until a
// geometry tool explicitly edits it. This is a document service, not a second
// geometry compiler: dmap remains responsible for geometric validation.
namespace oq4editor {

using EntityId = std::uint64_t;

struct Property {
	std::string key;
	std::string value;
	std::size_t begin = 0;
	std::size_t valueBegin = 0;
	std::size_t end = 0;
};

struct Entity {
	EntityId id = 0;
	std::string prefix;
	std::string source;
	std::vector<Property> properties;
	std::vector<std::pair<std::size_t, std::size_t>> primitiveRanges;
	std::size_t primitiveCount = 0;
	std::size_t propertyInsert = 0;
	std::string Value( const std::string &key ) const;
};

bool EqualNoCase( const std::string &a, const std::string &b );

class MapDocument {
public:
	static constexpr std::size_t MaxSourceBytes = 64 * 1024 * 1024;
	static constexpr std::size_t MaxHistoryBytes = 16 * 1024 * 1024;
	static constexpr std::size_t MaxHistoryEntries = 256;

	bool OpenText( const std::string &source, std::string &error );
	void New();
	void Close();
	bool IsOpen() const { return open; }
	int Version() const { return version; }
	bool IsDirty() const { return open && revision != savedRevision; }
	void MarkSaved() { savedRevision = revision; }
	void MarkUnsaved() { savedRevision = 0; }
	std::uint64_t Revision() const { return revision; }
	std::size_t HistoryBytes() const { return historyBytes; }
	bool CanUndo() const { return cursor != 0; }
	bool CanRedo() const { return cursor < history.size(); }
	const std::vector<Entity> &Entities() const { return entities; }
	const Entity *Find( EntityId id ) const;
	std::string Serialize() const;

	bool SetProperty( EntityId id, const std::string &key, const std::string &value, std::string &error );
	bool RemoveProperty( EntityId id, const std::string &key, std::string &error );
	bool AddEntity( const std::string &classname, const std::string &name, EntityId &id, std::string &error );
	// Inserts one complete entity block (properties and primitives) after
	// structural validation. Used by prefabs, duplication and grouping.
	bool InsertEntity( const std::string &source, EntityId &id, std::string &error );
	bool DeleteEntity( EntityId id, std::string &error );
	// Primitive edits replace only that primitive's bytes. The text must be one
	// balanced "{ ... }" block; geometric validity stays with the adapters/dmap.
	bool SetPrimitive( EntityId id, std::size_t index, const std::string &text, std::string &error );
	// Many primitives of one entity in a single linear pass and one change.
	bool SetPrimitives( EntityId id, const std::vector<std::pair<std::size_t, std::string>> &edits, std::string &error );
	bool AddPrimitive( EntityId id, const std::string &text, std::size_t &index, std::string &error );
	bool RemovePrimitive( EntityId id, std::size_t index, std::string &error );
	std::string PrimitiveText( EntityId id, std::size_t index ) const;
	// Transform tools move geometry and the stored origin together.
	bool SetGeometryOrigin( EntityId id, const std::string &value, std::string &error );
	bool NameAvailable( const std::string &name, EntityId except ) const;
	std::string Newline() const;
	bool Undo( std::string &error );
	bool Redo( std::string &error );
	// Grouped edits form one undo step. Revert discards the pending edits but
	// keeps the transaction open, so a gesture can re-apply its total result
	// on every pointer move. Cancel reverts and closes; nesting is counted.
	bool BeginTransaction( const std::string &label, std::string &error );
	bool CommitTransaction( std::string &error );
	void RevertTransaction();
	void CancelTransaction();
	bool InTransaction() const { return transactionDepth != 0; }
	std::size_t PendingChanges() const { return pending.changes.size(); }
	std::string UndoLabel() const { return CanUndo() ? history[cursor - 1].label : std::string(); }
	std::string RedoLabel() const { return CanRedo() ? history[cursor].label : std::string(); }
	std::size_t HistorySteps() const { return history.size(); }

private:
	enum class ChangeKind { Text, Insert, Erase, Spans };
	struct Span { std::size_t offset = 0; std::string before, after; };
	struct Change {
		ChangeKind kind = ChangeKind::Text;
		EntityId id = 0;
		std::size_t index = 0;
		std::size_t offset = 0;
		std::string before, after, prefix;
		std::vector<Span> spans; // Spans: exact primitive replacements, ascending
		std::uint64_t beforeRevision = 0, afterRevision = 0;
		std::size_t Bytes() const {
			std::size_t bytes = before.size() + after.size() + prefix.size() + sizeof( Change );
			for ( const auto &span : spans ) { bytes += span.before.size() + span.after.size() + sizeof( Span ); }
			return bytes;
		}
		std::size_t Growth() const {
			std::size_t grown = after.size();
			for ( const auto &span : spans ) { grown += span.after.size(); }
			return grown;
		}
		std::size_t Shrink() const {
			std::size_t shrunk = before.size();
			for ( const auto &span : spans ) { shrunk += span.before.size(); }
			return shrunk;
		}
	};
	struct Step {
		std::vector<Change> changes;
		std::string label;
		std::uint64_t beforeRevision = 0, afterRevision = 0;
		std::size_t bytes = 0;
	};
	bool Commit( Change change, std::string &error );
	void Apply( const Change &change, bool forward );
	void Push( Step step );
	bool CheckProperty( const Entity &entity, const std::string &key, const std::string &value, std::string &error ) const;
	std::size_t IndexOf( EntityId id ) const;
	void ResetHistory();

	std::vector<Entity> entities;
	std::vector<Step> history;
	Step pending;
	int transactionDepth = 0;
	bool allowGeometryOrigin = false;
	std::string header, tail;
	std::size_t cursor = 0, historyBytes = 0, sourceBytes = 0;
	EntityId nextId = 1;
	std::uint64_t revision = 0, savedRevision = 0, nextRevision = 1;
	bool open = false;
	int version = 3;
};

}
#endif
