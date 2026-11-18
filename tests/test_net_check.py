#!/usr/bin/env python3
"""tools/net_check.py against a fake board on loopback: the script's own logic, since the real run needs hardware."""
import http.server
import io
import json
import sys
import tempfile
import threading
import time
import unittest
import urllib.request
from contextlib import redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import net_check


class FakeBoard:
    """/health, /, /notify (remembers the callback) and /ota (goes dark for a moment, like a reboot)."""

    def __init__(self, notify_ok=True, ota_ok=True):
        self.callback = None
        self.down_until = 0.0
        board = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def reply(self, code, body, kind="application/json"):
                data = body.encode()
                self.send_response(code)
                self.send_header("Content-Type", kind)
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)

            def do_GET(self):
                if time.monotonic() < board.down_until:
                    self.connection.close()
                elif self.path == "/health":
                    self.reply(200, '{"ok":true,"ip":"127.0.0.1","device_id":"x"}')
                elif self.path == "/":
                    self.reply(200, "<html>" + "x" * 300 + "</html>", "text/html")
                else:
                    self.reply(404, "{}")

            def do_POST(self):
                length = int(self.headers.get("Content-Length", "0"))
                body = self.rfile.read(length)
                if self.path == "/notify" and notify_ok:
                    board.callback = json.loads(body)["callback"]
                    self.reply(200, '{"ok":true}')
                elif self.path == "/ota" and ota_ok:
                    board.down_until = time.monotonic() + 1.5
                    self.reply(200, '{"ok":true,"reboot":true}')
                else:
                    self.reply(400, '{"ok":false}')

            def log_message(self, *args):
                pass

        notify_ok, ota_ok = notify_ok, ota_ok
        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.address = "127.0.0.1:%d" % self.server.server_address[1]
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def press_key(self):
        """What the firmware does when K1 is pressed in the Hook app."""
        deadline = time.monotonic() + 3
        while not self.callback and time.monotonic() < deadline:
            time.sleep(0.02)
        request = urllib.request.Request("http://%s/keyevent" % self.callback, data=b'{"word":"yes"}', method="POST")
        urllib.request.urlopen(request, timeout=3).read()

    def close(self):
        self.server.shutdown()
        self.server.server_close()


class NetCheckTests(unittest.TestCase):
    def run_checks(self, board, press, ota=None):
        results = []
        say = lambda passed, name, detail="": results.append((passed, name))
        base = "http://" + board.address
        with redirect_stdout(io.StringIO()):
            net_check.check_server(base, say)
            net_check.check_notify_and_reply(base, "127.0.0.1", press, say)
            if ota:
                net_check.check_ota(base, ota, None, say, boot_wait=15)
        return results

    def test_everything_passes_against_a_healthy_board(self):
        board = FakeBoard()
        self.addCleanup(board.close)
        with tempfile.NamedTemporaryFile(suffix=".bin") as image:
            image.write(b"\xe9" * 4096)
            image.flush()
            results = self.run_checks(board, board.press_key, image.name)
        self.assertTrue(all(passed for passed, _ in results), results)
        self.assertEqual(len(results), 6)

    def test_a_missing_key_reply_is_reported(self):
        board = FakeBoard()
        self.addCleanup(board.close)
        original = net_check.Catcher.wait
        net_check.Catcher.wait = lambda self, seconds: original(self, 0.3)  # do not sit through the 20 s
        self.addCleanup(setattr, net_check.Catcher, "wait", original)
        results = self.run_checks(board, lambda: None)
        failed = [name for passed, name in results if not passed]
        self.assertEqual(failed, ["key reply POST /keyevent to the callback"])

    def test_a_rejected_notify_is_reported(self):
        board = FakeBoard(notify_ok=False)
        self.addCleanup(board.close)
        original = net_check.Catcher.wait
        net_check.Catcher.wait = lambda self, seconds: original(self, 0.3)
        self.addCleanup(setattr, net_check.Catcher, "wait", original)
        failed = [name for passed, name in self.run_checks(board, lambda: None) if not passed]
        self.assertIn("POST /notify", failed)

    def test_local_ip_is_the_loopback_address_for_a_loopback_device(self):
        self.assertEqual(net_check.local_ip_for("127.0.0.1"), "127.0.0.1")

    def test_main_returns_one_when_the_board_is_unreachable(self):
        out = io.StringIO()
        with redirect_stdout(out):
            self.assertEqual(net_check.main(["--device", "127.0.0.1:9", "--no-usb"]), 1)
        self.assertIn("FAIL the board answers", out.getvalue())


if __name__ == "__main__":
    unittest.main()
