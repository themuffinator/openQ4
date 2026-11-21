// Shadowed volumetric lighting (REN-09): the march of one light stage into
// the scattering target. The OpenGL twin is rbVolumetricInjectSource in
// src/renderer/draw_volumetric.cpp; keep the two in step.
//
// The pass draws post_fullscreen.vert's covering triangle into the target
// with a positive viewport, so gl_FragCoord.y counts rows the way the
// VK_Exec_CopyRender depth capture stores them: bottom-up, like OpenGL.
// Variants define VOL_SHADOW_PROJECTED or VOL_SHADOW_POINT.

layout(set = 0, binding = 0) uniform sampler2D uDepth;
layout(set = 1, binding = 0) uniform sampler2D uProjection;
layout(set = 2, binding = 0) uniform sampler2D uFalloff;

layout(std140, set = 6, binding = 0) uniform VolumetricBlock {
    mat4 clipToWorld;
    vec4 eye;			// xyz eye, w march offset
    vec4 forward;		// xyz view forward, w steps
    vec4 targetInfo;	// xy divisor / scene size
    vec4 params0;		// extinction, scattering, anisotropy, max distance
    vec4 lightOrigin;
    vec4 projS;
    vec4 projT;
    vec4 projQ;
    vec4 falloffS;
    vec4 lightColor;
    vec4 boundsMin;
    vec4 boundsMax;
} vol;

#if defined(VOL_SHADOW_PROJECTED) || defined(VOL_SHADOW_POINT)
layout(std140, set = 7, binding = 1) uniform VolumetricShadowBlock {
    vec4 row0[4];
    vec4 row1[4];
    vec4 row2[4];
    vec4 row3[4];
    vec4 atlasRects[4];
    vec4 splitDepths;
    vec4 depthBias;
    vec4 info;			// x cascade count, y 1 when a second block holds the
						// global casters (PCSS GLOBAL pass), zw its uv offset
    vec4 point;			// x far, y bias
    vec4 pointOrigin;	// the origin the cube was rendered from
} vshadow;
#endif
#if defined(VOL_SHADOW_PROJECTED)
layout(set = 7, binding = 2) uniform sampler2D shadowRawMap;
#elif defined(VOL_SHADOW_POINT)
layout(set = 7, binding = 2) uniform samplerCube shadowRawMap;
#endif

layout(location = 0) out vec4 outColor;

float VolumeNoise(vec2 p) {
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

float ShadowVisibility(vec3 p, float viewDepth) {
#if defined(VOL_SHADOW_PROJECTED)
    int count = int(vshadow.info.x + 0.5);
    int first = 0;
    for (int s = 0; s < 3; s++) {
        if (s + 1 < count && viewDepth >= vshadow.splitDepths[s]) {
            first = s + 1;
        }
    }
    vec4 p4 = vec4(p, 1.0);
    // The cascade of this depth first, then the coarser ones whose crop
    // still holds the point (the whole-light map is the last).
    for (int c = 0; c < 4; c++) {
        if (c < first) {
            continue;
        }
        if (c >= count) {
            break;
        }
        vec4 coord = vec4(dot(p4, vshadow.row0[c]), dot(p4, vshadow.row1[c]),
            dot(p4, vshadow.row2[c]), dot(p4, vshadow.row3[c]));
        if (coord.w <= 1.0e-6) {
            continue;
        }
        vec2 localUv = coord.xy / coord.w * 0.5 + 0.5;
        float depth = coord.z;
        if (localUv.x <= 0.0 || localUv.y <= 0.0 || localUv.x >= 1.0
                || localUv.y >= 1.0 || depth <= 0.0 || depth >= 1.0) {
            continue;
        }
        vec2 atlasUv = mix(vshadow.atlasRects[c].xy, vshadow.atlasRects[c].zw, localUv);
        float compareDepth = depth - vshadow.depthBias[c];
        float visible = compareDepth <= textureLod(shadowRawMap, atlasUv, 0.0).r ? 1.0 : 0.0;
        if (vshadow.info.y > 0.5) {
            visible *= compareDepth <= textureLod(shadowRawMap,
                atlasUv + vshadow.info.zw, 0.0).r ? 1.0 : 0.0;
        }
        return visible;
    }
    return 1.0;
#elif defined(VOL_SHADOW_POINT)
    vec3 v = p - vshadow.pointOrigin.xyz;
    float depth = length(v) / vshadow.point.x;
    if (depth <= 0.0 || depth >= 1.0) {
        return 1.0;
    }
    return depth - vshadow.point.y <= textureLod(shadowRawMap, v, 0.0).r ? 1.0 : 0.0;
#else
    return 1.0;
#endif
}

void main() {
    vec2 sceneUV = clamp(gl_FragCoord.xy * vol.targetInfo.xy, vec2(0.0), vec2(1.0));
    float sceneDepth = textureLod(uDepth, sceneUV, 0.0).r;
    vec4 world = vol.clipToWorld * vec4(sceneUV * 2.0 - 1.0, sceneDepth * 2.0 - 1.0, 1.0);
    vec3 ray = world.xyz / world.w - vol.eye.xyz;
    float sceneDistance = length(ray);
    vec3 dir = ray / max(sceneDistance, 1.0e-4);
    if (sceneDepth >= 0.99999) {
        sceneDistance = vol.params0.w;
    }
    float tEnd = min(sceneDistance, vol.params0.w);
    vec3 safeDir = dir + vec3(lessThan(abs(dir), vec3(1.0e-6))) * 1.0e-6;
    vec3 inv = 1.0 / safeDir;
    vec3 t0 = (vol.boundsMin.xyz - vol.eye.xyz) * inv;
    vec3 t1 = (vol.boundsMax.xyz - vol.eye.xyz) * inv;
    vec3 tn = min(t0, t1);
    vec3 tf = max(t0, t1);
    float tNear = max(max(tn.x, tn.y), max(tn.z, 0.0));
    float tFar = min(min(tf.x, tf.y), min(tf.z, tEnd));
    if (tFar <= tNear) {
        discard;
    }
    float steps = vol.forward.w;
    float dt = (tFar - tNear) / steps;
    float jitter = fract(VolumeNoise(gl_FragCoord.xy) + vol.eye.w);
    float g = vol.params0.z;
    float g2 = g * g;
    float forward = dot(dir, vol.forward.xyz);
    vec3 sum = vec3(0.0);
    for (int i = 0; i < 16; i++) {
        if (float(i) >= steps) {
            break;
        }
        float t = tNear + (float(i) + jitter) * dt;
        vec3 p = vol.eye.xyz + dir * t;
        vec4 p4 = vec4(p, 1.0);
        float q = dot(p4, vol.projQ);
        if (q <= 0.0) {
            continue;
        }
        vec2 projUv = vec2(dot(p4, vol.projS), dot(p4, vol.projT)) / q;
        float fall = dot(p4, vol.falloffS);
        if (projUv.x < 0.0 || projUv.y < 0.0 || projUv.x > 1.0 || projUv.y > 1.0
                || fall < 0.0 || fall > 1.0) {
            continue;
        }
        vec3 light = textureLod(uProjection, projUv, 0.0).rgb
            * textureLod(uFalloff, vec2(fall, 0.5), 0.0).rgb;
        if (light.r + light.g + light.b <= 0.0001) {
            continue;
        }
        float visibility = ShadowVisibility(p, t * forward);
        if (visibility <= 0.0) {
            continue;
        }
        float c = dot(normalize(vol.lightOrigin.xyz - p), dir);
        float phase = (1.0 - g2) / pow(max(1.0 + g2 - 2.0 * g * c, 1.0e-4), 1.5);
        sum += light * (visibility * phase * exp(-vol.params0.x * t));
    }
    outColor = vec4(sum * (dt * vol.params0.y) * vol.lightColor.rgb, 0.0);
}
