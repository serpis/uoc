#ifndef _MULLIB_HPP
#define _MULLIB_HPP

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
    int quality;
    int quantity;
    int animation;
    int height;
    char name[21];
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
    struct {
        int tile_id;
        int dx, dy, z;
    } statics[];
};

void ml_init();


// these return instantly, so there are no asynchronous versions of them
// the data returned should NOT be freed!
ml_tile_data_entry *ml_get_tile_data(int tile_id);
ml_item_data_entry *ml_get_item_data(int item_id);

// it is the caller's responsibility to free the memory returned from these
ml_anim *ml_read_anim(int body_id, int action, int direction);
ml_art *ml_read_land_art(int land_id);
ml_art *ml_read_static_art(int static_id);
ml_land_block *ml_read_land_block(int map, int block_x, int block_y);
ml_statics_block *ml_read_statics_block(int map, int block_x, int block_y);



// threaded version of the lib. resource loading runs on separate thread.
// responses will then be dispached on the thread calling mlt_callbacks()
void mlt_init();

void mlt_read_anim(int body_id, int action, int direction, void (*callback)(int body_id, int action, int direction, ml_anim *a));
void mlt_read_land_art(int land_id, void (*callback)(int land_id, ml_art *l));
void mlt_read_static_art(int static_id, void (*callback)(int static_id, ml_art *s));
void mlt_read_land_block(int map, int block_x, int block_y, void (*callback)(int map, int block_x, int block_y, ml_land_block *lb));
void mlt_read_statics_block(int map, int block_x, int block_y, void (*callback)(int map, int block_x, int block_y, ml_statics_block *sb));

// run this on the thread where you want the responses
void mlt_process_callbacks();

#endif

