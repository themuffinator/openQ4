#version 110

uniform float uMode;
uniform vec4 uColor;
uniform float uPointLight;
uniform float uAtlasDiv;
uniform float uCascadeCount;
uniform float uPassMapped;
uniform float uGlyphCode;

uniform sampler2D uShadowAtlasMap;
uniform samplerCube uPointShadowMap;

varying vec2 vTexCoord;

const float kModePanel = 0.0;
const float kModeSolid = 1.0;
const float kModeGlyph = 2.0;

float UnpackDepth16( vec2 rg ) {
	return rg.x + rg.y * ( 1.0 / 255.0 );
}

vec3 PointFaceDirection( float faceIndex, vec2 uv ) {
	vec2 st = uv * 2.0 - 1.0;

	if ( faceIndex < 0.5 ) {
		return normalize( vec3( 1.0, -st.y, -st.x ) );
	}
	if ( faceIndex < 1.5 ) {
		return normalize( vec3( -1.0, -st.y, st.x ) );
	}
	if ( faceIndex < 2.5 ) {
		return normalize( vec3( st.x, 1.0, st.y ) );
	}
	if ( faceIndex < 3.5 ) {
		return normalize( vec3( st.x, -1.0, -st.y ) );
	}
	if ( faceIndex < 4.5 ) {
		return normalize( vec3( st.x, -st.y, 1.0 ) );
	}
	return normalize( vec3( -st.x, -st.y, -1.0 ) );
}

void GetGlyphRows( float glyph, out vec4 rowsA, out vec4 rowsB ) {
	rowsA = vec4( 0.0 );
	rowsB = vec4( 0.0 );

	if ( glyph < 32.5 ) {
		return;
	}
	if ( abs( glyph - 47.0 ) < 0.5 ) {
		rowsA = vec4( 1.0, 2.0, 4.0, 8.0 );
		rowsB = vec4( 16.0, 0.0, 0.0, 0.0 );
		return;
	}
	if ( abs( glyph - 48.0 ) < 0.5 ) {
		rowsA = vec4( 14.0, 17.0, 19.0, 21.0 );
		rowsB = vec4( 25.0, 17.0, 14.0, 0.0 );
		return;
	}
	if ( abs( glyph - 49.0 ) < 0.5 ) {
		rowsA = vec4( 4.0, 12.0, 4.0, 4.0 );
		rowsB = vec4( 4.0, 4.0, 14.0, 0.0 );
		return;
	}
	if ( abs( glyph - 50.0 ) < 0.5 ) {
		rowsA = vec4( 14.0, 17.0, 1.0, 2.0 );
		rowsB = vec4( 4.0, 8.0, 31.0, 0.0 );
		return;
	}
	if ( abs( glyph - 51.0 ) < 0.5 ) {
		rowsA = vec4( 30.0, 1.0, 1.0, 14.0 );
		rowsB = vec4( 1.0, 1.0, 30.0, 0.0 );
		return;
	}
	if ( abs( glyph - 52.0 ) < 0.5 ) {
		rowsA = vec4( 2.0, 6.0, 10.0, 18.0 );
		rowsB = vec4( 31.0, 2.0, 2.0, 0.0 );
		return;
	}
	if ( abs( glyph - 53.0 ) < 0.5 ) {
		rowsA = vec4( 31.0, 16.0, 30.0, 1.0 );
		rowsB = vec4( 1.0, 17.0, 14.0, 0.0 );
		return;
	}
	if ( abs( glyph - 54.0 ) < 0.5 ) {
		rowsA = vec4( 7.0, 8.0, 16.0, 30.0 );
		rowsB = vec4( 17.0, 17.0, 14.0, 0.0 );
		return;
	}
	if ( abs( glyph - 55.0 ) < 0.5 ) {
		rowsA = vec4( 31.0, 1.0, 2.0, 4.0 );
		rowsB = vec4( 8.0, 8.0, 8.0, 0.0 );
		return;
	}
	if ( abs( glyph - 56.0 ) < 0.5 ) {
		rowsA = vec4( 14.0, 17.0, 17.0, 14.0 );
		rowsB = vec4( 17.0, 17.0, 14.0, 0.0 );
		return;
	}
	if ( abs( glyph - 57.0 ) < 0.5 ) {
		rowsA = vec4( 14.0, 17.0, 17.0, 15.0 );
		rowsB = vec4( 1.0, 2.0, 28.0, 0.0 );
		return;
	}
	if ( abs( glyph - 65.0 ) < 0.5 ) {
		rowsA = vec4( 14.0, 17.0, 17.0, 31.0 );
		rowsB = vec4( 17.0, 17.0, 17.0, 0.0 );
		return;
	}
	if ( abs( glyph - 66.0 ) < 0.5 ) {
		rowsA = vec4( 30.0, 17.0, 17.0, 30.0 );
		rowsB = vec4( 17.0, 17.0, 30.0, 0.0 );
		return;
	}
	if ( abs( glyph - 67.0 ) < 0.5 ) {
		rowsA = vec4( 14.0, 17.0, 16.0, 16.0 );
		rowsB = vec4( 16.0, 17.0, 14.0, 0.0 );
		return;
	}
	if ( abs( glyph - 70.0 ) < 0.5 ) {
		rowsA = vec4( 31.0, 16.0, 16.0, 30.0 );
		rowsB = vec4( 16.0, 16.0, 16.0, 0.0 );
		return;
	}
	if ( abs( glyph - 71.0 ) < 0.5 ) {
		rowsA = vec4( 14.0, 17.0, 16.0, 23.0 );
		rowsB = vec4( 17.0, 17.0, 14.0, 0.0 );
		return;
	}
	if ( abs( glyph - 73.0 ) < 0.5 ) {
		rowsA = vec4( 14.0, 4.0, 4.0, 4.0 );
		rowsB = vec4( 4.0, 4.0, 14.0, 0.0 );
		return;
	}
	if ( abs( glyph - 74.0 ) < 0.5 ) {
		rowsA = vec4( 1.0, 1.0, 1.0, 1.0 );
		rowsB = vec4( 17.0, 17.0, 14.0, 0.0 );
		return;
	}
	if ( abs( glyph - 76.0 ) < 0.5 ) {
		rowsA = vec4( 16.0, 16.0, 16.0, 16.0 );
		rowsB = vec4( 16.0, 16.0, 31.0, 0.0 );
		return;
	}
	if ( abs( glyph - 77.0 ) < 0.5 ) {
		rowsA = vec4( 17.0, 27.0, 21.0, 17.0 );
		rowsB = vec4( 17.0, 17.0, 17.0, 0.0 );
		return;
	}
	if ( abs( glyph - 78.0 ) < 0.5 ) {
		rowsA = vec4( 17.0, 25.0, 21.0, 19.0 );
		rowsB = vec4( 17.0, 17.0, 17.0, 0.0 );
		return;
	}
	if ( abs( glyph - 79.0 ) < 0.5 ) {
		rowsA = vec4( 14.0, 17.0, 17.0, 17.0 );
		rowsB = vec4( 17.0, 17.0, 14.0, 0.0 );
		return;
	}
	if ( abs( glyph - 80.0 ) < 0.5 ) {
		rowsA = vec4( 30.0, 17.0, 17.0, 30.0 );
		rowsB = vec4( 16.0, 16.0, 16.0, 0.0 );
		return;
	}
	if ( abs( glyph - 82.0 ) < 0.5 ) {
		rowsA = vec4( 30.0, 17.0, 17.0, 30.0 );
		rowsB = vec4( 20.0, 18.0, 17.0, 0.0 );
		return;
	}
	if ( abs( glyph - 83.0 ) < 0.5 ) {
		rowsA = vec4( 15.0, 16.0, 16.0, 14.0 );
		rowsB = vec4( 1.0, 1.0, 30.0, 0.0 );
		return;
	}
	if ( abs( glyph - 84.0 ) < 0.5 ) {
		rowsA = vec4( 31.0, 4.0, 4.0, 4.0 );
		rowsB = vec4( 4.0, 4.0, 4.0, 0.0 );
		return;
	}
	if ( abs( glyph - 85.0 ) < 0.5 ) {
		rowsA = vec4( 17.0, 17.0, 17.0, 17.0 );
		rowsB = vec4( 17.0, 17.0, 14.0, 0.0 );
		return;
	}
}

float GlyphAlpha( vec2 uv, float glyph ) {
	vec2 local = ( uv - vec2( 0.10, 0.08 ) ) / vec2( 0.80, 0.84 );
	if ( local.x < 0.0 || local.x > 1.0 || local.y < 0.0 || local.y > 1.0 ) {
		return 0.0;
	}

	float col = floor( local.x * 5.0 );
	float row = floor( local.y * 7.0 );
	col = clamp( col, 0.0, 4.0 );
	row = clamp( row, 0.0, 6.0 );

	vec4 rowsA;
	vec4 rowsB;
	GetGlyphRows( glyph, rowsA, rowsB );

	float rowMask = rowsA.x;
	if ( row > 0.5 ) {
		rowMask = rowsA.y;
	}
	if ( row > 1.5 ) {
		rowMask = rowsA.z;
	}
	if ( row > 2.5 ) {
		rowMask = rowsA.w;
	}
	if ( row > 3.5 ) {
		rowMask = rowsB.x;
	}
	if ( row > 4.5 ) {
		rowMask = rowsB.y;
	}
	if ( row > 5.5 ) {
		rowMask = rowsB.z;
	}

	float divisor = pow( 2.0, 4.0 - col );
	return mod( floor( rowMask / divisor ), 2.0 );
}

vec4 PanelColor( vec2 uv ) {
	vec3 borderColor = ( uPassMapped > 0.5 ) ? vec3( 0.14, 0.78, 0.94 ) : vec3( 0.84, 0.24, 0.20 );
	if ( uPointLight > 0.5 ) {
		vec2 tiledUv = vec2( uv.x * 3.0, uv.y * 2.0 );
		vec2 localUv = fract( tiledUv );
		float faceIndex = floor( tiledUv.x ) + floor( tiledUv.y ) * 3.0;

		if ( faceIndex < 0.0 || faceIndex > 5.0 ) {
			return vec4( 0.02, 0.02, 0.03, 0.92 );
		}

		vec3 direction = PointFaceDirection( faceIndex, localUv );
		float depth = UnpackDepth16( textureCube( uPointShadowMap, direction ).rg );
		float shade = clamp( pow( max( 1.0 - depth, 0.0 ), 0.55 ), 0.0, 1.0 );
		vec3 base = mix( vec3( 0.04, 0.04, 0.05 ), vec3( 0.92 ), shade );
		float border = max( step( localUv.x, 0.025 ), max( step( localUv.y, 0.025 ), max( step( 0.975, localUv.x ), step( 0.975, localUv.y ) ) ) );
		base = mix( base, borderColor, border * 0.85 );
		return vec4( base, 0.95 );
	}

	float atlasDiv = max( uAtlasDiv, 1.0 );
	vec2 gridUv = fract( uv * atlasDiv );
	float tileIndex = floor( uv.x * atlasDiv ) + floor( uv.y * atlasDiv ) * atlasDiv;
	float active = step( tileIndex + 0.5, uCascadeCount );
	float depth = texture2D( uShadowAtlasMap, uv ).r;
	float shade = clamp( pow( max( 1.0 - depth, 0.0 ), 0.55 ), 0.0, 1.0 );
	vec3 base = mix( vec3( 0.04, 0.04, 0.05 ), vec3( 0.92 ), shade );
	base *= mix( 0.22, 1.0, active );
	float border = max( step( gridUv.x, 0.025 ), max( step( gridUv.y, 0.025 ), max( step( 0.975, gridUv.x ), step( 0.975, gridUv.y ) ) ) );
	base = mix( base, borderColor, border * 0.85 );
	return vec4( base, 0.95 );
}

void main() {
	if ( abs( uMode - kModeSolid ) < 0.5 ) {
		gl_FragColor = uColor;
		return;
	}

	if ( abs( uMode - kModeGlyph ) < 0.5 ) {
		float alpha = GlyphAlpha( vTexCoord, uGlyphCode );
		gl_FragColor = vec4( uColor.rgb, uColor.a * alpha );
		return;
	}

	gl_FragColor = PanelColor( vTexCoord );
}
