#ifndef TIMER_H
#define TIMER_H

#include <cstdint>

// DIV and the TIMA counter chain.
//
// Everything is driven by one 16-bit counter running at the full 4.194 MHz
// system clock. DIV exposes its top byte; TIMA is clocked by watching a single
// bit of it fall from 1 to 0. That falling-edge design is what produces the
// timer's well-known quirks — see the notes on resetDiv() and writeTac().
class Timer {
public:
    // Advances the counter. Expects a multiple of 4 T-cycles, which is what
    // CPU::step() always returns.
    void tick(uint8_t tcycles);

    uint8_t div() const { return static_cast<uint8_t>(m_counter >> 8); }  // $FF04

    // DIV bit 4, i.e. bit 12 of the internal counter. The APU's frame sequencer
    // steps on this signal's falling edge, which is why writing to DIV clocks
    // the sequencer as a side effect.
    bool apuDivBit() const { return ((m_counter >> 12) & 1u) != 0; }

    uint8_t tima() const { return m_tima; }                              // $FF05
    uint8_t tma() const { return m_tma; }                                // $FF06
    uint8_t tac() const { return static_cast<uint8_t>(m_tac | 0xF8); }   // $FF07

    void resetDiv();
    void writeTima(uint8_t val);
    void writeTma(uint8_t val) { m_tma = val; }
    void writeTac(uint8_t val);

    bool takeIrq();

private:
    // True when the counter bit selected by TAC is set and the timer is
    // enabled. TIMA increments when this goes from true to false.
    bool timerBit(uint16_t counter) const;

    // Assigns the counter and increments TIMA on a falling edge.
    void setCounter(uint16_t next);

    void incTima();

    uint16_t m_counter{};
    uint8_t  m_tima{};
    uint8_t  m_tma{};
    uint8_t  m_tac{};

    // T-cycles left before an overflowed TIMA is reloaded from TMA. Non-zero
    // means an overflow is in flight and TIMA currently reads $00.
    uint8_t m_reloadDelay{};

    bool m_irq{};
};

#endif // TIMER_H
