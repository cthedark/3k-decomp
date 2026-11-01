#!/usr/bin/env python3
"""Build the translated modes' bitmap fonts in localization/fonts/ (localization/README.md, "Fonts").

    python3 tools/l10n_fonts.py HANGUL.ttf NARROW.bdf [--out localization/fonts]

HANGUL.ttf is Neo Dunggeunmo (github.com/neodgm/neodgm, release v1.601, neodgm.ttf; SIL Open Font License 1.1), rasterized at its
16-pixel size: Hangul syllables, compatibility jamo and a few punctuation marks, 16 x 14 (rows 0 .. 13 of the em). Under the OFL a
converted copy may not use the font's Reserved Font Names, so the result is named tk_hangul16. NARROW.bdf is the X11 misc-fixed
6x13 font (public domain): ASCII and a few punctuation marks, 6 x 13 in a 6 x 14 cell, named tk_narrow6.

Output format (.tkf): "TKF1", u16 width, u16 height, u32 count (little-endian); `count` u32 code points in ascending order; then
`count` glyphs of `height` rows, ceil(width / 8) bytes a row, the most significant bit the leftmost pixel. Needs Pillow for the TTF.
"""
import argparse
import struct
import sys
from pathlib import Path

# (marks whose Neo Dunggeunmo glyph reaches below row 13, such as arrows and stars, are left out)
PUNCT = [0x00B7, 0x2010, 0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2026, 0x25CB, 0x25CF, 0x3001, 0x3002, 0x300C, 0x300D,
         0x300E, 0x300F]


def write_tkf(path, w, h, glyphs):
    cps = sorted(glyphs)
    bw = (w + 7) // 8
    out = bytearray(b'TKF1' + struct.pack('<HHI', w, h, len(cps)))
    for cp in cps:
        out += struct.pack('<I', cp)
    for cp in cps:
        g = glyphs[cp]
        assert len(g) == h * bw, (cp, len(g))
        out += g
    path.write_bytes(bytes(out))
    return len(cps)


def hangul(ttf):
    from PIL import Image, ImageDraw, ImageFont
    f = ImageFont.truetype(str(ttf), 16)
    cps = list(range(0xAC00, 0xD7A4)) + list(range(0x3131, 0x3164)) + PUNCT   # syllables, modern jamo
    glyphs = {}
    for cp in cps:
        im = Image.new('1', (16, 16), 0)
        ImageDraw.Draw(im).text((0, 0), chr(cp), font=f, fill=1)
        px = im.load()
        if any(px[x, 14] or px[x, 15] for x in range(16)):
            print(f'U+{cp:04X}: ink below row 13, cut', file=sys.stderr)
        rows = bytearray()
        for y in range(14):
            v = 0
            for x in range(16):
                v = v << 1 | (1 if px[x, y] else 0)
            rows += struct.pack('>H', v)
        if any(rows) or cp in (0x3000,):
            glyphs[cp] = bytes(rows)
    return glyphs


def narrow(bdf):
    glyphs, cur, bits, bbx = {}, None, None, None
    for line in Path(bdf).read_text(encoding='latin-1').splitlines():
        p = line.split()
        if not p:
            continue
        if p[0] == 'ENCODING':
            cur = int(p[1])
        elif p[0] == 'BBX':
            bbx = [int(v) for v in p[1:5]]
        elif p[0] == 'BITMAP':
            bits = []
        elif p[0] == 'ENDCHAR':
            if cur is not None and (0x20 <= cur < 0x7F or cur in PUNCT):
                w, h, xo, yo = bbx
                # the font's cell is 6 x 13 with the baseline 2 rows above the bottom (FONT_DESCENT 2); a glyph's bitmap is h rows
                # from row 13 - 2 - yo - h
                cell = [0] * 14
                top = 13 - 2 - yo - h
                for r, v in enumerate(bits):
                    if 0 <= top + r < 13:
                        cell[top + r] = (v >> xo if xo >= 0 else v << -xo) & 0xFC
                glyphs[cur] = bytes(cell)
            bits = None
        elif bits is not None:
            bits.append(int(p[0], 16))
    return glyphs


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument('ttf')
    ap.add_argument('bdf')
    ap.add_argument('--out', default='localization/fonts')
    a = ap.parse_args(argv)
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    print('tk_hangul16:', write_tkf(out / 'tk_hangul16.tkf', 16, 14, hangul(a.ttf)), 'glyphs')
    print('tk_narrow6:', write_tkf(out / 'tk_narrow6.tkf', 6, 14, narrow(a.bdf)), 'glyphs')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
