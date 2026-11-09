#version 110

uniform float uPointShadowFar;
uniform sampler2D uAlphaMap;
uniform float uAlphaRef;
uniform float uAlphaTestMode;
uniform float uAlphaScale;
uniform float uAlphaTestEnabled;
uniform float uAlphaHashEnabled;
uniform float uAlphaHashStable;
uniform float uPointShadowDepthMode;
uniform vec2 uShadowCasterDepthOffset;

varying vec3 vPointShadowVector;
varying vec2 vAlphaTexCoord;
varying vec3 vAlphaHashCoord;

vec2 PackDepth16( float depth ) {
	// Stop one code below 1.0: fract( 1.0 ) is zero, so a caster at or beyond
	// the far envelope wrapped to the nearest depth there is and shadowed
	// every receiver behind its texel.
	vec2 enc = fract( vec2( 1.0, 255.0 ) * clamp( depth, 0.0, 65024.0 / 65025.0 ) );
	enc.x -= enc.y * ( 1.0 / 255.0 );
	return enc;
}

float AlphaHashThreshold( vec2 fragmentCoord ) {
	vec2 texel = floor( fragmentCoord );
	return fract( 52.9829189 * fract( texel.x * 0.06711056 + texel.y * 0.00583715 ) );
}

float StableAlphaHashThreshold( vec3 hashCoord ) {
	vec3 texel = floor( hashCoord * 0.5 );
	return fract( 52.9829189 * fract( texel.x * 0.06711056 + texel.y * 0.00583715 + texel.z * 0.01327111 ) );
}

float AlphaCoverage( float alpha ) {
	float alphaRef = clamp( uAlphaRef, 0.0, 0.9999 );
	float scaledAlpha = clamp( alpha, 0.0, 1.0 );
	return clamp( ( scaledAlpha - alphaRef ) / max( 1.0 - alphaRef, 1.0e-4 ), 0.0, 1.0 );
}

bool AlphaTestPass( float alpha ) {
	if ( uAlphaTestMode < -0.5 ) {
		return alpha < uAlphaRef;
	}
	if ( abs( uAlphaTestMode ) <= 0.5 ) {
		return abs( alpha - uAlphaRef ) <= ( 0.5 / 255.0 );
	}
	return alpha > uAlphaRef;
}

void main() {
	if ( uAlphaTestEnabled > 0.5 ) {
		float alpha = texture2D( uAlphaMap, vAlphaTexCoord ).a * uAlphaScale;
		if ( uAlphaHashEnabled > 0.5 && uAlphaTestMode > 0.5 ) {
			float coverage = AlphaCoverage( alpha );
			float threshold = ( uAlphaHashStable > 0.5 ) ? StableAlphaHashThreshold( vAlphaHashCoord ) : AlphaHashThreshold( gl_FragCoord.xy );
			if ( coverage <= 0.0 || coverage <= threshold ) {
				discard;
			}
		} else if ( !AlphaTestPass( alpha ) ) {
			discard;
		}
	}

	if ( uPointShadowFar <= 0.0 ) {
		discard;
	}
	// Casters beyond the padded far envelope clamp to the far depth instead
	// of vanishing; the receiver treats depth >= 1 as unshadowed, so clamping
	// only improves filter behavior near the envelope boundary.
	float rawDepth = length( vPointShadowVector ) / uPointShadowFar;
	// glPolygonOffset cannot bias the stored radial depth (only the rasterized
	// ordering depth), so the slope-scale caster offset is folded into the
	// stored value here: x scales with the radial-depth slope, y is pre-scaled
	// by the CPU to one resolvable depth-buffer step.
	float depthSlope = max( abs( dFdx( rawDepth ) ), abs( dFdy( rawDepth ) ) );
	float depth = clamp( rawDepth + uShadowCasterDepthOffset.x * depthSlope + uShadowCasterDepthOffset.y, 0.0, 1.0 );
#ifdef OPENQ4_POINT_SHADOW_CASTER_DEPTH
	// The hardware-compare path samples the depth cubemap against a radial
	// reference, so the radial value replaces the rasterized fragment depth.
	// Writing gl_FragDepth only in this variant keeps the default variant's
	// fragment depth fully defined.
	gl_FragDepth = depth;
#endif
	if ( uPointShadowDepthMode > 0.5 ) {
		gl_FragColor = vec4( depth, depth, depth, 1.0 );
		return;
	}
	vec2 packedDepth = PackDepth16( depth );
	gl_FragColor = vec4( packedDepth, 0.0, 1.0 );
}
