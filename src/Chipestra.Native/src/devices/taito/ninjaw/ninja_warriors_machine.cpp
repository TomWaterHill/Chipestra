// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "ninja_warriors_machine.h"

#include "address_space.h"

#include <string>
#include <utility>

// Dispatches the scattered addresses within NinjaWarriorsSoundMemoryMap::kIoStart/
// kIoSize (0xE000-0xF200). Offsets are relative to kIoStart.
class NinjaWarriorsMachine::IoBus final : public AddressSpaceEndpoint {
public:
    IoBus(Ym2610DeviceAdapter& ym2610, NinjaWarriorsCommandInterface& ciu, NinjaWarriorsMachine& owner)
        : ym2610_(ym2610), ciu_(ciu), owner_(owner) {}

    std::uint8_t Read8(std::uint64_t offset) const override {
        switch (offset) {
            case kYm2610RegBase + 0:
            case kYm2610RegBase + 1:
            case kYm2610RegBase + 2:
            case kYm2610RegBase + 3:
                owner_.SyncSoundDevices(owner_.audiocpu_->CyclesConsumedThisCall());
                return ym2610_.Read(static_cast<std::uint8_t>(offset - kYm2610RegBase));
            case kCiuSlaveCommOffset:
                return ciu_.SlaveCommR();
            default:
                // 0xE200 (slave_port_w) is a no-op read; the write-only
                // pancontrol_w and the unwired stubs also read as 0.
                return 0;
        }
    }

    void Write8(std::uint64_t offset, std::uint8_t value) override {
        switch (offset) {
            case kYm2610RegBase + 0:
            case kYm2610RegBase + 1:
            case kYm2610RegBase + 2:
            case kYm2610RegBase + 3:
                owner_.SyncSoundDevices(owner_.audiocpu_->CyclesConsumedThisCall());
                ym2610_.Write(static_cast<std::uint8_t>(offset - kYm2610RegBase), value);
                break;
            case kCiuSlavePortOffset:
                ciu_.SlavePortW(value);
                break;
            case kCiuSlaveCommOffset:
                ciu_.SlaveCommW(value);
                break;
            case kPanControl0Offset:
            case kPanControl1Offset:
            case kPanControl2Offset:
            case kPanControl3Offset:
                // Stored only - not wired to any LogicalChannel.
                owner_.pan_control_[offset - kPanControl0Offset] = value;
                break;
            case kBankswitchOffset:
                owner_.sound_memory_map_.SetBank(value & 0x07);
                break;
            default:
                // Unwired addresses in the window (no-op stubs) are accepted
                // and ignored.
                break;
        }
    }

private:
    // All offsets are address - NinjaWarriorsSoundMemoryMap::kIoStart (0xE000).
    static constexpr std::uint64_t kYm2610RegBase = 0x0000;      // 0xE000-0xE003
    static constexpr std::uint64_t kCiuSlavePortOffset = 0x0200;  // 0xE200
    static constexpr std::uint64_t kCiuSlaveCommOffset = 0x0201;  // 0xE201
    static constexpr std::uint64_t kPanControl0Offset = 0x0400;   // 0xE400
    static constexpr std::uint64_t kPanControl1Offset = 0x0401;   // 0xE401
    static constexpr std::uint64_t kPanControl2Offset = 0x0402;   // 0xE402
    static constexpr std::uint64_t kPanControl3Offset = 0x0403;   // 0xE403
    static constexpr std::uint64_t kBankswitchOffset = 0x1200;    // 0xF200

    Ym2610DeviceAdapter& ym2610_;
    NinjaWarriorsCommandInterface& ciu_;
    NinjaWarriorsMachine& owner_;
};

NinjaWarriorsMachine::NinjaWarriorsMachine(std::vector<std::uint8_t> audiocpu_rom,
                                           std::vector<std::uint8_t> adpcmARom,
                                           std::vector<std::uint8_t> adpcmBRom)
    : audiocpu_rom_(std::move(audiocpu_rom)),
      adpcm_a_rom_(std::move(adpcmARom)),
      adpcm_b_rom_(std::move(adpcmBRom)),
      clock_converter_(kNativeRuntimeTickRateHz, kZ80ClockRate) {}

// Out-of-line: io_bus_ (unique_ptr to a forward-declared nested type) needs
// its complete type, which only exists in this translation unit.
NinjaWarriorsMachine::~NinjaWarriorsMachine() = default;

void NinjaWarriorsMachine::Initialize() {
    sound_memory_map_.LoadRom(audiocpu_rom_.data(), audiocpu_rom_.size());

    auto audiocpu = std::make_unique<MameDerivedZ80Adapter>();
    audiocpu_ = audiocpu.get();
    device_manager_.Add(std::move(audiocpu));
    // Apply any pre-Initialize() test observer now, before the boot passes run.
    if (audiocpu_memory_write_observer_for_test_) {
        audiocpu_->SetMemoryWriteObserverForTest(audiocpu_memory_write_observer_for_test_);
    }
    if (audiocpu_opcode_fetch_observer_for_test_) {
        audiocpu_->SetOpcodeFetchObserverForTest(audiocpu_opcode_fetch_observer_for_test_);
    }

    // YM2610's irq_handler -> audiocpu's maskable IRQ line (direct forward).
    auto ym2610 = std::make_unique<Ym2610DeviceAdapter>(
        std::move(adpcm_a_rom_), std::move(adpcm_b_rom_), [this](bool asserted) {
            audiocpu_->SetInputLine(Z80InputLine::Irq, asserted ? Z80InputLineState::Assert : Z80InputLineState::Clear);
        });
    ym2610_ = ym2610.get();
    device_manager_.Add(std::move(ym2610));

    command_interface_ = std::make_unique<NinjaWarriorsCommandInterface>([this](bool asserted) {
        audiocpu_->SetInputLine(Z80InputLine::Nmi, asserted ? Z80InputLineState::Assert : Z80InputLineState::Clear);
    });

    io_bus_ = std::make_unique<IoBus>(*ym2610_, *command_interface_, *this);
    sound_memory_map_.MapDevice(NinjaWarriorsSoundMemoryMap::kIoStart, NinjaWarriorsSoundMemoryMap::kIoSize,
                                io_bus_.get());

    audiocpu_->SetMemoryCallbacks(
        [this](std::uint16_t addr) { return sound_memory_map_.Read8(addr); },
        [this](std::uint16_t addr, std::uint8_t value) { sound_memory_map_.Write8(addr, value); });
    // audiocpu has no separate I/O port space (sound_map is AS_PROGRAM only).
    audiocpu_->SetIoCallbacks([](std::uint8_t) -> std::uint8_t { return 0xFF; }, [](std::uint8_t, std::uint8_t) {});

    // LogicalChannels: FM (0-3), SSG (4-6), ADPCM-A (7-12), ADPCM-B (13),
    // registered with the sink lazily on Render()'s first call.
    channels_.clear();
    for (std::uint32_t ch = 0; ch < Ym2610DeviceAdapter::kFmChannelCount; ++ch) {
        LogicalChannel channel;
        channel.Id = kFmBase + ch;
        channel.DeviceName = "YM2610";
        channel.ChannelName = "FM " + std::to_string(ch + 1);
        channel.NativeSampleRate = Ym2610DeviceAdapter::kFmAdpcmOutputSampleRateHz;
        channel.RoutingGain = kFmAdpcmRoutingGain;
        channels_.push_back(std::move(channel));
    }
    for (std::uint32_t ch = 0; ch < Ym2610DeviceAdapter::kSsgChannelCount; ++ch) {
        LogicalChannel channel;
        channel.Id = kSsgBase + ch;
        channel.DeviceName = "YM2610";
        channel.ChannelName = "SSG " + std::to_string(ch + 1);
        channel.NativeSampleRate = Ym2610DeviceAdapter::kSsgOutputSampleRateHz;
        channel.RoutingGain = kSsgRoutingGain;
        channels_.push_back(std::move(channel));
    }
    for (std::uint32_t ch = 0; ch < Ym2610DeviceAdapter::kAdpcmAChannelCount; ++ch) {
        LogicalChannel channel;
        channel.Id = kAdpcmABase + ch;
        channel.DeviceName = "YM2610";
        channel.ChannelName = "ADPCM-A " + std::to_string(ch + 1);
        channel.NativeSampleRate = Ym2610DeviceAdapter::kFmAdpcmOutputSampleRateHz;
        channel.RoutingGain = kFmAdpcmRoutingGain;
        channels_.push_back(std::move(channel));
    }
    {
        LogicalChannel channel;
        channel.Id = kAdpcmBBase;
        channel.DeviceName = "YM2610";
        channel.ChannelName = "ADPCM-B";
        channel.NativeSampleRate = Ym2610DeviceAdapter::kFmAdpcmOutputSampleRateHz;
        channel.RoutingGain = kFmAdpcmRoutingGain;
        channels_.push_back(std::move(channel));
    }
    channels_registered_ = false;
    ym2610_channel_scratch_.assign(Ym2610DeviceAdapter::kChannelCount, {});

    audiocpu_->Reset();
    ym2610_->Reset();
    command_interface_->Reset();
    sound_memory_map_.SetBank(0);
    pan_control_[0] = pan_control_[1] = pan_control_[2] = pan_control_[3] = 0;
    sound_sync_cursor_ = 0;
    cumulative_ticks_processed_ = 0;
    cumulative_cycles_executed_ = 0;

    // Run the driver's boot sequence to completion before returning, so
    // SendCommand() is always safe to call immediately afterward.
    RunBootWarmup();
    // Replay the master's boot handshake (without it no later command has any effect).
    SendMasterBootHandshake();
}

void NinjaWarriorsMachine::Reset() {
    audiocpu_->Reset();
    ym2610_->Reset();
    command_interface_->Reset();
    sound_memory_map_.SetBank(0);
    pan_control_[0] = pan_control_[1] = pan_control_[2] = pan_control_[3] = 0;
    channels_registered_ = false;
    song_active_ = false;
    sound_sync_cursor_ = 0;
    // Restart the carry-forward baseline from zero, as in Initialize().
    cumulative_ticks_processed_ = 0;
    cumulative_cycles_executed_ = 0;

    RunBootWarmup();
    SendMasterBootHandshake();
}

void NinjaWarriorsMachine::Shutdown() {
    // device_manager_ releases audiocpu_/ym2610_; io_bus_/command_interface_ are
    // declared after it and so are destroyed first.
}

std::vector<LogicalChannel>& NinjaWarriorsMachine::LogicalChannels() { return channels_; }

void NinjaWarriorsMachine::Render(EmuDuration duration, LogicalChannelSink* sink) {
    if (sink != nullptr && !channels_registered_) {
        for (const LogicalChannel& channel : channels_) {
            sink->RegisterChannel(channel.Id);
        }
        channels_registered_ = true;
    }

    for (std::vector<float>& buffer : ym2610_channel_scratch_) {
        buffer.clear();
    }

    // Carry-forward cadence: this block's budget closes the gap between the
    // cumulative target (as of the block's end) and what has executed so far.
    cumulative_ticks_processed_ += duration.ticks;
    const std::uint64_t targetCumulativeCycles =
        clock_converter_.TicksToCycles(EmuDuration(cumulative_ticks_processed_));
    const std::uint64_t cycles =
        targetCumulativeCycles > cumulative_cycles_executed_ ? targetCumulativeCycles - cumulative_cycles_executed_ : 0;
    sound_sync_cursor_ = 0;
    audiocpu_->Execute(cycles);
    cumulative_cycles_executed_ += audiocpu_->CyclesConsumedThisCall();
    // Final catch-up: advance YM2610 to the end of the block.
    SyncSoundDevices(audiocpu_->CyclesConsumedThisCall() > cycles ? audiocpu_->CyclesConsumedThisCall() : cycles);

    UpdateChannelStereoRouting();

    if (sink != nullptr) {
        for (std::uint32_t ch = 0; ch < Ym2610DeviceAdapter::kChannelCount; ++ch) {
            sink->WriteSamples(channels_[ch].Id, ym2610_channel_scratch_[ch].data(),
                               ym2610_channel_scratch_[ch].size());
        }
    }
}

void NinjaWarriorsMachine::UpdateChannelStereoRouting() {
    // On this board the FM/ADPCM-A/ADPCM-B section is unconditionally mono. The
    // YM2610's per-channel L/R output bits do not reach the speakers as stereo:
    // the machine config sums the chip's two raw internal outputs onto BOTH
    // physical channels equally (unlike a 1:1 YM2610 wiring that routes each raw
    // output to one side). The only remaining divergence would be the four gains
    // the Z80 writes via pancontrol_w (0xE400-0xE403), but the ROM always writes
    // the same value to all four (a uniform fade, not a pan). So all channels are
    // left centered unless the user has panned one (PanOverridden is honored).
    for (std::uint32_t ch = 0; ch < Ym2610DeviceAdapter::kFmChannelCount; ++ch) {
        LogicalChannel& channel = channels_[kFmBase + ch];
        if (!channel.PanOverridden) {
            channel.Pan = 0.0f;
        }
    }
    for (std::uint32_t ch = 0; ch < Ym2610DeviceAdapter::kAdpcmAChannelCount; ++ch) {
        LogicalChannel& channel = channels_[kAdpcmABase + ch];
        if (!channel.PanOverridden) {
            channel.Pan = 0.0f;
        }
    }
    {
        LogicalChannel& channel = channels_[kAdpcmBBase];
        if (!channel.PanOverridden) {
            channel.Pan = 0.0f;
        }
    }
    // SSG has no per-channel pan on real hardware either; left centered.
}

void NinjaWarriorsMachine::SyncSoundDevices(std::uint64_t audioCpuCycleMark) {
    if (audioCpuCycleMark <= sound_sync_cursor_) {
        return;
    }
    const std::uint64_t delta = audioCpuCycleMark - sound_sync_cursor_;
    sound_sync_cursor_ = audioCpuCycleMark;
    ym2610_->Render(delta * kYm2610ClocksPerZ80Cycle, ym2610_channel_scratch_);
}

void NinjaWarriorsMachine::RunBootWarmup() {
    static_assert(kBootWarmupFrames % 256 == 0, "kBootWarmupFrames must be an exact multiple of the warmup chunk");
    for (std::uint64_t rendered = 0; rendered < kBootWarmupFrames; rendered += 256) {
        Render(EmuDuration{256}, nullptr);
    }
}

void NinjaWarriorsMachine::SendMasterBootHandshake() {
    static_assert(kMasterHandshakeGapFrames % 256 == 0,
                  "kMasterHandshakeGapFrames must be an exact multiple of the warmup chunk");
    // A full render gap separates each send: the TC0140SYT mailbox is a single
    // two-nibble register, not a queue.
    command_interface_->Send(0xEF);
    for (std::uint64_t rendered = 0; rendered < kMasterHandshakeGapFrames; rendered += 256) {
        Render(EmuDuration{256}, nullptr);
    }
    command_interface_->Send(0x5A);
    for (std::uint64_t rendered = 0; rendered < kMasterHandshakeGapFrames; rendered += 256) {
        Render(EmuDuration{256}, nullptr);
    }
    command_interface_->Send(0x3B);
    for (std::uint64_t rendered = 0; rendered < kMasterHandshakeGapFrames; rendered += 256) {
        Render(EmuDuration{256}, nullptr);
    }
    // The 0x5A/0x3B pair starts the real machine's attract sequence, which keeps
    // running on its own (with no commands at all it plays an ADPCM-A voice 32.2 s
    // after 0x3B). A music player must play only the picked track, so finish the
    // handshake with the game's own STOP (0x00) and let audiocpu drain it.
    command_interface_->Send(0x00);
    for (std::uint64_t rendered = 0; rendered < kMasterHandshakeGapFrames; rendered += 256) {
        Render(EmuDuration{256}, nullptr);
    }
}

void NinjaWarriorsMachine::SendCommand(const MusicCommand& command) {
    // Send() drives the MASTER side (the 68000 is not emulated) through the
    // mode-0/mode-1 two-nibble sequence; audiocpu picks it up via its ROM code at
    // 0xE200/0xE201.
    //
    // The game's sound driver does not always silence the running song when a new
    // command arrives (e.g. Metal Black 0x0E -> 0x0A keeps 0x0E's ADPCM-A channels
    // running under 0x0A, while 0x0E -> 0x00 -> 0x0A matches 0x0A alone). So when a
    // song is already running, pass the game's own STOP first and let audiocpu
    // drain it (a full single-mailbox gap, rendered silently) before the requested
    // command. 2 s is enough (1, 2 and 4 s all matched over 81 Metal Black pairs).
    // Called by the Player only while the pump is stopped.
    if (song_active_ && command.Value != 0x00) {
        command_interface_->Send(0x00);
        for (std::uint64_t rendered = 0; rendered < kSwitchStopGapFrames; rendered += 256) {
            Render(EmuDuration{256}, nullptr);
        }
    }
    command_interface_->Send(command.Value);
    song_active_ = command.Value != 0x00;
}
