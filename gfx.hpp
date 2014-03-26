#ifndef _GFX_HPP
#define _GFX_HPP

struct pixel_storage_t
{
    int width;
    int height;
    unsigned int tex;
    float tcxs[4];
    float tcys[4];
};

pixel_storage_t gfx_upload_tex2d(int width, int height, void *data);

int gfx_upload_program(const char *vert_filename, const char *frag_filename);

void gfx_render(pixel_storage_t *ps, int xs[4], int ys[4], int draw_prio, int hue_id, int pick_id);


#endif

