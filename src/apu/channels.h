#ifndef CHANNELS_H
#define CHANNELS_H

#include <array>
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

// The NRx2 volume envelope, shared by the two pulse channels and the noise
// channel. Owns the register byte so reads return exactly what was written —
// the envelope's running volume never writes back into it.
class VolumeEnvelope {
public:
    void    write(uint8_t nrx2) { m_reg = nrx2; }
    uint8_t reg() const { return m_reg; }

    // The DAC is powered by the upper five bits; all zero means no output at
    // all, regardless of the running volume.
    bool dacEnabled() const { return (m_reg & 0xF8) != 0; }

    void trigger();
    void tick();

    uint8_t volume() const { return m_volume; }
    void    reset() { m_reg = 0; m_volume = 0; m_timer = 0; }

private:
    uint8_t m_reg{};
    uint8_t m_volume{};
    uint8_t m_timer{};
};

// Square wave with a duty cycle, length counter and volume envelope. Channel 1
// additionally has a frequency sweep; channel 2 is the same unit without it.
class PulseChannel {
public:
    explicit PulseChannel(bool hasSweep) : m_hasSweep(hasSweep) {}

    void tick(uint8_t tcycles);
    void tickLength();
    void tickEnvelope() { m_envelope.tick(); }
    void tickSweep();

    // Register index 0-4, corresponding to NRx0-NRx4.
    uint8_t readReg(int idx) const;
    void    writeReg(int idx, uint8_t val);

    // Loads the length counter without touching any other register state. On
    // DMG this stays reachable while the APU is powered off.
    void writeLengthLoad(uint8_t val);

    // Current digital level, 0-15. Meaningless while the DAC is off.
    uint8_t output() const;

    bool enabled() const { return m_enabled; }
    bool dacEnabled() const { return m_envelope.dacEnabled(); }

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
    uint8_t m_nrx3{};  // frequency low
    uint8_t m_nrx4{};  // frequency high + trigger + length enable

    VolumeEnvelope m_envelope;

    bool    m_enabled{};
    int32_t m_periodTimer{};  // T-cycles until the next duty step
    uint8_t m_dutyStep{};

    uint8_t m_lengthCounter{};

    uint16_t m_sweepShadow{};
    uint8_t  m_sweepTimer{};
    bool     m_sweepEnabled{};
    bool     m_sweepNegated{};  // a subtract has happened since the last trigger
};

// Plays 32 four-bit samples from its own wave RAM. No envelope — the volume is
// a coarse shift instead.
class WaveChannel {
public:
    void tick(uint8_t tcycles);
    void tickLength();

    // Register index 0-4, corresponding to NR30-NR34.
    uint8_t readReg(int idx) const;
    void    writeReg(int idx, uint8_t val);

    // See PulseChannel::writeLengthLoad. Channel 3's counter is 8-bit-loaded
    // but counts to 256.
    void writeLengthLoad(uint8_t val);

    uint8_t readWaveRam(int idx) const;
    void    writeWaveRam(int idx, uint8_t val);

    uint8_t output() const;

    bool enabled() const { return m_enabled; }
    bool dacEnabled() const { return (m_nr30 & 0x80) != 0; }

    void powerOff();

private:
    void     trigger();
    uint16_t frequency() const;
    void     reloadPeriod();
    void     loadSample();

    uint8_t m_nr30{};  // DAC enable
    uint8_t m_nr31{};  // length load
    uint8_t m_nr32{};  // output level
    uint8_t m_nr33{};  // frequency low
    uint8_t m_nr34{};  // frequency high + trigger + length enable

    std::array<uint8_t, 16> m_ram{};

    bool     m_enabled{};
    int32_t  m_periodTimer{};
    uint8_t  m_position{};       // 0-31, which nibble is playing
    uint8_t  m_sample{};         // the nibble currently held
    uint16_t m_lengthCounter{};  // up to 256, so wider than the others
};

// Pseudo-random noise from a linear feedback shift register, with the same
// length counter and volume envelope as the pulse channels.
class NoiseChannel {
public:
    void tick(uint8_t tcycles);
    void tickLength();
    void tickEnvelope() { m_envelope.tick(); }

    // Register index 0-3, corresponding to NR41-NR44.
    uint8_t readReg(int idx) const;
    void    writeReg(int idx, uint8_t val);

    // See PulseChannel::writeLengthLoad.
    void writeLengthLoad(uint8_t val);

    uint8_t output() const;

    bool enabled() const { return m_enabled; }
    bool dacEnabled() const { return m_envelope.dacEnabled(); }

    void powerOff();

private:
    void    trigger();
    int32_t period() const;

    uint8_t m_nr41{};  // length load
    uint8_t m_nr43{};  // clock shift + width + divisor
    uint8_t m_nr44{};  // trigger + length enable

    VolumeEnvelope m_envelope;

    bool     m_enabled{};
    int32_t  m_periodTimer{};
    uint16_t m_lfsr{0x7FFF};
    uint8_t  m_lengthCounter{};
};

#endif // CHANNELS_H
