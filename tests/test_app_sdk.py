import json
from collections import deque
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "framework/pc"))
from esphook_sdk import AppLink

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

if __name__ == "__main__": unittest.main()
