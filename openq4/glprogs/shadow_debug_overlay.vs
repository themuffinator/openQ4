#version 110

uniform vec2 uScreenSize;

varying vec2 vTexCoord;

void main() {
	vec2 position = gl_Vertex.xy;
	vec2 clip;
	clip.x = position.x / uScreenSize.x * 2.0 - 1.0;
	clip.y = 1.0 - position.y / uScreenSize.y * 2.0;

	gl_Position = vec4( clip, 0.0, 1.0 );
	vTexCoord = gl_MultiTexCoord0.xy;
}
