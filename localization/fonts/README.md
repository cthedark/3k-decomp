# Fonts of the translated modes

Made by `tools/l10n_fonts.py` (format in its header). ASCII in the normal font is the game's own (`RPG.LCP` 0x1a6b7, read at run
time), so it is not here.

| File | Glyphs | Made from | Licence |
|------|-------:|-----------|---------|
| `tk_hangul16.tkf` | 11,240 (Hangul syllables, modern jamo, some punctuation), 16 x 14 | Neo Dunggeunmo (Neo둥근모) v1.601, `neodgm.ttf`, by Eunbin Jeong, https://github.com/neodgm/neodgm | SIL Open Font License 1.1 (`OFL-tk_hangul16.txt`) |
| `tk_narrow6.tkf` | 106 (ASCII, some punctuation), 6 x 14 | X11 misc-fixed `6x13.bdf`, by Markus Kuhn, https://gitlab.freedesktop.org/xorg/font/misc-misc | public domain |

`tk_hangul16` is a converted copy of Neo Dunggeunmo (rasterized at its 16-pixel size and cut to rows 0 .. 13). The OFL lets a
converted copy be used and shared under the same licence, but not under the font's Reserved Font Names ("Neo둥근모", "Neo둥근모
Code", "NeoDunggeunmo", "NeoDunggeunmo Code"), hence its own name. It stays under the OFL; the licence text is beside it.

To make them again:

    curl -L -o work/fonts/neodgm.ttf https://github.com/neodgm/neodgm/releases/download/v1.601/neodgm.ttf
    curl -L -o work/fonts/6x13.bdf https://gitlab.freedesktop.org/xorg/font/misc-misc/-/raw/master/6x13.bdf
    python3 tools/l10n_fonts.py work/fonts/neodgm.ttf work/fonts/6x13.bdf
