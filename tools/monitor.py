"""Read USB logs without claiming any application or emitting key actions."""
import argparse
import sys
import time
import queue
import threading
from pathlib import Path
from tools.flash import detect_port
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"framework/pc"))
from esphook_sdk.link import open_serial

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("--port");p.add_argument("--read-only",action="store_true");a=p.parse_args(argv)
    port=open_serial(a.port or detect_port(),timeout=0.1)  # opening must not reset the chip
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
