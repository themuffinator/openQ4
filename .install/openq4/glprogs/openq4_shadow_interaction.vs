uniform vec4 uShadowRow0;
uniform vec4 uShadowRow1;
uniform vec4 uShadowRow2;
uniform vec4 uShadowRow3;
varying vec4 vShadowCoord;

void main() {
	vec4 position = gl_Vertex;
	vShadowCoord = vec4(
		dot( position, uShadowRow0 ),
		dot( position, uShadowRow1 ),
		dot( position, uShadowRow2 ),
		dot( position, uShadowRow3 ) );

	gl_Position = ftransform();
}
