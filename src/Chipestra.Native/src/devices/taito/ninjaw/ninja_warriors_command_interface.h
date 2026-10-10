// license:BSD-3-Clause
// copyright-holders:Philip Bennett
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/mame/shared/taitosnd.cpp
//   src/mame/shared/taitosnd.h
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#pragma once

#include <cstdint>
#include <functional>

// MAME-derived reimplementation of the Taito TC0140SYT command latch
// (taitosnd.cpp, BSD-3-Clause). Protocol-identical to DariusCommandInterface
// (Darius's pc060ha_device is a subclass with no functional override); the
// implementation is duplicated per Machine rather than shared.
//
// The SLAVE side (slave_port_w/slave_comm_r/slave_comm_w) is driven by
// instructions the emulated audiocpu Z80 executes from its own ROM (mapped at
// 0xE200/0xE201), so the full master/slave nibble state machine is reproduced.
//
// Protocol: a "mode" register (0-4) selects which of 4 nibble slots
// (slavedata[]/masterdata[]) a subsequent comm read/write targets,
// auto-incrementing after each access. Only 2-nibble sequences (mode 0 then
// mode 1) form a real message here. Mode 4 is reset control, not data.
class NinjaWarriorsCommandInterface {
public:
    // Fires when the NMI-worthy status (PORT01_FULL or PORT23_FULL, gated by the
    // slave's NMI-enable state set via SlaveCommW() mode 5/6) changes. Wired to
    // the audiocpu Z80's NMI line.
    using NmiHandler = std::function<void(bool asserted)>;

    // Fires on a master-side mode-4 write's hi->lo transition ("this does a
    // hi-lo transition to reset the sound cpu"). Wired to the audiocpu Z80's
    // RESET line. The SendCommand()/Send() path never drives it.
    using ResetHandler = std::function<void(bool asserted)>;

    explicit NinjaWarriorsCommandInterface(NmiHandler nmiHandler = nullptr, ResetHandler resetHandler = nullptr);

    // Power-on-equivalent reset (mode/status/nmi_enabled/data zeroed). Does not
    // invoke either handler.
    void Reset();

    // --- MASTER side (the main 68000 is not emulated; simulated by Send()) ---
    void MasterPortW(std::uint8_t data);
    void MasterCommW(std::uint8_t data);
    std::uint8_t MasterCommR();

    // --- SLAVE side (wired into the audiocpu memory map at 0xE200/0xE201) ---
    void SlavePortW(std::uint8_t data);
    std::uint8_t SlaveCommR();
    void SlaveCommW(std::uint8_t data);

    // Delivers one byte as a message through the real mode-0/mode-1 two-nibble
    // sequence, LOW nibble first, then HIGH. The order is a convention of this
    // game's audiocpu ROM (it rebuilds the byte as (second_nibble << 4) |
    // first_nibble), not something the generic command-latch code specifies.
    void Send(std::uint8_t value);

    // Test-only observation points.
    std::uint8_t StatusForTest() const { return status_; }
    bool NmiAssertedForTest() const { return nmi_asserted_; }

private:
    void UpdateNmi();

    NmiHandler nmi_handler_;
    ResetHandler reset_handler_;

    std::uint8_t slave_data_[4] = {};   // master->slave (4 nibbles)
    std::uint8_t master_data_[4] = {};  // slave->master (4 nibbles)
    std::uint8_t main_mode_ = 0;        // master-side mode register
    std::uint8_t sub_mode_ = 0;         // slave-side mode register
    std::uint8_t status_ = 0;
    bool nmi_enabled_ = false;  // slave has enabled NMI (SlaveCommW mode 6)
    bool nmi_asserted_ = false;

    // Status bits - matches taitosnd.cpp's own TC0140SYT_PORT01_FULL(0x01) /
    // PORT23_FULL(0x02) / PORT01_FULL_MASTER(0x04) / PORT23_FULL_MASTER(0x08).
    static constexpr std::uint8_t kPort01Full = 0x01;
    static constexpr std::uint8_t kPort23Full = 0x02;
    static constexpr std::uint8_t kPort01FullMaster = 0x04;
    static constexpr std::uint8_t kPort23FullMaster = 0x08;
};
