// Shared SP/MP VR presentation. Quake 4 SDK EULA; see src/game/LICENSE.
#include "../idlib/precompiled.h"
#pragma hdrstop

#include "Game_local.h"
#include "VRWristDisplay.h"
#include "VRWristCore.h"

/*
===============================================================================

	Wrist displays: see VRWristDisplay.h.

	Each display is laid out once per frame on a panel 200 units wide, y
	down, from rectangles, polygons, stock HUD icons and glyphs the UI's
	text shaper returns (uiManager->BuildHudText, the HUD's Lowpixel face at
	its largest size). The panel then stands in the world at its wrist,
	vr_wristWidth metres wide, turned to the head with no roll, and each eye
	projects its corners onto its own 640x480 canvas, the way the aim marker
	is drawn: the pair fuses at the wrist's depth. The 2D pass culls by
	winding, so every triangle is put into the order it keeps.

===============================================================================
*/

namespace {

const float PANEL_WIDTH = 200.0f;		// panel units across
const float PAD = 8.0f;
const float CHAMFER = 12.0f;			// the 45-degree cut of the plate's upper leading corner
// where a display stands: back from the grip toward the wrist, then off it
const float WRIST_BACK = 0.08f;			// metres
const float WEAPON_WRIST_LIFT = -0.04f;	// below the weapon hand's wrist, under the line of the gun
const float OFF_WRIST_LIFT = 0.05f;		// above the off hand's, as a watch
const float HOLDING_DIM = 0.3f;			// the off hand's while it holds the foregrip
const int WEAPON_NAME_MSEC = 2500;
const idVec4 GUN_ICON_UV( 0.10f, 0.0f, 0.92f, 1.0f );
const int PICKUP_MSEC = 4000;
const int OBJECTIVE_MSEC = 6000;
const int NOTICE_FADE_MSEC = 400;
const int HIT_MSEC = 900;
// damage direction: arcs on a ring round the line of sight
const float HIT_RING = 0.23f;			// tangent of the ring's angular radius, about 13 degrees
const float HIT_BAND = 0.028f;			// its thickness, as a tangent
const float HIT_ARC = DEG2RAD( 44.0f );
const float HIT_DISTANCE = 1.5f;		// metres: where the arcs fuse

struct palette_t {
	idVec4	readout, icon, fill, track, low, warn, plate;
};

// docs/dev/ui-visual-design.md section 2, the Marine and Strogg HUD families
const palette_t marinePalette = {
	idVec4( 0.867f, 0.922f, 0.765f, 1.0f ),		// #DDEBC3
	idVec4( 0.659f, 0.639f, 0.376f, 0.95f ),	// #A8A360, stronger than the HUD's 60% for the distance
	idVec4( 0.702f, 0.816f, 0.431f, 0.85f ),	// #B3D06E
	idVec4( 0.702f, 0.816f, 0.431f, 0.20f ),
	idVec4( 1.0f, 0.298f, 0.0f, 1.0f ),			// #FF4C00
	idVec4( 0.890f, 0.678f, 0.212f, 1.0f ),		// #E3AD36
	idVec4( 0.0f, 0.0f, 0.0f, 0.62f ),
};
const palette_t stroggPalette = {
	idVec4( 0.988f, 1.0f, 0.784f, 1.0f ),		// #FCFFC8
	idVec4( 1.0f, 0.8f, 0.0f, 0.95f ),			// #FFCC00
	idVec4( 1.0f, 0.565f, 0.0f, 0.85f ),		// #FF9000
	idVec4( 1.0f, 0.565f, 0.0f, 0.20f ),
	idVec4( 1.0f, 0.298f, 0.0f, 1.0f ),
	idVec4( 0.890f, 0.678f, 0.212f, 1.0f ),
	idVec4( 0.0f, 0.0f, 0.0f, 0.62f ),
};

idVec4 WithAlpha( const idVec4 &color, float alpha ) {
	return idVec4( color.x, color.y, color.z, color.w * alpha );
}

// A shaped line of text, kept while the same text is asked for again.
struct shapedText_t {
	idStr						text;
	idList<uiHudPrimitive_t>	glyphs;
	float						left, right;
	int							used;
};

const int TEXT_CACHE = 24;
const float TEXT_SCALE = 1.0f;			// the face's largest glyph set
const float TEXT_BOX_W = 1600.0f;
const float TEXT_BOX_H = 160.0f;

class idWristText {
public:
	idWristText() : capTop( 0.0f ), baseline( 0.0f ), measured( false ), clock( 0 ) {}

	void Reset() {
		for ( int i = 0; i < TEXT_CACHE; i++ ) {
			cache[i].text.Clear();
			cache[i].glyphs.Clear();
		}
		measured = false;
	}

	// The glyphs of text, left aligned at x (align 0), centred (1) or right
	// aligned (2), the figures' cap height cap with their baseline at
	// baseline, squeezed to fit width when one is given.
	void Draw( idList<uiHudPrimitive_t> &out, const char *text, float x, float baseLine, float cap, int align, const idVec4 &color,
			float width = 0.0f ) {
		if ( text == NULL || !text[0] || !Measure() ) {
			return;
		}
		const shapedText_t *shaped = Shape( text );
		if ( shaped == NULL || shaped->glyphs.Num() == 0 ) {
			return;
		}
		float scale = cap / ( baseline - capTop );
		const float inkWidth = ( shaped->right - shaped->left ) * scale;
		float xScale = scale;
		if ( width > 0.0f && inkWidth > width ) {
			xScale *= width / inkWidth;
		}
		const float drawn = ( shaped->right - shaped->left ) * xScale;
		const float left = align == 0 ? x : ( align == 1 ? x - drawn * 0.5f : x - drawn );
		for ( int i = 0; i < shaped->glyphs.Num(); i++ ) {
			uiHudPrimitive_t item = shaped->glyphs[i];
			for ( int n = 0; n < 4; n++ ) {
				item.points[n].x = left + ( item.points[n].x - shaped->left ) * xScale;
				item.points[n].y = baseLine + ( item.points[n].y - baseline ) * scale;
			}
			item.color = color;
			out.Append( item );
		}
	}

	// The drawn width of text at a cap height.
	float Width( const char *text, float cap ) {
		if ( text == NULL || !text[0] || !Measure() ) {
			return 0.0f;
		}
		const shapedText_t *shaped = Shape( text );
		return shaped == NULL ? 0.0f : ( shaped->right - shaped->left ) * cap / ( baseline - capTop );
	}

private:
	// The figures' cap top and baseline, from a zero shaped as every line is.
	bool Measure() {
		if ( measured ) {
			return true;
		}
		idList<uiHudPrimitive_t> zero;
		uiManager->BuildHudText( "0", TEXT_SCALE, TEXT_BOX_W, TEXT_BOX_H, zero );
		if ( zero.Num() == 0 ) {
			return false;
		}
		float top = idMath::INFINITY, bottom = -idMath::INFINITY;
		for ( int i = 0; i < zero.Num(); i++ ) {
			for ( int n = 0; n < 4; n++ ) {
				top = Min( top, zero[i].points[n].y );
				bottom = Max( bottom, zero[i].points[n].y );
			}
		}
		if ( bottom - top < 1.0f ) {
			return false;
		}
		// the glyph quad of a zero holds its ink with a little margin; the
		// quad's own extent is what every other glyph is placed by, so it is
		// the reference for the cap and the baseline alike
		capTop = top;
		baseline = bottom;
		measured = true;
		return true;
	}

	const shapedText_t *Shape( const char *text ) {
		clock++;
		int oldest = 0;
		for ( int i = 0; i < TEXT_CACHE; i++ ) {
			if ( cache[i].text.Length() && cache[i].text == text ) {
				cache[i].used = clock;
				return &cache[i];
			}
			if ( cache[i].used < cache[oldest].used ) {
				oldest = i;
			}
		}
		shapedText_t &slot = cache[oldest];
		slot.text = text;
		slot.used = clock;
		uiManager->BuildHudText( text, TEXT_SCALE, TEXT_BOX_W, TEXT_BOX_H, slot.glyphs );
		slot.left = idMath::INFINITY;
		slot.right = -idMath::INFINITY;
		for ( int i = 0; i < slot.glyphs.Num(); i++ ) {
			for ( int n = 0; n < 4; n++ ) {
				slot.left = Min( slot.left, slot.glyphs[i].points[n].x );
				slot.right = Max( slot.right, slot.glyphs[i].points[n].x );
			}
		}
		if ( slot.glyphs.Num() == 0 ) {
			slot.left = slot.right = 0.0f;
		}
		return &slot;
	}

	shapedText_t	cache[TEXT_CACHE];
	float			capTop, baseline;
	bool			measured;
	int				clock;
};

// One panel: its primitives in panel units, then where it stands.
struct wristPanel_t {
	idList<uiHudPrimitive_t>	items;
	float						height;
	bool						visible;
	float						alpha;
	idVec3						centre;
	idVec3						right, up;		// world, units per panel unit
};

struct notice_t {
	idStr				text;
	idStr				label;		// a heading before the text (objectives)
	const idMaterial *	icon;
	int					start;
	int					life;
};

class idVRWristDisplay {
public:
	idVRWristDisplay() : guiPass( false ), lastWeapon( -1 ), weaponChanged( 0 ), lastTime( 0 ), showHit( false ), hitAlpha( 0.0f ),
		white( NULL ), healthIcon( NULL ), armorIcon( NULL ), infinityIcon( NULL ), flagIcon( NULL ), hudMode( NULL ), wristWidth( NULL ),
		damageCvar( NULL ), reducedFlashing( NULL ) {}

	bool						guiPass;

	void Prepare( const vrWristStatus_t &status, const vrFrameState_t &frame, const idVec3 &eyeOrigin, float trackingYaw,
			bool show, bool damageDirection );
	void DrawEye( const renderView_t &lens );
	void NotePickup( const char *text, const char *icon );
	void NoteObjective( const char *title, bool complete );
	bool Selected();

private:
	void Precache();
	void LayoutWeapon( const vrWristStatus_t &status, const palette_t &palette, wristPanel_t &panel );
	void LayoutVitals( const vrWristStatus_t &status, const palette_t &palette, wristPanel_t &panel );
	void Rect( wristPanel_t &panel, float x, float y, float w, float h, const idVec4 &color );
	void Icon( wristPanel_t &panel, const idMaterial *material, float x, float y, float w, float h, const idVec4 &color,
			const idVec4 &uv = idVec4( 0.0f, 0.0f, 1.0f, 1.0f ) );
	void Plate( wristPanel_t &panel, const idVec4 &color, const idVec4 &rim );
	void Bar( wristPanel_t &panel, float x, float y, float w, float h, float fraction, const palette_t &palette, const idVec4 &fill );
	void Place( wristPanel_t &panel, const idVec3 &grip, const idMat3 &axis, float lift, const idVec3 &head, const idVec3 &headUp,
			float unitsPerMetre );
	void Notice( wristPanel_t &panel, const notice_t &notice, float y, const palette_t &palette, float alpha );
	void DrawPanel( const renderView_t &lens, const wristPanel_t &panel );
	void DrawHitArcs( const renderView_t &lens );

	wristPanel_t				panels[2];		// weapon hand, off hand
	idWristText					text;
	notice_t					notices[2];		// the newest first
	int							lastWeapon;
	int							weaponChanged;
	int							lastTime;
	// the damage-direction arcs, in the head's frame
	bool						showHit;
	float						hitAlpha;
	float						hitAngle;		// round the ring from the top, clockwise as seen
	idVec3						headOrigin;
	idMat3						headAxis;
	float						hitDistance;
	const idMaterial *			white;
	const idMaterial *			healthIcon;
	const idMaterial *			armorIcon;
	const idMaterial *			infinityIcon;
	const idMaterial *			flagIcon;
	idCVar *					hudMode;
	idCVar *					wristWidth;
	idCVar *					damageCvar;
	idCVar *					reducedFlashing;
};

idVRWristDisplay wristDisplay;

void idVRWristDisplay::Precache() {
	// a level load purges declarations; a material pointer from before it
	// may name another one now
	if ( white == NULL || white != declManager->FindMaterial( "_white" ) ) {
		white = declManager->FindMaterial( "_white" );
		healthIcon = declManager->FindMaterial( "gfx/guis/hud/icons/item_health" );
		armorIcon = declManager->FindMaterial( "gfx/guis/hud/icons/item_armor" );
		infinityIcon = declManager->FindMaterial( "gfx/guis/hud/icons/icon_infinity" );
		flagIcon = declManager->FindMaterial( "gfx/guis/hud/ctf_flag" );
		const idMaterial *icons[] = { healthIcon, armorIcon, infinityIcon, flagIcon };
		for ( int i = 0; i < 4; i++ ) {
			if ( icons[i] != NULL ) {
				icons[i]->SetSort( SS_GUI );
			}
		}
		text.Reset();
	}
	if ( hudMode == NULL ) {
		hudMode = cvarSystem->Find( "vr_hudMode" );
		wristWidth = cvarSystem->Find( "vr_wristWidth" );
		damageCvar = cvarSystem->Find( "ui_hudDamageDirection" );
		reducedFlashing = cvarSystem->Find( "ui_reducedFlashing" );
	}
}

bool idVRWristDisplay::Selected() {
	Precache();
	return hudMode != NULL && hudMode->GetInteger() == 1;
}

void idVRWristDisplay::NotePickup( const char *pickup, const char *icon ) {
	if ( pickup == NULL || !pickup[0] ) {
		return;
	}
	const int now = gameLocal.time;
	// the same item again while its line still shows only restarts it
	if ( notices[0].label.Length() == 0 && notices[0].text == pickup && now - notices[0].start < notices[0].life ) {
		notices[0].start = now;
		return;
	}
	notices[1] = notices[0];
	notices[0].text = pickup;
	notices[0].label.Clear();
	notices[0].icon = icon != NULL && icon[0] ? declManager->FindMaterial( icon, false ) : NULL;
	notices[0].start = now;
	notices[0].life = PICKUP_MSEC;
}

void idVRWristDisplay::NoteObjective( const char *title, bool complete ) {
	notices[1] = notices[0];
	notices[0].text = title != NULL ? title : "";
	notices[0].label = common->GetLocalizedString( complete ? "#str_233564" : "#str_233563" );
	notices[0].icon = NULL;
	notices[0].start = gameLocal.time;
	notices[0].life = OBJECTIVE_MSEC;
}

void idVRWristDisplay::Rect( wristPanel_t &panel, float x, float y, float w, float h, const idVec4 &color ) {
	if ( w <= 0.0f || h <= 0.0f || color.w <= 0.0f ) {
		return;
	}
	uiHudPrimitive_t item;
	item.points[0].Set( x, y );
	item.points[1].Set( x + w, y );
	item.points[2].Set( x + w, y + h );
	item.points[3].Set( x, y + h );
	item.uv.Set( 0.0f, 0.0f, 1.0f, 1.0f );
	item.color = color;
	item.material = white;
	item.glyph = false;
	panel.items.Append( item );
}

// A stock icon, or the window uv of it, fitted into a box by its own
// aspect, centred.
void idVRWristDisplay::Icon( wristPanel_t &panel, const idMaterial *material, float x, float y, float w, float h, const idVec4 &color,
		const idVec4 &uv ) {
	if ( material == NULL ) {
		return;
	}
	const float iw = static_cast<float>( Max( 1, material->GetImageWidth() ) ) * ( uv.z - uv.x );
	const float ih = static_cast<float>( Max( 1, material->GetImageHeight() ) ) * ( uv.w - uv.y );
	float dw = w, dh = w * ih / iw;
	if ( dh > h ) {
		dh = h;
		dw = h * iw / ih;
	}
	uiHudPrimitive_t item;
	const float left = x + ( w - dw ) * 0.5f, top = y + ( h - dh ) * 0.5f;
	item.points[0].Set( left, top );
	item.points[1].Set( left + dw, top );
	item.points[2].Set( left + dw, top + dh );
	item.points[3].Set( left, top + dh );
	item.uv = uv;
	item.color = color;
	item.material = material;
	item.glyph = false;
	panel.items.Append( item );
}

// The backing that keeps the figures readable over a bright scene: a dark
// plate with the family's 45-degree cut at its upper leading corner, and a
// thin rim. Drawn first, so it goes to the front of the list.
void idVRWristDisplay::Plate( wristPanel_t &panel, const idVec4 &color, const idVec4 &rim ) {
	idList<uiHudPrimitive_t> plate;
	const float w = PANEL_WIDTH, h = panel.height, c = CHAMFER;
	uiHudPrimitive_t item;
	item.uv.Set( 0.0f, 0.0f, 1.0f, 1.0f );
	item.material = white;
	item.glyph = false;
	item.color = color;
	// the body under the cut, then the strip beside it, then the cut itself
	item.points[0].Set( 0.0f, c );
	item.points[1].Set( w, c );
	item.points[2].Set( w, h );
	item.points[3].Set( 0.0f, h );
	plate.Append( item );
	item.points[0].Set( c, 0.0f );
	item.points[1].Set( w, 0.0f );
	item.points[2].Set( w, c );
	item.points[3].Set( c, c );
	plate.Append( item );
	item.points[0].Set( c, 0.0f );
	item.points[1].Set( c, 0.0f );
	item.points[2].Set( c, c );
	item.points[3].Set( 0.0f, c );
	plate.Append( item );
	// the rim along the top and the leading edge
	const float t = 1.5f;
	item.color = rim;
	item.points[0].Set( c, 0.0f );
	item.points[1].Set( w, 0.0f );
	item.points[2].Set( w, t );
	item.points[3].Set( c, t );
	plate.Append( item );
	item.points[0].Set( 0.0f, c );
	item.points[1].Set( t, c );
	item.points[2].Set( t, h );
	item.points[3].Set( 0.0f, h );
	plate.Append( item );
	item.points[0].Set( c, 0.0f );
	item.points[1].Set( c, t * 1.414f );
	item.points[2].Set( t * 1.414f, c );
	item.points[3].Set( 0.0f, c );
	plate.Append( item );
	plate.Append( panel.items );
	panel.items = plate;
}

void idVRWristDisplay::Bar( wristPanel_t &panel, float x, float y, float w, float h, float fraction, const palette_t &palette,
		const idVec4 &fill ) {
	fraction = idMath::ClampFloat( 0.0f, 1.0f, fraction );
	Rect( panel, x, y, w, h, palette.track );
	Rect( panel, x, y, w * fraction, h, fill );
}

void idVRWristDisplay::LayoutWeapon( const vrWristStatus_t &status, const palette_t &palette, wristPanel_t &panel ) {
	panel.items.SetNum( 0, false );
	float y = PAD;
	const int now = gameLocal.time;
	// the weapon's name for a moment after a change
	if ( weaponChanged && now - weaponChanged < WEAPON_NAME_MSEC && status.weaponName.Length() ) {
		const float fade = idMath::ClampFloat( 0.0f, 1.0f, ( WEAPON_NAME_MSEC - ( now - weaponChanged ) ) / static_cast<float>( NOTICE_FADE_MSEC ) );
		text.Draw( panel.items, status.weaponName.c_str(), PAD, y + 13.0f, 13.0f, 0, WithAlpha( palette.readout, fade ), PANEL_WIDTH - 2.0f * PAD );
		y += 20.0f;
	}
	const float top = y;
	// the stock gun pictures (gun_*, 256x64, The Awakening's too) draw the gun
	// right-aligned between a ninth and 0.91 of their width: the window holds
	// the longest, the shotgun, and the rest stand at its right
	Icon( panel, status.weaponIcon, PAD, top + 8.0f, 94.0f, 40.0f, WithAlpha( palette.readout, 0.85f ), GUN_ICON_UV );

	const float right = PANEL_WIDTH - PAD;
	const vrWristAmmo_t ammo = VRWrist_Ammo( status.infinite, status.usesClip, status.clip, status.clipSize, status.total, status.maxAmmo );
	const idVec4 figure = ammo.tone == VR_WRIST_TONE_LOW ? palette.low : ( ammo.tone == VR_WRIST_TONE_WARN ? palette.warn : palette.readout );
	if ( status.infinite ) {
		Icon( panel, infinityIcon, 112.0f, top + 8.0f, 80.0f, 44.0f, palette.readout );
	} else if ( status.usesClip ) {
		text.Draw( panel.items, va( "%d", Max( 0, status.clip ) ), right, top + 38.0f, 36.0f, 2, figure, 88.0f );
		text.Draw( panel.items, va( "%d", Max( 0, status.reserve ) ), right, top + 60.0f, 15.0f, 2, palette.readout, 88.0f );
	} else {
		text.Draw( panel.items, va( "%d", Max( 0, status.total ) ), right, top + 44.0f, 36.0f, 2, figure, 88.0f );
	}
	y = top + 66.0f;
	Bar( panel, PAD, y, PANEL_WIDTH - 2.0f * PAD, 6.0f, ammo.fill, palette,
		ammo.tone == VR_WRIST_TONE_NORMAL ? palette.fill : WithAlpha( figure, 0.85f ) );
	y += 6.0f;
	if ( ammo.label != VR_WRIST_LABEL_NONE ) {
		// RELOAD or NO AMMO, pulsing at 2 Hz
		const float pulse = 0.65f + 0.35f * idMath::Cos( MS2SEC( now ) * idMath::TWO_PI * 2.0f );
		const bool none = ammo.label == VR_WRIST_LABEL_NO_AMMO;
		text.Draw( panel.items, common->GetLocalizedString( none ? "#str_233566" : "#str_233565" ), PANEL_WIDTH * 0.5f, y + 19.0f, 14.0f, 1,
			WithAlpha( none ? palette.low : palette.warn, pulse ), PANEL_WIDTH - 2.0f * PAD );
		y += 22.0f;
	}
	panel.height = y + PAD;
	Plate( panel, palette.plate, WithAlpha( palette.icon, 0.5f ) );
}

void idVRWristDisplay::Notice( wristPanel_t &panel, const notice_t &notice, float y, const palette_t &palette, float alpha ) {
	float x = PAD;
	if ( notice.icon != NULL ) {
		Icon( panel, notice.icon, x, y, 22.0f, 18.0f, WithAlpha( palette.icon, alpha ) );
		x += 26.0f;
	}
	if ( notice.label.Length() ) {
		text.Draw( panel.items, notice.label.c_str(), x, y + 12.0f, 11.0f, 0, WithAlpha( palette.warn, alpha ), PANEL_WIDTH - PAD - x );
		y += 16.0f;
	}
	text.Draw( panel.items, notice.text.c_str(), x, y + 14.0f, 12.0f, 0, WithAlpha( palette.readout, alpha ), PANEL_WIDTH - PAD - x );
}

void idVRWristDisplay::LayoutVitals( const vrWristStatus_t &status, const palette_t &palette, wristPanel_t &panel ) {
	panel.items.SetNum( 0, false );
	const int now = gameLocal.time;
	const float half = ( PANEL_WIDTH - 3.0f * PAD ) * 0.5f;
	const float top = PAD;
	// health, low under a quarter of its maximum: red, pulsing at 2 Hz
	const bool low = VRWrist_LowHealth( status.health, status.maxHealth );
	const float pulse = 0.5f + 0.5f * idMath::Cos( MS2SEC( now ) * idMath::TWO_PI * 2.0f );
	const idVec4 healthColor = low ? palette.low : palette.readout;
	const idVec4 healthIcon_ = low ? palette.low : palette.icon;
	Icon( panel, healthIcon, PAD, top + 3.0f, 30.0f, 30.0f, healthIcon_ );
	text.Draw( panel.items, va( "%d", Max( 0, status.health ) ), PAD + 34.0f, top + 34.0f, 30.0f, 0, healthColor, half - 34.0f );
	Bar( panel, PAD, top + 42.0f, half, 6.0f, static_cast<float>( status.health ) / Max( 1, status.maxHealth ), palette,
		low ? palette.low : palette.fill );
	// armor
	const float ax = PAD * 2.0f + half;
	Icon( panel, armorIcon, ax, top + 3.0f, 30.0f, 30.0f, palette.icon );
	text.Draw( panel.items, va( "%d", Max( 0, status.armor ) ), ax + 34.0f, top + 34.0f, 30.0f, 0, palette.readout, half - 34.0f );
	Bar( panel, ax, top + 42.0f, half, 6.0f, static_cast<float>( status.armor ) / Max( 1, status.maxArmor ), palette, palette.fill );
	float y = top + 54.0f;

	// air under water
	if ( status.air < 100 ) {
		y += 4.0f;
		const idVec4 color = status.air <= 25 ? palette.low : palette.readout;
		text.Draw( panel.items, common->GetLocalizedString( "#str_231326" ), PAD, y + 12.0f, 11.0f, 0, color, 46.0f );
		Bar( panel, PAD + 52.0f, y + 4.0f, PANEL_WIDTH - 2.0f * PAD - 52.0f, 6.0f, status.air / 100.0f, palette,
			status.air <= 25 ? palette.low : palette.fill );
		y += 16.0f;
	}
	// a boss's health
	if ( status.boss >= 0.0f ) {
		y += 4.0f;
		text.Draw( panel.items, common->GetLocalizedString( "#str_233567" ), PAD, y + 12.0f, 11.0f, 0, palette.warn, 46.0f );
		Bar( panel, PAD + 52.0f, y + 4.0f, PANEL_WIDTH - 2.0f * PAD - 52.0f, 6.0f, status.boss, palette, WithAlpha( palette.low, 0.85f ) );
		y += 16.0f;
	}
	// powerups and a carried flag
	if ( status.powerups > 0 || status.flag ) {
		y += 4.0f;
		float x = PAD;
		if ( status.flag ) {
			Icon( panel, flagIcon, x, y, 22.0f, 20.0f, WithAlpha( palette.warn, 0.6f + 0.4f * pulse ) );
			x += 28.0f;
		}
		for ( int i = 0; i < status.powerups && i < VR_WRIST_MAX_POWERUPS; i++ ) {
			Icon( panel, status.powerupIcon[i], x, y, 20.0f, 20.0f, colorWhite );
			x += 22.0f;
			if ( status.powerupSeconds[i] >= 0 ) {
				const char *seconds = va( "%d", status.powerupSeconds[i] );
				text.Draw( panel.items, seconds, x, y + 15.0f, 12.0f, 0, palette.readout );
				x += text.Width( seconds, 12.0f ) + 6.0f;
			}
			x += 4.0f;
		}
		y += 22.0f;
	}
	// the newest notices, the newest nearest the figures
	for ( int i = 0; i < 2; i++ ) {
		const notice_t &notice = notices[i];
		const int age = now - notice.start;
		if ( notice.text.Length() == 0 || age < 0 || age >= notice.life ) {
			continue;
		}
		const float alpha = idMath::ClampFloat( 0.0f, 1.0f, ( notice.life - age ) / static_cast<float>( NOTICE_FADE_MSEC ) );
		y += 4.0f;
		Notice( panel, notice, y, palette, alpha );
		y += notice.label.Length() ? 36.0f : 20.0f;
	}
	panel.height = y + PAD;
	idVec4 plate = palette.plate;
	idVec4 rim = WithAlpha( palette.icon, 0.5f );
	if ( low ) {
		// the plate warms and its rim burns red with the pulse
		plate.x = 0.35f * pulse;
		rim = WithAlpha( palette.low, 0.4f + 0.6f * pulse );
	}
	Plate( panel, plate, rim );
}

// The panel at a wrist: back from the grip along the controller, off it by
// lift, square to the line from the head and upright as the head is held, so
// it reads level wherever it is in view and however the wrist turns.
void idVRWristDisplay::Place( wristPanel_t &panel, const idVec3 &grip, const idMat3 &axis, float lift, const idVec3 &head,
		const idVec3 &headUp, float unitsPerMetre ) {
	panel.centre = grip - axis[0] * ( WRIST_BACK * unitsPerMetre ) + idVec3( 0.0f, 0.0f, lift * unitsPerMetre );
	idVec3 forward = panel.centre - head;
	const float distance = forward.Normalize();
	idVec3 up = headUp - forward * ( headUp * forward );
	if ( up.Normalize() < 1.0e-3f ) {
		up = idVec3( 0.0f, 0.0f, 1.0f ) - forward * forward.z;
		up.Normalize();
	}
	idVec3 right = forward.Cross( up );
	right.Normalize();
	up = right.Cross( forward );
	up.Normalize();
	const float width = idMath::ClampFloat( 0.06f, 0.2f, wristWidth != NULL ? wristWidth->GetFloat() : 0.11f );
	const float scale = width * unitsPerMetre / PANEL_WIDTH;
	panel.right = right * scale;
	panel.up = up * scale;
	const float metres = distance / Max( 1.0f, unitsPerMetre );
	panel.alpha = VRWrist_NearFade( metres );
}

void idVRWristDisplay::Prepare( const vrWristStatus_t &status, const vrFrameState_t &frame, const idVec3 &eyeOrigin, float trackingYaw,
		bool show, bool damageDirection ) {
	Precache();
	const int now = gameLocal.time;
	// a new level, or a load, starts its clock again
	if ( now < lastTime ) {
		notices[0].text.Clear();
		notices[1].text.Clear();
		weaponChanged = 0;
		lastWeapon = -1;
	}
	lastTime = now;

	panels[0].visible = panels[1].visible = false;
	showHit = false;
	if ( !frame.head.valid ) {
		return;
	}
	const idVec3 correction = VR_HeadOffsetCorrection( frame );
	vrPose_t head = frame.head;
	head.origin += correction;
	VR_PoseToWorld( head, eyeOrigin, trackingYaw, headOrigin, headAxis );
	const float unitsPerMetre = frame.unitsPerMetre > 0.0f ? frame.unitsPerMetre : 40.0f;

	// the damage direction round the line of sight
	if ( damageDirection && status.valid && status.hitTime != 0 && ( damageCvar == NULL || damageCvar->GetBool() ) ) {
		const int age = now - status.hitTime;
		if ( age >= 0 && age < HIT_MSEC ) {
			// toward whoever hit: against the hit's travel, in the head's frame
			const idVec3 toward = -status.hitDirection;
			const float ahead = toward * headAxis[0];
			const float left = toward * headAxis[1];
			if ( idMath::Fabs( ahead ) + idMath::Fabs( left ) > 1.0e-4f ) {
				showHit = true;
				hitAngle = VRWrist_HitAngle( ahead, left );
				hitAlpha = VRWrist_HitAlpha( age, HIT_MSEC, reducedFlashing != NULL && reducedFlashing->GetBool() );
				hitDistance = HIT_DISTANCE * unitsPerMetre;
			}
		}
	}

	if ( !show || !status.valid ) {
		return;
	}
	if ( status.hasWeapon && status.weaponIndex != lastWeapon ) {
		// the first weapon of a life shows no name: nothing changed
		weaponChanged = lastWeapon >= 0 ? now : 0;
		lastWeapon = status.weaponIndex;
	}

	const palette_t &palette = status.strogg ? stroggPalette : marinePalette;
	const int weaponHand = frame.weaponHand == VR_HAND_LEFT ? VR_HAND_LEFT : VR_HAND_RIGHT;
	const int offHand = weaponHand == VR_HAND_LEFT ? VR_HAND_RIGHT : VR_HAND_LEFT;
	for ( int side = 0; side < 2; side++ ) {
		const int hand = side == 0 ? weaponHand : offHand;
		const vrPose_t &tracked = frame.grip[hand].valid ? frame.grip[hand] : frame.aim[hand];
		if ( !tracked.valid || !frame.aim[hand].valid ) {
			continue;
		}
		vrPose_t aim = frame.aim[hand];
		aim.origin += correction;
		vrPose_t grip = tracked;
		grip.origin += correction;
		idVec3 aimOrigin, gripOrigin;
		idMat3 aimAxis, gripAxis;
		VR_PoseToWorld( aim, eyeOrigin, trackingYaw, aimOrigin, aimAxis );
		VR_PoseToWorld( grip, eyeOrigin, trackingYaw, gripOrigin, gripAxis );
		wristPanel_t &panel = panels[side];
		if ( side == 0 ) {
			if ( !status.hasWeapon ) {
				continue;
			}
			LayoutWeapon( status, palette, panel );
		} else {
			LayoutVitals( status, palette, panel );
		}
		Place( panel, gripOrigin, aimAxis, side == 0 ? WEAPON_WRIST_LIFT : OFF_WRIST_LIFT, headOrigin, headAxis[2], unitsPerMetre );
		// the off hand on the foregrip puts its wrist under the line of fire
		if ( side == 1 && frame.twoHanded ) {
			panel.alpha *= HOLDING_DIM;
		}
		panel.visible = panel.alpha > 0.01f && panel.items.Num() > 0;
	}
}

// Draws a triangle in the order the 2D pass keeps: clockwise on the canvas,
// whose y runs down (counter-clockwise in the eye's tangent space).
void DrawKeptTri( const idVec2 &a, const idVec2 &b, const idVec2 &c, const idVec2 &ta, const idVec2 &tb, const idVec2 &tc,
		const idMaterial *material ) {
	const float cross = ( b.x - a.x ) * ( c.y - a.y ) - ( b.y - a.y ) * ( c.x - a.x );
	if ( cross > 0.0f ) {
		renderSystem->DrawStretchTri( a, c, b, ta, tc, tb, material );
	} else {
		renderSystem->DrawStretchTri( a, b, c, ta, tb, tc, material );
	}
}

bool EyePoint( const renderView_t &lens, const idVec3 &point, idVec2 &canvas ) {
	const vrEyeTangent_t tangent = VR_EyeTangent( lens, point );
	if ( !tangent.inFront ) {
		return false;
	}
	canvas = VR_EyeCanvasPoint( lens, tangent.x, tangent.y );
	return true;
}

void idVRWristDisplay::DrawPanel( const renderView_t &lens, const wristPanel_t &panel ) {
	const idVec3 origin = panel.centre - panel.right * ( PANEL_WIDTH * 0.5f ) + panel.up * ( panel.height * 0.5f );
	for ( int i = 0; i < panel.items.Num(); i++ ) {
		const uiHudPrimitive_t &item = panel.items[i];
		if ( item.material == NULL || item.color.w * panel.alpha <= 0.0f ) {
			continue;
		}
		idVec2 canvas[4];
		bool inFront = true;
		for ( int n = 0; n < 4 && inFront; n++ ) {
			inFront = EyePoint( lens, origin + panel.right * item.points[n].x - panel.up * item.points[n].y, canvas[n] );
		}
		if ( !inFront ) {
			continue;
		}
		const idVec2 uv[4] = { idVec2( item.uv.x, item.uv.y ), idVec2( item.uv.z, item.uv.y ), idVec2( item.uv.z, item.uv.w ),
			idVec2( item.uv.x, item.uv.w ) };
		renderSystem->SetColor4( item.color.x, item.color.y, item.color.z, item.color.w * panel.alpha );
		DrawKeptTri( canvas[0], canvas[3], canvas[2], uv[0], uv[3], uv[2], item.material );
		DrawKeptTri( canvas[0], canvas[2], canvas[1], uv[0], uv[2], uv[1], item.material );
	}
}

// Arcs on a ring about 13 degrees round where the head looks, on the side
// the hit came from: ahead at the top, behind at the bottom.
void idVRWristDisplay::DrawHitArcs( const renderView_t &lens ) {
	const int segments = 8;
	const idVec3 ahead = headOrigin + headAxis[0] * hitDistance;
	const idVec3 right = -headAxis[1] * hitDistance;
	const idVec3 up = headAxis[2] * hitDistance;
	const float inner = HIT_RING, outer = HIT_RING + HIT_BAND;
	idVec2 previousInner, previousOuter;
	bool previous = false;
	for ( int i = 0; i <= segments; i++ ) {
		const float along = static_cast<float>( i ) / segments;
		const float angle = hitAngle + ( along - 0.5f ) * HIT_ARC;
		float s, c;
		idMath::SinCos( angle, s, c );
		idVec2 nearPoint, farPoint;
		const bool shown = EyePoint( lens, ahead + right * ( s * inner ) + up * ( c * inner ), nearPoint ) &&
			EyePoint( lens, ahead + right * ( s * outer ) + up * ( c * outer ), farPoint );
		if ( shown && previous ) {
			// brightest at the arc's middle
			const float middle = 1.0f - idMath::Fabs( ( static_cast<float>( i ) - 0.5f ) / segments - 0.5f ) * 1.4f;
			renderSystem->SetColor4( 1.0f, 0.24f, 0.08f, hitAlpha * middle );
			DrawKeptTri( previousInner, previousOuter, farPoint, vec2_origin, vec2_origin, vec2_origin, white );
			DrawKeptTri( previousInner, farPoint, nearPoint, vec2_origin, vec2_origin, vec2_origin, white );
		}
		previousInner = nearPoint;
		previousOuter = farPoint;
		previous = shown;
	}
}

void idVRWristDisplay::DrawEye( const renderView_t &lens ) {
	if ( !lens.asymmetricFov || white == NULL || ( !panels[0].visible && !panels[1].visible && !showHit ) ) {
		return;
	}
	const bool previousUIViewportMode = renderSystem->GetUseUIViewportFor2D();
	renderSystem->SetUseUIViewportFor2D( false );
	for ( int side = 0; side < 2; side++ ) {
		if ( panels[side].visible ) {
			DrawPanel( lens, panels[side] );
		}
	}
	if ( showHit ) {
		DrawHitArcs( lens );
	}
	renderSystem->SetColor4( 1.0f, 1.0f, 1.0f, 1.0f );
	renderSystem->SetUseUIViewportFor2D( previousUIViewportMode );
}

}	// namespace

bool VRWrist_HudOnWrists( void ) {
	return wristDisplay.guiPass;
}

void VRWrist_SetGuiPass( bool wrists ) {
	wristDisplay.guiPass = wrists;
}

bool VRWrist_Selected( void ) {
	return wristDisplay.Selected();
}

void VRWrist_NotePickup( const char *text, const char *icon ) {
	wristDisplay.NotePickup( text, icon );
}

void VRWrist_NoteObjective( const char *title, bool complete ) {
	wristDisplay.NoteObjective( title, complete );
}

void VRWrist_Prepare( const vrWristStatus_t &status, const vrFrameState_t &frame, const idVec3 &eyeOrigin, float trackingYaw,
		bool show, bool damageDirection ) {
	wristDisplay.Prepare( status, frame, eyeOrigin, trackingYaw, show, damageDirection );
}

void VRWrist_DrawEye( const renderView_s &lens ) {
	wristDisplay.DrawEye( lens );
}
