#ifndef LOGGER_H
#define LOGGER_H

#include <string>

class CPU;
class MMU;

namespace logger {

// One trace line in Game Boy Doctor's format:
//
//   A:01 F:B0 B:00 C:13 D:00 E:D8 H:01 L:4D SP:FFFE PC:0100 PCMEM:00,C3,13,02
//
// Diff this against the golden logs at github.com/robert/gameboy-doctor and the
// first mismatching line is the instruction that broke. Capture it with LY
// stubbed to $90 (PPU::setLyStub) — the reference logs were made that way.
std::string traceLine(const CPU& cpu, MMU& mmu);

}  // namespace logger

#endif // LOGGER_H
