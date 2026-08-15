# PPU — pixel processing unit

**Source:** `src/ppu/ppu.h`, `src/ppu/ppu.cpp`

> **Status: scanline timing only.** Nothing is rendered. This exists so ROMs can
> get past their VBlank wait loops — see "Why this exists" below.

Eventually: tile and sprite rendering into a `160×144` framebuffer, VRAM and OAM
access rules, and the STAT interrupt.

## Why this exists in its current form

Almost every ROM, including Blargg's CPU tests, spins on LY (`$FF44`) waiting
for VBlank before doing anything else. With LY hardwired to zero the CPU never
escapes the first wait loop, so no amount of correct instruction decoding
produces observable progress. A free-running line counter is the minimum needed
to unblock CPU work.

## Public API

```cpp
void tick(uint8_t tcycles);

uint8_t ly() const;        // $FF44
PPUMode mode() const;
uint8_t stat() const;      // $FF41

bool takeVBlankIrq();      // consumes the flag
void setLyStub(bool on);
```

## Timing

```cpp
DOTS_PER_LINE   = 456
LINES_PER_FRAME = 154
VBLANK_LINE     = 144
```

144 visible lines plus 10 of VBlank, at 456 dots each — 70,224 T-cycles per
frame, which is where `GameBoy::TCYCLES_PER_FRAME` comes from.

`tick()` accumulates T-cycles into `m_dot`. Each time it crosses 456 it wraps and
advances LY, wrapping at 154. Crossing into line 144 sets `m_vblankIrq`.

The loop is a `while`, not an `if`, because a single instruction can consume up
to 24 T-cycles and nothing guarantees a caller ticks often enough — the
arithmetic stays correct at any granularity.

Mode is then derived from the position within the line:

| Condition | Mode |
|---|---|
| `LY >= 144` | `VBlank` (1) |
| `dot < 80` | `OAMScan` (2) |
| `dot < 252` | `PixelTransfer` (3) |
| otherwise | `HBlank` (0) |

Mode 3 is a fixed 172 dots here. On hardware it is 172–289 depending on sprite
count and `SCX`, which is what makes mid-scanline effects work.

## `stat()`

Returns `0x80 | mode`. Bit 7 is unused and always reads 1. The LYC coincidence
flag and the interrupt-select bits are not implemented.

## `takeVBlankIrq()`

Read-and-clear. `GameBoy::step()` calls it after every instruction and converts
a true return into an IF bit. The PPU has no MMU reference, so it cannot request
the interrupt itself — this keeps the dependency one-directional.

## The LY stub

`setLyStub(true)` makes `ly()` return `$90` unconditionally.

Game Boy Doctor's reference logs were captured with LY hardwired to `$90`, so
traces only line up against them with the stub on. `main.cpp` enables it in
`--doctor` mode and leaves it off everywhere else.

## Current state

LY and mode advance correctly, and VBlank interrupts fire. Nothing draws.

## Not implemented yet

Roadmap Phases 6, 7, and 12:

- **No framebuffer and no rendering** — no background, window, or sprites.
- **No VRAM or OAM ownership.** Those arrays live in the MMU and the PPU cannot
  see them.
- **No LCDC handling** (`$FF40`). The LCD cannot be turned off; `tick()` runs
  unconditionally.
- **No STAT interrupt.** The mode and LYC interrupt-select bits are ignored, and
  the edge-triggering rule is not implemented.
- **No LYC register** (`$FF45`) or coincidence flag.
- **Mode 3 length is fixed**, so mid-scanline raster effects will not work.
- **No pixel FIFO** — Phase 12 replaces the whole approach if high accuracy is
  the goal.
