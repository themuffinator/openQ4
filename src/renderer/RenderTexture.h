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
#ifndef __RENDERTEXTURE_H__
#define __RENDERTEXTURE_H__

/*
================================================================================================

	Render Texture

================================================================================================
*/

/*
================================================
idRenderTexture holds both the color and depth images that are made
resident on the video hardware.
================================================
*/
class idRenderTexture {
public:
							idRenderTexture(idImage *colorImage, idImage *depthImage);
							~idRenderTexture();

	ID_INLINE int			GetWidth() const { return ( colorImages.Num() > 0 ) ? colorImages[0]->GetUploadWidth() : ( depthImage != NULL ? depthImage->GetUploadWidth() : 0 ); }
	ID_INLINE int			GetHeight() const { return ( colorImages.Num() > 0 ) ? colorImages[0]->GetUploadHeight() : ( depthImage != NULL ? depthImage->GetUploadHeight() : 0 ); }

	ID_INLINE idImage *		GetColorImage(int idx) const { return colorImages[idx]; }
	ID_INLINE idImage *		GetDepthImage() const { return depthImage; }

	int						GetNumColorImages() const { return colorImages.Num(); }

	bool					Resize( int width, int height );
	bool					EnsureDeviceHandle( void );

	bool					MakeCurrent( void );
	bool					MakeCurrent( int cubeFace );
	// Attaches every face of a cube target at once (a layered framebuffer):
	// the shader then chooses the face with gl_Layer. False when the layered
	// attachment is unavailable or incomplete; the target is then left bound
	// with face 0 attached, and per-face MakeCurrent still works.
	bool					MakeCurrentLayered( void );
	static void				BindNull(void);

	unsigned int					GetDeviceHandle(void);
	void					SetDebugLabel( const char *label );

	void					AddRenderImage(idImage *image);
	bool					InitRenderTexture(void);

	// Compatibility no-op for older shadow-target call sites. FBO incompleteness
	// is universally non-fatal now, including initial creation and resize.
	ID_INLINE void			SetAllowIncomplete( void ) {}
	ID_INLINE bool			IsKnownIncomplete( void ) const { return knownIncomplete; }
private:
	bool					HasCurrentDeviceHandle( void ) const;
	void					ReleaseDeviceHandle( void );
	bool					FailFramebuffer( unsigned int status, const char *operation );
	void					ReportFramebufferFailure( unsigned int status, const char *operation ) const;
	bool					NeedsAttachmentRefresh( void ) const;
	void					CaptureAttachmentHandles( void );
	void					ApplyDebugLabel( void ) const;

	idList<idImage *>	colorImages;
	idImage *			depthImage;
	unsigned int				deviceHandle;
	int					deviceHandleGeneration;
	unsigned int		validatedCubeFaces;
	bool				knownIncomplete = false;
	int					incompleteGeneration = -1;
	idStr				debugLabel;
	idList<unsigned int>		cachedColorHandles;
	unsigned int				cachedDepthHandle;
	idList<uint64_t>	cachedColorGenerations;
	uint64_t			cachedDepthGeneration;
};

/*
================================================
openQ4: the frame's default target.

Desktop frames draw to the window's framebuffer. A VR frame stands an eye or
virtual-screen render texture in for it, and every "unbind"
(idRenderTexture::BindNull) then binds that stand-in instead, so the backend's
"present to the back buffer" paths land in the headset's targets unchanged.
================================================
*/
void				R_SetDefaultRenderTarget( idRenderTexture *target );
idRenderTexture *	R_GetDefaultRenderTarget( void );
// GL framebuffer name of the default target (0 for the window)
unsigned int		R_DefaultFramebufferHandle( void );
// GL_BACK for the window, GL_COLOR_ATTACHMENT0 for a stand-in
unsigned int		R_DefaultColorBuffer( void );
// glDrawBuffer/glReadBuffer for the default target, which must be bound
void				R_SetDefaultDrawAndReadBuffers( void );
// The VR virtual screen is composited over the world as a premultiplied-alpha
// layer, so drawing into it keeps a coverage alpha (GL_State blends alpha
// separately) while the default target is bound.
void				R_SetDefaultRenderTargetPremultiplied( bool premultiplied );
bool				R_PremultipliedDefaultTargetBound( void );

#endif //!__RENDERTEXTURE_H__
