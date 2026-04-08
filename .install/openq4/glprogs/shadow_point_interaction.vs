#version 110

attribute vec2 attr_TexCoord0;
attribute vec3 attr_Tangent;
attribute vec3 attr_Bitangent;
attribute vec3 attr_Normal;

uniform vec4 uLocalLightOrigin;
uniform vec4 uLocalViewOrigin;
uniform vec4 uLightProjectionS;
uniform vec4 uLightProjectionT;
uniform vec4 uLightProjectionQ;
uniform vec4 uLightFalloffS;
uniform vec4 uBumpMatrixS;
uniform vec4 uBumpMatrixT;
uniform vec4 uDiffuseMatrixS;
uniform vec4 uDiffuseMatrixT;
uniform vec4 uSpecularMatrixS;
uniform vec4 uSpecularMatrixT;
uniform vec4 uModelMatrixRow0;
uniform vec4 uModelMatrixRow1;
uniform vec4 uModelMatrixRow2;
uniform vec4 uGlobalLightOrigin;
uniform vec2 uVertexColorParams;

varying vec2 vBumpTexCoord;
varying vec2 vDiffuseTexCoord;
varying vec2 vSpecularTexCoord;
varying vec4 vLightFalloffTexCoord;
varying vec4 vLightProjectionTexCoord;
varying vec3 vLightVector;
varying vec3 vHalfAngleVector;
varying vec3 vPointShadowVector;
varying vec3 vVertexColor;
varying float vShadowLightCos;

vec3 TangentSpaceVector( vec3 objectVector ) {
	return vec3(
		dot( attr_Tangent, objectVector ),
		dot( attr_Bitangent, objectVector ),
		dot( attr_Normal, objectVector ) );
}

void main() {
	vec4 position = gl_Vertex;
	vec4 texCoord = vec4( attr_TexCoord0.xy, 0.0, 1.0 );

	vec3 toLight = uLocalLightOrigin.xyz - position.xyz;
	vec3 toView = uLocalViewOrigin.xyz - position.xyz;
	vec3 localLightDir = normalize( toLight );
	vec3 worldPos = vec3(
		dot( position, uModelMatrixRow0 ),
		dot( position, uModelMatrixRow1 ),
		dot( position, uModelMatrixRow2 ) );

	vLightVector = TangentSpaceVector( toLight );
	vHalfAngleVector = TangentSpaceVector( normalize( toLight ) + normalize( toView ) );
	vPointShadowVector = worldPos - uGlobalLightOrigin.xyz;

	vBumpTexCoord = vec2( dot( texCoord, uBumpMatrixS ), dot( texCoord, uBumpMatrixT ) );
	vDiffuseTexCoord = vec2( dot( texCoord, uDiffuseMatrixS ), dot( texCoord, uDiffuseMatrixT ) );
	vSpecularTexCoord = vec2( dot( texCoord, uSpecularMatrixS ), dot( texCoord, uSpecularMatrixT ) );

	vLightFalloffTexCoord = vec4( dot( position, uLightFalloffS ), 0.5, 0.0, 1.0 );
	vLightProjectionTexCoord = vec4(
		dot( position, uLightProjectionS ),
		dot( position, uLightProjectionT ),
		0.0,
		dot( position, uLightProjectionQ ) );

	vVertexColor = gl_Color.rgb * uVertexColorParams.x + vec3( uVertexColorParams.y );
	vShadowLightCos = max( dot( normalize( attr_Normal ), localLightDir ), 0.0 );

	gl_Position = ftransform();
}
