
#include <stdint.h>
#include <list>

struct mobile_t
{
    uint32_t serial;
    int body_id;
    int x, y, z;
    int dir;
    int hue_id;
    int noto;
    struct gump_t *paperdoll_gump;
    struct item_t *equipped_items[32];

    // animation related stuff
    int last_dir;
    long last_movement;
    int action_id;
};

// items can live in several places:
// in containers
// worn by mobiles (can we model this as a container?)
// on the ground
// being dragged by player
// they need to be deleted properly regardless of which of these places they are
const int SPACETYPE_WORLD = 0;
const int SPACETYPE_CONTAINER = 1;
const int SPACETYPE_EQUIPPED = 2;
const int SPACETYPE_DRAG = 3;
struct item_t
{
    uint32_t serial;
    int item_id;
    int hue_id;
    // items that are containers may have an open gump
    struct gump_t *container_gump;
    int space;
    union
    {
        struct
        {
            int x, y, z;
        } world;
        struct
        {
            mobile_t *mobile;
        } equipped;
        struct
        {
            gump_t *container;
            int x, y;
        } container;
    } loc;
};

// gumps can be plain gumps, containers, paperdolls (and some more?)
// figure out a way to model this...
const int GUMPTYPE_CONTAINER = 0;
const int GUMPTYPE_PAPERDOLL = 1;
struct gump_t
{
    int type;
    int x, y;
    union
    {
        struct
        {
            int gump_id;
            item_t *item;
            std::list<item_t *> *items;
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
void game_pick_up_rejected();

