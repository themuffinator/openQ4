// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENQ4_LEVEL_EDITOR_WINDOW_H
#define OPENQ4_LEVEL_EDITOR_WINDOW_H

#include "../framework/LevelEditor.h"

#include <functional>

// A normal GUI widget: device-context clipping/aspect and session event routing
// remain shared with the shipped UI. It owns no global window or GL context.
// "viewmode" picks a fixed view (0-3) or -1 to follow the workspace mode;
// "splitter" 1-3 makes it a dock splitter instead.
class idLevelEditorWindow : public idWindow {
public:
	explicit idLevelEditorWindow( idUserInterfaceLocal *gui ) : idWindow( gui ) {}
	void Draw( int time, float x, float y ) override {
		if ( !pane ) { LevelEditor_DrawView( dc, drawRect, time, viewMode ); }
	}
	void PostParse() override;
	void Activate( bool activate, idStr &act ) override;
	const char *HandleEvent( const sysEvent_t *event, bool *updateVisuals ) override;
	const char *RouteMouseCoords( float xd, float yd ) override;
	void LoseCapture() override;
	// Visits every visible single-line text window below root with the width
	// its text needs and the width its rectangle offers, in GUI units.
	static void MeasureLabels( idWindow *root, const std::function<void( const char *name, const char *text, float needed, float available )> &visit );
private:
	bool ParseInternalVar( const char *name, idParser *src ) override;
	void Normalized( float &x, float &y ) const;
	int pane = 0, viewMode = -1, dragMode = 0;
	float lastX = 0, lastY = 0;
};

#endif
