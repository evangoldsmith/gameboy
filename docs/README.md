# Component documentation

One file per component, mirroring the component map in `CLAUDE.md`. Each
document covers what the component owns, its public API, how the logic actually
works, and what is not implemented yet.

| Document | Source | Status |
|---|---|---|
| [cpu.md](cpu.md) | `src/cpu/cpu.{h,cpp}`, `src/cpu/opcodes.{h,cpp}` | Complete instruction set |
| [mmu.md](mmu.md) | `src/memory/mmu.{h,cpp}` | Address routing + partial I/O dispatch |
| [cartridge.md](cartridge.md) | `src/cartridge/cartridge.{h,cpp}` | Header parsing + minimal MBC1 |
| [ppu.md](ppu.md) | `src/ppu/ppu.{h,cpp}` | Scanline timing only, no rendering |
| [apu.md](apu.md) | `src/apu/apu.{h,cpp}` | **Stub** |
| [timer.md](timer.md) | `src/timer.{h,cpp}` | DIV only |
| [joypad.md](joypad.md) | `src/joypad.{h,cpp}` | **Stub** |
| [serial.md](serial.md) | `src/serial.{h,cpp}` | Output capture working |
| [gameboy.md](gameboy.md) | `src/gameboy.{h,cpp}` | Wiring + step/frame loop |
| [logger.md](logger.md) | `src/debug/logger.{h,cpp}` | Game Boy Doctor trace format |
| [main.md](main.md) | `src/main.cpp` | SDL frontend + headless trace mode |

## Reading order

If you are new to the codebase, follow the data flow:

1. **[main.md](main.md)** — how a ROM path becomes a running system.
2. **[gameboy.md](gameboy.md)** — the orchestrator that owns everything and
   drives the loop.
3. **[cpu.md](cpu.md)** — where all the work happens.
4. **[mmu.md](mmu.md)** — how the CPU reaches every other component.
5. Everything else, as needed.

## Current milestone

10 of 11 Blargg `cpu_instrs` sub-tests pass. `02-interrupts` fails because it
needs timer interrupts, which is roadmap Phase 4.

```
./build/release/gameboy roms/cpu_instrs.gb --doctor 250000000 > /dev/null
```

See `roadmap.md` for the phase plan this is tracking against.
