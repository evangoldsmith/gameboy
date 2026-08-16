#include "serial.h"

#include <cstdio>

void Serial::writeSC(uint8_t val) {
    m_sc = val;

    // Bit 7 starts a transfer, bit 0 selects the internal clock. Only an
    // internally-clocked transfer can finish with no cable attached: the
    // console drives the clock itself and shifts in $FF. An external-clock
    // transfer waits for a partner to supply the clock, so with nothing
    // connected it stays pending forever and never raises an interrupt.
    //
    // Completing both kinds looks like a phantom partner replying, which is
    // enough to strand a game in link negotiation.
    if ((val & 0x81) == 0x81) {
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
