#version 110

uniform sampler2D uBumpMap;
uniform sampler2D uLightFalloffMap;
uniform sampler2D uLightProjectionMap;
uniform sampler2D uDiffuseMap;
uniform sampler2D uSpecularMap;
#ifdef OPENQ4_POINT_SHADOW_COMPARE
uniform samplerCubeShadow uPointShadowMap;
#else
uniform samplerCube uPointShadowMap;
#endif
uniform samplerCube uPointTranslucentShadowMapR;
uniform samplerCube uPointTranslucentShadowMapG;
uniform samplerCube uPointTranslucentShadowMapB;

uniform vec4 uDiffuseColor;
uniform vec4 uSpecularColor;
uniform float uMaterialEnhanced;
uniform float uMaterialNormalScale;
uniform float uMaterialSpecularBoost;
uniform float uMaterialFresnel;
uniform vec4 uCelParams;
uniform vec4 uFlatDiffuseParams;
uniform float uPointShadowFar;
uniform float uShadowBias;
uniform float uShadowNormalBias;
uniform float uPointShadowTexelDepthBias;
uniform float uShadowFilterRadius;
uniform float uShadowFilterTaps;
uniform float uShadowFilterMode;
uniform float uShadowDebugMode;
uniform float uShadowReceiverDebugReason;
uniform float uPointShadowTexelScale;
uniform float uPointShadowDepthMode;
uniform float uTranslucentShadowEnabled;
uniform float uTranslucentShadowDensity;
uniform float uTranslucentShadowFilterRadius;
uniform float uTranslucentShadowMinVariance;
uniform float uTranslucentShadowBleedReduction;

const float kShadowDebugBiasOff = 8.0;
const float kShadowDebugPCFOff = 9.0;
const float kShadowDebugReceiverPlaneBiasOff = 11.0;
const float kShadowDebugCompareDelta = 12.0;
const float kShadowDebugReceiverEligibility = 13.0;
const float kShadowDebugReceiverFallbackReason = 14.0;
const float kShadowBiasMinLightCos = 0.20;
const float kShadowBiasMaxSlope = 4.0;

varying vec2 vBumpTexCoord;
varying vec2 vDiffuseTexCoord;
varying vec2 vSpecularTexCoord;
varying vec4 vLightFalloffTexCoord;
varying vec4 vLightProjectionTexCoord;
varying vec3 vLightVector;
varying vec3 vHalfAngleVector;
varying vec3 vViewVector;
varying vec3 vPointShadowVector;
varying vec3 vVertexColor;
varying float vShadowLightCos;

vec3 SafeNormalize( vec3 value ) {
	return value * inversesqrt( max( dot( value, value ), 1.0e-8 ) );
}

// uFlatDiffuseParams = ( strength, local min Z, inverse height, upward phase ).
// A wrapped, feathered band avoids a visible reset when it reaches the top.
vec3 ApplyFlatDiffuseSweep( vec3 diffuse, float localZ ) {
	if ( uFlatDiffuseParams.x <= 0.0 ) {
		return diffuse;
	}
	float height = clamp( ( localZ - uFlatDiffuseParams.y ) * uFlatDiffuseParams.z, 0.0, 1.0 );
	float distanceToBand = abs( height - fract( uFlatDiffuseParams.w ) );
	distanceToBand = min( distanceToBand, 1.0 - distanceToBand );
	float band = 1.0 - smoothstep( 0.045, 0.16, distanceToBand );
	return mix( diffuse, vec3( 1.0 ), uFlatDiffuseParams.x * band );
}

// ---------------------------------------------------------------------------
// Cel banding. uCelParams is ( bandsEnabled, bandCount, hardSpecular, softness ).
// The same ladder is shared with R_CelQuantizeUnitValue on the CPU side.
// ---------------------------------------------------------------------------

float CelSteps() {
	return max( uCelParams.y - 1.0, 1.0 );
}

// Places a 0..1 value on the band ladder. uCelParams.w widens every boundary
// into a smoothstep centred exactly where the hard step would have landed, so
// the plateaus survive but the transition stops being one texel wide - which
// is what kept a terminator crawling across a curved surface as the camera
// moved. At 0 this is floor( value * steps + 0.5 ) / steps again, to the bit.
float CelLadder( float value ) {
	float steps = CelSteps();
	float scaled = value * steps;

	float softness = clamp( uCelParams.w, 0.0, 1.0 );
	if ( softness <= 0.0 ) {
		return floor( scaled + 0.5 ) / steps;
	}

	// floor( scaled ) names the band below this value and the boundary above it
	// sits half a band away, so the blend window is centred on 0.5.
	float lower = floor( scaled );
	float halfWidth = softness * 0.5;
	float blend = smoothstep( 0.5 - halfWidth, 0.5 + halfWidth, scaled - lower );

	return ( lower + blend ) / steps;
}

// Quantizes a light contribution without shifting its hue: the brightest
// channel picks the band and the others follow it. Black and overbright pass
// through untouched so unlit surfaces stay unlit and headroom is preserved.
vec3 CelQuantizeLight( vec3 light ) {
	if ( uCelParams.x <= 0.5 ) {
		return light;
	}

	float peak = max( max( light.r, light.g ), light.b );
	if ( peak <= 0.0 || peak >= 1.0 ) {
		return light;
	}

	return light * ( CelLadder( peak ) / peak );
}

// Collapses the specular falloff into flat plateaus on the same ladder, which
// is what gives cel highlights their hard edge.
float CelSpecularTerm( float term ) {
	if ( uCelParams.x <= 0.5 || uCelParams.z <= 0.5 ) {
		return term;
	}

	return CelLadder( clamp( term, 0.0, 1.0 ) );
}

bool ShadowDebugModeIs( float mode ) {
	return abs( uShadowDebugMode - mode ) < 0.5;
}

bool ShadowReceiverDebugMode() {
	return ShadowDebugModeIs( kShadowDebugReceiverEligibility ) ||
		ShadowDebugModeIs( kShadowDebugReceiverFallbackReason );
}

bool ShadowVisualDebugMode() {
	return ShadowDebugModeIs( 1.0 ) ||
		ShadowDebugModeIs( 4.0 ) ||
		ShadowDebugModeIs( kShadowDebugCompareDelta ) ||
		ShadowReceiverDebugMode();
}

float EffectiveShadowFilterRadius() {
	return ShadowDebugModeIs( kShadowDebugPCFOff ) ? 0.0 : uShadowFilterRadius;
}

vec3 DecodeLocalNormal( vec4 bumpSample ) {
	if ( uMaterialEnhanced < 0.5 ) {
		return SafeNormalize( vec3( bumpSample.a, bumpSample.g, bumpSample.b ) * 2.0 - 1.0 );
	}

	vec2 localNormalXY = vec2( bumpSample.a, bumpSample.g ) * 2.0 - 1.0;
	localNormalXY *= max( uMaterialNormalScale, 0.0 );

	float xyLengthSq = dot( localNormalXY, localNormalXY );
	if ( xyLengthSq > 1.0 ) {
		localNormalXY *= inversesqrt( xyLengthSq );
		xyLengthSq = 1.0;
	}

	float encodedZ = max( bumpSample.b * 2.0 - 1.0, 0.0 );
	float reconstructedZ = sqrt( max( 1.0 - xyLengthSq, 0.0 ) );
	return SafeNormalize( vec3( localNormalXY, mix( encodedZ, reconstructedZ, 0.75 ) ) );
}

float EnhancedSpecularTerm( vec3 halfAngle, vec3 viewDir, vec3 localNormal, vec3 specularSample ) {
	float ndoth = max( dot( halfAngle, localNormal ), 0.0 );
	float ndotv = max( dot( viewDir, localNormal ), 0.0 );
	float gloss = clamp( max( max( specularSample.r, specularSample.g ), specularSample.b ), 0.0, 1.0 );
	float specularPower = mix( 10.0, 40.0, gloss );
	float fresnel = 1.0 + ( pow( 1.0 - ndotv, 5.0 ) * 2.0 * clamp( uMaterialFresnel, 0.0, 1.0 ) );
	return pow( ndoth, specularPower ) * max( uMaterialSpecularBoost, 0.0 ) * fresnel;
}

float LegacySpecularTerm( vec3 halfAngle, vec3 localNormal ) {
	float specular = clamp( dot( halfAngle, localNormal ) * 4.0 - 3.0, 0.0, 1.0 );
	return specular * specular;
}

vec3 InteractionSpecular( vec3 halfAngle, vec3 viewDir, vec3 localNormal, vec3 specularSample ) {
	if ( uMaterialEnhanced >= 0.5 ) {
		return specularSample * uSpecularColor.rgb * CelSpecularTerm( EnhancedSpecularTerm( halfAngle, viewDir, localNormal, specularSample ) );
	}
	return specularSample * ( uSpecularColor.rgb * 2.0 ) * CelSpecularTerm( LegacySpecularTerm( halfAngle, localNormal ) );
}

float ApproxErf( float x ) {
	float s = sign( x );
	float ax = abs( x );
	float t = 1.0 / ( 1.0 + 0.3275911 * ax );
	float y = 1.0 - ( ( ( ( ( 1.061405429 * t - 1.453152027 ) * t ) + 1.421413741 ) * t - 0.284496736 ) * t + 0.254829592 ) * t * exp( -ax * ax );
	return s * y;
}

float NormalCdf( float x ) {
	return 0.5 * ( 1.0 + ApproxErf( x * 0.70710678 ) );
}

// Two of the four ring taps of the five-tap kernel; the others mirror them.
const vec2 kShadowFiveTapRingA = vec2( 0.670820, 0.223607 );
const vec2 kShadowFiveTapRingB = vec2( -0.223607, 0.670820 );

float StableShadowHash( vec3 value ) {
	return fract( sin( dot( value, vec3( 12.9898, 78.233, 37.719 ) ) ) * 43758.5453 );
}

mat2 ShadowOffsetRotation( vec3 direction ) {
	if ( uShadowFilterMode < 0.5 ) {
		return mat2( 1.0, 0.0, 0.0, 1.0 );
	}
	float angle = StableShadowHash( floor( direction * 37.0 ) ) * 6.2831853;
	float s = sin( angle );
	float c = cos( angle );
	return mat2( c, s, -s, c );
}

float TranslucentFilterRadius() {
	return ( uTranslucentShadowFilterRadius >= 0.0 ) ? uTranslucentShadowFilterRadius : uShadowFilterRadius;
}

float ResolveTranslucentShadowMoments( vec4 moments, float depth ) {
	if ( uTranslucentShadowEnabled < 0.5 ) {
		return 1.0;
	}

	float totalTau = max( moments.x, 0.0 );
	if ( totalTau <= 1.0e-4 ) {
		return 1.0;
	}

	float mean = moments.y / totalTau;
	float variance = max( moments.z / totalTau - mean * mean, max( uTranslucentShadowMinVariance, 1.0e-6 ) );
	float sigma = sqrt( variance );
	float fraction = clamp( NormalCdf( ( depth - mean ) / sigma ), 0.0, 1.0 );
	float bleed = clamp( uTranslucentShadowBleedReduction, 0.0, 0.95 );
	fraction = clamp( ( fraction - bleed ) / max( 1.0 - bleed, 1.0e-4 ), 0.0, 1.0 );
	float tau = totalTau * fraction;
	return exp( -min( tau * max( uTranslucentShadowDensity, 0.0 ), 16.0 ) );
}

vec4 SampleFilteredPointMoments( samplerCube momentMap, vec3 direction ) {
	float filterRadius = ShadowDebugModeIs( kShadowDebugPCFOff ) ? 0.0 : TranslucentFilterRadius();
	if ( filterRadius <= 0.0 || uPointShadowTexelScale <= 0.0 ) {
		return textureCube( momentMap, direction );
	}

	vec3 up = ( abs( direction.z ) < 0.99 ) ? vec3( 0.0, 0.0, 1.0 ) : vec3( 0.0, 1.0, 0.0 );
	vec3 tangent = SafeNormalize( cross( up, direction ) );
	vec3 bitangent = cross( direction, tangent );
	float tap = uPointShadowTexelScale * max( filterRadius, 0.5 );
	vec4 moments = textureCube( momentMap, direction );
	moments += textureCube( momentMap, SafeNormalize( direction + ( tangent * -0.5 + bitangent * -0.5 ) * tap ) );
	moments += textureCube( momentMap, SafeNormalize( direction + ( tangent * 0.5 + bitangent * -0.5 ) * tap ) );
	moments += textureCube( momentMap, SafeNormalize( direction + ( tangent * -0.5 + bitangent * 0.5 ) * tap ) );
	moments += textureCube( momentMap, SafeNormalize( direction + ( tangent * 0.5 + bitangent * 0.5 ) * tap ) );
	return moments * 0.2;
}

float ShadowReceiverBias() {
	if ( ShadowDebugModeIs( kShadowDebugBiasOff ) ) {
		return 0.0;
	}
	float lightCos = clamp( vShadowLightCos, kShadowBiasMinLightCos, 1.0 );
	float sinTheta = sqrt( max( 1.0 - lightCos * lightCos, 0.0 ) );
	float slopeBias = min( sinTheta / lightCos, kShadowBiasMaxSlope );
	float normalBias = ShadowDebugModeIs( kShadowDebugReceiverPlaneBiasOff ) ? 0.0 : uShadowNormalBias;
	float scalarBias = uShadowBias + normalBias * sinTheta;
	float texelBias = uPointShadowTexelDepthBias * ( 1.0 + slopeBias );
	return max( max( scalarBias, 0.0 ), max( texelBias, 0.0 ) );
}

float UnpackDepth16( vec2 rg ) {
	return rg.x + rg.y * ( 1.0 / 255.0 );
}

float DecodePointShadowDepth( vec4 encodedDepth ) {
	if ( uPointShadowDepthMode > 0.5 ) {
		return encodedDepth.r;
	}
	return UnpackDepth16( encodedDepth.rg );
}

float PointShadowCompareDepth(vec3 direction, float compareDepth) {
#ifdef OPENQ4_POINT_SHADOW_COMPARE
    return texture(uPointShadowMap, vec4(direction, compareDepth));
#else
    float storedDepth = DecodePointShadowDepth(textureCube(uPointShadowMap, direction));
    return compareDepth <= storedDepth ? 1.0 : 0.0;
#endif
}

// The plane is evaluated before divergent control flow. Smooth vertex normals
// describe lighting, not the triangle rasterized into the shadow cube.
vec4 gPointReceiverPlane = vec4(0.0);

void PointShadowCubeAxes(vec3 direction, out vec3 face, out vec3 axisS, out vec3 axisT) {
    vec3 a = abs(direction);
    if (a.x >= a.y && a.x >= a.z) {
        float side = direction.x >= 0.0 ? 1.0 : -1.0;
        face = vec3(side, 0.0, 0.0);
        axisS = vec3(0.0, 0.0, -side);
        axisT = vec3(0.0, -1.0, 0.0);
    } else if (a.y >= a.z) {
        float side = direction.y >= 0.0 ? 1.0 : -1.0;
        face = vec3(0.0, side, 0.0);
        axisS = vec3(1.0, 0.0, 0.0);
        axisT = vec3(0.0, 0.0, side);
    } else {
        float side = direction.z >= 0.0 ? 1.0 : -1.0;
        face = vec3(0.0, 0.0, side);
        axisS = vec3(side, 0.0, 0.0);
        axisT = vec3(0.0, -1.0, 0.0);
    }
}

vec3 PointShadowTexelDirection(vec3 direction, float texelScale) {
    vec3 face, axisS, axisT;
    PointShadowCubeAxes(direction, face, axisS, axisT);
    vec2 st = vec2(dot(direction, axisS), dot(direction, axisT)) / dot(direction, face);
    // Re-select the face for taps across a cube seam, then snap to its actual
    // texel center. A hardware comparison there is a single depth comparison.
    st = (floor((st + 1.0) / texelScale) + 0.5) * texelScale - 1.0;
    return normalize(face + axisS * st.x + axisT * st.y);
}

float PointShadowPlaneDepth(vec3 direction, float depth) {
    float denominator = dot(gPointReceiverPlane.xyz, direction);
    float distance = gPointReceiverPlane.w / (abs(denominator) > 1.0e-5 ? denominator : 1.0);
    // Near the plane horizon a neighbouring ray may not reach the receiver.
    // Keep the ordinary comparison for that ill-conditioned extrapolation.
    return abs(denominator) > 1.0e-5 && distance > 0.0 && distance < 1.0
        ? distance : depth;
}

float PointShadowPlaneCompare(vec3 direction, float depth, float bias, float texelScale) {
    vec3 sampleDirection = PointShadowTexelDirection(direction, texelScale);
    float sampleDepth = PointShadowPlaneDepth(sampleDirection, depth);
    return PointShadowCompareDepth(sampleDirection, sampleDepth - bias);
}

float PointShadowFilteredCompare(vec3 direction, float depth, float bias, float texelScale, bool hardwareCompare) {
    if (texelScale <= 0.0 || dot(gPointReceiverPlane.xyz, gPointReceiverPlane.xyz) < 0.5
        || ShadowDebugModeIs(kShadowDebugBiasOff)
        || ShadowDebugModeIs(kShadowDebugReceiverPlaneBiasOff)) {
        return PointShadowCompareDepth(direction, depth - bias);
    }
    // Keep the inexpensive native lookup when the existing bias already
    // covers this tap's plane displacement and entire texel footprint. The
    // extra comparisons are needed only for under-resolved sloped receivers.
    float planeCos = abs(dot(gPointReceiverPlane.xyz, direction));
    float planeSlope = sqrt(max(1.0 - planeCos * planeCos, 0.0)) / max(planeCos, 1.0e-5);
    float planeDepth = PointShadowPlaneDepth(direction, depth);
    float footprint = 2.0 * planeDepth * texelScale * (planeSlope + texelScale);
    if (abs(planeDepth - depth) + footprint < bias * 0.5) {
        return PointShadowCompareDepth(direction, depth - bias);
    }
    if (!hardwareCompare) {
        return PointShadowPlaneCompare(direction, depth, bias, texelScale);
    }
    // One bilinear lookup against this tap's own point on the receiver plane
    // still holds while the bias covers the depth the plane spans across the
    // four texels it reads, a texel diagonal at most. That is most lit
    // surface; only steep or distant receivers need each texel compared
    // against its own ray, at four lookups and four face selections a tap.
    if (planeDepth * texelScale * (planeSlope + texelScale) * 1.4142136 < bias) {
        return PointShadowCompareDepth(direction, planeDepth - bias);
    }
    vec3 face, axisS, axisT;
    PointShadowCubeAxes(direction, face, axisS, axisT);
    vec2 st = vec2(dot(direction, axisS), dot(direction, axisT)) / dot(direction, face);
    vec2 grid = (st + 1.0) / texelScale - 0.5;
    vec2 weight = fract(grid);
    vec2 lower = (floor(grid) + 0.5) * texelScale - 1.0;
    vec3 corner = face + axisS * lower.x + axisT * lower.y;
    // Hardware PCF compares all four depths with one radial reference. On a
    // distant sloped receiver those depths can differ by many world units,
    // exceeding the contact-preserving bias cap. Compare each texel against
    // the ray/receiver-plane intersection and retain the bilinear weights.
    float a = PointShadowPlaneCompare(corner, depth, bias, texelScale);
    float b = PointShadowPlaneCompare(corner + axisS * texelScale, depth, bias, texelScale);
    float c = PointShadowPlaneCompare(corner + axisT * texelScale, depth, bias, texelScale);
    float d = PointShadowPlaneCompare(corner + (axisS + axisT) * texelScale, depth, bias, texelScale);
    return mix(mix(a, b, weight.x), mix(c, d, weight.x), weight.y);
}

float SamplePointShadowCompare( vec3 direction, float depth ) {
#ifdef OPENQ4_POINT_SHADOW_COMPARE
    bool hardwareCompare = true;
#else
    bool hardwareCompare = false;
#endif
    return PointShadowFilteredCompare(direction, depth, ShadowReceiverBias(), uPointShadowTexelScale, hardwareCompare);
}

float RawPointShadowDepth( vec3 direction ) {
#ifdef OPENQ4_POINT_SHADOW_COMPARE
	return texture( uPointShadowMap, vec4( direction, 0.5 ) );
#else
	return DecodePointShadowDepth( textureCube( uPointShadowMap, direction ) );
#endif
}

#ifdef OPENQ4_POINT_SHADOW_COMPARE
// The default kernel: nine bilinear comparisons on a grid one texel apart.
// Together they read a four-by-four block of texels, weighted
// ( 1-f, 1, 1, f ) along each axis. Where the bias covers the depth the
// receiver plane spans across that block, the nine hardware lookups are the
// kernel. Elsewhere each of the sixteen texels is compared once, against its
// own ray's point on the receiver plane, and given the same weight; taken tap
// by tap that cost four face selections and four lookups for each of the
// nine taps, most of them repeats of a texel already read.
float SamplePointShadowGrid( vec3 direction, float depth ) {
	float bias = ShadowReceiverBias();
	float texelScale = uPointShadowTexelScale;
	vec3 face, axisS, axisT;
	PointShadowCubeAxes( direction, face, axisS, axisT );
	vec2 st = vec2( dot( direction, axisS ), dot( direction, axisT ) ) / dot( direction, face );

	bool perTexel = dot( gPointReceiverPlane.xyz, gPointReceiverPlane.xyz ) >= 0.5
		&& !ShadowDebugModeIs( kShadowDebugBiasOff )
		&& !ShadowDebugModeIs( kShadowDebugReceiverPlaneBiasOff );
	if ( perTexel ) {
		float planeCos = abs( dot( gPointReceiverPlane.xyz, direction ) );
		float planeSlope = sqrt( max( 1.0 - planeCos * planeCos, 0.0 ) ) / max( planeCos, 1.0e-5 );
		// the block reaches two texels from the receiver in each direction
		perTexel = 4.0 * depth * texelScale * ( planeSlope + texelScale ) >= bias * 0.5;
	}
	if ( !perTexel ) {
		float lit = 0.0;
		for ( int j = -1; j <= 1; j++ ) {
			for ( int i = -1; i <= 1; i++ ) {
				vec2 tapSt = st + vec2( float( i ), float( j ) ) * texelScale;
				lit += PointShadowCompareDepth( face + axisS * tapSt.x + axisT * tapSt.y, depth - bias );
			}
		}
		return lit * ( 1.0 / 9.0 );
	}

	vec2 grid = ( st + 1.0 ) / texelScale - 0.5;
	vec2 weight = fract( grid );
	vec2 lower = ( floor( grid ) + 0.5 ) * texelScale - 1.0;
	float lit = 0.0;
	for ( int j = 0; j < 4; j++ ) {
		float t = lower.y + ( float( j ) - 1.0 ) * texelScale;
		float weightT = ( j == 0 ) ? 1.0 - weight.y : ( ( j == 3 ) ? weight.y : 1.0 );
		for ( int i = 0; i < 4; i++ ) {
			float s = lower.x + ( float( i ) - 1.0 ) * texelScale;
			float weightS = ( i == 0 ) ? 1.0 - weight.x : ( ( i == 3 ) ? weight.x : 1.0 );
			vec3 texel = face + axisS * s + axisT * t;
			float texelLit;
			if ( max( abs( s ), abs( t ) ) > 1.0 ) {
				// past the edge of this face the neighbouring face has its own grid
				texelLit = PointShadowPlaneCompare( texel, depth, bias, texelScale );
			} else {
				float denominator = dot( gPointReceiverPlane.xyz, texel );
				float planeDepth = gPointReceiverPlane.w * length( texel )
					/ ( abs( denominator ) > 1.0e-5 ? denominator : 1.0 );
				float texelDepth = ( abs( denominator ) > 1.0e-5 && planeDepth > 0.0 && planeDepth < 1.0 )
					? planeDepth : depth;
				texelLit = PointShadowCompareDepth( texel, texelDepth - bias );
			}
			lit += texelLit * weightS * weightT;
		}
	}
	return lit * ( 1.0 / 9.0 );
}
#endif

float SamplePointShadow() {
	if ( uPointShadowFar <= 0.0 ) {
		return 1.0;
	}

	float depth = length( vPointShadowVector ) / uPointShadowFar;
	if ( depth <= 0.0 || depth >= 1.0 ) {
		return 1.0;
	}

	vec3 direction = SafeNormalize( vPointShadowVector );
	float filterRadius = EffectiveShadowFilterRadius();
	if ( filterRadius <= 0.0 || uPointShadowTexelScale <= 0.0 ) {
		return SamplePointShadowCompare( direction, depth );
	}
#ifdef OPENQ4_POINT_SHADOW_COMPARE
	// nine fixed taps at a one-texel radius: the shipped default
	if ( uShadowFilterMode < 0.5 && uShadowFilterTaps > 5.0 && uShadowFilterTaps <= 9.0
			&& abs( filterRadius - 1.0 ) < 0.01 ) {
		return SamplePointShadowGrid( direction, depth );
	}
#endif

	vec3 up = ( abs( direction.z ) < 0.99 ) ? vec3( 0.0, 0.0, 1.0 ) : vec3( 0.0, 1.0, 0.0 );
	vec3 tangent = SafeNormalize( cross( up, direction ) );
	vec3 bitangent = cross( direction, tangent );
	float tap = uPointShadowTexelScale * filterRadius;

	float shadow = 0.0;
	shadow += SamplePointShadowCompare( direction, depth );
	if ( uShadowFilterTaps <= 1.0 ) {
		return shadow;
	}
	mat2 rotation = ShadowOffsetRotation( direction );
	// The first four taps of the nine- and thirteen-tap sets all lie to one
	// side of the centre. Alone they pull the shadow 0.4 radius sideways and
	// soften one side of its edge only, so the five-tap kernel uses a ring.
	vec2 o1 = rotation * vec2( -0.326212, -0.405805 );
	vec2 o2 = rotation * vec2( -0.840144, -0.073580 );
	vec2 o3 = rotation * vec2( -0.695914, 0.457137 );
	vec2 o4 = rotation * vec2( -0.203345, 0.620716 );
	if ( uShadowFilterTaps <= 5.0 ) {
		o1 = rotation * kShadowFiveTapRingA;
		o2 = rotation * kShadowFiveTapRingB;
		o3 = -o1;
		o4 = -o2;
	}
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * o1.x + bitangent * o1.y ) * tap ), depth );
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * o2.x + bitangent * o2.y ) * tap ), depth );
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * o3.x + bitangent * o3.y ) * tap ), depth );
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * o4.x + bitangent * o4.y ) * tap ), depth );
	if ( uShadowFilterTaps <= 5.0 ) {
		return shadow * ( 1.0 / 5.0 );
	}
	vec2 o5 = rotation * vec2( 0.962340, -0.194983 );
	vec2 o6 = rotation * vec2( 0.473434, -0.480026 );
	vec2 o7 = rotation * vec2( 0.519456, 0.767022 );
	vec2 o8 = rotation * vec2( 0.185461, -0.893124 );
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * o5.x + bitangent * o5.y ) * tap ), depth );
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * o6.x + bitangent * o6.y ) * tap ), depth );
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * o7.x + bitangent * o7.y ) * tap ), depth );
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * o8.x + bitangent * o8.y ) * tap ), depth );
	if ( uShadowFilterTaps <= 9.0 ) {
		return shadow * ( 1.0 / 9.0 );
	}
	vec2 o9 = rotation * vec2( 0.507431, 0.064425 );
	vec2 o10 = rotation * vec2( 0.896420, 0.412458 );
	vec2 o11 = rotation * vec2( -0.321940, -0.932615 );
	vec2 o12 = rotation * vec2( -0.791559, -0.597705 );
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * o9.x + bitangent * o9.y ) * tap ), depth );
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * o10.x + bitangent * o10.y ) * tap ), depth );
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * o11.x + bitangent * o11.y ) * tap ), depth );
	shadow += SamplePointShadowCompare( SafeNormalize( direction + ( tangent * o12.x + bitangent * o12.y ) * tap ), depth );
	return shadow * ( 1.0 / 13.0 );
}

vec4 PointReceiverDebugOutput() {
	float reason = floor( uShadowReceiverDebugReason + 0.5 );
	if ( ShadowDebugModeIs( kShadowDebugReceiverEligibility ) ) {
		if ( reason < 0.5 ) {
			return vec4( 0.0, 0.95, 0.18, 1.0 );
		}
		if ( reason < 1.5 ) {
			return vec4( 0.0, 0.85, 1.0, 1.0 );
		}
		return vec4( 1.0, 0.18, 0.08, 1.0 );
	}

	if ( reason < 0.5 ) {
		return vec4( 0.0, 0.85, 0.16, 1.0 );
	}
	if ( reason < 1.5 ) {
		return vec4( 0.0, 0.82, 1.0, 1.0 );
	}
	if ( reason < 2.5 ) {
		return vec4( 1.0, 0.08, 0.08, 1.0 );
	}
	if ( reason < 3.5 ) {
		return vec4( 0.95, 0.12, 1.0, 1.0 );
	}
	if ( reason < 4.5 ) {
		return vec4( 1.0, 0.86, 0.08, 1.0 );
	}
	return vec4( 1.0, 0.45, 0.0, 1.0 );
}

vec4 PointShadowDebugOutput() {
	if ( ShadowReceiverDebugMode() ) {
		return PointReceiverDebugOutput();
	}
	if ( uPointShadowFar <= 0.0 ) {
		return vec4( 1.0, 0.0, 1.0, 1.0 );
	}

	float depth = length( vPointShadowVector ) / uPointShadowFar;
	if ( depth <= 0.0 || depth >= 1.0 ) {
		return vec4( 1.0, 1.0, 0.0, 1.0 );
	}
	vec3 direction = SafeNormalize( vPointShadowVector );
	float storedDepth = RawPointShadowDepth( direction );

	if ( ShadowDebugModeIs( kShadowDebugCompareDelta ) ) {
		float delta = depth - ShadowReceiverBias() - storedDepth;
		float magnitude = clamp( abs( delta ) * 64.0, 0.0, 1.0 );
		vec3 litColor = vec3( 0.1, 0.35, 1.0 );
		vec3 shadowColor = vec3( 1.0, 0.16, 0.08 );
		vec3 nearColor = vec3( 0.0, 1.0, 0.22 );
		vec3 signColor = ( delta > 0.0 ) ? shadowColor : litColor;
		return vec4( mix( nearColor, signColor, magnitude ), 1.0 );
	}

	if ( ShadowDebugModeIs( 4.0 ) ) {
		return vec4( vec3( depth ), 1.0 );
	}

	return vec4( direction * 0.5 + 0.5, clamp( storedDepth, 0.0, 1.0 ) );
}

vec3 SamplePointTranslucentShadow() {
	if ( uTranslucentShadowEnabled < 0.5 || uPointShadowFar <= 0.0 ) {
		return vec3( 1.0 );
	}

	float depth = length( vPointShadowVector ) / uPointShadowFar;
	if ( depth <= 0.0 || depth >= 1.0 ) {
		return vec3( 1.0 );
	}

	vec3 direction = SafeNormalize( vPointShadowVector );
	return vec3(
		ResolveTranslucentShadowMoments( SampleFilteredPointMoments( uPointTranslucentShadowMapR, direction ), depth ),
		ResolveTranslucentShadowMoments( SampleFilteredPointMoments( uPointTranslucentShadowMapG, direction ), depth ),
		ResolveTranslucentShadowMoments( SampleFilteredPointMoments( uPointTranslucentShadowMapB, direction ), depth ) );
}

#ifdef OPENQ4_PBR
// Native PBR shading for one light, given its shadow visibility; draw_pbr.cpp
// appends the library that defines it to this source.
vec3 OpenQ4PBRDirect( vec3 shadow );
// The coverage of a translucent owner's replayed light; zero otherwise.
float OpenQ4PBRAlpha();
#endif

void main() {
    vec3 receiverNormal = cross(dFdx(vPointShadowVector), dFdy(vPointShadowVector));
    receiverNormal *= inversesqrt(max(dot(receiverNormal, receiverNormal), 1.0e-20));
    gPointReceiverPlane = vec4(receiverNormal, dot(receiverNormal, vPointShadowVector) / max(uPointShadowFar, 1.0));
#ifdef OPENQ4_PBR
	vec3 pbrShadow = vec3( 1.0 ) * SamplePointShadow() * SamplePointTranslucentShadow();
	if ( ShadowVisualDebugMode() ) {
		gl_FragColor = PointShadowDebugOutput();
		return;
	}
	gl_FragColor = vec4( OpenQ4PBRDirect( pbrShadow ), OpenQ4PBRAlpha() );
	return;
#endif
	vec4 bumpSample = texture2D( uBumpMap, vBumpTexCoord );
	vec3 localNormal = DecodeLocalNormal( bumpSample );

	vec3 lightDir = SafeNormalize( vLightVector );
	float ndotl = max( dot( lightDir, localNormal ), 0.0 );

	vec3 light = vec3( ndotl );
	light *= texture2DProj( uLightFalloffMap, vLightFalloffTexCoord ).rgb;
	light *= texture2DProj( uLightProjectionMap, vLightProjectionTexCoord ).rgb;
	// A light's box reaches well past its falloff, and a bumped surface turns
	// away from it: a fragment the light does not reach has no shadow to
	// resolve, and the filter is most of this shader's cost.
	if ( max( light.r, max( light.g, light.b ) ) > 0.0 ) {
		light *= SamplePointShadow();
		light *= SamplePointTranslucentShadow();
	}

	vec3 diffuse = texture2D( uDiffuseMap, vDiffuseTexCoord ).rgb * uDiffuseColor.rgb;
	diffuse = ApplyFlatDiffuseSweep( diffuse, vLightFalloffTexCoord.z );

	vec3 specularSample = texture2D( uSpecularMap, vSpecularTexCoord ).rgb;
	vec3 halfAngle = SafeNormalize( vHalfAngleVector );
	vec3 viewDir = SafeNormalize( vViewVector );
	vec3 specular = InteractionSpecular( halfAngle, viewDir, localNormal, specularSample );

	vec3 color = ( diffuse + specular ) * CelQuantizeLight( light ) * vVertexColor;
	if ( ShadowVisualDebugMode() ) {
		gl_FragColor = PointShadowDebugOutput();
		return;
	}
	gl_FragColor = vec4( color, 0.0 );
}
