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
#endif
