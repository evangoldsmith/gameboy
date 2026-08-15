#ifndef TIMER_H
#define TIMER_H

#include <cstdint>

// DIV only. TIMA/TMA/TAC and the falling-edge machinery land in Phase 4 — this
// exists now because Blargg's tests read DIV for seeding, and because it is
// where the 16-bit system counter belongs.
class Timer {
public:
    void tick(uint8_t tcycles) {
        m_counter = static_cast<uint16_t>(m_counter + tcycles);
    }

    // $FF04 exposes the upper 8 bits of the counter, so it ticks at 16384 Hz.
    uint8_t div() const { return static_cast<uint8_t>(m_counter >> 8); }

    // Any write resets the whole 16-bit counter, not just the visible byte.
    void resetDiv() { m_counter = 0; }

private:
    uint16_t m_counter{};
};

#endif // TIMER_H
