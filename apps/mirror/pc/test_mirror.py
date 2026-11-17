#!/usr/bin/env python3
"""Tests for the 副屏 PC side: the picture codec, the capture helpers, the stream protocol and the command line.

The wire format is checked against the firmware's own C parser (compiled here when a C compiler exists) and against a
line the C tests also read, so the two halves cannot drift apart."""
import contextlib
import ctypes
import io
import json
import os
import random
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / "framework/pc"))
from PIL import Image, ImageDraw

import importlib.util
import mirror_capture as capture
import mirror_codec as codec
from mirror_stream import Inactive, MirrorError, MirrorStream
_spec = importlib.util.spec_from_file_location("mirror_main", HERE / "main.py")
mirror_main = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(mirror_main)


def vector_payload():
    payload = bytearray()
    for k in range(16):
        payload += (k * 0x1111).to_bytes(2, "little")
    indices = [(x + y) % 16 for y in range(8) for x in range(160)]
    payload += bytes(indices[i] << 4 | indices[i + 1] for i in range(0, len(indices), 2))
    return bytes(payload)


class FakeDevice:
    """The firmware's side of the protocol, as far as the PC can tell, with faults that can be scripted."""
    def __init__(self):
        self.active, self.paused, self.free = True, False, 8
        self.accepted, self.lines, self.faults = [], [], []  # faults: names consumed one per strip line
        self.events = []
        self.silent = 0

    def send_line(self, text):
        self.lines.append(text)
        self.reply = None
        if self.silent:
            self.silent -= 1
            return
        if text == "mirror hello":
            self.reply = dict(type="mirror_ack", op="hello", ok=self.active, w=160, h=64, rows=8, free=self.free, p=int(self.paused))
            if not self.active:
                self.reply["err"] = "inactive"
        elif text == "mirror bye":
            self.reply = dict(type="mirror_ack", ok=True, free=self.free, p=int(self.paused))
        else:
            fault = self.faults.pop(0) if self.faults else None
            if not self.active:
                self.reply = dict(type="mirror_ack", ok=False, err="inactive", free=0, p=0)
            elif self.paused:
                self.reply = dict(type="mirror_ack", ok=True, free=self.free, p=1)
            elif fault == "busy":
                self.reply = dict(type="mirror_ack", ok=False, err="busy", free=0, p=0)
            elif fault == "crc":
                frame, index = int(text.split()[2]), int(text.split()[3])
                self.reply = dict(type="mirror_ack", ok=False, err="crc", f=frame, i=index, free=self.free, p=0)
            elif fault == "garbled":
                self.reply = dict(type="mirror_ack", ok=False, err="args", free=self.free, p=0)
            elif fault == "lost":
                self.accepted.append(codec.parse_line(text))
            else:
                frame, index, payload = codec.parse_line(text)
                self.accepted.append((frame, index, payload))
                self.reply = dict(type="mirror_ack", ok=True, f=frame, i=index, free=self.free, p=0)

    def wait_message(self, msg_type, match=None, timeout=1.0):
        reply, self.reply = getattr(self, "reply", None), None
        return reply if reply is not None and (match is None or match(reply)) else None


def flat(colour):
    """A frame of one colour: eight identical strips."""
    return [codec.quantize_strip(Image.new("RGB", (160, 8), colour))] * 8


class CodecTests(unittest.TestCase):
    def test_crc_and_colour_conversion(self):
        self.assertEqual(codec.crc16(b"123456789"), 0x29B1)
        self.assertEqual(codec.crc16(b""), 0xFFFF)
        self.assertEqual(codec.rgb565(255, 255, 255), 0xFFFF)
        self.assertEqual(codec.rgb565(0, 0, 0), 0)
        self.assertEqual(codec.rgb565(255, 0, 0), 0xF800)
        self.assertEqual(codec.from_rgb565(0xFFFF), (255, 255, 255))
        for value in (0x0000, 0xF800, 0x07E0, 0x001F, 0xFFE0, 0x8410, 0x1234):  # every panel colour survives a round trip
            self.assertEqual(codec.rgb565(*codec.from_rgb565(value)), value)

    def test_the_line_matches_the_firmware_vector(self):
        line = (ROOT / "tests/data/mirror_strip.txt").read_text().strip()
        self.assertEqual(codec.strip_line(37, 5, vector_payload()), line)
        self.assertEqual(codec.parse_line(line), (37, 5, vector_payload()))
        self.assertLess(len(line) + 1, 1024)  # One USB line is at most 1024 bytes.
        self.assertEqual(len(line), len("mirror s 37 5 44aa ") + 896)

    def test_the_line_codec_refuses_what_the_firmware_would(self):
        payload = vector_payload()
        for bad in ((256, 0), (-1, 0), (0, 8), (0, -1)):
            with self.assertRaises(ValueError):
                codec.strip_line(*bad, payload)
        with self.assertRaises(ValueError):
            codec.strip_line(0, 0, payload[:-1])
        line = codec.strip_line(1, 2, payload)
        for broken in (line[:-4], line + "AAAA", line.replace("mirror s", "mirror x"), line[:18] + "0" + line[19:]):
            with self.assertRaises(ValueError):
                codec.parse_line(broken)

    def test_fit_modes(self):
        wide = Image.new("RGB", (320, 100), (200, 30, 30))
        contained = codec.fit(wide, "contain")
        self.assertEqual(contained.size, (160, 64))
        self.assertEqual(contained.getpixel((80, 0)), (0, 0, 0))     # a bar above (the picture is 160 x 50)
        self.assertEqual(contained.getpixel((80, 32)), (200, 30, 30))
        self.assertEqual(contained.getpixel((80, 63)), (0, 0, 0))
        covered = codec.fit(wide, "cover")
        self.assertEqual(covered.getpixel((0, 0)), (200, 30, 30))   # no bars at all
        self.assertEqual(covered.getpixel((159, 63)), (200, 30, 30))
        tall = Image.new("RGB", (40, 200), (10, 200, 10))
        self.assertEqual(codec.fit(tall, "contain").getpixel((0, 32)), (0, 0, 0))  # pillar-boxed
        self.assertEqual(codec.fit(tall, "stretch").getpixel((0, 0)), (10, 200, 10))
        exact = Image.new("RGB", (160, 64), (1, 2, 3))
        self.assertEqual(codec.fit(exact, "contain").getpixel((5, 5)), (1, 2, 3))
        self.assertEqual(codec.fit(Image.new("RGBA", (10, 10), (9, 9, 9, 255)), "stretch").mode, "RGB")
        with self.assertRaises(ValueError):
            codec.fit(wide, "zoom")

    def test_crop_clamps_to_the_picture(self):
        image = Image.new("RGB", (100, 50))
        self.assertEqual(codec.crop(image, (10, 10, 20, 20)).size, (20, 20))
        self.assertEqual(codec.crop(image, (90, 40, 50, 50)).size, (10, 10))
        self.assertEqual(codec.crop(image, (500, 500, 5, 5)).size, (1, 1))

    def test_a_few_colours_are_exact(self):
        image = Image.new("RGB", (160, 64), (0, 0, 0))
        draw = ImageDraw.Draw(image)
        draw.fontmode = "1"  # No anti-aliasing: a window of flat colours has few colours per strip.
        draw.rectangle((10, 4, 70, 40), fill=(248, 252, 248))
        draw.line((0, 63, 159, 0), fill=(248, 0, 0), width=2)
        draw.text((12, 10), "Hello 副屏", fill=(0, 252, 0))
        payloads = codec.strips(image)
        self.assertEqual(len(payloads), 8)
        self.assertTrue(all(len(p) == codec.PAYLOAD for p in payloads))
        shown = codec.render(payloads)
        self.assertEqual(list(codec.pixels(shown)), [codec.from_rgb565(codec.rgb565(*p)) for p in codec.pixels(image)])

    def test_many_colours_stay_close(self):
        image = Image.new("RGB", (160, 64))
        image.putdata([(x * 255 // 159, y * 255 // 63, (x * y) % 256) for y in range(64) for x in range(160)])
        for dither in (False, True):
            payloads = codec.strips(image, dither)
            shown = codec.render(payloads)
            error = sum(abs(a - b) for p, q in zip(codec.pixels(image), codec.pixels(shown)) for a, b in zip(p, q)) / (160 * 64 * 3)
            self.assertLess(error, 24 if not dither else 30, f"dither={dither}: mean error {error:.1f}")
            self.assertTrue(all(len(p) == codec.PAYLOAD for p in payloads))
        self.assertNotEqual(codec.strips(image, False), codec.strips(image, True))

    def test_strips_refuse_other_sizes(self):
        with self.assertRaises(ValueError):
            codec.strips(Image.new("RGB", (100, 64)))
        with self.assertRaises(ValueError):
            codec.quantize_strip(Image.new("RGB", (160, 7)))

    def test_the_firmware_parser_agrees(self):
        compiler = shutil.which(os.environ.get("CC", "cc")) or shutil.which("gcc") or shutil.which("clang")
        if not compiler:
            self.skipTest("no C compiler")
        with tempfile.TemporaryDirectory() as folder:
            library = Path(folder) / "mirror_codec.so"
            subprocess.run([compiler, "-shared", "-fPIC", "-std=c11", "-I", str(ROOT / "apps/mirror/firmware"),
                            str(ROOT / "apps/mirror/firmware/mirror_codec.c"), "-o", str(library)], check=True)
            c = ctypes.CDLL(str(library))
            c.mirror_parse_strip.argtypes = [ctypes.c_char_p, ctypes.c_void_p]
            c.mirror_expand.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
            image = Image.new("RGB", (160, 64))
            rnd = random.Random(11)
            image.putdata([(rnd.randrange(256), rnd.randrange(256), rnd.randrange(256)) for _ in range(160 * 64)])
            for dither in (False, True):
                payloads = codec.strips(image, dither)
                canvas = (ctypes.c_uint16 * (160 * 64))()
                for index, payload in enumerate(payloads):
                    strip = ctypes.create_string_buffer(2 + codec.PAYLOAD)
                    status = c.mirror_parse_strip(codec.strip_line(200 + index, index, payload).encode(), strip)
                    self.assertEqual(status, 0)
                    self.assertEqual((strip.raw[0], strip.raw[1], strip.raw[2:2 + codec.PAYLOAD]), (200 + index, index, payload))
                    c.mirror_expand(strip, canvas)
                expected = codec.render(payloads)
                got = [codec.from_rgb565(value) for value in canvas]
                self.assertEqual(got, list(codec.pixels(expected)))  # What the panel will show is what the PC previews.


class StreamTests(unittest.TestCase):
    def stream(self, **kw):
        device = FakeDevice()
        sleeps = []
        return MirrorStream(device, sleep=sleeps.append, **kw), device, sleeps

    def test_hello(self):
        stream, device, _ = self.stream()
        self.assertTrue(stream.hello()["ok"])
        device.active = False
        with self.assertRaises(Inactive):
            stream.hello()
        device.active = True
        device.silent = 99
        ticks = iter(range(0, 100, 1))
        quiet = MirrorStream(device, clock=lambda: next(ticks))
        with self.assertRaises(MirrorError) as caught:
            quiet.hello(wait=3)
        self.assertNotIsInstance(caught.exception, Inactive)
        self.assertGreaterEqual(len(device.lines), 3)  # It asked again before giving up.

    def test_hello_checks_the_screen_size(self):
        stream, device, _ = self.stream()
        original = device.send_line
        def wrong(text):
            original(text)
            if device.reply and device.reply.get("op") == "hello":
                device.reply["w"] = 128
        device.send_line = wrong
        with self.assertRaises(MirrorError):
            stream.hello()

    def test_only_changed_strips_are_sent(self):
        stream, device, _ = self.stream()
        frame = flat((0, 0, 0))
        self.assertEqual(stream.send_frame(frame), dict(sent=8, skipped=0, paused=False))
        self.assertEqual([i for _, i, _ in device.accepted], list(range(8)))
        self.assertEqual({f for f, _, _ in device.accepted}, {1})
        device.accepted.clear()
        self.assertEqual(stream.send_frame(frame), dict(sent=0, skipped=8, paused=False))
        self.assertEqual(device.accepted, [])
        changed = list(frame)
        changed[3] = codec.quantize_strip(Image.new("RGB", (160, 8), (200, 0, 0)))
        changed[6] = codec.quantize_strip(Image.new("RGB", (160, 8), (0, 0, 200)))
        self.assertEqual(stream.send_frame(changed), dict(sent=2, skipped=6, paused=False))
        self.assertEqual([(f, i) for f, i, _ in device.accepted], [(2, 3), (2, 6)])
        device.accepted.clear()
        self.assertEqual(stream.send_frame(changed, force=True)["sent"], 8)  # A refresh sends everything again.
        stream.invalidate()
        device.accepted.clear()
        self.assertEqual(stream.send_frame(changed)["sent"], 8)
        self.assertEqual(stream.stats["frames"], 4)
        self.assertEqual(stream.stats["strips"], 8 + 2 + 8 + 8)

    def test_frame_numbers_wrap(self):
        stream, device, _ = self.stream()
        seen = []
        for n in range(300):  # Only the top strip changes each time, so each frame is one strip.
            top = codec.quantize_strip(Image.new("RGB", (160, 8), (200 * (n % 2), 0, 0)))
            stream.send_frame([top] + flat((0, 0, 0))[1:])
            seen.append(device.accepted[-1][0])
        self.assertEqual(seen[:3], [1, 2, 3])
        self.assertEqual(seen[254:257], [255, 0, 1])
        self.assertTrue(all(0 <= f <= 255 for f in seen))

    def test_busy_and_damaged_strips_are_sent_again(self):
        stream, device, sleeps = self.stream()
        device.faults = ["busy", "busy", "crc", "garbled"]
        self.assertEqual(stream.send_frame(flat((9, 9, 9)))["sent"], 8)
        self.assertEqual(len(device.accepted), 8)
        self.assertEqual(stream.stats["busy"], 2)
        self.assertEqual(stream.stats["retried"], 2)
        self.assertEqual(len(sleeps), 2)
        self.assertLess(sleeps[0], sleeps[1])  # It backs off a little more each time.

    def test_it_waits_when_the_device_says_its_buffer_is_full(self):
        stream, device, sleeps = self.stream()
        device.free = 0
        self.assertEqual(stream.send_frame(flat((9, 9, 9)))["sent"], 8)
        self.assertEqual(sleeps, [0.012] * 8)  # Ahead of a busy answer, not after it.
        self.assertEqual(stream.stats["busy"], 0)

    def test_a_lost_ack_is_harmless(self):
        stream, device, _ = self.stream()
        device.faults = ["lost"]
        self.assertEqual(stream.send_frame(flat((9, 9, 9)))["sent"], 8)
        self.assertEqual(stream.stats["retried"], 1)
        self.assertEqual(len(device.accepted), 9)  # The strip arrived twice: painting it again changes nothing.
        self.assertEqual(device.accepted[0][:2], device.accepted[1][:2])

    def test_it_gives_up_when_the_device_never_takes_a_strip(self):
        stream, device, _ = self.stream(retries=3)
        device.faults = ["busy"] * 10
        with self.assertRaises(MirrorError):
            stream.send_frame(flat((1, 1, 1)))
        self.assertEqual(len(device.lines), 4)

    def test_leaving_the_app_is_reported(self):
        stream, device, _ = self.stream()
        stream.send_frame(flat((200, 0, 0)))
        device.active = False
        with self.assertRaises(Inactive):
            stream.send_frame(flat((0, 200, 0)))

    def test_a_frozen_screen_is_not_counted_as_shown(self):
        stream, device, _ = self.stream()
        stream.send_frame(flat((1, 1, 1)))
        device.paused = True
        device.accepted.clear()
        result = stream.send_frame(flat((50, 60, 70)))
        self.assertEqual(result, dict(sent=0, skipped=0, paused=True))
        self.assertEqual(len(device.lines) - 8, 1)  # It stopped after the first strip.
        device.paused = False
        self.assertEqual(stream.send_frame(flat((50, 60, 70)))["sent"], 8)  # All of it still has to be shown.

    def test_bye(self):
        stream, device, _ = self.stream()
        stream.bye()
        self.assertEqual(device.lines, ["mirror bye"])

    def test_a_frame_needs_eight_strips(self):
        stream, _, _ = self.stream()
        with self.assertRaises(ValueError):
            stream.send_frame([bytes(codec.PAYLOAD)] * 7)


class FakeSource:
    label = "fake"

    def __init__(self, images):
        self.images, self.calls = list(images), 0

    def grab(self):
        self.calls += 1
        item = self.images[min(self.calls - 1, len(self.images) - 1)]
        if isinstance(item, Exception):
            raise item
        return item


class FakeClock:
    def __init__(self):
        self.now = 100.0
        self.sleeps = []

    def time(self):
        return self.now

    def sleep(self, seconds):
        self.sleeps.append(seconds)
        self.now += seconds


class Options:
    def __init__(self, **kw):
        self.__dict__.update(dict(fps=10.0, refresh=5.0, duration=None, frames=None, crop=None, fit="contain", dither=False, sharp=False))
        self.__dict__.update(kw)


class PumpTests(unittest.TestCase):
    def run_pump(self, source, options, device=None):
        clock = FakeClock()
        device = device or FakeDevice()
        stream = MirrorStream(device, clock=clock.time, sleep=clock.sleep)
        logs = []
        reason, payloads = mirror_main.pump(source, stream, options, clock=clock.time, sleep=clock.sleep, log=logs.append)
        return reason, payloads, stream, device, clock, logs

    def test_runs_at_the_requested_rate_and_skips_static_pictures(self):
        source = FakeSource([Image.new("RGB", (320, 128), (0, 80, 160))])
        reason, payloads, stream, device, clock, logs = self.run_pump(source, Options(duration=3.0, fps=10.0, refresh=100.0))
        self.assertEqual(reason, "duration")
        self.assertEqual(stream.stats["frames"], 1)          # The same picture again and again costs nothing.
        self.assertAlmostEqual(source.calls, 30, delta=2)
        self.assertEqual(len(payloads), 8)
        self.assertTrue(any("帧/秒" in line for line in logs))

    def test_a_full_refresh_repairs_the_screen_now_and_then(self):
        source = FakeSource([Image.new("RGB", (160, 64), (10, 20, 30))])
        _, _, stream, _, _, _ = self.run_pump(source, Options(duration=3.5, fps=10.0, refresh=1.0))
        self.assertEqual(stream.stats["frames"], 4)           # The first, and one a second after that.

    def test_changing_content_is_sent_strip_by_strip(self):
        a, b = Image.new("RGB", (160, 64), (0, 0, 0)), Image.new("RGB", (160, 64), (0, 0, 0))
        ImageDraw.Draw(b).rectangle((0, 0, 159, 7), fill=(255, 255, 255))
        source = FakeSource([a, b])
        _, _, stream, _, _, _ = self.run_pump(source, Options(frames=3, refresh=100.0))
        self.assertEqual(stream.stats["strips"], 8 + 1)

    def test_crop_and_fit_are_applied(self):
        image = Image.new("RGB", (400, 400), (0, 0, 0))
        ImageDraw.Draw(image).rectangle((100, 100, 299, 199), fill=(255, 255, 255))
        _, payloads, _, _, _, _ = self.run_pump(FakeSource([image]), Options(frames=1, crop=(100, 100, 200, 80), fit="stretch"))
        shown = codec.render(payloads)
        self.assertEqual(shown.getpixel((80, 32)), (255, 255, 255))
        self.assertEqual(shown.getpixel((0, 0)), (255, 255, 255))   # Zoomed in on the white box: no black left.

    def test_the_device_key_leaves(self):
        device = FakeDevice()
        source = FakeSource([Image.new("RGB", (160, 64))])
        clock_calls = []
        original = source.grab
        def grab():
            clock_calls.append(1)
            if len(clock_calls) == 3:
                device.events.append(dict(type="app_event", app="mirror", action="leave"))
            return original()
        source.grab = grab
        reason, _, _, _, _, _ = self.run_pump(source, Options(), device)
        self.assertEqual(reason, "leave")
        self.assertEqual(len(clock_calls), 3)

    def test_events_of_other_apps_are_ignored_and_enter_resends_everything(self):
        device = FakeDevice()
        device.events.append(dict(type="app_event", app="demo", action="leave"))
        source = FakeSource([Image.new("RGB", (160, 64))])
        count = []
        original = source.grab
        def grab():
            count.append(1)
            if len(count) == 2:
                device.events.append(dict(type="app_event", app="mirror", action="enter"))
            return original()
        source.grab = grab
        _, _, stream, _, _, _ = self.run_pump(source, Options(frames=3, refresh=100.0), device)
        self.assertEqual(stream.stats["frames"], 2)

    def test_capture_failures_are_retried_and_then_reported(self):
        good = Image.new("RGB", (160, 64))
        source = FakeSource([capture.CaptureError("gone"), capture.CaptureError("gone"), good])
        reason, _, stream, _, clock, logs = self.run_pump(source, Options(frames=1))
        self.assertEqual(reason, "frames")
        self.assertEqual(len(logs), 2)
        self.assertEqual(clock.sleeps[:2], [0.5, 0.5])
        with self.assertRaises(capture.CaptureError):
            self.run_pump(FakeSource([capture.CaptureError("closed")]), Options(frames=1))

    def test_a_frozen_device_makes_it_wait(self):
        device = FakeDevice()
        device.paused = True
        source = FakeSource([Image.new("RGB", (160, 64), (5, 5, 5))])
        _, _, stream, _, clock, _ = self.run_pump(source, Options(duration=1.0), device)
        self.assertEqual(stream.stats["frames"], 0)
        self.assertIn(0.2, clock.sleeps)
        self.assertLess(source.calls, 8)


class CommandLineTests(unittest.TestCase):
    def run_main(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = mirror_main.main(list(argv))
        return code, out.getvalue(), err.getvalue()

    def test_dry_run_checks_every_line_and_saves_a_preview(self):
        with tempfile.TemporaryDirectory() as folder:
            picture, preview = Path(folder) / "in.png", Path(folder) / "out.png"
            image = Image.new("RGB", (640, 256), (20, 30, 60))
            ImageDraw.Draw(image).rectangle((100, 50, 400, 200), fill=(250, 200, 0))
            image.save(picture)
            code, _, err = self.run_main("--image", str(picture), "--dry-run", "--frames", "2", "--fps", "60", "--preview", str(preview))
            self.assertEqual(code, 0, err)
            self.assertIn("结束（frames）：1 帧，8 条", err)
            with Image.open(preview) as shown:
                self.assertEqual(shown.size, (640, 256))
                self.assertEqual(shown.getpixel((200, 100)), codec.from_rgb565(codec.rgb565(250, 200, 0)))

    def test_a_source_is_required_and_only_one_is_allowed(self):
        self.assertEqual(self.run_main("--dry-run")[0], 2)
        self.assertEqual(self.run_main("--screen", "--image", "x.png", "--dry-run")[0], 2)
        code, _, err = self.run_main("--image", "/nonexistent/picture.png", "--dry-run")
        self.assertEqual(code, 2)
        self.assertIn("找不到图片", err)

    def test_boxes(self):
        self.assertEqual(capture.parse_box("1,2,3,4"), (1, 2, 3, 4))
        for bad in ("1,2,3", "a,b,c,d", "-1,0,5,5", "0,0,0,5", "1,2,3,4,5"):
            with self.assertRaises(ValueError):
                capture.parse_box(bad)

    def test_window_lookup(self):
        windows = [dict(id=7, app="Terminal", title="zsh", width=800, height=600), dict(id=9, app="Chrome", title="Dashboard — Grafana", width=1200, height=800)]
        self.assertEqual(capture.find_window("grafana", windows)["id"], 9)
        self.assertEqual(capture.find_window("term", windows)["id"], 7)
        self.assertEqual(capture.find_window("7", windows)["id"], 7)
        with self.assertRaises(capture.CaptureError):
            capture.find_window("nothing", windows)
        with self.assertRaises(capture.CaptureError):
            capture.find_window("123", windows)

    def test_file_and_screen_sources(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "a.png"
            Image.new("RGB", (30, 20), (1, 2, 3)).save(path)
            source = capture.FileSource(path)
            self.assertEqual(source.grab().getpixel((0, 0)), (1, 2, 3))
            Image.new("RGB", (30, 20), (9, 9, 9)).save(path)  # Edited while mirrored: the next frame shows it.
            self.assertEqual(source.grab().getpixel((0, 0)), (9, 9, 9))
        with self.assertRaises(capture.CaptureError):
            capture.FileSource("/nonexistent.png")
        if sys.platform != "darwin":
            with self.assertRaises(capture.CaptureError):
                capture.list_windows()

    def test_fps_is_validated(self):
        with self.assertRaises(SystemExit):
            self.run_main("--image", "x.png", "--fps", "0")


if __name__ == "__main__":
    unittest.main()
