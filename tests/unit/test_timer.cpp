// The timer is built entirely out of falling-edge detection on one shared
// counter, which is why several unrelated-looking actions tick TIMA. These
// tests pin down the ones that are easy to break by accident.

#include "timer.h"

#include <doctest/doctest.h>

namespace {

// Advances the timer in the 4-cycle steps CPU::step() always produces.
void advance(Timer& t, int tcycles) {
    for (int i = 0; i < tcycles; i += 4) t.tick(4);
}

constexpr uint8_t TAC_ENABLE = 0x04;

}  // namespace

TEST_CASE("DIV exposes the top byte of the system counter") {
    Timer t;
    advance(t, 252);
    CHECK(t.div() == 0);       // still short of a full 256
    advance(t, 4);
    CHECK(t.div() == 1);       // 16384 Hz, one tick per 256 T-cycles
    advance(t, 256 * 5);
    CHECK(t.div() == 6);
}

TEST_CASE("writing DIV clears the whole 16-bit counter, not just the visible byte") {
    Timer t;
    advance(t, 300);
    REQUIRE(t.div() == 1);

    t.resetDiv();
    CHECK(t.div() == 0);

    // If only the visible byte had been cleared, the hidden low byte would
    // still be at 44 and DIV would tick again after 212 cycles rather than 256.
    advance(t, 252);
    CHECK(t.div() == 0);
    advance(t, 4);
    CHECK(t.div() == 1);
}

TEST_CASE("TIMA counts at the rate TAC selects") {
    struct Rate { uint8_t tac; int period; };
    // TAC bits 1-0 pick which counter bit is watched: 9, 3, 5, 7.
    const Rate rates[] = {{0x00, 1024}, {0x01, 16}, {0x02, 64}, {0x03, 256}};

    for (const Rate& r : rates) {
        CAPTURE(r.tac);
        Timer t;
        t.writeTac(static_cast<uint8_t>(TAC_ENABLE | r.tac));
        t.resetDiv();

        advance(t, r.period - 4);
        CHECK(t.tima() == 0);
        advance(t, 4);
        CHECK(t.tima() == 1);
    }
}

TEST_CASE("a disabled timer never counts") {
    Timer t;
    t.writeTac(0x01);          // rate selected but enable bit clear
    t.resetDiv();
    advance(t, 4096);
    CHECK(t.tima() == 0);
}

TEST_CASE("writing DIV can tick TIMA") {
    // The watched bit is part of the counter DIV clears. If it happens to be
    // set, clearing it looks exactly like a normal falling edge.
    Timer t;
    t.writeTac(TAC_ENABLE | 0x01);   // watches bit 3
    t.resetDiv();

    advance(t, 8);                   // counter = 8, so bit 3 is set
    REQUIRE(t.tima() == 0);

    t.resetDiv();                    // 1 -> 0 on the watched bit
    CHECK(t.tima() == 1);
}

TEST_CASE("changing TAC can tick TIMA") {
    Timer t;
    t.writeTac(TAC_ENABLE | 0x01);   // watches bit 3
    t.resetDiv();
    advance(t, 8);                   // bit 3 set, bit 5 clear
    REQUIRE(t.tima() == 0);

    t.writeTac(TAC_ENABLE | 0x02);   // switch to bit 5: the signal drops
    CHECK(t.tima() == 1);
}

TEST_CASE("disabling the timer can tick TIMA") {
    Timer t;
    t.writeTac(TAC_ENABLE | 0x01);
    t.resetDiv();
    advance(t, 8);                   // watched bit set
    REQUIRE(t.tima() == 0);

    t.writeTac(0x01);                // clearing enable forces the signal low
    CHECK(t.tima() == 1);
}

TEST_CASE("TIMA overflow reloads from TMA one M-cycle late and raises the interrupt") {
    Timer t;
    t.writeTma(0xAB);
    t.writeTac(TAC_ENABLE | 0x01);   // fastest rate: one tick per 16 cycles
    t.resetDiv();
    t.writeTima(0xFF);

    advance(t, 16);                  // overflows
    CHECK(t.tima() == 0x00);         // reads zero during the delay
    CHECK(t.takeIrq() == false);     // and the interrupt has not fired yet

    advance(t, 4);                   // one M-cycle later
    CHECK(t.tima() == 0xAB);
    CHECK(t.takeIrq() == true);
    CHECK(t.takeIrq() == false);     // read-and-clear
}

TEST_CASE("writing TIMA during the reload delay cancels the overflow") {
    Timer t;
    t.writeTma(0xAB);
    t.writeTac(TAC_ENABLE | 0x01);
    t.resetDiv();
    t.writeTima(0xFF);

    advance(t, 16);                  // overflow in flight
    REQUIRE(t.tima() == 0x00);

    t.writeTima(0x42);               // cancels it outright
    advance(t, 4);
    CHECK(t.tima() == 0x42);         // not replaced by TMA
    CHECK(t.takeIrq() == false);     // and no interrupt
}

TEST_CASE("TAC reads back with its unused bits set") {
    Timer t;
    t.writeTac(0x05);
    CHECK(t.tac() == 0xFD);          // bits 3-7 do not exist and read as ones
}
