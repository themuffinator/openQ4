// Backend-owned readback of one ordinary presented frame. All calls run on the
// render thread. Poll never waits; the mapped bytes live until Release.
#ifndef OPENQ4_SAVE_PREVIEW_CAPTURE_H
#define OPENQ4_SAVE_PREVIEW_CAPTURE_H
bool RB_RequestSavePreview();
void RB_QueueSavePreview();
#ifndef OPENQ4_RENDERER_VK_MODULE
// A VR frame's preview is its left eye, read from that target's framebuffer
// before the desktop mirror is drawn (OpenGL). Vulkan's VR presentation reads
// its eye image through VK_QueueSavePreviewFromImage instead.
void RB_QueueSavePreviewFromFramebuffer( unsigned int framebuffer, int width, int height );
#endif
int RB_PollSavePreview( const byte *&pixels, int &width, int &height, bool &topDown, bool &bgra );
void RB_ReleaseSavePreview();

// Video capture: the same readback of the presented frame, continuously, into a
// small ring so a read is never waited for the frame it was made. Request marks
// the next presented frame; Queue (at the swap, beside the save preview) records
// its read; Poll hands back the oldest read once the GPU has finished it, and
// with wait blocks on that one read, never on later frames. Its pixels stay
// mapped until Release. All calls run on the render thread.
bool RB_RequestVideoFrame( int64 tag );
void RB_QueueVideoFrame();
int RB_PollVideoFrame( renderVideoFrame_t &frame, bool wait );
void RB_ReleaseVideoFrame();
void RB_ShutdownVideoCapture();
#endif
