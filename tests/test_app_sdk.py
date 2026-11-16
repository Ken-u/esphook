import json
from collections import deque
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "framework/pc"))
from esphook_sdk import AppLink

class FakeSerial:
    in_waiting = 0
    def __init__(self): self.rx = bytearray()
    def write(self, raw):
        self.sent = json.loads(raw[4:])
        self.rx.extend(b'I (1) log\n{"v":1,"type":"app_event","app":"demo","action":"tap"}\n')
        self.rx.extend(json.dumps(dict(v=1,type="app_ack",seq=self.sent["seq"]+1,ok=True)).encode()+b'\n')
        self.rx.extend(json.dumps(dict(v=1,type="app_ack",seq=self.sent["seq"],ok=True,queued=True)).encode()+b'\n')
    def read(self, n):
        raw = bytes(self.rx[:9]); del self.rx[:9]; return raw

class SdkTests(unittest.TestCase):
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
