uniform sampler2D tex;
//uniform vec3 pick_id;

varying vec2 texCoord;
varying vec3 normal;

void main()
{
    vec4 texColor;
    texColor.a = texture2D(tex, texCoord).a;
    texColor.rgb = normal;
    gl_FragColor = texColor;
}

