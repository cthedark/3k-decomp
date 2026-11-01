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
| Run and trace the original | `run-original.sh` (DOSBox-X, `--trace` logs file I/O); `dosemu.py` (the oracle: runs an unpacked program in a small emulator and saves its screen; options `--log-files`, `--snap-hit`, `--peek`, `--trace-calls`, `--dump-local`, `--dump-hit`, `--screen-from`, `--opl-log`, scripted `--key`, `--palette-from`, `--log-vga`; needs the venv); `capture_original_audio.sh` (record what the original outputs in DOSBox-X, headless) |
| Read code | `dis16.py`, `calls16.py` (disassemble; fold into calls with arguments; need the venv); `mz_strings.py` (strings with addresses) |
| Pictures and sprites | `lcz_decode.py`, `lcz_sheet.py`, `lcz_at.py` (LCZ containers, also a block inside another file such as `RPG.LCP`); `buf_sheet.py` (contact sheet of raw buffers); `render_font.py` |
| Compare screens | `compare_frames.py`, `compare_set.py`, `compare_raw.py` (raw dumps of 64,000 pixels + palette); `check_intro1.sh`..`check_intro6.sh`, `check_menu.sh`, `check_load.sh` (loading a save: the files written and the world after them), `check_credits.sh`, `check_walk.sh` (re-run a scene against the original frame by frame; `check_walk.sh` also covers the world in motion, walking, the roof and the music, about 3 minutes) |
| Voice and music data | `voc_extract.py`; `cmf_survey.py`, `cmf_behaviour.py`, `cmf_to_mid.py` (songs of `MUC.ALL`, also as MIDI) |
| Recorded audio | `audio_timeline.py`, `find_clip.py`, `clip_scale.py`, `loop_period.py`, `onset_fit.py`, `pitch_check.py` (has a self-test), `emu_baseline.py`, `compare_music.py`; method and pitfalls in `findings.md`, 2026-10-15 |
| Timing | `fit_timeline.py` (split event times into clock-bound and CPU-bound parts) |
| Compare the original's FM driver with the port's player | `check_music.sh` (all 25 songs: record with `capture_song_opl.py`, play with `build/music_regs`, compare with `compare_opl.py`); `dosemu.py --opl-log` and `--trace-regs`; `cmf_drum_regs.py` (the driver's writes at the drum events of a song); `build/music_dump DATA OUT.wav SECONDS OFFSET LENGTH` renders any song to a WAV |
| Check a model of CAST.SL's loop against the oracle's buffers | `cast_buffers.py` (loop) and `cast_wipe.py` (start-up wipe); they need dumps in `work/cast/buf`, see their headers and `findings.md` |
| Dump memory / log file reads and calls from WALK.SL in the oracle (`walk_wtrace.py` logs every write into a memory range with the writing instruction); model its map layers | `walk_dump.py` and `walk_layers.py` (headers have the commands; `findings.md` "WALK.SL step 1") |
| Model WALK.SL's entity pass (hero, party, villagers) and check it against the oracle at chosen frames | `walk_entities.py` (header has the commands; `findings.md` "WALK.SL step 2") |
| Rebuild WALK.SL's whole first screen (world, rain, button labels) from the game files and compare it with the oracle | `walk_first_frame.py` (header has the commands; `findings.md` "WALK.SL step 3") |
| Rebuild WALK.SL's start-up scene loader (maps, tiles, state patches) from the game files; compare every buffer with the oracle for all 53 scenes and random state files; negative controls | `walk_scene.py` (`show`, `check`, `scenes`, `fuzz`), `mutate_scene.py` (`findings.md` "WALK.SL step 4") |
| Plan a key script through waypoints with the model (a route through the streets), to ask the oracle to walk it | `walk_route.py` (`walk_keys.py route` uses it) |
| Make key scripts for the oracle (random arrow-key presses, or yo-yoing around the house door) | `walk_keys.py` (`talk` for F1, `look` for F2, `take` for F3, `cast` for F4, `items` for F5, `equip` for F6; `number_keys.py` answers a number box) (with `walk_idle_trace.py --key PASS:CODE`; `findings.md` "WALK.SL step 7") |
| Check WALK.SL's main loop (animated tiles, palette cycle, weather clock, villagers, hero counter, walking, roofs, shots and enemies) pass by pass against the oracle: record, model, C port | `walk_idle_trace.py`, `walk_idle.py` (`check`, `cross`; `WALK_DATA=DIR` for another scene's data); `check_walk.sh` runs seven clock readings (`findings.md` "WALK.SL step 6") |
| Check WALK.SL's battle pass by pass against the oracle (world, battle, rewards bar and message box loops as one pass sequence) | `battle_check.py` (one start; `--cache` keeps the original's recording), `check_battle.sh` (160 starts: six whole battles, level-ups, the bar, a walk-in win, fleeing, the magic and item lists, the mouse (`--mouse`), story enemies, the FM card (`--opl`), the Sound Blaster's sounds (`--sb`, `sb_stub.py`), and MARK's game-over screen through `gameover_check.py`; about 12 minutes once recorded; `RUNS="prefix ..."` runs only some), `battle_keys.py` (key scripts for the command and target choice, `--attack` for whole battles, `--flee`, `--all` for every key), `battle_mouse.py` (mouse scripts), `walk_scene_data.py` (a start in any scene and place; `--word` / `--byte` for experience, levels, MP, items) |
| Mutation checks (do the tests fail when the code is wrong?) | `mutate_audio.sh`, `mutate_music.sh` (needs nothing), `mutate_menu.sh` (needs `check_menu.sh` run first), `mutate_load.py` (needs `check_load.sh` run first), `mutate_credits.sh` (needs `check_credits.sh` run first), `mutate_walk.py` (needs `check_walk.sh` run first; 276 bugs, about 45 minutes; `ONLY=TEXT` runs the ones whose name contains TEXT), `mutate_battle.py` (needs `check_battle.sh` run first; 303 bugs; `SKIP=n`, `ONLY=TEXT`, and `RUNS="prefix ..."` passed to `check_battle.sh` to run fewer starts) |

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
- Native port (`port.md`), as of 2026-10-18. `make` builds `build/3k`; `--scene game` chains title -> intro -> title -> menu -> (new game) world,
  (credits) -> menu. Every scene below is compared with the original's code run in `tools/dosemu.py`, frame by frame or pass by pass, and has a
  `tools/check_*.sh` and a mutation script:
  - **Title** (`MARK.EXE`, milestones 1-2) and **intro** (`STORY2.SL`, milestone 3: seven slides, voice-over, subtitles), with the **FM music**
    (milestone 5/5b: the player writes the same OPL registers as the original's driver for all 25 songs of `MUC.ALL`, rhythm mode included).
  - **Menu** (`WALK0.SL`, milestone 6): opening animation, the four items by key and mouse, the choice transition, the load-game slot picker.
  - **Credits** (`CAST.SL`): wipe, scrolling names with the reflected floor, song 0 with drums.
  - **World** (`WALK.SL`, milestone 7, scene 0 only): the start-up scene loader (7a), the idle world (animated tiles, palette cycle, the
    hour-long weather clock, villagers walking, 7b), the hero walking with the arrow keys with collision, bump shake and party trail (7c), the
    roof of the first house coming off and going back (7d), the scene's song (7e), and shots, enemies and coins (7f, scene 1).
  - **Battle** (`WALK.SL`, milestone 8a, 2026-10-18): touching an enemy hands over to the battle's first screen (the doubled, perspective
    backdrop, the party strip with faces, HP and MP, the command tabs, the monsters) and its idle passes (sway, jitter, frame changes, the
    battle song); `tools/check_battle.sh`. Milestone 8b: the command tabs and choosing a target with the keys. Milestone 8c: attacks and the
    monsters' turns, so a battle can be fought to its end. Milestone 8d (2026-10-19): after a win the rewards bar, the coins, level-ups with
    their message box and the way back to the world; after a loss MARK.EXE's game-over screen, then the title. Milestone 8e: the other
    three commands: fleeing, the magic list (key 1 casts) with its member picker, and the item list (key 1 uses, key 2 turns the page).
    Milestone 8f: the mouse in the battle (tabs, targets, the lists, the picker, the bar, the message box) and the pointer in the world;
    8g: walking and shooting with the mouse in the world. Milestone 9a: F1, talking to villagers (message boxes, flags); 9b: the talks that sell and ask;
    9c (2026-10-20): the dark scenes (map header byte 5: the view zoomed in, darkened, a lit oval round the hero; 24 scenes);
    9d: the world's start zoom (the view comes in from its middle in 13 steps before the first pass). Milestone 8h (2026-10-21): story
    enemies, keys 1 and 2 in the fight, the picker face's flash, the game-over screen's mouse; 8i (2026-10-22): the Sound Blaster's
    sounds (`SND_EF.1`, `_1_.LOC`). The battle (milestone 8) is complete. Milestone 9e (2026-10-24): F2, looking at signs (`SPIC.LCZ`); 9f: F3, taking items and coins; 9g: F4, casting outside a battle; 9h: F5, the item list outside a battle; 9i: F6, the equipment screen; 9j (2026-10-26): F7, using items and coins; 9k: F8, the map; 9l (2026-10-27): F9, the party's order; 9m: F10, the party's data; 9n (2026-10-28): F7 at a sell counter; 9o: keys 1 to 4 and A (music, sounds, saving, leaving, sliding round corners); 9p (2026-10-30): loading a save from the menu's slot picker; 9q: doors to other scenes; 9r (2026-10-31): shops; 9s: boat rides and scene 3's snow; 9t: number locks and scripted talks; 9u: warps; 9v: the card game; 9w: the ending's record (ENDING.SL itself is not built); 9x: a sell counter stepped on; 10a to 10g: ENDING.SL, whole.
  - The operator has confirmed by eye or ear: the title, intro pacing, voice and subtitle sync, the music balance, the menu and its opening,
    the credits' pace, the first screen of the world, the idle animation, and walking with collision (2026-10-17).
- **Not built in the world** (`findings.md` "WALK.SL step 7"-"step 13", `port.md` 7c-8a): the
  new-game step that writes the `.NOW` files. A reached unbuilt case is printed on stderr
  (`walk: not built: ...`, `battle: not built: ...`).
- Not done: no code analysed in the overlays; no function renamed or
  reconstructed; subtitle and picture formats unknown.
- Operator note: the operator's keyboard has no function keys. Use the mouse on
  the on-screen buttons, and close DOSBox-X with the window close button. In the port (`build/3k`), the top letter row Q W E R T Y U I O P
  also works as F1 to F10 (`src/platform_sdl.c`, 2026-10-28).

## Next steps (ordered)
The earlier list (formats, chaining, `TAB.TAB`, `TV.EXE`, the speed class) is done or folded into the port; the save format (`SAVEnn.000/.001/.002`)
is known for writing (`findings.md` step 37) and reading ("WALK0.SL: loading a save", `port.md` 9p). For the world, in the order the operator and Kiro agreed to work:
1. The keys of `WALK.SL`'s main loop: the F-key actions, 1-4 and A (sliding) are built (`port.md` 9a to 9o), and Space (shots) (7f).
2. The scene record block `0x172f0` .. `0x18b82` (a dispatch on the record's kind) (step 39 says what each kind is): all built (a kind 0x14 record stepped on, `port.md` 9x, cannot be reached in the shipped scenes). Kind 5, the shops and the inn (`port.md` 9r), kind 8, the boat
   rides (9s), kinds 7, 0x11 and 0x12, the number locks and scripted talks (9t), kind 0, the warps (9u), kind 0xe, the card game
   (9v), and kind 0x13, the ending's record, which hands over to `ENDING.SL` (9w; ENDING.SL is built, 10a to 10g).
   Kind 1 is a door to another scene and kind 4 sets a flag, both built (`port.md` 9q); kind 0x14 is a sell counter, built for F7
   (`port.md` 9n).
3. The battle that touching an enemy starts, in stages (`findings.md` steps 12-18): the first screen and idle passes, the command and target
   choice with the keys, the attack and the monsters' turn, what follows a win or a loss, the other three commands (flee, magic, items)
   and the mouse are built (`port.md` 8a-8f; the world's mouse buttons 8g), then story enemies, keys 1 and 2 and the sounds (8h, 8i): the
   battle is complete. Next: the actions of the F-keys (F1 talk, F2 look, F3 take, F4 cast, F5 items, F6 equipment, F7 use, F8 map, F9 order and F10 data are built, `port.md` 9a, 9b and 9e to 9m; F7 sells at counters, 9n), keys 1 to 4 and A (9o).
4. New game (writing `__1.NOW` / `MAN_INFO.NOW` from `__1.AL1`, `__1.AL2`, `MAN_INFO.ALL`); loading a save from the slot picker is built (`port.md` 9p).

How to pick up the world work: `tools/check_walk.sh` checks everything built so far (about 21 minutes; `SHOTS=0` about 3); `tools/walk_idle_trace.py --key` records
the original's passes with any key script, `tools/walk_idle.py check` / `cross` compare the Python model / the C port with them, and
`tools/walk_route.py` plans a walk through waypoints. Read `findings.md` "WALK.SL step 6" onward first. For the battle: `tools/check_battle.sh` (about 3 minutes)
and `tools/battle_check.py DATA N [--key ...]` for one start; `tools/walk_scene_data.py` makes a start on an enemy.

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
