"""Offline tests for the real-time keyboard bridge; no serial port, keyboard hook or device needed."""
import contextlib
import io
import json
import os
from collections import deque
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[2] / "framework" / "pc"))
import bridge
from esphook_sdk import AppLink


class FakeLink:
    """Records the SDK calls the bridge makes."""

    def __init__(self, fail=None):
        self.calls, self.events, self.fail, self.closed = [], deque(), fail, False

    def _do(self, *call):
        if self.fail:
            raise self.fail
        self.calls.append(call)

    def tap(self, key):
        self._do("tap", key)

    def hold(self, key, ms):
        self._do("hold", key, ms)

    def triple(self, key):
        self._do("triple", key)

    def key_down(self, key):
        self._do("down", key)

    def key_up(self, key):
        self._do("up", key)

    def menu(self):
        self._do("menu")

    def release_keys(self):
        self._do("release")

    def poll(self, timeout=0.02):
        self.calls.append(("poll", timeout))
        events = list(self.events)
        self.events.clear()
        return events

    def close(self):
        self.closed = True


class FakeSource:
    def __init__(self, commands):
        self.commands, self.entered, self.exited = deque(commands), False, False

    def __enter__(self):
        self.entered = True
        return self

    def __exit__(self, *exc):
        self.exited = True
        return False

    def get(self):
        return self.commands.popleft() if self.commands else ("quit", None)


class FakeSerial:
    """Minimal device: acknowledges every request and reports a key event after key_down."""
    in_waiting = 0

    def __init__(self):
        self.rx, self.history = bytearray(), []

    def write(self, raw):
        _, body = raw.split(b" ", 1)
        request = json.loads(body)
        self.history.append(request)
        self.rx.extend(b"I (12) log noise\n")
        if request["op"] == "key" and request["pressed"]:
            self.rx.extend(b'{"v":1,"type":"app_event","app":"key_report","action":"key","key":0,"text":"ok"}\n')
        self.rx.extend(json.dumps(dict(v=1, type="app_ack", seq=request["seq"], ok=True, queued=True)).encode() + b"\n")

    def read(self, n):
        raw = bytes(self.rx[:7])
        del self.rx[:7]
        return raw


def real_link():
    link = object.__new__(AppLink)
    link.serial, link.sequence, link.events, link.pending, link.selected = FakeSerial(), 0, deque(), bytearray(), False
    return link


KEYMAP = {"space": 0, "enter": 1, "tab": 2, "1": 0}


class DecodeTests(unittest.TestCase):
    def test_names(self):
        self.assertEqual(bridge.decode(" \r\tA"), ["space", "enter", "tab", "a"])
        self.assertEqual(bridge.decode("\x1b"), ["esc"])
        self.assertEqual(bridge.decode("\x1b[A\x1b[1;5C\x1bOP"), ["up", "right", "csi"])
        self.assertEqual(bridge.decode("\x1b[Aq"), ["up", "q"])  # an arrow key is not Esc
        self.assertEqual(bridge.decode("\x04"), ["quit"])
        self.assertEqual(bridge.decode("\x1b["), ["csi"])  # truncated sequence must not hang or quit

    def test_command_state_machine(self):
        c = bridge.TerminalCommands(KEYMAP)
        self.assertEqual(c.feed("space"), ("tap", "space"))
        self.assertIsNone(c.feed("h"))
        self.assertEqual(c.feed("enter"), ("hold", "enter"))
        self.assertEqual(c.feed("enter"), ("tap", "enter"))  # the prefix applies once
        self.assertIsNone(c.feed("h"))
        self.assertIsNone(c.feed("z"))  # an unbound key cancels the prefix
        self.assertEqual(c.feed("tab"), ("tap", "tab"))
        self.assertEqual(c.feed("t"), ("triple", 0))
        self.assertEqual((c.feed("m"), c.feed("r")), (("menu", None), ("release", None)))
        for name in ("q", "esc", "quit"):
            self.assertEqual(c.feed(name), ("quit", None))
        self.assertIsNone(c.feed("up"))


class BridgeTests(unittest.TestCase):
    def make(self, link=None, **kwargs):
        out = []
        return bridge.KeyBridge(link or FakeLink(), KEYMAP, emit=out.append, **kwargs), out

    def test_tap_hold_triple_menu_release(self):
        b, out = self.make()
        for command in (("tap", "space"), ("hold", "enter"), ("triple", 0), ("menu", None), ("release", None)):
            self.assertTrue(b.handle(*command))
        self.assertEqual(b.link.calls, [("tap", 0), ("hold", 1, 550), ("triple", 0), ("menu",), ("release",)])
        self.assertEqual([item["action"] for item in out], ["tap", "hold", "triple", "menu", "release"])
        self.assertEqual(out[0], dict(type="forward", action="tap", key="space", usb=0))

    def test_unbound_keys_are_ignored_and_quit_stops(self):
        b, out = self.make()
        self.assertTrue(b.handle("tap", "z"))
        self.assertTrue(b.handle("down", "z"))
        self.assertEqual((b.link.calls, out), ([], []))
        self.assertFalse(b.handle("quit"))

    def test_press_release_ignores_auto_repeat_and_shared_usb_keys(self):
        b, _ = self.make()
        b.handle("down", "space")
        b.handle("down", "space")  # OS auto-repeat
        b.handle("down", "1")  # a second PC key on the same USB key
        b.handle("up", "space")
        self.assertEqual(b.link.calls, [("down", 0)])  # still held through "1"
        b.handle("up", "1")
        b.handle("up", "1")
        self.assertEqual(b.link.calls, [("down", 0), ("up", 0)])
        self.assertEqual(b.down, {})

    def test_errors_are_reported_without_stopping(self):
        for failure in (RuntimeError("固件拒绝请求"), TimeoutError("no ack")):
            b, out = self.make(FakeLink(fail=failure))
            self.assertTrue(b.handle("tap", "space"))
            self.assertEqual(out[0]["type"], "error")
            self.assertIn(str(failure), out[0]["error"])
        b, _ = self.make(FakeLink(fail=OSError("port lost")))
        with self.assertRaises(OSError):  # a lost port ends the session
            b.handle("tap", "space")

    def test_hold_duration_must_confirm(self):
        with self.assertRaises(ValueError):
            self.make(hold_ms=499)

    def test_run_forwards_then_relays_device_events(self):
        link = FakeLink()
        link.events.append(dict(type="app_event", app="key_report", action="key", key=0, text="ok"))
        b, out = self.make(link)
        source = FakeSource([("tap", "space"), ("down", "tab"), None])
        b.run(source)
        self.assertTrue(source.entered and source.exited)
        self.assertEqual([c for c in link.calls if c[0] != "poll"], [("tap", 0), ("down", 2)])
        self.assertIn("app_event", [item["type"] for item in out])
        # Held keys keep being renewed while commands arrive; idle iterations wait on the port.
        self.assertEqual([c[1] for c in link.calls if c[0] == "poll"], [0.0, 0.0, 0.03])
        b2, out2 = self.make(show_events=False)
        b2.link.events.append(dict(type="app_event"))
        b2.pump()
        self.assertEqual(out2, [])

    def test_through_the_real_sdk_including_lease_renewal(self):
        link = real_link()
        out = []
        b = bridge.KeyBridge(link, KEYMAP, emit=out.append)
        b.handle("tap", "space")
        ops = [(r["op"], r.get("pressed")) for r in link.serial.history]
        self.assertEqual(ops, [("key", True), ("key", False)])
        link.serial.history.clear()
        b.handle("hold", "enter")
        downs = [r for r in link.serial.history if r.get("pressed") is True]
        self.assertGreaterEqual(len(downs), 3)  # renewed inside the 350ms lease
        self.assertEqual(link.serial.history[-1]["pressed"], False)
        self.assertTrue(all(r["key"] == 1 for r in link.serial.history))
        link.serial.history.clear()
        b.handle("down", "tab")
        b.pump(0.05)
        b.handle("up", "tab")
        self.assertEqual([(r["op"], r.get("pressed")) for r in link.serial.history][0], ("key", True))
        self.assertEqual(link.serial.history[-1]["pressed"], False)
        self.assertTrue(any(item.get("app") == "key_report" for item in out))  # device event relayed
        link.serial.history.clear()
        b.handle("release")
        self.assertEqual(link.serial.history[0]["op"], "keys_release")


class MainTests(unittest.TestCase):
    def run_main(self, argv, **kwargs):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = bridge.main(argv, **kwargs)
        return code, out.getvalue(), err.getvalue()

    def test_dry_run_prints_plan_without_opening_anything(self):
        code, out, err = self.run_main(["--dry-run"])
        plan = json.loads(out)
        self.assertEqual((code, plan["backend"], plan["hold_ms"]), (0, "terminal", 550))
        self.assertEqual(plan["bindings"]["space"], 0)
        self.assertEqual(plan["bindings"]["2"], 1)  # digits are terminal-only conveniences
        code, out, _ = self.run_main(["--dry-run", "--backend", "pynput", "--config", str(HERE / "bridge.example.json")])
        plan = json.loads(out)
        self.assertEqual((plan["bindings"], plan["quit_key"]), (dict(f9=0, f10=1, f11=2), "f12"))

    def test_reserved_terminal_keys_are_dropped_with_a_warning(self):
        config = HERE / "bindings.example.json"
        code, out, err = self.run_main(["--dry-run", "--config", str(config)])
        self.assertEqual(json.loads(out)["bindings"], {"space": 0, "return": 0, "a": 1, "d": 2})
        self.assertEqual(err, "")
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "keys.json"
            path.write_text(json.dumps(dict(bindings={"q": 0, "x": 1})))
            code, out, err = self.run_main(["--dry-run", "--config", str(path)])
        self.assertEqual(json.loads(out)["bindings"], {"x": 1})
        self.assertIn("'q' is reserved for quit", err)

    def test_hold_below_confirmation_is_rejected(self):
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            bridge.main(["--dry-run", "--hold-ms", "300"])

    def test_session_opens_app_forwards_and_always_closes_the_link(self):
        link = FakeLink()
        link.open = lambda app: link.calls.append(("open", app))
        link.wait_active = lambda app: link.calls.append(("wait", app))
        code, out, err = self.run_main(["--app", "key_report"], link_factory=lambda port: link,
                                       sources=lambda keymap: FakeSource([("tap", "space")]))
        self.assertEqual(code, 0)
        self.assertEqual([c for c in link.calls if c[0] != "poll"], [("open", "key_report"), ("wait", "key_report"), ("tap", 0)])
        self.assertTrue(link.closed)
        self.assertIn("Forwarding terminal keys", err)
        self.assertEqual(json.loads(out.splitlines()[0])["action"], "tap")

        link = FakeLink()
        class Interrupted(FakeSource):
            def get(self):
                raise KeyboardInterrupt
        code, _, _ = self.run_main([], link_factory=lambda port: link, sources=lambda keymap: Interrupted([]))
        self.assertEqual(code, 0)
        self.assertTrue(link.closed)


@unittest.skipIf(os.name == "nt", "pty is POSIX only")
class TerminalTests(unittest.TestCase):
    def test_reads_a_real_tty_in_cbreak_mode_and_restores_it(self):
        import pty
        import termios
        master, slave = pty.openpty()
        stream = os.fdopen(slave, "r", closefd=False)
        before = termios.tcgetattr(slave)
        try:
            with bridge.TerminalKeys(KEYMAP, stream) as keys:
                self.assertIsNone(keys.get())  # nothing typed: must not block
                self.assertFalse(termios.tcgetattr(slave)[3] & termios.ICANON)
                os.write(master, b" h\r\x1b[Aq")
                got = []
                for _ in range(20):
                    command = keys.get()
                    if command:
                        got.append(command)
                    if len(got) == 3:
                        break
                self.assertEqual(got, [("tap", "space"), ("hold", "enter"), ("quit", None)])
            after = termios.tcgetattr(slave)
            mask = termios.ECHO | termios.ICANON | termios.ISIG  # macOS adds unrelated lflag bits to ptys
            self.assertEqual((after[0], after[3] & mask, after[6]), (before[0], before[3] & mask, before[6]))
        finally:
            stream.close()
            os.close(master)
            os.close(slave)

    def test_rejects_non_terminals(self):
        with self.assertRaises(RuntimeError):
            bridge.TerminalKeys(KEYMAP, io.StringIO()).__enter__()


class PynputTests(unittest.TestCase):
    def fake_pynput(self):
        class Listener:
            def __init__(self, on_press, on_release):
                self.on_press, self.on_release, self.running = on_press, on_release, False

            def start(self):
                self.running = True

            def stop(self):
                self.running = False

        keyboard = types.SimpleNamespace(Listener=Listener)
        return types.SimpleNamespace(keyboard=keyboard), keyboard

    def test_translates_press_release_and_quit_key(self):
        module, keyboard = self.fake_pynput()
        with patch.dict(sys.modules, {"pynput": module, "pynput.keyboard": keyboard}):
            source = bridge.PynputKeys("F12")
        char = lambda c: types.SimpleNamespace(char=c)
        named = lambda n: types.SimpleNamespace(char=None, name=n)
        with source:
            self.assertTrue(source.listener.running)
            source.listener.on_press(char("A"))
            source.listener.on_press(named("space"))
            source.listener.on_release(named("space"))
            source.listener.on_release(char("A"))
            source.listener.on_press(named("shift"))  # modifiers are just unbound names
            source.listener.on_press(types.SimpleNamespace(char=None))  # dead key, ignored
            source.listener.on_release(named("f12"))  # stop key acts on press only
            source.listener.on_press(named("f12"))
            seen = []
            while True:
                item = source.get()
                if item is None:
                    break
                seen.append(item)
        self.assertFalse(source.listener.running)
        self.assertEqual(seen, [("down", "a"), ("down", "space"), ("up", "space"), ("up", "a"), ("down", "shift"), ("quit", None)])

    def test_missing_package_gives_install_hint(self):
        with patch.dict(sys.modules, {"pynput": None}):
            with self.assertRaises(RuntimeError) as caught:
                bridge.PynputKeys()
        self.assertIn("--with pynput", str(caught.exception))


if __name__ == "__main__":
    unittest.main()
