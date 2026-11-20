#!/usr/bin/env python3
"""tools/device.py without a board: the argument handling, and the text-command reader on a serial port that hands over
its bytes in pieces (the reply of a real board can arrive split in the middle of a line)."""
import io
import json
import sys
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import device


class FakeSerial:
    """Hands out `chunks` one read at a time, then nothing."""

    def __init__(self, chunks):
        self.chunks = list(chunks)
        self.in_waiting = 0

    def reset_input_buffer(self):
        pass

    def read(self, size=1):
        return self.chunks.pop(0) if self.chunks else b""


class FakeLink:
    instances = []

    def __init__(self, port=None, chunks=()):
        self.port = port
        self.serial = FakeSerial(chunks)
        self.sent = []
        self.opened = []
        self.closed = False
        FakeLink.instances.append(self)

    def send_line(self, text):
        self.sent.append(text)

    def status(self):
        return {"type": "app_ack", "active": self.opened[-1] if self.opened else "wedding", "standby": "wedding"}

    def hardware(self):
        return {"active": "auto", "configured": "auto", "heap_free": 54000}

    def clock(self):
        return {"valid": True, "epoch": 1795013470, "tz_min": 480}

    def catalog(self):
        return [{"id": "wedding", "title": "新婚快乐"}, {"id": "buzztest", "title": "蜂鸣测试"}]

    def menu(self):
        return {"ok": True, "queued": True}

    def open(self, app):
        self.opened.append(app)

    def wait_active(self, app, timeout=3):
        if app not in [a["id"] for a in self.catalog()]:
            raise TimeoutError(app)

    def close(self):
        self.closed = True


def run(argv, chunks=()):
    """Runs device.main(argv) against a FakeLink; returns (exit code, stdout, stderr, the link used)."""
    FakeLink.instances.clear()
    original = device.AppLink
    device.AppLink = lambda port=None: FakeLink(port, chunks)
    out, err = io.StringIO(), io.StringIO()
    try:
        with redirect_stdout(out), redirect_stderr(err):
            code = device.main(argv)
    finally:
        device.AppLink = original
    return code, out.getvalue(), err.getvalue(), (FakeLink.instances[0] if FakeLink.instances else None)


class TextCommand(unittest.TestCase):
    def test_a_line_split_across_reads_is_returned_whole(self):
        link = FakeLink(chunks=[b"sound=on rea", b"dy=1 played=3\n"])
        self.assertEqual(device.text_command(link, "sound", 0.2, expect="sound="), ["sound=on ready=1 played=3"])
        self.assertEqual(link.sent, ["sound"])

    def test_a_line_still_arriving_when_time_is_up_is_dropped(self):
        link = FakeLink(chunks=[b"OK one\nOK tw"])
        self.assertEqual(device.text_command(link, "x", 0.1), ["OK one"])

    def test_json_frames_blank_lines_and_logs_are_left_out(self):
        chunks = [b'{"type":"app_event"}\n\nI (1234) servo: tick\nsound=off ready=1\n']
        self.assertEqual(device.text_command(FakeLink(chunks=chunks), "sound", 0.1), ["sound=off ready=1"])
        self.assertEqual(device.text_command(FakeLink(chunks=chunks), "sound", 0.1, logs=True),
                         ["I (1234) servo: tick", "sound=off ready=1"])

    def test_expect_stops_at_the_first_matching_line(self):
        link = FakeLink(chunks=[b"noise\nsound=on ready=1\nlater\n"])
        self.assertEqual(device.text_command(link, "sound", 5.0, expect="sound="), ["noise", "sound=on ready=1"])


class Cli(unittest.TestCase):
    def test_dry_run_never_opens_the_port_wherever_the_flag_stands(self):
        for argv in (["--dry-run", "menu"], ["menu", "--dry-run"], ["--dry-run", "--port", "/dev/x", "open", "buzztest"],
                     ["cmd", "sound test", "--dry-run", "--wait", "2"], ["status", "--dry-run"], ["apps", "--dry-run"]):
            code, out, _, link = run(argv)
            self.assertEqual(code, 0, argv)
            self.assertIsNone(link, f"{argv} opened the port")
            self.assertIn("action", json.loads(out))

    def test_status_gathers_app_hardware_and_clock(self):
        code, out, _, link = run(["status", "--port", "/dev/ttyACM0"])
        status = json.loads(out)
        self.assertEqual(code, 0)
        self.assertEqual(link.port, "/dev/ttyACM0")
        self.assertEqual((status["app"]["active"], status["hardware"]["configured"], status["clock"]["valid"]), ("wedding", "auto", True))
        self.assertTrue(link.closed)

    def test_open_waits_for_the_app_and_reports_it(self):
        code, out, _, link = run(["open", "buzztest"])
        self.assertEqual((code, link.opened, json.loads(out)["active"]), (0, ["buzztest"], "buzztest"))

    def test_apps_lists_the_catalog(self):
        _, out, _, _ = run(["apps"])
        self.assertEqual([a["id"] for a in json.loads(out)], ["wedding", "buzztest"])

    def test_the_factory_reset_and_servo_motion_need_force(self):
        for line in ("reset", "RESET", "robot arm", "robot  follow", "robot center", "robot angle:5"):
            for extra in ([], ["--dry-run"]):
                code, out, err, link = run(["cmd", line, *extra])
                self.assertEqual((code, out, link), (4, "", None), line)
                self.assertIn("Ask the owner", err)
        for line in ("sound", "robot status", "robot disarm", "robot center_us:1500", "status", "help"):
            self.assertIsNone(device.risk(line), line)
        code, _, _, link = run(["cmd", "robot arm", "--force", "--wait", "0.1"], chunks=[b"OK armed\n"])
        self.assertEqual((code, link.sent), (0, ["robot arm"]))

    def test_cmd_prints_the_reply_and_no_reply_is_an_error(self):
        code, out, _, link = run(["cmd", "sound", "--wait", "0.2"], chunks=[b"sound=on ready=1\n"])
        self.assertEqual((code, out.strip(), link.sent, link.closed), (0, "sound=on ready=1", ["sound"], True))
        code, out, err, link = run(["cmd", "status", "--wait", "0.1"])
        self.assertEqual((code, out), (3, ""))
        self.assertIn("no reply", err)
        self.assertTrue(link.closed)

    def test_cmd_rejects_empty_and_multi_line_commands(self):
        for line in ("", "   ", "a\nb"):
            with self.assertRaises(SystemExit), redirect_stderr(io.StringIO()):
                device.main(["cmd", line, "--dry-run"])


if __name__ == "__main__":
    unittest.main()
