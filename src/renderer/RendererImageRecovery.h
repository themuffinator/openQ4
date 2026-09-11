// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include "RendererConsumedPolicy.h"
#include "RendererResourceSettings.h"
#include "../imagetools/ImageRecoveryEnvelope.h"
#include <array>
#include <memory>
#include <vector>

// CPU-owned durable inventory. The codec proves only canonical structure. A
// cold preparation must check the complete current inventory, retained material
// sources, dependencies and exact source bytes before allowing a restart.
// Never write any C++ object representation or process token to disk.
namespace openq4::imageRecovery {
struct Image {
    Image()=default;
    Image(const Image&)=default;
    Image& operator=(const Image&)=default;
    std::string name{""};
    int filter=0,repeat=0,usage=0,cube=0;
    uint32_t flags=0;
    bool allowDownSize=false,resident=false;
    imagePortableContent_t content{};
};
struct Material {
    Material()=default;
    Material(const Material&)=default;
    Material& operator=(const Material&)=default;
    std::string name{""},file{""};
    int state=0,line=0;
    bool implicit=false,observed=false,ignoreHighQuality=false,makingBuild=false;
    uint32_t sourceBytes=0;
    imageContentDigest_t source{};
};
struct Dependency {std::string value{""};};
struct Data {
    unsigned direction=0;
    std::string attempt{""};
    renderImagePolicy_t policy{};
    std::array<Dependency,11> dependencies;
    std::vector<Image> images=std::vector<Image>(size_t{0});
    std::vector<Material> materials=std::vector<Material>(size_t{0});
};
class Record {
public:
    Record() noexcept=default;
    Record(Record&&) noexcept=default;
    Record& operator=(Record&&) noexcept=default;
    const Data* Get() const noexcept{return value.get();}
private:
    std::unique_ptr<const Data> value;
    friend bool Decode(std::string_view,unsigned,std::string_view,Record&,std::string&);
};
bool Encode(const Data&,std::string& raw,std::string& error);
bool Decode(std::string_view raw,unsigned direction,std::string_view attempt,Record&,std::string& error);
bool SameImageKey(const Image&,const Image&) noexcept;
bool SameMaterial(const Material&,const Material&) noexcept;
} // namespace openq4::imageRecovery
