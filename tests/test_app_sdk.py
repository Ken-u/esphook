import importlib.util
import json
import types
from collections import deque
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "framework/pc"))
from esphook_sdk import AppLink
spec = importlib.util.spec_from_file_location("check_apps", ROOT / "scripts/check-apps.py")
check_apps = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check_apps)

class FakeSerial:
    in_waiting = 0
    def __init__(self):
        self.rx = bytearray(); self.history=[]
        self.hardware=dict(active="auto",configured="auto",restart_required=False,servo="unknown")
        self.mapping=dict(physical=[0,1,2],usb=[0,1,2],words=["ok","continue","done"])
    def write(self, raw):
        prefix,body=raw.split(b" ",1);self.sent = json.loads(body);self.history.append((prefix,self.sent))
        self.rx.extend(b'I (1) log\n{"v":1,"type":"app_event","app":"demo","action":"tap"}\n')
        self.rx.extend(json.dumps(dict(v=1,type="app_ack",seq=self.sent["seq"]+1,ok=True)).encode()+b'\n')
        if "mapping" in self.sent:self.mapping=self.sent["mapping"]
        if self.sent.get("op")=="hardware" and "profile" in self.sent:
            self.hardware.update(configured=self.sent["profile"],restart_required=self.sent["profile"]!="auto")
        self.rx.extend(json.dumps(dict(v=1,type="app_ack" if prefix==b"app" else "game_ack",seq=self.sent["seq"],ok=True,queued=True,mapping=self.mapping,hardware=self.hardware)).encode()+b'\n')
    def read(self, n):
        raw = bytes(self.rx[:9]); del self.rx[:9]; return raw

class LineStateSerial:
    """pyserial's open order on a real OS: both lines asserted, then DTR applied before RTS."""
    def __init__(self, port=None, **settings):
        self.states, self._dtr, self._rts, self.opened = [], True, True, False
        self.port = port
    def _set(self, name, value):
        setattr(self, name, value)
        if self.opened:
            self.states.append((self._dtr, self._rts))
    dtr = property(lambda s: s._dtr, lambda s, v: s._set("_dtr", v))
    rts = property(lambda s: s._rts, lambda s, v: s._set("_rts", v))
    def open(self):
        self.opened = True
        self.states.append((True, True))
        self.states.append((self._dtr, True))
        self.states.append((self._dtr, self._rts))
    def close(self):
        pass


class ListAckSerial:
    """Answers `list` with the real catalog's worst-case ack, split into tiny reads between log noise."""
    in_waiting = 0

    def __init__(self, frame):
        self.frame, self.rx = frame, bytearray()

    def write(self, raw):
        request = json.loads(raw.split(b" ", 1)[1])
        assert request["op"] == "list"
        ack = json.loads(self.frame)
        ack["seq"] = request["seq"]
        self.rx.extend(b"I (88) cdc: log line\n" + json.dumps(ack, ensure_ascii=False, separators=(",", ":")).encode() + b"\n")

    def read(self, n):
        raw = bytes(self.rx[:5])
        del self.rx[:5]
        return raw


class SdkTests(unittest.TestCase):
    def link(self,cls=AppLink):
        link=object.__new__(cls);link.serial=FakeSerial();link.sequence=0;link.events=deque();link.pending=bytearray();link.selected=False;return link

    def test_virtual_keys_and_mapping(self):
        from esphook_sdk import UsbLink
        class ChartLink(UsbLink):prefix="game";ack_type="game_ack"
        link=self.link(ChartLink)
        link.set_bindings(dict(space=0,enter=0))
        link.key_down("space");link.key_up("space")
        self.assertEqual(link.serial.history[-1][0],b"app")
        self.assertFalse(link.serial.sent["pressed"])
        link.request(dict(op="heartbeat",id=24));self.assertEqual(link.serial.history[-1][0],b"game")
        mapping=link.set_keymap(usb=[2,0,1],words=["yes","next","done"])
        self.assertEqual(mapping["usb"],[2,0,1]);self.assertEqual(mapping["words"][0],"yes")
        with self.assertRaises(ValueError):link.set_keymap(usb=[0,1,9])
        with self.assertRaises(ValueError):link.key_down(True)
        with self.assertRaises(ValueError):link.set_bindings(dict(space=9))
        link.key_down(0)
        with self.assertRaises(RuntimeError):link.set_bindings(dict(space=1))
        link.release_keys();self.assertEqual(link.serial.sent["op"],"keys_release")

    def test_hardware_profile_round_trip(self):
        link=self.link()
        self.assertEqual(link.hardware()["active"],"auto")
        info=link.set_hardware_profile("robot",reboot=True)
        self.assertEqual(info["configured"],"robot")
        self.assertTrue(info["restart_required"])
        self.assertEqual(link.serial.history[-1][0],b"app")
        self.assertIs(link.serial.sent["reboot"],True)
        with self.assertRaises(ValueError):link.set_hardware_profile("missing")
        with self.assertRaises(ValueError):link.set_hardware_profile("robot",reboot=1)

    def test_confirm_renewals_and_abort(self):
        link=self.link()
        link.hold(0,520)
        downs=[v for _,v in link.serial.history if v.get("pressed") is True]
        self.assertGreaterEqual(len(downs),3)
        self.assertFalse(link.serial.history[-1][1]["pressed"])
        with patch("esphook_sdk.keys.time.sleep",side_effect=KeyboardInterrupt):
            with self.assertRaises(KeyboardInterrupt):link.press(0,100)
        self.assertEqual(link.serial.sent["op"],"keys_release")

    def test_generic_ack_and_events(self):
        link = object.__new__(AppLink)
        link.serial, link.sequence, link.events, link.pending = FakeSerial(), 0, deque(), bytearray()
        link.selected = False
        reply = link.open("demo")
        self.assertTrue(reply["queued"])
        self.assertEqual(link.serial.sent["app"], "demo")
        self.assertEqual(link.events[0]["action"], "tap")
        self.assertEqual(len(link.events), 1)
        link.sequence = 2147483647
        link.message("demo", dict(text="hello"))
        self.assertEqual(link.sequence, 1)
        with self.assertRaises(ValueError): link.message("demo", dict(text="x"*2000))

    def test_catalog_survives_ack_longer_than_the_old_tx_ring(self):
        # Regression for test-report-usb-ack-and-gestures.md: the `list` ack exceeded IDF's 256-byte default TX ring
        # and never reached the PC. The firmware budget and the SDK parse are both checked against the real catalog.
        catalog = []
        errors, count = check_apps.check(ROOT, catalog)
        self.assertEqual(errors, [])
        frame = check_apps.list_ack_frame(ROOT, catalog)
        self.assertGreater(len(frame.encode()) + 1, 256)
        self.assertLessEqual(len(frame.encode()) + 1, check_apps.tx_budget(ROOT))
        link = object.__new__(AppLink)
        link.serial, link.sequence, link.events, link.pending, link.selected = ListAckSerial(frame), 0, deque(), bytearray(), False
        apps = link.catalog()
        self.assertEqual([a["id"] for a in apps], ["basketball", "demo", "dino", "face", "hook", "key_report", "muyu", "radar", "settings", "ski", "stock", "wedding"])
        self.assertEqual(apps[0]["title"], "投篮")

    def test_opening_the_port_never_resets_the_chip(self):
        # USB Serial/JTAG resets the chip on DTR low + RTS high. Clearing DTR first (the old
        # `dtr = rts = False`) passes through that state, rebooting the device on every connect.
        RESET = (False, True)
        legacy = LineStateSerial()
        legacy.dtr = legacy.rts = False
        legacy.open()
        self.assertIn(RESET, legacy.states)  # the model does detect the old behaviour
        opened = []
        def factory(port=None, **settings):
            opened.append(LineStateSerial(port, **settings))
            return opened[-1]
        fake = types.SimpleNamespace(Serial=factory)
        from esphook_sdk import link as link_module
        with patch.dict(sys.modules, {"serial": fake}):
            sdk = AppLink("/dev/cu.test")
            self.assertEqual(opened[-1].port, "/dev/cu.test")
            self.assertNotIn(RESET, opened[-1].states)
            self.assertEqual(opened[-1].states[-1], (True, False))
            link_module.open_serial("/dev/cu.test", timeout=0.1)
            self.assertNotIn(RESET, opened[-1].states)

    def test_list_ack_budget_guard(self):
        catalog = [dict(id=f"app_{i:02d}_" + "x" * 10, title="四个汉字", hidden=False, standby=True) for i in range(40)]
        frame = check_apps.list_ack_frame(ROOT, catalog)
        self.assertGreater(len(frame.encode()) + 1, check_apps.tx_budget(ROOT))
        hidden = [dict(entry, hidden=True) for entry in catalog]
        self.assertNotIn('"apps":[{', check_apps.list_ack_frame(ROOT, hidden))

if __name__ == "__main__": unittest.main()
