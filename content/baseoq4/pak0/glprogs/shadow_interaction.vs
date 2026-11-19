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
uniform vec4 uShadowRow0[4];
uniform vec4 uShadowRow1[4];
uniform vec4 uShadowRow2[4];
uniform vec4 uShadowRow3[4];
uniform float uShadowNormalOffsetWorld[4];
uniform vec2 uVertexColorParams;

varying vec2 vBumpTexCoord;
varying vec2 vDiffuseTexCoord;
varying vec2 vSpecularTexCoord;
varying vec4 vLightFalloffTexCoord;
varying vec4 vLightProjectionTexCoord;
varying vec3 vLightVector;
varying vec3 vHalfAngleVector;
varying vec3 vViewVector;
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

// Normal-offset shadows: evaluate the shadow projection at a point pushed
// along the geometric normal by (world texel size * slope). Shifting the
// sampled point off the surface fixes self-shadow acne structurally where
// pure depth-space bias would have to grow until contact shadows detach.
// The shadow rows are localized planes, so the offset is model-space.
// A negative uShadowNormalOffsetWorld is the offset per unit of the
// receiver's light depth (row 3): a distant point light's texel grows with
// distance from its source (R_ShadowMapProjectedNormalOffsetWorld).
vec4 BuildShadowCoord( vec4 position, vec3 normalOffsetDir, float sinTheta, int index ) {
	float offsetWorld = uShadowNormalOffsetWorld[index];
	if ( offsetWorld < 0.0 ) {
		offsetWorld = -offsetWorld * max( dot( position, uShadowRow3[index] ), 0.0 );
	}
	vec4 offsetPosition = vec4( position.xyz + normalOffsetDir * ( offsetWorld * sinTheta ), 1.0 );
	return vec4(
		dot( offsetPosition, uShadowRow0[index] ),
		dot( offsetPosition, uShadowRow1[index] ),
		dot( offsetPosition, uShadowRow2[index] ),
		dot( offsetPosition, uShadowRow3[index] ) );
}

#ifdef OPENQ4_PBR
// Native PBR (draw_pbr.cpp) shades in object space with an orthonormal vertex
// frame, exactly as Vulkan does; that frame helper is appended to this source.
void PBRVertexFrame( vec3 normal, vec3 tangent, vec3 bitangent );
#endif

void main() {
	vec4 position = gl_Vertex;
	vec4 texCoord = vec4( attr_TexCoord0.xy, 0.0, 1.0 );
	vec4 viewPosition = gl_ModelViewMatrix * position;

	vec3 toLight = uLocalLightOrigin.xyz - position.xyz;
	vec3 toView = uLocalViewOrigin.xyz - position.xyz;
	vec3 localLightDir = normalize( toLight );

#ifdef OPENQ4_PBR
	// Object-space light and eye vectors: transforming them through the
	// vertex tangent frame before interpolation distorts their directions
	// across a curved triangle.
	vLightVector = toLight;
	vHalfAngleVector = vec3( 0.0 );
	vViewVector = toView;
	PBRVertexFrame( attr_Normal, attr_Tangent, attr_Bitangent );
#else
	vLightVector = TangentSpaceVector( toLight );
	vHalfAngleVector = TangentSpaceVector( normalize( toLight ) + normalize( toView ) );
	vViewVector = TangentSpaceVector( toView );
#endif

	vBumpTexCoord = vec2( dot( texCoord, uBumpMatrixS ), dot( texCoord, uBumpMatrixT ) );
	vDiffuseTexCoord = vec2( dot( texCoord, uDiffuseMatrixS ), dot( texCoord, uDiffuseMatrixT ) );
	vSpecularTexCoord = vec2( dot( texCoord, uSpecularMatrixS ), dot( texCoord, uSpecularMatrixT ) );

	// z is unused by the projected 2D lookup, so carry model-local height to
	// the fragment stage without consuming another GL 2.1 varying slot.
	vLightFalloffTexCoord = vec4( dot( position, uLightFalloffS ), 0.5, position.z, 1.0 );
	vLightProjectionTexCoord = vec4(
		dot( position, uLightProjectionS ),
		dot( position, uLightProjectionT ),
		0.0,
		dot( position, uLightProjectionQ ) );
	vec3 shadowNormal = normalize( attr_Normal );
	float shadowLightCos = max( dot( shadowNormal, localLightDir ), 0.0 );
	float shadowSinTheta = sqrt( max( 1.0 - shadowLightCos * shadowLightCos, 0.0 ) );
#ifdef OPENQ4_PBR_UNSHADOWED
	// The unshadowed PBR variant serves stencil-shadowed and unshadowed lights.
	vShadowCoord0 = vec4( 0.0 );
	vShadowCoord1 = vec4( 0.0 );
	vShadowCoord2 = vec4( 0.0 );
	vShadowCoord3 = vec4( 0.0 );
#else
	vShadowCoord0 = BuildShadowCoord( position, shadowNormal, shadowSinTheta, 0 );
	vShadowCoord1 = BuildShadowCoord( position, shadowNormal, shadowSinTheta, 1 );
	vShadowCoord2 = BuildShadowCoord( position, shadowNormal, shadowSinTheta, 2 );
	vShadowCoord3 = BuildShadowCoord( position, shadowNormal, shadowSinTheta, 3 );
#endif

	vVertexColor = gl_Color.rgb * uVertexColorParams.x + vec3( uVertexColorParams.y );
	vShadowLightCos = shadowLightCos;
	vViewDepth = max( -viewPosition.z, 0.0 );

	gl_Position = ftransform();
}
