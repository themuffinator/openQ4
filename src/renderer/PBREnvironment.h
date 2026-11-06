// Copyright (C) 2026 DarkMatter Productions
// Original, deterministic CPU integration of the shared PBR equations.
#ifndef OPENQ4_PBR_ENVIRONMENT_H
#define OPENQ4_PBR_ENVIRONMENT_H

#include "PBRMath.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <cstdint>
#include <cstring>
#include <system_error>
#include <thread>
#include <vector>

namespace openq4PBR {
using namespace openq4PBRMath;
// Nonnegative finite HDR storage, rounded to nearest with ties to even.
// The Vulkan uploader consumes native-endian IEEE binary16 bytes.
inline std::uint16_t RadianceHalf(float value) {
    if (!(value > 0.0f) || !std::isfinite(value)) return 0;
    if (value >= 65504.0f) return 0x7bff;
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    const int exponent = int((bits >> 23) & 255) - 127;
    if (exponent < -25) return 0;
    const std::uint32_t significand = (bits & 0x7fffff) | 0x800000;
    const int shift = exponent < -14 ? -exponent - 1 : 13;
    const std::uint32_t truncated = significand >> shift;
    const std::uint32_t remainder = significand & ((1u << shift) - 1);
    const std::uint32_t midpoint = 1u << (shift - 1);
    const std::uint32_t rounded = truncated
        + (remainder > midpoint || (remainder == midpoint && (truncated & 1)));
    return std::uint16_t(rounded + (exponent < -14 ? 0 : (exponent + 14) * 1024));
}
struct Vector {
    float x, y, z;
    Vector operator+(Vector b) const { return {x + b.x, y + b.y, z + b.z}; }
    Vector operator-(Vector b) const { return {x - b.x, y - b.y, z - b.z}; }
    Vector operator*(float s) const { return {x * s, y * s, z * s}; }
};
inline float Dot(Vector a, Vector b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline Vector Cross(Vector a, Vector b) { return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x}; }
inline Vector Unit(Vector v) { return v * (1.0f / std::sqrt((std::max)(Dot(v, v), 1e-20f))); }
inline Vector FaceDirection(int face, float u, float v) {
    switch (face) {
    case 0: return Unit({1, -v, -u});
    case 1: return Unit({-1, -v, u});
    case 2: return Unit({u, 1, v});
    case 3: return Unit({u, -1, -v});
    case 4: return Unit({u, -v, 1});
    default:return Unit({-u, -v, -1});
    }
}
inline int FaceCoordinates(Vector d, float &u, float &v) {
    const float x = std::fabs(d.x), y = std::fabs(d.y), z = std::fabs(d.z);
    if (x >= y && x >= z) { u = (d.x >= 0 ? -d.z : d.z) / x; v = -d.y / x; return d.x >= 0 ? 0 : 1; }
    if (y >= z) { u = d.x / y; v = (d.y >= 0 ? d.z : -d.z) / y; return d.y >= 0 ? 2 : 3; }
    u = (d.z >= 0 ? d.x : -d.x) / z; v = -d.y / z; return d.z >= 0 ? 4 : 5;
}

struct Cube {
    int size = 0;
    std::array<std::vector<Vector>, 6> faces;
    Vector Nearest(Vector direction) const {
        float u, v;
        const int face = FaceCoordinates(direction, u, v);
        const int x = (std::max)(0, (std::min)(size - 1, int((u * 0.5f + 0.5f) * size)));
        const int y = (std::max)(0, (std::min)(size - 1, int((v * 0.5f + 0.5f) * size)));
        return faces.data()[face].data()[y * size + x];
    }
    Vector Tap(int face, int x, int y) const {
        if (x >= 0 && x < size && y >= 0 && y < size) { return faces.data()[face].data()[y * size + x]; }
        // Filter across the actual neighbouring cube face, never another
        // probe's atlas cell. All convolution samples see a continuous cube.
        return Nearest(FaceDirection(face, 2.0f * (x + 0.5f) / size - 1.0f, 2.0f * (y + 0.5f) / size - 1.0f));
    }
    Vector operator()(Vector direction) const {
        float u, v;
        const int face = FaceCoordinates(direction, u, v);
        const float px = (u * 0.5f + 0.5f) * size - 0.5f, py = (v * 0.5f + 0.5f) * size - 0.5f;
        const int x = int(std::floor(px)), y = int(std::floor(py));
        const float fx = px - x, fy = py - y;
        return (Tap(face, x, y)*(1-fx) + Tap(face, x+1, y)*fx)*(1-fy)
             + (Tap(face, x, y+1)*(1-fx) + Tap(face, x+1, y+1)*fx)*fy;
    }
};

inline float RadicalInverse(std::uint32_t bits) {
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555u) << 1) | ((bits & 0xaaaaaaaau) >> 1);
    bits = ((bits & 0x33333333u) << 2) | ((bits & 0xccccccccu) >> 2);
    bits = ((bits & 0x0f0f0f0fu) << 4) | ((bits & 0xf0f0f0f0u) >> 4);
    bits = ((bits & 0x00ff00ffu) << 8) | ((bits & 0xff00ff00u) >> 8);
    return float(double(bits) / 4294967296.0);
}
inline Vector GGXHalfVector(int i, int count, float roughness) {
    const float a = roughness * roughness, a2 = a * a;
    const float u = (i + 0.5f) / count;
    const float z = std::sqrt((1-u) / (1 + (a2-1)*u));
    const float radius = std::sqrt((std::max)(0.0f, 1-z*z));
    const float phi = 6.28318530718f * RadicalInverse(i);
    return {radius * std::cos(phi), radius * std::sin(phi), z};
}
inline Vector Orient(Vector local, Vector normal) {
    const Vector up = std::fabs(normal.z) < 0.999f ? Vector{0,0,1} : Vector{1,0,0};
    const Vector tangent = Unit(Cross(up, normal));
    return tangent*local.x + Cross(normal, tangent)*local.y + normal*local.z;
}
inline void Store(std::vector<float> &rgba, int pixel, Vector value) {
    float *p = rgba.data() + pixel*4;
    p[0] = value.x; p[1] = value.y; p[2] = value.z; p[3] = 1;
}
template<class Sampler>
inline std::vector<float> PrefilterFace(const Sampler &sample, int face, int size, float roughness, int samples = 128) {
    std::vector<Vector> directions;
    if (roughness > 0) { for (int i = 0; i < samples; ++i) { directions.push_back(GGXHalfVector(i, samples, roughness)); } }
    std::vector<float> rgba(size*size*4);
    for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
        // Endpoints lie at face edges, matching the shader's centre-clamped
        // coordinates at every mip. This avoids bilinear bleed between cells.
        const Vector n = FaceDirection(face, size > 1 ? 2.0f*x/(size-1)-1 : 0, size > 1 ? 2.0f*y/(size-1)-1 : 0);
        const Vector up = std::fabs(n.z) < 0.999f ? Vector{0,0,1} : Vector{1,0,0};
        const Vector tangent = Unit(Cross(up, n)), bitangent = Cross(n, tangent);
        Vector sum{0,0,0}; float weight = 0;
        if (roughness <= 0) { sum = sample(n); weight = 1; }
        else for (int i = 0; i < samples; ++i) {
            const Vector h = directions.data()[i];
            const Vector local = h * (2*h.z) - Vector{0,0,1};
            const Vector l = tangent*local.x + bitangent*local.y + n*local.z;
            const float NoL = (std::max)(0.0f, 2*h.z*h.z-1);
            if (NoL > 0) { sum = sum + sample(l)*NoL; weight += NoL; }
        }
        Store(rgba, y*size+x, weight > 0 ? sum*(1/weight) : sample(n));
    }
    return rgba;
}
inline Vector OctahedralDirection(float x, float y) {
    Vector n{x, y, 1-std::fabs(x)-std::fabs(y)};
    if (n.z < 0) {
        n.x = (1-std::fabs(y))*(x >= 0 ? 1 : -1);
        n.y = (1-std::fabs(x))*(y >= 0 ? 1 : -1);
    }
    return Unit(n);
}
template<class Sampler>
inline std::vector<float> DiffuseIrradiance(const Sampler &sample, int size, int samples = 256) {
    std::vector<Vector> directions;
    for (int i = 0; i < samples; ++i) {
        const float radius = std::sqrt((i+0.5f)/samples), phi = 6.28318530718f*RadicalInverse(i);
        directions.push_back({radius*std::cos(phi), radius*std::sin(phi), std::sqrt(1-radius*radius)});
    }
    std::vector<float> rgba(size*size*4);
    for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
        const Vector n = OctahedralDirection(2.0f*x/(size-1)-1, 2.0f*y/(size-1)-1);
        Vector sum{0,0,0};
        for (Vector l : directions) { sum = sum + sample(Orient(l, n)); }
        // Store irradiance / pi: the cosine-weighted estimator already
        // includes Lambert's normalization when multiplied by diffuse color.
        Store(rgba, y*size+x, sum*(1.0f/samples));
    }
    return rgba;
}
// Every prefiltered (mip, face) tile of a probe, index mip * 6 + face, and its
// diffuse irradiance tile. Tiles are independent and each is exactly the
// PrefilterFace / DiffuseIrradiance result, so spreading them over worker
// threads changes only how long a probe-source cache miss takes (REN-06: about
// 0.5 s serially). The sampler must be safe to read concurrently.
struct PrefilteredTiles {
    std::vector<std::vector<float>> specular;
    std::vector<float> diffuse;
    // RGBA16F bit patterns after PackTilesHalf; the float tiles are released.
    std::vector<std::vector<std::uint16_t>> specularHalf;
    std::vector<std::uint16_t> diffuseHalf;
};
template<class Sampler>
inline PrefilteredTiles PrefilterTiles(const Sampler &sample, int faceSize, int maxMip, int diffuseSize,
        unsigned maxWorkers = 8) {
    PrefilteredTiles out;
    const int specularCount = (maxMip + 1) * 6;
    out.specular.resize(specularCount);
    std::atomic<int> next(0);
    const auto work = [&]() {
        for (int task = next++; task <= specularCount; task = next++) {
            if (task == specularCount) {
                out.diffuse = DiffuseIrradiance(sample, diffuseSize);
                continue;
            }
            const int level = task / 6;
            out.specular[task] = PrefilterFace(sample, task % 6, faceSize >> level,
                maxMip > 0 ? float(level) / maxMip : 0.0f);
        }
    };
    unsigned workers = std::thread::hardware_concurrency();
    workers = (std::max)(1u, (std::min)(workers, maxWorkers));
    std::vector<std::thread> threads;
    for (unsigned i = 1; i < workers; ++i) {
        try { threads.emplace_back(work); } catch (const std::system_error &) { break; }
    }
    work();
    for (std::thread &thread : threads) { thread.join(); }
    return out;
}
inline std::vector<std::uint16_t> HalfTile(const std::vector<float> &rgba) {
    std::vector<std::uint16_t> half(rgba.size());
    for (size_t i = 0; i < rgba.size(); ++i) { half[i] = RadianceHalf(rgba[i]); }
    return half;
}
// Converts every tile to the atlas' RGBA16F storage, so a background job leaves
// the frame thread only the upload (the drivers' own float conversion costs
// milliseconds per 256-texel tile).
inline void PackTilesHalf(PrefilteredTiles &tiles) {
    tiles.specularHalf.resize(tiles.specular.size());
    for (size_t i = 0; i < tiles.specular.size(); ++i) { tiles.specularHalf[i] = HalfTile(tiles.specular[i]); }
    tiles.diffuseHalf = HalfTile(tiles.diffuse);
    tiles.specular.clear();
    tiles.diffuse.clear();
}
// One probe source's PrefilterTiles on a background thread, so a cache miss
// publishes on a later frame instead of stalling the one that took it (REN-06:
// about 85 ms even with parallel tiles, against a 4 ms budget). The job owns
// its source, and its result equals the synchronous filter. StartFrom also
// moves producing the source (decoding the read texels) onto the thread; a
// producer that returns false leaves Failed() set and no tiles. Owners must
// Wait() before unloading the module that runs it; destruction joins as a
// last resort.
class AsyncPrefilter {
public:
    AsyncPrefilter() = default;
    AsyncPrefilter(const AsyncPrefilter &) = delete;
    AsyncPrefilter &operator=(const AsyncPrefilter &) = delete;
    ~AsyncPrefilter() { Wait(); }
    // False when no thread could be started; the caller filters synchronously.
    template<class Producer>
    bool StartFrom(Producer produce, int faceSize, int maxMip, int diffuseSize, unsigned maxWorkers,
            bool packHalf = false) {
        Wait();
        cube = Cube();
        result = PrefilteredTiles();
        failed = false;
        done.store(false, std::memory_order_relaxed);
        try {
            worker = std::thread([this, produce, faceSize, maxMip, diffuseSize, maxWorkers, packHalf]() mutable {
                if (produce(cube)) {
                    result = PrefilterTiles(cube, faceSize, maxMip, diffuseSize, maxWorkers);
                    if (packHalf) { PackTilesHalf(result); }
                } else {
                    failed = true;
                }
                done.store(true, std::memory_order_release);
            });
        } catch (const std::system_error &) {
            return false;
        }
        return true;
    }
    bool Start(Cube source, int faceSize, int maxMip, int diffuseSize, unsigned maxWorkers,
            bool packHalf = false) {
        auto owned = std::make_shared<Cube>(std::move(source));
        return StartFrom([owned](Cube &out) { out = std::move(*owned); return true; },
            faceSize, maxMip, diffuseSize, maxWorkers, packHalf);
    }
    bool Running() const { return worker.joinable(); }
    bool Ready() const { return worker.joinable() && done.load(std::memory_order_acquire); }
    void Wait() { if (worker.joinable()) { worker.join(); } }
    // After Take: the producer rejected its source, so the tiles are empty.
    bool Failed() const { return failed; }
    // Joins (blocking only when not Ready) and hands over the tiles.
    PrefilteredTiles Take() {
        Wait();
        cube = Cube();
        return std::move(result);
    }
    // Discards a job whose source or slot is no longer wanted.
    void Cancel() { Wait(); cube = Cube(); result = PrefilteredTiles(); failed = false; }
private:
    Cube cube;
    PrefilteredTiles result;
    bool failed = false;
    std::atomic<bool> done{false};
    std::thread worker;
};
inline std::array<float, 2> IntegrateBRDF(float NoV, float roughness, int samples = 256) {
    NoV = PBRClamp(NoV, 0.0001f, 1);
    roughness = PBRRoughness(roughness);
    const Vector v{std::sqrt(1-NoV*NoV), 0, NoV};
    float a = 0, b = 0;
    for (int i = 0; i < samples; ++i) {
        const Vector h = GGXHalfVector(i, samples, roughness);
        const float VoH = (std::max)(0.0f, Dot(v, h));
        const float NoL = 2*VoH*h.z-NoV;
        if (NoL <= 0) { continue; }
        const float weight = 4*NoL*PBRVisibilitySmithGGX(NoV, NoL, roughness)*VoH/h.z;
        const float fresnel = PBRFresnelWeight(VoH);
        a += (1-fresnel)*weight; b += fresnel*weight;
    }
    return {{a/samples, b/samples}};
}
inline std::vector<float> BRDFTable(int size) {
    std::vector<float> rgba(size*size*4);
    for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
        const auto ab = IntegrateBRDF(float(x)/(size-1), float(y)/(size-1));
        Store(rgba, y*size+x, {ab[0], ab[1], 0});
    }
    return rgba;
}
struct AnalyticEnvironment {
    Vector operator()(Vector d) const {
        d = Unit(d);
        const float t = PBRClamp((d.z+0.55f)/1.25f, 0, 1);
        const float up = t*t*(3-2*t);
        const Vector sky = Vector{0.025f,0.022f,0.020f}*(1-up) + Vector{0.46f,0.53f,0.68f}*up;
        const float key = std::pow((std::max)(0.0f, Dot(d, Unit({-0.35f,0.70f,0.62f}))), 48.0f);
        return sky + Vector{1.30f,1.16f,0.96f}*key;
    }
};
}
#endif
