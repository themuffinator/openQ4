#!/usr/bin/env python3
"""Actual CPU DDS/cube/resample methods with counted file/allocator/encoder doubles.

No native driver, real filesystem asset, compressed codec implementation or content identity
qualification. DDS parsing/layout/owned mip views and RGBA filter bytes are real.
"""
from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess, tempfile
from renderer_consumed_policy import method
ROOT=Path(__file__).resolve().parents[2]
SUPPORT=r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "RendererConsumedPolicy.h"
using byte=unsigned char;using uint32=uint32_t;using int64=int64_t;using ID_TIME_T=int64_t;
#define ID_INLINE inline
#define ALIGN16(x) alignas(16) x
static int checks=0,allocCalls=0,failAt=0;static bool throwAllocation=false;static int listCalls=0,failListAt=0,encodeCalls=0,failEncodeAt=0;
#define TEST(x) do{++checks;if(!(x))throw std::runtime_error(#x);}while(0)
template<class T>T Max(T a,T b){return a>b?a:b;}template<class T>T Min(T a,T b){return a<b?a:b;}
std::map<void*,size_t> allocations;
void* Mem_Alloc(size_t bytes){if(++allocCalls==failAt){if(throwAllocation)throw std::bad_alloc();return nullptr;}void*p=std::malloc(bytes);if(!p)throw std::bad_alloc();allocations.emplace(p,bytes);return p;}
void Mem_Free(void*p){if(!p)return;TEST(allocations.erase(p)==1);std::free(p);}
void* R_StaticAlloc(size_t n){return Mem_Alloc(n);}void R_StaticFree(void*p){Mem_Free(p);}
struct idMath {static float Pow(float a,float b){return std::pow(a,b);}static byte Ftob(float x){return byte(std::clamp(x,0.f,255.f));}};
enum textureFormat_t{FMT_NONE,FMT_DXT1,FMT_DXT5,FMT_BC7,FMT_RGBA8,FMT_OTHER,FMT_ETC2_RGB8,FMT_ETC2_RGBA8,FMT_EAC_RG11,FMT_LUM8,FMT_INT8,FMT_ALPHA,FMT_L8A8,FMT_RGB565};
enum textureColor_t{CFM_DEFAULT,CFM_NORMAL_DXT5,CFM_YCOCG_DXT5,CFM_GREEN_ALPHA};enum {TT_2D=1,TT_CUBIC=2};
using textureUsage_t=int;enum {TD_DEFAULT,TD_BUMP,TD_FONT,TD_LIGHT,TD_PBR_COLOR};
const int MAX_BINARY_IMAGE_DIMENSION=32768,MAX_BINARY_IMAGE_LEVELS=32,MAX_BINARY_IMAGE_DATA_SIZE=1<<30;
struct bimageImage_t{int level=0,destZ=0,width=0,height=0,dataSize=0;};
struct bimageFile_t{int textureType=0,width=0,height=0,numLevels=0;textureFormat_t format=FMT_NONE;textureColor_t colorFormat=CFM_DEFAULT;};
template<class T>struct idList:std::vector<T>{void SetNum(int n){if(++listCalls==failListAt)throw std::bad_alloc();this->resize(n);}void Clear(){this->clear();}int Num()const{return int(this->size());}T*Ptr(){return this->data();}};
struct idBinaryImageData:bimageImage_t{
 byte*data=nullptr;bool owns=false;idBinaryImageData()=default;idBinaryImageData(const idBinaryImageData&)=delete;
 idBinaryImageData(idBinaryImageData&&o)noexcept:bimageImage_t(o),data(std::exchange(o.data,nullptr)),owns(o.owns){}
 ~idBinaryImageData(){Free();}void Free(){if(owns)Mem_Free(data);data=nullptr;owns=false;dataSize=0;}
 void Alloc(int n){Free();dataSize=n;data=(byte*)Mem_Alloc(n);owns=true;}
 void SetExternalData(byte*p,int n){Free();data=p;dataSize=n;}
};
struct idBinaryImage{bimageFile_t fileData{};idList<idBinaryImageData>images;byte*loadedFileData=nullptr;int loadedFileBytes=0;imageFileContent_t fileContent{};void ObserveFileContent(const imageFileContent_t& v){fileContent=v;}
 ~idBinaryImage(){Clear();}void Clear();
 bool Load2DFromMemory(int,int,const byte*,int,textureFormat_t&,textureColor_t&,bool,bool);
 bool LoadCubeFromMemory(int,const byte*[6],int,textureFormat_t&,bool);
 void Load2DFromOwnedCompressedData(int,int,int,textureFormat_t,textureColor_t,byte*,const int*,const int*);
};
struct idStr{std::string s;idStr()=default;idStr(const char*v):s(v){};const char*c_str()const{return s.c_str();}
 static int snPrintf(char*p,size_t n,const char*fmt,...){va_list a;va_start(a,fmt);int v=vsnprintf(p,n,fmt,a);va_end(a);return v;}
 void BackSlashesToSlashes(){std::replace(s.begin(),s.end(),'\\','/');}
 void ExtractFileExtension(idStr&o)const{auto i=s.rfind('.');o.s=i==std::string::npos?"":s.substr(i+1);}
 int Icmp(const char*v)const{std::string a=s,b=v;for(auto&c:a)c=char(std::tolower((unsigned char)c));for(auto&c:b)c=char(std::tolower((unsigned char)c));return a.compare(b);}
};
struct idLib{template<class...T>static void Warning(const char*,T...){};};
const ID_TIME_T FILE_NOT_FOUND_TIMESTAMP=-1;
using cubeFiles_t=int;const int CF_CAMERA=1,MAX_IMAGE_NAME=1024;
struct idSuppressRetailProgramDDS{};
struct Common{template<class...T>void Warning(const char*,T...){};} commonStorage;Common*common=&commonStorage;
static int faceRead=0,missingFace=-1,rotations=0;static bool mismatchedFace=false;
void R_LoadImageProgram(const char*,byte**p,int*w,int*h,ID_TIME_T*t){int face=faceRead++;*w=7;*h=mismatchedFace&&face==missingFace?3:7;*t=73;if(p){*p=face==missingFace&&!mismatchedFace?nullptr:(byte*)Mem_Alloc(7*7*4);if(*p)memset(*p,face+1,7*7*4);}}
void R_RotatePic(byte*p,int){TEST(p);++rotations;}void R_HorizontalFlip(byte*p,int,int){TEST(p);++rotations;}void R_VerticalFlip(byte*p,int,int){TEST(p);++rotations;}

struct idFile{std::vector<byte>bytes;bool shortRead=false;int Length()const{return int(bytes.size());}int64_t Timestamp()const{return 73;}int Read(void*p,int n){if(shortRead)--n;memcpy(p,bytes.data(),n);return n;}};
struct FileSystem{idFile file;bool missing=false;int open=0;idFile*OpenFileRead(const char*){if(missing)return nullptr;++open;return &file;}void CloseFile(idFile*p){TEST(p==&file&&open==1);--open;}}fs;
FileSystem*fileSystem=&fs;
struct Caps{bool textureCompressionAvailable=true,bptcTextureCompressionAvailable=true;}caps;
const Caps&ImageTools_GetCompressionCaps(){return caps;}
static imageDownsizePolicy_t livePolicy{};static int policyReads=0;
void R_GetImageDownsizePolicy(const char*,textureUsage_t,bool,imageDownsizePolicy_t&out){++policyReads;out=livePolicy;}
struct idDxtEncoder{void Fill(const byte*src,byte*dst,int w,int h,int d){if(++encodeCalls==failEncodeAt)throw std::runtime_error("encoder refusal");TEST(w>=4&&h>=4&&w%4==0&&h%4==0);for(int i=0;i<w*h*4;++i)TEST(src[i]==src[i%4]);memset(dst,src[0],size_t(w)*h/d);}
 void CompressImageDXT1Fast(const byte*s,byte*d,int w,int h){Fill(s,d,w,h,2);}void CompressImageDXT5Fast(const byte*s,byte*d,int w,int h){Fill(s,d,w,h,1);}void CompressImageDXT1HQ(const byte*s,byte*d,int w,int h){Fill(s,d,w,h,2);}void CompressImageDXT5HQ(const byte*s,byte*d,int w,int h){Fill(s,d,w,h,1);}void CompressNormalMapDXT5HQ(const byte*s,byte*d,int w,int h){Fill(s,d,w,h,1);}void CompressNormalMapDXT5Fast(const byte*s,byte*d,int w,int h){Fill(s,d,w,h,1);}void CompressYCoCgDXT5HQ(const byte*s,byte*d,int w,int h){Fill(s,d,w,h,1);}void CompressYCoCgDXT5Fast(const byte*s,byte*d,int w,int h){Fill(s,d,w,h,1);}};
struct FakeCvar{bool value=false;bool GetBool()const{return value;}}image_highQualityCompression;
static bool failConversion=false;
struct idColorSpace{static void ConvertRGBToCoCg_Y(byte*,byte*,int,int){if(failConversion)throw std::runtime_error("conversion failure");}};
struct idEtcEncoder{void CompressImageETC2_RGB8(const byte*s,byte*d,int w,int h){idDxtEncoder{}.Fill(s,d,w,h,2);}void CompressImageETC2_RGBA8(const byte*s,byte*d,int w,int h){idDxtEncoder{}.Fill(s,d,w,h,1);}void CompressImageEAC_RG11(const byte*s,byte*d,int w,int h){idDxtEncoder{}.Fill(s,d,w,h,1);}};
'''
MAIN=r'''
static std::vector<byte> Pixels(int w,int h,int seed=1){std::vector<byte>p(size_t(w)*h*4);for(size_t i=0;i<p.size();++i)p[i]=byte(i*37+seed);return p;}
static void Filters(){
 for(auto d:std::vector<std::vector<int>>{{3,5,1,2},{16383,3,8191,1},{3,16383,1,8191},{32768,1,32767,1},{1,32768,1,32767},{11,19,7,13},{2,2,5,7}}){
  int iw=d[0],ih=d[1],ow=d[2],oh=d[3];auto src=Pixels(iw,ih);auto saved=src;byte*out=R_ResampleTexture(src.data(),iw,ih,ow,oh);TEST(out&&allocations[out]==size_t(ow)*oh*4);
  const uint64_t step=(uint64_t(iw)<<16)/ow;
  for(int y=0;y<oh;++y)for(int x=0;x<ow;++x)for(int c=0;c<4;++c){int x1=int(((step>>2)+uint64_t(x)*step)>>16),x2=int((3*(step>>2)+uint64_t(x)*step)>>16);int y1=int((uint64_t(y)*4+1)*ih/(uint64_t(oh)*4)),y2=int((uint64_t(y)*4+3)*ih/(uint64_t(oh)*4));int expected=(src[(size_t(y1)*iw+x1)*4+c]+src[(size_t(y1)*iw+x2)*4+c]+src[(size_t(y2)*iw+x1)*4+c]+src[(size_t(y2)*iw+x2)*4+c])/4;TEST(out[(size_t(y)*ow+x)*4+c]==expected);}
  TEST(src==saved);Mem_Free(out);
  failAt=allocCalls+1;TEST(!R_ResampleTexture(src.data(),iw,ih,ow,oh));TEST(src==saved);failAt=0;
 }
 byte pixel[4]{};int before=allocCalls;
 for(auto d:std::vector<std::vector<int>>{{0,1,1,1},{1,1,-1,1},{32769,1,1,1},{1,1,32769,1},{8193,8192,1,1},{1,1,8192,8193},{32768,32768,1,1},{1,1,32768,32768}})TEST(!R_ResampleTexture(pixel,d[0],d[1],d[2],d[3]));
 TEST(!R_ResampleTexture(nullptr,1,1,1,1));TEST(before==allocCalls);
 // The exact byte ceiling is accepted, but denial occurs before reading the tiny stand-in.
 failAt=allocCalls+1;TEST(!R_ResampleTexture(pixel,8192,8192,8192,8192));TEST(allocCalls==failAt);failAt=0;
}
static std::vector<byte> DDS(int w,int h,int levels,int format){
 const bool bc7=format==3;int header=bc7?148:128;std::vector<byte>out(header);auto put=[&](int at,uint32 v){out[at]=byte(v);out[at+1]=byte(v>>8);out[at+2]=byte(v>>16);out[at+3]=byte(v>>24);};
 put(0,0x20534444);put(4,124);put(8,0x20000);put(12,h);put(16,w);put(28,levels);put(76,32);put(80,4);put(84,format==0?0x31545844:format==1?0x35545844:format==2?0x42475852:0x30315844);
 if(bc7){put(128,98);put(132,3);put(140,1);}
 for(int l=0;l<levels;++l){size_t n=size_t((w+3)/4)*((h+3)/4)*(format==0?8:16);out.resize(out.size()+n,byte(l+17));w=Max(1,w/2);h=Max(1,h/2);}return out;
}
static void DDSLoads(){
 for(int format=0;format<4;++format)for(bool mips:{false,true})for(int chain:{1,2,5}){
  fs.file.bytes=DDS(16,8,chain,format);idBinaryImage image;imageReductionResult_t result;result.firstLevel=992;imageDownsizePolicy_t policy{0,2,1};ID_TIME_T stamp=0;
  TEST(R_LoadPrecompressedDDS("a.DDS",image,&stamp,TD_BUMP,policy,mips,&result));int skip=Min(2,chain-1);TEST(stamp==73&&fs.open==0);TEST(result.sourceWidth==16&&result.sourceHeight==8&&result.requestedWidth==4&&result.requestedHeight==2&&result.firstLevel==skip);
  TEST(result.status==(chain>=3?IR_EXACT:IR_INSUFFICIENT_MIPS));TEST(R_ImageReductionIsExact(policy,result)==(chain>=3));TEST(image.fileData.width==(16>>skip)&&image.fileData.height==Max(1,8>>skip));TEST(image.images.Num()==(mips?chain-skip:1));
  for(int i=0;i<image.images.Num();++i){const auto&v=image.images[i];TEST(v.data[0]==byte(17+skip+i));TEST(v.width==Max(1,16>>(skip+i))&&v.height==Max(1,8>>(skip+i)));TEST(!v.owns);}
  TEST(image.fileData.colorFormat==(format==1||format==2?CFM_NORMAL_DXT5:CFM_DEFAULT));
 }
 for(int mode=0;mode<5;++mode){fs.file.bytes=DDS(16,8,5,0);if(mode==0)fs.file.bytes.pop_back();fs.file.shortRead=mode==1;fs.missing=mode==2;caps.textureCompressionAvailable=mode!=3;if(mode==4)failAt=allocCalls+1;
  idBinaryImage im;imageReductionResult_t out;out.firstLevel=992;TEST(!R_LoadPrecompressedDDS("a.dds",im,nullptr,TD_DEFAULT,{},true,&out));TEST(out.firstLevel==992&&fs.open==0&&im.images.empty());failAt=0;fs.missing=fs.file.shortRead=false;caps.textureCompressionAvailable=true;
 }
 {fs.file.bytes=DDS(16,8,5,0);idBinaryImage image;imageReductionResult_t output;output.firstLevel=992;failAt=allocCalls+1;throwAllocation=true;try{R_LoadPrecompressedDDS("a.dds",image,nullptr,TD_DEFAULT,{},true,&output);TEST(false);}catch(const std::bad_alloc&){}TEST(fs.open==0&&output.firstLevel==992);failAt=0;throwAllocation=false;}
 for(int n=1;n<=3;++n){fs.file.bytes=DDS(16,8,5,0);idBinaryImage image;imageReductionResult_t out;out.firstLevel=992;failListAt=listCalls+n;try{R_LoadPrecompressedDDS("a.dds",image,nullptr,TD_DEFAULT,{},true,&out);TEST(false);}catch(const std::bad_alloc&){}TEST(fs.open==0&&out.firstLevel==992&&allocations.empty()&&image.images.empty());}failListAt=0;
 imageReductionResult_t out;out.firstLevel=77;TEST(!R_ResolveImageReduction({},0,8,1,out)&&out.firstLevel==77);TEST(!R_ResolveImageReduction({},8,8,33,out));TEST(!R_ResolveImageReduction({},1,1,2,out)&&out.firstLevel==77);TEST(!R_ResolveImageReduction({},8,8,5,out)&&out.firstLevel==77);
 for(int w:{1,3,16,32768})for(int h:{1,7,32})for(int shift:{0,1,4,30})for(int floor:{1,5,64}){imageDownsizePolicy_t p{64,shift,floor};TEST(R_ResolveImageReduction(p,w,h,0,out));TEST(R_ImageReductionIsExact(p,out));auto bad=out;++bad.requestedWidth;TEST(!R_ImageReductionIsExact(p,bad));bad=out;bad.status=IR_INSUFFICIENT_MIPS;TEST(!R_ImageReductionIsExact(p,bad));}
}
static void RawLoads(){
 for(auto shape:std::vector<std::pair<int,int>>{{16383,3},{16,8}}){int w=shape.first,h=shape.second;byte*p=(byte*)Mem_Alloc(size_t(w)*h*4);memset(p,77,size_t(w)*h*4);byte*original=p;auto owned=allocations.size();imageDownsizePolicy_t policy{0,1,1};
  failAt=allocCalls+1;R_DownsizeLoadedImageData("a",TD_DEFAULT,true,p,w,h,&policy);TEST(p==original&&w==shape.first&&h==shape.second&&allocations.size()==owned);failAt=0;
  R_DownsizeLoadedImageData("a",TD_DEFAULT,true,p,w,h,&policy);TEST(w==shape.first/2&&h==Max(1,shape.second/2)&&allocations[p]==size_t(w)*h*4);for(size_t i=0;i<allocations[p];++i)TEST(p[i]==77);Mem_Free(p);
 }
}
static void FaceLoads(){
 for(bool mismatch:{false,true})for(int bad=0;bad<6;++bad){faceRead=rotations=0;missingFace=bad;mismatchedFace=mismatch;byte*pics[6]{};int size=999;ID_TIME_T time=999;TEST(!R_LoadCubeImages("sky",CF_CAMERA,pics,&size,&time));TEST(size==999&&time==0&&allocations.empty());for(auto*p:pics)TEST(!p);TEST(faceRead==bad+1);}
 faceRead=rotations=0;missingFace=-1;mismatchedFace=false;byte*pics[6]{};int size=0;TEST(R_LoadCubeImages("sky",CF_CAMERA,pics,&size,nullptr));TEST(size==7&&rotations==8);for(auto*p:pics)Mem_Free(p);
}
static void Cubes(){
 for(int width:{2,7,16})for(int usage:{TD_DEFAULT,TD_PBR_COLOR})for(bool throwing:{false,true}){
  byte*pics[6]{};for(int i=0;i<6;++i){pics[i]=(byte*)Mem_Alloc(size_t(width)*width*4);memset(pics[i],i+1,size_t(width)*width*4);}byte*original[6];memcpy(original,pics,sizeof(pics));size_t owned=allocations.size();imageDownsizePolicy_t policy{0,1,1};
  for(int failure=1;failure<=6;++failure){int size=width;failAt=allocCalls+failure;throwAllocation=throwing;bool result=false;try{result=R_DownsizeLoadedCubeImageData("textures/a",usage,true,pics,size,&policy);}catch(const std::bad_alloc&){TEST(throwing);}TEST(!result&&size==width&&allocations.size()==owned);for(int i=0;i<6;++i)TEST(pics[i]==original[i]&&pics[i][0]==i+1);}
  failAt=0;throwAllocation=false;int size=width;int reads=policyReads;TEST(R_DownsizeLoadedCubeImageData("textures/a",usage,true,pics,size,&policy));TEST(size==width/2&&policyReads==reads);for(auto*p:pics)Mem_Free(p);
 }
 // Two-stage halving failure releases the first temporary mip and all earlier faces.
 {byte*pics[6];for(auto&p:pics){p=(byte*)Mem_Alloc(16*16*4);memset(p,42,16*16*4);}auto owned=allocations.size();imageDownsizePolicy_t policy{0,2,1};for(int f=1;f<=12;++f){int size=16;failAt=allocCalls+f;TEST(!R_DownsizeLoadedCubeImageData("a",TD_DEFAULT,true,pics,size,&policy));TEST(size==16&&allocations.size()==owned);}failAt=0;for(auto*p:pics)Mem_Free(p);}
 byte px[4]{};TEST(!R_ShrinkLoadedImageData(px,1,1,0,0,false));TEST(!R_ShrinkLoadedImageData(px,1,1,2,2,false));
 byte*none[6]{};int size=8;TEST(!R_DownsizeLoadedCubeImageData("a",TD_DEFAULT,true,none,size,nullptr));TEST(size==8);
}
static void Assembly(){
 for(int width:{1,2,3,7,8,15})for(auto format:{FMT_DXT1,FMT_DXT5,FMT_RGBA8,FMT_OTHER})for(bool gamma:{false,true}){
  std::vector<byte>faces[6];const byte*pics[6];for(int i=0;i<6;++i){faces[i].resize(size_t(width)*width*4);for(size_t j=0;j<faces[i].size();++j)faces[i][j]=byte(i*20+j%4+1);pics[i]=faces[i].data();}
  int levels=1;for(int w=width;w>1;w>>=1)++levels;idBinaryImage im;auto actual=format;int before=allocCalls;TEST(im.LoadCubeFromMemory(width,pics,levels,actual,gamma));int count=allocCalls-before;TEST(im.images.Num()==levels*6);TEST(im.fileData.width==width&&actual==(format==FMT_OTHER?FMT_RGBA8:format));
  for(const auto&v:im.images){int logical=Max(1,width>>v.level),padded=(logical+3)&~3;TEST(v.width==logical&&v.height==logical);TEST(v.dataSize==(actual==FMT_DXT1?padded*padded/2:actual==FMT_DXT5?padded*padded:logical*logical*4));}im.Clear();
  // Every allocation boundary, including final face/level and compression padding.
  for(bool throwing:{false,true})for(int n=1;n<=count;++n){throwAllocation=throwing;failAt=allocCalls+n;actual=format;bool result=false;try{result=im.LoadCubeFromMemory(width,pics,levels,actual,gamma);}catch(const std::bad_alloc&){TEST(throwing);}TEST(!result&&im.images.empty()&&allocations.empty()&&actual==format);}
  throwAllocation=false;failAt=0;
  failListAt=listCalls+1;try{im.LoadCubeFromMemory(width,pics,levels,actual,gamma);TEST(false);}catch(const std::bad_alloc&){}TEST(im.images.empty()&&allocations.empty());failListAt=0;
  if(format==FMT_DXT1){actual=format;failEncodeAt=encodeCalls+levels*6;try{im.LoadCubeFromMemory(width,pics,levels,actual,gamma);TEST(false);}catch(const std::runtime_error&e){TEST(std::string(e.what())=="encoder refusal");}TEST(im.images.empty()&&allocations.empty()&&actual==format);failEncodeAt=0;}
  for(int i=0;i<6;++i)for(size_t j=0;j<faces[i].size();++j)TEST(faces[i][j]==byte(i*20+j%4+1));
 }
}

static void Assembly2D(){
 for(auto format:{FMT_DXT1,FMT_DXT5,FMT_ETC2_RGB8,FMT_ETC2_RGBA8,FMT_EAC_RG11,FMT_LUM8,FMT_INT8,FMT_ALPHA,FMT_L8A8,FMT_RGB565,FMT_OTHER})for(auto color:{CFM_DEFAULT,CFM_NORMAL_DXT5,CFM_YCOCG_DXT5,CFM_GREEN_ALPHA}){
  int width=7,height=5,levels=3;std::vector<byte>src(size_t(width)*height*4,77);const auto saved=src;idBinaryImage im;auto f=format;auto c=color;image_highQualityCompression.value=color==CFM_DEFAULT;
  int before=allocCalls;TEST(im.Load2DFromMemory(width,height,src.data(),levels,f,c,false,true));int count=allocCalls-before;TEST(im.images.Num()==3&&im.fileData.width==7&&im.fileData.height==5);
  for(const auto&v:im.images)TEST(v.destZ==0&&v.width==Max(1,width>>v.level)&&v.height==Max(1,height>>v.level)&&v.data&&v.dataSize>0);im.Clear();TEST(allocations.empty());
  for(bool throwing:{false,true})for(int n=1;n<=count;++n){throwAllocation=throwing;failAt=allocCalls+n;f=format;c=color;bool okay=false;try{okay=im.Load2DFromMemory(width,height,src.data(),levels,f,c,true,true);}catch(const std::bad_alloc&){TEST(throwing);}TEST(!okay&&im.images.empty()&&allocations.empty()&&f==format&&c==color&&src==saved);}
  throwAllocation=false;failAt=0;failListAt=listCalls+1;f=format;c=color;try{im.Load2DFromMemory(width,height,src.data(),levels,f,c,false,true);TEST(false);}catch(const std::bad_alloc&){}TEST(im.images.empty()&&allocations.empty()&&f==format&&c==color&&src==saved);failListAt=0;
 }
 {std::vector<byte>src(16,77);idBinaryImage im;auto f=FMT_DXT5;auto c=CFM_YCOCG_DXT5;failConversion=true;try{im.Load2DFromMemory(2,2,src.data(),2,f,c,false,false);TEST(false);}catch(const std::runtime_error&e){TEST(std::string(e.what())=="conversion failure");}TEST(allocations.empty()&&im.images.empty()&&f==FMT_DXT5&&c==CFM_YCOCG_DXT5);failConversion=false;}
}
int main(){try{Filters();DDSLoads();TEST(allocations.empty());RawLoads();FaceLoads();Cubes();TEST(allocations.empty());Assembly();Assembly2D();TEST(allocations.empty());printf("Image exact reduction: %d checks passed\n",checks);return 0;}catch(const std::exception&e){fprintf(stderr,"FAILED %s (%d checks)\n",e.what(),checks);return 1;}}
'''

def main():
 p=argparse.ArgumentParser();p.add_argument('--compiler');p.add_argument('--sanitize',action='store_true');p.add_argument('--mutations',action='store_true');a=p.parse_args()
 names=['src/renderer/RendererConsumedPolicy.h','src/renderer/Image.h','src/renderer/Image_load.cpp','src/imagetools/Image_process.cpp','src/imagetools/Image_files.cpp','src/imagetools/BinaryImage.cpp','src/imagetools/BinaryImage.h','tools/tests/renderer_image_reduction.py','tools/tests/renderer_consumed_policy.py']
 names.append('src/renderer/RenderWorld_lightgrid.cpp')
 names += ['src/imagetools/ImageContentIdentity.h','src/imagetools/ImageContentIdentity.cpp','src/idlib/CryptoHash.h','src/idlib/CryptoHash.cpp']
 before={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in names}
 process=(ROOT/names[3]).read_text();files=(ROOT/names[4]).read_text();binary=(ROOT/names[5]).read_text();load=(ROOT/names[2]).read_text()
 cpu='\n'.join(method(process,s) for s in ['void R_ApplyImageDownsizePolicy(', 'int R_ImageDownsizePolicyMipSkip(', 'bool R_ResolveImageReduction(', 'bool R_ImageReductionIsExact(', 'byte *R_ResampleTexture('])
 cpu+='\n'+process[process.index('float mip_gammaTable[256]'):process.index('/*',process.index('float mip_gammaTable[256]'))]
 cpu+='\n'+'\n'.join(method(process,s) for s in ['byte * R_MipMapWithGamma(', 'byte * R_MipMap(', 'void R_ApplyFilterNeutralAlpha('])
 cpu+='\n'+'\n'.join(method(load,s) for s in ['static bool R_ImageUsageUsesGammaMips(', 'static int R_CountExactHalvings(', 'static byte *R_ShrinkLoadedImageData(', 'static void R_DownsizeLoadedImageData(', 'static bool R_DownsizeLoadedCubeImageData('] if s not in ['static void R_DownsizeLoadedImageData(','static bool R_DownsizeLoadedCubeImageData('])
 # Skip top-of-file forward declarations when extracting the two reduction bodies.
 for sig in ['static void R_DownsizeLoadedImageData(', 'static bool R_DownsizeLoadedCubeImageData(']:cpu+='\n'+method(load[load.index('static byte *R_ShrinkLoadedImageData('):],sig)
 dds=files[files.index('static ID_INLINE uint32 R_ReadLittleUInt32('):files.index('static bool R_ReadDDSFileInfoUncached(')]
 dds+='\n'+method(files,'bool R_LoadPrecompressedDDS(').replace('const imageFileContent_t* expected )','const imageFileContent_t* expected = nullptr )')+'\n'+method(files,'bool R_LoadCubeImages(')
 assembly='\n'.join(method(binary,s) for s in ['void idBinaryImage::Clear(', 'static void R_PadRGBAImageTo4x4Blocks(', 'static void PadImageTo4x4(', 'void idBinaryImage::Load2DFromOwnedCompressedData(', 'bool idBinaryImage::Load2DFromMemory(', 'bool idBinaryImage::LoadCubeFromMemory('])
 actual=method(load,'void idImage::ActuallyLoadImage(');assert 'consumedLoad.Reduction(consumedReduction);' in actual
 assert 'if ( !loadedPrecompressedDDS && exactDecodedReduction )' in actual
 assert 'R_ApplyImageDownsizePolicy( precompressedDownsizePolicy' not in actual
 assert 'if (!im.Load2DFromMemory(' in actual and 'if (!im.Load2DFromMemory(' in method(load,'void idImage::GenerateImage(')
 assert 'if (!packedImage.Load2DFromMemory(' in method((ROOT/'src/renderer/RenderWorld_lightgrid.cpp').read_text(),'static bool LightGrid_WritePackImagePayload(')
 assert 'if (!im.LoadCubeFromMemory(' in actual and 'if (!im.LoadCubeFromMemory(' in method(load,'void idImage::GenerateCubeImage(')
 assert '_name += "r2";' in method(load,'void idImage::GetGeneratedName(')
 compiler=a.compiler or shutil.which('clang++') or shutil.which('g++');assert compiler
 out=Path(tempfile.mkdtemp(prefix='image-reduction-',dir=ROOT/'.tmp'));env=dict(os.environ,TEMP=str(out),TMP=str(out),TMPDIR=str(out));
 if a.sanitize:env["ASAN_OPTIONS"]="symbolize=0"
 (out/'RendererConsumedPolicy.h').write_text((ROOT/names[0]).read_text().replace('../imagetools/ImageContentIdentity.h','ImageContentIdentity.h'))
 shutil.copyfile(ROOT/'src/imagetools/ImageContentIdentity.h',out/'ImageContentIdentity.h')
 code=SUPPORT+cpu+assembly+dds+MAIN;cases=[('actual',code,False)]
 if a.mutations:
  for tag,old,new in [('impossible-authored-levels','if (authoredLevels > maxLevels) return false;','if (false) return false;'),('dds-target-twice','result.requestedWidth = width; result.requestedHeight = height;','result.requestedWidth = Max(1,width>>1); result.requestedHeight = Max(1,height>>1);'),('dds-unreached-exact','? IR_EXACT : IR_INSUFFICIENT_MIPS','? IR_EXACT : IR_EXACT'),('cube-partial-success','if (!candidates[i]) return false;','if (!candidates[i]) break;'),('cube-size-early','byte *candidates[6]{};','size = scaledSize; byte *candidates[6]{};'),('mip-leak','if (value) Mem_Free(value);','(void)value;'),('cube-partial-binary','if (!complete) image.Clear();','(void)complete;'),('resample-hidden-clamp','const size_t outputBytes =','if (outwidth > 4096) outwidth = 4096; const size_t outputBytes ='),('resample-narrow-numerator','(uint64_t(inwidth) << 16)','uint64_t(inwidth * 65536)'),('dds-output-before-read','if ( cname == NULL','if (reduction) *reduction = {}; if ( cname == NULL'),('cube-null-face',' || (pics && !pics[i])',''),('2d-mip-null','if (!shrunk) return false;','if (false) return false;'),('2d-owned-leak','if (owned) Mem_Free((void*)pic);','(void)owned;'),('2d-publish-early','fileData.format = textureFormat = FMT_RGBA8;','fileData.format = textureFormat = FMT_RGBA8; textureFormatOutput = textureFormat;')]:
   assert old in code,tag;cases.append((tag,code.replace(old,new,1),True))
 records=[];status='failed'
 try:
  for tag,source,rejected in cases:
   cpp=out/(tag+'.cpp');cpp.write_text(source);exe=out/(tag+'.exe')
   command=[compiler,'-std=c++20','-I',str(out),str(cpp),str(ROOT/'src/imagetools/ImageContentIdentity.cpp'),str(ROOT/'src/idlib/CryptoHash.cpp'),'-o',str(exe)]
   if a.sanitize:command+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','-no-pie']
   if Path(compiler).stem.lower() in ['cl','clang-cl']:command=[compiler,'/nologo','/std:c++20','/EHsc','/MTd','/D_DEBUG','/Zc:strictStrings-','/I'+str(out),str(cpp),str(ROOT/'src/imagetools/ImageContentIdentity.cpp'),str(ROOT/'src/idlib/CryptoHash.cpp'),'/Fe:'+str(exe),'/Fo:'+str(out)+os.sep]
   for stage,cmd in [('compile',command),('run',[str(exe)])]:
    run=subprocess.run(cmd,cwd=out,env=env,capture_output=True,text=True,timeout=120);log=out/f'{tag}-{stage}.log';log.write_text(run.stdout+run.stderr);records.append({'case':tag,'stage':stage,'command':cmd,'exit_code':run.returncode,'expected_rejection':rejected and stage=='run','log':str(log),'sha256':hashlib.sha256(log.read_bytes()).hexdigest()})
    if (stage=='compile' and run.returncode) or (stage=='run' and bool(run.returncode)!=rejected):raise RuntimeError(str(log))
   print(tag, 'rejected' if rejected else run.stdout.strip())
  status='passed'
 finally:
  after={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in names}
  if after!=before:status='source_changed'
  (out/'result.json').write_text(json.dumps({'status':status,'files_before':before,'files_after':after,'records':records,'generated':{str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in out.iterdir() if x.suffix in ['.cpp','.h','.exe']},'limitations':__doc__},indent=2)+'\n');print(out/'result.json')
 if status != 'passed':raise SystemExit(1)
if __name__=='__main__':main()
