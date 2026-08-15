#ifndef PPU_H
#define PPU_H

#include "../types.h"

#include <cstdint>

// Scanline timing only — no rendering yet (roadmap Phase 6 fills that in).
//
// This much is needed now because almost every ROM, including Blargg's CPU
// tests, spins waiting for LY to reach VBlank before it does anything. With LY
// stuck at zero the CPU never gets past the first wait loop.
class PPU {
public:
    static constexpr uint16_t DOTS_PER_LINE   = 456;
    static constexpr uint8_t  LINES_PER_FRAME = 154;
    static constexpr uint8_t  VBLANK_LINE     = 144;

    void tick(uint8_t tcycles);

    uint8_t ly() const;
    PPUMode mode() const { return m_mode; }
    uint8_t stat() const;

    bool takeVBlankIrq();

    // Game Boy Doctor compares traces against logs captured with LY hardwired
    // to $90, so its expected values only line up if we do the same.
    void setLyStub(bool on) { m_lyStub = on; }

private:
    uint16_t m_dot{};
    uint8_t  m_ly{};
    PPUMode  m_mode{PPUMode::OAMScan};
    bool     m_vblankIrq{};
    bool     m_lyStub{};
};

#endif // PPU_H
