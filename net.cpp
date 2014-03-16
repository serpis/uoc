#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cstring>

#include <stdint.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>

#include "serialize.hpp"
#include "game.hpp"

bool enable_compression = false;

int compress(char *dst, const char *dst_end, const char *src, const char *src_end);

static void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int net_connect(const char *address, int port)
{
    int sockfd;  
    struct addrinfo hints, *servinfo, *p;
    int rv;
    char s[INET6_ADDRSTRLEN];

    char port_str[6];
    assert(port >= 0 && port < 65536);
    sprintf(port_str, "%d", port);

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(address, port_str, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
    }

    // loop through all the results and connect to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("client: socket");
            continue;
        }

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("client: connect");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "client: failed to connect\n");
        return -1;
    }

    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
            s, sizeof s);
    printf("client: connecting to %s\n", s);

    freeaddrinfo(servinfo); // all done with this structure

    return sockfd;
}

int sockfd;

void send_packet(const char *p, const char *end)
{
    char buf[1024];
    char *bp = buf;
    char *bend = bp + sizeof(buf);

    if (enable_compression)
    {
        int compressed_bytes = compress(bp, bend, p, end);
        p = bp;
        end = bp + compressed_bytes;
    }

    /*printf("sending packet ");
    for (int i = 0; i < end-p; i++)
    {
        printf("0x%02x ", (int)(uint8_t)p[i]);
    }
    printf("\n");*/
    while (p < end)
    {
        int sent_bytes = send(sockfd, p, end-p, 0);
        //printf("sent %d bytes\n", sent_bytes);
        assert(sent_bytes > 0);
        p += sent_bytes;
    }
}

void send_prelogin(uint32_t seed)
{
    char data[4];
    char *p = data;
    char *end = p + sizeof(data);
    write_uint32_be(&p, end, seed);
    send_packet(data, end);
}

void send_account_login(const char *username, const char *password)
{
    char data[62];
    memset(data, 0, sizeof(data));
    char *p = data;
    char *end = p + sizeof(data);
    write_uint8(&p, end, 0x80);
    write_ascii_fixed(&p, end, username, 30);
    write_ascii_fixed(&p, end, password, 30);
    write_uint8(&p, end, 0x5d);
    send_packet(data, end);
}

void send_game_server_login(const char *username, const char *password, uint32_t key)
{
    char data[65];
    memset(data, 0, sizeof(data));
    char *p = data;
    char *end = p + sizeof(data);
    write_uint8(&p, end, 0x91);
    write_uint32_be(&p, end, key);
    write_ascii_fixed(&p, end, username, 30);
    write_ascii_fixed(&p, end, password, 30);
    send_packet(data, end);
}

void send_select_server(int server_id)
{
    char data[3];
    memset(data, 0, sizeof(data));
    char *p = data;
    char *end = p + sizeof(data);
    write_uint8(&p, end, 0xa0);
    write_uint16_be(&p, end, server_id);
    send_packet(data, end);
}

void send_create_character()
{
    char data[104];
    char *p = data;
    char *end = p + sizeof(data);
    write_uint8(&p, end, 0x00);
    write_uint32_be(&p, end, 0xededededu);
    write_uint32_be(&p, end, 0xffffffffu);
    write_uint8(&p, end, 0);
    write_ascii_fixed(&p, end, "dummy", 30);
    write_ascii_fixed(&p, end, "", 30);
    write_uint8(&p, end, 0); // 0=male, 1=female, 2=.... new classes
    write_uint8(&p, end, 45); // str 
    write_uint8(&p, end, 10); // dex
    write_uint8(&p, end, 10); // int
    write_uint8(&p, end, 10); // skill1
    write_uint8(&p, end, 30); // skill1 value
    write_uint8(&p, end, 11); // skill2
    write_uint8(&p, end, 50); // skill2 value
    write_uint8(&p, end, 12); // skill3
    write_uint8(&p, end, 20); // skill3 value
    write_uint16_be(&p, end, 0x422); // skin color
    write_uint16_be(&p, end, 0x203b); // hair style
    write_uint16_be(&p, end, 0x44e); // hair color
    write_uint16_be(&p, end, 0); // beard style
    write_uint16_be(&p, end, 0x44e); // beard color
    write_uint8(&p, end, 0); // unknown
    write_uint8(&p, end, 0); // location index
    write_uint32_be(&p, end, 0); // slot
    write_uint32_be(&p, end, 0); // ip
    write_uint16_be(&p, end, 0); // shirt color
    write_uint16_be(&p, end, 0); // pants color
    assert(p == end);
    send_packet(data, end);
}

void send_select_character(int slot)
{
    char data[73];
    char *p = data;
    char *end = p + sizeof(data);
    write_uint8(&p, end, 0x5d);
    write_uint32_be(&p, end, 0xededededu);
    write_ascii_fixed(&p, end, "dummy", 30);
    write_ascii_fixed(&p, end, "", 30);
    write_uint32_be(&p, end, slot); // slot
    write_uint32_be(&p, end, 0); // ip
    assert(p == end);
    send_packet(data, end);
}

void send_client_version_response(const char *v)
{
    int len = strlen(v);
    if (len > 9)
        len = 9;
    char data[3+10];
    char *p = data;
    char *end = p + sizeof(data);
    write_uint8(&p, end, 0xbd);
    write_uint16_be(&p, end, 3+len+1);
    printf("%s len: %d\n", v, len);
    write_ascii_fixed(&p, end, v, len+1);
    //assert(p == end);
    send_packet(data, data + 3+len+1);
}

int sequence = 0;

void net_send_move(int dir)
{
    char data[7];
    char *p = data;
    char *end = p + sizeof(data);
    write_uint8(&p, end, 0x02);
    write_uint8(&p, end, dir);
    write_uint8(&p, end, sequence);
    sequence += 1;
    write_uint32_be(&p, end, 0);
    assert(p == end);
    send_packet(data, end);
}

void net_send_use(uint32_t serial)
{
    char data[5];
    char *p = data;
    char *end = p + sizeof(data);
    write_uint8(&p, end, 0x06);
    write_uint32_be(&p, end, serial);
    assert(p == end);
    send_packet(data, end);
}

extern int huffman_tree[256][2];

void find_parent(int n, int *from, int *bit)
{
    for (int i = 0; i < 512; i++)
    {
        if (huffman_tree[i / 2][i % 2] == n)
        {
            *from = i / 2;
            *bit = i % 2;
            return;
        }
    }

    assert(0 && "no hit... what the heck.");
}

void find_bit_sequence(int n, int *seq, int *len)
{
    *seq = 0;
    *len = 0;

    while (true)
    {
        int from, bit;
        find_parent(n, &from, &bit);

        *seq = (*seq << 1) | bit;
        *len += 1;

        n = from;

        if (n == 0)
        {
            break;
        }
    }
}

static struct {
    int seq;
    int len;
} huffman_compress_table[257];

static void write_bit_be(char **p, const char *end, int b, int *target_bit)
{
    assert(*p < end);
    assert(*target_bit >= 0 && *target_bit < 8);

    uint8_t mask = (b&1) << *target_bit;
    **p = (**p & (~(1 << *target_bit))) | mask;
    if (*target_bit == 0)
    {
        *target_bit = 7;
        *p += 1;
    }
    else
    {
        *target_bit -= 1;
    }
}

void compress_byte(char **dst, const char *dst_end, int *target_bit, int byte)
{
    int seq = huffman_compress_table[byte].seq;
    int len = huffman_compress_table[byte].len;

    while (len)
    {
        write_bit_be(dst, dst_end, seq & 1, target_bit);
        len -= 1;
        seq >>= 1;
    }
}

int compress(char *dst, const char *dst_end, const char *src, const char *src_end)
{
    char *start = dst;

    // first build compress table
    for (int i = 0; i < 257; i++)
    {
        int seq, len;
        find_bit_sequence(-i, &seq, &len);

        huffman_compress_table[i].seq = seq;
        huffman_compress_table[i].len = len;
    }

    int target_bit = 7;

    // then compress all bytes
    while (src < src_end)
    {
        uint8_t b = *src;
        src += 1;

        compress_byte(&dst, dst_end, &target_bit, b);
    }
    // special flush character
    compress_byte(&dst, dst_end, &target_bit, 256);

    return (int)(dst-start);
}

// see http://code.google.com/p/nordwind/source/browse/trunk/Network/compression/Huffman.cpp?r=52
// calling this the caller need to make sure there is enough space in dst to hold all data!
// to do this one can use the fact that it takes at least 2 input bits to generate an output byte.
// so, make sure that 
int decompress(char *dst, const char *dst_end, const char *src, const char *src_end, int *src_state)
{
    int decompressed_bytes = 0;

    int node = (*src_state) & 0xff;
    uint8_t bit_mask = 0x80;

    while (src < src_end)
    {
        int select = (*src & bit_mask) != 0;

        //printf("%d %02x %d\n", node, bit_mask, select);
        node = huffman_tree[node][select];

        // special value to align next packet
        if (node == -256)
        {
            bit_mask = 0;
            node = 0;
        }
        else if (node <= 0)
        {
            //printf("node: %d\n", node);
            assert(dst < dst_end);
            //printf("assigning %d\n", -node);
            *dst = -node;
            dst += 1;
            decompressed_bytes += 1;

            node = 0;
        }

        bit_mask >>= 1;

        if (bit_mask == 0)
        {
            src++;
            bit_mask = 0x80;
        }
    }

    // repack state
    *src_state = (node);

    return decompressed_bytes;
}


int ma2in()
{
    char input[] = "\0\0\0hejsan på dig gamle gosse";
    char encoded[100] = {0};
    char decoded[100] = {0};

    int bla = 0;
    int compressed_bytes = compress(encoded, encoded+sizeof(encoded), input, input+sizeof(input));
    bla = 0;
    int decompressed_bytes = decompress(decoded, decoded+sizeof(decoded), encoded, encoded+compressed_bytes, &bla);

    printf("encoded (%d): ", compressed_bytes);
    for (int i = 0; i < compressed_bytes; i++)
    {
        printf("%02x ", (uint8_t)encoded[i]);
    }
    printf("\n");
    printf("decoded (%d): ", decompressed_bytes);
    for (int i = 0; i < decompressed_bytes; i++)
    {
        printf("%02x ", (uint8_t)decoded[i]);
    }
    printf("\n");

    printf("decoded str: %s\n", decoded);

    //int from, bit;
    //find_parent(0, &from, &bit);
    //printf("got to 0 from %d, %d\n", from, bit);

    int seq, len;
    printf("got to 1 with %x, %d\n", seq, len);

    return 0;
}


bool connect_to_game_server = false;
bool just_connected = true;
bool do_account_login = true;
bool use_game_server = false;

uint32_t game_server_ip;
uint16_t game_server_port;
uint32_t game_server_key;


// 128K buffer for incoming packets
char buf[1024*128];
int has_bytes = 0;

int packet_lengths[256];

int decompress_state = 0;

static bool inited;

void net_poll()
{
    assert(inited);
    while (true)
    {
        if (connect_to_game_server)
        {
            close(sockfd);
            sockfd = -1;

            uint32_t ip = game_server_ip;
            char ip_str[3*4+3+1];
            sprintf(ip_str, "%d.%d.%d.%d", (ip >> 24), (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff);

            sockfd = net_connect(ip_str, game_server_port);
            if (sockfd == -1)
            {
                printf("couldn't connect =(\n");
                exit(-1);
            }

            connect_to_game_server = false;
            just_connected = true;
            do_account_login = false;
            use_game_server = true;

            has_bytes = 0;
        }

        if (just_connected)
        {
            if (do_account_login)
            {
                send_prelogin(123);
                send_account_login("admin", "lol");
                do_account_login = false;
            }
            else
            {
                send_prelogin(game_server_key);
                send_game_server_login("admin", "lol", game_server_key);
            }

            just_connected = false;
        }

        char read_buf[16*1024];
        assert(fcntl(sockfd, F_SETFL, O_NONBLOCK) == 0);
        int read_bytes = recv(sockfd, read_buf, sizeof(read_buf), 0);
        assert(fcntl(sockfd, F_SETFL, 0) == 0);
        if (read_bytes == -1 && errno == EWOULDBLOCK)
        {
            // no new data at the moment...
            break;
        }
        else if (read_bytes <= 0)
        {
            printf("Disconnected? O_o\n");
            exit(-1);
        }

        //printf("read %d bytes\n", read_bytes);

        if (use_game_server)
        {
            char *p = &buf[has_bytes];
            char *end = buf + sizeof(buf) - has_bytes;
            int decompressed_bytes = decompress(p, end, read_buf, read_buf + read_bytes, &decompress_state);
            has_bytes += decompressed_bytes;

            /*for (int i = 0; i < has_bytes; i++)
            {
                printf("%02x ", (uint8_t)p[i]);
            }
            printf("\n");*/
        }
        else
        {
            memcpy(&buf[has_bytes], read_buf, read_bytes);
            has_bytes += read_bytes;
        }


        /*int bytes_left = sizeof(buf) - has_bytes;
                for (int i = 0; i < has_bytes; i++)
                {
                    printf("%02x %c ", (uint8_t)buf[i], ((uint8_t)buf[i]) >= 32 ? (uint8_t)buf[i] : ' ');
                }
                printf("\n");
#define min(a, b) (((a)<(b))?(a):(b))
        int read_bytes = recv(sockfd, &buf[has_bytes], min(2, bytes_left), 0);*/


        /*has_bytes += read_bytes;

        for (int i = 0; i < has_bytes; i++)
        {
            printf("%02x %c ", (uint8_t)buf[i], ((uint8_t)buf[i]) >= 32 ? (uint8_t)buf[i] : ' ');
        }
        printf("\n");

        if (use_game_server)
        {
            char bla[1024*1024];
            int decompressed_bytes = decompress(bla, bla+1024*1024, buf, buf+has_bytes, &decompress_state);

            printf("decompressed:\n");
            for (int i = 0; i < decompressed_bytes; i++)
            {
                printf("%02x %c ", (uint8_t)bla[i], ((uint8_t)bla[i]) >= 32 ? (uint8_t)bla[i] : ' ');
            }
            printf("\n");
        }*/


        while (has_bytes > 0)
        {
            uint8_t packet_id = buf[0];
            int packet_len = packet_lengths[packet_id];

            if (packet_len < 0)
            {
                printf("unknown packet: %02x\n", packet_id);
                printf("add support for this packet, then try again...\n");
                exit(-1);
            }

            if (packet_len == 0 && has_bytes < 3)
            {
                //printf("has %d bytes, need at least 3\n", has_bytes);
                // not enough data to read length
                break;
            }

            const char *p = &buf[1];
            const char *end;
            if (packet_len == 0)
            {
                packet_len = read_uint16_be(&p, p+2);
                end = buf + packet_len;
            }
            else
            {
                end = buf + packet_len;
            }
            //printf("%d %d\n", packet_len, (int)(end-buf));

            if (has_bytes < packet_len)
            {
                // not enough data to read content
                //printf("has %d bytes, need %d\n", has_bytes, packet_len);
                break;
            }

            /*printf("trying to handle packet:\n");
            for (const char *pp = buf; pp < end; pp++)
            {
                printf("%02x ", (uint8_t)*pp);
            }
            printf("\n");*/

            //printf("packet id: %02x, length: %d\n", packet_id, packet_len);

            switch (packet_id)
            {
                case 0x1a: {
                    uint32_t temp_serial = read_uint32_be(&p, end);
                    uint32_t serial = temp_serial & 0x7fffffff;
                    int temp_item_id = read_uint16_be(&p, end);
                    int item_id = temp_item_id & 0x7fff;
                    int amount = 1;
                    if (temp_serial & 0x80000000ul)
                    {
                        amount = read_uint16_be(&p, end);
                    }
                    if (temp_item_id & 0x8000)
                    {
                        item_id += read_uint8(&p, end);
                    }
                    int temp_x = read_uint16_be(&p, end);
                    int x = temp_x & 0x7fff;
                    int temp_y = read_uint16_be(&p, end);
                    int y = temp_y & 0x3fff;
                    int dir = 0;
                    if (temp_x & 0x8000)
                    {
                        dir = read_uint8(&p, end);
                    }
                    int z = read_sint8(&p, end);
                    int hue_id = 0;
                    if (temp_y & 0x8000)
                    {
                        hue_id = read_uint16_be(&p, end);
                    }
                    if (temp_y & 0x4000)
                    {
                        int flag = read_uint8(&p, end);
                    }
                    assert(p == end);

                    item_t *item = game_get_item(serial);
                    item->item_id = item_id;
                    item->x = x;
                    item->y = y;
                    item->z = z;
                    item->hue_id = hue_id;

                    //printf("0x1a: hue_id = %04x\n", hue_id);

                    break;
                }
                case 0x1b: {
                    uint32_t serial = read_uint32_be(&p, end);
                    read_uint32_be(&p, end);
                    int body_id = read_uint16_be(&p, end);
                    int x = read_uint16_be(&p, end);
                    int y = read_uint16_be(&p, end);
                    read_uint8(&p, end);
                    int z = read_sint8(&p, end);
                    int dir = read_uint8(&p, end);
                    read_uint32_be(&p, end);
                    read_uint32_be(&p, end);
                    read_uint8(&p, end);
                    int map_width = read_uint16_be(&p, end);
                    int map_height = read_uint16_be(&p, end);
                    printf("map width: %d height: %d\n", map_width, map_height);
                    read_uint32_be(&p, end);
                    read_uint16_be(&p, end);
                    game_set_player_info(serial, body_id, x, y, z, 0, dir);

                    break;
                }
                case 0x1d: {
                    uint32_t serial = read_uint32_be(&p, end);
                    //printf("delete object %x\n", serial);
                    game_delete_object(serial);
                    break;
                }
                case 0x20: {
                    uint32_t serial = read_uint32_be(&p, end);
                    int body_id = read_uint16_be(&p, end);
                    read_uint8(&p, end);
                    int hue_id = read_uint16_be(&p, end);
                    int flags = read_uint8(&p, end);
                    int x = read_uint16_be(&p, end);
                    int y = read_uint16_be(&p, end);
                    read_uint16_be(&p, end);
                    int dir = read_uint8(&p, end);
                    int z = read_sint8(&p, end);
                    //printf("0x20: hue_id = %04x\n", hue_id);
                    game_set_player_info(serial, body_id, x, y, z, hue_id, dir);
                    break;
                }
                case 0x21: {
                    int seq = read_uint8(&p, end);
                    int x = read_uint16_be(&p, end);
                    int y = read_uint16_be(&p, end);
                    int dir = read_uint8(&p, end);
                    int z = read_sint8(&p, end);

                    // reset movement sequence
                    sequence = 0;

                    //printf("move reject seq %d\n", seq);
                    game_set_player_pos(x, y, z, dir);
                    break;
                }
                case 0x22: {
                    int seq = read_uint8(&p, end);
                    uint8_t status = read_uint8(&p, end);

                    //printf("move ack seq %d\n", seq);
                    break;
                }
                case 0x24: {
                    uint32_t item_serial = read_uint32_be(&p, end);
                    int gump_id = read_uint16_be(&p, end);

                    game_display_container(item_serial, gump_id);

                    break;
                }
                case 0x25: {
                    uint32_t serial = read_uint32_be(&p, end);
                    int item_id = read_uint16_be(&p, end);
                    read_uint8(&p, end);
                    int amount = read_uint16_be(&p, end);
                    int x = read_sint16_be(&p, end);
                    int y = read_sint16_be(&p, end);
                    read_uint8(&p, end); // grid location?
                    uint32_t cont_serial = read_uint32_be(&p, end);
                    int hue_id = read_uint16_be(&p, end);

                    container_t *container = game_get_container(cont_serial);
                    // TODO: don't use magic constant here
                    assert(container->item_count < 256);
                    // TODO: don't just add another item, instead modify the existing item, if any..
                    container->items[container->item_count].serial = serial;
                    container->items[container->item_count].x = x;
                    container->items[container->item_count].y = y;
                    container->items[container->item_count].item_id = item_id;
                    container->items[container->item_count].hue_id = hue_id;
                    container->item_count += 1;

                    printf("adding item to container: %08x\n", serial);

                    break;
                }
                case 0x2e: {
                    uint32_t serial = read_uint32_be(&p, end);
                    int item_id = read_uint16_be(&p, end);
                    read_uint8(&p, end);
                    int layer = read_uint8(&p, end);
                    uint32_t mob_serial = read_uint32_be(&p, end);
                    int hue_id = read_uint16_be(&p, end);

                    mobile_t *m = game_get_mobile(mob_serial);
                    //printf("0x2e: hue_id = %04x\n", hue_id);
                    game_equip(m, serial, item_id, layer, hue_id);

                    break;
                }
                case 0x3c: {
                    int item_count = read_uint16_be(&p, end);

                    for (int i = 0; i < item_count; i++)
                    {
                        uint32_t serial = read_uint32_be(&p, end);
                        int item_id = read_uint16_be(&p, end);
                        read_uint8(&p, end);
                        int amount = read_uint16_be(&p, end);
                        int x = read_sint16_be(&p, end);
                        int y = read_sint16_be(&p, end);
                        read_uint8(&p, end); // grid location?
                        uint32_t cont_serial = read_uint32_be(&p, end);
                        int hue_id = read_uint16_be(&p, end);

                        container_t *container = game_get_container(cont_serial);
                        // TODO: don't use magic constant here
                        assert(container->item_count < 256);
                        container->items[container->item_count].serial = serial;
                        container->items[container->item_count].x = x;
                        container->items[container->item_count].y = y;
                        container->items[container->item_count].item_id = item_id;
                        container->items[container->item_count].hue_id = hue_id;
                        container->item_count += 1;

                        printf("adding item to container: %08x\n", serial);
                    }
                    break;
                }
                case 0x77: {
                    uint32_t mob_id = read_uint32_be(&p, end);
                    mobile_t *m = game_get_mobile(mob_id);

                    m->body_id = read_uint16_be(&p, end);
                    m->x = read_uint16_be(&p, end);
                    m->y = read_uint16_be(&p, end);
                    m->z = read_sint8(&p, end);
                    m->dir = read_uint8(&p, end);
                    m->hue_id = read_uint16_be(&p, end);
                    //printf("0x77: hue_id = %04x\n", m->hue_id);
                    read_uint8(&p, end); // status.. TODO: what is this?
                    m->noto = read_uint8(&p, end);
                    break;
                }
                case 0x78: {
                    uint32_t mob_serial = read_uint32_be(&p, end);
                    mobile_t *m = game_create_mobile(mob_serial);

                    m->body_id = read_uint16_be(&p, end);
                    m->x = read_uint16_be(&p, end);
                    m->y = read_uint16_be(&p, end);
                    m->z = read_sint8(&p, end);
                    m->dir = read_uint8(&p, end);
                    m->hue_id = read_uint16_be(&p, end);
                    //printf("0x78: mob.hue_id = %04x\n", m->hue_id);
                    read_uint8(&p, end); // status.. TODO: what is this?
                    m->noto = read_uint8(&p, end);
                    while (true)
                    {
                        uint32_t serial = read_uint32_be(&p, end);
                        if (serial == 0)
                        {
                            break;
                        }
                        int item_id = read_uint16_be(&p, end);
                        int layer = read_uint8(&p, end);
                        int hue_id = 0;
                        if (item_id & 0x8000)
                        {
                            hue_id = read_uint16_be(&p, end);
                            item_id &= 0x7fff;
                        }
                        //printf("0x78: item.hue_id = %04x\n", m->hue_id);
                        //printf("mob %x, equip %x %d %d %d\n", mob_id, id, item_id, layer, hue_id);
                        game_equip(m, serial, item_id, layer, hue_id);
                    }

                    break;
                }
                case 0x82: {
                    // login denied...
                    uint8_t reason = read_uint8(&p, end);
                    printf("login denied. reason: %d\n", reason);
                    break;
                }
                case 0x8c: {
                    uint32_t ip = read_uint32_be(&p, end);
                    game_server_ip = ip;
                    game_server_port = read_uint16_be(&p, end);
                    game_server_key = read_uint32_be(&p, end);
                    char ip_str[3*4+3+1];
                    sprintf(ip_str, "%d.%d.%d.%d", (ip >> 24), (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff);
                    printf("server: please connect to %s %d\n", ip_str, game_server_port);
                    connect_to_game_server = true;
                    break;
                }
                case 0xa8: {
                    uint8_t sys_info = read_uint8(&p, end);
                    int server_count = read_uint16_be(&p, end);
                    printf("%d servers:\n", server_count);
                    for (int i = 0; i < server_count; i++)
                    {
                        int index = read_uint16_be(&p, end);
                        char name[33];
                        read_ascii_fixed(&p, end, name, 32);
                        int full = read_uint8(&p, end);
                        int time_zone = read_uint8(&p, end);
                        uint32_t ip = read_uint32_be(&p, end);
                        char ip_str[3*4+3+1];
                        sprintf(ip_str, "%d.%d.%d.%d", (ip >> 24), (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff);
                        printf(" %s %s\n", name, ip_str);

                        if (server_count == 1)
                        {
                            // only 1 server? easy choice!
                            send_select_server(index);
                        }
                    }


                    /*close(sockfd);
                    sockfd = -1;

                    assert(server_count == 1);

                    if (server_count == 1)
                    {
                        // only 1 server? easy choice!
                        sockfd = net_connect("localhost", 2593);
                        if (sockfd == -1)
                        {
                            printf("couldn't connect =(\n");
                            return 0;
                        }
                        just_connected = true;
                    }
                    else
                    {
                        printf("multiple server... unhandled\n");
                    }*/
                    break;
                }
                case 0xa9: {
                    int char_slot_count = read_uint8(&p, end);
                    printf("%d chars slots:\n", char_slot_count);
                    bool occupied_slots[10];
                    int char_count = 0;
                    for (int i = 0; i < char_slot_count; i++)
                    {
                        char name[31];
                        char password[31];
                        read_ascii_fixed(&p, end, name, 30);
                        read_ascii_fixed(&p, end, password, 30);
                        occupied_slots[i] = (name[0] != '\0');
                        if (occupied_slots[i])
                        {
                            char_count += 1;
                        }
                        printf(" %d %s\n", occupied_slots[i], name);
                    }
                    printf("%d chars\n", char_count);
                    int city_count = read_uint8(&p, end);

                    int client_70130_len = 11 + char_slot_count * 60 + city_count * 89;
                    int else_len = 11 + char_slot_count * 60 + city_count * 63;

                    bool sa_client = (packet_len == client_70130_len + 2) || (packet_len == else_len + 2);

                    bool client_70130;
                    client_70130 = packet_len == (client_70130_len + sa_client ? 2 : 0);

                    printf("%d start cities:\n", city_count);
                    for (int i = 0; i < city_count; i++)
                    {
                        if (client_70130)
                        {
                            uint8_t loc_id = read_uint8(&p, end);
                            char name[33];
                            char area[33];
                            read_ascii_fixed(&p, end, name, 32);
                            read_ascii_fixed(&p, end, area, 32);
                            uint32_t x = read_uint32_be(&p, end);
                            uint32_t y = read_uint32_be(&p, end);
                            int z = read_sint32_be(&p, end);
                            uint32_t map = read_uint32_be(&p, end);
                            uint32_t cliloc = read_uint32_be(&p, end);
                            uint32_t zero = read_uint32_be(&p, end);
                            printf(" %d: %s\n", loc_id, name);
                        }
                        else
                        {
                            uint8_t loc_id = read_uint8(&p, end);
                            char name[32];
                            char area[32];
                            read_ascii_fixed(&p, end, name, 31);
                            read_ascii_fixed(&p, end, area, 31);
                            printf(" %d: %s\n", loc_id, name);
                        }
                    }
                    //printf("%d %d %d %d %d\n", packet_len, client_70130_len, else_len, (int)sa_client, (int)client_70130);

                    uint32_t flags = read_uint32_be(&p, end);
                    if (sa_client)
                    {
                        int last_char = read_uint16_be(&p, end);
                    }

                    if (char_count == 0)
                    {
                        printf("WHAT AM I DOING\n");
                        // no chars... create one!
                        send_create_character();
                    }
                    else
                    {
                        // find valid char and login
                        for (int i = 0; i < char_slot_count; i++)
                        {
                            if (occupied_slots[i])
                            {
                                send_select_character(i);
                                break;
                            }
                        }
                    }

                    break;
                }
                case 0xbd: {
                    send_client_version_response("7.0.1");
                    packet_lengths[0xb9] = 5; // expect longer packet after this.. (TODO: this should only be done for newer servers)
                    break;
                }
                case 0xbf: {
                    int cmd = read_uint16_be(&p, end);
                    switch (cmd)
                    {
                        case 8: {
                            // TODO: actually do something with this info
                            int map = read_uint8(&p, end);
                            printf("ignoring instruction to switch to map %d\n", map);
                            break;
                        }
                        default:
                            printf("ignoring 0xbf subcommand %02x\n", cmd);
                    }
                    break;
                }
                case 0xf3: {
                    read_uint16_be(&p, end); // unknown
                    int type = read_uint8(&p, end); // 0 = item, 1 = multi
                    uint32_t serial = read_uint32_be(&p, end);
                    int item_id = read_uint16_be(&p, end);
                    int direction = read_uint8(&p, end);
                    int amount = read_uint16_be(&p, end);
                    read_uint16_le(&p, end); // amount again?
                    int x = read_uint16_be(&p, end);
                    int y = read_uint16_be(&p, end);
                    int z = read_sint8(&p, end);
                    int layer = read_uint8(&p, end);
                    int hue_id = read_uint16_be(&p, end);
                    //printf("0xf3: hue_id = %04x\n", hue_id);
                    int flag = read_uint8(&p, end);

                    if (type == 0)
                    {
                        item_t *item = game_get_item(serial);
                        item->item_id = item_id;
                        item->x = x;
                        item->y = y;
                        item->z = z;
                        item->hue_id = hue_id;
                    }
                    else
                    {
                        // TODO: add multi
                        printf("OMGOGMOGMOGM MULTI?!?!\n");
                    }

                    break;
                }
                default: {
                    printf("ignoring packet %02x\n", packet_id);
                    break;
                }
            }
            //assert(p == end);

            //printf("memmove(%p, %p, %d)\n", buf, &buf[packet_len], has_bytes - packet_len);
            memmove(buf, &buf[packet_len], has_bytes - packet_len);
            has_bytes -= packet_len;
        }
    }

}

void net_connect()
{
    assert(inited);
    printf("connecting...\n");
    sockfd = net_connect("192.168.1.226", 2593);
    printf("ok!\n");
    if (sockfd == -1)
    {
        printf("couldn't connect =(\n");
        exit(0);
    }
}

void net_init()
{
    assert(!inited);

    memset(packet_lengths, -1, sizeof(packet_lengths));

    packet_lengths[0x0b] = 7; // damage
    packet_lengths[0x11] = 0; // status bar info
    packet_lengths[0x17] = 12; // health bar update
    packet_lengths[0x1a] = 0; // add item
    packet_lengths[0x1b] = 37; // login confirm
    packet_lengths[0x1c] = 0; // text ascii
    packet_lengths[0x1d] = 5; // delete item
    packet_lengths[0x20] = 19; // update mobile
    packet_lengths[0x21] = 8; // move reject
    packet_lengths[0x22] = 3; // move accept
    packet_lengths[0x24] = 7; // display container
    packet_lengths[0x25] = 21; // update container
    packet_lengths[0x2c] = 2; // death status
    packet_lengths[0x2e] = 15; // equip update
    packet_lengths[0x3a] = 0; // skills
    packet_lengths[0x3c] = 0; // container contents
    packet_lengths[0x4e] = 6; // set personal light level
    packet_lengths[0x4f] = 2; // set overall light level
    packet_lengths[0x54] = 12; // play sound
    packet_lengths[0x55] = 1; // login complete
    packet_lengths[0x5b] = 4; // server time
    packet_lengths[0x65] = 4; // weather change
    packet_lengths[0x6d] = 3; // play music
    packet_lengths[0x6e] = 14; // character animation
    packet_lengths[0x72] = 5; // set war mode
    packet_lengths[0x77] = 17; // move mobile
    packet_lengths[0x78] = 0; // incoming mobile
    packet_lengths[0x82] = 2; // login denied
    packet_lengths[0x88] = 66; // display paperdoll
    packet_lengths[0x89] = 0; // corpse clothing
    packet_lengths[0x8c] = 11; // connect to game server
    packet_lengths[0xa1] = 9; // set health
    packet_lengths[0xa2] = 9; // set mana
    packet_lengths[0xa3] = 9; // set stamina
    packet_lengths[0xa8] = 0; // server list
    packet_lengths[0xa9] = 0; // character list
    packet_lengths[0xaa] = 5; // set combatant
    packet_lengths[0xae] = 0; // unicode speech
    packet_lengths[0xaf] = 13; // death animation
    packet_lengths[0xb0] = 0; // display gump fast
    packet_lengths[0xb9] = 3; // enable client features
    packet_lengths[0xbd] = 0; // client version request
    packet_lengths[0xbc] = 3; // season
    packet_lengths[0xbf] = 0; // extended command
    packet_lengths[0xc0] = 36; // hued effect
    packet_lengths[0xc1] = 0; // cliloc message
    packet_lengths[0xdc] = 9; // item revision
    packet_lengths[0xdd] = 0; // display gump packed
    packet_lengths[0xdf] = 0; // "buff/debuff system" <- what is this?
    packet_lengths[0xf3] = 24; // add item (new version)

    assert(!inited);
    inited = true;
}

int huffman_tree[256][2] = {
    /*node*/ /*leaf0 leaf1*/
    /* 0*/ { 2, 1},
    /* 1*/ { 4, 3},
    /* 2*/ { 0, 5},
    /* 3*/ { 7, 6},
    /* 4*/ { 9, 8},
    /* 5*/ { 11, 10},
    /* 6*/ { 13, 12},
    /* 7*/ { 14, -256},
    /* 8*/ { 16, 15},
    /* 9*/ { 18, 17},
    /* 10*/ { 20, 19},
    /* 11*/ { 22, 21},
    /* 12*/ { 23, -1},
    /* 13*/ { 25, 24},
    /* 14*/ { 27, 26},
    /* 15*/ { 29, 28},
    /* 16*/ { 31, 30},
    /* 17*/ { 33, 32},
    /* 18*/ { 35, 34},
    /* 19*/ { 37, 36},
    /* 20*/ { 39, 38},
    /* 21*/ { -64, 40},
    /* 22*/ { 42, 41},
    /* 23*/ { 44, 43},
    /* 24*/ { 45, -6},
    /* 25*/ { 47, 46},
    /* 26*/ { 49, 48},
    /* 27*/ { 51, 50},
    /* 28*/ { 52, -119},
    /* 29*/ { 53, -32},
    /* 30*/ { -14, 54},
    /* 31*/ { -5, 55},
    /* 32*/ { 57, 56},
    /* 33*/ { 59, 58},
    /* 34*/ { -2, 60},
    /* 35*/ { 62, 61},
    /* 36*/ { 64, 63},
    /* 37*/ { 66, 65},
    /* 38*/ { 68, 67},
    /* 39*/ { 70, 69},
    /* 40*/ { 72, 71},
    /* 41*/ { 73, -51},
    /* 42*/ { 75, 74},
    /* 43*/ { 77, 76},
    /* 44*/ {-111, -101},
    /* 45*/ { -97, -4},
    /* 46*/ { 79, 78},
    /* 47*/ { 80, -110},
    /* 48*/ {-116, 81},
    /* 49*/ { 83, 82},
    /* 50*/ {-255, 84},
    /* 51*/ { 86, 85},
    /* 52*/ { 88, 87},
    /* 53*/ { 90, 89},
    /* 54*/ { -10, -15},
    /* 55*/ { 92, 91},
    /* 56*/ { 93, -21},
    /* 57*/ { 94, -117},
    /* 58*/ { 96, 95},
    /* 59*/ { 98, 97},
    /* 60*/ { 100, 99},
    /* 61*/ { 101, -114},
    /* 62*/ { 102, -105},
    /* 63*/ { 103, -26},
    /* 64*/ { 105, 104},
    /* 65*/ { 107, 106},
    /* 66*/ { 109, 108},
    /* 67*/ { 111, 110},
    /* 68*/ { -3, 112},
    /* 69*/ { -7, 113},
    /* 70*/ {-131, 114},
    /* 71*/ {-144, 115},
    /* 72*/ { 117, 116},
    /* 73*/ { 118, -20},
    /* 74*/ { 120, 119},
    /* 75*/ { 122, 121},
    /* 76*/ { 124, 123},
    /* 77*/ { 126, 125},
    /* 78*/ { 128, 127},
    /* 79*/ {-100, 129},
    /* 80*/ { -8, 130},
    /* 81*/ { 132, 131},
    /* 82*/ { 134, 133},
    /* 83*/ { 135, -120},
    /* 84*/ { -31, 136},
    /* 85*/ { 138, 137},
    /* 86*/ {-234, -109},
    /* 87*/ { 140, 139},
    /* 88*/ { 142, 141},
    /* 89*/ { 144, 143},
    /* 90*/ { 145, -112},
    /* 91*/ { 146, -19},
    /* 92*/ { 148, 147},
    /* 93*/ { -66, 149},
    /* 94*/ {-145, 150},
    /* 95*/ { -65, -13},
    /* 96*/ { 152, 151},
    /* 97*/ { 154, 153},
    /* 98*/ { 155, -30},
    /* 99*/ { 157, 156},
    /* 100*/ { 158, -99},
    /* 101*/ { 160, 159},
    /* 102*/ { 162, 161},
    /* 103*/ { 163, -23},
    /* 104*/ { 164, -29},
    /* 105*/ { 165, -11},
    /* 106*/ {-115, 166},
    /* 107*/ { 168, 167},
    /* 108*/ { 170, 169},
    /* 109*/ { 171, -16},
    /* 110*/ { 172, -34},
    /* 111*/ {-132, 173},
    /* 112*/ {-108, 174},
    /* 113*/ { -22, 175},
    /* 114*/ { -9, 176},
    /* 115*/ { -84, 177},
    /* 116*/ { -37, -17},
    /* 117*/ { 178, -28},
    /* 118*/ { 180, 179},
    /* 119*/ { 182, 181},
    /* 120*/ { 184, 183},
    /* 121*/ { 186, 185},
    /* 122*/ {-104, 187},
    /* 123*/ { -78, 188},
    /* 124*/ { -61, 189},
    /* 125*/ {-178, -79},
    /* 126*/ {-134, -59},
    /* 127*/ { -25, 190},
    /* 128*/ { -18, -83},
    /* 129*/ { -57, 191},
    /* 130*/ { 192, -67},
    /* 131*/ { 193, -98},
    /* 132*/ { -68, -12},
    /* 133*/ { 195, 194},
    /* 134*/ {-128, -55},
    /* 135*/ { -50, -24},
    /* 136*/ { 196, -70},
    /* 137*/ { -33, -94},
    /* 138*/ {-129, 197},
    /* 139*/ { 198, -74},
    /* 140*/ { 199, -82},
    /* 141*/ { -87, -56},
    /* 142*/ { 200, -44},
    /* 143*/ { 201, -248},
    /* 144*/ { -81, -163},
    /* 145*/ {-123, -52},
    /* 146*/ {-113, 202},
    /* 147*/ { -41, -48},
    /* 148*/ { -40, -122},
    /* 149*/ { -90, 203},
    /* 150*/ { 204, -54},
    /* 151*/ {-192, -86},
    /* 152*/ { 206, 205},
    /* 153*/ {-130, 207},
    /* 154*/ { 208, -53},
    /* 155*/ { -45, -133},
    /* 156*/ { 210, 209},
    /* 157*/ { -91, 211},
    /* 158*/ { 213, 212},
    /* 159*/ { -88, -106},
    /* 160*/ { 215, 214},
    /* 161*/ { 217, 216},
    /* 162*/ { -49, 218},
    /* 163*/ { 220, 219},
    /* 164*/ { 222, 221},
    /* 165*/ { 224, 223},
    /* 166*/ { 226, 225},
    /* 167*/ {-102, 227},
    /* 168*/ { 228, -160},
    /* 169*/ { 229, -46},
    /* 170*/ { 230, -127},
    /* 171*/ { 231, -103},
    /* 172*/ { 233, 232},
    /* 173*/ { 234, -60},
    /* 174*/ { -76, 235},
    /* 175*/ {-121, 236},
    /* 176*/ { -73, 237},
    /* 177*/ { 238, -149},
    /* 178*/ {-107, 239},
    /* 179*/ { 240, -35},
    /* 180*/ { -27, -71},
    /* 181*/ { 241, -69},
    /* 182*/ { -77, -89},
    /* 183*/ {-118, -62},
    /* 184*/ { -85, -75},
    /* 185*/ { -58, -72},
    /* 186*/ { -80, -63},
    /* 187*/ { -42, 242},
    /* 188*/ {-157, -150},
    /* 189*/ {-236, -139},
    /* 190*/ {-243, -126},
    /* 191*/ {-214, -142},
    /* 192*/ {-206, -138},
    /* 193*/ {-146, -240},
    /* 194*/ {-147, -204},
    /* 195*/ {-201, -152},
    /* 196*/ {-207, -227},
    /* 197*/ {-209, -154},
    /* 198*/ {-254, -153},
    /* 199*/ {-156, -176},
    /* 200*/ {-210, -165},
    /* 201*/ {-185, -172},
    /* 202*/ {-170, -195},
    /* 203*/ {-211, -232},
    /* 204*/ {-239, -219},
    /* 205*/ {-177, -200},
    /* 206*/ {-212, -175},
    /* 207*/ {-143, -244},
    /* 208*/ {-171, -246},
    /* 209*/ {-221, -203},
    /* 210*/ {-181, -202},
    /* 211*/ {-250, -173},
    /* 212*/ {-164, -184},
    /* 213*/ {-218, -193},
    /* 214*/ {-220, -199},
    /* 215*/ {-249, -190},
    /* 216*/ {-217, -230},
    /* 217*/ {-216, -169},
    /* 218*/ {-197, -191},
    /* 219*/ { 243, -47},
    /* 220*/ { 245, 244},
    /* 221*/ { 247, 246},
    /* 222*/ {-159, -148},
    /* 223*/ { 249, 248},
    /* 224*/ { -93, -92},
    /* 225*/ {-225, -96},
    /* 226*/ { -95, -151},
    /* 227*/ { 251, 250},
    /* 228*/ { 252, -241},
    /* 229*/ { -36, -161},
    /* 230*/ { 254, 253},
    /* 231*/ { -39, -135},
    /* 232*/ {-124, -187},
    /* 233*/ {-251, 255},
    /* 234*/ {-238, -162},
    /* 235*/ { -38, -242},
    /* 236*/ {-125, -43},
    /* 237*/ {-253, -215},
    /* 238*/ {-208, -140},
    /* 239*/ {-235, -137},
    /* 240*/ {-237, -158},
    /* 241*/ {-205, -136},
    /* 242*/ {-141, -155},
    /* 243*/ {-229, -228},
    /* 244*/ {-168, -213},
    /* 245*/ {-194, -224},
    /* 246*/ {-226, -196},
    /* 247*/ {-233, -183},
    /* 248*/ {-167, -231},
    /* 249*/ {-189, -174},
    /* 250*/ {-166, -252},
    /* 251*/ {-222, -198},
    /* 252*/ {-179, -188},
    /* 253*/ {-182, -223},
    /* 254*/ {-186, -180},
    /* 255*/ {-247, -245}
};

