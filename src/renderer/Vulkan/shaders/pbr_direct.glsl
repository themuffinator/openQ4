// Copyright (C) 2026 DarkMatter Productions
// Native opaque/perforated PBR inputs shared by unshadowed, projected and
// point-shadow interactions. The classic pipeline keeps its original ABI.
// pc.c.xyw carries the data-layout bits, normal encoding and specular-AA switch.
// The classic specular-table sampler is unused by PBR and carries metallic.

// Parallax occlusion mapping (REN-11). A PBR draw never reads the classic
// cel-banding or specular-colour fields, so they carry the parallax inputs:
// inter.celParams = (scale, bias, most samples, self-shadow) and
// inter.specularColor.xy = fade (start, end); all zero when the surface is
// flat. PBRParallaxBegin traces the ray once per fragment; every material
// sample then reads the traced texel with the untraced coordinate's own
// gradients, so the hit never selects a wrong mip. Flat surfaces sample
// exactly as they always did.
#include "../../ParallaxOcclusion.h"

bool gPBRParallax = false;
vec4 gPBRParallaxHit = vec4(0.0);
vec2 gPBRBumpDx, gPBRBumpDy, gPBRAlbedoDx, gPBRAlbedoDy, gPBRDataDx, gPBRDataDy;

// The retail guide keeps its height in the red channel of the RXGB bump
// map; a PBR heightMap rides in the ORM alpha.
bool gParallaxRetailHeight = false;

float OpenQ4ParallaxHeight(vec2 uv, vec2 dx, vec2 dy) {
    if (gParallaxRetailHeight) {
        return textureGrad(bumpMap, uv, dx, dy).r;
    }
    return textureGrad(specularMap, uv, dx, dy).a;
}

vec3 PBRParallaxTangent(vec3 objectVector) {
    return vec3(dot(SafeNormalize(vPBRTangent0), objectVector),
        dot(SafeNormalize(vPBRTangent1), objectVector),
        dot(SafeNormalize(vPBRNormal), objectVector));
}

// pbrDraw: this draw is a native PBR draw, whose block carries the inputs.
void PBRParallaxBegin(bool pbrDraw) {
    gPBRParallax = pbrDraw && inter.celParams.x > 0.0 && inter.celParams.z > 0.5;
    if (!gPBRParallax) {
        return;
    }
    gPBRBumpDx = dFdx(vBumpTexCoord);
    gPBRBumpDy = dFdy(vBumpTexCoord);
    gPBRAlbedoDx = dFdx(vDiffuseTexCoord);
    gPBRAlbedoDy = dFdy(vDiffuseTexCoord);
    gPBRDataDx = dFdx(vSpecularTexCoord);
    gPBRDataDy = dFdy(vSpecularTexCoord);
    gPBRParallaxHit = OpenQ4ParallaxTrace(vSpecularTexCoord, PBRParallaxTangent(vViewVector),
        inter.celParams, inter.specularColor, length(vViewVector), gPBRDataDx, gPBRDataDy);
}

vec4 PBRBumpTexture(sampler2D map, vec2 texCoord) {
    if (gPBRParallax) {
        return textureGrad(map, texCoord + gPBRParallaxHit.xy, gPBRBumpDx, gPBRBumpDy);
    }
    return texture(map, texCoord);
}

vec4 PBRAlbedoTexture(sampler2D map, vec2 texCoord) {
    if (gPBRParallax) {
        return textureGrad(map, texCoord + gPBRParallaxHit.xy, gPBRAlbedoDx, gPBRAlbedoDy);
    }
    return texture(map, texCoord);
}

vec4 PBRDataTexture(sampler2D map, vec2 texCoord) {
    if (gPBRParallax) {
        return textureGrad(map, texCoord + gPBRParallaxHit.xy, gPBRDataDx, gPBRDataDy);
    }
    return texture(map, texCoord);
}

// The light's visibility through the height field; 1 without the option.
float PBRParallaxShadow(vec3 objectLight) {
    if (!gPBRParallax) {
        return 1.0;
    }
    return OpenQ4ParallaxShadow(vSpecularTexCoord, gPBRParallaxHit,
        PBRParallaxTangent(objectLight), inter.celParams, gPBRDataDx, gPBRDataDy);
}

// The retail Parallaxbump guide on classic draws (pc.d.x == 0): pc.c.xy carry
// its authored scale and bias and pc.c.z enables it. With
// r_parallaxOcclusion, pc.a.w is the ray's most samples and |pc.b.w| its fade
// end, negative with the self-shadow; pc.a.w == 0 keeps the retail single
// step exactly. The classic view and light vectors are already tangent-space.
bool gRetailParallaxRay = false;

vec2 RetailParallaxOffset() {
    if (pc.a.w < 0.5) {
        float height = texture(bumpMap, vBumpTexCoord).r;
        return SafeNormalize(vViewVector).xy * (height * pc.c.x + pc.c.y);
    }
    gRetailParallaxRay = true;
    gParallaxRetailHeight = true;
    gPBRBumpDx = dFdx(vBumpTexCoord);
    gPBRBumpDy = dFdy(vBumpTexCoord);
    gPBRAlbedoDx = dFdx(vDiffuseTexCoord);
    gPBRAlbedoDy = dFdy(vDiffuseTexCoord);
    gPBRDataDx = dFdx(vSpecularTexCoord);
    gPBRDataDy = dFdy(vSpecularTexCoord);
    float fadeEnd = abs(pc.b.w);
    gPBRParallaxHit = OpenQ4ParallaxTrace(vBumpTexCoord, vViewVector,
        vec4(pc.c.x, pc.c.y, pc.a.w, pc.b.w < 0.0 ? 1.0 : 0.0),
        vec4(fadeEnd * 0.75, fadeEnd, 0.0, 0.0), length(vViewVector), gPBRBumpDx, gPBRBumpDy);
    return gPBRParallaxHit.xy;
}

// texCoord already carries the offset; the gradients are the untraced ones.
vec4 RetailTexture(sampler2D map, vec2 texCoord, vec2 dx, vec2 dy) {
    if (gRetailParallaxRay) {
        return textureGrad(map, texCoord, dx, dy);
    }
    return texture(map, texCoord);
}

float RetailParallaxShadow() {
    if (!gRetailParallaxRay || pc.a.z > 0.5) {
        return 1.0;
    }
    return OpenQ4ParallaxShadow(vBumpTexCoord, gPBRParallaxHit, vLightVector,
        vec4(pc.c.x, pc.c.y, pc.a.w, pc.b.w < 0.0 ? 1.0 : 0.0), gPBRBumpDx, gPBRBumpDy);
}

// Native ordered transparency: pc.a.w carries the authored blend stage's
// alpha register, and the albedo image supplies the per-texel coverage the
// classic stage would have sampled. Every other interaction draw is additive
// and keeps the zero-alpha contract.
float PBRTransparentAlpha(vec2 albedoTexCoord) {
    if (pc.a.w <= 0.0) {
        return 0.0;
    }
    return clamp(PBRAlbedoTexture(diffuseMap, albedoTexCoord).a * pc.a.w, 0.0, 1.0);
}

vec3 PBRDirectNormal(vec2 texCoord) {
    int encoding = int(pc.c.y + 0.5);
    if (encoding == 0) {
        return vec3(0.0, 0.0, 1.0);
    }
    vec4 value = PBRBumpTexture(bumpMap, texCoord);
    vec3 normal;
    if (encoding == 2) {
        vec2 xy = (value.rg * 2.0 - 1.0) * pc.d.w;
        normal = vec3(xy, sqrt(max(1.0 - dot(xy, xy), 0.0)));
    } else {
        normal = (encoding == 3 ? value.rgb : value.agb) * 2.0 - 1.0;
        normal.xy *= pc.d.w;
    }
    float lengthSquared = dot(normal, normal);
    return lengthSquared > 1.0e-8
        ? normal * inversesqrt(lengthSquared) : vec3(0.0, 0.0, 1.0);
}

// SSAO occludes indirect light only, as the lesser of its occlusion and the
// material AO: both describe the same crevices. The field sits in slot 0
// (flag 32), drawn before lighting at this target's size; flag 64 when its
// rows run opposite to this target's. Direct light never reads it.
float PBRScreenAO(float ao, int dataFlags) {
    if ((dataFlags & 32) == 0) {
        return ao;
    }
    ivec2 texel = ivec2(gl_FragCoord.xy);
    if ((dataFlags & 64) != 0) {
        texel.y = textureSize(specularTableMap, 0).y - 1 - texel.y;
    }
    return min(ao, texelFetch(specularTableMap, texel, 0).r);
}

// Per-channel wrappers of the shared scalar AO/energy kernel (PBRMath.h).
vec3 PBRMultiBounceAOColor(float visibility, vec3 albedo) {
    return vec3(PBRMultiBounceAO(visibility, albedo.r),
        PBRMultiBounceAO(visibility, albedo.g), PBRMultiBounceAO(visibility, albedo.b));
}

// Production composition: a PBR draw that writes Quake 4's display-referred
// framebuffer encodes its linear radiance, exactly like the classic surfaces
// beside it. pc.b.w is zero only while the laboratory linear scene
// accumulates PBR separately (r_pbrLinearScene).
vec3 PBRDisplayOutput(vec3 radiance) {
    if (pc.b.w < 0.5) {
        return radiance;
    }
    return vec3(PBRLinearToSRGBExtended(radiance.r),
        PBRLinearToSRGBExtended(radiance.g), PBRLinearToSRGBExtended(radiance.b));
}

// The classic light term is what a white classic surface shows. Decoded and
// scaled by pi it is the irradiance at which a white Lambertian PBR surface
// matches it (PBRClassicLightIrradiance).
vec3 PBRClassicLightIrradianceColor(vec3 lightTerm) {
    return vec3(PBRClassicLightIrradiance(lightTerm.r),
        PBRClassicLightIrradiance(lightTerm.g), PBRClassicLightIrradiance(lightTerm.b));
}

vec3 PBREnergyCompensationColor(vec3 f0, float specularAlbedo) {
    return vec3(PBREnergyCompensation(f0.r, specularAlbedo),
        PBREnergyCompensation(f0.g, specularAlbedo), PBREnergyCompensation(f0.b, specularAlbedo));
}

// An authored ambient light stands in for bounced light from every
// direction: a uniform environment of radiance E / pi. It is the environment
// term (pbr_environment.glsl) with prefiltered radiance and irradiance both
// E / pi, so metals reflect it; the split sum needs no table
// (PBRUniformEnvironmentSpecular). Material AO is its indirect visibility.
vec3 PBRUniformEnvironmentLight(vec3 irradiance, vec3 albedo, float metallic, float roughness,
        float ao, vec3 n, vec3 v, vec3 vertexNormal) {
    float NoV = PBRShadingNoV(dot(n, v));
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 diffuseColor = albedo * (1.0 - metallic);
    vec3 fresnel = f0 + (max(vec3(1.0 - roughness), f0) - f0) * PBRFresnelWeight(NoV);
    vec3 specular = vec3(PBRUniformEnvironmentSpecular(f0.r, NoV, roughness),
            PBRUniformEnvironmentSpecular(f0.g, NoV, roughness),
            PBRUniformEnvironmentSpecular(f0.b, NoV, roughness))
        * PBRMultiBounceAOColor(PBRSpecularOcclusion(NoV, ao, roughness), f0)
        * PBRHorizonOcclusion(dot(reflect(-v, n), SafeNormalize(vertexNormal)));
    vec3 diffuse = (vec3(1.0) - fresnel) * diffuseColor * PBRMultiBounceAOColor(ao, diffuseColor);
    return (diffuse + specular) * irradiance * (1.0 / 3.14159265);
}

vec3 EvaluatePBRDirect(vec3 localNormal, vec2 albedoTexCoord,
        vec2 dataTexCoord, float shadowFactor) {
    // Color uses sRGB storage and decodes before filtering. Data stays linear.
    vec3 albedo = PBRAlbedoTexture(diffuseMap, albedoTexCoord).rgb;
    int dataFlags = int(pc.c.x + 0.5);
    vec2 materialData = vec2(1.0);
    float aoTexel = 1.0;
    if ((dataFlags & 1) != 0) {
        vec3 orm = PBRDataTexture(specularMap, dataTexCoord).rgb;
        materialData = orm.bg;
        aoTexel = orm.r;
    } else {
        if ((dataFlags & 2) != 0) {
            materialData.x = PBRDataTexture(specularTableMap, dataTexCoord).r;
        }
        if ((dataFlags & 4) != 0) {
            materialData.y = PBRDataTexture(specularMap, dataTexCoord).r;
        }
    }
    float metallic = clamp(materialData.x * pc.d.y, 0.0, 1.0);
    vec3 radiance = PBRClassicLightIrradianceColor(
        textureProj(lightFalloffMap, vLightFalloffTexCoord).rgb
        * textureProj(lightProjectionMap, vLightProjectionTexCoord).rgb
        * inter.diffuseColor.rgb) * shadowFactor;
    float roughness = PBRRoughness(materialData.y * pc.d.z);
    // A flat tangent-space normal still varies across a curved surface.
    // Measure the final normal in object space; rigid model rotation leaves
    // this variance unchanged. The three interaction variants share the same
    // footprint before any per-fragment lighting rejection.
    vec3 objectNormal = SafeNormalize(
        SafeNormalize(vPBRTangent0) * localNormal.x
        + SafeNormalize(vPBRTangent1) * localNormal.y
        + SafeNormalize(vPBRNormal) * localNormal.z);
    vec3 normalDx = dFdx(objectNormal);
    vec3 normalDy = dFdy(objectNormal);
    if (pc.c.w > 0.5) {
        roughness = PBRFilteredRoughness(roughness,
            0.5 * (dot(normalDx, normalDx) + dot(normalDy, normalDy)));
    }
    vec3 viewDir = SafeNormalize(vViewVector);
    if (pc.a.z > 0.5) {
        // An authored ambient light is a uniform environment, matching
        // ModernClusterEvaluatePBRLight and the OpenGL owner. pc.b.x is the
        // AO scalar; the texel comes from the ORM red channel or, when
        // separate maps could not be packed, a separate AO map bound in place
        // of the roughness map for this light stage only (flag 16), which
        // then leaves the scalar roughness.
        if ((dataFlags & 16) != 0) {
            aoTexel = PBRDataTexture(specularMap, dataTexCoord).r;
        }
        return PBRDisplayOutput(PBRUniformEnvironmentLight(radiance, albedo, metallic, roughness,
            PBRScreenAO(clamp(aoTexel * pc.b.x, 0.0, 1.0), dataFlags), objectNormal, viewDir, vPBRNormal) * vVertexColor);
    }
    vec3 lightDir = SafeNormalize(vLightVector);
    vec3 halfDir = SafeNormalize(lightDir + viewDir);
    float ndotl = max(dot(objectNormal, lightDir), 0.0);
    float ndotv = PBRShadingNoV(dot(objectNormal, viewDir));
    float ndoth = max(dot(objectNormal, halfDir), 0.0);
    float vdoth = max(dot(viewDir, halfDir), 0.0);
    if (ndotl <= 0.0 || dot(lightDir + viewDir, lightDir + viewDir) <= 1.0e-8) {
        return vec3(0.0);
    }
    float distribution = PBRDistributionGGX(ndoth, roughness);
    float visibility = PBRVisibilitySmithGGX(ndotv, ndotl, roughness);
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 fresnel = f0 + (vec3(1.0) - f0) * PBRFresnelWeight(vdoth);
    // Single scattering loses up to 69% of a rough conductor's energy. No
    // per-light pass binds the split-sum table, so the fitted albedo serves.
    vec3 specular = distribution * visibility * fresnel
        * PBREnergyCompensationColor(f0, PBRSpecularAlbedo(ndotv, roughness));
    vec3 diffuse = (vec3(1.0) - fresnel) * (1.0 - metallic)
        * albedo * (1.0 / 3.14159265);
    // Authored AO modulates indirect irradiance, never this direct light.
    // The parallax self-shadow is direct visibility, like the shadow factor.
    return PBRDisplayOutput((diffuse + specular) * radiance * ndotl * vVertexColor
        * PBRParallaxShadow(vLightVector));
}
