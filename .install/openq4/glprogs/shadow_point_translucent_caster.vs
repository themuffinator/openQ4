#version 110

uniform vec4 uModelMatrixRow0;
uniform vec4 uModelMatrixRow1;
uniform vec4 uModelMatrixRow2;
uniform vec4 uGlobalLightOrigin;
uniform vec4 uAlphaTexCoordS;
uniform vec4 uAlphaTexCoordT;
uniform vec4 uCoverageTexCoordS;
uniform vec4 uCoverageTexCoordT;
uniform vec2 uVertexAlphaParams;
uniform vec2 uCoverageVertexAlphaParams;

varying vec3 vPointShadowVector;
varying vec2 vAlphaTexCoord;
varying vec2 vCoverageTexCoord;
varying vec3 vVertexColorRgb;
varying float vVertexAlpha;
varying float vCoverageVertexAlpha;

void main() {
	vec4 position = gl_Vertex;
	vec4 alphaTexCoord = vec4( gl_MultiTexCoord0.xy, 0.0, 1.0 );
	vec3 worldPos = vec3(
		dot( position, uModelMatrixRow0 ),
		dot( position, uModelMatrixRow1 ),
		dot( position, uModelMatrixRow2 ) );
	vPointShadowVector = worldPos - uGlobalLightOrigin.xyz;
	vAlphaTexCoord = vec2( dot( alphaTexCoord, uAlphaTexCoordS ), dot( alphaTexCoord, uAlphaTexCoordT ) );
	vCoverageTexCoord = vec2( dot( alphaTexCoord, uCoverageTexCoordS ), dot( alphaTexCoord, uCoverageTexCoordT ) );
	vVertexColorRgb = gl_Color.rgb;
	vVertexAlpha = clamp( gl_Color.a * uVertexAlphaParams.x + uVertexAlphaParams.y, 0.0, 1.0 );
	vCoverageVertexAlpha = clamp( gl_Color.a * uCoverageVertexAlphaParams.x + uCoverageVertexAlphaParams.y, 0.0, 1.0 );
	gl_Position = ftransform();
}
