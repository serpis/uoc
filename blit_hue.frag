uniform sampler2D tex;
uniform sampler2D tex_hue;
//uniform vec3 tex_coords_hue;

varying vec2 texCoord;
varying vec3 normal;

void main()
{
    vec4 texColor = texture2D(tex, texCoord);
    vec3 base = texColor.rgb;
    float grey = base.r;
    int idx = int(15.0*grey);
    if (idx >= 0 && idx < 32)
    {
        float lookup_x = mix(normal.x, normal.y, grey);
        float lookup_y = normal.z;

        texColor.rgb = texture2D(tex_hue, vec2(lookup_x, lookup_y)).rgb;
        //texColor.rgb = normal.xyz;
    }
    else
    {
        texColor.rgb = vec3(1.0, 0.0, 1.0);
    }
    gl_FragColor = texColor;
}

