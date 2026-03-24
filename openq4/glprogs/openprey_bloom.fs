uniform sampler2D Scene;
uniform sampler2D BloomTex;
uniform float bloomIntensity;
uniform float bloomEnabled;
uniform float toneMapEnabled;
uniform float hdrExposure;
uniform float hdrWhitePoint;
uniform float hdrLift;
uniform float hdrPostGamma;
uniform float hdrGain;
uniform float hdrVibrance;
uniform float hdrSaturation;
uniform float hdrContrast;

float ACESFilmScalar( float x ) {
	const float a = 2.51;
	const float b = 0.03;
	const float c = 2.43;
	const float d = 0.59;
	const float e = 0.14;
	return ( x * ( a * x + b ) ) / ( x * ( c * x + d ) + e );
}

vec3 ACESFilm( vec3 x ) {
	const float a = 2.51;
	const float b = 0.03;
	const float c = 2.43;
	const float d = 0.59;
	const float e = 0.14;
	return ( x * ( a * x + b ) ) / ( x * ( c * x + d ) + e );
}

vec3 ToneMapHDR( vec3 color ) {
	float safeExposure = max( hdrExposure, 0.001 );
	vec3 exposedColor = color * safeExposure;
	float safeWhitePoint = max( hdrWhitePoint, 1.0 );
	float whiteScale = 1.0 / max( ACESFilmScalar( safeWhitePoint * safeExposure ), 0.0001 );
	return clamp( ACESFilm( exposedColor ) * whiteScale, 0.0, 1.0 );
}

vec3 ApplyLiftGammaGain( vec3 color ) {
	color = max( color + vec3( hdrLift ), vec3( 0.0 ) );
	color = pow( color, vec3( 1.0 / max( hdrPostGamma, 0.001 ) ) );
	color *= hdrGain;
	return color;
}

vec3 ApplyVibrance( vec3 color ) {
	float luma = dot( color, vec3( 0.2126, 0.7152, 0.0722 ) );
	float maxChannel = max( max( color.r, color.g ), color.b );
	float minChannel = min( min( color.r, color.g ), color.b );
	float saturation = maxChannel - minChannel;
	float vibranceMix = clamp( 1.0 + hdrVibrance * ( 1.0 - saturation ), 0.0, 2.0 );
	return mix( vec3( luma ), color, vibranceMix );
}

vec3 ApplyColorAdjustments( vec3 color ) {
	color = ApplyLiftGammaGain( color );
	color = ApplyVibrance( color );

	float luma = dot( color, vec3( 0.2126, 0.7152, 0.0722 ) );
	color = mix( vec3( luma ), color, hdrSaturation );
	color = ( color - 0.5 ) * hdrContrast + 0.5;
	return clamp( color, 0.0, 1.0 );
}

void main() {
	vec2 uv = gl_TexCoord[0].st;
	vec4 sceneSample = texture2D( Scene, uv );
	vec3 color = sceneSample.rgb;

	if ( bloomEnabled > 0.5 && bloomIntensity > 0.0001 ) {
		color += texture2D( BloomTex, uv ).rgb * bloomIntensity;
	}

	if ( toneMapEnabled > 0.5 ) {
		color = ToneMapHDR( color );
		color = ApplyColorAdjustments( color );
	}

	gl_FragColor = vec4( color, sceneSample.a );
}
