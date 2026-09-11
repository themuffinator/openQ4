// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// Included after the actual recovery owner and full renderer Shutdown body.
static constexpr char durableAttempt[]="0123456789abcdef0123456789abcdef";
static renderImageRecoveryLease_t lease{};
static char error[256];
static void Seed(){
    traceDisposal=false;Reset();preparedRecovery.reset();sourceRefused=false;
    cpuReads=cpuTakes=0;lease={};error[0]=0;shutdownCalls=0;shutdownHook={};
    cpuDisposals=0;disposalOutsideScope=false;glConfig.isInitialized=true;
    initializations=deviceInitializations=0;
    TEST(!fullOwnerShutdown);TEST(R_ImagePolicyBindRendererThread());
}
static bool Prepare(){const auto target=ReadPolicy();return R_PrepareImagePolicyRecovery(7,9,durableAttempt,&target,&lease,error,sizeof(error));}
static void FailedTarget(){
    TEST(Prepare());request.recovery=lease;request.recoveryDirection=2;
    uploadFailure=true;renderImagePolicyResult_t result=sentinel;
    TEST(!R_TryImagePolicyRestart(&request,&result,error,sizeof(error)));
    TEST(preparedRecovery&&recovery);uploadFailure=false;
}
int main(){try{
    std::memset(&sentinel,0x6b,sizeof(sentinel));
    // Whole-owner disposal survives stale resource identity and failed target;
    // it releases both CPU directions and the original census together.
    for(bool failed:{false,true}){
        Seed();if(failed)FailedTarget();else TEST(Prepare());
        const auto old=lease;const auto serial=nextPreparation,attemptSerial=nextAttempt;
        const auto identity=resourceIdentityCounter.load();
        std::weak_ptr<const Baseline> census=preparedRecovery->original;
        TEST(!census.expired());
        R_ImagePolicyLifecycleChanged();traceDisposal=true;
        shutdownHook=[&](const char*){TEST(preparedRecovery);TEST(fullOwnerShutdown);};
        engine.Shutdown();shutdownHook={};
        TEST(shutdownCalls==16);TEST(!preparedRecovery&&!recovery&&!recoveryInvalidated&&!fullOwnerShutdown);
        TEST(cpuDisposals==2&&!disposalOutsideScope&&census.expired());
        TEST(nextPreparation==serial&&nextAttempt==attemptSerial&&resourceIdentityCounter.load()==identity);
        TEST(!R_CancelPreparedImagePolicyRecovery(&old,error,sizeof(error)));
        traceDisposal=false;Reset();TEST(Prepare());TEST(lease.preparation>old.preparation);
        TEST(!R_CancelPreparedImagePolicyRecovery(&old,error,sizeof(error)));
    }
    // Empty/mutated inventories and ordinary device restarts are not disposal.
    Seed();FailedTarget();const auto* retained=preparedRecovery.get();
    imageManager.images.clear();R_ImagePolicyLifecycleChanged();
    TEST(preparedRecovery.get()==retained&&recovery);
    TEST(!R_CancelPreparedImagePolicyRecovery(&lease,error,sizeof(error)));
    Seed();FailedTarget();retained=preparedRecovery.get();
    request.recoveryDirection=1;renderImagePolicyResult_t restored{};
    TEST(R_TryImagePolicyRestart(&request,&restored,error,sizeof(error)));
    TEST(preparedRecovery.get()==retained&&cpuTakes==2&&!fullOwnerShutdown);

    // The admission check runs before the first cleanup callback, including
    // attempted shutdown during preparation and a borrowed loader attempt.
    for(const char* boundary:{"read","upload"}){
        Seed();bool saw=false;
        if(!std::strcmp(boundary,"upload"))TEST(Prepare());
        callback=[&](const char* at){if(saw||std::strcmp(at,boundary))return;saw=true;
            engine.Shutdown();TEST(shutdownCalls==0&&!fullOwnerShutdown);};
        if(!std::strcmp(boundary,"read")){TEST(!Prepare());TEST(!preparedRecovery);}
        else{request.recovery=lease;request.recoveryDirection=2;TEST(!R_TryImagePolicyRestart(&request,&restored,error,sizeof(error)));TEST(preparedRecovery);}
        callback={};TEST(saw);engine.Shutdown();TEST(!preparedRecovery&&!recovery&&!fullOwnerShutdown);
    }
    Seed();FailedTarget();std::thread foreign([]{engine.Shutdown();engine.Init();char diagnostic[128];TEST(!R_InitRendererDevice(false,false,diagnostic,sizeof(diagnostic)));});foreign.join();
    TEST(!shutdownCalls&&preparedRecovery&&!fullOwnerShutdown);
    TEST(!initializations&&!deviceInitializations);

    // Every foreign cleanup callback sees sealed recovery/initialization entry
    // points. Nested full shutdown cannot manufacture completion.
    Seed();TEST(Prepare());const auto original=lease;
    char raw[8]={'s','e','n','t','i','n','e','l'};uint32_t bytes=99;
    shutdownHook=[&](const char*){
        const int before=shutdownCalls;engine.Shutdown();TEST(shutdownCalls==before&&preparedRecovery);
        TEST(!R_ImagePolicyBindRendererThread());TEST(!R_ImagePolicyOperationAllowed());
        engine.Init();TEST(!initializations);
        TEST(!R_InitRendererDevice(false,false,error,sizeof(error)));TEST(!deviceInitializations);
        TEST(!Prepare());TEST(!std::memcmp(&lease,&original,sizeof(lease)));
        TEST(!R_CaptureImagePolicyRecovery(&lease,1,raw,sizeof(raw),&bytes,error,sizeof(error)));
        TEST(bytes==99&&!std::memcmp(raw,"sentinel",8));
        TEST(!R_CancelPreparedImagePolicyRecovery(&lease,error,sizeof(error)));
        TEST(!R_ReleaseCompletedImagePolicyRecovery(&lease,2,&restored,error,sizeof(error)));
        TEST(!R_TryImagePolicyRestart(&request,&restored,error,sizeof(error)));
        bool wrongAllowed=true;std::thread wrong([&]{wrongAllowed=R_ImagePolicyContentMutation();});wrong.join();TEST(!wrongAllowed);
        TEST(R_ImagePolicyContentMutation());
    };
    engine.Shutdown();shutdownHook={};TEST(!preparedRecovery&&!recoveryInvalidated);

    // Replacing the manager or adding an image after its cleanup prevents
    // completion. Emptiness supplements the private full-shutdown proof; it is
    // never accepted on its own.
    for(bool replacement:{false,true}){
        Seed();FailedTarget();Images other;retained=preparedRecovery.get();
        shutdownHook=[&](const char* at){if(std::strcmp(at,"device"))return;
            if(replacement)globalImages=&other;else imageManager.images.push_back(&extra);};
        engine.Shutdown();shutdownHook={};globalImages=&imageManager;
        TEST(preparedRecovery.get()==retained&&recovery&&recoveryInvalidated&&!fullOwnerShutdown);
        engine.Shutdown();TEST(!preparedRecovery&&!recovery&&!recoveryInvalidated);
    }

    // Unwind after each counted cleanup boundary retains both directions; retry
    // only a complete actual Shutdown, with allocation disabled throughout.
    for(int fail=1;fail<=16;++fail){
        Seed();FailedTarget();retained=preparedRecovery.get();traceDisposal=true;
        shutdownHook=[&](const char*){if(shutdownCalls==fail)throw 17;};
        bool threw=false;try{engine.Shutdown();}catch(int v){TEST(v==17);threw=true;}
        TEST(threw&&preparedRecovery.get()==retained&&recovery&&recoveryInvalidated&&!fullOwnerShutdown&&cpuDisposals==0);
        shutdownHook={};allocationBudget=0;
        try{engine.Shutdown();}catch(...){allocationBudget=-1;throw;}
        allocationBudget=-1;
        TEST(!preparedRecovery&&!recovery&&!recoveryInvalidated&&!fullOwnerShutdown&&cpuDisposals==2&&!disposalOutsideScope);
    }
    Seed();rendererThread={};engine.Shutdown();TEST(R_ImagePolicyRendererThread()&&!fullOwnerShutdown);
    std::printf("PASS %d full image owner shutdown checks\n",checks);return 0;
}catch(const std::exception& e){allocationBudget=-1;std::fprintf(stderr,"FAIL %d: %s (%s)\n",checks,e.what(),error);return 1;}}
