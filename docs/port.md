# Native port: the vertical slice

The reconstruction lives in `../src/`, built with `../Makefile`. It is a clean-room
C99 program written from observed behaviour (see `findings.md`, `formats.md`,
`code.md`), not from pasted decompiler output. It needs the user's own copy of the
game data; no game files are in the repository.

## Build and run
    make            # builds build/3k (needs a C99 compiler, a C++14 compiler for ymfm, and sdl2-config)
    make test       # headless tests, uses original_bin/ if present
    build/3k --data original_bin [--scale N] [--ppm FILE]

- `--data DIR` or `TK_DATA`: where the original files are (default `original_bin`).
  Lookup is case-insensitive in effect: the original asks for `title1.lcz`, the
  disk has `TITLE1.LCZ`.
- `--ppm FILE`: write the screen to a PPM file and exit, no window. Used to compare
  with reference renders.
- Escape or closing the window quits.
- Toolchain used: Apple clang 21 (arm64), SDL2 via Homebrew (`sdl2-config` reports
  2.32.74, which is the `sdl2-compat` layer on top of SDL3). Window creation worked;
  nothing else of SDL was exercised yet.

## Layout
| File | Role |
|------|------|
| `src/lcz.[ch]` | LCZ chunk decoder (escape-coded RLE). Same algorithm as the original's routine at `MARK.EXE 0x10ce5`. |
| `src/assets.[ch]` | Finds and reads data files; case-insensitive lookup. |
| `src/screen.[ch]` | 320x200 8-bit framebuffer plus 6-bit VGA palette; RGB conversion (`v*255/63`); PPM writer. |
| `src/platform.h`, `src/platform_sdl.c` | The only code that uses SDL: window, present, events, time. |
| `src/main.c` | Entry point. Currently shows `gmain.lcz` with the `rpg.lcp` palette. |
| `src/opl.h`, `src/opl.cpp` | C interface to the OPL2 chip (the only C++ in the project, apart from ymfm). |
| `src/music.[ch]` | CMF song to OPL2 register writes, rendered with ymfm; loops. See "Milestone 5". |
| `third_party/ymfm/` | ymfm, unmodified, BSD-3-Clause; `PINNED.txt` names the commit. |
| `tests/test_core.c` | Headless tests: synthetic chunk, malformed chunks, palette scaling, chunk counts and sizes of three real files. |

The game logic never touches SDL; `screen_t` is the boundary. That keeps the
logic testable without a window and replaceable if SDL is ever dropped.

## Design decisions so far
- **Plain linear 320x200x8 screen**, no planes: the original runs in BIOS mode 13h
  (`findings.md`, CORRECTION on Mode X).
- **Chained programs become modes of one program.** The original replaces itself with
  `execl`; the port will switch mode inside one process. The one-byte message that
  `MARK` leaves in `ANI.LCP` (byte `0x2ff`) becomes an ordinary variable. Never write
  to the user's data files.
- **Fixed-rate timing** is planned instead of the original's machine-speed class
  (`code.md`, speed class). Not built yet.

### Timing policy (decided 2026-10-15, user's choice)
The port uses **fixed durations**, never a CPU-speed or emulator cycle setting.
- **Waits that the original paces with the BIOS clock keep their exact length**, because
  they are already real time: one tick = 65536 / 1193182 s = 54.925 ms (18.2065 Hz). Without
  sound hardware a voice/subtitle line lasts 74 ticks = 4.064 s, and a slide 0 line 56 ticks
  = 3.076 s (`findings.md`, unattended timeline). With sound, a line should last as long as its
  clip; the code for that branch is not read yet.
- **Parts the original runs as CPU-bound loops** (animation passes, repeated blits) get a
  fixed duration chosen by eye and kept as named constants in one place (`src/timing.h`, to
  be created with the first scene that needs it), so they can be retuned without touching
  scene logic. No measured value exists for the original's intended duration; the first
  values are guesses and will be marked as such.
- The scene code advances by explicit steps with the clock passed in (as `title.c` does), so
  tests can run it faster than real time.
## Milestones
| # | Goal | Status |
|---|------|--------|
| 1 | Decode a full-screen picture and show it with the right palette | **done 2026-10-14** |
| 2 | `MARK.EXE` title: palette fade, logo pieces, input, Space/Enter/mouse choice | **done 2026-10-14** (see below) |
| 3 | `STORY2.SL` intro: slide order, timing, voice, subtitles | **in progress** (2026-10-15): slides 0-6 (the whole intro) done and match the original (see below); voice-over and slide 0's sound built 2026-10-15, voice-over and subtitle sync confirmed by the operator by listening (see "Sound"); FM music built, not yet heard (see "Music") |
| 4 | Chain the modes: title -> intro -> title, or title -> menu | not started |
| 5 | Music (`MUC.ALL`, FM) | **built 2026-10-15, awaiting the operator's ears**: ymfm OPL2 + CMF player, plays under slides 1-6 of `--scene intro` (see "Music") |

### Milestone 1 evidence
- `make test`: 0 failures, also clean under `-fsanitize=address,undefined`.
- `build/3k --ppm` output for `gmain.lcz` + `rpg.lcp` is byte-identical (192,015
  bytes) to an independent render from `tools/lcz_decode.py`. Visual check: the
  "Ready" screen with the stone frame and F-key buttons.
- The window opened and stayed up for 4 seconds without a crash. **No human has yet
  looked at the live window**; only the PPM/PNG output was inspected.

## Known gaps and risks
- The title uses small pieces, not one picture: `TITLE1.LCZ` is read in two parts,
  `0x3118` bytes (chunks 0-1) and `0xd5b` bytes (chunks 2-5), plus chunk 6 after a
  seek to `0x3e73`. Frame sizes inside these chunks are still unknown.
- The title animation (64-step palette fade, tick-driven sprite updates, several draw
  routines at `MARK.EXE 0x11a76`, `0x11d40`, `0x11d91`, `0x11eb7`, `0x11be4`...) has
  not been read yet.
- Sound: `MARK` loads blocks of `muc.all` for an FM music driver. OPL emulation is a
  separate decision (library choice and licence); milestones 2-4 run silent.
- Aspect ratio: a DOSBox capture had aspect about 1.6; the port scales uniformly.

## Milestone 2: the title screen

`build/3k` (default scene) now plays the real title: a 64-step palette fade-in, the three
brush characters 三 界 諭 as textured windows scrolling upward, the 邦沛之謎 line sliding
along the orange bars, and a fade-out after 699 passes (to the intro) or when Space, Enter
or a mouse button is pressed (to the menu). At the end it prints which scene would come next;
those scenes are not built yet, so the program then exits.

Source: `src/title.[ch]` (the scene), `src/gfx.[ch]` (drawing routines). The scene advances
one pass per call with the clock and input passed in, so it runs in real time or stepped.

### The oracle: how it was checked
`tools/dosemu.py` is a small DOS emulator, built on the Unicorn CPU library, that runs the
original unpacked program, answers its DOS/BIOS/port requests, and saves the screen and the
palette on demand. It only knows what these programs ask for; anything else is listed in its
report. It serves files from a copy of the data directory (`work/emu_data`), so the original
files are never written. Needs `unicorn` in `work/venv` (building it needs `cmake` and
`pkg-config`; see `environment.md`).

To make a comparison fair, the emulator counts the title loop's passes and drives the BIOS
clock from the pass count (one tick per 3 passes), and the C port uses the same model.

    # reference frames from the original (pass 0, 100, 300, ...)
    work/venv/bin/python tools/dosemu.py work/unpacked/MARK.unpacked.exe --ticks 5000 \
        --loop-top 0x107a7 --tick-div 3 --sync-var 0x16 \
        --snap-pass 100:work/oracle/p0100.raw --snap-pass 300:work/oracle/p0300.raw
    # the same frames from the C port
    build/3k --data original_bin --step-dump 100:work/cport/p0100.raw --step-dump 300:work/cport/p0300.raw
    # compare
    python3 tools/compare_frames.py work/oracle/p0100.raw work/cport/p0100.raw

Add `--key-pass 100:0x39` (oracle) and `--press-pass 100` (C) to hold Space from pass 100.

### Evidence
- 14 frames without input and 8 with Space held: **0 differing pixels, 0 index and 0 palette
  differences**. Exit decision and pass count for no key (intro, 762), Space at 10 (menu, 22)
  and Space at 100 (menu, 165) equal the original's.
- Negative controls: unrelated passes differ by 1,083 to 2,899 pixels; one flipped pixel is
  detected.
- `make test`: gfx primitives, including a 48-size comparison of the rotate routine with
  a register-level transcription of the original's assembly, over two successive calls.
- The window opened and ran for 6 seconds with no crash. **Not yet looked at by a human.**

### Not covered
- The mouse path has not been compared (the oracle reports no mouse); the C port accepts a
  mouse button as a press.
- Passes between the sampled ones, and the real-time feel. The real-time loop uses a fixed 70
  passes per second, an assumption; the original's real rate was never measured.
- Sound. The original starts music at the title (it loads blocks of `muc.all`).
- The `ANI.LCP` message byte: the C port does not write to data files, as decided.

## Milestone 3, slide 0: the star field with subtitles

`build/3k --scene intro0` plays slide 0 of the intro: the picture drifts, 20 stars drift
faster past it, and the seven opening lines of narration cross-fade at the bottom, each
shown for 56 BIOS ticks (3.08 s). The scene is `src/intro0.[ch]`; its helpers are
`gfx_expand_bits`, `gfx_rotate_rows_left` and `gfx_stamp` in `src/gfx.c`, the C library
random generator in `src/tkrand.[ch]` (seed 1, as in the original), and the pacing constants in
`src/timing.h`. After the last line it fades to black and prints that the next slide is not
built yet. With a sound device it also plays `STORY0_1` (see "Sound" below); without one, or with
`--no-audio`, it is silent as before.

    build/3k --scene intro0 --data original_bin           # in a window
    # the same comparison as for the title, on the original's loop at 0x10590:
    work/venv/bin/python tools/dosemu.py work/unpacked/STORY2.SL.unpacked.exe \
        --ticks 100000 --loop-top 0x10590 --tick-div 3 --snap-pass 100:work/oracle/p0100.raw
    build/3k --scene intro0 --data original_bin --tick-div 3 --step-dump 100:work/cport/p0100.raw
    python3 tools/compare_frames.py work/oracle/p0100.raw work/cport/p0100.raw

Pass counts in `--step-dump` count main-loop passes only: the 64-step fade-in is run first
and not counted. With `--snap-pass` the emulator stops after the last snapshot only if the
loop is still running then: the loop has 1,294 passes, so a snapshot past that never fires
and the emulator runs on until `--ticks` (a run to 100,000 ticks took 30 minutes). Keep the
last snapshot below 1,294 or lower `--ticks`.

### Evidence
197 snapshots (every 7th pass to 1400 plus every pass from 1280 to 1330) are identical in
pixels, indices and palette; the loop length (1,294 passes) is the same on both sides.
Negative controls, unit tests, mutation checks and the sanitizer run are in `findings.md`.

### Not covered / known gaps
- Real-time pacing is a guess (35 passes/s, fades at 70 steps/s). **Update 2026-10-15: the
  operator ran `build/3k --scene intro0` and judged the drift speed and cross-fade length
  right ("looks and feels great"), so `TK_INTRO0_PASS_HZ` = 35 and `TK_FADE_STEP_HZ` = 70 are
  now accepted values, still not measured against the original.**
- The fade-in and fade-out were not compared with the original.
- Only the 3-passes-per-tick clock model was compared.
- Space, Enter and mouse clicks are ignored here, as in the original's slide 0 (its loop
  reads no keyboard); later slides read Space.
- Sound: none.

## Milestone 3, slide 1: the cliff scene with the face animation

`build/3k --scene intro1` plays slide 1 (`build/3k --scene intro` plays slides 0 and 1 in a row):
fade to the dim palette and then the bright one; 320 passes of drifting clouds and a four-frame
face; fade to the dim palette; eight narration lines cross-fading at the bottom, each held for 74
BIOS ticks (4.06 s) or until Space; fade to black. Space skips the animation loop or a line's
hold, as in the original (Enter and the mouse do nothing here). The scene is `src/intro1.[ch]`;
new helpers are `gfx_blit_masked` and `gfx_blit_rect`; the new platform call is
`platform_space_hit`.

    build/3k --scene intro1 --data original_bin
    # the comparison with the original (reference side from `docs/findings.md`):
    work/venv/bin/python tools/dosemu.py work/unpacked/STORY2.SL.unpacked.exe --ticks 20000 \
        --snap-hit 1250e:50:work/oracle1s/s050.raw
    build/3k_headless --scene intro1 --faithful-keys --dump sub:50:work/cport1/s050.raw
    python3 tools/compare_set.py work/oracle1s 's{}.raw' work/cport1 's{}.raw' 050

`--faithful-keys` reproduces how the original paces the second cloud strip when no key is down
(it moves only on the first pass). Without it the strip moves every third pass, which looks like
the intent. **Decision for the operator:** keep the default, or pick the original's behaviour.

### Not covered / known gaps
- Real-time pacing: **the operator ran `build/3k --scene intro1` (2026-10-15) and judged the
  animation "as intended" and the subtitle fades right**, so 35 passes/s (animation and subtitle
  passes) and 70 steps/s (fades) are accepted values, still not measured against the original.
  The operator did not ask for `--faithful-keys`, so the default (second cloud strip every third
  pass) stays.
- The final fade to black was not compared with the original.
- Silent: the voice clips (`STORY1_V`) are not played. With a sound card the original waits for
  each clip, not 74 ticks, so line timing will change when voice is built.

## Milestone 3, slide 2: the road through the cliffs

`build/3k --scene intro2` plays slide 2: the same fades as slide 1; 500 passes in which a small
figure walks the cliff path (four frames) and a woman's hair moves in the wind (a 91-pass cycle
through four frames); three narration lines; fade to black. `build/3k --scene intro` plays
slides 0 to 2 in a row. The scene is `src/intro2.[ch]`. The subtitle player is now shared
(`src/subs.[ch]`), as are the fade helpers (`src/slide.[ch]`) and the driver in `main.c`.
Slides 3-6 plug in by adding a `slide_ops` to the `slides[]` table.

    build/3k --scene intro2 --data original_bin
    tools/check_intro2.sh        # compare with the saved reference frames (needs work/oracle2*)

### Not covered / known gaps
- Real-time pacing: **the operator ran `build/3k --scene intro2` (2026-10-15) and judged the
  slide good**, so 500 passes at 35 per second is accepted (still not measured against the
  original). The final fade to black was not compared with the original; silent (no voice-over).

## Milestone 3, slide 3: the sinking figure

`build/3k --scene intro3` plays slide 3: the same two fades; a picture that scrolls slowly
down (its top rows stretch), two sparkles twinkling in the sky, and a large figure that sinks
out of sight over 142 passes (8.4 s at 17 passes per second); ten narration lines; fade to
black. `--scene intro` plays slides 0 to 3 in a row. The scene is `src/intro3.[ch]`.

    build/3k --scene intro3 --data original_bin
    tools/check_intro3.sh        # compare with the saved reference frames (needs work/oracle3*)

### Not covered / known gaps
- Real-time pacing (accepted by the operator 2026-10-15 after the pacing fix below): first tried at 35 passes per second, which the operator found too fast; now
  17 per second (half speed, set by eye, 2026-10-15). The operator then reported that the first
  seconds still felt fast and slowed down after the fade-in. **Cause found, not the rate:** see
  the pacing fix below. **The operator has not yet confirmed the result.** If it is still off,
  change `TK_INTRO3_PASS_HZ` in `src/timing.h`.
- The final fade to black was not compared with the original; silent (no voice-over).

### Real-time pacing fix (2026-10-15)
The operator reported that slide 3's first few seconds felt fast and that it slowed down after
the fade-in. `TK_PACE_LOG=FILE build/3k --scene intro3` (new; one line per step with its time and
how late it ran) showed why. The window presents with vertical sync, and on this display a fade
step took 16.7 ms against its 14 ms slot (70 steps per second), so each of the 128 fade steps ran
a little late and the lag grew to 328 ms. The loop then paid the backlog back: the first passes
of the animation ran every 17 ms instead of every 58 ms (about eight passes in a row, `late`
falling 328, 287, 246, 204, 163, 122, 80 ms) until it caught up. This affected every slide that follows a fade,
but at 35 passes per second (28 ms slots) it was much less noticeable.

Fix (`next_due` in `src/main.c`, used by every real-time loop): a step that runs late is no
longer made up for. The schedule carries at most one step of lateness; a larger backlog is
dropped. After the fix the same log shows steady intervals: fades 14.0 ms, slide 3 animation
58.0 ms (first ten 58, 58, 59, 57, 59, 57, 58, 58, 58, 59), slide 1 animation 28.0 ms, with the
latest step 1 to 2 ms late. Checked on this machine only, in a window I opened for 11 s; it
depends on the display, so it may differ on the operator's. The stepped comparisons are
unaffected (they do not use the real-time loop).

## Milestone 3, slide 4: the eyes

`build/3k --scene intro4` plays slide 4: the same fades; a face whose eyes change through a long
sequence (open, moving, closing, then two long pauses in which they blink, then moving again) while
ten small sparkles appear and fade in a patch of sky at the top right; five narration lines; fade
to black. `--scene intro` plays slides 0 to 4 in a row. The scene is `src/intro4.[ch]`.

    build/3k --scene intro4 --data original_bin
    tools/check_intro4.sh        # compare with the saved reference frames (needs work/oracle4*)

The animation loop is 11,401 passes. The original does not pace it, so the length is a free choice:
it runs at 300 passes per second (`TK_INTRO4_PASS_HZ`), shown as 5 passes per displayed frame at
60 frames per second (new: `rt_slide` runs several passes per frame when a loop's rate is above 60).
That is 38 s in all, with pauses of 6.7 s and 10 s and a blink every 0.2 s. **The operator ran it
(2026-10-15) and judged it good**, so 300 passes per second is accepted. Space skips the whole
animation.

The sparkle positions use the program's random generator, which is not reset between slides, so in
the original they depend on how many passes slide 0 ran. The port keeps the same single generator
across slides, so they depend on slide 0's real-time pass count; nothing needs them to be fixed.

### Not covered / known gaps
- Pacing is a guess (see above); **no human has looked at this slide yet.**
- The final fade to black was not compared with the original; silent (no voice-over).
- The values carried over from earlier slides (sparkle counter 3, blink counter 0) assume the
  earlier slides were not skipped.

## Milestone 3, slide 5: the woman in red

`build/3k --scene intro5` plays slide 5: the same fades; 2,000 passes in which a woman's face changes
slowly between three expressions (cycle 0 1 2 1, each held 151 passes); eight narration lines; fade
to black. `--scene intro` plays slides 0 to 5 in a row. The scene is `src/intro5.[ch]`.

    build/3k --scene intro5 --data original_bin
    tools/check_intro5.sh        # compare with the saved reference frames (needs work/oracle5*)

The loop is unpaced in the original; it runs at 120 passes per second here (`TK_INTRO5_PASS_HZ`,
two passes per displayed frame): 16.7 s in all, a new expression every 1.3 s. **A guess: please
judge it by eye.** Space skips the animation.

### Not covered / known gaps
- Pacing: the operator ran it (2026-10-15) and found it good ("I don't notice anything weird").
- The final fade to black was not compared with the original; silent (no voice-over).
- The frame counter's start value (0 or 1 in the original, from slide 4's leftovers) is fixed at 0.

## Milestone 3, slide 6: the red robe (the last slide)

`build/3k --scene intro6` plays slide 6: the same fades; 2,000 passes in which a woman's hair
alternates between two frames every 25 passes (ten periods of 51 passes), then shows a third frame for
a period, and repeats; eleven narration lines; fade to black. `build/3k --scene intro` plays the
whole intro, slides 0 to 6, in real time and then reports that the original would now start the
title again. The scene is `src/intro6.[ch]`.

    build/3k --scene intro6 --data original_bin
    build/3k --scene intro --data original_bin     # the whole intro
    tools/check_intro6.sh        # compare with the saved reference frames (needs work/oracle6*)

The loop is unpaced in the original; it runs at 120 passes per second here (`TK_INTRO6_PASS_HZ`),
as slide 5: 16.7 s, the hair changing every 0.2 s. **A guess: please judge it by eye.**

### What is left of the intro (milestone 3)
The music (`MUC.ALL`, FM), listening to the new voice and slide 0 sound, the pause length between
lines if it feels wrong, and the exec of `mark.exe` (milestone 4).

## Milestone 3: sound (voice-over and slide 0's clip), built 2026-10-15

Run `build/3k --scene intro` (or `intro1`..`intro6`, `intro0`) with the game's data; there is sound
when the machine has an output device. `--no-audio` forces the silent path (the original's timing
without a sound card: 74-tick holds). `--slide0-replay` turns on the restart described below.

What it does, from the original's code (`findings.md`, "the sound path of STORY2.SL"):
- **Slides 1 to 6:** for each subtitle line, the cross-fade runs first (14 passes, about 0.4 s at the
  35 Hz subtitle rate), then the line's clip starts and the line stays until the clip has finished.
  Space cuts the clip and goes to the next line. There is no extra wait after a clip. So the
  pause between lines is the cross-fade plus the silence at the ends of the clips (0 to 0.5 s each).
  Slide 2 in a real run held its lines 3.74, 4.51 and 3.12 s against clips of 3.76, 4.50 and 3.13 s.
- **Slide 0:** `STORY0_1` (20.87 s) starts on the 20th loop pass, about 0.5 s in. At the end of the
  loop, if it is still playing, the screen stays as it is until it ends (Space does not skip this,
  as in the original), then the fade-out.
- **Decision for you, off by default: the restart.** The original's code starts the clip again 20
  passes after it ends, and then waits for that second playing at the end. At this port's pacing the
  loop outlasts the clip, so that adds 18 s of a frozen last frame (measured: 27 s without, 45 s with
  `--slide0-replay`). Whether the authors meant that is unknown. If you listen and the replay seems
  intended, make it the default.
- **Design:** `src/audio.[ch]` is a three-function sink (play, playing, stop) that the scenes use as the
  original used the driver's status word. The SDL layer fills it; with no sink (stepped comparisons,
  the headless build, `--no-audio`, no device) the scenes run exactly as before, which is why the
  frame comparisons are unchanged. The scene asks the device whether it is still playing, so the
  hold follows the audio itself and not a calculated length.
- **Tests:** `tests/test_audio.c` (decoder, VOC refusals, `TAB.TAB` tiling, the player and slide 0
  against a fake sink). `tools/mutate_audio.sh` breaks five things in a scratch copy and checks the
  tests notice.

### Not covered / known gaps
- **Listened to (2026-10-15, operator):** the subtitles and the voice-over are in sync. That
  covers the clip-per-line pairing, the hold lasting as long as the clip, and the pause between
  lines. It was a judgement by ear on the operator's machine. Slide 0's sound also "seemed right"
  to the operator (2026-10-15), with the restart off (the default); the operator did not say
  whether the original's restart would be wanted, so it stays off.
- Whether the ADPCM clip (slide 0) sounds right is unknown; the decoder is
  DOSBox-X's rule, not checked against hardware (`formats.md`). The 8-bit voice clips need no
  decoding rule and are covered by the listening above.
- The FM music loaded from `MUC.ALL` (it plays under the whole intro in the original) is not built.
- Resampling quality (SDL's stream, 13,158 or 6,060 Hz to the device rate) was not examined.
- With a pause between lines that you find too short or too long, the knobs are the subtitle pass
  rate (`TK_SUBTITLE_PASS_HZ`) and nothing else; the original had also a disk read in that gap whose
  length is not known.

## Milestone 5: music (`MUC.ALL`), built 2026-10-15

**Status: built and tested, NOT yet listened to by anyone.** The note-to-register mapping follows
the public CMF description (ModdingWiki, "CMF Format"), not the original driver, whose handlers
were not read. Whether it sounds like the original is for the operator's ears.

**Chip: ymfm** (BSD-3-Clause, `third_party/ymfm/`, unmodified files, commit `81aec25`, see
`PINNED.txt` there; the licence text is `third_party/ymfm/LICENSE`). Chosen by the operator over
Nuked-OPL3 (LGPL) and a home-made emulator. It is C++14, so `src/opl.cpp` wraps the YM3812 in a C
interface (`src/opl.h`); everything else stays C99. The chip is clocked like an AdLib (3,579,545 Hz),
so it renders at 49,716 Hz. The Makefile builds ymfm into `build/opl/`.

**Player (`src/music.c`):**
- The song is the one `STORY2.SL` reads: `MUC.ALL` offset `0x27137`, 18,850 bytes.
- Time is exact: ticks (96 per second) to samples with integer arithmetic. Playback does not depend
  on the machine or the audio buffer size (the test renders identically in chunks of 1, 997 and 4,096).
- It loops: at the end it releases the sounding notes and starts again. The original does the same
  from its poll (`STORY2.SL 0x130b0`).
- A song of zero length (all events at tick 0) never loops; it rings out. This was a real bug, found
  when a test hung for 30 minutes (stack sample: `music_render` calling `cmf_next` forever).
  `test_zero_length` and the Makefile's 5-minute limit on every test binary guard it now.

**ASSUMED, to be judged by ear** (each is marked ASSUMED in `src/music.c`):
- MIDI channel n plays on OPL channel n (melody mode; this song uses channels 0-8 only).
- Velocity v sets the carrier output level to 63 - v/2, keeping the patch's key-scale bits. The song
  uses velocity 127 on eight channels and 76-88 on channel 8 (`tools/cmf_behaviour.py`).
- Pitch: MIDI note 69 = 440 Hz, f-number by the standard formula for a 49,716 Hz chip. Measured on
  the rendered audio, A2/A3/A4/C5/A5/A6 are within 0.5 % of true.
- Chip set-up: waveform select on, AM and vibrato depth both on (the CMF default).
- Not used by this song, so not decided: rhythm mode (**songs 0 and 3 of MUC.ALL do use it** and
  would play wrongly; see findings.md 2026-10-15), patches the song does not define (the
  original has 16 built-in defaults), pitch bend (the original CMF player ignores it).

**In the game (`--scene intro` only):** the song starts after slide 0's fade to black, where
`STORY2.SL` calls its start function (`0x10ab5`, the beginning of slide 1's set-up), and stops when
the intro ends. It has its own SDL audio device (`platform_sdl.c`, callback-driven), so the voice
clips and their timing (confirmed by the operator) are untouched. Options: `--no-music`,
`--music-gain PERCENT` (default 100). `TK_AUDIO_LOG=1` reports the music samples SDL pulls.
The standalone `--scene intro1` .. `intro6` play without music.

**Checks:**
- `tests/test_music.c`: registers for one note against the standard AdLib A4 value (f-number 0x244,
  block 4); velocity mapping; note-off matching (including *when* the last key-off happens);
  velocity 0 as note-off; ignored-event counts; rendered pitch by zero crossings; transpose
  controller; looping; zero-length songs; and on the real song: 2,402 key-ons and 2,402 key-offs
  (the survey's 4,804 counts both), no ignored events, one loop in 135 s, bit-identical output for
  any chunk size, peak 13,088 in 20 s (audible, no clipping).
- `tools/mutate_music.sh`: seven deliberate bugs, all caught (one by the hang guard).
- AddressSanitizer + UBSan on `test_music` and `test_audio` (ymfm built with them too): clean.
- The real SDL path was run headless (`SDL_AUDIODRIVER=dummy`): the music device pulled 45 s of
  samples in 45 s of wall clock after slide 0, with no stall.
- To listen without the game: `make build/music_dump && build/music_dump original_bin out.wav 135`.

**Not checked:** how it sounds; the original driver's register writes (could be logged in the
oracle emulator if the sound is off); the volume balance between music and voice (`--music-gain`
exists for that); whether the original restarts the song at exactly the end or after a delay.
