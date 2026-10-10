// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "clock.h"
#include "device_manager.h"
#include "devices/cpu/z80/mame_derived_z80_adapter.h"
#include "devices/sound/ym2610/ym2610_device_adapter.h"
#include "chipestra_abi.h"
#include "ninja_warriors_command_interface.h"
#include "ninja_warriors_sound_memory_map.h"

#include <cstdint>
#include <memory>
#include <vector>

// The Taito Ninja Warriors sound subsystem: one Z80 (audiocpu), one YM2610
// (FM+SSG+ADPCM-A+ADPCM-B with its own external sample ROMs), and the TC0140SYT
// command latch (NinjaWarriorsCommandInterface) that substitutes for the real
// 68000 master side, which is not emulated.
//
// LogicalChannels reuse Ym2610DeviceAdapter's base offsets as ids (one sound
// chip on the board): FM (0-3), SSG (4-6), ADPCM-A (7-12), ADPCM-B (13).
//
// Interrupt lines (independent Z80 pins):
//   NMI = command latch (slave-side status)
//   IRQ = YM2610's irq_handler, forwarded directly
//
// Clock domains: audiocpu Z80 = 16MHz/4 = 4,000,000Hz; YM2610 = 16MHz/2 =
// 8,000,000Hz, exactly 2x the Z80, so Render() drives it with a fixed integer
// multiplier (kYm2610ClocksPerZ80Cycle).
//
// SyncSoundDevices() runs on every YM2610 bus access so a driver that polls the
// status register sees up-to-date chip state.
class NinjaWarriorsMachine final : public IMachine {
public:
    // audiocpu Z80 clock.
    static constexpr ClockRate kZ80ClockRate{4'000'000};

    // YM2610 clock is exactly 2x the Z80 clock: a fixed integer multiplier.
    static constexpr std::uint64_t kYm2610ClocksPerZ80Cycle = 2;

    // Boot warm-up so a command arriving before the first Render() is handled.
    static constexpr std::uint64_t kBootWarmupFrames = 256ull * 24;

    // Master-CPU boot handshake substitution: the real 68000 master autonomously
    // sends command 0xEF (a system init ack), then 0x5A (the opening theme), then
    // 0x3B during its attract-mode boot. Without at least 0xEF/0x5A every command
    // sent afterwards has no effect (0xEF's handler initializes state the command
    // dispatch depends on); 0x3B lets the audiocpu's song-sequence reader advance
    // past its first batch of channel triggers (omitting it leaves ADPCM-A/B
    // channels audibly shorter-lived).
    //
    // Each send must be separated by a render gap long enough for the audiocpu's
    // idle loop to come back around to its TC0140SYT reception check (~7-8 s):
    // the hardware mailbox is a single two-nibble register, not a queue, so a
    // second send before the first is drained overwrites it. Real hardware sends
    // 0x5A and 0x3B only ~33 ms apart; the same safe 8 s gap is used for all.
    static constexpr std::uint64_t kMasterHandshakeGapFrames = 192'000ull * 8;  // 8.0s at 192kHz

    static constexpr std::uint32_t kFmBase = Ym2610DeviceAdapter::kFmBase;
    static constexpr std::uint32_t kSsgBase = Ym2610DeviceAdapter::kSsgBase;
    static constexpr std::uint32_t kAdpcmABase = Ym2610DeviceAdapter::kAdpcmABase;
    static constexpr std::uint32_t kAdpcmBBase = Ym2610DeviceAdapter::kAdpcmBBase;
    static constexpr std::uint32_t kChannelCount = Ym2610DeviceAdapter::kChannelCount;

    // Per-channel routing gain. MAME's own mixer levels for this board are tied
    // to an asymmetric subwoofer/software-pan-filter graph that is not
    // reproduced; routing is expressed through LogicalChannel Pan/RoutingGain.
    //
    // FM, ADPCM-A and ADPCM-B share ONE gain: the chip sums the three generators
    // onto one internal bus and clamps the result once (clamp16), and no driver
    // separates them; their balance is set by each voice's hardware volume (plus
    // FM's rshift=1 vs ADPCM's rshift=0, handled in Ym2610DeviceAdapter). The
    // Mixer's master clamp to [-1, 1] mirrors the chip's clamp16, so hot content
    // clamps as on hardware instead of being pre-attenuated. 5.94 was calibrated
    // so command 0x51 (DADDY MULK) matches a MAME -wavwrite reference to within
    // 0.1 dB (RMS 0.0558, peak 0.2788).
    //
    // SSG is a separate bus on real hardware and is deliberately silenced (0.0):
    // in the commands checked, MAME's SSG route reads exactly zero, while the
    // adapter reports the same command-independent raw level (~0.4999, most
    // likely a resting DC bias in ymfm's SSG engine) that never reaches the real
    // output. No SSG-driven track has been found to calibrate against.
    static constexpr float kFmAdpcmRoutingGain = 5.94f;
    static constexpr float kSsgRoutingGain = 0.0f;

    // `audiocpu_rom` is loaded into the Z80's ROM region; `adpcmARom`/`adpcmBRom`
    // into Ym2610DeviceAdapter's sample ROMs, during Initialize(). All default to
    // empty (a registered-but-not-yet-playable Machine).
    explicit NinjaWarriorsMachine(std::vector<std::uint8_t> audiocpu_rom,
                                  std::vector<std::uint8_t> adpcmARom = {},
                                  std::vector<std::uint8_t> adpcmBRom = {});
    ~NinjaWarriorsMachine() override;

    void Initialize() override;
    void Reset() override;
    void Shutdown() override;
    std::vector<LogicalChannel>& LogicalChannels() override;
    void Render(EmuDuration duration, LogicalChannelSink* sink) override;
    void SendCommand(const MusicCommand& command) override;

    // Test-only observation points.
    MameDerivedZ80Adapter::RegisterSnapshot AudioCpuSnapshot() const { return audiocpu_->Snapshot(); }
    MameDerivedZ80Adapter& AudioCpuForTest() { return *audiocpu_; }
    Ym2610DeviceAdapter& Ym2610ForTest() { return *ym2610_; }
    std::uint8_t ReadAudioCpuMemory(std::uint16_t address) const { return sound_memory_map_.Read8(address); }

    // Pre-Initialize() test hooks: audiocpu_ only exists once Initialize() has
    // constructed it, so the observer is stashed and applied inside Initialize()
    // before RunBootWarmup()/SendMasterBootHandshake() run, which is the only way
    // to see writes made during that internal boot pass.
    void SetAudioCpuMemoryWriteObserverForTest(MameDerivedZ80Adapter::MemoryWriteObserver observer) {
        audiocpu_memory_write_observer_for_test_ = std::move(observer);
    }
    void SetAudioCpuOpcodeFetchObserverForTest(MameDerivedZ80Adapter::OpcodeFetchObserver observer) {
        audiocpu_opcode_fetch_observer_for_test_ = std::move(observer);
    }

private:
    // Endpoint mapped at NinjaWarriorsSoundMemoryMap::kIoStart that dispatches
    // the audiocpu's scattered addresses (YM2610 registers, TC0140SYT slave
    // port/comm, pancontrol_w, sound_bankswitch_w, no-op stubs).
    class IoBus;

    // Advances ym2610_ up to the given mark on the current Render()'s audiocpu
    // cycle timeline (0 at the top of Render()).
    void SyncSoundDevices(std::uint64_t audioCpuCycleMark);

    std::vector<std::uint8_t> audiocpu_rom_;
    std::vector<std::uint8_t> adpcm_a_rom_;
    std::vector<std::uint8_t> adpcm_b_rom_;
    NinjaWarriorsSoundMemoryMap sound_memory_map_;
    DeviceManager device_manager_;
    // Concrete types (not IZ80Core*/IDevice*); non-owning, owned by device_manager_.
    MameDerivedZ80Adapter* audiocpu_ = nullptr;
    Ym2610DeviceAdapter* ym2610_ = nullptr;
    std::unique_ptr<NinjaWarriorsCommandInterface> command_interface_;
    std::unique_ptr<IoBus> io_bus_;

    // Stashed until Initialize() constructs audiocpu_.
    MameDerivedZ80Adapter::MemoryWriteObserver audiocpu_memory_write_observer_for_test_;
    MameDerivedZ80Adapter::OpcodeFetchObserver audiocpu_opcode_fetch_observer_for_test_;

    ClockConverter clock_converter_;  // canonical ticks -> 4,000,000Hz audiocpu cycles

    std::uint64_t cumulative_ticks_processed_ = 0;
    std::uint64_t cumulative_cycles_executed_ = 0;  // audiocpu

    // audiocpu-cycle mark ym2610_ has been advanced to within the current Render().
    std::uint64_t sound_sync_cursor_ = 0;

    // Software pan-control register bytes (0xE400-0xE403). The ROM always writes
    // the same value to all four (0xFF at boot, settling to 0xCE), so they are a
    // uniform fade, not a stereo pan; stored but not wired to any LogicalChannel.
    std::uint8_t pan_control_[4] = {};

    std::vector<LogicalChannel> channels_;
    // Gap between the STOP and the next command when switching songs.
    static constexpr std::uint64_t kSwitchStopGapFrames = 192'000ull * 2;  // 2.0s at 192kHz
    static_assert(kSwitchStopGapFrames % 256 == 0, "must be a multiple of the render chunk");
    bool song_active_ = false;  // a non-STOP command has been sent since the last STOP/boot (cleared by Reset())

    bool channels_registered_ = false;  // registered with the sink on Render()'s first call
    std::vector<std::vector<float>> ym2610_channel_scratch_;

    void UpdateChannelStereoRouting();
    void RunBootWarmup();
    // Substitutes for the real master's boot handshake (0xEF -> gap -> 0x5A -> gap
    // -> 0x3B -> gap -> STOP); called after RunBootWarmup() in Initialize()/Reset().
    void SendMasterBootHandshake();
};
