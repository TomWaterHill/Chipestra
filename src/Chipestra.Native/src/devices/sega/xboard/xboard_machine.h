// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Portions derived from MAME (src/mame/sega/segaxbd.cpp); see THIRD-PARTY-NOTICES.md.
#pragma once

#include "clock.h"
#include "device_manager.h"
#include "devices/cpu/z80/mame_derived_z80_adapter.h"
#include "devices/sound/segapcm/segapcm_device_adapter.h"
#include "devices/sound/ym2151/ym2151_device_adapter.h"
#include "chipestra_abi.h"
#include "xboard_command_interface.h"
#include "xboard_memory_map.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

// The SEGA X-Board sound subsystem: the sound Z80, XBoardMemoryMap,
// XBoardCommandInterface, the YM2151 and the SegaPCM, wired together as on the
// board. The main and video CPUs are not emulated.
//
// LogicalChannels: 8 YM2151 FM channels (ids 0-7) then 16 SegaPCM voices
// (ids 8-23). Each device emits its own native rate (62500Hz / 31250Hz);
// ResamplingMixerFeed brings both up to the canonical mix rate.
//
// Interrupt lines: NMI = command latch (XBoardCommandInterface); IRQ = YM2151
// Timer A/B. They are independent Z80 pins. SegaPCM has no interrupt line.
class XBoardMachine final : public IMachine {
public:
    // Z80 and YM2151 clock: SOUND_CLOCK/4 = 16MHz/4 = 4MHz. Render() converts its
    // EmuDuration (canonical 192kHz output frames) to this rate via
    // clock_converter_; the same cycle count drives the YM2151.
    static constexpr ClockRate kZ80ClockRate{4'000'000};

    // A command can arrive before the first sink-bearing Render() call. The Z80
    // adapter services a pending NMI before fetching its first instruction, so
    // a command sent right after Initialize() would jump into the NMI handler
    // with SP/PC still at their reset values, skipping the driver's boot
    // sequence and corrupting RAM. Initialize() therefore runs the Z80 for this
    // many canonical frames (audio discarded) so the driver is already waiting
    // for a command.
    static constexpr std::uint64_t kBootWarmupFrames = 256ull * 24;

    // SegaPCM master input (16.000MHz; see SegaPcmDeviceAdapter for why the
    // 16MHz form is used rather than MAME's SOUND_CLOCK/4). It is exactly 4x the
    // Z80/YM2151 clock, so Render() drives every sound device off one Z80-cycle
    // timeline.
    static constexpr ClockRate kSegaPcmMasterClockRate = SegaPcmDeviceAdapter::kMasterClockRate;
    static constexpr std::uint64_t kSegaPcmMasterClocksPerZ80Cycle = 4;

    // The gain values below are copied from MAME (https://github.com/mamedev/mame, tag mame0289):
    //   src/mame/sega/segaxbd.cpp
    //   license:BSD-3-Clause
    //   copyright-holders:Aaron Giles
    // See THIRD-PARTY-NOTICES.md.
    // Per-device amp routing gains, copied from MAME's X-Board machine config:
    //   ymsnd.add_route(0/1, "speaker", 0.15)
    //   pcm.add_route  (0/1, "speaker", 0.35)
    // i.e. on the real board SegaPCM is mixed ~2.33x (+7.4dB) above the YM2151.
    // UpdateChannelStereoRouting() folds each device's per-channel L/R routing
    // into LogicalChannel::Pan and multiplies these gains by a per-channel factor
    // (bus count for the YM2151, (lvol+rvol)/256 for a SegaPCM voice), so the
    // absolute level matches MAME's `waveform/32768 * route` per side.
    static constexpr float kYm2151RoutingGain = 0.15f;
    static constexpr float kSegaPcmRoutingGain = 0.35f;

    // `z80_rom` is loaded into the sound Z80's ROM region and `pcm_rom` into the
    // SegaPCM sample ROM during Initialize(). Both default to empty (registered
    // but not playable); see AssembleXBoardSoundRoms (xboard_rom_layout.h).
    explicit XBoardMachine(std::vector<std::uint8_t> z80_rom,
                           std::vector<std::uint8_t> pcm_rom = {});

    void Initialize() override;
    void Reset() override;
    void Shutdown() override;
    std::vector<LogicalChannel>& LogicalChannels() override;
    void Render(EmuDuration duration, LogicalChannelSink* sink) override;
    void SendCommand(const MusicCommand& command) override;

private:
    // Advances the YM2151 and SegaPCM up to the given mark on the current
    // Render()'s Z80-cycle timeline (0 at the top of Render()). Called from the
    // Z80's YM/SegaPCM bus callbacks right before each access, so busy-polling
    // drivers see device state as of that exact Z80 cycle, and once more at the
    // end of the block.
    void SyncSoundDevices(std::uint64_t z80CycleMark);

    // Refreshes every LogicalChannel's Pan/RoutingGain from the current
    // YM2151 RL bits and SegaPCM per-voice L/R volumes. Called at the end of
    // each Render() (block-granular).
    void UpdateChannelStereoRouting();

public:

    // Test-only observation points (not part of IMachine).
    MameDerivedZ80Adapter::RegisterSnapshot Z80Snapshot() const;
    std::uint8_t ReadMemory(std::uint16_t address) const;
    // Lets a test program the YM2151's timers directly and observe the IRQ path.
    Ym2151DeviceAdapter& Ym2151ForTest();
    MameDerivedZ80Adapter& Z80ForTest() { return *z80_; }
    // Lets a test program SegaPCM voice registers directly.
    SegaPcmDeviceAdapter& SegaPcmForTest();

    // Lets a test compute the long-run Z80 clock error
    // (cumulative cycles / TicksToCycles(cumulative ticks) should be ~1.0).
    std::uint64_t CumulativeTicksProcessedForTest() const { return cumulative_ticks_processed_; }
    std::uint64_t CumulativeCyclesExecutedForTest() const { return cumulative_cycles_executed_; }

    // Sound-bus activity counters (diagnostics).
    struct BusActivity {
        std::uint64_t Ym2151Writes = 0;
        std::uint64_t Ym2151Reads = 0;
        std::uint64_t SegaPcmWrites = 0;
        std::uint64_t SegaPcmReads = 0;
        std::uint64_t CommandLatchReads = 0;
    };
    BusActivity BusActivityForTest() const { return bus_activity_; }

    // Called on every YM2151 data write with the latched register address and
    // the byte written. Null by default.
    using Ym2151WriteObserver = std::function<void(std::uint8_t address, std::uint8_t data)>;
    void SetYm2151WriteObserverForTest(Ym2151WriteObserver observer) {
        ym2151_write_observer_ = std::move(observer);
    }

private:
    std::vector<std::uint8_t> z80_rom_;
    std::vector<std::uint8_t> pcm_rom_;
    XBoardMemoryMap memory_map_;
    DeviceManager device_manager_;
    // Concrete type so Z80Snapshot() can call MameDerivedZ80Adapter::Snapshot();
    // non-owning, owned by device_manager_.
    MameDerivedZ80Adapter* z80_ = nullptr;
    // Non-owning, owned by device_manager_. Its IRQ handler drives the Z80's IRQ line.
    Ym2151DeviceAdapter* ym2151_ = nullptr;
    // Non-owning, owned by device_manager_. No interrupt line.
    SegaPcmDeviceAdapter* segapcm_ = nullptr;
    ClockConverter clock_converter_;  // canonical ticks -> 4MHz Z80/YM cycles
    // ClockConverter is stateless, and 256 canonical ticks per block do not divide
    // evenly into this board's 6:125 tick:cycle ratio (the remainder would be
    // dropped on every block, running the Z80 a constant 0.00625% slow). These
    // cumulative counters (ticks processed vs. Z80 cycles executed since
    // Initialize()/Reset()) let each block's cycle budget close the previous
    // block's gap, bounding the long-run error to under one Z80 cycle.
    std::uint64_t cumulative_ticks_processed_ = 0;
    std::uint64_t cumulative_cycles_executed_ = 0;
    std::unique_ptr<XBoardCommandInterface> command_interface_;
    // Z80-cycle mark the sound devices have been advanced to within the current Render() call.
    std::uint64_t sound_sync_cursor_ = 0;
    std::vector<LogicalChannel> channels_;
    // Registered with the sink on Render()'s first call. Reused scratch buffers
    // avoid per-block allocation.
    bool channels_registered_ = false;
    std::vector<std::vector<float>> ym_channel_scratch_;
    std::vector<std::vector<float>> pcm_channel_scratch_;
    BusActivity bus_activity_;
    std::uint8_t ym2151_latched_address_ = 0;
    Ym2151WriteObserver ym2151_write_observer_;
};
