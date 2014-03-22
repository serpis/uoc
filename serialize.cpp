#include <assert.h>
#include <stdint.h>

void write_uint8(char **p, const char *end, uint8_t b)
{
    assert(*p < end);
    **p = b;
    *p += 1;
}

void write_sint8(char **p, const char *end, int8_t b)
{
    assert(*p < end);
    **p = b;
    *p += 1;
}

void write_uint16_be(char **p, const char *end, uint16_t u)
{
    write_uint8(p, end, u >> 8);
    write_uint8(p, end, u & 0xff);
}

void write_uint32_be(char **p, const char *end, uint32_t u)
{
    write_uint8(p, end, (u >> 24) & 0xff);
    write_uint8(p, end, (u >> 16) & 0xff);
    write_uint8(p, end, (u >> 8) & 0xff);
    write_uint8(p, end, u & 0xff);
}


uint8_t read_uint8(const char **p, const char *end)
{
    assert(*p < end);
    uint8_t res = **p;
    *p += 1;
    return res;
}

int8_t read_sint8(const char **p, const char *end)
{
    return (int8_t)read_uint8(p, end);
}

int16_t read_sint16_be(const char **p, const char *end)
{
    int16_t t = 0;
    // big-endian
    t |= read_uint8(p, end) << 8;
    t |= read_uint8(p, end);
    return t;
}

uint16_t read_uint16_be(const char **p, const char *end)
{
    return (uint16_t)read_sint16_be(p, end);
}

int32_t read_sint32_be(const char **p, const char *end)
{
    int32_t t = 0;
    // big-endian
    t |= read_uint8(p, end) << 24;
    t |= read_uint8(p, end) << 16;
    t |= read_uint8(p, end) << 8;
    t |= read_uint8(p, end);
    return t;
}

uint32_t read_uint32_be(const char **p, const char *end)
{
    return (uint32_t)read_sint32_be(p, end);
}


int16_t read_sint16_le(const char **p, const char *end)
{
    int16_t t = 0;
    // little-endian
    t |= read_uint8(p, end);
    t |= read_uint8(p, end) << 8;
    return t;
}

uint16_t read_uint16_le(const char **p, const char *end)
{
    return (uint16_t)read_sint16_le(p, end);
}

int32_t read_sint32_le(const char **p, const char *end)
{
    int32_t t = 0;
    // little-endian
    t |= read_uint8(p, end);
    t |= read_uint8(p, end) << 8;
    t |= read_uint8(p, end) << 16;
    t |= read_uint8(p, end) << 24;
    return t;
}

uint32_t read_uint32_le(const char **p, const char *end)
{
    return (uint32_t)read_sint32_le(p, end);
}

int64_t  read_sint64_le   (const char **p, const char *end)
{
    int64_t t = 0;
    // little-endian
    t |= ((uint64_t)read_uint8(p, end));
    t |= ((uint64_t)read_uint8(p, end)) << 8;
    t |= ((uint64_t)read_uint8(p, end)) << 16;
    t |= ((uint64_t)read_uint8(p, end)) << 24;
    t |= ((uint64_t)read_uint8(p, end)) << 32;
    t |= ((uint64_t)read_uint8(p, end)) << 40;
    t |= ((uint64_t)read_uint8(p, end)) << 48;
    t |= ((uint64_t)read_uint8(p, end)) << 56;
    return t;
}

uint64_t read_uint64_le   (const char **p, const char *end)
{
    return (uint64_t)read_sint64_le(p, end);
}

void read_ascii_fixed(const char **p, const char *end, char *s, int n)
{
    for (int i = 0; i < n; i++)
    {
        *s = read_uint8(p, end);
        s += 1;
    }
    // important: null terminate
    *s = '\0';
}

void write_ascii_fixed(char **p, const char *end, const char *s, int n)
{
    for (int i = 0; i < n; i++)
    {
        write_uint8(p, end, *s);
        if (*s)
        {
            s += 1;
        }
    }
}
