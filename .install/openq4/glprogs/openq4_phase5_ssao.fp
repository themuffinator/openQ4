!!ARBfp1.0
OPTION ARB_precision_hint_fastest;

# local[0]: x = strength, y = depthBias, z = minVisibility, w = depthScale
# local[1]: xy = horizontal sample offset
# local[2]: xy = vertical sample offset

PARAM ONE = { 1.0, 1.0, 1.0, 1.0 };
PARAM ZERO = { 0.0, 0.0, 0.0, 0.0 };

TEMP color, centerDepth, sampleDepth, tc, diff, occlusion, visibility;

TEX color, fragment.texcoord[0], texture[0], 2D;
TEX centerDepth, fragment.texcoord[0], texture[1], 2D;

MOV occlusion, ZERO;

ADD tc.xy, fragment.texcoord[0], program.local[1];
TEX sampleDepth, tc, texture[1], 2D;
SUB diff, centerDepth.xxxx, sampleDepth.xxxx;
SUB diff, diff, program.local[0].yyyy;
MAX diff, diff, ZERO;
ADD occlusion, occlusion, diff;

SUB tc.xy, fragment.texcoord[0], program.local[1];
TEX sampleDepth, tc, texture[1], 2D;
SUB diff, centerDepth.xxxx, sampleDepth.xxxx;
SUB diff, diff, program.local[0].yyyy;
MAX diff, diff, ZERO;
ADD occlusion, occlusion, diff;

ADD tc.xy, fragment.texcoord[0], program.local[2];
TEX sampleDepth, tc, texture[1], 2D;
SUB diff, centerDepth.xxxx, sampleDepth.xxxx;
SUB diff, diff, program.local[0].yyyy;
MAX diff, diff, ZERO;
ADD occlusion, occlusion, diff;

SUB tc.xy, fragment.texcoord[0], program.local[2];
TEX sampleDepth, tc, texture[1], 2D;
SUB diff, centerDepth.xxxx, sampleDepth.xxxx;
SUB diff, diff, program.local[0].yyyy;
MAX diff, diff, ZERO;
ADD occlusion, occlusion, diff;

MUL occlusion, occlusion, program.local[0].wwww;
MUL occlusion, occlusion, program.local[0].xxxx;
MIN occlusion, occlusion, ONE;

SUB visibility, ONE, occlusion;
MAX visibility, visibility, program.local[0].zzzz;

MUL color, color, visibility;
MOV color.a, ONE.x;
MOV result.color, color;

END
