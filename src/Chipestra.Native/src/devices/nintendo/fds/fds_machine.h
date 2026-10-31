// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "chipestra_abi.h"
#include "clock.h"
#include "devices/cpu/m6502/mame_derived_rp2a03_adapter.h"
#include "devices/nintendo/nes/nes_driver_code.h"
#include "devices/nintendo/nes/nes_driver_config.h"
#include "devices/sound/nes_apu/nes_apu_device_adapter.h"
#include "devices/sound/rp2c33/rp2c33_sound_device_adapter.h"
#include "fds_disk.h"
#include "resource_resolver.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

// A Famicom with the Disk System RAM adapter, as far as the sound goes: the 2A03 (CPU + APU), 2 KB of RAM, the
// adapter's 32 KB of RAM at $6000-$DFFF and the adapter's wavetable sound chip (Rp2c33SoundDeviceAdapter). Neither
// the BIOS nor the game program runs: the files of the disk image the sound driver needs are copied to the
// addresses their headers give, and the driver's entry point is called once per frame, as NesMachine does for a
// cartridge (generated code at $4400/$4500, the reset and NMI vectors redirected there, a frame's NMI raised by
// this Machine). Everything the BIOS would have done for the driver is done here: the sound registers are enabled
// ($4023 bit 1) and the files are in place.
//
// Files: the game's <driver> element names the disk files the driver uses (by name and CRC-32, so a disk whose
// save data differs still matches) and groups them: the base files are always loaded, a track may add a <set>
// of files on top (later files overwrite earlier ones where they overlap). A command's auxiliary byte selects the
// set and the request address (NesDriverConfig::PackSelector); the command value is written to that address.
//
// Switching: like NesMachine, a command sent after another command (or needing a different set) first resets the
// whole Machine - the files are loaded again - so nothing of the previous song stays. A Raw command with no
// request address only loads the set (Music Stop is one).
//
// The picture chip ($2000-$3FFF), the controllers ($4016/$4017), the disk registers ($4020-$4033) and the BIOS area
// ($E000-$FFFF) are stand-ins that read 0; the accesses the sound routines make to them are counted (BusStats) so a
// test can show the driver does not depend on them. The reset and NMI vectors are the generated code's.
//
// Sound: the APU's five channels (as in NesMachine) and the wavetable chip, mixed as the Famicom does: 0.90 x APU
// + 0.2 x disk adapter on one mono speaker, i.e. RoutingGain 1.8 and 0.4 (the Mixer's 2 for a centred channel is
// included). Each channel goes through a DC-blocking filter. A muted APU channel is removed from the mix by
// calculating the mixer again without it; the wavetable chip is a channel of its own.
//
// LogicalChannels: Pulse 1, Pulse 2, Triangle, Noise, DMC (ids 0-4, device "RP2A03"), Wave (id 5, device "RP2C33").
class FdsMachine final : public IMachine {
public:
    static constexpr std::uint64_t kCpuClockHz = 1'789'772;
    static constexpr std::uint32_t kApuSampleRateHz = static_cast<std::uint32_t>(kCpuClockHz / NesApuDeviceAdapter::kCpuCyclesPerSample);
    static constexpr std::uint32_t kFdsSampleRateHz = static_cast<std::uint32_t>(kCpuClockHz);  // one sample per CPU cycle
    static constexpr std::uint32_t kCyclesPerTwoFrames = 59561;  // a frame is 29780.5 cycles
    static constexpr std::uint16_t kStartCodeAddress = 0x4400;
    static constexpr std::uint16_t kFrameCodeAddress = 0x4500;
    static constexpr float kApuRoutingGain = 1.8f;
    static constexpr float kFdsRoutingGain = 0.4f;
    static constexpr std::uint32_t kFdsChannelId = 5;
    static constexpr std::uint16_t kRamAdapterFirst = 0x6000;
    static constexpr std::uint16_t kRamAdapterLast = 0xDFFF;

    struct BusStats {
        std::uint64_t ppuReads = 0, ppuWrites = 0;      // $2000-$3FFF and the $4014 OAM DMA register
        std::uint64_t inputReads = 0, inputWrites = 0;  // $4016 / $4017 (the $4017 write also reaches the APU)
        std::uint64_t diskReads = 0, diskWrites = 0;    // $4020-$4033, the disk registers
        std::uint64_t biosReads = 0, biosWrites = 0;    // $E000-$FFF9 (the vectors at $FFFA-$FFFD are the generated code's)
    };

    FdsMachine(FdsDisk disk, NesDriverConfig driver);

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
    std::uint8_t AdapterRamForTest(std::uint16_t address) const { return adapter_ram_[address - kRamAdapterFirst]; }
    const Rp2c33SoundDeviceAdapter& FdsChipForTest() const { return *fds_; }
    std::uint32_t LoadedSetForTest() const { return loaded_set_; }
    std::uint64_t FramesStartedForTest() const { return frames_started_; }
    // Called for every write to an APU register ($4000-$4013, $4015) or a disk-adapter sound register ($4040-$408A),
    // not during the start-up frames, with the frame it happens in (0 = the first frame after start-up or after the
    // last reset), the CPU address and the value.
    using RegisterWriteObserver = std::function<void(std::uint64_t frame, std::uint16_t address, std::uint8_t value)>;
    void SetRegisterWriteObserverForTest(RegisterWriteObserver observer) { register_write_observer_ = std::move(observer); }

private:
    void ColdStart(std::uint32_t setNumber);
    void LoadFiles(std::uint32_t setNumber);
    std::uint8_t ReadBus(std::uint16_t address);
    void WriteBus(std::uint16_t address, std::uint8_t value);
    std::uint64_t Now() const { return cpu_cycles_done_ + cpu_->CyclesConsumedThisCall(); }
    void SyncApu(std::uint64_t cpuCycle);
    void SyncFds(std::uint64_t cpuCycle);
    void RunCpuTo(std::uint64_t targetCycle, bool frames);
    std::uint8_t MutedMask() const;
    void WriteChannels(LogicalChannelSink* sink);
    const FdsFile* FindFile(const NesDriverConfig::DiskFile& wanted) const;

    FdsDisk disk_;
    NesDriverConfig driver_;
    std::vector<std::uint8_t> start_code_;
    std::vector<std::uint8_t> frame_code_;

    std::array<std::uint8_t, 0x800> ram_{};
    std::array<std::uint8_t, kRamAdapterLast - kRamAdapterFirst + 1> adapter_ram_{};
    std::unique_ptr<MameDerivedRp2a03Adapter> cpu_;
    std::unique_ptr<NesApuDeviceAdapter> apu_;
    std::unique_ptr<Rp2c33SoundDeviceAdapter> fds_;
    ClockConverter clock_converter_;

    std::vector<LogicalChannel> channels_;
    bool channels_registered_ = false;
    std::array<std::vector<float>, NesApuDeviceAdapter::kChannelCount> pending_;  // APU output not yet sent
    std::vector<float> pending_fds_;

    std::uint32_t loaded_set_ = 0;
    std::uint64_t cpu_cycles_done_ = 0;
    std::uint64_t apu_cycles_ = 0;
    std::uint64_t fds_cycles_ = 0;
    std::uint64_t origin_cycles_ = 0;
    std::uint64_t cumulative_ticks_ = 0;
    std::uint64_t frames_started_ = 0;
    std::uint64_t last_frame_start_cycle_ = 0;
    bool command_sent_since_reset_ = false;
    bool starting_up_ = false;
    RegisterWriteObserver register_write_observer_;
    BusStats bus_stats_;

    std::array<DcBlocker, NesApuDeviceAdapter::kChannelCount> dc_block_{};
    DcBlocker fds_dc_block_;
};

// Everything an FdsMachine is built from.
struct FdsMachineInputs {
    FdsDisk disk;
    NesDriverConfig driver;
};

// Logical resource names: the whole disk image (.fds, with or without the 16-byte header) and the text of the
// game's <driver> element (supplied by Managed Core from the game definition XML).
inline constexpr const char* kFdsDiskResource = "disk";
inline constexpr const char* kFdsDriverResource = "driver_xml";

// Maps resolved resources onto a disk and driver settings. An entirely empty `resources` yields a
// registered-but-not-playable set (no files, no routines). Once any resource is supplied both are required, every
// <file> of the driver must be found on the disk by name and (when given) CRC-32, and every file must fit in the
// adapter's RAM ($6000-$DFFF): a problem throws std::runtime_error naming it.
FdsMachineInputs AssembleFdsMachineInputs(const MachineResources& resources);
