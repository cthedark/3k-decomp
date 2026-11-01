"""Render the text drawn as pictures, 2x, white on black, one sheet per source, to transcribe them into
localization/source/pictures.tsv: the intro's subtitles (STORYn_T.LCZ), the ending's (ENDING1T, ENDING4T), the credits (CAST2.LCZ,
in parts of 400 rows) and the battle's shouts (RPG.LCP 0x198d8).

    python3 tools/l10n_textpics.py

Reads original_bin/; writes only under work/l10n/textpics/ (derived from game data)."""
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import lcz_decode as L  # noqa: E402

D = ROOT / 'original_bin'
OUT = ROOT / 'work' / 'l10n' / 'textpics'


def png(path, w, h, grey):
    raw = b''.join(b'\0' + grey[y * w:(y + 1) * w] for y in range(h))
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 0, 0, 0, 0)) +
                     chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def bits(b, w, h, stride):
    return [[(b[r * stride + x // 8] >> (7 - x % 8)) & 1 for x in range(w)] for r in range(h)]


def bytes8(b, w, h):
    return [[1 if b[r * w + x] else 0 for x in range(w)] for r in range(h)]


def save(name, pics, scale=2, label_gap=6):
    W = max(len(p[0]) for p in pics) * scale + 8
    H = sum(len(p) * scale + label_gap for p in pics)
    img = bytearray(W * H)
    y = 0
    for p in pics:
        for r, row in enumerate(p):
            for x, v in enumerate(row):
                if v:
                    for dy in range(scale):
                        o = (y + r * scale + dy) * W + 4 + x * scale
                        img[o:o + scale] = b'\xff' * scale
        y += len(p) * scale + label_gap
        img[(y - 3) * W:(y - 2) * W] = b'\x40' * W   # a grey rule between lines
    png(OUT / f'{name}.png', W, H, bytes(img))


def unpack(inp):
    end, si, out = inp[1] | inp[2] << 8, 3, bytearray()
    end = min(end, len(inp))
    while si < end:
        if inp[si] != inp[0]:
            out.append(inp[si]); si += 1; continue
        out += bytes([inp[si + 2]]) * inp[si + 1]; si += 3
    return bytes(out)


def chunks(name):
    return list(L.decode_chunks((D / name).read_bytes()))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for n in range(7):
        c = L.decode_chunks((D / f'STORY{n}_T.LCZ').read_bytes())[0][0][3]
        save(f'story{n}', [bits(c[600 * k:], 320, 15, 40) for k in range(len(c) // 600)])
    for name, w in (('ENDING1T.LCZ', 236), ('ENDING4T.LCZ', 220)):
        u = unpack((D / name).read_bytes())
        save(name[:-4].lower(), [bytes8(u[w * 15 * k:], w, 15) for k in range(len(u) // (w * 15))])
    u = unpack((D / 'CAST2.LCZ').read_bytes())
    rows = len(u) // 15
    for part in range(0, rows, 400):
        save(f'cast2_{part // 400}', [bits(u[15 * part:], 120, min(400, rows - part), 15)], scale=2)
    rpg = (D / 'RPG.LCP').read_bytes()
    save('shouts', [bits(rpg[0x198D8 + 0xFF * k:], 136, 14, 17) for k in range(12)])


if __name__ == '__main__':
    main()
