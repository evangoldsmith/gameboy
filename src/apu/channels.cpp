#include "channels.h"

namespace {

// Duty waveforms, one bit per step, read from bit 7 down. 12.5%, 25%, 50%, 75%.
constexpr uint8_t kDuty[4] = {0b00000001, 0b10000001, 0b10000111, 0b01111110};

// NR32 output level: mute, 100%, 50%, 25% — applied as a right shift.
constexpr uint8_t kWaveShift[4] = {4, 0, 1, 2};

// NR43 divisor code. Code 0 is a half-step rather than 0, hence the table.
constexpr int32_t kNoiseDivisor[8] = {8, 16, 32, 48, 64, 80, 96, 112};

constexpr uint8_t NRX4_TRIGGER   = 0x80;
constexpr uint8_t NRX4_LENGTH_EN = 0x40;

}  // namespace

// ── FrameSequencer ───────────────────────────────────────────────────────────

int FrameSequencer::tick(uint8_t tcycles) {
    m_counter += tcycles;
    if (m_counter < PERIOD) return -1;

    m_counter -= PERIOD;
    m_step = (m_step + 1) % 8;
    return m_step;
}

// ── VolumeEnvelope ───────────────────────────────────────────────────────────

void VolumeEnvelope::trigger() {
    m_volume = static_cast<uint8_t>((m_reg >> 4) & 0x0F);
    m_timer  = static_cast<uint8_t>(m_reg & 0x07);
}

void VolumeEnvelope::tick() {
    const uint8_t period = static_cast<uint8_t>(m_reg & 0x07);
    if (period == 0) return;  // period 0 disables the envelope entirely

    if (m_timer > 0 && --m_timer != 0) return;

    m_timer = period;
    const bool up = (m_reg & 0x08) != 0;
    if (up && m_volume < 15)      ++m_volume;
    else if (!up && m_volume > 0) --m_volume;
}

// ── PulseChannel ─────────────────────────────────────────────────────────────

uint16_t PulseChannel::frequency() const {
    return static_cast<uint16_t>(((m_nrx4 & 0x07) << 8) | m_nrx3);
}

void PulseChannel::setFrequency(uint16_t f) {
    m_nrx3 = static_cast<uint8_t>(f & 0xFF);
    m_nrx4 = static_cast<uint8_t>((m_nrx4 & 0xF8) | ((f >> 8) & 0x07));
}

// A period value of N means the duty step advances every (2048 - N) * 4
// T-cycles, so larger N is a higher pitch.
void PulseChannel::reloadPeriod() {
    m_periodTimer = (2048 - static_cast<int32_t>(frequency())) * 4;
}

void PulseChannel::tick(uint8_t tcycles) {
    m_periodTimer -= tcycles;
    while (m_periodTimer <= 0) {
        const int32_t overshoot = m_periodTimer;
        reloadPeriod();
        m_periodTimer += overshoot;
        m_dutyStep = static_cast<uint8_t>((m_dutyStep + 1) & 0x07);
    }
}

uint8_t PulseChannel::output() const {
    if (!m_enabled) return 0;
    const uint8_t pattern = kDuty[(m_nrx1 >> 6) & 0x03];
    const bool    high    = ((pattern >> (7 - m_dutyStep)) & 1) != 0;
    return high ? m_envelope.volume() : uint8_t{0};
}

void PulseChannel::tickLength() {
    if ((m_nrx4 & NRX4_LENGTH_EN) == 0 || m_lengthCounter == 0) return;
    if (--m_lengthCounter == 0) m_enabled = false;
}

// new = shadow +/- (shadow >> shift)
uint16_t PulseChannel::nextSweepFrequency() {
    const uint8_t  shift = static_cast<uint8_t>(m_nrx0 & 0x07);
    const uint16_t delta = static_cast<uint16_t>(m_sweepShadow >> shift);

    if ((m_nrx0 & 0x08) != 0) {
        m_sweepNegated = true;
        return static_cast<uint16_t>(m_sweepShadow - delta);
    }
    return static_cast<uint16_t>(m_sweepShadow + delta);
}

void PulseChannel::tickSweep() {
    if (!m_hasSweep) return;
    if (m_sweepTimer > 0 && --m_sweepTimer != 0) return;

    const uint8_t period = static_cast<uint8_t>((m_nrx0 >> 4) & 0x07);
    // A period of 0 still reloads as 8, it just never produces a sweep.
    m_sweepTimer = period != 0 ? period : uint8_t{8};
    if (!m_sweepEnabled || period == 0) return;

    const uint16_t next = nextSweepFrequency();
    if (next > 2047) {
        m_enabled = false;
        return;
    }

    if ((m_nrx0 & 0x07) != 0) {
        m_sweepShadow = next;
        setFrequency(next);
        // A second calculation runs purely as an overflow check; its result is
        // discarded, but it can still disable the channel.
        if (nextSweepFrequency() > 2047) m_enabled = false;
    }
}

void PulseChannel::trigger() {
    m_enabled = dacEnabled();

    if (m_lengthCounter == 0) m_lengthCounter = 64;

    reloadPeriod();
    m_envelope.trigger();

    if (m_hasSweep) {
        m_sweepShadow = frequency();
        const uint8_t period = static_cast<uint8_t>((m_nrx0 >> 4) & 0x07);
        const uint8_t shift  = static_cast<uint8_t>(m_nrx0 & 0x07);
        m_sweepTimer   = period != 0 ? period : uint8_t{8};
        m_sweepEnabled = period != 0 || shift != 0;
        m_sweepNegated = false;
        // Triggering with a shift set performs an immediate overflow check.
        if (shift != 0 && nextSweepFrequency() > 2047) m_enabled = false;
    }
}

uint8_t PulseChannel::readReg(int idx) const {
    // Write-only bits read back as ones.
    switch (idx) {
        case 0:  return static_cast<uint8_t>(m_nrx0 | 0x80);
        case 1:  return static_cast<uint8_t>(m_nrx1 | 0x3F);
        case 2:  return m_envelope.reg();
        case 3:  return 0xFF;
        default: return static_cast<uint8_t>(m_nrx4 | 0xBF);
    }
}

void PulseChannel::writeReg(int idx, uint8_t val) {
    switch (idx) {
        case 0:
            m_nrx0 = val;
            // Clearing the negate bit after a subtract has already happened
            // disables the channel immediately.
            if (m_sweepNegated && (val & 0x08) == 0) m_enabled = false;
            break;
        case 1:
            m_nrx1 = val;
            m_lengthCounter = static_cast<uint8_t>(64 - (val & 0x3F));
            break;
        case 2:
            m_envelope.write(val);
            // Turning the DAC off kills the channel; turning it on does not
            // start it, that needs a trigger.
            if (!dacEnabled()) m_enabled = false;
            break;
        case 3:
            m_nrx3 = val;
            break;
        default:
            m_nrx4 = val;
            if ((val & NRX4_TRIGGER) != 0) trigger();
            break;
    }
}

void PulseChannel::powerOff() {
    m_nrx0 = m_nrx1 = m_nrx3 = m_nrx4 = 0;
    m_envelope.reset();
    m_enabled      = false;
    m_dutyStep     = 0;
    m_periodTimer  = 0;
    m_sweepEnabled = false;
    m_sweepNegated = false;
}

// ── WaveChannel ──────────────────────────────────────────────────────────────

uint16_t WaveChannel::frequency() const {
    return static_cast<uint16_t>(((m_nr34 & 0x07) << 8) | m_nr33);
}

// Twice the rate of a pulse channel: 32 samples per cycle rather than 8 duty
// steps, so a full waveform repeats at 65536 / (2048 - N) Hz.
void WaveChannel::reloadPeriod() {
    m_periodTimer = (2048 - static_cast<int32_t>(frequency())) * 2;
}

void WaveChannel::loadSample() {
    const uint8_t byte = m_ram[m_position / 2];
    // Upper nibble first.
    m_sample = static_cast<uint8_t>((m_position % 2) == 0 ? (byte >> 4)
                                                          : (byte & 0x0F));
}

void WaveChannel::tick(uint8_t tcycles) {
    if (!m_enabled) return;

    m_periodTimer -= tcycles;
    while (m_periodTimer <= 0) {
        const int32_t overshoot = m_periodTimer;
        reloadPeriod();
        m_periodTimer += overshoot;
        m_position = static_cast<uint8_t>((m_position + 1) & 0x1F);
        loadSample();
    }
}

uint8_t WaveChannel::output() const {
    if (!m_enabled) return 0;
    return static_cast<uint8_t>(m_sample >> kWaveShift[(m_nr32 >> 5) & 0x03]);
}

void WaveChannel::tickLength() {
    if ((m_nr34 & NRX4_LENGTH_EN) == 0 || m_lengthCounter == 0) return;
    if (--m_lengthCounter == 0) m_enabled = false;
}

void WaveChannel::trigger() {
    m_enabled = dacEnabled();
    if (m_lengthCounter == 0) m_lengthCounter = 256;
    reloadPeriod();
    // Position resets to 0, but the first tick advances before reading — so the
    // first sample actually played is index 1, not 0.
    m_position = 0;
    m_sample   = 0;
}

uint8_t WaveChannel::readReg(int idx) const {
    switch (idx) {
        case 0:  return static_cast<uint8_t>(m_nr30 | 0x7F);
        case 1:  return 0xFF;  // length is write-only
        case 2:  return static_cast<uint8_t>(m_nr32 | 0x9F);
        case 3:  return 0xFF;
        default: return static_cast<uint8_t>(m_nr34 | 0xBF);
    }
}

void WaveChannel::writeReg(int idx, uint8_t val) {
    switch (idx) {
        case 0:
            m_nr30 = val;
            if (!dacEnabled()) m_enabled = false;
            break;
        case 1:
            m_nr31 = val;
            m_lengthCounter = static_cast<uint16_t>(256 - val);
            break;
        case 2: m_nr32 = val; break;
        case 3: m_nr33 = val; break;
        default:
            m_nr34 = val;
            if ((val & NRX4_TRIGGER) != 0) trigger();
            break;
    }
}

uint8_t WaveChannel::readWaveRam(int idx) const {
    // On DMG, reading wave RAM while the channel is running returns whichever
    // byte the wave pointer is currently on, not the one addressed.
    if (m_enabled) return m_ram[m_position / 2];
    return m_ram[static_cast<std::size_t>(idx)];
}

void WaveChannel::writeWaveRam(int idx, uint8_t val) {
    if (m_enabled) {
        m_ram[m_position / 2] = val;
        return;
    }
    m_ram[static_cast<std::size_t>(idx)] = val;
}

void WaveChannel::powerOff() {
    // Wave RAM deliberately survives: powering the APU down clears the
    // registers but leaves the sample data intact.
    m_nr30 = m_nr31 = m_nr32 = m_nr33 = m_nr34 = 0;
    m_enabled     = false;
    m_position    = 0;
    m_sample      = 0;
    m_periodTimer = 0;
}

// ── NoiseChannel ─────────────────────────────────────────────────────────────

int32_t NoiseChannel::period() const {
    const int32_t divisor = kNoiseDivisor[m_nr43 & 0x07];
    const int32_t shift   = (m_nr43 >> 4) & 0x0F;
    return divisor << shift;
}

void NoiseChannel::tick(uint8_t tcycles) {
    if (!m_enabled) return;

    m_periodTimer -= tcycles;
    while (m_periodTimer <= 0) {
        const int32_t overshoot = m_periodTimer;
        m_periodTimer = period();
        m_periodTimer += overshoot;

        // XOR the bottom two bits and feed the result back into bit 14.
        const uint16_t bit =
            static_cast<uint16_t>((m_lfsr & 1) ^ ((m_lfsr >> 1) & 1));
        m_lfsr = static_cast<uint16_t>(m_lfsr >> 1);
        m_lfsr = static_cast<uint16_t>((m_lfsr & ~(1u << 14)) |
                                       (static_cast<unsigned>(bit) << 14));

        // Width bit also feeds bit 6, shortening the period to 127 steps and
        // giving the metallic, obviously-periodic tone.
        if ((m_nr43 & 0x08) != 0)
            m_lfsr = static_cast<uint16_t>((m_lfsr & ~(1u << 6)) |
                                           (static_cast<unsigned>(bit) << 6));
    }
}

uint8_t NoiseChannel::output() const {
    if (!m_enabled) return 0;
    // Bit 0 inverted: the register is mostly ones, so this idles quiet.
    return (m_lfsr & 1) == 0 ? m_envelope.volume() : uint8_t{0};
}

void NoiseChannel::tickLength() {
    if ((m_nr44 & NRX4_LENGTH_EN) == 0 || m_lengthCounter == 0) return;
    if (--m_lengthCounter == 0) m_enabled = false;
}

void NoiseChannel::trigger() {
    m_enabled = dacEnabled();
    if (m_lengthCounter == 0) m_lengthCounter = 64;
    m_periodTimer = period();
    m_envelope.trigger();
    m_lfsr = 0x7FFF;  // all ones
}

uint8_t NoiseChannel::readReg(int idx) const {
    switch (idx) {
        case 0:  return 0xFF;  // length is write-only
        case 1:  return m_envelope.reg();
        case 2:  return m_nr43;
        default: return static_cast<uint8_t>(m_nr44 | 0xBF);
    }
}

void NoiseChannel::writeReg(int idx, uint8_t val) {
    switch (idx) {
        case 0:
            m_nr41 = val;
            m_lengthCounter = static_cast<uint8_t>(64 - (val & 0x3F));
            break;
        case 1:
            m_envelope.write(val);
            if (!dacEnabled()) m_enabled = false;
            break;
        case 2: m_nr43 = val; break;
        default:
            m_nr44 = val;
            if ((val & NRX4_TRIGGER) != 0) trigger();
            break;
    }
}

void NoiseChannel::powerOff() {
    m_nr41 = m_nr43 = m_nr44 = 0;
    m_envelope.reset();
    m_enabled     = false;
    m_periodTimer = 0;
    m_lfsr        = 0x7FFF;
}
