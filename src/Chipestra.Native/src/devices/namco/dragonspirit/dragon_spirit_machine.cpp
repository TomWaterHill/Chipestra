// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "dragon_spirit_machine.h"

#include "address_space.h"

#include <algorithm>
#include <functional>
#include <utility>

// std::function-backed AddressSpaceEndpoint; local glue that wires the memory map's device windows
// onto this Machine's devices.
class DragonSpiritMachine::CallbackEndpoint final : public AddressSpaceEndpoint {
public:
    using ReadFn = std::function<std::uint8_t(std::uint64_t offset)>;
    using WriteFn = std::function<void(std::uint64_t offset, std::uint8_t value)>;

    CallbackEndpoint(ReadFn read, WriteFn write) : read_(std::move(read)), write_(std::move(write)) {}

    std::uint8_t Read8(std::uint64_t offset) const override { return read_ ? read_(offset) : 0x00; }
    void Write8(std::uint64_t offset, std::uint8_t value) override {
        if (write_) write_(offset, value);
    }

private:
    ReadFn read_;
    WriteFn write_;
};

DragonSpiritMachine::DragonSpiritMachine(std::vector<std::uint8_t> soundcpu_rom, std::vector<std::uint8_t> mcu_rom,
                                          std::vector<std::uint8_t> voice_rom)
    : soundcpu_rom_(std::move(soundcpu_rom)),
      mcu_rom_(std::move(mcu_rom)),
      voice_rom_(std::move(voice_rom)),
      mcu_memory_map_(memory_map_.TriRamForSharing()),  // memory_map_ is declared first, so it is constructed first
      clock_converter_(kNativeRuntimeTickRateHz, kM6809ClockRate),
      ym2151_from_m6809_(kM6809ClockRate.hz, kYm2151ClockRate),
      cus30_from_m6809_(kM6809ClockRate.hz, kCus30ClockRate),
      mcu_clock_converter_(kNativeRuntimeTickRateHz, kMcuClockRate) {}

DragonSpiritMachine::~DragonSpiritMachine() = default;

void DragonSpiritMachine::Initialize() {
    memory_map_.LoadSoundRom(soundcpu_rom_.data(), soundcpu_rom_.size());

    auto m6809 = std::make_unique<MameDerivedM6809Adapter>();
    m6809_ = m6809.get();
    device_manager_.Add(std::move(m6809));
    if (m6809_opcode_fetch_observer_for_test_) {
        m6809_->SetOpcodeFetchObserverForTest(m6809_opcode_fetch_observer_for_test_);
    }

    // YM2151 timer IRQ -> M6809 FIRQ.
    auto ym2151 = std::make_unique<Ym2151DeviceAdapter>([this](bool asserted) {
        m6809_->SetInputLine(M6809InputLine::Firq, asserted ? M6809InputLineState::Assert : M6809InputLineState::Clear);
    });
    ym2151_ = ym2151.get();
    device_manager_.Add(std::move(ym2151));

    auto cus30 = std::make_unique<Cus30DeviceAdapter>();
    cus30_ = cus30.get();
    device_manager_.Add(std::move(cus30));

    // The MCU loads its internal ROM and the voice ROM here (empty resources leave them zero-filled).
    mcu_memory_map_.LoadInternalRom(mcu_rom_.data(), mcu_rom_.size());
    mcu_memory_map_.LoadVoiceRom(voice_rom_.data(), voice_rom_.size());
    auto mcu = std::make_unique<MameDerivedHd63701Adapter>();
    mcu_ = mcu.get();
    device_manager_.Add(std::move(mcu));
    if (mcu_opcode_fetch_observer_for_test_) {
        mcu_->SetOpcodeFetchObserverForTest(mcu_opcode_fetch_observer_for_test_);
    }
    // Port 2 writes set the DAC gains (as on hardware). Port 1 (coin input/lockout output) is left
    // unwired: there is no coin mechanism, and an unset port reads as all ones ("no coin").
    mcu_->SetPort2Callbacks(nullptr, [this](std::uint8_t value) {
        // Port 2 layout: DAC0 gain from bits 0/2, DAC1 gain from bits 3/4, each a 2-bit field selecting
        // one of 4 gain steps, (value+1)/4.
        const int dac0Gain = (((value >> 2) & 1) << 1) | (value & 1);
        const int dac1Gain = (((value >> 4) & 1) << 1) | ((value >> 3) & 1);
        dac0_.SetGain((static_cast<float>(dac0Gain) + 1.0f) / 4.0f);
        dac1_.SetGain((static_cast<float>(dac1Gain) + 1.0f) / 4.0f);
    });

    command_interface_ = std::make_unique<DragonSpiritCommandInterface>(memory_map_);

    // YM2151 (0x4000 address, 0x4001 data).
    endpoints_[0] = std::make_unique<CallbackEndpoint>(
        [this](std::uint64_t) -> std::uint8_t {
            SyncSoundDevices(CurrentBlockCycles());
            return ym2151_->ReadStatus();
        },
        [this](std::uint64_t offset, std::uint8_t value) {
            SyncSoundDevices(CurrentBlockCycles());
            if (offset == 0) ym2151_->WriteAddress(value); else ym2151_->WriteData(value);
            ++ym2151_write_count_for_test_;
        });
    memory_map_.MapDevice(DragonSpiritMemoryMap::kYm2151Start, DragonSpiritMemoryMap::kYm2151Size, endpoints_[0].get());

    // CUS30 (0x5000-0x53FF, full 0x000-0x3FF decode).
    endpoints_[1] = std::make_unique<CallbackEndpoint>(
        [this](std::uint64_t offset) -> std::uint8_t {
            SyncSoundDevices(CurrentBlockCycles());
            return cus30_->Read8(static_cast<std::uint16_t>(offset));
        },
        [this](std::uint64_t offset, std::uint8_t value) {
            SyncSoundDevices(CurrentBlockCycles());
            cus30_->Write8(static_cast<std::uint16_t>(offset), value);
        });
    memory_map_.MapDevice(DragonSpiritMemoryMap::kCus30Start, DragonSpiritMemoryMap::kCus30Size, endpoints_[1].get());

    // DAC0/DAC1 (0xD000/0xD400). Writes only happen inside mcu_->Execute(), so the MCU's own
    // CyclesConsumedThisCall() gives the write's cycle offset within the block.
    endpoints_[2] = std::make_unique<CallbackEndpoint>(
        [](std::uint64_t) -> std::uint8_t { return 0x00; },  // write-only
        [this](std::uint64_t, std::uint8_t value) {
            dac0_.RecordWrite(mcu_->CyclesConsumedThisCall(), value);
            if (dac0_write_observer_for_test_) dac0_write_observer_for_test_(value);
        });
    mcu_memory_map_.MapDevice(DragonSpiritMcuMemoryMap::kDac0Address, 1, endpoints_[2].get());

    endpoints_[3] = std::make_unique<CallbackEndpoint>(
        [](std::uint64_t) -> std::uint8_t { return 0x00; },
        [this](std::uint64_t, std::uint8_t value) {
            dac1_.RecordWrite(mcu_->CyclesConsumedThisCall(), value);
            if (dac1_write_observer_for_test_) dac1_write_observer_for_test_(value);
        });
    mcu_memory_map_.MapDevice(DragonSpiritMcuMemoryMap::kDac1Address, 1, endpoints_[3].get());

    // VBLANK IRQ ack (0xE000): the hardware has a real software-ack address, so the CPU's own handler clears the line.
    memory_map_.SetIrqAckCallback([this]() {
        m6809_->SetInputLine(M6809InputLine::Irq, M6809InputLineState::Clear);
    });
    // MCU IRQ1 ack (0xF000): same VBLANK edge, separate software-acked line.
    mcu_memory_map_.SetIrqAckCallback([this]() {
        mcu_->SetInputLine(Hd63701InputLine::Irq1, Hd63701InputLineState::Clear);
    });

    m6809_->SetMemoryCallbacks(
        [this](std::uint16_t addr) { return memory_map_.Read8(addr); },
        [this](std::uint16_t addr, std::uint8_t value) { memory_map_.Write8(addr, value); });
    mcu_->SetMemoryCallbacks(
        [this](std::uint16_t addr) { return mcu_memory_map_.Read8(addr); },
        [this](std::uint16_t addr, std::uint8_t value) { mcu_memory_map_.Write8(addr, value); });

    // The CPU constructors ran Reset() before the memory callbacks were wired, so the reset vector was
    // read as 0 and PC is wrong; reset again now that the ROM is loaded and the maps are live.
    m6809_->Reset();
    mcu_->Reset();

    // LogicalChannels: see the id layout in the header.
    channels_.clear();
    for (std::uint32_t ch = 0; ch < Ym2151DeviceAdapter::kChannelCount; ++ch) {
        LogicalChannel channel;
        channel.Id = kYm2151Base + ch;
        channel.DeviceName = "YM2151";
        channel.ChannelName = "FM" + std::to_string(ch + 1);
        channel.NativeSampleRate = static_cast<std::uint32_t>(kYm2151ClockRate.hz / Ym2151DeviceAdapter::kInputClocksPerSample);
        channel.RoutingGain = kYm2151RoutingGain;
        channels_.push_back(std::move(channel));
    }
    for (std::uint32_t v = 0; v < Cus30DeviceAdapter::kVoiceCount; ++v) {
        LogicalChannel channel;
        channel.Id = kCus30Base + v;
        channel.DeviceName = "CUS30";
        channel.ChannelName = "Voice" + std::to_string(v + 1);
        channel.NativeSampleRate = static_cast<std::uint32_t>(kCus30ClockRate.hz / Cus30DeviceAdapter::kInputClocksPerSample);
        channel.RoutingGain = kCus30RoutingGain;
        channels_.push_back(std::move(channel));
    }
    {
        // One LogicalChannel per DAC.
        LogicalChannel dac0Channel;
        dac0Channel.Id = kDac0Base;
        dac0Channel.DeviceName = "DAC1";  // hardware m_dac[0], numbered from 1 like the other devices
        dac0Channel.ChannelName = "Output";
        // 6000Hz is the true DAC rate; Render() reconstructs at that rate and ResamplingMixerFeed's
        // anti-aliased resampler upsamples to 192kHz.
        dac0Channel.NativeSampleRate = kDacSampleRateHz;
        dac0Channel.RoutingGain = kDac0RoutingGain;
        channels_.push_back(std::move(dac0Channel));

        LogicalChannel dac1Channel;
        dac1Channel.Id = kDac1Base;
        dac1Channel.DeviceName = "DAC2";  // m_dac[1]
        dac1Channel.ChannelName = "Output";
        dac1Channel.NativeSampleRate = kDacSampleRateHz;  // see dac0Channel
        dac1Channel.RoutingGain = kDac1RoutingGain;
        channels_.push_back(std::move(dac1Channel));
    }
    channels_registered_ = false;
    ym2151_channel_scratch_.assign(Ym2151DeviceAdapter::kChannelCount, {});
    cus30_channel_scratch_.assign(Cus30DeviceAdapter::kVoiceCount, {});
    dac0_channel_scratch_.clear();
    dac1_channel_scratch_.clear();

    cumulative_ticks_processed_ = 0;
    cumulative_cycles_executed_ = 0;
    block_base_cycles_ = 0;
    sound_sync_cursor_ = 0;
    vblank_irq_pulses_delivered_ = 0;
    mcu_cumulative_cycles_executed_ = 0;
    ym2151_cycles_issued_ = 0;
    cus30_cycles_issued_ = 0;
    main_cpu_gate_write_done_ = false;
    command_sent_since_reset_ = false;
    dac0_.Reset();
    dac1_.Reset();

    command_interface_->Reset();

    // Let the driver's reset-vector boot code run before any command is accepted; RunBootWarmup() also
    // injects the CUS30 self-test byte that the main CPU would write.
    RunBootWarmup();
}

void DragonSpiritMachine::Reset() {
    // Back to the power-on state, RAM included (CPU Reset() alone leaves TRIRAM/work RAM as the last song
    // left them, which made a post-reset song differ slightly from a fresh one).
    memory_map_.ClearRam();
    mcu_memory_map_.ClearNvram();
    mcu_->ClearInternalRam();
    command_sent_since_reset_ = false;
    ++reset_count_for_test_;
    m6809_->Reset();
    ym2151_->Reset();
    cus30_->Reset();
    mcu_->Reset();
    dac0_.Reset();
    dac1_.Reset();
    channels_registered_ = false;
    cumulative_ticks_processed_ = 0;
    cumulative_cycles_executed_ = 0;
    block_base_cycles_ = 0;
    sound_sync_cursor_ = 0;
    vblank_irq_pulses_delivered_ = 0;
    mcu_cumulative_cycles_executed_ = 0;
    ym2151_cycles_issued_ = 0;
    cus30_cycles_issued_ = 0;
    main_cpu_gate_write_done_ = false;

    command_interface_->Reset();
    RunBootWarmup();
}

void DragonSpiritMachine::Shutdown() {
    // device_manager_ releases the CPUs and chips; endpoints_/command_interface_ are destroyed first by the
    // destructor (declared after device_manager_).
}

std::vector<LogicalChannel>& DragonSpiritMachine::LogicalChannels() { return channels_; }

void DragonSpiritMachine::Render(EmuDuration duration, LogicalChannelSink* sink) {
    if (sink != nullptr && !channels_registered_) {
        for (const LogicalChannel& channel : channels_) {
            if (!sink->HasChannel(channel.Id)) sink->RegisterChannel(channel.Id);  // a Reset() re-registers into a reused sink
        }
        channels_registered_ = true;
    }

    for (std::vector<float>& buffer : ym2151_channel_scratch_) buffer.clear();
    for (std::vector<float>& buffer : cus30_channel_scratch_) buffer.clear();
    // Runs on every Render() call, including boot warm-up calls (sink == nullptr).
    dac0_.BeginBlock();
    dac1_.BeginBlock();

    cumulative_ticks_processed_ += duration.ticks;

    // VBLANK IRQ is a level signal, so re-asserting an asserted line is idempotent and each crossing need
    // not be delivered separately. The audio CPU's IRQ and the MCU's IRQ1 come from the same pulse count.
    const std::uint64_t targetVblankPulses = cumulative_ticks_processed_ / kVblankIrqPeriodCanonicalTicks;
    if (targetVblankPulses > vblank_irq_pulses_delivered_) {
        m6809_->SetInputLine(M6809InputLine::Irq, M6809InputLineState::Assert);
        mcu_->SetInputLine(Hd63701InputLine::Irq1, Hd63701InputLineState::Assert);
        vblank_irq_pulses_delivered_ = targetVblankPulses;
    }

    // The main CPU's one-time write of 0xA6 to the TRIRAM gate byte (see kMainCpuTriRamReadyByte). The
    // MCU memory map drops the MCU dispatcher's own writes to that byte, so this single write keeps it at
    // 0xA6 for the rest of the session; no periodic re-assert is needed.
    if (!main_cpu_gate_write_done_ && cumulative_ticks_processed_ >= kMainCpuGateWriteTimeCanonicalTicks) {
        switch (triram_gate_mode_for_test_) {
            case TriRamGateByteModeForTest::Default:
                memory_map_.Write8(DragonSpiritMemoryMap::kTriRamStart, kMainCpuTriRamReadyByte);
                if (triram_gate_write_observer_for_test_) triram_gate_write_observer_for_test_("render", kMainCpuTriRamReadyByte);
                break;
            case TriRamGateByteModeForTest::Suppressed:
                break;  // diagnostic-only: skip the substitute write
            case TriRamGateByteModeForTest::ForceValue:
                memory_map_.Write8(DragonSpiritMemoryMap::kTriRamStart, triram_gate_force_value_for_test_);
                if (triram_gate_write_observer_for_test_) triram_gate_write_observer_for_test_("render", triram_gate_force_value_for_test_);
                break;
        }
        main_cpu_gate_write_done_ = true;
        // The DAC0 gate bytes at TRIRAM offsets 0x2B/0x2C are deliberately not synthesized (see the known
        // limitation in the header).
    }

    // Carry-forward cadence: cumulative cycle targets keep the M6809 clock free of drift.
    const std::uint64_t targetCumulativeCycles = clock_converter_.TicksToCycles(EmuDuration(cumulative_ticks_processed_));
    const std::uint64_t cycles =
        targetCumulativeCycles > cumulative_cycles_executed_ ? targetCumulativeCycles - cumulative_cycles_executed_ : 0;
    block_base_cycles_ = cumulative_cycles_executed_;
    const std::uint64_t executeReturn = m6809_->Execute(cycles);
    cumulative_cycles_executed_ += executeReturn;

    // Block-end catch-up uses Execute()'s return value (see SyncSoundDevices in the header).
    SyncSoundDevices(block_base_cycles_ + executeReturn);

    // MCU: stepped once per block. DAC write-event recording (RecordWrite via endpoints_[2]/[3]) captures
    // the correct placement within the block regardless of this coarser CPU step.
    const std::uint64_t mcuTargetCycles = mcu_clock_converter_.TicksToCycles(EmuDuration(cumulative_ticks_processed_));
    if (mcuTargetCycles > mcu_cumulative_cycles_executed_) {
        mcu_->Execute(mcuTargetCycles - mcu_cumulative_cycles_executed_);
        mcu_cumulative_cycles_executed_ = mcuTargetCycles;
    }

    // DAC0/DAC1 reconstruction: cycle-to-tick conversion is approximate (no carry). Reconstruct at the
    // true 6000Hz sample count so ResamplingMixerFeed's anti-aliased resampler upsamples to 192kHz.
    const auto dacNativeSampleCount =
        static_cast<std::size_t>((static_cast<std::uint64_t>(duration.ticks) * kDacSampleRateHz) / kNativeRuntimeTickRateHz);
    dac0_.Reconstruct(dac0_channel_scratch_, dacNativeSampleCount, kDacNativeSamplesPerMcuCycle);
    dac1_.Reconstruct(dac1_channel_scratch_, dacNativeSampleCount, kDacNativeSamplesPerMcuCycle);

    if (sink != nullptr) {
        for (std::uint32_t ch = 0; ch < Ym2151DeviceAdapter::kChannelCount; ++ch) {
            sink->WriteSamples(channels_[kYm2151Base + ch].Id, ym2151_channel_scratch_[ch].data(),
                               ym2151_channel_scratch_[ch].size());
        }
        for (std::uint32_t v = 0; v < Cus30DeviceAdapter::kVoiceCount; ++v) {
            sink->WriteSamples(channels_[kCus30Base + v].Id, cus30_channel_scratch_[v].data(),
                               cus30_channel_scratch_[v].size());
        }
        sink->WriteSamples(channels_[kDac0Base].Id, dac0_channel_scratch_.data(), dac0_channel_scratch_.size());
        sink->WriteSamples(channels_[kDac1Base].Id, dac1_channel_scratch_.data(), dac1_channel_scratch_.size());
    }

    UpdateChannelStereoRouting();
}

void DragonSpiritMachine::SyncSoundDevices(std::uint64_t cumulativeM6809) {
    if (cumulativeM6809 <= sound_sync_cursor_) return;
    sound_sync_cursor_ = cumulativeM6809;

    {
        const std::uint64_t target = ym2151_from_m6809_.TicksToCycles(EmuDuration(cumulativeM6809));
        if (target > ym2151_cycles_issued_) {
            const std::uint64_t delta = target - ym2151_cycles_issued_;
            ym2151_cycles_issued_ = target;
            ym2151_->Render(delta, ym2151_channel_scratch_);
        }
    }
    {
        const std::uint64_t target = cus30_from_m6809_.TicksToCycles(EmuDuration(cumulativeM6809));
        if (target > cus30_cycles_issued_) {
            const std::uint64_t delta = target - cus30_cycles_issued_;
            cus30_cycles_issued_ = target;
            cus30_->Render(delta, cus30_channel_scratch_);
        }
    }
}

void DragonSpiritMachine::UpdateChannelStereoRouting() {
    // YM2151: pan and gain from the RL output bits.
    for (std::uint32_t ch = 0; ch < Ym2151DeviceAdapter::kChannelCount; ++ch) {
        const bool left = ym2151_->ChannelOutputsLeft(ch);
        const bool right = ym2151_->ChannelOutputsRight(ch);
        LogicalChannel& channel = channels_[kYm2151Base + ch];
        channel.RoutingGain = kYm2151RoutingGain * static_cast<float>((left ? 1 : 0) + (right ? 1 : 0));
        if (!channel.PanOverridden) {
            channel.Pan = (left == right) ? 0.0f : (left ? -1.0f : 1.0f);
        }
    }
    // CUS30: gain from the per-voice volumes, scaled to this chip's 4-bit-per-side range (max sum 30);
    // exact for the waveform path.
    for (std::uint32_t v = 0; v < Cus30DeviceAdapter::kVoiceCount; ++v) {
        const int left = cus30_->VoiceVolumeLeft(v);
        const int right = cus30_->VoiceVolumeRight(v);
        LogicalChannel& channel = channels_[kCus30Base + v];
        const int sum = left + right;
        channel.RoutingGain = kCus30RoutingGain * (static_cast<float>(sum) / 30.0f);
        if (!channel.PanOverridden) {
            channel.Pan = sum > 0 ? static_cast<float>(right - left) / static_cast<float>(sum) : 0.0f;
        }
    }
}

void DragonSpiritMachine::RunBootWarmup() {
    // Substitutes for the main CPU's CUS30 self-test write (kMainCpuCus30SelfTestByte), injected twice:
    // the main CPU writes it twice (from two PCs, ~0.5s apart on hardware), and the audio CPU's boot code
    // re-enters the $D000-$D011 self-test a second time after the first pass succeeds. Once past the gate
    // the audio CPU overwrites that byte itself, so a single injection would leave the second pass hung.
    // The warm-up budget is split in half and the byte re-injected partway through; the real ~0.35s gap
    // is not reproduced. The other main-CPU action (the TRIRAM gate byte) is handled in Render().
    const std::uint64_t half = (kBootWarmupFrames / 2 / kWarmupChunkFrames) * kWarmupChunkFrames;
    cus30_->Write8(0, kMainCpuCus30SelfTestByte);
    for (std::uint64_t rendered = 0; rendered < half; rendered += kWarmupChunkFrames) {
        Render(EmuDuration{kWarmupChunkFrames}, nullptr);
    }
    cus30_->Write8(0, kMainCpuCus30SelfTestByte);
    for (std::uint64_t rendered = half; rendered < kBootWarmupFrames; rendered += kWarmupChunkFrames) {
        Render(EmuDuration{kWarmupChunkFrames}, nullptr);
    }
}

void DragonSpiritMachine::SendCommand(const MusicCommand& command) {
    // This driver has no "silence everything" command: a new command swaps the FM voices but leaves the
    // CUS30 (WSG) and the DACs playing. So once anything has been sent (0x00 included), the next command
    // starts from a Reset() - which silences every device at once, without rendering anything (a
    // type="control" STOP track never starts playback). The very first command after Initialize()/Reset()
    // needs no reset. Channel Mute/Gain/Pan live in channels_, which Reset() does not touch.
    if (command_sent_since_reset_) {
        Reset();
    }
    if (command.Kind == MusicCommandKind::Wsg) {
        // A WSG sound effect is requested through the CUS30's shared RAM, not the TRIRAM mailbox
        // (0x100/0x101 stay untouched): clear the request table, then flag slot n.
        for (std::uint16_t i = 0; i < kWsgRequestTableSize; ++i) {
            cus30_->Write8(static_cast<std::uint16_t>(kWsgRequestTableOffset + i), 0x00);
        }
        cus30_->Write8(static_cast<std::uint16_t>(kWsgRequestTableOffset + 2u * command.Value), 0x01);
    } else {
        command_interface_->Send(command.Value);
    }
    command_sent_since_reset_ = true;
}

bool DragonSpiritMachine::AcceptsCommand(const MusicCommand& command) const {
    if (command.Kind == MusicCommandKind::Wsg) {
        return command.Value <= kMaxWsgNumber;
    }
    return true;
}

void DragonSpiritMachine::OnPanOverrideCleared(std::uint32_t) {
    // Pan is re-derived every Render() block by UpdateChannelStereoRouting(); nothing else to do.
}

MameDerivedM6809Adapter::RegisterSnapshot DragonSpiritMachine::M6809Snapshot() const { return m6809_->Snapshot(); }

std::uint8_t DragonSpiritMachine::ReadMemory(std::uint32_t physicalAddress) const {
    return memory_map_.Read8(physicalAddress);
}

void DragonSpiritMachine::WriteMemory(std::uint32_t physicalAddress, std::uint8_t value) {
    memory_map_.Write8(physicalAddress, value);
}
