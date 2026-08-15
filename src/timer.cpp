#include "timer.h"

namespace {
// TAC bits 1-0 select which bit of the 16-bit counter clocks TIMA. Watching a
// higher bit means a slower tick: bit 9 falls every 1024 T-cycles (4096 Hz),
// bit 3 every 16 (262144 Hz), bit 5 every 64 (65536 Hz), bit 7 every 256
// (16384 Hz).
constexpr uint8_t kClockBit[4] = {9, 3, 5, 7};

constexpr uint8_t TAC_ENABLE = 0x04;
}  // namespace

bool Timer::timerBit(uint16_t counter) const {
    if ((m_tac & TAC_ENABLE) == 0) return false;
    const uint8_t bit = kClockBit[m_tac & 0x03];
    return ((counter >> bit) & 1u) != 0;
}

void Timer::incTima() {
    if (m_tima == 0xFF) {
        // Overflow does not reload immediately. For one M-cycle TIMA reads $00
        // and no interrupt has fired yet; the reload and the IRQ land together
        // when the delay expires.
        m_tima        = 0x00;
        m_reloadDelay = 4;
    } else {
        m_tima = static_cast<uint8_t>(m_tima + 1);
    }
}

void Timer::setCounter(uint16_t next) {
    const bool before = timerBit(m_counter);
    m_counter = next;
    if (before && !timerBit(m_counter)) incTima();
}

void Timer::tick(uint8_t tcycles) {
    // Stepping one M-cycle at a time is enough to catch every edge: the fastest
    // watched bit (3) holds each state for 8 T-cycles, so a 4-cycle step can
    // never skip past a transition.
    for (uint8_t done = 0; done + 4 <= tcycles; done = static_cast<uint8_t>(done + 4)) {
        if (m_reloadDelay != 0) {
            m_reloadDelay = static_cast<uint8_t>(m_reloadDelay - 4);
            if (m_reloadDelay == 0) {
                m_tima = m_tma;
                m_irq  = true;
            }
        }
        setCounter(static_cast<uint16_t>(m_counter + 4));
    }
}

void Timer::resetDiv() {
    // Any write to DIV clears the whole 16-bit counter. If the watched bit was
    // set, clearing it looks exactly like a falling edge and ticks TIMA.
    setCounter(0);
}

void Timer::writeTima(uint8_t val) {
    // Writing during the reload delay cancels the overflow outright: no TMA
    // reload, no interrupt.
    m_reloadDelay = 0;
    m_tima        = val;
}

void Timer::writeTac(uint8_t val) {
    // Changing the clock select or clearing the enable bit can drop the watched
    // signal from 1 to 0, which the hardware counts as a real edge.
    const bool before = timerBit(m_counter);
    m_tac = static_cast<uint8_t>(val & 0x07);
    if (before && !timerBit(m_counter)) incTima();
}

bool Timer::takeIrq() {
    const bool v = m_irq;
    m_irq = false;
    return v;
}
