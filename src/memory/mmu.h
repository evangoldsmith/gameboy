#ifndef MMU_H
#define MMU_H

#include "../cartridge/cartridge.h"
#include <array>
#include <cstdint>

class MMU {
public:
    explicit MMU(Cartridge& cart);

    uint8_t read(uint16_t addr) const;
    void    write(uint16_t addr, uint8_t val);

private:
    Cartridge& m_cart;

    std::array<uint8_t, 0x2000> m_vram{};  // $8000–$9FFF  8 KB
    std::array<uint8_t, 0x2000> m_wram{};  // $C000–$DFFF  8 KB
    std::array<uint8_t, 0x00A0> m_oam{};   // $FE00–$FE9F 160 B
    std::array<uint8_t, 0x0080> m_io{};    // $FF00–$FF7F 128 B
    std::array<uint8_t, 0x007F> m_hram{};  // $FF80–$FFFE 127 B
    uint8_t                     m_ie{};    // $FFFF
};

#endif // MMU_H
