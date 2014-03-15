#include <cassert>
#include <cstdio>
#include <cstdlib>

#include <stdint.h>

#include <map>
#include <list>

#include "net.hpp"
#include "mullib.hpp"
#include "game.hpp"

#ifdef _LINUX

 #define GL3_PROTOTYPES 1
 #include <GL/gl.h>
 
 #include <SDL2/SDL.h>

#else

 #define GL3_PROTOTYPES 1
 #include <OpenGL/gl.h>
 
 #include <SDL2/SDL.h>

#endif

long now;

std::map<int, item_t *> items;
std::map<int, mobile_t *> mobiles;

int draw_ceiling = 128;

/* A simple function that prints a message, the error code returned by SDL,
 * and quits the application */
void sdl_die(const char *msg)
{
    printf("%s: %s\n", msg, SDL_GetError());
    SDL_Quit();
    exit(1);
}
 
void checkSDLError(int line = -1)
{
#ifndef NDEBUG
	const char *error = SDL_GetError();
	if (*error != '\0')
	{
		printf("SDL Error: %s\n", error);
		if (line != -1)
			printf(" + line: %i\n", line);
		SDL_ClearError();
	}
#endif
}

void dump_tga(const char *filename, int width, int height, void *argb1555_data)
{
    uint16_t *data = (uint16_t *)argb1555_data;
    int size = width * height;
    char header[] =
    {
        0,
        0,
        2,
        0, 0,
        0, 0,
        0,
        0, 0,
        0, 0,
        width & 0xff,
        width >> 8,
        height & 0xff,
        height >> 8,
        24,
        0x20
    };
    FILE *f = fopen(filename, "wb");
    fwrite(header, 18, 1, f);
    for (int i = 0; i < size; i++)
    {
        uint16_t pixel = data[i];
        uint8_t a = (pixel >> 8) & 0x80;
        uint8_t r = (pixel >> 7) & 0xf8;
        uint8_t g = (pixel >> 2) & 0xf8;
        uint8_t b = (pixel << 3) & 0xf8;
        char buf[4] = { r, g, b, a };
        fwrite(buf, 3, 1, f);
    }
    fclose(f);
}

struct pixel_storage_i
{
    int width;
    int height;
    unsigned int tex;
    float tcxs[4];
    float tcys[4];
};

bool is_2pot(int n)
{
    return (n & (n - 1)) == 0;
}

int round_up_to_2pot(int n)
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

pixel_storage_i upload(int width, int height, void *data)
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

    pixel_storage_i ps;
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

struct anim_frame_t
{
    int center_x, center_y;
    pixel_storage_i ps;
};

static struct
{
    struct
    {
        bool valid;
        bool fetching;
        int fetching_entries;
        struct
        {
            int frame_count;
            anim_frame_t *frames;
        } directions[5];
    } entries[1100*35]; // 1100 bodies, each with 35 actions
} anim_cache;

static struct
{
    struct
    {
        bool valid;
        bool fetching;
        pixel_storage_i ps;
    } entries[0x4000];
} land_cache;

static struct
{
    struct
    {
        bool valid;
        bool fetching;
        pixel_storage_i ps;
    } entries[0x10000];
} static_cache;

struct land_block_t
{
    struct {
        int tile_id;
        int z;
    } tiles[8*8];
};

static struct
{
    struct
    {
        bool valid;
        bool fetching;
        int x, y;
        land_block_t land_block;
    } entries[8 * 8];
} land_block_cache;

struct statics_block_entry_t
{
    int tile_id;
    int dx, dy, z;
};

struct statics_block_t
{
    int statics_count;
    statics_block_entry_t *statics;
};

static struct
{
    struct
    {
        bool valid;
        bool fetching;
        int x, y;
        statics_block_t statics_block;
    } entries[8 * 8];
} statics_block_cache;

void write_anim_frames(int body_id, int action, int direction, ml_anim *anim)
{
    int body_action_id = body_id * 35 + action;

    anim_frame_t *frames = (anim_frame_t *)malloc(anim->frame_count * sizeof(anim_frame_t));

    for (int j = 0; j < anim->frame_count; j++)
    {
        frames[j].center_x = anim->frames[j].center_x;
        frames[j].center_y = anim->frames[j].center_y;
        frames[j].ps = upload(anim->frames[j].width, anim->frames[j].height, anim->frames[j].data);
    }

    anim_cache.entries[body_action_id].directions[direction].frame_count = anim->frame_count;
    anim_cache.entries[body_action_id].directions[direction].frames = frames;

    free(anim);

    anim_cache.entries[body_action_id].fetching_entries -= 1;
    assert(anim_cache.entries[body_action_id].fetching_entries >= 0);
    if (anim_cache.entries[body_action_id].fetching_entries == 0)
    {
        anim_cache.entries[body_action_id].fetching = false;
    }
}

anim_frame_t *get_anim_frames(int body_id, int action, int direction, int *frame_count)
{
    assert(body_id   >= 0 && body_id   < 1100);
    assert(action    >= 0 && action    <   35);
    assert(direction >= 0 && direction <    5);

    int body_action_id = body_id * 35 + action;

    if (!anim_cache.entries[body_action_id].valid)
    {
        anim_cache.entries[body_action_id].valid = true;
        anim_cache.entries[body_action_id].fetching = true;

        anim_cache.entries[body_action_id].fetching_entries = 5;

        for (int i = 0; i < 5; i++)
        {
            //printf("anim_cache         : loading %d %d %d\n", body_id, action, i);
            mlt_read_anim(body_id, action, i, write_anim_frames);
            //write_anim_frames(body_id, action, i);
        }
    }

    if (anim_cache.entries[body_action_id].fetching)
    {
        // TODO: return dummy item
        return NULL;
    }
    else
    {
        *frame_count = anim_cache.entries[body_action_id].directions[direction].frame_count;
        return anim_cache.entries[body_action_id].directions[direction].frames;
    }
}

void write_land_ps(int land_id, ml_art *l)
{
    land_cache.entries[land_id].ps = upload(l->width, l->height, l->data);
    free(l);

    land_cache.entries[land_id].fetching = false;
}

pixel_storage_i *get_land_ps(int land_id)
{
    assert(land_id >= 0 && land_id < 0x4000);
    if (!land_cache.entries[land_id].valid)
    {
        land_cache.entries[land_id].valid = true;
        land_cache.entries[land_id].fetching = true;

        //printf("land_cache         : loading %d\n", land_id);

        //write_land_ps(land_id);
        mlt_read_land_art(land_id, write_land_ps);
    }

    if (land_cache.entries[land_id].fetching)
    {
        return NULL;
    }
    else
    {
        return &land_cache.entries[land_id].ps;
    }
}

void write_static_ps(int item_id, ml_art *s)
{
    static_cache.entries[item_id].ps = upload(s->width, s->height, s->data);
    free(s);

    static_cache.entries[item_id].fetching = false;
}

pixel_storage_i *get_static_ps(int item_id)
{
    assert(item_id >= 0 && item_id < 0x10000);
    if (!static_cache.entries[item_id].valid)
    {
        static_cache.entries[item_id].valid = true;
        static_cache.entries[item_id].fetching = true;

        //printf("static_cache       : loading %d\n", item_id);

        //async_get_static_ps(item_id);
        mlt_read_static_art(item_id, write_static_ps);
    }

    if (static_cache.entries[item_id].fetching)
    {
        return NULL;
    }
    else
    {
        return &static_cache.entries[item_id].ps;
    }
}

void write_land_block(int map, int block_x, int block_y, ml_land_block *lb)
{
    int cache_block_x = block_x % 8;
    int cache_block_y = block_y % 8;

    int cache_block_index = cache_block_x + cache_block_y * 8;

    for (int i = 0; i < 8 * 8; i++)
    {
        land_block_cache.entries[cache_block_index].land_block.tiles[i].tile_id = lb->tiles[i].tile_id;
        land_block_cache.entries[cache_block_index].land_block.tiles[i].z = lb->tiles[i].z;
    }
    free(lb);

    land_block_cache.entries[cache_block_index].fetching = false;
}

land_block_t *get_land_block(int map, int block_x, int block_y)
{
    // TODO: remove these assumptions
    assert(map == 0 || map == 1);
    assert(block_x >= 0 && block_x < 896);
    assert(block_y >= 0 && block_y < 512);

    int cache_block_x = block_x % 8;
    int cache_block_y = block_y % 8;

    // TODO: also care about map in the cache lookup

    int cache_block_index = cache_block_x + cache_block_y * 8;

    // TODO: add thrashing check
    if (!land_block_cache.entries[cache_block_index].valid ||
         land_block_cache.entries[cache_block_index].x != block_x ||
         land_block_cache.entries[cache_block_index].y != block_y)
    {
        land_block_cache.entries[cache_block_index].valid = true;
        land_block_cache.entries[cache_block_index].fetching = true;
        land_block_cache.entries[cache_block_index].x = block_x;
        land_block_cache.entries[cache_block_index].y = block_y;

        //printf("land_block_cache   : loading %d %d %d\n", map, block_x, block_y);

        mlt_read_land_block(map, block_x, block_y, write_land_block);
    }

    if (land_block_cache.entries[cache_block_index].fetching)
    {
        return NULL;
    }
    else
    {
        return &land_block_cache.entries[cache_block_index].land_block;
    }
}

void write_statics_block(int map, int block_x, int block_y, ml_statics_block *sb)
{
    int cache_block_x = block_x % 8;
    int cache_block_y = block_y % 8;

    // TODO: also care about map in the cache lookup

    int cache_block_index = cache_block_x + cache_block_y * 8;

    statics_block_cache.entries[cache_block_index].statics_block.statics_count = sb->statics_count;
    statics_block_cache.entries[cache_block_index].statics_block.statics = (statics_block_entry_t *)malloc(sb->statics_count * sizeof(statics_block_entry_t));

    for (int i = 0; i < sb->statics_count; i++)
    {
        statics_block_cache.entries[cache_block_index].statics_block.statics[i].tile_id = sb->statics[i].tile_id;
        statics_block_cache.entries[cache_block_index].statics_block.statics[i].dx = sb->statics[i].dx;
        statics_block_cache.entries[cache_block_index].statics_block.statics[i].dy = sb->statics[i].dy;
        statics_block_cache.entries[cache_block_index].statics_block.statics[i].z = sb->statics[i].z;
    }
    free(sb);

    statics_block_cache.entries[cache_block_index].fetching = false;
}

statics_block_t *get_statics_block(int map, int block_x, int block_y)
{
    // TODO: remove these assumptions
    assert(map == 0 || map == 1);
    assert(block_x >= 0 && block_x < 896);
    assert(block_y >= 0 && block_y < 512);

    int cache_block_x = block_x % 8;
    int cache_block_y = block_y % 8;

    int cache_block_index = cache_block_x + cache_block_y * 8;

    // TODO: add thrashing check
    if (!statics_block_cache.entries[cache_block_index].valid ||
         statics_block_cache.entries[cache_block_index].x != block_x ||
         statics_block_cache.entries[cache_block_index].y != block_y)
    {
        // if we're throwing an old block out, free its resources!
        if (statics_block_cache.entries[cache_block_index].valid)
        {
            free(statics_block_cache.entries[cache_block_index].statics_block.statics);
        }

        statics_block_cache.entries[cache_block_index].valid = true;
        statics_block_cache.entries[cache_block_index].fetching = true;
        statics_block_cache.entries[cache_block_index].x = block_x;
        statics_block_cache.entries[cache_block_index].y = block_y;

        //printf("statics_block_cache: loading %d %d %d\n", map, block_x, block_y);

        mlt_read_statics_block(map, block_x, block_y, write_statics_block);
    }

    if (statics_block_cache.entries[cache_block_index].fetching)
    {
        return NULL;
    }
    else
    {
        return &statics_block_cache.entries[cache_block_index].statics_block;
    }
}


void render(pixel_storage_i *ps, int xs[4], int ys[4], int draw_prio)
{
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glOrtho(0, 512, 512, 0, -1, 1);

    glBindTexture(GL_TEXTURE_2D, ps->tex);

    glEnable(GL_TEXTURE_2D);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glAlphaFunc(GL_GREATER, 0.0f);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_ALPHA_TEST);
    glBegin(GL_QUADS);
    for (int i = 0; i < 4; i++)
    {
        glTexCoord2f(ps->tcxs[i], ps->tcys[i]);
        glVertex3f(xs[i], ys[i], draw_prio / 5000000.0f);
    }
    glEnd();
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);

    glBindTexture(GL_TEXTURE_2D, 0);

    glPopMatrix();
}

void blit_ps(pixel_storage_i *ps, int x, int y, int draw_prio)
{
    int xs[4] = { x, x + ps->width, x + ps->width, x };
    int ys[4] = { y, y, y + ps->height, y + ps->height };

    render(ps, xs, ys, draw_prio);
}

void blit_ps_flipped(pixel_storage_i *ps, int x, int y, int draw_prio)
{
    int xs[4] = { x + ps->width, x, x, x + ps->width };
    int ys[4] = { y, y, y + ps->height, y + ps->height };

    render(ps, xs, ys, draw_prio);
}

void blit(int x, int y, int draw_prio, int width, int height, void *data)
{
    pixel_storage_i ps = upload(width, height, data);

    blit_ps(&ps, x, y, draw_prio);

    glDeleteTextures(1, &ps.tex);
}

mobile_t player =
{
    12345, // id
    401, // body_id
    1212, 1537, 0, // xyz
    0, // dir
    0, // hue
    0, // noto
    {0} // equipped items
};

// Lord British' throne
//int player.x = 1323;
//int player.y = 1624;
// secret practice place
//int player.x = 1212;
//int player.y = 1537;
//int player.z = 0;

int screen_center_x = 256;
int screen_center_y = 256;

void world_to_screen(int world_x, int world_y, int world_z, int *screen_x, int *screen_y)
{
    //int player.x = 3;
    //int player.y = 3;
    //int player.z = 0;

    int world_dx = world_x - player.x;
    int world_dy = world_y - player.y;
    int world_dz = world_z - player.z;

    int screen_dx = world_dx * 22 - world_dy * 22;
    int screen_dy = world_dx * 22 + world_dy * 22 - world_dz * 4;

    *screen_x = screen_center_x + screen_dx;
    *screen_y = screen_center_y + screen_dy;
}

int world_draw_prio(int world_x, int world_y, int world_z)
{
    int prio = 256 * world_x + 256 * world_y + (world_z + 128);
    return prio;
}

/*void draw_world_land(ml_art *art, int x, int y, int z)
{
    int dxs[4] = { 0, 1, 1, 0 };
    int dys[4] = { 0, 0, 1, 1 };

    int xs[4];
    int ys[4];

    for (int i = 0; i < 4; i++)
    {
        int screen_x;
        int screen_y;
        world_to_screen(x + dxs[i], y + dys[i], z, &screen_x, &screen_y);

        xs[i] = screen_x;
        ys[i] = screen_y;
    }

    upload_and_render(xs, ys, 0, art->width, art->height, art->data);
}*/

int land_block_z_slow(int x, int y)
{
    int block_x = x / 8;
    int block_y = y / 8;
    int dx = x % 8;
    int dy = y % 8;
    land_block_t *lb = get_land_block(1, block_x, block_y);
    if (lb)
    {
        int z = lb->tiles[dx + dy * 8].z;
        return z;
    }
    else
    {
        return 0;
    }
}

// this might be optimized when it comes to z calculations
void draw_world_land_ps(pixel_storage_i *ps, int x, int y)
{
    int dxs[4] = { 0, 1, 1, 0 };
    int dys[4] = { 0, 0, 1, 1 };

    int xs[4];
    int ys[4];

    int zs[4];
    for (int i = 0; i < 4; i++)
    {
        zs[i] = land_block_z_slow(x + dxs[i], y + dys[i]);
    }

    for (int i = 0; i < 4; i++)
    {
        int screen_x;
        int screen_y;
        world_to_screen(x + dxs[i], y + dys[i], zs[i], &screen_x, &screen_y);

        // land tiles are shifted half a world unit upwards
        screen_y -= 22;

        xs[i] = screen_x;
        ys[i] = screen_y;
    }

    // find smallest z and use that for prio order 
    // seems to work ok...
    int min_z = zs[0];
    for (int i = 1; i < 4; i++)
    {
        if (zs[i] < min_z)
        {
            min_z = zs[i];
        }
    }
    render(ps, xs, ys, world_draw_prio(x, y, min_z));
}

void draw_world_art_ps(pixel_storage_i *ps, int x, int y, int z, int height)
{
    if (z >= draw_ceiling)
    {
        return;
    }

    int screen_x;
    int screen_y;
    world_to_screen(x, y, z, &screen_x, &screen_y);
    // offset x by art's width/2
    screen_x -= ps->width / 2;
    // offset y by art's height
    screen_y -= ps->height;
    // statics tiles are shifted half a world unit downwards
    screen_y += 22;
    blit_ps(ps, screen_x, screen_y, world_draw_prio(x, y, z+height));
}

void draw_world_art(ml_art *art, int x, int y, int z)
{
    int screen_x;
    int screen_y;
    world_to_screen(x, y, z, &screen_x, &screen_y);
    // offset x by art's width/2
    screen_x -= art->width / 2;
    // offset y by art's height
    screen_y -= art->height;
    blit(screen_x, screen_y, world_draw_prio(x, y, z), art->width, art->height, art->data);
}

void draw_world_anim_frame(anim_frame_t *frame, bool flip, int x, int y, int z)
{
    pixel_storage_i *ps = &frame->ps;
    int screen_x;
    int screen_y;
    world_to_screen(x, y, z, &screen_x, &screen_y);
    // offset by frame's center in y
    screen_y -= frame->center_y;
    // ...and offset y by frame's height
    screen_y -= ps->height;
    if (flip)
    {
        // offset by frame's center in x
        screen_x -= (frame->ps.width - frame->center_x - 1);
        blit_ps_flipped(ps, screen_x, screen_y, world_draw_prio(x, y, z));
    }
    else
    {
        // offset by frame's center in x
        screen_x -= frame->center_x;
        blit_ps(ps, screen_x, screen_y, world_draw_prio(x, y, z));
    }
}

void draw_world_land(int tile_id, int x, int y)
{
    pixel_storage_i *ps = get_land_ps(tile_id);
    // TODO: this null check shouldn't be necessary
    if (ps)
    {
        draw_world_land_ps(ps, x, y);
    }
}

void draw_world_land_block(int map, int block_x, int block_y)
{
    land_block_t *lb = get_land_block(map, block_x, block_y);

    // TODO: this null check shouldn't be necessary
    if (lb)
    {
        for (int dy = 0; dy < 8; dy++)
        for (int dx = 0; dx < 8; dx++)
        {
            int tile_id = lb->tiles[dx + dy * 8].tile_id;
            //int z = lb->tiles[dx + dy * 8].z;

            draw_world_land(tile_id, 8 * block_x + dx, 8 * block_y + dy);
        }
    }
}

void draw_world_static(int item_id, int x, int y, int z)
{
    pixel_storage_i *ps = get_static_ps(item_id);
    // TODO: this null check shouldn't be necessary
    if (ps)
    {
        int height = ml_get_item_data(item_id)->height;
        draw_world_art_ps(ps, x,  y, z, height);
    }
}

void draw_world_anim(int body_id, int action, int direction, int x, int y, int z)
{
    int frame_count;
    bool flip = false;
    direction = (direction + 5) % 8;
    if (direction >= 5)
    {
        direction = 8 - direction;
        flip = true;
    }
    anim_frame_t *frames = get_anim_frames(body_id, action, direction, &frame_count);
    // TODO: this null check shouldn't be necessary
    if (frames)
    {
        draw_world_anim_frame(&frames[(now / 200) % frame_count], flip, x, y, z);
    }
}


void draw_world_mobile(mobile_t *mobile)
{
    draw_world_anim(mobile->body_id, 0, mobile->dir, mobile->x, mobile->y, mobile->z);
    for (int i = 0; i < 32; i++)
    {
        int layer = i;
        int item_id = mobile->equipped_item_id[layer];
        if (item_id != 0)
        {
            ml_item_data_entry *item_data = ml_get_item_data(item_id);
            if (item_data->animation != 0)
            {
                draw_world_anim(item_data->animation, 0, mobile->dir, mobile->x, mobile->y, mobile->z);
            }
        }
    }
}

void draw_world_statics_block(int map, int block_x, int block_y)
{
    statics_block_t *sb = get_statics_block(map, block_x, block_y);

    // TODO: this null check shouldn't be necessary
    if (sb)
    {
        for (int i = 0; i < sb->statics_count; i++)
        {
            int item_id = sb->statics[i].tile_id;
            int dx = sb->statics[i].dx;
            int dy = sb->statics[i].dy;
            int z  = sb->statics[i].z;

            draw_world_static(item_id, 8 * block_x + dx, 8 * block_y + dy, z);
        }
    }
}

/*void game_add_item(uint32_t id, int item_id, int x, int y, int z)
{
    item_t item = { id, item_id, x, y, z };
    items.push_back(item);
}*/

void game_set_player_info(uint32_t id, int body_id, int x, int y, int z, int dir)
{
    printf("player id: %x\n", id);
    player.id = id;
    player.body_id = body_id;
    player.x = x;
    player.y = y;
    player.z = z;
    player.dir = dir;
}

void game_set_player_pos(int x, int y, int z, int dir)
{
    printf("player pos: %d %d %d\n", x, y, z);
    player.x = x;
    player.y = y;
    player.z = z;
    player.dir = dir;
}

void game_equip(mobile_t *m, int id, int item_id, int layer, int hue)
{
    // TODO: track item..
    // TODO: more general equip code
    assert(layer >= 0 && layer < 32);
    m->equipped_item_id[layer] = item_id;
}

item_t *game_get_item(uint32_t id)
{
    std::map<int, item_t *>::iterator it = items.find(id);
    if (it != items.end())
    {
        return it->second;
    }
    else
    {
        item_t *i = (item_t *)malloc(sizeof(item_t));
        memset(i, 0, sizeof(*i));
        i->id = id;
        items[id] = i;
        return i;
    }
}

mobile_t *game_get_mobile(uint32_t id)
{
    if (player.id == id)
    {
        return &player;
    }
    else
    {
        std::map<int, mobile_t *>::iterator it = mobiles.find(id);
        if (it != mobiles.end())
        {
            return it->second;
        }
        else
        {
            mobile_t *m = (mobile_t *)malloc(sizeof(mobile_t));
            memset(m, 0, sizeof(*m));
            m->id = id;
            mobiles[id] = m;
            return m;
        }
    }
}

void game_delete_object(uint32_t id)
{
    // delete mobile
    {
        std::map<int, mobile_t *>::iterator it = mobiles.find(id);
        if (it != mobiles.end())
        {
            // TODO: delete all equipped items, gumps etc

            free(it->second);
            mobiles.erase(it);
        }
    }
    // delete item
    {
        std::map<int, item_t *>::iterator it = items.find(id);
        if (it != items.end())
        {
            // TODO: delete all contained items, gumps etc

            free(it->second);
            items.erase(it);
        }
    }
}

// find ceiling at player's pos
int find_ceiling(int map, int p_x, int p_y, int p_z)
{
    // we need to look through statics and dynamic items.
    // for now, just look at statics...

    int block_x = p_x / 8;
    int block_y = p_y / 8;

    int p_dx = p_x % 8;
    int p_dy = p_y % 8;

    int ceiling = 128;
    return ceiling;

    statics_block_t *sb = get_statics_block(map, block_x, block_y);

    if (sb)
    {
        for (int i = 0; i < sb->statics_count; i++)
        {
            int item_id = sb->statics[i].tile_id;
            int dx = sb->statics[i].dx;
            int dy = sb->statics[i].dy;
            int z  = sb->statics[i].z;

            if (dx == p_dx && dy == p_dy && z > p_z && z < ceiling)
            {
                ceiling = z;
            }
        }
    }

    printf("ceiling: %d\n", ceiling);

    return ceiling;
}

int main()
{
    ml_init();
    mlt_init();
    net_init();
    net_connect();

    SDL_Window *main_window;
    SDL_GLContext main_context;
 
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        sdl_die("Unable to initialize SDL");
    }
 
    // Request opengl 3.2 context.
    // SDL doesn't have the ability to choose which profile at this time of writing,
    // but it should default to the core profile */
    //SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    //SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
 
    // Turn on double buffering with a 24bit Z buffer.
    // You may need to change this to 16 or 32 for your system
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
 
    // Create our window centered at 512x512 resolution
    main_window = SDL_CreateWindow(":D", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        512, 512, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    if (!main_window)
    {
        sdl_die("Unable to create window");
    }
 
    checkSDLError(__LINE__);
 
    // Create our opengl context and attach it to our window
    main_context = SDL_GL_CreateContext(main_window);
    checkSDLError(__LINE__);
 
    // 0 = free running
    // 1 = vsync
    SDL_GL_SetSwapInterval(1);

    long next_move = -1;
    int move_dx;
    int move_dy;
    int move_dir;
 
    long start = SDL_GetTicks();

    long next_fps = start + 1000;
    int frames = 0;
    bool running = true;
    while (running)
    {
        // first some housekeeping...

        now = SDL_GetTicks() - start;

        // FPS counter
        if (now > next_fps)
        {
            char title[64];
            sprintf(title, ":D - fps: %d", frames);
            SDL_SetWindowTitle(main_window, title);

            next_fps += 1000;
            frames = 0;
        }
        frames += 1;

        // event polling
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            // window closed
            if (e.type == SDL_QUIT)
            {
                running = false;
            }
            if (e.type == SDL_KEYDOWN)
            {
                running = false;
            }
            if (e.type == SDL_MOUSEBUTTONDOWN)
            {
                if (e.button.button == SDL_BUTTON_RIGHT)
                {
                    next_move = now;
                }
            }
            if (e.type == SDL_MOUSEBUTTONUP)
            {
                if (e.button.button == SDL_BUTTON_RIGHT)
                {
                    next_move = -1;
                }
            }
            if (e.type == SDL_MOUSEMOTION)
            {
                int x = e.motion.x;
                int y = e.motion.y;
                float angle = atan2(y-screen_center_y, x-screen_center_x);
                angle += M_PI/8.0;
                if (angle < 0.0)
                {
                    angle += 2.0*M_PI;
                }
                int dir = ((int)(8 * angle / (2.0*M_PI) + 1)) % 8;
                move_dir = dir;

                // UO dirs:

                // server:
                // 0 North
                // 1 Northeast
                // 2 East
                // 3 Southeast
                // 4 South
                // 5 Southwest
                // 6 West
                // 7 Northwest

                // animations
                // 0: south east
                // 1: south
                // 2: south west
                // 3: west
                // 4: north west
                // 5: north
                // 6: north east
                // 7: east



                //int dxs[8] = { 1, 0, -1, -1, -1, 0, 1, 1 };
                //int dys[8] = { 1, 1, 1, 0, -1, -1, -1, 0 };

                int dxs[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
                int dys[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };

                move_dx = dxs[dir];
                move_dy = dys[dir];
            }
        }

        if (next_move != -1 && now >= next_move)
        {
            if (player.dir == move_dir)
            {
                player.x += move_dx;
                player.y += move_dy;
            }
            else
            {
                player.dir = move_dir;
            }
            net_send_move(player.dir);

            next_move += 100;
        }

        // network...
        net_poll();

        // resource load handling
        mlt_process_callbacks();

        // draw distinct background
        glClearColor(1.0, 0.0, 1.0, 1.0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glColor4f(1.0f, 1.0f, 0.0f, 1.0f);
        glBegin(GL_TRIANGLES);
        glVertex2f(-1.0f, -1.0f);
        glVertex2f( 1.0f, -1.0f);
        glVertex2f( 0.0f,  1.0f);
        glEnd();

        int world_center_block_x = player.x / 8;
        int world_center_block_y = player.y / 8;

        draw_ceiling = find_ceiling(1, player.x, player.y, player.z);

        // draw land blocks
        for (int block_dy = -1; block_dy <= 1; block_dy++)
        for (int block_dx = -1; block_dx <= 1; block_dx++)
        {
            int block_x = world_center_block_x + block_dx;
            int block_y = world_center_block_y + block_dy;

            draw_world_land_block(1, block_x, block_y);
        }
        // draw statics blocks
        for (int block_dy = -1; block_dy <= 1; block_dy++)
        for (int block_dx = -1; block_dx <= 1; block_dx++)
        {
            int block_x = world_center_block_x + block_dx;
            int block_y = world_center_block_y + block_dy;

            draw_world_statics_block(1, block_x, block_y);
        }
        // draw items
        {
            std::map<int, item_t *>::iterator it;
            for (it = items.begin(); it != items.end(); ++it)
            {
                item_t *item = it->second;
                draw_world_static(item->item_id, item->x, item->y, item->z);
            }
        }
        // draw mobiles
        {
            std::map<int, mobile_t *>::iterator it;
            for (it = mobiles.begin(); it != mobiles.end(); ++it)
            {
                //printf("drawing %p\n", it->second);
                draw_world_mobile(it->second);
            }
        }
        // draw character
        {
            draw_world_mobile(&player);
        }

        //
        SDL_GL_SwapWindow(main_window);
    }
 
    SDL_GL_DeleteContext(main_context);
    SDL_DestroyWindow(main_window);
    SDL_Quit();
 
    return 0;
}

