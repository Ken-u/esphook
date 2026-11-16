import json
import sys
import unittest
from decimal import Decimal
from unittest.mock import patch
from stock_t_core import StockT, TradingDay
from stock_t_data import prepare_days, demo_rows, SLOTS, request_json
from stock_link import GameLink


def day():
    return TradingDay("sh600000", "测试", "2026-11-12",
                      [[1000,1000,1000,1000,1000,1000],
                       [1000,1100,1000,1100,1050,1040],
                       [1100,1200,1100,1200,1100,1080]], ["09:35", "09:40", "09:45"], 0)


class CoreTests(unittest.TestCase):
    def test_hold_pause_buy_and_resume(self):
        g = StockT(day())
        g.button(True, 0.5, 0)
        g.update(0.99)
        self.assertEqual(g.cursor, 0)
        self.assertFalse(g.shares)
        g.update(1.0)
        self.assertEqual(g.shares, 10000)
        self.assertEqual(g.cash, 0)
        g.button(False, 1.2, duration=0.7)
        self.assertEqual(g.trades, 1)  # Release after confirmation is never a sell.
        g.update(1.5)
        self.assertEqual(g.cursor, 1)
        self.assertEqual(g.equity, 110000)
        g.button(True, 1.6, 1)
        g.button(False, 1.7, duration=0.1)
        self.assertEqual(g.cash, 110000)
        self.assertEqual(g.shares, 0)
        self.assertEqual(g.frame(24, 2.2)["pct_bps"], 1000)

    def test_cancel_and_late_release(self):
        g = StockT(day())
        g.button(False, 0.1)
        self.assertEqual(g.trades, 0)
        g.button(True, 0.2, 0)
        g.button(False, 0.6, duration=0.4)
        self.assertEqual(g.trades, 0)
        g.update(1.39)
        self.assertEqual(g.cursor, 0)
        g.update(1.41)
        self.assertEqual(g.cursor, 1)
        g.button(True, 1.6, 1)
        g.button(False, 1.8, duration=1.05)  # Device duration survives transport delay.
        self.assertEqual(g.trades, 1)
        self.assertTrue(g.shares)

    def test_displayed_price_and_deadline(self):
        g = StockT(day())
        g.update(1)
        g.button(True, 1.1, 0)
        g.update(1.6)
        self.assertEqual(g.price, 10)
        self.assertEqual(g.trades, 1)
        g.button(False, 2.2, duration=1.1)
        g.update(3.1); g.update(4.1); g.update(5.1)
        self.assertEqual(g.phase, "result")
        self.assertEqual(g.frame(24, 5.1)["pct_bps"], 2000)
        g.button(True, 5.2)
        g.update(6.2)
        self.assertEqual(g.request, "next")
        self.assertEqual(g.phase, "loading")
        self.assertEqual(g.trades, 1)
        h = StockT(day()); h.update(1); h.update(2); h.update(3)
        h.button(True, 3.1); h.button(False, 3.2, duration=0.1)
        self.assertEqual(h.request, "exit")
        h = StockT(day()); h.button(True, 0, 2); h.update(1)
        self.assertFalse(h.shares)  # A forged future cursor never buys.

    def test_all_in_precision(self):
        g = StockT(day()); g.day.bars[0][3] = 333
        g.button(True, 0, 0); g.update(1)
        self.assertEqual(g.frame(1, 1)["pnl_cents"], 0)
        g.button(False, 1.1, duration=1.1)
        g.button(True, 1.2, 0); g.button(False, 1.3, duration=0.1)
        self.assertEqual(g.cash.quantize(Decimal("0.01")), Decimal("100000.00"))


class DataTests(unittest.TestCase):
    def test_full_days_and_ma(self):
        rows = demo_rows()
        days = prepare_days(rows, "sh600000", "test", today="9999-12-31")
        self.assertTrue(days)
        self.assertEqual(len(days[-1].bars), 108)
        self.assertEqual(days[-1].clocks[60:], SLOTS)
        self.assertTrue(all(all(x > 0 for x in b) for b in days[-1].bars))
        # Changing future bars cannot change any earlier moving average.
        before = days[-1].bars[60:70]
        for row in rows[-10:]:
            row.update(open="200", high="200", low="200", close="200")
        later = prepare_days(rows, "sh600000", "test", today="9999-12-31")
        self.assertEqual(before, later[-1].bars[60:70])
        incomplete = [r for r in rows if r["day"] != rows[-1]["day"]]
        self.assertNotEqual(prepare_days(incomplete, "sh600000", "test", today="9999-12-31")[-1].date, days[-1].date)

    def test_source_is_json_not_executable_jsonp(self):
        class Response:
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def read(self, size): return b'/*<script>ignore</script>*/=([{"day":"x"}]);'
        with patch("urllib.request.urlopen", return_value=Response()):
            self.assertEqual(request_json("https://example.test/bars"), [{"day": "x"}])


class LinkTests(unittest.TestCase):
    def test_logs_events_and_matching_ack(self):
        class Serial:
            in_waiting = 0
            def __init__(self): self.messages = bytearray(); self.sent = []
            def write(self, raw):
                self.sent.append(json.loads(raw[5:]))
                seq = self.sent[-1]["seq"]
                self.messages.extend(b'I (1) firmware log\n')
                self.messages.extend(b'{"v":1,"type":"game_button","id":24,"pressed":true,"ms":10,"cursor":0}\n')
                self.messages.extend(json.dumps(dict(v=1, type="game_ack", seq=seq, ok=True)).encode() + b'\n')
            def read(self, n):
                raw = bytes(self.messages[:7]); del self.messages[:7]; return raw
        link = object.__new__(GameLink)
        from collections import deque
        link.serial, link.sequence, link.events, link.pending = Serial(), 0, deque(), bytearray()
        link.selected = False
        link.request(dict(op="heartbeat", id=24))
        self.assertEqual(len(link.events), 1)
        self.assertEqual(link.events[0]["cursor"], 0)
        with self.assertRaises(ValueError): link.request(dict(op="x", id=24, label="x" * 2000))


if __name__ == "__main__": unittest.main()
