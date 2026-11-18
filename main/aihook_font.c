/* main/aihook_font.c
 *
 * The two Chinese fonts (12 px 1 bpp and 16 px 2 bpp compressed, from SourceHanSansSC) are LVGL "fmt_txt" fonts whose
 * glyph bitmaps, glyph records and character-map lists used to be compiled into both OTA app slots, about 270 KB of
 * flash twice. They now live once in the fixed `fontdata` partition (tools/make_fontdata.py writes the image) and this
 * file wires LVGL's font descriptors to that memory. The layout is documented in tools/make_fontdata.py.
 */
#include "aihook_font.h"
#include "lvgl.h"
#include <string.h>

#define FONT_COUNT 2
#define MAX_CMAPS 64
#define MAGIC 0x44464841u /* bytes: AHFD */
#define VERSION 2u
#define HEADER_SIZE (16u + 32u * FONT_COUNT)
#define GLYPH_SIZE 8u
#define CMAP_SIZE 28u

/* The glyph records are used in place, so their memory layout has to be the compact one the image stores. */
_Static_assert(sizeof(lv_font_fmt_txt_glyph_dsc_t) == GLYPH_SIZE, "fontdata stores 8-byte glyph records");
_Static_assert(!LV_FONT_FMT_TXT_LARGE, "fontdata stores the compact glyph record");

typedef struct {
    uint32_t bitmap_offset, bitmap_size, glyph_offset, glyph_count, cmap_offset, cmap_count, bpp, bitmap_format;
} font_record_t;

typedef struct {
    uint32_t range_start, range_length, glyph_id_start, unicode_offset, ofs_offset, list_length, type;
} cmap_record_t;

static lv_font_fmt_txt_cmap_t s_cmaps[FONT_COUNT][MAX_CMAPS];
static lv_font_fmt_txt_dsc_t s_dsc[FONT_COUNT];
static bool s_ready;

/* Metrics come from the lv_font_conv runs (see the "Opts:" lines in git history of the old aihook_font_*.c). The font
 * objects are not const only so that a failed load can give them LVGL's built-in font as a fallback. */
lv_font_t aihook_font_12 = {
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .line_height = 16,
    .base_line = 4,
    .subpx = LV_FONT_SUBPX_NONE,
    .underline_position = -1,
    .underline_thickness = 1,
    .dsc = &s_dsc[0],
    .fallback = NULL,
    .user_data = NULL,
};

lv_font_t aihook_font_16 = {
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .line_height = 21,
    .base_line = 5,
    .subpx = LV_FONT_SUBPX_NONE,
    .underline_position = -2,
    .underline_thickness = 1,
    .dsc = &s_dsc[1],
    .fallback = NULL,
    .user_data = NULL,
};

static uint32_t read32(const uint8_t *p)
{
    uint32_t value;
    memcpy(&value, p, sizeof value);
    return value;
}

static uint32_t read16(const uint8_t *p)
{
    uint16_t value;
    memcpy(&value, p, sizeof value);
    return value;
}

static bool in_range(size_t offset, size_t length, size_t limit)
{
    return offset <= limit && length <= limit - offset;
}

static bool load_font(const uint8_t *image, size_t size, const font_record_t *r, unsigned index)
{
    if (r->bpp != 1 && r->bpp != 2 && r->bpp != 4) return false;
    if (r->bitmap_format > 2) return false;
    if (r->glyph_count < 1 || r->glyph_count > 0xFFFFu || r->cmap_count < 1 || r->cmap_count > MAX_CMAPS) return false;
    if (!in_range(r->bitmap_offset, r->bitmap_size, size) || r->bitmap_size > (1u << 20)) return false;
    if (!in_range(r->glyph_offset, (size_t)r->glyph_count * GLYPH_SIZE, size) || r->glyph_offset % 4u) return false;
    if (!in_range(r->cmap_offset, (size_t)r->cmap_count * CMAP_SIZE, size) || r->cmap_offset % 4u) return false;

    const lv_font_fmt_txt_glyph_dsc_t *glyphs = (const lv_font_fmt_txt_glyph_dsc_t *)(const void *)(image + r->glyph_offset);
    for (uint32_t i = 0; i < r->glyph_count; ++i) {
        /* A compressed glyph has no length of its own: its stream only has to start inside the bitmaps. */
        uint32_t end = glyphs[i].bitmap_index;
        if (r->bitmap_format == 0) end += ((uint32_t)glyphs[i].box_w * glyphs[i].box_h * r->bpp + 7u) / 8u;
        if (end > r->bitmap_size) return false;
    }

    lv_font_fmt_txt_cmap_t *out = s_cmaps[index];
    for (uint32_t n = 0; n < r->cmap_count; ++n) {
        const uint8_t *raw = image + r->cmap_offset + n * CMAP_SIZE;
        cmap_record_t c;
        c.range_start = read32(raw);
        c.range_length = read32(raw + 4);
        c.glyph_id_start = read32(raw + 8);
        c.unicode_offset = read32(raw + 12);
        c.ofs_offset = read32(raw + 16);
        c.list_length = read32(raw + 20);
        c.type = read32(raw + 24);
        if (c.range_start > 0x10FFFFu || c.range_length > 0xFFFFu || c.glyph_id_start > 0xFFFFu ||
            c.list_length > 0xFFFFu || c.type > LV_FONT_FMT_TXT_CMAP_SPARSE_TINY) return false;

        bool sparse = c.type == LV_FONT_FMT_TXT_CMAP_SPARSE_FULL || c.type == LV_FONT_FMT_TXT_CMAP_SPARSE_TINY;
        bool full = c.type == LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL || c.type == LV_FONT_FMT_TXT_CMAP_SPARSE_FULL;
        if (sparse != (c.unicode_offset != 0) || full != (c.ofs_offset != 0)) return false;
        if (sparse && (c.unicode_offset % 2u || !in_range(c.unicode_offset, (size_t)c.list_length * 2u, size))) return false;
        size_t ofs_width = sparse ? 2u : 1u;
        size_t ofs_count = sparse ? c.list_length : c.range_length;
        if (full && (c.ofs_offset % ofs_width || c.list_length < ofs_count ||
                     !in_range(c.ofs_offset, ofs_count * ofs_width, size))) return false;

        /* Highest glyph id this map can hand to LVGL must exist in the glyph table. */
        size_t reach = sparse ? c.list_length : c.range_length;
        if (reach) {
            uint32_t top = c.glyph_id_start + (uint32_t)reach - 1u;
            if (full) {
                uint32_t largest = 0;
                for (size_t i = 0; i < ofs_count; ++i) {
                    uint32_t v = sparse ? read16(image + c.ofs_offset + i * 2u) : image[c.ofs_offset + i];
                    if (v > largest) largest = v;
                }
                top = c.glyph_id_start + largest;
            }
            if (top >= r->glyph_count) return false;
        }

        out[n].range_start = c.range_start;
        out[n].range_length = (uint16_t)c.range_length;
        out[n].glyph_id_start = (uint16_t)c.glyph_id_start;
        out[n].unicode_list = c.unicode_offset ? (const uint16_t *)(const void *)(image + c.unicode_offset) : NULL;
        out[n].glyph_id_ofs_list = c.ofs_offset ? (const void *)(image + c.ofs_offset) : NULL;
        out[n].list_length = (uint16_t)c.list_length;
        out[n].type = (lv_font_fmt_txt_cmap_type_t)c.type;
    }

    s_dsc[index] = (lv_font_fmt_txt_dsc_t){
        .glyph_bitmap = image + r->bitmap_offset,
        .glyph_dsc = glyphs,
        .cmaps = out,
        .kern_dsc = NULL,
        .kern_scale = 0,
        .cmap_num = (uint16_t)r->cmap_count,
        .bpp = (uint16_t)r->bpp,
        .kern_classes = 0,
        .bitmap_format = (uint16_t)r->bitmap_format,
    };
    return true;
}

static void unload(void)
{
    s_ready = false;
    memset(s_dsc, 0, sizeof s_dsc);
    memset(s_cmaps, 0, sizeof s_cmaps);
#if LV_FONT_UNSCII_8
    aihook_font_12.fallback = &lv_font_unscii_8;
    aihook_font_16.fallback = &lv_font_unscii_8;
#endif
}

bool aihook_font_load(const uint8_t *image, size_t size)
{
    unload();
    if (!image || (uintptr_t)image % 4u || size < HEADER_SIZE || read32(image) != MAGIC || read32(image + 4) != VERSION) return false;
    uint32_t header_size = read32(image + 8), total = read32(image + 12);
    if (header_size != HEADER_SIZE || total < HEADER_SIZE || total > size) return false;
    for (unsigned i = 0; i < FONT_COUNT; ++i) {
        font_record_t r;
        const uint8_t *raw = image + 16u + 32u * i;
        r.bitmap_offset = read32(raw);
        r.bitmap_size = read32(raw + 4);
        r.glyph_offset = read32(raw + 8);
        r.glyph_count = read32(raw + 12);
        r.cmap_offset = read32(raw + 16);
        r.cmap_count = read32(raw + 20);
        r.bpp = read32(raw + 24);
        r.bitmap_format = read32(raw + 28);
        if (!load_font(image, total, &r, i)) {
            unload();
            return false;
        }
    }
    aihook_font_12.fallback = NULL;
    aihook_font_16.fallback = NULL;
    s_ready = true;
    return true;
}

bool aihook_font_ready(void)
{
    return s_ready;
}
