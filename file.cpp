#include <cassert>
#include <cstdio>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

static long filesize(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    assert(f != NULL);
    fseek(f, 0, SEEK_SET);
    long beg = ftell(f);
    fseek(f, 0, SEEK_END);
    long end = ftell(f);
    fclose(f);
    return end-beg;
}

const char *file_map(const char *filename, const char **end)
{
    long size = filesize(filename);
    int fd = open(filename, O_RDONLY);
    assert(fd != -1);
    const char *p = (const char *)mmap(NULL, size, PROT_READ, MAP_FILE|MAP_PRIVATE, fd, 0);
    assert(p != MAP_FAILED);
    assert(close(fd) == 0);

    *end = p + size;

    return p;
}

void file_unmap(const char *p, const char *end)
{
    long size = end - p;

    assert(munmap((void *)p, size) == 0);
}
