"""Contact sheets (3 columns of 320 x 200) in work/l10n/shots/, to find text drawn into pictures (localization/README.md):
kindNN.png, six screens of each pass kind from the port.bin files tools/check_battle.sh keeps (run it first), and spic.png, the
signs of SPIC.LCZ.

    python3 tools/l10n_shots.py
    python3 tools/l10n_shots.py --raw NAME RAW [RAW ...]    # screen dumps (--step-dump files) as NAME.png, up to 12

Reads original_bin/ and work/check_battle/; writes only under work/l10n/shots/ (derived from game data)."""
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
STATE = 4 + 1000 + 0x124
FRAME = 64000 + 768 + 1 + STATE
OUT = ROOT / 'work' / 'l10n' / 'shots'


def png(path, w, h, rgb):
    raw = b''.join(b'\0' + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                     chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def rgb_of(scr, pal):
    lut = [bytes((pal[3 * i] * 255 // 63, pal[3 * i + 1] * 255 // 63, pal[3 * i + 2] * 255 // 63)) for i in range(256)]
    return b''.join(lut[p] for p in scr)


def sheet(name, frames, cols=3):
    rows = (len(frames) + cols - 1) // cols
    W, H = 320 * cols + 4 * (cols - 1), 200 * rows + 4 * (rows - 1)
    img = bytearray(b'\xff\x00\xff' * W * H)
    for k, (scr, pal) in enumerate(frames):
        x0, y0 = (k % cols) * 324, (k // cols) * 204
        rgb = rgb_of(scr, pal)
        for y in range(200):
            o = ((y0 + y) * W + x0) * 3
            img[o:o + 960] = rgb[y * 960:(y + 1) * 960]
    png(OUT / f'{name}.png', W, H, bytes(img))


def spic():
    """The 13 signs of SPIC.LCZ (100 x 70, not packed, 7000 bytes each), six to a screen."""
    s = (ROOT / 'original_bin' / 'SPIC.LCZ').read_bytes()
    pal = (ROOT / 'original_bin' / 'RPG.LCP').read_bytes()[:768]
    out = []
    for page in range(0, len(s) // 7000, 6):
        scr = bytearray(64000)
        for j, k in enumerate(range(page, min(page + 6, len(s) // 7000))):
            pic = s[k * 7000:(k + 1) * 7000]
            x0, y0 = (j % 3) * 106, (j // 3) * 100
            for r in range(70):
                scr[(y0 + r) * 320 + x0:(y0 + r) * 320 + x0 + 100] = pic[r * 100:(r + 1) * 100]
        out.append((bytes(scr), pal))
    sheet('spic', out)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    if len(sys.argv) > 2 and sys.argv[1] == '--raw':   # --raw NAME RAW...: screen dumps (64,000 pixels + palette) as one sheet
        frames = [(b[:64000], b[64000:64768]) for b in (Path(p).read_bytes() for p in sys.argv[3:15])]
        sheet(sys.argv[2], frames)
        return
    spic()
    per_kind = {}
    for d in sorted((ROOT / 'work' / 'check_battle').glob('k_*')):
        p = d / 'port.bin'
        if not p.exists() or p.stat().st_size % FRAME:
            continue
        n = p.stat().st_size // FRAME
        with p.open('rb') as f:
            seen = {}
            for i in range(n):
                f.seek(i * FRAME + 64768)
                k = f.read(1)[0]
                seen.setdefault(k, []).append(i)
            for k, idx in seen.items():
                lst = per_kind.setdefault(k, [])
                if len(lst) >= 6:
                    continue
                i = idx[len(idx) * 2 // 3]
                f.seek(i * FRAME)
                b = f.read(64768)
                lst.append((b[:64000], b[64000:], d.name, i))
    for k, lst in sorted(per_kind.items()):
        sheet(f'kind{k:02d}', [(s, p) for s, p, _, _ in lst])
        print(k, [(nm, i) for _, _, nm, i in lst])


if __name__ == '__main__':
    main()
