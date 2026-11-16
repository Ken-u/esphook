"""USB virtual keys on the same serial link as apps and PC-hosted games."""
import json
from pathlib import Path
import time

DEFAULT_BINDINGS = {"space": 0, "enter": 1, "return": 1, "tab": 2, "k1": 0, "k2": 1, "k3": 2}

def bindings(value=None):
    if isinstance(value, (str, Path)):
        value = json.loads(Path(value).read_text(encoding="utf-8"))
        value = value.get("bindings", value)
    value = DEFAULT_BINDINGS if value is None else value
    if not isinstance(value, dict) or not value:
        raise ValueError("bindings must be a non-empty name-to-key object")
    result = {}
    for name, key in value.items():
        if not isinstance(name, str) or not name or len(name) > 32 or type(key) is not int or not 0 <= key < 3:
            raise ValueError("binding names must map to integer USB key indices 0..2")
        normalized = name.casefold()
        if normalized in result:
            raise ValueError("duplicate normalized binding")
        result[normalized] = key
    return result

def validate_keymap(mapping):
    if not isinstance(mapping, dict) or set(mapping) != {"physical", "usb", "words"}:
        raise ValueError("keymap requires physical, usb and words")
    for name in ("physical", "usb"):
        values = mapping[name]
        if not isinstance(values, list) or len(values) != 3 or any(type(v) is not int or not 0 <= v < 3 for v in values):
            raise ValueError("key maps must contain three integer logical keys 0..2")
    words = mapping["words"]
    if not isinstance(words, list) or len(words) != 3 or any(
        not isinstance(w, str) or not 1 <= len(w) <= 15 or any(not 0x20 <= ord(c) <= 0x7e for c in w) for w in words
    ):
        raise ValueError("reply words must be three printable ASCII strings of 1..15 characters")
    return mapping

class VirtualKeys:
    """Mixin for a single-owner UsbLink. poll() renews held USB keys."""
    def _key(self, key):
        if isinstance(key, str):
            key = getattr(self, "_bindings", DEFAULT_BINDINGS).get(key.casefold())
        if type(key) is not int or not 0 <= key < 3:
            raise ValueError("key must be 0..2 or a configured binding name")
        return key

    def set_bindings(self, mapping):
        if getattr(self, "_usb_pressed", {}):
            raise RuntimeError("release USB keys before changing PC bindings")
        self._bindings = bindings(mapping)

    def key_down(self, key=0):
        key = self._key(key)
        reply = self.request(dict(op="key", key=key, pressed=True), channel="app")
        if not hasattr(self, "_usb_pressed"):
            self._usb_pressed = {}
        self._usb_pressed[key] = time.monotonic()
        return reply

    def key_up(self, key=0):
        key = self._key(key)
        reply = self.request(dict(op="key", key=key, pressed=False), channel="app")
        getattr(self, "_usb_pressed", {}).pop(key, None)
        return reply

    def _renew_keys(self):
        for key, stamp in list(getattr(self, "_usb_pressed", {}).items()):
            if time.monotonic() - stamp >= 0.1:
                self.key_down(key)

    def release_keys(self):
        reply = self.request(dict(op="keys_release"), channel="app")
        self._usb_pressed = {}
        return reply

    def press(self, key=0, duration_ms=60):
        if type(duration_ms) not in (int, float) or not 1 <= duration_ms <= 60000:
            raise ValueError("duration must be 1..60000 milliseconds")
        key = self._key(key)
        self.key_down(key)
        try:
            deadline = time.monotonic() + duration_ms / 1000
            while time.monotonic() < deadline:
                time.sleep(min(0.08, max(0, deadline - time.monotonic())))
                self._renew_keys()
        except BaseException:
            try:
                self.release_keys()
            except (OSError, RuntimeError, TimeoutError):
                pass
            raise
        else:
            self.key_up(key)

    def tap(self, key=0):
        self.press(key, 60)

    def hold(self, key=0, duration_ms=550):
        if duration_ms < 500:
            raise ValueError("confirmation requires at least 500 milliseconds")
        self.press(key, duration_ms)

    def triple(self, key=0):
        for index in range(3):
            if index:
                time.sleep(0.06)
            self.tap(key)

    def keymap(self):
        return self.request(dict(op="keymap"), channel="app")["mapping"]

    def set_keymap(self, *, physical=None, usb=None, words=None):
        mapping = self.keymap()
        for name, value in (("physical", physical), ("usb", usb), ("words", words)):
            if value is not None:
                mapping[name] = value
        validate_keymap(mapping)
        return self.request(dict(op="keymap", mapping=mapping), channel="app")["mapping"]
