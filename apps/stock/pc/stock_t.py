#!/usr/bin/env python3
"""PC-hosted A-share replay game. Device is a chart/input terminal, never a trading account."""
import argparse
import concurrent.futures
import json
import secrets
import sys
import time
from pathlib import Path
from stock_link import GameLink
from stock_t_core import StockT
from stock_t_data import DataSource


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port", help="USB serial device (auto-detect a single ESP32 if omitted)")
    p.add_argument("--symbols", nargs="+", help="optional pool, e.g. sh600000 sz000001")
    p.add_argument("--csv", help="offline CSV with day/open/high/low/close columns")
    p.add_argument("--demo", action="store_true", help="explicit synthetic demonstration, not real market data")
    p.add_argument("--interval", type=float, default=1.0, help="seconds per 5-minute candle, default 1")
    p.add_argument("--seed", type=int, help="repeatable data selection")
    p.add_argument("--dry-run", action="store_true", help="fetch/validate a day and print metadata, without USB")
    p.add_argument("--keyboard",action="store_true",help="open a focused USB keyboard window on the same serial connection")
    p.add_argument("--key-config",type=Path,help="PC keyboard bindings JSON")
    args = p.parse_args(argv)
    if args.interval <= 0:
        p.error("--interval must be positive")
    source = DataSource(args.symbols, args.csv, args.demo, args.seed)
    print("准备历史行情...", file=sys.stderr)
    day = source.pick()
    print(f"{day.symbol} {day.name} {day.date}: 60 根背景 + {len(day.bars) - day.first} 根日内K线", file=sys.stderr)
    if args.dry_run:
        print(json.dumps(dict(code=day.symbol, date=day.date, bars=len(day.bars), first=day.first,
                              first_clock=day.clocks[day.first], last_clock=day.clocks[-1], synthetic=args.demo), ensure_ascii=False))
        return 0
    link = GameLink(args.port)
    session = secrets.randbelow(2147483646) + 1
    executor = concurrent.futures.ThreadPoolExecutor(max_workers=1)
    future = None
    down_ms = None
    keyboard = None
    try:
        link.upload(day, session)
        engine = StockT(day, time.monotonic(), args.interval, device_confirm=True)
        link.request(engine.frame(session, time.monotonic()))
        print("已就绪：设备短按打开菜单，选择做T并按住500ms进入。", file=sys.stderr)
        if args.keyboard:
            sys.path.insert(0,str(Path(__file__).resolve().parents[2]/"key_report/pc"))
            from controller import KeyController
            keyboard=KeyController(link,args.key_config,poll=False)
        last_frame = None
        last_sent = 0.0
        was_selected = link.selected
        while True:
            if keyboard:keyboard.pump()
            for event in link.poll():
                if event.get("id") != session:
                    continue
                now = time.monotonic()
                action = event.get("action")
                if action == "menu":
                    link.selected = False
                    engine.down_at = None
                    engine.consumed = False
                    continue
                if action == "launch":
                    link.selected = True
                    engine.next_bar = now + args.interval
                    continue
                if action == "cancel":
                    engine.cancel(now)
                    down_ms = None
                    continue
                if not link.selected:
                    continue
                if action == "hold":
                    engine.confirm(now)
                    continue
                pressed = event.get("pressed")
                if not isinstance(pressed, bool):
                    continue
                if pressed:
                    down_ms = event.get("ms")
                duration = None
                if not pressed and isinstance(down_ms, int) and isinstance(event.get("ms"), int):
                    duration = (event["ms"] - down_ms) / 1000
                engine.button(pressed, now, event.get("cursor"), duration)
                if not pressed:
                    down_ms = None
            now = time.monotonic()
            if link.selected and not was_selected:
                engine.next_bar = now + args.interval
            if not link.selected:
                engine.down_at = None
                engine.consumed = False
            was_selected = link.selected
            if link.selected:
                engine.update(now)
            if engine.request == "exit":
                break
            if engine.request == "next" and future is None:
                future = executor.submit(source.pick)
                engine.request = None
            if future and future.done():
                try:
                    day = future.result()
                except (OSError, ValueError, RuntimeError, TimeoutError) as exc:
                    print(f"准备下一局失败：{exc}；长按重试，短按退出。", file=sys.stderr)
                    engine.phase = "result"
                    engine.down_at = None
                    engine.consumed = False
                    future = None
                    continue
                print(f"下一局 {day.symbol} {day.date}", file=sys.stderr)
                session = secrets.randbelow(2147483646) + 1
                link.upload(day, session)
                engine = StockT(day, time.monotonic(), args.interval, device_confirm=True)
                last_frame = None
                future = None
                down_ms = None
            frame = engine.frame(session, now)
            encoded = json.dumps(frame, sort_keys=True)
            if encoded != last_frame and now - last_sent >= 0.08:
                link.request(frame)
                last_frame, last_sent = encoded, now
            elif now - last_sent >= 1:
                link.request(dict(op="heartbeat", id=session))
                last_sent = now
    except KeyboardInterrupt:
        pass
    finally:
        if keyboard:keyboard.close()
        try:
            link.request(dict(op="close", id=session))
        except (OSError, ValueError, RuntimeError, TimeoutError):
            pass
        link.close()
        executor.shutdown(wait=False, cancel_futures=True)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, TimeoutError) as exc:
        print(f"stock-t: {exc}", file=sys.stderr)
        raise SystemExit(1)
