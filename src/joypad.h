#ifndef JOYPAD_H
#define JOYPAD_H

#include <cstdint>

enum class Button : uint8_t {
    Right, Left, Up, Down,   // direction row
    A, B, Select, Start      // action row
};

// The $FF00 button matrix.
//
// Everything here is active low: a 0 bit means selected or pressed. Getting
// that backwards makes every button read as permanently held, which is enough
// to deadlock a ROM before it draws anything.
class Joypad {
public:
    void setButton(Button b, bool pressed);

    uint8_t read() const;            // $FF00
    void    write(uint8_t val);      // only bits 4-5 are writable

    bool takeIrq();

private:
    // The four output lines for the currently selected row(s), active low.
    uint8_t lines() const;
    void    updateIrq();

    uint8_t m_select{0x30};   // bits 4-5; both rows deselected at reset
    uint8_t m_dirs{};         // bits 0-3: Right Left Up Down,  1 = pressed
    uint8_t m_actions{};      // bits 0-3: A B Select Start,    1 = pressed

    uint8_t m_lastLines{0x0F};
    bool    m_irq{};
};

#endif // JOYPAD_H
