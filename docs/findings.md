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
2. `SPIC.LCZ` (91 KB) and `STORY5_1.LCZ`: why does a chunk size field look wrong? (`SPIC.LCZ` answered 2026-10-24, step 26: it is not
   chunked at all but 13 raw 100 x 70 pictures, the signs F2 shows.)
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
   `ENDING1T.LCZ`, `ENDING4T.LCZ`: unresolved. (`SPIC.LCZ` answered 2026-10-24, step 26: 13 raw 100 x 70 pictures.)
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

### The original's FM driver, seen from its OPL register writes (2026-10-16)

New tools: `dosemu.py --opl-log FILE` (an AdLib whose detection succeeds: status port `0x388` answers `0x00` after a
timer reset and `0xc0` after timer 1 is started; every write to `0x388`/`0x389` is logged as `interrupt register
value`; the PIT reload written to `0x40` sets how many timer interrupts a BIOS tick delivers, `65536/reload`),
`build/music_regs` (the same log for the port's player, from `tests/music_regs.c`), `tools/compare_opl.py` (aligns the two
on the first note and diffs tick by tick), `tools/cmf_drum_regs.py` (the driver's writes at the rhythm events of a song).

- **The driver's clock is the song's:** it sets the PIT to reload 12,428 = 96.0 Hz (`1193182/12428`); with the model of one
  interrupt slice per 1/96 s, the driver's writes for the rhythm channels of song 0 fall on exactly the song ticks of the
  events in the file (98, 114, 131, 136, 147, 153, 164, 175, ...: the first 26 events checked). So **the tempo of the
  port (96 ticks per second) is the original's**, now seen on the driver itself and not only on a recording.
- **CAST.SL plays song 0 of MUC.ALL (CHECKED):** its PIT reload and register writes are those of the song's patches
  (instrument 6 `32 11 44 00 f8 f5 ff 7f` for the bass drum, 7 for the tom, 8 for the hi-hat); the driver is the same
  code as MARK's ("FM-Music Functions, Copyright (c) Creative Technology Pte Ltd, 1990" at `0x134a0`).
- **Rhythm mode, as the driver does it (SEEN in the log, song 0):**
  - At the first note it writes `0xbd = 0xe0` (rhythm bit 0x20, with the two depth bits 0xc0 kept).
  - MIDI channel 11 (bass drum) loads both operators of OPL voice 6 (`0x30/0x33`, `0x50`, `0x70/0x73`, `0x90/0x93`,
    `0xf0/0xf3`, `0xc6`; the carrier's level `0x53` comes with each note); channel 13 (tom-tom) only the modulator of voice 8
    (`0x32 0x72 0x92 0xf2`, `0xc8`); channel 15 (hi-hat) only the modulator of voice 7 (`0x31 0x71 0x91 0xf1`, `0xc7`): these
    are the standard rhythm operators (BD: 0x10+0x13, SD: 0x14, TT: 0x12, CY: 0x15, HH: 0x11). All of this is written once,
    at song tick 0 (where the song's program changes are; not checked that nothing reloads it later).
  - A note-on writes the operator's output level (`0x53`, `0x52`, `0x51`), the pitch of the voice (`0xa6/0xb6` for the bass drum,
    `0xa8/0xb8` for the tom, `0xa7/0xb7` for the hi-hat; `0xb6` etc. WITHOUT the key-on bit) and then `0xbd` with the drum's bit
    set: bass drum 0x10 (`0xf0`), tom 0x04, hi-hat 0x01 (`0xe5` when both sound). A note-off clears that bit (`0xe0`, `0xe1`).
    Snare (0x08) and cymbal (0x02) are not used by song 0.
  - The pitch of a drum follows the note like a melodic one (note 51 is 3 semitones above note 48 on the bass drum:
    f-number `0x198` against `0x157`, same block 3; note 15 has the same f-number as 51, three blocks lower).
- **Where the port's melodic player differs from the driver (SEEN, MARK's intro song, aligned on the first note):**
  - **Voices:** the driver does not map MIDI channel n to OPL voice n. At song tick 27 it releases voices 0 and 3
    where the port releases 0 and 8, and it loads the whole instrument onto a voice at each note-on (`0x29 0x2c 0x49
    0x69 0x6c 0x89 0x8c 0xe9 0xec 0xc4` before `0xa4/0xb4`). It allocates voices dynamically. NOT yet understood: its rule.
  - **Pitch:** its f-numbers are slightly lower than the port's computed ones (577 against 580, 343 against 345 for C3), about
    9-10 cents flat. It uses a table; the port computes `440 * 2^((n-69)/12)` and rounds.
  - **Levels:** its output level for a velocity is not the port's `63 - vel/2`. For velocity 95 the log shows level 9 on a
    patch with level 0 and 33 on a patch with level 28, which fits `63 - (63 - patch) * 54 / 63`: the velocity is scaled to 54
    first (how 95 gives 54 is NOT known; one point). In the same notes the port writes other values.
  - These make the port's music differ in detail from the original's while having the same notes, tempo and instruments.
    Of the 837 ticks compared after the first note, 83 differ in at least one register (mostly `0xa0..` and `0x40..`).


### The original's FM driver, read and checked (2026-10-16)

Supersedes the list "Where the port's melodic player differs from the driver" in the entry above, and the choices the
first player made from the public CMF description. Addresses are `CAST.SL`'s (linear, load segment 0x1000); the data
segment is at file offset 0x4fd0 + 0x400.

- **Register write (0x135ad):** AH = register, AL = value, OUT to the base port (0x388 for an AdLib) then +1, with a busy
  delay loop after each (counts in data words 8 and 10).
- **Pitch (0x138cc ..):** `di = note + [0x16]` (a transposition, 0 unless set; no song sets it), clamped at 0, `& 0x7f`; `al = table[di]`
  (128 bytes at data 0xc7); block = `(al & 0x70) >> 2` merged into the high byte; f-number = the word table at 0x147
  indexed by `al & 0xf`. The note table is `block<<4 | pitch class` with block = max(0, note/12 - 1) up to note 107,
  block 7 and pitch classes repeating for 108-119, and the constant `0x7b` for 120-127. The twelve words are
  `0x157 0x16b 0x181 0x198 0x1b0 0x1ca 0x1e5 0x202 0x220 0x241 0x263 0x287`. Written as `0xa0+v` (low), `0xb0+v` (high | 0x20 for a
  key-on; without it for a drum). All 13 notes seen in the first logs fit it, then every note of 25 songs.
- **Level (0x138a5 ..):** the loader (0x1364f) stores `0x3f - (p[3] & 0x3f)` in data 0x34+v and `p[3] & 0xc0` in 0x3f+v
  (`p[2]` instead for the single-operator rhythm instruments, bl >= 7 in rhythm mode). A note-on computes `ah = (that * (0x80 | velocity)) >> 8`
  (`mul`, high byte), writes `(0x3f - ah) | ksl` to `0x43 + modulator offset`. Fits 127/0 to 1, 101/0 to 7, 95/0 to 9,
  116/3 to 6, 127/5 to 6, 95/28 to 33 (`0x21`). Velocity 0 is a note-off (0x13866).
- **Voices (0x13708):** 9 voices (6 in rhythm mode), a tag byte each (data 0x1e): 0xff free, the channel when held, `0x80 | channel` when
  released. Scans for the channel's released voice, then 0xff, then any tag > 0x7f, then steals the oldest (timestamp table at 0x60,
  clock word 0x14): it writes that voice's pitch registers without the key bit and continues. A note-on that finds
  the voice's old owner `!=` this channel loads the channel's patch (program number at data 0x9b + channel). Note-off (0x137fa) scans
  the note table (0x29) for the first voice with that note whose tag is exactly the channel, marks it released and writes
  `0xa0+v`, `0xb0+v` from the stored pitch word. Program change (0x139ea): stores the program; for a drum channel in rhythm mode loads
  the drum now; otherwise frees the channel's released voices (tag 0xff) and reloads its held ones.
- **Rhythm (0x13917, 0x139bb):** controller 0x67 calls the reset (0x135f3: clears the 16 program bytes, sets the tags to 0xff, writes
  default patches to all voices) and sets `[0x99]` (the value written to `0xbd`: `0xc0`, or `0xe0` in rhythm mode) and the voice count 9 or
  6. Tables at data 0x8a (operators `10 14 12 15 11`), 0x8f (bits `10 08 04 02 01`), 0x94 (voices `06 07 08 08 07`) for MIDI channels 11-15.
  The drum loader writes (bass drum) both operators or (the others) one operator from the patch's modulator bytes; the level register
  is not written at load.
- **Loader quirk (SEEN in the log, not explained by the code I read):** a patch load never writes the register of the operator whose
  level the note-on will set (the carrier `0x43+...`, or the single rhythm operator's `0x40+...`), although the code around
  `0x13699` looks as if it did. The register log is complete (every call of `0x135ad` is in it, in order, 0 missing; index and
  data writes strictly alternate), and the port reproduces the log.
- **Controllers (0x139a7):** only `0x66` and `0x67` are dispatched; `0x66` points at a `ret`.
- **Default instrument (0x137ba):** the init writes the 11 bytes of a built-in patch to each voice, but its last write goes to register
  `mo + voice` instead of `0xc0 + voice` (so registers `0x00 0x02 0x04 0x0b 0x0d 0x0f 0x16 0x18 0x1a` get `0x08`): a bug of the original,
  harmless on the chip, not reproduced.
- **The songs:** all 25 start with controller `0x67` (value 0 or 1) at tick 0; 22 of them change a channel's program later;
  none has an additive-connection patch (the first player wrote the modulator's level for those), none has overlapping notes on a channel,
  none uses pitch bend, 0x63 or 0x68/0x69; the most voices held at once is 9.
- **Checked:** 25 of 25 songs, every tick after the first, identical (`tools/check_music.sh`). The driver's stop routine (after the last
  event) writes `0x83=0x13` nine times and `0xbd`; accepted as a suffix of the last tick only.
- **Tooling note:** `dosemu.py --opl-log` clocks the writes by timer interrupts actually delivered (`irq_delivered`), not by the slices it
  runs: an early version counted slices, which differed by 2 in 2,063 (interrupts masked during start-up); the song-0 comparison
  done with it was unaffected, and everything after was recorded with the fixed clock.

## WALK.SL (the game world): first survey, 2026-10-16

Read-only. Nothing of `WALK.SL` is built. Evidence: the MZ header, a prologue scan of the disassembly (`work/walk/dis.txt`),
the call list of `main` (`tools/calls16.py`), three oracle runs (`dosemu.py work/unpacked/WALK.SL.exe`, which is the file itself:
it is not packed), and a sample of where the emulated CPU spends its time. The oracle was started with the menu's screen and
palette (`--screen-from/--palette-from work/check_credits/start.raw`), as for `CAST.SL`.

- **Size:** 126,865 bytes, image 119,185. Code `0x10000 .. 0x2c5xx`; entry `0x1e` in segment `0x176b`; asks DOS for all memory
  (minalloc 0xffff paragraphs). 108 functions by prologue scan (the older note says 110). **`main` (`0x10000`) is 36,828 bytes
  by itself; `0x1cbb6` is 12,624;** then 5,824, 4,728, 4,212, 3,404, 2,672. 16 functions are over 1 KB, 5 over 4 KB, the median is 108
  bytes; the 20 largest hold 86 KB. About 1,760 far calls to 160 distinct targets. For scale: the four programs built so far
  (`MARK` 24 KB, `STORY2.SL` 37, `WALK0.SL` 31, `CAST.SL` 23) are about 114 KB of code in all; `WALK.SL` alone is as much.
- **Files it names (about 25 distinct):** `mes_txt.lcc` (98 KB, text), `number.lcz` (408 KB; opened 28 times in the first tick),
  `muc.all` (music), `rpg.lcp` (the interface palette and tables, 15 uses), `snd_ef.1` (sound effects), `man_info.now` and `__1.now`
  (92,750 bytes: the state that "new game" writes from `__1.al1`, `__1.al2`, `man_info.all`), `tab.tab`, `__2.all` (259 KB), `gtab.all`
  (567 KB), `m_m.all` (160 KB), `mon.all` (489 KB, monsters?), `object.lcc`, `magict.lcc`, `mandata1.lcc`, `man_pic.lcc`,
  `shop_pic.lcc` (105 KB), `spic.lcz` (91 KB), `boat.lcz`, `ani.lcp`; and the programs it starts: `mark.exe`, `ending.sl`. About 2.9 MB of data in all.
  The role of each file is a GUESS from its name and size.
- **Start-up (SEEN in `main`, `0x10000..0x10a54` and on):** carves roughly 60 buffers out of three large allocations, then loads in
  stages: `mes_txt.lcc`, `number.lcz` (in pieces, into tables), the music, `rpg.lcp` (palette and tables of 0x300, 0x40, 0x100, 0x15e,
  0x44, 0x140 bytes), `man_info.now`, `__1.now`, `tab.tab`, `__2.all`, `gtab.all`, `m_m.all`; a second `muc.all` read takes its offset
  and length from variables (a song chosen at run time, see "Rhythm mode" above: song 3 can come from here).
  In the oracle all of it is done by BIOS tick 6.
- **What the screen shows (MEASURED):** from tick 6 the interface frame (the menu's F1-F10 buttons left and right) around a
  192 x 168 window at screen (64, 16): a top-down tile map with houses, a fountain pond, a character, rain and animated water.
  The window changes between snapshots with no input (animation). With the Down key held for 180 ticks the view scrolled
  south; with Right held for 190 ticks it scrolled east to another place (a red roof). Only the window changed (pixels in
  x 64..255, y 16..183 only; the frame is unchanged). The palette starts as the interface palette and changes in 6-11 entries.
- **Drawing primitives (SEEN, hand-written assembly at `0x2b9xx .. 0x2c4xx`, where the CPU spends nearly all its time):** an 8 x 8 tile
  copied into an off-screen buffer 224 pixels wide (`0x2c3c8`: `rep movsw` of 4 words, then 216 bytes on); a masked 8 x 8 blit
  (`0x2c4cd`: `dest = (dest & mask) | pixels`, the mask at +0x40 in the sprite); and the window copy (`0x2b9f0`: 168 rows of 96
  words from the buffer, source stride 224, to screen offset `0x1440` = row 16, column 64). So the picture is a 224-wide buffer
  (28 tiles) of which a 192-wide window (24 tiles) is shown, which is how the scroll works.
- **Input (SEEN):** the code compares scancodes: Esc 77 times, arrows 11-16 times each, Enter and Space 13 each, F1-F10 (0x3b-0x44)
  about 40 times in all, and some letters. The operator's keyboard has no function keys, so the ten buttons have to be reachable by mouse.
- **Not known:** the file formats of the map and tiles (which of `__1.now`, `__2.all`, `gtab.all`, `m_m.all` and `number.lcz` holds what), the
  game's rules, what F1-F10 do (the labels are the characters 觀看, 取物 and so on), shops, fights (`mon.all`?), the save format
  (`saveNN.000/.001/.002`), the clock/animation rates, and where `main`'s loop and the 12 KB function at `0x1cbb6` begin and end.

### Size of a port (a judgement, not a measurement)

Different in kind from the programs built so far, not just larger: those were scenes (a fixed script of pictures), checkable by
comparing the screen after N passes. `WALK.SL` is an engine: its behaviour depends on 2.9 MB of data in formats not yet decoded and on a
state (the 92 KB `__1.now`, the saves) that the oracle can load but that the port would have to understand. By code size alone it is
as large as everything built so far, and the part that is rules and menus, not drawing, has not been looked at.
A sensible order, each step checkable against the oracle the way the menu was: (1) decode the map, tile and sprite data
and draw the first view, pixel-identical to the oracle at a fixed tick; (2) scrolling with the keys, including the animation of water and rain; (3) the
interface around the window (the right-hand panel, the status line); (4) the ten actions one at a time, starting with the ones that need no new
data. Steps 3 and 4 are open-ended.

## WALK.SL step 1: the map layers and the window, 2026-10-16

Method: `tools/walk_dump.py` (new) runs `WALK.SL` in the oracle with the menu's screen and palette, logs every DOS file read with
its file offset and destination, logs calls to chosen entries, and dumps memory at the Nth arrival at a code address.
`tools/walk_layers.py` (new) is a Python model of the two map layers. Linear addresses below are the oracle's; the allocator
decides them, they are not in any file.

**Drawing one frame is `0x19680`** (SEEN in the disassembly; it is called once per frame, I did not read its callers). In order:
`0x2c05a` and `0x2be49` (not read), then
1. **Ground** `0x2c362` (called at `0x1970b`): 25 x 23 tiles of 8 x 8 pixels into a 224-wide buffer.
2. **Entities** (loop of 25 at `0x19729`): for each of 25 list entries, kind < 0x14 or >= 0x14 picks one of two record tables
   (50-byte and 40-byte records); `0x2bbec` / `0x2bc7c` turn the record into a sprite offset; `0x26c04` blits a 16 x 16 sprite
   with colour 0 transparent into the buffer (stride 0xe0). The hero is drawn here. NOT decoded: the record fields, the two
   offset routines, where the sprites live.
3. **Objects** `0x2c456` (called at `0x1996f`): the same grid over a byte map, masked 8 x 8 sprites.
4. `0x2c658` and a copy of `0x5080` words (not read). Then the window copy `0x2b9ec` (below).

**Ground layer (CONFIRMED against the oracle, 0 differing bytes of 44,800):**
- Map: 128 words per row, 100 rows. Word = tile id. Tile = 64 bytes at `id << 6` (16-bit shift) in the tile bank.
- The view is centred on (X, Y) in half-tile units: first map word is column `(X & ~1)/2 - 11`, row `Y/2 - 10`; first tile is drawn at
  buffer (16, 16); odd X shifts the picture left by 4 pixels, odd Y up by 4 rows. At the first frame X = 0x24, Y = 0x2c (36, 44).
  Odd X / odd Y were read from the code but NOT compared with the oracle (only the even case was seen).
- A second variant of the same routine (flag argument non-zero: 14 x 13 tiles at buffer offset `0x3140`, map offset +0x50c) exists. NOT seen running.

**Object layer (CONFIRMED, 0 differing bytes, using the buffer at the start of the call as the base):**
- Byte map, 128 per row, at ground map + `0x92ea`; id 0xff = none. Sprite = 128 bytes at `id << 7`: 64 pixels, then 64 mask bytes
  (`dest = (dest & mask) | pixel`). Same 25 x 23 grid and shift rules as the ground; map column is `(X - 0x16)/2` here.
- The first comparison differed by 186 bytes: that was the hero (tiles 12-13 x 13-14), drawn between ground and objects. With the buffer
  taken at the start of the objects call the difference is 0.

**Window copy `0x2b9ec`** (CONFIRMED, 0 differing pixels): screen (64, 16), 192 x 168, from buffer (16, 16). So the buffer is
28 tiles wide and the window is its middle 24 x 21 tiles.

**Where the data comes from** (SEEN in the read log at ticks 9-10; the files are in `original_bin`):
- Tiles: `GTAB.ALL` offset `0x11040`, 16,000 bytes (250 tiles), read straight into memory, no decoding.
- Object sprites: `GTAB.ALL` offset `0x14ec0`, 15,872 bytes (124 sprites), read straight.
- Ground map: `__2.ALL` offset 6, 3,681 bytes, run-length decoded (escape byte = first byte, `esc, count, value`, length word after the
  escape; the same scheme as `.LCZ`), by `0x25eeb` into a 25,700-byte buffer. Skip the first 100 bytes (a header, not read), then two
  planes of 12,800 bytes (low bytes, then high bytes); `0x2b86e` interleaves them into 12,800 words. The game read `__2.ALL` from offset 0,
  0xe67 bytes. Why those numbers: NOT known (a size/offset table I have not found).
- Object map (the layer at `0x76f00`): where its 885 non-empty cells come from is NOT known. It is NOT the `M_M.ALL` decode, which I
  first assumed from the address: `M_M.ALL` offset `0x1a4a9` (6,304 bytes) is RLE-decoded into a different 10,240-byte layer at `0x74016`
  (128 x 80 bytes, none of them 0xff, first row starts `00 .. d7 d4 d6 d9`; MEASURED that the two buffers share no byte value at the same
  position). A routine at `0x27519` writes all 10,240 bytes of `0x76f00..0x79700` at tick 10 (a word fill or copy; the source/value is
  not read, the buffer is 0xff afterwards except for the 885 cells), so the object ids are put there afterwards by code I have not read.
  What the `0x74016` layer is for (collision? events? a roof layer?) is NOT known either.
- The offsets `0x11040`, `0x14ec0`, `0x1a4a9` are fixed by the starting state (probably the area number in `__1.NOW` / `MAN_INFO.NOW`).
  How they are computed (the loops around `0x10cf0..0x11200` that sum table entries to a file position) is NOT worked out.

**How much of the picture this explains (MEASURED, one frame at tick ~21):** ground + objects reproduce 31,862 of 32,256 window pixels
(98.8%). The other 394 are: the hero (about 170 pixels, tiles 10-11 x 11-12 of the window) and about 220 scattered single pixels in
1-6-pixel groups per tile, which look like rain streaks (INFERRED from the pattern and the screenshot; the code that draws them is
one of the writers at `0x2c1d1`-`0x2c218` that start at tick 21, not read).

**Not done:** the entity list and its records (hero, villagers), the rain and water animation, the palette changes (6-11 entries),
the source of X/Y and of the file offsets, `0x2c05a`, `0x2be49`, `0x2c658`, the C port. Nothing of WALK.SL is in C yet.

## WALK.SL step 2: the entities (hero, party, villagers), 2026-10-17

Method: `tools/walk_entities.py` (new) is a Python model of the entity pass. It was compared with the oracle's world buffer
(the 224-pixel-wide one) at the moment the objects pass starts, in 11 frames: the first frame, and ten frames of a run that
holds Right from tick 30 (so the hero walks east across the village). Every one has 0 differing bytes of 44,800. `walk_dump.py`
now also prints the caller (`ret=`) for each `--call`, which is how the main loop's own copy of this code was found.

**Two copies of the frame code (SEEN).** `0x19680` drew the first frame (it was called once in the 300 ticks I ran). Every later frame
is drawn by the main loop itself, with the same calls (ground returns to `0x11e23`, positions to `0x11eac`/`0x11fe6`, objects
to `0x12311`) and the same logic. The one difference found is the hero's walk counter: `0x19680` passes the literal 1, the
main loop passes a local that counts 0, 1, 2, 3.

**The 25 entities (CONFIRMED, 11 frames).** Entity numbers 0-19 are the villagers (50-byte records), 20-24 the party (40-byte records).
- **Villagers** (`__1.NOW` bytes 0-1749, identical to memory): `[0]` x, `[1]` y in half tiles, `[2]` direction 0-3, `[0xa]` frame 0/1,
  `[0xb]` sprite set 1-5. Set 0 or >= 6 is not drawn, and neither is x = 0 or y = 0. Records 13-19 are all zero in the shipped state.
- **Party** (`MAN_INFO.NOW` bytes 0-519, with the positions overwritten by code at start-up; the file holds the last saved ones, 0x56,0x23):
  `[0]` x, `[1]` y, `[2]` direction, `[3]` character number; >= 0x64 means not in the party. Only record 0 is in the party in this state
  (number 0), so **the hero is party record 0**, and it is drawn at buffer (104, 96) in every frame.
- **View centre.** X, Y in the header are simply the hero's x, y. That settles where the first frame's (36, 44) comes from: the code puts the hero there.
  Why there, and why the file holds a different position, are not known.
- **Draw order.** `0x2c05a` collects the 25 y values and `0x2be49` sorts them with 24 passes of adjacent compares that swap on strictly greater
  (a stable bubble sort), so lower entities are drawn later and cover higher ones. The order matched the game's own list in all 11 frames.

**Where each entity goes on the screen** (`0x2bbec`). Positions are in half tiles = 4 pixels. Add 4 to x and y; subtract the view's top-left
(X - 0x16, Y - 0x14); the buffer offset is rows * 0x380 + columns * 4, i.e. 4 buffer rows of 224 per half tile. Villagers (flag 0) are
skipped when outside a box 0x34 wide and 0x2e high around the view, edges inclusive; the party (flag 1) has no box test.
The box edges were checked at the right edge (inside at 0, outside at -1), the bottom edge (both) and the left edge (both). The top edge was
NOT reached in the run, so it comes from the code only.

**Sprites.** 16 x 16, colour 0 transparent (`0x26c04`), so no mask.
- **Villagers** (`0x2bc7c`): offset = (set - 1) * 0x800 + direction * 0x200 + frame * 0x100, in a 10,240-byte bank. That bank is the `M_M.ALL` decode
  from offset `0x1a4a9` (CONFIRMED byte for byte). This is the layer I called "unknown purpose" on 2026-10-16: it is the villagers' sprites, not a map.
  Offsets >= 0x2800 would read the second bank; the largest reachable is 0x2700, so that branch and the special direction 0x61 were not seen running.
- **Party** (`0x2beca`): offset = character * 0xc00 + direction * 0x300 + pose * 0x100, in the `RPG.LCP` decode from `0x1ae37`
  (CONFIRMED). The pose is the walk counter 0, 1, 2, 3 mapped to 0, 1, 2, 1. Directions seen: 1 (down, standing) and 3 (right, walking).
  The pose changes every ~3.4 ticks even while the hero stands still (SEEN at ticks 20-27).

**What the checks cover.** The first frame, and main-loop frames 50, 53, 68, 163, 170, 173, 175, 221, 400 and 440 of a 300-tick run (`walk_entities.py frames`
lists them): hero directions 1 and 3, all four walk-counter values, 1 to 4 villagers drawn at a time, villager sets 1-4, a villager on
the right, bottom and left edge of the box. I did not tabulate which villager directions occurred.
`work/walk3/mut.py` (scratch, not kept) made 20 deliberate changes to the model and 17 were caught. The three that are NOT observable here:
the party's character stride (only character 0 is on screen), the exact cut-off 0x64 (no record holds a value near it), and the box's top edge.
Those three values come from the code only.

**Not done**
- **Rain** (the 0xa8-0xae pixels): it is NOT in the buffer at the start of the objects pass (that is why the model matches without it), so it
  is drawn later. The writers at `0x2c1d1`-`0x2c218` (routine `0x2c1ae`) are the likely ones; not read.
- **How the records change** each frame (walking, speeds, the party following): `0x2bccd` looks like the villagers' step (a walking area in `[3..6]`,
  a speed period in `[8]`/`[9]`, the direction picked by `[2]`). It was read, not run or compared.
- **Where the hero's walk counter and direction are set** from the keys, and the F1-F10 buttons.
- **The source of the object map's 885 cells** and of the file offsets are still open (see step 1).
- **Nothing is in C yet.**

## WALK.SL step 3: the first screen, complete, 2026-10-17

`tools/walk_first_frame.py check` rebuilds the screen WALK.SL shows after its first window copy (BIOS tick 21) and compares it with the
oracle: **0 differing pixels of 64,000**. The tiles, object sprites, ground map, both sprite banks, both entity tables and the rain list are
rebuilt from the game files and compared with the oracle's memory first (all identical). Inputs that still come from the oracle are listed at
the end. Palette: the first change to any entry is at tick 23 (SEEN with a per-tick palette log), after the first window copy, so the first
screen uses the palette the menu left.

**The screen is made of four things, in this order (SEEN in the write log: which routine writes which pixels, and when):**
1. **The menu's last screen** (stone frame, empty buttons, the "486" label), inherited. Taken from `work/check_credits/start.raw`.
2. **Sixteen button labels, BIOS ticks 0-2** (`0x1035f`-`0x10440`). Left column F1-F8 at x = 17, y = 4 + 20k; right column at x = 282,
   y = 60 + 20k (six labels); 是 at (273, 40) and 否 at (297, 40). Each label is drawn three times: its pixels in colour 0x8e two rows
   down, in 0x8f one row down, then the label itself (the engraved look).
3. **Bottom right, tick 9:** a 40 x 5 box of colour 0 at (273, 185) (`0x270e3`) and a 7 x 5 piece of `RPG.LCP` (offset `0x9b8`) at (289, 185).
4. **The world window, ticks 11-21:** the world buffer (ground, entities, objects, rain) and its middle 192 x 168 pixels copied to (64, 16).

**The label text.** `MES_TXT.LCC` offset `0x386` holds the Big5 string 交談觀看取物施法物品裝備使用地圖位置資料音樂音效儲存離開 (and more).
The font is `NUMBER.LCZ`: 28-byte glyphs, 16 x 14, one bit per pixel, in 30-byte cells in Big5 order:
offset = `0x3b20 + 30 * ((hi - 0xa4) * 157 + (lo - 0x40, or lo - 0x62 if lo >= 0xa1))` (the code at `0x24700`-`0x24760`; CONFIRMED: this
gives the offsets of all 28 reads, in the labels I regenerated, and inverts to 是 and 否 for the last two). The game keeps a 292-entry cache of
glyphs, which is why a repeated character (物) is read once.
The label sprite is the glyphs side by side, header (width, height) = (16 n, 14). The renderer (`0x243ec`) colours glyph rows 0-4 with 0x89,
5-9 with 0x8a, 10-13 with 0x88. Then, for two-character labels only, `0x103ab` runs `0x2741b` (replace colour 0x88 by a new colour over a byte range)
three times: 0x88 -> 0x89 over data bytes [120, 270), -> 0x8a over [240, 390), -> 0x8b over [360, 510). Net effect: rows 10-11 and part of row 12
become 0x8a, the rest of row 12 and row 13 become 0x8b, and the first six bytes of row 12 stay 0x8a because the second range reaches them first.
The lone 是 and 否 are not recoloured (rows 10-13 stay 0x88). All 16 sprites regenerated this way match the oracle's byte for byte.

**Rain (CONFIRMED, first frame and the move to the next frame).** Routine `0x2c1ae`, called from the main loop right after the objects pass:
- **List:** 75 words at `0x67470`, filled once at tick 2 with the first 75 `rand()` values after `srand(0)`. The game's generator is the usual C one
  (state = state * 0x343FD + 0x269EC3, result = state >> 16 & 0x7fff; first values 0x26, 0x1e27, 0x52f6, ...). Each word is a buffer offset.
- **Drops:** entries 0-39 are short drops, 3 pixels (colours 0xae, 0xad, 0xac); entries 40-74 are long drops, 6 pixels (0xad ... 0xa8). Each
  pixel is one row lower and one pixel to the right (offset + 0xe1). That is 330 pixels per frame.
- **Moving on:** after drawing, a short drop moves by stride / 2 and a long one by stride, modulo 0xa100. At the first frames the stride is 0x384
  (the one-frame-later list matches the oracle's: +450 and +900).
- **Stride and weather:** the main loop reads the BIOS tick counter (0040:006c) through `0x25df2`. Below 0x5000 it draws rain with stride
  0xe1 * n + 0x384, where n = ticks >> 10 (or 20 minus that when above 10); between 0x5000 and 0xa000 nothing; above 0xa000 it
  calls `0x2c2c2` instead (150 + 150 bytes decremented by 2; not understood) and no rain. So the weather follows the real clock; at the start n = 0.
  This was read from code and seen for the first frames only. The mode byte 2 of `0x2c1ae` (a 2-pixel and a 4-pixel drop, colours 0x10-0x16,
  probably snow) was read and never seen.
- **My earlier guess** (step 1) that the "scattered single pixels" were rain was right, and the 330 count is exactly 40 x 3 + 35 x 6.

**Start-up state that changes the view.** The loader at `0x115c0`-`0x11b60` walks 15-byte records in `__1.NOW` (from byte 1000 on) and patches the
maps. One of them changes the first view: record at `__1.NOW[1195:1210]` (x 30, y 15, kind 2) writes tile 0x19 over ground word 1935
(it was 0xf5). I reproduce that one record only. The same loader writes the object map (at `0x11704`-`0x1171c`: object id = `rec[0xa]`, cell =
`(rec[9] << 7) + rec[8]`), so it is very likely the source of the 885 object cells; not derived yet. The party's start positions (0x24, 0x2c) are
written by code at start-up (step 2).

**Controls.** `work/walk4/mut.py` (scratch) made 25 deliberate changes: 22 caught. Three are not visible in this frame: the rain wrap value
(no drop has wrapped yet), and the first byte of recolour range 1 and of range 3 (the neighbouring ranges already cover them).
Those values come from the code only.

**Inputs that still come from the oracle:** the object map (885 cells), the menu's last screen, and the facts that the hero's walk counter is 0 and
the rain stride is 0x384 in this frame.

**Not done:** frames after the first (the palette cycling starts at tick 23; the weather clock; villager and hero movement; input), the
object map's loader, `0x2c658` and the other routines between the world pass and the window, the F-key actions. Nothing is in C yet.

## WALK.SL step 4: the start-up scene loader, 2026-10-17

`tools/walk_scene.py` rebuilds what WALK.SL's start-up code (`0x10b00`-`0x11c17`) makes from the game files: the ground map, the **object map**, the tile
set, the object sprites, sprite bank 1, both state tables, the 100-word list and the party trail. `check` runs the original in the oracle to the end of
the record loop (`0x11c17`) and compares each buffer: **identical**. `scenes` does the same for all 53 scene numbers with the shipped state files:
**53 of 53 identical**. `fuzz N SEED` randomises the state files and compares again: **84 of 84 identical** (120 variants at seed 7; 36 skipped, see below).
`tools/mutate_scene.py` applies 36 deliberate bugs to a copy of the model: **36 of 36 caught** (about 12 minutes).
`walk_first_frame.py check` now takes the object map and ground map from this loader and still gives 0 differing pixels of 64,000.
All of this was SEEN in the oracle on the shipped files; everything below marked READ comes from the disassembly only.

**Where the object map comes from (CONFIRMED).** It is not made by the records. The 12,800 bytes at `[25700:38500]` of the decoded `__2.ALL` block are
copied (`0x27503`, word copy, seen in a write trace with `tools/walk_wtrace.py`: 5,120 word writes, one writer, tick 10) and are byte-identical to the
oracle's object map. The records then patch it. Last step's "885 cells" are the 885 non-`0xff` bytes of that block plus the patches of the shipped state.

**Which scene, and the files (CONFIRMED for scene 0 and all 53 scene numbers on the shipped data).**
- **Scene number:** `MAN_INFO.NOW` byte 500 (the loader copies bytes 500-503 to 510-513; 512 = hero x, 513 = hero y / 2). `__1.NOW` holds one
  1,750-byte block per scene (20 villager records of 50 bytes, then 50 scene records of 15 bytes).
- **Map:** `TAB.TAB` is an index of 5-byte entries (offset = byte 2 << 16 + word 0, length = word at +3). Entry `5 * scene` locates the scene's block in
  `__2.ALL`. The block starts with a 6-byte header (tile set, sprite-bank number, song, ?, weather mode, ?), then the run-length data (same code as
  `.LCZ`). Decoded: `[0:100]` first half of a 100-word list, `[100:25700]` ground map (two planes), `[25700:38500]` object map, `[38500:38740]` and
  `[38840:39080]` 240 bytes each (not understood), `[38740:38840]` second half of the list.
- **Tiles and sprites:** entry `0x109 + 10 * tile set` in `TAB.TAB`: GTAB.ALL offset, tile length (word at +3), sprite length (word at +8). This is where
  `0x11040`, `0x14ec0` and the read sizes come from, which was open at step 1. For scene 0: offsets `0x11040` / `0x14ec0`, lengths `0x3e80` / `0x3e00`.
- **Bank 1:** `M_M.ALL`, entry `0x334 + 5 * n` of `TAB.TAB`, n = header byte 1 (minus 5 when above 0x13). Scene 0: `0x1a4a9`, `0x18a0`.
- **Song:** `RPG.LCP` at `0x7add + 5 * header byte 2` holds an entry in the same form that locates the song in `MUC.ALL` (scene 0: `0x1e085`, `0x2593`).
  The read is matched to the oracle's file log; the music itself is not part of this tool.
- **Start position:** the party's five records and a 13-entry trail get (x, y, 1) with x = byte 512 and y = 2 * byte 513. So the first frame's (36, 44) is
  `MAN_INFO.NOW` bytes 502-503 = (0x24, 0x16): **answered**, it is stored, as bytes 502-503 (bytes 0-1 of the file, 0x56 0x23, are the last saved position, which the loader overwrites).

**The 50 scene records (READ from the code, CONFIRMED by the 53 scenes and the fuzz).** Kind = byte 2; `flag(i, b)` is bit b of `MAN_INFO.NOW[200 + i]`.
Full rules are in the docstring of `walk_scene.py`. In short: kind 2 writes a word (object map) or two words (ground map) at a cell; kind 0xa and 0xc
patch the ground map / object map when a flag is set; kind 0xb sets flags when a party member holds an item; kind 0xd patches the state table itself;
kind 0xf adds ids to the 100-word list; kind 0x10 starts a cut scene. Before the loop, if `RPG.LCP[0]` (the CPU speed class) is below 2, every
villager's byte 8 is reduced and byte 9 cleared.

**Corrections made while building it (my mistakes, each found by the oracle comparison):**
- I first took the six header bytes from the decoded data; they are the file's own bytes before the run-length data.
- Kind 2 copies through a word-count routine, so it writes 2 bytes to the object map and 4 to the ground map, not 1 and 2.
- In kind 0xb, a party member whose status byte is above 10 ends the search for that item, it does not skip to the next member.
- The flag test (`0x2c158`) shifts by `bit + 1`; the shift count is masked to 5 bits, and a count of 0 leaves the carry flag as the preceding compare left it.
  The model reproduces this; whether real DOS-era hardware (8086) would differ is not something I tested (the oracle is a 386+ emulator).

**What the checks do not cover.**
- **Out-of-range writes (32 of 120 fuzz variants):** a record can point past its map. The original then writes into whatever follows; the model
  raises and the variant is skipped. Nothing in the shipped data does this.
- **Cut-scene records (kind 0x10) and the `0x81` byte** at the start of `0x11315` run code that is not modelled: such variants are skipped (4 of 120 fuzz
  variants, none of the 53 shipped scenes).
- **What the fuzz varies:** the scene number, the hero position, the flag bytes, the party's status and item bytes, `RPG.LCP[0]`, and a third of the scene
  records (kind and fields). It does not change `TAB.TAB`, `__2.ALL` or the other data files, so those formats are checked by the 53 scenes only.
- **Not understood:** the two 240-byte blocks of the decoded map; header bytes 3 and 5; why the state file's own position differs from byte 502-503.
- **Not looked at:** music start-up, the weather clock, and what the shipped `__1.NOW` holds for scenes other than 0 (the model matches the oracle there,
  which shows the code path, not that the contents make sense).
- The record rules were written from the code; the 36 deliberate bugs are all caught, but this is a model of one start-up path, not of the whole game state.

**Inputs that still come from the oracle for the first screen:** the menu's last screen (`work/check_credits/start.raw`), the hero's walk counter
(0) and the rain stride (0x384) in this frame. Nothing is in C yet.

**New tools:** `walk_scene.py` (`show`, `check`, `scenes`, `fuzz`), `mutate_scene.py`, `walk_wtrace.py` (log every write into a memory range with the
writing instruction). `walk_first_frame.py` now uses the loader.

## WALK.SL step 5: the C port of the first screen, 2026-10-17

Built from the verified pieces of steps 1-4 (details and checks in `docs/port.md`, "Milestone 7a"). Results worth recording here:
- The port's first screen is **identical** to the original's at BIOS tick 21 (pixels and palette), over the port's own menu screen.
- It is also identical when drawn over the menu's *first* frame instead of its pass 3: outside the 192 x 168 panel the menu screens are the same picture
  and the window covers the panel exactly, so the hand-over from the menu does not matter for this frame.
- The labels need `MES_TXT.LCC` bytes `0x386..0x3bd` (fourteen two-character labels, 4 Big5 bytes each); the lone 是 / 否 are not in that read,
  so the port writes their Big5 codes (`AC 4F`, `A7 5F`); where the original gets them is still not found.
- A hex slip of mine (0xe90a - 0x9470 is 0x549a, not 0x5496) was found by a unit test failing on correct code; it concerns only the test.
- The shipped `__1.NOW` and `MAN_INFO.NOW` are used as they are; the new-game step that writes them from `__1.AL1`, `__1.AL2` and
  `MAN_INFO.ALL` is not ported.

## WALK.SL step 6: the idle main loop, frame 21 onward, 2026-10-17

What WALK.SL does pass after pass at the loop top `0x11d36` with no key and no mouse. Method: `tools/walk_idle_trace.py` runs the original in the
oracle (`--loop-top` style: the BIOS clock is `base + pass / 3`) and records, at every arrival at the loop top, the main frame's locals, the data
segment's counters, the villager / party / animation tables, the rain list, the rand state, the screen and the palette. `tools/walk_idle.py`
(`check`) is a Python model of one pass compared with those records; `walk_idle.py cross` compares the C port (`build/walk_dump idle`).
Pass numbering: arrival n = n passes completed. Arrival 1 is the first screen of step 3 (0 differing pixels), so the first screen is the window
copy of pass 0, and "frame 21" onward is arrival 2 and later.

**Result (CONFIRMED):** the model and the C port both reproduce the oracle's villager table, party table, rain list, rand state, palette buffer,
DAC, both animation tables, the specks list and the screen **at every arrival**, over 5,593 passes at seven starting clock readings (1,499 passes
from clock 20, and 599-899 from 4096, 11264, 20224, 28672, 40704 and 45056).

**What an idle pass is made of** (addresses in WALK.SL; the order is the original's):
1. `0x2b898`, `0x2b99a`: animated tiles. These are **the two 240-byte blocks** of the decoded map that were "not understood" at step 4:
   `[38500:38740]` is a table of up to 30 ground-tile animations (8 bytes: ground-map byte offset, period, counter, first tile, last tile;
   an offset of 0 ends the list), `[38840:39080]` up to 40 object animations (6 bytes: offset, period, counter, first id, last id). A ground
   tile steps every `period + 1` passes and goes from the last tile back to the first; an object's counter must hit the period exactly.
   Scene 0 has 23 ground entries (the pond: first/last tile 0x76/0x7e and others) and 2 object entries.
2. `0x1c284`: **palette cycle.** Five counters at data segment `0x532..0x536`, all counting every pass from 0, act when they pass 3 / 10 / 6 / 12 / 8:
   `0x533` rotates palette-buffer entries 1-3 forward, `0x534` entries 4-6 forward, `0x535` entries 7-10 backward, `0x536` entries 11-14 backward
   (`0x2c71d`, `0x2c795`), `0x532` steps entry 15's green byte by 7 (`0x2c82c`: up while its red byte is 1, a landing exactly on 0x3f sets the red
   byte to 0 and it falls by 7 until it is 1 or less, then rises again; the byte wraps at 256 and the DAC keeps 6 bits). Each time `0x2c85e`
   writes 48 bytes to DAC entries **0 to 15** after a wait for the retrace. The palette buffer's first 48 bytes are `RPG.LCP[0:48]`.
   (It was `0x2c85e` that I first read as starting at DAC entry 1: `tools/calls16.py` does not print `xor al, al`, which is at `0x2c88c`, and
   the same hid the `xor al, al` at `0x2c849` that sets the ramp's red byte to 0. The oracle's DAC log showed both; the tool is a reading aid.)
3. The picture: ground `0x2c362`, entities (existing model), objects `0x2c456`, then, by the clock below, the dark specks `0x2c2c2` or the rain
   `0x2c1ae`, then the window copy `0x2b9ec`. The ten entries at `[bp-0xcc]` (frame, x, y, direction; moved by `0x2ba82`, drawn as 8 x 8 sprites)
   are all switched off (frame 0xa) in an idle pass: nothing starts them here.
4. Villagers: for each of the 20 records `0x2bccd`. Fields: x, y (half tiles), direction (0 up, 1 down, 2 left, 3 right; 4-7 stand still;
   `0x61` a timer), limits x [3] .. [5], y [4] .. [6], [7] a counter, [8] the speed (a step every [8] calls, counted in [9]), [0xa] the 0/1 frame,
   [0xb] the sprite set (0 or 6+: the record is skipped). A step moves one half tile towards the direction unless at the limit, **then tests the tiles
   it would stand on**: the cells at the new position (two, a third when x is odd, the next row's when y is odd) must all be on the scene's list of
   100 tile numbers (`__2.ALL`'s first 100 bytes plus `[38740:38840]`, extended by scene records of kind 0xf): this is a list of **walkable** tiles, a
   tile not on it blocks the step. The scan is `repne scasw` and the code takes a count of 0 as "not found", so **an entry in the 100th place
   never counts**. Then [7] += 1; it returns that value (and resets [7] to 0 when above 10). When the value is 10 the caller runs `0x134ba`: [7] = clock & 7,
   then `rand()`: below 0x2aaa go left / right towards the hero's x, above 0x5554 up / down towards the hero's y, otherwise (or when it is raining,
   or when byte 0x31 of the record is 0) a second `rand()` / 0x1000 (0 to 7) picks the direction. The special return 0xc (direction `0x61` timing
   out) is not built.
5. The hero's walk counter: `[bp-0x3c]` counts passes and resets above `class + [bp-0x2e]`, `class` = `RPG.LCP[0]` (2) and `[bp-0x2e]` = 3 while nothing is
   pressed, so the frame `[bp-0x5e]` advances every 6th pass, 0 1 2 3 (the offsets 0, 0x100, 0x200, 0x100 as before).

**Clock, weather and the random lists** (CONFIRMED over the seven clock readings). `0x25df2` reads the BIOS clock; the main loop uses **its low 16 bits**.
At weather mode 1 or 2 (scene header byte 4; scene 0: 1): below 0x5000 it rains with stride `0xe1 * n + 0x384`, n = clock >> 10 (20 - n above 10);
from 0x5000 to 0xa000 it is dry; above 0xa000 `0x2c2c2` draws 300 dark specks (each darkens its pixel by 2, first 150 move down-left by 0xdf, the other
150 up-right, with wraps at 0xa0f0 / 0xe10) and there is no rain. So the weather has a period of 65,536 ticks (3,599.6 s, an hour of the BIOS clock): 18.75 minutes of rain,
18.75 dry and 22.5 with the specks (I read these as night, but that is a guess). The specks can reach offset 0xaf00, past the end of the 224 x 200 buffer; the
original writes into whatever follows, the port into a buffer made 0x400 bytes longer (nothing in the window shows it over the passes checked).
At start-up `srand(clock)` (the program reads the BIOS clock first thing), then `0x10610` draws the 75 rain values and `0x10641` the **second list of
300 distinct words** (`rand() + 0xe10`, a repeat is drawn again): 378 draws in all for seed 0; the model reproduces the rand state at pass 0 exactly.
In the oracle the first screen's clock is 0, so there is rain with stride 0x384 and the specks are only reached with a clock above 0xa000.
Villagers' decisions use the same clock (the low 3 bits) and, for rain, the same stride.

**Not built / not seen:** keys, mouse, the party loop (`0x120a3`), hero-villager contact (`0x2c0da`), the special return 0xc of `0x2bccd`, weather
mode 2 (the port draws nothing for it), the call at `0x1c346` (only when a byte at `[0x38c]:5` is 0x81; not reached here), other scenes' idle
loops (only scene 0 was run), what the ten effect entries do. Only the 23 + 2 animation entries of scene 0 were exercised, so the 30- and 40-entry
limits come from the code.

**New tools:** `walk_idle_trace.py` (records the oracle's passes, zlib-compressed pickle), `walk_idle.py` (`check` the model, `cross` the C port).
`check_walk.sh` runs the seven traces (about a minute; `IDLE=0` skips them).

## WALK.SL step 7: the arrow keys, 2026-10-17

The hero walks. `tools/walk_idle.py` (the Python model, now with a keyboard port value per pass) and the C port reproduce the oracle's passes
exactly with the four arrow keys: 21 scripted or random key sequences, about 18,000 passes in all, thousands of steps and bumps and dozens of
scene-record hits, at seven BIOS clock readings. Compared after every pass: screen, palette, villager table, party table (all five records), trail, rand
state, rain and specks lists, palette buffer, animation tables and the hero's counters. SEEN = in the oracle's traces
(`tools/walk_idle_trace.py --key PASS:CODE`; `tools/walk_keys.py` makes the scripts); READ = from the disassembly only.

**Keys (SEEN for the arrows, READ for the rest).** The loop reads the keyboard port raw each pass (`0x274b8`), so a key is its make code while
held and its break code (make + 0x80) after release; the code lands in `[bp-0x58]`, the previous one in `[bp-0xc2]`. The logic (`0x135f0`-`0x13ad9`):
- a break code 0xc8 .. 0xd0 does not release the held arrow unless it is that arrow's own (0xc8 up, 0xd0 down, 0xcb left, 0xcd right);
  otherwise the held code is used again;
- 0xe0 (the extended prefix) also means "the held key"; any code above 0x7f clears the last key and ends the pass's key work;
- the make codes 2, 3 and 0x1e (the keys 1, 2, A) are treated as releases while they repeat; the last key is then set to the new one;
- the arrow handlers run only when the counter `[bp-0x98]` has just wrapped to 0: it counts up to the speed class, so **every third pass**
  (`class + 1`), in the order up, down, left, right. A handler that sets `[bp-0x58]` to a new value is followed by the later handlers, which
  test it again: the success value 1 or the failure value 0 ends the matching. (Sliding sets 0x4b/0x4d from up or down: see below.)

**A step (SEEN).** The direction goes into the hero's party record (byte 2: 0 up, 1 down, 2 left, 3 right), `[bp-0x2e]` becomes 1, the coordinate moves
by one half tile and the same footprint test as the villagers' is made on it (`0x2bf02`: the cells under the hero on the list of walkable tiles).
- **Allowed:** `0x2bfd5` shifts the 13-entry trail one place on (entry k+1 gets entry k), puts (x, y, direction) in front, sets party record 0 to
  (x, y) and gives records 1-4 the trail entries 3, 6, 9 and 12 (so the followers walk the hero's path, 3 steps apart).
- **Blocked:** the coordinate is restored, `[bp-0x2e]` becomes 5 and `[bp-0x48]` becomes 2: the bump.
- **The walk frame:** `[bp-0x2e]` is also the walk-frame counter's limit: the pass end resets `[bp-0x3c]` and advances the hero's frame when it
  exceeds `class + [bp-0x2e]` (3 at the start, 1 on the pass that made a step, 5 after that and **for good**: nothing sets it back to 3, so after the
  first key press an idle hero changes frame every 8th pass, not every 6th).

**The bump (SEEN).** For `[bp-0x48]` passes (2, then 1) the window is copied from a little way into the world buffer: with `t = 4 * [bp-0x48]`,
`d = 0x7fff / t + 1`, `a = rand() / d`; when `a` is above `[bp-0x48]` the window is read `(a - [bp-0x48]) * 224` bytes in (1 to 5 rows down), otherwise `a`
bytes in (0 to 2 pixels). It draws from the game's `rand()` (shared with the villagers), so a bump changes every villager's later choices; the model
and the port do the same, and the rand state agrees at every pass.

**Scene records under the hero (SEEN).** In the pass after a step (`[bp-0x2e]` is 1 at `0x12051`), `0x2c098` looks for a scene record whose
x (low bit cleared) and y/2 equal the hero's; a kind 5 record under odd coordinates is not a hit. On a hit the code jumps to `0x172f0`, which
handles the record's kind, and then back through `0x10bf9` to **`0x113c4`: the loader's record loop, run again on the live state** (maps, villager
table, flags, list of walkable tiles); the rest of that pass is skipped: no rain, no window copy, no villager step, no key work and no counter
update (`jmp 0x172ed`). The picture stays as it was for that pass. The record loop is now a function in the model (`walk_scene.apply_records`) and in C
(`walk_scene_apply_records`).
- **Kind 3:** the roof of a house; built in the next step ("WALK.SL step 8").
- **Other kinds:** the block names kinds 1, 4, 5, 7, 8, 0xe and 0x11-0x14 (**not built**, reported). The other kinds (2, 6, 0xa, 0xb ...) are
  taken to do nothing but the re-entry; that is READ (the dispatch has no case for them), and in scene 0 only kind 3 was stepped on.

**Not built at this step.** Sliding round a corner (`[bp-0x76]`, toggled by the A key: the four handlers then test positions two cells to the side and
substitute a sideways move; READ); the no-clip toggle (Backspace after the key left of 1: the arrow keys then move one half tile with only a clamp
to x 0x16 .. 0xe4, y 0x16 .. 0xb0; the developers' test mode, READ); every other key, the mouse and the ten F-key actions; hero-villager contact
(`0x2c0da`); the scene changes of kind 1 records. Stepping off the map's edge is not modelled (the original reads whatever follows the map).

**New or changed:** `tools/walk_idle.py` (`check` / `cross` take the key script from the trace), `walk_idle_trace.py --key`, `walk_keys.py`,
`walk_scene.py apply_records`; `check_walk.sh` runs eight key scripts after the idle traces (about 2.5 minutes in all).

## WALK.SL step 8: the roof of a house, 2026-10-17

The records of kind 3 take a house's roof off when the hero goes up through the door and put it back when he comes down. Built in the Python
model and in C; the door of scene 0's first house, walked through in both directions, matches the oracle at every pass (screen, palette,
all tables, the roof flag and the weather mode) at three clock readings (rain of strength 0, rain of strength 4, and the dark specks).

**The record** (15 bytes, kind 3; pairs of records one row apart): bytes 0-1 the position (x, y/2), byte 3 = 0 on the row where the hero enters
and 1 on the row where he leaves, bytes 4-7 the roof's rectangle (x0, y0, x1, y1) **in the object map** (128 columns). Scene 0's first house:
records 1 (38, 21) and 2 (38, 22), rectangle x 12 .. 21, y 11 .. 21 (10 by 11 cells of 8 x 8 pixels).

**What happens** (READ at `0x17b0f` .. `0x17c32`, `0x1b634`; SEEN in the oracle):
- **Going in:** the hero faces up (party byte 2 = 0), the record's byte 3 is 0 and the flag byte `alt[0]` (the first byte of the buffer `[bp-0x9e]`)
  is 0. `0x1b634` copies the rectangle of the object map row by row into `alt` from byte 1 on and fills it with 0xff in the map (no object:
  the roof is gone and the room shows), `alt[0]` becomes 1, the weather mode `[bp-0xd8]` gains 3 and `0x1c68e` is called.
- **Coming out:** the hero faces down (byte 2 = 1), the record's byte 3 is 1 and `alt[0]` is not 0: the rectangle is copied back, `alt[0]`
  becomes 0, the bump (`[bp-0x48]` = 2) starts and the weather mode loses 3.
- **The weather:** scene 0 has mode 1 (rain by the clock). Mode 4 draws neither rain nor the dark specks, and the villagers' direction choice sees
  stride 0, so indoors the rain stops and the villagers decide as in dry weather.
- **After either:** the record loop runs again on the live state (step 7), as for every record.
- **Not built:** `0x1c68e`. When the sound card state at `[0x388]:0xc` is 0x81 it stops the music and starts another song (pointer `[bp-0x4c]` + 0x127d):
  the inside and outside themes. WALK.SL's music is not played by the port at all yet, so there is nothing to change; the oracle's run has no
  OPL answering at that point, so it was not seen either. The other three roofs of scene 0 (records 3-8 at (86, 34/35), (90, 47/48), (106, 34/35))
  use the same code but their doors cannot be reached at the start: the cells in front of them are not on the list of walkable tiles until a
  flag has patched the map (the kind 0xa record 10 at (86, 36)), so they were not walked through.

**Tools:** `walk_route.py` plans a key script through given waypoints with the model (a breadth-first search over walkable half-tile positions,
the model walking it pass by pass). `walk_keys.py` has two new scripts: `door` (right to x = 38, up through the door, down again) and `route`
(a tour of the streets: (60, 46), (86, 73) and back to (40, 46); 152 steps, five records stepped on). `walk_idle_trace.py` records the roof flag byte.

## WALK.SL step 9: the scene's music, 2026-10-17

WALK.SL plays the song its map names, through the same FM driver as the other programs. Checked in the oracle with an AdLib model that answers
(`dosemu.py --opl-log`): the register writes of the original over the whole first pass of scene 0's song are **identical** to the port's player for
all 6,180 song ticks (`compare_opl.py`: 6180 identical, 0 different), the driver's own reset writes at tick 0 aside (known, step 5b).

- **Which song:** `RPG.LCP` at `0x7add + 5 * (map header byte 2)` locates it in `MUC.ALL` (scene 0: offset `0x1e085`, `0x2593` bytes, 2,410 events, 96 ticks a
  second, ends at tick 6180, about 64 s). The loader already found it (step 4); it reads it at BIOS tick 10, the first note sounds 11 driver ticks after the
  driver starts, and the first screen appears at tick 21.
- **A second song is read and not played:** the loader also reads `MUC.ALL` at `0x2bad9` (`0x1c92` bytes), the menu's song, at tick 9. In the oracle's runs
  (300 passes of walking, a trip through the house door and back) it is never started. What it is for (an event, the other side of a door) is not known.
- **The loop:** at the end of the song (interrupt 6191, song tick 6180) the driver writes its stop routine (nine writes to register 0x83, the same as at the
  end of every song). The main loop polls a flag the driver sets (`[0x384]:6`, tested at the loop top `0x11d3a` together with `[0x380]:0xc` and
  `[0x24e]`), and calls `0x2914d` (stop) and `0x29129(0)` (play from the start); in the oracle the song starts again at interrupt 6199, **8 ticks (83 ms)
  after the end**, with the same 157 reset writes as the first time and the same notes after it. That gap is however long the main loop took to come
  round, which is the machine's speed (about one pass); the port starts the song again at once, as it does for the other scenes. GUESS (the gap is an
  artifact of the unpaced loop).
- **The roof does not change the music.** The call a roof makes (`0x1c68e`) tests `[0x388]:0xc` = 0x81 and then stops the sound and starts the sample at
  `[bp-0x4c] + 0x127d` with `0x2917c`: this is the **digital sound** (Sound Blaster) driver, not the FM one; the oracle has no Sound Blaster, so the flag is 0 and
  nothing happens there. With a Sound Blaster the door gives a sound effect (probably from `SND_EF.1`, which WALK.SL does not read in the oracle's runs, and the menu
  reads on a real machine). Not seen, not built.

## WALK.SL step 10: the other keys, a survey, 2026-10-17

Read-only survey; nothing built. READ = the dispatch in `main` after the arrow handlers (`0x1424c` .. `0x172a4`, via `tools/calls16.py`). SEEN = one key
pressed in the oracle at the start position of scene 0 (`dosemu.py --key 26:CODE:30 --snap 59:...`, and `--loop-top 0x11d36 --key-pass 9:CODE` to see whether
the main loop keeps running). A handler that waits for an answer stops the main loop's passes, so "waits" below means the oracle's pass count stopped.
Each F-key handler first highlights its button with `0x1c3ba(screen offset, 0x2f, 0x14, ...)`; the offsets match the label positions of step 3, which is
how the key-to-button mapping was read (high confidence). F-keys are handled only between 0x3b and 0x44 (`0x14d4c`), after the screen is redrawn once.

| Key (scan code) | Button (screen offset of the highlight) | What the oracle showed | Files opened |
|---|---|---|---|
| F1 (0x3b) | 交談 talk (0x284) | nothing: no one to talk to at the start; the loop goes on | - |
| F2 (0x3c) | 觀看 look (0x1b84) | nothing visible; the loop goes on | - |
| F3 (0x3d) | 取物 take (0x3484) | nothing visible; the loop goes on | - |
| F4 (0x3e) | 施法 magic (0x4d84) | a selection box over the hero (pick a party member); waits | - |
| F5 (0x3f) | 物品 items (0x6684) | the same selection box; waits | - |
| F6 (0x40) | 裝備 equip (0x7f84) | the same selection box; waits | - |
| F7 (0x41) | 使用 use (0x9884) | the prompt 金錢 或 物品 ("money or item?") at the bottom of the window; waits | `mes_txt.lcc` |
| F8 (0x42) | 地圖 map (0xb184) | nothing visible (no map item, probably); the loop goes on | - |
| F9 (0x43) | 位置 position (0x498d) | a small box at the hero (probably the party's marching order); waits | `rpg.lcp` |
| F10 (0x44) | 資料 data (0x628d) | the hero's status panel over the left buttons: portrait and six values (HP, MP and others); waits | `mandata1.lcc`, `mes_txt.lcc`, `man_pic.lcc` |
| 1 (0x02) | 音樂 music (0x7b8d) | music on/off: tests the FM flag `[0x37e]:5` (0x81 = playing; `0x266f6` stops, `0x2660c` starts); the loop goes on | - |
| 2 (0x03) | 音效 sound effects (0x948d) | sound effects on/off (`[0x380]:0xc`, the Sound Blaster flag); the loop goes on | - |
| 3 (0x04) | 儲存 save (0xad8d) | 請輸入欲存編號(0-10) ("enter the save number, 0-10") with a number field at the top; waits. The code after it writes the state files (`0x26c54` with mode 1 = create, `0x26cf5` = write) | `mes_txt.lcc`, `rpg.lcp` |
| 4 (0x05) | 離開 quit (0xc68d) | 確定離開遊戲嗎? ("really leave the game?"); waits | `mes_txt.lcc` |
| A (0x1e) | - | sliding round corners on/off (`[bp-0x76]`, step 7); recolours a 0x1ec-byte strip of the screen between colours 9 and 0x1f as the indicator | - |
| Space (0x39) | - | runs code that tests a party record against 0x64 and draws random numbers; nothing visible here; the loop goes on. Not understood (an action on what the hero faces?) | - |

So the right-hand column of buttons is 位置/資料 on F9/F10 and 音樂/音效/儲存/離開 on the number keys 1-4 (the labels carry no key names; this mapping is
from the highlight offsets). 是/否 (yes/no) are the answers to the prompts; how they are given (keys or the mouse) is not read. The mouse: not read.

**Not known:** what Space does; what F1-F3 and F8 do when there is something to act on; the dialogs' own input loops; the save file layout written by key 3.

### Operator's account of Space and enemies, and what it explains (2026-10-17)

The operator, from playing the original: **Space shoots a projectile. Outside the town, where there are enemies, a shot destroys an enemy; walking into
an enemy starts a normal turn-based battle.** This matches several pieces read earlier and not understood (INFERRED from the operator's account plus the
code; none of it has been run with enemies present, because scene 0 has none):
- **The ten entries at `[bp-0xcc]`** (4 bytes: frame, x, y, direction), drawn as 8 x 8 sprites from `[bp-0xd4]` in the idle pass and moved one step a pass in
  their direction by `0x2ba82` (step 6, "all switched off (frame 0xa) in an idle pass"), are very likely **the projectiles**. Space would start one at the
  hero, facing his way (the Space handler at `0x14252` writes such an entry with `0x274da` and draws from `rand()`; READ). Up to ten at once.
- **The villager-table records that are enemies** in other scenes: the step-6 decision rule (`0x134ba`) moves a record towards the hero when its byte 0x31 is
  set; for an enemy that is "chase the hero". The special direction **0x61**, a 20-pass timer that then turns the record to direction 1 and returns 0xc
  (`0x2bccd`), drawn with its own sprites (`0xe80a` + frame, the second bank), is very likely **an enemy being destroyed** (an explosion or fading
  animation), and 0xc the "it is gone" case at `0x13356` (not read).
- **`0x2c0da`**, called for each party member each pass (step 7, "hero-villager contact"), looks for a record at the same position (both coordinates with the low
  bit cleared) that is not in direction 0x61 and has byte 0x31 above 0: that is **"did the hero touch a live enemy"**, the start of a battle. `0x18fdc`,
  called from several key handlers and this path, is a candidate for the battle itself (not read).
- Scene 0 (the first town) has no records with byte 0x31 set where the hero can reach them, which is why none of this ran in the oracle's traces.

To check this: find a scene with enemies (byte 0x31 set in its villager records, `walk_scene.py scenes` loads all 53), start the oracle there (a state file with
`MAN_INFO.NOW` byte 500 = that scene), press Space and walk into one, and trace `[bp-0xcc]`, the records and `0x18fdc`.
- **Which scenes have them** (the loader on the shipped state, `MAN_INFO.NOW` byte 500 set to each scene; records with byte 0x31 set and a visible sprite
  set): scenes 1, 3, 4, 7, 8, 9, 10, 11, 12, 24, 25, 29, 34, 40, 42 and 47 (7 to 20 such records each); scene 0 has none. So scene 1 is the obvious first place
  to look. MEASURED with `walk_scene.load(..., mi_override=...)`; that these records are enemies is the inference above.
- The operator agreed with this reading (2026-10-17): the projectile list, enemies chasing, the 0x61 timer as an enemy being destroyed, and 0x2c0da as the
  touch that starts a battle. Still to be seen running in the oracle (scene 1).

### Shots and enemy contact SEEN in the oracle, scene 1 (2026-10-17)

Method: a copy of `original_bin` with `MAN_INFO.NOW` byte 500 = 1 (scene 1) and bytes 502/503 = the hero's x and y/2, then `dosemu.py` with
`--loop-top 0x11d36`, `--key-pass`, `--hit-pass`, `--dump-hit 0x11d36:N:-0xcc:16` and `--snap-pass` (scripts in `work/walk8/`, git-ignored:
`run.sh NAME X YHALF ...`, `rawmont.py`). The hero position and the records use the **same units** (a hero at (104, 72) stands beside record 4 at (106, 72)).
Scene 1 is outdoors (river, hills, trees); its records with byte 0x31 set are red creatures.

- **Touching an enemy starts a battle, inside WALK.SL** (SEEN). With the hero placed on record 4 (106, 72), the main loop ran one pass and left; the
  program then opened `rpg.lcp`, `tab.tab`, **`mon.all`** (monsters) and `rpg.lcp` again and drew a **battle screen in the play window**: the party
  strip at the top (the hero's face, 600), a row of four red command icons, and three red monsters on a forest backdrop. It was still there at tick 119
  (waiting for input). There was **no exec**: the battle code is in WALK.SL. `0x18fdc` was **not** reached, so my guess that it is the battle was wrong
  (2026-10-17); where the loop goes on a touch is still to be read. `0x2c0da` itself runs every pass (the contact test); the touch is its result.
- **Space fires a shot** (SEEN). Space pressed after pass 20 with the hero facing down at (106, 66): at pass 26 entry 0 of `[bp-0xcc]` was
  `03 6c 49 01`, then `0a 6c 4d 01` (30), `0a 6c 51 01` (34), `0a 6c 57 01` (40), `0a 6c 61 01` (50). So the entry is (frame, x, y, direction) as read; the
  shot starts at x + 2 (0x6c = 108, hero 106), moves **one unit a pass** in the hero's direction (1 = down), and its frame runs up to 0xa, which then
  stays (0xa is the "flying" frame, not "off" as I wrote in step 6: the unused entries show 0a, a counter, fa fa). The other entries' second byte counts
  up by one a pass. Earlier with three entries in use (pass 18) a single press made more than one shot; not read why.
- **A shot destroys an enemy** (SEEN, the screen at passes 20, 27, 35, 44, 47): the shot flies down to the red creature, the creature turns into a
  white puff of smoke, which shrinks to a speck and is gone. `0x13356` (the "return 0xc" case of `0x2bccd`, direction `0x61` running out) was reached
  **after pass 45**, when the puff ended. So direction 0x61 is "being destroyed" and 0xc is "removed", as inferred.
- In 757 passes with the hero beside record 4 (not on it) no touch happened: enemies here did not walk into the hero in that time.

**Still to read:** the battle (where the loop goes after a touch, and the battle's own loop and files); how a shot finds its target (the hit test and
the switch of the record to direction 0x61); why one press can make several shots; the shot's sprites at `[bp-0xd4]`; whether shots or touches change
the record bytes in `__1.NOW` (an enemy that stays dead).

## WALK.SL step 11: shots, sparks, smoke and respawns (read, then checked through the port), 2026-10-17

READ from the code, then CONFIRMED by the C port matching the oracle at every pass (`port.md` "Milestone 7f"). Corrections to step 10 and the
"SEEN" notes above: the list at `[bp-0xcc]` is the **sparks**, not the shots; the shots are `[bp-0x62]`, and the two are one 60-byte block
(`0x102fe` fills it with 0xfa; just before the loop `0x11cee` sets each spark's frame to 9 and each shot's direction to 4). What I saw "flying to the
enemy" was the shot, not an entry of `[bp-0xcc]`; the entry I dumped there was a spark.

- **Space** (`0x1424c`, read on **every** pass, not every third like the arrows): for party records 0-4 (stop at member number byte 3 > 0x64), if the
  word at +8 is not 0 (the hero's: 600) and the shot slot **numbered by the member** (byte 3) is free (direction > 3): copy x, y, direction; phase =
  rand() / 0x4000; x and y even; y += 2 when facing up; when the phase is 0, x += 2 (facing up or down) or y += 2. Followers (records 1-4) then face
  rand() / 0x2000. So one shot per member at a time; a held Space fires again as soon as the shot ends.
- **Shots** (`0x1208f`, after the scene-record check, before the objects): `0x2bac3` moves all five (used or not) 2 along the direction (unused ones
  move as 'up') and 2 across, alternately to either side (the phase). Each used shot: off screen ends it (direction 0xa); else it is drawn (`RPG.LCP`
  0xb16: a 4-byte header, 8 x 8), then `0x2bb5e` tests the 20 villager records (skipping smoke and byte 0x31 = 0) at the shot's position against the four
  cells (x & ~1, y & ~1), (+0, +1), (+2, +1), (+2, +0): a hit adds 1 to byte 0x28 and, at twice the sprite set (byte 0xb, 6 excepted), sets direction
  0x61, frame 0, counter 0 (result 2), else turns the enemy the shot's way (result 1). `0x2ba52` tests the tile under the shot at ((y >> 1) << 8 | x),
  one row above where a foot test looks. A wall or result 1 starts a spark in the first free slot (frame > 4) at the shot, sets the shake to 4, and
  ends the shot; result 2 adds a coin (party table word 0x122, at most 60,000), redraws the count (`0x1bd6e`: 40 x 5 box of 0 at (273, 185), digits
  7 x 5 from `RPG.LCP` 0x9b8 right-aligned 8 apart) and ends the shot.
- **Sparks** (`0x12314`, after the objects): from frame 2 on a spark drifts 1 a pass in its direction; frames 0-4 are drawn (`RPG.LCP` 0xb5a, 8 x 8)
  and counted on, then it stays at 0xa.
- **Smoke**: a villager in direction 0x61 is drawn from `[bp-0xba]` + 0xe80a + 0x100 * ((frame >> 2) + 1), i.e. five 16 x 16 frames decoded from
  `RPG.LCP` 0x1a4cc (0x1eb bytes, to `[bp-0xba]` + 0xe90a by `0x109cb`). The port had guessed that region was the party bank (it is past its end).
  `0x2bccd` returns **0** while the smoke lasts (the port returned the timer, so at 10 it chose a direction) and 0xc at 0x14.
- **Respawn** (`0x13356`, on 0xc): bounds x from hero x - 0x16 to + 0x30, y / 2 from hero y / 2 - 10 to + 0x15 (bytes), kept in the hero's walk counter
  `[bp-0x3c]` and frame `[bp-0x5e]` (the original reuses them; the frame advance then resets them, `> 3` gives 0); then repeat x = (rand() / 0x13c + 12) * 2,
  y / 2 = rand() / 0x19a + 10 until the place is outside those bounds and its two cells (`0x2c119`, row y / 2 + 1) are walkable; the enemy goes there.
- **Touching** (`0x12703`, after the window copy): for each party member, `0x2c0da` finds a record not in smoke, at the same position with the low bits
  cleared, with byte 0x31 > 0. Then byte 0x2f of the record: 0 moves the enemy to a random walkable place (no view bounds) and fights a monster group
  from `rpg[0x12] * 5 + set - 1`; not 0 runs `0x15fe:0` on the record's +0x11 and, if that returns non-zero, a dialog (`0x8c3:0x2380`), then the battle
  with group byte 0x30. Only read; not built.
- Not built: the sound effects (`0x1c68e`, at spark frame 2 when `[0x380]:0xc` = 0x81, and on a kill); the battle.
- 2026-10-18: the Python model (`tools/walk_idle.py`) now has all of the above except the battle, and matches the oracle at every pass of the same six
  scripts, so this step is CONFIRMED by both the model and the port. NOT SEEN: the followers' facing after Space (no start has a follower).

## WALK.SL step 12 (survey): the battle, 2026-10-18

A survey to plan the build: the structure is READ from the code, the screens and controls are SEEN in the oracle. Nothing is built yet. Method: start on
an enemy (`tools/walk_scene_data.py DIR 1 106 36`), run `tools/dosemu.py` with no `--loop-top` (the battle has its own loop, and a frozen pass clock
would stop it), `--snap` at chosen ticks and `--key` scripts; code coverage from `work/battle/bcover.py` (scratch, git-ignored).

- **Entry:** after the contact test the world loop sets up at `0x12aba`-`0x12e4e`: two `RPG.LCP` reads, `TAB.TAB`, `MON.ALL` at tick 24, a sound
  (`0x1c68e`). Then it calls **`0x1cbb6`**, the battle, with 21 argument words (the world's buffers: party table, villagers, palette buffer, screen
  buffers and more).
- **Size:** `0x1cbb6`-`0x1fd06` is one function of 12.6 KB, with its own loop from `0x1d418` to `0x1f8e9`. It calls the helpers `0x1fd06` (1.5 KB),
  `0x202e6`, `0x203b4`, and two big subroutines, `0x20412` (4.2 KB) and `0x21486` (3.4 KB). Further routines nearby have the same call profile and are
  probably battle code too: `0x221d2` (2.7 KB) and `0x22c42` (5.8 KB). That is roughly 30 KB, more than all of `WALK0.SL`.
- **Screen (SEEN, tick 30):** the play window shows the battle. At the top, the party strip has the hero's face, his HP (600) and four empty slots. Below
  it is a red command strip, then a forest backdrop with three red monsters. The monsters animate while it waits: their frames change, but no input is
  needed.
- **Keys (SEEN):** the battle reads port 0x60 through `0x274b8`, like the world does. A key counts only after a break code has been seen (the latch
  `[bp-0x38]`, set by a value above 0x7f), so a press needs its release before the next one is taken.
  - Left (0x4b) and Right (0x4d) move the highlight along the command strip. The handlers clamp it at 0 and 3 (`[bp-0x82]`).
  - Space (0x39) or Enter (0x1c) confirms. The first confirm on the first command puts a yellow box round the first monster (choosing a target). A
    second Enter attacks it.
  - Keys 1 and 2 have the world's music and sound toggles (`0x1c3ba` highlights the same buttons).
  - The mouse path (`0x260c9`, hit boxes such as x 0x10c-0x13c, y 0x62-0x8a) runs only when a mouse driver is present (`es:[0x392]:4`).
- **One round (SEEN, Enter then Enter):**
  - **Hero's attack:** the hero's turn shows a shout, 送你們下地獄! ("I'll send you all to hell!"), over the field, and the target monster is drawn dark
    (hit).
  - **Monster's attack:** then a monster's attack is shown close up, with 別想過我這關! ("You won't get past me!") at the bottom. The hero's HP goes
    600 → 590 → 560 in the strip.
  - **Back to waiting:** the screen returns to the waiting state (three monsters, command strip) by tick 400.
- **Not seen yet:**
  - **Commands 2-4:** probably magic, items and flee or defend. The world's 施法, 物品 and 使用 suggest it, but that is not checked.
  - **Damage:** the damage formula and where the shouts come from (probably `MES_TXT.LCC` or `MON.ALL`).
  - **Results:** winning (coins, experience, the return to the world), losing, and the monster groups' data in `MON.ALL`.
  - **Byte 0x2f:** the record's byte 0x2f paths (dialog before the battle).

## WALK.SL step 13: the battle's first screen and idle passes (stage 1), 2026-10-18

Built in C (`src/battle.c`, the hooks in `src/walk.c`) and compared with the original pass by pass by `tools/check_battle.sh`
(`tools/battle_check.py`: the oracle counts the world loop `0x11d36` and the battle loop `0x1d418` as one sequence of passes). Everything
below is READ from the code and then CONFIRMED by that comparison unless it says otherwise. Method: oracle dumps of each intermediate
buffer (`--dump-local` / `--dump-hit` at the call sites), checked one step at a time in a scratch model (`work/b1/m.py`), then the C port
against the screens.

**The world's side of a touch (`0x1275e` .. `0x12e4e`).**
- The touching member's party offset (`di`, 0, 0x28, ...) is kept. Record byte 0x2f = 0: the enemy goes to a random open place (the
  respawn's two `rand()` draws and `0x2c119` test, without the view bounds), and the monster group is `party table[518] * 5 + record[0xb] - 1`
  (bytes). **Correction to step 11:** `[bp-0x34]` is the party table + 500, not the `RPG.LCP` buffer; byte 0x12 of it is party-table byte 518,
  the map header's byte 1 (written by the loader at `0x10e05`). Byte 0x2f set: the group is byte 0x30, after a dialog when the word at 0x11 is
  not 0 (not built; scenes 4 and 40 have such enemies).
- The world buffer's bytes 0..3 become 0, hero x, hero y, 1, and the frame routine `0x19680` runs again. It is **not** the main loop's drawing
  (that is inlined in the loop); with byte 0 = 0 it draws no villagers and no party (`0x19713`), with byte 3 = 1 the ground and objects of 14 x 13
  cells from buffer (64, 56) only, then `0x2c658` doubles the 96 x 84 pixels from (64, 57): `p`, `t[p]` / `t[p]`, `t[t[p]]`, with
  `t` = `RPG.LCP` 0x340 (256 bytes, at world buffer + 0xfe78), and copies the doubled picture back over the world buffer (`0x5080` words).
  A first attempt drew the hero in it; the dump at `0x19985` showed he is left out.
- `0x2728d` takes the 192 x 168 window from (16, 16). Row k of it (k = 0 .. 109) is stretched to 195 + 6k pixels (`0x26de8`: a 16-bit step
  0xffff * 192 / width, starting at 1, so even equal widths repeat a pixel) and its middle 195 kept, giving a 195 x 110 backdrop in which the
  ground seems to lie back. Every 36 rows two 6 x 6 squares go on screen (colour 0x10 at (80 + 10i, 50), 0x1f at (82 + 10i, 52)), a progress row
  the party strip covers a moment later. In the original each row's 4-byte image header is written over the end of the previous row in place.
- `0x2b8f6` (read as a scan in step 12; it is a replace): in every second byte of the backdrop's first 0x5284, the colours below 16 (the ones the
  palette cycle changes) become fixed ones (1 → 0x57, 2 → 0x59, ... 15 → 0x36). Found when scenes 1 records 5 and 9 differed in 46 pixels.
- `0x25dca` four times (byte translation through `t`): bytes 0..0x3e0e, 1..0x2e89, 0..0x1f05, 0..0xf82, so the far rows are shaded most.
- The battle's music: if music is on (`[0x37e]:5` = 0x81) the scene's song stops and the song at buffer + 0x4a38 starts, which is `MUC.ALL`
  0x2bad9 (0x1c92 bytes). **Correction to step 9:** that "second song" is the battle's song, not unused. The original's register writes after
  the touch match the player for song ticks 1 .. 3,119 (`compare_opl.py`; tick 0 is the driver's known reset).
- The party strip (`RPG.LCP` 0x18691, 0x1247 bytes, a 192 x 58 picture) at (64, 16); for each member with byte 3 <= 4: a 20 x 20 box of colour 0
  at (67 + 38k, 29), his 16 x 16 face (bank 2 + number * 0xc00 + 0x400) there, darkened seven times through `t` when his HP word (+8) is 0, the
  HP at 16 rows below in colour 0xf0 and the MP word (+0xd) 16 pixels right in 0xb0. Numbers (`0x1c47c`, mode 0): a 20 x 5 picture, five 3 x 5
  digits 4 apart from `RPG.LCP` 0xd30 (the battle buffer's +0xd44c), leading zeros left out, ink 0x10 recoloured.
- `0x1c68e` (a Sound Blaster sample) and the mouse pointer's save and restore are not built (no Sound Blaster, no mouse in the oracle).

**The battle's set-up (`0x1cbb6` .. `0x1d415`).** Its buffers are carved out of the world's (backdrop copy at world buffer + 0x5460, field
picture at + 0xa974, monster table at the big buffer + 0x4028, and more).
- Five slots (4 bytes: field position word, frame, state): each slot draws `rand() / 0x199a` (0 .. 4); above 2 it is empty (9); a record with
  byte 0x2f set makes every slot frame 1. Again until a slot is filled. Then each empty front slot (2 .. 4) takes the first filled back one (0, 1).
  Positions in the 192-wide field: 0x21f6, 0x222c (back), 0x2101, 0x2142, 0x2183 (front).
- The group: `TAB.TAB` 0x19f + 5 * group gives its place in `MON.ALL`; decoded, three 60 x 60 pictures 0xe14 apart (their headers overwritten
  with 60 x 60). The last 10 bytes of the group as read go to table + 0x28 (stats); each filled slot's HP byte (table + 0x32 + slot) is the first.
  The copy's source is `[bp-0x80]` - 10, where `[bp-0x80]` is the read's byte count, not the `group * 5` stored there just before.
- Also read and decoded but not used yet: `RPG.LCP` 0x135e4 (two blocks, probably the attack pictures), `RPG.LCP` 0x14891 (21,600 bytes).
- Who starts: `rand()` below 0x3e80 → the monsters (turn counter 0, acting member 0xf0); otherwise the party from the touching member: the first
  member with HP and byte 3 below 5 gets the mark (colour 0x1f of the 12 x 3 pixels at (71 + 38k, 21) turns 0x0f).

**The idle pass (`0x1d418` loop).**
- The world's palette cycle `0x1c284` on the same counters; the four command tabs when the chosen one changed (all at the first pass: outlines
  0x95 / 0x94 and a fill 0x93, the chosen one 0x92 / 0x90 / 0x10); the sway (count 0 .. 3 and direction, one step a pass) and `0x2c6b9`: field row r
  is backdrop row r from an offset that starts at the count and bounces between 0 and 3 down the rows.
- `0x1fd06`, per live slot: one `rand()` jitters it a pixel (below 0xbb8 left, above 0x7447 right; the table is not changed), the back slots are
  shrunk to 30 x 30 (`0x26f72`) and shaded twice, the front ones drawn at 60 x 60 (state 0 or 0x32), colour 0 transparent; a second `rand()` moves
  its frame (below 0x2ff down, above 0x7d00 up, within 0 .. 2). So the monsters' idle animation is random frame changes and jitter.
- The effects list (20 entries at big buffer + 0xd4e2) is empty; a 1 x 1 picture cleared at the second pass is drawn every pass and shows nothing.
- The field (192 x 110) to screen (64, 74). The key latch: a value above 0x7f arms it; a key counts only when armed.
- The monsters' turn: the counter walks the slots 4 counts each (one count a pass while the sway's direction is set); an empty slot is passed over
  (`0x1dbf3`). A live monster's action (`0x1d641` on) is not built.

**Checked:** 10 runs, all identical at every pass in screen, palette, rand state, monster table, sway, acting member and turn counter: starts on
six enemies of scene 1 (groups 0 to 4, 150 to 300 passes), scenes 34 and 42 (120), a walk into an enemy (9 world passes then 241 battle passes
at clock 30000; at clock 20 the monsters start, and the 13 passes before their first action match). `tools/mutate_battle.py`: 30 deliberate
bugs, all caught (one by the unit tests); three more no start can show are listed in the file. Unit tests `tests/test_battle.c`. ASan/UBSan clean on all 10 runs.

**Found in the world on the way** (scenes other than 0 and 1, which the earlier checks did not cover):
- Map header byte 3 is the shake's start and floor (`0x11233`, `0x1259b`): a scene with 1 there keeps shaking. Built (C port and Python model).
- Map header byte 5 = 1 (scenes 9, 10, 11, 12, 24, 25, 29, 47) draws the world doubled every pass (it is passed as the view flag at `0x11b4f`,
  `0x11e01`, `0x122ef`, and changes the window copy at `0x125c6`). **Not built:** in those scenes the port's world window differs from the first
  pass. Weather mode 2 (scene 3) differs in 122 pixels at the first pass (not built, as before).

**Correction to step 12:** that start shows five monsters, three in front and two half-size ones behind them, not three.

**Not built:** the battle's keys and mouse (commands, targets), the monsters' actions, attacks and damage, the effects, winning, losing and the
way back to the world (the port stays in the battle), the dialog of byte-0x2f enemies, the Sound Blaster parts. The touched enemy's new place is
computed but not seen until the battle returns. **Pace:** the original's battle loop is unpaced; the port runs it at the world's 35 passes a
second. The monsters jitter and change frames at that rate. Not judged by eye yet.

## WALK.SL step 14: choosing a command and a target (battle stage 2), 2026-10-18

Built in C (`src/battle.c`: `keys`, the target box in `draw_monsters`) and compared with the original pass by pass: `tools/check_battle.sh`
now also runs five key scripts from `tools/battle_keys.py` (tabs, targets, Esc, keys 2 and 3), and `tools/battle_check.py` compares the
chosen command `[bp-0x82]`, the choosing flag `[bp-0x72]` and the key latch `[bp-0x38]` as well. READ from the code, then CONFIRMED by that
comparison unless it says otherwise.

- **When a key counts** (`0x1e7e3`): only in the party's turn (`[bp-0x44]` below 0xc8), when the monster routine reported nothing (`[bp-0x84]`
  = 0) and the latch is set; a value above 0x7f (a release) sets the latch. Any value below 0x80, **0 included**, then clears the latch, so after
  the first pass with no key a key needs a release before it counts.
- **The latch's first value** is never set: it is what the stack holds there, the SI the world pushed for its copy at `0x199a8`, which is the
  touched record's offset in the villager table (low byte; 0xc8 for record 4, 0xfa for record 5, 0 for record 0). Found with a write trace on
  the stack slot (`work/b1/latch.py`), after three starts disagreed. So with a record offset that is not 0, a key held when the battle starts counts.
- **Keys 2 and 3** (`0x1e82e`, `0x1e8f1`): music or sound effects on and off, with the right-hand button lit (`0x1c3ba`); each only when its
  device is there (`[0x394]:5`, `[0x38e]:0xc` not 0). The oracle has neither, so there the keys do nothing; not built in the port (reported).
- **Commands** (choosing flag 0): Left `0x4b` goes to the tab before, from the first round to the last; Right `0x4d` to the next, from the last to
  the first. Space `0x39` or Enter `0x1c` sets the choosing flag. With the first command (attack) the first front slot (2, 3, 4) with a live frame
  (below 3) and state 0 gets state 0x96, the target; with none the flag is cleared again.
- **Choosing a target** (flag set, command 0): Left moves the 0x96 to the nearest live, state-0 front slot on the left, Right on the right; with
  none in that direction it stays. Esc `0x01` clears the flag and the three front states. Space or Enter starts the attack (`0x1ec8f`: the mark
  goes back to 0x1f, damage and pictures; **not built**, stage 3). With commands 1 to 3 the confirm goes straight on: command 1 runs the menu
  routine `0x21486` (probably the magic list), 2 and 3 are at `0x1f6f6` on; **not built**.
- **The target's box** (`0x20066`): the monster's picture copied (0x70a words, header included), three outlines drawn into the copy at its
  edge and one and two pixels in (colours 0xf4, 0xff, 0xf8, `0x271d3`), then the copy drawn with colour 0 transparent.
- **The mouse** (`0x1e472` .. `0x1e7e3`, used only with a mouse driver) is not read and not built.

**Checked:** 15 runs, all identical at every pass (screen, palette, rand, monster table, sway, turn, command, choosing flag, latch): the 10
earlier ones and key scripts of 400 to 450 passes in scenes 1 and 34, two of them in a battle whose third front slot is empty (a walk from
(114, 72)). Over the scripts all four tabs are chosen and the target visits all three front slots. `tools/mutate_battle.py`: 43 deliberate bugs,
all caught. ASan/UBSan clean. In the real-time program Esc no longer closes the window during a battle (`platform_escape_quits`).
Not covered: a front gap in the middle (no start found has one), a confirm with no live front monster.

## WALK.SL step 15: the attack and the monsters' turn (battle stage 3), 2026-10-18

Built in C (`src/battle.c`: `party_attack`, `monster_blow`, the states in `draw_monsters`, the per-pass blocks of `battle_pass`) and compared
with the original pass by pass, now including 16 battle locals, the party table and the floating numbers' list (`tools/battle_check.py`).
`tools/check_battle.sh` plays six whole battles to their end with `tools/battle_keys.py --attack` (Space, Enter, Esc). READ from the code
(`0x1d5de` .. `0x1e46f`, `0x1ec8f` .. `0x1f18d`, `0x1fd06`, `0x202e6`, `0x203b4`), then CONFIRMED by that comparison unless it says otherwise.
Coverage of the round was found with `work/battle/bcover.py` and a key script.

**Data** (all read once at the start): `RPG.LCP` 0x135e4 (0x12ad bytes): block 1, five 20 x 20 hit pictures; block 2 (+0x25e), the members'
attack pictures with headers, at the offsets the set-up stores at +0x93be (0x7be6, 0x80fe, 0x859c, 0x8b13, 0x8fbb). `RPG.LCP` 0x14891: six 60 x 60
masks (the world buffer's first 21,600 bytes in the battle). `RPG.LCP` 0xb5a: five 8 x 8 sparks (the world's). `RPG.LCP` 0xc9a: the floating
numbers' digits. `RPG.LCP` 0x198d8: one-bit 136 x 14 pictures of shouts, 0xff apart (2 .. 6 the party's, 7 .. 11 the monsters'). Each was found by
comparing the battle buffer with the files (`--dump-hit 0x1d418`).

**The party's attack** (Space or Enter on a target), all at once:
- The member's mark goes back (0x0f → 0x1f); the target's state becomes 0x32; the hit animation starts (frame 0 of 5).
- **The blow:** `experience / 1200` (party word +4) plus `(byte 0x13 / 5) * 4` (halved for members 3 and 4), at least 1; then, with
  `t = (2000 - word 0x10) / 128 + 2`, less `blow / t` when t is below 17. A blow at least the monster's HP kills it and adds monster stat 4
  (table +0x2c) to the member's experience, capped at 60,000.
- The key latch `[bp-0x38]` is used here for the monster's HP and keeps that value afterwards (so not 0 means armed).
- Two floating numbers (`0x202e6`: the first free of 20 places, at most 999, rising a row a pass): the HP left (colour 0x23, 65 passes) and
  the blow (0xf0, 50 passes) over the target. Their 12 x 5 pictures are rendered with the second font; the recolouring runs 0x68 bytes from each
  picture, 40 bytes into the next (from the 20th, into the shouts).
- The mark moves to the next member with HP and byte 3 below 5; with none left, the monsters' turn comes. Then a shout (`rand() / 0x199a + 2`)
  appears at field (28, 10) for 45 passes.
- **Over the next passes:** the hit pictures, grown to `blow / 7 + 20` pixels (`0x26de8`), three passes each over the target, with the member's
  attack picture at the bottom of the field. When they end, the slot under them flashes: state 0x14 counting down, the monster drawn as a
  silhouette of colour `state + 0xf`. At 0, with no HP it dies: states 0x3d .. 0x53 draw it through mask `(state - 0x3c) / 4`, with sparks;
  at 0x54 the slot's frame becomes 5 (dead) and the monster routine returns 1. Any state other than 0 and 0x96 adds falling sparks (five frames,
  two rows apart every third pass, then a new random place) and makes the routine return 2, which holds the keys and the turn.
- **A death:** all monsters' HP 0 is the end (won). Otherwise each dead front slot takes the first live back monster with **full** HP (stat 0).

**The monsters' turn** (the counter walks the slots 4 counts each, as in step 13):
- At a live slot's first count it leaves the row (frame 4) and is shown doubled (`0x25cdc`, 120 x 110) at field (36, 0), its three frames four
  passes each. Then the blow: the field fills with colour 0x28 for a pass; a member is chosen (`rand() / 0x2aab` among the first three for slots 0,
  1 and 3, `rand() / 0x4000` (0 or 1, slot 4: 0 or 2) for the others, retried while the member is not in the party, ten tries at most; and with
  `rand()` below 0x666, from the second slot on, one of the last two if member 3 is there); `blow = monster word 0x29` (halved from the back row)
  less his byte 0x12, the HP goes down (to 0: he falls, his face darkened seven times), the strip shows the new HP; the blow floats over the
  monster (colour 0x78, 80 passes); his word 0x10 grows by stat 5 (halved from the back row; at most 2000), shown too (colour 0x69) with a field
  flash of colour 0x30. For the next 7 passes the field jumps up 0 .. 19 rows (`rand() / 0x667`, the bottom rows black); for 10 passes his face
  shows the five hit pictures over it. A monster's shout (`rand() / 0x199a + 7`) for 46 passes.
- Nobody left with HP is the end (lost). When the counter reaches 0x14 and the face animation is done, the turn goes to the first member with HP.

**Checked:** 23 runs, all identical at every pass: the 17 earlier ones (the monsters-first walk now 300 passes: three blows and the turn back)
and six battles fought to their end: in scene 1 against three groups, in scene 34, with a party of three (`walk_scene_data.py --join 1 --join 2`),
and with a member of 40 HP who loses (`--hp 0:40`). At the end both stop at the same pass: the original leaves the battle loop, the port stands
still. ASan/UBSan clean on three of the long runs. `tools/mutate_battle.py`: 69 deliberate bugs, all caught. The first run of the stage-3
list (2026-10-18) missed five and had two stale patterns: the trace starts never give a member experience at a multiple of 1200, a word 0x10
above 80, a defence above 0, or a hit column between 0x61 and 0x70, so the blow formulas and the slot pick are now separate functions
(`battle_blow`, `battle_monster_blow`, `battle_hit_slot`) pinned by unit tests worked out from the code. The fifth, a monster stepping
forward with its own HP instead of the full HP, cannot differ yet: only front monsters can be hit until commands 1 to 3 are built, so it
is left out of the list with a note.

**Not built:** what follows a win or a loss (`0x1f8ec` on: rewards, the end screen, the way back); commands 1 to 3; the mouse; keys 2 and 3;
the Sound Blaster samples at each blow and death.

## WALK.SL step 16: what follows a win or a loss, 2026-10-19

Built in C (`src/battle.c`, `src/gameover.c`, `walk_resume` in `src/walk.c`, `main.c`) and compared with the original pass by pass:
`tools/battle_check.py` now counts four loop tops as one sequence (world `0x11d36`, battle `0x1d418`, rewards bar `0x1fb7d`, message box
wait `0x1b506`) and compares the villager and party tables in world passes; `tools/gameover_check.py` runs MARK.EXE. READ from the code
(`0x1f8ec` .. `0x1fd01`, the world's `0x12e56` .. `0x13318`, `0x1afb0` .. `0x1b633`, `0x2464a`, MARK's `0x101ed` .. `0x1055f`), SEEN in
oracle screenshots (method: `work/b4/probe.py`, scratch), then CONFIRMED by the comparisons unless it says otherwise.

**Correction to step 15 (2026-10-19):** of the six whole battles in `check_battle.sh`, two are won (scene 1 record 4, the party of three) and
four lost (scene 1 records 9 and 19, scene 34, the 40 HP member); step 15 implied five wins. The comparison could not tell won from lost then.

**The battle's three ways out** (`0x1f8ec`): `[bp-0x84]` = 0x63 (set at `0x1dba1` when nobody in the party has HP) returns 0 at once; the
win (`0x1dc42`, all monster HP 0) and a lucky flee (`0x1f8d9`: `rand() >= 0x4000` stays, below leaves; command 3, not built) go to the bar.

**The rewards bar** (`0x1f900` .. `0x1fcb5`, SEEN: `5 x 001 = 0005` over the field):
- NUMBER.LCZ chunk 0x359b decoded (`0x25eeb`): twelve 13 x 13 pictures without headers, x, =, then the digits 0 .. 9.
- Into a 192 x 13 bar cleared to 0 (`0x25d82`, opaque): the kills digit at x 3 (kills = slots with frame 3 .. 8, i.e. dead), x at 0x16, the last
  three digits of stat 3 (`mon[0x2b]`, coins per monster; `0x26450` makes five digits) from 0x29, = at 0x62, the last four of kills x stat 3 from
  0x75, 0x13 apart.
- Coins: party word 0x122 + that (16-bit), at most 50,000 (the shots' cap is 60,000), and `0x1bd6e` redraws them at once.
- Loop: the sway, `0x1c284`, the bar moved by `[bp-0x64] - 0xc0` (start 0x1744, step 0x180) and, outside 0x1804 .. 0x2944, the step + 0x180,
  reset to 0 above 0x180, so it bobs one row a pass between field rows 31 and 56 (offsets include the field picture's 4-byte header);
  `0x26d52` copies it with 0 transparent; the field to (64, 74); then port 0x60: Space, Esc or Enter leave (no release needed). No monsters,
  no numbers. Returns the kills (`[bp-0x66]`).

**Back in the world** (`0x12e56`): the result goes to `[bp-0x3c]` (the hero's walk counter). Nobody with HP and byte 3 below 10: the game-over
path. Otherwise `[bp-0x5e]` (the hero's frame), which the touch had set to the enemy's byte 0x2f: not 0 goes to a path for those enemies (all
monsters killed: record word 0x11 = 0 and `0x2c158` on its map cell; else the party is put back; **not built**). Then for each of the five
records (`0x130e2`): experience (word 4) at least the next level's (word 6) and below 60,000: experience = word 6, word 6 + 500 (at most
60,000), word 0xa + word 0xa / 12 (at most 60,000) into words 0xa and 8, and the message box with text 0x2e2 + byte 3 (SEEN: `恭喜!恭喜!桑得斯的
生命力增強了。` for the hero). Then the mouse pointer, the scene's song from its start if music is on, and `0x13318`: the rest of the touching
pass (villagers' steps with the rain stride of that pass, keys, walk frame).

**The message box** (`0x1afb0`, called with colour 0x70 and the wait flag):
- Text: 0x78 bytes of MES_TXT.LCC at (n - 1) * 0x80 + 6, up to `#`. Below 40 bytes: row 150, centred by 8 pixels per missing byte pair; below 80:
  row 142; else 135. Each 8 x 14 ASCII character comes from RPG.LCP 0x1a6b7 (15 bytes each, one bit a pixel; `@` is drawn as a small circle),
  each 16 x 14 Big5 one from NUMBER.LCZ (`0x2464a`: 28 bytes at 0x3b20 + 30 * cell, lead bytes 0xa4 .. 0xc6 then 0xc9 on after 0x1519 cells,
  through a 292-entry cache that changes nothing visible). Rows recoloured colour + 1 and + 2 from byte 0x26 / 0x50 of the picture, a shadow of
  colour + 6 one row down at x and x + 1. A Big5 character at x 0x138 wraps first; x 0x140 wraps after.
- The box (the wait flag makes it 320 x 46 at row 134): `0x2723c` takes the screen into the world buffer, `0x27503` copies it 5 bytes on but
  4 bytes short, so the copy's last 4 pixels are the world buffer's 0x7305 .. 0x7308 (in a battle the backdrop's 0x1ea5 .. 0x1ea8); shaded
  twice through the table, rows 1 .. 44 twice more, drawn, text over it.
- Wait (`0x1b506`): a counter (colour + 3 after the text) steps the palette when it passes 0x28; port 0x60 above 0x7f clears the wait for a
  release, then a value below 0x80 ends it; the screen under the box goes back.

**The game over** (WALK.SL `0x12ebc`): sound and music stopped, 0x3e written at ANI.LCP 0x2ff (`0x26ccf` seek, one byte), the buffers freed, and
MARK.EXE started (DS:0xe0). In the oracle the exec is not taken, so the world runs on there; the comparison stops at the loss. MARK
(`0x101ed`, SEEN): the screen left by WALK.SL (MARK sets no palette on this path) shaded four times through RPG.LCP 0x340, shrunk to 80 x 50
(`0x11a76`, the same stepping as `0x26f72`) and tiled 4 x 4; song 24 (MUC.ALL 0x2d76b, 0x7b1 bytes) if music is on; then each pass (`0x1040a`)
the tiles roll up a row and left a pixel (both wrapping) and the lettering 全體陣亡 (TITLE1.LCZ 0x3e73, 93 x 26; the read asks for one byte past
the file's end) is drawn at row 87, column 114 for the screen only (the call is given the picture with its header, so 0x6d36 lands 4 pixels
left). Space or Enter (or a click) end it; MARK then sets its message to 0x3f and goes on with its normal start, the title (SEEN: with Space
still held the title passed straight to WALK0.SL's menu).

**Checked:** `tools/check_battle.sh`: all earlier runs, the two won battles continued 1,276 and 1,699 world passes after the bar, level-ups
with one message box (hero) and three (party of three, long waits, 279 box passes), the bar held 100 passes then walking, a walk-in win across
tick 1024 (rain stride change), and 400 game-over passes. All identical. Unit tests: `battle_level_up`, `battle_add_coins`,
`battle_glyph_offset`.

**A second battle** (FOUND by the comparison, 2026-10-19): the same walk-in start at the clock 20300 wins, and then the hero walks into a
second enemy (pass 1148). Everything matched there except `[bp-0x84]` after the second battle's first pass: 1 in the original, 0 in the port.
The battle routine never sets it before its loop's first call of the monster routine (which overwrites it before anything reads it), so it is
what the stack held, and the first battle had left 1 there (a won battle ends on a death, which returns 1). The port now carries it from one
battle to the next (`walk_battle_left`), 0 before the first. INFERRED from this one run: that the 451 world passes between the two battles
never write that stack word. Invisible on screen; `walkwin_20300` in `check_battle.sh` covers it.

**Mutations:** `tools/mutate_battle.py` now has 111, all caught (the full run caught 107 of 109 with two stale patterns, which now match once
each, plus a rewards-loop twin of each; those four re-run and caught). It also runs `tests/test_walk.c` for mutations in `walk.c`. Two
survivors of the run before:
- "resume: the new pass's rain stride" shows only when an enemy asks for a direction on the very pass after a battle that began in rain and
  ended dry; no recorded start does (ten clock readings tried, 20150 .. 20460). It is now pinned by a unit test (`test_resume_stride`: twelve
  enemies one step from asking, the battle touched at clock 20, resumed at 0x6000).
- "game over: tiles not shrunk the same way" is dropped: 320 / 80 and 200 / 50 are whole, so `battle_shrink` takes every 4th pixel too and the
  two cannot differ.

**Not built:** fleeing; what follows a battle with a byte-0x2f enemy; the mouse in the bar, the box and the game over; the Sound Blaster
samples; the ANI.LCP byte itself (the port hands the result over directly). **Pace:** the bar and the game over at 35 passes a second, the
box's wait 41 rounds a pass: choices, the original paces none of them.

## WALK.SL step 17: the other three commands (flee, magic, items), 2026-10-19

Built in C (`src/battle.c`: the flee in `keys`, `magic_begin` / `magic_round` / `magic_picked` / `magic_end`, `pick_begin` /
`pick_round`, `items_begin` / `items_round` / `item_use`, `text_pic`, the spell flashes in `battle_pass`) and compared with the original
pass by pass: `tools/battle_check.py` now also stops at the magic list's loop (`0x217a6`), the member picker's (`0x1aa30`) and the item
list's (`0x207b2`), where it compares screen, palette and rand. READ from the code (`0x1f1ca` .. `0x1f8e9`, `0x1e034` .. `0x1e109`,
`0x20412` .. `0x21485`, `0x21486` .. `0x221d0`, `0x1a8a4` .. `0x1afaf`, `0x243ec`), SEEN in oracle screenshots (`work/b5/probe.py`, scratch),
then CONFIRMED by the comparison unless it says otherwise. The shipped save has no spells and no items, so the starts set them
(`tools/walk_scene_data.py --byte`).

**The tabs** are attack, magic, items, flee: CONFIRMED (step 12 guessed magic, items and flee for the other three; step 14's "command 1
runs `0x21486`, 2 and 3 are at `0x1f6f6` on" holds: 2 is the item list, 3 fleeing).

**Flee** (`0x1f86b`): the member's mark goes back (0x0f → 0x1f), the turn goes to the monsters (`[bp-0x44]` = 0xc8, so later members lose
theirs), and `rand()` below 0x4000 leaves for the rewards bar (`0x1f8ec`), as after a win: the kills so far, their coins, level-ups, the
world. Otherwise the monsters act.

**The magic list** (`0x21486`, called with the field picture as its drawing buffer; byte 0 of that header set to 1 means "in a battle"):
- RPG.LCP 0xe95 decoded (0x264 bytes → 2,216): a 96 x 15 row with its header, then icons 48 wide. The icon at offset 0x5a8 (16 x 16) goes to
  (84, 76); ten rows at (64, 102), 15 rows apart, the second column from (160, 102) (SEEN: the brown rows over the field).
- Each spell is 20 bytes of MAGICT.LCC at 200 x the member's number + 20 x i: a 12-byte Big5 name; byte 0xc the level it needs (party byte
  0xc); words 0xd (heal), 0xf (lowers word 0x10), 0x11 (revive); byte 0x13 its strength. A spell his level has not reached is not shown;
  one shown with his MP (word 0xd) below twice its level is greyed (colour 0x16) and cannot be cast.
- A name is drawn by `0x243ec`: an (8 x n) x 14 picture, ASCII 8 x 15 from the message box's font, other bytes (below 0x20 too) start Big5
  pairs from NUMBER.LCZ; the colour turns +1 in each character's first 0x25 (Big5 0x50) pixels and +2 in the next as many; a shadow of colour
  0xfa at (x + 1, y + 1) and (x, y + 1).
- The lit row: 94 x 13 inside the row, colours 0x74 .. 0x77 turned 7 .. 10 (SEEN: red). Up and Down move a row (from the bottom of the
  first column to the top of the second and back), Left and Right a column, Esc leaves (0), and **key 1 (scan code 2) casts** (SEEN: Space and
  Enter do nothing there). Only after a release; a palette step every 41st round.
- A spell with any of the three words opens the picker; one without costs twice its level in MP at once and returns its strength.
- `[bp-0x1c]` (the palette counter) is never set: 3 at the loop's first round in every start tried (INFERRED constant).

**The member picker** (`0x1a8a4`, also used by the world's F-keys; here with faces and fallen members allowed): 18 x 18 boxes from
(70, 74), 18 apart, each with the member's face, a fallen one crossed out (eight 2 x 2 squares down each diagonal, colours 0x10 .. 0x17); the
chosen box outlined in 0xf, the others 0x4e. Left and Right go round the party, Enter chooses (the face crossed in 7 .. 10), Esc gives up
(0xc8, back to the list). Its palette counter `[bp-0x16]` is never set either: it holds the width argument (8 or 16) that the list's last
character draw (`0x26c04` from `0x243ec`) pushed at that stack address. FOUND 2026-10-19 after two starts gave 8 and 0x10.
- Then the caster's MP goes down, and: the heal word adds HP to a member who has some (up to word 0xa; result colour 0x2f, 0x9f when he had
  none and nothing changed); the second word lowers word 0x10 (to 0 at least; 0x3f); the revive word adds HP even from 0 (0x1f, and his face
  in the strip is drawn again). The face in the picker is drawn as a silhouette in that colour counting down 16 steps, three times, 100
  draws a step (the port draws only the last, colour - 15). The list returns 1.

**Back in the battle** (`0x1f247`): the result is also the key latch `[bp-0x38]` (so after a cast a held key counts at once: SEEN in the
heal start, where the held Enter opens the list again for the next member). Not 0: the mark goes back and the next member who can act gets
it. Above 1, on the monsters: each live front monster loses that many HP (a kill adds the monsters' experience stat to the caster's
experience, **with no cap**, unlike an attack) and goes to state 0x32; the blow floats over it (colour 0xf0, 50 passes) and the HP left (0x22,
65 passes), 0x3c apart from 0x30c5; shout 0 or 1 (`rand() / 0x4000`) in colour 0x30; then for 19 passes (`0x1e034`) the field is filled
with colour 0x10 every other pass under the member's attack picture at x 0x47 at the bottom, and the hit place `[bp-0x32]` moves 0x241 a
pass; at the end the states 0x32 become 0x14 and the monsters flash and die as after an attack. The strip's HP and MP are drawn again.

**The item list** (`0x20412`): the member's 20 item bytes (party record 0x14 ..) are first packed to the front; two icons (32 x 16 from the
same block) at (84, 76); the OBJECT.LCC records (30 bytes, item n at (n - 1) x 30) of his items and of the six things he wears (party table
0x12c + 6 x his number) are read into the field + 0x7d0. In each round, when asked, the ten rows of the page (items 1 .. 10 or 11 .. 20) with
the names (colour 0x10, shadow 0x16); the lit row in 0xb .. 0xe (SEEN: blue), recoloured over 0x4ca bytes of a 94 x 15 grab, so its last
two rows are not lit. Arrows move as in the magic list but by items (0 .. 4 down the first column, 5 .. 9 the second), **key 1 uses the
lit item, key 2 (scan code 3) turns the page**, Esc leaves; key 3 (giving to someone else) works only outside a battle. Using things
**does not end the member's turn**: the list stays open, and after Esc the commands are chosen again.
- A worn thing (byte 0x10 = its slot, 1 .. 6) changes places with what he wears there, and the two records change places in the list too
  (`0x25c8e` swaps them); his attack (party byte 0x13) and defence (0x12) become the 8-bit sums over the six worn records of byte 0xe and
  byte 0xf / 5, each halved when the record's byte 0x16 is not his number.
- Then, from the record now in that row: word 0xc heals him (up to word 0xa), word 0x14 lowers word 0x10, word 0x17 adds MP (up to 4,000);
  each of those uses the item up (its byte is 0; the list is packed again only when it is next opened).
- `[bp-0x1e]` (the palette counter) is never set: 0x98 in every start tried, after the magic list too (INFERRED constant), so the first
  round steps the palette.
- Before the call the battle puts the mouse pointer's background (6 x 10) back at the screen's top-left corner even with no mouse; it held
  the corner's own pixels (SEEN once), so the port does nothing there.

**Checked:** `tools/check_battle.sh`, 18 new starts, all identical: three flee scripts (`tools/battle_keys.py --flee`, alone and with three
members); a cast on the monsters that kills the three in front, and casts again and again until the battle is won (an empty front slot
included); heals through the picker (one reaching the most HP, one on a fallen member, where nothing changes); 88 rounds in the list then
120 in the picker (palette steps in both); the list walked top to bottom and back with greyed spells, Space and Enter doing nothing; a
level-255 hero moving round the list, leaving the picker with Esc and reviving a fallen member; four item
scripts (14 things on two pages: food eaten, worn things changed, the page turned) alone and with three members; three any-key scripts
(`--all`). ASan/UBSan clean on five of them. `tools/mutate_battle.py`: 46 new mutations, all caught (port.md milestone 8e).

**Not built:** the mouse in the lists and the picker; the face's countdown in the picker (only its last step is drawn); key 3 in the item
list and the lists outside a battle (the F-keys); the Sound Blaster sample at a cast.

## WALK.SL step 18: the mouse in the battle, 2026-10-19

Built in C (`src/battle.c`: `fight_mouse`, `ptr_*`, `press`, the mouse parts of the fight pass, the lists, the picker, the rewards bar and
the message box; `src/walk.c`: the world's pointer, `walk_set_mouse` / `walk_mouse*`; `main.c` feeds the window's pointer) and compared
with the original pass by pass with a mouse driver in the oracle: `tools/battle_check.py --mouse PASS:X:Y:BUTTONS` (the emulator's
INT 33h reports a driver with two buttons and function 3 in pixels), with `tools/battle_mouse.py` for random scripts. READ from the code
(`0x260c9`, `0x10b87`, `0x12608`, `0x1287a`, `0x1329d`, `0x1cbfa`, `0x1d48b`, `0x1e10e`, `0x1e472` .. `0x1e7e3`, `0x1f1d9`, `0x1f24a`,
`0x1f716`, `0x1f753`, `0x1fc75`, `0x1fcd7`, the lists' and the picker's mouse blocks, `0x1b5b5`), then CONFIRMED by the comparison.

**The driver** (`0x260c9`, function 3): x kept at most 0x13a and y at most 0xc2; the buttons as the driver gives them (1 left, 2 right).
Every place the program asks it is guarded by "a driver is there" (the segments at DS:0x382 .. 0x396, byte 4), except the corner grabs.

**The pointer** (RPG.LCP 0x300: 6 x 10 after a 4-byte header, read after the palette; colour 0 transparent). One 6 x 10 buffer (the
world buffer + 0x9538) keeps what is under it, for the world, the fight, the lists and the picker in turn. Each loop keeps its own place:
when the place changes, what is under the old one is put back, the new one is kept and the pointer drawn. At the program's start
(`0x10b87`, mouse or not) the place is 0 and the corner is kept; the battle (`0x1cbfa`), each list and the picker start the same way, and
after each list the fight's pointer starts again from the corner (`0x1f24a`, `0x1f753`), after the battle the world's (`0x1329d`).
- **World** (`0x12608`, before the window copy): the pointer on the screen, and over the play window (x 0x3b .. 0xff, y 7 .. 0xb7) also
  into the world buffer at x - 0x30, so the window copy shows it. At a touch (`0x1287a`) what is under it goes back. **The world's
  buttons are not built** (a left press walks towards the pointer through `0x2c8a0`, the right one shoots: `0x136d7`).
- **The touch's left-over:** what the world kept under the pointer is from before the window copy, i.e. what the screen showed before
  the world loop's first pass; with the pointer over the window the battle's first screen shows that patch. `battle_check.py` therefore
  starts the port from the original's screen at the loop's first arrival when `--mouse` is given (the port's own start screen has no
  world window).
- **Fight** (`0x1e10e`, after the spell flashes): the pointer on the screen, and over the field (x 0x3b .. 0xff, y 0x41 .. 0xb2) its
  part inside the field drawn into the field picture (`0x26b97`, rows and columns cut at the field's edges), so the field copy shows it.
  The tabs are drawn with the pointer taken off and put back (`0x1d48b`).

**The fight's buttons** (`0x1e472`, after the keyboard is read; the places are from the first read): a press counts once (`[bp-0x5c]`,
set by a release; never initialised, taken as 0). The right button is Esc (the key latch set too). A press on the music or sound
button (x 0x10d .. 0x13b, y 0x63 .. 0x75 / 0x76 .. 0x89) is key 1 or 2. With no command confirmed the pointer over the tabs (y 0x3c ..
0x49) **chooses the tab without a press**, and a left press confirms it (Space). While the attack's target is chosen, the pointer over a
live front monster (y 0x76 .. 0xb1, x 0x41 .. 0x7c, 0x83 .. 0xbe, 0xc3 .. 0xfe) makes it the target, and a left press attacks.

**The lists and the picker** (each with a "button still down" flag, so a press counts once):
- Magic list: a left press on a row lights it (row = (y - 0x66) / 15, column = (x - 0x40) / 0x60); on the icon (x 0x54 .. 0x63, y 0x4c ..
  0x5b) it casts; the right button leaves. Its row loop reuses the buttons' variable, so after a row press the button tests see the row
  number. When the pointer moves the lit row is lit again by a loop whose counter is the palette counter `[bp-0x1c]`, left at 0x78.
- Item list: a left press on a row lights it (item = column x 5 + row; the loop's counter is the palette counter, left at the item, and
  the pointer's x and y are set to 0); on an icon (x 0x54 .. 0x83) it is key 1, 2 or 3; on the left-hand buttons' corner (x 5 .. 0x31, y
  0x67 .. 0x78) it leaves the list (once a key release has set the latch); the right button leaves. A move lights the row again with the
  palette counter as the loop's (left at 0x78).
- Member picker (read before the keyboard): the pointer over a box (y 0x4a .. 0x5b, from x 0x86, 18 apart, only members in the party)
  chooses it without a press, a left press takes it, the right button gives up. A move redraws the boxes with the "box drawn last"
  variable as the loop's, which the next round then redraws again.
- Rewards bar: any button leaves (`0x1fc75`), and what is under the pointer goes back (`0x1fcd7`, also after a key; not after a loss).
- Message box: the right button closes it once all buttons were up after it opened (`[bp-0xe]` starts at 1).

**Where the reads write:** `0x260c9` writes x then y through two pointers; several callers pass the same buffer twice (the fight's
second read: the field picture; the rewards bar: the hit pictures; the message box: the world's `[bp-0x20]` buffer), so its first word
becomes y. None of this showed in any comparison (why the field picture's width word does not matter was not looked into; the hit
pictures are not drawn after the bar); the port does not write them.

**Checked:** `tools/check_battle.sh`, 18 new starts, all identical (screen, palette, rand, and in the lists now also the party table,
which `battle_check.py` reads from the lists' first argument): the pointer standing still for 300 passes; a walk into the enemy with
the pointer outside the window, then over it; a scripted visit of the magic tab, a row and the icon, the picker (hover, right button, a
member), the item tab, a row, the icons and the corner; a press held from a tab onto a row of each list (the held press counts: the
lists' flags are never initialised either, and the original behaved as with 0, seen once each); a press on the music button held onto a
tab (not confirmed); the pointer going over a dead front monster while a target is chosen; item rows pressed in the pass the pointer
arrives; the picker with the pointer on a box's edge; a message box opened with the right button held; seven random scripts with
spells, items and the picker; one with a level-up's message box. The comparison stops when the world comes back (`--world-passes 0`).
ASan/UBSan clean on three. The 47 earlier starts are unchanged and identical.

**Not built:** the world's buttons (walking and shooting with the mouse, the on-screen F-key buttons), the mouse in MARK.EXE's game-over
screen, the pointer in the dialog some enemies start with.

## WALK.SL step 19: the world's mouse buttons, 2026-10-19

Built in C (`walk_mouse_key`, the mouse part of `walk_keys`, the shake in the pointer's place, in `src/walk.c`) and compared with the
original pass by pass with a mouse driver (`tools/battle_check.py --mouse`, scripts from `tools/battle_mouse.py --world`). READ from the
code (`0x136d7` .. `0x137d6`, `0x13a02`, `0x2c8a0`), then CONFIRMED by the comparison unless it says otherwise.

**In the key part of each pass** (`0x136d7`, after the arrows' release rules, before the 0x82 suppression): with a driver the buttons
are read; the left one makes the key `0x2c8a0(x, y, last)` from the place the pass's pointer block read (`[bp-0x46]`, `[bp-0x70]`; in
the pass a battle resumes, the place before the battle), an arrow from it setting `[bp-0xc0]`; the right one makes it Space (a shot).
`[bp-0x30]` keeps the key after the suppression (`0x137d6`) as `last`. After the key is taken as the held one (`0x139fb`), a set
`[bp-0xc0]` is cleared and so is the held key `[bp-0xc2]`, so a mouse walk is not continued by the arrows' release rules. Both start
at 0 (`[bp-0x30]` SEEN 0 at the first pass).

**`0x2c8a0`, the key of a left press** (SEEN for 26 places, one press each, the key read back after the call; pinned by
`test_mouse_key` in `tests/test_walk.c`):
- The arrow towards the pointer from (160, 100): along the larger of |x - 160| and |y - 100|, across when they are equal (Left below
  x 160, else Right; Down when y is over 100, i.e. lower on the screen, else Up). This is the answer after an arrow (`last` 0x48 .. 0x50: a held press keeps walking
  over the buttons too) and everywhere outside the two button columns.
- Left column (x 4 .. 0x32): F1 .. F8 for y up to 0x15, 0x29, 0x3d, 0x51, 0x65, 0x79, 0x8d and above; y 0xa2 .. 0xae the arrow; from 0xaf
  the A key (the corner icon).
- Right column (x 0x10d .. 0x13b, y 0x3a .. 0xb1): F9, F10 and the keys 1, 2, 3, 4 for y up to 0x4d, 0x61, 0x75, 0x89, 0x9d and above.

**The pointer and the shake:** the pointer is drawn into the world buffer where the shaken window copy starts (`0x2c581` moves the
buffer's start before `0x12608`), so it does not shake with the world. FOUND by the comparison (a shot's spark shook the window and the
port's pointer with it).

**Checked:** `tools/check_battle.sh`, eight new starts in scene 1, all identical at every pass: from (110, 72) three pure walks of 1,500
passes, and three of 2,500 that walk into battles, fight them with the same presses (the item list once) and go on in the world, which
is now compared to the end; from (100, 100) a press held from the window onto the left-hand buttons, and a walk released while the
keyboard port holds an arrow's break code. `make test` (with `test_mouse_key`) passes. `tools/mutate_battle.py`: 11 new mutations, all
caught (three only after the last two starts were added; one left out with a note, it cannot differ).

**Not built:** what the buttons' keys do (the F-key actions, keys 1 to 4, A): a press on a button reaches the same "not built" as the key.
SEEN: a press on F1 in scene 1 makes the original do something at once (the screen changes), so starts must keep off the buttons.

## WALK.SL step 20: F1, talking, 2026-10-19

Built in C (`talk_key`, `talk_begin`, `talk_round`, `talk_done`, `fkey_frame`, `fkey_shade`, `button_toggle` in `src/walk.c`; the message
box moved out of `src/battle.c` into `src/msgbox.c` and is shared) and compared with the original pass by pass (`tools/battle_check.py`,
which counts the world's message box as the loop `0x1b506`). READ from the code (`0x14d46` .. `0x153ca`, `0x19680`, `0x19b9a` .. `0x1a120`,
`0x1afb0`, `0x1c3ba`, `0x2c158`), SEEN in oracle screenshots (`work/b5/probe.py`), then CONFIRMED by the comparison unless it says otherwise.

**Every F-key first** (`0x14d62`): the buffer's first bytes become 1, x, y and the map header's byte 5, and the frame routine `0x19680`
draws the world into the buffer: ground, every villager and party member (the party with the standing frame 1), objects; no shots, rain or
pointer (with byte 5 set it would double the view: not built). The buffer from 0xe00 on (0x9300 bytes) is shaded and the window shown,
twice. The palette's first 48 bytes are copied to a scratch buffer (`[bp-0xb0]`, which the frame routine also uses for the entities'
places) and its first 45 bytes lowered by 16 (at least 0): the message box's palette steps work on that copy, so the colours 0 .. 15 dim
while it waits. After the key's action the pass ends at `0x172f0` (`0x1729e`), past the hero's walk counters.

**F1** (`0x14e94`): its button is pressed (`0x1c3ba`: the 47 x 20 picture at (4, 2) turned round, its inside put back two rows and columns
in, so the light and dark edges change places; a second call lets it go). Three cells are looked at: the hero's (x & ~1, y / 2) and the
two beside it across the way he faces; then the three one step further, up to three steps. The first villager in one of them who is not
an enemy (byte 0x31 = 0) is the one: he is turned to face the hero, the world is drawn and shaded once more (not shown), and he turns
back. The loop uses the hero's walk counter `[bp-0x3c]` for each villager's x, so after F1 it holds the last one looked at (FOUND by the
comparison: the hero's frame changed a pass early). It also writes the cells into the first bytes of the map buffer (`[bp-0xba]`,
whose raw header is not read again); the port leaves that out. With nobody found the button is let go and the pass ends.

**The talk** (`0x19b9a`): the 96 x 84 middle of the buffer doubled into the window (`0x2c658`, as at a battle's start). A villager has
five 7-byte entries from byte 0xc: a flag that must be set (byte, bit), a flag to set or a member who joins (0xc9 .. ), a kind, a word
message. The entry used is the last of the leading ones whose message is not 0 and whose flag is set, the first one if its flag is not
set (`0x2c158`, the flag table at party table + 200). A joining member takes the first free place of the party (records swapped), the
villager goes (position 0) and the flag at bytes 0x28/0x29 is set; otherwise the entry's flag is set, unless its byte 4 is 0xff. Then the
message box (colour 0x50, the wait flag; `0x1afb0`), again with the next message as long as the text's last byte before `#` is 0x7f. At
the end the coins are drawn again, the pointer starts from the corner and the button is let go.

**The message box, a correction to step 16:** its two shadings run over the 4 bytes past the copy where they are (the world buffer's
0x7305 .. 0x7308, in a battle the backdrop's 0x1ea5 .. 0x1ea8), so they are left darkened and the next box takes them darker still
(FOUND: the second box of a five-box talk differed in its last pixel). Step 16's boxes never showed it (one box per member, each taking
the same 4 bytes two shadings further was not covered).

**Checked:** `tools/check_battle.sh`, 12 new starts in scene 0 and 1, all identical at every pass (screen, palette, rand, and in the world
the villager and party tables, flags included): one talk; a five-box talk; walks with F1 now and then from the shipped start and from
beside a villager (`tools/walk_keys.py talk`, 2,500 passes each, 168 to 313 box rounds); F1 finding nobody in scene 1; F1 pressed with the
mouse and boxes closed with the right button; a villager whose second entry's flag is set, and not. `tools/mutate_battle.py`: 17 new
mutations, all caught, two left out with notes (they cannot differ in scene 0); the message box's 16, now in `msgbox.c`, all caught
against the talk and level-up starts. Scenes 13, 37, 39 and 43 have villagers who join, but their first world pass already differs
(not looked into), so joining is READ only.

**Not built:** talks that sell something (villager byte 0x30 set: a box without the wait flag, then `0x18c36`, the price at word 0x22 and
the item at byte 0x21; scenes 2, 21, 27) and that ask for an answer (an entry's byte 4 = 0xff: `0x18fdc`, compared with the word at
0x28 + 2 x the entry; scenes 9, 23). Both are reported. The other F-keys (still reported as their keys).

## WALK.SL step 21: talks that sell and talks that ask, 2026-10-19

Built in C (`yesno_open`, `yesno_round`, `talk_buy`, `ask_open`, `ask_round`, `ask_marks`, `talk_close` in `src/walk.c`; boxes without
the wait flag in `msgbox_open`) and compared pass by pass (`tools/battle_check.py` now also counts the loops `0x18d23`, "yes / no", and
`0x19166`, "number"). READ from the code (`0x19dd8` .. `0x1a120`, `0x18c36` .. `0x18fda`, `0x18fdc` .. `0x1967f`, `0x1afb0`, `0x27670`,
`0x26450`, `0x25b94`), SEEN in the oracle's screens, CONFIRMED by the comparison.

Step 20's "Not built" is now built (that paragraph stays as it was written). The only villagers with these talks are in scenes 2, 9, 21,
23 and 27, and 9 and 23 are dark scenes (a lit circle round the hero; not built: their first world pass already differs). So the checks
give their talks to a villager of scene 0 (`tools/walk_scene_data.py --talk V:SCENE:W` copies bytes 0xc .. 0x31 of a record in
`__1.NOW`).

**The boxes of such a talk do not wait** (villager byte 0x30 set, or the entry's byte 4 = 0xff): the screen's rows 134 .. 179 are kept
first (`0x2723c` into the scratch buffer + 0x9c40), then every box of the talk is drawn at once (0x7f still chains them; no shipped
villager of this kind has a chain) and stays. Without the wait flag `0x1afb0` makes the box only as big as the text: 320 x 16 for fewer
than 40 bytes, narrowed by 16 pixels for each 8 of centring, 320 x 31 for fewer than 80, else 320 x 46, one row above the text. The copy
it shades is still 0x3980 bytes, so whatever the box's size its first 320 pixels are shaded twice and the rest four times, and the 4
bytes at the world buffer's 0x7305 are shaded twice as before. After the talk the kept rows are put back.

**Selling** (`0x18c36`): the 47 x 20 pair of buttons at (269, 38), "是" and "否", is kept three ways: as it is, with the left half
pressed and with the right half pressed (`0x1c3ba` on each 23 x 20 half). One pass a round: the palette step on the dimmed copy every
26th round, the pressed half drawn again when the choice changed, Left = yes, Right = no, Enter or Space takes it (the raw port 0x60, so
a key held counts every round); with a mouse, its own pointer (the pointer's picture and the 6 x 10 at its + 0x40), and over the buttons
(x 270 .. 315, y 38 .. 57) it chooses by half and the left button takes it. Yes with enough coins: the price (word 0x22) is taken and the
item (byte 0x21) put in the first empty place of 0x14 .. 0x27 of the first member with HP; the members are looked at only up to the
first who is not in the party. Not enough coins, no place, or no: nothing happens and nothing is said.

**Asking** (`0x18fdc`): RPG.LCP 0x1643b (0x4be bytes, decoded into the world buffer) holds ten 8 x 8 digits and the 94 x 16 box drawn at
(113, 0) (it comes in over 256 steps of `0x27670`, a pixel one colour index nearer the picture's each, all in the same pass, so the loop
starts with the picture). A number 0 .. 50,000 starts at 0 with the units chosen (marks of colour 9 above and below the digit). A round:
the palette step every 23rd; the five digits drawn (`0x26450`, `0x25b94`, at (138 + 9 i, 4)); a key only after a release (a scancode
> 0x7e): Up adds the digit's unit (at most 50,000), Down takes it away when the number is that large, Left the next digit up and Right
the next down, both round; Enter takes it. Mouse: left button over the box's left end is Up, right end Down, middle Enter; the right
button is Left. The right number is the word at 0x28 + 2 x the entry's place in the record; it sets the entry's flag (and plays sound
`0x1c68e`, not built). SEEN: scene 9's villager 18 wants 38945 (the box `00845` part-way, then flag 10 bit 6 set); scene 23's villager 5
asks two questions in turn, then gives a box that waits.

**Stack bytes the two loops start from (FOUND by the comparison and a write watch, `work/b5/wprobe.py`):** neither loop sets its palette
count, and the number box does not set its key latch. The yes / no count lies where the message box kept the high word of the text's
file offset (`[bp-8]`; 0 for messages below 513); the number box's count lies where `0x19680` pushes an argument, the low byte of a
buffer's offset, 0x16 in every run; its latch where the message box kept the last Big5 character's second byte (or the box's height),
never 0. The latch only shows when the first round's key is an arrow made while F1 was still down (FOUND: a key made one pass after F1
already counts); any other first key, F1's own make included, uses it up or sets it again.

**The number box's mouse needs a key release.** The latch is set only by a scancode above 0x7e in port 0x60, and a mouse press stands
for a key only in the rounds it is held, so after each press the port must show a break code (any key's release, the last one the
keyboard sent) before the next press counts. In a run with no key since the start the port reads 0 and the box ignores every press
except the middle one (Enter does not go through the latch). SEEN in the oracle; the check's mouse start releases Escape first.

**The coins are drawn when the world starts** (`0x10bc9`, after the pointer's corner is kept), FOUND when a start with 100 coins
differed at its first pass: every earlier start had 0 coins, which the start screen already showed. Built (`mouse_start`).

**Checked:** `tools/check_battle.sh`, 17 new starts, all identical at every pass: selling with yes (bought, coins taken), no, too few
coins, member 0's places full, a two-row box, with the mouse (the right button over the buttons, no, then yes) and waited on for 600
passes; scene 9's question answered right and wrong; scene 23's villager 5 three times in a row (two questions, then a box); scene
23's villagers 7 (two rows) and 8 (13 bytes); the number set with the mouse; an arrow made over F1; 50,000 and one more; Down with
too little. Keys: `tools/number_keys.py`. `tools/mutate_battle.py`: 34 new mutations, all caught after the four starts above were
added for the ones first missed; one left out with a note (the yes / no count, 0 for every shipped seller).

**Not built:** the sound of a right answer; the dark scenes the asking villagers live in.

## WALK.SL step 22: the dark scenes, 2026-10-20

Built in C (`walk_double_dark`, `walk_double`, `records_again` in `src/walk.c`) and compared pass by pass with `tools/battle_check.py`.
READ from the code (`0x1124a`, `0x108cd` .. `0x1091d`, `0x11df1` .. `0x11e1e`, `0x122df` .. `0x1230c`, `0x125c6` .. `0x12608`, `0x19680`
.. `0x199b0`, `0x2c362`, `0x2c456`, `0x2c581`, `0x2c658`, `0x172f0` .. `0x1731c`, `0x18b82`), SEEN in oracle screenshots, the oval's
source FOUND with a write watch (`work/dark/wprobe.py`, scratch), CONFIRMED by the comparison.

**Map header byte 5** (`[bp-4]`, any value not 0; 1 in 23 scenes, 2 in scene 6) makes a scene dark. **Correction to step 13:** its list
of scenes (9, 10, 11, 12, 24, 25, 29, 47) was short (the scenes are 6, 9 .. 20, 23 .. 26, 29, 37, 39, 43, 45, 47, 50), and the world is
not only doubled but darkened. **Correction to step 20:** scenes 13, 37, 39 and 43 differed at their first pass because they are dark.
**Correction to step 21:** the lit part is an oval, not a circle.

**A pass** in a dark scene: the ground (`0x2c362`) and the objects (`0x2c456`) are drawn only for the 14 x 13 cells from the buffer's
(64, 56) (the view flag), the villagers, party and shots as always; then, after the shake (`[bp-0xa]`), `0x2c581` takes the 96 x 84
pixels from (64, 57) + the shake and doubles them into the scratch buffer `[bp-0x20]` from (16, 16), 224 wide: source rows 34 .. 60 as
p, t[p] / t[p], t[t[p]] (t = the shading table at the buffer's + 0xfe78, as the battle's doubling), the 34 rows above and 23 below as
p, 0 / 0, 0. Then 0x187a words of it from row 83 (offset 0x48a0) are ANDed with the oval: RPG.LCP 0x4350, 0x15a4 bytes run-length coded
to 12,544 (56 rows of 224: 0xff inside, 0 out), which the start-up (`0x108cd`) reads in every scene, to the ground map's + 0xca70. So the
middle band shows the shaded, doubled oval round the hero and colour 0 beside it, and the rest one pixel in four. The pointer is drawn
into the scratch buffer and the window copied from it, both with no shake.

**The F-keys' world drawing** (`0x19680` with the buffer's byte 3 set, also at the start, `0x11c25`) doubles the same 96 x 84 with
`0x2c658` (no darkening) into its scratch buffer and copies its first 0x5080 words back over the world buffer: a talk in a dark scene
shades a lit doubled view, and the talk's own zoom doubles it again. The start (`0x11c25` .. `0x11cca`) fades in that lit view before
the first pass (not built: the port has no world fade-in).

**After an F-key's action the scene records run again** (FOUND by the first join compared: scene 43's villager 3 joins, and the next
pass's picture still showed objects 0x0a and 0x0b at cells (37, 17) and (38, 17), a picture of him; a write watch caught the record loop
`0x1171c` / `0x117eb` clearing them in the pass after the talk). The pass's end at `0x172f0` with `si` = 0x384 (`0x1729e`, every F-key
action) or a record's offset goes to `0x18b82`, which runs the record loop again when `[bp-0xbe]` is not 0. `[bp-0xbe]` is 1 from the
start (`0x10bad`), then the kind of the last record stepped on (`0x1731c`), and 0 after the key at `0x14cd6` (not built). The port did
run the loop after every record stepped on; it now does that only when the kind is not 0 (kind 0 is not built) and also after a talk.
The earlier talks in scene 0 never changed a flag a record looks at, so they could not show it.

**Joining is CONFIRMED** (step 20 read it only): scenes 37 (villager 4), 39 (4) and 43 (3) and scene 13's villager 1 with flag 0 bit 4
set; the member takes the first free place and the villager goes.

**Checked:** first a scratch survey (`work/dark/survey.py`): all 24 dark scenes, 300 passes idle and 300 with walking and shooting keys,
all identical, including a battle and a lost one in scene 10. Then `tools/check_battle.sh`, 19 new starts, all identical at every pass:
scenes 6, 9, 13, 23 and 47 idle (300) and with walking and shooting (1,500; in scene 9 into an enemy and a lost battle); F1 walks among
scene 23's ten villagers (2,500, 603 and 821 number-box rounds); the mouse walking and shooting in scene 13; the four joins; scene 9's
question (38945) and scene 23's villager 5 in their own scenes. Unit test `test_doubling` (the band edges, the zeros, the mask's first
and last byte). `tools/mutate_battle.py`: 15 new mutations, all caught (6 by the unit test); two more left out with a note (`[bp-0xbe]` is 0 only after a kind-0
record, itself not built). ASan/UBSan clean on the dark starts; the quick `tools/check_walk.sh` all identical.

**Not built:** the world's fade-in at the start (built in step 23: a zoom, not a fade); the key at `0x14cd6` that clears `[bp-0xbe]`.

## WALK.SL step 23: the world's start zoom, 2026-10-20

Built in C (`walk_zoom_step`, `walk_zoom_scale`) and compared step by step with `tools/zoom_check.py`, which keeps the oracle's screen
after each step (at `0x19b75`) and at the main loop's first arrival. READ from the code (`0x10bb8`, `0x11c17` .. `0x11ce6`, `0x19680`,
`0x272df`, `0x19ab2` .. `0x19b98`, `0x2728d`, `0x26e5f`, `0x27328`, `0x274cd`), CONFIRMED by the comparison.

**Correction to step 22:** what it called the world's fade-in is a zoom. With `[bp-0x74]` set (from the start, `0x10bb8`; also set at
`0x138c8` and `0x17b0b`, a scene change, not built) the end of the start-up's record loop (`0x11c25`) draws the world as the F-keys do
(`0x19680`: the party standing; in a dark scene the lit doubled view), keeps its 192 x 168 window in `[bp-0xba]` (`0x272df`) and runs
`0x19ab2`: 13 steps, the part w x h at `0x3654 - 0x487 k` of the kept window (24 x 24 at step 0, 14 wider and 12 taller each step, so
centred, up to 192 x 168) copied with a size header into the world buffer + 0x7e04 (`0x2728d`), scaled to 192 x 168 into the world
buffer (`0x26e5f`) and shown at (64, 16) (`0x27328`). Between the steps it only empties the BIOS key buffer (`0x274cd`); it waits for
nothing, so its pace was the machine's. It also restarts the song if it has ended.

**The scaling** (`0x26e5f`) clears the picture, then for each source row writes 192 pixels: a pixel register starts as the row's first
source pixel, is written, set to 0, and loaded with the next source pixel each time a 16-bit sum (from 1, adding 0xffff * w / 192)
carries; after the row, rows are skipped (left 0) until a second sum (from 1, adding 0xffff * h / 168) carries. So each source pixel
appears once, as a dot, and the dots close up as the part grows; even the last step, at full size, leaves out one column and one row
(source column 191 and row 167 go, a 0 column after the second and a 0 row after the second). The first pass then draws over it.

**The song** starts at the loader's end (BIOS tick 10, the music step), before the zoom; the port started it after the first pass and now starts
it before the zoom.

**Checked:** `tools/check_battle.sh`, section "zoom": scenes 0, 1, 34 and the dark scenes 6, 9, 23 and 47, all 13 steps identical, and
the screen unchanged from the last step to the main loop. Unit test `test_zoom_scale` (the full-size pattern and a 24 x 24 part).
`tools/mutate_battle.py`: 8 new mutations, all caught (4 by the unit test). The pace is a choice: one step a pass of the world's 35 a
second, about 0.37 s for the zoom.

## WALK.SL step 24: the rest of the battle: story enemies, keys 1 and 2, the picker's flash, the game-over screen's mouse, 2026-10-21

Built in C and compared pass by pass (`tools/battle_check.py`, `tools/gameover_check.py`). READ from the code (`0x1275e` .. `0x128ee`,
`0x12e56` .. `0x130e2`, `0x13300`, `0x172f0`, `0x25fe0`, `0x25ff1`, `0x106c0` .. `0x1072b`, `0x1e81c` .. `0x1e98f`, `0x22041` .. `0x220a2`,
MARK's `0x10502` .. `0x10537`), SEEN in the oracle, then CONFIRMED by the comparison unless it says otherwise.

**Story enemies** (villager byte 0x2f set, byte 0x31 > 0; 13 in the shipped `__1.NOW`: scenes 4, 18, 23, 35, 40, 41 and 43, all with sprite set 6,
which shots cannot kill):
- **The touch:** the hero's walk counter `[bp-0x3c]` takes the member's party offset and his frame `[bp-0x5e]` the record's byte 0x2f (for every
  enemy, `0x1276c`, `0x1277f`). The enemy is not moved. If its word 0x11 (read by `0x25fe0`, which only returns the word) is not 0, the message
  box `0x1afb0` opens with that message, colour 0xc2 and the wait flag, the same call as a level-up's but for the colour (palette steps on the
  palette itself); the battle starts when it closes. Scenes 4, 23, 40 and 41 have such messages. The group is byte 0x30, and every slot holds
  a monster (step 13), so all five must die.
- **After the battle** (`0x12fb5`, after the rewards bar, before the level-ups): if the battle returned 5 (all five killed) the record's
  position becomes 0 (`0x25ff1` writes a word) and its flag (bytes 0xc, 0xd) is set; `di` = 0xffff then makes the pass end at `0x172f0` with
  `si` = 0xc8 (left by the level-up loop), which goes to `0x18b82`: the scene records run again and the villagers' steps and the hero's walk
  frame are skipped. Otherwise (fled) the five party records and the 13 trail entries are put at the record's bytes 0x28, 0x29 (with flag 1),
  and the pass goes on as after any battle. Fleeing a story enemy is therefore the only way a battle moves the party.
- SEEN: scene 4's enemy (group 77) kills a hero with 9,000 HP in about 240 passes; scene 35's records 8 to 11 (groups 4 .. 18, no message)
  can be beaten.

**Keys 1 and 2 in the fight** (scan codes 2 and 3; `0x1e81c` .. `0x1e98f`): only when the card is there (`0x2cc5:5` for the FM music,
`0x2cc5:0xc` for the Sound Blaster's sounds: 0 no card, 0x81 on, 0x80 off). The flag is switched, the music stopped (`0x266f6`) or the
battle's song started from its beginning (`0x2660c`), and the right-hand button (47 x 20 at 0x7b8d or 0x948d) pressed or let go
(`0x1c3ba`) with the pointer taken off before and its place only kept after. At the start (`0x106c0` .. `0x1072b`) the original sets the
music flag to 0x81 and presses the music button when it finds an FM card, and the same for the sound button and a Sound Blaster. The
oracle had neither, so step 14 saw the keys do nothing; `battle_check.py --opl` turns on the oracle's AdLib.
- **A value found with the FM card:** the battle's `[bp-0x58]` (the hit face's pass counter, never set before it counts) is 0 without the
  card and 1, 3, 4 or 5 with it, by when the FM driver's timer interrupt (`0x2aa52`, which pushes its registers on the current stack)
  last came while the stack was below the world's frame (FOUND by a write watch). The counter adds 1 and asks "above 1", so all those
  values act alike; the port uses 1 and the comparison takes 1 .. 0xfe as equal there.

**The picker face's flash** (`0x22041` .. `0x220a2`): three times, the 16 colours from the result down, each drawn 100 times (`0x25c18`).
The port now draws all 48 steps and shows each one (10 ms each in real time, a choice; the original's pace was the machine's).
`battle_check.py` compares the screen after each step (the oracle's at `0x22087`): all 48 identical in the five heal and revive starts.

**MARK.EXE's game-over screen with a mouse** (`0x10502`): when a driver was found at MARK's start (`0x10120`), INT 33h function 3's buttons
not 0 end the screen, in the pass they are seen, without waiting for a release (a button held from the start ends it at pass 0). Esc does
nothing. Both buttons, a held one, Space and Enter are checked against the oracle.

**The Sound Blaster** (READ and SEEN with the driver at segment `0x190e` stubbed to succeed; not built): with one, WALK.SL reads all of
`SND_EF.1` (0x6179 bytes) at the start and the battle reads `_1_.LOC` (0xede6 bytes); `0x1c68e(pointer)` stops any sample and plays the
VOC sound-data block at the pointer (8-bit PCM or 2-, 3- or 4-bit ADPCM, 8,000 to 13,157 Hz). The call sites: `SND_EF.1` 0 at a spark's frame 2
(`0x122a7`) and at a touch (`0x12df4`), 0x5f80 at a kill (`0x1243c`), 0x127d at a roof (`0x17b9e`), 0x1bf5 in the F-key handlers, 0xc8 at a
right answer (`0x1a0fd`); `_1_.LOC` 0xbc69 in the monsters' blow (`0x1d8f1`), 0x37a3 in the party's attack (`0x1dfb9`, `0x1e0ba`) and
offsets from a table at `0x1f083` and `0x1f4f4` (what each one sounds like was not listened to). Nothing on screen depends on them. SEEN:
the stubbed run plays `SND_EF.1` 0 at the touch and then `_1_.LOC` blocks through a whole battle.

**Checked:** `tools/check_battle.sh`, 28 new starts and checks (149 in all), all identical: scene 4's box then a lost battle; three
flights from it by a strong hero (put back each time); scene 35's record 8 won (taken away, flag set, pass ended, then 1,864 world passes)
and record 11 fled twice; scene 40's box closed with the right button and after 120 passes; with the FM card key 1 four times and key 2,
a won battle back in the world, walks into the enemy at two clock readings, the button pressed with the mouse (also on its edge); the
game-over screen ended by Space, Enter, either button and a held button, and not by Esc. Unit test `test_boss_after`. `tools/mutate_battle.py`:
23 new mutations, all caught (three only after the unit test and the edge start were added); two left out with notes.

## WALK.SL step 25: the Sound Blaster's sounds, 2026-10-22

Built in C and compared pass by pass with the original run with a stubbed Sound Blaster (`tools/sb_stub.py`, `battle_check.py --sb`).
READ from the code (`0x1c68e`, the driver wrapper at `0x290f1` .. `0x2918f`, the 26 reads of the busy word, the call sites below,
`0x1cd79` .. `0x1cde7`), SEEN in the stubbed oracle, then CONFIRMED by the comparison unless it says otherwise.

**The driver** (segment `0x190e`, a wrapper round CT-VOICE: `0x11` detect, `0x49(on)` speaker, `0x6d` stop, `0x76` terminate, `0x9c(far
pointer)` play). `0x1c68e(pointer)` does nothing unless the sound flag `0x2cc5:0xc` is 0x81; then it turns the speaker off, stops any
sound, turns it on and plays the bare VOC sound-data block at the pointer (no file header). The voice driver's status word `[0x24e]`
(not 0 while a sound plays) is read at 26 places: the spark's sound waits for it, three F-key handlers spin on it, and every loop's
clean-up stops the sound when it is 0.

**What sounds when** (12 blocks; the codec in brackets: 0 8-bit, 1 4-bit, 2 2.6-bit, 3 2-bit ADPCM):
- `SND_EF.1` 0 (2): an enemy destroyed by a shot (`0x122a7`) and a battle's start (`0x12df4`, after the party strip).
- `SND_EF.1` 0x5f80 (3): a spark reaching its frame 2 (`0x12404` .. `0x1243c`), only when no sound plays (`[0x24e]` = 0).
- `SND_EF.1` 0x127d (2): a roof taken off and put back (`0x17b9e`, `0x17c2a`).
- `SND_EF.1` 0x1bf5 (0): a right answer to an asking villager (`0x1a0fd`) and the six F-key handlers (not built).
- `_1_.LOC` 0xbc69 (0): a monster's blow (`0x1d8f1`); 0x37a3 (2): the party's hit landing (`0x1dfb9`) and a spell's flashes ending
  (`0x1e0ba`); 0 (2): a monster starting to die (`0x1ff7c`).
- `_1_.LOC` 0x6771, 0x7742, 0x8849, 0x8e01, 0xa1bb (1, 0, 2, 0, 0): each member's shout when he attacks (`0x1f083`) or casts at the
  monsters (`0x1f4f4`), by his number (party byte 3), from a table the battle builds at `0x1cd79` (at `_1_.LOC`'s buffer - 0x2e).
- The rewards bar's end cuts the sound (`0x1fcc8`).
- **Not built:** `SND_EF.1` 0x5571 at `0x17a3f` (a scene record of a kind not built) and the sound of the routine `0x22c42` (`0x24225`,
  called from `0x17df7`; an action not built).

**The busy word in pass time.** A stepped comparison has no real time, so `sb_stub.py` and `walk_dump`'s `sb` share a model: a sound
started in pass P plays until the top of pass P + D, D being its samples at its rate at 35 passes a second, rounded up (`sfx_passes`:
9 to 187 passes); a stop or a wait ends it. In the real game the busy word is the sink still playing the sound (`audio_playing`). A GUESS
in both: what the sound card took is not modelled.

**The decoders.** The 2.6-bit and 2-bit codecs written out as rules, like the 4-bit one (`docs/formats.md`), from DOSBox-X's tables. For
all 12 blocks the port's samples equal a decoder that uses DOSBox-X's tables directly (`scaleMap_ADPCM2/3/4`, `adjustMap_ADPCM2/3/4`,
copied as data): 153,469 samples, identical. Not compared with real hardware, and nobody has listened to them.

**Checked:** `tools/check_battle.sh`, 11 new `sb_` starts (the sounds by pass and by block, as well as everything else), all identical:
a won battle and the world after it (105 sounds), a party of five (146), spells, fleeing, the house door both ways, shots that destroy
enemies, a right answer, a story enemy beaten, a walk-in with the monsters first, key 2 off and on again, the sound button clicked.
Unit tests `test_adpcm_short` (hand-worked samples, blocks, `sfx_passes`) and the real blocks' codecs and lengths. `tools/mutate_battle.py`:
34 new mutations, all caught (9 by the unit tests). ASan/UBSan clean on five starts. A real-time run with music off wrote sound.

## WALK.SL step 26: F2, looking, 2026-10-24

Built in C and compared pass by pass with the original (`tools/battle_check.py`; the look's box is the message box loop `0x1b506`).
READ from the code (`0x157f5` .. `0x15a89`, `0x2c28f`, `0x1c5e2`, `0x27123`, `0x25b94`), SEEN in oracle runs (`work/b8/probe_look.py`:
the calls made, `[bp-0xb0]` dumped at F2's entry), then CONFIRMED by the comparison unless it says otherwise.

**The shared start:** as step 20 says, the world drawn and darkened twice and the dimmed palette copy (`0x14d62` .. `0x14e94`) come
before every F-key's own code (`[bp-0x58]` 0x3b .. 0x44). The port had put them in F1's handler; they are now `fkey_begin`, called for
F1 and F2. SEEN again: F2 never makes the copy itself, and the dump at its entry already held the current palette dimmed.

**F2** (`0x157f5`): the pointer's corner is put back and F2's button pressed (47 x 20 at (4, 22), as F1's); the pointer's corner is not
kept again afterwards, unlike F1. Two cells are looked at: the hero's own (x & ~1, y / 2) and the one to its right when he faces up or
down, the one below it when he faces left or right; up to three times, a step further the way he faces each time, both cells moving. Each
is searched for a scene record of kind 9 (`0x2c28f`: the first of the 50 whose x, y and kind match; 0x2ee for none). The hero's walk
counter and frame (`[bp-0x3c]`, `[bp-0x5e]`) hold the first cell's x and y as they step. With nothing found the button is let go.

**A kind 9 record:** byte 0xe is a picture of `SPIC.LCZ` (1 .. 13; 0 for none), byte 3 on three 4-byte entries (a flag, its bit, a word
message). The message is the one before the first entry whose flag is not set, or before the first whose message is 0, or the third's:
the loop at `0x15959` steps while the flag is set and the next entry has a message. Before the first entry are the record's bytes 1 and 2
(y and the kind), so a record whose first flag is not set would give a message number of 0x09yy; every shipped record's first entry is
flag 0 bit 0, which the shipped start sets. Shipped second entries: flag 6 bit 0 (scene 2), 49 bit 7 (scenes 16, 32), 5 bit 1 (16),
42 bit 3 (27), 10 bit 0 (30). The third entry's message is bytes 0xd and 0xe, so its high byte is the picture: none of the records
with a second entry has a picture, so no shipped third entry has a message.

**The picture** (`0x1c5e2`): `SPIC.LCZ` is 13 pictures of 100 x 70 bytes, not packed (91,000 bytes; the name says LCZ). Picture N is read
from (N - 1) * 7000 into the start of the world buffer, a frame of colour 0x28 is drawn 102 x 72 at (109, 45) (`0x27123`), and the
picture is copied inside it at (110, 46) (`0x25b94`). Nothing puts the window back: the next pass draws over it. This answers the
open questions of the picture files (above) on `SPIC.LCZ`: its pictures are signs (picture 4 is a map with the area of the 宏特羅
continent).

**The text:** the message box `0x1afb0` with colour 0xb0 and the wait flag, its palette steps on `[bp-0xb0]` (the copy above), then the
next message while a text ends in 0x7f (scene 6's records 10 and 11 give five boxes). Its third argument, the scratch buffer it reads the
text into, is the map buffer `[bp-0xba]` here (the screen's segment for a story enemy's box); the port reads the text into its own memory,
so this has no effect in the port. After the look the button is let go and the pass ends through `0x1729e`, as for F1.

**The facing at the start:** whatever byte 2 of the party record says, the world starts with the hero facing down (dir 1, SEEN in the
calls; not traced to where it is set). The checks take a step first to face another way.

**Checked:** `tools/check_battle.sh`, 22 new `look_` starts, all identical at every pass: a sign held 100 passes (two palette steps);
found on the third step, not found from four cells, found by the second cell; facing left, right and up; pictures 2 to 6 and 8 (scene 6,
dark); five boxes and two; scene 2's second entry with its flag set and not; F2 clicked with the mouse and closed with the right button;
four 2,500-pass walks with F2 three times in four and F1 the fourth (`tools/walk_keys.py look`). F1's starts unchanged after the move.
Unit test `test_look` (the record search and the entry rule, hand-worked). `tools/mutate_battle.py`: 27 new mutations, all caught (3 by
the unit test); one left out with a note: the records run again after a look, which changes nothing in any scene with the shipped
flags (`work/b8/rerun.c` re-ran every scene's records after up to 400 passes of animation). ASan/UBSan clean on five starts. A real-time
run under SDL's dummy drivers looked twice without trouble.

**Found on the way, not fixed:** a start with the hero within 9 cells of the map's top (y / 2 <= 9) differs from the original on its first
pass, in lit and dark scenes alike (scene 1 at y / 2 = 5 and 9, scenes 6, 9 and 13 at 9): the original's window shows the map's top rows
as a jumble of tiles where the port shows plain ground. `walk_draw_ground` reads nothing above the map (word 0 there); the original
apparently reads whatever is before it. Not looked into further; no check started that near the top before. (Fixed the next day, the
same date: step 27.)

## WALK.SL step 27: the view past the map's edges, 2026-10-24

Fixed in C and compared pass by pass. READ from the code (`0x2c362`, `0x2c456`, the start-up's allocations `0x10057`, `0x1019c`,
`0x10204`, the reads `0x10aaf` and `0x172df`), SEEN in the oracle (`work/b8/probe_gmap.py`: both drawings' segments dumped at pass 5, then
compared buffer by buffer with the port's, `work/b8/segcmp.c`, `artcmp.c`, `headcmp.c`), then CONFIRMED by the comparison.

**What the drawing does at the edges.** The ground (`0x2c362`) works out its first cell in two bytes, `bh` = y / 2 - 10 and `bl` =
(x & ~1) - 0x16, so above the map's top the row wraps to 0xf6 .. 0xff and left of its left edge the column wraps within the row; it
then adds 2 a cell and 0xce a row in 16 bits, from the map's offset in its segment (0x16), and those wrap at 64 KB too. The objects
(`0x2c456`) do the same in bytes (row << 7, column / 2, from the object map's 0x9300). So past an edge the view reads what lies there
in the segment, never zeros. The port had taken cells outside the map as tile 0 (and the object columns as rounded-down negatives;
the unit test that pinned that was wrong and is corrected).

**The map segment** (0xfb70 bytes from `0x1019c`, at its segment's 0x16): the ground map (0x6400), the villagers' sprite bank
(0x2800), the villager table and records (0x6ea), the object map (0x3200), `NUMBER.LCZ` 0x359b's 0x585 bytes as read (`0x10aaf`, for
the rewards bar), a 0, the dark scenes' oval (0x3100); then the allocator's header of the next block and the map buffer `[bp-0xba]` from
0xfba6, which holds the scene's `__2.ALL` block as read until the start zoom keeps its 192 x 168 window there (`0x172df`). Below the map
the view reads the sprite bank (and the objects the end of the object map, the `NUMBER` bytes and the oval); above it, the oval's end,
the header and the zoom's window, and at the top right the segment's own first 0x16 bytes.

**The art segment** (0xffdc bytes from `0x10057`): the object sprites at 0x16 (0x7f80), the tiles at 0x7f96. A word read past the map
gives tile numbers whose offset (word << 6, 16 bits) can pass the segment's end and wrap into the sprites (FOUND: below the bottom the
original's tiles there were the sprites' pixels, not zeros).

**A GUESS:** the allocator's headers (the segment's 0 .. 0x15, 0xfb86 .. 0xfba5, the art segment's 0 .. 0x15 and 0xfff2 ..) hold the
segment numbers and sizes DOS gave the oracle's run; the port uses those bytes. Only the top edge's right end reads them, and no
place there can be stood on.

**Where it matters in play.** `work/b8/edges.c` tried every place the hero can stand in the 53 scenes: none is within 10 cells of the
top or 11 of the left, but the bottom (y / 2 > 87) is reachable in scenes 2, 3, 4, 5, 7, 9, 24, 33 and 50 and the right (x > 228) in
1, 2, 3, 7, 9, 38 and 42. The right edge already matched (the columns run on into the next row, in both). The bottom is the real fix.

**Not modelled:** F1's cells and a look's text written into the map buffer's first bytes (step 20, step 26), which the rows above the
map would then show; no place there can be stood on.

**Checked:** `tools/check_battle.sh`, 11 new `edge_` starts, all identical at every pass: above the top (lit at y / 2 = 5 and 9, at the
right end, dark scene 13), left of the left edge, below the bottom in scenes 2, 4, 7 and 24, and walks along the bottom of scenes 4 and 2.
`tests/walk_dump.c` now keeps the zoom's window at its start (`walk_zoom_keep`), as the game does. Unit tests for an object column
and row past the edges and a ground row below the map. `tools/mutate_battle.py`: 15 new mutations, all caught (3 by the unit tests);
two left out with notes (no start reaches them).

## WALK.SL step 28: F3, taking, 2026-10-24

Built in C and compared pass by pass with the original (`tools/battle_check.py`, which now also counts F3's two wait loops,
`0x160b0` and `0x164ad`, as loop kind 10). READ from the code (`0x15bbd` .. `0x165df`), then CONFIRMED by the comparison.

**The search.** After the F-keys' shared start, F3's button is pressed (at (4, 42); the pointer's corner put back, not kept again).
Three cells are looked at: the hero's own (x & ~1, y / 2), the next one right and the next one left when he faces up or down (below and
above when he faces left or right); then each one cell further the way he faces, three times, for a scene record of kind 2
(`0x2c28f`). The third cell's x and y are left in the hero's walk counter and frame (`[bp-0x3c]`, `[bp-0x5e]`). A record found must have
its flag (bytes 3, 4) set.

**An item** (byte 5 = 0, byte 0xd the item, not 0): it goes into the first empty one of the 20 item places of the first member whose
number is below 5 and whose HP is not 0 (others are passed over; unlike a seller's, the search does not stop at a member who is not in
the party). The record's item byte is cleared and the flag at bytes 0xb, 0xc set. Its name, the first 12 bytes of its `OBJECT.LCC`
record ((item - 1) * 30), is read into the map buffer `[bp-0xba]` and shown.

**Coins** (byte 5 = 1, the word at 0xd, not 0): added when the total stays at most 60,000; the word cleared, the coins drawn again
(`0x1bd6e`), the flag set; the text is the sum as five digits (`0x26450`) with up to four leading zeros turned to spaces, a space and
Big5 銀兩 (`aa54 aaf7 b9f4` as bytes).

**No place, or too many coins:** message 11 in a box (colour 0x20, the wait flag), its palette steps on the palette itself.

**What is shown:** the 12 bytes as a 96 x 14 picture in colour 0x10 (`0x243ec`, the battle's `text_pic`, now `battle_text_pic`), its
shape in colour 0x1e one pixel up, down, left and right (`0x25b54` four times), the picture over it (`0x27371`) at (112, 120); nothing
puts the window back (the next pass draws over it). Then `SND_EF.1` 0x1bf5, and with the sounds on the game stands still until it has
played (`0x1609e`, `0x1649b`; the port's real-time build waits for the sink, `walk_dump` and `sb_stub.py` log a 'wait'). Then a loop
(`0x160b0`, `0x164ad`): the palette step every 41st round, on the palette itself; Esc, Space or Enter, no release needed, or the right
button alone ends it. Its count starts from `[bp-0x9a]` as it is: 1 for an item (the loop that works out the file offset counts the
item down to 1), the first cell's y for coins (SEEN as the same in the comparison).

**After it** the button is let go and the pass ends through `0x1729e`: the record loop runs again, so a taken chest's picture changes
(scene 1's item 1 at (126, 45): its object, byte 6 = 1, becomes 0xff).

**The shipped data:** 233 kind 2 records, 178 of them with flag 0 bit 0 (set at the start); 9 hold coins (10 to 7,400).

**Checked:** `tools/check_battle.sh`, 19 new `take_` starts, all identical at every pass: an item from each of the three cells and three
cells away (and not from four), facing down, left and right, held 100 passes, taken again (nothing left), with the sound waited for
(`--sb`), closed with Esc, Enter and the right button (both buttons do not), F3 clicked, every place full, a first member with no HP, a
flag not set and set, coins in a dark scene and another, too many coins, and three 2,500-pass walks with F3 now and then
(`tools/walk_keys.py take`). Unit test: the search for kind 2 and `walk_take_slot`. `tools/mutate_battle.py`: 34 new mutations, all
caught; one left out with a note (the hero's frame from the first cell's y instead of the third's draws the same sprite wherever the hero
can stand). ASan/UBSan clean on six starts; a real-time run took the item.

## WALK.SL step 29: F4, casting, 2026-10-24

Built in C and compared pass by pass. READ from the code (`0x154e0` .. `0x15620`; the picker `0x1a8a4` and the magic list `0x21486`
again, for what differs out of a battle), SEEN in the oracle (`work/b8/probe_cast.py`: the list's art source and the two loops' first
counters), then CONFIRMED by the comparison.

**F4** (`0x154e0`): after the F-keys' shared start its button is pressed (at (4, 62); the pointer's corner put back). The member picker
(`0x1a8a4`) asks who casts: faces drawn, a fallen member not taken (its flags 1, 0), its palette steps on the dimmed copy `[bp-0xb0]`.
A member with HP left gets his magic list (`0x21486`); the world buffer's byte 0 is set to 0 first (`0x15585`). After it (or a refusal)
the button is let go, the pointer's place set to 0 and the corner kept, and the pass ends through `0x1729e`.

**The list out of a battle.** `0x21486` reads its buffer's byte 0 into `[bp-0xe]`: 1 in a battle, 0 from F4. With 0:
- its art is decoded from the map buffer + 0xd5b1 instead of the battle's buffer + 0xab6d; SEEN to be the same bytes as `RPG.LCP` 0xe95,
  which the port's battle already decodes;
- a spell on the monsters (no heal, defence or revive) does nothing when key 1 casts it: no MP is spent and the list goes on (`0x22107`);
- a revived member's face is not drawn into the battle's strip (`0x220b3`);
- its palette steps (and those of the picker it opens for a helped member) are on its palette argument, the dimmed copy from F4.
A heal, a defence spell or a revive is cast as in a battle: the helped member's picker, the MP spent, the face's 48-step flash, and back
in the world.

**Stack values, a GUESS:** neither loop sets its palette count. The caster's picker starts from 0x2d and the list from 0xc3 in all three
F4 starts tried (scenes 1, 0 and 13); in a battle the list's was 3. Any value from 0x29 on gives a palette step in the first round.

**In the port:** `battle_world_cast` runs the battle's picker and list with a world flag (`src/battle.c`); `walk.c`'s F4 hands each pass
to it while it runs. `tests/walk_dump.c` dumps its state as a battle's, so the comparison checks the party table in its loops too.
In the real-time build Esc closes the window only in the world loop now: F3's wait and F4's picker and list take it.

**Checked:** `tools/check_battle.sh`, 14 new `cast_` starts, all identical at every pass (the flash steps included): a heal on the second
member with long waits; a spell on the monsters (nothing, the list stays); Esc in the caster's picker; a fallen caster; Esc in the helped
member's picker (back to the list); a defence spell; a revive; a greyed spell; member 1's own list; the mouse; a dark scene; three
2,500-pass walks with F4 (`tools/walk_keys.py cast`, 490 to 694 rounds in each loop). `tools/mutate_battle.py`: 14 new mutations, all
caught (the revived face's one after the revive start was made to reach the revive spell); three left out with notes (the records run
again with no flag changed; each of the two refusals of a fallen caster hides the other). ASan/UBSan clean on four starts; a real-time
run cast a heal.

## WALK.SL step 30: F5, items, 2026-10-24

Built in C and compared pass by pass. READ from the code (`0x15620` .. `0x157f5` with key 0x3f; the item list `0x20412` again for what
differs out of a battle), SEEN in the oracle (`work/b8/probe_items.py`: the list's art source, the first counters), then CONFIRMED by the
comparison.

**F5 and F6 share a handler** (`0x15620`): both buttons, F5's at (4, 82) and F6's at (4, 102), are pressed whichever key it was; the same
member picker as F4's (faces, no fallen one, count 0x2d); then, with the world buffer's byte 0 set to 0, F5's item list (`0x20412`) or,
for F6, the equipment screen (`0x221d2`). Each returns what comes next: the item list 0xff for the equipment screen (F6 or its button
pressed in the list), the equipment screen 0x3f for the item list; the handler goes on while it is 0x3f. At the end both buttons are let
go, the pointer's place is set to 0 and the corner kept, and the pass ends through `0x1729e`.

**The item list out of a battle** (its buffer's byte 0, `[bp-0x10]`, 0):
- its art comes from the map buffer + 0xd5b1, the same bytes as in a battle (`RPG.LCP` 0xe95), and all three icons are shown (48 x 16;
  a battle crops it to the first two, `0x20590`);
- key 3 (scan 4) or the third icon gives the lit item away (`0x212b5` .. `0x21423`): a picker without faces in which a fallen member
  is not taken; to another member below 0xc8 with an empty item place the item goes to his first one, and the list is drawn again;
  to himself, or to nobody, nothing;
- its palette steps are on F5's dimmed copy;
- its count starts from 0x70 (0x98 in a battle; a stack value, INFERRED from the starts).
Key 1 uses an item and key 2 turns the page as in a battle (`port.md` 8e).

**A found leftover:** the picker's palette count (`[bp-0x16]`) is never set at its start: in a battle it was the width of the last
character the list drew. When key 3 opens the picker a second time with nothing drawn between, it starts where the first picker's count
stopped (FOUND: one palette step a pass early in a start that gave twice; the port now leaves it there, `b->last_gw`).

**Not built:** the equipment screen (F6, `0x221d2`); the item list's F6 reports it and F5 ends there. F6 itself is still an unbuilt key.
(Built in step 31.)

**Checked:** `tools/check_battle.sh`, 13 new `items_` starts, all identical at every pass: giving with long waits; eating and wearing;
turning the page; giving to himself, to a fallen member, to one with no room, and Esc in its picker; Esc in the first picker; member
1's list; twelve items over both pages; the mouse (the third icon); a dark scene; three 2,500-pass walks with F5 (`tools/walk_keys.py
items`). The battle's item starts are unchanged, and a new one presses key 3 and the third icon in a battle's list (nothing happens).
`tools/mutate_battle.py`: 19 new mutations, all caught; one left out with a note (the battle's first count in the world: both above
0x28). Four older patterns that these changes had made stale were repointed and re-run (caught). ASan/UBSan clean on three starts.

## WALK.SL step 31: F6, the equipment screen, 2026-10-24

Built in C and compared pass by pass. READ from the code (`0x221d2` .. `0x22c41`, its number routine `0x24302`, and F5's handler
`0x15620`), SEEN in the oracle (`work/b8/probe_equip.py`: the picture, the digits, the first counters and the frames), then CONFIRMED by
the comparison (a new loop top for `tools/battle_check.py`, `0x2256a`, kind 11 "equipment").

**F6 is F5's handler with key 0x40:** the same two buttons and picker, then the equipment screen; it returns 0x3f to go to the item
list (F5 or its button), which returns 0xff to come back (F6 or its button); anything else ends both.

**The screen:**
- a box of colour 0x5c at (64, 92), 96 x 90, with six rows 15 high; a 96 x 90 picture at (160, 93), colour 0 clear: a body with lines
  to six black boxes. It is RPG.LCP **0x168f9** (0x1d98 bytes, the usual run-length picture): the seek's long offset is pushed as
  (0x68f9, 1), so its high word is 1 and not a whence (SEEN: equal byte for byte to what the original decodes);
- for each of the six things worn (party table 0x12c + 6 x his number, slot k at k - 1) its OBJECT.LCC name in colour 0xa0, or 0x27
  when the record's byte 0x16 is not his number, over a shadow of that colour + 5 one pixel down and down right; and its byte 0xe
  (attack), or byte 0xf (defence) when that is 0, in three 7 x 5 digits (RPG.LCP 0x9b8, the world's) in the picture's box for that
  slot, leading zeros left out (`0x24302` with its flag set; without it the same routine draws five);
- the lit row has its colour 0x5c turned 0xd; Up and Down move (no wrap); the pointer over the box lights the row under it;
- Enter, Space or a left press on a row takes the lit thing off into his first empty item place: the row wiped to 0x5c and his attack
  (0x13) and defence (0x12) summed again over the six (byte 0xe, byte 0xf / 5, each halved when not his, as item_use does). With no
  empty place the thing stays, but its number is wiped all the same (FOUND by reading; seen in the comparison);
- Esc or the right button leaves; F5 or a press on F5's button (x 5 .. 49, y 83 .. 100) goes to the item list. Keys count only after a
  release, presses only after the buttons were all up.

**Leftovers from the stack** (the item list and this screen get the same 13 words from the handler, so they share a frame; the picker
gets 10):
- the screen's palette count `[bp-0x1c]` is never set at its start; its setup leaves the last drawn name's colour there. With nothing
  worn it is the item list's `[bp-0x1c]` (its last key's item place, m + page + row) after the list, and 0xeb after the picker (INFERRED,
  every F6 tried);
- the item list's palette count `[bp-0x1e]` is the screen's `[bp-0x1e]`: the last worn thing's number, the pointer's y, or the place of
  what Enter took off, whichever came last (CONFIRMED by starts that go back and forth);
- after taking a thing off, the two "released" flags hold the new attack and defence sums (any non-zero sum counts as released).

**Checked:** `tools/check_battle.sh`, 14 `equip_` starts, all identical at every pass: two things taken off and F5 and F6 between the
screens; Esc in the picker; a 360-pass wait with Down to the last row; the pointer kept at (0, 0); a press held into the screen; F6
from the list's second page; member 1 (his own things and another's); nothing worn; no room; the mouse (rows lit by the pointer, a
press, F5's button, the right button); a dark scene; three 2,500-pass walks with F5 and F6 (`tools/walk_keys.py equip`). The full
`check_battle.sh` (254 starts) and `check_walk.sh` are identical. `tools/mutate_battle.py`: 41 new mutations, all caught; five F5
patterns repointed and re-run (caught). ASan/UBSan clean on three starts.

## WALK.SL step 32: F7, using an item or coins, 2026-10-26

Built in C and compared pass by pass. READ from the code (F7's handler `0x165df` .. `0x1729e`, the item list's mode 2 in `0x20412`,
the record loop's kind 0x10 at `0x11aab` .. `0x11c08`), SEEN in the oracle (`work/b8/probe_use.py`: the stack leftovers, the calls
after a match), then CONFIRMED by the comparison (a new loop top for `tools/battle_check.py`, `0x166a8`, kind 12 "use prompt").

**The prompt:** F7's button (0x9884) pressed, message 0x2e9 (金錢 或 物品, "money or item", colour 0x10, not waiting) and two 33 x 17
frames (`0x27123`) at (103, 149) and (183, 149), colour 0x18; coins lit. Its loop: a palette step (on the palette itself) every 61st
round; the lit frame colour 0xf, the other 0x1a; Left lights coins, Right the item, Enter or Space takes the lit one, Esc none, with no
release needed; the pointer lights the word under it (y 0x95 .. 0xa5, x 0x67 .. 0x87 or 0xb7 .. 0xd6) and a left press there takes it;
the right button is none. None: the end (`0x17239`: the button let go).

**An item:** the same picker as F4 (faces; a fallen member is refused), then his item list with its buffer's byte 0 = 2. That mode
draws two icons and has no key 3 (as in a battle); key 1 on an item ends the list and returns the item, its OBJECT.LCC record (15
words, `0x27503`) copied to the map buffer `[bp-0xba]`; Esc or the right button returns 0; F6 returns 0xff (the map buffer is left as
it was). Then the hero's cell (x & 0xfe, y / 2) and the one beside it (right facing up or down, below facing left or right) are looked
at, both moving a step the way he faces, four times: a kind 6 record whose byte 3 is 0 ends the search, and so does a kind 0x14 record
(a scene change: `0x172f0` with that record, its button left pressed; NOT BUILT). **Correction (2026-10-28, step 36):** kind 0x14 is a
sell counter, not a scene change; built. The last record looked at stays, even one that did
not end it. The world is drawn again (`0x19680`, the party standing) and shown, not darkened. Then:
- the record's byte 4 is the item: its flag (bytes 5, 6) set, the item gone from the member's first place holding it, the item's name
  (12 bytes of the map buffer, colour 0x10) over (112, 93) or not (see the blink), SND_EF.1's 0x1bf5, a shake of 4 (`[bp-0x48]`);
- otherwise, when the item's byte 0x11 is 0 (food and the like): message 0x251 (colour 0x88, not waiting) and the yes / no buttons
  (`0x18c36`, on the palette itself); yes throws the item away. Byte 0x11 not 0: message 0x2b5 (colour 0x88, waiting).

**Coins:** the number box (`0x18fdc`, on the palette itself). 0, or more than the coins (word 0x122): the end. Otherwise the same two
cells for a kind 6 record whose byte 3 is not 0 (no kind 0x14), the world drawn again; when byte 4 is the number's low byte (so 356
pays a price of 100) the flag is set, the number taken from the coins, the coins drawn again, SND_EF.1's 0x1bf5; otherwise message 0x173
(waiting). The shipped prices: scene 21 record 23 (100), scene 23 records 17 and 18 (50), scenes 27, 33, 41 (20 to 200).

**The search's locals:** the first cell's x and y are left in the hero's walk counter and frame (`[bp-0x3c]`, `[bp-0x5e]`), the second
cell's x in `[bp-0xc4]`, the prompt's palette count. After a match `[bp-0x5e]` counts the item down to 1 (`0x16c75`: a file offset
worked out and never read), so the hero's frame is then 1 (CONFIRMED: item 35 in scene 1 showed it).

**The blink** (`0x16ce0`): the name's 96 x 15 place is grabbed, then 1200 rounds count `[bp-0x96]` up (0 after 0x190) and put back the
grab (count below 0xc8) or draw the name over it (from 0xc8). The rounds do not wait (22 clock ticks in the oracle), so only the end is
seen: the name shows when the count ends at 0xc8 or more. `[bp-0x96]` is a scratch word: the world pass writes it at a kill (the coins
+ 1, `0x12249`), a spark (frame x 64, `0x123c3`), the rain (the clock, shifted down 10 below 0x5000, `0x1248d`), the shake
(0x7fff / (4 v48) + 1, `0x1255a`) and the pointer in the window (x - 0x30, `0x126ba`); F7 itself writes the coins into it. The port
follows these; the other F-keys also write it (F1 `0x14f21`, F2, F3, F4, keys 1 and 2) and are not followed: a difference could show
only when F7 comes after one of them with no kill, spark, rain, shake or pointer in the window between (not in any start).

**Kind 0x10 records** (the record loop, run again at the end of F7 as after every F-key): a record whose byte 5 is set and whose flag
(bytes 3, 4) has become set clears byte 5, draws the world again (`0x19680`), grabs its window into the map buffer (`0x2728d`), then 50
times fills the window with colour 0x10 and puts the grab back (`0x270e0`, `0x27328`), with no wait: a flash, of which only the world
is seen at the loop top. Before this step the port reported "a cut scene starts"; it now draws the end. Scene 1's record 15 (item 35)
fires one. What else such a record starts (if anything) is not known: in the starts tried nothing followed.

**Leftovers:** the picker's palette count starts at 0x1e after F7's prompt (0x2d after F4; any value from 0x29 steps at once) and the
item list's at 0x70 as in F5 (SEEN in three starts); the prompt's `[bp-0xc4]` was 0 at every F7 probed, also after a walk and a first F7:
the record loop that ends each F-key writes it (`0x11409`, `0x11b07`), and the port sets it to 0 there (INFERRED). The number box's
count starts at 0x16, as in a talk (the comparison's palette steps agree).

**Checked:** `tools/check_battle.sh`, 29 `use_` starts, all identical at every pass: an item used (facing down, and left one and two
cells away), from the list's second page, on a record that fires a cut scene (at two clock readings), the wrong item, thrown away and
kept, an item that may not be thrown away, Esc in the prompt and in the list, F6 in the list, Space, a 600-pass start with long waits in
each loop, the mouse (the item word and its left end, the right button), the pointer in the window before F7 (the name not shown),
coins paid, the wrong sum, 0, too few coins, 356 for 100, coins on an item's place, a dark scene, and three 2,500-pass walks with F7
(`tools/walk_keys.py use`). `tools/mutate_battle.py`: 62 new mutations, all caught (the first run missed 8: 6 starts added); two
dropped as unshowable (see its note); eight older patterns the change made stale repointed and re-run, all caught. ASan/UBSan clean on
five starts; a real-time run under SDL's dummy drivers used an item and paid coins.

## WALK.SL step 33: F8, the map, 2026-10-26

Built in C and compared pass by pass. READ from the code (F8's handler `0x15ab0` .. `0x15bbd`, the map `0x1c6e4` .. `0x1cbb5`), SEEN
in the oracle (`work/b8/probe_map.py`: the frame's locals, the screen), then CONFIRMED by the comparison (a new loop top for
`tools/battle_check.py`, `0x1ca82`, kind 13 "map").

**F8:** the pointer's corner put back, F8's button (0xb184) pressed; the five party records' 20 item places (0x14 .. 0x27 of each,
all five whoever is in the party) looked at for item 0x13, the map, writing `[bp-0x96]` = the record's place + 0x28 as it goes (0xc8
when none has it). Found: `[bp-0xc4]` = the hero's x & 0xfe and the map; then the button let go and `0x1729e`, as every F-key. No
map: only the button, pressed and let go. The map is a world item: any member carrying it will do.

**The map** (`0x1c6e4`, its arguments the hero's x & 0xfe and y / 2, from `[bp-0x8e]` and `[bp-0xb6]`, which follow `tab_b[0]`, `[1]`):
- the screen's rows 49 .. 150 kept in the world buffer at 0x32c8 (`0x2723c`);
- one pixel per map cell, byte 0x20 of its tile (row 4, column 0; the tile number shifted in 16 bits, as the view does), 128 x 100 in
  the world buffer from byte 4; then (`0x2741b`, in this order) 0xbf turned 0x2f, 1 to 0x57, 2 to 0x59, 3 to 0x5a; then the hero's
  cell set to 0xbf;
- a line of 0xb7 at (97, 49), 128 wide (`0x270e0`);
- each of the 100 rows: a header written over the 4 pixels before it (`0x80, 1`), stretched into the map buffer `[bp-0xba]` to the
  row's width (`0x26de8`: step 0xffff * 128 / w, from 1; so a 128-wide row draws its second cell twice and not its last), 0xb0 at
  each edge; in the hero's row the first 0xbf from byte 5 gives his place; then shaded (`0x25dca`, the edges too) 7 times for rows
  0 .. 11, then one time less every 13 rows (none from row 90), and put at (96, 50) on, 130 wide, every second row a pixel further left
  and two wider (228 + 2 at the last row, which starts at (46, 149));
- a line of 0xb0 under the last row, as wide;
- the mark: the 4 x 4 from a pixel up and left of the hero kept in the map buffer (and a copy at 0x14), its rows 1 and 2 coloured
  0x10, the two middle pixels of rows 0 and 3 0x12.

**Its loop** (`0x1ca82`): the count `[bp-0xe]` (starting at the last row's width, so 0 in the first round) goes 0 .. 0x50: the mark is
drawn for 45 rounds and the kept 4 x 4 for 36; a palette step (`0x1c284`, on the palette itself) every round; Esc, Space or Enter (the
key byte as read, no release needed) or the right button alone (`0x260c9` mode 0: buttons = 2) ends it, the screen's rows put back. The
song's end is also handled there, as in the main loop (step 9); the port restarts songs in real time.

**A stack byte:** whether the first row already widens is `[bp-0xc]`, which the frame never sets before the drawing; it was 0x2cc6 (the
low byte 0xc6, not 0: it widens) at every F8 probed, at the first pass, after a walk and after F1 (INFERRED: it looks like a segment
number left by an earlier call). The mark's place `[bp-4]` is set only when the hero's row is drawn; every place the hero can stand
is on the map's 100 rows.

**Map pixels:** no scene's map has a pixel 0xbf (`work/b8/mappix.py`), so the first recolouring changes nothing in the shipped data
and the hero's 0xbf is the only one; 1, 2 and 3 appear in 18 scenes.

**Checked:** `tools/check_battle.sh`, 20 `map_` starts, all identical at every pass: scenes 0, 1, 2 (the right edge), 4 and 7 (the
bottom rows), 6, 13 (dark), 23 and row 5 of scene 1; a 400-pass start (the blink's cycles), Space, Enter and a second F8, the mouse
(F8's button clicked, the left button and both do nothing, the right one closes), the map in the last item place and in the second
member's, no map, F7 after F8 with and without the map (`[bp-0x96]`), and three 2,500-pass walks (`tools/walk_keys.py map`).
`tools/mutate_battle.py`: 49 new mutations, all caught; two dropped as unshowable (see its note); three F3 patterns that also matched
the map's close test repointed and re-run, all caught. `tests/test_walk.c` `test_map`: the item search and the row's stretch.

## WALK.SL step 34: F9, the party's order, 2026-10-27

Built in C and compared pass by pass. READ from the code (F9's handler `0x1546a` .. `0x154e0`, the screen `0x1b748` .. `0x1bd6d`),
SEEN in the oracle (`work/b8/probe_f9.py`: the frame's locals at the loop's first rounds, the screen), then CONFIRMED by the comparison
(a new loop top for `tools/battle_check.py`, `0x1b834`, kind 14 "order", which also compares the party table, its first argument).

**F9 (位置, position):** the pointer's corner put back, F9's button (0x498d) pressed, `0x1b748` (the party table, the party's sprites
`[bp-0x38]`, the world buffer, the dimmed palette copy `[bp-0xb0]`), the button let go, then `0x1729e` as every F-key. The screen puts
nothing back: the world drawn after it covers it.

**The screen** (`0x1b748`): RPG.LCP 0x7b5a, 0x271 bytes, run-length decoded (`0x25eeb`) into the world buffer: a 20 x 100 picture
(header 0x14, 0x64), five 20 x 20 rows. The members are the party records before the first whose number (byte 3) is above 4, at most
five; the picture's height becomes 20 for each, the last row's offset `[bp-0x10]` = 0x28 (n - 1). The lit row `[bp-0x1c]` (0x28 a
member, its picture offset `[bp-0x1e]` = 0xbbc + 400 a row) starts at 0, nothing picked up (`[bp-0x22]`), the walk frame
`[bp-0x20]` 1.

**Its loop** (`0x1b834`), each round: the song's end (as the main loop's); the count `[bp-0x18]` 0 .. 0x3c, past it the next frame
(0 .. 3, frame 3 drawn as 1); the palette step (`0x1c284` on the dimmed copy) at counts 0, 15, 30 and 45; the clean picture copied to
the world buffer + 0xbb8 (`0x27503`); the blink count `[bp-0xe]` 0 .. 0x50 and, above 0x28 (40 rounds of 81), the lit row filled
(`0x273f8`, 400 bytes) with 0x25, or 0x10 when picked up; each member's walking-down sprite (`bank2` + number x 0xc00 + 0x300 + frame x
0x100, colour 0 transparent, `0x26c04`) at (4, 1) in his row; shown at (150, 50) (`0x27328`). Then the key read raw (`0x274b8`) and the
BIOS buffer emptied; with a mouse (`0x260c9` function 3): the pointer's y below its last moves the row up, above it down (not past the
first or last row), carrying the member when picked up; no button sets a "let go" flag, the left button after it picks up or puts
down, the right button alone ends it. A key counts after a break code (the latch `[bp-0x24]`): Esc ends, Space picks up or puts down,
Up and Down move as the pointer does. Moving a picked-up member swaps bytes 3 .. 0x27 of his record and the next (`0x25c8e`, 0x25
bytes): every record keeps its place and facing (bytes 0 .. 2), so the member moved to the top walks where the hero stood. Enter does
nothing.

**Stack leftovers (INFERRED):** the frame's count, the blink count and the latch are never set before the loop: 0xc6 (the same 0x2cc6
F8's `[bp-0xc]` holds), 0x45 and 0x32 at every F9 probed (at the first pass, after a walk, after F8, after F1). So the first round
starts the next frame and steps the palette, the row is lit at once, and a key's make code counts without a break first. The pointer's
last y `[bp-0xa]` is the slot where F9's handler pushed 0x498d for its button: the pointer's first reading always moves the row up
(nothing at the top).

**Checked:** `tools/check_battle.sh`, 12 `order_` starts, all identical at every pass: a party of three with long waits, both ends
picked up and pushed past, Esc while picked up and F9 again, a held key and Enter, Down with no break code after F9, all five members,
the hero alone, member 2 without member 1 (the list stops), a dark scene, the mouse (F9's button, the pointer's y, the left button with
and without a release, both buttons, the right one), and three 2,500-pass walks (`tools/walk_keys.py order`). `tests/test_walk.c`
`test_party_order`: the members counted and two records swapped. `tools/mutate_battle.py`: 49 new mutations, all caught (the latch's
leftover only by `order_nobreak`, Down in the loop's first round); one dropped: the records run again after F9 change nothing. ASan and
UBSan clean on five starts; a real-time run under SDL's dummy drivers opened F9, moved a member and closed it (F10 pressed after it
reached the world).

## WALK.SL step 35: F10, the party's data, 2026-10-27

Built in C and compared pass by pass. READ from the code (F10's handler `0x153d8` .. `0x1545c`, the screen `0x1a122` .. `0x1a7c6`, its
number routine `0x1a7c8`), SEEN in the oracle (`work/b9/probe_f10.py`: the frame's locals at the loop's first rounds, the screen),
then CONFIRMED by the comparison (a new loop top for `tools/battle_check.py`, `0x1a641`, kind 15 "data").

**F10 (資料, data):** the pointer's corner put back, F10's button (0x628d) pressed, `0x1a122` (the party table, the world buffer, the
map buffer `[bp-0xba]`, `[bp-0x20]`, the dimmed palette copy `[bp-0xb0]`, the 8-pixel font `[bp-0x40]`), the button let go, then
`0x1729e`.

**The screen** (`0x1a122`):
- MANDATA1.LCC's first 0x23e6 bytes read into the map buffer and run-length decoded (`0x25eeb`) into the world buffer: a 64-wide panel
  at 0 (its header), three pictures of 0x3c4 bytes with headers at 0x274d, 0x2b11 and 0x2ed5, and ten 4 x 7 digits (28 bytes each)
  at 0x3299.
- Kept (`0x2723c`): the screen's 64 x 160 at (0, 26) in the map buffer, its 64 x 149 at (256, 26) at the map buffer + 0x280a, its
  320 x 15 at (0, 11) in `[bp-0x20]`; MES_TXT.LCC 0x6a06, 50 bytes, read to `[bp-0x20]` + 0x1388: the names, 6 bytes a member
  number.
- The members are the records before the first whose number (byte 3) is above 4, at most five (as F9's). For each, from x 0 every 64:
  the panel at y 26 (all panels first); then, member by member: his name (`0x243ec`, 6 bytes, colour 0x98, into the world buffer)
  over a 52 x 8 box of 0x56 at (6, 17), its shadow of 0x9c a row down a pixel left and right (`0x25b54`), the name over it at (8, 12)
  (`0x27371`); his face, MAN_PIC.LCC at 0xce8 x his number (0xce8 bytes, not packed), shaded 8 times (`0x25dca`) when his HP (word 8)
  is 0, at (2, 28); his numbers (`0x1a7c8`) from (17, 90): words 4 and 6 (experience, next level; the second 21 right and 6 down),
  13 rows down words 8 and 0xa (HP, most HP), 13 more byte 0xc (right) and word 0xd (left), 19 more word 0x10, then bytes 0x12 and
  0x13 13 rows apart.
- **Numbers** (`0x1a7c8`): from the ten thousands, each digit drawn 4 to the right of the last (colour 0 clear, `0x25bda`), leading
  zeros left out, zeros after a digit drawn, the units always.

**Its loop** (`0x1a641`): the song's end; the count `[bp-0x14]` to 0x28, past it the palette step (`0x1c284` on the dimmed copy);
the count `[bp-0x16]` to 0xc8, past it the next of the three pictures (0x274d, 0x2b11, 0x2ed5, then 0x274d) at each member's (5, 90).
The picture shown first is the second: before the first turn none is drawn. Esc (the key byte as read) or the right button alone
(`0x260c9`, which writes the pointer's x and then y into the world buffer's first word) ends it: the three kept parts put back.
Nothing else is read.

**Stack leftovers:** `[bp-0x16]` is the last word or byte read: the fifth member's byte 0x13, or the number of the first record not
listed (0xc8 + k for a member who has not joined: the first round turns the picture). `[bp-0x14]` is never set: it was 0x28 x the
members in every start probed (one, two, three and five members, after a walk and after F8; INFERRED), so the first round steps the
palette.

**Not reproduced:** F10's writes into the world buffer's start (the name picture, the faces, the pointer) only change what the world's
next drawing replaces. The map buffer's start also gets them (the packed file, the kept columns), as the port writes them; only the
unreachable rows above a map read it (step 27).

**Checked:** `tools/check_battle.sh`, 12 `data_` starts, all identical at every pass: a party of three for 850 rounds (four picture
turns, the third back to the first) with Space, Enter and Down (nothing), five members with the fifth's byte 0x13 0xc5 and 0, one
member, member 2 without member 1, a fallen face and numbers with zeros inside, five digits and 0, a dark scene, the mouse (F10's
button, the left button and both do nothing, the right one ends it; F10 again by key), F10 after F9 has moved the hero to the
second place (names and faces go by the number, byte 3), and three 2,500-pass walks (`tools/walk_keys.py data`). `tests/test_walk.c`
`test_data_number`. `tools/mutate_battle.py`: 49 new mutations, all caught (the two that take the name and face by place only by
`data_order`); one dropped: the records run again after F10 change nothing. ASan and UBSan clean on five starts; a real-time run under
SDL's dummy drivers opened and closed F10 and F9 (key 4 then reached the world).

## WALK.SL step 36: F7 at a sell counter (kind 0x14), 2026-10-28

Built in C and compared pass by pass. READ from the code (the kind 0x14 case of the record block, `0x1863c` .. `0x18b7d`, reached from
F7's item path at `0x16b9c`), SEEN in the oracle (`work/b8/probe_use.py` on a sale: the yes / no loop's frame), then CONFIRMED by the
comparison.

**Correction:** steps 7 and 32 and `docs/README.md` called `0x172f0` .. `0x18b82` the scene-change block and kind 0x14 a scene change.
`0x172f0` is the dispatch on a scene record's kind (byte 2, `[bp-0xbe]`); kind 0x14 is a **sell counter**: F7 sells the chosen item there
for half its price. The shipped counters: scene 0 record 17 at (106, 11), scene 2 record 31 at (88, 75), scene 21 record 47 at (108, 36),
scene 33 record 43 at (134, 59), scene 41 record 38 at (114, 36). Their bytes 3 to 14 are 0; the price comes from the item. The other kinds
the block names (1, 4, 5, 7, 8, 0xe, 0x11, 0x13) were not read in this step.

**The sale** (F7's item path found a kind 0x14 record and drew the world again):
- the item's name (the map buffer `[bp-0xba]`, the item's OBJECT.LCC record, 12 bytes; `0x243ec`, colour 0x10, into the world buffer);
- the screen's 192 x 60 at (64, 50) kept in F7's buffer (`0x2723c`), shaded four times through the shading table (`0x25dca`) and put back
  (`0x27328`); the name's shadow of colour 0x16 at (116, 53) and (117, 53) (`0x25b54`), the name at (116, 52) (`0x27371`); a frame of
  colour 0x2a round the 192 x 60 (`0x27123`);
- **an item whose byte 0x11 is 0** (the one F7 lets you throw away): the map buffer's first bytes become `原價:` (5 bytes), the price
  (word 0x12, `0x25fe0`) as five digits (`0x26450`, which ends them with a 0), a space over that 0, `半價:`, half the price (shifted
  right, so rounded down) as five digits. Each number's leading zeros up to its fourth digit become spaces (a byte at most '0' is a
  zero). 21 bytes are drawn in colour 0x80 at (67, 70) over a shadow of 0x84 at (67, 71) and (68, 71); then the map buffer's first 7
  bytes become `願意嗎?`, drawn in colour 0x98 at (172, 87) over 0x9b at (172, 88) and (173, 88). The pointer's corner is put back and
  the yes / no buttons (`0x18c36`) ask. **Yes:** the half price is added to the coins (word 0x122) one at a time in a 16-bit word, so
  there is no cap and the sum wraps past 65,535; the coins are drawn (`0x1bd6e`); the item goes from the member's first item place
  holding it; SND_EF.1's 0x1bf5, waited for and cut when the sounds are on. **No:** nothing.
- **otherwise** (keys and the like): message 0xd4 (colour 0x20, waiting).
- Then the pointer's corner put back, F7's button let go, the corner kept again, `[bp-0xbe]` = 1, and the record loop runs again
  (`0x10bf9`).

**Stack leftovers:** the yes / no loop's palette count (`[bp-8]` at `0x18d23`) is 0x3fdc here, left by the text drawing (`0x243ec`'s
offset): SEEN, and in a sale no message box comes first to set it. So the count starts at 0xdc and the first round steps the palette.
`[bp-0x96]` ends where the second number's spaces stop (0x10 .. 0x14).

**Not built:** the hero stepping onto a kind 0x14 record (the step-on path of step 7 reaches the same code with whatever the map buffer
holds); whether the hero can stand on any counter's cell was not checked.

**Checked:** `tools/check_battle.sh`, 23 `pawn_` starts, all identical at every pass: selling food (price 20) with yes, no, no then yes,
and Space; an item that may not be sold (message 0xd4) by key and by mouse; a four-digit price (6300), an odd price (15), the coins
wrapping past 65,535; facing left, a counter three cells away; with a Sound Blaster; the mouse (yes, no, and the pointer resting inside
the box); a second sale after the first; member 1's item; the counters of scenes 2, 21, 33 and 41; three 2,500-pass walks. The names
`sell_` were already the villagers' sales. `tests/test_walk.c` `test_use` pins the text (`walk_sell_text`). `tools/mutate_battle.py`:
40 new mutations, all caught (two only after `pawn_odd` and `pawn_mouse_in` were added); three dropped with notes (the records run
again, `[bp-0x96]`, which no start can read after a sale, and the pointer's corner put back before the yes / no, which is the corner
just kept). Three older patterns the new code duplicated were repointed and re-run on their own starts.

## WALK.SL step 37: the world's keys 1 to 4 and A, 2026-10-28

Built in C and compared pass by pass. READ from the code (`0x143f7` .. `0x14d46` for the five keys, the four walking handlers
`0x13aed` .. `0x1423d` for sliding, `0x25e14` for the saves' packing, `0x18b82` .. `0x18c34` for the end of `WALK.SL`), SEEN in the
oracle (`work/b10/probe_keys.py`: the screens, the number box's frame, the files written), then CONFIRMED by the comparison. The right
column's four lower buttons are 音樂 (1, music), 音效 (2, sounds), 儲存 (3, save) and 離開 (4, leave); A is the 大 mark at the bottom left.

**Key 1** (`0x144d4`): with an FM card (`0x2cc5:5` not 0), the music flag goes 0x81 -> 0x80 (the song stopped, `0x165d:0x126`) or back
(the scene's song from its start, `0x165d:0x3c`) and the button at (269, 98) is turned (`0x1c3ba`, the pointer's corner put back
before and kept after). No card: nothing at all, not even the button. **Key 2** (`0x14597`): the same for the Sound Blaster's flag
(`0x2cc5:0xc`), the button at (269, 118); a sound already playing is not cut. Both, and A, count as released while they repeat (step 7).

**Key A** (`0x143f7`): `[bp-0x76]` (0 at the start, `0x10ba3`) is turned over and the 41 x 12 at (7, 182) is grabbed into the world
buffer's start, its colour 0x1f turned 9 (on) or 9 turned 0x1f (off) over the grab's 0x1ec bytes, and put back. **Sliding** (each
walking handler, when its step is blocked and `[bp-0x76]` is set): the coordinate is taken back, `[bp-0x2e]` is 5, and three places are
tested with `0x2c318` (both words at map row y / 2 and column x with its low bit cleared on the list of walkable tiles; the foot test
looks a row lower). Up tests x - 2, x - 1 (left) and x + 2 (right) on row y; down the same on y + 4; left tests x - 2 at y, y + 2 (up)
and y + 4 (down); right the same at x + 2. The last one open wins and becomes the key the later handlers see, so the sideways step is
taken in the same pass; none open: the bump (`[bp-0x48]` = 2). After the four handlers, up and down run once more without sliding (a
sideways slide's key). Up on row y + 1 and left from x - 1 would give the same cells everywhere a hero stands blocked in the 53 scenes
(`work/b10/slidesearch.c`).

**Key 3, saving** (`0x14647` .. `0x14b4f`): the button at (269, 138); message 0x253 (請輸入欲存編號(0-10)?, colour 0x20, not waiting:
the window's next copy covers it); the pointer off; the number box of the talks (`0x18fdc`, on the palette itself). Its palette count
starts at 0xe8, a stack leftover (SEEN; any value from 0x16 up acts the same). Then the pointer's place is 0 and its corner kept, and a
number below 11 saves into slot NN:
- the party table's bytes 510 .. 513 are copied to 500 .. 503 (502, 503 again from 512, 513) and its first 504 bytes are packed into
  `saveNN.000` (the name is "sav" + the slot as three digits, then byte 3 = 'e': `save05`; then `.000`);
- each villager's byte 8 is put back from its copy after the table (the start lowers it on a slow machine), and the scene's 1750 bytes
  are written into `__1.NOW` at 1750 x the scene (party byte 510);
- `__1.NOW` is read back: its first 0xfcee bytes go into a scratch buffer and are packed into `saveNN.001`, the rest (at most 0x9470;
  28,000 in the shipped file) into the map buffer `[bp-0xba]` and `saveNN.002`. `[bp-0x96]` is left at the last packed length;
- with a speed class below 2 the villagers' byte 8 is lowered again (by 2 - class, at least 1) and byte 9 zeroed, as at the start.
Then the button is let go and the pass ends with the walk counter (`0x172a4`), not through the record block. 11 or more: only the button.

**The packing** (`0x25e14`): the counts of the 256 byte values (16-bit words, in the output buffer); the marker is the value used least,
the lowest of a tie. Byte 0 the marker, then a word with the packed length (header included), then the bytes: a run of three or more as
(marker, count, byte), at most 255 a run; the marker itself always as such a triple, counted one short (the unpacker `0x25eeb` writes
`count` copies, so a save drops one marker byte per run: the original's own loss). `src/walk.c` `walk_rle_pack`.

**Key 4, leaving** (`0x14bb4` .. `0x14d46`): the button at (269, 158); message 0x252 (colour 0x20, not waiting); the pointer off; the
yes / no buttons of the talks (on the palette itself). Then the pointer's place is 0 and its corner kept. **No:** the button let go, the
walk counter. **Yes:** `[bp-0xbe]` = 0 and `0x172f0` with no record, so `0x18b82` falls through to the end of `WALK.SL`: the Sound
Blaster's driver ended (`0x290e0 + 0x76`), the music stopped, the buffers freed, and `execl("mark.exe")` (`0x18c29`, string `ds:0x126`):
MARK.EXE's title. The oracle's C runtime returns from that call without reaching DOS (the run then exits with 255), so
`tools/battle_check.py` records the call itself.

**Found along the way:** the DAC's entries 0 .. 15 are the palette buffer (RPG.LCP's first 48 bytes) at the loop's first pass, and
RPG.LCP's byte 0 is also the speed class TV.EXE writes: colour 0's red is the class (SEEN with classes 0, 1 and 2). The port did not
load them (the shipped class, 2, matched the start's screen). `walk_palette_load`.

**Checked:** `tools/check_battle.sh`, 27 `sys_` starts, all identical at every pass, and `tools/battle_check.py` now also compares the
files the run wrote (names and bytes; the port writes into a scratch directory, `w:DIR`) and that key 4's yes ends both at the same
pass. The starts: key 1 twice with an FM card, held, and without a card; key 2 before and inside the house door (only the way out
sounds); A held and twice; sliding on two 1,500-pass random walks in scene 0 (33 slides) and four starts placed where only one test
tells the rules apart; saves into slots 5, 0 (the shipped `SAVE00.002` written again the same) and 15 (nothing written), with speed
classes 0 and 1, in a dark scene; leaving with yes and with no; the five buttons by mouse; the pointer resting where the messages go;
three 2,500-pass walks with all of them. `tests/test_walk.c` `test_save_pack` pins the packing and the sliding test.
`tools/mutate_battle.py`: 55 new mutations, all caught (four only after starts were added); two dropped with notes (above); three older
patterns the new code touched were repointed and re-run on their own starts. ASan / UBSan clean on five starts; a real-time run under
SDL's dummy drivers saved into slot 2 and left.

**Not built:** loading a save (the menu's slot picker); the scene record block's other kinds.

## WALK0.SL: loading a save, 2026-10-30

READ from the code (`0x115c2` .. `0x119de`, the unpacker `0x11c97`), then CONFIRMED in the oracle (`tools/check_load.sh`). WALK0.SL's data
segment starts at 0x16db0, so the file names are `ds:0xb7` `man_info.now`, `ds:0xc4` `__1.now`, `ds:0xcc` and `ds:0xdd` `walk0.sl`,
`ds:0xd5` `ani.lcp`, `ds:0xe6` `walk.sl`, `ds:0xee` `cast.sl`.

**After the pick** (`0x115c2`): if `es:[4]` of segment `[0x298]` is set, `0x22d:0x124` and `0x22d:0x14c` are called (GUESS: the music
stopped; not read); the marker's place `[bp-0x2c]` gives the slot, `(place - 0x129c) / 11`; the name is built as on saving ("sav", the slot in three digits, byte 3 = 'e', byte 6 = '.', then the
extension).
- `saveNN.000`: at most 0x1f8 (504) bytes read, unpacked, and 504 bytes of the result written into a new `MAN_INFO.NOW` (the
  unpacker's length is not used).
- `saveNN.001` and `.002`: at most 0xffdc bytes each read into two buffers, each unpacked, and a new `__1.NOW` gets the first
  result, then the second, each as long as its unpacking gave (92,750 bytes with every save tried).

**The unpacker** (`0x11c97`, the same as WALK.SL's `0x25eeb`): byte 0 the marker, the word at 1 the packed length counted from byte 0;
until that end, a byte other than the marker is copied, and (marker, count, byte) writes `count` copies (`rep stosb`, so 0 writes
none). It returns the length written. It reads past what the file held if the header says more.

**Then, on every path out of the menu** (`0x11810` ..): `walk0.sl` is read (at most 0xffdc bytes) and compared with as many bytes of
`ani.lcp` from 0x600. If any differs, the copy from `ani.lcp` is written over `walk0.sl` and the menu starts again (`0x119e8`);
otherwise `[bp-0x2c]` picks the program: below 0xe51 (0 after a load) `execl("walk.sl")`, 0x1c9c `cast.sl`. This looks like
a self-repair of the menu program. In the oracle the two match and `walk.sl` is opened. The port has no WALK0.SL to check, so it
does not reproduce this.

**Checked:** `tools/check_load.sh`, 12 of 12 identical:
- every shipped save (slots 0, 1, 3, 4, 7, 9 and 10) and two saves the port's key 3 wrote (slot 5 from scene 1, slot 0 from the
  dark scene 13): the `MAN_INFO.NOW` and `__1.NOW` the original writes in the oracle are byte for byte the port's. The oracle's data
  copy has neither file beforehand, because slot 0 gives exactly the shipped `__1.NOW`;
- 150 world passes from what slots 0, 3 and 7 load (scenes 27, 2 and 13).

`tests/test_walk.c` `test_save_pack` pins the unpacker; `tools/mutate_load.py`: 9 mutations, all caught, one dropped (reading all of
`.000` only differs for a `.000` over 504 bytes, which a party table packs to only if it uses nearly every byte value).

## WALK.SL step 38: doors to other scenes (kind 1 records), 2026-10-30

READ from the code (`0x1767b` .. `0x17b0b`, the zoom out `0x199b4`, the start's jump into the loader `0x10bad` .. `0x10bf9`), then
CONFIRMED by the comparison. A kind 1 record is a door: byte 3 the scene it leads to, byte 4 (0 in every record) goes to party byte
511, bytes 5 and 6 the arrival's x and y / 2. 227 of the 1,262 non-empty records in the shipped `__1.NOW` are doors; 13 of them have byte 0xe
set (below).

**The start is a door too.** Before its loop the start sets `[bp-0xbe]` = 1 and `[bp-0x74]` = 1 and jumps to `0x18b82`; with
`[bp-0xbe]` not 0 that goes to `0x10bf9`, and with `[bp-0x74]` set `0x10bf9` runs the loader (the scene in party byte 510 read from
`__1.NOW`, the map, the tiles, the banks, the song from its start at `0x1132e` when the music is on, the party at bytes 512 and 513,
the records) and the zoom in (`0x19ab2`). Without `[bp-0x74]` it only runs the record loop again (`0x113c4`, step 7). So what the
start does before `0x10bf9` is not done again after a door: bytes 500 .. 503 copied to 510 .. 513 (`0x10b17`), the sound and mouse
set-up, sliding off (`0x10ba3`), `[bp-0x2e]` = 3, the rain's random lists.

**A door, byte 0xe 0** (`0x1768a` ..):
- the first 100 words of `[bp-0x8c]` set to 10000, 10050, .. 14950. `[bp-0x8c]` is the world buffer here (the F-keys' drawing
  writes its header there, step 26), so the zoom out's grab covers them at once; the DAC does not change (SEEN: the first door
  compared, the port's first try also put them into the palette and the comparison caught it);
- each villager's byte 8 put back from its copy after the table (as on saving, step 37);
- the sound at `[bp-0x4c]` + 0x4086 (`SND_EF.1`; `[bp-0x96]` = 0x4086 at `0x17709`);
- the zoom out (`0x199b4`): the window, 192 x 168 at (64, 16), grabbed with its header into `[bp-0x8c]`; with the Sound Blaster's flag
  (`[0x388]:0xc`) set, `[0x24e]` 0 and `[0x38a]:6` set, two calls into the Sound Blaster driver (`0x190e:0x6d`, `0x190e:0x49(0)`; not
  read further); then 13 steps, each after a retrace
  wait: the grab's middle w x h (192 x 168 shrinking by 14 x 12, from 0x487 k in) scaled to the window into the map buffer
  `[bp-0xba]` and copied to the screen. These are the start's zoom in backwards;
- the scene's 1750 bytes written into `__1.NOW` at 1750 x party byte 510 (the old scene), as on saving;
- the record's bytes 3 .. 6 copied (two words, `0x27503`) to party bytes 510 .. 513, and `[bp-0x74]` = 1.
Then the other kind tests fail, `0x18b82` goes to `0x10bf9`, and the loader and the zoom in run for the new scene. On a slow machine
the loader lowers the villagers' byte 8 again (`0x11358`).

**A door with byte 0xe set** (13 records: scenes 1 <-> 9, 3 <-> 8, 7 <-> 10, 23 <-> 28; built 2026-10-30, CONFIRMED by the
comparison): before the sound, RPG.LCP 0x1236 (0x14e bytes, read into the world buffer) is unpacked (the saves' unpacker, `0x25eeb`)
into `[bp-0x20]`; its two 256-byte halves go to rows 1 and 4, and 7 and 10, of the map buffer `[bp-0xba]` (256 bytes a row), whose rows
0 .. 11 are then copied to 12, 24, 36 and 48 (two-word copies, `0x27503`). Then up to 60 steps (`0x178b5` .. `0x17a28`):
- a round counter `[bp-0x10]` (0 when the door is reached, in every start probed: `work/b11/probe_pic.py`) counts up; past `[bp-0x78]`
  (3, then 0 once 20 steps are done) it goes back to 0, the party turns (`0x2489a`: each record's direction 0 -> 3 -> 1 -> 2 -> 0,
  others kept) and a step is counted;
- the world drawn as the F-keys draw it (`0x19680`), after step 20 with the map buffer in place of the party's sprite bank
  (`[bp-0x38]`): the party is drawn as a blue ball, which is what the 512 bytes are; the window copied to the screen (`0x2b9ec`);
- the keyboard port read (`0x1732:0x198`): a make code ends it. Walking onto the door with an arrow held, it ends after one round;
  walking with the mouse, it runs all 120 rounds.
The sound is then 0x5571 (`[bp-0x96]` set at `0x17a2b`), and the rest is a door's.

**Kind 4** (`0x17c41` .. `0x17d5a`, built with them: walking into scene 28 passes one): the party record whose number (byte 3) is
the record's byte 3 is looked for; if it is there and its HP (word 8) is not 0, its byte 0xc is raised to the record's byte 4
(never lowered) and the flag (byte 5, bit byte 6) is set (`0x2c158` mode 1). Then the record loop runs again, as for every kind.
Scene 1's kind 4 at (102, 85), on the island the boat from scene 9 lands on, sets the flag that its kind 0xa records turn into a
bridge north; the boat door south of it needs more (not reached by any start).

**Correction to the record loop (step 7, steps 32 and 33):** a kind 0x10 record that fires draws the world again and grabs it
(`0x11b2b`) where it stands in the loop, before the records after it are applied. The port drew it after the whole loop. In scene 1
the kind 0x10 record (20) comes before the kind 0xa records (21 .. 23) that add the bridge, so the screen shows no bridge until the next
pass (SEEN: `door_kind4`). `walk_scene_apply_records_cb` now calls back at that point.

**Checked:** `tools/check_battle.sh`, 25 `door_` starts, all identical at every pass, with the zoom out and the zoom in after a door
compared step by step (`tools/battle_check.py` now records the screen after each step of `0x199b4` and `0x19ab2` once the loop has
started) and `__1.NOW` compared byte for byte: scene 0's south door to scene 1, doors from scenes 1 (two), 6, 13 (dark), 9 and 42
(their shake's floor, map header byte 3, is 1; a battle follows the one from 42) and 43; a shot still flying when the door is taken;
scene 0 to 1 and back again (the first door's state read again), with music, the Sound Blaster, speed classes 0 and 1, and the
mouse; three 1,500-pass random walks from beside the south door (one goes through two doors). Seven more for the doors with a picture and
kind 4: scene 23 to 28 by key, with the Sound Blaster, by mouse, and by mouse with a party of five (all 120 rounds compared, `tools/battle_check.py` also records the screen after each
round, `0x17a00`), scene 28 to 23 both ways through a kind 4 record, and scene 1's kind 4 record. `tools/mutate_battle.py`: 36 door,
picture, kind 4 and loop mutations, all caught (two only after the last two starts were added); two dropped with notes (a door
with a picture taken like the others: no start reached one before they were built; turning ten records: 0 or 60 turns leave them
as they were). The two cut-scene patterns the loop change moved were repointed and re-run, both caught. The loader does not set the DAC (its only `0x1732` call is the window's
copy), so the port's door does not either.

## WALK.SL step 39: the scene-record kinds still unbuilt (read only), 2026-10-31

READ from the code (`work/b11/rec.asm`, `0x172f0` .. `0x18b82`), not run or compared. Far calls are named by their linear address
(segment x 16 + offset + 0x10000); WALK.SL's data segment starts at 0x2cc60, so `ds:0x11c` is `ending.sl`. Counts are of the shipped
`__1.NOW` (`work/b11/kinds.py`). The flag tests are `0x2c158` mode 0 on (byte, bit); "the pointer off / on" is `0x1732:8` /
`0x1723:0xc` around the action when a mouse driver is there (`[0x382]:4`).

- **Kind 0, a warp (26: scenes 23 and 50):** if flag (byte 3, bit byte 4) is set: up to 60 steps of the party turning (`0x2489a`,
  one every second round), the world drawn (`0x19680`) and shown, a key down ends it; then the view slides one cell a round towards
  (byte 5, byte 6), the world drawn with the buffer's byte 0 = 0, until it is there; then all five party records and the 13 trail
  entries are put at that place. Byte 6 is a full y here. Built and compared in step 44 (the turning is one round in two).
- **Kind 5, a shop (29):** the pointer off; the record's bytes 3 .. 8 copied to `[bp-0x80]`; `0x22c42` (it reads `SHOP_PIC.LCC`,
  `MES_TXT.LCC` and `OBJECT.LCC`: the bytes are GUESSED to be the six things for sale); the pointer on. The record finder returns
  0x320 (no record) for a kind 5 under odd coordinates, so the hero must stand exactly on it.
- **Kind 7, a number lock (8: scenes 4, 21, 30, 43):** if flag (byte 5, bit byte 6) is not set: message 9 (colour 0x68, not waiting),
  the number box (`0x18fdc`); a number equal to the word at byte 3 sets the flag and plays SND_EF 0x1bf5; another gives message 10
  (waiting). Scene 43 has five of them.
- **Kind 8, a boat ride (8: scenes 1, 3, 7):** the pointer off; `0x1be44` (it reads `BOAT.LCZ`); then as a door: the villagers' byte
  8 put back, the scene's state into `__1.NOW` (`ds:0x114`), party bytes 510 .. 513 = the record's bytes 3 .. 6, `[bp-0x74]` set.
  Built and compared in step 42.
- **Kind 0xe (1: scene 22):** if the party table's word at 0x122 is not 0: `0x24962` (it reads `RPG.LCP`, `MES_TXT.LCC` and
  `SHOP_PIC.LCC`; not read further), then the party and trail put at (byte 3, 2 x byte 4) and the roof flag `[bp-0x9e]` cleared.
  Step 45: `0x24962` is a card game, built and compared.
- **Kind 0x11, a scripted talk (2: scenes 22, 42), with kind 0x12 after it (1):** if flag (byte 3, bit byte 4) is set and byte 5 is not
  0: flag (byte 0xd, bit byte 0xe) set, byte 5 = 0, `0x1732:0xd8(map buffer, 0x14, 0)` (not read); bytes 6 .. 11, and when the next
  record is kind 0x12 its bytes 3 .. 14, are up to six (message word, sound byte) entries: for each, the sound (`0x1c5e2`) when not 0,
  then the message box (colour 0xa8, waiting) when the word is not 0. Correction (step 43): the byte is a picture, `0x1c5e2` is F2's
  `SPIC.LCZ` picture.
- **Kind 0x13, the ending (2: scene 52):** if flag (byte 3, bit byte 4) is set: the Sound Blaster driver ended, the music stopped,
  the buffers freed and `execl("ending.sl")`.

## WALK.SL step 40: the shop (kind 5, `0x22c42`), 2026-10-31

READ from the code (`work/b11/shop.asm`, `0x22c42` .. `0x24301`), built for types 0 to 3 and CONFIRMED by the comparison (see
**Checked** below); the inn's night (type 4) is read only. First SEEN in the oracle: the shipped start walked into scene 0's
shop (record 9 at (110, 28); `work/b11/shop1.sh`: `tools/walk_route.py 110:56` gives the keys), the loop entered at pass 347 and left on
Esc at pass 460. `tools/battle_check.py` now knows the shop's loop top `0x23386` (kind 17) and the inn's rest `0x240ec` (kind 18).
The record's bytes 3 .. 8 are copied to `[bp-0x80]` and passed: byte 3 is the shop's type `t`, bytes 4 .. 6 its three items (types 0
to 2) or a price word at 4 (types 3, 4). Of the 29 shops: types 0, 1, 2 sell things; 3 and 4 have one price. All the helpers are
ones the port already has.

**Opening:**
- the window (192 x 168 at (64, 16)) grabbed and shaded six times (`0x25dca` with the shading table) and put back: it darkens;
- the 320 x 14 strip at (0, 152) grabbed into a scratch buffer, copied twice (+0x1954 and +0x2ad8), the first shaded three times;
- `SHOP_PIC.LCC` at `t` x 0x3d86, 0x3d86 bytes: three 70 x 75 pictures of the shopkeeper, 0x1482 apart; the first at (70, 22);
- `MES_TXT.LCC` at `t` x 0x80 + 6: the greeting (0x78 bytes, up to `#`) drawn centred, colour 0x10, into the first strip copy, and
  10 bytes at 0x306 into the second at x 120 (SEEN: 歡迎光臨!要買些吃的嗎? under scene 0's shop);
- three boxes (`0x270e0(at, w, h, colour)`): 97 x 64 of colour 0x63 at (149, 23), 97 x 64 of 0x66 at (150, 24), its inside 96 x 63
  of 0xae (a frame with a shadow);
- types 0 to 2: for each of the three items, its `OBJECT.LCC` record ((item - 1) x 30, 0x1c bytes) into the map buffer; the item number
  and its price (word 0x12) kept at map buffer + 0xbb8 + 3k; its name (12 bytes, colour 0x10) with a shadow of colour 0x1a a row
  down, in the list's row k (21 rows apart from (150, 24)); its price (`0x24302`, five digits) 15 rows and 48 pixels in;
- types 3 and 4: `MES_TXT.LCC` 0x3cb (type 3) or 0x3bf (type 4), 12 bytes, colour 0xa8 with a shadow of 0xac, in the first row; the
  price at row 2: type 3 the record's word, type 4 that times the members alive (number below 5, HP not 0), which is written back
  into the record.

**The loop** (`0x23386`):
- a pass counter from 0xbb7 (at most 60,000): at 2 the second strip is shown, at 3000 the first; so the greeting shows at once;
- the palette steps every 26th pass (on the palette itself); now and then (`rand()` below 7 or above 0x7ff8) the shopkeeper's
  picture moves to the next of its three, which blinks;
- the lit row: its 96 x 21 recoloured 0xae -> 9, the one before back 9 -> 0xae;
- the keys after a release (`0x274b8` then the BIOS buffer emptied); the mouse: over the list (x 151 .. 244) the row under it is lit,
  the left button is Enter, the right is Esc. Esc leaves; Up and Down move the lit row (three rows);
- **Enter or Space, types 0 to 2 (a purchase):** if the coins (party word 0x122) cover the price, the member picker without faces
  (`0x1a8a4` with faces, a fallen member not taken, as F4's); the item goes into the first empty item place (bytes 0x14 .. 0x27) of the member picked, the price is taken
  from the coins, the coins redrawn (`0x1bd6e`), and SND_EF 0x1bf5 plays. No room: nothing. Too few coins: nothing;
- **type 3, first row (healing one member):** if the coins cover the price, the picker (args 1, 1); the member's HP (word 8) becomes
  his most HP (word 0xa), word 0x10 becomes 0, the price is taken, the sound; the picker here takes a fallen member too;
- **type 4, first row (the inn):** if the coins cover the price: it is taken; the window is shaded 16 times; `RPG.LCP` 0x7777
  (0x366 bytes) is unpacked into the scratch buffer (the stars' pictures); every member alive gets his HP to the most, word 0x10 to 0 and his MP (word 0xd)
  to at least 20 x his level (byte 0xc), at most 4000; then the night: five stars at random places, the music stopped and, with a Sound Blaster, a
  sound read from `RPG.LCP` 0x7dcb (0xc350 bytes) played over and over, and a loop (`0x240ec`) twinkling them until a key or a button.
- **Leaving:** the pointer off, the strip at (0, 152) put back; the world redraws itself on the next pass.

**The mouse** (`0x23664`): over x 151 .. 244 the row is y 25 .. 44, 45 .. 65 or 67 .. 86 (66 lights none), and the left button is
Enter there; the right button leaves anywhere. **The second strip** (MES_TXT.LCC 0x306) shows at the counter's 2, after a purchase or a
healing, until 3000.

**Stack leftovers:** the palette count `[bp-0x16]` is never set for types 3 and 4: 0x48 at scene 2's healer and 0x60 at its inn, the
hero's y both times (INFERRED: y; any value above 0x19 steps at once). For types 0 to 2 the item records' file offsets count it down to
1. The first picker's count is 0xc6 (SEEN at three shops, a segment byte, as F8's); after the coins are drawn it is 0 (SEEN once: what
`0x1bd6e`'s callees leave there); after a picker that bought nothing the port keeps the picker's own count (INFERRED, as in F5).

**Checked:** `tools/check_battle.sh`, 19 `shop_` starts, all identical at every pass, the member picker's passes included
(`tools/battle_check.py` counts the shop's loop `0x23386` as kind 17): scene 0's food shop entered from outside its door (purchases by
the hero, the second member and the fifth, with Space and the Sound Blaster, twice in a row, too few coins, no room, Esc in the
picker, Down past the third row, the mouse and the rows' edges, a 900-pass and a 4000-pass stay for the shopkeeper's blinks); scene
2's two type 2 shops; its healer (the hero, a fallen member with word 0x10 set, too few coins, the second row); its inn's opening.
`tools/mutate_battle.py`: 42 shop mutations, all caught (8 only after the last starts were added); one dropped (the record loop not run
again after a shop: a shop sets no flag). Three older patterns the shop duplicated were repointed and caught.

**The inn's night** (type 4): built 2026-10-31, step 41.

## WALK.SL step 41: the inn's night (shop type 4, `0x23c09` .. `0x2428b`), 2026-10-31

READ from the code (`work/b11/shop.asm`), built and CONFIRMED by the comparison. Enter, Space or the left button on the inn's first row,
with the coins for its price (its record's word times the members alive):
- the strip at (0, 152) put back and the price taken (the coins drawn again);
- the window grabbed and shaded 16 times, each put back, then filled with colour 0 (only that is seen: no wait between);
- `RPG.LCP` 0x7777 (0x366 bytes) read into the world buffer and unpacked into the scratch buffer: 0x798 bytes, a 16 x 10 bed at 0, the
  Zzz's 9 x 14 pictures from 0x5a0 (0x7e apart), and five 16 x 16 quilts from 0xa0 (0x100 apart); a 16 x 25 picture with its header
  at the world buffer's start, cleared once (`0x273f8`);
- five times, two `rand()`s: the 256-byte blocks at 0xa0 + 0x100 x (rand / 0x199a) swapped (`0x25c8e`), which shuffles the quilts;
- for each party record alive (HP not 0, number below 5): HP to the most (word 0xa), MP (word 0xd) to at least 20 x his level (byte
  0xc), the floor at most 4000; then into the picture (`0x26c04`, colour 0 clear, each over what the last left): the bed's 10 rows at row
  0, his sprite (bank 2 + number x 0xc00 + 0x400 + 0x300 x (rand / 0x2aab)) at row 4, the quilt of his place in the party at row 9; the
  picture shown (`0x27371`) at (115, 88) + 18 a sleeper. A fallen member is neither healed nor drawn, and his quilt is skipped;
- the scratch buffer's first 15 bytes: five Zzz's, a word (0x5a0 + 0x7e x (rand / 0x2000)) and a byte (rand / 0x82);
- the music stopped (`0x165d:0x126`) when its flag is 0x81; with the Sound Blaster's flag 0x81, `RPG.LCP` 0x7dcb (0xc350 bytes, a VOC
  sound-data block) read into the world buffer.

**The night's loop** (`0x240ec`, kind 18 in `tools/battle_check.py`): each round, every sleeper's Zzz (9 x 14 from the scratch buffer at
its word, opaque) at (113, 73) + 18 a sleeper; a counter `[bp-0xc]`, and past 4 every Zzz's byte goes up, and from 0xfe to 0 with its
word 0x7e on (past 0x71a back to 0x5a0); with the sounds on and none playing, the sound again (`0x1c68e`); then the keyboard port read
raw: a make code ends it, so the Enter that chose the inn, if still held, ends it in its first round; else any mouse button ends it.
At the end the sound is cut and the shop ends without putting its strip back; the world draws its window again on the next pass.
Nothing read starts the music again (GUESS: it stays off until the next song is started).

**Stack leftover:** the counter `[bp-0xc]` is the shop's: the latched mouse buttons of its last round with a driver, or the low byte of
the member a picker gave; before either, 0xbc (SEEN at scene 2's inn without a mouse; INFERRED constant).

**Checked:** `tools/check_battle.sh`, 7 `shop_night` starts, all identical at every pass: Enter released at once then Space, Enter held
(one round), the Sound Blaster (its sound five times), the mouse, 2,460 rounds with five members one fallen, the MP floor (0, the 4000
cap, one above it), too few coins. `tools/mutate_battle.py`: 28 inn mutations, all caught (two after the MP start was added); two
dropped with notes. ASan / UBSan clean on four starts; a real-time night under SDL's dummy drivers. `tools/sb_stub.py` now also finds
sounds in `RPG.LCP`, and `src/sfx.c` reads it (`SFX_RPG`).

## WALK.SL step 42: the boat rides (kind 8, `0x17ff4`, `0x1be44` .. `0x1c283`) and the snow, 2026-10-31

READ from the code (`work/b11/k8.asm`, `work/b11/boat.asm`, `work/b11/boath.asm`), built and CONFIRMED by the comparison.

**Kind 8** (`0x17ff4` .. `0x1816f`): the pointer off (`0x1732:8`, with a mouse driver); `0x1be44` with the world buffer `[bp-0x8c]`, the
map buffer `[bp-0xba]`, the 64 KB buffer `[bp-0x20]`, the palette `[bp-0xac]`, bank 2, the party table and the shading table; then the
villagers' byte 8 put back, the scene's 1750 bytes into `__1.NOW`, party bytes 510 .. 513 = the record's bytes 3 .. 6, `[bp-0x74]` set
(the loader and the zoom in follow, as after a door); the pointer's place 0 with the corner kept (`0x1723:0xc`). No sound, no zoom out.

**The ride** (`0x1be44`):
- `BOAT.LCZ` (0x43b7 bytes, `ds:0x171`) read into the map buffer and unpacked (`0x25eeb`) three times: from 0 into `[bp-0x20]` the water,
  192 x 168 with its header; from 0x1b04 into the map buffer itself the shore, 192 x 29 (0x15c4 bytes out, so the third block at
  0x28ec is not reached); from 0x28ec into `[bp-0x20]` + 0x7e04, 0x3a20 bytes with no header: two 80 x 55 frames of the boat, then two
  80 x 38 fronts;
- the world buffer's first 0x3208 bytes cleared (`0x273f8`); two 80 x 80 pictures with headers at 0 and 0x1904; each frame's 0x898 words
  into one (`0x27503`); for the party records at 3, 43, .. 163 whose number is below 5 (fallen ones too) the 16 x 16 sprite at bank 2 +
  number x 0xc00 + 0x100 into both at row 39, 16 pixels a member (`0x26c04`, colour 0 clear); the fronts over rows 42 .. 79 of each
  (`0x26d52`, colour 0 clear);
- 84 steps, the size n from 168 down by 2 to 2: a Sound Blaster sound that has played out ended (`0x190e:0x6d`, `0x190e:0x49(0)`;
  nothing shows); the palette step (`0x1c284`); the water's rows turned 2 pixels left (`0x2758d` mode 2: the first word goes to the
  row's end) and its 0x3f02 words copied to the world buffer + 0x32c8; the frame `[bp-8]` (0 and 0x1904 in turn) stretched
  (`0x26de8`, n above 80) or shrunk (`0x26f72`) to n x n into `[bp-0x20]` + 0x7e04, and when shrunk shaded 5 - n / 16 times
  (`0x25dca`, 0x1900 bytes); it is put (`0x26ad2`, colour 0 clear, no clipping) at `[bp-0xa]` into the picture at + 0x32cc, 192 wide;
  the shore turned 6 pixels (three calls) and put at `[bp-0x10]`; the picture shown at (64, 16) (`0x27328`, 0x1440);
- the places: `[bp-0xa]` from 12 goes on by 0x181 + `[bp-6]` - 0xc0 (3 rows and 1 pixel, or 1 row and 1), `[bp-0x10]` from 0x6840 by
  `[bp-6]` - 0xc0 (a row down or up); `[bp-6]` flips between 0x180 and 0 when `[bp-0x10]` is outside 0x6900 .. 0x72c0, so the shore
  bobs between rows 140 and 154. The boat's first steps and the shore's low places write past the picture's 168 rows (up to 0x8925
  bytes from its pixels); nothing shows those bytes and the loader redraws the buffer. The ride reads no key.

**Weather mode 2 is snow** (`0x2c236` .. `0x2c28e`, the rain routine `0x2c1ae` with its mode argument 2). Scene 3 (the first pier) has
mode 2, so the notes of steps 3 and 6 ("read and never seen", "probably snow") and the port's "nothing is drawn" were wrong: the oracle's first screen there
shows it. On the rain's 75 places, with the rain's stride and clock: the first 40 are 2 x 2 flakes (0x14 0x14 over 0x16 0x16) moving
by half the stride, the other 35 crosses (0x10 at +1; 0x11 0x11 0x12 at +0xe0; 0x11 at +0x1c1) moving by the full stride; the same
wrap at 0xa100. CONFIRMED by the boat starts of scene 3 (before it the first pass differed in 210 pixels).

**Starts:** `work/b11/boatfind.py` (`doorfind.py` for kind 8) found straight walks onto scene 3's two piers; scene 1's record 7 and
scene 7's record 1 were reached by starting below them (`work/b11/boatnear.py` maps the walkable places); scene 1's records 4, 8 and 9
and scene 7's record 0 were not reached by a single held arrow.

**Checked:** `tools/check_battle.sh`, 10 `boat_` starts, all identical at every pass with the 84 + 13 steps compared one by one
(`tools/battle_check.py` adds the ride's step at `0x1c21a`): scene 3 to 1 from both piers, 1 to 3, 7 to 1, five members aboard, a fallen
member (he rides), the pointer over the window and over the panel (moved after the ride), the Sound Blaster, the music.
`tools/mutate_battle.py`: 30 boat and 3 snow mutations, all caught (the two pointer ones after the panel start was added); dropped with
notes: the stretch's rows counted from 1 (no size crosses a row differently), clipping to the picture (never shown), the villagers'
byte 8 (equal to its copy in every start). The state write and the place copy are now `scene_leave`, shared with the door, whose
mutations cover them. ASan / UBSan clean on four starts; a real-time ride under SDL's dummy drivers ended in scene 1.

## WALK.SL step 43: number locks (kind 7) and scripted talks (kinds 0x11, 0x12), 2026-10-31

READ from the code (`work/b11/rec.asm`, `0x17e3d` .. `0x17fe2` and `0x18305` .. `0x18536`), built and CONFIRMED by the comparison.
Both use helpers the port has: the message box `0x1afb0` (with its wait loop `0x1b506`), the number box `0x18fdc`, F2's picture
`0x1c5e2` and the flags `0x2c158`. Both pass the palette itself (`[bp-0xac]`) to the boxes, not the talk's dimmed copy.

**Kind 7, a number lock:** nothing happens while its flag (byte 5, bit byte 6) is set. Otherwise: message 9 (colour 0x68, not
waiting; nothing puts the screen back after it); the pointer off (`0x1732:8`); the number box; the pointer's place 0 with its corner
kept (`0x1723:0xc`); the number equal to the word at byte 3 sets the flag and plays `SND_EF.1` 0x1bf5, any other gives message 10
(colour 0x68, waiting). Then, as for every record, the record loop runs again (`0x18b82`), which applies what the flag opens. The
number box's palette count starts from a stack byte: 0xee at scene 4's lock (SEEN with `work/b11/probe_pic.py` at `0x191af`); any
value from 0x16 up steps the palette at the first round. The answers in the shipped `__1.NOW`: scene 4 15388, scene 21 83, scene 30
39517, scene 43 4, 9, 6, 7 and 3.

**Kinds 0x11 and 0x12, a scripted talk:** the pointer off. When its flag (byte 3, bit byte 4) is set and byte 5 is not 0: the flag
(byte 0xd, bit byte 0xe) set; byte 5 = 0 in the scene's records (so it plays once, and the scene's state written by a door keeps
that); the map buffer's first 0x14 bytes cleared (`0x273f8`); the record's bytes 6 .. 11 copied there and, when the next record's
kind is 0x12, that record's bytes 3 .. 14 after them. Each of the six 3-byte entries is a message word and a picture byte: the
picture (`SPIC.LCZ`, framed at (109, 45), as F2's) when the byte is not 0, then the message (colour 0xa8, waiting) when the word is
not 0. Then the pointer's place 0 with its corner kept, and the record loop again. A kind 0x12 record stepped on does nothing.
Scene 42's talk (record 11; flag 0 bit 0, set from the start) is messages 0x22f with picture 12, then 0x230; scene 22's (record 13,
with record 14 of kind 0x12; flag 7 bit 3) is 0xd7 with picture 1, 0xd8, 0xd9 with picture 11, 0xda, 0xdc with picture 10.
**Correction to step 39:** the entries' byte is a picture, not a sound.

**Starts:** `work/b11/lockfind.py` (the door finder for kinds 7 and 0x11) gave straight walks for every lock and scene 42's talk; scene
22's talk is in rooms the Python walk model cannot judge (its tiles raise in `walk_idle.foot_ok`), and a start below it was found by
trial. The flags for a start are set with `tools/walk_scene_data.py --byte 0:200+idx:value`.

**Checked:** `tools/check_battle.sh`, 9 `lock_` and 7 `script_` starts, all identical at every pass: scene 4's lock given a wrong
number, closing message 10 by key or the right button; scene 43's last lock given 3 by keys, with the Sound Blaster (its sound), by
the mouse on the box's arrows and middle, with the pointer still on the panel, then stepped on again (no box); the lock with its flag
already set; scene 42's talk by keys, by the right button, with the pointer still, stepped on again (byte 5 is 0: nothing); scene 22's
five messages, without its flag, and saved after it (the flag at 0x8d is past the part of the party table the passes compare; the
save's files are compared). `tools/mutate_battle.py`: 14 lock and 12 talk mutations, all caught (four only after the still-pointer,
re-visit and save starts were added); four dropped with notes. Four older patterns that the shared number box changed were repointed.
ASan / UBSan clean on four starts. A real-time run under SDL's dummy drivers walked scene 43 into its last lock's place with the keys
of the starts and reported nothing unbuilt (nothing in its log shows that the box opened).

## WALK.SL step 44: warps (kind 0, `0x1732f` .. `0x17678`), 2026-10-31

READ from the code (`work/b11/rec.asm`), built and CONFIRMED by the comparison. A kind 0 record (26 of them, in the dark scenes 23
and 50; 13 have flag 0 bit 0, which is set from the start, the others a flag of their own) acts when its flag (byte 3, bit byte 4) is
set; else nothing happens.

- **The turning:** `[bp-0xc4]` from 0 to 59; each round a counter `[bp-0x10]` goes up and, past 1, back to 0 with the party turning
  (`0x2489a`, the picture doors' `walk_spin`), so every second round; the world buffer's bytes 0 .. 3 = 1, the view's x (`[bp-0x8e]`),
  y (`[bp-0xb6]`) and `[bp-4]` (the dark flag), the world drawn (`0x19680`, with its people) and the window copied (`0x2b9ec`); a make
  code on the keyboard port (`0x1732:0x198`) ends the rounds. So with the arrow still held there is one round. `[bp-0x10]` starts at 0
  in every start compared (it is a stack variable of the main loop; INFERRED, as for the picture doors).
- **The slide:** the target is (byte 5, byte 6), both in the view's units (byte 6 a full y). Each round, unless the view is there: the
  buffer's byte 0 = 0 (no villagers, no party), the world drawn and shown, then x one step towards the target and y one step towards
  it (both in the same round).
- **The arrival:** the five party records' x and y and the 13 trail entries (x, y, facing 1) put at the target. `[bp-0xbe]` is 0, so
  the record loop does not run again after it.

**Checked:** `tools/check_battle.sh`, 8 `warp_` starts, all identical at every pass, each round compared as a step (`tools/battle_check.py`
counts the rounds after their window copies, `0x17441` and `0x17563`): scene 23's record 1 with the arrow still held and let go on
arrival (all 60 turning rounds), with the mouse, walking on after it; its record 0 without and with its flag; scene 50's records 9 and
13 (the slide both ways). Starts from `work/b11/warpfind.py`. `tools/mutate_battle.py`: 16 warp mutations, all caught; one dropped (the
record loop run again: a warp sets no flag). The picture door's key pattern, which the warp's duplicated, was repointed. ASan / UBSan
clean on three starts; a real-time run under SDL's dummy drivers walked onto scene 23's warp with nothing reported unbuilt.

## WALK.SL step 45: the card game (kind 0xe, `0x18181` .. `0x182f3`, `0x24962` .. `0x25b52`), 2026-10-31

READ from the code (`work/b11/kinde.asm`, `work/b11/text.asm`), built and CONFIRMED by the comparison. Scene 22's record 12 at
(172, 48) holds a higher-or-lower card game against a dealer. `tools/battle_check.py` now knows its two loops: the betting loop
`0x25088` (kind 19, "cards") and the wait after a card `0x258ad` (kind 20, "cards end"); the window's shading and the cards' turning
are compared as steps (`0x24a17`, `0x256c6`, `0x2576d`, `0x25a66`, `0x25b14`).

**The record:** the pointer off; with coins (party word 0x122) the game; then (with coins or not) the party and the trail's x and y
at (byte 3, 2 x byte 4), the roof flag cleared, the pointer's place 0 with its corner kept and the record loop again. Without coins
only the pointer and the loop.

**The buffers:** the game uses the world buffer `[bp-0x8c]` up to 0xea6b (the deck is at 0xea60), `[bp-0x20]` and the map buffer
(digits at 0xd070), past what the port's own buffers hold, so the port gives it three 64 KB buffers of its own laid out as the
original's. Two overlaps in that layout are visible: the fourth text strip ends at 0x83e2, 12 bytes into the first dealer picture at
0x83d6, which is read after the strips were made (so the strip's last 12 pixels are the picture's); the 190 x 36 grab at (65, 66) goes
over the table's picture (27 x 30, already shown) at the world buffer's start.

**Opening (`0x24962`):** the window grabbed and shaded six times (`0x25dca`), each shown; `RPG.LCP` 0x1643b (the number box) unpacked
and shown at (113, 150), a 46 x 10 box of colour 0 at (137, 153); the 160 x 14 at (80, 169) grabbed and copied three times;
`MES_TXT.LCC` 0x17586: four 128-byte texts, drawn by `0x243ec` (arguments: text, picture, characters, colour, font; 20, 20, 18 and 18
characters in colour 0x78) and centred in the four strips; `RPG.LCP` 0x58f4 (0x1e83 bytes): ten 19 x 36 card faces 0x2ac apart, a
card's back with its header at 0x1ab8 and the table (packed at 0x1d68, 27 x 30, shown at (71, 62) with colour 0 clear);
`SHOP_PIC.LCC` 0x1339e: five 70 x 75 dealer pictures, 0x1482 apart; the ten backs 19 apart in a 190 x 36 picture (`0x26a88`, opaque);
the deck 0 .. 9; the bet 0.

**A round (`0x24dcd`):** the backs shown at (65, 106); 20 swaps of two deck places at random (rand / 0xccd each, which reaches 10, one
byte past the deck, for rand 0x7ff2 .. 0x7fff; the port keeps a 0 there, GUESS); the way to win at random (rand / 0x4000: 1 higher,
the lit arrow at (69, 61), else lower, at (69, 77)); a light runs 20 .. 30 places (rand / 0xccc + 20) along the marks over the cards
(two lights, 19 x 4 frames 19 apart from (65, 101)), the dealer's first picture and the palette stepping at each; where the bright
light stops is the dealer's card. The ten marks under the cards; the bet (`0x24302`) at (141, 155).

**Betting (`0x25088`):** the strip with text 1 (no bet) or 2; the palette every 31st round (`[bp-0x1a]`, a stack byte: 0x19 at the
first round, SEEN); the lit card's mark; the keyboard port raw (and the BIOS buffer emptied); with a mouse: the pointer drawn, a
button let go before it counts, + (113 .. 130, 150 .. 164) and - (189 .. 206) and a card (65 .. 254, 111 .. 131; hovering lights it,
the left button picks it). A key counts after a release; Left and Right move round the ten; keypad - or `-` (0x4a, 0xc) and keypad + or
`=` (0x4e, 0xd) take one from or add one to the bet (at most 500, from the coins), and held they repeat, the wait between repeats
starting at 250 rounds and one shorter each time (`[bp-8]`, `[bp-0x1c]`); the bet and the coins drawn again after a change (and the
palette steps in the next round). Space or Enter picks the lit card unless it is the dealer's; with no bet it ends the game.

**A card:** the coins written; the faces drawn over the kept 190 x 36; the row turned over (the backs squashed from 36 rows to 2 in 18
steps, the faces from 2 to 34 in 17, each centred in a copy of the grab and shown), the faces shown; the bet doubled when the card is
higher (lower) than the dealer's as the arrow said, else lost (an equal card loses); text 3 (a win) or 4; with no bet and no coins
the end; the bet drawn, `SND_EF.1` 0x1bf5 played on a win, 0 on a loss. **The wait (`0x258ad`):** the dealer's picture 1 or 3, the
other of the two (2 or 4) every 91st round; the palette every 6th; a make code or any mouse button (no latch) ends it: the coins
written, the faces turned back to the backs, the next round. **The end:** message 0xe1 (colour 0xb0, waiting); nothing puts the
screen back (the window is drawn again on the next pass; the number box stays on the left).

**Checked:** `tools/check_battle.sh`, 12 `cards_` starts with 1000 coins (one with none, one with one), all identical at every pass and
step: the end at once and walking on; three rounds by keys (with the Sound Blaster); `=`, `-`, Left round the row and Space; + held
for 580 rounds; the mouse on +, -, a card a pixel from its edge and to end the wait; the dealer's card tried first and a 240-round
wait; a button held from a card into the next round; the pointer left over the number box at the end. `tools/mutate_battle.py`: 43
card mutations, all caught (three after the starts were added); five dropped with notes (the text pictures' extra row, a rand range no start reaches, two that change
nothing, the button latch the key latch covers). ASan / UBSan clean on three starts; a real-time game under SDL's dummy drivers.

## WALK.SL step 46: the ending (kind 0x13, `0x18548` .. `0x18639`), 2026-10-31

READ from the code (`work/b11/rec.asm`), built and CONFIRMED by the comparison. Scene 52's records 5 and 6 at (146, 60) and (146, 61)
test flag 20 bit 1 (party byte 220, value 2). With it set: the Sound Blaster driver ended (`0x190e:0x76`) when there is one
(`[0x380]:0xc`); the music stopped and its driver shut down (`0x165d:0x126`, `0x165d:0x14e`) when it is on (`[0x37e]:5`); six far
buffers freed (`0x176b:0x704`: the world buffer, `[bp-0x20]`, `[bp-0x6e]`, `[bp-0x6a]`, the map buffer, `[bp-0x54]`); then
`execl("ending.sl", 0)` (`ds:0x11c`). Nothing is written first: the scene's state and the party stay as they are in memory. Without
the flag nothing happens and the record loop runs again as for any record. Should the exec fail, WALK.SL goes on with its buffers
freed (the emulator's C runtime returns from `execl`, so `tools/battle_check.py` stops the run at the call `0x18634`).

The port ends the walk there (`walk_ended`): `src/main.c` stops the song and the sounds and reports the end. `ENDING.SL` itself
(14,756 bytes packed, 25,301 unpacked) is not read or built.

**Checked:** `tools/check_battle.sh`, 4 `end_` starts, all identical up to the pass WALK.SL ends in, which is the same in both: both
records with their flag, with the music and the Sound Blaster, and without the flag. `tools/mutate_battle.py`: 3 mutations, all
caught; one dropped (the world going on after the end, which nothing compares). A real-time run under SDL's dummy drivers walked onto
the record and the port stopped with "walk: the game's end".

## WALK.SL step 47: a sell counter stepped on (kind 0x14), 2026-11-01

READ from the code (`work/b11/rec.asm`, `0x1863c` .. `0x18b7d`) and built, not compared: no start can reach it. Stepping on a kind 0x14
record runs the same code as F7's sale (step 36), on whatever the map buffer `[bp-0xba]` holds (the item's name, its byte 0x11 and its
price word 0x12 come from it) and with F7's member and item as F7 last left them; at its end F7's button is turned (`0x8c3:0x378a`
at 0x9884) though it was never pressed, and `[bp-0xbe]` = 1.

**Unreachable in the shipped data (SEEN):** each of the five counters (scenes 0, 2, 21, 33, 41) stops the hero one step short from all
four sides (`work/b11/sell1.sh`, `sell2.sh`: below at y + 2, above at y - 2, beside at x - 4 and x + 4), and a hero placed on a
counter's cell cannot move (`work/b11/sell3.sh`: Right and Down leave him at the counter's place). The record finder only runs after a
step, so the sale cannot start that way either.

**Checked:** `tests/test_walk.c` puts a kind 0x14 record under scene 0's door and checks that the sale's box opens and no kind is
reported; `tools/mutate_battle.py` "sellstep: not taken" is caught by it. The 23 `pawn_` starts stay identical.

## ENDING.SL step 1: what it is and how it is laid out (survey), 2026-11-01

SEEN in the emulator (`tools/dosemu.py work/unpacked/ENDING.SL.unpacked.exe --log-files`, no sound card: `work/e1/run.log`; a
snapshot every 150 ticks, `work/e1/sheet.sh`, `work/e1/sheet.png`) and READ in outline from its `main` (`0x10000` .. `0x113ff`,
`work/e1/ending.asm`, `work/e1/loops.py` for its loops and file opens). Nothing is built yet.

**The run:** 4,869 BIOS ticks (about 4 min 27 s) without a sound card, then `exit(0)` to DOS (not MARK.EXE). It reads, in order:
`muc.all` (the music, offset 0x7137), `ani.lcp` (0x600 bytes: palettes), `ending12.lcz`, `ending1.lcz`, `ending1t.lcz` (tick 3 .. 5),
`ending2.lcz` and `_1_.loc` (the voice, tick 511), `ending3.lcz` (675), `ending4.lcz` (957, again at 1007), `ending4t.lcz` (1008),
`ending5.lcz` (1042), `cast2.lcz` (1291). Seven 0xdc00-byte buffers and one 0x5420-byte buffer are allocated first ("Memory is not
enough !" when one fails).

**The loops of `main`** (by address): `0x1042d` .. `0x10600` after `ending12/1/1t.lcz`; `0x106d1` .. `0x108cd` after `ending2.lcz`
and `_1_.loc`; `0x10968` .. `0x10b69` after `ending3.lcz`; two from `0x10cd1` to `0x10e9d` after `ending4.lcz`; three from `0x10fd6` to
`0x110c5` after `ending4.lcz` and `ending4t.lcz`; six from `0x1112f` to `0x11215` after `ending5.lcz`; `0x112c1` .. `0x11381` after
`cast2.lcz`.

**What is on the screen** (snapshots; which loop draws which is not yet tied down): ticks 50 .. 500, clouds drifting over a lilac sky,
the villain (a blonde woman in a red cape) rising from the bottom, then her speech with subtitles; tick 650, the heroes' faces in
boxes over black; tick 800, a dark red sky full of sparks with her figure on both sides; tick 950, a sunset over the sea with
silhouettes; from about tick 1100 to the end, the credits rising slowly over the silhouettes; at tick 3000 of the first run, a girl in
blue sitting on the grass (not caught by the 150-tick sheet: a short scene, or the runs differ; to be checked).

**Helpers:** `main` calls the C runtime (`0x265:*`, `0x204:*` for files), and library routines in segments `0x14d`, `0x152` (an
unpacker, `0x152:0xdd`), `0x16b`, `0x19c` (FM music), `0x1e7`, `0x21d`, `0x22c`, `0x247` (blits), `0x2fc` (the voice, CT-VOICE);
its own near helpers start at `0x11400`. The intro's port (`STORY2.SL`, `src/intro*.c`, `src/slide.c`) and `CAST.SL`'s
(`src/credits.c`) likely share several of these; that is to be checked routine by routine.

## ENDING.SL step 2: the first scene (`0x101f1` .. `0x10600`), 2026-11-01

READ from the code (`work/e1/ending.asm`, the helpers in `work/e1/helpers.asm`), built in C (`src/ending.c`) and CONFIRMED by the
comparison: `tools/ending_check.py` records the original's screen and palette at each arrival at the loop top `0x1042d` and at the
loop's end `0x10603` (389 records: 388 passes and the end), and `tests/ending_dump.c` writes the port's.

**Set-up:** `muc.all` 0x49a2 bytes at 0x27137 (the intro's song, started by `0x19c:0x40` only when an FM card was found); `ani.lcp`'s
first 0x600 bytes (two palettes); mode 0x13; the DAC's 256 entries from the first palette (`0x22c:0x14e`). Each `.LCZ` is read whole
into the file buffer `[bp-0x12]` (at most 0xffdc bytes) and unpacked (`0x115fd`, the usual run-length code): `ending12.lcz`, the
clouds, 320 x 200 with a header, into `[bp-0x22]`; `ending1.lcz` in three blocks, at 0 five 41 x 44 pictures of her face with headers
(0x710 bytes each) into `[bp-0xe]`, at 0x1925 and 0x40d1 two 320 x 147 pictures of her without a header into `[bp-0x36]` and
`[bp-0x46]`; `ending1t.lcz`, four 236 x 15 subtitle lines (0xdd4 bytes each, no header), into `[bp-0x1e]`.

**A pass (`0x1042d`):** the clouds' rows turned a pixel to the left (`0x1252d` mode 1; WALK.SL's mode 2 turns two); the current face
picture (opaque, `0x11e7c`) into both pictures of her at offset 0xbcf, (143, 9); with a Sound Blaster a sound that ended started again
(`0x19c:0x102`, not built); the clouds copied into the file buffer, which is the frame; a counter `[bp-0x48]` past 4 back to 0 turns
`[bp-0x1a]`, so her two pictures take turns every fifth pass; the first `[bp-0x24]` bytes of the one shown (colour 0 clear, `0x12146`)
at `[bp-0x28]` in the frame. Both start at one row (0x140 bytes at 0xfa00, the bottom row), and after the first 51 passes (`di`) one
row more each pass, from one row higher, until all 147 rows show (from row 53); she rises out of the bottom. Once she is up
(`[bp-0x2a]`): every sixth pass (`[bp-0x3e]`) the next face picture; after the fourth (0x1c40) the first again, and after two rounds
(`[bp-4]`) the next subtitle line, drawn each pass at (42, 184) (`0x11ff8`, colour 0 clear); after the fourth line the loop ends at
once, before the frame is shown. Else the frame (0x7d00 words) is copied to the screen. Nothing in the loop waits or reads a key.

**Stack leftovers (SEEN at the first loop top):** `[bp-0x48]` is 0xa, the handle `ending1t.lcz` was read through; `[bp-0x3e]` is 0. The
fifth face picture is never shown. The palette does not change during the scene.

**Checked:** all 389 records identical. `tools/mutate_ending.py`: 18 mutations, all caught. ASan / UBSan clean on the whole scene.
`build/3k --scene ending` plays it in real time (`TK_ENDING_PASS_HZ`, 35 passes a second, a choice) with the intro's song, then stops;
a run under SDL's dummy drivers reached the end of the scene.
