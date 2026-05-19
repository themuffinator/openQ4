uniform sampler2D ParticleTexture;
uniform sampler2D SceneDepth;
uniform vec4 stageColor;
uniform float vertexColorMode;
uniform vec2 depthProjection;
uniform vec2 viewportOrigin;
uniform vec2 invDepthTexSize;
uniform float fadeDistance;
uniform float additiveBlend;

varying vec2 var_TexCoord;
varying vec4 var_Color;

const float COVERAGE_EPSILON = 0.00001;

float ViewSpaceZFromDepth( float depth ) {
	float ndcDepth = depth * 2.0 - 1.0;
	float denom = ndcDepth + depthProjection.x;
	if ( abs( denom ) < 0.00001 ) {
		denom = ( denom < 0.0 ) ? -0.00001 : 0.00001;
	}
	return ( -depthProjection.y ) / denom;
}

vec4 VertexColorFactor() {
	if ( vertexColorMode < 0.5 ) {
		return vec4( 1.0 );
	}
	if ( vertexColorMode < 1.5 ) {
		return var_Color;
	}
	return vec4( vec3( 1.0 ) - var_Color.rgb, var_Color.a );
}

void main() {
	vec4 particle = texture2D( ParticleTexture, var_TexCoord ) * stageColor * VertexColorFactor();
	float particleCoverage = max( particle.a, max( max( particle.r, particle.g ), particle.b ) );
	if ( particleCoverage <= COVERAGE_EPSILON ) {
		discard;
	}

	vec2 depthUv = ( gl_FragCoord.xy - viewportOrigin ) * invDepthTexSize;
	float fade = 1.0;

	if ( depthUv.x >= 0.0 && depthUv.y >= 0.0 && depthUv.x <= 1.0 && depthUv.y <= 1.0 ) {
		float sceneDepth = texture2D( SceneDepth, depthUv ).r;
		float rawDepthSeparation = sceneDepth - gl_FragCoord.z;
		// Only soften fragments in front of scene depth; sky/background depth should not erase smoke.
		if ( sceneDepth < 0.99999 && rawDepthSeparation > 0.0 ) {
			float sceneViewDepth = abs( ViewSpaceZFromDepth( sceneDepth ) );
			float particleViewDepth = abs( ViewSpaceZFromDepth( gl_FragCoord.z ) );
			float separation = max( sceneViewDepth - particleViewDepth, 0.0 );
			if ( separation <= 0.0001 && rawDepthSeparation > 0.0 ) {
				float depthToWorldScale = ( 2.0 * particleViewDepth * particleViewDepth ) / max( abs( depthProjection.y ), 0.0001 );
				separation = rawDepthSeparation * max( depthToWorldScale, 1.0 );
			}
			float softFade = smoothstep( 0.0, max( fadeDistance, 1.0 ), separation );
			fade = softFade;
		}
	}

	if ( additiveBlend > 0.5 ) {
		particle.rgb *= fade;
		particle.a *= fade;
	} else {
		particle.a *= fade;
	}

	gl_FragColor = particle;
}
