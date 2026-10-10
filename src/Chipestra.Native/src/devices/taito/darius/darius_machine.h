// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "clock.h"
#include "device_manager.h"
#include "devices/cpu/z80/mame_derived_z80_adapter.h"
#include "devices/sound/msm5205/msm5205_device_adapter.h"
#include "devices/sound/ym2203/ym2203_device_adapter.h"
#include "chipestra_abi.h"
#include "darius_adpcm_sound_map.h"
#include "darius_command_interface.h"
#include "darius_sound_memory_map.h"

#include <cstdint>
#include <memory>
#include <vector>

// The Taito Darius sound subsystem: two Z80s (audiocpu = the main sound CPU,
// adpcm = a second ADPCM-playback-only Z80), two YM2203 FM+SSG chips, one
// MSM5205 ADPCM decoder, and the PC060HA/TC0140SYT command latch
// (DariusCommandInterface) that substitutes for the real 68000 master side
// (both of Darius's 68000s are video-only and out of scope).
//
// Both Z80s are fully emulated and run their own ROM code; the command
// interface only replaces the missing master side.
//
// LogicalChannels: ym1 FM (ids 0-2), ym1 SSG (3-5), ym2 FM (6-8), ym2 SSG
// (9-11), then the MSM5205 mono channel (12).
//
// Clock domains:
//   - audiocpu Z80, adpcm Z80, ym1, ym2 share one nominal clock
//     (XTAL(8'000'000)/2 = 4,000,000 Hz). It is not an exact multiple of
//     kNativeRuntimeTickRateHz, so one ClockConverter (Bresenham carry) drives
//     all four from one per-block cycle target; each Z80 tracks its own
//     cumulative cycles since each consumes cycles per its own instruction
//     boundaries.
//   - MSM5205: 384 kHz, exactly 2x kNativeRuntimeTickRateHz, so a plain integer
//     doubling suffices.
//
// Interrupt lines:
//   - audiocpu NMI = the command interface's slave-side status (edge-based, so
//     a plain assert/clear forward is correct).
//   - audiocpu INT (maskable) = ym1's IRQ line only (ym2's IRQ is not wired on
//     the real board), forwarded directly.
//   - adpcm Z80 NMI = MSM5205's VCK line (8000/sec pulses), gated by the adpcm
//     Z80's own nmi_enable flag (set via its I/O ports 0x00/0x01). Render()
//     interleaves both Z80s one MSM5205 sample at a time so each pulse is
//     serviced individually.
class DariusMachine final : public IMachine {
public:
    // audiocpu/adpcm/ym1/ym2 shared clock.
    static constexpr ClockRate kAudioClockRate{4'000'000};

    // MSM5205's fixed clock, exactly 2x kNativeRuntimeTickRateHz. A plain Hz
    // value is all LogicalChannel::NativeSampleRate needs.
    static constexpr std::uint32_t kMsm5205ClockHz = 384'000;

    // Exact doubling relationship, no rounding.
    static constexpr std::uint64_t kMsm5205InputClocksPerCanonicalTick = 2;

    // One MSM5205 output sample every 24 canonical ticks (exact:
    // Msm5205DeviceAdapter::kInputClocksPerSample (48) / 2).
    static constexpr std::uint64_t kMsm5205SamplePeriodCanonicalTicks = 24;

    // Boot warm-up so a command arriving before the first Render() is handled.
    // Chunked so a pulse does not fire before the CPUs have executed anything.
    static constexpr std::uint64_t kBootWarmupFrames = 256ull * 24;
    static constexpr std::uint64_t kWarmupChunkFrames = 256;

    // The real 68000 master sends 0xEF via PC060HA about 4.4 s after boot; the
    // Machine replays that boot-time value. A full render gap must separate
    // handshake sends: the TC0140SYT/PC060HA mailbox is a single two-nibble
    // register, not a queue, and the audiocpu only polls it periodically, so a
    // second send before it drains would overwrite the first.
    static constexpr std::uint64_t kMasterHandshakeGapFrames = 192'000ull * 8;  // 8.0s at 192kHz, conservative

    // Gap between the STOP and the next command when switching songs.
    static constexpr std::uint64_t kSwitchStopGapFrames = 192'000ull * 2;  // 2.0s at 192kHz
    static_assert(kSwitchStopGapFrames % kWarmupChunkFrames == 0, "must be a multiple of the render chunk");

    static constexpr std::uint32_t kFmChannelsPerChip = Ym2203DeviceAdapter::kFmChannelCount;
    static constexpr std::uint32_t kSsgChannelsPerChip = Ym2203DeviceAdapter::kSsgChannelCount;
    static constexpr std::uint32_t kFm0Base = 0;
    static constexpr std::uint32_t kPsg0Base = kFm0Base + kFmChannelsPerChip;
    static constexpr std::uint32_t kFm1Base = kPsg0Base + kSsgChannelsPerChip;
    static constexpr std::uint32_t kPsg1Base = kFm1Base + kFmChannelsPerChip;
    static constexpr std::uint32_t kMsm5205ChannelId = kPsg1Base + kSsgChannelsPerChip;
    static constexpr std::uint32_t kChannelCount = kMsm5205ChannelId + 1;

    // `audiocpu_rom`/`adpcm_rom` are loaded into the two Z80s' ROM regions during
    // Initialize(); a short or empty vector is zero-padded and never crashes.
    explicit DariusMachine(std::vector<std::uint8_t> audiocpu_rom, std::vector<std::uint8_t> adpcm_rom = {});
    ~DariusMachine() override;

    void Initialize() override;
    void Reset() override;
    void Shutdown() override;
    std::vector<LogicalChannel>& LogicalChannels() override;
    void Render(EmuDuration duration, LogicalChannelSink* sink) override;
    void SendCommand(const MusicCommand& command) override;

    // ApplyFm0/ApplyFm1/ApplyPsg0/ApplyPsg1/ApplyDa only run from their register-write
    // handlers, so clearing PanOverridden alone would leave the old pan in place.
    // Re-runs all five unconditionally (cheap); the parameter is ignored.
    void OnPanOverrideCleared(std::uint32_t channelId) override;

    // Test-only observation points.
    MameDerivedZ80Adapter::RegisterSnapshot AudioCpuSnapshot() const { return audiocpu_->Snapshot(); }
    MameDerivedZ80Adapter::RegisterSnapshot AdpcmZ80Snapshot() const { return adpcm_z80_->Snapshot(); }
    MameDerivedZ80Adapter& AudioCpuForTest() { return *audiocpu_; }
    MameDerivedZ80Adapter& AdpcmZ80ForTest() { return *adpcm_z80_; }
    Ym2203DeviceAdapter& Ym1ForTest() { return *ym1_; }
    Ym2203DeviceAdapter& Ym2ForTest() { return *ym2_; }
    Msm5205DeviceAdapter& Msm5205ForTest() { return *msm5205_; }
    bool SongActiveForTest() const { return song_active_; }
    std::uint8_t ReadAudioCpuMemory(std::uint16_t address) const { return sound_memory_map_.Read8(address); }

private:
    // Endpoint mapped at DariusSoundMemoryMap::kIoStart that dispatches the
    // audiocpu's scattered addresses (ym1/ym2 registers, PC060HA slave port/comm,
    // the 5 pan-control writes, adpcm_command_w, sound_bankswitch_w).
    class IoBus;

    // Advances ym1_/ym2_ up to the given mark on the current Render()'s audiocpu
    // cycle timeline (0 at the top of Render()), per bus access rather than per
    // block, so status-register polling sees up-to-date chip state.
    void SyncSoundDevices(std::uint64_t audioCpuCycleMark);
    std::uint64_t CurrentBlockCycles() const { return block_base_cycles_ + audiocpu_->CyclesConsumedThisCall(); }

    // Advances both Z80s' cycle budgets up to `absoluteCanonicalTicks` (they share
    // one clock). Called once per MSM5205 sample boundary so the adpcm Z80
    // services each NMI pulse individually.
    void RunCpusUpToTicks(std::uint64_t absoluteCanonicalTicks);

    void RunBootWarmup();
    void SendMasterBootHandshake();

    // Pan/volume routing (the real update_fm0/update_fm1/update_psg0/update_psg1/
    // update_da). Modeled as LogicalChannel Pan/RoutingGain rather than the real
    // filter_volume_device graph. `DefVol` follows the driver's lookup table
    // (a 4-bit index -> a 0..100 logarithmic gain).
    static float DefVol(std::uint8_t index4bit);

    // Fixed hardware mixer-wiring gains from the driver's machine config: SSG
    // routes 0.08, FM (already mixed 3ch->1 by the chip) 0.60, i.e. FM:SSG =
    // 7.5:1. `kMasterCalibrationGain` is a separate overall multiplier applied on
    // top of both (preserving that ratio), because FM and SSG come out of the same
    // ymfm generate() call and are already scaled correctly relative to each other.
    static constexpr float kFmHwRoutingGain = 0.60f;
    static constexpr float kSsgHwRoutingGain = 0.08f;
    // Calibrated to a reference capture of command 0x06 (CAPTAIN NEO) from
    // MAME: the reference was taken well after the audiocpu boot has settled
    // (the real master's own demo-mode commands override earlier injections),
    // comparing combined L/R RMS over the same time window (reference 0.0519
    // vs this Machine at gain 1.0, a ~1.12x gap).
    static constexpr float kMasterCalibrationGain = 1.12f;
    void ApplyFm0();
    void ApplyFm1();
    void ApplyPsg0(int port);  // port: 0=A, 1=B, 2=C
    void ApplyPsg1(int port);
    void ApplyDa();

    std::vector<std::uint8_t> audiocpu_rom_;
    std::vector<std::uint8_t> adpcm_rom_;
    DariusSoundMemoryMap sound_memory_map_;
    DariusAdpcmSoundMap adpcm_memory_map_;
    DeviceManager device_manager_;
    // Concrete types (not IZ80Core*), same reasoning as every other Machine
    // Concrete types (not IZ80Core*); non-owning, owned by device_manager_.
    MameDerivedZ80Adapter* audiocpu_ = nullptr;
    MameDerivedZ80Adapter* adpcm_z80_ = nullptr;
    Ym2203DeviceAdapter* ym1_ = nullptr;
    Ym2203DeviceAdapter* ym2_ = nullptr;
    Msm5205DeviceAdapter* msm5205_ = nullptr;
    std::unique_ptr<DariusCommandInterface> command_interface_;
    std::unique_ptr<IoBus> io_bus_;

    ClockConverter clock_converter_;  // canonical ticks -> 4,000,000Hz audiocpu/adpcm/ym1/ym2 cycles

    std::uint64_t cumulative_ticks_processed_ = 0;
    std::uint64_t cumulative_cycles_executed_ = 0;       // audiocpu
    std::uint64_t adpcm_cumulative_cycles_executed_ = 0;  // adpcm Z80
    std::uint64_t msm_samples_delivered_ = 0;  // Bresenham due-pulse counter

    // audiocpu-cycle mark ym1_/ym2_ have been advanced to within the current
    // Render() call, and cycles consumed by earlier audiocpu_->Execute() calls in
    // the block. Both reset to 0 at the top of every Render().
    std::uint64_t sound_sync_cursor_ = 0;
    std::uint64_t block_base_cycles_ = 0;

    // adpcm Z80 I/O-port state: the 1-byte latch audiocpu writes at 0xD400
    // (adpcm_command_w, read at port 0x00) and its NMI-enable flag (ports 0x00/
    // 0x01) gating whether MSM5205's VCK reaches its NMI pin.
    std::uint8_t adpcm_command_ = 0;
    bool adpcm_nmi_enable_ = false;

    // Pan/volume registers (m_pan[]/m_vol[]); all zero at power-on.
    std::uint8_t fm0_pan_ = 0, fm1_pan_ = 0, psg0_pan_ = 0, psg1_pan_ = 0, da_pan_ = 0;
    std::uint8_t fm0_vol_ = 0, fm1_vol_ = 0;
    std::uint8_t psg0_vol_[3] = {};
    std::uint8_t psg1_vol_[3] = {};

    std::vector<LogicalChannel> channels_;
    bool song_active_ = false;  // any command (STOP included) has been sent since boot/Reset() (cleared only by Reset())
    bool channels_registered_ = false;  // registered with the sink on Render()'s first call
    std::vector<std::vector<float>> ym1_channel_scratch_;
    std::vector<std::vector<float>> ym2_channel_scratch_;
    std::vector<float> msm5205_channel_scratch_;  // single mono channel, not vector-of-vectors
};
