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

//#define WRITE_GUIS

typedef struct {
	int		version;
	int		sizeofRenderEntity;
	int		sizeofRenderLight;
	char	mapname[256];
} demoHeader_t;

// generous sanity bound for joint counts read from demo files, keeping
// numJoints * sizeof( idJointMat ) within the int parameter of Mem_Alloc16
const int DEMO_MAX_ENTITY_JOINTS = 65536;

namespace {

void R_FreeRenderDemoDecalChain( idRenderModelDecal *decal ) {
	while ( decal != NULL ) {
		idRenderModelDecal *next = decal->Next();
		idRenderModelDecal::Free( decal );
		decal = next;
	}
}

void R_FreeRenderDemoEntityPayload( renderEntity_t &ent, idRenderModelDecal *decal, idRenderModelOverlay *overlay ) {
	if ( ent.joints != NULL ) {
		Mem_Free16( ent.joints );
		ent.joints = NULL;
	}
	R_FreeRenderDemoDecalChain( decal );
	idRenderModelOverlay::Free( overlay );
}

bool R_RejectRenderDemo( idDemoFile *demo, const char *reason ) {
	common->Warning( "Malformed render demo: %s; playback stopped safely", reason );
	if ( demo != NULL ) {
		demo->Close();
	}
	return false;
}

bool R_DemoReadInt( idDemoFile *demo, int &value ) {
	if ( demo != NULL && demo->ReadInt( value ) == sizeof( value ) ) {
		return true;
	}
	return R_RejectRenderDemo( demo, "truncated integer payload" );
}

bool R_DemoReadFloat( idDemoFile *demo, float &value ) {
	if ( demo != NULL && demo->ReadFloat( value ) == sizeof( value ) ) {
		return true;
	}
	return R_RejectRenderDemo( demo, "truncated floating-point payload" );
}

bool R_DemoReadBool( idDemoFile *demo, bool &value ) {
	if ( demo != NULL && demo->ReadBool( value ) == sizeof( value ) ) {
		return true;
	}
	return R_RejectRenderDemo( demo, "truncated boolean payload" );
}

bool R_DemoReadChar( idDemoFile *demo, char &value ) {
	if ( demo != NULL && demo->ReadChar( value ) == sizeof( value ) ) {
		return true;
	}
	return R_RejectRenderDemo( demo, "truncated byte payload" );
}

bool R_DemoReadVec3( idDemoFile *demo, idVec3 &value ) {
	if ( demo != NULL && demo->ReadVec3( value ) == sizeof( value ) ) {
		return true;
	}
	return R_RejectRenderDemo( demo, "truncated vector payload" );
}

bool R_DemoReadVec4( idDemoFile *demo, idVec4 &value ) {
	if ( demo != NULL && demo->ReadVec4( value ) == sizeof( value ) ) {
		return true;
	}
	return R_RejectRenderDemo( demo, "truncated vector payload" );
}

bool R_DemoReadMat3( idDemoFile *demo, idMat3 &value ) {
	if ( demo != NULL && demo->ReadMat3( value ) == sizeof( value ) ) {
		return true;
	}
	return R_RejectRenderDemo( demo, "truncated matrix payload" );
}

/*
Render demo v12 joint records. After an entity's other fields:

	byte mode
	DEMO_JOINTS_RAW:       numJoints * 12 little-endian IEEE words (v11's layout)
	DEMO_JOINTS_PREDICTED: int payloadBytes
	                       payload: a bit per joint, set when it moved since the
	                                handle's last record, then for each moved joint
	                                12 LEB128 varints of the zigzagged residual
	                       uint checksum of the decoded words

Prediction works on the words as unsigned integers, never as floats, so every
platform decodes the recorded bits exactly: the last record's word, extended
linearly by the step from the record before it when both have this joint count.
Recording and playback keep the same history, so the reader rebuilds the words
the writer saw; the checksum stops playback cleanly if they ever disagree.
*/
const int DEMO_JOINT_WORDS = 12;
const byte DEMO_JOINTS_RAW = 0;
const byte DEMO_JOINTS_PREDICTED = 1;
const int DEMO_JOINT_VARINT_MAX_BYTES = 5;

static_assert( sizeof( idJointMat ) == DEMO_JOINT_WORDS * sizeof( unsigned int ), "render demo joints are 12 IEEE words" );
static_assert( sizeof( float ) == sizeof( unsigned int ), "render demo joints store floats as 32-bit words" );

unsigned int R_DemoJointChecksum( const unsigned int *words, int numWords ) {
	// FNV-1a over the little-endian bytes
	unsigned int hash = 2166136261u;
	for ( int i = 0; i < numWords; i++ ) {
		const unsigned int word = words[i];
		for ( int shift = 0; shift < 32; shift += 8 ) {
			hash ^= ( word >> shift ) & 0xFFu;
			hash *= 16777619u;
		}
	}
	return hash;
}

ID_INLINE unsigned int R_DemoJointPrediction( const idRenderWorldLocal::demoJointHistory_t &history, int word ) {
	const unsigned int last = history.current[word];
	if ( !history.hasPrevious ) {
		return last;
	}
	return last + ( last - history.previous[word] );
}

// Keeps what was just written or read as the handle's history.
void R_DemoStoreJointHistory( idRenderWorldLocal::demoJointHistory_t &history, const unsigned int *words, int numJoints ) {
	const int numWords = numJoints * DEMO_JOINT_WORDS;
	if ( history.numJoints == numJoints && history.current.Num() == numWords ) {
		history.previous.Swap( history.current );
		history.hasPrevious = true;
	} else {
		history.previous.Clear();
		history.hasPrevious = false;
	}
	history.current.SetNum( numWords );
	memcpy( history.current.Ptr(), words, numWords * sizeof( unsigned int ) );
	history.numJoints = numJoints;
}

ID_INLINE void R_DemoPutWord( byte *out, unsigned int word ) {
	out[0] = static_cast<byte>( word );
	out[1] = static_cast<byte>( word >> 8 );
	out[2] = static_cast<byte>( word >> 16 );
	out[3] = static_cast<byte>( word >> 24 );
}

ID_INLINE unsigned int R_DemoGetWord( const byte *in ) {
	return static_cast<unsigned int>( in[0] ) | ( static_cast<unsigned int>( in[1] ) << 8 ) |
		( static_cast<unsigned int>( in[2] ) << 16 ) | ( static_cast<unsigned int>( in[3] ) << 24 );
}

void R_WriteDemoRenderViewData( idDemoFile *demo, const renderView_t *renderView ) {
	demo->WriteInt( renderView->viewID );
	demo->WriteInt( renderView->x );
	demo->WriteInt( renderView->y );
	demo->WriteInt( renderView->width );
	demo->WriteInt( renderView->height );
	demo->WriteFloat( renderView->fov_x );
	demo->WriteFloat( renderView->fov_y );
	demo->WriteVec3( renderView->vieworg );
	demo->WriteMat3( renderView->viewaxis );
	demo->WriteBool( renderView->cramZNear );
	demo->WriteBool( renderView->forceUpdate );
	// binary compatibility with old win32 version writing padded structures directly to disk
	demo->WriteUnsignedChar( 0 );
	demo->WriteUnsignedChar( 0 );
	demo->WriteInt( renderView->time );
	for ( int i = 0; i < MAX_GLOBAL_SHADER_PARMS; i++ ) {
		demo->WriteFloat( renderView->shaderParms[i] );
	}
	demo->WriteBool( renderView->globalMaterial != NULL );
	if ( renderView->globalMaterial != NULL ) {
		demo->WriteHashString( renderView->globalMaterial->GetName() );
	}
}

bool R_ReadDemoRenderViewData( idDemoFile *demo, renderView_t *renderView, int renderdemoVersion ) {
	renderView->globalMaterial = NULL;

	if ( !R_DemoReadInt( demo, renderView->viewID ) ||
		 !R_DemoReadInt( demo, renderView->x ) ||
		 !R_DemoReadInt( demo, renderView->y ) ||
		 !R_DemoReadInt( demo, renderView->width ) ||
		 !R_DemoReadInt( demo, renderView->height ) ||
		 !R_DemoReadFloat( demo, renderView->fov_x ) ||
		 !R_DemoReadFloat( demo, renderView->fov_y ) ||
		 !R_DemoReadVec3( demo, renderView->vieworg ) ||
		 !R_DemoReadMat3( demo, renderView->viewaxis ) ||
		 !R_DemoReadBool( demo, renderView->cramZNear ) ||
		 !R_DemoReadBool( demo, renderView->forceUpdate ) ) {
		return false;
	}
	if ( renderView->width < 1 || renderView->width > 32768 || renderView->height < 1 || renderView->height > 32768 ) {
		return false;
	}

	char tmp;
	if ( !R_DemoReadChar( demo, tmp ) ||
		 !R_DemoReadChar( demo, tmp ) ||
		 !R_DemoReadInt( demo, renderView->time ) ) {
		return false;
	}
	for ( int i = 0; i < MAX_GLOBAL_SHADER_PARMS; i++ ) {
		if ( !R_DemoReadFloat( demo, renderView->shaderParms[i] ) ) {
			return false;
		}
	}

	if ( renderdemoVersion >= OPENQ4_RENDERDEMO_RENDER_VIEW_DECLS_VERSION ) {
		bool hasGlobalMaterial = false;
		if ( !R_DemoReadBool( demo, hasGlobalMaterial ) ) {
			return false;
		}
		if ( hasGlobalMaterial ) {
			renderView->globalMaterial = declManager->FindMaterial( demo->ReadHashString() );
			if ( !demo->IsOpen() ) {
				return false;
			}
		}
	} else {
		int legacyGlobalMaterial = 0;
		if ( !R_DemoReadInt( demo, legacyGlobalMaterial ) ) {
			return false;
		}
		// Older render demos only persisted the raw pointer value here, which
		// is meaningless once replayed in a later process.
		renderView->globalMaterial = NULL;
	}

	return true;
}

void R_WriteDemoEffectData( idDemoFile *demo, const rvRenderEffectLocal *effectDef ) {
	const renderEffect_t &effect = effectDef->parms;
	const bool stopped = bse->IsEffectStopped( effectDef );

	demo->WriteInt( effectDef->gameTime );
	demo->WriteFloat( effect.startTime );
	demo->WriteInt( effect.suppressSurfaceInViewID );
	demo->WriteInt( effect.allowSurfaceInViewID );
	demo->WriteInt( effect.groupID );
	demo->WriteVec3( effect.origin );
	demo->WriteMat3( effect.axis );
	demo->WriteVec3( effect.gravity );
	demo->WriteVec3( effect.endOrigin );
	demo->WriteFloat( effect.attenuation );
	demo->WriteBool( effect.hasEndOrigin );
	demo->WriteBool( effect.loop );
	demo->WriteBool( effect.ambient );
	demo->WriteBool( effect.inConnectedArea );
	demo->WriteInt( effect.weaponDepthHackInViewID );
	demo->WriteFloat( effect.modelDepthHack );
	demo->WriteInt( effect.referenceSoundHandle );
	for ( int i = 0; i < MAX_ENTITY_SHADER_PARMS; ++i ) {
		demo->WriteFloat( effect.shaderParms[i] );
	}
	demo->WriteBool( stopped );
	demo->WriteBool( effect.declEffect != NULL );
	if ( effect.declEffect != NULL ) {
		demo->WriteHashString( effect.declEffect->GetName() );
	}
}

bool R_ReadDemoEffectData( idDemoFile *demo, renderEffect_t *effect, int *gameTime, bool *stopped ) {
	effect->declEffect = NULL;

	if ( !R_DemoReadInt( demo, *gameTime ) ||
		 !R_DemoReadFloat( demo, effect->startTime ) ||
		 !R_DemoReadInt( demo, effect->suppressSurfaceInViewID ) ||
		 !R_DemoReadInt( demo, effect->allowSurfaceInViewID ) ||
		 !R_DemoReadInt( demo, effect->groupID ) ||
		 !R_DemoReadVec3( demo, effect->origin ) ||
		 !R_DemoReadMat3( demo, effect->axis ) ||
		 !R_DemoReadVec3( demo, effect->gravity ) ||
		 !R_DemoReadVec3( demo, effect->endOrigin ) ||
		 !R_DemoReadFloat( demo, effect->attenuation ) ||
		 !R_DemoReadBool( demo, effect->hasEndOrigin ) ||
		 !R_DemoReadBool( demo, effect->loop ) ||
		 !R_DemoReadBool( demo, effect->ambient ) ||
		 !R_DemoReadBool( demo, effect->inConnectedArea ) ||
		 !R_DemoReadInt( demo, effect->weaponDepthHackInViewID ) ||
		 !R_DemoReadFloat( demo, effect->modelDepthHack ) ||
		 !R_DemoReadInt( demo, effect->referenceSoundHandle ) ) {
		return false;
	}
	for ( int i = 0; i < MAX_ENTITY_SHADER_PARMS; ++i ) {
		if ( !R_DemoReadFloat( demo, effect->shaderParms[i] ) ) {
			return false;
		}
	}
	if ( !R_DemoReadBool( demo, *stopped ) ) {
		return false;
	}
	bool hasDeclEffect = false;
	if ( !R_DemoReadBool( demo, hasDeclEffect ) ) {
		return false;
	}
	if ( hasDeclEffect ) {
		effect->declEffect = declManager->FindType( DECL_EFFECT, demo->ReadHashString() );
		if ( !demo->IsOpen() ) {
			return false;
		}
	}

	return true;
}

}


/*
==============
StartWritingDemo
==============
*/
void		idRenderWorldLocal::StartWritingDemo( idDemoFile *demo ) {
	int		i;

	// FIXME: we should track the idDemoFile locally, instead of snooping into session for it

	WriteLoadMap();

	// write the door portal state
	for ( i = 0 ; i < numInterAreaPortals ; i++ ) {
		if ( doublePortals[i].blockingBits ) {
			SetPortalState( i+1, doublePortals[i].blockingBits );
		}
	}

	// clear the archive counter on all defs
	for ( i = 0 ; i < lightDefs.Num() ; i++ ) {
		if ( lightDefs[i] ) {
			lightDefs[i]->archived = false;
		}
	}
	for ( i = 0 ; i < entityDefs.Num() ; i++ ) {
		if ( entityDefs[i] ) {
			entityDefs[i]->archived = false;
		}
	}
	for ( i = 0 ; i < effectsDef.Num() ; i++ ) {
		if ( effectsDef[i] ) {
			effectsDef[i]->archived = false;
		}
	}

	// every in-world GUI is written again as it next draws, and the next
	// frame's 2D in full: a segment depends on nothing before it
	demoGuiSurfaceChecksums.Clear();
	if ( this == session->rw ) {
		tr.demoLastGuiModelValid = false;
	}
}

void idRenderWorldLocal::StopWritingDemo() {
//	writeDemo = NULL;
}

// Entity handles a render demo may carry an in-world GUI for.
static const int DEMO_MAX_GUI_SURFACE_HANDLE = 1 << 16;

/*
==============
WriteGuiSurface
==============
*/
void idRenderWorldLocal::WriteGuiSurface( qhandle_t handle, int entityGui, const idGuiModel &model ) {
	if ( this != session->rw || session->writeDemo == NULL || handle < 0 || handle >= DEMO_MAX_GUI_SURFACE_HANDLE ||
		 entityGui < 0 || entityGui >= MAX_RENDERENTITY_GUI ) {
		return;
	}
	// The screen-only pictures a GUI may sample at runtime never reach the demo.
	if ( demoGuiScratch == NULL ) {
		demoGuiScratch = new idGuiModel;
	}
	idGuiModel &recorded = *demoGuiScratch;
	recorded.Clear();
	recorded.AppendForDemo( model );
	const unsigned int checksum = recorded.ContentChecksum() | 1u;
	const int slot = handle * MAX_RENDERENTITY_GUI + entityGui;
	if ( demoGuiSurfaceChecksums.Num() <= slot ) {
		const int previous = demoGuiSurfaceChecksums.Num();
		demoGuiSurfaceChecksums.SetNum( slot + 1 );
		for ( int i = previous; i < demoGuiSurfaceChecksums.Num(); i++ ) {
			demoGuiSurfaceChecksums[i] = 0;
		}
	}
	if ( demoGuiSurfaceChecksums[slot] == checksum ) {
		return;		// unchanged since it was last written
	}
	demoGuiSurfaceChecksums[slot] = checksum;
	session->writeDemo->WriteInt( DS_RENDER );
	session->writeDemo->WriteInt( DC_GUI_SURFACE );
	session->writeDemo->WriteInt( handle );
	session->writeDemo->WriteInt( entityGui );
	recorded.WriteToDemo( session->writeDemo );
	if ( r_showDemo.GetBool() ) {
		common->Printf( "write DC_GUI_SURFACE: %i:%i\n", handle, entityGui );
	}
}

/*
==============
ReadGuiSurface
==============
*/
bool idRenderWorldLocal::ReadGuiSurface() {
	int handle = 0;
	int entityGui = 0;
	if ( !R_DemoReadInt( session->readDemo, handle ) || !R_DemoReadInt( session->readDemo, entityGui ) ) {
		return false;
	}
	if ( handle < 0 || handle >= DEMO_MAX_GUI_SURFACE_HANDLE || entityGui < 0 || entityGui >= MAX_RENDERENTITY_GUI ) {
		return R_RejectRenderDemo( session->readDemo, va( "in-world GUI %d:%d is out of range", handle, entityGui ) );
	}
	const int slot = handle * MAX_RENDERENTITY_GUI + entityGui;
	if ( demoGuiSurfaces.Num() <= slot ) {
		const int previous = demoGuiSurfaces.Num();
		demoGuiSurfaces.SetNum( slot + 1 );
		for ( int i = previous; i < demoGuiSurfaces.Num(); i++ ) {
			demoGuiSurfaces[i] = NULL;
		}
	}
	if ( demoGuiSurfaces[slot] == NULL ) {
		demoGuiSurfaces[slot] = new idGuiModel;
	}
	if ( !demoGuiSurfaces[slot]->ReadFromDemo( session->readDemo ) ) {
		delete demoGuiSurfaces[slot];
		demoGuiSurfaces[slot] = NULL;
		return false;
	}
	if ( r_showDemo.GetBool() ) {
		common->Printf( "DC_GUI_SURFACE: %i:%i\n", handle, entityGui );
	}
	return true;
}

/*
==============
DemoGuiSurface
==============
*/
idGuiModel *idRenderWorldLocal::DemoGuiSurface( qhandle_t handle, int entityGui ) const {
	if ( handle < 0 || entityGui < 0 || entityGui >= MAX_RENDERENTITY_GUI ) {
		return NULL;
	}
	const int slot = handle * MAX_RENDERENTITY_GUI + entityGui;
	return slot < demoGuiSurfaces.Num() ? demoGuiSurfaces[slot] : NULL;
}

/*
==============
FreeDemoGuiSurfaces
==============
*/
void idRenderWorldLocal::FreeDemoGuiSurfaces( qhandle_t handle ) {
	if ( handle < 0 ) {
		demoGuiSurfaces.DeleteContents( true );
		demoGuiSurfaceChecksums.Clear();
		delete demoGuiScratch;
		demoGuiScratch = NULL;
		return;
	}
	for ( int entityGui = 0; entityGui < MAX_RENDERENTITY_GUI; entityGui++ ) {
		const int slot = handle * MAX_RENDERENTITY_GUI + entityGui;
		if ( slot < demoGuiSurfaces.Num() && demoGuiSurfaces[slot] != NULL ) {
			delete demoGuiSurfaces[slot];
			demoGuiSurfaces[slot] = NULL;
		}
		if ( slot < demoGuiSurfaceChecksums.Num() ) {
			demoGuiSurfaceChecksums[slot] = 0;
		}
	}
}

/*
==============
DemoJointHistory
==============
*/
idRenderWorldLocal::demoJointHistory_t *idRenderWorldLocal::DemoJointHistory( qhandle_t handle, bool create ) {
	if ( handle < 0 || handle > LUDICROUS_INDEX ) {
		return NULL;
	}
	if ( handle >= demoJointHistory.Num() ) {
		if ( !create ) {
			return NULL;
		}
		const int previous = demoJointHistory.Num();
		demoJointHistory.SetNum( handle + 1 );
		for ( int i = previous; i < demoJointHistory.Num(); i++ ) {
			demoJointHistory[i] = NULL;
		}
	}
	if ( demoJointHistory[handle] == NULL && create ) {
		demoJointHistory[handle] = new demoJointHistory_t;
	}
	return demoJointHistory[handle];
}

/*
==============
ResetDemoJointHistory

Playback never drops one handle's history: the DC_DELETE_ENTITYDEF that
precedes every rewrite of an archived entity is not a free. Recording may, and
then writes that handle's next joints raw.
==============
*/
void idRenderWorldLocal::ResetDemoJointHistory( qhandle_t handle ) {
	if ( handle < 0 ) {
		demoJointHistory.DeleteContents( true );
		demoJointBuffer.Clear();
		demoJointWords.Clear();
		return;
	}
	if ( handle < demoJointHistory.Num() && demoJointHistory[handle] != NULL ) {
		delete demoJointHistory[handle];
		demoJointHistory[handle] = NULL;
	}
}

/*
==============
WriteDemoJoints
==============
*/
void idRenderWorldLocal::WriteDemoJoints( qhandle_t handle, const idJointMat *joints, int numJoints ) {
	idDemoFile *demo = session->writeDemo;
	const int numWords = numJoints * DEMO_JOINT_WORDS;
	demoJointWords.SetNum( numWords );
	memcpy( demoJointWords.Ptr(), joints, numWords * sizeof( unsigned int ) );
	const unsigned int *words = demoJointWords.Ptr();

	demoJointHistory_t *history = DemoJointHistory( handle, true );
	if ( history == NULL || history->numJoints != numJoints ) {
		demoJointBuffer.SetNum( 1 + numWords * 4 );
		byte *out = demoJointBuffer.Ptr();
		out[0] = DEMO_JOINTS_RAW;
		for ( int i = 0; i < numWords; i++ ) {
			R_DemoPutWord( out + 1 + i * 4, words[i] );
		}
		demo->Write( out, demoJointBuffer.Num() );
	} else {
		const int maskBytes = ( numJoints + 7 ) >> 3;
		const int headerBytes = 1 + 4;
		demoJointBuffer.SetNum( headerBytes + maskBytes + numWords * DEMO_JOINT_VARINT_MAX_BYTES + 4 );
		byte *out = demoJointBuffer.Ptr();
		byte *mask = out + headerBytes;
		memset( mask, 0, maskBytes );
		byte *cursor = mask + maskBytes;
		for ( int joint = 0; joint < numJoints; joint++ ) {
			const int first = joint * DEMO_JOINT_WORDS;
			if ( memcmp( words + first, history->current.Ptr() + first, DEMO_JOINT_WORDS * sizeof( unsigned int ) ) == 0 ) {
				continue;
			}
			mask[joint >> 3] |= static_cast<byte>( 1 << ( joint & 7 ) );
			for ( int word = first; word < first + DEMO_JOINT_WORDS; word++ ) {
				const unsigned int residual = words[word] - R_DemoJointPrediction( *history, word );
				unsigned int value = ( residual << 1 ) ^ ( 0u - ( residual >> 31 ) );
				while ( value >= 0x80u ) {
					*cursor++ = static_cast<byte>( ( value & 0x7Fu ) | 0x80u );
					value >>= 7;
				}
				*cursor++ = static_cast<byte>( value );
			}
		}
		const int payloadBytes = static_cast<int>( cursor - mask );
		out[0] = DEMO_JOINTS_PREDICTED;
		R_DemoPutWord( out + 1, static_cast<unsigned int>( payloadBytes ) );
		R_DemoPutWord( cursor, R_DemoJointChecksum( words, numWords ) );
		cursor += 4;
		demo->Write( out, static_cast<int>( cursor - out ) );
	}
	if ( history != NULL ) {
		R_DemoStoreJointHistory( *history, words, numJoints );
	}
}

/*
==============
ReadDemoJoints
==============
*/
bool idRenderWorldLocal::ReadDemoJoints( qhandle_t handle, idJointMat *joints, int numJoints ) {
	idDemoFile *demo = session->readDemo;
	const int numWords = numJoints * DEMO_JOINT_WORDS;

	if ( session->renderdemoVersion < OPENQ4_RENDERDEMO_JOINT_PREDICTION_VERSION ) {
		for ( int i = 0; i < numJoints; i++ ) {
			float *data = joints[i].ToFloatPtr();
			for ( int j = 0; j < DEMO_JOINT_WORDS; j++ ) {
				if ( !R_DemoReadFloat( demo, data[j] ) ) {
					return false;
				}
			}
		}
		return true;
	}

	char mode = 0;
	if ( !R_DemoReadChar( demo, mode ) ) {
		return false;
	}
	demoJointWords.SetNum( numWords );
	unsigned int *words = demoJointWords.Ptr();
	demoJointHistory_t *history = DemoJointHistory( handle, true );

	if ( static_cast<byte>( mode ) == DEMO_JOINTS_RAW ) {
		demoJointBuffer.SetNum( numWords * 4 );
		if ( demo->Read( demoJointBuffer.Ptr(), demoJointBuffer.Num() ) != demoJointBuffer.Num() ) {
			return R_RejectRenderDemo( demo, "truncated joint payload" );
		}
		for ( int i = 0; i < numWords; i++ ) {
			words[i] = R_DemoGetWord( demoJointBuffer.Ptr() + i * 4 );
		}
	} else if ( static_cast<byte>( mode ) == DEMO_JOINTS_PREDICTED ) {
		if ( history == NULL || history->numJoints != numJoints ) {
			return R_RejectRenderDemo( demo, va( "entity %d predicts joints it never recorded", handle ) );
		}
		const int maskBytes = ( numJoints + 7 ) >> 3;
		int payloadBytes = 0;
		if ( !R_DemoReadInt( demo, payloadBytes ) ) {
			return false;
		}
		if ( payloadBytes < maskBytes || payloadBytes > maskBytes + numWords * DEMO_JOINT_VARINT_MAX_BYTES ) {
			return R_RejectRenderDemo( demo, va( "joint payload of %d bytes is out of range", payloadBytes ) );
		}
		demoJointBuffer.SetNum( payloadBytes + 4 );
		if ( demo->Read( demoJointBuffer.Ptr(), demoJointBuffer.Num() ) != demoJointBuffer.Num() ) {
			return R_RejectRenderDemo( demo, "truncated joint payload" );
		}
		const byte *mask = demoJointBuffer.Ptr();
		const byte *cursor = mask + maskBytes;
		const byte *end = mask + payloadBytes;
		for ( int joint = 0; joint < numJoints; joint++ ) {
			const int first = joint * DEMO_JOINT_WORDS;
			if ( ( mask[joint >> 3] & ( 1 << ( joint & 7 ) ) ) == 0 ) {
				memcpy( words + first, history->current.Ptr() + first, DEMO_JOINT_WORDS * sizeof( unsigned int ) );
				continue;
			}
			for ( int word = first; word < first + DEMO_JOINT_WORDS; word++ ) {
				unsigned int value = 0;
				for ( int shift = 0; ; shift += 7 ) {
					if ( cursor >= end || shift >= 7 * DEMO_JOINT_VARINT_MAX_BYTES ||
						 ( shift == 28 && *cursor > 0x0Fu ) ) {
						return R_RejectRenderDemo( demo, "malformed joint residual" );
					}
					const byte next = *cursor++;
					value |= static_cast<unsigned int>( next & 0x7Fu ) << shift;
					if ( ( next & 0x80u ) == 0 ) {
						break;
					}
				}
				const unsigned int residual = ( value >> 1 ) ^ ( 0u - ( value & 1u ) );
				words[word] = R_DemoJointPrediction( *history, word ) + residual;
			}
		}
		if ( cursor != end ) {
			return R_RejectRenderDemo( demo, "joint payload has trailing bytes" );
		}
		if ( R_DemoGetWord( end ) != R_DemoJointChecksum( words, numWords ) ) {
			return R_RejectRenderDemo( demo, va( "entity %d joints do not match their checksum", handle ) );
		}
	} else {
		return R_RejectRenderDemo( demo, va( "unknown joint record %d", static_cast<int>( static_cast<byte>( mode ) ) ) );
	}

	memcpy( joints, words, numWords * sizeof( unsigned int ) );
	if ( history != NULL ) {
		R_DemoStoreJointHistory( *history, words, numJoints );
	}
	return true;
}

/*
==============
ProcessDemoCommand
==============
*/
bool		idRenderWorldLocal::ProcessDemoCommand( idDemoFile *readDemo, renderView_t *renderView, int *demoTimeOffset ) {
	if ( !readDemo ) {
		return false;
	}

	demoCommand_t	dc;
	qhandle_t		h;

	if ( readDemo->ReadInt( (int&)dc ) != sizeof( dc ) ) {
		R_RejectRenderDemo( readDemo, "truncated render command" );
		return false;
	}

	switch( dc ) {
	case DC_LOADMAP: {
		// read the initial data
		demoHeader_t	header = {};

		if ( !R_DemoReadInt( readDemo, header.version ) ||
			 !R_DemoReadInt( readDemo, header.sizeofRenderEntity ) ||
			 !R_DemoReadInt( readDemo, header.sizeofRenderLight ) ) {
			return false;
		}
		for ( int i = 0; i < (int)sizeof( header.mapname ); i++ ) {
			if ( !R_DemoReadChar( readDemo, header.mapname[i] ) ) {
				return false;
			}
		}
		// a corrupt or truncated demo may not include a terminator
		header.mapname[sizeof( header.mapname ) - 1] = '\0';
		// the internal version value got replaced by DS_VERSION at toplevel
		if ( header.version < 4 || header.version > OPENQ4_RENDERDEMO_CURRENT_VERSION ) {
			return R_RejectRenderDemo( readDemo, va( "unsupported internal version %d", header.version ) );
		}

		if ( r_showDemo.GetBool() ) {
			common->Printf( "DC_LOADMAP: %s\n", header.mapname );
		}
		// the writer dropped its joint history as it wrote this command
		ResetDemoJointHistory( -1 );
		if ( !InitFromMap( header.mapname ) ) {
			return R_RejectRenderDemo( readDemo, va( "map '%s' could not be loaded", header.mapname ) );
		}

		pendingDemoTimeOffset = true;		// the first render view after the map load sets the playback offset

		break;
	}

	case DC_RENDERVIEW:
		if ( !R_ReadDemoRenderViewData( readDemo, renderView, session->renderdemoVersion ) ) {
			return false;
		}
												 
		if ( r_showDemo.GetBool() ) {
			common->Printf( "DC_RENDERVIEW: %i\n", renderView->time );
		}

		// possibly change the time offset if this is from a new map
		if ( pendingDemoTimeOffset && demoTimeOffset ) {
			*demoTimeOffset = renderView->time - eventLoop->Milliseconds();
		}
		pendingDemoTimeOffset = false;
		return false;

	case DC_UPDATE_ENTITYDEF:
		if ( !ReadRenderEntity() ) {
			return false;
		}
		break;
	case DC_DELETE_ENTITYDEF:
		if ( !R_DemoReadInt( readDemo, h ) ) {
			return false;
		}
		if ( r_showDemo.GetBool() ) {
			common->Printf( "DC_DELETE_ENTITYDEF: %i\n", h );
		}
		FreeEntityDef( h );
		break;
	case DC_UPDATE_LIGHTDEF:
		if ( !ReadRenderLight() ) {
			return false;
		}
		break;
	case DC_DELETE_LIGHTDEF:
		if ( !R_DemoReadInt( readDemo, h ) ) {
			return false;
		}
		if ( r_showDemo.GetBool() ) {
			common->Printf( "DC_DELETE_LIGHTDEF: %i\n", h );
		}
		FreeLightDef( h );
		break;
	case DC_UPDATE_EFFECTDEF:
		if ( !ReadRenderEffect() ) {
			return false;
		}
		break;
	case DC_STOP_EFFECTDEF:
		if ( !R_DemoReadInt( readDemo, h ) ) {
			return false;
		}
		if ( r_showDemo.GetBool() ) {
			common->Printf( "DC_STOP_EFFECTDEF: %i\n", h );
		}
		StopEffectDef( h );
		break;
	case DC_DELETE_EFFECTDEF:
		if ( !R_DemoReadInt( readDemo, h ) ) {
			return false;
		}
		if ( r_showDemo.GetBool() ) {
			common->Printf( "DC_DELETE_EFFECTDEF: %i\n", h );
		}
		FreeEffectDef( h );
		break;

	case DC_CAPTURE_RENDER:
		if ( r_showDemo.GetBool() ) {
			common->Printf( "DC_CAPTURE_RENDER\n" );
		}
		renderSystem->CaptureRenderToImage( readDemo->ReadHashString() );
		if ( !readDemo->IsOpen() ) {
			return false;
		}
		break;

	case DC_CROP_RENDER:
		if ( r_showDemo.GetBool() ) {
			common->Printf( "DC_CROP_RENDER\n" );
		}
		int	size[3];
		if ( !R_DemoReadInt( readDemo, size[0] ) ||
			 !R_DemoReadInt( readDemo, size[1] ) ||
			 !R_DemoReadInt( readDemo, size[2] ) ) {
			return false;
		}
		if ( size[0] < 1 || size[0] > 32768 || size[1] < 1 || size[1] > 32768 ) {
			return R_RejectRenderDemo( readDemo, va( "invalid crop size %d x %d", size[0], size[1] ) );
		}
		renderSystem->CropRenderSize( size[0], size[1], size[2] != 0 );
		break;

	case DC_UNCROP_RENDER:
		if ( r_showDemo.GetBool() ) {
			common->Printf( "DC_UNCROP\n" );
		}
		renderSystem->UnCrop();
		break;

	case DC_GUI_MODEL:
		if ( r_showDemo.GetBool() ) {
			common->Printf( "DC_GUI_MODEL\n" );
		}
		if ( !tr.demoGuiModel->ReadFromDemo( readDemo ) ) {
			return false;
		}
		break;

	case DC_DEFINE_MODEL:
		{
	// jmarshall: demos broken
		//idRenderModel	*model = renderModelManager->AllocModel();
		//model->ReadFromDemoFile( session->readDemo );
		//// add to model manager, so we can find it
		//renderModelManager->AddModel( model );
		//
		//// save it in the list to free when clearing this map
		//localModels.Append( model );
		//
		//if ( r_showDemo.GetBool() ) {
		//	common->Printf( "DC_DEFINE_MODEL\n" );
		//}
	// jmarshall end
		break;
		}
	case DC_SET_PORTAL_STATE:
		{
			int		data[2];
			if ( !R_DemoReadInt( readDemo, data[0] ) ||
				 !R_DemoReadInt( readDemo, data[1] ) ) {
				return false;
			}
			if ( data[0] < 0 || data[0] > numInterAreaPortals ) {
				return R_RejectRenderDemo( readDemo, va( "portal %d is out of range", data[0] ) );
			}
			SetPortalState( data[0], data[1] );
			if ( r_showDemo.GetBool() ) {
				common->Printf( "DC_SET_PORTAL_STATE: %i %i\n", data[0], data[1] );
			}
		}
		
		break;
	case DC_END_FRAME:
		return true;

	case DC_GUI_SURFACE:
		if ( session->renderdemoVersion < OPENQ4_RENDERDEMO_SESSION_VERSION ) {
			return R_RejectRenderDemo( readDemo, "in-world GUI record before render demo v11" );
		}
		if ( !ReadGuiSurface() ) {
			return false;
		}
		break;

	default:
		return R_RejectRenderDemo( readDemo, va( "invalid render command %d", static_cast<int>( dc ) ) );
	}

	return false;
}

/*
================
WriteLoadMap
================
*/
void	idRenderWorldLocal::WriteLoadMap() {

	// only the main renderWorld writes stuff to demos, not the wipes or
	// menu renders
	if ( this != session->rw ) {
		return;
	}

	session->writeDemo->WriteInt( DS_RENDER );
	session->writeDemo->WriteInt( DC_LOADMAP );
	// no entity's joints are predicted across a map load or segment start
	ResetDemoJointHistory( -1 );

	demoHeader_t	header;
	memset( &header, 0, sizeof( header ) );
	strncpy( header.mapname, mapName.c_str(), sizeof( header.mapname ) - 1 );
	header.version = OPENQ4_RENDERDEMO_CURRENT_VERSION;
	header.sizeofRenderEntity = sizeof( renderEntity_t );
	header.sizeofRenderLight = sizeof( renderLight_t );
	session->writeDemo->WriteInt( header.version );
	session->writeDemo->WriteInt( header.sizeofRenderEntity );
	session->writeDemo->WriteInt( header.sizeofRenderLight );
	for ( int i = 0; i < 256; i++ )
		session->writeDemo->WriteChar( header.mapname[i] );
	
	if ( r_showDemo.GetBool() ) {
		common->Printf( "write DC_LOADMAP: %s\n", mapName.c_str() );
	}
}

/*
================
WriteVisibleDefs

================
*/
void	idRenderWorldLocal::WriteVisibleDefs( const viewDef_t *viewDef ) {
	// only the main renderWorld writes stuff to demos, not the wipes or
	// menu renders
	if ( this != session->rw ) {
		return;
	}

	// make sure all necessary entities and lights are updated
	for ( viewEntity_t *viewEnt = viewDef->viewEntitys ; viewEnt ; viewEnt = viewEnt->next ) {
		idRenderEntityLocal *ent = viewEnt->entityDef;

		if ( ent->archived ) {
			// still up to date
			continue;
		}

		// write it out
		WriteRenderEntity( ent->index, &ent->parms );
		ent->archived = true;
	}

	for ( viewLight_t *viewLight = viewDef->viewLights ; viewLight ; viewLight = viewLight->next ) {
		idRenderLightLocal *light = viewLight->lightDef;

		if ( light->archived ) {
			// still up to date
			continue;
		}
		// write it out
		WriteRenderLight( light->index, &light->parms );
		light->archived = true;
	}

	for ( int i = 0; i < effectsDef.Num(); ++i ) {
		rvRenderEffectLocal *effectDef = effectsDef[i];
		if ( effectDef == NULL ) {
			continue;
		}
		if ( effectDef->visibleCount != tr.viewCount || effectDef->archived ) {
			continue;
		}

		WriteRenderEffect( effectDef->index, effectDef );
		effectDef->archived = true;
	}
}


/*
================
WriteRenderView
================
*/
void	idRenderWorldLocal::WriteRenderView( const renderView_t *renderView ) {
	// only the main renderWorld writes stuff to demos, not the wipes or
	// menu renders
	if ( this != session->rw ) {
		return;
	}
	
	// write the actual view command
	session->writeDemo->WriteInt( DS_RENDER );
	session->writeDemo->WriteInt( DC_RENDERVIEW );
	R_WriteDemoRenderViewData( session->writeDemo, renderView );
	
	if ( r_showDemo.GetBool() ) {
		common->Printf( "write DC_RENDERVIEW: %i\n", renderView->time );
	}
}

/*
================
WriteFreeEntity
================
*/
void	idRenderWorldLocal::WriteFreeEntity( qhandle_t handle ) {

	// only the main renderWorld writes stuff to demos, not the wipes or
	// menu renders
	if ( this != session->rw ) {
		return;
	}

	session->writeDemo->WriteInt( DS_RENDER );
	session->writeDemo->WriteInt( DC_DELETE_ENTITYDEF );
	session->writeDemo->WriteInt( handle );

	if ( r_showDemo.GetBool() ) {
		common->Printf( "write DC_DELETE_ENTITYDEF: %i\n", handle );
	}
}

/*
================
WriteFreeLightEntity
================
*/
void	idRenderWorldLocal::WriteFreeLight( qhandle_t handle ) {

	// only the main renderWorld writes stuff to demos, not the wipes or
	// menu renders
	if ( this != session->rw ) {
		return;
	}

	session->writeDemo->WriteInt( DS_RENDER );
	session->writeDemo->WriteInt( DC_DELETE_LIGHTDEF );
	session->writeDemo->WriteInt( handle );

	if ( r_showDemo.GetBool() ) {
		common->Printf( "write DC_DELETE_LIGHTDEF: %i\n", handle );
	}
}

/*
================
WriteFreeEffect
================
*/
void	idRenderWorldLocal::WriteFreeEffect( qhandle_t handle ) {

	// only the main renderWorld writes stuff to demos, not the wipes or
	// menu renders
	if ( this != session->rw ) {
		return;
	}

	session->writeDemo->WriteInt( DS_RENDER );
	session->writeDemo->WriteInt( DC_DELETE_EFFECTDEF );
	session->writeDemo->WriteInt( handle );

	if ( r_showDemo.GetBool() ) {
		common->Printf( "write DC_DELETE_EFFECTDEF: %i\n", handle );
	}
}

/*
================
WriteStopEffect
================
*/
void	idRenderWorldLocal::WriteStopEffect( qhandle_t handle ) {

	// only the main renderWorld writes stuff to demos, not the wipes or
	// menu renders
	if ( this != session->rw ) {
		return;
	}

	session->writeDemo->WriteInt( DS_RENDER );
	session->writeDemo->WriteInt( DC_STOP_EFFECTDEF );
	session->writeDemo->WriteInt( handle );

	if ( r_showDemo.GetBool() ) {
		common->Printf( "write DC_STOP_EFFECTDEF: %i\n", handle );
	}
}

/*
================
WriteRenderLight
================
*/
void	idRenderWorldLocal::WriteRenderLight( qhandle_t handle, const renderLight_t *light ) {

	// only the main renderWorld writes stuff to demos, not the wipes or
	// menu renders
	if ( this != session->rw ) {
		return;
	}

	session->writeDemo->WriteInt( DS_RENDER );
	session->writeDemo->WriteInt( DC_UPDATE_LIGHTDEF );
	session->writeDemo->WriteInt( handle );

	session->writeDemo->WriteMat3( light->axis );
	session->writeDemo->WriteVec3( light->origin );
	session->writeDemo->WriteInt( light->suppressLightInViewID );
	session->writeDemo->WriteInt( light->allowLightInViewID );
	session->writeDemo->WriteBool( light->noShadows );
	session->writeDemo->WriteBool( light->noSpecular );
	session->writeDemo->WriteBool( light->pointLight );
	session->writeDemo->WriteBool( light->parallel );
	session->writeDemo->WriteVec3( light->lightRadius );
	session->writeDemo->WriteVec3( light->lightCenter );
	session->writeDemo->WriteVec3( light->target );
	session->writeDemo->WriteVec3( light->right );
	session->writeDemo->WriteVec3( light->up );
	session->writeDemo->WriteVec3( light->start );
	session->writeDemo->WriteVec3( light->end );
	idRenderModel *prelightModel = R_SanitizePrelightModelPointer( light->prelightModel );
	session->writeDemo->WriteBool( prelightModel != NULL );
	session->writeDemo->WriteInt( light->lightId );
	session->writeDemo->WriteBool( light->shader != NULL );
	for ( int i = 0; i < MAX_ENTITY_SHADER_PARMS; i++)
		session->writeDemo->WriteFloat( light->shaderParms[i] );
	session->writeDemo->WriteInt( light->referenceSoundHandle );
	session->writeDemo->WriteInt( light->referenceSound );

	if ( prelightModel ) {
		session->writeDemo->WriteHashString( prelightModel->Name() );
	}
	if ( light->shader ) {
		session->writeDemo->WriteHashString( light->shader->GetName() );
	}
	if ( light->referenceSound ) {
		//int	index = light->referenceSound->Index();
		//session->writeDemo->WriteInt( index );
	}

	// Preserve Quake 4-era light flags without disturbing the older serialized prefix.
	session->writeDemo->WriteBool( light->noDynamicShadows );
	session->writeDemo->WriteBool( light->globalLight );
	session->writeDemo->WriteFloat( light->detailLevel );

	if ( r_showDemo.GetBool() ) {
		common->Printf( "write DC_UPDATE_LIGHTDEF: %i\n", handle );
	}
}

/*
================
ReadRenderLight
================
*/
bool	idRenderWorldLocal::ReadRenderLight( ) {
	renderLight_t	light = {};
	int				index;
	bool			hasPrelightModel = false;
	bool			hasShader = false;

	light.detailLevel = DEFAULT_LIGHT_DETAIL_LEVEL;

	if ( !R_DemoReadInt( session->readDemo, index ) ) {
		return false;
	}
	if ( index < 0 || index > LUDICROUS_INDEX ) {
		return R_RejectRenderDemo( session->readDemo, va( "light index %d is out of range", index ) );
	}

	if ( !R_DemoReadMat3( session->readDemo, light.axis ) ||
		 !R_DemoReadVec3( session->readDemo, light.origin ) ||
		 !R_DemoReadInt( session->readDemo, light.suppressLightInViewID ) ||
		 !R_DemoReadInt( session->readDemo, light.allowLightInViewID ) ||
		 !R_DemoReadBool( session->readDemo, light.noShadows ) ||
		 !R_DemoReadBool( session->readDemo, light.noSpecular ) ||
		 !R_DemoReadBool( session->readDemo, light.pointLight ) ||
		 !R_DemoReadBool( session->readDemo, light.parallel ) ||
		 !R_DemoReadVec3( session->readDemo, light.lightRadius ) ||
		 !R_DemoReadVec3( session->readDemo, light.lightCenter ) ||
		 !R_DemoReadVec3( session->readDemo, light.target ) ||
		 !R_DemoReadVec3( session->readDemo, light.right ) ||
		 !R_DemoReadVec3( session->readDemo, light.up ) ||
		 !R_DemoReadVec3( session->readDemo, light.start ) ||
		 !R_DemoReadVec3( session->readDemo, light.end ) ) {
		return false;
	}
	if ( session->renderdemoVersion >= OPENQ4_RENDERDEMO_POINTER_FREE_VERSION ) {
		if ( !R_DemoReadBool( session->readDemo, hasPrelightModel ) ) {
			return false;
		}
	} else {
		int legacyPrelightModel = 0;
		if ( !R_DemoReadInt( session->readDemo, legacyPrelightModel ) ) {
			return false;
		}
		hasPrelightModel = ( legacyPrelightModel != 0 );
	}
	if ( !R_DemoReadInt( session->readDemo, light.lightId ) ) {
		return false;
	}
	if ( session->renderdemoVersion >= OPENQ4_RENDERDEMO_POINTER_FREE_VERSION ) {
		if ( !R_DemoReadBool( session->readDemo, hasShader ) ) {
			return false;
		}
	} else {
		int legacyShader = 0;
		if ( !R_DemoReadInt( session->readDemo, legacyShader ) ) {
			return false;
		}
		hasShader = ( legacyShader != 0 );
	}
	for ( int i = 0; i < MAX_ENTITY_SHADER_PARMS; i++ ) {
		if ( !R_DemoReadFloat( session->readDemo, light.shaderParms[i] ) ) {
			return false;
		}
	}
	if ( session->renderdemoVersion >= OPENQ4_RENDERDEMO_ENTITY_EXTRAS_VERSION ) {
		if ( !R_DemoReadInt( session->readDemo, light.referenceSoundHandle ) ) {
			return false;
		}
	}
	if ( !R_DemoReadInt( session->readDemo, light.referenceSound ) ) {
		return false;
	}
	if ( hasPrelightModel ) {
		light.prelightModel = renderModelManager->FindModel( session->readDemo->ReadHashString() );
		if ( !session->readDemo->IsOpen() ) {
			return false;
		}
	}
	if ( hasShader ) {
		light.shader = declManager->FindMaterial( session->readDemo->ReadHashString() );
		if ( !session->readDemo->IsOpen() ) {
			return false;
		}
	}
	if ( session->renderdemoVersion >= OPENQ4_RENDERDEMO_LIGHT_EXTRAS_VERSION ) {
		if ( !R_DemoReadBool( session->readDemo, light.noDynamicShadows ) ||
			 !R_DemoReadBool( session->readDemo, light.globalLight ) ||
			 !R_DemoReadFloat( session->readDemo, light.detailLevel ) ) {
			return false;
		}
	}
	UpdateLightDef( index, &light );

	if ( r_showDemo.GetBool() ) {
		common->Printf( "DC_UPDATE_LIGHTDEF: %i\n", index );
	}
	return true;
}

/*
================
WriteRenderEffect
================
*/
void	idRenderWorldLocal::WriteRenderEffect( qhandle_t handle, const rvRenderEffectLocal *effectDef ) {

	// only the main renderWorld writes stuff to demos, not the wipes or
	// menu renders
	if ( this != session->rw || effectDef == NULL ) {
		return;
	}

	session->writeDemo->WriteInt( DS_RENDER );
	session->writeDemo->WriteInt( DC_UPDATE_EFFECTDEF );
	session->writeDemo->WriteInt( handle );
	R_WriteDemoEffectData( session->writeDemo, effectDef );

	if ( r_showDemo.GetBool() ) {
		const char *effectName = effectDef->parms.declEffect ? effectDef->parms.declEffect->GetName() : "NULL";
		common->Printf( "write DC_UPDATE_EFFECTDEF: %i = %s\n", handle, effectName );
	}
}

/*
================
ReadRenderEffect
================
*/
bool	idRenderWorldLocal::ReadRenderEffect() {
	renderEffect_t		effect = {};
	int					index = -1;
	int					effectGameTime = 0;
	bool				stopped = false;

	if ( !R_DemoReadInt( session->readDemo, index ) ) {
		return false;
	}
	if ( index < 0 || index > LUDICROUS_INDEX ) {
		return R_RejectRenderDemo( session->readDemo, va( "effect index %d is out of range", index ) );
	}
	if ( !R_ReadDemoEffectData( session->readDemo, &effect, &effectGameTime, &stopped ) ) {
		return false;
	}

	UpdateEffectDef( index, &effect, effectGameTime );

	rvRenderEffectLocal *def = ( index >= 0 && index < effectsDef.Num() ) ? effectsDef[index] : NULL;
	if ( def != NULL ) {
		bse->SetEffectStopped( def, stopped );
	}

	if ( r_showDemo.GetBool() ) {
		const char *effectName = effect.declEffect ? effect.declEffect->GetName() : "NULL";
		common->Printf( "DC_UPDATE_EFFECTDEF: %i = %s\n", index, effectName );
	}
	return true;
}

/*
================
WriteRenderEntity
================
*/
void	idRenderWorldLocal::WriteRenderEntity( qhandle_t handle, const renderEntity_t *ent ) {
	idRenderEntityLocal *localEnt;

	// only the main renderWorld writes stuff to demos, not the wipes or
	// menu renders
	if ( this != session->rw ) {
		return;
	}

	localEnt = ( handle >= 0 && handle < entityDefs.Num() ) ? entityDefs[handle] : NULL;

	session->writeDemo->WriteInt( DS_RENDER );
	session->writeDemo->WriteInt( DC_UPDATE_ENTITYDEF );
	session->writeDemo->WriteInt( handle );
	
	session->writeDemo->WriteBool( ent->hModel != NULL );
	session->writeDemo->WriteInt( ent->entityNum );
	session->writeDemo->WriteInt( ent->bodyId );
	session->writeDemo->WriteVec3( ent->bounds[0] );
	session->writeDemo->WriteVec3( ent->bounds[1] );
	session->writeDemo->WriteBool( false );
	session->writeDemo->WriteBool( false );
	session->writeDemo->WriteInt( ent->suppressSurfaceInViewID );
	session->writeDemo->WriteInt( ent->suppressShadowInViewID );
	session->writeDemo->WriteInt( ent->suppressShadowInLightID );
	session->writeDemo->WriteInt( ent->allowSurfaceInViewID );
	session->writeDemo->WriteVec3( ent->origin );
	session->writeDemo->WriteMat3( ent->axis );
	session->writeDemo->WriteBool( ent->customShader != NULL );
	session->writeDemo->WriteBool( ent->referenceShader != NULL );
	session->writeDemo->WriteBool( ent->customSkin != NULL );
	session->writeDemo->WriteInt( ent->referenceSound );
	for ( int i = 0; i < MAX_ENTITY_SHADER_PARMS; i++ )
		session->writeDemo->WriteFloat( ent->shaderParms[i] );
	session->writeDemo->WriteVec4( ent->outlineColor );
	session->writeDemo->WriteVec4( ent->rimlightColor );
	session->writeDemo->WriteVec4( ent->brightSkinColor );
	session->writeDemo->WriteFloat( ent->outlineWidth );
	session->writeDemo->WriteInt( ent->outlineFlags );
	session->writeDemo->WriteVec4( ent->flatDiffuseColor );
	session->writeDemo->WriteInt( ent->flatDiffuseFlags );
	for ( int i = 0; i < MAX_RENDERENTITY_GUI; i++ )
		session->writeDemo->WriteBool( ent->gui[i] != NULL );
	session->writeDemo->WriteBool( ent->remoteRenderView != NULL );
	if ( ent->remoteRenderView != NULL ) {
		R_WriteDemoRenderViewData( session->writeDemo, ent->remoteRenderView );
	}
	session->writeDemo->WriteInt( ent->numJoints );
	session->writeDemo->WriteFloat( ent->modelDepthHack );
	session->writeDemo->WriteBool( ent->noSelfShadow );
	session->writeDemo->WriteBool( ent->noShadow );
	session->writeDemo->WriteBool( ent->noDynamicInteractions );
	//session->writeDemo->WriteBool( ent->weaponDepthHack );
	session->writeDemo->WriteInt( ent->forceUpdate );

	if ( ent->customShader ) {
		session->writeDemo->WriteHashString( ent->customShader->GetName() );
	}
	if ( ent->customSkin ) {
		session->writeDemo->WriteHashString( ent->customSkin->GetName() );
	}
	if ( ent->hModel ) {
		session->writeDemo->WriteHashString( ent->hModel->Name() );
	}
	if ( ent->referenceShader ) {
		session->writeDemo->WriteHashString( ent->referenceShader->GetName() );
	}
	if ( ent->numJoints ) {
		WriteDemoJoints( handle, ent->joints, ent->numJoints );
	}

	session->writeDemo->WriteInt( ent->weaponDepthHackInViewID );
	session->writeDemo->WriteFloat( ent->shadowLODDistance );
	session->writeDemo->WriteInt( ent->suppressLOD );
	session->writeDemo->WriteInt( ent->referenceSoundHandle );
	session->writeDemo->WriteBool( ent->overlayShader != NULL );
	if ( ent->overlayShader != NULL ) {
		session->writeDemo->WriteHashString( ent->overlayShader->GetName() );
	}

	session->writeDemo->WriteBool( localEnt != NULL && localEnt->decals != NULL );
	if ( localEnt != NULL && localEnt->decals != NULL ) {
		localEnt->decals->WriteToDemoFile( session->writeDemo );
	}
	session->writeDemo->WriteBool( localEnt != NULL && localEnt->overlay != NULL );
	if ( localEnt != NULL && localEnt->overlay != NULL ) {
		localEnt->overlay->WriteToDemoFile( session->writeDemo );
	}

#ifdef WRITE_GUIS
	if ( ent->gui ) {
		ent->gui->WriteToDemoFile( session->writeDemo );
	}
	if ( ent->gui2 ) {
		ent->gui2->WriteToDemoFile( session->writeDemo );
	}
	if ( ent->gui3 ) {
		ent->gui3->WriteToDemoFile( session->writeDemo );
	}
#endif

	// RENDERDEMO_VERSION >= 2 ( legacy engine 1.2 )
	//session->writeDemo->WriteInt( ent->timeGroup );
	//session->writeDemo->WriteInt( ent->xrayIndex );

	if ( r_showDemo.GetBool() ) {
		common->Printf( "write DC_UPDATE_ENTITYDEF: %i = %s\n", handle, ent->hModel ? ent->hModel->Name() : "NULL" );
	}
}

/*
================
ReadRenderEntity
================
*/
bool	idRenderWorldLocal::ReadRenderEntity() {
	renderEntity_t		ent = {};
	idRenderModelDecal *demoDecals = NULL;
	idRenderModelOverlay *demoOverlay = NULL;
	renderView_t		demoRemoteRenderView = {};
	bool				hasRemoteRenderView = false;
	bool				hasModel = false;
	bool				hasCallback = false;
	bool				hasCallbackData = false;
	bool				hasCustomShader = false;
	bool				hasReferenceShader = false;
	bool				hasCustomSkin = false;
	bool				hasGui[ MAX_RENDERENTITY_GUI ] = { false };
	int					index, i;

	if ( !R_DemoReadInt( session->readDemo, index ) ) {
		return false;
	}
	if ( index < 0 || index > LUDICROUS_INDEX ) {
		return R_RejectRenderDemo( session->readDemo, va( "entity index %d is out of range", index ) );
	}

	if ( session->renderdemoVersion >= OPENQ4_RENDERDEMO_POINTER_FREE_VERSION ) {
		if ( !R_DemoReadBool( session->readDemo, hasModel ) ) {
			return false;
		}
	} else {
		int legacyModel = 0;
		if ( !R_DemoReadInt( session->readDemo, legacyModel ) ) {
			return false;
		}
		hasModel = ( legacyModel != 0 );
	}
	if ( !R_DemoReadInt( session->readDemo, ent.entityNum ) ||
		 !R_DemoReadInt( session->readDemo, ent.bodyId ) ||
		 !R_DemoReadVec3( session->readDemo, ent.bounds[0] ) ||
		 !R_DemoReadVec3( session->readDemo, ent.bounds[1] ) ) {
		return false;
	}
	if ( session->renderdemoVersion >= OPENQ4_RENDERDEMO_POINTER_FREE_VERSION ) {
		// Render callbacks are process-local and cannot be replayed safely.
		if ( !R_DemoReadBool( session->readDemo, hasCallback ) ||
			 !R_DemoReadBool( session->readDemo, hasCallbackData ) ) {
			return false;
		}
	} else {
		int legacyCallback = 0;
		int legacyCallbackData = 0;
		if ( !R_DemoReadInt( session->readDemo, legacyCallback ) ||
			 !R_DemoReadInt( session->readDemo, legacyCallbackData ) ) {
			return false;
		}
		hasCallback = ( legacyCallback != 0 );
		hasCallbackData = ( legacyCallbackData != 0 );
	}
	if ( !R_DemoReadInt( session->readDemo, ent.suppressSurfaceInViewID ) ||
		 !R_DemoReadInt( session->readDemo, ent.suppressShadowInViewID ) ||
		 !R_DemoReadInt( session->readDemo, ent.suppressShadowInLightID ) ||
		 !R_DemoReadInt( session->readDemo, ent.allowSurfaceInViewID ) ||
		 !R_DemoReadVec3( session->readDemo, ent.origin ) ||
		 !R_DemoReadMat3( session->readDemo, ent.axis ) ) {
		return false;
	}
	if ( session->renderdemoVersion >= OPENQ4_RENDERDEMO_POINTER_FREE_VERSION ) {
		if ( !R_DemoReadBool( session->readDemo, hasCustomShader ) ||
			 !R_DemoReadBool( session->readDemo, hasReferenceShader ) ||
			 !R_DemoReadBool( session->readDemo, hasCustomSkin ) ) {
			return false;
		}
	} else {
		int legacyCustomShader = 0;
		int legacyReferenceShader = 0;
		int legacyCustomSkin = 0;
		if ( !R_DemoReadInt( session->readDemo, legacyCustomShader ) ||
			 !R_DemoReadInt( session->readDemo, legacyReferenceShader ) ||
			 !R_DemoReadInt( session->readDemo, legacyCustomSkin ) ) {
			return false;
		}
		hasCustomShader = ( legacyCustomShader != 0 );
		hasReferenceShader = ( legacyReferenceShader != 0 );
		hasCustomSkin = ( legacyCustomSkin != 0 );
	}
	if ( !R_DemoReadInt( session->readDemo, ent.referenceSound ) ) {
		return false;
	}
	for ( i = 0; i < MAX_ENTITY_SHADER_PARMS; i++ ) {
		if ( !R_DemoReadFloat( session->readDemo, ent.shaderParms[i] ) ) {
			return false;
		}
	}
	if ( session->renderdemoVersion >= OPENQ4_RENDERDEMO_VISIBILITY_EFFECTS_VERSION ) {
		if ( !R_DemoReadVec4( session->readDemo, ent.outlineColor ) ||
			 !R_DemoReadVec4( session->readDemo, ent.rimlightColor ) ||
			 ( session->renderdemoVersion >= OPENQ4_RENDERDEMO_BRIGHTSKIN_VERSION && !R_DemoReadVec4( session->readDemo, ent.brightSkinColor ) ) ||
			 !R_DemoReadFloat( session->readDemo, ent.outlineWidth ) ||
			 !R_DemoReadInt( session->readDemo, ent.outlineFlags ) ) {
			return false;
		}
	}
	if ( session->renderdemoVersion >= OPENQ4_RENDERDEMO_FLAT_DIFFUSE_VERSION ) {
		if ( !R_DemoReadVec4( session->readDemo, ent.flatDiffuseColor ) ||
			 !R_DemoReadInt( session->readDemo, ent.flatDiffuseFlags ) ) {
			return false;
		}
	}
	for ( i = 0; i < MAX_RENDERENTITY_GUI; i++ ) {
		if ( session->renderdemoVersion >= OPENQ4_RENDERDEMO_POINTER_FREE_VERSION ) {
			if ( !R_DemoReadBool( session->readDemo, hasGui[i] ) ) {
				return false;
			}
		} else {
			int legacyGui = 0;
			if ( !R_DemoReadInt( session->readDemo, legacyGui ) ) {
				return false;
			}
			hasGui[i] = ( legacyGui != 0 );
		}
	}
	if ( session->renderdemoVersion >= OPENQ4_RENDERDEMO_REMOTE_VIEW_VERSION ) {
		if ( !R_DemoReadBool( session->readDemo, hasRemoteRenderView ) ) {
			return false;
		}
		if ( hasRemoteRenderView && !R_ReadDemoRenderViewData( session->readDemo, &demoRemoteRenderView, session->renderdemoVersion ) ) {
			return false;
		}
	} else {
		int legacyRemoteRenderView = 0;
		if ( !R_DemoReadInt( session->readDemo, legacyRemoteRenderView ) ) {
			return false;
		}
		hasRemoteRenderView = false;
	}
	if ( !R_DemoReadInt( session->readDemo, ent.numJoints ) ) {
		return false;
	}
	if ( ent.numJoints < 0 || ent.numJoints > DEMO_MAX_ENTITY_JOINTS ) {
		return R_RejectRenderDemo( session->readDemo, va( "entity joint count %d is out of range", ent.numJoints ) );
	}
	if ( session->renderdemoVersion < OPENQ4_RENDERDEMO_POINTER_FREE_VERSION ) {
		int legacyJoints = 0;
		if ( !R_DemoReadInt( session->readDemo, legacyJoints ) ) {
			return false;
		}
	}
	if ( !R_DemoReadFloat( session->readDemo, ent.modelDepthHack ) ||
		 !R_DemoReadBool( session->readDemo, ent.noSelfShadow ) ||
		 !R_DemoReadBool( session->readDemo, ent.noShadow ) ||
		 !R_DemoReadBool( session->readDemo, ent.noDynamicInteractions ) ) {
		return false;
	}
	{
		int forceUpdate;
		if ( !R_DemoReadInt( session->readDemo, forceUpdate ) ) {
			return false;
		}
		ent.forceUpdate = ( forceUpdate != 0 );
	}
	ent.callback = NULL;
	if ( hasCustomShader ) {
		ent.customShader = declManager->FindMaterial( session->readDemo->ReadHashString() );
		if ( !session->readDemo->IsOpen() ) {
			return false;
		}
	}
	if ( hasCustomSkin ) {
		ent.customSkin = declManager->FindSkin( session->readDemo->ReadHashString() );
		if ( !session->readDemo->IsOpen() ) {
			return false;
		}
	}
	if ( hasModel ) {
		ent.hModel = renderModelManager->FindModel( session->readDemo->ReadHashString() );
		if ( !session->readDemo->IsOpen() ) {
			return false;
		}
	}
	if ( hasReferenceShader ) {
		ent.referenceShader = declManager->FindMaterial( session->readDemo->ReadHashString() );
		if ( !session->readDemo->IsOpen() ) {
			return false;
		}
	}
	if ( ent.numJoints ) {
		ent.joints = (idJointMat *)Mem_Alloc16( ent.numJoints * sizeof( ent.joints[0] ) );
		if ( !ReadDemoJoints( index, ent.joints, ent.numJoints ) ) {
			R_FreeRenderDemoEntityPayload( ent, demoDecals, demoOverlay );
			return false;
		}
	}

	if ( hasRemoteRenderView ) {
		idRenderEntityLocal *def = ( index >= 0 && index < entityDefs.Num() ) ? entityDefs[index] : NULL;
		if ( def != NULL ) {
			if ( def->demoRemoteRenderView == NULL ) {
				def->demoRemoteRenderView = new renderView_t;
			}
			*def->demoRemoteRenderView = demoRemoteRenderView;
			def->hasDemoRemoteRenderView = true;
			ent.remoteRenderView = def->demoRemoteRenderView;
		} else {
			ent.remoteRenderView = &demoRemoteRenderView;
		}
	} else {
		ent.remoteRenderView = NULL;
	}

	if ( session->renderdemoVersion >= OPENQ4_RENDERDEMO_ENTITY_EXTRAS_VERSION ) {
		bool hasOverlayShader = false;
		bool hasDecals = false;
		bool hasOverlay = false;

		if ( !R_DemoReadInt( session->readDemo, ent.weaponDepthHackInViewID ) ||
			 !R_DemoReadFloat( session->readDemo, ent.shadowLODDistance ) ||
			 !R_DemoReadInt( session->readDemo, ent.suppressLOD ) ||
			 !R_DemoReadInt( session->readDemo, ent.referenceSoundHandle ) ||
			 !R_DemoReadBool( session->readDemo, hasOverlayShader ) ) {
			R_FreeRenderDemoEntityPayload( ent, demoDecals, demoOverlay );
			return false;
		}
		if ( hasOverlayShader ) {
			ent.overlayShader = declManager->FindMaterial( session->readDemo->ReadHashString() );
			if ( !session->readDemo->IsOpen() ) {
				R_FreeRenderDemoEntityPayload( ent, demoDecals, demoOverlay );
				return false;
			}
		}

		if ( !R_DemoReadBool( session->readDemo, hasDecals ) ) {
			R_FreeRenderDemoEntityPayload( ent, demoDecals, demoOverlay );
			return false;
		}
		if ( hasDecals ) {
			demoDecals = idRenderModelDecal::Alloc();
			if ( !demoDecals->ReadFromDemoFile( session->readDemo ) ) {
				R_FreeRenderDemoEntityPayload( ent, demoDecals, demoOverlay );
				return false;
			}
		}

		if ( !R_DemoReadBool( session->readDemo, hasOverlay ) ) {
			R_FreeRenderDemoEntityPayload( ent, demoDecals, demoOverlay );
			return false;
		}
		if ( hasOverlay ) {
			demoOverlay = idRenderModelOverlay::Alloc();
			if ( !demoOverlay->ReadFromDemoFile( session->readDemo ) ) {
				R_FreeRenderDemoEntityPayload( ent, demoDecals, demoOverlay );
				return false;
			}
		}
	}

	ent.callbackData = NULL;

	if ( ent.hModel == NULL ) {
		R_FreeRenderDemoEntityPayload( ent, demoDecals, demoOverlay );
		return R_RejectRenderDemo( session->readDemo, "entity update has no replayable model" );
	}

	for ( i = 0; i < MAX_RENDERENTITY_GUI; i++ ) {
		if ( hasGui[ i ] ) {
			ent.gui[ i ] = uiManager->Alloc();
#ifdef WRITE_GUIS
			ent.gui[ i ]->ReadFromDemoFile( session->readDemo );
#endif
		}
	}

	// >= legacy engine v1.2 only
	//if ( session->renderdemoVersion >= 2 ) {
	//	session->readDemo->ReadInt( ent.timeGroup );
	//	session->readDemo->ReadInt( ent.xrayIndex );
	//} else {
	//	ent.timeGroup = 0;
	//	ent.xrayIndex = 0;
	//}

	UpdateEntityDef( index, &ent );

	idRenderEntityLocal *def = ( index >= 0 && index < entityDefs.Num() ) ? entityDefs[index] : NULL;
	if ( def != NULL ) {
		if ( hasRemoteRenderView ) {
			if ( def->demoRemoteRenderView == NULL ) {
				def->demoRemoteRenderView = new renderView_t;
			}
			*def->demoRemoteRenderView = demoRemoteRenderView;
			def->parms.remoteRenderView = def->demoRemoteRenderView;
			def->hasDemoRemoteRenderView = true;
		} else {
			if ( def->demoRemoteRenderView != NULL ) {
				delete def->demoRemoteRenderView;
				def->demoRemoteRenderView = NULL;
			}
			def->parms.remoteRenderView = NULL;
			def->hasDemoRemoteRenderView = false;
		}
	}

	if ( session->renderdemoVersion >= OPENQ4_RENDERDEMO_ENTITY_EXTRAS_VERSION ) {
		if ( def != NULL ) {
			R_FreeRenderDemoDecalChain( def->decals );
			if ( def->overlay != NULL ) {
				idRenderModelOverlay::Free( def->overlay );
			}

			def->decals = demoDecals;
			def->overlay = demoOverlay;
			demoDecals = NULL;
			demoOverlay = NULL;
		}
	}

	if ( demoDecals != NULL ) {
		R_FreeRenderDemoDecalChain( demoDecals );
	}
	if ( demoOverlay != NULL ) {
		idRenderModelOverlay::Free( demoOverlay );
	}

	if ( r_showDemo.GetBool() ) {
		common->Printf( "DC_UPDATE_ENTITYDEF: %i = %s\n", index, ent.hModel ? ent.hModel->Name() : "NULL" );
	}
	return true;
}
