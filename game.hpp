
#include <stdint.h>
#include <list>

const int MOBFLAG_WARMODE = 0x40;
const int MOBFLAG_HIDDEN  = 0x80;
struct mobile_t
{
    uint32_t serial;
    int body_id;
    int x, y, z;
    int dir;
    int hue_id;
    int noto;
    int flags;
    struct gump_t *paperdoll_gump;
    struct item_t *equipped_items[32];

    // animation related stuff
    int last_dir;
    long last_movement;

    long anim_start;
    int anim_action_id;
    int anim_frame_count;
    int anim_total_frames;
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

const int GUMPWTYPE_PIC       = 0;
const int GUMPWTYPE_PICTILED  = 1;
const int GUMPWTYPE_RESIZEPIC = 2;
const int GUMPWTYPE_BUTTON    = 3;
const int GUMPWTYPE_ITEM      = 4;
const int GUMPWTYPE_TEXT      = 5;
struct gump_widget_t
{
    int page;
    int type;
    union
    {
        struct
        {
            int x, y;
            int gump_id;
        } pic;
        struct
        {
            int x, y;
            int width, height;
            int gump_id;
        } pictiled;
        struct
        {
            int x, y;
            int width, height;
            int gump_id_base;
        } resizepic;
        struct
        {
            int x, y;
            int up_gump_id, down_gump_id;
            int type;
            int param;
            int button_id;
        } button;
        struct
        {
            int x, y;
            int item_id;
            int hue_id;
        } item;
        struct
        {
            int x, y;
            int font_id;
            std::wstring *text;
        } text;
    };
};

// gumps can be plain gumps, containers, paperdolls (and some more?)
// is this a good way to model that?
const int GUMPTYPE_CONTAINER = 0;
const int GUMPTYPE_PAPERDOLL = 1;
const int GUMPTYPE_GENERIC   = 2;
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
            std::wstring *name;
        } paperdoll;
        struct
        {
            uint32_t serial;
            uint32_t type_id;
            int current_page;
            std::list<gump_widget_t> *widgets;
        } generic;
    };
};

void game_set_player_serial(uint32_t serial);
uint32_t game_get_player_serial();
void game_equip(mobile_t *m, uint32_t serial, int item_id, int layer, int hue);
item_t *game_get_item(uint32_t serial);
mobile_t *game_get_mobile(uint32_t serial);
mobile_t *game_create_mobile(uint32_t serial);
void game_delete_object(uint32_t serial);
void game_show_container(uint32_t item_serial, int gump_id);
void game_show_paperdoll(mobile_t *m, std::wstring name);
gump_t *game_create_generic_gump(uint32_t gump_serial, uint32_t gump_type_id, int x, int y);
gump_t *game_get_container(uint32_t item_serial);
void game_pick_up_rejected();
void game_do_action(uint32_t mob_serial, int action_id, int frame_count, int repeat_count, bool forward, bool do_repeat, int frame_delay);

