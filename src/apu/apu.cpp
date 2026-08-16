#include "apu.h"

namespace {

constexpr uint16_t REG_NR50 = 0xFF24;
constexpr uint16_t REG_NR51 = 0xFF25;
constexpr uint16_t REG_NR52 = 0xFF26;
constexpr uint16_t WAVE_RAM_START = 0xFF30;

constexpr uint8_t NR52_POWER = 0x80;

// Four channels each swing +/-1, so a naive scale of 32767/4 leaves exactly
// zero headroom — and the high-pass filter's ripple then clips on every cycle.
// The extra divisor buys room for that ripple at the cost of ~2.5 dB.
constexpr float MIX_SCALE = 32767.0f / 5.5f;

// Hardware bleeds the DC offset away with an RC network, decaying by a factor
// of ~0.999958 per T-cycle. Raised to the number of cycles between output
// samples (4194304 / 48000), that becomes this per-sample factor.
constexpr float HPF_CHARGE = 0.996337f;

// A channel's DAC turns the digital 0-15 level into an analogue swing. A
// disabled DAC sits at zero rather than at the level the channel last held.
template <typename Channel>
float dac(const Channel& ch) {
    if (!ch.dacEnabled()) return 0.0f;
    return static_cast<float>(ch.output()) / 7.5f - 1.0f;
}

// Removes the running DC offset so channels sitting at a non-zero idle level
// do not add a constant bias — which would otherwise click on every trigger.
float highPass(float in, float& capacitor) {
    const float out = in - capacitor;
    capacitor = in - out * HPF_CHARGE;
    return out;
}

// The filter overshoots on sharp edges, so the mixed signal can exceed full
// scale even with per-channel headroom. Casting an out-of-range float to
// int16_t is undefined behaviour, so clamp rather than relying on the cast.
int16_t toSample(float v) {
    const float scaled = v * MIX_SCALE;
    if (scaled >= 32767.0f) return 32767;
    if (scaled <= -32768.0f) return -32768;
    return static_cast<int16_t>(scaled);
}

}  // namespace

void APU::pushSilence() {
    m_samples.push_back(0);
    m_samples.push_back(0);
}

void APU::tick(uint8_t tcycles) {
    if (!m_powered) {
        // Keep producing silence so the frontend's audio queue does not starve
        // while the APU is switched off.
        m_sampleAccum += static_cast<uint32_t>(tcycles) * SAMPLE_RATE;
        while (m_sampleAccum >= CPU_HZ) {
            m_sampleAccum -= CPU_HZ;
            pushSilence();
        }
        return;
    }

    if (const int step = m_sequencer.tick(tcycles); step >= 0) {
        if (FrameSequencer::clocksLength(step)) {
            m_ch1.tickLength();
            m_ch2.tickLength();
            m_ch3.tickLength();
            m_ch4.tickLength();
        }
        if (FrameSequencer::clocksSweep(step)) m_ch1.tickSweep();
        if (FrameSequencer::clocksEnvelope(step)) {
            m_ch1.tickEnvelope();
            m_ch2.tickEnvelope();
            m_ch4.tickEnvelope();  // channel 3 has no envelope
        }
    }

    m_ch1.tick(tcycles);
    m_ch2.tick(tcycles);
    m_ch3.tick(tcycles);
    m_ch4.tick(tcycles);

    // Resample from the 4.19 MHz master clock down to SAMPLE_RATE without
    // accumulating rounding error.
    m_sampleAccum += static_cast<uint32_t>(tcycles) * SAMPLE_RATE;
    while (m_sampleAccum >= CPU_HZ) {
        m_sampleAccum -= CPU_HZ;
        generateSample();
    }
}

void APU::generateSample() {
    const float c[4] = {dac(m_ch1), dac(m_ch2), dac(m_ch3), dac(m_ch4)};

    // NR51: bits 0-3 route channels 1-4 to the right, bits 4-7 to the left.
    float left  = 0.0f;
    float right = 0.0f;
    for (int i = 0; i < 4; ++i) {
        if ((m_nr51 & (0x10 << i)) != 0) left  += c[i];
        if ((m_nr51 & (0x01 << i)) != 0) right += c[i];
    }

    // NR50 holds a 0-7 volume per side, which scales as (n + 1) / 8.
    const float lVol = static_cast<float>(((m_nr50 >> 4) & 0x07) + 1) / 8.0f;
    const float rVol = static_cast<float>((m_nr50 & 0x07) + 1) / 8.0f;

    m_samples.push_back(toSample(highPass(left * lVol, m_capL)));
    m_samples.push_back(toSample(highPass(right * rVol, m_capR)));
}

uint8_t APU::readReg(uint16_t addr) const {
    if (addr >= WAVE_RAM_START)
        return m_ch3.readWaveRam(static_cast<int>(addr - WAVE_RAM_START));

    switch (addr) {
        case REG_NR50: return m_nr50;
        case REG_NR51: return m_nr51;
        case REG_NR52: {
            // Bits 0-3 are live channel status and cannot be written; 4-6 are
            // unused and read as ones.
            uint8_t v = m_powered ? NR52_POWER : uint8_t{0};
            v = static_cast<uint8_t>(v | 0x70);
            if (m_ch1.enabled()) v = static_cast<uint8_t>(v | 0x01);
            if (m_ch2.enabled()) v = static_cast<uint8_t>(v | 0x02);
            if (m_ch3.enabled()) v = static_cast<uint8_t>(v | 0x04);
            if (m_ch4.enabled()) v = static_cast<uint8_t>(v | 0x08);
            return v;
        }
        default: break;
    }

    if (addr >= 0xFF10 && addr <= 0xFF14) return m_ch1.readReg(addr - 0xFF10);
    if (addr >= 0xFF16 && addr <= 0xFF19) return m_ch2.readReg(addr - 0xFF15);
    if (addr >= 0xFF1A && addr <= 0xFF1E) return m_ch3.readReg(addr - 0xFF1A);
    if (addr >= 0xFF20 && addr <= 0xFF23) return m_ch4.readReg(addr - 0xFF20);
    return 0xFF;
}

void APU::writeReg(uint16_t addr, uint8_t val) {
    // Wave RAM stays accessible with the APU switched off.
    if (addr >= WAVE_RAM_START) {
        m_ch3.writeWaveRam(static_cast<int>(addr - WAVE_RAM_START), val);
        return;
    }

    if (addr == REG_NR52) {
        const bool on = (val & NR52_POWER) != 0;
        if (!on && m_powered) {
            // Powering down clears every register and silences everything.
            // Wave RAM survives — WaveChannel::powerOff leaves it alone.
            m_ch1.powerOff();
            m_ch2.powerOff();
            m_ch3.powerOff();
            m_ch4.powerOff();
            m_nr50 = 0;
            m_nr51 = 0;
        } else if (on && !m_powered) {
            m_sequencer.reset();
        }
        m_powered = on;
        return;
    }

    // While powered off the registers are read-only.
    if (!m_powered) return;

    switch (addr) {
        case REG_NR50: m_nr50 = val; return;
        case REG_NR51: m_nr51 = val; return;
        default: break;
    }

    if (addr >= 0xFF10 && addr <= 0xFF14)      m_ch1.writeReg(addr - 0xFF10, val);
    else if (addr >= 0xFF16 && addr <= 0xFF19) m_ch2.writeReg(addr - 0xFF15, val);
    else if (addr >= 0xFF1A && addr <= 0xFF1E) m_ch3.writeReg(addr - 0xFF1A, val);
    else if (addr >= 0xFF20 && addr <= 0xFF23) m_ch4.writeReg(addr - 0xFF20, val);
}
