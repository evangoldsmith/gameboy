#ifndef MMU_H
#define MMU_H

#include "../cartridge/cartridge.h"
#include "../ppu/ppu.h"
#include "../serial.h"
#include "../timer.h"

#include <array>
#include <cstdint>

class MMU {
public:
    MMU(Cartridge& cart, Serial& serial, Timer& timer, PPU& ppu);

    // Not const: reads of I/O registers can observe live subsystem state and
    // will eventually mutate it (the DIV/TIMA edge cases in Phase 4).
    uint8_t read(uint16_t addr);
    void    write(uint16_t addr, uint8_t val);

private:
    uint8_t readIO(uint16_t addr);
    void    writeIO(uint16_t addr, uint8_t val);

    Cartridge& m_cart;
    Serial&    m_serial;
    Timer&     m_timer;
    PPU&       m_ppu;

    // VRAM and OAM belong to the PPU; the MMU only routes to them.
    std::array<uint8_t, 0x2000> m_wram{};  // $C000–$DFFF  8 KB
    std::array<uint8_t, 0x0080> m_io{};    // $FF00–$FF7F 128 B
    std::array<uint8_t, 0x007F> m_hram{};  // $FF80–$FFFE 127 B
    uint8_t                     m_ie{};    // $FFFF
};

#endif // MMU_H
