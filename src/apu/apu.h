#ifndef APU_H
#define APU_H

#include "channels.h"

#include <cstdint>
#include <vector>

// Audio processing unit.
//
// Owns the sound registers ($FF10-$FF3F), drives the four channels from the
// frame sequencer, and resamples their combined output down to a normal audio
// rate. The frontend drains samples() once a frame; the core has no SDL
// dependency.
class APU {
public:
    static constexpr int SAMPLE_RATE = 48000;
    static constexpr int CHANNELS    = 2;  // interleaved stereo

    void tick(uint8_t tcycles);

    uint8_t readReg(uint16_t addr) const;
    void    writeReg(uint16_t addr, uint8_t val);

    // Interleaved signed 16-bit stereo, left first.
    const std::vector<int16_t>& samples() const { return m_samples; }
    void clearSamples() { m_samples.clear(); }

private:
    void generateSample();
    void pushSilence();

    static constexpr uint32_t CPU_HZ = 4194304;

    PulseChannel   m_ch1{true};   // with frequency sweep
    PulseChannel   m_ch2{false};
    WaveChannel    m_ch3;
    NoiseChannel   m_ch4;
    FrameSequencer m_sequencer;

    uint8_t m_nr50{};  // master volume + VIN routing
    uint8_t m_nr51{};  // per-channel panning
    bool    m_powered{};

    // Fixed-point accumulator: emits a sample once it reaches CPU_HZ.
    uint32_t m_sampleAccum{};

    // One-pole high-pass state, per side. Removes the DC offset a channel
    // contributes while its DAC is enabled but it is not playing.
    float m_capL{};
    float m_capR{};

    std::vector<int16_t> m_samples;
};

#endif // APU_H
