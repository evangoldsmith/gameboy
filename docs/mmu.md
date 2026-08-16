# MMU — memory management unit

**Source:** `src/memory/mmu.h`, `src/memory/mmu.cpp`

The central address router. Every CPU memory access lands here and is dispatched
to the right component based on the address. Owns the memory regions that do not
belong to any other component: WRAM, HRAM, the flat I/O array, and the IE
register. **VRAM and OAM belong to the PPU** — the MMU only routes to them.

The MMU holds references to `Cartridge`, `Serial`, `Timer`, and `PPU`. It does
not own them — `GameBoy` does.

## Public API

```cpp
MMU(Cartridge& cart, Serial& serial, Timer& timer, PPU& ppu);

uint8_t read(uint16_t addr);
void    write(uint16_t addr, uint8_t val);
```

### Why `read` is not `const`

Reading an I/O register can observe live subsystem state, and some registers
mutate on read. Making this const would force `mutable` or `const_cast` the
moment the Phase 4 timer edge cases land, so the qualifier is off from the
start.

## Address map

| Range | Destination | Notes |
|---|---|---|
| `$0000–$7FFF` | `Cartridge::read/write` | ROM; writes go to MBC registers |
| `$8000–$9FFF` | `PPU::readVram/writeVram` | 8 KB, owned by the PPU |
| `$A000–$BFFF` | `Cartridge::read/write` | External RAM, if present |
| `$C000–$DFFF` | `m_wram` | 8 KB |
| `$E000–$FDFF` | `m_wram` | Echo RAM, mirrors `$C000–$DDFF` |
| `$FE00–$FE9F` | `PPU::readOam/writeOam` | 160 B, 40 sprite entries, owned by the PPU |
| `$FEA0–$FEFF` | — | Unusable; reads `$FF`, writes ignored |
| `$FF00–$FF7F` | `readIO`/`writeIO` | 128 B |
| `$FF80–$FFFE` | `m_hram` | 127 B |
| `$FFFF` | `m_ie` | Interrupt enable |

Both `read` and `write` are ordered if-chains from low addresses upward, so each
test only needs an upper bound.

Echo RAM is implemented as a straight `m_wram[addr - 0xE000]` index. The region
is `$1E00` bytes but WRAM is `$2000`, so the top of WRAM is simply not mirrored,
which matches hardware.

## I/O dispatch

`readIO`/`writeIO` special-case the registers a component reacts to. Anything
not listed falls through to `m_io`, a flat 128-byte array — correct for
registers nothing responds to yet.

| Register | Read | Write |
|---|---|---|
| `$FF01` SB | `Serial::readSB()` | `Serial::writeSB()` |
| `$FF02` SC | `Serial::readSC()` | `Serial::writeSC()` — triggers the transfer |
| `$FF04` DIV | `Timer::div()` | `Timer::resetDiv()` — any value resets it |
| `$FF05` TIMA | `Timer::tima()` | `Timer::writeTima()` — cancels a pending overflow |
| `$FF06` TMA | `Timer::tma()` | `Timer::writeTma()` |
| `$FF07` TAC | `Timer::tac()` | `Timer::writeTac()` — can trigger a TIMA edge |
| `$FF0F` IF | `m_io` with the top 3 bits forced to 1 | `m_io` |
| `$FF40–$FF45`, `$FF47` | `PPU::readReg()` | `PPU::writeReg()` |

The PPU register range is matched by an `isPpuReg()` helper in the `default`
branch rather than one case per address, since the PPU decodes them itself.

IF reads back with bits 5–7 set because those bits do not exist in hardware and
always read as 1. Some test ROMs check this.

## Current state

Address routing is complete. I/O dispatch covers only the registers that
currently have a live component behind them.

## Not implemented yet

- **No boot ROM overlay.** `$FF50` is written once at CPU construction to mark
  the boot ROM as unmapped, and `$0000–$00FF` always reads cartridge ROM. Phase
  6/13 adds the real 256-byte overlay.
- **No OAM DMA** (`$FF46`). Writes currently land in `m_io` and do nothing.
  Phase 7.
- **No access restrictions.** VRAM and OAM are readable at all times; hardware
  blocks them during PPU modes 2 and 3.
- **No APU or joypad registers** — `$FF00` and `$FF10–$FF3F` are inert array
  slots, as are `$FF48–$FF4B` (sprite palettes and window position).
