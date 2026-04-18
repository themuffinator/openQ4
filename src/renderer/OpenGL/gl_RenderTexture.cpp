// RenderTexture.cpp
//



#include "../tr_local.h"

static const GLuint INVALID_RENDER_TEXTURE_HANDLE = static_cast<GLuint>( -1 );

static GLenum R_CubeFaceTarget( int cubeFace ) {
	const int clampedFace = idMath::ClampInt( 0, 5, cubeFace );
	return GL_TEXTURE_CUBE_MAP_POSITIVE_X + clampedFace;
}

static bool R_IsRenderTextureHandleValid( GLuint handle ) {
	if ( !glConfig.isInitialized ) {
		return false;
	}
	if ( handle == INVALID_RENDER_TEXTURE_HANDLE ) {
		return false;
	}
	return glIsFramebuffer( handle ) == GL_TRUE;
}

static GLuint R_GetAttachmentHandle( idImage *image ) {
	return ( image != nullptr ) ? image->GetDeviceHandle() : INVALID_RENDER_TEXTURE_HANDLE;
}

static uint64_t R_GetAttachmentGeneration( idImage *image ) {
	return ( image != nullptr ) ? image->GetStorageGeneration() : 0;
}

/*
========================
idRenderTexture::idRenderTexture
========================
*/
idRenderTexture::idRenderTexture(idImage *colorImage, idImage *depthImage) {
	deviceHandle = INVALID_RENDER_TEXTURE_HANDLE;
	cachedDepthHandle = INVALID_RENDER_TEXTURE_HANDLE;
	cachedDepthGeneration = 0;
	if (colorImage != nullptr)
	{
		AddRenderImage(colorImage);
	}
	this->depthImage = depthImage;
}

/*
========================
idRenderTexture::~idRenderTexture
========================
*/
idRenderTexture::~idRenderTexture() {
	if ( deviceHandle != INVALID_RENDER_TEXTURE_HANDLE )
	{
		if ( R_IsRenderTextureHandleValid( deviceHandle ) ) {
			glDeleteFramebuffers( 1, &deviceHandle );
		}
		deviceHandle = INVALID_RENDER_TEXTURE_HANDLE;
	}
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
void idRenderTexture::EnsureDeviceHandle( void ) {
	// Scratch images can be purged/reallocated in place when runtime renderer settings
	// change. Rebuild the FBO attachments when that happens, even if the dimensions match.
	if ( R_IsRenderTextureHandleValid( deviceHandle ) && !NeedsAttachmentRefresh() ) {
		return;
	}

	if ( deviceHandle != INVALID_RENDER_TEXTURE_HANDLE && R_IsRenderTextureHandleValid( deviceHandle ) ) {
		glDeleteFramebuffers( 1, &deviceHandle );
		deviceHandle = INVALID_RENDER_TEXTURE_HANDLE;
	}

	InitRenderTexture();
}

/*
================
idRenderTexture::InitRenderTexture
================
*/
void idRenderTexture::InitRenderTexture(void) {
	if ( !glConfig.isInitialized ) {
		return;
	}

	if ( deviceHandle != INVALID_RENDER_TEXTURE_HANDLE ) {
		if ( R_IsRenderTextureHandleValid( deviceHandle ) ) {
			glDeleteFramebuffers( 1, &deviceHandle );
		}
		deviceHandle = INVALID_RENDER_TEXTURE_HANDLE;
	}

	glGenFramebuffers(1, &deviceHandle);
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

		if ( colorImages.Num() > 0 ) {
			GLenum DrawBuffers[5] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT4 };
			if (colorImages.Num() >= 5) {
				common->FatalError("InitRenderTextures: Too many render targets!");
			}
			glDrawBuffers(colorImages.Num(), &DrawBuffers[0]);
		} else {
			// Required for depth-only FBO completeness on strict drivers.
			glDrawBuffer( GL_NONE );
			glReadBuffer( GL_NONE );
		}
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


	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		common->FatalError("idRenderTexture::InitRenderTexture: Failed to create rendertexture!");
	}

	CaptureAttachmentHandles();
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/*
================
idRenderTexture::GetDeviceHandle
================
*/
GLuint idRenderTexture::GetDeviceHandle(void) {
	EnsureDeviceHandle();
	return deviceHandle;
}

/*
================
idRenderTexture::MakeCurrent
================
*/
void idRenderTexture::MakeCurrent(void) {
	EnsureDeviceHandle();
	glBindFramebuffer(GL_FRAMEBUFFER, deviceHandle);
}

/*
================
idRenderTexture::MakeCurrent
================
*/
void idRenderTexture::MakeCurrent( int cubeFace ) {
	EnsureDeviceHandle();
	glBindFramebuffer( GL_FRAMEBUFFER, deviceHandle );

	const GLenum faceTarget = R_CubeFaceTarget( cubeFace );
	if ( colorImages.Num() > 0 && colorImages[0]->GetOpts().textureType == TT_CUBIC ) {
		GLenum drawBuffers[5] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT4 };
		if ( colorImages.Num() >= 5 ) {
			common->FatalError( "idRenderTexture::MakeCurrent: Too many cubemap color targets!" );
		}

		for ( int i = 0; i < colorImages.Num(); i++ ) {
			if ( colorImages[i]->GetOpts().textureType != TT_CUBIC ) {
				common->FatalError( "idRenderTexture::MakeCurrent: Mixed cubemap/non-cubemap color targets are unsupported!" );
			}
			glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, faceTarget, colorImages[i]->GetDeviceHandle(), 0 );
		}
		glDrawBuffers( colorImages.Num(), drawBuffers );
		glReadBuffer( GL_COLOR_ATTACHMENT0 );
	} else {
		glDrawBuffer( GL_NONE );
		glReadBuffer( GL_NONE );
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

	if ( glCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE ) {
		common->FatalError( "idRenderTexture::MakeCurrent: Cubemap framebuffer face is incomplete!" );
	}
}

/*
================
idRenderTexture::BindNull
================
*/
void idRenderTexture::BindNull(void) {
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/*
================
idRenderTexture::Resize
================
*/
void idRenderTexture::Resize(int width, int height) {
	idImage *target = nullptr;

	if (colorImages.Num() > 0) {
		target = colorImages[0];
	}
	else {
		target = depthImage;
	}

	if (target->GetOpts().width == width && target->GetOpts().height == height) {
		EnsureDeviceHandle();
		return;
	}

	for(int i = 0; i < colorImages.Num(); i++) {
		colorImages[i]->Resize(width, height);
	}

	if (depthImage != nullptr) {
		depthImage->Resize(width, height);
	}

	InitRenderTexture();
}
