# MMU — memory management unit

**Source:** `src/memory/mmu.h`, `src/memory/mmu.cpp`

The central address router. Every CPU memory access lands here and is dispatched
to the right component based on the address. Owns the memory regions that do not
belong to any other component: WRAM, HRAM, the flat I/O array, and the IE
register. **VRAM and OAM belong to the PPU** — the MMU only routes to them.

The MMU holds references to `Cartridge`, `Serial`, `Timer`, `PPU`, `Joypad`,
and `APU`. It does not own them — `GameBoy` does.

## Public API

```cpp
MMU(Cartridge& cart, Serial& serial, Timer& timer, PPU& ppu, Joypad& joypad,
    APU& apu);

uint8_t read(uint16_t addr);
void    write(uint16_t addr, uint8_t val);
void    tick(uint8_t tcycles);
```

### `tick()` — the MMU is also the bus

`tick()` advances the timer, PPU and APU, then drains their interrupt flags into
IF. The CPU calls it **once per M-cycle from inside an instruction**, so a
peripheral moves partway through one rather than in a lump afterwards. A ROM
that reads a timer or LCD register mid-instruction then sees the value hardware
would have produced at that point — the difference between passing and failing
Blargg's `mem_timing`.

Interrupt routing lives here rather than in `GameBoy` for the same reason: an
interrupt raised mid-instruction must be visible to a later access within that
same instruction.

`GameBoy::step()` is consequently just `m_cpu.step()`. Note that `oamDma()` and
the trace logger call `read()` directly and so do **not** tick — neither is a
CPU bus access.

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
| `$FF00` P1 | `Joypad::read()` | `Joypad::write()` — only bits 4–5 |
| `$FF01` SB | `Serial::readSB()` | `Serial::writeSB()` |
| `$FF02` SC | `Serial::readSC()` | `Serial::writeSC()` — triggers the transfer |
| `$FF04` DIV | `Timer::div()` | `Timer::resetDiv()` — any value resets it |
| `$FF05` TIMA | `Timer::tima()` | `Timer::writeTima()` — cancels a pending overflow |
| `$FF06` TMA | `Timer::tma()` | `Timer::writeTma()` |
| `$FF07` TAC | `Timer::tac()` | `Timer::writeTac()` — can trigger a TIMA edge |
| `$FF0F` IF | `m_io` with the top 3 bits forced to 1 | `m_io` |
| `$FF10–$FF3F` | `APU::readReg()` | `APU::writeReg()` |
| `$FF40–$FF4B` except `$FF46` | `PPU::readReg()` | `PPU::writeReg()` |
| `$FF46` DMA | last source byte written | `oamDma()` — see below |

The PPU and APU ranges are matched by `isPpuReg()`/`isApuReg()` helpers in the
`default` branch rather than one case per address, since each component decodes
its own registers.

`isApuReg()` covers the whole `$FF10–$FF3F` block including the unused gap at
`$FF27–$FF2F`. Those addresses have no register behind them but must still read
as `$FF`; excluding them let them fall through to `m_io` and read back `$00`,
which failed Blargg's `dmg_sound` `01-registers`.

## OAM DMA

`$FF46` sits in the middle of the PPU's register range but is handled here,
because it is the one LCD register that reads from arbitrary memory — only the
MMU can reach the whole address space.

Writing `$XX` copies `$XX00–$XX9F` into OAM:

```cpp
for (uint16_t i = 0; i < 0xA0; ++i)
    m_ppu.writeOam(0xFE00 + i, read(src + i));
```

On hardware the copy takes 160 M-cycles, during which the CPU can only reach
HRAM. Games therefore copy a small trigger routine into HRAM and spin there
until it finishes. **Copying instantly is invisible to that pattern** — the
routine still runs, it just waits on an already-completed transfer — so games
work correctly. Real DMA timing, and blocking non-HRAM access during it, is a
later accuracy pass.

IF reads back with bits 5–7 set because those bits do not exist in hardware and
always read as 1. Some test ROMs check this.

## Current state

Address routing is complete. I/O dispatch covers only the registers that
currently have a live component behind them.

## Not implemented yet

- **No boot ROM overlay.** `$FF50` is written once at CPU construction to mark
  the boot ROM as unmapped, and `$0000–$00FF` always reads cartridge ROM. Phase
  6/13 adds the real 256-byte overlay.
- **OAM DMA is instantaneous** rather than taking 160 M-cycles, and does not
  restrict the CPU to HRAM while it runs.
- **No access restrictions.** VRAM and OAM are readable at all times; hardware
  blocks them during PPU modes 2 and 3.
- **The APU's wave-RAM access window is approximate** — see [apu.md](apu.md).
