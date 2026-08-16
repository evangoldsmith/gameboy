# Component documentation

One file per component, mirroring the component map in `CLAUDE.md`. Each
document covers what the component owns, its public API, how the logic actually
works, and what is not implemented yet.

| Document | Source | Status |
|---|---|---|
| [cpu.md](cpu.md) | `src/cpu/cpu.{h,cpp}`, `src/cpu/opcodes.{h,cpp}` | Complete instruction set |
| [mmu.md](mmu.md) | `src/memory/mmu.{h,cpp}` | Address routing + partial I/O dispatch |
| [cartridge.md](cartridge.md) | `src/cartridge/cartridge.{h,cpp}` | Header parsing, MBC1/2/3/5, battery saves |
| [ppu.md](ppu.md) | `src/ppu/ppu.{h,cpp}` | Background, window and sprite rendering |
| [apu.md](apu.md) | `src/apu/apu.{h,cpp}`, `channels.{h,cpp}` | Pulse channels 1–2; no wave or noise |
| [timer.md](timer.md) | `src/timer.{h,cpp}` | DIV + TIMA with falling-edge clocking |
| [joypad.md](joypad.md) | `src/joypad.{h,cpp}` | Full button matrix + interrupt |
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

**All 11 Blargg `cpu_instrs` sub-tests pass.**

```
./build/release/gameboy roms/cpu_instrs.gb --doctor 250000000 > /dev/null
```

```
cpu_instrs

01:ok  02:ok  03:ok  04:ok  05:ok  06:ok  07:ok  08:ok  09:ok  10:ok  11:ok

Passed all tests
```

All three graphics layers render — background, window and sprites, with OAM
DMA — input works, and MBC1/2/3/5 are implemented. **Tetris is playable, and
Pokémon Red reaches Prof. Oak's intro and the name entry screen.**

Battery-backed cartridges persist to a `.sav` file next to the ROM, and the two
pulse sound channels play.

Next up is Phase 11 — sound channels 3 (wave) and 4 (noise), which is what makes
music sound complete rather than thin. The MBC3 real-time clock still does not
tick.

`instr_timing`, `dmg-acid2`, and the Mooneye suite are not in `roms/`, so timer
and PPU edge cases are implemented to spec but unverified beyond what
`cpu_instrs` and real games exercise.

See `roadmap.md` for the phase plan this is tracking against.
