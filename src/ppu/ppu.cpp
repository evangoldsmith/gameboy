#include "ppu.h"

namespace {

// LCDC ($FF40)
constexpr uint8_t LCDC_BG_ENABLE = 0x01;
constexpr uint8_t LCDC_BG_MAP    = 0x08;  // 0 = $9800, 1 = $9C00
constexpr uint8_t LCDC_TILE_DATA = 0x10;  // 0 = $8800 signed, 1 = $8000 unsigned
constexpr uint8_t LCDC_ENABLE    = 0x80;

// STAT ($FF41) interrupt-select bits
constexpr uint8_t STAT_LYC_IE    = 0x40;
constexpr uint8_t STAT_OAM_IE    = 0x20;
constexpr uint8_t STAT_VBLANK_IE = 0x10;
constexpr uint8_t STAT_HBLANK_IE = 0x08;
constexpr uint8_t STAT_LYC_FLAG  = 0x04;

// Mode 3 is 172–289 dots on hardware depending on sprites and SCX. Fixed here;
// variable length is Phase 12.
constexpr uint16_t OAM_SCAN_DOTS = 80;
constexpr uint16_t TRANSFER_DOTS = 172;

// The four DMG shades, lightest to darkest, as ARGB8888.
constexpr uint32_t kShades[4] = {0xFF9BBC0F, 0xFF8BAC0F, 0xFF306230, 0xFF0F380F};

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
        default:     return 0xFF;
    }
}

void PPU::writeReg(uint16_t addr, uint8_t val) {
    switch (addr) {
        case 0xFF40: {
            const bool wasOn = (m_lcdc & LCDC_ENABLE) != 0;
            m_lcdc = val;
            // Turning the LCD off resets the line counter. Games do this to get
            // unrestricted VRAM access.
            if (wasOn && (m_lcdc & LCDC_ENABLE) == 0) {
                m_ly   = 0;
                m_dot  = 0;
                m_mode = PPUMode::HBlank;
                m_lineRendered = false;
            }
            break;
        }
        case 0xFF41: m_stat = static_cast<uint8_t>(val & 0x78); break;  // only bits 3-6 writable
        case 0xFF42: m_scy = val; break;
        case 0xFF43: m_scx = val; break;
        case 0xFF44: break;  // LY is read-only
        case 0xFF45: m_lyc = val; break;
        case 0xFF47: m_bgp = val; break;
        default: break;
    }
}

// ── Timing ───────────────────────────────────────────────────────────────────

PPUMode PPU::computeMode() const {
    if (m_ly >= VBLANK_LINE)                       return PPUMode::VBlank;
    if (m_dot < OAM_SCAN_DOTS)                     return PPUMode::OAMScan;
    if (m_dot < OAM_SCAN_DOTS + TRANSFER_DOTS)     return PPUMode::PixelTransfer;
    return PPUMode::HBlank;
}

void PPU::step4() {
    m_dot = static_cast<uint16_t>(m_dot + 4);

    if (m_dot >= DOTS_PER_LINE) {
        m_dot = static_cast<uint16_t>(m_dot - DOTS_PER_LINE);
        m_ly  = static_cast<uint8_t>((m_ly + 1) % LINES_PER_FRAME);
        m_lineRendered = false;

        if (m_ly == VBLANK_LINE) {
            m_vblankIrq = true;
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
    if ((m_stat & STAT_LYC_IE) != 0 && lycEqual())                 line = true;
    if ((m_stat & STAT_OAM_IE) != 0 && m_mode == PPUMode::OAMScan) line = true;
    if ((m_stat & STAT_VBLANK_IE) != 0 && m_mode == PPUMode::VBlank) line = true;
    if ((m_stat & STAT_HBLANK_IE) != 0 && m_mode == PPUMode::HBlank) line = true;

    if (line && !m_statLine) m_statIrq = true;
    m_statLine = line;
}

// ── Rendering ────────────────────────────────────────────────────────────────

void PPU::renderScanline() {
    const std::size_t rowStart = static_cast<std::size_t>(m_ly) * WIDTH;

    if ((m_lcdc & LCDC_BG_ENABLE) == 0) {
        // BG disabled renders as colour 0, which is white before palette mapping.
        for (int x = 0; x < WIDTH; ++x) m_fb[rowStart + static_cast<std::size_t>(x)] = kShades[0];
        return;
    }

    // Scrolling wraps within the 256x256 background map.
    const uint8_t  bgY     = static_cast<uint8_t>(m_ly + m_scy);
    const uint16_t mapBase = (m_lcdc & LCDC_BG_MAP) != 0 ? 0x1C00 : 0x1800;
    const uint16_t rowOff  = static_cast<uint16_t>((bgY / 8) * 32);
    const uint16_t lineOff = static_cast<uint16_t>((bgY % 8) * 2);

    for (int x = 0; x < WIDTH; ++x) {
        const uint8_t bgX = static_cast<uint8_t>(m_scx + x);

        const uint8_t tileIdx = m_vram[mapBase + rowOff + (bgX / 8)];

        // LCDC bit 4 picks the addressing mode: $8000 with an unsigned index,
        // or $9000 with a signed one (VRAM offsets $0000 and $1000).
        uint16_t tileAddr;
        if ((m_lcdc & LCDC_TILE_DATA) != 0)
            tileAddr = static_cast<uint16_t>(tileIdx * 16);
        else
            tileAddr = static_cast<uint16_t>(0x1000 + static_cast<int8_t>(tileIdx) * 16);

        // Two bytes per row, 2 bits per pixel: low byte holds the LSBs of the
        // colour indices, high byte the MSBs. Bit 7 is the leftmost pixel.
        const uint8_t lo  = m_vram[tileAddr + lineOff];
        const uint8_t hi  = m_vram[tileAddr + lineOff + 1];
        const int     bit = 7 - (bgX % 8);

        const uint8_t colorIdx = static_cast<uint8_t>((((hi >> bit) & 1) << 1) |
                                                      ((lo >> bit) & 1));
        const uint8_t shade    = static_cast<uint8_t>((m_bgp >> (colorIdx * 2)) & 0x03);

        m_fb[rowStart + static_cast<std::size_t>(x)] = kShades[shade];
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
