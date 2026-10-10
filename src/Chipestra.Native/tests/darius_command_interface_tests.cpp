// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Synthetic-stimulus tests for DariusCommandInterface (no real ROM data).
//
// Verifies the master (Send()) <-> slave (SlavePortW/SlaveCommR/SlaveCommW)
// round trip matches the real PC060HA/TC0140SYT protocol: a message is
// only delivered as two nibbles (mode 0 then mode 1), NMI only fires once
// the slave has explicitly enabled it (mode 6), and the slave's own second
// read (mode 1) clears both the PORT01_FULL status bit and the NMI line.

#include "darius_command_interface_tests.h"

#include "devices/taito/darius/darius_command_interface.h"
#include "test_support.h"

#include <cstdint>
#include <cstdio>

namespace {

struct NmiProbe {
    int asserts = 0;
    int clears = 0;
    bool last = false;

    DariusCommandInterface::NmiHandler Handler() {
        return [this](bool asserted) {
            if (asserted) {
                ++asserts;
            } else {
                ++clears;
            }
            last = asserted;
        };
    }
};

}  // namespace

void DariusCommandInterfaceSlaveNmiGatedByEnableFlag() {
    std::printf("DariusCommandInterfaceSlaveNmiGatedByEnableFlag\n");

    NmiProbe nmi;
    DariusCommandInterface ciu(nmi.Handler());

    ciu.Send(0x55);
    Check(nmi.asserts == 0, "nmi gating: Send() before the slave enables NMI does not assert the line");
    Check(!ciu.NmiAssertedForTest(), "nmi gating: NmiAssertedForTest() agrees");

    // Slave enables NMI (mode 6, SlaveCommW's own "NMI enable" case).
    ciu.SlavePortW(0x06);
    ciu.SlaveCommW(0x00);  // value is irrelevant for mode 6/5, only the mode matters

    ciu.Send(0xAA);
    Check(nmi.asserts == 1 && nmi.last, "nmi gating: once enabled, Send() asserts NMI");
}

void DariusCommandInterfaceSendDeliversByteViaTwoNibbles() {
    std::printf("DariusCommandInterfaceSendDeliversByteViaTwoNibbles\n");

    // Nibble order: Send() writes the LOW nibble first, then the HIGH nibble
    // (confirmed against a real-MAME trace of Darius's NMI handler, which
    // reconstructs the byte as (second_read << 4) | first_read).
    DariusCommandInterface ciu;
    ciu.Send(0xA5);

    ciu.SlavePortW(0x00);
    const std::uint8_t lowNibble = ciu.SlaveCommR();
    const std::uint8_t highNibble = ciu.SlaveCommR();
    const std::uint8_t reconstructed = static_cast<std::uint8_t>((highNibble << 4) | lowNibble);

    Check(lowNibble == 0x05, "two-nibble delivery: first SlaveCommR() returns the low nibble");
    Check(highNibble == 0x0A, "two-nibble delivery: second SlaveCommR() returns the high nibble");
    Check(reconstructed == 0xA5, "two-nibble delivery: the two nibbles reconstruct the original byte");
}

void DariusCommandInterfaceSlaveReadClearsStatusAndNmi() {
    std::printf("DariusCommandInterfaceSlaveReadClearsStatusAndNmi\n");

    NmiProbe nmi;
    DariusCommandInterface ciu(nmi.Handler());
    ciu.SlavePortW(0x06);
    ciu.SlaveCommW(0x00);  // enable NMI

    ciu.Send(0x3C);
    Check((ciu.StatusForTest() & 0x01) != 0, "status clear: PORT01_FULL set right after Send()");
    Check(ciu.NmiAssertedForTest(), "status clear: NMI asserted right after Send()");

    ciu.SlavePortW(0x00);
    ciu.SlaveCommR();  // first read (mode 0->1): status/NMI untouched yet
    Check((ciu.StatusForTest() & 0x01) != 0, "status clear: PORT01_FULL still set after only the first read");
    Check(ciu.NmiAssertedForTest(), "status clear: NMI still asserted after only the first read");

    ciu.SlaveCommR();  // second read (mode 1->2): this is the one that clears things
    Check((ciu.StatusForTest() & 0x01) == 0, "status clear: PORT01_FULL cleared after the second (mode-1) read");
    Check(!ciu.NmiAssertedForTest() && nmi.clears == 1,
          "status clear: NMI line deasserted exactly once after the second read");
}
