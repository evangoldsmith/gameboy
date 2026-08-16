#include "channels.h"

namespace {

// Duty waveforms, one bit per step, read from bit 7 down. 12.5%, 25%, 50%, 75%.
constexpr uint8_t kDuty[4] = {0b00000001, 0b10000001, 0b10000111, 0b01111110};

constexpr uint8_t NRX4_TRIGGER    = 0x80;
constexpr uint8_t NRX4_LENGTH_EN  = 0x40;

}  // namespace

// ── FrameSequencer ───────────────────────────────────────────────────────────

int FrameSequencer::tick(uint8_t tcycles) {
    m_counter += tcycles;
    if (m_counter < PERIOD) return -1;

    m_counter -= PERIOD;
    m_step = (m_step + 1) % 8;
    return m_step;
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
    return high ? m_volume : uint8_t{0};
}

void PulseChannel::tickLength() {
    if ((m_nrx4 & NRX4_LENGTH_EN) == 0 || m_lengthCounter == 0) return;
    if (--m_lengthCounter == 0) m_enabled = false;
}

void PulseChannel::tickEnvelope() {
    const uint8_t period = static_cast<uint8_t>(m_nrx2 & 0x07);
    if (period == 0) return;  // period 0 disables the envelope entirely

    if (m_envelopeTimer > 0 && --m_envelopeTimer != 0) return;

    m_envelopeTimer = period;
    const bool up = (m_nrx2 & 0x08) != 0;
    if (up && m_volume < 15)       ++m_volume;
    else if (!up && m_volume > 0)  --m_volume;
}

// new = shadow +/- (shadow >> shift)
uint16_t PulseChannel::nextSweepFrequency() {
    const uint8_t shift = static_cast<uint8_t>(m_nrx0 & 0x07);
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

    m_volume        = static_cast<uint8_t>((m_nrx2 >> 4) & 0x0F);
    m_envelopeTimer = static_cast<uint8_t>(m_nrx2 & 0x07);

    if (m_hasSweep) {
        m_sweepShadow  = frequency();
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
        case 2:  return m_nrx2;
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
            m_nrx2 = val;
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
    m_nrx0 = m_nrx1 = m_nrx2 = m_nrx3 = m_nrx4 = 0;
    m_enabled = false;
    m_volume = 0;
    m_dutyStep = 0;
    m_periodTimer = 0;
    m_sweepEnabled = false;
    m_sweepNegated = false;
}
