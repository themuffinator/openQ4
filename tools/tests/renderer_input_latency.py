#!/usr/bin/env python3
"""Execute the real swap receipt and latency collector with deterministic clocks."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#include <cassert>
#include "DisplayPresentation.h"
template<class T> T Min(T a,T b){return a<b?a:b;}
static double now=100,frequency=1000;
double Common_GetPresentationClockNow(){return now;}
double Common_GetPresentationClockUnitsPerSecond(){return frequency;}
struct Sys { double GetClockTicks(){return now;} } sysObject,*sys=&sysObject;
struct { double frameLookSampleClock=0,frameFlashPressClock=0,frameSwapClock=0; } tr;
static int mode=0;
void GLimp_SwapBuffers() {
    now+=4;
    switch(mode){
        case 0: R_DisplayPresentationPresented();break;
        case 1: R_DisplayPresentationFailed(RDP_PRESENT_FAILED);break;
        case 2: break; // hidden frame/readback, no display present
        case 3: R_DisplayPresentationBeginDevice();R_DisplayPresentationReady();R_DisplayPresentationPresented();break;
        case 4: R_DisplayPresentationShutdown();break;
    }
}
'''

CASES = r'''
int main(){
    R_DisplayPresentationBeginDevice();R_DisplayPresentationReady();
    tr.frameLookSampleClock=98;tr.frameFlashPressClock=97;
    R_SwapBuffersWithInputTiming();assert(tr.frameSwapClock==104);
    now=180; // delayed main-thread retirement must not add time to the receipt
    Common_RecordFrameInputToSwap(tr.frameLookSampleClock,tr.frameFlashPressClock,tr.frameSwapClock);
    assert(commonLookSampleToSwap.count==1 && commonLookSampleToSwap.msec[0]==6);
    assert(commonFlashPressToSwap.count==1 && commonFlashPressToSwap.msec[0]==7);
    for(mode=1;mode<=4;++mode){
        R_DisplayPresentationBeginDevice();R_DisplayPresentationReady();
        tr.frameSwapClock=0;
        R_SwapBuffersWithInputTiming();assert(tr.frameSwapClock==0);
        Common_RecordFrameInputToSwap(tr.frameLookSampleClock,tr.frameFlashPressClock,tr.frameSwapClock);
        assert(commonLookSampleToSwap.count==1 && commonFlashPressToSwap.count==1);
    }
    // A reset excludes receipts for frames handed off before it.
    openQ4_ResetPresentationLatencyStats();
    Common_RecordFrameInputToSwap(98,97,104);
    assert(commonLookSampleToSwap.count==0 && commonFlashPressToSwap.count==0);
    Common_RecordFrameInputToSwap(now,now+1,now+5);
    assert(commonLookSampleToSwap.count==1 && commonLookSampleToSwap.msec[0]==5);
    assert(commonFlashPressToSwap.count==1 && commonFlashPressToSwap.msec[0]==4);
    Common_RecordFrameInputToSwap(0,0,now+5); // no input
    Common_RecordFrameInputToSwap(now,now,now-1); // clock discontinuity
    Common_RecordFrameInputToSwap(now,now,now+251); // loading sample
    assert(commonLookSampleToSwap.count==1 && commonFlashPressToSwap.count==1);
    frequency=0;Common_RecordFrameInputToSwap(now,now,now+5);
    assert(commonLookSampleToSwap.count==1);
}
'''


def main():
    backend = (ROOT / "src/renderer/RenderSystem.cpp").read_text(encoding="utf-8")
    common = (ROOT / "src/framework/Common.cpp").read_text(encoding="utf-8")
    for path in ("src/renderer/tr_backend.cpp", "src/renderer/Vulkan/vk_Backend.cpp"):
        executor = (ROOT / path).read_text(encoding="utf-8")
        assert "R_SwapBuffersWithInputTiming();" in executor, path
        assert "GLimp_SwapBuffers();" not in executor, path
    source = SUPPORT + function_body(backend, "void R_SwapBuffersWithInputTiming(")
    source += common[common.index("static const int COMMON_INPUT_TO_SWAP_HISTORY"):common.index("static idStr Common_InputToSwapPercentiles")]
    source += function_body(common, "void openQ4_ResetPresentationLatencyStats(")
    source += CASES
    compiler = next((p for name in ("clang++", "g++", "c++") if (p := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError("a C++ compiler is required")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="input-latency-", dir=ROOT / ".tmp") as directory:
        temp = Path(directory)
        cpp = temp / "latency.cpp"
        cpp.write_text(source, encoding="utf-8")
        exe = temp / ("latency.exe" if os.name == "nt" else "latency")
        subprocess.run([compiler, "-std=c++20", "-I", str(ROOT / "src/renderer"), str(cpp),
                        str(ROOT / "src/renderer/DisplayPresentation.cpp"), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=30)
    print("Input latency: successful swap, delayed retirement, failed/hidden frames, restart and reset passed")


if __name__ == "__main__":
    main()
