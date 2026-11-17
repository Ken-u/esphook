"""Turn a picture into the 副屏 strips the firmware paints (see apps/mirror/firmware/mirror_codec.h).

The screen is 160 x 64 pixels in 8 strips of 8 rows. Each strip carries its own 16 colour RGB565 palette and one
nibble per pixel, so text and flat UI colours come out exact and photos degrade to a posterised version, and a strip
that did not change since the last frame is simply not sent."""
import base64

W, H, ROWS, STRIPS, COLOURS = 160, 64, 8, 8, 16
PAYLOAD = COLOURS * 2 + W * ROWS // 2
FIT_MODES = ("contain", "cover", "stretch")


def _pillow():
    try:
        from PIL import Image
    except ImportError as exc:
        raise RuntimeError("副屏需要 Pillow：python3 -m pip install pillow（run.py mirror 会自动安装）") from exc
    return Image


def pixels(image):
    """The pixels of an image as a flat sequence (Pillow renamed getdata to get_flattened_data)."""
    return getattr(image, "get_flattened_data", image.getdata)()


def crc16(data):
    """CRC-16/CCITT-FALSE, the checksum the firmware verifies."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def rgb565(r, g, b):
    """Nearest RGB565 value of an 8-bit colour."""
    return ((r * 31 + 127) // 255) << 11 | ((g * 63 + 127) // 255) << 5 | (b * 31 + 127) // 255


def from_rgb565(value):
    """The 8-bit colour the panel shows for an RGB565 value (the inverse used by previews and tests)."""
    r, g, b = value >> 11, value >> 5 & 63, value & 31
    return (r * 255 + 15) // 31, (g * 255 + 31) // 63, (b * 255 + 15) // 31


def fit(image, mode="contain", resample=None):
    """The picture as exactly W x H RGB: letterboxed (contain), cropped to fill (cover) or stretched."""
    Image = _pillow()
    if mode not in FIT_MODES:
        raise ValueError("fit mode must be one of " + ", ".join(FIT_MODES))
    image = image.convert("RGB")
    resample = Image.Resampling.BOX if resample is None else resample
    if mode == "stretch" or image.size == (W, H):
        return image.resize((W, H), resample) if image.size != (W, H) else image
    scale = (max if mode == "cover" else min)(W / image.width, H / image.height)
    size = (max(1, round(image.width * scale)), max(1, round(image.height * scale)))
    scaled = image.resize(size, resample)
    canvas = Image.new("RGB", (W, H), (0, 0, 0))
    canvas.paste(scaled, ((W - size[0]) // 2, (H - size[1]) // 2))
    return canvas


def crop(image, box):
    """Keep only box = (x, y, width, height) of the picture, clamped to it (zoom into one part of a window)."""
    x, y, w, h = box
    x, y = max(0, min(x, image.width - 1)), max(0, min(y, image.height - 1))
    return image.crop((x, y, min(image.width, x + max(1, w)), min(image.height, y + max(1, h))))


def quantize_strip(strip, dither=False):
    """The payload of one W x ROWS RGB strip: palette words then packed indices."""
    Image = _pillow()
    if strip.size != (W, ROWS):
        raise ValueError("a strip is 160 x 8 pixels")
    colours = strip.getcolors(COLOURS)
    if colours is not None:  # Few enough colours that the palette is exact: no quantizer needed.
        palette = [colour for _, colour in sorted(colours, key=lambda item: -item[0])]
        lookup = {colour: i for i, colour in enumerate(palette)}
        indices = [lookup[colour] for colour in pixels(strip)]
    else:
        reduced = strip.quantize(colors=COLOURS, method=Image.Quantize.MEDIANCUT)  # The palette that fits this strip...
        if dither:  # ...and, when asked, the pixels spread over it with error diffusion.
            reduced = strip.quantize(palette=reduced, dither=Image.Dither.FLOYDSTEINBERG)
        flat = reduced.getpalette()[:COLOURS * 3]
        palette = [tuple(flat[i:i + 3]) for i in range(0, len(flat), 3)]
        indices = list(pixels(reduced))
    palette += [(0, 0, 0)] * (COLOURS - len(palette))
    words = bytearray()
    for r, g, b in palette:
        words += rgb565(r, g, b).to_bytes(2, "little")
    packed = bytes(indices[i] << 4 | indices[i + 1] for i in range(0, len(indices), 2))
    payload = bytes(words) + packed
    assert len(payload) == PAYLOAD
    return payload


def strips(image, dither=False):
    """The eight payloads of a W x H RGB picture, top to bottom."""
    if image.size != (W, H):
        raise ValueError("a frame is 160 x 64 pixels; call fit() first")
    return [quantize_strip(image.crop((0, i * ROWS, W, (i + 1) * ROWS)), dither) for i in range(STRIPS)]


def strip_line(frame, index, payload):
    """The protocol line (no newline) that paints `payload` as strip `index` of frame number `frame`."""
    if not 0 <= frame <= 255 or not 0 <= index < STRIPS or len(payload) != PAYLOAD:
        raise ValueError("bad strip")
    return f"mirror s {frame} {index} {crc16(payload):04x} " + base64.b64encode(payload).decode()


def parse_line(line):
    """The inverse of strip_line(): (frame, index, payload); raises ValueError for anything the firmware would refuse."""
    parts = line.split(" ")
    if len(parts) != 6 or parts[:2] != ["mirror", "s"] or len(parts[4]) != 4:
        raise ValueError("not a strip line")
    frame, index, crc = int(parts[2]), int(parts[3]), int(parts[4], 16)
    payload = base64.b64decode(parts[5], validate=True)
    if not (0 <= frame <= 255 and 0 <= index < STRIPS and len(payload) == PAYLOAD) or crc16(payload) != crc:
        raise ValueError("bad strip")
    return frame, index, payload


def render_strip(payload):
    """What the panel shows for a payload: a W x ROWS RGB image (previews and tests)."""
    Image = _pillow()
    palette = [from_rgb565(payload[i] | payload[i + 1] << 8) for i in range(0, COLOURS * 2, 2)]
    pixels = []
    for byte in payload[COLOURS * 2:]:
        pixels += [palette[byte >> 4], palette[byte & 15]]
    strip = Image.new("RGB", (W, ROWS))
    strip.putdata(pixels)
    return strip


def render(payloads):
    """The whole panel picture of eight payloads."""
    Image = _pillow()
    picture = Image.new("RGB", (W, H))
    for i, payload in enumerate(payloads):
        picture.paste(render_strip(payload), (0, i * ROWS))
    return picture
