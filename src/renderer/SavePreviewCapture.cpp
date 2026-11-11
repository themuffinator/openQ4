#include "tr_local.h"
#include "SavePreviewCapture.h"

#ifndef OPENQ4_RENDERER_VK_MODULE
static GLuint savePreviewBuffer = 0;
static GLsync savePreviewFence = NULL;
static int savePreviewState = 0; // idle, requested, submitted, mapped; -1 failed
static int savePreviewWidth = 0;
static int savePreviewHeight = 0;

bool RB_RequestSavePreview() {
	if ( savePreviewState != 0 || !glConfig.isInitialized || r_frontBuffer.GetBool() ||
		glFenceSync == NULL || glClientWaitSync == NULL || glDeleteSync == NULL ||
		glMapBufferRange == NULL || glUnmapBufferARB == NULL || glGetBufferParameterivARB == NULL ) return false;
	savePreviewState = 1;
	return true;
}

// Queues the readback of a requested preview: the window's back buffer as it
// is bound for the swap (framebuffer 0 here), or a VR eye target's colour.
static void RB_QueueSavePreviewRead( GLuint framebuffer, int width, int height ) {
	if ( savePreviewState != 1 || tr.takingScreenshot ) return;
	savePreviewState = -1;
	savePreviewWidth = width;
	savePreviewHeight = height;
	const int64 pixels = (int64)savePreviewWidth * savePreviewHeight;
	if ( savePreviewWidth <= 0 || savePreviewHeight <= 0 || pixels > 33554432 ) return;
	GLint oldBuffer = 0, oldReadBuffer = 0, oldReadFramebuffer = 0;
	const GLenum packNames[] = { GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH, GL_PACK_SKIP_PIXELS, GL_PACK_SKIP_ROWS };
	GLint oldPack[4];
	for ( int i = 0; i < 4; ++i ) glGetIntegerv( packNames[i], &oldPack[i] );
	glGetIntegerv( GL_PIXEL_PACK_BUFFER_BINDING, &oldBuffer );
	glGetIntegerv( GL_READ_FRAMEBUFFER_BINDING, &oldReadFramebuffer );
	glGenBuffersARB( 1, &savePreviewBuffer );
	if ( savePreviewBuffer == 0 ) return;
	glBindBufferARB( GL_PIXEL_PACK_BUFFER_ARB, savePreviewBuffer );
	glBufferDataARB( GL_PIXEL_PACK_BUFFER_ARB, (GLsizeiptrARB)( pixels * 4 ), NULL, GL_STREAM_READ_ARB );
	GLint allocated = 0;
	glGetBufferParameterivARB( GL_PIXEL_PACK_BUFFER_ARB, GL_BUFFER_SIZE, &allocated );
	if ( allocated != pixels * 4 ) {
		glBindBufferARB( GL_PIXEL_PACK_BUFFER_ARB, oldBuffer );
		return;
	}
	for ( int i = 0; i < 4; ++i ) glPixelStorei( packNames[i], i == 0 ? 1 : 0 );
	// the window is read as the swap has it bound; an eye target is bound for the read
	const bool target = framebuffer != 0;
	if ( target ) {
		glBindFramebuffer( GL_READ_FRAMEBUFFER, framebuffer );
	}
	// the read buffer is per framebuffer: read the one bound now, put it back
	glGetIntegerv( GL_READ_BUFFER, &oldReadBuffer );
	glReadBuffer( target ? GL_COLOR_ATTACHMENT0 : GL_BACK );
	glReadPixels( 0, 0, savePreviewWidth, savePreviewHeight, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
	savePreviewFence = glFenceSync( GL_SYNC_GPU_COMMANDS_COMPLETE, 0 );
	glReadBuffer( oldReadBuffer );
	if ( target ) {
		glBindFramebuffer( GL_READ_FRAMEBUFFER, (GLuint)oldReadFramebuffer );
	}
	glBindBufferARB( GL_PIXEL_PACK_BUFFER_ARB, oldBuffer );
	for ( int i = 0; i < 4; ++i ) glPixelStorei( packNames[i], oldPack[i] );
	if ( savePreviewFence != NULL ) savePreviewState = 2;
}

void RB_QueueSavePreview() {
	RB_QueueSavePreviewRead( 0, glConfig.vidWidth, glConfig.vidHeight );
}

void RB_QueueSavePreviewFromFramebuffer( unsigned int framebuffer, int width, int height ) {
	if ( framebuffer != 0 ) {
		RB_QueueSavePreviewRead( (GLuint)framebuffer, width, height );
	}
}

int RB_PollSavePreview( const byte *&pixels, int &width, int &height, bool &topDown, bool &bgra ) {
	if ( savePreviewState <= 0 ) return -1;
	if ( savePreviewState == 1 ) return 0;
	const GLenum result = glClientWaitSync( savePreviewFence, 0, 0 );
	if ( result == GL_TIMEOUT_EXPIRED ) return 0;
	if ( result != GL_ALREADY_SIGNALED && result != GL_CONDITION_SATISFIED ) return -1;
	GLint oldBuffer = 0;
	glGetIntegerv( GL_PIXEL_PACK_BUFFER_BINDING, &oldBuffer );
	glBindBufferARB( GL_PIXEL_PACK_BUFFER_ARB, savePreviewBuffer );
	pixels = static_cast<const byte *>( glMapBufferRange( GL_PIXEL_PACK_BUFFER_ARB, 0,
		(GLsizeiptrARB)( (int64)savePreviewWidth * savePreviewHeight * 4 ), GL_MAP_READ_BIT ) );
	glBindBufferARB( GL_PIXEL_PACK_BUFFER_ARB, oldBuffer );
	if ( pixels == NULL ) return -1;
	savePreviewState = 3;
	width = savePreviewWidth;
	height = savePreviewHeight;
	topDown = bgra = false;
	return 1;
}

void RB_ReleaseSavePreview() {
	if ( savePreviewBuffer != 0 ) {
		if ( savePreviewState == 3 ) {
			GLint oldBuffer = 0;
			glGetIntegerv( GL_PIXEL_PACK_BUFFER_BINDING, &oldBuffer );
			glBindBufferARB( GL_PIXEL_PACK_BUFFER_ARB, savePreviewBuffer );
			glUnmapBufferARB( GL_PIXEL_PACK_BUFFER_ARB );
			glBindBufferARB( GL_PIXEL_PACK_BUFFER_ARB, oldBuffer );
		}
		glDeleteBuffersARB( 1, &savePreviewBuffer );
	}
	if ( savePreviewFence != NULL ) glDeleteSync( savePreviewFence );
	savePreviewBuffer = 0;
	savePreviewFence = NULL;
	savePreviewState = 0;
}
#endif
