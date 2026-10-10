// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "clock.h"
#include "device_manager.h"
#include "devices/cpu/m68000/mame_derived_m68000_adapter.h"
#include "devices/cpu/z80/mame_derived_z80_adapter.h"
#include "devices/konami/bubblesystem/bubble_system_main_memory_map.h"
#include "devices/konami/gradius/gradius_command_interface.h"
#include "devices/konami/gradius/gradius_memory_map.h"
#include "devices/sound/ay8910/ay8910_device_adapter.h"
#include "devices/sound/k005289/k005289_device_adapter.h"
#include "devices/sound/vlm5030/vlm5030_device_adapter.h"
#include "chipestra_abi.h"

#include <cstdint>
#include <memory>
#include <vector>

// The real Konami Bubble System (bubsys_state in nemesis.cpp), as distinct from
// GradiusMachine's GX400/Mask-ROM revision (gx400_state). It reuses the same sound-side
// adapter types and GradiusMemoryMap/GradiusCommandInterface unchanged (bubsys_state
// inherits gx400_state and never overrides sound_map()), but has a wholly different
// main-CPU side: no real 68000 program ROM, only a 480-byte boot program that the K005297
// bubble-memory controller copies into shared RAM once the bubble memory has warmed up;
// the boot program then reads the real game code from the cartridge one 128-byte page at
// a time (BubbleSystemMainMemoryMap emulates the K005297). The memory map, boot model and
// interrupt scheme all differ from GX400, so this is a separate Machine; the sound-side
// wiring is duplicated rather than shared.
//
// NMI delivery: GX400's automatic `screen_vblank -> audiocpu NMI` wiring is commented out
// in bubsys_state's machine config. The real mechanism is software: the 68000 boot code
// toggles outlatch bit 4 (sound_nmi_w), and MainIoBus wires that bit directly to
// z80_->SetInputLine(Nmi, ...), so no due-pulse counter is needed.
//
// Interrupt scheme (bubsys_interrupt): IRQ2 fires at scanline 0 every frame. IRQ1 also
// fires at scanline 0 (GX400: 240), on every other frame. IRQ4 is a free-running
// 72-scanline counter that VBLANK does not reset ("a sort of constant timer"). Level 5
// (HOLD_LINE/auto-ack on real hardware) fires when the K005297 finishes a page read
// (BubbleSystemMainMemoryMap's onPageReadComplete callback); UpdateMainInterruptLevel()
// treats it as the highest priority and it auto-clears at the end of the Render() block
// that raised it.
// Board configuration. WithCartridge is the full board: the main 68000 runs the boot
// program, which loads a second-stage loader from the bubble-memory cartridge and executes
// it. BiosOnly models the K005297's bubble-memory warm-up phase, before it releases the
// 68000: the main CPU never runs, and the sound Z80 - which alone speaks the "Getting
// ready... Fifty..." countdown and plays the morning music, both from the BIOS sound ROM -
// gets the one thing the 68000's boot code would otherwise supply, a per-frame NMI. It needs
// only the three BIOS sound resources (soundcpu/wave0/wave1); no cartridge.
enum class BubbleSystemBoardMode {
    WithCartridge,
    BiosOnly,
};

class BubbleSystemMachine final : public IMachine {
public:
    // Identical to GradiusMachine's constants: `gx400()` and `bubsys()` specify the same
    // clocks for every chip and the same screen raw params, and `bubsys()` delegates its
    // sound-hardware config to the same `gx400_common_sound()`.
    static constexpr ClockRate kZ80ClockRate{1'789'773};
    static constexpr std::uint32_t kK005289ClockRate = 3'579'545;
    static constexpr std::uint64_t kVblankNmiPeriodCanonicalTicks = 3168;
    static constexpr std::uint64_t kTicksPerScanline = 12;

    // IRQ4's free-running period: 72 scanlines, not phase-locked to VBLANK.
    static constexpr std::uint64_t kIrq4PeriodTicks = 72 * kTicksPerScanline;

    // Main 68000 clock: 18'432'000/2, identical to GradiusMachine's.
    static constexpr ClockRate kMainCpuClockRate{9'216'000};

    // Boot warm-up: a command can arrive before the first Render(), so Initialize() runs
    // the driver's boot sequence first. Unlike GradiusMachine there is always a boot program
    // (the main region is RAM populated from it), so the adaptive warm-up is always used.
    static constexpr std::uint64_t kBootWarmupMaxFrames = 192'000ull * 30;
    static constexpr std::uint64_t kWarmupChunkFrames = 256;

    // Per-device routing gains: the same as GradiusMachine's (identical chip set and
    // clocks). Each is the MAME route gain doubled to compensate the mono->stereo centre pan.
    static constexpr float kAy1RoutingGain = 0.40f;  // MAME route 0.20 x2 (mono->stereo centre-pan compensation)
    static constexpr float kAy2RoutingGain = 2.00f;  // MAME route 1.00 x2 (mono->stereo centre-pan compensation)
    static constexpr float kK005289RoutingGain = 0.70f;  // MAME route 0.35 x2 (mono->stereo centre-pan compensation)
    // VLM5030: MAME route 0.70 x2 pan compensation, as in GradiusMachine. With this gain the
    // countdown's chip output peaks at 0.256 (like MAME) and no sample clips.
    static constexpr float kVlm5030RoutingGain = 1.40f;

    // `z80_rom`/`k005289_waveform_prom` have the same shape as GradiusMachine's, but different
    // content (the Bubble System's audiocpu ROM is a different program from GX400's).
    // `boot_program` is the 480-byte boot image, copied into RAM at Initialize() (not mapped
    // as ROM). `bubble_memory` is the cartridge image, already de-swapped to MAME's internal
    // byte order.
    BubbleSystemMachine(std::vector<std::uint8_t> z80_rom, std::vector<std::uint8_t> k005289_waveform_prom,
                         std::vector<std::uint8_t> boot_program, std::vector<std::uint8_t> bubble_memory,
                         BubbleSystemBoardMode board_mode = BubbleSystemBoardMode::WithCartridge);
    ~BubbleSystemMachine() override;

    void Initialize() override;
    void Reset() override;
    void Shutdown() override;
    std::vector<LogicalChannel>& LogicalChannels() override;
    void Render(EmuDuration duration, LogicalChannelSink* sink) override;
    void SendCommand(const MusicCommand& command) override;

    // Test-only observation points; mirrors GradiusMachine's set.
    MameDerivedZ80Adapter::RegisterSnapshot Z80Snapshot() const;
    std::uint8_t ReadMemory(std::uint16_t address) const;
    Ay8910DeviceAdapter& Ay1ForTest() { return *ay1_; }
    Ay8910DeviceAdapter& Ay2ForTest() { return *ay2_; }
    K005289DeviceAdapter& K005289ForTest() { return *k005289_; }
    Vlm5030DeviceAdapter& Vlm5030ForTest() { return *vlm5030_; }
    MameDerivedZ80Adapter& Z80ForTest() { return *z80_; }
    MameDerivedM68000Adapter& MainCpuForTest() { return *main_cpu_; }
    std::uint8_t ReadMainMemory(std::uint32_t address) const { return main_memory_map_->Read8(address); }

    // Pre-Initialize() observer registration, as in GradiusMachine.
    void SetMainCpuOpcodeFetchObserverForTest(MameDerivedM68000Adapter::OpcodeFetchObserver observer) {
        main_cpu_opcode_fetch_observer_for_test_ = std::move(observer);
    }
    void SetMainCpuMemoryWriteObserverForTest(MameDerivedM68000Adapter::MemoryWriteObserver observer) {
        main_cpu_memory_write_observer_for_test_ = std::move(observer);
    }
    void SetMainCpuIllegalInstructionObserverForTest(MameDerivedM68000Adapter::IllegalInstructionObserver observer) {
        main_cpu_illegal_instruction_observer_for_test_ = std::move(observer);
    }
    void SetZ80OpcodeFetchObserverForTest(MameDerivedZ80Adapter::OpcodeFetchObserver observer) {
        z80_opcode_fetch_observer_for_test_ = std::move(observer);
    }
    void SetZ80MemoryWriteObserverForTest(MameDerivedZ80Adapter::MemoryWriteObserver observer) {
        z80_memory_write_observer_for_test_ = std::move(observer);
    }

private:
    // Scattered-glue endpoints, as in GradiusMachine (see gradius_machine.cpp).
    class IoBus;
    class K005289LatchBus;
    class MainIoBus;

    void SyncSoundDevices(std::uint64_t z80CycleMark);
    std::uint64_t CurrentBlockCycles() const { return block_base_cycles_ + z80_->CyclesConsumedThisCall(); }

    void RunBootWarmup();

    // Recomputes the main CPU's combined interrupt level from the four independent line
    // states (irq1/2/4_asserted_ plus irq5_pending_). Called from Render() and from MainIoBus
    // when software clears an enable bit.
    void UpdateMainInterruptLevel();

    std::vector<std::uint8_t> z80_rom_;
    std::vector<std::uint8_t> k005289_waveform_prom_;
    std::vector<std::uint8_t> boot_program_;
    std::vector<std::uint8_t> bubble_memory_;
    GradiusMemoryMap memory_map_;
    std::unique_ptr<BubbleSystemMainMemoryMap> main_memory_map_;
    DeviceManager device_manager_;
    MameDerivedZ80Adapter* z80_ = nullptr;
    MameDerivedM68000Adapter* main_cpu_ = nullptr;
    MameDerivedM68000Adapter::OpcodeFetchObserver main_cpu_opcode_fetch_observer_for_test_;
    MameDerivedM68000Adapter::MemoryWriteObserver main_cpu_memory_write_observer_for_test_;
    MameDerivedM68000Adapter::IllegalInstructionObserver main_cpu_illegal_instruction_observer_for_test_;
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
    ClockConverter clock_converter_;
    ClockConverter main_clock_converter_;
    std::uint64_t main_cumulative_ticks_processed_ = 0;
    std::uint64_t main_cumulative_cycles_executed_ = 0;
    std::uint64_t cumulative_ticks_processed_ = 0;
    std::uint64_t cumulative_cycles_executed_ = 0;

    // Screen interrupts (see the class comment). *_enabled_ mirrors nemesis.cpp's m_irqN_on.
    // *_asserted_ is the level-triggered state, cleared only when software disables the
    // enable bit (the 68000 has no auto-ack). *_pulses_delivered_ is the due-pulse counter for
    // IRQ2/IRQ1 (both at scanline phase 0 here) and IRQ4 (its own 72-scanline period).
    bool irq1_enabled_ = false;
    bool irq2_enabled_ = false;
    bool irq4_enabled_ = false;
    bool irq1_asserted_ = false;
    bool irq2_asserted_ = false;
    bool irq4_asserted_ = false;
    std::uint64_t irq1_pulses_delivered_ = 0;
    std::uint64_t irq2_pulses_delivered_ = 0;
    std::uint64_t irq4_pulses_delivered_ = 0;

    // IRQ5 (K005297 page-read completion, HOLD_LINE on real hardware). Raised synchronously
    // from main_cpu_'s in-progress Execute() call, so dropping it after that call returns is
    // enough: the next instruction boundary in the same call is guaranteed to see it.
    bool irq5_pending_ = false;

    // Sound NMI: driven directly by the 68000's outlatch-bit-4 write (MainIoBus); no
    // due-pulse counter.
    bool sound_nmi_level_ = false;

    // BiosOnly mode: the per-frame sound NMI this Machine itself generates (no 68000 exists
    // to toggle outlatch bit 4), paced by the same due-pulse formula as GradiusMachine's
    // vblank NMI (kVblankNmiPeriodCanonicalTicks).
    BubbleSystemBoardMode board_mode_ = BubbleSystemBoardMode::WithCartridge;
    std::uint64_t nmi_pulses_delivered_ = 0;

    std::uint64_t sound_sync_cursor_ = 0;
    std::uint64_t block_base_cycles_ = 0;
    bool irq_awaiting_ack_ = false;
    // Music-player takeover: once the player has sent its first command, the real 68000's
    // sound-latch writes / sound-IRQ pulses are ignored so only the selected track plays.
    // Cleared by Reset()/Initialize().
    bool player_owns_sound_latch_ = false;
    void ClearAllRamForColdBoot();
    std::vector<LogicalChannel> channels_;
    bool channels_registered_ = false;
    std::vector<std::vector<float>> ay1_channel_scratch_;
    std::vector<std::vector<float>> ay2_channel_scratch_;
    std::vector<std::vector<float>> k005289_voice_scratch_;
    std::vector<float> vlm5030_channel_scratch_;
};
