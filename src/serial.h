#ifndef SERIAL_H
#define SERIAL_H

#include <cstdint>
#include <string>

// Link cable port. No device is connected, so transfers complete instantly and
// shift in $FF. The only thing this is used for right now is capturing test ROM
// output: Blargg's suites report pass/fail through $FF01/$FF02 long before they
// draw anything to the screen.
class Serial {
public:
    uint8_t readSB() const { return m_sb; }
    uint8_t readSC() const { return static_cast<uint8_t>(m_sc | 0x7E); }

    void writeSB(uint8_t val) { m_sb = val; }
    void writeSC(uint8_t val);

    // Everything the ROM has printed so far.
    const std::string& output() const { return m_output; }

    // Set true when a transfer completes; GameBoy turns this into an interrupt.
    bool takeIrq();

    // Mirror output to stdout as it arrives.
    void setEcho(bool on) { m_echo = on; }

private:
    uint8_t     m_sb{};
    uint8_t     m_sc{};
    std::string m_output;
    bool        m_irq{};
    bool        m_echo{true};
};

#endif // SERIAL_H
