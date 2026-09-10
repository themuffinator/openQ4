// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// Included after extracted production SDL queue methods; no device/native input.
static sysEventDispositionTag_t Tag(std::uint64_t emission = 1) {
    return {Sys_EventDispositionEpoch(), Sys_EventQueueToken(), 3, 4, 5, 6, 7, 8, 9, 10, 11, 0, emission};
}
static sysKeyboardInputDisposition_t Keyboard(int key = 'a', std::uint64_t child = 0) {
    return {key, true, 17, {Tag(), child}};
}
static sysMouseInputDisposition_t Mouse(int action = M_DELTAX, int value = 21) {
    return {action, value, 19, Tag()};
}
static bool Same(const sysKeyboardInputDisposition_t& a, const sysKeyboardInputDisposition_t& b) {
    return a.key == b.key && a.down == b.down && a.time == b.time &&
        a.disposition.parent == b.disposition.parent && a.disposition.deferredEmission == b.disposition.deferredEmission;
}
static bool Same(const sysMouseInputDisposition_t& a, const sysMouseInputDisposition_t& b) {
    return a.action == b.action && a.value == b.value && a.time == b.time && a.disposition == b.disposition;
}
static void Clear() {
    const auto token = Sys_EventQueueToken();
    Sys_ClearInputEvents();
    Check(Sys_EventQueueToken() != token, "every clear invalidates continuity including empty storage");
    Check(s_keyboardHead == s_keyboardTail && s_mouseHead == s_mouseTail &&
        !s_polledKeyboardCount && !s_polledMouseCount && !s_keyboardDispositionState.checked &&
        !s_mouseDispositionState.checked, "clear discards rings and slices");
    for (int i = 0; i < SDL3_INPUT_QUEUE_SIZE; ++i) {
        Check(s_keyboardDisposition[i].Empty() && s_mouseDisposition[i].Empty() &&
            s_polledKeyboardDisposition[i].Empty() && s_polledMouseDisposition[i].Empty(), "clear erases all tags");
    }
}
static void RoundTrip() {
    Clear();
    SDL3_QueueKeyboardInput('b', false, 6);
    auto key = Keyboard(K_CTRL, 2); const auto savedKey = key;
    Check(Sys_QueKeyboardInputWithDisposition(key) && Same(key, {}), "tracked keyboard admission consumes whole caller");
    SDL3_QueueKeyboardInput('c', true, 7);
    auto mouse = Mouse(); const auto savedMouse = mouse;
    Check(Sys_QueMouseInputWithDisposition(mouse) && Same(mouse, {}), "tracked mouse admission consumes whole caller");
    sysInputDispositionSlice_t keys, mice;
    const auto pump = pumps, emitted = emissions.size();
    Check(Sys_PollKeyboardInputWithDisposition(keys) && keys.count == 3 && keys.lane == sysInputDispositionLane_t::Keyboard, "checked keyboard preserves mixed FIFO");
    Check(Sys_PollMouseInputWithDisposition(mice) && mice.count == 1 && mice.lane == sysInputDispositionLane_t::Mouse && mice.serial > keys.serial, "checked mouse has distinct monotonic slice");
    auto replaced = keys;
    Check(!Sys_PollKeyboardInputWithDisposition(replaced) && replaced == keys, "unread checked slice cannot be overwritten");
    Check(!Sys_EndKeyboardInputWithDisposition(keys), "cannot end before all slots transferred");
    sysInputDispositionSlot_t slot;
    auto out = Keyboard('z');
    for (unsigned i = 0; i < keys.count; ++i) {
        Check(Sys_PeekKeyboardInputWithDisposition(keys, slot, out) == sysEventTransfer_t::Ready && slot.index == i, "peek exact FIFO slot");
        const auto before = out;
        auto invalid = slot; ++invalid.index;
        Check(Sys_TakeKeyboardInputWithDisposition(invalid, out) == sysEventTransfer_t::Refused && Same(out, before), "skip or reorder refuses output atomically");
        Check(Sys_TakeKeyboardInputWithDisposition(slot, out) == sysEventTransfer_t::Ready, "exact take transfers once");
        Check(i == 1 ? Same(out, savedKey) : (out.key == (i == 0 ? 'b' : 'c') && out.disposition.Empty()), "complete scalar/tag identity survives");
        Check(Sys_TakeKeyboardInputWithDisposition(slot, out) == sysEventTransfer_t::Refused, "duplicate slot cannot replay");
    }
    const auto before = out; const auto beforeSlot = slot;
    Check(Sys_PeekKeyboardInputWithDisposition(keys, slot, out) == sysEventTransfer_t::Empty && Same(out, before) &&
        slot.slice == beforeSlot.slice && slot.index == beforeSlot.index, "empty peek preserves both outputs");
    Check(Sys_EndKeyboardInputWithDisposition(keys) && !Sys_EndKeyboardInputWithDisposition(keys), "exact End consumes slice once");
    auto mout = Mouse(M_DELTAZ, 88);
    Check(Sys_PeekMouseInputWithDisposition(mice, slot, mout) == sysEventTransfer_t::Ready, "peek mouse");
    Check(Sys_TakeMouseInputWithDisposition(slot, mout) == sysEventTransfer_t::Ready && Same(mout, savedMouse), "take exact mouse");
    Check(Sys_TakeMouseInputWithDisposition(slot, mout) == sysEventTransfer_t::Refused && Sys_EndMouseInputWithDisposition(mice), "mouse replay refused then End");
    Check(pumps == pump && emissions.size() == emitted, "checked storage neither pumps nor emits delayed modifier child");
    Check(Sys_PollKeyboardInputWithDisposition(replaced) && !replaced.count && replaced.serial > mice.serial, "empty slice is honest new observation");
    Check(!Sys_EndKeyboardInputWithDisposition(keys) && Sys_EndKeyboardInputWithDisposition(replaced), "old slice cannot end new empty slice");
}
static void InvalidAdmissions() {
    for (int field = 0; field < 13; ++field) {
        Clear(); auto input = Keyboard();
        auto& t = input.disposition.parent;
        std::array<std::uint64_t*, 13> fields{&t.dispatchEpoch,&t.streamToken,&t.providerEpoch,&t.route,&t.window,&t.windowLifetime,
            &t.ingress,&t.batchSerial,&t.ledger,&t.ledgerSerial,&t.queueSequence,&t.recordIndex,&t.emission};
        *fields[field] = field == 11 ? 8192 : 0;
        const auto saved = input; const auto token = Sys_EventQueueToken();
        Check(!Sys_QueKeyboardInputWithDisposition(input) && Same(input, saved), "malformed parent rejected without caller consumption");
        Check(s_keyboardHead == s_keyboardTail && token == Sys_EventQueueToken(), "invalid caller cannot perturb existing continuity");
    }
    for (int bad = 0; bad < 8; ++bad) {
        Clear(); auto key = Keyboard(); auto mouse = Mouse();
        if (bad == 0) { key.key = 0; mouse.action = -1; }
        if (bad == 1) { key.key = K_LAST_KEY; mouse.action = M_DELTAZ + 1; }
        if (bad == 2) { key.time = -1; mouse.time = -1; }
        if (bad == 3) { key.disposition = {}; mouse.disposition = {}; }
        if (bad == 4) { ++key.disposition.parent.dispatchEpoch; ++mouse.disposition.dispatchEpoch; }
        if (bad == 5) { ++key.disposition.parent.streamToken; ++mouse.disposition.streamToken; }
        if (bad == 6) { key.disposition.deferredEmission = 2; mouse.value = 0; }
        if (bad == 7) { key.key = K_CTRL; key.disposition.deferredEmission = key.disposition.parent.emission; mouse.disposition.recordIndex = 8192; }
        const auto k = key; const auto m = mouse;
        Check(!Sys_QueKeyboardInputWithDisposition(key) && Same(key, k), "invalid keyboard scalar/child/current refuses");
        Check(!Sys_QueMouseInputWithDisposition(mouse) && Same(mouse, m), "invalid mouse scalar/tag refuses");
    }
}
static void ExactReceiptsAndPreflight() {
    for (int bad = 0; bad < 6; ++bad) {
        Clear(); auto key = Keyboard(); Check(Sys_QueKeyboardInputWithDisposition(key), "receipt fixture admitted");
        sysInputDispositionSlice_t slice; Check(Sys_PollKeyboardInputWithDisposition(slice), "receipt fixture polled");
        auto wrong = slice;
        if (bad == 0) ++wrong.epoch;
        if (bad == 1) ++wrong.streamToken;
        if (bad == 2) ++wrong.serial;
        if (bad == 3) wrong.lane = sysInputDispositionLane_t::Mouse;
        if (bad == 4) ++wrong.count;
        if (bad == 5) wrong.lane = static_cast<sysInputDispositionLane_t>(17);
        auto out = Keyboard('q'); const auto saved = out;
        sysInputDispositionSlot_t slot{wrong, 9}, savedSlot = slot;
        Check(Sys_PeekKeyboardInputWithDisposition(wrong, slot, out) == sysEventTransfer_t::Refused && Same(out, saved) &&
            slot.slice == savedSlot.slice && slot.index == savedSlot.index, "every receipt component checked before outputs");
        Check(Sys_TakeKeyboardInputWithDisposition({wrong, 0}, out) == sysEventTransfer_t::Refused && Same(out, saved), "wrong exact Take refuses");
        Check(!Sys_EndKeyboardInputWithDisposition(wrong), "wrong exact End refuses");
        Check(Sys_PeekKeyboardInputWithDisposition(slice, slot, out) == sysEventTransfer_t::Ready &&
            Sys_TakeKeyboardInputWithDisposition(slot, out) == sysEventTransfer_t::Ready && Sys_EndKeyboardInputWithDisposition(slice), "refusals preserve current ownership");
    }
    Clear(); SDL3_QueueKeyboardInput('p', true, 1); auto input = Keyboard();
    Check(Sys_QueKeyboardInputWithDisposition(input), "mixed preflight source queued");
    Sys_InvalidateEventQueue();
    sysInputDispositionSlice_t output{98, 99, 100, sysInputDispositionLane_t::Mouse, 12}; const auto saved = output;
    const int tail = s_keyboardTail;
    Check(!Sys_PollKeyboardInputWithDisposition(output) && output == saved && s_keyboardTail == tail && s_polledKeyboardCount == 0,
        "stale later tag rejects full preflight without consuming safe prefix");
    Clear(); input = Keyboard(); Check(Sys_QueKeyboardInputWithDisposition(input) && Sys_PollKeyboardInputWithDisposition(output), "stale active slice fixture");
    const auto epoch = Sys_EventDispositionEpoch();
    Check(Sys_RetireEventDispositionThread() && Sys_EventDispositionBoundThread() && !Sys_EventDispositionEpoch(), "retirement preserves constructing thread identity only");
    sysInputDispositionSlot_t slot; auto out = Keyboard('z'); const auto before = out;
    Check(Sys_PeekKeyboardInputWithDisposition(output, slot, out) == sysEventTransfer_t::Refused && Same(out, before), "retired epoch cannot deliver");
    Check(Sys_BindEventDispositionThread() > epoch && !Sys_EndKeyboardInputWithDisposition(output), "fresh epoch cannot revive stale slice");
    Clear();
}
static void LegacyBoundaries() {
    Clear();
    const auto beforePump = pumps;
    SDL3_QueueKeyboardInput(K_ALT, true, 14); SDL3_QueueKeyboardInput('a', false, 15);
    SDL3_QueueMouseInput(M_DELTAX, 0, 1); SDL3_QueueMouseInput(-1, 3, 1); SDL3_QueueMouseInput(M_ACTION1, 0, 8);
    Check(Sys_PollKeyboardInputEvents() == 2 && Sys_PollMouseInputEvents() == 1, "ordinary scalar validation/poll preserved");
#if defined(OPENQ4_SDL3_POSIX_HOST)
    Check(pumps == beforePump + 1, "POSIX legacy mouse entry still pumps exactly once before lock");
#else
    Check(pumps == beforePump, "Windows legacy mouse entry does not pump");
#endif
    sysInputDispositionSlice_t refused{9,9,9,sysInputDispositionLane_t::Mouse,9}; const auto saved = refused;
    Check(!Sys_PollKeyboardInputWithDisposition(refused) && refused == saved, "checked poll refuses unread legacy slice");
    int ch = 99, action = 99, value = 99; bool down = false;
    const auto emitted = emissions.size();
    Check(Sys_ReturnKeyboardInputEvent(0,ch,down) == K_ALT && ch == K_ALT && down, "ordinary modifier Return preserved");
    Check(emissions.size() == emitted + 1 && emissions.back().key == K_ALT && emissions.back().time == 14, "legacy delayed child remains at Return");
    Check(Sys_ReturnKeyboardInputEvent(1,ch,down) == 'a' && !down && emissions.size() == emitted + 1, "ordinary nonmodifier has no delayed child");
    Check(Sys_ReturnMouseInputEvent(0,action,value) == 1 && action == M_ACTION1 && value == 0, "ordinary release allowed");
    Check(Sys_ReturnKeyboardInputEvent(-1,ch,down) == 0 && !ch && !down && Sys_ReturnMouseInputEvent(99,action,value) == 0 && !action && !value,
        "legacy invalid indices still clear outputs");
    Sys_EndKeyboardInputEvents(); Sys_EndMouseInputEvents();
    Check(Sys_PollKeyboardInputWithDisposition(refused) && Sys_EndKeyboardInputWithDisposition(refused), "legacy End makes slice available");
    for (int deferred : {K_CTRL,K_ALT,K_RIGHT_ALT,K_PRINT_SCR}) {
        SDL3_QueueKeyboardInput(deferred,false,36);
        Check(Sys_PollKeyboardInputEvents()==1,"each historical delayed key polled");
        const auto count=emissions.size();
        Check(Sys_ReturnKeyboardInputEvent(0,ch,down)==deferred&&!down&&emissions.size()==count+1&&
            emissions.back().key==deferred&&emissions.back().time==36&&!emissions.back().down,"all four delayed release emissions preserved");
        Sys_EndKeyboardInputEvents();
        auto tracked=Keyboard(deferred,2);Check(Sys_QueKeyboardInputWithDisposition(tracked),"each delayed key can retain exact child reservation");
        sysInputDispositionSlice_t childSlice;sysInputDispositionSlot_t childSlot;
        Check(Sys_PollKeyboardInputWithDisposition(childSlice)&&Sys_PeekKeyboardInputWithDisposition(childSlice,childSlot,tracked)==sysEventTransfer_t::Ready&&
            Sys_TakeKeyboardInputWithDisposition(childSlot,tracked)==sysEventTransfer_t::Ready&&tracked.disposition.deferredEmission==2&&
            Sys_EndKeyboardInputWithDisposition(childSlice)&&emissions.size()==count+1,"checked storage keeps reservation without emitting it");
    }

    for (int lane = 0; lane < 2; ++lane) {
        Clear();
        if (!lane) {
            SDL3_QueueKeyboardInput('a', true, 1); auto key = Keyboard(K_CTRL,2); Check(Sys_QueKeyboardInputWithDisposition(key), "legacy blocked key queued"); SDL3_QueueKeyboardInput('b',true,2);
        } else {
            SDL3_QueueMouseInput(M_DELTAX,1,1); auto mouse=Mouse(); Check(Sys_QueMouseInputWithDisposition(mouse), "legacy blocked mouse queued"); SDL3_QueueMouseInput(M_DELTAX,2,2);
        }
        const auto token = Sys_EventQueueToken();
        Check((lane ? Sys_PollMouseInputEvents() : Sys_PollKeyboardInputEvents()) == 1, "legacy drains only untagged prefix");
        Check(Sys_EventQueueToken() != token && (lane ? (s_mouseHead-s_mouseTail)&SDL3_INPUT_QUEUE_MASK : (s_keyboardHead-s_keyboardTail)&SDL3_INPUT_QUEUE_MASK) == 2,
            "tagged head invalidates and preserves itself plus suffix");
        if (lane) { Check(Sys_ReturnMouseInputEvent(0,action,value)==1 && value==1, "mouse prefix returned"); Sys_EndMouseInputEvents(); }
        else { Check(Sys_ReturnKeyboardInputEvent(0,ch,down)=='a', "key prefix returned"); Sys_EndKeyboardInputEvents(); }
        Check((lane ? Sys_PollMouseInputEvents() : Sys_PollKeyboardInputEvents()) == 0, "legacy never bypasses tagged head on next poll");
    }
    for (int attempted = 0; attempted < 6; ++attempted) {
        Clear(); auto key=Keyboard(K_CTRL,2);auto mouse=Mouse();
        Check(Sys_QueKeyboardInputWithDisposition(key) && Sys_QueMouseInputWithDisposition(mouse), "checked protection fixture queued");
        sysInputDispositionSlice_t keys,mice;
        Check(Sys_PollKeyboardInputWithDisposition(keys) && Sys_PollMouseInputWithDisposition(mice), "checked protection fixture polled");
        const auto token=Sys_EventQueueToken(), emittedBefore=emissions.size();
        if(attempted==0)Check(Sys_PollKeyboardInputEvents()==0,"legacy key poll blocked");
        if(attempted==1)Check(Sys_PollMouseInputEvents()==0,"legacy mouse poll blocked");
        if(attempted==2)Check(Sys_ReturnKeyboardInputEvent(0,ch,down)==0,"legacy key Return blocked");
        if(attempted==3)Check(Sys_ReturnMouseInputEvent(0,action,value)==0,"legacy mouse Return blocked");
        if(attempted==4)Sys_EndKeyboardInputEvents();
        if(attempted==5)Sys_EndMouseInputEvents();
        Check(Sys_EventQueueToken()!=token && emissions.size()==emittedBefore && s_keyboardDispositionState.checked && s_mouseDispositionState.checked &&
            s_keyboardDispositionState.next==0 && s_mouseDispositionState.next==0 && s_polledKeyboardCount==1 && s_polledMouseCount==1,
            "legacy access cannot consume, overwrite, emit or end checked slice");
    }
    Clear(); SDL3_QueueKeyboardInput('a',true,1); Check(Sys_PollKeyboardInputEvents()==1,"legacy overwrite baseline");
    SDL3_QueueKeyboardInput('b',false,2); Check(Sys_PollKeyboardInputEvents()==1 && Sys_ReturnKeyboardInputEvent(0,ch,down)=='b',"legacy untracked overwrite policy unchanged");
    Sys_EndKeyboardInputEvents();
}
static void CapacityThreadAndSaturation() {
    for (int cycle=0; cycle<4; ++cycle) {
        Clear();
        for(int i=0;i<511;++i) { auto key=Keyboard();key.time=i;auto mouse=Mouse(M_DELTAX,i+1);
            Check(Sys_QueKeyboardInputWithDisposition(key)&&Sys_QueMouseInputWithDisposition(mouse),"fill all 511 usable slots"); }
        sysInputDispositionSlice_t keys,mice;
        Check(Sys_PollKeyboardInputWithDisposition(keys)&&keys.count==511&&Sys_PollMouseInputWithDisposition(mice)&&mice.count==511,"full ring transfers bounded slice");
        for(unsigned i=0;i<511;++i) { sysKeyboardInputDisposition_t key;sysMouseInputDisposition_t mouse;sysInputDispositionSlot_t slot;
            Check(Sys_PeekKeyboardInputWithDisposition(keys,slot,key)==sysEventTransfer_t::Ready && Sys_TakeKeyboardInputWithDisposition(slot,key)==sysEventTransfer_t::Ready && key.time==static_cast<int>(i),"wrapped keyboard FIFO exact");
            Check(Sys_PeekMouseInputWithDisposition(mice,slot,mouse)==sysEventTransfer_t::Ready && Sys_TakeMouseInputWithDisposition(slot,mouse)==sysEventTransfer_t::Ready && mouse.value==static_cast<int>(i)+1,"wrapped mouse FIFO exact"); }
        Check(Sys_EndKeyboardInputWithDisposition(keys)&&Sys_EndMouseInputWithDisposition(mice),"full slices end");
        // No clear between this admission and next poll: wrap the actual ring.
        for(int i=0;i<23;++i)SDL3_QueueKeyboardInput('w',true,i);
        Check(Sys_PollKeyboardInputEvents()==23,"ring wraps after full checked slice"); Sys_EndKeyboardInputEvents();
    }
    for(int lane=0;lane<2;++lane) {
        Clear(); for(int i=0;i<511;++i) { if(lane)SDL3_QueueMouseInput(M_DELTAX,i+1,i);else SDL3_QueueKeyboardInput('a',true,i); }
        auto key=Keyboard();auto mouse=Mouse();const auto k=key;const auto m=mouse;const auto token=Sys_EventQueueToken();
        Check(!(lane?Sys_QueMouseInputWithDisposition(mouse):Sys_QueKeyboardInputWithDisposition(key)),"full checked admission refuses");
        Check(Same(key,k)&&Same(mouse,m)&&Sys_EventQueueToken()!=token,"full refusal preserves caller and invalidates failed fanout");
        Check((lane?s_mouseQueue[s_mouseTail].time:s_keyboardQueue[s_keyboardTail].time)==0,"full checked admission does not evict oldest");
        const auto beforeLegacyEviction=Sys_EventQueueToken();
        if(lane)SDL3_QueueMouseInput(M_DELTAX,999,999);else SDL3_QueueKeyboardInput('b',true,999);
        Check(Sys_EventQueueToken()!=beforeLegacyEviction && (lane?s_mouseQueue[s_mouseTail].time:s_keyboardQueue[s_keyboardTail].time)==1,"legacy overflow still evicts oldest with continuity loss");
    }
    Clear();for(int i=0;i<510;++i)SDL3_QueueKeyboardInput('p',true,i);
    auto firstSibling=Keyboard(K_CTRL,2),secondSibling=Keyboard('s');
    const auto firstIdentity=firstSibling,secondIdentity=secondSibling;
    Check(Sys_QueKeyboardInputWithDisposition(firstSibling)&&Same(firstSibling,{}),"first fanout sibling fits final free slot");
    const auto siblingToken=Sys_EventQueueToken();
    Check(!Sys_QueKeyboardInputWithDisposition(secondSibling)&&Same(secondSibling,secondIdentity)&&Sys_EventQueueToken()!=siblingToken,
        "partial fanout failure preserves second caller and invalidates before effects");
    Check(s_keyboardDisposition[(s_keyboardHead-1)&SDL3_INPUT_QUEUE_MASK].parent==firstIdentity.disposition.parent&&
        ((s_keyboardHead-s_keyboardTail)&SDL3_INPUT_QUEUE_MASK)==511,"successful earlier sibling remains owned at exact queue slot");
    sysInputDispositionSlice_t refused{};
    Check(!Sys_PollKeyboardInputWithDisposition(refused)&&!refused.serial,"partial failure cannot publish old tagged sibling as valid input");
    Clear(); auto key=Keyboard();Check(Sys_QueKeyboardInputWithDisposition(key),"thread fixture queued");sysInputDispositionSlice_t slice;
    Check(Sys_PollKeyboardInputWithDisposition(slice),"thread fixture polled");const auto token=Sys_EventQueueToken();
    std::thread worker([&]{
        Check(!Sys_EventDispositionBoundThread()&&!Sys_EventDispositionEpoch()&&!Sys_RetireEventDispositionThread(),"worker cannot impersonate bound thread");
        auto input=Keyboard();input.disposition.parent=Tag();input.disposition.parent.dispatchEpoch=slice.epoch;const auto original=input;
        Check(!Sys_QueKeyboardInputWithDisposition(input)&&Same(input,original),"worker admission refuses even copied epoch");
        auto out=slice;Check(!Sys_PollMouseInputWithDisposition(out)&&out==slice,"worker Poll output atomic");
        sysInputDispositionSlot_t slot{slice,0};auto value=Keyboard();const auto saved=value;
        Check(Sys_PeekKeyboardInputWithDisposition(slice,slot,value)==sysEventTransfer_t::Refused&&Same(value,saved),"worker Peek refused");
        Check(Sys_TakeKeyboardInputWithDisposition(slot,value)==sysEventTransfer_t::Refused&&Same(value,saved)&&!Sys_EndKeyboardInputWithDisposition(slice),"worker Take/End refused");
    });worker.join();
    Check(Sys_EventQueueToken()==token&&s_keyboardDispositionState.next==0,"wrong thread cannot mutate live ownership");
    const auto beforeClear=slice;
    Clear();
    auto clearOutput=Keyboard('v');const auto clearSaved=clearOutput;
    sysInputDispositionSlot_t clearSlot{beforeClear,0};
    Check(Sys_PeekKeyboardInputWithDisposition(beforeClear,clearSlot,clearOutput)==sysEventTransfer_t::Refused&&
        Sys_TakeKeyboardInputWithDisposition(clearSlot,clearOutput)==sysEventTransfer_t::Refused&&Same(clearOutput,clearSaved)&&
        !Sys_EndKeyboardInputWithDisposition(beforeClear),"explicit clear retires every old slice/slot without delivery");
    // Simulate an ordinary producer completing immediately before the Poll lock.
    beforeLock=[] { SDL3_QueueKeyboardInput('r',true,32); };
    Check(Sys_PollKeyboardInputWithDisposition(slice)&&slice.count==1,"head sampled under actual storage lock, not before acquisition");
    Clear();
    const auto old=slice; s_inputDispositionHighwater=(std::numeric_limits<std::uint64_t>::max)()-1;
    Check(Sys_PollKeyboardInputWithDisposition(slice)&&slice.serial==(std::numeric_limits<std::uint64_t>::max)()&&Sys_EndKeyboardInputWithDisposition(slice),"last nonreused serial can end");
    auto out=old;Check(!Sys_PollMouseInputWithDisposition(out)&&out==old,"serial exhaustion preserves output");
    Clear();Check(!Sys_PollKeyboardInputWithDisposition(out)&&out==old,"clear cannot reset exhausted highwater");
}
int main() {
    Check(!Sys_EventDispositionBoundThread()&&!Sys_EventDispositionEpoch(),"unbound process unavailable");
    Check(Sys_BindEventDispositionThread()!=0&&Sys_EventDispositionBoundThread(),"event thread binds");
    RoundTrip(); InvalidAdmissions(); ExactReceiptsAndPreflight(); LegacyBoundaries(); CapacityThreadAndSaturation();
    Check(lockDepth==0,"all storage critical sections balanced");
    std::printf("Input disposition storage passed %u checks\n",checks);
    return 0;
}
