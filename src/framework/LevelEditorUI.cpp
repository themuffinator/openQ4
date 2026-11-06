// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#include "LevelEditor.h"
#include "LevelEditorPreview.h"
#include "LevelEditorSession.h"
#include "LevelEditorViews.h"
#include "../ui/DeviceContext.h"
#include "../ui/Window.h"
#include "../ui/UserInterfaceLocal.h"
#include "../ui/LevelEditorWindow.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <vector>

static idCVar editorExperimental_layout( "editorExperimental_layout", "", CVAR_TOOL | CVAR_ARCHIVE,
	"editor panel docking: panel=dock;size.<dock>=n;tab.<dock>=panel" );
static idCVar editorExperimental_modelBudget( "editorExperimental_modelBudget", "3", CVAR_TOOL | CVAR_FLOAT,
	"milliseconds per frame the editor may spend loading entity model previews", 0.0f, 50.0f );

using namespace oq4editor;
namespace views = leveleditorviews;

namespace {
idUserInterface *workspaceGui = nullptr;
std::unique_ptr<idLevelEditorPreview> preview;
std::uint64_t displayedRevision = 0;
std::vector<EntityId> entityRows;
std::vector<std::string> browserRows, propertyRows, layerRows;
std::vector<std::size_t> problemRows;
std::string pendingOperation, pendingPath, displayedPath;
std::set<std::string> displayedHidden;
Selection displayedSelection;
std::size_t displayedDiagnostics = static_cast<std::size_t>( -1 );
int displayedBuildState = -1, displayedBuildPercent = -1, displayedIndex = -1;
bool displayedExternal = false;
bool switchingWorkspace = false, displayedDirty = false, startupPending = false, previewBuilding = false;
double previewStarted = 0;
int browserMode = 0;
double openedAt = 0;

struct Gesture {
	enum Kind { None, Pending, Move, Pan, Orbit, Rubber };
	Kind kind = None;
	int mode = 0, button = 0;
	float startX = 0, startY = 0, lastX = 0, lastY = 0, width = 1, height = 1;
	bool shift = false, ctrl = false;
	SelectionItem hit;
	bool hitValid = false;
	Vec3 anchor;
} gesture;
views::Overlay overlay;

class WorkspaceTransition {
public:
	WorkspaceTransition() : previous( switchingWorkspace ) { switchingWorkspace = true; }
	~WorkspaceTransition() { switchingWorkspace = previous; }
private:
	bool previous;
};

idLevelEditorSession &S() { return LevelEditor_Session(); }
const char *Tr( const char *key ) { return common->GetLanguageDict()->GetString( key ); }
void Message( const char *key ) { workspaceGui->SetStateString( "ed_message", Tr( key ) ); }
void Result( bool ok, const std::string &error ) {
	Message( ok ? "#str_42926" : "#str_42927" );
	if ( !ok ) { common->Printf( "editor workspace rejected: %s\n", error.c_str() ); }
}

// Authored text is data, including tabs/newlines and Quake color escapes.
// Strip list separators/control bytes so a name cannot invent extra columns.
std::string ListText( std::string text ) {
	for ( char &c : text ) { if ( static_cast<unsigned char>( c ) < 32 || c == '^' ) { c = ' '; } }
	return text;
}

void ClearList( const char *name ) {
	for ( int i = 0; workspaceGui->State().FindKey( va( "%s_item_%d", name, i ) ); ++i ) {
		workspaceGui->DeleteStateVar( va( "%s_item_%d", name, i ) );
	}
}

const char *TypeLabel( PropertyType type ) {
	static const char *keys[] = { "#str_43118", "#str_43119", "#str_43120", "#str_43121", "#str_43122", "#str_43123", "#str_43124",
		"#str_43125", "#str_43126", "#str_43127", "#str_43128", "#str_43129", "#str_43130" };
	return Tr( keys[static_cast<int>( type )] );
}

const char *CodeLabel( const std::string &code ) {
	static const struct { const char *code, *key; } labels[] = {
		{ "duplicate-name", "#str_43102" }, { "unknown-class", "#str_43103" }, { "invalid-value", "#str_43104" },
		{ "missing-target", "#str_43105" }, { "missing-asset", "#str_43106" }, { "invalid-origin", "#str_43107" },
		{ "invalid-primitive", "#str_43108" }, { "legacy-primitive", "#str_43109" }, { "invalid-geometry", "#str_43110" },
		{ "no-player-start", "#str_43111" }, { "leak", "#str_43112" }, { "compiler-error", "#str_43113" }, { "compiler-warning", "#str_43114" } };
	for ( const auto &label : labels ) { if ( code == label.code ) { return Tr( label.key ); } }
	return code.c_str();
}

const char *SeverityLabel( Diagnostic::Severity severity ) {
	return Tr( severity == Diagnostic::Severity::Error ? "#str_43115" : severity == Diagnostic::Severity::Warning ? "#str_43116" : "#str_43117" );
}

void SetRect( const char *prefix, const DockLayout::Rect &rect, bool visible ) {
	workspaceGui->SetStateFloat( va( "%s_x", prefix ), rect.x );
	workspaceGui->SetStateFloat( va( "%s_y", prefix ), rect.y );
	workspaceGui->SetStateFloat( va( "%s_w", prefix ), rect.w );
	workspaceGui->SetStateFloat( va( "%s_h", prefix ), rect.h );
	workspaceGui->SetStateBool( va( "%s_vis", prefix ), visible );
}

void Layout() {
	auto &s = S();
	const auto result = s.layout.Compute( { 8, 126, 624, 312 } );
	for ( int p = 0; p < DockLayout::PanelCount; ++p ) {
		const auto &placement = result.panels[p];
		const char *name = DockLayout::PanelName( static_cast<DockLayout::Panel>( p ) );
		SetRect( va( "ed_%s", name ), placement.rect, placement.visible );
		SetRect( va( "ed_%s_tab", name ), placement.tab, placement.tabVisible );
		workspaceGui->SetStateBool( va( "ed_%s_active", name ), placement.visible );
	}
	SetRect( "ed_view", result.views, true );
	SetRect( "ed_lsplit", result.leftSplitter, result.leftSplitter.w > 0 );
	SetRect( "ed_rsplit", result.rightSplitter, result.rightSplitter.w > 0 );
	SetRect( "ed_bsplit", result.bottomSplitter, result.bottomSplitter.w > 0 );
	workspaceGui->SetStateInt( "ed_mode", s.view.mode );
	workspaceGui->SetStateBool( "ed_quad", s.view.quad );
	workspaceGui->SetStateBool( "ed_single", !s.view.quad );
	workspaceGui->SetStateBool( "ed_lock", s.view.textureLock );
	workspaceGui->SetStateString( "ed_lockText", Tr( s.view.textureLock ? "#str_42958" : "#str_42988" ) );
	workspaceGui->SetStateString( "ed_hint", Tr( !s.view.quad && s.view.mode == 0 ? "#str_42945" : "#str_42997" ) );
	workspaceGui->SetStateString( "ed_grid", va( "%s %g", Tr( "#str_42954" ), s.view.grid ) );
	editorExperimental_layout.SetString( s.layout.Serialize().c_str() );
}

void Inspector( bool preserveKey = false ) {
	auto &s = S();
	const std::string previousKey = preserveKey ? workspaceGui->GetStateString( "ed_key" ) : "";
	ClearList( "edProperties" ); propertyRows.clear();
	const auto *entity = s.Document().Find( s.PrimaryEntity() );
	const ValidatorOptions options = s.ValidationOptions();
	if ( s.selection.size() > 1 ) {
		workspaceGui->SetStateString( "ed_selected", va( Tr( "#str_43100" ), static_cast<int>( s.selection.size() ) ) );
	} else {
		workspaceGui->SetStateString( "ed_selected", entity ? ListText( entity->Value( "classname" ) + " / " + entity->Value( "name" ) +
			( s.selection.front().primitive == WholeEntity ? std::string() : va( " :%zu", s.selection.front().primitive ) ) ).c_str() : Tr( "#str_42923" ) );
	}
	const ClassSchema *schema = entity ? s.Schema( entity->Value( "classname" ) ) : nullptr;
	workspaceGui->SetStateString( "ed_usage", schema ? ListText( schema->usage ).c_str() : "" );
	if ( entity ) {
		for ( const auto &property : entity->properties ) {
			PropertyType type;
			std::string row = property.key + " = " + property.value;
			if ( PropertyTypeFor( options, *entity, property.key, type ) ) { row += std::string( "  [" ) + TypeLabel( type ) + "]"; }
			workspaceGui->SetStateString( va( "edProperties_item_%d", static_cast<int>( propertyRows.size() ) ), ListText( row ).c_str() );
			propertyRows.push_back( property.key );
		}
	}
	workspaceGui->SetStateInt( "edProperties_sel_0", -1 );
	workspaceGui->SetStateString( "ed_key", "" );
	workspaceGui->SetStateString( "ed_value", "" );
	workspaceGui->SetStateString( "ed_keyType", "" );
	if ( entity && !previousKey.empty() ) {
		for ( std::size_t i = 0; i < propertyRows.size(); ++i ) {
			if ( EqualNoCase( propertyRows[i], previousKey ) ) {
				workspaceGui->SetStateInt( "edProperties_sel_0", static_cast<int>( i ) );
				workspaceGui->SetStateString( "ed_key", propertyRows[i].c_str() );
				workspaceGui->SetStateString( "ed_value", entity->Value( propertyRows[i] ).c_str() );
				break;
			}
		}
	}
	const std::string name = entity ? entity->Value( "name" ) : "";
	workspaceGui->SetStateString( "ed_rename", name.c_str() );
	workspaceGui->SetStateString( "ed_refs", va( Tr( "#str_42984" ), static_cast<int>( s.editor.References( s.Document(), name ).size() ) ) );
	workspaceGui->SetStateString( "ed_layer", entity ? EntityLayer( *entity ).c_str() : "" );
}

void KeyType() {
	auto &s = S();
	const auto *entity = s.Document().Find( s.PrimaryEntity() );
	const std::string key = workspaceGui->GetStateString( "ed_key" );
	PropertyType type;
	if ( !entity || key.empty() || !PropertyTypeFor( s.ValidationOptions(), *entity, key, type ) ) { workspaceGui->SetStateString( "ed_keyType", "" ); return; }
	const ClassSchema *schema = s.Schema( entity->Value( "classname" ) );
	const PropertySpec *spec = schema ? schema->Find( key ) : nullptr;
	workspaceGui->SetStateString( "ed_keyType", ListText( std::string( va( Tr( "#str_42998" ), TypeLabel( type ) ) ) + ( spec && !spec->description.empty() ? "  " + spec->description : "" ) ).c_str() );
}

void Browser() {
	auto &s = S();
	ClearList( "edBrowser" ); entityRows.clear(); browserRows.clear();
	const std::string filter = workspaceGui->GetStateString( "ed_filter" );
	const auto accepts = [&filter]( const std::string &name ) { return filter.empty() || idStr::FindText( name.c_str(), filter.c_str(), false ) >= 0; };
	if ( browserMode == 0 ) {
		for ( const auto &entity : s.Document().Entities() ) {
			std::string label = entity.Value( "name" );
			if ( label.empty() ) { label = entity.Value( "classname" ); }
			if ( !accepts( label + " " + entity.Value( "classname" ) ) ) { continue; }
			if ( s.LayerHidden( entity ) ) { label = "(" + label + ")"; }
			entityRows.push_back( entity.id ); browserRows.push_back( ListText( label ) );
		}
	} else {
		static const AssetIndex::Kind kinds[] = { AssetIndex::EntityDef, AssetIndex::Map, AssetIndex::EntityDef, AssetIndex::Material, AssetIndex::Model, AssetIndex::Sound };
		s.StartAssetIndex();
		for ( const auto &name : s.assets.Search( kinds[browserMode], filter, 65536 ) ) { browserRows.push_back( ListText( name ) ); }
	}
	int row = -1;
	for ( std::size_t i = 0; i < browserRows.size(); ++i ) {
		workspaceGui->SetStateString( va( "edBrowser_item_%d", static_cast<int>( i ) ), browserRows[i].c_str() );
		if ( browserMode == 0 && entityRows[i] == s.PrimaryEntity() ) { row = static_cast<int>( i ); }
	}
	workspaceGui->SetStateInt( "edBrowser_sel_0", row );
	if ( browserMode != 0 && !s.assets.Complete() ) {
		workspaceGui->SetStateString( "ed_browserCount", va( Tr( "#str_42999" ), static_cast<int>( s.assets.GetStats().entries ) ) );
	} else {
		workspaceGui->SetStateString( "ed_browserCount", va( Tr( "#str_42924" ), static_cast<int>( browserRows.size() ) ) );
	}
	workspaceGui->SetStateInt( "ed_browserMode", browserMode );
	displayedIndex = static_cast<int>( s.assets.GetStats().entries );
}

void Tools() {
	auto &s = S();
	ClearList( "edLayers" ); layerRows.clear();
	std::map<std::string, int> layers;
	for ( const auto &entity : s.Document().Entities() ) { const std::string layer = EntityLayer( entity ); if ( !layer.empty() ) { ++layers[layer]; } }
	for ( const auto &layer : s.view.hiddenLayers ) { layers[layer]; }
	for ( const auto &[layer, count] : layers ) {
		std::string row = layer + "  " + std::to_string( count );
		if ( s.view.hiddenLayers.count( layer ) ) { row += std::string( "  " ) + Tr( "#str_42976" ); }
		if ( s.view.lockedLayers.count( layer ) ) { row += std::string( "  " ) + Tr( "#str_42978" ); }
		workspaceGui->SetStateString( va( "edLayers_item_%d", static_cast<int>( layerRows.size() ) ), ListText( row ).c_str() );
		layerRows.push_back( layer );
	}
	workspaceGui->SetStateInt( "edLayers_sel_0", -1 );
}

void BuildStatus() {
	auto &s = S();
	const MapBuild *build = s.Build();
	int state = build ? static_cast<int>( build->GetState() ) : 0;
	const int percent = build ? static_cast<int>( build->Progress() * 100.0f ) : 0;
	const char *text = "";
	if ( s.Playing() ) { text = Tr( "#str_42992" ); state = 10; }
	else if ( !build ) { text = ""; }
	else if ( build->GetState() == MapBuild::State::Running ) { text = va( Tr( "#str_43101" ), percent ); }
	else if ( build->GetState() == MapBuild::State::Succeeded ) { text = Tr( "#str_42989" ); }
	else if ( build->GetState() == MapBuild::State::Failed ) { text = Tr( "#str_42990" ); }
	else if ( build->GetState() == MapBuild::State::Cancelled ) { text = Tr( "#str_42991" ); }
	workspaceGui->SetStateString( "ed_buildStatus", text );
	workspaceGui->SetStateFloat( "ed_buildProgress", build && build->Active() ? build->Progress() : 0.0f );
	workspaceGui->SetStateBool( "ed_building", build && build->Active() );
	displayedBuildState = state;
	displayedBuildPercent = percent;
}

void Problems() {
	auto &s = S();
	ClearList( "edProblems" ); problemRows.clear();
	int counts[3] = {};
	for ( std::size_t i = 0; i < s.diagnostics.size(); ++i ) {
		const auto &d = s.diagnostics[i];
		++counts[static_cast<int>( d.severity )];
		const Entity *entity = s.Document().Find( d.entity );
		std::string subject = entity ? ( entity->Value( "name" ).empty() ? entity->Value( "classname" ) : entity->Value( "name" ) ) : "";
		if ( d.primitive != WholeEntity ) { subject += ":" + std::to_string( d.primitive ); }
		const bool compiler = d.code.compare( 0, 8, "compiler" ) == 0 || d.code == "leak";
		std::string row = std::string( SeverityLabel( d.severity ) ) + "  " + CodeLabel( d.code ) + "  " + subject;
		// Property and compiler details are data from the map and the compiler log.
		const auto colon = d.message.find( ':' );
		if ( compiler ) { row += "  " + d.message; }
		else if ( d.code == "invalid-value" || d.code == "missing-target" || d.code == "missing-asset" ) { row += "  " + d.message.substr( 0, colon == std::string::npos ? d.message.size() : colon ); }
		workspaceGui->SetStateString( va( "edProblems_item_%d", static_cast<int>( problemRows.size() ) ), ListText( row ).c_str() );
		problemRows.push_back( i );
	}
	workspaceGui->SetStateInt( "edProblems_sel_0", -1 );
	workspaceGui->SetStateString( "ed_problemCount", va( Tr( "#str_42987" ), counts[2], counts[1] ) );
	displayedDiagnostics = s.diagnostics.size();
}

std::set<EntityId> WholeSelection() {
	std::set<EntityId> whole;
	for ( const auto &item : S().selection ) { if ( item.primitive == WholeEntity ) { whole.insert( item.entity ); } }
	return whole;
}

void SyncCameraToView() {
	if ( !preview ) { return; }
	const auto camera = preview->GetCamera();
	auto &view = S().view;
	view.target = Vec3( camera.target.x, camera.target.y, camera.target.z );
	view.yaw = camera.yaw; view.pitch = camera.pitch; view.distance = camera.distance;
	view.valid = true;
}

void FitOrtho( const Vec3 &mins, const Vec3 &maxs ) {
	auto &view = S().view;
	view.orthoCenter = ( mins + maxs ) * 0.5;
	const double extent = ( std::max )( { maxs.x - mins.x, maxs.y - mins.y, maxs.z - mins.z, 64.0 } );
	view.orthoScale = idMath::ClampFloat( 1.0f / 64.0f, 512.0f, static_cast<float>( extent * 1.2 / 200.0 ) );
}

// Whole-map framing ignores the few primitives far from the playable space
// (sky shells, distant scenery): the 2nd-98th percentile of item centres.
bool PlayableBounds( Vec3 &mins, Vec3 &maxs ) {
	auto &s = S();
	std::vector<double> axes[3];
	for ( const auto &[id, cached] : s.Geometry().Entities() ) {
		if ( cached.point ) { for ( int i = 0; i < 3; ++i ) { axes[i].push_back( cached.origin[i] ); } continue; }
		for ( const auto &item : cached.primitives ) {
			if ( item.valid ) { for ( int i = 0; i < 3; ++i ) { axes[i].push_back( ( item.mins[i] + item.maxs[i] ) * 0.5 ); } }
		}
	}
	if ( axes[0].size() < 16 ) { return false; }
	for ( int i = 0; i < 3; ++i ) {
		std::sort( axes[i].begin(), axes[i].end() );
		const std::size_t n = axes[i].size();
		mins[i] = axes[i][n / 50]; maxs[i] = axes[i][n - 1 - n / 50];
		const double pad = ( std::max )( 64.0, ( maxs[i] - mins[i] ) * 0.05 );
		mins[i] -= pad; maxs[i] += pad;
	}
	return true;
}

void FrameSelection( bool all ) {
	auto &s = S();
	Vec3 mins, maxs;
	Selection target = s.selection;
	if ( all || target.empty() ) {
		target.clear();
		for ( const auto &entity : s.Document().Entities() ) { target.push_back( { entity.id, WholeEntity } ); }
		if ( !PlayableBounds( mins, maxs ) && !s.editor.Bounds( s.Document(), target, mins, maxs ) ) { return; }
	} else if ( !s.editor.Bounds( s.Document(), target, mins, maxs ) ) { return; }
	FitOrtho( mins, maxs );
	if ( preview ) {
		preview->FocusBounds( idBounds( idVec3( mins.x, mins.y, mins.z ), idVec3( maxs.x, maxs.y, maxs.z ) ) );
		SyncCameraToView();
	}
}

void RebuildPreview() {
	auto &s = S();
	const bool first = !preview;
	if ( first ) { preview = std::make_unique<idLevelEditorPreview>(); previewStarted = LevelEditor_Milliseconds(); }
	// Opening a map tessellates over several frames so the workspace stays
	// interactive; later edits rebuild only the entities they touched.
	previewBuilding = !preview->Rebuild( s.Document(), [&s]( const Entity &entity ) { return s.LayerHidden( entity ); }, first || previewBuilding ? 12.0 : 0.0 );
	if ( !previewBuilding && previewStarted > 0 ) {
		common->Printf( "editor preview complete: %.0fms\n", LevelEditor_Milliseconds() - previewStarted );
		previewStarted = 0;
	}
	if ( first ) {
		if ( s.view.valid ) {
			idLevelEditorPreview::Camera camera;
			camera.target.Set( static_cast<float>( s.view.target.x ), static_cast<float>( s.view.target.y ), static_cast<float>( s.view.target.z ) );
			camera.yaw = static_cast<float>( s.view.yaw ); camera.pitch = static_cast<float>( s.view.pitch ); camera.distance = static_cast<float>( s.view.distance );
			preview->SetCamera( camera );
		} else {
			FrameSelection( true );
		}
	}
	preview->SetSelection( WholeSelection() );
	workspaceGui->SetStateString( "ed_previewStatus", va( Tr( "#str_42925" ), preview->Triangles(), preview->Skipped() ) );
	displayedHidden = s.view.hiddenLayers;
}

void Refresh( bool rebuildPreview = true ) {
	auto &s = S();
	const double started = LevelEditor_Milliseconds();
	double marks[6] = {};
	auto &workspace = s.Workspace();
	const auto &document = workspace.Document();
	s.SyncDocument();
	s.PruneSelection();
	if ( displayedPath != workspace.Path() ) {
		displayedPath = workspace.Path(); workspaceGui->SetStateString( "ed_path", displayedPath.c_str() );
	}
	workspaceGui->SetStateString( "ed_title", workspace.Path().empty() ? Tr( "#str_42922" ) : workspace.Path().c_str() );
	workspaceGui->SetStateString( "ed_dirty", Tr( document.IsDirty() ? "#str_42920" : "#str_42921" ) );
	workspaceGui->SetStateInt( "ed_canUndo", document.CanUndo() );
	workspaceGui->SetStateInt( "ed_canRedo", document.CanRedo() );
	Layout(); marks[0] = LevelEditor_Milliseconds();
	Browser(); marks[1] = LevelEditor_Milliseconds();
	Inspector( true ); KeyType(); marks[2] = LevelEditor_Milliseconds();
	Tools(); Problems(); BuildStatus(); marks[3] = LevelEditor_Milliseconds();
	if ( rebuildPreview || !preview ) { RebuildPreview(); }
	else { preview->SetSelection( WholeSelection() ); }
	marks[4] = LevelEditor_Milliseconds();
	if ( marks[4] - started > 50 ) {
		common->Printf( "editor workspace refresh: %.0fms (layout %.0f, browser %.0f, inspector %.0f, panels %.0f, preview %.0f)\n", marks[4] - started,
			marks[0] - started, marks[1] - marks[0], marks[2] - marks[1], marks[3] - marks[2], marks[4] - marks[3] );
	}
	displayedRevision = document.Revision();
	displayedDirty = document.IsDirty();
	displayedSelection = s.selection;
	workspaceGui->StateChanged( common->GetPresentationTime() );
}

void ReplaceDocument( const std::string &operation, const std::string &path, bool discard = false ) {
	auto &workspace = S().Workspace();
	if ( !discard && workspace.Document().IsDirty() ) {
		pendingOperation = operation; pendingPath = path;
		workspaceGui->SetStateBool( "ed_confirm", true );
		workspaceGui->StateChanged( common->GetPresentationTime() );
		workspaceGui->HandleNamedEvent( "workspaceConfirm" );
		return;
	}
	S().SaveViewState();
	std::string error;
	bool ok = operation == "new" ? workspace.New( error, discard ) : operation == "close" ? workspace.Close( discard, error ) : workspace.Open( path, error, discard );
	Result( ok, error );
	if ( ok ) {
		S().ClearSelection(); preview.reset(); workspaceGui->SetStateString( "ed_path", workspace.Path().c_str() );
		openedAt = LevelEditor_Milliseconds(); startupPending = true;
		Refresh();
	}
}

Vec3 InsertionPoint() {
	auto &s = S();
	Vec3 point = s.view.orthoCenter;
	for ( int i = 0; i < 3; ++i ) { point[i] = SnapValue( point[i], s.view.grid ); }
	return point;
}

void ToolResult( bool ok, const std::string &error, bool rebuild = true ) {
	Result( ok, error );
	if ( ok ) { Refresh( rebuild ); } else { workspaceGui->StateChanged( common->GetPresentationTime() ); }
}

void SelectProblem( int row ) {
	auto &s = S();
	if ( row < 0 || row >= static_cast<int>( problemRows.size() ) ) { return; }
	const auto &d = s.diagnostics[problemRows[row]];
	if ( !s.Document().Find( d.entity ) ) { return; }
	s.Select( { d.entity, d.primitive }, false );
	FrameSelection( false );
	Refresh( false );
}

void Action( const std::string &action ) {
	if ( !workspaceGui || session->GetActiveGUI() != workspaceGui ) { return; }
	auto &s = S();
	auto &workspace = s.Workspace();
	auto &document = workspace.Document();
	std::string error;
	if ( !pendingOperation.empty() ) {
		if ( action == "confirm" ) {
			const std::string operation = pendingOperation, path = pendingPath;
			pendingOperation.clear(); pendingPath.clear(); workspaceGui->SetStateBool( "ed_confirm", false );
			ReplaceDocument( operation, path, true );
		} else if ( action == "cancel" ) {
			pendingOperation.clear(); pendingPath.clear(); workspaceGui->SetStateBool( "ed_confirm", false );
		}
		workspaceGui->StateChanged( common->GetPresentationTime() );
		if ( pendingOperation.empty() ) { workspaceGui->HandleNamedEvent( "workspaceResume" ); }
		return;
	}
	const double started = LevelEditor_Milliseconds();
	// GUI actions carry arguments after underscores: "mode_1", "dock_browser_right".
	const auto colon = action.find( '_' );
	const std::string verb = action.substr( 0, colon ), argument = colon == std::string::npos ? "" : action.substr( colon + 1 );
	if ( verb == "new" || verb == "open" || verb == "close" ) { ReplaceDocument( verb, workspaceGui->GetStateString( "ed_path" ) ); }
	else if ( verb == "hide" ) { SyncCameraToView(); s.SaveViewState(); WorkspaceTransition transition; session->StartMenu(); preview.reset(); return; }
	else if ( verb == "save" || verb == "copy" ) {
		const bool ok = workspace.Save( workspaceGui->GetStateString( "ed_path" ), verb == "copy", error );
		if ( ok && verb == "save" ) { s.SyncDocument(); SyncCameraToView(); s.SaveViewState(); }
		ToolResult( ok, error, false );
	} else if ( verb == "undo" || verb == "redo" ) { ToolResult( verb == "undo" ? document.Undo( error ) : document.Redo( error ), error ); }
	else if ( verb == "apply" || verb == "unset" ) {
		const std::string key = workspaceGui->GetStateString( "ed_key" ), value = workspaceGui->GetStateString( "ed_value" );
		const Entity *entity = document.Find( s.PrimaryEntity() );
		PropertyType type;
		std::string problem;
		// Typed keys must hold a value of their type; raw console edits remain.
		if ( verb == "apply" && entity && PropertyTypeFor( s.ValidationOptions(), *entity, key, type ) && !ValidateValue( type, value, problem ) ) {
			error = key + ": " + problem;
			workspaceGui->SetStateString( "ed_message", va( "%s: %s", Tr( "#str_43104" ), ListText( key ).c_str() ) );
			common->Printf( "editor workspace rejected: %s\n", error.c_str() );
			workspaceGui->StateChanged( common->GetPresentationTime() );
			return;
		}
		const bool ok = verb == "apply" ? document.SetProperty( s.PrimaryEntity(), key, value, error ) : document.RemoveProperty( s.PrimaryEntity(), key, error );
		s.latency[idLevelEditorSession::LATENCY_EDIT].Add( LevelEditor_Milliseconds() - started );
		ToolResult( ok, error );
	} else if ( verb == "create" ) {
		EntityId created = 0;
		Vec3 at = InsertionPoint();
		const bool ok = document.BeginTransaction( "create entity", error ) && document.AddEntity( workspaceGui->GetStateString( "ed_class" ), workspaceGui->GetStateString( "ed_name" ), created, error ) &&
			document.SetProperty( created, "origin", FormatVector( at ), error );
		if ( ok ) { document.CommitTransaction( error ); s.Select( { created, WholeEntity }, false ); browserMode = 0; } else { document.CancelTransaction(); }
		ToolResult( ok, error );
	} else if ( verb == "delete" ) {
		const bool ok = s.editor.Delete( document, s.selection, error );
		if ( ok ) { s.ClearSelection(); }
		ToolResult( ok, error );
	} else if ( verb == "property" ) {
		const int row = workspaceGui->GetStateInt( "edProperties_sel_0", "-1" );
		const auto *entity = document.Find( s.PrimaryEntity() );
		if ( entity && row >= 0 && row < static_cast<int>( propertyRows.size() ) ) {
			workspaceGui->SetStateString( "ed_key", propertyRows[row].c_str() );
			workspaceGui->SetStateString( "ed_value", entity->Value( propertyRows[row] ).c_str() );
			KeyType();
		}
	} else if ( verb == "key" ) { KeyType(); }
	else if ( verb == "select" ) {
		const int row = workspaceGui->GetStateInt( "edBrowser_sel_0", "-1" );
		if ( row >= 0 && row < static_cast<int>( browserRows.size() ) ) {
			if ( browserMode == 0 ) {
				s.Select( { entityRows[row], WholeEntity }, false );
				s.latency[idLevelEditorSession::LATENCY_SELECT].Add( LevelEditor_Milliseconds() - started );
				Inspector(); if ( preview ) { preview->SetSelection( WholeSelection() ); }
				displayedSelection = s.selection;
			}
			else if ( browserMode == 1 ) { workspaceGui->SetStateString( "ed_path", browserRows[row].c_str() ); }
			else if ( browserMode == 2 ) { workspaceGui->SetStateString( "ed_class", browserRows[row].c_str() ); }
			else if ( browserMode == 3 ) { workspaceGui->SetStateString( "ed_material", browserRows[row].c_str() ); }
			else { workspaceGui->SetStateString( "ed_value", browserRows[row].c_str() ); }
		}
	} else if ( verb == "browse" ) {
		browserMode = idMath::ClampInt( 0, 5, atoi( argument.c_str() ) ); Browser();
	} else if ( verb == "entities" || verb == "maps" || verb == "classes" ) {
		browserMode = verb == "entities" ? 0 : verb == "maps" ? 1 : 2; Browser();
	} else if ( verb == "filter" ) { Browser(); }
	else if ( verb == "frame" ) { FrameSelection( false ); }
	else if ( verb == "all" ) { FrameSelection( true ); }
	else if ( verb == "left" && preview ) { preview->Orbit( -15, 0, 1 ); SyncCameraToView(); }
	else if ( verb == "right" && preview ) { preview->Orbit( 15, 0, 1 ); SyncCameraToView(); }
	else if ( verb == "up" && preview ) { preview->Orbit( 0, 15, 1 ); SyncCameraToView(); }
	else if ( verb == "down" && preview ) { preview->Orbit( 0, -15, 1 ); SyncCameraToView(); }
	else if ( verb == "in" && preview ) { preview->Orbit( 0, 0, 0.8f ); SyncCameraToView(); }
	else if ( verb == "out" && preview ) { preview->Orbit( 0, 0, 1.25f ); SyncCameraToView(); }
	else if ( verb == "mode" ) { s.view.mode = idMath::ClampInt( 0, 3, atoi( argument.c_str() ) ); s.view.quad = false; Layout(); }
	else if ( verb == "quad" ) { s.view.quad = !s.view.quad; Layout(); }
	else if ( verb == "gridDown" || verb == "gridUp" ) {
		s.view.grid = idMath::ClampFloat( 0.125f, 1024.0f, static_cast<float>( verb == "gridUp" ? s.view.grid * 2 : s.view.grid * 0.5 ) ); Layout();
	} else if ( verb == "lock" ) { s.view.textureLock = !s.view.textureLock; Layout(); }
	else if ( verb == "validate" ) { s.Validate(); Problems(); if ( s.layout.DockOf( DockLayout::Problems ) == DockLayout::Hidden ) { s.layout.Toggle( DockLayout::Problems ); Layout(); } }
	else if ( verb == "panels" || verb == "layout" ) { s.layout = DockLayout(); Layout(); }
	else if ( verb == "dock" ) {
		const auto split = argument.find( '_' );
		const std::string panel = argument.substr( 0, split ), dock = split == std::string::npos ? "" : argument.substr( split + 1 );
		for ( int p = 0; p < DockLayout::PanelCount; ++p ) {
			if ( panel != DockLayout::PanelName( static_cast<DockLayout::Panel>( p ) ) ) { continue; }
			for ( int d = 0; d < DockLayout::DockCount; ++d ) {
				if ( dock == DockLayout::DockName( static_cast<DockLayout::Dock>( d ) ) ) { s.layout.Move( static_cast<DockLayout::Panel>( p ), static_cast<DockLayout::Dock>( d ) ); }
			}
		}
		Layout();
	} else if ( verb == "tab" ) {
		for ( int p = 0; p < DockLayout::PanelCount; ++p ) {
			if ( argument == DockLayout::PanelName( static_cast<DockLayout::Panel>( p ) ) ) { s.layout.Activate( static_cast<DockLayout::Panel>( p ) ); }
		}
		Layout();
	} else if ( verb == "brush" || verb == "patch" ) {
		const Vec3 at = InsertionPoint();
		const double size = ( std::max )( s.view.grid * 8, 16.0 );
		SelectionItem created;
		const EntityId world = document.Entities().empty() ? 0 : document.Entities().front().id;
		const std::string material = *workspaceGui->GetStateString( "ed_material" ) ? workspaceGui->GetStateString( "ed_material" ) : "textures/common/caulk";
		const bool ok = verb == "brush" ? s.editor.CreateBrush( document, world, at - Vec3( size, size, 0 ) * 0.5, at + Vec3( size * 0.5, size * 0.5, size ), material, created, error ) :
			s.editor.CreatePatch( document, world, at - Vec3( size, size, 0 ) * 0.5, at + Vec3( size * 0.5, size * 0.5, 0 ), material, created, error );
		if ( ok ) { s.Select( created, false ); }
		ToolResult( ok, error );
	} else if ( verb == "duplicate" ) {
		Selection created;
		const bool ok = s.editor.Duplicate( document, s.selection, Vec3( s.view.grid, s.view.grid, 0 ), created, error );
		if ( ok ) { s.selection = created; }
		ToolResult( ok, error );
	} else if ( verb == "remove" ) {
		const bool ok = s.editor.Delete( document, s.selection, error );
		if ( ok ) { s.ClearSelection(); }
		ToolResult( ok, error );
	} else if ( verb == "group" ) {
		EntityId group = 0;
		const bool ok = s.editor.Group( document, s.selection, group, error );
		if ( ok ) { s.Select( { group, WholeEntity }, false ); }
		ToolResult( ok, error );
	} else if ( verb == "ungroup" ) {
		const bool ok = s.editor.Ungroup( document, s.PrimaryEntity(), error );
		if ( ok ) { s.ClearSelection(); }
		ToolResult( ok, error );
	} else if ( verb == "rotateCcw" || verb == "rotateCw" ) {
		const int axis = s.view.mode == 2 ? 1 : s.view.mode == 3 ? 0 : 2;
		ToolResult( s.Rotate( axis, verb == "rotateCcw" ? 90 : -90, error ), error );
	} else if ( verb == "snap" ) { ToolResult( s.editor.Snap( document, s.selection, s.view.grid, s.view.textureLock, error ), error ); }
	else if ( verb == "material" ) { ToolResult( s.editor.SetMaterial( document, s.selection, workspaceGui->GetStateString( "ed_material" ), error ), error ); }
	else if ( verb == "layer" ) {
		Selection layered;
		const bool ok = s.editor.SetLayer( document, s.selection, workspaceGui->GetStateString( "ed_layer" ), layered, error );
		if ( ok ) { s.selection = layered; }
		ToolResult( ok, error );
	} else if ( verb == "layerHide" || verb == "layerShow" || verb == "layerLock" || verb == "layerUnlock" || verb == "layerVisibility" || verb == "layerLocking" ) {
		const int row = workspaceGui->GetStateInt( "edLayers_sel_0", "-1" );
		const std::string layer = row >= 0 && row < static_cast<int>( layerRows.size() ) ? layerRows[row] : workspaceGui->GetStateString( "ed_layer" );
		if ( !layer.empty() ) {
			const bool hidden = s.view.hiddenLayers.count( layer ) != 0, locked = s.view.lockedLayers.count( layer ) != 0;
			if ( verb == "layerHide" || ( verb == "layerVisibility" && !hidden ) ) { s.view.hiddenLayers.insert( layer ); }
			else if ( verb == "layerShow" || verb == "layerVisibility" ) { s.view.hiddenLayers.erase( layer ); }
			else if ( verb == "layerLock" || ( verb == "layerLocking" && !locked ) ) { s.view.lockedLayers.insert( layer ); }
			else { s.view.lockedLayers.erase( layer ); }
			s.selection.erase( std::remove_if( s.selection.begin(), s.selection.end(), [&]( const SelectionItem &item ) { return !s.Selectable( item ); } ), s.selection.end() );
			Refresh( true );
		}
	} else if ( verb == "rename" ) {
		std::size_t updated = 0;
		ToolResult( s.editor.Rename( document, s.PrimaryEntity(), workspaceGui->GetStateString( "ed_rename" ), updated, error ), error );
	} else if ( verb == "prefabSave" || verb == "prefabInsert" ) {
		const std::string name = workspaceGui->GetStateString( "ed_prefab" );
		if ( verb == "prefabSave" ) { cmdSystem->BufferCommandText( CMD_EXEC_NOW, va( "mapEdit prefab save \"%s\"", name.c_str() ) ); }
		else {
			const Vec3 at = InsertionPoint();
			cmdSystem->BufferCommandText( CMD_EXEC_NOW, va( "mapEdit prefab insert \"%s\" %s", name.c_str(), FormatVector( at ).c_str() ) );
		}
		Refresh( true );
	} else if ( verb == "problem" ) { SelectProblem( workspaceGui->GetStateInt( "edProblems_sel_0", "-1" ) ); }
	else if ( verb == "build" ) {
		const bool ok = s.StartBuild( true, error );
		if ( !ok ) { Message( document.IsDirty() || workspace.Path().empty() ? "#str_42993" : "#str_42927" ); common->Printf( "editor workspace rejected: %s\n", error.c_str() ); }
		else { if ( s.layout.DockOf( DockLayout::Problems ) == DockLayout::Hidden ) { s.layout.Toggle( DockLayout::Problems ); } Layout(); Message( "#str_42926" ); }
		BuildStatus();
	} else if ( verb == "stop" ) { s.CancelBuild(); BuildStatus(); }
	else if ( verb == "play" ) {
		SyncCameraToView();
		const bool ok = s.Play( error );
		if ( !ok ) { Message( "#str_42994" ); common->Printf( "editor workspace rejected: %s\n", error.c_str() ); }
		else { Message( "#str_42992" ); }
		BuildStatus();
	}
	workspaceGui->StateChanged( common->GetPresentationTime() );
}

const char *GuiCommand( const char *commands ) {
	idCmdArgs args( commands, false );
	for ( int i = 0; i < args.Argc(); ++i ) {
		if ( !idStr::Cmp( args.Argv( i ), "editorExperimentalAction" ) && i + 1 < args.Argc() ) { Action( args.Argv( ++i ) ); }
		else if ( !idStr::Cmp( args.Argv( i ), "play" ) && i + 1 < args.Argc() ) {
			const char *sound = args.Argv( ++i );
			if ( session->menuSoundWorld ) { session->menuSoundWorld->PlayShaderDirectly( sound ); }
		}
	}
	return "";
}

// Gestures are shared by the GUI widgets and the scripted controller, so a
// test drives exactly the code a pointer would.
int ViewMode( int requested ) { return requested >= 0 ? requested : S().view.mode; }

bool PickAt( int mode, float x, float y, float width, float height, SelectionItem &hit ) {
	auto &s = S();
	const double started = LevelEditor_Milliseconds();
	const auto accept = [&s]( const SelectionItem &item ) { return s.Selectable( item ); };
	double distance = 0;
	bool found = false;
	if ( mode == 0 ) {
		if ( !preview ) { return false; }
		idVec3 start, direction;
		preview->Ray( x, y, width / height, start, direction );
		found = s.Geometry().Pick( Vec3( start.x, start.y, start.z ), Vec3( direction.x, direction.y, direction.z ), accept, hit, distance );
		float fraction = 1.0f;
		const EntityId model = preview->Pick( x, y, width / height, &fraction );
		const Entity *modelEntity = model ? s.Document().Find( model ) : nullptr;
		if ( modelEntity && !s.LayerLocked( *modelEntity ) && ( !found || fraction * 2000000.0f < distance ) ) { hit = { model, WholeEntity }; found = true; }
	} else {
		const auto frame = views::Frame( mode, s.view, width, height );
		const Vec3 point = frame.ToWorld( x, y );
		found = s.Geometry().Pick( point + frame.out * 1e6, -frame.out, accept, hit, distance );
	}
	s.latency[idLevelEditorSession::LATENCY_SELECT].Add( LevelEditor_Milliseconds() - started );
	return found;
}

void SelectHit( bool found, const SelectionItem &hit, bool add ) {
	auto &s = S();
	if ( found ) {
		// Clicking a primitive of an entity selects the entity, except world
		// and group geometry, whose primitives are edited individually.
		SelectionItem item = hit;
		const Entity *entity = s.Document().Find( item.entity );
		if ( entity && item.primitive != WholeEntity && !IsWorld( s.Document(), *entity ) && !EqualNoCase( entity->Value( "classname" ), "func_group" ) ) {
			item.primitive = WholeEntity;
		}
		s.Select( item, add );
	} else if ( !add ) { s.ClearSelection(); }
	// Selection changes only the inspector, highlights and browser row.
	Inspector( true ); KeyType();
	if ( preview ) { preview->SetSelection( WholeSelection() ); }
	int row = -1;
	if ( browserMode == 0 ) {
		for ( std::size_t i = 0; i < entityRows.size(); ++i ) { if ( entityRows[i] == s.PrimaryEntity() ) { row = static_cast<int>( i ); break; } }
		workspaceGui->SetStateInt( "edBrowser_sel_0", row );
	}
	displayedSelection = s.selection;
	workspaceGui->StateChanged( common->GetPresentationTime() );
}

Vec3 SnappedDelta( const views::OrthoFrame &frame, float x0, float y0, float x1, float y1 ) {
	const Vec3 a = frame.ToWorld( x0, y0 ), b = frame.ToWorld( x1, y1 );
	Vec3 delta = b - a;
	const double grid = S().view.grid;
	for ( int i = 0; i < 3; ++i ) { delta[i] = std::fabs( frame.out[i] ) > 0.5 ? 0 : SnapValue( delta[i], grid ); }
	return delta;
}

void EndGesture( bool commit );

}

void LevelEditor_OpenWorkspace() {
	if ( !renderSystem->IsOpenGLRunning() ) { common->Printf( "editor workspace requires a client renderer; use 'editorExperimental help' for document commands.\n" ); return; }
	WorkspaceTransition transition;
	auto &s = S();
	if ( !workspaceGui ) {
		workspaceGui = uiManager->FindGui( "guis/level_editor_experimental.gui", true, false, true );
		if ( !workspaceGui ) { common->Warning( "Could not load the level editor GUI" ); return; }
		workspaceGui->SetStateString( "ed_class", "light" );
		workspaceGui->SetStateString( "ed_name", "light_1" );
		workspaceGui->SetStateString( "ed_material", "textures/common/caulk" );
		workspaceGui->SetStateString( "ed_path", s.Workspace().Path().c_str() );
		DockLayout layout;
		if ( layout.Parse( editorExperimental_layout.GetString() ) ) { s.layout = layout; }
	}
	LevelEditor_RegisterWorkspaceCommand();
	std::string error;
	if ( !s.Workspace().Document().IsOpen() ) { s.Workspace().New( error ); }
	openedAt = LevelEditor_Milliseconds(); startupPending = true;
	console->Close(); session->SetGUI( workspaceGui, GuiCommand );
	workspaceGui->SetStateString( "ed_message", "" ); Refresh();
}

void LevelEditor_UIFrame() {
	if ( !workspaceGui ) { return; }
	if ( session->GetActiveGUI() != workspaceGui ) { if ( preview ) { SyncCameraToView(); S().SaveViewState(); } preview.reset(); return; }
	auto &s = S();
	const auto &workspace = s.Workspace();
	const bool changed = displayedRevision != workspace.Document().Revision();
	if ( previewBuilding && preview && !changed ) {
		RebuildPreview();
		workspaceGui->StateChanged( common->GetPresentationTime() );
	}
	if ( changed || displayedPath != workspace.Path() || displayedDirty != workspace.Document().IsDirty() || !preview || displayedHidden != s.view.hiddenLayers ) {
		const bool edit = changed && preview && !previewBuilding && displayedPath == workspace.Path();
		const double started = LevelEditor_Milliseconds();
		Refresh( changed || !preview || displayedHidden != s.view.hiddenLayers );
		if ( edit ) { s.latency[idLevelEditorSession::LATENCY_REFRESH].Add( LevelEditor_Milliseconds() - started ); }
	} else if ( !( displayedSelection == s.selection ) ) {
		displayedSelection = s.selection; Inspector( true ); KeyType(); if ( preview ) { preview->SetSelection( WholeSelection() ); }
		workspaceGui->StateChanged( common->GetPresentationTime() );
	}
	const MapBuild *build = s.Build();
	const int buildState = s.Playing() ? 10 : build ? static_cast<int>( build->GetState() ) : 0;
	if ( buildState != displayedBuildState || ( build && static_cast<int>( build->Progress() * 100 ) != displayedBuildPercent ) ) {
		BuildStatus();
		if ( build && !build->Active() && build->GetState() != MapBuild::State::Idle ) { s.Validate(); Problems(); }
		workspaceGui->StateChanged( common->GetPresentationTime() );
	}
	if ( s.ExternalChange() != displayedExternal ) {
		displayedExternal = s.ExternalChange();
		if ( displayedExternal ) { Message( "#str_43134" ); workspaceGui->StateChanged( common->GetPresentationTime() ); }
	}
	if ( browserMode != 0 && displayedIndex != static_cast<int>( s.assets.GetStats().entries ) ) { Browser(); workspaceGui->StateChanged( common->GetPresentationTime() ); }
	if ( preview && preview->PendingModels() > 0 && preview->UpgradeModels( editorExperimental_modelBudget.GetFloat() ) > 0 ) {
		workspaceGui->SetStateString( "ed_previewStatus", va( Tr( "#str_42925" ), preview->Triangles(), preview->Skipped() ) );
	}
}
static int editorLookupDepth = 0;
idLevelEditorLookup::idLevelEditorLookup() { ++editorLookupDepth; }
idLevelEditorLookup::~idLevelEditorLookup() { --editorLookupDepth; }
bool LevelEditor_UIIsActive() { return switchingWorkspace || editorLookupDepth > 0 || ( workspaceGui && session->GetActiveGUI() == workspaceGui ); }
void LevelEditor_UIShutdown() {
	if ( preview ) { SyncCameraToView(); }
	preview.reset(); workspaceGui = nullptr; displayedRevision = 0; displayedDirty = false; switchingWorkspace = false; browserMode = 0;
	previewBuilding = false; previewStarted = 0;
	pendingOperation.clear(); pendingPath.clear(); displayedPath.clear(); entityRows.clear(); browserRows.clear(); propertyRows.clear();
	layerRows.clear(); problemRows.clear(); displayedSelection.clear(); gesture = Gesture(); overlay = views::Overlay();
}

void LevelEditor_DrawView( idDeviceContext *dc, const idRectangle &rect, int time, int requestedMode ) {
	if ( !workspaceGui || session->GetActiveGUI() != workspaceGui ) { return; }
	const int mode = ViewMode( requestedMode );
	const double started = LevelEditor_Milliseconds();
	auto &s = S();
	if ( mode == 0 ) {
		if ( !preview ) { return; }
		preview->Draw( dc, rect, time );
		views::DrawSelection3D( dc, rect, *preview, s );
		views::DrawLabel( dc, rect, Tr( "#str_42950" ) );
		s.latency[idLevelEditorSession::LATENCY_VIEW3D].Add( LevelEditor_Milliseconds() - started );
	} else {
		// Parsing changed primitives is document work, not drawing.
		s.Geometry();
		const double drawStart = LevelEditor_Milliseconds();
		views::DrawOrtho( dc, rect, mode, s, gesture.kind == Gesture::Rubber && gesture.mode == mode ? overlay : views::Overlay() );
		s.latency[idLevelEditorSession::LATENCY_ORTHO].Add( LevelEditor_Milliseconds() - drawStart );
	}
	if ( startupPending && preview ) {
		startupPending = false;
		s.latency[idLevelEditorSession::LATENCY_STARTUP].Add( LevelEditor_Milliseconds() - openedAt );
	}
}

bool LevelEditor_UIInputEnabled() { return workspaceGui && session->GetActiveGUI() == workspaceGui && pendingOperation.empty(); }

void LevelEditor_ViewPress( int requestedMode, int button, float x, float y, float width, float height, bool shift, bool ctrl ) {
	if ( !LevelEditor_UIInputEnabled() || !std::isfinite( x + y + width + height ) || width < 1 || height < 1 ) { return; }
	if ( gesture.kind != Gesture::None ) { EndGesture( true ); }
	gesture = Gesture();
	gesture.mode = ViewMode( requestedMode ); gesture.button = button;
	gesture.startX = gesture.lastX = idMath::ClampFloat( 0, 1, x ); gesture.startY = gesture.lastY = idMath::ClampFloat( 0, 1, y );
	gesture.width = width; gesture.height = height; gesture.shift = shift; gesture.ctrl = ctrl;
	if ( button == 2 ) { gesture.kind = gesture.mode == 0 && !shift ? Gesture::Orbit : Gesture::Pan; return; }
	if ( button != 1 ) { return; }
	gesture.kind = Gesture::Pending;
	// In a flat view, pressing inside the selection's outline drags the
	// selection even when other geometry lies above it.
	auto &s = S();
	if ( gesture.mode != 0 && !s.selection.empty() && !shift && !ctrl ) {
		const auto frame = views::Frame( gesture.mode, s.view, width, height );
		const Vec3 point = frame.ToWorld( gesture.startX, gesture.startY );
		const double slack = 3 * s.view.orthoScale;
		for ( const auto &item : s.selection ) {
			Vec3 mins, maxs;
			if ( !s.editor.Bounds( s.Document(), { item }, mins, maxs ) ) { continue; }
			const double u = point.Dot( frame.right ), v = point.Dot( frame.up );
			const double u0 = ( std::min )( mins.Dot( frame.right ), maxs.Dot( frame.right ) ) - slack, u1 = ( std::max )( mins.Dot( frame.right ), maxs.Dot( frame.right ) ) + slack;
			const double v0 = ( std::min )( mins.Dot( frame.up ), maxs.Dot( frame.up ) ) - slack, v1 = ( std::max )( mins.Dot( frame.up ), maxs.Dot( frame.up ) ) + slack;
			if ( u >= u0 && u <= u1 && v >= v0 && v <= v1 ) { gesture.hit = item; gesture.hitValid = true; return; }
		}
	}
	gesture.hitValid = PickAt( gesture.mode, gesture.startX, gesture.startY, width, height, gesture.hit );
}

void LevelEditor_ViewDrag( float x, float y ) {
	if ( gesture.kind == Gesture::None || !LevelEditor_UIInputEnabled() || !std::isfinite( x + y ) ) { return; }
	auto &s = S();
	x = idMath::ClampFloat( -1, 2, x ); y = idMath::ClampFloat( -1, 2, y );
	const float dx = x - gesture.lastX, dy = y - gesture.lastY;
	gesture.lastX = x; gesture.lastY = y;
	const auto frame = views::Frame( gesture.mode, s.view, gesture.width, gesture.height );
	if ( gesture.kind == Gesture::Orbit && preview ) { preview->Orbit( -dx * gesture.width * 0.6f, -dy * gesture.height * 0.6f, 1 ); SyncCameraToView(); return; }
	if ( gesture.kind == Gesture::Pan ) {
		if ( gesture.mode == 0 ) { if ( preview ) { preview->Pan( dx, dy * gesture.height / gesture.width ); SyncCameraToView(); } }
		else { s.view.orthoCenter = s.view.orthoCenter - frame.right * ( dx * gesture.width * s.view.orthoScale ) + frame.up * ( dy * gesture.height * s.view.orthoScale ); }
		return;
	}
	const float moved = std::fabs( x - gesture.startX ) * gesture.width + std::fabs( y - gesture.startY ) * gesture.height;
	if ( gesture.kind == Gesture::Pending && moved >= 3 && gesture.mode != 0 ) {
		std::string error;
		if ( gesture.ctrl || !gesture.hitValid ) {
			gesture.kind = Gesture::Rubber;
		} else {
			// Dragging an unselected item selects it first, as in other editors.
			if ( !s.IsSelected( gesture.hit ) ) { SelectHit( true, gesture.hit, gesture.shift ); }
			if ( !s.Document().BeginTransaction( "move", error ) ) { gesture.kind = Gesture::None; return; }
			gesture.kind = Gesture::Move;
		}
	}
	if ( gesture.kind == Gesture::Move ) {
		// Re-apply the whole offset each time: one undo step, no drift.
		std::string error;
		s.Document().RevertTransaction();
		const Vec3 delta = SnappedDelta( frame, gesture.startX, gesture.startY, x, y );
		if ( delta.x != 0 || delta.y != 0 || delta.z != 0 ) { s.Translate( delta, error ); }
	} else if ( gesture.kind == Gesture::Rubber ) {
		overlay.rubber = true;
		overlay.a = frame.ToWorld( gesture.startX, gesture.startY );
		overlay.b = frame.ToWorld( x, y );
		for ( int i = 0; i < 3; ++i ) { overlay.a[i] = SnapValue( overlay.a[i], s.view.grid ); overlay.b[i] = SnapValue( overlay.b[i], s.view.grid ); }
	}
}

namespace {
void EndGesture( bool commit ) {
	auto &s = S();
	std::string error;
	const Gesture ended = gesture;
	gesture = Gesture();
	if ( ended.kind == Gesture::Pending ) {
		SelectHit( ended.hitValid, ended.hit, ended.shift );
	} else if ( ended.kind == Gesture::Move ) {
		if ( commit ) { s.Document().CommitTransaction( error ); } else { s.Document().CancelTransaction(); }
	} else if ( ended.kind == Gesture::Rubber && commit ) {
		// A new brush spans the drawn rectangle and one depth step around the
		// view's centre along the axis the view looks down.
		const auto frame = views::Frame( ended.mode, s.view, ended.width, ended.height );
		Vec3 mins, maxs;
		const double depth = ( std::max )( s.view.grid * 8, 8.0 );
		for ( int i = 0; i < 3; ++i ) {
			if ( std::fabs( frame.out[i] ) > 0.5 ) {
				mins[i] = SnapValue( s.view.orthoCenter[i], s.view.grid ) - ( i == 2 ? 0 : depth * 0.5 );
				maxs[i] = mins[i] + depth;
			} else {
				mins[i] = ( std::min )( overlay.a[i], overlay.b[i] ); maxs[i] = ( std::max )( overlay.a[i], overlay.b[i] );
			}
		}
		SelectionItem created;
		const char *material = workspaceGui->GetStateString( "ed_material" );
		if ( s.editor.CreateBrush( s.Document(), s.Document().Entities().front().id, mins, maxs, *material ? material : "textures/common/caulk", created, error ) ) {
			s.Select( created, false );
		} else {
			common->Printf( "editor workspace rejected: %s\n", error.c_str() );
		}
	}
	overlay = views::Overlay();
	if ( workspaceGui ) { Refresh( false ); }
}
}

void LevelEditor_ViewRelease( float x, float y ) {
	if ( gesture.kind == Gesture::None ) { return; }
	LevelEditor_ViewDrag( x, y );
	EndGesture( true );
}

void LevelEditor_ViewCancel() {
	if ( gesture.kind != Gesture::None ) { EndGesture( false ); }
}

void LevelEditor_ViewWheel( int requestedMode, float x, float y, float width, float height, int direction ) {
	if ( !LevelEditor_UIInputEnabled() || direction == 0 ) { return; }
	auto &s = S();
	const int mode = ViewMode( requestedMode );
	const float factor = direction > 0 ? 0.8f : 1.25f;
	if ( mode == 0 ) { if ( preview ) { preview->Orbit( 0, 0, factor ); SyncCameraToView(); } return; }
	// Zoom about the cursor: the world point under it stays put.
	const auto before = views::Frame( mode, s.view, width, height );
	const Vec3 anchor = before.ToWorld( x, y );
	s.view.orthoScale = idMath::ClampFloat( 1.0f / 64.0f, 512.0f, static_cast<float>( s.view.orthoScale * factor ) );
	const auto after = views::Frame( mode, s.view, width, height );
	s.view.orthoCenter = s.view.orthoCenter + ( anchor - after.ToWorld( x, y ) );
}

void LevelEditor_ViewKey( int requestedMode, int key ) {
	if ( !LevelEditor_UIInputEnabled() ) { return; }
	auto &s = S();
	const int mode = ViewMode( requestedMode );
	std::string error;
	if ( key == 'f' ) { FrameSelection( false ); return; }
	if ( key == K_DEL || key == K_BACKSPACE ) { if ( s.editor.Delete( s.Document(), s.selection, error ) ) { s.ClearSelection(); } Refresh( true ); return; }
	if ( key == K_ESCAPE ) { LevelEditor_ViewCancel(); return; }
	if ( mode == 0 ) { return; }
	const auto frame = views::Frame( mode, s.view, 1, 1 );
	Vec3 delta;
	if ( key == K_LEFTARROW ) { delta = frame.right * -s.view.grid; }
	else if ( key == K_RIGHTARROW ) { delta = frame.right * s.view.grid; }
	else if ( key == K_UPARROW ) { delta = frame.up * s.view.grid; }
	else if ( key == K_DOWNARROW ) { delta = frame.up * -s.view.grid; }
	else { return; }
	if ( !s.Translate( delta, error ) ) { common->Printf( "editor workspace rejected: %s\n", error.c_str() ); }
}

void LevelEditor_ResizePane( int pane, float delta ) {
	if ( !LevelEditor_UIInputEnabled() || !std::isfinite( delta ) ) { return; }
	auto &layout = S().layout;
	if ( pane == 1 ) { layout.Resize( DockLayout::Left, delta ); }
	else if ( pane == 2 ) { layout.Resize( DockLayout::Right, -delta ); }
	else if ( pane == 3 ) { layout.Resize( DockLayout::Bottom, -delta ); }
	Layout(); workspaceGui->StateChanged( common->GetPresentationTime() );
}

namespace {

// Measures every visible single-line label of the workspace against its
// rectangle in the current language and resolution.
int CheckTextFit( bool verbose ) {
	auto *gui = static_cast<idUserInterfaceLocal *>( workspaceGui );
	if ( !gui || !gui->GetDesktop() ) { return -1; }
	int overflows = 0, checked = 0;
	idLevelEditorWindow::MeasureLabels( gui->GetDesktop(), [&]( const char *name, const char *text, float needed, float available ) {
		++checked;
		if ( needed > available + 0.5f ) {
			++overflows;
			if ( verbose ) { common->Printf( "editor fit: overflow %s needs %.1f has %.1f: %s\n", name, needed, available, text ); }
		}
	} );
	common->Printf( "editor fit: language=%s checked=%d overflows=%d size=%dx%d\n", cvarSystem->GetCVarString( "sys_lang" ), checked, overflows,
		engineWindowState.vidWidth, engineWindowState.vidHeight );
	return overflows;
}

bool KeyByName( const char *name, int &key ) {
	key = idKeyInput::StringToKeyNum( name );
	return key >= 0;
}

void WorkspaceCommand( const idCmdArgs &args ) {
	// Scriptable workspace actions use the same controller as GUI widgets.
	// "event" delivers engine events to this GUI only; it never reaches the
	// operating system or another window.
	if ( !workspaceGui || session->GetActiveGUI() != workspaceGui ) { common->Printf( "Open 'editorExperimental' first.\n" ); return; }
	auto &s = S();
	const auto number = [&args]( int index, float &value ) {
		char *end = nullptr;
		value = std::strtof( args.Argv( index ), &end );
		return end && end != args.Argv( index ) && !*end && std::isfinite( value );
	};
	if ( args.Argc() == 4 && idStr::Cmp( args.Argv(1), "field" ) == 0 ) {
		const char *fields[] = { "ed_path", "ed_filter", "ed_key", "ed_value", "ed_class", "ed_name", "ed_material", "ed_layer", "ed_rename", "ed_prefab",
			"edBrowser_sel_0", "edProperties_sel_0", "edLayers_sel_0", "edProblems_sel_0" };
		for ( const char *field : fields ) {
			if ( !idStr::Cmp( args.Argv(2), field ) ) { workspaceGui->SetStateString( field, args.Argv(3) ); workspaceGui->StateChanged( common->GetPresentationTime() ); return; }
		}
	} else if ( args.Argc() == 3 && idStr::Cmp( args.Argv(1), "action" ) == 0 ) { Action( args.Argv(2) ); return; }
	else if ( args.Argc() == 2 && idStr::Cmp( args.Argv(1), "status" ) == 0 ) {
		common->Printf( "editor workspace: rows=%d selected=%zu primary=%llu mode=%d quad=%d grid=%g lock=%d layout=%s confirm=%d key=%s browser=%d problems=%zu models=%d pending=%d\n",
			workspaceGui->GetStateInt( "edBrowser_num" ), s.selection.size(), static_cast<unsigned long long>( s.PrimaryEntity() ), s.view.mode, s.view.quad,
			s.view.grid, s.view.textureLock, s.layout.Serialize().c_str(), !pendingOperation.empty(), workspaceGui->GetStateString( "ed_key" ), browserMode,
			s.diagnostics.size(), preview ? preview->Models() : 0, preview ? preview->PendingModels() : 0 );
		common->Printf( "editor workspace camera: target=%s yaw=%g pitch=%g distance=%g ortho=%s scale=%g\n", FormatVector( s.view.target ).c_str(), s.view.yaw,
			s.view.pitch, s.view.distance, FormatVector( s.view.orthoCenter ).c_str(), s.view.orthoScale );
		return;
	} else if ( args.Argc() >= 2 && idStr::Cmp( args.Argv(1), "fit" ) == 0 ) { CheckTextFit( args.Argc() == 3 ); return; }
	else if ( args.Argc() == 2 && idStr::Cmp( args.Argv(1), "latency" ) == 0 ) { s.PrintLatency(); return; }
	else if ( args.Argc() >= 3 && idStr::Cmp( args.Argv(1), "event" ) == 0 ) {
		sysEvent_t event = {};
		float x, y;
		if ( args.Argc() == 5 && idStr::Cmp( args.Argv(2), "move" ) == 0 && number( 3, x ) && number( 4, y ) ) {
			event.evType = SE_MOUSE;
			event.evValue = idMath::Ftoi( x - workspaceGui->CursorX() );
			event.evValue2 = idMath::Ftoi( y - workspaceGui->CursorY() );
		} else if ( args.Argc() == 5 && idStr::Cmp( args.Argv(2), "key" ) == 0 ) {
			int key;
			if ( !KeyByName( args.Argv(3), key ) ) { common->Printf( "editor workspace rejected: unknown key\n" ); return; }
			event.evType = SE_KEY; event.evValue = key; event.evValue2 = atoi( args.Argv(4) ) != 0;
			idKeyInput::PreliminaryKeyEvent( key, event.evValue2 != 0 );
		} else { common->Printf( "editor workspace rejected: event move <x> <y> | key <name> <0|1>\n" ); return; }
		bool updateVisuals = false;
		workspaceGui->HandleEvent( &event, common->GetPresentationTime(), &updateVisuals );
		return;
	} else if ( args.Argc() >= 5 && idStr::Cmp( args.Argv(1), "view" ) == 0 ) {
		float values[6] = {};
		for ( int i = 3; i < args.Argc() && i < 9; ++i ) { if ( !number( i, values[i - 3] ) ) { common->Printf( "editor workspace rejected: invalid view argument\n" ); return; } }
		const char *verb = args.Argv(2);
		// Legacy 3D controls.
		if ( args.Argc() == 6 && !idStr::Cmp( verb, "orbit" ) ) { if ( preview && values[2] > 0 && values[2] <= 10 ) { preview->Orbit( values[0], values[1], values[2] ); SyncCameraToView(); } return; }
		if ( args.Argc() == 6 && !idStr::Cmp( verb, "pick" ) ) {
			SelectionItem hit;
			const bool found = values[0] >= 0 && values[0] <= 1 && values[1] >= 0 && values[1] <= 1 && values[2] > 0.05f && PickAt( 0, values[0], values[1], 200 * values[2], 200, hit );
			SelectHit( found, hit, false );
			return;
		}
		if ( args.Argc() == 5 && !idStr::Cmp( verb, "pan" ) ) { if ( preview ) { preview->Pan( idMath::ClampFloat( -1, 1, values[0] ), idMath::ClampFloat( -1, 1, values[1] ) ); SyncCameraToView(); } return; }
		if ( args.Argc() == 5 && !idStr::Cmp( verb, "panes" ) ) {
			const float left = values[0] - s.layout.Size( DockLayout::Left ), right = s.layout.Size( DockLayout::Right ) - values[1];
			LevelEditor_ResizePane( 1, left ); LevelEditor_ResizePane( 2, right ); return;
		}
		// Pointer gestures in a view: mode, normalized coordinates, and
		// flags (1 shift, 2 ctrl), using the current view rectangle size.
		const int mode = static_cast<int>( values[0] );
		const float width = s.view.quad ? workspaceGui->GetStateFloat( "ed_view_w" ) * 0.5f : workspaceGui->GetStateFloat( "ed_view_w" );
		const float height = s.view.quad ? workspaceGui->GetStateFloat( "ed_view_h" ) * 0.5f : workspaceGui->GetStateFloat( "ed_view_h" );
		if ( args.Argc() >= 6 && args.Argc() <= 7 && !idStr::Cmp( verb, "click" ) ) {
			const int flags = args.Argc() == 7 ? static_cast<int>( values[3] ) : 0;
			LevelEditor_ViewPress( mode, 1, values[1], values[2], width, height, ( flags & 1 ) != 0, ( flags & 2 ) != 0 );
			LevelEditor_ViewRelease( values[1], values[2] );
			return;
		}
		if ( args.Argc() >= 8 && args.Argc() <= 9 && !idStr::Cmp( verb, "drag" ) ) {
			const int flags = args.Argc() == 9 ? static_cast<int>( values[5] ) : 0;
			LevelEditor_ViewPress( mode, ( flags & 4 ) ? 2 : 1, values[1], values[2], width, height, ( flags & 1 ) != 0, ( flags & 2 ) != 0 );
			for ( int step = 1; step <= 8; ++step ) {
				const float t = step / 8.0f;
				LevelEditor_ViewDrag( values[1] + ( values[3] - values[1] ) * t, values[2] + ( values[4] - values[2] ) * t );
			}
			if ( flags & 8 ) { LevelEditor_ViewCancel(); } else { LevelEditor_ViewRelease( values[3], values[4] ); }
			return;
		}
		if ( args.Argc() == 7 && !idStr::Cmp( verb, "wheel" ) ) { LevelEditor_ViewWheel( mode, values[1], values[2], width, height, static_cast<int>( values[3] ) ); return; }
		if ( args.Argc() == 5 && !idStr::Cmp( verb, "key" ) ) {
			int key;
			if ( KeyByName( args.Argv(4), key ) ) { LevelEditor_ViewKey( mode, key ); }
			return;
		}
	}
	common->Printf( "editorExperimentalWorkspace field <field> <value> | action <action> | status | fit [verbose] | latency |"
		" event move <x> <y> | event key <name> <0|1> | view orbit|pan|pick|panes|click|drag|wheel|key ...\n" );
}

struct CommandRegistration {
	CommandRegistration() = default;
	void Ensure() {
		if ( registered ) { return; }
		registered = true;
		cmdSystem->AddCommand( "editorExperimentalWorkspace", WorkspaceCommand, CMD_FL_TOOL, "level editor workspace actions" );
	}
	bool registered = false;
} registration;

}

void LevelEditor_RegisterWorkspaceCommand() { registration.Ensure(); }
void LevelEditor_FrameSelection() { if ( LevelEditor_UIInputEnabled() ) { FrameSelection( false ); } }
