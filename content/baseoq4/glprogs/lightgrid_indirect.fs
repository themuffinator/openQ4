#version 110

uniform sampler2D uBumpMap;
uniform sampler2D uDiffuseMap;
uniform sampler2D uLightGridAtlas;
uniform sampler2D uLightGridVisibilityAtlas;
uniform sampler2D uLightGridProbeAtlas;

uniform vec4 uLightGridOrigin;
uniform vec4 uLightGridSize;
uniform vec4 uLightGridBounds;
uniform vec4 uAtlasInfo;
uniform vec4 uVisibilityInfo;
uniform vec4 uProbeInfo;
uniform vec4 uBlendInfo;
uniform vec4 uPortalPlane;
uniform vec4 uPortalBoundsMin;
uniform vec4 uPortalBoundsMax;
uniform vec4 uDiffuseColor;

varying vec2 vBumpTexCoord;
varying vec2 vDiffuseTexCoord;
varying vec3 vWorldTangent;
varying vec3 vWorldBitangent;
varying vec3 vWorldNormal;
varying vec3 vWorldPosition;
varying vec3 vVertexColor;

vec2 SignNotZero( vec2 value ) {
	return vec2( value.x >= 0.0 ? 1.0 : -1.0, value.y >= 0.0 ? 1.0 : -1.0 );
}

vec2 OctEncode( vec3 normal ) {
	float invLength = 1.0 / max( abs( normal.x ) + abs( normal.y ) + abs( normal.z ), 1.0e-4 );
	vec3 n = normal * invLength;
	vec2 oct = n.xy;
	if ( n.z < 0.0 ) {
		oct = ( vec2( 1.0 ) - abs( oct.yx ) ) * SignNotZero( oct );
	}
	return oct;
}

void ComputeGridAxis( float lightOrigin, float cellSize, float bound, out float gridCoord, out float fracCoord ) {
	if ( bound <= 1.0 || cellSize <= 0.0 ) {
		gridCoord = 0.0;
		fracCoord = 0.0;
		return;
	}

	float position = max( 0.0, lightOrigin / cellSize );
	gridCoord = floor( position );
	fracCoord = position - gridCoord;

	if ( gridCoord < 0.0 ) {
		gridCoord = 0.0;
		fracCoord = 0.0;
	} else if ( gridCoord >= bound - 1.0 ) {
		gridCoord = bound - 1.0;
		fracCoord = 0.0;
	}
}

vec2 ProbeAtlasCoord( vec3 sampleCoord, vec2 octCoord ) {
	float invCellsX = 1.0 / max( uLightGridBounds.x * uLightGridBounds.z, 1.0 );
	float invCellsY = 1.0 / max( uLightGridBounds.y, 1.0 );
	float probeScale = uAtlasInfo.w;
	vec2 octCoordInCell = octCoord * vec2( invCellsX, invCellsY );
	float cellIndex = sampleCoord.x + sampleCoord.z * uLightGridBounds.x;
	vec2 atlasOffset = vec2( cellIndex * invCellsX, sampleCoord.y * invCellsY );

	vec2 octCoordWithinAtlas = ( octCoordInCell + atlasOffset ) * probeScale;
	vec2 probeTopLeftPixels = vec2(
		cellIndex * uAtlasInfo.z + uAtlasInfo.z * 0.5,
		sampleCoord.y * uAtlasInfo.z + uAtlasInfo.z * 0.5 );
	return probeTopLeftPixels * uAtlasInfo.xy + octCoordWithinAtlas;
}

vec2 ProbeGridCoord( vec3 sampleCoord ) {
	float invCellsX = 1.0 / max( uLightGridBounds.x * uLightGridBounds.z, 1.0 );
	float invCellsY = 1.0 / max( uLightGridBounds.y, 1.0 );
	float cellIndex = sampleCoord.x + sampleCoord.z * uLightGridBounds.x;
	return vec2( ( cellIndex + 0.5 ) * invCellsX, ( sampleCoord.y + 0.5 ) * invCellsY );
}

vec3 ProbeWorldPosition( vec3 sampleCoord ) {
	vec3 idealPosition = uLightGridOrigin.xyz + sampleCoord * uLightGridSize.xyz;
	if ( uProbeInfo.y <= 0.0 ) {
		return idealPosition;
	}

	vec3 encodedRelocation = texture2D( uLightGridProbeAtlas, ProbeGridCoord( sampleCoord ) ).rgb * 255.0;
	vec3 relocation = ( encodedRelocation - vec3( 128.0 ) ) * ( uProbeInfo.x / 127.0 );
	return idealPosition + relocation;
}

float VisibilityWeight( vec4 moments, float receiverDistance, vec3 worldNormal, vec3 probeToReceiverDir ) {
	if ( moments.b <= 0.001 ) {
		return 1.0;
	}

	float maxDistance = max( uVisibilityInfo.x, 1.0 );
	float meanDistance = moments.r * maxDistance;
	float meanDistanceSq = moments.g * maxDistance * maxDistance;
	float normalBias = uVisibilityInfo.y * ( 0.25 + 0.75 * max( dot( worldNormal, -probeToReceiverDir ), 0.0 ) );
	float biasedDistance = max( receiverDistance - normalBias, 0.0 );
	if ( biasedDistance <= meanDistance ) {
		return 1.0;
	}

	float variance = max( meanDistanceSq - meanDistance * meanDistance, maxDistance * maxDistance * 0.000025 );
	float delta = biasedDistance - meanDistance;
	float chebyshev = variance / ( variance + delta * delta );
	chebyshev = clamp( chebyshev, uVisibilityInfo.z, 1.0 );
	return pow( chebyshev, max( uVisibilityInfo.w, 1.0 ) );
}

float LightGridContributionScale() {
	if ( abs( uBlendInfo.y ) <= 0.001 || uBlendInfo.z <= 0.0 ) {
		return uBlendInfo.x;
	}

	float portalDistance = abs( dot( vWorldPosition, uPortalPlane.xyz ) + uPortalPlane.w );
	vec3 nearestPortalBoundsPoint = clamp( vWorldPosition, uPortalBoundsMin.xyz, uPortalBoundsMax.xyz );
	float apertureDistance = length( vWorldPosition - nearestPortalBoundsPoint );
	float portalFade =
		( 1.0 - smoothstep( 0.0, uBlendInfo.z, portalDistance ) ) *
		( 1.0 - smoothstep( 0.0, uBlendInfo.z, apertureDistance ) );
	float neighborWeight = 0.5 * portalFade;
	float blendWeight = uBlendInfo.y > 0.0 ? neighborWeight : 1.0 - neighborWeight;
	return uBlendInfo.x * blendWeight;
}

void main() {
	vec4 bumpSample = texture2D( uBumpMap, vBumpTexCoord );
	vec3 localNormal = normalize( vec3( bumpSample.a, bumpSample.g, bumpSample.b ) * 2.0 - 1.0 );
	vec3 worldNormal = normalize(
		vWorldTangent * localNormal.x +
		vWorldBitangent * localNormal.y +
		vWorldNormal * localNormal.z );

	vec2 octCoord = ( OctEncode( worldNormal ) + vec2( 1.0 ) ) * 0.5;

	float gridCoordX;
	float gridCoordY;
	float gridCoordZ;
	float fracX;
	float fracY;
	float fracZ;
	vec3 lightOrigin = vWorldPosition - uLightGridOrigin.xyz;
	ComputeGridAxis( lightOrigin.x, uLightGridSize.x, uLightGridBounds.x, gridCoordX, fracX );
	ComputeGridAxis( lightOrigin.y, uLightGridSize.y, uLightGridBounds.y, gridCoordY, fracY );
	ComputeGridAxis( lightOrigin.z, uLightGridSize.z, uLightGridBounds.z, gridCoordZ, fracZ );

	vec3 irradiance = vec3( 0.0 );
	float validFactor = 0.0;

	for ( int i = 0; i < 8; i++ ) {
		float fi = float( i );
		vec3 corner = vec3(
			mod( fi, 2.0 ),
			mod( floor( fi * 0.5 ), 2.0 ),
			floor( fi * 0.25 ) );
		float factor =
			( corner.x > 0.0 ? fracX : 1.0 - fracX ) *
			( corner.y > 0.0 ? fracY : 1.0 - fracY ) *
			( corner.z > 0.0 ? fracZ : 1.0 - fracZ );
		if ( factor <= 0.0 ) {
			continue;
		}

		vec3 sampleCoord = vec3( gridCoordX, gridCoordY, gridCoordZ ) + corner;
		vec2 atlasCoord = ProbeAtlasCoord( sampleCoord, octCoord );

		vec3 sampleColor = texture2D( uLightGridAtlas, atlasCoord ).rgb;
		if ( dot( sampleColor, vec3( 1.0 ) ) < 0.0001 ) {
			continue;
		}

		vec3 probePosition = ProbeWorldPosition( sampleCoord );
		vec3 probeToReceiver = vWorldPosition - probePosition;
		float receiverDistance = length( probeToReceiver );
		vec3 probeToReceiverDir = receiverDistance > 0.001 ? probeToReceiver / receiverDistance : worldNormal;
		vec2 visibilityOctCoord = ( OctEncode( probeToReceiverDir ) + vec2( 1.0 ) ) * 0.5;
		vec4 visibilityMoments = texture2D( uLightGridVisibilityAtlas, ProbeAtlasCoord( sampleCoord, visibilityOctCoord ) );
		float visibility = VisibilityWeight( visibilityMoments, receiverDistance, worldNormal, probeToReceiverDir );

		irradiance += sampleColor * factor * visibility;
		validFactor += factor;
	}

	if ( validFactor > 0.0 && validFactor < 0.9999 ) {
		irradiance *= 1.0 / validFactor;
	}

	vec3 diffuseSample = texture2D( uDiffuseMap, vDiffuseTexCoord ).rgb;
	vec3 diffuseLighting = irradiance * diffuseSample * uDiffuseColor.rgb * vVertexColor * LightGridContributionScale();
	gl_FragColor = vec4( diffuseLighting, 1.0 );
}
