/*
===============================================================================
	Vulkan full-screen post passes.

	These are the Vulkan versions of OpenGL post passes that live in
	draw_common.cpp, which the renderer-vk module does not build.

	Back-buffer passes: RB_SwapBuffers (tr_backend.cpp) finishes every OpenGL
	frame by running RB_ApplyCRTToBackBuffer and then
	RB_ApplyColorMappingsToBackBuffer over the whole back buffer, HUD and
	menus included, and screenshots read the result. The Vulkan backend
	draws straight into the swapchain image, so the CRT pass copies that
	image out with VK_Exec_CopyRender and draws the copy back through a
	full-screen shader. vk_Backend.cpp runs it from the RC_SWAP_BUFFERS
	handler, before the frame is presented or read back. r_brightness and
	r_gamma have no pass here: the executor's display mapping
	(VK_DisplayColorMapping_Apply, vk_GuiExecutor.cpp) applies them once per
	composition, after CRT, at presentation or screenshot readback. A pass
	here as well would map every frame twice.

	Scene passes: RB_STD_DrawView runs SSAO, motion blur, bloom with the HDR
	tone map, and the cel world ink over the main 3D view after the post-fog
	material passes and before the SS_POST_PROCESS surfaces.
	VK_GuiExecutor_Draw3DView calls VK_PostProcess_DrawSceneEffects at the
	same point. Each pass copies the scene out of whatever target the view is
	drawing into (the swapchain, the game's own render texture or the scaled
	scene target) and draws the result back over the view rectangle. The
	intermediate targets (bloom levels, motion vectors) are RGBA16F like
	OpenGL's. Explicit HDR tone mapping selects an RGBA16F scene target when
	r_hdrSceneTarget is enabled. Luminance reduction precedes tone mapping;
	the normal exposure readback retires behind the existing frame-slot fence.

	Orientation: every sampled image is stored bottom-up like an OpenGL
	texture (VK_Exec_CopyRender flips its captures, and the intermediate
	targets are drawn with a positive-height viewport). Passes that draw into
	the scene use a negative-height viewport. Either way the full-screen
	vertex shader's fragUV is OpenGL's texture coordinate, so the shaders
	keep the OpenGL math unchanged. The back-buffer passes predate this and
	flip in the shader instead.

	Every pass uses the interaction pipeline layout: single-sampler sets 0-5
	plus the dynamic uniform slice on set 6.
===============================================================================
*/

#ifdef OPENQ4_RENDERER_VK_MODULE

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../tr_local.h"
#include "VulkanGpuFrameTiming.h"
#include "../CelShading.h"
#include "../ScenePackets.h"
#include "../HDRExposureCore.h"
#include "../ModernGLExecutor.h"
#include "../TemporalPresentation.h"
#include "../MotionBlurCore.h"

#undef snprintf
#undef vsnprintf
#include <cstdio>
#include <cstring>
#include "volk.h"

#include "VulkanDevice.h"
#include "vk_ExecutorHooks.h"
#include "vk_HDRScene.h"
#include "vk_Image.h"
#include "shaders/post_shaders_spv.h"
#include "shaders/hdr_luminance_spv.h"

extern idCVar r_skipPostProcess;
extern idCVar r_crt;
extern idCVar r_crtAmount;
extern idCVar r_crtScanlineStrength;
extern idCVar r_crtMaskStrength;
extern idCVar r_crtCurvature;
extern idCVar r_crtChromatic;
extern idCVar r_ssao;
extern idCVar r_ssaoRadius;
extern idCVar r_ssaoBias;
extern idCVar r_ssaoIntensity;
extern idCVar r_ssaoPower;
extern idCVar r_ssaoMaxDistance;
extern idCVar r_ssaoSamples;
extern idCVar r_ssaoDebug;
extern idCVar r_bloom;
extern idCVar r_bloomThreshold;
extern idCVar r_bloomSoftKnee;
extern idCVar r_bloomIntensity;
extern idCVar r_bloomRadius;
extern idCVar r_bloomMipCount;
extern idCVar r_hdrToneMap;
extern idCVar r_hdrExposure;
extern idCVar r_hdrWhitePoint;
extern idCVar r_hdrLift;
extern idCVar r_hdrPostGamma;
extern idCVar r_hdrGain;
extern idCVar r_hdrVibrance;
extern idCVar r_hdrSaturation;
extern idCVar r_hdrContrast;
extern idCVar r_hdrHighlightDesaturation;
extern idCVar r_hdrGamutCompression;
extern idCVar r_hdrDebugView;
extern idCVar r_hdrSceneTarget;
extern idCVar r_hdrAutoExposure;
extern idCVar r_hdrAutoExposureAsync;
extern idCVar r_hdrKeyValue;
extern idCVar r_hdrMinExposure;
extern idCVar r_hdrMaxExposure;
extern idCVar r_hdrAdaptUpSpeed;
extern idCVar r_hdrAdaptDownSpeed;
static idCVar r_vkSSAODeferredNormal( "r_vkSSAODeferredNormal", "0", CVAR_RENDERER | CVAR_BOOL,
	"defer SSAO normal reconstruction until after its distance fade rejects untouched pixels" );
static idCVar r_vkHDRPrepareFailure( "r_vkHDRPrepareFailure", "0", CVAR_RENDERER | CVAR_INTEGER,
	"diagnostic linear HDR preparation failure: 0=off, 1=exposure, 2=bloom, 3=output", 0, 3 );
static idCVar r_vkPostDepthTransitionReference( "r_vkPostDepthTransitionReference", "0", CVAR_RENDERER | CVAR_BOOL,
	"transition unused scene depth for post-processing intermediates as a validation reference" );
extern idCVar r_motionBlur;
extern idCVar r_motionBlurStrength;
extern idCVar r_motionBlurMaxPixels;
extern idCVar r_motionBlurSamples;
extern idCVar r_motionBlurDebug;
extern idCVar r_motionBlurObjectVectors;
extern idCVar r_jitter;
extern idCVar r_useScissor;
extern idCVar r_celShadingWorldDebug;
extern idCVar r_underwater;
extern idCVar r_underwaterWarp;
extern idCVar r_underwaterBlur;
extern idCVar r_underwaterEdgeSoften;
extern idCVar r_underwaterCaustics;
extern idCVar r_underwaterBloom;
extern idCVar r_underwaterAberration;
extern idCVar r_underwaterParticles;
extern idCVar r_underwaterVisibility;

// pipeline cache keys for VK_Exec_PostPipeline / VK_Exec_ExtraPipeline.
// vk_SceneEffects.cpp and vk_DebugTools.cpp use 32 and up.
enum vkPostPassKind_t {
	VK_POST_CRT = 1,
	VK_POST_SSAO,
	VK_POST_BLOOM_EXTRACT,
	VK_POST_BLOOM_DOWNSAMPLE,
	VK_POST_BLOOM_BLUR,
	VK_POST_BLOOM_COMPOSITE,
	VK_POST_MOTION_BLUR,
	VK_POST_MOTION_VECTORS,
	VK_POST_CEL_OUTLINE,
	VK_POST_UNDERWATER,
	VK_POST_DEBUG_VIEW,
	VK_POST_HDR_LUMINANCE,
	VK_POST_MOTION_VECTORS_PREVIOUS,
	VK_POST_MOTION_VECTORS_REACTIVE,
	VK_POST_SKY_MASK
};

// scene shader modules, created the first time a pass needs one
enum vkPostSceneModule_t {
	VK_POST_MODULE_SSAO,
	VK_POST_MODULE_BLOOM_EXTRACT,
	VK_POST_MODULE_BLOOM_DOWNSAMPLE,
	VK_POST_MODULE_BLOOM_BLUR,
	VK_POST_MODULE_BLOOM_COMPOSITE,
	VK_POST_MODULE_MOTION_BLUR,
	VK_POST_MODULE_MOTION_VECTORS_VERT,
	VK_POST_MODULE_MOTION_VECTORS_FRAG,
	VK_POST_MODULE_CEL_OUTLINE,
	VK_POST_MODULE_UNDERWATER,
	VK_POST_MODULE_DEBUG_VIEW,
	VK_POST_MODULE_HDR_LUMINANCE,
	VK_POST_MODULE_MOTION_VECTORS_PREVIOUS_VERT,
	VK_POST_MODULE_MOTION_VECTORS_REACTIVE_VERT,
	VK_POST_MODULE_MOTION_VECTORS_REACTIVE_FRAG,
	VK_POST_MODULE_SKY_MASK,
	VK_POST_SCENE_MODULE_COUNT
};

typedef struct vkPostModuleSource_s {
	const unsigned char *	code;
	unsigned int			size;
	const char *			name;
} vkPostModuleSource_t;

static const vkPostModuleSource_t vkPostSceneModuleSources[ VK_POST_SCENE_MODULE_COUNT ] = {
	{ vk_post_ssao_frag_spv, vk_post_ssao_frag_spv_size, "post SSAO fragment" },
	{ vk_post_bloom_extract_frag_spv, vk_post_bloom_extract_frag_spv_size, "post bloom extract fragment" },
	{ vk_post_bloom_downsample_frag_spv, vk_post_bloom_downsample_frag_spv_size, "post bloom downsample fragment" },
	{ vk_post_bloom_blur_frag_spv, vk_post_bloom_blur_frag_spv_size, "post bloom blur fragment" },
	{ vk_post_bloom_composite_frag_spv, vk_post_bloom_composite_frag_spv_size, "post bloom composite fragment" },
	{ vk_post_motionblur_frag_spv, vk_post_motionblur_frag_spv_size, "post motion blur fragment" },
	{ vk_post_motionvectors_vert_spv, vk_post_motionvectors_vert_spv_size, "post motion vector vertex" },
	{ vk_post_motionvectors_frag_spv, vk_post_motionvectors_frag_spv_size, "post motion vector fragment" },
	{ vk_post_celoutline_frag_spv, vk_post_celoutline_frag_spv_size, "post cel outline fragment" },
	{ vk_post_underwater_frag_spv, vk_post_underwater_frag_spv_size, "post underwater fragment" },
	{ vk_post_debug_view_frag_spv, vk_post_debug_view_frag_spv_size, "post debug view fragment" },
	{ vk_post_hdr_luminance_frag_spv, vk_post_hdr_luminance_frag_spv_size, "HDR luminance reduction fragment" },
	{ vk_post_motionvectors_previous_vert_spv, vk_post_motionvectors_previous_vert_spv_size, "post posed motion vector vertex" },
	{ vk_post_motionvectors_reactive_vert_spv, vk_post_motionvectors_reactive_vert_spv_size, "post reactive coverage vertex" },
	{ vk_post_motionvectors_reactive_frag_spv, vk_post_motionvectors_reactive_frag_spv_size, "post reactive coverage fragment" },
	{ vk_post_skymask_frag_spv, vk_post_skymask_frag_spv_size, "post portal-sky mask fragment" }
};

// RB_BLOOM_MAX_LEVELS and RB_BLOOM_BASE_WEIGHTS (draw_common.cpp)
static const int VK_POST_BLOOM_MAX_LEVELS = 5;
static const float VK_POST_BLOOM_BASE_WEIGHTS[ VK_POST_BLOOM_MAX_LEVELS ] = {
	0.34f, 0.24f, 0.17f, 0.14f, 0.11f
};

// rbMotionBlurViewState_t (draw_common.cpp)
typedef struct vkPostMotionViewState_s {
	const idRenderWorldLocal *	renderWorld;
	idStr						mapName;
	int							videoRestartCount;
	int							viewportWidth;
	int							viewportHeight;
	int							renderTime;
	float						fovX;
	float						fovY;
	idVec3						viewOrigin;
	idVec3						viewAxis[ 3 ];
	float						reconstructInfo[ 4 ];
	float						projectInfo[ 4 ];
	float						depthProjection[ 2 ];
	float						projectionMatrix[ 16 ];
	float						worldModelViewMatrix[ 16 ];
	float						neutralProjectOffset[ 2 ];	// projectInfo[2]/[3] without TAA jitter
	bool						forceUpdate;
} vkPostMotionViewState_t;

typedef struct vkPostMotionEntityHistory_s {
	int							entityIndex;
	const idRenderModel *		model;
	float						modelMatrix[ 16 ];
} vkPostMotionEntityHistory_t;

// World-depth snapshot bits for VK_PostProcess_WorldDepthCaptures
static const int VK_POST_CAPTURE_CEL_WORLD = 1 << 0;
static const int VK_POST_CAPTURE_SSAO_WORLD = 1 << 1;
// The view's native PBR world surfaces fill after the classic ones, with a
// snapshot between (VK_PostProcess_PrepareSSAOField).
static const int VK_POST_CAPTURE_SSAO_SPLIT = 1 << 2;

// vk_Interactions.cpp: an opaque or perforated surface native PBR owns.
bool VK_PBR_NativeOpaqueSurface( const drawSurf_t *surf );

typedef struct vkPostSceneState_s {
	VkShaderModule		modules[ VK_POST_SCENE_MODULE_COUNT ];
	bool				moduleFailed[ VK_POST_SCENE_MODULE_COUNT ];

	idImage *			sceneCopy;			// the scene each pass reads, RGBA16F
	idImage *			finalDepth;			// depth after the post-fog passes
	int					finalDepthFrame;
	int					finalDepthView;

	idImage *			ssaoWorldDepth;		// RB_CaptureSSAOWorldDepthImage
	int					ssaoWorldDepthFrame;
	int					ssaoWorldDepthWidth;
	int					ssaoWorldDepthHeight;
	idImage *			celWorldDepth;		// RB_CaptureCelWorldDepthImage
	int					celWorldDepthFrame;
	int					celWorldDepthWidth;
	int					celWorldDepthHeight;

	// Native PBR keeps SSAO off its direct light (VK_PostProcess_PrepareSSAOField)
	idImage *			ssaoClassicDepth;	// the world depth without native PBR surfaces
	int					ssaoClassicDepthFrame;
	idImage *			ssaoFieldDepth;		// the depth right after the prepass
	idImage *			ssaoField;			// r: native PBR indirect AO, g: classic factor, b: world depth
	idRenderTexture *	ssaoFieldTarget;
	const viewDef_t *	ssaoFieldView;
	int					ssaoFieldFrame;
	bool				ssaoFieldFlip;		// its rows run opposite to the view's target

	// Half-resolution classic AO (VK_Post_DrawSSAOHalf): r AO, g view depth
	idImage *			ssaoHalf;
	idRenderTexture *	ssaoHalfTarget;

	// The main view's world depth drawn single-sample before its portal-sky
	// backdrop (VK_PostProcess_PortalSkyMask); its capture is ssaoWorldDepth.
	idImage *			earlyWorldColor;
	idImage *			earlyWorldDepth;
	idRenderTexture *	earlyWorldTarget;
	const viewDef_t *	earlyWorldView;
	int					earlyWorldFrame;

	idImage *			bloomImages[ VK_POST_BLOOM_MAX_LEVELS ][ 2 ];
	idRenderTexture *	bloomTargets[ VK_POST_BLOOM_MAX_LEVELS ][ 2 ];

	idImage *			motionVectorImage;
	idRenderTexture *	motionVectorTarget;
	bool				motionVectorValid;
	bool				motionHistoryValid;
	int					viewSerial;			// counts VK_PostProcess_DrawSceneEffects calls
} vkPostSceneState_t;

static vkPostSceneState_t vkPostScene;
static const int VK_HDR_MAX_LEVELS = 16;
struct vkHDRExposure_t {
	idImage *images[ VK_HDR_MAX_LEVELS ];
	idRenderTexture *targets[ VK_HDR_MAX_LEVELS ];
	hdrExposureState_t adaptation;
	unsigned int generation;
	const idRenderWorldLocal *renderWorld;
	int videoRestartCount;
	idVec3 viewOrigin;
	idVec3 viewForward;
	float viewTime;
	float fovX;
	float fovY;
	bool cameraValid;
	bool sceneFloat;
	bool sceneLinear;
	bool skyPreserved;
	int sceneSamples;
	int width;
	int height;
	int queuedFrame;
	int completedFrame;
	float logLuminance;
	bool enabled;
	bool haveSample;
	unsigned int queuedSamples;
	unsigned int completedSamples;
};
static vkHDRExposure_t vkHDR;
// The adaptation each scene domain (0 display-referred, 1 linear HDR) last
// had. The two meter different values, so a change of domain alone resumes
// the other domain's state instead of restarting at exposure 1, which made
// every admission flip pop. Any other reset trigger forgets both.
static hdrExposureState_t vkHDRDomainExposure[ 2 ];
static bool vkHDRDomainExposureValid[ 2 ];
static unsigned int vkHDRDomainChanges;
static idStr vkHDRMapName;
static int vkPortalSkyFrame = -1;
static idScreenRect vkPortalSkyViewport;
static const idRenderWorldLocal *vkPortalSkyWorld = NULL;
static idRenderTexture *vkPortalSkyTarget = NULL;
static const viewDef_t *vkPortalSkyOwner = NULL;

bool VK_PostProcess_HDRSceneRequested( void ) {
	return !r_skipPostProcess.GetBool() && r_hdrSceneTarget.GetBool()
		&& ( r_hdrToneMap.GetBool() || r_hdrDebugView.GetInteger() > 0 );
}

static void VK_Post_ResetHDRExposure( void ) {
	++vkHDR.generation;
	if ( vkHDR.generation == 0 ) {
		++vkHDR.generation;
	}
	vkHDR.haveSample = false;
	vkHDR.queuedFrame = vkHDR.completedFrame = -1;
	HDRExposure_Reset( vkHDR.adaptation );
}

void VK_PostProcess_ConsumeHDRSample( unsigned int generation, int frame, float logLuminance ) {
	if ( !vkHDR.enabled || generation != vkHDR.generation || frame <= vkHDR.completedFrame
			|| !std::isfinite( logLuminance ) ) {
		return;
	}
	vkHDR.logLuminance = logLuminance;
	vkHDR.completedFrame = frame;
	vkHDR.haveSample = true;
	++vkHDR.completedSamples;
}

int VK_PostProcess_SceneSamples( void ) {
	return vkHDR.sceneSamples;
}

void R_RendererVulkanHDRInfo_f( const idCmdArgs &args ) {
	(void)args;
	common->Printf( "Vulkan HDR: sceneRequested=%d sceneFormat=%s samples=%d autoExposure=%d initialized=%d generation=%u queued=%u completed=%u average=%g target=%g exposure=%g extent=%dx%d async=%d skyPreserved=%d linearScene=%d\n",
		(int)VK_PostProcess_HDRSceneRequested(), vkHDR.sceneFloat ? "RGBA16F" : "LDR", vkHDR.sceneSamples,
		(int)vkHDR.enabled, (int)vkHDR.adaptation.initialized,
		vkHDR.generation, vkHDR.queuedSamples, vkHDR.completedSamples, vkHDR.adaptation.averageLuminance,
		vkHDR.adaptation.targetExposure, vkHDR.adaptation.exposure, vkHDR.width, vkHDR.height,
		(int)r_hdrAutoExposureAsync.GetBool(), (int)vkHDR.skyPreserved, int( vkHDR.sceneLinear ) );
	common->Printf( "Vulkan HDR domain changes: %u (each resumes that domain's exposure and invalidates temporal history)\n",
		vkHDRDomainChanges );
	VK_HDRScene_PrintInfo();
}
// why the last scene pass gave up, for the one-time warning
static const char *vkPostFailReason = NULL;
static vkPostMotionViewState_t vkPostMotionHistory;
static idList<vkPostMotionEntityHistory_t> vkPostMotionEntityHistory;
static idList<vkPostMotionEntityHistory_t> vkPostMotionNextEntityHistory;

static vkPostMotionViewState_t vkTemporalMotionViewHistory;
static idList<vkPostMotionEntityHistory_t> vkTemporalMotionEntityHistory;
static idList<vkPostMotionEntityHistory_t> vkTemporalMotionNextEntityHistory;
static idImage *vkTemporalMotionImage = NULL;
static idRenderTexture *vkTemporalMotionTarget = NULL;
static unsigned int vkTemporalMotionGeneration = 0;
static unsigned long long vkTemporalMotionViewIdentity = 0;
static int vkTemporalMotionFrame = -1;
static int vkTemporalMotionEligible = 0;
static int vkTemporalMotionDrawn = 0;
static bool vkTemporalMotionComplete = false;
static unsigned long long vkTemporalMotionCompletedViews = 0;

/*
====================
Temporal motion history slots (VR-05)

Each VR eye reprojects from its own previous frame: the camera and
rigid-object timelines above belong to the active history slot
(viewDef_t::temporalHistorySlot), and the other slot's wait here, trading
places when a view of the other eye resolves. The vector image itself is
rebuilt for each resolve and needs no slot.
====================
*/
typedef struct vkTemporalMotionSlot_s {
	vkPostMotionViewState_t viewHistory;
	idList<vkPostMotionEntityHistory_t> entityHistory;
	unsigned int		generation;
	unsigned long long	viewIdentity;
	int					frame;
} vkTemporalMotionSlot_t;
static vkTemporalMotionSlot_t vkTemporalMotionStoredSlot;
static int vkTemporalMotionActiveSlot = 0;
static bool vkTemporalMotionStoredSlotInitialized = false;

static void VK_PostProcess_SelectTemporalMotionSlot( int slot ) {
	slot = slot == 1 ? 1 : 0;
	vkTemporalMotionSlot_t &other = vkTemporalMotionStoredSlot;
	if ( !vkTemporalMotionStoredSlotInitialized ) {
		memset( &other.viewHistory, 0, sizeof( other.viewHistory ) );
		other.generation = 0;
		other.viewIdentity = 0;
		other.frame = -1;
		vkTemporalMotionStoredSlotInitialized = true;
	}
	if ( slot == vkTemporalMotionActiveSlot ) {
		return;
	}
	const vkPostMotionViewState_t savedView = vkTemporalMotionViewHistory;
	vkTemporalMotionViewHistory = other.viewHistory;
	other.viewHistory = savedView;
	vkTemporalMotionEntityHistory.Swap( other.entityHistory );
	const unsigned int savedGeneration = vkTemporalMotionGeneration;
	vkTemporalMotionGeneration = other.generation;
	other.generation = savedGeneration;
	const unsigned long long savedIdentity = vkTemporalMotionViewIdentity;
	vkTemporalMotionViewIdentity = other.viewIdentity;
	other.viewIdentity = savedIdentity;
	const int savedFrame = vkTemporalMotionFrame;
	vkTemporalMotionFrame = other.frame;
	other.frame = savedFrame;
	vkTemporalMotionActiveSlot = slot;
}

void VK_PostProcess_PrintTemporalMotion( void ) {
	common->Printf( "Vulkan temporal motion: frame=%d generation=%u eligible=%d drawn=%d complete=%d completedViews=%llu\n",
		vkTemporalMotionFrame, vkTemporalMotionGeneration, vkTemporalMotionEligible,
		vkTemporalMotionDrawn, (int)vkTemporalMotionComplete, vkTemporalMotionCompletedViews );
}

typedef struct vkPostState_s {
	VkShaderModule	fullscreenVert;
	VkShaderModule	crtFrag;
	bool			modulesFailed;	// creation failed once; do not retry every frame
	idImage *		backBufferCopy;
} vkPostState_t;

static vkPostState_t vkPost;

// This is the completed pre-post scene, distinct from scratch reused by later
// effects. Reuse the linear output's existing capture; no extra per-frame copy.
static struct vkLinearCapture_t {
	idImage *image;
	unsigned int handle;
	unsigned int generation;
	int frame;
	int videoRestart;
	bool valid;
} vkLinearCapture;

void VK_PostProcess_ResetLinearCapture() {
	vkLinearCapture.valid = false;
}

// std140 layout of CRTBlock in post_crt.frag
typedef struct vkPostCRTBlock_s {
	float	texel[ 4 ];		// x: 1/width, y: 1/height, z: framebuffer height, w: timeSeconds
	float	crt[ 4 ];		// x: amount, y: scanline strength, z: mask strength, w: curvature
	float	chroma[ 4 ];	// x: chromatic aberration
} vkPostCRTBlock_t;

static VkShaderModule VK_Post_CreateModule( const unsigned char *code, unsigned int size,
		const char *name ) {
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

static bool VK_Post_EnsureModules( void ) {
	if ( vkPost.fullscreenVert != VK_NULL_HANDLE && vkPost.crtFrag != VK_NULL_HANDLE ) {
		return true;
	}
	if ( vkPost.modulesFailed || vkCtx.device == VK_NULL_HANDLE ) {
		return false;
	}
	if ( vkPost.fullscreenVert == VK_NULL_HANDLE ) {
		vkPost.fullscreenVert = VK_Post_CreateModule( vk_post_fullscreen_vert_spv,
				vk_post_fullscreen_vert_spv_size, "post full-screen vertex" );
	}
	if ( vkPost.crtFrag == VK_NULL_HANDLE ) {
		vkPost.crtFrag = VK_Post_CreateModule( vk_post_crt_frag_spv,
				vk_post_crt_frag_spv_size, "post CRT fragment" );
	}
	if ( vkPost.fullscreenVert == VK_NULL_HANDLE || vkPost.crtFrag == VK_NULL_HANDLE ) {
		vkPost.modulesFailed = true;
		return false;
	}
	return true;
}

static void VK_Post_DestroyModule( VkShaderModule &module ) {
	if ( module != VK_NULL_HANDLE && vkCtx.device != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, module, NULL );
	}
	module = VK_NULL_HANDLE;
}

void VK_SceneEffects_Shutdown( void );
void VK_DebugTools_Shutdown( void );

static motionBlurShutterState_t vkPostMotionShutter = { false, 0.0f };

static void VK_Post_ResetMotionBlurHistory( void ) {
	vkPostScene.motionHistoryValid = false;
	vkPostScene.motionVectorValid = false;
	vkPostMotionEntityHistory.Clear();
	vkPostMotionNextEntityHistory.Clear();
	MotionBlurCore_ResetShutter( vkPostMotionShutter );
}

static void VK_Post_MotionBlurRecordDisabled( const char *reason ) {
	motionBlurStats_t &stats = R_TemporalPresentation_MotionBlurStats();
	stats.disabledViews++;
	stats.lastDisabledReason = reason;
}

void VK_PostProcess_ResetTemporalMotion( void ) {
	vkTemporalMotionEntityHistory.Clear();
	vkTemporalMotionNextEntityHistory.Clear();
	vkTemporalMotionGeneration = 0;
	vkTemporalMotionViewIdentity = 0;
	vkTemporalMotionFrame = -1;
	vkTemporalMotionEligible = vkTemporalMotionDrawn = 0;
	vkTemporalMotionComplete = false;
}

/*
====================
VK_PostProcess_Shutdown

Called first thing in VK_GuiExecutor_Shutdown. The images belong to the
image manager, which frees them on its own; the render textures are ours.
====================
*/
void VK_PostProcess_Shutdown( void ) {
	memset( &vkLinearCapture, 0, sizeof( vkLinearCapture ) );
	VK_SceneEffects_Shutdown();
	VK_DebugTools_Shutdown();
	for ( int i = 0; i < VK_HDR_MAX_LEVELS; ++i ) {
		delete vkHDR.targets[ i ];
	}
	memset( &vkHDR, 0, sizeof( vkHDR ) );
	vkHDRMapName.Clear();
	vkPortalSkyFrame = -1;
	vkPortalSkyOwner = NULL;
	vkPortalSkyWorld = NULL;
	vkPortalSkyTarget = NULL;
	VK_Post_ResetHDRExposure();
	VK_Post_DestroyModule( vkPost.fullscreenVert );
	VK_Post_DestroyModule( vkPost.crtFrag );
	memset( &vkPost, 0, sizeof( vkPost ) );

	for ( int i = 0; i < VK_POST_SCENE_MODULE_COUNT; i++ ) {
		VK_Post_DestroyModule( vkPostScene.modules[ i ] );
	}
	for ( int level = 0; level < VK_POST_BLOOM_MAX_LEVELS; level++ ) {
		for ( int pingPong = 0; pingPong < 2; pingPong++ ) {
			delete vkPostScene.bloomTargets[ level ][ pingPong ];
		}
	}
	delete vkPostScene.motionVectorTarget;
	delete vkPostScene.ssaoFieldTarget;
	delete vkPostScene.ssaoHalfTarget;
	delete vkPostScene.earlyWorldTarget;
	delete vkTemporalMotionTarget;
	vkTemporalMotionTarget = NULL;
	vkTemporalMotionImage = NULL;
	// both eyes' timelines
	VK_PostProcess_ResetTemporalMotion();
	VK_PostProcess_SelectTemporalMotionSlot( vkTemporalMotionActiveSlot ^ 1 );
	VK_PostProcess_ResetTemporalMotion();
	VK_PostProcess_SelectTemporalMotionSlot( 0 );
	vkTemporalMotionCompletedViews = 0;
	memset( &vkPostScene, 0, sizeof( vkPostScene ) );
	vkPostScene.finalDepthFrame = -1;
	vkPostScene.ssaoWorldDepthFrame = -1;
	vkPostScene.celWorldDepthFrame = -1;
	vkPostScene.ssaoClassicDepthFrame = -1;
	vkPostScene.ssaoFieldFrame = -1;
	vkPostScene.earlyWorldFrame = -1;
	VK_Post_ResetMotionBlurHistory();
}

// Swapchain-sized RGBA8 copy the back-buffer passes sample. It is separate
// from _currentRender because the 3D views capture that at their own
// viewport size, and sharing it would resize it twice a frame.
static void VK_Post_BackBufferCopyImage( idImage *image ) {
	idImageOpts opts;
	opts.textureType = TT_2D;
	opts.format = FMT_RGBA8;
	opts.width = 32;
	opts.height = 32;
	opts.numLevels = 1;
	image->AllocImage( opts, TF_LINEAR, TR_CLAMP );
}

static idImage *VK_Post_BackBufferCopy( void ) {
	if ( vkPost.backBufferCopy == NULL && globalImages != NULL ) {
		vkPost.backBufferCopy = globalImages->ImageFromFunction( "_vkBackBufferCopy",
				VK_Post_BackBufferCopyImage );
	}
	return vkPost.backBufferCopy;
}

/*
====================
VK_Post_DrawFullscreen

Draws the covering triangle into the active target with every
depth/stencil test off. Image sets bind to sets 0..numSets-1 and the uniform
slice to set 6.
====================
*/
static bool VK_Post_DrawFullscreen( VkPipeline pipeline, const VkDescriptorSet *imageSets,
		int numSets, int uniformOffset ) {
	VkCommandBuffer cmd = VK_Exec_ActiveCmd();
	if ( cmd == VK_NULL_HANDLE || pipeline == VK_NULL_HANDLE || uniformOffset < 0
			|| !VK_Exec_MainRenderingScopeOpen() ) {
		return false;
	}
	for ( int i = 0; i < numSets; i++ ) {
		if ( imageSets[ i ] == VK_NULL_HANDLE ) {
			return false;
		}
	}
	const int width = VK_Exec_ActiveFramebufferWidth();
	const int height = VK_Exec_ActiveFramebufferHeight();
	VkViewport viewport;
	memset( &viewport, 0, sizeof( viewport ) );
	viewport.width = (float)width;
	viewport.height = (float)height;
	viewport.maxDepth = 1.0f;
	VkRect2D scissor;
	memset( &scissor, 0, sizeof( scissor ) );
	scissor.extent.width = (uint32_t)width;
	scissor.extent.height = (uint32_t)height;
	vkCmdSetViewport( cmd, 0, 1, &viewport );
	vkCmdSetScissor( cmd, 0, 1, &scissor );
	vkCmdSetDepthTestEnable( cmd, VK_FALSE );
	vkCmdSetDepthWriteEnable( cmd, VK_FALSE );
	vkCmdSetDepthCompareOp( cmd, VK_COMPARE_OP_ALWAYS );
	vkCmdSetCullMode( cmd, VK_CULL_MODE_NONE );
	vkCmdSetFrontFace( cmd, VK_Exec_CanonicalFrontFace() );
	vkCmdSetDepthBiasEnable( cmd, VK_FALSE );
	vkCmdSetStencilTestEnable( cmd, VK_FALSE );
	if ( vkCtx.depthBoundsSupported ) {
		vkCmdSetDepthBoundsTestEnable( cmd, VK_FALSE );
	}
	const VkPipelineLayout layout = VK_Exec_InteractionPipelineLayout();
	vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
	if ( numSets > 0 ) {
		vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
				0, (uint32_t)numSets, imageSets, 0, NULL );
	}
	const VkDescriptorSet uniformSet = VK_Exec_InteractionUniformSet();
	const uint32_t dynamicOffset = (uint32_t)uniformOffset;
	vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
			6, 1, &uniformSet, 1, &dynamicOffset );
	vkCmdDraw( cmd, 3, 1, 0, 0 );
	return true;
}

// Copies the finished swapchain image into the back-buffer copy and returns
// the copy's descriptor, ready to sample.
static VkDescriptorSet VK_Post_CaptureBackBuffer( void ) {
	idImage *copy = VK_Post_BackBufferCopy();
	if ( copy == NULL || !VK_Exec_SetRenderTarget( NULL ) ) {
		return VK_NULL_HANDLE;
	}
	const int width = VK_Exec_ActiveFramebufferWidth();
	const int height = VK_Exec_ActiveFramebufferHeight();
	if ( width <= 0 || height <= 0
			|| !VK_Exec_CopyRender( copy, 0, 0, width, height, 0, false ) ) {
		return VK_NULL_HANDLE;
	}
	VK_Exec_TransitionImageForSampling( copy );
	return VK_Exec_ImageDescriptor( copy->GetDeviceHandle(), true );
}

// RB_ApplyCRTToBackBuffer: same clamps and frame-based clock as OpenGL.
static float VK_Post_CRTAmount( void ) {
	return idMath::ClampFloat( 0.0f, 1.0f, r_crtAmount.GetFloat() );
}

static bool VK_Post_DrawCRT( void ) {
	const VkDescriptorSet sceneSet = VK_Post_CaptureBackBuffer();
	if ( sceneSet == VK_NULL_HANDLE ) {
		return false;
	}
	const int width = VK_Exec_ActiveFramebufferWidth();
	const int height = VK_Exec_ActiveFramebufferHeight();
	vkPostCRTBlock_t block;
	memset( &block, 0, sizeof( block ) );
	block.texel[ 0 ] = 1.0f / (float)width;
	block.texel[ 1 ] = 1.0f / (float)height;
	block.texel[ 2 ] = (float)height;
	block.texel[ 3 ] = (float)backEnd.frameCount * ( 1.0f / 60.0f );
	block.crt[ 0 ] = VK_Post_CRTAmount();
	block.crt[ 1 ] = idMath::ClampFloat( 0.0f, 1.0f, r_crtScanlineStrength.GetFloat() );
	block.crt[ 2 ] = idMath::ClampFloat( 0.0f, 1.0f, r_crtMaskStrength.GetFloat() );
	block.crt[ 3 ] = idMath::ClampFloat( 0.0f, 0.25f, r_crtCurvature.GetFloat() );
	block.chroma[ 0 ] = idMath::ClampFloat( 0.0f, 0.35f, r_crtChromatic.GetFloat() );
	const int uniformOffset = VK_Exec_InteractionUniformAlloc( &block, sizeof( block ) );
	const VkPipeline pipeline = VK_Exec_PostPipeline( VK_POST_CRT,
			vkPost.fullscreenVert, vkPost.crtFrag, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	return VK_Post_DrawFullscreen( pipeline, &sceneSet, 1, uniformOffset );
}

/*
====================
VK_PostProcess_ApplyBackBuffer

The Vulkan counterpart of RB_ApplyCRTToBackBuffer at the tail of
RB_SwapBuffers. RB_ApplyColorMappingsToBackBuffer's counterpart is the
executor's display mapping, which runs after this when the frame is presented
or read back.
====================
*/
void VK_PostProcess_ApplyBackBuffer( void ) {
	if ( !VK_GuiExecutor_FrameIsOpen() ) {
		return;
	}
	const bool crt = !r_skipPostProcess.GetBool() && r_crt.GetBool()
			&& VK_Post_CRTAmount() > 0.001f;
	if ( !crt || !VK_Post_EnsureModules() ) {
		return;
	}
	if ( !VK_Post_DrawCRT() ) {
		static bool crtWarned = false;
		if ( !crtWarned ) {
			common->Warning( "Vulkan: r_crt pass could not run this frame" );
			crtWarned = true;
		}
	}
}

/*
===============================================================================

	Scene passes

===============================================================================
*/

static VkShaderModule VK_Post_SceneModule( int which ) {
	if ( which < 0 || which >= VK_POST_SCENE_MODULE_COUNT ) {
		return VK_NULL_HANDLE;
	}
	if ( vkPostScene.modules[ which ] != VK_NULL_HANDLE ) {
		return vkPostScene.modules[ which ];
	}
	if ( vkPostScene.moduleFailed[ which ] || vkCtx.device == VK_NULL_HANDLE ) {
		return VK_NULL_HANDLE;
	}
	const vkPostModuleSource_t &source = vkPostSceneModuleSources[ which ];
	vkPostScene.modules[ which ] = VK_Post_CreateModule( source.code, source.size, source.name );
	if ( vkPostScene.modules[ which ] == VK_NULL_HANDLE ) {
		vkPostScene.moduleFailed[ which ] = true;
	}
	return vkPostScene.modules[ which ];
}

// RB_IsMainScenePostProcessView (draw_common.cpp)
static bool VK_Post_IsMainSceneView( const viewDef_t *viewDef ) {
	if ( viewDef == NULL || viewDef->viewEntitys == NULL ) {
		return false;
	}
	if ( ( viewDef->renderFlags & RF_PORTAL_SKY ) != 0 ) {
		return false;
	}
	if ( viewDef->isSubview || viewDef->superView != NULL
			|| viewDef->subviewSurface != NULL || viewDef->renderView.viewID < 0 ) {
		return false;
	}
	if ( viewDef->renderWorld != NULL && viewDef->renderWorld->mapName.Length() == 0 ) {
		return false;
	}
	return !viewDef->isXraySubview;
}

void VK_PostProcess_BeginView( const viewDef_t *viewDef ) {
	vkPortalSkyOwner = NULL;
	if ( viewDef == NULL ) { return; }
	if ( ( viewDef->renderFlags & RF_PORTAL_SKY ) != 0 ) {
		vkPortalSkyFrame = backEnd.frameCount;
		vkPortalSkyViewport = viewDef->viewport;
		vkPortalSkyWorld = viewDef->renderWorld;
		vkPortalSkyTarget = VK_Exec_ActiveRenderTexture();
		return;
	}
	if ( VK_Post_IsMainSceneView( viewDef ) ) {
		VK_PostProcess_ResetLinearCapture();
		if ( vkPortalSkyFrame == backEnd.frameCount
				&& vkPortalSkyWorld == viewDef->renderWorld
				&& vkPortalSkyViewport.Equals( viewDef->viewport )
				&& vkPortalSkyTarget == VK_Exec_ActiveRenderTexture() ) {
			vkPortalSkyOwner = viewDef;
		}
		// A backdrop belongs to the next matching main view, not a later root
		// or capture which happens to use the same dimensions and render world.
		vkPortalSkyFrame = -1;
		vkHDR.skyPreserved = false;
	}
}

static int VK_Post_ViewWidth( const viewDef_t *viewDef ) {
	return viewDef->viewport.x2 - viewDef->viewport.x1 + 1;
}

static int VK_Post_ViewHeight( const viewDef_t *viewDef ) {
	return viewDef->viewport.y2 - viewDef->viewport.y1 + 1;
}

static void VK_Post_SynchronizeHDRExposure( const viewDef_t *viewDef, bool sceneLinear = false ) {
	if ( !VK_Post_IsMainSceneView( viewDef ) || R_SubmittedTakingScreenshot() || viewDef->temporalCaptureFrame ) {
		return;
	}
	const bool requested = !r_skipPostProcess.GetBool() && r_hdrToneMap.GetBool() && r_hdrAutoExposure.GetBool();
	const idRenderTexture *target = VK_Exec_ActiveRenderTexture();
	const idImage *scene = target != NULL && target->GetNumColorImages() > 0 ? target->GetColorImage( 0 ) : NULL;
	const bool sceneFloat = scene != NULL && scene->GetOpts().format == FMT_RGBA16F;
	const int sceneSamples = scene != NULL ? scene->GetOpts().numMSAASamples : 0;
	const int width = VK_Post_ViewWidth( viewDef );
	const int height = VK_Post_ViewHeight( viewDef );
	const char *mapName = viewDef->renderWorld != NULL ? viewDef->renderWorld->mapName.c_str() : "";
	const bool cameraCut = vkHDR.cameraValid && ( viewDef->floatTime < vkHDR.viewTime
		|| viewDef->floatTime - vkHDR.viewTime > 1.0f
		|| ( viewDef->renderView.vieworg - vkHDR.viewOrigin ).LengthSqr() > 256.0f * 256.0f
		|| viewDef->renderView.viewaxis[ 0 ] * vkHDR.viewForward < 0.70710678f
		|| idMath::Fabs( viewDef->renderView.fov_x - vkHDR.fovX ) > 1.0f
		|| idMath::Fabs( viewDef->renderView.fov_y - vkHDR.fovY ) > 1.0f );
	const bool contextChanged = requested != vkHDR.enabled || width != vkHDR.width || height != vkHDR.height
			|| sceneFloat != vkHDR.sceneFloat || sceneSamples != vkHDR.sceneSamples
			|| vkHDR.renderWorld != viewDef->renderWorld || vkHDRMapName != mapName
			|| vkHDR.videoRestartCount != tr.GetVideoRestartCount() || cameraCut;
	if ( contextChanged ) {
		vkHDRDomainExposureValid[ 0 ] = vkHDRDomainExposureValid[ 1 ] = false;
		VK_Post_ResetHDRExposure();
	} else if ( sceneLinear != vkHDR.sceneLinear && vkHDR.cameraValid ) {
		// Same view, other tone curve. Samples in flight were metered in the old
		// domain (the new generation drops them), and temporal history would
		// blend the two curves.
		const int from = vkHDR.sceneLinear ? 1 : 0;
		const int to = sceneLinear ? 1 : 0;
		vkHDRDomainExposure[ from ] = vkHDR.adaptation;
		vkHDRDomainExposureValid[ from ] = vkHDR.adaptation.initialized;
		VK_Post_ResetHDRExposure();
		if ( vkHDRDomainExposureValid[ to ] ) {
			vkHDR.adaptation = vkHDRDomainExposure[ to ];
		}
		++vkHDRDomainChanges;
		R_TemporalPresentation_InvalidateHistory( sceneLinear ? "Vulkan scene entered linear HDR"
			: "Vulkan scene left linear HDR" );
	}
	vkHDR.enabled = requested;
	vkHDR.sceneFloat = sceneFloat;
	vkHDR.sceneLinear = sceneLinear;
	vkHDR.sceneSamples = sceneSamples;
	vkHDR.width = width;
	vkHDR.height = height;
	vkHDR.renderWorld = viewDef->renderWorld;
	vkHDRMapName = mapName;
	vkHDR.videoRestartCount = tr.GetVideoRestartCount();
	vkHDR.viewOrigin = viewDef->renderView.vieworg;
	vkHDR.viewForward = viewDef->renderView.viewaxis[ 0 ];
	vkHDR.fovX = viewDef->renderView.fov_x;
	vkHDR.fovY = viewDef->renderView.fov_y;
	vkHDR.viewTime = viewDef->floatTime;
	vkHDR.cameraValid = true;
}

// RB_SSAORequestedForCurrentView
static bool VK_Post_SSAORequested( const viewDef_t *viewDef ) {
	return !r_skipPostProcess.GetBool() && r_ssao.GetBool()
		&& VK_Post_IsMainSceneView( viewDef )
		&& r_ssaoRadius.GetFloat() > 0.0f && r_ssaoIntensity.GetFloat() > 0.0f;
}

// RB_PostProcessBloomRequested
static bool VK_Post_BloomRequested( void ) {
	return r_bloom.GetBool() && r_bloomIntensity.GetFloat() > 0.0001f;
}

// the gate at the top of RB_STD_Bloom: the composite also carries the tone map
static bool VK_Post_BloomPassRequested( const viewDef_t *viewDef ) {
	return !r_skipPostProcess.GetBool() && VK_Post_IsMainSceneView( viewDef )
		&& ( VK_Post_BloomRequested() || r_hdrToneMap.GetBool()
			|| idMath::ClampInt( 0, 2, r_hdrDebugView.GetInteger() ) > 0 );
}

// RB_CelWorldOutlineRequestedForCurrentView
static bool VK_Post_CelInkRequested( const viewDef_t *viewDef ) {
	return !r_skipPostProcess.GetBool() && R_CelWorldOutlineEnabled()
		&& VK_Post_IsMainSceneView( viewDef );
}

// RB_MaterialIsSkyForSSAODepth
static bool VK_Post_MaterialIsSky( const idMaterial *material ) {
	if ( material == NULL ) {
		return false;
	}
	if ( material->IsPortalSky() || material->GetSort() == SS_PORTAL_SKY ) {
		return true;
	}
	const texgen_t texgen = material->Texgen();
	return texgen == TG_SKYBOX_CUBE || texgen == TG_WOBBLESKY_CUBE;
}

// RB_SSAOWorldDepthSurfFilter
static bool VK_Post_SSAOWorldDepthSurf( const drawSurf_t *surf ) {
	if ( surf == NULL || surf->space == NULL || surf->geo == NULL || surf->material == NULL ) {
		return false;
	}
	if ( ( surf->dsFlags & DSF_BSE_EFFECT ) != 0 ) {
		return false;
	}
	if ( surf->space->weaponDepthHack || surf->space->modelDepthHack != 0.0f ) {
		return false;
	}
	const idMaterial *material = surf->material;
	if ( !material->IsDrawn() || material->Coverage() == MC_TRANSLUCENT ) {
		return false;
	}
	if ( material->GetSort() >= SS_POST_PROCESS || material->GetSort() == SS_SUBVIEW ) {
		return false;
	}
	if ( material->HasGui() || material->SuppressInSubview() || VK_Post_MaterialIsSky( material ) ) {
		return false;
	}
	const idRenderEntityLocal *entityDef = surf->space->entityDef;
	if ( entityDef == NULL ) {
		return true;
	}
	const renderEntity_t &renderEntity = entityDef->parms;
	return renderEntity.remoteRenderView == NULL
		&& renderEntity.allowSurfaceInViewID == 0
		&& renderEntity.weaponDepthHackInViewID == 0
		&& renderEntity.modelDepthHack == 0.0f;
}

// RB_CelWorldDepthSurfFilter
static bool VK_Post_CelWorldDepthSurf( const drawSurf_t *surf ) {
	if ( surf == NULL || surf->space == NULL || surf->geo == NULL || surf->material == NULL ) {
		return false;
	}
	if ( ( surf->dsFlags & DSF_BSE_EFFECT ) != 0 || !R_CelSurfaceIsWorld( surf ) ) {
		return false;
	}
	const idMaterial *material = surf->material;
	if ( !material->IsDrawn() || material->Coverage() == MC_TRANSLUCENT ) {
		return false;
	}
	if ( material->GetSort() >= SS_POST_PROCESS || material->GetSort() == SS_SUBVIEW ) {
		return false;
	}
	return !material->HasGui() && !material->SuppressInSubview()
		&& !VK_Post_MaterialIsSky( material );
}

static void VK_Post_FloatColorImage( idImage *image ) {
	idImageOpts opts;
	opts.textureType = TT_2D;
	opts.format = FMT_RGBA16F;
	opts.width = 32;
	opts.height = 32;
	opts.numLevels = 1;
	image->AllocImage( opts, TF_LINEAR, TR_CLAMP );
}

static void VK_Post_DepthImage( idImage *image ) {
	idImageOpts opts;
	opts.textureType = TT_2D;
	opts.format = FMT_DEPTH;
	opts.width = 32;
	opts.height = 32;
	opts.numLevels = 1;
	image->AllocImage( opts, TF_NEAREST, TR_CLAMP );
}

static idImage *VK_Post_EnsureImage( idImage *&image, const char *name, void ( *generator )( idImage *image ) ) {
	if ( image == NULL && globalImages != NULL ) {
		image = globalImages->ImageFromFunction( name, generator );
	}
	return image;
}

// Copies the view rectangle of the active target into the scene copy.
static idImage *VK_Post_CaptureScene( const viewDef_t *viewDef ) {
	idImage *image = VK_Post_EnsureImage( vkPostScene.sceneCopy, "_vkPostScene", VK_Post_FloatColorImage );
	if ( image == NULL || !VK_Exec_CopyRender( image, viewDef->viewport.x1, viewDef->viewport.y1,
			VK_Post_ViewWidth( viewDef ), VK_Post_ViewHeight( viewDef ), 0, false ) ) {
		return NULL;
	}
	return image;
}

static idImage *VK_Post_CaptureLinearScene( const viewDef_t *viewDef ) {
	idImage *image = VK_Post_EnsureImage( vkLinearCapture.image, "_vkLinearScene", VK_Post_FloatColorImage );
	if ( image == NULL || !VK_Exec_CopyRender( image, viewDef->viewport.x1, viewDef->viewport.y1,
			VK_Post_ViewWidth( viewDef ), VK_Post_ViewHeight( viewDef ), 0, false ) ) {
		return NULL;
	}
	return image;
}

bool VK_PostProcess_LinearScreenshot( const char *fileName ) {
	R_RenderThread_Sync( "readback" );
	idStr path( fileName );
	if ( path.Icmpn( "screenshots/", 12 ) != 0 || path.Find( ".." ) >= 0
			|| path.Find( ':' ) >= 0 || path.Find( '\\' ) >= 0 || !path.CheckExtension( ".pfm" ) ) {
		common->Printf( "screenshot linear: use screenshots/<name>.pfm\n" );
		return false;
	}
	vkImageEntry_t *entry = vkLinearCapture.image != NULL
		? VK_Image_GetEntry( vkLinearCapture.image->GetDeviceHandle() ) : NULL;
	if ( !vkLinearCapture.valid || !VK_HDRScene_Requested()
			|| vkLinearCapture.frame != backEnd.frameCount
			|| vkLinearCapture.videoRestart != tr.GetVideoRestartCount()
			|| entry == NULL || entry->generation != vkLinearCapture.generation
			|| vkLinearCapture.image->GetDeviceHandle() != vkLinearCapture.handle
			|| entry->format != VK_FORMAT_R16G16B16A16_SFLOAT || entry->materialSampleFlipY
			|| entry->width < 1 || entry->height < 1 || entry->width > 8192 || entry->height > 8192 ) {
		common->Printf( "screenshot linear: no completed modern HDR scene\n" );
		return false;
	}
	const int width = entry->width, height = entry->height;
	idList<float> rgba;
	if ( !VK_Exec_ReadLinearScreenshot( vkLinearCapture.image, rgba )
			|| rgba.Num() != width * height * 4 ) {
		common->Printf( "screenshot linear: HDR readback failed\n" );
		return false;
	}
	const int values = width * height * 3;
	idTempArray<float> rgb( values );
	for ( int pixel = 0; pixel < width * height; ++pixel ) {
		for ( int channel = 0; channel < 3; ++channel ) {
			rgb[pixel * 3 + channel] = LittleFloat( rgba[pixel * 4 + channel] );
		}
	}
	// CopyRender's bottom-up RGB is PFM's row order. Preserve overbright values
	// without exposure, tone mapping, output transfer, alpha or an extra flip.
	idFile *file = fileSystem->OpenFileWrite( path.c_str() );
	if ( file == NULL ) { return false; }
	const idStr header = va( "PF\n%d %d\n-1.0\n", width, height );
	const bool headerWritten = file->Write( header.c_str(), header.Length() ) == header.Length();
	const int written = headerWritten ? file->Write( rgb.Ptr(), values * sizeof( float ) ) : 0;
	fileSystem->CloseFile( file );
	if ( !headerWritten || written != values * sizeof( float ) ) { return false; }
	common->Printf( "Wrote %s (linear HDR scene, %dx%d RGB float)\n", path.c_str(), width, height );
	return true;
}

static bool VK_Post_CaptureDepth( idImage *image, const viewDef_t *viewDef ) {
	return image != NULL && VK_Exec_CopyRender( image, viewDef->viewport.x1, viewDef->viewport.y1,
			VK_Post_ViewWidth( viewDef ), VK_Post_ViewHeight( viewDef ), 0, true );
}

// The finished depth of this view, captured once per VK_PostProcess_DrawSceneEffects call.
static idImage *VK_Post_FinalDepth( const viewDef_t *viewDef ) {
	idImage *image = VK_Post_EnsureImage( vkPostScene.finalDepth, "_vkPostFinalDepth", VK_Post_DepthImage );
	if ( image == NULL ) {
		return NULL;
	}
	if ( vkPostScene.finalDepthFrame == backEnd.frameCount
			&& vkPostScene.finalDepthView == vkPostScene.viewSerial ) {
		return image;
	}
	if ( !VK_Post_CaptureDepth( image, viewDef ) ) {
		return NULL;
	}
	vkPostScene.finalDepthFrame = backEnd.frameCount;
	vkPostScene.finalDepthView = vkPostScene.viewSerial;
	return image;
}

static VkDescriptorSet VK_Post_Descriptor( idImage *image ) {
	return image != NULL ? VK_Exec_ImageDescriptor( image->GetDeviceHandle(), true ) : VK_NULL_HANDLE;
}

static void VK_Post_ResetDrawState( VkCommandBuffer cmd ) {
	vkCmdSetDepthTestEnable( cmd, VK_FALSE );
	vkCmdSetDepthWriteEnable( cmd, VK_FALSE );
	vkCmdSetDepthCompareOp( cmd, VK_COMPARE_OP_ALWAYS );
	vkCmdSetCullMode( cmd, VK_CULL_MODE_NONE );
	vkCmdSetFrontFace( cmd, VK_Exec_CanonicalFrontFace() );
	vkCmdSetDepthBiasEnable( cmd, VK_FALSE );
	vkCmdSetStencilTestEnable( cmd, VK_FALSE );
	if ( vkCtx.depthBoundsSupported ) {
		vkCmdSetDepthBoundsTestEnable( cmd, VK_FALSE );
	}
}

static void VK_Post_BindAndDraw( VkCommandBuffer cmd, VkPipeline pipeline,
		const VkDescriptorSet *imageSets, int numSets, int uniformOffset ) {
	const VkPipelineLayout layout = VK_Exec_InteractionPipelineLayout();
	vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
	if ( numSets > 0 ) {
		vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
				0, (uint32_t)numSets, imageSets, 0, NULL );
	}
	const VkDescriptorSet uniformSet = VK_Exec_InteractionUniformSet();
	const uint32_t dynamicOffset = (uint32_t)uniformOffset;
	vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
			6, 1, &uniformSet, 1, &dynamicOffset );
	vkCmdDraw( cmd, 3, 1, 0, 0 );
}

static bool VK_Post_SetsReady( VkPipeline pipeline, const VkDescriptorSet *imageSets,
		int numSets, int uniformOffset ) {
	if ( pipeline == VK_NULL_HANDLE || uniformOffset < 0 || !VK_Exec_MainRenderingScopeOpen() ) {
		return false;
	}
	for ( int i = 0; i < numSets; i++ ) {
		if ( imageSets[ i ] == VK_NULL_HANDLE ) {
			return false;
		}
	}
	return true;
}

/*
====================
VK_Post_DrawSceneRect

Draws the covering triangle over the view rectangle of the scene target,
scissored to the view scissor (RB_BeginFullscreenPostProcessPass). The
canonical viewport puts fragUV (0,0) at the bottom-left of the visible
scene, regardless of the attachment's stored row order.
====================
*/
static bool VK_Post_DrawSceneRect( const viewDef_t *viewDef, VkPipeline pipeline,
		const VkDescriptorSet *imageSets, int numSets, int uniformOffset, bool preserveFarDepth = false ) {
	VkCommandBuffer cmd = VK_Exec_ActiveCmd();
	if ( cmd == VK_NULL_HANDLE || !VK_Post_SetsReady( pipeline, imageSets, numSets, uniformOffset ) ) {
		return false;
	}
	const int fbHeight = VK_Exec_ActiveFramebufferHeight();
	VkViewport viewport;
	memset( &viewport, 0, sizeof( viewport ) );
	viewport.x = (float)viewDef->viewport.x1;
	viewport.y = (float)( VK_Exec_ActiveLowerOrigin() ? viewDef->viewport.y1 : fbHeight - viewDef->viewport.y1 );
	viewport.width = (float)VK_Post_ViewWidth( viewDef );
	viewport.height = ( VK_Exec_ActiveLowerOrigin() ? 1.0f : -1.0f ) * VK_Post_ViewHeight( viewDef );
	viewport.maxDepth = 1.0f;
	if ( preserveFarDepth ) {
		viewport.minDepth = viewport.maxDepth = 0.99999f;
	}
	vkCmdSetViewport( cmd, 0, 1, &viewport );
	VK_Exec_SetViewScissor( cmd, viewDef, fbHeight );
	VK_Post_ResetDrawState( cmd );
	if ( preserveFarDepth ) {
		vkCmdSetDepthTestEnable( cmd, VK_TRUE );
		vkCmdSetDepthCompareOp( cmd, VK_COMPARE_OP_GREATER );
	}
	VK_Post_BindAndDraw( cmd, pipeline, imageSets, numSets, uniformOffset );
	VK_Exec_MarkCanonicalWrites();
	if ( preserveFarDepth ) {
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport( cmd, 0, 1, &viewport );
		VK_Post_ResetDrawState( cmd );
	}
	return true;
}

// Fills the whole active intermediate target; positive viewport, so the rows
// come out bottom-up like the captures.
static bool VK_Post_DrawTarget( VkPipeline pipeline, const VkDescriptorSet *imageSets,
		int numSets, int uniformOffset ) {
	const bool drawn = VK_Post_DrawFullscreen( pipeline, imageSets, numSets, uniformOffset );
	if ( drawn ) { VK_Exec_MarkColorOrigin( false ); }
	return drawn;
}

static bool VK_Post_ProjectionUsable( const viewDef_t *viewDef ) {
	return idMath::Fabs( viewDef->projectionMatrix[ 0 ] ) > 0.00001f
		&& idMath::Fabs( viewDef->projectionMatrix[ 5 ] ) > 0.00001f;
}

static void VK_Post_ProjectionInfo( const viewDef_t *viewDef, float out[ 4 ] ) {
	out[ 0 ] = 1.0f / viewDef->projectionMatrix[ 0 ];
	out[ 1 ] = 1.0f / viewDef->projectionMatrix[ 5 ];
	out[ 2 ] = viewDef->projectionMatrix[ 8 ];
	out[ 3 ] = viewDef->projectionMatrix[ 9 ];
}

/*
====================
World-depth snapshots

OpenGL's RB_CaptureSSAOWorldDepthImage and RB_CaptureCelWorldDepthImage each
draw a filtered depth-only fill, copy depth and clear again before the real
prepass. Depth writes are order independent, so the Vulkan prepass orders its
surfaces instead: the ones the cel filter keeps, a copy, the rest of the SSAO
filter's, a copy, then everything else. The cel filter keeps world surfaces
only, which the SSAO filter also keeps. A view whose native PBR owns world
surfaces splits the SSAO filter's surfaces: classic ones, a copy, native PBR
ones, the world copy (draw_common.cpp RB_CaptureSSAOWorldDepthImage).
====================
*/
int VK_PostProcess_WorldDepthCaptures( const viewDef_t *viewDef ) {
	// The world depth this view's portal-sky mask already drew and captured
	// into ssaoWorldDepth (VK_PostProcess_PortalSkyMask) stands in for the
	// SSAO snapshot. Only main views capture into that image, so the portal
	// view's own prepass, which resets the frame stamp below, leaves it intact.
	const bool earlyWorld = vkPostScene.earlyWorldView == viewDef
		&& vkPostScene.earlyWorldFrame == backEnd.frameCount;
	vkPostScene.ssaoWorldDepthFrame = -1;
	vkPostScene.celWorldDepthFrame = -1;
	vkPostScene.ssaoClassicDepthFrame = -1;
	vkPostScene.ssaoFieldFrame = -1;
	vkPostScene.ssaoFieldView = NULL;
	int captures = 0;
	if ( VK_Post_CelInkRequested( viewDef ) ) {
		captures |= VK_POST_CAPTURE_CEL_WORLD;
	}
	if ( VK_Post_SSAORequested( viewDef ) ) {
		captures |= VK_POST_CAPTURE_SSAO_WORLD;
		// The cel world ink keeps the classic whole-frame occlusion.
		if ( ( captures & VK_POST_CAPTURE_CEL_WORLD ) == 0 ) {
			for ( int i = 0; i < viewDef->numDrawSurfs; i++ ) {
				const drawSurf_t *surf = viewDef->drawSurfs[ i ];
				if ( VK_Post_SSAOWorldDepthSurf( surf ) && VK_PBR_NativeOpaqueSurface( surf ) ) {
					captures |= VK_POST_CAPTURE_SSAO_SPLIT;
					break;
				}
			}
		}
	}
	// Only the plain SSAO snapshot has an early equivalent; cel ink and the
	// native PBR split keep their in-prepass captures.
	if ( captures == VK_POST_CAPTURE_SSAO_WORLD && earlyWorld ) {
		static bool logged = false;
		if ( !logged ) {
			logged = true;
			common->Printf( "Vulkan: SSAO uses the portal-sky mask's world depth (no prepass snapshot)\n" );
		}
		vkPostScene.ssaoWorldDepthFrame = backEnd.frameCount;
		return 0;
	}
	return captures;
}

int VK_PostProcess_DepthFillPhase( const drawSurf_t *surf, int captures ) {
	if ( ( captures & VK_POST_CAPTURE_CEL_WORLD ) != 0 && VK_Post_CelWorldDepthSurf( surf ) ) {
		return 0;
	}
	if ( ( captures & VK_POST_CAPTURE_SSAO_WORLD ) != 0 && VK_Post_SSAOWorldDepthSurf( surf ) ) {
		return ( captures & VK_POST_CAPTURE_SSAO_SPLIT ) != 0 && VK_PBR_NativeOpaqueSurface( surf ) ? 2 : 1;
	}
	return 3;
}

// Runs after prepass phases 0-2. phaseDrawn counts the surfaces each phase
// drew; the copy restarts rendering, so the caller re-establishes its state.
void VK_PostProcess_CaptureWorldDepth( const viewDef_t *viewDef, int fillPhase, int captures,
		const int phaseDrawn[ 4 ] ) {
	const int width = VK_Post_ViewWidth( viewDef );
	const int height = VK_Post_ViewHeight( viewDef );
	if ( fillPhase == 0 && ( captures & VK_POST_CAPTURE_CEL_WORLD ) != 0 && phaseDrawn[ 0 ] > 0 ) {
		idImage *image = VK_Post_EnsureImage( vkPostScene.celWorldDepth, "_vkCelWorldDepth", VK_Post_DepthImage );
		if ( VK_Post_CaptureDepth( image, viewDef ) ) {
			vkPostScene.celWorldDepthFrame = backEnd.frameCount;
			vkPostScene.celWorldDepthWidth = width;
			vkPostScene.celWorldDepthHeight = height;
		}
	} else if ( fillPhase == 0 && ( captures & VK_POST_CAPTURE_CEL_WORLD ) != 0
			&& r_celShadingWorldDebug.GetBool() ) {
		common->Printf( "cel world outline skipped: no world surfaces passed the depth snapshot filter\n" );
	}
	const bool split = ( captures & VK_POST_CAPTURE_SSAO_SPLIT ) != 0;
	if ( fillPhase == 1 && split && ( captures & VK_POST_CAPTURE_SSAO_WORLD ) != 0 ) {
		// The classic world alone; empty where native PBR is the only world.
		idImage *image = VK_Post_EnsureImage( vkPostScene.ssaoClassicDepth, "_vkSSAOClassicDepth", VK_Post_DepthImage );
		if ( VK_Post_CaptureDepth( image, viewDef ) ) {
			vkPostScene.ssaoClassicDepthFrame = backEnd.frameCount;
		}
	}
	if ( fillPhase == ( split ? 2 : 1 ) && ( captures & VK_POST_CAPTURE_SSAO_WORLD ) != 0
			&& phaseDrawn[ 0 ] + phaseDrawn[ 1 ] + ( split ? phaseDrawn[ 2 ] : 0 ) > 0 ) {
		idImage *image = VK_Post_EnsureImage( vkPostScene.ssaoWorldDepth, "_vkSSAOWorldDepth", VK_Post_DepthImage );
		vkGpuPhaseScope timing( VK_Exec_ActiveCmd(), VK_GPU_PHASE_SSAO_SNAPSHOT );
		if ( VK_Post_CaptureDepth( image, viewDef ) ) {
			vkPostScene.ssaoWorldDepthFrame = backEnd.frameCount;
			vkPostScene.ssaoWorldDepthWidth = width;
			vkPostScene.ssaoWorldDepthHeight = height;
		}
	}
}

/*
====================
Portal-sky mask

A portal-sky backdrop is a whole second view drawn into the scene target
before its main view, and it shows only where the main view draws nothing:
the compiled map has no sky faces, and the main prepass writes black over
every opaque or perforated pixel it covers. Before a command list runs, each
backdrop view is paired with the next main view of the same world, viewport
and target. At the backdrop's depth clear, the main view's world surfaces (the
SSAO world filter) are drawn depth-only into a single-sample target and
captured. A full-screen pass then sets the backdrop's depth to 1 wherever that
world depth, or any of its eight neighbours, is still far, and to 0 elsewhere.
Every depth-tested pass of the backdrop rejects the 0 pixels before shading.
The neighbourhood covers multisampled silhouette pixels whose centre the world
covers but some samples do not. Translucent surfaces, alpha-tested holes and
models leave far depth, so the backdrop still draws behind them. The capture
is the SSAO world depth for the main view, which then takes no snapshot in its
prepass. r_vkPortalSkyMask 0 restores the full backdrop and the snapshot.
====================
*/
static idCVar r_vkPortalSkyMask( "r_vkPortalSkyMask", "1", CVAR_RENDERER | CVAR_BOOL,
	"draw the portal-sky backdrop only where its main view's world leaves far depth" );

static const int VK_PORTAL_SKY_MASK_MAX = 8;
typedef struct vkPortalSkyPair_s {
	const viewDef_t *	portal;
	const viewDef_t *	mainView;
} vkPortalSkyPair_t;
static vkPortalSkyPair_t vkPortalSkyPairs[ VK_PORTAL_SKY_MASK_MAX ];
static int vkPortalSkyPairCount = 0;
static bool vkPortalSkyMaskPlanned = false;

// The pairs also name each backdrop's main view for the temporal scene owner
// (VK_PostProcess_PortalSkyMainView): a VR frame has a main view per eye, so
// the frame's one primary view cannot stand for both, and they are planned
// with the mask switched off too.
void VK_PostProcess_PlanPortalSkyMask( const emptyCommand_t *cmds ) {
	vkPortalSkyPairCount = 0;
	vkPortalSkyMaskPlanned = r_vkPortalSkyMask.GetBool() && !r_skipPostProcess.GetBool();
	const idRenderTexture *target = NULL;
	for ( const emptyCommand_t *cmd = cmds; cmd != NULL; cmd = (const emptyCommand_t *)cmd->next ) {
		if ( cmd->commandId == RC_SET_RENDERTEXTURE ) {
			target = ( (const setRenderTargetCommand_t *)cmd )->renderTexture;
			continue;
		}
		if ( cmd->commandId != RC_DRAW_VIEW ) {
			continue;
		}
		const viewDef_t *portal = ( (const drawSurfsCommand_t *)cmd )->viewDef;
		if ( portal == NULL || ( portal->renderFlags & RF_PORTAL_SKY ) == 0
				|| vkPortalSkyPairCount >= VK_PORTAL_SKY_MASK_MAX ) {
			continue;
		}
		// The owner is the next main view; another target in between, or a
		// main view that does not match, leaves this backdrop whole.
		const idRenderTexture *laterTarget = target;
		for ( const emptyCommand_t *next = (const emptyCommand_t *)cmd->next; next != NULL;
				next = (const emptyCommand_t *)next->next ) {
			if ( next->commandId == RC_SET_RENDERTEXTURE ) {
				laterTarget = ( (const setRenderTargetCommand_t *)next )->renderTexture;
				continue;
			}
			if ( next->commandId != RC_DRAW_VIEW ) {
				continue;
			}
			const viewDef_t *mainView = ( (const drawSurfsCommand_t *)next )->viewDef;
			if ( mainView == NULL || !VK_Post_IsMainSceneView( mainView ) ) {
				continue;
			}
			if ( laterTarget == target && mainView->renderWorld == portal->renderWorld
					&& mainView->viewport.Equals( portal->viewport ) ) {
				vkPortalSkyPairs[ vkPortalSkyPairCount ].portal = portal;
				vkPortalSkyPairs[ vkPortalSkyPairCount ].mainView = mainView;
				vkPortalSkyPairCount++;
			}
			break;
		}
	}
}

// Single-sample colour + depth target the size of the active framebuffer;
// the colour is only the prepass's black, which nothing reads.
static bool VK_Post_EnsureEarlyWorldTarget( int width, int height ) {
	idRenderTexture *&target = vkPostScene.earlyWorldTarget;
	if ( target != NULL ) {
		int currentWidth = 0, currentHeight = 0;
		tr.GetRenderTextureSize( target, currentWidth, currentHeight );
		if ( currentWidth != width || currentHeight != height ) {
			tr.ResizeRenderTexture( target, width, height );
		}
		return target != NULL && target->EnsureDeviceHandle();
	}
	idImageOpts opts;
	opts.format = FMT_RGBA8;
	opts.colorFormat = CFM_DEFAULT;
	opts.numLevels = 1;
	opts.textureType = TT_2D;
	opts.isPersistant = true;
	opts.width = width;
	opts.height = height;
	opts.numMSAASamples = 0;
	if ( vkPostScene.earlyWorldColor == NULL ) {
		vkPostScene.earlyWorldColor = globalImages->ScratchImage( "_vkEarlyWorldColor", &opts, TF_NEAREST, TR_CLAMP, TD_DEFAULT );
	}
	opts.format = FMT_DEPTH_STENCIL;
	if ( vkPostScene.earlyWorldDepth == NULL ) {
		vkPostScene.earlyWorldDepth = globalImages->ScratchImage( "_vkEarlyWorldDepth", &opts, TF_NEAREST, TR_CLAMP, TD_DEPTH );
	}
	if ( vkPostScene.earlyWorldColor == NULL || vkPostScene.earlyWorldDepth == NULL ) {
		return false;
	}
	target = tr.CreateRenderTexture( vkPostScene.earlyWorldColor, vkPostScene.earlyWorldDepth );
	if ( target != NULL ) {
		target->SetDebugLabel( "Vulkan early world depth" );
	}
	return target != NULL && target->EnsureDeviceHandle();
}

// Depth 1 where the captured world depth or a neighbour is far, else 0;
// colour is untouched (blend ZERO, ONE).
static bool VK_Post_DrawSkyMask( const viewDef_t *portal, idImage *worldDepth ) {
	const VkShaderModule fragModule = VK_Post_SceneModule( VK_POST_MODULE_SKY_MASK );
	VkCommandBuffer cmd = VK_Exec_ActiveCmd();
	if ( fragModule == VK_NULL_HANDLE || cmd == VK_NULL_HANDLE ) {
		return false;
	}
	const float block[ 4 ] = { 1.0f / (float)VK_Post_ViewWidth( portal ), 1.0f / (float)VK_Post_ViewHeight( portal ), 0.0f, 0.0f };
	const VkDescriptorSet sets[ 1 ] = { VK_Post_Descriptor( worldDepth ) };
	const int uniformOffset = VK_Exec_InteractionUniformAlloc( block, sizeof( block ) );
	const VkPipeline pipeline = VK_Exec_PostPipeline( VK_POST_SKY_MASK, vkPost.fullscreenVert, fragModule,
			GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE );
	if ( !VK_Post_SetsReady( pipeline, sets, 1, uniformOffset ) ) {
		return false;
	}
	const int fbHeight = VK_Exec_ActiveFramebufferHeight();
	VkViewport viewport;
	memset( &viewport, 0, sizeof( viewport ) );
	viewport.x = (float)portal->viewport.x1;
	viewport.y = (float)( VK_Exec_ActiveLowerOrigin() ? portal->viewport.y1 : fbHeight - portal->viewport.y1 );
	viewport.width = (float)VK_Post_ViewWidth( portal );
	viewport.height = ( VK_Exec_ActiveLowerOrigin() ? 1.0f : -1.0f ) * VK_Post_ViewHeight( portal );
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport( cmd, 0, 1, &viewport );
	VK_Exec_SetViewScissor( cmd, portal, fbHeight );
	VK_Post_ResetDrawState( cmd );
	vkCmdSetDepthTestEnable( cmd, VK_TRUE );
	vkCmdSetDepthCompareOp( cmd, VK_COMPARE_OP_ALWAYS );
	vkCmdSetDepthWriteEnable( cmd, VK_TRUE );
	VK_Post_BindAndDraw( cmd, pipeline, sets, 1, uniformOffset );
	VK_Post_ResetDrawState( cmd );
	return true;
}

const viewDef_t *VK_PostProcess_PortalSkyMainView( const viewDef_t *portal ) {
	for ( int i = 0; i < vkPortalSkyPairCount; i++ ) {
		if ( vkPortalSkyPairs[ i ].portal == portal ) {
			return vkPortalSkyPairs[ i ].mainView;
		}
	}
	return NULL;
}

bool VK_PostProcess_PortalSkyMask( const viewDef_t *portal ) {
	const viewDef_t *mainView = vkPortalSkyMaskPlanned ? VK_PostProcess_PortalSkyMainView( portal ) : NULL;
	if ( mainView == NULL || !mainView->viewport.Equals( portal->viewport ) || !VK_Post_EnsureModules() ) {
		return false;
	}
	idRenderTexture *sceneTarget = VK_Exec_ActiveRenderTexture();
	const int sceneFace = VK_Exec_ActiveCubeFace();
	const int fbWidth = VK_Exec_ActiveFramebufferWidth();
	const int fbHeight = VK_Exec_ActiveFramebufferHeight();
	if ( sceneTarget == NULL || sceneFace != 0 || !VK_Post_EnsureEarlyWorldTarget( fbWidth, fbHeight ) ) {
		return false;
	}
	idImage *capture = VK_Post_EnsureImage( vkPostScene.ssaoWorldDepth, "_vkSSAOWorldDepth", VK_Post_DepthImage );
	bool captured = false;
	if ( capture != NULL && VK_Exec_SetRenderTarget( vkPostScene.earlyWorldTarget, 0,
			r_vkPostDepthTransitionReference.GetBool() ) ) {
		captured = VK_Exec_DrawWorldDepth( mainView, VK_Post_SSAOWorldDepthSurf )
			&& VK_Post_CaptureDepth( capture, mainView );
	}
	if ( !VK_Exec_SetRenderTarget( sceneTarget, sceneFace ) || !captured ) {
		return false;
	}
	vkPostScene.earlyWorldView = mainView;
	vkPostScene.earlyWorldFrame = backEnd.frameCount;
	vkPostScene.ssaoWorldDepthWidth = VK_Post_ViewWidth( mainView );
	vkPostScene.ssaoWorldDepthHeight = VK_Post_ViewHeight( mainView );
	const bool masked = VK_Post_DrawSkyMask( portal, capture );
	static bool logged = false;
	if ( !logged ) {
		logged = true;
		common->Printf( "Vulkan: portal-sky mask %s\n", masked ? "drew" : "pass failed (backdrop unmasked)" );
	}
	return masked;
}

// ---- SSAO (RB_STD_SSAO) ----

// std140 layout of SSAOBlock in post_ssao.frag
typedef struct vkPostSSAOBlock_s {
	float	texInfo[ 4 ];
	float	projection[ 4 ];
	float	depthInfo[ 4 ];
	float	params[ 4 ];
	float	params2[ 4 ];
	float	modeInfo[ 4 ];		// x mode (0 classic, 1 field, 2 apply, 3 half, 4 upsample), y field rows flipped
	float	halfInfo[ 4 ];		// xy half-resolution target size, zw view size
} vkPostSSAOBlock_t;

static void VK_Post_FillSSAOBlock( vkPostSSAOBlock_t &block, const viewDef_t *viewDef,
		int width, int height, float mode, bool flip ) {
	memset( &block, 0, sizeof( block ) );
	block.texInfo[ 0 ] = 1.0f / (float)width;
	block.texInfo[ 1 ] = 1.0f / (float)height;
	block.texInfo[ 2 ] = 0.5f * (float)height * idMath::Fabs( viewDef->projectionMatrix[ 5 ] );
	block.texInfo[ 3 ] = r_ssaoDebug.GetBool() ? 1.0f : 0.0f;
	VK_Post_ProjectionInfo( viewDef, block.projection );
	block.depthInfo[ 0 ] = viewDef->projectionMatrix[ 10 ];
	block.depthInfo[ 1 ] = viewDef->projectionMatrix[ 14 ];
	block.params[ 0 ] = r_ssaoRadius.GetFloat();
	block.params[ 1 ] = r_ssaoBias.GetFloat();
	block.params[ 2 ] = r_ssaoIntensity.GetFloat();
	block.params[ 3 ] = r_ssaoPower.GetFloat();
	block.params2[ 0 ] = r_ssaoMaxDistance.GetFloat();
	block.params2[ 1 ] = (float)idMath::ClampInt( 4, 32, r_ssaoSamples.GetInteger() );
	block.modeInfo[ 0 ] = mode;
	block.modeInfo[ 1 ] = flip ? 1.0f : 0.0f;
	block.modeInfo[ 3 ] = r_vkSSAODeferredNormal.GetBool() ? 1.0f : 0.0f;
}

static bool VK_Post_SSAOFieldValid( const viewDef_t *viewDef ) {
	return vkPostScene.ssaoFieldFrame == backEnd.frameCount && vkPostScene.ssaoFieldView == viewDef
		&& vkPostScene.ssaoField != NULL;
}

bool VK_PostProcess_SSAOField( const viewDef_t *viewDef, idImage **image, bool *flip ) {
	if ( !VK_Post_SSAOFieldValid( viewDef ) ) {
		return false;
	}
	*image = vkPostScene.ssaoField;
	*flip = vkPostScene.ssaoFieldFlip;
	return true;
}

static idCVar r_vkSSAOReferenceComposition( "r_vkSSAOReferenceComposition", "0", CVAR_RENDERER | CVAR_BOOL,
	"use the resolved-color SSAO composition reference for validation" );

static bool VK_Post_EnsureColorTarget( idImage *&image, idRenderTexture *&target, const char *name,
		int width, int height, textureFilter_t filter, const char *label );

/*
====================
VK_Post_DrawSSAOHalf

The classic multiply pass at half resolution (R_SSAOHalfResolution). The AO
of the even pixel of each 2x2 block goes into a half-size target, computed as
the full-resolution pass computes it there; a full-resolution pass then
multiplies the scene by a depth-aware upsample of it. Sky and foreground
pixels stay untouched exactly as before. With debug set (r_ssaoDebug) the
upsample writes the AO itself instead, white where no AO applies. The
reference composition and native PBR's occlusion field keep the
full-resolution pass.
====================
*/
static bool VK_Post_DrawSSAOHalf( const viewDef_t *viewDef, VkShaderModule fragModule, idImage *worldDepth,
		idImage *finalDepth, int width, int height, bool debug ) {
	idRenderTexture *sceneTarget = VK_Exec_ActiveRenderTexture();
	const int sceneFace = VK_Exec_ActiveCubeFace();
	const int halfWidth = ( width + 1 ) / 2;
	const int halfHeight = ( height + 1 ) / 2;
	if ( !VK_Post_EnsureColorTarget( vkPostScene.ssaoHalf, vkPostScene.ssaoHalfTarget, "_vkSSAOHalf",
			halfWidth, halfHeight, TF_NEAREST, "SSAO half resolution" ) ) {
		vkPostFailReason = "the half-resolution target could not be created";
		return false;
	}
	vkPostSSAOBlock_t block;
	VK_Post_FillSSAOBlock( block, viewDef, width, height, 3.0f, false );
	block.halfInfo[ 0 ] = (float)halfWidth;
	block.halfInfo[ 1 ] = (float)halfHeight;
	block.halfInfo[ 2 ] = (float)width;
	block.halfInfo[ 3 ] = (float)height;
	bool drawn = false;
	if ( VK_Exec_SetRenderTarget( vkPostScene.ssaoHalfTarget, 0, r_vkPostDepthTransitionReference.GetBool() ) ) {
		const VkDescriptorSet sets[ 5 ] = {
			VK_Post_Descriptor( globalImages->whiteImage ), VK_Post_Descriptor( worldDepth ),
			VK_Post_Descriptor( finalDepth ), VK_Post_Descriptor( finalDepth ),
			VK_Post_Descriptor( globalImages->whiteImage )
		};
		const int uniformOffset = VK_Exec_InteractionUniformAlloc( &block, sizeof( block ) );
		const VkPipeline pipeline = VK_Exec_PostPipeline( VK_POST_SSAO, vkPost.fullscreenVert, fragModule,
				GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
		drawn = VK_Post_DrawTarget( pipeline, sets, 5, uniformOffset );
	}
	if ( !VK_Exec_SetRenderTarget( sceneTarget, sceneFace ) || !drawn ) {
		vkPostFailReason = "the half-resolution pass failed";
		return false;
	}
	block.modeInfo[ 0 ] = 4.0f;
	block.modeInfo[ 2 ] = 1.0f;
	const VkDescriptorSet sets[ 5 ] = {
		VK_Post_Descriptor( globalImages->whiteImage ), VK_Post_Descriptor( worldDepth ),
		VK_Post_Descriptor( finalDepth ), VK_Post_Descriptor( finalDepth ),
		VK_Post_Descriptor( vkPostScene.ssaoHalf )
	};
	const int uniformOffset = VK_Exec_InteractionUniformAlloc( &block, sizeof( block ) );
	const VkPipeline pipeline = VK_Exec_PostPipeline( VK_POST_SSAO, vkPost.fullscreenVert, fragModule,
			( debug ? GLS_SRCBLEND_ONE : GLS_SRCBLEND_DST_COLOR ) | GLS_DSTBLEND_ZERO );
	return VK_Post_DrawSceneRect( viewDef, pipeline, sets, 5, uniformOffset );
}

static bool VK_Post_DrawSSAO( const viewDef_t *viewDef ) {
	const VkShaderModule fragModule = VK_Post_SceneModule( VK_POST_MODULE_SSAO );
	if ( fragModule == VK_NULL_HANDLE || !VK_Post_ProjectionUsable( viewDef ) ) {
		vkPostFailReason = "no shader module or unusable projection";
		return false;
	}
	const int width = VK_Post_ViewWidth( viewDef );
	const int height = VK_Post_ViewHeight( viewDef );
	idImage *finalDepth;
	{
		vkGpuPhaseScope timing(VK_Exec_ActiveCmd(),VK_GPU_PHASE_SSAO_DEPTH);
		finalDepth = VK_Post_FinalDepth( viewDef );
	}
	if ( finalDepth == NULL ) {
		vkPostFailReason = "the depth copy failed";
		return false;
	}
	// AO is a scalar multiplier. Blend it into each existing scene sample,
	// preserving alpha, instead of resolving/copying the whole scene only to
	// multiply that copy in the shader. Keep the reference for debug views and
	// targets whose color domain is not explicitly known here.
	const idRenderTexture *target = VK_Exec_ActiveRenderTexture();
	const idImage *color = target != NULL && target->GetNumColorImages() == 1 ? target->GetColorImage( 0 ) : NULL;
	const bool field = VK_Post_SSAOFieldValid( viewDef );
	const bool composable = !field && !r_vkSSAOReferenceComposition.GetBool() && color != NULL
		&& ( color->GetOpts().format == FMT_RGBA8 || color->GetOpts().format == FMT_RGBA16F );
	const bool multiply = composable && !r_ssaoDebug.GetBool();
	// the world-only snapshot when the prepass took one at this size,
	// otherwise the finished depth, like OpenGL's _currentDepth fallback
	idImage *worldDepth = finalDepth;
	if ( vkPostScene.ssaoWorldDepthFrame == backEnd.frameCount && vkPostScene.ssaoWorldDepth != NULL
			&& vkPostScene.ssaoWorldDepthWidth == width && vkPostScene.ssaoWorldDepthHeight == height ) {
		worldDepth = vkPostScene.ssaoWorldDepth;
	}
	if ( composable && R_SSAOHalfResolution() ) {
		// The debug view shows the half-resolution AO the scene would receive.
		vkGpuPhaseScope drawTiming(VK_Exec_ActiveCmd(),VK_GPU_PHASE_SSAO_DRAW);
		return VK_Post_DrawSSAOHalf( viewDef, fragModule, worldDepth, finalDepth, width, height, !multiply );
	}
	idImage *scene = globalImages->whiteImage;
	if ( !multiply )
	{
		vkGpuPhaseScope timing(VK_Exec_ActiveCmd(),VK_GPU_PHASE_SSAO_COLOR);
		scene = VK_Post_CaptureScene( viewDef );
	}
	if ( scene == NULL ) {
		vkPostFailReason = "the scene copy failed";
		return false;
	}
	// A view with an occlusion field only applies its classic factor.
	vkGpuPhaseScope drawTiming(VK_Exec_ActiveCmd(),VK_GPU_PHASE_SSAO_DRAW);
	vkPostSSAOBlock_t block;
	VK_Post_FillSSAOBlock( block, viewDef, width, height, field ? 2.0f : 0.0f, field && vkPostScene.ssaoFieldFlip );
	block.modeInfo[ 2 ] = multiply ? 1.0f : 0.0f;

	const VkDescriptorSet sets[ 5 ] = {
		VK_Post_Descriptor( scene ), VK_Post_Descriptor( worldDepth ), VK_Post_Descriptor( finalDepth ),
		VK_Post_Descriptor( finalDepth ), VK_Post_Descriptor( field ? vkPostScene.ssaoField : globalImages->whiteImage )
	};
	const int uniformOffset = VK_Exec_InteractionUniformAlloc( &block, sizeof( block ) );
	const VkPipeline pipeline = VK_Exec_PostPipeline( VK_POST_SSAO, vkPost.fullscreenVert, fragModule,
			( multiply ? GLS_SRCBLEND_DST_COLOR : GLS_SRCBLEND_ONE ) | GLS_DSTBLEND_ZERO );
	return VK_Post_DrawSceneRect( viewDef, pipeline, sets, 5, uniformOffset );
}

static bool VK_Post_EnsureColorTarget( idImage *&image, idRenderTexture *&target, const char *name,
		int width, int height, textureFilter_t filter, const char *label );

/*
====================
VK_PostProcess_PrepareSSAOField

Right after the depth prepass of a view whose world depth was split, before
any light draws: the occlusion field native PBR folds into the AO of its
indirect light, as OpenGL's RB_PrepareSSAOIndirectField. It has the active
target's size and is drawn at the view's rectangle, so a fragment reads it at
gl_FragCoord; its rows are flipped when that target is the top-down swapchain.
The caller re-establishes its draw state afterwards.
====================
*/
bool VK_PostProcess_PrepareSSAOField( const viewDef_t *viewDef ) {
	vkPostScene.ssaoFieldFrame = -1;
	vkPostScene.ssaoFieldView = NULL;
	const int width = VK_Post_ViewWidth( viewDef );
	const int height = VK_Post_ViewHeight( viewDef );
	if ( vkPostScene.ssaoClassicDepthFrame != backEnd.frameCount || vkPostScene.ssaoWorldDepthFrame != backEnd.frameCount
			|| vkPostScene.ssaoWorldDepthWidth != width || vkPostScene.ssaoWorldDepthHeight != height
			|| width <= 0 || height <= 0 || !VK_Post_EnsureModules() || !VK_Post_ProjectionUsable( viewDef ) ) {
		return false;
	}
	const VkShaderModule fragModule = VK_Post_SceneModule( VK_POST_MODULE_SSAO );
	idImage *depth = VK_Post_EnsureImage( vkPostScene.ssaoFieldDepth, "_vkSSAOFieldDepth", VK_Post_DepthImage );
	if ( fragModule == VK_NULL_HANDLE || !VK_Post_CaptureDepth( depth, viewDef ) ) {
		return false;
	}
	idRenderTexture *sceneTarget = VK_Exec_ActiveRenderTexture();
	const int sceneFace = VK_Exec_ActiveCubeFace();
	const bool sceneLowerOrigin = VK_Exec_ActiveLowerOrigin();
	if ( !VK_Post_EnsureColorTarget( vkPostScene.ssaoField, vkPostScene.ssaoFieldTarget, "_vkSSAOField",
			VK_Exec_ActiveFramebufferWidth(), VK_Exec_ActiveFramebufferHeight(), TF_NEAREST, "SSAO field" ) ) {
		return false;
	}
	bool drawn = false, fieldLowerOrigin = false;
	if ( VK_Exec_SetRenderTarget( vkPostScene.ssaoFieldTarget, 0, r_vkPostDepthTransitionReference.GetBool() ) ) {
		fieldLowerOrigin = VK_Exec_ActiveLowerOrigin();
		vkPostSSAOBlock_t block;
		VK_Post_FillSSAOBlock( block, viewDef, width, height, 1.0f, false );
		const VkDescriptorSet sets[ 5 ] = {
			VK_Post_Descriptor( globalImages->whiteImage ), VK_Post_Descriptor( vkPostScene.ssaoWorldDepth ),
			VK_Post_Descriptor( depth ), VK_Post_Descriptor( vkPostScene.ssaoClassicDepth ),
			VK_Post_Descriptor( globalImages->whiteImage )
		};
		const int uniformOffset = VK_Exec_InteractionUniformAlloc( &block, sizeof( block ) );
		const VkPipeline pipeline = VK_Exec_PostPipeline( VK_POST_SSAO, vkPost.fullscreenVert, fragModule,
				GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
		drawn = VK_Post_DrawSceneRect( viewDef, pipeline, sets, 5, uniformOffset );
	}
	if ( !VK_Exec_SetRenderTarget( sceneTarget, sceneFace ) || !drawn ) {
		return false;
	}
	vkPostScene.ssaoFieldFlip = fieldLowerOrigin != sceneLowerOrigin;
	vkPostScene.ssaoFieldFrame = backEnd.frameCount;
	vkPostScene.ssaoFieldView = viewDef;
	return true;
}

// ---- motion blur (RB_STD_MotionBlur) ----

// RB_IsMainMotionBlurView
static bool VK_Post_IsMainMotionBlurView( const viewDef_t *viewDef ) {
	return VK_Post_IsMainSceneView( viewDef ) && !viewDef->isSubview
		&& viewDef->superView == NULL && viewDef->renderView.viewID >= 0;
}

// RB_BuildMotionBlurViewState
static bool VK_Post_BuildMotionViewState( const viewDef_t *viewDef, vkPostMotionViewState_t &state,
		int viewportWidth, int viewportHeight ) {
	const float projX = viewDef->projectionMatrix[ 0 ];
	const float projY = viewDef->projectionMatrix[ 5 ];
	if ( idMath::Fabs( projX ) <= 0.00001f || idMath::Fabs( projY ) <= 0.00001f ) {
		return false;
	}
	state.renderWorld = viewDef->renderWorld;
	state.mapName.Clear();
	if ( state.renderWorld != NULL ) {
		state.mapName = state.renderWorld->mapName;
	}
	state.videoRestartCount = tr.videoRestartCount;
	state.viewportWidth = viewportWidth;
	state.viewportHeight = viewportHeight;
	state.renderTime = viewDef->renderView.time;
	state.fovX = viewDef->renderView.fov_x;
	state.fovY = viewDef->renderView.fov_y;
	state.viewOrigin = viewDef->renderView.vieworg;
	state.viewAxis[ 0 ] = viewDef->renderView.viewaxis[ 0 ];
	state.viewAxis[ 1 ] = viewDef->renderView.viewaxis[ 1 ];
	state.viewAxis[ 2 ] = viewDef->renderView.viewaxis[ 2 ];
	state.reconstructInfo[ 0 ] = 1.0f / projX;
	state.reconstructInfo[ 1 ] = 1.0f / projY;
	state.reconstructInfo[ 2 ] = viewDef->projectionMatrix[ 8 ];
	state.reconstructInfo[ 3 ] = viewDef->projectionMatrix[ 9 ];
	state.projectInfo[ 0 ] = projX;
	state.projectInfo[ 1 ] = projY;
	state.projectInfo[ 2 ] = viewDef->projectionMatrix[ 8 ];
	state.projectInfo[ 3 ] = viewDef->projectionMatrix[ 9 ];
	state.depthProjection[ 0 ] = viewDef->projectionMatrix[ 10 ];
	state.depthProjection[ 1 ] = viewDef->projectionMatrix[ 14 ];
	memcpy( state.projectionMatrix, viewDef->projectionMatrix, sizeof( state.projectionMatrix ) );
	memcpy( state.worldModelViewMatrix, viewDef->worldSpace.modelViewMatrix, sizeof( state.worldModelViewMatrix ) );
	if ( viewDef->temporalNeutralProjectionValid ) {
		state.neutralProjectOffset[ 0 ] = viewDef->temporalNeutralProjectionOffset.x;
		state.neutralProjectOffset[ 1 ] = viewDef->temporalNeutralProjectionOffset.y;
	} else {
		state.neutralProjectOffset[ 0 ] = state.projectInfo[ 2 ];
		state.neutralProjectOffset[ 1 ] = state.projectInfo[ 3 ];
	}
	// Screenshots set forceUpdate to refresh dynamic models; only an authored
	// camera cut (Camera.cpp) is a discontinuity.
	state.forceUpdate = viewDef->renderView.forceUpdate && !viewDef->temporalCaptureFrame;
	return true;
}

static void VK_Post_MotionCameraSample( const vkPostMotionViewState_t &state, motionBlurCameraSample_t &sample ) {
	sample.timeMsec = state.renderTime;
	for ( int i = 0; i < 3; i++ ) {
		sample.origin[ i ] = state.viewOrigin[ i ];
		for ( int j = 0; j < 3; j++ ) {
			sample.axis[ i ][ j ] = state.viewAxis[ i ][ j ];
		}
	}
	sample.fov[ 0 ] = state.fovX;
	sample.fov[ 1 ] = state.fovY;
	sample.projectionNeutral[ 0 ] = state.projectInfo[ 0 ];
	sample.projectionNeutral[ 1 ] = state.projectInfo[ 1 ];
	sample.projectionNeutral[ 2 ] = state.neutralProjectOffset[ 0 ];
	sample.projectionNeutral[ 3 ] = state.neutralProjectOffset[ 1 ];
	sample.depthProjection[ 0 ] = state.depthProjection[ 0 ];
	sample.depthProjection[ 1 ] = state.depthProjection[ 1 ];
}

// RB_MotionBlurCameraMovedEnough
static bool VK_Post_MotionCameraMovedEnough( const vkPostMotionViewState_t &current,
		const vkPostMotionViewState_t &previous ) {
	motionBlurCameraSample_t currentSample;
	motionBlurCameraSample_t previousSample;
	VK_Post_MotionCameraSample( current, currentSample );
	VK_Post_MotionCameraSample( previous, previousSample );
	return MotionBlurCore_CameraMovedEnough( currentSample, previousSample );
}

// RB_MotionBlurValidateHistory
static motionBlurHistoryResult_t VK_Post_MotionValidateHistory( const vkPostMotionViewState_t &current,
		const vkPostMotionViewState_t &previous, bool allowStillCameraObjectVectors ) {
	motionBlurHistoryContext_t context;
	context.historyValid = vkPostScene.motionHistoryValid;
	context.videoRestarted = current.videoRestartCount != previous.videoRestartCount;
	context.worldChanged = current.renderWorld != previous.renderWorld
		|| current.mapName.Icmp( previous.mapName ) != 0;
	context.viewportChanged = MotionBlurCore_ViewportChanged( current.viewportWidth, current.viewportHeight,
		previous.viewportWidth, previous.viewportHeight );
	context.explicitCut = current.forceUpdate;
	context.allowStillCamera = allowStillCameraObjectVectors;
	motionBlurCameraSample_t currentSample;
	motionBlurCameraSample_t previousSample;
	VK_Post_MotionCameraSample( current, currentSample );
	VK_Post_MotionCameraSample( previous, previousSample );
	return MotionBlurCore_ValidateHistory( context, currentSample, previousSample );
}

// RB_MotionBlurAlignPreviousState: the previous camera with this frame's jitter
static bool VK_Post_MotionAlignPreviousState( const vkPostMotionViewState_t &current,
		const vkPostMotionViewState_t &previous, vkPostMotionViewState_t &aligned ) {
	aligned = previous;
	const float currentApplied[ 2 ] = { current.projectInfo[ 2 ], current.projectInfo[ 3 ] };
	float offsets[ 2 ];
	MotionBlurCore_AlignPreviousOffsets( currentApplied, current.neutralProjectOffset,
		previous.neutralProjectOffset, offsets );
	aligned.projectInfo[ 2 ] = aligned.projectionMatrix[ 8 ] = offsets[ 0 ];
	aligned.projectInfo[ 3 ] = aligned.projectionMatrix[ 9 ] = offsets[ 1 ];
	return currentApplied[ 0 ] != current.neutralProjectOffset[ 0 ]
		|| currentApplied[ 1 ] != current.neutralProjectOffset[ 1 ];
}

static void VK_Post_UpdateTemporalMotionEntityHistory( const viewDef_t *viewDef,
		idList<vkPostMotionEntityHistory_t> &history, idList<vkPostMotionEntityHistory_t> &nextHistory );

// RB_UpdateMotionBlurEntityHistory: every moving entity, as temporal AA keeps
static void VK_Post_UpdateMotionEntityHistory( const viewDef_t *viewDef ) {
	VK_Post_UpdateTemporalMotionEntityHistory( viewDef, vkPostMotionEntityHistory, vkPostMotionNextEntityHistory );
}

static int VK_Post_CompareMotionEntityHistory( const void *a, const void *b ) {
	return static_cast<const vkPostMotionEntityHistory_t *>( a )->entityIndex
		- static_cast<const vkPostMotionEntityHistory_t *>( b )->entityIndex;
}

// TAA's transform history covers every moving entity the next frame may draw
// exactly (rigid or posed, the view weapon included), sorted for lookups.
static void VK_Post_UpdateTemporalMotionEntityHistory( const viewDef_t *viewDef,
		idList<vkPostMotionEntityHistory_t> &history, idList<vkPostMotionEntityHistory_t> &nextHistory ) {
	// openQ4: keep the storage; Clear() made every frame regrow both lists
	nextHistory.SetNum( 0, false );
	for ( int i = 0; i < viewDef->numDrawSurfs; i++ ) {
		const drawSurf_t *surf = viewDef->drawSurfs[ i ];
		if ( surf == NULL || surf->space == NULL || surf->space->entityDef == NULL
				|| surf->space->entityDef->index < 0
				|| ( surf->space->entityDef->parms.hModel != NULL
					&& surf->space->entityDef->parms.hModel->IsStaticWorldModel() ) ) {
			continue;
		}
		vkPostMotionEntityHistory_t &entry = nextHistory.Alloc();
		entry.entityIndex = surf->space->entityDef->index;
		entry.model = surf->space->entityDef->parms.hModel;
		memcpy( entry.modelMatrix, surf->space->modelMatrix, sizeof( entry.modelMatrix ) );
	}
	if ( nextHistory.Num() > 1 ) {
		qsort( nextHistory.Ptr(), nextHistory.Num(), sizeof( vkPostMotionEntityHistory_t ),
			VK_Post_CompareMotionEntityHistory );
		int unique = 1;
		for ( int i = 1; i < nextHistory.Num(); i++ ) {
			if ( nextHistory[ i ].entityIndex != nextHistory[ unique - 1 ].entityIndex ) {
				nextHistory[ unique++ ] = nextHistory[ i ];
			}
		}
		nextHistory.SetNum( unique, false );
	}
	history.Swap( nextHistory );
	nextHistory.SetNum( 0, false );
}

static bool VK_Post_FindTemporalMotionEntityHistory( const idList<vkPostMotionEntityHistory_t> &history,
		const idRenderEntityLocal *entity, float previousModelMatrix[ 16 ] ) {
	int low = 0;
	int high = history.Num() - 1;
	while ( low <= high ) {
		const int middle = ( low + high ) >> 1;
		const int key = history[ middle ].entityIndex;
		if ( key == entity->index ) {
			if ( history[ middle ].model != entity->parms.hModel ) {
				return false;
			}
			memcpy( previousModelMatrix, history[ middle ].modelMatrix, sizeof( float ) * 16 );
			return true;
		}
		if ( key < entity->index ) {
			low = middle + 1;
		} else {
			high = middle - 1;
		}
	}
	return false;
}

static bool VK_Post_EnsureColorTarget( idImage *&image, idRenderTexture *&target, const char *name,
		int width, int height, textureFilter_t filter, const char *label ) {
	if ( width <= 0 || height <= 0 ) {
		return false;
	}
	if ( image == NULL ) {
		idImageOpts opts;
		opts.textureType = TT_2D;
		opts.format = FMT_RGBA16F;
		opts.width = width;
		opts.height = height;
		opts.numLevels = 1;
		opts.numMSAASamples = 0;
		opts.isPersistant = true;
		image = globalImages->ScratchImage( name, &opts, filter, TR_CLAMP, TD_DEFAULT );
		if ( image == NULL ) {
			return false;
		}
	}
	if ( target == NULL ) {
		if ( image->GetUploadWidth() != width || image->GetUploadHeight() != height ) {
			image->Resize( width, height );
		}
		target = tr.CreateRenderTexture( image, NULL );
		if ( target != NULL ) {
			target->SetDebugLabel( label );
		}
	} else if ( target->GetWidth() != width || target->GetHeight() != height ) {
		(void)tr.ResizeRenderTexture( target, width, height );
	}
	return target != NULL && target->GetWidth() == width && target->GetHeight() == height;
}

typedef struct vkPostMotionVectorPush_s {
	float	currentMvp[ 16 ];
	float	previousMvp[ 16 ];
} vkPostMotionVectorPush_t;

// post_motionvectors_reactive: the same 128-byte range, read as one matrix
// and a parameter vector
typedef struct vkPostReactiveCoveragePush_s {
	float	currentMvp[ 16 ];
	float	params[ 4 ];			// x: reactive strength
} vkPostReactiveCoveragePush_t;

/*
====================
VK_Post_RenderMotionVectors

RB_RenderMotionVectorBuffer: rigid entities that were drawn last frame write
their screen-space velocity, in pixels, into an RGBA16F target the size of
the view. The target has no depth attachment; the shader compares against the
finished depth instead. The positive-height viewport stores the rows
bottom-up like OpenGL's, which reverses the winding, so the front face flips
to keep the material cull.
====================
*/
static bool VK_Post_RenderMotionVectors( const viewDef_t *viewDef, const vkPostMotionViewState_t &previous,
		idImage *depthImage, idRenderTexture *sceneTarget,
		const idList<vkPostMotionEntityHistory_t> &entityHistory,
		idImage *&vectorImage, idRenderTexture *&vectorTarget,
		int width, int height, bool temporal, bool &complete ) {
	complete = false;
	// Motion blur (temporal == false) draws the same exact vectors as temporal
	// AA - rigid, posed and view-weapon surfaces - and leaves every other pixel
	// to camera reprojection; a view weapon without one is written as zero
	// motion, since camera reprojection would smear it whenever the view turns.
	motionBlurStats_t *blurStats = temporal ? NULL : &R_TemporalPresentation_MotionBlurStats();
	const int sceneCubeFace = VK_Exec_ActiveCubeFace();
	const VkShaderModule vertModule = VK_Post_SceneModule( VK_POST_MODULE_MOTION_VECTORS_VERT );
	const VkShaderModule fragModule = VK_Post_SceneModule( VK_POST_MODULE_MOTION_VECTORS_FRAG );
	const VkDescriptorSet depthSet = VK_Post_Descriptor( depthImage );
	if ( vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE || depthSet == VK_NULL_HANDLE
			|| !VK_Post_EnsureColorTarget( vectorImage, vectorTarget,
				temporal ? "_vkTemporalMotionVector" : "_vkMotionVector",
				width, height, TF_NEAREST, "Vulkan motion vectors" ) ) {
		return false;
	}
	// Motion blur reads a separate final-depth capture. Temporal consumers may
	// instead pass the scene attachment itself; retain its sampled transition.
	idImage *sceneDepth = sceneTarget != NULL ? sceneTarget->GetDepthImage() : NULL;
	const bool samplesSceneDepth = sceneDepth != NULL
		&& sceneDepth->GetDeviceHandle() == depthImage->GetDeviceHandle();
	if ( !VK_Exec_SetRenderTarget( vectorTarget, 0,
			samplesSceneDepth || r_vkPostDepthTransitionReference.GetBool() ) ) {
		VK_Exec_SetRenderTarget( sceneTarget, sceneCubeFace );
		return false;
	}
	const float clearColor[ 4 ] = { 0.0f, 0.0f, 0.0f, 0.0f };
	VK_Exec_ClearRenderTarget( true, false, 1.0f, clearColor );

	VkCommandBuffer cmd = VK_Exec_ActiveCmd();
	const VkPipeline pipeline = VK_Exec_ExtraPipeline( VK_POST_MOTION_VECTORS, vertModule, fragModule,
			GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO, VK_EXTRA_VERTEX_POSITION, 0 );
	// Temporal AA owns every moving surface per pixel: posed surfaces add a
	// previous-position stream, everything without exact motion marks the blue
	// reactive channel only. Motion blur shares the exact pipelines only.
	VkPipeline previousPipeline = VK_NULL_HANDLE;
	VkPipeline reactivePipeline = VK_NULL_HANDLE;
	const VkShaderModule previousVertModule = VK_Post_SceneModule( VK_POST_MODULE_MOTION_VECTORS_PREVIOUS_VERT );
	if ( previousVertModule != VK_NULL_HANDLE ) {
		previousPipeline = VK_Exec_ExtraPipeline( VK_POST_MOTION_VECTORS_PREVIOUS, previousVertModule, fragModule,
			GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO, VK_EXTRA_VERTEX_POSITION_PREVIOUS, 0 );
	}
	if ( temporal ) {
		const VkShaderModule reactiveVertModule = VK_Post_SceneModule( VK_POST_MODULE_MOTION_VECTORS_REACTIVE_VERT );
		const VkShaderModule reactiveFragModule = VK_Post_SceneModule( VK_POST_MODULE_MOTION_VECTORS_REACTIVE_FRAG );
		if ( reactiveVertModule != VK_NULL_HANDLE && reactiveFragModule != VK_NULL_HANDLE ) {
			reactivePipeline = VK_Exec_ExtraPipeline( VK_POST_MOTION_VECTORS_REACTIVE, reactiveVertModule, reactiveFragModule,
				GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO | GLS_REDMASK | GLS_GREENMASK | GLS_ALPHAMASK,
				VK_EXTRA_VERTEX_POSITION, 0 );
		}
	}
	// Sample the depth image using its recorded row order, including
	// both canonical scene depth and normalized post captures.
	const vkImageEntry_t *depthEntry = VK_Image_GetEntry( depthImage->GetDeviceHandle() );
	const bool depthFlip = depthEntry != NULL && depthEntry->materialSampleFlipY;
	float viewportSize[ 4 ] = { (float)width, (float)height, depthFlip ? 1.0f : 0.0f, 0.0f };
	const int uniformOffset = VK_Exec_InteractionUniformAlloc( viewportSize, sizeof( viewportSize ) );
	bool drew = false;
	if ( cmd != VK_NULL_HANDLE && pipeline != VK_NULL_HANDLE && uniformOffset >= 0
			&& VK_Exec_MainRenderingScopeOpen() ) {
		complete = true;
		VkViewport viewport;
		memset( &viewport, 0, sizeof( viewport ) );
		viewport.width = (float)width;
		viewport.height = (float)height;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport( cmd, 0, 1, &viewport );
		VK_Post_ResetDrawState( cmd );
		vkCmdSetFrontFace( cmd, VK_FRONT_FACE_CLOCKWISE );

		const VkPipelineLayout layout = VK_Exec_InteractionPipelineLayout();
		vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
		vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &depthSet, 0, NULL );
		const VkDescriptorSet uniformSet = VK_Exec_InteractionUniformSet();
		const uint32_t dynamicOffset = (uint32_t)uniformOffset;
		vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 6, 1, &uniformSet, 1, &dynamicOffset );

		const int slot = VK_Exec_ActiveFrameSlot();
		VkPipeline boundPipeline = pipeline;
		bool weaponDepthRange = false;
		temporalOwnershipStats_t ownership;
		memset( &ownership, 0, sizeof( ownership ) );
		ownership.frameNumber = backEnd.frameCount;
		for ( int i = 0; i < viewDef->numDrawSurfs; i++ ) {
			const drawSurf_t *surf = viewDef->drawSurfs[ i ];
			temporalSurfaceMotion_t motion = R_ScenePackets_TemporalSurfaceMotion( surf );
			if ( motion == TEMPORAL_SURFACE_MOTION_NONE ) {
				continue;
			}
			if ( temporal && motion != TEMPORAL_SURFACE_MOTION_REACTIVE ) { ++vkTemporalMotionEligible; }
			// a surface that should move exactly but cannot owns no history at all
			bool noHistory = false;
			bool weaponStill = false;
			float previousModelMatrix[ 16 ];
			if ( motion != TEMPORAL_SURFACE_MOTION_REACTIVE
					&& !VK_Post_FindTemporalMotionEntityHistory( entityHistory, surf->space->entityDef, previousModelMatrix ) ) {
				// newly visible: no previous transform, so it owns no history yet
				motion = TEMPORAL_SURFACE_MOTION_REACTIVE;
				noHistory = true;
			}
			const srfTriangles_t *tri = surf->geo;
			if ( blurStats != NULL && motion == TEMPORAL_SURFACE_MOTION_REACTIVE ) {
				if ( !surf->space->weaponDepthHack ) {
					// camera reprojection covers this surface's pixels
					blurStats->cameraFallback++;
					continue;
				}
				weaponStill = true;
				motion = TEMPORAL_SURFACE_MOTION_RIGID;
			}
			if ( tri->ambientCache == NULL || tri->indexes == NULL
					|| !VK_Exec_BindTriGeometry( cmd, slot, tri ) ) {
				complete = false;
				if ( blurStats != NULL ) {
					blurStats->cameraFallback++;
				} else {
					ownership.missed++;
				}
				continue;
			}
			if ( motion == TEMPORAL_SURFACE_MOTION_PREVIOUS_POSITIONS
					&& ( previousPipeline == VK_NULL_HANDLE
						|| !VK_Exec_BindPreviousPositions( cmd, slot, surf ) ) ) {
				if ( blurStats != NULL ) {
					if ( !surf->space->weaponDepthHack ) {
						blurStats->cameraFallback++;
						continue;
					}
					weaponStill = true;
					motion = TEMPORAL_SURFACE_MOTION_RIGID;
				} else {
					motion = TEMPORAL_SURFACE_MOTION_REACTIVE;
					noHistory = true;
				}
			}
			const VkPipeline wantedPipeline = motion == TEMPORAL_SURFACE_MOTION_REACTIVE ? reactivePipeline
				: ( motion == TEMPORAL_SURFACE_MOTION_PREVIOUS_POSITIONS ? previousPipeline : pipeline );
			if ( wantedPipeline == VK_NULL_HANDLE ) {
				complete = false;
				ownership.missed++;
				continue;
			}
			if ( blurStats != NULL ) {
				if ( weaponStill ) {
					blurStats->weaponStill++;
				} else if ( motion == TEMPORAL_SURFACE_MOTION_PREVIOUS_POSITIONS ) {
					blurStats->posed++;
				} else {
					blurStats->rigid++;
				}
				if ( surf->space->weaponDepthHack && !weaponStill ) {
					blurStats->weapon++;
				}
			} else if ( motion == TEMPORAL_SURFACE_MOTION_REACTIVE ) {
				ownership.reactive++;
			} else {
				if ( motion == TEMPORAL_SURFACE_MOTION_PREVIOUS_POSITIONS ) {
					ownership.posed++;
				} else {
					ownership.rigid++;
				}
				if ( surf->space->weaponDepthHack ) {
					ownership.weapon++;
				}
			}
			if ( wantedPipeline != boundPipeline ) {
				// every variant shares the interaction layout, so the bound
				// depth and uniform sets stay valid
				vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, wantedPipeline );
				boundPipeline = wantedPipeline;
			}
			// the view weapon is drawn into depth range 0..0.5
			const bool wantWeaponRange = surf->space->weaponDepthHack;
			if ( wantWeaponRange != weaponDepthRange ) {
				weaponDepthRange = wantWeaponRange;
				viewport.maxDepth = wantWeaponRange ? 0.5f : 1.0f;
				vkCmdSetViewport( cmd, 0, 1, &viewport );
			}

			// TAA runs after scene scaling restores the view rectangles. Scale
			// the native scissor to the actual vector-target extent.
			VkRect2D scissor;
			scissor.offset.x = 0;
			scissor.offset.y = 0;
			scissor.extent.width = (uint32_t)width;
			scissor.extent.height = (uint32_t)height;
			if ( r_useScissor.GetBool() && !surf->scissorRect.IsEmpty() ) {
				const float scaleX = (float)width / Max( 1, VK_Post_ViewWidth( viewDef ) );
				const float scaleY = (float)height / Max( 1, VK_Post_ViewHeight( viewDef ) );
				const int x1 = idMath::ClampInt( 0, width - 1, idMath::Ftoi( surf->scissorRect.x1 * scaleX ) );
				const int y1 = idMath::ClampInt( 0, height - 1, idMath::Ftoi( surf->scissorRect.y1 * scaleY ) );
				const int x2 = idMath::ClampInt( x1 + 1, width, idMath::Ceil( ( surf->scissorRect.x2 + 1 ) * scaleX ) );
				const int y2 = idMath::ClampInt( y1 + 1, height, idMath::Ceil( ( surf->scissorRect.y2 + 1 ) * scaleY ) );
				scissor.offset.x = x1;
				scissor.offset.y = y1;
				scissor.extent.width = (uint32_t)( x2 - x1 );
				scissor.extent.height = (uint32_t)( y2 - y1 );
			}
			vkCmdSetScissorClamped( cmd, 0, 1, &scissor );

			switch ( surf->material->GetCullType() ) {
				case CT_TWO_SIDED:
					vkCmdSetCullMode( cmd, VK_CULL_MODE_NONE );
					break;
				case CT_BACK_SIDED:
					vkCmdSetCullMode( cmd, VK_CULL_MODE_BACK_BIT );
					break;
				default:
					vkCmdSetCullMode( cmd, VK_CULL_MODE_FRONT_BIT );
					break;
			}

			if ( motion == TEMPORAL_SURFACE_MOTION_REACTIVE ) {
				vkPostReactiveCoveragePush_t push;
				VK_BuildSurfMVP( viewDef, surf, push.currentMvp );
				push.params[ 0 ] = noHistory ? 1.0f : R_ScenePackets_TemporalSurfaceReactiveStrength( surf );
				push.params[ 1 ] = push.params[ 2 ] = push.params[ 3 ] = 0.0f;
				vkCmdPushConstants( cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
						0, sizeof( push ), &push );
				vkCmdDrawIndexed( cmd, (uint32_t)tri->numIndexes, 1, 0, 0, 0 );
				drew = true;
				continue;
			}
			// The weapon's depth hack only rescales clip z, which the vector
			// ignores, so last frame's plain projection still matches.
			vkPostMotionVectorPush_t push;
			VK_BuildSurfMVP( viewDef, surf, push.currentMvp );
			if ( weaponStill ) {
				// this frame's own transform: an exact, zero vector
				memcpy( push.previousMvp, push.currentMvp, sizeof( push.previousMvp ) );
			} else {
				float previousModelView[ 16 ];
				myGlMultMatrix( previousModelMatrix, previous.worldModelViewMatrix, previousModelView );
				myGlMultMatrix( previousModelView, previous.projectionMatrix, push.previousMvp );
			}
			vkCmdPushConstants( cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
					0, sizeof( push ), &push );
			vkCmdDrawIndexed( cmd, (uint32_t)tri->numIndexes, 1, 0, 0, 0 );
			if ( temporal ) { ++vkTemporalMotionDrawn; }
			drew = true;
		}
		if ( weaponDepthRange ) {
			viewport.maxDepth = 1.0f;
			vkCmdSetViewport( cmd, 0, 1, &viewport );
		}
		vkCmdSetFrontFace( cmd, VK_Exec_CanonicalFrontFace() );
		if ( temporal ) {
			ownership.complete = complete;
			R_TemporalPresentation_RecordOwnership( ownership );
		}
	}
	if ( drew ) { VK_Exec_MarkColorOrigin( false ); }
	complete = VK_Exec_SetRenderTarget( sceneTarget, sceneCubeFace ) && complete;
	return drew;
}

idImage *VK_PostProcess_TemporalMotionVectors( const viewDef_t *viewDef,
		idImage *depthImage, int width, int height, unsigned int generation,
		bool useHistory, bool &complete ) {
	complete = false;
	VK_PostProcess_SelectTemporalMotionSlot( viewDef != NULL ? viewDef->temporalHistorySlot : 0 );
	vkTemporalMotionEligible = vkTemporalMotionDrawn = 0;
	vkTemporalMotionComplete = false;
	if ( !useHistory || viewDef == NULL || depthImage == NULL || R_SubmittedTakingScreenshot()
			|| viewDef->temporalCaptureFrame || !viewDef->temporalPreviousProjectionValid
			|| generation != vkTemporalMotionGeneration
			|| generation != R_TemporalPresentation_HistoryGeneration()
			|| viewDef->temporalViewIdentity != vkTemporalMotionViewIdentity
			|| vkTemporalMotionFrame != backEnd.frameCount - 1
			|| width != vkTemporalMotionViewHistory.viewportWidth
			|| height != vkTemporalMotionViewHistory.viewportHeight ) {
		VK_PostProcess_ResetTemporalMotion();
		return NULL;
	}
	const bool drawn = VK_Post_RenderMotionVectors( viewDef, vkTemporalMotionViewHistory,
		depthImage, VK_Exec_ActiveRenderTexture(), vkTemporalMotionEntityHistory,
		vkTemporalMotionImage, vkTemporalMotionTarget, width, height, true, complete );
	vkTemporalMotionComplete = complete;
	if ( complete ) { ++vkTemporalMotionCompletedViews; }
	// A complete per-pixel target is meaningful even when nothing moved: its
	// cleared texels say "camera reprojection, no reactive coverage".
	return ( drawn || complete ) ? vkTemporalMotionImage : NULL;
}

void VK_PostProcess_CommitTemporalMotion( const viewDef_t *viewDef,
		int width, int height, unsigned int generation ) {
	VK_PostProcess_SelectTemporalMotionSlot( viewDef != NULL ? viewDef->temporalHistorySlot : 0 );
	if ( viewDef == NULL || R_SubmittedTakingScreenshot() || viewDef->temporalCaptureFrame
			|| generation != R_TemporalPresentation_HistoryGeneration()
			|| !VK_Post_BuildMotionViewState( viewDef, vkTemporalMotionViewHistory, width, height ) ) {
		VK_PostProcess_ResetTemporalMotion();
		return;
	}
	VK_Post_UpdateTemporalMotionEntityHistory( viewDef, vkTemporalMotionEntityHistory, vkTemporalMotionNextEntityHistory );
	vkTemporalMotionGeneration = generation;
	vkTemporalMotionViewIdentity = viewDef->temporalViewIdentity;
	vkTemporalMotionFrame = backEnd.frameCount;
}

// std140 layout of MotionBlurBlock in post_motionblur.frag
typedef struct vkPostMotionBlurBlock_s {
	float	texInfo[ 4 ];
	float	currentReconstructInfo[ 4 ];
	float	previousProjectInfo[ 4 ];
	float	depthInfo[ 4 ];
	float	currentViewOrigin[ 4 ];
	float	currentViewAxis[ 3 ][ 4 ];
	float	previousViewOrigin[ 4 ];
	float	previousViewAxis[ 3 ][ 4 ];
	float	motionBlurParams[ 4 ];
	float	motionBlurObjectParams[ 4 ];
} vkPostMotionBlurBlock_t;

static void VK_Post_CopyVec3( float out[ 4 ], const idVec3 &value ) {
	out[ 0 ] = value.x;
	out[ 1 ] = value.y;
	out[ 2 ] = value.z;
	out[ 3 ] = 0.0f;
}

// Returns true when it drew into the scene.
static bool VK_Post_MotionBlur( const viewDef_t *viewDef, idRenderTexture *sceneTarget ) {
	if ( !r_motionBlur.GetBool() ) {
		VK_Post_ResetMotionBlurHistory();
		return false;
	}
	if ( r_skipPostProcess.GetBool() ) {
		if ( VK_Post_IsMainMotionBlurView( viewDef ) ) {
			VK_Post_MotionBlurRecordDisabled( "r_skipPostProcess" );
		}
		VK_Post_ResetMotionBlurHistory();
		return false;
	}
	motionBlurStats_t &stats = R_TemporalPresentation_MotionBlurStats();
	if ( !VK_Post_IsMainMotionBlurView( viewDef ) ) {
		// Subviews have independent cameras and must not invalidate the main
		// camera's timeline. Its world, time, projection and restart checks
		// below reject discontinuities when the next main view arrives.
		if ( viewDef != NULL && viewDef->viewEntitys != NULL ) {
			stats.subviewSkips++;
		}
		return false;
	}
	stats.mainViews++;
	stats.lastFrame = backEnd.frameCount;
	vkGpuPhaseScope timing( VK_Exec_ActiveCmd(), VK_GPU_PHASE_MOTION );
	if ( !r_motionBlurDebug.GetBool() && ( r_motionBlurStrength.GetFloat() <= 0.0f
			|| r_motionBlurMaxPixels.GetFloat() <= 0.0f || r_motionBlurSamples.GetInteger() <= 0 ) ) {
		VK_Post_MotionBlurRecordDisabled( "zero strength, radius or samples" );
		VK_Post_ResetMotionBlurHistory();
		return false;
	}
	const VkShaderModule fragModule = VK_Post_SceneModule( VK_POST_MODULE_MOTION_BLUR );
	if ( r_jitter.GetBool() || fragModule == VK_NULL_HANDLE ) {
		VK_Post_MotionBlurRecordDisabled( r_jitter.GetBool() ? "r_jitter" : "motion blur program unavailable" );
		VK_Post_ResetMotionBlurHistory();
		return false;
	}
	const int width = VK_Post_ViewWidth( viewDef );
	const int height = VK_Post_ViewHeight( viewDef );
	vkPostMotionViewState_t currentState;
	if ( width <= 0 || height <= 0 || !VK_Post_BuildMotionViewState( viewDef, currentState, width, height ) ) {
		VK_Post_MotionBlurRecordDisabled( width <= 0 || height <= 0 ? "empty viewport" : "degenerate projection" );
		VK_Post_ResetMotionBlurHistory();
		return false;
	}

	const vkPostMotionViewState_t previousState = vkPostMotionHistory;
	const bool objectVectorsRequested = r_motionBlurObjectVectors.GetBool();
	const bool cameraMovedEnough = vkPostScene.motionHistoryValid
		&& VK_Post_MotionCameraMovedEnough( currentState, previousState );
	const motionBlurHistoryResult_t historyResult =
		VK_Post_MotionValidateHistory( currentState, previousState, objectVectorsRequested );
	stats.history[ historyResult ]++;
	const bool historyUsable = historyResult == MOTION_BLUR_HISTORY_USABLE;
	if ( historyUsable ) {
		MotionBlurCore_UpdateShutter( vkPostMotionShutter, currentState.renderTime - previousState.renderTime );
	} else if ( historyResult != MOTION_BLUR_HISTORY_CAMERA_STILL ) {
		// a still camera keeps the frame interval; a discontinuity does not
		MotionBlurCore_ResetShutter( vkPostMotionShutter );
	}
	vkPostMotionHistory = currentState;
	vkPostScene.motionHistoryValid = true;
	if ( !historyUsable ) {
		vkPostScene.motionVectorValid = false;
		VK_Post_UpdateMotionEntityHistory( viewDef );
		return false;
	}

	idImage *depthImage = VK_Post_FinalDepth( viewDef );
	idImage *scene = VK_Post_CaptureScene( viewDef );
	if ( depthImage == NULL || scene == NULL ) {
		VK_Post_MotionBlurRecordDisabled( "scene or depth capture unavailable" );
		VK_Post_ResetMotionBlurHistory();
		return false;
	}
	// Temporal AA jitters this frame's projection; cancel it out of the
	// previous camera so neither the history test nor the vectors see it.
	vkPostMotionViewState_t alignedPreviousState;
	const bool jitterAligned = VK_Post_MotionAlignPreviousState( currentState, previousState, alignedPreviousState );
	vkPostScene.motionVectorValid = false;
	if ( objectVectorsRequested ) {
		bool complete = false;
		vkPostScene.motionVectorValid = VK_Post_RenderMotionVectors( viewDef, alignedPreviousState, depthImage, sceneTarget,
			vkPostMotionEntityHistory, vkPostScene.motionVectorImage, vkPostScene.motionVectorTarget,
			width, height, false, complete );
	}
	VK_Post_UpdateMotionEntityHistory( viewDef );
	if ( VK_Exec_ActiveRenderTexture() != sceneTarget ) {
		return false;
	}
	const float shutterScale = MotionBlurCore_ShutterScale( vkPostMotionShutter,
		R_TemporalPresentation_MotionBlurShutterMsec() );

	vkPostMotionBlurBlock_t block;
	memset( &block, 0, sizeof( block ) );
	block.texInfo[ 0 ] = 1.0f / (float)width;
	block.texInfo[ 1 ] = 1.0f / (float)height;
	block.texInfo[ 2 ] = (float)width;
	block.texInfo[ 3 ] = (float)height;
	memcpy( block.currentReconstructInfo, currentState.reconstructInfo, sizeof( block.currentReconstructInfo ) );
	memcpy( block.previousProjectInfo, alignedPreviousState.projectInfo, sizeof( block.previousProjectInfo ) );
	block.depthInfo[ 0 ] = currentState.depthProjection[ 0 ];
	block.depthInfo[ 1 ] = currentState.depthProjection[ 1 ];
	VK_Post_CopyVec3( block.currentViewOrigin, currentState.viewOrigin );
	VK_Post_CopyVec3( block.previousViewOrigin, previousState.viewOrigin );
	for ( int i = 0; i < 3; i++ ) {
		VK_Post_CopyVec3( block.currentViewAxis[ i ], currentState.viewAxis[ i ] );
		VK_Post_CopyVec3( block.previousViewAxis[ i ], previousState.viewAxis[ i ] );
	}
	block.motionBlurParams[ 0 ] = idMath::ClampFloat( 0.0f, 2.0f, r_motionBlurStrength.GetFloat() );
	block.motionBlurParams[ 1 ] = idMath::ClampFloat( 0.0f, 64.0f, r_motionBlurMaxPixels.GetFloat() );
	block.motionBlurParams[ 2 ] = (float)idMath::ClampInt( 1, 16, r_motionBlurSamples.GetInteger() );
	block.motionBlurParams[ 3 ] = r_motionBlurDebug.GetBool() ? 1.0f : 0.0f;
	block.motionBlurObjectParams[ 0 ] = vkPostScene.motionVectorValid ? 1.0f : 0.0f;
	block.motionBlurObjectParams[ 1 ] = cameraMovedEnough ? 1.0f : 0.0f;
	block.motionBlurObjectParams[ 2 ] = shutterScale;

	idImage *velocity = vkPostScene.motionVectorValid ? vkPostScene.motionVectorImage : globalImages->blackImage;
	const VkDescriptorSet sets[ 3 ] = {
		VK_Post_Descriptor( scene ), VK_Post_Descriptor( depthImage ), VK_Post_Descriptor( velocity )
	};
	const int uniformOffset = VK_Exec_InteractionUniformAlloc( &block, sizeof( block ) );
	const VkPipeline pipeline = VK_Exec_PostPipeline( VK_POST_MOTION_BLUR, vkPost.fullscreenVert, fragModule,
			GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	const bool drew = VK_Post_DrawSceneRect( viewDef, pipeline, sets, 3, uniformOffset );
	if ( drew ) {
		stats.drawnViews++;
		if ( vkPostScene.motionVectorValid ) {
			stats.objectVectorViews++;
		}
		if ( jitterAligned ) {
			stats.jitterAlignedViews++;
		}
		motionBlurCameraSample_t currentSample;
		motionBlurCameraSample_t previousSample;
		VK_Post_MotionCameraSample( currentState, currentSample );
		VK_Post_MotionCameraSample( previousState, previousSample );
		const float centerPixels = MotionBlurCore_CenterRotationPixels( currentSample, previousSample,
			(float)width, (float)height );
		stats.centerSamples++;
		stats.centerPixelsSum += centerPixels;
		stats.scaledCenterPixelsSum += centerPixels * shutterScale;
		stats.lastShutterScale = shutterScale;
		stats.lastFrameMsec = vkPostMotionShutter.valid ? vkPostMotionShutter.smoothedFrameMsec : 0.0f;
		stats.lastStrength = block.motionBlurParams[ 0 ];
		stats.lastMaxPixels = block.motionBlurParams[ 1 ];
		stats.lastSamples = (int)block.motionBlurParams[ 2 ];
	}
	return drew;
}

// ---- bloom, tone map and grade (RB_STD_Bloom) ----

typedef struct vkPostBloomBlurBlock_s {
	float	params[ 4 ];
	float	radius[ 4 ];
} vkPostBloomBlurBlock_t;

// std140 layout of BloomCompositeBlock in post_bloom_composite.frag
typedef struct vkPostBloomCompositeBlock_s {
	float	bloom[ 4 ];
	float	exposure[ 4 ];
	float	grade[ 4 ];
	float	highlight[ 4 ];
	float	weights[ 4 ];
	float	weights2[ 4 ];
} vkPostBloomCompositeBlock_t;

// RB_GetBloomLevelSize: level 0 is the view, each next level halves, rounding up
static void VK_Post_BloomLevelSize( int level, int viewWidth, int viewHeight, int &width, int &height ) {
	width = viewWidth;
	height = viewHeight;
	for ( int i = 0; i < level; i++ ) {
		width = Max( 1, ( width + 1 ) / 2 );
		height = Max( 1, ( height + 1 ) / 2 );
	}
}

struct vkPostPreparedStep_t {
	idRenderTexture *target;
	VkPipeline pipeline;
	VkDescriptorSet sourceSet;
	int uniformOffset;
};
struct vkPostPreparedSteps_t {
	vkPostPreparedStep_t steps[VK_HDR_MAX_LEVELS + 3 * VK_POST_BLOOM_MAX_LEVELS];
	int count;
};

static bool VK_Post_ExecuteSteps( const vkPostPreparedSteps_t &plan ) {
	for ( int i = 0; i < plan.count; ++i ) {
		const vkPostPreparedStep_t &step = plan.steps[i];
		if ( !VK_Exec_SetRenderTarget( step.target, 0, r_vkPostDepthTransitionReference.GetBool() )
				|| !VK_Post_DrawTarget( step.pipeline, &step.sourceSet, 1, step.uniformOffset ) ) { return false; }
	}
	return true;
}

// The same builder serves ordinary immediate passes and complete-view HDR
// preparation. Preparing allocates every target, pipeline, descriptor and
// uniform slice without sampling or writing any scene/pyramid contents.
static bool VK_Post_DrawBloomStep( idRenderTexture *target, int kind, int module, idImage *source,
		const void *block, int blockBytes, vkPostPreparedSteps_t *plan = NULL ) {
	const VkShaderModule fragModule = VK_Post_SceneModule( module );
	if ( fragModule == VK_NULL_HANDLE || !VK_Exec_SetRenderTarget( target, 0, r_vkPostDepthTransitionReference.GetBool() ) ) {
		return false;
	}
	const VkDescriptorSet sourceSet = VK_Post_Descriptor( source );
	const int uniformOffset = VK_Exec_InteractionUniformAlloc( block, blockBytes );
	const VkPipeline pipeline = VK_Exec_PostPipeline( kind, vkPost.fullscreenVert, fragModule,
			GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	if ( !VK_Post_SetsReady( pipeline, &sourceSet, 1, uniformOffset ) ) { return false; }
	if ( plan != NULL ) {
		if ( plan->count >= int( sizeof( plan->steps ) / sizeof( plan->steps[0] ) ) ) { return false; }
		vkPostPreparedStep_t &step = plan->steps[plan->count++];
		step.target = target;
		step.pipeline = pipeline;
		step.sourceSet = sourceSet;
		step.uniformOffset = uniformOffset;
		return true;
	}
	return VK_Post_DrawTarget( pipeline, &sourceSet, 1, uniformOffset );
}

static idImage *VK_Post_BuildHDRPyramid( const viewDef_t *viewDef, idImage *scene, vkPostPreparedSteps_t *plan = NULL ) {
	idImage *source = scene;
	int width = VK_Post_ViewWidth( viewDef ), height = VK_Post_ViewHeight( viewDef );
	for ( int level = 0; level < VK_HDR_MAX_LEVELS; ++level ) {
		const int nextWidth = Max( 1, ( width + 1 ) / 2 ), nextHeight = Max( 1, ( height + 1 ) / 2 );
		const float block[4] = { 1.0f / width, 1.0f / height, level == 0 ? 1.0f : 0.0f, 0.0f };
		// Not va(): its shared ring is not thread safe and this can run on the render thread.
		char targetName[32];
		idStr::snPrintf( targetName, sizeof( targetName ), "_vkHDRLuminance%d", level );
		if ( !VK_Post_EnsureColorTarget( vkHDR.images[level], vkHDR.targets[level],
				targetName, nextWidth, nextHeight, TF_LINEAR, "Vulkan HDR luminance" )
				|| !VK_Post_DrawBloomStep( vkHDR.targets[level], VK_POST_HDR_LUMINANCE,
					VK_POST_MODULE_HDR_LUMINANCE, source, block, sizeof( block ), plan ) ) { return NULL; }
		source = vkHDR.images[level];
		width = nextWidth;
		height = nextHeight;
		if ( width == 1 && height == 1 ) { return source; }
	}
	return NULL;
}

static void VK_Post_AdaptHDRExposure( float time ) {
	if ( vkHDR.haveSample ) {
		const hdrExposureSettings_t settings = { r_hdrKeyValue.GetFloat(), r_hdrMinExposure.GetFloat(),
			r_hdrMaxExposure.GetFloat(), r_hdrAdaptUpSpeed.GetFloat(), r_hdrAdaptDownSpeed.GetFloat() };
		(void)HDRExposure_Update( vkHDR.adaptation, vkHDR.logLuminance, time, settings );
	}
}

static float VK_Post_UpdateHDRAutoExposure( const viewDef_t *viewDef, idImage *scene,
		const vkPostPreparedSteps_t *plan = NULL, idImage *preparedLuminance = NULL ) {
	if ( !vkHDR.enabled || !r_hdrAutoExposure.GetBool() || !r_hdrToneMap.GetBool() ) {
		return 1.0f;
	}
	// Captures use the already adapted exposure and never advance its history.
	if ( R_SubmittedTakingScreenshot() || viewDef->temporalCaptureFrame || vkHDR.queuedFrame == backEnd.frameCount ) {
		return vkHDR.adaptation.exposure;
	}
	const bool synchronous = !r_hdrAutoExposureAsync.GetBool();
	if ( !synchronous ) {
		VK_Post_AdaptHDRExposure( viewDef->floatTime );
	}
	idRenderTexture *savedTarget = VK_Exec_ActiveRenderTexture();
	const int savedFace = VK_Exec_ActiveCubeFace();
	idImage *source = plan != NULL ? ( VK_Post_ExecuteSteps( *plan ) ? preparedLuminance : NULL )
		: VK_Post_BuildHDRPyramid( viewDef, scene );
	const bool queued = source != NULL
		&& VK_Exec_QueueHDRExposureReadback( source, vkHDR.generation, backEnd.frameCount, synchronous );
	(void)VK_Exec_SetRenderTarget( savedTarget, savedFace );
	if ( queued ) {
		vkHDR.queuedFrame = backEnd.frameCount;
		++vkHDR.queuedSamples;
		if ( synchronous ) {
			VK_Post_AdaptHDRExposure( viewDef->floatTime );
		}
	} else {
		static bool warned = false;
		if ( !warned ) {
			warned = true;
			common->Warning( "Vulkan: HDR luminance sample unavailable; retaining current exposure" );
		}
	}
	return vkHDR.adaptation.exposure;
}

// Drive the real scene capture, FP16 reduction and frame-fenced readback. A
// constant radiance fixture has an independent analytic log-luminance result;
// values above one catch an accidental UNORM scene or MSAA resolve immediately.
static bool VK_Post_TestHDRCase( int width, int height, int samples, bool asynchronous,
		const float color[ 4 ], viewDef_t &view ) {
	idImageOpts options;
	options.textureType = TT_2D;
	options.format = FMT_RGBA16F;
	options.width = width;
	options.height = height;
	options.numLevels = 1;
	options.numMSAASamples = samples;
	options.isPersistant = true;
	idImage *image = globalImages->ScratchImage( "_vkHDRTestScene", &options, TF_LINEAR, TR_CLAMP, TD_DEFAULT );
	if ( image == NULL ) {
		return false;
	}
	// ScratchImage can return an existing image. Reallocate deliberately to
	// exercise format/extent/sample storage retirement across successive cases.
	image->AllocImage( options, TF_LINEAR, TR_CLAMP );
	idRenderTexture target( image, NULL );
	target.SetDebugLabel( "Vulkan HDR numerical fixture" );
	view.viewport.x1 = view.viewport.y1 = view.scissor.x1 = view.scissor.y1 = 0;
	view.viewport.x2 = view.scissor.x2 = width - 1;
	view.viewport.y2 = view.scissor.y2 = height - 1;
	view.floatTime += 2.0f;
	++backEnd.frameCount;
	r_hdrAutoExposureAsync.SetBool( asynchronous );
	bool passed = VK_GuiExecutor_BeginFrame() && target.MakeCurrent()
		&& image->GetOpts().format == FMT_RGBA16F
		&& ( samples <= 1 || image->GetOpts().numMSAASamples == samples );
	if ( passed ) {
		VK_Post_SynchronizeHDRExposure( &view );
		VK_Post_ResetHDRExposure();
		VK_Exec_ClearRenderTarget( true, false, 1.0f, color );
		idImage *scene = VK_Post_CaptureScene( &view );
		const unsigned int queuedBefore = vkHDR.queuedSamples;
		if ( scene != NULL ) {
			(void)VK_Post_UpdateHDRAutoExposure( &view, scene );
		}
		passed = scene != NULL && vkHDR.queuedSamples == queuedBefore + 1
			&& ( asynchronous ? !vkHDR.haveSample : vkHDR.haveSample );
	}
	if ( VK_GuiExecutor_FrameIsOpen() ) {
		passed = VK_Exec_SetRenderTarget( NULL ) && passed;
		passed = VK_GuiExecutor_EndFrameAndPresent() && passed;
	}
	// Retire the normal ring; the consumer must publish this exact generation
	// and frame only once. No readback-specific queue/device wait is inserted.
	for ( int i = 0; i < VK_FRAMES_IN_FLIGHT && passed; ++i ) {
		passed = VK_GuiExecutor_BeginFrame();
		if ( VK_GuiExecutor_FrameIsOpen() ) {
			passed = VK_GuiExecutor_EndFrameAndPresent() && passed;
		}
	}
	const float expectedLuminance = Max( 0.0001f, color[ 0 ] * 0.2126f + color[ 1 ] * 0.7152f + color[ 2 ] * 0.0722f );
	const float expectedLog = std::log( expectedLuminance );
	VK_Post_AdaptHDRExposure( view.floatTime );
	const float expectedExposure = idMath::ClampFloat( r_hdrMinExposure.GetFloat(), r_hdrMaxExposure.GetFloat(),
		r_hdrKeyValue.GetFloat() / expectedLuminance );
	passed = passed && vkHDR.haveSample && vkHDR.completedFrame == backEnd.frameCount
		&& idMath::Fabs( vkHDR.logLuminance - expectedLog ) < 0.012f
		&& vkHDR.adaptation.initialized
		&& idMath::Fabs( vkHDR.adaptation.exposure - expectedExposure ) < Max( 0.002f, expectedExposure * 0.015f );
	common->Printf( "Vulkan HDR fixture: extent=%dx%d samples=%d async=%d rgb=%g,%g,%g log=%g expected=%g exposure=%g expectedExposure=%g %s\n",
		width, height, samples, (int)asynchronous, color[ 0 ], color[ 1 ], color[ 2 ], vkHDR.logLuminance,
		expectedLog, vkHDR.adaptation.exposure, expectedExposure, passed ? "passed" : "FAILED" );
	image->PurgeImage();
	return passed;
}

static bool VK_Post_TestToneMapping( viewDef_t &view );
static bool VK_Post_TestLinearToneMapping( viewDef_t &view );
static bool VK_Post_TestPreparedHDR( viewDef_t &view );

void R_RendererVulkanHDRSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	idCVar *settings[] = { &r_skipPostProcess, &r_hdrSceneTarget, &r_hdrToneMap, &r_hdrAutoExposure,
		&r_hdrAutoExposureAsync, &r_hdrKeyValue, &r_hdrMinExposure, &r_hdrMaxExposure };
	idStr previous[ sizeof( settings ) / sizeof( settings[ 0 ] ) ];
	for ( unsigned int i = 0; i < sizeof( settings ) / sizeof( settings[ 0 ] ); ++i ) {
		previous[ i ] = settings[ i ]->GetString();
	}
	const int previousFrame = backEnd.frameCount;
	r_skipPostProcess.SetBool( false );
	r_hdrSceneTarget.SetBool( true );
	r_hdrToneMap.SetBool( true );
	r_hdrAutoExposure.SetBool( true );
	r_hdrKeyValue.SetFloat( 0.18f );
	r_hdrMinExposure.SetFloat( 0.01f );
	r_hdrMaxExposure.SetFloat( 8.0f );
	viewDef_t view = {};
	viewEntity_t entity = {};
	view.viewEntitys = &entity;
	view.renderView.viewaxis.Identity();
	view.renderView.fov_x = 90.0f;
	view.renderView.fov_y = 60.0f;
	const float colors[ 3 ][ 4 ] = { { 4.0f, 4.0f, 4.0f, 1.0f },
		{ 8.0f, 2.0f, 0.5f, 1.0f }, { 0.03125f, 0.03125f, 0.03125f, 1.0f } };
	bool passed = vkCtx.initialized && VK_Post_EnsureModules();
	int cases = 0;
	for ( int asynchronous = 0; asynchronous < 2 && passed; ++asynchronous ) {
		for ( int msaa = 0; msaa < 2 && passed; ++msaa ) {
			for ( int size = 0; size < 2 && passed; ++size ) {
				for ( int color = 0; color < 3 && passed; ++color ) {
					passed = VK_Post_TestHDRCase( size == 0 ? 8 : 17, size == 0 ? 8 : 9,
						msaa == 0 ? 0 : 4, asynchronous != 0, colors[ color ], view );
					++cases;
				}
			}
		}
	}
	// Completed stale generations, duplicate frames and disabled samples must
	// never seed adaptation after a discontinuity.
	const unsigned int oldGeneration = vkHDR.generation;
	VK_Post_ResetHDRExposure();
	VK_PostProcess_ConsumeHDRSample( oldGeneration, backEnd.frameCount + 1, 0.0f );
	passed = passed && !vkHDR.haveSample;
	VK_PostProcess_ConsumeHDRSample( vkHDR.generation, backEnd.frameCount + 1, 1.0f );
	VK_PostProcess_ConsumeHDRSample( vkHDR.generation, backEnd.frameCount + 1, 2.0f );
	passed = passed && vkHDR.haveSample && vkHDR.logLuminance == 1.0f;
	vkHDR.enabled = false;
	VK_Post_ResetHDRExposure();
	VK_PostProcess_ConsumeHDRSample( vkHDR.generation, backEnd.frameCount + 2, 0.0f );
	passed = passed && !vkHDR.haveSample;
	passed = passed && VK_Post_TestToneMapping( view );
	passed = passed && VK_Post_TestLinearToneMapping( view );
	passed = passed && VK_Post_TestPreparedHDR( view );
	passed = passed && VK_HDRScene_Test();
	for ( unsigned int i = 0; i < sizeof( settings ) / sizeof( settings[ 0 ] ); ++i ) {
		settings[ i ]->SetString( previous[ i ] );
	}
	backEnd.frameCount = previousFrame;
	vkHDR.cameraValid = false;
	VK_Post_ResetHDRExposure();
	if ( passed && cases == 24 ) {
		common->Printf( "Vulkan HDR self-test passed (24 FP16/MSAA luminance fixtures, synchronous/asynchronous exposure, resize and stale-sample rejection)\n" );
	} else {
		common->Warning( "Vulkan HDR self-test failed after %d fixtures", cases );
	}
}

// Builds the bloom pyramid; each level ends blurred in its P0 image. Leaves
// the last level's target bound; the caller restores the scene target.
static bool VK_Post_BuildBloomPyramid( const viewDef_t *viewDef, idImage *scene, int levelCount,
		vkPostPreparedSteps_t *plan = NULL ) {
	const int viewWidth = VK_Post_ViewWidth( viewDef );
	const int viewHeight = VK_Post_ViewHeight( viewDef );
	const float bloomRadius = Max( r_bloomRadius.GetFloat(), 0.1f );
	for ( int level = 0; level < levelCount; level++ ) {
		int width, height;
		VK_Post_BloomLevelSize( level, viewWidth, viewHeight, width, height );
		for ( int pingPong = 0; pingPong < 2; pingPong++ ) {
			// Not va(): its shared ring is not thread safe and this can run on the render thread.
			char targetName[32];
			idStr::snPrintf( targetName, sizeof( targetName ), "_vkBloomL%dP%d", level, pingPong );
			if ( !VK_Post_EnsureColorTarget( vkPostScene.bloomImages[ level ][ pingPong ],
					vkPostScene.bloomTargets[ level ][ pingPong ],
					targetName, width, height, TF_LINEAR, "Vulkan bloom level" ) ) {
				return false;
			}
		}
		idRenderTexture *p0 = vkPostScene.bloomTargets[ level ][ 0 ];
		idRenderTexture *p1 = vkPostScene.bloomTargets[ level ][ 1 ];
		if ( level == 0 ) {
			const float block[ 4 ] = {
				1.0f / (float)viewWidth, 1.0f / (float)viewHeight,
				r_bloomThreshold.GetFloat(), r_bloomSoftKnee.GetFloat()
			};
			if ( !VK_Post_DrawBloomStep( p0, VK_POST_BLOOM_EXTRACT, VK_POST_MODULE_BLOOM_EXTRACT,
					scene, block, sizeof( block ), plan ) ) {
				return false;
			}
		} else {
			int previousWidth, previousHeight;
			VK_Post_BloomLevelSize( level - 1, viewWidth, viewHeight, previousWidth, previousHeight );
			const float block[ 4 ] = { 1.0f / (float)previousWidth, 1.0f / (float)previousHeight, 0.0f, 0.0f };
			if ( !VK_Post_DrawBloomStep( p0, VK_POST_BLOOM_DOWNSAMPLE, VK_POST_MODULE_BLOOM_DOWNSAMPLE,
					vkPostScene.bloomImages[ level - 1 ][ 0 ], block, sizeof( block ), plan ) ) {
				return false;
			}
		}
		vkPostBloomBlurBlock_t blur;
		memset( &blur, 0, sizeof( blur ) );
		blur.params[ 0 ] = 1.0f / (float)width;
		blur.params[ 1 ] = 1.0f / (float)height;
		blur.radius[ 0 ] = bloomRadius * ( 1.0f + 0.65f * (float)level );
		blur.params[ 2 ] = 1.0f;
		blur.params[ 3 ] = 0.0f;
		if ( !VK_Post_DrawBloomStep( p1, VK_POST_BLOOM_BLUR, VK_POST_MODULE_BLOOM_BLUR,
				vkPostScene.bloomImages[ level ][ 0 ], &blur, sizeof( blur ), plan ) ) {
			return false;
		}
		blur.params[ 2 ] = 0.0f;
		blur.params[ 3 ] = 1.0f;
		if ( !VK_Post_DrawBloomStep( p0, VK_POST_BLOOM_BLUR, VK_POST_MODULE_BLOOM_BLUR,
				vkPostScene.bloomImages[ level ][ 1 ], &blur, sizeof( blur ), plan ) ) {
			return false;
		}
	}
	return true;
}

static void VK_Post_BloomCompositeBlock( vkPostBloomCompositeBlock_t &block, bool bloomRequested,
		bool bloomEnabled, int levelCount, float adaptedExposure, bool linearScene ) {
	memset( &block, 0, sizeof( block ) );
	block.bloom[0] = bloomRequested ? r_bloomIntensity.GetFloat() : 0.0f;
	block.bloom[1] = bloomEnabled ? 1.0f : 0.0f;
	block.bloom[2] = r_hdrToneMap.GetBool() ? 1.0f : 0.0f;
	block.bloom[3] = float( idMath::ClampInt( 0, 2, r_hdrDebugView.GetInteger() ) );
	block.exposure[0] = r_hdrExposure.GetFloat() * adaptedExposure;
	block.exposure[1] = r_hdrWhitePoint.GetFloat();
	block.exposure[2] = r_hdrLift.GetFloat();
	block.exposure[3] = r_hdrPostGamma.GetFloat();
	block.grade[0] = r_hdrGain.GetFloat();
	block.grade[1] = r_hdrVibrance.GetFloat();
	block.grade[2] = r_hdrSaturation.GetFloat();
	block.grade[3] = r_hdrContrast.GetFloat();
	block.highlight[0] = r_hdrHighlightDesaturation.GetFloat();
	block.highlight[1] = r_hdrGamutCompression.GetFloat();
	if ( bloomEnabled ) {
		float total = 0.0f;
		for ( int i = 0; i < levelCount; ++i ) { total += VK_POST_BLOOM_BASE_WEIGHTS[i]; }
		if ( total <= 0.0f ) { total = 1.0f; }
		for ( int i = 0; i < levelCount; ++i ) {
			( i < 4 ? block.weights[i] : block.weights2[0] ) = VK_POST_BLOOM_BASE_WEIGHTS[i] / total;
		}
	}
	block.weights2[1] = linearScene ? 1.0f : 0.0f;
}

// A complete linear scene must have a usable output before it takes ownership.
// Seal the exposure/bloom chains and final output, including capture/resolve
// storage, readback buffer, descriptors and every uniform slice. The private
// output slice is filled with the adapted exposure only after metering, before
// its first draw; synchronous metering can submit without consuming that slice.
static struct vkLinearOutputPlan_t {
	const viewDef_t *view;
	int frame;
	idRenderTexture *target;
	VkPipeline pipeline;
	VkDescriptorSet sets[1 + VK_POST_BLOOM_MAX_LEVELS];
	int uniformOffset;
	vkPostBloomCompositeBlock_t block;
	vkPostPreparedSteps_t exposureSteps;
	vkPostPreparedSteps_t bloomSteps;
	idImage *luminance;
	int uniformCheckpoint;
	bool consumed;
} vkLinearOutputPlan;

void VK_PostProcess_DiscardLinearOutput() {
	if ( !vkLinearOutputPlan.consumed && vkLinearOutputPlan.uniformCheckpoint >= 0
			&& vkLinearOutputPlan.frame == backEnd.frameCount ) {
		VK_Exec_InteractionUniformRestore( vkLinearOutputPlan.uniformCheckpoint );
	}
	vkLinearOutputPlan.uniformCheckpoint = -1;
	vkLinearOutputPlan.view = NULL;
}

const char *VK_PostProcess_PrepareLinearOutput( const viewDef_t *view ) {
	memset( &vkLinearOutputPlan, 0, sizeof( vkLinearOutputPlan ) );
	vkLinearOutputPlan.uniformCheckpoint = -1;
	if ( !VK_Post_IsMainSceneView( view ) || !VK_Post_BloomPassRequested( view ) ) { return "post-view"; }
	if ( VK_Post_SSAORequested( view )
			|| r_motionBlur.GetBool() || VK_Post_CelInkRequested( view ) ) { return "post-combination"; }
	if ( !VK_Post_EnsureModules() ) { return "output-modules"; }
	idRenderTexture *target = VK_Exec_ActiveRenderTexture();
	if ( target == NULL || target->GetNumColorImages() != 1 || target->GetDepthImage() == NULL
			|| target->GetColorImage( 0 )->GetOpts().format != FMT_RGBA16F ) { return "scene-target"; }
	if ( vkPortalSkyOwner == view ) { return "portal-sky"; }
	idImage *scene = VK_Post_CaptureLinearScene( view );
	if ( scene == NULL ) { return "output-capture"; }
	// The capture above records real commands. Only the subsequently reserved,
	// never-drawn post slices may be reclaimed if any preparation stage fails.
	vkLinearOutputPlan.uniformCheckpoint = VK_Exec_InteractionUniformCheckpoint();
	vkLinearOutputPlan.frame = backEnd.frameCount;
	const bool bloom = VK_Post_BloomRequested();
	const int levelCount = bloom ? idMath::ClampInt( 1, VK_POST_BLOOM_MAX_LEVELS, r_bloomMipCount.GetInteger() ) : 0;
	const char *failure = NULL;
	if ( r_hdrAutoExposure.GetBool() ) {
		vkLinearOutputPlan.luminance = VK_Post_BuildHDRPyramid( view, scene, &vkLinearOutputPlan.exposureSteps );
		if ( vkLinearOutputPlan.luminance == NULL || !VK_Exec_PrepareHDRExposureReadback()
				|| r_vkHDRPrepareFailure.GetInteger() == 1 ) { failure = "exposure-resources"; }
	}
	if ( failure == NULL && bloom ) {
		if ( !VK_Post_BuildBloomPyramid( view, scene, levelCount, &vkLinearOutputPlan.bloomSteps )
				|| r_vkHDRPrepareFailure.GetInteger() == 2 ) { failure = "bloom-resources"; }
	}
	if ( !VK_Exec_SetRenderTarget( target ) ) { failure = "output-target"; }
	if ( failure != NULL ) { VK_PostProcess_DiscardLinearOutput(); return failure; }
	VK_Post_BloomCompositeBlock( vkLinearOutputPlan.block, bloom, bloom, levelCount, 1.0f, true );
	vkLinearOutputPlan.uniformOffset = VK_Exec_InteractionUniformAlloc( &vkLinearOutputPlan.block, sizeof( vkLinearOutputPlan.block ) );
	vkLinearOutputPlan.sets[0] = VK_Post_Descriptor( scene );
	for ( int i = 1; i <= VK_POST_BLOOM_MAX_LEVELS; ++i ) {
		vkLinearOutputPlan.sets[i] = VK_Post_Descriptor( i <= levelCount
			? vkPostScene.bloomImages[i - 1][0] : globalImages->blackImage );
	}
	vkLinearOutputPlan.pipeline = VK_Exec_PostPipeline( VK_POST_BLOOM_COMPOSITE, vkPost.fullscreenVert,
		VK_Post_SceneModule( VK_POST_MODULE_BLOOM_COMPOSITE ), GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	if ( !VK_Post_SetsReady( vkLinearOutputPlan.pipeline, vkLinearOutputPlan.sets,
			1 + VK_POST_BLOOM_MAX_LEVELS, vkLinearOutputPlan.uniformOffset )
			|| r_vkHDRPrepareFailure.GetInteger() == 3 ) {
		VK_PostProcess_DiscardLinearOutput();
		return "output-resources";
	}
	vkLinearOutputPlan.target = target;
	vkLinearOutputPlan.view = view;
	vkLinearOutputPlan.frame = backEnd.frameCount;
	return NULL;
}

static bool VK_Post_DrawPreparedLinearOutput( const viewDef_t *view, idRenderTexture *target ) {
	if ( vkLinearOutputPlan.view != view || vkLinearOutputPlan.frame != backEnd.frameCount
			|| vkLinearOutputPlan.target != target ) { return false; }
	vkLinearOutputPlan.consumed = true;
	idImage *scene = VK_Post_CaptureLinearScene( view );
	if ( scene == NULL ) { return false; }
	const float exposure = VK_Post_UpdateHDRAutoExposure( view, scene,
		&vkLinearOutputPlan.exposureSteps, vkLinearOutputPlan.luminance );
	const bool bloomReady = VK_Post_ExecuteSteps( vkLinearOutputPlan.bloomSteps );
	if ( !VK_Exec_SetRenderTarget( target ) || !bloomReady ) { return false; }
	vkLinearOutputPlan.block.exposure[0] = r_hdrExposure.GetFloat() * exposure;
	const bool submitted = VK_Exec_UpdateInteractionUniform( vkLinearOutputPlan.uniformOffset,
			&vkLinearOutputPlan.block, sizeof( vkLinearOutputPlan.block ) )
		&& VK_Post_DrawSceneRect( view, vkLinearOutputPlan.pipeline, vkLinearOutputPlan.sets,
			1 + VK_POST_BLOOM_MAX_LEVELS, vkLinearOutputPlan.uniformOffset );
	if ( submitted && VK_HDRScene_LinearActive() ) {
		const vkImageEntry_t *entry = VK_Image_GetEntry( scene->GetDeviceHandle() );
		vkLinearCapture.valid = entry != NULL;
		vkLinearCapture.handle = scene->GetDeviceHandle();
		vkLinearCapture.generation = entry != NULL ? entry->generation : 0;
		vkLinearCapture.frame = backEnd.frameCount;
		vkLinearCapture.videoRestart = tr.GetVideoRestartCount();
	}
	return submitted;
}

static bool VK_Post_DrawBloom( const viewDef_t *viewDef, idRenderTexture *sceneTarget,
		bool linearScene ) {
	vkGpuPhaseScope timing(VK_Exec_ActiveCmd(),VK_GPU_PHASE_BLOOM);
	if ( linearScene && VK_HDRScene_LinearActive() ) {
		return VK_Post_DrawPreparedLinearOutput( viewDef, sceneTarget );
	}
	const int sceneCubeFace = VK_Exec_ActiveCubeFace();
	const VkShaderModule fragModule = VK_Post_SceneModule( VK_POST_MODULE_BLOOM_COMPOSITE );
	if ( fragModule == VK_NULL_HANDLE ) {
		return false;
	}
	idImage *scene = VK_Post_CaptureScene( viewDef );
	if ( scene == NULL ) {
		return false;
	}
	const bool bloomRequested = VK_Post_BloomRequested();
	const float adaptedExposure = VK_Post_UpdateHDRAutoExposure( viewDef, scene );
	const int levelCount = idMath::ClampInt( 1, VK_POST_BLOOM_MAX_LEVELS, r_bloomMipCount.GetInteger() );
	idImage *bloomImages[ VK_POST_BLOOM_MAX_LEVELS ];
	for ( int i = 0; i < VK_POST_BLOOM_MAX_LEVELS; i++ ) {
		bloomImages[ i ] = globalImages->blackImage;
	}
	bool bloomEnabled = false;
	if ( bloomRequested ) {
		bloomEnabled = VK_Post_BuildBloomPyramid( viewDef, scene, levelCount );
		if ( bloomEnabled ) {
			for ( int level = 0; level < levelCount; level++ ) {
				bloomImages[ level ] = vkPostScene.bloomImages[ level ][ 0 ];
			}
		}
		if ( !VK_Exec_SetRenderTarget( sceneTarget, sceneCubeFace ) ) {
			return false;
		}
	}

	vkPostBloomCompositeBlock_t block;
	VK_Post_BloomCompositeBlock( block, bloomRequested, bloomEnabled, levelCount, adaptedExposure, linearScene );

	VkDescriptorSet sets[ 1 + VK_POST_BLOOM_MAX_LEVELS ];
	sets[ 0 ] = VK_Post_Descriptor( scene );
	for ( int i = 0; i < VK_POST_BLOOM_MAX_LEVELS; i++ ) {
		sets[ 1 + i ] = VK_Post_Descriptor( bloomImages[ i ] );
	}
	const int uniformOffset = VK_Exec_InteractionUniformAlloc( &block, sizeof( block ) );
	const VkPipeline pipeline = VK_Exec_PostPipeline( VK_POST_BLOOM_COMPOSITE, vkPost.fullscreenVert,
			fragModule, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	// The portal sky is an already composed backdrop. OpenGL preserves its
	// far-depth pixels at scene presentation; do the equivalent here before
	// authored post effects or game-owned presentation can consume this target.
	bool preserveSky = false;
	if ( sceneTarget != NULL && sceneTarget->GetDepthImage() != NULL ) {
		preserveSky = vkPortalSkyOwner == viewDef;
		for ( int i = 0; i < viewDef->numDrawSurfs && !preserveSky; ++i ) {
			const drawSurf_t *surf = viewDef->drawSurfs[i];
			preserveSky = surf != NULL && surf->material != NULL
				&& ( surf->material->IsPortalSky() || surf->material->GetSort() == SS_PORTAL_SKY );
		}
	}
	vkHDR.skyPreserved = preserveSky;
	return VK_Post_DrawSceneRect( viewDef, pipeline, sets, 1 + VK_POST_BLOOM_MAX_LEVELS, uniformOffset, preserveSky );
}

// ---- cel world ink (RB_STD_CelWorldOutline) ----

// std140 layout of CelOutlineBlock in post_celoutline.frag
typedef struct vkPostCelOutlineBlock_s {
	float	texInfo[ 4 ];
	float	projection[ 4 ];
	float	depthInfo[ 4 ];
	float	celEdgeParams[ 4 ];
	float	celOutlineColor[ 4 ];
} vkPostCelOutlineBlock_t;

static bool VK_Post_TestToneMapping( viewDef_t &view ) {
	idCVar *settings[] = { &r_bloom, &r_hdrAutoExposure, &r_hdrExposure, &r_hdrWhitePoint,
		&r_hdrLift, &r_hdrPostGamma, &r_hdrGain, &r_hdrVibrance, &r_hdrSaturation,
		&r_hdrContrast, &r_hdrHighlightDesaturation, &r_hdrGamutCompression, &r_hdrDebugView };
	const float controls[] = { 0, 0, 1, 6, 0, 1, 1, 0, 1, 1, 0, 0, 0 };
	idStr previous[ sizeof( settings ) / sizeof( settings[0] ) ];
	for ( unsigned int i = 0; i < sizeof( settings ) / sizeof( settings[0] ); ++i ) {
		previous[i] = settings[i]->GetString();
		settings[i]->SetFloat( controls[i] );
	}
	idImageOpts opts;
	opts.textureType = TT_2D;
	opts.format = FMT_RGBA16F;
	opts.width = opts.height = 8;
	opts.numLevels = 1;
	opts.numMSAASamples = 0;
	opts.isPersistant = true;
	idImage *image = globalImages->ScratchImage( "_vkHDRToneMapTest", &opts, TF_NEAREST, TR_CLAMP, TD_DEFAULT );
	bool passed = image != NULL;
	int cases = 0;
	if ( passed ) {
		image->AllocImage( opts, TF_NEAREST, TR_CLAMP );
		idRenderTexture target( image, NULL );
		view.viewport.x1 = view.viewport.y1 = view.scissor.x1 = view.scissor.y1 = 0;
		view.viewport.x2 = view.viewport.y2 = view.scissor.x2 = view.scissor.y2 = 7;
		const float colors[][4] = {
			{0,0,0,1}, {.18f,.18f,.18f,1}, {.5f,.5f,.5f,1}, {.75f,.75f,.75f,1},
			{1,1,1,1}, {2,2,2,1}, {4,4,4,1}, {6,6,6,1}, {8,8,8,1}, {64,64,64,1},
			{.25f,.5f,.75f,1}, {1,.7f,.35f,1}, {4,1,.5f,1}, {-1,.18f,2,1}
		};
		const float exposures[] = { .125f, 1, 3, 8 };
		for ( int white = 0; white < 2 && passed; ++white ) {
			const float whitePoint = white == 0 ? 1.0f : 6.0f;
			r_hdrWhitePoint.SetFloat( whitePoint );
			for ( int e = 0; e < 4 && passed; ++e ) {
				r_hdrExposure.SetFloat( exposures[e] );
				for ( int c = 0; c < 14 && passed; ++c ) {
					++backEnd.frameCount;
					passed = VK_GuiExecutor_BeginFrame() && target.MakeCurrent();
					if ( passed ) {
						VK_Exec_ClearRenderTarget( true, false, 1, colors[c] );
						passed = VK_Post_DrawBloom( &view, &target, false );
					}
					idList<float> pixels;
					passed = passed && VK_Exec_TestReadFloatImage( image, pixels ) && pixels.Num() == 8 * 8 * 4;
					for ( int i = 0; i < pixels.Num() && passed; ++i ) {
						const int channel = i % 4;
						const double x = Max( 0.0, double( colors[c][channel] ) * exposures[e] );
						const double range = Max( 1.0, double( whitePoint ) * exposures[e] ) - .5;
						const double t = Max( 0.0, x - .5 );
						// Independent rational form: identity slope at the knee,
						// exact reference white and unchanged lower half-range.
						const double expected = channel == 3 ? 1.0 : Min( 1.0,
							x <= .5 ? x : .5 + t * range / ( range + t * ( 2 * range - 1 ) ) );
						passed = std::isfinite( pixels[i] ) && fabs( pixels[i] - expected ) < .002;
						if ( !passed ) {
							common->Warning( "Vulkan HDR tone-map fixture white=%g exposure=%g color=%d channel=%d got=%g expected=%g",
								whitePoint, exposures[e], c, channel, pixels[i], expected );
						}
					}
					++cases;
				}
			}
		}
		(void)VK_Exec_SetRenderTarget( NULL );
		image->PurgeImage();
	}
	// The right half is portal-sky depth, the left half foreground. Both
	// contain identical radiance; only foreground receives exposure/tone map.
	int skyCases = 0;
	const idMaterial *skyMaterial = declManager->FindMaterial( "_default" );
	const float originalSort = skyMaterial->GetSort();
	skyMaterial->SetSort( SS_PORTAL_SKY );
	drawSurf_t skySurf = {};
	skySurf.material = skyMaterial;
	drawSurf_t *skySurfs[] = { &skySurf };
	view.drawSurfs = skySurfs;
	view.numDrawSurfs = 1;
	r_hdrExposure.SetFloat( 8 );
	r_hdrWhitePoint.SetFloat( 6 );
	for ( int fixture = 0; fixture < 20 && passed; ++fixture ) {
		const int mode = fixture % 5;
		const bool linearScene = fixture >= 10;
		opts.numMSAASamples = fixture % 10 < 5 ? 0 : 4;
		opts.format = FMT_RGBA16F;
		image->AllocImage( opts, TF_NEAREST, TR_CLAMP );
		opts.format = FMT_DEPTH;
		idImage *depth = globalImages->ScratchImage( "_vkHDRSkyDepthTest", &opts, TF_NEAREST, TR_CLAMP, TD_DEFAULT );
		passed = depth != NULL;
		if ( passed ) {
			depth->AllocImage( opts, TF_NEAREST, TR_CLAMP );
			idRenderTexture target( image, depth );
			++backEnd.frameCount;
			passed = VK_GuiExecutor_BeginFrame() && target.MakeCurrent();
			if ( passed ) {
				view.numDrawSurfs = mode == 0 ? 1 : 0;
				if ( mode != 0 ) {
					viewDef_t portal = view;
					portal.renderFlags |= RF_PORTAL_SKY;
					if ( mode == 3 ) { ++portal.viewport.x2; }
					VK_PostProcess_BeginView( &portal );
					if ( mode == 2 ) { ++backEnd.frameCount; }
					if ( mode == 4 ) { (void)VK_Exec_SetRenderTarget( NULL ); }
				}
				VK_PostProcess_BeginView( &view );
				passed = VK_Exec_SetRenderTarget( &target );
				const float color[] = { .5f, .5f, .5f, 1 };
				VK_Exec_ClearRenderTarget( true, true, 1, color );
				VkClearAttachment clear = {};
				clear.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
				clear.clearValue.depthStencil.depth = .5f;
				VkClearRect rect = {};
				rect.rect.extent.width = 4;
				rect.rect.extent.height = 8;
				rect.layerCount = 1;
				vkCmdClearAttachments( VK_Exec_ActiveCmd(), 1, &clear, 1, &rect );
				passed = VK_Post_DrawBloom( &view, &target, linearScene );
			}
			idList<float> pixels;
			idImage *resolved = passed ? VK_Post_CaptureScene( &view ) : NULL;
			passed = passed && VK_Exec_TestReadFloatImage( resolved, pixels ) && pixels.Num() == 8 * 8 * 4;
			for ( int i = 0; i < pixels.Num() && passed; ++i ) {
				const bool foreground = mode >= 2 || ( i / 4 ) % 8 < 4;
				// Linear filmic output for radiance .5 at exposure 8/white 6
				// is 0.99119336; the stock shoulder is 0.94156707.
				const double expected = i % 4 == 3 ? 1.0 : !foreground ? .5
					: linearScene ? .99119336 : .94156707;
				passed = std::isfinite( pixels[i] ) && fabs( pixels[i] - expected ) < .002;
			}
			(void)VK_Exec_SetRenderTarget( NULL );
			depth->PurgeImage();
		}
		image->PurgeImage();
		++skyCases;
	}
	skyMaterial->SetSort( originalSort );
	view.drawSurfs = NULL;
	view.numDrawSurfs = 0;
	for ( unsigned int i = 0; i < sizeof( settings ) / sizeof( settings[0] ); ++i ) {
		settings[i]->SetString( previous[i] );
	}
	common->Printf( "Vulkan HDR tone-map self-test %s (%d production shader fixtures, %d portal-sky masks)\n",
		passed && cases == 112 && skyCases == 20 ? "passed" : "FAILED", cases, skyCases );
	return passed && cases == 112 && skyCases == 20;
}

// Double-precision reference for the output contract. This is deliberately
// independent of the GLSL arithmetic: integer-coefficient rational film and
// scalar piecewise display transfer, evaluated from the actual CVar inputs.
static void VK_Post_TestHDRExpected( const float *source, bool linearScene, double *expected ) {
	double color[3];
	for ( int c = 0; c < 3; ++c ) {
		color[c] = Max( 0.0, double( source[c] ) );
	}
	const int debugView = r_hdrDebugView.GetInteger();
	if ( debugView != 0 ) {
		const double peak = Max( color[0], Max( color[1], color[2] ) );
		const double logPeak = log( Max( peak, .0001 ) ) / log( 2.0 );
		if ( debugView == 2 ) {
			for ( int c = 0; c < 3; ++c ) {
				color[c] = Min( 1.0, Max( 0.0, ( logPeak + 10.0 ) / 10.0 ) );
			}
		} else {
			const double stops[5][3] = { {.02,.05,.16}, {0,.55,.95}, {.18,.84,.18}, {.98,.78,.08}, {.95,.14,.05} };
			const double position = Min( 4.0, Max( 0.0, ( logPeak + 8.0 ) / 2.0 ) );
			const int lower = Min( 3, int( position ) );
			for ( int c = 0; c < 3; ++c ) {
				color[c] = stops[lower][c] + ( stops[lower+1][c] - stops[lower][c] ) * ( position - lower );
			}
		}
	} else {
		// A spatially constant input with zero threshold is unchanged by the
		// normalized bloom pyramid, so its exact contribution is intensity*C.
		if ( r_bloom.GetBool() ) {
			for ( int c = 0; c < 3; ++c ) {
				color[c] *= 1.0 + double( r_bloomIntensity.GetFloat() );
			}
		}
		if ( r_hdrToneMap.GetBool() ) {
			const double exposure = Max( .001, double( r_hdrExposure.GetFloat() ) );
			const double white = Max( 1.0, double( r_hdrWhitePoint.GetFloat() ) );
			for ( int c = 0; c < 3; ++c ) {
				const double x = color[c] * exposure;
				if ( linearScene ) {
					color[c] = ( 251*x*x + 3*x ) * ( 243*white*white + 59*white + 14 )
						/ ( ( 243*x*x + 59*x + 14 ) * ( 251*white*white + 3*white ) );
				} else {
					const double range = Max( 1.0, white * exposure ) - .5;
					const double t = Max( 0.0, x - .5 );
					color[c] = x <= .5 ? x : .5 + t * range / ( range + t * ( 2*range - 1 ) );
				}
			}
			double luma = .2126*color[0] + .7152*color[1] + .0722*color[2];
			const double h = Min( 1.0, Max( 0.0, ( Max( color[0], Max( color[1], color[2] ) ) - .98 ) / .02 ) );
			const double desaturate = Min( 1.0, Max( 0.0, h*h*(3-2*h) * r_hdrHighlightDesaturation.GetFloat() ) );
			for ( int c = 0; c < 3; ++c ) {
				color[c] += ( luma - color[c] ) * desaturate;
			}
			const double peak = Max( color[0], Max( color[1], color[2] ) );
			const double gamut = r_hdrGamutCompression.GetFloat();
			const double scale = peak > 1 && gamut > 0 ? ( 1 + ( peak - 1 ) / ( 1 + gamut*( peak - 1 ) ) ) / peak : 1;
			for ( int c = 0; c < 3; ++c ) {
				color[c] = Min( 1.0, Max( 0.0, color[c] * scale ) );
				if ( linearScene ) {
					color[c] = color[c] < .0031308 ? 12.92*color[c] : 1.055*pow( color[c], 1.0/2.4 ) - .055;
				}
				color[c] = pow( Max( 0.0, color[c] + r_hdrLift.GetFloat() ),
					1.0 / Max( .001, double( r_hdrPostGamma.GetFloat() ) ) ) * r_hdrGain.GetFloat();
			}
			luma = .2126*color[0] + .7152*color[1] + .0722*color[2];
			const double saturation = Max( color[0], Max( color[1], color[2] ) ) - Min( color[0], Min( color[1], color[2] ) );
			const double vibrance = Min( 2.0, Max( 0.0, 1 + r_hdrVibrance.GetFloat() * ( 1 - saturation ) ) );
			for ( int c = 0; c < 3; ++c ) {
				// Both vibrance and saturation preserve the same luma.
				color[c] = luma + ( color[c] - luma ) * vibrance * r_hdrSaturation.GetFloat();
				color[c] = Min( 1.0, Max( 0.0, ( color[c] - .5 ) * r_hdrContrast.GetFloat() + .5 ) );
			}
		}
	}
	for ( int c = 0; c < 3; ++c ) {
		expected[c] = color[c];
	}
	expected[3] = source[3];
}

static bool VK_Post_TestOutputFixture( viewDef_t &view, idRenderTexture &target,
		const float *color, bool linearScene, const char *group, int fixture ) {
	++backEnd.frameCount;
	bool passed = VK_GuiExecutor_BeginFrame() && target.MakeCurrent();
	if ( passed ) {
		VK_Exec_ClearRenderTarget( true, false, 1, color );
		passed = VK_Post_DrawBloom( &view, &target, linearScene );
	}
	idImage *resolved = passed ? VK_Post_CaptureScene( &view ) : NULL;
	idList<float> pixels;
	passed = passed && VK_Exec_TestReadFloatImage( resolved, pixels ) && pixels.Num() == 8 * 8 * 4;
	double expected[4];
	VK_Post_TestHDRExpected( color, linearScene, expected );
	for ( int i = 0; i < pixels.Num() && passed; ++i ) {
		const int channel = i % 4;
		passed = std::isfinite( pixels[i] ) && ( channel == 3
			? pixels[i] == expected[channel] : fabs( pixels[i] - expected[channel] ) < .003 );
		if ( !passed ) {
			common->Warning( "Vulkan HDR %s fixture=%d linear=%d samples=%d channel=%d got=%g expected=%g",
				group, fixture, int( linearScene ), target.GetColorImage( 0 )->GetOpts().numMSAASamples,
				channel, pixels[i], expected[channel] );
		}
	}
	return passed;
}

static bool VK_Post_TestLinearToneMapping( viewDef_t &view ) {
	idCVar *settings[] = { &r_bloom, &r_bloomIntensity, &r_bloomThreshold, &r_bloomSoftKnee,
		&r_bloomMipCount, &r_bloomRadius, &r_hdrAutoExposure, &r_hdrExposure, &r_hdrWhitePoint,
		&r_hdrLift, &r_hdrPostGamma, &r_hdrGain, &r_hdrVibrance, &r_hdrSaturation,
		&r_hdrContrast, &r_hdrHighlightDesaturation, &r_hdrGamutCompression, &r_hdrDebugView, &r_hdrToneMap };
	const float controls[] = { 0, .5f, 0, 0, 5, 1, 0, 1, 6, 0, 1, 1, 0, 1, 1, 0, 0, 0, 1 };
	idStr previous[ sizeof( settings ) / sizeof( settings[0] ) ];
	for ( unsigned int i = 0; i < sizeof( settings ) / sizeof( settings[0] ); ++i ) {
		previous[i] = settings[i]->GetString();
		settings[i]->SetFloat( controls[i] );
	}
	idImageOpts opts;
	opts.textureType = TT_2D;
	opts.format = FMT_RGBA16F;
	opts.width = opts.height = 8;
	opts.numLevels = 1;
	opts.numMSAASamples = 0;
	opts.isPersistant = true;
	idImage *image = globalImages->ScratchImage( "_vkHDRLinearOutputTest", &opts, TF_NEAREST, TR_CLAMP, TD_DEFAULT );
	view.viewport.x1 = view.viewport.y1 = view.scissor.x1 = view.scissor.y1 = 0;
	view.viewport.x2 = view.viewport.y2 = view.scissor.x2 = view.scissor.y2 = 7;
	const float colors[][4] = {
		{0,0,0,0}, {.18f,.18f,.18f,.125f}, {.5f,.5f,.5f,.4375f}, {.75f,.75f,.75f,1},
		{1,1,1,0}, {2,2,2,.125f}, {4,4,4,.4375f}, {6,6,6,1}, {8,8,8,0}, {64,64,64,.125f},
		{.25f,.5f,.75f,.4375f}, {1,.7f,.35f,1}, {4,1,.5f,0}, {-1,.18f,2,.125f},
		{65504,4096,1,.4375f}, {.00001f,.0001f,.001f,1}, {.0009765625f,.001953125f,.00390625f,0},
		{.015625f,.03125f,.0625f,.125f}, {1,0,0,.4375f}, {0,0,4,1}
	};
	const float exposures[] = { .125f, 1, 3, 8 };
	bool passed = image != NULL;
	int cases = 0;
	int compositionCases = 0;
	for ( int sampleCase = 0; sampleCase < 2 && passed; ++sampleCase ) {
		opts.numMSAASamples = sampleCase == 0 ? 0 : 4;
		image->AllocImage( opts, TF_NEAREST, TR_CLAMP );
		passed = image->GetOpts().numMSAASamples == opts.numMSAASamples;
		idRenderTexture target( image, NULL );
		for ( int white = 0; white < 2 && passed; ++white ) {
			r_hdrWhitePoint.SetFloat( white == 0 ? 1.0f : 6.0f );
			for ( int e = 0; e < 4 && passed; ++e ) {
				r_hdrExposure.SetFloat( exposures[e] );
				for ( int c = 0; c < 20 && passed; ++c ) {
					passed = VK_Post_TestOutputFixture( view, target, colors[c], true, "linear output", cases++ );
				}
			}
		}
		for ( int domain = 0; domain < 2 && passed; ++domain ) {
			for ( int variant = 0; variant < 8 && passed; ++variant ) {
				for ( unsigned int i = 0; i < sizeof( settings ) / sizeof( settings[0] ); ++i ) {
					settings[i]->SetFloat( controls[i] );
				}
				float color[] = { .25f, .0625f, .015625f, .4375f };
				r_bloom.SetBool( variant == 1 || variant == 2 || variant == 5 || variant == 6 );
				r_hdrToneMap.SetBool( variant != 1 && variant != 7 );
				if ( variant == 0 ) { r_hdrExposure.SetFloat( 0 ); }
				if ( variant == 3 ) {
					r_hdrLift.SetFloat( .02f ); r_hdrPostGamma.SetFloat( 1.6f ); r_hdrGain.SetFloat( .9f );
					r_hdrVibrance.SetFloat( .25f ); r_hdrSaturation.SetFloat( .85f ); r_hdrContrast.SetFloat( 1.2f );
				}
				if ( variant == 4 ) {
					color[0] = 4; color[1] = 1; color[2] = .5f;
					r_hdrHighlightDesaturation.SetFloat( .6f ); r_hdrGamutCompression.SetFloat( 1 );
				}
				r_hdrDebugView.SetInteger( variant == 5 ? 1 : variant == 6 ? 2 : 0 );
				passed = VK_Post_TestOutputFixture( view, target, color, domain != 0, "composition", compositionCases++ );
			}
		}
		(void)VK_Exec_SetRenderTarget( NULL );
		image->PurgeImage();
		for ( unsigned int i = 0; i < sizeof( settings ) / sizeof( settings[0] ); ++i ) {
			settings[i]->SetFloat( controls[i] );
		}
	}
	for ( unsigned int i = 0; i < sizeof( settings ) / sizeof( settings[0] ); ++i ) {
		settings[i]->SetString( previous[i] );
	}
	passed = passed && cases == 320 && compositionCases == 32;
	common->Printf( "Vulkan HDR linear-output self-test %s (%d linear fixtures, %d composition fixtures, actual 0x/4x MSAA)\n",
		passed ? "passed" : "FAILED", cases, compositionCases );
	return passed;
}

static bool VK_Post_TestPreparedHDR( viewDef_t &view ) {
	idCVar *settings[] = { &r_bloom, &r_bloomIntensity, &r_bloomThreshold, &r_bloomSoftKnee,
		&r_bloomMipCount, &r_bloomRadius, &r_hdrAutoExposure, &r_hdrAutoExposureAsync,
		&r_hdrExposure, &r_hdrWhitePoint, &r_hdrLift, &r_hdrPostGamma, &r_hdrGain,
		&r_hdrVibrance, &r_hdrSaturation, &r_hdrContrast, &r_hdrHighlightDesaturation,
		&r_hdrGamutCompression, &r_hdrDebugView, &r_hdrToneMap, &r_ssao, &r_motionBlur,
		&r_vkHDRPrepareFailure };
	const float controls[] = { 0, .5f, 0, 0, 5, 1, 0, 0, 1, 6, 0, 1, 1, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0 };
	static_assert( sizeof( settings ) / sizeof( settings[0] ) == sizeof( controls ) / sizeof( controls[0] ), "HDR test settings" );
	idStr previous[sizeof( settings ) / sizeof( settings[0] )];
	for ( unsigned int i = 0; i < sizeof( settings ) / sizeof( settings[0] ); ++i ) {
		previous[i] = settings[i]->GetString();
		settings[i]->SetFloat( controls[i] );
	}
	idImageOpts opts;
	opts.textureType = TT_2D;
	opts.format = FMT_RGBA16F;
	opts.width = opts.height = 8;
	opts.numLevels = 1;
	opts.numMSAASamples = 0;
	opts.isPersistant = true;
	idImage *image = globalImages->ScratchImage( "_vkHDRPreparedOutputTest", &opts, TF_NEAREST, TR_CLAMP, TD_DEFAULT );
	opts.format = FMT_DEPTH;
	idImage *depth = globalImages->ScratchImage( "_vkHDRPreparedOutputDepth", &opts, TF_NEAREST, TR_CLAMP, TD_DEFAULT );
	const float colors[2][4] = { {.25f,.5f,2.0f,.125f}, {4.0f,.125f,.5f,.4375f} };
	bool passed = image != NULL && depth != NULL;
	int cases = 0, rejected = 0, domains = 0;
	for ( int sample = 0; sample < 2 && passed; ++sample ) {
		for ( int size = 0; size < 2 && passed; ++size ) {
			opts.width = size == 0 ? 8 : 17;
			opts.height = size == 0 ? 8 : 9;
			opts.numMSAASamples = sample == 0 ? 0 : 4;
			opts.format = FMT_RGBA16F;
			image->AllocImage( opts, TF_NEAREST, TR_CLAMP );
			opts.format = FMT_DEPTH;
			depth->AllocImage( opts, TF_NEAREST, TR_CLAMP );
			passed = image->GetOpts().numMSAASamples == opts.numMSAASamples && depth->GetOpts().numMSAASamples == opts.numMSAASamples;
			idRenderTexture target( image, depth );
			view.viewport.x1 = view.viewport.y1 = view.scissor.x1 = view.scissor.y1 = 0;
			view.viewport.x2 = view.scissor.x2 = opts.width - 1;
			view.viewport.y2 = view.scissor.y2 = opts.height - 1;
			for ( int automatic = 0; automatic < 2 && passed; ++automatic ) {
				for ( int bloom = 0; bloom < 2 && passed; ++bloom ) {
					for ( int c = 0; c < 2 && passed; ++c ) {
						++backEnd.frameCount;
						view.floatTime = float( backEnd.frameCount ) * .016f;
						r_hdrExposure.SetFloat( 1.0f );
						r_hdrAutoExposure.SetBool( automatic != 0 );
						r_bloom.SetBool( bloom != 0 );
						r_bloomMipCount.SetInteger( c == 0 ? 1 : 5 );
						passed = VK_GuiExecutor_BeginFrame() && target.MakeCurrent();
						if ( passed ) {
							VK_Exec_ClearRenderTarget( true, true, 1, colors[c] );
							VK_PostProcess_BeginView( &view );
							VK_Post_SynchronizeHDRExposure( &view, true );
							VK_Post_ResetHDRExposure();
							passed = VK_PostProcess_PrepareLinearOutput( &view ) == NULL
								&& VK_Post_DrawPreparedLinearOutput( &view, &target );
						}
						idList<float> pixels;
						idImage *resolved = passed ? VK_Post_CaptureScene( &view ) : NULL;
						passed = passed && VK_Exec_TestReadFloatImage( resolved, pixels ) && pixels.Num() == opts.width * opts.height * 4;
						const double luminance = .2126*colors[c][0] + .7152*colors[c][1] + .0722*colors[c][2];
						const double expectedExposure = automatic ? Min( double( r_hdrMaxExposure.GetFloat() ),
							Max( double( r_hdrMinExposure.GetFloat() ), double( r_hdrKeyValue.GetFloat() ) / luminance ) ) : 1.0;
						// Use the analytic input luminance, not the measured exposure,
						// to independently predict the complete production-chain output.
						r_hdrExposure.SetFloat( float( expectedExposure ) );
						double expected[4];
						VK_Post_TestHDRExpected( colors[c], true, expected );
						r_hdrExposure.SetFloat( 1.0f );
						for ( int i = 0; i < pixels.Num() && passed; ++i ) {
							passed = std::isfinite( pixels[i] ) && ( i % 4 == 3 ? pixels[i] == expected[3]
								: fabs( pixels[i] - expected[i % 4] ) < .003 );
						}
						if ( automatic ) {
							passed = passed && vkHDR.adaptation.initialized
								&& fabs( vkHDR.adaptation.exposure - expectedExposure ) < expectedExposure * .015;
						}
						if ( !passed ) { common->Warning( "Vulkan HDR prepared fixture %d failed (samples=%d size=%d auto=%d bloom=%d color=%d)", cases, opts.numMSAASamples, size, automatic, bloom, c ); }
						++cases;
					}
				}
			}
			// Late preflight failures must restore the destination without writing
			// a partial pyramid, tone map or changed scene. Exercise every stage.
			if ( size == 1 ) {
				for ( int fault = 1; fault <= 3 && passed; ++fault ) {
					++backEnd.frameCount;
					r_vkHDRPrepareFailure.SetInteger( fault );
					passed = VK_GuiExecutor_BeginFrame() && target.MakeCurrent();
					if ( passed ) {
						VK_Exec_ClearRenderTarget( true, true, 1, colors[0] );
						const int checkpoint = VK_Exec_InteractionUniformCheckpoint();
						passed = VK_PostProcess_PrepareLinearOutput( &view ) != NULL && VK_Exec_ActiveRenderTexture() == &target
							&& VK_Exec_InteractionUniformCheckpoint() == checkpoint;
					}
					idList<float> pixels;
					idImage *resolved = passed ? VK_Post_CaptureScene( &view ) : NULL;
					passed = passed && VK_Exec_TestReadFloatImage( resolved, pixels ) && pixels.Num() == opts.width * opts.height * 4;
					for ( int i = 0; i < pixels.Num() && passed; ++i ) { passed = pixels[i] == colors[0][i % 4]; }
					++rejected;
				}
				r_vkHDRPrepareFailure.SetInteger( 0 );
				// A change of scene domain alone drops the samples metered in the
				// old domain, invalidates temporal history and resumes the new
				// domain's own adaptation when it has one. The size change above
				// forgot both, so the first change (to display-referred) restarts
				// and the change back resumes the linear fixtures' exposure.
				for ( int linear = 0; linear < 2 && passed; ++linear ) {
					++backEnd.frameCount;
					passed = VK_GuiExecutor_BeginFrame() && target.MakeCurrent();
					const unsigned int oldGeneration = vkHDR.generation;
					const unsigned int oldHistory = R_TemporalPresentation_HistoryGeneration();
					const bool resumes = vkHDRDomainExposureValid[ linear ];
					const float resumedExposure = vkHDRDomainExposure[ linear ].exposure;
					VK_Post_SynchronizeHDRExposure( &view, linear != 0 );
					VK_PostProcess_ConsumeHDRSample( oldGeneration, backEnd.frameCount, 4.0f );
					passed = passed && vkHDR.generation != oldGeneration && !vkHDR.haveSample
						&& R_TemporalPresentation_HistoryGeneration() != oldHistory
						&& resumes == ( linear != 0 ) && vkHDR.adaptation.initialized == resumes
						&& ( !resumes || vkHDR.adaptation.exposure == resumedExposure );
					VK_PostProcess_ConsumeHDRSample( vkHDR.generation, backEnd.frameCount, 0.0f );
					VK_Post_AdaptHDRExposure( view.floatTime );
					passed = passed && vkHDR.haveSample && vkHDR.adaptation.initialized && vkHDR.adaptation.averageLuminance == 1.0f;
					++domains;
				}
			}
			(void)VK_Exec_SetRenderTarget( NULL );
			image->PurgeImage();
			depth->PurgeImage();
		}
	}
	for ( unsigned int i = 0; i < sizeof( settings ) / sizeof( settings[0] ); ++i ) { settings[i]->SetString( previous[i] ); }
	passed = passed && cases == 32 && rejected == 6 && domains == 4;
	common->Printf( "Vulkan HDR prepared-post self-test %s (%d exposure/bloom fixtures, %d unchanged-scene rejections, %d domain resets, actual 0x/4x MSAA)\n",
		passed ? "passed" : "FAILED", cases, rejected, domains );
	return passed;
}

static bool VK_Post_DrawCelInk( const viewDef_t *viewDef ) {
	const int width = VK_Post_ViewWidth( viewDef );
	const int height = VK_Post_ViewHeight( viewDef );
	if ( vkPostScene.celWorldDepthFrame != backEnd.frameCount || vkPostScene.celWorldDepth == NULL
			|| vkPostScene.celWorldDepthWidth != width || vkPostScene.celWorldDepthHeight != height ) {
		if ( r_celShadingWorldDebug.GetBool() ) {
			common->Printf( "cel world outline skipped: no world depth snapshot for this view\n" );
		}
		return false;
	}
	const VkShaderModule fragModule = VK_Post_SceneModule( VK_POST_MODULE_CEL_OUTLINE );
	if ( fragModule == VK_NULL_HANDLE || !VK_Post_ProjectionUsable( viewDef ) ) {
		return false;
	}
	idImage *sceneDepth = VK_Post_FinalDepth( viewDef );
	idImage *scene = VK_Post_CaptureScene( viewDef );
	if ( sceneDepth == NULL || scene == NULL ) {
		return false;
	}

	vkPostCelOutlineBlock_t block;
	memset( &block, 0, sizeof( block ) );
	block.texInfo[ 0 ] = 1.0f / (float)width;
	block.texInfo[ 1 ] = 1.0f / (float)height;
	VK_Post_ProjectionInfo( viewDef, block.projection );
	block.depthInfo[ 0 ] = viewDef->projectionMatrix[ 10 ];
	block.depthInfo[ 1 ] = viewDef->projectionMatrix[ 14 ];
	block.celEdgeParams[ 0 ] = R_CelWorldOutlineWidth();
	block.celEdgeParams[ 1 ] = R_CelWorldOutlineDepthThreshold();
	block.celEdgeParams[ 2 ] = R_CelWorldOutlineNormalThreshold();
	block.celEdgeParams[ 3 ] = r_celShadingWorldDebug.GetBool() ? 1.0f : 0.0f;
	idVec4 color;
	R_CelWorldOutlineColor( color );
	block.celOutlineColor[ 0 ] = color.x;
	block.celOutlineColor[ 1 ] = color.y;
	block.celOutlineColor[ 2 ] = color.z;
	block.celOutlineColor[ 3 ] = color.w;

	const VkDescriptorSet sets[ 3 ] = {
		VK_Post_Descriptor( scene ), VK_Post_Descriptor( vkPostScene.celWorldDepth ), VK_Post_Descriptor( sceneDepth )
	};
	const int uniformOffset = VK_Exec_InteractionUniformAlloc( &block, sizeof( block ) );
	const VkPipeline pipeline = VK_Exec_PostPipeline( VK_POST_CEL_OUTLINE, vkPost.fullscreenVert, fragModule,
			GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	return VK_Post_DrawSceneRect( viewDef, pipeline, sets, 3, uniformOffset );
}

// one-shot bring-up evidence that a scene pass really drew
static void VK_Post_LogFirstDraw( bool &logged, const char *pass, const char *detail ) {
	if ( !logged ) {
		logged = true;
		common->Printf( "Vulkan: first %s pass drew%s\n", pass, detail );
	}
}

static void VK_Post_WarnOnce( bool &warned, const char *pass ) {
	if ( !warned ) {
		warned = true;
		common->Warning( "Vulkan: %s pass could not run (%s)", pass,
			vkPostFailReason != NULL ? vkPostFailReason : "draw refused" );
	}
	vkPostFailReason = NULL;
}

/*
====================
VK_PostProcess_DrawSceneEffects

RB_STD_DrawView steps 9-12 for one 3D view: SSAO, motion blur, bloom with the
tone map, then the cel world ink. Called for every 3D view after the post-fog
material passes; each pass gates itself on the main scene view. Returns true
when anything drew, in which case the caller restores its viewport and
treats _currentRender as stale.
====================
*/
static bool VK_Post_DrawSceneEffectsInView( const viewDef_t *viewDef ) {
	if ( viewDef == NULL || viewDef->viewEntitys == NULL || !VK_GuiExecutor_FrameIsOpen() ) {
		return false;
	}
	vkPostScene.viewSerial++;
	VK_Post_SynchronizeHDRExposure( viewDef, VK_HDRScene_LinearActive() );
	const bool ssao = VK_Post_SSAORequested( viewDef );
	const bool bloomPass = VK_Post_BloomPassRequested( viewDef );
	const bool celInk = VK_Post_CelInkRequested( viewDef );
	if ( !VK_Post_EnsureModules() ) {
		VK_Post_ResetMotionBlurHistory();
		return false;
	}
	if ( VK_Post_ViewWidth( viewDef ) <= 0 || VK_Post_ViewHeight( viewDef ) <= 0 ) {
		return false;
	}
	idRenderTexture *sceneTarget = VK_Exec_ActiveRenderTexture();
	const int sceneCubeFace = VK_Exec_ActiveCubeFace();
	bool drew = false;

	static bool ssaoWarned = false;
	static bool motionBlurWarned = false;
	static bool bloomWarned = false;
	static bool celWarned = false;
	static bool ssaoLogged = false;
	static bool motionBlurLogged = false;
	static bool bloomLogged = false;
	static bool celLogged = false;
	if ( ssao ) {
		if ( VK_Post_DrawSSAO( viewDef ) ) {
			drew = true;
			VK_Post_LogFirstDraw( ssaoLogged, "SSAO",
				vkPostScene.ssaoWorldDepthFrame == backEnd.frameCount ? " (world depth snapshot)" : "" );
		} else {
			VK_Post_WarnOnce( ssaoWarned, "r_ssao" );
		}
	}
	// Only the main camera advances the motion-blur history.
	if ( VK_Post_MotionBlur( viewDef, sceneTarget ) ) {
		drew = true;
		VK_Post_LogFirstDraw( motionBlurLogged, "motion blur",
			vkPostScene.motionVectorValid ? " (with object vectors)" : " (camera only)" );
	}
	if ( !VK_Exec_SetRenderTarget( sceneTarget, sceneCubeFace ) ) {
		VK_Post_WarnOnce( motionBlurWarned, "r_motionBlur" );
		return drew;
	}
	if ( bloomPass ) {
		const bool linearScene = R_ModernGLExecutor_PBRLinearSceneActive();
		const bool outputSubmitted = VK_Post_DrawBloom( viewDef, sceneTarget, linearScene );
		if ( linearScene ) { VK_HDRScene_FinishOutput( outputSubmitted ); }
		if ( outputSubmitted ) {
			drew = true;
			VK_Post_LogFirstDraw( bloomLogged, "bloom/tone map", "" );
		} else {
			VK_Post_WarnOnce( bloomWarned, "r_bloom/r_hdrToneMap" );
		}
		if ( !VK_Exec_SetRenderTarget( sceneTarget, sceneCubeFace ) ) {
			return drew;
		}
	}
	if ( celInk ) {
		if ( VK_Post_DrawCelInk( viewDef ) ) {
			drew = true;
			VK_Post_LogFirstDraw( celLogged, "cel world outline", "" );
		} else if ( vkPostScene.celWorldDepthFrame == backEnd.frameCount ) {
			VK_Post_WarnOnce( celWarned, "r_celShadingWorld outline" );
		}
	}
	return drew;
}

bool VK_PostProcess_DrawSceneEffects( const viewDef_t *viewDef ) {
	const bool drew = VK_Post_DrawSceneEffectsInView( viewDef );
	// Every exit settles a linear scene. One that never reached its output
	// pass (an early return above) reports "output" instead of staying active
	// for the GUI and later views of the frame.
	if ( VK_HDRScene_LinearActive() ) {
		static bool warned = false;
		if ( !warned ) {
			warned = true;
			common->Warning( "Vulkan: a linear HDR scene ended without its tone-map output (%s)",
				vkPostFailReason != NULL ? vkPostFailReason : "scene effects stopped early" );
		}
		VK_HDRScene_FinishOutput( false );
	}
	return drew;
}

/*
===============================================================================

	Underwater view (RB_STD_Underwater)

	A scene pass over the finished world, after the SS_POST_PROCESS surfaces
	and before the HUD, confined to the main view. The game asks
	RB_UnderwaterViewAvailable before handing over the state and draws a flat
	wash itself when the answer is no.

===============================================================================
*/

bool RB_UnderwaterViewAvailable( void ) {
	if ( r_skipPostProcess.GetBool() || !r_underwater.GetBool() ) {
		return false;
	}
	return vkCtx.device != VK_NULL_HANDLE && VK_Post_SceneModule( VK_POST_MODULE_UNDERWATER ) != VK_NULL_HANDLE;
}

// std140 layout of UnderwaterBlock in post_underwater.frag
typedef struct vkPostUnderwaterBlock_s {
	float	texInfo[ 4 ];
	float	depthInfo[ 4 ];
	float	tint[ 4 ];
	float	fogParams[ 4 ];
	float	effectParams0[ 4 ];
	float	effectParams1[ 4 ];
} vkPostUnderwaterBlock_t;

bool VK_PostProcess_DrawUnderwater( const viewDef_t *viewDef ) {
	const float amount = idMath::ClampFloat( 0.0f, 1.0f, tr.underwaterAmount );
	if ( amount <= 0.001f || viewDef == NULL || !RB_UnderwaterViewAvailable()
			|| !VK_Post_IsMainSceneView( viewDef ) || !VK_GuiExecutor_FrameIsOpen() ) {
		return false;
	}
	const int width = VK_Post_ViewWidth( viewDef );
	const int height = VK_Post_ViewHeight( viewDef );
	if ( width <= 0 || height <= 0 || !VK_Post_EnsureModules() ) {
		return false;
	}
	// the post-process surfaces may have drawn since the scene effects, so
	// take depth afresh; without it the shader treats everything as mid-range
	vkPostScene.finalDepthFrame = -1;
	idImage *depthImage = VK_Post_FinalDepth( viewDef );
	idImage *scene = VK_Post_CaptureScene( viewDef );
	if ( scene == NULL ) {
		return false;
	}

	vkPostUnderwaterBlock_t block;
	memset( &block, 0, sizeof( block ) );
	block.texInfo[ 0 ] = 1.0f / (float)width;
	block.texInfo[ 1 ] = 1.0f / (float)height;
	block.texInfo[ 2 ] = 1.0f;		// the capture is exactly the view's size
	block.texInfo[ 3 ] = 1.0f;
	block.depthInfo[ 0 ] = viewDef->projectionMatrix[ 10 ];
	block.depthInfo[ 1 ] = viewDef->projectionMatrix[ 14 ];
	block.depthInfo[ 2 ] = amount;
	block.depthInfo[ 3 ] = (float)backEnd.frameCount * ( 1.0f / 60.0f );
	block.tint[ 0 ] = idMath::ClampFloat( 0.0f, 1.0f, tr.underwaterTint.x );
	block.tint[ 1 ] = idMath::ClampFloat( 0.0f, 1.0f, tr.underwaterTint.y );
	block.tint[ 2 ] = idMath::ClampFloat( 0.0f, 1.0f, tr.underwaterTint.z );
	block.fogParams[ 0 ] = Max( 1.0f, tr.underwaterFogDistance * Max( 0.01f, r_underwaterVisibility.GetFloat() ) );
	block.fogParams[ 1 ] = depthImage != NULL ? 1.0f : 0.0f;
	block.fogParams[ 2 ] = (float)width / (float)height;
	block.effectParams0[ 0 ] = idMath::ClampFloat( 0.0f, 4.0f, r_underwaterWarp.GetFloat() ) * 0.0035f;
	block.effectParams0[ 1 ] = idMath::ClampFloat( 0.0f, 4.0f, r_underwaterBlur.GetFloat() );
	block.effectParams0[ 2 ] = idMath::ClampFloat( 0.0f, 2.0f, r_underwaterEdgeSoften.GetFloat() );
	block.effectParams0[ 3 ] = idMath::ClampFloat( 0.0f, 0.5f, r_underwaterCaustics.GetFloat() );
	block.effectParams1[ 0 ] = idMath::ClampFloat( 0.0f, 4.0f, r_underwaterBloom.GetFloat() );
	block.effectParams1[ 1 ] = idMath::ClampFloat( 0.0f, 4.0f, r_underwaterAberration.GetFloat() );
	block.effectParams1[ 2 ] = idMath::ClampFloat( 0.0f, 2.0f, r_underwaterParticles.GetFloat() );

	const VkDescriptorSet sets[ 2 ] = {
		VK_Post_Descriptor( scene ), VK_Post_Descriptor( depthImage != NULL ? depthImage : scene )
	};
	const int uniformOffset = VK_Exec_InteractionUniformAlloc( &block, sizeof( block ) );
	const VkPipeline pipeline = VK_Exec_PostPipeline( VK_POST_UNDERWATER, vkPost.fullscreenVert,
			VK_Post_SceneModule( VK_POST_MODULE_UNDERWATER ), GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	if ( !VK_Post_DrawSceneRect( viewDef, pipeline, sets, 2, uniformOffset ) ) {
		static bool warned = false;
		VK_Post_WarnOnce( warned, "underwater view" );
		return false;
	}
	static bool logged = false;
	VK_Post_LogFirstDraw( logged, "underwater view", "" );
	backEnd.currentRenderCopied = false;
	return true;
}

/*
===============================================================================

	r_showIntensity and r_showDepth

	vk_DebugTools.cpp's RB_ShowIntensity and RB_ShowDepthBuffer. OpenGL reads
	the frame back with glReadPixels, recolours it on the CPU and draws it
	again with glDrawPixels; here post_debug_view.frag does the same mapping
	over a copy of the view.

===============================================================================
*/

// std140 layout of DebugViewBlock in post_debug_view.frag
typedef struct vkPostDebugViewBlock_s {
	float	params[ 4 ];	// x: 0 intensity, 1 depth
} vkPostDebugViewBlock_t;

bool VK_PostProcess_DrawDebugView( const viewDef_t *viewDef, int mode ) {
	if ( viewDef == NULL || !VK_GuiExecutor_FrameIsOpen() || !VK_Post_EnsureModules() ) {
		return false;
	}
	const VkShaderModule fragModule = VK_Post_SceneModule( VK_POST_MODULE_DEBUG_VIEW );
	if ( fragModule == VK_NULL_HANDLE ) {
		return false;
	}
	idImage *source = NULL;
	if ( mode == 1 ) {
		// always afresh: the debug tools draw after the scene effects' capture
		vkPostScene.finalDepthFrame = -1;
		source = VK_Post_FinalDepth( viewDef );
	} else {
		source = VK_Post_CaptureScene( viewDef );
	}
	if ( source == NULL ) {
		return false;
	}

	vkPostDebugViewBlock_t block;
	memset( &block, 0, sizeof( block ) );
	block.params[ 0 ] = mode == 1 ? 1.0f : 0.0f;
	const VkDescriptorSet sets[ 1 ] = { VK_Post_Descriptor( source ) };
	const int uniformOffset = VK_Exec_InteractionUniformAlloc( &block, sizeof( block ) );
	const VkPipeline pipeline = VK_Exec_PostPipeline( VK_POST_DEBUG_VIEW, vkPost.fullscreenVert,
			fragModule, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	if ( !VK_Post_DrawSceneRect( viewDef, pipeline, sets, 1, uniformOffset ) ) {
		static bool warned = false;
		VK_Post_WarnOnce( warned, mode == 1 ? "depth debug view" : "intensity debug view" );
		return false;
	}
	backEnd.currentRenderCopied = false;
	return true;
}

#endif /* OPENQ4_RENDERER_VK_MODULE */
