# Localization: English and Korean

Goal: play the port in Chinese (`zh`, the original), English (`en`) or Korean (`ko`), chosen with `language=zh|en|ko` in a settings
file. Only on-screen text is translated; the voice-over stays in Mandarin. Chinese stays the default and the only mode the
comparison checks run in, so the translated modes change nothing that was checked.

Decisions (operator, 2026-11-01): names in English as tone-less pinyin, in Korean by their Korean reading; no extra UI (the settings
file only); translations live here and may be committed (no original binaries in the repository). The title logo 三界諭 stays as it
is in every language. The credits stay in Chinese (not translated). Item and spell names get a narrower pixel font in the
translated modes and at most 16 characters, with abbreviations where a name is longer.

## Layout
- `source/`: what is translated, extracted by `tools/l10n_extract.py` (re-run it to regenerate) or, for text drawn as pictures,
  transcribed by eye (`source/pictures.tsv`). Tab-separated, UTF-8: `id`, `bytes` (the room the original gives), `zh`, and in
  the extracted tables `big5` (the original bytes in hex, which the port matches drawn strings against).
- `en/`, `ko/` (step 3): the translations, one file per source table, keyed by the same ids.

## Inventory (step 1, 2026-11-01)
Counts are rows with text; CJK is the number of Chinese characters.

| Table | Rows | CJK | Where it comes from | How the port draws it | Room |
|-------|-----:|----:|---------------------|-----------------------|------|
| `messages` | 768 | 23,476 | `MES_TXT.LCC`, record n at (n - 1) x 0x80 + 6, up to `#` | `msgbox_open` (talks, shops, level-ups, the system keys), `shop_text` (a shop's greeting) | 0x78 bytes: 3 rows of 40 half-width or 20 Chinese characters |
| `fields` | 24 | 72 | `MES_TXT.LCC` fixed fields: shop strip 0x306, button labels 0x386, members' names 0x6a06, card game 0x17586 | `walk_label_sprite` (labels), `battle_text_pic` (the rest) | labels 4 bytes (32 x 14 pixels); names 6; card lines 20 or 18 |
| `items` | 219 | 767 | `OBJECT.LCC`, 12 of each 30-byte record | `battle_text_pic` (lists, equipment, F3, F7, shops) | 12 bytes, 96 pixels |
| `spells` | 50 | 160 | `MAGICT.LCC`, 12 of each 20-byte record, 10 a member | `battle_text_pic` (the magic list) | 12 bytes, 96 pixels |
| `code` | 6 | 12 | the programs' own strings (in `src/walk.c`: yes / no, coins, the sale's prices and question) | `walk_label_sprite`, `battle_text_pic` | as listed |
| `pictures` | 127 | 1,051 | text drawn into pictures (below) | each its own picture code | the picture's size |

`messages`: 100 talks run over several records (a record ending in 0x7f is followed by the next in the same talk). The `@` in the
messages is drawn as a small circle, the original's sentence stop. Record 336 is not text (`EWKLJFL...`); record 708 is empty.
Records 7, 8, 213 and 748-751 hold the fixed fields of `fields`; whether every other record is shown somewhere is not known.

### Text in pictures
Found by looking at renders: every screen kind the battle and world checks record (22 kinds, 6 starts each) and every `SPIC.LCZ`
sign (`python3 tools/l10n_shots.py`); the menu, credits, intro, title and game-over screens (their `--step-dump` files through
`tools/l10n_shots.py --raw`); and the text pictures at 2x, which the `pictures` table is transcribed from
(`python3 tools/l10n_textpics.py`). All renders go to `work/l10n/` (derived from game data, not committed).

| Picture | Where | Text | Plan |
|---------|-------|------|------|
| Intro subtitles | `STORY0_T` .. `STORY6_T.LCZ`, 1-bit 320 x 15 lines | 52 lines (`story0.1` .. `story6.11`) | draw the translated line in their place |
| Ending subtitles | `ENDING1T.LCZ` (4 lines 236 x 15), `ENDING4T.LCZ` (2 lines 220 x 15) | 6 lines | the same |
| Battle shouts | `RPG.LCP` 0x198d8, 12 one-bit 136 x 14 pictures | 12 lines | the same, 136 pixels wide |
| Menu items | `RPG.LCP` 0x1384, four labels | 新遊戲開始, 取一舊進度, 製作群介紹, 回到 DOS | the same, in the menu's colours |
| Credits | `CAST2.LCZ`, a 1-bit picture 120 wide, 1,660 rows (CAST.SL and ENDING.SL) | 19 roles, 31 name lines (the publisher, 第三波文化事業, among them) | kept in Chinese (decided); transcribed for reference only |
| Title line | `TITLE1.LCZ` chunk 5, 112 x 24 | 邦沛之謎 | draw the translation in the same mask |
| Game over | `TITLE1.LCZ` 0x3e73, 93 x 26 | 全體陣亡 | draw the translation in the same gold |
| Title logo | `TITLE1.LCZ` chunks 2-4 (three brush characters in textured windows); `CAST1.LCZ` (the embossed banner) | 三界諭 | kept as it is (decided) |

No text to translate: the interface frame (`GMAIN.LCZ`: F-key numbers and "Ready" are in Latin letters); the battle's tabs, bars
and rewards (icons and digits); the shops' and inn's pictures; the card faces (symbols); the maps; the signs of `SPIC.LCZ` (made-up
scripts and symbols, one with "Shin Ting" in Latin letters; their captions are messages); the intro and ending slides.

### What fits where
- Half-width characters come from `RPG.LCP` 0x1a6b7 (128 ASCII characters, 8 x 15, lowercase included), so English can use the
  game's own letters. `NUMBER.LCZ` has only Big5 Chinese (16 x 15): Korean needs a Hangul font added, 16 pixels high
  (DungGeunMo / 둥근모꼴, SIL Open Font License, is the closest in style).
- English runs 2 to 3 times longer than the Chinese: a message's 3 rows of 40 will often not hold it. The translated modes need word
  wrap and extra pages in the message box; item and spell names need 12 half-width characters or a narrower font (or wider
  columns where the screen has room).
- Korean is about as long as the Chinese in characters, and Hangul is full width like Chinese: most text fits as it is.
- Button labels are 32 x 14: two Hangul syllables (대화, 보기) fit exactly; English needs four letters (Talk, Look).

## Next
2. The language mode in the port: the settings file, UTF-8 tables, the Hangul font, word wrap and pages, the picture texts; a
   sample (menu, buttons, the first scene's talks) in both languages; tests that every translation fits and every character is in
   its font.
3. The translations: a glossary of names first, then everything above.
4. The pictures: the subtitles, shouts, menu, title line and game over.
