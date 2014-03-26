uniform sampler2D tex;
uniform sampler2D tex_hue;
uniform vec3 tex_coords_hue;

varying vec2 texCoord;

void main()
{
    vec4 texColor = texture2D(tex, texCoord);
    vec3 base = texColor.rgb;
    float grey = base.r;
    int idx = int(15.0*grey);
    if (idx >= 0 && idx < 32)
    {
        vec2 lookup_start = tex_coords_hue.xz;
        vec2 lookup_end   = tex_coords_hue.yz;
        vec2 lookup = (lookup_start, lookup_end, grey);

        texColor.rgb = texture2D(tex_hue, lookup).rgb;
    }
    else
    {
        texColor.rgb = vec3(1.0, 0.0, 1.0);
    }
    gl_FragColor = texColor;
}

