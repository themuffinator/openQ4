// Copyright (C) 2026 DarkMatter Productions
//
/*
===============================================================================

	gles_d3 -- authored material stages, 2D and GUI.

	The RB_STD_DrawShaderPasses / RB_STD_T_RenderShaderPasses port
	(draw_common.cpp:7442 and :6878), rebuilt for OpenGL ES 3.0.

	This pass draws more than the menu. Quake 4 renders the world into a
	game-owned render texture and resolves it to the back buffer with a
	fullscreen material draw, so until this works the 3D scene has no route to
	the screen at all.

	One draw per stage, deliberately. Collapsing a material to a single draw
	is what leaves the ModernGL path unable to render materials whose later
	stages read alpha their earlier stages wrote (gfx/guis/hud/ekg), and that
	limitation is expensive to unpick later.

===============================================================================
*/

#ifdef OPENQ4_RENDERER_GLES_MODULE

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../tr_local.h"
#include "gles_d3_local.h"
#include "gles_program.h"

/*
===============================================================================
	Unsupported-feature accounting.

	Under BE_GLES_D3 there is no legacy path behind a skip, so a skipped stage
	is a surface that does not appear. Every skip is counted by reason and
	named once, because "an absent feature must be a recorded absence, never a
	silent one" is the single lesson that cost the ModernGL bring-up the most
	frames (G6k/G6l).
===============================================================================
*/
typedef enum {
	GLESD3_SKIP_TEXGEN = 0,		// texgen other than TG_EXPLICIT
	GLESD3_SKIP_NEWSTAGE,		// GLSL / ARB material program (D7)
	GLESD3_SKIP_CUSTOM_LIGHTING,	// program stage the interaction pass owns
	GLESD3_SKIP_POSTPROCESS,	// SS_POST_PROCESS, needs a _currentRender copy
	GLESD3_SKIP_NO_GEOMETRY,	// no ambient or index cache
	GLESD3_SKIP_COUNT
} glesD3SkipReason_t;

static const char *gles_skipReasonNames[ GLESD3_SKIP_COUNT ] = {
	"texgen", "materialProgram", "customLighting", "postProcess", "geometry"
};
// Up to this many DISTINCT material names per reason, not just the first.
// Recording only the first was a real blind spot: "postProcess=7" named
// warp_mask and read as "seven heat-haze surfaces", when the break at the
// first SS_POST_PROCESS surface drops every surface after it whatever its
// material. Which materials those are is the whole question.
static const int GLESD3_SKIP_NAMES = 5;
static int	gles_skipCounts[ GLESD3_SKIP_COUNT ];
static char	gles_skipNames[ GLESD3_SKIP_COUNT ][ GLESD3_SKIP_NAMES ][ 96 ];
static int	gles_skipNameCounts[ GLESD3_SKIP_COUNT ];
static int	gles_skipNamesOverflow[ GLESD3_SKIP_COUNT ];

/*
====================
GLESD3_StepError

r_glesD3Report 2 names the first call in the stage draw that raises a GL
error, once. Chasing an accumulated error across a whole pass cost the
ModernGL bring-up a round (G6g: "the earlier GL_INVALID_ENUM reading came
from measuring the accumulated error across the whole compose block").
====================
*/
static bool gles_stepErrorReported = false;
static int gles_dumpedDraws = 0;

/*
====================
r_glesD3DepthFillColor

The depth prepass paints black behind opaque geometry, exactly as the legacy
path does. Setting this to 0 masks that colour write, so a surface that is
never lit and never shaded shows r_glesD3DebugClearColor rather than black -- which
is the difference between "drew nothing here" and "drew black here", and is
not otherwise visible.
====================
*/
static idCVar r_glesD3DepthFillColor( "r_glesD3DepthFillColor", "1", CVAR_RENDERER | CVAR_BOOL,
		"gles_d3: depth prepass writes black to colour (1, the legacy behaviour); 0 masks it so unshaded surfaces show the clear colour" );

/*
====================
r_glesD3SkipCubeTexgen

Bisect switch for the D7a cube-texgen stages (TG_REFLECT_CUBE,
TG_SKYBOX_CUBE, TG_WOBBLESKY_CUBE, TG_DIFFUSE_CUBE). Set it and those stages
are skipped and counted, exactly as they were before D7a landed.

It exists because of an open defect: black patches on the dropship hull and
elsewhere, which r_skipAmbient 1 removes -- so a MATERIAL stage is painting
them -- while the per-draw dump clears the other two stages of that material
(the glow stage is additive, and maskcolor really does carry mask=RGB-Z, so
it writes alpha only). The reflect-cube stage is the one stage of the three
the dump could not see, and the one D7a added.

One bit, togglable in front of the artifact: if the black disappears with
this set, the D7a stage is responsible; if it stays, D7a is exonerated and
the culprit is another material entirely. That is worth more than another
round of reasoning about blend modes.
====================
*/
static idCVar r_glesD3SkipCubeTexgen( "r_glesD3SkipCubeTexgen", "0", CVAR_RENDERER | CVAR_BOOL,
		"gles_d3: skip the cube-map texgen stages (reflect/skybox/wobblesky/diffuse cube) and count them, as before D7a" );

/*
====================
r_glesD3SkipMaterial

Skip every material-pass surface whose material name contains this substring.

A bisect tool, and the one that finally worked. The black-surface hunt burned
six hypotheses because each one needed its own build to test; a name filter
turns "which material draws this?" into a binary search over the per-draw dump
that costs one run per step and no rebuild. Set it to a path prefix to cut a
whole family ("gfx/effects"), then narrow.

It skips the SURFACE, not the stage: a material's stages only make sense
together, and half-drawing one produces an artifact that is neither the bug
nor the fix.
====================
*/
static idCVar r_glesD3SkipMaterial( "r_glesD3SkipMaterial", "", CVAR_RENDERER,
		"gles_d3: skip material-pass surfaces whose material name contains this substring" );

/*
====================
r_glesD3SkipMaterialPrograms

Turns the material-program stages this backend can draw -- heat haze, masked
heat haze, bumpy environment, monochrome -- off, as a player setting rather than
a bisect tool.

Separate from r_skipNewAmbient rather than folded into it, because that cvar
cannot reach these stages and is not meant to. Its exemption is
`sort < SS_POST_PROCESS`, and Material.cpp:3217 forces SS_POST_PROCESS onto any
material whose program samples a scene-capture image -- which every program
material Quake 4 places does. Measured on mp/q4xdm14: 5885 newStage stages, all
sort=100, none reachable by r_skipNewAmbient. Widening that cvar instead would
have made the same name mean two different things on two renderers.

The saving is mostly NOT the haze draws. It is the fullscreen _currentRender
copy they force: a material sorted at SS_POST_PROCESS stops the surface walk
until that copy exists. So when this is on, a material whose ONLY use of the
screen copy is through program stages stops requesting it -- see
GLESD3_MaterialCopyIsProgramOnly. Skipping the draws while still taking the copy
would leave most of the cost on the table.
====================
*/
static idCVar r_glesD3SkipMaterialPrograms( "r_glesD3SkipMaterialPrograms", "0", CVAR_RENDERER | CVAR_BOOL | CVAR_ARCHIVE,
		"gles_d3: skip material-program stages (heat haze, bumpy environment, monochrome) and the screen copy they would force" );


static void GLESD3_StepError( const char *step, const idMaterial *material ) {
	if ( r_glesD3Report.GetInteger() < 2 || gles_stepErrorReported ) {
		return;
	}
	const GLenum err = glGetError();
	if ( err == GL_NO_ERROR ) {
		return;
	}
	gles_stepErrorReported = true;
	common->Printf( "gles_d3 step '%s' raised 0x%04x on '%s'\n", step, err,
			material != NULL ? material->GetName() : "?" );
}

static void GLESD3_RecordSkip( glesD3SkipReason_t reason, const idMaterial *material ) {
	gles_skipCounts[ reason ]++;
	if ( material == NULL ) {
		return;
	}
	const char *name = material->GetName();
	for ( int i = 0; i < gles_skipNameCounts[ reason ]; i++ ) {
		if ( idStr::Icmp( gles_skipNames[ reason ][ i ], name ) == 0 ) {
			return;
		}
	}
	if ( gles_skipNameCounts[ reason ] >= GLESD3_SKIP_NAMES ) {
		gles_skipNamesOverflow[ reason ]++;
		return;
	}
	idStr::Copynz( gles_skipNames[ reason ][ gles_skipNameCounts[ reason ] ], name,
			sizeof( gles_skipNames[ reason ][ 0 ] ) );
	gles_skipNameCounts[ reason ]++;
}

void R_GLESD3_ResetSkipCounts( void ) {
	memset( gles_skipCounts, 0, sizeof( gles_skipCounts ) );
	memset( gles_skipNames, 0, sizeof( gles_skipNames ) );
	memset( gles_skipNameCounts, 0, sizeof( gles_skipNameCounts ) );
	memset( gles_skipNamesOverflow, 0, sizeof( gles_skipNamesOverflow ) );
}

void R_GLESD3_ReportSkipCounts( void ) {
	for ( int i = 0; i < GLESD3_SKIP_COUNT; i++ ) {
		if ( gles_skipCounts[ i ] == 0 ) {
			continue;
		}
		idStr line = va( "gles_d3 skip %s=%i:", gles_skipReasonNames[ i ], gles_skipCounts[ i ] );
		for ( int n = 0; n < gles_skipNameCounts[ i ]; n++ ) {
			line += va( " %s", gles_skipNames[ i ][ n ] );
		}
		if ( gles_skipNamesOverflow[ i ] > 0 ) {
			line += va( " (+%i more distinct)", gles_skipNamesOverflow[ i ] );
		}
		common->Printf( "%s\n", line.c_str() );
	}
}

/*
===============================================================================
	_currentRender capture -- the D7b prerequisite.

	RB_CaptureCurrentRenderImage and the two RB_StageUsesCurrent* predicates
	(draw_common.cpp:41, :75, :1518), which this module excludes.

	Without this, every SS_POST_PROCESS surface in a view is dropped: the
	material pass stops at the first one and never resumes, because nothing
	ever sets backEnd.currentRenderCopied. That is where the heatHaze,
	glasswarp and refraction families all go.

	The copy itself is idImage::CopyFramebuffer, which is shared. Its non-blit
	branch had to grow an ES case: _currentRender is FMT_RGBA16F, and ES 3.0
	refuses to copy the fixed-point default framebuffer into a float texture,
	so on ES the image is respecified as RGBA8 before the copy.

	_currentDepth is deliberately NOT captured here. ES has no legal
	glCopyTexSubImage2D into a depth texture, so it needs a depth-attachment
	blit that this backend does not have yet; the soft-particle and
	depth-fade consumers stay unimplemented and are counted rather than
	silently wrong.
===============================================================================
*/
static bool GLESD3_ImageIsCurrentRender( const idImage *image ) {
	if ( image == NULL ) {
		return false;
	}
	if ( image == globalImages->currentRenderImage
			|| image == globalImages->originalCurrentRenderImage ) {
		return true;
	}
	const char *name = image->GetName();
	return name != NULL && idStr::Icmpn( name, "_currentRender", 14 ) == 0;
}

static bool GLESD3_StageUsesCurrentRender( const shaderStage_t *stage ) {
	if ( stage == NULL ) {
		return false;
	}
	if ( GLESD3_ImageIsCurrentRender( stage->texture.image ) ) {
		return true;
	}
	const newShaderStage_t *newStage = stage->newStage;
	if ( newStage == NULL ) {
		return false;
	}
	for ( int i = 0; i < newStage->numFragmentProgramImages; i++ ) {
		if ( GLESD3_ImageIsCurrentRender( newStage->fragmentProgramImages[i] ) ) {
			return true;
		}
	}
	for ( int i = 0; i < newStage->numShaderTextures; i++ ) {
		if ( GLESD3_ImageIsCurrentRender( newStage->shaderTextureImages[i] ) ) {
			return true;
		}
	}
	return false;
}

/*
====================
GLESD3_ProgramStageDisabled

customLighting stages are excluded: the interaction pass owns those, so this
pass declining them is already correct and already counted, and letting this
cvar short-circuit them would only hide that from the counter.
====================
*/
static bool GLESD3_ProgramStageDisabled( const shaderStage_t *stage ) {
	return stage != NULL
			&& stage->newStage != NULL
			&& !stage->newStage->customLighting
			&& r_glesD3SkipMaterialPrograms.GetBool();
}

/*
====================
GLESD3_MaterialCopyIsProgramOnly

True when this material wants the screen copy solely to feed stages that
r_glesD3SkipMaterialPrograms is about to skip -- so with the cvar on, the copy
is pure cost and the material can be walked without one.

Deliberately conservative: one ordinary stage sampling _currentRender is enough
to answer false and keep the copy. Glass with both a plain stage and a haze
stage answers true, because only the haze stage reads the copy; the plain stage
still draws, and it never needed it.
====================
*/
static bool GLESD3_MaterialCopyIsProgramOnly( const idMaterial *shader ) {
	if ( shader == NULL || !r_glesD3SkipMaterialPrograms.GetBool() ) {
		return false;
	}
	bool anyCopyUser = false;
	const int stageCount = shader->GetNumStages();
	for ( int i = 0; i < stageCount; i++ ) {
		const shaderStage_t *pStage = shader->GetStage( i );
		if ( !GLESD3_StageUsesCurrentRender( pStage ) ) {
			continue;
		}
		if ( !GLESD3_ProgramStageDisabled( pStage ) ) {
			return false;
		}
		anyCopyUser = true;
	}
	return anyCopyUser;
}

/*
====================
GLESD3_CaptureAllowed

RB_AutomaticCurrentRenderCaptureAllowed (draw_common.cpp:1495) as far as it is
reachable. Its rbSceneRenderTexture is that path's own MSAA scene target and is
never created under BE_GLES_D3, so what is left is: capture when there is no
render target at all, when it is the feedback target, or when this is a 3D
view -- Quake 4 renders the scene into a game-owned render texture, and
capturing from that IS the point.

A GUI-to-texture subview is excluded on purpose. Capturing there would
overwrite _currentRender with panel content that the real view then samples.
====================
*/
static bool GLESD3_CaptureAllowed( void ) {
	if ( backEnd.renderTexture == NULL ) {
		return true;
	}
	if ( backEnd.renderTexture == backEnd.feedbackRenderTexture ) {
		return true;
	}
	return backEnd.viewDef != NULL && backEnd.viewDef->viewEntitys != NULL;
}

/*
====================
GLESD3_VerifyCurrentRenderContents

"No GL error" is not "the scene is in the texture" -- the copy that this
replaced returned no error on desktop while doing nothing useful on ES, and
there is no shipped material that samples _currentRender through a plain stage,
so nothing in a normal frame would show a stale capture until D7c lands the
GLSL material programs. This reads one pixel back out of the image and prints
it next to the same pixel of the framebuffer it was copied from.

Once per run, under r_glesD3Report >= 2 only: it is a full pipeline stall.
====================
*/
static void GLESD3_VerifyCurrentRenderContents( idImage *sceneImage, int width, int height ) {
	static bool verified = false;
	if ( verified ) {
		return;
	}
	verified = true;

	GLint previousReadFbo = 0;
	glGetIntegerv( GL_READ_FRAMEBUFFER_BINDING, &previousReadFbo );

	// sample away from the edges, where a letterboxed or scissored view is
	// most likely to be legitimately black in both buffers and prove nothing
	const int sampleX = backEnd.viewDef->viewport.x1 + width / 2;
	const int sampleY = backEnd.viewDef->viewport.y1 + height / 2;

	byte fromFramebuffer[4] = { 0, 0, 0, 0 };
	glBindFramebuffer( GL_READ_FRAMEBUFFER, 0 );
	glReadPixels( sampleX, sampleY, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, fromFramebuffer );

	byte fromImage[4] = { 0, 0, 0, 0 };
	GLuint probeFbo = 0;
	glGenFramebuffers( 1, &probeFbo );
	glBindFramebuffer( GL_READ_FRAMEBUFFER, probeFbo );
	glFramebufferTexture2D( GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneImage->GetDeviceHandle(), 0 );
	const GLenum status = glCheckFramebufferStatus( GL_READ_FRAMEBUFFER );
	if ( status == GL_FRAMEBUFFER_COMPLETE ) {
		glReadPixels( width / 2, height / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, fromImage );
	}
	glFramebufferTexture2D( GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0 );
	glBindFramebuffer( GL_READ_FRAMEBUFFER, previousReadFbo );
	glDeleteFramebuffers( 1, &probeFbo );

	if ( status != GL_FRAMEBUFFER_COMPLETE ) {
		common->Printf( "glesd3 _currentRender verify: not readable, fbo status 0x%04x\n", status );
		return;
	}
	common->Printf( "glesd3 _currentRender verify: framebuffer %i %i %i %i, image %i %i %i %i\n",
			fromFramebuffer[0], fromFramebuffer[1], fromFramebuffer[2], fromFramebuffer[3],
			fromImage[0], fromImage[1], fromImage[2], fromImage[3] );

	backEnd.glState.forceGlState = true;
}

void R_GLESD3_CaptureCurrentRender( void ) {
	if ( backEnd.currentRenderCopied || backEnd.viewDef == NULL ) {
		return;
	}
	if ( !GLESD3_CaptureAllowed() ) {
		// an offscreen pass owns its own copy; saying the copy exists is what
		// lets the post-process surfaces in this view draw at all
		backEnd.currentRenderCopied = true;
		return;
	}

	idImage *sceneImage = globalImages->currentRenderImage;
	const int width = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
	const int height = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;
	if ( sceneImage == NULL || width <= 0 || height <= 0 ) {
		return;
	}

	// already rendering INTO the image the copy would produce
	if ( backEnd.renderTexture != NULL && backEnd.renderTexture->GetNumColorImages() > 0
			&& backEnd.renderTexture->GetColorImage( 0 ) == sceneImage ) {
		backEnd.currentRenderCopied = true;
		return;
	}

	const bool probe = ( r_glesD3Report.GetInteger() >= 2 );
	if ( probe ) {
		( void )glGetError();	// drain, so the probe below attributes correctly
	}

	sceneImage->CopyFramebuffer( backEnd.viewDef->viewport.x1, backEnd.viewDef->viewport.y1,
			width, height );
	backEnd.currentRenderCopied = true;

	// Kept as a probe rather than removed: this copy failed silently for the
	// whole of D7b's first half (GL_INVALID_OPERATION then GL_INVALID_VALUE on
	// every call, because _currentRender is FMT_RGBA16F and ES will not copy the
	// fixed-point back buffer into a float texture), and nothing downstream
	// notices a stale capture except by looking wrong.
	if ( probe ) {
		const GLenum err = glGetError();
		if ( err != GL_NO_ERROR ) {
			common->Printf( "glesd3 _currentRender %ix%i raised 0x%04x\n", width, height, err );
		} else {
			common->Printf( "glesd3 _currentRender captured %ix%i\n", width, height );
		}
		GLESD3_VerifyCurrentRenderContents( sceneImage, width, height );
	}
}

/*
====================
GLESD3_BindStageImage

RB_BindVariableStageImage (tr_render.cpp:553), which this module excludes.
The cinematic half is not optional: the menu background and every briefing
screen is a cinematic stage.
====================
*/
static void GLESD3_BindStageImage( const textureStage_t *texture, const float *shaderRegisters ) {
	( void )shaderRegisters;
	if ( texture->cinematic ) {
		if ( r_skipDynamicTextures.GetBool() ) {
			globalImages->defaultImage->Bind();
			return;
		}
		cinData_t cin = texture->cinematic->ImageForTime(
				(int)( 1000 * ( backEnd.viewDef->floatTime + backEnd.viewDef->renderView.shaderParms[11] ) ) );
		if ( cin.image ) {
			globalImages->cinematicImage->UploadScratch( cin.image, cin.imageWidth, cin.imageHeight );
		} else {
			globalImages->blackImage->Bind();
		}
		return;
	}
	if ( texture->image ) {
		texture->image->Bind();
	}
}

/*
====================
GLESD3_StageTextureMatrix

The stage texture matrix as two rows against (s, t, 0, 1), matching
R_SetDrawInteraction (tr_render.cpp:782) including its wrap of large
translations -- scrolls otherwise accumulate texture coordinates large enough
to lose precision in a mediump-capable fragment stage.

Identity is written when the stage has no matrix. That is not a
micro-optimisation to skip: GL zero-initialises unset uniforms, and a zero
matrix collapses every texture coordinate onto a single texel. The ModernGL
path shipped exactly that bug (G6l).
====================
*/
static void GLESD3_StageTextureMatrix( const textureStage_t *texture, const float *regs,
		idVec4 &matrixS, idVec4 &matrixT ) {
	if ( !texture->hasMatrix ) {
		matrixS.Set( 1.0f, 0.0f, 0.0f, 0.0f );
		matrixT.Set( 0.0f, 1.0f, 0.0f, 0.0f );
		return;
	}

	matrixS[0] = regs[ texture->matrix[0][0] ];
	matrixS[1] = regs[ texture->matrix[0][1] ];
	matrixS[2] = 0.0f;
	matrixS[3] = regs[ texture->matrix[0][2] ];

	matrixT[0] = regs[ texture->matrix[1][0] ];
	matrixT[1] = regs[ texture->matrix[1][1] ];
	matrixT[2] = 0.0f;
	matrixT[3] = regs[ texture->matrix[1][2] ];

	if ( matrixS[3] < -40.0f || matrixS[3] > 40.0f ) {
		matrixS[3] -= (int)matrixS[3];
	}
	if ( matrixT[3] < -40.0f || matrixT[3] > 40.0f ) {
		matrixT[3] -= (int)matrixT[3];
	}
}

/*
====================
GLESD3_VertexColorPacking

(rgbModulate, rgbAdd, alphaModulate, alphaAdd), applied in the shader as
`vColor * modulate + add` with the RGB and alpha terms taken separately.
Reproduces the fixed-function TexEnv COMBINE configuration the legacy path
builds per stage, and matches the Vulkan interaction shader's packing.

RGB and alpha are NOT the same term, which is why this is a vec4 and not the
vec2 it started as. SVC_INVERSE_MODULATE configures only the RGB combiner
(draw_common.cpp:7338-7352): COMBINE_RGB = MODULATE, SOURCE1_RGB =
PRIMARY_COLOR with OPERAND1_RGB = ONE_MINUS_SRC_COLOR. The ALPHA combiner is
left at the GL_COMBINE default -- MODULATE of GL_TEXTURE against GL_PREVIOUS,
which at unit 0 is the primary colour -- so alpha comes out `texA * vertA`,
NOT inverted. Applying the inversion to all four channels zeroes the alpha of
every SVC_INVERSE_MODULATE stage whose vertex alpha is 255, which is every
`*_to_smolder` material in the game: their first stage writes an alpha mask
(maskcolor) that the second blends through GL_DST_ALPHA, so a zeroed mask
removes the overlay entirely.
====================
*/
static void GLESD3_VertexColorPacking( stageVertexColor_t vertexColor, float packing[4] ) {
	switch ( vertexColor ) {
		case SVC_MODULATE:
			packing[0] = 1.0f;
			packing[1] = 0.0f;
			packing[2] = 1.0f;
			packing[3] = 0.0f;
			break;
		case SVC_INVERSE_MODULATE:
			packing[0] = -1.0f;
			packing[1] = 1.0f;
			// alpha modulates, it does not invert -- see above
			packing[2] = 1.0f;
			packing[3] = 0.0f;
			break;
		case SVC_IGNORE:
		default:
			packing[0] = 0.0f;
			packing[1] = 1.0f;
			packing[2] = 0.0f;
			packing[3] = 1.0f;
			break;
	}
}

/*
====================
GLESD3_BindStageVertexColor

Decal stages bake per-stage colour into their own cache with a stride; every
other stage reads idDrawVert::color. RB_SetStageVertexColorPointer
(draw_common.cpp:619) is the reference.
====================
*/
static void GLESD3_BindStageVertexColor( const drawSurf_t *surf, int stage, const void *ambientBase ) {
	if ( surf->decalColorCache != NULL && stage >= 0 && stage < surf->decalColorStageCount
			&& surf->decalColorStride > 0 ) {
		const void *colorData = vertexCache.Position( surf->decalColorCache );
		glVertexAttribPointer( GLESD3_ATTR_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0,
				RB_DrawVertAttributePointer( colorData,
						surf->decalColorOffset + stage * surf->decalColorStride ) );
		return;
	}
	glVertexAttribPointer( GLESD3_ATTR_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof( idDrawVert ),
			RB_DrawVertAttributePointer( ambientBase, offsetof( idDrawVert, color ) ) );
}

/*
===============================================================================
	Depth prepass.

	Planned as its own stage (D3), landed with the material pass because the
	two are one unit: idMaterial gives every opaque and perforated stage
	GLS_DEPTHFUNC_EQUAL (Material.cpp:3263), so without a filled depth buffer
	the entire opaque world fails its depth test and only the translucent
	stages -- which carry GLS_DEPTHFUNC_LESS -- appear. That is exactly what
	the first D2 frame showed: glows, sparks and a fluorescent tube over an
	empty clear.

	RB_T_FillDepthBuffer / RB_STD_FillDepthBuffer (draw_common.cpp:6213,
	:6414) are the reference.
===============================================================================
*/

/*
====================
RB_GLESD3_T_FillDepthBuffer
====================
*/
static void RB_GLESD3_T_FillDepthBuffer( const drawSurf_t *surf, glesProgram_t *program,
		glesProgram_t *alphaTestProgram ) {
	const srfTriangles_t *tri = surf->geo;
	const idMaterial *shader = surf->material;

	if ( shader == NULL || !shader->IsDrawn() ) {
		return;
	}
	if ( tri == NULL || tri->numIndexes == 0 ) {
		return;
	}
	// translucent surfaces write nothing to depth and do not test against it
	if ( shader->Coverage() == MC_TRANSLUCENT ) {
		return;
	}
	// DrawElements uploads CPU indices when the optional index cache is absent.
	// Multiplayer resets r_useIndexBuffers to its default, so requiring a cache
	// here would discard the depth prepass before that fallback can run.
	if ( tri->ambientCache == NULL || ( tri->indexCache == NULL && tri->indexes == NULL ) ) {
		GLESD3_RecordSkip( GLESD3_SKIP_NO_GEOMETRY, shader );
		return;
	}

	const float *regs = surf->shaderRegisters;
	const int stageCount = shader->GetNumStages();

	// a material with every stage conditioned off contributes no depth
	int stage;
	for ( stage = 0; stage < stageCount; stage++ ) {
		if ( regs[ shader->GetStage( stage )->conditionRegister ] != 0 ) {
			break;
		}
	}
	if ( stage == stageCount ) {
		return;
	}

	if ( surf->space != backEnd.currentSpace ) {
		backEnd.currentSpace = surf->space;
	}
	float mvp[ 16 ];
	myGlMultMatrix( surf->space->modelViewMatrix, backEnd.viewDef->projectionMatrix, mvp );

	// an empty rect covers no pixels: skip rather than issue a rejected call
	if ( !RB_GLESD3_SetScissor( surf->scissorRect ) ) {
		return;
	}

	if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glEnable( GL_POLYGON_OFFSET_FILL );
		glPolygonOffset( r_offsetFactor.GetFloat(),
				r_offsetUnits.GetFloat() * shader->GetPolygonOffset() );
	}

	// r_glesD3DepthFillColor 0 masks the depth pass's colour write. The pass
	// normally paints black, which is correct (it is how the colour buffer
	// gets cleared behind opaque geometry) but makes "this surface was never
	// lit or shaded" indistinguishable from "this surface drew black". With
	// the write masked, anything neither lit nor shaded shows
	// r_glesD3DebugClearColor instead.
	if ( !r_glesD3DepthFillColor.GetBool() ) {
		GL_State( GLS_COLORMASK | GLS_ALPHAMASK | GLS_DEPTHFUNC_LESS );
	}

	float color[ 4 ];
	if ( shader->GetSort() == SS_SUBVIEW ) {
		// subviews down-modulate the colour buffer by overbright instead of
		// painting black
		GL_State( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO | GLS_DEPTHFUNC_LESS );
		color[0] = color[1] = color[2] = 1.0f / backEnd.overBright;
		color[3] = 1.0f;
	} else {
		color[0] = color[1] = color[2] = 0.0f;
		color[3] = 1.0f;
	}

	if ( !R_GLESD3_BindDrawVertAttributes( tri ) ) {
		GLESD3_RecordSkip( GLESD3_SKIP_NO_GEOMETRY, shader );
		return;
	}

	bool drawSolid = ( shader->Coverage() == MC_OPAQUE );

	if ( shader->Coverage() == MC_PERFORATED && alphaTestProgram != NULL ) {
		// Perforated surfaces carry their cutout in one or more alpha-tested
		// stages; each is drawn so the depth buffer gets the holes right.
		// Only these draws bind the ALPHATEST variant -- solid fills below use
		// the discard-free base program, which is what lets a tiler keep its
		// early-Z pipeline for the bulk of the prepass (gles_program.h, D8).
		bool didDraw = false;
		R_GLESD3_UseProgram( alphaTestProgram );
		glUniformMatrix4fv( alphaTestProgram->uMVP, 1, GL_FALSE, mvp );
		// vertex colour plays no part in the depth fill
		const float perforatedVertexColor[ 4 ] = { 0.0f, 1.0f, 0.0f, 1.0f };
		glUniform4fv( alphaTestProgram->uVertexColor, 1, perforatedVertexColor );
		for ( stage = 0; stage < stageCount; stage++ ) {
			const shaderStage_t *pStage = shader->GetStage( stage );
			if ( !pStage->hasAlphaTest || regs[ pStage->conditionRegister ] == 0 ) {
				continue;
			}
			didDraw = true;

			color[3] = regs[ pStage->color.registers[3] ];
			if ( color[3] <= 0.0f ) {
				continue;
			}
			if ( pStage->texture.texgen != TG_EXPLICIT ) {
				GLESD3_RecordSkip( GLESD3_SKIP_TEXGEN, shader );
				continue;
			}

			idVec4 matrixS, matrixT;
			GLESD3_StageTextureMatrix( &pStage->texture, regs, matrixS, matrixT );

			GL_SelectTexture( 0 );
			if ( pStage->texture.image != NULL ) {
				pStage->texture.image->Bind();
			}

			glUniform4fv( alphaTestProgram->uTexMatrixS, 1, matrixS.ToFloatPtr() );
			glUniform4fv( alphaTestProgram->uTexMatrixT, 1, matrixT.ToFloatPtr() );
			glUniform4fv( alphaTestProgram->uColor, 1, color );
			// the stage's own reference, not the GLS_ATEST buckets: a
			// perforated stage carries an arbitrary alphaTestRegister
			glUniform1f( alphaTestProgram->uAlphaTest, regs[ pStage->alphaTestRegister ] );

			R_GLESD3_DrawElements( tri );
		}
		if ( !didDraw ) {
			drawSolid = true;
		}
	} else if ( shader->Coverage() == MC_PERFORATED ) {
		// the ALPHATEST variant failed to build: fill solid rather than not at
		// all, accepting wrong depth in the cutouts over holes in the prepass
		drawSolid = true;
	}

	if ( drawSolid ) {
		const idVec4 identityS( 1.0f, 0.0f, 0.0f, 0.0f );
		const idVec4 identityT( 0.0f, 1.0f, 0.0f, 0.0f );
		R_GLESD3_UseProgram( program );
		glUniformMatrix4fv( program->uMVP, 1, GL_FALSE, mvp );
		const float ignoreVertexColor[ 4 ] = { 0.0f, 1.0f, 0.0f, 1.0f };
		glUniform4fv( program->uVertexColor, 1, ignoreVertexColor );
		GL_SelectTexture( 0 );
		globalImages->whiteImage->Bind();
		glUniform4fv( program->uTexMatrixS, 1, identityS.ToFloatPtr() );
		glUniform4fv( program->uTexMatrixT, 1, identityT.ToFloatPtr() );
		glUniform4fv( program->uColor, 1, color );

		R_GLESD3_DrawElements( tri );
	}

	if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}
	if ( shader->GetSort() == SS_SUBVIEW ) {
		GL_State( GLS_DEPTHFUNC_LESS );
	}
}

/*
====================
RB_GLESD3_FillDepthBuffer
====================
*/
void RB_GLESD3_FillDepthBuffer( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	if ( !backEnd.viewDef->viewEntitys ) {
		// 2D views have no depth buffer to fill
		return;
	}
	if ( drawSurfs == NULL || numDrawSurfs <= 0 ) {
		return;
	}

	glesProgram_t *program = R_GLESD3_Program( GLESD3_PROGRAM_MATERIAL );
	glesProgram_t *alphaTestProgram = R_GLESD3_Program( GLESD3_PROGRAM_MATERIAL,
			GLESD3_VARIANT_ALPHATEST );
	if ( program == NULL ) {
		return;
	}

	RB_LogComment( "---------- RB_GLESD3_FillDepthBuffer ----------\n" );

	glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() );
	GL_State( GLS_DEPTHFUNC_LESS );

	// armed here rather than in the shadow pass: without it the ambient and
	// light passes are free to z-fight against each other
	glEnable( GL_STENCIL_TEST );
	glStencilFunc( GL_ALWAYS, 1, 255 );

	backEnd.currentSpace = NULL;
	for ( int i = 0; i < numDrawSurfs; i++ ) {
		if ( drawSurfs[i]->material == NULL || drawSurfs[i]->material->SuppressInSubview() ) {
			continue;
		}
		RB_GLESD3_T_FillDepthBuffer( drawSurfs[i], program, alphaTestProgram );
	}
}

/*
====================
GLESD3_CubeTexgenProgramId

Which program a cube texgen stage draws with, or GLESD3_PROGRAM_COUNT when the
texgen is not one this backend implements.

The TG_REFLECT_CUBE split is not a stage property: ARB2 picks bumpyEnvironment
over environment on whether the MATERIAL carries a bump stage
(draw_common.cpp:4846), and reproducing that is what makes a reflective surface
with a normal map look the same on both. 140 of the game's material stages are
`texgen reflect`, more than every ARB material program put together.
====================
*/
static glesD3ProgramId_t GLESD3_CubeTexgenProgramId( const idMaterial *shader, texgen_t texgen,
		const shaderStage_t **bumpStageOut ) {
	*bumpStageOut = NULL;
	switch ( texgen ) {
		case TG_SKYBOX_CUBE:
		case TG_WOBBLESKY_CUBE:
		case TG_DIFFUSE_CUBE:
			return GLESD3_PROGRAM_CUBEMAP;
		case TG_REFLECT_CUBE: {
			const shaderStage_t *bump = shader->GetBumpStage();
			if ( bump != NULL && bump->texture.image != NULL ) {
				*bumpStageOut = bump;
				return GLESD3_PROGRAM_BUMPY_ENVIRONMENT;
			}
			return GLESD3_PROGRAM_ENVIRONMENT;
		}
		default:
			return GLESD3_PROGRAM_COUNT;
	}
}

/*
====================
GLESD3_DrawCubeTexgenStage

One cube-map texgen stage. Returns false when the stage cannot be drawn, and
the caller counts that as a texgen skip -- the same accounting the blanket
"texgen != TG_EXPLICIT" skip used to do for all of them.

The cube map binds to unit 0 as a samplerCube. That is why no program declares
both uTexture0 and uCubeMap: on ES a single texture unit may not be sampled as
two different types by one program, and the validation failure is a link-time
error on some drivers and a black draw on others.
====================
*/
static bool GLESD3_DrawCubeTexgenStage( const drawSurf_t *surf, const shaderStage_t *pStage,
		const float mvp[16], const float color[4], const float vertexColorPacking[4] ) {
	const idMaterial *shader = surf->material;
	const srfTriangles_t *tri = surf->geo;
	const shaderStage_t *bumpStage = NULL;

	const glesD3ProgramId_t id =
			GLESD3_CubeTexgenProgramId( shader, pStage->texture.texgen, &bumpStage );
	if ( id == GLESD3_PROGRAM_COUNT ) {
		return false;
	}
	// an alpha-tested stage binds the ALPHATEST variant; everything else stays
	// on the discard-free base program (gles_program.h, D8)
	const bool alphaTested =
			R_GLESD3_AlphaTestReference( pStage->drawStateBits ) >= 0.0f;
	glesProgram_t *program = R_GLESD3_Program( id,
			alphaTested ? GLESD3_VARIANT_ALPHATEST : GLESD3_VARIANT_BASE );
	if ( program == NULL || pStage->texture.image == NULL ) {
		return false;
	}

	if ( id == GLESD3_PROGRAM_CUBEMAP ) {
		// skybox and wobblesky read the front end's stream; diffuse cube reads
		// the idDrawVert normal
		const bool fromNormal = ( pStage->texture.texgen == TG_DIFFUSE_CUBE );
		if ( !R_GLESD3_BindCubeTexDirAttribute( surf, fromNormal ) ) {
			return false;
		}
	}

	R_GLESD3_UseProgram( program );
	glUniformMatrix4fv( program->uMVP, 1, GL_FALSE, mvp );

	if ( program->uLocalViewOrigin >= 0 ) {
		idVec4 localViewOrigin;
		R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg,
				localViewOrigin.ToVec3() );
		localViewOrigin.w = 1.0f;
		glUniform4fv( program->uLocalViewOrigin, 1, localViewOrigin.ToFloatPtr() );
	}

	if ( program->uModelRow0 >= 0 ) {
		// bumpyEnvironment reflects in GLOBAL space, so it needs the model
		// rows the stock ARB program gets in program.env[6..8]. The layout
		// matches R_LocalVectorToGlobal: global.x = dot( local, (m0, m4, m8) ).
		const float *m = surf->space->modelMatrix;
		const float row0[4] = { m[0], m[4], m[8],  m[12] };
		const float row1[4] = { m[1], m[5], m[9],  m[13] };
		const float row2[4] = { m[2], m[6], m[10], m[14] };
		glUniform4fv( program->uModelRow0, 1, row0 );
		glUniform4fv( program->uModelRow1, 1, row1 );
		glUniform4fv( program->uModelRow2, 1, row2 );
	}

	if ( program->uColor >= 0 ) {
		glUniform4fv( program->uColor, 1, color );
	}
	if ( program->uVertexColor >= 0 ) {
		glUniform4fv( program->uVertexColor, 1, vertexColorPacking );
	}
	// the STAGE's bits, not the cached ones: GL_State for this stage does not
	// run until below, so reading backEnd.glState here would arm the previous
	// stage's alpha test
	if ( program->uAlphaTest >= 0 ) {
		glUniform1f( program->uAlphaTest,
				R_GLESD3_AlphaTestReference( pStage->drawStateBits ) );
	}

	GL_SelectTexture( 0 );
	pStage->texture.image->Bind();
	if ( bumpStage != NULL ) {
		GL_SelectTexture( 1 );
		bumpStage->texture.image->Bind();
		GL_SelectTexture( 0 );
	}

	GL_State( pStage->drawStateBits );
	R_GLESD3_DrawElements( tri );

	if ( bumpStage != NULL ) {
		GL_SelectTexture( 1 );
		globalImages->BindNull();
		GL_SelectTexture( 0 );
	}
	if ( id == GLESD3_PROGRAM_CUBEMAP ) {
		R_GLESD3_DisableCubeTexDirAttribute();
	}
	return true;
}

/*
===============================================================================
	Authored material programs -- D7c.

	A `newStage` is a stage whose material named a vertex/fragment program
	pair. On ES there is no ARB assembly to run, so each family this backend
	supports has a hand-written GLSL ES equivalent; the rest stay counted as
	GLESD3_SKIP_NEWSTAGE.

	The shipped archives reference six program names across pak0-pak022, and
	25 of the 29 references are the heatHaze family -- which is why it is
	first. The names come back from R_GLES_MaterialProgramName: the parser
	stores only a handle, so the name a material author wrote is otherwise
	unrecoverable at draw time.

	Deliberately matched to the Vulkan implementation
	(vk_GuiExecutor.cpp:5820), down to the parms slot numbering, so a
	side-by-side against ./run_vulkan.sh compares two ports of one program
	rather than two independent guesses.
===============================================================================
*/
typedef enum {
	GLESD3_MATPROG_NONE = 0,
	GLESD3_MATPROG_HEATHAZE,
	GLESD3_MATPROG_HEATHAZE_MASK,
	GLESD3_MATPROG_HEATHAZE_MASK_VERTEX,
	GLESD3_MATPROG_BUMPY_ENVIRONMENT,
	GLESD3_MATPROG_MONOCHROME,
	GLESD3_MATPROG_COUNT
} glesD3MaterialProgram_t;

static glesD3MaterialProgram_t GLESD3_MaterialProgramFamily( const newShaderStage_t *newStage ) {
	if ( newStage == NULL ) {
		return GLESD3_MATPROG_NONE;
	}
	const char *name = R_GLES_MaterialProgramName( newStage->fragmentProgram );
	if ( name == NULL ) {
		return GLESD3_MATPROG_NONE;
	}
	idStr base = name;
	base.StripFileExtension();

	if ( idStr::Icmp( base.c_str(), "heatHaze" ) == 0 ) {
		return GLESD3_MATPROG_HEATHAZE;
	}
	if ( idStr::Icmp( base.c_str(), "heatHazeWithMask" ) == 0 ) {
		return GLESD3_MATPROG_HEATHAZE_MASK;
	}
	// No program by this name exists in the shipped archives -- the material
	// reference is dangling. The ordinary mask contract is the nearest
	// stock-defined behaviour, and it is what Vulkan resolves it to.
	if ( idStr::Icmp( base.c_str(), "heatHazeGrayWithMask" ) == 0 ) {
		return GLESD3_MATPROG_HEATHAZE_MASK;
	}
	if ( idStr::Icmp( base.c_str(), "heatHazeWithMaskAndVertex" ) == 0 ) {
		return GLESD3_MATPROG_HEATHAZE_MASK_VERTEX;
	}
	if ( idStr::Icmp( base.c_str(), "bumpyEnvironment" ) == 0 ) {
		return GLESD3_MATPROG_BUMPY_ENVIRONMENT;
	}
	if ( idStr::Icmp( base.c_str(), "monochrome" ) == 0 ) {
		return GLESD3_MATPROG_MONOCHROME;
	}
	return GLESD3_MATPROG_NONE;
}

/*
====================
GLESD3_DrawBumpyEnvironmentStage

The explicitly-programmed form of bumpyEnvironment.vfp. D7a already ported that
program for TG_REFLECT_CUBE stages, where the renderer picks it and finds the
cube and bump images itself; here the material names the program and supplies
both as fragmentMap 0 and 1. Same shaders, different source of images.
====================
*/
static bool GLESD3_DrawBumpyEnvironmentStage( const drawSurf_t *surf, const shaderStage_t *pStage,
		const float mvp[16] ) {
	const newShaderStage_t *newStage = pStage->newStage;
	if ( newStage->numFragmentProgramImages < 2 || surf->space == NULL ) {
		return false;
	}
	idImage *cubeImage = newStage->fragmentProgramImages[ 0 ];
	idImage *normalImage = newStage->fragmentProgramImages[ 1 ];
	if ( cubeImage == NULL || normalImage == NULL ) {
		return false;
	}

	glesProgram_t *program = R_GLESD3_Program( GLESD3_PROGRAM_BUMPY_ENVIRONMENT,
			R_GLESD3_AlphaTestReference( pStage->drawStateBits ) >= 0.0f
				? GLESD3_VARIANT_ALPHATEST : GLESD3_VARIANT_BASE );
	if ( program == NULL ) {
		return false;
	}

	R_GLESD3_UseProgram( program );
	glUniformMatrix4fv( program->uMVP, 1, GL_FALSE, mvp );

	idVec4 localViewOrigin;
	R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg,
			localViewOrigin.ToVec3() );
	localViewOrigin.w = 1.0f;
	glUniform4fv( program->uLocalViewOrigin, 1, localViewOrigin.ToFloatPtr() );

	// program.env[6..8] in the stock program: the model matrix rows, laid out
	// to match R_LocalVectorToGlobal.
	const float *m = surf->space->modelMatrix;
	const float row0[4] = { m[0], m[4], m[8],  m[12] };
	const float row1[4] = { m[1], m[5], m[9],  m[13] };
	const float row2[4] = { m[2], m[6], m[10], m[14] };
	glUniform4fv( program->uModelRow0, 1, row0 );
	glUniform4fv( program->uModelRow1, 1, row1 );
	glUniform4fv( program->uModelRow2, 1, row2 );

	glUniform1f( program->uAlphaTest, R_GLESD3_AlphaTestReference( pStage->drawStateBits ) );

	GL_SelectTexture( 1 );
	normalImage->Bind();
	GL_SelectTexture( 0 );
	cubeImage->Bind();

	GL_State( pStage->drawStateBits );
	const bool drew = R_GLESD3_DrawElements( surf->geo );

	GL_SelectTexture( 1 );
	globalImages->BindNull();
	GL_SelectTexture( 0 );
	return drew;
}

/*
====================
GLESD3_DrawMonochromeStage
====================
*/
static bool GLESD3_DrawMonochromeStage( const drawSurf_t *surf, const shaderStage_t *pStage,
		const float mvp[16] ) {
	const newShaderStage_t *newStage = pStage->newStage;
	const float *regs = surf->shaderRegisters;
	if ( newStage->numFragmentProgramImages < 1
			|| newStage->fragmentProgramImages[ 0 ] == NULL ) {
		return false;
	}

	glesProgram_t *program = R_GLESD3_Program( GLESD3_PROGRAM_MONOCHROME,
			R_GLESD3_AlphaTestReference( pStage->drawStateBits ) >= 0.0f
				? GLESD3_VARIANT_ALPHATEST : GLESD3_VARIANT_BASE );
	if ( program == NULL ) {
		return false;
	}

	R_GLESD3_UseProgram( program );
	glUniformMatrix4fv( program->uMVP, 1, GL_FALSE, mvp );

	float color[ 4 ] = { 1.0f, 1.0f, 1.0f, 1.0f };
	if ( regs != NULL ) {
		for ( int i = 0; i < 4; i++ ) {
			color[ i ] = regs[ pStage->color.registers[ i ] ];
		}
	}
	glUniform4fv( program->uColor, 1, color );
	glUniform1f( program->uAlphaTest, R_GLESD3_AlphaTestReference( pStage->drawStateBits ) );

	GL_SelectTexture( 0 );
	newStage->fragmentProgramImages[ 0 ]->Bind();

	GL_State( pStage->drawStateBits );
	return R_GLESD3_DrawElements( surf->geo );
}

/*
====================
GLESD3_DrawHeatHazeStage

Returns false without drawing if anything it needs is missing, so the caller
counts the stage as a skip rather than reporting a draw that did not happen.
====================
*/
static bool GLESD3_DrawHeatHazeStage( const drawSurf_t *surf, const shaderStage_t *pStage,
		const float mvp[16], glesD3MaterialProgram_t family ) {
	const srfTriangles_t *tri = surf->geo;
	const newShaderStage_t *newStage = pStage->newStage;
	const float *regs = surf->shaderRegisters;

	const bool masked = ( family != GLESD3_MATPROG_HEATHAZE );
	const int numTextures = masked ? 3 : 2;
	if ( newStage->numFragmentProgramImages < numTextures || surf->space == NULL ) {
		return false;
	}
	for ( int i = 0; i < numTextures; i++ ) {
		if ( newStage->fragmentProgramImages[ i ] == NULL ) {
			return false;
		}
	}

	glesProgram_t *program = R_GLESD3_Program(
			masked ? GLESD3_PROGRAM_HEATHAZE_MASK : GLESD3_PROGRAM_HEATHAZE,
			R_GLESD3_AlphaTestReference( pStage->drawStateBits ) >= 0.0f
				? GLESD3_VARIANT_ALPHATEST : GLESD3_VARIANT_BASE );
	if ( program == NULL ) {
		return false;
	}

	// Slot numbering is the shared contract with Vulkan/shaders/heathaze.*;
	// see the StageParms comment there.
	float parms[ 8 ][ 4 ];
	memset( parms, 0, sizeof( parms ) );
	for ( int i = 0; i < newStage->numVertexParms && i < 2; i++ ) {
		for ( int j = 0; j < 4; j++ ) {
			parms[ i ][ j ] = regs != NULL ? regs[ newStage->vertexParms[ i ][ j ] ] : 0.0f;
		}
	}

	// The ARB program reads these as state.matrix rows, which no longer exist.
	// Column-major storage means row 2 of the modelview is elements 2, 6, 10,
	// 14 -- not 8..11.
	const float *modelView = surf->space->modelViewMatrix;
	const float *projection = backEnd.viewDef->projectionMatrix;
	parms[ 2 ][ 0 ] = modelView[ 2 ];
	parms[ 2 ][ 1 ] = modelView[ 6 ];
	parms[ 2 ][ 2 ] = modelView[ 10 ];
	parms[ 2 ][ 3 ] = modelView[ 14 ];
	parms[ 3 ][ 0 ] = projection[ 0 ];
	parms[ 3 ][ 1 ] = projection[ 4 ];
	parms[ 3 ][ 2 ] = projection[ 8 ];
	parms[ 3 ][ 3 ] = projection[ 12 ];
	parms[ 4 ][ 0 ] = projection[ 3 ];
	parms[ 4 ][ 1 ] = projection[ 7 ];
	parms[ 4 ][ 2 ] = projection[ 11 ];
	parms[ 4 ][ 3 ] = projection[ 15 ];

	const int viewportWidth = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
	const int viewportHeight = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;
	if ( viewportWidth <= 0 || viewportHeight <= 0 ) {
		return false;
	}
	// RB_SetProgramEnvironment's env[0]/env[1] (draw_common.cpp:6795). This
	// backend copies exactly the viewport into _currentRender, so the scale is
	// 1 today -- computed rather than assumed, because the moment the capture
	// gains a power-of-two or downsampled target it stops being 1 and the
	// failure would be a silent half-screen offset.
	int textureWidth = viewportWidth;
	int textureHeight = viewportHeight;
	if ( globalImages->currentRenderImage != NULL ) {
		if ( globalImages->currentRenderImage->GetOpts().width > 0 ) {
			textureWidth = globalImages->currentRenderImage->GetOpts().width;
		}
		if ( globalImages->currentRenderImage->GetOpts().height > 0 ) {
			textureHeight = globalImages->currentRenderImage->GetOpts().height;
		}
	}
	parms[ 5 ][ 0 ] = (float)viewportWidth / (float)textureWidth;
	parms[ 5 ][ 1 ] = (float)viewportHeight / (float)textureHeight;
	parms[ 5 ][ 3 ] = 1.0f;
	parms[ 6 ][ 0 ] = 1.0f / (float)viewportWidth;
	parms[ 6 ][ 1 ] = 1.0f / (float)viewportHeight;
	// The retail fragment program multiplies raw window coordinates by env[1],
	// which is only right for a viewport at the origin. Subtracting the
	// viewport origin costs nothing and makes subviews correct.
	parms[ 7 ][ 0 ] = (float)backEnd.viewDef->viewport.x1;
	parms[ 7 ][ 1 ] = (float)backEnd.viewDef->viewport.y1;
	parms[ 7 ][ 2 ] = ( family == GLESD3_MATPROG_HEATHAZE_MASK_VERTEX ) ? 1.0f : 0.0f;

	R_GLESD3_UseProgram( program );
	glUniformMatrix4fv( program->uMVP, 1, GL_FALSE, mvp );
	glUniform4fv( program->uParms, 8, &parms[ 0 ][ 0 ] );
	glUniform1f( program->uAlphaTest, R_GLESD3_AlphaTestReference( pStage->drawStateBits ) );

	for ( int i = 0; i < numTextures; i++ ) {
		GL_SelectTexture( i );
		newStage->fragmentProgramImages[ i ]->Bind();
	}
	GL_SelectTexture( 0 );

	GL_State( pStage->drawStateBits );
	const bool drew = R_GLESD3_DrawElements( tri );

	for ( int i = numTextures - 1; i >= 1; i-- ) {
		GL_SelectTexture( i );
		globalImages->BindNull();
	}
	GL_SelectTexture( 0 );

	return drew;
}

/*
====================
GLESD3_DrawMaterialProgramStage

One line per family per run. A material program that draws nothing is
indistinguishable from one that is skipped once the skip counter stops naming
it, and that is exactly the state D7b's first half was in.
====================
*/
static bool GLESD3_DrawMaterialProgramStage( const drawSurf_t *surf, const shaderStage_t *pStage,
		const float mvp[16], glesD3MaterialProgram_t family ) {
	bool drew = false;
	switch ( family ) {
		case GLESD3_MATPROG_HEATHAZE:
		case GLESD3_MATPROG_HEATHAZE_MASK:
		case GLESD3_MATPROG_HEATHAZE_MASK_VERTEX:
			drew = GLESD3_DrawHeatHazeStage( surf, pStage, mvp, family );
			break;
		case GLESD3_MATPROG_BUMPY_ENVIRONMENT:
			drew = GLESD3_DrawBumpyEnvironmentStage( surf, pStage, mvp );
			break;
		case GLESD3_MATPROG_MONOCHROME:
			drew = GLESD3_DrawMonochromeStage( surf, pStage, mvp );
			break;
		default:
			return false;
	}

	static bool loggedFamily[ GLESD3_MATPROG_COUNT ];
	if ( drew && !loggedFamily[ family ] ) {
		loggedFamily[ family ] = true;
		common->Printf( "gles_d3 material program family %i drew first on '%s'\n",
				(int)family, surf->material->GetName() );
	}
	return drew;
}

/*
====================
RB_GLESD3_T_RenderShaderPasses

One surface, one draw per eligible stage.
====================
*/
static void RB_GLESD3_T_RenderShaderPasses( const drawSurf_t *surf ) {
	const srfTriangles_t *tri = surf->geo;
	const idMaterial *shader = surf->material;

	if ( shader == NULL || shader->IsPortalSky() ) {
		return;
	}
	{
		const char *skipSubstring = r_glesD3SkipMaterial.GetString();
		if ( skipSubstring != NULL && skipSubstring[0] != '\0'
				&& idStr::FindText( shader->GetName(), skipSubstring, false ) >= 0 ) {
			return;
		}
	}
	if ( !shader->HasAmbient() ) {
		// Normally this is just a lit surface -- Quake 4 world materials carry
		// no ambient stage and are drawn entirely by the interaction pass, so
		// counting them would be noise. A material whose ONLY stages are custom
		// programs is different: it has no ambient stage because we cannot run
		// the program, and it renders nothing at all. Named here because the
		// stage loop below never sees it.
		const int stageCount = shader->GetNumStages();
		for ( int i = 0; i < stageCount; i++ ) {
			if ( shader->GetStage( i )->newStage != NULL ) {
				GLESD3_RecordSkip( GLESD3_SKIP_NEWSTAGE, shader );
				break;
			}
		}
		return;
	}
	if ( tri == NULL || tri->numIndexes == 0 ) {
		// some deforms disable themselves by setting numIndexes to 0
		return;
	}
	// Keep the same CPU-index fallback as depth and interaction draws.
	if ( tri->ambientCache == NULL || ( tri->indexCache == NULL && tri->indexes == NULL ) ) {
		GLESD3_RecordSkip( GLESD3_SKIP_NO_GEOMETRY, shader );
		return;
	}

	glesProgram_t *program = R_GLESD3_Program( GLESD3_PROGRAM_MATERIAL );
	// alpha-tested stages bind this variant; NULL degrades them to the base
	// program (no cutout) rather than dropping the surface
	glesProgram_t *alphaTestProgram = R_GLESD3_Program( GLESD3_PROGRAM_MATERIAL,
			GLESD3_VARIANT_ALPHATEST );
	if ( program == NULL ) {
		return;
	}

	// the modelview changes per space; the projection is fixed for the view
	if ( surf->space != backEnd.currentSpace ) {
		backEnd.currentSpace = surf->space;
	}
	float mvp[ 16 ];
	myGlMultMatrix( surf->space->modelViewMatrix, backEnd.viewDef->projectionMatrix, mvp );

	// an empty rect covers no pixels: skip rather than issue a rejected call
	if ( !RB_GLESD3_SetScissor( surf->scissorRect ) ) {
		return;
	}

	const float *regs = surf->shaderRegisters;

	GL_Cull( shader->GetCullType() );

	if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glEnable( GL_POLYGON_OFFSET_FILL );
		glPolygonOffset( r_offsetFactor.GetFloat(),
				r_offsetUnits.GetFloat() * shader->GetPolygonOffset() );
	}

	if ( !R_GLESD3_BindDrawVertAttributes( tri ) ) {
		GLESD3_RecordSkip( GLESD3_SKIP_NO_GEOMETRY, shader );
		return;
	}
	const void *ambientBase = vertexCache.Position( tri->ambientCache );

	// uniforms are per-program: both variants get this surface's MVP up front,
	// so the per-stage selection below only has to bind
	if ( alphaTestProgram != NULL ) {
		R_GLESD3_UseProgram( alphaTestProgram );
		glUniformMatrix4fv( alphaTestProgram->uMVP, 1, GL_FALSE, mvp );
	}
	R_GLESD3_UseProgram( program );
	glUniformMatrix4fv( program->uMVP, 1, GL_FALSE, mvp );

	const int stageCount = shader->GetNumStages();
	for ( int stage = 0; stage < stageCount; stage++ ) {
		const shaderStage_t *pStage = shader->GetStage( stage );

		// a stage switched off by its condition register is the material
		// behaving as authored, not a gap: not counted as a skip
		if ( regs[ pStage->conditionRegister ] == 0 ) {
			continue;
		}
		// Custom GLSL / ARB material programs are D7. Checked HERE, ahead of the
		// lighting classification, because a program stage is a program stage
		// whatever its lighting field says -- and a program stage that exits
		// through one of the generic `continue`s below leaves no trace, which
		// is exactly how seven heatHazeWithMask stages on game/airdefense1 were
		// dropped without appearing in any counter.
		//
		// customLighting stages are a different thing: the interaction pass
		// owns them, so the material pass declining them is correct rather
		// than a gap. Counted separately so both stay visible.
		// Before the newStage skip, exactly as the legacy loop orders it
		// (draw_common.cpp:6989): a stage this backend cannot draw may still be
		// the reason a LATER stage has something to sample.
		// Ahead of the capture below, unlike every other skip in this loop. A
		// stage that is not going to draw must not be the reason a fullscreen
		// screen copy is taken -- that copy is most of what these stages cost.
		if ( GLESD3_ProgramStageDisabled( pStage ) ) {
			continue;
		}

		if ( !backEnd.currentRenderCopied && GLESD3_StageUsesCurrentRender( pStage ) ) {
			R_GLESD3_CaptureCurrentRender();
		}

		if ( pStage->newStage != NULL ) {
			// r_skipNewAmbient, mirroring draw_common.cpp:7262.
			//
			// Two exemptions carried across with it. SS_POST_PROCESS materials
			// still draw: the cvar suppresses world and material ambient
			// programs, and taking the fullscreen post chain out with them is
			// not what it means. And customLighting stages are outside its
			// scope entirely -- the interaction pass owns those, so this pass
			// declining them is already correct and is already counted below;
			// letting the cvar short-circuit them would only hide that from the
			// counter.
			//
			// Not recorded as a skip. The counters exist to expose stages this
			// backend cannot draw, and a stage switched off by a debug cvar is
			// not one of those -- the same reasoning as the condition-register
			// test at the top of the loop.
			//
			// Expect this to change nothing on retail Quake 4 content, and do
			// not read that as the cvar being broken. Material.cpp:3217 forces
			// sort = SS_POST_PROCESS on any material whose program samples a
			// scene-capture image, and every program material the game actually
			// places is a _currentRender heat haze -- so the exemption above
			// covers all of them. Measured on mp/q4xdm14: 5885 newStage stages
			// reached this point, every one of them sort=100, none suppressed.
			// The only bumpyEnvironment materials in the pk4s are shaderDemos
			// and gfx/effects/test*, which no map references. The ARB2 path has
			// the same exemption and is equally inert; this is parity, not a
			// performance lever.
			//
			// Placed after the _currentRender capture above, deliberately: a
			// stage that is not drawn can still be the reason a later stage has
			// something to sample.
			if ( !pStage->newStage->customLighting
					&& r_skipNewAmbient.GetBool()
					&& shader->GetSort() < SS_POST_PROCESS ) {
				continue;
			}

			const glesD3MaterialProgram_t matProgram =
					pStage->newStage->customLighting
						? GLESD3_MATPROG_NONE
						: GLESD3_MaterialProgramFamily( pStage->newStage );
			if ( matProgram != GLESD3_MATPROG_NONE ) {
				if ( !GLESD3_DrawMaterialProgramStage( surf, pStage, mvp, matProgram ) ) {
					GLESD3_RecordSkip( GLESD3_SKIP_NEWSTAGE, shader );
				}
				// hand the material program its state back, exactly as the
				// cube-texgen branch below does
				R_GLESD3_UseProgram( program );
				continue;
			}
			GLESD3_RecordSkip( pStage->newStage->customLighting
					? GLESD3_SKIP_CUSTOM_LIGHTING : GLESD3_SKIP_NEWSTAGE, shader );
			continue;
		}

		if ( pStage->lighting != SL_AMBIENT ) {
			continue;
		}
		// (GL_ZERO, GL_ONE) stages exist only to write alpha masks
		if ( ( pStage->drawStateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) )
				== ( GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE ) ) {
			continue;
		}
		// these refresh offscreen targets sampled by later stages
		if ( pStage->texture.dynamic == DI_REFLECTION_RENDER
				|| pStage->texture.dynamic == DI_REFRACTION_RENDER ) {
			continue;
		}

		// The cube texgens have their own programs (D7a); the screen-space ones
		// (TG_SCREEN, TG_SCREEN2, TG_GLASSWARP, TG_POT_CORRECTION) still do
		// not, and are counted below. The dispatch itself happens after the
		// stage colour is resolved, because the cube programs consume it.
		const shaderStage_t *unusedBumpStage = NULL;
		const bool isCubeTexgen = pStage->texture.texgen != TG_EXPLICIT
				&& GLESD3_CubeTexgenProgramId( shader, pStage->texture.texgen,
						&unusedBumpStage ) != GLESD3_PROGRAM_COUNT;
		if ( pStage->texture.texgen != TG_EXPLICIT && !isCubeTexgen ) {
			GLESD3_RecordSkip( GLESD3_SKIP_TEXGEN, shader );
			continue;
		}

		float color[ 4 ];
		color[0] = regs[ pStage->color.registers[0] ];
		color[1] = regs[ pStage->color.registers[1] ];
		color[2] = regs[ pStage->color.registers[2] ];
		color[3] = regs[ pStage->color.registers[3] ];

		const int blendBits = pStage->drawStateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS );
		// an add of black and a blend of nothing are both no-ops worth skipping
		if ( blendBits == ( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE )
				&& color[0] <= 0.0f && color[1] <= 0.0f && color[2] <= 0.0f ) {
			continue;
		}
		if ( blendBits == ( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA )
				&& color[3] <= 0.0f ) {
			continue;
		}

		// decal stages bake stage colour into their vertex data; applying both
		// would darken them, which is why the legacy path also skips the
		// constant multiply in that case
		const bool hasBakedDecalStageColor =
				( surf->decalColorCache != NULL && stage < surf->decalColorStageCount
					&& surf->decalColorStride > 0 );

		float vertexColorPacking[ 4 ];
		GLESD3_VertexColorPacking( pStage->vertexColor, vertexColorPacking );
		if ( pStage->vertexColor != SVC_IGNORE ) {
			GLESD3_BindStageVertexColor( surf, stage, ambientBase );
		}

		// A cube stage draws with its own program and then hands the material
		// program back its state. uMVP survives the round trip: uniforms are
		// per-program, and this surface's value is still the one that was set
		// before the loop.
		if ( isCubeTexgen ) {
			if ( r_glesD3SkipCubeTexgen.GetBool() ) {
				GLESD3_RecordSkip( GLESD3_SKIP_TEXGEN, shader );
				continue;
			}
			if ( !GLESD3_DrawCubeTexgenStage( surf, pStage, mvp, color, vertexColorPacking ) ) {
				GLESD3_RecordSkip( GLESD3_SKIP_TEXGEN, shader );
			}
			R_GLESD3_UseProgram( program );
			if ( pStage->vertexColor != SVC_IGNORE ) {
				glVertexAttribPointer( GLESD3_ATTR_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE,
						sizeof( idDrawVert ),
						RB_DrawVertAttributePointer( ambientBase, offsetof( idDrawVert, color ) ) );
			}
			continue;
		}

		idVec4 matrixS, matrixT;
		GLESD3_StageTextureMatrix( &pStage->texture, regs, matrixS, matrixT );

		// an alpha-tested stage draws with the ALPHATEST variant; every other
		// stage stays on the discard-free base program (gles_program.h, D8)
		const float alphaTestRef = R_GLESD3_AlphaTestReference( pStage->drawStateBits );
		glesProgram_t *stageProgram = program;
		if ( alphaTestRef >= 0.0f && alphaTestProgram != NULL ) {
			stageProgram = alphaTestProgram;
		}

		( void )glGetError();	// drain, so the step probe attributes correctly
		GL_SelectTexture( 0 );
		GLESD3_StepError( "GL_SelectTexture", shader );
		GLESD3_BindStageImage( &pStage->texture, regs );
		GLESD3_StepError( "BindStageImage", shader );
		GL_State( pStage->drawStateBits );
		GLESD3_StepError( "GL_State", shader );

		R_GLESD3_UseProgram( stageProgram );
		glUniform4fv( stageProgram->uTexMatrixS, 1, matrixS.ToFloatPtr() );
		glUniform4fv( stageProgram->uTexMatrixT, 1, matrixT.ToFloatPtr() );
		glUniform4fv( stageProgram->uVertexColor, 1, vertexColorPacking );
		glUniform1f( stageProgram->uAlphaTest, alphaTestRef );
		if ( hasBakedDecalStageColor ) {
			glUniform4f( stageProgram->uColor, 1.0f, 1.0f, 1.0f, 1.0f );
		} else {
			glUniform4fv( stageProgram->uColor, 1, color );
		}

		GLESD3_StepError( "uniforms", shader );
		R_GLESD3_DrawElements( tri );
		GLESD3_StepError( "drawElements", shader );

		// r_glesD3Report 3: name every material stage this pass draws, with the
		// state that decides how it looks. A wrong-looking surface that is NOT
		// in the skip counts is being drawn incorrectly rather than dropped,
		// and this is what tells the two apart -- the same per-draw dump that
		// identified the post-process resolve quads in the ModernGL bring-up.
		// r_glesD3Report 4 includes the 2D views. That is where Quake 4's
		// fullscreen resolve lives -- the draw that copies the scene render
		// texture to the back buffer -- so gating the dump on viewEntitys hid
		// the one stage that decides whether the frame reaches the screen at
		// all.
		if ( r_glesD3Report.GetInteger() >= 3 && gles_dumpedDraws < 512
				&& ( backEnd.viewDef->viewEntitys || r_glesD3Report.GetInteger() >= 4 ) ) {
			gles_dumpedDraws++;
			// the first vertex's colour, read from CPU memory: this is the data
			// the COLOR attribute is pointed at, so it separates "the vertex
			// colours really are black" from "the attribute binding is wrong"
			int v0r = -1, v0g = -1, v0b = -1, v0a = -1;
			if ( tri->verts != NULL && tri->numVerts > 0 ) {
				v0r = tri->verts[0].color[0];
				v0g = tri->verts[0].color[1];
				v0b = tri->verts[0].color[2];
				v0a = tri->verts[0].color[3];
			}
			// The channel masks matter as much as the blend: a stage that means
			// to write ALPHA ONLY (maskcolor, used by every masked reflection
			// material in the game) becomes an opaque colour write if the mask
			// bits are lost, and paints over everything already lit there.
			const unsigned maskBits = (unsigned)( pStage->drawStateBits
					& ( GLS_REDMASK | GLS_GREENMASK | GLS_BLUEMASK | GLS_ALPHAMASK | GLS_DEPTHMASK ) );
			common->Printf( "glesd3 draw[%i] %s %s stage=%i blend=0x%x mask=%c%c%c%c%c atest=0x%x "
					"svc=%i color=%.2f,%.2f,%.2f,%.2f v0=%i,%i,%i,%i decal=%i\n",
					gles_dumpedDraws,
					backEnd.viewDef->viewEntitys ? "3d" : "2D",
					shader->GetName(), stage,
					blendBits,
					( maskBits & GLS_REDMASK ) ? 'R' : '-',
					( maskBits & GLS_GREENMASK ) ? 'G' : '-',
					( maskBits & GLS_BLUEMASK ) ? 'B' : '-',
					( maskBits & GLS_ALPHAMASK ) ? 'A' : '-',
					( maskBits & GLS_DEPTHMASK ) ? 'Z' : '-',
					(unsigned)( pStage->drawStateBits & GLS_ATEST_BITS ),
					(int)pStage->vertexColor,
					color[0], color[1], color[2], color[3],
					v0r, v0g, v0b, v0a,
					hasBakedDecalStageColor ? 1 : 0 );
		}

		if ( pStage->vertexColor != SVC_IGNORE ) {
			// restore the shared idDrawVert colour pointer: a decal stage may
			// have pointed it at a different buffer with a different stride
			glVertexAttribPointer( GLESD3_ATTR_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE,
					sizeof( idDrawVert ),
					RB_DrawVertAttributePointer( ambientBase, offsetof( idDrawVert, color ) ) );
		}
	}

	if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}
}

/*
====================
RB_GLESD3_DrawShaderPasses

Returns the number of surfaces consumed, so the caller can break at the first
SS_POST_PROCESS surface and re-enter after the passes those materials sample.
====================
*/
int RB_GLESD3_DrawShaderPasses( drawSurf_t **drawSurfs, int numDrawSurfs,
		bool recordPostProcessSkips ) {
	if ( drawSurfs == NULL || numDrawSurfs <= 0 ) {
		return 0;
	}
	if ( backEnd.viewDef->viewEntitys && r_skipAmbient.GetBool() ) {
		return numDrawSurfs;
	}

	RB_LogComment( "---------- RB_GLESD3_DrawShaderPasses ----------\n" );

	backEnd.currentSpace = NULL;

	int i;
	for ( i = 0; i < numDrawSurfs; i++ ) {
		if ( drawSurfs[i]->material == NULL || drawSurfs[i]->material->SuppressInSubview() ) {
			continue;
		}

		// A material that needs the screen copy but sorts BEFORE the
		// post-process block takes it here, mid-list, so the surfaces behind it
		// are in the copy and the ones in front of it are not
		// (draw_common.cpp:7543).
		if ( drawSurfs[i]->material->TestMaterialFlag( MF_NEED_CURRENT_RENDER )
				&& drawSurfs[i]->material->GetSort() < SS_POST_PROCESS
				&& !backEnd.currentRenderCopied
				&& !GLESD3_MaterialCopyIsProgramOnly( drawSurfs[i]->material ) ) {
			R_GLESD3_CaptureCurrentRender();
		}

		// Post-process materials sample the finished frame. The view driver
		// captures _currentRender after the fog pass and re-enters here; until
		// that capture exists this walk stops, and every surface after the
		// first post-process one is counted rather than silently dropped.
		//
		// Counted rather than silently broken out of: this is where every
		// heatHaze / glasswarp / refraction material in a scene goes, and on
		// game/airdefense1 it is seven warp_mask surfaces. They were drawn as
		// opaque black quads until R_FindARBProgram stopped returning 0 --
		// which had also been suppressing their SS_POST_PROCESS sort.
		// With r_glesD3SkipMaterialPrograms on, a material whose only reader of
		// the screen copy is a program stage no longer needs one, so it must not
		// stop the walk either. This is where the saving actually comes from:
		// breaking here is what makes the view driver take a fullscreen copy and
		// re-enter. The material still draws whatever ordinary stages it has --
		// glass keeps its plain stage and loses only the haze.
		if ( drawSurfs[i]->material->GetSort() >= SS_POST_PROCESS
				&& !backEnd.currentRenderCopied
				&& !GLESD3_MaterialCopyIsProgramOnly( drawSurfs[i]->material ) ) {
			if ( recordPostProcessSkips ) {
				for ( int remaining = i; remaining < numDrawSurfs; remaining++ ) {
					if ( drawSurfs[remaining]->material != NULL ) {
						GLESD3_RecordSkip( GLESD3_SKIP_POSTPROCESS, drawSurfs[remaining]->material );
					}
				}
			}
			break;
		}

		RB_GLESD3_T_RenderShaderPasses( drawSurfs[i] );
	}

	GL_Cull( CT_FRONT_SIDED );
	return i;
}

#endif /* OPENQ4_RENDERER_GLES_MODULE */
