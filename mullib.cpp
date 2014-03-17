#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include <stdint.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>

#include "file.hpp"
#include "mullib.hpp"
#include "serialize.hpp"



// state of this module
static ml_tile_data_entry *tile_datas            = NULL;
static int                 item_data_entry_count = 0;
static ml_item_data_entry *item_datas            = NULL;
static ml_hue             *hues                  = NULL;

static ml_index           *anim_idx              = NULL;
static ml_index           *art_idx               = NULL;
static ml_index           *gump_idx              = NULL;
static ml_index           *statics0_blocks_idx   = NULL;
static ml_index           *statics1_blocks_idx   = NULL;

static bool ml_inited = false;
static bool mlt_inited = false;


// first a bunch of low level functions...
// at bottom of file are exposed easy-to-use functions


static void parse_anim(const char *p, const char *end, ml_anim **animation)
{
    //printf("parsing... offset: %d, length: %d\n", offset, length);

    //p += offset;

    uint16_t palette[0x100];
    int frame_count;
    int frame_start_offsets[64];

    int total_frames_size;

    for (int i = 0; i < 0x100; i++)
    {
        palette[i] = read_uint16_le(&p, end);
        // set alpha
        if (palette[i]) { palette[i] |= 0x8000; }
        //printf("palette %d %d\n", i, palette[i]);
    }

    const char *payload_start = p;

    frame_count = read_sint32_le(&p, end);
    assert(frame_count <= sizeof(frame_start_offsets)/sizeof(frame_start_offsets[0]));
    for (int i = 0; i < frame_count; i++)
    {
        frame_start_offsets[i] = read_sint32_le(&p, end);
        //printf("offset %d %d\n", i, frame_start_offsets[i]);
    }

    total_frames_size = 0;
    for (int i = 0; i < frame_count; i++)
    {
        if (frame_start_offsets[i] != 0)
        {
            p = payload_start + frame_start_offsets[i] + 4;

            int width = read_sint16_le(&p, end);
            int height = read_sint16_le(&p, end);

            total_frames_size += 2 * width * height;
        }
    }

    //printf("frames size: %d\n", total_frames_size);

    int anim_meta_data_size = sizeof(ml_anim) + frame_count * sizeof((*animation)->frames[0]);
    int anim_size = anim_meta_data_size + total_frames_size;
    //printf("this animation is %d bytes\n", anim_size);
    *animation = (ml_anim *)malloc(anim_size);
    ml_anim *anim = *animation;
    anim->frame_count = frame_count;

    char *frame_data_start = ((char *)anim) + anim_meta_data_size;

    int offset_accum = 0;
    for (int i = 0; i < frame_count; i++)
    {
        //printf("frame %d of %d\n", i, frame_count);
        p = payload_start + frame_start_offsets[i];

        int center_x = 0;
        int center_y = 0;

        int width = 0;
        int height = 0;

        if (frame_start_offsets[i] != 0)
        {
            center_x = read_sint16_le(&p, end);
            center_y = read_sint16_le(&p, end);
            
            width = read_sint16_le(&p, end);
            height = read_sint16_le(&p, end);
        }

        anim->frames[i].center_x = center_x;
        anim->frames[i].center_y = center_y;
        anim->frames[i].width = width;
        anim->frames[i].height = height;
        anim->frames[i].data = (uint16_t *)(frame_data_start + offset_accum);
        offset_accum += 2 * width * height;

        uint16_t *target_data = anim->frames[i].data;
        memset(target_data, 0, 2 * width * height);

        //printf("%d %d %d %d\n", center_x, center_y, width, height);

        if (frame_start_offsets[i] != 0)
        {
            while (true)
            {
                uint32_t header = read_uint32_le(&p, end);
                //printf("header %08x\n", header);
                if (header == 0x7FFF7FFFul)
                {
                    // finished...
                    break;
                }
                int offset_x = ((header >> 22) ^ 0x200) - 0x200;
                int offset_y = (((header >> 12) & 0x3ff) ^ 0x200) - 0x200;
                int run = header & 0xfff;

                int start_x = center_x + offset_x;
                int start_y = center_y + height + offset_y;

                uint16_t *w = (uint16_t *)&target_data[start_x + start_y * width];
                uint16_t *wend = w + width * height;

                //printf("%d %d\n", width, height);
                //printf("%d %d %d\n", start_x, start_y, run);

                for (int j = 0; j < run; j++)
                {
                    assert(w < wend);
                    *w = palette[read_uint8(&p, end)];
                    w += 1;
                }

                //printf("%d %d %d\n", start_x, start_y, run);
            }
        }
    }

    // when we are done we should have written every byte we allocated
    assert(anim_meta_data_size + offset_accum == anim_size);

    //printf("read %d\n", (int)((p - payload_start) + 0x200));

}

static void anim(int offset, int length, ml_anim **animation)
{
    const char *end;
    const char *p = file_map("files/anim.mul", &end);

    assert(offset >= 0);
    assert(length >= 0);
    assert(p + offset + length <= end);

    //printf("offset %d length %d\n", offset, length);

    // do stuff...
    parse_anim(p + offset, p + offset + length, animation);

    file_unmap(p, end);
}

static void parse_stat(const char *p, const char *end, ml_art **art)
{
    int header = read_sint32_le(&p, end);
    int width = read_sint16_le(&p, end);
    int height = read_sint16_le(&p, end);

    const char *line_start_offsets[512];

    const char *start = p + height * 2;

    //printf("%d %d\n", width, height);
    assert(height <= sizeof(line_start_offsets)/sizeof(line_start_offsets[0]));

    for (int i = 0; i < height; i++)
    {
        line_start_offsets[i] = start + 2 * read_uint16_le(&p, end);
    }

    int art_size = sizeof(ml_art) + 2 * width * height;

    *art = (ml_art *)malloc(art_size);

    (*art)->width = width;
    (*art)->height = height;

    uint16_t *target_data = (*art)->data;
    memset(target_data, 0, 2 * width * height);

    for (int i = 0; i < height; i++)
    {
        uint16_t *w = target_data + i * width;
        p = line_start_offsets[i];

        while (true)
        {
            int skip = read_sint16_le(&p, end);
            int length = read_sint16_le(&p, end);

            if (skip == 0 && length == 0)
            {
                break;
            }

            w += skip;
            for (int j = 0; j < length; j++)
            {
                *w = read_uint16_le(&p, end);
                // set alpha...
                if (*w) { *w |= 0x8000; }
                w += 1;
            }
        }
    }
}

static void stat(int offset, int length, ml_art **art)
{
    const char *end;
    const char *p = file_map("files/art.mul", &end);

    assert(offset >= 0);
    assert(length >= 0);
    assert(p + offset + length <= end);

    // do stuff...
    parse_stat(p + offset, p + offset + length, art);

    file_unmap(p, end);
}

static void parse_land(const char *p, const char *end, ml_art **art, bool rotate)
{
    int width = 44;
    int height = 44;

    uint16_t data[44*44];
    // for debugging, fill with magenta.
    for (int i = 0; i < 44*44; i++)
    {
        uint16_t color = (0x1f << 10) | 0x1f;
        data[i] = color;
    }

    for (int i = 0; i < height; i++)
    {
        int offset_x = 0;
        if (i < 22) offset_x = 21 - i;
        else        offset_x = i - 22;
        int length = 2 * (22 - offset_x);

        uint16_t *w = data + i * width + offset_x;

        for (int j = 0; j < length; j++)
        {
            //assert((const char *)w < end);
            *w = read_sint16_le(&p, end);
            w += 1;
        }
    }

    if (!rotate)
    {
        int art_size = sizeof(ml_art) + 2 * width * height;

        *art = (ml_art *)malloc(art_size);

        (*art)->width = width;
        (*art)->height = height;
        uint16_t *target_data = (*art)->data;
        memcpy(target_data, data, 2 * width * height);
        return;
    }

    // ok we read the land tile as 44x44... let's rotate and map to 32x32!

    // will use bilinear..
    // want to map ( 0,  0) -> (21.5,    0)
    //             (31,  0) -> (43  , 21.5)
    //             ( 0, 31) -> (0   , 21.5)
    //             (31, 31) -> (21.5,   43)
    //
    {
        int res_width = 32;
        int res_height = 32;

        int art_size = sizeof(ml_art) + 2 * res_width * res_height;

        *art = (ml_art *)malloc(art_size);

        (*art)->width = res_width;
        (*art)->height = res_height;
        uint16_t *target_data = (*art)->data;

        for (int y = 0; y < res_height; y++)
        for (int x = 0; x < res_width ; x++)
        {
            // TODO: most of the calculations here are always the same
            // it is probably a good idea to only run that code once.
            float sample_x = 21.5 + x*21.5/31.0 - y*21.5/31.0;
            float sample_y = x*21.5/31.0 + y*21.5/31.0;

            int sample_x0 = sample_x;
            int sample_x1 = sample_x+1;
            int sample_y0 = sample_y;
            int sample_y1 = sample_y+1;

            float x0_fact = sample_x1 - sample_x;
            float x1_fact = sample_x  - sample_x0;
            float y0_fact = sample_y1 - sample_y;
            float y1_fact = sample_y  - sample_y0;

            float p0_fact = x0_fact * y0_fact;
            float p1_fact = x1_fact * y0_fact;
            float p2_fact = x0_fact * y1_fact;
            float p3_fact = x1_fact * y1_fact;

            if      (sample_x0 <  0) sample_x0 = 0;
            else if (sample_x0 > 43) sample_x0 = 43;
            if      (sample_x1 <  0) sample_x1 = 0;
            else if (sample_x1 > 43) sample_x1 = 43;
            if      (sample_y0 <  0) sample_y0 = 0;
            else if (sample_y0 > 43) sample_y0 = 43;
            if      (sample_y1 <  0) sample_y1 = 0;
            else if (sample_y1 > 43) sample_y1 = 43;

            int xs[4] = { sample_x0, sample_x1, sample_x0, sample_x1 };
            int ys[4] = { sample_y0, sample_y0, sample_y1, sample_y1 };
            float factor[4] = { p0_fact, p1_fact, p2_fact, p3_fact };

            float accum_r = 0.0f;
            float accum_g = 0.0f;
            float accum_b = 0.0f;
            for (int i = 0; i < 4; i++)
            {
                uint16_t col = data[xs[i] + ys[i] * 44];
                int cr = ((col >> 10) & 0x1f);
                int cg = ((col >>  5) & 0x1f);
                int cb = ((col >>  0) & 0x1f);
                accum_r += factor[i] * cr;
                accum_g += factor[i] * cg;
                accum_b += factor[i] * cb;
            }
            int r = (int)accum_r;
            if (r > 31) r = 31;
            int g = (int)accum_g;
            if (g > 31) g = 31;
            int b = (int)accum_b;
            if (b > 31) b = 31;

            uint16_t assemble_color = 0x8000 | (r << 10) | (g << 5) | b; 

            target_data[x + y * 32] = assemble_color;
        }
    }
}

static void land(int offset, int length, ml_art **art, bool rotate)
{
    const char *end;
    const char *p = file_map("files/art.mul", &end);

    assert(offset >= 0);
    assert(length >= 0);
    assert(p + offset + length <= end);

    // do stuff...
    parse_land(p + offset, p + offset + length, art, rotate);

    file_unmap(p, end);
}

static void parse_gump(const char *p, const char *end, int width, int height, ml_gump **g)
{
    //printf("gump %d %d\n", width, height);
    int line_start_offsets[256];
    assert(height < sizeof(line_start_offsets)/sizeof(line_start_offsets[0]));

    const char *start = p;

    for (int i = 0; i < height; i++)
    {
        line_start_offsets[i] = read_uint32_le(&p, end);
        //printf("%d %d\n", i, line_start_offsets[i]);
    }

    int gump_size = sizeof(ml_gump) + 2 * width * height;
    *g = (ml_gump *)malloc(gump_size);
    (*g)->width = width;
    (*g)->height = height;
    uint16_t *target_data = (*g)->data;
    memset(target_data, 0, 2 * width * height);

    for (int i = 0; i < height; i++)
    {
        uint16_t *w = target_data + i * width;
        p = start + line_start_offsets[i] * 4;

        int accum_length = 0;

        while (accum_length < width)
        {
            uint16_t color = read_uint16_le(&p, end);
            int length = read_uint16_le(&p, end);

            for (int j = 0; j < length; j++)
            {
                *w = color;
                // set alpha...
                if (*w) { *w |= 0x8000; }
                w += 1;
            }


            accum_length += length;
        }
    }
}

static void gump(int offset, int length, int width, int height, ml_gump **g)
{
    const char *end;
    const char *p = file_map("files/Gumpart.mul", &end);

    assert(offset >= 0);
    assert(length >= 0);
    assert(p + offset + length <= end);

    // do stuff...
    parse_gump(p + offset, p + offset + length, width, height, g);

    file_unmap(p, end);
}

static void parse_land_block(const char *p, const char *end, ml_land_block **mb)
{
    int header = read_sint32_le(&p, end);

    *mb = (ml_land_block *)malloc(sizeof(ml_land_block));

    for (int i = 0; i < 8*8; i++)
    {
        int tile_id = read_uint16_le(&p, end);
        int z = read_sint8(&p, end);
        (*mb)->tiles[i].tile_id = tile_id;
        (*mb)->tiles[i].z       = z;
    }
}

static void land_block(int map, int offset, int length, ml_land_block **mb)
{
    assert(map == 0 || map == 1);

    const char *end;
    const char *p;

    if (map == 0)
    {
        p = file_map("files/map0.mul", &end);
    } else if (map == 1)
    {
        p = file_map("files/map1.mul", &end);
    }

    assert(offset >= 0);
    assert(length >= 0);
    assert(p + offset + length <= end);

    parse_land_block(p + offset, p + offset + length, mb);

    file_unmap(p, end);
}

static void parse_statics_block(const char *p, const char *end, ml_statics_block **sb)
{
    long size = end - p;
    int statics_count = size / 7;

    int statics_block_size = sizeof(ml_statics_block) + statics_count * sizeof((*sb)->statics[0]);

    *sb = (ml_statics_block *)malloc(statics_block_size);
    (*sb)->statics_count = statics_count;

    for (int i = 0; i < 8*8; i++)
    {
        (*sb)->roof_heights[i] = -1;
    }

    //printf("statics_count. %d\n", statics_count);

    for (int i = 0; i < statics_count; i++)
    {
        int tile_id = read_sint16_le(&p, end);
        int dx = read_uint8(&p, end);
        int dy = read_uint8(&p, end);
        int z = read_sint8(&p, end);
        read_uint16_le(&p, end); // unknown
        (*sb)->statics[i].tile_id = tile_id;
        (*sb)->statics[i].dx = dx;
        (*sb)->statics[i].dy = dy;
        (*sb)->statics[i].z = z;

        ml_item_data_entry *item_data = ml_get_item_data(tile_id);
        if (item_data->flags & 0x10000000)
        {
            // is roof!
            if ((*sb)->roof_heights[dx+dy*8] == -1 || z > (*sb)->roof_heights[dx+dy*8]) // higher than existing roof?
            {
                (*sb)->roof_heights[dx+dy*8] = z;
            }
        }

        //printf("%d %d %d %d\n", tile_id, dx, dy, z);
    }
}

static void statics_block(int map, int offset, int length, ml_statics_block **sb)
{
    assert(map == 0 || map == 1);

    const char *end;
    const char *p;

    if (map == 0)
    {
        p = file_map("files/statics0.mul", &end);
    } else if (map == 1)
    {
        p = file_map("files/statics1.mul", &end);
    }

    assert(offset >= 0);
    assert(length >= 0);
    assert(p + offset + length <= end);

    parse_statics_block(p + offset, p + offset + length, sb);

    file_unmap(p, end);
}

/*static void parse_bodyconv(const char *p, const char *end)
{
    char line[128];
    while (true)
    {
        const char *lstart = p;
        while (*p != '\n' && p < end)
        {
            p += 1;
        }
        const char *lend = p;

        long line_length = lend - lstart;
        assert(line_length < sizeof(line) - 1);

        memcpy(line, lstart, line_length);
        line[line_length] = '\0';

        //printf("line: %s\n", line);
        
        int anim1, anim2, anim3, anim4, anim5;
        if (sscanf(line, "%d %d %d %d %d", &anim1, &anim2, &anim3, &anim4, &anim5) == 5) 
        {
            //printf("%d %d %d %d %d\n", anim1, anim2, anim3, anim4, anim5);

            // only one of anim[2-5] is allowed to be != -1
            assert(anim1 >= 0 && anim1 < 2048 && // anim1 in range
                   ((anim2 == -1 && anim3 == -1 && anim4 == -1 && anim5 == -1) || // no remapping...
                    (anim2 >= 0 && anim2 < 2048 && anim3 == -1 && anim4 == -1 && anim5 == -1) || // anim2 is the remapped one?
                    (anim3 >= 0 && anim3 < 2048 && anim2 == -1 && anim4 == -1 && anim5 == -1) || // anim3 is the remapped one?
                    (anim4 >= 0 && anim4 < 2048 && anim2 == -1 && anim3 == -1 && anim5 == -1) || // anim4 is the remapped one?
                    (anim5 >= 0 && anim5 < 2048 && anim2 == -1 && anim3 == -1 && anim4 == -1))); // anim5 is the remapped one?
            int anim_file = -1, anim_id = -1;
            if (anim2 != -1)
            {
                anim_file = 2;
                anim_id = anim2;
            }
            if (anim3 != -1)
            {
                anim_file = 3;
                anim_id = anim3;
            }
            if (anim4 != -1)
            {
                anim_file = 4;
                anim_id = anim4;
            }
            if (anim5 != -1)
            {
                anim_file = 5;
                anim_id = anim5;
            }
            if (anim_file != -1)
            {
                anim_remap_table[anim1].anim_file = anim_file;
                anim_remap_table[anim1].anim_id = anim_id;

                //printf("remapping %d -> %d %d\n", anim1, anim_file, anim_id);
            }
        }

        // reached end of file?
        if (p == end)
        {
            break;
        }
        else
        {
            p += 1;
        }
    }
}

static void bodyconv()
{
    const char *end;
    const char *p = file_map("files/Bodyconv.def", &end);

    parse_bodyconv(p, end);

    file_unmap(p, end);
}*/

static void parse_index(const char *p, const char *end, ml_index **idx)
{
    long size = end - p;
    int entry_count = size / 12;

    int index_size = sizeof(ml_index) + entry_count * sizeof((*idx)->entries[0]);

    *idx = (ml_index *)malloc(index_size);
    (*idx)->entry_count = entry_count;
    for (int i = 0; i < entry_count; i++)
    {
        int32_t offset = read_sint32_le(&p, end);
        int32_t length = read_sint32_le(&p, end);
        int32_t extra = read_sint32_le(&p, end);
        (*idx)->entries[i].offset = offset;
        (*idx)->entries[i].length = length;
        (*idx)->entries[i].extra = extra;
        //printf("%d: offset: %d, length: %d, extra: %d\n", i, offset, length, extra);
    }
}

static void index(const char *filename, ml_index **idx)
{
    const char *end;
    const char *p = file_map(filename, &end);

    parse_index(p, end, idx);

    file_unmap(p, end);
}

static void parse_tiledata(const char *p, const char *end)
{
    int remaining_bytes = (int)(end - p);
    int block_count = (remaining_bytes-512*32*(8+2+20)) / (4 + 32*(8+1+1+4+2+2+2+1+20));
    item_data_entry_count = 32*block_count;

    tile_datas = (ml_tile_data_entry *)malloc(512*32*sizeof(ml_tile_data_entry));
    item_datas = (ml_item_data_entry *)malloc(item_data_entry_count * sizeof(ml_item_data_entry));

    // 512*4 + 512*32*(8+2+20) bytes
    // first, 512 land data
    for (int i = 0; i < 512*32; i++)
    {
        // every 32 entries have a header of unknown use
        if (i % 32 == 0)
        {
            read_uint32_le(&p, end);
        }
        ml_tile_data_entry *tile_data = &tile_datas[i];
        tile_data->flags = read_uint64_le(&p, end);
        tile_data->texture = read_uint16_le(&p, end); // hmm can this be used instead of the rotation of land gfx?
        read_ascii_fixed(&p, end, tile_data->name, 20);

        //printf("%d: %08llx %d %s\n", i, (unsigned long long)flags, texture, name);
    }

    int i = 0;
    while (p < end)
    {
        // every 32 entries have a header of unknown use
        if (i % 32 == 0)
        {
            read_uint32_le(&p, end);
        }

        ml_item_data_entry *item_data = &item_datas[i];

        item_data->flags = read_uint64_le(&p, end);
        item_data->weight = read_uint8(&p, end);
        item_data->quality = read_uint8(&p, end);
        item_data->quantity = read_uint32_le(&p, end);
        item_data->animation = read_uint16_le(&p, end);
        read_uint16_le(&p, end);
        read_uint16_le(&p, end);
        item_data->height = read_uint8(&p, end);
        read_ascii_fixed(&p, end, item_data->name, 20);
        //printf("%d: %08llx %d %s\n", i, (unsigned long long)flags, animation, name);

        i += 1;
    }
}

static void read_tiledata()
{
    const char *end;
    const char *p = file_map("files/tiledata.mul", &end);

    parse_tiledata(p, end);

    file_unmap(p, end);
}

static void parse_hues(const char *p, const char *end)
{
    hues = (ml_hue *)malloc(sizeof(ml_hue)*8*375);
    for (int i = 0; i < 8*375; i++)
    {
        // every eighth hue is prepended by an unknown header
        if (i % 8 == 0)
        {
            read_uint32_le(&p, end);
        }

        ml_hue *hue = &hues[i];
        for (int j = 0; j < 32; j++)
        {
            hue->colors[j] = read_uint16_le(&p, end);
        }
        hue->start_color = read_uint16_le(&p, end);
        hue->end_color = read_uint16_le(&p, end);
        read_ascii_fixed(&p, end, hue->name, 20);
        //printf("%d: %s %04x %04x\n", i, name, start_color, end_color);
    }
}

static void read_hues()
{
    const char *end;
    const char *p = file_map("files/hues.mul", &end);

    parse_hues(p, end);

    file_unmap(p, end);
}


// see https://github.com/fdsprod/OpenUO/blob/master/OpenUO.Ultima.PresentationFramework/Adapters/AnimationImageSourceStorageAdapter.cs
static int calc_anim_id(int anim_file, int body_id, int action, int direction)
{
    int anim_id;
    if (anim_file == 1) {
        if      (body_id < 200) { anim_id = body_id * 110; }
        else if (body_id < 400) { anim_id = 22000 + ((body_id - 200) * 65); }
        else                    { anim_id = 35000 + ((body_id - 400) * 175); }
    }
    else if (anim_file == 2) {
        if (body_id < 200) { anim_id = body_id * 110; }
        else               { anim_id = 22000 + ((body_id - 200) * 65); }
    }
    else if (anim_file == 3) {
        if      (body_id < 300) { anim_id = body_id * 65; }
        else if (body_id < 400) { anim_id = 33000 + ((body_id - 300) * 110); }
        else                    { anim_id = 35000 + ((body_id - 400) * 175); }
    }
    else if (anim_file == 4) {
        if      (body_id < 200) { anim_id = body_id * 110; }
        else if (body_id < 400) { anim_id = 22000 + ((body_id - 200) * 65); }
        else                    { anim_id = 35000 + ((body_id - 400) * 175); }
    }
    else {
        if (body_id < 200 && body_id != 34) { anim_id = body_id * 110; }
        else                                { anim_id = 35000 + ((body_id - 400) * 65); }
    }

    anim_id += action * 5;

    // there are actually only 5 directions... the remaining 3 are generated by flipping along x axis
    if (direction <= 4) { anim_id += direction; }
    else { anim_id += direction - (direction - 4) * 2; }

    bool flip = direction > 4;

    return anim_id;
}





/*static struct {
    int anim_file;
    int anim_id;
} anim_remap_table[2048];*/

// exposed interface
void ml_init()
{
    assert(!ml_inited);

    printf("[ML]: Reading tiledata...\n");
    read_tiledata();

    printf("[ML]: Reading hues...\n");
    read_hues();

    printf("[ML]: Reading indexes...\n");

    index("files/anim.idx"   , &anim_idx);
    index("files/artidx.mul" , &art_idx);
    index("files/Gumpidx.mul", &gump_idx);
    index("files/staidx0.mul", &statics0_blocks_idx);
    index("files/staidx1.mul", &statics1_blocks_idx);
    assert(anim_idx           != NULL);
    assert(art_idx            != NULL);
    assert(statics0_blocks_idx != NULL);
    assert(statics1_blocks_idx != NULL);

    //printf("anim entries:           %d\n", anim_idx->entry_count);
    //printf("art entries:            %d\n", art_idx->entry_count);
    //printf("statics blocks entries: %d\n", statics_blocks_idx->entry_count);

    //printf("[ML]: Initing bodyconv table...\n");
    // init anim_remap_table
    /*for (int i = 0; i < 2048; i++)
    {
        anim_remap_table[i].anim_file = 1;
        anim_remap_table[i].anim_id = i;
    }*/

    printf("[ML]: Init OK!\n");

    // check this again... 
    // this is a rudimentary safeguard against 
    // initing the lib at the same time from different threads
    assert(!ml_inited);
    ml_inited = true;
}

ml_tile_data_entry *ml_get_tile_data(int tile_id)
{
    assert(ml_inited);
    assert(tile_id >= 0 && tile_id < 512*32);

    return &tile_datas[tile_id];
}

ml_item_data_entry *ml_get_item_data(int item_id)
{
    assert(ml_inited);
    assert(item_id >= 0 && item_id < item_data_entry_count);

    return &item_datas[item_id];
}

ml_hue *ml_get_hue(int hue_id)
{
    assert(ml_inited);
    assert(hue_id >= 0 && hue_id < 8*375);

    return &hues[hue_id];
}

static ml_anim *create_empty_animation()
{
    ml_anim *a;
    a = (ml_anim *)malloc(sizeof(ml_anim) + sizeof(a->frames[0]));
    a->frame_count = 1;
    a->frames[0].center_x = 0;
    a->frames[0].center_y = 0;
    a->frames[0].width = 0;
    a->frames[0].height = 0;
    a->frames[0].data = NULL;
    return a;
}

ml_anim *ml_read_anim(int body_id, int action, int direction)
{
    assert(ml_inited);
    // TODO: first remap body id according to bodyconv?
    int anim_file = 1;
    int anim_id = calc_anim_id(anim_file, body_id, action, direction);

    assert(anim_id >= 0 && anim_id < anim_idx->entry_count);

    ml_anim *animation;
    int offset = anim_idx->entries[anim_id].offset;
    int length = anim_idx->entries[anim_id].length;
    if (offset == -1 || length == -1)
    {
        return create_empty_animation();
    }
    anim(offset, length, &animation);

    return animation;
}

ml_art *ml_read_land_art(int land_id)
{
    assert(ml_inited);

    assert(land_id >= 0 && land_id < art_idx->entry_count);

    int offset = art_idx->entries[land_id].offset;
    int length = art_idx->entries[land_id].length;

    ml_art *art = NULL;
    land(art_idx->entries[land_id].offset, art_idx->entries[land_id].length, &art, true);

    return art;
}

ml_art *ml_read_static_art(int item_id)
{
    assert(ml_inited);
    int id = 0x4000 + item_id;

    assert(id >= 0 && id < art_idx->entry_count);

    int offset = art_idx->entries[id].offset;
    int length = art_idx->entries[id].length;

    ml_art *art = NULL;
    stat(offset, length, &art);

    return art;
}

ml_gump *ml_read_gump(int gump_id)
{
    assert(ml_inited);

    assert(gump_id >= 0 && gump_id < gump_idx->entry_count);

    int offset = gump_idx->entries[gump_id].offset;
    int length = gump_idx->entries[gump_id].length;
    uint32_t extra =  gump_idx->entries[gump_id].extra;

    int width  = (extra >> 16) & 0xffff;
    int height = (extra >>  0) & 0xffff;
    
    ml_gump *g = NULL;
    gump(offset, length, width, height, &g);

    return g;
}

ml_land_block *ml_read_land_block(int map, int block_x, int block_y)
{
    assert(ml_inited);
    assert(map == 1);
    assert(map == 0 || map == 1);

    // size in number of blocks
    int map_block_width = 768;//896;
    int map_block_height = 512;//512;

    assert(block_x >= 0 && block_x < map_block_width);
    assert(block_y >= 0 && block_y < map_block_height);

    int length = 4 + 8 * 8 * 3;
    int offset = (block_x * map_block_height + block_y) * length;

    ml_land_block *mb = NULL;
    land_block(map, offset, length, &mb);

    return mb;
}

static ml_statics_block *create_empty_statics_block()
{
    ml_statics_block *sb = (ml_statics_block *)malloc(sizeof(ml_statics_block));
    sb->statics_count = 0;
    for (int i = 0; i < 8*8; i++)
    {
        sb->roof_heights[i] = -1;
    }
    return sb;
}


ml_statics_block *ml_read_statics_block(int map, int block_x, int block_y)
{
    assert(ml_inited);
    assert(map == 1);
    assert(map == 0 || map == 1);

    // size in number of blocks
    int map_block_width = 768;//896;
    int map_block_height = 512;//512;

    assert(block_x >= 0 && block_x < map_block_width);
    assert(block_y >= 0 && block_y < map_block_height);

    int block_num = block_x * map_block_height + block_y;

    assert(map != 0 || (block_num >= 0 && block_num < statics0_blocks_idx->entry_count));
    assert(map != 1 || (block_num >= 0 && block_num < statics1_blocks_idx->entry_count));

    int offset = -1;
    int length = -1;

    if (map == 0)
    {
        offset = statics0_blocks_idx->entries[block_num].offset;
        length = statics0_blocks_idx->entries[block_num].length;
    }
    else if (map == 1)
    {
        offset = statics1_blocks_idx->entries[block_num].offset;
        length = statics1_blocks_idx->entries[block_num].length;
    }

    if (offset == -1)
    {
        return create_empty_statics_block();
    }
    else
    {
        ml_statics_block *sb = NULL;
        statics_block(map, offset, length, &sb);

        return sb;
    }
}

// threaded mullib

#include <queue>

template <typename T>
class Queue
{
public:
    Queue()
    {
        pthread_mutex_init(&mutex, NULL);
    }

    bool empty()
    {
        pthread_mutex_lock(&mutex);
        bool res = queue.empty();
        pthread_mutex_unlock(&mutex);
        return res;
    }

    void push(T item)
    {
        pthread_mutex_lock(&mutex);
        queue.push(item);
        pthread_mutex_unlock(&mutex);
    }

    T pop()
    {
        assert(!empty());
        pthread_mutex_lock(&mutex);
        T res = queue.front();
        queue.pop();
        pthread_mutex_unlock(&mutex);
        return res;

    }

    std::queue<T> queue;
    pthread_mutex_t mutex;
};

enum ml_type
{
    ANIM,
    LAND_ART,
    STATIC_ART,
    GUMP,
    LAND_BLOCK,
    STATICS_BLOCK
};

struct async_req_t
{
    ml_type type;
    union
    {
        struct
        {
            int body_id, action, direction;
            ml_anim *res;
            void (*callback)(int body_id, int action, int direction, ml_anim *a);
        } anim;
        struct
        {
            int land_id;
            ml_art *res;
            void (*callback)(int land_id, ml_art *l);
        } land_art;
        struct
        {
            int item_id;
            ml_art *res;
            void (*callback)(int item_id, ml_art *s);
        } static_art;
        struct
        {
            int gump_id;
            ml_gump *res;
            void (*callback)(int gump_id, ml_gump *s);
        } gump;
        struct
        {
            int map, block_x, block_y;
            ml_land_block *res;
            void (*callback)(int map, int block_x, int block_y, ml_land_block *lb);
        } land_block;
        struct
        {
            int map, block_x, block_y;
            ml_statics_block *res;
            void (*callback)(int map, int block_x, int block_y, ml_statics_block *sb);
        } statics_block;
    };
};

Queue<async_req_t> async_requests;
Queue<async_req_t> async_responses;

static pthread_t worker_thread;

static void *mlt_worker_thread_main(void *)
{
    while (true)
    {
        // wait for request
        //printf("slave: waiting...\n");
        while (async_requests.empty())
        {
            usleep(1000);
        }
        //printf("slave: req queue not empty!\n");
        async_req_t req = async_requests.pop();
        
        /*printf("slave: got req: %s.\n", (req.type == ANIM) ? "anim" :
                            (req.type == LAND_ART) ? "land art" :
                            (req.type == STATIC_ART) ? "static art" :
                            (req.type == LAND_BLOCK) ? "land block" :
                            (req.type == STATICS_BLOCK) ? "statics block" :
                            "<unknown>");*/

        if (req.type == ANIM)
        {
            req.anim.res = ml_read_anim(req.anim.body_id, req.anim.action, req.anim.direction);
        }
        else if (req.type == LAND_ART)
        {
            req.land_art.res = ml_read_land_art(req.land_art.land_id);
        }
        else if (req.type == STATIC_ART)
        {
            req.static_art.res = ml_read_static_art(req.static_art.item_id);
        }
        else if (req.type == GUMP)
        {
            req.gump.res = ml_read_gump(req.gump.gump_id);
        }
        else if (req.type == LAND_BLOCK)
        {
            req.land_block.res = ml_read_land_block(req.land_block.map, req.land_block.block_x, req.land_block.block_y);
        }
        else if (req.type == STATICS_BLOCK)
        {
            req.statics_block.res = ml_read_statics_block(req.statics_block.map, req.statics_block.block_x, req.statics_block.block_y);
        }
        else
        {
            assert(0 && "slave: unknown req type...");
        }
        async_responses.push(req);

        //printf("slave: sent response.\n");
    }
    return NULL;
}


void mlt_init()
{
    assert(ml_inited);
    assert(!mlt_inited);

    pthread_create(&worker_thread, NULL, mlt_worker_thread_main, NULL);

    printf("[ML]: Threaded part inited!\n");

    assert(!mlt_inited);
    mlt_inited = true;
}
 
void mlt_read_anim(int body_id, int action, int direction, void (*callback)(int body_id, int action, int direction, ml_anim *a))
{
    assert(mlt_inited);

    async_req_t req;
    req.type = ANIM;
    req.anim.body_id   = body_id;
    req.anim.action    = action;
    req.anim.direction = direction;
    req.anim.callback  = callback;

    async_requests.push(req);
}

void mlt_read_land_art(int land_id, void (*callback)(int land_id, ml_art *l))
{
    assert(mlt_inited);

    async_req_t req;
    req.type = LAND_ART;
    req.land_art.land_id  = land_id;
    req.land_art.callback = callback;
    
    async_requests.push(req);
}

void mlt_read_static_art(int item_id, void (*callback)(int item_id, ml_art *s))
{
    assert(mlt_inited);

    async_req_t req;
    req.type = STATIC_ART;
    req.static_art.item_id = item_id;
    req.static_art.callback  = callback;
    
    async_requests.push(req);
}

void mlt_read_gump(int gump_id, void (*callback)(int gump_id, ml_gump *g))
{
    assert(mlt_inited);

    async_req_t req;
    req.type = GUMP;
    req.gump.gump_id = gump_id;
    req.gump.callback  = callback;
    
    async_requests.push(req);
}

void mlt_read_land_block(int map, int block_x, int block_y, void (*callback)(int map, int block_x, int block_y, ml_land_block *lb))
{
    assert(mlt_inited);

    async_req_t req;
    req.type = LAND_BLOCK;
    req.land_block.map      = map;
    req.land_block.block_x  = block_x;
    req.land_block.block_y  = block_y;
    req.land_block.callback = callback;
    
    async_requests.push(req);
}

void mlt_read_statics_block(int map, int block_x, int block_y, void (*callback)(int map, int block_x, int block_y, ml_statics_block *sb))
{
    assert(mlt_inited);

    async_req_t req;
    req.type = STATICS_BLOCK;
    req.statics_block.map      = map;
    req.statics_block.block_x  = block_x;
    req.statics_block.block_y  = block_y;
    req.statics_block.callback = callback;
    
    async_requests.push(req);
}

void mlt_process_callbacks()
{
    assert(mlt_inited);

    while (!async_responses.empty())
    {
        async_req_t response = async_responses.pop();
        /*printf("master: got response\n");
        printf("master: got response: %s.\n", (response.type == ANIM) ? "anim" :
                            (response.type == LAND_ART) ? "land art" :
                            (response.type == STATIC_ART) ? "static art" :
                            (response.type == LAND_BLOCK) ? "land block" :
                            (response.type == STATICS_BLOCK) ? "statics block" :
                            "<unknown>");*/
        if (response.type == ANIM)
        {
            int body_id   = response.anim.body_id;
            int action    = response.anim.action;
            int direction = response.anim.direction;
            ml_anim *res  = response.anim.res;
            response.anim.callback(body_id, action, direction, res);
        }
        else if (response.type == LAND_ART)
        {
            int land_id = response.land_art.land_id;
            ml_art *res = response.land_art.res;
            response.land_art.callback(land_id, res);
        }
        else if (response.type == STATIC_ART)
        {
            int item_id = response.static_art.item_id;
            ml_art *res = response.static_art.res;
            response.static_art.callback(item_id, res);
        }
        else if (response.type == GUMP)
        {
            int gump_id  = response.gump.gump_id;
            ml_gump *res = response.gump.res;
            response.gump.callback(gump_id, res);
        }
        else if (response.type == LAND_BLOCK)
        {
            int map            = response.land_block.map;
            int block_x        = response.land_block.block_x;
            int block_y        = response.land_block.block_y;
            ml_land_block *res = response.land_block.res;
            response.land_block.callback(map, block_x, block_y, res);
        }
        else if (response.type == STATICS_BLOCK)
        {
            int map               = response.statics_block.map;
            int block_x           = response.statics_block.block_x;
            int block_y           = response.statics_block.block_y;
            ml_statics_block *res = response.statics_block.res;
            response.statics_block.callback(map, block_x, block_y, res);
        }
        else
        {
            assert(0 && "unknown response type");
        }
    }
}

