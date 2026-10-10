// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Portions derived from MAME (src/mame/sega/segaorun.cpp); see THIRD-PARTY-NOTICES.md.
#pragma once

#include "clock.h"
#include "device_manager.h"
#include "devices/cpu/z80/mame_derived_z80_adapter.h"
#include "devices/sound/segapcm/segapcm_device_adapter.h"
#include "devices/sound/ym2151/ym2151_device_adapter.h"
#include "chipestra_abi.h"
#include "outrun_command_interface.h"
#include "outrun_memory_map.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

// The SEGA Out Run sound subsystem: the sound Z80, OutRunMemoryMap,
// OutRunCommandInterface, the YM2151 and the SegaPCM. The Z80, YM2151 and
// SegaPCM adapters are shared with XBoardMachine unchanged. The main and video
// CPUs are not emulated.
//
// LogicalChannels: 8 YM2151 FM channels (ids 0-7) then 16 SegaPCM voices
// (ids 8-23). Each device emits its own native rate (62500Hz / 31250Hz);
// ResamplingMixerFeed brings both up to the canonical mix rate.
//
// Interrupt lines:
//   NMI = command latch (OutRunCommandInterface); the circuit behaves like
//     X-Board's although the physical latch chip differs.
//   IRQ = not wired to anything on Out Run (unlike X-Board, which connects the
//     YM2151's irq_handler() to the Z80 IRQ line), so the Ym2151DeviceAdapter
//     gets a no-op IRQ callback.
class OutRunMachine final : public IMachine {
public:
    // Z80 and YM2151 clock: 16MHz XTAL/4 = 4MHz. Render() converts its
    // EmuDuration (canonical 192kHz output frames) to this rate via
    // clock_converter_; the same cycle count drives the YM2151.
    static constexpr ClockRate kZ80ClockRate{4'000'000};

    // A command can arrive before the first sink-bearing Render() call, and the
    // Z80 adapter services a pending NMI before fetching its first instruction,
    // so without a warm-up the command would hit an un-booted driver.
    // Initialize() therefore runs the Z80 for this many canonical frames (audio
    // discarded) before returning.
    static constexpr std::uint64_t kBootWarmupFrames = 256ull * 24;

    // SegaPCM master input (16.000MHz; see SegaPcmDeviceAdapter for why the
    // 16MHz form is used rather than MAME's 16_MHz_XTAL/4 config-time value). It
    // is exactly 4x the Z80/YM2151 clock, so Render() drives every sound device
    // off one Z80-cycle timeline.
    static constexpr ClockRate kSegaPcmMasterClockRate = SegaPcmDeviceAdapter::kMasterClockRate;
    static constexpr std::uint64_t kSegaPcmMasterClocksPerZ80Cycle = 4;

    // The gain values below are copied from MAME (https://github.com/mamedev/mame, tag mame0289):
    //   src/mame/sega/segaorun.cpp
    //   license:BSD-3-Clause
    //   copyright-holders:Aaron Giles
    // See THIRD-PARTY-NOTICES.md.
    // Per-device amp routing gains, copied from MAME's Out Run machine config:
    //   ymsnd.add_route(0/1, "speaker", 0.30)
    //   pcm.add_route  (0/1, "speaker", 0.70)
    // Exactly double X-Board's gains (0.15/0.35), with the same 1:2.33 ratio
    // between the two devices. The per-channel L/R routing fold in
    // UpdateChannelStereoRouting() applies as on X-Board.
    static constexpr float kYm2151RoutingGain = 0.30f;
    static constexpr float kSegaPcmRoutingGain = 0.70f;

    // `z80_rom` is loaded into the sound Z80's ROM region and `pcm_rom` into the
    // SegaPCM sample ROM during Initialize(). Both default to empty (registered
    // but not playable); see AssembleOutRunSoundRoms (outrun_rom_layout.h).
    explicit OutRunMachine(std::vector<std::uint8_t> z80_rom,
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
    // Z80's YM/SegaPCM bus callbacks right before each access, and once more at
    // the end of the block.
    void SyncSoundDevices(std::uint64_t z80CycleMark);

    // Refreshes every LogicalChannel's Pan/RoutingGain from the current
    // YM2151 RL bits and SegaPCM per-voice L/R volumes. Called at the end of
    // each Render() (block-granular).
    void UpdateChannelStereoRouting();

    // Forces every SegaPCM voice silent immediately before a new command's NMI
    // is injected, so a track switch never leaves a previous command's voices
    // (e.g. "wave sound"/"Drift On" ambience) sounding under the new BGM. This is
    // a deliberate player behavior, not hardware behavior. See SendCommand().
    void SilenceAllSegaPcmVoices();

    // Every one of Out Run's commands also arms a few SegaPCM voices in hardware
    // infinite-loop mode (ctrl bit1 clear), a persistent ambience the driver never
    // stops on its own, whereas melodic notes and voice clips are one-shot. Called
    // once per Render() block before the Z80 runs to disable any enabled,
    // infinite-looping voice. See its definition for the signature relied on.
    void SuppressPersistentAmbientLoopVoices();

    // Clears the sound Z80's work RAM that a previous command's dispatch leaves
    // behind (per-voice state tracking tables) and that changes a later command's
    // dispatch branch. SilenceAllSegaPcmVoices() covers the chip registers, not
    // this RAM. Called from SendCommand(); see its definition for the ranges.
    void ClearStaleDriverVoiceTrackingRam();

public:

    // Test-only observation points (not part of IMachine).
    MameDerivedZ80Adapter::RegisterSnapshot Z80Snapshot() const;
    std::uint8_t ReadMemory(std::uint16_t address) const;
    Ym2151DeviceAdapter& Ym2151ForTest();
    MameDerivedZ80Adapter& Z80ForTest() { return *z80_; }
    SegaPcmDeviceAdapter& SegaPcmForTest();

private:
    std::vector<std::uint8_t> z80_rom_;
    std::vector<std::uint8_t> pcm_rom_;
    OutRunMemoryMap memory_map_;
    DeviceManager device_manager_;
    // Concrete type so Z80Snapshot() can call MameDerivedZ80Adapter::Snapshot();
    // non-owning, owned by device_manager_.
    MameDerivedZ80Adapter* z80_ = nullptr;
    // Non-owning, owned by device_manager_. Constructed with a no-op IRQ handler:
    // the YM2151's IRQ pin is not wired on Out Run.
    Ym2151DeviceAdapter* ym2151_ = nullptr;
    // Non-owning, owned by device_manager_. No interrupt line.
    SegaPcmDeviceAdapter* segapcm_ = nullptr;
    ClockConverter clock_converter_;  // canonical ticks -> 4MHz Z80/YM cycles
    // Carry-forward of the tick-to-cycle remainder, as in XBoardMachine: 256
    // canonical ticks per block do not divide evenly into the 6:125 tick:cycle ratio.
    std::uint64_t cumulative_ticks_processed_ = 0;
    std::uint64_t cumulative_cycles_executed_ = 0;
    std::unique_ptr<OutRunCommandInterface> command_interface_;
    // Z80-cycle mark the sound devices have been advanced to within the current Render() call.
    std::uint64_t sound_sync_cursor_ = 0;
    std::vector<LogicalChannel> channels_;
    // Registered with the sink on Render()'s first call. Reused scratch buffers
    // avoid per-block allocation.
    bool channels_registered_ = false;
    std::vector<std::vector<float>> ym_channel_scratch_;
    std::vector<std::vector<float>> pcm_channel_scratch_;
    std::uint8_t ym2151_latched_address_ = 0;
};
