English | [中文](README.zh.md) | [한국어](README.ko.md)

# 3k-decomp

A personal reverse-engineering experiment: reconstruct the source code of
**三界諭：邦沛之謎** (*San Jie Yu: Bangpei zhi Mi*, working name "3K"), a 1993 DOS
role-playing game from Taiwanese publisher 第三波 (Third Wave), as C/C++ that
can be modified, recompiled, and run on modern hardware.

## Goal

1. **Complete decompilation.** Recover all of the game's executable logic as
   readable, buildable **C or C++** source (language choice is an open
   decision, see below). "Complete" means every code path in the shipped
   executables is accounted for, not just the main loop.
2. **Modifiable and recompilable.** The result builds from source with a current
   toolchain (clang/gcc/MSVC) using a normal build system, with no 16-bit DOS
   compiler or DOS emulator required. Game data, tables, and text are
   understood well enough to change.
3. **Runs on modern hardware.** The rebuilt game runs natively on current
   64-bit systems (macOS Apple Silicon first, other platforms desirable). That
   means replacing everything tied to 1993 PCs: DOS interrupts, direct
   VGA/video memory access, legacy sound hardware, keyboard/timer hooks,
   16-bit `int`/segment:offset pointers, and CPU-speed-dependent timing.
4. **Faithful behavior.** The rebuilt game should play like the original:
   same rules, maps, puzzles, battles, story, and save-file compatibility
   where practical.

### Non-goals (for now)

- Byte-identical reproduction of the original executables. Source-level
  equivalence is the target, not a matching build.
- Redistributing the original game or its assets (see [Legal](#legal-and-scope)).
- Bypassing any copy protection, licensing check, or anti-tamper mechanism. If
  one turns up, it is out of scope.
- Improving the game (new features, balance changes) before the faithful port
  works. (Translation came after it: English and Korean, `localization/`.)

## Target

| Item | Value |
|------|-------|
| Title | 三界諭：邦沛之謎 |
| Developer / publisher | 第三波 (Third Wave), catalog no. 6G046 |
| Year | 1993 (release date also given as 1993-06-23) |
| Genre | RPG, 3D isometric view; real-time shooting combined with team turn-based combat |
| Language | Native Chinese (the port adds English and Korean, see [Playing](#building-and-playing)) |
| Platform | DOS |
| Credits (programming) | 李超群 |
| Reference | [Chinese DOS Game Database, cdg-0202](https://cdosgame.simagame.me/games/cdg-0202/) |

The database page also notes that the game is **sensitive to CPU speed and can
crash on machines that are too fast**. Timing is therefore a first-class
concern for goal 3, not a polish item.

## Source material

All original files live in `original_bin/` (not modified, treated as read-only).

### What the first look found

Method: header dumps (`xxd`), `file`, and `strings` over the files in
`original_bin/`. This was a surface inspection only. Nothing has been
disassembled or run yet.

| File | Observation | Confidence |
|------|-------------|------------|
| `3K.EXE` (3.3 MB) | MZ stub of about 15 KB with the `RJSX` marker at offset 0x1C, followed by an ARJ archive (ARJ header magic `60 EA` at byte offset ~15090). `file` reports an ARJ self-extracting archive. Most likely this is the installer, and the loose files beside it are its extracted contents. | High for "ARJ SFX"; installer role is inferred |
| `PLAY.BAT` | `cd\3k`, then `mark`, then `cd ..`. So **`MARK.EXE` is the entry point** the game is launched through. | High |
| `MARK.EXE` (15 KB) | Packed with **PKLITE** (banner `PKLITE Copr. 1990-91 PKWARE Inc.` at offset 0x1E). Must be unpacked before analysis, otherwise only the decompression stub is visible. | High |
| `TV.EXE` (8 KB) | Also PKLITE-packed. Listed in `CHKLIST.MS` next to `MARK.EXE`. Role unknown, perhaps the intro/title sequence. | Packing high; role unknown |
| `CHKLIST.MS` | A DOS 6 anti-virus checksum list naming `MARK.EXE` and `TV.EXE`. Confirms these are the two executables of the installed game. | Medium |
| `*.LCZ`, `*.LCC`, `*.LCP`, `*.SL`, `*.ALL`, `SND_EF.1`, `TAB.TAB`, `_1_.LOC`, `__*.AL?`, `STORY*_V` | Proprietary data files. Headers do not match any format I recognize, and some look compressed or encoded. Likely images, animation, maps, text, and sound. | Formats unknown |
| `SAVE*.000/.001/.002` | Save games, three files per slot. A useful sample set for reverse-engineering the save format. Some are dated 2000s/1980, so they may not all be original. | Medium |

### Open questions

- `MARK.EXE` is only 15 KB packed, which is small for a whole RPG engine. Where
  does the rest of the code live? Candidates: a larger unpacked size, code
  overlays loaded from a data file such as `RPG.LCP` (120 KB), or `TV.EXE`
  acting as another stage. **Not yet determined.**
- What compiler and memory model built it? `3K.EXE` contains a Borland
  copyright string, but that is probably the ARJ SFX stub, not the game.
- Which hardware does it drive directly (VGA mode, Sound Blaster/AdLib/PC
  speaker, mouse)? Needed to design the modern platform layer.
- How does the game handle Chinese text and fonts? No obvious font file was
  seen in the directory. It may use a resident Chinese system, built-in font
  data, or something in the data files.

## Approach

Phases are ordered so that each one produces something checkable.

1. **Baseline.** Run the original under an emulator (DOSBox-X) to record
   reference behavior: screenshots, save files, and timing. This is the oracle
   every later phase is compared against.
2. **Unpack and map.** Unpack the PKLITE executables, confirm the real code
   entry points, and settle the open questions above.
3. **Disassemble and decompile.** Load the unpacked images in Ghidra through
   the REA tooling. Recover functions, identify the C runtime and library
   code, and separate engine code from game logic.
4. **Document formats.** Reverse the data formats (`.LCZ`, `.LCC`, `.LCP`,
   `.SL`, `.ALL`, saves). Write small standalone tools that parse and
   re-encode them, and verify round-trips against the original bytes.
5. **Rebuild the logic in C/C++.** Reconstruct module by module, naming
   things as understanding grows. Keep the original function address in a
   comment for traceability.
6. **Platform layer.** Replace DOS-specific services (video, input, sound,
   timing, file access) with a portable layer, likely SDL2, and make timing
   independent of CPU speed.
7. **Verify and iterate.** Compare against the Phase 1 baseline, fix
   divergences, and only then consider modifications.

## Definition of done

- The project builds from a clean checkout with one documented command and no
  DOS toolchain.
- The built game reaches the title screen, runs a new game through at least
  one full story chapter, loads the original save files, and plays the ending
  sequences.
- Every function in the original executables is either reconstructed or
  explicitly recorded as intentionally replaced (for example, a DOS interrupt
  handler swapped for the platform layer).
- Behavior matches the original on a recorded set of test scenarios: the
  same inputs give the same game state.
- The data-format tools can round-trip every shipped data file.

## Tooling

- **REA MCP server** (`rea-agents`) with **Ghidra 12.1.4** for disassembly and
  decompilation. Ghidra's 16-bit real-mode support is its weak spot, so expect
  manual cleanup of segment:offset and far-pointer code, and no automatic
  recognition of old Borland/Microsoft/Watcom library functions.
- A PKLITE unpacker (to be chosen).
- **DOSBox-X** for the debugger and reference runs.
- Optional alternatives for cross-checking: Reko (DOS-oriented decompiler),
  IDA Free.

## Legal and scope

"Abandonware" is a community term, not a legal status. The game is no longer
sold, but the copyright almost certainly still belongs to 第三波 or its
successors. This is a personal, educational experiment, and the rules below
keep it that way:

- **Do not commit or redistribute the original game files.** Keep
  `original_bin/` out of version control and out of any published copy of this
  repository. Anyone who builds the result must supply their own copy of the
  game data.
- Publish only original work: reconstructed source, tools, and notes. Even
  that should be reviewed before it is shared publicly, since decompiled code
  derived from a copyrighted program may still be a derivative work.
- No work on circumventing copy protection or license checks.

## Repository layout

```
original_bin/   original game files, read-only, not for redistribution (git-ignored)
README.md       this file (README.zh.md, README.ko.md: the same in Chinese and Korean)
docs/           findings: file formats, code notes, the port's milestones (start with docs/README.md)
src/            the reconstructed game, C99 (plus one C++ file for the FM chip)
tests/          unit tests, the headless platform, dump programs for the checks
tools/          data-format tools, the oracle emulator (dosemu.py), check and mutation scripts
localization/   the English and Korean translations, their source tables and fonts (localization/README.md)
third_party/    ymfm (OPL2 emulation)
work/           scratch output of the tools (git-ignored)
```

## Building and playing

    make                                  # build/3k (C99, C++14 for ymfm, SDL2 via sdl2-config)
    make test                             # unit tests and the translations' check
    build/3k --data original_bin --scene game

`--scene game` runs the whole game: title, intro, menu, new game or a saved one, the world and its battles, the ending.
The game writes its saves and state files into the data directory, as the original does; play on a copy of `original_bin/`,
or pass `--write-dir DIR`. More options: `docs/port.md`, "Build and run".

### Playing after the build

1. **Start it from the repository root**, not from `build/`. Without `--data`, the game looks for `original_bin/` in the
   *current* directory, so a run from `build/` (or a double-click on the executable) finds no data and quits at once.
   `TK_DATA=DIR` works in place of `--data DIR`.
2. **Pass `--scene game`.** Without it the program runs `--scene title`, only the title screen.
3. **Keep the original files clean:** make an empty directory for the saves and pass it as `--write-dir`:

       mkdir -p saves
       build/3k --data original_bin --scene game --write-dir saves --lang en

   Escape or closing the window quits.

**Windows (MinGW/MSYS2).** The executable is `build/3k.exe`. Run it from the MSYS2 shell in the repository root, with
the same arguments as above.
- `sdl2-config --libs` links with `-mwindows`, so the program has no console: when it cannot start (for example
  `cannot read ... (data dir: original_bin)`), it exits without showing the message. To see it, redirect the output:
  `build/3k.exe --data original_bin --scene game > log.txt 2>&1`, then read `log.txt`.
- The executable needs `SDL2.dll` and `libstdc++-6.dll`. The MSYS2 shell finds them on `PATH`; to start the game
  from Explorer, copy both from the MinGW `bin` directory (e.g. `C:\msys64\ucrt64\bin`) next to `3k.exe`, and
  start it from a shortcut whose "Start in" folder is the repository root, with the arguments above added.

**Language.** Chinese (the original) is the default. For English or Korean put a `3k.ini` in the directory the game is
started from:

    [game]
    language=en          ; zh, en or ko
    localization=localization

`--lang zh|en|ko` overrides it and `--config FILE` reads another file. Everything on screen is translated except the title
logo and the credits; the voice-over stays Mandarin. How the translations are stored, the naming rules, the fonts and the
checks: `localization/README.md`.

## Status

The game is ported from start to finish (`docs/port.md`, "Current state"): every program of the original runs as part of
one native program, each compared pass by pass with the original's code run in an emulator (`tools/dosemu.py`). English and
Korean translations of all on-screen text are built in (`localization/README.md`). Not yet done: one play-through of the
whole story in the port, and comparing the Sound Blaster sounds by ear. The reading order and the next steps are in
`docs/README.md`.

## Third-party code
- `third_party/ymfm/`: Yamaha FM chip emulation by Aaron Giles,
  [ymfm](https://github.com/aaronsgiles/ymfm), BSD 3-Clause (licence text in that directory), used
  unmodified for the OPL2 music. The pinned commit is in `third_party/ymfm/PINNED.txt`.
- `localization/fonts/tk_hangul16.tkf`: Hangul bitmaps converted from
  [Neo Dunggeunmo](https://github.com/neodgm/neodgm) by Eunbin Jeong, SIL Open Font License 1.1 (text beside it), renamed
  as the OFL requires; `localization/fonts/tk_narrow6.tkf`: from the X11 misc-fixed 6x13 font by Markus Kuhn, public domain.
  See `localization/fonts/README.md`.

