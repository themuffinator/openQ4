#version 450

// Portal-sky mask (vk_PostProcess.cpp VK_PostProcess_PortalSkyMask). Draws
// over the backdrop view before any of its passes and writes its depth: 1
// where the main view's single-sample world depth, at this pixel or one of its
// eight neighbours, is still the cleared far value, so the backdrop can show;
// 0 elsewhere, so every depth-tested pass of the backdrop rejects the pixel.
// The neighbours cover multisampled silhouette pixels whose centre is world
// but some samples are not. Colour is untouched (the pipeline blends ZERO,
// ONE). fragUV is OpenGL's texture coordinate, as for every post pass, and
// the world depth is a bottom-up capture of the view's rectangle.

layout(set = 0, binding = 0) uniform sampler2D WorldDepth;

layout(std140, set = 6, binding = 0) uniform SkyMaskBlock {
    vec4 texInfo;			// xy: one over the view size
} block;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

void main() {
	float backdrop = 0.0;
	for ( int y = -1; y <= 1; ++y ) {
		for ( int x = -1; x <= 1; ++x ) {
			if ( texture( WorldDepth, fragUV + vec2( x, y ) * block.texInfo.xy ).x >= 1.0 ) {
				backdrop = 1.0;
			}
		}
	}
	gl_FragDepth = backdrop;
	outColor = vec4( 0.0 );
}
