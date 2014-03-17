
#include <stdint.h>

struct mobile_t
{
    uint32_t serial;
    int body_id;
    int x, y, z;
    int dir;
    int hue_id;
    int noto;
    int equipped_item_id[32];
    int equipped_hue_id[32];
};

// items can live in several places:
// in containers
// worn by mobiles (can we model this as a container?)
// on the ground
// being dragged by player
// they need to be deleted properly regardless of which of these places they are
struct item_t
{
    uint32_t serial;
    int item_id;
    int x, y, z;
    int hue_id;
};

// gumps can be plain gumps, containers, paperdolls (and some more?)
// figure out a way to model this...
const int GUMPTYPE_CONTAINER = 0;
const int GUMPTYPE_PAPERDOLL = 1;
struct gump_t
{
    int type;
    union
    {
        struct
        {
            int gump_id;
            int x, y;
            int item_count;
            struct
            {
                uint32_t serial;
                int x, y;
                int item_id;
                int hue_id;
            } items[256];
        } container;
        struct
        {
            mobile_t *mobile;
        } paperdoll;
    };
};

void game_set_player_info(uint32_t serial, int body_id, int x, int y, int z, int hue_id, int dir);
void game_set_player_pos(int x, int y, int z, int dir);
void game_equip(mobile_t *m, uint32_t serial, int item_id, int layer, int hue);
item_t *game_get_item(uint32_t serial);
mobile_t *game_get_mobile(uint32_t serial);
mobile_t *game_create_mobile(uint32_t serial);
void game_delete_object(uint32_t serial);
void game_show_container(uint32_t item_serial, int gump_id);
void game_show_paperdoll(mobile_t *m);
gump_t *game_get_container(uint32_t item_serial);

