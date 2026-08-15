#include "gameboy.h"

GameBoy::GameBoy(const std::string& romPath)
    : m_cart(Cartridge::load(romPath)),
      m_mmu(m_cart, m_serial, m_timer, m_ppu),
      m_cpu(m_mmu) {}

void GameBoy::requestInterrupt(Interrupt which) {
    const uint8_t bit = static_cast<uint8_t>(1u << static_cast<uint8_t>(which));
    m_mmu.write(0xFF0F, static_cast<uint8_t>(m_mmu.read(0xFF0F) | bit));
}

uint8_t GameBoy::step() {
    const uint8_t tcycles = m_cpu.step();

    m_timer.tick(tcycles);
    m_ppu.tick(tcycles);

    if (m_ppu.takeVBlankIrq()) requestInterrupt(Interrupt::VBlank);
    if (m_timer.takeIrq())     requestInterrupt(Interrupt::Timer);
    if (m_serial.takeIrq())    requestInterrupt(Interrupt::Serial);

    return tcycles;
}

void GameBoy::runFrame() {
    uint32_t elapsed = 0;
    while (elapsed < TCYCLES_PER_FRAME) {
        if (m_cpu.stopped()) return;
        elapsed += step();
    }
}
