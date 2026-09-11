// Actual driver, stores, EventLoop and Usercmd methods plus real native models.
struct DriverFacts final:NativeInputRouteSource,NativeDispositionProbe {
    NativeInputBinding binding;NativeInputEmissionInventory* inventory=nullptr;
    NativeInputRoute* route=nullptr;native_model::Source* source=nullptr;
    std::uint64_t id=0;bool retired=false,completed=false,busy=false;
    mutable std::function<void()> callback;
    bool Observe(NativeInputObservation& out)const noexcept override {
        CHECK(lockDepth==0);auto call=std::move(callback);if(call)call();
        out={binding.outer,binding.editor.allocation,binding.sessionTransition,Sys_EventDispositionEpoch(),Sys_EventQueueToken(),binding.window,true,true,true,true};return true;
    }
    bool Current(const NativeDispositionContext& c)const noexcept override {
        return source && source->status.healthy && c.providerEpoch==source->status.providerEpoch &&
            c.generation==source->status.generation && c.engineToken==Sys_EventQueueToken();
    }
    bool Retirement(std::uint64_t r,const NativeInputBinding& b,NativeInputRetirement& out)const noexcept override {
        if(r!=id || b.editor!=binding.editor || b.native!=binding.native || b.window!=binding.window)return false;
        out={id,binding.native,binding.window,busy?NativeInputUiRetirement::Busy:retired?NativeInputUiRetirement::RetiredExact:NativeInputUiRetirement::Unknown,
            retired?NativeInputNativeRetirement::RetiredExact:NativeInputNativeRetirement::Unknown,
            retired?NativeInputNativeRetirement::RetiredExact:NativeInputNativeRetirement::Unknown,retired,retired,completed || (inventory&&inventory->BacklogDisposed())};return true;
    }
    bool InspectIssued(std::uint64_t r,const NativeInputBinding& b,const sysEventDispositionTag_t& tag,NativeInputIssued& out)const noexcept override {
        return inventory && inventory->Inspect(r,b,tag,out);
    }
};
struct DriverLifecycle final:NativeInputDriverLifecycle {
    DriverFacts& facts;native_model::Run& native;unsigned retires=0,detaches=0,releases=0;
    std::function<void()> callback;
    DriverLifecycle(DriverFacts& f,native_model::Run& n):facts(f),native(n){}
    void RetirePreservingEvents()noexcept override {
        ++retires;auto call=std::move(callback);if(call)call();
        if(!facts.retired){native.owner.RetireExact(native.f.nativeId,native.f.barrier.editor);native.store.RetireExact(native.f.nativeId);facts.retired=true;}
    }
    bool DetachCompleted(NativeInputDriver& driver)noexcept override {
        ++detaches;if(!driver.DetachCompletedInventory(*facts.inventory))return false;facts.completed=true;return true;
    }
    bool ReleaseRetired(std::uint64_t id)noexcept override {++releases;return facts.route->Release(id);}
    bool PumpsRetired()const noexcept override{return facts.retired;}
};
struct DriverFixture {
    native_model::Run n;DriverFacts facts;NativeInputRoute route{facts};
    NativeEventDispositionLedger ledger{facts};NativeInputEmissionInventory inventory;idEventLoop loop;
    NativeInputTransfers transfers{inventory,route,loop};DriverLifecycle lifecycle{facts,n};
    std::unique_ptr<NativeInputDriver> driver;ui::NativeTextCollectionResult nativeResult;std::string error;
    NativeTranslatedEmission initial,mouse;NativeTranslatedKeyboard a,ctrl;
    void* payload=nullptr;
    explicit DriverFixture(NativeDispositionSchedule schedule=NativeDispositionSchedule::BeforeSessionDrain, unsigned extraKeys=0,bool withPayload=false,bool prefix=false,unsigned keyWindow=5,int firstKey='a',SDL_Scancode scan=SDL_SCANCODE_A,bool firstDown=true,bool modifier=true) {
        Sys_ClearEvents();SDL3_ClearInputQueues();CHECK(Sys_BindEventDispositionThread());eventLoop=&loop;
        std::fill_n(idKeyInput::down,K_LAST_KEY,false);localUsercmdGen=idUsercmdGenLocal{};effects.clear();sessionObject.callback={};consoleObject.callback={};consoleObject.closeCallback={};commandObject.callback={};consoleObject.consumes=false;sessionObject.guiActive=&guiEndpoint;sessionObject.game=nullptr;
        n.source.status.engineToken=Sys_EventQueueToken();
        n.store.Seal(1);n.source.status.pending=OQ4_NativeFence{1,3,n.f.dispatch,n.f.fence};
        n.source.Add(SDL_EVENT_KEY_DOWN,OQ4_QUEUE_COLLECTION,1);
        auto& key=n.source.records.back().first.key;key.windowID=keyWindow;key.scancode=scan;key.down=firstDown;key.which=2;
        n.source.Add(SDL_EVENT_KEY_DOWN,OQ4_QUEUE_COLLECTION,2);
        auto& mod=n.source.records.back().first.key;mod.windowID=5;mod.scancode=SDL_SCANCODE_LCTRL;mod.down=true;mod.which=2;
        n.source.Add(SDL_EVENT_MOUSE_MOTION,OQ4_QUEUE_COLLECTION,3);n.source.records.back().first.motion.windowID=5;
        n.source.Add(SDL_EVENT_CLIPBOARD_UPDATE);n.source.Add(SDL_EVENT_POLL_SENTINEL,OQ4_QUEUE_SENTINEL);
        n.source.Add(n.source.status.fenceEventType,OQ4_QUEUE_FENCE,3);
        CHECK(n.ingress.Read(n.source,n.batch,error)==NativeQueueRead::Ready);
        CHECK(n.coordinator->Reconcile(n.ingress,n.source,*n.batch,n.owner,n.store,nativeResult,error));
        auto& b=facts.binding;b.outer=0x1000;b.sessionTransition=1;b.dispatchEpoch=Sys_EventDispositionEpoch();b.streamToken=Sys_EventQueueToken();
        b.window={0x2000,5,6,n.source.status.providerEpoch,8,9};b.editor=n.f.barrier.editor;b.native=n.f.nativeId;
        facts.inventory=&inventory;facts.route=&route;facts.source=&n.source;
        CHECK(route.Prepare(b,facts.id,error));CHECK(ledger.BeginPlanned(n.ingress,n.source,*n.batch,schedule,error));
        CHECK(inventory.Bind(route,facts.id,b,ledger,*n.batch,error));CHECK(transfers.Claim(error));
        if(prefix){QueueLegacy(SE_KEY,'b');SDL3_QueueMouseInput(M_DELTAY,8,0);SDL3_QueueKeyboardInput('b',true,0);}
        NativeDispositionRecord record;CHECK(ledger.OpenRecord(0,record,error));sysEvent_t e{SE_KEY,firstKey,firstDown?1:0,0,nullptr};
        if(withPayload){auto bytes=EncodeKeyEventMetadata({true,true,false,false});e.evPtr=payload=Allocate(bytes.size());e.evPtrLength=static_cast<int>(bytes.size());std::memcpy(payload,bytes.data(),bytes.size());}
        if(firstKey!=K_CTRL)CHECK(transfers.SessionKey(record,e,NativeInputSessionQueue::Platform,initial,error)==NativeInputTransferStatus::StoredAdmitted);
        CHECK(transfers.Keyboard(record,firstKey,firstDown,1,a,error)==NativeInputTransferStatus::StoredAdmitted);
        for(unsigned i=0;i<extraKeys;++i){NativeTranslatedKeyboard repeat;CHECK(transfers.Keyboard(record,'a',true,1,repeat,error)==NativeInputTransferStatus::StoredAdmitted);}
        CHECK(ledger.SealRecord(record,NativeRecordDisposition::Emitted,error));CHECK(ledger.OpenRecord(1,record,error));
        if(modifier)CHECK(transfers.Keyboard(record,K_CTRL,true,2,ctrl,error)==NativeInputTransferStatus::StoredAdmitted);
        CHECK(ledger.SealRecord(record,modifier?NativeRecordDisposition::Emitted:NativeRecordDisposition::Ignored,error));CHECK(ledger.OpenRecord(2,record,error));
        CHECK(transfers.Mouse(record,M_DELTAX,9,3,mouse,error)==NativeInputTransferStatus::StoredAdmitted);
        CHECK(ledger.SealRecord(record,NativeRecordDisposition::Emitted,error));
        for(std::size_t i=3;i<n.batch->Events().size();++i){CHECK(ledger.OpenRecord(i,record,error));CHECK(ledger.SealRecord(record,i==3?NativeRecordDisposition::Ignored:i==4?NativeRecordDisposition::Sentinel:NativeRecordDisposition::Fence,error));}
        CHECK(ledger.SealTranslation(error));
        driver=std::make_unique<NativeInputDriver>(route,facts.id,b,inventory,transfers,ledger,n.ingress,n.source,*n.batch,
            *n.coordinator,n.owner,n.store,n.fence,lifecycle,loop);
    }
    void Install(){CHECK(driver->Install(nativeResult.completion,error));CHECK(NativeInput_OwnsPump());}
    ~DriverFixture(){
        sessionObject.callback={};consoleObject.callback={};consoleObject.closeCallback={};commandObject.callback={};sessionObject.game=nullptr;facts.callback={};n.hooks.callback={};onFree={};
        if(driver){driver->Abort();for(unsigned i=0;i<4;++i){driver->BeginFrame(100000+i);driver->Checkpoint();}CHECK(driver->Uninstall());}
        Sys_ClearEvents();SDL3_ClearInputQueues();eventLoop=nullptr;
    }
};
static void NaturalSinks() {
    DriverFixture f;localUsercmdGen.inhibitCommands=4;f.Install();
    CHECK(localUsercmdGen.inhibitCommands==4);
    const auto polls=f.n.source.records.size();const auto commands=commandObject.executions,sim=localUsercmdGen.simulations;
    f.loop.RunEventLoop(false);CHECK(sessionDeliveries>=1 && f.n.fence.acks==0);
    localUsercmdGen.GetDirectUsercmd();
    CHECK(localUsercmdGen.simulations==sim+1 && localUsercmdGen.joysticks==1);
    CHECK(commandObject.executions==commands && f.n.source.records.size()==polls);
    CHECK(f.driver->Finished() && f.n.fence.acks==1 && f.lifecycle.detaches==1);
    CHECK(NativeInput_OwnsPump() && !f.driver->Uninstall());
    CHECK(localUsercmdGen.mouseDx==0 && localUsercmdGen.buttonState[1]==0 && localUsercmdGen.inhibitCommands==4);
    CHECK(localUsercmdGen.NativeKeyBlocked(K_CTRL) && localUsercmdGen.NativeKeyBlocked('a'));
    const auto held=localUsercmdGen.NativeKeyBlocked(K_CTRL);
    Usercmd_NativeInputSource(f.facts.id+1,6,2,SDL_SCANCODE_LCTRL,K_CTRL,false);
    CHECK(localUsercmdGen.NativeKeyBlocked(K_CTRL)==held);
    Usercmd_NativeInputSource(f.facts.id,6,2,SDL_SCANCODE_RCTRL,K_CTRL,false);
    CHECK(localUsercmdGen.NativeKeyBlocked(K_CTRL));
    Usercmd_NativeInputSource(f.facts.id,6,2,SDL_SCANCODE_LCTRL,K_CTRL,false);
    CHECK(!localUsercmdGen.NativeKeyBlocked(K_CTRL));
}
static void PollFirstAndPrefix() {
    DriverFixture f(NativeDispositionSchedule::AtMousePollEntry);f.Install();
    const auto delivered=sessionDeliveries;
    localUsercmdGen.GetDirectUsercmd();CHECK(sessionDeliveries==delivered && f.n.fence.acks==0);
    f.loop.RunEventLoop(false);CHECK(sessionDeliveries==delivered+1);
    const auto sim=localUsercmdGen.simulations;
    NativeInput_ContinueDeferred(f.loop);CHECK(sessionDeliveries==delivered+2 && f.driver->Finished());
    CHECK(localUsercmdGen.simulations==sim);
}
static void Replacement() {
    DriverFixture f;f.Install();sessionObject.callback=[&]{f.n.owner.replaced=true;};
    const auto sync=scrollSyncs,apps=applicationDispatches;
    f.loop.RunEventLoop(false);CHECK(f.driver->Faulted() && f.n.fence.acks==0);CHECK(scrollSyncs==sync && applicationDispatches==apps);
    f.driver->Checkpoint();CHECK(f.route.State()==NativeInputRoute::Phase::Empty);
    CHECK(!NativeInput_CompleteSession()); // duplicate cannot mint a new disposal
}

static void PrefixAndSuffix() {
    {DriverFixture f(NativeDispositionSchedule::BeforeSessionDrain,0,false,true);f.Install();
     const auto before=sessionDeliveries;f.loop.RunEventLoop(false);CHECK(sessionDeliveries==before+2);
     localUsercmdGen.GetDirectUsercmd();CHECK(f.driver->Finished() && f.n.fence.acks==1);CHECK(localUsercmdGen.NativeKeyBlocked('b'));}
    {DriverFixture f;f.Install();f.loop.RunEventLoop(false);QueueLegacy(SE_KEY,'z');
     const auto before=sessionDeliveries;localUsercmdGen.GetDirectUsercmd();f.loop.RunEventLoop(false);
     CHECK(sessionDeliveries==before && !f.driver->Finished() && f.n.fence.acks==0);
     f.driver->Abort();f.driver->Checkpoint();CHECK(f.route.State()!=NativeInputRoute::Phase::Empty);
     // The test owns this unrelated event. Production deliberately has no generic
     // fallback: after exact retirement its independent legacy disposition is
     // still required before the deferred original head can be cancelled.
     const auto unrelated=Sys_GetEvent();CHECK(unrelated.evType==SE_KEY && unrelated.evValue=='z');
     f.driver->Checkpoint();CHECK(f.route.State()==NativeInputRoute::Phase::Empty);}
    {DriverFixture f;f.Install();SDL3_QueueKeyboardInput('z',true,0);f.loop.RunEventLoop(false);localUsercmdGen.GetDirectUsercmd();
     CHECK(!f.driver->Finished() && f.n.fence.acks==0);f.driver->Abort();f.driver->Checkpoint();
     sysInputDispositionSlot_t slot;sysKeyboardInputDisposition_t value;
     CHECK(Sys_PeekKeyboardInputWithDisposition(s_keyboardDispositionState.slice,slot,value)==sysEventTransfer_t::Ready);
     CHECK(value.disposition.parent.Empty() && value.key=='z');
     CHECK(Sys_TakeKeyboardInputWithDisposition(slot,value)==sysEventTransfer_t::Ready);
     f.driver->Checkpoint();CHECK(f.route.State()==NativeInputRoute::Phase::Empty);}
}
static void ConsoleReplacement() {
 for(bool consumes:{false,true}){DriverFixture f;f.Install();consoleObject.consumes=consumes;
  consoleObject.callback=[&]{f.n.owner.replaced=true;};const auto before=sessionDeliveries,frames=retainedFrames;
  f.loop.RunEventLoop(false);CHECK(f.driver->Faulted() && sessionDeliveries==before && retainedFrames==frames && f.n.fence.acks==0);
  f.driver->Checkpoint();CHECK(f.route.State()==NativeInputRoute::Phase::Empty);}
}
static void BetweenSinkRetirement() {
 for(bool checkpointOnly:{false,true}){DriverFixture f;f.Install();f.loop.RunEventLoop(false);
  const auto delivered=sessionDeliveries;CHECK(f.route.Revoke(f.facts.id));
  if(checkpointOnly)f.driver->Checkpoint();else localUsercmdGen.GetDirectUsercmd();
  CHECK(f.driver->Faulted() && !f.driver->Finished() && !f.n.fence.acks && sessionDeliveries==delivered);
  f.driver->Checkpoint();CHECK(f.route.State()==NativeInputRoute::Phase::Empty);}
 // The original route is currently inside its Source observation. This exact
 // callback cannot make its temporarily unprovable binding an unmarked stop.
 {DriverFixture f;f.Install();f.facts.callback=[&]{sysEvent_t out{SE_CHAR,88,89,0,nullptr};
   CHECK(f.driver->TakeSession(out)==nativeInputSessionResult_t::Stop && out.evValue==88);};
  NativeInputSelection selected;CHECK(!f.route.Probe(selected));CHECK(f.driver->Faulted());
  f.driver->Checkpoint();CHECK(f.route.State()==NativeInputRoute::Phase::Empty && !f.n.fence.acks);}
 // Install publishes pump ownership before its last foreign validation. A late
 // failure retains that ownership until the actual backlog has been disposed.
 {DriverFixture f;f.n.hooks.callback=[&](const char* name){if(std::string(name)=="observe" && NativeInput_OwnsPump())f.n.owner.replaced=true;};
  CHECK(!f.driver->Install(f.nativeResult.completion,f.error));CHECK(f.driver->Faulted() && NativeInput_OwnsPump());
  f.n.hooks.callback={};f.driver->Checkpoint();CHECK(f.route.State()==NativeInputRoute::Phase::Empty && !f.n.fence.acks);}
}
static void MultiCallbackReplacement() {
 {DriverFixture f;void* text=Allocate(2);std::memcpy(text,"x",2);sysEvent_t prefix{SE_CONSOLE,0,0,2,text};f.loop.PushEvent(&prefix);f.Install();
  const auto buffers=commandObject.buffers;commandObject.callback=[&]{f.n.owner.replaced=true;};
  f.loop.RunEventLoop(false);CHECK(commandObject.buffers==buffers+1 && Freed(text));
  CHECK(f.driver->Faulted() && !f.n.fence.acks);f.driver->Checkpoint();CHECK(f.route.State()==NativeInputRoute::Phase::Empty);}
 for(int stage=0;stage<3;++stage){DriverFixture f;GameEndpoint game;game.reply=stage==2?ESC_MAIN:ESC_GUI;
  sessionObject.game=&game;sessionObject.guiActive=nullptr;
  sysEvent_t escape{SE_KEY,K_ESCAPE,1,0,nullptr};f.loop.PushEvent(&escape);f.Install();
  const auto setGui=sessionObject.setGuiCalls,menu=sessionObject.startMenuCalls;
  if(stage==0)consoleObject.closeCallback=[&]{f.n.owner.replaced=true;};else game.callback=[&]{f.n.owner.replaced=true;};
  f.loop.RunEventLoop(false);CHECK(game.calls==(stage?1u:0u));
  CHECK(sessionObject.setGuiCalls==setGui && sessionObject.startMenuCalls==menu && f.driver->Faulted() && !f.n.fence.acks);
  f.driver->Checkpoint();CHECK(f.route.State()==NativeInputRoute::Phase::Empty);}
}
static void PayloadAndReentry() {
 for(int mode=0;mode<3;++mode){DriverFixture f(NativeDispositionSchedule::BeforeSessionDrain,0,true);f.Install();
  const auto before=sessionDeliveries;
  if(mode==0)sessionObject.callback=[&]{throw std::runtime_error("partial Session effect");};
  if(mode==1)onFree=[&]{f.n.owner.replaced=true;};
  if(mode==2)onFree=[&]{sysEvent_t untouched{SE_CHAR,99,98,0,nullptr};CHECK(NativeInput_TakeSession(untouched)==nativeInputSessionResult_t::Stop);CHECK(untouched.evValue==99);};
  bool threw=false;try{f.loop.RunEventLoop(false);}catch(...){threw=true;}
  CHECK(threw==(mode==0) && sessionDeliveries==before+1 && Freed(f.payload));CHECK(f.driver->Faulted() && f.n.fence.acks==0);
  f.driver->Checkpoint();CHECK(f.route.State()==NativeInputRoute::Phase::Empty);CHECK(!NativeInput_CompleteSession());}
}
static void OwnershipFailures() {
 {DriverFixture f;auto bad=f.nativeResult.completion;++bad.serial;CHECK(!f.driver->Install(bad,f.error));CHECK(!NativeInput_OwnsPump());
  com_asyncInput.SetInteger(1);CHECK(!f.driver->Install(f.nativeResult.completion,f.error));com_asyncInput.SetInteger(0);f.Install();
  sysEvent_t out{SE_CHAR,42,43,0,nullptr};int key=123;bool down=true;
  std::thread worker([&]{CHECK(NativeInput_TakeSession(out)==nativeInputSessionResult_t::Stop);CHECK(!NativeInput_NextKeyboard(key,down));CHECK(!NativeInput_FatalRetire());CHECK(NativeInput_OwnsPump());});worker.join();
  CHECK(out.evValue==42 && key==123 && down && !f.driver->Faulted());}
 {DriverFixture f;f.Install();f.facts.callback=[&]{f.driver->Checkpoint();};sysEvent_t out{SE_CHAR,42,43,0,nullptr};
  CHECK(f.driver->TakeSession(out)==nativeInputSessionResult_t::Stop && out.evValue==42 && f.driver->Faulted());f.driver->Checkpoint();CHECK(f.route.State()==NativeInputRoute::Phase::Empty);}
 {DriverFixture f;f.Install();Sys_InvalidateEventQueue();const auto before=sessionDeliveries;
  f.loop.RunEventLoop(false);CHECK(f.driver->Faulted() && sessionDeliveries==before && !f.n.fence.acks);f.driver->Checkpoint();CHECK(f.route.State()==NativeInputRoute::Phase::Empty);}
}
static void AfterTakeAndWindow() {
 {DriverFixture f(NativeDispositionSchedule::BeforeSessionDrain,0,true);f.Install();const auto before=eventTail;
  f.n.hooks.callback=[&](const char* name){if(std::string(name)=="observe" && eventTail!=before)f.n.owner.replaced=true;};
  sysEvent_t out{SE_CHAR,66,67,0,nullptr};CHECK(f.driver->TakeSession(out)==nativeInputSessionResult_t::Stop);
  CHECK(out.evValue==66 && !out.evPtr && Freed(f.payload) && f.driver->Faulted() && f.n.fence.acks==0);
  f.n.hooks.callback={};f.driver->Checkpoint();CHECK(f.route.State()==NativeInputRoute::Phase::Empty);}
 {DriverFixture f(NativeDispositionSchedule::BeforeSessionDrain,0,false,false,6);f.Install();f.loop.RunEventLoop(false);localUsercmdGen.GetDirectUsercmd();
  CHECK(f.driver->Faulted() && f.n.fence.acks==0);f.driver->Checkpoint();CHECK(f.route.State()==NativeInputRoute::Phase::Empty);}
}
static void FenceFailures() {
 for(int mode=0;mode<3;++mode){DriverFixture f;f.Install();f.loop.RunEventLoop(false);
  if(mode==0)f.n.fence.okay=false;
  if(mode==1)f.n.hooks.callback=[&](const char* name){if(std::string(name)=="fenced")f.n.owner.replaced=true;};
  if(mode==2)f.n.hooks.callback=[&](const char* name){if(std::string(name)=="fence")f.driver->Checkpoint();};
  localUsercmdGen.GetDirectUsercmd();CHECK(f.driver->Faulted() && !f.driver->Finished() && f.n.fence.acks==1 && f.lifecycle.detaches==0);
  f.n.hooks.callback={};f.driver->Checkpoint();CHECK(f.route.State()==NativeInputRoute::Phase::Empty);}
}
static void BudgetAndPasses() {
 DriverFixture f(NativeDispositionSchedule::BeforeSessionDrain,509);f.Install();
 CHECK(NativeInput_BeginKeyboard());int key=99;bool down=true;CHECK(!NativeInput_NextKeyboard(key,down) && key==99);NativeInput_EndKeyboard();
 f.loop.RunEventLoop(false);const auto before=sessionDeliveries;localUsercmdGen.GetDirectUsercmd();
 CHECK(!f.driver->Finished() && f.n.fence.acks==0 && sessionDeliveries==before);
 NativeDispositionProgress progress;CHECK(f.ledger.QueryProgress(progress) && progress.pass==NativeDispositionPass::KeyboardPoll && progress.next.emission);
 const auto cursor=s_keyboardDispositionState.next;localUsercmdGen.GetDirectUsercmd();CHECK(s_keyboardDispositionState.next==cursor);
 f.driver->BeginFrame(0);localUsercmdGen.GetDirectUsercmd();CHECK(s_keyboardDispositionState.next==cursor);
 f.driver->BeginFrame(1);localUsercmdGen.GetDirectUsercmd();CHECK(f.driver->Finished() && f.n.fence.acks==1 && sessionDeliveries==before+1);
}

static void Turnover() {
 DriverFixture f;f.Install();f.loop.RunEventLoop(false);localUsercmdGen.GetDirectUsercmd();CHECK(f.driver->Finished());
 CHECK(!NativeInput_CanPrepareCollection());NativeInput_BeginFrame(1);CHECK(NativeInput_CanPrepareCollection());
 auto prior=std::move(f.driver);auto priorBatch=std::move(f.n.batch);
 CHECK(f.n.coordinator->QueryBarrier(f.n.f.barrier,f.error));++f.n.f.dispatch;++f.n.f.fence;f.n.store.Seal(2);
 f.n.source.status.pending=OQ4_NativeFence{1,0,f.n.f.dispatch,f.n.f.fence};
 f.n.source.Add(SDL_EVENT_CLIPBOARD_UPDATE);f.n.source.Add(SDL_EVENT_POLL_SENTINEL,OQ4_QUEUE_SENTINEL);f.n.source.Add(f.n.source.status.fenceEventType,OQ4_QUEUE_FENCE,0);
 CHECK(f.n.ingress.Read(f.n.source,f.n.batch,f.error)==NativeQueueRead::Ready);
 ui::NativeTextCollectionResult next;CHECK(f.n.coordinator->Reconcile(f.n.ingress,f.n.source,*f.n.batch,f.n.owner,f.n.store,next,f.error));
 NativeEventDispositionLedger ledger(f.facts);NativeInputEmissionInventory inventory;NativeInputTransfers transfers(inventory,f.route,f.loop);
 CHECK(ledger.BeginPlanned(f.n.ingress,f.n.source,*f.n.batch,f.error));CHECK(inventory.Bind(f.route,f.facts.id,f.facts.binding,ledger,*f.n.batch,f.error));CHECK(transfers.Claim(f.error));
 for(std::size_t i=0;i<f.n.batch->Events().size();++i){NativeDispositionRecord rec;CHECK(ledger.OpenRecord(i,rec,f.error));CHECK(ledger.SealRecord(rec,i==0?NativeRecordDisposition::Ignored:i==1?NativeRecordDisposition::Sentinel:NativeRecordDisposition::Fence,f.error));}
 CHECK(ledger.SealTranslation(f.error));f.facts.inventory=&inventory;f.facts.completed=false;
 NativeInputDriver nextDriver(f.route,f.facts.id,f.facts.binding,inventory,transfers,ledger,f.n.ingress,f.n.source,*f.n.batch,*f.n.coordinator,f.n.owner,f.n.store,f.n.fence,f.lifecycle,f.loop);
 auto wrong=next.completion;++wrong.serial;CHECK(!nextDriver.Install(wrong,f.error));
 const bool installed=nextDriver.Install(next.completion,f.error);if(!installed)std::fprintf(stderr,"turnover install refused: %s fault=%d frame=%llu previous=%d\n",f.error.c_str(),nextDriver.Faulted(),static_cast<unsigned long long>(nativeInputBudgetFrame),prior->Faulted());CHECK(installed && NativeInput_OwnsPump());CHECK(!prior->Uninstall());
 const auto sim=localUsercmdGen.simulations;f.loop.RunEventLoop(false);localUsercmdGen.GetDirectUsercmd();
 CHECK(nextDriver.Finished() && f.n.fence.acks==2 && localUsercmdGen.simulations==sim+1);
 CHECK(localUsercmdGen.NativeKeyBlocked('a'));nextDriver.Abort();nextDriver.Checkpoint();CHECK(nextDriver.Uninstall());
 prior.reset();f.facts.inventory=nullptr;
}

static void CancelledPhysicalRelease() {
 for(int mode=0;mode<5;++mode){DriverFixture f(NativeDispositionSchedule::BeforeSessionDrain,0,false,false,5,K_CTRL,SDL_SCANCODE_LCTRL,false,false);f.Install();
  auto& held=localUsercmdGen.nativeHeldSources[0];held={f.facts.id,6,2,SDL_SCANCODE_LCTRL,K_CTRL,true};idKeyInput::down[K_CTRL]=true;
  if(mode==1)localUsercmdGen.nativeHeldSources[1]={f.facts.id,6,2,SDL_SCANCODE_RCTRL,K_CTRL,true};
  if(mode==2)localUsercmdGen.nativeUnknownKeyBlocked[K_CTRL]=true;
  if(mode==3){++held.route;}
  if(mode==4){f.lifecycle.callback=[&]{f.driver->Checkpoint();};}
  f.driver->Abort();f.driver->Checkpoint();
  if(mode==4){CHECK(idKeyInput::IsDown(K_CTRL));f.driver->Checkpoint();}
  CHECK(idKeyInput::IsDown(K_CTRL)==(mode==1||mode==2||mode==3));CHECK(f.n.fence.acks==0);
  CHECK(f.route.State()==NativeInputRoute::Phase::Empty);
 }
 {DriverFixture f;f.Install();idKeyInput::down[K_CTRL]=true;localUsercmdGen.nativeHeldSources[0]={f.facts.id,6,2,SDL_SCANCODE_LCTRL,K_CTRL,true};
  Usercmd_NativeInputSource(f.facts.id,6,2,SDL_SCANCODE_LCTRL,K_CTRL,false);CHECK(idKeyInput::IsDown(K_CTRL));}
}
int main(){NaturalSinks();PollFirstAndPrefix();Replacement();PrefixAndSuffix();ConsoleReplacement();BetweenSinkRetirement();MultiCallbackReplacement();PayloadAndReentry();OwnershipFailures();AfterTakeAndWindow();FenceFailures();BudgetAndPasses();CancelledPhysicalRelease();Turnover();std::printf("Native input driver: %u checks\n",checks);}
