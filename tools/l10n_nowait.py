"""python3 tools/l10n_nowait.py __1.NOW [...]: the messages villagers show in boxes that do not wait (sellers and askers: walk.c talk_start, record byte 0x30 set or an entry's
byte 4 = 0xff), from every scene's villager records in the state files given (the shipped and the new game's __1.NOW), with each
message's 0x7f chain. tests/l10n_check.c holds the result: a translation of these must fit one box of three rows."""
import sys
from pathlib import Path

SCENE, VILL = 1750, 50
mes = Path('original_bin/MES_TXT.LCC').read_bytes()


def last_byte(n):
    r = mes[6 + (n - 1) * 0x80: 6 + (n - 1) * 0x80 + 0x78]
    e = r.find(b'#')
    t = r[:e if e >= 0 else len(r)]
    return t[-1] if t else 0


ids = set()
for f in sys.argv[1:]:
    now = Path(f).read_bytes()
    for sc in range(len(now) // SCENE):
        b = now[sc * SCENE: sc * SCENE + 1000]
        for v in range(20):
            r = b[v * VILL:(v + 1) * VILL]
            if not any(r):
                continue
            for k in range(0xC, 0x2F, 7):
                msg = r[k + 5] | r[k + 6] << 8
                if not msg or msg > 769:
                    continue
                if r[0x30] != 0 or r[k + 4] == 0xFF:
                    m = msg
                    while True:
                        ids.add(m)
                        if last_byte(m) != 0x7F or m >= 769:
                            break
                        m += 1
print(len(ids), sorted(ids))
