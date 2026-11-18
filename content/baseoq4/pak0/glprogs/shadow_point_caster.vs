#version 110

uniform vec4 uModelMatrixRow0;
uniform vec4 uModelMatrixRow1;
uniform vec4 uModelMatrixRow2;
uniform vec4 uGlobalLightOrigin;
uniform vec4 uAlphaTexCoordS;
uniform vec4 uAlphaTexCoordT;

#ifdef OPENQ4_POINT_SHADOW_LAYERED
// the engine draws all six faces into one layered cube target and names the
// face here (ARB_shader_viewport_layer_array or AMD_vertex_shader_layer)
uniform int uPointShadowLayer;
#endif

varying vec3 vPointShadowVector;
varying vec2 vAlphaTexCoord;
varying vec3 vAlphaHashCoord;

void main() {
	vec4 position = gl_Vertex;
	vec4 alphaTexCoord = vec4( gl_MultiTexCoord0.xy, 0.0, 1.0 );
	vec3 worldPos = vec3(
		dot( position, uModelMatrixRow0 ),
		dot( position, uModelMatrixRow1 ),
		dot( position, uModelMatrixRow2 ) );
	vPointShadowVector = worldPos - uGlobalLightOrigin.xyz;
	vAlphaTexCoord = vec2( dot( alphaTexCoord, uAlphaTexCoordS ), dot( alphaTexCoord, uAlphaTexCoordT ) );
	vAlphaHashCoord = worldPos;
	gl_Position = ftransform();
#ifdef OPENQ4_POINT_SHADOW_LAYERED
	gl_Layer = uPointShadowLayer;
#endif
}
