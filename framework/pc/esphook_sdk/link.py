"""Small USB transport shared by PC-hosted games; firmware logs are ignored."""
import json
import time
from collections import deque


class UsbLink:
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
        self.serial = serial.Serial(port=None, baudrate=115200, timeout=0.02, write_timeout=2)
        self.serial.dtr = self.serial.rts = False
        self.serial.port = port
        self.serial.open()
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

    def request(self, payload):
        self.sequence = self.sequence % 2147483647 + 1
        body = dict(payload, v=1, seq=self.sequence)
        raw = json.dumps(body, ensure_ascii=False, separators=(",", ":")).encode()
        if len(raw) > 1024:
            raise ValueError("game JSON exceeds 1024 bytes")
        self.serial.write(self.prefix.encode() + b" " + raw + b"\n")
        for msg in self._read(time.monotonic() + 3):
            if msg.get("type") in ("game_button", "app_event"):
                self.events.append(msg)
            elif msg.get("type") == self.ack_type and msg.get("seq") == self.sequence:
                if not msg.get("ok"):
                    raise RuntimeError("固件拒绝请求：" + msg.get("error", "unknown"))
                self.selected = msg.get("selected", self.selected)
                return msg
        raise TimeoutError(f"ESP 未确认 {self.prefix} 协议；检查固件和串口")

    def poll(self, timeout=0.02):
        for msg in self._read(time.monotonic() + timeout):
            if msg.get("type") in ("game_button", "app_event"):
                self.events.append(msg)
        result = list(self.events)
        self.events.clear()
        return result

    def close(self):
        self.serial.close()


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
