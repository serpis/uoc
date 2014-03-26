uniform sampler2D tex;
uniform sampler2D tex_hue;

varying vec2 texCoord;

void main()
{
    vec4 texColor = texture2D(tex, texCoord);
    vec3 base = texColor.rgb;
    float grey = base.r;
    int idx = int(15.0*grey);
    if (idx >= 0 && idx < 32)
        texColor.rgb = texture2D(tex_hue, vec2(grey, 0.0)).rgb;
    else
        texColor.rgb = vec3(1.0, 0.0, 1.0);
    gl_FragColor = texColor;
}

