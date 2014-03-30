#ifndef _MULLIB_HPP
#define _MULLIB_HPP

#include <string>

const uint64_t TILEFLAG_IMPASSABLE = 0x00000040;
const uint64_t TILEFLAG_SURFACE    = 0x00000200;
const uint64_t TILEFLAG_BRIDGE     = 0x00000400;
const uint64_t TILEFLAG_FOLIAGE    = 0x00020000;
const uint64_t TILEFLAG_CONTAINER  = 0x00200000;
const uint64_t TILEFLAG_WEARABLE   = 0x00400000;
const uint64_t TILEFLAG_ROOF       = 0x10000000;
const uint64_t TILEFLAG_DOOR       = 0x20000000;
const uint64_t TILEFLAG_STAIRS_A   = 0x40000000;
const uint64_t TILEFLAG_STAIRS_B   = 0x80000000;

struct ml_tile_data_entry
{
    uint64_t flags;
    int texture;
    char name[21];
};

struct ml_item_data_entry
{
    uint64_t flags;
    int weight;
    union
    {
        int quality;
        int layer;
    };
    int quantity;
    int animation;
    int height;
    char name[21];
};

struct ml_hue
{
    uint16_t colors[32];
    uint16_t start_color;
    uint16_t end_color;
    char name[21];
};

struct ml_cliloc_entry
{
    int id;
    const char *s;
};

struct ml_index
{
    int entry_count;
    struct {
        int offset;
        int length;
        int extra;
    } entries[];
};

struct ml_anim
{
    int frame_count;
    struct {
        int center_x;
        int center_y;
        int width;
        int height;
        uint16_t *data;
    } frames[];
};

struct ml_art
{
    int width;
    int height;
    uint16_t data[];
};

struct ml_gump
{
    int width;
    int height;
    uint16_t data[];
};

struct ml_multi
{
    int item_count;
    struct
    {
        int item_id;
        int x, y, z;
        bool visible;
    } items[];
};

struct ml_land_block
{
    struct {
        int tile_id;
        int z;
    } tiles[8*8];
};

struct ml_statics_block
{
    int statics_count;
    int roof_heights[8*8];
    struct {
        int tile_id;
        int dx, dy, z;
    } statics[];
};

struct ml_font_metadata
{
    struct
    {
        int8_t kerning;
        int8_t baseline;
        int8_t width;
        int8_t height;
    } chars[0x10000];
};

struct ml_font
{
    struct
    {
        int offset_x;
        int offset_y;
        int width;
        int height;
        int map_start_x;
        int map_start_y;
    } chars[0x10000];
    int map_width;
    int map_height;
    uint16_t map_data[];
};

void ml_init();

void ml_get_font_string_dimensions(int font_id, std::wstring s, int *width, int *height);

// these return instantly, so there are no asynchronous versions of them
// the data returned is owned by the library and so should NOT be freed!
ml_tile_data_entry *ml_get_tile_data(int tile_id);
ml_item_data_entry *ml_get_item_data(int item_id);
ml_hue             *ml_get_hue(int hue_id);
const char         *ml_get_cliloc(int cliloc_id);
ml_font_metadata   *ml_get_unicode_font_metadata(int font_id);

// it is the caller's responsibility to free the memory returned from these
ml_anim *ml_read_anim(int body_id, int action, int direction);
ml_art *ml_read_land_art(int land_id);
ml_art *ml_read_static_art(int item_id);
ml_gump *ml_read_gump(int gump_id);
ml_multi *ml_read_multi(int multi_id);
ml_land_block *ml_read_land_block(int map, int block_x, int block_y);
ml_statics_block *ml_read_statics_block(int map, int block_x, int block_y);
ml_art *ml_render_string(int font_id, std::wstring s);



// threaded version of the lib. resource loading runs on separate thread.
// responses will then be dispached on the thread calling mlt_callbacks()
void mlt_init();

void mlt_read_anim(int body_id, int action, int direction, void (*callback)(int body_id, int action, int direction, ml_anim *a));
void mlt_read_land_art(int land_id, void (*callback)(int land_id, ml_art *l));
void mlt_read_static_art(int item_id, void (*callback)(int item_id, ml_art *s));
void mlt_read_gump(int gump_id, void (*callback)(int gump_id, ml_gump *g));
void mlt_read_land_block(int map, int block_x, int block_y, void (*callback)(int map, int block_x, int block_y, ml_land_block *lb));
void mlt_read_statics_block(int map, int block_x, int block_y, void (*callback)(int map, int block_x, int block_y, ml_statics_block *sb));

// run this on the thread where you want the responses
void mlt_process_callbacks();

#endif

