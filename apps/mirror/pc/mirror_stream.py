"""Send pictures to the 副屏 app: the strip protocol of apps/mirror/firmware/mirror_codec.h over the USB link.

Every strip line is answered by one JSON `mirror_ack` from the device, which is how this side learns that a strip was
taken, that it must wait (the device is still painting), that the screen is frozen by its key, or that the app is no
longer on screen. Nothing is sent for a strip that is the same as the one the device already shows."""
import time

import mirror_codec as codec


class MirrorError(RuntimeError):
    pass


class Inactive(MirrorError):
    """The mirror app is not the app on the device screen (it was left, or never opened)."""


class MirrorStream:
    def __init__(self, link, *, clock=time.monotonic, sleep=time.sleep, timeout=0.6, retries=6):
        self.link, self.clock, self.sleep, self.timeout, self.retries = link, clock, sleep, timeout, retries
        self.shown = [None] * codec.STRIPS  # What the device is showing, strip by strip.
        self.frame = 0
        self.stats = dict(frames=0, strips=0, skipped=0, retried=0, busy=0, bytes=0, paused=0)

    def invalidate(self):
        """Forget what the device shows, so the next frame sends every strip."""
        self.shown = [None] * codec.STRIPS

    def hello(self, wait=2.0):
        """Ask the device whether it is ready. Returns its reply; raises Inactive or MirrorError."""
        deadline = self.clock() + wait
        while True:
            self.link.send_line("mirror hello")
            ack = self.link.wait_message("mirror_ack", lambda m: m.get("op") == "hello", timeout=0.4)
            if ack is not None:
                if not ack.get("ok"):
                    raise Inactive("设备上的副屏应用没有在运行")
                if (ack.get("w"), ack.get("h"), ack.get("rows")) != (codec.W, codec.H, codec.ROWS):
                    raise MirrorError(f"屏幕尺寸不一致：设备 {ack.get('w')}x{ack.get('h')}")
                return ack
            if self.clock() >= deadline:
                raise MirrorError("设备没有回应 mirror 协议：确认固件包含副屏应用，并且没有别的程序占用串口")

    def _exchange(self, line, frame, index):
        """One strip, retried until the device takes it. Returns the ack."""
        def mine(message):  # An error about a line it could not even read has no strip number.
            return message.get("op") is None and ("f" not in message or (message["f"] == frame and message.get("i") == index))
        for attempt in range(self.retries + 1):
            self.link.send_line(line)
            self.stats["bytes"] += len(line) + 1
            ack = self.link.wait_message("mirror_ack", mine, timeout=self.timeout)
            if ack is None:  # Lost on the way: sending the same strip again is harmless.
                self.stats["retried"] += 1
                continue
            if ack.get("ok"):
                if ack.get("free") == 0:  # The ring is full: the screen empties it every few milliseconds, so wait for that.
                    self.sleep(0.012)
                return ack
            error = ack.get("err")
            if error == "inactive":
                raise Inactive("设备上的副屏应用已经退出")
            if error == "busy":  # The screen is still painting what it has: give it a moment.
                self.stats["busy"] += 1
                self.sleep(0.006 * (attempt + 1))
            else:  # Damaged on the way (crc, base64): send it again at once.
                self.stats["retried"] += 1
        raise MirrorError(f"设备连续 {self.retries + 1} 次没有收下第 {index} 条画面")

    def send_frame(self, payloads, force=False):
        """Send the strips that differ from what the device shows. Returns {sent, skipped, paused}."""
        if len(payloads) != codec.STRIPS:
            raise ValueError("a frame is eight strips")
        changed = [i for i, payload in enumerate(payloads) if force or self.shown[i] != payload]
        result = dict(sent=0, skipped=codec.STRIPS - len(changed), paused=False)
        self.stats["skipped"] += result["skipped"]
        if not changed:
            return result
        frame = (self.frame + 1) & 255
        for index in changed:
            ack = self._exchange(codec.strip_line(frame, index, payloads[index]), frame, index)
            if ack.get("p"):  # Frozen by its key: it threw the strip away, so do not count it as shown.
                result["paused"] = True
                self.stats["paused"] += 1
                break
            self.shown[index] = payloads[index]
            result["sent"] += 1
            self.stats["strips"] += 1
        if result["sent"]:
            self.frame = frame
            self.stats["frames"] += 1
        return result

    def bye(self):
        """Tell the device the PC has stopped (it shows that and keeps the last picture)."""
        self.link.send_line("mirror bye")
        self.link.wait_message("mirror_ack", lambda m: m.get("op") is None and "f" not in m, timeout=0.5)
