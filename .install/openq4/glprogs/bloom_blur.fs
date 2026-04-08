uniform sampler2D Scene;
uniform vec2 invTexSize;
uniform vec2 blurAxis;
uniform float blurRadius;

void main() {
	vec2 uv = gl_TexCoord[0].st;
	vec2 stepSize = blurAxis * invTexSize * max( blurRadius, 0.1 );
	vec3 color = texture2D( Scene, uv ).rgb * 0.19648255;

	color += texture2D( Scene, uv + stepSize * 1.4117647 ).rgb * 0.29690696;
	color += texture2D( Scene, uv - stepSize * 1.4117647 ).rgb * 0.29690696;
	color += texture2D( Scene, uv + stepSize * 3.2941176 ).rgb * 0.0944704;
	color += texture2D( Scene, uv - stepSize * 3.2941176 ).rgb * 0.0944704;
	color += texture2D( Scene, uv + stepSize * 5.1764706 ).rgb * 0.01038136;
	color += texture2D( Scene, uv - stepSize * 5.1764706 ).rgb * 0.01038136;

	gl_FragColor = vec4( color, 1.0 );
}
