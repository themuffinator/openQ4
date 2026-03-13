uniform vec4 uModelMatrixRow0;
uniform vec4 uModelMatrixRow1;
uniform vec4 uModelMatrixRow2;
uniform vec4 uGlobalLightOrigin;

varying vec3 vPointShadowVector;

void main() {
	vec4 position = gl_Vertex;
	vec3 worldPos = vec3(
		dot( position, uModelMatrixRow0 ),
		dot( position, uModelMatrixRow1 ),
		dot( position, uModelMatrixRow2 ) );
	vPointShadowVector = worldPos - uGlobalLightOrigin.xyz;
	gl_Position = ftransform();
}
