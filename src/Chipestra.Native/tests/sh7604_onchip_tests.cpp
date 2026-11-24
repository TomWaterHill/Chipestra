// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill

#include "sh7604_onchip_tests.h"

#include "devices/cpu/sh2/sh7604_onchip.h"
#include "test_support.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

// A stand-in for the CPU: it only records the on-chip interrupt request.
class RecordingCore : public ISh2Core {
public:
    void SetCallbacks(Callbacks) override {}
    void Reset() override {}
    std::uint64_t Execute(std::uint64_t) override { return 0; }
    void SetInterruptLine(int, bool) override {}
    void SetNmi(bool) override {}
    void SetInternalInterrupt(int level, std::uint32_t vector) override { level_ = level; vector_ = vector; ++calls_; }
    int level_ = 0;
    std::uint32_t vector_ = 0;
    int calls_ = 0;
};

constexpr std::uint32_t kBase = 0xFFFFFE00u;

}  // namespace

void Sh7604RegistersAreReadBackAtAnyWidth() {
    std::printf("Sh7604RegistersAreReadBackAtAnyWidth\n");
    RecordingCore core;
    Sh7604OnChip chip(core, false);
    chip.Write(kBase + 0x60, 0x0600, 2);             // IPRB: FRT level 6
    Check(chip.Read(kBase + 0x60, 2) == 0x0600, "a 16-bit register reads back what was written");
    Check(chip.Read(kBase + 0x60, 1) == 0x06 && chip.Read(kBase + 0x61, 1) == 0x00, "its bytes are the high byte at the even address");
    chip.Write(kBase + 0x64, 0x1234, 2);             // VCRB
    Check(chip.Read(kBase + 0x64, 2) == 0x1234, "VCRB keeps its 7-bit fields");
    chip.Write(kBase + 0x140, 0xAAAA, 2);            // the user break controller: storage
    Check(chip.Read(kBase + 0x140, 2) == 0xAAAA, "registers without behaviour (UBC) are plain storage");
    chip.Write(kBase + 0xE0, 0xFFFF, 2);
    Check((chip.Read(kBase + 0xE0, 2) & 0x0101) == 0x0101 && (chip.Read(kBase + 0xE0, 2) & 0x8000) != 0, "ICR keeps its two control bits and shows the NMI input high");
    Sh7604OnChip slave(core, true);
    Check((slave.Read(kBase + 0x1E0, 4) & 0x8000) != 0 && (chip.Read(kBase + 0x1E0, 4) & 0x8000) == 0, "BCR1 tells the slave from the master (bit 15)");
    Check(Sh7604OnChip::Contains(0xFFFFFE00u) && Sh7604OnChip::Contains(0xFFFFFFFFu) && !Sh7604OnChip::Contains(0xFFFFFDFFu) && !Sh7604OnChip::Contains(0x06000000u),
          "the on-chip area is 0xFFFFFE00 and up");
    // BSC: a 32-bit write needs the 0xA55A key; a 16-bit write does not
    chip.Write(kBase + 0x1E4, 0x12345678u, 4);
    Check(chip.Read(kBase + 0x1E4, 4) == 0x00FC, "a 32-bit write to BCR2 without the key is ignored");
    chip.Write(kBase + 0x1E4, 0xA55A0ABCu, 4);
    Check(chip.Read(kBase + 0x1E4, 4) == 0x0ABC, "with the key it stores the low 16 bits");
    const std::uint32_t bcr1 = chip.Read(kBase + 0x1E0, 4);
    chip.Write(kBase + 0x1E0, 0x00001234u, 4);
    Check(chip.Read(kBase + 0x1E0, 4) == bcr1, "BCR1 ignores a 32-bit write without the key as well");
    chip.Write(kBase + 0x1E0, 0xA55A0123u, 4);
    Check(chip.Read(kBase + 0x1E0, 4) == (0x0123u & ~0xE008u), "and takes it with the key (some bits read back as fixed)");
}

void Sh7604DivideUnit() {
    std::printf("Sh7604DivideUnit\n");
    RecordingCore core;
    Sh7604OnChip chip(core, false);
    chip.Write(kBase + 0x100, 7, 4);          // DVSR
    chip.Write(kBase + 0x104, 100, 4);        // DVDNT: 32-bit division
    Check(chip.Read(kBase + 0x104, 4) == 14 && chip.Read(kBase + 0x110, 4) == 2, "32 / 32: the quotient in DVDNT, the remainder in DVDNTH");
    chip.Write(kBase + 0x100, static_cast<std::uint32_t>(-7), 4);
    chip.Write(kBase + 0x104, static_cast<std::uint32_t>(-100), 4);
    Check(chip.Read(kBase + 0x104, 4) == 14 && chip.Read(kBase + 0x110, 4) == static_cast<std::uint32_t>(-2), "signed division truncates toward zero, the remainder has the dividend's sign");
    chip.Write(kBase + 0x100, 0x10000, 4);
    chip.Write(kBase + 0x110, 0x1, 4);        // the high half of a 64-bit dividend: 0x1_0000_0000
    chip.Write(kBase + 0x114, 0x0, 4);        // the low half starts the division
    Check(chip.Read(kBase + 0x114, 4) == 0x10000 && chip.Read(kBase + 0x110, 4) == 0, "64 / 32: 0x100000000 / 0x10000");
    chip.Write(kBase + 0x100, 0, 4);
    chip.Write(kBase + 0x104, 5, 4);
    Check(chip.Read(kBase + 0x108, 4) == 1 && chip.Read(kBase + 0x104, 4) == 0x7FFFFFFF, "division by zero sets OVF and saturates the quotient");
    chip.Write(kBase + 0x108, 0, 4);
    Check(chip.Read(kBase + 0x108, 4) == 0, "OVF is cleared by writing 0");
    chip.Write(kBase + 0x100, 0, 4);
    chip.Write(kBase + 0x104, static_cast<std::uint32_t>(-5), 4);
    Check(chip.Read(kBase + 0x104, 4) == 0x80000000u, "with the interrupt disabled a negative overflowing quotient is the minimum H'80000000 (manual 10.3.3)");
    chip.Write(kBase + 0x108, 0, 4);
    chip.Write(kBase + 0x100, static_cast<std::uint32_t>(-1), 4);
    chip.Write(kBase + 0x104, 0x80000000u, 4);
    Check(chip.Read(kBase + 0x104, 4) == 0x7FFFFFFFu, "H'80000000 / -1 overflows to the maximum (a positive quotient)");
    chip.Write(kBase + 0x108, 0, 4);
    chip.Write(kBase + 0x100, 1, 4);
    chip.Write(kBase + 0x110, 0x7FFFFFFF, 4);
    chip.Write(kBase + 0x114, 0, 4);
    Check(chip.Read(kBase + 0x108, 4) == 1, "a quotient that does not fit 32 bits sets OVF");
    chip.Write(kBase + 0x108, 2, 4);          // OVFIE on, OVF off
    chip.Write(kBase + 0xE2, 0x7000, 2);      // IPRA: DIVU level 7
    chip.Write(kBase + 0x10C, 0x45, 4);       // VCRDIV
    chip.Write(kBase + 0x100, 0, 4);
    chip.Write(kBase + 0x104, 1, 4);
    Check(core.level_ == 7 && core.vector_ == 0x45, "an overflow raises the DIVU interrupt at its level with its vector");
}

void Sh7604FreeRunningTimerAndItsInterrupts() {
    std::printf("Sh7604FreeRunningTimerAndItsInterrupts\n");
    RecordingCore core;
    Sh7604OnChip chip(core, false);
    chip.Write(kBase + 0x16, 0, 1);           // TCR: clock / 8
    chip.Advance(8 * 100);
    Check(chip.Frc() == 100, "the counter counts every 8 CPU clocks with TCR = 0");
    chip.Write(kBase + 0x16, 1, 1);           // clock / 32
    chip.Advance(32 * 5 + 31);
    Check(chip.Frc() == 105, "clock / 32; the remainder carries over");
    chip.Advance(1);
    Check(chip.Frc() == 106, "the carried remainder completes a tick");
    chip.Write(kBase + 0x60, 0x0500, 2);      // IPRB: FRT level 5
    chip.Write(kBase + 0x66, 0x2122, 2);      // VCRC: input capture 0x21, output compare 0x22
    chip.Write(kBase + 0x68, 0x2300, 2);      // VCRD: overflow 0x23
    chip.Write(kBase + 0x12, 0, 2);           // FRC = 0
    chip.Write(kBase + 0x14, 0x0040, 2);      // OCRA = 0x40
    chip.Write(kBase + 0x10, 0x08, 1);        // TIER: output compare A interrupt enable
    chip.Write(kBase + 0x16, 0, 1);
    chip.Advance(8 * 0x3F);
    Check(core.level_ == 0 && (chip.Read(kBase + 0x11, 1) & 0x08) == 0, "no flag before the match");
    chip.Advance(8);
    Check((chip.Read(kBase + 0x11, 1) & 0x08) != 0 && core.level_ == 5 && core.vector_ == 0x22, "the match sets OCFA and requests the interrupt at its level with the output compare vector");
    chip.Write(kBase + 0x11, 0x00, 1);
    Check((chip.Read(kBase + 0x11, 1) & 0x08) == 0 && core.level_ == 0, "writing 0 clears the flag and withdraws the request");
    chip.Write(kBase + 0x11, 0xFF, 1);        // writing 1s never sets a flag
    Check((chip.Read(kBase + 0x11, 1) & 0x8E) == 0, "writing 1 to a clear flag does not set it");
    chip.Write(kBase + 0x11, 0x01, 1);        // CCLRA
    chip.Write(kBase + 0x12, 0, 2);
    chip.Advance(8 * 0x50);
    Check(chip.Frc() == 0x10, "with CCLRA the counter restarts at each match of A");
    chip.Write(kBase + 0x11, 0x00, 1);
    chip.Write(kBase + 0x10, 0x02, 1);
    chip.Write(kBase + 0x12, 0xFFF0, 2);
    chip.Write(kBase + 0x14, 0xFFFF, 2);
    chip.Advance(8 * 0x10);
    Check((chip.Read(kBase + 0x11, 1) & 0x02) != 0 && core.level_ == 5 && core.vector_ == 0x23, "the overflow sets OVF and requests the interrupt with the overflow vector");
    chip.Write(kBase + 0x17, 0x10, 1);
    chip.Write(kBase + 0x14, 0x1111, 2);
    chip.Write(kBase + 0x17, 0x00, 1);
    Check(chip.Read(kBase + 0x14, 2) == 0xFFFF, "OCRA keeps its value while OCRS selects OCRB");
    chip.Write(kBase + 0x17, 0x10, 1);
    Check(chip.Read(kBase + 0x14, 2) == 0x1111, "and OCRB reads back through the same address");
}

void Sh7604WatchdogAndBusController() {
    std::printf("Sh7604WatchdogAndBusController\n");
    RecordingCore core;
    Sh7604OnChip chip(core, false);
    chip.Write(kBase + 0xE2, 0x0030, 2);          // IPRA: WDT level 3
    chip.Write(kBase + 0xE4, 0x4800, 2);          // VCRWDT: interval timer vector 0x48
    chip.Write(kBase + 0x80, 0xA500 | 0x20, 2);   // WTCSR: enabled, interval timer, clock / 2
    chip.Write(kBase + 0x80, 0x5A00 | 0xF0, 2);   // WTCNT = 0xF0
    chip.Advance(2 * 0x0F);
    Check((chip.Read(kBase + 0x80, 2) & 0x8000) == 0, "the interval timer has not overflowed yet");
    chip.Advance(2);
    Check((chip.Read(kBase + 0x80, 2) & 0x8000) != 0 && core.level_ == 3 && core.vector_ == 0x48, "its overflow sets OVF and requests the interrupt");
    chip.Write(kBase + 0x80, 0xA500 | 0x20, 2);   // rewriting WTCSR with OVF = 0 clears it
    Check((chip.Read(kBase + 0x80, 2) & 0x8000) == 0 && core.level_ == 0, "writing WTCSR with OVF clear clears the flag");
    Check((chip.Read(kBase + 0x82, 2) & 0x1F) == 0x1F, "RSTCSR reads with its reserved bits set");
}

void Sh7604DmaController() {
    std::printf("Sh7604DmaController\n");
    RecordingCore core;
    Sh7604OnChip chip(core, false);
    std::vector<std::uint8_t> memory(0x1000, 0);
    for (std::size_t i = 0; i < 64; ++i) memory[0x100 + i] = static_cast<std::uint8_t>(i + 1);
    bool dreq = false;
    chip.SetDmaBus(
        [&](std::uint32_t a, int bytes) { std::uint32_t v = 0; for (int i = 0; i < bytes; ++i) v = (v << 8) | memory[(a + i) & 0xFFF]; return v; },
        [&](std::uint32_t a, std::uint32_t v, int bytes) { for (int i = 0; i < bytes; ++i) memory[(a + i) & 0xFFF] = static_cast<std::uint8_t>(v >> ((bytes - 1 - i) * 8)); },
        [&](int) { return dreq; });
    auto setup = [&](std::uint32_t sar, std::uint32_t dar, std::uint32_t count, std::uint32_t chcr) {
        chip.Write(kBase + 0x180, sar, 4);
        chip.Write(kBase + 0x184, dar, 4);
        chip.Write(kBase + 0x188, count, 4);
        chip.Write(kBase + 0x1B0, 1, 4);               // DMAOR: DME
        chip.Write(kBase + 0x18C, chcr, 4);
    };
    // auto request, longword units, both addresses increment, interrupt on the end: CHCR = DM1 SM1 TS=2 AR IE DE
    chip.Write(kBase + 0x1A0, 0x45, 4);                 // VCRDMA0
    chip.Write(kBase + 0xE2, 0x0100, 2);                // IPRA: DMAC level 1
    setup(0x100, 0x200, 4, 0x4000 | 0x1000 | 0x0800 | 0x0200 | 0x0004 | 0x0001);
    chip.Advance(6);                                    // three units of two cycles
    Check(chip.Read(kBase + 0x188, 4) == 1 && (chip.Read(kBase + 0x18C, 4) & 2) == 0, "a unit takes two cycles: after six cycles one of four is left");
    chip.Advance(2);
    Check(chip.Read(kBase + 0x188, 4) == 0 && (chip.Read(kBase + 0x18C, 4) & 2) != 0, "the count reaches 0 and TE is set");
    bool copied = true;
    for (int i = 0; i < 16; ++i) copied = copied && memory[0x200 + i] == memory[0x100 + i];
    Check(copied && memory[0x210] == 0, "sixteen bytes were copied and no more");
    Check(chip.Read(kBase + 0x184, 4) == 0x210 && chip.Read(kBase + 0x180, 4) == 0x110, "SAR and DAR advanced by the data moved");
    Check(core.level_ == 1 && core.vector_ == 0x45, "the end interrupt is requested at the DMAC level with VCRDMA's vector");
    chip.Write(kBase + 0x18C, chip.Read(kBase + 0x18C, 4) & ~2u, 4);
    Check((chip.Read(kBase + 0x18C, 4) & 2) == 0 && core.level_ == 0, "writing 0 to TE clears it and the request");
    // byte units, destination decrements
    chip.Write(kBase + 0x18C, 0, 4);
    setup(0x100, 0x2FF, 3, 0x8000 | 0x1000 | 0x0200 | 0x0001);   // DM=2 (decrement: each unit goes to DAR, then DAR goes down), SM=1, TS=0 (byte)
    chip.Advance(10);
    Check(memory[0x2FF] == 1 && memory[0x2FE] == 2 && memory[0x2FD] == 3 && memory[0x2FC] == 0, "with a decrementing destination the bytes go down");
    Check((chip.Read(kBase + 0x18C, 4) & 2) != 0 && core.level_ == 0, "a transfer that ends without the interrupt enabled sets TE and requests nothing");
    // fixed source (a FIFO), word units
    chip.Write(kBase + 0x18C, 0, 4);
    setup(0x100, 0x300, 2, 0x4000 | 0x0400 | 0x0200 | 0x0001);   // DM=1, SM=0, TS=1 (word)
    chip.Advance(10);
    Check(memory[0x300] == 1 && memory[0x301] == 2 && memory[0x302] == 1 && memory[0x303] == 2, "a fixed source is read again for each unit");
    // 16-byte unit: the count is in longwords
    chip.Write(kBase + 0x18C, 0, 4);
    setup(0x100, 0x400, 8, 0x4000 | 0x0C00 | 0x0200 | 0x0001);   // TS=3
    chip.Advance(10);
    bool block = true;
    for (int i = 0; i < 32; ++i) block = block && memory[0x400 + i] == memory[0x100 + i];
    Check(block && memory[0x420] == 0 && chip.Read(kBase + 0x180, 4) == 0x120, "the 16-byte unit moves 16 bytes, counts four, and the source moves on by 16");
    // external request: nothing moves until the partner has data
    chip.Write(kBase + 0x18C, 0, 4);
    setup(0x100, 0x500, 2, 0x4000 | 0x1000 | 0x0400 | 0x0001);   // no AR
    chip.Advance(10);
    Check(memory[0x500] == 0, "with an external request and no DREQ nothing is moved");
    dreq = true;
    chip.Advance(10);
    Check(memory[0x500] == 1 && (chip.Read(kBase + 0x18C, 4) & 2) != 0, "it runs while DREQ is asserted");
    // two channels at once: channel 0 goes first (fixed priority), with DMAOR.PR the channels take turns, channel 1 first
    auto twoChannels = [&](std::uint32_t dmaor) {
        chip.Write(kBase + 0x18C, 0, 4); chip.Write(kBase + 0x19C, 0, 4);
        chip.Write(kBase + 0x1B0, dmaor, 4);
        chip.Write(kBase + 0x180, 0x100, 4); chip.Write(kBase + 0x184, 0x700, 4); chip.Write(kBase + 0x188, 2, 4);
        chip.Write(kBase + 0x190, 0x100, 4); chip.Write(kBase + 0x194, 0x800, 4); chip.Write(kBase + 0x198, 2, 4);
        for (int i = 0; i < 0x20; ++i) { memory[0x700 + i] = 0; memory[0x800 + i] = 0; }
        const std::uint32_t chcr = 0x4000 | 0x1000 | 0x0800 | 0x0200 | 0x0001;
        chip.Write(kBase + 0x18C, chcr, 4); chip.Write(kBase + 0x19C, chcr, 4);
        chip.Advance(2);                                   // one unit
    };
    twoChannels(1);
    Check(memory[0x700] == 1 && memory[0x800] == 0, "with fixed priority channel 0 goes before channel 1");
    twoChannels(1 | 8);
    Check(memory[0x700] == 0 && memory[0x800] == 1, "with DMAOR.PR the first transfer after a reset is channel 1's");
    chip.Advance(2);
    Check(memory[0x700] == 1, "and then the channels take turns");
    // DMAOR: the controller is off without DME
    chip.Write(kBase + 0x18C, 0, 4);
    chip.Write(kBase + 0x1B0, 0, 4);
    chip.Write(kBase + 0x180, 0x100, 4); chip.Write(kBase + 0x184, 0x600, 4); chip.Write(kBase + 0x188, 2, 4);
    chip.Write(kBase + 0x18C, 0x4000 | 0x1000 | 0x0400 | 0x0200 | 0x0001, 4);
    chip.Advance(10);
    Check(memory[0x600] == 0, "nothing moves while DMAOR.DME is 0");
}
