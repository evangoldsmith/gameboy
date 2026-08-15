#include "ppu.h"

void PPU::tick(uint8_t tcycles) {
    m_dot = static_cast<uint16_t>(m_dot + tcycles);

    while (m_dot >= DOTS_PER_LINE) {
        m_dot = static_cast<uint16_t>(m_dot - DOTS_PER_LINE);
        m_ly  = static_cast<uint8_t>((m_ly + 1) % LINES_PER_FRAME);

        if (m_ly == VBLANK_LINE) m_vblankIrq = true;
    }

    // Approximate mode timing. Mode 3 is a fixed 172 dots here; it is actually
    // 172–289 depending on sprites and scroll, which Phase 6 handles.
    if (m_ly >= VBLANK_LINE)
        m_mode = PPUMode::VBlank;
    else if (m_dot < 80)
        m_mode = PPUMode::OAMScan;
    else if (m_dot < 80 + 172)
        m_mode = PPUMode::PixelTransfer;
    else
        m_mode = PPUMode::HBlank;
}

uint8_t PPU::ly() const { return m_lyStub ? uint8_t{0x90} : m_ly; }

uint8_t PPU::stat() const {
    return static_cast<uint8_t>(0x80 | static_cast<uint8_t>(m_mode));
}

bool PPU::takeVBlankIrq() {
    const bool v = m_vblankIrq;
    m_vblankIrq = false;
    return v;
}
