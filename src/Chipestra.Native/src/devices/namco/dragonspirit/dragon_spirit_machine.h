// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "clock.h"
#include "device_manager.h"
#include "devices/cpu/hd63701/mame_derived_hd63701_adapter.h"
#include "devices/cpu/m6809/mame_derived_m6809_adapter.h"
#include "devices/sound/cus30/cus30_device_adapter.h"
#include "devices/sound/dac8bitr2r/dac8bitr2r_device_adapter.h"
#include "devices/sound/ym2151/ym2151_device_adapter.h"
#include "dragon_spirit_command_interface.h"
#include "dragon_spirit_mcu_memory_map.h"
#include "dragon_spirit_memory_map.h"
#include "chipestra_abi.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

// The Namco System 1 sound subsystem: the audio CPU (MC6809E), YM2151, CUS30, and the MCU
// (CUS64 / HD63701V0) driving two 8-bit R2R DACs for voice-sample playback (command 0x15). The main and
// sub game-logic CPUs are not emulated: SendCommand() substitutes for them by writing the command bytes
// into the shared TRIRAM (see DragonSpiritCommandInterface). The audio-CPU/MCU voice-trigger handshake
// (TRIRAM offsets 0x02D/0x02E) is carried out by the ROM code of those two CPUs; likewise the audio CPU
// drives CUS30 directly from its own code. This Machine only wires TRIRAM/ROM/clocks/DAC ports.
//
// LogicalChannels: YM2151 (ids 0-7), CUS30 (ids 8-15), then DAC1/DAC2 (ids 16-17) - 18 total.
//
// The MCU is stepped once per Render() block (not per bus access): DAC precision comes from
// Dac8BitR2RDeviceAdapter recording write events with cycle offsets, and the MCU's IRQ1 handler writes
// each DAC in a fixed 99-iteration loop that finishes well within one block. That gives an exact DAC
// sample rate of 99 x 60.606...Hz = 6000.0Hz (99/1024 of the MCU execution clock).
//
// Interrupt lines:
//   IRQ  = VBLANK, ~60.606060Hz, level-triggered, acknowledged by a write to 0xE000
//          (DragonSpiritMemoryMap::kIrqAckAddress); the CPU's own handler clears the line.
//   FIRQ = YM2151 timer IRQ, a separate line from the VBLANK IRQ.
//   MCU IRQ1 = the same VBLANK falling edge, acknowledged by a write to 0xF000; both CPUs' lines are
//          asserted from the same pulse count in Render().
// Commands do not use these lines: the audio CPU discovers a new command by polling TRIRAM.
class DragonSpiritMachine final : public IMachine {
public:
    // Audio CPU clock: XTAL(49.152MHz)/32.
    static constexpr ClockRate kM6809ClockRate{1'536'000};
    static constexpr ClockRate kYm2151ClockRate{3'579'545};
    static constexpr ClockRate kCus30ClockRate{12'000};  // XTAL(49'152'000)/2048/2 (namcos1.cpp)
    // MCU execution clock. The HD63701 is fed 6,144,000Hz, but one executed cycle takes 4 input periods
    // (MAME's execute_cycles_to_clocks = cycles * 4), so the real rate is 6,144,000/4 = 1,536,000Hz.
    static constexpr ClockRate kMcuClockRate{1'536'000};

    // DAC0/DAC1 effective sample rate: 99 writes per VBLANK = 6000.0Hz exactly (99/1024 of the MCU clock).
    // This is also the LogicalChannel::NativeSampleRate, so the anti-aliased resampler used for every other
    // low-rate chip reconstructs the 192kHz signal (a bare zero-order hold would be audibly buzzy).
    static constexpr std::uint32_t kDacSampleRateHz = 6000;

    // MCU cycles -> canonical 192kHz ticks.
    static constexpr double kTicksPerMcuCycle =
        static_cast<double>(kNativeRuntimeTickRateHz) / static_cast<double>(kMcuClockRate.hz);

    // MCU cycles -> the DAC's native (6000Hz) sample grid, used for Dac8BitR2RDeviceAdapter::Reconstruct().
    static constexpr double kDacNativeSamplesPerMcuCycle =
        static_cast<double>(kDacSampleRateHz) / static_cast<double>(kMcuClockRate.hz);

    // VBLANK period in canonical 192kHz ticks: 192000 * 33 / 2000 = 3168 exactly.
    static constexpr std::uint64_t kVblankIrqPeriodCanonicalTicks = 3168;

    // Canonical ticks rendered (audio discarded) in Initialize(), so a SendCommand() right afterwards
    // reaches an already-booted driver. The boot code still runs a ~1134-instruction shared-RAM clear
    // after the CUS30 self-test gate; this value is generous rather than minimal.
    static constexpr std::uint64_t kBootWarmupFrames = 192'000ull;  // 1.0s at 192kHz
    static constexpr std::uint64_t kWarmupChunkFrames = 256;  // small chunks, so self-generated events never fire before the CPU runs
    static_assert(kBootWarmupFrames % kWarmupChunkFrames == 0, "must be a multiple of the render chunk");

    // Substitutes for the un-emulated main CPU's boot action: the main CPU clears the whole CUS30 shared
    // window (physical 0x2FE000-0x2FE3FF in its C117-translated space; the same CUS30 is also visible to the
    // audio CPU at 0x5000) and ~1.8s after reset writes this byte to offset 0 (CUS30 waveram byte 0). The
    // audio CPU's boot code (0xD000-0xD011) busy-polls that 16-bit read waiting for this value, a
    // self-test/signature gate. CUS30 waveram starts zeroed and no audio-CPU code writes there, so the
    // Machine performs the write itself.
    static constexpr std::uint8_t kMainCpuCus30SelfTestByte = 0xA6;

    // Second main-CPU boot action substituted by the Machine. The MCU's boot idle loop (0xB8E0-0xB8ED)
    // compares TRIRAM offset 0 (physical 0x7000 / MCU 0xC000) with this value before it first leaves the
    // loop. A second MCU jump-table dispatcher (0xBA75-0xBA8E) also writes that byte with values in
    // [0x00,0x1E]; on hardware those writes were observed to never affect the next read, so
    // DragonSpiritMcuMemoryMap::Write8 drops every write that is not this value. With them dropped, the
    // main CPU's single write at kMainCpuGateWriteTimeCanonicalTicks keeps the byte at this value for the
    // rest of the session.
    static constexpr std::uint8_t kMainCpuTriRamReadyByte = 0xA6;

    // Time of the main CPU's one-time write of kMainCpuTriRamReadyByte as observed on hardware:
    // 12.837s after boot = 2,464,704 canonical ticks at 192kHz.
    static constexpr std::uint64_t kMainCpuGateWriteTimeCanonicalTicks = 2'464'704ull;

    // Known limitation: command 0x15's DAC0 voice channel stays silent. The MCU's DAC0 setup routine
    // (0xF469-0xF521) only runs when the main CPU writes TRIRAM offsets 0x2B/0x2C during gameplay, and
    // the main CPU does not perform that write when the command is dispatched from the Sound Test, so the
    // Machine does not synthesize it. DAC1 plays command 0x15's voice.

    // Calibrated so the YM2151:CUS30 level ratio matches MAME's equal route gains (0.50/0.50 each). Measured
    // via MAME's sound_hook on command 0x7F: ym2151_rms=0.15600693, cus30_rms=0.07787181, target
    // cus30/ym2151 = 0.499156. kYm2151RoutingGain stays 1.0; kCus30RoutingGain = 0.499156 / 2.776111
    // (the ratio measured at unit gains in the full pipeline) = 0.179804. A clipping sweep over all 26
    // tracks with this gain shows no clipping (worst true peak 0.869).
    static constexpr float kYm2151RoutingGain = 1.0f;
    static constexpr float kCus30RoutingGain = 0.179804f;

    // DAC1 gain: MAME gives the DACs the same 0.50/0.50 route gain as YM2151/CUS30, so the target is the
    // DAC1:YM2151 level ratio of 3.113431 measured with MAME's sound_hook on command 0x15. The RMS-window
    // fit alone (2.707690) clipped command 0x15, so it was scaled down by 0.95/1.2376 to put the worst true
    // peak of the 26-track clipping sweep at ~0.95, accepting a DAC1:YM2151 ratio of 2.389916. DAC0 is
    // silent on every track, so its gain stays at 1.0.
    static constexpr float kDac0RoutingGain = 1.0f;
    static constexpr float kDac1RoutingGain = 2.078463f;

    static constexpr std::uint32_t kYm2151Base = 0;
    static constexpr std::uint32_t kCus30Base = kYm2151Base + Ym2151DeviceAdapter::kChannelCount;
    static constexpr std::uint32_t kDac0Base = kCus30Base + Cus30DeviceAdapter::kVoiceCount;
    static constexpr std::uint32_t kDac1Base = kDac0Base + 1;
    static constexpr std::uint32_t kChannelCount = kDac1Base + 1;

    // `soundcpu_rom` is loaded into the MC6809E's 8-bank (0x20000) ROM, `mcu_rom` into the MCU's 4KB
    // internal ROM, `voice_rom` into the MCU's banked external voice ROM (0xC0000 bytes). All default to
    // empty (a registered but not yet playable Machine).
    explicit DragonSpiritMachine(std::vector<std::uint8_t> soundcpu_rom, std::vector<std::uint8_t> mcu_rom = {},
                                  std::vector<std::uint8_t> voice_rom = {});
    ~DragonSpiritMachine() override;

    void Initialize() override;
    void Reset() override;
    std::uint32_t ResetCountForTest() const { return reset_count_for_test_; }
    void Shutdown() override;
    std::vector<LogicalChannel>& LogicalChannels() override;
    void Render(EmuDuration duration, LogicalChannelSink* sink) override;
    void SendCommand(const MusicCommand& command) override;

    // MusicCommandKind::Wsg (a <track type="wsg"> sound effect): Value is the game's sound-test
    // number, 0x00..kMaxWsgNumber. Every other kind is accepted. See SendCommand().
    bool AcceptsCommand(const MusicCommand& command) const override;

    // The WSG request table in the CUS30's shared RAM: audio CPU address 0x5000 + offset (main CPU
    // 0x2FE000 + offset). One 2-byte slot per sound-test number n at kWsgRequestTableOffset + 2*n; the
    // driver plays the effect when it sees 0x01 there. The whole table range is cleared before a request
    // so an earlier request cannot be replayed.
    static constexpr std::uint16_t kWsgRequestTableOffset = 0x240;
    static constexpr std::uint16_t kWsgRequestTableSize = 0xA0;  // 0x240..0x2DF
    static constexpr std::uint8_t kMaxWsgNumber = 0x24;
    void OnPanOverrideCleared(std::uint32_t channelId) override;

    // Test-only observation points.
    MameDerivedM6809Adapter::RegisterSnapshot M6809Snapshot() const;
    std::uint8_t ReadMemory(std::uint32_t physicalAddress) const;
    void WriteMemory(std::uint32_t physicalAddress, std::uint8_t value);
    MameDerivedM6809Adapter& M6809ForTest() { return *m6809_; }
    Ym2151DeviceAdapter& Ym2151ForTest() { return *ym2151_; }
    Cus30DeviceAdapter& Cus30ForTest() { return *cus30_; }
    std::uint32_t CurrentBankForTest() const { return memory_map_.CurrentBankForTest(); }

    // Test-only observation points (MCU).
    MameDerivedHd63701Adapter& McuForTest() { return *mcu_; }
    std::uint32_t McuCurrentBankForTest() const { return mcu_memory_map_.CurrentBankForTest(); }

    // Test-only: register snapshot of the MCU at the moment of the call (typically from inside an
    // opcode-fetch observer, where the registers reflect the state just before the fetched instruction).
    MameDerivedHd63701Adapter::RegisterSnapshot McuSnapshotForTest() const { return mcu_->Snapshot(); }

    // Forwards to DragonSpiritMcuMemoryMap::SetVoiceReadObserverForTest().
    void SetVoiceReadObserverForTest(std::function<void(std::size_t, std::uint8_t)> observer) const {
        mcu_memory_map_.SetVoiceReadObserverForTest(std::move(observer));
    }

    // Test-only: observers of TRIRAM offset 0 (the gate byte), tagged by source: "mcu" (MCU-side access
    // via DragonSpiritMcuMemoryMap) or "render" (the Machine's one-time substitute write in Render()). Both
    // forward to the same callback.
    using TriRamGateEventObserver = std::function<void(const char* source, std::uint8_t value)>;
    void SetTriRamGateReadObserverForTest(TriRamGateEventObserver observer) const {
        mcu_memory_map_.SetTriRamGateReadObserverForTest(
            [observer](std::uint8_t value) { observer("mcu_read", value); });
    }
    void SetTriRamGateWriteObserverForTest(TriRamGateEventObserver observer) {
        triram_gate_write_observer_for_test_ = observer;
        mcu_memory_map_.SetTriRamGateWriteObserverForTest(
            [observer](std::uint8_t value) { observer("mcu_write", value); });
    }

    // Addresses in [0x80,0xFF) are the MCU core's own internal RAM and never reach the external bus, so
    // they are read from the core.
    std::uint8_t ReadMcuMemory(std::uint32_t physicalAddress) const {
        if (physicalAddress >= MameDerivedHd63701Adapter::kInternalRamBaseForTest &&
            physicalAddress < MameDerivedHd63701Adapter::kInternalRamBaseForTest + MameDerivedHd63701Adapter::kInternalRamSizeForTest) {
            return mcu_->InternalRamForTest(
                static_cast<std::uint16_t>(physicalAddress - MameDerivedHd63701Adapter::kInternalRamBaseForTest));
        }
        return mcu_memory_map_.Read8(physicalAddress);
    }
    void WriteMcuMemory(std::uint32_t physicalAddress, std::uint8_t value) { mcu_memory_map_.Write8(physicalAddress, value); }

    // Pre-Initialize() test hook, same pattern as SetM6809OpcodeFetchObserverForTest() below.
    void SetMcuOpcodeFetchObserverForTest(MameDerivedHd63701Adapter::OpcodeFetchObserver observer) {
        mcu_opcode_fetch_observer_for_test_ = std::move(observer);
    }

    // Test-only: counts every write reaching the YM2151, independent of which ROM address performs it.
    std::uint64_t Ym2151WriteCountForTest() const { return ym2151_write_count_for_test_; }

    // Pre-Initialize() test hook: m6809_ only exists after Initialize(), so the observer is stored here and
    // applied inside Initialize() right after construction, before the boot warm-up.
    void SetM6809OpcodeFetchObserverForTest(MameDerivedM6809Adapter::OpcodeFetchObserver observer) {
        m6809_opcode_fetch_observer_for_test_ = std::move(observer);
    }

    // Test-only: forwards every raw byte recorded into DAC1 to an observer. May be set at any time.
    using Dac1WriteObserver = std::function<void(std::uint8_t)>;
    void SetDac1WriteObserverForTest(Dac1WriteObserver observer) { dac1_write_observer_for_test_ = std::move(observer); }

    // Same as SetDac1WriteObserverForTest, for DAC0.
    using Dac0WriteObserver = std::function<void(std::uint8_t)>;
    void SetDac0WriteObserverForTest(Dac0WriteObserver observer) { dac0_write_observer_for_test_ = std::move(observer); }

    // Test-only override of the one-time substitute write of the TRIRAM gate byte in Render(): `Default`
    // is the production behaviour, `Suppressed` skips the write, `ForceValue` writes a caller-chosen byte
    // at the same time.
    enum class TriRamGateByteModeForTest { Default, Suppressed, ForceValue };
    void SetTriRamGateByteModeForTest(TriRamGateByteModeForTest mode, std::uint8_t forceValue = 0) {
        triram_gate_mode_for_test_ = mode;
        triram_gate_force_value_for_test_ = forceValue;
    }

private:
    class CallbackEndpoint;  // std::function-backed AddressSpaceEndpoint, local glue (this .cpp)

    // Advances YM2151/CUS30 up to the given fully cumulative M6809-cycle mark (since Initialize()/Reset()):
    // every caller must include block_base_cycles_. The block-end caller uses Execute()'s return value, not
    // a post-call CyclesConsumedThisCall() read (which is 0 once the call has finished).
    void SyncSoundDevices(std::uint64_t cumulativeM6809);
    std::uint64_t CurrentBlockCycles() const { return block_base_cycles_ + m6809_->CyclesConsumedThisCall(); }

    // Refreshes YM2151's Pan/RoutingGain from its RL bits and CUS30's from its per-voice L/R volumes
    // (exact for the waveform path; see Cus30DeviceAdapter). Called once per Render() block.
    void UpdateChannelStereoRouting();

    void RunBootWarmup();

    std::vector<std::uint8_t> soundcpu_rom_;
    std::vector<std::uint8_t> mcu_rom_;
    std::vector<std::uint8_t> voice_rom_;

    DragonSpiritMemoryMap memory_map_;
    DragonSpiritMcuMemoryMap mcu_memory_map_;  // shares memory_map_'s TriRamForSharing()
    DeviceManager device_manager_;
    MameDerivedM6809Adapter* m6809_ = nullptr;
    Ym2151DeviceAdapter* ym2151_ = nullptr;
    Cus30DeviceAdapter* cus30_ = nullptr;
    MameDerivedHd63701Adapter* mcu_ = nullptr;
    Dac8BitR2RDeviceAdapter dac0_;  // plain members, not DeviceManager-owned (no CPU-callback wiring needed)
    Dac8BitR2RDeviceAdapter dac1_;
    std::unique_ptr<DragonSpiritCommandInterface> command_interface_;
    std::array<std::unique_ptr<CallbackEndpoint>, 4> endpoints_;  // ym2151, cus30, dac0, dac1 (Initialize() order)

    ClockConverter clock_converter_;    // canonical ticks -> 1,536,000Hz M6809 cycles
    ClockConverter ym2151_from_m6809_;  // M6809 cycles -> YM2151 input clocks
    ClockConverter cus30_from_m6809_;   // M6809 cycles -> CUS30 input clocks
    ClockConverter mcu_clock_converter_;  // canonical ticks -> 1,536,000Hz MCU cycles; block-granular stepping

    std::uint64_t cumulative_ticks_processed_ = 0;  // canonical ticks, since Initialize()/Reset()
    std::uint64_t cumulative_cycles_executed_ = 0;  // M6809 cycles, since Initialize()/Reset()
    std::uint64_t block_base_cycles_ = 0;           // M6809 cycles executed in PRIOR blocks
    std::uint64_t sound_sync_cursor_ = 0;           // machine-lifetime cumulative M6809-cycle sync mark
    std::uint64_t vblank_irq_pulses_delivered_ = 0;
    std::uint64_t mcu_cumulative_cycles_executed_ = 0;  // MCU cycles, since Initialize()/Reset()
    std::uint32_t reset_count_for_test_ = 0;  // Reset() calls since construction (test-only observation)
    bool command_sent_since_reset_ = false;  // SendCommand() resets the Machine before the next command
    bool main_cpu_gate_write_done_ = false;  // one-shot latch for kMainCpuGateWriteTimeCanonicalTicks

    std::uint64_t ym2151_cycles_issued_ = 0;
    std::uint64_t cus30_cycles_issued_ = 0;

    std::vector<LogicalChannel> channels_;
    bool channels_registered_ = false;
    std::vector<std::vector<float>> ym2151_channel_scratch_;
    std::vector<std::vector<float>> cus30_channel_scratch_;
    std::vector<float> dac0_channel_scratch_;
    std::vector<float> dac1_channel_scratch_;

    MameDerivedM6809Adapter::OpcodeFetchObserver m6809_opcode_fetch_observer_for_test_;
    MameDerivedHd63701Adapter::OpcodeFetchObserver mcu_opcode_fetch_observer_for_test_;
    std::uint64_t ym2151_write_count_for_test_ = 0;
    Dac1WriteObserver dac1_write_observer_for_test_;
    Dac0WriteObserver dac0_write_observer_for_test_;
    TriRamGateByteModeForTest triram_gate_mode_for_test_ = TriRamGateByteModeForTest::Default;
    std::uint8_t triram_gate_force_value_for_test_ = 0;
    TriRamGateEventObserver triram_gate_write_observer_for_test_;
};
