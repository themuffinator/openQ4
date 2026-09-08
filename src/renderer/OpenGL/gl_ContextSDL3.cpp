// Copyright (C) 2026 DarkMatter Productions
//

/*
===============================================================================

	SDL3 GL context half of the window/context seam (Phase B5b,
	docs/dev/plans/2026-07-16-vulkan-renderer-phase-b.md).

	This file owns the rendering context: candidate negotiation, context
	creation/teardown, current-ness, buffer swaps, swap interval, and
	extension lookup. The engine owns the window/display/input layer and is
	reached exclusively through renderWindowServices_t, so the Phase B8
	module split only has to change who compiles this file.

	Until B8 it is compiled as part of the SDL3 platform backend translation
	unit (sdl3_backend.cpp defines OPENQ4_SDL3_CONTEXT_IMPL and includes it
	at its tail), which supplies the SDL/QGL/wgl declarations; standalone
	glob compilation produces an empty TU.

===============================================================================
*/

#ifdef OPENQ4_SDL3_CONTEXT_IMPL

#ifndef OPENQ4_SDL3_CONTEXT_ENCLOSED
// standalone module compilation (Phase B8): supply the declarations the
// enclosing platform backend otherwise provides
#include "../tr_local.h"
#include "../RenderModuleAPI.h"
bool QGL_Init(const char *dllname);
void QGL_Shutdown(void);
void *GLimp_ExtensionPointer(const char *name);
#if defined(_WIN32)
// the wgl extension pointers win_local.h declares and win_qgl.cpp defines
// engine-side live in this TU inside the module (RenderSystem_init.cpp
// re-declares wglSwapIntervalEXT extern, so external linkage is required)
#include "../wglext.h"
PFNWGLGETEXTENSIONSSTRINGARBPROC wglGetExtensionsStringARB = NULL;
PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT = NULL;
PFNWGLGETPIXELFORMATATTRIBIVARBPROC wglGetPixelFormatAttribivARB = NULL;
PFNWGLGETPIXELFORMATATTRIBFVARBPROC wglGetPixelFormatAttribfvARB = NULL;
PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARB = NULL;
PFNWGLCREATEPBUFFERARBPROC wglCreatePbufferARB = NULL;
PFNWGLGETPBUFFERDCARBPROC wglGetPbufferDCARB = NULL;
PFNWGLRELEASEPBUFFERDCARBPROC wglReleasePbufferDCARB = NULL;
PFNWGLDESTROYPBUFFERARBPROC wglDestroyPbufferARB = NULL;
PFNWGLQUERYPBUFFERARBPROC wglQueryPbufferARB = NULL;
PFNWGLBINDTEXIMAGEARBPROC wglBindTexImageARB = NULL;
PFNWGLRELEASETEXIMAGEARBPROC wglReleaseTexImageARB = NULL;
PFNWGLSETPBUFFERATTRIBARBPROC wglSetPbufferAttribARB = NULL;
#else
#define OPENQ4_SDL3_POSIX_HOST 1
#if defined(__linux__)
// the module build has no linux_sdl3.cpp wrapper to define the host macro;
// the GLEW SDL3-loader hook (OpenQ4_GlewGetProcAddress) hangs off it
#define OPENQ4_SDL3_LINUX_HOST 1
#endif
#endif
#endif

// opaque handles into the engine's video instance; every operation on them
// crosses through renderWindowServices_t
static void *s_glWindow = NULL;
static void *s_glContext = NULL;
static void *s_glHDC = NULL;
static const renderWindowServices_t *s_glWindowServices = NULL;

static const char *R_GLVideoError(void) {
	return ( s_glWindowServices != NULL && s_glWindowServices->GetVideoErrorString != NULL )
		? s_glWindowServices->GetVideoErrorString() : "unknown video error";
}

#if defined(OPENQ4_SDL3_POSIX_HOST) || !defined(OPENQ4_SDL3_CONTEXT_ENCLOSED)
// The SDL3 POSIX backends (and the standalone module build, which has no
// win_glimp) reuse this translation unit but do not provide the legacy
// platform GL logging hooks from the native GLX/Cocoa paths.
void GLimp_EnableLogging(bool enable) {
	static bool loggingEnabled = false;
	if (enable != loggingEnabled) {
		common->DPrintf("GLimp_EnableLogging - unavailable for SDL3 POSIX backend\n");
		loggingEnabled = enable;
	}
}
#endif

static void SDL3_WindowParmsFromGlimpParms(const glimpParms_t &src, renderWindowParms_t &dst) {
	memset(&dst, 0, sizeof(dst));
	dst.width = src.width;
	dst.height = src.height;
	dst.fullScreen = src.fullScreen;
	dst.borderless = src.borderless;
	dst.hiddenWindow = src.hiddenWindow;
	dst.stereo = src.stereo;
	dst.displayHz = src.displayHz;
	dst.multiSamples = src.multiSamples;
}

static bool SDL3_EnsureGLContextCurrent(const char *operation) {
	if (!s_glWindow || !s_glContext) {
		return false;
	}

	if (s_glWindowServices->IsGLContextCurrent(s_glContext)) {
		return true;
	}

	if (!s_glWindowServices->MakeGLContextCurrent(s_glContext)) {
		common->Printf("SDL3: failed to make GL context current for %s: %s\n", operation ? operation : "operation", R_GLVideoError());
		if (s_glWindowServices != NULL && s_glWindowServices->CountContextError != NULL) {
			s_glWindowServices->CountContextError();
		}
		return false;
	}

	return true;
}

static bool SDL3_ApplySwapInterval(void) {
	if (!s_glWindow || !s_glContext) {
		return false;
	}
	if (!SDL3_EnsureGLContextCurrent("swap interval update")) {
		return false;
	}

	const int requestedInterval = R_GetEffectiveSwapInterval();
	if (!s_glWindowServices->SetGLSwapInterval(requestedInterval)) {
		common->Printf("SDL3: failed to set swap interval %d: %s\n", requestedInterval, R_GLVideoError());
		return false;
	}

	int actualInterval = 0;
	if (!s_glWindowServices->GetGLSwapInterval(&actualInterval)) {
		common->Printf("SDL3: swap interval set to %d, but query failed: %s\n", requestedInterval, R_GLVideoError());
	} else if (actualInterval == requestedInterval) {
		common->Printf("SDL3: swap interval set to %d\n", actualInterval);
	} else {
		common->Printf("SDL3: requested swap interval %d, driver reports %d\n", requestedInterval, actualInterval);
	}

	return true;
}

static void SDL3_LoadWGLExtensions(void) {
#if defined(OPENQ4_SDL3_POSIX_HOST)
	glConfig.wgl_extensions_string = "";
	r_swapInterval.SetModified();
#else
	wglGetExtensionsStringARB = (PFNWGLGETEXTENSIONSSTRINGARBPROC)GLimp_ExtensionPointer("wglGetExtensionsStringARB");
	if (wglGetExtensionsStringARB && s_glHDC) {
		glConfig.wgl_extensions_string = (const char *)wglGetExtensionsStringARB((HDC)s_glHDC);
	} else {
		glConfig.wgl_extensions_string = "";
	}

	wglSwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC)GLimp_ExtensionPointer("wglSwapIntervalEXT");
	r_swapInterval.SetModified();

	wglGetPixelFormatAttribivARB = (PFNWGLGETPIXELFORMATATTRIBIVARBPROC)GLimp_ExtensionPointer("wglGetPixelFormatAttribivARB");
	wglGetPixelFormatAttribfvARB = (PFNWGLGETPIXELFORMATATTRIBFVARBPROC)GLimp_ExtensionPointer("wglGetPixelFormatAttribfvARB");
	wglChoosePixelFormatARB = (PFNWGLCHOOSEPIXELFORMATARBPROC)GLimp_ExtensionPointer("wglChoosePixelFormatARB");

	wglCreatePbufferARB = (PFNWGLCREATEPBUFFERARBPROC)GLimp_ExtensionPointer("wglCreatePbufferARB");
	wglGetPbufferDCARB = (PFNWGLGETPBUFFERDCARBPROC)GLimp_ExtensionPointer("wglGetPbufferDCARB");
	wglReleasePbufferDCARB = (PFNWGLRELEASEPBUFFERDCARBPROC)GLimp_ExtensionPointer("wglReleasePbufferDCARB");
	wglDestroyPbufferARB = (PFNWGLDESTROYPBUFFERARBPROC)GLimp_ExtensionPointer("wglDestroyPbufferARB");
	wglQueryPbufferARB = (PFNWGLQUERYPBUFFERARBPROC)GLimp_ExtensionPointer("wglQueryPbufferARB");

	wglBindTexImageARB = (PFNWGLBINDTEXIMAGEARBPROC)GLimp_ExtensionPointer("wglBindTexImageARB");
	wglReleaseTexImageARB = (PFNWGLRELEASETEXIMAGEARBPROC)GLimp_ExtensionPointer("wglReleaseTexImageARB");
	wglSetPbufferAttribARB = (PFNWGLSETPBUFFERATTRIBARBPROC)GLimp_ExtensionPointer("wglSetPbufferAttribARB");
#endif
}

void GLimp_SetGamma(unsigned short red[256], unsigned short green[256], unsigned short blue[256]) {
	(void)red;
	(void)green;
	(void)blue;
}

bool GLimp_UseNativeGammaRamps(void) {
	return false;
}

static void SDL3_MoveCompatibilityFallbacksToFront(rendererContextCandidate_t *candidates, int candidateCount) {
	int insertIndex = 0;

	for (int i = 0; i < candidateCount; ++i) {
		if (candidates[i].explicitVersion || candidates[i].profile != RENDERER_CONTEXT_PROFILE_COMPATIBILITY) {
			continue;
		}

		if (i != insertIndex) {
			rendererContextCandidate_t fallback = candidates[i];
			memmove(&candidates[insertIndex + 1], &candidates[insertIndex], (i - insertIndex) * sizeof(candidates[0]));
			candidates[insertIndex] = fallback;
		}
		++insertIndex;
	}
}

// Declared in RendererBootstrap.h; forward-declared here because this TU is
// textually included from the platform backends, where the renderer include
// path differs.
bool RendererBootstrap_ShouldAutoPromoteModernVisible( void );

// The inverse of the above, for r_glCoreProfileFirst.
//
// RendererContextLadder_Build deliberately orders a forced modern tier as
// versioned-compatibility, then the 2.1-ish compatibility fallback, then core,
// so gameplay does not land on a core profile while the modern visible path is
// incomplete. On a driver that caps compatibility below 3.3 that ordering makes
// the modern tier unreachable: every versioned compatibility request fails, the
// fallback succeeds, and the core candidates behind it are never tried. macOS is
// exactly this case -- Apple offers 2.1 compatibility or 4.1 core and nothing
// between -- so r_glTier gl41 still lands on the legacy ARB2 bridge.
//
// Hoisting the core candidates ahead of the fallback makes the modern path
// reachable for diagnosis. Off by default: it changes which context shipping
// configurations get.
static void SDL3_MoveCoreCandidatesToFront(rendererContextCandidate_t *candidates, int candidateCount) {
	int insertIndex = 0;

	for (int i = 0; i < candidateCount; ++i) {
		if (candidates[i].profile != RENDERER_CONTEXT_PROFILE_CORE) {
			continue;
		}

		if (i != insertIndex) {
			rendererContextCandidate_t core = candidates[i];
			memmove(&candidates[insertIndex + 1], &candidates[insertIndex], (i - insertIndex) * sizeof(candidates[0]));
			candidates[insertIndex] = core;
		}
		++insertIndex;
	}
}

// Puts a single GLES 3.0 candidate at the front of the ladder. The desktop-GL
// candidates are deliberately left in place behind it: if no EGL/GLES driver is
// present the ES request simply fails and the normal ladder proceeds, so
// enabling this cannot leave the engine without a context.
static int SDL3_PrependGLESCandidate(rendererContextCandidate_t *candidates, int maxCandidates, int candidateCount) {
	if (candidates == NULL || maxCandidates <= 0) {
		return candidateCount;
	}

	if (candidateCount >= maxCandidates) {
		candidateCount = maxCandidates - 1;
	}
	if (candidateCount > 0) {
		memmove(&candidates[1], &candidates[0], candidateCount * sizeof(candidates[0]));
	}

	rendererContextCandidate_t &es = candidates[0];
	memset(&es, 0, sizeof(es));
	es.major = 3;
	es.minor = 0;
	es.profile = RENDERER_CONTEXT_PROFILE_ES;
	es.explicitVersion = true;
	es.debugContext = r_glDebugContext.GetBool();
	idStr::Copynz(es.label, "gles 3.0", sizeof(es.label));

	return candidateCount + 1;
}

static int SDL3_BuildGLContextCandidates(rendererContextCandidate_t *candidates, int maxCandidates) {
#if defined(OPENQ4_RENDERER_GLES_MODULE)
	// The GLES module links libGLESv2 and nothing else, so a desktop context is
	// not a fallback -- it is a context none of this module's GL entry points
	// can talk to. Without this the macOS ladder hands back a 2.1 compatibility
	// context, ANGLE never becomes current, and the first glGetString returns
	// NULL. Offer the ES candidate alone and let context creation fail loudly
	// if the platform cannot provide one.
	(void)r_glTier;
	return SDL3_PrependGLESCandidate(candidates, maxCandidates, 0);
#else
	const rendererTierPreference_t preference = RendererTierPreference_FromString(r_glTier.GetString());
	// r_glTier auto asks for a compatibility profile because the ARB2 bridge is
	// what actually draws, and it needs fixed-function state. Once the modern
	// visible path is promoted it no longer does, and the ladder's modern-auto
	// mode (covered by rendererContextLadderSelfTest) takes over. This is
	// deliberately driven by the promotion state rather than hardcoded, so the
	// profile request follows the renderer that will really be used; while no
	// parity contract is proven the promotion state is false and this is exactly
	// the previous behaviour.
	const bool keepAutoCompatibility = preference == RENDERER_TIER_PREF_AUTO
			&& !RendererBootstrap_ShouldAutoPromoteModernVisible();
	const int candidateCount = RendererContextLadder_Build(
		candidates,
		maxCandidates,
		preference,
		r_glDebugContext.GetBool(),
		keepAutoCompatibility);

	if (candidateCount > 0 && preference == RENDERER_TIER_PREF_AUTO
			&& s_glWindowServices != NULL && s_glWindowServices->PreferCompatibilityFallbackFirst != NULL) {
		const char *quirkMessage = NULL;
		if (s_glWindowServices->PreferCompatibilityFallbackFirst(&quirkMessage)) {
			SDL3_MoveCompatibilityFallbacksToFront(candidates, candidateCount);
			if (quirkMessage != NULL) {
				common->Printf("%s", quirkMessage);
			}
		}
	}

	if (candidateCount > 0 && r_glCoreProfileFirst.GetBool()) {
		SDL3_MoveCoreCandidatesToFront(candidates, candidateCount);
		common->Printf("SDL3: r_glCoreProfileFirst -- core-profile contexts moved ahead of the compatibility fallback\n");
	}

	if (r_glesContext.GetBool()) {
		return SDL3_PrependGLESCandidate(candidates, maxCandidates, candidateCount);
	}

	return candidateCount;
#endif
}

static int SDL3_NormalizeMSAASampleFallback(const int samples) {
	if (samples <= 1) {
		return 0;
	}
	if (samples <= 2) {
		return 2;
	}
	if (samples <= 4) {
		return 4;
	}
	if (samples <= 8) {
		return 8;
	}
	return 16;
}

static int SDL3_BuildMSAASampleFallbacks(const int requestedSamples, int *fallbacks, const int maxFallbacks) {
	static const int sampleSteps[] = {16, 8, 4, 2, 0};
	const int normalizedSamples = SDL3_NormalizeMSAASampleFallback(requestedSamples);
	int fallbackCount = 0;

	if (fallbacks == NULL || maxFallbacks <= 0) {
		return 0;
	}

	for (int i = 0; i < static_cast<int>(sizeof(sampleSteps) / sizeof(sampleSteps[0])) && fallbackCount < maxFallbacks; ++i) {
		if (sampleSteps[i] <= normalizedSamples) {
			fallbacks[fallbackCount++] = sampleSteps[i];
		}
	}

	return fallbackCount > 0 ? fallbackCount : 1;
}

static void SDL3_BuildFramebufferDesc(const glimpParms_t &parms, const rendererContextCandidate_t &candidate, const int multiSamples, renderFramebufferDesc_t &desc) {
	memset(&desc, 0, sizeof(desc));
	desc.redBits = 8;
	desc.greenBits = 8;
	desc.blueBits = 8;
	desc.alphaBits = 8;
	desc.depthBits = 24;
	desc.stencilBits = 8;
	desc.doubleBuffer = true;
	desc.stereo = parms.stereo;
	desc.multiSamples = multiSamples;
	desc.explicitGLVersion = candidate.explicitVersion;
	desc.glMajor = candidate.major;
	desc.glMinor = candidate.minor;
	desc.glCoreProfile = candidate.profile == RENDERER_CONTEXT_PROFILE_CORE;
	desc.glESProfile = candidate.profile == RENDERER_CONTEXT_PROFILE_ES;
	desc.glDebugContext = candidate.debugContext;
}

static void SDL3_RecordGLContextCandidate(const rendererContextCandidate_t &candidate) {
	memset(&glConfig.contextRequest, 0, sizeof(glConfig.contextRequest));
	glConfig.contextRequest = candidate;
}

static const char *SDL3_GLProfileMaskName(int profileMask) {
	switch (profileMask) {
		case RENDER_GLPROFILE_COMPATIBILITY:
			return "compatibility";
		case RENDER_GLPROFILE_CORE:
			return "core";
		case RENDER_GLPROFILE_ES:
			return "es";
		default:
			return "default";
	}
}

static void SDL3_LogGLContextAttributes(const int requestedMultiSamples, const int selectedMultiSamples) {
	int major = 0;
	int minor = 0;
	int profileMask = 0;
	int flags = 0;
	int multisampleBuffers = 0;
	int multisampleSamples = 0;

	const bool gotMajor = s_glWindowServices->GetGLAttribute(RENDER_GLATTR_CONTEXT_MAJOR_VERSION, &major);
	const bool gotMinor = s_glWindowServices->GetGLAttribute(RENDER_GLATTR_CONTEXT_MINOR_VERSION, &minor);
	const bool gotProfile = s_glWindowServices->GetGLAttribute(RENDER_GLATTR_CONTEXT_PROFILE_MASK, &profileMask);
	const bool gotFlags = s_glWindowServices->GetGLAttribute(RENDER_GLATTR_CONTEXT_FLAGS, &flags);
	const bool gotMultisampleBuffers = s_glWindowServices->GetGLAttribute(RENDER_GLATTR_MULTISAMPLE_BUFFERS, &multisampleBuffers);
	const bool gotMultisampleSamples = s_glWindowServices->GetGLAttribute(RENDER_GLATTR_MULTISAMPLE_SAMPLES, &multisampleSamples);

	common->Printf(
		"SDL3: reported OpenGL context attributes: version=%s%d.%d profile=%s flags=%s0x%x\n",
		(gotMajor && gotMinor) ? "" : "<unreported> ",
		major,
		minor,
		gotProfile ? SDL3_GLProfileMaskName(profileMask) : "unreported",
		gotFlags ? "" : "<unreported> ",
		flags);
	common->Printf(
		"SDL3: reported OpenGL multisample attributes: requested=%d selected=%d actualBuffers=%s%d actualSamples=%s%d\n",
		requestedMultiSamples,
		selectedMultiSamples,
		gotMultisampleBuffers ? "" : "<unreported> ",
		multisampleBuffers,
		gotMultisampleSamples ? "" : "<unreported> ",
		multisampleSamples);

	// Stencil shadow volumes need a stencil buffer, and a driver is free to
	// hand back a visual without one. Nothing printed the achieved sizes, so
	// "no shadows" was undiagnosable from a reporter log (issue #73).
	int depthBits = 0;
	int stencilBits = 0;
	const bool gotDepthBits = s_glWindowServices->GetGLAttribute(RENDER_GLATTR_DEPTH_SIZE, &depthBits);
	const bool gotStencilBits = s_glWindowServices->GetGLAttribute(RENDER_GLATTR_STENCIL_SIZE, &stencilBits);
	common->Printf(
		"SDL3: reported OpenGL framebuffer attributes: depthBits=%s%d stencilBits=%s%d hasStencilBuffer=%d\n",
		gotDepthBits ? "" : "<unreported> ",
		depthBits,
		gotStencilBits ? "" : "<unreported> ",
		stencilBits,
		( gotStencilBits && stencilBits > 0 ) ? 1 : 0);
	if (gotStencilBits && stencilBits <= 0) {
		common->Warning(
			"the OpenGL context has no stencil buffer; stencil shadow volumes cannot be drawn on this visual");
	}
}

bool GLimp_Init(glimpParms_t parms) {
	const char *driverName;

	s_glWindowServices = Sys_GetRenderWindowServices();
	if (s_glWindowServices == NULL) {
		common->Printf("SDL3: no window services available for GL context creation\n");
		return false;
	}

	common->Printf("Initializing OpenGL subsystem (SDL3 backend)\n");
	if (!s_glWindowServices->PrepareWindowSystem()) {
		return false;
	}

	rendererContextCandidate_t contextCandidates[RENDERER_CONTEXT_LADDER_MAX_CANDIDATES];
	const int contextCandidateCount = SDL3_BuildGLContextCandidates(contextCandidates, static_cast<int>(sizeof(contextCandidates) / sizeof(contextCandidates[0])));
	if (contextCandidateCount <= 0) {
		common->Printf("SDL3: no OpenGL context candidates were generated for r_glTier %s\n", r_glTier.GetString());
		return false;
	}

	if (parms.hiddenWindow) {
		parms.fullScreen = false;
		parms.borderless = false;
		common->Printf("SDL3: creating hidden OpenGL render window\n");
	}

	const int requestedMultiSamples = SDL3_NormalizeMSAASampleFallback(parms.multiSamples);
	parms.multiSamples = requestedMultiSamples;
	int multiSampleFallbacks[5];
	const int multiSampleFallbackCount = SDL3_BuildMSAASampleFallbacks(
		requestedMultiSamples,
		multiSampleFallbacks,
		static_cast<int>(sizeof(multiSampleFallbacks) / sizeof(multiSampleFallbacks[0])));
	int selectedMultiSamples = 0;

	renderWindowParms_t windowParms;
	SDL3_WindowParmsFromGlimpParms(parms, windowParms);
	renderModuleWindowInfo_t windowInfo;
	memset(&windowInfo, 0, sizeof(windowInfo));
	bool reusedPreservedWindow = false;

	idStr lastContextError;
	s_glContext = NULL;
	for (int candidateIndex = 0; candidateIndex < contextCandidateCount; ++candidateIndex) {
		const rendererContextCandidate_t &candidate = contextCandidates[candidateIndex];
		for (int sampleIndex = 0; sampleIndex < multiSampleFallbackCount; ++sampleIndex) {
			const int candidateMultiSamples = multiSampleFallbacks[sampleIndex];
			renderFramebufferDesc_t framebufferDesc;
			SDL3_BuildFramebufferDesc(parms, candidate, candidateMultiSamples, framebufferDesc);

			if (!s_glWindowServices->CreateWindowForFramebuffer(&framebufferDesc, &windowParms, &windowInfo, &reusedPreservedWindow)) {
				lastContextError = R_GLVideoError();
				common->Printf("SDL3: could not create window for OpenGL context %s with MSAA samples=%d: %s\n", candidate.label, candidateMultiSamples, lastContextError.c_str());
				continue;
			}
			s_glWindow = windowInfo.sdlWindow;

			common->Printf("SDL3: trying OpenGL context %s with MSAA samples=%d\n", candidate.label, candidateMultiSamples);
			s_glContext = s_glWindowServices->CreateGLContext();
			if (s_glContext && s_glWindowServices->MakeGLContextCurrent(s_glContext)) {
				SDL3_RecordGLContextCandidate(candidate);
				selectedMultiSamples = candidateMultiSamples;
				common->Printf("SDL3: created OpenGL context %s with MSAA samples=%d\n", glConfig.contextRequest.label, selectedMultiSamples);
				break;
			}

			lastContextError = R_GLVideoError();
			common->Printf("SDL3: OpenGL context %s with MSAA samples=%d failed: %s\n", candidate.label, candidateMultiSamples, lastContextError.c_str());
			if (s_glContext) {
				(void)s_glWindowServices->MakeGLContextCurrent(NULL);
				s_glWindowServices->DestroyGLContext(s_glContext);
				s_glContext = NULL;
			}
			if (!reusedPreservedWindow) {
				s_glWindowServices->DestroyAttemptWindow();
				s_glWindow = NULL;
			}
		}
		if (s_glContext) {
			break;
		}
	}
	if (!s_glContext) {
		common->Printf("SDL3: could not create OpenGL context: %s\n", lastContextError.Length() > 0 ? lastContextError.c_str() : R_GLVideoError());
		return false;
	}

	if (!s_glWindowServices->MakeGLContextCurrent(s_glContext)) {
		common->Printf("SDL3: could not make context current: %s\n", R_GLVideoError());
		s_glWindowServices->DestroyGLContext(s_glContext);
		s_glContext = NULL;
		if (!reusedPreservedWindow) {
			s_glWindowServices->DestroyAttemptWindow();
			s_glWindow = NULL;
		}
		return false;
	}
	if (selectedMultiSamples != requestedMultiSamples) {
		common->Printf("SDL3: r_multiSamples requested %d, using %d after context creation fallback\n", requestedMultiSamples, selectedMultiSamples);
		r_multiSamples.SetInteger(selectedMultiSamples);
		r_multiSamples.ClearModified();
		parms.multiSamples = selectedMultiSamples;
	}
	SDL3_LogGLContextAttributes(requestedMultiSamples, selectedMultiSamples);

#if defined(__linux__)
	driverName = r_glDriver.GetString()[0] ? r_glDriver.GetString() : "libGL.so.1";
#elif defined(__APPLE__)
	driverName = r_glDriver.GetString()[0] ? r_glDriver.GetString() : "OpenGL.framework";
#else
	driverName = r_glDriver.GetString()[0] ? r_glDriver.GetString() : "opengl32";
#endif
	if (!QGL_Init(driverName)) {
		common->Printf("^3GLimp_Init() could not load r_glDriver \"%s\"^0\n", driverName);
		GLimp_Shutdown();
		return false;
	}

	SDL3_WindowParmsFromGlimpParms(parms, windowParms);
	if (!s_glWindowServices->ApplyScreenParms(&windowParms)) {
		GLimp_Shutdown();
		return false;
	}

	s_glWindowServices->RefreshNativeWindowHandles(&windowInfo);
	s_glHDC = windowInfo.nativeDisplayHandle;
	SDL3_LoadWGLExtensions();
	if (r_swapInterval.IsModified()) {
		r_swapInterval.ClearModified();
		(void)SDL3_ApplySwapInterval();
	}

	s_glWindowServices->NotifyWindowReady();
	GLimp_EnableLogging((r_logFile.GetInteger() != 0));

	return true;
}

bool GLimp_SetScreenParms(glimpParms_t parms) {
	const renderWindowServices_t *windowServices = s_glWindowServices != NULL ? s_glWindowServices : Sys_GetRenderWindowServices();
	if (windowServices == NULL) {
		return false;
	}

	if (parms.hiddenWindow) {
		parms.fullScreen = false;
		parms.borderless = false;
	}

	renderWindowParms_t windowParms;
	SDL3_WindowParmsFromGlimpParms(parms, windowParms);
	if (!windowServices->ApplyScreenParms(&windowParms)) {
		return false;
	}

	if (s_glWindow && s_glContext && !SDL3_EnsureGLContextCurrent("screen parm change")) {
		return false;
	}

	renderModuleWindowInfo_t windowInfo;
	windowServices->RefreshNativeWindowHandles(&windowInfo);
	s_glHDC = windowInfo.nativeDisplayHandle;
	r_swapInterval.SetModified();
	if (r_swapInterval.IsModified()) {
		r_swapInterval.ClearModified();
		(void)SDL3_ApplySwapInterval();
	}

	return true;
}

void GLimp_Shutdown(void) {
	const renderWindowServices_t *windowServices = s_glWindowServices != NULL ? s_glWindowServices : Sys_GetRenderWindowServices();

	common->Printf("Shutting down OpenGL subsystem (SDL3 backend)\n");

	if (windowServices != NULL) {
		windowServices->BeginWindowTeardown();
	}

	if (s_glContext) {
		if (s_glWindow) {
			(void)windowServices->MakeGLContextCurrent(NULL);
		}
		windowServices->DestroyGLContext(s_glContext);
		s_glContext = NULL;
	}
	s_glHDC = NULL;

	if (windowServices != NULL) {
		windowServices->FinishWindowTeardown();
	}
	// the engine owns the window; whether it survived depends on the
	// preserve-window flag, so drop the cached pointer either way
	s_glWindow = NULL;

	QGL_Shutdown();
}

void GLimp_SwapBuffers(void) {
	if (r_swapInterval.IsModified()) {
		r_swapInterval.ClearModified();
		(void)SDL3_ApplySwapInterval();
	}

	// the engine owns the window; poll its live state each present so this
	// renderer's glConfig tracks resizes and UI-viewport changes (the
	// engine does not mirror into a module renderer's glConfig)
	if (s_glWindow && s_glWindowServices != NULL && s_glWindowServices->RefreshNativeWindowHandles != NULL) {
		renderModuleWindowInfo_t windowInfo;
		s_glWindowServices->RefreshNativeWindowHandles(&windowInfo);
		if (windowInfo.pixelWidth > 0 && windowInfo.pixelHeight > 0) {
			glConfig.vidWidth = windowInfo.pixelWidth;
			glConfig.vidHeight = windowInfo.pixelHeight;
		}
		glConfig.uiViewportX = windowInfo.uiViewportX;
		glConfig.uiViewportY = windowInfo.uiViewportY;
		glConfig.uiViewportWidth = windowInfo.uiViewportWidth;
		glConfig.uiViewportHeight = windowInfo.uiViewportHeight;
	}

	if (!SDL3_EnsureGLContextCurrent("swap buffers")) {
		return;
	}
	if (!s_glWindowServices->SwapGLWindow()) {
		common->Printf("SDL3: failed to swap window buffers: %s\n", R_GLVideoError());
	}

#if defined(__ANDROID__) && defined(OPENQ4_RENDERER_GLES_MODULE)
	// The host app's touch overlay draws inside that swap; undo what it left
	// behind before the next frame delta-codes against it.
	RB_GLES_RestoreStateAfterOverlay();
#endif
}

void GLimp_ActivateContext(void) {
	(void)SDL3_EnsureGLContextCurrent("activate context");
}

bool GLimp_EnsureActiveContext(const char *operation) {
	return SDL3_EnsureGLContextCurrent(operation);
}

void GLimp_DeactivateContext(void) {
	if (!SDL3_EnsureGLContextCurrent("deactivate context")) {
		return;
	}

	glFinish();
	if (!s_glWindowServices->MakeGLContextCurrent(NULL)) {
		if (s_glWindowServices != NULL && s_glWindowServices->CountContextError != NULL) {
			s_glWindowServices->CountContextError();
		}
	}
}

#if defined(OPENQ4_SDL3_LINUX_HOST)
typedef void ( *openQ4GlewProcAddress_t ) (void);

extern "C" openQ4GlewProcAddress_t OpenQ4_GlewGetProcAddress(const unsigned char *name) {
	if (name == NULL || name[0] == '\0') {
		return NULL;
	}
	if (!SDL3_EnsureGLContextCurrent("GLEW proc lookup")) {
		return NULL;
	}

	return reinterpret_cast<openQ4GlewProcAddress_t>(
		s_glWindowServices->GetGLProcAddress(reinterpret_cast<const char *>(name)));
}
#endif

void *GLimp_ExtensionPointer(const char *name) {
	if (name == NULL || name[0] == '\0') {
		return NULL;
	}
	if (!SDL3_EnsureGLContextCurrent("extension proc lookup")) {
		return NULL;
	}

	void *proc = s_glWindowServices->GetGLProcAddress(name);
#if !defined(OPENQ4_SDL3_POSIX_HOST)
	if (!proc && qwglGetProcAddress) {
		proc = (void *)qwglGetProcAddress(name);
	}
#endif

	if (!proc) {
		common->Printf("Couldn't find proc address for: %s\n", name);
	}
	return proc;
}

#endif /* OPENQ4_SDL3_CONTEXT_IMPL */
