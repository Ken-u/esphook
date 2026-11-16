"""Stock chart protocol adapter; the generic USB SDK knows no trading rules."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "framework" / "pc"))
from esphook_sdk import UsbLink

class GameLink(UsbLink):
    prefix = "game"
    ack_type = "game_ack"

    def upload(self, day, session):
        self.request(dict(op="begin", id=session, label=day.name, code=day.symbol[-6:],
                          date=day.date, count=len(day.bars), first=day.first))
        for offset in range(0, len(day.bars), 12):
            self.request(dict(op="bars", id=session, offset=offset, bars=day.bars[offset:offset + 12]))

