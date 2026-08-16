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

Roadmap Phases 0–6 are done, plus pieces of 7–9 that were needed to unblock them. **All 11 Blargg `cpu_instrs` sub-tests pass**, and the results screen renders.

- **Implemented**: full SM83 instruction set, interrupt dispatch, MMU routing with partial I/O dispatch, cartridge header parsing, minimal MBC1, serial output capture, the full DIV/TIMA timer, background rendering with VBlank and STAT interrupts, Game Boy Doctor trace logging.
- **Stubs**: `apu.{h,cpp}` and `joypad.{h,cpp}` are empty include guards.
- **No sprites, no window layer, no input, no audio.** Phase 7 is next.
- No tests exist beyond running test ROMs.

Verify the CPU with:

```bash
make release
./build/release/gameboy roms/cpu_instrs.gb --doctor 250000000 > /dev/null
```

C++20 with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion` — keep code warning-clean.
