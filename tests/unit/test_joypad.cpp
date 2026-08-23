// $FF00 is entirely active low — 0 means selected or pressed — and getting that
// backwards once made every button read as permanently held, which deadlocked
// Tetris before it drew anything. These tests pin the polarity down.

#include "joypad.h"

#include <doctest/doctest.h>

namespace {
constexpr uint8_t SELECT_DIRS    = 0x10;  // clear this bit to select directions
constexpr uint8_t SELECT_ACTIONS = 0x20;
constexpr uint8_t LOW_NIBBLE     = 0x0F;
}  // namespace

TEST_CASE("unused bits 6-7 always read as ones") {
    Joypad j;
    CHECK((j.read() & 0xC0) == 0xC0);
}

TEST_CASE("nothing pressed reads as all ones") {
    Joypad j;
    j.write(static_cast<uint8_t>(~SELECT_DIRS));   // select the direction row
    CHECK((j.read() & LOW_NIBBLE) == LOW_NIBBLE);
}

TEST_CASE("a pressed button clears its bit in the selected row") {
    Joypad j;
    j.write(static_cast<uint8_t>(~SELECT_DIRS));

    j.setButton(Button::Right, true);
    CHECK((j.read() & 0x01) == 0);                 // pressed reads 0
    CHECK((j.read() & 0x0E) == 0x0E);              // the others stay high

    j.setButton(Button::Down, true);
    CHECK((j.read() & 0x08) == 0);
}

TEST_CASE("a row shows only its own buttons") {
    Joypad j;
    j.setButton(Button::A, true);                  // action row

    j.write(static_cast<uint8_t>(~SELECT_DIRS));   // directions selected
    CHECK((j.read() & LOW_NIBBLE) == LOW_NIBBLE);  // A is invisible here

    j.write(static_cast<uint8_t>(~SELECT_ACTIONS));
    CHECK((j.read() & 0x01) == 0);                 // now it shows
}

TEST_CASE("selecting both rows ANDs them, selecting neither reads all ones") {
    Joypad j;
    j.setButton(Button::Right, true);   // direction row, bit 0
    j.setButton(Button::B, true);       // action row, bit 1

    j.write(0x00);                      // both rows selected
    CHECK((j.read() & 0x03) == 0);      // both buttons visible at once

    j.write(SELECT_DIRS | SELECT_ACTIONS);          // neither selected
    CHECK((j.read() & LOW_NIBBLE) == LOW_NIBBLE);
}

TEST_CASE("the interrupt fires on press but not on release") {
    Joypad j;
    j.write(static_cast<uint8_t>(~SELECT_DIRS));
    REQUIRE(j.takeIrq() == false);

    j.setButton(Button::Up, true);
    CHECK(j.takeIrq() == true);
    CHECK(j.takeIrq() == false);        // read-and-clear

    j.setButton(Button::Up, false);     // releasing is a rising edge
    CHECK(j.takeIrq() == false);
}

TEST_CASE("selecting a row that reveals a held button also fires the interrupt") {
    Joypad j;
    j.write(static_cast<uint8_t>(~SELECT_DIRS));    // directions selected
    j.setButton(Button::Start, true);               // action row: not visible
    REQUIRE(j.takeIrq() == false);

    j.write(static_cast<uint8_t>(~SELECT_ACTIONS)); // now it comes into view
    CHECK(j.takeIrq() == true);
}

TEST_CASE("the row select bits read back, the button bits do not") {
    Joypad j;
    j.write(SELECT_DIRS);                           // actions selected
    CHECK((j.read() & 0x30) == SELECT_DIRS);
}
