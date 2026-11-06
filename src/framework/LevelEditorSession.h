// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENQ4_LEVEL_EDITOR_SESSION_H
#define OPENQ4_LEVEL_EDITOR_SESSION_H

#include "../tools/leveleditor/MapBuild.h"
#include "../tools/leveleditor/EditorState.h"
#include "../tools/leveleditor/MapWorkspace.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

class idLevelEditorChild;

// Shared state of the experimental editor: the console adapter and the
// graphical workspace drive the same selection, tools, build and play.
class idLevelEditorSession {
public:
	// Single operations are judged by their worst case. Views redraw every
	// frame and the first frames after a restart or a new material load
	// textures, so their budget applies to the 95th percentile.
	struct Latency {
		const char *name = "";
		double budgetMs = 0, lastMs = 0, maxMs = 0, totalMs = 0;
		int samples = 0;
		bool percentile = false;
		std::vector<float> recent; // ring of the latest 4096 samples
		void Add( double ms ) {
			lastMs = ms; maxMs = ms > maxMs ? ms : maxMs; totalMs += ms;
			if ( recent.size() < 4096 ) { recent.push_back( static_cast<float>( ms ) ); }
			else { recent[static_cast<std::size_t>( samples ) % recent.size()] = static_cast<float>( ms ); }
			++samples;
		}
		double Percentile( double fraction ) const;
	};
	// refresh: from a document change to the workspace showing it (panels
	// and the re-tessellated preview chunks).
	enum LatencyKind { LATENCY_STARTUP, LATENCY_EDIT, LATENCY_SELECT, LATENCY_VIEW3D, LATENCY_ORTHO, LATENCY_VALIDATE, LATENCY_REFRESH, LATENCY_COUNT };

	idLevelEditorSession();
	~idLevelEditorSession();

	oq4editor::MapWorkspace &Workspace();
	oq4editor::MapDocument &Document() { return Workspace().Document(); }
	const oq4editor::MapGeometryCache &Geometry();

	oq4editor::Selection selection;
	oq4editor::MapEditor editor;
	oq4editor::ViewState view;
	oq4editor::DockLayout layout;
	oq4editor::AssetIndex assets;
	std::vector<oq4editor::Diagnostic> diagnostics;
	Latency latency[LATENCY_COUNT];

	void Frame();
	void Shutdown();
	// Saves the previous map's view state and loads the current one when the
	// document path changes. Cheap when nothing changed.
	void SyncDocument();
	void SaveViewState();

	// Selection respects hidden and locked layers.
	bool LayerHidden( const oq4editor::Entity &entity ) const;
	bool LayerLocked( const oq4editor::Entity &entity ) const;
	bool Selectable( const oq4editor::SelectionItem &item ) const;
	bool Visible( const oq4editor::SelectionItem &item ) const;
	void Select( const oq4editor::SelectionItem &item, bool add );
	void ClearSelection() { selection.clear(); }
	bool IsSelected( const oq4editor::SelectionItem &item ) const;
	void PruneSelection();
	oq4editor::EntityId PrimaryEntity() const;
	bool SelectionPivot( oq4editor::Vec3 &pivot );

	// Tools operating on the selection; each is one undo step.
	bool Transform( const oq4editor::Affine &transform, std::string &error );
	bool Translate( const oq4editor::Vec3 &delta, std::string &error );
	bool Rotate( int axis, double degrees, std::string &error );
	bool Scale( const oq4editor::Vec3 &factors, std::string &error );

	const oq4editor::ClassSchema *Schema( const std::string &classname );
	oq4editor::ValidatorOptions ValidationOptions();
	void Validate();

	bool StartBuild( bool aas, std::string &error );
	void CancelBuild();
	oq4editor::MapBuild *Build() { return build.get(); }
	bool BuildCurrent() const;
	bool Play( std::string &error );
	bool Playing();
	int LastPlayExit() const { return playExit; }
	bool ExternalChange() const { return externalChange; }
	bool Reload( bool discard, std::string &error );

	void StartAssetIndex();
	bool AssetIndexStarted() const { return assetIndexStarted; }
	void PrintLatency() const;

private:
	std::unique_ptr<oq4editor::MapWorkspace> workspace;
	oq4editor::MapGeometryCache geometry;
	std::uint64_t geometryRevision = 0;
	std::string geometryPath;
	std::unique_ptr<oq4editor::MapBuild> build;
	std::string builtPath, viewPath;
	std::uint64_t builtRevision = 0;
	bool viewLoaded = false, assetIndexStarted = false, buildReported = true, externalChange = false;
	unsigned lastExternalCheck = 0;
	std::unique_ptr<idLevelEditorChild> play;
	int playExit = 0;
	std::map<std::string, std::unique_ptr<oq4editor::ClassSchema>> schemas;
	std::vector<std::string> modelFiles, mapFiles;
};

idLevelEditorSession &LevelEditor_Session();
double LevelEditor_Milliseconds();

#endif
