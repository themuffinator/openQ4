// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/framework/PerformancePreset.h"
#include "src/framework/CVarAssignmentCapture.h"
#include "src/ui/application/SettingsTransaction.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <new>
#include <set>
#include <stdexcept>
#if defined(_WIN32) && defined(_DEBUG)
#include <crtdbg.h>
#endif
using namespace openq4;
using namespace openq4::ui;
// One-shot allocation failures exercise every actual staged allocation without
// making error reporting itself impossible. Never changes FP/device state.
static long failAllocation=-1;
void* operator new(std::size_t size) {
 if(failAllocation==0){failAllocation=-1;throw std::bad_alloc();}
 if(failAllocation>0)--failAllocation;
 if(void* p=std::malloc(size?size:1))return p;
 throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
static int checks=0;
static void Check(bool value,const char* message){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
static void AssignmentCapture(){
 const char* names[]={"com_performancePreset","r_multiSamples"};bool flags[]={true,true},nested[]={true,true};
 CVarAssignmentCapture::Record(names[0]);
 CVarAssignmentCapture outer;outer.Begin(names,2,flags);
 Check(!flags[0]&&!flags[1],"capture initializes only its bounded output");
 CVarAssignmentCapture::Record("r_multiSample");CVarAssignmentCapture::Record("");
 Check(!flags[0]&&!flags[1],"capture rejects partial/empty names");
 {
  CVarAssignmentCapture inner;inner.Begin(names,2,nested);
  CVarAssignmentCapture::Record("R_MULTISAMPLES");
  Check(flags[1]&&nested[1]&&!flags[0]&&!nested[0],"nested case-insensitive captures include equal-value assignments");
  outer.End();CVarAssignmentCapture::Record(names[0]);
  Check(!flags[0]&&nested[0],"early outer end preserves the inner chain");
 }
 flags[0]=false;CVarAssignmentCapture::Record(names[0]);Check(!flags[0],"scope exit detaches capture");
 outer.Begin(names,2,flags);outer.Begin(nullptr,2,flags);CVarAssignmentCapture::Record(names[0]);
 Check(!flags[0]&&!flags[1],"invalid reuse detaches old storage");
}
static void Profiles(){
 const std::vector<std::string> keys={"com_machineSpec","r_rendererBenchmarkPreset","r_multiSamples","r_postAA","com_maxfps","image_anisotropy","image_usePrecompressedTextures","image_downSize","image_downSizeLimit","image_downSizeSpecular","image_downSizeBump","image_downSizeSpecularLimit","image_downSizeBumpLimit","image_ignoreHighQuality","image_writeGeneratedImages","s_maxSoundsPerShader","r_useShadowMap","r_shadowMapSize","r_shadowMapMaxUpdatesPerView","r_bloom","r_ssao","r_hdrToneMap","r_motionBlur","r_crt","r_useLightGrid","r_rendererUploadMegs","r_rendererUploadFrameBuffers","s_numberOfSpeakers","s_useEAXReverb","s_maxEmitterChannels","r_shadowMapPointSize","r_shadowMapFilterTaps","r_shadowMapPointFilterTaps","r_shadowMapFilterMode","r_shadowMapPointFilterMode","r_shadowMapAtlasSize","r_ssaoSamples","r_bloomMipCount","r_motionBlurSamples","r_shadowMapPointLights","r_hdrSceneTarget","r_shadows","r_skipBump","r_skipSpecular","r_skipSky","image_picmip","r_shadowMapCSM","r_shadowMapCascadeCount","r_shadowMapProjectedCSM","r_shadowMapCascadeStabilize","r_motionBlurObjectVectors","r_shadowMapHashedAlpha","r_shadowMapTranslucentReceivers","r_depthOfField","com_performancePreset"};
 // Independent golden setter values for the revised quality progression.
 // Maximum asks for 8 samples, the most the Vulkan renderer draws (FPS-05).
 const std::vector<std::vector<int>> numbers={
 {0,0,0,30,1,1,1,512,1,1,64,256,1,1,1,0,512,1,0,0,0,0,0,0,8,3,0,1,24,512,1,1,0,0,2048,4,1,4,1,0,0,0,0,0,0,0,4,1,1,1,1,1,0},
 {0,0,0,30,1,1,1,1024,1,1,64,256,1,1,1,0,512,1,0,0,0,0,0,0,8,3,0,1,32,512,1,1,0,0,2048,4,1,4,1,0,0,0,0,0,0,0,4,1,1,1,1,1,0},
 {0,0,0,60,2,1,1,1024,1,1,64,256,1,1,1,0,512,1,0,0,0,0,0,0,8,3,0,1,32,512,1,1,0,0,2048,4,1,4,1,0,0,0,0,0,0,0,4,1,1,1,1,1,0},
 {1,0,1,60,2,1,0,0,0,0,64,0,0,1,0,0,1024,2,0,0,0,0,0,1,16,4,0,1,40,512,5,5,0,0,2048,8,3,8,1,0,1,0,0,0,0,0,4,1,1,1,1,1,0},
 {2,2,1,120,4,1,0,0,0,0,64,0,0,1,0,0,1024,0,0,0,0,0,0,1,16,4,0,1,48,512,5,5,0,0,2048,8,3,8,1,0,1,0,0,0,0,0,4,1,1,1,1,1,0},
 {2,0,2,144,16,1,0,0,0,0,64,0,0,1,0,1,1024,0,1,1,1,0,0,1,16,4,0,1,48,512,5,5,0,0,4096,12,5,8,0,1,1,0,0,0,0,0,4,1,1,1,1,1,1},
 {3,2,2,144,16,1,0,0,0,0,64,0,0,1,0,1,2048,0,1,1,1,0,0,1,32,4,0,1,48,512,9,9,2,0,4096,20,5,8,1,1,1,0,0,0,0,1,4,0,1,1,1,1,1},
 {3,4,3,240,16,1,0,0,0,0,64,0,0,1,0,1,4096,0,1,1,1,0,0,1,32,4,0,1,48,512,13,9,2,0,8192,32,5,8,1,1,1,0,0,0,0,1,4,0,1,1,1,1,1},
 {3,8,3,240,16,1,0,0,0,0,64,0,0,1,0,1,4096,0,1,1,1,1,0,1,32,4,0,1,48,1024,13,13,2,1,16384,32,5,16,1,1,1,0,0,0,0,1,4,1,1,1,1,1,1}};
 const char* names[]={"minimum","lowpower","low","performance","balanced","enhanced","quality","ultra","maximum"};
 const char* renderer[]={"low","low","low","baseline","baseline","modern","modern","high-end","high-end"};
 Check(PerformancePresetCount==9&&numbers.size()==9,"nine profiles");
 std::set<std::string> targets;for(const auto* key:PerformancePresetTargets())targets.insert(key);
 Check(targets.size()==55,"exact unique target count");
 // Presets never own the render scale or the display resolution.
 for(const char* key:{"r_screenFraction","r_mode","r_customWidth","r_customHeight","r_fullscreen","r_fullscreenDesktop","r_windowWidth","r_windowHeight","r_rendererDynamicResolution","r_dynamicResolutionMinScale"})
  Check(!targets.contains(key),"no preset target changes resolution");
 for(int n=0;n<PerformancePresetCount;++n){
  Check(FindPerformancePreset(names[n])==&PerformancePresets()[n],"canonical lookup");
  PerformancePresetAssignments out;std::string error;
  Check(ExpandPerformancePreset(PerformancePresets()[n],out,error),"expand actual profile");
  std::set<std::string> seen;size_t index=0;
  for(size_t i=0;i<out.size();++i){
   Check(out[i].key==keys[i],"exact setter order");seen.insert(out[i].key);
   if(i==1)Check(std::get<std::string>(out[i].value)==renderer[n],"benchmark classification");
   else if(i==54)Check(std::get<std::string>(out[i].value)==names[n],"marker last");
   else Check(std::get<int>(out[i].value)==numbers[n][index++],"golden profile value");
  }Check(seen==targets,"all and only declared targets");
 }
 Check(FindPerformancePreset("PeRfOrMaNcE")==FindPerformancePreset("performance"),"legacy case folding");
 for(const auto& name:std::vector<std::string>{"","missing","balanced ",std::string("quality\0evil",12),std::string(10000,'x')})Check(!FindPerformancePreset(name),"unknown name rejected");
 PerformancePresetAssignments output;std::string error;Check(ExpandPerformancePreset(PerformancePresets()[0],output,error),"sentinel");
 auto saved=output;auto invalid=PerformancePresets()[1];invalid.name=nullptr;
 Check(!ExpandPerformancePreset(invalid,output,error),"invalid source name");
 for(size_t i=0;i<output.size();++i)Check(output[i].key==saved[i].key && output[i].value==saved[i].value,"failed expansion unchanged");
 auto normalization=*FindPerformancePreset("performance");normalization.anisotropy=17;
 Check(ExpandPerformancePreset(normalization,output,error)&&std::get<int>(output[5].value)==17,"legacy normalization probe reaches setter unchanged");
}
static void Detection(){
 using GPU=RendererGpuClass;
 PerformancePresetSignals desktop;
 desktop.systemRamMB=32768;desktop.videoRamMB=8192;desktop.gpuClass=GPU::Discrete;
 desktop.modernEffects=true;desktop.logicalCpus=8;desktop.drawableWidth=1920;desktop.drawableHeight=1080;
 auto expect=[](const PerformancePresetSignals& s,const char* name){
  Check(std::string(DetectPerformancePreset(s).preset->name)==name,"hardware scenario recommendation");
 };
 expect({},"performance");expect(desktop,"enhanced");
 auto s=desktop;s.drawableWidth=2560;s.drawableHeight=1440;expect(s,"enhanced");
 s.drawableWidth=3840;s.drawableHeight=2160;expect(s,"balanced");
 s.drawableWidth=7680;s.drawableHeight=4320;expect(s,"performance");
 s=desktop;s.logicalCpus=2;expect(s,"performance");s.logicalCpus=0;expect(s,"performance");
 s=desktop;s.gpuClass=GPU::Integrated;s.videoRamMB=128;expect(s,"balanced");
 s.arm64=true;s.appleGpu=true;expect(s,"balanced"); // Apple Silicon is not penalized for ISA/UMA.
 s.linuxHost=true;expect(s,"balanced"); // Nor is it on Asahi Linux.
 s.appleGpu=false;s.linuxHost=false;expect(s,"balanced"); // Windows on ARM (Snapdragon X) keeps the iGPU rule.
 s.linuxHost=true;s.appleGpu=true;
 s.modernEffects=false;expect(s,"balanced"); // Apple GL 2.1 still runs classic quality.
 s.drawableWidth=1920;s.drawableHeight=1200;expect(s,"balanced"); // A 16:10 panel is the 1080p class.
 s.drawableWidth=2256;s.drawableHeight=1504;expect(s,"performance");
 s.drawableWidth=2560;s.drawableHeight=1600;expect(s,"performance");
 // ARM boards: any arm64 desktop-Linux host without a discrete or Apple GPU
 // gets the handheld ceiling, whatever its memory. RK3588/Mali, Jetson, a
 // Snapdragon laptop on Linux and an unrecognized driver all land here.
 {
  PerformancePresetSignals board;board.arm64=board.linuxHost=true;board.gpuClass=GPU::Integrated;
  board.systemRamMB=16384;board.modernEffects=true;board.logicalCpus=8;board.drawableWidth=1920;board.drawableHeight=1080;
  Check(PerformancePresetIsArmBoard(board),"an arm64 Linux iGPU is an ARM board");
  const auto found=DetectPerformancePreset(board);
  Check(std::string(found.preset->name)=="performance"&&found.reason==PerformancePresetReason::ArmBoard,"a 16 GiB Rockchip board is Performance, not Balanced");
  for(int ram:{8192,32768})for(auto gpu:{GPU::Integrated,GPU::Unknown}){auto b=board;b.systemRamMB=ram;b.gpuClass=gpu;expect(b,"performance");}
  auto b=board;b.systemRamMB=7900;expect(b,"performance"); // An 8 GiB board reports a little less.
  b.systemRamMB=4096;expect(b,"low");b.systemRamMB=2048;expect(b,"minimum"); // Smaller memory budgets still win.
  b=board;b.raspberryPi=true;Check(DetectPerformancePreset(b).reason==PerformancePresetReason::RaspberryPi,"a Pi is named by its own, stricter hint");expect(b,"lowpower");
  b=board;b.explicitLowPower=true;expect(b,"lowpower");
  b=board;b.gpuClass=GPU::Software;expect(b,"minimum");
  b=board;b.gpuClass=GPU::Discrete;b.videoRamMB=8192;Check(!PerformancePresetIsArmBoard(b),"a discrete card is not a board GPU");expect(b,"enhanced"); // An Ampere workstation.
  b=board;b.appleGpu=true;Check(!PerformancePresetIsArmBoard(b),"Asahi Linux is Apple Silicon");expect(b,"balanced");
  b=board;b.linuxHost=false;Check(!PerformancePresetIsArmBoard(b),"macOS and Windows on ARM are not boards");expect(b,"balanced");
  b=board;b.arm64=false;Check(!PerformancePresetIsArmBoard(b),"x86 Linux iGPUs are not boards");expect(b,"balanced");
  b=board;b.mobile=true;Check(!PerformancePresetIsArmBoard(b)&&DetectPerformancePreset(b).reason==PerformancePresetReason::Mobile,"Android keeps its phone rules");
 }
 s=desktop;s.gpuClass=GPU::Unknown;expect(s,"performance");
 s.gpuClass=GPU::Virtual;expect(s,"performance");
 s.gpuClass=GPU::Software;s.steamDeck=true;expect(s,"minimum");
 s=desktop;s.legacyRenderer=true;s.explicitLowPower=true;expect(s,"minimum");
 s=desktop;s.explicitLowPower=true;expect(s,"lowpower");
 s=desktop;s.raspberryPi=true;expect(s,"lowpower");s.systemRamMB=4096;expect(s,"lowpower");s.systemRamMB=2048;expect(s,"minimum");
 s=desktop;s.steamDeck=true;expect(s,"performance");
 s.videoRamMB=512;expect(s,"minimum");s.explicitLowPower=true;expect(s,"minimum");
 s=desktop;s.videoRamMB=512;s.raspberryPi=true;expect(s,"minimum");
 // The documented minimum PC keeps its stencil shadows; a phone that small does not.
 s=desktop;s.systemRamMB=4096;expect(s,"low");s.steamDeck=true;expect(s,"low");
 s=desktop;s.videoRamMB=1024;expect(s,"low");s.explicitLowPower=true;expect(s,"lowpower");
 s=desktop;s.mobile=true;expect(s,"performance");
 s.gpuClass=GPU::Integrated;expect(s,"performance");
 s.systemRamMB=4096;expect(s,"lowpower");s.systemRamMB=2048;expect(s,"minimum");
 s=desktop;s.modernEffects=false;expect(s,"balanced");
 for(const auto& [ram,name]:std::vector<std::pair<int,const char*>>{{0,"performance"},{2048,"minimum"},{2049,"low"},{4096,"low"},{4097,"performance"},{8191,"performance"},{8192,"balanced"},{15359,"balanced"},{15360,"enhanced"},{16383,"enhanced"},{16384,"enhanced"},{4194305,"performance"}}){s=desktop;s.systemRamMB=ram;expect(s,name);}
 for(const auto& [vram,name]:std::vector<std::pair<int,const char*>>{{-1,"performance"},{0,"performance"},{1,"minimum"},{512,"minimum"},{513,"low"},{1024,"low"},{1025,"performance"},{2048,"performance"},{2049,"balanced"},{7679,"balanced"},{7680,"enhanced"},{8188,"enhanced"},{8191,"enhanced"},{8192,"enhanced"},{11519,"enhanced"},{11520,"quality"},{12288,"quality"},{15359,"quality"},{15360,"ultra"},{16384,"ultra"},{32768,"ultra"},{262144,"ultra"},{262145,"performance"}}){s=desktop;s.videoRamMB=vram;expect(s,name);}
 // High-end cards: each capacity class takes the tier above, and each
 // resolution class above 1440p costs one. 24 GiB carries Ultra through 4K.
 struct Size{int width,height;};
 const Size p1080{1920,1080},p1440{2560,1440},wide{3440,1440},p2160{3840,2160},p2880{5120,2880},p4320{7680,4320};
 const auto at=[&](int vram,Size size,const char* name){auto h=desktop;h.videoRamMB=vram;h.drawableWidth=size.width;h.drawableHeight=size.height;expect(h,name);};
 at(8192,p1440,"enhanced");at(8192,wide,"balanced");at(8192,p2160,"balanced");at(8192,p2880,"performance");
 at(12288,p1080,"quality");at(12288,p1440,"quality");at(12288,wide,"enhanced");at(12288,p2160,"enhanced");at(12288,p2880,"balanced");at(12288,p4320,"balanced");
 at(16384,p1080,"ultra");at(16384,p1440,"ultra");at(16384,wide,"quality");at(16384,p2160,"quality");at(16384,p2880,"enhanced");at(16384,p4320,"enhanced");
 at(24576,p1440,"ultra");at(24576,p2160,"ultra");at(24576,p2880,"quality");at(32768,p2160,"ultra");at(32768,p4320,"quality");
 at(8192,{2560,1600},"enhanced");at(16384,{2560,1600},"ultra");at(16384,{3840,2400},"quality");
 // Memory alone never reaches the shadow-mapped tiers: they need system RAM,
 // the modern effects and, above Enhanced, eight CPU threads.
 s=desktop;s.videoRamMB=24576;s.logicalCpus=6;expect(s,"enhanced");s.logicalCpus=7;expect(s,"enhanced");s.logicalCpus=8;expect(s,"ultra");
 s.logicalCpus=3;expect(s,"performance");s.logicalCpus=4097;expect(s,"performance");
 s=desktop;s.videoRamMB=24576;s.systemRamMB=15359;expect(s,"balanced");s.systemRamMB=15360;expect(s,"ultra");
 s=desktop;s.videoRamMB=24576;s.modernEffects=false;expect(s,"balanced");
 s=desktop;s.videoRamMB=24576;s.gpuClass=GPU::Integrated;expect(s,"balanced"); // Shared memory is never a VRAM class.
 s.gpuClass=GPU::Unknown;expect(s,"performance");
 s=desktop;s.videoRamMB=24576;s.steamDeck=true;expect(s,"performance");
 for(const auto& [name,type]:std::vector<std::pair<const char*,GPU>>{
  {"NVIDIA GeForce RTX 4060 Laptop GPU",GPU::Discrete},{"AMD Radeon RX 6800",GPU::Discrete},
  {"ANGLE (Intel, Intel(R) Iris(R) Xe Graphics, D3D11)",GPU::Integrated},
  {"Intel(R) Arc(TM) A770 Graphics",GPU::Discrete},{"Apple M4 Pro",GPU::Integrated},
  {"AMD Radeon R7 Graphics",GPU::Integrated},{"AMD Radeon R7 260X",GPU::Discrete},
  {"AMD Radeon 780M",GPU::Integrated},{"Intel(R) Arc(TM) Graphics",GPU::Integrated},
  {"Apple GPU",GPU::Integrated},{"NVIDIA Tegra X1",GPU::Integrated},
  {"AMD Radeon(TM) Graphics",GPU::Integrated},{"Mali-G610",GPU::Integrated},
  {"Adreno (TM) 740",GPU::Integrated},{"V3D 7.1",GPU::Integrated},
  {"llvmpipe (LLVM 19)",GPU::Software},{"SwiftShader Device",GPU::Software},
  {"SVGA3D; build",GPU::Virtual},{"virgl",GPU::Virtual},{"Future Unknown GPU",GPU::Unknown},
  // Trademark marks and spacing vary by driver and must not change the class.
  {"AMD Radeon(TM) 780M",GPU::Integrated},{"AMD Radeon(TM) 8060S Graphics",GPU::Integrated},
  {"Radeon (TM) RX 480 Graphics",GPU::Discrete},{"AMD Radeon(TM) RX 6600 XT",GPU::Discrete},
  {"Mesa Intel(R) Xe Graphics (TGL GT2)",GPU::Integrated},{"Intel(R) UHD Graphics 620",GPU::Integrated},
  {"Mesa Intel(R) Graphics (RPL-P)",GPU::Integrated},{"Mesa Intel(R) Arc(tm) A770 Graphics (DG2)",GPU::Discrete},
  {"Intel(R) Arc(TM) 140V GPU (16GB)",GPU::Integrated},{"Intel(R) Arc(TM) B390 GPU",GPU::Integrated},
  {"Intel(R) Arc(TM) B580 Graphics",GPU::Discrete},{"NVIDIA TITAN RTX/PCIe/SSE2",GPU::Discrete},
  {"Mali-G925-Immortalis MC12",GPU::Integrated},{"Immortalis-G720",GPU::Integrated},
  {"ANGLE (Samsung Xclipse 940) on Vulkan 1.3",GPU::Integrated},{"Adreno (TM) 830",GPU::Integrated},
  {"AMD Custom GPU 0405 (radeonsi, vangogh, LLVM 17.0.6)",GPU::Integrated},
  {"D3D12 (Microsoft Basic Render Driver)",GPU::Software},
  {"",GPU::Unknown}})Check(RendererClassifyGLDevice(name)==type,"active GL driver classification");
 Check(RendererClassifyGLDevice(std::string(4096,'x')+" GeForce")==GPU::Unknown,"an overlong name is bounded, not overrun");
 // Memory cannot override hardware/power constraints, and ISA never ranks a GPU.
 for(int flags=0;flags<256;++flags)for(auto gpu:{GPU::Unknown,GPU::Integrated,GPU::Discrete,GPU::Virtual,GPU::Software})
 for(int ram:{-1,0,2048,4096,8192,16384,4194305})for(int vram:{-1,0,512,1024,2048,8192,12288,16384,32768,262145}){
  s=desktop;s.explicitLowPower=flags&1;s.raspberryPi=flags&2;s.steamDeck=flags&4;s.legacyRenderer=flags&8;s.arm64=flags&16;s.mobile=flags&32;
  s.linuxHost=flags&64;s.appleGpu=flags&128;
  s.gpuClass=gpu;s.systemRamMB=ram;s.videoRamMB=vram;const auto result=DetectPerformancePreset(s);
  const bool board=PerformancePresetIsArmBoard(s);
  Check(result.preset>=&PerformancePresets()[0]&&result.preset<FindPerformancePreset("maximum"),"auto never selects Maximum, which has no frame-rate target");
  if(gpu!=GPU::Discrete||vram<PerformancePresetVram12GiB||vram>262144)Check(result.preset<=FindPerformancePreset("enhanced"),"only a verified 12 GiB class card passes Enhanced");
  if(board)Check(result.preset<=FindPerformancePreset("performance"),"ARM board ceiling across hardware combinations");
  // Only the board rule reads the ISA, and only with a desktop Linux host.
  if(s.appleGpu||!s.linuxHost){auto x86=s;x86.arm64=!x86.arm64;Check(DetectPerformancePreset(x86).preset==result.preset,"CPU ISA alone cannot rank graphics; Apple Silicon is never a board");}
  if(gpu==GPU::Software||s.legacyRenderer)Check(result.preset==FindPerformancePreset("minimum"),"capability limits dominate environment hints");
  if(gpu==GPU::Discrete&&vram>0&&vram<=512)Check(result.preset==FindPerformancePreset("minimum"),"dedicated memory limits dominate all power hints");
  if(s.mobile)Check(result.preset<=FindPerformancePreset("performance")&&result.preset!=FindPerformancePreset("low"),"mobile power ceiling across hardware combinations");
  if(s.steamDeck)Check(result.preset<=FindPerformancePreset("performance"),"Deck power ceiling across hardware combinations");
  if(s.explicitLowPower||s.raspberryPi)Check(result.preset<=FindPerformancePreset("lowpower"),"low-power ceiling across hardware combinations");
  Check(result.systemRamMB>=0&&result.systemRamMB<=4194304&&result.videoRamMB>=0&&result.videoRamMB<=262144,"bounded telemetry");
 }
 // Shadows are the one feature the three lowest tiers drop, and nothing above does:
 // Low keeps 60 Hz on an Iris Xe class GPU at 1080p only without them.
 for(const auto& preset:PerformancePresets()){
  const std::string name=preset.name;
  const bool shadowless=name=="minimum"||name=="lowpower"||name=="low";
  Check(preset.shadows==(shadowless?0:1),"only Minimum, Low Power and Low draw no shadows");
  Check(preset.useLightGrid==(shadowless?0:1),"only Minimum, Low Power and Low skip the light grid");
  // Their 30 Hz cap is what stands automatic temporal AA down in the renderer.
  Check((preset.maxFps<=30)==(name=="minimum"||name=="lowpower"),"only Minimum and Low Power cap at 30 Hz");
 }
}
static void DetectionResolution(){
 using GPU=RendererGpuClass;
 PerformancePresetSignals s;
 s.systemRamMB=16384;s.videoRamMB=128;s.gpuClass=GPU::Integrated;
 s.modernEffects=true;s.logicalCpus=8;s.drawableWidth=1920;s.drawableHeight=1080;
 // The second number is the render scale detection used to choose before
 // presets stopped owning it. Every machine now keeps the player's scale
 // (native by default); a large drawable lowers the tier instead.
 auto expect=[&](const char* name,int,int aa){
  const auto detection=DetectPerformancePreset(s);PerformancePreset resolved;
  Check(ResolveDetectedPerformancePreset(detection,resolved),"detected record resolves");
  Check(std::string(resolved.name)==name&&resolved.postAA==aa,"independent automatic tier/AA expectation");
  PerformancePresetAssignments out;std::string error;
  Check(ExpandPerformancePreset(resolved,out,error),"detected record expands");
  for(const auto& assignment:out)Check(std::string(assignment.key)!="r_screenFraction","detection never sets the render scale");
 };
 Check(PerformancePresetClassPixels(540)==656100&&PerformancePresetClassPixels(720)==1166400&&PerformancePresetClassPixels(1080)==2624400&&
  PerformancePresetClassPixels(1440)==4665600&&PerformancePresetClassPixels(2160)==10497600,"resolution classes");
 expect("balanced",100,1);
 s.drawableWidth=1920;s.drawableHeight=1200;expect("balanced",100,1);
 s.drawableWidth=2560;s.drawableHeight=1440;expect("performance",75,1);
 s.drawableWidth=2560;s.drawableHeight=1600;expect("performance",75,1);
 s.drawableWidth=2560;s.drawableHeight=1664;expect("performance",75,1); // A 13-inch Retina panel renders 1920x1248.
 s.drawableWidth=3024;s.drawableHeight=1964;expect("performance",50,1);
 s.drawableWidth=3840;s.drawableHeight=2160;expect("performance",50,1);
 s.drawableWidth=5120;s.drawableHeight=2880;expect("performance",25,1);
 s.drawableWidth=1920;s.drawableHeight=1080;s.modernEffects=false;expect("balanced",100,0);
 s.postAAAvailable=true;expect("balanced",100,1); // AA can exist without all modern effects.
 s.modernEffects=true;s.gpuClass=GPU::Discrete;s.videoRamMB=4096;
 s.drawableWidth=3840;s.drawableHeight=2160;expect("balanced",75,1); // A 1620p scene: the 1440p class.
 s.videoRamMB=8192;expect("balanced",100,1);
 s.drawableWidth=7680;s.drawableHeight=4320;expect("performance",50,1);
 s.videoRamMB=16384;expect("enhanced",50,2);s.drawableWidth=5120;s.drawableHeight=2880;expect("enhanced",75,2);
 s.videoRamMB=8192;
 s.drawableWidth=3840;s.drawableHeight=2160;s.steamDeck=true;expect("performance",50,1);
 s.steamDeck=false;s.raspberryPi=true;expect("lowpower",25,0);
 s.raspberryPi=false;s.gpuClass=GPU::Software;expect("minimum",25,0);
 s.gpuClass=GPU::Unknown;expect("performance",50,1);
 // The documented minimum PC on a 1080p display renders 1440x810 with shadows.
 s.gpuClass=GPU::Discrete;s.videoRamMB=1024;s.drawableWidth=1920;s.drawableHeight=1080;expect("low",75,0);
 s.drawableWidth=1366;s.drawableHeight=768;expect("low",75,0);s.drawableWidth=2560;s.drawableHeight=1440;expect("low",50,0);
 s.videoRamMB=8192;s.raspberryPi=true;s.drawableWidth=1920;s.drawableHeight=1080;expect("lowpower",75,0);s.raspberryPi=false;
 // The Pi 5 of issue #233: 8 GiB, V3D on GL, a 720p display.
 s.gpuClass=GPU::Integrated;s.videoRamMB=0;s.systemRamMB=8058;s.logicalCpus=4;s.arm64=s.linuxHost=true;s.raspberryPi=true;
 s.modernEffects=false;s.postAAAvailable=false;s.drawableWidth=1280;s.drawableHeight=720;expect("lowpower",75,0);
 s.drawableWidth=1920;s.drawableHeight=1080;expect("lowpower",75,0);s.drawableWidth=3840;s.drawableHeight=2160;expect("lowpower",25,0);
 // Any other ARM board: Performance's 85% up to 1080p, then the iGPU scene budget.
 s.raspberryPi=false;s.systemRamMB=16384;s.logicalCpus=8;s.modernEffects=true;s.drawableWidth=1920;s.drawableHeight=1080;expect("performance",85,1);
 s.drawableWidth=2560;s.drawableHeight=1440;expect("performance",75,1);s.drawableWidth=3840;s.drawableHeight=2160;expect("performance",50,1);
 // Asahi Linux on an M2 keeps Apple Silicon's Balanced.
 s.appleGpu=true;s.drawableWidth=1920;s.drawableHeight=1080;expect("balanced",100,1);
 s.appleGpu=false;s.arm64=s.linuxHost=false;s.gpuClass=GPU::Discrete;s.videoRamMB=8192;s.systemRamMB=16384;s.postAAAvailable=true;
 // Phones: a 12 GiB flagship keeps the 1080p class, smaller ones a 720p scene.
 s.gpuClass=GPU::Integrated;s.mobile=true;s.systemRamMB=11400;
 s.drawableWidth=2340;s.drawableHeight=1080;expect("performance",85,1);
 s.drawableWidth=3120;s.drawableHeight=1440;expect("performance",75,1);
 s.drawableWidth=3840;s.drawableHeight=2160;expect("performance",50,1);
 s.systemRamMB=10239;s.drawableWidth=2340;s.drawableHeight=1080;expect("performance",50,1);
 s.systemRamMB=7600;s.drawableWidth=2400;s.drawableHeight=1080;expect("performance",50,1);
 s.drawableWidth=1600;s.drawableHeight=720;expect("performance",85,1);
 s.drawableWidth=3840;s.drawableHeight=2160;expect("performance",25,1);
 s.systemRamMB=3700;s.drawableWidth=1600;s.drawableHeight=720;expect("lowpower",75,0);
 s.drawableWidth=2400;s.drawableHeight=1080;expect("lowpower",50,0);
 s.systemRamMB=1900;expect("minimum",50,0);
 s.mobile=false;s.systemRamMB=16384;s.videoRamMB=128;
 for(int bad:{-1,0,32769,2147483647}){
  s.drawableWidth=bad;s.drawableHeight=1080;expect("performance",85,1);
  s.drawableWidth=1920;s.drawableHeight=bad;expect("performance",85,1);
 }
 s.gpuClass=GPU::Software;s.videoRamMB=128;s.mobile=false;s.drawableWidth=32768;s.drawableHeight=18432;
 auto output=*FindPerformancePreset("ultra");PerformancePresetDetection missing;
 Check(!ResolveDetectedPerformancePreset(missing,output)&&std::string(output.name)=="ultra","failed resolution leaves output unchanged");
 auto invalid=DetectPerformancePreset(s);invalid.postAA=9;
 Check(!ResolveDetectedPerformancePreset(invalid,output)&&std::string(output.name)=="ultra","invalid recommendation cannot change output");
 Check(FindPerformancePreset("balanced")->postAA==1,"automatic adaptations leave manual recipes immutable");
 // A detected start presents at the display's whole-hertz rate (FPS-02); an
 // unknown or implausible rate keeps the tier's manual-choice cap.
 {
  PerformancePresetSignals d;d.systemRamMB=32768;d.videoRamMB=8192;d.gpuClass=RendererGpuClass::Discrete;
  d.modernEffects=true;d.logicalCpus=8;d.drawableWidth=2560;d.drawableHeight=1440;
  const int tierCap=FindPerformancePreset("enhanced")->maxFps;
  for(const auto& [hz,cap]:std::vector<std::pair<int,int>>{{240,240},{144,144},{60,60},{30,30},{0,tierCap},{29,tierCap},{-1,tierCap},{1000,1000},{1001,tierCap}}){
   d.displayRefreshHz=hz;const auto detected=DetectPerformancePreset(d);PerformancePreset resolved;
   Check(detected.maxFps==cap&&ResolveDetectedPerformancePreset(detected,resolved)&&resolved.maxFps==cap,"detected frame cap follows the display");
  }
  d.displayRefreshHz=240;auto bad=DetectPerformancePreset(d);bad.maxFps=1001;PerformancePreset untouched=*FindPerformancePreset("ultra");
  Check(!ResolveDetectedPerformancePreset(bad,untouched)&&untouched.maxFps==FindPerformancePreset("ultra")->maxFps,"out-of-range detected cap refused");
  Check(FindPerformancePreset("enhanced")->maxFps==tierCap,"detection leaves the manual cap immutable");
  // A power-limited device keeps its tier's cap when the display is faster,
  // and still follows a slower display.
  const auto cap=[&](const PerformancePresetSignals& signals,int hz){auto copy=signals;copy.displayRefreshHz=hz;return DetectPerformancePreset(copy).maxFps;};
  auto phone=d;phone.mobile=true;phone.gpuClass=RendererGpuClass::Integrated;
  Check(cap(phone,120)==60&&cap(phone,90)==60&&cap(phone,60)==60&&cap(phone,48)==48&&cap(phone,0)==60,"a phone keeps Performance's 60 on a 120 Hz panel");
  auto deck=d;deck.steamDeck=true;
  Check(cap(deck,90)==60&&cap(deck,40)==40,"a handheld keeps its tier cap");
  auto pi=d;pi.raspberryPi=true;
  Check(cap(pi,60)==30&&cap(pi,30)==30&&cap(pi,0)==30,"a small board keeps Low Power's 30");
  auto board=d;board.arm64=board.linuxHost=true;board.gpuClass=RendererGpuClass::Integrated;
  Check(cap(board,144)==60&&cap(board,60)==60&&cap(board,50)==50&&cap(board,0)==60,"an ARM board keeps Performance's 60");
  auto mac=board;mac.appleGpu=true;mac.drawableWidth=1920;mac.drawableHeight=1080;
  Check(cap(mac,120)==120,"Apple Silicon follows its display");
  auto request=d;request.explicitLowPower=true;
  Check(cap(request,144)==30,"an explicit low-power request keeps its cap");
  auto small=d;small.systemRamMB=4096;
  Check(cap(small,75)==75&&cap(small,0)==60,"the minimum PC follows its display");
 }
 for(int spec=0;spec<4;++spec){
  const char* names[]={"low","performance","balanced","ultra"};
  Check(PerformancePresetForMachineSpec(spec)==FindPerformancePreset(names[spec]),"stock quality maps to canonical recipe");
 }
 for(int spec:{-2147483647,-1,4,2147483647})Check(!PerformancePresetForMachineSpec(spec),"invalid stock classification cannot apply a recipe");
}
struct Host:SettingsHost{
 StateValues live={{"number",1.0},{"flag",true},{"label",std::string("custom")}};
 std::function<void()> validation;int writes=0,reads=0;bool reject=false;
 bool Read(StateValues& out,std::string&)override{++reads;out=live;return true;}
 bool Defaults(StateValues& out,std::string&)override{out=live;return true;}
 bool Validate(const StateValues&,const StateValues& value,std::string&)override{if(validation)validation();return !reject&&std::get<double>(value.at("number"))<=2;}
 bool Write(const StateValues&,std::string&)override{++writes;return false;}
 bool NeedsConfirmation(const StateValues&,const StateValues&)const override{return false;}
};
static void Generated(){
 Host host;SettingsTransaction tx(host);int called=0;
 SettingsTransaction::EditGenerator good=[&](StateValues& out,std::string&){++called;out={{"number",1.5}};return true;};
 Check(tx.EditGenerated(1,good).code==SettingsCode::NotOpen&&called==0,"closed never observes capability");
 Check(tx.Begin(1).code==SettingsCode::Ok,"begin");const auto baseline=tx.Baseline();
 for(auto owner:{0u,2u})Check(tx.EditGenerated(owner,good).code!=SettingsCode::Ok&&called==0,"wrong owner never invokes generator");
 Check(tx.EditGenerated(1,{}).code==SettingsCode::Invalid,"empty producer");
 Check(tx.EditGenerated(1,good).code==SettingsCode::Ok,"valid generated edit");const auto saved=tx.Draft();
 Check(saved.at("number")==StateValue(1.5)&&saved.at("label")==baseline.at("label")&&host.writes==0&&host.reads==1,"draft-only change");
 for(const auto& partial:std::vector<StateValues>{{{"unknown",true}},{{"number",true}},{{"number",std::numeric_limits<double>::infinity()}},{{"number",std::numeric_limits<double>::quiet_NaN()}},{{"number",3.0}},{{"label",std::string("x\0y",3)}}}){
  auto result=tx.EditGenerated(1,[&](StateValues& out,std::string&){out=partial;return true;});
  Check(result.code==SettingsCode::Invalid&&SettingsValuesEqual(tx.Draft(),saved),"invalid complete candidate unchanged");
 }
 for(int failure=0;failure<3;++failure){
  host.reject=failure==2;
  auto result=tx.EditGenerated(1,[&](StateValues& out,std::string&)->bool{out={{"number",1.9}};if(failure==1)throw std::runtime_error("generator");return failure!=0;});
  Check(result.code==SettingsCode::Invalid&&SettingsValuesEqual(tx.Draft(),saved),"generator/validation failure atomic");
 }host.reject=false;
 const std::vector<std::function<SettingsResult()>> nested={
 [&]{return tx.Begin(1);},[&]{return tx.Edit(1,{});},[&]{return tx.EditGenerated(1,good);},[&]{return tx.Defaults(1);},[&]{return tx.Cancel(1);},[&]{return tx.Apply(1,0);},[&]{return tx.Confirm(1);},[&]{return tx.Revert(1);},[&]{return tx.Tick(0);},[&]{return tx.Abandon(1);},
 [&]{SettingsAttempt a;return tx.PrepareApply(1,0,a);},[&]{return tx.ExecuteApply(1,1);},[&]{return tx.CompleteApply(1,1,0);},[&]{return tx.CancelPreparedApply(1,1);},[&]{SettingsAttempt a;return tx.PrepareRestore(1,1,a);},[&]{return tx.ExecuteRestore(1,1);},[&]{return tx.CompleteRestore(1,1);},[&]{SettingsAttempt a;return tx.PrepareConfirm(1,1,0,a);},[&]{return tx.CompleteConfirm(1,1);},[&]{return tx.CancelPreparedConfirm(1,1);}};
 for(const auto& enter:nested)for(bool duringValidation:{false,true}){
  auto reenter=[&]{Check(enter().code==SettingsCode::Busy,"all nested mutations refused");};
  if(duringValidation)host.validation=reenter;
  auto result=tx.EditGenerated(1,[&](StateValues& out,std::string&){if(!duringValidation)reenter();out={{"number",1.9}};return true;});
  host.validation={};Check(result.code==SettingsCode::Busy&&SettingsValuesEqual(tx.Draft(),saved),"ignored reentry invalidates outer generated edit");
 }
 // Ordinary Edit still permits its historical ignored Busy callback response.
 host.validation=[&]{Check(tx.Cancel(1).code==SettingsCode::Busy,"legacy callback busy");};
 Check(tx.Edit(1,{{"number",1.5}}).code==SettingsCode::Ok,"ordinary edit behavior unchanged");host.validation={};
 bool succeeded=false;int failures=0;
#if defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL > 0
 constexpr bool debugReturnOnly=true;
#else
 constexpr bool debugReturnOnly=false;
#endif
 for(int n=0;n<256&&!succeeded;++n){
  SettingsTransaction::EditGenerator allocating=[](StateValues& out,std::string&){out.emplace("number",1.9);out.emplace("label",std::string(600,'p'));return true;};
  SettingsCode code=SettingsCode::Invalid;
  // MSVC debug string moves allocate inside noexcept. The normal STL sweep
  // covers construction; debug mode targets the final real return-storage
  // proxy by arming at the last read-only validation callback, before publish.
  if(debugReturnOnly)host.validation=[&]{failAllocation=n;};else failAllocation=n;
  try{code=tx.EditGenerated(1,allocating).code;}catch(const std::bad_alloc&){failAllocation=-1;std::fprintf(stderr,"allocation failure index %d, draft equal %d\n",n,SettingsValuesEqual(tx.Draft(),saved));Check(false,"allocation exception escaped generated edit");}
  const auto remaining=failAllocation;failAllocation=-1;host.validation={};
  if(code==SettingsCode::Ok){succeeded=true;Check(remaining==0,"success reached only after sweeping actual return allocation");}else{++failures;Check(SettingsValuesEqual(tx.Draft(),saved),"each allocation failure preserves draft");Check(tx.LastResult().code==code,"failure result is not a published success");}
 }
 Check(succeeded&&failures>=(debugReturnOnly?1:6)&&tx.LastResult().code==SettingsCode::Ok&&tx.LastResult().diagnostic.empty(),"every prepublication/return allocation fault reached; final success published");
 std::printf("generated edit swept %d failing allocation positions before complete return/publication (debug return-only=%d)\n",failures,debugReturnOnly);
 Check(host.writes==0&&host.live==baseline&&tx.Owner()==1&&tx.Phase()==SettingsPhase::Editing,"no host effects throughout failures");
 SettingsAttempt attempt;Check(tx.PrepareApply(1,0,attempt).code==SettingsCode::Ok,"freeze for device");const int before=called;
 Check(tx.EditGenerated(1,good).code==SettingsCode::Busy&&called==before,"frozen device transaction never observes profile");
}
// The preset system version (docs/dev/ui/performance-presets.md, "Preset
// system version"): the revision and an automatic fingerprint of every preset
// and of detection. The pin below is meant to move: any change to a preset,
// a target or a detection rule changes the fingerprint, and players are then
// offered the system check again. Update the pin with that change and say so
// in the release notes.
static void SystemVersion(){
 const std::string version=PerformancePresetSystemVersion();
 Check(version==PerformancePresetSystemVersion()&&PerformancePresetSystemFingerprint()==PerformancePresetSystemFingerprint(),"version is stable");
 const std::string revision=std::to_string(PerformancePresetSystemRevision)+".";
 Check(version.compare(0,revision.size(),revision)==0&&version.size()==revision.size()+8,"version is revision.fingerprint");
 for(size_t i=revision.size();i<version.size();++i)
  Check((version[i]>='0'&&version[i]<='9')||(version[i]>='a'&&version[i]<='f'),"fingerprint is lower-case hex");
 Check(PerformancePresetFingerprintMachines()==9042,"detection matrix covers every GPU class, memory band, resolution class and platform ceiling");
 if(version!="1.d7f4b595")std::fprintf(stderr,"preset system version is %s\n",version.c_str());
 Check(version=="1.d7f4b595","pinned preset system version; a preset or detection change moves it: update this pin and the release notes");
}
int main(){
#if defined(_WIN32) && defined(_DEBUG)
 _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
 _CrtSetReportMode(_CRT_ERROR,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ERROR,_CRTDBG_FILE_STDERR);
#endif
 AssignmentCapture();Profiles();Detection();DetectionResolution();Generated();SystemVersion();std::printf("performance profiles and generated transaction: %d checks passed\n",checks);}
