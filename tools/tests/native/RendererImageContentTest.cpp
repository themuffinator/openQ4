// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// Included after actual loader/identity/reconstruction methods by the runner.
static std::vector<byte> Snapshot(const idBinaryImage& image) {
    std::vector<byte> value;
    auto append=[&](const void* p,size_t n){const auto* b=(const byte*)p;value.insert(value.end(),b,b+n);};
    append(&image.fileData,sizeof(image.fileData));append(&image.loadedFileBytes,sizeof(image.loadedFileBytes));
    append(&image.fileContent.kind,sizeof(image.fileContent.kind));append(&image.fileContent.bytes,sizeof(image.fileContent.bytes));
    for(const auto& mip:image.images){append((const bimageImage_t*)&mip,sizeof(bimageImage_t));append(mip.data,mip.dataSize);}
    append(image.fileContent.qpath,sizeof(image.fileContent.qpath));append(image.fileContent.digest.bytes,32);
    return value;
}
static imagePortableContent_t Descriptor(const idBinaryImage& image,const imageReductionResult_t& reduction={},const imageDownsizePolicy_t& policy={}) {
    imagePortableContent_t value;value.version=1;value.file=image.GetFileContent();TEST(image.GetContentIdentity(value.binary));value.usage=TD_BUMP;
    if(value.file.kind==IFC_DIRECT_DDS){value.scope=IPC_DIRECT_SOURCE;value.reduction=reduction;value.resolved=policy;value.mipmaps=true;}
    else value.scope=IPC_CACHE_PIXELS_ONLY;
    return value;
}
static void PristineFailure(const imagePortableContent_t& wanted,idBinaryImage& output) {
    const auto name=output.name;const auto old=Snapshot(output);const auto* storage=output.loadedFileData;const auto allocationsBefore=allocations.size();
    TEST(!R_ReconstructImageContent(wanted,output));TEST(Snapshot(output)==old);TEST(output.loadedFileData==storage&&output.name==name);
    TEST(allocations.size()==allocationsBefore&&fs.open==0);
}
static void PutBig(std::vector<byte>& bytes,uint64_t value,int size=4){for(int i=size-1;i>=0;--i)bytes.push_back(byte(value>>(i*8)));}
static std::vector<byte> Bimage(int sides,bool reverse=false) {
    std::vector<byte> data;PutBig(data,987,8);PutBig(data,BIMAGE_MAGIC);
    for(int n:{int(sides==6?TT_CUBIC:TT_2D),int(FMT_RGBA8),int(CFM_DEFAULT),8,8,4})PutBig(data,n);
    for(int record=0;record<sides*4;++record){int index=reverse?sides*4-record-1:record;int side=index/4,level=index%4,size=8>>level;
        for(int n:{level,side,size,size,size*size*4})PutBig(data,n);
        data.insert(data.end(),size*size*4,byte(side*20+level+7));
    }return data;
}
static void PathsAndDigests() {
    for(const char* p:{"a.dds","textures/ABC x.dds","generated/a#__pic.bimage"})TEST(R_ImageContentPathValid(p));
    for(const char* p:{"","/a","../a","a/../b","a/./b","a//b","a/","a\\b","C:a","a\n","./a","a/.."})TEST(!R_ImageContentPathValid(p));
    char full[IMAGE_CONTENT_PATH_BYTES];memset(full,'a',sizeof(full));TEST(!R_ImageContentPathValid(full));
    byte bytes[3]{'a','b','c'};imageFileContent_t value;TEST(R_MakeImageFileContent(IFC_DIRECT_DDS,"a.dds",bytes,3,value));
    const byte known[]{0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    TEST(memcmp(value.digest.bytes,known,32)==0);const auto old=value;
    TEST(!R_MakeImageFileContent(IFC_UNOBSERVED,"a.dds",bytes,3,value));TEST(R_ImageFileContentEqual(value,old));
    TEST(!R_MakeImageFileContent(IFC_DIRECT_DDS,"a.dds",bytes,IMAGE_CONTENT_MAX_BYTES+1,value));TEST(R_ImageFileContentEqual(value,old));
    TEST(!R_MakeImageFileContent(IFC_DIRECT_DDS,"a.dds",nullptr,3,value));TEST(R_ImageFileContentEqual(value,old));
    // Fixed canonical LE vector computed independently by Python hashlib/struct.
    imageBinaryContent_t h;h.textureType=1;h.format=1;h.width=h.height=h.levels=h.layers=1;
    byte pixel[]{0x11,0x22,0x33,0x44};imageContentMipView_t mip{0,0,1,1,4,pixel};imageBinaryContent_t binary;
    TEST(R_MakeImageBinaryContent(h,&mip,1,binary));const byte canonical[]{0xc0,0x59,0x37,0x7f,0x97,0x56,0xd4,0x06,0x2f,0x27,0x98,0x54,0x64,0x49,0x18,0x14,0x37,0x5f,0x05,0x5b,0xd6,0x17,0x34,0x20,0x01,0xf6,0x36,0xdf,0xbe,0x25,0xdb,0x17};
    TEST(memcmp(binary.digest.bytes,canonical,32)==0&&binary.payloadBytes==4);
    const auto saved=binary;
    for(int mode=0;mode<15;++mode){auto bad=h;auto m=mip;size_t count=1;const imageContentMipView_t* ptr=&m;
        if(mode==0)bad.width=0;if(mode==1)bad.height=32769;if(mode==2)bad.levels=33;if(mode==3)bad.layers=2;
        if(mode==4)m.level=-1;if(mode==5)m.layer=1;if(mode==6)m.width=2;if(mode==7)m.height=0;
        if(mode==8)m.bytes=0;if(mode==9)m.data=nullptr;if(mode==10)ptr=nullptr;if(mode==11)count=0;
        if(mode==12)count=SIZE_MAX;if(mode==13)m.bytes=-1;if(mode==14)bad.layers=6;
        TEST(!R_MakeImageBinaryContent(bad,ptr,count,binary));TEST(R_ImageBinaryContentEqual(saved,binary));
    }
    imageContentMipView_t repeated[]{mip,mip};h.levels=2;
    TEST(!R_MakeImageBinaryContent(h,repeated,2,binary));TEST(R_ImageBinaryContentEqual(saved,binary));
    // Builders stage output before publication even if header/output or qpath alias.
    h=saved;TEST(R_MakeImageBinaryContent(h,&mip,1,h));TEST(R_ImageBinaryContentEqual(h,saved));
    TEST(R_MakeImageFileContent(IFC_DIRECT_DDS,value.qpath,bytes,3,value));TEST(R_ImageFileContentEqual(value,old));

}
static void Direct() {
    for(int format=0;format<4;++format)for(int shift:{0,1,3}){
        fs.file.bytes=DDS(16,8,5,format);idBinaryImage live("live"),restored("restored");imageReductionResult_t reduction;imageDownsizePolicy_t policy{0,shift,1};
        TEST(R_LoadPrecompressedDDS("textures/a.dds",live,nullptr,TD_BUMP,policy,true,&reduction,nullptr));
        auto wanted=Descriptor(live,reduction,policy);TEST(wanted.scope==IPC_DIRECT_SOURCE&&wanted.file.bytes==fs.file.bytes.size());
        TEST(R_ReconstructImageContent(wanted,restored));TEST(Snapshot(restored)==Snapshot(live));
        const auto bytes=fs.file.bytes;
        fs.file.bytes.back()^=1;PristineFailure(wanted,restored);
        auto old=Snapshot(restored);imageReductionResult_t untouched;untouched.firstLevel=999;
        TEST(!R_LoadPrecompressedDDS(wanted.file.qpath,restored,nullptr,TD_BUMP,policy,true,&untouched,&wanted.file));
        TEST(Snapshot(restored)==old&&untouched.firstLevel==999);fs.file.bytes=bytes;
        fs.file.bytes.pop_back();PristineFailure(wanted,restored);fs.file.bytes=bytes;
        fs.file.shortRead=true;PristineFailure(wanted,restored);fs.file.shortRead=false;
        fs.missing=true;PristineFailure(wanted,restored);fs.missing=false;
        for(bool throwing:{false,true}){throwAllocation=throwing;failAt=allocCalls+1;PristineFailure(wanted,restored);failAt=0;throwAllocation=false;}
        for(int fail=1;fail<=3;++fail){failListAt=listCalls+fail;PristineFailure(wanted,restored);failListAt=0;}
        imageBinaryContent_t different;live.images.back().data[0]^=1;TEST(live.GetContentIdentity(different));
        TEST(!R_ImageBinaryContentEqual(wanted.binary,different));live.images.back().data[0]^=1;
        auto bad=wanted;++bad.binary.width;PristineFailure(bad,restored);
        bad=wanted;bad.binary.digest.bytes[0]^=1;PristineFailure(bad,restored);
        bad=wanted;++bad.reduction.firstLevel;PristineFailure(bad,restored);
        bad=wanted;bad.scope=IPC_CACHE_PIXELS_ONLY;PristineFailure(bad,restored);
        bad=wanted;bad.version=2;int opens=fs.opens;PristineFailure(bad,restored);TEST(fs.opens==opens);
        // The actual Read copies once. Subsequent filesystem changes cannot
        // relabel the owned bytes; a later reconstruction detects that change.
        onRead=[&]{fs.file.bytes.back()^=3;};TEST(R_ReconstructImageContent(wanted,restored));onRead={};
        PristineFailure(wanted,restored);fs.file.bytes=bytes;
        // Close callbacks also precede publication; callers cannot rewrite the
        // borrowed expected descriptor to substitute a different output.
        auto mutableRequest=wanted;onClose=[&]{mutableRequest.binary.digest.bytes[0]^=1;};
        TEST(R_ReconstructImageContent(mutableRequest,restored));onClose={};TEST(Snapshot(restored)==Snapshot(live));
    }
}
static void Cached() {
    for(int sides:{1,6}){
        fs.file.bytes=Bimage(sides);imageFileContent_t source;
        TEST(R_MakeImageFileContent(IFC_OBSERVED_BIMAGE,"generated/cache.bimage",fs.file.bytes.data(),fs.file.bytes.size(),source));
        idBinaryImage live("cached"),restored("output");
        for(bool compact:{false,true}) {
            idBinaryImage observed("observed");
            TEST((compact?observed.LoadFromCompactGeneratedFileUnchecked():observed.LoadFromGeneratedFileUnchecked())==73);
            TEST(R_ImageFileContentValid(observed.GetFileContent()));TEST(fs.lastPath==observed.GetFileContent().qpath);
            imageFileContent_t raw;TEST(R_MakeImageFileContent(IFC_OBSERVED_BIMAGE,fs.lastPath.c_str(),fs.file.bytes.data(),fs.file.bytes.size(),raw));
            TEST(R_ImageFileContentEqual(raw,observed.GetFileContent()));
        }
        TEST(live.LoadExactContentFile(source));
        auto wanted=Descriptor(live);TEST(wanted.scope==IPC_CACHE_PIXELS_ONLY);
        TEST(R_ReconstructImageContent(wanted,restored));TEST(Snapshot(restored)==Snapshot(live));
        auto bytes=fs.file.bytes;
        // File record ordering is separate from canonical admitted output.
        fs.file.bytes=Bimage(sides,true);imageFileContent_t reversed;
        TEST(R_MakeImageFileContent(IFC_OBSERVED_BIMAGE,"generated/cache.bimage",fs.file.bytes.data(),fs.file.bytes.size(),reversed));
        idBinaryImage reordered;TEST(reordered.LoadExactContentFile(reversed));imageBinaryContent_t reorderedDigest;
        TEST(reordered.GetContentIdentity(reorderedDigest));TEST(R_ImageBinaryContentEqual(wanted.binary,reorderedDigest));
        PristineFailure(wanted,restored);fs.file.bytes=bytes;
        // Same qpath and timestamp, changed pixels or a malformed mip header.
        for(int offset:{0,int(bytes.size())-1,int(sizeof(bimageFile_t))+3,int(sizeof(bimageFile_t))+15}){
            fs.file.bytes[offset]^=1;int parses=swapCalls;PristineFailure(wanted,restored);TEST(swapCalls==parses);fs.file.bytes=bytes;
        }
        for(size_t size=0;size<bytes.size();++size){fs.file.bytes.resize(size);PristineFailure(wanted,restored);fs.file.bytes=bytes;}
        for(bool throwing:{false,true}){throwAllocation=throwing;failAt=allocCalls+1;PristineFailure(wanted,restored);failAt=0;throwAllocation=false;}
        failListAt=listCalls+1;PristineFailure(wanted,restored);failListAt=0;
        auto bad=wanted;bad.resolved.mipShift=1;int opens=fs.opens;PristineFailure(bad,restored);TEST(fs.opens==opens);
        bad=wanted;bad.reduction.status=IR_EXACT;PristineFailure(bad,restored);
        bad=wanted;bad.scope=IPC_DIRECT_SOURCE;PristineFailure(bad,restored);
        bad=wanted;bad.binary.digest.bytes[31]^=1;PristineFailure(bad,restored);
        // A digest for malformed bytes is not permission to accept bad layout.
        fs.file.bytes[sizeof(bimageFile_t)+3]=99;
        TEST(R_MakeImageFileContent(IFC_OBSERVED_BIMAGE,"generated/cache.bimage",fs.file.bytes.data(),fs.file.bytes.size(),bad.file));
        bad=wanted;TEST(R_MakeImageFileContent(IFC_OBSERVED_BIMAGE,"generated/cache.bimage",fs.file.bytes.data(),fs.file.bytes.size(),bad.file));
        PristineFailure(bad,restored);fs.file.bytes=bytes;
        live.Clear();imageBinaryContent_t untouched;untouched.width=993;TEST(!live.GetContentIdentity(untouched)&&untouched.width==993);
        TEST(!R_ImageFileContentValid(live.GetFileContent()));
    }
}
int main(){try{PathsAndDigests();Direct();Cached();TEST(fs.open==0&&allocations.empty());std::printf("RendererImageContentTest PASS %d checks\n",checks);return 0;}catch(const std::exception&e){std::fprintf(stderr,"FAIL %s (%d checks)\n",e.what(),checks);return 1;}}
