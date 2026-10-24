#!/usr/bin/env python3
"""Execute the production XR ownership methods against a hostile runtime.

The fake owns handles and image queues, not application lifecycle decisions.
No renderer, headset, input device or operating-system input is used.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from filesystem_case_segments import function_body
from pinned_archive_headers import pinned_include

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdarg>
constexpr int VR_NUM_EYES=2, VR_NUM_HANDS=2, VR_INPUT_CRITICAL_SECTION=2;
constexpr int VR_SYSTEM_RETRY_MSEC=3000;
constexpr uint32_t VR_MAX_SWAPCHAIN_IMAGES=64;
constexpr XrDuration VR_IMAGE_WAIT_NSEC=100000000;
template<class T> struct idList : std::vector<T> {
    int Num() const { return static_cast<int>(this->size()); }
    void Clear() { this->clear(); }
    void SetNum(int n) { if(n<0 || n>64) throw std::runtime_error("unbounded image count"); this->resize(n); }
    void Append(T value) { this->push_back(value); }
    T* Ptr() { return this->data(); }
};
struct idStr : std::string {
    void Clear() { clear(); }
    static void snPrintf(char* dst,size_t n,const char* fmt,...) {
        va_list args; va_start(args,fmt); vsnprintf(dst,n,fmt,args); va_end(args);
    }
};
struct Common {
    int warnings=0;
    void Warning(const char*,...) { if(++warnings>40) throw std::runtime_error("recursive teardown"); }
    void Printf(const char*,...) {}
} commonObject;
Common* common=&commonObject;
struct Renderer { int clears=0; void SetVRFrame(const void*) { ++clears; } } renderer;
Renderer* renderSystem=&renderer;
int pointerLeaves=0,keyReleases=0;
bool Sys_PostVRPointer(bool active,float,float) { assert(!active); ++pointerLeaves; return false; }
struct Cvar { int GetInteger() const { return 0; } } vr_debug;
void Sys_EnterCriticalSection(int) {}
void Sys_LeaveCriticalSection(int) {}
int Sys_Milliseconds() { return 500; }
struct Frame { bool active=false,stereo=false; };
template<class T> T handle(uintptr_t value) { return reinterpret_cast<T>(value); }
struct ImageQueue { bool acquired=false,waited=false; };
struct Runtime {
    std::map<XrSwapchain,ImageQueue> chains;
    bool session=true,instance=true;
    int acquireCalls=0,waitCalls=0,releaseCalls=0,destroySessions=0,destroyInstances=0;
    int failAcquireAt=0,failWaitAt=0,failReleaseAt=0,failEnumerateAt=0,enumerates=0;
    XrResult failure=XR_ERROR_SESSION_LOST;
    uint32_t count=3,secondCount=3,index=0;
    bool failCreate=false,zeroImage=false,persistentReleaseFailure=false;
    uintptr_t nextHandle=50;
    ImageQueue& Get(XrSwapchain chain) {
        auto found=chains.find(chain);
        if(!session || found==chains.end()) throw std::runtime_error("stale swapchain call");
        return found->second;
    }
} rt;
extern "C" {
XRAPI_ATTR XrResult XRAPI_CALL xrResultToString(XrInstance,XrResult,char out[XR_MAX_RESULT_STRING_SIZE]) {
    std::memcpy(out,"injected",9); return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrCreateSwapchain(XrSession,const XrSwapchainCreateInfo*,XrSwapchain* out) {
    assert(rt.session);
    if(rt.failCreate) return rt.failure;
    *out=handle<XrSwapchain>(rt.nextHandle++); rt.chains[*out]={}; return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateSwapchainImages(XrSwapchain chain,uint32_t capacity,uint32_t* count,XrSwapchainImageBaseHeader* out) {
    rt.Get(chain);
    if(++rt.enumerates==rt.failEnumerateAt) return rt.failure;
    *count=capacity ? rt.secondCount : rt.count;
    if(capacity) {
        auto images=reinterpret_cast<XrSwapchainImageOpenGLKHR*>(out);
        for(uint32_t i=0;i<capacity;++i) images[i].image=rt.zeroImage ? 0 : 100+i;
    }
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrDestroySwapchain(XrSwapchain chain) {
    rt.Get(chain); rt.chains.erase(chain); return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrAcquireSwapchainImage(XrSwapchain chain,const XrSwapchainImageAcquireInfo*,uint32_t* index) {
    auto& q=rt.Get(chain); assert(!q.acquired);
    if(++rt.acquireCalls==rt.failAcquireAt) return rt.failure;
    q.acquired=true; *index=rt.index; return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrWaitSwapchainImage(XrSwapchain chain,const XrSwapchainImageWaitInfo*) {
    auto& q=rt.Get(chain); assert(q.acquired && !q.waited);
    if(++rt.waitCalls==rt.failWaitAt) return rt.failure;
    q.waited=true; return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrReleaseSwapchainImage(XrSwapchain chain,const XrSwapchainImageReleaseInfo*) {
    auto& q=rt.Get(chain);
    if(!q.acquired || !q.waited) throw std::runtime_error("release without successful wait");
    ++rt.releaseCalls;
    if(rt.releaseCalls==rt.failReleaseAt || (rt.persistentReleaseFailure && rt.releaseCalls>=rt.failReleaseAt)) return rt.failure;
    q={}; return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrDestroySpace(XrSpace) { assert(rt.session); return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL xrEndSession(XrSession) { assert(rt.session); return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL xrDestroySession(XrSession) {
    assert(rt.session); ++rt.destroySessions; rt.session=false; rt.chains.clear(); return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrDestroyInstance(XrInstance) {
    assert(rt.instance && !rt.session); ++rt.destroyInstances; rt.instance=false; return XR_SUCCESS;
}
}
class idVRSystemOpenXR {
public:
    enum recovery_t { RECOVERY_NONE, RECOVERY_SESSION, RECOVERY_INSTANCE };
    recovery_t recovery=RECOVERY_NONE;
    struct swapchain_t {
        XrSwapchain handle=XR_NULL_HANDLE;
        int width=0,height=0;
        idList<uint32_t> images;
        uint32_t acquiredIndex=0;
        bool acquired=false;
    } eyeSwapchains[2],screenSwapchain;
    XrInstance instance=handle<XrInstance>(1);
    XrSession session=handle<XrSession>(2);
    XrSystemId systemId=3;
    XrSessionState sessionState=XR_SESSION_STATE_FOCUSED;
    XrSpace localSpace=XR_NULL_HANDLE,viewSpace=XR_NULL_HANDLE;
    XrPath handPaths[2]={};
    PFN_xrGetOpenGLGraphicsRequirementsKHR getGLRequirements=nullptr;
    bool frameOpen=true,frameShouldRender=true,rendererReady=true;
    bool sessionRunning=true,sessionFocused=true,viewsValid=true;
    bool inputValid=true,bodyYawValid=true,twoHanded=true,runtimeStopped=false;
    bool pointerOnScreen=false,physicalCrouch=false,roomScaleWalking=false,roomScaleStickIdle=true;
    bool aimLocalValid[2]={};
    int hands[2]={};
    float moveForward=0,moveRight=0;
    int heldKeys[16]={},screenResizeFailedWidth=0,screenResizeFailedHeight=0,nextSessionRetryTime=0;
    int64_t swapchainFormat=0;
    idStr interactionProfile;
    Frame frame;
    void DestroyActions() {}
    void UpdateKey(int slot,int,bool) { if(heldKeys[slot])++keyReleases; heldKeys[slot]=0; }
    void ScreenSizeForWindow(int& width,int& height) const { width=1000; height=600; }
    bool Check(XrResult,const char*);
    bool CheckFrame(XrResult,const char*);
    bool RecoverIfNeeded();
    void DestroySession(); void DestroyInstance();
    bool CreateSwapchain(swapchain_t&,int,int);
    void DestroySwapchain(swapchain_t&);
    bool ResizeScreenSwapchain(); bool AcquireImages(); void ReleaseImages();
    void Prepare() {
        int i=10;
        for(auto* c : {&screenSwapchain,&eyeSwapchains[0],&eyeSwapchains[1]}) {
            c->handle=handle<XrSwapchain>(i++); c->width=800;c->height=600;
            c->images.SetNum(3); for(int j=0;j<3;++j)c->images[j]=100+j;
            rt.chains[c->handle]={};
        }
    }
};
'''
MAIN = r'''
void run(const std::string& test) {
    if(test=="partial_acquire") {
        for(auto failure : {XR_ERROR_SESSION_LOST,XR_ERROR_INSTANCE_LOST,XR_ERROR_CALL_ORDER_INVALID,XR_SESSION_LOSS_PENDING}) {
            for(int position=1;position<=3;++position) {
                rt=Runtime{};
                idVRSystemOpenXR probe; probe.Prepare();
                rt.failAcquireAt=position; rt.failure=failure;
                assert(!probe.AcquireImages()); probe.RecoverIfNeeded();
                assert(rt.acquireCalls==position && rt.waitCalls==position-1 && !rt.session);
                probe.DestroyInstance(); probe.DestroyInstance();
                assert(rt.chains.empty() && rt.destroySessions==1 && rt.destroyInstances==1);
            }
        }
        return;
    }
    idVRSystemOpenXR app; app.Prepare();
    if(test=="normal") {
        for(int i=0;i<1000;++i) { assert(app.AcquireImages()); app.ReleaseImages(); }
        assert(rt.acquireCalls==3000 && rt.waitCalls==3000 && rt.releaseCalls==3000);
    } else if(test=="teardown") {
        assert(app.AcquireImages()); rt.failReleaseAt=1; rt.persistentReleaseFailure=true;
        app.DestroySession(); app.DestroySession();
        assert(rt.destroySessions==1 && rt.releaseCalls==0);
    } else if(test=="input_teardown") {
        app.pointerOnScreen=app.physicalCrouch=app.roomScaleWalking=true;
        app.moveForward=1; app.moveRight=-1; app.hands[0]=1; app.aimLocalValid[0]=true;
        app.heldKeys[0]=4;app.heldKeys[5]=7;
        app.DestroySession(); app.DestroySession();
        assert(!app.pointerOnScreen && !app.physicalCrouch && !app.roomScaleWalking);
        assert(app.moveForward==0 && app.moveRight==0 && app.hands[0]==0 && !app.aimLocalValid[0]);
        assert(pointerLeaves==1 && keyReleases==2);
    } else if(test=="wait_loss" || test=="wait_timeout" || test=="wait_error") {
        rt.failWaitAt=2;
        rt.failure=test=="wait_timeout" ? XR_TIMEOUT_EXPIRED : test=="wait_error" ? XR_ERROR_CALL_ORDER_INVALID : XR_ERROR_SESSION_LOST;
        assert(!app.AcquireImages()); app.RecoverIfNeeded();
        assert(rt.waitCalls==2 && rt.releaseCalls==0 && !rt.session);
    } else if(test=="release_loss" || test=="release_error") {
        assert(app.AcquireImages()); rt.failReleaseAt=1;
        if(test=="release_error") rt.failure=XR_ERROR_CALL_ORDER_INVALID;
        app.ReleaseImages(); app.RecoverIfNeeded();
        assert(rt.releaseCalls==1 && !rt.session);
    } else if(test=="index_large" || test=="index_end") {
        rt.index=test=="index_large" ? UINT32_MAX : 3;
        assert(!app.AcquireImages()); app.RecoverIfNeeded();
        assert(!rt.session);
    } else if(test=="resize_loss") {
        rt.failEnumerateAt=2;
        assert(!app.ResizeScreenSwapchain()); app.RecoverIfNeeded();
        assert(!rt.session && rt.chains.empty());
    } else if(test=="resize_failure") {
        rt.failCreate=true; rt.failure=XR_ERROR_LIMIT_REACHED;
        auto previous=app.screenSwapchain.handle;
        assert(app.ResizeScreenSwapchain()); app.RecoverIfNeeded();
        assert(rt.session && app.screenSwapchain.handle==previous && rt.chains.size()==3);
    } else if(test=="count_large" || test=="count_zero" || test=="count_changed" || test=="image_zero") {
        if(test=="count_large")rt.count=UINT32_MAX;
        if(test=="count_zero")rt.count=0;
        if(test=="count_changed")rt.secondCount=4;
        if(test=="image_zero")rt.zeroImage=true;
        idVRSystemOpenXR::swapchain_t local;
        assert(!app.CreateSwapchain(local,1000,600));
        assert(local.handle==XR_NULL_HANDLE && rt.chains.size()==3);
    } else if(test=="instance_loss" || test=="session_pending") {
        assert(app.AcquireImages());
        app.frame.active=app.frame.stereo=true;
        assert(!app.Check(test=="session_pending" ? XR_SESSION_LOSS_PENDING : XR_ERROR_INSTANCE_LOST,"fault"));
        // Reporting a failure must not destroy resources underneath its caller.
        assert(rt.session && rt.instance);
        assert(!app.frame.active && !app.frame.stereo && !app.inputValid);
        assert(app.RecoverIfNeeded());
        assert(!rt.session && (test=="session_pending" || (!rt.instance && app.runtimeStopped)));
        assert(!app.RecoverIfNeeded());
    } else throw std::runtime_error("unknown case");
    app.DestroyInstance(); app.DestroyInstance();
    assert(rt.chains.empty() && rt.destroySessions==1 && rt.destroyInstances==1);
    assert(!app.frameOpen && !app.inputValid && !app.sessionRunning);
}
int main(int argc,char** argv) {
    try {
        if(argc!=2) throw std::runtime_error("one case required");
        run(argv[1]); std::printf("XR lifecycle: %s passed\n",argv[1]); return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"XR lifecycle: %s\n",e.what()); return 1; }
}
'''
CASES = ("normal", "teardown", "input_teardown", "partial_acquire", "wait_loss", "wait_timeout", "wait_error",
         "release_loss", "release_error", "index_large", "index_end", "resize_loss", "resize_failure",
         "count_large", "count_zero", "count_changed", "image_zero", "instance_loss", "session_pending")


def source() -> str:
    code = (ROOT / "src/sys/openxr/OpenXRSystem.cpp").read_text(encoding="utf-8")
    methods = ["bool Check", "void DestroySession", "void DestroyInstance", "bool CreateSwapchain",
               "void DestroySwapchain", "bool ResizeScreenSwapchain", "bool AcquireImages", "void ReleaseImages"]
    result = SUPPORT
    for method in methods:
        kind, name = method.split()
        result += "\n" + function_body(code, f"{kind} idVRSystemOpenXR::{name}(")
    if "bool idVRSystemOpenXR::RecoverIfNeeded(" in code:
        result += "\n" + function_body(code, "bool idVRSystemOpenXR::RecoverIfNeeded(")
        result += "\n" + function_body(code, "bool idVRSystemOpenXR::CheckFrame(")
    else:
        result += "\nbool idVRSystemOpenXR::RecoverIfNeeded() { return false; }\n"
    return result + MAIN


def main() -> None:
    if len(sys.argv) == 3 and sys.argv[1] == "--generate-native":
        Path(sys.argv[2]).write_text(source(), encoding="utf-8")
        return
    compiler = next((p for name in ("clang++", "g++", "c++") if (p := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError("C++17 compiler required")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="xr-lifecycle-", dir=ROOT / ".tmp") as directory:
        # The script smoke job runs before Meson provisions OpenXR.
        headers = pinned_include("openxr", "openxr", Path(directory))
        cpp, executable = Path(directory) / "test.cpp", Path(directory) / "test.exe"
        cpp.write_text(source(), encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-I", str(headers), str(cpp), "-o", str(executable)], check=True)
        for case in sys.argv[1:] or CASES:
            subprocess.run([str(executable), case], check=True)


if __name__ == "__main__":
    main()
