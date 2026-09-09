/* openQ4 private implementation declarations; zlib license. */
#ifndef SDL_openq4_windows_text_h_
#define SDL_openq4_windows_text_h_
#include <SDL3/SDL_openq4_text_provenance.h>
typedef struct OQ4_TextScope {
    Uint32 window_id, message, origin;
    Uint64 lifetime, session, composition;
} OQ4_TextScope;
OQ4_TextScope OQ4_WIN_SaveTextScope(void);
void OQ4_WIN_RestoreTextScope(OQ4_TextScope scope);
void OQ4_WIN_ObserveTextMessage(SDL_Window *window, unsigned int msg, uintptr_t wp, intptr_t lp);
void OQ4_WIN_TextSession(SDL_Window *window, bool start);
void OQ4_WIN_TextCancel(SDL_Window *window);
void OQ4_WIN_TextShutdown(void);
bool OQ4_WIN_PushKeyboardText(SDL_Event *event);
OQ4_TextScope OQ4_WIN_BeginTextAdmission(const SDL_Event *event);
bool OQ4_WIN_ValidateTextMarker(const SDL_Event *event);
void OQ4_WIN_AssociateText(SDL_Event *event, OQ4_TextScope admission);
void OQ4_WIN_DropTextAssociation(const SDL_Event *event);
#endif
