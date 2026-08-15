#include "logger.h"

#include "../cpu/cpu.h"
#include "../memory/mmu.h"

#include <format>

namespace logger {

std::string traceLine(const CPU& cpu, MMU& mmu) {
    const uint16_t pc = cpu.pc();
    return std::format(
        "A:{:02X} F:{:02X} B:{:02X} C:{:02X} D:{:02X} E:{:02X} H:{:02X} L:{:02X} "
        "SP:{:04X} PC:{:04X} PCMEM:{:02X},{:02X},{:02X},{:02X}",
        cpu.a(), cpu.f(), cpu.b(), cpu.c(), cpu.d(), cpu.e(), cpu.h(), cpu.l(),
        cpu.sp(), pc,
        mmu.read(pc),
        mmu.read(static_cast<uint16_t>(pc + 1)),
        mmu.read(static_cast<uint16_t>(pc + 2)),
        mmu.read(static_cast<uint16_t>(pc + 3)));
}

}  // namespace logger
