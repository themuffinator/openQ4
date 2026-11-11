// Shared SP/MP classic wheel. Quake 4 SDK EULA. Retained mode uses authored .rui documents instead.
namespace {
const int	WEAPON_WHEEL_SLOT_COUNT			= 12;
const int	WEAPON_WHEEL_WEDGE_SUBDIVISIONS	= 12;

const float	WEAPON_WHEEL_CENTER_X			= SCREEN_WIDTH * 0.77f;
const float	WEAPON_WHEEL_CENTER_Y			= SCREEN_HEIGHT * 0.50f;
const float	WEAPON_WHEEL_INNER_RADIUS		= 54.0f;
const float	WEAPON_WHEEL_OUTER_RADIUS		= 122.0f;
const float	WEAPON_WHEEL_CURSOR_RADIUS		= 118.0f;
const float	WEAPON_WHEEL_DEADZONE_RADIUS	= 24.0f;
const float	WEAPON_WHEEL_SEGMENT_ANGLE_PAD	= DEG2RAD( 1.25f );
const float	WEAPON_WHEEL_SEGMENT_FILL_INSET	= 1.5f;
const float	WEAPON_WHEEL_SEGMENT_OUTER_GAP	= 3.0f;
const float	WEAPON_WHEEL_SEGMENT_RADIAL_INSET = 0.0f;
const float	WEAPON_WHEEL_SEGMENT_DETAIL_RADIUS = 92.0f;
const float	WEAPON_WHEEL_SEGMENT_DETAIL_HALF_WIDTH = 0.9f;
const float	WEAPON_WHEEL_SEGMENT_DETAIL_SWEEP = 0.60f;
const float	WEAPON_WHEEL_SEGMENT_CONTENT_MARGIN = 7.0f;
const float	WEAPON_WHEEL_SEGMENT_ICON_RADIUS = 88.0f;
const float	WEAPON_WHEEL_SEGMENT_ICON_SIZE	= 30.0f;
const float	WEAPON_WHEEL_SEGMENT_ICON_MIN_SIZE = 20.0f;
const float	WEAPON_WHEEL_CURSOR_SIZE		= 6.0f;
const float	WEAPON_WHEEL_CURSOR_THICKNESS	= 1.15f;
const float	WEAPON_WHEEL_LINE_THICKNESS		= 1.6f;
const float	WEAPON_WHEEL_INNER_RING_RADIUS	= WEAPON_WHEEL_INNER_RADIUS + 1.8f;
const float	WEAPON_WHEEL_INNER_RING_HALF_WIDTH = 1.7f;
const float	WEAPON_WHEEL_INNER_RING_CORE_HALF_WIDTH = 0.8f;
const float	WEAPON_WHEEL_MOUSE_SENSITIVITY	= 0.70f;
const float	WEAPON_WHEEL_TIMESCALE_SCALE	= 0.18f;
const float	WEAPON_WHEEL_BLEND_IN_SPEED		= 8.0f;
const float	WEAPON_WHEEL_BLEND_OUT_SPEED	= 9.0f;
const float	WEAPON_WHEEL_TRANSITION_MIN_SCALE = 0.94f;
const int	WEAPON_WHEEL_RADIAL_FADE_STEPS	= 6;
const int	WEAPON_WHEEL_ARROW_FADE_STEPS	= 10;
const float	WEAPON_WHEEL_ARROW_OFFSET		= WEAPON_WHEEL_INNER_RADIUS - 7.0f;
const float	WEAPON_WHEEL_ARROW_LENGTH		= 11.0f;
const float	WEAPON_WHEEL_ARROW_HALF_WIDTH	= 22.0f;
const float	WEAPON_WHEEL_CENTER_TEXT_SCALE	= 0.54f;
const float	WEAPON_WHEEL_CENTER_TEXT_MIN_SCALE = 0.34f;
const float	WEAPON_WHEEL_CENTER_TEXT_RADIUS = WEAPON_WHEEL_INNER_RADIUS - 10.0f;
const float	WEAPON_WHEEL_CENTER_TEXT_PADDING = 6.0f;
const float	WEAPON_WHEEL_CENTER_TEXT_STEP	= 0.02f;
const float	WEAPON_WHEEL_CENTER_TEXT_GAP	= 2.0f;
const float	WEAPON_WHEEL_CENTER_AMMO_SCALE	= 0.96f;
const float	WEAPON_WHEEL_CENTER_AMMO_MIN_SCALE = 0.48f;
const float	WEAPON_WHEEL_CENTER_STACK_GAP	= 4.0f;

const int weaponWheelSlotWeapons[ WEAPON_WHEEL_SLOT_COUNT ] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, -1 };

static float WeaponWheelLerp( float a, float b, float t ) {
	return a + ( b - a ) * t;
}

static float WeaponWheelTransitionScale( float blend ) {
	return WeaponWheelLerp( WEAPON_WHEEL_TRANSITION_MIN_SCALE, 1.0f, idMath::ClampFloat( 0.0f, 1.0f, blend ) );
}

static idVec2 WeaponWheelDirection( float angle ) {
	return idVec2( idMath::Sin( angle ), -idMath::Cos( angle ) );
}

static float WeaponWheelGetAspectScale( void ) {
	const float baseAspect = static_cast<float>( SCREEN_WIDTH ) / static_cast<float>( SCREEN_HEIGHT );
	const float currentAspect = idMath::ClampFloat( 0.1f, 10.0f, gameLocal.GetScreenAspectRatio() );
	return baseAspect / currentAspect;
}

static idVec2 WeaponWheelProjectPoint( const idVec2& center, const idVec2& localPoint, float aspectScale ) {
	return idVec2( center.x + localPoint.x * aspectScale, center.y + localPoint.y );
}

static idVec2 WeaponWheelScreenToLocalPoint( const idVec2& center, const idVec2& screenPoint, float aspectScale ) {
	if ( aspectScale <= 0.0f ) {
		return idVec2( 0.0f, 0.0f );
	}

	return idVec2( ( screenPoint.x - center.x ) / aspectScale, screenPoint.y - center.y );
}

static float WeaponWheelNormalizeAngle( float angle ) {
	while ( angle > idMath::PI ) {
		angle -= idMath::TWO_PI;
	}
	while ( angle <= -idMath::PI ) {
		angle += idMath::TWO_PI;
	}

	return angle;
}

static void DrawWeaponWheelQuad( const idVec2& p0, const idVec2& p1, const idVec2& p2, const idVec2& p3, const idMaterial* material ) {
	if ( material == NULL ) {
		return;
	}

	renderSystem->DrawStretchTri( p0, p1, p2, idVec2( 0.0f, 0.0f ), idVec2( 1.0f, 0.0f ), idVec2( 1.0f, 1.0f ), material );
	renderSystem->DrawStretchTri( p0, p2, p3, idVec2( 0.0f, 0.0f ), idVec2( 1.0f, 1.0f ), idVec2( 0.0f, 1.0f ), material );
}

static void DrawWeaponWheelLine( const idVec2& center, const idVec2& from, const idVec2& to, float halfWidth, float aspectScale, const idMaterial* material ) {
	idVec2 dir = to - from;
	float length = dir.Length();
	if ( length <= 0.0f || material == NULL ) {
		return;
	}

	dir *= ( 1.0f / length );
	const idVec2 perp( -dir.y, dir.x );
	const idVec2 p0 = WeaponWheelProjectPoint( center, from + perp * halfWidth, aspectScale );
	const idVec2 p1 = WeaponWheelProjectPoint( center, to + perp * halfWidth, aspectScale );
	const idVec2 p2 = WeaponWheelProjectPoint( center, to - perp * halfWidth, aspectScale );
	const idVec2 p3 = WeaponWheelProjectPoint( center, from - perp * halfWidth, aspectScale );
	DrawWeaponWheelQuad( p0, p1, p2, p3, material );
}

static void DrawWeaponWheelFadedLine( const idVec2& center, const idVec2& from, const idVec2& to, float halfWidth, float aspectScale, const idMaterial* material, const idVec4& color ) {
	if ( material == NULL || color.w <= 0.0f ) {
		return;
	}

	const idVec2 delta = to - from;
	if ( delta.LengthSqr() <= 0.0f ) {
		return;
	}

	for ( int step = 0; step < WEAPON_WHEEL_RADIAL_FADE_STEPS; ++step ) {
		const float frac0 = static_cast<float>( step ) / WEAPON_WHEEL_RADIAL_FADE_STEPS;
		const float frac1 = static_cast<float>( step + 1 ) / WEAPON_WHEEL_RADIAL_FADE_STEPS;
		const float alphaFrac = 1.0f - ( frac0 + frac1 ) * 0.5f;
		const float alpha = color.w * Max( 0.0f, alphaFrac );
		if ( alpha <= 0.0f ) {
			continue;
		}

		renderSystem->SetColor4( color.x, color.y, color.z, alpha );
		DrawWeaponWheelLine(
			center,
			from + delta * frac0,
			from + delta * frac1,
			halfWidth,
			aspectScale,
			material );
	}
}

static void DrawWeaponWheelSegment( const idVec2& center, const idVec2& popoutOffset, float startAngle, float endAngle, float innerRadius, float outerRadius, float aspectScale, const idMaterial* material ) {
	if ( material == NULL ) {
		return;
	}

	for ( int step = 0; step < WEAPON_WHEEL_WEDGE_SUBDIVISIONS; ++step ) {
		const float frac0 = static_cast<float>( step ) / WEAPON_WHEEL_WEDGE_SUBDIVISIONS;
		const float frac1 = static_cast<float>( step + 1 ) / WEAPON_WHEEL_WEDGE_SUBDIVISIONS;
		const float angle0 = startAngle + ( endAngle - startAngle ) * frac0;
		const float angle1 = startAngle + ( endAngle - startAngle ) * frac1;
		const idVec2 dir0 = WeaponWheelDirection( angle0 );
		const idVec2 dir1 = WeaponWheelDirection( angle1 );
		const idVec2 inner0 = WeaponWheelProjectPoint( center, popoutOffset + dir0 * innerRadius, aspectScale );
		const idVec2 outer0 = WeaponWheelProjectPoint( center, popoutOffset + dir0 * outerRadius, aspectScale );
		const idVec2 inner1 = WeaponWheelProjectPoint( center, popoutOffset + dir1 * innerRadius, aspectScale );
		const idVec2 outer1 = WeaponWheelProjectPoint( center, popoutOffset + dir1 * outerRadius, aspectScale );

		renderSystem->DrawStretchTri( inner0, outer0, outer1, idVec2( 0.0f, 0.0f ), idVec2( 1.0f, 0.0f ), idVec2( 1.0f, 1.0f ), material );
		renderSystem->DrawStretchTri( inner0, outer1, inner1, idVec2( 0.0f, 0.0f ), idVec2( 1.0f, 1.0f ), idVec2( 0.0f, 1.0f ), material );
	}
}

static void DrawWeaponWheelArc( const idVec2& center, const idVec2& popoutOffset, float radius, float halfWidth, float startAngle, float endAngle, float aspectScale, const idMaterial* material ) {
	if ( material == NULL ) {
		return;
	}

	const float innerRadius = Max( 0.0f, radius - halfWidth );
	const float outerRadius = radius + halfWidth;
	DrawWeaponWheelSegment( center, popoutOffset, startAngle, endAngle, innerRadius, outerRadius, aspectScale, material );
}

static void DrawWeaponWheelArrow( const idVec2& center, const idVec2& dir, float offset, float length, float halfWidth, float aspectScale, const idMaterial* material, const idVec4& color ) {
	if ( material == NULL || color.w <= 0.0f ) {
		return;
	}

	const idVec2 perp( -dir.y, dir.x );
	const idVec2 baseCenterLocal = dir * ( offset - length );

	for ( int step = 0; step < WEAPON_WHEEL_ARROW_FADE_STEPS; ++step ) {
		const float frac0 = static_cast<float>( step ) / WEAPON_WHEEL_ARROW_FADE_STEPS;
		const float frac1 = static_cast<float>( step + 1 ) / WEAPON_WHEEL_ARROW_FADE_STEPS;
		const idVec2 center0Local = baseCenterLocal + dir * ( length * frac0 );
		const idVec2 center1Local = baseCenterLocal + dir * ( length * frac1 );
		const float halfWidth0 = halfWidth * ( 1.0f - frac0 );
		const float halfWidth1 = halfWidth * ( 1.0f - frac1 );
		const float alpha = color.w * frac1;
		if ( alpha <= 0.0f ) {
			continue;
		}

		renderSystem->SetColor4( color.x, color.y, color.z, alpha );

		if ( step == WEAPON_WHEEL_ARROW_FADE_STEPS - 1 ) {
			const idVec2 tip = WeaponWheelProjectPoint( center, center1Local, aspectScale );
			const idVec2 left = WeaponWheelProjectPoint( center, center0Local + perp * halfWidth0, aspectScale );
			const idVec2 right = WeaponWheelProjectPoint( center, center0Local - perp * halfWidth0, aspectScale );
			renderSystem->DrawStretchTri( tip, left, right, idVec2( 0.0f, 0.0f ), idVec2( 1.0f, 0.0f ), idVec2( 0.0f, 1.0f ), material );
		} else {
			const idVec2 p0 = WeaponWheelProjectPoint( center, center0Local + perp * halfWidth0, aspectScale );
			const idVec2 p1 = WeaponWheelProjectPoint( center, center1Local + perp * halfWidth1, aspectScale );
			const idVec2 p2 = WeaponWheelProjectPoint( center, center1Local - perp * halfWidth1, aspectScale );
			const idVec2 p3 = WeaponWheelProjectPoint( center, center0Local - perp * halfWidth0, aspectScale );
			DrawWeaponWheelQuad( p0, p1, p2, p3, material );
		}
	}
}

static bool WeaponWheelLocalPointInsideSegment( const idVec2& point, const idVec2& popoutOffset, float centerAngle, float halfAngle, float innerRadius, float outerRadius, float margin ) {
	const idVec2 relative = point - popoutOffset;
	const float radius = relative.Length();
	if ( radius < innerRadius + margin || radius > outerRadius - margin ) {
		return false;
	}

	const float angle = atan2f( relative.x, -relative.y );
	const float delta = WeaponWheelNormalizeAngle( angle - centerAngle );
	return idMath::Fabs( delta ) <= halfAngle;
}

static bool WeaponWheelRectFitsSegment( const idVec2& center, const idVec2& localCenter, float screenWidth, float screenHeight, float aspectScale, const idVec2& popoutOffset, float centerAngle, float halfAngle, float innerRadius, float outerRadius, float margin ) {
	const idVec2 screenCenter = WeaponWheelProjectPoint( center, localCenter, aspectScale );
	const float halfWidth = screenWidth * 0.5f;
	const float halfHeight = screenHeight * 0.5f;
	idVec2 samples[ 9 ];

	samples[ 0 ] = idVec2( screenCenter.x - halfWidth, screenCenter.y - halfHeight );
	samples[ 1 ] = idVec2( screenCenter.x + halfWidth, screenCenter.y - halfHeight );
	samples[ 2 ] = idVec2( screenCenter.x + halfWidth, screenCenter.y + halfHeight );
	samples[ 3 ] = idVec2( screenCenter.x - halfWidth, screenCenter.y + halfHeight );
	samples[ 4 ] = idVec2( screenCenter.x, screenCenter.y );
	samples[ 5 ] = idVec2( screenCenter.x, screenCenter.y - halfHeight );
	samples[ 6 ] = idVec2( screenCenter.x + halfWidth, screenCenter.y );
	samples[ 7 ] = idVec2( screenCenter.x, screenCenter.y + halfHeight );
	samples[ 8 ] = idVec2( screenCenter.x - halfWidth, screenCenter.y );

	for ( int i = 0; i < 9; ++i ) {
		const idVec2 localPoint = WeaponWheelScreenToLocalPoint( center, samples[ i ], aspectScale );
		if ( !WeaponWheelLocalPointInsideSegment( localPoint, popoutOffset, centerAngle, halfAngle, innerRadius, outerRadius, margin ) ) {
			return false;
		}
	}

	return true;
}

static float WeaponWheelFitSquareInSegment( const idVec2& center, const idVec2& localCenter, float desiredSize, float minSize, float aspectScale, const idVec2& popoutOffset, float centerAngle, float halfAngle, float innerRadius, float outerRadius, float margin ) {
	if ( desiredSize <= 0.0f || minSize <= 0.0f || desiredSize < minSize ) {
		return 0.0f;
	}

	if ( !WeaponWheelRectFitsSegment( center, localCenter, minSize * aspectScale, minSize, aspectScale, popoutOffset, centerAngle, halfAngle, innerRadius, outerRadius, margin ) ) {
		return 0.0f;
	}

	float low = minSize;
	float high = desiredSize;
	if ( WeaponWheelRectFitsSegment( center, localCenter, desiredSize * aspectScale, desiredSize, aspectScale, popoutOffset, centerAngle, halfAngle, innerRadius, outerRadius, margin ) ) {
		return desiredSize;
	}

	for ( int i = 0; i < 8; ++i ) {
		const float mid = ( low + high ) * 0.5f;
		if ( WeaponWheelRectFitsSegment( center, localCenter, mid * aspectScale, mid, aspectScale, popoutOffset, centerAngle, halfAngle, innerRadius, outerRadius, margin ) ) {
			low = mid;
		} else {
			high = mid;
		}
	}

	return low;
}

static float WeaponWheelStringWidth( const char* string, float aspectScale, float textScale ) {
	if ( string == NULL ) {
		return 0.0f;
	}

	float width = 0.0f;
	for ( const char* s = string; *s != '\0'; ++s ) {
		width += BIGCHAR_WIDTH * aspectScale * textScale;
	}

	return width;
}

static void DrawWeaponWheelString( float x, float y, const char* string, const idVec4& color, float aspectScale, float textScale, const idMaterial* material ) {
	if ( string == NULL || string[ 0 ] == '\0' || material == NULL ) {
		return;
	}

	float drawX = x;
	const float charWidth = BIGCHAR_WIDTH * aspectScale * textScale;
	const float charHeight = BIGCHAR_HEIGHT * textScale;

	renderSystem->SetColor( color );
	for ( const unsigned char* s = reinterpret_cast<const unsigned char*>( string ); *s != '\0'; ++s ) {
		if ( *s == ' ' ) {
			drawX += charWidth;
			continue;
		}

		const int row = ( *s ) >> 4;
		const int col = ( *s ) & 15;
		const float frow = row * 0.0625f;
		const float fcol = col * 0.0625f;
		const float size = 0.0625f;

		renderSystem->DrawStretchPic( drawX, y, charWidth, charHeight, fcol, frow, fcol + size, frow + size, material );
		drawX += charWidth;
	}
	renderSystem->SetColor( colorWhite );
}

static void WeaponWheelWrapString( const char* string, float maxWidth, float aspectScale, float textScale, idStrList& lines ) {
	lines.Clear();
	if ( string == NULL || string[ 0 ] == '\0' ) {
		return;
	}

	const char* cursor = string;
	idStr currentLine;

	while ( *cursor != '\0' ) {
		while ( *cursor == ' ' ) {
			cursor++;
		}

		if ( *cursor == '\0' ) {
			break;
		}

		const char* wordStart = cursor;
		while ( *cursor != '\0' && *cursor != ' ' ) {
			cursor++;
		}

		idStr word;
		word.Append( wordStart, cursor - wordStart );

		idStr candidate = currentLine;
		if ( candidate.Length() > 0 ) {
			candidate += " ";
		}
		candidate += word;

		if ( currentLine.Length() > 0 && WeaponWheelStringWidth( candidate.c_str(), aspectScale, textScale ) > maxWidth ) {
			lines.Append( currentLine );
			currentLine = word;
		} else {
			currentLine = candidate;
		}
	}

	if ( currentLine.Length() > 0 ) {
		lines.Append( currentLine );
	}
}

static bool WeaponWheelWrappedTextFitsCircle( const idStrList& lines, float aspectScale, float textScale, float radius, float gap, float padding ) {
	if ( lines.Num() <= 0 ) {
		return false;
	}

	const float usableRadius = Max( 0.0f, radius - padding );
	const float lineHeight = BIGCHAR_HEIGHT * textScale;
	const float totalHeight = lines.Num() * lineHeight + Max( 0, lines.Num() - 1 ) * gap;
	if ( totalHeight > usableRadius * 2.0f ) {
		return false;
	}

	float lineCenterY = -totalHeight * 0.5f + lineHeight * 0.5f;
	for ( int i = 0; i < lines.Num(); ++i ) {
		const float y = lineCenterY + i * ( lineHeight + gap );
		const float remaining = usableRadius * usableRadius - y * y;
		if ( remaining <= 0.0f ) {
			return false;
		}

		const float allowedWidth = Max( 0.0f, 2.0f * idMath::Sqrt( remaining ) * aspectScale - padding * 2.0f * aspectScale );
		if ( WeaponWheelStringWidth( lines[ i ].c_str(), aspectScale, textScale ) > allowedWidth ) {
			return false;
		}
	}

	return true;
}

static bool WeaponWheelCenterStackFitsCircle( const idStrList& nameLines, float nameScale, const char* ammoString, float ammoScale, float aspectScale, float radius, float padding, float lineGap, float stackGap ) {
	if ( nameLines.Num() <= 0 ) {
		return false;
	}

	const bool showAmmo = ( ammoString != NULL && ammoString[ 0 ] != '\0' && ammoScale > 0.0f );
	const float usableRadius = Max( 0.0f, radius - padding );
	const float nameLineHeight = BIGCHAR_HEIGHT * nameScale;
	const float ammoLineHeight = BIGCHAR_HEIGHT * ammoScale;
	const float nameBlockHeight = nameLines.Num() * nameLineHeight + Max( 0, nameLines.Num() - 1 ) * lineGap;
	const float totalHeight = nameBlockHeight + ( showAmmo ? stackGap + ammoLineHeight : 0.0f );
	if ( totalHeight > usableRadius * 2.0f ) {
		return false;
	}

	float centerY = -totalHeight * 0.5f + nameLineHeight * 0.5f;
	for ( int i = 0; i < nameLines.Num(); ++i ) {
		const float remaining = usableRadius * usableRadius - centerY * centerY;
		if ( remaining <= 0.0f ) {
			return false;
		}

		const float allowedWidth = Max( 0.0f, 2.0f * idMath::Sqrt( remaining ) * aspectScale - padding * 2.0f * aspectScale );
		if ( WeaponWheelStringWidth( nameLines[ i ].c_str(), aspectScale, nameScale ) > allowedWidth ) {
			return false;
		}

		centerY += nameLineHeight + lineGap;
	}

	if ( showAmmo ) {
		const float ammoCenterY = totalHeight * 0.5f - ammoLineHeight * 0.5f;
		const float remaining = usableRadius * usableRadius - ammoCenterY * ammoCenterY;
		if ( remaining <= 0.0f ) {
			return false;
		}

		const float allowedWidth = Max( 0.0f, 2.0f * idMath::Sqrt( remaining ) * aspectScale - padding * 2.0f * aspectScale );
		if ( WeaponWheelStringWidth( ammoString, aspectScale, ammoScale ) > allowedWidth ) {
			return false;
		}
	}

	return true;
}

static bool WeaponWheelFitCenterStack( const char* nameString, const char* ammoString, float aspectScale, idStrList& nameLines, float& nameScale, float& ammoScale ) {
	nameLines.Clear();
	nameScale = 0.0f;
	ammoScale = 0.0f;

	if ( nameString == NULL || nameString[ 0 ] == '\0' ) {
		return false;
	}

	const bool showAmmo = ( ammoString != NULL && ammoString[ 0 ] != '\0' );
	for ( float candidateNameScale = WEAPON_WHEEL_CENTER_TEXT_SCALE; candidateNameScale >= WEAPON_WHEEL_CENTER_TEXT_MIN_SCALE; candidateNameScale -= WEAPON_WHEEL_CENTER_TEXT_STEP ) {
		idStrList candidateLines;
		const float wrapWidth = ( WEAPON_WHEEL_CENTER_TEXT_RADIUS * 2.0f - WEAPON_WHEEL_CENTER_TEXT_PADDING * 2.0f ) * aspectScale;
		WeaponWheelWrapString( nameString, wrapWidth, aspectScale, candidateNameScale, candidateLines );
		if ( candidateLines.Num() <= 0 ) {
			continue;
		}

		if ( !showAmmo ) {
			if ( WeaponWheelWrappedTextFitsCircle( candidateLines, aspectScale, candidateNameScale, WEAPON_WHEEL_CENTER_TEXT_RADIUS, WEAPON_WHEEL_CENTER_TEXT_GAP, WEAPON_WHEEL_CENTER_TEXT_PADDING ) ) {
				nameLines = candidateLines;
				nameScale = candidateNameScale;
				return true;
			}

			continue;
		}

		for ( float candidateAmmoScale = WEAPON_WHEEL_CENTER_AMMO_SCALE; candidateAmmoScale >= WEAPON_WHEEL_CENTER_AMMO_MIN_SCALE; candidateAmmoScale -= WEAPON_WHEEL_CENTER_TEXT_STEP ) {
			if ( WeaponWheelCenterStackFitsCircle( candidateLines, candidateNameScale, ammoString, candidateAmmoScale, aspectScale, WEAPON_WHEEL_CENTER_TEXT_RADIUS, WEAPON_WHEEL_CENTER_TEXT_PADDING, WEAPON_WHEEL_CENTER_TEXT_GAP, WEAPON_WHEEL_CENTER_STACK_GAP ) ) {
				nameLines = candidateLines;
				nameScale = candidateNameScale;
				ammoScale = candidateAmmoScale;
				return true;
			}
		}
	}

	return false;
}

static idVec4 WeaponWheelSelectedColor( float alpha ) {
	return idVec4( 0.62f, 0.95f, 0.42f, alpha );
}

static idVec4 WeaponWheelMPNoticeColor( float alpha ) {
	// Match guis/mphud.gui mainNotice forecolor used by the MP warmup center notice.
	return idVec4( 1.0f, 1.0f, 0.549f, alpha );
}

static idVec4 WeaponWheelStateColor( bool hasWeapon, bool lowAmmo, float alpha ) {
	if ( !hasWeapon ) {
		return idVec4( 0.30f, 0.09f, 0.10f, alpha );
	}
	if ( lowAmmo ) {
		return idVec4( 0.94f, 0.54f, 0.16f, alpha );
	}
	return idVec4( 0.82f, 0.92f, 1.00f, alpha );
}
}


void idPlayer::DrawLegacyWeaponWheel( void ) {
	const float blend = idMath::ClampFloat( 0.0f, 1.0f, weaponWheelBlend );
	if ( blend <= 0.0f ) {
		return;
	}

	const idMaterial* whiteMaterial = declManager->FindMaterial( "_white" );
	const idMaterial* fontMaterial = declManager->FindMaterial( "fonts/english/bigchars" );
	if ( whiteMaterial == NULL ) {
		return;
	}

	const idVec2 center( SCREEN_WIDTH * ( 0.5f + idMath::ClampFloat( -40.0f, 40.0f, cvarSystem->GetCVarFloat( "ui_weaponWheelOffset" ) ) / 100.0f ), WEAPON_WHEEL_CENTER_Y );
	const float aspectScale = WeaponWheelGetAspectScale();
	const float transitionScale = WeaponWheelTransitionScale( blend );
	const float slice = idMath::TWO_PI / WEAPON_WHEEL_SLOT_COUNT;
	const float usableHalfAngle = Max( 0.01f, slice * 0.5f - WEAPON_WHEEL_SEGMENT_ANGLE_PAD );
	const float fillAlpha = 0.26f * blend;
	const float detailAlpha = 0.74f * blend;
	const float lineAlpha = 0.92f * blend;
	const float innerRadius = WEAPON_WHEEL_INNER_RADIUS * transitionScale;
	const float outerRadius = WEAPON_WHEEL_OUTER_RADIUS * transitionScale;
	const float segmentFillInset = WEAPON_WHEEL_SEGMENT_FILL_INSET * transitionScale;
	const float segmentOuterGap = WEAPON_WHEEL_SEGMENT_OUTER_GAP * transitionScale;
	const float segmentRadialInset = WEAPON_WHEEL_SEGMENT_RADIAL_INSET * transitionScale;
	const float segmentDetailRadius = WEAPON_WHEEL_SEGMENT_DETAIL_RADIUS * transitionScale;
	const float segmentDetailHalfWidth = WEAPON_WHEEL_SEGMENT_DETAIL_HALF_WIDTH * transitionScale;
	const float segmentContentMargin = WEAPON_WHEEL_SEGMENT_CONTENT_MARGIN * transitionScale;
	const float segmentIconRadius = WEAPON_WHEEL_SEGMENT_ICON_RADIUS * transitionScale;
	const float segmentIconSize = WEAPON_WHEEL_SEGMENT_ICON_SIZE * transitionScale;
	const float segmentIconMinSize = WEAPON_WHEEL_SEGMENT_ICON_MIN_SIZE * transitionScale;
	const float cursorSize = WEAPON_WHEEL_CURSOR_SIZE * transitionScale;
	const float cursorThickness = WEAPON_WHEEL_CURSOR_THICKNESS * transitionScale;
	const float lineThickness = WEAPON_WHEEL_LINE_THICKNESS * transitionScale;
	const float innerRingRadius = WEAPON_WHEEL_INNER_RING_RADIUS * transitionScale;
	const float innerRingHalfWidth = WEAPON_WHEEL_INNER_RING_HALF_WIDTH * transitionScale;
	const float innerRingCoreHalfWidth = WEAPON_WHEEL_INNER_RING_CORE_HALF_WIDTH * transitionScale;
	const float arrowOffset = WEAPON_WHEEL_ARROW_OFFSET * transitionScale;
	const float arrowLength = WEAPON_WHEEL_ARROW_LENGTH * transitionScale;
	const float arrowHalfWidth = WEAPON_WHEEL_ARROW_HALF_WIDTH * transitionScale;
	const float centerTextGap = WEAPON_WHEEL_CENTER_TEXT_GAP * transitionScale;
	const float centerStackGap = WEAPON_WHEEL_CENTER_STACK_GAP * transitionScale;
	const float contentInnerRadius = innerRadius + 8.0f * transitionScale;
	const float contentOuterRadius = outerRadius - 12.0f * transitionScale;

	for ( int slot = 0; slot < WEAPON_WHEEL_SLOT_COUNT; ++slot ) {
		const int weaponIndex = GetWeaponWheelWeaponForSlot( slot );
		const bool hasWeapon = weaponIndex >= 0 && ( inventory.weapons & ( 1 << weaponIndex ) ) != 0;
		const bool lowAmmo = hasWeapon && IsWeaponWheelSlotLowAmmo( slot );
		const float centerAngle = slot * slice;
		const float startAngle = centerAngle - usableHalfAngle;
		const float endAngle = centerAngle + usableHalfAngle;
		const bool hovered = ( slot == weaponWheelHoveredSlot );
		const bool hoveredActive = hovered && hasWeapon;
		const float popout = hoveredActive ? 5.0f * blend * transitionScale : 0.0f;
		const idVec2 popoutOffset = WeaponWheelDirection( centerAngle ) * popout;
		const idVec2 slotDir = WeaponWheelDirection( centerAngle );
		const idVec2 startDir = WeaponWheelDirection( startAngle );
		const idVec2 endDir = WeaponWheelDirection( endAngle );
		const idVec4 selectedColor = WeaponWheelSelectedColor( 1.0f );
		const idVec4 selectedLineColor = WeaponWheelMPNoticeColor( 1.0f );
		const idVec4 stateColor = hoveredActive ? selectedColor : WeaponWheelStateColor( hasWeapon, lowAmmo, 1.0f );
		const float hoverBoost = hoveredActive ? 0.10f * blend : 0.0f;
		const idVec4 fillColor(
			Min( 1.0f, stateColor.x * 0.24f ),
			Min( 1.0f, stateColor.y * 0.24f ),
			Min( 1.0f, stateColor.z * 0.25f ),
			fillAlpha + hoverBoost );
		const idVec4 accentColor(
			Min( 1.0f, stateColor.x * 0.78f + 0.18f ),
			Min( 1.0f, stateColor.y * 0.78f + 0.18f ),
			Min( 1.0f, stateColor.z * 0.78f + 0.18f ),
			detailAlpha + hoverBoost );
		const idVec4 structuralLineColor = hasWeapon ?
			( hoveredActive ? idVec4( selectedLineColor.x, selectedLineColor.y, selectedLineColor.z, lineAlpha ) : idVec4( 0.94f, 0.97f, 1.0f, lineAlpha * 0.92f ) ) :
			idVec4( 0.82f, 0.86f, 0.92f, lineAlpha * 0.45f );
		const idVec4 innerRingColor = hasWeapon ?
			( hoveredActive ? idVec4( selectedLineColor.x, selectedLineColor.y, selectedLineColor.z, lineAlpha ) : idVec4( 0.97f, 0.99f, 1.0f, lineAlpha ) ) :
			idVec4( 0.88f, 0.92f, 0.98f, lineAlpha * 0.70f );
		const idVec4 innerRingCoreColor = hasWeapon ?
			( hoveredActive ? idVec4( selectedLineColor.x, selectedLineColor.y, selectedLineColor.z, lineAlpha ) : idVec4( 1.0f, 1.0f, 1.0f, lineAlpha ) ) :
			idVec4( 0.96f, 0.98f, 1.0f, lineAlpha * 0.82f );

		if ( hasWeapon ) {
			renderSystem->SetColor4( fillColor.x, fillColor.y, fillColor.z, fillColor.w );
			DrawWeaponWheelSegment(
				center,
				popoutOffset,
				startAngle,
				endAngle,
				innerRadius + segmentFillInset,
				outerRadius - segmentOuterGap,
				aspectScale,
				whiteMaterial );
		}

		renderSystem->SetColor4( innerRingColor.x, innerRingColor.y, innerRingColor.z, innerRingColor.w );
		DrawWeaponWheelArc(
			center,
			popoutOffset,
			innerRingRadius,
			innerRingHalfWidth,
			startAngle,
			endAngle,
			aspectScale,
			whiteMaterial );
		renderSystem->SetColor4( innerRingCoreColor.x, innerRingCoreColor.y, innerRingCoreColor.z, innerRingCoreColor.w );
		DrawWeaponWheelArc(
			center,
			popoutOffset,
			innerRingRadius,
			innerRingCoreHalfWidth,
			startAngle,
			endAngle,
			aspectScale,
			whiteMaterial );

		renderSystem->SetColor4( structuralLineColor.x, structuralLineColor.y, structuralLineColor.z, structuralLineColor.w );
		DrawWeaponWheelFadedLine(
			center,
			popoutOffset + startDir * ( innerRadius + segmentRadialInset ),
			popoutOffset + startDir * ( outerRadius - segmentOuterGap ),
			lineThickness * 0.5f,
			aspectScale,
			whiteMaterial,
			structuralLineColor );
		DrawWeaponWheelFadedLine(
			center,
			popoutOffset + endDir * ( innerRadius + segmentRadialInset ),
			popoutOffset + endDir * ( outerRadius - segmentOuterGap ),
			lineThickness * 0.5f,
			aspectScale,
			whiteMaterial,
			structuralLineColor );

		if ( !hasWeapon ) {
			continue;
		}

		renderSystem->SetColor4( accentColor.x, accentColor.y, accentColor.z, accentColor.w );
		DrawWeaponWheelArc(
			center,
			popoutOffset,
			segmentDetailRadius,
			segmentDetailHalfWidth,
			centerAngle - usableHalfAngle * WEAPON_WHEEL_SEGMENT_DETAIL_SWEEP,
			centerAngle + usableHalfAngle * WEAPON_WHEEL_SEGMENT_DETAIL_SWEEP,
			aspectScale,
			whiteMaterial );

		const idDeclEntityDef* weaponDecl = GetWeaponDef( weaponIndex );
		if ( weaponDecl == NULL ) {
			continue;
		}

		const char* iconName = weaponDecl->dict.GetString( "inv_icon" );
		if ( iconName != NULL && iconName[ 0 ] != '\0' ) {
			const idMaterial* iconMaterial = declManager->FindMaterial( iconName, false );
			if ( iconMaterial != NULL ) {
				const idVec2 iconCenterLocal = popoutOffset + slotDir * segmentIconRadius;
				const float iconSize = WeaponWheelFitSquareInSegment(
					center,
					iconCenterLocal,
					segmentIconSize,
					segmentIconMinSize,
					aspectScale,
					popoutOffset,
					centerAngle,
					usableHalfAngle,
					contentInnerRadius,
					contentOuterRadius,
					segmentContentMargin );
				idVec4 iconColor( 1.0f, 1.0f, 1.0f, blend );
				if ( hoveredActive ) {
					iconColor = idVec4( selectedColor.x, selectedColor.y, selectedColor.z, blend );
				} else if ( lowAmmo ) {
					iconColor.Set( 1.0f, 0.92f, 0.84f, blend );
				}

				if ( iconSize > 0.0f ) {
					const idVec2 iconPos = WeaponWheelProjectPoint( center, iconCenterLocal, aspectScale );
					renderSystem->SetColor4( iconColor.x, iconColor.y, iconColor.z, iconColor.w );
					renderSystem->DrawStretchPic(
						iconPos.x - iconSize * aspectScale * 0.5f,
						iconPos.y - iconSize * 0.5f,
						iconSize * aspectScale,
						iconSize,
						0.0f,
						0.0f,
						1.0f,
						1.0f,
						iconMaterial );
				}
			}
		}
	}

	{
		const idVec2 cursorPos = WeaponWheelProjectPoint( center, weaponWheelCursor * transitionScale, aspectScale );
		const float halfScreenSizeX = cursorSize * aspectScale * 0.5f;
		const float halfScreenSizeY = cursorSize * 0.5f;
		const float thicknessX = cursorThickness * aspectScale;
		const float thicknessY = cursorThickness;
		renderSystem->SetColor4( 1.0f, 1.0f, 1.0f, lineAlpha * 0.92f );
		renderSystem->DrawStretchPic( cursorPos.x - halfScreenSizeX, cursorPos.y - thicknessY * 0.5f, cursorSize * aspectScale, thicknessY, 0.0f, 0.0f, 1.0f, 1.0f, whiteMaterial );
		renderSystem->DrawStretchPic( cursorPos.x - thicknessX * 0.5f, cursorPos.y - halfScreenSizeY, thicknessX, cursorSize, 0.0f, 0.0f, 1.0f, 1.0f, whiteMaterial );
	}

	if ( weaponWheelHoveredSlot >= 0 && weaponWheelHoveredSlot < WEAPON_WHEEL_SLOT_COUNT ) {
		const int weaponIndex = GetWeaponWheelWeaponForSlot( weaponWheelHoveredSlot );
		const bool hasWeapon = weaponIndex >= 0 && ( inventory.weapons & ( 1 << weaponIndex ) ) != 0;
		if ( hasWeapon ) {
			const float angle = weaponWheelHoveredSlot * slice;
			const idVec2 dir = WeaponWheelDirection( angle );
			DrawWeaponWheelArrow(
				center,
				dir,
				arrowOffset,
				arrowLength,
				arrowHalfWidth,
				aspectScale,
				whiteMaterial,
				WeaponWheelMPNoticeColor( lineAlpha ) );
		}
	}

	{
		const int weaponIndex = GetWeaponWheelWeaponForSlot( weaponWheelHoveredSlot );
		const bool hasWeapon = weaponIndex >= 0 && ( inventory.weapons & ( 1 << weaponIndex ) ) != 0;
		const idDeclEntityDef* weaponDecl = ( hasWeapon && weaponIndex >= 0 ) ? GetWeaponDef( weaponIndex ) : NULL;
		if ( weaponDecl != NULL && fontMaterial != NULL ) {
			idStr weaponName = common->GetLocalizedString( weaponDecl->dict.GetString( "inv_name" ) );
			const int ammoCount = GetWeaponWheelAmmoCount( weaponIndex );
			const bool lowAmmo = IsWeaponWheelSlotLowAmmo( weaponWheelHoveredSlot );
			const char* ammoString = ( ammoCount >= 0 ) ? va( "%d", ammoCount ) : "";
			idStrList wrappedLines;
			float baseTextScale = 0.0f;
			float baseAmmoScale = 0.0f;
			if ( WeaponWheelFitCenterStack( weaponName.c_str(), ammoString, aspectScale, wrappedLines, baseTextScale, baseAmmoScale ) ) {
				const float textScale = baseTextScale * transitionScale;
				const float ammoScale = baseAmmoScale * transitionScale;
				const float lineHeight = BIGCHAR_HEIGHT * textScale;
				const bool showAmmo = ( ammoString[ 0 ] != '\0' && ammoScale > 0.0f );
				const float ammoHeight = BIGCHAR_HEIGHT * ammoScale;
				const float totalHeight = wrappedLines.Num() * lineHeight + Max( 0, wrappedLines.Num() - 1 ) * centerTextGap + ( showAmmo ? centerStackGap + ammoHeight : 0.0f );
				float drawY = center.y - totalHeight * 0.5f;

				for ( int i = 0; i < wrappedLines.Num(); ++i ) {
					const float lineWidth = WeaponWheelStringWidth( wrappedLines[ i ].c_str(), aspectScale, textScale );
					DrawWeaponWheelString(
						center.x - lineWidth * 0.5f,
						drawY,
						wrappedLines[ i ].c_str(),
						idVec4( 1.0f, 1.0f, 1.0f, blend ),
						aspectScale,
						textScale,
						fontMaterial );
					drawY += lineHeight + centerTextGap;
				}

				if ( showAmmo ) {
					const idVec4 ammoColor = lowAmmo ? idVec4( 1.0f, 0.80f, 0.46f, blend ) : WeaponWheelSelectedColor( blend );
					const float ammoWidth = WeaponWheelStringWidth( ammoString, aspectScale, ammoScale );
					drawY += centerStackGap;
					DrawWeaponWheelString(
						center.x - ammoWidth * 0.5f,
						drawY,
						ammoString,
						ammoColor,
						aspectScale,
						ammoScale,
						fontMaterial );
				}
			}
		}
	}

	renderSystem->SetColor4( 1.0f, 1.0f, 1.0f, 1.0f );
}

