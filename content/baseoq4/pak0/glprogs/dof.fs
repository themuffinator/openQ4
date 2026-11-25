// openQ4 depth of field (RB_STD_DepthOfField in draw_common.cpp; the lens
// model and the block layout are src/renderer/DepthOfFieldCore.h). One
// program, seven passes chosen by dofParams[2].z. The Vulkan port is
// src/renderer/Vulkan/shaders/post_dof.frag; keep the two in step.
//
// The circle of confusion (CoC) is a signed radius in view pixels: negative
// in front of the focus plane, positive behind it. The gather is a
// scatter-as-gather over a golden-angle spiral: a sample contributes to a
// pixel when its own blur disc covers that pixel, a farther sample may not
// spill over a sharper pixel in front of it, and a nearer one may, so
// foreground blur bleeds over the focus plane the way a lens does.

uniform sampler2D Scene;		// the pass's primary input
uniform sampler2D DepthBuffer;	// the view's depth
uniform sampler2D AuxA;			// the focus target, or the dilated tiles (gather)
uniform sampler2D AuxB;			// the filtered gather (composite)
uniform vec4 dofParams[10];

#define outInvSize			dofParams[0].xy
#define srcInvSize			dofParams[0].zw
#define sceneUVScale		dofParams[1].xy
#define depthUVScale		dofParams[1].zw
#define depthProjection		dofParams[2].xy
#define dofPass				dofParams[2].z
#define dofDebug			dofParams[2].w
#define manualFocus			dofParams[3].x
#define lensScale			dofParams[3].y
#define focalMm				dofParams[3].z
#define fixedScale			dofParams[3].w
#define focusRange			dofParams[4].x
#define nearScale			dofParams[4].y
#define farScale			dofParams[4].z
#define maxRadius			dofParams[4].w
#define sampleCount			dofParams[5].x
#define bladeCount			dofParams[5].y
#define bladeRotation		dofParams[5].z
#define bladeRoundness		dofParams[5].w
#define highlightBoost		dofParams[6].x
#define highlightThreshold	dofParams[6].y
#define weaponFocus			dofParams[6].z
#define gatherScale			dofParams[6].w
#define focusUV				dofParams[7].xy
#define focusBlend			dofParams[7].z
#define focusHistory		dofParams[7].w
#define tileSize			dofParams[8].x
#define dilateTiles			dofParams[8].y
#define viewInvSize			dofParams[8].zw
#define tileUVScale			dofParams[9].xy
#define minSamples			dofParams[9].z

const float kUnitsToMm = 25.4;
const float kGoldenAngle = 2.39996323;
const float kTwoPi = 6.28318531;
const int kMaxSamples = 192;
const int kTileSize = 8;
const int kMaxDilate = 4;
const vec3 kLuma = vec3( 0.2126, 0.7152, 0.0722 );

float RawDepth( vec2 uv ) {
	return texture2D( DepthBuffer, uv * depthUVScale ).x;
}

// The view weapon draws into the depth range [0, 0.5].
bool IsWeaponDepth( float depth ) {
	return depth < 0.5;
}

// Distance along the view axis, in world units.
float ViewDistance( float depth ) {
	if ( IsWeaponDepth( depth ) ) {
		depth *= 2.0;
	}
	float denom = depth * 2.0 - 1.0 + depthProjection.x;
	if ( denom > -0.000001 ) {
		return 1000000.0;
	}
	return clamp( depthProjection.y / denom, 0.01, 1000000.0 );
}

float FocusDistance() {
	if ( manualFocus > 0.0 ) {
		return manualFocus;
	}
	return exp2( texture2D( AuxA, vec2( 0.5, 0.5 ) ).r );
}

// DepthOfFieldCore_CircleOfConfusion
float CircleOfConfusion( float depth, float focus ) {
	if ( weaponFocus > 0.5 && IsWeaponDepth( depth ) ) {
		return 0.0;
	}
	float dist = ViewDistance( depth );
	float delta = dist - focus;
	float outside = abs( delta ) - focusRange;
	if ( outside <= 0.0 ) {
		return 0.0;
	}
	float d = focus + ( delta < 0.0 ? -outside : outside );
	float k = fixedScale > 0.0 ? fixedScale : lensScale / max( focus * kUnitsToMm - focalMm, 1.0 );
	float coc = k * ( 1.0 - focus / max( d, 0.01 ) );
	coc *= coc < 0.0 ? nearScale : farScale;
	return clamp( coc, -maxRadius, maxRadius );
}

void Order( inout float a, inout float b ) {
	float low = min( a, b );
	b = max( a, b );
	a = low;
}

// 1x1: the autofocus distance (log2 world units), eased toward the median
// depth of five taps around the focus point so one thin edge cannot grab it.
vec4 FocusPass() {
	vec2 offset = vec2( 0.012, 0.012 * viewInvSize.y / viewInvSize.x );
	float d0 = ViewDistance( RawDepth( focusUV ) );
	float d1 = ViewDistance( RawDepth( focusUV + vec2( offset.x, 0.0 ) ) );
	float d2 = ViewDistance( RawDepth( focusUV - vec2( offset.x, 0.0 ) ) );
	float d3 = ViewDistance( RawDepth( focusUV + vec2( 0.0, offset.y ) ) );
	float d4 = ViewDistance( RawDepth( focusUV - vec2( 0.0, offset.y ) ) );
	Order( d0, d1 ); Order( d3, d4 ); Order( d2, d4 ); Order( d2, d3 ); Order( d0, d3 );
	Order( d0, d2 ); Order( d1, d4 ); Order( d1, d3 ); Order( d1, d2 );
	float measured = log2( clamp( d2, 1.0, 65536.0 ) );
	if ( focusHistory > 0.5 ) {
		measured = mix( texture2D( AuxA, vec2( 0.5, 0.5 ) ).r, measured, focusBlend );
	}
	return vec4( measured, 0.0, 0.0, 1.0 );
}

// Colour and CoC at the gather resolution. At half resolution the four view
// pixels under a texel merge, the nearest CoC winning so the foreground keeps
// its silhouette, and each colour weighted by how close its CoC is.
vec4 PrefilterPass( vec2 uv ) {
	float focus = FocusDistance();
	if ( gatherScale > 0.99 ) {
		vec3 color = max( texture2D( Scene, uv * sceneUVScale ).rgb, vec3( 0.0 ) );
		return vec4( color, CircleOfConfusion( RawDepth( uv ), focus ) );
	}
	vec2 h = 0.5 * viewInvSize;
	vec2 uv0 = uv + vec2( -h.x, -h.y );
	vec2 uv1 = uv + vec2( h.x, -h.y );
	vec2 uv2 = uv + vec2( -h.x, h.y );
	vec2 uv3 = uv + h;
	vec3 c0 = max( texture2D( Scene, uv0 * sceneUVScale ).rgb, vec3( 0.0 ) );
	vec3 c1 = max( texture2D( Scene, uv1 * sceneUVScale ).rgb, vec3( 0.0 ) );
	vec3 c2 = max( texture2D( Scene, uv2 * sceneUVScale ).rgb, vec3( 0.0 ) );
	vec3 c3 = max( texture2D( Scene, uv3 * sceneUVScale ).rgb, vec3( 0.0 ) );
	float k0 = CircleOfConfusion( RawDepth( uv0 ), focus );
	float k1 = CircleOfConfusion( RawDepth( uv1 ), focus );
	float k2 = CircleOfConfusion( RawDepth( uv2 ), focus );
	float k3 = CircleOfConfusion( RawDepth( uv3 ), focus );
	float nearest = min( min( k0, k1 ), min( k2, k3 ) );
	float coc = nearest < 0.0 ? nearest : 0.25 * ( k0 + k1 + k2 + k3 );
	float scale = max( abs( coc ), 1.0 );
	float w0 = 1.0 - 0.9 * clamp( abs( k0 - coc ) / scale, 0.0, 1.0 );
	float w1 = 1.0 - 0.9 * clamp( abs( k1 - coc ) / scale, 0.0, 1.0 );
	float w2 = 1.0 - 0.9 * clamp( abs( k2 - coc ) / scale, 0.0, 1.0 );
	float w3 = 1.0 - 0.9 * clamp( abs( k3 - coc ) / scale, 0.0, 1.0 );
	vec3 color = ( c0 * w0 + c1 * w1 + c2 * w2 + c3 * w3 ) / ( w0 + w1 + w2 + w3 );
	return vec4( color, coc );
}

// Per 8x8 tile of the gather: the largest near CoC and the largest of all.
vec4 TileMaxPass( vec2 uv ) {
	vec2 origin = floor( uv / outInvSize ) * tileSize;
	float nearMax = 0.0;
	float absMax = 0.0;
	for ( int y = 0; y < kTileSize; y++ ) {
		for ( int x = 0; x < kTileSize; x++ ) {
			vec2 sampleUV = ( origin + vec2( float( x ), float( y ) ) + 0.5 ) * srcInvSize;
			float coc = texture2D( Scene, sampleUV ).a;
			nearMax = max( nearMax, -coc );
			absMax = max( absMax, abs( coc ) );
		}
	}
	return vec4( nearMax, absMax, 0.0, 1.0 );
}

// A near tile's blur reaches the tiles within its own radius.
vec4 TileDilatePass( vec2 uv ) {
	int reach = int( dilateTiles + 0.5 );
	float nearMax = 0.0;
	float absMax = 0.0;
	for ( int y = -kMaxDilate; y <= kMaxDilate; y++ ) {
		if ( y < -reach || y > reach ) {
			continue;
		}
		for ( int x = -kMaxDilate; x <= kMaxDilate; x++ ) {
			if ( x < -reach || x > reach ) {
				continue;
			}
			vec4 tile = texture2D( Scene, uv + vec2( float( x ), float( y ) ) * srcInvSize );
			float gap = ( max( abs( float( x ) ), abs( float( y ) ) ) - 1.0 ) * tileSize;
			if ( tile.r * gatherScale >= gap ) {
				nearMax = max( nearMax, tile.r );
			}
			absMax = max( absMax, tile.g );
		}
	}
	return vec4( nearMax, absMax, 0.0, 1.0 );
}

// Scales a disc sample out to the aperture polygon's edge at this angle.
float ApertureScale( float angle ) {
	if ( bladeCount < 2.5 ) {
		return 1.0;
	}
	float segment = kTwoPi / bladeCount;
	float local = mod( angle - bladeRotation, segment ) - 0.5 * segment;
	float polygon = cos( 0.5 * segment ) / cos( local );
	return mix( polygon, 1.0, bladeRoundness );
}

// The samples a disc of this radius (gather texels) takes: the full count at
// the largest blur, and for a smaller disc the same spacing between samples,
// so a disc of half the radius takes a quarter of them, never fewer than
// minSamples. The r_dof camera sends minSamples = sampleCount: every blurred
// pixel takes them all.
int GatherCount( float radius ) {
	float share = radius / max( maxRadius * gatherScale, 0.5 );
	return int( clamp( ceil( sampleCount * share * share ), minSamples, sampleCount ) + 0.5 );
}

vec4 GatherPass( vec2 uv ) {
	vec4 center = texture2D( Scene, uv );
	float centerCoc = center.a;
	float nearReach = texture2D( AuxA, uv * tileUVScale ).r;
	float radius = min( max( abs( centerCoc ), nearReach ), maxRadius ) * gatherScale;
	if ( radius < 0.5 ) {
		return vec4( center.rgb, 0.0 );
	}
	float centerSize = abs( centerCoc ) * gatherScale;
	int count = GatherCount( radius );
	float invCount = 1.0 / float( count );
	// sample i lies at sqrt( ( i + 0.5 ) / count ) of the radius
	float spiralScale = radius * sqrt( invCount );
	float highlightRange = 1.0 / max( 1.0 - highlightThreshold, 0.05 );
	float turnCos = cos( kGoldenAngle );
	float turnSin = sin( kGoldenAngle );
	vec2 dir = vec2( 1.0, 0.0 );
	vec3 accum = center.rgb;
	float total = 1.0;
	float nearCover = 0.0;
	for ( int i = 0; i < kMaxSamples; i++ ) {
		if ( i >= count ) {
			break;
		}
		float r = sqrt( float( i ) + 0.5 ) * spiralScale;
		vec4 s = texture2D( Scene, uv + dir * ( r * ApertureScale( float( i ) * kGoldenAngle ) ) * srcInvSize );
		float sampleSize = abs( s.a ) * gatherScale;
		if ( s.a > centerCoc ) {
			sampleSize = min( sampleSize, centerSize * 2.0 );
		}
		// smoothstep( r - 0.5, r + 0.5, sampleSize ), its unit width folded in
		float m = clamp( sampleSize - r + 0.5, 0.0, 1.0 );
		m = m * m * ( 3.0 - 2.0 * m );
		float highlight = clamp( ( dot( s.rgb, kLuma ) - highlightThreshold ) * highlightRange, 0.0, 4.0 );
		float w = 1.0 + m * highlightBoost * highlight;
		accum += mix( accum / total, s.rgb, m ) * w;
		total += w;
		if ( s.a < centerCoc - 0.5 ) {
			nearCover += m;
		}
		dir = vec2( dir.x * turnCos - dir.y * turnSin, dir.x * turnSin + dir.y * turnCos );
	}
	return vec4( accum / total, clamp( nearCover * invCount * 2.0, 0.0, 1.0 ) );
}

// A tent over the gather settles the spiral's sampling: the 3x3 [1 2 1]
// tent, as four bilinear taps half a texel off the centre.
vec4 PostfilterPass( vec2 uv ) {
	vec2 h = 0.5 * srcInvSize;
	return 0.25 * ( texture2D( Scene, uv - h ) + texture2D( Scene, uv + h )
		+ texture2D( Scene, uv + vec2( h.x, -h.y ) ) + texture2D( Scene, uv + vec2( -h.x, h.y ) ) );
}

vec3 DebugColor( vec3 scene, float coc, vec2 uv ) {
	float grey = dot( scene, kLuma ) * 0.4;
	float amount = clamp( abs( coc ) / max( maxRadius, 1.0 ), 0.0, 1.0 );
	vec3 tint = coc < 0.0 ? vec3( 1.0, 0.5, 0.1 ) : vec3( 0.15, 0.45, 1.0 );
	vec3 color = abs( coc ) < 0.5 ? vec3( grey ) + vec3( 0.0, 0.18, 0.0 )
		: mix( vec3( grey ), tint, 0.35 + 0.65 * sqrt( amount ) );
	if ( manualFocus <= 0.0 ) {
		float ring = length( ( uv - focusUV ) / viewInvSize );
		if ( ring > 7.0 && ring < 9.0 ) {
			color = vec3( 1.0 );
		}
	}
	return color;
}

vec4 CompositePass( vec2 uv ) {
	float coc = CircleOfConfusion( RawDepth( uv ), FocusDistance() );
	if ( dofDebug > 0.5 ) {
		vec4 scene = texture2D( Scene, uv * sceneUVScale );
		return vec4( DebugColor( scene.rgb, coc, uv ), scene.a );
	}
	vec4 blurred = texture2D( AuxB, uv );
	float blend = smoothstep( 0.5, 0.5 + 1.0 / max( gatherScale, 0.25 ), abs( coc ) );
	blend = max( blend, blurred.a );
	if ( blend <= 0.0 ) {
		// in focus: the scene under this pass already holds the pixel
		discard;
	}
	vec4 sharp = texture2D( Scene, uv * sceneUVScale );
	return vec4( mix( sharp.rgb, blurred.rgb, blend ), sharp.a );
}

void main() {
	vec2 uv = gl_TexCoord[0].st;
	vec4 result;
	if ( dofPass < 0.5 ) {
		result = FocusPass();
	} else if ( dofPass < 1.5 ) {
		result = PrefilterPass( uv );
	} else if ( dofPass < 2.5 ) {
		result = TileMaxPass( uv );
	} else if ( dofPass < 3.5 ) {
		result = TileDilatePass( uv );
	} else if ( dofPass < 4.5 ) {
		result = GatherPass( uv );
	} else if ( dofPass < 5.5 ) {
		result = PostfilterPass( uv );
	} else {
		result = CompositePass( uv );
	}
	gl_FragColor = result;
}
