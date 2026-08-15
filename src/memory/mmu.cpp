#include "mmu.h"

namespace {
// I/O register addresses handled by a subsystem rather than the flat array.
constexpr uint16_t REG_SB   = 0xFF01;  // Serial transfer data
constexpr uint16_t REG_SC   = 0xFF02;  // Serial transfer control
constexpr uint16_t REG_DIV  = 0xFF04;  // Divider
constexpr uint16_t REG_TIMA = 0xFF05;  // Timer counter
constexpr uint16_t REG_TMA  = 0xFF06;  // Timer modulo
constexpr uint16_t REG_TAC  = 0xFF07;  // Timer control
constexpr uint16_t REG_IF   = 0xFF0F;  // Interrupt flags
constexpr uint16_t REG_STAT = 0xFF41;  // LCD status
constexpr uint16_t REG_LY   = 0xFF44;  // LCD Y coordinate
}  // namespace

MMU::MMU(Cartridge& cart, Serial& serial, Timer& timer, PPU& ppu)
    : m_cart(cart), m_serial(serial), m_timer(timer), m_ppu(ppu) {}

uint8_t MMU::read(uint16_t addr) {
    // $0000–$7FFF: Cartridge ROM
    if (addr < 0x8000)
        return m_cart.read(addr);

    // $8000–$9FFF: VRAM
    if (addr < 0xA000)
        return m_vram[addr - 0x8000];

    // $A000–$BFFF: External RAM (cartridge)
    if (addr < 0xC000)
        return m_cart.read(addr);

    // $C000–$DFFF: WRAM
    if (addr < 0xE000)
        return m_wram[addr - 0xC000];

    // $E000–$FDFF: Echo RAM (mirror of $C000–$DDFF)
    if (addr < 0xFE00)
        return m_wram[addr - 0xE000];

    // $FE00–$FE9F: OAM
    if (addr < 0xFEA0)
        return m_oam[addr - 0xFE00];

    // $FEA0–$FEFF: Unusable
    if (addr < 0xFF00)
        return 0xFF;

    // $FF00–$FF7F: I/O registers
    if (addr < 0xFF80)
        return readIO(addr);

    // $FF80–$FFFE: HRAM
    if (addr < 0xFFFF)
        return m_hram[addr - 0xFF80];

    // $FFFF: IE register
    return m_ie;
}

void MMU::write(uint16_t addr, uint8_t val) {
    // $0000–$7FFF: Cartridge (MBC register writes)
    if (addr < 0x8000) {
        m_cart.write(addr, val);
        return;
    }

    // $8000–$9FFF: VRAM
    if (addr < 0xA000) {
        m_vram[addr - 0x8000] = val;
        return;
    }

    // $A000–$BFFF: External RAM (cartridge)
    if (addr < 0xC000) {
        m_cart.write(addr, val);
        return;
    }

    // $C000–$DFFF: WRAM
    if (addr < 0xE000) {
        m_wram[addr - 0xC000] = val;
        return;
    }

    // $E000–$FDFF: Echo RAM
    if (addr < 0xFE00) {
        m_wram[addr - 0xE000] = val;
        return;
    }

    // $FE00–$FE9F: OAM
    if (addr < 0xFEA0) {
        m_oam[addr - 0xFE00] = val;
        return;
    }

    // $FEA0–$FEFF: Unusable — ignore writes
    if (addr < 0xFF00)
        return;

    // $FF00–$FF7F: I/O registers
    if (addr < 0xFF80) {
        writeIO(addr, val);
        return;
    }

    // $FF80–$FFFE: HRAM
    if (addr < 0xFFFF) {
        m_hram[addr - 0xFF80] = val;
        return;
    }

    // $FFFF: IE register
    m_ie = val;
}

// ── I/O dispatch ─────────────────────────────────────────────────────────────
// Registers not listed here fall through to the flat array, which is fine for
// anything nothing reacts to yet.

uint8_t MMU::readIO(uint16_t addr) {
    switch (addr) {
        case REG_SB:   return m_serial.readSB();
        case REG_SC:   return m_serial.readSC();
        case REG_DIV:  return m_timer.div();
        case REG_TIMA: return m_timer.tima();
        case REG_TMA:  return m_timer.tma();
        case REG_TAC:  return m_timer.tac();
        case REG_IF:   return static_cast<uint8_t>(m_io[REG_IF - 0xFF00] | 0xE0);
        case REG_STAT: return m_ppu.stat();
        case REG_LY:   return m_ppu.ly();
        default:       return m_io[addr - 0xFF00];
    }
}

void MMU::writeIO(uint16_t addr, uint8_t val) {
    switch (addr) {
        case REG_SB:  m_serial.writeSB(val); break;
        case REG_SC:  m_serial.writeSC(val); break;
        case REG_DIV:  m_timer.resetDiv();     break;  // any write resets it
        case REG_TIMA: m_timer.writeTima(val); break;
        case REG_TMA:  m_timer.writeTma(val);  break;
        case REG_TAC:  m_timer.writeTac(val);  break;
        case REG_LY:   break;                          // read-only
        default:      m_io[addr - 0xFF00] = val; break;
    }
}
