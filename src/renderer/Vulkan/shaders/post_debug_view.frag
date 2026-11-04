#version 450

// r_showIntensity and r_showDepth. OpenGL reads the framebuffer back, recolours
// it on the CPU and draws it again with glDrawPixels (RB_ShowIntensity,
// RB_ShowDepthBuffer); Vulkan does the same mapping in one full-screen pass.
//
// Intensity: the brightest channel j (0-255) shades from red at 0 through
// green at 128 to blue at 255, exactly RB_ShowIntensity's byte ramp (black
// wraps to zero). Depth is clamped to [0,1] and shown as grey, matching
// RB_ShowDepthBuffer's float-to-byte conversion. fragUV is OpenGL's texture
// coordinate; see post_ssao.frag.

layout(set = 0, binding = 0) uniform sampler2D Source;

layout(std140, set = 6, binding = 0) uniform DebugViewBlock {
    vec4 params;			// x: 0 intensity, 1 depth
} block;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

void main() {
	vec4 source = texture( Source, fragUV );
	if ( block.params.x > 0.5 ) {
		outColor = vec4( vec3( clamp( source.x, 0.0, 1.0 ) ), 1.0 );
		return;
	}
	// the CPU ramp works on the bytes glReadPixels clamps the frame to
	float j = floor( clamp( max( max( source.r, source.g ), source.b ), 0.0, 1.0 ) * 255.0 + 0.5 );
	vec3 color;
	if ( j < 128.0 ) {
		color = vec3( 2.0 * ( 128.0 - j ), 2.0 * j, 0.0 );
	} else {
		color = vec3( 0.0, 2.0 * ( 255.0 - j ), 2.0 * ( j - 128.0 ) );
	}
	// the ramp is stored into bytes, so 2 * 128 at j = 0 wraps to black
	outColor = vec4( mod( color, 256.0 ) / 255.0, 1.0 );
}
