#ifndef CHANNELS_H
#define CHANNELS_H

#include <cstdint>

// Sound channel primitives, deliberately kept free of any Game Boy register
// plumbing: each takes writes through a small index-based interface and knows
// nothing about where in the address space it lives.
//
// The GBA's four legacy PSG channels are this same hardware at different
// addresses, so keeping the logic standalone is what makes it reusable — see
// docs/apu.md.

// Drives the length, envelope and sweep units at 256, 64 and 128 Hz from a
// single 512 Hz clock.
class FrameSequencer {
public:
    // Advances the 8-step cycle. Returns the step that just began, or -1 if the
    // 512 Hz boundary was not crossed.
    int tick(uint8_t tcycles);

    void reset() { m_counter = 0; m_step = 0; }
    int  step() const { return m_step; }

    static bool clocksLength(int step)   { return (step % 2) == 0; }
    static bool clocksSweep(int step)    { return step == 2 || step == 6; }
    static bool clocksEnvelope(int step) { return step == 7; }

private:
    // 4194304 / 512
    static constexpr uint32_t PERIOD = 8192;

    uint32_t m_counter{};
    int      m_step{};
};

// Square wave with a duty cycle, length counter and volume envelope. Channel 1
// additionally has a frequency sweep; channel 2 is the same unit without it.
class PulseChannel {
public:
    explicit PulseChannel(bool hasSweep) : m_hasSweep(hasSweep) {}

    void tick(uint8_t tcycles);
    void tickLength();
    void tickEnvelope();
    void tickSweep();

    // Register index 0-4, corresponding to NRx0-NRx4.
    uint8_t readReg(int idx) const;
    void    writeReg(int idx, uint8_t val);

    // Current digital level, 0-15. Meaningless while the DAC is off.
    uint8_t output() const;

    bool enabled() const { return m_enabled; }
    bool dacEnabled() const { return (m_nrx2 & 0xF8) != 0; }

    void powerOff();

private:
    void     trigger();
    uint16_t frequency() const;
    void     setFrequency(uint16_t f);
    uint16_t nextSweepFrequency();
    void     reloadPeriod();

    const bool m_hasSweep;

    uint8_t m_nrx0{};  // sweep    (channel 1 only)
    uint8_t m_nrx1{};  // duty + length load
    uint8_t m_nrx2{};  // envelope
    uint8_t m_nrx3{};  // frequency low
    uint8_t m_nrx4{};  // frequency high + trigger + length enable

    bool     m_enabled{};
    int32_t  m_periodTimer{};  // T-cycles until the next duty step
    uint8_t  m_dutyStep{};

    uint8_t m_lengthCounter{};

    uint8_t m_volume{};
    uint8_t m_envelopeTimer{};

    uint16_t m_sweepShadow{};
    uint8_t  m_sweepTimer{};
    bool     m_sweepEnabled{};
    bool     m_sweepNegated{};  // a subtract has happened since the last trigger
};

#endif // CHANNELS_H
