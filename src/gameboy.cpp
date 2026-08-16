#include "gameboy.h"

GameBoy::GameBoy(const std::string& romPath)
    : m_cart(Cartridge::load(romPath)),
      m_mmu(m_cart, m_serial, m_timer, m_ppu, m_joypad, m_apu),
      m_cpu(m_mmu) {}

// Peripherals are advanced from inside CPU::step(), one M-cycle at a time, via
// MMU::tick(). See docs/cpu.md.
uint8_t GameBoy::step() { return m_cpu.step(); }

void GameBoy::runFrame() {
    uint32_t elapsed = 0;
    while (elapsed < TCYCLES_PER_FRAME) {
        if (m_cpu.stopped()) return;
        elapsed += step();
    }
}
