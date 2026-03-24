/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").  

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/





#include "tr_local.h"
#include "smaa/AreaTex.h"
#include "smaa/SearchTex.h"

#define	DEFAULT_SIZE	16

/*
==================
idImage::MakeDefault

the default image will be grey with a white box outline
to allow you to see the mapping coordinates on a surface
==================
*/
void idImage::MakeDefault() {
	int		x, y;
	byte	data[DEFAULT_SIZE][DEFAULT_SIZE][4];

	if ( com_developer.GetBool() ) {
		// grey center
		for ( y = 0 ; y < DEFAULT_SIZE ; y++ ) {
			for ( x = 0 ; x < DEFAULT_SIZE ; x++ ) {
				data[y][x][0] = 32;
				data[y][x][1] = 32;
				data[y][x][2] = 32;
				data[y][x][3] = 255;
			}
		}

		// white border
		for ( x = 0 ; x < DEFAULT_SIZE ; x++ ) {
			data[0][x][0] =
				data[0][x][1] =
				data[0][x][2] =
				data[0][x][3] = 255;

			data[x][0][0] =
				data[x][0][1] =
				data[x][0][2] =
				data[x][0][3] = 255;

			data[DEFAULT_SIZE-1][x][0] =
				data[DEFAULT_SIZE-1][x][1] =
				data[DEFAULT_SIZE-1][x][2] =
				data[DEFAULT_SIZE-1][x][3] = 255;

			data[x][DEFAULT_SIZE-1][0] =
				data[x][DEFAULT_SIZE-1][1] =
				data[x][DEFAULT_SIZE-1][2] =
				data[x][DEFAULT_SIZE-1][3] = 255;
		}
	} else {
		for ( y = 0 ; y < DEFAULT_SIZE ; y++ ) {
			for ( x = 0 ; x < DEFAULT_SIZE ; x++ ) {
				data[y][x][0] = 0;
				data[y][x][1] = 0;
				data[y][x][2] = 0;
				data[y][x][3] = 0;
			}
		}
	}

	GenerateImage( (byte *)data, 
		DEFAULT_SIZE, DEFAULT_SIZE, 
		TF_DEFAULT, TR_REPEAT, TD_DEFAULT );

	defaulted = true;
}

static void R_DefaultImage( idImage *image ) {
	image->MakeDefault();
}

static void R_WhiteImage( idImage *image ) {
	byte	data[DEFAULT_SIZE][DEFAULT_SIZE][4];

	// solid white texture
	memset( data, 255, sizeof( data ) );
	image->GenerateImage( (byte *)data, DEFAULT_SIZE, DEFAULT_SIZE, 
		TF_DEFAULT, TR_REPEAT, TD_DEFAULT );
}

static void R_BlackImage( idImage *image ) {
	byte	data[DEFAULT_SIZE][DEFAULT_SIZE][4];

	// solid black texture
	memset( data, 0, sizeof( data ) );
	image->GenerateImage( (byte *)data, DEFAULT_SIZE, DEFAULT_SIZE, 
		TF_DEFAULT, TR_REPEAT, TD_DEFAULT );
}

static void R_RGBA8Image( idImage *image ) {
	byte	data[DEFAULT_SIZE][DEFAULT_SIZE][4];

	memset( data, 0, sizeof( data ) );
	data[0][0][0] = 16;
	data[0][0][1] = 32;
	data[0][0][2] = 48;
	data[0][0][3] = 96;

	image->GenerateImage( (byte *)data, DEFAULT_SIZE, DEFAULT_SIZE, TF_DEFAULT, TR_REPEAT, TD_LOOKUP_TABLE_RGBA );
}

static void R_RGBA16FImage( idImage *image ) {
	idImageOpts opts;
	opts.textureType = TT_2D;
	opts.format = FMT_RGBA16F;
	opts.width = DEFAULT_SIZE;
	opts.height = DEFAULT_SIZE;
	opts.numLevels = 1;

	image->AllocImage( opts, TF_LINEAR, TR_CLAMP );

	if ( !tr.IsOpenGLRunning() ) {
		return;
	}

	unsigned short data[DEFAULT_SIZE][DEFAULT_SIZE][4];
	memset( data, 0, sizeof( data ) );
	image->SubImageUpload( 0, 0, 0, 0, DEFAULT_SIZE, DEFAULT_SIZE, data );
}

static void R_DepthImage( idImage *image ) {
	byte	data[DEFAULT_SIZE][DEFAULT_SIZE][4];

	memset( data, 0, sizeof( data ) );
	data[0][0][0] = 16;
	data[0][0][1] = 32;
	data[0][0][2] = 48;
	data[0][0][3] = 96;

	image->GenerateImage( (byte *)data, DEFAULT_SIZE, DEFAULT_SIZE, TF_NEAREST, TR_CLAMP, TD_DEPTH );
}

static void R_AlphaNotchImage( idImage *image ) {
	byte	data[2][4];

	// this is used for alpha test clip planes

	data[0][0] = data[0][1] = data[0][2] = 255;
	data[0][3] = 0;
	data[1][0] = data[1][1] = data[1][2] = 255;
	data[1][3] = 255;

	image->GenerateImage( (byte *)data, 2, 1, TF_NEAREST, TR_CLAMP, TD_LOOKUP_TABLE_ALPHA );
}

static void R_FlatNormalImage( idImage *image ) {
	byte	data[DEFAULT_SIZE][DEFAULT_SIZE][4];

	// flat normal map for default bunp mapping
	for ( int i = 0 ; i < 4 ; i++ ) {
		data[0][i][0] = 128;
		data[0][i][1] = 128;
		data[0][i][2] = 255;
		data[0][i][3] = 255;
	}
	image->GenerateImage( (byte *)data, 2, 2, TF_DEFAULT, TR_REPEAT, TD_BUMP );
}

/*
================
R_CreateNoFalloffImage

This is a solid white texture that is zero clamped.
================
*/
static void R_CreateNoFalloffImage( idImage *image ) {
	int		x,y;
	byte	data[16][FALLOFF_TEXTURE_SIZE][4];

	memset( data, 0, sizeof( data ) );
	for (x=1 ; x<FALLOFF_TEXTURE_SIZE-1 ; x++) {
		for (y=1 ; y<15 ; y++) {
			data[y][x][0] = 255;
			data[y][x][1] = 255;
			data[y][x][2] = 255;
			data[y][x][3] = 255;
		}
	}
	image->GenerateImage( (byte *)data, FALLOFF_TEXTURE_SIZE, 16, TF_DEFAULT, TR_CLAMP_TO_ZERO, TD_LOOKUP_TABLE_MONO );
}

/*
================
R_FogImage

We calculate distance correctly in two planes, but the
third will still be projection based
================
*/
const int	FOG_SIZE = 128;

void R_FogImage( idImage *image ) {
	int		x,y;
	byte	data[FOG_SIZE][FOG_SIZE][4];
	int		b;

	float	step[256];
	int		i;
	float	remaining = 1.0;
	for ( i = 0 ; i < 256 ; i++ ) {
		step[i] = remaining;
		remaining *= 0.982f;
	}

	for (x=0 ; x<FOG_SIZE ; x++) {
		for (y=0 ; y<FOG_SIZE ; y++) {
			float	d;

			d = idMath::Sqrt( (x - FOG_SIZE/2) * (x - FOG_SIZE/2) 
				+ (y - FOG_SIZE/2) * (y - FOG_SIZE / 2) );
			d /= FOG_SIZE/2-1;

			b = (byte)(d * 255);
			if ( b <= 0 ) {
				b = 0;
			} else if ( b > 255 ) {
				b = 255;
			}
			b = (byte)(255 * ( 1.0 - step[b] ));
			if ( x == 0 || x == FOG_SIZE-1 || y == 0 || y == FOG_SIZE-1 ) {
				b = 255;		// avoid clamping issues
			}
			data[y][x][0] =
			data[y][x][1] =
			data[y][x][2] = 255;
			data[y][x][3] = b;
		}
	}

	image->GenerateImage( (byte *)data, FOG_SIZE, FOG_SIZE, TF_LINEAR, TR_CLAMP, TD_LOOKUP_TABLE_ALPHA );
}


/*
================
FogFraction

Height values below zero are inside the fog volume
================
*/
static const float	RAMP_RANGE =	8;
static const float	DEEP_RANGE =	-30;
static float	FogFraction( float viewHeight, float targetHeight ) {
	float	total = idMath::Fabs( targetHeight - viewHeight );

//	return targetHeight >= 0 ? 0 : 1.0;

	// only ranges that cross the ramp range are special
	if ( targetHeight > 0 && viewHeight > 0 ) {
		return 0.0;
	}
	if ( targetHeight < -RAMP_RANGE && viewHeight < -RAMP_RANGE ) {
		return 1.0;
	}

	float	above;
	if ( targetHeight > 0 ) {
		above = targetHeight;
	} else if ( viewHeight > 0 ) {
		above = viewHeight;
	} else {
		above = 0;
	}

	float	rampTop, rampBottom;

	if ( viewHeight > targetHeight ) {
		rampTop = viewHeight;
		rampBottom = targetHeight;
	} else {
		rampTop = targetHeight;
		rampBottom = viewHeight;
	}
	if ( rampTop > 0 ) {
		rampTop = 0;
	}
	if ( rampBottom < -RAMP_RANGE ) {
		rampBottom = -RAMP_RANGE;
	}

	float	rampSlope = 1.0 / RAMP_RANGE;

	if ( !total ) {
		return -viewHeight * rampSlope;
	}

	float ramp = ( 1.0 - ( rampTop * rampSlope + rampBottom * rampSlope ) * -0.5 ) * ( rampTop - rampBottom );

	float	frac = ( total - above - ramp ) / total;

	// after it gets moderately deep, always use full value
	float deepest = viewHeight < targetHeight ? viewHeight : targetHeight;

	float	deepFrac = deepest / DEEP_RANGE;
	if ( deepFrac >= 1.0 ) {
		return 1.0;
	}

	frac = frac * ( 1.0 - deepFrac ) + deepFrac;

	return frac;
}

/*
================
R_FogEnterImage

Modulate the fog alpha density based on the distance of the
start and end points to the terminator plane
================
*/
void R_FogEnterImage( idImage *image ) {
	int		x,y;
	byte	data[FOG_ENTER_SIZE][FOG_ENTER_SIZE][4];
	int		b;

	for (x=0 ; x<FOG_ENTER_SIZE ; x++) {
		for (y=0 ; y<FOG_ENTER_SIZE ; y++) {
			float	d;

			d = FogFraction( x - (FOG_ENTER_SIZE / 2), y - (FOG_ENTER_SIZE / 2) );

			b = (byte)(d * 255);
			if ( b <= 0 ) {
				b = 0;
			} else if ( b > 255 ) {
				b = 255;
			}
			data[y][x][0] =
			data[y][x][1] =
			data[y][x][2] = 255;
			data[y][x][3] = b;
		}
	}

	// if mipmapped, acutely viewed surfaces fade wrong
	image->GenerateImage( (byte *)data, FOG_ENTER_SIZE, FOG_ENTER_SIZE, TF_LINEAR, TR_CLAMP, TD_LOOKUP_TABLE_ALPHA );
}


/*
================
R_QuadraticImage

================
*/
static const int	QUADRATIC_WIDTH = 32;
static const int	QUADRATIC_HEIGHT = 4;

void R_QuadraticImage( idImage *image ) {
	int		x,y;
	byte	data[QUADRATIC_HEIGHT][QUADRATIC_WIDTH][4];
	int		b;


	for (x=0 ; x<QUADRATIC_WIDTH ; x++) {
		for (y=0 ; y<QUADRATIC_HEIGHT ; y++) {
			float	d;

			d = x - (QUADRATIC_WIDTH/2 - 0.5);
			d = idMath::Fabs( d );
			d -= 0.5;
			d /= QUADRATIC_WIDTH/2;
		
			d = 1.0 - d;
			d = d * d;

			b = (byte)(d * 255);
			if ( b <= 0 ) {
				b = 0;
			} else if ( b > 255 ) {
				b = 255;
			}
			data[y][x][0] =
			data[y][x][1] =
			data[y][x][2] = b;
			data[y][x][3] = 255;
		}
	}

	image->GenerateImage( (byte *)data, QUADRATIC_WIDTH, QUADRATIC_HEIGHT, TF_DEFAULT, TR_CLAMP, TD_LOOKUP_TABLE_RGB1 );
}

static void R_AmbientNormalImage(idImage* image) {
	byte	data[DEFAULT_SIZE][DEFAULT_SIZE][4];
	int		i;

	int red = (/*globalImages->image_useNormalCompression.GetInteger() == 1*/ false) ? 0 : 3;
	int alpha = (red == 0) ? 3 : 0;
	// flat normal map for default bunp mapping
	for (i = 0; i < 4; i++) {
		data[0][i][red] = (byte)(255 * tr.ambientLightVector[0]);
		data[0][i][1] = (byte)(255 * tr.ambientLightVector[1]);
		data[0][i][2] = (byte)(255 * tr.ambientLightVector[2]);
		data[0][i][alpha] = 255;
	}
	const byte* pics[6];
	for (i = 0; i < 6; i++) {
		pics[i] = data[0][0];
	}
	// this must be a cube map for fragment programs to simply substitute for the normalization cube map
	image->GenerateCubeImage(pics, 2, TF_DEFAULT, TD_DEFAULT);
}

/*
================
R_SpecularTableImage

Creates a ramp that matches our fudged specular calculation
================
*/
static void R_SpecularTableImage(idImage* image) {
	int		x;
	byte	data[256][4];

	for (x = 0; x < 256; x++) {
		float f = x / 255.f;
#if 0
		f = pow(f, 16);
#else
		// this is the behavior of the hacked up fragment programs that
		// can't really do a power function
		f = (f - 0.75) * 4;
		if (f < 0) {
			f = 0;
		}
		f = f * f;
#endif
		int		b = (int)(f * 255);

		data[x][0] =
			data[x][1] =
			data[x][2] =
			data[x][3] = b;
	}

	image->GenerateImage((byte*)data, 256, 1, TF_LINEAR, TR_CLAMP, TD_DEFAULT);
}

#define	NORMAL_MAP_SIZE		32

/*** NORMALIZATION CUBE MAP CONSTRUCTION ***/

/* Given a cube map face index, cube map size, and integer 2D face position,
 * return the cooresponding normalized vector.
 */
static void getCubeVector(int i, int cubesize, int x, int y, float* vector) {
	float s, t, sc, tc, mag;

	s = ((float)x + 0.5) / (float)cubesize;
	t = ((float)y + 0.5) / (float)cubesize;
	sc = s * 2.0 - 1.0;
	tc = t * 2.0 - 1.0;

	switch (i) {
	case 0:
		vector[0] = 1.0;
		vector[1] = -tc;
		vector[2] = -sc;
		break;
	case 1:
		vector[0] = -1.0;
		vector[1] = -tc;
		vector[2] = sc;
		break;
	case 2:
		vector[0] = sc;
		vector[1] = 1.0;
		vector[2] = tc;
		break;
	case 3:
		vector[0] = sc;
		vector[1] = -1.0;
		vector[2] = -tc;
		break;
	case 4:
		vector[0] = sc;
		vector[1] = -tc;
		vector[2] = 1.0;
		break;
	case 5:
		vector[0] = -sc;
		vector[1] = -tc;
		vector[2] = -1.0;
		break;
	}

	mag = idMath::InvSqrt(vector[0] * vector[0] + vector[1] * vector[1] + vector[2] * vector[2]);
	vector[0] *= mag;
	vector[1] *= mag;
	vector[2] *= mag;
}

/* Initialize a cube map texture object that generates RGB values
 * that when expanded to a [-1,1] range in the register combiners
 * form a normalized vector matching the per-pixel vector used to
 * access the cube map.
 */
static void makeNormalizeVectorCubeMap(idImage* image) {
	float vector[3];
	int i, x, y;
	byte* pixels[6];
	int		size;

	size = NORMAL_MAP_SIZE;

	pixels[0] = (GLubyte*)Mem_Alloc(size * size * 4 * 6);

	for (i = 0; i < 6; i++) {
		pixels[i] = pixels[0] + i * size * size * 4;
		for (y = 0; y < size; y++) {
			for (x = 0; x < size; x++) {
				getCubeVector(i, size, x, y, vector);
				pixels[i][4 * (y * size + x) + 0] = (byte)(128 + 127 * vector[0]);
				pixels[i][4 * (y * size + x) + 1] = (byte)(128 + 127 * vector[1]);
				pixels[i][4 * (y * size + x) + 2] = (byte)(128 + 127 * vector[2]);
				pixels[i][4 * (y * size + x) + 3] = 255;
			}
		}
	}

	image->GenerateCubeImage((const byte**)pixels, size,
		TF_LINEAR, TD_DEFAULT);

	Mem_Free(pixels[0]);
}

// the size determines how far away from the edge the blocks start fading
static const int BORDER_CLAMP_SIZE = 32;
static void R_BorderClampImage(idImage* image) {
	byte	data[BORDER_CLAMP_SIZE][BORDER_CLAMP_SIZE][4];

	// solid white texture with a single pixel black border
	memset(data, 255, sizeof(data));
	for (int i = 0; i < BORDER_CLAMP_SIZE; i++) {
		data[i][0][0] =
			data[i][0][1] =
			data[i][0][2] =
			data[i][0][3] =

			data[i][BORDER_CLAMP_SIZE - 1][0] =
			data[i][BORDER_CLAMP_SIZE - 1][1] =
			data[i][BORDER_CLAMP_SIZE - 1][2] =
			data[i][BORDER_CLAMP_SIZE - 1][3] =

			data[0][i][0] =
			data[0][i][1] =
			data[0][i][2] =
			data[0][i][3] =

			data[BORDER_CLAMP_SIZE - 1][i][0] =
			data[BORDER_CLAMP_SIZE - 1][i][1] =
			data[BORDER_CLAMP_SIZE - 1][i][2] =
			data[BORDER_CLAMP_SIZE - 1][i][3] = 0;
	}

	image->GenerateImage((byte*)data, BORDER_CLAMP_SIZE, BORDER_CLAMP_SIZE,
		TF_LINEAR /* TF_NEAREST */, TR_CLAMP_TO_BORDER, TD_DEFAULT);

	if (!glConfig.isInitialized) {
		// can't call qglTexParameterfv yet
		return;
	}
	// explicit zero border
	float	color[4];
	color[0] = color[1] = color[2] = color[3] = 0;
	glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, color);
}

/*
================
R_RampImage

Creates a 0-255 ramp image
================
*/
static void R_RampImage(idImage* image) {
	int		x;
	byte	data[256][4];

	for (x = 0; x < 256; x++) {
		data[x][0] =
			data[x][1] =
			data[x][2] =
			data[x][3] = x;
	}

	image->GenerateImage((byte*)data, 256, 1,
		TF_NEAREST, TR_CLAMP, TD_DIFFUSE);
}

static void R_SMAAAreaImage( idImage *image ) {
	const int pixelCount = AREATEX_WIDTH * AREATEX_HEIGHT;
	byte *data = (byte *)Mem_Alloc( pixelCount * 4 );

	for ( int src = 0, dst = 0; src < AREATEX_SIZE; src += 2, dst += 4 ) {
		data[dst + 0] = areaTexBytes[src + 0];
		data[dst + 1] = areaTexBytes[src + 1];
		data[dst + 2] = 0;
		data[dst + 3] = 255;
	}

	image->GenerateImage( data, AREATEX_WIDTH, AREATEX_HEIGHT, TF_LINEAR, TR_CLAMP, TD_LOOKUP_TABLE_RGBA );
	Mem_Free( data );
}

static void R_SMAASearchImage( idImage *image ) {
	const int pixelCount = SEARCHTEX_WIDTH * SEARCHTEX_HEIGHT;
	byte *data = (byte *)Mem_Alloc( pixelCount * 4 );

	for ( int src = 0, dst = 0; src < SEARCHTEX_SIZE; src++, dst += 4 ) {
		data[dst + 0] = searchTexBytes[src];
		data[dst + 1] = 0;
		data[dst + 2] = 0;
		data[dst + 3] = 255;
	}

	image->GenerateImage( data, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, TF_LINEAR, TR_CLAMP, TD_LOOKUP_TABLE_RGBA );
	Mem_Free( data );
}


/*
================
idImageManager::CreateIntrinsicImages
================
*/
void idImageManager::CreateIntrinsicImages() {
	// create built in images
	ambientNormalMap = ImageFromFunction("_ambient", R_AmbientNormalImage);
	specularTableImage = ImageFromFunction("_specularTable", R_SpecularTableImage);
	normalCubeMapImage = ImageFromFunction("_normalCubeMap", makeNormalizeVectorCubeMap);

	// create built in images
	defaultImage = ImageFromFunction("_default", R_DefaultImage);
	whiteImage = ImageFromFunction("_white", R_WhiteImage);
	blackImage = ImageFromFunction("_black", R_BlackImage);
	//borderClampImage = ImageFromFunction("_borderClamp", R_BorderClampImage);
	flatNormalMap = ImageFromFunction("_flat", R_FlatNormalImage);
	specularTableImage = ImageFromFunction("_specularTable", R_SpecularTableImage);
	//specular2DTableImage = ImageFromFunction("_specular2DTable", R_Specular2DTableImage);
	rampImage = ImageFromFunction("_ramp", R_RampImage);
	alphaRampImage = ImageFromFunction("_alphaRamp", R_RampImage);
	alphaNotchImage = ImageFromFunction("_alphaNotch", R_AlphaNotchImage);
	fogImage = ImageFromFunction("_fog", R_FogImage);
	fogEnterImage = ImageFromFunction("_fogEnter", R_FogEnterImage);
	normalCubeMapImage = ImageFromFunction("_normalCubeMap", makeNormalizeVectorCubeMap);
	noFalloffImage = ImageFromFunction("_noFalloff", R_CreateNoFalloffImage);
	ImageFromFunction("_quadratic", R_QuadraticImage);

	// cinematicImage is used for cinematic drawing
	// scratchImage is used for screen wipes/doublevision etc..
	cinematicImage = ImageFromFunction("_cinematic", R_RGBA8Image);
	scratchImage = ImageFromFunction("_scratch", R_RGBA8Image);
	scratchImage2 = ImageFromFunction("_scratch2", R_RGBA8Image);
	accumImage = ImageFromFunction("_accum", R_RGBA8Image);
	//scratchCubeMapImage = ImageFromFunction("_scratchCubeMap", makeNormalizeVectorCubeMap);

	currentRenderImage = ImageFromFunction("_currentRender", R_RGBA16FImage);
	currentDepthImage = ImageFromFunction("_currentDepth", R_DepthImage);
	ImageFromFunction("_smaaArea", R_SMAAAreaImage);
	ImageFromFunction("_smaaSearch", R_SMAASearchImage);

	// placeholders for runtime render targets referenced by materials
	ImageFromFunction("_forwardRenderResolvedAlbedo", R_RGBA16FImage);
	ImageFromFunction("_postProcessAlbedo0", R_RGBA16FImage);
	ImageFromFunction("_postProcessAlbedo1", R_RGBA16FImage);


	// save a copy of this for material comparison, because currentRenderImage may get
	// reassigned during stereo rendering
	originalCurrentRenderImage = currentRenderImage;

	loadingIconImage = ImageFromFile("gfx/guis/loadscreens/generic", TF_DEFAULT, TR_CLAMP, TD_DEFAULT, CF_2D );
	hellLoadingIconImage = ImageFromFile("gfx/guis/loadscreens/generic", TF_DEFAULT, TR_CLAMP, TD_DEFAULT, CF_2D );

	//release_assert( loadingIconImage->referencedOutsideLevelLoad );
	//release_assert( hellLoadingIconImage->referencedOutsideLevelLoad );
}
