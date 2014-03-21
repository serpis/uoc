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

#endif

