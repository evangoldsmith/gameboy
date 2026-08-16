#include "ppu.h"

#include <algorithm>

namespace {

// LCDC ($FF40)
constexpr uint8_t LCDC_BG_ENABLE  = 0x01;
constexpr uint8_t LCDC_OBJ_ENABLE = 0x02;
constexpr uint8_t LCDC_OBJ_SIZE   = 0x04;  // 0 = 8x8, 1 = 8x16
constexpr uint8_t LCDC_BG_MAP     = 0x08;  // 0 = $9800, 1 = $9C00
constexpr uint8_t LCDC_TILE_DATA  = 0x10;  // 0 = $8800 signed, 1 = $8000 unsigned
constexpr uint8_t LCDC_WIN_ENABLE = 0x20;
constexpr uint8_t LCDC_WIN_MAP    = 0x40;  // 0 = $9800, 1 = $9C00
constexpr uint8_t LCDC_ENABLE     = 0x80;

// OAM attribute byte
constexpr uint8_t OBJ_BG_PRIORITY = 0x80;  // sprite goes behind BG colours 1-3
constexpr uint8_t OBJ_Y_FLIP      = 0x40;
constexpr uint8_t OBJ_X_FLIP      = 0x20;
constexpr uint8_t OBJ_PALETTE     = 0x10;  // 0 = OBP0, 1 = OBP1

// STAT ($FF41) interrupt-select bits
constexpr uint8_t STAT_LYC_IE    = 0x40;
constexpr uint8_t STAT_OAM_IE    = 0x20;
constexpr uint8_t STAT_VBLANK_IE = 0x10;
constexpr uint8_t STAT_HBLANK_IE = 0x08;
constexpr uint8_t STAT_LYC_FLAG  = 0x04;

// Mode 3 is 172-289 dots on hardware depending on sprites and SCX. Fixed here;
// variable length is Phase 12.
constexpr uint16_t OAM_SCAN_DOTS = 80;
constexpr uint16_t TRANSFER_DOTS = 172;

// Sprites are positioned with an offset so they can scroll off the top and left
// edges: a sprite at screen y=0 is stored as 16, at screen x=0 as 8.
constexpr int OBJ_Y_OFFSET = 16;
constexpr int OBJ_X_OFFSET = 8;

// The four DMG shades, lightest to darkest, as ARGB8888.
constexpr uint32_t kShades[4] = {0xFF9BBC0F, 0xFF8BAC0F, 0xFF306230, 0xFF0F380F};

constexpr uint8_t applyPalette(uint8_t palette, uint8_t colorIdx) {
    return static_cast<uint8_t>((palette >> (colorIdx * 2)) & 0x03);
}

}  // namespace

// ── Memory ───────────────────────────────────────────────────────────────────

uint8_t PPU::readVram(uint16_t addr) const { return m_vram[addr - 0x8000]; }
void    PPU::writeVram(uint16_t addr, uint8_t val) { m_vram[addr - 0x8000] = val; }

uint8_t PPU::readOam(uint16_t addr) const { return m_oam[addr - 0xFE00]; }
void    PPU::writeOam(uint16_t addr, uint8_t val) { m_oam[addr - 0xFE00] = val; }

// ── Registers ────────────────────────────────────────────────────────────────

uint8_t PPU::readReg(uint16_t addr) const {
    switch (addr) {
        case 0xFF40: return m_lcdc;
        case 0xFF41:
            // Bit 7 is unused and reads 1. Bits 0-2 are live status, not stored.
            return static_cast<uint8_t>(0x80 | (m_stat & 0x78) |
                                        (lycEqual() ? STAT_LYC_FLAG : 0) |
                                        static_cast<uint8_t>(m_mode));
        case 0xFF42: return m_scy;
        case 0xFF43: return m_scx;
        case 0xFF44: return ly();
        case 0xFF45: return m_lyc;
        case 0xFF47: return m_bgp;
        case 0xFF48: return m_obp0;
        case 0xFF49: return m_obp1;
        case 0xFF4A: return m_wy;
        case 0xFF4B: return m_wx;
        default:     return 0xFF;
    }
}

void PPU::writeReg(uint16_t addr, uint8_t val) {
    switch (addr) {
        case 0xFF40: {
            const bool wasOn = (m_lcdc & LCDC_ENABLE) != 0;
            m_lcdc = val;
            // Turning the LCD off resets the line counters. Games do this to get
            // unrestricted VRAM access.
            if (wasOn && (m_lcdc & LCDC_ENABLE) == 0) {
                m_ly          = 0;
                m_dot         = 0;
                m_mode        = PPUMode::HBlank;
                m_windowLine  = 0;
                m_lineRendered = false;
            }
            break;
        }
        case 0xFF41: m_stat = static_cast<uint8_t>(val & 0x78); break;  // only bits 3-6 writable
        case 0xFF42: m_scy  = val; break;
        case 0xFF43: m_scx  = val; break;
        case 0xFF44: break;  // LY is read-only
        case 0xFF45: m_lyc  = val; break;
        case 0xFF47: m_bgp  = val; break;
        case 0xFF48: m_obp0 = val; break;
        case 0xFF49: m_obp1 = val; break;
        case 0xFF4A: m_wy   = val; break;
        case 0xFF4B: m_wx   = val; break;
        default: break;
    }
}

// ── Timing ───────────────────────────────────────────────────────────────────

PPUMode PPU::computeMode() const {
    if (m_ly >= VBLANK_LINE)                   return PPUMode::VBlank;
    if (m_dot < OAM_SCAN_DOTS)                 return PPUMode::OAMScan;
    if (m_dot < OAM_SCAN_DOTS + TRANSFER_DOTS) return PPUMode::PixelTransfer;
    return PPUMode::HBlank;
}

void PPU::step4() {
    m_dot = static_cast<uint16_t>(m_dot + 4);

    if (m_dot >= DOTS_PER_LINE) {
        m_dot = static_cast<uint16_t>(m_dot - DOTS_PER_LINE);
        m_ly  = static_cast<uint8_t>((m_ly + 1) % LINES_PER_FRAME);
        m_lineRendered = false;

        if (m_ly == 0) m_windowLine = 0;  // new frame

        if (m_ly == VBLANK_LINE) {
            m_vblankIrq  = true;
            m_frameReady = true;
        }
    }

    m_mode = computeMode();

    // Draw the line once, when the PPU first reaches HBlank on a visible line.
    if (m_mode == PPUMode::HBlank && !m_lineRendered && m_ly < VBLANK_LINE) {
        renderScanline();
        m_lineRendered = true;
    }

    updateStatLine();
}

void PPU::tick(uint8_t tcycles) {
    if ((m_lcdc & LCDC_ENABLE) == 0) return;  // LCD off: nothing advances

    for (uint8_t done = 0; done + 4 <= tcycles; done = static_cast<uint8_t>(done + 4))
        step4();
}

// The STAT interrupt is edge-triggered on the OR of every enabled condition,
// not on each condition individually. If two selected conditions overlap, only
// the transition from none-active to some-active fires an interrupt.
void PPU::updateStatLine() {
    bool line = false;
    if ((m_stat & STAT_LYC_IE) != 0 && lycEqual())                   line = true;
    if ((m_stat & STAT_OAM_IE) != 0 && m_mode == PPUMode::OAMScan)   line = true;
    if ((m_stat & STAT_VBLANK_IE) != 0 && m_mode == PPUMode::VBlank) line = true;
    if ((m_stat & STAT_HBLANK_IE) != 0 && m_mode == PPUMode::HBlank) line = true;

    if (line && !m_statLine) m_statIrq = true;
    m_statLine = line;
}

// ── Rendering ────────────────────────────────────────────────────────────────

uint8_t PPU::tilePixel(uint16_t tileAddr, uint8_t row, int bit) const {
    // Two bytes per row, 2 bits per pixel: the low byte holds the LSBs of the
    // row's colour indices and the high byte the MSBs, interleaved across the
    // pair rather than packed consecutively.
    const uint16_t off = static_cast<uint16_t>(tileAddr + row * 2);
    const uint8_t  lo  = m_vram[off];
    const uint8_t  hi  = m_vram[off + 1];
    return static_cast<uint8_t>((((hi >> bit) & 1) << 1) | ((lo >> bit) & 1));
}

void PPU::renderScanline() {
    LineIndices bgIndex{};  // pre-palette colour index, needed for sprite priority

    renderBackground(bgIndex);

    if (windowVisibleOnLine()) {
        renderWindow(bgIndex);
        m_windowLine = static_cast<uint8_t>(m_windowLine + 1);
    }

    if ((m_lcdc & LCDC_OBJ_ENABLE) != 0) renderSprites(bgIndex);
}

void PPU::renderBackground(LineIndices& bgIndex) {
    const std::size_t rowStart = static_cast<std::size_t>(m_ly) * WIDTH;

    if ((m_lcdc & LCDC_BG_ENABLE) == 0) {
        // BG disabled renders as colour 0 and leaves bgIndex zeroed, so sprites
        // with the BG-priority bit still draw over it.
        for (int x = 0; x < WIDTH; ++x)
            m_fb[rowStart + static_cast<std::size_t>(x)] = kShades[0];
        return;
    }

    // Scrolling wraps within the 256x256 background map.
    const uint8_t  bgY     = static_cast<uint8_t>(m_ly + m_scy);
    const uint16_t mapBase = (m_lcdc & LCDC_BG_MAP) != 0 ? 0x1C00 : 0x1800;
    const uint16_t rowOff  = static_cast<uint16_t>((bgY / 8) * 32);

    for (int x = 0; x < WIDTH; ++x) {
        const uint8_t bgX     = static_cast<uint8_t>(m_scx + x);
        const uint8_t tileIdx = m_vram[mapBase + rowOff + (bgX / 8)];

        // LCDC bit 4 picks the addressing mode: $8000 with an unsigned index,
        // or $9000 with a signed one (VRAM offsets $0000 and $1000).
        const uint16_t tileAddr =
            (m_lcdc & LCDC_TILE_DATA) != 0
                ? static_cast<uint16_t>(tileIdx * 16)
                : static_cast<uint16_t>(0x1000 + static_cast<int8_t>(tileIdx) * 16);

        const uint8_t colorIdx = tilePixel(tileAddr, static_cast<uint8_t>(bgY % 8),
                                           7 - (bgX % 8));

        bgIndex[static_cast<std::size_t>(x)] = colorIdx;
        m_fb[rowStart + static_cast<std::size_t>(x)] = kShades[applyPalette(m_bgp, colorIdx)];
    }
}

bool PPU::windowVisibleOnLine() const {
    return (m_lcdc & LCDC_WIN_ENABLE) != 0 && m_ly >= m_wy && m_wx <= 166;
}

void PPU::renderWindow(LineIndices& bgIndex) {
    const std::size_t rowStart = static_cast<std::size_t>(m_ly) * WIDTH;

    // WX is offset by 7, so WX=7 puts the window's left edge at screen x=0.
    // Smaller values push it off the left edge.
    const int startX = static_cast<int>(m_wx) - 7;

    const uint16_t mapBase = (m_lcdc & LCDC_WIN_MAP) != 0 ? 0x1C00 : 0x1800;
    const uint16_t rowOff  = static_cast<uint16_t>((m_windowLine / 8) * 32);

    for (int x = std::max(0, startX); x < WIDTH; ++x) {
        const int winX = x - startX;  // column within the window

        const uint8_t tileIdx =
            m_vram[mapBase + rowOff + static_cast<unsigned>(winX / 8)];

        const uint16_t tileAddr =
            (m_lcdc & LCDC_TILE_DATA) != 0
                ? static_cast<uint16_t>(tileIdx * 16)
                : static_cast<uint16_t>(0x1000 + static_cast<int8_t>(tileIdx) * 16);

        const uint8_t colorIdx = tilePixel(tileAddr, static_cast<uint8_t>(m_windowLine % 8),
                                           7 - (winX % 8));

        bgIndex[static_cast<std::size_t>(x)] = colorIdx;
        m_fb[rowStart + static_cast<std::size_t>(x)] = kShades[applyPalette(m_bgp, colorIdx)];
    }
}

void PPU::renderSprites(const LineIndices& bgIndex) {
    const int height = (m_lcdc & LCDC_OBJ_SIZE) != 0 ? 16 : 8;
    const int line   = static_cast<int>(m_ly);

    // Mode 2 scans OAM in order and keeps the first ten overlapping sprites.
    // Sprites off-screen horizontally still consume one of those slots.
    std::array<Sprite, MAX_SPRITES_PER_LINE> selected{};
    int count = 0;

    for (int i = 0; i < 40 && count < MAX_SPRITES_PER_LINE; ++i) {
        const std::size_t base = static_cast<std::size_t>(i) * 4;
        const int spriteY = static_cast<int>(m_oam[base]) - OBJ_Y_OFFSET;

        if (line >= spriteY && line < spriteY + height) {
            selected[static_cast<std::size_t>(count)] =
                Sprite{m_oam[base], m_oam[base + 1], m_oam[base + 2], m_oam[base + 3],
                       static_cast<uint8_t>(i)};
            ++count;
        }
    }

    // On DMG the sprite with the smaller X draws on top; OAM order breaks ties.
    const auto last = selected.begin() + static_cast<std::ptrdiff_t>(count);
    std::stable_sort(selected.begin(), last,
                     [](const Sprite& a, const Sprite& b) { return a.x < b.x; });

    const std::size_t rowStart = static_cast<std::size_t>(m_ly) * WIDTH;

    // A sprite that wins arbitration for a pixel blocks lower-priority sprites
    // even when it then loses to the background, so "claimed" is tracked
    // separately from "drawn".
    std::array<bool, static_cast<std::size_t>(WIDTH)> claimed{};

    for (int s = 0; s < count; ++s) {
        const Sprite& sp = selected[static_cast<std::size_t>(s)];

        const int spriteY = static_cast<int>(sp.y) - OBJ_Y_OFFSET;
        const int spriteX = static_cast<int>(sp.x) - OBJ_X_OFFSET;

        int row = line - spriteY;
        if ((sp.attr & OBJ_Y_FLIP) != 0) row = height - 1 - row;

        // In 8x16 mode the index's low bit is ignored; the two tiles are
        // contiguous, so a row of 0-15 indexes straight through both.
        const uint8_t  tileIdx  = height == 16 ? static_cast<uint8_t>(sp.tile & 0xFE) : sp.tile;
        const uint16_t tileAddr = static_cast<uint16_t>(tileIdx * 16);  // sprites always use $8000

        const uint8_t palette = (sp.attr & OBJ_PALETTE) != 0 ? m_obp1 : m_obp0;

        for (int px = 0; px < 8; ++px) {
            const int x = spriteX + px;
            if (x < 0 || x >= WIDTH) continue;

            const std::size_t xi = static_cast<std::size_t>(x);
            if (claimed[xi]) continue;

            const int bit = (sp.attr & OBJ_X_FLIP) != 0 ? px : 7 - px;
            const uint8_t colorIdx = tilePixel(tileAddr, static_cast<uint8_t>(row), bit);

            // Colour 0 is transparent for sprites — it is not a palette entry.
            if (colorIdx == 0) continue;

            claimed[xi] = true;

            // With the priority bit set the sprite hides behind BG colours 1-3,
            // but still shows over colour 0.
            if ((sp.attr & OBJ_BG_PRIORITY) != 0 && bgIndex[xi] != 0) continue;

            m_fb[rowStart + xi] = kShades[applyPalette(palette, colorIdx)];
        }
    }
}

// ── Flags ────────────────────────────────────────────────────────────────────

bool PPU::takeVBlankIrq() {
    const bool v = m_vblankIrq;
    m_vblankIrq = false;
    return v;
}

bool PPU::takeStatIrq() {
    const bool v = m_statIrq;
    m_statIrq = false;
    return v;
}

bool PPU::takeFrameReady() {
    const bool v = m_frameReady;
    m_frameReady = false;
    return v;
}
