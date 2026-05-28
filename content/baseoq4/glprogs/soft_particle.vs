varying vec2 var_TexCoord;
varying vec4 var_Color;

void main() {
	vec4 texCoord = gl_TextureMatrix[0] * gl_MultiTexCoord0;
	if ( abs( texCoord.q ) > 0.00001 ) {
		var_TexCoord = texCoord.st / texCoord.q;
	} else {
		var_TexCoord = texCoord.st;
	}

	var_Color = gl_Color;
	gl_Position = ftransform();
}
