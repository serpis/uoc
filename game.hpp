
#include <stdint.h>

struct mobile_t
{
    uint32_t id;
    int body_id;
    int x, y, z;
    int dir;
    int hue;
    int noto;
    uint32_t equipped_item_id[32];
};

struct item_t
{
    uint32_t id;
    int item_id;
    int x, y, z;
};


void game_set_player_info(uint32_t id, int body_id, int x, int y, int z, int dir);
void game_set_player_pos(int x, int y, int z, int dir);
void game_add_item(uint32_t id, int item_id, int x, int y, int z);
void game_equip(mobile_t *m, int id, int item_id, int layer, int hue);
mobile_t *game_get_mobile(uint32_t id);
