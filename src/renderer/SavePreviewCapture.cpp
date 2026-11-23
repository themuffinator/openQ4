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

/*
====================
Video capture ring

Four pixel-pack buffers, reused while the size holds: a 60 Hz capture keeps
about three frames in flight, so the read of a frame is mapped two or three
frames after it was queued, when the GPU has long finished it.
====================
*/
static const int RB_VIDEO_RING = 4;
static struct {
	GLuint buffer;
	GLsync fence;
	int64 allocated;
	int width, height;
	int64 tag;
	const byte *mapped;
	int state;	// 0 free, 1 queued, 2 mapped
} videoSlots[ RB_VIDEO_RING ];
static int videoHead = 0;
static int videoCount = 0;
static bool videoRequested = false;
static int64 videoRequestTag = 0;

static bool RB_VideoCaptureSupported() {
	return glConfig.isInitialized && !r_frontBuffer.GetBool() &&
		glFenceSync != NULL && glClientWaitSync != NULL && glDeleteSync != NULL &&
		glMapBufferRange != NULL && glUnmapBufferARB != NULL;
}

bool RB_RequestVideoFrame( int64 tag ) {
	if ( !RB_VideoCaptureSupported() ) return false;
	if ( videoRequested ) {
		// the frame asked for earlier was never presented: this one replaces it
		videoRequestTag = tag;
		return true;
	}
	if ( videoCount >= RB_VIDEO_RING ) return false;
	videoRequested = true;
	videoRequestTag = tag;
	return true;
}

void RB_QueueVideoFrame() {
	if ( !videoRequested || tr.takingScreenshot || videoCount >= RB_VIDEO_RING ) return;
	videoRequested = false;
	const int width = glConfig.vidWidth;
	const int height = glConfig.vidHeight;
	const int64 bytes = (int64)width * height * 4;
	if ( width <= 0 || height <= 0 || bytes > 33554432LL * 4 ) return;
	const int index = ( videoHead + videoCount ) % RB_VIDEO_RING;
	GLint oldBuffer = 0, oldReadBuffer = 0;
	const GLenum packNames[] = { GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH, GL_PACK_SKIP_PIXELS, GL_PACK_SKIP_ROWS };
	GLint oldPack[4];
	for ( int i = 0; i < 4; ++i ) glGetIntegerv( packNames[i], &oldPack[i] );
	glGetIntegerv( GL_PIXEL_PACK_BUFFER_BINDING, &oldBuffer );
	if ( videoSlots[index].buffer == 0 ) {
		glGenBuffersARB( 1, &videoSlots[index].buffer );
		videoSlots[index].allocated = 0;
	}
	if ( videoSlots[index].buffer == 0 ) return;
	glBindBufferARB( GL_PIXEL_PACK_BUFFER_ARB, videoSlots[index].buffer );
	if ( videoSlots[index].allocated != bytes ) {
		glBufferDataARB( GL_PIXEL_PACK_BUFFER_ARB, (GLsizeiptrARB)bytes, NULL, GL_STREAM_READ_ARB );
		GLint allocated = 0;
		glGetBufferParameterivARB( GL_PIXEL_PACK_BUFFER_ARB, GL_BUFFER_SIZE, &allocated );
		videoSlots[index].allocated = allocated;
		if ( allocated != bytes ) {
			glBindBufferARB( GL_PIXEL_PACK_BUFFER_ARB, oldBuffer );
			return;
		}
	}
	for ( int i = 0; i < 4; ++i ) glPixelStorei( packNames[i], i == 0 ? 1 : 0 );
	// the window's back buffer, read as the swap has it bound
	glGetIntegerv( GL_READ_BUFFER, &oldReadBuffer );
	glReadBuffer( GL_BACK );
	glReadPixels( 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
	videoSlots[index].fence = glFenceSync( GL_SYNC_GPU_COMMANDS_COMPLETE, 0 );
	glReadBuffer( oldReadBuffer );
	glBindBufferARB( GL_PIXEL_PACK_BUFFER_ARB, oldBuffer );
	for ( int i = 0; i < 4; ++i ) glPixelStorei( packNames[i], oldPack[i] );
	if ( videoSlots[index].fence == NULL ) return;
	videoSlots[index].width = width;
	videoSlots[index].height = height;
	videoSlots[index].tag = videoRequestTag;
	videoSlots[index].state = 1;
	videoCount++;
}

int RB_PollVideoFrame( renderVideoFrame_t &frame, bool wait ) {
	if ( videoCount == 0 ) return -1;
	const int index = videoHead;
	if ( videoSlots[index].state == 1 ) {
		const GLenum result = glClientWaitSync( videoSlots[index].fence, wait ? GL_SYNC_FLUSH_COMMANDS_BIT : 0,
			wait ? (GLuint64)2000000000ULL : 0 );
		if ( result == GL_TIMEOUT_EXPIRED ) return wait ? -1 : 0;
		if ( result != GL_ALREADY_SIGNALED && result != GL_CONDITION_SATISFIED ) {
			RB_ReleaseVideoFrame();
			return -1;
		}
		GLint oldBuffer = 0;
		glGetIntegerv( GL_PIXEL_PACK_BUFFER_BINDING, &oldBuffer );
		glBindBufferARB( GL_PIXEL_PACK_BUFFER_ARB, videoSlots[index].buffer );
		frame.pixels = static_cast<const byte *>( glMapBufferRange( GL_PIXEL_PACK_BUFFER_ARB, 0,
			(GLsizeiptrARB)( (int64)videoSlots[index].width * videoSlots[index].height * 4 ), GL_MAP_READ_BIT ) );
		glBindBufferARB( GL_PIXEL_PACK_BUFFER_ARB, oldBuffer );
		if ( frame.pixels == NULL ) {
			RB_ReleaseVideoFrame();
			return -1;
		}
		videoSlots[index].mapped = frame.pixels;
		videoSlots[index].state = 2;
	} else {
		frame.pixels = videoSlots[index].mapped;
	}
	frame.width = videoSlots[index].width;
	frame.height = videoSlots[index].height;
	frame.tag = videoSlots[index].tag;
	frame.topDown = frame.bgra = false;
	return 1;
}

void RB_ReleaseVideoFrame() {
	if ( videoCount == 0 ) return;
	const int index = videoHead;
	if ( videoSlots[index].state == 2 ) {
		GLint oldBuffer = 0;
		glGetIntegerv( GL_PIXEL_PACK_BUFFER_BINDING, &oldBuffer );
		glBindBufferARB( GL_PIXEL_PACK_BUFFER_ARB, videoSlots[index].buffer );
		glUnmapBufferARB( GL_PIXEL_PACK_BUFFER_ARB );
		glBindBufferARB( GL_PIXEL_PACK_BUFFER_ARB, oldBuffer );
	}
	if ( videoSlots[index].fence != NULL ) glDeleteSync( videoSlots[index].fence );
	videoSlots[index].fence = NULL;
	videoSlots[index].mapped = NULL;
	videoSlots[index].state = 0;
	videoHead = ( videoHead + 1 ) % RB_VIDEO_RING;
	videoCount--;
}

void RB_ShutdownVideoCapture() {
	while ( videoCount > 0 ) RB_ReleaseVideoFrame();
	for ( int i = 0; i < RB_VIDEO_RING; ++i ) {
		if ( videoSlots[i].buffer != 0 && glConfig.isInitialized ) glDeleteBuffersARB( 1, &videoSlots[i].buffer );
		videoSlots[i].buffer = 0;
		videoSlots[i].allocated = 0;
	}
	videoHead = videoCount = 0;
	videoRequested = false;
}
#endif
