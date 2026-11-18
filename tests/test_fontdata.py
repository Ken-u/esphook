#!/usr/bin/env python3
"""tools/make_fontdata.py: parsing lv_font_conv C, the v2 image layout, its sanity checks, and the committed image.

The firmware side (main/aihook_font.c, real LVGL, damaged images) is covered by tests/headless/test_apps.c.
"""
import os
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import make_fontdata as fd


ROWS = [
    "{.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,",
    "{.bitmap_index = 0, .adv_w = 57, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},",
    "{.bitmap_index = 0, .adv_w = 80, .box_w = 3, .box_h = 8, .ofs_x = 1, .ofs_y = 0},",
    "{.bitmap_index = 3, .adv_w = 117, .box_w = 5, .box_h = 8, .ofs_x = -1, .ofs_y = -2},",
    "{.bitmap_index = 8, .adv_w = 141, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0}"]


def font_c(bpp=1, bitmap_format=0, bitmap=True, extra_cmap="", glyph_rows=None):
    """A tiny lv_font_conv-shaped file: 5 glyphs, a format-0 tiny, a sparse tiny and a format-0 full map."""
    rows = glyph_rows or ROWS
    bitmap_text = ("static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {\n"
                   "    /* U+0021 \"!\" */\n    0xff, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf,\n"
                   "    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17\n};\n") if bitmap else ""
    return """
%s
static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    %s
};

static const uint16_t unicode_list_1[] = {
    0x0, 0x3, 0x4
};

static const uint8_t glyph_id_ofs_list_2[] = {
    0, 1, 0, 3
};

static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 32, .range_length = 2, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 200, .range_length = 10, .glyph_id_start = 2,
        .unicode_list = unicode_list_1, .glyph_id_ofs_list = NULL, .list_length = 3, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    },
    {
        .range_start = 300, .range_length = 4, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = glyph_id_ofs_list_2, .list_length = 4, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL
    },%s
};

static lv_font_fmt_txt_dsc_t font_dsc = {
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 3,
    .bpp = %d,
    .kern_classes = 0,
    .bitmap_format = %d,
};
""" % (bitmap_text, "\n    ".join(rows), extra_cmap, bpp, bitmap_format)


def unpack(blob):
    """Decode an image with the layout from the module docstring, independently of make_fontdata.pack()."""
    magic, version, header_size, total = struct.unpack_from("<4sIII", blob)
    assert (magic, version, header_size, total) == (b"AHFD", 2, 80, len(blob))
    fonts = []
    for i in range(2):
        b_off, b_size, g_off, g_count, c_off, c_count, bpp, fmt = struct.unpack_from("<8I", blob, 16 + 32 * i)
        glyphs = []
        for g in range(g_count):
            word, w, h, ox, oy = struct.unpack_from("<IBBbb", blob, g_off + 8 * g)
            glyphs.append((word & 0xFFFFF, word >> 20, w, h, ox, oy))
        cmaps = []
        for c in range(c_count):
            start, length, first, u_off, o_off, n, kind = struct.unpack_from("<7I", blob, c_off + 28 * c)
            unicode = list(struct.unpack_from("<%dH" % n, blob, u_off)) if u_off else None
            width = 2 if kind in (1, 3) else 1
            ofs = list(struct.unpack_from("<%d%s" % (n if width == 2 else length, "H" if width == 2 else "B"), blob, o_off)) if o_off else None
            cmaps.append((start, length, first, unicode, ofs, n, kind, u_off, o_off))
        fonts.append(dict(bitmap=blob[b_off:b_off + b_size], glyphs=glyphs, cmaps=cmaps, bpp=bpp, fmt=fmt,
                          offsets=(b_off, g_off, c_off)))
    return fonts


def glyph_id(cmaps, cp):
    """LVGL's lookup, as documented in lv_font_fmt_txt.h."""
    for start, length, first, unicode, ofs, n, kind, *_ in cmaps:
        rcp = cp - start
        if not 0 <= rcp < length:
            continue
        if kind in (2, 0):
            return first + (rcp if kind == 2 else ofs[rcp])
        if rcp in unicode:
            index = unicode.index(rcp)
            return first + (index if kind == 3 else ofs[index])
    return 0


class ParseTests(unittest.TestCase):
    def test_parses_tables_lists_and_inline_bitmaps(self):
        font = fd.parse_font_c(font_c())
        self.assertEqual((font.bpp, font.bitmap_format, len(font.glyphs), len(font.cmaps)), (1, 0, 5, 3))
        self.assertEqual(font.glyphs[3], (3, 117, 5, 8, -1, -2))
        self.assertEqual(font.bitmap[:3], b"\xff\x01\x02")
        self.assertEqual(len(font.bitmap), 24)
        self.assertEqual([c["type"] for c in font.cmaps], [2, 3, 0])
        self.assertEqual(font.cmaps[1]["unicode"], (2, [0, 3, 4]))
        self.assertEqual(font.cmaps[2]["ofs"], (1, [0, 1, 0, 3]))

    def test_bitmaps_can_come_from_an_older_image(self):
        font = fd.parse_font_c(font_c(bitmap=False), bitmap=b"\x00" * 32)
        self.assertEqual(len(font.bitmap), 32)
        with self.assertRaisesRegex(fd.FontError, "no bitmap data"):
            fd.parse_font_c(font_c(bitmap=False))

    def test_unsupported_fonts_are_refused(self):
        with self.assertRaisesRegex(fd.FontError, "bpp"):
            fd.parse_font_c(font_c(bpp=3))
        with self.assertRaisesRegex(fd.FontError, "bitmap_format"):
            fd.parse_font_c(font_c(bitmap_format=7))
        with self.assertRaisesRegex(fd.FontError, "no kerning"):
            fd.parse_font_c(font_c().replace(".kern_dsc = NULL", ".kern_dsc = kern_pairs"))

    def test_checks_catch_inconsistent_fonts(self):
        rows = ROWS[:4] + ["{.bitmap_index = 20, .adv_w = 80, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0}"]
        with self.assertRaisesRegex(fd.FontError, "past the end of the bitmaps"):
            fd.parse_font_c(font_c(glyph_rows=rows))
        compressed = fd.parse_font_c(font_c(bitmap_format=1, glyph_rows=rows))  # a compressed stream only has to start inside
        self.assertEqual(compressed.bitmap_format, 1)
        too_far = ("\n    {\n        .range_start = 500, .range_length = 9, .glyph_id_start = 1,\n"
                   "        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, "
                   ".type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY\n    },")
        with self.assertRaisesRegex(fd.FontError, "past the glyph table"):
            fd.parse_font_c(font_c(extra_cmap=too_far))
        with self.assertRaisesRegex(fd.FontError, "does not fit"):
            fd.parse_font_c(font_c(glyph_rows=["{.bitmap_index = 0, .adv_w = 5000, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0}"]))


class PackTests(unittest.TestCase):
    def test_layout_round_trips_and_matches_the_lookup_rules(self):
        a, b = fd.parse_font_c(font_c()), fd.parse_font_c(font_c(bpp=2, bitmap_format=1))
        blob = fd.pack([a, b])
        self.assertEqual(blob, fd.pack([a, b]))  # deterministic
        first, second = unpack(blob)
        self.assertEqual(first["glyphs"], a.glyphs)
        self.assertEqual((first["bpp"], first["fmt"], second["bpp"], second["fmt"]), (1, 0, 2, 1))
        self.assertEqual(first["bitmap"], a.bitmap)
        self.assertEqual(second["bitmap"], b.bitmap)
        for cp, expected in ((32, 1), (33, 2), (34, 0), (200, 2), (203, 3), (204, 4), (201, 0), (300, 1), (301, 2), (302, 1), (303, 4), (999, 0)):
            self.assertEqual(glyph_id(first["cmaps"], cp), expected, cp)
        for item in (first, second):
            for offset in item["offsets"]:
                self.assertEqual(offset % 4, 0)

    def test_identical_lists_are_stored_once_and_aligned(self):
        blob = fd.pack([fd.parse_font_c(font_c()), fd.parse_font_c(font_c())])
        first, second = unpack(blob)
        for one, two in zip(first["cmaps"], second["cmaps"]):
            self.assertEqual((one[7], one[8]), (two[7], two[8]))  # same offsets
        for cmap in first["cmaps"]:
            if cmap[7]:
                self.assertEqual(cmap[7] % 2, 0)

    def test_bitmaps_of_reads_both_versions(self):
        blob = fd.pack([fd.parse_font_c(font_c()), fd.parse_font_c(font_c(bpp=2))])
        self.assertEqual([len(x) for x in fd.bitmaps_of(blob)], [24, 24])
        old = bytearray(b"AHFD" + struct.pack("<7I", 1, 32, 3, 5, 8, 6, 14) + b"\x00" * 3)
        old[32:32] = bytes(range(5)) + b"\xaa" * 6
        v1 = bytes(old[:16]) + struct.pack("<4I", 32, 5, 37, 6) + bytes(old[32:])
        self.assertEqual(fd.bitmaps_of(v1), [bytes(range(5)), b"\xaa" * 6])
        with self.assertRaises(fd.FontError):
            fd.bitmaps_of(b"XXXX" + b"\0" * 28)

    def test_command_line_checks_the_partition_size(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            for name in ("12", "16"):
                (tmp / ("f%s.c" % name)).write_text(font_c(), encoding="utf-8")
            table = tmp / "p.csv"
            table.write_text("# Name,Type,SubType,Offset,Size\nnvs,data,nvs,,0x6000,\nfontdata,data,0x40,,1K,\n", encoding="utf-8")
            command = [sys.executable, str(ROOT / "tools/make_fontdata.py"), "--font12", str(tmp / "f12.c"),
                       "--font16", str(tmp / "f16.c"), "--partitions", str(table), "-o", str(tmp / "out.bin")]
            self.assertEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertLess((tmp / "out.bin").stat().st_size, 1024)
            table.write_text("fontdata,data,0x40,,256,\n", encoding="utf-8")
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn("partition", result.stderr)

    def test_partition_size_units(self):
        with tempfile.TemporaryDirectory() as tmp:
            table = Path(tmp) / "p.csv"
            table.write_text("fontdata, data, 0x40, , 768K,  # comment\n", encoding="utf-8")
            self.assertEqual(fd.partition_size(table), 768 * 1024)
            table.write_text("fontdata, data, 0x40, , 0xC0000,\n", encoding="utf-8")
            self.assertEqual(fd.partition_size(table), 0xC0000)
            with self.assertRaises(fd.FontError):
                fd.partition_size(table, "nvs")


class CommittedImageTests(unittest.TestCase):
    """main/fontdata.bin is what the firmware maps and CI packages; keep it in step with the partition table."""

    def setUp(self):
        self.blob = (ROOT / "main/fontdata.bin").read_bytes()

    def test_header_and_size(self):
        self.assertEqual(self.blob[:4], b"AHFD")
        self.assertEqual(struct.unpack_from("<I", self.blob, 4)[0], 2)
        self.assertEqual(struct.unpack_from("<I", self.blob, 12)[0], len(self.blob))
        limit = fd.partition_size(ROOT / "partitions_aihook.csv")
        self.assertLessEqual(len(self.blob), limit)
        self.assertLess(len(self.blob), limit, "keep some room for a few more glyphs")

    def test_both_fonts_are_complete(self):
        fonts = unpack(self.blob)
        self.assertEqual([len(f["glyphs"]) for f in fonts], [7541, 7541])
        self.assertEqual([f["bpp"] for f in fonts], [1, 2])
        for font in fonts:
            self.assertEqual(font["cmaps"][0][:3], (32, 95, 1))  # ASCII first
            for ch in "新婚快乐ABC09中文字库":
                self.assertNotEqual(glyph_id(font["cmaps"], ord(ch)), 0, ch)
            self.assertEqual(glyph_id(font["cmaps"], 0x1F600), 0)  # no emoji
            for g in font["glyphs"][1:]:
                self.assertLess(g[0], len(font["bitmap"]))


if __name__ == "__main__":
    unittest.main()
