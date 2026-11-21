// Copyright (C) 2026 DarkMatter Productions
// Material diagnostics are a single full-surface pass, independent of lights.
// The environment ABI supplies the same material maps; its direction rows
// carry object-to-cluster rotation so normal colors match the modern GL view.
vec3 EvaluatePBRDebug() {
    int mode = int(pc.c.z + 0.5);
    if (mode == 7) {
        return vec3(0.0, 1.0, 0.0);
    }
    if (mode == 1) {
        return PBRAlbedoTexture(diffuseMap, vDiffuseTexCoord).rgb;
    }
    if (mode == 2) {
        vec3 mapped = PBRDirectNormal(vBumpTexCoord);
        vec3 objectNormal = SafeNormalize(
            SafeNormalize(vPBRTangent0) * mapped.x
            + SafeNormalize(vPBRTangent1) * mapped.y
            + SafeNormalize(vPBRNormal) * mapped.z);
        return PBREnvironmentWorld(objectNormal) * 0.5 + 0.5;
    }
    int flags = int(pc.c.x + 0.5);
    float metallic = pc.d.y, roughness = pc.d.z, ao = pc.b.x;
    if ((flags & 1) != 0) {
        vec3 orm = PBRDataTexture(specularMap, vSpecularTexCoord).rgb;
        ao *= orm.r;
        roughness *= orm.g;
        metallic *= orm.b;
    } else {
        if ((flags & 2) != 0) metallic *= PBRDataTexture(specularTableMap, vSpecularTexCoord).r;
        if ((flags & 4) != 0) roughness *= PBRDataTexture(specularMap, vSpecularTexCoord).r;
        if ((flags & 8) != 0) ao *= PBRDataTexture(lightProjectionMap, vSpecularTexCoord).r;
    }
    return vec3(mode == 3 ? clamp(metallic, 0.0, 1.0)
        : mode == 4 ? PBRRoughness(roughness) : clamp(ao, 0.0, 1.0));
}
