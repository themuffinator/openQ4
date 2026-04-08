#version 110

uniform vec4 uAlphaTexCoordS;
uniform vec4 uAlphaTexCoordT;

varying vec2 vAlphaTexCoord;

void main() {
	vec4 alphaTexCoord = vec4( gl_MultiTexCoord0.xy, 0.0, 1.0 );
	vAlphaTexCoord = vec2( dot( alphaTexCoord, uAlphaTexCoordS ), dot( alphaTexCoord, uAlphaTexCoordT ) );
	gl_Position = ftransform();
}
