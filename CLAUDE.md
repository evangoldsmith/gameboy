# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
make debug          # Build debug version (default)
make release        # Build optimized release version
make run            # Build debug + run (ROM=path/to/rom.gb to specify ROM)
make run-release    # Build release + run
make clean          # Remove build artifacts
make distclean      # Remove all build directories
make reconfigure    # Re-run CMake configuration
```

Build outputs go to `build/debug/` or `build/release/`. The main binary is `build/<config>/gameboy`.

**Dependency**: SDL2 must be installed (`brew install sdl2` on macOS).

## Architecture

The project splits into two compilation units:

- **`gameboy_core`** — static library with all emulation logic, no SDL dependency
- **`gameboy`** — executable linking `gameboy_core` + SDL2 for the frontend

### Component Map

| Component | Path | Docs | Responsibility |
|-----------|------|------|----------------|
| CPU | `src/cpu/` | [cpu.md](docs/cpu.md) | LR35902 instruction execution (`cpu.cpp`, `opcodes.cpp`) |
| MMU | `src/memory/mmu.cpp` | [mmu.md](docs/mmu.md) | Memory address space, ROM/RAM banking |
| PPU | `src/ppu/ppu.cpp` | [ppu.md](docs/ppu.md) | Tile/sprite rendering, VRAM, frame buffer |
| APU | `src/apu/apu.cpp` | [apu.md](docs/apu.md) | 4-channel audio synthesis |
| Timer | `src/timer.cpp` | [timer.md](docs/timer.md) | Internal timing and interrupt generation |
| Joypad | `src/joypad.cpp` | [joypad.md](docs/joypad.md) | Button input |
| Serial | `src/serial.cpp` | [serial.md](docs/serial.md) | Link cable serial I/O |
| Cartridge | `src/cartridge/cartridge.h` | [cartridge.md](docs/cartridge.md) | ROM loading, MBC handling |
| Logger | `src/debug/logger.cpp` | [logger.md](docs/logger.md) | Debug logging |
| GameBoy | `src/gameboy.cpp` | [gameboy.md](docs/gameboy.md) | Top-level system class wiring all components |
| Main | `src/main.cpp` | [main.md](docs/main.md) | SDL2 frontend entry point |

The `GameBoy` class in `src/gameboy.h` is the central orchestrator — it owns all subsystem instances and drives the main emulation loop.

## Contributing

**All changes go through a pull request.** Branch, push, open a PR, let CI pass, then merge. Do not commit directly to `main`.

CI (`.github/workflows/ci.yml`) builds both configurations with `-Werror` and then runs the test ROMs, which it downloads from a pinned release rather than the repo. See [docs/testing.md](docs/testing.md).

Note that CI uses GCC while local development is usually Clang; a clean local build is not proof of a clean CI build.

## Documentation

`docs/` holds one Markdown file per component, indexed by `docs/README.md` and linked from the table above. Each covers responsibility, public API, how the logic actually works, current state, and an explicit "Not implemented yet" list.

**Keep `docs/` in sync as part of the same change that alters the code — not as a follow-up.** Specifically:

- Changing a public method signature or adding one → update that component's **Public API** section.
- Changing how something works → update the corresponding **How it works** prose. Doc statements about *why* a piece of logic exists (initialisation order, active-low bits, read-and-clear IRQ flags) are load-bearing; revise them rather than deleting them.
- Implementing something listed under **Not implemented yet** → remove that bullet and update **Current state**. If it was a stub file, drop the status banner at the top.
- Completing a roadmap phase → update **Current state** in every doc it touched, plus the status column in `docs/README.md`.
- Adding a component → create `docs/<name>.md` following the existing structure, add a row to the component map above and to `docs/README.md`.
- Changing test results → update the milestone line in `docs/README.md`.

When a doc and the code disagree, the code is right and the doc is a bug.

## Project State

Roadmap Phases 0–11 are done. **All 11 Blargg `cpu_instrs` sub-tests pass**, **Tetris is playable with full audio**, and **Pokémon Red reaches the name entry screen.**

- **Implemented**: full SM83 instruction set, interrupt dispatch, MMU routing with I/O dispatch, cartridge header parsing, MBC1/2/3/5, battery-backed saves, serial with correct clock-source handling, the full DIV/TIMA timer, background/window/sprite rendering, OAM DMA, all five interrupts, joypad input, all four sound channels with SDL audio output, optional boot ROM support, Game Boy Doctor trace logging.
- **Boot ROM support is in** (roadmap Phase 13). Supply a 256-byte image via `--boot` or at `roms/dmg_boot.bin`; without one the CPU starts in its post-boot state as before. None is committed — Nintendo's is copyrighted, Bootix is an MIT-licensed replacement.
- **Phase 12 is partly done**: M-cycle memory timing (`mem_timing` 3/3) and the DIV-driven APU frame sequencer (`dmg_sound` 9/12). Still outstanding: variable mode 3 length, the pixel FIFO, the STAT and OAM bugs, and the wave-RAM access window.
- **The MBC3 RTC stores and latches but does not tick**, and is not persisted to the `.sav`.

### Test ROM results

`roms/` is gitignored and stays that way — the repo is deliberately decoupled from ROM files. CI is expected to `curl` the [c-sp bundle](https://github.com/c-sp/game-boy-test-roms/releases) before running tests.

| Suite | Result |
|---|---|
| `cpu_instrs` | 11 / 11 |
| `instr_timing` | pass |
| `halt_bug` | pass |
| `dmg-acid2` | pixel perfect |
| `dmg_sound` | 9 / 12 |
| `oam_bug` | 2 / 8 |
| `mem_timing` | 3 / 3 |

Per-suite breakdowns live in `docs/README.md` and the relevant component docs. Mooneye and Mealybug have not been run — they need different harnesses (register signature and screenshot comparison respectively).
- No tests exist beyond running test ROMs.

Verify the CPU with:

```bash
make release
./build/release/gameboy roms/cpu_instrs.gb --doctor 250000000 > /dev/null
```

C++20 with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion` — keep code warning-clean.
