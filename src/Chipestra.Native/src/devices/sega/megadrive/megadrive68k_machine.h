// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "chipestra_abi.h"
#include "clock.h"
#include "devices/cpu/m68000/mame_derived_m68000_adapter.h"
#include "devices/cpu/z80/mame_derived_z80_adapter.h"
#include "devices/sega/megadrive/megadrive_machine.h"
#include "devices/sound/sn76489/sn76489_sound_device_adapter.h"
#include "devices/sound/ym2612/ym2612_device_adapter.h"
#include "resource_resolver.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The settings of a Mega Drive game whose sound driver runs on the 68000 (read from the game definition XML's <driver>
// element). Every number is an address of the 68000's bus; nothing here is a ROM byte.
//
//   <driver m68k_init="0x134C" m68k_frame="0x71B4C" request="0xFFF00A" warmup_frames="4" region="usa|europe" cart_crc32="afe05eee"/>
//
//   m68k_init      subroutines of the game that the 68000 calls once after the reset, in this order (each ends with RTS): the
//                  game's own routines that load the Z80's program and set up the sound driver
//   m68k_frame     the subroutine(s) the game's vertical-blank interrupt handler calls every frame to run the sound driver
//                  (several addresses, separated by spaces, are called in this order)
//   request        the 68000 address of the byte (in the game's RAM) in which the game puts the number of a song or an
//                  effect for the driver to take at its next frame
//   requests       instead of `request`: the subroutines of the game that start a song or an effect (a driver the game
//                  calls with the number in D0 and D1 instead of reading it from a byte); a track names one with
//                  request="..." and the Machine calls it at the next vertical-blank interrupt, before the sound routine
//   vblank_handler "game": the game's own vertical-blank handler (the vector of the cartridge) runs as it is, instead of a
//                  generated one that calls the m68k_frame routines (then m68k_frame is not needed)
//   hblank_lines   the horizontal-blank interrupt (level 4) comes every this many scanlines (the VDP's register 10 plus one;
//                  1-256), first after that many lines of each frame and not after line 223, and runs the game's own handler
//                  (the vector of the cartridge); without it the interrupt is never raised
//   m68k_a6        the value of A6 when the initialization begins (a game whose handlers take a base register for granted)
//   m68k_ram_words words that the Machine writes into the game's RAM before the initialization routines, as "address=value"
//                  pairs separated by spaces (the settings a game's own start-up code leaves for its sound driver)
//   request_arg    "pointer": the routines of `requests` get A0 pointing at a word on the stack that holds the number (and no
//                  number in D0/D1)
//   z80_bus        "instruction_end": a change of the Z80's bus request takes effect at the end of the 68000 instruction that makes it
//                  (the Z80 runs on to that time before it stops, and takes up again from that time); by default it takes effect
//                  at the start of that instruction
//   z80_reset      "released": the Z80 is not held in reset when the initialization begins (the game's own start-up code has
//                  released it before it sets the sound driver up); by default the reset is held
//   warmup_frames  how many frames the Machine runs after the initialization before it first writes a request (the state
//                  after them is kept and restored at every switch)
//   region, cart_crc32   as for MegaDriveMachine
struct MegaDrive68kDriverConfig {
    MegaDriveRegion region = MegaDriveRegion::Japan;
    std::vector<std::uint32_t> init;
    std::vector<std::uint32_t> frames;           // the sound routines, called in this order at every vertical-blank interrupt
    std::uint32_t requestAddress = 0;
    std::vector<std::uint32_t> requestRoutines;   // see `requests`; when not empty, requestAddress is not used
    std::uint32_t warmupFrames = 0;
    std::optional<std::uint32_t> cartridgeCrc32;
    bool z80ResetReleased = false;               // see `z80_reset`
    bool vblankHandlerIsGame = false;            // see `vblank_handler`
    std::uint32_t hblankLines = 0;               // see `hblank_lines` (0: none)
    std::optional<std::uint32_t> a6;             // see `m68k_a6`
    bool requestByPointer = false;               // see `request_arg`
    bool z80BusAtInstructionEnd = false;         // see `z80_bus`
    std::vector<std::pair<std::uint32_t, std::uint16_t>> ramWords;   // see `m68k_ram_words`
};

bool ParseMegaDrive68kDriverConfig(std::string_view driverXml, MegaDrive68kDriverConfig& out, std::string* error);

// A Mega Drive running only what the sound driver of a game needs: the 68000 (with its 64 KB RAM and the cartridge), the
// Z80 with its 8 KB RAM, the YM2612, the PSG, and the bus between them (the Z80 bus request and reset registers, the bank
// register and the window onto the 68000's bus). The VDP is not run: the Machine raises its vertical-blank interrupt on
// both CPUs and answers the few registers a driver can read.
//
// What runs on the 68000: a few bytes of generated code (placed in an unused part of the bus, the way the game's own
// reset and interrupt vectors are replaced) call the game's initialization routines after the reset and the game's sound
// routine at every vertical-blank interrupt. The game's own main loop, its graphics and its controllers do not run;
// the request for a song is written into the game's RAM by the Machine, as the game's main loop would.
//
// Time: one counter of master clock ticks (a 68000 cycle is 7 ticks, a Z80 cycle 15). The CPU that is behind runs one
// instruction at a time, so the two are never more than an instruction apart. A change of the Z80's bus request takes effect
// at the start of the 68000 instruction that made it, or, with z80_bus="instruction_end", at the end of it (the Z80 runs on to
// that time before it stops, and takes up again from that time when the bus is given back); a Z80 held in reset stops and
// starts at once. The Z80 does not wait for the 68000's bus when it reads the cartridge through its window (the original
// delays it by a few cycles).
//
// Interrupts: the vertical-blank interrupt (level 6, line 224), the horizontal-blank interrupt (level 4, when hblank_lines
// is given) and the call requests (level 2) are held until the 68000 takes them, the highest first.
//
// Switching: a command sent after another one starts everything again; the state after the initialization and
// warmup_frames is kept and restored.
//
// LogicalChannels: FM 1-6 and DAC (device "YM2612"), Tone 1-3 and Noise (device "SN76489"), as MegaDriveMachine.
class MegaDrive68kMachine final : public IMachine {
public:
    static constexpr std::uint32_t kYmChannelCount = Ym2612DeviceAdapter::kChannelCount;
    static constexpr std::uint32_t kPsgFirstChannel = kYmChannelCount;
    static constexpr float kYmRoutingGain = 0.5f;
    static constexpr float kPsgRoutingGain = 0.5f;
    // Where the generated code is (68000 bus addresses in the unused area).
    static constexpr std::uint32_t kStubBase = 0x500000;
    static constexpr std::uint32_t kStubResetAddress = kStubBase;
    static constexpr std::uint32_t kStubInterruptAddress = kStubBase + 0x100;
    static constexpr std::uint32_t kStubReturnAddress = kStubBase + 0x200;   // an RTE
    // The call request: a routine address (long, 0 = none) and the number for D0/D1 (word), read by the interrupt code.
    static constexpr std::uint32_t kStubRequestRoutine = kStubBase + 0x280;
    static constexpr std::uint32_t kStubRequestValue = kStubBase + 0x284;
    static constexpr std::uint32_t kStubCallAddress = kStubBase + 0x300;     // the level 2 interrupt: the call request

    MegaDrive68kMachine(std::vector<std::uint8_t> cartridge, MegaDrive68kDriverConfig driver);

    void Initialize() override;
    void Reset() override;
    void Shutdown() override;
    std::vector<LogicalChannel>& LogicalChannels() override { return channels_; }
    void Render(EmuDuration duration, LogicalChannelSink* sink) override;
    void SendCommand(const MusicCommand& command) override;
    bool AcceptsCommand(const MusicCommand& command) const override;

    // ---- test and diagnostic observation points (not part of production playback) ----
    // Called for every write to the YM2612 (kind 'Y', port 0-3) or the PSG (kind 'P') with the time in master clock
    // ticks since power-on and the CPU that wrote ('6' the 68000, 'Z' the Z80).
    using ChipWriteObserver = std::function<void(std::uint64_t ticks, char cpu, char kind, std::uint32_t port, std::uint8_t value)>;
    void SetChipWriteObserverForTest(ChipWriteObserver observer) { chip_write_observer_ = std::move(observer); }
    // Called when the 68000 gives or takes back the Z80's bus (value true: requested).
    using BusObserver = std::function<void(std::uint64_t ticks, bool requested)>;
    void SetBusObserverForTest(BusObserver observer) { bus_observer_ = std::move(observer); }
    // Called for every 68000 write to RAM whose address is within [low, high].
    using RamWriteObserver = std::function<void(std::uint64_t ticks, std::uint32_t address, std::uint8_t value)>;
    void SetRamWriteObserverForTest(std::uint32_t low, std::uint32_t high, RamWriteObserver observer) {
        ram_observer_low_ = low; ram_observer_high_ = high; ram_observer_ = std::move(observer);
    }
    // Called for every 68000 write to the Z80's RAM (offset 0-0x1FFF) while it holds the bus.
    using Z80RamWriteObserver = std::function<void(std::uint64_t ticks, std::uint32_t offset, std::uint8_t value)>;
    void SetZ80RamWriteObserverForTest(Z80RamWriteObserver observer) { z80_ram_observer_ = std::move(observer); }
    MameDerivedM68000Adapter& CpuForTest() { return *m68k_; }
    MameDerivedZ80Adapter& Z80ForTest() { return *z80_; }
    std::uint8_t RamForTest(std::uint32_t address) const { return ram_[address & 0xFFFF]; }
    void PokeRamForTest(std::uint32_t address, std::uint8_t value) { ram_[address & 0xFFFF] = value; }
    // The 68000's vertical-blank interrupts are raised at these times (master clock ticks since power-on, increasing) instead
    // of once a frame: for replaying a recording of a game whose main program kept interrupts off for a while. (The Z80's
    // interrupt stays at its frame.)
    void SetVblankScheduleForTest(std::vector<std::uint64_t> times) {
        vint_schedule_ = std::move(times);
        vint_schedule_index_ = 0;
        vint_schedule_active_ = true;
    }
    std::uint8_t Z80RamForTest(std::uint16_t address) const { return z80_ram_[address & 0x1FFF]; }
    std::uint64_t TicksForTest() const { return std::min(t68_, TimeZ80()); }
    const MegaDriveTiming& TimingForTest() const { return timing_; }
    bool Z80BusRequestedForTest() const { return bus_requested_; }
    // false: every switch runs the initialization and warm-up again instead of restoring the saved state.
    void SetWarmStateRestoreForTest(bool enabled) { restore_warm_state_ = enabled; }
    // Runs the Machine to `ticks` (master clock ticks since power-on) with the chips running (a driver that polls the
    // YM2612's busy flag only gets on this way); the sound is kept until Render() takes it.
    void RunToTicksForTest(std::uint64_t ticks) { RunTo(ticks, true); }
    // Plays the sound from `ticks` on: Render() delivers it from there. (Used with RunToTicksForTest.)
    void BeginSoundForTest(std::uint64_t ticks);
    // Makes a request of a game with <driver requests> at the current time without starting again from the saved state
    // (SendCommand does): the 68000 calls entry `routineNumber` (1..) of `requests` with `value` in D0 and D1.
    // Several requests made one after the other are called in this order, each when the 68000 is idle again.
    void CallRequestForTest(std::uint32_t routineNumber, std::uint8_t value) { call_queue_.emplace_back(routineNumber, value); }

private:
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
    struct WarmState {
        bool valid = false;
        std::array<std::uint8_t, 0x10000> ram{};
        std::array<std::uint8_t, 0x2000> z80Ram{};
        MameDerivedM68000Adapter::SavedState m68k;
        MameDerivedZ80Adapter::SavedState z80;
        Ym2612DeviceAdapter::SavedState ym;
        Sn76489SoundDeviceAdapter::SavedState psg;
        std::uint32_t bank = 0;
        bool busRequested = false, z80Reset = false, irqHigh = false, vintPending = false, hintPending = false, stopPending = false, resumePending = false;
        std::uint64_t nextHint = 0;
        std::uint64_t t68 = 0, tz80 = 0, nextVblank = 0, nextZ80IrqEdge = 0, ymSamplesMade = 0, psgSamplesMade = 0;
    };

    void ColdStart();
    void UpdateIrqLevel();
    void SetNextHint(std::uint64_t afterTicks);
    void QueueCall(std::uint32_t routineNumber, std::uint8_t value);
    void BuildStub();
    void ResetMachineState();
    void RunTo(std::uint64_t targetTicks, bool keepSound);
    void ProcessEvents(std::uint64_t ticks);
    std::uint64_t TimeZ80() const { return z80_stopped_ ? std::max(tz80_, t68_) : tz80_; }

    // The 68000's bus.
    std::uint8_t Read68k(std::uint32_t address);
    void Write68k(std::uint32_t address, std::uint8_t value);
    std::uint8_t ReadZ80Area(std::uint32_t offset);
    void WriteZ80Area(std::uint32_t offset, std::uint8_t value);
    std::uint8_t RomByte(std::uint32_t address) const;
    // The Z80's bus.
    std::uint8_t ReadZ80Bus(std::uint16_t address);
    void WriteZ80Bus(std::uint16_t address, std::uint8_t value);
    void SetBusRequest(bool requested);
    void SetZ80Reset(bool reset);

    std::uint64_t AccessTime68k() const { return t68_; }
    std::uint64_t AccessTimeZ80() const { return tz80_ + 15ull * z80_->CyclesConsumedThisCall(); }
    void SyncYm(std::uint64_t ticks);
    void SyncPsg(std::uint64_t ticks);
    void PsgWrite(std::uint64_t ticks, char cpu, std::uint8_t value);
    void YmWrite(std::uint64_t ticks, char cpu, std::uint32_t port, std::uint8_t value);
    void UpdateRouting();
    void WriteChannels(LogicalChannelSink* sink);

    std::vector<std::uint8_t> cartridge_;
    MegaDrive68kDriverConfig driver_;
    MegaDriveTiming timing_;
    std::uint64_t master_hz_ = 0;     // 15 * the Z80 clock: one tick of the time base is 1/master_hz_ seconds
    std::uint64_t frame_ticks_ = 0;

    std::array<std::uint8_t, 0x10000> ram_{};
    std::array<std::uint8_t, 0x2000> z80_ram_{};
    std::array<std::uint8_t, 0x100> vectors_{};     // the vector table the 68000 sees (the cartridge's, with ours over the reset and level-6 entries)
    std::array<std::uint8_t, 0x400> stub_{};
    std::unique_ptr<MameDerivedM68000Adapter> m68k_;
    std::unique_ptr<MameDerivedZ80Adapter> z80_;
    std::unique_ptr<Ym2612DeviceAdapter> ym_;
    std::unique_ptr<Sn76489SoundDeviceAdapter> psg_;
    ClockConverter clock_converter_;

    std::uint32_t bank_ = 0;
    bool bus_requested_ = false;   // the 68000 holds the Z80's bus
    bool z80_reset_ = true;        // the Z80 is held in reset
    bool z80_stopped_ = true;      // bus_requested_ || z80_reset_
    // The Z80 sees a change of the bus request when the 68000's instruction that made it has ended (the original's Z80 runs on
    // to the end of the 68000's time slice, and the request takes effect at the next one).
    bool z80_stop_pending_ = false;     // bus_requested_ is set, the Z80 runs on until the 68000's time
    bool z80_resume_pending_ = false;   // the bus was given back, the Z80 goes on from the end of the instruction
    std::uint64_t t68_ = 0, tz80_ = 0;
    std::uint64_t next_vblank_ = 0;      // time of the next vertical-blank start (the 68000's interrupt and the Z80's)
    std::uint64_t next_z80_irq_edge_ = 0;
    bool z80_irq_high_ = false;
    bool vint_pending_ = false;
    bool hint_pending_ = false;
    std::uint64_t next_hint_ = ~0ull;    // time of the next horizontal-blank interrupt (master clock ticks)
    bool call_pending_ = false;   // a request waits for the level 2 interrupt (the call requests)
    std::vector<std::pair<std::uint32_t, std::uint8_t>> call_queue_;   // requests that wait for the 68000 to be idle (CallRequestForTest)
    std::vector<std::uint64_t> vint_schedule_;       // SetVblankScheduleForTest
    std::size_t vint_schedule_index_ = 0;
    bool vint_schedule_active_ = false;

    std::vector<LogicalChannel> channels_;
    bool channels_registered_ = false;
    std::vector<std::vector<float>> pending_ym_;
    std::vector<std::vector<float>> pending_psg_;
    std::array<DcBlocker, Ym2612DeviceAdapter::kChannelCount> dc_block_{};
    bool keep_sound_ = false;
    std::uint64_t origin_ticks_ = 0;
    std::uint64_t cumulative_ticks_ = 0;
    std::uint64_t ym_samples_made_ = 0;
    std::uint64_t psg_samples_made_ = 0;
    bool command_sent_since_reset_ = false;
    bool restore_warm_state_ = true;
    WarmState warm_;
    ChipWriteObserver chip_write_observer_;
    BusObserver bus_observer_;
    std::uint32_t ram_observer_low_ = 1, ram_observer_high_ = 0;
    RamWriteObserver ram_observer_;
    Z80RamWriteObserver z80_ram_observer_;
};

struct MegaDrive68kMachineInputs {
    std::vector<std::uint8_t> cartridge;
    MegaDrive68kDriverConfig driver;
};

// As AssembleMegaDriveMachineInputs, for the 68000 driver settings.
MegaDrive68kMachineInputs AssembleMegaDrive68kMachineInputs(const MachineResources& resources);
