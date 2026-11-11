#version 450

layout(set = 0, binding = 0) uniform sampler2D currentScene;
layout(set = 1, binding = 0) uniform sampler2D sceneDepth;
layout(set = 2, binding = 0) uniform sampler2D historyScene;
layout(set = 3, binding = 0) uniform sampler2D objectMotion;

layout(std140, set = 6, binding = 0) uniform TemporalResolveBlock {
    // xy = inverse scene extent; zw = native output extent.
    vec4 sceneOutputExtent;
    // xy = inverse projection scale; zw = current projection offsets.
    vec4 currentReconstruct;
    // xy = previous projection scale; zw = the previous camera's jitter-free
    // offsets plus the jitter applied to the current frame, so a reprojected
    // scene texel is that texel plus its motion (no jitter from either frame).
    vec4 previousProject;
    // xy = current depth projection; z = feedback; w = reactive scale.
    vec4 depthFeedback;
    // The unused w components carry the bounded screen-effect packet:
    // mask, froxel density/distance/slices, SSR intensity/distance/steps,
    // and SSGI intensity. xyz retain their temporal camera contract.
    vec4 currentViewOrigin;
    vec4 currentViewAxis0;
    vec4 currentViewAxis1;
    vec4 currentViewAxis2;
    vec4 previousViewOrigin;
    vec4 previousViewAxis0;
    vec4 previousViewAxis1;
    vec4 previousViewAxis2;
    // xy = jitter the current projection applies, as a fraction of the scene
    // extent (a point at unjittered uv is drawn at uv - jitter);
    // z = use history; w = debug mode.
    vec4 temporalParams;
    // x = use object velocity;
    // y = camera reprojection valid; z = depth valid;
    // w = flags: sample the scene recentered (1: spatial presents and
    //     upscaling), flip scene/depth/history Y (2/4/8), sharpen the
    //     presented history (16).
    vec4 motionParams;
    // Conservative current-frame regions whose packet motion domains lack an
    // exact Vulkan velocity stream. Invalid entries have non-positive extent.
    // A history present (flag 16) reads no regions: reactiveRect1.x then
    // carries the sharpening amount.
    vec4 reactiveRect0;
    vec4 reactiveRect1;
} temporal;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

const float kDepthEpsilon = 0.00001;
vec2 TextureToCameraUV(vec2 uv) {
    return vec2(uv.x, 1.0 - uv.y);
}

vec2 CameraToTextureUV(vec2 uv) {
    return vec2(uv.x, 1.0 - uv.y);
}

vec2 StoredTextureUV(vec2 uv, int bit) {
    return (int(temporal.motionParams.w + 0.5) & bit) != 0
        ? vec2(uv.x, 1.0 - uv.y) : uv;
}

float ViewZFromDepth(float depth) {
    float denominator = depth * 2.0 - 1.0 + temporal.depthFeedback.x;
    if (abs(denominator) < kDepthEpsilon) {
        denominator = denominator < 0.0 ? -kDepthEpsilon : kDepthEpsilon;
    }
    return -temporal.depthFeedback.y / denominator;
}

vec3 CurrentViewToWorldDirection(vec3 viewPosition) {
    return temporal.currentViewAxis0.xyz * (-viewPosition.z)
        + temporal.currentViewAxis1.xyz * (-viewPosition.x)
        + temporal.currentViewAxis2.xyz * viewPosition.y;
}

vec3 WorldToPreviousViewDirection(vec3 worldDirection) {
    return vec3(
        -dot(worldDirection, temporal.previousViewAxis1.xyz),
        dot(worldDirection, temporal.previousViewAxis2.xyz),
        -dot(worldDirection, temporal.previousViewAxis0.xyz));
}

vec2 ProjectPreviousView(vec3 previousViewPosition) {
    float w = -previousViewPosition.z;
    if (abs(w) < kDepthEpsilon) {
        return vec2(-1000.0);
    }
    vec2 clipXY = temporal.previousProject.xy * previousViewPosition.xy
        + temporal.previousProject.zw * previousViewPosition.z;
    return clipXY / w * 0.5 + 0.5;
}

vec2 CameraPreviousUV(vec2 cameraUV, float depth) {
    vec2 ndc = cameraUV * 2.0 - 1.0;
    if (depth >= 0.99999) {
        vec3 ray = vec3(
            -(ndc.x + temporal.currentReconstruct.z)
                * temporal.currentReconstruct.x,
            -(ndc.y + temporal.currentReconstruct.w)
                * temporal.currentReconstruct.y,
            -1.0);
        return ProjectPreviousView(WorldToPreviousViewDirection(
            CurrentViewToWorldDirection(ray)));
    }
    float viewZ = ViewZFromDepth(depth);
    vec3 viewPosition = vec3(
        -viewZ * (ndc.x + temporal.currentReconstruct.z)
            * temporal.currentReconstruct.x,
        -viewZ * (ndc.y + temporal.currentReconstruct.w)
            * temporal.currentReconstruct.y,
        viewZ);
    // Relative to the previous origin without passing through absolute world
    // coordinates: their rounding is a constant sub-pixel history offset per
    // pixel, which a still camera would integrate into blur.
    vec3 delta = CurrentViewToWorldDirection(viewPosition)
        + (temporal.currentViewOrigin.xyz - temporal.previousViewOrigin.xyz);
    return ProjectPreviousView(vec3(
        -dot(delta, temporal.previousViewAxis1.xyz),
        dot(delta, temporal.previousViewAxis2.xyz),
        -dot(delta, temporal.previousViewAxis0.xyz)));
}

float MaxComponent(vec3 value) {
    return max(value.x, max(value.y, value.z));
}

float Luma(vec3 c) {
    return dot(c, vec3(0.25, 0.5, 0.25));
}

vec3 RGBToYCoCg(vec3 c) {
    return vec3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b,
        0.5 * c.r - 0.5 * c.b,
        -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}

vec3 YCoCgToRGB(vec3 c) {
    return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

// Catmull-Rom history reconstruction from five bilinear taps, in the history
// texture's stored orientation (a Y flip maps texel centres onto texel
// centres). A bilinear-only history softens a little more every reprojection.
vec3 SampleHistory(vec2 textureUV) {
    vec2 historySize = temporal.sceneOutputExtent.zw;
    vec2 position = textureUV * historySize;
    vec2 center = floor(position - 0.5) + 0.5;
    vec2 f = position - center;
    vec2 f2 = f * f;
    vec2 f3 = f2 * f;
    vec2 w0 = -0.5 * f3 + f2 - 0.5 * f;
    vec2 w1 = 1.5 * f3 - 2.5 * f2 + 1.0;
    vec2 w2 = -1.5 * f3 + 2.0 * f2 + 0.5 * f;
    vec2 w3 = 0.5 * f3 - 0.5 * f2;
    vec2 w12 = w1 + w2;
    vec2 invSize = 1.0 / historySize;
    vec2 tc12 = (center + w2 / w12) * invSize;
    vec2 tc0 = (center - 1.0) * invSize;
    vec2 tc3 = (center + 2.0) * invSize;
    vec3 sum = texture(historyScene, vec2(tc12.x, tc0.y)).rgb * (w12.x * w0.y)
        + texture(historyScene, vec2(tc0.x, tc12.y)).rgb * (w0.x * w12.y)
        + texture(historyScene, tc12).rgb * (w12.x * w12.y)
        + texture(historyScene, vec2(tc3.x, tc12.y)).rgb * (w3.x * w12.y)
        + texture(historyScene, vec2(tc12.x, tc3.y)).rgb * (w12.x * w3.y);
    float weight = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y
        + w3.x * w12.y + w12.x * w3.y;
    return max(sum / max(weight, 0.0001), vec3(0.0));
}

// AMD CAS, cross pattern, for presenting a resolved history: sharpen less where
// the neighbourhood is already near black or white, so edges do not ring.
vec3 SharpenPresent(vec2 textureUV, vec3 center, float amount) {
    vec2 texel = temporal.sceneOutputExtent.xy;
    vec3 north = texture(currentScene, textureUV + vec2(0.0, texel.y)).rgb;
    vec3 south = texture(currentScene, textureUV - vec2(0.0, texel.y)).rgb;
    vec3 east = texture(currentScene, textureUV + vec2(texel.x, 0.0)).rgb;
    vec3 west = texture(currentScene, textureUV - vec2(texel.x, 0.0)).rgb;
    vec3 lowest = min(center, min(min(north, south), min(east, west)));
    vec3 highest = max(center, max(max(north, south), max(east, west)));
    vec3 strength = sqrt(clamp(min(lowest, 2.0 - highest)
        / max(highest, vec3(0.0001)), 0.0, 1.0));
    vec3 weight = strength * (-1.0 / mix(8.0, 5.0, amount));
    return max((center + (north + south + east + west) * weight)
        / (1.0 + 4.0 * weight), vec3(0.0));
}

// Clip toward the box centre rather than clamping each channel, so a rejected
// history colour keeps its hue instead of turning grey.
vec3 ClipToBox(vec3 value, vec3 boxMin, vec3 boxMax) {
    vec3 center = 0.5 * (boxMax + boxMin);
    vec3 extents = 0.5 * (boxMax - boxMin) + vec3(0.0001);
    vec3 offset = value - center;
    vec3 units = abs(offset / extents);
    float largest = MaxComponent(units);
    return largest > 1.0 ? center + offset / largest : value;
}

bool InsideReactiveRect(vec2 cameraUV, vec4 rect) {
    return rect.z > rect.x && rect.w > rect.y
        && all(greaterThanEqual(cameraUV, rect.xy))
        && all(lessThan(cameraUV, rect.zw));
}

bool ScreenEffectEnabled(float bitValue) {
    return mod(floor(temporal.currentViewOrigin.w / bitValue), 2.0) > 0.5;
}

float SceneDepthAt(vec2 cameraUV) {
    return texture(sceneDepth, StoredTextureUV(CameraToTextureUV(clamp(cameraUV,
        vec2(0.0), vec2(1.0))), 4)).r;
}

vec3 SceneColorAt(vec2 cameraUV) {
    return texture(currentScene, StoredTextureUV(CameraToTextureUV(clamp(cameraUV,
        vec2(0.0), vec2(1.0))), 2)).rgb;
}

vec3 CurrentViewPosition(vec2 cameraUV, float depth) {
    vec2 ndc = cameraUV * 2.0 - 1.0;
    float viewZ = ViewZFromDepth(depth);
    return vec3(
        -viewZ * (ndc.x + temporal.currentReconstruct.z)
            * temporal.currentReconstruct.x,
        -viewZ * (ndc.y + temporal.currentReconstruct.w)
            * temporal.currentReconstruct.y,
        viewZ);
}

vec2 ProjectCurrentView(vec3 viewPosition) {
    float w = -viewPosition.z;
    if (w <= kDepthEpsilon) {
        return vec2(-1000.0);
    }
    vec2 clipXY = vec2(
        viewPosition.x / temporal.currentReconstruct.x,
        viewPosition.y / temporal.currentReconstruct.y)
        + temporal.currentReconstruct.zw * viewPosition.z;
    return clipXY / w * 0.5 + 0.5;
}

vec3 DepthNormal(vec2 cameraUV, vec3 centerPosition) {
    vec2 dx = vec2(temporal.sceneOutputExtent.x, 0.0);
    vec2 dy = vec2(0.0, temporal.sceneOutputExtent.y);
    vec2 leftUV = clamp(cameraUV - dx, vec2(0.0), vec2(1.0));
    vec2 rightUV = clamp(cameraUV + dx, vec2(0.0), vec2(1.0));
    vec2 downUV = clamp(cameraUV - dy, vec2(0.0), vec2(1.0));
    vec2 upUV = clamp(cameraUV + dy, vec2(0.0), vec2(1.0));
    vec3 tangentX = CurrentViewPosition(rightUV, SceneDepthAt(rightUV))
        - CurrentViewPosition(leftUV, SceneDepthAt(leftUV));
    vec3 tangentY = CurrentViewPosition(upUV, SceneDepthAt(upUV))
        - CurrentViewPosition(downUV, SceneDepthAt(downUV));
    vec3 normalCross = cross(tangentX, tangentY);
    float normalLengthSquared = dot(normalCross, normalCross);
    vec3 normal = normalLengthSquared > 1.0e-12
        ? normalCross * inversesqrt(normalLengthSquared)
        : vec3(0.0, 0.0, 1.0);
    if (dot(normal, -centerPosition) < 0.0) {
        normal = -normal;
    }
    return normal;
}

float Luminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

vec3 ApplyScreenSpaceGI(vec3 baseColor, vec2 cameraUV, float depth,
        vec3 position, vec3 normal) {
    if (!ScreenEffectEnabled(4.0) || temporal.motionParams.z < 0.5
            || depth >= 0.99999) {
        return baseColor;
    }
    vec3 indirect = vec3(0.0);
    float totalWeight = 0.0;
    for (int i = 0; i < 8; ++i) {
        float fi = float(i);
        float angle = fi * 2.39996323;
        vec2 offset = vec2(cos(angle), sin(angle)) * (2.0 + fi * 1.5)
            * temporal.sceneOutputExtent.xy;
        vec2 sampleUV = clamp(cameraUV + offset, vec2(0.0), vec2(1.0));
        float sampleDepth = SceneDepthAt(sampleUV);
        if (sampleDepth >= 0.99999) {
            continue;
        }
        vec3 samplePosition = CurrentViewPosition(sampleUV, sampleDepth);
        vec3 delta = samplePosition - position;
        float deltaLength = length(delta);
        if (deltaLength <= 0.0001) {
            continue;
        }
        float distanceWeight = 1.0 - smoothstep(0.0,
            max(24.0, abs(position.z) * 0.18), deltaLength);
        float facing = max(dot(normal, delta / deltaLength), 0.0);
        float weight = distanceWeight * (0.20 + 0.80 * facing);
        indirect += SceneColorAt(sampleUV) * weight;
        totalWeight += weight;
    }
    indirect /= max(totalWeight, 0.0001);
    float receive = 0.08 + 0.12
        * (1.0 - clamp(Luminance(baseColor), 0.0, 1.0));
    return baseColor + indirect * temporal.previousViewAxis2.w * receive;
}

vec3 ApplyScreenSpaceReflection(vec3 baseColor, vec2 cameraUV, float depth,
        vec3 position, vec3 normal) {
    if (!ScreenEffectEnabled(2.0) || temporal.motionParams.z < 0.5
            || depth >= 0.99999) {
        return baseColor;
    }
    vec3 incident = normalize(position);
    vec3 rayDirection = normalize(reflect(incident, normal));
    float stepCount = clamp(floor(temporal.previousViewAxis1.w + 0.5),
        4.0, 16.0);
    float stepLength = temporal.previousViewAxis0.w / stepCount;
    vec3 rayPosition = position + normal
        * max(1.0, abs(position.z) * 0.002);
    vec3 hitColor = baseColor;
    float hitWeight = 0.0;
    for (int i = 0; i < 16; ++i) {
        if (float(i) >= stepCount) {
            break;
        }
        rayPosition += rayDirection * stepLength;
        if (rayPosition.z >= -0.5) {
            break;
        }
        vec2 rayUV = ProjectCurrentView(rayPosition);
        if (rayUV.x <= 0.0 || rayUV.y <= 0.0
                || rayUV.x >= 1.0 || rayUV.y >= 1.0) {
            break;
        }
        float rayDepth = SceneDepthAt(rayUV);
        if (rayDepth >= 0.99999) {
            continue;
        }
        float sceneZ = ViewZFromDepth(rayDepth);
        float thickness = max(4.0, abs(rayPosition.z) * 0.02);
        float crossing = sceneZ - rayPosition.z;
        if (i > 0 && crossing >= 0.0 && crossing < thickness) {
            vec2 edge = smoothstep(vec2(0.0), vec2(0.08), rayUV)
                * smoothstep(vec2(0.0), vec2(0.08), vec2(1.0) - rayUV);
            hitColor = SceneColorAt(rayUV);
            hitWeight = edge.x * edge.y;
            break;
        }
    }
    float facing = clamp(dot(-incident, normal), 0.0, 1.0);
    float fresnel = 0.04 + 0.96 * pow(1.0 - facing, 5.0);
    return mix(baseColor, hitColor,
        hitWeight * fresnel * temporal.previousViewOrigin.w);
}

vec3 ApplyFroxelVolumetrics(vec3 baseColor, vec2 cameraUV, float depth) {
    if (!ScreenEffectEnabled(1.0)) {
        return baseColor;
    }
    vec2 ndc = cameraUV * 2.0 - 1.0;
    vec3 viewRay = normalize(vec3(
        -(ndc.x + temporal.currentReconstruct.z)
            * temporal.currentReconstruct.x,
        -(ndc.y + temporal.currentReconstruct.w)
            * temporal.currentReconstruct.y,
        -1.0));
    vec3 worldRay = normalize(CurrentViewToWorldDirection(viewRay));
    float travel = temporal.currentViewAxis1.w;
    if (temporal.motionParams.z > 0.5 && depth < 0.99999) {
        travel = min(travel, length(CurrentViewPosition(cameraUV, depth)));
    }
    float sliceCount = clamp(floor(temporal.currentViewAxis2.w + 0.5),
        4.0, 16.0);
    float sliceLength = travel / sliceCount;
    float transmittance = 1.0;
    vec3 scattering = vec3(0.0);
    vec3 sunDirection = normalize(vec3(0.35, 0.25, 0.90));
    for (int i = 0; i < 16; ++i) {
        if (float(i) >= sliceCount) {
            break;
        }
        float midpoint = (float(i) + 0.5) * sliceLength;
        float worldHeight = temporal.currentViewOrigin.z
            + worldRay.z * midpoint;
        float heightDensity = exp(-clamp((worldHeight
            - temporal.currentViewOrigin.z) * 0.0008, -1.5, 2.0));
        float sliceTransmittance = exp(-temporal.currentViewAxis0.w
            * heightDensity * sliceLength);
        float phase = 0.55 + 0.45
            * pow(max(dot(worldRay, sunDirection), 0.0), 2.0);
        vec3 fogColor = vec3(0.17, 0.21, 0.26)
            + vec3(0.08, 0.07, 0.04) * max(worldRay.z, 0.0);
        scattering += transmittance * (1.0 - sliceTransmittance)
            * fogColor * phase;
        transmittance *= sliceTransmittance;
    }
    return baseColor * transmittance + scattering;
}

void main() {
    vec2 outputCameraUV = TextureToCameraUV(fragUV);
    vec2 sceneCameraUV = clamp(outputCameraUV
        - temporal.temporalParams.xy * float(int(temporal.motionParams.w + 0.5) & 1),
        vec2(0.0), vec2(1.0));
    float centerDepth = SceneDepthAt(sceneCameraUV);
    // What lies behind the jittered view (a portal sky) was drawn without
    // jitter. Where nothing of this view covers the pixel - cleared depth at
    // both positions and no reactive surface over it - the recentered position
    // would move that backdrop by the jitter, so sample it in place. Without a
    // depth image nothing is known to be backdrop.
    if ((int(temporal.motionParams.w + 0.5) & 1) != 0
            && temporal.motionParams.z > 0.5 && centerDepth >= 0.99999
            && SceneDepthAt(outputCameraUV) >= 0.99999
            && (temporal.motionParams.x < 0.5
                || texture(objectMotion, sceneCameraUV).b <= 0.0)) {
        sceneCameraUV = outputCameraUV;
    }
    vec2 sceneTextureUV = CameraToTextureUV(sceneCameraUV);
    vec4 current = texture(currentScene, StoredTextureUV(sceneTextureUV, 2));
    if (temporal.currentViewOrigin.w > 0.5) {
        vec3 centerPosition = centerDepth < 0.99999
            ? CurrentViewPosition(sceneCameraUV, centerDepth)
            : vec3(0.0, 0.0, -temporal.currentViewAxis1.w);
        vec3 centerNormal = centerDepth < 0.99999
            ? DepthNormal(sceneCameraUV, centerPosition)
            : vec3(0.0, 0.0, 1.0);
        current.rgb = ApplyScreenSpaceGI(current.rgb, sceneCameraUV,
            centerDepth, centerPosition, centerNormal);
        current.rgb = ApplyScreenSpaceReflection(current.rgb, sceneCameraUV,
            centerDepth, centerPosition, centerNormal);
        current.rgb = ApplyFroxelVolumetrics(current.rgb, sceneCameraUV,
            centerDepth);
    }
    // 3x3 neighbourhood: YCoCg mean/variance for the history clip box, and the
    // nearest depth, whose velocity is used so a moving silhouette carries its
    // own motion onto the background texels around it.
    vec3 moment1 = vec3(0.0);
    vec3 moment2 = vec3(0.0);
    vec3 sampleMin = vec3(1.0e20);
    vec3 sampleMax = vec3(-1.0e20);
    float depthMin = centerDepth;
    float depthMax = centerDepth;
    vec2 closestCameraUV = sceneCameraUV;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 sampleCameraUV = clamp(sceneCameraUV
                + vec2(float(x), float(y)) * temporal.sceneOutputExtent.xy,
                vec2(0.0), vec2(1.0));
            vec2 sampleTextureUV = CameraToTextureUV(sampleCameraUV);
            vec3 sampleColor = (x == 0 && y == 0) ? current.rgb
                : texture(currentScene, StoredTextureUV(sampleTextureUV, 2)).rgb;
            vec3 sampleYCoCg = RGBToYCoCg(sampleColor);
            moment1 += sampleYCoCg;
            moment2 += sampleYCoCg * sampleYCoCg;
            sampleMin = min(sampleMin, sampleYCoCg);
            sampleMax = max(sampleMax, sampleYCoCg);
            float sampleDepth = texture(sceneDepth, StoredTextureUV(sampleTextureUV, 4)).r;
            if (sampleDepth < depthMin) {
                closestCameraUV = sampleCameraUV;
            }
            depthMin = min(depthMin, sampleDepth);
            depthMax = max(depthMax, sampleDepth);
        }
    }
    vec3 mean = moment1 / 9.0;
    vec3 sigma = sqrt(max(moment2 / 9.0 - mean * mean, vec3(0.0)));
    // One low-resolution frame under-samples what the history has built up per
    // output pixel, so the box widens with the upscale factor (it stays inside
    // the sample range): a tight box pulled every accumulated edge toward
    // wherever this frame's aliased edge happened to fall.
    float clipSigma = 1.25 * clamp(temporal.sceneOutputExtent.z
        * temporal.sceneOutputExtent.x, 1.0, 3.0);
    vec3 neighborhoodMin = max(mean - sigma * clipSigma, sampleMin);
    vec3 neighborhoodMax = min(mean + sigma * clipSigma, sampleMax);

    // Vectors use bottom-up camera coordinates and scene-pixel units. The
    // scene/history targets use top-down texture coordinates. Blue carries the
    // per-pixel reactive coverage the velocity pass wrote.
    // Dilate only toward a moving object: a still occluder's edge must not pin
    // the moving background beside it.
    vec4 closestVelocity = temporal.motionParams.x > 0.5
        ? texture(objectMotion, closestCameraUV) : vec4(0.0);
    vec4 centerVelocity = temporal.motionParams.x > 0.5
        ? texture(objectMotion, sceneCameraUV) : vec4(0.0);
    vec4 objectVelocity = closestVelocity.a > 0.5 ? closestVelocity : centerVelocity;
    float reactiveMask = max(closestVelocity.b, centerVelocity.b);
    bool objectValid = objectVelocity.a > 0.5;
    bool cameraValid = temporal.motionParams.y > 0.5
        && temporal.motionParams.z > 0.5;
    // previousProject carries the previous camera aligned to this frame's
    // jitter, so the reprojection of a scene texel is that texel plus its
    // motion; the history itself lives on the unjittered output grid.
    vec2 previousCameraUV = cameraValid
        ? outputCameraUV
            + (CameraPreviousUV(sceneCameraUV, depthMin) - sceneCameraUV)
        : outputCameraUV;
    if (objectValid) {
        previousCameraUV = outputCameraUV
            - objectVelocity.xy * temporal.sceneOutputExtent.xy;
    }
    bool inside = previousCameraUV.x >= 0.0
        && previousCameraUV.y >= 0.0
        && previousCameraUV.x <= 1.0
        && previousCameraUV.y <= 1.0;
    bool historyUsable = temporal.temporalParams.z > 0.5
        && inside && (objectValid || cameraValid);
    vec3 historyRaw = historyUsable
        ? SampleHistory(StoredTextureUV(CameraToTextureUV(previousCameraUV), 8))
        : current.rgb;
    vec3 historyClamped = YCoCgToRGB(ClipToBox(RGBToYCoCg(historyRaw),
        neighborhoodMin, neighborhoodMax));
    vec2 velocityPixels = (outputCameraUV - previousCameraUV)
        * temporal.sceneOutputExtent.zw;
    float speed = length(velocityPixels);
    // Depth edges only disocclude while something moves; a still edge is
    // exactly where accumulated history anti-aliases.
    float depthReactive = temporal.motionParams.z > 0.5
        ? smoothstep(0.001, 0.02, depthMax - depthMin)
            * smoothstep(0.5, 4.0, speed) * 0.5
        : 0.0;
    float packetReactive = (InsideReactiveRect(outputCameraUV,
            temporal.reactiveRect0) || InsideReactiveRect(outputCameraUV,
            temporal.reactiveRect1)) ? 1.0 : 0.0;
    float reactive = clamp(max(max(reactiveMask * temporal.depthFeedback.w,
        packetReactive), depthReactive), 0.0, 1.0);
    // A full mask (no previous clip position, a newly visible surface, a
    // subview) owns no history at all, whatever the reactive scale.
    if (!historyUsable || reactiveMask >= 0.999) {
        reactive = 1.0;
    }
    // Where history is rejected the current frame shows through. Its raw
    // samples sit at this frame's jittered positions, which reads as the
    // surface shaking; take the recentered frame for that share instead.
    if ((int(temporal.motionParams.w + 0.5) & 1) == 0 && reactive > 0.0
            && temporal.currentViewOrigin.w < 0.5) {
        vec3 steady = SceneColorAt(outputCameraUV - temporal.temporalParams.xy);
        current.rgb = mix(current.rgb, steady, reactive);
    }
    // Fast motion keeps less history: it cannot be anti-aliased anyway, and
    // every reprojection resamples it.
    float feedback = temporal.depthFeedback.z
        * (1.0 - 0.3 * smoothstep(16.0, 96.0, speed));
    // A pixel at rest keeps integrating: it takes half the usual share of each
    // new frame, and less again when upscaling, where one frame holds only a
    // fraction of the output samples. That shrinks the ripple each jittered
    // sample leaves on a still edge; nothing at rest can ghost, and any motion
    // returns to the base feedback.
    float sceneShare = min(1.0, 1.0
        / (temporal.sceneOutputExtent.z * temporal.sceneOutputExtent.x));
    feedback += (1.0 - feedback) * feedback * (1.0 - 0.5 * sceneShare)
        * (1.0 - smoothstep(0.05, 0.5, speed));
    float historyWeight = historyUsable
        ? feedback * (1.0 - reactive)
        : 0.0;
    // Luminance-weighted blend: one bright jittered sample cannot flicker.
    float currentWeight = (1.0 - historyWeight) / (1.0 + Luma(current.rgb));
    float historyBlend = historyWeight / (1.0 + Luma(historyClamped));
    vec3 resolved = (current.rgb * currentWeight + historyClamped * historyBlend)
        / max(currentWeight + historyBlend, 0.0001);
    if ((int(temporal.motionParams.w + 0.5) & 16) != 0
            && temporal.reactiveRect1.x > 0.0) {
        resolved = SharpenPresent(StoredTextureUV(sceneTextureUV, 2),
            resolved, clamp(temporal.reactiveRect1.x, 0.0, 1.0));
    }
    if (temporal.temporalParams.w > 0.5
            && temporal.temporalParams.w < 1.5) {
        float magnitude = clamp(length(velocityPixels) / 32.0, 0.0, 1.0);
        vec2 direction = clamp(velocityPixels / 32.0,
            vec2(-1.0), vec2(1.0));
        resolved = vec3(direction * 0.5 + 0.5, 1.0) * magnitude;
    } else if (temporal.temporalParams.w > 1.5
            && temporal.temporalParams.w < 2.5) {
        resolved = vec3(reactive, reactive * 0.25, 0.0);
    } else if (temporal.temporalParams.w > 2.5) {
        resolved = vec3(historyWeight);
    }
    outColor = vec4(resolved, current.a);
}
