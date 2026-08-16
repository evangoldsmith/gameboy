# PPU — pixel processing unit

**Source:** `src/ppu/ppu.h`, `src/ppu/ppu.cpp`

Owns VRAM, OAM, the LCD registers, and the framebuffer. Advances scanline
timing, renders the background, window and sprite layers, and raises the VBlank
and STAT interrupts.

Rendering is **scanline-based**: the whole line is composited at once when the
PPU enters HBlank. That covers the large majority of games. The pixel FIFO that
mid-scanline raster effects need is Phase 12.

## Public API

```cpp
void tick(uint8_t tcycles);          // expects a multiple of 4

uint8_t readVram(uint16_t addr) const;   // $8000-$9FFF, absolute address
void    writeVram(uint16_t addr, uint8_t val);
uint8_t readOam(uint16_t addr) const;    // $FE00-$FE9F
void    writeOam(uint16_t addr, uint8_t val);

uint8_t readReg(uint16_t addr) const;    // $FF40-$FF45, $FF47-$FF4B
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
| `$FF40` | LCDC | Control — see the bit table below |
| `$FF41` | STAT | Bits 0–2 are live status and not stored; only bits 3–6 are writable |
| `$FF42` | SCY | Background scroll Y |
| `$FF43` | SCX | Background scroll X |
| `$FF44` | LY | Current scanline, read-only |
| `$FF45` | LYC | Compare value for the coincidence flag |
| `$FF47` | BGP | Background and window palette |
| `$FF48` | OBP0 | Sprite palette 0 |
| `$FF49` | OBP1 | Sprite palette 1 |
| `$FF4A` | WY | Window Y position |
| `$FF4B` | WX | Window X position, **offset by 7** |

`$FF46` (OAM DMA) is deliberately *not* here — it copies from anywhere in the
address space, so only the MMU can service it. See [mmu.md](mmu.md).

### LCDC bits

| Bit | Meaning |
|---|---|
| 7 | LCD enable |
| 6 | Window tile map (`$9800` / `$9C00`) |
| 5 | Window enable |
| 4 | BG and window tile data (`$8800` signed / `$8000` unsigned) |
| 3 | BG tile map (`$9800` / `$9C00`) |
| 2 | Sprite size (8×8 / 8×16) |
| 1 | Sprite enable |
| 0 | BG and window enable |

`m_lcdc`, `m_bgp`, `m_obp0` and `m_obp1` start at the values the boot ROM would
leave behind (`$91`, `$FC`, `$FF`, `$FF`), since we skip it.

**Turning the LCD off** resets LY, the dot counter and the window line counter,
and parks the PPU in HBlank. `tick()` returns immediately while it is off.

## Timing

```
DOTS_PER_LINE   = 456      OAM_SCAN_DOTS = 80
LINES_PER_FRAME = 154      TRANSFER_DOTS = 172
VBLANK_LINE     = 144
```

`tick()` steps 4 dots at a time via `step4()`, matching the timer. Fixed
M-cycle steps guarantee at most one line advance per iteration, which is what
lets the render hook fire exactly once per line (guarded by `m_lineRendered`).

Mode 3 is a fixed 172 dots; hardware varies it from 172–289 with sprite count
and `SCX`.

## Compositing order

`renderScanline()` builds a line in three passes:

```cpp
LineIndices bgIndex{};        // pre-palette colour index per pixel
renderBackground(bgIndex);
if (windowVisibleOnLine()) { renderWindow(bgIndex); ++m_windowLine; }
if (LCDC & OBJ_ENABLE)       renderSprites(bgIndex);
```

`bgIndex` carries the **colour index before palette mapping**, not the final
shade. Sprites need it because their priority rule is defined against index 0
versus 1–3, which palette mapping would destroy — BGP could map index 0 to any
shade.

## Background

1. **Scroll.** `bgY = LY + SCY`, `bgX = SCX + x`, both `uint8_t` so they wrap
   within the 256×256 map for free.
2. **Tile map.** LCDC bit 3 picks `$9800` or `$9C00` (VRAM offsets `$1800` /
   `$1C00`); the entry is at `mapBase + (bgY/8)*32 + bgX/8`.
3. **Tile data.** LCDC bit 4 switches addressing:
   - **Set** — `$8000` base, *unsigned* index: `tileAddr = idx * 16`.
   - **Clear** — `$9000` base, *signed* index: `0x1000 + int8_t(idx) * 16`.
4. **Pixels.** `tilePixel()` reads the 2-byte row and combines bit *n* of each:
   the low byte holds the LSBs and the high byte the MSBs, interleaved across
   the pair rather than packed consecutively. Bit 7 is leftmost.
5. **Palette.** `shade = (BGP >> (colorIdx * 2)) & 3`.

With LCDC bit 0 clear the line fills with shade 0 and `bgIndex` stays zeroed, so
sprites still draw over it.

## Window

A second tile layer drawn over the background, using the same tile data and the
same BGP palette but its own tile map (LCDC bit 6).

Visible on a line when the window is enabled, `LY >= WY`, and `WX <= 166`.

**WX is offset by 7**: `WX == 7` puts the left edge at screen x=0, and smaller
values push it off the left edge. `startX` is therefore an `int`, not a
`uint8_t`, because it legitimately goes negative.

The window keeps **its own line counter**, `m_windowLine`, which advances only
on lines where the window actually drew — not with LY. Disabling and re-enabling
the window mid-frame resumes where it left off rather than restarting. It resets
when LY wraps to 0 and when the LCD is switched off.

## Sprites

**Selection.** Mode 2 scans the 40 OAM entries in order and keeps the first ten
whose Y range covers the line. Sprites off-screen horizontally still consume a
slot — that limit is about OAM scanning, not visibility.

Each entry is 4 bytes: Y, X, tile index, attributes. Positions are offset so
sprites can scroll off the top and left edges: `screenY = Y - 16`,
`screenX = X - 8`.

**Attribute bits:**

| Bit | Meaning |
|---|---|
| 7 | Behind BG colours 1–3 (still in front of colour 0) |
| 6 | Y flip |
| 5 | X flip |
| 4 | Palette: OBP0 / OBP1 |

**Priority.** On DMG the smaller X draws on top, with OAM order breaking ties —
a `std::stable_sort` on X, since stability gives the tie-break for free.

Then the subtle part:

```cpp
if (claimed[xi]) continue;
...
if (colorIdx == 0) continue;      // transparent: does not claim
claimed[xi] = true;
if ((attr & OBJ_BG_PRIORITY) && bgIndex[xi] != 0) continue;   // claimed, not drawn
```

`claimed` is tracked **separately from drawn**. A sprite that wins arbitration
for a pixel blocks every lower-priority sprite there even when it then loses to
the background. Collapsing these two into one flag lets a lower sprite show
through a higher one, which hardware does not do.

**Colour 0 is transparent** for sprites — it is not a palette entry, which is
why OBP entry 0 is never read.

**8×16 mode** ignores the index's low bit. The two tiles are contiguous in VRAM,
so masking to `tile & 0xFE` and letting the row run 0–15 indexes straight
through both. Sprites always use `$8000` addressing regardless of LCDC bit 4.

## The STAT interrupt

Edge-triggered on the **OR of every enabled condition**, not on each separately:

```cpp
if (line && !m_statLine) m_statIrq = true;
m_statLine = line;
```

Conditions overlap, so without the shared line, moving from one selected mode
directly into another would raise two interrupts where hardware raises one.

## Interrupt routing

`takeVBlankIrq()` and `takeStatIrq()` are read-and-clear, drained by
`GameBoy::step()`. The PPU holds no MMU reference, matching the timer and serial
port — see [gameboy.md](gameboy.md).

`takeFrameReady()` exists for a frontend that wants to present exactly on frame
completion. `main.cpp` uploads after every `runFrame()` instead, so nothing
consumes it yet.

## The LY stub

`setLyStub(true)` pins LY reads to `$90` for Game Boy Doctor trace comparison.
Scoped to `--doctor`. See [logger.md](logger.md).

## Current state

Complete for roadmap Phases 6 and 7. Verified against Pokémon Red: the title
screen renders correctly, and dumping OAM after 1500 frames shows 35 on-screen
sprites with coherent positions and sequential tile indices, confirming both
OAM DMA and sprite compositing.

## Not implemented yet

- **`dmg-acid2` has not been run** — it is not in `roms/`, and it is the
  objective check for everything in this file. Sprite priority, the window line
  counter and the 10-sprite limit are implemented to spec but only verified
  against real games so far.
- **No VRAM/OAM access restrictions.** Hardware blocks CPU access during modes 2
  and 3; here it is always allowed.
- **Mode 3 length is fixed**, so mid-scanline raster effects will not render
  correctly.
- **No sprite count penalty on mode 3 timing** — each sprite should extend it by
  6–11 dots.
- **No STAT interrupt bug** — the DMG spurious-interrupt-on-write behaviour.
  Phase 12.
- **No OAM bug** — the DMG OAM corruption on 16-bit access during mode 2.
- **No pixel FIFO.** Phase 12 replaces the scanline renderer if high accuracy is
  the goal.
