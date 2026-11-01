#!/usr/bin/env python3
"""Mutation check for the ENDING.SL port: each mutation below is applied to a copy of src/ending.c, tests/ending_dump.c is built
on it, and tools/ending_check.py must report a difference. A mutation it does not notice is printed as MISSED.

Usage: work/venv/bin/python tools/mutate_ending.py     (after tools/ending_check.py has cached the original's run)
       ONLY=s6: runs only the mutations whose name contains the text.
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WORK = ROOT / 'work/mut_ending'
CACHE = ROOT / 'work/ending_check/s6.rec'
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

# the third scene (step 4)
m('s3: sky from 0x3e6c', '    unpack(e->file + 0x3E6B, n - 0x3E6B, e->b36, BUF);', '    unpack(e->file + 0x3E6C, n - 0x3E6C, e->b36, BUF);')
m('s3: she starts a row lower', '    e->swap = 0xA46;', '    e->swap = 0xB86;')
m('s3: sky drifts every pass', '    if (e->talk) {\n        drift(e, e->b36);', '    if (1) {\n        drift(e, e->b36);')
m('s3: sky drifts on the other passes', '        e->talk = 0;\n    } else {\n        e->talk = 1;', '        e->talk = 1;\n    } else {\n        e->talk = 0;')
m('s3: sparks still', '    drift(e, e->b46);\n', '')
m('s3: drift without the wrap', '    memcpy(pic + 0xF8C4, e->file, 0x140);', '    (void)0;')
m('s3: drift not up', '    memmove(pic + 4, pic + 4 + 0x140, 0xFA00 - 0x140);', '    (void)0;')
m('s3: one picture of her', '    e->face = e->face ? 0 : 0x7144;', '    (void)0;')
m('s3: she is opaque', '            if (v && o < BUF)\n                e->file[o] = v;\n        }\n    e->face', '            if (o < BUF)\n                e->file[o] = v;\n        }\n    e->face')
m('s3: sparks opaque', '        if (e->b46[4 + i])\n            e->file[i] = e->b46[4 + i];', '        e->file[i] = e->b46[4 + i];')
m('s3: sparks under her', '    for (unsigned i = 0; i < 0xFA00; i++)   /* 0x12146 */\n        if (e->b46[4 + i])\n            e->file[i] = e->b46[4 + i];\n', '')
# Dropped: her pictures keeping their height (she goes a row down as they lose a row, so her bottom stays at row 200; the rows
# below would fall past the frame's 200 rows, which are not shown).
m('s3: ends a row later', '    if (e->swap > 0x88B8) {', '    if (e->swap > 0x89F8) {')
m('s3: last frame shown', '        memset(scr->pix, 0, sizeof scr->pix);\n        return ENDING_SCENE3_END;', '        memcpy(scr->pix, e->file, 0xFA00);\n        return ENDING_SCENE3_END;')
m('s3: right every sixth pass', '    if (++e->which > 6) {', '    if (++e->which > 5) {')

# the fourth scene (step 5)
m('s4: first picture from row 0', '                scr->pix[0x140 + i] = pic[i];', '                scr->pix[i] = pic[i];')
m('s4: blocks from the file start', '        at += m;', '        (void)0;')
m('s4: frame not taken from the screen', '    memcpy(e->file, scr->pix, 0xFA00);\n    e->which = 0;\n    e->wait = 0;', '    e->which = 0;\n    e->wait = 0;')
m('s4: no shake', '        memcpy(scr->pix + 0x140, e->file, 0xFA00 - 0x140);', '        memcpy(scr->pix, e->file, 0xFA00);')
m('s4: shake starts high', '    e->which = 0;\n    e->wait = 0;\n    return ENDING_SCENE4_SHAKE;', '    e->which = 1;\n    e->wait = 0;\n    return ENDING_SCENE4_SHAKE;')
m('s4: 79 shakes', '    return ++e->wait < 0x50 ?', '    return ++e->wait < 0x4F ?')
m('s4: strobe frame from row 21', '    memcpy(e->file, scr->pix + 0x1900, 0xCE40);', '    memcpy(e->file, scr->pix + 0x1A40, 0xCE40);')
m('s4: strobe starts with the next', '    e->rounds = 1;\n    e->which = 1;', '    e->rounds = 1;\n    e->which = 0;')
m('s4: strobe at row 0', '            memcpy(scr->pix + 0x1900, e->b22, 0xCE40);', '            memcpy(scr->pix, e->b22, 0xCE40);')
m('s4: 51 passes a picture', '    if (++e->face < 0x32) {', '    if (++e->face < 0x33) {')
m('s4: next not kept', '    memcpy(e->file, e->b22, 0xCE40);\n    if (e->rounds', '    if (e->rounds')
m('s4: blocks one late', '        unpack(keep[e->rounds - 1], BUF, e->b22, BUF);', '        unpack(keep[e->rounds > 1 ? e->rounds - 2 : 0], BUF, e->b22, BUF);')
m('s4: five pictures', '    if (e->rounds == 6)\n        return ENDING_SCENE4_END;', '    if (e->rounds == 5)\n        return ENDING_SCENE4_END;')

# the fifth scene (step 6)
m('s5: read from 0x3eaf9', '    memcpy(e->file, all + 0x3EAF8, m);', '    memcpy(e->file, all + 0x3EAF9, m);')
m('s5: eyes from 0x5bb2', '        unpack(e->file + 0x5BB1, m - 0x5BB1, e->b36, BUF);', '        unpack(e->file + 0x5BB2, m - 0x5BB2, e->b36, BUF);')
m('s5: grab a row lower', '        memcpy(g + 4 + r * 0xDC, scr->pix + 0xE632 + r * 320, 0xDC);', '        memcpy(g + 4 + r * 0xDC, scr->pix + 0xE772 + r * 320, 0xDC);')
m('s5: subtitle opaque', '        if (e->b46[k * 0xCE4 + i])\n            e->be[4 + i]', '        if (1)\n            e->be[4 + i]')
m('s5: subtitle not over the grab', '    memcpy(e->be, e->file, 0xCE8);\n', '')
m('s5: subtitle a pixel right', '    show_hdr(scr, 0xE632, e->be);', '    show_hdr(scr, 0xE633, e->be);')
m('s5: second line is the first', '    scene5_subtitle(e, scr, 1);', '    scene5_subtitle(e, scr, 0);')
m('s5: face a row lower', '    show_raw(scr, 0x1469, e->b22 + e->face, 0x68, 0x5E);', '    show_raw(scr, 0x15A9, e->b22 + e->face, 0x68, 0x5E);')
m('s5: face 80 passes', '    if (++e->wait > 0x50) {', '    if (++e->wait > 0x4F) {')
m('s5: two face pictures', '        if (e->face == 0x7290)', '        if (e->face == 0x4C60)')
m('s5: eyes 700 passes', '    if (++e->wait > 0x2BC) {', '    if (++e->wait > 0x2BB) {')
m('s5: eyes a pixel left', '    show_raw(scr, 0x2ED0, e->b36 + e->face, 0x20, 0x1A);', '    show_raw(scr, 0x2ECF, e->b36 + e->face, 0x20, 0x1A);')
m('s5: eyes once round', '            if (++e->rounds >= 2)', '            if (++e->rounds >= 1)')
m('s5: eyes wait kept between rounds', '            e->face = 0;\n            if (++e->rounds >= 2)', '            e->face = 0;\n            e->wait = 5;\n            if (++e->rounds >= 2)')

# the sixth scene (step 7)
m('s6: wipe from row 1', '    e->count = 0;\n    e->at = 0xF8C0;', '    e->count = 0x140;\n    e->at = 0xF8C0;')
m('s6: no wipe from the bottom', '    memcpy(scr->pix + e->at, e->b22 + e->at, 0x140);', '    (void)0;')
m('s6: wipe a row a step', '    e->count += 0x280;\n    e->at -= 0x280;', '    e->count += 0x140;\n    e->at -= 0x140;')
m('s6: 99 wipe steps', '    if (++e->wait < 0x64)', '    if (++e->wait < 0x63)')
m('s6: fade to the first palette', '        if (e->pal[i] > e->pal[0x300 + i])\n            e->pal[i]--;', '        if (e->pal[i] > e->pal[i])\n            e->pal[i]--;')
m('s6: fade only up', '        if (e->pal[i] > e->pal[0x300 + i])\n            e->pal[i]--;\n        else if', '        if (0)\n            e->pal[i]--;\n        else if')
m('s6: fade two at a time', '        else if (e->pal[i] < e->pal[0x300 + i])\n            e->pal[i]++;', '        else if (e->pal[i] < e->pal[0x300 + i])\n            e->pal[i] = (uint8_t)(e->pal[i] + 2 > e->pal[0x300 + i] ? e->pal[0x300 + i] : e->pal[i] + 2);')
m('s6: 63 fade steps', '    return ++e->wait < 0x40 ?', '    return ++e->wait < 0x3F ?')


def main():
    if not CACHE.exists():
        print('run tools/ending_check.py --n 20000 --cache work/ending_check/s6.rec first')
        return 2
    missed = bad = 0
    WORK.mkdir(parents=True, exist_ok=True)
    only = os.environ.get('ONLY', '')
    for name, old, new in M:
        if only not in name:
            continue
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
        r = subprocess.run([sys.executable, str(ROOT / 'tools/ending_check.py'), '--n', '20000', '--cache', str(CACHE),
                            '--port', str(exe), '--keep', str(WORK / 'keep')], capture_output=True, text=True, cwd=ROOT)
        if r.returncode == 0:
            print('MISSED: ' + name)
            missed += 1
        else:
            print('caught: ' + name)
    ran = sum(1 for name, _, _ in M if only in name)
    print(f'{ran - missed - bad} of {ran} caught, {missed} missed, {bad} bad')
    return 1 if missed or bad else 0


if __name__ == '__main__':
    sys.exit(main())
