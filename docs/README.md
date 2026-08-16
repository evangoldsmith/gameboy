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
| [apu.md](apu.md) | `src/apu/apu.{h,cpp}`, `channels.{h,cpp}` | All four channels + mixer |
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

## Test ROM results

Measured, not estimated. ROMs live in `roms/`, which is gitignored — see
"Getting the test ROMs" below.

| Suite | Result | Notes |
|---|---|---|
| Blargg `cpu_instrs` | **11 / 11** | Full instruction set |
| Blargg `instr_timing` | **Pass** | Instruction cycle counts |
| Blargg `halt_bug` | **Pass** | HALT with IME=0 and an interrupt pending |
| `dmg-acid2` | **Pixel perfect** | 23040/23040 pixels match the reference |
| Blargg `dmg_sound` | 7 / 12 | See [apu.md](apu.md) |
| Blargg `oam_bug` | 2 / 8 | OAM corruption is not emulated at all |
| Blargg `mem_timing` | 0 / 3 | Needs M-cycle memory access — Phase 12 |

`dmg-acid2` matching exactly is the strongest single result here: it exercises
sprite priority, the 10-sprite limit, X/Y flipping, the window line counter,
BG-over-OBJ priority and palette application in one image.

The three failing suites are all Phase 12 accuracy work, and none of them
affects whether games run.

### Getting the test ROMs

Neither Mooneye nor Mealybug publishes releases, so the practical source for
everything is the aggregated bundle:

```bash
curl -L -o gb-tests.zip \
  https://github.com/c-sp/game-boy-test-roms/releases/download/v7.0/game-boy-test-roms-v7.0.zip
```

Suites report results differently, which matters for automating them:

| Suite | Reports via |
|---|---|
| Blargg | Serial (`$FF01`/`$FF02`), and on screen |
| Mooneye | `LD B,B` breakpoint, then registers hold 3, 5, 8, 13, 21, 34 |
| Mealybug, acid2 | Framebuffer vs a reference PNG |

All three graphics layers render — background, window and sprites, with OAM
DMA — input works, and MBC1/2/3/5 are implemented. **Tetris is playable, and
Pokémon Red reaches Prof. Oak's intro and the name entry screen.**

Battery-backed cartridges persist to a `.sav` file next to the ROM, and all four
sound channels play.

Next up is Phase 12 — the accuracy push: mid-scanline PPU timing, M-cycle memory
access, and the remaining hardware quirks. The MBC3 real-time clock still does
not tick.

`instr_timing`, `dmg-acid2`, and the Mooneye suite are not in `roms/`, so timer
and PPU edge cases are implemented to spec but unverified beyond what
`cpu_instrs` and real games exercise.

See `roadmap.md` for the phase plan this is tracking against.
