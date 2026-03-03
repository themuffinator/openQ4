!!ARBfp1.0
OPTION ARB_precision_hint_fastest;

# local[0]: x = exposure, y = invGamma, z = shoulderScale

PARAM ONE = { 1.0, 1.0, 1.0, 1.0 };

TEMP color, mapped, denom;

TEX color, fragment.texcoord[0], texture[0], 2D;
MUL color, color, program.local[0].xxxx;

MAD denom, color, program.local[0].zzzz, ONE;
RCP denom.r, denom.r;
RCP denom.g, denom.g;
RCP denom.b, denom.b;
MUL mapped.r, color.r, denom.r;
MUL mapped.g, color.g, denom.g;
MUL mapped.b, color.b, denom.b;

POW mapped.r, mapped.r, program.local[0].y;
POW mapped.g, mapped.g, program.local[0].y;
POW mapped.b, mapped.b, program.local[0].y;

MOV result.color, mapped;
MOV result.color.a, ONE.x;

END
