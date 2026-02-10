// Game_render.cpp
//


#include "Game_local.h"

idCVar g_renderCasUpscale("g_renderCasUpscale", "1", CVAR_BOOL, "jmarshall: toggles cas upscaling");

static const idMaterial* FindPostProcessMaterial( const char* primaryName, const char* fallbackName ) {
	const idMaterial* material = declManager->FindMaterial( primaryName, false );
	if ( material != NULL || fallbackName == NULL ) {
		return material;
	}
	return declManager->FindMaterial( fallbackName, false );
}

/*
========================
idGameLocal::ShutdownGameRenderSystem
========================
*/
void idGameLocal::ShutdownGameRenderSystem( void ) {
	for ( int i = 0; i < 2; i++ ) {
		if ( gameRender.postProcessRT[i] != NULL ) {
			renderSystem->DestroyRenderTexture( gameRender.postProcessRT[i] );
			gameRender.postProcessRT[i] = NULL;
		}
	}

	if ( gameRender.forwardRenderPassRT != NULL ) {
		renderSystem->DestroyRenderTexture( gameRender.forwardRenderPassRT );
		gameRender.forwardRenderPassRT = NULL;
	}
	if ( gameRender.forwardRenderPassResolvedRT != NULL ) {
		renderSystem->DestroyRenderTexture( gameRender.forwardRenderPassResolvedRT );
		gameRender.forwardRenderPassResolvedRT = NULL;
	}

	gameRender.noPostProcessMaterial = NULL;
	gameRender.casPostProcessMaterial = NULL;
	gameRender.blurPostProcessMaterial = NULL;
	gameRender.blackPostProcessMaterial = NULL;
	gameRender.resolvePostProcessMaterial = NULL;
	gameRender.smaaEdgePostProcessMaterial = NULL;
	gameRender.smaaBlendPostProcessMaterial = NULL;
	gameRender.postProcessAvailable = false;
	gameRender.smaaAvailable = false;
	gameRender.videoRestartCount = ( renderSystem != NULL ) ? renderSystem->GetVideoRestartCount() : 0;
}

/*
=======================================

Game Render

The engine renderer is designed to do two things, generate the geometry pass, and the shadow passes. The pipeline,
including post process, is now handled in the game code. This allows more granular control over how the final pixels,
are presented on screen based on whatever is going on in game.

=======================================
*/

/*
========================
idGameLocal::InitGameRenderSystem
========================
*/
void idGameLocal::InitGameRenderSystem(void) {
	ShutdownGameRenderSystem();

	if ( !renderSystem->IsOpenGLRunning() ) {
		return;
	}

	const int requestedMsaaSamples = Max( 0, cvarSystem->GetCVarInteger( "r_multiSamples" ) );

	{
		idImageOpts opts;
		opts.format = FMT_RGBA8;
		opts.colorFormat = CFM_DEFAULT;
		opts.numLevels = 1;
		opts.textureType = TT_2D;
		opts.isPersistant = true;
		opts.width = renderSystem->GetScreenWidth();
		opts.height = renderSystem->GetScreenHeight();
		opts.numMSAASamples = requestedMsaaSamples;

		idImage *albedoImage = renderSystem->CreateImage("_forwardRenderAlbedo", &opts, TF_LINEAR);
		idImage *emissiveImage = renderSystem->CreateImage("_forwardRenderEmissive", &opts, TF_LINEAR);

		opts.numMSAASamples = requestedMsaaSamples;
		opts.format = FMT_DEPTH_STENCIL;
		idImage *depthImage = renderSystem->CreateImage("_forwardRenderDepth", &opts, TF_LINEAR);

		gameRender.forwardRenderPassRT = renderSystem->CreateRenderTexture(albedoImage, depthImage, emissiveImage);
	}

	for(int i = 0; i < 2; i++)
	{
		idImageOpts opts;
		opts.format = FMT_RGBA8;
		opts.colorFormat = CFM_DEFAULT;
		opts.numLevels = 1;
		opts.textureType = TT_2D;
		opts.isPersistant = true;
		opts.width = renderSystem->GetScreenWidth();
		opts.height = renderSystem->GetScreenHeight();
		opts.numMSAASamples = 0;

		idImage* albedoImage = renderSystem->CreateImage(va("_postProcessAlbedo%d", i), &opts, TF_LINEAR);
		opts.format = FMT_DEPTH_STENCIL;
		idImage* depthImage = renderSystem->CreateImage(va("_postProcessDepth%d", i), &opts, TF_LINEAR);

		gameRender.postProcessRT[i] = renderSystem->CreateRenderTexture(albedoImage, depthImage, NULL);
	}

	{
		idImageOpts opts;
		opts.format = FMT_RGBA8;
		opts.colorFormat = CFM_DEFAULT;
		opts.numLevels = 1;
		opts.textureType = TT_2D;
		opts.isPersistant = true;
		opts.width = renderSystem->GetScreenWidth();
		opts.height = renderSystem->GetScreenHeight();
		opts.numMSAASamples = 0;

		idImage *albedoImage = renderSystem->CreateImage("_forwardRenderResolvedAlbedo", &opts, TF_LINEAR);
		idImage *emissiveImage = renderSystem->CreateImage("_forwardRenderResolvedEmissive", &opts, TF_LINEAR);
		opts.format = FMT_DEPTH;
		idImage *depthImage = renderSystem->CreateImage("_forwardRenderResolvedDepth", &opts, TF_LINEAR);

		gameRender.forwardRenderPassResolvedRT = renderSystem->CreateRenderTexture(albedoImage, depthImage, emissiveImage);
	}

	gameRender.blackPostProcessMaterial = FindPostProcessMaterial( "postprocess/black", "postprocess/openq4_black" );
	gameRender.noPostProcessMaterial = FindPostProcessMaterial( "postprocess/nopostprocess", "postprocess/openq4_nopostprocess" );
	gameRender.casPostProcessMaterial = FindPostProcessMaterial( "postprocess/casupscale", "postprocess/openq4_casupscale" );
	gameRender.blurPostProcessMaterial = FindPostProcessMaterial( "postprocess/blur", "postprocess/openq4_blur" );
	gameRender.resolvePostProcessMaterial = FindPostProcessMaterial( "postprocess/resolvepostprocess", "postprocess/openq4_resolvepostprocess" );
	gameRender.smaaEdgePostProcessMaterial = FindPostProcessMaterial( "postprocess/smaa_edge", "postprocess/openq4_smaa_edge" );
	gameRender.smaaBlendPostProcessMaterial = FindPostProcessMaterial( "postprocess/smaa_blend", "postprocess/openq4_smaa_blend" );
	gameRender.postProcessAvailable = (gameRender.noPostProcessMaterial != NULL) &&
		(gameRender.resolvePostProcessMaterial != NULL);
	gameRender.smaaAvailable = (gameRender.smaaEdgePostProcessMaterial != NULL) &&
		(gameRender.smaaBlendPostProcessMaterial != NULL);
	if (!gameRender.postProcessAvailable) {
		common->Warning("Postprocess materials missing; falling back to direct render.");
	}

	if ( cvarSystem->GetCVarInteger( "r_postAA" ) == 1 && !gameRender.smaaAvailable ) {
		common->Warning( "SMAA is enabled (r_postAA = 1), but SMAA materials are missing. Falling back to no post AA." );
	}

	if ( requestedMsaaSamples > 0 ) {
		common->Printf( "MSAA requested %d samples\n", requestedMsaaSamples );
	}
}

/*
========================
idGameLocal::ResizeRenderTextures
========================
*/
void idGameLocal::ResizeRenderTextures(int width, int height) {
	// Resize all of the different render textures.
	if ( gameRender.forwardRenderPassRT != NULL ) {
		renderSystem->ResizeRenderTexture( gameRender.forwardRenderPassRT, width, height );
	}
	if ( gameRender.forwardRenderPassResolvedRT != NULL ) {
		renderSystem->ResizeRenderTexture( gameRender.forwardRenderPassResolvedRT, width, height );
	}
	for ( int i = 0; i < 2; i++ ) {
		if ( gameRender.postProcessRT[i] != NULL ) {
			renderSystem->ResizeRenderTexture( gameRender.postProcessRT[i], width, height );
		}
	}
}

/*
====================
idGameLocal::RenderScene
====================
*/
void idGameLocal::RenderScene(const renderView_t *view, idRenderWorld *renderWorld, idCamera* portalSky) {
	const int currentVideoRestartCount = renderSystem->GetVideoRestartCount();
	if ( gameRender.videoRestartCount != currentVideoRestartCount ) {
		common->Printf( "Reinitializing game render targets after vid_restart (%d -> %d)\n",
			gameRender.videoRestartCount, currentVideoRestartCount );
		InitGameRenderSystem();
	}

	if (!gameRender.postProcessAvailable || gameRender.forwardRenderPassRT == NULL ||
		gameRender.forwardRenderPassResolvedRT == NULL || gameRender.postProcessRT[0] == NULL ||
		gameRender.resolvePostProcessMaterial == NULL) {
		// Fallback for stock Quake 4 assets: render directly to the backbuffer.
		renderSystem->BindRenderTexture( nullptr, nullptr );
		if (portalSky) {
			renderView_t portalSkyView = *view;
			portalSky->GetViewParms(&portalSkyView);
			gameRenderWorld->RenderScene(&portalSkyView);
		}
		renderWorld->RenderScene(view);
		return;
	}
	// Minimum render is used for screen captures(such as envcapture) calls, caller is responsible for all rendertarget setup.
	//if (view->minimumRender)
	//{
	//	RenderSky(view);
	//	if (view->cubeMapTargetImage)
	//	{
	//		renderView_t worldRefDef = *view;
	//		worldRefDef.cubeMapClearBuffer = false;
	//		renderWorld->RenderScene(&worldRefDef);
	//	}
	//	else
	//	{
	//		renderWorld->RenderScene(view);
	//	}
	//
	//	return;
	//}

	// Make sure all of our render textures are the right dimensions for this render.
	ResizeRenderTextures(renderSystem->GetScreenWidth(), renderSystem->GetScreenHeight());

	// Render the scene to the forward render pass rendertexture.
	renderSystem->BindRenderTexture(gameRender.forwardRenderPassRT, nullptr);
	{
		// Clear the color/depth buffers
		renderSystem->ClearRenderTarget(true, true, 1.0f, 0.0f, 0.0f, 0.0f);
	
		// Render our sky first.
		if (portalSky) {
			renderView_t portalSkyView = *view;
			portalSky->GetViewParms(&portalSkyView);
			gameRenderWorld->RenderScene(&portalSkyView);
		}
	
		// Render the current world.
		renderWorld->RenderScene(view);
	}
	renderSystem->BindRenderTexture(nullptr, nullptr);

	// Resolve our MSAA buffer.
	renderSystem->ResolveMSAA(
		gameRender.forwardRenderPassRT,
		gameRender.forwardRenderPassResolvedRT,
		cvarSystem->GetCVarBool( "r_msaaResolveDepth" ) );

	// Resolve pass writes scene color to the post-process source buffer.
	renderSystem->BindRenderTexture(gameRender.postProcessRT[0], nullptr);
		renderSystem->ClearRenderTarget( true, true, 1.0f, 0.0f, 0.0f, 0.0f );
		renderSystem->DrawStretchPic(0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT, 0.0f, 1.0f, 1.0f, 0.0f, gameRender.resolvePostProcessMaterial);
	renderSystem->BindRenderTexture( nullptr, nullptr );

	const bool useSMAA = ( cvarSystem->GetCVarInteger( "r_postAA" ) == 1 ) &&
		gameRender.smaaAvailable &&
		gameRender.postProcessRT[1] != NULL;
	if ( useSMAA ) {
		// Pass 1: edge detection into _postProcessAlbedo1.
		renderSystem->BindRenderTexture( gameRender.postProcessRT[1], nullptr );
		renderSystem->ClearRenderTarget( true, true, 1.0f, 0.0f, 0.0f, 0.0f );
		renderSystem->DrawStretchPic(
			0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT,
			0.0f, 1.0f, 1.0f, 0.0f,
			gameRender.smaaEdgePostProcessMaterial );

		// Pass 2: neighborhood blending back into _postProcessAlbedo0.
		renderSystem->BindRenderTexture( gameRender.postProcessRT[0], nullptr );
		renderSystem->ClearRenderTarget( true, true, 1.0f, 0.0f, 0.0f, 0.0f );
		renderSystem->DrawStretchPic(
			0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT,
			0.0f, 1.0f, 1.0f, 0.0f,
			gameRender.smaaBlendPostProcessMaterial );
		renderSystem->BindRenderTexture( nullptr, nullptr );
	}

	const bool blurEnabled = IsSpecialEffectEnabled( SPECIAL_EFFECT_BLUR ) &&
		( gameRender.blurPostProcessMaterial != NULL );

	const idMaterial* finalMaterial = gameRender.noPostProcessMaterial;
	if ( blurEnabled ) {
		finalMaterial = gameRender.blurPostProcessMaterial;
	} else if ( g_renderCasUpscale.GetBool() && gameRender.casPostProcessMaterial != NULL ) {
		finalMaterial = gameRender.casPostProcessMaterial;
	}
	if ( finalMaterial != NULL ) {
		// SS_POST_PROCESS stages use depth testing; reset backbuffer depth each frame
		// so final full-screen composition is deterministic across drivers/devices.
		renderSystem->ClearRenderTarget( false, true, 1.0f, 0.0f, 0.0f, 0.0f );
		renderSystem->DrawStretchPic(
			0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT,
			0.0f, 1.0f, 1.0f, 0.0f,
			finalMaterial );
	}

	// Copy everything to _currentRender
	renderSystem->CaptureRenderToImage("_currentRender");
}
