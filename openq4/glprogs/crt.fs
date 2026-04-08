uniform sampler2D Scene;
uniform vec2 invTexSize;
uniform float crtAmount;
uniform float scanlineStrength;
uniform float maskStrength;
uniform float curvature;
uniform float chromaticAberration;
uniform float timeSeconds;

vec2 WarpUV( vec2 uv ) {
	vec2 centered = uv * 2.0 - 1.0;
	vec2 squared = centered * centered;
	centered *= 1.0 + squared.yx * ( curvature * 1.6 );
	centered.x *= 1.0 + squared.y * ( curvature * 0.25 );
	centered.y *= 1.0 + squared.x * ( curvature * 0.20 );
	return centered * 0.5 + 0.5;
}

float ScreenMask( vec2 uv ) {
	vec2 edge = min( uv, 1.0 - uv );
	float maskX = smoothstep( 0.0, 0.018, edge.x );
	float maskY = smoothstep( 0.0, 0.018, edge.y );
	return maskX * maskY;
}

vec3 SampleHorizontalBeam( vec2 uv ) {
	vec2 texel = vec2( invTexSize.x, 0.0 );
	vec3 sample0 = texture2D( Scene, uv - texel * 2.0 ).rgb;
	vec3 sample1 = texture2D( Scene, uv - texel ).rgb;
	vec3 sample2 = texture2D( Scene, uv ).rgb;
	vec3 sample3 = texture2D( Scene, uv + texel ).rgb;
	vec3 sample4 = texture2D( Scene, uv + texel * 2.0 ).rgb;
	return sample0 * 0.08 + sample1 * 0.22 + sample2 * 0.40 + sample3 * 0.22 + sample4 * 0.08;
}

vec3 SampleCRTColor( vec2 uv ) {
	vec3 beam = SampleHorizontalBeam( uv );
	vec2 radial = uv - 0.5;
	float spread = 1.0 + length( radial ) * 2.25;
	vec2 chroma = vec2(
		chromaticAberration * invTexSize.x * spread,
		chromaticAberration * invTexSize.y * 0.35 * spread );

	vec3 chromaColor;
	chromaColor.r = texture2D( Scene, uv + chroma ).r;
	chromaColor.g = beam.g;
	chromaColor.b = texture2D( Scene, uv - chroma ).b;

	float mixFactor = clamp( 0.35 + chromaticAberration * 0.12, 0.0, 1.0 );
	return mix( beam, chromaColor, mixFactor );
}

float ScanlineFactor( float luma ) {
	float phase = gl_FragCoord.y * 3.14159265 + sin( timeSeconds * 7.0 ) * 0.35;
	float wave = 0.5 + 0.5 * cos( phase );
	wave *= wave;

	float darkFloor = 0.22 + luma * 0.35;
	float lineValue = mix( darkFloor, 1.0, wave );
	return mix( 1.0, lineValue, scanlineStrength );
}

vec3 PhosphorMask( void ) {
	float column = mod( floor( gl_FragCoord.x ), 3.0 );
	vec3 triad;
	if ( column < 0.5 ) {
		triad = vec3( 1.18, 0.80, 0.80 );
	} else if ( column < 1.5 ) {
		triad = vec3( 0.80, 1.18, 0.80 );
	} else {
		triad = vec3( 0.80, 0.80, 1.18 );
	}

	float slot = ( mod( floor( gl_FragCoord.y ), 2.0 ) < 0.5 ) ? 1.0 : 0.94;
	return mix( vec3( 1.0 ), triad * slot, maskStrength );
}

void main() {
	vec2 baseUV = gl_TexCoord[0].st;
	vec4 originalSample = texture2D( Scene, baseUV );
	vec2 warpedUV = WarpUV( baseUV );
	float screenMask = ScreenMask( warpedUV );

	if ( screenMask <= 0.0 ) {
		gl_FragColor = vec4( 0.0, 0.0, 0.0, originalSample.a );
		return;
	}

	vec3 crtColor = SampleCRTColor( warpedUV );
	float luma = dot( crtColor, vec3( 0.2126, 0.7152, 0.0722 ) );
	crtColor *= ScanlineFactor( luma );
	crtColor *= PhosphorMask();

	float edge = dot( warpedUV * 2.0 - 1.0, warpedUV * 2.0 - 1.0 );
	float vignette = clamp( 1.0 - edge * 0.22, 0.0, 1.0 );
	float shimmer = 0.985 + 0.015 * sin( gl_FragCoord.y * 0.35 + timeSeconds * 11.0 );
	crtColor *= mix( 1.0, vignette * shimmer, 0.85 );
	crtColor *= screenMask;

	vec3 finalColor = mix( originalSample.rgb, crtColor, crtAmount );
	gl_FragColor = vec4( clamp( finalColor, 0.0, 1.0 ), originalSample.a );
}
