# PPU — pixel processing unit

**Source:** `src/ppu/ppu.h`, `src/ppu/ppu.cpp`

Owns VRAM, OAM, the LCD registers, and the framebuffer. Advances scanline
timing, renders the background layer, and raises the VBlank and STAT
interrupts.

Rendering is **scanline-based**: the whole line is drawn at once when the PPU
reaches HBlank. That covers the large majority of games. The pixel FIFO that
mid-scanline raster effects need is Phase 12.

## Public API

```cpp
void tick(uint8_t tcycles);          // expects a multiple of 4

uint8_t readVram(uint16_t addr) const;   // $8000-$9FFF, absolute address
void    writeVram(uint16_t addr, uint8_t val);
uint8_t readOam(uint16_t addr) const;    // $FE00-$FE9F
void    writeOam(uint16_t addr, uint8_t val);

uint8_t readReg(uint16_t addr) const;    // $FF40-$FF45, $FF47
void    writeReg(uint16_t addr, uint8_t val);

const Framebuffer& framebuffer() const;  // std::array<uint32_t, 160*144>, ARGB8888

bool takeVBlankIrq();
bool takeStatIrq();
bool takeFrameReady();

void setLyStub(bool on);
```

## Registers

| Address | Name | Notes |
|---|---|---|
| `$FF40` | LCDC | Control. Bit 7 LCD enable, bit 4 tile data select, bit 3 BG map select, bit 0 BG enable |
| `$FF41` | STAT | Bits 0–2 are live status and not stored; only bits 3–6 are writable |
| `$FF42` | SCY | Background scroll Y |
| `$FF43` | SCX | Background scroll X |
| `$FF44` | LY | Current scanline, read-only |
| `$FF45` | LYC | Compare value for the coincidence flag |
| `$FF47` | BGP | Background palette |

`m_lcdc` and `m_bgp` are initialised to `$91` and `$FC` — the values the boot
ROM leaves behind, since we skip it.

**Turning the LCD off** (clearing LCDC bit 7) resets LY and the dot counter and
parks the PPU in HBlank. `tick()` returns immediately while it is off. Games do
this to get unrestricted VRAM access.

## Timing

```
DOTS_PER_LINE   = 456      OAM_SCAN_DOTS = 80
LINES_PER_FRAME = 154      TRANSFER_DOTS = 172
VBLANK_LINE     = 144
```

144 visible lines plus 10 of VBlank at 456 dots each — 70,224 T-cycles per
frame, which is where `GameBoy::TCYCLES_PER_FRAME` comes from.

`tick()` steps 4 dots at a time via `step4()`, matching the timer's approach.
Stepping in fixed M-cycle units guarantees at most one line advance per
iteration, which is what lets the render hook below fire exactly once per line.

Each `step4()`:

1. Advances `m_dot`, wrapping into the next LY at 456.
2. Sets `m_vblankIrq` and `m_frameReady` on entering line 144.
3. Recomputes the mode from the position within the line.
4. Renders the scanline if this is the first step to reach HBlank on a visible
   line — guarded by `m_lineRendered`, which resets on each line advance.
5. Updates the STAT interrupt line.

Mode 3 is a fixed 172 dots. On hardware it is 172–289 depending on sprite count
and `SCX`.

## Background rendering

`renderScanline()` walks the 160 pixels of the current line:

1. **Scroll.** `bgY = LY + SCY` and `bgX = SCX + x`, both as `uint8_t` so they
   wrap within the 256×256 background map for free.
2. **Tile map lookup.** LCDC bit 3 selects `$9800` or `$9C00` (VRAM offsets
   `$1800` / `$1C00`). The map is 32×32 bytes of tile indices, so the entry is
   at `mapBase + (bgY/8)*32 + bgX/8`.
3. **Tile data addressing.** LCDC bit 4 picks between two modes:
   - **Set** — `$8000` base, *unsigned* index: `tileAddr = idx * 16`.
   - **Clear** — `$9000` base, *signed* index: `tileAddr = 0x1000 + int8_t(idx) * 16`.

   This is the part that trips people up: the same byte means different tiles
   depending on one LCDC bit.
4. **Pixel extraction.** Each 8×8 tile is 16 bytes, 2 bytes per row, 2 bits per
   pixel. The low byte holds the LSBs of the row's colour indices and the high
   byte the MSBs — they are *interleaved across two bytes*, not packed
   consecutively. Bit 7 is the leftmost pixel.
5. **Palette.** BGP maps each 2-bit colour index to one of four shades, two bits
   per entry: `shade = (BGP >> (colorIdx * 2)) & 3`.

Shades are written as ARGB8888 from a fixed DMG-green palette (`kShades`).

When LCDC bit 0 clears the background off, the line fills with shade 0 rather
than being left stale.

## The STAT interrupt

STAT is edge-triggered on the **OR of every enabled condition**, not on each
condition separately. `updateStatLine()` computes that single boolean and fires
only on a false → true transition:

```cpp
if (line && !m_statLine) m_statIrq = true;
m_statLine = line;
```

This matters because conditions overlap. Without the shared line, moving from
one selected mode straight into another selected mode would raise two
interrupts where hardware raises one.

## Interrupt routing

`takeVBlankIrq()` and `takeStatIrq()` are read-and-clear, drained by
`GameBoy::step()`. The PPU has no MMU reference, matching the pattern the timer
and serial port use — see [gameboy.md](gameboy.md).

`takeFrameReady()` exists for a frontend that wants to present exactly on frame
completion. `main.cpp` currently uploads after every `runFrame()` instead, so
nothing consumes it yet.

## The LY stub

`setLyStub(true)` makes LY reads return `$90` unconditionally, because Game Boy
Doctor's reference logs were captured that way. Scoped to `--doctor`; the normal
path uses the real counter. See [logger.md](logger.md).

## Current state

Complete for roadmap Phase 6. Verified by running Blargg's `cpu_instrs`
headlessly for 4000 frames and dumping the framebuffer — the full results
screen renders correctly, including all 11 `ok` lines and "Passed all tests".

## Not implemented yet

- **No sprites.** OAM is stored and routed but never read by the renderer.
  Phase 7.
- **No window layer.** LCDC bits 5–6 are stored and ignored; WY/WX (`$FF4A`,
  `$FF4B`) are still inert MMU slots.
- **No OAM DMA** (`$FF46`). Phase 7.
- **No sprite palettes** OBP0/OBP1 (`$FF48`, `$FF49`).
- **No VRAM/OAM access restrictions.** Hardware blocks CPU access during modes
  2 and 3; here it is always allowed.
- **Mode 3 length is fixed**, so mid-scanline raster effects will not render
  correctly.
- **No STAT interrupt bug** — the DMG spurious-interrupt-on-write behaviour.
  Phase 12.
- **No pixel FIFO.** Phase 12 replaces the scanline renderer if high accuracy is
  the goal.
