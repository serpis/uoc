#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cstring>

#include <string>
#include <iostream>
#include <sstream>
#include <vector>

#include <zlib.h>

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
#include "mullib.hpp" // only for ml_get_cliloc... move to game?

// used to define address/port and username/password
#include "account.hpp"

static bool enable_compression = false;

static int compress(char *dst, const char *dst_end, const char *src, const char *src_end);

static int decode_one_utf8(const char *s, int *used_octets)
{
    assert(s[0] != 0);
    if (s[0] & 0x80) // highest bit set?
    {
        int rem_octs;
        int build;
        if ((s[0] & 0x20) == 0) // 2 octets
        {
            rem_octs = 1;
            build = s[0] & 0x1f;
        }
        else if ((s[0] & 0x10) == 0) // 3 octets
        {
            rem_octs = 2;
            build = s[0] & 0xf;
        }
        else // if ((s[0] & 0x8) == 0) // 4 octets
        {
            rem_octs = 3;
            build = s[0] & 0x7;
        }
        *used_octets = rem_octs + 1;

        int i = 0;
        while (rem_octs-- > 0)
        {
            i++;
            assert(s[i] != 0);
            build = (build << 6) | (s[i] & 0x3f);
        }
        return build;
    }
    else
    {
        *used_octets = 1;
        return s[0];
    }
}

std::wstring decode_utf8_cstr(const char *s)
{
    std::wstring res;
    while (*s)
    {
        int used;
        res += decode_one_utf8(s, &used);
        s += used;
    }
    return res;
}

/*static std::wstring cstr_to_wstring(const char *s)
{
    const char *p = s;
    std::wstring ws;
    while (*p)
    {
        ws += *p;
        p++;
    }
    return ws;
}*/

const int GUMPCMD_NOCLOSE   =  0;
const int GUMPCMD_NORESIZE  =  1;
const int GUMPCMD_PAGE      =  2;
const int GUMPCMD_PIC       =  3;
const int GUMPCMD_PICTILED  =  4;
const int GUMPCMD_RESIZEPIC =  5;
const int GUMPCMD_BUTTON    =  6;
const int GUMPCMD_ITEM      =  7;
const int GUMPCMD_LABEL     =  8;
const int GUMPCMD_FREETEXT  =  9;
const int GUMPCMD_LOCALIZED = 10;
struct gump_command_t
{
    int type;
    union
    {
        struct
        {
            int page_no;
        } page;
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
            int hue_id;
            int text_id;
        } label;
        struct
        {
            int x, y;
            int width, height;
            int text_id;
            int background;
            int scrollbar;
        } free_text;
        struct
        {
            int x, y;
            int width, height;
            int cliloc_id;
            int background;
            int scrollbar;
            int color;
            std::wstring *arg_str;
        } localized;
    };
};

static gump_command_t parse_gump_command(std::wstring command_str)
{
    gump_command_t command;

    std::wcout << command_str << std::endl;

    std::vector<std::wstring> tokens;

    // first split str into tokens
    {
        // first split into tokens
        std::wstring::iterator it = command_str.begin();

        // skip first "{"
        it += 1;

        while (it != command_str.end())
        {
            // skip spaces
            while (*it == ' ') it += 1;

            if (*it == '}')
            {
                break;
            }
            else if (*it == '@')
            {
                it += 1;
                std::wstring token;
                while (*it != '@')
                {
                    token += *it++;
                }
                tokens.push_back(token);
                it += 1;
            }
            else
            {
                std::wstring token;
                while (*it != ' ')
                {
                    token += *it++;
                }
                tokens.push_back(token);
            }
        }
    }

    assert(tokens.size() > 0);

    if (tokens[0] == L"noclose")
    {
        command.type = GUMPCMD_NOCLOSE;
    }
    else if (tokens[0] == L"noresize")
    {
        command.type = GUMPCMD_NORESIZE;
    }
    else if (tokens[0] == L"page")
    {
        command.type = GUMPCMD_PAGE;
        std::wistringstream(tokens[1]) >> command.page.page_no;
    }
    else if (tokens[0] == L"gumppic")
    {
        command.type = GUMPCMD_PIC;
        std::wistringstream(tokens[1]) >> command.pic.x;
        std::wistringstream(tokens[2]) >> command.pic.y;
        std::wistringstream(tokens[3]) >> command.pic.gump_id;
    }
    else if (tokens[0] == L"gumppictiled")
    {
        command.type = GUMPCMD_PICTILED;
        std::wistringstream(tokens[1]) >> command.pictiled.x;
        std::wistringstream(tokens[2]) >> command.pictiled.y;
        std::wistringstream(tokens[3]) >> command.pictiled.width;
        std::wistringstream(tokens[4]) >> command.pictiled.height;
        std::wistringstream(tokens[5]) >> command.pictiled.gump_id;
    }
    else if (tokens[0] == L"button")
    {
        command.type = GUMPCMD_BUTTON;
        std::wistringstream(tokens[1]) >> command.button.x;
        std::wistringstream(tokens[2]) >> command.button.y;
        std::wistringstream(tokens[3]) >> command.button.up_gump_id;
        std::wistringstream(tokens[4]) >> command.button.down_gump_id;
        std::wistringstream(tokens[5]) >> command.button.type;
        std::wistringstream(tokens[6]) >> command.button.param;
        std::wistringstream(tokens[7]) >> command.button.button_id;
    }
    else if (tokens[0] == L"xmfhtmlgump")
    {
        command.type = GUMPCMD_LOCALIZED;
        std::wistringstream(tokens[1]) >> command.localized.x;
        std::wistringstream(tokens[2]) >> command.localized.y;
        std::wistringstream(tokens[3]) >> command.localized.width;
        std::wistringstream(tokens[4]) >> command.localized.height;
        std::wistringstream(tokens[5]) >> command.localized.cliloc_id;
        std::wistringstream(tokens[6]) >> command.localized.background;
        std::wistringstream(tokens[7]) >> command.localized.scrollbar;
        command.localized.color = 0;
        command.localized.arg_str = new std::wstring;
    }
    else if (tokens[0] == L"xmfhtmlgumpcolor")
    {
        command.type = GUMPCMD_LOCALIZED;
        std::wistringstream(tokens[1]) >> command.localized.x;
        std::wistringstream(tokens[2]) >> command.localized.y;
        std::wistringstream(tokens[3]) >> command.localized.width;
        std::wistringstream(tokens[4]) >> command.localized.height;
        std::wistringstream(tokens[5]) >> command.localized.cliloc_id;
        std::wistringstream(tokens[6]) >> command.localized.background;
        std::wistringstream(tokens[7]) >> command.localized.scrollbar;
        std::wistringstream(tokens[8]) >> command.localized.color;
        command.localized.arg_str = new std::wstring;
    }
    else if (tokens[0] == L"xmfhtmltok")
    {
        command.type = GUMPCMD_LOCALIZED;
        std::wistringstream(tokens[1]) >> command.localized.x;
        std::wistringstream(tokens[2]) >> command.localized.y;
        std::wistringstream(tokens[3]) >> command.localized.width;
        std::wistringstream(tokens[4]) >> command.localized.height;
        std::wistringstream(tokens[5]) >> command.localized.background;
        std::wistringstream(tokens[6]) >> command.localized.scrollbar;
        std::wistringstream(tokens[7]) >> command.localized.color;
        std::wistringstream(tokens[8]) >> command.localized.cliloc_id;
        command.localized.arg_str = new std::wstring;
        *command.localized.arg_str = tokens[9];
    }
    else if (tokens[0] == L"resizepic")
    {
        command.type = GUMPCMD_RESIZEPIC;
        std::wistringstream(tokens[1]) >> command.resizepic.x;
        std::wistringstream(tokens[2]) >> command.resizepic.y;
        std::wistringstream(tokens[3]) >> command.resizepic.gump_id_base;
        std::wistringstream(tokens[4]) >> command.resizepic.width;
        std::wistringstream(tokens[5]) >> command.resizepic.height;
    }
    else if (tokens[0] == L"htmlgump")
    {
        command.type = GUMPCMD_FREETEXT;
        std::wistringstream(tokens[1]) >> command.free_text.x;
        std::wistringstream(tokens[2]) >> command.free_text.y;
        std::wistringstream(tokens[3]) >> command.free_text.width;
        std::wistringstream(tokens[4]) >> command.free_text.height;
        std::wistringstream(tokens[5]) >> command.free_text.text_id;
        std::wistringstream(tokens[6]) >> command.free_text.background;
        std::wistringstream(tokens[7]) >> command.free_text.scrollbar;
    }
    else if (tokens[0] == L"text")
    {
        command.type = GUMPCMD_LABEL;
        std::wistringstream(tokens[1]) >> command.label.x;
        std::wistringstream(tokens[2]) >> command.label.y;
        std::wistringstream(tokens[3]) >> command.label.hue_id;
        std::wistringstream(tokens[4]) >> command.label.text_id;
    }
    else if (tokens[0] == L"croppedtext")
    {
        command.type = GUMPCMD_LABEL;
        std::wistringstream(tokens[1]) >> command.label.x;
        std::wistringstream(tokens[2]) >> command.label.y;
        int width, height;
        std::wistringstream(tokens[3]) >> width;
        std::wistringstream(tokens[4]) >> width;
        std::wistringstream(tokens[5]) >> command.label.hue_id;
        std::wistringstream(tokens[6]) >> command.label.text_id;
    }
    else if (tokens[0] == L"tilepic")
    {
        command.type = GUMPCMD_ITEM;
        std::wistringstream(tokens[1]) >> command.item.x;
        std::wistringstream(tokens[2]) >> command.item.y;
        std::wistringstream(tokens[3]) >> command.item.item_id;
        command.item.hue_id = 0;
    }
    else if (tokens[0] == L"tilepichue")
    {
        command.type = GUMPCMD_ITEM;
        std::wistringstream(tokens[1]) >> command.item.x;
        std::wistringstream(tokens[2]) >> command.item.y;
        std::wistringstream(tokens[3]) >> command.item.item_id;
        std::wistringstream(tokens[3]) >> command.item.hue_id;
    }
    else if (tokens[0] == L"checkbox")
    {
        // TODO: handle this properly
        command.type = GUMPCMD_NOCLOSE;
    }
    else if (tokens[0] == L"checkertrans")
    {
        // TODO: handle this properly
        command.type = GUMPCMD_NOCLOSE;
    }
    else
    {
        std::wcout << "unhandled gump command: " << tokens[0] << std::endl;
        assert(0 && "unhandled gump command");
    }

    return command;
}

static std::list<gump_command_t> parse_gump_commands(std::wstring all_commands_str)
{
    std::list<gump_command_t> commands;

    int i = 0;
    while (true)
    {
        long cmd_end = all_commands_str.find('}', i);
        if (cmd_end == std::wstring::npos)
        {
            break;
        }

        std::wstring command_str = all_commands_str.substr(i, cmd_end - i + 1);
        i = cmd_end + 1;

        gump_command_t command = parse_gump_command(command_str);
        commands.push_back(command);
    }

    return commands;
}

static std::wstring cliloc_format_resolve(std::wstring format, std::vector<std::wstring> args)
{
    std::wstring res;
    for (int i = 0; i < format.length(); i++)
    {
        if (format[i] == '~')
        {
            // arg substitution
            long num_end = format.find('_', i+1);
            if (num_end == std::wstring::npos)
            {
                num_end = format.length();
            }
            std::wstring num_str = format.substr(i+1, num_end-(i+1));

            int arg_idx;
            std::wistringstream(num_str) >> arg_idx;

            // arguments are 1-indexed, but our array is 0-indexed
            arg_idx -= 1;
            assert(arg_idx >= 0 && arg_idx < args.size());

            std::wstring arg = args[arg_idx];

            // resolve any clilocs in argument list
            if (arg.length() > 0 && arg[0] == L'#')
            {
                int arg_cliloc_id;
                std::wistringstream(arg.substr(1)) >> arg_cliloc_id;
                arg = decode_utf8_cstr(ml_get_cliloc(arg_cliloc_id));
            }

            res += arg;

            // skip rest of arg in format string
            long next_tilde = format.find('~', i+1);
            if (next_tilde == std::wstring::npos)
            {
                next_tilde = format.length();
            }
            i = next_tilde;
        }
        else
        {
            res += format[i];
        }
    }

    return res;
}

static std::vector<std::wstring> split(std::wstring s, int delim)
{
    std::vector<std::wstring> args;
    std::wstring arg;
    for (std::wstring::iterator it = s.begin(); it != s.end(); ++it)
    {
        if (*it == delim)
        {
            args.push_back(arg);
            arg = L"";
        }
        else
        {
            arg += *it;
        }
    }
    if (arg.length() > 0)
    {
        args.push_back(arg);
    }

    return args;
}

static std::list<std::wstring> massage_text(int font_id, int width, std::wstring s)
{
    std::vector<std::wstring> words = split(s, ' ');

    int space_width;
    int space_height;
    ml_get_font_string_dimensions(font_id, L" ", &space_width, &space_height);

    std::list<std::wstring> strs;

    int w = 0;

    std::wstring building;
    for (std::vector<std::wstring>::iterator it = words.begin(); it != words.end(); ++it)
    {
        int word_width, word_height;
        ml_get_font_string_dimensions(font_id, *it, &word_width, &word_height);

        if (w == 0)
        {
            building = *it;
            w = word_width;
        }
        else
        {
            if (w + space_width + word_width > width)
            {
                strs.push_back(building);
                building = *it;
                w = word_width;
            }
            else
            {
                building += L" ";
                building += *it;
                w += space_width + word_width;
            }
        }
    }
    if (building.size() > 0)
    {
        strs.push_back(building);
    }

    return strs;
}

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
            sockfd = -1;
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
    //printf("%s len: %d\n", v, len);
    write_ascii_fixed(&p, end, v, len+1);
    //assert(p == end);
    send_packet(data, data + 3+len+1);
}


static int ping_sequence = 0;
void net_send_ping()
{
    char data[2];
    char *p = data;
    char *end = p + sizeof(data);
    write_uint8(&p, end, 0x73);
    write_uint8(&p, end, ping_sequence);
    ping_sequence += 1;
    ping_sequence %= 256;
    assert(p == end);
    send_packet(data, end);
}

//static int move_sequence = 0;
void net_send_move(int dir, int seq)
{
    char data[7];
    char *p = data;
    char *end = p + sizeof(data);
    write_uint8(&p, end, 0x02);
    write_uint8(&p, end, 0x80|dir);
    write_uint8(&p, end, seq);
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

void net_send_inspect(uint32_t serial)
{
    char data[5];
    char *p = data;
    char *end = p + sizeof(data);
    write_uint8(&p, end, 0x09);
    write_uint32_be(&p, end, serial);
    assert(p == end);
    send_packet(data, end);
}

void net_send_pick_up_item(uint32_t serial, int amount)
{
    char data[7];
    char *p = data;
    char *end = p + sizeof(data);
    write_uint8(&p, end, 0x07);
    write_uint32_be(&p, end, serial);
    write_uint16_be(&p, end, amount);
    assert(p == end);
    send_packet(data, end);
}

void net_send_drop_item(uint32_t item_serial, int x, int y, int z, uint32_t cont_serial)
{
    char data[15];
    char *p = data;
    char *end = p + sizeof(data);
    write_uint8(&p, end, 0x08);
    write_uint32_be(&p, end, item_serial);
    write_uint16_be(&p, end, x);
    write_uint16_be(&p, end, y);
    write_sint8(&p, end, z);
    write_uint8(&p, end, 0); // backpack grid index
    write_uint32_be(&p, end, cont_serial);
    assert(p == end);
    send_packet(data, end);
}

void net_send_equip_item(uint32_t item_serial, int layer, uint32_t mob_serial)
{
    char data[10];
    char *p = data;
    char *end = p + sizeof(data);
    write_uint8(&p, end, 0x13);
    write_uint32_be(&p, end, item_serial);
    write_uint8(&p, end, layer);
    write_uint32_be(&p, end, mob_serial);
    assert(p == end);
    send_packet(data, end);
}

void net_send_gump_response(uint32_t serial, uint32_t gump_type_id, int response_id)
{
    char data[128];
    char *p = data;
    char *end = p + sizeof(data);
    write_uint8(&p, end, 0xb1);
    char *lenp = p;
    write_uint16_be(&p, end, 0); // reserve length field

    printf("sending response to gump %08x, %08x: %d\n", serial, gump_type_id, response_id);

    write_uint32_be(&p, end, serial);
    write_uint32_be(&p, end, gump_type_id);
    write_uint32_be(&p, end, response_id);

    write_uint32_be(&p, end, 0); // switch count

    write_uint32_be(&p, end, 0); // text count

    // fill in length
    int length = p - data;
    write_uint16_be(&lenp, end, length);

    send_packet(data, p);
}

void net_send_speech(std::wstring str)
{
    char data[512];
    char *p = data;
    const char *start = p;
    const char *end = p + sizeof(data);
    int str_len = str.length();
    int str_byte_len = str_len * 2;
    int packet_len = 12 + str_byte_len + 2;

    write_uint8(&p, end, 0xad);
    write_uint16_be(&p, end, packet_len);

    write_uint8(&p, end, 0); // type
    write_uint16_be(&p, end, 0); // hue_id
    write_uint16_be(&p, end, 0); // font_id
    write_uint8(&p, end, 'E'); // language
    write_uint8(&p, end, 'N'); // language
    write_uint8(&p, end, 'U'); // language
    write_uint8(&p, end, '\0'); // language

    for (int i = 0; i < str_len; i++)
    {
        int c = str[i];
        write_uint16_be(&p, end, c);
    }
    // null-terminate
    write_uint16_be(&p, end, 0);

    assert(p - start == packet_len);
    send_packet(data, p);
}

void net_send_razor_ack()
{
    char data[4];
    char *p = data;
    char *end = p + sizeof(data);
    write_uint8(&p, end, 0xf0);
    write_uint16_be(&p, end, 4);
    write_uint8(&p, end, 0xff);
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
                send_account_login(LOGIN_USERNAME, LOGIN_PASSWORD);
                do_account_login = false;
            }
            else
            {
                send_prelogin(game_server_key);
                send_game_server_login(LOGIN_USERNAME, LOGIN_PASSWORD, game_server_key);
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
                    item->space = SPACETYPE_WORLD;
                    item->loc.world.x = x;
                    item->loc.world.y = y;
                    item->loc.world.z = z;
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
                    //printf("map width: %d height: %d\n", map_width, map_height);
                    read_uint32_be(&p, end);
                    read_uint16_be(&p, end);

                    game_set_player_serial(serial);
                    mobile_t *m = game_get_mobile(serial);
                    m->body_id = body_id;
                    m->x = x;
                    m->y = y;
                    m->z = z;
                    m->dir = dir;

                    break;
                }
                case 0x1c: {
                    uint32_t serial = read_uint32_be(&p, end);
                    int graphic = read_uint16_be(&p, end);
                    read_uint8(&p, end); // type
                    int hue = read_uint16_be(&p, end);
                    int font_id = read_uint16_be(&p, end);
                    char name[31];
                    name[30] = '\0';
                    read_ascii_fixed(&p, end, name, 30);
                    std::wstring s;
                    int remaining_bytes = end - p;
                    int remaining_chars = remaining_bytes;
                    for (int i = 0; i < remaining_chars; i++)
                    {
                        s += read_uint8(&p, end);
                    }
                    // TODO: do something with this text...
                    printf("%s: ", name);
                    std::wcout << s << std::endl;
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
                    game_set_player_serial(serial);

                    mobile_t *m = game_get_mobile(serial);

                    m->body_id = read_uint16_be(&p, end);
                    read_uint8(&p, end);
                    m->hue_id = read_uint16_be(&p, end);
                    m->flags = read_uint8(&p, end);
                    m->x = read_uint16_be(&p, end);
                    m->y = read_uint16_be(&p, end);
                    read_uint16_be(&p, end);
                    m->dir = read_uint8(&p, end);
                    m->z = read_sint8(&p, end);

                    break;
                }
                case 0x21: {
                    uint32_t serial = game_get_player_serial();
                    mobile_t *m = game_get_mobile(serial);

                    int seq = read_uint8(&p, end);

                    int x = read_uint16_be(&p, end);
                    int y = read_uint16_be(&p, end);
                    int dir = read_uint8(&p, end);
                    int z = read_sint8(&p, end);

                    game_move_rejected(seq, x, y, z, dir);

                    break;
                }
                case 0x22: {
                    uint32_t serial = game_get_player_serial();
                    mobile_t *m = game_get_mobile(serial);

                    int seq = read_uint8(&p, end);
                    int flags = read_uint8(&p, end);
                    game_move_ack(seq, flags);

                    break;
                }
                case 0x24: {
                    uint32_t item_serial = read_uint32_be(&p, end);
                    int gump_id = read_uint16_be(&p, end);

                    game_show_container(item_serial, gump_id);

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

                    item_t *parent_item = game_get_item(cont_serial);
                    assert(parent_item->container_gump != NULL);

                    gump_t *container = parent_item->container_gump;;

                    item_t *item = game_get_item(serial);
                    // item_id == 0 means just inited
                    printf("item_id: %d, space: %d\n", item->item_id, item->space);
                    assert(item->item_id == 0 || item->space == SPACETYPE_CONTAINER);
                    item->space = SPACETYPE_CONTAINER;
                    item->loc.container.container = container;
                    item->loc.container.x = x;
                    item->loc.container.y = y;
                    item->item_id = item_id;
                    item->hue_id = hue_id;

                    container->container.items->push_back(item);

                    printf("adding item to container: %08x\n", serial);

                    break;
                }
                case 0x27: {
                    int reason = read_uint8(&p, end);
                    switch (reason)
                    {

                        case 0: printf("reject pick up: cannot lift this!\n"); break;
                        case 1: printf("reject pick up: out of range\n"); break;
                        case 2: printf("reject pick up: out of sight\n"); break;
                        case 3: printf("reject pick up: belongs to someone else. you have to steal it!\n"); break;
                        case 4: printf("reject pick up: already holding something\n"); break;
                        default: printf("reject pick up: unknown reason\n"); break;
                    }   
                    game_pick_up_rejected();
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

                        gump_t *container = game_get_container(cont_serial);

                        item_t *item = game_get_item(serial);
                        //item_id == 0 means just inited
                        printf("item_id: %d, space: %d\n", item->item_id, item->space);
                        assert(item->item_id == 0 || item->space == SPACETYPE_CONTAINER);
                        item->space = SPACETYPE_CONTAINER;
                        item->loc.container.container = container;
                        item->loc.container.x = x;
                        item->loc.container.y = y;
                        item->item_id = item_id;
                        item->hue_id = hue_id;

                        container->container.items->push_back(item);

                        printf("adding item to container: %08x\n", serial);
                    }
                    break;
                }
                case 0x6e: {
                    uint32_t mob_serial = read_uint32_be(&p, end);
                    int action_id = read_uint16_be(&p, end);
                    int frame_count = read_uint16_be(&p, end);
                    int repeat_count = read_uint16_be(&p, end);
                    bool forward = !read_uint8(&p, end);
                    bool do_repeat = read_uint8(&p, end) != 0;
                    int frame_delay = read_uint8(&p, end);

                    game_do_action(mob_serial, action_id, frame_count, repeat_count, forward, do_repeat, frame_delay);
                    break;
                }
                case 0x73: {
                    // ping response... nothing to do.
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
                    m->flags = read_uint8(&p, end);
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
                    m->flags = read_uint8(&p, end);
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
                case 0x88: {
                    uint32_t serial = read_uint32_be(&p, end);
                    char full_name[61];
                    read_ascii_fixed(&p, end, full_name, 60);
                    int flags = read_uint8(&p, end);

                    mobile_t *m = game_get_mobile(serial);
                    game_show_paperdoll(m, decode_utf8_cstr(full_name));

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
                    //printf("%d servers:\n", server_count);
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
                        //printf(" %s %s\n", name, ip_str);

                        if (server_count == 1)
                        {
                            printf("Only one server: %s. Connecting to it!\n", name);
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
                    //printf("%d chars slots:\n", char_slot_count);
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
                        //printf(" %d %s\n", occupied_slots[i], name);
                    }
                    //printf("%d chars\n", char_count);
                    int city_count = read_uint8(&p, end);

                    int client_70130_len = 11 + char_slot_count * 60 + city_count * 89;
                    int else_len = 11 + char_slot_count * 60 + city_count * 63;

                    bool sa_client = (packet_len == client_70130_len + 2) || (packet_len == else_len + 2);

                    bool client_70130;
                    client_70130 = packet_len == (client_70130_len + (sa_client ? 2 : 0));

                    //printf("%d start cities:\n", city_count);
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
                            //printf(" %d: %s\n", loc_id, name);
                        }
                        else
                        {
                            uint8_t loc_id = read_uint8(&p, end);
                            char name[32];
                            char area[32];
                            read_ascii_fixed(&p, end, name, 31);
                            read_ascii_fixed(&p, end, area, 31);
                            //printf(" %d: %s\n", loc_id, name);
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
                        printf("No chars on this account... create one!\n");
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
                case 0xae: {
                    uint32_t serial = read_uint32_be(&p, end);
                    int graphic = read_uint16_be(&p, end);
                    read_uint8(&p, end); // type
                    int hue = read_uint16_be(&p, end);
                    int font_id = read_uint16_be(&p, end);
                    char lang[5];
                    char name[31];
                    lang[4] = '\0';
                    name[30] = '\0';
                    read_ascii_fixed(&p, end, lang, 4);
                    read_ascii_fixed(&p, end, name, 30);
                    std::wstring s;
                    int remaining_bytes = end - p;
                    int remaining_chars = remaining_bytes / 2;
                    for (int i = 0; i < remaining_chars; i++)
                    {
                        s += read_uint16_be(&p, end);
                    }
                    // TODO: do something with this text...
                    printf("[%s] %s: ", lang, name);
                    std::wcout << s << std::endl;
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
                case 0xc1: {
                    uint32_t speaker_serial = read_uint32_be(&p, end);
                    int graphic = read_uint16_le(&p, end);
                    int type = read_uint8(&p, end);
                    int hue_id = read_uint16_be(&p, end);
                    int font_id = read_uint16_be(&p, end);
                    int cliloc_id = read_uint32_be(&p, end);
                    char speaker[31];
                    speaker[30] = '\0';
                    read_ascii_fixed(&p, end, speaker, 30);

                    std::wstring format = decode_utf8_cstr(ml_get_cliloc(cliloc_id));
                    std::wstring arg_str;
                    while (true)
                    {
                        // yes, this is little-endian in 0xc1
                        int c = read_uint16_le(&p, end);
                        if (c == 0)
                        {
                            break;
                        }
                        else
                        {
                            arg_str += c;
                        }
                    }
                    std::vector<std::wstring> args = split(arg_str, L'\t');
                    std::wstring res = cliloc_format_resolve(format, args);
                    std::wcout << speaker << ": " << res << std::endl;

                    break;
                }
                case 0xcc: {
                    uint32_t speaker_serial = read_uint32_be(&p, end);
                    int graphic_id = read_uint16_le(&p, end);
                    int type = read_uint8(&p, end);
                    int hue_id = read_uint16_be(&p, end);
                    int font_id = read_uint16_be(&p, end);
                    int cliloc_id = read_uint32_be(&p, end);
                    int flags = read_uint8(&p, end);

                    char speaker[31];
                    speaker[30] = '\0';
                    read_ascii_fixed(&p, end, speaker, 30);

                    std::wstring affix;
                    while (true)
                    {
                        int c = read_uint8(&p, end);
                        if (c == 0)
                        {
                            break;
                        }
                        else
                        {
                            affix += c;
                        }
                    }

                    std::wstring format = decode_utf8_cstr(ml_get_cliloc(cliloc_id));
                    std::wstring arg_str;
                    while (true)
                    {
                        // big-endian in 0xcc
                        int c = read_uint16_be(&p, end);
                        if (c == 0)
                        {
                            break;
                        }
                        else
                        {
                            arg_str += c;
                        }
                    }
                    std::vector<std::wstring> args = split(arg_str, L'\t');
                    std::wstring res = cliloc_format_resolve(format, args);

                    // prepend or append?
                    if (flags & 0x1)
                    {
                        res = affix + res;
                    }
                    else
                    {
                        res += affix;
                    }

                    std::wcout << speaker << ": " << res << std::endl;

                    break;
                }
                case 0xdd: {
                    uint32_t serial = read_uint32_be(&p, end);
                    uint32_t gump_type_id = read_uint32_be(&p, end);
                    int x = read_uint32_be(&p, end);
                    int y = read_uint32_be(&p, end);

                    std::list<gump_command_t> commands;
                    std::vector<std::wstring> lines;

                    // read commands
                    {
                        unsigned long decompressed_layout_length;
                        char *decompressed_layout_data;

                        int compressed_layout_length = read_sint32_be(&p, end)-4;
                        printf("compressed_layout_length: %d\n", compressed_layout_length);
                        decompressed_layout_length = read_sint32_be(&p, end);
                        printf("decompressed_layout_length: %d\n", (int)decompressed_layout_length);

                        char *compressed_layout_data = (char *)malloc(compressed_layout_length);
                        read_chunk(&p, end, compressed_layout_data, compressed_layout_length);

                        decompressed_layout_data = (char *)malloc(decompressed_layout_length);
                        assert(uncompress((unsigned char *)decompressed_layout_data, &decompressed_layout_length,
                                          (unsigned char *)compressed_layout_data  , compressed_layout_length) == Z_OK);

                        std::wstring all_commands_str = decode_utf8_cstr(decompressed_layout_data);
                        commands = parse_gump_commands(all_commands_str);
                        printf("commands: %d\n", (int)commands.size());

                        free(compressed_layout_data);
                        free(decompressed_layout_data);
                    }

                    // read lines
                    {
                        int text_line_count = read_uint32_be(&p, end);

                        int compressed_text_length = read_sint32_be(&p, end) - 4;

                        if (compressed_text_length > 0)
                        {
                            unsigned long decompressed_text_length;
                            char *decompressed_text_data;

                            decompressed_text_length = read_sint32_be(&p, end);

                            char *compressed_text_data = (char *)malloc(compressed_text_length);
                            decompressed_text_data = (char *)malloc(decompressed_text_length);
                            read_chunk(&p, end, compressed_text_data, compressed_text_length);

                            assert(uncompress((unsigned char *)decompressed_text_data, &decompressed_text_length,
                                              (unsigned char *)compressed_text_data  , compressed_text_length) == Z_OK);

                            {
                                const char *tp = decompressed_text_data;
                                const char *tend = decompressed_text_data + decompressed_text_length;
                                for (int i = 0; i < text_line_count; i++)
                                {
                                    std::wstring s;
                                    int len = read_uint16_be(&tp, tend);
                                    for (int j = 0; j < len; j++)
                                    {
                                        int c = read_uint16_be(&tp, tend);
                                        s += c;
                                    }
                                    lines.push_back(s);
                                }
                            }

                            free(compressed_text_data);
                            free(decompressed_text_data);
                        }
                        else
                        {
                            compressed_text_length = 0;
                        }
                    }

                    // parse commands
                    {
                        int current_page = 0;

                        gump_t *gump = game_create_generic_gump(serial, gump_type_id, x, y);
                        for (std::list<gump_command_t>::iterator it = commands.begin(); it != commands.end(); ++it)
                        {
                            gump_command_t command = *it;
                            if (command.type == GUMPCMD_PAGE)
                            {
                                current_page = command.page.page_no;
                            }
                            else if (command.type == GUMPCMD_PIC)
                            {
                                gump_widget_t widget;
                                widget.page = current_page;
                                widget.type = GUMPWTYPE_PIC;
                                widget.pic.x = command.pic.x;
                                widget.pic.y = command.pic.y;
                                widget.pic.gump_id = command.pic.gump_id;
                                gump->generic.widgets->push_back(widget);
                            }
                            else if (command.type == GUMPCMD_PICTILED)
                            {
                                gump_widget_t widget;
                                widget.page = current_page;
                                widget.type = GUMPWTYPE_PICTILED;
                                widget.pictiled.x = command.pictiled.x;
                                widget.pictiled.y = command.pictiled.y;
                                widget.pictiled.width = command.pictiled.width;
                                widget.pictiled.height = command.pictiled.height;
                                widget.pictiled.gump_id = command.pictiled.gump_id;
                                gump->generic.widgets->push_back(widget);
                            }
                            else if (command.type == GUMPCMD_RESIZEPIC)
                            {
                                gump_widget_t widget;
                                widget.page = current_page;
                                widget.type = GUMPWTYPE_RESIZEPIC;
                                widget.resizepic.x = command.resizepic.x;
                                widget.resizepic.y = command.resizepic.y;
                                widget.resizepic.width = command.resizepic.width;
                                widget.resizepic.height = command.resizepic.height;
                                widget.resizepic.gump_id_base = command.resizepic.gump_id_base;
                                gump->generic.widgets->push_back(widget);
                            }
                            else if (command.type == GUMPCMD_BUTTON)
                            {
                                gump_widget_t widget;
                                widget.page = current_page;
                                widget.type = GUMPWTYPE_BUTTON;
                                widget.button.x = command.button.x;
                                widget.button.y = command.button.y;
                                widget.button.up_gump_id = command.button.up_gump_id;
                                widget.button.down_gump_id = command.button.down_gump_id;
                                widget.button.type = command.button.type;
                                widget.button.param = command.button.param;
                                widget.button.button_id = command.button.button_id;
                                gump->generic.widgets->push_back(widget);
                            }
                            else if (command.type == GUMPCMD_ITEM)
                            {
                                gump_widget_t widget;
                                widget.page = current_page;
                                widget.type = GUMPWTYPE_ITEM;
                                widget.item.x = command.item.x;
                                widget.item.y = command.item.y;
                                widget.item.item_id = command.item.item_id;
                                widget.item.hue_id = command.item.hue_id;
                                gump->generic.widgets->push_back(widget);
                            }
                            else if (command.type == GUMPCMD_LABEL)
                            {
                                int font_id = 1;
                                gump_widget_t widget;
                                widget.page = current_page;
                                widget.type = GUMPWTYPE_TEXT;
                                widget.text.x = command.label.x;
                                widget.text.y = command.label.y;
                                widget.text.font_id = font_id;
                                widget.text.text = new std::wstring;
                                *widget.text.text = lines[command.label.text_id];
                                gump->generic.widgets->push_back(widget);
                            }
                            else if (command.type == GUMPCMD_FREETEXT)
                            {
                                int font_id = 1;
                                std::list<std::wstring> strs = massage_text(font_id, command.free_text.width, lines[command.free_text.text_id]);
                                int y = command.free_text.y;
                                int line_height = 20;
                                for (std::list<std::wstring>::iterator it = strs.begin(); it != strs.end(); ++it)
                                {
                                    gump_widget_t widget;
                                    widget.page = current_page;
                                    widget.type = GUMPWTYPE_TEXT;
                                    widget.text.x = command.free_text.x;
                                    widget.text.y = y;
                                    widget.text.font_id = font_id;
                                    widget.text.text = new std::wstring();
                                    *widget.text.text = *it;
                                    gump->generic.widgets->push_back(widget);

                                    y += line_height;
                                }
                            }
                            else if (command.type == GUMPCMD_LOCALIZED)
                            {
                                //printf("%d\n", command.localized.cliloc_id);
                                //printf("%s\n", ml_get_cliloc(command.localized.cliloc_id));
                                std::wstring format = decode_utf8_cstr(ml_get_cliloc(command.localized.cliloc_id));
                                std::vector<std::wstring> args = split(*command.localized.arg_str, L'\t');
                                delete command.localized.arg_str;
                                std::wstring res = cliloc_format_resolve(format, args);
                                int font_id = 1;
                                std::list<std::wstring> strs = massage_text(font_id, command.localized.width, res);
                                int y = command.localized.y;
                                int line_height = 20;
                                for (std::list<std::wstring>::iterator it = strs.begin(); it != strs.end(); ++it)
                                {
                                    gump_widget_t widget;
                                    widget.page = current_page;
                                    widget.type = GUMPWTYPE_TEXT;
                                    widget.text.x = command.localized.x;
                                    widget.text.y = y;
                                    widget.text.font_id = font_id;
                                    widget.text.text = new std::wstring();
                                    *widget.text.text = *it;
                                    gump->generic.widgets->push_back(widget);

                                    y += line_height;
                                }
                            }
                            else
                            {
                                //assert(0 && "unhandled command");
                            }
                        }
                    }

                    break;
                }
                case 0xf0: {
                    int type = read_uint8(&p, end);
                    if (type == 0xfe)
                    {
                        uint64_t features_disallowed = read_uint64_le(&p, end);
                        printf("received razor feature control packet. features disallowed: %lx\n", features_disallowed);
                        net_send_razor_ack();
                    }

                    break;
                }
                case 0xf3: {
                    read_uint16_be(&p, end); // unknown
                    int type = read_uint8(&p, end); // 0 = item, 1 = multi
                    uint32_t serial = read_uint32_be(&p, end);
                    int graphic_id = read_uint16_be(&p, end);
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
                        item->item_id = graphic_id;
                        item->space = SPACETYPE_WORLD;
                        item->loc.world.x = x;
                        item->loc.world.y = y;
                        item->loc.world.z = z;
                        item->hue_id = hue_id;
                    }
                    else
                    {
                        printf("OMGOGMOGMOGM MULTI?!?!\n");
                        multi_t *multi = game_get_multi(serial);
                        multi->x = x;
                        multi->y = y;
                        multi->z = z;
                        multi->multi_id = graphic_id;
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
    sockfd = net_connect(LOGIN_ADDRESS, LOGIN_PORT);
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
    packet_lengths[0x23] = 26; // display drag effect
    packet_lengths[0x24] = 7; // display container
    packet_lengths[0x25] = 21; // update container
    packet_lengths[0x27] = 2; // reject pick up
    packet_lengths[0x2c] = 2; // death status
    packet_lengths[0x2e] = 15; // equip update
    packet_lengths[0x2f] = 10; // fight occurring (swing)
    packet_lengths[0x3a] = 0; // skills
    packet_lengths[0x3c] = 0; // container contents
    packet_lengths[0x4e] = 6; // set personal light level
    packet_lengths[0x4f] = 2; // set overall light level
    packet_lengths[0x54] = 12; // play sound
    packet_lengths[0x55] = 1; // login complete
    packet_lengths[0x5b] = 4; // server time
    packet_lengths[0x65] = 4; // weather change
    packet_lengths[0x66] = 0; // book content changed
    packet_lengths[0x6d] = 3; // play music
    packet_lengths[0x6e] = 14; // character animation
    packet_lengths[0x72] = 5; // set war mode
    packet_lengths[0x73] = 2; // ping response
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
    packet_lengths[0xc8] = 2; // update range
    packet_lengths[0xcc] = 0; // cliloc message, affix
    packet_lengths[0xd4] = 0; // book header
    packet_lengths[0xdc] = 9; // item revision
    packet_lengths[0xdd] = 0; // display gump packed
    packet_lengths[0xdf] = 0; // "buff/debuff system" <- what is this?
    packet_lengths[0xf0] = 0; // protocol extensions
    packet_lengths[0xf3] = 24; // add item (new version)

    assert(!inited);
    inited = true;
}

void net_shutdown()
{
    if (sockfd != -1)
    {
        close(sockfd);
        sockfd = -1;
    }

    inited = false;
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

