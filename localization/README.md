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

## Playing in English or Korean
Put a `3k.ini` in the directory the game is started from:

    [game]
    language=en          ; zh (the original, the default), en or ko
    localization=localization

`build/3k --config FILE` reads another file; `--lang zh|en|ko` overrides the file's language. The headless build (the checks)
reads no `3k.ini` unless `--config` or `--lang` is given, so the comparisons always see the Chinese original. Anything without a
translation is drawn in Chinese. `TK_TRACE_MESSAGES=1` prints the number of every message box shown, to find a text's id while
playing.

## The language mode (step 2, built 2026-11-01)
- `src/settings.c`: the settings file. `src/l10n.c`: loads `source/`, `<lang>/` and `fonts/`; looks a text up by id or by the
  original's bytes; decodes UTF-8; wraps; draws with a colouring function so each place keeps its original colours.
- Where it draws: `msgbox_open` (by message number: wrapped at words into rows of 320 pixels, three rows a box, further boxes on the
  next key or right click when the box waits; a box that does not wait shows its first three rows); `battle_text_pic` (by the
  original's bytes: names, list rows, short lines; the narrow font when the normal one does not fit); `walk_label_sprite` (the
  buttons, centred); the menu's items (`menu.0` .. `menu.3`: the font doubled in the original lettering's two colours).
- Not yet: the picture texts other than the menu (subtitles, shouts, title line, game over), and the shop's greeting strip, whose
  place the original works out from the Chinese length.
- The game's own font draws `'` as a low comma, so apostrophes use its `` ` `` (a high quote); `@ { | } ~` are other marks in it
  (a circle, corner brackets) and the check rejects them.
- `build/l10n_check DATA LANG` (in `make test` for both languages): every id is a source id, every character is in the fonts, every
  text fits its room (labels 32 pixels, yes / no 16, names their bytes x 8 or the narrow font, items and spells also at most 16
  characters, menu items 120, subtitles their picture's width). It also counts the message boxes.
- `glossary.tsv`: the names and terms used so far.
## The translations (step 3, done 2026-11-01)
Both languages: every message with text (760; not 8, 213 and 748-751, which are the tables of `fields`, nor 108 and 336, which
are not words, nor the empty 708), every item (219) and spell (50), every field (the buttons, names, card game lines, shop strips,
the inn's and healer's price rows) and the programs' own strings: 1,065 texts each, all passing `l10n_check`. Drafted from the
Chinese by Kiro; to be read through by the operator while playing.
- Names follow the rule (pinyin; Korean readings), listed in `glossary.tsv`. Some Korean readings are also ordinary words (娜蒂
  나체 "nude", 莎坦 사탄 "Satan", 貝倫 패륜 "immorality", 卡文 가문 "family") and some names are Norse names in Chinese (耶夢加得
  Jormungandr, 芬麗絲 Fenrir, 尼德赫克 Nidhogg, 赫拉 Hel, 洛奇 Loki): marked "(?)" there, for the operator to decide.
- Spells: English by meaning, Korean by the Sino-Korean reading of the characters.
- Strings the game puts together (the coins F3 shows, a sale's prices) are translated piece by piece: `battle_text_pic` takes the
  ASCII as it is and each run of Chinese by its own entry (`code.coins`, `code.price`, `code.half`, `code.sell`).
- The shop's greeting strip is laid out by the translation's width (`battle_text_cols`, at most 40 columns), not the Chinese one's.
- `l10n_check` also checks: the boxes that do not wait (sellers, askers, bosses and the system prompts: the list is in the check,
  from `tools/l10n_nowait.py`) get one box of three rows; a shop's greeting (messages 1-6) fits its 320-pixel strip.

## Next
4. The pictures: the intro and ending subtitles, the battle's shouts, the title line and the game-over lettering.
