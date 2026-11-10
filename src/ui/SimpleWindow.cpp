/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).  

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/




#include "DeviceContext.h"
#include "Window.h"
#include "UserInterfaceLocal.h"
#include "RetainedHud.h"
#include "LegacyGuiLayout.h"
#include "AudioGraph.h"
#include "SimpleWindow.h"

namespace {
static bool IsFullscreenBackdropRect( const idRectangle &drawRect ) {
	const float epsilon = 0.01f;
	if ( idMath::Fabs( drawRect.x ) > epsilon || idMath::Fabs( drawRect.y ) > epsilon ||
		 idMath::Fabs( drawRect.w - 640.0f ) > epsilon ) {
		return false;
	}
	// Some intro videos are authored as 640x479.
	return idMath::Fabs( drawRect.h - 480.0f ) <= 1.01f;
}

static bool ShouldDrawCinematicUnderlay( const idMaterial *background, const idRectangle &drawRect ) {
	if ( background == NULL || !IsFullscreenBackdropRect( drawRect ) ) {
		return false;
	}

	const char *materialName = background->GetName();
	const bool isVideoMaterial = ( materialName != NULL && idStr::Icmpn( materialName, "video/", 6 ) == 0 );
	return isVideoMaterial || ( background->CinematicLength() > 0 );
}

static bool ShouldDrawSplashUnderlay( const idMaterial *background, const idRectangle &drawRect ) {
	if ( background == NULL || !IsFullscreenBackdropRect( drawRect ) ) {
		return false;
	}

	const char *materialName = background->GetName();
	if ( materialName == NULL || materialName[0] == '\0' ) {
		return false;
	}

	return idStr::Icmp( materialName, "gfx/guis/mainmenu/splash" ) == 0 ||
		idStr::Icmp( materialName, "gfx/guis/mainmenu/splash.dds" ) == 0 ||
		idStr::Icmp( materialName, "gfx/splashScreen" ) == 0 ||
		idStr::Icmp( materialName, "gfx/guis/loadscreens/generic" ) == 0 ||
		idStr::Icmp( materialName, "gfx/guis/loadscreens/generic.dds" ) == 0;
}

static void DrawCinematicUnderlay( idDeviceContext *dc ) {
	const bool previousUIViewportMode = renderSystem->GetUseUIViewportFor2D();
	renderSystem->SetUseUIViewportFor2D( false );
	renderSystem->SetColor( colorBlack );
	renderSystem->DrawStretchPic( 0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT, 0.0f, 0.0f, 1.0f, 1.0f, declManager->FindMaterial( "_white" ) );
	renderSystem->SetColor( colorWhite );
	renderSystem->SetUseUIViewportFor2D( previousUIViewportMode );
}

static void DrawSplashUnderlay( idDeviceContext *dc ) {
	const idVec4 underlayColor( 24.0f / 255.0f, 26.0f / 255.0f, 8.0f / 255.0f, 1.0f );
	dc->DrawFilledRect( 0.0f, 0.0f, 640.0f, 480.0f, underlayColor );
}

static bool IsLegacyVehicleDamageOverlay( const idUserInterfaceLocal *gui, const char *windowName, const idMaterial *background, const idRectangle &drawRect ) {
	if ( gui == NULL || background == NULL || windowName == NULL || !IsFullscreenBackdropRect( drawRect ) ) {
		return false;
	}

	if ( idStr::Icmp( windowName, "damage" ) != 0 ) {
		return false;
	}

	const char *materialName = background->GetName();
	if ( materialName == NULL || idStr::Icmp( materialName, "gfx/guis/common/add_box2" ) != 0 ) {
		return false;
	}

	const char *sourceFile = gui->GetSourceFile();
	return idStr::Icmp( sourceFile, "guis/vehicles/hud.gui" ) == 0 ||
		idStr::Icmp( sourceFile, "guis/vehicles/medchange.gui" ) == 0;
}

static bool ShouldDrawNativeScreenOverlay( const idUserInterfaceLocal *gui, const char *windowName, const idMaterial *background, const idRectangle &drawRect, int flags ) {
	if ( ( flags & WIN_NATIVESCREENOVERLAY ) != 0 ) {
		return IsFullscreenBackdropRect( drawRect );
	}

	return IsLegacyVehicleDamageOverlay( gui, windowName, background, drawRect );
}

static void DrawNativeScreenOverlay( const idMaterial *background, const idVec4 &matColor ) {
	if ( background == NULL ) {
		return;
	}

	const bool previousUIViewportMode = renderSystem->GetUseUIViewportFor2D();
	renderSystem->SetUseUIViewportFor2D( false );
	renderSystem->SetColor( matColor );
	renderSystem->DrawStretchPic( 0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT, 0.0f, 0.0f, 1.0f, 1.0f, background );
	renderSystem->SetColor( colorWhite );
	renderSystem->SetUseUIViewportFor2D( previousUIViewportMode );
}

static void AdjustTournamentWarmupLayout( const idUserInterfaceLocal *gui, const char *windowName, const char *parentName, float xofs, float xExpand, idRectangle &drawRect ) {
	if ( gui == NULL || windowName == NULL || xExpand <= 0.0f ) {
		return;
	}

	if ( idStr::Icmp( gui->GetSourceFile(), "guis/mphud.gui" ) != 0 ) {
		return;
	}

	// Stretch the tournament pre-game bar across the native render width while
	// keeping its title centered in the middle 640-wide virtual HUD canvas.
	if ( idStr::Icmp( windowName, "tourn_warmupbar" ) == 0 || idStr::Icmp( windowName, "d_tourn_warmupbar" ) == 0 ) {
		drawRect.x = xofs - xExpand;
		drawRect.w = 640.0f + ( xExpand * 2.0f );
		return;
	}

	if ( parentName != NULL && idStr::Icmp( parentName, "tourn_warmupbar" ) == 0 &&
		 idStr::Icmp( windowName, "tourn_warmup_title" ) == 0 ) {
		drawRect.x = xofs + xExpand;
		drawRect.w = 640.0f;
	}
}
}


idSimpleWindow::idSimpleWindow(idWindow *win) {
	gui = win->GetGui();
	dc = win->dc;
	drawRect = win->drawRect;
	clientRect = win->clientRect;
	textRect = win->textRect;
	origin = win->origin;
	fontNum = win->fontNum;
	name = win->name;
	matScalex = win->matScalex;
	matScaley = win->matScaley;
	borderSize = win->borderSize;
	textAlign = win->textAlign;
	textAlignx = win->textAlignx;
	textAligny = win->textAligny;
	forceAspectWidth = win->forceAspectWidth;
	forceAspectHeight = win->forceAspectHeight;
	textSpacing = static_cast<int>( win->textspacing );
	textStyle = static_cast<signed char>( static_cast<int>( win->textstyle ) );
	screenAlignX = win->screenAlignX;
	screenAlignY = win->screenAlignY;
	background = win->background;
	flags = win->flags;
	textShadow = win->textShadow;

	visible = win->visible;
	text = win->text;
	rect = win->rect;
	backColor = win->backColor;
	matColor = win->matColor;
	foreColor = win->foreColor;
	borderColor = win->borderColor;
	textScale = win->textScale;
	rotate = win->rotate;
	shear = win->shear;
	backGroundName = win->backGroundName;
	background = NULL;
	if (backGroundName.Length()) {
		background = declManager->FindMaterial(backGroundName);
		if ( background ) {
			background->SetSort( SS_GUI );
			background->SetImageClassifications( 1 );	// just for resource tracking
		}
	}
	backGroundName.SetMaterialPtr(&background);

// 
//  added parent
	mParent = win->GetParent();
// 

	hideCursor = win->hideCursor;

	idWindow *parent = win->GetParent();
	if (parent) {
		if (text.NeedsUpdate()) {
			parent->AddUpdateVar(&text);
		}
		if (visible.NeedsUpdate()) {
			parent->AddUpdateVar(&visible);
		}
		if (rect.NeedsUpdate()) {
			parent->AddUpdateVar(&rect);
		}
		if (backColor.NeedsUpdate()) {
			parent->AddUpdateVar(&backColor);
		}
		if (matColor.NeedsUpdate()) {
			parent->AddUpdateVar(&matColor);
		}
		if (foreColor.NeedsUpdate()) {
			parent->AddUpdateVar(&foreColor);
		}
		if (borderColor.NeedsUpdate()) {
			parent->AddUpdateVar(&borderColor);
		}
		if (textScale.NeedsUpdate()) {
			parent->AddUpdateVar(&textScale);
		}
		if (rotate.NeedsUpdate()) {
			parent->AddUpdateVar(&rotate);
		}
		if (shear.NeedsUpdate()) {
			parent->AddUpdateVar(&shear);
		}
		if (backGroundName.NeedsUpdate()) {
			parent->AddUpdateVar(&backGroundName);
		}
	}
}

idSimpleWindow::~idSimpleWindow() {

}

void idSimpleWindow::StateChanged( bool redraw ) {
	if ( redraw && background && background->CinematicLength() ) { 
		background->UpdateCinematic( gui->GetTime() );
	}
}

void idSimpleWindow::SetupTransforms(float x, float y) {
	static idMat3 trans;
	static idVec3 org;

	trans.Identity();
	org.Set( origin.x + x, origin.y + y, 0 );
	if ( rotate ) {
		static idRotation rot;
		static idVec3 vec( 0, 0, 1 );
		rot.Set( org, vec, rotate );
		trans = rot.ToMat3();
	}

	static idMat3 smat;
	smat.Identity();
	if (shear.x() || shear.y()) {
		smat[0][1] = shear.x();
		smat[1][0] = shear.y();
		trans *= smat;
	}

	if ( !trans.IsIdentity() ) {
		dc->SetTransformInfo( org, trans );
	}
}

void idSimpleWindow::DrawBackground(const idRectangle &drawRect) {
	if (backColor.w() > 0) {
		dc->DrawFilledRect(drawRect.x, drawRect.y, drawRect.w, drawRect.h, backColor);
	}

	if (background) {
		if (matColor.w() > 0) {
			if ( DrawPlaybackGraph( dc, gui, name.c_str(), background, drawRect, matColor ) ) {
				return;
			}
			if ( ShouldDrawCinematicUnderlay( background, drawRect ) ) {
				DrawCinematicUnderlay( dc );
			} else if ( ShouldDrawSplashUnderlay( background, drawRect ) ) {
				DrawSplashUnderlay( dc );
			}
			if ( ShouldDrawNativeScreenOverlay( gui, name.c_str(), background, drawRect, flags ) ) {
				DrawNativeScreenOverlay( background, matColor );
				return;
			}

			if ( flags & WIN_MATCANVASFILL ) {
				const float imageWidth = background->GetImageWidth();
				const float imageHeight = background->GetImageHeight();
				if ( imageWidth > 0.0f && imageHeight > 0.0f && drawRect.w > 0.0f && drawRect.h > 0.0f ) {
					float s0 = 0.0f;
					float s1 = 1.0f;
					float t0 = 0.0f;
					float t1 = 1.0f;

					const float imageAspect = imageWidth / imageHeight;
					const float canvasAspect = ( dc != NULL ) ? dc->GetCanvasAspect() : ( drawRect.w / drawRect.h );
					if ( imageAspect > canvasAspect ) {
						// Uniform fill on canvas: crop equally from left/right.
						const float keptWidth = canvasAspect / imageAspect;
						s0 = ( 1.0f - keptWidth ) * 0.5f;
						s1 = s0 + keptWidth;
					} else if ( imageAspect < canvasAspect ) {
						// Uniform fill on canvas: crop equally from top/bottom.
						const float keptHeight = imageAspect / canvasAspect;
						t0 = ( 1.0f - keptHeight ) * 0.5f;
						t1 = t0 + keptHeight;
					}

					float drawX = drawRect.x;
					float drawY = drawRect.y;
					float drawW = drawRect.w;
					float drawH = drawRect.h;
					if ( dc->ClippedCoords( &drawX, &drawY, &drawW, &drawH, &s0, &t0, &s1, &t1 ) ) {
						return;
					}
					dc->AdjustCoords( &drawX, &drawY, &drawW, &drawH );

					renderSystem->SetColor( matColor );
					dc->DrawStretchPic( drawX, drawY, drawW, drawH, s0, t0, s1, t1, background );
					return;
				}
			}

			float axisScaleX = 1.0f;
			float axisScaleY = 1.0f;
			if ( dc != NULL ) {
				dc->AdjustCoords( NULL, NULL, &axisScaleX, &axisScaleY );
			}
			if ( axisScaleX <= 0.0f ) {
				axisScaleX = 1.0f;
			}
			if ( axisScaleY <= 0.0f ) {
				axisScaleY = 1.0f;
			}

			if ( flags & WIN_MATCOVER ) {
				const float imageWidth = background->GetImageWidth();
				const float imageHeight = background->GetImageHeight();
				if ( imageWidth > 0.0f && imageHeight > 0.0f && drawRect.w > 0.0f && drawRect.h > 0.0f ) {
					float s0 = 0.0f;
					float s1 = 1.0f;
					float t0 = 0.0f;
					float t1 = 1.0f;

					const float imageAspect = imageWidth / imageHeight;
					const float rectAspect = ( drawRect.w * axisScaleX ) / ( drawRect.h * axisScaleY );
					if ( imageAspect > rectAspect ) {
						// Image is wider than the destination: crop equally from left/right.
						const float keptWidth = rectAspect / imageAspect;
						s0 = ( 1.0f - keptWidth ) * 0.5f;
						s1 = s0 + keptWidth;
					} else if ( imageAspect < rectAspect ) {
						// Image is taller than the destination: crop equally from top/bottom.
						const float keptHeight = imageAspect / rectAspect;
						t0 = ( 1.0f - keptHeight ) * 0.5f;
						t1 = t0 + keptHeight;
					}

					float drawX = drawRect.x;
					float drawY = drawRect.y;
					float drawW = drawRect.w;
					float drawH = drawRect.h;
					if ( dc->ClippedCoords( &drawX, &drawY, &drawW, &drawH, &s0, &t0, &s1, &t1 ) ) {
						return;
					}
					dc->AdjustCoords( &drawX, &drawY, &drawW, &drawH );

					renderSystem->SetColor( matColor );
					dc->DrawStretchPic( drawX, drawY, drawW, drawH, s0, t0, s1, t1, background );
					return;
				}
			}
			if ( flags & WIN_MATFIT ) {
				const float imageWidth = background->GetImageWidth();
				const float imageHeight = background->GetImageHeight();
				if ( imageWidth > 0.0f && imageHeight > 0.0f && drawRect.w > 0.0f && drawRect.h > 0.0f ) {
					float fitX = drawRect.x;
					float fitY = drawRect.y;
					float fitW = drawRect.w;
					float fitH = drawRect.h;

					const float imageAspect = imageWidth / imageHeight;
					const float rectAspect = ( drawRect.w * axisScaleX ) / ( drawRect.h * axisScaleY );
					if ( imageAspect > rectAspect ) {
						// Fit to width; center vertically.
						fitH = ( drawRect.w * axisScaleX ) / ( imageAspect * axisScaleY );
						fitY = drawRect.y + ( ( drawRect.h - fitH ) * 0.5f );
					} else if ( imageAspect < rectAspect ) {
						// Fit to height; center horizontally.
						fitW = ( drawRect.h * axisScaleY * imageAspect ) / axisScaleX;
						fitX = drawRect.x + ( ( drawRect.w - fitW ) * 0.5f );
					}

					float drawX = fitX;
					float drawY = fitY;
					float drawW = fitW;
					float drawH = fitH;
					float s0 = 0.0f;
					float t0 = 0.0f;
					float s1 = 1.0f;
					float t1 = 1.0f;
					if ( dc->ClippedCoords( &drawX, &drawY, &drawW, &drawH, &s0, &t0, &s1, &t1 ) ) {
						return;
					}
					dc->AdjustCoords( &drawX, &drawY, &drawW, &drawH );

					renderSystem->SetColor( matColor );
					dc->DrawStretchPic( drawX, drawY, drawW, drawH, s0, t0, s1, t1, background );
					return;
				}
			}

			float scalex, scaley;
			if ( flags & WIN_NATURALMAT ) {
				scalex = drawRect.w / background->GetImageWidth();
				scaley = drawRect.h / background->GetImageHeight();
			} else {
				scalex = matScalex;
				scaley = matScaley;
			}
			dc->DrawMaterial(drawRect.x, drawRect.y, drawRect.w, drawRect.h, background, matColor, scalex, scaley);
		}
	}
}

void idSimpleWindow::DrawBorderAndCaption(const idRectangle &drawRect) {
	if (flags & WIN_BORDER) {
		if (borderSize) {
			dc->DrawRect(drawRect.x, drawRect.y, drawRect.w, drawRect.h, borderSize, borderColor);
		}
	}
}

void idSimpleWindow::CalcClientRect(float xofs, float yofs) {

	drawRect = rect;

	if ( flags & WIN_INVERTRECT ) {
		drawRect.x = rect.x() - rect.w();
		drawRect.y = rect.y() - rect.h();
	}
	
	drawRect.x += xofs;
	drawRect.y += yofs;

	const bool applyScreenAlignX = ( mParent == NULL ) || ( ( mParent->GetFlags() & WIN_DESKTOP ) != 0 ) || ( mParent->GetScreenAlignX() == idWindow::SCREEN_ALIGN_X_MIDDLE );
	const bool applyScreenAlignY = ( mParent == NULL ) || ( ( mParent->GetFlags() & WIN_DESKTOP ) != 0 ) || ( mParent->GetScreenAlignY() == idWindow::SCREEN_ALIGN_Y_MIDDLE );
	if ( dc != NULL && ( applyScreenAlignX || applyScreenAlignY ) &&
			!( gui != NULL && gui->ClassicCanvas() ) ) {
		float xExpand = 0.0f;
		float yExpand = 0.0f;
		dc->GetVirtualScreenExpansion( forceAspectWidth, forceAspectHeight, xExpand, yExpand );
		// The stock interface's Remastered HUD edges, as idWindow::CalcClientRect.
		float edges[4] = { xExpand, yExpand, xExpand, yExpand };
		const bool legacyEdges = gui != NULL && gui->LegacyEdges( edges );

		if ( ( legacyEdges || xExpand > 0.0f ) && applyScreenAlignX ) {
			if ( screenAlignX == idWindow::SCREEN_ALIGN_X_LEFT ) {
				drawRect.x -= edges[0];
			} else if ( screenAlignX == idWindow::SCREEN_ALIGN_X_RIGHT ) {
				drawRect.x += edges[2];
			}
		}

		if ( ( legacyEdges || yExpand > 0.0f ) && applyScreenAlignY ) {
			if ( screenAlignY == idWindow::SCREEN_ALIGN_Y_TOP ) {
				drawRect.y -= edges[1];
			} else if ( screenAlignY == idWindow::SCREEN_ALIGN_Y_BOTTOM ) {
				drawRect.y += edges[3];
			}
		}

		AdjustTournamentWarmupLayout( gui, name.c_str(), mParent != NULL ? mParent->GetName() : NULL, xofs, xExpand, drawRect );
		OpenQ4_ExpandLegacyMessageBackdrop( gui != NULL ? gui->GetSourceFile() : NULL,
			name.c_str(), xExpand, yExpand, drawRect );
	}

	clientRect = drawRect;
	if (rect.h() > 0.0 && rect.w() > 0.0) {

		if (flags & WIN_BORDER && borderSize != 0.0) {
			clientRect.x += borderSize;
			clientRect.y += borderSize;
			clientRect.w -= borderSize;
			clientRect.h -= borderSize;
		}

		textRect = clientRect;
		textRect.x += 2.0;
	 	textRect.w -= 2.0;
		textRect.y += 2.0;
		textRect.h -= 2.0;
		textRect.x += textAlignx;
		textRect.y += textAligny;

	}
	origin.Set( rect.x() + ( rect.w() / 2 ), rect.y() + ( rect.h() / 2 ) );

}


void idSimpleWindow::Redraw(float x, float y) {
	
	if (!visible) {
		return;
	}

	CalcClientRect(0, 0);
	dc->SetFont(fontNum);
	drawRect.Offset(x, y);
	clientRect.Offset(x, y);
	textRect.Offset(x, y);
	SetupTransforms(x, y);
	if ( flags & WIN_NOCLIP ) {
		dc->EnableClipping( false );
	}
	DrawBackground(drawRect);
	DrawBorderAndCaption(drawRect);
	dc->DrawText( text, textScale, textAlign, foreColor, textRect, !( flags & WIN_NOWRAP ), -1, false, NULL, 0, textSpacing, textStyle, ( flags & WIN_CHATWINDOW ) != 0 );
	dc->SetTransformInfo(vec3_origin, mat3_identity);
	if ( flags & WIN_NOCLIP ) {
		dc->EnableClipping( true );
	}
	drawRect.Offset(-x, -y);
	clientRect.Offset(-x, -y);
	textRect.Offset(-x, -y);
}

intptr_t idSimpleWindow::GetWinVarOffset( idWinVar *wv, drawWin_t* owner) {
	intptr_t ret = -1;

	if ( wv == &rect ) {
		ret = (intptr_t)&( ( idSimpleWindow * ) 0 )->rect;
	}

	if ( wv == &backColor ) {
		ret = (intptr_t)&( ( idSimpleWindow * ) 0 )->backColor;
	}

	if ( wv == &matColor ) {
		ret = (intptr_t)&( ( idSimpleWindow * ) 0 )->matColor;
	}

	if ( wv == &foreColor ) {
		ret = (intptr_t)&( ( idSimpleWindow * ) 0 )->foreColor;
	}

	if ( wv == &borderColor ) {
		ret = (intptr_t)&( ( idSimpleWindow * ) 0 )->borderColor;
	}

	if ( wv == &textScale ) {
		ret = (intptr_t)&( ( idSimpleWindow * ) 0 )->textScale;
	}

	if ( wv == &rotate ) {
		ret = (intptr_t)&( ( idSimpleWindow * ) 0 )->rotate;
	}

	if ( ret != -1 ) {
		owner->simp = this;
	}
	return ret;
}

idWinVar *idSimpleWindow::GetWinVarByName(const char *_name) {
	idWinVar *retVar = NULL;
	if (idStr::Icmp(_name, "background") == 0) {
		retVar = &backGroundName;
	}
	if (idStr::Icmp(_name, "visible") == 0) {
		retVar = &visible;
	}
	if (idStr::Icmp(_name, "rect") == 0) {
		retVar = &rect;
	}
	if (idStr::Icmp(_name, "backColor") == 0) {
		retVar = &backColor;
	}
	if (idStr::Icmp(_name, "matColor") == 0) {
		retVar = &matColor;
	}
	if (idStr::Icmp(_name, "foreColor") == 0) {
		retVar = &foreColor;
	}
	if (idStr::Icmp(_name, "borderColor") == 0) {
		retVar = &borderColor;
	}
	if (idStr::Icmp(_name, "textScale") == 0) {
		retVar = &textScale;
	}
	if (idStr::Icmp(_name, "rotate") == 0) {
		retVar = &rotate;
	}
	if (idStr::Icmp(_name, "shear") == 0) {
		retVar = &shear;
	}
	if (idStr::Icmp(_name, "text") == 0) {
		retVar = &text;
	}
	return retVar;
}

/*
========================
idSimpleWindow::WriteToSaveGame
========================
*/
static bool OpenQ4_IsFiniteSimpleWindowRectangle( const idRectangle &rect ) {
	return std::isfinite( rect.x ) && std::isfinite( rect.y ) &&
		std::isfinite( rect.w ) && std::isfinite( rect.h );
}

static bool OpenQ4_IsFiniteSimpleWindowVec2( const idVec2 &vec ) {
	return std::isfinite( vec.x ) && std::isfinite( vec.y );
}

void idSimpleWindow::WriteToSaveGame( idFile *savefile ) {
	if ( savefile == NULL ) {
		common->Error( "idSimpleWindow::WriteToSaveGame: invalid output file for window '%s'", name.c_str() );
		return;
	}
	if ( !OpenQ4_IsFiniteSimpleWindowRectangle( drawRect ) || !OpenQ4_IsFiniteSimpleWindowRectangle( clientRect ) ||
		 !OpenQ4_IsFiniteSimpleWindowRectangle( textRect ) || !OpenQ4_IsFiniteSimpleWindowVec2( origin ) ||
		 !std::isfinite( matScalex ) || !std::isfinite( matScaley ) || !std::isfinite( borderSize ) ||
		 !std::isfinite( textAlignx ) || !std::isfinite( textAligny ) ) {
		common->Error( "idSimpleWindow::WriteToSaveGame: refusing non-finite layout state for window '%s'", name.c_str() );
		return;
	}
	if ( !OpenQ4_WriteSaveGameField( savefile, flags, "idSimpleWindow::WriteToSaveGame", "flags" ) ||
		 !OpenQ4_WriteSaveGameField( savefile, drawRect, "idSimpleWindow::WriteToSaveGame", "draw rect" ) ||
		 !OpenQ4_WriteSaveGameField( savefile, clientRect, "idSimpleWindow::WriteToSaveGame", "client rect" ) ||
		 !OpenQ4_WriteSaveGameField( savefile, textRect, "idSimpleWindow::WriteToSaveGame", "text rect" ) ||
		 !OpenQ4_WriteSaveGameField( savefile, origin, "idSimpleWindow::WriteToSaveGame", "origin" ) ||
		 !OpenQ4_WriteSaveGameField( savefile, fontNum, "idSimpleWindow::WriteToSaveGame", "font number" ) ||
		 !OpenQ4_WriteSaveGameField( savefile, matScalex, "idSimpleWindow::WriteToSaveGame", "material scale x" ) ||
		 !OpenQ4_WriteSaveGameField( savefile, matScaley, "idSimpleWindow::WriteToSaveGame", "material scale y" ) ||
		 !OpenQ4_WriteSaveGameField( savefile, borderSize, "idSimpleWindow::WriteToSaveGame", "border size" ) ||
		 !OpenQ4_WriteSaveGameField( savefile, textAlign, "idSimpleWindow::WriteToSaveGame", "text align" ) ||
		 !OpenQ4_WriteSaveGameField( savefile, textAlignx, "idSimpleWindow::WriteToSaveGame", "text align x" ) ||
		 !OpenQ4_WriteSaveGameField( savefile, textAligny, "idSimpleWindow::WriteToSaveGame", "text align y" ) ||
		 !OpenQ4_WriteSaveGameField( savefile, textSpacing, "idSimpleWindow::WriteToSaveGame", "text spacing" ) ||
		 !OpenQ4_WriteSaveGameField( savefile, textStyle, "idSimpleWindow::WriteToSaveGame", "text style" ) ||
		 !OpenQ4_WriteSaveGameField( savefile, textShadow, "idSimpleWindow::WriteToSaveGame", "text shadow" ) ) {
		return;
	}

	OpenQ4_SanitizeSaveGameWinVar( text, "idSimpleWindow::WriteToSaveGame", name.c_str(), "text" );
	OpenQ4_SanitizeSaveGameWinVar( visible, "idSimpleWindow::WriteToSaveGame", name.c_str(), "visible" );
	OpenQ4_SanitizeSaveGameWinVar( rect, "idSimpleWindow::WriteToSaveGame", name.c_str(), "rect" );
	OpenQ4_SanitizeSaveGameWinVar( backColor, "idSimpleWindow::WriteToSaveGame", name.c_str(), "backColor" );
	OpenQ4_SanitizeSaveGameWinVar( matColor, "idSimpleWindow::WriteToSaveGame", name.c_str(), "matColor" );
	OpenQ4_SanitizeSaveGameWinVar( foreColor, "idSimpleWindow::WriteToSaveGame", name.c_str(), "foreColor" );
	OpenQ4_SanitizeSaveGameWinVar( borderColor, "idSimpleWindow::WriteToSaveGame", name.c_str(), "borderColor" );
	OpenQ4_SanitizeSaveGameWinVar( textScale, "idSimpleWindow::WriteToSaveGame", name.c_str(), "textScale" );
	OpenQ4_SanitizeSaveGameWinVar( rotate, "idSimpleWindow::WriteToSaveGame", name.c_str(), "rotate" );
	OpenQ4_SanitizeSaveGameWinVar( shear, "idSimpleWindow::WriteToSaveGame", name.c_str(), "shear" );
	text.WriteToSaveGame( savefile );
	visible.WriteToSaveGame( savefile );
	rect.WriteToSaveGame( savefile );
	backColor.WriteToSaveGame( savefile );
	matColor.WriteToSaveGame( savefile );
	foreColor.WriteToSaveGame( savefile );
	borderColor.WriteToSaveGame( savefile );
	textScale.WriteToSaveGame( savefile );
	rotate.WriteToSaveGame( savefile );
	shear.WriteToSaveGame( savefile );
	backGroundName.WriteToSaveGame( savefile );

	int stringLen;

	if ( background ) {
		stringLen = idLib::SizeToInt( strlen( background->GetName() ), "idSimpleWindow::WriteToSaveGame" );
		if ( stringLen > 64 * 1024 ) {
			common->Error( "idSimpleWindow::WriteToSaveGame: background name for window '%s' is too long (%d bytes)",
				name.c_str(), stringLen );
			return;
		}
		if ( !OpenQ4_WriteSaveGameInt( savefile, stringLen, "idSimpleWindow::WriteToSaveGame", "background length" ) ||
			 !OpenQ4_WriteSaveGameBytes( savefile, background->GetName(), stringLen,
				 "idSimpleWindow::WriteToSaveGame", "background name" ) ) {
			return;
		}
	} else {
		stringLen = 0;
		OpenQ4_WriteSaveGameInt( savefile, stringLen, "idSimpleWindow::WriteToSaveGame", "background length" );
	}

}

/*
========================
idSimpleWindow::ReadFromSaveGame
========================
*/
void idSimpleWindow::ReadFromSaveGame( idFile *savefile ) {
	int savedFlags;
	idRectangle savedDrawRect;
	idRectangle savedClientRect;
	idRectangle savedTextRect;
	idVec2 savedOrigin;
	int savedFontNum;
	float savedMatScaleX;
	float savedMatScaleY;
	float savedBorderSize;
	int savedTextAlign;
	float savedTextAlignX;
	float savedTextAlignY;
	int savedTextSpacing;
	signed char savedTextStyle;
	int savedTextShadow;
	if ( !OpenQ4_ReadSaveGameField( savefile, savedFlags, "idSimpleWindow::ReadFromSaveGame", "flags" ) ||
		 !OpenQ4_ReadSaveGameField( savefile, savedDrawRect, "idSimpleWindow::ReadFromSaveGame", "draw rect" ) ||
		 !OpenQ4_ReadSaveGameField( savefile, savedClientRect, "idSimpleWindow::ReadFromSaveGame", "client rect" ) ||
		 !OpenQ4_ReadSaveGameField( savefile, savedTextRect, "idSimpleWindow::ReadFromSaveGame", "text rect" ) ||
		 !OpenQ4_ReadSaveGameField( savefile, savedOrigin, "idSimpleWindow::ReadFromSaveGame", "origin" ) ||
		 !OpenQ4_ReadSaveGameField( savefile, savedFontNum, "idSimpleWindow::ReadFromSaveGame", "font number" ) ||
		 !OpenQ4_ReadSaveGameField( savefile, savedMatScaleX, "idSimpleWindow::ReadFromSaveGame", "material scale x" ) ||
		 !OpenQ4_ReadSaveGameField( savefile, savedMatScaleY, "idSimpleWindow::ReadFromSaveGame", "material scale y" ) ||
		 !OpenQ4_ReadSaveGameField( savefile, savedBorderSize, "idSimpleWindow::ReadFromSaveGame", "border size" ) ||
		 !OpenQ4_ReadSaveGameField( savefile, savedTextAlign, "idSimpleWindow::ReadFromSaveGame", "text align" ) ||
		 !OpenQ4_ReadSaveGameField( savefile, savedTextAlignX, "idSimpleWindow::ReadFromSaveGame", "text align x" ) ||
		 !OpenQ4_ReadSaveGameField( savefile, savedTextAlignY, "idSimpleWindow::ReadFromSaveGame", "text align y" ) ||
		 !OpenQ4_ReadSaveGameField( savefile, savedTextSpacing, "idSimpleWindow::ReadFromSaveGame", "text spacing" ) ||
		 !OpenQ4_ReadSaveGameField( savefile, savedTextStyle, "idSimpleWindow::ReadFromSaveGame", "text style" ) ||
		 !OpenQ4_ReadSaveGameField( savefile, savedTextShadow, "idSimpleWindow::ReadFromSaveGame", "text shadow" ) ) {
		return;
	}
	if ( !OpenQ4_IsFiniteSimpleWindowRectangle( savedDrawRect ) || !OpenQ4_IsFiniteSimpleWindowRectangle( savedClientRect ) ||
		 !OpenQ4_IsFiniteSimpleWindowRectangle( savedTextRect ) || !OpenQ4_IsFiniteSimpleWindowVec2( savedOrigin ) ||
		 !std::isfinite( savedMatScaleX ) || !std::isfinite( savedMatScaleY ) || !std::isfinite( savedBorderSize ) ||
		 !std::isfinite( savedTextAlignX ) || !std::isfinite( savedTextAlignY ) ) {
		common->Error( "idSimpleWindow::ReadFromSaveGame: non-finite layout state for window '%s'", name.c_str() );
		return;
	}
	flags = savedFlags;
	drawRect = savedDrawRect;
	clientRect = savedClientRect;
	textRect = savedTextRect;
	origin = savedOrigin;
	fontNum = savedFontNum;
	matScalex = savedMatScaleX;
	matScaley = savedMatScaleY;
	borderSize = savedBorderSize;
	textAlign = savedTextAlign;
	textAlignx = savedTextAlignX;
	textAligny = savedTextAlignY;
	textSpacing = savedTextSpacing;
	textStyle = savedTextStyle;
	textShadow = savedTextShadow;

	text.ReadFromSaveGame( savefile );
	visible.ReadFromSaveGame( savefile );
	rect.ReadFromSaveGame( savefile );
	backColor.ReadFromSaveGame( savefile );
	matColor.ReadFromSaveGame( savefile );
	foreColor.ReadFromSaveGame( savefile );
	borderColor.ReadFromSaveGame( savefile );
	textScale.ReadFromSaveGame( savefile );
	rotate.ReadFromSaveGame( savefile );
	shear.ReadFromSaveGame( savefile );
	backGroundName.ReadFromSaveGame( savefile );

	int stringLen = 0;

	const int stringOffset = savefile->Tell();
	if ( !OpenQ4_ReadSaveGameInt( savefile, stringLen, "idSimpleWindow::ReadFromSaveGame", "background length" ) ) {
		background = NULL;
		return;
	}
	const int remainingBytes = Max( 0, savefile->Length() - savefile->Tell() );
	const int maxSavedStringLength = 64 * 1024;
	if ( stringLen < 0 || stringLen > maxSavedStringLength || stringLen > remainingBytes ) {
		common->Error( "idSimpleWindow::ReadFromSaveGame: invalid background length %d at offset %d (remaining %d)",
			stringLen, stringOffset, remainingBytes );
		background = NULL;
		return;
	}
	if ( stringLen > 0 ) {
		idStr backName;

		backName.Fill( ' ', stringLen );
		if ( !OpenQ4_ReadSaveGameBytes( savefile, &(backName)[0], stringLen,
				"idSimpleWindow::ReadFromSaveGame", "background name" ) ) {
			background = NULL;
			return;
		}

		background = declManager->FindMaterial( backName );
		if ( background ) {
			background->SetSort( SS_GUI );
		}
	} else {
		background = NULL;
	}

}


/*
===============================
*/

size_t idSimpleWindow::Size() {
	size_t sz = sizeof(*this);
	sz += name.Size();
	sz += text.Size();
	sz += backGroundName.Size();
	return sz;
}

// jmarshall - quake 4 gui
void idSimpleWindow::ResetCinematics(void) {
	if (background) {
		background->ResetCinematicTime(gui->GetTime());
	}
}

bool idSimpleWindow::IsBackgroundCinematicComplete() const {
	if ( background == NULL || gui == NULL || background->CinematicLength() <= 0 ) {
		return false;
	}

	const int status = background->CinematicStatus( gui->GetTime() );
	return ( status == FMV_EOF || status == FMV_IDLE || status == FMV_LOOPED );
}
// jmarshall end
