#!/usr/bin/env python3
"""Mutation check for the ENDING.SL port: each mutation below is applied to a copy of src/ending.c, tests/ending_dump.c is built
on it, and tools/ending_check.py must report a difference. A mutation it does not notice is printed as MISSED.

Usage: work/venv/bin/python tools/mutate_ending.py     (after tools/ending_check.py has cached the original's run)
"""
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WORK = ROOT / 'work/mut_ending'
CACHE = ROOT / 'work/ending_check/s2.rec'
M = []


def m(name, old, new):
    M.append((name, old, new))


# the first scene (step 2)
m('s1: clouds still', '    turn_rows(e->b22);\n    put_hdr', '    put_hdr')
m('s1: clouds turn right', '        memmove(p, p + 1, w - 1);\n        p[w - 1] = a;', '        memmove(p + 1, p, w - 1);\n        p[0] = p[w - 1];\n        (void)a;')
m('s1: face a pixel left', '    put_hdr(e->b36, sizeof e->b36, 0xBCF,', '    put_hdr(e->b36, sizeof e->b36, 0xBCE,')
m('s1: face only in one picture', '    put_hdr(e->b46, sizeof e->b46, 0xBCF, 0x140, e->be + e->face);\n', '')
m('s1: pictures every fourth pass', '    if (++e->swap > 4) {', '    if (++e->swap > 3) {')
m('s1: swap counter from 0', '    e->swap = 0xA;', '    e->swap = 0;')
m('s1: pictures swapped', '    const uint8_t *b = e->which ? e->b36 : e->b46;', '    const uint8_t *b = e->which ? e->b46 : e->b36;')
m('s1: body opaque', '        if (b[i] && o < sizeof e->file)', '        if (o < sizeof e->file)')
m('s1: rises after 50', '    if (!e->risen && ++e->wait > 0x32) {', '    if (!e->risen && ++e->wait > 0x31) {')
m('s1: rises a row too far', '        if (e->count > 0xB7C0) {', '        if (e->count > 0xB900) {')
m('s1: face every fifth pass', '        if (++e->talk > 5) {', '        if (++e->talk > 4) {')
m('s1: talk counter from 1', '    e->talk = 0;      /* [bp-0x3e]', '    e->talk = 1;      /* [bp-0x3e]')
m('s1: three rounds a line', '                if (++e->rounds > 1) {', '                if (++e->rounds > 2) {')
m('s1: subtitles opaque', '            if (v && o < cap)\n                dst[o] = v;', '            if (o < cap)\n                dst[o] = v;')
m('s1: subtitles a row lower', 'put_clear(e->file, sizeof e->file, 0xE62A,', 'put_clear(e->file, sizeof e->file, 0xE76A,')
m('s1: subtitle before she has risen', '    if (e->risen) {\n        if (++e->talk', '    if (1) {\n        if (++e->talk')
m('s1: last frame shown', '                    if (e->sub == 0x3750)\n                        return ENDING_SCENE1_END;', '                    if (e->sub == 0x3750) {\n                        memcpy(scr->pix, e->file, 0xFA00);\n                        return ENDING_SCENE1_END;\n                    }')
m('s1: palette from its second half', '    if (assets_read_at("ANI.LCP", 0, pal, sizeof pal) != 0)', '    if (assets_read_at("ANI.LCP", 0x300, pal, sizeof pal) != 0)')

# the second scene (step 3)
m('s2: background still', '    turn_rows2(e->b22);\n    if (!e->which)', '    if (!e->which)')
m('s2: background turns one pixel', '    turn_rows2(e->b22);\n    if (!e->which)', '    turn_rows(e->b22);\n    if (!e->which)')
m('s2: ENDING2 pictures from 0x46d', '    unpack(e->file + 0x46C, n - 0x46C, e->b36, BUF);', '    unpack(e->file + 0x46D, n - 0x46D, e->b36, BUF);')
m('s2: screen not cleared', '    memset(scr->pix, 0, sizeof scr->pix);\n    e->face = 0;', '    e->face = 0;')
m('s2: first place a row lower', '    e->swap = 0x286C;', '    e->swap = 0x29AC;')
m('s2: grab 141 wide', '            size_t s = 4 + (size_t)r * 0x8E + c,', '            size_t s = 4 + (size_t)r * 0x8D + c,')
m('s2: grab from the header', '            size_t s = 4 + (size_t)r * 0x8E + c,', '            size_t s = (size_t)r * 0x8E + c,')
m('s2: picture opaque', '            if (v && d < BUF)\n                g[d] = v;\n        }\n    for (unsigned r = 0; r < e->at; r++)   /* 0x122c8 */',
  '            if (d < BUF)\n                g[d] = v;\n        }\n    for (unsigned r = 0; r < e->at; r++)   /* 0x122c8 */')
m('s2: no picture', '                g[d] = v;\n        }\n    for (unsigned r = 0; r < e->at; r++)   /* 0x122c8 */', '                (void)v;\n        }\n    for (unsigned r = 0; r < e->at; r++)   /* 0x122c8 */')
m('s2: 99 passes a picture', '    if (++e->wait <= 0x63)', '    if (++e->wait <= 0x62)')
m('s2: second picture at the third place', '        e->face = 0x3088, e->swap = 0x2A;', '        e->face = 0x3088, e->swap = 0x76CB;')
m('s2: third picture at the fourth place', '        e->face = 0x4800, e->swap = 0x76CB;', '        e->face = 0x4800, e->swap = 0x1732;')
m('s2: fifth picture at the start', '        e->face = 0x8FD4, e->swap = 0x8E00;', '        e->face = 0x8FD4, e->swap = 0x286C;')
m('s2: four pictures', '    if (e->face == 0x8FD4)\n        return ENDING_SCENE2_END;', '    if (e->face == 0x6C50)\n        return ENDING_SCENE2_END;')
m('s2: size not read again', '    e->which = 0;\n    return ENDING_SCENE2;\n}', '    e->which = 1;\n    return ENDING_SCENE2;\n}')
m('s2: count of the next picture kept', '        e->which = 1;\n        e->wait = 0;', '        e->which = 1;')


def main():
    if not CACHE.exists():
        print('run tools/ending_check.py --n 3000 --cache work/ending_check/s2.rec first')
        return 2
    missed = bad = 0
    WORK.mkdir(parents=True, exist_ok=True)
    for name, old, new in M:
        text = (ROOT / 'src/ending.c').read_text()
        if text.count(old) != 1:
            print(f'BAD PATTERN ({text.count(old)} matches): {name}')
            bad += 1
            continue
        src = WORK / 'src'
        if src.exists():
            shutil.rmtree(src)
        shutil.copytree(ROOT / 'src', src)
        (src / 'ending.c').write_text(text.replace(old, new))
        exe = WORK / 'ending_dump'
        r = subprocess.run(['cc', '-std=c99', '-O1', '-w', '-I', str(src)] +
                           [str(src / f) for f in ('ending.c', 'assets.c', 'lcz.c', 'screen.c')] +
                           [str(ROOT / 'tests/ending_dump.c'), '-o', str(exe)], capture_output=True, text=True)
        if r.returncode != 0:
            print(f'DOES NOT BUILD: {name}: {r.stderr.strip()[:200]}')
            bad += 1
            continue
        r = subprocess.run([sys.executable, str(ROOT / 'tools/ending_check.py'), '--n', '3000', '--cache', str(CACHE),
                            '--port', str(exe), '--keep', str(WORK / 'keep')], capture_output=True, text=True, cwd=ROOT)
        if r.returncode == 0:
            print('MISSED: ' + name)
            missed += 1
        else:
            print('caught: ' + name)
    print(f'{len(M) - missed - bad} of {len(M)} caught, {missed} missed, {bad} bad')
    return 1 if missed or bad else 0


if __name__ == '__main__':
    sys.exit(main())
