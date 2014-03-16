
#include <stdint.h>

struct mobile_t
{
    uint32_t id;
    int body_id;
    int x, y, z;
    int dir;
    int hue_id;
    int noto;
    int equipped_item_id[32];
    int equipped_hue_id[32];
};

struct item_t
{
    uint32_t id;
    int item_id;
    int x, y, z;
    int hue_id;
};


void game_set_player_info(uint32_t id, int body_id, int x, int y, int z, int hue_id, int dir);
void game_set_player_pos(int x, int y, int z, int dir);
void game_equip(mobile_t *m, int id, int item_id, int layer, int hue);
item_t *game_get_item(uint32_t id);
mobile_t *game_get_mobile(uint32_t id);
void game_delete_object(uint32_t id);

