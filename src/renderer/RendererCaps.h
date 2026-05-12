// Copyright (C) 2004 Id Software, Inc.
//

#ifndef __RENDERER_CAPS_H__
#define __RENDERER_CAPS_H__

/*
===============================================================================

	Feature-driven renderer capability model.

	This is the GL-only foundation for the staged renderer rewrite.  The active
	executor can still be the legacy ARB2 bridge while these structs describe the
	actual context and the highest modern path the driver can support.

===============================================================================
*/

enum rendererTier_t {
	RENDERER_TIER_NULL = 0,
	RENDERER_TIER_LEGACY_GL2_COMPAT,
	RENDERER_TIER_MODERN_GL33,
	RENDERER_TIER_MODERN_GL41,
	RENDERER_TIER_GPU_DRIVEN_GL43,
	RENDERER_TIER_LOW_OVERHEAD_GL45,
	RENDERER_TIER_TOP_GL46
};

enum rendererTierPreference_t {
	RENDERER_TIER_PREF_AUTO = 0,
	RENDERER_TIER_PREF_LEGACY,
	RENDERER_TIER_PREF_GL33,
	RENDERER_TIER_PREF_GL41,
	RENDERER_TIER_PREF_GL43,
	RENDERER_TIER_PREF_GL45,
	RENDERER_TIER_PREF_GL46
};

enum rendererContextProfile_t {
	RENDERER_CONTEXT_PROFILE_UNKNOWN = 0,
	RENDERER_CONTEXT_PROFILE_COMPATIBILITY,
	RENDERER_CONTEXT_PROFILE_CORE
};

typedef struct rendererContextRequest_s {
	int							major;
	int							minor;
	rendererContextProfile_t	profile;
	bool						debugContext;
	bool						explicitVersion;
	char						label[32];
} rendererContextRequest_t;

typedef struct renderBackendCaps_s {
	bool						contextCreated;
	int							glMajor;
	int							glMinor;
	float						glVersion;
	rendererContextProfile_t	profile;
	bool						debugContext;
	bool						forwardCompatibleContext;

	bool						hasFixedFunctionCompatibility;
	bool						hasARBVertexProgram;
	bool						hasARBFragmentProgram;
	bool						hasARBShaderObjects;
	bool						hasGLSL;
	bool						hasVBO;
	bool						hasPBO;
	bool						hasFBO;
	bool						hasMRT;
	bool						hasSRGBTextures;
	bool						hasFramebufferSRGB;

	bool						hasUBO;
	bool						hasVAO;
	bool						hasInstancing;
	bool						hasTextureArrays;
	bool						hasTimerQuery;
	bool						hasSync;
	bool						hasMapBufferRange;
	bool						hasBufferStorage;
	bool						hasDSA;
	bool						hasMultiBind;
	bool						hasCompute;
	bool						hasSSBO;
	bool						hasDrawIndirect;
	bool						hasMultiDrawIndirect;
	bool						hasTextureViews;
	bool						hasGLSpirv;
	bool						hasBindlessTexture;
	bool						hasDebugOutput;

	int							maxTextureSize;
	int							maxTextureUnits;
	int							maxTextureCoords;
	int							maxTextureImageUnits;
	int							maxDrawBuffers;
	int							maxColorAttachments;
} renderBackendCaps_t;

typedef struct renderFeatureSet_s {
	bool						legacyARB2Bridge;
	bool						modernBaseline;
	bool						modernGL41;
	bool						gpuDriven;
	bool						lowOverhead;
	bool						persistentMappedUploads;
	bool						directStateAccess;
	bool						multiBind;
	bool						bindlessTextures;
	bool						shaderLibrary;
	bool						scenePackets;
	bool						renderGraph;
} renderFeatureSet_t;

const char *RendererTier_Name( rendererTier_t tier );
const char *RendererTier_CVarName( rendererTier_t tier );
const char *RendererContextProfile_Name( rendererContextProfile_t profile );

rendererTierPreference_t RendererTierPreference_FromString( const char *value );
rendererTier_t RendererTierPreference_ToForcedTier( rendererTierPreference_t preference );
bool RendererTier_IsModern( rendererTier_t tier );
bool RendererCaps_SupportsTier( const renderBackendCaps_t &caps, rendererTier_t tier );
rendererTier_t RendererTier_Select( const renderBackendCaps_t &caps, rendererTierPreference_t preference );
renderFeatureSet_t RendererFeatureSet_Build( const renderBackendCaps_t &caps, rendererTier_t tier );
void RendererCaps_FormatSummary( const renderBackendCaps_t &caps, char *buffer, int bufferSize );

bool RendererTierSelect_RunSelfTest( void );

void GLCapabilityProbe_Build( renderBackendCaps_t &caps, const char *versionString, const char *legacyExtensionsString );
bool GLCapabilityProbe_HasExtension( const char *name );
const char *GLCapabilityProbe_ExtensionString( void );

#endif /* !__RENDERER_CAPS_H__ */
