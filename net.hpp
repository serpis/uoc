#ifndef _NET_HPP
#define _NET_HPP

void net_init();
void net_connect();
void net_poll();

void net_send_ping();
void net_send_move(int dir);
void net_send_use(uint32_t serial);
void net_send_inspect(uint32_t serial);
void net_send_pick_up_item(uint32_t serial, int amount);
void net_send_drop_item(uint32_t item_serial, int x, int y, int z, uint32_t cont_serial);
void net_send_equip_item(uint32_t item_serial, int layer, uint32_t mob_serial);
void net_send_gump_response(uint32_t serial, uint32_t gump_type_id, int response_id);
void net_send_speech(std::wstring str);

#endif

