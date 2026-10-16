# Findings log

Newest entries last. Each entry states the method used and how confident we are.

## 2026-10-14: Phase 1-2, first look and unpacking

### Tools installed
- DOSBox-X 2026.10.01 (`brew install dosbox-x`), for reference runs and the debugger.
- deark 1.7.3 (`brew install deark`), used to unpack PKLITE.

### Unpacking
Command (run on copies in a scratch directory, originals untouched):

    deark -m pklite -o MARK MARK.EXE
    deark -m pklite -o TV TV.EXE

| File | Packed | Unpacked | Unpacked SHA-256 |
|------|--------|----------|------------------|
| MARK.EXE | 15,388 | 25,697 | 388439d9328d5ce7bfd1ea56ed3d082e78b850d1231b4f8412e5734de6813a86 |
| TV.EXE | 8,150 | 12,283 | 402ca7acf1a7fa5f133b376a0ff4b68c5b4810db77c07731115799d7105c98fc |

Unpacked copies live in `work/unpacked/` (git-ignored). deark warns that
decompressed executables are used at your own risk, so we have not yet verified
that the unpacked files run. Verify in DOSBox-X before relying on them.

### What strings show (method: `strings -a` on the unpacked files)
- Both were built with Microsoft C (banner: "MS Run-Time Library - Copyright (c)
  1992, Microsoft Corp"). Confidence: high that the C runtime is Microsoft's;
  exact compiler version and memory model not yet determined.
- `MARK.EXE` contains a Creative Technology FM music driver (copyright 1990,
  "FMDRV") and references `rpg.lcp` and `tv.exe`.
- `TV.EXE` references `number.lcz`, `rpg.lcp` and `mark.exe`, and contains the
  message "Sorry ! Your memory is not enough to play game."
- Both are only 12-26 KB, so they look like launchers/drivers. The game logic
  is probably in `RPG.LCP` (120 KB). Hypothesis, not yet confirmed.

### First Ghidra pass on MARK.unpacked.exe (method: REA open_binary, binary_overview, inspect_native_load_image, list_strings, procedure_pseudo_code)
- Loads as DOS MZ, 16-bit real mode, 136 functions, 24 segments. REA's
  load-image check verified the image against the file bytes (relocations and
  entry point matched). Entry point is at linear address 0x121d2 (in the
  Ghidra image, load segment 0x1000).
- `entry` is standard Microsoft C startup code (DOS version check via int 21h,
  memory sizing, BSS zeroing). It then calls 0x10000, which is very likely
  `main`. Confidence: medium-high, from the pushed arguments and call order.
- 0x10000 looks like a title/intro loop, not the game engine:
  - loops over 64000 units, stepping by 16000 and 0x50. 64000 equals 320x200,
    so the code probably writes to a mode 13h (VGA 256-colour) screen.
    Confidence: medium. Not yet confirmed by finding the video-mode set call.
  - polls a keyboard scan code and exits on 0x39 (space) or 0x1C (Enter). Also
    medium: inferred from constants only.
  - calls an FM music driver (Creative Labs FMDRV strings) and references
    `story2.sl`.
- The exec-error strings (R6005-R6007) are normal C runtime text, but combined
  with the `tv.exe` and `rpg.lcp` strings they suggest MARK hands control to
  another program. Not confirmed.
- Ghidra names are still auto-generated (FUN_1175_0008, etc.). Nothing has
  been renamed or interpreted yet. Several helper functions at segment 0x1175
  and 0x121c are called a lot and are probably C runtime or small library
  routines.

### Working hypothesis (not yet tested)
MARK.EXE (intro/logo plus music) hands off to TV.EXE, which loads `RPG.LCP`
(120 KB, the newest file by date, Sep 5 1993) and `NUMBER.LCZ`. If so, RPG.LCP
is the real game code in a custom packed or encoded format. Next test: analyze
TV.unpacked.exe and find where it opens and uses RPG.LCP.

## 2026-10-14: Corrections and the real program structure

### CORRECTION: RPG.LCP is not packed game code
The earlier hypothesis (above) that `RPG.LCP` is custom-packed game code is
**wrong**. Evidence:
- Bytes 4..771 of `RPG.LCP` are exactly 768 values, all <= 0x3F. That is a
  VGA palette: 256 colours x 3 channels (R,G,B), 6 bits per channel, the range
  the VGA DAC accepts. The first 5 bytes (`02 00 09 04 04`) are probably a small
  header; their meaning is unknown. Confidence: high that 768 bytes are a
  palette, unknown for the header.
- `ANI.LCP` starts with a 3-byte header and then a smooth colour ramp
  (63,61,43 -> 58,55,37 -> ...), consistent with a palette as well.
- Method: Python byte statistics over `original_bin/RPG.LCP` and `ANI.LCP`.
- Not yet known: what follows the palette in each file (likely image or
  animation data).

### Where the code is: the .SL files are DOS executables
All five `.SL` files start with `MZ` and are DOS executables:

| File | Size | State | Notes |
|------|------|-------|-------|
| WALK.SL | 126,865 | not packed | 110 function prologues (`55 8B EC`). Largest. Name suggests map exploration. |
| CAST.SL | 22,903 | not packed | 59 function prologues. |
| WALK0.SL | 17,631 | PKLITE | unpacks to 30,681 bytes |
| STORY2.SL | 19,777 | PKLITE | unpacks to 36,575 bytes |
| ENDING.SL | 14,756 | PKLITE | unpacks to 25,301 bytes |

Method: header and byte-pattern scan with Python; `deark -m pklite` to unpack.
The meaning of the names (WALK, CAST, STORY2, ENDING) is a guess from the
words only. Unpacked copies are in `work/unpacked/` (git-ignored).

Likely design (hypothesis): the game is a set of separate programs
(`.EXE` and `.SL`) that hand control to each other, with `.LCZ`, `.LCC`,
`.LCP`, `.ALL` etc. as data. The `.SL` files may be loaded and run by another
program as overlays or sub-programs. How they are launched is **not** yet
known.

### Filename tables found inside the programs
Method: split the data segment on NUL bytes (Python, both unpacked files).

MARK.EXE, data offsets 0x5b42..0x5b98 (image offsets):
`rpg.lcp, tv.exe, ani.lcp, ani.lcp, rpg.lcp, title1.lcz, muc.all, muc.all,
title1.lcz, walk0.sl, story2.sl`

TV.EXE, data offsets 0x2923..0x293e:
`number.lcz, rpg.lcp, rpg.lcp, mark.exe`

Observations:
- The names repeat in pairs (for example `ani.lcp` twice, `muc.all` twice).
  Possibly one string is the file to open and one is a path or mode, or each
  is used by two different code paths. Unknown.
- `MARK.EXE` and `TV.EXE` name each other, so they hand control back and
  forth. `PLAY.BAT` only starts `mark`.
- `MARK` names `walk0.sl` and `story2.sl`, so it may be the program that starts
  the `.SL` programs. Not confirmed.
- `MUC.ALL` (188 KB) is probably music data, because MARK has an FM music
  driver. Guess.

### TV.EXE main (0x10000): what it does (method: Ghidra pseudo-code)
- Allocates about 8 buffers and treats any failed allocation as fatal (the
  "Sorry ! Your memory is not enough to play game ." message).
- Fills a buffer by writing `0xFF` wherever a random number is below 3000.
  That looks like a "TV static" / noise effect (the file is called TV).
- Then runs one function 100 times, measures the elapsed value, and turns it
  into a speed class 0..4 using thresholds 0x41 (65), 0x78 (120), 0xAA (170),
  300 and 500, written to a variable.
- **Why this matters for the port:** the original measures how fast the machine
  is and adapts. The known crash on fast CPUs (reported on the CDOS page) is
  probably tied to this or to similar delay loops. Confidence: medium. The
  link to the crash is a guess until we run it and read the speed-class
  variable's uses.
- The speed-class meaning (0 = slow or fast?) is unknown.

### Open questions (updated)
1. How are the `.SL` programs started (DOS `exec`/`spawn`, or loaded as
   overlays)? Look for the DOS exec call (`int 21h`, AH=4Bh) in MARK/TV/WALK.SL.
2. What do the `.LCZ`, `.LCC`, `.ALL`, `*_V`, `SND_EF.1`, `TAB.TAB` and
   `_1_.LOC` files contain? `.LCZ` is the biggest group (37 files) and has no
   recognizable header.
3. What is the video mode? 64000-byte loops suggest 320x200x256 (mode 13h).
   Confirm by finding `int 10h` with AX=0013h.
4. What does the original look like when it runs? Not yet observed in DOSBox-X.

## 2026-10-14: First run of the original in DOSBox-X (oracle observations)

Setup: `tools/run-original.sh`, DOSBox-X 2026.10.01, 486 CPU, fixed 10000
cycles, SVGA S3, run from a throwaway copy of `original_bin/` in
`work/dosroot/3K`. Reported by the human operator (cthedark), who watched the
screen. These are human observations, not tool measurements.

### Flow observed
1. `PLAY` starts the game with no error or crash.
2. An intro **slideshow** of semi-animated pictures. Chinese text at the bottom
   of the screen. Music plays and there is a **Chinese voice-over**. The Space
   bar skips a line and advances the slideshow. Enter does nothing.
3. After the slideshow, a **main menu with four options**. Works with keyboard
   and mouse.
4. The first option starts the game: the main character begins in a village.
5. No crash, hang or error message at 10000 cycles (as far as played).

### In-game screen (operator screenshot, about 638x400 pixels)
- Isometric (3/4 top-down) map: a thatched-roof house, a fountain with a statue,
  a planted field, a small post/signpost, and a wooden-log border at the water's
  edge. Diagonal rain streaks fall across the map.
- The window is about 2x of 320x200 (aspect 1.6). That agrees with the earlier
  guess that the game uses VGA mode 13h (320x200, 256 colours). Confidence:
  medium-high. Still not confirmed in code (`int 10h` mode set).
- A textured stone frame surrounds the map. Left column buttons with function
  keys: F1 交談 (talk), F2 觀看 (look), F3 取物 (take), F4 施法 (cast spell),
  F5 物品 (items), F6 裝備 (equipment), F7 使用 (use), F8 地圖 (map).
- Right column: 是 / 否 (yes / no), F9 位置 (position), F10 資料 (data/info),
  then 音樂 (music), 音效 (sound effects), 儲存 (save), 離開 (leave). The last
  four seem to be toggles or actions bound to number keys (a "3" and "4" are
  visible next to two of them; I cannot read the others reliably).
- A small digital number display at the top right shows "486". Its meaning is
  unknown (could be money, a counter, or a frame/step value).
- A small compass or icon sits in the lower left; a small bar in the lower
  right. Meanings unknown.

### What this tells us (hypotheses, not yet tested)
- The intro voice-over probably comes from the `STORYn_V` files (150-460 KB
  each, no extension) and the music from `MUC.ALL`. Reasoning: names only.
  To test: log which files DOS opens during the intro.
- The Chinese text is drawn as pixels. We do not know yet whether the glyphs
  come from a font file or are pre-drawn into the pictures. No font file is
  obvious in the file list.
- The UI is a pre-rendered picture frame plus buttons, probably stored in one
  of the `.LCZ`/`.LCC` image files.
- The game is real-time (rain animates) with menu-driven actions on F-keys,
  matching the CDOS page's note that combat mixes real-time shooting with
  team turn-based play.

### Next
Log DOS file opens while the game runs, to map "screen or event -> file".
That is the fastest route to decoding the data formats.

## 2026-10-14: Traced run (file I/O log) and the font breakthrough

Method: `tools/run-original.sh --trace` (DOSBox-X log channels fileio, exec,
files, int10) while the operator played about 90 seconds. Log kept at
`work/logs/trace.log` (git-ignored). Log timestamps are emulated CPU cycles;
dividing by 10,000,000 gives "seconds" at the fixed 10,000 cycles/ms setting.
**Ordering in the log is reliable; absolute times are not.** The operator's
stopwatch estimates and the log differ by up to about 18 s (for example the
dialog opened at 62 s in the log, "about 80 s" by the operator).

The operator's keyboard has no function keys, so F1-F10 were not used. The
on-screen buttons can be clicked with the mouse instead (Talk worked). The
window was closed with the mouse instead of Ctrl+F9; the log was still written
in full.

### Timeline: operator action -> log events
| Log time | Operator | DOS events |
|----------|----------|-----------|
| 1.00 s | launch | `PLAY.BAT` read, `EXEC mark.EXE` type 0 (load and run) |
| 1.02-1.10 s | | MARK opens `RPG.LCP`, `ANI.LCP`, `MUC.ALL`; **`INT10 Set Video Mode 13`**; opens `TITLE1.LCZ` |
| 11.12 s | pressed Space (skip slideshow) | **`EXEC walk0.sl` type 3** (overlay), then `RPG.LCP`, `GMAIN.LCZ`, `MUC.ALL` |
| 16.75 s | game started ("15 s") | reads `__1.AL1`, `__1.AL2`, **creates `__1.NOW`**; reads `MAN_INFO.ALL`, **creates `MAN_INFO.NOW`** |
| 16.83 s | | **`EXEC walk.sl` type 3** (overlay) |
| 16.84-16.89 s | | `MES_TXT.LCC`, then `NUMBER.LCZ` opened 29 times |
| 17.06-17.11 s | | `SND_EF.1`, `MUC.ALL`, `RPG.LCP`, `NUMBER.LCZ`, `MAN_INFO.NOW`, `__1.NOW`, `TAB.TAB`, `__2.ALL`, `GTAB.ALL`, `M_M.ALL`, `RPG.LCP`, `MUC.ALL` |
| 62.00-62.11 s | talked to villager (mouse, Talk button) | `MES_TXT.LCC`, then `NUMBER.LCZ` opened 41 times |
| 92 s | closed window | no further file opens |

No file activity was logged for walking, toggling music (key 1), or shooting
(Space). So the map, sprites and sounds seem to be loaded once at game start.
Caveat: only opens and closes are logged, not reads; a file kept open could
still be read without a log line.

### Confirmed
1. **Video mode 13h (320x200, 256 colours)** at 1.08 s. Direct evidence
   (DOSBox-X INT10 log). Resolves the earlier open question. (Mode 3 and 12
   lines at 0.00 s are DOSBox's own start-up.)
2. **The `.SL` files are DOS overlays.** `EXEC walk0.sl 3` and `EXEC walk.sl 3`
   use DOS exec function 4Bh subfunction 3 = "load overlay". They are loaded
   into memory by a running program, not started as separate processes.
   (Programs show type 0, like `mark.EXE 0`.) Confidence: high for the exec type;
   what the overlay contains and how MARK calls into it is not yet analyzed.
3. **`WALK.SL` is the engine for the exploration screen.** It is loaded right
   when the new game starts. It is also the largest `.SL`.
4. **`NUMBER.LCZ` is the Chinese bitmap font**, not number graphics. Format
   below. Confidence: high (rendered the game's own labels and title with it).
5. **`MES_TXT.LCC` is plain Big5 text** (in-game dialogue). About 2,480 readable
   runs. Samples (shop dialogue): 歡迎光臨 (welcome), 要買些吃的嗎 (buy
   something to eat?), 買些武器吧 (buy some weapons), 我的武器最棒喔 (my weapons
   are the best), 買些護具防身吧 (buy some armour). Record structure not yet
   understood (the file starts with spaces and punctuation before text).
6. **Talk opens `MES_TXT.LCC`, then the font.** Adjacent in the log, twice
   (game start and the Talk action). Confidence: high for the association.

### Font format (NUMBER.LCZ), reverse-engineered
- 407,956 bytes = 16-byte header + 13,598 glyphs x 30 bytes. No remainder.
  (The header is not understood.)
- Each glyph: 16 pixels wide, 15 rows, 2 bytes per row, most significant bit
  is the leftmost pixel.
- Index: `index = (lead - 0xA1) * 157 + trail_index + 33`, where `lead, trail` are
  the two Big5 bytes, `trail_index = trail - 0x40` for 0x40-0x7E and
  `trail - 0xA1 + 63` for 0xA1-0xFE. The `+33` was constant across the 20
  characters checked, then confirmed by rendering all 30 UI labels and the
  game title correctly.
- The 29 and 41 file opens in the two bursts are about one per character drawn
  (the screen has 30 label characters). Possibly the game opens, seeks and reads
  the font once per character. Not proven.
- Tool: `tools/render_font.py "text" out.png`. Output images go in `work/render/`
  (git-ignored, derived from game data).
- Method of discovery: statistical block scan, then visual inspection of 16-bit
  rows, blank-row spacing (30-byte period), then rendering.
- Not known: purpose of the 16-byte header; whether the font includes
  half-width Latin letters or digits (the name "NUMBER" hints at digits);
  where the index offset 33 comes from (probably symbols placed before the
  first Big5 block).

### State files: what the game writes
- At new game start the game reads `__1.AL1`, `__1.AL2` and `MAN_INFO.ALL`,
  and creates `__1.NOW` and `MAN_INFO.NOW`. Hypothesis (medium): `.ALL/.AL1/.AL2`
  are initial-state templates and `.NOW` is the current working state. Sizes do
  not simply add up (`__1.AL1` 12,121 + `__1.AL2` 14,423 = 26,544, but the
  shipped `__1.NOW` is 92,750 bytes), so the relationship is unknown.
- After the session only `__1.NOW` differed from the original; `MAN_INFO.NOW`
  was recreated byte-identical. No `SAVEnn` files were written (the operator
  did not save).
- The shipped `.NOW` files are therefore someone's mid-game state, not part of
  a clean install (consistent with earlier provenance notes).

### Hypothesis update: TV.EXE may be the slideshow
`TV.EXE` was **never executed** in this run (the exec log shows only `mark.EXE`,
`walk0.sl`, `walk.sl`). The operator pressed Space at once, skipping the
slideshow. `MARK` names `tv.exe`, and `TV` names `mark.exe`, so TV may be the
intro slideshow and the intro's "TV static" effect may be part of it. Also, the
`STORYn_V` files were not opened, so their purpose (voice-over?) is untested.
**Next trace run: do not press Space; let the slideshow play.** Predictions:
`EXEC tv.exe` appears and `STORY*` files are opened.

### Open questions (updated)
1. Does the slideshow run through `TV.EXE`, and which `STORY*` files feed it?
2. What do `GMAIN.LCZ` (opened at 11.19 s) and `TITLE1.LCZ` contain? Guess: the
   main-menu and title images.
3. How are overlays called (far calls into a loaded segment)? Needs the code
   analysis of MARK and of `WALK0.SL`/`WALK.SL`.
4. Why is `WALK0.SL` opened again at 16.79 s, just before `WALK.SL` loads?
5. What is the "486" display? Still unknown.

## 2026-10-14: Second traced run (full slideshow, load game) and the voice files

Method: `tools/run-original.sh --trace`, operator did not skip the slideshow,
then chose the second menu option (Load) and loaded the second slot. Log:
`work/logs/run2-full-slideshow-load.log` (the first run is
`run1-skipped-slideshow.log`). Remember: log times are emulated cycles / 1e7,
not wall-clock; order is reliable, absolute time is not.

### CORRECTION: the slideshow is NOT TV.EXE
The earlier hypothesis that `TV.EXE` is the slideshow is **wrong**. `TV.EXE`
did not run in this run either. The slideshow is the overlay `STORY2.SL`:
`EXEC story2.sl 3` appears at 12.63 s, then the `STORYn` files are loaded for
the seven slides. The role of `TV.EXE` is still unknown (it was never seen
running in two sessions). The names `WALK`, `CAST`, `STORY2`, `ENDING` for the
`.SL` programs are therefore only partly right: `STORY2.SL` = the intro
slideshow. Confidence: high.

### Program flow observed (log order)
1. `PLAY.BAT` -> `mark.EXE` (type 0, run as a program). Opens `RPG.LCP`,
   `ANI.LCP`, `MUC.ALL`, sets video mode 13h, opens `TITLE1.LCZ`. This is the
   title screen.
2. After about 11 log-seconds on the title screen (run 2: 12.63 s; run 1 with a
   Space press: 11.12 s) the next step starts:
   - not skipped: `EXEC story2.sl 3` (overlay) -> the slideshow;
   - skipped with Space: `EXEC walk0.sl 3` (overlay) -> the main menu.
3. Slideshow: seven slides, see table below. At the end,
   **`EXEC mark.exe 3`**: `MARK.EXE` is loaded again **as an overlay** (type 3)
   by the slideshow, and the title sequence runs again (`RPG.LCP`, `ANI.LCP`,
   `MUC.ALL`, mode 13h, `TITLE1.LCZ`). So `MARK.EXE` is used both as the
   program and as a re-enterable code block. (How control returns is not
   analysed yet.)
4. Main menu (`EXEC walk0.sl 3`). `GMAIN.LCZ` is opened (guess: the menu
   picture).
5. Load: the game opens `SAVE00.000` through `SAVE10.000` (all 11 slot headers,
   shown as the slot list). Choosing slot 01 (the second file): opens
   `SAVE01.000`, creates `MAN_INFO.NOW`, opens `SAVE01.001` and `SAVE01.002`,
   creates `__1.NOW`. Then `EXEC walk.sl 3` and the same asset-loading burst
   as New Game (`MES_TXT.LCC`, `NUMBER.LCZ` x28, `SND_EF.1`, `MUC.ALL`,
   `RPG.LCP`, `MAN_INFO.NOW`, `__1.NOW`, `TAB.TAB`, `__2.ALL`, `GTAB.ALL`,
   `M_M.ALL`).

### The seven slides (operator: 7 slides, each animates, darkens, then voice + text)
| Slide | Files opened (log order) | Voice clips | Notes |
|-------|-------------------------|-------------|-------|
| 0 | `STORY0.LCZ`, `STORY0_T.LCZ`, `STORY0_1.LCZ` | none | no `_V` file exists for slide 0 |
| 1 | `STORY1.LCZ`, `STORY1_2.LCZ`, `STORY1_T.LCZ`, `TAB.TAB`, `STORY1_V` x8 | 8 | |
| 2 | `STORY2.LCZ`, `STORY2_1.LCZ`, `STORY2_T.LCZ`, `TAB.TAB`, `STORY2_V` x3 | 3 | |
| 3 | `STORY3.LCZ`, `STORY3_1.LCZ`, `STORY3_T.LCZ`, `TAB.TAB`, `STORY3_V` x10 | 10 | |
| 4 | `STORY4.LCZ`, `STORY4_2/3/4.LCZ`, `STORY4_T.LCZ`, `TAB.TAB`, `STORY4_V` x5 | 5 | |
| 5 | `STORY5_1.LCZ`, `STORY5_T.LCZ`, `TAB.TAB`, `STORY5_V` x8 | 8 | no `STORY5.LCZ` exists |
| 6 | `STORY6.LCZ`, `STORY6_1.LCZ`, `STORY6_T.LCZ`, `TAB.TAB`, `STORY6_V` x11 | 11 | |

Interpretation (hypotheses, medium confidence): `STORYn.LCZ` and the numbered
variants are the picture and its animation frames; `STORYn_T.LCZ` is the
subtitle text for that slide; `TAB.TAB` is loaded before the voice; the `_V`
file holds that slide's voice lines. The varied numbering (`_2`, `_3`, `_4`, no
`_1` for slide 4; no base file for slide 5) shows the files are picked
per slide, not by a fixed scheme.

### Voice files are decoded (STORYn_V)
- Each `STORYn_V` is several **Creative Voice File (VOC)** clips concatenated.
  Confirmed: every file starts with the text `Creative Voice File`, and the
  number of VOC signatures in each file equals the number of times the game
  opened it: 8, 3, 10, 5, 8, 11 = **45 clips**. All 45 match exactly.
- Each clip: 26-byte VOC header, one sound-data block (type 1), terminator.
  8-bit unsigned PCM, mono, **13,158 Hz** for all 45 clips. Total about 157.5 s.
- Tool: `tools/voc_extract.py original_bin/STORY1_V work/audio/STORY1` writes
  WAV files. Decoding is verified structurally (rates, block layout), but I
  have not **listened** to the output. The human operator should confirm that
  the clips are the voice-over they heard (see Next).
- Each game open of a `_V` file corresponds to one line spoken; the operator
  heard "one line at a time with a pause at the end".
- Not known: which clip matches which subtitle line (`TAB.TAB`? or the `_T`
  file?).

### Subtitle files STORYn_T.LCZ: not readable text
- Tried to decode all seven as Big5: no readable Chinese. So the text is
  compressed, bitmap, or indexed into the font. Not solved.
- First bytes differ per file (`09 d6 0b`, `0b e9 0f`, `0d ac 05`, ...), so
  there is some per-file header. Unknown.

### Save files and working-state files
- Slot list = the 11 small `.000` files are read first (about 100-230 bytes
  each). The chosen slot's `.001` (about 18 KB) and `.002` (about 8.5 KB) are read
  only on selection. So `.000` is a small summary or header for the slot.
- On load, the game **creates** `MAN_INFO.NOW` right after reading the slot's
  `.000`, and `__1.NOW` right after reading `.001` and `.002`. Both then
  differ from the shipped files in the play copy (sizes unchanged:
  `MAN_INFO.NOW` 504, `__1.NOW` 92,750).
  Interpretation (medium): `.000` -> `MAN_INFO.NOW` (character info),
  `.001+.002` -> `__1.NOW` (world state). The expansion from about 26.5 KB to
  92,750 bytes suggests the save is compressed or stored sparsely.
- Size observation: `SAVE01` and `SAVE04` have `.001 + .002` = 26,544 bytes,
  exactly the size of `__1.AL1 + __1.AL2` (26,544), but different content;
  other slots are 26,550 and 26,534 bytes. Likely variable-length records.
  Not explained.
- `.000` files show a repeating record pattern: a `04` byte, then a few
  numbers, and ids `c8 c9 ca cb cc` (200-204, five consecutive ids), followed
  by 16-bit values such as `0x0898` (2200), `0x07d0` (2000), `0x0258` (600).
  The values look like stats of five characters. This is only an observation
  from hexdumps of `SAVE00.000`, `SAVE01.000` and `SAVE07.000`; the meaning is a
  guess.
- Slot numbering: the operator's "second file" is `SAVE01` (slots start at 00).

### Open questions (updated)
1. What is `TV.EXE` for? Never run in two sessions.
2. How do `STORY2.SL` and `MARK.EXE` hand over control (overlay loading, then a
   far call, then?). Needs code analysis.
3. How are subtitles stored (`STORYn_T.LCZ`)? And how are voice lines tied to
   text lines (`TAB.TAB`)?
4. What is `.LCZ`? Over 37 files; `NUMBER.LCZ` is an uncompressed font, so the
   extension does not mean "compressed". Compare more files.
5. Save format: `.000` header, `.001`, `.002` internals.
6. Why does the title wait about 11 log-seconds before continuing? A timer, or
   a wait for a key / the end of the music?

### Confirmation by listening (operator)
The operator played `work/audio/STORY1/STORY1_V_01.wav` and confirmed it is
the very first voice line heard on the **second** slide (the first slide has no
voice). This matches the file mapping (slide 0 = no `_V` file; slide 1 =
`STORY1_V`). The VOC decoding and the clip order are therefore verified.

## 2026-10-14: Picture and subtitle formats decoded

Method: Python byte analysis of the `.LCZ` files, then rendering with
`tools/lcz_decode.py` and viewing the output.

### What was found
1. The `.LCZ` files are chunked containers with run-length encoding
   (`esc, count, value`). Full spec in `formats.md`. 33 of 36 parse cleanly.
   Evidence: the start bytes of `TITLE1.LCZ` (`01 ff 00 01 ff 00 01 8f 00 ...`)
   look like runs; decoding every file with the rule produced exactly 64,000
   bytes for 8 chunks (a 320x200 screen).
2. Pictures are 320x200 with an 8-bit palette index. Rendered `GMAIN`,
   `STORY1` and `STORY3` in colour and checked them by eye (and `GMAIN` against
   the operator's earlier in-game screenshot of the interface).
3. **Palettes start at offset 0** of `RPG.LCP` (interface) and `ANI.LCP`
   (slideshow). I viewed three candidates for `GMAIN.LCZ`: `RPG.LCP` at offset 0
   (correct, matches the screenshot), `RPG.LCP` at offset 4 (wrong: green) and
   `ANI.LCP` at offset 3 (wrong: yellow/pink). Other candidates were rendered
   but not viewed.
   **CORRECTION:** the earlier notes said `RPG.LCP` has a "5-byte header" and
   `ANI.LCP` a "3-byte header" before the palette. Both are wrong. The
   "header" bytes I saw were the palette's first entries.
4. **Subtitles are bitmaps** (`STORYn_T.LCZ`): decoded length is a multiple of
   600 (one 320x15 1-bit line); the number of lines equals the number of
   voice clips in `STORYn_V` for slides 1-6. The first lines of `STORY1_T`
   rendered as readable Chinese (the opening narration; see `formats.md`).
5. **CORRECTION to the earlier Big5 attempt:** the subtitle files did not
   decode as Big5 because the text is stored as pre-drawn bitmaps, not as text.
   The trace shows the font file is not opened during the slideshow.

### What I got wrong along the way (kept for the record)
- First tried reading the subtitle lines as 20 consecutive font glyphs (16x15,
  30 bytes each, same layout as `NUMBER.LCZ`). That produced noise. The 1-bit
  320x15 row layout is correct.
- The idea that every chunk starts with a width/height header is true for only
  13 of 58 chunks (for example `TITLE1.LCZ` chunk 0 = 67 x 200). Not a general
  rule.

### Open questions (updated)
1. How are the 37 chunks that match neither the 64,000-byte rule nor the
   `w*h+4` rule laid out? Animation frames? Try rendering them as sub-images with
   various widths and compare against screenshots.
2. `SPIC.LCZ` (91 KB) and `STORY5_1.LCZ`: why does a chunk size field look wrong?
3. (answered 2026-10-15) The 21-second sound in `STORY0_1.LCZ` is slide 0's audio, a plain VOC
   with 4-bit ADPCM; see `formats.md`. Still open: is it music or an effect (needs a listener)?
4. Does `TITLE1.LCZ`'s 7-chunk layout form the title screen? What does the
   67-pixel-wide chunk belong to?
5. Does each slide's picture combine a base chunk with animated overlays
   (consistent with "hair animates, then it darkens")?

## 2026-10-14: Remaining picture chunks (frame stacks, sprites and masks)

Method: `tools/lcz_sheet.py` contact sheets, a row-similarity width estimator
(scratch scripts in `work/scratch/`, not kept), and viewing every rendering.

### Results
1. **Sprites with a size header** are 16 chunks in total. My first test
   (decoded length = w*h+4) caught 13 of them, the ones holding exactly one
   sprite. Reading the chunk as a sequence of `[w:2][h:2][pixels]` records
   explains 3 more that hold several sprites back to back: `ENDING1` chunk 0,
   `ENDING2` chunk 1 and `ENDING3` chunk 0.
2. **The remaining chunks are headerless.** They are one picture or a stack of
   equal-size frames, with no size in the file. The frame size comes from the
   program. I found sizes by testing divisors of the data length and looking at
   the output; the table is in `formats.md`.
3. **The slideshow animates by drawing small frame stacks over a base picture.**
   `STORY2` is a base landscape plus four hair frames; `STORY4_2` is the woman's
   eyes closing over 7 frames (tears appear); `STORY6_1` is three frames of
   hair in the wind. That matches the operator's description of "parts of
   images like hair animated, then it darkens".
4. **The endings composite face sprites onto a still picture.** `ENDING4`
   draws a woman whose face area is empty (black) in the base picture, plus
   32x26 face frames, so the mouth moves independently. The dress-colour change
   is a sequence of full 320x165 pictures.
5. **Title logo masks.** `TITLE1.LCZ` chunks 2-5 are two-valued (`0xFE`/`0x00`)
   masks of the title characters. `0xFE` is probably a transparent colour key
   (the game draws strokes with some colour effect). Likely, not proven.

### Corrections and lessons
- My width estimator was misleading on flat pictures (it said `CAST1` is
  16 pixels wide; it is 320x100). The earlier `w*h+4` rule was too narrow; the
  true header form is a sequence of sprite records.
- `STORY3_1.LCZ` chunk 0 (a 168x143 sprite with header) was not looked at.

### Open questions (updated)
1. Where are the frame sizes in the code? Look for the constants (79, 99,
   155, 125, 123, 96, 97, 84, 189, 160, 104, 94 ...) in `STORY2.SL`, `WALK.SL`,
   `MARK.EXE`.
2. `BOAT.LCZ` chunk 2, `CAST2.LCZ`, `SPIC.LCZ`, `STORY3_1.LCZ` chunk 1,
   `ENDING1T.LCZ`, `ENDING4T.LCZ`: unresolved.
3. Which palette does each file use? `ANI.LCP` fits `STORY*`, `ENDING4`, `ENDING1`
   and `CAST1`; `BOAT` looks wrong with it. Where are the other palettes (the
   rest of `RPG.LCP` and `ANI.LCP`, or inside the code)?
4. `ENDING4` chunks 4-6 should be checked by eye.

## 2026-10-14: Code analysis begins (MARK.EXE graphics, files, mouse)

Method: byte scan for `int` instructions and VGA port numbers
(`work/scratch/ints.py`), then Ghidra decompilation of the hits and
disassembly with `tools/dis16.py` (Capstone) where Ghidra had no function.
Full details and addresses are in `code.md`.

### Findings
1. **The game uses VGA "Mode X"** (unchained 320x200x256, four planes), not the
   plain linear mode 13h. `0x118c0` calls `int 10h` AX=0013h and then writes
   sequencer register 4 := 06 (chain-4 off), CRTC 0x14 := 00 and CRTC 0x17 :=
   E3. The earlier trace entry "Set Video Mode 13" is consistent: the BIOS call
   is mode 13h, then the program unchains it. Evidence: disassembly, seen.
2. **Page flipping and fast copies are used:** `0x110b9` sets the CRTC start
   address (regs 0x0C/0x0D); `0x110fa` copies 16,000 bytes with VGA write mode 1
   (latch copy, all planes at once).
3. **Sprites are blitted one plane at a time.** `0x10e1a` reads `[w][h]` from
   the sprite, then for map masks 1, 2, 4, 8 does a `rep movsw` per row. So the
   sprite data in memory is grouped by plane. The `.LCZ` sprite header
   `[w:2][h:2]` matches. **Not yet checked:** whether the data in the `.LCZ`
   file is already plane-grouped, or is reordered after loading.
4. **Palette upload** `0x11f72`: waits for retrace, blanks the display via port
   0x3D8, writes `count*3` bytes to the DAC. Takes 6-bit RGB, matching the
   palette format found in `RPG.LCP` and `ANI.LCP`.
5. **File access** is through five tiny DOS wrappers (open, close, read, seek,
   write) at `0x11758`-`0x117f9`, each returning 0 on success or 5 on carry.
6. **Mouse** through `int 33h` in `0x10d2c` and `0x10d9f` (pointer clamped to
   x <= 314, y <= 194).

### Corrections and lessons
- I first called the mode "Mode X" from the plane-by-plane writes alone, before
  seeing the setup code. I then found the setup (`0x118c0`) and it matched. My
  byte-pattern search for the mode set had missed it (it assumed
  `mov ax,0604h`; the code used separate `out dx, al` writes), which shows a
  pattern search is not proof of absence.
- Ghidra defined no function for part of the graphics code. Absence from the
  function list does not mean absence of code.

### Open questions (updated)
1. Do the pixel bytes in a decoded `.LCZ` sprite already sit grouped by plane?
   (Compare against what the blitter reads.)
2. Where does the code call the overlay loader, and where are the filename tables
   used? This will give frame sizes and palette use.
3. Who calls `enter_mode_x`?

## 2026-10-14: How MARK starts the other programs; what TV.EXE does

Method: string dump of the unpacked images (`tools/mz_strings.py`), byte scan
for code that loads each string's data-segment offset, disassembly with
`tools/dis16.py`, and the two saved traces. Full detail with addresses is in
`code.md` ("Program chaining").

### Findings
1. **There is no filename table.** The names are plain string literals loaded
   at the point of use inside `MARK`'s `main`. (The question as first phrased
   assumed a table.) Confidence: high (all 11 uses located; none go through an
   index).
2. **`.SL` files are chained programs, not overlays.** `main` ends with
   `execl("walk0.sl")` if a flag is 1, else `execl("story2.sl")`; the C runtime
   reaches `mov ax,4B03h` (`0x13990`) and jumps into the new program. The trace
   shows `Execute story2.sl 3`. Each program also names the one it returns to
   (`STORY2.SL` and `WALK.SL` name `mark.exe`; `CAST.SL` names `walk0.sl`).
   Confidence: medium-high. The exact trampoline at `ljmp [0x170]` was not traced.
3. **Title screen decides intro vs menu.** A key press (Space `0x39` or Enter
   `0x1c`) or a mouse click during the title wait sets a flag and gives the
   menu (`walk0.sl`); otherwise the intro slideshow (`story2.sl`). Matches both
   traced runs. Confidence: high for the branch, medium for which exact event
   sets the flag in the mouse path.
4. **`TV.EXE` is a speed test that runs once.** `MARK` reads byte 0 of
   `RPG.LCP`; if it is above 10 it runs `TV.EXE`. `TV` shows noise, times
   itself with the BIOS tick counter, maps the result to a class 0..4, writes
   that byte back to the start of `RPG.LCP`, and starts `MARK.EXE` again.
   Our `RPG.LCP` has `0x02` there, which is why `TV` has never run in a trace.
   Confidence: high for the read/compare/exec, medium-high for the write-back
   (wrapper identities inferred from call shape).
5. **The speed class is stored inside the palette.** Byte 0 of `RPG.LCP` is
   also colour 0's red value, so values 0..4 are practically invisible. This
   is an inference; the code itself only shows "write 1 byte at offset 0".
   Confidence: medium.

### Corrections to earlier entries
- CORRECTION (2026-10-14): earlier entries and `formats.md`/`files.md` called
  the `.SL` files "overlays" loaded with "exec AL=3". The AL=3 part is correct as
  observed, but the word overlay suggested call-and-return. They replace the
  calling program. Wording fixed in `formats.md` and `files.md`; the old
  entries above stay as written.
- CORRECTION (2026-10-14): `files.md` said `TV.EXE` was "never seen running;
  role unknown". The role is now read from the code (finding 4). It is still
  true that no trace has seen it run.

### Open questions (new)
1. **Who reads the speed class later, and what does it change?** `WALK0.SL`
   names `rpg.lcp` five times and `WALK.SL` fifteen times; look there for a
   one-byte read at offset 0 and where the value goes (delay loops?).
2. Is the measured stretch in `TV` CPU-bound? If yes, a fast modern CPU would
   give a tiny tick count and class 0, the same class as "very slow". This is
   a hypothesis for the crash-on-fast-machines warning; it is not verified.
3. How does `ENDING.SL` exit? It names only `ani.lcp`.
4. What are the two calls `0x112:0x12a` and `0x112:0x152` at the end of the
   title loop (guessed: sound shutdown)?

## 2026-10-14: Where the speed class is used, and the "486" hunch

Method: `strrefs2.py` (string offsets -> code), `farptr.py` (accesses to a
stack-local far pointer), `dis16.py` disassembly, `ticks.py`, `cpudetect.py`,
and a Python search of the save files. Details in `code.md`, last section.

### Findings
1. **`WALK.SL` reads the speed class at start-up and uses it as a frame
   divider.** It reads `0x300` bytes of `RPG.LCP` (the palette) and copies the
   first byte into a local (`0x109d3`). That local is compared against loop
   counters in six places; at `0x172bc` it decides how many main-loop passes
   happen between steps of a 4-frame animation. Confidence: high for the read
   and the comparisons; medium for "frame divider"; the effect of the
   `class < 2` branches is not understood.
2. **Direction of the scale:** with `TV`'s table, a slower machine gets a
   smaller class and a faster one a larger class (up to 4), so the game
   slows its animation on faster machines. Confidence: medium (inferred from two
   independent code reads).
3. **Possible cause of the fast-CPU problem (hypothesis):** `TV` gives the
   class 0 to very *fast* results (`v <= 65` ticks) as well as to very slow
   ones. Not tested.
4. **No CPU-detection code was found** in any of the seven programs, and none
   contains the text 486/386/286/Pentium/CPU/MHz. Confidence: medium (a byte
   scan only; false hits were checked by hand and rejected).
5. **The value 486 is a field in the game data.** `e6 01` is in `__1.AL2` and
   in all seven `SAVEnn.001` files inside an identical byte pattern. Confidence:
   high that the field exists; low-to-medium that it is what the operator saw.

### Operator hunch: "486 = CPU class"
- Not supported by what was found. My emulator is set to `cputype=486`, so a
  genuine CPU readout would have shown 486 in these runs too, but no code that
  reads the CPU exists in the programs, and 486 is present in the new-game
  data. This is evidence against, not a refutation: the decisive test is to
  edit the field in a copy of a save and watch the display.
- Worth keeping the hunch alive in one form: if the display were driven by
  the speed class, a value of 2 (our `RPG.LCP`) would not give 486 without a
  further formula. Nothing in the code read so far computes such a number.

### Corrections / lessons
- CORRECTION: my first byte scan reported "CPUID" and "pushfd" hits in all
  seven programs. All the ones inspected were false positives (the bytes
  `0F A2` and `66 9C` inside ordinary instructions). A byte-pattern hit is a lead,
  not a finding.
- My `strrefs2.py` first printed negative offsets (it subtracted the
  data-segment base twice). Fixed before any result was used.

### Open questions (updated)
1. Test the 486 field: change `e6 01` in a copy of a save in `work/dosroot/3K`,
   load it, and see if the on-screen number changes. (Needs the operator and
   the mouse; the original files stay untouched.)
2. Which function draws the top-right number, and which variable does it read?
3. What do the `class < 2` branches (`0x11336`, `0x14ac1`) do?
4. Is `TV`'s timed stretch CPU-bound? Is class 0 treated as "unthrottled"?
5. What are the other `rpg.lcp` uses in `WALK.SL` (15 string uses; palette,
   then seeks to offsets such as `0x98d8`, `0xae37`, `0x8691`, `0x643b`)? They
   suggest `RPG.LCP` is a bundle of UI graphics after the palette (120,013 bytes).

## 2026-10-14: CORRECTION to the "Mode X" finding; ANI.LCP is used as a message byte

Method: `work/scratch/modex_callers.py` and `planar_callers.py` (far-call scan
over every program), then `callsum.py` over `MARK`'s `main`.

### CORRECTION: the game runs in plain mode 13h, not Mode X
- The earlier entry "The game uses VGA Mode X" (2026-10-14, graphics layer) is
  **wrong for how the game runs**. The routines I found are real, but **none of
  the four routines has any far-call site** in `MARK.EXE`, `STORY2.SL`,
  `WALK0.SL` or `WALK.SL` (`enter_mode_x`, `blit_planar`, `copy_page`,
  `set_screen_start`). The same unused library appears in every program (all
  seven contain `enter_mode_x`).
- What the code does instead: `set_video_mode(0x13)` (`push 0x13` then the
  `int 10h` wrapper at `0x118b3`) in `MARK`, `TV`, `STORY2`, `ENDING`; and it
  sets up a far pointer to `A000:0000` (`mov [bp-0x1a], 0xa000`) and treats it as
  one linear 64,000-byte screen.
- Why I got it wrong: I read the routine bodies and inferred their use from
  what they do. I never checked that anything called them. Lesson: before
  describing a routine as part of the design, find a caller.
- Limit of the check: it only sees direct far calls. A call through a function
  pointer table would not appear. No such table was seen.
- Consequence: the port needs a plain 8-bit 320x200 framebuffer, no plane
  conversion. The earlier worry about whether `.LCZ` sprite data is grouped by
  plane is moot: the real drawing routines are different ones (see below).

### MARK writes a message byte into ANI.LCP (seen)
- `main` (`0x10152`..`0x101df`) reads `ani.lcp`'s first `0x600` bytes, saves
  byte `0x2ff` (the blue value of palette colour 255, in the first palette),
  forces that byte to `0x3f` **in memory**, opens `ani.lcp` for writing and
  writes `0x300` bytes back, closes, then branches on the saved value:
  `0x3e` takes the path at `0x101ed` (reads a block from `rpg.lcp` at `0x340`);
  anything else jumps to `0x10559`, where the value is tested again and, if it
  is not `0x3f`, `main` returns to the C runtime (the program quits).
- In the shipped file byte `0x2ff` is `0x3f`, so this writes the same bytes back
  and nothing changes (confirmed: the play copy of `ANI.LCP` is identical to the
  original after two runs).
- `WALK.SL` opens `ani.lcp` with mode 1 (write) at `0x12f02` and writes at
  `0x2ff`. The other programs open it for reading.
- Interpretation (inferred, medium): the chained programs cannot pass
  arguments, so they leave a one-byte message in `ANI.LCP` byte `0x2ff` for the
  next program: `0x3f` normal, `0x3e` a second state. The exact meanings are not
  known. A native port can use an ordinary variable.

### MARK start-up order, from the call summary (seen)
`mouse init` -> read `ani.lcp` (2 palettes, `0x600`) and write the message byte
-> read `0x100` bytes of `rpg.lcp` at `0x340` (a 256-byte table 0,1,2,...) ->
read `0x5b8` bytes of `title1.lcz` at `0x3e73` -> decompress (`0x10ce5`) ->
allocate and clear buffers -> load `muc.all` blocks (`0x7b1` bytes at
`0xd76b` from the end, `0x49a2` bytes at `0x7137` from the end) for the music
driver -> `set_video_mode(0x13)` -> `set_palette` -> read the rest of
`title1.lcz` and decompress several chunks -> main loop. The decompressor at
`0x10ce5` is exactly the RLE in `tools/lcz_decode.py` (header: escape byte then
a 16-bit total size; escape, count, value).

## 2026-10-14: Native port started, milestone 1 done

Method: C99 sources in `src/`, `make test`, a PPM comparison against
`tools/lcz_decode.py`. Details and evidence in `port.md`.

- The C LCZ decoder gives the same chunk counts and sizes as the Python one for
  `title1.lcz`, `story1_2.lcz` and `gmain.lcz`, and the rendered `gmain.lcz` with the
  `rpg.lcp` palette is byte-identical to the Python render. Confidence: high.
- `MARK`'s `main` loads `title1.lcz` as: `0x3118` bytes (chunks 0-1), then `0xd5b`
  bytes (chunks 2-5), then chunk 6 after a seek to `0x3e73`; the sizes equal the
  chunk boundaries in the file exactly. Confidence: high.
- `MARK` start-up order is in the previous entry.
- Lesson recorded: my first test run crashed because I wrote the wrong size byte in
  a hand-made test chunk (13 instead of 12). The decoder correctly refused it; the
  test then dereferenced a null pointer. Test fixed to stop on failure.

## 2026-10-14: Title screen reproduced in C and verified against the original

Method: `tools/dosemu.py` (a small DOS emulator on the Unicorn CPU library) runs the
unpacked `MARK.EXE` machine code; `build/3k --step-dump` runs the C title; 
`tools/compare_frames.py` compares them. Details: `port.md` (milestone 2) and `code.md`
("Title screen").

### Findings
1. **The C title scene matches the original exactly.** Zero differing pixels, palette
   bytes and indices at all 14 sampled passes without input (0, 1, 2, 10, 63, 64, 65, 100,
   200, 300, 600, 698, 699, 700) and at 8 passes with Space held from pass 100 (100..162).
   Negative controls: neighbouring passes differ by 1,083 to 2,899 pixels, and one flipped
   pixel is reported. Confidence: high for these passes. Not compared: passes between
   the samples, the mouse path, and anything after pass 700.
2. **Exit decisions match:** no key -> `story2.sl` after 762 passes (699 + 63 fade-out);
   Space at pass 100 -> `walk0.sl` after 165; Space at pass 10 -> `walk0.sl` after 22.
   The same three numbers come from the C port. Confidence: high.
3. **The title length is counted in loop passes, not time** (see `code.md`). Speed depends
   on the machine. Seen in code (a pass counter compared against 0x2bc, no wait in the
   loop after the fade-in); real-time effect not measured.
4. **The C runtime start-up includes a CPU-speed calibration loop** (`0x1577f`..`0x157db`)
   whose result is kept at DS:`0x2e`/`0x30` in every program. Where it is used is not yet
   known.
5. **The music driver probes the AdLib/OPL chip** at start-up (port `0x388`).

### Corrections
- CORRECTION (formats.md / earlier entry "Title logo masks"): in the title masks `0xFE` is
  the visible fill and `0` is the transparent part, not the other way round. The overlay
  routine copies non-zero source bytes. Colour 254 is black in the title palette, so
  the masks appear as black windows with the texture showing through the zeros.
- CORRECTION (my reading of `TITLE1` chunks 2-5 after the contact sheet): they are
  headerless images, 67x67 for chunks 2-4 and 112x24 for chunk 5, not sprites with
  `[w][h]` headers and not "frame overlays".
- CORRECTION (own work): the first emulator run produced streaky wrong colours. Cause was my
  emulator, not the game: DOS function 4Ah (resize memory block) was a no-op, so the
  program's palette buffer was overwritten by a later allocation. Fixed by implementing the
  call. A first attempt to install the emulator library also failed because `pkg-config` and
  `cmake` were missing (installed with Homebrew: `cmake`, `pkgconf`).
- LESSON: my first version of `gfx_rotate_rows` took the "spare byte" as an argument. It is
  state that the original overwrites each call. The assembly-model test would not have caught
  it with one call; two successive calls did. Fixed.
- LESSON: I almost took a block of `main` (`0x1040a`) to be part of the title path. The
  oracle's file trace showed it is skipped (no read of `rpg.lcp` at `0x340`).

## 2026-10-15: milestone 3 started, STORY2.SL read and run in the oracle

- **METHOD:** Ghidra pseudo-code of `STORY2.SL` `main` (`0x10000`) for structure; new
  `tools/calls16.py` for arguments; `tools/dosemu.py` run on the unpacked program. Details
  are in `code.md`, section "STORY2.SL".
- **SEEN:** `dosemu.py` runs `STORY2.SL.unpacked.exe` without changes. It sets mode 0x13,
  and its unhandled-request list is a Sound Blaster DSP probe (ports 0x226-0x22e, including
  8,192 reads of 0x22e) and an AdLib probe (0x388/0x389). Sound is therefore off in the
  oracle, and the picture/subtitle path runs regardless.
- **SEEN (rendered, looked at):** slide 0 is a black starfield with coloured stars and one
  Chinese subtitle line at the bottom, with different lines at different times (four distinct
  lines in the first four samples: 在遙遠的、遙遠的；如瞳孔般深的宇宙裡， / 那是傳說中的希望世界… /
  獸人類與大自然精靈三界交合的虹奇世界。 / 就能夠得到智慧、財富、生命與力量…). Slide 1 is the
  woman cradling the wounded man, once in a dark, desaturated state and once in full colour,
  with subtitles 故事的發生，該由亞紀元 1660 年， and 邦沛帝國十一歲的英俊王子卡洛安，. Those two
  match lines 1 and 3 of `STORY1_T` as decoded earlier (first confirmation of line k
  = subtitle for clip k beyond slide 1 clip 1, for two lines of slide 1).
- **LIMITATION:** I drove that run by tapping Space every 150 ticks, and the order of the dark
  and coloured states of slide 1, and the tick numbers, are NOT the timeline of an unattended
  run (see the unattended timeline below). CORRECTION (2026-10-15, later): I first wrote that
  a Space tap "changes what is shown" on slide 0 as well. Slide 0's loop (`0x10590`-`0x10a56`)
  contains no keyboard read at all, so the taps cannot have affected it; the four different
  subtitle lines in the first four samples are just elapsed time. Later slides do read the
  keyboard (`0x13a44`, scan code `0x39`).
  claim "dark state = the same picture with a dimmed palette" is inferred from the two
  pictures having the same composition; the palette writes were not checked.
- **CORRECTION (my own draft, same day):** I first wrote the `muc.all` seek offset as
  `0x7137`. The seek wrapper takes the high word as the fourth argument, so the offset is
  `0x27137`; checked against the 188,188-byte file size.
- **UNKNOWN / next:** pairing of each slide's phase with its picture and voice files
  (needs the blit and sound routines read), the real timing of an unattended run, the voice
  file names (not in the `DS:` table), and what `0x12fb2` (flag `[5]`) probes.

## 2026-10-15: unattended timeline of STORY2.SL (oracle, no sound hardware)

- **METHOD:** `tools/dosemu.py --log-files` (new) prints each successful file open with the
  emulated BIOS tick. The program was run with no key input at four `--instr-per-tick`
  values (125k, 250k, 500k, 1M). The emulator's clock advances only with executed
  instructions, so an event at tick t is modelled as `t = a + W / ipt`: `a` is time spent
  waiting on the clock (real time at 18.2065 ticks/s) and `W` the instructions of CPU-bound
  work. `tools/fit_timeline.py` (new) fits both per event. Residuals stay under about 10
  ticks and grow steadily with event index (quantisation), so the two-part model holds.
  Fit table: `work/scratch/story2_fit.txt` (not tracked).
- **SEEN (code, `0x12a02`, `0x12800`-`0x12847`):** `0x12a02` returns the BIOS tick word at
  `0040:006C`. Without sound, a subtitle/voice line ends when the tick counter has advanced by
  `0x4a` (74 ticks, 4.06 s); the loop also ends if Space (`0x39`) is read from port `0x60`.
  Slide 0 uses `0x38` (56 ticks, 3.08 s) per line. The blits `0x1293e` and `0x138b4` have no
  clock or retrace wait.
- **SEEN (fit):** consecutive voice-file opens are 73.6 to 74.1 clock ticks apart (matches
  the 74-tick wait), and slide 0 waits 392 clock ticks = 7 lines x 56. The numbers of
  `story<k>_v` opens per slide are 8, 3, 10, 5, 8, 11 for slides 1-6, equal to the clip counts
  per file found in the voice-file analysis.
- **Clock-bound part per slide** (interval between first file open of a slide and the next):
  slide 0 about 389 ticks (21.4 s), slide 1 588 (32.3 s), slide 2 221 (12.1 s), slide 3 736
  (40.4 s), slide 4 368 (20.2 s), slide 5 589 (32.4 s), slide 6 809 (44.4 s); the final
  `mark.exe` exec is about 3,700 ticks (203 s) after start. This is the schedule **without
  sound hardware**, where a voice line is replaced by a fixed 74-tick wait.
- **INFERRED (not tested):** with a sound card present, the same code starts the voice clip
  (`0x156b5`, `0x15708`) and loops until a sound-busy flag at `DS:0x19a` clears
  (`0x127e4`: `cmp word ptr [0x19a], 0`), so a line then lasts as long as its clip plus the
  loop's overhead. I did not run it with a sound card; the earlier DOSBox-X observation of a
  pause after each line is consistent with this but is not a measurement.
- **CPU-BOUND PARTS (speed dependent in the original):** every slide also has a part that
  takes `W` instructions (for example 64 M for slide 0's set-up, 142 M for slide 1) made of
  animation passes and busy re-blits (`0x138b4` called 20 or 35 times in a row at
  `0x1274a`/`0x128fb`). On a faster CPU these shorten. The port cannot copy them as time
  because the original's speed was never fixed; the emulator's instruction count is also not
  a CPU-time measurement (how it counts `rep movs` was not checked), so I have **not**
  converted W into seconds.
- **LIMITATION:** only one input sequence (no keys) was run. Timing with Space presses was
  not modelled.
- **UNKNOWN:** how long the animation phases were meant to last on the target hardware;
  the length of each voice clip is known (decoded earlier) but its interaction with the loop
  overhead is not.

## 2026-10-15: slide 0 reproduced in C and compared with the original

- **METHOD:** the scene `src/intro0.c` was written from the disassembly of STORY2.SL
  `0x10590`..`0x10a56` (details in `code.md`, "Slide 0, verified"). The original was run in the
  oracle (`tools/dosemu.py --loop-top 0x10590 --tick-div 3 --snap-pass N:FILE`), the C port
  with `build/3k --scene intro0 --tick-div 3 --step-dump N:FILE`. Both write screen and
  palette; `tools/compare_frames.py` compares what a viewer would see, plus indices and palette.
- **SEEN:** 197 snapshots (every 7th pass from 0 to 1400, and every pass from 1280 to 1330)
  are identical: 0 differing pixels, 0 index differences, 0 palette byte differences, in all.
  The loop ends after 1,294 passes on both sides (the oracle's pass counter says 1,294
  started; the C port reports the loop ended after pass 1,294); no snapshot exists past it on
  either side. All 197 frames are distinct from each other, 196 show subtitle ink, and every
  colour index `0xf0`..`0xfe` of the cross-fade appears.
- **NEGATIVE CONTROLS:** frames from different passes differ by 1,717 to 3,564 pixels, and
  one flipped pixel is detected (1 of 64,000).
- **UNIT TESTS:** `make test`, 0 failures. New: `gfx_rotate_rows_left` compared with a literal
  transcription of STORY2.SL `0x13b7a`..`0x13bab` over 7 sizes including the odd/even split;
  `gfx_expand_bits`, `gfx_stamp`, and the C library generator (seed 1 gives 41, 18467, 6334,
  26500, 19169, which is also what the scene reproduces). Two deliberate mutations (rotate
  copies the wrong pixel; generator constant changed by one) were each caught.
- **SANITIZERS:** the program, built with AddressSanitizer and UBSan against a no-window
  platform stub (`make build/3k_headless`), produced no report and byte-identical dumps.
- **WHAT THE COMPARISON DOES NOT COVER:** the fade-in and fade-out (the stepped run does the
  fade-in without sampling it, and the fade-out starts after the last compared pass); the
  real-time pacing (a free choice, `src/timing.h`, values marked GUESS); sound; the effect of
  a hold longer or shorter than 3 passes per tick (only the 3-per-tick model was run, so the
  56-tick hold equals 168 passes in these dumps). The oracle's clock is a model I supply, so
  these results show the logic matches, not the speed.
- **WRONG EARLIER, FIXED:** my notes said the stars "twinkle" via a palette routine. They
  drift (2 rows, 2 pixels per pass) and respawn at the bottom. I also called `0x13a8f` a byte
  copy (it copies words) and misread the `muc.all` seek offset (see above).
- **PROCESS FAILURE (not a code defect):** a sanitizer build of the full program with SDL hung
  for 1.5 hours: `libSDL2` (the sdl2-compat layer) raised a modal `NSAlert` from its
  initializer before `main` ran, and nothing was there to click it. I found this by sampling the
  stuck process and then killed it. The fix is `tests/platform_null.c` and
  `make build/3k_headless`. Cause of the dialog itself was not investigated.

## Slide 1 reproduced (milestone 3, 2026-10-15)

- **RESULT:** `src/intro1.c` (scene `build/3k --scene intro1`, or `--scene intro` for slides 0
  and 1 in a row) matches the original's machine code, run in `tools/dosemu.py`, in every frame
  compared: 39 animation frames (passes 0..30, 40, 60, 100, 150, 200, 250, 300, 319), all 112
  cross-fade passes of the eight subtitle lines, and the 14 fade-out passes plus the strip
  restore (15 states). Pixels, indices and palettes are identical. The first animation frame
  also checks both fades before the loop (palette), and the first cross-fade frame checks the
  fade to the dim palette after it.
- **METHOD:** the oracle gained `--snap-hit ADDR:N:FILE`, a snapshot at the N-th arrival at an
  address that leaves the clock alone (the subtitle waits need the clock; `--loop-top` freezes
  it). Hits: `0x10c80` (animation loop; used with `--loop-top`), `0x1250e` (cross-fade pass),
  `0x12870` (fade-out pass), `0x12928` (after them). The C side writes the same states with
  `--dump anim|sub|out:N:FILE`. `tools/compare_set.py` compares many pairs at once.
- **NEGATIVE CONTROLS:** frames from different points differ by 679 to 3,937 pixels, a pass
  apart in the cross-fade by 1,272 and 2,022 pixels, and one flipped pixel is detected. The
  same run without `--faithful-keys` differs from the reference by 2,972 pixels at pass 30
  and 3,741 at pass 319, so the keyboard quirk below is real and is what the flag controls.
- **FOUND BY THE COMPARISON:** the second cloud strip moves only on the first pass in the
  reference run. Reading the code gave "every third pass"; the cause is that the keyboard-port
  read overwrites the counter's stack slot (`code.md`, "Slide 1, verified"). On real hardware
  the result depends on the last scan code the keyboard sent, so the original's behaviour there
  was never well defined. The port's default is every third pass (the evident intent).
- **UNIT TESTS:** `make test`, 0 failures; new tests for the masked sprite blit and the
  rectangle blit. AddressSanitizer + UBSan run of the headless build: no reports, dumps
  identical to the normal build's.
- **WHAT THE COMPARISON DOES NOT COVER:** the fade to black at the end; the real-time pacing
  (35 passes per second for the animation and the subtitle passes, copied from slide 0 and
  marked GUESS in `src/timing.h`); Space handling (the oracle was run without keys); the
  non-faithful cloud pacing (checked only against the faithful one, plus the code); voice-over
  (not built: the run is silent and each line waits the 74 ticks of the no-sound path); the
  clip table in `tab.tab` (read from the code, never decoded). Only the no-key case was run.
- **MY MISTAKES ALONG THE WAY:** I first read `[bp-0x20]` as a work picture; it is the screen,
  which is why the picture is decoded straight onto it and why the patch cut from it is the
  cliff. I assumed the second palette fade target was black; it is the dim second palette of
  `ANI.LCP`. Both were corrected from the code before the C was written.
- **TOOLING NOTE:** one shell heredoc was blocked by a host safety rule that mistook it for an
  SSH-to-localhost command. I made the same edits with the file tool, not a reworded command.

## Slide 2 reproduced (milestone 3, 2026-10-15)

- **RESULT:** `src/intro2.c` (`build/3k --scene intro2`, or `--scene intro` for slides 0, 1 and
  2 in a row) matches the original's machine code in every frame compared: 54 animation frames
  (passes 0..30, then 40, 44-46, 59-61, 74-76, 89-92, 100, 105, 126, 150, 200, 250, 300, 400,
  499, chosen to straddle every hair-counter threshold and several figure-frame changes), all 42
  cross-fade passes of the three subtitle lines, and the 15 fade-out and restore states.
  Pixels, indices and palettes are identical. `tools/check_intro2.sh` reruns it.
- **METHOD:** two reference runs of `tools/dosemu.py`: `--loop-top 0x10f9d` for the animation
  loop and `--snap-hit 1250e:N`, `12870:N`, `12928:1` for the player (arrival counts are global
  over the program, so slide 2's passes are 112..153, its fade-out 14..27, its restore the
  second arrival at `0x12928`). The C side counts per slide.
- **NEGATIVE CONTROLS:** frames from different points differ by 25 to 9,511 pixels; adjacent
  passes across a hair threshold differ by 8,171 to 8,543 pixels, and inside a run of equal
  hair frames (passes 14/15, 29/30, 44/45) they are identical as the model says; one flipped
  pixel is detected.
- **REFACTOR, CHECKED:** the subtitle player moved out of `intro1.c` into `src/subs.c`, and the
  slide scenes now share `src/slide.c` and one driver in `main.c` (`slide_ops`). Slide 1's whole
  comparison (`tools/check_intro1.sh`: 39 + 112 + 14 + restore) was rerun afterwards and is still
  identical.
- **UNIT TESTS / SANITIZERS:** `make test`, 0 failures; the headless build with AddressSanitizer
  and UBSan ran slides 1 and 2 with no report and an identical output frame.
- **WHAT THE COMPARISON DOES NOT COVER:** the final fade to black; real-time pacing (500 passes
  at 35 per second, taken from slide 1, marked GUESS in `src/timing.h`); Space handling; the
  values carried over from slide 1 if slide 1 is skipped (see `code.md`); voice-over.

## Slide 3 reproduced (milestone 3, 2026-10-15)

- **RESULT:** `src/intro3.c` (`build/3k --scene intro3`; `--scene intro` plays slides 0 to 3)
  matches the original's machine code in every frame compared: **all 142 passes** of the
  animation loop, the state after the loop (before the fade), all 140 cross-fade passes of the
  ten subtitle lines, and the 15 fade-out and restore states (`tools/check_intro3.sh`). Pixels,
  indices and palettes are identical. This slide is fully covered by passes, unlike slides 1
  and 2, where the animation was sampled.
- **METHOD:** reference runs of `tools/dosemu.py`: `--loop-top 0x1132e` with `--snap-pass`
  for the loop; a separate `--snap-hit 1145a:0` for the state after it (the loop-top run stops
  at the last snapshot); `--snap-hit 1250e:154..293`, `12870:28..41`, `12928:2` for the player
  (global arrival counts: slide 1 used 112 passes and 14 fade-outs, slide 2 used 42 and 14).
  The C side gained `--dump end:N:FILE` for the state after N animation passes.
- **NEGATIVE CONTROLS:** neighbouring passes differ by 50 to 8,129 pixels (the big jumps are the
  scroll steps and figure moves), the first and last pass by 31,051, and one flipped pixel is
  detected.
- **REGRESSION:** slides 1 and 2 were rerun after the changes and are still identical;
  `make test` 0 failures; AddressSanitizer + UBSan run of the headless build: no report, output
  frames identical to the normal build's.
- **WHAT THE COMPARISON DOES NOT COVER:** the final fade to black; real-time pacing (35 passes per
  second, GUESS); Space handling; voice-over. The clock model of the reference run is mine, as
  before.
- **A DETAIL I GOT RIGHT ONLY AFTER READING TWICE:** the scroll does not blank the top rows; they
  keep a copy of the picture's first row from the smaller shifts. I derived this from the
  code (the copy writes only rows from `shift` down) before writing the C, and the frame
  comparison confirms it.

## Slide 4 reproduced (milestone 3, 2026-10-15)

- **RESULT:** `src/intro4.c` (`build/3k --scene intro4`; `--scene intro` plays slides 0 to 4)
  matches the original's machine code in every frame compared: **266 animation frames** (of
  11,401 passes), the state after the loop, all 70 cross-fade passes of the five subtitle lines,
  and the 15 fade-out and restore states (`tools/check_intro4.sh`). Pixels, indices and palettes
  are identical. The 266 arrivals straddle every boundary of the eye sequence (700, 1200, 1700,
  2400, 3100, the start and end of both pauses, 5500, the last six), the first sparkle updates
  (passes 44 and 91 and the ones after), the start of the first pause (3797..3806, 3810..3900 every
  pass, which crosses the blink threshold) and of the second (6598..6606, 6650..6720).
- **STRONG CHECK ON THE PAUSES:** the last compared arrival is pass 11,400, so the whole length of
  the loop (two pauses of 2,002 and 3,003 passes) agrees with the original, not only the frames I
  looked at.
- **THE RANDOM GENERATOR:** the sparkles depend on the generator's state, which slide 0 left. I read
  it out of the reference run at the start of slide 4 (`tools/dosemu.py --peek 11659:182a0:4`
  printed `8942c90f`, i.e. the 32-bit state `0x0fc94289`) and started the C side from it
  (`--rand-state 0fc94289`, new `tk_set_state`). Both reference runs (animation and subtitles)
  printed the same state. Without it the C frames differ (pass 140: different), so the check
  really exercises the generator's use: 30 numbers at set-up, 3 per respawn.
- **NEGATIVE CONTROLS:** sparkle-update passes differ from their neighbours by 509 to 766
  pixels, an eye-frame change by 1,059, the first and last pass by 1,141, and one flipped pixel
  is detected; passes with no change are identical, as the model says.
- **REGRESSION:** `tools/check_intro1.sh`, `check_intro2.sh`, `check_intro3.sh` rerun and still
  identical; `make test`, 0 failures; AddressSanitizer + UBSan headless run: no report, output
  frames identical to the normal build's.
- **REAL-TIME:** with `TK_PACE_LOG` on this machine the animation ran at 16.7 ms per displayed
  frame (60 Hz vsync) with 5 passes each, the fades at 16.3 ms; the latest step was 35 ms late
  once, and the pacing fix kept it from being replayed.
- **WHAT THE COMPARISON DOES NOT COVER:** the final fade to black; pacing (a free choice);
  Space; voice; the carried-over counters if an earlier slide is skipped.
- **CORRECTION TO AN EARLIER TABLE:** `STORY4_4` is twenty 23x19 frames in two banks, not "23x20,
  19 frames" (`formats.md` fixed).
- **TOOLING:** `dosemu.py` gained `--peek ADDR:LINEAR:LEN` (print memory at the first arrival at an
  address).

## Slide 5 reproduced (milestone 3, 2026-10-15)

- **RESULT:** `src/intro5.c` (`build/3k --scene intro5`; `--scene intro` plays slides 0 to 5) matches
  the original's machine code in every frame compared: **62 animation frames** (of 2,000 passes), the
  state after the loop, all 112 cross-fade passes of the eight subtitle lines, and the 15 fade-out
  and restore states (`tools/check_intro5.sh`). Pixels, indices and palettes are identical. The
  animation frames include every pass from 148 to 156 (the first frame change), runs around the
  next ones (299-305, 450-456, 600-610, 750-758) and the last ten.
- **NEGATIVE CONTROLS:** the pass where a frame changes differs from its predecessor by 1,335
  pixels (and the first pass by 1,715), passes inside a frame are identical as the model says, and
  one flipped pixel is detected. A counter start of 1 instead of 0 would move the change from pass
  150 to 149, which the consecutive samples would show; so the reference run's start value is 0.
- **REGRESSION:** `tools/check_intro1.sh` to `check_intro4.sh` rerun and still identical; `make test`,
  0 failures; AddressSanitizer + UBSan headless run: no report, output identical to the normal build.
- **WHAT THE COMPARISON DOES NOT COVER:** the final fade to black; pacing (a free choice); Space;
  voice; the counter's leftover start value in other runs.

## Slide 6 reproduced; the intro's visuals are complete (milestone 3, 2026-10-15)

- **RESULT:** `src/intro6.c` (`build/3k --scene intro6`) matches the original's machine code in
  every frame compared: **269 animation frames** (of 2,000 passes: every pass 0..160, 465..560, the
  last ten and two between), the state after the loop, all 154 cross-fade passes of the eleven
  subtitle lines, the 15 fade-out and restore states, **and the final state after the fade to black**
  (`tools/check_intro6.sh`). Pixels, indices and palettes are identical. The samples include several
  counter wraps (passes 51, 102, 153) and the eleventh period where `k` reaches 10 (around pass 473)
  and the frame H1 is shown.
- **FIRST FINAL-FADE CHECK:** the fade to black at the end of a slide had not been compared on
  slides 1 to 5. The reference run's state at `0x12111` (just after slide 6's last fade, before the
  `exec`) was saved (`--snap-hit 12111:0`) and the C side's `--dump final:0:FILE` state is identical,
  with palette entries 0..239 all zero. All slides use the same fade routine (`0x121ce`) and the same C
  code (`slide_fade_step`), so this covers them by construction; it was only run on slide 6.
- **NEGATIVE CONTROLS / REGRESSION:** as for the other slides (a frame change differs from its
  neighbours, passes inside a frame are identical, flipped pixel detected); `tools/check_intro1.sh`
  to `check_intro5.sh` still identical; `make test`, 0 failures; AddressSanitizer + UBSan headless
  run: no report, output identical to the normal build's.
- **WHAT THE COMPARISON DOES NOT COVER:** pacing (a free choice); Space; voice; the counter's start
  value if slide 5 is skipped (the port uses the value after a full slide 5, 37).
- **STATE OF THE PORT:** all seven slides of the intro (0 to 6) are built and compared, silent. The
  total number of compared frames: slide 0, 197; slide 1, 166; slide 2, 111; slide 3, 298; slide 4, 352;
  slide 5, 190; slide 6, 440.

### 2026-10-15: the sound path of STORY2.SL, and voice-over in the port
- **WHAT WAS READ:** the Creative Voice driver wrappers at `0x1567d`..`0x15800` (each is `mov bx, N` and
  a far call into the driver), the sound set-up in `main`, the subtitle player's hold
  (`0x1277a`..`0x127f8`) and slide 0's sound handling (`0x10590`..`0x1060e`, `0x10a59`..`0x10a88`).
  METHOD: `tools/dis16.py` and `tools/calls16.py` on `work/unpacked/STORY2.SL.unpacked.exe`, and a
  Python check of `TAB.TAB` against the voice files.
- **DRIVER API (inferred from how it is called, the driver itself is not in the image):** function 4 is
  speaker on/off, 5 gives the driver the address of a status word (`DS:0x19a`, which is non-zero
  while a clip sounds), 6 starts a clip at a far pointer, 8 stops it. The wrappers are `0x156b5` (4),
  `0x156c5` (5), `0x15708` (6), `0x156d9` (8). `0x130ae`, called in every wait loop, is the FM music
  driver's poll, not the voice path, so it does not affect clip timing. CONFIDENCE: medium-high.
- **A LINE WITH SOUND** (flag `[0xa]` of the data at `[0x2cc]` non-zero), in order: read table entry
  k from `TAB.TAB`, open `STORYn_V` (the name is built letter by letter, `0x12364`..`0x123ae`), read the
  clip; cross-fade the subtitle in (14 passes); **then** start the clip (`0x1279f`: speaker on, play
  from buffer + 0x7e, that is the VOC header's 0x1a past the clip) and loop `while [0x19a] != 0`,
  polling the keyboard; Space (scan code 0x39) leaves the loop early; then speaker off and stop. There
  is no further wait after the clip. Without sound the loop is replaced by the 74-tick wait
  (`0x12800`). So with a voice the pause between lines is the next clip's file read, the 14-pass
  cross-fade, and the silence at the ends of the clips (`formats.md`).
- **SLIDE 0 WITH SOUND** (`0x10590`): at the top of every pass, if nothing is playing and the counter
  `[bp-4]` is 0, set it to 1 and stop; if the counter is non-zero, add 1 and when it passes 0x14 reset
  it and start `STORY0_1` (from byte 0x1a). So the clip first starts on the **20th pass**, and starts
  **again 20 passes after it ends** whenever that happens before the loop does. At the end of the loop
  (`0x10a6f`) it spins `while [0x19a] != 0` with no keyboard test, then stops the clip.
  **Consequence worth knowing:** the loop is 7 holds of 56 ticks (21.5 s) plus 112 fade passes, and the
  clip is 20.87 s, so on any machine that is not extremely fast the clip ends before the loop does,
  restarts, and the slide then waits for that second playing. The slide would freeze on its last
  frame for most of 20 s. I cannot tell whether the authors intended that or whether it is a quirk
  their machine hid; no sound-card run was available (the oracle has none). CONFIDENCE in the code
  reading: high; in the consequence: depends on pass speed, which the original does not fix.
- **IN THE PORT:** `src/voc.c` (VOC and ADPCM), `src/voice.c` (TAB.TAB lookup), `src/audio.c` (the sink
  that stands for the driver), `src/subs.c` (the hold), `src/intro0.c` (slide 0). The SDL layer
  queues the clip and "playing" means the queue is not empty. Without a sink the old timed hold is used.
  **[Superseded 2026-10-15: the replay is now ON by default, see the entry at the end of this file.]**
  Slide 0 replay was off by default (`--slide0-replay` turned the original's restart on): at the
  port's pacing slide 0 then takes 27 s instead of 45 s, the extra 18 s being a frozen screen with the
  clip playing a second time. Measured with the real build: wall time 27 s and 45 s.
- **CHECKED:** `make test` (new `tests/test_audio.c`), 0 failures; five deliberate breakages of the
  code (no half-step in the decoder, a wrong restart gap, the wrong clip per line, Space not
  stopping the clip, the hold ignoring the clip) are each caught (`tools/mutate_audio.sh`). The six
  slides' frame comparisons (`tools/check_intro1.sh` .. `check_intro6.sh`) and slide 0's passes 0, 7,
  700 and 1293 are still identical, so nothing visual changed. In the real build, slide 2's three
  holds measured 3.74, 4.51 and 3.12 s against clip lengths of 3.76, 4.50 and 3.13 s.
- **NOT CHECKED:** that anything sounds right: nobody has listened. The ADPCM rule is DOSBox-X's, not
  verified against hardware. The original's behaviour with a sound card was not run. Resampling is by
  SDL's audio stream (quality not examined). The FM music (`MUC.ALL`) is not built.

### 2026-10-15: the music is CMF data, and how the intro starts it
- **WHAT WAS FOUND:** `MUC.ALL` is 25 CMF songs (`formats.md`, "CMF songs in MUC.ALL"). The intro
  loads song 22 (`0x27137`, 18,850 bytes) whole. METHOD: a signature scan, then
  `tools/cmf_survey.py` (a Python reader written first), then `src/cmf.c` checked against it
  (25 songs parse; song 22: 4,819 channel events, last tick 12,599).
- **HOW THE INTRO PLAYS IT** (`STORY2.SL`): the start function `0x12fec` takes a pointer to the song,
  calls `0x15851` (driver function 8, stop), programs the driver clock from the header's
  ticks-per-second (call to `0x13f7c` with 0x34dc / 0x12, which is 1,193,180 = 0x1234dc), hands the driver the patch
  block (`0x1586e`, function 3) and the music block (`0x1583b`, function 2) and starts it (`0x15828`,
  function 6). It is called once, at the end of slide 0 (`0x10ab5`). `0x130b0` is the poll: if the
  driver's status byte (`0x158ba`) is 0 the song has ended and it is started again, so the music
  loops. The poll is called from eight places, in every wait and animation loop. The driver's clock is
  the header's 96 ticks per second, so the tempo does not depend on the machine's speed. CONFIDENCE:
  medium-high for the flow (the roles of driver functions are inferred from how they are called, the
  driver code was not read); not known whether slide 0's own `0x130ae` polls also start it.
- **NOT KNOWN:** how the driver maps notes to OPL registers. The `0x15f28` entry splits a MIDI status
  byte into one of 8 handlers through a table at DS:`0xab` (word entries `0352 03c6 0506 0507 054a
  05aa 05ab 05ac`); those handlers were not read. Plan: either read them, or log the OPL writes of
  the real driver in the oracle (needs an AdLib status reply on port `0x388` and the PIT clock in
  `tools/dosemu.py`), or follow the public CMF semantics and compare by ear.
- **ALSO NOTED:** `MARK.EXE` (the title) reads song 22 at `0x10589` and song 24 (`0x2d76b`, 1,969
  bytes, the shortest) at `0x103b8`. What each is for was not checked.

### 2026-10-15: the OPL2 player, a hang, and a miscount
- **WHAT WAS BUILT:** ymfm (BSD-3-Clause, commit `81aec25`) vendored in `third_party/ymfm/`; a C
  wrapper `src/opl.[ch]pp`; the CMF player `src/music.[ch]`; the music is wired into `--scene intro`
  on a separate SDL device. Details and the list of assumptions: `port.md`, "Milestone 5".
- **CORRECTION (mine):** I wrote "4,804 notes" for the intro song. 4,804 is the number of note-on
  plus note-off events (`tools/cmf_survey.py`). The song has **2,402 notes** (`tools/cmf_behaviour.py`
  per-channel counts: 509+148+215+209+167+229+218+198+509; the player's key-on/key-off counts agree).
  `formats.md` says "note events" and is right.
- **MY BUG, FOUND BY A HANG:** the first version of the player restarted a zero-length song forever
  without producing a sample. `build/test_music` ran for 29 minutes at 100 % CPU before the operator
  cancelled it. I had misread the first silent run as a problem with my wrapper; it was the same
  hang (the 300 s alarm killed the process before its buffered output was flushed). A stack sample
  (`sample`) showed `music_render` -> `cmf_next`. Fixed in `src/music.c`; guarded by
  `test_zero_length`, unbuffered test output, and a 5-minute limit on every test in the Makefile.
  In the real game this would have frozen the audio thread, but only for a song of zero length; the
  intro song is not one.
- **MY TEST ERRORS (the player was right):** 21 first-run failures were wrong expectations in
  `tests/test_music.c` (songs ending before the checked time, one extra register write by the
  patch load, the 4,804 miscount). One of seven mutations (a note-off that releases any note) was
  not caught until the test also checked *when* the last key-off occurs.
- **FACTS ABOUT THE SONG** (`tools/cmf_behaviour.py`): no note overlaps another on its channel, no
  note-off lacks its note, no zero-length notes; velocity 127 on channels 0-7, 76-88 on channel 8;
  only controller is `0x67` = 0; all 10 patches use FM (not additive) connection. So the edge-case
  rules in the player (overlap, stale off, same-tick off) are not exercised by this song.
- **NOT KNOWN:** how it sounds. Confidence that the register mapping is right is medium: the
  pitch and patch layout are standard and measured; the velocity-to-level rule and the channel
  mapping are from the public description only.

### 2026-10-15: all 25 songs exported as MIDI; two songs use rhythm mode
- **WHAT WAS DONE:** `tools/cmf_to_mid.py MUC.ALL OUTDIR` writes each CMF song as a format-0 MIDI
  file (division = the header's ticks per quarter note, tempo = 1,000,000 x tpq / tps microseconds
  per quarter note, so the playing time equals the CMF's). The OPL patches are kept, unchanged, in
  one sequencer-specific meta event at tick 0 (id `0x7d`, tag `CMFPATCH`). Output: 25 files, 23,389
  notes in total, in `work/music/midi/` (git-ignored: derived from the game's data, never commit).
- **HOW IT WAS CHECKED:** each file is read back by a separate MIDI reader inside the tool (no code
  shared with the writer) and compared with the source: division, tempo, every channel event with
  its tick, the end tick, and the patch bytes. All 25 passed. Song 22 gives 2,402 notes and 131.2 s,
  as before. Not checked: playing the files in a MIDI player (no player was run).
- **THE FILES DO NOT SOUND LIKE THE GAME:** a MIDI file cannot hold OPL instruments, so a player
  uses its own General MIDI sounds. Notes, rhythm and timing are the game's; the timbres are not.
- **NEW FINDING, songs 0 (`0x2`) and 3 (`0x8fb6`) use RHYTHM MODE:** they send controller `0x67` = 1
  and play 290 and 326 notes on MIDI channels 11-15, which in rhythm mode are the percussion
  instruments (bass drum, snare, tom-tom, top cymbal, hi-hat; ModdingWiki "CMF Format"). In a General
  MIDI player those channels are NOT drums (only channel 9 is), so that part will sound wrong in the
  exported files. The other 23 songs set `0x67` = 0 (melody mode, channels 0-8 only; song 23 skips
  channel 6, song 4 skips channel 7).
- **CONSEQUENCE FOR THE PORT:** `src/music.c` ignores controller `0x67` = 1 and channels 9-15, so it
  would play songs 0 and 3 wrongly (it prints a warning once and counts the ignored events). The
  intro song (22) and the title's second song (24, `0x2d76b`) are melody-mode and unaffected. Which
  programs play songs 0 and 3 has not been checked. Rhythm mode needs the OPL rhythm registers
  (`0xBD` bits 0-5, channels 6-8 reassigned to the drums) and the drum patch loading rule from the
  wiki; not built.

### 2026-10-15: operator listening result for the music
- The operator listened to `build/3k --scene intro` and confirmed that the music plays along with the
  voice during the slides. This is all that was reported. NOT confirmed: whether the instruments
  sound like the original, the volume balance with the voice (default `--music-gain` 100), and the
  loop point. The work was committed by the operator as `ea692f7` ("initial commit with intro fully working").

### 2026-10-15: MARK.EXE's title plays song 22; the chain title <-> intro runs
- **WHAT WAS FOUND (MARK.EXE, `tools/calls16.py`, read-only):** at `0x10577`..`0x105a4` the title opens
  `MUC.ALL`, seeks to `0x27137` (cx:dx = 2:0x7137) and reads `0x49a2` bytes: song 22, the intro's
  song. With the sound flag `[0x1d0]:4` set it calls `0x11160(buffer)` (`0x105d0`), then sets
  mode 13h. After the loop, with the flag set, it calls `0x1124a` and `0x11272` (`0x10b1c`/`0x10b21`).
  The earlier read at `0x103b8` (seek `0x2d76b`, `0x7b1` bytes = song 24) is inside the optional
  scene skipped by the shipped message byte, followed by a call to the same `0x11160`.
  CONFIDENCE: high for which file, offset and length (literal arguments); medium for the roles
  "start" (`0x11160`), "stop" (`0x1124a`), "shut down" (`0x11272`): inferred from where they sit
  and from the intro's use of the same driver, the driver code was not read.
- **WHAT WAS BUILT:** `--scene game` chains title -> intro -> title (milestone 4 in `port.md`). The
  title now plays the song from its start, like the original.
- **NOT KNOWN:** whether the original's title restarts the song or the driver keeps state across
  `exec` (each program loads the driver again, so a fresh start is the likely case; not tested).

### 2026-10-15: recording the original's audio in DOSBox-X; slide 0's sound; the music's tempo
**METHOD (reusable).** `tools/capture_original_audio.sh PROGRAM CYCLES SECONDS OUT.raw` runs the
original in DOSBox-X with SDL's dummy video driver (no window) and SDL's `disk` audio driver, which
writes the mixer output to a file instead of the speakers. Options by environment: `CAP_SBTYPE=none`
(no Sound Blaster), `CAP_OPLMODE=opl2` (AdLib only), `CAP_OPLEMU=nuked|fast|compat` (OPL core). It
works in a throwaway copy, `work/dosroot_cap/3K`; `STORY2.SL` is started through a copy named
`STORY2X.EXE` because DOS only starts .EXE/.COM files directly. Readers: `audio_timeline.py`
(sound/silence), `find_clip.py` (where a known clip plays), `clip_scale.py`, `loop_period.py`,
`onset_fit.py`, `pitch_check.py` (has a self-test), `emu_baseline.py`, `compare_music.py`.
- **PITFALL (my mistake, caught the same day): the file is 48,000 Hz stereo, S16LE, not 44,100.**
  SDL prints it at start ("format=S16LE channels=2 freq=48000" in the `.log` next to the capture).
  I assumed 44,100 and every time I read was 8.8 % too long (a 215 s run gave "227 s" of audio, which
  a real-time writer cannot do; that is what exposed it). The tools now default to 48,000. Two
  conclusions I drew with the wrong rate, a "5.7 % slower tempo" and a "-1.5 semitone pitch", were
  artefacts of it and are withdrawn; with the right rate they become 0 and +0.00 (below).
- **The capture clock is about 3 % off real emulated time.** Three measurements agree: the file is
  208.7 s for a 215 s run (0.97); a clip of known length (STORY0_1, 20.864 s) fits best at a stretch
  of 0.945-0.97; two beeps 20 s apart by the DOS timer (`CHOICE /T`) are 19.7 s apart in the file
  (about 0.985, a lower bound). So times read from captures are good to about +-3 %, and slower
  than that for anything fine. Pitch is not affected (it is fixed by the sample rate).

**SLIDE 0's SOUND (answers the open "restart question"; `STORY2X.EXE` with the Sound Blaster on).**
`find_clip.py` matches the loudness envelope of `STORY0_1` (20.87 s) against the recording:

| cycles per ms | play 1 | play 2 | scores |
|---|---|---|---|
| max | 2.55 s | 22.2 s | 0.93 / 0.94 |
| 10,000 | 4.05 s | 24.75 s | 0.97 / 0.96 |
| 3,000 | 7.95 s | 29.0 s | 0.95 / 0.94 |

(best score anywhere else: 0.30, 0.29, 0.35; start times are good to about +-1 s because the clip is
quiet at its ends.) **The clip plays exactly twice at every speed**, the second play ends 43 to 50 s
into the run, and slide 1's music starts 3.5 s after that in the 10,000 run (RMS table read off the
capture). That is what the code reading said: restart 20 passes after it ends, wait for the second
play at the end. CONFIDENCE: high that the original does this with an emulated Sound Blaster at the
speeds tried; NOT known that real hardware does the same (DOSBox-X's Sound Blaster is an emulation),
or whether the authors meant it. The port now does it by default (`--slide0-once` for one play).

**THE MUSIC'S TEMPO AND PITCH (OPL only: `CAP_SBTYPE=none CAP_OPLMODE=opl2`).**
- Loop period of the original's song, by matching the recording against itself (`loop_period.py`):
  127.4 s (10,000 cycles), 127.1 s (max), 127.2 s (3,000): **independent of CPU speed**. The port's
  song is 131.2 s. The ratio 0.970 is the capture clock (above), not a tempo difference.
- Aligning note onsets between the recording and the port's render (`onset_fit.py`) gives a time
  scale 0.972 (150 of 212 capture onsets matched, against 67-70 at scales 0.92, 1.00 and 1.04), the
  same on a separate capture of the title (0.972, 48 of 76). Same ratio as the clock error, so **no
  tempo difference is visible at about +-1.5 %** (the clock calibration's accuracy).
- **Pitch** (`pitch_check.py`, which reads back +1.00 and -2.00 semitones on fake captures made by
  resampling): the recording is **+0.00 semitones** relative to the port's render, with a match score
  of 0.76 against 0.36-0.55 for deliberately wrong alignments. So the pitch formula (note 69 = 440 Hz,
  f-number by the standard rule) agrees with the original driver's output for this song.
- **NOT established: timbre, volume mapping, channel mapping.** The spectrum similarity score cannot
  separate right from wrong at fine level: two DOSBox-X OPL cores playing the same original score
  0.66-0.74 against each other, and the port's render scores 0.66-0.74 against them (`emu_baseline.py`),
  while wrong time alignments score 0.36-0.55. Meaning: the port is as close to the original as two
  chip emulations are to each other, by this blunt measure; nothing finer is claimed. Overall level:
  the original's title music is about 1 dB louder than the port's render (RMS 3,818 vs 3,414 over the
  first 15 s, different chip emulation scaling, so weak evidence). Earlier attempts with a spectral
  window score at a constant offset failed for the clock error and the wrong rate; they are not evidence.

**WHICH PROGRAM PLAYS WHICH SONG (literal arguments of the `MUC.ALL` reads; `calls16.py --ds-off`).**
The seek wrapper's arguments are (handle, origin, offset-low, offset-high); this reading puts every
offset found so far exactly on a `CTMF` signature. Song 22 (`0x27137`, 18,850 B): MARK (title),
STORY2.SL (intro), ENDING.SL. Song 24 (`0x2d76b`, 1,969 B): MARK's optional scene. Song 23
(`0x2bad9`, 7,314 B): WALK0.SL (the menu) and one of WALK.SL's two reads. **CAST.SL: seek 0:2 = file
offset 2, reads 20,000 B: song 0 (8,996 B) and the first 11,004 B of song 1** (so it plays song 0).
WALK.SL's second read takes its position and length from variables (a song chosen at run time).
CORRECTION (mine, the same day): an earlier version of this paragraph said CAST.SL reads "the last
20,000 bytes"; that misread the seek arguments.
**The drum songs: song 0 (rhythm mode, 290 drum notes) is CAST.SL's song; song 3 can only come from
WALK.SL's run-time choice.** So rhythm mode is not needed for the title, intro, menu or ending, and
it is needed for CAST.SL (reached from the menu: WALK0.SL names it) and the world. CAST.SL was not
run here, and which song it actually starts from that buffer is inferred (the driver starts at the
beginning of the buffer it is given, as in the other programs).

### 2026-10-15: decision on rhythm mode
- The operator agreed to defer rhythm mode (controller `0x67` = 1, MIDI channels 11-15 as drums) until
  `CAST.SL` (plays song 0) and `WALK.SL` (can play song 3) are ported. Reason: the title, intro, menu
  and ending only use melody-mode songs, and a drum implementation can then be checked against a
  recording of the original program that plays it (`tools/capture_original_audio.sh`) instead of
  against the public CMF description alone. Until then `src/music.c` ignores rhythm mode, warns once
  and counts the ignored events.

### 2026-10-15: survey of WALK0.SL (the menu), read-only
- **SIZE:** unpacked image 30,681 bytes; code and data end at `0x16db0`/`0x17xxx`, so about 28 KB of
  code, the size class of `MARK.EXE` (23 KB) and a fraction of `WALK.SL` (118 KB). METHOD: file sizes,
  `tools/mz_strings.py`, `tools/calls16.py --ds-off 0x6db0` (the data segment starts 8 bytes before the
  "MS Run-Time Library" string, as in every program here).
- **FILES IT TOUCHES** (oracle run, `--log-files`, and the filename table at `DS:0x42`...): `walk0.sl`
  (itself), `rpg.lcp` (four reads), `gmain.lcz`, `muc.all` (song 23, `0x2bad9`, 7,314 bytes), `__1.al1`,
  `__1.al2`, `__1.now`, `man_info.all`, `man_info.now`, `ani.lcp`. It can start `walk.sl`, `cast.sl`, or
  end the program. The save files (`SAVEnn.000/.001/.002`) are NOT named in its string table; how it
  finds them is not read.
- **ON SCREEN (oracle, 500,000 instructions per tick, ready by tick 30):** the stone frame of
  `GMAIN.LCZ` (milestone 1) with a dark-red panel and four menu items, read from the snapshot
  (`work/walk0/t030.png`, git-ignored): 新遊戲開始 (new game), 取一舊進度 (load a saved game),
  製作群介紹 (credits), 回到 DOS (back to DOS). The translations are mine. F1..F10 buttons sit in the
  left and right columns of the frame. Faint characters inside the panel differ between snapshots
  (30, 60 and 119 ticks), so something animates. NOT identified: what moves, and how a menu item is
  chosen (keys, mouse, or the F buttons).
- **THE EXIT PATH** (`0x11790`..`0x11a10`): writes `__1.now` (`DS:0xc4`), then reads `walk0.sl` and
  `ani.lcp` (from offset `0x600`) and compares them, and if they differ writes `walk0.sl` back from
  the `ani.lcp` copy. **CONFIRMED on the files: `ANI.LCP` after its first `0x600` bytes is an exact
  copy of `WALK0.SL` (17,631 bytes, 0 differing).** This is the program checking and repairing its own
  file (an integrity check). The port does not run this code and has no program file to protect, so
  it is NOT reproduced; recorded so it is not mistaken for part of the menu. After it come
  `cmp [bp-0x2c], 0xe51` (below: start `walk.sl`), `== 0x1c9c` (start `cast.sl`), otherwise return to
  DOS. NOT UNDERSTOOD: [bp-0x2c] is also the variable the check uses, so this reading of the
  decision variable is probably wrong; where the menu choice is kept was not found. Needs a read of
  the code above `0x11790` (the menu loop).
- **ORACLE LABEL NOT EXPLAINED:** the run reports "video mode 0x3" at the end though the snapshots
  are 320x200 graphics. Not looked into; the pictures are the evidence.
- **WHAT THE PORT WILL NEED (inferred):** the frame and palette (done in milestone 1), the four
  items (text probably from a font or a picture chunk; not found yet), mouse hit-testing (the port has
  only a "button held" signal today), the animation, hand-over to `walk.sl` / `cast.sl`, and song 23.
  No work on any of it has been done.

### 2026-10-15: WALK0.SL (the menu): how it is controlled, and what each item does
METHOD: the call-level outline (`tools/calls16.py --ds-off 0x6db0`, saved as `work/walk0/calls_main.txt`),
raw disassembly of the key read (`tools/dis16.py 0x10c80..0x10cf0`), and runs in the oracle emulator
(`tools/dosemu.py`, 500,000 instructions per tick) with scripted keys, watching the file opens and
snapshots. Everything below that says "oracle" was observed, not inferred.
- **The selection is a screen offset, not an index.** `[bp-0x2c]` holds the highlighted item as the
  pixel offset (320 per row) of its top edge: `0x4`, `0xe50`, `0x1c9c`, `0x2ae8`, `0xe4c` apart (11.4
  rows). This is also what the exit path compares (`0xe51` is "item 2 or later", `0x1c9c` item 3);
  the earlier note that the decision variable "collided" with the self-check's was a mix-up caused by
  `calls16.py` not tracking loads. CORRECTED.
- **Keyboard** (the loop `0x10c8d`..`0x10e9a`, one pass per BIOS tick or so): scancode read straight from
  port `0x60` (`0x131b6`). Up `0x48` / Down `0x50` move the highlight and wrap at both ends. Space
  `0x39` or Enter `0x1c` confirms (`0x10e9d`). **A key only counts after a key RELEASE (a break code,
  scancode >= 0x80) has been seen**: until then every make code is replaced by `0x82` and ignored
  (`0x10cac`..`0x10cba`), and the latch clears after each accepted key. Effect: the Space that left
  the title cannot select an item, and holding a key does not repeat. The port must reproduce this
  (a key event, not a held state).
- **Mouse** (`0x11dc7`): position clamped to x `0x40..0xed`, y `0x10..0xa2`; moving over an item
  highlights it: y `0x28..0x40` item 1, `0x47..0x5f` item 2, `0x68..0x80` item 3, `0x88..0xa0` item 4
  (and presumably a click confirms; the click path was not read). The oracle has no mouse, so this was
  read, not run. The F1-F10 buttons in the frame: no code for them was found in the menu loop; they
  may be part of the picture only. NOT established.
- **Transition:** after a confirm, 33 steps of an animation (`0x10ecf`..`0x10fe1`: blits through
  `0x13201`, `0x13119`, `0x127d0`) run before the action; about 11 ticks in the oracle.
- **The four items (all observed in the oracle):**
  1. New game (highlight `0x4`): reads `__1.al1` and `__1.al2` (20,000 bytes each), decompresses
     them (`0x11c97`), writes `__1.now` (47,250 + 45,500 bytes), reads `man_info.all` (504 bytes) and
     writes `man_info.now`, runs the self-check, starts `walk.sl`.
  2. Load a saved game (`0xe50`): reads `rpg.lcp`, then opens `save00.000`, `save01.000`, `save03.000`,
     `save04.000`, `save07.000`, `save09.000`, `save10.000` and probes `save02/05/06/08.000` (missing in
     the copy). So **saves are found by probing `saveNN.000` for NN = 0..10**. On screen the item turns
     into a row of ten numbered cells with markers below them (slot picker). Not read further.
  3. Credits (`0x1c9c`): self-check, then starts `cast.sl` (the credits are a separate program).
  4. Quit (`0x2ae8`): self-check, then exits with code 1.
- **Screens seen** (`work/walk0/*.png`, git-ignored): the main menu; the highlight in white on the
  chosen item; a small green arrow appears next to it once something is selected (the cursor?
  not identified); the slot picker.
- **THE SELF-CHECK is not part of the menu** (see the entry above): it runs on every exit path.
- **WHERE THE ITEM TEXT COMES FROM: not found.** `rpg.lcp` is read at offset `0x1643b` (1,214
  bytes, `0x101dc`) and at `0x58f4` (7,811 bytes, `0x1055c`), and `0x3a1e` later; one of these is the
  likely source of the four labels and the slot picker, unverified.
- **ORACLE NOTE (for later runs):** `tools/dosemu.py --key T:CODE` sets port `0x60` to CODE and keeps
  it until changed, like a real keyboard keeps the last scancode. Programs that latch on break codes
  (this one) need a release (CODE | 0x80) before the press, e.g.
  `--key 60:0x9c --key 80:0x1c --key 82:0x9c`. Holding a key from tick 0 does nothing here, by design.

### 2026-10-15: WALK0.SL (the menu): what it draws, in what order, and where the pictures come from
METHOD: new oracle options `--trace-calls` (log the stack arguments of chosen function entries) and
`--dump-local` (write the buffer a local far pointer points to), then `tools/lcz_at.py` to decode
`RPG.LCP` blocks and `tools/buf_sheet.py` to view buffers. All numbers below are from a run of
`WALK0.SL` in the oracle (`--instr-per-tick 500000`), 1,200 drawing calls logged up to tick 40.
- **Data the menu loads from `RPG.LCP`** (offsets are the seek arguments, low word then high word):
  `0x0` 768-byte palette; `0x1643b` (1,214 bytes -> 2,148 decoded, small tiles, use not yet read);
  `0x58f4` (7,811 bytes) and `0x3a1e` (later, save picker); and the two that matter now:
  - `0x1384`, 7,889 bytes -> **14,644 decoded = a sprite with header `7a 00 78 00` (width 122,
    height 120) + 122 x 120 pixels: the four labels, 30 rows each** (新遊戲開始, 取一舊進度,
    製作群介紹, 回到 DOS). Item offsets `4 + 3,660 x k` are the highlight values.
  - `0x1d3e` + `0x10000` = `0x11d3e`, 7,000 bytes -> **32,260 decoded = header `c0 00 a8 00`
    (192 x 168) + 32,256 pixels: the dark-red backdrop with the faint characters** that scroll.
  Both render correctly with the interface palette (`work/walk0/lab_a.png`, `lab_b.png`).
- **The panel** is 192 x 168 at screen (64, 16); the pixels the menu adds to `GMAIN.LCZ` span x 64..255,
  y 16..183 (plus the three small digits at the top right). Presented with
  `0x13026(0x1440, buffer)`, where `0x1440` = 16 x 320 + 64 = the panel's top-left in a 320-wide screen.
- **Opening, in phases** (oracle ticks at 500,000 instructions per tick; the real timing is not known):
  1. ticks 0-3: load palette and `gmain.lcz`; three small blits; twelve palette fills (`0x13143`, the
     F-key cells?).
  2. ticks 3-15: **a wipe of 20 steps**: each step blits a 16-pixel-wide piece at `x += 16, y += 10`
     (`0x12f8b`, `0x12b5d`, `0x13201` to the screen). The "Ready" lettering of `GMAIN` is built up
     in pieces: the first snapshots show it as scattered dots that resolve into the word.
  3. ticks 15-19: **160 steps** (`0x130f6`, `0x12c70`, `0x12786`, `0x13026`) in which the frame appears
     and the word "Ready" shrinks to a point, leaving the empty dark panel. (The reverse of an
     expanding effect.) The number at the top right appears here and does not change afterwards.
  4. tick 19: 100 calls of `0x11cef` with random values (`0x136aa` = rand) and two `0x12dde` calls;
     then both pictures above are decoded.
  5. from tick 19: the loop, about 2.3 passes per tick in the oracle. Per pass: **rotate or shift the
     backdrop** (a 1-pixel step, `0x1328b`, or for pass counter >= 0x78 a different shift
     `0x1e4:0x25f` with two 96-byte copies); copy 16,130 bytes; copy the label strip (7,322 bytes =
     `0x1c9a`); **recolour the highlighted 122 x 30 label** (`0x13119(ptr + highlight, 3660, 0x17, 0x10)`:
     colour 0x17 -> 0x10, i.e. the selected label changes shade); a masked blit (`0x127d0`, colour 0
     transparent, as everywhere else) of the label onto the panel; present. Counter `[bp-0x12]`
     wraps at 0xc8.
- **THE NUMBER AT THE TOP RIGHT ("486" in the snapshots) is static** (checked: the region is
  identical at 250k, 500k, 1M and 2M instructions per tick, 0 of 1,248 pixels differ). It is three
  small 8x8 blits at x = 4, 13, 22 (`0x12902`, `0x10278`..`0x102be`) from the 2,148-byte block at
  `0x1643b`; this is a stored picture, not a measured value. Per the operator it is not a priority.
- **NOT READ YET:** the exact semantics of `0x1328b`, `0x1209f`, `0x13119`, `0x12f8b`, `0x12c70`,
  `0x12786` (several have equivalents already identified in other programs by address; the
  addresses differ per program), the slot picker, the click path, the 33-step confirm transition.

### 2026-10-15: THE "486" ON THE MENU IS A CPU-CLASS LABEL DERIVED FROM THE SPEED CLASS (answers the operator's hunch)
- **WHAT:** the three digits at the top right of the menu (`WALK0.SL`) are drawn from three 8 x 8
  tiles of the 2,148-byte block at `RPG.LCP` `0x1643b` (an atlas of digits, 64 bytes per tile, in
  digit order). The first tile is `RPG.LCP` byte 0 x 64 + 0x80 = tile (byte 0 + 2); the other two are
  fixed tiles 6 and 8 (offsets `0x180`, `0x200`). So the label is **"(2 + speed class) 8 6"**.
- **EVIDENCE (oracle, `--data` pointing at a copy with byte 0 changed; only the copy was edited):**
  byte 0 = 0, 1, 2, 3, 4 gives the first digit 2, 3, 4, 5, 6 (read from the glyph shapes in the
  snapshots; the original file has byte 0 = 2, which gives the "486" the operator saw). The other
  two digits did not change with the byte. The label did NOT change with the oracle's emulated CPU
  speed (identical at 250k..2M instructions per tick), so it is not measured by this program: it
  only reads the stored byte that `TV.EXE` wrote after timing the machine.
- **MEANING:** byte 0 of `RPG.LCP` is the speed class written by `TV.EXE` (class 0..4, larger =
  faster, see code.md); `WALK0.SL` shows it as the CPU generation 286..686 next to the frame. My earlier
  statement that "no code reads the CPU" stands (no CPUID or similar), but the earlier
  hypothesis that "486" was an unrelated stored game value was WRONG, and the operator's guess
  (an 80486-class label) was right about what the number means.
- **NOT KNOWN:** whether other programs show it too; what the label is for beyond display; the
  mapping of classes 5+ (the byte can only be 0..4 from `TV.EXE`'s table). Glyph shapes for the
  non-486 values were identified by eye on a coarse text rendering, not compared pixel by pixel.

### 2026-10-15: the menu's static frame matches the original (WALK0.SL)
- **RESULT:** `src/menu.c` reproduces the original's menu screen pixel for pixel: 40 of 40 frames
  (`tools/check_menu.sh`). Details and what is not covered: `port.md`, "Milestone 4b".
- **METHOD, for later programs:** (1) `--trace-calls` to see the order and arguments of the drawing helpers;
  (2) `lcz_at.py` / `buf_sheet.py` to find the pictures; (3) screen diffs against the plain base picture
  to find what the program adds and where; (4) read each helper's disassembly and reimplement it;
  (5) `--dump-local` to compare intermediate buffers byte for byte before the whole screen.
- **HELPERS IDENTIFIED** (linear addresses in the unpacked WALK0.SL; the same routines exist at other
  addresses in the other programs): `0x127d0` masked blit (colour 0 transparent), `0x1280f` stencil blit
  (paints a sprite's silhouette in one colour), `0x12902` masked tile copy into a wider strip, `0x12b5d`
  the sparse "stretch" (see port.md), `0x13119` recolour, `0x13143` shade (every non-zero byte to one
  value), `0x13201` copy counted in WORDS, `0x13026` present a sprite at a screen offset, `0x1328b`
  roll rows left, `0x1209f` scroll up, `0x130f6` memset, `0x11cef` store a word, `0x11c97` LCZ decode.
- **CORRECTIONS (mine, the same day):** (a) I first read the strip tile columns as 4, 13, 22; the 4 is the
  sprite header, the columns are 0, 9, 18. (b) The `0x13201` argument order is (source, destination,
  words), the first pointer is the SOURCE. (c) My first stretch re-read the source pixel in the inner
  loop; the original reads it once per carry, which makes the label dotted. (d) A "highlight 3" check
  showed differences only because the oracle's third key press had not landed yet at those passes
  (verified by counting white pixels per label); settled passes match.

### 2026-10-15: the menu's input, checked against the original (WALK0.SL)
- **KEYBOARD, exactly:** `0x10c8d`..`0x10d65`. Reads port `0x60` once per pass. A byte above `0x7f` sets
  the latch `[bp-0x32]`; with the latch clear a make code is replaced by `0x82`; below `0x80` the latch is
  cleared and Space/Enter jump to the confirmation, Up/Down move the selection (wrapping). Confirmed pass
  by pass in the oracle's pass-synchronised mode (`--loop-top`, `--key-pass`, `--hit-pass`).
- **MOUSE, exactly:** `0x10d69`..`0x10e86`. The wrapper at `0x11d54` only does a reset (INT 33h function 0)
  at the start (`0x100e5`); each pass `0x11dc7` calls function 3 and clamps CX to 314 and DX to 194, and
  the menu clamps again to x 64..237, y 16..162. The cursor sprite is the 382 raw bytes after the label
  strip in `RPG.LCP` (offset `0x3255`; the read continues from the label read, `0x10aab`). It is drawn at
  `(y-16)*192 + (x-64)` in the panel buffer after the labels, before the present. Hover and click rules in
  `port.md`, "Milestone 4c". The flag `[bp-0x22]` is the reset's result (mouse present); `es:[4]` (the sound
  flag set by `0x122d0`) is a different thing, and I had briefly misread it as the mouse flag.
- **ORACLE EXTENDED:** `dosemu.py` now emulates a mouse driver for the three functions the menu uses
  (`--mouse`, `--mouse-pass`), reports the pass at which an address is reached (`--hit-pass`,
  `--hit-pass-stop`), and replays keys per pass (`--key-pass`, existing). LIMIT: an emulation written by me.
- **A TWO-MODE ORACLE QUIRK, unexplained:** the oracle's free-running run (`--snap-hit`) shows the backdrop
  one pixel further on than its pass-synchronised run (`--loop-top`) after the same number of passes.
  The port's default matches the free-running run (verified at 20 passes up to 401); the input checks use
  `--menu-no-initial-roll` to line up with the pass-synchronised run. Cause not found (the tick manipulation
  of `--loop-top` is the suspect). Not visible in play.
- **MY MISTAKES (the same day), so nobody repeats them:** (a) my first scripted confirm tests held the mouse
  button down for the rest of the run, so the original kept confirming in later screens and the oracle runs
  took minutes; my script printed "never" for runs it had killed on a timeout. Release the button, and
  distinguish "never" from "did not finish" (`check_menu.sh` now does). (b) I compared the first keyboard
  frames against the wrong pass (an off-by-one between "snapshot at the loop top" and "picture
  presented by the pass") and spent a while chasing a phantom; the fix was to line up two simple frames
  first. (c) My first mutation checks missed three input mutations because the scripted positions did not
  include the band edges and Space; the scripts now do.

### 2026-10-15: THE MENU'S OPENING ANIMATION (WALK0.SL 0x10357 .. 0x10537), read and checked frame by frame
- **WHAT IT DRAWS** (all three phases: oracle `--snap-hit` at the return from each present, 183 screens, all
  reproduced pixel for pixel by `menu_opening_frame`):
  1. **A zoom of 20 steps** (`0x1035c .. 0x103cc`). Step i grabs the rectangle (2 + 16 i) x (2 + 10 i) of the
     finished picture (GMAIN with the CPU label), top-left at linear offset `0x7c5f - 0x648 i` (so it
     grows around the centre, 5 rows and 8 columns per step), with `0x12f8b` (a rectangle copy into a
     sprite), stretches it to 320 x 200 with the SPARSE stretch `0x12b5d` (the same routine as the CPU
     label's: one source pixel written per carry, zeros between) and copies the 64,000 bytes to the
     screen. That is why the picture starts as a few dots and fills in.
  2. **The whole picture** is copied to the screen (`0x103dd`).
  3. **The lettering shrinks in 160 steps** (`0x10410 .. 0x104c7`). The 83 x 33 rectangle at `0x65b1`
     = (113, 81) is copied from the screen once. Each step clears an 83 x 33 frame (2,740 bytes: one
     more than the pixels), resizes the lettering to w x h with `0x12c70` (a DIFFERENT routine from the
     sparse stretch: nearest neighbour, every destination pixel filled), places it with `0x12786` at an
     offset that keeps it centred, and shows the frame with `0x13026`. Every 5th step w -= 2 and the
     offset moves one column; every 10th h -= 2 and it moves one row. The loop runs while h > 1:
     160 steps in all, ending at 21 x 3.
  4. **A clear** of 84 x 33 at the same place (`0x12dde`, colour 0): the area is under the panel anyway.
- **THE TWO STRETCHES, so nobody confuses them:** `0x12b5d` (sparse, zeroes its destination first, the
  horizontal accumulator restarts every source row) and `0x12c70` (dense nearest neighbour). In `0x12c70`
  **the horizontal fraction is NOT reset per row**: it carries from one row into the next, so
  neighbouring rows can pick different source columns. The port reproduces that (unit test with a
  hand-computed 3x2 -> 2x2 case); with a reset per row the shrink frames differ from the original's.
- **NOTHING IN THE OPENING WAITS:** no retrace test, no delay, no BIOS clock (`0x13026`, `0x13201` and the
  rest are plain copies). The pace is whatever the CPU gives. In the oracle (500,000 instructions per
  tick) the zoom took about 12 ticks and the shrink about 4; on a real machine of the time the
  shrink, which moves a few KB per step, was probably over in a fraction of a second. This is why
  the port's rates are guesses (`timing.h`).
- **CORRECTION TO THE EARLIER NOTE ("tick 19: 100 calls of 0x11cef ... then both pictures are decoded"):**
  after the 100 random words (written into a buffer that the backdrop's decode later overwrites, so
  invisible) the code does NOT skip straight to the pictures. It runs `0x1054a .. 0x107be`: it reads
  `RPG.LCP` `0x58f4` (7,811 bytes), seeds the random generator, fills a 10-byte digit table, draws
  random digits and a few glyphs into two work buffers (one of them the 7,811-byte buffer, which the
  song later overwrites; the other the digit atlas, which is not used again). None of it reaches the
  screen. The code after it, `0x107c1 .. 0x10a4c` (which handles Left/Right keys and a counter to 5000
  and would belong to a screen of selectable cells), is NEVER reached: the loop's test at `0x10a50` is
  three `nop`s, so the program falls through to `0x10a53`. It looks like a part of the game that was
  switched off by patching the test. My earlier guess that this was the slot picker was WRONG: the picker
  is reached from item 1 (seen by clicking it in the oracle); where its code lies was not read. The 7,811-byte block at `0x58f4` is read by the opening, whatever else uses it. Not reproduced, because nothing visible depends on it.
- **THE SONG** is started after the opening (`0x10b3b .. 0x10b54`, only if the sound flag
  `es:[4]` is set), not at the start as the port did before today.
- **KEYS DURING THE OPENING:** no input is read; the keyboard port keeps its value, so a key still down
  when the loop starts is handled by the latch rule (it is ignored until released). The port does the same.

### 2026-10-15: THE "ONE-PIXEL QUIRK" WAS MY OWN OFF-BY-ONE (correction of the entry on the menu's input)
- **WHAT I HAD WRITTEN:** that the oracle's free-running run (`--snap-hit 0x10e97:N`) shows the backdrop one
  pixel further on than its pass-synchronised run (`--loop-top`, `--snap-pass N`), cause unknown, and
  the port started one roll in with the counter at 1 to match the first, with a switch
  (`--menu-no-initial-roll`) for the second.
- **WHAT IT WAS:** `--snap-hit ADDR:N` is the N-th arrival at ADDR counting from 0, so `0x10e97:N` is the
  screen after N + 1 passes, not N. The port's "pass N" was compared with it, and I made up the
  difference with a hidden extra roll and a counter that started one ahead. In the original the counter
  is set to 0 before the loop and counted at its top (`0x10b89`, `0x10b91`): no extra roll, and the first
  pass has counter 1. The hack gave the right pictures up to the pass where the counter reaches 120 (the
  switch from rolling left to rolling up) and the wrong ones from there on, which none of the
  input scenarios (up to 66 passes) reached. It was found when the first transition test at pass 130 failed.
- **FIXED:** no initial roll, counter starts at 0, `check_menu.sh` asks the oracle for arrival N - 1 to get
  N passes. All 289 comparisons that existed then were identical again under the correct model (the pass
  frames at the 119/120 turn and at 125, 150, 199-202 among them). The flag `--menu-no-initial-roll` and
  the function `menu_set_initial_roll` are gone.
- **WHY IT SURVIVED:** the earlier checks compared the same frame numbers under both modes without
  asking what an "N" meant in each tool. Lesson recorded: when two tools count "N", test with a case
  where the answer depends on the exact count (here: the turn of the roll).

### 2026-10-15: THE TRANSITION AFTER A CHOICE (WALK0.SL 0x10e9d .. 0x10fe4) AND WHAT THE FOUR ACTIONS DO
- **THE TRANSITION IS A CHECK MARK DRAWN BY THE ARROW.** 33 steps, no wait, no sound. Per step: the backdrop
  rolls one pixel left (always, even in the vertical-roll phase; `0x1328b`); two words `0x0024` are
  written into the chosen label's pixels (`0x11cef`) one row apart, i.e. colour `0x24` and a 0 that erases
  the pixel beside it; the panel is built as a pass does; the arrow sprite is drawn over it at the head of the
  stroke; the panel is shown. The stroke starts at row 10, column 0 of the label and moves one row and one
  column per step: down for 11 steps, then up for 22 (`0x10fc1`/`0x10fcd`). The arrow's own position starts
  at panel row 33 + 30 k, column 37 and moves with it. Two pixels of colour `0x24` per step are the only
  `0x24` pixels in the label box (66 after 33 steps), which the unit test uses.
- **THE MARK IS WRITTEN INTO THE LABEL STRIP ITSELF** (it is not undone), so the original cannot return to the
  menu loop afterwards; it never does: every item ends the program or starts another one.
- **CHECKED:** 8 scenarios x 33 frames identical to the oracle: the four items, at the turn of the roll
  (pass 119), in the vertical phase (130), after the counter wrapped (204), and by mouse click.
- **WHAT EACH ITEM DOES AFTER IT** (`0x10fe4 ..`; read from the call list and from the file opens seen in the
  oracle; not run to the end because the next programs are not unpacked here):
  - **Item 0, new game:** stops the music, reads `__1.al1` and `__1.al2` (20,000 bytes each), decodes them,
    writes `__1.now` (0xb892 + 0xb1bc bytes), reads `man_info.all` (504 bytes), decodes it and writes
    `man_info.now`; then the common end.
  - **Item 1, load:** the slot picker (`0x111a1 ..`): `RPG.LCP` at `0x3a1e` (2,354 bytes), the file names
    `saveNN.000` are built and probed (11 slots; seen in the oracle by clicking the item). That the chosen
    slot's data ends up in `__1.now` and `man_info.now` is READ from the call list (`0x115d0 ..`, `0x1167e ..`),
    NOT RUN: medium confidence. NOT BUILT. (`0x11304 ..` is the picker's own loop, with the mouse and keys.)
  - **Item 2, credits:** the common end with `cast.sl`.
  - **Item 3, quit:** exit code 1 (seen in the oracle: the run ended with code 1). Why code 1 is not known.
  - **The common end:** a self-check (the first 0x600 bytes of `walk0.sl` against the copy in `ani.lcp`,
    which is an integrity check and is not reproduced), frees the buffers and starts `walk.sl` (items 0, 1)
    or `cast.sl` (item 2) with the exec call at `0x136d2`.

### 2026-10-15: THE SLOT PICKER (WALK0.SL 0x111a1 .. 0x115bf), read and checked against the oracle
- **SETUP (item 1, after the check-mark transition):** reads `RPG.LCP` `0x3a1e`, 2,354 bytes = **2,301 bytes of
  LCZ (`0x8fd`) that decode to 3,172 = 122 x 26 raw pixels (the 11 numbered cells, colours `0xf0` and up)
  followed by 53 raw bytes = a 7 x 7 sprite (header `07 00 07 00`, colour `0x54` with a few `0x1a/0x1c/0x1d`
  pixels)**. The cells are decoded straight into the label strip at label 1's place (`0xe50`), so they
  cover the top 26 rows of the "load" label (and with them the check mark); its bottom 4 rows stay.
  The sprite is copied to the start of its buffer (`0x131d8`: a plain BYTE copy, not the word copy `0x13201`).
- **ICONS:** for each of 11 slots the program tries to open `saveNN.000` (NN = 00..10, built digit by digit
  by `0x12258`, which writes a 3-digit decimal); if it opens, the 7 x 7 sprite is stamped (opaque,
  stride 122) into the label strip at `0x166c + 11 NN` (row 17, column 2 + 11 NN of the strip). Then the
  sprite's colour `0x54` is turned into `0x11` (`0x13119` over all 53 bytes), which makes the blinking marker.
  With the shipped data the saves are 00, 01, 03, 04, 07, 09, 10 (seen as the oracle's file opens).
- **PER PASS (`0x11304`):** the backdrop rolls one row UP, every pass (the same code as the menu's vertical phase;
  there is no counter test here); the strip is copied; a counter `[bp-8]` goes 1..10, 0, 1.. (set to 0 at
  `0x107b6`, before the jump over the unused code), and while it is below 5 the marker is drawn at the
  selected cell (opaque, 7 x 7, row 9, column 2 + 11 slot): 4 passes on at first, then 5 on and 6 off;
  the strip is laid over the panel with colour 0 transparent (no white highlight here); the keyboard
  with the menu's latch; the mouse; the panel is shown at (64, 16).
- **KEYS:** Left (0x4b) and Right (0x4d) move the selection and stop at the ends; Space (0x39) or Enter (0x1c) pick
  if the selected slot has a save, which the program tests by reading one pixel of the icon (offset `+0x44a`
  from the cell = one row down and over in the strip: colour `0x54` only where the icon was stamped); on
  an empty slot they do nothing. **There is no Escape or any other way out** (the picker only ends by a pick).
- **MOUSE:** the arrow is drawn like in the menu. With x in 100..220 and y in 70..95 (strictly: x > 0x63 and
  < 0xdd, y > 0x45 and < 0x60) the cell under the pointer, (x - 100) / 11, becomes the selection; a button
  down there acts as Space. The latch is the menu's variable, so the Space/Enter that chose the item
  must be released before another key counts, as in the menu.
- **THE PICK** (`0x115c2 ..`, not reproduced): stops the music, builds `saveNN.000`, reads it (504 bytes after
  decoding), writes `man_info.now`; reads `saveNN.001` and `.002` (up to 65,500 bytes each), decodes both and
  writes them as `__1.now`; then the common end (starts `walk.sl`). From the call list; not run to the end.
- **CHECKED:** 87 comparisons identical to the oracle: 18 frames of the blink, 34 of the keys, 21 of the mouse,
  and 14 decisions (the pass in which the original takes the pick branch `0x115c2` and which `saveNN.000`
  it opens next) for Enter, Space, no release first, Right to every end, empty slots, clicks in and out of the cells,
  a button held at the start. To count the picker's passes next to the menu's, `dosemu.py --loop-top` now takes a
  comma-separated list: arrivals at all the addresses count as one sequence.
- **A WRONG FIRST GUESS, corrected by the check the same hour:** I took the blink counter's start from
  the opening's leftover code (at least 0x57, so the first pass would reset it to 0 and the marker would
  show 5 passes); two frames of 15 differed. The program sets it to 0 at `0x107b6`, so the first cycle is 4 passes.


## 2026-10-16: CAST.SL (the credits), first survey in the oracle

Read-only survey; nothing of `CAST.SL` is built. Run: `work/venv/bin/python tools/dosemu.py
work/unpacked/CAST.SL.exe --data original_bin --ticks 1500 --instr-per-tick 500000 --log-files --log-vga
--palette-from work/check_menu/o_conf_item0_1.raw --snap TICK:FILE.ppm ...` (`CAST.SL` is not packed; the exe in
`work/unpacked/` is the file itself).

- **Files, in order:** `cast.sl` (tick 0), `cast1.lcz` (3), `cast2.lcz`, `ani.lcp`, `muc.all` (14). Nothing is
  opened after tick 14 in 1,500 ticks.
- **It never sets the DAC.** The palette routine (`0x116a6`) and the port-write helper (`0x10bcc`) get zero calls
  in 1,500 ticks, there are no `int 10h` palette calls, and no other `0x3c8`/`0x3c9` writes exist in the code.
  Run from zero, the oracle shows pixels with an all-black palette. With the menu's palette preloaded
  (`--palette-from`, new) the picture is coherent (navy sky, blue floor, gold text). That the program inherits its
  parent's palette (as an exec'd program would) is the likeliest reading; NOT confirmed: which palette the
  menu actually hands over, and whether the credits rotate or fade colours.
- **What is on screen (five snapshots, ticks 100-1400):** the title lettering (三界論) on a floor under a sky;
  staff credits (出品, 第三波文化事業, 發行人, 製作人, 技術支援, 創意支援 and names) fade and move in the sky; the
  floor shows streaks of the credits' text in perspective. Whether the streaks are the intended effect is not known.
- **A WRONG WARNING of mine, corrected:** the code has Mode X routines (sequencer `0x3c4`, graphics controller
  `0x3ce`, write mode 1 at `0x10be3` / `0x10bf2`), and I suspected the oracle's flat video memory was corrupting
  the floor. `--log-vga` (new) shows NO write to `0x3c4`, `0x3ce` or `0x3d4` in 1,500 ticks: the routines are not
  used (as in MARK, where the Mode X routines are never called). The picture is plain mode 13h, so a planar model
  in the oracle is not needed.
- **The only unusual port traffic:** 539 reads and 11 writes of `0x388`/`0x389` (AdLib detection or register
  writes, answered with 0 by the oracle) in the first ticks, and `0x61`/`0x20` keyboard acknowledgements.
- **New oracle options:** `--palette-from FILE.raw`, `--log-vga` (see `tools/dosemu.py --help`).
- **Not known:** timing and scroll of the credits, what ends the program, whether the music is song 0 (only the
  `muc.all` read is observed), and the keys it reads.

### CAST.SL: main loop, pacing and exit (same day, from `calls16.py` on `0x10000..0x10800` and oracle runs)

- **Structure of `main` (`0x10000`, SEEN in the call list):** allocates 7 buffers, opens `cast1.lcz` / `cast2.lcz` /
  `ani.lcp` and reads them (decoding the pictures with the LCZ routine already known), calls `0x10c1e` (sound
  start-up; its result goes to a flag at `[seg 0x1a6]:4`), reads `muc.all` (seek 0:2 first, then up to 20,000 bytes)
  and starts the song with `0x10c58`, builds the sky/floor/title pictures, then runs the loop at
  `0x103fa .. 0x106af`.
- **The loop:** one pass per displayed frame. A scroll value `[bp-0x14]` grows by 15 each pass; when it exceeds
  `0x6144` (24,900) it is clamped and a second counter `[bp-0x58]` is incremented, and the loop ends when that
  counter exceeds `0x82`. So **the credits last about 1,661 passes of scrolling plus 130 held passes** (my reading
  of the code; the measured total below agrees). Each pass also
  polls the music (`0x10d1a`) when the sound flag is set, and reads the keyboard through `0x116f0`.
- **MEASURED in the oracle (no key):** the loop's exit at `0x106b2` is reached after **1,790 completed passes**
  (1,791 started; the predicted 1,661 + 130 = 1,791). In the oracle that takes about 1,576 ticks at 500,000
  instructions per tick; the real pace is not known.
- **Keys (SEEN and MEASURED):** `0x116f0` reads port `0x60` raw, with no release latch and no queue (acks
  `0x61` and `0x20`); the loop ends when the scan code read in that pass is `0x39` (Space) or `0x1c` (Enter). A press at
  tick 100 ended the run after 97 passes for both; Esc (`0x01`) did nothing. Unlike the menu, a held key is enough
  and a key already down when the program starts counts at once.
- **Exit (SEEN):** stops the music (`0x10d42`, `0x10d6a`), frees the buffers, then calls the exec wrapper
  `0x11bce` with the string `walk0.sl` (the only other program name in the file), so the credits return to the menu.
- **What I have not read:** what each per-pass call draws. Seen only by name of shape: `0x10748` expands a 1-bit-per-pixel
  bitmap into two colours (15x16 glyphs, so the staff names probably come from a bitmap font in `cast1.lcz` or
  `cast2.lcz`), `0x11653` is a masked blit, `0x114e2` a stretch (120x120), `0x1173b` / `0x11712` are copies. How the
  floor streaks are made is unknown.

### CAST.SL: the per-pass drawing, first reading (same day; SEEN in disassembly, nothing built or compared yet)

Data-segment names (file offsets checked): `DS:0x42` cast.sl, `0x4a` cast1.lcz, `0x54` cast2.lcz, `0x5e` ani.lcp,
`0x66` muc.all, `0x6e` walk0.sl. `main` reads `cast1.lcz` into `[bp-0x12]`, decodes it with the LCZ routine
(`0x10855`) into `[bp-0xe]`; reads `cast2.lcz` into `[bp-0x56]` and decodes it into `[bp-0x24]` after zero-filling
25,000 (`0x61a8`) bytes; reads `ani.lcp` into `[bp-0x48]` (first `0x600` bytes); music in `[bp-0x1a]`.

Primitives identified (address: what it does; argument order = parameter order as printed by `calls16.py`):
- `0x10748 (src, dst, nbytes, colour)`: expands a 1-bit-per-pixel bitmap, MSB first, set bit -> `colour`,
  clear bit -> 0. Called per pass with 15 bytes = one **120-pixel row** and colour `0x10`.
- `0x10b6f (sprite, n)`: scrolls a sprite (`w,h` header) up by `n` rows inside itself (copies `(h-n)*w` bytes from
  row `n` to row 0). Called with n = 1 on two sprites per pass.
- `0x11653 (ptr, count, from, to)`: recolours: every byte equal to `from` becomes `to` (in a run of `count`
  bytes). Used with count `0xf0` (two rows of 120) and `to` running through `0xdf..0xd0` and `0xd1..0xdf`, i.e. a 16-shade
  ramp, so the text colour varies by row (the fade seen at the top and bottom of the credit column).
  These palette entries must come from the inherited palette (see the earlier entry).
- `0x114e2 (src, dst, srclen, dstlen)`: nearest-neighbour resize of one line, 16.16 fixed-point step
  `0xffff * srclen / dstlen`. (`0x11519` does the same for a sprite with header.)
- `0x11560 (screen offset, sprite)`: opaque blit of a sprite to the screen; `0x115a9`: the same with colour 0
  transparent; `0x111d2`: masked byte copy; `0x11630 (ptr, count, value)`: fill; `0x1173b` / `0x11712`: copies.

What one pass of the loop does (`0x103fa..0x106af`), in order:
1. Music poll (`0x10d1a`) if the sound flag is set.
2. Scrolls the two credit-text sprites up one row; expands one new 120-pixel row of the 1-bit text bitmap into the
   bottom (source offset from the scroll value `[bp-0x14]`, +15 bytes per pass: inferred from the `add cx, ax`
   before the call, not verified).
3. Recolours the sprites row pair by row pair with the 16-shade ramps.
4. **The floor** (`0x105b2..0x1063c`): for 101 rows, resizes one 120-pixel row of the title-lettering sprite to a
   width that grows from 120 to 320 in steps of 2, writing each result 319 bytes after the previous one (so the
   rows shear by one pixel each). The source row advances only after `[bp-0x42] + 1` rows, and `[bp-0x42]` grows
   by 1 every 4 rows, which compresses the lettering vertically with distance: a perspective floor.
5. Composites (copies of 16,000 and 32,000 bytes, a masked copy) into the screen halves, then reads the key.

The start-up before the loop (`0x100ba..0x10330`) copies the screen to and from buffers in 320-byte rows, a vertical
wipe, in a pattern I have not read. Not yet understood: the exact source offsets in steps 2 and 5, the wipe, and
what gives the text its horizontal position.

Size estimate for porting (a guess from the amount of code): about the size of the menu without its input; the
primitives are small and several are known from other programs, but the composite order and buffer offsets need
the same pass-by-pass comparison with the oracle (`--loop-top 0x103fa`, about 1.1 passes per tick, 1,790 passes).

### CAST.SL: the loop's model, checked against the oracle's buffers (2026-10-16)

`tools/cast_buffers.py` runs a Python model of one pass of the main loop (`0x103fa..0x106af`) from the buffers at
the top of pass 0 and compares it with buffers dumped by the oracle at 22 later passes (0-3, 300-302, 700-702,
1000-1002, 1650/1651, 1659-1663, 1700/1701) and with 11 screen snapshots. Dumps come from the new
`dosemu.py --dump-hit ADDR:N:BPOFF:LEN:FILE` (and `--snap-hit`), run with `--loop-top 0x103fa`; the file names
and sizes are in the tool's header. **Result: A, B, C, L and F identical at all 21 dumped passes after pass 0, and
the screen identical in all 11 snapshots.** The model (all of it SEEN in the code first, then confirmed):
1. `B` last row = `A` row 0 (`0x1173b`, 60 **words**, from `A+4` to `B+0x2e6c`). Note `0x1173b` counts words, so
   `0x7d00` = 64,000 bytes (the whole screen) and `0x3e80` = 32,000 bytes (half).
2. `A` (120x30) and `B` (120x100) each scroll up one row (`0x10b6f`, n = 1); the bottom rows stay as they were.
3. `A` row 26 (offset `A+0xc34`) = text-bitmap row at byte offset `s` expanded to colour `0x10` (`0x10748`);
   then `s += 15`, at most `0x6144`. `T` (the 25,000-byte bitmap from `cast2.lcz`) never changes.
4. `C` (120x100, `A+0x4268`) = first 12,000 bytes of `B` (header + 11,996 pixels; the last 4 bytes are stale),
   then colour `0x10` becomes `0xdf..0xd1` over 15 bands of 2 rows from row 0 and `0xd1..0xdf` over 15 bands of
   2 rows from row 70.
5. `L` = rows 0-26 of `A` (3,240 bytes), rows 0-15 recoloured one by one `0xdf..0xd0`.
6. The floor `F` (320 wide, header says 80 rows but 101 are written): for i = 0..100, `L` row `r(i)` is resized from
   120 to `120+2i` pixels (`0x114e2`, 16.16 step `0xffff*120/dstlen`, accumulator starting at 1, step carried by
   `adc`) and written at `F + 0x68 + 319*i`; `r` advances after `rep+1` rows, and `rep` grows by 1 every 4 source rows
   (starting 0). `F` is not cleared between passes (rows without lettering leave older pixels).
7. Screen: `C` copied opaque to x = 100, y = 0, 120x100 (`0x11560`); the lower half is the first 32,000 bytes of
   the decoded `cast1.lcz` picture with the non-zero pixels of `F+4` (32,000 bytes) over them (`0x111d2`), copied to
   `A000:0x7d00`.

Negative controls (the same tool with one thing changed): new row written at A row 24-29 except 26, the floor
shear 320 instead of 319, the repeat rule (3 instead of 4 rows), a ramp one band short or misplaced, the copy done
after the scroll, `C` blitted at x = 101, the floor not masked, the background offset by one byte, floor row
off by one: each is caught at some dumped pass. **Not distinguishable by these dumps:** the accumulator start (0 or
1) of the resize and the clamp value `0x6144` (the text bitmap is blank there), so those two come from the code only.

Open for the port: the screen at the top of pass 0 is an input here (`S_0`) and so is the decoded background (`G_0`);
the start-up that makes them (the vertical wipe from `0x100ba`, the pictures built before the loop, the palette
question) is not read yet. Pass count, keys and exit: see the entry above.

### CAST.SL: the start-up before the loop, and the palette (2026-10-16)

- **Order (SEEN in `main`, `0x10000..0x10747`):** allocate buffers; open and close `cast.sl` (check that it exists);
  sound start-up (`0x10c1e`); read and decode `cast1.lcz`; copy the whole screen to a save buffer; the wipe;
  read and decode `cast2.lcz`; read the first `0x600` bytes of `ani.lcp`; read 20,000 bytes of song 0 from
  `muc.all`; start the music if the sound flag is set (`0x10c58`); zero the floor buffer and the sprite buffer and
  write the headers (floor 320x80, `A` 120x30, `B` 120x100); the loop. At the end: stop the music, free, exec `walk0.sl`.
- **Pictures (CHECKED against the oracle's buffers):** `cast1.lcz` is one LCZ chunk that decodes to 32,000 bytes: the
  lower half of the screen (the background of the floor), not a full screen. `cast2.lcz` is one chunk that decodes to
  **24,900** bytes, which is the text bitmap (the buffer is zero-filled to 25,000 first) and equals the scroll clamp
  `0x6144`, so the credits scroll ends exactly at the end of the bitmap. Both agree with `lcz_decode.py`.
- **The wipe (`0x101b0..0x10240`), CHECKED:** `tools/cast_wipe.py` reproduces the oracle's screen after 0, 1, 2, 3,
  50, 99, 100, 101, 102, 150, 199 and 200 steps, using a real menu screen as the old picture (new oracle option
  `--screen-from FILE.raw`). Each of the 200 steps: the old picture (saved) shifts up one row at the top; for the first
  100 steps the new background rolls up from the bottom edge of the screen (`bg[0:320*k]` at `64000 - 320*k`); from step
  101 the background stays whole in the lower half, and the row at the bottom of the shrinking old picture is
  cleared to colour 0 as it goes. At step 200 the upper half is colour 0 and the screen is **identical to the screen at
  the top of pass 0** (CHECKED). The old picture is whatever the previous program left (the menu): the port has to pass its last
  frame on. The wipe has no wait of its own, so its speed in the original is the CPU's.
  Two changes to the model that the 12 screens cannot tell apart are equivalent: the threshold `0x7d00` moved by less than
  320, and copying one row more in the first copy (the next copy or the clear overwrites it).
- **The palette (resolved as far as it can be):** the DAC at the menu is **exactly `RPG.LCP[0:768]`** (the interface
  palette; compared with the oracle's menu snapshots, 0 entries differ). `CAST.SL` never touches the DAC. It reads the first
  `0x600` bytes of `ani.lcp` (two 768-byte palettes) and computes pointers to them, but nothing in `main` uses them
  (they are freed unused), and neither equals the menu palette. With the interface palette preloaded, the credits'
  ramps `0xd0..0xdf` are gold, as on screen. So the port should show the credits with the interface palette that the
  menu leaves. NOT verified in the original itself: only that a DOS exec leaves the DAC alone, which is standard
  behaviour.
- **New oracle option:** `--screen-from FILE.raw` (start with the screen of an earlier program).

### CAST.SL: details that the port reproduces (2026-10-16, from building `src/credits.c`)

- The pass that finds the scroll finished (the 131st with the scroll clamped) leaves at `0x1049d` **before drawing and
  before reading the key**: 1,790 passes draw, the 1,791st only ends. A key is read at the **end** of a pass, after
  the drawing, so a key pressed in a pass ends the loop after that pass has drawn; Space at pass 1790 is never seen.
- All of this was compared with the oracle in `tools/check_credits.sh` (46 of 46) and the port runs the same
  numbers; see `port.md`, "Milestone 6".
