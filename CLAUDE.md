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

| Component | Path | Responsibility |
|-----------|------|----------------|
| CPU | `src/cpu/` | LR35902 instruction execution (`cpu.cpp`, `opcodes.cpp`) |
| MMU | `src/memory/mmu.cpp` | Memory address space, ROM/RAM banking |
| PPU | `src/ppu/ppu.cpp` | Tile/sprite rendering, VRAM, frame buffer |
| APU | `src/apu/apu.cpp` | 4-channel audio synthesis |
| Timer | `src/timer.cpp` | Internal timing and interrupt generation |
| Joypad | `src/joypad.cpp` | Button input |
| Serial | `src/serial.cpp` | Link cable serial I/O |
| Cartridge | `src/cartridge/cartridge.h` | ROM loading, MBC handling |
| Logger | `src/debug/logger.cpp` | Debug logging |
| GameBoy | `src/gameboy.cpp` | Top-level system class wiring all components |
| Main | `src/main.cpp` | SDL2 frontend entry point |

The `GameBoy` class in `src/gameboy.h` is the central orchestrator — it owns all subsystem instances and drives the main emulation loop.

## Project State

All component files are currently **stub implementations** (headers and empty `.cpp` files). The executable builds and runs but only prints "Hello, world!". No tests exist yet.

C++20 with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion` — keep code warning-clean.
