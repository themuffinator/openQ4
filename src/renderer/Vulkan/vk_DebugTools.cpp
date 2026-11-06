/*
===============================================================================
	Vulkan debug drawing.

	tr_rendertools.cpp draws every r_show* view and the game's debug lines,
	text and polygons with fixed-function OpenGL: glBegin/glVertex, the matrix
	stack, GL_State, the polygon mode and the stencil. The Vulkan module
	compiles that file unchanged and routes those GL 1.1 calls
	(vk_GLStubs.cpp, and GL_State/GL_Cull in vk_Backend.cpp) into the small
	emulation below, which turns each glBegin/glEnd or glDrawElements into a
	draw through debug_draw.vert/.frag. Outside VK_DebugTools_DrawView the
	calls stay no-ops, as every other GL call site the shared front end still
	carries expects.

	The emulation covers what tr_rendertools.cpp and tr_trace.cpp use:
	points, lines, line loops and strips, triangles, fans, quads and
	polygons; GLS_POLYMODE_LINE, which turns filled primitives into their
	edges; GL_State's blend, depth function, depth mask and colour mask;
	GL_Cull; the depth test, stencil test and scissor enables; the modelview
	and projection stacks with glOrtho; glDepthRange; polygon offset (depth
	bias for filled polygons; for polygons drawn as lines, each polygon's
	slope-scaled offset applied to its edges in clip space, see
	VK_Debug_AssembleOffsetEdges); glLineWidth and glPointSize where the
	device has wideLines and largePoints; vertex, colour and texture coordinate
	arrays for glArrayElement and glDrawElements; stencil and colour clears;
	one bound 2D image (RB_BindDebugImage); and glRasterPos/glDrawPixels
	through a scratch image. The pixel readbacks are not emulated, because a
	Vulkan frame cannot stall for one: this file has its own RB_ShowIntensity
	and RB_ShowDepthBuffer, which draw full-screen passes
	(vk_PostProcess.cpp), and RB_CountStencilBuffer and RB_ScanStencilBuffer,
	which queue a copy of the stencil that prints once its frame has
	completed (VK_Exec_QueueStencilReadback).
===============================================================================
*/

#ifdef OPENQ4_RENDERER_VK_MODULE

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../tr_local.h"
#include "../Model_local.h"

#undef snprintf
#undef vsnprintf
#include <cstdio>
#include <cstring>
#include "volk.h"

#include "VulkanDevice.h"
#include "vk_ExecutorHooks.h"
#include "shaders/debug_shaders_spv.h"

void VK_FixupClipSpaceZ( float dst[ 16 ], const float src[ 16 ] );

extern idCVar r_useScissor;
extern idCVar cl_gunfov;
extern idCVar cl_gunfov_adjust;

enum vkDebugPassKind_t {
	VK_DEBUG_DRAW = VK_EXTRA_KIND_DEBUG_BASE,
	VK_DEBUG_DRAW_TEXTURED
};

typedef struct vkDebugVert_s {
	float	xyzw[ 4 ];		// w is 0 for shadow volume vertices at infinity
	byte	rgba[ 4 ];
	float	st[ 2 ];
} vkDebugVert_t;

typedef struct vkDebugPush_s {
	float	mvp[ 16 ];
	float	params[ 4 ];	// x: point size
} vkDebugPush_t;

static const int VK_DEBUG_MATRIX_STACK = 8;
static const int VK_DEBUG_ATTRIB_STACK = 4;
static const int VK_DEBUG_PIXEL_IMAGES = 2;

// the fixed-function state the tools set and read back
typedef struct vkDebugGLState_s {
	int				stateBits;			// GL_State
	int				cullType;			// GL_Cull
	bool			cullFace;			// GL_CULL_FACE, which GL_Cull toggles too
	bool			depthTest;
	bool			stencilTest;
	bool			scissorTest;
	bool			texture2D;
	bool			offsetFill;
	bool			offsetLine;
	float			offsetFactor;
	float			offsetUnits;
	float			depthNear;
	float			depthFar;
	int				scissor[ 4 ];		// GL window coordinates: x, y, w, h
	GLenum			stencilFunc;
	int				stencilRef;
	unsigned int	stencilMask;
	GLenum			stencilFail;
	GLenum			stencilDepthFail;
	GLenum			stencilPass;
	float			lineWidth;
	float			pointSize;
	float			color[ 4 ];
} vkDebugGLState_t;

typedef struct vkDebugGL_s {
	bool				active;
	const viewDef_t *	viewDef;
	VkCommandBuffer		cmd;
	int					fbWidth;
	int					fbHeight;

	vkDebugGLState_t	state;
	vkDebugGLState_t	attribStack[ VK_DEBUG_ATTRIB_STACK ];
	int					attribDepth;

	GLenum				matrixMode;
	float				modelView[ VK_DEBUG_MATRIX_STACK ][ 16 ];
	float				projection[ VK_DEBUG_MATRIX_STACK ][ 16 ];
	int					modelViewDepth;
	int					projectionDepth;

	bool				inBegin;
	GLenum				primitive;
	idList<vkDebugVert_t> verts;
	float				texCoord[ 2 ];

	const byte *		vertexArray;
	int					vertexArraySize;
	int					vertexArrayStride;
	const byte *		colorArray;
	int					colorArrayStride;
	bool				colorArrayEnabled;
	const byte *		texCoordArray;
	int					texCoordArrayStride;
	bool				texCoordArrayEnabled;

	float				clearColor[ 4 ];
	int					clearStencil;
	idImage *			boundImage;
	float				rasterPos[ 2 ];		// GL window coordinates
	idImage *			pixelImages[ VK_DEBUG_PIXEL_IMAGES ];
	int					pixelImageNext;

	VkShaderModule		vertModule;
	VkShaderModule		fragModule;
	VkShaderModule		texturedFragModule;
	bool				modulesFailed;
	bool				drew;
} vkDebugGL_t;

static vkDebugGL_t vkDbg;

static const float vkDebugIdentity[ 16 ] = {
	1, 0, 0, 0,
	0, 1, 0, 0,
	0, 0, 1, 0,
	0, 0, 0, 1
};

static VkShaderModule VK_Debug_CreateModule( const unsigned char *code, unsigned int size, const char *name ) {
	VkShaderModuleCreateInfo smci;
	memset( &smci, 0, sizeof( smci ) );
	smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	smci.codeSize = size;
	smci.pCode = (const uint32_t *)code;
	VkShaderModule module = VK_NULL_HANDLE;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &module ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: %s shader module creation failed", name );
		return VK_NULL_HANDLE;
	}
	return module;
}

static bool VK_Debug_EnsureModules( void ) {
	if ( vkDbg.vertModule != VK_NULL_HANDLE ) {
		return true;
	}
	if ( vkDbg.modulesFailed || vkCtx.device == VK_NULL_HANDLE ) {
		return false;
	}
	vkDbg.vertModule = VK_Debug_CreateModule( vk_debug_draw_vert_spv, vk_debug_draw_vert_spv_size, "debug draw vertex" );
	vkDbg.fragModule = VK_Debug_CreateModule( vk_debug_draw_frag_spv, vk_debug_draw_frag_spv_size, "debug draw fragment" );
	vkDbg.texturedFragModule = VK_Debug_CreateModule( vk_debug_draw_textured_frag_spv,
			vk_debug_draw_textured_frag_spv_size, "debug draw textured fragment" );
	if ( vkDbg.vertModule == VK_NULL_HANDLE || vkDbg.fragModule == VK_NULL_HANDLE
			|| vkDbg.texturedFragModule == VK_NULL_HANDLE ) {
		vkDbg.modulesFailed = true;
		return false;
	}
	return true;
}

static void VK_Debug_DestroyModule( VkShaderModule &module ) {
	if ( module != VK_NULL_HANDLE && vkCtx.device != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, module, NULL );
	}
	module = VK_NULL_HANDLE;
}

void VK_DebugTools_Shutdown( void ) {
	VK_Debug_DestroyModule( vkDbg.vertModule );
	VK_Debug_DestroyModule( vkDbg.fragModule );
	VK_Debug_DestroyModule( vkDbg.texturedFragModule );
	vkDbg.modulesFailed = false;
	vkDbg.verts.Clear();
	for ( int i = 0; i < VK_DEBUG_PIXEL_IMAGES; i++ ) {
		vkDbg.pixelImages[ i ] = NULL;
	}
	vkDbg.active = false;
}

/*
====================
Matrices

id's matrices are OpenGL column-major arrays; myGlMultMatrix( a, b, out )
applies a first, then b, as glLoadMatrixf( b ); glMultMatrixf( a ) would.
====================
*/
static float *VK_Debug_CurrentMatrix( void ) {
	return vkDbg.matrixMode == GL_PROJECTION
		? vkDbg.projection[ vkDbg.projectionDepth ]
		: vkDbg.modelView[ vkDbg.modelViewDepth ];
}

static void VK_Debug_MVP( float out[ 16 ] ) {
	float mvpGL[ 16 ];
	myGlMultMatrix( vkDbg.modelView[ vkDbg.modelViewDepth ], vkDbg.projection[ vkDbg.projectionDepth ], mvpGL );
	VK_FixupClipSpaceZ( out, mvpGL );
}

// GL window coordinates of a point under the current matrices and the view
static bool VK_Debug_ProjectToWindow( float x, float y, float z, float window[ 2 ] ) {
	float mvpGL[ 16 ];
	myGlMultMatrix( vkDbg.modelView[ vkDbg.modelViewDepth ], vkDbg.projection[ vkDbg.projectionDepth ], mvpGL );
	const float cx = mvpGL[ 0 ] * x + mvpGL[ 4 ] * y + mvpGL[ 8 ] * z + mvpGL[ 12 ];
	const float cy = mvpGL[ 1 ] * x + mvpGL[ 5 ] * y + mvpGL[ 9 ] * z + mvpGL[ 13 ];
	const float cw = mvpGL[ 3 ] * x + mvpGL[ 7 ] * y + mvpGL[ 11 ] * z + mvpGL[ 15 ];
	if ( idMath::Fabs( cw ) < 1e-6f ) {
		return false;
	}
	const viewDef_t *viewDef = vkDbg.viewDef;
	const float width = (float)( viewDef->viewport.x2 - viewDef->viewport.x1 + 1 );
	const float height = (float)( viewDef->viewport.y2 - viewDef->viewport.y1 + 1 );
	window[ 0 ] = (float)viewDef->viewport.x1 + ( cx / cw * 0.5f + 0.5f ) * width;
	window[ 1 ] = (float)viewDef->viewport.y1 + ( cy / cw * 0.5f + 0.5f ) * height;
	return true;
}

/*
====================
Flushing a primitive
====================
*/
static VkCompareOp VK_Debug_CompareOp( GLenum func ) {
	switch ( func ) {
		case GL_NEVER:		return VK_COMPARE_OP_NEVER;
		case GL_LESS:		return VK_COMPARE_OP_LESS;
		case GL_EQUAL:		return VK_COMPARE_OP_EQUAL;
		case GL_LEQUAL:		return VK_COMPARE_OP_LESS_OR_EQUAL;
		case GL_GREATER:	return VK_COMPARE_OP_GREATER;
		case GL_NOTEQUAL:	return VK_COMPARE_OP_NOT_EQUAL;
		case GL_GEQUAL:		return VK_COMPARE_OP_GREATER_OR_EQUAL;
		default:			return VK_COMPARE_OP_ALWAYS;
	}
}

static VkStencilOp VK_Debug_StencilOp( GLenum op ) {
	switch ( op ) {
		case GL_ZERO:		return VK_STENCIL_OP_ZERO;
		case GL_REPLACE:	return VK_STENCIL_OP_REPLACE;
		case GL_INCR:		return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
		case GL_DECR:		return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
		case GL_INVERT:		return VK_STENCIL_OP_INVERT;
		case GL_INCR_WRAP:	return VK_STENCIL_OP_INCREMENT_AND_WRAP;
		case GL_DECR_WRAP:	return VK_STENCIL_OP_DECREMENT_AND_WRAP;
		default:			return VK_STENCIL_OP_KEEP;
	}
}

static void VK_Debug_SetScissor( VkCommandBuffer cmd ) {
	VkRect2D rect;
	if ( vkDbg.state.scissorTest ) {
		const int x = Max( 0, vkDbg.state.scissor[ 0 ] );
		const int top = VK_Exec_ActiveLowerOrigin() ? vkDbg.state.scissor[ 1 ]
			: vkDbg.fbHeight - ( vkDbg.state.scissor[ 1 ] + vkDbg.state.scissor[ 3 ] );
		const int y = Max( 0, top );
		const int x2 = Min( vkDbg.fbWidth, vkDbg.state.scissor[ 0 ] + vkDbg.state.scissor[ 2 ] );
		const int y2 = Min( vkDbg.fbHeight, top + vkDbg.state.scissor[ 3 ] );
		rect.offset.x = x;
		rect.offset.y = y;
		rect.extent.width = (uint32_t)Max( 0, x2 - x );
		rect.extent.height = (uint32_t)Max( 0, y2 - y );
	} else {
		rect.offset.x = 0;
		rect.offset.y = 0;
		rect.extent.width = (uint32_t)vkDbg.fbWidth;
		rect.extent.height = (uint32_t)vkDbg.fbHeight;
	}
	vkCmdSetScissorClamped( cmd, 0, 1, &rect );
}

// GL_Cull on the view's negative-height viewport, with the mirror swap
static void VK_Debug_SetCull( VkCommandBuffer cmd ) {
	const bool mirror = vkDbg.viewDef != NULL && vkDbg.viewDef->isMirror;
	if ( !vkDbg.state.cullFace ) {
		vkCmdSetCullMode( cmd, VK_CULL_MODE_NONE );
		return;
	}
	switch ( vkDbg.state.cullType ) {
		case CT_TWO_SIDED:
			vkCmdSetCullMode( cmd, VK_CULL_MODE_NONE );
			break;
		case CT_BACK_SIDED:
			vkCmdSetCullMode( cmd, mirror ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT );
			break;
		default:
			vkCmdSetCullMode( cmd, mirror ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_FRONT_BIT );
			break;
	}
}

static void VK_Debug_EmitEdge( idList<vkDebugVert_t> &out, const vkDebugVert_t &a, const vkDebugVert_t &b ) {
	out.Append( a );
	out.Append( b );
}

static void VK_Debug_TransformClip( const float mvp[ 16 ], const vkDebugVert_t &in, vkDebugVert_t &out ) {
	out = in;
	for ( int row = 0; row < 4; row++ ) {
		out.xyzw[ row ] = mvp[ row ] * in.xyzw[ 0 ] + mvp[ 4 + row ] * in.xyzw[ 1 ]
			+ mvp[ 8 + row ] * in.xyzw[ 2 ] + mvp[ 12 + row ] * in.xyzw[ 3 ];
	}
}

static float VK_Debug_Det3( float a0, float a1, float a2, float b0, float b1, float b2,
		float c0, float c1, float c2 ) {
	return a0 * ( b1 * c2 - b2 * c1 ) - a1 * ( b0 * c2 - b2 * c0 ) + a2 * ( b0 * c1 - b1 * c0 );
}

/*
====================
VK_Debug_ClipPolygonCulled

GL_Cull for a polygon drawn in GLS_POLYMODE_LINE. OpenGL culls the polygon
before it rasterizes its edges; Vulkan never culls lines, so the edges of a
culled polygon are dropped here. The sign of the determinant of the clip
space (x, y, w) of three corners is the polygon's window-space winding, and
it stays right for corners behind the eye. Front faces are counterclockwise
(glFrontFace's default); CT_FRONT_SIDED culls them, as GL_Cull does, and a
mirror view swaps the two.
====================
*/
static bool VK_Debug_ClipPolygonCulled( const vkDebugVert_t &a, const vkDebugVert_t &b, const vkDebugVert_t &c ) {
	if ( !vkDbg.state.cullFace || vkDbg.state.cullType == CT_TWO_SIDED ) {
		return false;
	}
	const float winding = VK_Debug_Det3( a.xyzw[ 0 ], a.xyzw[ 1 ], a.xyzw[ 3 ],
		b.xyzw[ 0 ], b.xyzw[ 1 ], b.xyzw[ 3 ], c.xyzw[ 0 ], c.xyzw[ 1 ], c.xyzw[ 3 ] );
	if ( winding == 0.0f ) {
		return true;
	}
	const bool mirror = vkDbg.viewDef != NULL && vkDbg.viewDef->isMirror;
	const bool cullCounterClockwise = ( vkDbg.state.cullType != CT_BACK_SIDED ) != mirror;
	return ( winding > 0.0f ) == cullCounterClockwise;
}

static bool VK_Debug_PolygonCulled( const float mvp[ 16 ], const vkDebugVert_t &a, const vkDebugVert_t &b,
		const vkDebugVert_t &c ) {
	if ( !vkDbg.state.cullFace || vkDbg.state.cullType == CT_TWO_SIDED ) {
		return false;
	}
	vkDebugVert_t clip[ 3 ];
	VK_Debug_TransformClip( mvp, a, clip[ 0 ] );
	VK_Debug_TransformClip( mvp, b, clip[ 1 ] );
	VK_Debug_TransformClip( mvp, c, clip[ 2 ] );
	return VK_Debug_ClipPolygonCulled( clip[ 0 ], clip[ 1 ], clip[ 2 ] );
}

// Expands the recorded primitive into a point, line or triangle list.
// Filled polygons are culled by the pipeline; line-mode ones here.
static int VK_Debug_Assemble( GLenum primitive, const idList<vkDebugVert_t> &in, const float mvp[ 16 ],
		idList<vkDebugVert_t> &out ) {
	const bool lineMode = ( vkDbg.state.stateBits & GLS_POLYMODE_LINE ) != 0;
	const int n = in.Num();
	out.SetNum( 0, false );
	switch ( primitive ) {
		case GL_POINTS:
			for ( int i = 0; i < n; i++ ) {
				out.Append( in[ i ] );
			}
			return VK_EXTRA_PIPELINE_POINTS;
		case GL_LINES:
			for ( int i = 0; i + 1 < n; i += 2 ) {
				VK_Debug_EmitEdge( out, in[ i ], in[ i + 1 ] );
			}
			return VK_EXTRA_PIPELINE_LINES;
		case GL_LINE_STRIP:
		case GL_LINE_LOOP:
			for ( int i = 0; i + 1 < n; i++ ) {
				VK_Debug_EmitEdge( out, in[ i ], in[ i + 1 ] );
			}
			if ( primitive == GL_LINE_LOOP && n > 2 ) {
				VK_Debug_EmitEdge( out, in[ n - 1 ], in[ 0 ] );
			}
			return VK_EXTRA_PIPELINE_LINES;
		case GL_TRIANGLES:
			for ( int i = 0; i + 2 < n; i += 3 ) {
				if ( lineMode ) {
					if ( !VK_Debug_PolygonCulled( mvp, in[ i ], in[ i + 1 ], in[ i + 2 ] ) ) {
						VK_Debug_EmitEdge( out, in[ i ], in[ i + 1 ] );
						VK_Debug_EmitEdge( out, in[ i + 1 ], in[ i + 2 ] );
						VK_Debug_EmitEdge( out, in[ i + 2 ], in[ i ] );
					}
				} else {
					out.Append( in[ i ] );
					out.Append( in[ i + 1 ] );
					out.Append( in[ i + 2 ] );
				}
			}
			return lineMode ? VK_EXTRA_PIPELINE_LINES : 0;
		case GL_TRIANGLE_STRIP:
			for ( int i = 0; i + 2 < n; i++ ) {
				const vkDebugVert_t &a = in[ i ];
				const vkDebugVert_t &b = ( i & 1 ) ? in[ i + 2 ] : in[ i + 1 ];
				const vkDebugVert_t &c = ( i & 1 ) ? in[ i + 1 ] : in[ i + 2 ];
				if ( lineMode ) {
					if ( !VK_Debug_PolygonCulled( mvp, a, b, c ) ) {
						VK_Debug_EmitEdge( out, a, b );
						VK_Debug_EmitEdge( out, b, c );
						VK_Debug_EmitEdge( out, c, a );
					}
				} else {
					out.Append( a );
					out.Append( b );
					out.Append( c );
				}
			}
			return lineMode ? VK_EXTRA_PIPELINE_LINES : 0;
		case GL_QUADS:
			for ( int i = 0; i + 3 < n; i += 4 ) {
				if ( lineMode ) {
					if ( !VK_Debug_PolygonCulled( mvp, in[ i ], in[ i + 1 ], in[ i + 2 ] ) ) {
						for ( int e = 0; e < 4; e++ ) {
							VK_Debug_EmitEdge( out, in[ i + e ], in[ i + ( ( e + 1 ) & 3 ) ] );
						}
					}
				} else {
					out.Append( in[ i ] );
					out.Append( in[ i + 1 ] );
					out.Append( in[ i + 2 ] );
					out.Append( in[ i ] );
					out.Append( in[ i + 2 ] );
					out.Append( in[ i + 3 ] );
				}
			}
			return lineMode ? VK_EXTRA_PIPELINE_LINES : 0;
		case GL_TRIANGLE_FAN:
		case GL_POLYGON:
		default:
			if ( lineMode ) {
				// a polygon's outline; a fan's own triangle edges
				if ( primitive == GL_POLYGON ) {
					if ( n > 2 && !VK_Debug_PolygonCulled( mvp, in[ 0 ], in[ 1 ], in[ 2 ] ) ) {
						for ( int i = 0; i < n; i++ ) {
							VK_Debug_EmitEdge( out, in[ i ], in[ ( i + 1 ) % n ] );
						}
					}
				} else {
					for ( int i = 1; i + 1 < n; i++ ) {
						if ( !VK_Debug_PolygonCulled( mvp, in[ 0 ], in[ i ], in[ i + 1 ] ) ) {
							VK_Debug_EmitEdge( out, in[ 0 ], in[ i ] );
							VK_Debug_EmitEdge( out, in[ i ], in[ i + 1 ] );
							VK_Debug_EmitEdge( out, in[ i + 1 ], in[ 0 ] );
						}
					}
				}
				return VK_EXTRA_PIPELINE_LINES;
			}
			for ( int i = 1; i + 1 < n; i++ ) {
				out.Append( in[ 0 ] );
				out.Append( in[ i ] );
				out.Append( in[ i + 1 ] );
			}
			return 0;
	}
}

static idList<vkDebugVert_t> vkDebugAssembled;

// Draws vertices (already a point, line or triangle list) with the current
// state and matrices.
static void VK_Debug_Draw( const idList<vkDebugVert_t> &verts, int topologyFlags, const float mvp[ 16 ] ) {
	if ( verts.Num() <= 0 || !VK_Debug_EnsureModules() || !VK_Exec_MainRenderingScopeOpen() ) {
		return;
	}
	VkCommandBuffer cmd = vkDbg.cmd;
	const vkDebugGLState_t &s = vkDbg.state;

	// OpenGL leaves a bound image off whenever GL_TEXTURE_2D is disabled
	idImage *image = s.texture2D ? vkDbg.boundImage : NULL;
	VkDescriptorSet imageSet = VK_NULL_HANDLE;
	if ( image != NULL ) {
		if ( !image->IsLoaded() ) {
			image->ActuallyLoadImage( true );
		}
		imageSet = VK_Exec_ImageDescriptor( image->GetDeviceHandle(), true );
		if ( imageSet == VK_NULL_HANDLE ) {
			image = NULL;
		}
	}

	const int pipelineBits = s.stateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS | GLS_COLORMASK | GLS_ALPHAMASK );
	const VkPipeline pipeline = VK_Exec_ExtraPipeline( image != NULL ? VK_DEBUG_DRAW_TEXTURED : VK_DEBUG_DRAW,
			vkDbg.vertModule, image != NULL ? vkDbg.texturedFragModule : vkDbg.fragModule,
			pipelineBits, VK_EXTRA_VERTEX_DEBUG, topologyFlags );
	if ( pipeline == VK_NULL_HANDLE
			|| !VK_Exec_BindTransientVertices( cmd, verts.Ptr(), verts.Num() * (int)sizeof( vkDebugVert_t ) ) ) {
		return;
	}

	VK_Exec_SetViewViewport( cmd, vkDbg.viewDef, idMath::ClampFloat( 0.0f, 1.0f, s.depthFar ) );
	VK_Debug_SetScissor( cmd );
	VK_Debug_SetCull( cmd );
	vkCmdSetFrontFace( cmd, VK_Exec_CanonicalFrontFace() );

	VkCompareOp depthOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	if ( s.stateBits & GLS_DEPTHFUNC_ALWAYS ) {
		depthOp = VK_COMPARE_OP_ALWAYS;
	} else if ( s.stateBits & GLS_DEPTHFUNC_EQUAL ) {
		depthOp = VK_COMPARE_OP_EQUAL;
	}
	vkCmdSetDepthTestEnable( cmd, s.depthTest ? VK_TRUE : VK_FALSE );
	vkCmdSetDepthWriteEnable( cmd, s.depthTest && ( s.stateBits & GLS_DEPTHMASK ) == 0 ? VK_TRUE : VK_FALSE );
	vkCmdSetDepthCompareOp( cmd, depthOp );
	if ( vkCtx.depthBoundsSupported ) {
		vkCmdSetDepthBoundsTestEnable( cmd, VK_FALSE );
	}

	// GL_POLYGON_OFFSET_FILL is Vulkan's depth bias, units and slope factor
	// alike; polygons drawn as lines carry their offset in the vertices
	// (VK_Debug_AssembleOffsetEdges), and plain lines and points take none
	const bool triangles = ( topologyFlags & ( VK_EXTRA_PIPELINE_LINES | VK_EXTRA_PIPELINE_POINTS ) ) == 0;
	if ( triangles && s.offsetFill ) {
		vkCmdSetDepthBiasEnable( cmd, VK_TRUE );
		vkCmdSetDepthBias( cmd, s.offsetUnits, 0.0f, s.offsetFactor );
	} else {
		vkCmdSetDepthBiasEnable( cmd, VK_FALSE );
	}

	if ( s.stencilTest ) {
		vkCmdSetStencilTestEnable( cmd, VK_TRUE );
		vkCmdSetStencilOp( cmd, VK_STENCIL_FACE_FRONT_AND_BACK, VK_Debug_StencilOp( s.stencilFail ),
				VK_Debug_StencilOp( s.stencilPass ), VK_Debug_StencilOp( s.stencilDepthFail ),
				VK_Debug_CompareOp( s.stencilFunc ) );
		vkCmdSetStencilCompareMask( cmd, VK_STENCIL_FACE_FRONT_AND_BACK, s.stencilMask & 255 );
		vkCmdSetStencilWriteMask( cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 255 );
		vkCmdSetStencilReference( cmd, VK_STENCIL_FACE_FRONT_AND_BACK, (uint32_t)( s.stencilRef & 255 ) );
	} else {
		vkCmdSetStencilTestEnable( cmd, VK_FALSE );
	}

	if ( topologyFlags & VK_EXTRA_PIPELINE_LINES ) {
		float width = 1.0f;
		if ( vkCtx.wideLinesSupported ) {
			width = idMath::ClampFloat( vkCtx.deviceProperties.limits.lineWidthRange[ 0 ],
					vkCtx.deviceProperties.limits.lineWidthRange[ 1 ], s.lineWidth );
		}
		vkCmdSetLineWidth( cmd, width );
	}

	vkDebugPush_t push;
	memcpy( push.mvp, mvp, sizeof( push.mvp ) );
	push.params[ 0 ] = vkCtx.largePointsSupported
		? idMath::ClampFloat( vkCtx.deviceProperties.limits.pointSizeRange[ 0 ],
			vkCtx.deviceProperties.limits.pointSizeRange[ 1 ], s.pointSize )
		: 1.0f;
	push.params[ 1 ] = 0.0f;
	push.params[ 2 ] = 0.0f;
	push.params[ 3 ] = 0.0f;

	const VkPipelineLayout layout = VK_Exec_InteractionPipelineLayout();
	vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
	if ( image != NULL ) {
		vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &imageSet, 0, NULL );
	}
	vkCmdPushConstants( cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof( push ), &push );
	vkCmdDraw( cmd, (uint32_t)verts.Num(), 1, 0, 0 );
	vkDbg.drew = true;
}

/*
====================
VK_Debug_AssembleOffsetEdges

GL_POLYGON_OFFSET_LINE: OpenGL offsets a polygon drawn in GLS_POLYMODE_LINE
by factor * m + units * r, where m is the polygon's window-space depth
slope. Vulkan's depth bias only reaches line-mode polygons through
VK_POLYGON_MODE_LINE, which also draws the diagonals of quads and polygons,
so the edges go out as lines in clip space instead, each polygon's edges
moved by that polygon's own offset. The slope comes from the plane through
the polygon's first three clip-space vertices, which holds for vertices
behind the eye as well.
====================
*/
// the NDC depth offset of the polygon through three clip-space vertices
static float VK_Debug_PolygonLineOffset( const vkDebugVert_t &p0, const vkDebugVert_t &p1,
		const vkDebugVert_t &p2 ) {
	const float *a = p0.xyzw;
	const float *b = p1.xyzw;
	const float *c = p2.xyzw;
	// A x + B y + C z + D w = 0 for all three: the plane in NDC is
	// z = -( A x + B y + D ) / C
	const float planeA = VK_Debug_Det3( a[ 1 ], a[ 2 ], a[ 3 ], b[ 1 ], b[ 2 ], b[ 3 ], c[ 1 ], c[ 2 ], c[ 3 ] );
	const float planeB = -VK_Debug_Det3( a[ 0 ], a[ 2 ], a[ 3 ], b[ 0 ], b[ 2 ], b[ 3 ], c[ 0 ], c[ 2 ], c[ 3 ] );
	const float planeC = VK_Debug_Det3( a[ 0 ], a[ 1 ], a[ 3 ], b[ 0 ], b[ 1 ], b[ 3 ], c[ 0 ], c[ 1 ], c[ 3 ] );
	const viewDef_t *viewDef = vkDbg.viewDef;
	const float halfWidth = 0.5f * (float)( viewDef->viewport.x2 - viewDef->viewport.x1 + 1 );
	const float halfHeight = 0.5f * (float)( viewDef->viewport.y2 - viewDef->viewport.y1 + 1 );
	const float depthScale = idMath::ClampFloat( 0.0f, 1.0f, vkDbg.state.depthFar );
	if ( depthScale <= 0.0f ) {
		return 0.0f;
	}
	float slope = 0.0f;
	const float planeMagnitude = idMath::Fabs( planeA ) + idMath::Fabs( planeB ) + idMath::Fabs( planeC );
	if ( idMath::Fabs( planeC ) > planeMagnitude * 1e-6f ) {
		// window depth per window pixel
		const float slopeX = idMath::Fabs( planeA / planeC ) * depthScale / halfWidth;
		const float slopeY = idMath::Fabs( planeB / planeC ) * depthScale / halfHeight;
		slope = Max( slopeX, slopeY );
	} else if ( planeMagnitude > 0.0f ) {
		// seen edge on: as steep as a window-depth range per pixel
		slope = depthScale;
	}
	const float windowOffset = vkDbg.state.offsetFactor * slope
		+ vkDbg.state.offsetUnits * ( 1.0f / 16777216.0f );
	return windowOffset / depthScale;
}

static void VK_Debug_EmitOffsetPolygon( const vkDebugVert_t *clip, const int *corners, int count,
		idList<vkDebugVert_t> &out ) {
	if ( VK_Debug_ClipPolygonCulled( clip[ corners[ 0 ] ], clip[ corners[ 1 ] ], clip[ corners[ 2 ] ] ) ) {
		return;
	}
	const float offset = VK_Debug_PolygonLineOffset( clip[ corners[ 0 ] ], clip[ corners[ 1 ] ], clip[ corners[ 2 ] ] );
	for ( int e = 0; e < count; e++ ) {
		vkDebugVert_t a = clip[ corners[ e ] ];
		vkDebugVert_t b = clip[ corners[ ( e + 1 ) % count ] ];
		a.xyzw[ 2 ] += offset * a.xyzw[ 3 ];
		b.xyzw[ 2 ] += offset * b.xyzw[ 3 ];
		out.Append( a );
		out.Append( b );
	}
}

static idList<vkDebugVert_t> vkDebugClip;

static void VK_Debug_AssembleOffsetEdges( GLenum primitive, const idList<vkDebugVert_t> &in,
		const float mvp[ 16 ], idList<vkDebugVert_t> &out ) {
	const int n = in.Num();
	out.SetNum( 0, false );
	vkDebugClip.SetNum( n, false );
	for ( int i = 0; i < n; i++ ) {
		VK_Debug_TransformClip( mvp, in[ i ], vkDebugClip[ i ] );
	}
	const vkDebugVert_t *clip = vkDebugClip.Ptr();
	int corners[ 4 ];
	switch ( primitive ) {
		case GL_TRIANGLES:
			for ( int i = 0; i + 2 < n; i += 3 ) {
				corners[ 0 ] = i; corners[ 1 ] = i + 1; corners[ 2 ] = i + 2;
				VK_Debug_EmitOffsetPolygon( clip, corners, 3, out );
			}
			break;
		case GL_TRIANGLE_STRIP:
			for ( int i = 0; i + 2 < n; i++ ) {
				corners[ 0 ] = i; corners[ 1 ] = i + 1; corners[ 2 ] = i + 2;
				VK_Debug_EmitOffsetPolygon( clip, corners, 3, out );
			}
			break;
		case GL_TRIANGLE_FAN:
			for ( int i = 1; i + 1 < n; i++ ) {
				corners[ 0 ] = 0; corners[ 1 ] = i; corners[ 2 ] = i + 1;
				VK_Debug_EmitOffsetPolygon( clip, corners, 3, out );
			}
			break;
		case GL_QUADS:
			for ( int i = 0; i + 3 < n; i += 4 ) {
				corners[ 0 ] = i; corners[ 1 ] = i + 1; corners[ 2 ] = i + 2; corners[ 3 ] = i + 3;
				VK_Debug_EmitOffsetPolygon( clip, corners, 4, out );
			}
			break;
		default: {
			// GL_POLYGON: the outline, offset by the plane of its first corners
			if ( n < 3 || VK_Debug_ClipPolygonCulled( clip[ 0 ], clip[ 1 ], clip[ 2 ] ) ) {
				break;
			}
			const float offset = VK_Debug_PolygonLineOffset( clip[ 0 ], clip[ 1 ], clip[ 2 ] );
			for ( int i = 0; i < n; i++ ) {
				vkDebugVert_t a = clip[ i ];
				vkDebugVert_t b = clip[ ( i + 1 ) % n ];
				a.xyzw[ 2 ] += offset * a.xyzw[ 3 ];
				b.xyzw[ 2 ] += offset * b.xyzw[ 3 ];
				out.Append( a );
				out.Append( b );
			}
			break;
		}
	}
}

static void VK_Debug_FlushPrimitive( GLenum primitive, const idList<vkDebugVert_t> &verts ) {
	float mvp[ 16 ];
	VK_Debug_MVP( mvp );
	const bool polygon = primitive != GL_POINTS && primitive != GL_LINES
		&& primitive != GL_LINE_STRIP && primitive != GL_LINE_LOOP;
	if ( polygon && vkDbg.state.offsetLine && ( vkDbg.state.stateBits & GLS_POLYMODE_LINE ) != 0 ) {
		VK_Debug_AssembleOffsetEdges( primitive, verts, mvp, vkDebugAssembled );
		VK_Debug_Draw( vkDebugAssembled, VK_EXTRA_PIPELINE_LINES, vkDebugIdentity );
		return;
	}
	const int topologyFlags = VK_Debug_Assemble( primitive, verts, mvp, vkDebugAssembled );
	VK_Debug_Draw( vkDebugAssembled, topologyFlags, mvp );
}

static void VK_Debug_EmitVertex( float x, float y, float z, float w ) {
	vkDebugVert_t vert;
	vert.xyzw[ 0 ] = x;
	vert.xyzw[ 1 ] = y;
	vert.xyzw[ 2 ] = z;
	vert.xyzw[ 3 ] = w;
	for ( int i = 0; i < 4; i++ ) {
		vert.rgba[ i ] = (byte)idMath::Ftoi( idMath::ClampFloat( 0.0f, 1.0f, vkDbg.state.color[ i ] ) * 255.0f );
	}
	vert.st[ 0 ] = vkDbg.texCoord[ 0 ];
	vert.st[ 1 ] = vkDbg.texCoord[ 1 ];
	vkDbg.verts.Append( vert );
}

/*
===============================================================================

	The GL 1.1 entry points (vk_GLStubs.cpp forwards them here)

===============================================================================
*/

void VK_DebugGL_Begin( GLenum mode ) {
	if ( !vkDbg.active ) {
		return;
	}
	vkDbg.inBegin = true;
	vkDbg.primitive = mode;
	vkDbg.verts.SetNum( 0, false );
}

void VK_DebugGL_End( void ) {
	if ( !vkDbg.active || !vkDbg.inBegin ) {
		return;
	}
	vkDbg.inBegin = false;
	VK_Debug_FlushPrimitive( vkDbg.primitive, vkDbg.verts );
	vkDbg.verts.SetNum( 0, false );
}

void VK_DebugGL_Vertex3f( float x, float y, float z ) {
	if ( vkDbg.active && vkDbg.inBegin ) {
		VK_Debug_EmitVertex( x, y, z, 1.0f );
	}
}

void VK_DebugGL_Color4f( float r, float g, float b, float a ) {
	if ( !vkDbg.active ) {
		return;
	}
	vkDbg.state.color[ 0 ] = r;
	vkDbg.state.color[ 1 ] = g;
	vkDbg.state.color[ 2 ] = b;
	vkDbg.state.color[ 3 ] = a;
}

void VK_DebugGL_TexCoord2f( float s, float t ) {
	if ( vkDbg.active ) {
		vkDbg.texCoord[ 0 ] = s;
		vkDbg.texCoord[ 1 ] = t;
	}
}

void VK_DebugGL_VertexPointer( int size, GLenum type, int stride, const void *pointer ) {
	if ( !vkDbg.active ) {
		return;
	}
	vkDbg.vertexArray = type == GL_FLOAT ? (const byte *)pointer : NULL;
	vkDbg.vertexArraySize = size;
	vkDbg.vertexArrayStride = stride > 0 ? stride : size * (int)sizeof( float );
}

void VK_DebugGL_ColorPointer( int size, GLenum type, int stride, const void *pointer ) {
	if ( !vkDbg.active ) {
		return;
	}
	vkDbg.colorArray = ( type == GL_UNSIGNED_BYTE && size == 4 ) ? (const byte *)pointer : NULL;
	vkDbg.colorArrayStride = stride > 0 ? stride : 4;
}

void VK_DebugGL_TexCoordPointer( int size, GLenum type, int stride, const void *pointer ) {
	if ( !vkDbg.active ) {
		return;
	}
	vkDbg.texCoordArray = ( type == GL_FLOAT && size >= 2 ) ? (const byte *)pointer : NULL;
	vkDbg.texCoordArrayStride = stride > 0 ? stride : size * (int)sizeof( float );
}

void VK_DebugGL_ClientState( GLenum array, bool enable ) {
	if ( !vkDbg.active ) {
		return;
	}
	if ( array == GL_COLOR_ARRAY ) {
		vkDbg.colorArrayEnabled = enable;
	} else if ( array == GL_TEXTURE_COORD_ARRAY ) {
		vkDbg.texCoordArrayEnabled = enable;
	}
}

void VK_DebugGL_ArrayElement( int i ) {
	if ( !vkDbg.active || !vkDbg.inBegin || vkDbg.vertexArray == NULL || i < 0 ) {
		return;
	}
	const float *xyz = (const float *)( vkDbg.vertexArray + (size_t)i * vkDbg.vertexArrayStride );
	if ( vkDbg.colorArrayEnabled && vkDbg.colorArray != NULL ) {
		const byte *rgba = vkDbg.colorArray + (size_t)i * vkDbg.colorArrayStride;
		for ( int c = 0; c < 4; c++ ) {
			vkDbg.state.color[ c ] = rgba[ c ] * ( 1.0f / 255.0f );
		}
	}
	if ( vkDbg.texCoordArrayEnabled && vkDbg.texCoordArray != NULL ) {
		const float *st = (const float *)( vkDbg.texCoordArray + (size_t)i * vkDbg.texCoordArrayStride );
		vkDbg.texCoord[ 0 ] = st[ 0 ];
		vkDbg.texCoord[ 1 ] = st[ 1 ];
	}
	VK_Debug_EmitVertex( xyz[ 0 ], xyz[ 1 ], vkDbg.vertexArraySize >= 3 ? xyz[ 2 ] : 0.0f,
			vkDbg.vertexArraySize >= 4 ? xyz[ 3 ] : 1.0f );
}

void VK_DebugGL_DrawElements( GLenum mode, int count, GLenum type, const void *indices ) {
	if ( !vkDbg.active || indices == NULL || count <= 0 ) {
		return;
	}
	VK_DebugGL_Begin( mode );
	for ( int i = 0; i < count; i++ ) {
		const int index = type == GL_UNSIGNED_SHORT ? ( (const unsigned short *)indices )[ i ]
			: ( type == GL_UNSIGNED_BYTE ? ( (const byte *)indices )[ i ] : (int)( (const unsigned int *)indices )[ i ] );
		VK_DebugGL_ArrayElement( index );
	}
	VK_DebugGL_End();
}

void VK_DebugGL_Enable( GLenum cap, bool enable ) {
	if ( !vkDbg.active ) {
		return;
	}
	switch ( cap ) {
		case GL_CULL_FACE:				vkDbg.state.cullFace = enable; break;
		case GL_DEPTH_TEST:				vkDbg.state.depthTest = enable; break;
		case GL_STENCIL_TEST:			vkDbg.state.stencilTest = enable; break;
		case GL_SCISSOR_TEST:			vkDbg.state.scissorTest = enable; break;
		case GL_TEXTURE_2D:				vkDbg.state.texture2D = enable; break;
		case GL_POLYGON_OFFSET_FILL:	vkDbg.state.offsetFill = enable; break;
		case GL_POLYGON_OFFSET_LINE:	vkDbg.state.offsetLine = enable; break;
		default: break;
	}
}

void VK_DebugGL_PolygonOffset( float factor, float units ) {
	if ( vkDbg.active ) {
		vkDbg.state.offsetFactor = factor;
		vkDbg.state.offsetUnits = units;
	}
}

void VK_DebugGL_DepthRange( double zNear, double zFar ) {
	if ( vkDbg.active ) {
		vkDbg.state.depthNear = (float)zNear;
		vkDbg.state.depthFar = (float)zFar;
	}
}

void VK_DebugGL_Scissor( int x, int y, int width, int height ) {
	if ( vkDbg.active ) {
		vkDbg.state.scissor[ 0 ] = x;
		vkDbg.state.scissor[ 1 ] = y;
		vkDbg.state.scissor[ 2 ] = width;
		vkDbg.state.scissor[ 3 ] = height;
	}
}

void VK_DebugGL_LineWidth( float width ) {
	if ( vkDbg.active ) {
		vkDbg.state.lineWidth = width;
	}
}

void VK_DebugGL_PointSize( float size ) {
	if ( vkDbg.active ) {
		vkDbg.state.pointSize = size;
	}
}

void VK_DebugGL_DepthMask( bool write ) {
	if ( !vkDbg.active ) {
		return;
	}
	if ( write ) {
		vkDbg.state.stateBits &= ~GLS_DEPTHMASK;
	} else {
		vkDbg.state.stateBits |= GLS_DEPTHMASK;
	}
}

void VK_DebugGL_ColorMask( bool r, bool g, bool b, bool a ) {
	if ( !vkDbg.active ) {
		return;
	}
	vkDbg.state.stateBits &= ~( GLS_COLORMASK | GLS_ALPHAMASK );
	vkDbg.state.stateBits |= ( r ? 0 : GLS_REDMASK ) | ( g ? 0 : GLS_GREENMASK )
		| ( b ? 0 : GLS_BLUEMASK ) | ( a ? 0 : GLS_ALPHAMASK );
}

void VK_DebugGL_StencilFunc( GLenum func, int ref, unsigned int mask ) {
	if ( vkDbg.active ) {
		vkDbg.state.stencilFunc = func;
		vkDbg.state.stencilRef = ref;
		vkDbg.state.stencilMask = mask;
	}
}

void VK_DebugGL_StencilOp( GLenum fail, GLenum zfail, GLenum zpass ) {
	if ( vkDbg.active ) {
		vkDbg.state.stencilFail = fail;
		vkDbg.state.stencilDepthFail = zfail;
		vkDbg.state.stencilPass = zpass;
	}
}

void VK_DebugGL_ClearColor( float r, float g, float b, float a ) {
	if ( vkDbg.active ) {
		vkDbg.clearColor[ 0 ] = r;
		vkDbg.clearColor[ 1 ] = g;
		vkDbg.clearColor[ 2 ] = b;
		vkDbg.clearColor[ 3 ] = a;
	}
}

void VK_DebugGL_ClearStencil( int s ) {
	if ( vkDbg.active ) {
		vkDbg.clearStencil = s;
	}
}

// glClear honours the scissor test; without it the whole target is cleared
void VK_DebugGL_Clear( GLbitfield mask ) {
	if ( !vkDbg.active || !VK_Exec_MainRenderingScopeOpen() ) {
		return;
	}
	VkClearAttachment attachments[ 2 ];
	memset( attachments, 0, sizeof( attachments ) );
	uint32_t count = 0;
	if ( mask & GL_COLOR_BUFFER_BIT ) {
		VkClearAttachment &color = attachments[ count++ ];
		color.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		memcpy( color.clearValue.color.float32, vkDbg.clearColor, sizeof( vkDbg.clearColor ) );
	}
	if ( ( mask & ( GL_STENCIL_BUFFER_BIT | GL_DEPTH_BUFFER_BIT ) ) && VK_Exec_ActiveTargetHasStencil() ) {
		VkClearAttachment &depthStencil = attachments[ count++ ];
		if ( mask & GL_STENCIL_BUFFER_BIT ) {
			depthStencil.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
		if ( mask & GL_DEPTH_BUFFER_BIT ) {
			depthStencil.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
		}
		depthStencil.clearValue.depthStencil.depth = 1.0f;
		depthStencil.clearValue.depthStencil.stencil = (uint32_t)( vkDbg.clearStencil & 255 );
	}
	if ( count == 0 ) {
		return;
	}
	VkClearRect rect;
	memset( &rect, 0, sizeof( rect ) );
	rect.layerCount = 1;
	if ( vkDbg.state.scissorTest ) {
		const int x = Max( 0, vkDbg.state.scissor[ 0 ] );
		const int top = Max( 0, VK_Exec_ActiveLowerOrigin() ? vkDbg.state.scissor[ 1 ]
			: vkDbg.fbHeight - ( vkDbg.state.scissor[ 1 ] + vkDbg.state.scissor[ 3 ] ) );
		const int x2 = Min( vkDbg.fbWidth, vkDbg.state.scissor[ 0 ] + vkDbg.state.scissor[ 2 ] );
		const int y2 = Min( vkDbg.fbHeight, VK_Exec_ActiveLowerOrigin()
			? vkDbg.state.scissor[ 1 ] + vkDbg.state.scissor[ 3 ]
			: vkDbg.fbHeight - vkDbg.state.scissor[ 1 ] );
		if ( x2 <= x || y2 <= top ) {
			return;
		}
		rect.rect.offset.x = x;
		rect.rect.offset.y = top;
		rect.rect.extent.width = (uint32_t)( x2 - x );
		rect.rect.extent.height = (uint32_t)( y2 - top );
	} else {
		rect.rect.extent.width = (uint32_t)vkDbg.fbWidth;
		rect.rect.extent.height = (uint32_t)vkDbg.fbHeight;
	}
	vkCmdClearAttachments( vkDbg.cmd, count, attachments, 1, &rect );
	vkDbg.drew = true;
}

void VK_DebugGL_MatrixMode( GLenum mode ) {
	if ( vkDbg.active ) {
		vkDbg.matrixMode = mode;
	}
}

void VK_DebugGL_LoadMatrixf( const float *m ) {
	if ( vkDbg.active && m != NULL ) {
		memcpy( VK_Debug_CurrentMatrix(), m, sizeof( float ) * 16 );
	}
}

void VK_DebugGL_LoadIdentity( void ) {
	if ( vkDbg.active ) {
		memcpy( VK_Debug_CurrentMatrix(), vkDebugIdentity, sizeof( vkDebugIdentity ) );
	}
}

void VK_DebugGL_PushMatrix( void ) {
	if ( !vkDbg.active ) {
		return;
	}
	if ( vkDbg.matrixMode == GL_PROJECTION ) {
		if ( vkDbg.projectionDepth + 1 < VK_DEBUG_MATRIX_STACK ) {
			memcpy( vkDbg.projection[ vkDbg.projectionDepth + 1 ], vkDbg.projection[ vkDbg.projectionDepth ], sizeof( float ) * 16 );
			vkDbg.projectionDepth++;
		}
	} else if ( vkDbg.modelViewDepth + 1 < VK_DEBUG_MATRIX_STACK ) {
		memcpy( vkDbg.modelView[ vkDbg.modelViewDepth + 1 ], vkDbg.modelView[ vkDbg.modelViewDepth ], sizeof( float ) * 16 );
		vkDbg.modelViewDepth++;
	}
}

void VK_DebugGL_PopMatrix( void ) {
	if ( !vkDbg.active ) {
		return;
	}
	if ( vkDbg.matrixMode == GL_PROJECTION ) {
		if ( vkDbg.projectionDepth > 0 ) {
			vkDbg.projectionDepth--;
		}
	} else if ( vkDbg.modelViewDepth > 0 ) {
		vkDbg.modelViewDepth--;
	}
}

void VK_DebugGL_Ortho( double left, double right, double bottom, double top, double zNear, double zFar ) {
	if ( !vkDbg.active ) {
		return;
	}
	float ortho[ 16 ];
	memset( ortho, 0, sizeof( ortho ) );
	ortho[ 0 ] = (float)( 2.0 / ( right - left ) );
	ortho[ 5 ] = (float)( 2.0 / ( top - bottom ) );
	ortho[ 10 ] = (float)( -2.0 / ( zFar - zNear ) );
	ortho[ 12 ] = (float)( -( right + left ) / ( right - left ) );
	ortho[ 13 ] = (float)( -( top + bottom ) / ( top - bottom ) );
	ortho[ 14 ] = (float)( -( zFar + zNear ) / ( zFar - zNear ) );
	ortho[ 15 ] = 1.0f;
	float *current = VK_Debug_CurrentMatrix();
	float result[ 16 ];
	myGlMultMatrix( ortho, current, result );
	memcpy( current, result, sizeof( result ) );
}

void VK_DebugGL_PushAttrib( void ) {
	if ( vkDbg.active && vkDbg.attribDepth < VK_DEBUG_ATTRIB_STACK ) {
		vkDbg.attribStack[ vkDbg.attribDepth++ ] = vkDbg.state;
	}
}

void VK_DebugGL_PopAttrib( void ) {
	if ( vkDbg.active && vkDbg.attribDepth > 0 ) {
		vkDbg.state = vkDbg.attribStack[ --vkDbg.attribDepth ];
	}
}

void VK_DebugGL_RasterPos2f( float x, float y ) {
	if ( vkDbg.active ) {
		float window[ 2 ];
		if ( VK_Debug_ProjectToWindow( x, y, 0.0f, window ) ) {
			vkDbg.rasterPos[ 0 ] = window[ 0 ];
			vkDbg.rasterPos[ 1 ] = window[ 1 ];
		}
	}
}

static void VK_Debug_PixelImage( idImage *image ) {
	idImageOpts opts;
	opts.textureType = TT_2D;
	opts.format = FMT_RGBA8;
	opts.width = 32;
	opts.height = 32;
	opts.numLevels = 1;
	image->AllocImage( opts, TF_NEAREST, TR_CLAMP );
}

// glDrawPixels: the rows are uploaded bottom-up as given, so texture row 0
// lands at the bottom of the rectangle, where OpenGL puts the first row
void VK_DebugGL_DrawPixels( int width, int height, GLenum format, GLenum type, const void *pixels ) {
	if ( !vkDbg.active || pixels == NULL || width <= 0 || height <= 0
			|| format != GL_RGBA || type != GL_UNSIGNED_BYTE || globalImages == NULL ) {
		return;
	}
	const int slot = vkDbg.pixelImageNext;
	vkDbg.pixelImageNext = ( vkDbg.pixelImageNext + 1 ) % VK_DEBUG_PIXEL_IMAGES;
	if ( vkDbg.pixelImages[ slot ] == NULL ) {
		vkDbg.pixelImages[ slot ] = globalImages->ImageFromFunction( slot == 0 ? "_vkDebugPixels0" : "_vkDebugPixels1",
				VK_Debug_PixelImage );
	}
	idImage *image = vkDbg.pixelImages[ slot ];
	if ( image == NULL ) {
		return;
	}
	idImageOpts opts;
	opts.textureType = TT_2D;
	opts.format = FMT_RGBA8;
	opts.width = width;
	opts.height = height;
	opts.numLevels = 1;
	if ( image->GetUploadWidth() != width || image->GetUploadHeight() != height ) {
		image->AllocImage( opts, TF_NEAREST, TR_CLAMP );
	}
	image->SubImageUpload( 0, 0, 0, 0, width, height, pixels );

	// a quad over the pixel rectangle, in the view's normalized coordinates
	const viewDef_t *viewDef = vkDbg.viewDef;
	const float vpX = (float)viewDef->viewport.x1;
	const float vpY = (float)viewDef->viewport.y1;
	const float vpW = (float)( viewDef->viewport.x2 - viewDef->viewport.x1 + 1 );
	const float vpH = (float)( viewDef->viewport.y2 - viewDef->viewport.y1 + 1 );
	const float x0 = ( vkDbg.rasterPos[ 0 ] - vpX ) / vpW * 2.0f - 1.0f;
	const float y0 = ( vkDbg.rasterPos[ 1 ] - vpY ) / vpH * 2.0f - 1.0f;
	const float x1 = ( vkDbg.rasterPos[ 0 ] + width - vpX ) / vpW * 2.0f - 1.0f;
	const float y1 = ( vkDbg.rasterPos[ 1 ] + height - vpY ) / vpH * 2.0f - 1.0f;

	idList<vkDebugVert_t> quad;
	const float corners[ 6 ][ 4 ] = {
		{ x0, y0, 0.0f, 0.0f }, { x1, y0, 1.0f, 0.0f }, { x1, y1, 1.0f, 1.0f },
		{ x0, y0, 0.0f, 0.0f }, { x1, y1, 1.0f, 1.0f }, { x0, y1, 0.0f, 1.0f }
	};
	for ( int i = 0; i < 6; i++ ) {
		vkDebugVert_t vert;
		vert.xyzw[ 0 ] = corners[ i ][ 0 ];
		vert.xyzw[ 1 ] = corners[ i ][ 1 ];
		vert.xyzw[ 2 ] = 0.0f;
		vert.xyzw[ 3 ] = 1.0f;
		vert.rgba[ 0 ] = vert.rgba[ 1 ] = vert.rgba[ 2 ] = vert.rgba[ 3 ] = 255;
		vert.st[ 0 ] = corners[ i ][ 2 ];
		vert.st[ 1 ] = corners[ i ][ 3 ];
		quad.Append( vert );
	}
	idImage *previousImage = vkDbg.boundImage;
	const bool previousTexture = vkDbg.state.texture2D;
	const int previousBits = vkDbg.state.stateBits;
	vkDbg.boundImage = image;
	vkDbg.state.texture2D = true;
	vkDbg.state.stateBits &= ~( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS | GLS_POLYMODE_LINE );
	float identity[ 16 ];
	VK_FixupClipSpaceZ( identity, vkDebugIdentity );
	VK_Debug_Draw( quad, 0, identity );
	vkDbg.boundImage = previousImage;
	vkDbg.state.texture2D = previousTexture;
	vkDbg.state.stateBits = previousBits;
}

// idImage::Bind and BindNull in the debug tools (RB_BindDebugImage, tr_local.h)
void VK_DebugGL_BindImage( idImage *image ) {
	if ( vkDbg.active ) {
		vkDbg.boundImage = image;
	}
}

// GL_State / GL_Cull (vk_Backend.cpp forwards them here)
void VK_DebugGL_State( int stateBits ) {
	if ( vkDbg.active ) {
		vkDbg.state.stateBits = stateBits;
	}
}

void VK_DebugGL_Cull( int cullType ) {
	if ( vkDbg.active ) {
		vkDbg.state.cullType = cullType;
		vkDbg.state.cullFace = cullType != CT_TWO_SIDED;
	}
}

/*
===============================================================================

	tr_render.cpp helpers the debug tools call. The OpenGL versions live in
	tr_render.cpp, which the Vulkan module does not build; these are the
	same code, drawing through the emulation above.

===============================================================================
*/

void RB_DrawElementsImmediate( const srfTriangles_t *tri ) {
	const idDrawVert *drawVerts = tri->verts;
	const glIndex_t *drawIndexes = tri->indexes;
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	if ( tri->primBatchMesh != NULL && ( drawVerts == NULL || drawIndexes == NULL ) ) {
		if ( tri->numVerts <= 0 || tri->numIndexes <= 0 ) {
			return;
		}
		idDrawVert *tempVerts = (idDrawVert *)R_FrameAlloc( tri->numVerts * sizeof( tempVerts[0] ) );
		glIndex_t *tempIndexes = (glIndex_t *)R_FrameAlloc( tri->numIndexes * sizeof( tempIndexes[0] ) );
		renderSystem->CopyPrimBatchTriangles( tempVerts, tempIndexes, tri->primBatchMesh, tri->silTraceVerts );
		drawVerts = tempVerts;
		drawIndexes = tempIndexes;
	}
#endif
	if ( drawVerts == NULL || drawIndexes == NULL ) {
		return;
	}
	glBegin( GL_TRIANGLES );
	for ( int i = 0; i < tri->numIndexes; i++ ) {
		glTexCoord2fv( drawVerts[ drawIndexes[ i ] ].st.ToFloatPtr() );
		glVertex3fv( drawVerts[ drawIndexes[ i ] ].xyz.ToFloatPtr() );
	}
	glEnd();
}

void RB_DrawElementsWithCounters( const srfTriangles_t *tri ) {
	backEnd.pc.c_drawElements++;
	backEnd.pc.c_drawIndexes += tri->numIndexes;
	backEnd.pc.c_drawVertexes += tri->numVerts;
	const void *indexes = tri->indexes;
	if ( tri->indexCache != NULL && r_useIndexBuffers.GetBool() ) {
		indexes = vertexCache.Position( tri->indexCache );
	}
	glDrawElements( GL_TRIANGLES, r_singleTriangle.GetBool() ? 3 : tri->numIndexes, GL_INDEX_TYPE, indexes );
}

static bool VK_Debug_EnsurePackedCaches( const srfTriangles_t *tri, bool needsLighting ) {
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	if ( tri == NULL || tri->primBatchMesh == NULL ) {
		return true;
	}
	srfTriangles_t *mutableTri = const_cast<srfTriangles_t *>( tri );
	if ( mutableTri->ambientCache == NULL
			&& !R_CreatePackedSurfaceFrameCaches( mutableTri, needsLighting, true ) ) {
		return false;
	}
	R_TouchVertexCache( mutableTri->ambientCache );
	if ( mutableTri->indexCache != NULL ) {
		R_TouchVertexCache( mutableTri->indexCache );
	}
#else
	(void)tri;
	(void)needsLighting;
#endif
	return true;
}

void RB_RenderTriangleSurface( const srfTriangles_t *tri ) {
	if ( !VK_Debug_EnsurePackedCaches( tri, false ) ) {
		return;
	}
	if ( !tri->ambientCache ) {
		RB_DrawElementsImmediate( tri );
		return;
	}
	idDrawVert *ac = (idDrawVert *)vertexCache.Position( tri->ambientCache );
	glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, xyz ) ) );
	glTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, st ) ) );
	RB_DrawElementsWithCounters( tri );
}

void RB_T_RenderTriangleSurface( const drawSurf_t *surf ) {
	if ( !VK_Debug_EnsurePackedCaches( surf->geo,
			surf->material != NULL ? surf->material->ReceivesLighting() : false ) ) {
		return;
	}
	RB_RenderTriangleSurface( surf->geo );
}

static float VK_Debug_CalcFovForAspect( float fovX, float width, float height ) {
	const float clampedFovX = idMath::ClampFloat( 0.75f, 179.0f, fovX );
	const float safeWidth = Max( width, 1.0f );
	const float safeHeight = Max( height, 1.0f );
	const float x = safeWidth / idMath::Tan( DEG2RAD( clampedFovX ) * 0.5f );
	return RAD2DEG( idMath::ATan( safeHeight / Max( x, idMath::FLOAT_EPSILON ) ) ) * 2.0f;
}

void R_GetDepthHackProjectionMatrix( const viewDef_t *viewDef, bool weaponDepthHack, float modelDepthHack, float matrix[16] ) {
	memcpy( matrix, viewDef->projectionMatrix, sizeof( float ) * 16 );
	if ( modelDepthHack != 0.0f ) {
		matrix[14] -= modelDepthHack;
		return;
	}
	if ( !weaponDepthHack ) {
		return;
	}
	const float weaponFovOverride = cl_gunfov.GetFloat();
	if ( weaponFovOverride > 0.0f ) {
		const float viewportWidth = static_cast<float>( Max( 1, viewDef->viewport.x2 - viewDef->viewport.x1 + 1 ) );
		const float viewportHeight = static_cast<float>( Max( 1, viewDef->viewport.y2 - viewDef->viewport.y1 + 1 ) );
		float weaponFovX = idMath::ClampFloat( 30.0f, 160.0f, weaponFovOverride );
		float weaponFovY = 0.0f;
		if ( cl_gunfov_adjust.GetBool() ) {
			weaponFovY = VK_Debug_CalcFovForAspect( weaponFovX, 4.0f, 3.0f );
			weaponFovX = VK_Debug_CalcFovForAspect( weaponFovY, viewportHeight, viewportWidth );
		} else {
			weaponFovY = VK_Debug_CalcFovForAspect( weaponFovX, viewportWidth, viewportHeight );
		}
		weaponFovX = idMath::ClampFloat( 1.0f, 179.0f, weaponFovX );
		weaponFovY = idMath::ClampFloat( 1.0f, 179.0f, weaponFovY );
		matrix[0] = 1.0f / idMath::Tan( DEG2RAD( weaponFovX ) * 0.5f );
		matrix[5] = 1.0f / idMath::Tan( DEG2RAD( weaponFovY ) * 0.5f );
	}
	matrix[14] *= 0.25f;
}

void RB_EnterWeaponDepthHack() {
	glDepthRange( 0.0f, 0.5f );
	float matrix[16];
	R_GetDepthHackProjectionMatrix( backEnd.viewDef, true, 0.0f, matrix );
	glMatrixMode( GL_PROJECTION );
	glLoadMatrixf( matrix );
	glMatrixMode( GL_MODELVIEW );
}

void RB_EnterModelDepthHack( float depth ) {
	glDepthRange( 0.0f, 1.0f );
	float matrix[16];
	R_GetDepthHackProjectionMatrix( backEnd.viewDef, false, depth, matrix );
	glMatrixMode( GL_PROJECTION );
	glLoadMatrixf( matrix );
	glMatrixMode( GL_MODELVIEW );
}

void RB_LeaveDepthHack() {
	glDepthRange( 0, 1 );
	glMatrixMode( GL_PROJECTION );
	glLoadMatrixf( backEnd.viewDef->projectionMatrix );
	glMatrixMode( GL_MODELVIEW );
}

void RB_RenderDrawSurfListWithFunction( drawSurf_t **drawSurfs, int numDrawSurfs,
		void (*triFunc_)( const drawSurf_t * ) ) {
	backEnd.currentSpace = NULL;
	for ( int i = 0; i < numDrawSurfs; i++ ) {
		const drawSurf_t *drawSurf = drawSurfs[i];
		if ( drawSurf->space != backEnd.currentSpace ) {
			glLoadMatrixf( drawSurf->space->modelViewMatrix );
		}
		if ( drawSurf->space->weaponDepthHack ) {
			RB_EnterWeaponDepthHack();
		}
		if ( drawSurf->space->modelDepthHack != 0.0f ) {
			RB_EnterModelDepthHack( drawSurf->space->modelDepthHack );
		}
		if ( r_useScissor.GetBool() && !backEnd.currentScissor.Equals( drawSurf->scissorRect ) ) {
			backEnd.currentScissor = drawSurf->scissorRect;
			glScissor( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
				backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
				backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
				backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
		}
		triFunc_( drawSurf );
		if ( drawSurf->space->weaponDepthHack || drawSurf->space->modelDepthHack != 0.0f ) {
			RB_LeaveDepthHack();
		}
		backEnd.currentSpace = drawSurf->space;
	}
}

/*
===============================================================================

	Entry points

===============================================================================
*/

bool VK_PostProcess_DrawDebugView( const viewDef_t *viewDef, int mode );

/*
====================
RB_ShowIntensity / RB_ShowDepthBuffer

tr_rendertools.cpp reads the framebuffer back, recolours it on the CPU and
draws it again; here the same mapping is a full-screen pass over a copy of
the target (post_debug_view.frag).
====================
*/
void RB_ShowIntensity( void ) {
	if ( !r_showIntensity.GetBool() || !vkDbg.active ) {
		return;
	}
	if ( VK_PostProcess_DrawDebugView( vkDbg.viewDef, 0 ) ) {
		vkDbg.drew = true;
	}
}

void RB_ShowDepthBuffer( void ) {
	if ( !r_showDepth.GetBool() || !vkDbg.active ) {
		return;
	}
	if ( VK_PostProcess_DrawDebugView( vkDbg.viewDef, 1 ) ) {
		vkDbg.drew = true;
	}
}

/*
====================
RB_CountStencilBuffer / RB_ScanStencilBuffer

The overdraw average (r_showLightCount 3, r_showShadowCount 2-4) and the
stencil histogram. OpenGL reads the stencil back on the spot; Vulkan queues
a copy of it and VK_DebugTools_PrintStencilReadback prints the same lines
when the frame's fence has signalled, a frame or two later.
====================
*/
static void VK_Debug_QueueStencilReadback( int mode ) {
	const char *failure = NULL;
	if ( !vkDbg.active ) {
		failure = "the debug tools are not active";
	} else if ( VK_Exec_QueueStencilReadback( mode, &failure ) ) {
		return;
	}
	common->Printf( "%s: the stencil buffer could not be read back (%s)\n",
			mode == VK_STENCIL_READBACK_SCAN ? "stencil values" : "overdraw",
			failure != NULL ? failure : "unknown reason" );
}

void RB_CountStencilBuffer( void ) {
	VK_Debug_QueueStencilReadback( VK_STENCIL_READBACK_COUNT );
}

void RB_ScanStencilBuffer( void ) {
	VK_Debug_QueueStencilReadback( VK_STENCIL_READBACK_SCAN );
}

void VK_DebugTools_PrintStencilReadback( const byte *stencil, int width, int height, int mode ) {
	const int pixels = width * height;
	if ( stencil == NULL || pixels <= 0 ) {
		return;
	}
	if ( mode == VK_STENCIL_READBACK_SCAN ) {
		int counts[ 256 ];
		memset( counts, 0, sizeof( counts ) );
		for ( int i = 0; i < pixels; i++ ) {
			counts[ stencil[ i ] ]++;
		}
		common->Printf( "stencil values:\n" );
		for ( int i = 0; i < 255; i++ ) {
			if ( counts[ i ] ) {
				common->Printf( "%i: %i\n", i, counts[ i ] );
			}
		}
		return;
	}
	double count = 0.0;
	for ( int i = 0; i < pixels; i++ ) {
		count += stencil[ i ];
	}
	common->Printf( "overdraw: %5.1f\n", (float)( count / pixels ) );
}

// OpenGL's state at RB_RenderDebugTools: the view's projection, the world
// modelview slot free, depth testing on, front-sided culling, texturing on.
// The scissor test starts off here only until RB_RenderDebugTools enables it:
// the passes ahead of the tools leave GL_SCISSOR_TEST in either state (SMAA
// disables it), so the shared tools set it themselves on both backends.
static void VK_Debug_ResetState( const viewDef_t *viewDef ) {
	memset( &vkDbg.state, 0, sizeof( vkDbg.state ) );
	vkDbg.state.stateBits = GLS_DEFAULT;
	vkDbg.state.cullType = CT_FRONT_SIDED;
	vkDbg.state.cullFace = true;
	vkDbg.state.depthTest = true;
	vkDbg.state.scissorTest = false;
	vkDbg.state.texture2D = true;
	vkDbg.state.depthNear = 0.0f;
	vkDbg.state.depthFar = 1.0f;
	vkDbg.state.stencilFunc = GL_ALWAYS;
	vkDbg.state.stencilRef = 128;
	vkDbg.state.stencilMask = 255;
	vkDbg.state.stencilFail = GL_KEEP;
	vkDbg.state.stencilDepthFail = GL_KEEP;
	vkDbg.state.stencilPass = GL_KEEP;
	vkDbg.state.lineWidth = 1.0f;
	vkDbg.state.pointSize = 1.0f;
	vkDbg.state.color[ 0 ] = vkDbg.state.color[ 1 ] = vkDbg.state.color[ 2 ] = vkDbg.state.color[ 3 ] = 1.0f;
	vkDbg.state.scissor[ 0 ] = viewDef->viewport.x1 + viewDef->scissor.x1;
	vkDbg.state.scissor[ 1 ] = viewDef->viewport.y1 + viewDef->scissor.y1;
	vkDbg.state.scissor[ 2 ] = viewDef->scissor.x2 + 1 - viewDef->scissor.x1;
	vkDbg.state.scissor[ 3 ] = viewDef->scissor.y2 + 1 - viewDef->scissor.y1;
	vkDbg.attribDepth = 0;
	vkDbg.matrixMode = GL_MODELVIEW;
	vkDbg.modelViewDepth = 0;
	vkDbg.projectionDepth = 0;
	memcpy( vkDbg.modelView[ 0 ], viewDef->worldSpace.modelViewMatrix, sizeof( float ) * 16 );
	memcpy( vkDbg.projection[ 0 ], viewDef->projectionMatrix, sizeof( float ) * 16 );
	vkDbg.inBegin = false;
	vkDbg.verts.SetNum( 0, false );
	vkDbg.texCoord[ 0 ] = vkDbg.texCoord[ 1 ] = 0.0f;
	vkDbg.vertexArray = NULL;
	vkDbg.colorArray = NULL;
	vkDbg.texCoordArray = NULL;
	vkDbg.colorArrayEnabled = false;
	vkDbg.texCoordArrayEnabled = false;
	vkDbg.clearColor[ 0 ] = vkDbg.clearColor[ 1 ] = vkDbg.clearColor[ 2 ] = vkDbg.clearColor[ 3 ] = 0.0f;
	vkDbg.clearStencil = 0;
	vkDbg.boundImage = NULL;
	vkDbg.rasterPos[ 0 ] = (float)viewDef->viewport.x1;
	vkDbg.rasterPos[ 1 ] = (float)viewDef->viewport.y1;
	vkDbg.pixelImageNext = 0;
}

/*
====================
VK_DebugTools_DrawView

RB_RenderDebugTools for one 3D view, at the end of RB_STD_DrawView's world:
after the post-process surfaces and the underwater view, over whatever
target the view is drawing into, depth attachment still intact.
====================
*/
void VK_DebugTools_DrawView( const viewDef_t *viewDef ) {
	if ( viewDef == NULL || viewDef->viewEntitys == NULL || !VK_GuiExecutor_FrameIsOpen()
			|| !VK_Exec_MainRenderingScopeOpen() ) {
		return;
	}
	vkDbg.active = true;
	vkDbg.viewDef = viewDef;
	vkDbg.cmd = VK_Exec_ActiveCmd();
	vkDbg.fbWidth = VK_Exec_ActiveFramebufferWidth();
	vkDbg.fbHeight = VK_Exec_ActiveFramebufferHeight();
	vkDbg.drew = false;
	VK_Debug_ResetState( viewDef );

	RB_RenderDebugTools( viewDef->drawSurfs, viewDef->numDrawSurfs );

	vkDbg.active = false;
	vkDbg.viewDef = NULL;
	if ( vkDbg.drew ) {
		// leave the 3D view's baseline for what follows
		VK_Exec_SetViewViewport( vkDbg.cmd, viewDef, 1.0f );
		vkCmdSetDepthBiasEnable( vkDbg.cmd, VK_FALSE );
		vkCmdSetStencilTestEnable( vkDbg.cmd, VK_FALSE );
		static bool logged = false;
		if ( !logged ) {
			logged = true;
			common->Printf( "Vulkan: first debug tools pass drew\n" );
		}
	}
}

#endif /* OPENQ4_RENDERER_VK_MODULE */
