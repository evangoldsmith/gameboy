// Bus-level behaviour that is easy to break from a distance: which addresses
// exist at all, and which registers the MMU owns rather than delegating.

#include "gameboy.h"

#include "synthetic_rom.h"

#include <doctest/doctest.h>

// Groups these under a name so `make test TEST_ARGS="-ts=mmu"` works.
TEST_SUITE_BEGIN("mmu");

namespace {
// A plain ROM-only cartridge is enough; these tests never leave the I/O page.
SyntheticRom g_rom(0x00, 1, 0);
}  // namespace

TEST_CASE("registers that do not exist on a DMG read as $FF") {
    // Falling through to a zero-filled array instead reports every unused
    // register as $00, which is what Mooneye's unused_hwio catches.
    GameBoy gb(g_rom.path());

    // Gaps in the I/O page, plus registers that are CGB-only. Spelled as a
    // typed array rather than a braced list: the literals are int, and
    // narrowing them in a range-for warns under GCC's -Wconversion.
    static constexpr uint16_t unused[] = {0xFF03, 0xFF08, 0xFF0E, 0xFF4C,
                                          0xFF4D, 0xFF4F, 0xFF51, 0xFF55,
                                          0xFF68, 0xFF6B, 0xFF70, 0xFF7F};
    for (uint16_t addr : unused) {
        CAPTURE(addr);
        CHECK(gb.mmu().read(addr) == 0xFF);
    }
}

TEST_CASE("writing a nonexistent register is harmless and does not stick") {
    GameBoy gb(g_rom.path());
    gb.mmu().write(0xFF4D, 0x42);
    CHECK(gb.mmu().read(0xFF4D) == 0xFF);
}

TEST_CASE("IF is writable from software") {
    // IF is stored by the MMU rather than owned by a component, so it needs an
    // explicit write case. Losing that makes raising an interrupt from software
    // silently do nothing — which breaks Blargg's 02-interrupts and little else,
    // so it is worth pinning here.
    GameBoy gb(g_rom.path());

    gb.mmu().write(0xFF0F, 0x04);            // request the timer interrupt
    CHECK((gb.mmu().read(0xFF0F) & 0x1F) == 0x04);

    gb.mmu().write(0xFF0F, 0x00);
    CHECK((gb.mmu().read(0xFF0F) & 0x1F) == 0x00);
}

TEST_CASE("IF reads back with its three unused bits set") {
    GameBoy gb(g_rom.path());
    gb.mmu().write(0xFF0F, 0x00);
    CHECK((gb.mmu().read(0xFF0F) & 0xE0) == 0xE0);
}

TEST_CASE("the DMA register reads back the source it was given") {
    GameBoy gb(g_rom.path());
    gb.mmu().write(0xFF46, 0xC0);
    CHECK(gb.mmu().read(0xFF46) == 0xC0);
}

TEST_CASE("echo RAM mirrors work RAM") {
    GameBoy gb(g_rom.path());
    gb.mmu().write(0xC000, 0x5A);
    CHECK(gb.mmu().read(0xE000) == 0x5A);
    gb.mmu().write(0xE100, 0xA5);
    CHECK(gb.mmu().read(0xC100) == 0xA5);
}

TEST_CASE("OAM DMA takes 160 M-cycles rather than happening at once") {
    GameBoy gb(g_rom.path());

    // Stage a recognisable pattern in work RAM and copy it from there.
    for (int i = 0; i < 0xA0; ++i)
        gb.mmu().write(static_cast<uint16_t>(0xC000 + i), static_cast<uint8_t>(i));

    gb.mmu().write(0xFF46, 0xC0);

    // Partway through, only some of it has landed. Two of these cycles are the
    // scheduling delay before the transfer starts at all.
    for (int i = 0; i < 42; ++i) gb.mmu().tick(4);
    CHECK(gb.ppu().readOam(0xFE00) == 0x00);
    CHECK(gb.ppu().readOam(0xFE9F) != 0x9F);

    // 160 M-cycles plus the two scheduling cycles covers the whole transfer.
    for (int i = 0; i < 130; ++i) gb.mmu().tick(4);
    CHECK(gb.ppu().readOam(0xFE00) == 0x00);
    CHECK(gb.ppu().readOam(0xFE50) == 0x50);
    CHECK(gb.ppu().readOam(0xFE9F) == 0x9F);
}

TEST_CASE("the memory bus reads $FF while a DMA is running, but I/O does not") {
    GameBoy gb(g_rom.path());
    gb.mmu().write(0xC000, 0x11);
    REQUIRE(gb.mmu().read(0xC000) == 0x11);

    gb.mmu().write(0xFF46, 0xC0);
    // The transfer starts two M-cycles after the write, not immediately, so
    // OAM and the bus stay readable until then.
    CHECK(gb.mmu().read(0xC000) == 0x11);
    gb.mmu().tick(4);
    gb.mmu().tick(4);                       // now it is running

    CHECK(gb.mmu().read(0xC000) == 0xFF);   // work RAM is behind the held bus
    CHECK(gb.mmu().read(0x0000) == 0xFF);   // so is ROM, which is why the
                                            // trigger routine must live in HRAM
    CHECK(gb.mmu().read(0xFF46) == 0xC0);   // I/O is inside the CPU, still live

    gb.mmu().write(0xFF80, 0x22);
    CHECK(gb.mmu().read(0xFF80) == 0x22);   // HRAM stays reachable throughout

    for (int i = 0; i < 200; ++i) gb.mmu().tick(4);
    CHECK(gb.mmu().read(0xC000) == 0x11);   // and comes back afterwards
}

TEST_SUITE_END();
