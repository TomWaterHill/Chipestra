// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "outrun_machine.h"

#include <string>
#include <utility>

namespace {

// Z80 I/O port decode (same as X-Board):
//   YM2151 at port 0x00-0x3F (bit 0 selects address/data register);
//   command latch at 0x40-0x7F;
//   0x80-0xFF unmapped.
bool IsYm2151Port(std::uint8_t port) { return (port & 0xC0) == 0x00; }
bool IsCommandLatchPort(std::uint8_t port) { return (port & 0xC0) == 0x40; }
bool IsSegaPcmAddress(std::uint16_t addr) {
    return (addr & 0xF800) == OutRunMemoryMap::kSegaPcmStart;
}

}  // namespace

OutRunMachine::OutRunMachine(std::vector<std::uint8_t> z80_rom, std::vector<std::uint8_t> pcm_rom)
    : z80_rom_(std::move(z80_rom)),
      pcm_rom_(std::move(pcm_rom)),
      clock_converter_(kNativeRuntimeTickRateHz, kZ80ClockRate) {}

void OutRunMachine::Initialize() {
    memory_map_.LoadRom(z80_rom_.data(), z80_rom_.size());

    auto z80 = std::make_unique<MameDerivedZ80Adapter>();
    z80_ = z80.get();
    device_manager_.Add(std::move(z80));

    z80_->SetMemoryCallbacks(
        [this](std::uint16_t addr) {
            if (IsSegaPcmAddress(addr)) {
                SyncSoundDevices(z80_->CyclesConsumedThisCall());
            }
            return memory_map_.Read8(addr);
        },
        [this](std::uint16_t addr, std::uint8_t value) {
            if (IsSegaPcmAddress(addr)) {
                SyncSoundDevices(z80_->CyclesConsumedThisCall());
            }
            memory_map_.Write8(addr, value);
        });

    command_interface_ = std::make_unique<OutRunCommandInterface>(*z80_);

    // The YM2151's IRQ pin is not wired on Out Run (X-Board connects it to the
    // Z80 IRQ line), so a no-op callback reproduces the wiring.
    auto ym2151 = std::make_unique<Ym2151DeviceAdapter>([](bool /*asserted*/) {});
    ym2151_ = ym2151.get();
    device_manager_.Add(std::move(ym2151));

    // SegaPCM (315-5218): 16-voice 8-bit PCM. Out Run wires BANK_512, same as X-Board.
    auto segapcm =
        std::make_unique<SegaPcmDeviceAdapter>(std::move(pcm_rom_), SegaPcmDeviceAdapter::kBank512);
    segapcm_ = segapcm.get();
    device_manager_.Add(std::move(segapcm));
    // The sound Z80 reaches SegaPCM's registers at memory 0xF000-0xF0FF (mirror 0x0700).
    memory_map_.MapDevice(OutRunMemoryMap::kSegaPcmStart, OutRunMemoryMap::kSegaPcmSize, segapcm_);

    z80_->SetIoCallbacks(
        [this](std::uint8_t port) -> std::uint8_t {
            if (IsCommandLatchPort(port)) {
                return command_interface_->ReadLatchAndClearNmi();
            }
            if (IsYm2151Port(port)) {
                // Bring the YM2151 up to this exact Z80 cycle first: drivers busy-poll its status.
                SyncSoundDevices(z80_->CyclesConsumedThisCall());
                // The YM2151 returns the status byte for any read on its range.
                return ym2151_->ReadStatus();
            }
            // Unmapped high range reads as open bus.
            return 0xFF;
        },
        [this](std::uint8_t port, std::uint8_t value) {
            if (IsYm2151Port(port)) {
                SyncSoundDevices(z80_->CyclesConsumedThisCall());
                if ((port & 0x01) == 0) {
                    ym2151_latched_address_ = value;
                    ym2151_->WriteAddress(value);
                } else {
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
    // right afterwards (see kBootWarmupFrames).
    Render(EmuDuration{kBootWarmupFrames}, nullptr);
}

void OutRunMachine::Reset() {
    z80_->Reset();
    ym2151_->Reset();
    segapcm_->Reset();
    channels_registered_ = false;
    // Restart the carry-forward baseline from zero, as after Initialize().
    cumulative_ticks_processed_ = 0;
    cumulative_cycles_executed_ = 0;

    Render(EmuDuration{kBootWarmupFrames}, nullptr);
}

void OutRunMachine::Shutdown() {
    // device_manager_'s destructor releases the devices; nothing else to release.
}

std::vector<LogicalChannel>& OutRunMachine::LogicalChannels() {
    return channels_;
}

void OutRunMachine::Render(EmuDuration duration, LogicalChannelSink* sink) {
    if (sink != nullptr && !channels_registered_) {
        for (const LogicalChannel& channel : channels_) {
            sink->RegisterChannel(channel.Id);
        }
        channels_registered_ = true;
    }

    // `duration` is in canonical 192kHz frames; clock_converter_ turns it into
    // 4MHz Z80 T-states. The YM2151 runs at the same clock and SegaPCM at 4x it.
    // SyncSoundDevices() catches the devices up to the CPU on every YM2151/SegaPCM
    // bus access.
    for (std::vector<float>& buffer : ym_channel_scratch_) {
        buffer.clear();
    }
    for (std::vector<float>& buffer : pcm_channel_scratch_) {
        buffer.clear();
    }

    // One relatch per block while a Send() burst is in progress (the relatches
    // must be spaced across separate Render() calls).
    command_interface_->Tick();

    // Disable any voice the current command's driver code just armed in
    // infinite-loop mode before this block is rendered.
    SuppressPersistentAmbientLoopVoices();

    // Carry-forward: this block's budget closes the gap between the cumulative
    // target and what has executed so far.
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

void OutRunMachine::UpdateChannelStereoRouting() {
    // Maps each device's per-channel L/R state onto the LogicalChannel's Pan and
    // RoutingGain (same design as XBoardMachine, including PanOverridden).
    for (std::uint32_t ch = 0; ch < Ym2151DeviceAdapter::kChannelCount; ++ch) {
        const bool left = ym2151_->ChannelOutputsLeft(ch);
        const bool right = ym2151_->ChannelOutputsRight(ch);
        LogicalChannel& channel = channels_[ch];
        channel.RoutingGain = kYm2151RoutingGain * static_cast<float>((left ? 1 : 0) + (right ? 1 : 0));
        if (!channel.PanOverridden) {
            channel.Pan = (left == right) ? 0.0f : (left ? -1.0f : 1.0f);
        }
    }
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

void OutRunMachine::SyncSoundDevices(std::uint64_t z80CycleMark) {
    if (z80CycleMark <= sound_sync_cursor_) {
        return;
    }
    const std::uint64_t delta = z80CycleMark - sound_sync_cursor_;
    sound_sync_cursor_ = z80CycleMark;
    // YM2151 input clock == Z80 clock; SegaPCM master == 4x it.
    ym2151_->Render(delta, ym_channel_scratch_);
    segapcm_->Render(delta * kSegaPcmMasterClocksPerZ80Cycle, pcm_channel_scratch_);
}

void OutRunMachine::SendCommand(const MusicCommand& command) {
    SilenceAllSegaPcmVoices();
    ClearStaleDriverVoiceTrackingRam();
    command_interface_->Send(command);
}

void OutRunMachine::ClearStaleDriverVoiceTrackingRam() {
    // SilenceAllSegaPcmVoices() and SuppressPersistentAmbientLoopVoices() reset
    // SegaPCM chip state but not the sound Z80's work RAM (0xF800-0xFFFF), and
    // stale driver state there changes a later command's dispatch: switching from
    // some BGMs to "LAST WAVE" left SegaPCM voices re-triggering about once per
    // second, and "wave sound" kept replaying under any later track.
    //
    // Comparing RAM snapshots found a 16-entry x 32-byte per-voice tracking table
    // at 0xF820-0xFA1F (apparently the driver's shadow of what each SegaPCM voice
    // is doing) and a smaller 6-entry x 32-byte table at 0xFC00-0xFCBF; both are
    // cleared in full, along with a few isolated addresses found empirically.
    // 0xF800-0xF807 (the command mailbox/ring buffer) is deliberately excluded,
    // since clearing it would interfere with command delivery.
    //
    // Clearing to 0x00 is safe at every SendCommand() call: these addresses read
    // 0x00 on a clean boot, so it is a no-op unless a previous command left
    // something behind.
    //
    // Known gap: "Drift On -> LAST WAVE" still leaves a lingering one-shot voice
    // pair, and FM notes of the previous BGM keep sounding after a track switch.
    // Both come from still-running driver code (the music sequencer or a periodic
    // mechanism), not leftover values, so clearing RAM cannot fix them.
    for (std::uint16_t addr = 0xF820; addr <= 0xFA1F; ++addr) {
        memory_map_.Write8(addr, 0x00);
    }
    for (std::uint16_t addr = 0xFC00; addr <= 0xFCBF; ++addr) {
        memory_map_.Write8(addr, 0x00);
    }
    static const std::uint16_t kIsolatedStaleAddrs[] = {0xF808, 0xF811, 0xF813, 0xFA43, 0xFD01, 0xFD08,
                                                          0xFFF4, 0xFFF7, 0xFFF8, 0xFFFA, 0xFFFB, 0xFFFC, 0xFFFD};
    for (std::uint16_t addr : kIsolatedStaleAddrs) {
        memory_map_.Write8(addr, 0x00);
    }
}

void OutRunMachine::SilenceAllSegaPcmVoices() {
    // The real Out Run driver does not reset unrelated SegaPCM voices when a new
    // command starts a BGM: "ambience" commands such as "wave sound" (0xA4) or
    // "Drift On" (0x8A) occupy their own voices and stay audible under a BGM
    // started afterwards, as layers on the hardware. As a jukebox-style player,
    // selecting a new track should replace what was playing, so this Machine (not
    // the shared SegaPcmDeviceAdapter) forces every voice silent right before
    // injecting each command. Voices the new command needs are reprogrammed a few
    // Z80 cycles later anyway.
    //
    // Voice N's control register is at SegaPCM offset 0x86 + N*8 (bit 0 =
    // disable; power-on default 0xFF), written through the normal memory-mapped
    // path.
    constexpr std::uint16_t kSegaPcmVoiceCtrlBase = 0xF086;
    constexpr std::uint16_t kSegaPcmVoiceStride = 8;
    constexpr std::uint8_t kSegaPcmVoiceCtrlPowerOnValue = 0xFF;
    for (std::uint32_t voice = 0; voice < SegaPcmDeviceAdapter::kVoiceCount; ++voice) {
        memory_map_.Write8(static_cast<std::uint16_t>(kSegaPcmVoiceCtrlBase + voice * kSegaPcmVoiceStride),
                            kSegaPcmVoiceCtrlPowerOnValue);
    }
}

void OutRunMachine::SuppressPersistentAmbientLoopVoices() {
    // SilenceAllSegaPcmVoices() only clears state when SendCommand() is called; a
    // command's own driver code re-arms ambience voices moments later. Tracing
    // the SegaPCM voice registers for all 15 catalogued commands showed that
    // every one arms a few voices in hardware infinite-loop mode (ctrl bit 1,
    // "loop disable", left clear) within about 0.5s and never rewrites them: a
    // persistent "engine idle" ambience with no stopping point. All melodic and
    // voice-clip voices observed were one-shot (ctrl bit 1 set).
    //
    // So this suppresses by that general hardware property rather than by sample
    // addresses: any enabled voice with loop-disable clear is treated as ambience.
    // This would need revisiting if a title uses infinite-loop mode for a real note.
    //
    // Called once per Render() block, since the voices are armed once and never
    // rewritten.
    constexpr std::uint16_t kSegaPcmVoiceCtrlBase = 0xF086;
    constexpr std::uint16_t kSegaPcmVoiceStride = 8;
    constexpr std::uint8_t kSegaPcmVoiceDisableBit = 0x01;
    constexpr std::uint8_t kSegaPcmVoiceLoopDisableBit = 0x02;
    for (std::uint32_t voice = 0; voice < SegaPcmDeviceAdapter::kVoiceCount; ++voice) {
        const std::uint16_t ctrlAddress = static_cast<std::uint16_t>(kSegaPcmVoiceCtrlBase + voice * kSegaPcmVoiceStride);
        const std::uint8_t ctrl = memory_map_.Read8(ctrlAddress);
        const bool enabled = (ctrl & kSegaPcmVoiceDisableBit) == 0;
        const bool loopsForever = (ctrl & kSegaPcmVoiceLoopDisableBit) == 0;
        if (enabled && loopsForever) {
            memory_map_.Write8(ctrlAddress, static_cast<std::uint8_t>(ctrl | kSegaPcmVoiceDisableBit));
        }
    }
}

MameDerivedZ80Adapter::RegisterSnapshot OutRunMachine::Z80Snapshot() const {
    return z80_->Snapshot();
}

std::uint8_t OutRunMachine::ReadMemory(std::uint16_t address) const {
    return memory_map_.Read8(address);
}

Ym2151DeviceAdapter& OutRunMachine::Ym2151ForTest() {
    return *ym2151_;
}

SegaPcmDeviceAdapter& OutRunMachine::SegaPcmForTest() {
    return *segapcm_;
}
