// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

// The pointer the retained menus and the console draw: vector art in the
// stock guicursor_arrow's style (black outline, lit grey facets, soft shadow),
// and an I-beam in the same style over text a click would edit.
enum pointerCursor_t {
	POINTER_ARROW,
	POINTER_TEXT
};

// Draws the pointer's hotspot (the arrow's tip, the I-beam's centre) at
// (x, y) pixels of the frame the 2D pass's 640x480 screen spans, frameWidth by
// frameHeight pixels. `unit` is the pixels in a texel of the stock 32-texel
// cursor. The caller owns the 2D viewport mode and flushes the GUI model.
void UI_DrawPointerCursor( pointerCursor_t shape, float x, float y, float frameWidth, float frameHeight, float unit );
