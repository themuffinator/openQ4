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




#include "tr_local.h"
#include "ScenePackets.h"

static const int Q4_GUI_MODEL_MAX_SURFACE_VERTS = 12800;
static idCVar r_guiSharedSpace( "r_guiSharedSpace", "0", CVAR_RENDERER | CVAR_BOOL,
	"share the identical frame-local transform of fullscreen GUI surfaces" );

static ID_INLINE void R_SetGuiDrawVertPayload( idDrawVert *vert ) {
	vert->color[0] = vert->color[1] = vert->color[2] = vert->color[3] = 255;
	vert->color2[0] = vert->color2[1] = vert->color2[2] = vert->color2[3] = 255;
}

static ID_INLINE void R_SetGuiDrawVert( idDrawVert *vert, float x, float y, float z, float s, float t, const idDrawVert *source = NULL ) {
	if ( source != NULL ) {
		*vert = *source;
	} else {
		vert->Clear();
	}

	vert->xyz.Set( x, y, z );
	vert->st.Set( s, t );
	vert->normal.Set( 0, 0, 1 );
	vert->tangents[0].Set( 1, 0, 0 );
	vert->tangents[1].Set( 0, 1, 0 );
	R_SetGuiDrawVertPayload( vert );
}

static bool R_IsFiniteGuiDemoDrawVert( const idDrawVert &vert ) {
	for ( int component = 0; component < 3; component++ ) {
		if ( !std::isfinite( vert.xyz[component] ) || !std::isfinite( vert.normal[component] ) ||
			 !std::isfinite( vert.tangents[0][component] ) || !std::isfinite( vert.tangents[1][component] ) ) {
			return false;
		}
	}
	return std::isfinite( vert.st[0] ) && std::isfinite( vert.st[1] );
}

static bool R_IsFiniteGuiDemoColor( const float color[4] ) {
	return std::isfinite( color[0] ) && std::isfinite( color[1] ) &&
		std::isfinite( color[2] ) && std::isfinite( color[3] );
}

static bool R_RejectGuiModelDemo( idDemoFile *demo, const char *reason ) {
	common->Warning( "Malformed render demo GUI model: %s; playback stopped safely", reason );
	if ( demo != NULL ) {
		demo->Close();
	}
	return false;
}

/*
================
idGuiModel::idGuiModel
================
*/
idGuiModel::idGuiModel() {
	indexes.SetGranularity( 1000 );
	verts.SetGranularity( 1000 );
}

/*
================
idGuiModel::HasSceneFeedback

Keep capture-dependent batches separate when switching between scene and UI.
================
*/
bool idGuiModel::HasSceneFeedback() const {
	for ( int i = 0; i < surfaces.Num(); ++i ) {
		const guiModelSurface_t &surface = surfaces[i];
		if ( surface.numIndexes > 0 && surface.material != NULL &&
				( surface.material->GetSort() >= SS_POST_PROCESS ||
				  surface.material->TestMaterialFlag( MF_NEED_CURRENT_RENDER ) ) ) {
			return true;
		}
	}
	return false;
}

// Begins collecting draw commands into surfaces.
void idGuiModel::Clear() {
	surfaces.SetNum( 0, false );
	indexes.SetNum( 0, false );
	verts.SetNum( 0, false );
	AdvanceSurf();
}

// A GUI vertex whose normal and tangents hold the constant 2D frame; only
// payload-carrying vertices (shader-evaluated linear masks) differ.
static bool R_GuiDemoVertIsPlain( const idDrawVert &vert ) {
	return vert.normal[0] == 0.0f && vert.normal[1] == 0.0f && vert.normal[2] == 1.0f &&
		vert.tangents[0][0] == 1.0f && vert.tangents[0][1] == 0.0f && vert.tangents[0][2] == 0.0f &&
		vert.tangents[1][0] == 0.0f && vert.tangents[1][1] == 1.0f && vert.tangents[1][2] == 0.0f;
}

// Whether surface index of model a equals the same surface of model b, in
// everything a recorded frame stores.
static bool R_GuiDemoSurfacesMatch( const idGuiModel &a, const guiModelSurface_t &sa,
		const idGuiModel &b, const guiModelSurface_t &sb ) {
	if ( sa.material != sb.material || sa.numVerts != sb.numVerts || sa.numIndexes != sb.numIndexes ||
		 memcmp( sa.color, sb.color, sizeof( sa.color ) ) != 0 ) {
		return false;
	}
	for ( int i = 0; i < sa.numVerts; i++ ) {
		const idDrawVert &va = a.Verts()[sa.firstVert + i];
		const idDrawVert &vb = b.Verts()[sb.firstVert + i];
		if ( va.xyz != vb.xyz || va.st != vb.st || memcmp( va.color, vb.color, sizeof( va.color ) ) != 0 ||
			 va.normal != vb.normal || va.tangents[0] != vb.tangents[0] || va.tangents[1] != vb.tangents[1] ) {
			return false;
		}
	}
	return sa.numIndexes == 0 ||
		memcmp( &a.Indexes()[sa.firstIndex], &b.Indexes()[sb.firstIndex], sa.numIndexes * sizeof( glIndex_t ) ) == 0;
}

/*
================
idGuiModel::WriteToDemo

Render demo v11 writes a model surface by surface. A surface identical to the
same surface of previous (the frame written before it) is one byte; a HUD
mostly repeats from frame to frame. A changed surface stores plain vertices
in 25 bytes rather than 68, and indices in two bytes.
================
*/
void idGuiModel::WriteToDemo( idDemoFile *demo, const idGuiModel *previous ) {
	int written = 0;
	demo->WriteInt( surfaces.Num() );
	for ( int j = 0; j < surfaces.Num(); j++ ) {
		const guiModelSurface_t &surface = surfaces[j];
		if ( previous != NULL && j < previous->surfaces.Num() &&
			 R_GuiDemoSurfacesMatch( *this, surface, *previous, previous->surfaces[j] ) ) {
			demo->WriteUnsignedChar( 0 );		// as the previous frame drew it
			continue;
		}
		written++;
		demo->WriteUnsignedChar( 1 );
		demo->WriteBool( surface.material != NULL );
		if ( surface.material != NULL ) {
			demo->WriteHashString( surface.material->GetName() );
		}
		demo->WriteFloat( surface.color[0] );
		demo->WriteFloat( surface.color[1] );
		demo->WriteFloat( surface.color[2] );
		demo->WriteFloat( surface.color[3] );
		demo->WriteInt( surface.numVerts );
		for ( int i = 0; i < surface.numVerts; i++ ) {
			const idDrawVert &vert = verts[surface.firstVert + i];
			const bool plain = R_GuiDemoVertIsPlain( vert );
			demo->WriteUnsignedChar( plain ? 0 : 1 );
			demo->WriteVec3( vert.xyz );
			demo->WriteVec2( vert.st );
			if ( !plain ) {
				demo->WriteVec3( vert.normal );
				demo->WriteVec3( vert.tangents[0] );
				demo->WriteVec3( vert.tangents[1] );
			}
			demo->WriteUnsignedChar( vert.color[0] );
			demo->WriteUnsignedChar( vert.color[1] );
			demo->WriteUnsignedChar( vert.color[2] );
			demo->WriteUnsignedChar( vert.color[3] );
		}
		demo->WriteInt( surface.numIndexes );
		const bool shortIndexes = surface.numVerts <= 0xffff;
		for ( int i = 0; i < surface.numIndexes; i++ ) {
			if ( shortIndexes ) {
				demo->WriteUnsignedShort( static_cast<unsigned short>( indexes[surface.firstIndex + i] ) );
			} else {
				demo->WriteInt( indexes[surface.firstIndex + i] );
			}
		}
		if ( r_showDemo.GetBool() && surface.numVerts > 0 ) {
			common->Printf( "  gui surface %d: %s color=%.2f %.2f %.2f %.2f verts=%d\n", j,
				surface.material != NULL ? surface.material->GetName() : "<none>",
				surface.color[0], surface.color[1], surface.color[2], surface.color[3], surface.numVerts );
		}
	}
	if ( r_showDemo.GetBool() ) {
		common->Printf( "  gui model: %d of %d surfaces written\n", written, surfaces.Num() );
	}
}

/*
================
idGuiModel::ReadFromDemo
================
*/
bool idGuiModel::ReadFromDemo( idDemoFile *demo ) {
	if ( session->renderdemoVersion >= OPENQ4_RENDERDEMO_SESSION_VERSION ) {
		return ReadSurfacesFromDemo( demo );
	}

	int		i, j;

	i = 0;
	if ( demo == NULL || demo->ReadInt( i ) != sizeof( i ) ) {
		Clear();
		return R_RejectGuiModelDemo( demo, "truncated vertex count" );
	}
	if ( i < 0 || i > 1 << 20 ) {
		Clear();
		return R_RejectGuiModelDemo( demo, va( "vertex count %d is out of range", i ) );
	}
	verts.SetNum( i, false );
	for ( j = 0; j < i; j++ )
	{
		if ( demo->ReadVec3( verts[j].xyz ) != sizeof( verts[j].xyz ) ||
			 demo->ReadVec2( verts[j].st ) != sizeof( verts[j].st ) ||
			 demo->ReadVec3( verts[j].normal ) != sizeof( verts[j].normal ) ||
			 demo->ReadVec3( verts[j].tangents[0] ) != sizeof( verts[j].tangents[0] ) ||
			 demo->ReadVec3( verts[j].tangents[1] ) != sizeof( verts[j].tangents[1] ) ||
			 demo->ReadUnsignedChar( verts[j].color[0] ) != sizeof( verts[j].color[0] ) ||
			 demo->ReadUnsignedChar( verts[j].color[1] ) != sizeof( verts[j].color[1] ) ||
			 demo->ReadUnsignedChar( verts[j].color[2] ) != sizeof( verts[j].color[2] ) ||
			 demo->ReadUnsignedChar( verts[j].color[3] ) != sizeof( verts[j].color[3] ) ) {
			Clear();
			return R_RejectGuiModelDemo( demo, "truncated vertex payload" );
		}
		verts[j].color2[0] = verts[j].color2[1] = verts[j].color2[2] = verts[j].color2[3] = 255;
		if ( !R_IsFiniteGuiDemoDrawVert( verts[j] ) ) {
			Clear();
			return R_RejectGuiModelDemo( demo, "non-finite vertex payload" );
		}
	}
	
	i = 0;
	if ( demo->ReadInt( i ) != sizeof( i ) ) {
		Clear();
		return R_RejectGuiModelDemo( demo, "truncated index count" );
	}
	if ( i < 0 || i > 1 << 21 ) {
		Clear();
		return R_RejectGuiModelDemo( demo, va( "index count %d is out of range", i ) );
	}
	indexes.SetNum( i, false );
	for ( j = 0; j < i; j++ ) {
		if ( demo->ReadInt( indexes[j] ) != sizeof( indexes[j] ) ) {
			Clear();
			return R_RejectGuiModelDemo( demo, "truncated index payload" );
		}
	}
	
	i = 0;
	if ( demo->ReadInt( i ) != sizeof( i ) ) {
		Clear();
		return R_RejectGuiModelDemo( demo, "truncated surface count" );
	}
	if ( i < 0 || i > 1 << 16 ) {
		Clear();
		return R_RejectGuiModelDemo( demo, va( "surface count %d is out of range", i ) );
	}
	surfaces.SetNum( i, false );
	for ( j = 0 ; j < i ; j++ ) {
		guiModelSurface_t	*surf = &surfaces[j];
		bool				hasMaterial = false;
		
		if ( session->renderdemoVersion >= OPENQ4_RENDERDEMO_POINTER_FREE_VERSION ) {
			if ( demo->ReadBool( hasMaterial ) != sizeof( byte ) ) {
				Clear();
				return R_RejectGuiModelDemo( demo, "truncated material flag" );
			}
		} else {
			int legacyMaterial = 0;
			if ( demo->ReadInt( legacyMaterial ) != sizeof( legacyMaterial ) ) {
				Clear();
				return R_RejectGuiModelDemo( demo, "truncated legacy material flag" );
			}
			hasMaterial = ( legacyMaterial != 0 );
		}
		if ( demo->ReadFloat( surf->color[0] ) != sizeof( surf->color[0] ) ||
			 demo->ReadFloat( surf->color[1] ) != sizeof( surf->color[1] ) ||
			 demo->ReadFloat( surf->color[2] ) != sizeof( surf->color[2] ) ||
			 demo->ReadFloat( surf->color[3] ) != sizeof( surf->color[3] ) ||
			 demo->ReadInt( surf->firstVert ) != sizeof( surf->firstVert ) ||
			 demo->ReadInt( surf->numVerts ) != sizeof( surf->numVerts ) ||
			 demo->ReadInt( surf->firstIndex ) != sizeof( surf->firstIndex ) ||
			 demo->ReadInt( surf->numIndexes ) != sizeof( surf->numIndexes ) ) {
			Clear();
			return R_RejectGuiModelDemo( demo, "truncated surface payload" );
		}
		if ( !R_IsFiniteGuiDemoColor( surf->color ) ) {
			Clear();
			return R_RejectGuiModelDemo( demo, "non-finite surface color" );
		}
		if ( surf->firstVert < 0 || surf->numVerts < 0 || surf->firstVert > verts.Num() - surf->numVerts ||
			 surf->firstIndex < 0 || surf->numIndexes < 0 || surf->firstIndex > indexes.Num() - surf->numIndexes ) {
			Clear();
			return R_RejectGuiModelDemo( demo, "surface range is out of bounds" );
		}
		for ( int indexOffset = 0; indexOffset < surf->numIndexes; indexOffset++ ) {
			const int storedIndex = indexes[surf->firstIndex + indexOffset];
			if ( storedIndex < 0 || storedIndex >= surf->numVerts ) {
				Clear();
				return R_RejectGuiModelDemo( demo, va( "surface vertex index %d is out of range", storedIndex ) );
			}
		}
		surf->material = hasMaterial ? declManager->FindMaterial( demo->ReadHashString() ) : NULL;
		if ( !demo->IsOpen() ) {
			Clear();
			return false;
		}
	}
	return true;
}

/*
================
idGuiModel::ReadSurfacesFromDemo

The v11 surface-by-surface record: a repeated surface copies the same surface
of the model as it stood before this read.
================
*/
bool idGuiModel::ReadSurfacesFromDemo( idDemoFile *demo ) {
	idGuiModel previous;
	Swap( previous );
	surfaces.SetNum( 0, false );
	verts.SetNum( 0, false );
	indexes.SetNum( 0, false );
	surf = NULL;

	int count = 0;
	if ( demo == NULL || demo->ReadInt( count ) != sizeof( count ) ) {
		Clear();
		return R_RejectGuiModelDemo( demo, "truncated surface count" );
	}
	if ( count < 0 || count > 1 << 16 ) {
		Clear();
		return R_RejectGuiModelDemo( demo, va( "surface count %d is out of range", count ) );
	}
	for ( int j = 0; j < count; j++ ) {
		unsigned char kind = 0;
		if ( demo->ReadUnsignedChar( kind ) != sizeof( kind ) || kind > 1 ) {
			Clear();
			return R_RejectGuiModelDemo( demo, "invalid surface record" );
		}
		guiModelSurface_t surface;
		if ( kind == 0 ) {
			if ( j >= previous.surfaces.Num() ) {
				Clear();
				return R_RejectGuiModelDemo( demo, "repeats a surface the previous frame did not have" );
			}
			const guiModelSurface_t &source = previous.surfaces[j];
			surface = source;
			surface.firstVert = verts.Num();
			surface.firstIndex = indexes.Num();
			for ( int i = 0; i < source.numVerts; i++ ) {
				verts.Append( previous.verts[source.firstVert + i] );
			}
			for ( int i = 0; i < source.numIndexes; i++ ) {
				indexes.Append( previous.indexes[source.firstIndex + i] );
			}
			surfaces.Append( surface );
			continue;
		}

		bool hasMaterial = false;
		if ( demo->ReadBool( hasMaterial ) != sizeof( byte ) ) {
			Clear();
			return R_RejectGuiModelDemo( demo, "truncated material flag" );
		}
		surface.material = hasMaterial ? declManager->FindMaterial( demo->ReadHashString() ) : NULL;
		if ( !demo->IsOpen() ) {
			Clear();
			return false;
		}
		int numVerts = 0;
		if ( demo->ReadFloat( surface.color[0] ) != sizeof( surface.color[0] ) ||
			 demo->ReadFloat( surface.color[1] ) != sizeof( surface.color[1] ) ||
			 demo->ReadFloat( surface.color[2] ) != sizeof( surface.color[2] ) ||
			 demo->ReadFloat( surface.color[3] ) != sizeof( surface.color[3] ) ||
			 demo->ReadInt( numVerts ) != sizeof( numVerts ) ) {
			Clear();
			return R_RejectGuiModelDemo( demo, "truncated surface payload" );
		}
		if ( !R_IsFiniteGuiDemoColor( surface.color ) ) {
			Clear();
			return R_RejectGuiModelDemo( demo, "non-finite surface color" );
		}
		if ( numVerts < 0 || numVerts > ( 1 << 20 ) - verts.Num() ) {
			Clear();
			return R_RejectGuiModelDemo( demo, va( "vertex count %d is out of range", numVerts ) );
		}
		surface.firstVert = verts.Num();
		surface.numVerts = numVerts;
		for ( int i = 0; i < numVerts; i++ ) {
			idDrawVert vert;
			unsigned char layout = 0;
			if ( demo->ReadUnsignedChar( layout ) != sizeof( layout ) || layout > 1 ) {
				Clear();
				return R_RejectGuiModelDemo( demo, "invalid vertex layout" );
			}
			vert.normal.Set( 0, 0, 1 );
			vert.tangents[0].Set( 1, 0, 0 );
			vert.tangents[1].Set( 0, 1, 0 );
			if ( demo->ReadVec3( vert.xyz ) != sizeof( vert.xyz ) ||
				 demo->ReadVec2( vert.st ) != sizeof( vert.st ) ||
				 ( layout != 0 && demo->ReadVec3( vert.normal ) != sizeof( vert.normal ) ) ||
				 ( layout != 0 && demo->ReadVec3( vert.tangents[0] ) != sizeof( vert.tangents[0] ) ) ||
				 ( layout != 0 && demo->ReadVec3( vert.tangents[1] ) != sizeof( vert.tangents[1] ) ) ||
				 demo->ReadUnsignedChar( vert.color[0] ) != sizeof( vert.color[0] ) ||
				 demo->ReadUnsignedChar( vert.color[1] ) != sizeof( vert.color[1] ) ||
				 demo->ReadUnsignedChar( vert.color[2] ) != sizeof( vert.color[2] ) ||
				 demo->ReadUnsignedChar( vert.color[3] ) != sizeof( vert.color[3] ) ) {
				Clear();
				return R_RejectGuiModelDemo( demo, "truncated vertex payload" );
			}
			vert.color2[0] = vert.color2[1] = vert.color2[2] = vert.color2[3] = 255;
			if ( !R_IsFiniteGuiDemoDrawVert( vert ) ) {
				Clear();
				return R_RejectGuiModelDemo( demo, "non-finite vertex payload" );
			}
			verts.Append( vert );
		}
		int numIndexes = 0;
		if ( demo->ReadInt( numIndexes ) != sizeof( numIndexes ) ) {
			Clear();
			return R_RejectGuiModelDemo( demo, "truncated index count" );
		}
		if ( numIndexes < 0 || numIndexes > ( 1 << 21 ) - indexes.Num() ) {
			Clear();
			return R_RejectGuiModelDemo( demo, va( "index count %d is out of range", numIndexes ) );
		}
		surface.firstIndex = indexes.Num();
		surface.numIndexes = numIndexes;
		const bool shortIndexes = numVerts <= 0xffff;
		for ( int i = 0; i < numIndexes; i++ ) {
			int index = 0;
			if ( shortIndexes ) {
				unsigned short shortIndex = 0;
				if ( demo->ReadUnsignedShort( shortIndex ) != sizeof( shortIndex ) ) {
					Clear();
					return R_RejectGuiModelDemo( demo, "truncated index payload" );
				}
				index = shortIndex;
			} else if ( demo->ReadInt( index ) != sizeof( index ) ) {
				Clear();
				return R_RejectGuiModelDemo( demo, "truncated index payload" );
			}
			if ( index < 0 || index >= numVerts ) {
				Clear();
				return R_RejectGuiModelDemo( demo, va( "surface vertex index %d is out of range", index ) );
			}
			indexes.Append( index );
		}
		surfaces.Append( surface );
	}
	if ( surfaces.Num() == 0 ) {
		Clear();
	} else {
		surf = &surfaces[surfaces.Num() - 1];
	}
	return true;
}

/*
================
idGuiModel::Swap
================
*/
void idGuiModel::Swap( idGuiModel &other ) {
	surfaces.Swap( other.surfaces );
	verts.Swap( other.verts );
	indexes.Swap( other.indexes );
	surf = surfaces.Num() > 0 ? &surfaces[surfaces.Num() - 1] : NULL;
	other.surf = other.surfaces.Num() > 0 ? &other.surfaces[other.surfaces.Num() - 1] : NULL;
}

/*
================
R_GuiDemoSurfaceIsExcluded

2D a render demo leaves out of a frame. Post-process sorts compose the scene
itself: the game presents its off-screen scene target with one (for example
postprocess/nopostprocess sampling _postProcessAlbedo0), and playback renders
the recorded scene directly, so replaying it would cover the scene with a
stale target. Retained UI materials whose images exist only in the recording
process: the coverage atlas, composition layers and masks, generated pixels,
blurred backdrops, model previews and the pause scene; also a layer-sampling
linear mask (slot 0 samples _white and replays).
================
*/
static bool R_GuiDemoSurfaceIsExcluded( const idMaterial *material ) {
	if ( material == NULL ) {
		return false;
	}
	if ( material->GetSort() >= SS_POST_PROCESS ) {
		return true;
	}
	const char *name = material->GetName();
	if ( idStr::Icmpn( name, "_retained", 9 ) != 0 ) {
		return false;
	}
	static const char *const runtimeOnly[] = {
		"_retainedAtlas/", "_retainedMultiplyAtlas/", "_retainedLayer/", "_retainedMask/",
		"_retainedGenerated/", "_retainedBlur/", "_retainedModel", "_retainedPauseScene"
	};
	for ( int i = 0; i < static_cast<int>( sizeof( runtimeOnly ) / sizeof( runtimeOnly[0] ) ); i++ ) {
		if ( idStr::Icmpn( name, runtimeOnly[i], idStr::Length( runtimeOnly[i] ) ) == 0 ) {
			return true;
		}
	}
	static const char linearMask[] = "_retainedLinearMask/";
	if ( idStr::Icmpn( name, linearMask, sizeof( linearMask ) - 1 ) == 0 ) {
		return idStr::Cmp( name + sizeof( linearMask ) - 1, "0" ) != 0;
	}
	return false;
}

/*
================
R_DemoRetainedLayerSlot

N for a retained UI composition layer's image or material
(_retainedLayerImageN, _retainedLayer/N), else 0.
================
*/
static int R_DemoRetainedLayerSlot( const char *name, const char *prefix ) {
	const int length = idStr::Length( prefix );
	if ( name == NULL || idStr::Icmpn( name, prefix, length ) != 0 || !idStr::IsNumeric( name + length ) ) {
		return 0;
	}
	const int slot = atoi( name + length );
	return slot > 0 && slot < 4096 ? slot : 0;
}

/*
================
R_DemoGuiTarget

The model a flushed 2D batch belongs to while a render demo records: the
frame's screen model, a retained layer's content, or NULL for any other
target (scene captures, crops, render textures of the game).
================
*/
idGuiModel *R_DemoGuiTarget() {
	if ( session->writeDemo == NULL || tr.currentRenderCrop != 0 || tr.guiRecursionLevel != 0 ) {
		return NULL;
	}
	if ( tr.activeRenderTexture == NULL ) {
		return tr.demoFrameGuiModel;
	}
	if ( tr.activeRenderTexture->GetNumColorImages() <= 0 || tr.activeRenderTexture->GetColorImage( 0 ) == NULL ) {
		return NULL;
	}
	const int slot = R_DemoRetainedLayerSlot( tr.activeRenderTexture->GetColorImage( 0 )->GetName(), "_retainedLayerImage" );
	if ( slot <= 0 ) {
		return NULL;
	}
	if ( tr.demoLayerGuiModels.Num() <= slot ) {
		const int previous = tr.demoLayerGuiModels.Num();
		tr.demoLayerGuiModels.SetNum( slot + 1 );
		for ( int i = previous; i < tr.demoLayerGuiModels.Num(); i++ ) {
			tr.demoLayerGuiModels[i] = NULL;
		}
	}
	if ( tr.demoLayerGuiModels[slot] == NULL ) {
		tr.demoLayerGuiModels[slot] = new idGuiModel;
		tr.demoLayerGuiModels[slot]->Clear();
	}
	return tr.demoLayerGuiModels[slot];
}

/*
================
idGuiModel::AppendForDemo
================
*/
void idGuiModel::AppendForDemo( const idGuiModel &other ) {
	AppendForDemoScaled( other, 1.0f );
}

/*
================
idGuiModel::AppendForDemoScaled

opacity scales every appended vertex colour: GUI colours are premultiplied,
so this is the composite of a retained layer at that opacity.
================
*/
void idGuiModel::AppendForDemoScaled( const idGuiModel &other, float opacity ) {
	if ( &other == this ) {
		return;
	}
	for ( int i = 0; i < other.surfaces.Num(); i++ ) {
		const guiModelSurface_t &source = other.surfaces[i];
		if ( source.numVerts <= 0 || source.numIndexes <= 0 ) {
			continue;
		}
		// A retained layer's composite records as the layer's content.
		const int layerSlot = source.material != NULL
			? R_DemoRetainedLayerSlot( source.material->GetName(), "_retainedLayer/" ) : 0;
		if ( layerSlot > 0 ) {
			const idGuiModel *content = layerSlot < tr.demoLayerGuiModels.Num() ? tr.demoLayerGuiModels[layerSlot] : NULL;
			if ( content != NULL && content != this ) {
				const float layerOpacity = opacity * source.color[3] *
					( other.verts[source.firstVert].color[3] / 255.0f );
				AppendForDemoScaled( *content, layerOpacity );
			}
			continue;
		}
		if ( R_GuiDemoSurfaceIsExcluded( source.material ) ) {
			if ( r_showDemo.GetBool() ) {
				common->Printf( "  gui surface left out of the demo: %s verts=%d\n",
					source.material->GetName(), source.numVerts );
			}
			continue;
		}
		// Fill the open surface when it is still empty, so the first surface
		// carries geometry: EmitFullScreen tests that one alone.
		if ( surf == NULL || surf->numVerts > 0 || surf->numIndexes > 0 ) {
			AdvanceSurf();
		}
		surf->material = source.material;
		surf->color[0] = source.color[0];
		surf->color[1] = source.color[1];
		surf->color[2] = source.color[2];
		surf->color[3] = source.color[3];
		surf->firstVert = verts.Num();
		surf->numVerts = source.numVerts;
		surf->firstIndex = indexes.Num();
		surf->numIndexes = source.numIndexes;
		// Indices are relative to their surface's first vertex. A linear mask's
		// colour carries shader data, not light, so it is never scaled.
		const bool scale = opacity < 1.0f && !( source.material != NULL &&
			idStr::Icmpn( source.material->GetName(), "_retainedLinearMask/", 20 ) == 0 );
		for ( int vert = 0; vert < source.numVerts; vert++ ) {
			idDrawVert copy = other.verts[source.firstVert + vert];
			if ( scale ) {
				for ( int channel = 0; channel < 4; channel++ ) {
					copy.color[channel] = static_cast<byte>( idMath::ClampFloat( 0.0f, 255.0f,
						copy.color[channel] * opacity + 0.5f ) );
				}
			}
			verts.Append( copy );
		}
		for ( int index = 0; index < source.numIndexes; index++ ) {
			indexes.Append( other.indexes[source.firstIndex + index] );
		}
	}
}

/*
================
idGuiModel::IsEmpty
================
*/
bool idGuiModel::IsEmpty() const {
	for ( int i = 0; i < surfaces.Num(); i++ ) {
		if ( surfaces[i].numVerts > 0 && surfaces[i].numIndexes > 0 ) {
			return false;
		}
	}
	return true;
}

/*
================
idGuiModel::ContentChecksum
================
*/
unsigned int idGuiModel::ContentChecksum() const {
	uint32_t crc;
	CRC32_InitChecksum( crc );
	if ( verts.Num() > 0 ) {
		CRC32_UpdateChecksum( crc, verts.Ptr(), verts.Num() * static_cast<int>( sizeof( verts[0] ) ) );
	}
	if ( indexes.Num() > 0 ) {
		CRC32_UpdateChecksum( crc, indexes.Ptr(), indexes.Num() * static_cast<int>( sizeof( indexes[0] ) ) );
	}
	for ( int i = 0; i < surfaces.Num(); i++ ) {
		const guiModelSurface_t &surface = surfaces[i];
		CRC32_UpdateChecksum( crc, surface.color, sizeof( surface.color ) );
		CRC32_UpdateChecksum( crc, &surface.firstVert, sizeof( surface.firstVert ) );
		CRC32_UpdateChecksum( crc, &surface.numVerts, sizeof( surface.numVerts ) );
		CRC32_UpdateChecksum( crc, &surface.firstIndex, sizeof( surface.firstIndex ) );
		CRC32_UpdateChecksum( crc, &surface.numIndexes, sizeof( surface.numIndexes ) );
		const char *name = surface.material != NULL ? surface.material->GetName() : "";
		CRC32_UpdateChecksum( crc, name, idStr::Length( name ) );
	}
	CRC32_FinishChecksum( crc );
	return crc;
}

/*
================
EmitSurface
================
*/
void idGuiModel::EmitSurface( guiModelSurface_t *surf, float modelMatrix[16], float modelViewMatrix[16], bool depthHack,
		const viewEntity_t *sharedSpace ) {
	srfTriangles_t	*tri;

	if ( surf->numVerts == 0 ) {
		return;		// nothing in the surface
	}

	// copy verts and indexes
	tri = (srfTriangles_t *)R_ClearedFrameAlloc( sizeof( *tri ) );

	tri->numIndexes = surf->numIndexes;
	tri->numVerts = surf->numVerts;
	tri->indexes = (glIndex_t *)R_FrameAlloc( tri->numIndexes * sizeof( tri->indexes[0] ) );
	memcpy( tri->indexes, &indexes[surf->firstIndex], tri->numIndexes * sizeof( tri->indexes[0] ) );

	// we might be able to avoid copying these and just let them reference the list vars
	// but some things, like deforms and recursive
	// guis, need to access the verts in cpu space, not just through the vertex range
	tri->verts = (idDrawVert *)R_FrameAlloc( tri->numVerts * sizeof( tri->verts[0] ) );
	memcpy( tri->verts, &verts[surf->firstVert], tri->numVerts * sizeof( tri->verts[0] ) );

	// move the verts to the vertex cache
	tri->ambientCache = vertexCache.AllocFrameTemp( tri->verts, tri->numVerts * sizeof( tri->verts[0] ) );

	// if we are out of vertex cache, don't create the surface
	if ( !tri->ambientCache ) {
		return;
	}

	// GUI geometry drew from tri->indexes directly, which is a client-memory
	// index pointer: legal in a compatibility context and REMOVED in a core
	// profile and in OpenGL ES, where a bound element-array buffer is the only
	// way to draw indexed geometry. Without this the entire GUI -- menu, HUD,
	// every in-world screen -- silently draws nothing on those profiles.
	//
	// r_useIndexBuffers is already forced to 2 for them in idVertexCache::Init,
	// but that only governs geometry that asks the cache for an index range;
	// this path never asked.
	if ( r_useIndexBuffers.GetInteger() >= 2 ) {
		tri->indexCache = vertexCache.AllocFrameTemp( tri->indexes,
				tri->numIndexes * sizeof( tri->indexes[0] ), true );
		if ( !tri->indexCache ) {
			return;
		}
	}

	renderEntity_t renderEntity;
	memset( &renderEntity, 0, sizeof( renderEntity ) );
	memcpy( renderEntity.shaderParms, surf->color, sizeof( surf->color ) );

	const viewEntity_t *guiSpace = sharedSpace;
	if ( guiSpace == NULL ) {
		viewEntity_t *privateSpace = (viewEntity_t *)R_ClearedFrameAlloc( sizeof( *privateSpace ) );
		memcpy( privateSpace->modelMatrix, modelMatrix, sizeof( privateSpace->modelMatrix ) );
		memcpy( privateSpace->modelViewMatrix, modelViewMatrix, sizeof( privateSpace->modelViewMatrix ) );
		privateSpace->weaponDepthHack = depthHack;
		guiSpace = privateSpace;
	}

	// add the surface, which might recursively create another gui
	R_AddDrawSurf( tri, guiSpace, &renderEntity, surf->material, tr.viewDef->scissor );
}

/*
====================
EmitToCurrentView
====================
*/
void idGuiModel::EmitToCurrentView( float modelMatrix[16], bool depthHack ) {
	float	modelViewMatrix[16];

	myGlMultMatrix( modelMatrix, tr.viewDef->worldSpace.modelViewMatrix, 
			modelViewMatrix );

	for ( int i = 0 ; i < surfaces.Num() ; i++ ) {
		EmitSurface( &surfaces[i], modelMatrix, modelViewMatrix, depthHack );
	}
}

/*
================
idGuiModel::EmitFullScreen

Creates a view that covers the screen and emit the surfaces
================
*/
void idGuiModel::EmitFullScreen( void ) {
	viewDef_t	*viewDef;

	if ( surfaces[0].numVerts == 0 ) {
		return;
	}

	// A recorded frame keeps every 2D batch flushed to the screen, not only
	// the one still pending when WriteDemoPics runs: the retained HUD flushes
	// as its root begins, and a cropped or render-texture batch never reached
	// the screen.
	if ( this == tr.guiModel && session->writeDemo != NULL ) {
		idGuiModel *demoTarget = R_DemoGuiTarget();
		if ( demoTarget != NULL ) {
			demoTarget->AppendForDemo( *this );
		}
	}

	viewDef = (viewDef_t *)R_ClearedFrameAlloc( sizeof( *viewDef ) );

	// for gui editor
	if ( !tr.viewDef || !tr.viewDef->isEditor ) {
		viewDef->renderView.x = 0;
		viewDef->renderView.y = 0;
		viewDef->renderView.width = SCREEN_WIDTH;
		viewDef->renderView.height = SCREEN_HEIGHT;

		const bool useUIViewport =
			( tr.currentRenderCrop == 0 ) &&
			tr.useUIViewportFor2D &&
			( tr.activeRenderTexture == NULL );

		if ( useUIViewport ) {
			// Constrain fullscreen 2D UI to the configured UI viewport.
			const int viewportX = glConfig.uiViewportX;
			const int viewportY = glConfig.uiViewportY;
			const int viewportWidth = glConfig.uiViewportWidth;
			const int viewportHeight = glConfig.uiViewportHeight;
			const int bottomY = glConfig.vidHeight - ( viewportY + viewportHeight );

			viewDef->viewport.x1 = viewportX;
			viewDef->viewport.x2 = viewportX + viewportWidth - 1;
			viewDef->viewport.y1 = bottomY;
			viewDef->viewport.y2 = bottomY + viewportHeight - 1;
		} else if ( tr.currentRenderCrop == 0 && tr.activeRenderTexture != NULL ) {
			// Fullscreen 2D while rendering into an offscreen target should use
			// the target's full extents, not the UI viewport sub-rect.
			const int targetWidth = ( tr.activeRenderTexture->GetWidth() > 0 ) ? tr.activeRenderTexture->GetWidth() : 1;
			const int targetHeight = ( tr.activeRenderTexture->GetHeight() > 0 ) ? tr.activeRenderTexture->GetHeight() : 1;

			viewDef->viewport.x1 = 0;
			viewDef->viewport.y1 = 0;
			viewDef->viewport.x2 = targetWidth - 1;
			viewDef->viewport.y2 = targetHeight - 1;
		} else {
			// Preserve render-crop behavior used by screenshot/demo capture paths.
			tr.RenderViewToViewport( &viewDef->renderView, &viewDef->viewport );
		}

		viewDef->scissor.x1 = 0;
		viewDef->scissor.y1 = 0;
		viewDef->scissor.x2 = viewDef->viewport.x2 - viewDef->viewport.x1;
		viewDef->scissor.y2 = viewDef->viewport.y2 - viewDef->viewport.y1;
	} else {
		viewDef->renderView.x = tr.viewDef->renderView.x;
		viewDef->renderView.y = tr.viewDef->renderView.y;
		viewDef->renderView.width = tr.viewDef->renderView.width;
		viewDef->renderView.height = tr.viewDef->renderView.height;
		
		viewDef->viewport.x1 = tr.viewDef->renderView.x;
		viewDef->viewport.x2 = tr.viewDef->renderView.x + tr.viewDef->renderView.width;
		viewDef->viewport.y1 = tr.viewDef->renderView.y;
		viewDef->viewport.y2 = tr.viewDef->renderView.y + tr.viewDef->renderView.height;

		viewDef->scissor.x1 = tr.viewDef->scissor.x1;
		viewDef->scissor.y1 = tr.viewDef->scissor.y1;
		viewDef->scissor.x2 = tr.viewDef->scissor.x2;
		viewDef->scissor.y2 = tr.viewDef->scissor.y2;
	}

	viewDef->renderView.time = tr.frameShaderTimeMsec;
	memcpy( viewDef->renderView.shaderParms, tr.primaryRenderView.shaderParms, sizeof( viewDef->renderView.shaderParms ) );
	viewDef->floatTime = tr.frameShaderTime;

	// glOrtho( 0, 640, 480, 0, 0, 1 );		// always assume 640x480 virtual coordinates
	viewDef->projectionMatrix[0] = 2.0f / 640.0f;
	viewDef->projectionMatrix[5] = -2.0f / 480.0f;
	viewDef->projectionMatrix[10] = -2.0f / 1.0f;
	viewDef->projectionMatrix[12] = -1.0f;
	viewDef->projectionMatrix[13] = 1.0f;
	viewDef->projectionMatrix[14] = -1.0f;
	viewDef->projectionMatrix[15] = 1.0f;

	viewDef->worldSpace.modelViewMatrix[0] = 1.0f;
	viewDef->worldSpace.modelViewMatrix[5] = 1.0f;
	viewDef->worldSpace.modelViewMatrix[10] = 1.0f;
	viewDef->worldSpace.modelViewMatrix[15] = 1.0f;

	viewDef->maxDrawSurfs = surfaces.Num();
	viewDef->drawSurfs = (drawSurf_t **)R_FrameAlloc( viewDef->maxDrawSurfs * sizeof( viewDef->drawSurfs[0] ) );
	viewDef->numDrawSurfs = 0;

	viewDef_t	*oldViewDef = tr.viewDef;
	tr.viewDef = viewDef;

	// add the surfaces to this view
	for ( int i = 0 ; i < surfaces.Num() ; i++ ) {
		// Fullscreen surfaces use exactly the view's cleared world space. Its
		// frame lifetime covers deferred/SMP consumption, and its identity lets
		// the backend retain the same transform between adjacent GUI draws.
		EmitSurface( &surfaces[i], viewDef->worldSpace.modelMatrix, viewDef->worldSpace.modelViewMatrix, false,
			r_guiSharedSpace.GetBool() ? &viewDef->worldSpace : NULL );
	}

	tr.viewDef = oldViewDef;

	if ( R_ScenePackets_FrontEndCaptureRequired() ) {
		R_ScenePackets_AddRenderView( viewDef );
	}
	R_AddDrawViewCmd( viewDef );
}

/*
=============
AdvanceSurf
=============
*/
void idGuiModel::AdvanceSurf() {
	guiModelSurface_t	s;

	if ( surfaces.Num() ) {
		s.color[0] = surf->color[0];
		s.color[1] = surf->color[1];
		s.color[2] = surf->color[2];
		s.color[3] = surf->color[3];
		s.material = surf->material;
	} else {
		s.color[0] = 1;
		s.color[1] = 1;
		s.color[2] = 1;
		s.color[3] = 1;
		s.material = tr.defaultMaterial;
	}
	s.numIndexes = 0;
	s.firstIndex = indexes.Num();
	s.numVerts = 0;
	s.firstVert = verts.Num();

	surfaces.Append( s );
	surf = &surfaces[ surfaces.Num() - 1 ];
}

/*
=============
SetColor
=============
*/
void idGuiModel::SetColor( float r, float g, float b, float a ) {
	if ( !glConfig.isInitialized ) {
		return;
	}
	if ( r == surf->color[0] && g == surf->color[1]
		&& b == surf->color[2] && a == surf->color[3] ) {
		return;	// no change
	}

	if ( surf->numVerts ) {
		AdvanceSurf();
	}

	// change the parms
	surf->color[0] = r;
	surf->color[1] = g;
	surf->color[2] = b;
	surf->color[3] = a;
}

/*
=============
DrawStretchPic
=============
*/
void idGuiModel::DrawStretchPic( const idDrawVert *dverts, const glIndex_t *dindexes, int vertCount, int indexCount, const idMaterial *hShader, 
									   bool clip, float min_x, float min_y, float max_x, float max_y ) {
	if ( !glConfig.isInitialized ) {
		return;
	}
	if ( !( dverts && dindexes && vertCount && indexCount && hShader ) ) {
		return;
	}

	// break the current surface if we are changing to a new material
	if ( hShader != surf->material ) {
		if ( surf->numVerts ) {
			AdvanceSurf();
		}
		if ( !com_SingleDeclFile.GetBool() ) {
			const_cast<idMaterial *>(hShader)->EnsureNotPurged();	// in case it was a gui item started before a level change
		}
		surf->material = hShader;
	}

	// A retained vector draw can append thousands of vertices at once. Split
	// before that append: checking only the previous size can put two valid
	// batches into a surface larger than a frame allocation block.
	if ( surf->numVerts && !clip && vertCount > Q4_GUI_MODEL_MAX_SURFACE_VERTS - surf->numVerts ) {
		AdvanceSurf();
	}

	// add the verts and indexes to the current surface

	if ( clip ) {
		int i, j;

		// FIXME:	this is grim stuff, and should be rewritten if we have any significant
		//			number of guis asking for clipping
		idFixedWinding w;
		for ( i = 0; i < indexCount; i += 3 ) {
			w.Clear();
			w.AddPoint(idVec5(dverts[dindexes[i]].xyz.x, dverts[dindexes[i]].xyz.y, dverts[dindexes[i]].xyz.z, dverts[dindexes[i]].st.x, dverts[dindexes[i]].st.y));
			w.AddPoint(idVec5(dverts[dindexes[i+1]].xyz.x, dverts[dindexes[i+1]].xyz.y, dverts[dindexes[i+1]].xyz.z, dverts[dindexes[i+1]].st.x, dverts[dindexes[i+1]].st.y));
			w.AddPoint(idVec5(dverts[dindexes[i+2]].xyz.x, dverts[dindexes[i+2]].xyz.y, dverts[dindexes[i+2]].xyz.z, dverts[dindexes[i+2]].st.x, dverts[dindexes[i+2]].st.y));

			bool needsClip = false;
			for ( j = 0; j < 3; j++ ) {
				if ( w[j].x < min_x || w[j].x > max_x ||
					w[j].y < min_y || w[j].y > max_y ) {
					needsClip = true;
					break;
				}
			}

			if ( needsClip ) {
				idPlane clipPlane( 1.0f, 0.0f, 0.0f, -min_x );
				w.ClipInPlace( clipPlane, 0.1f, false );

				clipPlane = idPlane( -1.0f, 0.0f, 0.0f, max_x );
				w.ClipInPlace( clipPlane, 0.1f, false );

				clipPlane = idPlane( 0.0f, 1.0f, 0.0f, -min_y );
				w.ClipInPlace( clipPlane, 0.1f, false );

				clipPlane = idPlane( 0.0f, -1.0f, 0.0f, max_y );
				w.ClipInPlace( clipPlane, 0.1f, false );
			}

			if ( w.GetNumPoints() <= 0 ) {
				continue;
			}

			const int windingPointCount = w.GetNumPoints();
			if ( surf->numVerts && windingPointCount > Q4_GUI_MODEL_MAX_SURFACE_VERTS - surf->numVerts ) {
				AdvanceSurf();
			}
			const idDrawVert *sourceVert = &dverts[dindexes[i]];
			int	numVerts = verts.Num();
			verts.SetNum( numVerts + windingPointCount, false );
			for ( j = 0 ; j < windingPointCount ; j++ ) {
				idDrawVert *dv = &verts[numVerts+j];
				R_SetGuiDrawVert( dv, w[j].x, w[j].y, w[j].z, w[j].s, w[j].t, sourceVert );
			}
			surf->numVerts += windingPointCount;

			for ( j = 2; j < windingPointCount; j++ ) {
				indexes.Append( numVerts - surf->firstVert );
				indexes.Append( numVerts + j - 1 - surf->firstVert );
				indexes.Append( numVerts + j - surf->firstVert );
				surf->numIndexes += 3;
			}
		}

	} else {

		int numVerts = verts.Num();
		int numIndexes = indexes.Num();

		verts.AssureSize( numVerts + vertCount );
		indexes.AssureSize( numIndexes + indexCount );

		surf->numVerts += vertCount;
		surf->numIndexes += indexCount;

		for ( int i = 0; i < indexCount; i++ ) {
			indexes[numIndexes + i] = numVerts + dindexes[i] - surf->firstVert;
		}

		memcpy( &verts[numVerts], dverts, vertCount * sizeof( verts[0] ) );
	}
}

/*
=============
DrawStretchPic

x/y/w/h are in the 0,0 to 640,480 range
=============
*/
void idGuiModel::DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, const idMaterial *hShader ) {
	idDrawVert verts[4];
	glIndex_t indexes[6];

	if ( !glConfig.isInitialized ) {
		return;
	}
	if ( !hShader ) {
		return;
	}

	// clip to edges, because the pic may be going into a guiShader
	// instead of full screen
	if ( x < 0 ) {
		s1 += ( s2 - s1 ) * -x / w;
		w += x;
		x = 0;
	}
	if ( y < 0 ) {
		t1 += ( t2 - t1 ) * -y / h;
		h += y;
		y = 0;
	}
	if ( x + w > 640 ) {
		s2 -= ( s2 - s1 ) * ( x + w - 640 ) / w;
		w = 640 - x;
	}
	if ( y + h > 480 ) {
		t2 -= ( t2 - t1 ) * ( y + h - 480 ) / h;
		h = 480 - y;
	}
	
	if ( w <= 0 || h <= 0 ) {
		return;		// completely clipped away
	}

	indexes[0] = 3;
	indexes[1] = 0;
	indexes[2] = 2;
	indexes[3] = 2;
	indexes[4] = 0;
	indexes[5] = 1;
	R_SetGuiDrawVert( &verts[0], x, y, 0.0f, s1, t1 );
	R_SetGuiDrawVert( &verts[1], x + w, y, 0.0f, s2, t1 );
	R_SetGuiDrawVert( &verts[2], x + w, y + h, 0.0f, s2, t2 );
	R_SetGuiDrawVert( &verts[3], x, y + h, 0.0f, s1, t2 );

	DrawStretchPic( &verts[0], &indexes[0], 4, 6, hShader, false, 0.0f, 0.0f, 640.0f, 480.0f );
}

/*
=============
DrawStretchTri

x/y/w/h are in the 0,0 to 640,480 range
=============
*/
void idGuiModel::DrawStretchTri( idVec2 p1, idVec2 p2, idVec2 p3, idVec2 t1, idVec2 t2, idVec2 t3, const idMaterial *material ) {
	idDrawVert tempVerts[3];
	glIndex_t tempIndexes[3];
	int vertCount = 3;
	int indexCount = 3;

	if ( !glConfig.isInitialized ) {
		return;
	}
	if ( !material ) {
		return;
	}

	tempIndexes[0] = 1;
	tempIndexes[1] = 0;
	tempIndexes[2] = 2;
	R_SetGuiDrawVert( &tempVerts[0], p1.x, p1.y, 0.0f, t1.x, t1.y );
	R_SetGuiDrawVert( &tempVerts[1], p2.x, p2.y, 0.0f, t2.x, t2.y );
	R_SetGuiDrawVert( &tempVerts[2], p3.x, p3.y, 0.0f, t3.x, t3.y );

	// break the current surface if we are changing to a new material
	if ( material != surf->material ) {
		if ( surf->numVerts ) {
			AdvanceSurf();
		}
		if ( !com_SingleDeclFile.GetBool() ) {
			const_cast<idMaterial *>(material)->EnsureNotPurged();	// in case it was a gui item started before a level change
		}
		surf->material = material;
	}


	int numVerts = verts.Num();
	int numIndexes = indexes.Num();

	verts.AssureSize( numVerts + vertCount );
	indexes.AssureSize( numIndexes + indexCount );

	surf->numVerts += vertCount;
	surf->numIndexes += indexCount;

	for ( int i = 0; i < indexCount; i++ ) {
		indexes[numIndexes + i] = numVerts + tempIndexes[i] - surf->firstVert;
	}

	memcpy( &verts[numVerts], tempVerts, vertCount * sizeof( verts[0] ) );
}

