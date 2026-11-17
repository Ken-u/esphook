"""Offline tests for the real-time keyboard bridge; no serial port, keyboard hook or device needed."""
import contextlib
import importlib.machinery
import io
import json
import os
from collections import deque
from pathlib import Path
import sys
import time
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

    def close(self):
        pass

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
        self.assertEqual((code, plan["backend"], plan["hold_ms"]), (0, "auto", 550))
        self.assertEqual(plan["bindings"], {"space": 0})  # the default is Space = K1, nothing else
        code, out, _ = self.run_main(["--dry-run", "--backend", "pynput", "--config", str(HERE / "bridge.example.json")])
        plan = json.loads(out)
        self.assertEqual((plan["bindings"], plan["quit_key"]), (dict(f9=0, f10=1, f11=2), "f12"))

    def test_auto_resolves_to_the_global_listener_only_when_pynput_exists(self):
        spec = importlib.machinery.ModuleSpec("pynput", None)
        module = types.ModuleType("pynput")
        module.__spec__ = spec
        with patch.dict(sys.modules, {"pynput": module}):
            plan = json.loads(self.run_main(["--dry-run"])[1])
        self.assertEqual((plan["resolved"], plan["quit_key"]), ("pynput", "f12"))
        with patch.dict(sys.modules, {"pynput": None}):
            plan = json.loads(self.run_main(["--dry-run"])[1])
            self.assertEqual((plan["resolved"], plan["quit_key"]), ("terminal", None))
            self.assertEqual(json.loads(self.run_main(["--dry-run", "--backend", "pynput"])[1])["resolved"], "pynput")

    def test_reserved_terminal_keys_are_dropped_with_a_warning(self):
        config = HERE / "bindings.example.json"
        code, out, err = self.run_main(["--dry-run", "--backend", "terminal", "--config", str(config)])
        self.assertEqual(json.loads(out)["bindings"], {"space": 0, "return": 0, "a": 1, "d": 2})
        self.assertEqual(err, "")
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "keys.json"
            path.write_text(json.dumps(dict(bindings={"q": 0, "x": 1})))
            code, out, err = self.run_main(["--dry-run", "--backend", "terminal", "--config", str(path)])
            code2, out2, err2 = self.run_main(["--dry-run", "--backend", "pynput", "--config", str(path)])
        self.assertEqual(json.loads(out)["bindings"], {"x": 1})
        self.assertEqual((json.loads(out2)["bindings"], err2), ({"q": 0, "x": 1}, ""))  # only a terminal reserves q
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
        self.assertIn("Forwarding terminal keys: space=K1", err)
        self.assertEqual(json.loads(out.splitlines()[0])["action"], "tap")

        link = FakeLink()
        class Interrupted(FakeSource):
            def get(self):
                raise KeyboardInterrupt
        code, _, _ = self.run_main([], link_factory=lambda port: link, sources=lambda keymap: Interrupted([]))
        self.assertEqual(code, 0)
        self.assertTrue(link.closed)

    def test_a_failing_key_source_is_reported_and_still_closes_the_link(self):
        link = FakeLink()

        class Broken(FakeSource):
            def get(self):
                raise RuntimeError("the global keyboard listener stopped")
        code, _, err = self.run_main([], link_factory=lambda port: link, sources=lambda keymap: Broken([]))
        self.assertEqual(code, 1)
        self.assertIn("bridge: the global keyboard listener stopped", err)
        self.assertTrue(link.closed)

    def test_holding_space_on_the_global_backend_is_one_device_hold(self):
        """Press, OS auto-repeat, release through the real SDK: one key_down, renewals, one key_up, no taps."""
        link = real_link()
        started = time.monotonic()

        class Scripted(FakeSource):
            def __init__(self):
                super().__init__([])
                self.script = deque([(0.0, ("down", "space")), (0.15, ("down", "space")), (0.30, ("down", "space")),
                                     (0.60, ("up", "space")), (0.70, ("quit", None))])

            def get(self):
                if self.script and time.monotonic() - started >= self.script[0][0]:
                    return self.script.popleft()[1]
                return None
        code, out, err = self.run_main(["--backend", "pynput"], link_factory=lambda port: link,
                                       sources=lambda keymap: Scripted())
        self.assertEqual(code, 0)
        history = [(r["op"], r.get("pressed"), r.get("key")) for r in link.serial.history]
        self.assertEqual(history[0], ("key", True, 0))
        self.assertEqual(history[-1], ("key", False, 0))
        self.assertEqual([h for h in history if h[1] is False], [("key", False, 0)])  # one release, not a burst of taps
        self.assertGreaterEqual(sum(1 for h in history if h[1] is True), 3)  # renewed inside the 350ms lease
        self.assertIn("Forwarding global keys with real press/release: space=K1", err)
        forwards = [json.loads(line) for line in out.splitlines() if '"forward"' in line]
        self.assertEqual([(f["action"], f["key"]) for f in forwards], [("down", "space"), ("up", "space")])


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

    def test_holding_a_key_is_not_a_burst_of_taps(self):
        """What a held Space looks like to a terminal: one character, a pause, then a fast stream."""
        now = [0.0]
        repeat = bridge.RepeatFilter(clock=lambda: now[0])
        taps = 0
        for at in [0.0] + [0.40 + 0.03 * i for i in range(40)]:  # 30 ms auto-repeat after a 400 ms delay
            now[0] = at
            taps += repeat.accept("space")
        self.assertEqual(taps, 2)  # the first character and the first repeat; the stream itself is dropped
        now[0] = 3.0
        self.assertTrue(repeat.accept("space"))  # a later press counts again

    def test_deliberate_taps_are_never_filtered(self):
        now = [0.0]
        repeat = bridge.RepeatFilter(clock=lambda: now[0])
        accepted = []
        for at in (0.0, 0.2, 0.4):  # a triple-click at 5 taps per second
            now[0] = at
            accepted.append(repeat.accept("space"))
        now[0] = 0.41
        accepted.append(repeat.accept("enter"))  # other keys have their own window
        self.assertEqual(accepted, [True, True, True, True])

    @unittest.skipIf(os.name == "nt", "pty is POSIX only")
    def test_a_repeating_stream_on_a_real_tty_yields_one_command(self):
        import pty
        master, slave = pty.openpty()
        stream = os.fdopen(slave, "r", closefd=False)
        try:
            with bridge.TerminalKeys(KEYMAP, stream) as keys:
                os.write(master, b" " * 30)  # one read worth of auto-repeat
                got = [command for command in (keys.get() for _ in range(40)) if command]
            self.assertEqual(got, [("tap", "space")])
        finally:
            stream.close()
            os.close(master)
            os.close(slave)


class PynputTests(unittest.TestCase):
    def fake_pynput(self):
        class Listener:
            trusted, alive = True, True

            def __init__(self, on_press, on_release):
                self.on_press, self.on_release, self.running, self.waited = on_press, on_release, False, False

            def start(self):
                self.running = True

            def wait(self):
                self.waited = True
                self.IS_TRUSTED = self.trusted  # the macOS listener sets this once it is ready

            def is_alive(self):
                return self.alive

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
        self.assertIn("uv run apps/key_report/pc/bridge.py", str(caught.exception))
        self.assertIn("--with pynput", str(caught.exception))

    def test_missing_keyboard_permission_is_announced(self):
        module, keyboard = self.fake_pynput()
        warnings = []
        with patch.dict(sys.modules, {"pynput": module, "pynput.keyboard": keyboard}):
            trusted = bridge.PynputKeys("f12", warnings.append)
            untrusted = bridge.PynputKeys("f12", warnings.append)
        untrusted.listener.trusted = False
        with trusted:
            self.assertTrue(trusted.listener.waited)
            self.assertEqual(warnings, [])
        with untrusted:
            pass
        self.assertEqual(len(warnings), 1)
        self.assertIn("Input Monitoring", warnings[0])

    def test_a_dead_listener_is_an_error_not_silence(self):
        module, keyboard = self.fake_pynput()
        with patch.dict(sys.modules, {"pynput": module, "pynput.keyboard": keyboard}):
            source = bridge.PynputKeys()
        with source:
            source.listener.on_press(types.SimpleNamespace(char=" "))
            source.listener.alive = False
            self.assertEqual(source.get(), ("down", "space"))  # queued keys are still delivered first
            with self.assertRaises(RuntimeError):
                source.get()

    def test_auto_prefers_the_global_listener_and_explains_every_fallback(self):
        module, keyboard = self.fake_pynput()
        module.__spec__ = importlib.machinery.ModuleSpec("pynput", None)
        warnings = []
        with patch.dict(sys.modules, {"pynput": module, "pynput.keyboard": keyboard}):
            name, source, keymap = bridge.pick_source("auto", {"space": 0}, "f12", warnings.append)
        self.assertEqual((name, type(source).__name__, keymap, warnings), ("pynput", "PynputKeys", {"space": 0}, []))

        with patch.dict(sys.modules, {"pynput": None}):  # not installed
            name, source, keymap = bridge.pick_source("auto", {"space": 0, "q": 1}, "f12", warnings.append)
            self.assertEqual((name, type(source).__name__, keymap), ("terminal", "TerminalKeys", {"space": 0}))
            self.assertTrue(any("cannot be detected" in w and "bridge.py" in w for w in warnings))
            with self.assertRaises(RuntimeError):
                bridge.pick_source("pynput", {"space": 0}, "f12", warnings.append)
            self.assertEqual(bridge.pick_source("terminal", {"space": 0}, "f12", warnings.append)[0], "terminal")

        broken = types.ModuleType("pynput")  # installed but the keyboard backend cannot load (no display)
        broken.__spec__ = importlib.machinery.ModuleSpec("pynput", None)
        broken.__path__ = []
        warnings.clear()
        with patch.dict(sys.modules, {"pynput": broken}):
            sys.modules.pop("pynput.keyboard", None)
            name, _, _ = bridge.pick_source("auto", {"space": 0}, "f12", warnings.append)
            self.assertEqual(name, "terminal")
            self.assertIn("using terminal keys instead", warnings[-1])
            with self.assertRaises(RuntimeError):
                bridge.pick_source("pynput", {"space": 0}, "f12", warnings.append)


if __name__ == "__main__":
    unittest.main()
