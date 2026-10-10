// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "bubble_system_machine.h"

#include "address_space.h"

#include <algorithm>
#include <string>
#include <utility>

// BubbleSystemMachine::IoBus - dispatches the sound Z80's scattered I/O addresses
// (GradiusMemoryMap::kIoStart/kIoSize). Identical to GradiusMachine::IoBus (same sound
// board); duplicated rather than shared.
class BubbleSystemMachine::IoBus final : public AddressSpaceEndpoint {
public:
    IoBus(GradiusCommandInterface& commandInterface, Ay8910DeviceAdapter& ay1, Ay8910DeviceAdapter& ay2,
          K005289DeviceAdapter& k005289, Vlm5030DeviceAdapter& vlm5030, BubbleSystemMachine& owner)
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
                owner_.SyncSoundDevices(owner_.CurrentBlockCycles());
                k005289_.Tg1(value);
                break;
            case kTg2Offset:
                owner_.SyncSoundDevices(owner_.CurrentBlockCycles());
                k005289_.Tg2(value);
                break;
            default:
                scratch_[offset] = value;
                break;
        }
    }

private:
    void HandleSpeechW(std::uint8_t offset, std::uint8_t value) {
        owner_.SyncSoundDevices(owner_.CurrentBlockCycles());
        if (((~offset) & 0x08) != 0 && (speech_offset_ & 0x08) != 0) {
            vlm5030_.DataW(value);
        }
        vlm5030_.StPin((offset & 0x20) != 0);
        vlm5030_.RstPin((offset & 0x40) != 0);
        speech_offset_ = offset;
    }

    static constexpr std::uint64_t kSoundLatchOffset = 0x001;
    static constexpr std::uint64_t kTg1Offset = 0x003;
    static constexpr std::uint64_t kTg2Offset = 0x004;
    static constexpr std::uint64_t kAy2AddressOffset = 0x005;
    static constexpr std::uint64_t kAy1AddressOffset = 0x006;
    static constexpr std::uint64_t kAy1DataOffset = 0x086;
    static constexpr std::uint64_t kAy1WriteDataOffset = 0x106;
    static constexpr std::uint64_t kAy2DataOffset = 0x205;
    static constexpr std::uint64_t kAy2WriteDataOffset = 0x405;

    GradiusCommandInterface& command_interface_;
    Ay8910DeviceAdapter& ay1_;
    Ay8910DeviceAdapter& ay2_;
    K005289DeviceAdapter& k005289_;
    Vlm5030DeviceAdapter& vlm5030_;
    BubbleSystemMachine& owner_;
    mutable std::uint8_t scratch_[GradiusMemoryMap::kIoSize] = {};
    std::uint8_t speech_offset_ = 0;
};

// BubbleSystemMachine::K005289LatchBus - identical to GradiusMachine::K005289LatchBus.
class BubbleSystemMachine::K005289LatchBus final : public AddressSpaceEndpoint {
public:
    K005289LatchBus(K005289DeviceAdapter& k005289, bool isVoice2) : k005289_(k005289), is_voice2_(isVoice2) {}

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

// BubbleSystemMachine::MainIoBus - dispatches the main 68000's scattered I/O addresses
// (BubbleSystemMainMemoryMap::kIoStart/kIoSize). Mostly identical to
// GradiusMachine::MainIoBus (same offsets and outlatch/intlatch bit assignments), plus
// outlatch bit 4 (byte offset 8) = sound_nmi_w, which drives the sound Z80's NMI line
// directly (it is unconnected on GX400).
class BubbleSystemMachine::MainIoBus final : public AddressSpaceEndpoint {
public:
    MainIoBus(GradiusCommandInterface& commandInterface, BubbleSystemMachine& owner)
        : command_interface_(commandInterface), owner_(owner) {}

    std::uint8_t Read8(std::uint64_t offset) const override {
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
            command_interface_.SetRawLatch(value);
            return;
        }
        if (offset == kWatchdogOffset || offset == kWatchdogOffset + 1) {
            return;
        }
        if (offset >= kLatchStart && offset < kLatchStart + 16) {
            const std::uint64_t rel = offset - kLatchStart;
            const int bit = static_cast<int>(rel / 2);
            const bool isOutlatch = (rel % 2) == 0;
            const bool set = (value & 1) != 0;
            if (isOutlatch && bit == 2) {
                if (set && !owner_.player_owns_sound_latch_) command_interface_.PulseIrq();
            } else if (isOutlatch && bit == 4) {
                // sound_nmi_w: level-driven onto the Z80's NMI input. The Z80 core reacts
                // only to the rising edge, so holding the level (as the real NOR-gated
                // wiring does) is correct and needs no Assert-then-Clear pair.
                owner_.sound_nmi_level_ = set;
                owner_.z80_->SetInputLine(Z80InputLine::Nmi,
                                           set ? Z80InputLineState::Assert : Z80InputLineState::Clear);
            } else if (isOutlatch && bit == 7) {
                owner_.irq4_enabled_ = set;
                if (!set) owner_.irq4_asserted_ = false;
                owner_.UpdateMainInterruptLevel();
            } else if (!isOutlatch && bit == 0) {
                owner_.irq2_enabled_ = set;
                if (!set) owner_.irq2_asserted_ = false;
                owner_.UpdateMainInterruptLevel();
            } else if (!isOutlatch && bit == 1) {
                owner_.irq1_enabled_ = set;
                if (!set) owner_.irq1_asserted_ = false;
                owner_.UpdateMainInterruptLevel();
            }
            return;
        }
    }

private:
    static constexpr std::uint64_t kSoundLatchOffset = 0x0001;
    static constexpr std::uint64_t kDsw0Offset = 0x0402;
    static constexpr std::uint64_t kDsw1Offset = 0x0404;
    static constexpr std::uint64_t kTestOffset = 0x0406;
    static constexpr std::uint64_t kWatchdogOffset = 0x0800;
    static constexpr std::uint64_t kIn0Offset = 0x0C00;
    static constexpr std::uint64_t kIn2Offset = 0x0C04;
    static constexpr std::uint64_t kLatchStart = 0x2000;

    GradiusCommandInterface& command_interface_;
    BubbleSystemMachine& owner_;
};

BubbleSystemMachine::BubbleSystemMachine(std::vector<std::uint8_t> z80_rom,
                                          std::vector<std::uint8_t> k005289_waveform_prom,
                                          std::vector<std::uint8_t> boot_program,
                                          std::vector<std::uint8_t> bubble_memory,
                                          BubbleSystemBoardMode board_mode)
    : z80_rom_(std::move(z80_rom)),
      k005289_waveform_prom_(std::move(k005289_waveform_prom)),
      boot_program_(std::move(boot_program)),
      bubble_memory_(std::move(bubble_memory)),
      clock_converter_(kNativeRuntimeTickRateHz, kZ80ClockRate),
      main_clock_converter_(kNativeRuntimeTickRateHz, kMainCpuClockRate),
      board_mode_(board_mode) {}

BubbleSystemMachine::~BubbleSystemMachine() = default;

void BubbleSystemMachine::Initialize() {
    memory_map_.LoadRom(z80_rom_.data(), z80_rom_.size());

    auto z80 = std::make_unique<MameDerivedZ80Adapter>();
    z80_ = z80.get();
    device_manager_.Add(std::move(z80));
    if (z80_opcode_fetch_observer_for_test_) {
        z80_->SetOpcodeFetchObserverForTest(z80_opcode_fetch_observer_for_test_);
    }
    if (z80_memory_write_observer_for_test_) {
        z80_->SetMemoryWriteObserverForTest(z80_memory_write_observer_for_test_);
    }

    auto main_cpu = std::make_unique<MameDerivedM68000Adapter>();
    main_cpu_ = main_cpu.get();
    device_manager_.Add(std::move(main_cpu));
    if (main_cpu_opcode_fetch_observer_for_test_) {
        main_cpu_->SetOpcodeFetchObserverForTest(main_cpu_opcode_fetch_observer_for_test_);
    }
    if (main_cpu_memory_write_observer_for_test_) {
        main_cpu_->SetMemoryWriteObserverForTest(main_cpu_memory_write_observer_for_test_);
    }
    if (main_cpu_illegal_instruction_observer_for_test_) {
        main_cpu_->SetIllegalInstructionObserverForTest(main_cpu_illegal_instruction_observer_for_test_);
    }

    main_memory_map_ = std::make_unique<BubbleSystemMainMemoryMap>(
        [this](std::uint16_t offset) {
            return memory_map_.Read8(static_cast<std::uint16_t>(GradiusMemoryMap::kSharedRamStart + offset));
        },
        [this](std::uint16_t offset, std::uint8_t value) {
            // The boot sequence's "stop the music" signal is not the sound latch: the 68000
            // writes 0x01 into shared-RAM byte 0 (Z80 0x4000) and a RAM-resident Z80 handler
            // clears every track pointer. The music loops by sequence data (pointers rewind
            // every ~57.1s), so once the player owns the sound that write is dropped.
            if (player_owns_sound_latch_ && offset == 0 && value == 0x01) return;
            memory_map_.Write8(static_cast<std::uint16_t>(GradiusMemoryMap::kSharedRamStart + offset), value);
        },
        [this]() {
            // K005297 "read page" completion: raise IRQ5 (dropped after Execute()).
            irq5_pending_ = true;
            UpdateMainInterruptLevel();
        });
    main_memory_map_->LoadBootProgramAndPrimeControlRam(boot_program_.data(), boot_program_.size());
    main_memory_map_->LoadBubbleMemory(bubble_memory_.data(), bubble_memory_.size());

    auto vlm5030 = std::make_unique<Vlm5030DeviceAdapter>([this](std::uint16_t offset) {
        return memory_map_.Read8(static_cast<std::uint16_t>(GradiusMemoryMap::kVoiceRamStart + offset));
    });
    vlm5030_ = vlm5030.get();
    device_manager_.Add(std::move(vlm5030));

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

    auto k005289 = std::make_unique<K005289DeviceAdapter>(k005289_waveform_prom_);
    k005289_ = k005289.get();
    device_manager_.Add(std::move(k005289));

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

    main_io_bus_ = std::make_unique<MainIoBus>(*command_interface_, *this);
    main_memory_map_->MapDevice(BubbleSystemMainMemoryMap::kIoStart, BubbleSystemMainMemoryMap::kIoSize,
                                  main_io_bus_.get());
    main_cpu_->SetMemoryCallbacks(
        [this](std::uint32_t addr) { return main_memory_map_->Read8(addr); },
        [this](std::uint32_t addr, std::uint8_t value) { main_memory_map_->Write8(addr, value); });

    z80_->SetIoCallbacks([](std::uint8_t) -> std::uint8_t { return 0xFF; }, [](std::uint8_t, std::uint8_t) {});

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
        channel.NativeSampleRate = kK005289ClockRate;
        channel.RoutingGain = kK005289RoutingGain;
        channels_.push_back(std::move(channel));
    }
    {
        LogicalChannel channel;
        channel.Id = 2 * Ay8910DeviceAdapter::kChannelCount + K005289DeviceAdapter::kVoiceCount;
        channel.DeviceName = "VLM5030";
        channel.ChannelName = "VLM5030";
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
    main_cumulative_ticks_processed_ = 0;
    main_cumulative_cycles_executed_ = 0;
    irq1_enabled_ = irq2_enabled_ = irq4_enabled_ = false;
    irq1_asserted_ = irq2_asserted_ = irq4_asserted_ = false;
    irq1_pulses_delivered_ = irq2_pulses_delivered_ = irq4_pulses_delivered_ = 0;
    irq5_pending_ = false;
    sound_nmi_level_ = false;
    nmi_pulses_delivered_ = 0;
    UpdateMainInterruptLevel();

    RunBootWarmup();
}

void BubbleSystemMachine::ClearAllRamForColdBoot() {
    // Power-on-equivalent RAM state: Z80 work/shared/voice RAM and every 68000-side RAM
    // (program/char/work), then reload the boot program into the 4KB bubsys shared RAM.
    for (std::uint32_t a = 0x4000; a < 0x8800; ++a) memory_map_.Write8(static_cast<std::uint16_t>(a), 0);
    for (std::uint32_t a = BubbleSystemMainMemoryMap::kProgramRamStart;
         a < BubbleSystemMainMemoryMap::kProgramRamStart + BubbleSystemMainMemoryMap::kProgramRamSize; ++a)
        main_memory_map_->Write8(a, 0);
    for (std::uint32_t a = BubbleSystemMainMemoryMap::kCharRamStart;
         a < BubbleSystemMainMemoryMap::kCharRamStart + BubbleSystemMainMemoryMap::kCharRamSize; ++a)
        main_memory_map_->Write8(a, 0);
    for (std::uint32_t a = BubbleSystemMainMemoryMap::kWorkRamStart;
         a < BubbleSystemMainMemoryMap::kWorkRamStart + BubbleSystemMainMemoryMap::kWorkRamSize; ++a)
        main_memory_map_->Write8(a, 0);
    main_memory_map_->LoadBootProgramAndPrimeControlRam(boot_program_.data(), boot_program_.size());
}

void BubbleSystemMachine::Reset() {
    player_owns_sound_latch_ = false;
    ClearAllRamForColdBoot();
    command_interface_->Reset();  // no stale, never-delivered command may leak into this cold boot
    z80_->Reset();
    main_cpu_->Reset();
    ay1_->Reset();
    ay2_->Reset();
    k005289_->Reset();
    vlm5030_->Reset();
    channels_registered_ = false;
    sound_sync_cursor_ = 0;
    irq_awaiting_ack_ = false;
    cumulative_ticks_processed_ = 0;
    cumulative_cycles_executed_ = 0;
    main_cumulative_ticks_processed_ = 0;
    main_cumulative_cycles_executed_ = 0;
    irq1_enabled_ = irq2_enabled_ = irq4_enabled_ = false;
    irq1_asserted_ = irq2_asserted_ = irq4_asserted_ = false;
    irq1_pulses_delivered_ = irq2_pulses_delivered_ = irq4_pulses_delivered_ = 0;
    irq5_pending_ = false;
    sound_nmi_level_ = false;
    nmi_pulses_delivered_ = 0;
    UpdateMainInterruptLevel();

    RunBootWarmup();
}

void BubbleSystemMachine::Shutdown() {}

std::vector<LogicalChannel>& BubbleSystemMachine::LogicalChannels() {
    return channels_;
}

void BubbleSystemMachine::Render(EmuDuration duration, LogicalChannelSink* sink) {
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

    if (command_interface_->Tick()) {
        irq_awaiting_ack_ = true;
    }

    cumulative_ticks_processed_ += duration.ticks;

    if (board_mode_ == BubbleSystemBoardMode::BiosOnly) {
        // No 68000 runs: generate the sound NMI its boot code would toggle, once per video
        // frame, with the same due-pulse formula as GradiusMachine's VBLANK NMI (an
        // assert/clear pair latches the edge-triggered NMI).
        const std::uint64_t dueNmis = cumulative_ticks_processed_ / kVblankNmiPeriodCanonicalTicks;
        while (nmi_pulses_delivered_ < dueNmis) {
            z80_->SetInputLine(Z80InputLine::Nmi, Z80InputLineState::Assert);
            z80_->SetInputLine(Z80InputLine::Nmi, Z80InputLineState::Clear);
            ++nmi_pulses_delivered_;
        }
    } else {
        // No automatic VBLANK NMI: the sound NMI is driven by the 68000's outlatch-bit-4
        // writes (MainIoBus).

        main_cumulative_ticks_processed_ += duration.ticks;

        {
            // IRQ2 and IRQ1 share scanline phase 0 here (bubsys_interrupt; GX400's IRQ1 is
            // at scanline 240); IRQ1 is additionally gated to every other frame.
            const std::uint64_t dueAtScanlineZero = main_cumulative_ticks_processed_ / kVblankNmiPeriodCanonicalTicks;
            while (irq2_pulses_delivered_ < dueAtScanlineZero) {
                if (irq2_enabled_) irq2_asserted_ = true;
                ++irq2_pulses_delivered_;
            }
            while (irq1_pulses_delivered_ < dueAtScanlineZero) {
                const std::uint64_t frameIndex = irq1_pulses_delivered_;
                if (irq1_enabled_ && (frameIndex % 2) == 0) irq1_asserted_ = true;
                ++irq1_pulses_delivered_;
            }

            // IRQ4: free-running 72-scanline counter, not phase-locked to VBLANK.
            const std::uint64_t dueIrq4 = main_cumulative_ticks_processed_ / kIrq4PeriodTicks;
            while (irq4_pulses_delivered_ < dueIrq4) {
                if (irq4_enabled_) irq4_asserted_ = true;
                ++irq4_pulses_delivered_;
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

        // IRQ5 auto-ack: the K005297 write that raises it happens synchronously inside the
        // Execute() call above, so the CPU has already had its chance to service it.
        if (irq5_pending_) {
            irq5_pending_ = false;
            UpdateMainInterruptLevel();
        }

    }

    const std::uint64_t targetCumulativeCycles =
        clock_converter_.TicksToCycles(EmuDuration(cumulative_ticks_processed_));
    const std::uint64_t cycles =
        targetCumulativeCycles > cumulative_cycles_executed_ ? targetCumulativeCycles - cumulative_cycles_executed_ : 0;
    sound_sync_cursor_ = 0;
    block_base_cycles_ = 0;
    std::uint64_t cyclesRemaining = cycles;

    if (irq_awaiting_ack_) {
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
    }

    if (cyclesRemaining > 0) {
        z80_->Execute(cyclesRemaining);
        const std::uint64_t consumed = z80_->CyclesConsumedThisCall();
        cumulative_cycles_executed_ += consumed;
        block_base_cycles_ += consumed;
    }

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

void BubbleSystemMachine::UpdateMainInterruptLevel() {
    // Highest asserted level wins (as in GradiusMachine), with IRQ5 (K005297 page-read
    // completion) as the highest priority.
    std::uint8_t level = 0;
    if (irq1_asserted_) level = 1;
    if (irq2_asserted_) level = 2;
    if (irq4_asserted_) level = 4;
    if (irq5_pending_) level = 5;
    main_cpu_->SetInterruptLevel(level);
}

void BubbleSystemMachine::SyncSoundDevices(std::uint64_t z80CycleMark) {
    if (z80CycleMark <= sound_sync_cursor_) {
        return;
    }
    const std::uint64_t delta = z80CycleMark - sound_sync_cursor_;
    sound_sync_cursor_ = z80CycleMark;
    ay1_->Render(delta, ay1_channel_scratch_);
    ay2_->Render(delta, ay2_channel_scratch_);
    k005289_->Render(delta * 2, k005289_voice_scratch_);
    vlm5030_->Render(delta * 2, vlm5030_channel_scratch_);
}

void BubbleSystemMachine::RunBootWarmup() {
    static_assert(kBootWarmupMaxFrames % kWarmupChunkFrames == 0,
                  "kBootWarmupMaxFrames must be an exact multiple of kWarmupChunkFrames");

    // Always adaptive: a Machine without a boot program cannot run meaningfully.
    for (std::uint64_t rendered = 0; rendered < kBootWarmupMaxFrames; rendered += kWarmupChunkFrames) {
        Render(EmuDuration{kWarmupChunkFrames}, nullptr);
        if (z80_->Snapshot().iff1) {
            return;
        }
    }
}

void BubbleSystemMachine::SendCommand(const MusicCommand& command) {
    if (board_mode_ == BubbleSystemBoardMode::BiosOnly) {
        // Every selection starts from the board's power-on state: the BIOS sound driver
        // speaks "Presented by Konami" and the "Getting ready... Fifty..." countdown by
        // itself from power-on and cannot get back into that state (Countdown, 0x40, does
        // nothing after STOP or Morning Music). Restarting makes every selection
        // reproducible, and is a no-op for the other commands.
        Reset();
    }
    player_owns_sound_latch_ = true;
    command_interface_->Send(command);
}

MameDerivedZ80Adapter::RegisterSnapshot BubbleSystemMachine::Z80Snapshot() const {
    return z80_->Snapshot();
}

std::uint8_t BubbleSystemMachine::ReadMemory(std::uint16_t address) const {
    return memory_map_.Read8(address);
}
