# Code analysis notes

What the program code does, by program, with addresses. Source of truth for
renaming and for rebuilding the engine.

Conventions:
- Addresses are Ghidra linear addresses for the **unpacked** program in
  `work/unpacked/`, load segment 0x1000 (first byte = `0x10000`). Ghidra names
  such as `FUN_1175_0008` mean segment `0x1175`, offset `0x0008`.
- "Seen" = read in disassembly or decompiled output. "Inferred" = deduced from
  how a function is used. "Guess" = from names or sizes only.
- Disassemble with `work/venv/bin/python tools/dis16.py FILE START [END]`
  (needs the `capstone` package in `work/venv`, see `environment.md`).
- Ghidra annotations made through REA do not persist (the session is ephemeral).
  Record every renaming here instead.

---

## MARK.unpacked.exe

Built with Microsoft C 1992 (small/medium-model style 16-bit code). Load image
24,161 bytes at `0x10000`..`0x15e60`, then 2,064 bytes of uninitialised data.
136 functions in Ghidra; **a region around `0x10e1a`-`0x11126` has no functions
defined by Ghidra** (hand-written assembly the analyser did not recognise); use
`tools/dis16.py` there.

### Entry and main
| Address | Role | Status |
|---------|------|--------|
| `0x121d2` | `entry`: Microsoft C start-up (DOS version check via `int 21h`, memory sizing, clearing BSS), then calls `main` | seen |
| `0x10000` | `main`. The decompile shows many calls to the file-open/read/close wrappers, mouse setup, a loop that polls a key code and exits on 0x39 (Space) or 0x1C (Enter), music, and palette changes | seen (decompiled, **not yet interpreted**; Ghidra's output for this function is messy) |

### Graphics layer (Mode X-style routines that the game does NOT call)
**CORRECTION 2026-10-14: none of these routines has a caller in any program
(see `findings.md`). The game runs in plain mode 13h. They are an unused
library, linked into every program. Their descriptions below are still accurate
as to what the routines do.** The heading used to say "Mode X ... unchained".
All seen in disassembly.

| Address | Role | Notes |
|---------|------|-------|
| `0x118b3` | `set_video_mode(mode)`: `xor ah,ah; mov al,[bp+6]; int 10h` | tiny wrapper |
| `0x118c0` | `enter_mode_x()`: `int 10h` AX=0013h, then sequencer reg 4 := 06 (chain-4 off), CRTC reg 0x14 := 00, CRTC reg 0x17 := E3 | seen. No `enter`/`leave`; pushes `ds`, `si`, `di`, pops them and returns with `retf` (so it is a separate function from `0x118b3`) |
| `0x110b9` | `set_screen_start(offset)`: CRTC regs 0x0C/0x0D | hardware page flip / scroll |
| `0x110d4` | `outp(port, value)` wrapper | |
| `0x110e1` | `inp(port)` wrapper | |
| `0x110eb` | seq map-mask := 0xF, gfx ctrl mode := 0x49 (write mode 1) | setup for latch copies (falls through to retf) |
| `0x110fa` | `copy_page(src, dst)`: 0x3E80 = 16,000 bytes with write mode 1 | VGA-to-VGA copy of a 320x200 page (4 planes at once, latch copy) |
| `0x10e1a` | `blit_planar(dst, sprite)`: reads `[w][h]` from sprite; for planes 1,2,4,8 sets map-mask via `out 3c4h` and `rep movsw` a column of `w/2` words per row | sprite data is stored **grouped by plane** |
| `0x10eb3` | a second routine with the same prologue as `0x10e1a` (variant blitter) | **guess**: only its first lines were seen, not read through |
| `0x11090`..`0x110b8` | a copy loop (`rep movsw`, with an odd-byte tail via `movsb`) that skips the first 4 bytes of source and destination, the size of a `[w][h]` header | seen, but its start is above `0x11090` and was not read |
| `0x11f72` | `set_palette(rgb_ptr, seg, count)`: wait for vertical retrace (port 0x3DA), `out 3d8h,1`, DAC index := 0, write `count*3` bytes to port 0x3C9, `out 3d8h,9` | decompiled and read; takes 6-bit palette triples, the format of `RPG.LCP`/`ANI.LCP` |

**Interpretation (inferred):** the screen is 4 planes of 80 bytes per row. A
sprite stored as `[w:2][h:2]` followed by pixel data, grouped by plane, is
drawn by four `rep movsw` passes with the plane mask changed between them.
The `.LCZ` sprite headers `[w][h]` match this header. Not verified: whether the
`.LCZ` decompressor reorders pixel data into planes, or whether the plane
grouping is in the stored data. Check by comparing a decoded `.LCZ` sprite
with the memory a blitter reads.

**For the port:** a modern version needs a plain 320x200 8-bit framebuffer
and a palette; the plane logic can be dropped as long as the sprite format is
converted. Screen start offset (page flip) becomes "which buffer to present".

### System services
| Address | Role | Status |
|---------|------|--------|
| `0x11758` | `dos_open` (`int 21h`, AH=3Dh in the wrapper at `0x1176a`): returns 0 and stores the handle, or 5 on carry | seen (decompiled) |
| `0x11784` | `dos_close` (AH=3Eh) | seen |
| `0x117a3` | `dos_read` (AH=3Fh) | seen |
| `0x117d3` | `dos_seek` (AH=42h) | seen |
| `0x117f9` | `dos_write` (AH=40h) | seen |
| `0x10d2c` | `mouse_call(n)`: the `int 33h` helper for subfunctions 0-6 | seen (decompiled); the numbers and mapping are not fully traced |
| `0x10d9f` | `mouse_poll(mode, &x, &y)`: mode 0 = get position and clamp x <= 0x13A (314), y <= 0xC2 (194); mode 1 = position and buttons; mode 2 = int 33h again | seen (decompiled) |
| `0x12474`-`0x13a6c` | The addresses where the `int 21h` calls with function numbers 4Ch (exit), 4Ah/48h/49h (memory), 30h (DOS version), 25h/35h (interrupt vectors) and 44h/43h (file attributes) appear | **inferred**: this looks like the Microsoft C runtime (the strings say so). Not read function by function |

**Mouse note:** the clamp of the pointer position to 314 x 194 leaves a 6 pixel
margin at the right and bottom of the 320 x 200 screen. Likely related to the
mouse cursor image size. Inferred.

### Function-call convention (inferred)
Far calls (`retf`), C-style stack arguments at `[bp+6]` and up (so the first
16-bit argument is at `[bp+6]`, the far-return takes `[bp+2..bp+4]`, and `enter 0,0`
was used for the frame). Ghidra's pseudo-code names them `param_1`... and gets
arguments partly wrong where the function is hand-written assembly.

### Open questions
1. Who calls `enter_mode_x` (`0x118c0`)? It has no frame and ends with `retf`
   without `leave`, so it may be called through a table.
2. How many functions are hand-written assembly versus compiled C? The
   un-recognised region around `0x10e1a`-`0x11126` suggests a separate
   assembly module (graphics primitives) linked into the C program.
3. Where is the palette read from, and which palette goes with which picture?
4. (answered 2026-10-14, see "Program chaining" below.)
5. What are mouse subfunctions 4, 7, 8, 9, 15 (cursor shape, limits,
   sensitivity)? Needs the call sites.


---

## Program chaining: how MARK, TV and the `.SL` files start each other

Added 2026-10-14. Method: string extraction (`tools/mz_strings.py`), a byte
scan for instructions that load each string's offset (data segment `0x15b0`,
linear base `0x15b00` in `MARK.unpacked.exe`), and disassembly with
`tools/dis16.py`. Cross-checked against the saved DOSBox-X traces in
`work/logs/`.

### There is no filename table
Each program holds its filenames as **plain string literals in its data
segment** and loads the offset right at the call site (`mov ax, offset`).
Nothing indexes into a table. In `MARK.EXE` all uses are inside `main`
(`0x10000`..`0x10bc2`). Order of the literals in the data segment (offset from
`0x15b00`), and the code address that loads each one:

| DS offset | String | Loaded at | Used for |
|-----------|--------|-----------|----------|
| `0x42` | `rpg.lcp` | `0x10026` | read first 1 byte (speed class check) |
| `0x4a` | `tv.exe` | `0x1007d` | `exec` target if that byte is > 10 |
| `0x51` | `ani.lcp` | `0x10158` | read `0x600` bytes (two 768-byte palettes) |
| `0x59` | `ani.lcp` | `0x101aa` | second read |
| `0x61` | `rpg.lcp` | `0x101f6` | |
| `0x69` | `title1.lcz` | `0x10246` | title picture |
| `0x74`, `0x7c` | `muc.all` | `0x1039f`, `0x10570` | music data |
| `0x84` | `title1.lcz` | `0x1063d` | |
| `0x8f` | `walk0.sl` | `0x10b9a` | exec target (menu) |
| `0x98` | `story2.sl` | `0x10bb0` | exec target (intro slideshow) |

Status: addresses seen (byte scan plus disassembly of the call sites at
`0x10026`, `0x1007d`, `0x10158`, `0x10b9a`, `0x10bb0`). The "used for" column
is read from the surrounding calls for the first four and the last two;
the others are not yet read through.

### Calling convention and file helpers (seen)
- 16-bit **far calls**, arguments pushed right to left, caller removes them
  (`add sp, N`). Far pointers are pushed as segment then offset.
- Wrapper segment `0x175` (linear `0x11750` + offset):
  - `0x175:0x08` = `open(name_far, mode, &handle)`; returns 0, or 5 on carry.
  - `0x175:0x34` = `close(handle)`.
  - `0x175:0x53` = `read(handle, buffer_far, count, &bytes_read)`.
  - Read in `main` at `0x1002d` (open), `0x10047` (read), `0x10052` (close).
- `0x21c:0x675` = far `malloc` (called with `-0x24`, i.e. 65,500 bytes, six
  times at `0x1000f`, `0x1008e`, `0x1009e`, `0x100ae`, `0x100be`, `0x100ce`, then
  once with `0x900` at `0x100df`). `0x21c:0x662` = `free`. Inferred from use
  (allocation pattern, matching frees at `0x10b56`..`0x10b80`).
- `0x21c:0x15e` (linear `0x1231e`) = C runtime `execl(path, arg0, ..., NULL)`.
  It forwards to `0x21c:0x906`. Seen.

### Step 1: the speed-class byte (`RPG.LCP` offset 0)
`main` opens `rpg.lcp`, reads **one byte** into a buffer, and closes it.
If the byte is greater than 10 (`cmp ax,0xa; jg`, `0x10063`), it frees the
buffer and calls `execl("tv.exe", NULL)` (`0x10084`).
In our copy that byte is `0x02` (checked with Python on
`original_bin/RPG.LCP`), so `TV.EXE` never runs. This matches both traces.

### Step 2: `TV.EXE` is a first-run speed test (seen; purpose inferred)
(Addresses are Ghidra linear addresses in `TV.unpacked.exe`; its data segment
base is `0x12890`.)
- `main` `0x10000`: allocates seven buffers; if any fails it prints
  "Sorry ! Your memory is not enough to play game." and exits (flag at
  `0x10031`..`0x10132`, message at `0x128d2`, seen).
- Sets video mode 0x13, reads `number.lcz` (the font) and the 768-byte palette
  from `rpg.lcp`, draws a noise ("TV static") picture: for each pixel,
  `rand() < 3000` sets `0xFF` (`0x10274`..`0x10296`).
- Zeroes the BIOS tick counter `0040:006C` (`0x84:0x18`, at `0x10254`), runs its
  animation, then reads the tick counter (`0x84:0x08`, at `0x105cb`) into a
  variable `v`. The helper bodies are at `0x10848` and `0x10858` (seen).
- Classifies `v` (unsigned compares at `0x10604`..`0x1064b`; each later test
  overrides the earlier ones, so the final class is):

  | `v` (BIOS ticks, 18.2 per second) | class stored |
  |------|------|
  | `v <= 0x41` (65) | 0 |
  | `0x42`..`0x78` (66..120) | 4 |
  | `0x79`..`0xAA` (121..170) | 3 |
  | `0xAB`..`0x12C` (171..300) | 2 |
  | `0x12D`..`0x1F4` (301..500) | 1 |
  | `> 0x1F4` (500) | 0 |

- **Writes the class back into `RPG.LCP`:** opens `rpg.lcp` again (`ds:0xa6`,
  mode argument 1), writes **1 byte** from the class buffer (`0x10681`) and
  closes it, then frees all buffers and calls `execl("mark.exe", NULL)`
  (`0x1070f`, string at `ds:0xae`). So it is a one-time setup run that returns
  to `MARK`.
- The wrapper identities in `TV` (`0x9e:0x06` open, `0xa7` write, `0x32`
  close, `0x51` read) are inferred from matching call shapes with `MARK`'s
  wrappers; the mode value 1 meaning "write" is the usual DOS value, not
  separately verified in `TV`'s wrapper.

**Reading of the whole mechanism (inferred, medium confidence):** the speed
class (0..4) lives in byte 0 of `RPG.LCP`. Because `RPG.LCP` begins with the
interface palette, that byte is also colour 0's red value (6-bit scale), which
is why a small number (0..4) is invisible there. The shipped file has `0x02`.
`MARK` treats any value above 10 as "not measured yet" and runs `TV` once.
**Not yet found:** which program reads the class later and what it changes
(delays? animation speed?). `WALK0.SL` and `WALK.SL` name `rpg.lcp` 5 and 15
times, so they are the leads.

**Possible link to the crash on fast machines (hypothesis, not verified):**
the table maps both very fast and very slow results to class 0. If a modern CPU
makes `v` tiny the class would be 0, the same as "extremely slow".
Whether the program is CPU-bound in the measured stretch is not checked.

### Step 3: how MARK picks the next program
- `main` loop (`0x107a7` back-edge at `0x10b06`): each pass reads a key
  (`0x1e2:0x19c`, `0x1e2:0x1b1`) and tests scan codes `0x39` (Space) and `0x1c`
  (Enter) (`0x10a8b`, `0x10a99`; same pair at `0x104e9`/`0x104f7`), and, when a
  mouse was found at start-up (`[bp-0x34] != 0`), polls the mouse
  (`0xd2:0x7f`, linear `0x10d9f`, `mouse_poll` above, called at `0x10ad5`). A press sets the flag `[bp-0x28]` to 1
  (`0x10ab5`, `0x10b02`); it starts at 0 (`0x107c9`).
- After the loop (`0x10b09`): if a global byte at `[seg 0x1d0]:4` is non-zero
  (set at `0x1010c`, from the result of `0x112:0x06`), it calls
  `0x112:0x12a` and `0x112:0x152` before freeing the buffers
  (sound shutdown, **inferred** from position only).
- Then frees the allocated buffers (at least four frees seen, `0x10b56`..`0x10b80`;
  the rest were not read) and does
  `if (flag == 1) execl("walk0.sl") else execl("story2.sl")` (`0x10b88`..`0x10bbc`).
  So: no key pressed during the title wait -> intro slideshow; Space, Enter or
  a mouse click -> menu. This matches the two traced runs. Seen.

### What the "overlay" load really is
The saved traces log `Execute story2.sl 3` and `Execute mark.EXE 0` (the
first start from DOS). Type 3 is DOS EXEC "load overlay". The code at
`0x13990` is `mov ax, 0x4b03` followed by a far jump, in the C runtime's
`exec` path (the runtime is "MS Run-Time Library - Copyright (c) 1992", string
at `0x15b08`).

**Interpretation (medium confidence):** the `.SL` files are not overlays with
a call-and-return interface. `MARK` calls the C runtime's `execl`, which in
this runtime loads the new program on top of the old one with function 4B03h
and jumps into it. The old program is gone and never returns. Evidence for
"never returns": `STORY2.SL` and `WALK.SL` contain the string `mark.exe`, and
`WALK0.SL` contains `walk.sl`/`cast.sl`/`walk0.sl`, and `CAST.SL` contains
`walk0.sl`. That is exactly the shape of programs that chain to each other.
Not verified: the exact stub that `ljmp [0x170]` at `0x13993` reaches.

### The chain, from the programs' own strings
| Program | Names of other programs it can start |
|---------|--------------------------------------|
| `MARK.EXE` | `tv.exe`, `walk0.sl`, `story2.sl` |
| `TV.EXE` | `mark.exe` |
| `STORY2.SL` | `mark.exe` (restarts the title) |
| `WALK0.SL` | `walk.sl`, `cast.sl`, `walk0.sl` (itself) |
| `CAST.SL` | `walk0.sl` |
| `WALK.SL` | `ending.sl`, `mark.exe` |
| `ENDING.SL` | none found (only `ani.lcp`); its exit path is not known |

Strings only, from `tools/mz_strings.py` on the unpacked files. A name
being present does not prove the call is reachable.

### Consequence for the port
There is no overlay interface to emulate. Each program can become a **mode of
one new program** (title, intro, speed test, menu, cast, world, ending). The
`execl` calls become "switch to mode X". Shared state between them is limited
to files: every program re-reads its data, and the speed class lives in
`RPG.LCP`. (Inferred; if a program also passes arguments or environment
variables, this needs a second look. The `execl` calls seen pass no arguments
beyond the program name.)


---

## The speed class in WALK.SL, and the "486" display

Added 2026-10-14. Addresses are Ghidra linear addresses in
`work/unpacked/WALK.SL.exe` (the file is not packed; data segment base
`0x2cc60`, found as 8 bytes before the "MS Run-Time Library" string).
Tools: `tools/dis16.py` plus throwaway scans in `work/scratch/`
(`strrefs2.py`, `farptr.py`, `ticks.py`, `cpudetect.py`).

### Where the speed class is read (seen, high confidence on the read; medium on the meaning)
- `WALK.SL` `main` (`0x10000`) opens `rpg.lcp` (string `ds:0x67`, at `0x107e4`)
  and reads **`0x300` bytes** into the buffer `[bp-0x8c]:[bp-0x8a]` (`0x10808`).
  No seek happens between the open and the read, so byte 0 of that buffer is
  byte 0 of `RPG.LCP`, the speed class.
- At `0x109d3` it copies that **first byte** into the local `[bp-0x3a]`
  (`les bx,[bp-0x8c]; mov al,es:[bx]; mov [bp-0x3a],al`), then uses the buffer as
  a palette (`0x174d:0x33` copies `0x18` words, `0x1732:0x14e` =
  `set_palette(ptr, 0x100)`; the latter waits for retrace and writes the DAC
  exactly like `MARK`'s routine).
- Between `0x10808` and `0x109d3` no other instruction uses the buffer's own
  pointer variable. **Not checked:** writes through pointers derived from the same
  buffer (for example `[bp-0x50]` is `buffer + 0xfe78`).
- `[bp-0x3a]` ("class") is stored once (`0x109da`), cleared once (`0x1d3d7`,
  probably game start or a restart), and **read at six places**:

  | Address | What it does with `class` (seen) |
  |---------|----------------------------------|
  | `0x11336`, `0x11344` | `if (class < 2) { n = 2 - class; loop over n ... }`: extra work for classes 0 and 1 |
  | `0x14ac1`, `0x14acf` | the same pattern, a second copy of that code |
  | `0x135d5` | `counter = [bp-0x98]; if (counter > class) [bp-0x98] = 0;` a counter compared against the class |
  | `0x172bc` | `if (pass_counter > class + [bp-0x2e]) { pass_counter = 0; frame = (frame+1) mod 4 }`: steps a 4-frame animation, then jumps back to the top of the main loop (`jmp 0x11d36`) |

  `pass_counter` is `[bp-0x3c]`, incremented by one per loop pass at `0x172a4`
  (seen). The `0x172bc` block is the clearest: **the class is a frame
  divider.** The larger the class, the more loop passes pass between animation
  steps.

### Reading of the whole mechanism (inferred, medium confidence)
`TV` maps the measured tick count `v` to a class: a larger `v` means a slower
machine, and gets a smaller class (class 1 for `v` in 301..500, class 4 for
66..120). `WALK.SL` then divides its animation by that class. So **class is a
"machine is fast" rating (bigger = faster) and the game throttles itself with
it**; slower machines get less throttling or extra frame-saving work (the
`class < 2` branches).

**Why this matters for crashes on fast PCs (hypothesis, not verified):** `TV`
gives class 0 to any result of `v <= 0x41` (65 ticks, about 3.6 s), the same as the
slowest. On a very fast machine the game would be treated as the slowest
class, so it would get the least throttling while being the fastest. Two things
remain unchecked: whether `v` actually depends on CPU speed (the measured
stretch might be partly timed waits), and whether class 0 really leads to
unthrottled behaviour in `WALK.SL`.

Not looked at: whether `WALK0.SL`, `CAST.SL`, `STORY2.SL`, `ENDING.SL` or the
other 15 `rpg.lcp` uses in `WALK.SL` read the class; `WALK0.SL` reads the first
`0x300` bytes too, byte 0 use not checked.

### Other uses of the BIOS tick counter in WALK.SL (seen)
`read_ticks` (`0x25df2`) is called from three places: `0x1000d` (start of
`main`, result passed straight to `0x176b:0x2e6`; most likely `srand`), `0x12488`
(arithmetic on the tick value, then a call; looks like a time-derived random
parameter) and `0x134ba` (not read). None of the first two is a pacing delay.
In `MARK`, `read_ticks` (`0x10bec`) is called at `0x10743` and `0x108a5`; also
not read. `zero_ticks` is only used by `TV` (`0x10254`).

### "486": not CPU detection (seen), most likely game data (inferred)
- **No CPU-type detection found.** A byte scan of all seven programs
  (`cpudetect.py`: pushfd/popfd, the AC/ID flag masks, CPUID, `0xF000` flag
  tests) produced hits only in the common library region and each one I
  opened was a false positive (a `shl` operand, `and al,0xf; mov [0x3c],al`).
  No program contains the text `486`, `386`, `286`, `Pentium`, `CPU`, `MHz` or
  `speed`. A scan can miss obfuscated or indirect code, so this is "not found",
  not proof.
- **My own emulator profile is `cputype=486`** (`tools/run-original.sh` line 75).
  The coincidence with the number on the operator's screenshot is therefore
  explainable only if the game reads the CPU; the scan found no such code.
- **The value 486 is stored in the game data.** The 16-bit little-endian value
  `0x01E6` (bytes `e6 01`) sits in `__1.AL2` (the new-game template, offset
  5527) and in all seven existing `SAVEnn.001` files, always inside the same
  byte pattern:

  ```
  7c 23 09 00 00 | e6 01 | xx xx | 00 44 3e 0a 48 3e c0 00 ...
                    486     varies (2149, 2271, 2281)
  ```

  In `SAVEnn.001` the pattern starts 416 or 417 bytes before the end of the
  file; in the template at offset 5522. The second 16-bit field varies
  between saves (2149, 2271 and 2281 in the template); the first (486) never
  does in the files available. The runtime file `__1.NOW` has the pattern too,
  but with `0` in that field (it was modified by the load test).
- **Interpretation (inferred, medium-low):** "486" on the screen is probably
  this stored field, unchanged since a new game. What it means in the game
  (money? a year? a counter?) is unknown. **It has not been shown that the
  display reads this field.** The decisive test is to change those two bytes in
  a copy of a save and see whether the on-screen number changes.


---

## Title screen (MARK.EXE main loop, 0x107a7..0x10b06)

Added 2026-10-14. Verified against the original's machine code run in `tools/dosemu.py`
(see `port.md`, milestone 2). Linear addresses in `work/unpacked/MARK.unpacked.exe`.
Helper routines are the small hand-written ones in the 0x1076..0x1217 region.

### Helpers (all seen in disassembly, all have callers in `main`)
| Address | Behaviour | C equivalent (`src/gfx.c`) |
|---------|-----------|----------------------------|
| `0x10ce5` | LCZ chunk decompressor | `lcz_next_chunk` |
| `0x11fde` | byte copy (`memcpy`) | memcpy |
| `0x12007` | word copy | memcpy |
| `0x11856` | copy skipping source bytes equal to 0 (colour 0 transparent) | `gfx_overlay` |
| `0x11e2c` | draw `[w][h]` sprite to the screen, opaque | `gfx_draw_sprite` |
| `0x11eb7` | copy a rectangle out of a wider image into a packed buffer | `gfx_copy_rect` |
| `0x11be4` | fill a rectangle of the screen | `gfx_fill_rect` |
| `0x11077` | scroll a sprite up by n rows (bottom rows keep old data) | `gfx_scroll_up` |
| `0x12124` | rotate each row right by one pixel, with a one-byte offset quirk | `gfx_rotate_rows` |
| `0x12174` | move each byte one step toward a target (palette fade step) | `gfx_palette_step` |
| `0x11f72` | wait for retrace, upload `n*3` palette bytes to the DAC | `screen_set_palette` |
| `0x11fbc` / `0x11fd1` | read keyboard port `0x60` and acknowledge / flush BIOS key buffer | not needed |

**Quirk of `0x12124`:** the row windows are shifted by +1 byte from the true rows, and the
first window wraps in the byte just past the sprite, which the routine reads *and then
overwrites*. That byte is persistent state across calls. Reproduced on purpose in
`gfx_rotate_rows`; the visible effect is a one-pixel glitch column that moves.
Confirmed by a test that runs the C function and a register-by-register transcription of
the assembly through two successive calls on 48 sprite sizes.

### Data layout (checked by sizes, and by the pixel-exact match below)
| TITLE1 chunk | Decoded size | Meaning |
|--------------|--------------|---------|
| 0 | 13,404 | texture strip sprite, 67 x 200 (4 + 67*200) |
| 1 | 6,244 | banner sprite, 260 x 24 (4 + 260*24), icy brush texture |
| 2, 3, 4 | 4,489 each | **headerless** 67 x 67 masks (character shapes) |
| 5 | 2,688 | **headerless** 112 x 24 image (the 邦沛之謎 line) |
| 6 | 2,422 | not used by the title loop (loaded from a seek to `0x3e73`; role unknown) |

(CORRECTION of my earlier guess: chunks 2-4 are not frame overlays and chunk 5 is not a
sprite with a header. The first guess came from the sheet view only.)

### One pass of the loop (in order)
1. `passes += 1` (saturating at 0x2bc). At 699 (`0x2bb`): fade-out begins, "next = intro".
2. If a fade-out is running (counter < 64): count it up; when it reaches 64 the scene ends;
   otherwise move the working palette one step toward black and upload it.
3. If the fade-in is running (< 64): one step toward `ANI.LCP` palette 0, upload, count.
   Otherwise: rotate the banner one pixel, and while the bar counter is < 24 draw two
   40-pixel bars at row 104 (columns 58 and 222) with a colour that ramps down for 15
   passes and then up. Start colour `0x1f`.
4. If the BIOS clock word changed since the last pass: scroll the strip up one row and put
   the old top row at the bottom (wrap).
5. Compose and draw three windows: strip rows 0, 67, 133 (as offsets `4`, `0x118d`,
   `0x22d3`), mask laid over (colour 0 transparent), drawn at screen offsets `0x1a5d`,
   `0x1abd`, `0x1b1d` (row 21, columns 29, 125, 221). Compose the logo: the right 112
   columns (from column 148) of the banner, text laid over it, drawn at `0x70e8`
   (row 90, column 104).
6. If Space, Enter (scan codes `0x39`, `0x1c`) or a mouse button is down and no fade-out
   has begun: fade-out length := 64 - fade-in progress, fade-in := done, "next = menu".

### Palette setup (seen at `0x1073d`..`0x10851`)
The working palette starts as the DAC contents the mode switch leaves (all black here);
the fade-in target is `ANI.LCP` palette 0 (the first 768 bytes), the fade-out target is
a black palette. `ANI.LCP` palette 1 (bytes 0x300..0x5ff) is read but the title loop
does not use it in the path followed.

### Findings with consequences for the port
- **The title's length is pass-counted, not time-counted.** The timeout is 699 passes, one
  per loop. Only the fade-in is tied to the display (the palette upload waits for the
  vertical retrace). After the fade-in the loop has no wait; the strip scroll alone is tied
  to the 18.2 Hz BIOS clock. So on a faster CPU the title reaches pass 699 sooner. This
  is the first confirmed place where the game speed depends on the machine (seen in code;
  the real-time effect on hardware was not measured). The port uses a fixed 70 passes/s.
- **Skipped block `0x1040a..0x1053a`.** A second scene with its own key and mouse loop that
  saves the screen first. `main` skips it when byte `0x2ff` of `ANI.LCP` is `0x3f`
  (the shipped value): the jump at `0x101e5` goes straight to `0x10559`. It runs only when
  another program has left `0x3e` there. Not decoded; see open questions.
- **C runtime start-up measures the CPU.** At `0x1577f`..`0x157db` the C start-up code
  installs its own INT 8 handler, reprograms the timer, spins counting loop iterations
  between two timer ticks, and stores `(count + count/8) >> 10` at DS:`0x2e` and 7 times
  that at DS:`0x30`. This is an empty-loop calibration for a delay function, in the
  runtime and therefore in every program. Not yet traced to where the values are used.
- **Sound hardware probing.** The oracle logged 539 reads of port `0x388` (AdLib/OPL
  status) and 11 writes each to `0x388` and `0x389` during start-up: the music driver
  detects the FM chip.

### Open questions
1. What does the `0x3e` path (`0x1040a`) show, and who sets `0x3e` in `ANI.LCP`?
2. What are the values at DS:`0x2e` and DS:`0x30` used for?
3. `title1.lcz` chunk 6 (2,422 bytes): loaded after a seek to `0x3e73`, role unknown.
4. The mouse is never needed by the title (any button) but the driver is probed first.

## STORY2.SL: the intro slideshow (milestone 3, first read, 2026-10-15)

Unpacked image: `work/unpacked/STORY2.SL.unpacked.exe` (34,015 byte load image). `main` is
`0x10000` and is about 8 KB; the code before the C runtime (`0x13c5a` entry) is the whole
program. Method: Ghidra pseudo-code for the shape, `tools/calls16.py` for call arguments
(new tool, folds argument pushes into one line and resolves `DS:` pointers to strings), and
`tools/dosemu.py` to run it.

**Data segment** is at image offset `0x8080` (linear `0x18080`). The file-name table starts
at `DS:0x42` and holds, in order: `story2.sl`, the text `Memory is not enough !`, `muc.all`,
`ani.lcp`, `story0.lcz`, `story0_t.lcz`, `story0_1.lcz`, `story1.lcz`, `story1_2.lcz`,
`story1_t.lcz`, `story2.lcz`, `story2_1.lcz`, `story2_t.lcz`, `story3.lcz`, `story3_1.lcz`,
`story3_t.lcz`, `story4.lcz`, `story4_2.lcz` .. `story4_4.lcz`, `story4_t.lcz`,
`story5_1.lcz`, `story5_t.lcz`, `story6.lcz`, `story6_1.lcz`, `story6_t.lcz`, `mark.exe`,
`tab.tab`. (Seen: bytes dumped from the image. The voice files `STORYn_V` are not in this
table; they are probably named by the sound code in `0x1567d`-`0x158d4`, not read yet.)

**Start-up** (seen in `main`, `0x10000`-`0x10350`):
- Seven buffers of 65,500 bytes (`push -0x24` = 0xFFDC) and one of 0xC00 are allocated; if any
  allocation fails it prints `Memory is not enough !`.
- It opens `story2.sl` itself (a DOS handle check at `0x10142`), and reads `muc.all` from file
  offset `0x27137` (the seek wrapper `0x136af` takes handle, whence 0, offset low `0x7137`,
  offset high `2`), length `0x49a2`. That is 160,055 to 178,905 of the 188,188-byte file, so
  it lies inside it. It is the music data block. It also reads `ani.lcp` (0x600 bytes: the
  palette and what follows). CORRECTION: an earlier draft of this paragraph said offset
  `0x7137`; the high word `2` had been read as a fourth argument.
- Sets BIOS mode 0x13 via `0x1378f(0x13)` and clears with `0x13984`.
- Two flags are set from probe routines: `[5]` from `0x12fb2` and `[0xa]` from `0x1567d`.
  The emulator log shows port reads at 0x226-0x22e (a Sound Blaster DSP reset/read probe) and
  0x388/0x389 (AdLib), which is what these probes appear to be. In the emulator both fail and
  the program continues silently. Inferred from the port numbers; the routines are unread.

**Seven phases, one per slide.** After slide k (k = 1..6) the code writes k to a byte through
a far pointer and calls `0x1000_225a`; the last call before the `mark.exe` exec is the
runtime exit path. Each phase is a counted loop that breaks when the key scan code read by
`0x138b_0194` equals `0x39` (Space). That matches the observation that Space skips ahead.
Loop limits read from the pseudo-code (pass counts, not time): slide 0 is a state machine
that ends after 4000/600 cycles; then 320 (`0x140`), 500, a variable-length loop ending at
`0xa04b`, 6400 (`0x1900`), 2000 and 2000.

**Slide 0 (starfield)**: fully read and reproduced; see "Slide 0, verified" below. (An earlier
version of this paragraph called the stars "twinkling" and the subtitle colours a palette
fade; both were wrong, as the verified account shows.)

**Not yet read:** the routine `0x1293e` (a plain sprite blit), and the loops for slides 1 to 6
(since read; see the sections below). The voice playback path (`0x1567d` .. `0x158d4`, flags `[5]`
and `[0xa]`) was read on 2026-10-15: see `findings.md`, "the sound path of STORY2.SL". The voice
files are named by building the string `story` + digit + `_v` in the subtitle player
(`0x12364`..`0x123ae`), which is why they are not in the file-name table.

### STORY2.SL helper routines and slide 0 set-up (read 2026-10-15)

Calls are far calls; `seg:off` is converted to linear as `0x10000 + seg*16 + off`.

| Linear | Role | Status |
|--------|------|--------|
| `0x13f34` | `rand()`: seed (`DS:0x220`) = seed * 214013 + 2531011; returns bits 16..30 (constants `0x343FD`, `0x269EC3`; the `& 0x7fff` is `and ah, 0x7f` on the high word) | seen; this is the Microsoft C library generator |
| `0x13984` | memset (byte fill, word-wise) | seen |
| `0x139fa` | upload N palette entries (3 bytes each) from a far buffer to the VGA DAC, after waiting for vertical retrace; `0x3d8` writes are around it | seen |
| `0x138b4` | draw a `[w][h]` sprite to the screen at a given offset, row stride 320, no transparency | seen |
| `0x13a8f` | far memcpy in **words** (`rep movsw`, CX = word count); `0x13aac` is the byte version | seen (an earlier version of this table called it a byte copy, wrongly) |
| `0x12afb` | decode one LCZ chunk into a buffer (the equivalent of `MARK.EXE 0x10ce5`) | called with a file buffer and a destination; body not re-read, assumed same algorithm |
| `0x13bfc` | for N bytes, move each destination byte one step toward the source byte (`dl--` if above, `dl++` if below) | seen; used as the palette fade step |
| `0x12a02` | read the BIOS tick word `0040:006C` | seen |
| `0x121ce` | 64 outer steps, each preceded by a nested counter (inner 0..0xfe, outer 0..0x64) with no work in it, then `0x13bfc` over 0x2d0 (720) bytes, `0x139fa` upload of 0xf0 entries, and the sound poll `0x130ae` | seen; role "palette fade of entries 0..239" inferred from those calls |

- **Delays are CPU-bound busy loops, not clock waits.** Two kinds were seen: (a) the nested
  counter in `0x121ce` (about 25,755 empty iterations before each of 64 fade steps); (b) in the
  subtitle player `0x12738`-`0x12765` and `0x128ea`-`0x12917`, a loop of 20 (fade in) or 35
  (fade out) calls of `0x138b4` that all pass the same arguments, so the repeats draw the same
  strip again and change nothing on screen (they cost video-memory write time). Seen in the
  disassembly: the loop counter is not used in the arguments. These are what make a fade
  take a different time on different machines, and what the port replaces by fixed durations
  (`port.md`, "Timing policy").
- **Slide 0 set-up** (`0x1031c`-`0x10507`), buffer roles read from the allocation order:
  - A 768-byte zero buffer is built; the real palette (256 entries) is uploaded, then the
    zero palette is uploaded over entries 0..0xef, leaving entries 0xf0..0xff as in
    `ani.lcp`. Inferred effect: everything but the subtitle colours is black until the fade.
  - `story0.lcz` is decoded and 0x7d00 **words** (64,000 bytes) are copied to `A000:0000`.
    CORRECTION: I first read `0x13a8f` as a byte copy and wrote "0x7d00 (32,000) bytes" and a
    puzzle about the star picture covering half the screen. `0x13a8f` is `rep movsw`, so every
    count passed to it is in 16-bit words (`0x180` = 768-byte palette, `0xa0` = one 320-byte
    scan line, `0x960` = 4,800 bytes). There is no puzzle: it is a full-screen copy. The byte
    copy is `0x13aac`.
  - `story0_t.lcz` is decoded into a buffer zeroed for 0x1298 bytes (subtitle lines).
  - A save-under buffer holds two `[w=0x140][h=0xe]` headers, at offsets 0 and 0x1388,
    so the subtitle strip is 320 x 14 pixels per stored copy. Subtitle lines are 15 rows
    high in `STORY0_T`; the one-row difference is unexplained.
  - **Star table:** 20 entries of 3 bytes (offsets 0, 3, ..., 57): a 16-bit screen offset
    and one byte for the pixel that was under the star. Offset = (`rand()`, minus 0x7d00 if above
    it) * 2 + 0x320. The star is drawn with pixel value equal to its entry offset, so values
    0, 3, ..., 57. Seen in `0x104b8`-`0x10504` and the main loop; the palette entries those
    values index were not looked at.
  - **Seed:** `rand()` has 8 callers and `srand` (`0x13f22`) has none in this program, and
    the seed at `DS:0x220` is 1 in the load image (bytes `01 00 00 00`). So `rand()` gives
    the fixed sequence 41, 18467, 6334, 26500, 19169, ... (computed from the constants above,
    not yet compared with a program run). If nothing else consumes `rand()` first, the
    star positions are the same on every run, so slide 0 can be compared pixel for pixel with
    the oracle. Whether another caller runs before the star set-up is not checked.

### Slide 0, verified (STORY2.SL main loop `0x10590`..`0x10a56`, 2026-10-15)

The C scene `src/intro0.c` reproduces this and matches the original pixel for pixel (see
`findings.md`, "Slide 0 reproduced"). Everything below was read from the disassembly and then
confirmed by that comparison. Corrections to earlier notes are listed at the end.

Buffers (from the allocation order in `main`): **W** (the picture, 64,004 bytes with a
`[320][200]` header, in a 64 KB block), **temp** (4,800 bytes), **masks** (two `[320][14]`
sprites at offsets 0 and `0x1388`), **lines** (`STORY0_T` decoded, zero-padded to `0x1298`
bytes), the **star table** (20 entries of 3 bytes: a 16-bit position and one saved byte), and
palette buffers.

Set-up: screen := W's pixels; palette entries 0..239 black, 240..255 from `ani.lcp`;
the stars' start positions come from 20 `rand()` calls (`r` minus `0x7d00` if above it,
doubled, plus `0x320`); then a 64-step fade of entries 0..239 toward the `ani.lcp` palette
(`0x121ce`).

One loop pass, in this order:
1. Rotate W left by one pixel (`0x13b19`), copy W's top row to temp, scroll W up one row
   (`0x12f03`), copy temp to W's last row (`0xf8c4` past W's start, with W at offset 0).
2. For each star i: remember `W[pos]`, write `i*3` there. `pos` indexes the block from its
   first byte, header included, so the pixel is `pos - 4`.
3. Save W's rows 185..199 (`0xe744`, 4,800 bytes) to temp.
4. Subtitle state machine (below).
5. Copy W's pixels to the screen (this is where a pass becomes visible).
6. Restore the 15 rows, restore each star's pixel, then move each star: `pos -= 0x282`
   (16-bit); if the new value is below `0x282`, `pos = rand() / 103 + 0xf8c0` (the bottom row).
   Stars therefore drift up two rows and left two pixels per pass and the picture itself
   drifts up one row and left one pixel, so the stars move relative to it.

Subtitle state machine (a line is a 40-byte-wide, 14-row, 1-bit mask expanded to a
320 x 14 sprite with the set value `0x10`, from `lines + line_offset`):
- **state 1**: expand the next line into the mask not in use; `a = 0xf0`, `b = 0xfe`.
- **state 2** (15 passes): draw the new line in colour `b` and the old line in colour `a`
  (each twice, at x offset 0 and 1; the purpose, bolder text, is my inference); then `a++`, `b--`. The
  drawing order of the two lines swaps when `a >= b`. The colours are palette entries
  `0xf0` (white) .. `0xfe` (black) of `ani.lcp`, which are never faded, so the "fade" is the
  line's colour index stepping through a grey ramp. When `a` reaches `0xff`: take
  `hold_until = tick + 0x38` and, if `line_offset > 4000`, leave the loop at once (without
  copying W to the screen).
- **state 3**: draw the new line in `0xf0`; when the BIOS tick (16-bit) reaches `hold_until`,
  swap the masks, `line_offset += 600`, go to state 1.
Seven lines are shown (offsets 0..3600); the eighth read (offset 4200) is blank, so the last
real line is cross-faded to nothing and the loop ends. That is 8 cross-fades and 7 holds of
56 ticks; the loop ran 1,294 passes in the oracle at 3 passes per tick.

After the loop: a 64-step fade of entries 0..239 to black, then the slide 1 files are loaded.

Corrections to earlier notes on this program:
- The stars do not twinkle and no palette routine is involved; `0x12b42`/`0x12b53` are a
  16-bit read and write of far memory (the star table accessors).
- `0x13a8f` copies words (counts are word counts), so there is no "half screen" puzzle.
- The "state 'F'" in the decompiler output is a dead store; the assembly has no such state.
- The 14-row strip (not 15): the masks are 14 rows; the strip saved and restored is 15 rows.

### Slide 1, verified (STORY2.SL `0x10a59`..`0x10f9a` and the subtitle player `0x1225a`, 2026-10-15)

`src/intro1.c` reproduces this and matches the original pixel for pixel (see `findings.md`,
"Slide 1 reproduced"). All of it was read from the disassembly (`tools/dis16.py`,
`tools/calls16.py`) and then confirmed by that comparison. Buffer names follow the locals of
`main`: the screen is `[bp-0x20]` (A000:0000), not a work picture.

**Palettes.** All are slices of one block `P` (`[bp-0x3e]`): `ani.lcp`'s 0x600 bytes are read to
`P`; `[bp-0xa]` = `P+0x300`, `[bp-0x2c]` = `P+0x600`, black = `P+0x900`. The copies at
`0x102e8`/`0x102ff` make `[bp-0xa]` the **first** palette of `ANI.LCP` (bright, the normal
look) and `[bp-0x2c]` the **second** palette, bytes 768..1535 of `ANI.LCP` (dim). So
`ANI.LCP` holds two palettes (an earlier note said only "a palette and what follows"). Fades
move palette entries 0..239 only, so entries 240..255 (the subtitle ramp) always keep the first
palette's values.

**Set-up (`0x10a59`..`0x10c7d`).** Fade to black, then: decode `story1.lcz` (64,000 bytes, no
header) straight onto the screen; `0x137c8` saves the 230x28 screen rectangle at pixel `0x12c0`
(row 15, column 0) as a sprite (the "patch"); `story1_2.lcz` chunks go to cloud strip 0
(320x43, file offset 0), cloud strip 1 (320x39, file offset `0x958`, placed `0x36b0` after strip
0 in its buffer) and the face frames (chunk 2, file offset `0x1a33`, four headerless 123x96
frames; `0x10bcf` copies frame 2 over a fifth slot, so the cycle is 0 1 2 3 2). Then the sky is
drawn once (below), and the palette fades to the dim palette (64 steps) and then to the bright
one (64 steps).

**The sky, drawn on every pass (`0x10be6`..`0x10c43`).** A work buffer gets strip 0 (header
included, 13,764 bytes); strip 1's non-zero pixels go over it (`0x13732`, 12,480 bytes); the
patch goes over that, non-zero pixels only (`0x134b2`, stride 320, at pixel `0x12c0`); the first
13,760 bytes go to screen offset 0 (rows 0..42). The patch is a piece of the still picture, so it
keeps the cliff in front of the drifting clouds.

**One animation pass (loop `0x10c80`..`0x10dc0`, 320 passes).**
1. Counter `[bp-0x3a]` +1; above 5 it resets and cloud strip 0 rotates left one pixel (`0x13b19`).
   Every sixth pass.
2. Counter `[bp-0x2e]` +1; above 2 it resets and cloud strip 1 rotates left. Meant to be every
   third pass (see the keyboard quirk below).
3. The sky is drawn (above), then the current face frame is copied to screen offset `0x3b0f`
   (row 47, column 79), 123x96, no transparency (`0x1293e`).
4. Counter `[bp-0x40]` +1; above 10 it resets and the face offset grows by `0x2e20`
   (11,808, one frame); above `0xb880` it returns to 0. So each frame shows for 11 passes.
5. The keyboard port is read (`0x13a44`, `in al,0x60`). Scan code `0x39` (Space press) leaves the
   loop; otherwise `si++`, and the loop ends at 320.

**Keyboard quirk (found by the comparison, not by reading).** Step 5 stores the scan code in
`[bp-0x2e]`, the same byte as step 2's counter. With no key down the port reads 0 (in the
reference run), so the counter is 0 after every pass and becomes 1 at the next, never above 2:
strip 1 moves only on the first pass. If the port still holds a stale scan code of 2 or more
(for example the release code of the last key pressed before the program started), strip 1
moves on **every** pass. Neither is the evident intent (every third pass). The first pass moves
both strips because the two counters start at values slide 0 leaves in those stack slots (`0xef`
and `0x3c`). `intro1_create(…, faithful_keys)` reproduces the no-key behaviour for comparison;
the default is the intended every-third-pass.

**After the loop.** Fade to the dim palette (64 steps), then the subtitle player
`0x1225a(lines, strip, masks, length, voice-name buffer)`, then a fade to black.

**Subtitle player (`0x1225a`..`0x1293c`).** Per line (offset step 600; lines are 14 rows of 40
bytes, 560 of the 600 used):
- Saves screen rows 185..198 (`0xe740`, 320x14) as the background sprite `bg`; two mask
  sprites (320x14) share one buffer at offsets 0 and `0x1388`; the second starts all zero.
- Loads the line's voice clip (`story<n>_v`; clip offset and length come from 5-byte records
  in `tab.tab`, read at file offsets `0x3a7`, `0x3cf`, `0x3de`, `0x410`, `0x429`, `0x451` for
  slides 1..6; **not yet decoded**). This happens whether or not sound is present.
- Expands the line into the current mask (set bit -> `0x10`), then 14 passes with colours
  `a` = `0xf0`.. rising and `b` = `0xfe`.. falling: each pass copies `bg` to a work sprite,
  stamps the masks (two stamps each, at x offset 0 and 1; new line `b`, old line `a`, drawing
  order swapped once `a >= b`, same as slide 0), and the last pass (the 14th) redraws `bg` and
  the new line alone in `0xf0`. The work sprite is then drawn at `0xe740`, 20 times in a row
  (identical draws, a busy loop).
- Swaps the masks, then waits: with no sound card, until the BIOS tick is 74 above its value
  at the start of the wait or Space is hit (`0x12800`..`0x12847`); with a sound card, until the
  busy flag at DS:`0x19a` clears or Space is hit (`0x127a9`..`0x127eb`, not run).
- The line offset grows by 600; the player stops when it exceeds length - 300, so 8 lines for
  `story1_t.lcz`'s 4,800 bytes.
After the last line: 14 passes drawing only the last line with colours `0xf0`..`0xfd` (35 draws
per pass), then `bg` is drawn back at `0xe740`.

Helpers named here: `0x137c8` save a screen rectangle as a sprite; `0x13732` copy source bytes
that are not 0; `0x134b2` draw a sprite onto a buffer, non-zero pixels, with a stride;
`0x1293e` copy a w x h block to the screen with stride 320; `0x13a44` read the keyboard port
and acknowledge it; `0x13a59` clear the BIOS keyboard buffer.

**Not read:** the voice playback (`0x1567d`..`0x158d4`, `0x12fec`, `0x130ae`), the contents of
`tab.tab`, and slides 2..6.

### Slide 2, verified (STORY2.SL `0x10e61`..`0x11200`, 2026-10-15)

`src/intro2.c` reproduces this and matches the original pixel for pixel (`findings.md`,
"Slide 2 reproduced"). Same shape as slide 1; only what differs is listed.

**Set-up.** `story2.lcz` chunk 0 (file offset 0, 64,000 bytes) is decoded onto the screen;
chunk 1 (offset `0x5978`, 58,125 bytes) is three headerless 155x125 hair frames (`[bp-0x4c]`);
chunk 2 (offset `0xb44c`, 19,375 bytes) is one more hair frame (`[bp-0x12]`); `story2_1.lcz`
chunk 0 is four headerless 75x78 figure frames (`[bp-0x24]`). Before any fade, hair frame 0 is
drawn at screen offset `0x5e65` (row 75, column 165) and figure frame 0 at `0x334f` (row 41,
column 15) with `0x1293e`. Then the same two palette fades as slide 1 (dim, then bright).

**One animation pass (loop `0x10f9d`..`0x11155`, 500 passes).**
1. Hair counter `T` (`[bp-0x2e]`) +1; above 90 it resets to 0 (cycle of 91 passes).
2. Figure counter (`[bp-0x40]`) +1; above 20 it resets and the figure offset grows by
   `0x16da` (5,850, one frame); above `0x448e` (3 frames) it returns to 0. Each frame shows for
   21 passes.
3. The current figure frame is drawn at `0x334f` every pass.
4. The hair frame, by `T`: below 15, chunk 2's frame; 16..29, frame 1; 31..44, frame 2;
   46..59, frame 1; 61..74, frame 0; 76..89, frame 1. `T` = 15, 30, 45, 60, 75 and 90 draw
   nothing, so the frame stays. (Frame 1 at `+0x4baf`, frame 2 at `+0x975e`.)
5. The keyboard port is read into `[bp-0x3a]` (not a counter here, unlike slide 1); Space ends
   the loop, otherwise `si++` up to 500.

**Values carried over from slide 1** (stack slots no code resets): the figure counter starts at
1 (slide 1's counter after 320 passes is 320 mod 11) and `T` starts at 0 (the last scan code
read, none down). A skipped slide 1 would leave other values; only the length of the first
figure frame and the phase of the hair cycle would differ. The port uses 1 and 0.

**Then** fade to the dim palette, the subtitle player on `story2_t.lcz` (1,800 bytes: three
lines, the player stops when the line offset passes 1,500), fade to black. The player was
written for slide 1 and works unchanged (`src/subs.c`).

**Not read:** the voice playback, `tab.tab`, slides 3..6.

### Slide 3, verified (STORY2.SL `0x111fe`..`0x11466`, 2026-10-15)

`src/intro3.c` reproduces this and matches the original pixel for pixel (`findings.md`, "Slide 3
reproduced"). Different from slides 1 and 2: no fixed pass count and no cloud/hair counters.

**Set-up.** `story3.lcz` chunk 0 (64,000 bytes, no header) is the picture `BG` (`[bp-0x4c]`).
`story3_1.lcz` chunk 0 (24,028 bytes) is a **168x143 sprite with header** (`[bp-0x12]`, the
figure); its chunk 1 (330 bytes, file offset 13,556) is three headerless **10x11 sparkle
frames**, decoded to 25,000 bytes (`0x61a8`) after the figure in the same buffer. `BG` is
copied to the screen (`0x7d00` words) and the figure is drawn over it with `0x138fd` (masked
sprite blit to the screen, 0 transparent) at `0x478c` (row 57, column 76). Then the two palette
fades (dim, bright).

**One animation pass (loop `0x1132e`..`0x11457`).** State: sparkle frame offset (`[bp-6]`, 3
frames of 110 bytes), figure offset `di` (starts `0x478c`), a "half" flag (`[bp-0x40]`), an update
counter (`[bp-0x2e]`, starts 0, set explicitly), a scroll offset `[bp-0x4e]` (bytes, starts 0) and
word count `[bp-0x30]` (starts `0x7d00`).
1. The current sparkle frame is drawn **into `BG`** at `0x57a` (row 4, column 122) and `0x6fb`
   (row 5, column 187) (`0x135e4`: headerless block, 0 transparent, stride 320). The sparkles
   therefore stay in `BG`; the frame changes every pass (0, 1, 2, 0, ...).
2. `BG`'s first `64000 - shift` bytes are copied to the work picture `W` (`[bp-0x24]`) starting
   `shift` bytes in. The top `shift/320` rows of `W` are not rewritten, so they keep what an
   earlier, smaller shift put there: a copy of `BG`'s first row. The picture scrolls down and its
   top row is stretched upwards.
3. The figure is drawn into `W` at `di` (`0x134b2`, masked, stride 320).
4. `W` goes to the screen (`0x7d00` words).
5. The half flag: 0 -> 1 on the first pass, and on the second pass (flag 1) it resets and
   everything advances: `di += 320`, the figure's **height byte is decremented** (so the sprite
   shows one row fewer, its bottom rows are cut off and it sinks), the update counter +1; when it
   passes 5 it resets and `shift += 320` (one row). So the figure moves every second pass and the
   picture scrolls every twelfth.
6. Keyboard: the scan code goes to `[bp-0x3a]` (no counter is clobbered). Space ends the loop;
   otherwise it continues while `di < 0xa04c`.
The figure needs 71 updates to reach `0xa04c` from `0x478c`, so the loop ends after 142 passes
(11 scroll steps, a shift of 3,520 bytes, figure height 143 -> 72).

`W`'s contents before the first pass are leftovers (slide 2's figure frames), but the first pass
overwrites 64,000 of its bytes and nothing reads the rest, so they do not matter.

**Then** fade to dim, the subtitle player on `story3_t.lcz` (6,000 bytes: ten lines), fade to
black. New helpers named here: `0x138fd` masked sprite blit to the screen; `0x135e4` masked
headerless-block blit to any buffer.

**Not read:** the voice playback, `tab.tab`, slides 4..6.

### Slide 4, verified (STORY2.SL `0x1146b`..`0x11bf3`, 2026-10-15)

`src/intro4.c` reproduces this and matches the original pixel for pixel (`findings.md`, "Slide 4
reproduced"). The most intricate slide so far: a long eye sequence driven by one counter, and a
particle effect that uses the random generator.

**Set-up.** `story4.lcz` chunk 0 (64,000 bytes) goes onto the screen. `story4_2.lcz` chunk 0
(54,747 bytes) is **seven 79x99 frames** (`[bp-0x4c]`, "eyes A", frame size 7,821 = `0x1e8d`);
`story4_3.lcz` chunk 0 (31,284 bytes) is **four 79x99 frames** (`[bp-0x12]`, "eyes B");
`story4_4.lcz` chunk 0 (8,740 bytes) is **twenty 23x19 frames** (`[bp-0x24]`, frame size 437 =
`0x1b5`): two banks of ten, bank 1 at `+0x1112`. (An earlier table in `formats.md` said 23x20,
19 frames; the draw calls settle it.) `0x137c8` saves the **100x81 screen rectangle at offset 187**
(row 0, column 187) as a sprite into the big buffer `C` (`[bp-0x34]`, the one that later holds the
voice file name). `C` also holds the particle table, at `C+9000`: ten 5-byte entries (word
position, word frame offset, byte bank flag). Then the two palette fades (dim, bright).

**Particles (a sparkle).** Initial entry `i` (`0x11662`): `a = rand()/432`, `b = rand()/529`
(integer quotients: 0..75 and 0..61), position `b*100 + a + 4` (an index into the 100-wide
sprite, header included); frame offset `(i+1) * 437`, wrapped to 0 at 4,370; bank flag
`rand()/0x4000` (0 or 1). That is 30 `rand()` calls, in the order a, b, flag per entry.

**Particle update, every 47th pass** (`[bp-0x14]` counts, resets above 46): the working sprite
`W2` (`[bp-0x48]`, 100x81) is set to the saved background (`0x13a8f`, `0xfd4` words); for each
entry the 23x19 frame is drawn into `W2` at its position (`0x135e4`, stride 100, 0 transparent) from
bank 0 if the flag is non-zero, else bank 1; the frame offset grows by 437; when it reaches
`0x1112` the entry is re-placed (the same three `rand()` calls) with frame 0. After the ten
entries `W2` is drawn to the screen at 187, opaque (`0x138b4`).

**The eye sequence.** One pass counter `si` (the loop's `si`), a blink counter (`[bp-0x40]`,
+1 per pass, wraps when it passes 60, a cycle of 61), and two pauses. Per pass the eye frame
(79x99 at screen offset `0x2d50`, row 36, column 80, no transparency) is:

| `si` | frame |
|------|-------|
| < 700 | A0 |
| 701..1199 | A1 |
| 1201..1699 | A2 |
| 1701..2399 | A3 |
| 2401..3099 | A4 |
| 3101..3799 | A5 |
| 3800 | (nothing drawn; the pause counters are reset) |
| 3801 (the **first pause**) | `si` is pushed back to 3800 every pass, so it lasts until its counter is done: 1,001 passes with the pause counter below 500 show A5 while the blink counter is below 30 and A6 otherwise, then A6 only; the pause counter wraps after 1,001 passes and the pause ends after the second wrap (2,002 passes) |
| 3802..4599 | B0 |
| 4600 | (nothing drawn; pause counters reset) |
| 4601 (the **second pause**) | the same, with B0/B1 for A5/A6 and three wraps (3,003 passes) |
| 4602..5499 | B2 |
| 5501..6399 | B3 |

Frames at the exact boundary values 700, 1200, 1700, 2400, 3100, 5500 are not drawn (the
comparisons are strict), so the previous frame stays. The loop ends when `si` reaches `0x1900` =
6,400, which with the pauses is **11,401 passes**.

**Values carried over from earlier slides** (stack slots no code resets): the particle counter
starts at 3, the state slide 0's subtitle machine ends in; the blink counter starts at 0, slide
3's every-second-pass flag after its 142 passes. The random generator is **not reset**: its
state at this point is whatever slide 0 left. In the original that depends on how many passes
slide 0 ran (its loop is unpaced), so sparkle positions differ from machine to machine; the
reference run read it out (`dosemu.py --peek 11659:182a0:4`, a 32-bit state) and the C side
starts from it (`--rand-state`).

**Then** fade to dim, the subtitle player on `story4_t.lcz` (3,000 bytes: five lines), fade to
black.

**Not read:** the voice playback, `tab.tab`, slides 5 and 6.

### Slide 5, verified (STORY2.SL `0x11c85`..`0x11e69`, 2026-10-15)

`src/intro5.c` reproduces this and matches the original pixel for pixel (`findings.md`, "Slide 5
reproduced"). The simplest slide.

**Set-up.** `story5_1.lcz` chunk 0 (file offset 0, 64,000 bytes) is decoded onto the screen;
chunk 1 (file offset `0x9aed`, 24,444 bytes) is **three headerless 97x84 frames** (8,148 =
`0x1fd4` each; `[bp-0x4c]`). `0x11d22` copies frame 1 into a fourth slot (`+0x5f7c`), so the
cycle is 0 1 2 1. (The file has trailing bytes after chunk 1 that do not form a chunk; the
program reads chunk 1 from a fixed offset and never looks at them.) Then the two palette fades.

**One animation pass (loop `0x11d5b`..`0x11dc8`, 2,000 passes).** Counter `[bp-0x2e]` +1; above 150
it resets and the frame offset grows by `0x1fd4`; above `0x5f7c` it returns to 0. So a frame stays for
151 passes. The current frame is drawn at screen offset `0x28c0` (row 32, column 192), 97x84, no
transparency, every pass. Space (scan code `0x39`) ends the loop.

The counter's initial value is whatever slide 4 left in that stack slot: slide 4 uses it as a
temporary for the sparkle bank flag, so it is 0 or 1 depending on the random numbers (0 in the
reference run). The port starts at 0; the only effect of a 1 would be a first frame one pass shorter.

**Then** fade to dim, the subtitle player on `story5_t.lcz` (4,800 bytes: eight lines), fade to black.

**Not read:** the voice playback, `tab.tab`, slide 6.

### Slide 6, verified (STORY2.SL `0x11e69`..`0x12199`, 2026-10-15)

`src/intro6.c` reproduces this and matches the original pixel for pixel (`findings.md`, "Slide 6
reproduced"). The last slide.

**Set-up.** `story6.lcz` chunk 0 (64,000 bytes) is decoded onto the screen. `story6_1.lcz` has three
chunks of 30,240 bytes (file offsets 0, 16,820, 32,142), each **one headerless 189x160 hair frame**,
decoded to `[bp-0x4c]` (H0), `[bp-0x12]` (H1) and `[bp-0x24]` (H2). Frame H0 is drawn at screen offset
`0x325a` (row 40, column 90) before the fades. Then the two palette fades.

**One animation pass (loop `0x11f93`..`0x12068`, 2,000 passes).** A counter `[bp-0x2e]` +1; above 50 it
resets and a period count `[bp-0x4e]` (`k`, starts 0) +1, so a period is 51 passes. Then:
1. counter < 25 and `k` < 10: draw H0;
2. counter > 25 and `k` < 10: draw H2 (at exactly 25 nothing is drawn, the previous frame stays);
3. `k` > 9 (it reaches exactly 10): draw H1, and when the counter is above 24, `k` = 0.
All draws are 189x160 at `0x325a`, no transparency. So for ten periods the hair alternates between
H0 and H2 every 25 passes, then the eleventh period shows H1 until its counter passes 24 and the
alternation starts again. The counter's start is the value slide 5 leaves: 2000 mod 151 = 37.
Space ends the loop.

**Then** fade to dim, the subtitle player on `story6_t.lcz` (6,600 bytes: eleven lines), fade to
black, then (`0x12111`..`0x121c6`) stop the sound, free the buffers and **`exec` `mark.exe`**: the
title starts again, as the first traces showed.

**Not read:** the voice playback, `tab.tab`, and the code that frees buffers and starts `mark.exe`
(only its shape).
