uniform sampler2D tex;
uniform vec3 id;

varying vec2 texCoord;

void main()
{
    vec4 texColor;
    texColor.a = texture2D(tex, texCoord).a;
    texColor.rgb = id;
    gl_FragColor = texColor;
}

