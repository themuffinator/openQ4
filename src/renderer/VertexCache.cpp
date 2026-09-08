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
#include "RendererUpload.h"


static const int	FRAME_MEMORY_BYTES = 0x200000;
static const int	EXPAND_HEADERS = 1024;

idCVar idVertexCache::r_showVertexCache( "r_showVertexCache", "0", CVAR_INTEGER|CVAR_RENDERER, "" );
idCVar idVertexCache::r_vertexBufferMegs( "r_vertexBufferMegs", "32", CVAR_INTEGER|CVAR_RENDERER, "" );

idVertexCache		vertexCache;

// shadow copies of the GL array/element buffer bindings so the legacy backend
// can skip redundant glBindBufferARB calls; 0xFFFFFFFF means unknown
static const GLuint VERTCACHE_BIND_UNKNOWN = 0xFFFFFFFFu;
static GLuint vc_boundArrayBuffer = VERTCACHE_BIND_UNKNOWN;
static GLuint vc_boundIndexBuffer = VERTCACHE_BIND_UNKNOWN;

/*
==============
idVertexCache::BindArrayBuffer
==============
*/
void idVertexCache::BindArrayBuffer( GLuint vbo ) {
	if ( glBindBufferARB == NULL ) {
		vc_boundArrayBuffer = VERTCACHE_BIND_UNKNOWN;
		return;
	}
	if ( vbo == vc_boundArrayBuffer && r_useRedundantStateFiltering.GetBool() ) {
		return;
	}
	glBindBufferARB( GL_ARRAY_BUFFER_ARB, vbo );
	vc_boundArrayBuffer = vbo;
}

/*
==============
idVertexCache::BindIndexBuffer
==============
*/
void idVertexCache::BindIndexBuffer( GLuint vbo ) {
	if ( glBindBufferARB == NULL ) {
		vc_boundIndexBuffer = VERTCACHE_BIND_UNKNOWN;
		return;
	}
	if ( vbo == vc_boundIndexBuffer && r_useRedundantStateFiltering.GetBool() ) {
		return;
	}
	glBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, vbo );
	vc_boundIndexBuffer = vbo;
}

/*
==============
idVertexCache::InvalidateBufferBindings
==============
*/
void idVertexCache::InvalidateBufferBindings() {
	vc_boundArrayBuffer = VERTCACHE_BIND_UNKNOWN;
	vc_boundIndexBuffer = VERTCACHE_BIND_UNKNOWN;
}

/*
==============
R_ListVertexCache_f
==============
*/
static void R_ListVertexCache_f( const idCmdArgs &args ) {
	vertexCache.List();
}

/*
==============
idVertexCache::ActuallyFree
==============
*/
void idVertexCache::ActuallyFree( vertCache_t *block ) {
	if (!block) {
		common->Error( "idVertexCache Free: NULL pointer" );
	}

	if ( block->user ) {
		// let the owner know we have purged it
		*block->user = NULL;
		block->user = NULL;
	}

	// temp blocks are in a shared space that won't be freed
	if ( block->tag != TAG_TEMP ) {
		staticAllocTotal -= block->size;
		staticCountTotal--;

		if ( block->vbo ) {
			R_RendererUpload_FreeStaticBuffer( block->vbo, block->size, block->indexBuffer );
		} else if ( block->virtMem ) {
			Mem_Free( block->virtMem );
			block->virtMem = NULL;
		}
	}
	block->tag = TAG_FREE;		// mark as free

	// unlink stick it back on the free list
	block->next->prev = block->prev;
	block->prev->next = block->next;

#if 1
	// stick it on the front of the free list so it will be reused immediately
	block->next = freeStaticHeaders.next;
	block->prev = &freeStaticHeaders;
#else
	// stick it on the back of the free list so it won't be reused soon (just for debugging)
	block->next = &freeStaticHeaders;
	block->prev = freeStaticHeaders.prev;
#endif

	block->next->prev = block;
	block->prev->next = block;
}

/*
==============
idVertexCache::Position

this will be a real pointer with virtual memory,
but it will be an int offset cast to a pointer with
ARB_vertex_buffer_object

The ARB_vertex_buffer_object will be bound
==============
*/
void *idVertexCache::Position( vertCache_t *buffer ) {
	if ( !buffer || buffer->tag == TAG_FREE ) {
		common->FatalError( "idVertexCache::Position: bad vertCache_t" );
	}

	// the ARB vertex object just uses an offset
	if ( buffer->vbo ) {
		if ( r_showVertexCache.GetInteger() == 2 ) {
			if ( buffer->tag == TAG_TEMP ) {
				common->Printf( "GL_ARRAY_BUFFER_ARB = %i + %i (%i bytes)\n", buffer->vbo, buffer->offset, buffer->size ); 
			} else {
				common->Printf( "GL_ARRAY_BUFFER_ARB = %i (%i bytes)\n", buffer->vbo, buffer->size ); 
			}
		}
		if ( buffer->indexBuffer ) {
			BindIndexBuffer( buffer->vbo );
		} else {
			BindArrayBuffer( buffer->vbo );
		}
		return reinterpret_cast<void *>( static_cast<uintptr_t>( buffer->offset ) );
	}

	// Client-memory array and index pointers are only interpreted as CPU
	// addresses when both buffer-object targets are zero. Clear both here:
	// Apple GL 2.1 deliberately uses a CPU-backed cache, r_useIndexBuffers is
	// disabled in that mode, and another pass may still have left an EBO bound.
	BindArrayBuffer( 0 );
	BindIndexBuffer( 0 );

	// virtual memory is a real pointer
	return (void *)((byte *)buffer->virtMem + buffer->offset);
}

void idVertexCache::UnbindIndex() {
	BindIndexBuffer( 0 );
}


//================================================================================

/*
===========
idVertexCache::ReclaimHeaderList

Moves every block on the list back onto freeStaticHeaders without touching GL.
Only valid during a re-Init after the GL context was destroyed: the buffer
names died with the old context, so they are dropped (never glDelete'd or
pooled) and zeroed so Alloc can't recycle a dead name into the new context.
===========
*/
void idVertexCache::ReclaimHeaderList( vertCache_t *list ) {
	while ( list->next != list ) {
		vertCache_t *block = list->next;

		if ( block->user ) {
			// let the owner know we have purged it
			*block->user = NULL;
			block->user = NULL;
		}
		if ( block->virtMem ) {
			Mem_Free( block->virtMem );
			block->virtMem = NULL;
		}
		block->vbo = 0;
		block->tag = TAG_FREE;

		block->next->prev = block->prev;
		block->prev->next = block->next;

		block->next = freeStaticHeaders.next;
		block->prev = &freeStaticHeaders;
		block->next->prev = block;
		block->prev->next = block;
	}
}

/*
===========
idVertexCache::ReclaimAllHeaders

Called when Init runs again after a full vid_restart. Re-pointing the list
heads would orphan every header in headerAllocator (which never reclaims
nodes), so instead put all blocks back onto the free lists. Headers are never
deallocated and temp headers stay segregated on the dynamic free list, so
stale cache pointers held by tri surfs keep their documented tag-check
behavior (see R_FreeTriSurfVertCache).
===========
*/
void idVertexCache::ReclaimAllHeaders() {
	ReclaimHeaderList( &deferredFreeList );
	ReclaimHeaderList( &staticHeaders );

	// the fixed temp buffer headers were unlinked from the lists at the
	// previous Init, so their next/prev are stale and must not be unlinked
	for ( int i = 0; i < NUM_VERTEX_FRAMES; i++ ) {
		vertCache_t *block = tempBuffers[i];
		if ( block == NULL ) {
			continue;
		}
		if ( block->virtMem ) {
			Mem_Free( block->virtMem );
			block->virtMem = NULL;
		}
		block->vbo = 0;
		block->user = NULL;
		block->tag = TAG_FREE;
		block->next = freeStaticHeaders.next;
		block->prev = &freeStaticHeaders;
		block->next->prev = block;
		block->prev->next = block;
		tempBuffers[i] = NULL;
	}

	// frame temp headers go back on the dynamic free list, the same wholesale
	// relink EndFrame does; their fields are fully rewritten when reused
	vertCache_t *block = dynamicHeaders.next;
	if ( block != &dynamicHeaders ) {
		block->prev = &freeDynamicHeaders;
		dynamicHeaders.prev->next = freeDynamicHeaders.next;
		freeDynamicHeaders.next->prev = dynamicHeaders.prev;
		freeDynamicHeaders.next = block;

		dynamicHeaders.next = dynamicHeaders.prev = &dynamicHeaders;
	}
}

/*
===========
idVertexCache::Init
===========
*/
void idVertexCache::Init() {
	cmdSystem->AddCommand( "listVertexCache", R_ListVertexCache_f, CMD_FL_RENDERER, "lists vertex cache" );

	if ( r_vertexBufferMegs.GetInteger() < 8 ) {
		r_vertexBufferMegs.SetInteger( 8 );
	}

	virtualMemory = false;
	InvalidateBufferBindings();

	// use ARB_vertex_buffer_object unless explicitly disabled
	if( r_useVertexBuffers.GetInteger() && glConfig.ARBVertexBufferObjectAvailable ) {
		common->Printf( "using ARB_vertex_buffer_object memory\n" );

		// r_useIndexBuffers defaults to 0, so index data normally stays in
		// client memory and glDrawElements receives a real pointer. That is
		// legal in a compatibility context and removed outright in a core
		// profile and in OpenGL ES, where a bound element-array buffer is the
		// only way to draw indexed geometry. Leaving it off there makes every
		// indexed draw fail with GL_INVALID_VALUE and render nothing --
		// measured on game/mcc_1, all 375 forward+ draws submitted with ibo=0
		// and the scene target read back as entirely zero.
		const bool clientMemoryIndicesIllegal =
			glConfig.backendCaps.profile == RENDERER_CONTEXT_PROFILE_CORE
			|| glConfig.backendCaps.profile == RENDERER_CONTEXT_PROFILE_ES;
		if ( clientMemoryIndicesIllegal && r_useIndexBuffers.GetInteger() < 2 ) {
			r_useIndexBuffers.SetInteger( 2 );
			common->Printf( "forcing r_useIndexBuffers 2: this profile cannot draw from client index memory\n" );
		}
	} else {
		virtualMemory = true;
		r_useIndexBuffers.SetBool( false );
		// Establish the client-memory invariant immediately. Bind helpers are
		// entry-point guarded, so this is also safe on implementations without
		// ARB_vertex_buffer_object.
		BindArrayBuffer( 0 );
		BindIndexBuffer( 0 );
		common->Printf( "WARNING: vertex array range in virtual memory (SLOW)\n" );
	}

	// initialize the cache memory blocks
	if ( tempBuffers[0] == NULL ) {
		freeStaticHeaders.next = freeStaticHeaders.prev = &freeStaticHeaders;
		staticHeaders.next = staticHeaders.prev = &staticHeaders;
		freeDynamicHeaders.next = freeDynamicHeaders.prev = &freeDynamicHeaders;
		dynamicHeaders.next = dynamicHeaders.prev = &dynamicHeaders;
		deferredFreeList.next = deferredFreeList.prev = &deferredFreeList;
	} else {
		// a full vid_restart re-runs Init under a fresh GL context
		ReclaimAllHeaders();
	}

	// set up the dynamic frame memory
	frameBytes = FRAME_MEMORY_BYTES;
	staticAllocTotal = 0;
	staticCountTotal = 0;

	byte	*junk = (byte *)Mem_Alloc( frameBytes );
	for ( int i = 0 ; i < NUM_VERTEX_FRAMES ; i++ ) {
		allocatingTempBuffer = true;	// force the alloc to use GL_STREAM_DRAW_ARB
		Alloc( junk, frameBytes, &tempBuffers[i] );
		allocatingTempBuffer = false;
		tempBuffers[i]->tag = TAG_FIXED;
		// unlink these from the static list, so they won't ever get purged
		tempBuffers[i]->next->prev = tempBuffers[i]->prev;
		tempBuffers[i]->prev->next = tempBuffers[i]->next;
	}
	Mem_Free( junk );

	if ( R_RendererUpload_DynamicFrameBridgeAvailable() ) {
		common->Printf(
			"vertex cache: frame-temp uploads routed through renderer upload stream (%dKB per frame buffer)\n",
			R_RendererUpload_FrameCapacity() / 1024 );
	}

	EndFrame();
}

/*
===========
idVertexCache::PurgeAll

Used when toggling vertex programs on or off, because
the cached data isn't valid
===========
*/
void idVertexCache::PurgeAll() {
	if ( staticHeaders.next == NULL ) {
		// Init() has never run, so the list sentinels are not self-linked yet.
		// Without this the loop condition is immediately true and ActuallyFree()
		// is handed a NULL block, which raises an error from inside whatever
		// teardown reached us - typically an early-startup fatal error unwinding
		// through idRenderSystem::Shutdown() long before R_InitOpenGL() ran.
		return;
	}
	while( staticHeaders.next != &staticHeaders ) {
		ActuallyFree( staticHeaders.next );
	}
}

/*
===========
idVertexCache::Shutdown
===========
*/
void idVertexCache::Shutdown() {
	if ( staticHeaders.next == NULL || deferredFreeList.next == NULL ) {
		// never initialized; the sentinels still hold their zero-initialized
		// static-storage values and there is nothing to free
		return;
	}

	PurgeAll();

	while( deferredFreeList.next != &deferredFreeList ) {
		ActuallyFree( deferredFreeList.next );
	}

	for ( int i = 0; i < NUM_VERTEX_FRAMES; i++ ) {
		vertCache_t *block = tempBuffers[i];
		if ( block == NULL ) {
			continue;
		}
		if ( block->vbo ) {
			R_RendererUpload_FreeStaticBuffer( block->vbo, block->size, block->indexBuffer );
		} else if ( block->virtMem ) {
			Mem_Free( block->virtMem );
			block->virtMem = NULL;
		}
		block->tag = TAG_FREE;
		tempBuffers[i] = NULL;
	}

	headerAllocator.Shutdown();
}

/*
===========
idVertexCache::Alloc
===========
*/
void idVertexCache::Alloc( void *data, int size, vertCache_t **buffer, bool indexBuffer ) {
	vertCache_t	*block;

	if ( size < 0 ) {
		common->Error( "idVertexCache::Alloc: size = %i\n", size );
	}

	// if we can't find anything, it will be NULL
	*buffer = NULL;
	if ( size == 0 ) {
		return;
	}

	// if we don't have any remaining unused headers, allocate some more
	if ( freeStaticHeaders.next == &freeStaticHeaders ) {

		for ( int i = 0; i < EXPAND_HEADERS; i++ ) {
			block = headerAllocator.Alloc();
			memset( block, 0, sizeof( *block ) );
			block->next = freeStaticHeaders.next;
			block->prev = &freeStaticHeaders;
			block->next->prev = block;
			block->prev->next = block;
		}
	}

	// move it from the freeStaticHeaders list to the staticHeaders list
	block = freeStaticHeaders.next;
	block->next->prev = block->prev;
	block->prev->next = block->next;
	block->next = staticHeaders.next;
	block->prev = &staticHeaders;
	block->next->prev = block;
	block->prev->next = block;

	block->size = size;
	block->offset = 0;
	block->tag = TAG_USED;

	// save data for debugging
	staticAllocThisFrame += block->size;
	staticCountThisFrame++;
	staticCountTotal++;
	staticAllocTotal += block->size;

	// this will be set to zero when it is purged
	block->user = buffer;
	*buffer = block;

	// allocation doesn't imply used-for-drawing, because at level
	// load time lots of things may be created, but they aren't
	// referenced by the GPU yet, and can be purged if needed.
	block->frameUsed = currentFrame - NUM_VERTEX_FRAMES;

	block->indexBuffer = indexBuffer;

	// copy the data
	if ( !virtualMemory ) {
		if ( !R_RendererUpload_AllocStaticBuffer( data, size, indexBuffer, allocatingTempBuffer, block->vbo ) ) {
			if ( block->vbo == 0 ) {
				glGenBuffersARB( 1, &block->vbo );
			}
			if ( indexBuffer ) {
				BindIndexBuffer( block->vbo );
				glBufferDataARB( GL_ELEMENT_ARRAY_BUFFER_ARB, (GLsizeiptrARB)size, data, GL_STATIC_DRAW_ARB );
			} else {
				BindArrayBuffer( block->vbo );
				if ( allocatingTempBuffer ) {
					glBufferDataARB( GL_ARRAY_BUFFER_ARB, (GLsizeiptrARB)size, data, GL_STREAM_DRAW_ARB );
				} else {
					glBufferDataARB( GL_ARRAY_BUFFER_ARB, (GLsizeiptrARB)size, data, GL_STATIC_DRAW_ARB );
				}
			}
			R_RendererUpload_RecordLegacyUpload( size );
		}
		block->virtMem = NULL;
	} else {
		block->virtMem = Mem_Alloc( size );
		SIMDProcessor->Memcpy( block->virtMem, data, size );
		R_RendererUpload_RecordLegacyUpload( size );
	}
}

/*
===========
idVertexCache::Touch
===========
*/
void idVertexCache::Touch( vertCache_t *block ) {
	if ( !block ) {
		common->Error( "idVertexCache Touch: NULL pointer" );
	}

	if ( block->tag == TAG_FREE ) {
		common->FatalError( "idVertexCache Touch: freed pointer" );
	}
	if ( block->tag == TAG_TEMP ) {
		common->FatalError( "idVertexCache Touch: temporary pointer" );
	}

	block->frameUsed = currentFrame;

	// move to the head of the LRU list
	block->next->prev = block->prev;
	block->prev->next = block->next;

	block->next = staticHeaders.next;
	block->prev = &staticHeaders;
	staticHeaders.next->prev = block;
	staticHeaders.next = block;
}

/*
===========
idVertexCache::Free
===========
*/
void idVertexCache::Free( vertCache_t *block ) {
	if (!block) {
		return;
	}

	if ( block->tag == TAG_FREE ) {
		common->FatalError( "idVertexCache Free: freed pointer" );
	}
	if ( block->tag == TAG_TEMP ) {
		common->FatalError( "idVertexCache Free: temporary pointer" );
	}

	// this block still can't be purged until the frame count has expired,
	// but it won't need to clear a user pointer when it is
	block->user = NULL;

	block->next->prev = block->prev;
	block->prev->next = block->next;

	block->next = deferredFreeList.next;
	block->prev = &deferredFreeList;
	deferredFreeList.next->prev = block;
	deferredFreeList.next = block;
}

/*
===========
idVertexCache::AllocFrameTemp

A frame temp allocation must never be allowed to fail due to overflow.
We can't simply sync with the GPU and overwrite what we have, because
there may still be future references to dynamically created surfaces.
===========
*/
vertCache_t	*idVertexCache::AllocFrameTemp( void *data, int size, bool indexBuffer ) {
	vertCache_t	*block;

	if ( size < 0 ) {
		common->Error( "idVertexCache::AllocFrameTemp: size = %i\n", size );
	}
	if ( size == 0 ) {
		return NULL;
	}

	rendererUploadAllocation_t uploadAllocation;
	const int alignment = indexBuffer ? 4 : 16;
	if ( R_RendererUpload_AllocFrameTemp( data, size, alignment, uploadAllocation ) ) {
		if ( freeDynamicHeaders.next == &freeDynamicHeaders ) {
			for ( int i = 0; i < EXPAND_HEADERS; i++ ) {
				block = headerAllocator.Alloc();
				memset( block, 0, sizeof( *block ) );
				block->next = freeDynamicHeaders.next;
				block->prev = &freeDynamicHeaders;
				block->next->prev = block;
				block->prev->next = block;
			}
		}

		block = freeDynamicHeaders.next;
		block->next->prev = block->prev;
		block->prev->next = block->next;
		block->next = dynamicHeaders.next;
		block->prev = &dynamicHeaders;
		block->next->prev = block;
		block->prev->next = block;

		block->size = size;
		block->tag = TAG_TEMP;
		block->indexBuffer = indexBuffer;
		block->offset = uploadAllocation.offset;
		block->virtMem = NULL;
		block->vbo = uploadAllocation.vbo;
		dynamicAllocThisFrame += block->size;
		dynamicCountThisFrame++;
		block->user = NULL;
		block->frameUsed = 0;
		return block;
	}

	// keep legacy temp offsets aligned (4 for index data, 16 for vertex data);
	// every current alloc size is already a multiple of these, so this is a
	// guard rather than a behavior change
	legacyAllocThisFrame = ( legacyAllocThisFrame + alignment - 1 ) & ~( alignment - 1 );

	if ( legacyAllocThisFrame + size > frameBytes ) {
		// if we don't have enough room in the temp block, allocate a static block,
		// but immediately free it so it will get freed at the next frame
		tempOverflow = true;
		R_RendererUpload_RecordLegacyStall();
		Alloc( data, size, &block, indexBuffer );
		Free( block);
		return block;
	}

	// this data is just going on the shared dynamic list

	// if we don't have any remaining unused headers, allocate some more
	if ( freeDynamicHeaders.next == &freeDynamicHeaders ) {

		for ( int i = 0; i < EXPAND_HEADERS; i++ ) {
			block = headerAllocator.Alloc();
			memset( block, 0, sizeof( *block ) );
			block->next = freeDynamicHeaders.next;
			block->prev = &freeDynamicHeaders;
			block->next->prev = block;
			block->prev->next = block;
		}
	}

	// move it from the freeDynamicHeaders list to the dynamicHeaders list
	block = freeDynamicHeaders.next;
	block->next->prev = block->prev;
	block->prev->next = block->next;
	block->next = dynamicHeaders.next;
	block->prev = &dynamicHeaders;
	block->next->prev = block;
	block->prev->next = block;

	block->size = size;
	block->tag = TAG_TEMP;
	block->indexBuffer = indexBuffer;
	block->offset = legacyAllocThisFrame;
	legacyAllocThisFrame += block->size;
	dynamicAllocThisFrame += block->size;
	dynamicCountThisFrame++;
	block->user = NULL;
	block->frameUsed = 0;

	// copy the data
	block->virtMem = tempBuffers[listNum]->virtMem;
	block->vbo = tempBuffers[listNum]->vbo;

	if ( block->vbo ) {
		const GLenum target = indexBuffer ? GL_ELEMENT_ARRAY_BUFFER_ARB : GL_ARRAY_BUFFER_ARB;
		if ( indexBuffer ) {
			BindIndexBuffer( block->vbo );
		} else {
			BindArrayBuffer( block->vbo );
		}
		glBufferSubDataARB( target, block->offset, (GLsizeiptrARB)size, data );
	} else {
		SIMDProcessor->Memcpy( (byte *)block->virtMem + block->offset, data, size );
	}
	R_RendererUpload_RecordLegacyUpload( size );

	return block;
}

/*
===========
idVertexCache::EndFrame
===========
*/
void idVertexCache::EndFrame() {
	// display debug information
	if ( r_showVertexCache.GetBool() ) {
		int	staticUseCount = 0;
		int staticUseSize = 0;

		for ( vertCache_t *block = staticHeaders.next ; block != &staticHeaders ; block = block->next ) {
			if ( block->frameUsed == currentFrame ) {
				staticUseCount++;
				staticUseSize += block->size;
			}
		}

		const char *frameOverflow = tempOverflow ? "(OVERFLOW)" : "";

		common->Printf( "vertex dynamic:%i=%ik%s, static alloc:%i=%ik used:%i=%ik total:%i=%ik\n",
			dynamicCountThisFrame, dynamicAllocThisFrame/1024, frameOverflow,
			staticCountThisFrame, staticAllocThisFrame/1024,
			staticUseCount, staticUseSize/1024,
			staticCountTotal, staticAllocTotal/1024 );
	}

#if 0
	// if our total static count is above our working memory limit, start purging things
	while ( staticAllocTotal > r_vertexBufferMegs.GetInteger() * 1024 * 1024 ) {
		// free the least recently used

	}
#endif

	// Always finish the frame with client-memory-safe bindings. Mixed upload
	// paths can bind a buffer even while the legacy vertex cache is virtual,
	// and redundant-state filtering makes the zero-bind free when already set.
	BindArrayBuffer( 0 );
	BindIndexBuffer( 0 );


	currentFrame = tr.frameCount;
	listNum = currentFrame % NUM_VERTEX_FRAMES;
	staticAllocThisFrame = 0;
	staticCountThisFrame = 0;
	dynamicAllocThisFrame = 0;
	dynamicCountThisFrame = 0;
	legacyAllocThisFrame = 0;
	tempOverflow = false;

	// free all the deferred free headers
	while( deferredFreeList.next != &deferredFreeList ) {
		ActuallyFree( deferredFreeList.next );
	}

	// free all the frame temp headers
	vertCache_t	*block = dynamicHeaders.next;
	if ( block != &dynamicHeaders ) {
		block->prev = &freeDynamicHeaders;
		dynamicHeaders.prev->next = freeDynamicHeaders.next;
		freeDynamicHeaders.next->prev = dynamicHeaders.prev;
		freeDynamicHeaders.next = block;

		dynamicHeaders.next = dynamicHeaders.prev = &dynamicHeaders;
	}
}

/*
=============
idVertexCache::List
=============
*/
void idVertexCache::List( void ) {
	int	numActive = 0;
	int frameStatic = 0;
	int	totalStatic = 0;

	vertCache_t *block;
	for ( block = staticHeaders.next ; block != &staticHeaders ; block = block->next) {
		numActive++;

		totalStatic += block->size;
		if ( block->frameUsed == currentFrame ) {
			frameStatic += block->size;
		}
	}

	int	numFreeStaticHeaders = 0;
	for ( block = freeStaticHeaders.next ; block != &freeStaticHeaders ; block = block->next ) {
		numFreeStaticHeaders++;
	}

	int	numFreeDynamicHeaders = 0;
	for ( block = freeDynamicHeaders.next ; block != &freeDynamicHeaders ; block = block->next ) {
		numFreeDynamicHeaders++;
	}

	common->Printf( "%i megs working set\n", r_vertexBufferMegs.GetInteger() );
	common->Printf( "%i dynamic temp buffers of %ik\n", NUM_VERTEX_FRAMES, frameBytes / 1024 );
	common->Printf( "%5i active static headers\n", numActive );
	common->Printf( "%5i free static headers\n", numFreeStaticHeaders );
	common->Printf( "%5i free dynamic headers\n", numFreeDynamicHeaders );

	if ( !virtualMemory  ) {
		common->Printf( "Vertex cache is in ARB_vertex_buffer_object memory (FAST).\n");
	} else {
		common->Printf( "Vertex cache is in virtual memory (SLOW)\n" );
	}

	if ( r_useIndexBuffers.GetBool() ) {
		common->Printf( "Index buffers are accelerated.\n" );
	} else {
		common->Printf( "Index buffers are not used.\n" );
	}
}

/*
=============
idVertexCache::IsFast

just for gfxinfo printing
=============
*/
bool idVertexCache::IsFast() {
	if ( virtualMemory ) {
		return false;
	}
	return true;
}
