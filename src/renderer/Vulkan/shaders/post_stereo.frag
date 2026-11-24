#version 450

// Desktop stereoscopic 3D (r_stereo3D): composites a frame's two eyes and
// its screen layer (menus, HUD, console: premultiplied alpha) into the
// window. RB_DesktopStereo_Composite (draw_common.cpp) is the OpenGL twin,
// line for line.
//
// The three inputs are executor captures, stored bottom-up like OpenGL
// textures, so the composite works in OpenGL's orientation: uv (0,0) is the
// window's bottom-left. fragUV follows image-memory order (0,0 top-left), so
// V flips once here.

layout(set = 0, binding = 0) uniform sampler2D LeftEye;
layout(set = 1, binding = 0) uniform sampler2D RightEye;
layout(set = 2, binding = 0) uniform sampler2D ScreenLayer;

layout(std140, set = 6, binding = 0) uniform StereoBlock {
    vec4 params;	// x: stereo3DLayout_t, y: window height, z: eye height, w: 1 when the eyes were drawn
    vec4 screen;	// x: 1 when the screen layer was drawn
} block;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

vec3 Over( vec4 layer, vec3 below ) {
    return layer.rgb + below * ( 1.0 - layer.a );
}

vec3 ToLinear( vec3 c ) {
    return pow( max( c, vec3( 0.0 ) ), vec3( 2.2 ) );
}

vec3 FromLinear( vec3 c ) {
    return pow( clamp( c, 0.0, 1.0 ), vec3( 1.0 / 2.2 ) );
}

void main() {
    vec2 uv = vec2( fragUV.x, 1.0 - fragUV.y );
    float mode = block.params.x;
    float eyes = block.params.w;
    float screenWeight = block.screen.x;

    if ( mode > 3.5 && mode < 4.5 ) {
        // anaglyph: each eye under the screen layer, then Dubois' red/cyan
        // least-squares projection in linear light
        vec4 layer = texture( ScreenLayer, uv ) * screenWeight;
        vec3 left = ToLinear( Over( layer, texture( LeftEye, uv ).rgb * eyes ) );
        vec3 right = ToLinear( Over( layer, texture( RightEye, uv ).rgb * eyes ) );
        vec3 color;
        color.r = dot( vec3( 0.456, 0.500, 0.176 ), left ) + dot( vec3( -0.043, -0.088, -0.002 ), right );
        color.g = dot( vec3( -0.040, -0.038, -0.016 ), left ) + dot( vec3( 0.378, 0.734, -0.018 ), right );
        color.b = dot( vec3( -0.015, -0.021, -0.005 ), left ) + dot( vec3( -0.072, -0.113, 1.226 ), right );
        outColor = vec4( FromLinear( color ), 1.0 );
        return;
    }

    vec2 eyeUV = uv;
    vec2 screenUV = uv;
    float rightEye = 0.0;
    if ( mode < 2.5 ) {
        // side by side: the left eye on the left, the screen layer in each half
        rightEye = step( 0.5, uv.x );
        eyeUV.x = uv.x * 2.0 - rightEye;
        screenUV = eyeUV;
    } else if ( mode < 3.5 ) {
        // top and bottom: the left eye on top
        rightEye = 1.0 - step( 0.5, uv.y );
        eyeUV.y = uv.y * 2.0 - ( 1.0 - rightEye );
        screenUV = eyeUV;
    } else {
        // row interlaced: window rows counted from the top, the left eye on
        // even ones; each eye holds every other row
        float row = floor( ( 1.0 - uv.y ) * block.params.y );
        rightEye = mod( row, 2.0 );
        eyeUV.y = 1.0 - ( floor( row * 0.5 ) + 0.5 ) / block.params.z;
    }
    vec3 eye = mix( texture( LeftEye, eyeUV ).rgb, texture( RightEye, eyeUV ).rgb, rightEye ) * eyes;
    outColor = vec4( Over( texture( ScreenLayer, screenUV ) * screenWeight, eye ), 1.0 );
}
