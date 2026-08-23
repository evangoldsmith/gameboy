// The channel primitives are deliberately free of address decoding so they can
// be driven directly. These tests cover the behaviours that look like bugs
// until you check the hardware: an envelope period of zero disabling the
// envelope, the sequencer stepping on a falling edge, and the noise LFSR's two
// very different repeat lengths.

#include "apu/channels.h"

#include <doctest/doctest.h>

// Groups these under a name so `make test TEST_ARGS="-ts=apu"` works.
TEST_SUITE_BEGIN("apu");

#include <vector>

TEST_CASE("VolumeEnvelope reads back exactly what was written") {
    // The running volume must never write back into the register, or a game
    // reading NRx2 would see the envelope's current level instead of its own
    // configuration.
    VolumeEnvelope e;
    e.write(0xA3);
    e.trigger();
    e.tick();
    CHECK(e.reg() == 0xA3);
}

TEST_CASE("an envelope period of zero disables the envelope entirely") {
    // Easy to invert into "as fast as possible", which is wrong.
    VolumeEnvelope e;
    e.write(0xF0);            // volume 15, period 0
    e.trigger();
    REQUIRE(e.volume() == 15);
    for (int i = 0; i < 10; ++i) e.tick();
    CHECK(e.volume() == 15);
}

TEST_CASE("the envelope ramps in the direction bit 3 selects and clamps at both ends") {
    SUBCASE("upwards, clamping at 15") {
        VolumeEnvelope e;
        e.write(0xD9);        // volume 13, increase, period 1
        e.trigger();
        e.tick();
        CHECK(e.volume() == 14);
        e.tick();
        CHECK(e.volume() == 15);
        e.tick();
        CHECK(e.volume() == 15);   // clamped
    }
    SUBCASE("downwards, clamping at 0") {
        VolumeEnvelope e;
        e.write(0x21);        // volume 2, decrease, period 1
        e.trigger();
        e.tick();
        CHECK(e.volume() == 1);
        e.tick();
        CHECK(e.volume() == 0);
        e.tick();
        CHECK(e.volume() == 0);
    }
}

TEST_CASE("the DAC is powered by bits 3-7 of NRx2, not just the volume") {
    // The mask is 0xF8, which reaches down to the direction bit — so a channel
    // with zero volume but the direction bit set still has a live DAC. Reading
    // "upper five bits" as "the volume nibble" is the easy mistake here.
    VolumeEnvelope e;
    e.write(0x00);
    CHECK(e.dacEnabled() == false);

    e.write(0x08);            // direction alone is enough
    CHECK(e.dacEnabled() == true);

    e.write(0x10);            // as is a single volume bit
    CHECK(e.dacEnabled() == true);
}

TEST_CASE("FrameSequencer steps on the falling edge of DIV bit 4") {
    FrameSequencer fs;
    CHECK(fs.tick(true) == -1);    // low to high is not an edge it cares about
    CHECK(fs.tick(true) == -1);    // holding high does nothing
    CHECK(fs.tick(false) != -1);   // high to low steps
    CHECK(fs.tick(false) == -1);   // holding low does nothing
}

TEST_CASE("reset parks the sequencer so the next step is 0") {
    // Powering the APU on resets it such that step 0 comes next, which is why
    // reset() parks at 7 rather than 0.
    FrameSequencer fs;
    fs.reset();
    fs.tick(true);
    CHECK(fs.tick(false) == 0);
}

TEST_CASE("the sequencer clocks each unit on the documented steps") {
    // Length at 256 Hz, sweep at 128 Hz, envelope at 64 Hz.
    for (int step : {0, 2, 4, 6}) CHECK(FrameSequencer::clocksLength(step));
    for (int step : {1, 3, 5, 7}) CHECK_FALSE(FrameSequencer::clocksLength(step));

    for (int step : {2, 6}) CHECK(FrameSequencer::clocksSweep(step));
    CHECK_FALSE(FrameSequencer::clocksSweep(0));

    CHECK(FrameSequencer::clocksEnvelope(7));
    for (int step = 0; step < 7; ++step) CHECK_FALSE(FrameSequencer::clocksEnvelope(step));
}

TEST_CASE("a triggered pulse channel produces its duty waveform") {
    PulseChannel ch(false);
    ch.writeReg(1, 0x80, true);   // duty 2 = 50%
    ch.writeReg(2, 0xF0, true);   // volume 15, DAC on
    ch.writeReg(3, 0xFF, true);   // period 2047, so one duty step every 4 cycles
    ch.writeReg(4, 0x87, true);   // trigger, frequency high bits
    REQUIRE(ch.enabled());
    REQUIRE(ch.dacEnabled());

    int high = 0;
    for (int i = 0; i < 8; ++i) {
        ch.tick(4);
        if (ch.output() != 0) ++high;
    }
    CHECK(high == 4);             // 50% duty: four of the eight steps
}

TEST_CASE("clearing the DAC kills the channel, and restoring it does not restart it") {
    PulseChannel ch(false);
    ch.writeReg(2, 0xF0, true);
    ch.writeReg(4, 0x80, true);   // trigger
    REQUIRE(ch.enabled());

    ch.writeReg(2, 0x00, true);   // DAC off
    CHECK_FALSE(ch.enabled());

    ch.writeReg(2, 0xF0, true);   // DAC back on, but no trigger
    CHECK_FALSE(ch.enabled());    // needs a trigger to sound again

    ch.writeReg(4, 0x80, true);
    CHECK(ch.enabled());
}

TEST_CASE("the length counter disables the channel when it runs out") {
    PulseChannel ch(false);
    ch.writeReg(2, 0xF0, true);
    ch.writeReg(1, 0x3F, true);   // load 63, so the counter is 64 - 63 = 1
    ch.writeReg(4, 0xC0, true);   // trigger with length enabled
    REQUIRE(ch.enabled());

    ch.tickLength();
    CHECK_FALSE(ch.enabled());
}

TEST_CASE("length ticks are ignored unless NRx4 bit 6 enables them") {
    PulseChannel ch(false);
    ch.writeReg(2, 0xF0, true);
    ch.writeReg(1, 0x3F, true);
    ch.writeReg(4, 0x80, true);   // trigger, length *not* enabled
    REQUIRE(ch.enabled());

    for (int i = 0; i < 100; ++i) ch.tickLength();
    CHECK(ch.enabled());
}

TEST_CASE("triggering with a sweep shift set runs an immediate overflow check") {
    // The check happens at trigger, not on the first sweep tick, so a frequency
    // that would overflow never sounds at all.
    PulseChannel ch(true);        // channel 1 has the sweep unit
    ch.writeReg(0, 0x11, true);   // period 1, add, shift 1
    ch.writeReg(2, 0xF0, true);
    ch.writeReg(3, 0xFF, true);
    ch.writeReg(4, 0x87, true);   // trigger at frequency 2047
    CHECK_FALSE(ch.enabled());    // 2047 + (2047>>1) is past 2047 already
}

TEST_CASE("a sweep that overflows later disables the channel then") {
    PulseChannel ch(true);
    ch.writeReg(0, 0x11, true);   // period 1, add, shift 1
    ch.writeReg(2, 0xF0, true);
    ch.writeReg(3, 0x00, true);   // frequency 1024: survives the trigger check
    ch.writeReg(4, 0x84, true);
    REQUIRE(ch.enabled());

    // Each sweep adds shadow>>1, so the frequency grows by half each time and
    // passes 2047 within a few ticks.
    for (int i = 0; i < 4 && ch.enabled(); ++i) ch.tickSweep();
    CHECK_FALSE(ch.enabled());
}

TEST_CASE("the noise LFSR repeats every 127 steps in 7-bit mode") {
    // The width bit feeds bit 6 as well as bit 14, shortening the repeat from
    // 32767 steps to 127 — short enough to hear as a pitch.
    const auto capture = [](uint8_t nr43) {
        NoiseChannel ch;
        ch.writeReg(1, 0xF0, true);   // volume 15, DAC on
        ch.writeReg(2, nr43, true);   // divisor code 0, shift 0 => 8 cycles/step
        ch.writeReg(3, 0x80, true);   // trigger
        std::vector<uint8_t> seq;
        for (int i = 0; i < 400; ++i) {
            ch.tick(4);
            ch.tick(4);               // one LFSR step per 8 cycles
            seq.push_back(ch.output());
        }
        return seq;
    };

    const std::vector<uint8_t> narrow = capture(0x08);   // 7-bit
    const std::vector<uint8_t> wide   = capture(0x00);   // 15-bit

    bool narrowRepeats = true;
    for (std::size_t i = 0; i + 127 < narrow.size(); ++i)
        if (narrow[i] != narrow[i + 127]) narrowRepeats = false;
    CHECK(narrowRepeats);

    // The 15-bit register runs for 32767 steps, so it must not repeat at 127.
    bool wideRepeats = true;
    for (std::size_t i = 0; i + 127 < wide.size(); ++i)
        if (wide[i] != wide[i + 127]) wideRepeats = false;
    CHECK_FALSE(wideRepeats);
}

TEST_CASE("the wave channel plays its RAM upper nibble first") {
    WaveChannel ch;
    ch.writeWaveRam(0, 0xA5);     // first sample $A, second $5
    ch.writeReg(0, 0x80, true);   // DAC on
    ch.writeReg(2, 0x20, true);   // output level 100%
    ch.writeReg(3, 0xFE, true);   // frequency 2046 => one sample every 4 cycles
    ch.writeReg(4, 0x87, true);   // trigger
    REQUIRE(ch.enabled());

    // Trigger resets the position to 0, and the first advance reads index 1 —
    // the *low* nibble of byte 0 — rather than index 0.
    ch.tick(4);
    CHECK(ch.output() == 0x5);
}

TEST_CASE("wave RAM survives an APU power cycle") {
    WaveChannel ch;
    ch.writeWaveRam(3, 0x7E);
    ch.powerOff();
    CHECK(ch.readWaveRam(3) == 0x7E);
}

TEST_SUITE_END();
