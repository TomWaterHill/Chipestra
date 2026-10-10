// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "clock.h"
#include "device_manager.h"
#include "devices/cpu/m68000/mame_derived_m68000_adapter.h"
#include "devices/cpu/z80/mame_derived_z80_adapter.h"
#include "devices/sound/ay8910/ay8910_device_adapter.h"
#include "devices/sound/k005289/k005289_device_adapter.h"
#include "devices/sound/vlm5030/vlm5030_device_adapter.h"
#include "chipestra_abi.h"
#include "gradius_command_interface.h"
#include "gradius_main_memory_map.h"
#include "gradius_memory_map.h"

#include <cstdint>
#include <memory>
#include <vector>

// The Konami GX400/Bubble System sound subsystem: the sound Z80 (MameDerivedZ80Adapter)
// + GradiusMemoryMap/GradiusCommandInterface + AY-3-8910 x2 + Konami K005289 + Sanyo
// VLM5030, plus the main 68000 (MameDerivedM68000Adapter over GradiusMainMemoryMap),
// which the sound driver needs for its boot-time shared-RAM handshake.
//
// LogicalChannels: ay1's 3 tone generators (ids 0-2), ay2's 3 (ids 3-5), K005289's 2
// voices (ids 6-7), VLM5030's mono channel (id 8).
//
// Clocks: the Z80 and both AY8910s run at 14'318'180/8 Hz; K005289 and VLM5030 run at
// 14'318'180/4 Hz, exactly 2x that, so SyncSoundDevices() doubles the Z80-cycle mark
// instead of carrying extra ClockConverters.
//
// AY8910 ports: ay2's port A/B write callbacks drive K005289's ControlA/ControlB (the I/O
// port doubles as that chip's control bus). ay1's port A read callback reproduces
// nemesis_portA_r(): bits 0-3 from the free-running cycle counter (total_cycles()/512),
// bits 4/6/7 pinned high, bit 5 from the VLM5030 BSY status. Boot code branches on bit 7
// alone, so the whole formula is needed.
//
// Sound Z80 interrupts: IRQ = command latch (GradiusCommandInterface); NMI = the video
// screen's VBLANK pulse, independent of the IRQ path.
//
// Main 68000: runs the real main program against GradiusMainMemoryMap (ROM, RAM,
// shared-RAM bridge, character RAM, work RAM); the scattered I/O window
// (soundlatch/DSW/TEST/IN0-2/watchdog/outlatch/intlatch) is this class's MainIoBus.
// SendCommand() injects commands through GradiusCommandInterface directly; the main CPU's
// own soundlatch writes use the same interface but are ignored once the player has sent a
// command. The 68000 screen interrupts IRQ1/IRQ2/IRQ4 are needed because the boot code
// polls a flag byte that only the IRQ2 handler writes. They are independent
// level-triggered lines (gated by intlatch bit0 = irq2, bit1 = irq1, outlatch bit7 = irq4)
// and are cleared only when software writes the enable bit back to 0 (the 68000 has no
// auto-ack). IM68000Core takes one combined level, so UpdateMainInterruptLevel() computes
// the maximum of the asserted lines, as the external priority encoder does.
class GradiusMachine final : public IMachine {
public:
    // Z80/AY8910 clock: 14'318'180/8 = 1,789,772.5 Hz, rounded to whole Hz (a ~0.000028%
    // deviation, inaudible).
    static constexpr ClockRate kZ80ClockRate{1'789'773};

    // K005289's (and VLM5030's) fixed clock, 14'318'180/4 exactly. A plain Hz value (no
    // ClockConverter): the render budget is the Z80-domain cycle count doubled.
    static constexpr std::uint32_t kK005289ClockRate = 3'579'545;

    // Sound Z80 NMI: driven by the video screen's VBLANK (gx400_state::gx400()'s
    // screen_vblank -> audiocpu NMI), not by the command latch. The true Bubble System
    // config has this line commented out and uses outlatch bit4 instead. The vblank rate
    // is 60.606060 Hz (XTAL(18'432'000)/3 pixel clock / (384*264)), which gives exactly
    // 192000*33/2000 = 3168 canonical ticks per vblank, so a plain integer tick counter
    // suffices.
    static constexpr std::uint64_t kVblankNmiPeriodCanonicalTicks = 3168;

    // Screen-interrupt (IRQ1/IRQ2/IRQ4) scanline phase offsets within the same
    // 3168-tick/264-scanline frame: 18'432'000/3 Hz pixel clock / htotal 384 = 16'000 Hz
    // horizontal rate = exactly 12 canonical ticks per scanline. IRQ2 fires at scanline 0
    // (same phase as the VBLANK pulse), IRQ4 at scanline 120, IRQ1 at scanline 240 but
    // only on every other frame ((frame_number() & 1) == 0 in nemesis.cpp).
    static constexpr std::uint64_t kTicksPerScanline = 12;
    static constexpr std::uint64_t kIrq4ScanlineOffsetTicks = 120 * kTicksPerScanline;
    static constexpr std::uint64_t kIrq1ScanlineOffsetTicks = 240 * kTicksPerScanline;

    // Main 68000 clock: 18'432'000/2 = 9,216,000 Hz exactly.
    static constexpr ClockRate kMainCpuClockRate{9'216'000};

    // Boot warm-up for a Machine with no main-CPU ROM (e.g. synthetic-ROM tests): a command
    // can arrive before the first sink-bearing Render(), and the Z80 adapter services a
    // pending interrupt before fetching its first instruction, so the driver must be booted
    // first. With a main-CPU ROM present, RunBootWarmup() instead runs the adaptive warm-up
    // described at kBootWarmupMaxFrames, because the sound driver's shared-RAM handshake
    // takes seconds to clear.
    static constexpr std::uint64_t kBootWarmupFrames = 256ull * 24;

    // Upper bound of the adaptive warm-up used when a main-CPU ROM is present: chunks are
    // rendered (audio discarded) until the sound Z80's IFF1 is set (it can only be set once
    // the boot code passes the handshake poll loop) or this many canonical frames (30 s)
    // have elapsed. Real hardware clears the main CPU's first wait loop around t=22 s.
    // Warm-up is pure instruction execution, so a generous cap costs only build/test time.
    static constexpr std::uint64_t kBootWarmupMaxFrames = 192'000ull * 30;

    // Per-device routing gains. Each is the MAME route gain doubled to compensate the
    // mono->stereo centre pan. The adapters always expose independent channels, whereas
    // MAME sums ay1 (ALL_OUTPUTS, 0.20) and uses one route per channel for ay2 (1.00).
    static constexpr float kAy1RoutingGain = 0.40f;  // MAME route 0.20 x2 (mono->stereo centre-pan compensation)
    static constexpr float kAy2RoutingGain = 2.00f;  // MAME route 1.00 x2 (mono->stereo centre-pan compensation)

    // K005289: MAME route ALL_OUTPUTS 0.35 (summing both voices); the adapter exposes two
    // channels instead.
    static constexpr float kK005289RoutingGain = 0.70f;  // MAME route 0.35 x2 (mono->stereo centre-pan compensation)

    // VLM5030: MAME route ALL_OUTPUTS 0.70.
    static constexpr float kVlm5030RoutingGain = 1.40f;  // MAME route 0.70 x2 (mono->stereo centre-pan compensation)

    // `z80_rom` is loaded into the sound Z80's ROM region during Initialize().
    // `k005289_waveform_prom` is copied into the K005289 adapter (zero-padded/truncated to
    // its size). `main_cpu_program`/`main_cpu_data` are loaded into the main 68000's ROM
    // regions; empty means no main CPU program (see kBootWarmupFrames).
    GradiusMachine(std::vector<std::uint8_t> z80_rom, std::vector<std::uint8_t> k005289_waveform_prom,
                    std::vector<std::uint8_t> main_cpu_program = {}, std::vector<std::uint8_t> main_cpu_data = {});
    ~GradiusMachine() override;

    void Initialize() override;
    void Reset() override;
    void Shutdown() override;
    std::vector<LogicalChannel>& LogicalChannels() override;
    void Render(EmuDuration duration, LogicalChannelSink* sink) override;
    void SendCommand(const MusicCommand& command) override;

    // Test-only observation points; not part of IMachine.
    MameDerivedZ80Adapter::RegisterSnapshot Z80Snapshot() const;
    std::uint8_t ReadMemory(std::uint16_t address) const;
    Ay8910DeviceAdapter& Ay1ForTest() { return *ay1_; }
    Ay8910DeviceAdapter& Ay2ForTest() { return *ay2_; }
    K005289DeviceAdapter& K005289ForTest() { return *k005289_; }
    Vlm5030DeviceAdapter& Vlm5030ForTest() { return *vlm5030_; }
    // Test/diagnostics only: keep the ~21 s start-up jingle (VLM5030 announcement included)
    // audible in the first Render() calls instead of discarding it (SkipBootJingle()). Set
    // BEFORE Initialize().
    void SetSkipBootJingleForTest(bool skip) { skip_boot_jingle_ = skip; }
    MameDerivedZ80Adapter& Z80ForTest() { return *z80_; }
    MameDerivedM68000Adapter& MainCpuForTest() { return *main_cpu_; }
    std::uint8_t ReadMainMemory(std::uint32_t address) const { return main_memory_map_->Read8(address); }

    // Test-only observers registered BEFORE Initialize() (stored here and applied to
    // main_cpu_ when Initialize() constructs it, before RunBootWarmup() runs), so the first
    // and only boot pass is the observed one. Attaching through MainCpuForTest() would need
    // main_cpu_ to exist already, and a later Reset() does not clear main_memory_map_'s RAM,
    // so a second pass is not a true cold boot.
    void SetMainCpuOpcodeFetchObserverForTest(MameDerivedM68000Adapter::OpcodeFetchObserver observer) {
        main_cpu_opcode_fetch_observer_for_test_ = std::move(observer);
    }
    void SetMainCpuMemoryWriteObserverForTest(MameDerivedM68000Adapter::MemoryWriteObserver observer) {
        main_cpu_memory_write_observer_for_test_ = std::move(observer);
    }
    void SetMainCpuIllegalInstructionObserverForTest(MameDerivedM68000Adapter::IllegalInstructionObserver observer) {
        main_cpu_illegal_instruction_observer_for_test_ = std::move(observer);
    }

    // Same, for the sound Z80.
    void SetZ80OpcodeFetchObserverForTest(MameDerivedZ80Adapter::OpcodeFetchObserver observer) {
        z80_opcode_fetch_observer_for_test_ = std::move(observer);
    }
    void SetZ80MemoryWriteObserverForTest(MameDerivedZ80Adapter::MemoryWriteObserver observer) {
        z80_memory_write_observer_for_test_ = std::move(observer);
    }

private:
    // AddressSpaceEndpoint mapped at GradiusMemoryMap::kIoStart: dispatches the scattered
    // soundlatch read, AY8910 x2, K005289 tg1_w/tg2_w and VLM5030 speech_w addresses
    // (nemesis_filter_w is ignored). Defined in gradius_machine.cpp (internal glue, not a
    // modeled chip).
    class IoBus;

    // AddressSpaceEndpoint for the K005289 ld1/ld2 windows: forwards every write within its
    // 4KB window to K005289DeviceAdapter::Ld1()/Ld2() (the write address itself is the
    // latched value; the chip has no data bus). One instance per voice (`isVoice2`).
    class K005289LatchBus;

    // AddressSpaceEndpoint mapped into GradiusMainMemoryMap at its kIoStart/kIoSize:
    // dispatches the main CPU's soundlatch/DSW/TEST/IN0-2/watchdog/outlatch/intlatch
    // addresses.
    class MainIoBus;

    // Advances ay1/ay2/k005289/vlm5030 to the given mark on the current Render()'s
    // Z80-cycle timeline (0 at the top of Render()); the K005289 and VLM5030 deltas are
    // doubled. Called before each sound-device bus access and once at the end of the block.
    void SyncSoundDevices(std::uint64_t z80CycleMark);

    // Cumulative Z80 cycles consumed so far within the current Render() block, across all
    // Execute() calls (z80_->CyclesConsumedThisCall() only covers the in-flight one).
    std::uint64_t CurrentBlockCycles() const { return block_base_cycles_ + z80_->CyclesConsumedThisCall(); }

    // Runs kBootWarmupFrames in small kWarmupChunkFrames Render() calls instead of one
    // giant call: the VBLANK-NMI pulse count is computed before the Z80 executes the block,
    // so one 6144-tick call would deliver an NMI before the reset vector ran a single
    // instruction and corrupt SP (PUSH AF against SP=0).
    void RunBootWarmup();
    void SkipBootJingle();
    static constexpr std::uint64_t kWarmupChunkFrames = 256;

    // Recomputes the main CPU's combined interrupt level from the irqN_asserted_ states and
    // pushes it via main_cpu_->SetInterruptLevel(). Called from Render() and from MainIoBus
    // when software clears an enable bit. Idempotent.
    void UpdateMainInterruptLevel();

    std::vector<std::uint8_t> z80_rom_;
    std::vector<std::uint8_t> k005289_waveform_prom_;
    std::vector<std::uint8_t> main_cpu_program_;
    std::vector<std::uint8_t> main_cpu_data_;
    GradiusMemoryMap memory_map_;
    // Constructed in Initialize() (needs `this`-capturing shared-RAM callbacks).
    std::unique_ptr<GradiusMainMemoryMap> main_memory_map_;
    DeviceManager device_manager_;
    // Concrete type (not IZ80Core*). Non-owning; owned by device_manager_.
    MameDerivedZ80Adapter* z80_ = nullptr;
    MameDerivedM68000Adapter* main_cpu_ = nullptr;
    // Pre-Initialize() test observers; applied to main_cpu_ right after construction.
    MameDerivedM68000Adapter::OpcodeFetchObserver main_cpu_opcode_fetch_observer_for_test_;
    MameDerivedM68000Adapter::MemoryWriteObserver main_cpu_memory_write_observer_for_test_;
    MameDerivedM68000Adapter::IllegalInstructionObserver main_cpu_illegal_instruction_observer_for_test_;
    // Same, for the sound Z80.
    MameDerivedZ80Adapter::OpcodeFetchObserver z80_opcode_fetch_observer_for_test_;
    MameDerivedZ80Adapter::MemoryWriteObserver z80_memory_write_observer_for_test_;
    Ay8910DeviceAdapter* ay1_ = nullptr;
    Ay8910DeviceAdapter* ay2_ = nullptr;
    K005289DeviceAdapter* k005289_ = nullptr;
    Vlm5030DeviceAdapter* vlm5030_ = nullptr;
    std::unique_ptr<GradiusCommandInterface> command_interface_;
    std::unique_ptr<IoBus> io_bus_;
    std::unique_ptr<MainIoBus> main_io_bus_;
    std::unique_ptr<K005289LatchBus> k005289_ld1_bus_;
    std::unique_ptr<K005289LatchBus> k005289_ld2_bus_;
    ClockConverter clock_converter_;  // canonical ticks -> 1,789,773Hz Z80/AY8910 cycles
    // canonical ticks -> 9,216,000Hz main-CPU cycles (the two CPUs run at unrelated clocks).
    ClockConverter main_clock_converter_;
    // Bresenham-style carry-forward for the main CPU, tracked independently.
    std::uint64_t main_cumulative_ticks_processed_ = 0;
    std::uint64_t main_cumulative_cycles_executed_ = 0;
    // Bresenham-style carry-forward for the sound Z80.
    std::uint64_t cumulative_ticks_processed_ = 0;
    std::uint64_t cumulative_cycles_executed_ = 0;
    // Number of VBLANK NMI pulses already delivered.
    std::uint64_t vblank_nmi_pulses_delivered_ = 0;
    // Screen interrupts (IRQ1/IRQ2/IRQ4; see the class comment). *_enabled_ mirrors
    // nemesis.cpp's m_irqN_on (gates whether a scanline crossing asserts the line).
    // *_asserted_ is the level-triggered line state, cleared only when software disables
    // the enable bit. *_pulses_delivered_ is the due-pulse counter for each line's scanline
    // phase (IRQ1 also skips every other frame).
    bool irq1_enabled_ = false;
    bool irq2_enabled_ = false;
    bool irq4_enabled_ = false;
    bool irq1_asserted_ = false;
    bool irq2_asserted_ = false;
    bool irq4_asserted_ = false;
    std::uint64_t irq1_pulses_delivered_ = 0;
    std::uint64_t irq2_pulses_delivered_ = 0;
    std::uint64_t irq4_pulses_delivered_ = 0;
    // Z80-cycle mark the sound devices have been advanced to within the current Render().
    std::uint64_t sound_sync_cursor_ = 0;
    // Cycles consumed by Execute() calls before the in-flight one within the current
    // Render() block; reset to 0 at the top of every Render().
    std::uint64_t block_base_cycles_ = 0;
    // True from GradiusCommandInterface::Tick() asserting IRQ until the CPU takes it
    // (Render()'s IRQ-priming loop). Carries across blocks if interrupts are not enabled
    // yet, like a level-triggered line staying high until acknowledged.
    bool irq_awaiting_ack_ = false;
    // Music-player takeover: once the player has sent its first command, the real 68000's
    // sound-latch writes / sound-IRQ pulses are ignored so only the selected track plays.
    // Cleared by Reset()/Initialize().
    bool player_owns_sound_latch_ = false;
    bool skip_boot_jingle_ = true;
    std::vector<LogicalChannel> channels_;
    // Registered with the sink on Render()'s first call.
    bool channels_registered_ = false;
    std::vector<std::vector<float>> ay1_channel_scratch_;
    std::vector<std::vector<float>> ay2_channel_scratch_;
    std::vector<std::vector<float>> k005289_voice_scratch_;
    std::vector<float> vlm5030_channel_scratch_;  // single mono channel, not vector-of-vectors
};
