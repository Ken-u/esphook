#!/usr/bin/env python3
"""Hardware check for the network paths of a flashed board, run from a computer on the board's Wi-Fi.

    python3 tools/net_check.py --device 192.168.3.3 [--ota build/supermini-aihook.bin] [--token HEX] [--no-usb]

What it proves, in order:
  1. GET /health and GET / answer (the HTTP server is up).
  2. POST /notify is accepted, and a key pressed in the Hook app makes the board POST /keyevent to the callback it was
     given (that is main/http_post.c on real lwIP). The key is pressed over USB with the esphook SDK; --no-usb skips this
     step when you would rather press K1 yourself (the script waits 20 s).
  3. With --ota: POST /ota with an app image, then the board reboots into the other slot and answers /health again.

Exit status 0 only when every requested step passed. Standard library only (USB needs framework/pc on PYTHONPATH).
"""
import argparse
import http.server
import json
import socket
import sys
import threading
import time
import urllib.error
import urllib.request


def local_ip_for(device):
    """The address of this computer that the device can reach."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.connect((device, 80))
        return probe.getsockname()[0]


def request(method, url, body=None, headers=None, timeout=8):
    req = urllib.request.Request(url, data=body, method=method, headers=headers or {})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as response:
            return response.status, response.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as error:
        return error.code, error.read().decode("utf-8", "replace")


class Catcher:
    """A throw-away HTTP server standing in for the PC daemon that receives /keyevent."""

    def __init__(self, host=""):
        self.bodies = []
        owner = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_POST(self):
                length = int(self.headers.get("Content-Length", "0"))
                owner.bodies.append((self.path, self.rfile.read(length)))
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", "11")
                self.end_headers()
                self.wfile.write(b'{"ok":true}')

            def log_message(self, *args):
                pass

        self.server = http.server.ThreadingHTTPServer((host, 0), Handler)
        self.port = self.server.server_address[1]
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def wait(self, seconds):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline and not self.bodies:
            time.sleep(0.05)
        return list(self.bodies)

    def close(self):
        self.server.shutdown()
        self.server.server_close()


def check_server(base, say):
    status, text = request("GET", base + "/health")
    say(status == 200, "GET /health", "%d %s" % (status, text[:80]))
    status, text = request("GET", base + "/")
    say(status == 200 and len(text) > 100, "GET /", "%d, %d bytes" % (status, len(text)))


def check_notify_and_reply(base, my_ip, press, say, token=None):
    catcher = Catcher()
    try:
        body = json.dumps({"client_id": "netcheck", "session_id": "s1", "title": "net check", "body": "press a key",
                           "type": "confirm", "callback": "%s:%d" % (my_ip, catcher.port)}).encode()
        headers = {"Content-Type": "application/json"}
        if token:
            headers["X-Esphook-Token"] = token
        status, text = request("POST", base + "/notify", body, headers)
        say(status == 200 and '"ok":true' in text.replace(" ", ""), "POST /notify", "%d %s" % (status, text[:60]))
        press()
        got = catcher.wait(20)
        parsed = json.loads(got[0][1]) if got else {}
        say(bool(got) and got[0][0] == "/keyevent" and bool(parsed.get("word")), "key reply POST /keyevent to the callback",
            repr(got[0]) if got else "nothing arrived in 20 s")
    finally:
        catcher.close()


def check_ota(base, image, token, say, boot_wait=90):
    with open(image, "rb") as handle:
        data = handle.read()
    headers = {"Content-Type": "application/octet-stream"}
    if token:
        headers["X-Esphook-Token"] = token
    started = time.monotonic()
    status, text = request("POST", base + "/ota", data, headers, timeout=120)
    sent = time.monotonic() - started
    say(status == 200 and '"reboot":true' in text.replace(" ", ""), "POST /ota (%d KiB in %.1f s)" % (len(data) // 1024, sent),
        "%d %s" % (status, text[:60]))
    if status != 200:
        return
    time.sleep(3)  # let it go down before polling
    deadline = time.monotonic() + boot_wait
    back = None
    while time.monotonic() < deadline:
        try:
            if request("GET", base + "/health", timeout=3)[0] == 200:
                back = time.monotonic() - started
                break
        except OSError:
            pass
        time.sleep(1)
    say(back is not None, "device answers /health again after the reboot", "%.1f s after the upload began" % back if back else "no answer")


def usb_presser(port):
    from esphook_sdk import AppLink
    link = AppLink(port) if port else AppLink()

    def press():
        link.release_keys()
        link.menu()
        time.sleep(0.4)
        link.open("hook")
        time.sleep(1.0)
        link.tap(0)

    return press, link.close


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--device", required=True, help="IP address of the board")
    parser.add_argument("--token", help="device secret when the board is paired")
    parser.add_argument("--ota", metavar="BIN", help="also upload this app image and wait for the reboot")
    parser.add_argument("--no-usb", action="store_true", help="do not press the key over USB; press K1 in the Hook app yourself")
    parser.add_argument("--usb-port", help="serial port for the key press (default: auto)")
    args = parser.parse_args(argv)
    base = "http://" + args.device
    failures = []

    def say(passed, name, detail=""):
        print(("PASS " if passed else "FAIL ") + name + (" - " + detail if detail else ""), flush=True)
        if not passed:
            failures.append(name)

    close = lambda: None
    try:
        try:
            check_server(base, say)
        except OSError as error:
            say(False, "the board answers at %s" % args.device, str(error))
            return 1
        if args.no_usb:
            print("press K1 in the Hook app on the board now (you have 20 s after the notification arrives)", flush=True)
            press = lambda: None
        else:
            press, close = usb_presser(args.usb_port)
        check_notify_and_reply(base, local_ip_for(args.device), press, say, args.token)
        if args.ota:
            check_ota(base, args.ota, args.token, say)
    finally:
        close()
    print("ALL PASS" if not failures else "FAILED: " + ", ".join(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
