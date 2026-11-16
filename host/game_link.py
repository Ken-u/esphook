"""Small USB transport shared by PC-hosted games; firmware logs are ignored."""
import json
import time
from collections import deque


class GameLink:
    def __init__(self, port=None):
        try:
            import serial
        except ImportError as exc:
            raise RuntimeError("需要 pyserial：运行 uv run --with pyserial python host/stock_t.py 或安装 pyserial") from exc
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
        self.events = deque()
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
        self.sequence += 1
        body = dict(payload, v=1, seq=self.sequence)
        raw = json.dumps(body, ensure_ascii=False, separators=(",", ":")).encode()
        if len(raw) > 1024:
            raise ValueError("game JSON exceeds 1024 bytes")
        self.serial.write(b"game " + raw + b"\n")
        for msg in self._read(time.monotonic() + 3):
            if msg.get("type") == "game_button":
                self.events.append(msg)
            elif msg.get("type") == "game_ack" and msg.get("seq") == self.sequence:
                if not msg.get("ok"):
                    raise RuntimeError("固件拒绝请求：" + msg.get("error", "unknown"))
                self.selected = msg.get("selected", self.selected)
                return
        raise TimeoutError("ESP 未确认 game 协议；检查是否已刷最新机器人固件")

    def poll(self, timeout=0.02):
        for msg in self._read(time.monotonic() + timeout):
            if msg.get("type") == "game_button":
                self.events.append(msg)
        result = list(self.events)
        self.events.clear()
        return result

    def upload(self, day, session):
        self.request(dict(op="begin", id=session, label=day.name, code=day.symbol[-6:],
                          date=day.date, count=len(day.bars), first=day.first))
        for offset in range(0, len(day.bars), 12):
            self.request(dict(op="bars", id=session, offset=offset, bars=day.bars[offset:offset + 12]))

    def close(self):
        self.serial.close()
