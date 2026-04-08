#version 110

uniform vec4 uAlphaTexCoordS;
uniform vec4 uAlphaTexCoordT;
uniform vec4 uCoverageTexCoordS;
uniform vec4 uCoverageTexCoordT;
uniform vec2 uVertexAlphaParams;
uniform vec2 uCoverageVertexAlphaParams;

varying vec2 vAlphaTexCoord;
varying vec2 vCoverageTexCoord;
varying vec3 vVertexColorRgb;
varying float vVertexAlpha;
varying float vCoverageVertexAlpha;

void main() {
	vec4 alphaTexCoord = vec4( gl_MultiTexCoord0.xy, 0.0, 1.0 );
	vAlphaTexCoord = vec2( dot( alphaTexCoord, uAlphaTexCoordS ), dot( alphaTexCoord, uAlphaTexCoordT ) );
	vCoverageTexCoord = vec2( dot( alphaTexCoord, uCoverageTexCoordS ), dot( alphaTexCoord, uCoverageTexCoordT ) );
	vVertexColorRgb = gl_Color.rgb;
	vVertexAlpha = clamp( gl_Color.a * uVertexAlphaParams.x + uVertexAlphaParams.y, 0.0, 1.0 );
	vCoverageVertexAlpha = clamp( gl_Color.a * uCoverageVertexAlphaParams.x + uCoverageVertexAlphaParams.y, 0.0, 1.0 );
	gl_Position = ftransform();
}
