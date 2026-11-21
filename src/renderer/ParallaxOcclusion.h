// Copyright (C) 2026 DarkMatter Productions
// Parallax occlusion mapping (REN-11), shared by every pass that lights a
// parallax surface on OpenGL and Vulkan, so each pass traces the same ray and
// lands on the same texel. GL embeds the string below; the offline Vulkan
// shaders include this file. The backend defines OpenQ4ParallaxHeight first:
// the height in [0, 1] (white is high), sampled with explicit gradients.
//
// The height field: a texel of height h lies at z = h * p.x + p.y texture
// coordinate units above the polygon (p.x the scale, p.y the bias; an authored
// heightReference r gives p.y = -r * p.x). A ray toward the viewer leaves the
// polygon point uv along uv + v.xy / v.z * z, so the trace marches z from the
// top of the field down, finds the first layer under the surface, then
// refines the crossing with secant steps. The retail Parallaxbump single step
// is the zeroth-order version of the same field: uv + v.xy * (h * p.x + p.y).
#ifndef OPENQ4_PARALLAX_OCCLUSION_H
#define OPENQ4_PARALLAX_OCCLUSION_H

#define OPENQ4_PARALLAX_FUNCTIONS \
float OpenQ4ParallaxHeight(vec2 uv, vec2 dx, vec2 dy); \
float OpenQ4ParallaxSurface(vec2 uv, vec2 dx, vec2 dy, vec2 field) { \
    return OpenQ4ParallaxHeight(uv, dx, dy) * field.x + field.y; \
} \
/* The share of the effect left at this distance: 1 near, 0 at fade.y.     \
   Faded, the field flattens toward the polygon, so far surfaces converge  \
   on the flat material and stop paying for the ray. */ \
float OpenQ4ParallaxFade(vec4 fade, float viewDistance) { \
    return 1.0 - smoothstep(fade.x, max(fade.y, fade.x + 1.0), viewDistance); \
} \
/* uv: the untraced coordinate of the height map; view: the tangent-space \
   direction toward the viewer; p = (scale, bias, most samples, shadow);   \
   fade = (start, end). Returns (offset.xy, hit z, fade share); the offset \
   applies to every map of the surface. */ \
vec4 OpenQ4ParallaxTrace(vec2 uv, vec3 view, vec4 p, vec4 fade, float viewDistance, vec2 dx, vec2 dy) { \
    float share = OpenQ4ParallaxFade(fade, viewDistance); \
    vec2 field = vec2(p.x, p.y) * share; \
    if (share <= 0.0 || field.x <= 0.0) { \
        return vec4(0.0); \
    } \
    vec3 v = view * inversesqrt(max(dot(view, view), 1.0e-12)); \
    /* A back-facing or edge-on view would shift the texel without bound. */ \
    float vz = max(v.z, 0.02); \
    vec2 slope = v.xy / vz; \
    float top = field.x + field.y; \
    float bottom = field.y; \
    /* More layers at grazing angles, where one layer spans more texels;  \
       fewer with distance, where the field has already flattened. */ \
    float layers = mix(p.z, max(p.z * 0.25, 4.0), clamp(v.z, 0.0, 1.0)); \
    layers = max(4.0, floor(layers * mix(1.0, 0.25, clamp(viewDistance / max(fade.y, 1.0), 0.0, 1.0)) + 0.5)); \
    float layerStep = (top - bottom) / layers; \
    float zAbove = top; \
    float dAbove = top - OpenQ4ParallaxSurface(uv + slope * top, dx, dy, field); \
    float zBelow = top; \
    float dBelow = dAbove; \
    if (dAbove > 0.0) { \
        for (int i = 1; i <= 64; ++i) { \
            if (float(i) > layers) { \
                break; \
            } \
            zBelow = top - layerStep * float(i); \
            dBelow = zBelow - OpenQ4ParallaxSurface(uv + slope * zBelow, dx, dy, field); \
            if (dBelow <= 0.0) { \
                break; \
            } \
            zAbove = zBelow; \
            dAbove = dBelow; \
        } \
        if (dBelow > 0.0) { \
            /* Rounding left the last layer above the floor of the field. */ \
            zBelow = bottom; \
            dBelow = 0.0; \
        } \
        /* Secant refinement between the last layer above the surface and \
           the first below it, then one final interpolation. */ \
        for (int k = 0; k < 4; ++k) { \
            float zMid = mix(zAbove, zBelow, dAbove / max(dAbove - dBelow, 1.0e-7)); \
            float dMid = zMid - OpenQ4ParallaxSurface(uv + slope * zMid, dx, dy, field); \
            if (dMid > 0.0) { \
                zAbove = zMid; \
                dAbove = dMid; \
            } else { \
                zBelow = zMid; \
                dBelow = dMid; \
            } \
        } \
        zBelow = mix(zAbove, zBelow, dAbove / max(dAbove - dBelow, 1.0e-7)); \
    } \
    return vec4(slope * zBelow, zBelow, share); \
} \
/* The light's visibility from the traced hit: a ray toward the light, in \
   tangent space, that passes under the height field is in shadow; the     \
   deepest penetration, weighted toward the hit, darkens it smoothly. */ \
float OpenQ4ParallaxShadow(vec2 uv, vec4 hit, vec3 light, vec4 p, vec2 dx, vec2 dy) { \
    if (p.w < 0.5 || hit.w <= 0.0) { \
        return 1.0; \
    } \
    vec3 l = light * inversesqrt(max(dot(light, light), 1.0e-12)); \
    if (l.z <= 0.0) { \
        return 1.0; \
    } \
    vec2 field = vec2(p.x, p.y) * hit.w; \
    float top = field.x + field.y; \
    float rise = top - hit.z; \
    if (rise <= 0.0 || field.x <= 0.0) { \
        return 1.0; \
    } \
    vec2 slope = l.xy / max(l.z, 0.02); \
    vec2 origin = uv + hit.xy; \
    float occlusion = 0.0; \
    for (int i = 1; i <= 12; ++i) { \
        float along = rise * (float(i) - 0.5) / 12.0; \
        float above = OpenQ4ParallaxSurface(origin + slope * along, dx, dy, field) - (hit.z + along); \
        occlusion = max(occlusion, above / field.x * (1.0 - float(i) / 13.0)); \
    } \
    return 1.0 - clamp(occlusion * 4.0, 0.0, 1.0); \
}

#ifdef __cplusplus
#define OPENQ4_PARALLAX_STRING_IMPL(...) #__VA_ARGS__
#define OPENQ4_PARALLAX_STRING(...) OPENQ4_PARALLAX_STRING_IMPL(__VA_ARGS__)
static const char OPENQ4_PARALLAX_GLSL[] =
    OPENQ4_PARALLAX_STRING(OPENQ4_PARALLAX_FUNCTIONS);
#undef OPENQ4_PARALLAX_STRING
#undef OPENQ4_PARALLAX_STRING_IMPL
#else
OPENQ4_PARALLAX_FUNCTIONS
#endif

#endif
