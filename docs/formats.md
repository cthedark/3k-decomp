# File formats

One section per format. Each says how sure we are and how it was found. For
the evidence trail and dates see `findings.md`. Addresses and sizes refer to
the files in `original_bin/` (checksums in `original_checksums.sha256`).

Status key: **decoded** (reproduced with a tool), **partial** (some structure
known), **unknown**.

| File(s) | Status | Summary |
|---------|--------|---------|
| `NUMBER.LCZ` | **decoded** | Chinese bitmap font, Big5-indexed |
| `MES_TXT.LCC` | partial | Big5 dialogue text; record structure unknown |
| `*.LCZ` (about 34 files) | **mostly decoded** | Chunked, run-length-encoded containers of pictures, sprite sheets and animation frame stacks; a few chunks unresolved |
| `STORYn_T.LCZ` | **decoded** | Subtitles: 1-bit bitmaps, one 320x15 image per spoken line |
| `RPG.LCP`, `ANI.LCP` | partial | Start with a VGA palette at offset 0; the rest is unknown |
| `STORY1_V` .. `STORY6_V` | **decoded** | Concatenated Creative Voice File clips: 8-bit mono, 13,158 Hz |
| `*.SL` | partial | DOS executables chained by `execl`; code not yet analyzed |
| `SAVEnn.000/.001/.002`, `*.NOW` | partial | Save slot and working state; sizes and load flow known, internals unknown |
| `.LCC`, `.ALL`, `TAB.TAB`, `__*.AL?`; some chunks inside `.LCZ` | unknown | |
| `SND_EF.1`, `_1_.LOC` | **decoded** | WALK.SL's sound effects: bare VOC sound-data blocks at offsets the code names (`findings.md` "WALK.SL step 25") |

---

## NUMBER.LCZ: bitmap font (decoded)
- Size 407,956 = 16-byte header + 13,598 glyphs x 30 bytes.
- Glyph: 16 pixels wide, 15 rows, 2 bytes per row, MSB = left pixel. A set bit
  is ink.
- Index for a Big5 character with bytes `lead`, `trail`:
  `(lead - 0xA1) * 157 + t + 33`, with `t = trail - 0x40` for `trail` in
  0x40..0x7E and `t = trail - 0xA1 + 63` for 0xA1..0xFE.
- Verified by rendering the 30 UI button labels and the game title.
- Reference implementation: `tools/render_font.py`.
- Unknown: the 16-byte header; half-width (ASCII/digit) glyph storage; what
  lies in the first 33 glyph slots (probably symbols before the Big5 range);
  whether glyphs beyond the Big5 range are used.
- Despite the name, it holds no digit graphics that we found. Unlike the other
  `.LCZ` files it is **not** chunked or compressed; it is raw glyph data. The
  `.LCZ` extension is therefore not a reliable format marker (see below).

## MES_TXT.LCC: dialogue text (partial)
- 98,561 bytes. About 48% of bytes decode as valid Big5 double-byte pairs; the
  rest is ASCII punctuation, spaces and control-like bytes.
- Starts with six spaces (0x20) then text. Lines such as 歡迎光臨 appear as plain
  Big5 in the file.
- The game opens it just before drawing dialogue, then opens `NUMBER.LCZ` once
  per character drawn.
- Unknown: how messages are indexed (offset table, delimiters such as `#`/`!`
  or `?`, speaker tags), and whether any are conditional.

## RPG.LCP / ANI.LCP: palettes (partial)
- Both files start with a VGA palette **at offset 0** (no header): 768 bytes =
  256 x (R,G,B), each 0-63 (6 bits, the range VGA hardware accepts). Multiply
  by 255/63 for 8-bit colour.
- `RPG.LCP` (120,013 bytes) is the **interface palette**. Verified by rendering
  `GMAIN.LCZ` with it: the olive stone frame, F1-F8 buttons and the "Ready" text
  match the in-game screenshot. (An earlier note here said "5-byte header";
  that was wrong. Starting 4 bytes late gave a green wash.)
- `ANI.LCP` (19,167 bytes) is the **animation / slideshow palette**. Verified
  by rendering `STORY1.LCZ` and `STORY3.LCZ` with it: the cliff scene and the
  hall render in natural colours.
- Unknown: what follows the palette in each file (`RPG.LCP` has about 119 KB
  more; `ANI.LCP` about 18 KB more). Probably more interface art and animation
  data.

## *.SL: chained programs (partial)
- Real MZ executables. Started by the running program through the C runtime's
  `execl`, which the DOS trace shows as EXEC type 3 (function 4B03h). Observed
  in the trace: `WALK0.SL` at the title/menu, `WALK.SL` for the exploration
  screen. (2026-10-14: earlier text here said "overlay"; they replace the caller
  and are not called like a library. See `code.md`, "Program chaining".)
- Three of five are PKLITE-packed. The unpacked ones are in `work/unpacked/`.
- Each has a normal MZ entry point and C start-up code. Their internal logic is
  not yet analysed.

## STORYn_V: voice-over clips (decoded)
- Six files, `STORY1_V` to `STORY6_V` (slide 0 has none). No file extension.
- Each file is several complete Creative Voice File (VOC) files concatenated.
  Find clips by searching for the 19-byte text `Creative Voice File`.
  Clip counts: 8, 3, 10, 5, 8, 11 (45 total). The game opens each file once per
  line spoken, and the counts match exactly.
- Clip layout: 26-byte VOC header (version 0x010A), one type-1 block
  (divisor byte, codec byte 0, then 8-bit unsigned PCM), one type-0
  terminator byte.
- Sample rate = 1,000,000 / (256 - divisor) = 13,158 Hz for all 45 clips.
  Mono. Total about 157.5 s.
- Reference implementation: `tools/voc_extract.py`.
- Structure verified, and **confirmed by listening**: the operator identified
  `STORY1_V_01.wav` as the first voice line heard on the second slide (slide 0
  has no voice). So clip n of `STORYk_V` is the n-th spoken line of slide k.
- Unknown: how a clip is chosen for a subtitle line (`TAB.TAB`?).

## SAVEnn.000 / .001 / .002 and the .NOW files (partial)
- Load flow (trace): read all 11 `SAVEnn.000` for the slot list, then for the
  chosen slot read `.000` (creates `MAN_INFO.NOW`, 504 bytes), `.001` and `.002`
  (creates `__1.NOW`, 92,750 bytes).
- Sizes: `.000` about 100-230 bytes, `.001` about 18 KB, `.002` about 8.5 KB.
  `.001 + .002` is 26,544 bytes in two saves, which equals `__1.AL1 + __1.AL2`,
  but the contents differ.
- `.000` shows a repeating pattern: `04` then numbers, and record ids
  `c8 c9 ca cb cc`, followed by 16-bit little-endian values (for example
  0x0898, 0x07d0, 0x0258). Possibly stats for five characters.
  Observation only.
- Unknown: everything else. Likely compressed or sparse (26.5 KB expands to
  92.7 KB).

## STORYn_T.LCZ: subtitle bitmaps (decoded)
- Seven files (`STORY0_T` to `STORY6_T`), each one chunk in the `.LCZ` container.
- The decoded chunk is a sequence of **600-byte lines**. Each line is a
  **320 x 15 pixel, 1 bit per pixel** bitmap (40 bytes per row, 15 rows, MSB =
  leftmost pixel). Text is already drawn into the bitmap; no font lookup. The
  trace shows `NUMBER.LCZ` (the font) is not opened during the slideshow,
  which agrees.
- Line counts: `STORY0_T` 7, `STORY1_T` 8, `STORY2_T` 3, `STORY3_T` 10,
  `STORY4_T` 5, `STORY5_T` 8, `STORY6_T` 11. For slides 1-6 this equals the
  number of voice clips in `STORYn_V`, so line k is likely the subtitle for
  clip k. Slide 0 has 7 subtitle lines and no voice.
- Verified by rendering the first four lines of `STORY1_T`, which read
  (opening narration): 故事的發生，該由亞紀元 1660 年， / 統治一半以上虹奇星球的邦沛帝國開始。 /
  邦沛帝國十一歲的英俊王子卡洛安， / 在王室狩獵中，為了追趕一隻巨大的太格虎，
- Not yet verified: the pairing of line k with clip k beyond slide 1's first
  clip. Likely but untested for the others.

## TAB.TAB (unknown)
- 1,160 bytes. Opened before every voice sequence and also at game start, so it
  is not specific to the slideshow. Bytes begin `00 00 00 67 0e 67 0e 00 aa 20
  11 2f ...`; no pattern recognized yet.

## *.LCZ: chunked run-length-encoded containers (mostly decoded)
- A file is a series of chunks: `[esc: 1 byte][size: 2 bytes, little-endian,
  counts these 3 header bytes][data: size - 3 bytes]`.
- Data is run-length encoded. The chunk's first byte (`esc`) is also the
  marker: `esc, count, value` means `count` copies of `value`; any other byte is
  a literal. `esc` differs between files (0x00, 0x01, 0x02, 0x09, 0x0b, 0x0d,
  0x14, 0x15, 0x1d, 0x42 seen). Probably a byte value that is rare in the data.
  How a literal `esc` byte is stored is not known.
- 33 of the 36 non-voice `.LCZ` files parse cleanly to the end of the file with
  this rule. Failures: `NUMBER.LCZ` (raw font, not chunked), `SPIC.LCZ` and
  `STORY5_1.LCZ` (a chunk size field is inconsistent: not understood).
- `STORY0_1.LCZ` is not an image container: it is a single Creative Voice File
  clip with **codec 1** (Creative 4-bit ADPCM, about 6,060 Hz, 20.87 s). (An earlier version
  of this file said "not decoded"; it is decoded now, see "VOC codec 1" below.) It is the
  sound of slide 0: `STORY2.SL` plays it from byte 0x1a, so slide 0 does have audio, just
  no spoken voice. Its loudness runs from quiet through a loud middle (about 3 to 6 s) and
  fades to near silence at the end; whether it is music or an effect is a judgement for the ear.

## CMF songs in MUC.ALL (decoded 2026-10-15)
- `MUC.ALL` (188,188 bytes) is **25 Creative Music Format songs back to back**; each starts with
  the signature `CTMF` and runs to the next one (offsets 0x2, 0x2326, 0x4ead, 0x8fb6, 0xa4b3,
  0xb52b, 0xcfd3, 0xdcbd, 0x10812, 0x12d97, 0x14a5e, 0x151ca, 0x16a2a, 0x1744e, 0x18e7e,
  0x1aca4, 0x1cad2, 0x1e085, 0x20618, 0x22084, 0x2421e, 0x2529a, 0x27137, 0x2bad9, 0x2d76b).
  All are version 1.1 with 96 ticks per second, no title/composer/remarks text, and 6 to 14
  instrument patches.
- **Layout** (public CMF format, ModdingWiki "CMF Format"): header of 40 bytes (offsets to the
  patch block and the music at 6 and 8, ticks per quarter at 10, **ticks per second at 12**,
  channel-in-use table at 20, patch count at 36, a basic tempo at 38), 16-byte OPL patches, then a
  MIDI track body (variable-length deltas, running status, `FF 2F 00` ends it).
- **Which song the intro plays:** the one at `0x27137` (song 22, 18,850 bytes = the `0x49a2` the
  program reads at `STORY2.SL 0x10288`). The title (`MARK.EXE 0x10589`) seeks to `0x7137` + 2 x
  65,536 = the same `0x27137`; its other read (`0x103b8`, seek `0xd76b` + 2 x 65,536 = `0x2d76b`)
  is song 24, the last one. Not yet checked what `MARK` does with each.
- **Song 22** (`tools/cmf_survey.py original_bin/MUC.ALL 0x27137 18850`): 10 patches, 9 channels,
  4,804 note events, 14 program changes (programs 0 to 9, all present in its own patch block), one
  controller (`0x67` = 0: melody mode, no rhythm channels), no pitch bends, longest simultaneous
  notes 9, **last event at tick 12,599 = 131.2 s** at 96 ticks/s. Reader: `src/cmf.c` agrees with
  the survey (4,819 channel events, same end tick); `tests/test_cmf.c`.
- **Rhythm mode** (controller `0x67` = 1) is used by songs 0 and 3 only (notes on channels 11-15 are
  drums); the other 23 songs are melody-mode. `tools/cmf_to_mid.py` exports the songs as MIDI
  (timbres are not carried; see findings.md, 2026-10-15).
- **Not decoded:** how the Creative FM driver turns a note into OPL registers (frequency, volume
  from velocity, voice allocation, patch loading). The wiki says velocity goes more or less
  directly into the operator output level and that the driver never uses pitch bend; neither was
  checked against this driver.

## VOC codec 1: Sound Blaster 4-bit ADPCM (decoded 2026-10-15)
- A VOC type-1 block is `[divisor][codec][data...]`; the rate is 1,000,000 / (256 - divisor).
  For `STORY0_1` the divisor is 91, which is 6,060.6 Hz, and the data is 63,230 bytes.
- The first data byte is the starting level (0x80 here) and gives no sample. Each later byte holds
  two nibbles, high first, so n data bytes give 2(n-1) samples: 126,458 samples = 20.87 s.
- A nibble is a sign bit (0x8) and a 3-bit magnitude m. With step size r (0..3, starts at 0) the
  level moves by sign * ((m << r) + ((1 << r) >> 1)), clamped to 0..255. Afterwards r grows by one
  if m >= 5 (up to 3) and shrinks by one if m == 0 (down to 0).
- **Source of the rule:** DOSBox-X's Sound Blaster emulation (`src/hardware/sblaster.cpp`, its
  `scaleMap_ADPCM4` / `adjustMap_ADPCM4` tables), written here as the formula above. It was not
  compared with real hardware.
- **Evidence it is right (and what it is not):** decoded as above, the 21 seconds stay centred
  (mean within about +-150 of zero in 16-bit units in every 1 s window). FFmpeg's `adpcm_sbpro_4`
  decoder uses the same step logic but no half-step term and starts its predictor at 0, and its
  output drifts to about -15,000 in the same clip (correlation with ours only 0.30). Each rule
  was transcribed into Python and reproduces its own decoder bit for bit, so the difference is
  between the rules, not a slip in either implementation. A centred result is consistent with
  our rule; it does not prove bit-exactness against the real chip. Listening is the real check.
- Code: `src/voc.c`. `tests/voc_dump.c` writes a clip as WAV.

## VOC codecs 2 and 3: 2.6-bit and 2-bit ADPCM (decoded 2026-10-22)
- Used by `SND_EF.1` and `_1_.LOC`. The same scheme as codec 1 with shorter codes; the first data byte is again the starting level.
- **Codec 2:** a byte holds three codes: bits 7-5, bits 4-2, and bits 1-0 with a 0 bit below them. A code is a sign (4) and m 0..3; the
  step r runs 0..4, the level moves by (m << r) + ((1 << r) >> 1), but by 10m + 5 at r = 4. r grows when m = 3, shrinks when m = 0.
  n data bytes give 3(n-1) samples.
- **Codec 3:** four 2-bit codes a byte, high first; a sign (2) and m 0 or 1; r runs 0..5, grows when m = 1, shrinks when m = 0. 4(n-1)
  samples.
- **Source and check:** DOSBox-X's tables (`scaleMap_ADPCM2/3`, `adjustMap_ADPCM2/3` in `src/hardware/sblaster.cpp`), written as rules;
  the port's samples equal a decoder using those tables for all 12 blocks the game plays. Not compared with real hardware.

## TAB.TAB: the voice-over index (decoded 2026-10-15)
- Six runs of 5-byte entries, one run per slide 1..6, at file offsets `0x3a7`, `0x3cf`,
  `0x3de`, `0x410`, `0x429`, `0x451` (the constants `STORY2.SL` seeks to). One entry per
  subtitle line: 3-byte offset into `STORYn_V`, then a 2-byte length, little endian. Slide 6's
  table ends exactly at the end of the file (1,160 bytes); the first 935 bytes, before slide 1's
  table, are not read by the intro and were not examined.
- **Checked against the data:** for every slide the entries start at 0, each begins where the
  previous ends, the last ends at the exact size of `STORYn_V`, each offset is where the
  signature "Creative Voice File" starts, and each length equals the VOC file header (26) + block
  header (4) + payload + end byte. Entry counts 8, 3, 10, 5, 8, 11 equal the subtitle line counts.
- Clip lengths (seconds): slide 1: 3.84 4.06 3.10 4.42 4.80 2.85 4.07 4.65; slide 2: 3.76 4.50
  3.13; slide 3: 4.78 3.45 2.29 4.17 3.31 2.45 3.52 2.15 4.37 4.61; slide 4: 2.43 3.44 3.30
  4.58 2.84; slide 5: 4.08 3.33 4.01 3.20 4.10 2.98 4.50 2.34; slide 6: 2.87 4.08 2.07 3.83
  2.35 2.40 2.67 4.36 3.78 3.71 1.93. Total 157.5 s.
- Silence at the ends of the clips (samples within +-6 of the centre): the start has 0 to 517 ms
  (typically 200 to 350 ms) and the end 0 to 420 ms (mostly under 200 ms).
- Code: `src/voice.c`.
- **What a decoded chunk contains.** Four kinds were found (58 non-subtitle
  chunks examined). All pixels are 8-bit palette indices.
  1. **Full screen**: exactly 64,000 bytes = 320 x 200. 8 chunks.
  2. **Sprite with header**: `[w: 2 bytes LE][h: 2 bytes LE][w*h pixels]`, and a
     chunk may hold several of these back to back (for example `ENDING1` chunk 0 is
     five 41x44 sprites; `ENDING2` chunk 1 is five different sizes). The
     same header is also used for full-screen 320x200 pictures (64,004 bytes).
     16 chunks.
  3. **Headerless picture or frame stack**: raw pixels with **no size stored in
     the file**. The chunk is one picture, or N equal frames stacked one under
     the other (an animation). The program that loads it must know the width and
     height; they have to be recovered from the code or from the picture
     itself. 31 chunks placed by width estimate, of which some were wrong (see
     "Not resolved"). Sizes found so far are in the table below.
  4. **Mask**: two-valued pictures (`0xFE` and `0x00`). `TITLE1.LCZ` chunks 2-5
     are the title-logo characters 三 界 諭 and 邦沛之謎 (black ink on `0xFE`).
     CORRECTION 2026-10-14: in the title code `0xFE` is the visible fill and `0` is the
     transparent part (the overlay routine copies non-zero bytes); chunks 2-4 are
     headerless 67x67 images and chunk 5 a headerless 112x24 image. See `code.md`,
     "Title screen". Chunks 0 and 1 are sprites with `[w][h]` headers, 67x200 and 260x24.
     `0xFE` is probably the "transparent" colour key. Likely, not proven.
- **Why frame sizes matter for the port.** The `.LCZ` files do not describe
  headerless chunks. A faithful rebuild needs the (width, height, frame count)
  of each, so the constants have to be read from the overlay code (`STORY2.SL`,
  `WALK.SL`, ...). The values in the table were found by inspecting the
  pictures, not read from code.

### Sizes found by inspecting the rendered pictures
Viewed means the rendered result was looked at and looks right.

| File, chunk | Size | Viewed | What it shows |
|-------------|------|--------|---------------|
| `STORY2` 0 | 320x200 | yes | Road through cliffs at sunset (base picture) |
| `STORY2` 1 | 155x125, 3 frames | yes | Woman's hair moving |
| `STORY2` 2 | 155x125, 1 frame | yes | Fourth hair frame |
| `STORY4_2` 0 | 79x99, 7 frames | yes | Woman's eyes closing, tears appear |
| `STORY4_3` 0 | 79x99, 4 frames | yes | Woman's eyes moving |
| `STORY4_4` 0 | 23x19, 20 frames in two banks of 10 (CORRECTED 2026-10-15 from "23x20, 19 frames"; the program's draw calls use 23x19 and a 437-byte frame step, bank 1 at +0x1112) | yes | Tiny sparkle/firework-like effects |
| `STORY1_2` 0, 1 | 320x43 and 320x39 (header) | yes | Cloud strips |
| `STORY1_2` 2 | 123x96, 4 frames | yes | Woman's face |
| `STORY2_1` 0 | 75x78, 4 frames | yes | Small figure on a cliff path |
| `STORY5_1` 1 | 97x84, 3 frames | yes | Woman in red |
| `STORY6_1` 0-2 | 189x160, 3 chunks of 1 frame | yes | Woman in red robe, hair in the wind |
| `ENDING4` 0 | 320x198 | yes | Woman sitting in a forest (base) |
| `ENDING4` 1-3 | 320x165 each | yes | Same scene, dress turning red to blue |
| `ENDING4` 4-6 | 320x165 each | no | Same size; rendered, not looked at |
| `ENDING4` 7 | 104x94, 3 frames | yes | Blonde woman in blue gown with tiara |
| `ENDING4` 8 | 32x26, 8 frames | yes | Small face frames (mouth movement) |
| `ENDING1` 0 | five 41x44 (header) | yes | Face frames |
| `ENDING1` 1, 2 | 320x147 | yes | Red-robed figure |
| `CAST1` 0 | 320x100 | yes | Embossed title banner 三界諭 |
| `TITLE1` 0 | 67x200 (header) | yes | Blue crystal strip |
| `TITLE1` 1 | 260x24 (header) | yes | Flame-coloured logo strip |
| `TITLE1` 2-4 | 67x67 masks | yes | 三, 界, 諭 |
| `TITLE1` 5 | 112x24 mask | yes | 邦沛之謎 |
| `TITLE1` 6 | 93x26 (header) | yes | Text 全體陣亡 ("entire party dead") |

Frame count and split can be off in a few cases: a stack of N frames of one
size and a stack of 2N frames of half the height cover the same bytes
(`ENDING4` chunk 8, `STORY2_1`, `STORY4_4`). The choice above is the one that
looked right.

### Not resolved
- `BOAT.LCZ`: chunks 0 and 1 (192x168, 192x29) decode, but the colours are wrong
  with the `ANI.LCP` palette (yellow clouds), so this scene probably uses its own
  palette. Chunk 2 (14,880 bytes) is still noise at 96x155.
- `CAST2.LCZ` (24,900), `SPIC.LCZ` (both chunks), `STORY3_1.LCZ` chunk 1 (330
  bytes), `ENDING1T.LCZ` (14,160 = not a multiple of 600) and `ENDING4T.LCZ` (6,600 =
  11 x 600, probably subtitle lines like `STORYn_T`): not placed.
- **Warning on width guessing:** the row-similarity heuristic prefers narrow
  widths on pictures with flat areas (it chose 16 for `CAST1`, which is
  really 320 wide). Always look at the picture. `tools/lcz_sheet.py` marks any
  guess as GUESSED for that reason.
- Reference implementations: `tools/lcz_decode.py` (`list`, and `render` for
  chunks of 64,000 bytes or more) and `tools/lcz_sheet.py` (contact sheet of
  every chunk; `--dims CHUNK:WxH` to test a size hypothesis; `--guess` to
  auto-guess widths).
- A multi-chunk file probably holds several frames or parts of one scene (for
  example `TITLE1.LCZ` has 7 chunks, `ENDING4.LCZ` has 9). Their roles are
  unknown.
