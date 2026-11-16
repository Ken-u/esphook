"""Read USB logs without claiming any application or emitting key actions."""
import argparse
import sys
import time
import queue
import threading
from tools.flash import detect_port

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("--port");p.add_argument("--read-only",action="store_true");a=p.parse_args(argv)
    import serial
    port=serial.Serial(port=None,baudrate=115200,timeout=0.1)
    port.dtr=port.rts=False;port.port=a.port or detect_port();port.open()
    lines=queue.Queue(maxsize=16)
    def stdin_reader():
        for line in sys.stdin:lines.put(line)
    if not a.read_only:threading.Thread(target=stdin_reader,daemon=True).start()
    try:
        while True:
            data=port.read(port.in_waiting or 1)
            if data:sys.stdout.buffer.write(data);sys.stdout.buffer.flush()
            else:time.sleep(0.01)
            for _ in range(16):
                try:line=lines.get_nowait()
                except queue.Empty:break
                port.write(line.encode("utf-8"))
    except KeyboardInterrupt:pass
    finally:port.close()
    return 0
