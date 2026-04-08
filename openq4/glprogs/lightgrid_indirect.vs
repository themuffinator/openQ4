#version 110

uniform vec4 uBumpMatrixS;
uniform vec4 uBumpMatrixT;
uniform vec4 uDiffuseMatrixS;
uniform vec4 uDiffuseMatrixT;
uniform vec4 uModelMatrixRow0;
uniform vec4 uModelMatrixRow1;
uniform vec4 uModelMatrixRow2;
uniform vec2 uVertexColorParams;

varying vec2 vBumpTexCoord;
varying vec2 vDiffuseTexCoord;
varying vec3 vWorldTangent;
varying vec3 vWorldBitangent;
varying vec3 vWorldNormal;
varying vec3 vWorldPosition;
varying vec3 vVertexColor;

vec3 TransformVectorToWorld( vec3 localVector ) {
	return vec3(
		dot( localVector, uModelMatrixRow0.xyz ),
		dot( localVector, uModelMatrixRow1.xyz ),
		dot( localVector, uModelMatrixRow2.xyz ) );
}

void main() {
	vec4 baseTexCoord = gl_MultiTexCoord0;

	vBumpTexCoord = vec2( dot( baseTexCoord, uBumpMatrixS ), dot( baseTexCoord, uBumpMatrixT ) );
	vDiffuseTexCoord = vec2( dot( baseTexCoord, uDiffuseMatrixS ), dot( baseTexCoord, uDiffuseMatrixT ) );

	vWorldTangent = normalize( TransformVectorToWorld( gl_MultiTexCoord1.xyz ) );
	vWorldBitangent = normalize( TransformVectorToWorld( gl_MultiTexCoord2.xyz ) );
	vWorldNormal = normalize( TransformVectorToWorld( gl_Normal.xyz ) );

	vWorldPosition = vec3(
		dot( gl_Vertex, uModelMatrixRow0 ),
		dot( gl_Vertex, uModelMatrixRow1 ),
		dot( gl_Vertex, uModelMatrixRow2 ) );

	vVertexColor = gl_Color.rgb * uVertexColorParams.x + vec3( uVertexColorParams.y );

	gl_Position = ftransform();
}
