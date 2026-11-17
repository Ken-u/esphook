"""Where the mirrored picture comes from: one window (macOS), a rectangle of the screen, the whole screen or a file.

Every source has grab() returning a Pillow RGB image. A window is captured by its id even when other windows cover
it. That needs pyobjc-framework-Quartz and, on macOS, the Screen Recording permission for the terminal; the screen
region and file sources need only Pillow."""
import subprocess
import sys
import tempfile
from pathlib import Path


class CaptureError(RuntimeError):
    pass


def _pillow():
    try:
        from PIL import Image
    except ImportError as exc:
        raise CaptureError("需要 Pillow：python3 -m pip install pillow") from exc
    return Image


def _quartz():
    if sys.platform != "darwin":
        raise CaptureError("按窗口抓取目前只支持 macOS；其它系统请用 --region x,y,w,h 或 --screen")
    try:
        import Quartz
    except ImportError as exc:
        raise CaptureError("按窗口抓取需要 pyobjc-framework-Quartz：python3 -m pip install pyobjc-framework-Quartz，或改用 --region") from exc
    return Quartz


def list_windows():
    """Visible, ordinary windows of other apps, front to back: dicts of id, app, title, width, height."""
    Quartz = _quartz()
    info = Quartz.CGWindowListCopyWindowInfo(
        Quartz.kCGWindowListOptionOnScreenOnly | Quartz.kCGWindowListExcludeDesktopElements, Quartz.kCGNullWindowID)
    windows = []
    for item in info or ():
        bounds = item.get("kCGWindowBounds") or {}
        width, height = int(bounds.get("Width", 0)), int(bounds.get("Height", 0))
        if item.get("kCGWindowLayer", 1) != 0 or width < 80 or height < 40 or item.get("kCGWindowAlpha", 1) <= 0:
            continue
        windows.append(dict(id=int(item["kCGWindowNumber"]), app=str(item.get("kCGWindowOwnerName") or ""),
                            title=str(item.get("kCGWindowName") or ""), width=width, height=height))
    return windows


def find_window(query, windows=None):
    """The front-most window whose app or title contains `query` (case-insensitive), or a window id given as digits."""
    windows = list_windows() if windows is None else windows
    if query.isdigit():
        for window in windows:
            if window["id"] == int(query):
                return window
        raise CaptureError(f"没有 id 为 {query} 的可见窗口")
    needle = query.casefold()
    matches = [w for w in windows if needle in w["app"].casefold() or needle in w["title"].casefold()]
    if not matches:
        raise CaptureError(f"没有名字含「{query}」的窗口；用 --list 查看可选窗口")
    return matches[0]


class WindowSource:
    """One macOS window by id; the contents are grabbed from the window itself, whatever is in front of it."""
    def __init__(self, window):
        self.window = window
        self.label = f"{window['app']} — {window['title']}".strip(" —") + f" (#{window['id']})"
        self._quartz = _quartz()
        self._image = _pillow()

    def grab(self):
        Quartz = self._quartz
        image = None
        if hasattr(Quartz, "CGWindowListCreateImage"):
            image = Quartz.CGWindowListCreateImage(
                Quartz.CGRectNull, Quartz.kCGWindowListOptionIncludingWindow, self.window["id"],
                Quartz.kCGWindowImageBoundsIgnoreFraming | Quartz.kCGWindowImageNominalResolution)
        if image is None:
            return self._screencapture()
        width, height = Quartz.CGImageGetWidth(image), Quartz.CGImageGetHeight(image)
        if width < 1 or height < 1:
            raise CaptureError("窗口已关闭或没有内容")
        data = Quartz.CGDataProviderCopyData(Quartz.CGImageGetDataProvider(image))
        return self._image.frombuffer("RGBA", (width, height), bytes(data), "raw", "BGRA",
                                      Quartz.CGImageGetBytesPerRow(image), 1).convert("RGB")

    def _screencapture(self):
        """Slower fallback through the system tool, for macOS versions without CGWindowListCreateImage."""
        with tempfile.TemporaryDirectory() as folder:
            target = Path(folder) / "window.png"
            done = subprocess.run(["screencapture", "-x", "-o", "-l", str(self.window["id"]), str(target)],
                                  capture_output=True, text=True, timeout=5)
            if done.returncode or not target.is_file():
                raise CaptureError("抓取窗口失败：" + (done.stderr.strip() or "检查窗口是否还在，以及终端的「屏幕录制」权限"))
            with self._image.open(target) as shot:
                return shot.convert("RGB")


class ScreenSource:
    """The whole screen or one rectangle (x, y, width, height in screen pixels) of it."""
    def __init__(self, region=None):
        self.region = region
        self.label = "整个屏幕" if region is None else "屏幕区域 %d,%d %dx%d" % tuple(region)
        try:
            from PIL import ImageGrab
        except ImportError as exc:
            raise CaptureError("需要 Pillow：python3 -m pip install pillow") from exc
        self._grab = ImageGrab.grab

    def grab(self):
        try:
            if self.region is None:
                return self._grab().convert("RGB")
            x, y, w, h = self.region
            return self._grab(bbox=(x, y, x + w, y + h)).convert("RGB")
        except OSError as exc:
            raise CaptureError(f"抓取屏幕失败：{exc}；macOS 请给终端授权「屏幕录制」") from exc


class FileSource:
    """A still picture, read again on every grab so it can be edited while mirrored."""
    def __init__(self, path):
        self.path = Path(path)
        self.label = f"图片 {self.path.name}"
        if not self.path.is_file():
            raise CaptureError(f"找不到图片 {path}")
        self._image = _pillow()

    def grab(self):
        with self._image.open(self.path) as shot:
            return shot.convert("RGB")


def parse_box(text, what="--region"):
    try:
        x, y, w, h = (int(part) for part in text.split(","))
    except ValueError:
        raise ValueError(f"{what} 需要 x,y,宽,高 四个整数，例如 100,100,640,256") from None
    if x < 0 or y < 0 or w < 1 or h < 1:
        raise ValueError(f"{what} 的位置不能为负，宽高必须大于 0")
    return x, y, w, h
