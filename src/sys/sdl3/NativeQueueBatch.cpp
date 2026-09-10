// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "NativeQueueBatch.h"
#include "../../ui/retained/TextInput.h"
#include <atomic>
#include <cstring>
#include <limits>

namespace openq4 {
namespace {
std::atomic<std::uint64_t> nextIngress{1};
bool SameFence(const std::optional<OQ4_NativeFence>& a,const std::optional<OQ4_NativeFence>& b) {
    return bool(a)==bool(b) && (!a || (a->version==b->version && a->event_count==b->event_count && a->dispatch==b->dispatch && a->sequence==b->sequence));
}
bool ValidStatus(const NativeQueueStatus& s) {
    return s.mainThread && s.healthy && s.providerEpoch && s.generation && s.engineToken &&
        s.fenceEventType>=SDL_EVENT_USER && s.fenceEventType<SDL_EVENT_LAST &&
        (!s.pending || (s.pending->version==1 && s.pending->dispatch && s.pending->sequence && s.pending->event_count<=NativeQueueIngress::MaxCollectionEvents));
}
bool SameStatus(const NativeQueueStatus& a,const NativeQueueStatus& b,bool fence) {
    return ValidStatus(a) && a.providerEpoch==b.providerEpoch && a.generation==b.generation &&
        a.engineToken==b.engineToken && a.fenceEventType==b.fenceEventType && (!fence || SameFence(a.pending,b.pending));
}
bool CopyText(const char* text,std::string& out,std::size_t& bytes,std::string& error) {
    if (!text) { error="SDL text payload is null"; return false; }
    std::size_t size=0;
    while(size<=ui::TextInputMaxBytes && text[size]) ++size;
    if (size>ui::TextInputMaxBytes || size+1>NativeQueueIngress::MaxPayloadBytes-bytes) { error="SDL text payload exceeds ingress budget"; return false; }
    const std::string_view view(text,size);
    if (!ui::ValidateTextInputUtf8(view,error)) return false;
    out.assign(view); bytes+=size+1; return true;
}
bool CopyEvent(const SDL_Event& e,const OQ4_NativeQueueRecord& tag,OwnedNativeQueueEvent& out,std::size_t& bytes,std::string& error) {
    std::memset(&out.header,0,sizeof(out.header));
    out.record=tag;
    if (tag.kind==OQ4_QUEUE_FENCE) {
        if(e.user.data1 || e.user.data2 || e.user.windowID || e.user.reserved || e.user.code<=0) { error="Invalid fence marker shape"; return false; }
        out.header.user=e.user; return true;
    }
    if(e.type>=SDL_EVENT_DISPLAY_FIRST && e.type<=SDL_EVENT_DISPLAY_LAST) { out.header.display=e.display; return true; }
    if(e.type>=SDL_EVENT_WINDOW_FIRST && e.type<=SDL_EVENT_WINDOW_LAST) { out.header.window=e.window; return true; }
    switch(e.type) {
    case SDL_EVENT_QUIT: case SDL_EVENT_TERMINATING: case SDL_EVENT_LOW_MEMORY:
    case SDL_EVENT_WILL_ENTER_BACKGROUND: case SDL_EVENT_DID_ENTER_BACKGROUND:
    case SDL_EVENT_WILL_ENTER_FOREGROUND: case SDL_EVENT_DID_ENTER_FOREGROUND:
    case SDL_EVENT_POLL_SENTINEL: out.header.common=e.common; return true;
    case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: out.header.key=e.key; return true;
    case SDL_EVENT_TEXT_INPUT:
        out.header.text=e.text; out.header.text.text=nullptr;
        return CopyText(e.text.text,out.text,bytes,error);
    case SDL_EVENT_TEXT_EDITING:
        out.header.edit=e.edit; out.header.edit.text=nullptr;
        // Preserve SDL's original offsets. This layer does not guess their units.
        return CopyText(e.edit.text,out.text,bytes,error);
    case SDL_EVENT_TEXT_EDITING_CANDIDATES:
        out.header.edit_candidates=e.edit_candidates; out.header.edit_candidates.candidates=nullptr;
        if(e.edit_candidates.num_candidates<0 || std::size_t(e.edit_candidates.num_candidates)>NativeQueueIngress::MaxCandidates ||
            e.edit_candidates.selected_candidate < -1 || e.edit_candidates.selected_candidate>=e.edit_candidates.num_candidates ||
            (e.edit_candidates.num_candidates && !e.edit_candidates.candidates)) { error="Invalid SDL candidate table"; return false; }
        for(int i=0;i<e.edit_candidates.num_candidates;++i) {
            std::string text;
            if(!CopyText(e.edit_candidates.candidates[i],text,bytes,error)) return false;
            out.candidates.push_back(std::move(text));
        }
        return true;
    case SDL_EVENT_MOUSE_MOTION: out.header.motion=e.motion; return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP: out.header.button=e.button; return true;
    case SDL_EVENT_MOUSE_WHEEL: out.header.wheel=e.wheel; return true;
    case SDL_EVENT_GAMEPAD_ADDED: case SDL_EVENT_GAMEPAD_REMOVED: case SDL_EVENT_GAMEPAD_REMAPPED:
    case SDL_EVENT_GAMEPAD_STEAM_HANDLE_UPDATED: out.header.gdevice=e.gdevice; return true;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: case SDL_EVENT_GAMEPAD_BUTTON_UP: out.header.gbutton=e.gbutton; return true;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: out.header.gaxis=e.gaxis; return true;
    case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN: case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION: case SDL_EVENT_GAMEPAD_TOUCHPAD_UP:
        out.header.gtouchpad=e.gtouchpad; return true;
    case SDL_EVENT_GAMEPAD_SENSOR_UPDATE: out.header.gsensor=e.gsensor; return true;
    case SDL_EVENT_JOYSTICK_ADDED: case SDL_EVENT_JOYSTICK_REMOVED: out.header.jdevice=e.jdevice; return true;
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN: case SDL_EVENT_JOYSTICK_BUTTON_UP: out.header.jbutton=e.jbutton; return true;
    case SDL_EVENT_JOYSTICK_HAT_MOTION: out.header.jhat=e.jhat; return true;
    case SDL_EVENT_JOYSTICK_AXIS_MOTION: out.header.jaxis=e.jaxis; return true;
    case SDL_EVENT_FINGER_DOWN: case SDL_EVENT_FINGER_MOTION: case SDL_EVENT_FINGER_UP: case SDL_EVENT_FINGER_CANCELED:
        out.header.tfinger=e.tfinger; return true;
    // Audited current engine switch ignores these families. Preserve their place
    // without dereferencing/copying unneeded drop or clipboard MIME pointers.
    case SDL_EVENT_CLIPBOARD_UPDATE: case SDL_EVENT_DROP_FILE: case SDL_EVENT_DROP_TEXT:
    case SDL_EVENT_DROP_BEGIN: case SDL_EVENT_DROP_COMPLETE: case SDL_EVENT_DROP_POSITION:
    case SDL_EVENT_LOCALE_CHANGED: case SDL_EVENT_SYSTEM_THEME_CHANGED: case SDL_EVENT_KEYMAP_CHANGED:
    case SDL_EVENT_KEYBOARD_ADDED: case SDL_EVENT_KEYBOARD_REMOVED:
    case SDL_EVENT_SCREEN_KEYBOARD_SHOWN: case SDL_EVENT_SCREEN_KEYBOARD_HIDDEN:
    case SDL_EVENT_MOUSE_ADDED: case SDL_EVENT_MOUSE_REMOVED:
    case SDL_EVENT_JOYSTICK_BALL_MOTION: case SDL_EVENT_JOYSTICK_BATTERY_UPDATED: case SDL_EVENT_JOYSTICK_UPDATE_COMPLETE:
    case SDL_EVENT_GAMEPAD_UPDATE_COMPLETE: case SDL_EVENT_AUDIO_DEVICE_ADDED: case SDL_EVENT_AUDIO_DEVICE_REMOVED:
    case SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED: case SDL_EVENT_SENSOR_UPDATE:
    case SDL_EVENT_RENDER_TARGETS_RESET: case SDL_EVENT_RENDER_DEVICE_RESET: case SDL_EVENT_RENDER_DEVICE_LOST:
        out.header.common=e.common; out.ignored=true; return true;
    default: error="Unsupported SDL record in checked ingress"; return false;
    }
}
struct CallGuard { bool& calling; explicit CallGuard(bool& v):calling(v){v=true;} ~CallGuard(){calling=false;} };
}
NativeQueueIngress::NativeQueueIngress() {
    auto value=nextIngress.load();
    while(value && !nextIngress.compare_exchange_weak(value,value==(std::numeric_limits<std::uint64_t>::max)()?0:value+1)) {}
    identity=value;
}
bool NativeQueueIngress::Fail(std::string& error,const char* message) { phase=Phase::Retire; error=message; return false; }
bool NativeQueueIngress::CheckSource(NativeQueueSource& source,const NativeQueueStatus& expected,bool fence,std::string& error) {
    NativeQueueStatus current;
    if(!source.Observe(current,error) || phase==Phase::Retire || !SameStatus(current,expected,fence)) return Fail(error,"Native queue source changed; retirement required");
    return true;
}
NativeQueueRead NativeQueueIngress::Read(NativeQueueSource& source,std::unique_ptr<const NativeQueueBatch>& out,std::string& error) {
    if(calling) { reentered=true; Fail(error,"Reentrant native ingress"); return NativeQueueRead::RetireRequired; }
    if(phase==Phase::Retire) { error="Native ingress requires retirement"; return NativeQueueRead::RetireRequired; }
    if(phase==Phase::Published) { error="Native batch is still published"; return NativeQueueRead::Busy; }
    CallGuard guard(calling); reentered=false;
    try {
        NativeQueueStatus status;
        if(!identity || serial==(std::numeric_limits<std::uint64_t>::max)() || !source.Observe(status,error) || phase==Phase::Retire || !ValidStatus(status) ||
            status.providerEpoch<lastEpoch || (requireFreshGeneration && status.providerEpoch!=retirementEpoch) ||
            (lastEpoch && !requireFreshGeneration && (status.providerEpoch!=lastEpoch || status.generation!=lastGeneration)) || (status.providerEpoch==lastEpoch &&
            (status.generation<lastGeneration || (requireFreshGeneration && status.generation<=lastGeneration)))) {
            Fail(error,"Unavailable or stale native queue source"); return NativeQueueRead::RetireRequired;
        }
        auto candidate=std::unique_ptr<NativeQueueBatch>(new NativeQueueBatch);
        candidate->status=status;
        std::size_t bytes=0,ordinal=0;
        auto sequence=(status.providerEpoch==lastEpoch && status.generation==lastGeneration)?lastSequence:0;
        // Capture the generation before any Poll, including failed attempts.
        lastEpoch=status.providerEpoch; lastGeneration=status.generation; lastSequence=sequence;
        for(;;) {
            if(!CheckSource(source,status,true,error)) return NativeQueueRead::RetireRequired;
            SDL_Event event{}; OQ4_NativeQueueRecord record{};
            const int read=source.Poll(event,record);
            if(phase==Phase::Retire || read<0 || read>1) { Fail(error,"Checked native Poll failed"); return NativeQueueRead::RetireRequired; }
            if(!read) {
                if(!CheckSource(source,status,true,error)) return NativeQueueRead::RetireRequired;
                if(status.pending) { Fail(error,"Native collection ended without verified fence"); return NativeQueueRead::RetireRequired; }
                if(candidate->events.empty()) { requireFreshGeneration=false; error.clear(); return NativeQueueRead::Empty; }
                break;
            }
            if(candidate->events.size()==MaxEvents || record.version!=1 || record.reserved || record.generation!=status.generation ||
                !record.queue_sequence || record.queue_sequence<=sequence || record.kind>OQ4_QUEUE_FENCE ||
                (event.type==SDL_EVENT_POLL_SENTINEL && record.kind!=OQ4_QUEUE_SENTINEL)) {
                Fail(error,"Invalid native queue entry or budget exceeded"); return NativeQueueRead::RetireRequired;
            }
            sequence=record.queue_sequence;
            if(record.kind==OQ4_QUEUE_COLLECTION) {
                if(!status.pending || record.dispatch!=status.pending->dispatch || record.ordinal!=ordinal+1 || record.ordinal>status.pending->event_count || record.fence_sequence) {
                    Fail(error,"Native collection ordinal mismatch"); return NativeQueueRead::RetireRequired;
                }
                ++ordinal;
            } else if(record.kind==OQ4_QUEUE_FENCE) {
                if(!status.pending || event.type!=status.fenceEventType || record.dispatch!=status.pending->dispatch || record.fence_sequence!=status.pending->sequence ||
                    record.ordinal!=status.pending->event_count || ordinal!=status.pending->event_count) {
                    Fail(error,"Native fence does not complete its collection"); return NativeQueueRead::RetireRequired;
                }
            } else if(record.dispatch || record.ordinal || record.fence_sequence ||
                ((record.kind==OQ4_QUEUE_SENTINEL)!=(event.type==SDL_EVENT_POLL_SENTINEL))) {
                Fail(error,"Invalid outside/sentinel native record"); return NativeQueueRead::RetireRequired;
            }
            OwnedNativeQueueEvent owned;
            if(!CopyEvent(event,record,owned,bytes,error)) { phase=Phase::Retire; return NativeQueueRead::RetireRequired; }
            if(!CheckSource(source,status,true,error)) return NativeQueueRead::RetireRequired;
            candidate->events.push_back(std::move(owned));
            if(record.kind==OQ4_QUEUE_FENCE) {
                OQ4_NativeFence proof{};
                if(!source.CopyFence(event,proof) || !SameFence(proof,status.pending) || !CheckSource(source,status,true,error)) {
                    Fail(error,"Native fence receipt acquisition failed"); return NativeQueueRead::RetireRequired;
                }
                break;
            }
        }
        if(!CheckSource(source,status,true,error)) return NativeQueueRead::RetireRequired;
        published=status; candidate->receipt={identity,++serial};
        lastSequence=sequence; requireFreshGeneration=false; phase=Phase::Published;
        out=std::move(candidate); error.clear(); return NativeQueueRead::Ready;
    } catch(...) { Fail(error,"Native ingress allocation or source failure"); return NativeQueueRead::RetireRequired; }
}
bool NativeQueueIngress::Validate(NativeQueueSource& source,NativeQueueReceipt expected,std::string& error) {
    if(calling) { reentered=true; return Fail(error,"Reentrant native ingress"); }
    if(phase!=Phase::Published || expected!=NativeQueueReceipt{identity,serial}) { error="Stale native batch receipt"; return false; }
    CallGuard guard(calling);
    try { if(!CheckSource(source,published,true,error)) return false; error.clear(); return true; }
    catch(...) { return Fail(error,"Native source validation failed"); }
}
bool NativeQueueIngress::Finish(NativeQueueSource& source,NativeQueueReceipt expected,std::string& error) {
    if(calling) { reentered=true; return Fail(error,"Reentrant native ingress"); }
    if(phase!=Phase::Published || expected!=NativeQueueReceipt{identity,serial}) { error="Stale native batch receipt"; return false; }
    CallGuard guard(calling);
    try {
        NativeQueueStatus current;
        if(!source.Observe(current,error) || phase==Phase::Retire || !SameStatus(current,published,false)) return Fail(error,"Native source changed before completion");
        if(current.pending) {
            if(!SameFence(current.pending,published.pending)) return Fail(error,"Different native collection before completion");
            error="Native fence still awaits external acknowledgement"; return false;
        }
        phase=Phase::Idle; error.clear(); return true;
    } catch(...) { return Fail(error,"Native source completion failed"); }
}
bool NativeQueueIngress::ResetAfterRetirement(NativeQueueSource& source,std::string& error) {
    if(calling) { reentered=true; return Fail(error,"Reentrant native ingress"); }
    CallGuard guard(calling); reentered=false;
    try {
        NativeQueueStatus status;
        if(!source.Observe(status,error) || reentered || !status.mainThread || !status.providerEpoch || status.providerEpoch<lastEpoch || status.healthy || status.generation || status.pending)
            return Fail(error,"Native provider has not retired");
        phase=Phase::Idle; requireFreshGeneration=true; retirementEpoch=status.providerEpoch; published={}; error.clear(); return true;
    } catch(...) { return Fail(error,"Native source retirement observation failed"); }
}
} // namespace openq4
