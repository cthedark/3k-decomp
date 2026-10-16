# docs/ index

These documents are the project's memory. They are written for two readers:
the human learning the process, and any AI agent (or person) picking the work
up later with no other context. Start here.

## Read in this order
1. `../README.md`: the goal, scope, legal note, plan.
2. `findings.md`: the evidence log. Append-only. Newest entries last. Later
   entries may correct earlier ones. Always read to the end.
3. `files.md`: what each file in `original_bin/` is, with confidence.
4. `formats.md`: file formats, one section each, with a decoded/partial/unknown status.
5. `code.md`: what the program code does, by program and address.
6. `port.md`: the native C reconstruction (`../src/`): how to build, layout,
   milestones and their evidence.
7. `environment.md`: how tools were installed and what is missing.
8. `concepts.md`: short explanations of the terms used.
9. `original_checksums.sha256`: SHA-256 of every original file.

Tools live in `../tools/` (standard library only unless noted; `work/venv` is the Python with Capstone and
Unicorn; every tool's header says how to use it). Output derived from the game goes under `work/`.

| Area | Tools |
|---|---|
| Run and trace the original | `run-original.sh` (DOSBox-X, `--trace` logs file I/O); `dosemu.py` (the oracle: runs an unpacked program in a small emulator and saves its screen; options `--log-files`, `--snap-hit`, `--peek`, `--trace-calls`, `--dump-local`, `--dump-hit`, `--screen-from`, scripted `--key`, `--palette-from`, `--log-vga`; needs the venv); `capture_original_audio.sh` (record what the original outputs in DOSBox-X, headless) |
| Read code | `dis16.py`, `calls16.py` (disassemble; fold into calls with arguments; need the venv); `mz_strings.py` (strings with addresses) |
| Pictures and sprites | `lcz_decode.py`, `lcz_sheet.py`, `lcz_at.py` (LCZ containers, also a block inside another file such as `RPG.LCP`); `buf_sheet.py` (contact sheet of raw buffers); `render_font.py` |
| Compare screens | `compare_frames.py`, `compare_set.py`, `compare_raw.py` (raw dumps of 64,000 pixels + palette); `check_intro1.sh`..`check_intro6.sh`, `check_menu.sh`, `check_credits.sh` (re-run a scene against the original frame by frame) |
| Voice and music data | `voc_extract.py`; `cmf_survey.py`, `cmf_behaviour.py`, `cmf_to_mid.py` (songs of `MUC.ALL`, also as MIDI) |
| Recorded audio | `audio_timeline.py`, `find_clip.py`, `clip_scale.py`, `loop_period.py`, `onset_fit.py`, `pitch_check.py` (has a self-test), `emu_baseline.py`, `compare_music.py`; method and pitfalls in `findings.md`, 2026-10-15 |
| Timing | `fit_timeline.py` (split event times into clock-bound and CPU-bound parts) |
| Check a model of CAST.SL's loop against the oracle's buffers | `cast_buffers.py` (loop) and `cast_wipe.py` (start-up wipe); they need dumps in `work/cast/buf`, see their headers and `findings.md` |
| Mutation checks (do the tests fail when the code is wrong?) | `mutate_audio.sh`, `mutate_music.sh`, `mutate_menu.sh` (needs `check_menu.sh` run first), `mutate_credits.sh` (needs `check_credits.sh` run first) |

## Current state (update this section each session)
- Done: PKLITE unpacking of 5 programs; Ghidra loads MARK and TV; the original
  runs in DOSBox-X; two file-I/O traces captured and matched to operator
  actions (skip-slideshow run, full-slideshow plus load-game run).
- Decoded formats: font (`NUMBER.LCZ`), voice clips (`STORYn_V`), picture
  containers (`*.LCZ`: full screens, sprites, animation frame stacks, masks;
  about 6 chunks unresolved), subtitle bitmaps (`STORYn_T.LCZ`), palettes
  (offset 0 of `RPG.LCP` and `ANI.LCP`). Partly known: dialogue text
  (`MES_TXT.LCC`), saves.
- Program flow known at file level: `MARK.EXE` (title) -> `STORY2.SL`
  (7-slide intro) -> `MARK.EXE` again -> `WALK0.SL` (menu, load) -> `WALK.SL`
  (game world). These are chained programs started by `execl`, not
  call-and-return overlays. `TV.EXE` is a one-time CPU speed test; `MARK`
  runs it only if byte 0 of `RPG.LCP` is above 10 (ours is 2), so it has never
  run in a trace. See `code.md`, "Program chaining".
- Code analysis started with `MARK.EXE` (see `code.md`): plain mode 13h
  (the Mode X routines in the programs are never called), DOS file wrappers,
  mouse and palette helpers identified.
- Native port (`port.md`): milestones 1 and 2 done. `make` builds `build/3k`, which plays
  the title screen. Its output is pixel-identical to the original's machine code run in
  `tools/dosemu.py` (22 frames compared, 3 exit decisions). Milestone 3 (the intro
  slideshow, `STORY2.SL`) is in progress: slide 0 (star field and seven subtitle lines,
  `build/3k --scene intro0`) matches the original in 197 compared frames, slide 1 (cliff
  scene, `--scene intro1`) in 166 more and slide 2 (road through the cliffs, `--scene intro2`)
  in 111 more, slide 3 (sinking figure, `--scene intro3`) in 298 more and slide 4 (eyes and
  sparkles, `--scene intro4`) in 352 more and slide 5 (woman in red, `--scene intro5`) in 190
  more; the operator judged slides 0-5's pacing right. Slide 6 (red robe, `--scene intro6`)
  matches in 440 more, including the final fade to black, so all seven slides of the intro are built
  and compared; `--scene intro` plays them in a row. Voice-over and slide 0's sound are built
  (2026-10-15); the operator confirmed subtitle and voice sync by listening; slide 0's sound was accepted by the operator; the slide 0 restart question is open; the FM music
  (ymfm + CMF player, `port.md` "Milestone 5") is built and tested; the operator confirmed by listening (2026-10-15) that it plays along with the voice. Milestone 4 (`--scene game`: title -> intro -> title, with music in both) was built the same day and has not been looked at by the operator; the menu is not built. The timing policy is fixed durations (`port.md`).
- Not done: no code analysed in the overlays; no function renamed or
  reconstructed; subtitle and picture formats unknown.
- Operator note: the operator's keyboard has no function keys. Use the mouse on
  the on-screen buttons, and close DOSBox-X with the window close button.

## Next steps (ordered)
1. (done) Voice clips confirmed by listening.
2. Finish the picture formats: find the frame sizes in the code (they are not in
   the files), resolve `BOAT`/`CAST2`/`SPIC`, find the other palettes, and
   decode the rest of `RPG.LCP`/`ANI.LCP`.
3. (done 2026-10-14) How `MARK.EXE` starts the other programs. Next: find who
   reads the speed class (byte 0 of `RPG.LCP`) in `WALK0.SL`/`WALK.SL`, then
   analyse `STORY2.SL` (smallest, holds the frame sizes) and `WALK.SL`.
4. (done 2026-10-15) `TAB.TAB` is the voice-over index (`formats.md`). The subtitle bitmaps
   (`STORYn_T.LCZ`) were decoded earlier.
5. Save format: `SAVEnn.000/.001/.002`.
6. (done 2026-10-14) What `TV.EXE` is for: a one-time speed test.
7. (done 2026-10-14) Where the speed class is read: `WALK.SL` `main`, a frame
   divider. Next: test the "486" field (edit a copy of a save), find the
   function that draws the top-right number, and see what the `class < 2`
   branches do.

## Rules for adding to these docs
- **Evidence first.** Every claim states the method (tool and command, or
  function address) and a confidence word: high, medium, low, or guess.
- **Never delete a wrong finding.** Add a dated CORRECTION that says what was
  wrong and why. The history of mistakes is useful.
- **Reproducible.** Anything in `work/` must be re-creatable from a command
  written down in these docs. Record tool versions.
- **Addresses.** Say which program, and give both Ghidra's linear address
  (`0x10000`) and, where useful, the segment:offset name.
- **Do not paste large chunks of decompiler output.** Summarize what a function
  does and cite its address. The tool can regenerate the text.
- **Keep game assets out of git.** `original_bin/` and `work/` are ignored.
  These docs may quote short strings and byte patterns needed as evidence.
- **Unknown is a valid answer.** Add it to an open-questions list instead of
  guessing silently.

## Reproducing the analysis state
- Open a program in Ghidra through REA: `open_binary` with the path under
  `work/unpacked/`. Ghidra sessions are ephemeral, so any rename or comment
  made through REA is lost on close. Record important names and meanings in
  `findings.md` (and later in a names file, once there are enough to track).
- Unpack: `deark -m pklite -o OUT IN` on a copy of the PKLITE-packed file.
