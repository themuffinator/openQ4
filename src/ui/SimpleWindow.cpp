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
	if (backGroundName.Length()) {
		background = declManager->FindMaterial(backGroundName);
		background->SetSort( SS_GUI );
		background->SetImageClassifications( 1 );	// just for resource tracking
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
	if ( dc != NULL && ( applyScreenAlignX || applyScreenAlignY ) ) {
		float xExpand = 0.0f;
		float yExpand = 0.0f;
		dc->GetVirtualScreenExpansion( forceAspectWidth, forceAspectHeight, xExpand, yExpand );

		if ( xExpand > 0.0f && applyScreenAlignX ) {
			if ( screenAlignX == idWindow::SCREEN_ALIGN_X_LEFT ) {
				drawRect.x -= xExpand;
			} else if ( screenAlignX == idWindow::SCREEN_ALIGN_X_RIGHT ) {
				drawRect.x += xExpand;
			}
		}

		if ( yExpand > 0.0f && applyScreenAlignY ) {
			if ( screenAlignY == idWindow::SCREEN_ALIGN_Y_TOP ) {
				drawRect.y -= yExpand;
			} else if ( screenAlignY == idWindow::SCREEN_ALIGN_Y_BOTTOM ) {
				drawRect.y += yExpand;
			}
		}
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
	if ( textShadow ) {
		idStr shadowText = text;
		idRectangle shadowRect = textRect;

		shadowText.RemoveColors();
		shadowRect.x += textShadow;
		shadowRect.y += textShadow;

		dc->DrawText( shadowText, textScale, textAlign, colorBlack, shadowRect, !( flags & WIN_NOWRAP ), -1 );
	}
	dc->DrawText(text, textScale, textAlign, foreColor, textRect, !( flags & WIN_NOWRAP ), -1);
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
void idSimpleWindow::WriteToSaveGame( idFile *savefile ) {

	savefile->Write( &flags, sizeof( flags ) );
	savefile->Write( &drawRect, sizeof( drawRect ) );
	savefile->Write( &clientRect, sizeof( clientRect ) );
	savefile->Write( &textRect, sizeof( textRect ) );
	savefile->Write( &origin, sizeof( origin ) );
	savefile->Write( &fontNum, sizeof( fontNum ) );
	savefile->Write( &matScalex, sizeof( matScalex ) );
	savefile->Write( &matScaley, sizeof( matScaley ) );
	savefile->Write( &borderSize, sizeof( borderSize ) );
	savefile->Write( &textAlign, sizeof( textAlign ) );
	savefile->Write( &textAlignx, sizeof( textAlignx ) );
	savefile->Write( &textAligny, sizeof( textAligny ) );
	savefile->Write( &textShadow, sizeof( textShadow ) );

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
		stringLen = strlen( background->GetName() );
		savefile->Write( &stringLen, sizeof( stringLen ) );
		savefile->Write( background->GetName(), stringLen );
	} else {
		stringLen = 0;
		savefile->Write( &stringLen, sizeof( stringLen ) );
	}

}

/*
========================
idSimpleWindow::ReadFromSaveGame
========================
*/
void idSimpleWindow::ReadFromSaveGame( idFile *savefile ) {

	savefile->Read( &flags, sizeof( flags ) );
	savefile->Read( &drawRect, sizeof( drawRect ) );
	savefile->Read( &clientRect, sizeof( clientRect ) );
	savefile->Read( &textRect, sizeof( textRect ) );
	savefile->Read( &origin, sizeof( origin ) );
	savefile->Read( &fontNum, sizeof( fontNum ) );
	savefile->Read( &matScalex, sizeof( matScalex ) );
	savefile->Read( &matScaley, sizeof( matScaley ) );
	savefile->Read( &borderSize, sizeof( borderSize ) );
	savefile->Read( &textAlign, sizeof( textAlign ) );
	savefile->Read( &textAlignx, sizeof( textAlignx ) );
	savefile->Read( &textAligny, sizeof( textAligny ) );
	savefile->Read( &textShadow, sizeof( textShadow ) );

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

	int stringLen;

	savefile->Read( &stringLen, sizeof( stringLen ) );
	if ( stringLen > 0 ) {
		idStr backName;

		backName.Fill( ' ', stringLen );
		savefile->Read( &(backName)[0], stringLen );

		background = declManager->FindMaterial( backName );
		background->SetSort( SS_GUI );
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
