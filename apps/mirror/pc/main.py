#!/usr/bin/env python3
"""副屏: show a window (or part of the screen) of this computer on the esphook screen.

  run.py mirror --list                      the windows that can be mirrored
  run.py mirror --window Terminal           the front-most window of an app or with that title (macOS)
  run.py mirror --region 100,100,640,256    a rectangle of the screen (any system with Pillow)
  run.py mirror --image logo.png --dry-run  check a picture without a device and save what the screen would show
"""
import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "framework" / "pc"))
import mirror_capture as capture
import mirror_codec as codec
from mirror_stream import Inactive, MirrorError, MirrorStream


def build_parser():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--port", help="USB serial device (auto-detected when one ESP32 is connected)")
    what = p.add_argument_group("what to show (one of)")
    what.add_argument("--list", action="store_true", help="list windows and exit")
    what.add_argument("--window", metavar="NAME|ID", help="window by app name, title text or id (macOS)")
    what.add_argument("--region", metavar="X,Y,W,H", help="rectangle of the screen in pixels")
    what.add_argument("--screen", action="store_true", help="the whole screen")
    what.add_argument("--image", metavar="FILE", help="a picture file (re-read each frame)")
    p.add_argument("--crop", metavar="X,Y,W,H", help="zoom into this rectangle of the picture first")
    p.add_argument("--fit", choices=codec.FIT_MODES, default="contain", help="how the picture meets the 160x64 screen")
    p.add_argument("--fps", type=float, default=12.0, help="most frames per second to send (default 12)")
    p.add_argument("--dither", action="store_true", help="dither photos (text and flat colours look better without)")
    p.add_argument("--sharp", action="store_true", help="Lanczos instead of box scaling: crisper edges and some ringing")
    p.add_argument("--refresh", type=float, default=5.0, help="seconds between full refreshes of every strip (default 5)")
    p.add_argument("--duration", type=float, help="stop after this many seconds")
    p.add_argument("--frames", type=int, help="stop after this many captured frames")
    p.add_argument("--dry-run", action="store_true", help="no device: check the pipeline and report the traffic")
    p.add_argument("--preview", metavar="PNG", help="save what the screen shows (4x) after the last frame")
    p.add_argument("--menu-on-exit", action="store_true", help="return the device to its menu when stopping")
    return p


def make_source(args):
    chosen = [bool(args.window), bool(args.region), args.screen, bool(args.image)]
    if sum(chosen) != 1:
        raise ValueError("请指定一个来源：--window、--region、--screen 或 --image（--list 查看窗口）")
    if args.window:
        return capture.WindowSource(capture.find_window(args.window))
    if args.region:
        return capture.ScreenSource(capture.parse_box(args.region))
    if args.screen:
        return capture.ScreenSource()
    return capture.FileSource(args.image)


class DryLink:
    """A stand-in for the device that checks every line the way the firmware does and always accepts it."""
    def __init__(self):
        self.lines, self.events, self._last = [], [], None

    def send_line(self, text):
        self._last = text
        self.lines.append(text)

    def wait_message(self, msg_type, match=None, timeout=1.0):
        if self._last == "mirror hello":
            return dict(type=msg_type, op="hello", ok=True, w=codec.W, h=codec.H, rows=codec.ROWS, free=8, p=0)
        if self._last.startswith("mirror s "):
            frame, index, _ = codec.parse_line(self._last)  # Raises when the line is one the firmware would refuse.
            return dict(type=msg_type, ok=True, f=frame, i=index, free=8, p=0)
        return dict(type=msg_type, ok=True, free=8, p=0)


def drain_events(link):
    events = list(link.events)
    link.events.clear()
    return [e for e in events if e.get("app") == "mirror"]


def pump(source, stream, args, *, clock=time.monotonic, sleep=time.sleep, log=lambda text: None):
    """Capture, convert and send until told to stop. Returns why it stopped."""
    resample = codec._pillow().Resampling.LANCZOS if args.sharp else None
    box = args.crop  # (x, y, w, h) or None
    interval, started = 1.0 / args.fps, clock()
    next_at, next_full, next_report = started, started + args.refresh, started + 2.0
    failures, captured, last_payloads, reported = 0, 0, None, dict(frames=0, at=started)
    while True:
        now = clock()
        if args.duration and now - started >= args.duration:
            return "duration", last_payloads
        if args.frames and captured >= args.frames:
            return "frames", last_payloads
        for event in drain_events(stream.link):
            if event.get("action") == "leave":
                return "leave", last_payloads
            if event.get("action") in ("enter", "resume"):
                stream.invalidate()
        try:
            image = source.grab()
            failures = 0
        except capture.CaptureError as exc:
            failures += 1
            if failures >= 5:
                raise
            log(f"抓取失败（{failures}/5）：{exc}")
            sleep(0.5)
            continue
        captured += 1
        picture = codec.fit(codec.crop(image, box) if box else image, args.fit, resample)
        last_payloads = codec.strips(picture, args.dither)
        force = now >= next_full
        if force:
            next_full = now + args.refresh
        result = stream.send_frame(last_payloads, force=force)
        if result["paused"]:
            sleep(0.2)
        if now >= next_report:
            seconds = now - reported["at"]
            log("%.1f 帧/秒  已发 %d 条  跳过 %d 条  重发 %d  忙 %d  %.0f KB" % (
                (stream.stats["frames"] - reported["frames"]) / seconds, stream.stats["strips"], stream.stats["skipped"],
                stream.stats["retried"], stream.stats["busy"], stream.stats["bytes"] / 1024))
            reported.update(frames=stream.stats["frames"], at=now)
            next_report = now + 2.0
        next_at += interval
        delay = next_at - clock()
        if delay > 0:
            sleep(delay)
        else:
            next_at = clock()


def save_preview(payloads, path):
    picture = codec.render(payloads)
    picture.resize((codec.W * 4, codec.H * 4), codec._pillow().Resampling.NEAREST).save(path)


def main(argv=None):
    args = build_parser().parse_args(argv)
    if args.fps <= 0 or args.fps > 60 or args.refresh <= 0:
        raise SystemExit("--fps 取值 0..60，--refresh 必须大于 0")
    try:
        args.crop = capture.parse_box(args.crop, "--crop") if args.crop else None
    except ValueError as exc:
        print(f"副屏：{exc}", file=sys.stderr)
        return 2
    if args.list:
        try:
            windows = capture.list_windows()
        except capture.CaptureError as exc:
            print(f"副屏：{exc}", file=sys.stderr)
            return 2
        for window in windows:
            print(f"{window['id']:>7}  {window['app'][:24]:<24} {window['title'][:40]:<40} {window['width']}x{window['height']}")
        return 0
    try:
        source = make_source(args)
    except (ValueError, capture.CaptureError) as exc:
        print(f"副屏：{exc}", file=sys.stderr)
        return 2
    print(f"副屏：{source.label} -> 160x64（{args.fit}），Ctrl-C 结束", file=sys.stderr)
    log = lambda text: print(text, file=sys.stderr)
    link = None
    if args.dry_run:
        stream = MirrorStream(DryLink())
    else:
        from esphook_sdk import AppLink
        link = AppLink(args.port)
        stream = MirrorStream(link)
    reason, payloads = "interrupt", None
    try:
        if link:
            link.open("mirror")
            link.wait_active("mirror")
        stream.hello()
        reason, payloads = pump(source, stream, args, log=log)
    except KeyboardInterrupt:
        pass
    except Inactive as exc:
        reason = "inactive"
        print(f"副屏：{exc}", file=sys.stderr)
    except (MirrorError, capture.CaptureError, TimeoutError, RuntimeError) as exc:
        print(f"副屏：{exc}", file=sys.stderr)
        return 1
    finally:
        if link:
            try:
                if reason not in ("leave", "inactive"):
                    stream.bye()
                if args.menu_on_exit and reason != "leave":
                    link.menu()
            except (MirrorError, TimeoutError, RuntimeError, OSError):
                pass
            link.close()
    if args.preview and payloads:
        save_preview(payloads, args.preview)
        print(f"副屏：预览已保存到 {args.preview}", file=sys.stderr)
    stats = stream.stats
    print(f"副屏：结束（{reason}）：{stats['frames']} 帧，{stats['strips']} 条，重发 {stats['retried']}，{stats['bytes'] / 1024:.0f} KB", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
