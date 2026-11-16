"""Sina historical bars, causal MA preparation and a cached A-share universe."""
import csv
import datetime as dt
import json
import os
import random
import re
import time
import urllib.parse
import urllib.request
from decimal import Decimal, ROUND_HALF_UP
from pathlib import Path
from stock_t_core import TradingDay

SYMBOL = re.compile(r"(?:sh|sz|bj)[0-9]{6}\Z")
SLOTS = [f"{h:02d}:{m:02d}" for h, m in
         [(9 + (35 + i * 5) // 60, (35 + i * 5) % 60) for i in range(24)] +
         [(13 + (5 + i * 5) // 60, (5 + i * 5) % 60) for i in range(24)]]


def request_json(url):
    req = urllib.request.Request(url, headers={"User-Agent": "esphook-stock-t/1.0",
                                               "Referer": "https://finance.sina.com.cn/"})
    with urllib.request.urlopen(req, timeout=20) as response:
        raw = response.read(4_000_001)
    if len(raw) > 4_000_000:
        raise ValueError("market response too large")
    raw = raw.decode("utf-8")
    # The minute endpoint wraps its JSON array in a JSONP callback; never eval it.
    start, end = raw.find("["), raw.rfind("]")
    if start < 0 or end < start:
        raise ValueError("market source did not return a JSON array")
    value = json.loads(raw[start:end + 1])
    if not isinstance(value, list):
        raise ValueError("invalid market response")
    return value


def cache_read(path, age):
    try:
        if time.time() - path.stat().st_mtime <= age:
            return json.loads(path.read_text())
    except (OSError, ValueError):
        pass
    return None


def cache_write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(value, ensure_ascii=False))
    temporary.replace(path)


def universe(cache):
    path = cache / "universe.json"
    saved = cache_read(path, 86400)
    if saved:
        return saved
    stocks = {}
    for page in range(1, 81):
        params = urllib.parse.urlencode(dict(page=page, num=100, sort="symbol", asc=1, node="hs_a", symbol="", _s_r_a="init"))
        rows = request_json("https://vip.stock.finance.sina.com.cn/quotes_service/api/json_v2.php/Market_Center.getHQNodeData?" + params)
        for row in rows:
            symbol = row.get("symbol", "")
            if re.fullmatch(r"sh(?:60[0135][0-9]{3}|688[0-9]{3})|sz(?:00[0-3][0-9]{3}|30[01][0-9]{3})|bj(?:[48][0-9]{5}|920[0-9]{3})", symbol):
                stocks[symbol] = str(row.get("name") or symbol)
        if len(rows) < 100:
            break
        time.sleep(0.05)
    if not stocks:
        raise ValueError("cannot obtain A-share universe; use --symbols or --csv")
    value = list(stocks.items())
    cache_write(path, value)
    return value


def minutes(symbol, cache):
    if not SYMBOL.fullmatch(symbol):
        raise ValueError("symbol must look like sh600000 or sz000001")
    path = cache / (symbol + "-5m.json")
    rows = cache_read(path, 21600)
    if rows is None:
        params = urllib.parse.urlencode(dict(symbol=symbol, scale=5, ma="no", datalen=1023))
        rows = request_json("https://quotes.sina.cn/cn/api/jsonp_v2.php/=/CN_MarketDataService.getKLineData?" + params)
        if not rows:
            raise ValueError(f"no minute bars for {symbol}")
        cache_write(path, rows)
    return rows


def prepare_days(rows, symbol, name, today=None):
    """Require a full 48-bar day plus 60 displayed context bars and 59 MA warmup bars."""
    today = today or dt.datetime.now(dt.timezone(dt.timedelta(hours=8))).date().isoformat()
    values = {}
    for row in rows:
        stamp = dt.datetime.fromisoformat(str(row["day"]))
        clock = stamp.strftime("%H:%M")
        if clock not in SLOTS:
            continue
        prices = []
        for key in ("open", "high", "low", "close"):
            price = Decimal(str(row[key]))
            if not price.is_finite() or price <= 0 or price > 1000000:
                raise ValueError("invalid OHLC price")
            prices.append(int((price * 100).quantize(Decimal(1), rounding=ROUND_HALF_UP)))
        o, h, l, c = prices
        if l > min(o, c) or h < max(o, c):
            raise ValueError("inconsistent OHLC")
        values[stamp] = prices
    ordered = sorted(values)
    bars, clocks = [], []
    closes = []
    for stamp in ordered:
        prices = values[stamp]
        closes.append(prices[3])
        ma30 = (sum(closes[-30:]) + 15) // 30 if len(closes) >= 30 else 0
        ma60 = (sum(closes[-60:]) + 30) // 60 if len(closes) >= 60 else 0
        bars.append(prices + [ma30, ma60])
        clocks.append(stamp.strftime("%H:%M"))
    result = []
    grouped = {}
    for i, stamp in enumerate(ordered):
        grouped.setdefault(stamp.date().isoformat(), []).append(i)
    for date, indices in grouped.items():
        first = indices[0]
        if date >= today or first < 119 or len(indices) != 48:
            continue
        if [clocks[i] for i in indices] != SLOTS or indices != list(range(first, first + 48)):
            continue
        result.append(TradingDay(symbol, name, date, bars[first - 60:first + 48], clocks[first - 60:first + 48], 60))
    return result


class DataSource:
    def __init__(self, symbols=None, csv_file=None, demo=False, seed=None, cache=None):
        self.rng = random.Random(seed)
        self.symbols = symbols
        self.csv_file = csv_file
        self.demo = demo
        self.cache = Path(cache or os.environ.get("XDG_CACHE_HOME", str(Path.home() / ".cache")))
        self.cache = self.cache / "esphook" / "stock-t"
        self.last = None

    def pick(self):
        if self.csv_file:
            with open(self.csv_file, newline="", encoding="utf-8-sig") as f:
                rows = list(csv.DictReader(f))
            candidates = prepare_days(rows, "sh000000", "CSV", today="9999-12-31")
            if not candidates:
                raise ValueError("CSV needs complete 5-minute days and at least 119 prior bars")
        elif self.demo:
            candidates = prepare_days(demo_rows(), "sh000000", "模拟数据", today="9999-12-31")
        else:
            pool = [(s, s) for s in self.symbols] if self.symbols else universe(self.cache)
            choices = self.rng.sample(pool, min(8, len(pool)))
            candidates = []
            errors = []
            for symbol, name in choices:
                try:
                    candidates = prepare_days(minutes(symbol, self.cache), symbol, name)
                    if candidates:
                        break
                except (ValueError, OSError, TimeoutError) as exc:
                    errors.append(f"{symbol}: {exc}")
            if not candidates:
                raise RuntimeError("no complete historical day: " + "; ".join(errors))
        different = [d for d in candidates if (d.symbol, d.date) != self.last]
        day = self.rng.choice(different or candidates)
        self.last = day.symbol, day.date
        return day


def demo_rows():
    rng = random.Random(24)
    rows, price = [], 1000
    date = dt.date(2026, 10, 12)
    for day in range(8):
        while date.weekday() >= 5:
            date += dt.timedelta(days=1)
        for clock in SLOTS:
            old = price
            price = max(200, price + rng.randrange(-8, 9))
            rows.append(dict(day=f"{date} {clock}:00", open=str(Decimal(old) / 100),
                             high=str(Decimal(max(old, price) + 3) / 100),
                             low=str(Decimal(min(old, price) - 3) / 100), close=str(Decimal(price) / 100)))
        date += dt.timedelta(days=1)
    return rows
