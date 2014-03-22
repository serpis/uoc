#ifndef _SERIALIZE_HPP
#define _SERIALIZE_HPP

#include <stdint.h>

int8_t   read_sint8       (const char **p, const char *end);
uint8_t  read_uint8       (const char **p, const char *end);
int16_t  read_sint16_be   (const char **p, const char *end);
uint16_t read_uint16_be   (const char **p, const char *end);
int32_t  read_sint32_be   (const char **p, const char *end);
uint32_t read_uint32_be   (const char **p, const char *end);
int16_t  read_sint16_le   (const char **p, const char *end);
uint16_t read_uint16_le   (const char **p, const char *end);
int32_t  read_sint32_le   (const char **p, const char *end);
uint32_t read_uint32_le   (const char **p, const char *end);
int64_t  read_sint64_le   (const char **p, const char *end);
uint64_t read_uint64_le   (const char **p, const char *end);
void     read_ascii_fixed (const char **p, const char *end, char *s, int n);
void     write_sint8      (char **p, const char *end, int8_t b);
void     write_uint8      (char **p, const char *end, uint8_t b);
void     write_uint16_be  (char **p, const char *end, uint16_t u);
void     write_uint32_be  (char **p, const char *end, uint32_t u);
void     write_ascii_fixed(char **p, const char *end, const char *s, int n);

#endif

