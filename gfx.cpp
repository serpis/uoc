#include <cassert>

#include <list>

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

pixel_storage_t get_hue_tex(int hue_id);


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

static int log2(int n)
{
    assert(is_2pot(n));
    int highest_bit = 0;
    while (n)
    {
        highest_bit += 1;
        n >>= 1;
    }
    return highest_bit;
}

static int round_up_to_2pot(int n)
{
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


struct atlas_slot_t
{
    bool valid;
};

struct atlas_t
{
    unsigned int tex;
    int width, height;
    int slot_width, slot_height;
    int slots_x, slots_y;
    int slot_count;

    atlas_slot_t slots[];
};

atlas_t *create_empty_atlas(int width, int height, int slot_width, int slot_height)
{
    // make sure proper dimensions are specified
    assert((width % slot_width) == 0);
    assert((height % slot_height) == 0);

    unsigned int tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, width, 0, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    check_gl_error(__LINE__);

    int slots_x = width / slot_width;
    int slots_y = height / slot_height;

    int slot_count = slots_x * slots_y;

    atlas_t *atlas = (atlas_t *)malloc(sizeof(atlas_t) + slot_count * sizeof(atlas_slot_t));

    atlas->tex = tex;
    atlas->width = width;
    atlas->height = height;
    atlas->slot_width = slot_width;
    atlas->slot_height = slot_height;
    atlas->slots_x = slots_x;
    atlas->slots_y = slots_y;
    atlas->slot_count = slot_count;

    for (int i = 0; i < slot_count; i++)
    {
        atlas->slots[i].valid = false;
    }

    return atlas;
}

std::list<atlas_t *> atlases;

void dump_tga(const char *filename, int width, int height, void *argb1555_data);

void find_atlas_slot(int req_width, int req_height, atlas_t **atlas, int *slot_x, int *slot_y)
{
    int slot_width  = round_up_to_2pot(req_width);
    int slot_height = round_up_to_2pot(req_height);

    // make tiny things use a 32x32 slot, hopefully this improves atlas utilization
    if (slot_width < 32)
    {
        slot_width = 32;
    }
    if (slot_height < 32 && slot_height != 1)
    {
        slot_height = 32;
    }

    //printf("slot_width, slot_height: %d %d\n", slot_width, slot_height);

    // first look for an empty slot in existing atlases
    for (std::list<atlas_t *>::iterator it = atlases.begin(); it != atlases.end(); ++it)
    {
        atlas_t *a = *it;

        if (a->slot_width == slot_width && a->slot_height == slot_height)
        {
            int slot_count = a->slot_count;

            for (int i = 0; i < slot_count; i++)
            {
                if (!a->slots[i].valid)
                {
                    // found one!
                    *atlas = a;
                    *slot_x = i % a->slots_x;
                    *slot_y = i / a->slots_x;
                    a->slots[i].valid = true;
                    //printf("found a slot in an atlas: %d %d\n", *slot_x, *slot_y);
                    return;
                }
            }
        }
    }

    // no free slot found, create a new atlas!
    {
        int width = 1024;
        int height = 1024;
        assert(slot_width < width);
        assert(slot_height < height);
        atlas_t *a = create_empty_atlas(width, height, slot_width, slot_height);
        atlases.push_back(a);

        *atlas = a;
        *slot_x = 0;
        *slot_y = 0;
        a->slots[0].valid = true;
        //printf("no slot found, created an atlas for slot size %d %d, w, h: %d %d, slots: %d\n", slot_width, slot_height, a->width, a->height, a->slot_count);
    }
}

pixel_storage_t gfx_upload_tex2d(int width, int height, void *data)
{
    atlas_t *atlas;
    int slot_x, slot_y;

    find_atlas_slot(width, height, &atlas, &slot_x, &slot_y);

    check_gl_error(__LINE__);

    //unsigned int tex;

    /*glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture_width, texture_height, 0, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV, 0);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV, data);
    glBindTexture(GL_TEXTURE_2D, 0);*/

    check_gl_error(__LINE__);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glBindTexture(GL_TEXTURE_2D, atlas->tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, slot_x * atlas->slot_width, slot_y * atlas->slot_height, width, height, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV, data);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (check_gl_error(__LINE__))
    {
        printf("error uploading 2d texture w, h: %d, %d\n", width, height);
    }

    pixel_storage_t ps;
    ps.width = width;
    ps.height = height;
    ps.tex = atlas->tex;
    // the small tex offsets are necessary to prevent sampling outside of this atlas slot
    ps.tcxs[0] = (slot_x * atlas->slot_width  +      0 + 0.1f) / (float)atlas->width ;
    ps.tcxs[1] = (slot_x * atlas->slot_width  +  width - 0.1f) / (float)atlas->width ;
    ps.tcxs[2] = (slot_x * atlas->slot_width  +  width - 0.1f) / (float)atlas->width ;
    ps.tcxs[3] = (slot_x * atlas->slot_width  +      0 + 0.1f) / (float)atlas->width ;
    ps.tcys[0] = (slot_y * atlas->slot_height +      0 + 0.1f) / (float)atlas->height;
    ps.tcys[1] = (slot_y * atlas->slot_height +      0 + 0.1f) / (float)atlas->height;
    ps.tcys[2] = (slot_y * atlas->slot_height + height - 0.1f) / (float)atlas->height;
    ps.tcys[3] = (slot_y * atlas->slot_height + height - 0.1f) / (float)atlas->height;
    /*ps.tcxs[0] = 0.0f;
    ps.tcxs[1] = width / (float)texture_width;
    ps.tcxs[2] = width / (float)texture_width;
    ps.tcxs[3] = 0.0f;
    ps.tcys[0] = 0.0f;
    ps.tcys[1] = 0.0f;
    ps.tcys[2] = height / (float)texture_height;
    ps.tcys[3] = height / (float)texture_height;*/

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

struct render_command_t
{
    int xs[4];
    int ys[4];
    float tcxs[4];
    float tcys[4];
    int draw_prio;

    unsigned int tex0;
    unsigned int tex1;

    int program;
    int uniform1i0_loc;
    int uniform1i0;
    int uniform1i1_loc;
    int uniform1i1;
    int uniform3f0_loc;
    float uniform3f0[3];
};

int bound_program = 0;
int bound_tex0 = 0;
int bound_tex1 = 0;
bool did_begin = false;

static void render_command(render_command_t *cmd)
{
    if (cmd->program != bound_program)
    {
        if (did_begin)
        {
            glEnd();
            did_begin = false;
        }
        glUseProgram(cmd->program);
        bound_program = cmd->program;
    }

    if (cmd->program != 0)
    {
        if (did_begin)
        {
            glEnd();
            did_begin = false;
        }
        if (cmd->uniform1i0_loc != -1)
        {
            glUniform1i(cmd->uniform1i0_loc, cmd->uniform1i0);
        }
        if (cmd->uniform1i1_loc != -1)
        {
            glUniform1i(cmd->uniform1i1_loc, cmd->uniform1i1);
        }
        if (cmd->uniform3f0_loc != -1)
        {
            glUniform3fv(cmd->uniform3f0_loc, 1, cmd->uniform3f0);
        }
    }
    if (cmd->tex0 != bound_tex0)
    {
        if (did_begin)
        {
            glEnd();
            did_begin = false;
        }
        glBindTexture(GL_TEXTURE_2D, cmd->tex0);
        bound_tex0 = cmd->tex0;
    }
    if (cmd->tex1 != bound_tex1)
    {
        if (did_begin)
        {
            glEnd();
            did_begin = false;
        }
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, cmd->tex1);
        glActiveTexture(GL_TEXTURE0);
        bound_tex1 = cmd->tex1;
    }

    //check_gl_error(__LINE__);


    //check_gl_error(__LINE__);

    if (!did_begin)
    {
        glBegin(GL_QUADS);
        did_begin = true;
    }
    for (int i = 0; i < 4; i++)
    {
        glTexCoord2f(cmd->tcxs[i], cmd->tcys[i]);
        glVertex3f(cmd->xs[i], cmd->ys[i], cmd->draw_prio / 5000000.0f);
    }
    //glEnd();

    //check_gl_error(__LINE__);

    //check_gl_error(__LINE__);
    /*if (cmd->program != 0)
    {
        glUseProgram(0);
        check_gl_error(__LINE__);
    }*/
    //check_gl_error(__LINE__);

    /*
    bool use_picking = cmd->pick_id != -1;
    bool use_hue = cmd->tex_hue != 0;
    unsigned int tex_hue;

    if (use_picking)
    {
        int pick_id0 = (cmd->pick_id >> 16) & 0xff;
        int pick_id1 = (cmd->pick_id >>  8) & 0xff;
        int pick_id2 = (cmd->pick_id >>  0) & 0xff;
        float pick_id_vec[3] = { pick_id0 / 255.0f, pick_id1 / 255.0f, pick_id2 / 255.0f };
        glUseProgram(prg_blit_picking);
        glUniform1i(glGetUniformLocation(prg_blit_picking, "tex"), 0);
        glUniform3fv(glGetUniformLocation(prg_blit_picking, "pick_id"), 1, pick_id_vec);
    }
    else if (use_hue)
    {
        glUseProgram(prg_blit_hue);
        glUniform1i(glGetUniformLocation(prg_blit_hue, "tex"), 0);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, cmd->tex_hue);
        glActiveTexture(GL_TEXTURE0);

        bool only_grey = cmd->only_grey;
        glUniform1i(glGetUniformLocation(prg_blit_hue, "tex_hue"), 1);
        glUniform3f(glGetUniformLocation(prg_blit_hue, "tex_coords_hue"), cmd->hue_tcxs[0], cmd->hue_tcxs[1], cmd->hue_tcy);//0.0, 1.0, 0.0);
    }
    check_gl_error(__LINE__);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, window_width, window_height, 0, -1, 1);

    glBindTexture(GL_TEXTURE_2D, cmd->tex);

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
        glTexCoord2f(cmd->tcxs[i], cmd->tcys[i]);
        glVertex3f(cmd->xs[i], cmd->ys[i], cmd->draw_prio / 5000000.0f);
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
    */
}

std::list<render_command_t> cmds;

void gfx_flush()
{
    // prepare a good state
    {
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0, window_width, window_height, 0, -1, 1);

        //check_gl_error(__LINE__);

        glEnable(GL_TEXTURE_2D);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glAlphaFunc(GL_GREATER, 0.0f);
        glDepthFunc(GL_LEQUAL);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_ALPHA_TEST);
    }

    // execute all render commands
    for (std::list<render_command_t>::iterator it = cmds.begin(); it != cmds.end(); ++it)
    {
        render_command_t *cmd = &(*it);
        render_command(cmd);
    }
    cmds.clear();

    if (did_begin)
    {
        glEnd();
        did_begin = false;
    }

    // clean up state
    {
        glDisable(GL_ALPHA_TEST);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_TEXTURE_2D);

        //glBindTexture(GL_TEXTURE_2D, 0);
        //check_gl_error(__LINE__);

        glPopMatrix();
    }

    if (bound_program != 0)
    {
        glUseProgram(0);
        bound_program = 0;
    }
    if (bound_tex0 != 0)
    {
        glBindTexture(GL_TEXTURE_2D, 0);
        bound_tex0 = 0;
    }
    if (bound_tex1 != 0)
    {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
        bound_tex1 = 0;
    }
}

void gfx_render(pixel_storage_t *ps, int xs[4], int ys[4], int draw_prio, int hue_id, int pick_id)
{
    render_command_t cmd;
    //cmd.tex = ps->tex;
    memcpy(cmd.xs, xs, sizeof(cmd.xs));
    memcpy(cmd.ys, ys, sizeof(cmd.ys));
    memcpy(cmd.tcxs, ps->tcxs, sizeof(cmd.tcxs));
    memcpy(cmd.tcys, ps->tcys, sizeof(cmd.tcys));
    cmd.draw_prio = draw_prio;
    //cmd.pick_id = pick_id;

    bool use_picking = pick_id != -1;
    bool use_hue = hue_id != 0;

    if (use_picking)
    {
        int pick_id0 = (pick_id >> 16) & 0xff;
        int pick_id1 = (pick_id >>  8) & 0xff;
        int pick_id2 = (pick_id >>  0) & 0xff;
        float pick_id_vec[3] = { pick_id0 / 255.0f, pick_id1 / 255.0f, pick_id2 / 255.0f };

        cmd.program = prg_blit_picking;

        cmd.tex0 = ps->tex;
        cmd.tex1 = 0;

        cmd.uniform1i0_loc = glGetUniformLocation(prg_blit_picking, "tex");
        cmd.uniform1i0 = 0;

        cmd.uniform1i1_loc = -1;

        cmd.uniform3f0_loc = glGetUniformLocation(prg_blit_picking, "pick_id");
        memcpy(cmd.uniform3f0, pick_id_vec, sizeof(cmd.uniform3f0));
    }
    else if (use_hue)
    {
        pixel_storage_t ps_hue = get_hue_tex(hue_id & 0x7fff);

        cmd.program = prg_blit_hue;

        cmd.tex0 = ps->tex;
        cmd.tex1 = ps_hue.tex;

        cmd.uniform1i0_loc = glGetUniformLocation(prg_blit_hue, "tex");
        cmd.uniform1i0 = 0;

        cmd.uniform1i1_loc = glGetUniformLocation(prg_blit_hue, "tex_hue");
        cmd.uniform1i1 = 1;

        cmd.uniform3f0_loc = glGetUniformLocation(prg_blit_hue, "tex_coords_hue");
        cmd.uniform3f0[0] = ps_hue.tcxs[0];
        cmd.uniform3f0[1] = ps_hue.tcxs[1];
        cmd.uniform3f0[2] = ps_hue.tcys[0];

        /*cmd.tex_hue = ps_hue.tex;
        cmd.hue_tcxs[0] = ps_hue.tcxs[0];
        cmd.hue_tcxs[1] = ps_hue.tcxs[1];
        cmd.hue_tcy = ps_hue.tcys[0];
        cmd.only_grey = (hue_id & 0x8000) == 0;*/
    }
    else
    {
        cmd.program = 0;

        cmd.tex0 = ps->tex;
        cmd.tex1 = 0;

        cmd.uniform1i0_loc = -1;
        cmd.uniform1i1_loc = -1;
        cmd.uniform3f0_loc = -1;

        /*cmd.tex_hue = 0;
        memset(cmd.hue_tcxs, 0, sizeof(cmd.hue_tcxs));
        cmd.hue_tcy = 0.0f;
        cmd.only_grey = false;*/
    }

    cmds.push_back(cmd);

    //render_command(&cmd);
/*
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

        pixel_storage_t ps_hue = get_hue_tex(hue_id & 0x7fff);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, ps_hue.tex);
        glActiveTexture(GL_TEXTURE0);

        bool only_grey = (hue_id & 0x8000) == 0;
        float tex_coords_hue[3] = { ps_hue.tcxs[0], ps_hue.tcxs[1], ps_hue.tcys[0] };
        glUniform1i(glGetUniformLocation(prg_blit_hue, "tex_hue"), 1);
        glUniform3f(glGetUniformLocation(prg_blit_hue, "tex_coords_hue"), ps_hue.tcxs[0], ps_hue.tcxs[1], ps_hue.tcys[0]);//0.0, 1.0, 0.0);
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
    check_gl_error(__LINE__);*/
}


