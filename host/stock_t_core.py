"""Deterministic replay and virtual trading. No network, device or future-price execution."""
from dataclasses import dataclass
from decimal import Decimal, ROUND_HALF_UP

HOLD_SECONDS = 0.5
TAP_SECONDS = 0.5


@dataclass
class TradingDay:
    symbol: str
    name: str
    date: str
    bars: list  # [open, high, low, close, ma30, ma60], prices in cents
    clocks: list
    first: int


class StockT:
    def __init__(self, day, now=0.0, interval=1.0):
        if interval <= 0 or day.first >= len(day.bars):
            raise ValueError("invalid replay")
        self.day, self.interval = day, interval
        self.cursor = day.first
        self.phase = "play"
        self.cash = Decimal("100000")
        self.shares = Decimal(0)
        self.initial = self.cash
        self.trades = 0
        self.down_at = None
        self.consumed = False
        self.next_bar = now + interval
        self.last_action = None
        self.request = None
        self.pause_remaining = interval

    @property
    def price(self):
        return Decimal(self.day.bars[self.cursor][3]) / 100

    @property
    def equity(self):
        return self.cash + self.shares * self.price

    def _buy(self):
        if not self.shares:
            self.shares = self.cash / self.price
            self.cash = Decimal(0)
            self.trades += 1
            self.last_action = ("buy", self.cursor)

    def _sell(self):
        if self.shares:
            self.cash = self.shares * self.price
            self.shares = Decimal(0)
            self.trades += 1
            self.last_action = ("sell", self.cursor)

    def button(self, pressed, now, cursor=None, duration=None):
        # The device supplies the displayed cursor; never trade a price not shown.
        if pressed:
            if self.down_at is not None or self.phase == "loading":
                return
            if self.phase == "play" and cursor is not None:
                if not self.day.first <= cursor <= self.cursor:
                    return
                rewind = cursor < self.cursor
                self.cursor = cursor
            else:
                rewind = False
            self.pause_remaining = self.interval if rewind else max(0.0, self.next_bar - now)
            self.down_at = now
            self.consumed = False
            return
        if self.down_at is None:
            return  # Release after startup/reconnect is not a click.
        elapsed = max(0.0, now - self.down_at) if duration is None else max(0.0, duration)
        if not self.consumed and elapsed >= HOLD_SECONDS:
            self._hold(now)
        elif not self.consumed and elapsed < TAP_SECONDS:
            if self.phase == "play":
                self._sell()
            elif self.phase == "result":
                self.request = "exit"
        if self.phase == "play" and not self.consumed:
            self.next_bar = now + self.pause_remaining
        self.down_at = None
        self.consumed = False

    def _hold(self, now):
        self.consumed = True
        if self.phase == "play":
            self._buy()
            self.next_bar = now + self.pause_remaining
        elif self.phase == "result":
            self.phase = "loading"
            self.request = "next"

    def confirm(self, now):
        if self.down_at is not None and not self.consumed:
            self._hold(now)

    def update(self, now):
        if self.down_at is not None and not self.consumed and now - self.down_at >= HOLD_SECONDS:
            self._hold(now)
        if self.phase != "play" or (self.down_at is not None and not self.consumed):
            return
        # Do not fast-forward unseen candles after a slow device/network request.
        if now >= self.next_bar:
            if self.cursor + 1 == len(self.day.bars):
                self.phase = "result"
                self.down_at = None
            else:
                self.cursor += 1
                self.next_bar = now + self.interval

    def frame(self, session, now):
        def rounded(value):
            return int(value.quantize(Decimal(1), rounding=ROUND_HALF_UP))
        pct = (self.equity / self.initial - 1) * 10000
        base = (self.price / (Decimal(self.day.bars[self.day.first][3]) / 100) - 1) * 10000
        progress = 0 if self.down_at is None else 500 if self.consumed else min(500, int((now - self.down_at) * 1000))
        return dict(op="show", id=session, cursor=self.cursor, state=self.phase,
                    held_ms=max(0, progress), holding=bool(self.shares), trades=self.trades,
                    pct_bps=rounded(pct), baseline_bps=rounded(base),
                    pnl_cents=rounded((self.equity - self.initial) * 100), clock=self.day.clocks[self.cursor])
