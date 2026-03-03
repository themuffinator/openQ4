!!ARBfp1.0
OPTION ARB_precision_hint_fastest;

# local[0]: x = strength, y = rayStepX, z = rayStepY, w = depthScale
# local[1]: x = edgeFadeScale, y = hizBias, z = fresnelScale, w = normalScale
# local[2]: x = normalSampleStepX, y = normalSampleStepY

PARAM ONE = { 1.0, 1.0, 1.0, 1.0 };
PARAM ZERO = { 0.0, 0.0, 0.0, 0.0 };
PARAM TWO = { 2.0, 2.0, 2.0, 2.0 };
PARAM THREE = { 3.0, 3.0, 3.0, 3.0 };
PARAM FOUR = { 4.0, 4.0, 4.0, 4.0 };
PARAM EPS = { 0.001, 0.001, 0.001, 0.001 };

TEMP baseColor, centerDepth;
TEMP depthL, depthR, depthU, depthD;
TEMP tc, n, len, fresnel, rayStep;
TEMP accum, sumW, sampleDepth, hizDepth, weight, sampleColor;
TEMP reflColor, blend, edge, tmp;

TEX baseColor, fragment.texcoord[0], texture[0], 2D;
TEX centerDepth, fragment.texcoord[0], texture[1], 2D;

# reconstruct a simple view-space normal estimate from depth derivatives
MOV tc, fragment.texcoord[0];
SUB tc.x, tc.x, program.local[2].x;
TEX depthL, tc, texture[1], 2D;
MOV tc, fragment.texcoord[0];
ADD tc.x, tc.x, program.local[2].x;
TEX depthR, tc, texture[1], 2D;
MOV tc, fragment.texcoord[0];
SUB tc.y, tc.y, program.local[2].y;
TEX depthU, tc, texture[1], 2D;
MOV tc, fragment.texcoord[0];
ADD tc.y, tc.y, program.local[2].y;
TEX depthD, tc, texture[1], 2D;

SUB n.x, depthR.x, depthL.x;
SUB n.y, depthD.x, depthU.x;
MUL n.xy, n.xy, program.local[1].wwww;
SUB n.xy, ZERO, n;
MOV n.z, ONE.x;
MOV n.w, ZERO.x;
DP3 len.x, n, n;
RSQ len.x, len.x;
MUL n, n, len.xxxx;

# Schlick-style fresnel approximation using pow5(1 - N.z)
SUB fresnel.x, ONE.x, n.z;
MUL fresnel.y, fresnel.x, fresnel.x;
MUL fresnel.z, fresnel.y, fresnel.y;
MUL fresnel.x, fresnel.z, fresnel.x;
MUL fresnel.x, fresnel.x, program.local[1].z;

MUL rayStep.xy, n.xy, n.zzzz;
MUL rayStep.xy, rayStep.xy, TWO.xx;
MUL rayStep.x, rayStep.x, program.local[0].y;
MUL rayStep.y, rayStep.y, program.local[0].z;

MOV accum, ZERO;
MOV sumW, EPS;

# sample 1
ADD tc.xy, fragment.texcoord[0], rayStep.xyxx;
TEX sampleDepth, tc, texture[1], 2D;
TEX hizDepth, tc, texture[2], 2D;
TEX sampleColor, tc, texture[0], 2D;
SUB weight.x, sampleDepth.x, centerDepth.x;
ABS weight.x, weight.x;
MAD weight.x, -weight.x, program.local[0].w, ONE.x;
MAX weight.x, weight.x, ZERO.x;
SUB tmp.x, centerDepth.x, hizDepth.x;
SUB tmp.x, tmp.x, program.local[1].y;
MAD tmp.x, -tmp.x, program.local[0].w, ONE.x;
MAX tmp.x, tmp.x, ZERO.x;
MUL weight.x, weight.x, tmp.x;
MAD accum, sampleColor, weight.xxxx, accum;
ADD sumW.x, sumW.x, weight.x;

# sample 2
MAD tc.xy, rayStep.xyxx, TWO.xx, fragment.texcoord[0];
TEX sampleDepth, tc, texture[1], 2D;
TEX hizDepth, tc, texture[2], 2D;
TEX sampleColor, tc, texture[0], 2D;
SUB weight.x, sampleDepth.x, centerDepth.x;
ABS weight.x, weight.x;
MAD weight.x, -weight.x, program.local[0].w, ONE.x;
MAX weight.x, weight.x, ZERO.x;
SUB tmp.x, centerDepth.x, hizDepth.x;
SUB tmp.x, tmp.x, program.local[1].y;
MAD tmp.x, -tmp.x, program.local[0].w, ONE.x;
MAX tmp.x, tmp.x, ZERO.x;
MUL weight.x, weight.x, tmp.x;
MAD accum, sampleColor, weight.xxxx, accum;
ADD sumW.x, sumW.x, weight.x;

# sample 3
MAD tc.xy, rayStep.xyxx, THREE.xx, fragment.texcoord[0];
TEX sampleDepth, tc, texture[1], 2D;
TEX hizDepth, tc, texture[2], 2D;
TEX sampleColor, tc, texture[0], 2D;
SUB weight.x, sampleDepth.x, centerDepth.x;
ABS weight.x, weight.x;
MAD weight.x, -weight.x, program.local[0].w, ONE.x;
MAX weight.x, weight.x, ZERO.x;
SUB tmp.x, centerDepth.x, hizDepth.x;
SUB tmp.x, tmp.x, program.local[1].y;
MAD tmp.x, -tmp.x, program.local[0].w, ONE.x;
MAX tmp.x, tmp.x, ZERO.x;
MUL weight.x, weight.x, tmp.x;
MAD accum, sampleColor, weight.xxxx, accum;
ADD sumW.x, sumW.x, weight.x;

# sample 4
MAD tc.xy, rayStep.xyxx, FOUR.xx, fragment.texcoord[0];
TEX sampleDepth, tc, texture[1], 2D;
TEX hizDepth, tc, texture[2], 2D;
TEX sampleColor, tc, texture[0], 2D;
SUB weight.x, sampleDepth.x, centerDepth.x;
ABS weight.x, weight.x;
MAD weight.x, -weight.x, program.local[0].w, ONE.x;
MAX weight.x, weight.x, ZERO.x;
SUB tmp.x, centerDepth.x, hizDepth.x;
SUB tmp.x, tmp.x, program.local[1].y;
MAD tmp.x, -tmp.x, program.local[0].w, ONE.x;
MAX tmp.x, tmp.x, ZERO.x;
MUL weight.x, weight.x, tmp.x;
MAD accum, sampleColor, weight.xxxx, accum;
ADD sumW.x, sumW.x, weight.x;

RCP sumW.x, sumW.x;
MUL reflColor, accum, sumW.xxxx;

# edge attenuation
SUB edge.xy, ONE, fragment.texcoord[0];
MIN edge.x, fragment.texcoord[0].x, edge.x;
MIN edge.y, fragment.texcoord[0].y, edge.y;
MIN edge.x, edge.x, edge.y;
MUL edge.x, edge.x, program.local[1].x;
MIN edge.x, edge.x, ONE.x;
MAX edge.x, edge.x, ZERO.x;

MUL blend.x, fresnel.x, program.local[0].x;
MUL blend.x, blend.x, edge.x;
MIN blend.x, blend.x, ONE.x;

SUB tmp, reflColor, baseColor;
MAD result.color, tmp, blend.xxxx, baseColor;
MOV result.color.a, ONE.x;

END
