#include "apu.h"

namespace {

constexpr uint16_t REG_NR50 = 0xFF24;
constexpr uint16_t REG_NR51 = 0xFF25;
constexpr uint16_t REG_NR52 = 0xFF26;
constexpr uint16_t WAVE_RAM_START = 0xFF30;

constexpr uint8_t NR52_POWER = 0x80;

// Headroom: four channels at full scale would clip, and the master volume
// scaling on top of that would clip harder.
constexpr float MIX_SCALE = 32767.0f / 4.0f;

// A channel's DAC turns the digital 0-15 level into an analogue swing. A
// disabled DAC sits at zero rather than at the level the channel last held.
float dac(const PulseChannel& ch) {
    if (!ch.dacEnabled()) return 0.0f;
    return static_cast<float>(ch.output()) / 7.5f - 1.0f;
}

}  // namespace

void APU::tick(uint8_t tcycles) {
    if (!m_powered) {
        // Keep producing silence so the frontend's audio queue does not starve
        // while the APU is switched off.
        m_sampleAccum += static_cast<uint32_t>(tcycles) * SAMPLE_RATE;
        while (m_sampleAccum >= CPU_HZ) {
            m_sampleAccum -= CPU_HZ;
            m_samples.push_back(0);
            m_samples.push_back(0);
        }
        return;
    }

    if (const int step = m_sequencer.tick(tcycles); step >= 0) {
        if (FrameSequencer::clocksLength(step)) {
            m_ch1.tickLength();
            m_ch2.tickLength();
        }
        if (FrameSequencer::clocksSweep(step)) m_ch1.tickSweep();
        if (FrameSequencer::clocksEnvelope(step)) {
            m_ch1.tickEnvelope();
            m_ch2.tickEnvelope();
        }
    }

    m_ch1.tick(tcycles);
    m_ch2.tick(tcycles);

    // Resample from the 4.19 MHz master clock down to SAMPLE_RATE without
    // accumulating rounding error.
    m_sampleAccum += static_cast<uint32_t>(tcycles) * SAMPLE_RATE;
    while (m_sampleAccum >= CPU_HZ) {
        m_sampleAccum -= CPU_HZ;
        generateSample();
    }
}

void APU::generateSample() {
    const float c1 = dac(m_ch1);
    const float c2 = dac(m_ch2);

    // NR51: bits 0-3 route channels 1-4 to the right, bits 4-7 to the left.
    float left  = 0.0f;
    float right = 0.0f;
    if ((m_nr51 & 0x10) != 0) left  += c1;
    if ((m_nr51 & 0x20) != 0) left  += c2;
    if ((m_nr51 & 0x01) != 0) right += c1;
    if ((m_nr51 & 0x02) != 0) right += c2;

    // NR50 holds a 0-7 volume per side, which scales as (n + 1) / 8.
    const float lVol = static_cast<float>(((m_nr50 >> 4) & 0x07) + 1) / 8.0f;
    const float rVol = static_cast<float>((m_nr50 & 0x07) + 1) / 8.0f;

    m_samples.push_back(static_cast<int16_t>(left * lVol * MIX_SCALE));
    m_samples.push_back(static_cast<int16_t>(right * rVol * MIX_SCALE));
}

uint8_t APU::readReg(uint16_t addr) const {
    if (addr >= WAVE_RAM_START)
        return m_waveRam[addr - WAVE_RAM_START];

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
            return v;
        }
        default: break;
    }

    if (addr >= 0xFF10 && addr <= 0xFF14) return m_ch1.readReg(addr - 0xFF10);
    if (addr >= 0xFF16 && addr <= 0xFF19) return m_ch2.readReg(addr - 0xFF15);
    return 0xFF;
}

void APU::writeReg(uint16_t addr, uint8_t val) {
    // Wave RAM stays accessible with the APU switched off.
    if (addr >= WAVE_RAM_START) {
        m_waveRam[addr - WAVE_RAM_START] = val;
        return;
    }

    if (addr == REG_NR52) {
        const bool on = (val & NR52_POWER) != 0;
        if (!on && m_powered) {
            // Powering down clears every register and silences everything.
            m_ch1.powerOff();
            m_ch2.powerOff();
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

    if (addr >= 0xFF10 && addr <= 0xFF14) m_ch1.writeReg(addr - 0xFF10, val);
    else if (addr >= 0xFF16 && addr <= 0xFF19) m_ch2.writeReg(addr - 0xFF15, val);
}
