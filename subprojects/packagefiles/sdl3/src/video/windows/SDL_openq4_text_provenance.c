/* openQ4 private Windows text observer. Copyright (C) 2026 DarkMatter
 * Productions. Distributed under SDL's zlib license. No native GUI calls. */
#include "SDL_internal.h"
#if defined(SDL_VIDEO_DRIVER_WINDOWS) && !defined(SDL_PLATFORM_XBOXONE) && !defined(SDL_PLATFORM_XBOXSERIES)
#include "SDL_windowsvideo.h"
#include "SDL_openq4_text_provenance.h"
#include <imm.h>

#define OQ4_TEXT_SLOTS 256
#define OQ4_TEXT_WINDOWS 128
#define OQ4_TEXT_BYTES 1048576
#define OQ4_TEXT_MAX 65536
#define OQ4_TEXT_FAILED ((Uint32)0xffffffffu)
typedef struct OQ4_TextSlot {
    Uint32 token;
    OQ4_TextRecord record;
    char *text;
} OQ4_TextSlot;
typedef struct OQ4_TextWindow {
    SDL_Window *window;
    Uint32 id;
    Uint64 lifetime, session, composition;
    HIMC context;
    bool started, seen_ime, tainted;
} OQ4_TextWindow;
static OQ4_TextSlot oq4_slots[OQ4_TEXT_SLOTS];
static OQ4_TextWindow oq4_windows[OQ4_TEXT_WINDOWS];
static OQ4_TextScope oq4_scope;
static const SDL_Event *oq4_keyboard_event, *oq4_marker_event, *oq4_marker_expected;
static OQ4_TextScope oq4_keyboard_scope;
static bool oq4_keyboard_push;
static Uint32 oq4_event_type, oq4_token;
static Uint64 oq4_identity, oq4_sequence;
static size_t oq4_bytes;
static bool oq4_enabled, oq4_healthy, oq4_emitting;

static bool OQ4_Main(void)
{
    return SDL_IsMainThread() || SDL_SetError("openQ4 text observation requires the main thread");
}
static void OQ4_Fault(void)
{
    oq4_healthy = false;
}
static bool OQ4_Utf8(const char *text, size_t length)
{
    size_t i = 0;
    while (i < length) {
        Uint32 value = (unsigned char)text[i++], minimum = 0;
        int remaining = 0;
        if (!value) return false;
        if (value < 0x80) continue;
        if (value >= 0xc2 && value <= 0xdf) { value &= 0x1f; remaining = 1; minimum = 0x80; }
        else if (value >= 0xe0 && value <= 0xef) { value &= 0x0f; remaining = 2; minimum = 0x800; }
        else if (value >= 0xf0 && value <= 0xf4) { value &= 7; remaining = 3; minimum = 0x10000; }
        else return false;
        while (remaining--) {
            unsigned char next;
            if (i == length || ((next = (unsigned char)text[i++]) & 0xc0) != 0x80) return false;
            value = (value << 6) | (next & 0x3f);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}
static Uint64 OQ4_Identity(void)
{
    if (oq4_identity == SDL_MAX_UINT64) {
        OQ4_Fault();
        return 0;
    }
    return ++oq4_identity;
}
static void OQ4_FreeSlot(OQ4_TextSlot *slot)
{
    if (slot->token) {
        oq4_bytes -= slot->record.text_bytes + 1;
        SDL_free(slot->text);
        SDL_zero(*slot);
    }
}
static void OQ4_ClearSlots(void)
{
    int i;
    for (i = 0; i < OQ4_TEXT_SLOTS; ++i) OQ4_FreeSlot(&oq4_slots[i]);
}
static OQ4_TextWindow *OQ4_Window(SDL_Window *window, bool create)
{
    int i;
    OQ4_TextWindow *empty = NULL;
    if (!window) return NULL;
    for (i = 0; i < OQ4_TEXT_WINDOWS; ++i) {
        OQ4_TextWindow *item = &oq4_windows[i];
        if (item->window == window && item->id == window->id) return item;
        if (!item->window && !empty) empty = item;
    }
    if (!create) return NULL;
    if (!empty) { OQ4_Fault(); return NULL; }
    empty->window = window;
    empty->id = window->id;
    empty->lifetime = OQ4_Identity();
    return empty;
}
static OQ4_TextRecord OQ4_Record(OQ4_TextWindow *window, Uint32 kind)
{
    OQ4_TextRecord record;
    SDL_zero(record);
    record.version = 1;
    record.kind = kind;
    record.selection_start = record.selection_length = -1;
    if (window) {
        record.window_id = window->id;
        record.window_lifetime = window->lifetime;
        record.session = window->session;
        record.composition = window->composition;
        if (window->composition && !window->tainted) record.origin = OQ4_TEXT_OBSERVED_COMPOSITION;
    }
    if (kind == OQ4_TEXT_SESSION_BEGIN || kind == OQ4_TEXT_SESSION_END || kind == OQ4_TEXT_WINDOW_END) {
        record.composition = 0; record.origin = OQ4_TEXT_UNKNOWN;
    }
    return record;
}
static Uint32 OQ4_Emit(OQ4_TextRecord record, const char *text, size_t length)
{
    int i;
    SDL_Event marker;
    OQ4_TextSlot *slot = NULL;
    if (!oq4_enabled || !oq4_healthy) return OQ4_TEXT_FAILED;
    if (!text || length > OQ4_TEXT_MAX || !OQ4_Utf8(text, length) || oq4_emitting || length + 1 > OQ4_TEXT_BYTES - oq4_bytes ||
        oq4_token >= 0x7fffffff || oq4_sequence == SDL_MAX_UINT64) {
        OQ4_Fault(); return OQ4_TEXT_FAILED;
    }
    for (i = 0; i < OQ4_TEXT_SLOTS; ++i) if (!oq4_slots[i].token) { slot = &oq4_slots[i]; break; }
    if (!slot) { OQ4_Fault(); return OQ4_TEXT_FAILED; }
    slot->text = (char *)SDL_malloc(length + 1);
    if (!slot->text) { OQ4_Fault(); return OQ4_TEXT_FAILED; }
    SDL_memcpy(slot->text, text, length);
    slot->text[length] = 0;
    record.sequence = ++oq4_sequence;
    record.text_bytes = (Uint32)length;
    slot->record = record;
    slot->token = ++oq4_token;
    oq4_bytes += length + 1;
    SDL_zero(marker);
    marker.type = oq4_event_type;
    marker.user.windowID = record.window_id;
    marker.user.code = (Sint32)slot->token;
    const SDL_Event expected = marker;
    oq4_marker_event = &marker;
    oq4_marker_expected = &expected;
    oq4_emitting = true;
    const bool queued = SDL_PushEvent(&marker);
    const bool intact = OQ4_WIN_ValidateTextMarker(&marker);
    oq4_emitting = false;
    oq4_marker_event = oq4_marker_expected = NULL;
    if (!queued || !intact) {
        OQ4_FreeSlot(slot);
        OQ4_Fault(); return OQ4_TEXT_FAILED;
    }
    return slot->token;
}
static void OQ4_Notify(OQ4_TextWindow *window, Uint32 kind, Uint32 message)
{
    OQ4_TextRecord record = OQ4_Record(window, kind);
    record.native_message = message;
    OQ4_Emit(record, "", 0);
}
static OQ4_TextSlot *OQ4_Find(const SDL_Event *marker)
{
    int i;
    if (!marker || !oq4_event_type || marker->type != oq4_event_type || marker->user.code <= 0 ||
        marker->user.data1 || marker->user.data2) return NULL;
    for (i = 0; i < OQ4_TEXT_SLOTS; ++i) {
        OQ4_TextSlot *slot = &oq4_slots[i];
        if (slot->token == (Uint32)marker->user.code && slot->record.window_id == marker->user.windowID) return slot;
    }
    return NULL;
}
bool OQ4_WindowsTextEnable(bool enabled)
{
    if (!OQ4_Main() || oq4_emitting) return false;
    if (enabled == oq4_enabled) return !enabled || oq4_healthy;
    if (enabled && !oq4_event_type) {
        oq4_event_type = SDL_RegisterEvents(1);
        if (!oq4_event_type) return false;
    }
    oq4_enabled = enabled;
    return OQ4_WindowsTextReset();
}
bool OQ4_WindowsTextReset(void)
{
    int i;
    if (!OQ4_Main() || oq4_emitting) return false;
    OQ4_ClearSlots();
    SDL_zero(oq4_scope);
    for (i = 0; i < OQ4_TEXT_WINDOWS; ++i) if (oq4_windows[i].window) {
        oq4_windows[i].tainted = true;
        oq4_windows[i].composition = 0;
        oq4_windows[i].session = 0;
        oq4_windows[i].started = false;
    }
    oq4_healthy = oq4_enabled && oq4_token < 0x7fffffff && oq4_identity < SDL_MAX_UINT64 && oq4_sequence < SDL_MAX_UINT64;
    return !oq4_enabled || oq4_healthy;
}
Uint32 OQ4_WindowsTextEventType(void) { return OQ4_Main() ? oq4_event_type : 0; }
bool OQ4_WindowsTextHealthy(void) { return OQ4_Main() && oq4_enabled && oq4_healthy; }
bool OQ4_WindowsTextCopy(const SDL_Event *marker, OQ4_TextRecord *record, char *text, size_t capacity)
{
    OQ4_TextSlot *slot;
    if (!OQ4_Main() || oq4_emitting || !record || !text || !(slot = OQ4_Find(marker)) || capacity <= slot->record.text_bytes) return false;
    SDL_memcpy(text, slot->text, slot->record.text_bytes + 1);
    *record = slot->record;
    return true;
}
bool OQ4_WindowsTextRelease(const SDL_Event *marker)
{
    OQ4_TextSlot *slot;
    if (!OQ4_Main() || oq4_emitting || !(slot = OQ4_Find(marker))) return false;
    OQ4_FreeSlot(slot); return true;
}
Uint32 OQ4_WindowsTextAssociation(const SDL_Event *event)
{
    return OQ4_Main() && event && event->type == SDL_EVENT_TEXT_INPUT ? event->common.reserved : 0;
}
OQ4_TextScope OQ4_WIN_SaveTextScope(void) { OQ4_TextScope scope; SDL_zero(scope); return SDL_IsMainThread() ? oq4_scope : scope; }
void OQ4_WIN_RestoreTextScope(OQ4_TextScope scope) { if (SDL_IsMainThread()) oq4_scope = scope; }

bool OQ4_WIN_PushKeyboardText(SDL_Event *event)
{
    if (!SDL_IsMainThread() || !oq4_enabled) return SDL_PushEvent(event);
    const SDL_Event *previous_event = oq4_keyboard_event;
    const OQ4_TextScope previous_scope = oq4_keyboard_scope;
    const bool previous_push = oq4_keyboard_push;
    oq4_keyboard_event = event;
    SDL_zero(oq4_keyboard_scope);
    /* Only the outer native keyboard admission may claim WindowProc scope.
     * A callback's reentrant SendKeyboardText is no stronger than PushEvent. */
    if (!previous_push) oq4_keyboard_scope = oq4_scope;
    oq4_keyboard_push = true;
    const bool queued = SDL_PushEvent(event);
    oq4_keyboard_event = previous_event;
    oq4_keyboard_scope = previous_scope;
    oq4_keyboard_push = previous_push;
    return queued;
}
OQ4_TextScope OQ4_WIN_BeginTextAdmission(const SDL_Event *event)
{
    OQ4_TextScope admission;
    SDL_zero(admission);
    if (SDL_IsMainThread() && event == oq4_keyboard_event && event->type == SDL_EVENT_TEXT_INPUT) {
        admission = oq4_keyboard_scope;
        /* Consume before filters/watchers, including a reentrant same-pointer
         * public push. Association uses this immutable local admission. */
        oq4_keyboard_event = NULL;
    }
    return admission;
}
bool OQ4_WIN_ValidateTextMarker(const SDL_Event *event)
{
    if (!SDL_IsMainThread() || event != oq4_marker_event) return true;
    return oq4_healthy && oq4_marker_expected && event->type == oq4_marker_expected->type &&
        event->common.reserved == oq4_marker_expected->common.reserved &&
        event->user.windowID == oq4_marker_expected->user.windowID &&
        event->user.code == oq4_marker_expected->user.code &&
        event->user.data1 == oq4_marker_expected->user.data1 && event->user.data2 == oq4_marker_expected->user.data2;
}

static void OQ4_Cancel(OQ4_TextWindow *window, Uint32 message)
{
    if (!window) return;
    window->tainted = true; /* A late IMM message cannot be relabeled at a new Begin. */
    OQ4_Notify(window, OQ4_TEXT_CANCEL, message);
    window->composition = 0;
    window->context = NULL;
}
void OQ4_WIN_TextSession(SDL_Window *window, bool start)
{
    OQ4_TextWindow *state;
    if (!SDL_IsMainThread() || !oq4_enabled || !(state = OQ4_Window(window, start))) return;
    if (start) {
        if (!state->started) {
            state->session = OQ4_Identity(); state->started = true;
            OQ4_Notify(state, OQ4_TEXT_SESSION_BEGIN, 0);
        }
    } else if (state->started) {
        OQ4_Cancel(state, 0);
        OQ4_Notify(state, OQ4_TEXT_SESSION_END, 0);
        state->started = false; state->session = 0;
    }
}
void OQ4_WIN_TextCancel(SDL_Window *window)
{
    if (SDL_IsMainThread() && oq4_enabled) OQ4_Cancel(OQ4_Window(window, false), 0);
}
void OQ4_WIN_TextShutdown(void)
{
    if (!OQ4_Main() || oq4_emitting) return;
    OQ4_ClearSlots(); SDL_zeroa(oq4_windows); SDL_zero(oq4_scope);
    oq4_enabled = oq4_healthy = false;
    /* event_type may be reused by SDL after video/event shutdown: register a
     * fresh event type on next enable. Marker tokens still never repeat. */
    oq4_event_type = 0;
}

static void OQ4_Composition(OQ4_TextWindow *state, Uint32 kind, DWORD query, Uint32 message)
{
    OQ4_TextRecord record = OQ4_Record(state, kind);
    HIMC context = ImmGetContext(state->window->internal->hwnd);
    LONG bytes, actual, cursor;
    WCHAR *wide = NULL;
    char *text = NULL;
    int utf8_bytes;
    record.native_message = message;
    if (!context) { OQ4_Cancel(state, message); OQ4_Fault(); return; }
    if (!state->composition || state->context != context) {
        state->tainted = true; record.origin = OQ4_TEXT_UNKNOWN;
    }
    bytes = ImmGetCompositionStringW(context, query, NULL, 0);
    if (bytes < 0 || bytes > OQ4_TEXT_MAX || bytes % sizeof(WCHAR)) goto failed;
    wide = (WCHAR *)SDL_malloc((size_t)bytes + sizeof(WCHAR));
    if (!wide) goto failed;
    actual = ImmGetCompositionStringW(context, query, wide, bytes);
    if (actual != bytes) goto failed;
    wide[bytes / sizeof(WCHAR)] = 0;
    /* Embedded NUL and invalid scalar UTF-16 are rejected, never truncated. */
    if (SDL_wcslen(wide) != (size_t)bytes / sizeof(WCHAR)) goto failed;
    utf8_bytes = bytes ? WIN_WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, bytes / sizeof(WCHAR), NULL, 0, NULL, NULL) : 0;
    if ((bytes && utf8_bytes <= 0) || utf8_bytes > OQ4_TEXT_MAX) goto failed;
    text = (char *)SDL_malloc((size_t)utf8_bytes + 1);
    if (!text) goto failed;
    if (bytes && WIN_WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, bytes / sizeof(WCHAR), text, utf8_bytes, NULL, NULL) != utf8_bytes) goto failed;
    text[utf8_bytes] = 0;
    if (kind == OQ4_TEXT_PREEDIT) {
        cursor = ImmGetCompositionStringW(context, GCS_CURSORPOS, NULL, 0);
        if (cursor >= 0 && cursor <= bytes / (LONG)sizeof(WCHAR)) {
            if (cursor > 0 && cursor < bytes / (LONG)sizeof(WCHAR) &&
                wide[cursor - 1] >= 0xd800 && wide[cursor - 1] <= 0xdbff && wide[cursor] >= 0xdc00 && wide[cursor] <= 0xdfff) goto failed;
            record.selection_start = cursor;
            record.selection_length = 0;
        }
    }
    OQ4_Emit(record, text, (size_t)utf8_bytes);
    SDL_free(text); SDL_free(wide); ImmReleaseContext(state->window->internal->hwnd, context);
    return;
failed:
    SDL_free(text); SDL_free(wide); ImmReleaseContext(state->window->internal->hwnd, context);
    OQ4_Cancel(state, message); OQ4_Fault();
}

void OQ4_WIN_ObserveTextMessage(SDL_Window *window, unsigned int msg, uintptr_t wp, intptr_t lp)
{
    OQ4_TextWindow *state;
    OQ4_TextScope parent;
    if (!SDL_IsMainThread()) return;
    if (!oq4_enabled) {
        /* Disable keeps surviving windows tainted, but a destruction callback
         * must still retire its registry entry without emitting any record. */
        if (msg == WM_NCDESTROY && (state = OQ4_Window(window, false))) SDL_zero(*state);
        return;
    }
    if (!(state = OQ4_Window(window, true))) return;
    parent = oq4_scope;
    SDL_zero(oq4_scope);
    oq4_scope.window_id = state->id; oq4_scope.lifetime = state->lifetime;
    oq4_scope.session = state->session; oq4_scope.message = msg;
    if (msg == WM_IME_STARTCOMPOSITION) {
        if (state->composition) state->tainted = true;
        state->seen_ime = true;
        state->composition = OQ4_Identity();
        state->context = ImmGetContext(window->internal->hwnd);
        if (state->context) ImmReleaseContext(window->internal->hwnd, state->context);
        else state->tainted = true;
        OQ4_Notify(state, OQ4_TEXT_BEGIN, msg);
    } else if (msg == WM_IME_COMPOSITION) {
        state->seen_ime = true;
        if (lp & GCS_RESULTSTR) {
            OQ4_TextRecord clear = OQ4_Record(state, OQ4_TEXT_PREEDIT);
            clear.native_message = msg; clear.selection_start = clear.selection_length = 0;
            OQ4_Emit(clear, "", 0);
            OQ4_Composition(state, OQ4_TEXT_RESULT, GCS_RESULTSTR, msg);
        }
        if (lp & (GCS_COMPSTR | GCS_CURSORPOS | GCS_COMPATTR)) OQ4_Composition(state, OQ4_TEXT_PREEDIT, GCS_COMPSTR, msg);
        if (!lp) OQ4_Cancel(state, msg);
    } else if (msg == WM_IME_ENDCOMPOSITION) {
        state->seen_ime = true;
        OQ4_Notify(state, OQ4_TEXT_END, msg);
        state->composition = 0; state->context = NULL;
        /* IMM supplies no origin in a later GCS_RESULTSTR. Even a subsequent
         * Begin cannot prove that such a result belongs to the new interval.
         * This observer stays conservative for this native window lifetime. */
        state->tainted = true;
    } else if (msg == WM_IME_CHAR) {
        state->seen_ime = true;
    } else if (msg == WM_KILLFOCUS || msg == WM_INPUTLANGCHANGE || (msg == WM_SHOWWINDOW && !wp)) {
        OQ4_Cancel(state, msg);
    } else if (msg == WM_NCDESTROY) {
        OQ4_Notify(state, OQ4_TEXT_WINDOW_END, msg);
        SDL_zero(*state); return;
    }
    /* Only a synchronous nested character emitted by the same observed result
     * scope inherits provenance. Later WM_CHAR cannot match by value/time. */
    if ((msg == WM_CHAR || msg == WM_UNICHAR || msg == WM_IME_CHAR) &&
        parent.window_id == state->id && parent.lifetime == state->lifetime &&
        parent.origin == OQ4_TEXT_OBSERVED_COMPOSITION && !state->tainted) {
        oq4_scope.origin = parent.origin; oq4_scope.composition = parent.composition;
    } else if (msg == WM_IME_COMPOSITION && state->composition && !state->tainted) {
        oq4_scope.origin = OQ4_TEXT_OBSERVED_COMPOSITION; oq4_scope.composition = state->composition;
    } else if ((msg == WM_CHAR || msg == WM_UNICHAR) && !state->seen_ime && !state->tainted) {
        oq4_scope.origin = OQ4_TEXT_UNMARKED_CHARACTER;
    }
}
void OQ4_WIN_AssociateText(SDL_Event *event, OQ4_TextScope admission)
{
    OQ4_TextWindow *window;
    OQ4_TextRecord record;
    size_t length;
    event->common.reserved = 0;
    /* SDL_PushEvent itself permits other threads. They must not inspect the
     * main-thread observer state or acquire a native composition affinity. */
    if (!SDL_IsMainThread()) { event->common.reserved = OQ4_TEXT_FAILED; return; }
    if (!oq4_enabled) return;
    event->common.reserved = OQ4_TEXT_FAILED;
    if (!OQ4_Main() || !oq4_healthy) return;
    if (!event->text.text) { OQ4_Fault(); return; }
    window = OQ4_Window(SDL_GetWindowFromID(event->text.windowID), true);
    if (!window) { OQ4_Fault(); return; }
    record = OQ4_Record(window, OQ4_TEXT_COMMIT);
    record.origin = OQ4_TEXT_UNKNOWN; record.composition = 0;
    record.native_message = admission.message;
    if (admission.window_id == window->id && admission.lifetime == window->lifetime && admission.session == window->session) {
        record.origin = admission.origin; record.composition = admission.composition;
    }
    length = SDL_strnlen(event->text.text, OQ4_TEXT_MAX + 1);
    event->common.reserved = OQ4_Emit(record, event->text.text, length);
}
void OQ4_WIN_DropTextAssociation(const SDL_Event *event)
{
    SDL_Event marker;
    OQ4_TextSlot *slot;
    if (!SDL_IsMainThread() || !event->common.reserved || event->common.reserved == OQ4_TEXT_FAILED) return;
    SDL_zero(marker); marker.type = oq4_event_type;
    marker.user.code = (Sint32)event->common.reserved;
    marker.user.windowID = event->text.windowID;
    slot = OQ4_Find(&marker);
    if (slot) OQ4_FreeSlot(slot);
    OQ4_Fault();
}
#endif
