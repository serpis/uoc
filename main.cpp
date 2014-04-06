#include <cassert>
#include <cstdio>
#include <cstdlib>

#include <stdint.h>

#include <algorithm>
#include <map>
#include <list>
#include <string>
#include <iostream>

#include "net.hpp"
#include "mullib.hpp"
#include "game.hpp"
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

extern int cnt_render_commands;
extern int cnt_use_program;
extern int cnt_bind_tex0;
extern int cnt_bind_tex1;
extern int cnt_glbegin;
extern int cnt_glend;

const int ping_frequency = 30000;

const int TYPE_LAND        = 0;
const int TYPE_STATIC      = 1;
const int TYPE_ITEM        = 2;
const int TYPE_MOBILE      = 3;
const int TYPE_GUMP        = 4;
const int TYPE_GUMP_WIDGET = 5;

int window_width = 800;
int window_height = 600;

std::wstring chat_str;

struct pick_target_t
{
    int type;
    union
    {
        struct 
        {
            int x, y, z;
            int tile_id;
        } land;
        struct 
        {
            int x, y, z;
            int item_id;
        } static_item;
        struct 
        {
            item_t *item;
        } item;
        struct 
        {
            mobile_t *mobile;
        } mobile;
        struct
        {
            gump_t *gump;
        } gump;
        struct
        {
            gump_t *gump;
            gump_widget_t *widget;
        } gump_widget;
    };
};
static pick_target_t pick_slots[0x10000];
static bool picking_enabled = false;
static int next_pick_id = 0;

const int DRAGTYPE_NONE = 0;
const int DRAGTYPE_ITEM = 1;
const int DRAGTYPE_GUMP = 2;
static struct
{
    int type;
    union
    {
        struct
        {
            uint32_t serial;
            int item_id;
            int hue_id;
        } item;
        struct
        {
            gump_t *gump;
            int handle_x;
            int handle_y;
        } gump;
    };
} dragging;

static int layer_draw_order[32] = 
{
     9, 20, 11, 16, 18,  4, 14,  3,
     8, 24,  5, 13, 17, 10,  6, 19,
     7, 22, 23, 12,  1,  2, 21, 15,
     0, 25, 26, 27, 28, 29, 30, 31,
};

int pick_land(int x, int y, int z, int tile_id)
{
    if (!picking_enabled)
    {
        return -1;
    }
    assert(next_pick_id < sizeof(pick_slots)/sizeof(pick_slots[0]));
    pick_slots[next_pick_id].type = TYPE_LAND;
    pick_slots[next_pick_id].land.x = x;
    pick_slots[next_pick_id].land.y = y;
    pick_slots[next_pick_id].land.z = z;
    pick_slots[next_pick_id].land.tile_id = tile_id;
    return next_pick_id++;
}

int pick_static(int x, int y, int z, int item_id)
{
    if (!picking_enabled)
    {
        return -1;
    }
    assert(next_pick_id < sizeof(pick_slots)/sizeof(pick_slots[0]));
    pick_slots[next_pick_id].type = TYPE_STATIC;
    pick_slots[next_pick_id].static_item.x = x;
    pick_slots[next_pick_id].static_item.y = y;
    pick_slots[next_pick_id].static_item.z = z;
    pick_slots[next_pick_id].static_item.item_id = item_id;
    return next_pick_id++;
}

int pick_item(item_t *item)
{
    if (!picking_enabled)
    {
        return -1;
    }
    assert(next_pick_id < sizeof(pick_slots)/sizeof(pick_slots[0]));
    pick_slots[next_pick_id].type = TYPE_ITEM;
    pick_slots[next_pick_id].item.item = item;
    return next_pick_id++;
}

int pick_mobile(mobile_t *mobile)
{
    if (!picking_enabled)
    {
        return -1;
    }
    assert(next_pick_id < sizeof(pick_slots)/sizeof(pick_slots[0]));
    pick_slots[next_pick_id].type = TYPE_MOBILE;
    pick_slots[next_pick_id].mobile.mobile = mobile;
    return next_pick_id++;
}

int pick_gump(gump_t *gump)
{
    if (!picking_enabled)
    {
        return -1;
    }
    assert(next_pick_id < sizeof(pick_slots)/sizeof(pick_slots[0]));
    pick_slots[next_pick_id].type = TYPE_GUMP;
    pick_slots[next_pick_id].gump.gump = gump;
    return next_pick_id++;
}

int pick_gump_widget(gump_t *gump, gump_widget_t *widget)
{
    if (!picking_enabled)
    {
        return -1;
    }
    assert(next_pick_id < sizeof(pick_slots)/sizeof(pick_slots[0]));
    pick_slots[next_pick_id].type = TYPE_GUMP_WIDGET;
    pick_slots[next_pick_id].gump_widget.gump = gump;
    pick_slots[next_pick_id].gump_widget.widget = widget;
    return next_pick_id++;
}


long now;

std::map <int, item_t *> items;
std::map <int, multi_t *> multis;
std::map <int, mobile_t *> mobiles;
std::list<gump_t *> gump_list;

int draw_ceiling = 128;
bool draw_roofs = true;

int prg_blit_picking;
int prg_blit_hue;

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
    uint8_t header[] =
    {
        0,
        0,
        2,
        0, 0,
        0, 0,
        0,
        0, 0,
        0, 0,
        (uint8_t)(width & 0xff),
        (uint8_t)(width >> 8),
        (uint8_t)(height & 0xff),
        (uint8_t)(height >> 8),
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
        uint8_t buf[4] = { r, g, b, a };
        fwrite(buf, 3, 1, f);
    }
    fclose(f);
}

struct anim_frame_t
{
    int center_x, center_y;
    pixel_storage_t ps;
};

static struct
{
    struct
    {
        bool valid;
        pixel_storage_t ps;
    } entries[8*375];
} hue_cache;

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
        pixel_storage_t ps;
    } entries[0x4000];
} land_cache;

static struct
{
    struct
    {
        bool valid;
        bool fetching;
        pixel_storage_t ps;
    } entries[0x10000];
} static_cache;

static struct
{
    struct
    {
        bool valid;
        bool fetching;
        pixel_storage_t ps;
    } entries[0x10000];
} gump_cache;

static struct
{
    struct
    {
        bool valid;
        bool fetching;
        ml_multi *multi;
    } entries[0x2000];
} multi_cache;

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
    int item_id;
    int dx, dy, z;
};

struct statics_block_t
{
    int statics_count;
    int roof_heights[8*8];
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

struct string_cache_entry_t
{
    bool fetching;
    pixel_storage_t ps;
};
static struct
{
    std::map<std::wstring, string_cache_entry_t> entries;
} string_cache;

pixel_storage_t get_hue_tex(int hue_id)
{
    assert(hue_id > 0 && hue_id < 8*375);
    if (!hue_cache.entries[hue_id-1].valid)
    {
        hue_cache.entries[hue_id-1].valid = true;

        ml_hue *hue = ml_get_hue(hue_id-1);
        hue_cache.entries[hue_id-1].ps = gfx_upload_tex2d(32, 1, hue->colors);
    }

    return hue_cache.entries[hue_id-1].ps;
}


void write_anim_frames(int body_id, int action, int direction, ml_anim *anim)
{
    int body_action_id = body_id * 35 + action;

    anim_frame_t *frames = (anim_frame_t *)malloc(anim->frame_count * sizeof(anim_frame_t));

    for (int j = 0; j < anim->frame_count; j++)
    {
        frames[j].center_x = anim->frames[j].center_x;
        frames[j].center_y = anim->frames[j].center_y;
        frames[j].ps = gfx_upload_tex2d(anim->frames[j].width, anim->frames[j].height, anim->frames[j].data);
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
    land_cache.entries[land_id].ps = gfx_upload_tex2d(l->width, l->height, l->data);
    free(l);

    land_cache.entries[land_id].fetching = false;
}

pixel_storage_t *get_land_ps(int land_id)
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
    static_cache.entries[item_id].ps = gfx_upload_tex2d(s->width, s->height, s->data);
    free(s);

    static_cache.entries[item_id].fetching = false;
}

pixel_storage_t *get_static_ps(int item_id)
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

void write_gump_ps(int gump_id, ml_gump *g)
{
    gump_cache.entries[gump_id].ps = gfx_upload_tex2d(g->width, g->height, g->data);
    free(g);

    gump_cache.entries[gump_id].fetching = false;
}

pixel_storage_t *get_gump_ps(int gump_id)
{
    assert(gump_id >= 0 && gump_id < 0x10000);
    if (!gump_cache.entries[gump_id].valid)
    {
        gump_cache.entries[gump_id].valid = true;
        gump_cache.entries[gump_id].fetching = true;

        //printf("gump_cache       : loading %d\n", gump_id);

        mlt_read_gump(gump_id, write_gump_ps);
    }

    if (gump_cache.entries[gump_id].fetching)
    {
        return NULL;
    }
    else
    {
        return &gump_cache.entries[gump_id].ps;
    }
}

void write_multi(int multi_id, ml_multi *m)
{
    multi_cache.entries[multi_id].multi = m;

    multi_cache.entries[multi_id].fetching = false;
}

ml_multi *get_multi(int multi_id)
{
    assert(multi_id >= 0 && multi_id < 0x10000);
    if (!multi_cache.entries[multi_id].valid)
    {
        multi_cache.entries[multi_id].valid = true;
        multi_cache.entries[multi_id].fetching = true;

        printf("multi_cache       : loading %d\n", multi_id);

        write_multi(multi_id, ml_read_multi(multi_id));
    }

    if (multi_cache.entries[multi_id].fetching)
    {
        return NULL;
    }
    else
    {
        return multi_cache.entries[multi_id].multi;
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

    for (int i = 0; i < 8*8; i++)
    {
        statics_block_cache.entries[cache_block_index].statics_block.roof_heights[i] = sb->roof_heights[i];
    }

    for (int i = 0; i < sb->statics_count; i++)
    {
        statics_block_cache.entries[cache_block_index].statics_block.statics[i].item_id = sb->statics[i].tile_id;
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

void write_string_ps(int font_id, std::wstring str, ml_art *a)
{
    string_cache.entries[str].ps = gfx_upload_tex2d(a->width, a->height, a->data);
    free(a);

    string_cache.entries[str].fetching = false;
}

pixel_storage_t *get_string_ps(int font_id, std::wstring str)
{
    std::map<std::wstring, string_cache_entry_t>::iterator it = string_cache.entries.find(str);
    if (it == string_cache.entries.end())
    {
        string_cache_entry_t entry;
        entry.fetching = true;
        string_cache.entries[str] = entry;
        it = string_cache.entries.find(str);

        //std::wcout << "string_cache       : loading " << str << std::endl;

        //mlt_read_string(font_id, str, write_string_ps);
        write_string_ps(font_id, str, ml_render_string(font_id, str));
    }

    if (it->second.fetching)
    {
        return NULL;
    }
    else
    {
        return &it->second.ps;
    }
}


void blit_ps(pixel_storage_t *ps, int x, int y, int draw_prio, int hue_id, int pick_id)
{
    int xs[4] = { x, x + ps->width, x + ps->width, x };
    int ys[4] = { y, y, y + ps->height, y + ps->height };

    gfx_render(ps, xs, ys, draw_prio, hue_id, pick_id);
}

void blit_ps_flipped(pixel_storage_t *ps, int x, int y, int draw_prio, int hue_id, int pick_id)
{
    int xs[4] = { x + ps->width, x, x, x + ps->width };
    int ys[4] = { y, y, y + ps->height, y + ps->height };

    gfx_render(ps, xs, ys, draw_prio, hue_id, pick_id);
}

mobile_t player =
{
    12345, // serial
    401, // body_id
    1212, 1537, 0, // xyz
    0, // dir
    0, // hue
    0, // noto
    0, // flags
    NULL,
    {NULL}, // equipped items
    0, // last_dir
    0, // last_movement
    4, // action_id
};

// Lord British' throne
//int player.x = 1323;
//int player.y = 1624;
// secret practice place
//int player.x = 1212;
//int player.y = 1537;
//int player.z = 0;

int screen_center_x = window_width / 2;
int screen_center_y = window_height / 2;

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
    int world_dx = world_x - player.x;
    int world_dy = world_y - player.y;

    world_dx += 32;
    world_dy += 32;
    world_z += 128;

    assert(world_dx >= 0 && world_dx < 64);
    assert(world_dy >= 0 && world_dy < 64);
    assert(world_z >= 0 && world_z < 256);

    // 32 layers, 2 types of foliage (is foliage/is not foliage)
    int prio = 2 * 32 * (64 * world_dx + 64 * world_dy + world_z);
    return prio;
}

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

int land_block_z_averaged(int x, int y)
{
    int z_sum = 0;
    const int dxs[4] = { 0, 1, 1, 0 };
    const int dys[4] = { 0, 0, 1, 1 };
    for (int i = 0; i < 4; i++)
    {
        z_sum += land_block_z_slow(x + dxs[i], y + dys[i]);
    }
    return z_sum / 4;
}

// this might be optimized when it comes to z calculations
void draw_world_land_ps(pixel_storage_t *ps, int x, int y, int pick_id)
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
    gfx_render(ps, xs, ys, world_draw_prio(x, y, min_z-1), 0, pick_id);
}

void draw_world_art_ps(pixel_storage_t *ps, int x, int y, int z, int height, int bonus_prio, int hue_id, int pick_id)
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
    blit_ps(ps, screen_x, screen_y, world_draw_prio(x, y, z) + bonus_prio, hue_id, pick_id);
}

void draw_world_anim_frame(anim_frame_t *frame, bool flip, int x, int y, int z, int layer_prio, int hue_id, int pick_id)
{
    pixel_storage_t *ps = &frame->ps;
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
        blit_ps_flipped(ps, screen_x, screen_y, world_draw_prio(x, y, z) + layer_prio, hue_id, pick_id);
    }
    else
    {
        // offset by frame's center in x
        screen_x -= frame->center_x;
        blit_ps(ps, screen_x, screen_y, world_draw_prio(x, y, z) + layer_prio, hue_id, pick_id);
    }
}

void draw_world_land(int tile_id, int x, int y, int pick_id)
{
    pixel_storage_t *ps = get_land_ps(tile_id);
    // TODO: this null check shouldn't be necessary
    if (ps)
    {
        draw_world_land_ps(ps, x, y, pick_id);
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
            int z = lb->tiles[dx + dy * 8].z;

            int x = 8 * block_x + dx;
            int y = 8 * block_y + dy;
            int pick_id = pick_land(x, y, z, tile_id);
            draw_world_land(tile_id, x, y, pick_id);
        }
    }
}

static int gump_draw_order = 0;
void draw_screen_item(int item_id, int x, int y, int hue_id, int pick_id)
{
    pixel_storage_t *ps = get_static_ps(item_id);
    // TODO: this null check shouldn't be necessary
    if (ps)
    {
        // TODO: figure out some kind of depth value
        blit_ps(ps, x, y, gump_draw_order++, hue_id, pick_id);
    }
}

void draw_world_item(int item_id, int x, int y, int z, int hue_id, int pick_id)
{
    pixel_storage_t *ps = get_static_ps(item_id);
    // TODO: this null check shouldn't be necessary
    if (ps)
    {
        int height = ml_get_item_data(item_id)->height;
        bool is_foliage = (ml_get_item_data(item_id)->flags & TILEFLAG_FOLIAGE) != 0;
        draw_world_art_ps(ps, x, y, z, height, is_foliage ? 32 : 0, hue_id, pick_id);
    }
}

void draw_world_anim(int body_id, int action, long action_start, int direction, int x, int y, int z, int layer_prio, int hue_id, int pick_id)
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
        draw_world_anim_frame(&frames[((now-action_start) / 200) % frame_count], flip, x, y, z, layer_prio, hue_id, pick_id);
    }
}

bool body_is_animal(int body_id)
{
    return body_id >= 200 && body_id < 400;
}

bool body_is_monster(int body_id)
{
    return body_id < 200;
}

bool body_is_human(int body_id)
{
    return body_id == 400 || body_id == 401;
}

void draw_world_mobile(mobile_t *mobile, int pick_id)
{
    int action_id = mobile->anim_action_id;
    long action_start = 0;

    int frame_duration = 200;
    if (mobile->anim_start + frame_duration*mobile->anim_total_frames < now)
    {

        mobile->anim_start = -1;
    }
    else
    {
        action_start = mobile->anim_start;
    }

    int walk_action_id;
    int run_action_id;
    int stand_action_id;

    if (body_is_monster(mobile->body_id))
    {
        walk_action_id = 0;
        run_action_id = 0; // dragon's fly seems to be action 19?
        stand_action_id = 1;
    }
    else if (body_is_animal(mobile->body_id))
    {
        walk_action_id = 0;
        run_action_id = 1;
        stand_action_id = 2;
    }
    else // human?
    {
        walk_action_id = (mobile->flags & MOBFLAG_WARMODE) ? 15 : 0;
        run_action_id = 2;
        stand_action_id = (mobile->flags & MOBFLAG_WARMODE) ? 7 : 4;
    }

    // no animation? do stand animation...
    if (mobile->anim_start == -1)
    {
        action_id = stand_action_id;
    }
    // mobiles' walk animation override current animation
    if (now - mobile->last_movement <= 500)
    {
        action_id = walk_action_id;
    }

    draw_world_anim(mobile->body_id, action_id, action_start, mobile->dir, mobile->x, mobile->y, mobile->z, 0, mobile->hue_id, pick_id);
    if (body_is_human(mobile->body_id))
    {
        for (int i = 0; i < 32; i++)
        {
            int layer = layer_draw_order[i];
            item_t *item = mobile->equipped_items[layer];
            if (item != NULL)
            {
                int item_id = item->item_id;
                int hue_id = item->hue_id;
                ml_item_data_entry *item_data = ml_get_item_data(item_id);
                if (item_data->animation != 0)
                {
                    //printf("drawing anim for item_id %d (%s)\n", item_id, item_data->name);
                    draw_world_anim(item_data->animation, action_id, action_start, mobile->dir, mobile->x, mobile->y, mobile->z, i, hue_id, pick_id);
                }
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
            int item_id = sb->statics[i].item_id;
            int dx = sb->statics[i].dx;
            int dy = sb->statics[i].dy;
            int z  = sb->statics[i].z;

            // conditionally skip roofs
            if ((ml_get_item_data(item_id)->flags & TILEFLAG_ROOF) && !draw_roofs)
            {
                continue;
            }

            int x = 8 * block_x + dx;
            int y = 8 * block_y + dy;
            draw_world_item(item_id, x, y, z, 0, pick_static(x, y, z, item_id));
        }
    }
}

void game_set_player_serial(uint32_t serial)
{
    //printf("player serial: %x\n", serial);
    player.serial = serial;
}

uint32_t game_get_player_serial()
{
    return player.serial;
}

void game_equip(mobile_t *m, uint32_t serial, int item_id, int layer, int hue_id)
{
    item_t *item = game_get_item(serial);
    item->item_id = item_id;
    item->hue_id = hue_id;
    item->loc.equipped.mobile = m;
    item->space = SPACETYPE_EQUIPPED;

    assert(layer >= 0 && layer < 32);
    if (m->equipped_items[layer] != NULL && m->equipped_items[layer] != item)
    {
        printf("got item duplicate item for a layer... this will likely leak memory\n");
    }
    //assert(m->equipped_items[layer] == NULL || m->equipped_items[layer] == item);
    m->equipped_items[layer] = item;
}

item_t *game_get_item(uint32_t serial)
{
    std::map<int, item_t *>::iterator it = items.find(serial);
    if (it != items.end())
    {
        return it->second;
    }
    else
    {
        assert(it == items.end());
        item_t *i = (item_t *)malloc(sizeof(item_t));
        memset(i, 0, sizeof(*i));
        i->serial = serial;
        items[serial] = i;
        return i;
    }
}

multi_t *game_get_multi(uint32_t serial)
{
    std::map<int, multi_t *>::iterator it = multis.find(serial);
    if (it != multis.end())
    {
        return it->second;
    }
    else
    {
        assert(it == multis.end());
        multi_t *m = (multi_t *)malloc(sizeof(multi_t));
        memset(m, 0, sizeof(*m));
        m->serial = serial;
        multis[serial] = m;
        return m;
    }
}

mobile_t *game_get_mobile(uint32_t serial)
{
    if (player.serial == serial)
    {
        return &player;
    }
    else
    {
        std::map<int, mobile_t *>::iterator it = mobiles.find(serial);
        assert(it != mobiles.end());
        return it->second;
    }
}

mobile_t *game_create_mobile(uint32_t serial)
{
    // servers will often create mobiles that already exist,
    // so first we'll check if the mobile already exists.
    if (player.serial == serial)
    {
        return &player;
    }
    else
    {
        std::map<int, mobile_t *>::iterator it = mobiles.find(serial);
        if (it != mobiles.end())
        {
            return it->second;
        }
    }

    mobile_t *m = (mobile_t *)malloc(sizeof(mobile_t));
    memset(m, 0, sizeof(*m));
    m->serial = serial;
    m->anim_start = -1;
    mobiles[serial] = m;
    return m;
}

// the three functions:
//  game_delete_mobile
//  game_delete_item
//  game_delete_gump
// are tightly aware of eachother's inner workings
// and cooperate to clean up all traces of objects and their children
// this cleaning up including removing objects from the global lists and
// freeing any dynamically allocated objects
void game_delete_item(item_t *item);
void game_delete_gump(gump_t *gump);
void game_delete_mobile(mobile_t *mobile)
{
    for (int i = 0; i < 32; i++)
    {
        if (mobile->equipped_items[i] != NULL)
        {
            game_delete_item(mobile->equipped_items[i]);

            // delete_item will set this pointer to NULL,
            //  but be explicit for clarity
            mobile->equipped_items[i] = NULL;
        }
    }

    if (mobile->paperdoll_gump != NULL)
    {
        game_delete_gump(mobile->paperdoll_gump);

        // delete_gump will set this pointer to NULL,
        //  but be explicit for clarity
        mobile->paperdoll_gump = NULL;
    }

    std::map<int, mobile_t *>::iterator it = mobiles.find(mobile->serial);
    assert(it != mobiles.end());
    assert(it->second == mobile);
    mobiles.erase(it);

    free(mobile);
}

void game_delete_item(item_t *item)
{
    // TODO: fix these things:
    // 1. if item is container, delete any gumps and items inside container
    assert(item->space == SPACETYPE_WORLD || item->space == SPACETYPE_CONTAINER || item->space == SPACETYPE_EQUIPPED);
    assert(item->container_gump == NULL);

    if (item->space == SPACETYPE_CONTAINER)
    {
        assert(item->loc.container.container->type == GUMPTYPE_CONTAINER);
        item->loc.container.container->container.items->remove(item);
    }
    else if (item->space == SPACETYPE_EQUIPPED)
    {
        mobile_t *m = item->loc.equipped.mobile;

        // TODO: use named constant instead of "32"
        bool found = false;
        for (int i = 0; i < 32; i++)
        {
            if (m->equipped_items[i] == item)
            {
                m->equipped_items[i] = NULL;
                found = true;
                break;
            }
        }
        assert(found);
    }

    std::map<int, item_t *>::iterator it = items.find(item->serial);
    assert(it != items.end());
    assert(it->second == item);
    items.erase(it);

    free(item);
}

void game_delete_gump(gump_t *gump)
{
    if (gump->type == GUMPTYPE_CONTAINER)
    {
        // delete all contained items
        while (gump->container.items->size() > 0)
        {
            // item will remove itself from this container's item list
            item_t *item = *gump->container.items->begin();
            game_delete_item(item);
        }
        delete gump->container.items;
        gump->container.item->container_gump = NULL;
    }
    else if (gump->type == GUMPTYPE_GENERIC)
    {
        // delete all contained widgets
        for (std::list<gump_widget_t>::iterator it = gump->generic.widgets->begin(); it != gump->generic.widgets->end(); ++it)
        {
            gump_widget_t &widget = *it;
            // TEXT widget needs to delete contained string
            if (widget.type == GUMPWTYPE_TEXT)
            {
                delete widget.text.text;
            }
        }
        delete gump->generic.widgets;
    }
    else if (gump->type == GUMPTYPE_PAPERDOLL)
    {
        delete gump->paperdoll.name;
        mobile_t *m = gump->paperdoll.mobile;
        m->paperdoll_gump = NULL;
    }
    else
    {
        assert(0 && "don't know how to delete this kind of gump");
    }

    std::list<gump_t *>::iterator it = std::find(gump_list.begin(), gump_list.end(), gump);
    assert(it != gump_list.end());
    gump_list.erase(it);

    free(gump);
}

void game_delete_object(uint32_t serial)
{
    // delete mobile
    {
        std::map<int, mobile_t *>::iterator it = mobiles.find(serial);
        if (it != mobiles.end())
        {
            game_delete_mobile(it->second);
        }
    }
    // delete item
    {
        std::map<int, item_t *>::iterator it = items.find(serial);
        if (it != items.end())
        {
            game_delete_item(it->second);
        }
    }
}


void game_show_container(uint32_t item_serial, int gump_id)
{
    std::map<int, item_t *>::iterator it = items.find(item_serial);
    assert(it != items.end());

    item_t *item = it->second;

    if (item->container_gump != NULL)
    {
        gump_t *container = item->container_gump;
        assert(container->type == GUMPTYPE_CONTAINER && container->container.gump_id == gump_id);
        // TODO: remove all items in this container ??
    }
    else
    {
        gump_t *container = (gump_t *)malloc(sizeof(gump_t));
        memset(container, 0, sizeof(gump_t));
        container->type = GUMPTYPE_CONTAINER;
        container->container.item = item;
        container->container.gump_id = gump_id;
        container->container.items = new std::list<item_t *>();

        item->container_gump = container;

        gump_list.push_back(container);
    }
}

void game_show_paperdoll(mobile_t *m, std::wstring name)
{
    if (m->paperdoll_gump != NULL)
    {
        gump_t *paperdoll = m->paperdoll_gump;
        assert(paperdoll->type == GUMPTYPE_PAPERDOLL);
    }
    else
    {
        gump_t *paperdoll = (gump_t *)malloc(sizeof(gump_t));
        memset(paperdoll, 0, sizeof(gump_t));
        paperdoll->type = GUMPTYPE_PAPERDOLL;
        paperdoll->paperdoll.mobile = m;
        paperdoll->paperdoll.name = new std::wstring;
        *paperdoll->paperdoll.name = name;
        m->paperdoll_gump = paperdoll;

        gump_list.push_back(paperdoll);
    }
}

gump_t *game_get_container(uint32_t item_serial)
{
    std::map<int, item_t *>::iterator it = items.find(item_serial);
    assert(it != items.end());

    item_t *item = it->second;

    gump_t *container = item->container_gump;
    assert(container != NULL);
    assert(container->type == GUMPTYPE_CONTAINER);

    return container;
}

int find_lowest_connected_roof(bool visited[64*64], int map, int start_x, int start_y, int x, int y)
{
    //printf("(%d, %d)...", x, y);
    {
        int dx = x - start_x + 31;
        int dy = y - start_y + 31;
        // quick-exit condition
        if (dx < 0 || dx >= 64 || dy < 0 || dy >= 64 || visited[dx + dy * 64])
        {
            //printf("quick_exit.\n");
            return -1;
        }

        // mark this as visited
        visited[dx + dy * 64] = true;
    }

    {
        int block_x = x / 8;
        int block_y = y / 8;

        int dx = x % 8;
        int dy = y % 8;

        statics_block_t *sb = get_statics_block(map, block_x, block_y);
        if (!sb)
        {
            //printf("no sb.\n");
            return -1;
        }

        int h = sb->roof_heights[dx + dy * 8];
        if (h == -1)
        {
            //printf("h = -1.\n");
            return -1;
        }
        else
        {
            //printf("inspecting children\n");
            int dxs[4] = { -1, 0, 1, 0 };
            int dys[4] = { 0, -1, 0, 1 };
            for (int i = 0; i < 4; i++)
            {
                int child_height = find_lowest_connected_roof(visited, map, start_x, start_y, x + dxs[i], y + dys[i]);
                if (child_height != -1 && child_height < h)
                {
                    h = child_height;
                }
            }
        }
        return h;
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
    //return ceiling;

    bool is_roof = false;

    statics_block_t *sb = get_statics_block(map, block_x, block_y);


    if (sb)
    {
        for (int i = 0; i < sb->statics_count; i++)
        {
            int item_id = sb->statics[i].item_id;
            int dx = sb->statics[i].dx;
            int dy = sb->statics[i].dy;
            int z  = sb->statics[i].z;

            ml_item_data_entry *item_data = ml_get_item_data(item_id);

            if (dx == p_dx && dy == p_dy && z > p_z && z < ceiling)
            {
                ceiling = z;

                if (item_data->flags & TILEFLAG_ROOF)
                {
                    //printf("is roof %d %d %d %d %d\n", item_data->weight, item_data->quality, item_data->quantity, item_data->animation, item_data->height);
                    is_roof = true;
                }
                else
                {
                    is_roof = false;
                }
            }
        }
    }

    if (is_roof)
    {
        // ceiling is a roof!
        // do flood fill to find lowest connected roof, and use that as ceiling
        bool visited[64*64];
        memset(visited, 0, sizeof(visited));
        ceiling = find_lowest_connected_roof(visited, map, p_x, p_y, p_x, p_y);
    }

    //printf("ceiling: %d\n", ceiling);

    return ceiling;
}

bool has_roof(int map, int p_x, int p_y)
{
    int block_x = p_x / 8;
    int block_y = p_y / 8;

    int p_dx = p_x % 8;
    int p_dy = p_y % 8;

    statics_block_t *sb = get_statics_block(map, block_x, block_y);
    if (sb)
    {
        for (int i = 0; i < sb->statics_count; i++)
        {
            int item_id = sb->statics[i].item_id;
            int dx = sb->statics[i].dx;
            int dy = sb->statics[i].dy;
            int z  = sb->statics[i].z;

            if (dx == p_dx && dy == p_dy && (ml_get_item_data(item_id)->flags & TILEFLAG_ROOF))
            {
                return true;
            }
        }
    }

    return false;
}

bool is_in_draw_range(int x, int y, int z)
{
    int dx = player.x - x;
    int dy = player.y - y;
    return std::max(std::abs(dx), std::abs(dy)) <= 18;
}

void draw_world()
{
    int world_center_block_x = player.x / 8;
    int world_center_block_y = player.y / 8;

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
            if (item->space == SPACETYPE_WORLD)
            {
                if (is_in_draw_range(item->loc.world.x, item->loc.world.y, item->loc.world.z))
                {
                    draw_world_item(item->item_id, item->loc.world.x, item->loc.world.y, item->loc.world.z, item->hue_id, pick_item(item));
                }
            }
        }
    }
    // draw multis
    {
        std::map<int, multi_t *>::iterator it;
        for (it = multis.begin(); it != multis.end(); ++it)
        {
            multi_t *multi = it->second;
            ml_multi *m = get_multi(multi->multi_id);
            if (m)
            {
                for (int i = 0; i < m->item_count; i++)
                {
                    int item_id = m->items[i].item_id;
                    int x = multi->x + m->items[i].x;
                    int y = multi->y + m->items[i].y;
                    int z = multi->z + m->items[i].z;
                    bool visible = m->items[i].visible;
                    //assert(visible);
                    if (visible)
                    {
                        if (is_in_draw_range(x, y, z))
                        {
                            draw_world_item(item_id, x, y, z, 0, pick_static(x, y, z, item_id));
                        }
                    }
                }
            }
        }
    }
    // draw mobiles
    {
        std::map<int, mobile_t *>::iterator it;
        for (it = mobiles.begin(); it != mobiles.end(); ++it)
        {
            mobile_t *mobile = it->second;
            if (is_in_draw_range(mobile->x, mobile->y, mobile->z))
            {
                draw_world_mobile(mobile, pick_mobile(mobile));
            }
        }
    }
    // draw character
    {
        draw_world_mobile(&player, pick_mobile(&player));
    }
}

void draw_gump(int gump_id, int x, int y, int hue_id, int pick_id)
{
    pixel_storage_t *ps = get_gump_ps(gump_id);
    // TODO: this null check shouldn't be necessary
    if (ps)
    {
        blit_ps(ps, x, y, gump_draw_order++, hue_id, pick_id);
    }
}

void draw_gump_tiled(int gump_id, int x, int y, int width, int height, int hue_id, int pick_id)
{
    pixel_storage_t *ps = get_gump_ps(gump_id);
    // TODO: this null check shouldn't be necessary
    if (ps)
    {
        for (int draw_y = y; draw_y < y + height; draw_y += ps->height)
        for (int draw_x = x; draw_x < x + width ; draw_x += ps->width )
        {
            int draw_width  = std::min(x + width  - draw_x, ps->width);
            int draw_height = std::min(y + height - draw_y, ps->height);

            bool custom_size = (draw_width  != ps->width) ||
                               (draw_height != ps->height);
            if (custom_size)
            {
                pixel_storage_t temp_ps;
                temp_ps.width  = draw_width;
                temp_ps.height = draw_height;
                temp_ps.tex    = ps->tex;
                // TODO: should do something more clever with texture coords...
                memcpy(temp_ps.tcxs, ps->tcxs, sizeof(temp_ps.tcxs));
                memcpy(temp_ps.tcys, ps->tcys, sizeof(temp_ps.tcys));
                blit_ps(&temp_ps, draw_x, draw_y, gump_draw_order++, hue_id, pick_id);
            }
            else
            {
                blit_ps(ps, draw_x, draw_y, gump_draw_order++, hue_id, pick_id);
            }
        }
        //blit_ps_tiled(ps, x, y, width, height, 0, hue_id, pick_id);
    }
}

void draw_gump_dynamic(int gump_id_base, int x, int y, int width, int height, int hue_id, int pick_id)
{
    pixel_storage_t *pss[3];
    pss[0] = get_gump_ps(gump_id_base + 0);
    pss[1] = get_gump_ps(gump_id_base + 2);
    pss[2] = get_gump_ps(gump_id_base + 6);

    // first make sure all pss are loaded
    for (int i = 0; i < 3; i++)
    {
        if (!pss[i])
        {
            return;
        }
    }

    int ws[3];
    ws[0] = pss[0]->width;
    ws[2] = pss[1]->width;
    ws[1] = width - ws[2] - ws[0];

    int hs[3];
    hs[0] = pss[0]->height;
    hs[2] = pss[2]->height;
    hs[1] = height - hs[2] - hs[0];

    int dxs[3] = { 0, ws[0], ws[0] + ws[1] };
    int dys[3] = { 0, hs[0], hs[0] + hs[1] };

    for (int i = 0; i < 9; i++)
    {
        int grid_x = i % 3;
        int grid_y = i / 3;

        int draw_x = x + dxs[grid_x];
        int draw_y = y + dys[grid_y];

        bool corner = (grid_x % 2) == 0 && (grid_y % 2) == 0;
        if (corner)
        {
            draw_gump(gump_id_base + i, draw_x, draw_y, hue_id, pick_id);
        }
        else
        {
            draw_gump_tiled(gump_id_base + i, draw_x, draw_y, ws[grid_x], hs[grid_y], hue_id, pick_id);
        }
    }
}

void draw_text(int x, int y, int font_id, std::wstring s, int pick_id)
{
    pixel_storage_t *ps = get_string_ps(font_id, s);
    blit_ps(ps, x, y, gump_draw_order++, 0, pick_id);
}

void draw_paperdoll(gump_t *gump)
{
// <body-id> <paperdoll-gump-id> <offset for this body> <fallback offset>
//400 12  50000 50000 # male human
//401 13  60000 50000 # female human
//605 14  50000 50000 # male elf
//606 15  60000 50000 # female elf
//666 666 50000 50000 # male gargoyle
//667 665 60000 50000 # female gargoyle
    assert(gump->type == GUMPTYPE_PAPERDOLL);
    mobile_t *m = gump->paperdoll.mobile;
    int x = gump->x;
    int y = gump->y;
    int pick_id = pick_gump(gump);

    // frame
    if (m == &player)
    {
        draw_gump(2000, x, y, 0, pick_id);
    }
    else
    {
        draw_gump(2001, x, y, 0, pick_id);
    }
    // TODO: draw buttons

    // name
    draw_text(x + 40, y + 262, 0, *gump->paperdoll.name, pick_id);

    // body
    draw_gump(12, x, y + 20, m->hue_id, pick_id);

    // equipment
    for (int i = 0; i < 32; i++)
    {
        int layer = layer_draw_order[i];
        if (m->equipped_items[layer] != 0)
        {
            item_t *item = m->equipped_items[layer];
            assert(item->space == SPACETYPE_EQUIPPED);
            ml_item_data_entry *item_data = ml_get_item_data(item->item_id);
            draw_gump(50000 + item_data->animation, x, y + 20, item->hue_id, pick_item(item));
        }
    }
}

void draw_container(gump_t *container)
{
    assert(container->type == GUMPTYPE_CONTAINER);
    int x = container->x;
    int y = container->y;
    draw_gump(container->container.gump_id, x, y, 0, pick_gump(container));
    for (std::list<item_t *>::iterator it = container->container.items->begin(); it != container->container.items->end(); ++it)
    {
        item_t *item = *it;

        assert(item->space == SPACETYPE_CONTAINER);

        int item_id = item->item_id;
        int hue_id = item->hue_id;
        int dx = item->loc.container.x;
        int dy = item->loc.container.y;

        draw_screen_item(item_id, x + dx, y + dy, hue_id, pick_item(item));
    }
}

void draw_generic_gump(gump_t *gump)
{
    assert(gump->type == GUMPTYPE_GENERIC);
    int x = gump->x;
    int y = gump->y;

    int pick_id = pick_gump(gump);

    for (std::list<gump_widget_t>::iterator it = gump->generic.widgets->begin(); it != gump->generic.widgets->end(); ++it)
    {
        gump_widget_t &widget = *it;
        if (widget.page == 0 || widget.page == gump->generic.current_page)
        {
            if (widget.type == GUMPWTYPE_PIC)
            {
                draw_gump(widget.pic.gump_id, x + widget.pic.x, y + widget.pic.y, 0, pick_id);
            }
            else if (widget.type == GUMPWTYPE_PICTILED)
            {
                draw_gump_tiled(widget.pictiled.gump_id, x + widget.pictiled.x, y + widget.pictiled.y, widget.pictiled.width, widget.pictiled.height, 0, pick_id);
            }
            else if (widget.type == GUMPWTYPE_RESIZEPIC)
            {
                draw_gump_dynamic(widget.resizepic.gump_id_base, x + widget.resizepic.x, y + widget.resizepic.y, widget.resizepic.width, widget.resizepic.height, 0, pick_id);
            }
            else if (widget.type == GUMPWTYPE_BUTTON)
            {
                draw_gump(widget.button.up_gump_id, x + widget.button.x, y + widget.button.y, 0, pick_gump_widget(gump, &widget));
            }
            else if (widget.type == GUMPWTYPE_ITEM)
            {
                draw_screen_item(widget.item.item_id, x + widget.item.x, y + widget.item.y, widget.item.hue_id, pick_id);
            }
            else if (widget.type == GUMPWTYPE_TEXT)
            {
                draw_text(x + widget.text.x, y + widget.text.y, widget.text.font_id, *widget.text.text, pick_id);
            }
            else
            {
                assert(0 && "unhandled widget type");
            }
        }
    }
}

void draw_gump(gump_t *gump)
{
    switch (gump->type)
    {
        case GUMPTYPE_CONTAINER:
            draw_container(gump);
            break;
        case GUMPTYPE_PAPERDOLL:
            draw_paperdoll(gump);
            break;
        case GUMPTYPE_GENERIC:
            draw_generic_gump(gump);
            break;
        default:
            printf("unknown gump type: %d\n", gump->type);
            assert(0 && "unknown gump type");
            break;
    }
}

void game_pick_up_rejected()
{
    if (dragging.type == DRAGTYPE_ITEM)
    {
        dragging.type = DRAGTYPE_NONE;
    }
}

void game_do_action(uint32_t mob_serial, int action_id, int frame_count, int repeat_count, bool forward, bool do_repeat, int frame_delay)
{
    if (action_id >= 35)
    {
        printf("ignoring animation with action_id %d (because it is >= 35, don't know how to handle)\n", action_id);
    }
    else
    {
        mobile_t *m = game_get_mobile(mob_serial);
        m->anim_start = now;
        m->anim_action_id = action_id;
        m->anim_frame_count = frame_count;
        m->anim_total_frames = frame_count * (do_repeat ? repeat_count : 1);
    }
}

gump_t *game_create_generic_gump(uint32_t gump_serial, uint32_t gump_type_id, int x, int y)
{
    gump_t *gump = (gump_t *)malloc(sizeof(gump_t));
    gump->type = GUMPTYPE_GENERIC;
    gump->x = x;
    gump->y = y;
    gump->generic.serial = gump_serial;
    gump->generic.type_id = gump_type_id;
    gump->generic.current_page = 1;
    gump->generic.widgets = new std::list<gump_widget_t>;

    gump_list.push_back(gump);

    return gump;
}

int find_move_z(int map, int x, int y, int cur_z)
{
    int block_x = x / 8;
    int block_y = y / 8;

    land_block_t *lb    = get_land_block   (map, block_x, block_y);
    statics_block_t *sb = get_statics_block(map, block_x, block_y);
    if (!lb || !sb)
    {
        // land block or statics block not yet loaded
        return -1;
    }

    const int mob_height  = 16;
    const int step_height =  2;

    // first, find the highest steppable surface in [-128, z+step_height]
    int highest_steppable_z = -1;
    {
        // TODO: land_block_z_averaged doesn't check that the land_blocks of all four z's are actually loaded before averaging. FIX THIS!
        int lb_z = land_block_z_averaged(x, y);
        int tile_id = lb->tiles[(x % 8) + (y % 8) * 8].tile_id;
        ml_tile_data_entry *tile_data = ml_get_tile_data(tile_id);
        if ((tile_data->flags & TILEFLAG_IMPASSABLE) == 0)
        {
            highest_steppable_z = lb_z;
        }
    }

    for (int i = 0; i < sb->statics_count; i++)
    {
        statics_block_entry_t *sbe = &sb->statics[i];
        int sx = block_x * 8 + sbe->dx;
        int sy = block_y * 8 + sbe->dy;
        int sz = sbe->z;
        int item_id = sbe->item_id;

        if (sx == x && sy == y)
        {
            ml_item_data_entry *item_data = ml_get_item_data(item_id);
            bool is_bridge = (item_data->flags & TILEFLAG_BRIDGE) != 0;
            int test_height = is_bridge ? item_data->height / 2 : item_data->height;
            int test_z = sz + test_height;
            /*if (is_bridge)
            {
            printf("comparing %d %d %d to player %d %d %d\n", sx, sy, test_z, x, y, cur_z);
            printf("test_z %d, cur_z + step_height %d\n", test_z, cur_z + step_height);
            }*/

            if ((item_data->flags & TILEFLAG_IMPASSABLE) == 0 &&
                (item_data->flags & TILEFLAG_SURFACE   ) != 0 &&
                    test_z <= cur_z + step_height &&
                    (highest_steppable_z == -1 || test_z > highest_steppable_z))
            {
                highest_steppable_z = sz + item_data->height;
            }
        }
    }

    // second, try to find reasons to disqualify the selected z
    // could be:
    // 1. there is something on top blocking it

    if (highest_steppable_z != -1)
    {
        // TODO: land_block_z_averaged doesn't check that the land_blocks of all four z's are actually loaded before averaging. FIX THIS!
        int lb_z = land_block_z_averaged(x, y);
        int tile_id = lb->tiles[(x % 8) + (y % 8) * 8].tile_id;
        ml_tile_data_entry *tile_data = ml_get_tile_data(tile_id);
        if ((tile_data->flags & TILEFLAG_IMPASSABLE) != 0 && lb_z > highest_steppable_z)
        {
            highest_steppable_z = -1;
        }
    }

    for (int i = 0; i < sb->statics_count && highest_steppable_z != -1; i++)
    {
        statics_block_entry_t *sbe = &sb->statics[i];
        int sx = block_x * 8 + sbe->dx;
        int sy = block_y * 8 + sbe->dy;
        int sz = sbe->z;
        int item_id = sbe->item_id;

        if (sx == x && sy == y)
        {
            ml_item_data_entry *item_data = ml_get_item_data(item_id);
            bool is_bridge = (item_data->flags & TILEFLAG_BRIDGE) != 0;
            int test_height = is_bridge ? item_data->height / 2 : item_data->height;
            int test_z = sz + test_height;

            bool blocks = (item_data->flags & TILEFLAG_IMPASSABLE);

            if (blocks &&
                (test_z > highest_steppable_z && sz < highest_steppable_z + mob_height))
            {
                highest_steppable_z = -1;
            }
        }
    }


    return highest_steppable_z;
}

extern std::wstring decode_utf8_cstr(const char *s);

struct
{
    int seq;

    int rd, wr;
    int valids;
    struct
    {
    } entries[4];

    bool empty() { return valids == 0; }
    bool full() { return valids == 4; }
    void reset() { wr = rd = valids = seq = 0; }
    int push()
    {
        assert(!full());
        //entries[wr];
        wr = (wr + 1) % 4;;
        valids += 1;
        int s = seq;
        seq = (seq + 1) % 256;
        return s;
    }
    void pop()
    {
        assert(!empty());
        //entries[rd];
        rd = (rd + 1) % 4;;
        valids -= 1;
    }
} move_seq_queue;

void game_move_rejected(int seq, int x, int y, int z, int dir)
{
    move_seq_queue.reset();
    player.x = x;
    player.y = y;
    player.z = z;
    player.dir = dir;
}

void game_move_ack(int seq, int flags)
{
    move_seq_queue.pop();
    player.flags = flags;
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
 
    // Create our window centered
    main_window = SDL_CreateWindow(":D", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        window_width, window_height, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    if (!main_window)
    {
        sdl_die("Unable to create window");
    }
 
    checkSDLError(__LINE__);
 
    // Create our opengl context and attach it to our window
    main_context = SDL_GL_CreateContext(main_window);
    checkSDLError(__LINE__);

    prg_blit_picking = gfx_upload_program("blit_picking.vert", "blit_picking.frag");
    prg_blit_hue     = gfx_upload_program("blit_hue.vert", "blit_hue.frag");
 
    // 0 = free running
    // 1 = vsync
    SDL_GL_SetSwapInterval(1);

    // accept input
    SDL_StartTextInput();

    long next_ping = ping_frequency;
    long next_move = -1;
    int move_dx;
    int move_dy;
    int move_dir;

    int mouse_x;
    int mouse_y;

    long last_click = -1;
    bool left_down = false;
 
    long start = SDL_GetTicks();

    long next_fps = start + 1000;
    int frames = 0;
    bool running = true;
    int fps = 0;
    while (running)
    {
        // first some housekeeping...

        now = SDL_GetTicks() - start;

        // FPS counter
        if (now > next_fps)
        {
            char title[64];
            sprintf(title, ":D - fps: %d", frames);
            fps = frames;
            SDL_SetWindowTitle(main_window, title);

            while (now > next_fps)
            {
                next_fps += 1000;
            }
            frames = 0;
        }
        frames += 1;

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

        // because GL counts y starting from bottom of screen
        int inverted_mouse_y = window_height-mouse_y-1;

        draw_ceiling = find_ceiling(1, player.x, player.y, player.z);
        draw_roofs = true;
        if (has_roof(1, player.x, player.y))
        {
            draw_roofs = false;
        }

        // reset pick ids
        next_pick_id = 0;
        picking_enabled = true;
        glScissor(mouse_x, inverted_mouse_y, 1, 1);
        glEnable(GL_SCISSOR_TEST);
        draw_world();
        gfx_flush();
        glClear(GL_DEPTH_BUFFER_BIT);
        // draw gumps
        {
            std::list<gump_t *>::iterator it;
            for (it = gump_list.begin(); it != gump_list.end(); ++it)
            {
                gump_t *gump = *it;
                draw_gump(gump);
            }
        }
        gfx_flush();
        //draw_paperdoll(&player, 0, 0, pick_gump());
        glDisable(GL_SCISSOR_TEST);
        glScissor(0, 0, window_width, window_height);

        // do picking
        pick_target_t *pick_target = NULL;
        {
            uint8_t data[3];
            glReadPixels(mouse_x, inverted_mouse_y, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, &data);
            int pick_id0 = data[0];
            int pick_id1 = data[1];
            int pick_id2 = data[2];
            int pick_id = (pick_id0 << 16) | (pick_id1 << 8) | pick_id2;

            if (pick_id >= 0 && pick_id < sizeof(pick_slots)/sizeof(pick_slots[0]))
            {
                pick_target = &pick_slots[pick_id];
                //printf("picking id %d\n", id);
                int type = pick_slots[pick_id].type;
                switch (type)
                {
                    case TYPE_LAND:
                        //printf("land (%d, %d)\n", pick_slots[id].land.x, pick_slots[id].land.y);
                        break;
                    case TYPE_STATIC:
                        //printf("static\n");
                        break;
                    case TYPE_ITEM:
                        //printf("item %x\n", pick_slots[pick_id].item.item->serial);
                        break;
                    case TYPE_MOBILE:
                        //printf("mobile %x\n", pick_slots[pick_id].mobile.mobile->serial);
                        break;
                    case TYPE_GUMP:
                        //printf("gump\n");
                        break;
                    case TYPE_GUMP_WIDGET:
                        //printf("gump\n");
                        break;
                    default:
                        printf("unknown item picked :O\n");
                        break;
                }
            }
            else
            {
                pick_target = NULL;
            }
        }
        


        // draw distinct background
        glClearColor(1.0, 0.0, 1.0, 1.0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glColor4f(1.0f, 1.0f, 0.0f, 1.0f);
        glBegin(GL_TRIANGLES);
        glVertex2f(-1.0f, -1.0f);
        glVertex2f( 1.0f, -1.0f);
        glVertex2f( 0.0f,  1.0f);
        glEnd();


        picking_enabled = false;
        draw_world();
        gfx_flush();

        glClear(GL_DEPTH_BUFFER_BIT);
        // draw gumps
        {
            std::list<gump_t *>::iterator it;
            for (it = gump_list.begin(); it != gump_list.end(); ++it)
            {
                gump_t *gump = *it;
                draw_gump(gump);
            }
        }

        // draw dragged item
        if (dragging.type == DRAGTYPE_ITEM)
        {
            int item_id = dragging.item.item_id;
            int hue_id  = dragging.item.hue_id;

            draw_screen_item(item_id, mouse_x, mouse_y, hue_id, -1);
        }

        // draw chat string
        {
            draw_text(0, 0, 0, chat_str + L"_", -1);
        }

        gfx_flush();


        // the rest of the logic is done after drawing, so that it can make use of the picking done in drawing stage

        // event polling
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            gump_draw_order = 0;

            // window closed
            if (e.type == SDL_QUIT)
            {
                running = false;
            }
            if (e.type == SDL_KEYDOWN)
            {
                if (e.key.keysym.sym == SDLK_ESCAPE)
                {
                    running = false;
                }
                if (e.key.keysym.sym == SDLK_BACKSPACE)
                {
                    if (chat_str.length() > 0)
                    {
                        chat_str = chat_str.substr(0, chat_str.length()-1);
                    }
                }
                if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER)
                {
                    if (chat_str.length() > 0)
                    {
                        net_send_speech(chat_str);
                        chat_str.clear();
                    }
                }
            }
            if (e.type == SDL_TEXTINPUT)
            {
                std::wstring s = decode_utf8_cstr(e.text.text);
                chat_str += s;
            }
            if (e.type == SDL_MOUSEBUTTONDOWN)
            {
                if (e.button.button == SDL_BUTTON_LEFT)
                {
                    left_down = true;
                    // gah this handling code is quite messy...
                    if (last_click != -1 && now - last_click < 200)
                    {
                        // double click!
                        bool handled = false;
                        if (pick_target != NULL)
                        {
                            int type = pick_target->type;
                            switch (type)
                            {
                                case TYPE_ITEM:
                                    printf("item %x\n", pick_target->item.item->serial);
                                    net_send_use(pick_target->item.item->serial);
                                    handled = true;
                                    break;
                                case TYPE_MOBILE:
                                    printf("mobile %x\n", pick_target->mobile.mobile->serial);
                                    net_send_use(pick_target->mobile.mobile->serial);
                                    handled = true;
                                    break;
                            }
                        }
                        if (handled)
                        {
                            last_click = -1;
                        }
                    }
                    else
                    {
                        // sometimes first click is handled instantly
                        if (pick_target != NULL)
                        {
                            int type = pick_target->type;
                            switch (type)
                            {
                                case TYPE_GUMP:
                                    if (dragging.type == DRAGTYPE_NONE)
                                    {
                                        gump_t *gump = pick_target->gump.gump;
                                        dragging.type = DRAGTYPE_GUMP;
                                        dragging.gump.gump = gump;
                                        dragging.gump.handle_x = mouse_x - gump->x;
                                        dragging.gump.handle_y = mouse_y - gump->y;

                                        // move gump to top
                                        std::list<gump_t *>::iterator it = std::find(gump_list.begin(), gump_list.end(), gump);
                                        assert(it != gump_list.end());
                                        gump_list.erase(it);
                                        gump_list.push_back(gump);
                                    }
                                    printf("gump\n");
                                    break;
                                case TYPE_MOBILE:
                                    printf("mobile at (%d, %d, %d)\n",
                                            pick_target->mobile.mobile->x,
                                            pick_target->mobile.mobile->y,
                                            pick_target->mobile.mobile->z);
                                    break;
                                case TYPE_LAND:
                                    printf("land at (%d, %d, %d), tile_id: %d, surface: %d, impassable: %d, stairs: %d\n",
                                            pick_target->land.x,
                                            pick_target->land.y,
                                            pick_target->land.z,
                                            pick_target->land.tile_id,
                                            (ml_get_tile_data(pick_target->land.tile_id)->flags & TILEFLAG_SURFACE   ) != 0,
                                            (ml_get_tile_data(pick_target->land.tile_id)->flags & TILEFLAG_IMPASSABLE) != 0,
                                            ((ml_get_tile_data(pick_target->land.tile_id)->flags & TILEFLAG_STAIRS_A ) != 0 ||
                                             (ml_get_tile_data(pick_target->land.tile_id)->flags & TILEFLAG_STAIRS_B ) != 0));
                                    break;
                                case TYPE_STATIC:
                                {
                                    bool check_a = (ml_get_item_data(pick_target->static_item.item_id)->flags & TILEFLAG_STAIRS_A) != 0;
                                    bool check_b = (ml_get_item_data(pick_target->static_item.item_id)->flags & TILEFLAG_STAIRS_B) != 0;
                                    printf("static at (%d, %d, %d), height: %d, item_id: %d, surface: %d, impassable: %d, stairs: %d flags: %lx, ca: %d, cb: %d %lx\n",
                                            pick_target->static_item.x,
                                            pick_target->static_item.y,
                                            pick_target->static_item.z,
                                            ml_get_item_data(pick_target->static_item.item_id)->height,
                                            pick_target->static_item.item_id,
                                            (ml_get_item_data(pick_target->static_item.item_id)->flags & TILEFLAG_SURFACE   ) != 0,
                                            (ml_get_item_data(pick_target->static_item.item_id)->flags & TILEFLAG_IMPASSABLE) != 0,
                                            ((ml_get_item_data(pick_target->static_item.item_id)->flags & TILEFLAG_STAIRS_A ) != 0 ||
                                             (ml_get_item_data(pick_target->static_item.item_id)->flags & TILEFLAG_STAIRS_B ) != 0),
                                            (long unsigned int)ml_get_item_data(pick_target->static_item.item_id)->flags,
                                            check_a, check_b,
                                            (long unsigned int)(ml_get_item_data(pick_target->static_item.item_id)->flags & TILEFLAG_STAIRS_B));
                                    break;
                                }
                                case TYPE_ITEM:
                                {
                                    item_t *item = pick_target->item.item;
                                    bool check_a = (ml_get_item_data(item->item_id)->flags & TILEFLAG_STAIRS_A) != 0;
                                    bool check_b = (ml_get_item_data(item->item_id)->flags & TILEFLAG_STAIRS_B) != 0;
                                    printf("item at (%d, %d, %d), item_id: %d, surface: %d, impassable: %d, stairs: %d flags: %lx, ca: %d, cb: %d\n",
                                            item->loc.world.x,
                                            item->loc.world.y,
                                            item->loc.world.z,
                                            item->item_id,
                                            (ml_get_item_data(item->item_id)->flags & TILEFLAG_SURFACE   ) != 0,
                                            (ml_get_item_data(item->item_id)->flags & TILEFLAG_IMPASSABLE) != 0,
                                            ((ml_get_item_data(item->item_id)->flags & TILEFLAG_STAIRS_A ) != 0 ||
                                             (ml_get_item_data(item->item_id)->flags & TILEFLAG_STAIRS_B ) != 0),
                                            (long unsigned int)ml_get_item_data(item->item_id)->flags,
                                            check_a, check_b);
                                    break;
                                }
                                case TYPE_GUMP_WIDGET:
                                {
                                    gump_t *gump = pick_target->gump_widget.gump;
                                    gump_widget_t *widget = pick_target->gump_widget.widget;
                                    assert(gump->type == GUMPTYPE_GENERIC);
                                    assert(widget->type == GUMPWTYPE_BUTTON);
                
                                    // handle button click!
                                    if (widget->button.type == 0)
                                    {
                                        // change page
                                        gump->generic.current_page = widget->button.param;
                                    }
                                    else
                                    {
                                        // send response
                                        net_send_gump_response(gump->generic.serial, gump->generic.type_id, widget->button.button_id);
                                        game_delete_gump(gump);
                                        pick_target = NULL;
                                    }

                                    break;
                                }
                            }
                        }
                        last_click = now;
                    }
                }
                if (e.button.button == SDL_BUTTON_RIGHT)
                {
                    if (pick_target != NULL && pick_target->type == TYPE_GUMP)
                    {
                        gump_t *gump = pick_target->gump_widget.gump;

                        if (gump->type == GUMPTYPE_GENERIC)
                        {
                            net_send_gump_response(gump->generic.serial, gump->generic.type_id, 0);
                            game_delete_gump(gump);
                        }
                        else if (gump->type == GUMPTYPE_PAPERDOLL)
                        {
                            game_delete_gump(gump);
                        }
                        else
                        {
                            printf("TODO: figure out how to close this kind of gump\n");
                        }

                        // let go of any gumps.
                        if (dragging.type == DRAGTYPE_GUMP)
                        {
                            dragging.type = DRAGTYPE_NONE;
                        }
                    }
                    else
                    {
                        next_move = now;
                    }
                }
            }
            if (e.type == SDL_MOUSEBUTTONUP)
            {
                if (e.button.button == SDL_BUTTON_LEFT)
                {
                    left_down = false;
                    if (dragging.type == DRAGTYPE_ITEM)
                    {
                        if (pick_target != NULL)
                        {
                            if (pick_target->type == TYPE_GUMP)
                            {
                                gump_t *gump = pick_target->gump.gump;
                                if (gump->type == GUMPTYPE_CONTAINER)
                                {
                                    int offset_x = mouse_x - gump->x;
                                    int offset_y = mouse_y - gump->y;
                                    net_send_drop_item(dragging.item.serial, offset_x, offset_y, 0, gump->container.item->serial);
                                    dragging.type = DRAGTYPE_NONE;
                                }
                                else if (gump->type == GUMPTYPE_PAPERDOLL)
                                {
                                    // TODO: only attempt to equip when dropping on player's own paperdoll
                                    ml_item_data_entry *item_data = ml_get_item_data(dragging.item.item_id);
                                    if ((item_data->flags & TILEFLAG_WEARABLE) != 0)
                                    {
                                        net_send_equip_item(dragging.item.serial, item_data->layer, gump->paperdoll.mobile->serial);
                                        dragging.type = DRAGTYPE_NONE;
                                    }
                                }
                                else
                                {
                                    printf("TODO: figure out how to drop into this gump.\n");
                                }
                            }
                            else if (pick_target->type == TYPE_ITEM)
                            {
                                item_t *target = pick_target->item.item;
                                ml_item_data_entry *item_data = ml_get_item_data(target->item_id);
                                // TODO: also attempt drop if items are stackable and of same type
                                if ((item_data->flags & TILEFLAG_CONTAINER) != 0)
                                {
                                    net_send_drop_item(dragging.item.serial, 0, 0, 0, target->serial);
                                    dragging.type = DRAGTYPE_NONE;
                                }
                            }
                            else if (pick_target->type == TYPE_MOBILE)
                            {
                                mobile_t *target = pick_target->mobile.mobile;
                                net_send_drop_item(dragging.item.serial, 0, 0, 0, target->serial);
                                dragging.type = DRAGTYPE_NONE;
                            }
                            else if (pick_target->type == TYPE_LAND)
                            {
                                printf("TODO: figure out now how to drop onto a land tile.\n");
                            }
                            else if (pick_target->type == TYPE_STATIC)
                            {
                                printf("TODO: figure out now how to drop onto a static item.\n");
                            }
                        }
                        last_click = -1;
                    }
                    else if (dragging.type == DRAGTYPE_GUMP)
                    {
                        last_click = -1;
                        dragging.type = DRAGTYPE_NONE;
                    }
                }
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

                // animations have a different enumeration, need to remap on draw

                int dxs[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
                int dys[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };

                move_dx = dxs[dir];
                move_dy = dys[dir];

                mouse_x = x;
                mouse_y = y;
            }
        }

        // double-click timed out... handle as single-click
        if (last_click != -1 && now - last_click >= 200)
        {
            last_click = -1;
            if (pick_target != NULL)
            {
                int type = pick_target->type;
                switch (type)
                {
                    case TYPE_LAND:
                        //printf("land (%d, %d)\n", pick_slots[id].land.x, pick_slots[id].land.y);
                        break;
                    case TYPE_STATIC:
                        //printf("static\n");
                        break;
                    case TYPE_ITEM:
                        if (left_down)
                        {
                            if (dragging.type == DRAGTYPE_NONE)
                            {
                                printf("dragging item %x\n", pick_target->item.item->serial);
                                net_send_pick_up_item(pick_target->item.item->serial, 1);
                                item_t *item = pick_target->item.item;
                                dragging.type = DRAGTYPE_ITEM;
                                dragging.item.serial = item->serial;
                                dragging.item.item_id = item->item_id;
                                dragging.item.hue_id = item->hue_id;
                            }
                        }
                        else
                        {
                            printf("inspecting item %x\n", pick_target->item.item->serial);
                            net_send_inspect(pick_target->item.item->serial);
                        }
                        break;
                    case TYPE_MOBILE:
                        printf("inspecting mobile %x\n", pick_target->mobile.mobile->serial);
                        net_send_inspect(pick_target->mobile.mobile->serial);
                        break;
                    case TYPE_GUMP:
                        //printf("inspecting gump... O_o\n");
                        break;
                    case TYPE_GUMP_WIDGET:
                        //printf("gump\n");
                        break;
                    default:
                        printf("unknown item picked :O\n");
                        break;
                }
            }
        }

        // dragging gump.. set its x,y
        if (dragging.type == DRAGTYPE_GUMP)
        {
            gump_t *gump = dragging.gump.gump;
            gump->x = mouse_x - dragging.gump.handle_x;
            gump->y = mouse_y - dragging.gump.handle_y;
        }

        if (next_move != -1 && now >= next_move)
        {
            if (!move_seq_queue.full())
            {
                if (player.dir == move_dir)
                {
                    // move in that dir

                    int new_x = player.x + move_dx;
                    int new_y = player.y + move_dy;

                    int new_z = find_move_z(1, new_x, new_y, player.z);
                    if (new_z != -1)
                    {
                        player.x = new_x;
                        player.y = new_y;
                        player.z = new_z;
                        net_send_move(player.dir, move_seq_queue.push());
                        player.last_movement = next_move;
                    }
                }
                else
                {
                    // just change direction we're facing

                    player.last_dir = player.dir;
                    player.dir = move_dir;
                    net_send_move(player.dir, move_seq_queue.push());
                    player.last_movement = next_move;
                }
            }
            next_move += 100;
        }

        if (now >= next_ping)
        {
            net_send_ping();
            next_ping += ping_frequency;
        }

        // network...
        net_poll();

        //
        SDL_GL_SwapWindow(main_window);

        //printf("fps: %d, per frame: commands: %d, use_program: %d, bind_tex0: %d, bind_tex1: %d, glbegin: %d, glend: %d\n", fps, cnt_render_commands, cnt_use_program, cnt_bind_tex0, cnt_bind_tex1, cnt_glbegin, cnt_glend);
        // reset counters
        cnt_render_commands = 0;
        cnt_use_program = 0;
        cnt_bind_tex0 = 0;
        cnt_bind_tex1 = 0;
        cnt_glbegin = 0;
        cnt_glend = 0;
    }

    // disconnect
    net_shutdown();

    // shut down work thread
    mlt_stop_worker_thread();

    // drain any remaining jobs
    while (mlt_still_working())
    {
        printf("draining...\n");
        mlt_process_callbacks();
        SDL_Delay(100);
    }

    // clean up world objects
    while (gump_list.size() > 0)
    {
        std::list<gump_t *>::iterator it = gump_list.begin();
        gump_t *gump = *it;
        game_delete_gump(gump);
        //gump_list.erase(it);
    }
    while (multis.size() > 0)
    {
        std::map<int, multi_t *>::iterator it = multis.begin();
        multi_t *multi = it->second;
        // no special delete function
        delete multi;
        multis.erase(it);
    }
    while (items.size() > 0)
    {
        std::map<int, item_t *>::iterator it = items.begin();
        item_t *item = it->second;
        game_delete_item(item);
        //items.erase(it);
    }
    while (mobiles.size() > 0)
    {
        std::map<int, mobile_t *>::iterator it = mobiles.begin();
        mobile_t *mobile = it->second;
        game_delete_mobile(mobile);
        //mobiles.erase(it);
    }

    SDL_GL_DeleteContext(main_context);
    SDL_DestroyWindow(main_window);
    SDL_Quit();
 
    return 0;
}

