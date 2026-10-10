// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "clock.h"
#include "device_manager.h"
#include "devices/cpu/h6280/mame_derived_h6280_adapter.h"
#include "devices/sound/okim6295/okim6295_device_adapter.h"
#include "devices/sound/ym2151/ym2151_device_adapter.h"
#include "devices/sound/ym2203/ym2203_device_adapter.h"
#include "vapor_trail_command_interface.h"
#include "vapor_trail_memory_map.h"
#include "chipestra_abi.h"

#include <array>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

// The Data East Vapor Trail sound subsystem: H6280 + YM2203 + YM2151 + two OKIM6295.
//
// LogicalChannels: YM2203 (FM 0-2, SSG 3-5), YM2151 (6-13), OKI1 (14-17), OKI2 (18-21) = 22 total. Every
// channel stays centered (Pan == 0) because the hardware is mono, so OnPanOverrideCleared() is a no-op.
//
// Interrupt lines: IRQ1 = command latch (level-triggered, cleared by reading the latch); IRQ2 = YM2151
// timer IRQ (level-triggered). The YM2203 IRQ and the OKI chips have no interrupt line on real hardware.
//
// The CPU and every sound chip run at different clocks. clock_converter_ turns canonical ticks into H6280
// cycles; four further ClockConverters turn elapsed H6280 cycles directly into each chip's input-clock
// domain (a ClockConverter only needs a constant-rate source, so H6280 cycles can stand in for ticks).
// SyncSoundDevices() advances every chip at bus-access granularity off that one timeline.
class VaporTrailMachine final : public IMachine {
public:
    // H6280 clock (24MHz / 4).
    static constexpr ClockRate kH6280ClockRate{6'000'000};
    // set_timer_scale(2): MAME's hack to fix music speed in some Data East games (see
    // IH6280Core::SetTimerScale); applied once, before the first Reset().
    static constexpr int kH6280TimerScale = 2;

    // Steady-state Render() sub-chunk size, in H6280 cycles. The BGM driver's idle-wait loop makes no bus
    // accesses, so nothing would advance the YM2151 (and detect a Timer B overflow, ~12,000 cycles) until the
    // end of a ~8,000-cycle block, which rounded every note interval up to a block boundary and slowed the
    // tempo. Executing in small chunks, with a device sync after each, bounds that latency to this value.
    static constexpr std::uint64_t kSteadyStateSyncChunkCycles = 128;

    // Real chip clocks, all derived from the same 32.22MHz XTAL: YM2203 = /8, YM2151 = /9, OKI1 = /32, OKI2 = /16.
    static constexpr ClockRate kYm2203ClockRate{4'027'500};
    static constexpr ClockRate kYm2151ClockRate{3'580'000};
    static constexpr ClockRate kOki1ClockRate{1'006'875};
    static constexpr ClockRate kOki2ClockRate{2'013'750};

    // Per-device routing gain, calibrated against MAME's own output of the same commands (one representative
    // command per device, with vaportra.cpp's route gains applied), then scaled down uniformly so the loudest
    // track of the 77 in vaportra.xml peaks at 0.95 instead of clipping. YM2151 is larger because this
    // Machine folds its L/R to one averaged value per channel while real hardware's mono speaker sums both
    // routed channels. Re-checked after the resampler's group-delay compensation, which changes how channel
    // onsets overlap.
    static constexpr float kYm2203RoutingGain = 0.567f;
    static constexpr float kYm2151RoutingGain = 1.163f;
    static constexpr float kOki1RoutingGain = 0.708f;
    static constexpr float kOki2RoutingGain = 0.578f;

    // Boot latch sequence written by the real 68000 at startup. Offsets are in canonical 192kHz ticks.
    struct BootLatchWrite {
        std::uint64_t canonicalTickOffset;
        std::uint8_t value;
    };
    static constexpr BootLatchWrite kBootLatchWrites[] = {
        {1465, 0x01},    // 7.630833ms
        {6708, 0x9F},    // 34.935583ms
        {10014, 0xC7},   // 52.155583ms
        {16639, 0x04},   // 86.661583ms
    };
    // Only 0x01 is needed to reach the same driver state; the full four-byte sequence is kept as observed.

    // Settle time after the last boot byte, and after the STOP that follows it, before Initialize()/Reset()
    // return. Ending the boot handshake with the driver's own STOP leaves nothing audible running before the
    // first real SendCommand().
    static constexpr std::uint64_t kBootSettleGapFrames = 192'000ull;       // 1.0s at 192kHz
    static constexpr std::uint64_t kStopSettleGapFrames = 192'000ull;      // 1.0s at 192kHz
    static constexpr std::uint64_t kWarmupChunkFrames = 256;
    static_assert(kBootSettleGapFrames % kWarmupChunkFrames == 0, "must be a multiple of the render chunk");
    static_assert(kStopSettleGapFrames % kWarmupChunkFrames == 0, "must be a multiple of the render chunk");

    // Gap between a driver-silencing STOP and the next real command when switching songs mid-playback.
    static constexpr std::uint64_t kSwitchStopGapFrames = 192'000ull * 2;  // 2.0s at 192kHz
    static_assert(kSwitchStopGapFrames % kWarmupChunkFrames == 0, "must be a multiple of the render chunk");

    // LogicalChannel id layout.
    static constexpr std::uint32_t kYm2203FmBase = 0;
    static constexpr std::uint32_t kYm2203SsgBase = kYm2203FmBase + Ym2203DeviceAdapter::kFmChannelCount;
    static constexpr std::uint32_t kYm2151Base = kYm2203SsgBase + Ym2203DeviceAdapter::kSsgChannelCount;
    static constexpr std::uint32_t kOki1Base = kYm2151Base + Ym2151DeviceAdapter::kChannelCount;
    static constexpr std::uint32_t kOki2Base = kOki1Base + Okim6295DeviceAdapter::kVoiceCount;
    static constexpr std::uint32_t kChannelCount = kOki2Base + Okim6295DeviceAdapter::kVoiceCount;

    // `soundcpu_rom` is loaded into the H6280 ROM region; `oki1_rom`/`oki2_rom` are the two OKIM6295 sample
    // ROMs. All default to empty (a registered-but-not-yet-playable Machine).
    explicit VaporTrailMachine(std::vector<std::uint8_t> soundcpu_rom, std::vector<std::uint8_t> oki1_rom = {},
                                std::vector<std::uint8_t> oki2_rom = {});
    ~VaporTrailMachine() override;

    void Initialize() override;
    void Reset() override;
    void Shutdown() override;
    std::vector<LogicalChannel>& LogicalChannels() override;
    void Render(EmuDuration duration, LogicalChannelSink* sink) override;
    void SendCommand(const MusicCommand& command) override;

    // Intentionally a no-op: the hardware is mono, so every channel's Pan stays at 0.
    void OnPanOverrideCleared(std::uint32_t channelId) override;

    // Test-only observation points - not part of IMachine.
    MameDerivedH6280Adapter::RegisterSnapshot H6280Snapshot() const;
    std::uint8_t ReadMemory(std::uint32_t physicalAddress) const;
    // Test-only counterpart to ReadMemory(): checks that a physical-address write reaches the right chip.
    void WriteMemory(std::uint32_t physicalAddress, std::uint8_t value);
    MameDerivedH6280Adapter& H6280ForTest() { return *h6280_; }
    Ym2151DeviceAdapter& Ym2151ForTest() { return *ym2151_; }
    Ym2203DeviceAdapter& Ym2203ForTest() { return *ym2203_; }
    bool SongActiveForTest() const { return song_active_; }

    // Pre-Initialize() test hook: h6280_ only exists once Initialize() has constructed it, so the observer is
    // stashed here and applied inside Initialize() before the boot pass runs.
    void SetH6280OpcodeFetchObserverForTest(MameDerivedH6280Adapter::OpcodeFetchObserver observer) {
        h6280_opcode_fetch_observer_for_test_ = std::move(observer);
    }
    // Same pre-Initialize() pattern, for the memory read observer.
    void SetH6280MemoryReadObserverForTest(MameDerivedH6280Adapter::MemoryReadObserver observer) {
        h6280_memory_read_observer_for_test_ = std::move(observer);
    }
    // Same pre-Initialize() pattern, for the memory write observer. Fires for every write outside the H6280's
    // internal I/O page (0x1FE800-0x1FF7FF), i.e. every device-mapped write.
    void SetH6280MemoryWriteObserverForTest(MameDerivedH6280Adapter::MemoryWriteObserver observer) {
        h6280_memory_write_observer_for_test_ = std::move(observer);
    }
    // Fires at the end of every Render() call with (cyclesRequested, executeReturn, cumulative cycles after
    // this call). Settable any time before the render loop of interest.
    using RenderBlockObserver = std::function<void(std::uint64_t cyclesRequested, std::uint64_t executeReturn,
                                                     std::uint64_t cumulativeCyclesExecutedAfter)>;
    void SetRenderBlockObserverForTest(RenderBlockObserver observer) { render_block_observer_for_test_ = std::move(observer); }

    // Raw internal counters for tests.
    std::uint64_t CumulativeCyclesExecutedForTest() const { return cumulative_cycles_executed_; }
    std::uint64_t Ym2151CyclesIssuedForTest() const { return ym2151_cycles_issued_; }
    std::uint64_t Ym2203CyclesIssuedForTest() const { return ym2203_cycles_issued_; }

private:
    class CallbackEndpoint;  // generic std::function-backed AddressSpaceEndpoint, local glue only (this .cpp)

    // Takes a fully cumulative H6280-cycle mark (since Initialize()/Reset()): every caller adds
    // block_base_cycles_ itself (CurrentBlockCycles() already does).
    void SyncSoundDevices(std::uint64_t cumulativeH6280);
    std::uint64_t CurrentBlockCycles() const { return block_base_cycles_ + h6280_->CyclesConsumedThisCall(); }

    // Renders in <=kWarmupChunkFrames chunks (nullptr sink) until `renderedSoFar` reaches exactly
    // `targetTicks`, clamping the final chunk so the boot-byte timing lands on its target.
    void RenderExactlyTo(std::uint64_t& renderedSoFar, std::uint64_t targetTicks);

    void RunBootWarmup();
    // Substitutes for the real 68000's autonomous boot sequence: replays the boot bytes at their real
    // relative timing, lets the driver settle, then sends the driver's STOP (0x00). Called by Initialize()
    // and Reset(), after RunBootWarmup().
    void SendMasterBootSequence();

    std::vector<std::uint8_t> soundcpu_rom_;
    std::vector<std::uint8_t> oki1_rom_;
    std::vector<std::uint8_t> oki2_rom_;

    VaporTrailMemoryMap memory_map_;
    DeviceManager device_manager_;
    MameDerivedH6280Adapter* h6280_ = nullptr;
    Ym2203DeviceAdapter* ym2203_ = nullptr;
    Ym2151DeviceAdapter* ym2151_ = nullptr;
    Okim6295DeviceAdapter* oki1_ = nullptr;
    Okim6295DeviceAdapter* oki2_ = nullptr;
    std::unique_ptr<VaporTrailCommandInterface> command_interface_;
    std::array<std::unique_ptr<CallbackEndpoint>, 5> endpoints_;  // ym2203, ym2151, oki1, oki2, latch (Initialize() order)

    ClockConverter clock_converter_;         // canonical ticks -> 6,000,000Hz H6280 cycles
    ClockConverter ym2151_from_h6280_;       // H6280 cycles -> YM2151 input clocks
    ClockConverter ym2203_from_h6280_;       // H6280 cycles -> YM2203 input clocks
    ClockConverter oki1_from_h6280_;         // H6280 cycles -> OKI1 input clocks
    ClockConverter oki2_from_h6280_;         // H6280 cycles -> OKI2 input clocks

    std::uint64_t cumulative_ticks_processed_ = 0;   // canonical ticks, since Initialize()/Reset()
    std::uint64_t cumulative_cycles_executed_ = 0;   // H6280 cycles, since Initialize()/Reset()
    std::uint64_t block_base_cycles_ = 0;            // H6280 cycles executed in prior blocks
    std::uint64_t sound_sync_cursor_ = 0;             // cumulative H6280-cycle mark the devices have advanced to

    // Cumulative chip-input-clocks already issued to each device's Render() (the Bresenham carry side of
    // each *_from_h6280_ converter).
    std::uint64_t ym2151_cycles_issued_ = 0;
    std::uint64_t ym2203_cycles_issued_ = 0;
    std::uint64_t oki1_cycles_issued_ = 0;
    std::uint64_t oki2_cycles_issued_ = 0;

    std::vector<LogicalChannel> channels_;
    bool channels_registered_ = false;
    std::vector<std::vector<float>> ym2151_channel_scratch_;
    std::vector<std::vector<float>> ym2203_channel_scratch_;
    std::vector<std::vector<float>> oki1_channel_scratch_;
    std::vector<std::vector<float>> oki2_channel_scratch_;

    // Set when a non-STOP command has been sent since the last STOP/boot (cleared by Reset()); decides
    // whether SendCommand() sends a STOP first.
    bool song_active_ = false;  // a non-STOP command has been sent since the last STOP/boot (cleared by Reset())

    MameDerivedH6280Adapter::OpcodeFetchObserver h6280_opcode_fetch_observer_for_test_;
    MameDerivedH6280Adapter::MemoryReadObserver h6280_memory_read_observer_for_test_;
    MameDerivedH6280Adapter::MemoryWriteObserver h6280_memory_write_observer_for_test_;
    RenderBlockObserver render_block_observer_for_test_;
};
