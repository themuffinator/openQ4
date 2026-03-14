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
uniform vec4 uShadowRow0[4];
uniform vec4 uShadowRow1[4];
uniform vec4 uShadowRow2[4];
uniform vec4 uShadowRow3[4];
uniform vec2 uVertexColorParams;

varying vec2 vBumpTexCoord;
varying vec2 vDiffuseTexCoord;
varying vec2 vSpecularTexCoord;
varying vec4 vLightFalloffTexCoord;
varying vec4 vLightProjectionTexCoord;
varying vec3 vLightVector;
varying vec3 vHalfAngleVector;
varying vec4 vShadowCoord0;
varying vec4 vShadowCoord1;
varying vec4 vShadowCoord2;
varying vec4 vShadowCoord3;
varying vec3 vVertexColor;
varying float vShadowLightCos;
varying float vViewDepth;

vec3 TangentSpaceVector( vec3 objectVector ) {
	return vec3(
		dot( attr_Tangent, objectVector ),
		dot( attr_Bitangent, objectVector ),
		dot( attr_Normal, objectVector ) );
}

vec4 BuildShadowCoord( vec4 position, int index ) {
	return vec4(
		dot( position, uShadowRow0[index] ),
		dot( position, uShadowRow1[index] ),
		dot( position, uShadowRow2[index] ),
		dot( position, uShadowRow3[index] ) );
}

void main() {
	vec4 position = gl_Vertex;
	vec4 texCoord = vec4( attr_TexCoord0.xy, 0.0, 1.0 );
	vec4 viewPosition = gl_ModelViewMatrix * position;

	vec3 toLight = uLocalLightOrigin.xyz - position.xyz;
	vec3 toView = uLocalViewOrigin.xyz - position.xyz;
	vec3 localLightDir = normalize( toLight );

	vLightVector = TangentSpaceVector( toLight );
	vHalfAngleVector = TangentSpaceVector( normalize( toLight ) + normalize( toView ) );

	vBumpTexCoord = vec2( dot( texCoord, uBumpMatrixS ), dot( texCoord, uBumpMatrixT ) );
	vDiffuseTexCoord = vec2( dot( texCoord, uDiffuseMatrixS ), dot( texCoord, uDiffuseMatrixT ) );
	vSpecularTexCoord = vec2( dot( texCoord, uSpecularMatrixS ), dot( texCoord, uSpecularMatrixT ) );

	vLightFalloffTexCoord = vec4( dot( position, uLightFalloffS ), 0.5, 0.0, 1.0 );
	vLightProjectionTexCoord = vec4(
		dot( position, uLightProjectionS ),
		dot( position, uLightProjectionT ),
		0.0,
		dot( position, uLightProjectionQ ) );
	vShadowCoord0 = BuildShadowCoord( position, 0 );
	vShadowCoord1 = BuildShadowCoord( position, 1 );
	vShadowCoord2 = BuildShadowCoord( position, 2 );
	vShadowCoord3 = BuildShadowCoord( position, 3 );

	vVertexColor = gl_Color.rgb * uVertexColorParams.x + vec3( uVertexColorParams.y );
	vShadowLightCos = max( dot( normalize( attr_Normal ), localLightDir ), 0.0 );
	vViewDepth = max( -viewPosition.z, 0.0 );

	gl_Position = ftransform();
}
