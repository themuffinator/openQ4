// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENQ4_LEVEL_EDITOR_H
#define OPENQ4_LEVEL_EDITOR_H

void LevelEditor_InitCommands();
void LevelEditor_Frame();
void LevelEditor_Shutdown();

namespace oq4editor { class MapWorkspace; }
class idDeviceContext;
class idRectangle;
oq4editor::MapWorkspace &LevelEditor_Workspace();
void LevelEditor_OpenWorkspace();
void LevelEditor_RegisterWorkspaceCommand();
void LevelEditor_UIFrame();
bool LevelEditor_UIIsActive();
// Marks the editor's on-demand declaration and model loads (entityDef
// schemas, entity models) as tool work, like the open workspace, so they are
// not reported as gameplay loads that missed the level precache.
class idLevelEditorLookup {
public:
	idLevelEditorLookup();
	~idLevelEditorLookup();
	idLevelEditorLookup( const idLevelEditorLookup & ) = delete;
	idLevelEditorLookup &operator=( const idLevelEditorLookup & ) = delete;
};
void LevelEditor_UIShutdown();
bool LevelEditor_UIInputEnabled();

// Views: mode -1 follows the workspace's view mode; 0 is the 3D preview,
// 1-3 the top, front and side wireframes. Pointer positions are
// normalized to the view rectangle, whose size is in GUI units.
void LevelEditor_DrawView( idDeviceContext *dc, const idRectangle &rect, int time, int mode );
void LevelEditor_ViewPress( int mode, int button, float x, float y, float width, float height, bool shift, bool ctrl );
void LevelEditor_ViewDrag( float x, float y );
void LevelEditor_ViewRelease( float x, float y );
void LevelEditor_ViewCancel();
void LevelEditor_ViewWheel( int mode, float x, float y, float width, float height, int direction );
void LevelEditor_ViewKey( int mode, int key );
// Splitters: 1 left dock, 2 right dock, 3 bottom dock.
void LevelEditor_ResizePane( int pane, float delta );
void LevelEditor_FrameSelection();

#endif
