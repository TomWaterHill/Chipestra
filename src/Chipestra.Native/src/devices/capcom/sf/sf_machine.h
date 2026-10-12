// license:BSD-3-Clause
// copyright-holders:Olivier Galibert
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Portions derived from MAME (src/mame/capcom/sf.cpp); see THIRD-PARTY-NOTICES.md.
#pragma once

#include "chipestra_abi.h"
#include "clock.h"
#include "device_manager.h"
#include "devices/capcom/sf/sf_memory_map.h"
#include "devices/cpu/z80/mame_derived_z80_adapter.h"
#include "devices/sound/msm5205/msm5205_device_adapter.h"
#include "devices/sound/ym2151/ym2151_device_adapter.h"

#include <cstdint>
#include <memory>
#include <vector>

// The sound side of the Capcom 68000-based Street Fighter board: two Z80s, a YM2151 and two MSM5205s.
// The 68000 is not emulated; its only job on the sound side is to write the command latch.
//
//   Z80 #1 (music): YM2151 (its IRQ drives the Z80's INT), the command latch, NMI on every latch write.
//   Z80 #2 (samples): both MSM5205s, banked sample ROM, an 8000Hz IRQ, no RAM.
//
// The 68000 writes the latch once per frame (60Hz) and pulses Z80 #1's NMI every time. Idle frames write
// 0xFF. A command takes the place of the idle value in one frame; the next frame is idle again.
//
// Time base: every chip runs from the canonical 192kHz tick count. Both Z80s and the YM2151 share one clock
// (3.579545MHz, converted with a carry-forward converter); the MSM5205s take 384kHz, exactly two input
// clocks per tick. The two Z80s do not talk to each other (only through the latch, which is written
// between instructions), so each runs to the same target independently.
//
// Switching songs (and any "stop") is a full Reset(): this driver has no command that silences both chips.
//
// LogicalChannels: YM2151 FM 1-8 (ids 0-7), MSM5205 #1 (id 8), MSM5205 #2 (id 9).
class SfMachine final : public IMachine {
public:
    static constexpr ClockRate kZ80ClockRate{3'579'545};
    static constexpr ClockRate kYm2151ClockRate{3'579'545};
    static constexpr std::uint64_t kMsm5205ClockHz = 384'000;
    static constexpr std::uint64_t kMsm5205InputClocksPerTick = 2;  // 384kHz / 192kHz

    // The 68000 writes the latch every frame; 60Hz is exactly 3200 canonical ticks.
    static constexpr std::uint64_t kFramePeriodTicks = 3200;
    // Z80 #2's periodic IRQ.
    static constexpr std::uint64_t kSamplesIrqHz = 8000;

    // Time given to the Z80s to boot before the first command can be taken (rounded to render chunks).
    static constexpr std::uint64_t kWarmupChunkTicks = 256;
    static constexpr std::uint64_t kBootWarmupTicks = 256ull * 192;

    // The latch value written when no command is pending.
    static constexpr std::uint8_t kIdleLatchValue = 0xFF;

    // Chip output gain: YM2151 is routed to both speakers at 0.60 (left channel to left, right channel to
    // right); each MSM5205 goes to both speakers at 1.0.
    static constexpr float kYm2151RoutingGain = 0.60f;
    static constexpr float kMsm5205RoutingGain = 1.0f;

    static constexpr std::uint32_t kYm2151Base = 0;
    static constexpr std::uint32_t kMsm5205Base = kYm2151Base + Ym2151DeviceAdapter::kChannelCount;
    static constexpr std::uint32_t kMsm5205Count = 2;
    static constexpr std::uint32_t kChannelCount = kMsm5205Base + kMsm5205Count;

    SfMachine(std::vector<std::uint8_t> music_rom, std::vector<std::uint8_t> sample_image);
    ~SfMachine() override;

    void Initialize() override;
    void Reset() override;
    void Shutdown() override;
    std::vector<LogicalChannel>& LogicalChannels() override;
    void Render(EmuDuration duration, LogicalChannelSink* sink) override;
    void SendCommand(const MusicCommand& command) override;
    bool AcceptsCommand(const MusicCommand& command) const override;
    void OnPanOverrideCleared(std::uint32_t) override {}

    // Test-only observation points.
    MameDerivedZ80Adapter::RegisterSnapshot MusicCpuSnapshot() const { return music_cpu_->Snapshot(); }
    MameDerivedZ80Adapter::RegisterSnapshot SamplesCpuSnapshot() const { return samples_cpu_->Snapshot(); }
    Ym2151DeviceAdapter& Ym2151ForTest() { return *ym2151_; }
    Msm5205DeviceAdapter& Msm5205ForTest(std::uint32_t index) { return *msm_[index]; }
    std::uint8_t LatchValueForTest() const { return latch_; }
    std::uint8_t SampleBankForTest() const { return memory_map_.SampleBank(); }
    std::uint8_t ReadMusicMemoryForTest(std::uint16_t address) const { return memory_map_.ReadMusic(address); }
    void WriteMusicMemoryForTest(std::uint16_t address, std::uint8_t value) { memory_map_.WriteMusic(address, value); }
    std::uint8_t ReadSamplesMemoryForTest(std::uint16_t address) const { return memory_map_.ReadSamples(address); }
    std::uint64_t SamplesIrqCountForTest() const { return samples_irq_count_; }
    std::uint64_t MsmWriteCountForTest() const { return msm_write_count_; }
    std::uint32_t ResetCountForTest() const { return reset_count_; }

private:
    void ResetState();
    void RunWarmup();

    // Runs Z80 #1 and Z80 #2 up to the given absolute canonical tick.
    void RunCpusUpToTicks(std::uint64_t absoluteTicks);
    void RunSamplesCpuUpToCycles(std::uint64_t targetCycles);
    // Advances the YM2151 to the given mark on the current Render() call's Z80 #1 cycle timeline.
    void SyncYm2151(std::uint64_t music_cpu_cycle_mark);
    std::uint64_t CurrentBlockCycles() const { return block_base_cycles_ + music_cpu_->CyclesConsumedThisCall(); }
    // The 68000's per-frame latch write plus the NMI it pulses.
    void DeliverLatchFrame();
    // Current chip-clock time of Z80 #2 inside its running Execute() call, in MSM5205 input clocks.
    std::uint64_t SamplesCpuMsmClock() const;
    void UpdateChannelStereoRouting();

    std::vector<std::uint8_t> music_rom_;
    std::vector<std::uint8_t> sample_image_;
    SfSoundMemoryMap memory_map_;
    DeviceManager device_manager_;
    MameDerivedZ80Adapter* music_cpu_ = nullptr;
    MameDerivedZ80Adapter* samples_cpu_ = nullptr;
    Ym2151DeviceAdapter* ym2151_ = nullptr;
    Msm5205DeviceAdapter* msm_[kMsm5205Count] = {nullptr, nullptr};

    ClockConverter clock_converter_;  // canonical ticks -> 3,579,545Hz Z80 / YM2151 cycles

    std::uint8_t latch_ = 0;
    bool pending_command_valid_ = false;
    std::uint8_t pending_command_ = 0;
    bool command_sent_since_reset_ = false;

    std::uint64_t cumulative_ticks_processed_ = 0;
    std::uint64_t music_cycles_executed_ = 0;
    std::uint64_t samples_cycles_executed_ = 0;
    std::uint64_t frames_delivered_ = 0;
    std::uint64_t samples_irq_count_ = 0;
    std::uint64_t msm_write_count_ = 0;
    std::uint32_t reset_count_ = 0;
    // Z80 #2's cycle count at the start of its running Execute() call.
    std::uint64_t samples_exec_base_cycles_ = 0;

    std::uint64_t sound_sync_cursor_ = 0;
    std::uint64_t block_base_cycles_ = 0;

    std::vector<LogicalChannel> channels_;
    bool channels_registered_ = false;
    std::vector<std::vector<float>> ym2151_channel_scratch_;
    std::vector<float> msm_channel_scratch_[kMsm5205Count];
};
