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

    // Per-channel routing gain of the legacy mix, which Metal Black still uses (the
    // Ninja Warriors mix below does not use these). Routing is expressed through
    // LogicalChannel Pan/RoutingGain.
    //
    // FM, ADPCM-A and ADPCM-B are summed by the chip onto one bus and clamped once (clamp16);
    // the Mixer's master clamp to [-1, 1] mirrors that, so hot content clamps as on hardware.
    // Their levels follow from the board's speaker routing (see Mix below), not from a
    // calibrated constant. The SSG is a separate bus: the Ninja Warriors board sends it to a
    // low-frequency speaker that this player does not have, so it stays silent (gain 0) there.
    static constexpr float kSsgRoutingGain = 0.0f;

    // `audiocpu_rom` is loaded into the Z80's ROM region; `adpcmARom`/`adpcmBRom`
    // into Ym2610DeviceAdapter's sample ROMs, during Initialize(). All default to
    // empty (a registered-but-not-yet-playable Machine).
    // How the YM2610's outputs reach the speakers.
    //  NinjaWarriors: as the board's machine configuration does. The chip's left and right
    //    outputs are both sent to both speakers, through four gains that the Z80 writes to
    //    0xE400-0xE403 (gain = int(value * 100 / 255) / 100, 1.0 until written). A channel
    //    that outputs on both the left and the right bit counts twice, one that outputs on
    //    one side counts once. Output stays centred while the four gains are equal.
    //  TaitoF2: as the F2 System's machine configuration does (used by Metal Black). The chip's
    //    left output goes to the left speaker and its right output to the right speaker, each
    //    with gain 1.0, and the SSG goes to both speakers with gain 0.75. The Z80's writes to
    //    0xE400-0xE403 are ignored by that board. The SSG has its resting DC removed.
    enum class Mix { NinjaWarriors, TaitoF2 };

    explicit NinjaWarriorsMachine(std::vector<std::uint8_t> audiocpu_rom,
                                  std::vector<std::uint8_t> adpcmARom = {},
                                  std::vector<std::uint8_t> adpcmBRom = {},
                                  Mix mix = Mix::NinjaWarriors);
    ~NinjaWarriorsMachine() override;

    void Initialize() override;
    void Reset() override;
    void Shutdown() override;
    std::vector<LogicalChannel>& LogicalChannels() override;
    void Render(EmuDuration duration, LogicalChannelSink* sink) override;
    void SendCommand(const MusicCommand& command) override;

    // Test-only observation points.
    MameDerivedZ80Adapter::RegisterSnapshot AudioCpuSnapshot() const { return audiocpu_->Snapshot(); }
    // The gain (0..1) of the pan-control port (0xE400 + port), as the machine uses it.
    float PanGainForTest(std::uint32_t port) const { return static_cast<float>(pan_percent_[port & 3]) / 100.0f; }
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

    // Software pan-control gains (0xE400-0xE403) as whole percents (100 until written).
    // Port p feeds one of the four gains of the board's filter chain:
    //   port 1: chip left -> left speaker, port 0: chip left -> right speaker,
    //   port 3: chip right -> left speaker, port 2: chip right -> right speaker.
    int pan_percent_[4] = {100, 100, 100, 100};
    Mix mix_;

    std::vector<LogicalChannel> channels_;
    // Gap between the STOP and the next command when switching songs.
    static constexpr std::uint64_t kSwitchStopGapFrames = 192'000ull * 2;  // 2.0s at 192kHz
    static_assert(kSwitchStopGapFrames % 256 == 0, "must be a multiple of the render chunk");
    bool song_active_ = false;  // a non-STOP command has been sent since the last STOP/boot (cleared by Reset())

    bool channels_registered_ = false;  // registered with the sink on Render()'s first call
    std::vector<std::vector<float>> ym2610_channel_scratch_;

    // A one-pole DC blocker (about 30 Hz at the SSG's stream rate), used by the TaitoF2 mix. The
    // YM2610's SSG output rests at a constant level, which the board's analog stage does not pass.
    struct DcBlocker {
        float prevInput = 0.0f;
        float prevOutput = 0.0f;
        void Process(float* samples, std::size_t count, float r) {
            for (std::size_t i = 0; i < count; ++i) {
                const float out = samples[i] - prevInput + r * prevOutput;
                prevInput = samples[i];
                prevOutput = out;
                samples[i] = out;
            }
        }
    };
    DcBlocker ssg_dc_[Ym2610DeviceAdapter::kSsgChannelCount];

    void UpdateChannelStereoRouting();
    static int PanPercentOf(std::uint8_t value);
    void RunBootWarmup();
    // Substitutes for the real master's boot handshake (0xEF -> gap -> 0x5A -> gap
    // -> 0x3B -> gap -> STOP); called after RunBootWarmup() in Initialize()/Reset().
    void SendMasterBootHandshake();
};
