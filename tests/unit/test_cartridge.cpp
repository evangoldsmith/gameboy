// Bank arithmetic differs between mappers in small ways that are easy to get
// wrong — and one of them, MBC3's RAM-bank register not touching the ROM bank,
// is exactly what once sent Pokemon Red into the wrong bank and crashed it on
// an illegal opcode.
//
// Each synthetic ROM fills every bank with its own bank number, so a read says
// which bank is currently mapped.

#include "cartridge/cartridge.h"

#include "synthetic_rom.h"

#include <doctest/doctest.h>

// Groups these under a name so `make test TEST_ARGS="-ts=cartridge"` works.
TEST_SUITE_BEGIN("cartridge");

namespace {

// Which bank is currently mapped at $4000-$7FFF.
uint8_t mappedBank(const Cartridge& c) { return c.read(0x4000); }

}  // namespace

TEST_CASE("header decoding") {
    SyntheticRom rom(0x13, 5, 3);          // MBC3 + RAM + BATTERY, 1 MB, 32 KB RAM
    const Cartridge cart = Cartridge::load(rom.path());

    CHECK(cart.header().mbcType == MBCType::MBC3);
    CHECK(cart.header().romBytes == 1024u * 1024u);
    CHECK(cart.header().ramBytes == 32u * 1024u);
    CHECK(cart.header().battery == true);
}

TEST_CASE("cartridges without a battery are not marked as saving") {
    SyntheticRom rom(0x11, 3, 0);          // MBC3, no RAM, no battery
    CHECK(Cartridge::load(rom.path()).header().battery == false);
}

TEST_CASE("bank 0 is always mapped low and the switchable window starts at bank 1") {
    SyntheticRom rom(0x01, 3, 0);          // MBC1, 256 KB
    Cartridge cart = Cartridge::load(rom.path());
    CHECK(cart.read(0x0000) == 0);
    CHECK(mappedBank(cart) == 1);          // banks start at 1, not 0
}

TEST_CASE("MBC1 treats a written bank of 0 as bank 1") {
    // The quirk that makes banks $20/$40/$60 unreachable: their low five bits
    // are zero, so they alias upward.
    SyntheticRom rom(0x01, 3, 0);
    Cartridge cart = Cartridge::load(rom.path());

    cart.write(0x2000, 0x00);
    CHECK(mappedBank(cart) == 1);

    cart.write(0x2000, 0x05);
    CHECK(mappedBank(cart) == 5);
}

TEST_CASE("MBC1 aliases bank $20 to $21") {
    SyntheticRom rom(0x01, 6, 0);          // 2 MB, so bank $21 exists
    Cartridge cart = Cartridge::load(rom.path());

    cart.write(0x4000, 0x01);              // upper bits -> bank $20
    cart.write(0x2000, 0x00);              // low bits zero, which becomes 1
    CHECK(mappedBank(cart) == 0x21);
}

TEST_CASE("MBC3 takes a full 7-bit bank number from one register") {
    // MBC1 would mask this to five bits and land on bank $0A.
    SyntheticRom rom(0x13, 5, 3);          // MBC3, 1 MB = 64 banks
    Cartridge cart = Cartridge::load(rom.path());

    cart.write(0x2000, 0x2A);
    CHECK(mappedBank(cart) == 0x2A);
}

TEST_CASE("MBC3's RAM bank register leaves the ROM bank alone") {
    // This is the Pokemon Red bug. On MBC1 that register holds the upper ROM
    // bits, so treating an MBC3 as MBC1 added 32 to the bank the moment the
    // game selected save RAM bank 1, and execution continued in the wrong bank.
    SyntheticRom rom(0x13, 5, 3);
    Cartridge cart = Cartridge::load(rom.path());

    cart.write(0x2000, 0x05);
    REQUIRE(mappedBank(cart) == 5);

    cart.write(0x4000, 0x01);              // select RAM bank 1
    CHECK(mappedBank(cart) == 5);          // ROM bank must not move
}

TEST_CASE("MBC5 can select bank 0 and spreads 9 bits across two registers") {
    SyntheticRom rom(0x19, 6, 0);          // MBC5, 2 MB = 128 banks
    Cartridge cart = Cartridge::load(rom.path());

    cart.write(0x2000, 0x00);              // unlike MBC1/MBC3, this really is 0
    CHECK(mappedBank(cart) == 0);

    cart.write(0x2000, 0x42);
    CHECK(mappedBank(cart) == 0x42);

    cart.write(0x3000, 0x01);              // bit 8 lives in its own register
    cart.write(0x2000, 0x05);
    CHECK(cart.read(0x4000) == static_cast<uint8_t>(0x105 & 0xFF));
}

TEST_CASE("MBC2 picks its register from bit 8 of the address") {
    // The only mapper where the address bit rather than the range decides.
    SyntheticRom rom(0x05, 3, 0);          // MBC2, 256 KB
    Cartridge cart = Cartridge::load(rom.path());

    cart.write(0x2100, 0x03);              // bit 8 set -> ROM bank
    CHECK(mappedBank(cart) == 3);

    cart.write(0x2000, 0x07);              // bit 8 clear -> RAM enable, not bank
    CHECK(mappedBank(cart) == 3);
}

TEST_CASE("cartridge RAM only responds once enabled") {
    SyntheticRom rom(0x03, 3, 2);          // MBC1 + RAM + BATTERY, 8 KB RAM
    Cartridge cart = Cartridge::load(rom.path());

    cart.write(0xA000, 0x42);              // RAM still disabled
    CHECK(cart.read(0xA000) == 0xFF);

    cart.write(0x0000, 0x0A);              // $0A in the low nibble enables it
    cart.write(0xA000, 0x42);
    CHECK(cart.read(0xA000) == 0x42);

    cart.write(0x0000, 0x00);              // disabled again
    CHECK(cart.read(0xA000) == 0xFF);
}

TEST_CASE("MBC2 RAM is four bits wide and mirrored") {
    SyntheticRom rom(0x06, 3, 0);          // MBC2 + BATTERY; its RAM is on-chip
    Cartridge cart = Cartridge::load(rom.path());

    cart.write(0x0000, 0x0A);
    cart.write(0xA000, 0x5C);
    CHECK(cart.read(0xA000) == 0xFC);      // upper nibble is unwired, reads ones
    CHECK(cart.read(0xA200) == 0xFC);      // 512 half-bytes mirrored across
}

TEST_CASE("a bank number past the end of the ROM wraps rather than reading garbage") {
    SyntheticRom rom(0x13, 1, 0);          // MBC3, 64 KB = 4 banks
    Cartridge cart = Cartridge::load(rom.path());

    cart.write(0x2000, 0x06);              // only banks 0-3 exist
    CHECK(mappedBank(cart) == 6 % 4);
}

TEST_CASE("a cartridge with no mapper ignores register writes") {
    SyntheticRom rom(0x00, 1, 0);          // ROM only
    Cartridge cart = Cartridge::load(rom.path());

    cart.write(0x2000, 0x03);
    CHECK(mappedBank(cart) == 1);          // still the fixed second bank
}

TEST_CASE("MBC1 reaches RAM banks only in mode 1") {
    // The secondary register is shared between upper ROM bits and RAM banking,
    // and the mode flag decides which it drives. Applying it to RAM in the
    // default mode makes a 32 KB cartridge look like it banks when it must not.
    SyntheticRom rom(0x03, 1, 0x03);        // MBC1 + RAM + BATTERY, 32 KB RAM
    Cartridge cart = Cartridge::load(rom.path());
    cart.write(0x0000, 0x0A);               // enable RAM

    cart.write(0x6000, 0x01);               // mode 1: banking reaches RAM
    for (uint8_t b = 0; b < 4; ++b) {
        cart.write(0x4000, b);
        cart.write(0xA000, static_cast<uint8_t>(0xE0 + b));
    }
    for (uint8_t b = 0; b < 4; ++b) {
        cart.write(0x4000, b);
        CHECK(cart.read(0xA000) == static_cast<uint8_t>(0xE0 + b));
    }

    cart.write(0x6000, 0x00);               // mode 0: always bank 0
    for (uint8_t b = 0; b < 4; ++b) {
        cart.write(0x4000, b);
        CHECK(cart.read(0xA000) == 0xE0);
    }
}

TEST_CASE("MBC1 mode 1 also banks the low ROM window") {
    // Mode 1 is how banks $20/$40/$60 become reachable: the secondary register
    // moves $0000-$3FFF off bank 0.
    SyntheticRom rom(0x01, 6, 0);           // MBC1, 2 MB
    Cartridge cart = Cartridge::load(rom.path());

    cart.write(0x4000, 0x01);
    CHECK(cart.read(0x0000) == 0x00);       // mode 0: still bank 0

    cart.write(0x6000, 0x01);
    CHECK(cart.read(0x0000) == 0x20);       // mode 1: bank 1 << 5
}

TEST_SUITE_END();
