#!/usr/bin/env python3
"""Listen to the PC keyboard in real time and forward it to the ESP as USB virtual keys.

Every forwarded action and every device event (key_report, apps, game buttons) is
printed as one JSON line on stdout, so the output can be piped or logged.
Keys are never injected into other programs; the device decides taps, 500ms
confirmations and the global triple-click.

  terminal  (default) needs only a TTY. A terminal cannot report key release, so
            each key is a tap; `h` then a key is a 550ms hold, `t` is a triple tap
            on K1, `m` opens the menu, `r` cancels held keys, `q`/Esc quits.
  pynput    optional global listener with real press/release, so a physical hold
            is a device hold. It sees every key system-wide and cannot swallow
            them, so bind dedicated keys (see bridge.example.json).
"""
import argparse
import json
import os
import queue
import sys
import time
from collections import deque
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "framework" / "pc"))
from esphook_sdk import AppLink
from esphook_sdk.keys import bindings

TERMINAL_DIGITS = {"1": 0, "2": 1, "3": 2}
TERMINAL_COMMANDS = {"h": "hold prefix", "t": "triple K1", "m": "menu", "r": "release", "q": "quit"}
CSI_NAMES = {"A": "up", "B": "down", "C": "right", "D": "left"}
CHAR_NAMES = {" ": "space", "\r": "enter", "\n": "enter", "\t": "tab", "\x7f": "backspace", "\x04": "quit"}


def decode(text):
    """Split raw terminal input into key names; a lone ESC is "esc", unknown escapes are "csi"."""
    names, i = [], 0
    while i < len(text):
        ch = text[i]
        i += 1
        if ch == "\x1b":
            if i < len(text) and text[i] in "[O":
                j = i + 1
                while j < len(text) and not "@" <= text[j] <= "~":
                    j += 1
                names.append(CSI_NAMES.get(text[j] if j < len(text) else "", "csi"))
                i = j + 1
            else:
                names.append("esc")
        else:
            names.append(CHAR_NAMES.get(ch, ch.casefold()))
    return names


class TerminalCommands:
    """Turns key names typed in a terminal into bridge actions (no release events exist there)."""

    def __init__(self, keymap):
        self.keymap = keymap
        self.hold_next = False

    def feed(self, name):
        if name in ("esc", "quit", "q"):
            return ("quit", None)
        if name == "h":
            self.hold_next = True
            return None
        hold, self.hold_next = self.hold_next, False
        if name == "t":
            return ("triple", 0)
        if name == "m":
            return ("menu", None)
        if name == "r":
            return ("release", None)
        if name in self.keymap:
            return ("hold" if hold else "tap", name)
        return None


class TerminalKeys:
    """Non-blocking keyboard reader for a POSIX terminal (cbreak) or the Windows console."""

    def __init__(self, keymap, stream=None):
        self.stream = stream or sys.stdin
        self.commands = TerminalCommands(keymap)
        self.pending = deque()
        self.saved = None

    def __enter__(self):
        if not self.stream.isatty():
            raise RuntimeError("terminal backend needs an interactive terminal; use --backend pynput or run.py input")
        if os.name != "nt":
            import termios
            import tty
            self.fd = self.stream.fileno()
            self.saved = termios.tcgetattr(self.fd)
            tty.setcbreak(self.fd)  # no echo or line buffering; Ctrl+C still interrupts
        return self

    def __exit__(self, *exc):
        if self.saved is not None:
            import termios
            termios.tcsetattr(self.fd, termios.TCSADRAIN, self.saved)
        return False

    def _read(self):
        if os.name == "nt":
            import msvcrt
            text = ""
            while msvcrt.kbhit():
                ch = msvcrt.getwch()
                if ch in ("\x00", "\xe0"):  # function/arrow key: the next code is its identity
                    msvcrt.getwch()
                    ch = "\x1b[~"
                text += ch
            return text
        import select
        if not select.select([self.fd], [], [], 0)[0]:
            return ""
        return os.read(self.fd, 64).decode("utf-8", "ignore")

    def get(self):
        if not self.pending:
            self.pending.extend(decode(self._read()))
        while self.pending:
            command = self.commands.feed(self.pending.popleft())
            if command:
                return command
        return None


class PynputKeys:
    """Global keyboard listener with real press/release events (optional pynput package)."""

    def __init__(self, quit_key="f12"):
        try:
            from pynput import keyboard
        except ImportError as exc:
            raise RuntimeError("--backend pynput needs the pynput package: "
                               "uv run --with pyserial --with pynput python run.py bridge --backend pynput "
                               "(macOS: allow Input Monitoring for the terminal)") from exc
        self.quit_key = quit_key.casefold()
        self.events = queue.Queue()
        self.listener = keyboard.Listener(on_press=lambda key: self._put("down", key),
                                          on_release=lambda key: self._put("up", key))

    @staticmethod
    def name(key):
        char = getattr(key, "char", None)
        if char:
            return CHAR_NAMES.get(char, char.casefold())
        return (getattr(key, "name", None) or "").casefold()

    def _put(self, action, key):  # runs on the listener thread; only the main thread talks to USB
        name = self.name(key)
        if not name:
            return
        if name == self.quit_key:
            if action == "down":
                self.events.put(("quit", None))
        else:
            self.events.put((action, name))

    def __enter__(self):
        self.listener.start()
        return self

    def __exit__(self, *exc):
        self.listener.stop()
        return False

    def get(self):
        try:
            return self.events.get_nowait()
        except queue.Empty:
            return None


class KeyBridge:
    """Applies keyboard actions to one AppLink and relays device events."""

    def __init__(self, link, keymap, *, hold_ms=550, emit=None, show_events=True):
        if hold_ms < 500:
            raise ValueError("hold needs at least 500 milliseconds to confirm")
        self.link, self.keymap, self.hold_ms, self.show_events = link, keymap, hold_ms, show_events
        self.emit = emit or (lambda item: print(json.dumps(item, ensure_ascii=False), flush=True))
        self.down = {}  # USB key -> names currently held (several PC keys may share one USB key)

    def handle(self, action, key=None):
        """Run one action; returns False when the user asked to stop."""
        if action == "quit":
            return False
        try:
            if action == "menu":
                self.link.menu()
                self.emit(dict(type="forward", action="menu"))
            elif action == "release":
                self.link.release_keys()
                self.down.clear()
                self.emit(dict(type="forward", action="release"))
            else:
                self._key_action(action, key)
        except (RuntimeError, TimeoutError) as exc:  # device refused or did not answer; USB errors still abort
            self.emit(dict(type="error", action=action, key=key, error=str(exc)))
        return True

    def _key_action(self, action, key):
        usb = key if isinstance(key, int) else self.keymap.get(key)
        if usb is None:
            return
        if action == "down":
            names = self.down.setdefault(usb, set())
            if key in names:  # operating-system auto-repeat
                return
            names.add(key)
            if len(names) > 1:  # another PC key already holds this USB key
                return
            self.link.key_down(usb)
        elif action == "up":
            names = self.down.get(usb)
            if not names or key not in names:
                return
            names.discard(key)
            if names:
                return
            del self.down[usb]
            self.link.key_up(usb)
        elif action == "tap":
            self.link.tap(usb)
        elif action == "hold":
            self.link.hold(usb, self.hold_ms)
        elif action == "triple":
            self.link.triple(usb)
        else:
            raise ValueError(f"unknown action {action}")
        self.emit(dict(type="forward", action=action, key=key, usb=usb))

    def pump(self, timeout=0.03):
        """Read the link for `timeout` seconds (also renews held keys) and print device events."""
        for event in self.link.poll(timeout):
            if self.show_events:
                self.emit(event)

    def run(self, source):
        with source:
            while True:
                command = source.get()
                if command is not None and not self.handle(*command):
                    break
                # Idle waits on the serial port; after a command only renew held keys.
                self.pump(0.0 if command is not None else 0.03)


def banner(backend, keymap, hold_ms, quit_key):
    names = ", ".join(f"{name}=K{usb + 1}" for name, usb in sorted(keymap.items()))
    if backend == "terminal":
        return (f"Forwarding terminal keys: {names}. Each key is a tap; h+key holds {hold_ms}ms, "
                "t = triple K1, m = menu, r = release, q/Esc/Ctrl+C = quit.")
    return f"Forwarding global keys with real press/release: {names}. Press {quit_key} or Ctrl+C to stop."


def main(argv=None, link_factory=AppLink, sources=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--port")
    p.add_argument("--backend", choices=("terminal", "pynput"), default="terminal")
    p.add_argument("--config", type=Path, help="PC bindings JSON (see bindings.example.json)")
    p.add_argument("--app", help="open this app before forwarding")
    p.add_argument("--hold-ms", type=int, default=550, help="duration of h+key in terminal mode")
    p.add_argument("--quit-key", default="f12", help="global stop key for the pynput backend")
    p.add_argument("--no-events", action="store_true", help="do not print device events")
    p.add_argument("--dry-run", action="store_true", help="print the plan without opening USB or the keyboard")
    a = p.parse_args(argv)
    if a.hold_ms < 500:
        p.error("--hold-ms must be at least 500 for a confirmation")
    keymap = bindings(a.config)
    if a.backend == "terminal":
        if a.config is None:
            keymap = dict(keymap, **TERMINAL_DIGITS)
        for name in sorted(set(keymap) & set(TERMINAL_COMMANDS)):
            print(f"warning: terminal key '{name}' is reserved for {TERMINAL_COMMANDS[name]}; its binding is ignored", file=sys.stderr)
            keymap.pop(name)
    if a.dry_run:
        print(json.dumps(dict(backend=a.backend, bindings=keymap, hold_ms=a.hold_ms, app=a.app,
                              quit_key=a.quit_key if a.backend == "pynput" else None), ensure_ascii=False))
        return 0
    if sources is not None:
        source = sources(keymap)
    else:
        source = TerminalKeys(keymap) if a.backend == "terminal" else PynputKeys(a.quit_key)
    link = link_factory(a.port)
    try:
        if a.app:
            link.open(a.app)
            link.wait_active(a.app)
        bridge = KeyBridge(link, keymap, hold_ms=a.hold_ms, show_events=not a.no_events)
        print(banner(a.backend, keymap, a.hold_ms, a.quit_key), file=sys.stderr)
        bridge.run(source)
    except KeyboardInterrupt:
        pass
    finally:
        link.close()  # cancels any key still held
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
