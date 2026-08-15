#include "serial.h"

#include <cstdio>

void Serial::writeSC(uint8_t val) {
    m_sc = val;

    // Bit 7 = start transfer, bit 0 = use internal clock. With no cable
    // attached the byte goes nowhere, so we complete it immediately.
    if ((val & 0x80) != 0) {
        m_output.push_back(static_cast<char>(m_sb));
        if (m_echo) {
            std::fputc(static_cast<int>(m_sb), stdout);
            std::fflush(stdout);
        }
        m_sb = 0xFF;                                  // shifted in from an absent device
        m_sc = static_cast<uint8_t>(m_sc & 0x7F);     // transfer done
        m_irq = true;
    }
}

bool Serial::takeIrq() {
    const bool v = m_irq;
    m_irq = false;
    return v;
}
