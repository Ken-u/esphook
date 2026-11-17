"""Small USB transport shared by PC-hosted games; firmware logs are ignored."""
import json
import time
from collections import deque
from .keys import VirtualKeys


def open_serial(port, **settings):
    """Open the ESP32-C3 USB Serial/JTAG port without resetting the chip.

    The USB bridge treats DTR low while RTS is high as the reset sequence. Operating systems assert
    both lines when a port opens and pyserial applies DTR before RTS, so clearing DTR first (as
    `dtr = rts = False` does) passes through exactly that state and reboots the device on every
    connect: acks then depend on boot progress and RAM-only state such as the active app is lost.
    Leave DTR asserted and clear RTS only."""
    import serial
    link = serial.Serial(port=None, baudrate=115200, **settings)
    link.rts = False
    link.port = port
    link.open()
    return link


class UsbLink(VirtualKeys):
    prefix = "app"
    ack_type = "app_ack"

    def __init__(self, port=None):
        try:
            import serial
        except ImportError as exc:
            raise RuntimeError("需要 pyserial：使用 uv run --with pyserial 或安装 pyserial") from exc
        if not port:
            from serial.tools import list_ports
            ports = [p.device for p in list_ports.comports() if p.vid == 0x303A]
            if len(ports) != 1:
                raise RuntimeError("请用 --port 指定 ESP32 USB 串口；候选：" + ", ".join(ports))
            port = ports[0]
        self.serial = open_serial(port, timeout=0.02, write_timeout=2)
        self.sequence = 0
        self.events = deque(maxlen=256)
        self.pending = bytearray()
        self.selected = False

    def _read(self, deadline):
        while time.monotonic() < deadline:
            chunk = self.serial.read(self.serial.in_waiting or 1)
            self.pending.extend(chunk)
            while b"\n" in self.pending:
                line, _, rest = self.pending.partition(b"\n")
                self.pending = bytearray(rest)
                try:
                    value = json.loads(line)
                except (ValueError, UnicodeDecodeError):
                    continue
                if isinstance(value, dict):
                    yield value
            if len(self.pending) > 4096:  # A reset or an unterminated console log.
                self.pending.clear()

    def request(self, payload, *, channel=None):
        if channel not in (None, "app"):
            raise ValueError("only the app control channel may override the native protocol")
        prefix = self.prefix if channel is None else channel
        ack_type = self.ack_type if channel is None else "app_ack"
        self.sequence = self.sequence % 2147483647 + 1
        body = dict(payload, v=1, seq=self.sequence)
        raw = json.dumps(body, ensure_ascii=False, separators=(",", ":")).encode()
        if len(raw) > 1024:
            raise ValueError("game JSON exceeds 1024 bytes")
        self.serial.write(prefix.encode() + b" " + raw + b"\n")
        for msg in self._read(time.monotonic() + 3):
            if msg.get("type") in ("game_button", "app_event"):
                self.events.append(msg)
            elif msg.get("type") == ack_type and msg.get("seq") == self.sequence:
                if not msg.get("ok"):
                    raise RuntimeError("固件拒绝请求：" + msg.get("error", "unknown"))
                self.selected = msg.get("selected", self.selected)
                return msg
        raise TimeoutError(f"ESP 未确认 {self.prefix} 协议；检查固件和串口")

    def send_line(self, text):
        """Writes one raw protocol line for an app's own command prefix (the firmware reads at most 1024 bytes a line)."""
        raw = text.encode() + b"\n"
        if len(raw) > 1024 or b"\n" in text.encode():
            raise ValueError("a protocol line must be one line of at most 1024 bytes")
        self.serial.write(raw)

    def wait_message(self, msg_type, match=None, timeout=1.0):
        """The next JSON message of `msg_type` (and, when given, accepted by `match`), or None after `timeout` seconds.
        Other messages are dropped, but button events seen on the way are kept for poll()."""
        for msg in self._read(time.monotonic() + timeout):
            if msg.get("type") in ("game_button", "app_event"):
                self.events.append(msg)
            if msg.get("type") == msg_type and (match is None or match(msg)):
                return msg
        return None

    def poll(self, timeout=0.02):
        self._renew_keys()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for msg in self._read(min(deadline, time.monotonic() + 0.08)):
                if msg.get("type") in ("game_button", "app_event"):
                    self.events.append(msg)
            self._renew_keys()
        result = list(self.events)
        self.events.clear()
        return result

    def close(self):
        try:
            if getattr(self, "_usb_pressed", {}):
                self.release_keys()
        finally:
            self.serial.close()

    def hardware(self):
        return self.request(dict(op="hardware"),channel="app")["hardware"]

    def set_hardware_profile(self,profile,*,reboot=False):
        if profile not in ("auto","screen","robot") or type(reboot) is not bool:
            raise ValueError("profile must be auto, screen or robot; reboot must be bool")
        return self.request(dict(op="hardware",profile=profile,reboot=reboot),channel="app")["hardware"]


class AppLink(UsbLink):
    """Generic catalog/navigation and bounded foreground app messages."""
    def catalog(self):
        return self.request(dict(op="list"))["apps"]

    def status(self):
        return self.request(dict(op="status"))

    def open(self, app):
        return self.request(dict(op="open", app=app))

    def wait_active(self, app, timeout=3):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.status().get("active") == app:
                return
            time.sleep(0.03)
        raise TimeoutError(f"app {app} did not become active")

    def message(self, app, data):
        if not isinstance(data, dict) or len(json.dumps(data, ensure_ascii=False, separators=(",", ":")).encode()) > 128:
            raise ValueError("app message data must be an object of at most 128 UTF-8 bytes")
        return self.request(dict(op="message", app=app, data=data))

    def menu(self):
        return self.request(dict(op="menu"))

    def clock(self):
        """The device's wall clock as {valid, epoch, tz_min}; valid is False until a PC or Wi-Fi has set it."""
        return self.request(dict(op="time"))["time"]

    def set_time(self, epoch=None, tz_min=None):
        """Sets the device clock (it has no battery: the alarm clock needs this after every power cycle unless Wi-Fi
        fetches the time). Defaults to this computer's time and its current UTC offset."""
        if epoch is None:
            epoch = int(time.time())
        if tz_min is None:
            tz_min = (time.localtime(epoch).tm_gmtoff or 0) // 60
        if type(epoch) is not int or type(tz_min) is not int or not 1735689600 <= epoch <= 4294967295 or not -720 <= tz_min <= 840:
            raise ValueError("epoch must be a UTC second count from 2025 on and tz_min an offset in -720..840 minutes")
        return self.request(dict(op="time", epoch=epoch, tz_min=tz_min))["time"]
