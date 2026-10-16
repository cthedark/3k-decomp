# Environment and tooling

How the analysis environment was set up on this machine (macOS, Apple Silicon,
arm64). Written so a person or another AI agent can reproduce it or diagnose
why a tool is missing.

## Machine
- macOS on Apple Silicon (arm64), Homebrew at `/opt/homebrew`.
- Java: Amazon Corretto 25.0.3 (accepted by REA; it needs a 64-bit JDK 21+).

## REA (reverse-engineering MCP server)
- Package: `rea-agents@6.4.0`, launched from the `reverse-engineer` agent
  definition at `~/.kiro/agents/reverse-engineer.json` (outside this repo).
- Needs Node `^22.19 || ^24.11 || >=26`. The default Node on this machine was
  20.9, which crashes the server on start (`ERR_IMPORT_ASSERTION_TYPE_MISSING`).
  The agent definition launches it with a newer Node instead.
- After changing the agent definition or installing a provider, the session
  must be reloaded: REA reads its environment once at startup and caches the
  provider status.
- `binary_session` is the cheap health check. It lists each provider and why
  it is unavailable.

## Ghidra (the decompiler REA uses for native code)
- Version 12.1.4 at `/Users/cthedark/tools/ghidra_12.1.4_PUBLIC`, installed from
  the official GitHub release zip (no Homebrew cask exists).
- REA finds it through the `GHIDRA_INSTALL_DIR` environment variable in the agent
  definition.
- **Known gap:** the release zip has no macOS arm64 decompiler. It ships only
  `linux_x86_64` and `win_x86_64` under
  `Ghidra/Features/Decompiler/os/`. REA requires
  `os/mac_arm_64/decompile`, so it was built from the C++ source bundled in the
  release (`Ghidra/Features/Decompiler/src/decompile/cpp/Makefile`).
  - The Makefile hardcodes `-arch x86_64` on macOS. The build overrode that to
    `arm64` with a minimum target of macOS 11.
  - The build was done in a temporary copy so no object files ended up in the
    Ghidra tree. The resulting binary was copied to `os/mac_arm_64/decompile`.
  - Result: a thin arm64 Mach-O executable. REA accepts it.
  - If Ghidra is ever reinstalled or upgraded, this step must be repeated.

## Other tools installed for this project
| Tool | Version | Installed with | Purpose |
|------|---------|----------------|---------|
| DOSBox-X | 2026.10.01 | `brew install dosbox-x` | Run the original game, reference behavior, debugger |
| deark | 1.7.3 | `brew install deark` | Unpack PKLITE-compressed executables |

The Homebrew cask `dosbox-x-app` is disabled (fails Gatekeeper), so the
command-line formula is used.

## Not available (and what that means)
- Hopper: not installed. IDA: not configured. Ghidra is the only native
  provider in use.
- REA cannot run or debug a program itself. It analyzes statically. Anything
  that needs execution (timing, screen output, sound, how a file is read at
  runtime) must be observed in DOSBox-X.

## Conventions for this project
- `original_bin/`: the untouched original game files. Read-only, git-ignored,
  never redistributed. Checksums are in `original_checksums.sha256` so anyone
  can verify they have the same files.
- `work/`: derived and scratch output (unpacked executables and so on).
  Git-ignored. Anything in `work/` must be reproducible from a command that is
  written down in these docs.
- `docs/`: tracked. See `README.md` in this directory.

## Disassembler for code Ghidra does not recognise (added during code analysis)
- `capstone` 5.0.3 in a throwaway virtual environment at `work/venv` (git-ignored):

      python3 -m venv work/venv
      work/venv/bin/pip install capstone==5.0.3

- Used by `tools/dis16.py`. Reason: the macOS `objdump` cannot read raw 16-bit
  x86, and Ghidra has no functions in some hand-written assembly regions
  (for example `0x10e1a`-`0x11126` in `MARK.EXE`).
- The system Python is unchanged. To remove: delete `work/venv`.

## DOS emulator for reference runs (added during milestone 2)
- `tools/dosemu.py` needs `unicorn` 2.1.2 in `work/venv`. Building it from source needs
  `cmake` and `pkg-config`, which were not installed. Installed with Homebrew:

      brew install cmake pkgconf
      work/venv/bin/pip install unicorn==2.1.2

  Installed versions: cmake 4.4.4, pkgconf 3.0.8, unicorn 2.1.2. A first attempt failed with
  "No such file or directory: 'cmake'", the second with "pkg-config binary not found".
- The emulator is not a general DOS emulator. It runs the unpacked programs from
  `work/unpacked/`, serves files from a private copy at `work/emu_data`, and reports any
  request it does not know.
- No `timeout` command on macOS; do not use it in scripts.

## Sanitizer builds and SDL (2026-10-15)

Do not link the windowed program (`build/3k`) with `-fsanitize=...` and run it from a script:
on this machine the SDL2 library (the `sdl2-compat` layer) showed a modal error dialog from its
initializer before `main`, and the process waited for a click for 1.5 hours. For sanitizer
runs use the headless build, which has no SDL:

    make clean && make build/3k_headless CFLAGS="-std=c99 -O1 -g -Wall -Wextra -Wpedantic \
        -fsanitize=address,undefined -fno-omit-frame-pointer"

It supports only the stepped modes (`--scene intro0|title --step-dump ...`). The unit tests
(`make test`) never used SDL. When running anything that could block, give it a time limit.
