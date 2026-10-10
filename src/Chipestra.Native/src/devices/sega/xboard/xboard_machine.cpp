// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "xboard_machine.h"

#include <string>
#include <utility>

namespace {

// Z80 I/O port decode:
//   YM2151 at port 0x00-0x3F (bit 0 selects address/data register);
//   command latch at 0x40-0x7F;
//   0x80-0xFF unmapped.
bool IsYm2151Port(std::uint8_t port) { return (port & 0xC0) == 0x00; }
bool IsCommandLatchPort(std::uint8_t port) { return (port & 0xC0) == 0x40; }
bool IsSegaPcmAddress(std::uint16_t addr) {
    return (addr & 0xF800) == XBoardMemoryMap::kSegaPcmStart;
}

}  // namespace

XBoardMachine::XBoardMachine(std::vector<std::uint8_t> z80_rom, std::vector<std::uint8_t> pcm_rom)
    : z80_rom_(std::move(z80_rom)),
      pcm_rom_(std::move(pcm_rom)),
      clock_converter_(kNativeRuntimeTickRateHz, kZ80ClockRate) {}

void XBoardMachine::Initialize() {
    memory_map_.LoadRom(z80_rom_.data(), z80_rom_.size());

    auto z80 = std::make_unique<MameDerivedZ80Adapter>();
    z80_ = z80.get();
    device_manager_.Add(std::move(z80));

    z80_->SetMemoryCallbacks(
        [this](std::uint16_t addr) {
            if (IsSegaPcmAddress(addr)) {
                SyncSoundDevices(z80_->CyclesConsumedThisCall());
                ++bus_activity_.SegaPcmReads;
            }
            return memory_map_.Read8(addr);
        },
        [this](std::uint16_t addr, std::uint8_t value) {
            if (IsSegaPcmAddress(addr)) {
                SyncSoundDevices(z80_->CyclesConsumedThisCall());
                ++bus_activity_.SegaPcmWrites;
            }
            memory_map_.Write8(addr, value);
        });

    command_interface_ = std::make_unique<XBoardCommandInterface>(*z80_);

    // YM2151 Timer A/B IRQ -> sound Z80 maskable IRQ line, distinct from the NMI of the command latch.
    auto ym2151 = std::make_unique<Ym2151DeviceAdapter>([this](bool asserted) {
        z80_->SetInputLine(Z80InputLine::Irq,
                           asserted ? Z80InputLineState::Assert : Z80InputLineState::Clear);
    });
    ym2151_ = ym2151.get();
    device_manager_.Add(std::move(ym2151));

    // SegaPCM (315-5218): 16-voice 8-bit PCM. X-Board wires BANK_512.
    auto segapcm =
        std::make_unique<SegaPcmDeviceAdapter>(std::move(pcm_rom_), SegaPcmDeviceAdapter::kBank512);
    segapcm_ = segapcm.get();
    device_manager_.Add(std::move(segapcm));
    // The sound Z80 reaches SegaPCM's registers at memory 0xF000-0xF0FF (mirror 0x0700).
    memory_map_.MapDevice(XBoardMemoryMap::kSegaPcmStart, XBoardMemoryMap::kSegaPcmSize, segapcm_);

    z80_->SetIoCallbacks(
        [this](std::uint8_t port) -> std::uint8_t {
            if (IsCommandLatchPort(port)) {
                ++bus_activity_.CommandLatchReads;
                return command_interface_->ReadLatchAndClearNmi();
            }
            if (IsYm2151Port(port)) {
                // Bring the YM2151 up to this exact Z80 cycle first: drivers busy-poll its status.
                SyncSoundDevices(z80_->CyclesConsumedThisCall());
                ++bus_activity_.Ym2151Reads;
                // The YM2151 returns the status byte for any read on its range.
                return ym2151_->ReadStatus();
            }
            // Unmapped high range reads as open bus.
            return 0xFF;
        },
        [this](std::uint8_t port, std::uint8_t value) {
            if (IsYm2151Port(port)) {
                SyncSoundDevices(z80_->CyclesConsumedThisCall());
                ++bus_activity_.Ym2151Writes;
                if ((port & 0x01) == 0) {
                    ym2151_latched_address_ = value;
                    ym2151_->WriteAddress(value);
                } else {
                    if (ym2151_write_observer_) {
                        ym2151_write_observer_(ym2151_latched_address_, value);
                    }
                    ym2151_->WriteData(value);
                }
            }
            // Command latch is read-only from the sound Z80; other writes drop.
        });

    // LogicalChannels: 8 YM2151 FM channels (ids 0-7) then 16 SegaPCM voices
    // (ids 8-23). Registered with the sink lazily on Render()'s first call.
    channels_.clear();
    for (std::uint32_t ch = 0; ch < Ym2151DeviceAdapter::kChannelCount; ++ch) {
        LogicalChannel channel;
        channel.Id = ch;
        channel.DeviceName = "YM2151";
        channel.ChannelName = "FM " + std::to_string(ch + 1);
        channel.NativeSampleRate = Ym2151DeviceAdapter::kOutputSampleRateHz;
        channel.RoutingGain = kYm2151RoutingGain;
        channels_.push_back(std::move(channel));
    }
    for (std::uint32_t v = 0; v < SegaPcmDeviceAdapter::kVoiceCount; ++v) {
        LogicalChannel channel;
        channel.Id = Ym2151DeviceAdapter::kChannelCount + v;
        channel.DeviceName = "SegaPCM";
        channel.ChannelName = "PCM " + std::to_string(v + 1);
        channel.NativeSampleRate = SegaPcmDeviceAdapter::kOutputSampleRateHz;
        channel.RoutingGain = kSegaPcmRoutingGain;
        channels_.push_back(std::move(channel));
    }
    channels_registered_ = false;
    ym_channel_scratch_.assign(Ym2151DeviceAdapter::kChannelCount, {});
    pcm_channel_scratch_.assign(SegaPcmDeviceAdapter::kVoiceCount, {});

    z80_->Reset();
    ym2151_->Reset();
    segapcm_->Reset();

    // Run the driver's boot sequence before returning so SendCommand() is safe
    // right afterwards (see kBootWarmupFrames). Channel registration waits for
    // the first sink-bearing Render() call.
    Render(EmuDuration{kBootWarmupFrames}, nullptr);
}

void XBoardMachine::Reset() {
    z80_->Reset();
    ym2151_->Reset();
    segapcm_->Reset();
    channels_registered_ = false;
    // Restart the carry-forward baseline from zero, as after Initialize().
    cumulative_ticks_processed_ = 0;
    cumulative_cycles_executed_ = 0;

    // Reset() leaves the same state as Initialize(), including the boot warm-up.
    Render(EmuDuration{kBootWarmupFrames}, nullptr);
}

void XBoardMachine::Shutdown() {
    // device_manager_'s destructor releases the devices; nothing else to release.
}

std::vector<LogicalChannel>& XBoardMachine::LogicalChannels() {
    return channels_;
}

void XBoardMachine::Render(EmuDuration duration, LogicalChannelSink* sink) {
    if (sink != nullptr && !channels_registered_) {
        for (const LogicalChannel& channel : channels_) {
            sink->RegisterChannel(channel.Id);
        }
        channels_registered_ = true;
    }

    // `duration` is in canonical 192kHz frames; clock_converter_ turns it into
    // 4MHz Z80 T-states. The YM2151 runs at the same clock and SegaPCM at 4x it.
    // SyncSoundDevices() catches the devices up to the CPU on every YM2151/SegaPCM
    // bus access, because drivers busy-poll the YM2151 status register and the
    // timer IRQ must reach the CPU within the same block.
    for (std::vector<float>& buffer : ym_channel_scratch_) {
        buffer.clear();
    }
    for (std::vector<float>& buffer : pcm_channel_scratch_) {
        buffer.clear();
    }

    // Carry-forward: this block's budget closes the gap between the cumulative
    // target and what has executed so far, keeping the long-run Z80 rate exact.
    cumulative_ticks_processed_ += duration.ticks;
    const std::uint64_t targetCumulativeCycles =
        clock_converter_.TicksToCycles(EmuDuration(cumulative_ticks_processed_));
    const std::uint64_t cycles =
        targetCumulativeCycles > cumulative_cycles_executed_ ? targetCumulativeCycles - cumulative_cycles_executed_ : 0;
    sound_sync_cursor_ = 0;
    z80_->Execute(cycles);
    cumulative_cycles_executed_ += z80_->CyclesConsumedThisCall();
    // Final catch-up to the end of the block (the Z80 may run past `cycles`).
    SyncSoundDevices(z80_->CyclesConsumedThisCall() > cycles ? z80_->CyclesConsumedThisCall() : cycles);

    UpdateChannelStereoRouting();

    if (sink != nullptr) {
        for (std::uint32_t ch = 0; ch < Ym2151DeviceAdapter::kChannelCount; ++ch) {
            sink->WriteSamples(channels_[ch].Id, ym_channel_scratch_[ch].data(),
                               ym_channel_scratch_[ch].size());
        }
        for (std::uint32_t v = 0; v < SegaPcmDeviceAdapter::kVoiceCount; ++v) {
            sink->WriteSamples(channels_[Ym2151DeviceAdapter::kChannelCount + v].Id,
                               pcm_channel_scratch_[v].data(), pcm_channel_scratch_[v].size());
        }
    }
}

void XBoardMachine::UpdateChannelStereoRouting() {
    // Maps each device's per-channel L/R state onto the LogicalChannel's Pan and
    // RoutingGain so the Mixer reproduces the board's stereo image. Block-granular.
    // Pan is skipped for channels the UI has overridden (PanOverridden);
    // RoutingGain always updates.
    //
    // YM2151: RL is on/off per bus at full level (not a proportional pan). A
    // channel routed to both buses is "dual mono" - full level on each side -
    // so RoutingGain scales with the number of enabled buses and Pan hard-sets
    // the side when only one is enabled.
    for (std::uint32_t ch = 0; ch < Ym2151DeviceAdapter::kChannelCount; ++ch) {
        const bool left = ym2151_->ChannelOutputsLeft(ch);
        const bool right = ym2151_->ChannelOutputsRight(ch);
        LogicalChannel& channel = channels_[ch];
        channel.RoutingGain = kYm2151RoutingGain * static_cast<float>((left ? 1 : 0) + (right ? 1 : 0));
        if (!channel.PanOverridden) {
            channel.Pan = (left == right) ? 0.0f : (left ? -1.0f : 1.0f);
        }
    }
    // SegaPCM: each voice has an independent 7-bit L and R volume. The raw
    // waveform is in the buffer; fold (lvol, rvol) back in as pan + gain so
    // that, with the Mixer's linear pan law, one side works out to exactly
    // MAME's `waveform * (vol & 0x7f) / 32768` (times the amp routing gain).
    for (std::uint32_t v = 0; v < SegaPcmDeviceAdapter::kVoiceCount; ++v) {
        const int left = segapcm_->VoiceVolumeLeft(v);
        const int right = segapcm_->VoiceVolumeRight(v);
        LogicalChannel& channel = channels_[Ym2151DeviceAdapter::kChannelCount + v];
        const int sum = left + right;
        channel.RoutingGain = kSegaPcmRoutingGain * (static_cast<float>(sum) / 256.0f);
        if (!channel.PanOverridden) {
            channel.Pan = sum > 0 ? static_cast<float>(right - left) / static_cast<float>(sum) : 0.0f;
        }
    }
}

void XBoardMachine::SyncSoundDevices(std::uint64_t z80CycleMark) {
    if (z80CycleMark <= sound_sync_cursor_) {
        return;
    }
    const std::uint64_t delta = z80CycleMark - sound_sync_cursor_;
    sound_sync_cursor_ = z80CycleMark;
    // YM2151 input clock == Z80 clock; SegaPCM master == 4x it.
    ym2151_->Render(delta, ym_channel_scratch_);
    segapcm_->Render(delta * kSegaPcmMasterClocksPerZ80Cycle, pcm_channel_scratch_);
}

void XBoardMachine::SendCommand(const MusicCommand& command) {
    command_interface_->Send(command);
}

MameDerivedZ80Adapter::RegisterSnapshot XBoardMachine::Z80Snapshot() const {
    return z80_->Snapshot();
}

std::uint8_t XBoardMachine::ReadMemory(std::uint16_t address) const {
    return memory_map_.Read8(address);
}

Ym2151DeviceAdapter& XBoardMachine::Ym2151ForTest() {
    return *ym2151_;
}

SegaPcmDeviceAdapter& XBoardMachine::SegaPcmForTest() {
    return *segapcm_;
}
