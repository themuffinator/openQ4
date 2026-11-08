/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).  

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/


typedef struct {
	const idMaterial	*material;
	float				color[4];
	int					firstVert;
	int					numVerts;
	int					firstIndex;
	int					numIndexes;
} guiModelSurface_t;

class idGuiModel;

// The model a 2D batch flushed now belongs to in a recording render demo:
// the frame's screen 2D, a retained layer's content, or NULL (GuiModel.cpp).
idGuiModel *R_DemoGuiTarget();

class idGuiModel {
public:
	idGuiModel();

	void	Clear();

	// previous: the model the stream's last DC_GUI_MODEL wrote, whose
	// unchanged surfaces a v11 frame repeats in one byte, or NULL.
	void	WriteToDemo( idDemoFile *demo, const idGuiModel *previous = NULL );
	bool	ReadFromDemo( idDemoFile *demo );
	void	Swap( idGuiModel &other );
	const idList<idDrawVert> &	Verts() const { return verts; }
	const idList<glIndex_t> &	Indexes() const { return indexes; }

	// Appends another model's surfaces to a render demo's frame model. Scene
	// composition (post-process sorts) and surfaces that sample a runtime-only
	// retained UI image (atlas, layer, generated or blur target) are left
	// out: playback renders the scene itself, and those pixels never reach
	// the demo.
	void	AppendForDemo( const idGuiModel &other );
	void	AppendForDemoScaled( const idGuiModel &other, float opacity );
	bool	IsEmpty() const;
	// CRC of the geometry, colors and materials, for change detection.
	unsigned int	ContentChecksum() const;

	void	EmitToCurrentView( float modelMatrix[16], bool depthHack );
	void	EmitFullScreen();
	bool	HasSceneFeedback() const;

	// these calls are forwarded from the renderer
	void	SetColor( float r, float g, float b, float a );
	void	DrawStretchPic( const idDrawVert *verts, const glIndex_t *indexes, int vertCount, int indexCount, const idMaterial *hShader,
									bool clip = true, float min_x = 0.0f, float min_y = 0.0f, float max_x = 640.0f, float max_y = 480.0f );
	void	DrawStretchPic( float x, float y, float w, float h,
									float s1, float t1, float s2, float t2, const idMaterial *hShader);
	void	DrawStretchTri ( idVec2 p1, idVec2 p2, idVec2 p3, idVec2 t1, idVec2 t2, idVec2 t3, const idMaterial *material );

	//---------------------------
private:
	bool	ReadSurfacesFromDemo( idDemoFile *demo );
	void	AdvanceSurf();
	void	EmitSurface( guiModelSurface_t *surf, float modelMatrix[16], float modelViewMatrix[16], bool depthHack,
				const struct viewEntity_s *sharedSpace = NULL );

	guiModelSurface_t		*surf;

	idList<guiModelSurface_t>	surfaces;
	idList<glIndex_t>		indexes;
	idList<idDrawVert>	verts;
};

