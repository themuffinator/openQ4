// Copyright (C) 2026 DarkMatter Productions
// The environment pass reuses the direct interaction ABI. Set 2 is the
// filtered environment atlas, set 3 separate AO, and the projection rows are
// object-to-world direction rows. No per-light value enters this evaluation.

vec3 PBREnvironmentLevel(int slot, vec3 direction, int level) {
    vec3 d = SafeNormalize(direction), ad = abs(d);
    int face;
    vec2 uv;
    float major;
    if (ad.x >= ad.y && ad.x >= ad.z) {
        major = ad.x;
        face = d.x >= 0.0 ? 0 : 1;
        uv = vec2(d.x >= 0.0 ? -d.z : d.z, -d.y);
    } else if (ad.y >= ad.z) {
        major = ad.y;
        face = d.y >= 0.0 ? 2 : 3;
        uv = vec2(d.x, d.y >= 0.0 ? d.z : -d.z);
    } else {
        major = ad.z;
        face = d.z >= 0.0 ? 4 : 5;
        uv = vec2(d.z >= 0.0 ? d.x : -d.x, -d.y);
    }
    uv = clamp(uv / max(major, 1.0e-6) * 0.5 + 0.5, 0.0, 1.0);
    int cell = slot * 6 + face;
    int size = 256 >> level;
    vec2 texel = vec2(ivec2(cell % 8, cell / 8) * size) + 0.5 + uv * float(size - 1);
    return textureLod(lightFalloffMap, texel / float(2048 >> level), float(level)).rgb;
}

vec3 PBREnvironmentTile(int cell, vec2 uv, float size) {
    vec2 origin = vec2(cell % 8, cell / 8) * 256.0 + 0.5;
    return textureLod(lightFalloffMap,
        (origin + clamp(uv, 0.0, 1.0) * (size - 1.0)) / 2048.0, 0.0).rgb;
}

vec3 PBREnvironmentWorld(vec3 objectDirection) {
    return SafeNormalize(vec3(dot(inter.lightProjectionS.xyz, objectDirection),
        dot(inter.lightProjectionT.xyz, objectDirection),
        dot(inter.lightProjectionQ.xyz, objectDirection)));
}

#ifdef PBR_AUTHORED_PROBES
#include "pbr_probes.glsl"
#endif

#ifdef PBR_BAKED_LIGHTGRID
#include "pbr_baked_grid.glsl"
vec3 EvaluateBakedClassic() {
    // The shipped flat bump has quantized AGB components, not exact (0,0,1).
    // Preserve the modern GL decode even for this bounded flat-map receiver.
    vec3 tangentNormal = SafeNormalize(texture(bumpMap, vBumpTexCoord).agb * 2.0 - 1.0);
    mat3 basis = mat3(SafeNormalize(vPBRTangent0), SafeNormalize(vPBRTangent1), SafeNormalize(vPBRNormal));
    vec3 n = PBREnvironmentWorld(SafeNormalize(basis * tangentNormal));
    vec4 baked = ModernBakedIrradiance(vLightProjectionTexCoord.xyw, n, false);
    return ModernBakedClamp(texture(diffuseMap, vDiffuseTexCoord).rgb
        * inter.diffuseColor.rgb * vVertexColor * baked.rgb, baked.w);
}
#endif

vec3 EvaluatePBREnvironment() {
    vec3 albedo = PBRAlbedoTexture(diffuseMap, vDiffuseTexCoord).rgb * inter.diffuseColor.rgb;
    int flags = int(pc.c.x + 0.5);
    float metallic = pc.d.y, roughness = pc.d.z, ao = pc.b.x;
    if ((flags & 1) != 0) {
        vec3 orm = PBRDataTexture(specularMap, vSpecularTexCoord).rgb;
        ao *= orm.r; roughness *= orm.g; metallic *= orm.b;
    } else {
        if ((flags & 2) != 0) metallic *= PBRDataTexture(specularTableMap, vSpecularTexCoord).r;
        if ((flags & 4) != 0) roughness *= PBRDataTexture(specularMap, vSpecularTexCoord).r;
        if ((flags & 8) != 0) ao *= PBRDataTexture(lightProjectionMap, vSpecularTexCoord).r;
    }
    metallic = clamp(metallic, 0.0, 1.0);
    roughness = PBRRoughness(roughness);
    ao = PBRScreenAO(clamp(ao, 0.0, 1.0), flags);
    mat3 basis = mat3(SafeNormalize(vPBRTangent0), SafeNormalize(vPBRTangent1), SafeNormalize(vPBRNormal));
    vec3 objectNormal = SafeNormalize(basis * PBRDirectNormal(vBumpTexCoord));
    vec3 dx = dFdx(objectNormal), dy = dFdy(objectNormal);
    if (pc.c.w > 0.5) {
        roughness = PBRFilteredRoughness(roughness, 0.5 * (dot(dx, dx) + dot(dy, dy)));
    }
    vec3 n = PBREnvironmentWorld(objectNormal);
    vec3 v = PBREnvironmentWorld(vViewVector);
    float NoV = clamp(dot(n, v), 0.0, 1.0);
    vec3 reflection = reflect(-v, n);
    float lod = roughness * 6.0;
    int low = int(floor(lod));
    // The analytic studio environment lights only the laboratory
    // (r_pbrAnalyticEnvironment, pc.b.y); production starts from black and
    // takes its environment from authored probes and baked light grids.
    vec3 prefiltered = mix(PBREnvironmentLevel(8, reflection, low),
        PBREnvironmentLevel(8, reflection, min(low + 1, 6)), fract(lod)) * pc.b.y;
    vec3 octNormal = n / max(abs(n.x) + abs(n.y) + abs(n.z), 1.0e-6);
    vec2 oct = octNormal.xy;
    if (n.z < 0.0) {
        oct = (1.0 - abs(octNormal.yx))
            * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    }
    vec3 irradiance = PBREnvironmentTile(54 + 8, oct * 0.5 + 0.5, 32.0) * pc.b.y;
#ifdef PBR_AUTHORED_PROBES
    PBRProbeBlend(vLightProjectionTexCoord.xyw, reflection, n, roughness, prefiltered, irradiance);
#endif
    vec2 brdf = PBREnvironmentTile(63, vec2(NoV, roughness), 128.0).rg;
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 fresnel = f0 + (max(vec3(1.0 - roughness), f0) - f0) * PBRFresnelWeight(NoV);
    // AO is indirect visibility. Diffuse takes its multi-bounce form; the
    // specular cone takes its own occlusion and bounce off F0, and fades where
    // the normal map turns the reflection below the geometric surface. The
    // table's A + B is the single-scattering albedo the compensation restores.
    vec3 diffuseColor = (1.0 - metallic) * albedo;
    vec3 diffuseAO = PBRMultiBounceAOColor(ao, diffuseColor);
    vec3 specularAO = PBRMultiBounceAOColor(PBRSpecularOcclusion(NoV, ao, roughness), f0)
        * PBRHorizonOcclusion(dot(reflection, PBREnvironmentWorld(vPBRNormal)));
    vec3 diffuse = (1.0 - fresnel) * diffuseColor * irradiance * diffuseAO;
    vec3 specular = prefiltered * (f0 * brdf.x + brdf.y)
        * PBREnergyCompensationColor(f0, brdf.x + brdf.y) * specularAO;
#ifdef PBR_BAKED_LIGHTGRID
    // PBR shades in linear light whether the draw accumulates the laboratory
    // linear scene or encodes into the display-referred frame, so it always
    // decodes the display-encoded atlas texels.
    vec4 baked = ModernBakedIrradiance(vLightProjectionTexCoord.xyw, n, true);
    diffuse = ModernBakedClamp((1.0 - fresnel) * diffuseColor
        * baked.rgb * diffuseAO * vVertexColor, baked.w);
    return PBRDisplayOutput(diffuse + specular * pc.c.z * vVertexColor);
#else
    return PBRDisplayOutput((diffuse + specular) * pc.c.z * vVertexColor);
#endif
}
