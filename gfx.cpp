#include <cassert>

#include "file.hpp"
#include "gfx.hpp"

#ifdef _LINUX

 #define GL3_PROTOTYPES 1
 #define GL_GLEXT_PROTOTYPES
 #include <GL/gl.h>
 #include <GL/glext.h>
 #include <GL/glu.h>
 
 #include <SDL2/SDL.h>

#else

 #define GL3_PROTOTYPES 1
 #include <OpenGL/gl.h>
 #include <OpenGL/glu.h>
 
 #include <SDL2/SDL.h>

#endif

extern int prg_blit_picking;
extern int prg_blit_hue;

extern const int window_width;
extern const int window_height;

extern unsigned int get_hue_tex(int hue_id);


static bool check_gl_error(int line = -1)
{
    GLenum err;
    bool found_error = false;
    while ((err = glGetError()) != GL_NO_ERROR)
    {
        printf("GL Error: %d %s\n", err, gluErrorString(err));
		if (line != -1)
			printf(" + line: %i\n", line);
        found_error = true;
    }
    return found_error;
}

static bool is_2pot(int n)
{
    return n && ((n & (n - 1)) == 0);
}

static int round_up_to_2pot(int n)
{
    assert(n > 0);
    if (is_2pot(n)) 
    {
        return n;
    }
    else
    {
        int highest_bit = 0;
        while (n)
        {
            highest_bit += 1;
            n >>= 1;
        }
        return 1 << highest_bit;
    }
}



unsigned int gfx_upload_tex1d(int width, void *data)
{
    unsigned int tex;

    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_1D, tex);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA, width, 0, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV, data);
    glBindTexture(GL_TEXTURE_1D, 0);

    if (check_gl_error(__LINE__))
    {
        printf("error uploading 1d texture w: %d\n", width);
    }
    
    return tex;
}

pixel_storage_t gfx_upload_tex2d(int width, int height, void *data)
{
    int texture_width;
    int texture_height;

    if (0)
    {
        int width_2pot = round_up_to_2pot(width);
        int height_2pot = round_up_to_2pot(height);
        printf("uploading (%d, %d) -> %dx%d atlas.\n", width, height, width_2pot, height_2pot);
        texture_width = width_2pot;
        texture_height = height_2pot;
    }
    else
    {
        texture_width = width;
        texture_height = height;
    }

    check_gl_error(__LINE__);

    unsigned int tex;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);

    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture_width, texture_height, 0, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV, 0);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV, data);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (check_gl_error(__LINE__))
    {
        printf("error uploading 2d texture w, h: %d, %d\n", texture_width, texture_height);
    }

    pixel_storage_t ps;
    ps.width = width;
    ps.height = height;
    ps.tex = tex;
    ps.tcxs[0] = 0.0f;
    ps.tcxs[1] = width / (float)texture_width;
    ps.tcxs[2] = width / (float)texture_width;
    ps.tcxs[3] = 0.0f;
    ps.tcys[0] = 0.0f;
    ps.tcys[1] = 0.0f;
    ps.tcys[2] = height / (float)texture_height;
    ps.tcys[3] = height / (float)texture_height;
    return ps;
}

static int compile_shader(const char *name, int type, const char *src, const char *end)
{
    int shader = glCreateShader(type);
    int length = end - src;
    glShaderSource(shader, 1, &src, &length);
    check_gl_error(__LINE__);
    glCompileShader(shader);

    int compiled_ok;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled_ok);
    if (!compiled_ok)
    {
        int log_length;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH , &log_length);
        if (log_length > 0)
        {
            char *log = (char *)malloc(log_length);
            int hmz;
            glGetShaderInfoLog(shader, log_length, &hmz, log);
            printf("%d %d\n", hmz, log_length);
            printf("Error compiling '%s':\n%s", name, log);
            // TODO: more graceful?
            exit(-1);
            free(log);
        }

        return -1;
    }
    return shader;
}

static int link_program(const char *name, int shader0, int shader1)
{
    int program = glCreateProgram();
    glAttachShader(program, shader0);
    glAttachShader(program, shader1);
    glLinkProgram(program);

    int linked_ok;
    glGetProgramiv(program, GL_LINK_STATUS, &linked_ok);
    if (!linked_ok)
    {
        int log_length;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH , &log_length);
        if (log_length > 0)
        {
            char *log = (char *)malloc(log_length+1);
            int hmz;
            glGetProgramInfoLog(program, log_length, &hmz, log);
            assert(hmz == log_length);
            printf("Error linking '%s':\n%s\n", name, log);
            // TODO: more graceful?
            exit(-1);
            free(log);
        }

        return -1;
    }
    return program;
}

int gfx_upload_program(const char *vert_filename, const char *frag_filename)
{
    const char *vert_src_end;
    const char *vert_src = file_map(vert_filename, &vert_src_end);
    const char *frag_src_end;
    const char *frag_src = file_map(frag_filename, &frag_src_end);

    int s0 = compile_shader(vert_filename, GL_VERTEX_SHADER, vert_src, vert_src_end);
    int s1 = compile_shader(frag_filename, GL_FRAGMENT_SHADER, frag_src, frag_src_end);

    file_unmap(vert_src, vert_src_end);
    file_unmap(frag_src, frag_src_end);

    char program_name[1024];
    sprintf(program_name, "program <%s> <%s>", vert_filename, frag_filename);

    return link_program(program_name, s0, s1);
}

void gfx_render(pixel_storage_t *ps, int xs[4], int ys[4], int draw_prio, int hue_id, int pick_id)
{
    bool use_picking = pick_id != -1;
    bool use_hue = hue_id != 0;
    unsigned int tex_hue;

    if (use_picking)
    {
        int pick_id0 = (pick_id >> 16) & 0xff;
        int pick_id1 = (pick_id >>  8) & 0xff;
        int pick_id2 = (pick_id >>  0) & 0xff;
        float pick_id_vec[3] = { pick_id0 / 255.0f, pick_id1 / 255.0f, pick_id2 / 255.0f };
        glUseProgram(prg_blit_picking);
        glUniform1i(glGetUniformLocation(prg_blit_picking, "tex"), 0);
        glUniform3fv(glGetUniformLocation(prg_blit_picking, "pick_id"), 1, pick_id_vec);
    }
    else if (use_hue)
    {
        glUseProgram(prg_blit_hue);
        glUniform1i(glGetUniformLocation(prg_blit_hue, "tex"), 0);

        unsigned int tex_hue = get_hue_tex(hue_id & 0x7fff);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_1D, tex_hue);
        glActiveTexture(GL_TEXTURE0);

        bool only_grey = (hue_id & 0x8000) == 0;
        glUniform1i(glGetUniformLocation(prg_blit_hue, "tex_hue"), 1);
    }
    check_gl_error(__LINE__);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, window_width, window_height, 0, -1, 1);

    glBindTexture(GL_TEXTURE_2D, ps->tex);

    glEnable(GL_TEXTURE_2D);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glAlphaFunc(GL_GREATER, 0.0f);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_ALPHA_TEST);

    check_gl_error(__LINE__);


    glBegin(GL_QUADS);
    for (int i = 0; i < 4; i++)
    {
        glTexCoord2f(ps->tcxs[i], ps->tcys[i]);
        glVertex3f(xs[i], ys[i], draw_prio / 5000000.0f);
    }
    glEnd();

    check_gl_error(__LINE__);

    glDisable(GL_ALPHA_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);

    glBindTexture(GL_TEXTURE_2D, 0);
    check_gl_error(__LINE__);

    glPopMatrix();

    check_gl_error(__LINE__);
    if (use_picking)
    {
        glUseProgram(0);
        check_gl_error(__LINE__);
    }
    else if (use_hue)
    {
        glUseProgram(0);
        check_gl_error(__LINE__);
    }
    check_gl_error(__LINE__);
}


