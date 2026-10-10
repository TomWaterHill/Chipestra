// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Portions derived from MAME (src/mame/konami/nemesis.cpp, src/devices/sound/k005289.cpp); see THIRD-PARTY-NOTICES.md.
#include "gradius_machine.h"

#include "address_space.h"

#include <algorithm>
#include <string>
#include <utility>

// GradiusMachine::IoBus - dispatches the scattered addresses within
// GradiusMemoryMap::kIoStart/kIoSize (gx400_sound_map, 0xE000-0xE405). Wired and unwired
// addresses interleave byte by byte, so this cannot be a MemoryRegion; the unwired bytes
// fall back to a small scratch buffer acting as plain RAM.
class GradiusMachine::IoBus final : public AddressSpaceEndpoint {
public:
    IoBus(GradiusCommandInterface& commandInterface, Ay8910DeviceAdapter& ay1, Ay8910DeviceAdapter& ay2,
          K005289DeviceAdapter& k005289, Vlm5030DeviceAdapter& vlm5030, GradiusMachine& owner)
        : command_interface_(commandInterface),
          ay1_(ay1),
          ay2_(ay2),
          k005289_(k005289),
          vlm5030_(vlm5030),
          owner_(owner) {}

    std::uint8_t Read8(std::uint64_t offset) const override {
        switch (offset) {
            case kSoundLatchOffset:
                return command_interface_.ReadLatch();
            case kAy1DataOffset:
                owner_.SyncSoundDevices(owner_.CurrentBlockCycles());
                return ay1_.ReadData();
            case kAy2DataOffset:
                owner_.SyncSoundDevices(owner_.CurrentBlockCycles());
                return ay2_.ReadData();
            default:
                return scratch_[offset];
        }
    }

    void Write8(std::uint64_t offset, std::uint8_t value) override {
        // speech_w (0xE000, MAME's `map(0xe000, 0xe000).select(0x78)`): claims every offset
        // {0x000, 0x008, ..., 0x078} (bits 3-6 free, all other bits 0). A bitmask match
        // cannot be a switch case label, so it is checked first; it is disjoint from the
        // cases below.
        if ((offset & ~static_cast<std::uint64_t>(0x78)) == 0) {
            HandleSpeechW(static_cast<std::uint8_t>(offset), value);
            return;
        }
        switch (offset) {
            case kAy2AddressOffset:
                owner_.SyncSoundDevices(owner_.CurrentBlockCycles());
                ay2_.WriteAddress(value);
                break;
            case kAy1AddressOffset:
                owner_.SyncSoundDevices(owner_.CurrentBlockCycles());
                ay1_.WriteAddress(value);
                break;
            case kAy1WriteDataOffset:
                owner_.SyncSoundDevices(owner_.CurrentBlockCycles());
                ay1_.WriteData(value);
                break;
            case kAy2WriteDataOffset:
                owner_.SyncSoundDevices(owner_.CurrentBlockCycles());
                ay2_.WriteData(value);
                break;
            case kTg1Offset:
                // tg1_w/tg2_w commit the latched pitch to the audible frequency, so sync
                // first (as k005289.cpp does with m_stream->update()); ld1_w/ld2_w do not.
                owner_.SyncSoundDevices(owner_.CurrentBlockCycles());
                k005289_.Tg1(value);
                break;
            case kTg2Offset:
                owner_.SyncSoundDevices(owner_.CurrentBlockCycles());
                k005289_.Tg2(value);
                break;
            default:
                // Not wired: nemesis_filter_w (0xE007), RC filter emulation is not
                // modeled. Plain read/write "RAM".
                scratch_[offset] = value;
                break;
        }
    }

private:
    // The speech_w handling below is ported from MAME (https://github.com/mamedev/mame, tag mame0289):
    //   src/mame/konami/nemesis.cpp
    //   license:BSD-3-Clause
    //   copyright-holders:Bryan McPhail
    // See THIRD-PARTY-NOTICES.md.
    // speech_w (nemesis.cpp) - ported verbatim, including its own use of
    // the previous offset (speech_offset_/m_speech_offset) to detect bit
    // 3's falling edge before latching a data-bus byte into the chip.
    void HandleSpeechW(std::uint8_t offset, std::uint8_t value) {
        owner_.SyncSoundDevices(owner_.CurrentBlockCycles());
        // bit 3 falling edge: latch VLM data (the data bus is irrelevant
        // for any other write - k005289.cpp's/vlm5030.cpp's own commented
        // convention of "address encodes meaning" applies here too).
        if (((~offset) & 0x08) != 0 && (speech_offset_ & 0x08) != 0) {
            vlm5030_.DataW(value);
        }
        // bit 5: ST, bit 6: RST.
        vlm5030_.StPin((offset & 0x20) != 0);
        vlm5030_.RstPin((offset & 0x40) != 0);
        speech_offset_ = offset;
    }

    // Offsets relative to GradiusMemoryMap::kIoStart (0xE000).
    static constexpr std::uint64_t kSoundLatchOffset = 0x001;    // 0xE001
    static constexpr std::uint64_t kTg1Offset = 0x003;           // 0xE003
    static constexpr std::uint64_t kTg2Offset = 0x004;           // 0xE004
    static constexpr std::uint64_t kAy2AddressOffset = 0x005;    // 0xE005
    static constexpr std::uint64_t kAy1AddressOffset = 0x006;    // 0xE006
    static constexpr std::uint64_t kAy1DataOffset = 0x086;       // 0xE086 (read) - matches 0xE106 (write) below
    static constexpr std::uint64_t kAy1WriteDataOffset = 0x106;  // 0xE106
    static constexpr std::uint64_t kAy2DataOffset = 0x205;       // 0xE205 (read)
    static constexpr std::uint64_t kAy2WriteDataOffset = 0x405;  // 0xE405 (write)

    GradiusCommandInterface& command_interface_;
    Ay8910DeviceAdapter& ay1_;
    Ay8910DeviceAdapter& ay2_;
    K005289DeviceAdapter& k005289_;
    Vlm5030DeviceAdapter& vlm5030_;
    GradiusMachine& owner_;
    mutable std::uint8_t scratch_[GradiusMemoryMap::kIoSize] = {};
    // Previous speech_w offset (m_speech_offset on real hardware), needed to detect bit
    // 3's falling edge.
    std::uint8_t speech_offset_ = 0;
};

// The explanatory comments on ld1_w/ld2_w below reuse the wording of comments in MAME
// (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/sound/k005289.cpp
//   license:BSD-3-Clause
//   copyright-holders:Bryan McPhail
// See THIRD-PARTY-NOTICES.md.
// GradiusMachine::K005289LatchBus - dispatches the K005289 ld1_w/ld2_w decode windows
// (0xA000-0xAFFF/0xC000-0xCFFF). The chip has no data bus for these writes: the write
// ADDRESS (`offset`, relative to the window) is the value. They only latch `pitch`, not
// the audible `frequency`, so (unlike Tg1/Tg2) no SyncSoundDevices() is needed.
class GradiusMachine::K005289LatchBus final : public AddressSpaceEndpoint {
public:
    K005289LatchBus(K005289DeviceAdapter& k005289, bool isVoice2) : k005289_(k005289), is_voice2_(isVoice2) {}

    // Write-only latch window; nothing meaningful to read.
    std::uint8_t Read8(std::uint64_t) const override { return 0xFF; }

    void Write8(std::uint64_t offset, std::uint8_t value) override {
        if (is_voice2_) {
            k005289_.Ld2(static_cast<std::uint16_t>(offset), value);
        } else {
            k005289_.Ld1(static_cast<std::uint16_t>(offset), value);
        }
    }

private:
    K005289DeviceAdapter& k005289_;
    bool is_voice2_;
};

// GradiusMachine::MainIoBus - dispatches the main 68000's scattered I/O addresses within
// GradiusMainMemoryMap::kIoStart/kIoSize (offsets relative to 0x05C000). Anything not
// listed is accepted and ignored.
class GradiusMachine::MainIoBus final : public AddressSpaceEndpoint {
public:
    MainIoBus(GradiusCommandInterface& commandInterface, GradiusMachine& owner)
        : command_interface_(commandInterface), owner_(owner) {}

    std::uint8_t Read8(std::uint64_t offset) const override {
        // DSW0/DSW1/TEST/IN0/IN1/IN2 (0x05C402-0x05C407 and 0x05CC00-0x05CC05) are modeled
        // input ports and read as all bits set (nothing pressed/configured). Everything else
        // is genuinely unmapped bus (nemesis.cpp marks 0x05D000-0x05FFFF "Unknown read") and
        // must read 0x00 like MAME's unmapped default (m_unmapval(0)): the boot code polls
        // BTST #0,($5D001).L waiting for bit 0 to read CLEAR.
        if ((offset >= kDsw0Offset && offset <= kDsw0Offset + 1) ||
            (offset >= kDsw1Offset && offset <= kDsw1Offset + 1) ||
            (offset >= kTestOffset && offset <= kTestOffset + 1) ||
            (offset >= kIn0Offset && offset <= kIn2Offset + 1)) {
            return 0xFF;
        }
        return 0x00;
    }

    void Write8(std::uint64_t offset, std::uint8_t value) override {
        if (offset == kSoundLatchOffset) {
            if (owner_.player_owns_sound_latch_) return;
            // This write alone does not pulse the Z80 IRQ; that is the separate
            // outlatch-bit-2 write below (nemesis.cpp's soundlatch/sound_irq_w split).
            command_interface_.SetRawLatch(value);
            return;
        }
        if (offset == kWatchdogOffset || offset == kWatchdogOffset + 1) {
            return;  // accepted, ignored; no watchdog timeout is modeled
        }
        if (offset >= kLatchStart && offset < kLatchStart + 16) {
            const std::uint64_t rel = offset - kLatchStart;
            const int bit = static_cast<int>(rel / 2);
            const bool isOutlatch = (rel % 2) == 0;  // even=outlatch (umask16 0xff00), odd=intlatch (umask16 0x00ff)
            const bool set = (value & 1) != 0;
            if (isOutlatch && bit == 2) {
                // sound_irq_w: pulses using whatever SetRawLatch most recently latched
                // (two independent bus cycles on real hardware).
                if (set && !owner_.player_owns_sound_latch_) command_interface_.PulseIrq();
            } else if (isOutlatch && bit == 7) {
                // irq4_enable_w: enabling does not assert the line (a scanline-120
                // crossing in Render() does); disabling clears it immediately.
                owner_.irq4_enabled_ = set;
                if (!set) owner_.irq4_asserted_ = false;
                owner_.UpdateMainInterruptLevel();
            } else if (!isOutlatch && bit == 0) {
                // irq2_enable_w.
                owner_.irq2_enabled_ = set;
                if (!set) owner_.irq2_asserted_ = false;
                owner_.UpdateMainInterruptLevel();
            } else if (!isOutlatch && bit == 1) {
                // irq1_enable_w.
                owner_.irq1_enabled_ = set;
                if (!set) owner_.irq1_asserted_ = false;
                owner_.UpdateMainInterruptLevel();
            }
            // Other bits (coin lockout, screen flip) are not modeled.
            return;
        }
        // Everything else is accepted and ignored.
    }

private:
    static constexpr std::uint64_t kSoundLatchOffset = 0x0001;  // 0x05C001
    static constexpr std::uint64_t kDsw0Offset = 0x0402;        // 0x05C402-0x05C403
    static constexpr std::uint64_t kDsw1Offset = 0x0404;        // 0x05C404-0x05C405
    static constexpr std::uint64_t kTestOffset = 0x0406;        // 0x05C406-0x05C407
    static constexpr std::uint64_t kWatchdogOffset = 0x0800;    // 0x05C800
    static constexpr std::uint64_t kIn0Offset = 0x0C00;         // 0x05CC00-0x05CC01
    static constexpr std::uint64_t kIn2Offset = 0x0C04;         // 0x05CC04-0x05CC05 (IN0/IN1/IN2 are contiguous)
    static constexpr std::uint64_t kLatchStart = 0x2000;        // 0x05E000-0x05E00F

    GradiusCommandInterface& command_interface_;
    GradiusMachine& owner_;
};

GradiusMachine::GradiusMachine(std::vector<std::uint8_t> z80_rom, std::vector<std::uint8_t> k005289_waveform_prom,
                                std::vector<std::uint8_t> main_cpu_program, std::vector<std::uint8_t> main_cpu_data)
    : z80_rom_(std::move(z80_rom)),
      k005289_waveform_prom_(std::move(k005289_waveform_prom)),
      main_cpu_program_(std::move(main_cpu_program)),
      main_cpu_data_(std::move(main_cpu_data)),
      clock_converter_(kNativeRuntimeTickRateHz, kZ80ClockRate),
      main_clock_converter_(kNativeRuntimeTickRateHz, kMainCpuClockRate) {}

// Out of line: the unique_ptr members to the forward-declared nested types need their
// complete types, which exist only in this translation unit.
GradiusMachine::~GradiusMachine() = default;

void GradiusMachine::Initialize() {
    memory_map_.LoadRom(z80_rom_.data(), z80_rom_.size());

    auto z80 = std::make_unique<MameDerivedZ80Adapter>();
    z80_ = z80.get();
    device_manager_.Add(std::move(z80));
    // Apply any pre-Initialize() test observers before the first boot pass.
    if (z80_opcode_fetch_observer_for_test_) {
        z80_->SetOpcodeFetchObserverForTest(z80_opcode_fetch_observer_for_test_);
    }
    if (z80_memory_write_observer_for_test_) {
        z80_->SetMemoryWriteObserverForTest(z80_memory_write_observer_for_test_);
    }

    // Main 68000 CPU.
    auto main_cpu = std::make_unique<MameDerivedM68000Adapter>();
    main_cpu_ = main_cpu.get();
    device_manager_.Add(std::move(main_cpu));
    // Apply any pre-Initialize() test observers before the first boot pass.
    if (main_cpu_opcode_fetch_observer_for_test_) {
        main_cpu_->SetOpcodeFetchObserverForTest(main_cpu_opcode_fetch_observer_for_test_);
    }
    if (main_cpu_memory_write_observer_for_test_) {
        main_cpu_->SetMemoryWriteObserverForTest(main_cpu_memory_write_observer_for_test_);
    }
    if (main_cpu_illegal_instruction_observer_for_test_) {
        main_cpu_->SetIllegalInstructionObserverForTest(main_cpu_illegal_instruction_observer_for_test_);
    }

    main_memory_map_ = std::make_unique<GradiusMainMemoryMap>(
        [this](std::uint16_t offset) { return memory_map_.Read8(static_cast<std::uint16_t>(GradiusMemoryMap::kSharedRamStart + offset)); },
        [this](std::uint16_t offset, std::uint8_t value) {
            memory_map_.Write8(static_cast<std::uint16_t>(GradiusMemoryMap::kSharedRamStart + offset), value);
        });
    main_memory_map_->LoadMainProgram(main_cpu_program_.data(), main_cpu_program_.size());
    main_memory_map_->LoadMainData(main_cpu_data_.data(), main_cpu_data_.size());

    // Constructed before ay1 so ay1's port A read callback can capture the pointer. Reads
    // the same shared RAM the Z80 writes speech data into (0x8000-0x87FF) instead of
    // owning a byte vector.
    auto vlm5030 = std::make_unique<Vlm5030DeviceAdapter>([this](std::uint16_t offset) {
        return memory_map_.Read8(static_cast<std::uint16_t>(GradiusMemoryMap::kVoiceRamStart + offset));
    });
    vlm5030_ = vlm5030.get();
    device_manager_.Add(std::move(vlm5030));

    // ay1's port A read callback: nemesis_portA_r():
    // `res = (total_cycles()/512 & 0x0F) | 0xD0 | (bsy ? 0x20 : 0)`. Bits 4/6/7 are
    // driven high unconditionally by the hardware; boot code tests bit 7 alone.
    auto ay1 = std::make_unique<Ay8910DeviceAdapter>(
        /*portAWrite=*/nullptr, /*portBWrite=*/nullptr, [this]() -> std::uint8_t {
            std::uint8_t res = static_cast<std::uint8_t>(((cumulative_cycles_executed_ + z80_->CyclesConsumedThisCall()) / 512) & 0x0F);
            res |= 0xD0;
            if (vlm5030_->BusyPin()) res |= 0x20;
            return res;
        });
    ay1_ = ay1.get();
    // nemesis.cpp: ay1.set_flags(AY8910_LEGACY_OUTPUT | AY8910_SINGLE_OUTPUT).
    ay1_->SetLegacyNormalizedMixOutput(true);
    device_manager_.Add(std::move(ay1));

    // Constructed before ay2 so its port A/B write callbacks can capture the pointer.
    auto k005289 = std::make_unique<K005289DeviceAdapter>(k005289_waveform_prom_);
    k005289_ = k005289.get();
    device_manager_.Add(std::move(k005289));

    // ay2's port A/B write callbacks drive K005289's ControlA/ControlB (the I/O port
    // doubles as that chip's control bus).
    auto ay2 = std::make_unique<Ay8910DeviceAdapter>(
        [this](std::uint8_t data) { k005289_->ControlA(data); },
        [this](std::uint8_t data) { k005289_->ControlB(data); });
    ay2_ = ay2.get();
    // ay2 never calls set_flags() -> MAME default AY8910_LEGACY_OUTPUT (normalised, 3 streams).
    ay2_->SetLegacyNormalizedSeparateOutput(true);
    device_manager_.Add(std::move(ay2));

    command_interface_ = std::make_unique<GradiusCommandInterface>(*z80_);
    io_bus_ = std::make_unique<IoBus>(*command_interface_, *ay1_, *ay2_, *k005289_, *vlm5030_, *this);
    memory_map_.MapDevice(GradiusMemoryMap::kIoStart, GradiusMemoryMap::kIoSize, io_bus_.get());

    k005289_ld1_bus_ = std::make_unique<K005289LatchBus>(*k005289_, /*isVoice2=*/false);
    memory_map_.MapDevice(GradiusMemoryMap::kK005289Ld1Start, GradiusMemoryMap::kK005289Ld1Size, k005289_ld1_bus_.get());
    k005289_ld2_bus_ = std::make_unique<K005289LatchBus>(*k005289_, /*isVoice2=*/true);
    memory_map_.MapDevice(GradiusMemoryMap::kK005289Ld2Start, GradiusMemoryMap::kK005289Ld2Size, k005289_ld2_bus_.get());

    z80_->SetMemoryCallbacks(
        [this](std::uint16_t addr) { return memory_map_.Read8(addr); },
        [this](std::uint16_t addr, std::uint8_t value) { memory_map_.Write8(addr, value); });

    // Main 68000 I/O.
    main_io_bus_ = std::make_unique<MainIoBus>(*command_interface_, *this);
    main_memory_map_->MapDevice(GradiusMainMemoryMap::kIoStart, GradiusMainMemoryMap::kIoSize, main_io_bus_.get());
    main_cpu_->SetMemoryCallbacks(
        [this](std::uint32_t addr) { return main_memory_map_->Read8(addr); },
        [this](std::uint32_t addr, std::uint8_t value) { main_memory_map_->Write8(addr, value); });

    // The sound bus has no separate I/O port space (everything is memory-mapped).
    z80_->SetIoCallbacks([](std::uint8_t) -> std::uint8_t { return 0xFF; }, [](std::uint8_t, std::uint8_t) {});

    // LogicalChannels: ay1's 3 tone generators (ids 0-2), ay2's 3 (ids 3-5), K005289's 2
    // voices (ids 6-7), VLM5030's mono channel (id 8). Registered with the sink on
    // Render()'s first call.
    channels_.clear();
    for (std::uint32_t ch = 0; ch < Ay8910DeviceAdapter::kChannelCount; ++ch) {
        LogicalChannel channel;
        channel.Id = ch;
        channel.DeviceName = "AY8910";
        channel.ChannelName = "AY1 " + std::to_string(ch + 1);
        channel.NativeSampleRate = static_cast<std::uint32_t>(kZ80ClockRate.hz / Ay8910DeviceAdapter::kInputClocksPerSample);
        channel.RoutingGain = kAy1RoutingGain;
        channels_.push_back(std::move(channel));
    }
    for (std::uint32_t ch = 0; ch < Ay8910DeviceAdapter::kChannelCount; ++ch) {
        LogicalChannel channel;
        channel.Id = Ay8910DeviceAdapter::kChannelCount + ch;
        channel.DeviceName = "AY8910";
        channel.ChannelName = "AY2 " + std::to_string(ch + 1);
        channel.NativeSampleRate = static_cast<std::uint32_t>(kZ80ClockRate.hz / Ay8910DeviceAdapter::kInputClocksPerSample);
        channel.RoutingGain = kAy2RoutingGain;
        channels_.push_back(std::move(channel));
    }
    for (std::uint32_t voice = 0; voice < K005289DeviceAdapter::kVoiceCount; ++voice) {
        LogicalChannel channel;
        channel.Id = 2 * Ay8910DeviceAdapter::kChannelCount + voice;
        channel.DeviceName = "K005289";
        channel.ChannelName = "K005289 " + std::to_string(voice + 1);
        // K005289's own fixed clock; every input clock is one output sample.
        channel.NativeSampleRate = kK005289ClockRate;
        channel.RoutingGain = kK005289RoutingGain;
        channels_.push_back(std::move(channel));
    }
    {
        LogicalChannel channel;
        channel.Id = 2 * Ay8910DeviceAdapter::kChannelCount + K005289DeviceAdapter::kVoiceCount;
        channel.DeviceName = "VLM5030";
        channel.ChannelName = "VLM5030";
        // Same clock as K005289; one output sample per 440 input clocks.
        channel.NativeSampleRate = kK005289ClockRate / Vlm5030DeviceAdapter::kInputClocksPerSample;
        channel.RoutingGain = kVlm5030RoutingGain;
        channels_.push_back(std::move(channel));
    }
    channels_registered_ = false;
    ay1_channel_scratch_.assign(Ay8910DeviceAdapter::kChannelCount, {});
    ay2_channel_scratch_.assign(Ay8910DeviceAdapter::kChannelCount, {});
    k005289_voice_scratch_.assign(K005289DeviceAdapter::kVoiceCount, {});
    vlm5030_channel_scratch_.clear();

    z80_->Reset();
    main_cpu_->Reset();
    ay1_->Reset();
    ay2_->Reset();
    k005289_->Reset();
    vlm5030_->Reset();
    sound_sync_cursor_ = 0;
    cumulative_ticks_processed_ = 0;
    cumulative_cycles_executed_ = 0;
    vblank_nmi_pulses_delivered_ = 0;
    main_cumulative_ticks_processed_ = 0;
    main_cumulative_cycles_executed_ = 0;
    irq1_enabled_ = irq2_enabled_ = irq4_enabled_ = false;
    irq1_asserted_ = irq2_asserted_ = irq4_asserted_ = false;
    irq1_pulses_delivered_ = irq2_pulses_delivered_ = irq4_pulses_delivered_ = 0;
    UpdateMainInterruptLevel();

    // Boot warm-up, chunked (see RunBootWarmup()).
    RunBootWarmup();
}

void GradiusMachine::Reset() {
    player_owns_sound_latch_ = false;
    z80_->Reset();
    main_cpu_->Reset();
    ay1_->Reset();
    ay2_->Reset();
    k005289_->Reset();
    vlm5030_->Reset();
    channels_registered_ = false;
    sound_sync_cursor_ = 0;
    // z80_->Reset() already clears the core's irqAsserted_ flag; keep the bookkeeping
    // consistent.
    irq_awaiting_ack_ = false;
    // Restart the carry-forward baseline, as in Initialize().
    cumulative_ticks_processed_ = 0;
    cumulative_cycles_executed_ = 0;
    vblank_nmi_pulses_delivered_ = 0;
    main_cumulative_ticks_processed_ = 0;
    main_cumulative_cycles_executed_ = 0;
    irq1_enabled_ = irq2_enabled_ = irq4_enabled_ = false;
    irq1_asserted_ = irq2_asserted_ = irq4_asserted_ = false;
    irq1_pulses_delivered_ = irq2_pulses_delivered_ = irq4_pulses_delivered_ = 0;
    UpdateMainInterruptLevel();

    RunBootWarmup();
}

void GradiusMachine::Shutdown() {
    // device_manager_ releases the CPUs and sound devices; the plain members are released
    // by ~GradiusMachine() in reverse declaration order.
}

std::vector<LogicalChannel>& GradiusMachine::LogicalChannels() {
    return channels_;
}

void GradiusMachine::Render(EmuDuration duration, LogicalChannelSink* sink) {
    if (sink != nullptr && !channels_registered_) {
        for (const LogicalChannel& channel : channels_) {
            if (!sink->HasChannel(channel.Id)) sink->RegisterChannel(channel.Id);  // a Reset() re-registers into a reused sink
        }
        channels_registered_ = true;
    }

    for (std::vector<float>& buffer : ay1_channel_scratch_) buffer.clear();
    for (std::vector<float>& buffer : ay2_channel_scratch_) buffer.clear();
    for (std::vector<float>& buffer : k005289_voice_scratch_) buffer.clear();
    vlm5030_channel_scratch_.clear();

    // Asserts IRQ if a Send() since the last block armed one. A still-unacked assertion
    // from a previous block (irq_awaiting_ack_) carries forward regardless.
    if (command_interface_->Tick()) {
        irq_awaiting_ack_ = true;
    }

    // `duration` is in canonical 192kHz frames; clock_converter_ turns it into
    // 1,789,773Hz Z80 cycles. SyncSoundDevices() catches the sound devices up to the CPU
    // on every register access.
    cumulative_ticks_processed_ += duration.ticks;

    // VBLANK-driven NMI: a 60.606060Hz pulse independent of the command-latch IRQ. NMI is
    // edge-triggered and the Z80 adapter clears the pending flag itself, so a plain
    // Assert-then-Clear pulse is enough. At most one pulse is due per block in practice
    // (RunBootWarmup() keeps warm-up chunked to that granularity); the loop delivers every
    // pulse the elapsed time earned.
    const std::uint64_t targetVblankPulses = cumulative_ticks_processed_ / kVblankNmiPeriodCanonicalTicks;
    while (vblank_nmi_pulses_delivered_ < targetVblankPulses) {
        z80_->SetInputLine(Z80InputLine::Nmi, Z80InputLineState::Assert);
        z80_->SetInputLine(Z80InputLine::Nmi, Z80InputLineState::Clear);
        ++vblank_nmi_pulses_delivered_;
    }

    // Main 68000: an unrelated clock domain from the sound Z80. It runs BEFORE the Z80
    // within a block so its shared-RAM writes are visible to the Z80 in the same block.
    main_cumulative_ticks_processed_ += duration.ticks;

    // Screen interrupts (IRQ1/IRQ2/IRQ4): due-pulse counting like the VBLANK-NMI loop, one
    // instance per line's scanline phase. At most one crossing per line is due per block in
    // practice; each loop delivers every crossing the elapsed time earned.
    {
        // IRQ2: scanline 0, every frame (phase 0), the same due-pulse formula as the NMI
        // loop. Counted against the main CPU's own tick counter, which stays numerically
        // equal to the Z80's.
        const std::uint64_t dueIrq2 = main_cumulative_ticks_processed_ / kVblankNmiPeriodCanonicalTicks;
        while (irq2_pulses_delivered_ < dueIrq2) {
            if (irq2_enabled_) irq2_asserted_ = true;
            ++irq2_pulses_delivered_;
        }

        // IRQ4: scanline 120, every frame - phase kIrq4ScanlineOffsetTicks.
        const std::uint64_t dueIrq4 = main_cumulative_ticks_processed_ >= kIrq4ScanlineOffsetTicks
                                           ? (main_cumulative_ticks_processed_ - kIrq4ScanlineOffsetTicks) /
                                                     kVblankNmiPeriodCanonicalTicks +
                                                 1
                                           : 0;
        while (irq4_pulses_delivered_ < dueIrq4) {
            if (irq4_enabled_) irq4_asserted_ = true;
            ++irq4_pulses_delivered_;
        }

        // IRQ1: scanline 240, but only on every other frame (nemesis.cpp's
        // `(frame_number() & 1) == 0`). irq1_pulses_delivered_ before the increment is the
        // frame index of the crossing.
        const std::uint64_t dueIrq1 = main_cumulative_ticks_processed_ >= kIrq1ScanlineOffsetTicks
                                           ? (main_cumulative_ticks_processed_ - kIrq1ScanlineOffsetTicks) /
                                                     kVblankNmiPeriodCanonicalTicks +
                                                 1
                                           : 0;
        while (irq1_pulses_delivered_ < dueIrq1) {
            const std::uint64_t frameIndex = irq1_pulses_delivered_;
            if (irq1_enabled_ && (frameIndex % 2) == 0) irq1_asserted_ = true;
            ++irq1_pulses_delivered_;
        }

        UpdateMainInterruptLevel();
    }

    const std::uint64_t targetMainCumulativeCycles =
        main_clock_converter_.TicksToCycles(EmuDuration(main_cumulative_ticks_processed_));
    const std::uint64_t mainCycles = targetMainCumulativeCycles > main_cumulative_cycles_executed_
                                          ? targetMainCumulativeCycles - main_cumulative_cycles_executed_
                                          : 0;
    if (mainCycles > 0) {
        main_cpu_->Execute(mainCycles);
        main_cumulative_cycles_executed_ += main_cpu_->CyclesConsumedThisCall();
    }

    const std::uint64_t targetCumulativeCycles =
        clock_converter_.TicksToCycles(EmuDuration(cumulative_ticks_processed_));
    const std::uint64_t cycles =
        targetCumulativeCycles > cumulative_cycles_executed_ ? targetCumulativeCycles - cumulative_cycles_executed_ : 0;
    sound_sync_cursor_ = 0;
    block_base_cycles_ = 0;
    std::uint64_t cyclesRemaining = cycles;

    if (irq_awaiting_ack_) {
        // Holding IRQ asserted for the rest of the block would re-trigger the interrupt at
        // every instruction boundary once the driver's EI;RETI re-enables interrupts. Step
        // one unit at a time (Execute(1) either services the pending IRQ or advances one
        // instruction) until irqServiceCount increments, then drop the line immediately,
        // like the real /IOREQ auto-ack.
        const std::uint64_t before = z80_->Snapshot().irqServiceCount;
        while (cyclesRemaining > 0 && z80_->Snapshot().irqServiceCount == before) {
            z80_->Execute(1);
            const std::uint64_t consumed = z80_->CyclesConsumedThisCall();
            cumulative_cycles_executed_ += consumed;
            block_base_cycles_ += consumed;
            cyclesRemaining = consumed >= cyclesRemaining ? 0 : cyclesRemaining - consumed;
        }
        if (z80_->Snapshot().irqServiceCount != before) {
            command_interface_->ClearIrqAfterBlock();
            irq_awaiting_ack_ = false;
        }
        // else: the driver has not re-enabled interrupts yet; leave the line asserted
        // (irq_awaiting_ack_ stays true), like a level-triggered line held until
        // acknowledged.
    }

    if (cyclesRemaining > 0) {
        z80_->Execute(cyclesRemaining);
        const std::uint64_t consumed = z80_->CyclesConsumedThisCall();
        cumulative_cycles_executed_ += consumed;
        block_base_cycles_ += consumed;
    }

    // Final catch-up: advance the sound devices to the end of the block.
    SyncSoundDevices(block_base_cycles_);

    if (sink != nullptr) {
        for (std::uint32_t ch = 0; ch < Ay8910DeviceAdapter::kChannelCount; ++ch) {
            sink->WriteSamples(channels_[ch].Id, ay1_channel_scratch_[ch].data(), ay1_channel_scratch_[ch].size());
        }
        for (std::uint32_t ch = 0; ch < Ay8910DeviceAdapter::kChannelCount; ++ch) {
            sink->WriteSamples(channels_[Ay8910DeviceAdapter::kChannelCount + ch].Id, ay2_channel_scratch_[ch].data(),
                                ay2_channel_scratch_[ch].size());
        }
        for (std::uint32_t voice = 0; voice < K005289DeviceAdapter::kVoiceCount; ++voice) {
            sink->WriteSamples(channels_[2 * Ay8910DeviceAdapter::kChannelCount + voice].Id,
                                k005289_voice_scratch_[voice].data(), k005289_voice_scratch_[voice].size());
        }
        sink->WriteSamples(channels_[2 * Ay8910DeviceAdapter::kChannelCount + K005289DeviceAdapter::kVoiceCount].Id,
                            vlm5030_channel_scratch_.data(), vlm5030_channel_scratch_.size());
    }
}

void GradiusMachine::UpdateMainInterruptLevel() {
    // The 68000 IPL pins see only the highest asserted request level (external priority
    // encoder): IRQ4 > IRQ2 > IRQ1.
    std::uint8_t level = 0;
    if (irq1_asserted_) level = 1;
    if (irq2_asserted_) level = 2;
    if (irq4_asserted_) level = 4;
    main_cpu_->SetInterruptLevel(level);
}

void GradiusMachine::SyncSoundDevices(std::uint64_t z80CycleMark) {
    if (z80CycleMark <= sound_sync_cursor_) {
        return;
    }
    const std::uint64_t delta = z80CycleMark - sound_sync_cursor_;
    sound_sync_cursor_ = z80CycleMark;
    // AY8910 input clock == Z80 clock: no scaling.
    ay1_->Render(delta, ay1_channel_scratch_);
    ay2_->Render(delta, ay2_channel_scratch_);
    // K005289/VLM5030 run at exactly 2x the Z80 clock: a pure multiply, no drift across
    // blocks since the ratio is exact and the cursor resets every block.
    k005289_->Render(delta * 2, k005289_voice_scratch_);
    vlm5030_->Render(delta * 2, vlm5030_channel_scratch_);
}

void GradiusMachine::RunBootWarmup() {
    static_assert(kBootWarmupFrames % kWarmupChunkFrames == 0,
                  "kBootWarmupFrames must be an exact multiple of kWarmupChunkFrames");
    static_assert(kBootWarmupMaxFrames % kWarmupChunkFrames == 0,
                  "kBootWarmupMaxFrames must be an exact multiple of kWarmupChunkFrames");

    if (main_cpu_program_.empty()) {
        // No main-CPU ROM: there is no handshake to wait for, so use the flat budget.
        for (std::uint64_t rendered = 0; rendered < kBootWarmupFrames; rendered += kWarmupChunkFrames) {
            Render(EmuDuration{kWarmupChunkFrames}, nullptr);
        }
        return;
    }

    // Adaptive warm-up: render until the sound Z80's IFF1 is set or the cap is reached.
    for (std::uint64_t rendered = 0; rendered < kBootWarmupMaxFrames; rendered += kWarmupChunkFrames) {
        Render(EmuDuration{kWarmupChunkFrames}, nullptr);
        if (z80_->Snapshot().iff1) {
            if (skip_boot_jingle_) SkipBootJingle();
            return;
        }
    }
}

void GradiusMachine::SkipBootJingle() {
    // After the boot handshake clears, the real driver plays a ~21s start-up jingle and
    // drops any command that arrives meanwhile (a command sent at t>=25s plays). A player
    // selects a track right after loading, so render and discard the jingle: run until the
    // channels have been active and then quiet for kQuietFrames, or until no jingle is
    // playing (kNoJingleGraceFrames without audio), capped at kMaxFrames.
    constexpr std::uint64_t kMaxFrames = 192'000ull * 40;
    constexpr std::uint64_t kQuietFrames = 192'000ull * 2;
    constexpr std::uint64_t kNoJingleGraceFrames = 192'000ull * 3;
    LogicalChannelSink probe;
    bool seenAudio = false;
    std::uint64_t quiet = 0;
    for (std::uint64_t extra = 0; extra < kMaxFrames; extra += kWarmupChunkFrames) {
        probe.ClearAll();
        Render(EmuDuration{kWarmupChunkFrames}, &probe);
        bool active = false;
        for (const LogicalChannel& channel : channels_) {
            const LogicalChannelBuffer& buffer = probe.ChannelBuffer(channel.Id);
            for (std::size_t i = 0; i < buffer.Size() && !active; ++i) {
                active = std::fabs(buffer[i]) > 1e-4f;
            }
        }
        if (active) {
            seenAudio = true;
            quiet = 0;
        } else {
            quiet += kWarmupChunkFrames;
            if (seenAudio ? quiet >= kQuietFrames : extra >= kNoJingleGraceFrames) break;
        }
    }
    channels_registered_ = false;  // the probe sink is discarded; the real sink registers on its first Render()
}

void GradiusMachine::SendCommand(const MusicCommand& command) {
    player_owns_sound_latch_ = true;
    command_interface_->Send(command);
}

MameDerivedZ80Adapter::RegisterSnapshot GradiusMachine::Z80Snapshot() const {
    return z80_->Snapshot();
}

std::uint8_t GradiusMachine::ReadMemory(std::uint16_t address) const {
    return memory_map_.Read8(address);
}
