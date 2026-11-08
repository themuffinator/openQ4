// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENQ4_LEVEL_EDITOR_VIEWS_H
#define OPENQ4_LEVEL_EDITOR_VIEWS_H

#include "LevelEditorSession.h"

class idDeviceContext;
class idRectangle;
class idLevelEditorPreview;

// Orthographic views are 2D wireframes drawn through the GUI device context,
// so they behave the same on every renderer backend. Mode 1 looks down -Z
// (top), 2 along +Y (front), 3 along -X (side).
namespace leveleditorviews {

struct OrthoFrame {
	oq4editor::Vec3 right, up, out; // screen right/up in world, out of the screen
	oq4editor::Vec3 center;
	double scale = 4; // world units per virtual pixel
	float width = 1, height = 1;   // view size in virtual pixels
	oq4editor::Vec3 ToWorld( float nx, float ny ) const; // normalized view coords
	void ToScreen( const oq4editor::Vec3 &p, float &x, float &y ) const; // offsets in virtual px
};
OrthoFrame Frame( int mode, const oq4editor::ViewState &view, float width, float height );

struct Overlay {
	bool rubber = false;
	oq4editor::Vec3 a, b; // world corners of a brush being drawn
};

struct DrawStats { int primitives = 0, outlines = 0, boxes = 0, points = 0, lines = 0; };

DrawStats DrawOrtho( idDeviceContext *dc, const idRectangle &rect, int mode, idLevelEditorSession &session, const Overlay &overlay );
// Wireframes of selected primitives on top of the rendered 3D view.
int DrawSelection3D( idDeviceContext *dc, const idRectangle &rect, const idLevelEditorPreview &preview, idLevelEditorSession &session );
void DrawLabel( idDeviceContext *dc, const idRectangle &rect, const char *text );

}

#endif
