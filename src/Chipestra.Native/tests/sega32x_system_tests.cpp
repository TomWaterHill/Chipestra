// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill

#include "sega32x_system_tests.h"

#include "devices/sega/megadrive/sega32x_system.h"
#include "test_support.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

void Put16(std::vector<std::uint8_t>& rom, std::uint32_t at, std::uint16_t v) { rom[at] = static_cast<std::uint8_t>(v >> 8); rom[at + 1] = static_cast<std::uint8_t>(v); }
void Put32(std::vector<std::uint8_t>& rom, std::uint32_t at, std::uint32_t v) { Put16(rom, at, static_cast<std::uint16_t>(v >> 16)); Put16(rom, at + 2, static_cast<std::uint16_t>(v)); }

// A cartridge whose header names a 1 KB program copied to the start of SDRAM (source 0x400), both processors at 0x06000200 or
// 0x06000280, vectors at 0x06000000. The program (SDRAM address: code):
//   0200 master: R1 = 0x20004030 (PWM), R2 = 0x20004000 (system register); the PWM control register = 0x0205 (both outputs, an
//        interrupt every 2 samples), cycle = 100; register 0x4000 = 9 (the vertical and the PWM interrupt allowed);
//        SR mask 0; then a loop
//   0240 PWM interrupt service: clear the interrupt (write 0x401C), R4 += 1, write R4 to the mono register (0x4038), RTE
//   0270 vertical interrupt service: clear it (write 0x4016), R6 += 1, RTE
//   0280 slave: a loop
std::vector<std::uint8_t> MakeCartridge() {
    std::vector<std::uint8_t> rom(0x1000, 0);
    Put32(rom, 0x3D4, 0x400);       // source offset in the ROM
    Put32(rom, 0x3D8, 0);           // destination in SDRAM
    Put32(rom, 0x3DC, 0x400);       // size
    Put32(rom, 0x3E0, 0x06000200);  // master start
    Put32(rom, 0x3E4, 0x06000280);  // slave start
    Put32(rom, 0x3E8, 0x06000000);  // master vector base
    Put32(rom, 0x3EC, 0x06000000);  // slave vector base
    auto sdram = [&](std::uint32_t at) { return 0x400 + at; };
    Put32(rom, sdram(0x10C), 0x06000240);   // vector 67: the PWM level (6 / 2 + 64)
    Put32(rom, sdram(0x118), 0x06000270);   // vector 70: the vertical level (12 / 2 + 64)
    const std::uint16_t main[] = {0xD107, 0xD208, 0xE002, 0x4018, 0x7005, 0x2101, 0xE064, 0x8111, 0xE009, 0x2201, 0xE000, 0x400E, 0xAFFE, 0x0009};
    std::uint32_t at = 0x200;
    for (std::uint16_t w : main) { Put16(rom, sdram(at), w); at += 2; }
    Put32(rom, sdram(0x220), 0x20004030);
    Put32(rom, sdram(0x224), 0x20004000);
    const std::uint16_t pwmIsr[] = {0xD106, 0xE000, 0x2101, 0xD306, 0x7401, 0x2341, 0x002B, 0x0009};
    at = 0x240;
    for (std::uint16_t w : pwmIsr) { Put16(rom, sdram(at), w); at += 2; }
    Put32(rom, sdram(0x25C), 0x2000401C);
    Put32(rom, sdram(0x260), 0x20004038);
    const std::uint16_t vintIsr[] = {0xD107, 0xE000, 0x2101, 0x7601, 0x002B, 0x0009};
    at = 0x270;
    for (std::uint16_t w : vintIsr) { Put16(rom, sdram(at), w); at += 2; }
    Put32(rom, sdram(0x290), 0x20004016);
    Put16(rom, sdram(0x280), 0xAFFE);
    Put16(rom, sdram(0x282), 0x0009);
    return rom;
}

}  // namespace

void Sega32xSystemBootsFromTheCartridgeHeader() {
    std::printf("Sega32xSystemBootsFromTheCartridgeHeader\n");
    Sega32xSystem system(MakeCartridge());
    Check(system.BootFromHeader(), "the header names a program inside the ROM and the SDRAM");
    Check(system.Read(0, 0x06000200, 2) == 0xD107 && system.Read(0, 0x26000200, 2) == 0xD107, "the program is copied to SDRAM, also seen through the uncached mirror");
    Check(system.Cpu(0).GetRegisters().pc == 0x06000200 && system.Cpu(1).GetRegisters().pc == 0x06000280, "each processor starts at the address of the header");
    Check(system.Read(0, 0x02000000 + 0x3E0, 4) == 0x06000200, "the cartridge is seen at 0x02000000");
    Check(system.CommWord(0) == 0x4D5F && system.CommWord(1) == 0x4F4B && system.CommWord(2) == 0x535F && system.CommWord(3) == 0x4F4B, "the communication registers hold \"M_OK\" and \"S_OK\" as the BIOS leaves them");
    std::vector<std::uint8_t> bad(0x1000, 0);
    Put32(bad, 0x3D4, 0x2000);
    Put32(bad, 0x3DC, 0x400);
    Sega32xSystem broken(bad);
    Check(!broken.BootFromHeader(), "a header whose program lies outside the ROM is refused");
    // memory areas
    system.Write(0, 0xC0000010, 0x12345678u, 4);
    Check(system.Read(0, 0xC0000010, 4) == 0x12345678u && system.Read(1, 0xC0000010, 4) == 0, "the cache RAM is private to each processor");
    Check(system.Read(0, 0x40000000, 4) == 0xA5A5A5A5u, "the cache address array area reads 0xA5A5A5A5");
}

void Sega32xSystemCommunicationRegisters() {
    std::printf("Sega32xSystemCommunicationRegisters\n");
    Sega32xSystem system(MakeCartridge());
    system.BootFromHeader();
    system.M68kWrite(0x20 + 4, 0xBEEF);
    Check(system.Read(0, 0x20004024, 2) == 0xBEEF && system.Read(1, 0x20004024, 2) == 0xBEEF, "a word the 68000 writes at 0xA15124 is seen by both SH-2 at 0x4024");
    system.Write(1, 0x20004026, 0x1234, 2);
    Check(system.M68kRead(0x26) == 0x1234, "and a word an SH-2 writes is seen by the 68000");
    system.Write(0, 0x20004022, 0xAB, 1);
    system.Write(0, 0x20004023, 0xCD, 1);
    Check(system.Read(0, 0x20004022, 2) == 0xABCD && system.Read(0, 0x20004022, 1) == 0xAB && system.Read(0, 0x20004023, 1) == 0xCD, "byte accesses compose the big-endian word");
    system.M68kWrite(0x24, 0x0012, 0x00FF);
    Check(system.Read(0, 0x20004024, 2) == 0xBE12, "the 68000's byte write (mask) changes only that byte");
    Check(system.Read(0, 0x20004022, 4) == 0xABCD0000u + 0x1234 - 0x1234 + (system.Read(0, 0x20004024, 2)), "a long read gives two consecutive words");
}

void Sega32xSystemPwmInterruptServiceFeedsTheFifo() {
    std::printf("Sega32xSystemPwmInterruptServiceFeedsTheFifo\n");
    Sega32xSystem system(MakeCartridge());
    system.BootFromHeader();
    system.RunTo(3000);
    Check(system.Pwm().Control() == 0x0205 && system.Pwm().Cycle() == 100, "the master's program sets the PWM control and cycle registers");
    Check(system.Read(0, 0x20004000, 2) == 0x0209, "and the interrupt mask register (the adapter bit and the two allowed interrupts)");
    std::vector<float> l(64), r(64);
    system.RenderPwm(0, 23011360.0 / 100.0, l.data(), r.data(), 30);   // one output frame per PWM sample (cycle = 100 clocks)
    // each interrupt service writes the next count to the mono register: 0.01, 0.02, ... (width / cycle)
    int steps = 0;
    float previous = 0;
    bool increasing = true;
    for (int i = 0; i < 30; ++i) {
        if (l[i] != previous) { if (l[i] < previous) increasing = false; ++steps; previous = l[i]; }
    }
    Check(steps >= 8 && increasing && std::fabs(l[0]) < 1e-6f, "the left output steps up as the interrupt service writes 1, 2, 3 ... (judged correct)");
    Check(std::fabs(previous - steps * 0.01f) < 1e-4f, "each step is width / cycle = 0.01");
    bool same = true;
    for (int i = 0; i < 30; ++i) if (l[i] != r[i]) same = false;
    Check(same, "the mono register feeds both outputs");
    // the interrupt every 2 samples: the writes come every 200 cycles
    Check(system.Cpu(0).GetRegisters().exceptionCount >= 10, "the processor took the PWM interrupt every second sample");
}

void Sega32xSystemVerticalInterruptMaskAndClear() {
    std::printf("Sega32xSystemVerticalInterruptMaskAndClear\n");
    Sega32xSystem system(MakeCartridge());
    system.BootFromHeader();
    system.RunTo(2000);
    Check(system.Cpu(0).GetRegisters().r[6] == 0, "no vertical interrupt before the blank");
    system.SetVblank(true);
    system.RunTo(4000);
    Check(system.Cpu(0).GetRegisters().r[6] == 1, "the vertical blank makes one vertical interrupt in a processor that allowed it");
    Check(system.Cpu(1).GetRegisters().exceptionCount == 0, "the slave did not allow it, so it took none");
    Check((system.Read(0, 0x2000410A, 2) & 0x8000) != 0, "the frame buffer control register shows the blank");
    system.SetVblank(false);
    Check((system.Read(0, 0x2000410A, 2) & 0x8000) == 0, "and not after it");
    system.SetVblank(true);
    system.RunTo(6000);
    Check(system.Cpu(0).GetRegisters().r[6] == 2, "the next blank makes the next interrupt (the service cleared the first)");
}
