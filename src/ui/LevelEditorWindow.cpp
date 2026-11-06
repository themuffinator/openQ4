// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Window.h"
#include "UserInterfaceLocal.h"
#include "EditWindow.h"
#include "ListWindow.h"
#include "LevelEditorWindow.h"

void idLevelEditorWindow::PostParse() {
	idWindow::PostParse();
	flags |= WIN_CANFOCUS;
	if ( pane ) { cursor = idDeviceContext::CURSOR_HAND; }
}

void idLevelEditorWindow::Activate( bool activate, idStr &act ) {
	// ESC can switch menus during a drag, before this GUI gets the button-up.
	// Reopening must not resume that stale drag or swallow subsequent hover.
	if ( GetCaptureChild() == this ) { gui->GetDesktop()->SetCapture( nullptr ); }
	if ( dragMode == 2 ) { LevelEditor_ViewCancel(); }
	dragMode = 0;
	idWindow::Activate( activate, act );
}

bool idLevelEditorWindow::ParseInternalVar( const char *name, idParser *src ) {
	if ( !idStr::Icmp( name, "splitter" ) ) { pane = idMath::ClampInt( 0, 3, src->ParseInt() ); return true; }
	if ( !idStr::Icmp( name, "viewmode" ) ) { viewMode = idMath::ClampInt( -1, 3, src->ParseInt() ); return true; }
	return idWindow::ParseInternalVar( name, src );
}

void idLevelEditorWindow::Normalized( float &x, float &y ) const {
	x = drawRect.w > 0 ? ( gui->CursorX() - actualX ) / drawRect.w : 0;
	y = drawRect.h > 0 ? ( gui->CursorY() - actualY ) / drawRect.h : 0;
}

const char *idLevelEditorWindow::HandleEvent( const sysEvent_t *event, bool *updateVisuals ) {
	if ( !LevelEditor_UIInputEnabled() || event->evType != SE_KEY ) { return ""; }
	const int key = event->evValue;
	const bool down = event->evValue2 != 0;
	const bool inside = Contains( drawRect, gui->CursorX(), gui->CursorY() );
	if ( updateVisuals ) { *updateVisuals = true; }
	if ( pane ) {
		if ( key == K_MOUSE1 && down && inside ) { dragMode = 1; lastX = gui->CursorX(); lastY = gui->CursorY(); SetCapture( this ); }
		else if ( key == K_MOUSE1 && !down ) { dragMode = 0; }
		return "";
	}
	float x, y;
	Normalized( x, y );
	if ( !down ) {
		if ( dragMode == 2 && ( key == K_MOUSE1 || key == K_MOUSE2 ) ) { dragMode = 0; LevelEditor_ViewRelease( x, y ); }
		return "";
	}
	if ( ( key == K_MOUSE1 || key == K_MOUSE2 ) && inside && drawRect.w > 0 && drawRect.h > 0 ) {
		LevelEditor_ViewPress( viewMode, key == K_MOUSE1 ? 1 : 2, x, y, drawRect.w, drawRect.h, idKeyInput::IsDown( K_SHIFT ), idKeyInput::IsDown( K_CTRL ) );
		dragMode = 2; lastX = gui->CursorX(); lastY = gui->CursorY(); SetCapture( this );
	} else if ( ( key == K_MWHEELUP || key == K_MWHEELDOWN ) && inside ) {
		LevelEditor_ViewWheel( viewMode, x, y, drawRect.w, drawRect.h, key == K_MWHEELUP ? 1 : -1 );
	} else if ( key < K_MOUSE1 || key > K_MOUSE8 ) {
		LevelEditor_ViewKey( viewMode, key );
	}
	return "";
}

const char *idLevelEditorWindow::RouteMouseCoords( float xd, float yd ) {
	if ( !( flags & WIN_CAPTURE ) || !dragMode || !LevelEditor_UIInputEnabled() ) { return ""; }
	const float dx = gui->CursorX() - lastX;
	lastX = gui->CursorX(); lastY = gui->CursorY();
	if ( pane ) {
		// Bottom splitters move vertically; side splitters horizontally.
		LevelEditor_ResizePane( pane, pane == 3 ? gui->CursorY() - ( actualY + drawRect.h * 0.5f ) : dx );
		return "";
	}
	float x, y;
	Normalized( x, y );
	LevelEditor_ViewDrag( x, y );
	return "";
}

void idLevelEditorWindow::LoseCapture() {
	// The desktop releases capture on button-up; finish the gesture there.
	if ( dragMode == 2 ) {
		float x, y;
		Normalized( x, y );
		LevelEditor_ViewRelease( x, y );
	}
	dragMode = 0;
	idWindow::LoseCapture();
}

void idLevelEditorWindow::MeasureLabels( idWindow *root, const std::function<void( const char *, const char *, float, float )> &visit ) {
	if ( !root ) { return; }
	for ( int i = 0; i < root->GetChildCount(); ++i ) {
		idWindow *window = root->GetChild( i );
		if ( !window || !window->visible ) { continue; }
		const bool data = window->text.GetName()[0] != '\0'; // bound to gui:: state: map or log data
		const bool control = dynamic_cast<idEditWindow *>( window ) || dynamic_cast<idListWindow *>( window ) || dynamic_cast<idLevelEditorWindow *>( window );
		if ( !data && !control && window->text.Length() > 0 && window->textRect.w > 0 ) {
			window->SetFont();
			const float scale = window->textScale;
			const int adjust = static_cast<int>( window->textspacing );
			const float line = static_cast<float>( window->dc->MaxCharHeight( scale ) );
			if ( ( window->flags & WIN_NOWRAP ) || window->textRect.h < line * 2.0f ) {
				visit( window->GetName(), window->text, static_cast<float>( window->dc->TextWidth( window->text, scale, -1, adjust ) ), window->textRect.w );
			} else {
				idList<int> breaks;
				window->dc->DrawText( window->text, scale, window->textAlign, window->foreColor, window->textRect, true, -1, true, &breaks, 0, adjust );
				visit( window->GetName(), window->text, ( breaks.Num() + 1 ) * line, window->textRect.h );
			}
		}
		MeasureLabels( window, visit );
	}
}
