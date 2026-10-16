# Concepts used in this project

Plain-language notes for learning the process. Each entry says why it matters
here.

**MZ executable.** The DOS program format. Starts with the bytes `MZ`. A header
(sizes, entry point, relocation table) is followed by the code. Our programs
(`.EXE` and `.SL`) are all MZ.

**Real mode and segment:offset.** DOS programs run in 16-bit "real mode". An
address is two numbers, a segment and an offset, and the CPU computes
`segment * 16 + offset`. Many pairs give the same address. Ghidra shows one
flat "linear" address, so a name like `FUN_1175_0008` means segment 0x1175,
offset 0x0008, while Ghidra's list shows `0x11758`. Memory models (small,
medium, large) decide whether pointers carry a segment. This is the hardest
part of 16-bit decompilation.

**Relocation.** Code that mentions a segment number cannot know it until the
program is loaded. The header lists every place to patch. The loader (DOS, or
Ghidra) applies them. REA listed several hundred for MARK.

**Packer (PKLITE, LZEXE, EXEPACK).** A tool that compresses an executable and
adds a tiny stub that decompresses it at start. Disassembling a packed file
shows only the stub. We unpack first (`deark`).

**Entry point and `main`.** The entry point is where DOS starts the program.
Compilers put "startup code" there (memory setup, clearing variables), and it
then calls the real `main`. In both MARK and TV, `entry` is Microsoft C
startup, and `main` is the function at `0x10000`.

**Decompiler output is a guess.** Ghidra turns machine code into C-like text,
but names, types and structure are inferred. Variables look like `local_3c`,
and pointer casts are often wrong in 16-bit code. We keep the evidence ID and
confidence next to any claim. Rename things as understanding grows.

**VGA mode 13h.** The classic 320x200, 256-colour graphics mode. The screen is
64,000 bytes at segment A000h, one byte per pixel. The colours come from a
256-entry palette of 6-bit R,G,B values (0-63) loaded into the VGA hardware.
That is why a 768-byte block of numbers all below 64 looks like a palette.

**Overlay / sub-program.** A program that loads and runs more code from other
files at run time. (The `.SL` files turned out not to be this: they are whole
programs that replace the caller via `exec`. Kept here because the word was used
in earlier entries.) Static analysis of one file
cannot see the others, so we must map how they fit together.

**Oracle.** The real original, run in DOSBox-X. The reconstruction is correct
when it behaves like the oracle, not when the C looks plausible.

**Clean-room.** We reconstruct behavior in new, original code, rather than
pasting decompiler output. Decompiler text is evidence for understanding, not
the final source.
