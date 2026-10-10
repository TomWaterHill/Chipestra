// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "vapor_trail_machine.h"

#include "address_space.h"

#include <algorithm>
#include <functional>
#include <utility>

// Generic std::function-backed AddressSpaceEndpoint, local glue that wires VaporTrailMemoryMap's five
// independent device windows (YM2203/YM2151/OKI1/OKI2/latch) onto this Machine's devices.
class VaporTrailMachine::CallbackEndpoint final : public AddressSpaceEndpoint {
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

VaporTrailMachine::VaporTrailMachine(std::vector<std::uint8_t> soundcpu_rom, std::vector<std::uint8_t> oki1_rom,
                                     std::vector<std::uint8_t> oki2_rom)
    : soundcpu_rom_(std::move(soundcpu_rom)),
      oki1_rom_(std::move(oki1_rom)),
      oki2_rom_(std::move(oki2_rom)),
      clock_converter_(kNativeRuntimeTickRateHz, kH6280ClockRate),
      ym2151_from_h6280_(kH6280ClockRate.hz, kYm2151ClockRate),
      ym2203_from_h6280_(kH6280ClockRate.hz, kYm2203ClockRate),
      oki1_from_h6280_(kH6280ClockRate.hz, kOki1ClockRate),
      oki2_from_h6280_(kH6280ClockRate.hz, kOki2ClockRate) {}

VaporTrailMachine::~VaporTrailMachine() = default;

void VaporTrailMachine::Initialize() {
    memory_map_.LoadRom(soundcpu_rom_.data(), soundcpu_rom_.size());

    auto h6280 = std::make_unique<MameDerivedH6280Adapter>();
    h6280_ = h6280.get();
    device_manager_.Add(std::move(h6280));
    h6280_->SetTimerScale(kH6280TimerScale);  // must precede Reset() (IH6280Core::SetTimerScale's own comment)
    // Apply the pre-Initialize() test observers before the first boot pass.
    if (h6280_opcode_fetch_observer_for_test_) {
        h6280_->SetOpcodeFetchObserverForTest(h6280_opcode_fetch_observer_for_test_);
    }
    if (h6280_memory_read_observer_for_test_) {
        h6280_->SetMemoryReadObserverForTest(h6280_memory_read_observer_for_test_);
    }
    if (h6280_memory_write_observer_for_test_) {
        h6280_->SetMemoryWriteObserverForTest(h6280_memory_write_observer_for_test_);
    }

    // YM2151 Timer A/B IRQ -> H6280 IRQ2. YM2203's IRQ is not wired on real hardware (null handler).
    auto ym2151 = std::make_unique<Ym2151DeviceAdapter>([this](bool asserted) {
        h6280_->SetInputLine(H6280InputLine::Irq2, asserted ? H6280InputLineState::Assert : H6280InputLineState::Clear);
    });
    ym2151_ = ym2151.get();
    device_manager_.Add(std::move(ym2151));

    auto ym2203 = std::make_unique<Ym2203DeviceAdapter>();
    ym2203_ = ym2203.get();
    device_manager_.Add(std::move(ym2203));

    auto oki1 = std::make_unique<Okim6295DeviceAdapter>(std::move(oki1_rom_), Okim6295DeviceAdapter::Pin7::High);
    oki1_ = oki1.get();
    device_manager_.Add(std::move(oki1));

    auto oki2 = std::make_unique<Okim6295DeviceAdapter>(std::move(oki2_rom_), Okim6295DeviceAdapter::Pin7::High);
    oki2_ = oki2.get();
    device_manager_.Add(std::move(oki2));

    command_interface_ = std::make_unique<VaporTrailCommandInterface>(*h6280_);

    // YM2203 (0x100000 address, 0x100001 data); reads return the status byte regardless of offset.
    endpoints_[0] = std::make_unique<CallbackEndpoint>(
        [this](std::uint64_t) -> std::uint8_t {
            SyncSoundDevices(CurrentBlockCycles());
            return ym2203_->ReadStatus();
        },
        [this](std::uint64_t offset, std::uint8_t value) {
            SyncSoundDevices(CurrentBlockCycles());
            if (offset == 0) ym2203_->WriteAddress(value); else ym2203_->WriteData(value);
        });
    memory_map_.MapDevice(VaporTrailMemoryMap::kYm2203Start, VaporTrailMemoryMap::kYm2203Size, endpoints_[0].get());

    // YM2151 (0x110000 address, 0x110001 data).
    endpoints_[1] = std::make_unique<CallbackEndpoint>(
        [this](std::uint64_t) -> std::uint8_t {
            SyncSoundDevices(CurrentBlockCycles());
            return ym2151_->ReadStatus();
        },
        [this](std::uint64_t offset, std::uint8_t value) {
            SyncSoundDevices(CurrentBlockCycles());
            if (offset == 0) ym2151_->WriteAddress(value); else ym2151_->WriteData(value);
        });
    memory_map_.MapDevice(VaporTrailMemoryMap::kYm2151Start, VaporTrailMemoryMap::kYm2151Size, endpoints_[1].get());

    // OKI1 (0x120000, single command/status address).
    endpoints_[2] = std::make_unique<CallbackEndpoint>(
        [this](std::uint64_t) -> std::uint8_t {
            SyncSoundDevices(CurrentBlockCycles());
            return oki1_->Read();
        },
        [this](std::uint64_t, std::uint8_t value) {
            SyncSoundDevices(CurrentBlockCycles());
            oki1_->Write(value);
        });
    memory_map_.MapDevice(VaporTrailMemoryMap::kOki1Start, VaporTrailMemoryMap::kOki1Size, endpoints_[2].get());

    // OKI2 (0x130000, single command/status address).
    endpoints_[3] = std::make_unique<CallbackEndpoint>(
        [this](std::uint64_t) -> std::uint8_t {
            SyncSoundDevices(CurrentBlockCycles());
            return oki2_->Read();
        },
        [this](std::uint64_t, std::uint8_t value) {
            SyncSoundDevices(CurrentBlockCycles());
            oki2_->Write(value);
        });
    memory_map_.MapDevice(VaporTrailMemoryMap::kOki2Start, VaporTrailMemoryMap::kOki2Size, endpoints_[3].get());

    // Sound latch (0x140000/0x140001, read-only): both bytes return the latched value and clear IRQ1.
    endpoints_[4] = std::make_unique<CallbackEndpoint>(
        [this](std::uint64_t) -> std::uint8_t { return command_interface_->ReadLatchAndClearIrq1(); },
        [](std::uint64_t, std::uint8_t) {});
    memory_map_.MapDevice(VaporTrailMemoryMap::kLatchStart, VaporTrailMemoryMap::kLatchSize, endpoints_[4].get());

    h6280_->SetMemoryCallbacks(
        [this](std::uint32_t addr) { return memory_map_.Read8(addr); },
        [this](std::uint32_t addr, std::uint8_t value) { memory_map_.Write8(addr, value); });

    // The adapter constructor already ran Reset() before the memory callbacks were wired, so the reset vector
    // was read as 0x00. The H6280 reads its reset PC from memory (vector at 0xFFFE/0xFFFF), so Reset() must
    // run again now that ROM and callbacks are live.
    h6280_->Reset();

    // LogicalChannels follow the id layout in the header; Pan stays 0 because the hardware is mono.
    channels_.clear();
    for (std::uint32_t ch = 0; ch < Ym2203DeviceAdapter::kFmChannelCount; ++ch) {
        LogicalChannel channel;
        channel.Id = kYm2203FmBase + ch;
        channel.DeviceName = "YM2203";
        channel.ChannelName = "FM" + std::to_string(ch + 1);
        channel.NativeSampleRate = static_cast<std::uint32_t>(kYm2203ClockRate.hz / Ym2203DeviceAdapter::kInputClocksPerSample);
        channel.RoutingGain = kYm2203RoutingGain;
        channels_.push_back(std::move(channel));
    }
    for (std::uint32_t ch = 0; ch < Ym2203DeviceAdapter::kSsgChannelCount; ++ch) {
        LogicalChannel channel;
        channel.Id = kYm2203SsgBase + ch;
        channel.DeviceName = "YM2203";
        channel.ChannelName = "SSG" + std::to_string(ch + 1);
        channel.NativeSampleRate = static_cast<std::uint32_t>(kYm2203ClockRate.hz / Ym2203DeviceAdapter::kInputClocksPerSample);
        channel.RoutingGain = kYm2203RoutingGain;
        channels_.push_back(std::move(channel));
    }
    for (std::uint32_t ch = 0; ch < Ym2151DeviceAdapter::kChannelCount; ++ch) {
        LogicalChannel channel;
        channel.Id = kYm2151Base + ch;
        channel.DeviceName = "YM2151";
        channel.ChannelName = "FM" + std::to_string(ch + 1);
        channel.NativeSampleRate = static_cast<std::uint32_t>(kYm2151ClockRate.hz / Ym2151DeviceAdapter::kInputClocksPerSample);
        channel.RoutingGain = kYm2151RoutingGain;
        channels_.push_back(std::move(channel));
    }
    for (std::uint32_t v = 0; v < Okim6295DeviceAdapter::kVoiceCount; ++v) {
        LogicalChannel channel;
        channel.Id = kOki1Base + v;
        channel.DeviceName = "OKIM6295 #1";
        channel.ChannelName = "Voice" + std::to_string(v + 1);
        channel.NativeSampleRate = static_cast<std::uint32_t>(kOki1ClockRate.hz / oki1_->Divisor());
        channel.RoutingGain = kOki1RoutingGain;
        channels_.push_back(std::move(channel));
    }
    for (std::uint32_t v = 0; v < Okim6295DeviceAdapter::kVoiceCount; ++v) {
        LogicalChannel channel;
        channel.Id = kOki2Base + v;
        channel.DeviceName = "OKIM6295 #2";
        channel.ChannelName = "Voice" + std::to_string(v + 1);
        channel.NativeSampleRate = static_cast<std::uint32_t>(kOki2ClockRate.hz / oki2_->Divisor());
        channel.RoutingGain = kOki2RoutingGain;
        channels_.push_back(std::move(channel));
    }
    channels_registered_ = false;
    ym2151_channel_scratch_.assign(Ym2151DeviceAdapter::kChannelCount, {});
    ym2203_channel_scratch_.assign(Ym2203DeviceAdapter::kChannelCount, {});
    oki1_channel_scratch_.assign(Okim6295DeviceAdapter::kVoiceCount, {});
    oki2_channel_scratch_.assign(Okim6295DeviceAdapter::kVoiceCount, {});

    cumulative_ticks_processed_ = 0;
    cumulative_cycles_executed_ = 0;
    block_base_cycles_ = 0;
    sound_sync_cursor_ = 0;
    ym2151_cycles_issued_ = 0;
    ym2203_cycles_issued_ = 0;
    oki1_cycles_issued_ = 0;
    oki2_cycles_issued_ = 0;
    song_active_ = false;

    // A command sent between Initialize() and the first Render() must not reach the H6280 before its
    // reset-vector boot code has run; the warm-up plus boot handshake make SendCommand() safe afterwards.
    RunBootWarmup();
    SendMasterBootSequence();
}

void VaporTrailMachine::Reset() {
    h6280_->Reset();
    ym2151_->Reset();
    ym2203_->Reset();
    oki1_->Reset();
    oki2_->Reset();
    channels_registered_ = false;
    cumulative_ticks_processed_ = 0;
    cumulative_cycles_executed_ = 0;
    block_base_cycles_ = 0;
    sound_sync_cursor_ = 0;
    ym2151_cycles_issued_ = 0;
    ym2203_cycles_issued_ = 0;
    oki1_cycles_issued_ = 0;
    oki2_cycles_issued_ = 0;
    song_active_ = false;

    RunBootWarmup();
    SendMasterBootSequence();
}

void VaporTrailMachine::Shutdown() {
    // device_manager_ releases the devices; endpoints_/command_interface_ are declared after it, so they
    // are destroyed first.
}

std::vector<LogicalChannel>& VaporTrailMachine::LogicalChannels() { return channels_; }

void VaporTrailMachine::Render(EmuDuration duration, LogicalChannelSink* sink) {
    if (sink != nullptr && !channels_registered_) {
        for (const LogicalChannel& channel : channels_) {
            sink->RegisterChannel(channel.Id);
        }
        channels_registered_ = true;
    }

    for (std::vector<float>& buffer : ym2151_channel_scratch_) buffer.clear();
    for (std::vector<float>& buffer : ym2203_channel_scratch_) buffer.clear();
    for (std::vector<float>& buffer : oki1_channel_scratch_) buffer.clear();
    for (std::vector<float>& buffer : oki2_channel_scratch_) buffer.clear();

    // Carry-forward cadence: this block's cycle budget closes the gap between the cumulative target and
    // what has executed so far.
    cumulative_ticks_processed_ += duration.ticks;
    const std::uint64_t targetCumulativeCycles = clock_converter_.TicksToCycles(EmuDuration(cumulative_ticks_processed_));
    const std::uint64_t cycles =
        targetCumulativeCycles > cumulative_cycles_executed_ ? targetCumulativeCycles - cumulative_cycles_executed_ : 0;
    block_base_cycles_ = cumulative_cycles_executed_;
    // Use Execute()'s return value for the cycles consumed: CyclesConsumedThisCall() is 0 outside an
    // Execute() call in progress. Execute in kSteadyStateSyncChunkCycles pieces and sync the devices after
    // each piece, because the driver's idle loop makes no bus accesses that would sync them (see header).
    std::uint64_t cyclesRemaining = cycles;
    std::uint64_t executeReturn = 0;
    while (cyclesRemaining > 0) {
        const std::uint64_t chunkBudget = std::min(cyclesRemaining, kSteadyStateSyncChunkCycles);
        const std::uint64_t chunkReturn = h6280_->Execute(chunkBudget);
        cumulative_cycles_executed_ += chunkReturn;
        executeReturn += chunkReturn;
        SyncSoundDevices(block_base_cycles_ + executeReturn);
        cyclesRemaining = (chunkReturn < cyclesRemaining) ? cyclesRemaining - chunkReturn : 0;
    }
    if (render_block_observer_for_test_) {
        render_block_observer_for_test_(cycles, executeReturn, cumulative_cycles_executed_);
    }

    if (sink != nullptr) {
        for (std::uint32_t ch = 0; ch < Ym2203DeviceAdapter::kFmChannelCount; ++ch) {
            sink->WriteSamples(channels_[kYm2203FmBase + ch].Id, ym2203_channel_scratch_[ch].data(),
                               ym2203_channel_scratch_[ch].size());
        }
        for (std::uint32_t ch = 0; ch < Ym2203DeviceAdapter::kSsgChannelCount; ++ch) {
            const std::uint32_t src = Ym2203DeviceAdapter::kFmChannelCount + ch;
            sink->WriteSamples(channels_[kYm2203SsgBase + ch].Id, ym2203_channel_scratch_[src].data(),
                               ym2203_channel_scratch_[src].size());
        }
        for (std::uint32_t ch = 0; ch < Ym2151DeviceAdapter::kChannelCount; ++ch) {
            sink->WriteSamples(channels_[kYm2151Base + ch].Id, ym2151_channel_scratch_[ch].data(),
                               ym2151_channel_scratch_[ch].size());
        }
        for (std::uint32_t v = 0; v < Okim6295DeviceAdapter::kVoiceCount; ++v) {
            sink->WriteSamples(channels_[kOki1Base + v].Id, oki1_channel_scratch_[v].data(), oki1_channel_scratch_[v].size());
        }
        for (std::uint32_t v = 0; v < Okim6295DeviceAdapter::kVoiceCount; ++v) {
            sink->WriteSamples(channels_[kOki2Base + v].Id, oki2_channel_scratch_[v].data(), oki2_channel_scratch_[v].size());
        }
    }
}

void VaporTrailMachine::SyncSoundDevices(std::uint64_t cumulativeH6280) {
    // `cumulativeH6280` is a cumulative H6280 cycle mark since Initialize()/Reset(), not a per-block delta:
    // callers add block_base_cycles_ themselves. sound_sync_cursor_ is cumulative for the machine lifetime.
    if (cumulativeH6280 <= sound_sync_cursor_) {
        return;
    }
    sound_sync_cursor_ = cumulativeH6280;

    {
        const std::uint64_t target = ym2151_from_h6280_.TicksToCycles(EmuDuration(cumulativeH6280));
        if (target > ym2151_cycles_issued_) {
            const std::uint64_t delta = target - ym2151_cycles_issued_;
            ym2151_cycles_issued_ = target;
            ym2151_->Render(delta, ym2151_channel_scratch_);
        }
    }
    {
        const std::uint64_t target = ym2203_from_h6280_.TicksToCycles(EmuDuration(cumulativeH6280));
        if (target > ym2203_cycles_issued_) {
            const std::uint64_t delta = target - ym2203_cycles_issued_;
            ym2203_cycles_issued_ = target;
            ym2203_->Render(delta, ym2203_channel_scratch_);
        }
    }
    {
        const std::uint64_t target = oki1_from_h6280_.TicksToCycles(EmuDuration(cumulativeH6280));
        if (target > oki1_cycles_issued_) {
            const std::uint64_t delta = target - oki1_cycles_issued_;
            oki1_cycles_issued_ = target;
            oki1_->Render(delta, oki1_channel_scratch_);
        }
    }
    {
        const std::uint64_t target = oki2_from_h6280_.TicksToCycles(EmuDuration(cumulativeH6280));
        if (target > oki2_cycles_issued_) {
            const std::uint64_t delta = target - oki2_cycles_issued_;
            oki2_cycles_issued_ = target;
            oki2_->Render(delta, oki2_channel_scratch_);
        }
    }
}

void VaporTrailMachine::RenderExactlyTo(std::uint64_t& renderedSoFar, std::uint64_t targetTicks) {
    // Renders in small chunks so a self-generated periodic event never fires before the CPU has executed
    // past that point, and clamps the last chunk so `renderedSoFar` lands exactly on `targetTicks` (the boot
    // byte timing needs near single-instruction precision).
    while (renderedSoFar < targetTicks) {
        const std::uint64_t chunk = std::min(kWarmupChunkFrames, targetTicks - renderedSoFar);
        Render(EmuDuration{chunk}, nullptr);
        renderedSoFar += chunk;
    }
}

void VaporTrailMachine::RunBootWarmup() {
    // No command is sent here; this lets the driver's reset-vector boot code run before anything reaches it.
    std::uint64_t rendered = 0;
    RenderExactlyTo(rendered, kBootLatchWrites[0].canonicalTickOffset);
}

void VaporTrailMachine::SendMasterBootSequence() {
    // Replays the boot latch sequence at its real relative timing, in small chunks between bytes so IRQ1 is
    // delivered when real hardware would.
    std::uint64_t renderedUpTo = kBootLatchWrites[0].canonicalTickOffset;
    for (std::size_t i = 0; i < std::size(kBootLatchWrites); ++i) {
        command_interface_->Send(kBootLatchWrites[i].value);
        const std::uint64_t nextTarget =
            (i + 1 < std::size(kBootLatchWrites)) ? kBootLatchWrites[i + 1].canonicalTickOffset : renderedUpTo;
        RenderExactlyTo(renderedUpTo, nextTarget);
    }

    // Let the driver settle into whatever the boot sequence started (an attract-mode track) before silencing it.
    for (std::uint64_t rendered = 0; rendered < kBootSettleGapFrames; rendered += kWarmupChunkFrames) {
        Render(EmuDuration{kWarmupChunkFrames}, nullptr);
    }

    // Finish with the driver's STOP so nothing audible is left running before the first real SendCommand().
    command_interface_->Send(0x00);
    for (std::uint64_t rendered = 0; rendered < kStopSettleGapFrames; rendered += kWarmupChunkFrames) {
        Render(EmuDuration{kWarmupChunkFrames}, nullptr);
    }
    song_active_ = false;
}

void VaporTrailMachine::SendCommand(const MusicCommand& command) {
    // When a song is already running, send the driver's STOP first and let it settle so only the newly chosen
    // track plays.
    if (song_active_ && command.Value != 0x00) {
        command_interface_->Send(0x00);
        for (std::uint64_t rendered = 0; rendered < kSwitchStopGapFrames; rendered += kWarmupChunkFrames) {
            Render(EmuDuration{kWarmupChunkFrames}, nullptr);
        }
    }
    command_interface_->Send(command.Value);
    song_active_ = command.Value != 0x00;
}

void VaporTrailMachine::OnPanOverrideCleared(std::uint32_t) {
    // Intentionally empty: the hardware is mono and every channel stays centered.
}

MameDerivedH6280Adapter::RegisterSnapshot VaporTrailMachine::H6280Snapshot() const { return h6280_->Snapshot(); }

std::uint8_t VaporTrailMachine::ReadMemory(std::uint32_t physicalAddress) const {
    return memory_map_.Read8(physicalAddress);
}

void VaporTrailMachine::WriteMemory(std::uint32_t physicalAddress, std::uint8_t value) {
    memory_map_.Write8(physicalAddress, value);
}
