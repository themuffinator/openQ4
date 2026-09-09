/* openQ4 private SDL extension. Copyright (C) 2026 DarkMatter Productions.
 * Distributed under the zlib license, like the SDL package containing it.
 * Not an upstream SDL API or a native TSF composition identity guarantee. */
#ifndef SDL_openq4_text_provenance_h_
#define SDL_openq4_text_provenance_h_
#include <SDL3/SDL_events.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum OQ4_TextKind {
    OQ4_TEXT_SESSION_BEGIN = 1, OQ4_TEXT_SESSION_END, OQ4_TEXT_BEGIN,
    OQ4_TEXT_PREEDIT, OQ4_TEXT_RESULT, OQ4_TEXT_END, OQ4_TEXT_CANCEL,
    OQ4_TEXT_COMMIT, OQ4_TEXT_WINDOW_END
} OQ4_TextKind;
typedef enum OQ4_TextOrigin {
    OQ4_TEXT_UNKNOWN = 0,
    /* Native WM_CHAR/WM_UNICHAR path, not proof of physical-key causation. */
    OQ4_TEXT_UNMARKED_CHARACTER = 1,
    /* An observed IMM interval/synchronous result scope, not TSF identity. */
    OQ4_TEXT_OBSERVED_COMPOSITION = 2
} OQ4_TextOrigin;
typedef struct OQ4_TextRecord {
    Uint32 version, kind, origin, window_id, native_message, text_bytes;
    Uint64 sequence, window_lifetime, session, composition;
    Sint32 selection_start, selection_length; /* UTF-16 code units; -1 unknown. */
} OQ4_TextRecord;

/* Main thread only. Reset releases bounded pending storage, poisons existing
 * native-window affinity, and never reuses marker/lifetime IDs. The caller must
 * reconcile its own queues; old marker/public associations remain obsolete. */
extern SDL_DECLSPEC bool SDLCALL OQ4_WindowsTextEnable(bool enabled);
extern SDL_DECLSPEC bool SDLCALL OQ4_WindowsTextReset(void);
extern SDL_DECLSPEC Uint32 SDLCALL OQ4_WindowsTextEventType(void);
extern SDL_DECLSPEC bool SDLCALL OQ4_WindowsTextHealthy(void);
/* Copy is atomic on failure. Copy UTF-8 before Release, including on decode
 * failure. Output buffer requires text_bytes+1 bytes, including empty records. */
extern SDL_DECLSPEC bool SDLCALL OQ4_WindowsTextCopy(const SDL_Event *marker, OQ4_TextRecord *record,
                         char *text, size_t capacity);
extern SDL_DECLSPEC bool SDLCALL OQ4_WindowsTextRelease(const SDL_Event *marker);
/* Native public TEXT_INPUT: 0 unassociated; UINT32_MAX failed association;
 * otherwise immutable token of its exact private Commit. Ignore only the
 * explicitly associated public duplicate. Unknown/failure is legacy-only. */
extern SDL_DECLSPEC Uint32 SDLCALL OQ4_WindowsTextAssociation(const SDL_Event *event);

#ifdef __cplusplus
}
#endif
#endif
