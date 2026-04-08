#version 110

uniform sampler2D uAlphaMap;
uniform sampler2D uCoverageMap;
uniform vec4 uStageColor;
uniform vec4 uCoverageStageColor;
uniform float uOpacitySourceMode;
uniform float uVertexColorMode;
uniform float uCoverageSourceMode;
uniform float uCoverageVertexColorMode;
uniform float uCoverageAlphaTestRef;
uniform float uCoverageAlphaTestEnabled;
uniform float uTranslucentMinAlpha;

varying vec2 vAlphaTexCoord;
varying vec2 vCoverageTexCoord;
varying vec3 vVertexColorRgb;
varying float vVertexAlpha;
varying float vCoverageVertexAlpha;

float OpticalDepth( float alpha ) {
	return -log( max( 1.0 - clamp( alpha, 0.0, 0.999 ), 1.0e-4 ) );
}

vec3 VertexColorRgb( float mode ) {
	if ( mode > 1.5 ) {
		return 1.0 - vVertexColorRgb;
	}
	if ( mode > 0.5 ) {
		return vVertexColorRgb;
	}
	return vec3( 1.0 );
}

float CoverageAmount() {
	if ( uCoverageSourceMode < 0.5 ) {
		return 1.0;
	}

	vec4 coverageSample = texture2D( uCoverageMap, vCoverageTexCoord );
	vec3 coverageTint = clamp( coverageSample.rgb * uCoverageStageColor.rgb * VertexColorRgb( uCoverageVertexColorMode ), 0.0, 1.0 );
	float coverageAlpha = coverageSample.a * uCoverageStageColor.a * vCoverageVertexAlpha;
	if ( uCoverageAlphaTestEnabled > 0.5 && coverageAlpha <= uCoverageAlphaTestRef ) {
		discard;
	}
	if ( uCoverageSourceMode > 1.5 ) {
		coverageAlpha = max( coverageAlpha, dot( coverageTint, vec3( 0.2126, 0.7152, 0.0722 ) ) * vCoverageVertexAlpha );
	}
	return clamp( coverageAlpha, 0.0, 1.0 );
}

vec3 StageAbsorption() {
	float coverage = CoverageAmount();
	if ( uOpacitySourceMode > 1.5 ) {
		vec3 transmission = clamp( uStageColor.rgb * VertexColorRgb( uVertexColorMode ), 0.0, 1.0 );
		return clamp( ( 1.0 - transmission ) * coverage * vVertexAlpha, 0.0, 0.999 );
	}

	vec4 sampleColor = texture2D( uAlphaMap, vAlphaTexCoord );
	vec3 transmission = clamp( sampleColor.rgb * uStageColor.rgb * VertexColorRgb( uVertexColorMode ), 0.0, 1.0 );
	return clamp( ( 1.0 - transmission ) * coverage * vVertexAlpha, 0.0, 0.999 );
}

void main() {
	vec3 absorption = StageAbsorption();
	float maxAbsorption = max( absorption.r, max( absorption.g, absorption.b ) );
	if ( maxAbsorption <= uTranslucentMinAlpha ) {
		discard;
	}

	vec3 tauColor = vec3( OpticalDepth( absorption.r ), OpticalDepth( absorption.g ), OpticalDepth( absorption.b ) );
	float depth = clamp( gl_FragCoord.z, 0.0, 1.0 );
	float depth2 = depth * depth;
	gl_FragData[0] = vec4( tauColor.r, tauColor.r * depth, tauColor.r * depth2, tauColor.r * depth2 * depth );
	gl_FragData[1] = vec4( tauColor.g, tauColor.g * depth, tauColor.g * depth2, tauColor.g * depth2 * depth );
	gl_FragData[2] = vec4( tauColor.b, tauColor.b * depth, tauColor.b * depth2, tauColor.b * depth2 * depth );
}
