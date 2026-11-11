// RenderTexture.cpp
//



#include "../tr_local.h"
#include "../GLDebugScope.h"

static const GLuint INVALID_RENDER_TEXTURE_HANDLE = static_cast<GLuint>( -1 );

// openQ4: the frame's default target (RenderTexture.h). NULL and 0 mean the
// window; a VR frame stands its eye or virtual-screen target in for it.
static idRenderTexture *	r_defaultRenderTarget = NULL;
static GLuint				r_defaultFramebuffer = 0;
static bool					r_defaultTargetPremultiplied = false;
static bool					r_defaultFramebufferBound = true;

// GL_State caches blend factors by state bits, so a change in how alpha is
// blended has to force its next call to reissue them.
static void R_TrackDefaultFramebufferBound( bool bound ) {
	const bool wasPremultiplied = R_PremultipliedDefaultTargetBound();
	r_defaultFramebufferBound = bound;
	if ( R_PremultipliedDefaultTargetBound() != wasPremultiplied ) {
		backEnd.glState.forceGlState = true;
	}
}

static GLenum R_CubeFaceTarget( int cubeFace ) {
	const int clampedFace = idMath::ClampInt( 0, 5, cubeFace );
	return GL_TEXTURE_CUBE_MAP_POSITIVE_X + clampedFace;
}

static bool R_IsRenderTextureHandleCurrent( GLuint handle, int handleGeneration ) {
	if ( !glConfig.isInitialized ) {
		return false;
	}
	// Never query a name from a destroyed context. GL object namespaces are
	// recycled, so the same numeric name may now identify an unrelated FBO.
	if ( handleGeneration != tr.glContextGeneration ) {
		return false;
	}
	return handle != 0 && handle != INVALID_RENDER_TEXTURE_HANDLE;
}

static GLuint R_GetAttachmentHandle( idImage *image ) {
	return ( image != nullptr ) ? image->GetDeviceHandle() : INVALID_RENDER_TEXTURE_HANDLE;
}

static void R_SetRenderTextureDrawBuffers( int colorImageCount ) {
	if ( colorImageCount > 0 ) {
		GLenum drawBuffers[5] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT4 };
		if ( colorImageCount > static_cast<int>( sizeof( drawBuffers ) / sizeof( drawBuffers[0] ) ) ) {
			common->FatalError( "idRenderTexture: Too many render targets!" );
		}
		glDrawBuffers( colorImageCount, drawBuffers );
		glReadBuffer( GL_COLOR_ATTACHMENT0 );
	} else {
		glDrawBuffer( GL_NONE );
		glReadBuffer( GL_NONE );
	}
}

static uint64_t R_GetAttachmentGeneration( idImage *image ) {
	return ( image != nullptr ) ? image->GetStorageGeneration() : 0;
}

static const char* R_FramebufferStatusName( GLenum status ) {
	switch ( status ) {
		case GL_FRAMEBUFFER_COMPLETE:
			return "GL_FRAMEBUFFER_COMPLETE";
		case GL_FRAMEBUFFER_UNDEFINED:
			return "GL_FRAMEBUFFER_UNDEFINED";
		case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
			return "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT";
		case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
			return "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT";
		case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
			return "GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER";
		case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
			return "GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER";
		case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT:
			return "GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT";
		case GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT:
			return "GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT";
		case GL_FRAMEBUFFER_UNSUPPORTED:
			return "GL_FRAMEBUFFER_UNSUPPORTED";
		case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
			return "GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE";
		case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS:
			return "GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS";
		case GL_FRAMEBUFFER_INCOMPLETE_LAYER_COUNT_ARB:
			return "GL_FRAMEBUFFER_INCOMPLETE_LAYER_COUNT_ARB";
		default:
			return "GL_FRAMEBUFFER_STATUS_UNKNOWN";
	}
}

static const char* R_TextureFormatName( textureFormat_t format ) {
	switch ( format ) {
		case FMT_RGBA8: return "RGBA8";
		case FMT_SRGBA8: return "SRGBA8";
		case FMT_XRGB8: return "XRGB8";
		case FMT_ALPHA: return "ALPHA";
		case FMT_L8A8: return "L8A8";
		case FMT_LUM8: return "LUM8";
		case FMT_INT8: return "INT8";
		case FMT_DXT1: return "DXT1";
		case FMT_DXT5: return "DXT5";
		case FMT_DEPTH: return "DEPTH";
		case FMT_X16: return "X16";
		case FMT_Y16_X16: return "Y16_X16";
		case FMT_RGB565: return "RGB565";
		case FMT_DEPTH_STENCIL: return "DEPTH_STENCIL";
		case FMT_RGBA16F: return "RGBA16F";
		case FMT_BC7: return "BC7";
		default: return "UNKNOWN";
	}
}

static const char* R_TextureTypeName( textureType_t textureType ) {
	switch ( textureType ) {
		case TT_2D: return "2D";
		case TT_CUBIC: return "CUBIC";
		default: return "DISABLED";
	}
}

static void R_ReportFramebufferAttachment( const char* role, int index, idImage* image ) {
	if ( image == NULL ) {
		common->Warning( "  %s[%d]: <none>", role, index );
		return;
	}

	const idImageOpts& opts = image->GetOpts();
	common->Warning(
		"  %s[%d]: name='%s' handle=%u loaded=%d type=%s format=%s(%d) size=%dx%d samples=%d generation=%llu",
		role,
		index,
		image->GetName(),
		static_cast<unsigned int>( image->GetDeviceHandle() ),
		image->IsLoaded() ? 1 : 0,
		R_TextureTypeName( opts.textureType ),
		R_TextureFormatName( opts.format ),
		static_cast<int>( opts.format ),
		opts.width,
		opts.height,
		opts.numMSAASamples,
		static_cast<unsigned long long>( image->GetStorageGeneration() ) );
}

/*
========================
idRenderTexture::idRenderTexture
========================
*/
idRenderTexture::idRenderTexture(idImage *colorImage, idImage *depthImage) {
	deviceHandle = INVALID_RENDER_TEXTURE_HANDLE;
	deviceHandleGeneration = -1;
	validatedCubeFaces = 0;
	cachedDepthHandle = INVALID_RENDER_TEXTURE_HANDLE;
	cachedDepthGeneration = 0;
	if (colorImage != nullptr)
	{
		AddRenderImage(colorImage);
	}
	this->depthImage = depthImage;
}

/*
================
idRenderTexture::SetDebugLabel
================
*/
void idRenderTexture::SetDebugLabel( const char *label ) {
	debugLabel = ( label != NULL ) ? label : "";
	if ( glConfig.isInitialized && debugLabel.Length() > 0 ) {
		EnsureDeviceHandle();
		ApplyDebugLabel();
	}
}

/*
================
idRenderTexture::ApplyDebugLabel
================
*/
void idRenderTexture::ApplyDebugLabel( void ) const {
	if ( debugLabel.Length() <= 0 || !HasCurrentDeviceHandle() ) {
		return;
	}

	R_GLDebug_LabelFramebuffer( deviceHandle, debugLabel.c_str() );

	for ( int i = 0; i < colorImages.Num(); i++ ) {
		if ( colorImages[i] == NULL || !colorImages[i]->IsLoaded() ) {
			continue;
		}
		char label[256];
		idStr::snPrintf(
			label,
			sizeof( label ),
			"%s color%d %s",
			debugLabel.c_str(),
			i,
			colorImages[i]->GetName() );
		R_GLDebug_LabelTexture( colorImages[i]->GetDeviceHandle(), label );
	}

	if ( depthImage != NULL && depthImage->IsLoaded() ) {
		char label[256];
		idStr::snPrintf(
			label,
			sizeof( label ),
			"%s depth %s",
			debugLabel.c_str(),
			depthImage->GetName() );
		R_GLDebug_LabelTexture( depthImage->GetDeviceHandle(), label );
	}
}

/*
========================
idRenderTexture::~idRenderTexture
========================
*/
idRenderTexture::~idRenderTexture() {
	if ( r_defaultRenderTarget == this ) {
		r_defaultRenderTarget = NULL;
		r_defaultFramebuffer = 0;
	}
	ReleaseDeviceHandle();
}

/*
================
idRenderTexture::HasCurrentDeviceHandle
================
*/
bool idRenderTexture::HasCurrentDeviceHandle( void ) const {
	// The render texture owns this name.  Generation validation prevents stale
	// context aliases without putting a synchronous glIsFramebuffer query on
	// every bind and handle lookup.
	return !knownIncomplete && R_IsRenderTextureHandleCurrent( deviceHandle, deviceHandleGeneration );
}

/*
================
idRenderTexture::ReleaseDeviceHandle

Stale-generation names are forgotten without querying or deleting them. The
old context already owned their lifetime, and the numeric name may alias an
unrelated object in the current context.
================
*/
void idRenderTexture::ReleaseDeviceHandle( void ) {
	if ( glConfig.isInitialized &&
		deviceHandleGeneration == tr.glContextGeneration &&
		deviceHandle != 0 && deviceHandle != INVALID_RENDER_TEXTURE_HANDLE &&
		glDeleteFramebuffers != NULL ) {
		glDeleteFramebuffers( 1, &deviceHandle );
	}
	deviceHandle = INVALID_RENDER_TEXTURE_HANDLE;
	deviceHandleGeneration = -1;
	validatedCubeFaces = 0;
	if ( r_defaultRenderTarget == this ) {
		// the stand-in keeps its role; InitRenderTexture publishes its new name
		r_defaultFramebuffer = 0;
	}
}

/*
================
idRenderTexture::ReportFramebufferFailure
================
*/
void idRenderTexture::ReportFramebufferFailure( GLenum status, const char* operation ) const {
	const GLenum glError = glGetError();
	common->Warning(
		"idRenderTexture::%s: framebuffer '%s' failed with %s (0x%04x), GL error 0x%04x; target disabled so the caller can fall back",
		operation,
		debugLabel.Length() > 0 ? debugLabel.c_str() : "<unlabeled>",
		R_FramebufferStatusName( status ),
		static_cast<unsigned int>( status ),
		static_cast<unsigned int>( glError ) );
	common->Warning(
		"  GL context: vendor='%s' renderer='%s' version='%s' actual=%d.%d %s request='%s' generation=%d",
		glConfig.vendor_string != NULL ? glConfig.vendor_string : "unknown",
		glConfig.renderer_string != NULL ? glConfig.renderer_string : "unknown",
		glConfig.version_string != NULL ? glConfig.version_string : "unknown",
		glConfig.backendCaps.glMajor,
		glConfig.backendCaps.glMinor,
		RendererContextProfile_Name( glConfig.backendCaps.profile ),
		glConfig.contextRequest.label,
		tr.glContextGeneration );
	for ( int i = 0; i < colorImages.Num(); i++ ) {
		R_ReportFramebufferAttachment( "color", i, colorImages[i] );
	}
	if ( depthImage != NULL ) {
		R_ReportFramebufferAttachment( "depth", 0, depthImage );
	}
}

/*
================
idRenderTexture::FailFramebuffer
================
*/
bool idRenderTexture::FailFramebuffer( GLenum status, const char* operation ) {
	ReportFramebufferFailure( status, operation );
	CaptureAttachmentHandles();
	if ( r_defaultRenderTarget == this ) {
		// a broken stand-in hands the frame back to the window
		r_defaultRenderTarget = NULL;
		r_defaultFramebuffer = 0;
	}
	if ( glConfig.isInitialized && glBindFramebuffer != NULL ) {
		glBindFramebuffer( GL_FRAMEBUFFER, r_defaultFramebuffer );
	}
	ReleaseDeviceHandle();
	knownIncomplete = true;
	incompleteGeneration = tr.glContextGeneration;
	return false;
}
/*
================
idRenderTexture::AddRenderImage
================
*/
void idRenderTexture::AddRenderImage(idImage *image) {
	if (deviceHandle != INVALID_RENDER_TEXTURE_HANDLE) {
		common->FatalError("idRenderTexture::AddRenderImage: Can't add render image after FBO has been created!");
	}

	colorImages.Append(image);
	cachedColorHandles.Append( INVALID_RENDER_TEXTURE_HANDLE );
	cachedColorGenerations.Append( 0 );
}

/*
================
idRenderTexture::NeedsAttachmentRefresh
================
*/
bool idRenderTexture::NeedsAttachmentRefresh( void ) const {
	if ( cachedColorHandles.Num() != colorImages.Num() || cachedColorGenerations.Num() != colorImages.Num() ) {
		return true;
	}

	for ( int i = 0; i < colorImages.Num(); i++ ) {
		if ( cachedColorHandles[i] != R_GetAttachmentHandle( colorImages[i] ) ||
			cachedColorGenerations[i] != R_GetAttachmentGeneration( colorImages[i] ) ) {
			return true;
		}
	}

	return cachedDepthHandle != R_GetAttachmentHandle( depthImage ) ||
		cachedDepthGeneration != R_GetAttachmentGeneration( depthImage );
}

/*
================
idRenderTexture::CaptureAttachmentHandles
================
*/
void idRenderTexture::CaptureAttachmentHandles( void ) {
	cachedColorHandles.SetNum( colorImages.Num() );
	cachedColorGenerations.SetNum( colorImages.Num() );
	for ( int i = 0; i < colorImages.Num(); i++ ) {
		cachedColorHandles[i] = R_GetAttachmentHandle( colorImages[i] );
		cachedColorGenerations[i] = R_GetAttachmentGeneration( colorImages[i] );
	}
	cachedDepthHandle = R_GetAttachmentHandle( depthImage );
	cachedDepthGeneration = R_GetAttachmentGeneration( depthImage );
}

/*
================
idRenderTexture::EnsureDeviceHandle
================
*/
bool idRenderTexture::EnsureDeviceHandle( void ) {
	if ( knownIncomplete && incompleteGeneration != tr.glContextGeneration ) {
		knownIncomplete = false;
		incompleteGeneration = -1;
	}

	if ( deviceHandle != INVALID_RENDER_TEXTURE_HANDLE && deviceHandleGeneration != tr.glContextGeneration ) {
		// Context loss invalidated the object. Do not call glIsFramebuffer or
		// glDeleteFramebuffers on the old numeric name in the new namespace.
		deviceHandle = INVALID_RENDER_TEXTURE_HANDLE;
		deviceHandleGeneration = -1;
	}

	// Scratch images can be purged/reallocated in place when runtime renderer settings
	// change. Rebuild the FBO attachments when that happens, even if the dimensions match.
	if ( HasCurrentDeviceHandle() && !NeedsAttachmentRefresh() ) {
		return true;
	}

	// Do not recreate and re-report the same rejected attachment set every bind.
	// A context change or image reallocation clears this latch and permits a retry.
	if ( knownIncomplete && incompleteGeneration == tr.glContextGeneration && !NeedsAttachmentRefresh() ) {
		return false;
	}

	knownIncomplete = false;
	incompleteGeneration = -1;
	ReleaseDeviceHandle();
	return InitRenderTexture();
}

/*
================
idRenderTexture::InitRenderTexture
================
*/
bool idRenderTexture::InitRenderTexture(void) {
	if ( !glConfig.isInitialized ) {
		return false;
	}
	if ( glGenFramebuffers == NULL || glBindFramebuffer == NULL || glCheckFramebufferStatus == NULL ) {
		if ( !knownIncomplete ) {
			common->Warning( "idRenderTexture::InitRenderTexture: framebuffer entry points are unavailable for '%s'", debugLabel.c_str() );
		}
		knownIncomplete = true;
		incompleteGeneration = tr.glContextGeneration;
		CaptureAttachmentHandles();
		ReleaseDeviceHandle();
		return false;
	}

	ReleaseDeviceHandle();

	GLuint generatedHandle = 0;
	glGenFramebuffers( 1, &generatedHandle );
	if ( generatedHandle == 0 || generatedHandle == INVALID_RENDER_TEXTURE_HANDLE ) {
		if ( generatedHandle == INVALID_RENDER_TEXTURE_HANDLE && glDeleteFramebuffers != NULL ) {
			glDeleteFramebuffers( 1, &generatedHandle );
		}
		if ( !knownIncomplete ) {
			common->Warning( "idRenderTexture::InitRenderTexture: failed to allocate framebuffer '%s'", debugLabel.c_str() );
		}
		knownIncomplete = true;
		incompleteGeneration = tr.glContextGeneration;
		CaptureAttachmentHandles();
		return false;
	}
	deviceHandle = generatedHandle;
	deviceHandleGeneration = tr.glContextGeneration;
	glBindFramebuffer(GL_FRAMEBUFFER, deviceHandle);

	bool isTexture3D = false;
	if ((colorImages.Num() > 0 && colorImages[0]->GetOpts().textureType == TT_CUBIC) || ((depthImage != nullptr) && depthImage->GetOpts().textureType == TT_CUBIC))
	{
		isTexture3D = true;
	}
	
	if (!isTexture3D)
	{
		for (int i = 0; i < colorImages.Num(); i++) {
			if (colorImages[i]->GetOpts().numMSAASamples == 0)
			{
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, colorImages[i]->GetDeviceHandle(), 0);
			}
			else
			{
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D_MULTISAMPLE, colorImages[i]->GetDeviceHandle(), 0);
			}
		}

		if (depthImage != nullptr) {
			if (depthImage->GetOpts().numMSAASamples == 0)
			{
				if (depthImage->GetOpts().format == FMT_DEPTH) {
					glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthImage->GetDeviceHandle(), 0);
				}
				else if (depthImage->GetOpts().format == FMT_DEPTH_STENCIL) {
					glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, depthImage->GetDeviceHandle(), 0);
				}
				else {
					common->FatalError("idRenderTexture::InitRenderTexture: Unknown depth buffer format!");
				}
			}
			else
			{
				if (depthImage->GetOpts().format == FMT_DEPTH) {
					glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D_MULTISAMPLE, depthImage->GetDeviceHandle(), 0);
				}
				else if (depthImage->GetOpts().format == FMT_DEPTH_STENCIL) {
					glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D_MULTISAMPLE, depthImage->GetDeviceHandle(), 0);
				}
				else {
					common->FatalError("idRenderTexture::InitRenderTexture: Unknown depth buffer format!");
				}
			}
		}

		R_SetRenderTextureDrawBuffers( colorImages.Num() );
	}
	else
	{
		if (colorImages.Num() > 0)
		{
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X, colorImages[0]->GetDeviceHandle(), 0);
			glDrawBuffer( GL_COLOR_ATTACHMENT0 );
			glReadBuffer( GL_COLOR_ATTACHMENT0 );
		}
		else
		{
			glDrawBuffer( GL_NONE );
			glReadBuffer( GL_NONE );
		}

		if (depthImage != nullptr) {
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_CUBE_MAP_POSITIVE_X, depthImage->GetDeviceHandle(), 0);
		}
	}


	const GLenum framebufferStatus = glCheckFramebufferStatus( GL_FRAMEBUFFER );
	if ( framebufferStatus != GL_FRAMEBUFFER_COMPLETE ) {
		return FailFramebuffer( framebufferStatus, "InitRenderTexture" );
	}

	knownIncomplete = false;
	incompleteGeneration = -1;
	if ( isTexture3D ) {
		validatedCubeFaces |= 1u;
	}

	CaptureAttachmentHandles();
	ApplyDebugLabel();
	if ( r_defaultRenderTarget == this ) {
		r_defaultFramebuffer = deviceHandle;
	}
	glBindFramebuffer( GL_FRAMEBUFFER, r_defaultFramebuffer );
	return true;
}

/*
================
idRenderTexture::GetDeviceHandle
================
*/
GLuint idRenderTexture::GetDeviceHandle(void) {
	EnsureDeviceHandle();
	return HasCurrentDeviceHandle() ? deviceHandle : 0;
}

/*
================
idRenderTexture::MakeCurrent
================
*/
bool idRenderTexture::MakeCurrent(void) {
	if ( !glConfig.isInitialized || glBindFramebuffer == NULL ) {
		return false;
	}
	if ( !EnsureDeviceHandle() || !HasCurrentDeviceHandle() ) {
		BindNull();
		return false;
	}
	glBindFramebuffer(GL_FRAMEBUFFER, deviceHandle);
	R_TrackDefaultFramebufferBound( false );
	R_SetRenderTextureDrawBuffers( colorImages.Num() );
	return true;
}

/*
================
idRenderTexture::MakeCurrent
================
*/
bool idRenderTexture::MakeCurrent( int cubeFace ) {
	if ( !glConfig.isInitialized || glBindFramebuffer == NULL ) {
		return false;
	}
	if ( !EnsureDeviceHandle() || !HasCurrentDeviceHandle() ) {
		BindNull();
		return false;
	}
	glBindFramebuffer( GL_FRAMEBUFFER, deviceHandle );
	R_TrackDefaultFramebufferBound( false );

	const int clampedCubeFace = idMath::ClampInt( 0, 5, cubeFace );
	const GLenum faceTarget = R_CubeFaceTarget( clampedCubeFace );
	if ( colorImages.Num() > 0 && colorImages[0]->GetOpts().textureType == TT_CUBIC ) {
		for ( int i = 0; i < colorImages.Num(); i++ ) {
			if ( colorImages[i]->GetOpts().textureType != TT_CUBIC ) {
				common->FatalError( "idRenderTexture::MakeCurrent: Mixed cubemap/non-cubemap color targets are unsupported!" );
			}
			glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, faceTarget, colorImages[i]->GetDeviceHandle(), 0 );
		}
		R_SetRenderTextureDrawBuffers( colorImages.Num() );
	} else {
		R_SetRenderTextureDrawBuffers( 0 );
	}

	if ( depthImage != nullptr && depthImage->GetOpts().textureType == TT_CUBIC ) {
		if ( depthImage->GetOpts().format == FMT_DEPTH ) {
			glFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, faceTarget, depthImage->GetDeviceHandle(), 0 );
		} else if ( depthImage->GetOpts().format == FMT_DEPTH_STENCIL ) {
			glFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, faceTarget, depthImage->GetDeviceHandle(), 0 );
		} else {
			common->FatalError( "idRenderTexture::MakeCurrent: Unknown depth buffer format for cubemap target!" );
		}
	}

	const unsigned int faceBit = 1u << clampedCubeFace;
	if ( ( validatedCubeFaces & faceBit ) != 0 ) {
		knownIncomplete = false;
		return true;
	}

	const GLenum framebufferStatus = glCheckFramebufferStatus( GL_FRAMEBUFFER );
	if ( framebufferStatus != GL_FRAMEBUFFER_COMPLETE ) {
		return FailFramebuffer( framebufferStatus, "MakeCurrent(cubemap)" );
	}

	knownIncomplete = false;
	incompleteGeneration = -1;
	validatedCubeFaces |= faceBit;
	return true;
}

/*
================
idRenderTexture::BindNull
================
*/
void idRenderTexture::BindNull(void) {
	if ( glConfig.isInitialized && glBindFramebuffer != NULL ) {
		glBindFramebuffer( GL_FRAMEBUFFER, r_defaultFramebuffer );
		R_TrackDefaultFramebufferBound( true );
	}
}

/*
================
R_SetDefaultRenderTarget
================
*/
void R_SetDefaultRenderTarget( idRenderTexture *target ) {
	R_SetDefaultRenderTargetPremultiplied( false );
	r_defaultRenderTarget = NULL;
	r_defaultFramebuffer = 0;
	if ( target == NULL ) {
		return;
	}
	const GLuint handle = target->GetDeviceHandle();
	if ( handle == 0 || handle == INVALID_RENDER_TEXTURE_HANDLE ) {
		// an unusable stand-in leaves the window as the target rather than
		// drawing the frame nowhere
		return;
	}
	r_defaultRenderTarget = target;
	r_defaultFramebuffer = handle;
}

idRenderTexture *R_GetDefaultRenderTarget( void ) {
	return r_defaultRenderTarget;
}

unsigned int R_DefaultFramebufferHandle( void ) {
	return r_defaultFramebuffer;
}

int R_DefaultRenderTargetSamples( void ) {
	const idImage *color = r_defaultRenderTarget != NULL && r_defaultRenderTarget->GetNumColorImages() > 0
		? r_defaultRenderTarget->GetColorImage( 0 ) : NULL;
	const int samples = color != NULL ? color->GetOpts().numMSAASamples : 0;
	return samples > 1 ? samples : 0;
}

unsigned int R_DefaultColorBuffer( void ) {
	return r_defaultFramebuffer != 0 ? GL_COLOR_ATTACHMENT0 : GL_BACK;
}

void R_SetDefaultDrawAndReadBuffers( void ) {
	const GLenum buffer = R_DefaultColorBuffer();
	glDrawBuffer( buffer );
	glReadBuffer( buffer );
}

void R_SetDefaultRenderTargetPremultiplied( bool premultiplied ) {
	const bool wasPremultiplied = R_PremultipliedDefaultTargetBound();
	r_defaultTargetPremultiplied = premultiplied;
	if ( R_PremultipliedDefaultTargetBound() != wasPremultiplied ) {
		backEnd.glState.forceGlState = true;
	}
}

bool R_PremultipliedDefaultTargetBound( void ) {
	return r_defaultTargetPremultiplied && r_defaultFramebufferBound && r_defaultFramebuffer != 0;
}

/*
================
idRenderTexture::Resize
================
*/
bool idRenderTexture::Resize(int width, int height) {
	idImage *target = nullptr;

	if (colorImages.Num() > 0) {
		target = colorImages[0];
	}
	else {
		target = depthImage;
	}
	if ( target == nullptr ) {
		common->Warning( "idRenderTexture::Resize: render texture has no attachments" );
		return false;
	}

	if (target->GetOpts().width == width && target->GetOpts().height == height) {
		return EnsureDeviceHandle();
	}

	for(int i = 0; i < colorImages.Num(); i++) {
		colorImages[i]->Resize(width, height);
	}

	if (depthImage != nullptr) {
		depthImage->Resize(width, height);
	}

	return InitRenderTexture();
}
