// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include <string_view>

// Active-device facts, not a performance score. Unknown is deliberately useful:
// neither an OS adapter inventory nor shared RAM establishes dedicated VRAM.
enum class RendererGpuClass { Unknown, Integrated, Discrete, Virtual, Software };
inline bool RendererNameContains(std::string_view name, std::string_view token) noexcept {
 if (token.size() > name.size()) return false;
 const auto lower=[](unsigned char c){return c>='A'&&c<='Z'?c+('a'-'A'):c;};
 for(size_t i=0;i+token.size()<=name.size();++i){
  size_t j=0;for(;j<token.size()&&lower(name[i+j])==lower(token[j]);++j){}
  if(j==token.size())return true;
 }
 return false;
}
inline RendererGpuClass RendererClassifyGLDevice(std::string_view name) noexcept {
 // Match lower-case text without (R)/(TM) marks and with single spaces, so
 // "Radeon(TM) 780M", "Radeon (TM) RX 480" and "Radeon RX 480" read alike.
 char text[256];size_t length=0;
 const auto lower=[](unsigned char c){return c>='A'&&c<='Z'?c+('a'-'A'):c;};
 for(size_t i=0;i<name.size()&&length<sizeof(text);){
  const auto mark=[&](std::string_view token){
   if(name.size()-i<token.size())return false;
   for(size_t j=0;j<token.size();++j)if(lower(name[i+j])!=static_cast<unsigned char>(token[j]))return false;
   return true;
  };
  if(mark("(r)")){i+=3;continue;}
  if(mark("(tm)")){i+=4;continue;}
  const char c=static_cast<char>(lower(name[i++]));
  if(c==' '&&length>0&&text[length-1]==' ')continue;
  text[length++]=c;
 }
 const std::string_view clean(text,length);
 const auto has=[&](std::string_view token){return clean.find(token)!=std::string_view::npos;};
 if(has("llvmpipe")||has("softpipe")||has("swiftshader")||has("software rasterizer")||
    has("gdi generic")||has("basic render driver")||has("lavapipe"))return RendererGpuClass::Software;
 if(has("virgl")||has("svga3d")||has("virtualbox")||has("parallels"))return RendererGpuClass::Virtual;
 // Integrated names are checked before vendor/family names (including ANGLE).
 // Intel's Arc B390/B370 are integrated; the discrete B-series is B5xx/B7xx.
 if(has("apple m")||has("apple gpu")||has("iris")||has("uhd graphics")||has("hd graphics")||
    has("intel graphics")||has("intel xe graphics")||has("radeon graphics")||
    has("radeon vega")||has("adreno")||has("mali")||has("immortalis")||has("xclipse")||
    has("powervr")||has("videocore")||has("v3d")||has("tegra")||
    has("radeon r7 graphics")||has("radeon r5 graphics")||
    has("radeon 610m")||has("radeon 660m")||has("radeon 680m")||
    has("radeon 740m")||has("radeon 760m")||has("radeon 780m")||
    has("radeon 820m")||has("radeon 840m")||has("radeon 860m")||
    has("radeon 880m")||has("radeon 890m")||has("radeon 8040s")||
    has("radeon 8050s")||has("radeon 8060s")||has("amd custom gpu")||
    has("arc graphics")||has("arc 1")||has("arc 2")||
    has("arc b390")||has("arc b370"))return RendererGpuClass::Integrated;
 if(has("geforce")||has("quadro")||has("nvidia rtx")||has("nvidia titan")||has("radeon rx")||
    has("radeon pro")||has("radeon r9")||has("radeon r7")||has("radeon vii")||
    has("arc a")||has("arc b"))return RendererGpuClass::Discrete;
 return RendererGpuClass::Unknown;
}
inline const char* RendererGpuClassName(RendererGpuClass type) noexcept {
 switch(type){
  case RendererGpuClass::Integrated:return "integrated/shared";
  case RendererGpuClass::Discrete:return "discrete";
  case RendererGpuClass::Virtual:return "virtual";
  case RendererGpuClass::Software:return "software";
  default:return "unknown";
 }
}
