#!/usr/bin/env python3
"""Extract every on-screen string the port draws from the game's data (and the few the programs hold) into translation source
tables under localization/source/ (docs/l10n.md).

Usage: python3 tools/l10n_extract.py [--data original_bin] [--out localization/source]

Writes one tab-separated file per table, UTF-8, columns: id, bytes (the room the original gives the text), zh (the text), big5 (the
original bytes in hex: what the port matches a drawn string against in the translated modes). Bytes that
are not Big5 text are written as {xx} (hex); 0x7f at a message's end means "the next message follows in the same talk". The '@' of
the messages is drawn as a small circle (RPG.LCP's 8 x 15 font), the original's sentence stop. Standard library only.
"""
import argparse
import sys
from pathlib import Path

MSG_LEN = 0x78        # a message's text: 0x78 bytes of a 0x80-byte record, from offset 6, up to '#'
MSG_REC = 0x80


def text(b):
    """Big5 bytes to a readable string: ASCII as is, Big5 pairs decoded (cp950), anything else as {xx}."""
    out, i = [], 0
    while i < len(b):
        c = b[i]
        if 0x20 <= c < 0x7F:
            out.append(chr(c))
            i += 1
            continue
        if c >= 0x81 and i + 1 < len(b):
            try:
                out.append(b[i:i + 2].decode('cp950'))
                i += 2
                continue
            except UnicodeDecodeError:
                pass
        out.append('{%02x}' % c)
        i += 1
    return ''.join(out)


def field(b):
    """A fixed-size name field: its bytes up to the first 0 byte, trailing spaces dropped."""
    z = b.find(b'\0')
    return (b[:z] if z >= 0 else b).rstrip(b' ')


def write(out, name, rows, note):
    p = out / (name + '.tsv')
    with p.open('w', encoding='utf-8', newline='\n') as f:
        f.write('# ' + note + '\n')
        f.write('id\tbytes\tzh\tbig5\n')
        for i, n, b in rows:
            f.write(f'{i}\t{n}\t{text(b)}\t{b.hex()}\n')
    return len(rows)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument('--data', default='original_bin')
    ap.add_argument('--out', default='localization/source')
    a = ap.parse_args(argv)
    data, out = Path(a.data), Path(a.out)

    def read(name):
        for p in data.iterdir():
            if p.name.lower() == name.lower():
                return p.read_bytes()
        sys.exit(f'{name} not found in {data}')

    out.mkdir(parents=True, exist_ok=True)
    mes = read('MES_TXT.LCC')
    counts = {}

    # messages: record n at (n - 1) * 0x80 + 6; some records are tables of fixed fields (below), listed here too
    rows = []
    for n in range(1, (len(mes) - 6) // MSG_REC + 1):
        r = mes[6 + (n - 1) * MSG_REC: 6 + (n - 1) * MSG_REC + MSG_LEN]
        e = r.find(b'#')
        rows.append((f'msg.{n:04d}', MSG_LEN, r[:e] if e >= 0 else r))
    counts['messages'] = write(out, 'messages', rows, 'MES_TXT.LCC messages (message boxes, talks, shops): 0x78 bytes each, up '
                               'to "#". Records 7, 8, 213 and 748-751 are also read as fixed fields (labels, names, card game).')

    # the fixed fields inside MES_TXT.LCC
    rows = [('shop.prices', 10, mes[0x306:0x310])]
    rows += [(f'label.{k:02d}', 4, mes[0x386 + 4 * k:0x38A + 4 * k]) for k in range(14)]
    rows += [(f'member.{k}', 6, field(mes[0x6A06 + 6 * k:0x6A0C + 6 * k])) for k in range(50 // 6)]
    rows += [(f'card.{k}', 0x14 if k < 2 else 0x12, mes[0x17586 + 0x80 * k:0x17586 + 0x80 * k + (0x14 if k < 2 else 0x12)])
             for k in range(4)]
    counts['fields'] = write(out, 'fields', rows, 'Fixed fields in MES_TXT.LCC: the shop strip (0x306), the button labels (0x386, '
                             '4 bytes = 2 characters = 32 pixels), the members\' names (0x6a06), the card game\'s lines (0x17586).')

    obj = read('OBJECT.LCC')
    rows = [(f'item.{n:03d}', 12, field(obj[(n - 1) * 30:(n - 1) * 30 + 12])) for n in range(1, len(obj) // 30 + 1)]
    counts['items'] = write(out, 'items', rows, 'OBJECT.LCC item names: 12 bytes (96 pixels) of each 30-byte record (item n at '
                            '(n - 1) x 30).')

    mg = read('MAGICT.LCC')
    rows = [(f'spell.{m}.{i}', 12, field(mg[200 * m + 20 * i:200 * m + 20 * i + 12]))
            for m in range(len(mg) // 200) for i in range(10)]
    counts['spells'] = write(out, 'spells', rows, 'MAGICT.LCC spell names: 12 bytes (96 pixels) of each 20-byte record, member m\'s '
                             'spell i at 200 m + 20 i.')

    # strings the programs hold (WALK.SL's code; the port has them as constants in src/walk.c)
    fixed = [('code.yes', 2, bytes([0xAC, 0x4F]), 'walk.c yes_no: the yes / no choice'),
             ('code.no', 2, bytes([0xA7, 0x5F]), 'walk.c yes_no'),
             ('code.coins', 6, bytes([0xAA, 0x54, 0xAA, 0xF7, 0xB9, 0xF4]), 'walk.c F3: after the number of coins taken'),
             ('code.price', 5, bytes([0xAD, 0xEC, 0xBB, 0xF9, ord(':')]), 'walk.c walk_sell_text: before the price'),
             ('code.half', 5, bytes([0xA5, 0x62, 0xBB, 0xF9, ord(':')]), 'walk.c walk_sell_text: before half the price'),
             ('code.sell', 7, bytes([0xC4, 0x40, 0xB7, 0x4E, 0xB6, 0xDC, ord('?')]), 'walk.c use_sell: the question')]
    rows = [(i, n, b) for i, n, b, _ in fixed]
    counts['code'] = write(out, 'code', rows, 'Strings in the programs\' code: ' + '; '.join(f'{i}: {w}' for i, _, _, w in fixed))

    for k, v in counts.items():
        print(f'{k}: {v} rows')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
