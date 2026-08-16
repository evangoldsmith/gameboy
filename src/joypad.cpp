#include "joypad.h"

namespace {
constexpr uint8_t SELECT_DIRS    = 0x10;  // bit 4, active low
constexpr uint8_t SELECT_ACTIONS = 0x20;  // bit 5, active low
}  // namespace

void Joypad::setButton(Button b, bool pressed) {
    const uint8_t idx  = static_cast<uint8_t>(b);
    const uint8_t mask = static_cast<uint8_t>(1u << (idx & 0x03));

    uint8_t& row = idx < 4 ? m_dirs : m_actions;
    row = static_cast<uint8_t>(pressed ? (row | mask)
                                       : (row & static_cast<uint8_t>(~mask)));

    updateIrq();
}

uint8_t Joypad::lines() const {
    // Start with everything released and clear a bit for each pressed button in
    // a selected row. Selecting both rows ANDs them together, which falls out
    // of clearing bits twice; selecting neither leaves all four high.
    uint8_t out = 0x0F;
    if ((m_select & SELECT_DIRS) == 0)
        out = static_cast<uint8_t>(out & ~m_dirs);
    if ((m_select & SELECT_ACTIONS) == 0)
        out = static_cast<uint8_t>(out & ~m_actions);
    return static_cast<uint8_t>(out & 0x0F);
}

uint8_t Joypad::read() const {
    // Bits 6-7 do not exist and always read as 1.
    return static_cast<uint8_t>(0xC0 | (m_select & 0x30) | lines());
}

void Joypad::write(uint8_t val) {
    m_select = static_cast<uint8_t>(val & 0x30);
    updateIrq();  // changing the selected row can pull a line low
}

// The interrupt fires on a high-to-low transition of any output line, so it
// triggers on press but not release — and also when a row select brings an
// already-held button into view.
void Joypad::updateIrq() {
    const uint8_t now = lines();
    if ((m_lastLines & ~now & 0x0F) != 0) m_irq = true;
    m_lastLines = now;
}

bool Joypad::takeIrq() {
    const bool v = m_irq;
    m_irq = false;
    return v;
}
