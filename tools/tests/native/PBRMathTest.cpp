// Copyright (C) 2026 DarkMatter Productions
// Execute the same scalar kernel embedded in GL and included by Vulkan.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include "../../../src/renderer/PBRMath.h"
#include "../../../src/renderer/PBREnvironment.h"
#include "../../../src/imagetools/SRGB.h"

namespace kernel = openq4PBRMath;

static void Require(bool condition, const char *message) {
    if (!condition) { std::fprintf(stderr, "PBRMathTest: %s\n", message); std::exit(1); }
}

static double RelativeError(double a, double b) {
    return std::fabs(a - b) / std::fmax(std::fabs(b), 1e-20);
}

// Independent double-precision reference: Smith's correlated lambda form.
static double ReferenceVisibility(double v, double l, double roughness) {
    const double a2 = std::pow(roughness, 4.0);
    const double lambdaV = (std::sqrt(1.0 + a2 * (1.0 - v * v) / (v * v)) - 1.0) / 2.0;
    const double lambdaL = (std::sqrt(1.0 + a2 * (1.0 - l * l) / (l * l)) - 1.0) / 2.0;
    return 1.0 / (4.0 * v * l * (1.0 + lambdaV + lambdaL));
}

static void EnvironmentTests() {
    using namespace openq4PBR;
    // Independent numeric decoding covers every nonnegative finite binary16
    // value, including subnormals and the mantissa/exponent carry boundaries.
    for (unsigned int half = 0; half <= 0x7bff; ++half) {
        const int exponent = half >> 10, mantissa = half & 1023;
        const float value = exponent == 0 ? std::ldexp(float(mantissa), -24)
            : std::ldexp(1.0f + float(mantissa)/1024.0f, exponent-15);
        Require(RadianceHalf(value) == half, "finite half-float round trip");
        if (half < 0x7bff) {
            const unsigned int next = half + 1;
            const float nextValue = (next >> 10) == 0 ? std::ldexp(float(next & 1023), -24)
                : std::ldexp(1.0f + float(next & 1023)/1024.0f, int(next >> 10)-15);
            Require(RadianceHalf((value + nextValue)*0.5f) == (half & 1 ? next : half),
                "half-float midpoint ties to even");
        }
    }
    Require(RadianceHalf(-1.0f) == 0 && RadianceHalf(INFINITY) == 0
        && RadianceHalf(NAN) == 0 && RadianceHalf(1e10f) == 0x7bff,
        "invalid radiance and finite HDR saturation");
    Cube cube;
    cube.size = 8;
    for (auto &face : cube.faces) { face.assign(64, {0.25f, 0.5f, 2.0f}); }
    for (int face = 0; face < 6; ++face) for (float r : {0.0f, 0.25f, 0.5f, 1.0f}) {
        const auto filtered = PrefilterFace(cube, face, 8, r);
        for (int pixel = 0; pixel < 64; ++pixel) {
            Require(std::fabs(filtered[pixel*4] - 0.25f) < 1e-5f, "constant-radiance specular furnace");
            Require(std::fabs(filtered[pixel*4+2] - 2.0f) < 1e-5f, "HDR probe must not clamp");
        }
    }
    const auto diffuse = DiffuseIrradiance(cube, 8);
    for (int pixel = 0; pixel < 64; ++pixel) { Require(std::fabs(diffuse[pixel*4+1] - 0.5f) < 1e-5f, "diffuse irradiance normalization"); }
    for (int face = 0; face < 6; ++face) for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) {
        const Vector d = FaceDirection(face, 2.0f*(x+0.5f)/8-1, 2.0f*(y+0.5f)/8-1);
        cube.faces[face][y*8+x] = (d + Vector{1,1,1})*0.5f;
    }
    for (Vector edge : {Vector{1,1,0.3f}, Vector{1,0.3f,1}, Vector{0.3f,1,-1}}) {
        const Vector a = cube(edge*1.001f + Vector{0.001f,0,0});
        const Vector b = cube(edge + Vector{0,0.001f,0});
        Require(Dot(a-b, a-b) < 0.001f, "cube seams cross into neighbouring faces");
    }
    const auto impulse = [](Vector d) -> Vector { const float x = d.z > 0.99f ? 1.0f : 0.0f; return {x,x,x}; };
    const auto sharp = PrefilterFace(impulse, 4, 9, 0);
    const auto rough = PrefilterFace(impulse, 4, 9, 0.8f, 1024);
    Require(sharp[40*4] == 1 && rough[40*4] < 0.1f && rough[40*4] > 0.001f, "roughness filters the source highlight");
    Require(rough[38*4] > sharp[38*4], "roughness broadens the highlight");
    // The parallel tile builder (REN-06) returns exactly the serial results,
    // whatever the worker count; the gradient cube above is the source.
    for (unsigned workers : {1u, 3u, 8u}) {
        const PrefilteredTiles tiles = PrefilterTiles(cube, 16, 4, 8, workers);
        Require(tiles.specular.size() == 30, "one tile per mip and face");
        for (int level = 0; level <= 4; ++level) for (int face = 0; face < 6; ++face) {
            Require(tiles.specular[level*6+face] == PrefilterFace(cube, face, 16 >> level, level / 4.0f),
                "parallel prefilter tiles match the serial filter exactly");
        }
        Require(tiles.diffuse == DiffuseIrradiance(cube, 8), "parallel diffuse tile matches exactly");
    }
    // The background job (REN-06) owns a copy of its source and publishes the
    // same tiles; a cancelled job leaves nothing behind and the job is reusable.
    {
        const PrefilteredTiles serial = PrefilterTiles(cube, 16, 4, 8, 1);
        AsyncPrefilter job;
        Require(!job.Running() && !job.Ready(), "an idle job is neither running nor ready");
        Cube copy = cube;
        Require(job.Start(std::move(copy), 16, 4, 8, 3), "background prefilter starts");
        Require(job.Running(), "a started job is running until taken");
        const PrefilteredTiles async = job.Take();
        Require(!job.Running() && !job.Ready(), "a taken job is idle");
        Require(async.specular == serial.specular && async.diffuse == serial.diffuse,
            "background prefilter tiles match the synchronous filter exactly");
        Require(job.Start(Cube(cube), 16, 4, 8, 2), "a finished job can be reused");
        job.Cancel();
        Require(!job.Running(), "a cancelled job is idle");
        Require(job.Start(Cube(cube), 16, 4, 8, 2), "a cancelled job can be reused");
        while (!job.Ready()) { std::this_thread::yield(); }
        Require(job.Take().diffuse == serial.diffuse, "a reused job publishes its own source");
        // Packed jobs hand over exactly the RGBA16F patterns of the float tiles.
        Require(job.Start(Cube(cube), 16, 4, 8, 2, true), "a packing job starts");
        const PrefilteredTiles packed = job.Take();
        Require(packed.specular.empty() && packed.diffuse.empty() && packed.specularHalf.size() == serial.specular.size(),
            "a packed job releases its float tiles");
        for (size_t i = 0; i < serial.specular.size(); ++i) {
            Require(packed.specularHalf[i] == HalfTile(serial.specular[i]), "packed specular tiles match RadianceHalf");
        }
        Require(packed.diffuseHalf == HalfTile(serial.diffuse), "packed diffuse tile matches RadianceHalf");
        // A producer runs on the job: one that rejects its source leaves no tiles.
        Require(job.StartFrom([](Cube &) { return false; }, 16, 4, 8, 2, true), "a rejecting producer starts");
        const PrefilteredTiles rejected = job.Take();
        Require(job.Failed() && rejected.specularHalf.empty() && rejected.specular.empty(),
            "a rejected source publishes nothing");
        Require(job.StartFrom([&cube](Cube &out) { out = cube; return true; }, 16, 4, 8, 2),
            "a producing job starts");
        const PrefilteredTiles produced = job.Take();
        Require(!job.Failed() && produced.specular == serial.specular && produced.diffuse == serial.diffuse,
            "a produced source filters exactly like the synchronous path");
    }
    const auto smoothBRDF = IntegrateBRDF(1, 0.045f, 4096);
    Require(smoothBRDF[0] > 0.99f && smoothBRDF[1] < 0.001f, "split-sum mirror reference");
    const auto roughBRDF = IntegrateBRDF(1, 1, 4096);
    Require(std::fabs(roughBRDF[0] + roughBRDF[1] - (1-std::log(2.0))) < 0.001, "split-sum rough furnace reference");
}

// The fitted directional albedo must stand in for the integrated table where
// a pass cannot bind it, and compensation must restore a white furnace without
// letting any F0 gain energy.
static void EnergyCompensationTests() {
    using namespace openq4PBR;
    double worstInverse = 0;
    for (int ri = 0; ri <= 20; ++ri) {
        const float r = 0.045f + (1.0f - 0.045f) * ri / 20.0f;
        for (int vi = 0; vi <= 18; ++vi) {
            const float v = 0.1f + 0.9f * vi / 18.0f;
            const auto ab = IntegrateBRDF(v, r, 4096);
            const double reference = double(ab[0]) + ab[1];
            const float fitted = kernel::PBRSpecularAlbedo(v, r);
            Require(fitted >= 0.3f && fitted <= 1.0f, "fitted directional albedo range");
            worstInverse = std::fmax(worstInverse, std::fabs(reference / fitted - 1.0));
            const double white = reference * kernel::PBREnergyCompensation(1.0f, fitted);
            Require(white > 0.96 && white < 1.04, "compensated white furnace");
            for (float f0 : {0.0f, 0.04f, 0.25f, 0.5f, 0.75f, 1.0f}) {
                const double single = f0 * ab[0] + ab[1];
                const double compensated = single * kernel::PBREnergyCompensation(f0, fitted);
                Require(compensated >= single - 1e-7 && compensated <= 1.04, "compensation adds bounded energy");
            }
        }
    }
    Require(worstInverse < 0.035, "fitted directional albedo accuracy");
    Require(kernel::PBREnergyCompensation(0.0f, 0.3f) == 1.0f, "no compensation without F0");
    Require(kernel::PBRSpecularAlbedo(0.0f, 0.5f) == kernel::PBRSpecularAlbedo(0.1f, 0.5f), "grazing view clamp");
    Require(kernel::PBRSpecularAlbedo(1.0f, 0.0f) == kernel::PBRSpecularAlbedo(1.0f, 0.045f), "roughness floor");
}

// A per-light pass binds no split-sum table: the fitted bias must stand in
// for it off the fit grid, and a uniform environment (an ambient light) must
// keep a white furnace while F0 only ever adds reflection.
static void UniformEnvironmentTests() {
    using namespace openq4PBR;
    double worstBias = 0, worstDielectric = 0;
    for (int ri = 0; ri < 23; ++ri) {
        const float r = 0.045f + (1.0f - 0.045f) * (ri + 0.37f) / 23.0f;
        for (int vi = 0; vi < 19; ++vi) {
            const float v = 0.1f + 0.9f * (vi + 0.41f) / 19.0f;
            const auto ab = IntegrateBRDF(v, r, 4096);
            const float albedo = kernel::PBRSpecularAlbedo(v, r);
            const float bias = kernel::PBRSpecularBias(v, r);
            Require(bias >= 0.0f && bias <= albedo, "split-sum bias within the directional albedo");
            worstBias = std::fmax(worstBias, std::fabs(bias - ab[1]));
            const double dielectric = 0.04 * (albedo - bias) + bias;
            worstDielectric = std::fmax(worstDielectric, std::fabs(dielectric - (0.04 * ab[0] + ab[1])));
            Require(std::fabs(kernel::PBRUniformEnvironmentSpecular(1.0f, v, r) - 1.0f) < 1e-5f,
                "a white conductor reflects all of a uniform environment");
            float previous = 0.0f;
            for (float f0 : {0.0f, 0.04f, 0.25f, 0.5f, 0.75f, 1.0f}) {
                const float reflected = kernel::PBRUniformEnvironmentSpecular(f0, v, r);
                Require(reflected >= previous - 1e-6f && reflected <= 1.0f + 1e-5f,
                    "uniform reflection grows with F0 and never exceeds one");
                previous = reflected;
            }
        }
    }
    Require(worstBias < 0.012, "fitted split-sum bias accuracy");
    Require(worstDielectric < 0.012, "dielectric uniform reflection accuracy");
}

// The extended transfer pair must equal the clamped one on [0, 1], continue
// smoothly above white, and the classic light calibration must reproduce a
// white classic surface's display value at normal incidence.
static void ClassicCalibrationTests() {
    for (int i = 0; i <= 255; ++i) {
        const float x = i / 255.0f;
        Require(kernel::PBRSRGBToLinearExtended(x) == kernel::PBRSRGBToLinear(x), "extended decode matches on [0,1]");
        Require(kernel::PBRLinearToSRGBExtended(x) == kernel::PBRLinearToSRGB(x), "extended encode matches on [0,1]");
    }
    float previous = 0.0f;
    for (int i = 0; i <= 64; ++i) {
        const float x = i / 8.0f;
        const float linear = kernel::PBRSRGBToLinearExtended(x);
        Require(linear >= previous, "extended decode is monotonic");
        previous = linear;
        Require(std::fabs(kernel::PBRLinearToSRGBExtended(linear) - x) < 2e-5f * (1.0f + x), "extended round trip above white");
    }
    Require(kernel::PBRSRGBToLinearExtended(-1.0f) == 0.0f && kernel::PBRLinearToSRGBExtended(-1.0f) == 0.0f,
        "negative light is black");
    // White Lambertian at normal incidence: radiance = albedo / pi * E.
    for (float display : {0.05f, 0.25f, 0.5f, 0.75f, 1.0f, 2.0f}) {
        const double radiance = 1.0 / 3.141592653589793 * kernel::PBRClassicLightIrradiance(display);
        Require(std::fabs(kernel::PBRLinearToSRGBExtended(float(radiance)) - display) < 1e-4f,
            "PBR white matches the classic white under the same light");
    }
}

static void OcclusionTests() {
    for (float r : {0.045f, 0.2f, 0.5f, 0.8f, 1.0f}) {
        for (int vi = 0; vi <= 20; ++vi) {
            const float v = vi / 20.0f;
            Require(kernel::PBRSpecularOcclusion(v, 1.0f, r) == 1.0f, "unoccluded specular");
            Require(kernel::PBRSpecularOcclusion(v, 0.0f, r) == 0.0f, "fully occluded specular");
            float previous = 0.0f;
            for (int ai = 0; ai <= 20; ++ai) {
                const float ao = ai / 20.0f;
                const float occlusion = kernel::PBRSpecularOcclusion(v, ao, r);
                Require(occlusion >= previous && occlusion <= 1.0f, "specular occlusion follows AO");
                previous = occlusion;
                if (r == 1.0f) {
                    Require(std::fabs(occlusion - ao) < 0.001f, "rough lobes follow AO");
                }
            }
        }
    }
    // Smooth lobes viewed head-on escape the cavity; grazing ones do not.
    Require(kernel::PBRSpecularOcclusion(1.0f, 0.5f, 0.045f) > 0.7f, "head-on smooth specular escapes AO");
    Require(kernel::PBRSpecularOcclusion(0.05f, 0.5f, 0.045f) < 0.5f, "grazing smooth specular is occluded");
    for (int vi = 0; vi <= 20; ++vi) {
        const float v = vi / 20.0f;
        Require(kernel::PBRMultiBounceAO(v, 0.0f) == v, "black albedo has no bounce");
        float previous = v;
        for (int ai = 0; ai <= 10; ++ai) {
            const float bounced = kernel::PBRMultiBounceAO(v, ai / 10.0f);
            Require(bounced >= previous && bounced <= 1.0f, "brighter albedo bounces more");
            previous = bounced;
        }
        Require(kernel::PBRMultiBounceAO(1.0f, vi / 20.0f) == 1.0f, "unoccluded multi-bounce");
    }
    Require(kernel::PBRMultiBounceAO(0.5f, 0.8f) > 0.6f, "multi-bounce lifts bright occluded diffuse");
    Require(kernel::PBRHorizonOcclusion(-1.0f) == 0.0f && kernel::PBRHorizonOcclusion(-0.5f) == 0.25f
        && kernel::PBRHorizonOcclusion(0.0f) == 1.0f && kernel::PBRHorizonOcclusion(1.0f) == 1.0f,
        "horizon fade below the geometric surface");
}

// Box-projected parallax: the exit distance against an independent slab
// intersection, and the box influence's margin.
static void BoxParallaxTests() {
    auto exitDistance = [](float px, float py, float pz, float rx, float ry, float rz, float ex, float ey, float ez) {
        return kernel::PBRBoxParallaxDistance(px, py, pz, rx, ry, rz, ex, ey, ez);
    };
    Require(exitDistance(0, 0, 0, 1, 0, 0, 2, 3, 4) == 2.0f, "centre exit along +x");
    Require(exitDistance(1, 0, 0, 1, 0, 0, 2, 2, 2) == 1.0f && exitDistance(1, 0, 0, -1, 0, 0, 2, 2, 2) == 3.0f,
        "exit distance runs to the face the ray points at");
    Require(exitDistance(0.5f, 0.5f, 0, 0, 0, 1, 1, 1, 1) == 1.0f, "an axis parallel to the ray never bounds it");
    Require(exitDistance(3, 0, 0, 1, 0, 0, 2, 2, 2) == -1.0f && exitDistance(0, 0, -5, 0, 0, 1, 2, 2, 2) == -1.0f,
        "a fragment outside the box has no exit");
    const float s = 1.0f / std::sqrt(2.0f);
    const float diagonal = exitDistance(0, 0, 0, s, s, 0, 1, 2, 2);
    Require(std::fabs(diagonal - std::sqrt(2.0f)) < 1e-6f, "the nearest face bounds a diagonal ray");
    // Independent reference: slab method on a sweep of points and rays.
    int checked = 0;
    for (int i = 0; i < 4096; ++i) {
        const double u = (i * 0.6180339887) - std::floor(i * 0.6180339887);
        const double v = (i * 0.7548776662) - std::floor(i * 0.7548776662);
        const double w = (i * 0.5698402910) - std::floor(i * 0.5698402910);
        const double e[3] = {64.0 + 448.0 * u, 32.0 + 224.0 * v, 96.0 + 160.0 * w};
        const double p[3] = {(2 * v - 1) * e[0] * 0.95, (2 * w - 1) * e[1] * 0.95, (2 * u - 1) * e[2] * 0.95};
        const double theta = 2.0 * 3.141592653589793 * u, z = 2.0 * w - 1.0, rr = std::sqrt(1.0 - z * z);
        const double r[3] = {rr * std::cos(theta), rr * std::sin(theta), z};
        double reference = 1e300;
        for (int axis = 0; axis < 3; ++axis) {
            if (std::fabs(r[axis]) > 1e-6) {
                reference = std::fmin(reference, ((r[axis] > 0 ? e[axis] : -e[axis]) - p[axis]) / r[axis]);
            }
        }
        const float t = exitDistance(float(p[0]), float(p[1]), float(p[2]), float(r[0]), float(r[1]), float(r[2]),
            float(e[0]), float(e[1]), float(e[2]));
        Require(t > 0 && RelativeError(t, reference) < 1e-4, "slab-method exit distance");
        // The exit point lies on the box surface.
        double face = 0;
        for (int axis = 0; axis < 3; ++axis) {
            face = std::fmax(face, std::fabs(p[axis] + t * r[axis]) / e[axis]);
        }
        Require(std::fabs(face - 1.0) < 1e-4, "the exit point lies on a face");
        ++checked;
    }
    Require(checked == 4096, "every sampled ray checked");
    // At the centre the corrected direction is the ray itself.
    const float t = exitDistance(0, 0, 0, 0.6f, 0.0f, 0.8f, 50, 50, 50);
    Require(std::fabs(t - 62.5f) < 1e-4f, "the capture point exits through its nearest face");

    Require(kernel::PBRBoxInfluence(0, 0, 0, 10, 10, 10, 0.2f) == 1.0f, "full weight inside the margin");
    Require(std::fabs(kernel::PBRBoxInfluence(9, 0, 0, 10, 10, 10, 0.2f) - 0.5f) < 1e-6f,
        "weight falls across the margin");
    Require(kernel::PBRBoxInfluence(10, 0, 0, 10, 10, 10, 0.2f) == 0.0f
        && kernel::PBRBoxInfluence(0, -12, 0, 10, 10, 10, 0.2f) == 0.0f, "no weight on or outside a face");
    Require(std::fabs(kernel::PBRBoxInfluence(0, 0, 3, 100, 100, 4, 0.5f) - 0.5f) < 1e-6f,
        "the smallest half extent sets the margin");
}

int main() {
    const double pi = 3.141592653589793;
    const float roughnesses[] = {0.045f, 0.08f, 0.2f, 0.5f, 0.8f, 1.0f};
    Require(std::fabs(kernel::PBRSRGBToLinear(0.5f) - 0.21404114f) < 1e-7f, "sRGB midpoint");
    Require(std::fabs(kernel::PBRSRGBToLinear(0.02f) - 0.02f / 12.92f) < 1e-8f, "sRGB toe");
    for (int i = 0; i <= 255; ++i) {
        const float x = i / 255.0f;
        Require(std::fabs(kernel::PBRLinearToSRGB(kernel::PBRSRGBToLinear(x)) - x) < 2e-7f, "sRGB round trip");
        Require(std::fabs(openq4SRGB::Decode(x)-kernel::PBRSRGBToLinear(x)) < 2e-7f, "CPU image/shader sRGB agreement");
    }
    // A checker mip must preserve half the light, while coverage averages
    // linearly. Gamma-correcting alpha incorrectly gives 186 instead of 128.
    const unsigned char checker[16]={0,0,0,0, 255,255,255,255, 255,255,255,255, 0,0,0,0};
    unsigned char mip[4]={};
    openq4SRGB::Downsample(checker,2,2,mip);
    Require(mip[0]==188 && mip[1]==188 && mip[2]==188 && mip[3]==128,"linear-light RGB mip and linear coverage");
    openq4SRGB::Downsample(checker,1,2,mip);
    Require(mip[0]==188 && mip[3]==128,"one-column sRGB mip");
    openq4SRGB::Downsample(checker,2,1,mip);
    Require(mip[0]==188 && mip[3]==128,"one-row sRGB mip");
    const unsigned char toe[8]={0,0,0,17, 20,20,20,239};
    openq4SRGB::Downsample(toe,2,1,mip);
    Require(mip[0]==11 && mip[3]==128,"sRGB toe differs from power-2.2 filtering");
    Require(kernel::PBRVisibilitySmithGGX(0, 1, 0) == 0, "grazing limit");
    Require(kernel::PBRVisibilitySmithGGX(1, -1, 0) == 0, "back-face light");
    // A normal map can turn the shading normal from the viewer at a
    // silhouette; direct light keeps a small positive N.V, never a black cut.
    Require(kernel::PBRShadingNoV(-0.3f) == 1.0e-4f && kernel::PBRShadingNoV(0.0f) == 1.0e-4f
        && kernel::PBRShadingNoV(0.5f) == 0.5f && kernel::PBRShadingNoV(2.0f) == 1.0f, "shading N.V clamp");
    for (float r : roughnesses) {
        const float grazing = kernel::PBRVisibilitySmithGGX(kernel::PBRShadingNoV(-0.01f), 0.5f, r);
        Require(grazing > 0.0f && std::isfinite(grazing), "clamped N.V keeps a finite specular lobe");
    }
    Require(kernel::PBRRoughness(0) == 0.045f && kernel::PBRRoughness(2) == 1, "roughness floor/range");
    for (float r : roughnesses) {
        Require(std::fabs(kernel::PBRFilteredRoughness(r, 0)-r)<1e-7f,"specular AA preserves a constant normal");
        float previous = r;
        for (float variance : {0.00001f,0.0001f,0.001f,0.01f,0.1f,10.0f}) {
            const float filtered = kernel::PBRFilteredRoughness(r,variance);
            Require(filtered>=previous && filtered<=1,"specular AA broadens without overflow");
            previous=filtered;
        }
        Require(kernel::PBRFilteredRoughness(r,0.1f)==kernel::PBRFilteredRoughness(r,10),"specular AA footprint cap");
    }

    for (float r : roughnesses) {
        const double expectedPeak = 1.0 / (pi * std::pow(double(r), 4.0));
        Require(RelativeError(kernel::PBRDistributionGGX(1, r), expectedPeak) < 1e-6, "GGX smooth peak suppressed");
        Require(kernel::PBRVisibilitySmithGGX(1, 1, r) == 0.25f, "Cook-Torrance factor of four");
        Require(kernel::PBRDistributionGGX(1, r) * 0.25f < 65504, "half-float highlight overflow");
        for (int vi = 1; vi <= 80; ++vi) {
            const float v = vi / 80.0f;
            for (int li = 1; li <= 80; ++li) {
                const float l = li / 80.0f;
                const float visibility = kernel::PBRVisibilitySmithGGX(v, l, r);
                Require(std::isfinite(visibility) && visibility > 0, "finite positive visibility");
                Require(RelativeError(visibility, ReferenceVisibility(v, l, r)) < 2e-6, "Smith correlated reference");
                Require(visibility == kernel::PBRVisibilitySmithGGX(l, v, r), "BRDF reciprocity");
            }
        }

        // A white conductor in a unit radiance furnace cannot create energy.
        // Integrate with a stratified GGX half-vector distribution, independent
        // of the kernel's D expression. This also covers very narrow lobes.
        for (float v : {0.05f, 0.2f, 0.6f, 1.0f}) {
            const double vx = std::sqrt(1.0 - double(v) * v);
            const double a2 = std::pow(double(r), 4.0);
            double energy = 0;
            const int rows = 256, columns = 256;
            for (int y = 0; y < rows; ++y) {
                const double u = (y + 0.5) / rows;
                const double hz = std::sqrt((1.0 - u) / (1.0 + (a2 - 1.0) * u));
                const double radial = std::sqrt(1.0 - hz * hz);
                for (int x = 0; x < columns; ++x) {
                    const double hx = radial * std::cos(2.0 * pi * (x + 0.5) / columns);
                    const double vh = vx * hx + v * hz;
                    const double l = 2.0 * vh * hz - v;
                    if (l > 0 && vh > 0) {
                        energy += 4.0 * l * kernel::PBRVisibilitySmithGGX(v, float(l), r) * vh / hz;
                    }
                }
            }
            energy /= rows * columns;
            Require(std::isfinite(energy) && energy > 0 && energy <= 1.005, "white furnace energy gain");
        }
    }
    EnvironmentTests();
    EnergyCompensationTests();
    UniformEnvironmentTests();
    OcclusionTests();
    ClassicCalibrationTests();
    BoxParallaxTests();
    std::puts("PBRMathTest: passed sRGB, mirror peaks, Smith reference, reciprocity, 24 white furnaces, probe convolution, HDR, seams, irradiance, split-sum integration, energy compensation, split-sum bias and uniform environments, specular/multi-bounce/horizon occlusion, extended transfer, classic light calibration and box-projected parallax");
    return 0;
}
