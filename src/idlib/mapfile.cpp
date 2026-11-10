



#ifdef _XENON
#define PACIFIER_UPDATE session->PacifierUpdate()
#else
#define PACIFIER_UPDATE
#endif


/*
===============
FloatCRC
===============
*/
ID_INLINE unsigned int FloatCRC( float f ) {
	return idMath_FloatBits( f );
}

/*
===============
StringCRC
===============
*/
ID_INLINE unsigned int StringCRC( const char *str ) {
	unsigned int i, crc;
	const unsigned char *ptr;

	crc = 0;
	ptr = reinterpret_cast<const unsigned char*>(str);
	for ( i = 0; str[i]; i++ ) {
		crc ^= str[i] << (i & 3);
	}
	return crc;
}

/*
=================
ComputeAxisBase

WARNING : special case behaviour of atan2(y,x) <-> atan(y/x) might not be the same everywhere when x == 0
rotation by (0,RotY,RotZ) assigns X to normal
=================
*/
static void ComputeAxisBase( const idVec3 &normal, idVec3 &texS, idVec3 &texT ) {
	float RotY, RotZ;
	idVec3 n;

	// do some cleaning
	n[0] = ( idMath::Fabs( normal[0] ) < 1e-6f ) ? 0.0f : normal[0];
	n[1] = ( idMath::Fabs( normal[1] ) < 1e-6f ) ? 0.0f : normal[1];
	n[2] = ( idMath::Fabs( normal[2] ) < 1e-6f ) ? 0.0f : normal[2];

	RotY = -idMath::ATan( n[2], idMath::Sqrt( n[1] * n[1] + n[0] * n[0]) );
	RotZ = idMath::ATan( n[1], n[0] );
	// rotate (0,1,0) and (0,0,1) to compute texS and texT
// RAVEN BEGIN
// jscott: routed thru idMath
	texS[0] = -idMath::Sin( RotZ );
	texS[1] = idMath::Cos( RotZ );
	texS[2] = 0;
	// the texT vector is along -Z ( T texture coorinates axis )
	texT[0] = -idMath::Sin( RotY ) * idMath::Cos( RotZ );
	texT[1] = -idMath::Sin( RotY ) * idMath::Sin( RotZ );
	texT[2] = -idMath::Cos( RotY );
// RAVEN END
}

// RAVEN BEGIN
// rhummer: Trying to debug why func_groups disappear when editing a map when they shouldn't.
idMapFile::~idMapFile( void ) 
{ 
	entities.DeleteContents( true ); 
	if ( cvarSystem->GetCVarBool( "developer" ) ) {
		common->Printf( "^2done with .map file ^0%s\n", name.c_str() );
	}
}
// RAVEN END

/*
=================
idMapBrushSide::GetTextureVectors
=================
*/
void idMapBrushSide::GetTextureVectors( idVec4 v[2] ) const {
	int i;
	idVec3 texX, texY;

	ComputeAxisBase( plane.Normal(), texX, texY );
	for ( i = 0; i < 2; i++ ) {
		v[i][0] = texX[0] * texMat[i][0] + texY[0] * texMat[i][1];
		v[i][1] = texX[1] * texMat[i][0] + texY[1] * texMat[i][1];
		v[i][2] = texX[2] * texMat[i][0] + texY[2] * texMat[i][1];
		v[i][3] = texMat[i][2] + ( origin * v[i].ToVec3() );
	}
}

/*
=================
idMapPatch::Parse
=================
*/
// RAVEN BEGIN
// jsinger: changed to be Lexer instead of idLexer so that we have the ability to read binary files
idMapPatch *idMapPatch::Parse( Lexer &src, const idVec3 &origin, bool patchDef3, int version ) {
// RAVEN END
	
	TIME_THIS_SCOPE( __FUNCLINE__);	
	
	float		info[7];
	idDrawVert *vert;
	idToken		token;
	int			i, j;

	if ( !src.ExpectTokenString( "{" ) ) {
		return NULL;
	}

	// read the material (we had an implicit 'textures/' in the old format...)
	if ( !src.ReadToken( &token ) ) {
		src.Error( "idMapPatch::Parse: unexpected EOF" );
		return NULL;
	}

	// Parse it
	if (patchDef3) {
		if ( !src.Parse1DMatrix( 7, info ) ) {
			src.Error( "idMapPatch::Parse: unable to Parse patchDef3 info" );
			return NULL;
		}
	} else {
		if ( !src.Parse1DMatrix( 5, info ) ) {
			src.Error( "idMapPatch::Parse: unable to parse patchDef2 info" );
			return NULL;
		}
	}

	// openQ4: the control points are stored as width * height vertices. The
	// product was taken in int from the file's numbers, so a patch declared
	// 65537 by 65536 got 65536 of them and its rows were written past the end.
	if ( !( info[0] >= 1.0f && info[0] <= 1024.0f && info[1] >= 1.0f && info[1] <= 1024.0f ) ) {
		src.Error( "idMapPatch::Parse: bad size" );
		return NULL;
	}

	idMapPatch *patch = new idMapPatch( info[0], info[1] );
	patch->SetSize( info[0], info[1] );
	if ( version < 2 ) {
		patch->SetMaterial( "textures/" + token );
	} else {
		patch->SetMaterial( token );
	}

	if ( patchDef3 ) {
		patch->SetHorzSubdivisions( info[2] );
		patch->SetVertSubdivisions( info[3] );
		patch->SetExplicitlySubdivided( true );
	}

	if ( patch->GetWidth() < 0 || patch->GetHeight() < 0 ) {
		src.Error( "idMapPatch::Parse: bad size" );
		delete patch;
		return NULL;
	}

	// these were written out in the wrong order, IMHO
	if ( !src.ExpectTokenString( "(" ) ) {
		src.Error( "idMapPatch::Parse: bad patch vertex data" );
		delete patch;
		return NULL;
	}
	for ( j = 0; j < patch->GetWidth(); j++ ) {
		if ( !src.ExpectTokenString( "(" ) ) {
			src.Error( "idMapPatch::Parse: bad vertex row data" );
			delete patch;
			return NULL;
		}
		for ( i = 0; i < patch->GetHeight(); i++ ) {
			float v[5];

			if ( !src.Parse1DMatrix( 5, v ) ) {
				src.Error( "idMapPatch::Parse: bad vertex column data" );
				delete patch;
				return NULL;
			}

			vert = &((*patch)[i * patch->GetWidth() + j]);
			vert->xyz[0] = v[0] - origin[0];
			vert->xyz[1] = v[1] - origin[1];
			vert->xyz[2] = v[2] - origin[2];
			vert->st[0] = v[3];
			vert->st[1] = v[4];
		}
		if ( !src.ExpectTokenString( ")" ) ) {
			delete patch;
			src.Error( "idMapPatch::Parse: unable to parse patch control points" );
			return NULL;
		}
	}
	if ( !src.ExpectTokenString( ")" ) ||
			!src.ExpectTokenString( "}" ) ||
				!src.ExpectTokenString( "}" ) ) {
		src.Error( "idMapPatch::Parse: unable to parse patch control points, no closure" );
		delete patch;
		return NULL;
	}

	return patch;
}

/*
============
idMapPatch::Write
============
*/
bool idMapPatch::Write( idFile *fp, int primitiveNum, const idVec3 &origin ) const {
	int i, j;
	const idDrawVert *v;

	if ( GetExplicitlySubdivided() ) {
		fp->WriteFloatString( "// primitive %d\n{\n patchDef3\n {\n", primitiveNum );
		fp->WriteFloatString( "  \"%s\"\n  ( %d %d %d %d 0 0 0 )\n", GetMaterial(), GetWidth(), GetHeight(), GetHorzSubdivisions(), GetVertSubdivisions());
	} else {
		fp->WriteFloatString( "// primitive %d\n{\n patchDef2\n {\n", primitiveNum );
		fp->WriteFloatString( "  \"%s\"\n  ( %d %d 0 0 0 )\n", GetMaterial(), GetWidth(), GetHeight());
	}

	fp->WriteFloatString( "  (\n" );
	for ( i = 0; i < GetWidth(); i++ ) {
		fp->WriteFloatString( "   ( " );
		for ( j = 0; j < GetHeight(); j++ ) {
			v = &verts[ j * GetWidth() + i ];
			fp->WriteFloatString( " ( %f %f %f %f %f )", v->xyz[0] + origin[0],
								v->xyz[1] + origin[1], v->xyz[2] + origin[2], v->st[0], v->st[1] );
		}
		fp->WriteFloatString( " )\n" );
	}
	fp->WriteFloatString( "  )\n }\n}\n" );

	return true;
}

// RAVEN BEGIN
// rjohnson: added resolve for handling func_groups and other aspects.  Before, radiant would do this processing on a map destroying the original data
/*
===============
idMapPatch::AdjustOrigin
===============
*/
void idMapPatch::AdjustOrigin( idVec3 &delta ) {
	TranslateSelf( delta );
}
// RAVEN END

/*
===============
idMapPatch::GetGeometryCRC
===============
*/
unsigned int idMapPatch::GetGeometryCRC( void ) const {
	int i, j;
	unsigned int crc;

	crc = GetHorzSubdivisions() ^ GetVertSubdivisions();
	for ( i = 0; i < GetWidth(); i++ ) {
		for ( j = 0; j < GetHeight(); j++ ) {
			crc ^= FloatCRC( verts[j * GetWidth() + i].xyz.x );
			crc ^= FloatCRC( verts[j * GetWidth() + i].xyz.y );
			crc ^= FloatCRC( verts[j * GetWidth() + i].xyz.z );
		}
	}

	crc ^= StringCRC( GetMaterial() );

	return crc;
}

/*
=================
idMapBrush::Parse
=================
*/
// RAVEN BEGIN
// jsinger: changed to be Lexer instead of idLexer so that we have the ability to read binary files
idMapBrush *idMapBrush::Parse( Lexer &src, const idVec3 &origin, bool newFormat, int version ) {
// RAVEN END
	
	TIME_THIS_SCOPE( __FUNCLINE__);	
	
	int i;
	idVec3 planepts[3];
	idToken token;
	idList<idMapBrushSide*> sides;
	idMapBrushSide	*side;
	idDict epairs;

	if ( !src.ExpectTokenString( "{" ) ) {
		return NULL;
	}

	do {
		if ( !src.ReadToken( &token ) ) {
			src.Error( "idMapBrush::Parse: unexpected EOF" );
			sides.DeleteContents( true );
			return NULL;
		}
		if ( token == "}" ) {
			break;
		}

		// here we may have to jump over brush epairs ( only used in editor )
		do {
			// if token is a brace
			if ( token == "(" ) {
				break;
			}
			// the token should be a key string for a key/value pair
			if ( token.type != TT_STRING ) {
				src.Error( "idMapBrush::Parse: unexpected %s, expected ( or epair key string", token.c_str() );
				sides.DeleteContents( true );
				return NULL;
			}

			idStr key = token;

			if ( !src.ReadTokenOnLine( &token ) || token.type != TT_STRING ) {
				src.Error( "idMapBrush::Parse: expected epair value string not found" );
				sides.DeleteContents( true );
				return NULL;
			}

			epairs.Set( key, token );

			// try to read the next key
			if ( !src.ReadToken( &token ) ) {
				src.Error( "idMapBrush::Parse: unexpected EOF" );
				sides.DeleteContents( true );
				return NULL;
			}
		} while (1);

		src.UnreadToken( &token );

		side = new idMapBrushSide();
		sides.Append(side);

		if ( newFormat ) {
			if ( !src.Parse1DMatrix( 4, side->plane.ToFloatPtr() ) ) {
				src.Error( "idMapBrush::Parse: unable to read brush side plane definition" );
				sides.DeleteContents( true );
				return NULL;
			}
		} else {
			// read the three point plane definition
			if (!src.Parse1DMatrix( 3, planepts[0].ToFloatPtr() ) ||
				!src.Parse1DMatrix( 3, planepts[1].ToFloatPtr() ) ||
				!src.Parse1DMatrix( 3, planepts[2].ToFloatPtr() ) ) {
				src.Error( "idMapBrush::Parse: unable to read brush side plane definition" );
				sides.DeleteContents( true );
				return NULL;
			}

			planepts[0] -= origin;
			planepts[1] -= origin;
			planepts[2] -= origin;

			side->plane.FromPoints( planepts[0], planepts[1], planepts[2] );
		}

		// read the texture matrix
		// this is odd, because the texmat is 2D relative to default planar texture axis
		if ( !src.Parse2DMatrix( 2, 3, side->texMat[0].ToFloatPtr() ) ) {
			src.Error( "idMapBrush::Parse: unable to read brush side texture matrix" );
			sides.DeleteContents( true );
			return NULL;
		}
		side->origin = origin;
		
		// read the material
		if ( !src.ReadTokenOnLine( &token ) ) {
			src.Error( "idMapBrush::Parse: unable to read brush side material" );
			sides.DeleteContents( true );
			return NULL;
		}

		// we had an implicit 'textures/' in the old format...
		if ( version < 2 ) {
			side->material = "textures/" + token;
		} else {
			side->material = token;
		}

// RAVEN BEGIN
// jscott: make sure the material is properly parsed
		declManager->FindMaterial( token );
// RAVEN END

// RAVEN BEGIN
// jscott: removed these in later versions
		if( version < 3 ) {
// RAVEN END
			// Q2 allowed override of default flags and values, but we don't any more
			if ( src.ReadTokenOnLine( &token ) ) {
				if ( src.ReadTokenOnLine( &token ) ) {
					if ( src.ReadTokenOnLine( &token ) ) {
					}
				}
			}
		}
	} while( 1 );

	if ( !src.ExpectTokenString( "}" ) ) {
		sides.DeleteContents( true );
		return NULL;
	}

	idMapBrush *brush = new idMapBrush();
	for ( i = 0; i < sides.Num(); i++ ) {
		brush->AddSide( sides[i] );
	}

	brush->epairs = epairs;

	return brush;
}

/*
=================
idMapBrush::ParseQ3
=================
*/
// RAVEN BEGIN
// jsinger: changed to be Lexer instead of idLexer so that we have the ability to read binary files
idMapBrush *idMapBrush::ParseQ3( Lexer &src, const idVec3 &origin ) {
// RAVEN END
	int i, shift[2], rotate;
	float scale[2];
	idVec3 planepts[3];
	idToken token;
	idList<idMapBrushSide*> sides;
	idMapBrushSide	*side;
	idDict epairs;

	do {
		if ( src.CheckTokenString( "}" ) ) {
			break;
		}

		side = new idMapBrushSide();
		sides.Append( side );

		// read the three point plane definition
		if (!src.Parse1DMatrix( 3, planepts[0].ToFloatPtr() ) ||
			!src.Parse1DMatrix( 3, planepts[1].ToFloatPtr() ) ||
			!src.Parse1DMatrix( 3, planepts[2].ToFloatPtr() ) ) {
			src.Error( "idMapBrush::ParseQ3: unable to read brush side plane definition" );
			sides.DeleteContents( true );
			return NULL;
		}

		planepts[0] -= origin;
		planepts[1] -= origin;
		planepts[2] -= origin;

		side->plane.FromPoints( planepts[0], planepts[1], planepts[2] );

		// read the material
		if ( !src.ReadTokenOnLine( &token ) ) {
			src.Error( "idMapBrush::ParseQ3: unable to read brush side material" );
			sides.DeleteContents( true );
			return NULL;
		}

		// we have an implicit 'textures/' in the old format
		side->material = "textures/" + token;

		// read the texture shift, rotate and scale
		shift[0] = src.ParseInt();
		shift[1] = src.ParseInt();
		rotate = src.ParseInt();
		scale[0] = src.ParseFloat();
		scale[1] = src.ParseFloat();
		side->texMat[0] = idVec3( 0.03125f, 0.0f, 0.0f );
		side->texMat[1] = idVec3( 0.0f, 0.03125f, 0.0f );
		side->origin = origin;
		
		// Q2 allowed override of default flags and values, but we don't any more
		if ( src.ReadTokenOnLine( &token ) ) {
			if ( src.ReadTokenOnLine( &token ) ) {
				if ( src.ReadTokenOnLine( &token ) ) {
				}
			}
		}
	} while( 1 );

	idMapBrush *brush = new idMapBrush();
	for ( i = 0; i < sides.Num(); i++ ) {
		brush->AddSide( sides[i] );
	}

	brush->epairs = epairs;

	return brush;
}

/*
============
idMapBrush::Write
============
*/
bool idMapBrush::Write( idFile *fp, int primitiveNum, const idVec3 &origin ) const {
	int i;
	idMapBrushSide *side;

	fp->WriteFloatString( "// primitive %d\n{\n brushDef3\n {\n", primitiveNum );

	// write brush epairs
	for ( i = 0; i < epairs.GetNumKeyVals(); i++) {
		fp->WriteFloatString( "  \"%s\" \"%s\"\n", epairs.GetKeyVal(i)->GetKey().c_str(), epairs.GetKeyVal(i)->GetValue().c_str());
	}

	// write brush sides
	for ( i = 0; i < GetNumSides(); i++ ) {
		side = GetSide( i );
		fp->WriteFloatString( "  ( %f %f %f %f ) ", side->plane[0], side->plane[1], side->plane[2], side->plane[3] );
// RAVEN BEGIN
		fp->WriteFloatString( "( ( %f %f %f ) ( %f %f %f ) ) \"%s\"\n",
// RAVEN END
							side->texMat[0][0], side->texMat[0][1], side->texMat[0][2],
								side->texMat[1][0], side->texMat[1][1], side->texMat[1][2],
									side->material.c_str() );
	}

	fp->WriteFloatString( " }\n}\n" );

	return true;
}

/*
===============
idMapBrush::GetGeometryCRC
===============
*/
unsigned int idMapBrush::GetGeometryCRC( void ) const {
	int i, j;
	idMapBrushSide *mapSide;
	unsigned int crc;

	crc = 0;
	for ( i = 0; i < GetNumSides(); i++ ) {
		mapSide = GetSide(i);
		for ( j = 0; j < 4; j++ ) {
			crc ^= FloatCRC( mapSide->GetPlane()[j] );
		}
		crc ^= StringCRC( mapSide->GetMaterial() );
	}

	return crc;
}

// RAVEN BEGIN
// rjohnson: added resolve for handling func_groups and other aspects.  Before, radiant would do this processing on a map destroying the original data

/*
===============
idMapBrush::AdjustOrigin
===============
*/
void idMapBrush::AdjustOrigin( idVec3 &delta ) {
	int				i;
	idMapBrushSide	*mapSide;

	for ( i = 0; i < GetNumSides(); i++ ) {
		mapSide = GetSide(i);

		mapSide->SetPlane( mapSide->GetPlane().Translate( delta ) );

		// Keep the texture locked to the moved face, as
		// Face_MoveTexture_BrushPrimit() in tools/radiant/EditorBrushPrimit.cpp
		// does. That solves a 3x3 system by Cramer's rule, and for a pure
		// translation its exact solution is this closed form: the scale and
		// rotation terms are unchanged, and each offset loses the translation's
		// projection onto the face's texture axes.
		//
		// Raven's copy ran that solve in single precision. The x87 FPU hid it
		// behind extended-precision intermediates, but in SSE float arithmetic
		// the determinant's partial sums pass 2^24 once a func_group origin is
		// a few thousand units out. The solve then returned flipped, sheared
		// or unlocked matrices for most folded faces, so every dmap-compiled
		// map with func_groups came out with misaligned textures.
		idVec3	texS, texT;
		ComputeAxisBase( mapSide->GetPlane().Normal(), texS, texT );

		const double tx = (double)delta[0] * texS[0] + (double)delta[1] * texS[1] + (double)delta[2] * texS[2];
		const double ty = (double)delta[0] * texT[0] + (double)delta[1] * texT[1] + (double)delta[2] * texT[2];

		idVec3	tm[2];
		mapSide->GetTextureMatrix( tm[0], tm[1] );
		for ( int j = 0; j < 2; j++ ) {
			tm[j][2] = (float)( (double)tm[j][2] - (double)tm[j][0] * tx - (double)tm[j][1] * ty );
		}
		mapSide->SetTextureMatrix( tm );
	}
}
// RAVEN END

/*
================
idMapEntity::Parse
================
*/
// RAVEN BEGIN
// jsinger: changed to be Lexer instead of idLexer so that we have the ability to read binary files
idMapEntity *idMapEntity::Parse( Lexer &src, bool worldSpawn, int version ) {
// RAVEN END
	
	TIME_THIS_SCOPE( __FUNCLINE__);	
	
	idToken	token;
	idMapEntity *mapEnt;
	idMapPatch *mapPatch;
	idMapBrush *mapBrush;
	bool worldent;
	idVec3 origin;
	double v1, v2, v3;

	if ( !src.ReadToken(&token) ) {
		return NULL;
	}

	if ( token != "{" ) {
		src.Error( "idMapEntity::Parse: { not found, found %s", token.c_str() );
		return NULL;
	}

	mapEnt = new idMapEntity();

	if ( worldSpawn ) {
		mapEnt->primitives.Resize( 1024, 256 );
	}

	origin.Zero();
	worldent = false;
	do {
		if ( !src.ReadToken(&token) ) {
			src.Error( "idMapEntity::Parse: EOF without closing brace" );
			return NULL;
		}
		if ( token == "}" ) {
			break;
		}

		if ( token == "{" ) {
			// parse a brush or patch
			if ( !src.ReadToken( &token ) ) {
				src.Error( "idMapEntity::Parse: unexpected EOF" );
				return NULL;
			}

			if ( worldent ) {
				origin.Zero();
			}

			// if is it a brush: brush, brushDef, brushDef2, brushDef3
			if ( token.Icmpn( "brush", 5 ) == 0 ) {
				mapBrush = idMapBrush::Parse( src, origin, ( !token.Icmp( "brushDef2" ) || !token.Icmp( "brushDef3" ) ), version );
				if ( !mapBrush ) {
					return NULL;
				}
				mapEnt->AddPrimitive( mapBrush );
			}
			// if is it a patch: patchDef2, patchDef3
			else if ( token.Icmpn( "patch", 5 ) == 0 ) {
				mapPatch = idMapPatch::Parse( src, origin, !token.Icmp( "patchDef3" ), version );
				if ( !mapPatch ) {
					return NULL;
				}
				mapEnt->AddPrimitive( mapPatch );
			}
			// assume it's a brush in Q3 or older style
			else {
				src.UnreadToken( &token );
				mapBrush = idMapBrush::ParseQ3( src, origin );
				if ( !mapBrush ) {
					return NULL;
				}
				mapEnt->AddPrimitive( mapBrush );
			}
		} else {
			idStr key, value;

			// parse a key / value pair
			key = token;
			src.ReadTokenOnLine( &token );
			value = token;

			// strip trailing spaces that sometimes get accidentally
			// added in the editor
			value.StripTrailingWhitespace();
			key.StripTrailingWhitespace();

			mapEnt->epairs.Set( key, value );

			if ( !idStr::Icmp( key, "origin" ) ) {
				// scanf into doubles, then assign, so it is idVec size independent
				v1 = v2 = v3 = 0;
				sscanf( value, "%lf %lf %lf", &v1, &v2, &v3 );
				origin.x = v1;
				origin.y = v2;
				origin.z = v3;
			}
			else if ( !idStr::Icmp( key, "classname" ) && !idStr::Icmp( value, "worldspawn" ) ) {
				worldent = true;
			}
		}
	} while( 1 );

	return mapEnt;
}

/*
============
idMapEntity::Write
============
*/
bool idMapEntity::Write( idFile *fp, int entityNum ) const {
	int i;
	idMapPrimitive *mapPrim;
	idVec3 origin;

	fp->WriteFloatString( "// entity %d\n{\n", entityNum );

	// write entity epairs
	for ( i = 0; i < epairs.GetNumKeyVals(); i++) {
		fp->WriteFloatString( "\"%s\" \"%s\"\n", epairs.GetKeyVal(i)->GetKey().c_str(), epairs.GetKeyVal(i)->GetValue().c_str());
	}

	epairs.GetVector( "origin", "0 0 0", origin );

	// write pritimives
	for ( i = 0; i < GetNumPrimitives(); i++ ) {
		mapPrim = GetPrimitive( i );

		switch( mapPrim->GetType() ) {
			case idMapPrimitive::TYPE_BRUSH:
				static_cast<idMapBrush*>(mapPrim)->Write( fp, i, origin );
				break;
			case idMapPrimitive::TYPE_PATCH:
				static_cast<idMapPatch*>(mapPrim)->Write( fp, i, origin );
				break;
		}
	}

	fp->WriteFloatString( "}\n" );

	return true;
}

/*
===============
idMapEntity::RemovePrimitiveData
===============
*/
void idMapEntity::RemovePrimitiveData() {
	primitives.DeleteContents(true);
}

/*
===============
idMapEntity::GetGeometryCRC
===============
*/
unsigned int idMapEntity::GetGeometryCRC( void ) const {
	int i;
	unsigned int crc;
	idMapPrimitive	*mapPrim;

	crc = 0;
	for ( i = 0; i < GetNumPrimitives(); i++ ) {
		mapPrim = GetPrimitive( i );

		switch( mapPrim->GetType() ) {
			case idMapPrimitive::TYPE_BRUSH:
				crc ^= static_cast<idMapBrush*>(mapPrim)->GetGeometryCRC();
				if ( epairs.GetString( "model" ) ) {
					crc ^= StringCRC( epairs.GetString( "model" ) );
				}
				break;
			case idMapPrimitive::TYPE_PATCH:
				crc ^= static_cast<idMapPatch*>(mapPrim)->GetGeometryCRC();
				if ( epairs.GetString( "model" ) ) {
					crc ^= StringCRC( epairs.GetString( "model" ) );
				}
				break;
		}
	}

	return crc;
}

/*
===============
idMapFile::ParseEntityStringFile

Parses one text-only entity-string companion into caller-owned temporary
storage. Parsing is bounded and atomic; this function never mutates the map's
active entity list.
===============
*/
bool idMapFile::ParseEntityStringFile( const char *extension, bool replacement, bool osPath,
	idList<idMapEntity *> &parsedEntities, unsigned int &timestamp, bool &found ) {
	static const int MAX_ENTITY_STRING_FILE_BYTES = 16 * 1024 * 1024;
	static const int MAX_ENTITY_STRING_FILE_ENTITIES = 4096;
	static const int MAX_ENTITY_STRING_FILE_KEYS = 4096;

	idStr extensionName = name;
	extensionName.SetFileExtension( extension );

	found = false;
	timestamp = 0;

	idFile *extensionFile = osPath
		? idLib::fileSystem->OpenExplicitFileRead( extensionName )
		: idLib::fileSystem->OpenFileRead( extensionName );
	if ( extensionFile == NULL ) {
		return true;
	}
	found = true;

	const int extensionLength = extensionFile->Length();
	const unsigned int extensionTime = static_cast<unsigned int>( extensionFile->Timestamp() );
	if ( extensionLength < 0 || extensionLength > MAX_ENTITY_STRING_FILE_BYTES ) {
		idLib::fileSystem->CloseFile( extensionFile );
		common->Warning( "Entity-string file '%s' is %d bytes; the supported limit is %d bytes",
			extensionName.c_str(), extensionLength, MAX_ENTITY_STRING_FILE_BYTES );
		return false;
	}

	char *extensionText = static_cast<char *>( Mem_Alloc( extensionLength + 1 ) );
	const int bytesRead = extensionFile->Read( extensionText, extensionLength );
	idLib::fileSystem->CloseFile( extensionFile );
	extensionText[ extensionLength ] = '\0';
	if ( bytesRead != extensionLength ) {
		common->Warning( "Could not read complete entity-string file '%s' (%d of %d bytes)",
			extensionName.c_str(), bytesRead, extensionLength );
		Mem_Free( extensionText );
		return false;
	}

	for ( int i = 0; i < extensionLength; i++ ) {
		if ( extensionText[ i ] == '\0' ) {
			common->Warning( "Entity-string file '%s' contains an embedded NUL byte at offset %d",
				extensionName.c_str(), i );
			Mem_Free( extensionText );
			return false;
		}
	}

	const char *parseText = extensionText;
	int parseLength = extensionLength;
	if ( parseLength >= 3 &&
		static_cast<unsigned char>( parseText[ 0 ] ) == 0xEF &&
		static_cast<unsigned char>( parseText[ 1 ] ) == 0xBB &&
		static_cast<unsigned char>( parseText[ 2 ] ) == 0xBF ) {
		parseText += 3;
		parseLength -= 3;
	}

	idHashIndex entityNameHash( 1024, 64 );

	bool valid = true;
	bool firstTopLevelToken = true;
	int errorLine = 1;
	idStr parseError;
	{
		idLexer src( parseText, parseLength, extensionName,
			LEXFL_NOSTRINGCONCAT | LEXFL_NOSTRINGESCAPECHARS |
			LEXFL_ALLOWPATHNAMES | LEXFL_NOFATALERRORS );
		idToken token;

		while ( valid && src.ReadToken( &token ) ) {
			if ( firstTopLevelToken && token.Icmp( "Version" ) == 0 ) {
				idToken versionToken;
				if ( !src.ReadToken( &versionToken ) || versionToken != "1" ) {
					valid = false;
					errorLine = src.GetLineNum();
					parseError = "the optional header must be 'Version 1'";
				}
				firstTopLevelToken = false;
				continue;
			}
			firstTopLevelToken = false;

			if ( token != "{" ) {
				valid = false;
				errorLine = src.GetLineNum();
				parseError = va( "expected '{', found '%s'", token.c_str() );
				break;
			}
			if ( parsedEntities.Num() >= MAX_ENTITY_STRING_FILE_ENTITIES ) {
				valid = false;
				errorLine = src.GetLineNum();
				parseError = va( "contains more than %d entities", MAX_ENTITY_STRING_FILE_ENTITIES );
				break;
			}

			idMapEntity *mapEnt = new idMapEntity;
			bool closed = false;
			while ( valid && src.ReadToken( &token ) ) {
				if ( token == "}" ) {
					closed = true;
					break;
				}
				if ( token == "{" ) {
					valid = false;
					errorLine = src.GetLineNum();
					parseError = "brushes and patches are not supported in entity-string files";
					break;
				}

				idStr key = token;
				idToken valueToken;
				if ( !src.ReadToken( &valueToken ) || valueToken == "{" || valueToken == "}" ) {
					valid = false;
					errorLine = src.GetLineNum();
					parseError = va( "missing value for key '%s'", key.c_str() );
					break;
				}

				idStr value = valueToken;
				key.StripTrailingWhitespace();
				value.StripTrailingWhitespace();
				if ( key.IsEmpty() ) {
					valid = false;
					errorLine = src.GetLineNum();
					parseError = "entity keys may not be empty";
					break;
				}
				if ( mapEnt->epairs.FindKey( key ) != NULL ) {
					valid = false;
					errorLine = src.GetLineNum();
					parseError = va( "duplicate key '%s'", key.c_str() );
					break;
				}
				mapEnt->epairs.Set( key, value );
				if ( mapEnt->epairs.GetNumKeyVals() > MAX_ENTITY_STRING_FILE_KEYS ) {
					valid = false;
					errorLine = src.GetLineNum();
					parseError = va( "an entity contains more than %d keys", MAX_ENTITY_STRING_FILE_KEYS );
					break;
				}
			}

			if ( valid && !closed ) {
				valid = false;
				errorLine = src.GetLineNum();
				parseError = "unexpected end of file before the entity's closing '}'";
			}
			if ( valid && src.HadError() ) {
				valid = false;
				errorLine = src.GetLineNum();
				parseError = "lexer error";
			}

			const char *className = mapEnt->epairs.GetString( "classname" );
			if ( valid && className[ 0 ] == '\0' ) {
				valid = false;
				errorLine = src.GetLineNum();
				parseError = "every entity requires a non-empty 'classname'";
			}
			if ( valid && replacement && parsedEntities.Num() == 0 && idStr::Icmp( className, "worldspawn" ) != 0 ) {
				valid = false;
				errorLine = src.GetLineNum();
				parseError = "a .ent replacement must begin with worldspawn";
			}
			if ( valid && idStr::Icmp( className, "worldspawn" ) == 0 && ( !replacement || parsedEntities.Num() != 0 ) ) {
				valid = false;
				errorLine = src.GetLineNum();
				parseError = replacement
					? "a .ent replacement may contain only one worldspawn, as its first entity"
					: "worldspawn cannot be added by an .entx extender";
			}
			if ( valid && mapEnt->epairs.FindKey( "spawn_entnum" ) != NULL ) {
				valid = false;
				errorLine = src.GetLineNum();
				parseError = "'spawn_entnum' is engine-owned and cannot be set by an entity-string file";
			}

			const char *entityName = mapEnt->epairs.GetString( "name" );
			if ( valid && entityName[ 0 ] != '\0' ) {
				const int hashKey = entityNameHash.GenerateKey( entityName, false );
				for ( int index = entityNameHash.First( hashKey ); index != -1; index = entityNameHash.Next( index ) ) {
					idMapEntity *other = parsedEntities[ index ];
					if ( idStr::Icmp( entityName, other->epairs.GetString( "name" ) ) == 0 ) {
						valid = false;
						errorLine = src.GetLineNum();
						parseError = va( "duplicate entity name '%s'", entityName );
						break;
					}
				}
				if ( valid ) {
					entityNameHash.Add( hashKey, parsedEntities.Num() );
				}
			}

			if ( !valid ) {
				delete mapEnt;
				break;
			}
			mapEnt->fromEntityStringFile = true;
			parsedEntities.Append( mapEnt );
		}

		if ( valid && src.HadError() ) {
			valid = false;
			errorLine = src.GetLineNum();
			parseError = "lexer error";
		}
	}
	if ( valid && replacement && parsedEntities.Num() == 0 ) {
		valid = false;
		parseError = "a .ent replacement must contain a worldspawn entity";
	}

	Mem_Free( extensionText );
	if ( !valid ) {
		common->Warning( "Invalid entity-string file '%s' at line %d: %s; no entity-string changes were applied",
			extensionName.c_str(), errorLine, parseError.c_str() );
		parsedEntities.DeleteContents( true );
		return false;
	}

	timestamp = extensionTime;
	return true;
}

/*
===============
idMapFile::ApplyEntityStringFiles

At runtime, <map>.ent replaces the complete entity string and <map>.entx then
appends additional point entities. The game calls this after collision loading,
so entity replacement cannot alter the source map's compiled geometry.
===============
*/
bool idMapFile::ApplyEntityStringFiles( void ) {
	if ( entityStringFilesEnabled ) {
		return true;
	}

	idList<idMapEntity *> replacementEntities;
	idList<idMapEntity *> extenderEntities;
	unsigned int replacementTime = 0;
	unsigned int extenderTime = 0;
	bool replacementFound = false;
	bool extenderFound = false;

	if ( !ParseEntityStringFile( "ent", true, parsedFromOSPath, replacementEntities, replacementTime, replacementFound ) ||
		!ParseEntityStringFile( "entx", false, parsedFromOSPath, extenderEntities, extenderTime, extenderFound ) ) {
		replacementEntities.DeleteContents( true );
		extenderEntities.DeleteContents( true );
		return false;
	}

	const idList<idMapEntity *> &activeEntities = replacementFound ? replacementEntities : entities;
	idHashIndex entityNameHash( 1024, activeEntities.Num() + extenderEntities.Num() + 64 );
	for ( int i = 0; i < activeEntities.Num(); i++ ) {
		const char *entityName = activeEntities[ i ]->epairs.GetString( "name" );
		if ( entityName[ 0 ] != '\0' ) {
			entityNameHash.Add( entityNameHash.GenerateKey( entityName, false ), i );
		}
	}
	for ( int i = 0; i < extenderEntities.Num(); i++ ) {
		const char *entityName = extenderEntities[ i ]->epairs.GetString( "name" );
		if ( entityName[ 0 ] == '\0' ) {
			continue;
		}
		const int hashKey = entityNameHash.GenerateKey( entityName, false );
		for ( int index = entityNameHash.First( hashKey ); index != -1; index = entityNameHash.Next( index ) ) {
			idMapEntity *other = index < activeEntities.Num()
				? activeEntities[ index ]
				: extenderEntities[ index - activeEntities.Num() ];
			if ( idStr::Icmp( entityName, other->epairs.GetString( "name" ) ) == 0 ) {
				common->Warning( "Invalid entity-string extender '%s.entx': duplicate entity name '%s'; no entity-string changes were applied",
					name.c_str(), entityName );
				replacementEntities.DeleteContents( true );
				extenderEntities.DeleteContents( true );
				return false;
			}
		}
		entityNameHash.Add( hashKey, activeEntities.Num() + i );
	}

	if ( replacementFound ) {
		entities.DeleteContents( true );
		for ( int i = 0; i < replacementEntities.Num(); i++ ) {
			entities.Append( replacementEntities[ i ] );
		}
		replacementEntities.Clear();
		mHasFuncGroups = false;
		common->Printf( "Entity string: replaced with %d entities from %s.ent\n", entities.Num(), name.c_str() );
	}

	const int numExtended = extenderEntities.Num();
	for ( int i = 0; i < numExtended; i++ ) {
		entities.Append( extenderEntities[ i ] );
	}
	extenderEntities.Clear();
	if ( extenderFound ) {
		common->Printf( "Entity string: appended %d entities from %s.entx\n", numExtended, name.c_str() );
	}

	replacementEntityFileLoaded = replacementFound;
	replacementEntityFileTime = replacementTime;
	extenderEntityFileLoaded = extenderFound;
	extenderEntityFileTime = extenderTime;
	entityStringFilesEnabled = true;
	if ( replacementFound || extenderFound ) {
		Resolve();
		if ( replacementFound ) {
			hasPrimitiveData = false;
		}
	}
	return true;
}

/*
===============
idMapFile::Parse
===============
*/
bool idMapFile::Parse( const char *filename, bool ignoreRegion, bool osPath ) {
	// no string concatenation for epairs and allow path names for materials
// RAVEN BEGIN
// jsinger: Done this way to reduce the amount of code change.  The auto pointer will
// delete the lexer when this method exits
	idAutoPtr<Lexer> lexer(LexerFactory::MakeLexer( LEXFL_NOSTRINGCONCAT | LEXFL_NOSTRINGESCAPECHARS | LEXFL_ALLOWPATHNAMES ));
	Lexer &src(*lexer);
// RAVEN END
	idToken token;
	idStr fullName;
	idMapEntity *mapEnt;
	
	TIME_THIS_SCOPE( __FUNCLINE__);	

	name = filename;
	name.StripFileExtension();
	loadedSourceFileName.Clear();
	parsedFromOSPath = osPath;
	entityStringFilesEnabled = false;
	replacementEntityFileLoaded = false;
	replacementEntityFileTime = 0;
	extenderEntityFileLoaded = false;
	extenderEntityFileTime = 0;
	fullName = name;
	hasPrimitiveData = false;

	if ( !ignoreRegion ) {
		// try loading a .reg file first
		fullName.SetFileExtension( "reg" );
		src.LoadFile( fullName, osPath );
	}

	if ( !src.IsLoaded() ) {

		PACIFIER_UPDATE;
		
		// now try a .map file
		fullName.SetFileExtension( "map" );
		src.LoadFile( fullName, osPath );

		if ( !src.IsLoaded() ) {
			// didn't get anything at all
			return false;
		}
	}

	version = OLD_MAP_VERSION;
	fileTime = src.GetFileTime();
	loadedSourceFileName = fullName;
	entities.DeleteContents( true );

	if ( src.CheckTokenString( "Version" ) ) {
		src.ReadTokenOnLine( &token );
		version = token.GetIntValue();
	}

	while( 1 ) {
		PACIFIER_UPDATE;
		mapEnt = idMapEntity::Parse( src, ( entities.Num() == 0 ), version );
		if ( !mapEnt ) {
			break;
		}
// RAVEN BEGIN
// rhummer: Check to see if there are func_groups in the map file.
		if ( !mHasFuncGroups && !idStr::Icmp( mapEnt->epairs.GetString( "classname" ), "func_group" ) ) {
			mHasFuncGroups = true;
		}
// RAVEN END
		entities.Append( mapEnt );
	}

	PACIFIER_UPDATE;
	ParseExport(filename, osPath);

	SetGeometryCRC();
// RAVEN BEGIN
// rhummer: Trying to debug why func_groups disappear when editing a map when they shouldn't.
	if ( cvarSystem->GetCVarBool( "developer" ) ) {
		common->Printf( "^2loaded .map file ^0%s\n", filename );
	}
	mHasBeenResolved = false;
// RAVEN END

	hasPrimitiveData = true;
	return true;
}

// RAVEN BEGIN
// rjohnson: added resolve for handling func_groups and other aspects.  Before, radiant would do this processing on a map destroying the original data
void idMapFile::Resolve( void )
{
	int i, j, k;
	idMapEntity *mapEnt;

	if ( !hasPrimitiveData ) {
		return;
	}

	// if the map has a worldspawn
	if ( entities.Num() ) {

		// "removeEntities" "classname" can be set in the worldspawn to remove all entities with the given classname
		const idKeyValue *removeEntities = entities[0]->epairs.MatchPrefix( "removeEntities", NULL );
		while ( removeEntities ) {
			RemoveEntities( removeEntities->GetValue() );
			removeEntities = entities[0]->epairs.MatchPrefix( "removeEntities", removeEntities );
		}

		// "overrideMaterial" "material" can be set in the worldspawn to reset all materials
		idStr material;
		if ( entities[0]->epairs.GetString( "overrideMaterial", "", material ) ) {
			for ( i = 0; i < entities.Num(); i++ ) {
				mapEnt = entities[i];
				for ( j = 0; j < mapEnt->GetNumPrimitives(); j++ ) {
					idMapPrimitive *mapPrimitive = mapEnt->GetPrimitive( j );
					switch( mapPrimitive->GetType() ) {
						case idMapPrimitive::TYPE_BRUSH: {
							idMapBrush *mapBrush = static_cast<idMapBrush *>(mapPrimitive);
							for ( k = 0; k < mapBrush->GetNumSides(); k++ ) {
								mapBrush->GetSide( k )->SetMaterial( material );
							}
							break;
						}
						case idMapPrimitive::TYPE_PATCH: {
							static_cast<idMapPatch *>(mapPrimitive)->SetMaterial( material );
							break;
						}
					}
				}
			}
		}

		// force all entities to have a name key/value pair
		if ( entities[0]->epairs.GetBool( "forceEntityNames" ) ) {
			for ( i = 1; i < entities.Num(); i++ ) {
				mapEnt = entities[i];
				if ( !mapEnt->epairs.FindKey( "name" ) ) {
					mapEnt->epairs.Set( "name", va( "%s%d", mapEnt->epairs.GetString( "classname", "forcedName" ), i ) );
				}
			}
		}

		// move the primitives of any func_group entities to the worldspawn
		for ( i = 1; i < entities.Num(); i++ ) {
			mapEnt = entities[i];
			if ( idStr::Icmp( mapEnt->epairs.GetString( "classname" ), "func_group" ) == 0 ) {
				idVec3	delta;
				
				mapEnt->epairs.GetVector( "origin", "0 0 0", delta );

				for( j = 0; j < mapEnt->primitives.Num(); j++ ) {
					idMapPrimitive	*mapPrim = mapEnt->primitives[j];

					mapPrim->AdjustOrigin( delta );
				}
				entities[0]->primitives.Append( mapEnt->primitives );
				mapEnt->primitives.Clear();
			}
		}
		RemoveEntities( "func_group" );
	}
// rhummer:
	mHasBeenResolved = true;
	if ( cvarSystem->GetCVarBool( "developer" ) ) {
		common->Printf( "^2idMapFile::Resolve has been run on ^0%s^2 so no func_groups exist.\n", name.c_str() );
	}
}

// rjohnson: added export
/*
============
idMapFile::Write
============
*/
bool idMapFile::Write( const char *fileName, const char *ext, bool fromBasePath, bool exportOnly ) {
	int i;
	int entityNum;
	idStr qpath;
	idFile *fp;
// RAVEN BEGIN
// rhummer: Used to verify func_groups didn't disappear somehow.
	bool funcGroupFound = false;
// RAVEN END

	qpath = fileName;
	qpath.SetFileExtension( ext );
	if ( replacementEntityFileLoaded ) {
		idLib::common->Warning( "Refusing to write %s while a .ent replacement is active; reload the original .map for editing first", qpath.c_str() );
		return false;
	}

	idLib::common->Printf( "writing %s...\n", qpath.c_str() );

	if ( fromBasePath ) {
		fp = idLib::fileSystem->OpenFileWrite( qpath, "fs_cdpath" );
	}
	else {
		fp = idLib::fileSystem->OpenExplicitFileWrite( qpath );
	}

	if ( !fp ) {
		idLib::common->Warning( "Couldn't open %s\n", qpath.c_str() );
		return false;
	}

	fp->WriteFloatString( "Version %d\n", CURRENT_MAP_VERSION );

	entityNum = 0;
	if (exportOnly)
	{
		for ( i = 0; i < entities.Num(); i++ ) 
		{
			if ( !entities[i]->IsFromEntityStringFile() && entities[i]->epairs.GetInt("export"))
			{
				entities[i]->Write( fp, entityNum++ );
			}
		}
	}
	else
	{
		for ( i = 0; i < entities.Num(); i++ ) {
			if ( entities[i]->IsFromEntityStringFile() ) {
				continue;
			}
			entities[i]->Write( fp, entityNum++ );
// RAVEN BEGIN
// rhummer: See if there are func_groups, there should be if there was during loading..
			if ( !idStr::Icmp( entities[i]->epairs.GetString( "classname" ), "func_group" ) ) {
				funcGroupFound = true;
			}
// RAVEN END
		}
	}

// RAVEN BEGIN
// rhummer: If this is true..something bad happened to cause all func_groups to go away between loading and saving.
	if ( mHasFuncGroups && !funcGroupFound && cvarSystem->GetCVarBool( "developer" ) ) {
		common->Warning( "^5Did not find any ^2func_groups^0 during save, but there were ^2func_groups^0 found during loading.  Was this intended?\n");
	}
// RAVEN END

	idLib::fileSystem->CloseFile( fp );

	return true;
}
// RAVEN END

/*
===============
idMapFile::SetGeometryCRC
===============
*/
void idMapFile::SetGeometryCRC( void ) {
	int i;

	geometryCRC = 0;
	for ( i = 0; i < entities.Num(); i++ ) {
		geometryCRC ^= entities[i]->GetGeometryCRC();
	}
}

/*
===============
idMapFile::AddEntity
===============
*/
int idMapFile::AddEntity( idMapEntity *mapEnt ) {
	int ret = entities.Append( mapEnt );
	return ret;
}

/*
===============
idMapFile::FindEntity
===============
*/
idMapEntity *idMapFile::FindEntity( const char *name ) {
	for ( int i = 0; i < entities.Num(); i++ ) {
		idMapEntity *ent = entities[i];
		if ( idStr::Icmp( ent->epairs.GetString( "name" ), name ) == 0 ) {
			return ent;
		}
	}
	return NULL;
}

/*
===============
idMapFile::RemoveEntity
===============
*/
void idMapFile::RemoveEntity( idMapEntity *mapEnt ) {
	entities.Remove( mapEnt );
	delete mapEnt;
}

/*
===============
idMapFile::RemoveEntity
===============
*/
void idMapFile::RemoveEntities( const char *classname ) {
	for ( int i = 0; i < entities.Num(); i++ ) {
		idMapEntity *ent = entities[i];
		if ( idStr::Icmp( ent->epairs.GetString( "classname" ), classname ) == 0 ) {
			delete entities[i];
			entities.RemoveIndex( i );
			i--;
		}
	}
}

/*
===============
idMapFile::RemoveAllEntities
===============
*/
void idMapFile::RemoveAllEntities() {
	entities.DeleteContents( true );
	hasPrimitiveData = false;
}

/*
===============
idMapFile::RemovePrimitiveData
===============
*/
void idMapFile::RemovePrimitiveData() {
	for ( int i = 0; i < entities.Num(); i++ ) {
		idMapEntity *ent = entities[i];
		ent->RemovePrimitiveData();
	}
	hasPrimitiveData = false;
// RAVEN BEGIN
// rhummer: debugging stuff..
	if ( cvarSystem->GetCVarBool( "developer" ) ) {
		common->Printf( "^2Removed all primitive data from ^0%s\n", name.c_str() );
	}
// RAVEN END
}

/*
===============
idMapFile::NeedsReload
===============
*/
bool idMapFile::NeedsReload() {
	if ( !loadedSourceFileName.Length() ) {
		return true;
	}

	idFile *sourceFile = parsedFromOSPath
		? idLib::fileSystem->OpenExplicitFileRead( loadedSourceFileName )
		: idLib::fileSystem->OpenFileRead( loadedSourceFileName );
	if ( sourceFile == NULL ) {
		return true;
	}
	const unsigned int sourceTime = static_cast<unsigned int>( sourceFile->Timestamp() );
	idLib::fileSystem->CloseFile( sourceFile );
	if ( sourceTime != fileTime ) {
		return true;
	}

	if ( entityStringFilesEnabled ) {
		idStr entityFileName = name;
		entityFileName.SetFileExtension( "ent" );
		idFile *entityFile = parsedFromOSPath
			? idLib::fileSystem->OpenExplicitFileRead( entityFileName )
			: idLib::fileSystem->OpenFileRead( entityFileName );
		if ( ( entityFile != NULL ) != replacementEntityFileLoaded ) {
			if ( entityFile != NULL ) {
				idLib::fileSystem->CloseFile( entityFile );
			}
			return true;
		}
		if ( entityFile != NULL ) {
			const unsigned int entityFileTime = static_cast<unsigned int>( entityFile->Timestamp() );
			idLib::fileSystem->CloseFile( entityFile );
			if ( entityFileTime != replacementEntityFileTime ) {
				return true;
			}
		}

		entityFileName = name;
		entityFileName.SetFileExtension( "entx" );
		entityFile = parsedFromOSPath
			? idLib::fileSystem->OpenExplicitFileRead( entityFileName )
			: idLib::fileSystem->OpenFileRead( entityFileName );
		if ( ( entityFile != NULL ) != extenderEntityFileLoaded ) {
			if ( entityFile != NULL ) {
				idLib::fileSystem->CloseFile( entityFile );
			}
			return true;
		}
		if ( entityFile != NULL ) {
			const unsigned int entityFileTime = static_cast<unsigned int>( entityFile->Timestamp() );
			idLib::fileSystem->CloseFile( entityFile );
			if ( entityFileTime != extenderEntityFileTime ) {
				return true;
			}
		}
	}
	return false;
}

bool idMapFile::WriteExport( const char *fileName, bool fromBasePath )
{
	int			i, j;
	idDict		newPairs;

	for ( i = 0; i < entities.Num(); i++ ) 
	{
		idMapEntity *ent = entities[i];
		if ( ent->IsFromEntityStringFile() ) {
			continue;
		}

		if ( ent->epairs.GetInt("export") == 1 )
		{
/*			for ( j = 0; j < ent->epairs.GetNumKeyVals(); j++) 
			{
				const idKeyValue	*kv = ent->epairs.GetKeyVal(j);

				if (kv->key.CmpPrefix("export_") == 0)
				{
					ent->epairs.Delete(kv->key.c_str());
					j--;
					continue;
				}
			}
*/
			newPairs.Clear();

			for ( j = 0; j < ent->epairs.GetNumKeyVals(); j++) 
			{
				const idKeyValue	*kv = ent->epairs.GetKeyVal(j);

				if (kv->GetKey().CmpPrefix("export_") != 0)
				{
					char				newName[1024];

					sprintf(newName, "export_%s", kv->GetKey().c_str());

					if (!ent->epairs.GetString(newName, 0))
					{	// don't overwrite an existing export
						newPairs.Set (newName, kv->GetValue().c_str());
					}
				}
			}

			ent->epairs.Copy(newPairs);
		}
	}

	return Write(fileName, "export", fromBasePath, true);
}

bool idMapFile::ParseExport( const char *filename, bool osPath )
{
	// no string concatenation for epairs and allow path names for materials
// RAVEN BEGIN
// jsinger: Done this way to reduce the amount of code to change.
//          The auto pointer will delete the lexer when this method exits.
	idAutoPtr<Lexer>		lexer(LexerFactory::MakeLexer( LEXFL_NOSTRINGCONCAT | LEXFL_NOSTRINGESCAPECHARS | LEXFL_ALLOWPATHNAMES ) );
	Lexer &src(*lexer);
// RAVEN END
	idToken		token;
	idStr		fullName;
	idMapEntity *mapEnt;
	int			i, j;
	int			numMerged, numNotMerged, numAdded, numRemoved;

	numMerged = numNotMerged = numAdded = numRemoved = 0;
	mHasExportEntities = false;
	
	fullName = filename;
	fullName.StripFileExtension();
	fullName.SetFileExtension( "export" );
	src.LoadFile( fullName, osPath );
	if ( !src.IsLoaded() ) {
		// didn't get anything at all
		return false;
	}

	version = OLD_MAP_VERSION;
	mExportEntities.DeleteContents( true );

	if ( src.CheckTokenString( "Version" ) ) {
		src.ReadTokenOnLine( &token );
		version = token.GetIntValue();
	}

	while( 1 ) {
		mapEnt = idMapEntity::Parse( src, false, version );
		if ( !mapEnt ) {
			break;
		}
		mExportEntities.Append( mapEnt );
	}

	if (mExportEntities.Num())
	{
		mHasExportEntities = true;
	}

	for(i=0;i<mExportEntities.Num();i++)
	{
		idMapEntity *ent = mExportEntities[i];
		idMapEntity *mapEnt;
		const char	*name;

		name = ent->epairs.GetString("name");
		mapEnt = FindEntity(name);

		if (!mapEnt)
		{	// check to see if we have an export_name
			name = ent->epairs.GetString("export_name", 0);
			if (!name || ent->epairs.GetInt("original") )
			{	// this is a new entity
				ent->epairs.SetInt("export", 2);			// 2 = when re-exporting, don't duplicated the export_ fields
				ent->epairs.SetInt("merged", 1);
				ent->epairs.SetInt("original", 0);			// 0 = now the entity in map is always treated as master
				entities.Append(ent);
				mExportEntities.RemoveIndex(i);
				i--;
				numAdded++;
				continue;
			}
			else
			{
				mapEnt = FindEntity(name);
			}
		}

		if (!mapEnt)
		{	// this export entity has been removed from the original map
			numNotMerged++;
		}
		else
		{	// exists in both, do a merge!
			mapEnt->epairs.SetInt("export", 2);			// 2 = when re-exporting, don't duplicated the export_ fields
			mapEnt->epairs.SetInt("merged", 1);
			for ( j = 0; j < ent->epairs.GetNumKeyVals(); j++) 
			{
				char				origName[1024];
				const idKeyValue	*exportKV = ent->epairs.GetKeyVal(j);
				const idKeyValue	*origKV, *mapKV;

				if (exportKV->GetKey().CmpPrefix("export_") != 0)
				{
					sprintf(origName, "export_%s", exportKV->GetKey().c_str());
					origKV = ent->epairs.FindKey(origName);
					mapKV = mapEnt->epairs.FindKey(exportKV->GetKey().c_str());

					if (origKV && mapKV)
					{	// keys exist in all spots, compare
						if (origKV->GetValue() == exportKV->GetValue())
						{	// export hasn't change
						}
						else if (mapKV->GetValue() == origKV->GetValue())
						{	// map is same as the orig, yet export has changed, so bring it over
							mapEnt->epairs.Set(exportKV->GetKey().c_str(), exportKV->GetValue().c_str());
						}
					}
					else if (origKV)
					{	// map has removed this key, so we shouldn't merge it
					}
					else if (mapKV)
					{	// map has added this key, so we'll ignore the exported value, which has also been added
					}
					else
					{	// this was added to the export, so merge it in
						mapEnt->epairs.Set(exportKV->GetKey().c_str(), exportKV->GetValue().c_str());
					}
				}
				else
				{
					mapEnt->epairs.Set(exportKV->GetKey().c_str(), exportKV->GetValue().c_str());
				}
			}
			numMerged++;
		}
	}

	// check for any entities that have been marked as exported in the original map but have been removed from the export file
	for(i=0;i<entities.Num();i++)
	{
		idMapEntity *ent = entities[i];

		if (ent->epairs.GetInt("export"))
		{
			if (!ent->epairs.GetInt("merged"))
			{
				entities.RemoveIndex(i);
				numRemoved++;
				i--;
			}
			else
			{
				ent->epairs.Delete("merged");
			}
		}
	}

	common->Printf("Export Merge: %d added, %d merged, %d not merged, %d removed\n", numAdded, numMerged, numNotMerged, numRemoved);

	mExportEntities.DeleteContents( true );

	return true;
}
