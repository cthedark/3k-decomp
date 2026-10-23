# File inventory of `original_bin/`

95 files. SHA-256 for every file is in `original_checksums.sha256` so the same
set can be verified without redistributing it. "Known" means supported by
evidence in `findings.md`; everything else is a guess or unknown.

## Programs (DOS executables)
| File | Size | Packing | Role |
|------|------|---------|------|
| `3K.EXE` | 3,295,211 | ARJ self-extractor | Installer. Contains the other files. Not analyzed further. |
| `PLAY.BAT` | 29 | text | `cd\3k`, `mark`, `cd ..`. Starts the game through MARK. |
| `MARK.EXE` | 15,388 | PKLITE (25,697 unpacked) | Entry point. Intro/title, FM music driver, filename table. |
| `TV.EXE` | 8,150 | PKLITE (12,283 unpacked) | **One-time speed test** (code read 2026-10-14, see `code.md`): shows a TV-static screen, times it with the BIOS tick counter, writes a speed class 0..4 into byte 0 of `RPG.LCP`, then starts `MARK.EXE`. `MARK` runs it only if that byte is > 10; our copy has 2, so it never runs. |
| `WALK.SL` | 126,865 | none | The game world (confirmed in the oracle, see findings.md "WALK.SL ... first survey"): a scrolling tile map in a 192 x 168 window with the interface frame around it. Largest. |
| `CAST.SL` | 22,903 | none | Overlay. Not seen in the traces yet (maybe the credits). Role unknown. |
| `WALK0.SL` | 17,631 | PKLITE (30,681) | Overlay loaded for the main menu (trace). |
| `STORY2.SL` | 19,777 | PKLITE (36,575) | **The intro slideshow** (loaded as an overlay by MARK; confirmed in the trace). |
| `ENDING.SL` | 14,756 | PKLITE (25,301) | Overlay. Not seen in the traces yet (the ending?). Role unknown. |

Confirmed by the traces: `WALK0.SL` = menu, `WALK.SL` = game world,
`STORY2.SL` = intro slideshow. `CAST.SL` and `ENDING.SL` are guesses from the
names only. All five are started by `execl` from another program, which
replaces it (they are chained programs, not call-and-return overlays; see
`code.md`, "Program chaining").

## Palettes
| File | Size | Notes |
|------|------|-------|
| `RPG.LCP` | 120,013 | 5-byte header, then a 768-byte VGA palette (6-bit), then unknown data. |
| `ANI.LCP` | 19,167 | 3-byte header, then palette ramp, then unknown data. |

## Data with unknown format
| Group | Files | Notes |
|-------|-------|-------|
| `NUMBER.LCZ` | | | **The Chinese bitmap font** (decoded; see `formats.md`). The name is misleading. |
| `.LCZ` (37) | `TITLE1`, `GMAIN`, `NUMBER` (408 KB), `SPIC`, `BOAT`, `CAST1/2`, `ENDING1..5`, `ENDING12`, `ENDING1T`, `ENDING4T`, `STORY0..6` and variants (`STORY0_1`, `STORY0_T`, `STORY1_2`, `STORY1_T` ...) | No recognizable header. Names suggest images and story text. Likely compressed. |
| `.LCC` (6) | `MAGICT`, `MANDATA1`, `MAN_PIC`, `MES_TXT` (98 KB), `OBJECT`, `SHOP_PIC` (105 KB) | Names suggest tables, text and pictures. No header recognized yet. |
| `.ALL` (6) | `GTAB` (567 KB), `MON` (489 KB), `MUC` (188 KB), `M_M` (160 KB), `MAN_INFO`, `__2` | Big bundles. `MUC` is named by MARK. |
| `STORYn_V` (6) | `STORY1_V` .. `STORY6_V` | 150-460 KB each. **Voice-over clips (VOC), decoded**: see `formats.md`. |
| other | `TAB.TAB`, `__1.AL1`, `__1.AL2`, `__1.NOW`, `MAN_INFO.NOW` | Unknown. |
| sound effects | `SND_EF.1` (25 KB), `_1_.LOC` (61 KB) | **Decoded**: WALK.SL's sounds, bare VOC sound-data blocks (`formats.md`). |
| `CHKLIST.MS` | 54 bytes | Anti-virus checksum list naming MARK.EXE and TV.EXE. Not game data. |

## Save files
`SAVE00`, `SAVE01`, `SAVE03`, `SAVE04`, `SAVE07`, `SAVE09`, `SAVE10`; each slot
has three files `.000`, `.001`, `.002` (about 100-230 bytes, 18 KB, 8.5 KB).
File dates are mixed: `SAVE03` is dated 1980-01-10, `SAVE07/09/10` 1993-09-05,
and `SAVE00/01/04` carry month/day dates without a year (so recent). They are
probably a previous user's saves, not part of the original release. Useful as
sample data for working out the save format.

## Provenance caution
The directory is an installed copy (extracted from the `3K.EXE` installer),
and it contains someone's saves and runtime state files (`.NOW`). It is not a
pristine install image.
