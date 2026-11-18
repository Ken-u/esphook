#!/usr/bin/env python3
"""Build main/fontdata.bin (format v2) from two lv_font_conv C files.

The fixed `fontdata` partition holds everything the two Chinese LVGL fonts need except their small descriptors:
glyph bitmaps, the glyph description table, and the character-map lists. The firmware (`main/aihook_font.c`)
maps the partition, validates it and points LVGL at the tables in place, so none of it is copied to RAM and none
of it is duplicated in the two OTA app slots.

Inputs are the C files that `lv_font_conv --format lvgl` writes (see the "Opts:" line at the top of such a file for
the exact command). If a C file has no inline `glyph_bitmap[]` array, pass `--bitmaps-from OLD.bin` and the bitmaps
of the same font are taken from an existing fontdata image (format v1 or v2). That is how the repository migrated from
v1; the old C files are in git history before the "Move the glyph tables into fontdata" commit.

Layout (little endian, everything 4-byte aligned except the lists):

    header      magic "AHFD", version 2, header_size, total_size, then one 8 x u32 record per font:
                bitmap_offset, bitmap_size, glyph_offset, glyph_count, cmap_offset, cmap_count, bpp, bitmap_format
    glyphs      glyph_count x 8 bytes, the memory layout of lv_font_fmt_txt_glyph_dsc_t without LV_FONT_FMT_TXT_LARGE:
                u32 (bitmap_index | adv_w << 20), u8 box_w, u8 box_h, s8 ofs_x, s8 ofs_y
    cmaps       cmap_count x 7 x u32: range_start, range_length, glyph_id_start, unicode_list_offset (0 = none),
                glyph_id_ofs_list_offset (0 = none), list_length, type (lv_font_fmt_txt_cmap_type_t)
    lists       u16 unicode lists, u8 (format 0) or u16 (sparse) glyph id offset lists; identical lists are stored once
    bitmaps     glyph bitmaps exactly as lv_font_conv wrote them: plain (bitmap_format 0, bpp bits per pixel, rows
                not padded) or LVGL's compressed stream (bitmap_format 1 or 2)
"""
import argparse
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAGIC = b"AHFD"
VERSION = 2
FONTS = ("12", "16")
HEADER_SIZE = 16 + 32 * len(FONTS)
MAX_BITMAP_INDEX = (1 << 20) - 1
MAX_ADV_W = (1 << 12) - 1
CMAP_TYPES = {
    "LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL": 0,
    "LV_FONT_FMT_TXT_CMAP_SPARSE_FULL": 1,
    "LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY": 2,
    "LV_FONT_FMT_TXT_CMAP_SPARSE_TINY": 3,
}
FORMAT0 = (0, 2)


class FontError(ValueError):
    pass


class Font:
    def __init__(self, bpp, bitmap_format, glyphs, cmaps, bitmap):
        self.bpp, self.bitmap_format, self.glyphs, self.cmaps, self.bitmap = bpp, bitmap_format, glyphs, cmaps, bitmap


def numbers(text):
    return [int(token, 0) for token in re.findall(r"0[xX][0-9a-fA-F]+|\d+", text)]


def strip_comments(text):
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)


def glyph_bytes(box_w, box_h, bpp):
    return (box_w * box_h * bpp + 7) // 8


def parse_font_c(text, bitmap=None):
    """Return a Font from lv_font_conv output; `bitmap` supplies the bytes when the file has none."""
    lists = {}
    for width, name, body in re.findall(
            r"static\s+const\s+uint(8|16)_t\s+((?:unicode_list|glyph_id_ofs_list)_\d+)\s*\[\]\s*=\s*\{(.*?)\};", text, re.S):
        lists[name] = (int(width) // 8, numbers(strip_comments(body)))
    block = re.search(r"glyph_dsc\[\]\s*=\s*\{(.*?)\n\};", text, re.S)
    if not block:
        raise FontError("no glyph_dsc table")
    glyphs = [tuple(int(v) for v in g) for g in re.findall(
        r"\{\s*\.bitmap_index\s*=\s*(\d+),\s*\.adv_w\s*=\s*(\d+),\s*\.box_w\s*=\s*(\d+),\s*\.box_h\s*=\s*(\d+),"
        r"\s*\.ofs_x\s*=\s*(-?\d+),\s*\.ofs_y\s*=\s*(-?\d+)\s*\}", block.group(1))]
    if not glyphs:
        raise FontError("glyph_dsc table is empty")
    cmap_block = re.search(r"cmaps\[\]\s*=\s*\{(.*?)\n\};", text, re.S)
    if not cmap_block:
        raise FontError("no cmaps table")
    cmaps = []
    for start, length, first, unicode, ofs, count, kind in re.findall(
            r"\.range_start\s*=\s*(\d+),\s*\.range_length\s*=\s*(\d+),\s*\.glyph_id_start\s*=\s*(\d+),\s*"
            r"\.unicode_list\s*=\s*(\w+),\s*\.glyph_id_ofs_list\s*=\s*(\w+),\s*\.list_length\s*=\s*(\d+),\s*"
            r"\.type\s*=\s*(\w+)", cmap_block.group(1)):
        if kind not in CMAP_TYPES:
            raise FontError("unknown cmap type " + kind)
        cmaps.append(dict(range_start=int(start), range_length=int(length), glyph_id_start=int(first),
                          unicode=lists[unicode] if unicode != "NULL" else None,
                          ofs=lists[ofs] if ofs != "NULL" else None,
                          list_length=int(count), type=CMAP_TYPES[kind]))
    if not cmaps:
        raise FontError("cmaps table is empty")
    for what, pattern in (("no kerning", r"\.kern_dsc\s*=\s*NULL"), ("no kern classes", r"\.kern_classes\s*=\s*0")):
        if not re.search(pattern, text):
            raise FontError("only fonts with %s are supported" % what)
    bitmap_format = re.search(r"\.bitmap_format\s*=\s*([012])\b", text)
    if not bitmap_format:
        raise FontError("bitmap_format must be 0 (plain), 1 or 2 (compressed)")
    bpp = re.search(r"\.bpp\s*=\s*(\d+)", text)
    if not bpp or int(bpp.group(1)) not in (1, 2, 4):
        raise FontError("bpp must be 1, 2 or 4")
    inline = re.search(r"glyph_bitmap\[\]\s*=\s*\{(.*?)\};", text, re.S)
    if inline:
        bitmap = bytes(numbers(strip_comments(inline.group(1))))
    if bitmap is None:
        raise FontError("the C file has no bitmap data; use --bitmaps-from")
    font = Font(int(bpp.group(1)), int(bitmap_format.group(1)), glyphs, cmaps, bytes(bitmap))
    check_font(font)
    return font


def check_font(font):
    """Everything the firmware loader will check, so a bad font fails here and not on the device."""
    count = len(font.glyphs)
    for index, (bitmap_index, adv_w, box_w, box_h, ofs_x, ofs_y) in enumerate(font.glyphs):
        if bitmap_index > MAX_BITMAP_INDEX or adv_w > MAX_ADV_W or box_w > 255 or box_h > 255 \
                or not -128 <= ofs_x <= 127 or not -128 <= ofs_y <= 127:
            raise FontError("glyph %d does not fit the compact glyph record" % index)
        # A compressed glyph has no length of its own; the stream just has to start inside the bitmaps.
        end = bitmap_index + (glyph_bytes(box_w, box_h, font.bpp) if font.bitmap_format == 0 else 0)
        if end > len(font.bitmap):
            raise FontError("glyph %d reaches past the end of the bitmaps" % index)
    for number, cmap in enumerate(font.cmaps):
        kind, unicode, ofs, length = cmap["type"], cmap["unicode"], cmap["ofs"], cmap["list_length"]
        sparse = kind in (1, 3)
        if (unicode is not None) != sparse or (ofs is not None) != (kind in (0, 1)):
            raise FontError("cmap %d has lists that do not match its type" % number)
        if unicode is not None and (unicode[0] != 2 or len(unicode[1]) != length):
            raise FontError("cmap %d unicode list length mismatch" % number)
        if ofs is not None and (ofs[0] != (2 if sparse else 1) or len(ofs[1]) < (length if sparse else cmap["range_length"])):
            raise FontError("cmap %d glyph id offset list is too short" % number)
        top = cmap["glyph_id_start"]
        if kind == 2:
            top += cmap["range_length"] - 1
        elif kind == 3:
            top += length - 1
        else:
            top += max(ofs[1][:length if sparse else cmap["range_length"]], default=0)
        if top >= count:
            raise FontError("cmap %d points past the glyph table" % number)


def pack(fonts):
    if len(fonts) != len(FONTS):
        raise FontError("expected %d fonts" % len(FONTS))
    body = bytearray(HEADER_SIZE)
    stored = {}

    def put(data, align, share=False):
        key = (bytes(data), align)
        if share and key in stored:
            return stored[key]
        while len(body) % align:
            body.append(0)
        offset = len(body)
        body.extend(data)
        if share:
            stored[key] = offset
        return offset

    records = []
    for font in fonts:
        glyph_offset = put(b"".join(struct.pack("<IBBbb", g[0] | g[1] << 20, g[2], g[3], g[4], g[5])
                                    for g in font.glyphs), 4)
        records.append([font, glyph_offset])
    for record in records:
        font = record[0]
        rows = []
        for cmap in font.cmaps:
            offsets = []
            for kind in ("unicode", "ofs"):
                item = cmap[kind]
                offsets.append(0 if item is None else put(
                    struct.pack("<%d%s" % (len(item[1]), "H" if item[0] == 2 else "B"), *item[1]), item[0], share=True))
            rows.append(struct.pack("<7I", cmap["range_start"], cmap["range_length"], cmap["glyph_id_start"],
                                    offsets[0], offsets[1], cmap["list_length"], cmap["type"]))
        record.append(put(b"".join(rows), 4))
    for record in records:
        record.append(put(record[0].bitmap, 4))
    header = struct.pack("<4sIII", MAGIC, VERSION, HEADER_SIZE, len(body))
    for font, glyph_offset, cmap_offset, bitmap_offset in records:
        header += struct.pack("<8I", bitmap_offset, len(font.bitmap), glyph_offset, len(font.glyphs),
                              cmap_offset, len(font.cmaps), font.bpp, font.bitmap_format)
    body[:HEADER_SIZE] = header
    return bytes(body)


def bitmaps_of(blob):
    """Per-font bitmaps of a fontdata image, format v1 or v2."""
    magic, version, header_size, total = struct.unpack_from("<4sIII", blob)
    if magic != MAGIC:
        raise FontError("not a fontdata image")
    if version == 1:
        o12, s12, o16, s16 = struct.unpack_from("<4I", blob, 16)
        return [blob[o12:o12 + s12], blob[o16:o16 + s16]]
    if version == 2:
        return [blob[o:o + s] for o, s in (struct.unpack_from("<2I", blob, 16 + 32 * i) for i in range(len(FONTS)))]
    raise FontError("unsupported fontdata version %d" % version)


def partition_size(csv_path, label="fontdata"):
    for line in Path(csv_path).read_text(encoding="utf-8").splitlines():
        cells = [c.strip() for c in line.split("#")[0].split(",")]
        if len(cells) >= 5 and cells[0] == label:
            number = re.fullmatch(r"(0[xX][0-9a-fA-F]+|\d+)([KkMm]?)", cells[4])
            if not number:
                break
            return int(number.group(1), 0) * {"": 1, "k": 1024, "m": 1 << 20}[number.group(2).lower()]
    raise FontError("no '%s' partition with a size in %s" % (label, csv_path))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--font12", required=True, help="lv_font_conv C file for the 12 px font")
    parser.add_argument("--font16", required=True, help="lv_font_conv C file for the 16 px font")
    parser.add_argument("--bitmaps-from", help="existing fontdata image that supplies the bitmaps")
    parser.add_argument("--partitions", default=str(ROOT / "partitions_aihook.csv"),
                        help="partition table the image must fit into")
    parser.add_argument("-o", "--output", default=str(ROOT / "main/fontdata.bin"))
    args = parser.parse_args(argv)
    old = bitmaps_of(Path(args.bitmaps_from).read_bytes()) if args.bitmaps_from else [None, None]
    fonts = [parse_font_c(Path(path).read_text(encoding="utf-8"), bitmap)
             for path, bitmap in zip((args.font12, args.font16), old)]
    blob = pack(fonts)
    limit = partition_size(args.partitions)
    if len(blob) > limit:
        raise FontError("image is %d bytes but the fontdata partition is %d" % (len(blob), limit))
    Path(args.output).write_bytes(blob)
    print("%s: %d bytes (%.1f%% of the %d KiB partition); glyphs %s, cmaps %s" % (
        args.output, len(blob), 100.0 * len(blob) / limit, limit // 1024,
        "/".join(str(len(f.glyphs)) for f in fonts), "/".join(str(len(f.cmaps)) for f in fonts)))


if __name__ == "__main__":
    try:
        main()
    except (FontError, OSError) as error:
        sys.exit("make_fontdata: %s" % error)
