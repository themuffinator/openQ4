!!ARBfp1.0
OPTION ARB_precision_hint_fastest;

# local[0]: xy = 1x texel offsets
# local[1]: xy = 2x texel offsets
# local[2]: xy = 4x texel offsets

TEMP centerDepth, sampleDepth, tc;
TEMP level1, level2, level3;

TEX centerDepth, fragment.texcoord[0], texture[0], 2D;
MOV level1, centerDepth.xxxx;
MOV level2, centerDepth.xxxx;
MOV level3, centerDepth.xxxx;

# 1x footprint min
ADD tc.xy, fragment.texcoord[0], program.local[0];
TEX sampleDepth, tc, texture[0], 2D;
MIN level1, level1, sampleDepth.xxxx;
SUB tc.xy, fragment.texcoord[0], program.local[0];
TEX sampleDepth, tc, texture[0], 2D;
MIN level1, level1, sampleDepth.xxxx;

# 2x footprint min
ADD tc.xy, fragment.texcoord[0], program.local[1];
TEX sampleDepth, tc, texture[0], 2D;
MIN level2, level1, sampleDepth.xxxx;
SUB tc.xy, fragment.texcoord[0], program.local[1];
TEX sampleDepth, tc, texture[0], 2D;
MIN level2, level2, sampleDepth.xxxx;

# 4x footprint min
ADD tc.xy, fragment.texcoord[0], program.local[2];
TEX sampleDepth, tc, texture[0], 2D;
MIN level3, level2, sampleDepth.xxxx;
SUB tc.xy, fragment.texcoord[0], program.local[2];
TEX sampleDepth, tc, texture[0], 2D;
MIN level3, level3, sampleDepth.xxxx;

# Pack hierarchy bands:
#   r = 1x min (SSR primary lookup)
#   g = 2x min
#   b = 4x min
#   a = center depth
MOV result.color.r, level1.x;
MOV result.color.g, level2.x;
MOV result.color.b, level3.x;
MOV result.color.a, centerDepth.x;

END
