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
| 4 | Chain the modes: title -> intro -> title, or title -> menu | **title <-> intro done 2026-10-15** (`--scene game`, see "Milestone 4"); the menu (`WALK0.SL`) is not built, so a key or click on the title ends the game; **menu built and playable 2026-10-15** (`--scene menu`, and `--scene game` after the title): frames, keyboard and mouse identical to the original (106 comparisons, "Milestone 4b" and "4c"); the opening animation, the choice transition and the slot picker are built ("Milestones 4d-4f"); the four actions are not (they start WALK.SL or CAST.SL), so a choice ends the program |
| 6 | Credits (`CAST.SL`), from the menu's third item | **built 2026-10-16** (`--scene credits`, and from the menu in `--scene game`): wipe, loop, keys and end identical to the original in 46 comparisons; pace confirmed by the operator; song 0 plays under it (rhythm mode, "Milestone 5b") |
| 5 | Music (`MUC.ALL`, FM) | **done 2026-10-15 for the intro song** (operator confirmed by listening that the music plays along with the voice; the title plays it too since milestone 4, not yet listened to): ymfm OPL2 + CMF player, plays under slides 1-6 of `--scene intro` (see "Milestone 5"); rhythm mode (songs 0 and 3) deliberately deferred: build it when `CAST.SL` / `WALK.SL` are ported (operator decision 2026-10-15), checking it against a DOSBox-X recording of those programs; **rebuilt 2026-10-16 on the original driver's rules, rhythm mode included: 25 of 25 songs write exactly the registers the driver writes, tick by tick ("Milestone 5b")** |

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
without a sound card: 74-tick holds). Slide 0's clip plays twice, as the original does; `--slide0-once`
plays it once (see below).

What it does, from the original's code (`findings.md`, "the sound path of STORY2.SL"):
- **Slides 1 to 6:** for each subtitle line, the cross-fade runs first (14 passes, about 0.4 s at the
  35 Hz subtitle rate), then the line's clip starts and the line stays until the clip has finished.
  Space cuts the clip and goes to the next line. There is no extra wait after a clip. So the
  pause between lines is the cross-fade plus the silence at the ends of the clips (0 to 0.5 s each).
  Slide 2 in a real run held its lines 3.74, 4.51 and 3.12 s against clips of 3.76, 4.50 and 3.13 s.
- **Slide 0:** `STORY0_1` (20.87 s) starts on the 20th loop pass, about 0.5 s in. At the end of the
  loop, if it is still playing, the screen stays as it is until it ends (Space does not skip this,
  as in the original), then the fade-out.
- **The restart is ON by default since 2026-10-15 (was a decision for you, off).** The original's
  code starts the clip again 20 passes after it ends, and then waits for that second playing at the
  end, so slide 0 holds its last frame while the clip plays a second time (27 s in this port with one
  play, 45 s with two). A recording of the ORIGINAL running in DOSBox-X with the Sound Blaster on
  settled it: the clip plays exactly twice at all three CPU speeds tried (max, 10,000 and 3,000
  cycles per ms), the second play ending 43 to 50 s into the run (findings.md, 2026-10-15, "Slide 0's
  sound: what the original does"). `--slide0-once` plays it once. Whether the authors *meant* it
  stays unknown; it is what the program does.
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

**Status: built and tested; the operator listened on 2026-10-15 and confirmed that the music plays along with the voice during the slides. Only that was reported: instrument fidelity, loudness balance and the loop point were not commented on, so they are not confirmed.** **SUPERSEDED 2026-10-16 (see "Milestone 5b" below):** what the operator
listened to followed the public CMF description (ModdingWiki, "CMF Format"). The player now follows the original's
driver and is compared with it register by register; the list of ASSUMED choices below is kept as the history of what
was replaced.

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

**ASSUMED when this milestone was built (all replaced by the driver's own rules in Milestone 5b):**
- MIDI channel n plays on OPL channel n (melody mode; this song uses channels 0-8 only).
- Velocity v sets the carrier output level to 63 - v/2, keeping the patch's key-scale bits. The song
  uses velocity 127 on eight channels and 76-88 on channel 8 (`tools/cmf_behaviour.py`).
- Pitch: MIDI note 69 = 440 Hz, f-number by the standard formula for a 49,716 Hz chip. Measured on
  the rendered audio, A2/A3/A4/C5/A5/A6 are within 0.5 % of true.
- Chip set-up: waveform select on, AM and vibrato depth both on (the CMF default).
- Not used by this song, so not decided: rhythm mode (**songs 0 and 3 of MUC.ALL do use it** and
  would play wrongly; song 0 is CAST.SL's, song 3 can only come from WALK.SL; see findings.md
  2026-10-15), patches the song does not define (the
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

**Checked against the original (2026-10-15, findings.md "recording the original's audio"):** the
original's loop period is independent of CPU speed and its tempo agrees with the port's within
about +-1.5 % (limited by the recording clock); its pitch agrees (+0.00 semitones on a tool that
reads +-1 and -2 exactly on fake captures). **Not established:** instrument timbre, volume mapping
and channel mapping; the blunt spectral score cannot separate them from the difference between two
chip emulators.

**Not checked:** how it sounds to the operator beyond "plays along with the voice"; the original
driver's register writes (could be logged in the oracle emulator if the sound is off); the volume balance between music and voice (`--music-gain`
exists for that); whether the original restarts the song at exactly the end or after a delay.

## Milestone 5b: the player follows the original's FM driver, 2026-10-16

**Why:** the operator asked for rhythm mode (needed by the credits' song) and for the pitch table and volume rule to match
the original. The first version of the player followed the public CMF description (Milestone 5); its choices were
guesses. The original's FM driver is linked into every program that plays music ("FM-Music Functions, Copyright (c)
Creative Technology Pte Ltd, 1990", `CAST.SL 0x134a0`), and its register writes can be recorded in the oracle emulator.

**Method:** `tools/dosemu.py --opl-log` models an AdLib and logs every register write with the driver's own clock (timer
interrupts delivered; the driver programs the timer to 96 Hz, the songs' tick rate). `tools/capture_song_opl.py N`
runs `CAST.SL` on a copy of the data in which song N sits where `CAST.SL` reads its song, so ANY of the 25 songs can be
played through the real driver. `build/music_regs` prints the port's writes for the same song, and
`tools/compare_opl.py` compares them per song tick. `tools/check_music.sh` does all 25.

**Result: 25 of 25 songs identical.** For every song tick after the first, the port's writes are the driver's, the same
registers with the same values in the same order (127,008 song ticks, 22 minutes of music and 23,389 notes in all). At song tick
0 every write the port makes is in the driver's list, in order; the driver also does its own reset there (default
patches for all voices, writes to `0x00..0x1a` that no chip uses), which the port does not reproduce. After the last
event the driver's stop routine writes `0x83=0x13` nine times and clears `0xbd`; that is accepted as a suffix on the
last tick only. Songs 0 and 3 use rhythm mode; song 3 also uses the snare and the cymbal.

**What the driver does, and the port now does** (`src/music.c`; details and addresses in findings.md):
- **Pitch:** twelve f-numbers `343 363 385 408 432 458 485 514 544 577 611 647` and the block one octave below the MIDI
  number (note 24 is block 1; below 24 it is 0; it stops at 7; notes above 119 repeat note 127). A4 is f-number 577, 9 cents
  flat of equal temperament. The assumed formula gave 580.
- **Volume:** `level = 0x3f - (((0x3f - patch level) * (0x80 | velocity)) >> 8)`, key-scale bits kept, written to the
  carrier at each note-on. The first player used `63 - velocity/2`, which IGNORED the patch's own level: every note at
  velocity 127 played at the loudest setting. The driver scales within the patch's range, so a patch with a high level
  (a quiet instrument) stays quiet. Over the velocities and patch levels the 25 songs use, the two rules differ by
  7.3 steps on average and by up to 30 (velocity 127 on a patch of level 29: old 0, new 30), one step being 0.75 dB.
- **Voices:** nine, or six in rhythm mode. A note takes the voice its channel released last, else the lowest free one, else
  one another channel released, else the oldest is cut. A patch is loaded when a voice's last owner was another channel,
  so patches load at note-on, not at the program change; a program change reloads the voices the channel holds and frees
  the ones it released. The old mapping "channel n on voice n" was wrong and produced different voice numbers (not
  audibly different, since every note loaded its own patch).
- **Patch load:** modulator and carrier bytes in the usual order, but the register of the SOUNDING operator's level is not
  written (the note-on writes it).
- **Rhythm mode** (controller `0x67`, value non-zero, at tick 0): `0xbd = 0xe0`. MIDI channels 11-15 are bass drum (both
  operators of voice 6, level in `0x53`, bit `0x10`), snare (operator `0x14`, voice 7, bit `0x08`), tom-tom (`0x12`, voice 8,
  `0x04`), cymbal (`0x15`, voice 8, `0x02`) and hi-hat (`0x11`, voice 7, `0x01`). Everything except the bass drum uses ONE
  operator, loaded from the patch's MODULATOR bytes with its modulator level. A note writes the operator's level, the voice's
  pitch (no key-on bit) and `0xbd` with the drum's bit set; a note-off clears the bit.
- **Controllers:** only `0x67` (rhythm) is acted on; `0x66` is a marker; `0x63` (AM/vibrato depth) and `0x68/0x69`
  (transposition) of the first version were inventions of the public description and are ignored, like every other. No song
  uses them.
- **Restart:** at the song's end all notes are released (melodic and drums) and the song starts again; its first events reset the
  driver's state as the original's do.

**What is deliberately not reproduced:** the driver's reset writes at song tick 0 (default patches, writes to unused
registers), and its stop routine's writes after the last event. Neither affects the sound: every voice is loaded
before its first note. NOT checked: how the audio itself compares (the registers are identical, and the chip is ymfm in both
the comparison and the port, but the original's chip was a real YM3812; that was never a variable). The old "instrument
timbre" doubt is therefore reduced to the chip emulator.

**Changes you may hear (the operator listened on 2026-10-16 and confirmed that the loudness balance feels good and that the credits play the music nicely; pitch and the other points were not commented on):**
- **Loudness balance between instruments**, the biggest change. Instruments whose patch has a high level are now quieter next to
  the others (by up to 22 dB for some combinations of velocity and patch in the songs; the mean over all the combinations they
  use is 5.5 dB, which says little about what is heard). Measured on the intro song's first 60 s: overall RMS -20.1 dBFS with the
  old player, -21.8 dBFS with the new (1.7 dB quieter), peak 14,528 to 12,480. This is what the driver does, but it is not what
  the operator heard on 2026-10-15, and the music against the voice-over may need `--music-gain` again. An A/B for listening:
  `work/opl/ab/intro_OLD_player.wav` (the player of commit `f371352`) and `intro_NEW_player.wav` (git-ignored; made with
  `build/music_dump original_bin OUT.wav 60` and the old `music.c` compiled with `tests/music_dump.c`).
- **Pitch:** about 9 cents flat everywhere (A4 is 437.7 Hz, not 440).
- **Credits:** they now have their song, with drums.
- Unchanged: tempo, notes, instruments' timbre, and the timing against the voice-over (which is what the operator confirmed).

**Checks:**
- `tools/check_music.sh`: 25 of 25 songs identical to the driver's writes (captures in `work/opl/songs`).
- `tests/test_music.c` (rewritten): expected values copied from the driver's logs (the rhythm sequence for bass drum, tom-tom
  and hi-hat; level points 127/0 to 1, 101/0 to 7, 95/0 to 9, 116/3 to 6; the pitch registers of 17 notes, including both ends
  of the table), voice reuse, the program-change rules, voice cutting, the six voices in rhythm mode, snare and cymbal, and
  the real intro song (2,402 key-ons and key-offs, no ignored events, bit-identical for any chunk size).
- `tools/mutate_music.sh`: 32 deliberate bugs, all caught (one by the hang guard).
- AddressSanitizer + UBSan: `test_music` clean; the sanitizer build of `music_regs` writes the same registers for songs 0, 3 and 22.
- Real time under SDL's dummy drivers: the credits with sound run 52 s and end; Space at 5 s and closing the window at 4 s stop
  the song cleanly; the chain title, menu, credits, menu works; the intro runs.

## Milestone 4: chaining title and intro, 2026-10-15

`build/3k --scene game --data original_bin` runs the original's program chain in one process:
title (`MARK.EXE`) -> after its 699-pass timeout the intro (`STORY2.SL`) -> back to the title, and
so on until the window is closed. A key or click on the title would start the menu (`WALK0.SL`),
which is not built: the game prints that and ends. `--scene title` and `--scene intro` still run
alone, with the same music.

**Design (`src/main.c`):** `play_title` and `play_intro` are real-time functions on an already
initialised platform; `run_game` loops over them. Each original program is started fresh by `exec`,
so each scene is created from its own initial state (the intro resets the random generator to seed
1 itself). The hand-over message byte in `ANI.LCP` is 0x3f for every hand-over involving
`STORY2.SL` and `STORY2.SL` leaves it alone (code.md, findings.md), so it is not modelled.
Closing the window is now sticky (`platform_poll` keeps answering "quit"), so a chain ends instead
of starting the next scene.

**The title has music (MARK.EXE, read 2026-10-15, `tools/calls16.py`):** on the normal path it opens
`MUC.ALL`, seeks to `0x27137`, reads `0x49a2` bytes (the same song as the intro) at `0x10577`..
`0x105a4` and, if the sound flag `[0x1d0]:4` is set, calls `0x11160` with the buffer (`0x105d0`),
before it sets the video mode. When the title loop ends it calls `0x1124a` and `0x11272`
(`0x10b1c`, `0x10b21`). Roles of these calls are inferred from position and from the intro's use of
the same driver (start, stop, shut down), not from the driver's code. The title therefore plays the
song from its first moment, through the fade-out; the port does the same (`song_start` in
`play_title`). The other song read at `0x103b8` (`0x2d76b`, 1,969 bytes) belongs to the optional
scene that the shipped data skips; it is not built.

**Checked:**
- Real build with SDL's dummy audio and video drivers (nothing plays): title -> intro -> title ->
  intro -> title -> intro went round three times (the intro sped up with `TK_AUTO_SPACE_MS=400`);
  the music device kept pulling samples throughout.
- Quit: a real `SDL_QUIT` event (`TK_AUTO_QUIT_MS`) during the title, slide 0 and the later slides
  each ended the program with exit 0 within a second, without starting another scene.
- Unchanged: `make test` (5 suites), slides 1-6 frame comparisons, sanitizer build of the headless
  target. The title's stepped comparison path was not touched.

**Not checked:** how the chain looks and sounds on a real display and speakers (nobody has run it
yet); the sanitizer cannot run the SDL path (an SDL modal dialog once hung a run for 1.5 hours);
whether the original's title after the intro starts with a held Space; the menu.

**Test hooks (environment, testing only):** `TK_AUTO_SPACE_MS=N` presses Space in the slides every
N ms (the title's held-key check is separate, so the title still times out); `TK_AUTO_QUIT_MS=N`
posts an `SDL_QUIT` N ms after the first poll; `TK_AUDIO_LOG=1` reports music samples pulled.

## Milestone 4b: the menu's static frame (WALK0.SL), built 2026-10-15

`src/menu.[ch]` computes the menu's screen after N passes of its loop: no clock, no input, no SDL (so it
can be compared and tested headless, like the title and the slides). `build/3k_headless --scene menu
--step-dump N:FILE [--highlight K]` writes the screen after N passes in the oracle's raw format.
It is not yet reachable from `--scene game`: nothing runs it in real time.

**What a frame is:** `GMAIN.LCZ` with a 3-digit CPU label at the top right; a 192 x 168 backdrop at
screen (64, 16) that rolls (below); the four labels (122 x 30 each) over the backdrop at (100, 40 + 30 k),
colour 0 transparent, the selected one recoloured `0x17` -> `0x10` (white). The palette is `RPG.LCP`'s
first 768 bytes, unchanged.

**The roll:** a counter runs 0..200 (it starts at 0, is counted at the top of each pass, so the first pass
has counter 1, and wraps to 0 after 200: a cycle of 201 passes). For counter 0..119 the backdrop rolls one
pixel left (the first pixel of each row wraps to the end); for 120..200 it rolls one row up (the top row
wraps to the bottom). [Corrected 2026-10-15: an earlier version of this text said the first frame was
already one pixel on, for a cause unknown. That was a misreading of the oracle's snapshot index, see
findings.md, "The one-pixel quirk was my own off-by-one".]

**The CPU label** ("(2 + speed class) 8 6", so 486 with the shipped class 2; answers the operator's
hunch, findings.md): three 8 x 8 tiles of a digit atlas (`RPG.LCP` `0x1643b`) into a 27 x 8 strip, a
colour ramp `0xf0`..`0xfb` over it in bands of 20 bytes, a "stretch" to 53 x 15 that is really a sparse
resampling (each source pixel written once, the pixels after it left 0, every other row blank, so the
label looks dotted), blitted at (268, 12) with a 1-pixel shadow (colour 0) one row and column on.

**Checked (re-run with `tools/check_menu.sh`, needs the oracle's set-up):**
- 40 of 40 frames identical to the original's, pixels and palette: item 0 at 18 passes (both halves of
  the roll and both counter wraps, up to pass 401), items 1-3 at 4 passes each, speed classes 0-4 at 2
  passes each (a throwaway copy of the data with `RPG.LCP` byte 0 changed).
- The CPU label's intermediate buffers (the strip and the stretched sprite) are byte-exact against dumps
  of the original's memory (`--dump-local`); the first stretch attempt was wrong because the original does
  not reload the pixel inside its inner loop (it writes a source pixel once, then zeros).
- `tests/test_menu.c`: primitives on hand-worked synthetic sprites, plus invariants of the frame with the
  real data (determinism over 450 passes, nothing outside the panel and label box is touched, the white
  label follows the selection). `make test`: 6 suites, 0 failures.
- `tools/mutate_menu.sh`: nine deliberate bugs, all caught by the frame comparison.
- AddressSanitizer + UBSan: clean, output identical.

**Not covered:** speed class bytes above 4 (the game cannot produce them; a tile read past the atlas is
treated as zeros, the original reads on into memory); the opening (the 20-step wipe and the 160-step
panel build, findings.md), key and mouse input, the confirm transition, the four actions, the slot
picker, music, real-time pacing. Pacing will be a guess as for the intro: the original does not fix the
pass rate (about 2.3 passes per tick in the oracle at 500,000 instructions per tick).

## Milestone 4c: the menu is playable (keyboard, mouse, real time), built 2026-10-15

`build/3k --scene menu --data original_bin` runs the menu in a window; `--scene game` goes title -> menu
when a key or click leaves the title. A confirmed choice prints what the original would do and ends the
program: new game (reads the new-game files, starts `WALK.SL`), load (the slot picker), credits
(`CAST.SL`), quit. None of those four is built yet, nor the opening animations or the 33-step closing
transition, so the menu appears at once and a choice returns at once.

**Input rules (all checked against the original, see below):**
- **Keyboard** is read as the keyboard PORT, not as key events: the last scancode the keyboard sent. The
  menu ignores every make code until it has seen a break code, and takes one key per break code. So the
  Space that left the title does nothing (it is still down, or its release opens the latch), and a held key
  does not repeat. Up/Down move the selection and wrap at both ends; Space or Enter confirm. The platform
  keeps `port60` (`platform_port60()`); auto-repeat sends nothing.
- **Mouse** (`menu_mouse`): the arrow (an 18 x 21 sprite, `RPG.LCP` `0x3255`, 382 bytes, drawn raw) is
  drawn on the panel at the position clamped to x 64..237, y 16..162. With x in 101..219 the item whose y
  band holds the pointer becomes the selection (bands 41..63, 72..94, 105..127, 137..159; the gaps and the
  band edges keep the old selection); a button down (either) inside that x range confirms the current
  selection, also from a gap. Outside the x range nothing is selected or confirmed, whatever the buttons do.
- **Order within a pass:** the picture is drawn with the OLD selection, then the input is read, then the
  panel is shown. A selection takes effect in the next picture; a confirming pass shows nothing.

**Decisions where the original gives no answer (all changeable):**
- **Pass rate: `TK_MENU_PASS_HZ` = 35** (`timing.h`), a GUESS from the intro's rate. The original's loop is
  unpaced; the backdrop moves one pixel per pass.
- **Mouse scale 1:1.** The original takes the mouse driver's x as a pixel. A DOS driver in mode 13h
  normally reports x in a virtual 0..639 range, which would make the arrow cross the panel twice as fast as
  the pointer; whether the original's driver did so was not determined. The port maps the window pointer
  to screen pixels. Until the pointer first moves it sits mid-screen (a driver's position after a reset).
- **Keyboard port starts as the break code of Enter (0x9c)**, as if the game were started from the DOS
  prompt: the latch is open from the first pass, so the first key counts. (With no scancode at all the
  first key press would be ignored, as the stepped checks show.)
- **A button still down when the menu opens confirms at once** if the pointer is over the item column,
  as in the original (checked: "button held from the start" confirms after 0 passes). The title needs a
  second or so to fade out, so a normal click has been released by then.

**Checked (`tools/check_menu.sh`, re-run from scratch: 106 of 106 comparisons identical to the original):**
- 40 frames of the static screen (milestone 4b); 20 frames of a keyboard script (latch opening, accepted
  and ignored presses, wraps both ways); 33 frames of a mouse script (every band, the gaps, the x edges at
  100/101 and 219/220, the four clamped corners, the band boundaries at 40/41, 63/64, 71/72, 94..96,
  104/105, 127/128, 136/137, 159/160); and 13 confirmation decisions (the pass in which the original takes
  its confirm branch, `0x10e9d`, for Enter, Space, clicks inside and outside the column, either button, a
  click in a gap, and a button held from the start).
- Through the scripted real-time runs under SDL's dummy drivers (`TK_TEST_INPUT`): `--scene menu` with
  keys and with the mouse, and `--scene game` title -> menu -> choice, including a Space held for 7
  seconds across the menu's opening, which did nothing until Enter.
- `tests/test_menu.c` grew keyboard and mouse rules (28 of them in all with the frame ones); `tools/mutate_menu.sh`:
  19 deliberate bugs (9 in the picture, 10 in the input), all caught. Sanitizers clean.

**Not covered:** how the arrow and the keys FEEL at 35 passes per second (nobody has played it); the F1..F10
buttons in the frame (no code for them was found in the menu loop); the mouse scale question above; the
music of the menu (it plays the menu's song, `MUC.ALL` `0x2bad9`, but nobody has listened);
that the oracle's mouse is my emulation of three INT 33h functions (reset, position, buttons), so
the checks prove the port agrees with the original program given THAT driver, not with a particular real one.

## Milestone 4d: the menu's opening animation, built 2026-10-15

`menu_opening_frame(m, scr, n)` (`src/menu.c`) gives the screen after n of the 182 steps of the opening
(`menu_opening_steps()`); `play_menu` runs them before the first pass, then starts the menu's song. What
happens is described in `findings.md` ("The menu's opening animation"): a 20-step zoom of the finished
picture stretched sparsely to the whole screen, the picture, the lettering "Ready" shrinking away in 160
steps, and a clear.

**Checked:** `tools/check_menu.sh` compares the port with the oracle at all 183 stages (before the first
step, after each step): 183 of 183 identical, palette included; with the earlier checks 289 of 289.
`tests/test_menu.c` has the resize routine (including the fraction that carries between rows) and the
properties of the frames; `tools/mutate_menu.sh` 12 more deliberate bugs in the opening, see its output.
The same dump under address and undefined-behaviour sanitizers is clean.

**Pace (GUESS, `timing.h`):** the original does not wait anywhere in the opening, so the length of
the real thing is only the speed of the machine. The port shows the zoom (and the whole picture) at 30
steps per second (0.73 s) and the shrink at 120 per second (1.34 s); about 2.1 s in all. The step due is
computed from elapsed time, so on a 60 Hz display the shrink shows every other step (each frame depends
only on its step number). Nobody has watched it yet.

**Not built:** the confirm transition (33 steps), the four actions, the slot picker.

## Milestone 4e: the transition after a choice, built 2026-10-15

`menu_confirm_begin` / `menu_confirm_step` (`src/menu.c`) draw the 33 steps that follow a choice: the
backdrop keeps rolling and the arrow draws a check mark in the chosen label (findings.md, "The transition
after a choice"). `play_menu` runs it after a choice and then holds the last frame for 0.3 s as a stand-in
for the next program, which does not exist yet (the original goes straight on to load files and start it).

**Checked:** `tools/check_menu.sh` now reports 553 of 553 comparisons identical to the original (the 289
before, plus the transition in 8 scenarios of 33 frames); `tests/test_menu.c` counts the mark's pixels step
by step for all four items; `tools/mutate_menu.sh` has 12 more bugs in the transition.

**Found on the way, and fixed:** the "one-pixel quirk" of milestone 4c was an off-by-one of my own in how
the check script numbered the oracle's snapshots (findings.md). The port is now simply the original's
logic: counter 0 before the loop, no extra roll. Passes 119 and later were wrong before today.

**Pace (GUESS, `timing.h`):** 33 steps per second, one second for the whole check mark. Not watched yet.

**Not built:** the four actions (they need `WALK.SL`, `CAST.SL` or the slot picker; a choice still ends
the program after the transition and prints what the original would do), the slot picker.

## Milestone 4f: the slot picker, built 2026-10-15

`menu_picker_begin` / `menu_picker_pass` (`src/menu.c`): after "load a saved game" and its check-mark transition, a
strip of 11 numbered cells replaces the label, with an icon under each slot whose `SAVEnn.000` exists in the data
directory (probed with `assets_exists`) and a blinking marker on the selected cell. Left/Right, Space/Enter or the
mouse pick a slot that has a save; the program prints the slot and ends (WALK.SL does not exist yet). As in the
original there is no way out of the picker other than a pick or closing the window; with no saves at all it just
waits.

**Checked:** `tools/check_menu.sh` reports 640 of 640 comparisons identical to the original (553 before, plus 87 for the
picker); unit tests in `tests/test_menu.c` (icons, held keys, ends, empty slots, mouse bands, the blink cycle);
16 deliberate bugs in `tools/mutate_menu.sh`. Real-time runs under SDL's dummy drivers: Right x3, Enter picks slot 3;
Enter on an empty slot stays.

**Pace:** the same 35 passes per second as the menu (`TK_MENU_PASS_HZ`), so the marker blinks about 3 times a second.
GUESS; nobody has watched it.

**Not built:** loading the picked save (the three `saveNN.00x` files into the game state), which belongs with WALK.SL.

## Milestone 6: the credits (CAST.SL), built 2026-10-16

`src/credits.[ch]` reproduces the program the menu's third item starts: the wipe, the main loop, the keys and the
end. It has no clock, input or SDL, like `menu.c`. Details of the original are in `findings.md` ("CAST.SL ...").

**Run it:** `build/3k --scene credits --data original_bin` (alone, starting from the menu's frame), or
`--scene game` and choose the third item of the menu; when the credits end the menu starts again, as the original
does (CAST.SL starts WALK0.SL). Space or Enter ends the credits at once; closing the window ends the program.

**What it does**
- **Wipe (200 steps):** the picture the previous program left rolls up and off the top; the lower half of the credits'
  background rolls in from the bottom edge, then stays whole while the upper half goes to colour 0.
- **Loop (1,790 passes):** one new row of the text bitmap per pass; a 120 x 100 window of staff names at x = 100,
  faded at its top and bottom; below, the same rows stretched wider with distance (120 to 320 pixels) and squeezed
  in height form a reflection on the floor.
- **End:** after 1,790 passes, or when the last scancode read at the end of a pass is Space (0x39) or Enter (0x1c).
  The original reads port 0x60 raw, with no release latch, so a key held from before counts; in the port the Enter that
  chose the item is normally released during the 2 s wipe.
- **Palette:** never touched here; the screen keeps what the menu left (the interface palette, `RPG.LCP[0:768]`).

**Checked** (`tools/check_credits.sh`, from a common starting screen in the oracle emulator): 46 of 46 comparisons
identical to the original: 13 screens of the wipe (steps 0-200), 24 of the loop (after 0 to 1,790 passes, both
sides of the scroll end and of the hold), and 9 decisions about the end (no key, Space, Enter, Esc then Space, a
make-and-break pair, only break codes, Space at pass 1000, 1789 and 1790). `tests/test_credits.c` (hand-worked
resize, scroll, expansion, the wipe's end state, the pass count, the keys, reproducibility); 17 deliberate bugs in
`tools/mutate_credits.sh`, all caught. Clean under AddressSanitizer and UBSan. Real-time runs under SDL's dummy
drivers: uninterrupted 52 s and `credits: ended`; Space (scripted port 0x60) at 6 s ends at 6 s; Esc ignored;
title -> menu -> credits -> menu through `--scene game`.

**Pace (operator ran it on 2026-10-16 and found it right: the wipe and the loop need no change):** the original has no wait anywhere in the credits, so its speed was the
CPU's. The wipe runs at 100 steps per second (`TK_CREDITS_WIPE_HZ`, 2 s) and the loop at 35 passes per second
(`TK_CREDITS_PASS_HZ`, the text moves 35 rows per second, 51 s for the whole credits). Still the port's own choice, not a measurement of the original.

**Sound (2026-10-16):** song 0 of `MUC.ALL` plays under the credits, as in the original (`CAST.SL` reads 20,000 bytes
at offset 2 and starts the song after loading its files, before the loop; the song is the first 8,996 bytes of them).
It starts when the loop does and stops when the credits end, by time, by key or by closing the window. It is the
first song that uses the OPL rhythm mode (see "Milestone 5b"). `--no-music` and `--music-gain` apply. Nobody has listened
to it yet. Not read: the sound start-up `0x10c1e`, the song start `0x10c58` and the poll `0x10d1a`, which each pass makes;
the port starts the song at the loop's first pass and does not model the poll (the original restarts a song that has
ended, as the intro's poll does; the credits last 51 s here and the song 88 s, so it never ends under them).

**Open:** the old picture the wipe rolls away is the menu's last frame; if the menu closes its window before, the
credits start from whatever `scr` holds. Mouse: the original reads none here, so the port ignores it.

## Milestone 7a: WALK.SL's first screen and scene loader (C), built 2026-10-17

`src/walkscene.[ch]` is the start-up loader (what `tools/walk_scene.py` models: scene number, maps, tiles, object sprites, sprite
banks, the 50 scene records) and `src/walk.[ch]` draws the first screen: the 224 x 200 world buffer (ground, villagers / party /
hero in y order, object sprites, rain), its middle 192 x 168 copied to screen (64, 16), the sixteen button labels from the
`NUMBER.LCZ` font (each drawn three times for the engraved look) and the two pieces at the bottom right. Both are plain C with no
clock, no input and no SDL, like `menu.c` and `credits.c`.

- **Run it:** `build/3k --scene walk --data original_bin` shows the first screen until the window is closed. `--scene game` now
  goes title -> menu -> (new game) -> this screen. Headless: `build/3k_headless --scene walk --step-dump N:FILE` (raw dump of the screen after pass N; 0 is the first
  screen, see Milestone 7b for the later passes) or `--ppm FILE`. `--screen-from FILE` starts over a screen dump instead of the menu's first frame.
- **What it was (superseded by 7b, where the world moves, and 7e, where the song plays):** the game. Nothing moves, nothing is played (WALK.SL's own song is located by the loader but not started),
  no input is read, and there is no hand-over of the menu's last frame (the port uses the menu's first frame; outside the window the
  two are the same picture, which the check confirms: the first screen built over the menu's pass 3 and over its first frame is identical).
- **New game:** the original writes `__1.NOW` and `MAN_INFO.NOW` from `__1.AL1`, `__1.AL2` and `MAN_INFO.ALL` first. The port does not;
  it reads the two state files as they are in the data directory (the shipped ones are the state a new game writes, `findings.md` "WALK.SL").
  Pointing it at a played game's state files shows that game's scene instead. **Load** is not wired (the saves are not read).
- **Out-of-range data:** the original reads whatever memory follows; the port reads zeros for a tile or sprite number outside the data
  (the buffers are padded), drops pixels outside the world buffer, and refuses to load a state in which a record writes outside its map
  (nothing shipped does any of these).

**Checked** (`tools/check_walk.sh`):
- the first screen against the original's screen at BIOS tick 21, started over the port's own menu screen: **identical** (64,000 pixels
  and the palette);
- the loader's state (maps, tables, tiles, sprites, banks, list, trail) against the Python model for all 53 scene numbers on the
  shipped files and 100 randomised state files: **130 identical, 23 refused by both**. The model is the one `walk_scene.py` checks against
  the oracle, so this is a chain: C = model = original. The scene 0 buffers were compared with the original through the model only;
- `tests/test_walk.c` (hand-worked flags, rain, positions and offsets, draw order, ground / objects including odd and negative view centres,
  entities on a made-up scene, label sprites, scene 0's values, the first screen's extent, missing data). `make test` passes;
- `tools/mutate_walk.py`: **113 deliberate bugs, all caught** (63 by the unit test, 19 by the first screen, 31 by the loader comparison).
  Mutations that no input can show are left out (listed at the end of the file). My first run caught 103 of 114; the missed ones were the
  odd-x and negative object columns, a tile number above 1023, the 25th tile column (outside the window copy), the villagers' bank switch and
  the `0x61` sprites, the party's limit and its lack of a box, and some loader record rules. I added the tests and widened the comparison.
  One of those new tests had my own arithmetic slip (a subtraction in hex), found because the test failed on correct code.
- clean under AddressSanitizer and UBSan (unit test, first screen, loader comparison).

**Not checked against the original** (read from the code, as in `findings.md`): odd view centres, other scenes' drawing (only their loader
state is compared with the model), the `0x61` villager sprites, the second weather mode, the rain stride after the first frame, the
hero's walk counter other than 0, the tick-by-tick palette changes from tick 23. The real-time chain from the menu to this screen was not
run end to end (a scripted Space on the title goes to the intro here); `--scene walk` was run under SDL's dummy drivers.

## Milestone 7b: WALK.SL's world in motion, frame 21 onward (C), built 2026-10-17

`walk_pass` in `src/walk.[ch]` runs one pass of WALK.SL's main loop with nothing pressed (`findings.md` "WALK.SL step 6"): the animated
ground tiles and objects step, the palette shimmers, the world is drawn and its window copied, the 20 villagers each take a step (and
sometimes pick a direction with `rand()`), and the hero's walk frame counts. The weather follows the BIOS clock: rain with a growing then
shrinking stride for the first 18.75 minutes of each 60, dry for the next 18.75, dark specks for the last 22.5. `walk_first_screen` is now
`walk_begin` (interface, `walk_reset`, pass 0), so the first screen is the first pass.

- **Run it:** `build/3k --scene walk --data original_bin` (also reached from `--scene game` through the menu's "new game"). It runs at
  `TK_WALK_PASS_HZ` = 35 passes a second (a choice, `src/timing.h`: the original has no pacing; the operator found the animation right by eye, 2026-10-17) and a BIOS clock of 18.2065 ticks a second.
  `--walk-clock N` sets the clock at the first pass (default 0, as in the oracle's runs: rain from the start; `--walk-clock real` takes the
  time of day, which will usually be in the dry or the specks part of the hour). Close the window to stop.
  Headless: `build/3k_headless --scene walk --step-dump N:FILE` writes the screen after pass N (clock = start + N / 3, the convention of the
  oracle runs).
- **What you will see move:** the pond's water tiles (23 animated ground entries), the palette shimmer of entries 1-14 and entry 15, rain
  (strength changes every 1,024 ticks = 56 s), the hero changing frame every 6th pass **while standing still** (the original does the
  same: the counter runs without any key), and nothing else on screen: the villagers of scene 0 are all outside the window at the start,
  and the hero cannot be moved yet. Their steps are real (state compared with the original) but invisible until the view reaches them.
- **Not built:** keys and mouse, the party loop (`0x120a3`), hero-villager contact (`0x2c0da`), the special return of `0x2bccd` (reported once
  on stderr and otherwise ignored), weather mode 2, other scenes, WALK.SL's song, the ten actions.

**Checked** (`tools/check_walk.sh`, about 75 s; `IDLE=0` skips the idle part):
- the Python model (`tools/walk_idle.py check`) and the C port (`walk_idle.py cross`) against the original's passes in the oracle at seven
  clock readings, 5,593 passes in all: the screen, the palette and the state the screen does not show (villager table, rand state, rain list,
  specks list, palette buffer, both animation tables) **identical after every pass**. The clock readings give rain of strength 0, 4 and 9, rain
  that stops at pass 768, a dry clock, specks that start at pass 768 and specks all along;
- `tests/test_walk.c`: the new pieces on hand-worked values (rand, stride at each boundary, both animation tables, rotations and the
  entry-15 ramp, specks and their wraps, the villager step in all four directions, limits, speed gate, odd positions, the 100th-place quirk, the
  `0x61` timer, the direction choice) and a pass-level test that a pass steps the villagers in order and gives each decision the hero's
  position. `make test` passes;
- `tools/mutate_walk.py`: **192 deliberate bugs, all caught** (122 by the unit test, 18 by the first screen, 22 by the idle comparison, 29 by
  the loader comparison, 1 by a hang). My first run (screen and palette only in the idle comparison, 40 state files for the loader) missed 13: three were
  villager wiring that never shows on the screen (every villager of scene 0 is outside the window), so I added the hidden state to the
  comparison (`walk_state_dump`) and a pass-level unit test; nine were missing unit cases (a limit at equality, distinct x / y limits, both
  coordinates odd, an overshot speed counter, a tile past the last animation tile, a second object entry, the ramp's bottom, the direction
  rule's x bound, and an entry after the animation list's end: my first test of that toggled a tile an even number of times and so saw
  nothing); one was a loader mutation that needs the default 100 state files. The idle
  traces are also used whole now, since the entry-15 ramp only turns after 896 passes. The mutation list covers the new code (about 80 lines).
- clean under AddressSanitizer and UBSan (unit test; idle comparison at four clock readings).

**Limits of what is checked:** only scene 0, only its villagers' idle behaviour (they stay outside the window, so the *drawing* of walking
villagers rests on step 2's frames, not on these passes), and only 1,500 passes at most per clock reading. The animation tables have 23 + 2 live
entries (the 30 / 40 entry limits come from the code). The specks write past the end of the 224 x 200 buffer in the original (into whatever
follows); the port gives the buffer 0x400 more bytes. Weather mode 2 draws nothing. `walk_draw_world` is gone: the pass draws the layers itself.

**A note from the mutation run:** the list of distinct words loops until it finds a new value. With a wrong rand multiplier (a deliberate
bug) it never finishes; the run counted the hang as noticed. With the right generator it ends after 303 draws.

## Milestone 7c: the hero walks (C), built 2026-10-17

`walk_pass` now takes the keyboard port's value: `build/3k --scene walk --data original_bin` reads it from the window (`platform_port60`), so
**the four arrow keys (and the keypad 8, 2, 4, 6) walk the hero** around scene 0. One step is a half tile (4 pixels) and one is made every third
pass (35 passes a second, so about 12 steps a second); the whole picture scrolls with the hero, who stays at the same place on the screen, as in
the original. Walls stop him and shake the window for two passes. The party's followers (none are drawn in scene 0) follow the trail.
Headless: `--walk-key N:CODE` (repeatable; the value set at pass N holds from there) with `--step-dump N:FILE`.

- **What happens at a scene record:** the pass after a step onto the position of one of the scene's 50 records does nothing else (as in the
  original), and the record loop runs again on the live state. The house's door (x 38 and 39, y 44 and 45 in scene 0) is such a place; walking
  sideways over it changes nothing you can see. Walking up into the door or down out of it takes the roof off or puts it back (7d below).
- **Not built:** sliding round corners (the A key), the no-clip test mode, every other key, the mouse, the F-key actions, hero-villager contact,
  the other record kinds named in `findings.md` "WALK.SL step 7". Any of these that is reached is reported once per pass on stderr.
- **Pace:** the hero's speed is the loop's: 35 passes a second, found right by the operator for the idle world. The operator confirmed that walking
  with collision works (2026-10-17) and did not ask for another speed, so a step every third pass stays.

**Checked** (`tools/check_walk.sh`, about 2.5 minutes; `IDLE=0` skips the idle and walking parts):
- eight key scripts (five random, three yo-yoing around the door) of 900 to 1,100 passes at five clock readings, and the seven idle traces of
  7.2: after every pass the screen, palette, villager table, party table, trail, rand state, lists and the hero's counters are **identical** to
  the original's in the oracle, for the Python model and for the C port. Further scripts run during the work (21 sequences, about 18,000 passes
  in all) agreed too;
- `tests/test_walk.c` adds the footprint test cell by cell, the trail shift and the followers' entries, the record lookup (first match, the
  low bit, kind 5 under odd coordinates), and runs on scene 0: a held Right key's positions pass by pass (including the pass the record hit
  takes out), releases and the break code of another key, the four directions, the bump (state and rand), an unbuilt key and the unbuilt roof;
- `tools/mutate_walk.py`: **249 deliberate bugs, all caught** (the 59 for the walking code and the 192 earlier). The first full run caught 243
  of 250; the seven that survived were: the record loop's re-entry, which record kinds are reported, a boundary of the arrow-key range, the
  repeat rule for the keys 1, 2 and A, the 0xe0 prefix, the shake's divisor (the unit test had no value at the edge of a bucket), and "the roof comes
  out" (an unreachable branch until the roof toggle is built, so that mutation was dropped). I added unit tests for the first six (and the key
  scripts now send the 0xe0 prefix and the break codes of other keys), and re-ran those mutations and the groups they belong to: all caught. The
  full list was not run again in one go after that.
- **A test bug the sanitizers found:** `walk_begin` reloads the scene, so a `walk_scene_t *` taken before it is dangling; my first version of the
  walking test read freed memory and passed anyway. AddressSanitizer caught it; the test now takes the pointer again after each `walk_begin`
  (and one test that changed a record before reloading had been testing nothing). The unit test, and the comparison for five of the key
  scripts, are clean under AddressSanitizer and UBSan.


## Milestone 7d: a house's roof (C), built 2026-10-17

Walking **up through the door** of the first house (right of the start, x = 38, y = 44 and up) takes its roof off: the object map's roof rectangle is saved
and cleared, so you see into the room, and the rain stops (the weather mode goes from 1 to 4, which draws neither rain nor the dark specks).
Walking **down** out of it puts the roof back, shakes the window once, and the rain returns. `walk_roof` is the copy, `hit_record` the rule (`src/walk.c`).

- **Checked:** `tools/check_walk.sh` now has three `door` scripts (at the clock readings 20, 4096 and 45056) and two `route` scripts (a tour of the streets
  from the start to (86, 73) and back, planned with the model): after every pass the screen, palette, villager and party tables, trail, rand state, counters,
  the roof flag and the weather mode are **identical** to the original's in the oracle, for the model and for the C port. `tests/test_walk.c` adds the roof copy on a
  made-up map (rows, the flag byte, the saved layout, empty and invalid rectangles at each edge) and the door on scene 0 (the cells are 0xff inside, the weather is 4,
  and after the way back the rectangle is identical to the start and the weather is 1).
- **Mutations:** `tools/mutate_walk.py` has 276 deliberate bugs (28 of them for the roof). The first full run caught 274; the two that survived (a roof
  rectangle of a single row, and the right edge of the map at column 128) were gaps in the unit test; I added both cases and re-ran the roof group:
  all caught. The full list was not run again in one go after that. AddressSanitizer and UBSan are clean on the unit test and on the door and route scripts.
- **Not built:** the change of music a roof makes (`0x1c68e`; there is no WALK.SL music yet), and the other three roofs of scene 0 were not walked through
  (their doors are locked at the start; the code is the same).


## Milestone 7e: the scene's music (C), built 2026-10-17

`build/3k --scene walk` now plays the scene's song while you walk: the one the map names (scene 0: `MUC.ALL` `0x1e085`), started with the first screen, looping
until the window is closed, with the other scenes' flags (`--no-music`, `--music-gain`). The player is the one built for the intro, credits and menu, so no new
music code: only `play_walk` in `src/main.c` starts and stops it.

- **Checked:** `tools/check_walk.sh` (new part 5, about 15 s, `MUSIC=0` skips it) runs WALK.SL in the oracle with the AdLib model for 1,250 BIOS ticks and compares the
  whole first pass of the song with the player's register writes: **6180 of 6180 song ticks identical**. `tests/test_walk.c` checks that the loader's song parses
  (2,410 events, 96 ticks a second, end at tick 6180) and that a player can be made from it. Rendered through SDL's disk audio driver for 6 seconds, the walk scene
  gives sound with about the same level as the other scenes (peak 15,276; RMS 2,954).
- **Differences from the original:** the song starts again at once at its end (the original's takes about one main-loop pass, 83 ms in the oracle); the song starts
  with the first screen, not 0.6 s earlier (the original starts it while the scene is still loading); the digital sound effect a roof makes on a machine with a
  Sound Blaster is not built (`findings.md` "WALK.SL step 9").
- **Not looked at:** how loud the music is against the original (the player's level is the one the operator found right in the other scenes).

## Milestone 7f: shots and enemies (C), built 2026-10-17

`build/3k --scene walk` now fires with **Space**: each party member who can shoot sends a small shot the way he faces; it zigzags along its line, sparks
where it hits a wall or an enemy, and an enemy hit often enough goes up in smoke, comes back at a random open place out of view, and adds a coin to
the count at the bottom right. Touching an enemy starts a battle in the original; the port does not have battles, so it stops the pass there and reports
it (`walk: not built: a battle: ...`). Scene 0 has no enemies; to try it, start in scene 1 with `tools/walk_scene_data.py DIR 1 106 33` and
`build/3k --scene walk --data DIR`. What the original does is in `findings.md` "WALK.SL step 11".

- **Code:** `src/walk.c`: `space`, `shots_pass` (before the objects), `sparks_pass` (after them), `walk_shots_move`, `walk_shot_hit`, `respawn`,
  `walk_draw_gold`, the contact test after the window copy; `src/walkscene.c` loads the smoke frames (`RPG.LCP` `0x1a4cc`). Two fixes to earlier pieces:
  the smoke timer returns 0 (it returned the timer, which asked for a new direction at 10), and the hero's frame wraps as in the original (`> 3` gives 0,
  not `& 3`), because a respawn leaves its bounds in the hero's counter and frame.
- **Checked:** `tools/check_walk.sh` part 4b (about 18 minutes, `SHOTS=0` skips it): six key scripts in the oracle from starts in scene 1 and scene 0
  (`tools/walk_keys.py volley` and `shoot`, 600 to 1,500 passes, four clock readings) against the C port **at every pass: screen, palette, villagers,
  party, trail, rand state, locals, the 60 bytes of shots and sparks and the coin count all identical**. Over the four volley runs the enemies' hit counters
  changed 78 times and 12 enemies were destroyed, smoked and respawned (the start at (80, 52) never hits: its shots end on walls). Then a start on an enemy: the original leaves the loop after pass 0 and the port reports the battle at pass 0.
  `make test` (new unit tests for the shot movement, the hit counter and the coin digits); ASan/UBSan clean over a 1,499-pass run.
- **Not built:** the battle; the sound effects (spark frame 2 and a destroyed enemy, Sound Blaster only). `tools/mutate_walk.py` was not extended or re-run.
- **The Python model too (2026-10-18):** `tools/walk_idle.py` now has the shots, sparks, hit test, smoke, respawn, coin count and the touch (its run ends
  where the original leaves the loop), and `Idle(data=...)` takes the whole scene from `tools/walk_scene.py` (tiles, sprites, banks, tables, weather) instead of
  scene 0's fixed offsets, so it runs in scene 1 (`WALK_DATA=DIR walk_idle.py check TRACE`). Part 4b now compares **both** the model and the C port with the
  original: all six scripts identical at every pass, including the shots and coins, and the model leaves the loop at pass 0 on the touch start. The idle,
  walking, door and route traces are still identical. Ten deliberate breaks of the model's shot code (zigzag phase, kill reported as a hit, the hit
  turning the enemy, the coin value, the sprite position, spark drift, the respawn's clobbered counter and its rand draws, no smoke sprites, ...) were
  all caught but one: the followers' random facing after Space, because every start has the hero alone in the party (the C port's checks have the
  same gap).
- **A harness bug found on the way:** `build/walk_dump` kept only the first 256 key arguments, silently; long key scripts lost their tail and the
  port seemed to miss a Space press at pass 907. It now takes 4,096 and refuses more.

## Milestone 8a: the battle's first screen and idle passes (C), built 2026-10-18

Touching an enemy in `build/3k --scene walk` now starts the battle instead of stopping: the window becomes the battle field (the world around the
hero doubled and laid back in perspective, shaded towards the top), the party strip with faces, HP and MP comes up, the battle's song starts, and
the monsters stand there jittering and changing frames while the field sways. Nothing can be chosen yet and the battle never ends. To try it:
`work/venv/bin/python tools/walk_scene_data.py DIR 1 106 36` and `build/3k --scene walk --data DIR` (that start has the party act first).
What the original does is in `findings.md` "WALK.SL step 13".

- **Code:** `src/battle.[ch]` (`battle_begin`, `battle_pass`, the pieces `battle_stretch_row`, `battle_shrink`, `battle_sway`, `battle_number`);
  `src/walk.c`: the touch (`touch`: the enemy moved away, the group), `walk_battle_view` (the world drawn by `0x19680` and doubled),
  `walk_rng`, `walk_palette_step`, `walk_battle_info`, `walk_begin_result`; `walk_pass` returns `WALK_PASS_BATTLE`. `src/main.c`: `play_walk` and
  the stepped mode switch to the battle; the music switches to `MUC.ALL` 0x2bad9.
- **Checked:** `tools/check_battle.sh` (about 15 seconds; the original's recordings are kept in `work/check_battle`): 10 runs in scenes 1, 34
  and 42, five monster groups, two clock readings and a walk into an enemy, **identical at every pass in screen, palette, rand state, the monster
  table, the sway, the acting member and the turn counter**. `tools/mutate_battle.py`: 30 deliberate bugs, all caught. `make test`
  (new `tests/test_battle.c`). ASan/UBSan clean. The battle song's register writes match the original's for 3,119 song ticks. The headless
  `--step-dump` agrees with the oracle at passes 0 and 50.
- **Fixed in the world on the way:** the shake starts at, and does not fall below, the map header's byte 3 (C and Python model); no effect in
  scenes 0 and 1.
- **Not built:** keys and mouse in the battle, the monsters' actions (about half of all battles start with them: the port then matches only up
  to their first action and says so on stderr), attacks, winning and losing, the way back, enemies that start with a dialog, the Sound Blaster
  sounds. Scenes whose map header byte 5 is 1 draw their world doubled, which the port does not do yet.
- **Pace:** 35 battle passes a second, the world's pace (the original paces neither). Not judged by eye yet.

## Milestone 8b: choosing a command and a target (C), built 2026-10-18

In the party's turn, Left and Right now move along the four command tabs, and Space or Enter on the first one (attack) puts a yellow box round
the first monster in front; Left and Right move the box between the live front monsters, Esc takes it away again. Confirming the attack, or
confirming one of the other three commands, is not built and is reported on stderr. What the original does is in `findings.md` "WALK.SL step 14".

- **Code:** `src/battle.c` (`keys`, `target_slot`, `can_target`, `box_outline`, the 0x96 case of `draw_monsters`; the key latch starts at the
  touched record's offset, as in the original); `src/walk.c` gives the record's offset (`walk_battle_info`); `src/platform*.c`
  `platform_escape_quits`, which `play_walk` switches off for the battle.
- **Checked:** `tools/check_battle.sh` (15 runs, about 15 seconds once recorded) identical at every pass, including the new command, choosing and
  latch bytes; `tools/battle_keys.py` writes the key scripts. `tools/mutate_battle.py`: 43 deliberate bugs, all caught. `make test`; ASan/UBSan
  clean. A real-time run under SDL's dummy drivers with scripted keys (`TK_TEST_INPUT`) chose a target, moved it, went back with Esc and
  reached the unbuilt magic command without closing.
- **Not built:** the mouse in the battle, keys 2 and 3 (music, sound), the attack, commands 1 to 3.

## Milestone 8c: the attack and the monsters' turn (C), built 2026-10-18

A battle can now be fought to its end with Space or Enter. A confirmed attack grows the hit pictures over the target with the member's attack
picture, the target flashes and, out of HP, dies behind the six masks (a monster from the back steps forward), numbers float up, the member
shouts; then the monsters take their turn one by one, each shown doubled in close-up before its blow, the field flashing and jumping, the hit
member's HP and face changing in the strip. When the monsters or the party are all down the original goes on to what follows a battle, which is
not built: the port stands still and says so on stderr (`battle: not built: the party has won ...` / `... lost ...`).

- **Code:** `src/battle.c` (`party_attack`, `monster_blow`, `hit_lands`, `draw_monsters` with the flash, dying, target and spark states,
  `double_up`, `stretch`, `add_number`, `set_shout`, and the pass in the original's order); `battle_over` in `src/battle.h`.
- **Checked:** `tools/check_battle.sh`, 23 runs (40 seconds once recorded; recording them all takes about 3 minutes), identical at every pass in
  screen, palette, rand, the monster table, 16 battle locals, the party table and the floating numbers; six of them are whole battles: three
  monster groups in scene 1, scene 34, a party of three, and a lost one. `tools/battle_keys.py --attack` writes their key scripts;
  `tools/walk_scene_data.py --join K --hp K:HP` makes the party. `tools/mutate_battle.py`: 69 deliberate bugs, all caught (the blow formulas
  and the slot pick are pinned by unit tests, since no start reaches their edges). ASan/UBSan clean. A real-time run under SDL's dummy drivers attacked twice.
- **Not built:** commands 1 to 3, the mouse, keys 2 and 3, the Sound Blaster samples. (What follows a win or a loss: milestone 8d.)
- **Pace:** the round's animations are counted in passes, so at 35 passes a second a party attack takes about half a second and a monster's
  close-up about a third of a second. Not judged by eye yet.

## Milestone 8d: what follows a win or a loss (C), built 2026-10-19

A won battle now ends the way the original's does and goes back to the world; a lost one ends in MARK.EXE's game-over screen and then the
title, as in the original.

- **Win:** the rewards bar (`5 x 001 = 0005`: kills, coins per monster, coins) bobs over the swaying field until Space, Esc or Enter; the
  coins are added at once (at most 50,000) and shown at the bottom right. Then each member who has reached his next level gets it (experience
  set to the threshold, the next one 500 higher, most HP a twelfth more, HP full) with a message box (`恭喜!恭喜!桑得斯的生命力增強了。`)
  that waits for a key. The scene's song starts again and the rest of the pass that touched the enemy runs (`walk_resume`); the world goes on.
- **Loss:** `play_walk` returns 2; `play_gameover` shows the game-over screen (the last screen shaded, shrunk and tiled 4 x 4, rolling up
  and left, with 全體陣亡) with song 24 until Space or Enter; `--scene game` then goes back to the title, `--scene walk` stops.
- **Code:** `src/battle.c` (`start_rewards`, `rewards_pass`, `level_ups`, `battle_level_up`, `battle_add_coins`, `start_message`,
  `message_char`, `dialog_pass`, `battle_glyph_offset`), `src/gameover.[ch]`, `walk_resume` / `walk_show_gold` in `src/walk.c`, `main.c`.
- **Checked:** `tools/check_battle.sh` (now four loop tops: world, battle, rewards, message box; world passes also compare the villager and
  party tables): the two won battles go on 1,276 and 1,699 world passes after the bar; level-ups with 1 and 3 message boxes (279 passes of
  waiting, palette steps included); the bar held 100 passes; a walk-in win across a rain-strength change; and `tools/gameover_check.py`, 400
  passes of the game-over screen from the screen a lost battle ends on. All identical. A walk-in at the clock 20300 also reaches a second
  battle, which starts with the first one's leftover `[bp-0x84]` (`walk_battle_left`). Unit tests pin the level-up and coin edges and the
  resumed pass's rain stride (`test_resume_stride` in `tests/test_walk.c`).
  `tools/mutate_battle.py` (`SKIP=n` leaves out the first n): 111 mutations, all caught.
- **Pace (choices, not measured):** the bar and the game-over screen run at the world's 35 passes a second (the original paces neither);
  a message box's wait runs 41 rounds a pass, one palette step a pass.
- **Not built:** fleeing (command 3's `rand() < 0x4000` path, which also reaches the bar; built in milestone 8e), what follows a battle with a byte-0x2f enemy
  (reported), the mouse, the Sound Blaster samples, ANI.LCP's message byte (the port passes the result directly).

## Milestone 8e: the other three commands: flee, magic, items (C), built 2026-10-19

All four command tabs now work with the keys (`docs/findings.md` "WALK.SL step 17").

- **Flee (fourth tab):** the turn passes to the monsters; one try in two gets away, to the rewards bar and the world as after a win.
- **Magic (second tab):** a list of the member's spells (MAGICT.LCC; those his level has reached, greyed when his MP is short) over the
  field; arrows move, Esc goes back, **key 1** casts. Spells on the monsters hit every live front one, with numbers, a shout and 19 passes
  of flashes, and then they flash and die as after an attack. Spells that heal, lower word 0x10 or revive open the member picker (arrows,
  Enter, Esc). Casting ends the member's turn.
- **Items (third tab):** a list of the member's 20 items on two pages (OBJECT.LCC); arrows move, **key 1** uses (food heals, potions add
  MP or lower word 0x10, worn things change places with what he wears and set his attack and defence), **key 2** turns the page, Esc goes
  back. Using things does not end his turn.
- **Code:** `src/battle.c` (`keys`, `magic_begin`, `magic_round`, `magic_picked`, `magic_end`, `pick_begin`, `pick_round`, `items_begin`,
  `items_round`, `item_use`, `text_pic`, `strip_numbers`, `next_member`, the spell flashes in `battle_pass`), `BATTLE_KIND_MAGIC`, `_PICK`,
  `_ITEMS` in `src/battle.h`, `main.c` (41 rounds a pass in the lists and the picker, as in the message box). `tools/walk_scene_data.py
  --byte` sets levels and items; `tools/battle_keys.py --flee` and `--all`; `tools/check_battle.sh` `RUNS=` runs only some starts.
- **Checked:** `tools/check_battle.sh`, 18 new starts (flee, cast, heal, revive, long stays in the list and the picker, two item pages,
  any key), all identical in screen, palette and rand in the lists, and in everything compared before in the battle and the world.
  ASan/UBSan clean on five of them. `tools/mutate_battle.py`: 46 new deliberate bugs in the new code, all caught (run with
  `RUNS` set to the new starts). The first run missed six, all for want of a start: no spell greyed between once and twice its level,
  no Down from the fourth row, no Space on a spell that can be cast, no heal on a fallen member or past the most HP, no spell on an empty
  front slot; four starts were added (`magic_walk`, `magic_heal_top`, `magic_heal_fallen`, `magic_casts`) and the six then caught. Three
  are left out with a note in the file because they cannot show with the game's data. Two older patterns that the new code duplicated
  were made unique and re-run (caught). The whole list (155) was not re-run in one go.
- **Three values the original never sets** (palette counters of the three loops) are taken from what was seen: 3 in the magic list and 0x98
  in the item list in every start (INFERRED constants), and in the picker the width (8 or 16) of the last character the list drew, which
  the original leaves at that stack address (FOUND by two starts that disagreed).
- **Not built:** the mouse in the lists and the picker; the picker face's 48-step countdown (only its last step is drawn); key 3 in the
  item list and the lists outside a battle (the world's F-keys); the Sound Blaster sample at a cast.

## Milestone 8f: the mouse in the battle (C), built 2026-10-19

With a mouse (the window's pointer stands in for the driver) the game draws its own pointer, in the world and in the battle, and the
battle can be played with it alone (`docs/findings.md` "WALK.SL step 18").

- **Fight:** the pointer over a tab chooses it, a left press confirms; while a target is chosen the pointer over a front monster makes it
  the target, a left press attacks; the right button is Esc; the music and sound buttons are keys 1 and 2.
- **Magic list:** a left press on a row lights it, on the icon casts; **item list:** a left press on a row lights it, on the icons uses
  the item, turns the page (or gives, outside a battle), on the left-hand buttons' corner leaves; **picker:** the pointer over a box
  chooses it, a left press takes it; the right button leaves each of them. Any button leaves the rewards bar; the right one closes a
  message box.
- **World:** only the pointer (on the screen and in the window); its buttons do nothing yet.
- **Code:** `src/battle.c` (`fight_mouse`, `ptr_hide` / `ptr_show` / `ptr_corner` / `ptr_move`, `press`, `items_end`, the mouse parts of
  the passes and rounds), `src/walk.c` / `walk.h` (`walk_set_mouse`, `walk_mouse`, `walk_mouse_grab` / `_put` / `_under` / `_pic` /
  `_hide` / `_reset`, the world's pointer), `main.c` (`feed_mouse`; the system pointer is hidden in the world). `tests/walk_dump.c` takes
  `m:PASS:X:Y:BUTTONS`; `tools/battle_check.py` `--mouse` and `--world-passes`, and the party table in the lists; `tools/battle_mouse.py`.
- **Checked:** `tools/check_battle.sh`, 18 new starts with a mouse driver in the oracle, all identical (and the 47 earlier ones);
  ASan/UBSan clean on three. `tools/mutate_battle.py` (now 195): 40 new mouse mutations, all caught against the mouse starts
  (`RUNS=mouse`). The first run missed seven for want of a start (the world's pointer outside the window, a held press onto a tab, a dead
  front place under the pointer, a box's edge under it in the picker, a row pressed in the pass the pointer arrives, a box opened with
  the right button held); six starts were added and all seven then caught. One is left out with a note (it cannot differ). Four older
  item and picker patterns were brought up to date and re-run against all starts (caught). The whole list was not re-run in one go.
- **Not built:** the world's buttons (walking and shooting with the mouse, the on-screen F-key buttons), the mouse in MARK.EXE's
  game-over screen. The "button still down" flags of the fight, the magic list and the item list are never set by the original: the
  lists' behaved as 0 when a press was held into them (seen once each), the fight's is taken as 0 (not seen).

## Milestone 8g: the world's mouse buttons (C), built 2026-10-19

In the world the left button walks the hero towards the pointer (a held press keeps walking, also over the buttons) and the right one
shoots; a press on an on-screen button is that button's key (F1 .. F10, 1 .. 4, A), whose action is not built yet, as with the keys
(`docs/findings.md` "WALK.SL step 19").

- **Code:** `walk_mouse_key` and the mouse part of `walk_keys` in `src/walk.c` (`walk.h`), the pointer placed with the shake.
- **Checked:** `tools/check_battle.sh`, eight new starts (pure walks, walks into battles and back, a drag onto the buttons, a release), all identical;
  `test_mouse_key` pins 26 places seen in the original and the edges read from the code. `tools/mutate_battle.py` (206): 11 new, all
  caught (four by the unit test), one left out with a note.
- **Not built:** the buttons' actions.

## Milestone 9a: F1, talking (C), built 2026-10-19

F1 (or its button) makes the hero talk to the villager he faces, up to three cells away: the world darkens, the button is pressed, the
view zooms in on the two of them and the villager's messages come one box at a time (Space, Enter or the right button for the next),
setting the flags the story uses (`docs/findings.md` "WALK.SL step 20").

- **Code:** `src/walk.c` (`talk_key`, `talk_begin`, `talk_round`, `talk_done`, `fkey_frame`, `fkey_shade`, `button_toggle`, `walk_kind`,
  `palette_cycle_on`), `src/msgbox.[ch]` (the message box, now shared with the battle's level-ups), `walk_flag_set` in `src/walkscene.c`;
  `tests/walk_dump.c` reports the world's message box as loop 4; `tools/walk_keys.py talk`.
- **Checked:** `tools/check_battle.sh`, 12 new starts, all identical at every pass; the battle's level-up runs unchanged after the box
  moved. `tools/mutate_battle.py` (224): 17 new talk mutations and the box's 16, all caught; two left out with notes.
- **Not built:** talks that sell or ask for an answer (reported); the other F-keys.

## Milestone 9b: talks that sell and ask (C), built 2026-10-19

A seller's text comes up with the "是 / 否" buttons pressed by turns (Left / Right or the mouse, Enter or Space to take it): yes buys his
item if the coins are enough. An asking villager's text comes with a five-digit box at the top (Up / Down to change a digit, Left /
Right to choose one, Enter), and the right number sets his flag (`docs/findings.md` "WALK.SL step 21").

- **Code:** `src/walk.c` (`yesno_open`, `yesno_round`, `talk_buy`, `ask_open`, `ask_round`, `ask_marks`, `talk_close`; the coins drawn
  at the start in `mouse_start`), `msgbox_open`'s `wait` argument; `battle_read_unpack`; `tools/battle_check.py` loops 8 and 9;
  `tools/walk_scene_data.py --talk`; `tools/number_keys.py`.
- **Checked:** `tools/check_battle.sh`, 17 new starts (102 in all), all identical at every pass. `tools/mutate_battle.py` (258): 34 new
  mutations, all caught; one left out with a note.
- **Not built:** the right answer's sound; the dark scenes where the asking villagers are (so they were checked in scene 0).

## Milestone 9c: the dark scenes (C), built 2026-10-20

In the 24 scenes whose map header byte 5 is set (6, 9 .. 20, 23 .. 26, 29, 37, 39, 43, 45, 47, 50) the world window shows the middle of
the view doubled: the hero's surroundings lit in an oval (RPG.LCP 0x4350), dimmed rows above and below it, and only every fourth pixel
of the rest. An F-key's drawing of the world there is doubled but not darkened. After a talk the scene records run again, as after
stepping on one (`docs/findings.md` "WALK.SL step 22").

- **Code:** `src/walk.c` (`walk_double`, `walk_double_dark`, the end of `walk_pass`, `fkey_frame`, `records_again`, `vbe`; the pointer
  drawn into the shown buffer in `mouse_pass`), the oval in `src/walkscene.c` (`dark_mask`); `test_doubling` in `tests/test_walk.c`.
- **Checked:** every dark scene idle and with walking and shooting keys, 300 passes each, all identical (a scratch survey); then
  `tools/check_battle.sh`, 19 new starts (120 in all), all identical at every pass, among them a battle in a dark scene, the mouse, F1 walks
  with 600 to 800 number-box rounds, four members joining (scenes 13, 37, 39, 43) and scenes 9 and 23's questions in their own scenes.
  `tools/mutate_battle.py` (273): 15 new mutations, all caught (6 of them by the unit test); two more left out with a note. ASan/UBSan clean on
  the dark starts; the quick `tools/check_walk.sh` is all identical.
- **Not built:** the world's fade-in at the start (built in 9d: it is a zoom, not a fade); the Python model
  (`tools/walk_idle.py`) has no dark scenes, since `check_walk.sh` runs only scenes 0 and 1.

## Milestone 9d: the world's start zoom (C), built 2026-10-20

Before the world's first pass the view comes in from its middle: a 24 x 24 part of the window grows to the whole window in 13 steps,
each scaled up to the window as spaced dots that close up (in a dark scene the lit doubled view). The scene's song now starts before it,
as in the original (`docs/findings.md` "WALK.SL step 23").

- **Code:** `src/walk.c` (`walk_zoom_step`, `walk_zoom_scale`, `walk_begin_shown`), `src/main.c` (`show_zoom_step`: one step a pass of
  the world's pace, about 0.37 s in all; the original does not wait between steps, so this pace is a choice); `tests/walk_dump.c`
  mode `zoom`; `tools/zoom_check.py`.
- **Checked:** `tools/check_battle.sh` (section "zoom"), all 13 steps identical to the original's screens in scenes 0, 1, 34 and the dark
  scenes 6, 9, 23 and 47; the screen does not change between the last step and the main loop. Unit test `test_zoom_scale`.
  `tools/mutate_battle.py` (281): 8 new mutations, all caught (4 by the unit test). The whole `check_battle.sh` and the quick
  `check_walk.sh` are still all identical; ASan/UBSan clean on the zoom checks.
- **Not built:** the zoom the other way (`0x199b4`, which shrinks the view) and the scene change that sets the zoom off again
  (`0x138c8`, `0x17b0b`).

## Milestone 8h: the rest of the battle (C), built 2026-10-21

Story enemies, the music and sound keys in the fight, the picker face's flash and the game-over screen's mouse (`docs/findings.md`
"WALK.SL step 24"). With this, every part of the battle is built except the Sound Blaster's sounds.

- **Story enemies** (byte 0x2f set; scenes 4, 18, 23, 35, 40, 41, 43): the opening message box (colour 0xc2) when word 0x11 is set,
  then the battle against group byte 0x30. All five killed: the enemy goes, its flag is set and the pass ends; fled: the party and its
  trail are put back at the record's bytes 0x28, 0x29.
- **Keys 1 and 2 in the fight:** with music playing the game has an FM card, so the music button starts pressed and key 1 (or the
  button) stops the battle's song or starts it again from its beginning. There is no Sound Blaster, so key 2 does nothing, as in the
  original without one.
- **The picker face's flash:** all 48 steps (three times 16 colours down), each shown for 10 ms in real time (a choice).
- **The game-over screen:** any mouse button ends it, as Space and Enter do.
- **Code:** `src/walk.c` (`touch`, `boss_round`, `walk_boss_after`, `walk_resume`, `walk_set_sound`, `walk_sound_buttons`,
  `walk_button_toggle`), `src/battle.c` (`keys`, `sound_button`, `magic_picked`, `battle_set_show`), `src/gameover.c` (`gameover_mouse`),
  `src/main.c`; `tests/walk_dump.c` (`fm`, `fm-shown`, `f:PATH`, keys and mouse in `gameover`); `tools/battle_check.py --opl` and the
  flash steps; `tools/gameover_check.py --key`, `--mouse`.
- **Checked:** `tools/check_battle.sh`, 28 new starts and checks (149 in all, about 11 minutes once recorded), all identical; unit test
  `test_boss_after`; `tools/mutate_battle.py`: 23 new mutations, all caught; ASan/UBSan clean on six of the new starts; the quick
  `tools/check_walk.sh` all identical; a real-time run closed a story enemy's box and fled. The whole mutation list (303) has not been run
  in one go. (Run in 8i, 337 by then: all caught.)
- **Not built:** the Sound Blaster's sounds (read in step 24; nothing on screen depends on them). (Built in 8i.)

## Milestone 8i: the Sound Blaster's sounds (C), built 2026-10-22

With a sound device the game now has a Sound Blaster as well as the FM card (`docs/findings.md` "WALK.SL step 25"): `SND_EF.1` and
`_1_.LOC` are read, the sound button starts pressed, and the battle's and the world's sounds play at the original's 12 call sites.
Key 2 or the button turns them off and on in a battle. This finishes milestone 8.

- **Code:** `src/sfx.c` (new: the two files, a block's length in passes, decoded clips kept), `src/voc.c` (`voc_adpcm_decode` for
  codecs 2 and 3, `voc_block_decode` for bare blocks), `src/walk.c` (`walk_set_sfx`, `walk_sfx_play`, `walk_sfx_busy`,
  `walk_sfx_stop`; shots, sparks, roofs, a right answer), `src/battle.c` (`member_sound`; the touch, blows, hits, deaths, the
  rewards bar's cut), `src/main.c` (the sink: a sound replaces any that plays); `tests/walk_dump.c` (`sb`, `sb-shown`, `s:PATH`);
  `tools/sb_stub.py` (new) and `tools/battle_check.py --sb`.
- **Checked:** 11 new starts in `tools/check_battle.sh` (all its starts identical, sounds included); the decoders give the same samples
  as DOSBox-X's tables for all 12 blocks; unit tests in `tests/test_audio.c`; 34 new mutations, all caught; ASan/UBSan clean on five
  starts; a real-time run with `--no-music` under SDL's disk audio driver wrote sound.
- **The whole mutation list (337), 2026-10-23:** run through once (resumed after mutation 221 when a restart stopped it), all caught. Four
  patterns had gone stale (three after `button_toggle` became `walk_button_toggle`, one after 8h's flash); they were brought up to date
  and re-run alone (caught).
- **A choice:** a sound's length in passes (35 a second) stands for the card's busy word in the comparisons; in the game it is the sink
  still playing. Nobody has listened to the sounds.
- **Not built:** the sounds of actions not built (the six F-key handlers' `SND_EF.1` 0x1bf5, a scene record's 0x5571, `0x22c42`'s).

## Milestone 9e: F2, looking (C), built 2026-10-24

F2 (or its button) makes the hero look at a sign: its own cell and the next one across, up to three steps the way he faces, are
searched for a scene record of kind 9. Found: its picture from `SPIC.LCZ` in a red frame over the window (when it has one), then its
message boxes (`docs/findings.md` "WALK.SL step 26").

- **Code:** `src/walk.c` (`fkey_begin`, now shared by F1 and F2; `look_key`, `look_picture`, `look_open`, `look_round`, `look_step`,
  `look_done`, `walk_look_record`, `walk_look_entry`; `talking` 4), `src/walk.h`; `tools/walk_keys.py look`.
- **Checked:** `tools/check_battle.sh`, 22 new `look_` starts, all identical at every pass; F1's starts unchanged. Unit test `test_look`.
  `tools/mutate_battle.py` (364): 27 new mutations, all caught; one left out with a note (cannot differ with the shipped data).
  ASan/UBSan clean on five starts; a real-time run looked twice.
- **Found, then fixed the same day:** the view past the map's edges (findings step 27): below the map's bottom, reachable in nine
  scenes, the original draws what follows the ground map in its segment (the villagers' sprite bank, as tile numbers that can wrap into
  the object sprites), not plain ground. `walk_draw_ground` and `walk_draw_objects` now work out their offsets in bytes and 16 bits as
  the original does and read the segment's layout past the maps (`walk_seg_byte` in `src/walkscene.c`, `art_byte` in `src/walk.c`;
  `walk_zoom_keep`). 11 new `edge_` starts identical; 15 new mutations caught, two left out with notes. The allocator's header bytes are
  the oracle run's (a GUESS; only unreachable places read them).
- **Not built:** F3 to F10, keys 1 to 4 and A.

## Milestone 9f: F3, taking (C), built 2026-10-24

F3 (or its button) makes the hero take what lies in his cell or the next ones across, up to three steps the way he faces: an item goes to
the first free item place of a member with HP left, coins to the purse (at most 60,000). Its name or the sum comes up in a shadowed
strip with a sound until Esc, Space, Enter or the right button; with no room, message 11 (`docs/findings.md` "WALK.SL step 28").

- **Code:** `src/walk.c` (`take_key`, `take_strip`, `take_round`, `take_full`, `take_box_round`, `take_done`, `walk_take_slot`,
  `walk_record_of_kind`, `walk_sfx_wait`; `talking` 5 and 6), `battle_text_pic` out of `src/battle.c`; `walk_sfx_t` has a `wait`
  (`src/main.c` waits for the sink, `tests/walk_dump.c` logs it); `tools/battle_check.py` loop kind 10; `tools/walk_keys.py take`.
- **Checked:** `tools/check_battle.sh`, 19 new `take_` starts, all identical at every pass. Unit tests for the search and the item place.
  `tools/mutate_battle.py` (413): 34 new mutations, all caught; one left out with a note. ASan/UBSan clean on six starts.
- **Not built:** F4 to F10, keys 1 to 4 and A.

## Milestone 9g: F4, casting (C), built 2026-10-24

F4 (or its button) picks who casts, then opens his magic list over the world: heals, defence spells and revives are cast on a member as
in a battle; spells on monsters do nothing there (`docs/findings.md` "WALK.SL step 29").

- **Code:** `src/battle.c` (`battle_world_cast`; the picker and the list with a world flag, `step_palette`), `src/walk.c` (`cast_key`,
  `cast_round`, `cast_done`, `walk_palette_step_dim`, `walk_set_show`, `walk_cast`; `talking` 7), `src/main.c` (Esc closes the window
  only in the world loop), `tests/walk_dump.c`; `tools/walk_keys.py cast`.
- **Checked:** `tools/check_battle.sh`, 14 new `cast_` starts, all identical at every pass. `tools/mutate_battle.py` (427): 14 new
  mutations, all caught; three left out with notes. ASan/UBSan clean on four starts.
- **Not built:** F5 to F10, keys 1 to 4 and A.

## Milestone 9h: F5, items (C), built 2026-10-24

F5 (or its button) picks a member and opens his item list over the world: key 1 uses an item, key 2 turns the page, and key 3 (the
third icon) gives the lit item to another member (`docs/findings.md` "WALK.SL step 30").

- **Code:** `src/battle.c` (`battle_world_items`, `battle_world_result`, `item_given`; the item list's world branches), `src/walk.c`
  (`items_key`; `cast_done` lets F5's two buttons go); `tools/walk_keys.py items`.
- **Checked:** `tools/check_battle.sh`, 13 new `items_` starts and a battle start for key 3, all identical at every pass.
  `tools/mutate_battle.py` (445): 19 new mutations, all caught; one left out with a note. ASan/UBSan clean on three starts.
- **Not built:** F7 to F10, keys 1 to 4 and A (F6, the equipment screen: 9i).

## Milestone 9i: F6, the equipment screen (C), built 2026-10-24

F6 (or its button, or F6 in the item list) picks a member and shows the six things he wears with their numbers; Enter takes the lit one
off into his items, F5 goes to the item list and back (`docs/findings.md` "WALK.SL step 31").

- **Code:** `src/battle.c` (`battle_world_equip`, `equip_begin`, `equip_round`, `equip_end`; the item list's F6), `src/walk.c`
  (`items_key` for both keys); `tools/battle_check.py` loop kind 11 (`0x2256a`); `tools/walk_keys.py equip`.
- **Checked:** `tools/check_battle.sh`, 14 `equip_` starts, all identical at every pass; `tools/mutate_battle.py` (486): 41 new
  mutations, all caught. ASan/UBSan clean on three starts.
- **Not built:** F7 to F10, keys 1 to 4 and A.
