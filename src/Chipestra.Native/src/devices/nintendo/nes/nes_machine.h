// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "chipestra_abi.h"
#include "clock.h"
#include "devices/cpu/m6502/mame_derived_rp2a03_adapter.h"
#include "devices/sound/namco_163/namco_163_sound_device_adapter.h"
#include "devices/sound/nes_apu/nes_apu_device_adapter.h"
#include "devices/sound/sunsoft_5b/sunsoft_5b_sound_device_adapter.h"
#include "devices/sound/vrc6/vrc6_sound_device_adapter.h"
#include "devices/sound/vrc7/vrc7_sound_device_adapter.h"
#include "nes_cartridge.h"
#include "nes_driver_code.h"
#include "nes_driver_config.h"
#include "nes_fme7.h"
#include "nes_mmc3.h"
#include "nes_namco163.h"
#include "nes_vrc6.h"
#include "nes_vrc7.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

// A Famicom/NES cartridge's sound: the 2A03 (CPU + APU), 2 KB of RAM and the cartridge's program ROM. The
// console's picture chip and controllers are not emulated; the sound driver is the game's own routines, which
// this Machine calls itself (the game's reset and NMI code and its main loop never run):
//
//   - start-up: RAM is filled with 0xFF, the driver's ram_clear range is set to 0, then the driver's init
//     routines are called one after another;
//   - one frame of the frame routines then runs with no song requested, so the driver has made its own first
//     step (a game always runs its driver for a while before a song starts) and is in the state a request finds;
//   - every frame (NTSC: 29780.5 CPU cycles), at the start of the frame, the driver's frame routines are
//     called one after another; when they return the CPU waits for the next frame;
//   - a song or effect is started by writing its number to the driver's bgm or sfx variable (SendCommand).
//
// The routines are called from a few bytes of generated code in the cartridge-expansion area ($4400-$46FF, where
// no supported mapper has a register: the Namco 163 starts at $4800, the others at $5000 or later, and the Disk System
// ends at $40FF): the reset vector and the NMI vector are redirected there, the game ROM is
// never changed. A frame's NMI is raised by this Machine, the generated NMI code does `JSR routine` for each
// frame routine and returns, and the CPU then spins in a one-instruction loop.
//
// MMC3 cartridges (mapper 4, e.g. Super Mario Bros. 3): the program ROM is banked (Mmc3Prg) and 8 KB of WRAM sits at
// $6000-$7FFF. The driver's prg_banks_call / prg_banks_idle give the two bank states the game itself has: the
// generated code maps the "call" banks, calls the routines with X = 0 (a few of the game's effects use the X it
// was interrupted with; 0 is the value seen most often in the real game) and maps the "idle" banks again, which
// are the ones the DMC reads its samples through. The APU is brought up to the CPU's cycle before every write to
// the mapper, so every DMC byte is read through the banks that were mapped at that point, as on the real chip.
//
// VRC6 cartridges (mapper 24 or 26, e.g. Akumajou Densetsu): the program ROM is a 16 KB bank at $8000 and an 8 KB
// bank at $C000 (Vrc6Prg) and the cartridge's three extra sound channels (Vrc6SoundDeviceAdapter) are written
// through $9000-$B002. prg_banks_call / prg_banks_idle are the values of the two bank registers; the generated
// code writes them around the routines in the same way. The sound chip is brought up to the CPU's cycle before
// every write to the cartridge.
//
// VRC7 cartridges (mapper 85, e.g. Lagrange Point): the program ROM is three 8 KB banks and the fixed last bank
// (Vrc7Prg; the address line that tells the registers apart comes from the game definition), 8 KB of WRAM sits at
// $6000-$7FFF (always usable, cleared at every reset), and the six FM channels (Vrc7SoundDeviceAdapter) are written
// through the sound address and data registers. The chip's built-in instrument table is a resource the user
// supplies. prg_banks_call / prg_banks_idle are three bank numbers. The chip runs at twice the CPU clock and is
// brought up to the CPU's cycle before every write to the cartridge.
//
// Namco 163 cartridges (mapper 19, e.g. Rolling Thunder): the program ROM is three 8 KB banks and the fixed last
// bank (Namco163Prg), 8 KB of WRAM sits at $6000-$7FFF, and the chip's wavetable channels
// (Namco163SoundDeviceAdapter) are written through the sound address register ($F800) and the data port
// ($4800-$4FFF, which also reads). prg_banks_call / prg_banks_idle are three bank numbers, written to the
// registers at $E000, $E800 and $F000; X and Y are 0 when a routine is called. The game definition gives the
// number of channels the meters show (n163_channels: the last ones, the ones that sound when the game uses
// that many) and the MAME submapper (n163_volume) whose sound gain is used; neither is guessed from the file.
// The chip runs at the CPU clock (one sample every 15 cycles) and is brought up to the CPU cycle before every
// access to it.
//
// FME-7 / Sunsoft 5B cartridges (mapper 69, e.g. Gimmick!): the program ROM is three 8 KB banks (commands 9, A and B
// of the bank registers) and the fixed last bank (Fme7Prg), 8 KB of WRAM sits at $6000-$7FFF (always usable, cleared at
// every reset), and the three tone channels of the 5B (Sunsoft5bSoundDeviceAdapter) are written through the
// register select ($C000-$DFFF) and the data ($E000-$FFFF). prg_banks_call / prg_banks_idle are two bank numbers,
// for the commands 9 and A; command B (the bank at $C000, where the DMC reads) starts at the value the game has
// left in it when its sound driver runs (kFme7InitialBankB) and is never written by the generated code, since the
// driver chooses it. X and Y are 0 when a routine is called. The chip is stepped to the same sample count as the
// original's stream (RenderToSampleCount: 111860 Hz against the CPU clock) and is brought up to the CPU cycle
// before every write to the cartridge.
//
// frame_counter: a RAM byte the generated frame code adds 1 to at the end of every frame.
//
// request_call: a driver that takes a request as a call with the number in A (not through RAM variables). A
// request is made in the first frame after the start-up: the generated frame code calls the frame routines, and
// then the request routine with the number in A, in the same frame, as the game itself does. A track that is not a
// control track makes its request whatever its number (0 too); a control track (<track type="control">, whose
// Auxiliary byte has NesDriverConfig::kControlFlag set) makes none and only resets the Machine - for a driver that
// takes its requests through RAM variables as well. request_register ("A", "X" or "Y") names the register the number
// is passed in, and sfx_call a second routine that effects are requested from (the same way).
//
// Switching: like Namco System 1 in this project, a command sent after another command first resets the whole
// Machine (so the previous song cannot leak into the next), then starts the new one; the result is bit for bit
// what a freshly initialized Machine gives for that command.
//
// The picture chip ($2000-$3FFF) and the controllers ($4016/$4017) are minimal stand-ins; the number of
// accesses the sound routines make to them is counted (BusStats) so a test can show they do not depend on them.
//
// Sound: the APU's five channels (NesApuDeviceAdapter, whose outputs add up to the original's non-linear
// mix) go through a DC-blocking filter and a RoutingGain of 1.8 (= MAME's 0.90 route gain into a mono speaker,
// times the Mixer's 2 for a centred channel). The VRC6's three channels go through the same kind of filter and a
// RoutingGain of 2.0 (= MAME's route gain of 1.0 for the cartridge's sound, times 2). The VRC7's six channels likewise, with a RoutingGain of 2.0, the 5B's three with 1.0 (MAME's 0.50). A muted channel is removed from the mix by calculating the mixer
// again without it (NesApuDeviceAdapter::SetMutedChannels).
//
// LogicalChannels: Pulse 1, Pulse 2, Triangle, Noise, DMC (ids 0-4), device name "RP2A03"; on a VRC6 cartridge also
// Pulse 1, Pulse 2, Saw (ids 5-7), device name "VRC6"; on a VRC7 cartridge FM1-FM6 (ids 5-10), device name "VRC7"; on a Namco 163 cartridge Wave (slot number + 1) for the last n163_channels slots (ids
// 5 up), device name "N163"; on an FME-7 cartridge Tone 1-3 (ids 5-7), device name "5B".
class NesMachine final : public IMachine {
public:
    // The CPU clock is MAME's whole-Hz value of 21'477'272 / 12, which makes the APU stream exactly 447443 Hz.
    static constexpr std::uint64_t kCpuClockHz = 1'789'772;
    static constexpr std::uint32_t kApuSampleRateHz = static_cast<std::uint32_t>(kCpuClockHz / NesApuDeviceAdapter::kCpuCyclesPerSample);
    static constexpr std::uint32_t kCyclesPerTwoFrames = 59561;  // a frame is 29780.5 cycles
    static constexpr std::uint16_t kStartCodeAddress = 0x4400;
    static constexpr std::uint16_t kFrameCodeAddress = 0x4500;
    static constexpr float kRoutingGain = 1.8f;
    static constexpr float kVrc6RoutingGain = 2.0f;
    static constexpr float kVrc7RoutingGain = 2.0f;
    // MAME's route gain of 0.50 for the 5B against 0.90 for the 2A03, times the Mixer's 2 for a centred channel.
    static constexpr float kSunsoft5bRoutingGain = 1.0f;
    // The value of command B (the bank at $C000) when the game's sound driver starts to run: the value the real game has
    // when its sound update is entered; the driver sets the bank it needs for the DMC itself.
    static constexpr std::uint8_t kFme7InitialBankB = 0;
    // MAME route gain of 0.5 into the mono speaker times the Mixer 2 for a centred channel, times the gain of the
    // cartridge submapper (0 and 1: not set, 1.0; 2: no expansion sound; 3: 1.125; 4: 1.17; 5: 1.19).
    static float N163RoutingGain(unsigned submapper);

    struct BusStats {
        std::uint64_t ppuReads = 0, ppuWrites = 0;      // $2000-$3FFF and the $4014 OAM DMA register
        std::uint64_t inputReads = 0, inputWrites = 0;  // $4016 / $4017 (the $4017 write also reaches the APU)
    };

    // `vrc7Instruments` is the 144-byte built-in instrument table, required for a VRC7 cartridge
    // (std::invalid_argument without it) and ignored for any other.
    NesMachine(NesCartridge cartridge, NesDriverConfig driver, std::vector<std::uint8_t> vrc7Instruments = {});

    void Initialize() override;
    void Reset() override;
    void Shutdown() override;
    std::vector<LogicalChannel>& LogicalChannels() override { return channels_; }
    void Render(EmuDuration duration, LogicalChannelSink* sink) override;
    void SendCommand(const MusicCommand& command) override;
    bool AcceptsCommand(const MusicCommand& command) const override;

    // Test-only observation points - not part of production playback.
    const BusStats& BusStatsForTest() const { return bus_stats_; }
    MameDerivedRp2a03Adapter::RegisterSnapshot CpuSnapshotForTest() const { return cpu_->Snapshot(); }
    std::uint8_t RamForTest(std::uint16_t address) const { return ram_[address & 0x7FF]; }
    void WriteRamForTest(std::uint16_t address, std::uint8_t value) { ram_[address & 0x7FF] = value; }
    std::uint64_t FramesStartedForTest() const { return frames_started_; }
    const NesApuDeviceAdapter& ApuForTest() const { return *apu_; }
    // Called for every write to an APU register (not during the start-up frame) with the frame it happens in
    // (0 = the first frame after start-up or after the last reset), the register offset from $4000 and the value.
    using ApuWriteObserver = std::function<void(std::uint64_t frame, std::uint8_t reg, std::uint8_t value)>;
    void SetApuWriteObserverForTest(ApuWriteObserver observer) { apu_write_observer_ = std::move(observer); }
    // Called for every byte the DMC reads (frame as above, CPU address, the byte).
    using DmcReadObserver = std::function<void(std::uint64_t frame, std::uint16_t address, std::uint8_t value)>;
    void SetDmcReadObserverForTest(DmcReadObserver observer) { dmc_read_observer_ = std::move(observer); }
    // Called for every write to a VRC6 sound register ($9000-$9003, $A000-$A002, $B000-$B002; not during start-up)
    // with the frame (as above), the CPU address and the value.
    using Vrc6WriteObserver = std::function<void(std::uint64_t frame, std::uint16_t address, std::uint8_t value)>;
    void SetVrc6WriteObserverForTest(Vrc6WriteObserver observer) { vrc6_write_observer_ = std::move(observer); }
    const Vrc6Prg& Vrc6PrgForTest() const { return vrc6_prg_; }
    const Vrc6SoundDeviceAdapter* Vrc6ForTest() const { return vrc6_.get(); }
    // Called for every write to the VRC7 sound registers (not during start-up) with the frame (as above),
    // whether it is the data register, and the value.
    using Vrc7WriteObserver = std::function<void(std::uint64_t frame, bool data, std::uint8_t value)>;
    void SetVrc7WriteObserverForTest(Vrc7WriteObserver observer) { vrc7_write_observer_ = std::move(observer); }
    const Vrc7Prg& Vrc7PrgForTest() const { return vrc7_prg_; }
    const Vrc7SoundDeviceAdapter* Vrc7ForTest() const { return vrc7_.get(); }
    // Called for every write to the 5B's register select ($C000, `address`) or data ($E000), not during start-up,
    // with the frame (as above) and the value.
    using Sunsoft5bWriteObserver = std::function<void(std::uint64_t frame, std::uint16_t address, std::uint8_t value)>;
    void SetSunsoft5bWriteObserverForTest(Sunsoft5bWriteObserver observer) { sun5b_write_observer_ = std::move(observer); }
    const Fme7Prg& Fme7PrgForTest() const { return fme7_prg_; }
    const Sunsoft5bSoundDeviceAdapter* Sunsoft5bForTest() const { return sun5b_.get(); }
    // Called for every write to the Namco 163 sound address register ($F800) or data port ($4800), not during
    // start-up, with the frame (as above), the address of the register and the value.
    using N163WriteObserver = std::function<void(std::uint64_t frame, std::uint16_t address, std::uint8_t value)>;
    void SetN163WriteObserverForTest(N163WriteObserver observer) { n163_write_observer_ = std::move(observer); }
    const Namco163Prg& N163PrgForTest() const { return n163_prg_; }
    const Namco163SoundDeviceAdapter* N163ForTest() const { return n163_.get(); }
    std::uint8_t WramForTest(std::uint16_t address) const { return wram_[address & 0x1FFF]; }
    // Offset into the PRG image of the byte the CPU would read at `address` now (MMC3 cartridges).
    std::size_t PrgOffsetForTest(std::uint16_t address) const {
        if (fme7_enabled_) return fme7_prg_.Offset(address);
        if (vrc7_enabled_) return vrc7_prg_.Offset(address);
        if (n163_enabled_) return n163_prg_.Offset(address);
        return vrc6_enabled_ ? vrc6_prg_.Offset(address) : mmc3_.Offset(address);
    }

private:
    void ColdStart();
    void BuildStartCode();
    std::uint8_t PrgRead(std::uint16_t address) const;
    std::uint8_t ReadBus(std::uint16_t address);
    void WriteBus(std::uint16_t address, std::uint8_t value);
    std::uint64_t Now() const { return cpu_cycles_done_ + cpu_->CyclesConsumedThisCall(); }
    void SyncApu(std::uint64_t cpuCycle);
    void RunCpuTo(std::uint64_t targetCycle, bool frames);
    std::uint16_t MutedMask() const;
    void WriteChannels(LogicalChannelSink* sink);

    NesCartridge cartridge_;
    NesDriverConfig driver_;
    std::vector<std::uint8_t> start_code_;  // generated code at kStartCodeAddress
    std::vector<std::uint8_t> frame_code_;  // generated code at kFrameCodeAddress

    std::array<std::uint8_t, 0x800> ram_{};
    std::array<std::uint8_t, 0x2000> wram_{};  // MMC3 and VRC7 cartridges
    bool mmc3_enabled_;
    Mmc3Prg mmc3_;
    bool vrc6_enabled_;
    Vrc6Prg vrc6_prg_;
    std::unique_ptr<Vrc6SoundDeviceAdapter> vrc6_;
    bool vrc7_enabled_;
    Vrc7Prg vrc7_prg_;
    std::vector<std::uint8_t> vrc7_instruments_;
    std::unique_ptr<Vrc7SoundDeviceAdapter> vrc7_;
    bool n163_enabled_;
    Namco163Prg n163_prg_;
    unsigned n163_first_slot_ = 0;  // the first of the sound slots the meters show (the last n163_channels)
    float n163_gain_ = 1.0f;
    std::unique_ptr<Namco163SoundDeviceAdapter> n163_;
    bool fme7_enabled_;
    Fme7Prg fme7_prg_;
    std::unique_ptr<Sunsoft5bSoundDeviceAdapter> sun5b_;
    std::unique_ptr<MameDerivedRp2a03Adapter> cpu_;
    std::unique_ptr<NesApuDeviceAdapter> apu_;
    ClockConverter clock_converter_;

    std::vector<LogicalChannel> channels_;
    bool channels_registered_ = false;
    std::array<std::vector<float>, NesApuDeviceAdapter::kChannelCount> pending_;  // APU output not yet sent
    std::array<std::vector<float>, Vrc6SoundDeviceAdapter::kChannelCount> vrc6_pending_;  // VRC6 output not yet sent
    std::array<std::vector<float>, Vrc7SoundDeviceAdapter::kChannelCount> vrc7_pending_;  // VRC7 output not yet sent
    std::array<std::vector<float>, Namco163SoundDeviceAdapter::kChannelCount> n163_pending_;  // N163 output not yet sent
    std::array<std::vector<float>, Sunsoft5bSoundDeviceAdapter::kChannelCount> sun5b_pending_;  // 5B output not yet sent

    std::uint64_t cpu_cycles_done_ = 0;
    std::uint64_t apu_cycles_ = 0;
    std::uint64_t origin_cycles_ = 0;  // CPU cycle where the first frame starts (after the start-up routines)
    std::uint64_t cumulative_ticks_ = 0;
    std::uint64_t frames_started_ = 0;
    std::uint64_t last_frame_start_cycle_ = 0;
    bool command_sent_since_reset_ = false;
    bool starting_up_ = false;
    ApuWriteObserver apu_write_observer_;
    Vrc6WriteObserver vrc6_write_observer_;
    Vrc7WriteObserver vrc7_write_observer_;
    N163WriteObserver n163_write_observer_;
    Sunsoft5bWriteObserver sun5b_write_observer_;
    std::uint8_t request_value_ = 0;
    std::uint8_t request_pending_ = 0;
    std::uint8_t sfx_value_ = 0;
    std::uint8_t sfx_pending_ = 0;
    DmcReadObserver dmc_read_observer_;
    BusStats bus_stats_;

    std::array<DcBlocker, NesApuDeviceAdapter::kChannelCount> dc_block_{};
    std::array<DcBlocker, Vrc6SoundDeviceAdapter::kChannelCount> vrc6_dc_block_{};
    std::array<DcBlocker, Vrc7SoundDeviceAdapter::kChannelCount> vrc7_dc_block_{};
    std::array<DcBlocker, Namco163SoundDeviceAdapter::kChannelCount> n163_dc_block_{};
    std::array<DcBlocker, Sunsoft5bSoundDeviceAdapter::kChannelCount> sun5b_dc_block_{};
};
