#ifndef PPU_H
#define PPU_H

#include "../types.h"

#include <array>
#include <cstdint>

// Background rendering and scanline timing.
//
// Rendering is done a whole scanline at a time, at the moment the PPU enters
// HBlank. That handles the large majority of games; the pixel FIFO that mid-
// scanline raster effects need is roadmap Phase 12.
class PPU {
public:
    static constexpr int WIDTH  = 160;
    static constexpr int HEIGHT = 144;

    static constexpr uint16_t DOTS_PER_LINE   = 456;
    static constexpr uint8_t  LINES_PER_FRAME = 154;
    static constexpr uint8_t  VBLANK_LINE     = 144;

    using Framebuffer = std::array<uint32_t, static_cast<std::size_t>(WIDTH * HEIGHT)>;

    // Expects a multiple of 4 T-cycles, as CPU::step() returns.
    void tick(uint8_t tcycles);

    // $8000–$9FFF and $FE00–$FE9F, addressed absolutely.
    uint8_t readVram(uint16_t addr) const;
    void    writeVram(uint16_t addr, uint8_t val);
    uint8_t readOam(uint16_t addr) const;
    void    writeOam(uint16_t addr, uint8_t val);

    // $FF40–$FF45 and $FF47.
    uint8_t readReg(uint16_t addr) const;
    void    writeReg(uint16_t addr, uint8_t val);

    const Framebuffer& framebuffer() const { return m_fb; }

    bool takeVBlankIrq();
    bool takeStatIrq();
    bool takeFrameReady();

    // Game Boy Doctor's reference logs were captured with LY hardwired to $90,
    // so traces only line up against them with this on.
    void setLyStub(bool on) { m_lyStub = on; }

private:
    void step4();            // advance exactly 4 dots
    void renderScanline();
    void updateStatLine();

    PPUMode computeMode() const;
    bool    lycEqual() const { return m_ly == m_lyc; }
    uint8_t ly() const { return m_lyStub ? uint8_t{0x90} : m_ly; }

    std::array<uint8_t, 0x2000> m_vram{};
    std::array<uint8_t, 0x00A0> m_oam{};
    Framebuffer                 m_fb{};

    uint8_t m_lcdc{0x91};  // post-boot value: LCD on, BG on, $8000 tile data
    uint8_t m_stat{};
    uint8_t m_scy{};
    uint8_t m_scx{};
    uint8_t m_ly{};
    uint8_t m_lyc{};
    uint8_t m_bgp{0xFC};   // post-boot value

    uint16_t m_dot{};
    PPUMode  m_mode{PPUMode::OAMScan};
    bool     m_lineRendered{};
    bool     m_statLine{};   // previous state of the STAT interrupt line

    bool m_vblankIrq{};
    bool m_statIrq{};
    bool m_frameReady{};
    bool m_lyStub{};
};

#endif // PPU_H
