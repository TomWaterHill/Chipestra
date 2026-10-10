// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "darius_machine.h"

#include "address_space.h"

#include <cmath>
#include <string>
#include <utility>

// Dispatches the scattered addresses within DariusSoundMemoryMap::kIoStart/kIoSize
// (0x9000-0xDC00). Offsets are relative to kIoStart.
class DariusMachine::IoBus final : public AddressSpaceEndpoint {
public:
    IoBus(Ym2203DeviceAdapter& ym1, Ym2203DeviceAdapter& ym2, DariusCommandInterface& ciu, DariusMachine& owner)
        : ym1_(ym1), ym2_(ym2), ciu_(ciu), owner_(owner) {}

    std::uint8_t Read8(std::uint64_t offset) const override {
        switch (offset) {
            case kYm1StatusOffset:
                owner_.SyncSoundDevices(owner_.CurrentBlockCycles());
                return ym1_.ReadStatus();
            case kYm1DataOffset:
                owner_.SyncSoundDevices(owner_.CurrentBlockCycles());
                return ym1_.ReadData();
            case kYm2StatusOffset:
                owner_.SyncSoundDevices(owner_.CurrentBlockCycles());
                return ym2_.ReadStatus();
            case kYm2DataOffset:
                owner_.SyncSoundDevices(owner_.CurrentBlockCycles());
                return ym2_.ReadData();
            case kCiuSlaveCommOffset:
                return ciu_.SlaveCommR();
            default:
                // 0xB000 (slave_port_w) is a no-op read, as is every other
                // uncovered address in this window.
                return 0;
        }
    }

    void Write8(std::uint64_t offset, std::uint8_t value) override {
        switch (offset) {
            case kYm1StatusOffset:
                owner_.SyncSoundDevices(owner_.CurrentBlockCycles());
                ym1_.WriteAddress(value);
                break;
            case kYm1DataOffset:
                owner_.SyncSoundDevices(owner_.CurrentBlockCycles());
                ym1_.WriteData(value);
                break;
            case kYm2StatusOffset:
                owner_.SyncSoundDevices(owner_.CurrentBlockCycles());
                ym2_.WriteAddress(value);
                break;
            case kYm2DataOffset:
                owner_.SyncSoundDevices(owner_.CurrentBlockCycles());
                ym2_.WriteData(value);
                break;
            case kCiuSlavePortOffset:
                ciu_.SlavePortW(value);
                break;
            case kCiuSlaveCommOffset:
                ciu_.SlaveCommW(value);
                break;
            case kFm0PanOffset:
                owner_.fm0_pan_ = value;
                owner_.ApplyFm0();
                break;
            case kFm1PanOffset:
                owner_.fm1_pan_ = value;
                owner_.ApplyFm1();
                break;
            case kPsg0PanOffset:
                owner_.psg0_pan_ = value;
                owner_.ApplyPsg0(0);
                owner_.ApplyPsg0(1);
                owner_.ApplyPsg0(2);
                break;
            case kPsg1PanOffset:
                owner_.psg1_pan_ = value;
                owner_.ApplyPsg1(0);
                owner_.ApplyPsg1(1);
                owner_.ApplyPsg1(2);
                break;
            case kDaPanOffset:
                owner_.da_pan_ = value;
                owner_.ApplyDa();
                break;
            case kAdpcmCommandOffset:
                owner_.adpcm_command_ = value;
                break;
            case kBankswitchOffset:
                owner_.sound_memory_map_.SetBank(value & 0x03);
                break;
            default:
                // Addresses in the window that are not wired to anything are
                // accepted and ignored.
                break;
        }
    }

private:
    // All offsets are address - DariusSoundMemoryMap::kIoStart (0x9000).
    static constexpr std::uint64_t kYm1StatusOffset = 0x0000;  // 0x9000
    static constexpr std::uint64_t kYm1DataOffset = 0x0001;    // 0x9001
    static constexpr std::uint64_t kYm2StatusOffset = 0x1000;  // 0xA000
    static constexpr std::uint64_t kYm2DataOffset = 0x1001;    // 0xA001
    static constexpr std::uint64_t kCiuSlavePortOffset = 0x2000;  // 0xB000
    static constexpr std::uint64_t kCiuSlaveCommOffset = 0x2001;  // 0xB001
    static constexpr std::uint64_t kFm0PanOffset = 0x3000;     // 0xC000
    static constexpr std::uint64_t kFm1PanOffset = 0x3400;     // 0xC400
    static constexpr std::uint64_t kPsg0PanOffset = 0x3800;    // 0xC800
    static constexpr std::uint64_t kPsg1PanOffset = 0x3C00;    // 0xCC00
    static constexpr std::uint64_t kDaPanOffset = 0x4000;      // 0xD000
    static constexpr std::uint64_t kAdpcmCommandOffset = 0x4400;  // 0xD400
    static constexpr std::uint64_t kBankswitchOffset = 0x4C00;    // 0xDC00

    Ym2203DeviceAdapter& ym1_;
    Ym2203DeviceAdapter& ym2_;
    DariusCommandInterface& ciu_;
    DariusMachine& owner_;
};

DariusMachine::DariusMachine(std::vector<std::uint8_t> audiocpu_rom, std::vector<std::uint8_t> adpcm_rom)
    : audiocpu_rom_(std::move(audiocpu_rom)),
      adpcm_rom_(std::move(adpcm_rom)),
      clock_converter_(kNativeRuntimeTickRateHz, kAudioClockRate) {}

// Out-of-line: io_bus_ (unique_ptr to a forward-declared nested type) needs
// its complete type, which only exists in this translation unit.
DariusMachine::~DariusMachine() = default;

void DariusMachine::Initialize() {
    sound_memory_map_.LoadRom(audiocpu_rom_.data(), audiocpu_rom_.size());
    adpcm_memory_map_.LoadRom(adpcm_rom_.data(), adpcm_rom_.size());

    auto audiocpu = std::make_unique<MameDerivedZ80Adapter>();
    audiocpu_ = audiocpu.get();
    device_manager_.Add(std::move(audiocpu));

    auto adpcmZ80 = std::make_unique<MameDerivedZ80Adapter>();
    adpcm_z80_ = adpcmZ80.get();
    device_manager_.Add(std::move(adpcmZ80));

    // ym1's IRQ -> audiocpu's maskable INT line (plain direct forward).
    auto ym1 = std::make_unique<Ym2203DeviceAdapter>(
        [this](bool asserted) {
            audiocpu_->SetInputLine(Z80InputLine::Irq, asserted ? Z80InputLineState::Assert : Z80InputLineState::Clear);
        },
        [this](std::uint8_t data) {
            // write_portA0: the HIGH nibble is PSG0 port A's volume, the LOW
            // nibble is FM0's volume.
            fm0_vol_ = static_cast<std::uint8_t>(data & 0x0F);
            psg0_vol_[0] = static_cast<std::uint8_t>((data >> 4) & 0x0F);
            ApplyFm0();
            ApplyPsg0(0);
        },
        [this](std::uint8_t data) {
            psg0_vol_[1] = static_cast<std::uint8_t>((data >> 4) & 0x0F);
            psg0_vol_[2] = static_cast<std::uint8_t>(data & 0x0F);
            ApplyPsg0(1);
            ApplyPsg0(2);
        });
    ym1_ = ym1.get();
    device_manager_.Add(std::move(ym1));

    // ym2's IRQ is not wired on the real board.
    auto ym2 = std::make_unique<Ym2203DeviceAdapter>(
        /*irq_handler=*/nullptr,
        [this](std::uint8_t data) {
            // write_portA1: same nibble layout as write_portA0 (FM1 = LOW,
            // PSG1 port A = HIGH).
            fm1_vol_ = static_cast<std::uint8_t>(data & 0x0F);
            psg1_vol_[0] = static_cast<std::uint8_t>((data >> 4) & 0x0F);
            ApplyFm1();
            ApplyPsg1(0);
        },
        [this](std::uint8_t data) {
            psg1_vol_[1] = static_cast<std::uint8_t>((data >> 4) & 0x0F);
            psg1_vol_[2] = static_cast<std::uint8_t>(data & 0x0F);
            ApplyPsg1(1);
            ApplyPsg1(2);
        });
    ym2_ = ym2.get();
    device_manager_.Add(std::move(ym2));

    // MSM5205's VCK fires once per output sample (8000/sec), before that
    // sample's nibble is decoded; it pulses the adpcm Z80's NMI line if that Z80
    // has enabled NMI (via its I/O ports 0x00/0x01). The handler is invoked only
    // from Render(), after everything is constructed.
    auto msm5205 = std::make_unique<Msm5205DeviceAdapter>([this]() {
        if (adpcm_nmi_enable_) {
            adpcm_z80_->SetInputLine(Z80InputLine::Nmi, Z80InputLineState::Assert);
            adpcm_z80_->SetInputLine(Z80InputLine::Nmi, Z80InputLineState::Clear);
        }
    });
    msm5205_ = msm5205.get();
    device_manager_.Add(std::move(msm5205));

    command_interface_ = std::make_unique<DariusCommandInterface>(
        [this](bool asserted) {
            audiocpu_->SetInputLine(Z80InputLine::Nmi, asserted ? Z80InputLineState::Assert : Z80InputLineState::Clear);
        });

    io_bus_ = std::make_unique<IoBus>(*ym1_, *ym2_, *command_interface_, *this);
    sound_memory_map_.MapDevice(DariusSoundMemoryMap::kIoStart, DariusSoundMemoryMap::kIoSize, io_bus_.get());

    audiocpu_->SetMemoryCallbacks(
        [this](std::uint16_t addr) { return sound_memory_map_.Read8(addr); },
        [this](std::uint16_t addr, std::uint8_t value) { sound_memory_map_.Write8(addr, value); });
    // audiocpu has no separate I/O port space (AS_PROGRAM only).
    audiocpu_->SetIoCallbacks([](std::uint8_t) -> std::uint8_t { return 0xFF; }, [](std::uint8_t, std::uint8_t) {});

    adpcm_z80_->SetMemoryCallbacks(
        [this](std::uint16_t addr) { return adpcm_memory_map_.Read8(addr); },
        [this](std::uint16_t addr, std::uint8_t value) { adpcm_memory_map_.Write8(addr, value); });
    // adpcm Z80's I/O ports, handled directly rather than through an
    // AddressSpace. Reads of ports 0x02/0x03 are modeled stubs returning 0;
    // genuinely unmapped ports read 0xFF.
    adpcm_z80_->SetIoCallbacks(
        [this](std::uint8_t port) -> std::uint8_t {
            switch (port) {
                case 0x00:
                    return adpcm_command_;
                case 0x02:
                case 0x03:
                    return 0;
                default:
                    return 0xFF;
            }
        },
        [this](std::uint8_t port, std::uint8_t value) {
            switch (port) {
                case 0x00:
                    adpcm_nmi_enable_ = false;
                    break;
                case 0x01:
                    adpcm_nmi_enable_ = true;
                    break;
                case 0x02:
                    // adpcm_data_w: low nibble -> MSM5205 data_w, bit 5 (inverted)
                    // -> reset_w (the original driver's own "best guess" for bit 5).
                    msm5205_->DataW(static_cast<std::uint8_t>(value & 0x0F));
                    msm5205_->ResetPin((value & 0x20) == 0);
                    break;
                default:
                    break;
            }
        });

    channels_.clear();
    for (std::uint32_t ch = 0; ch < kFmChannelsPerChip; ++ch) {
        LogicalChannel channel;
        channel.Id = kFm0Base + ch;
        channel.DeviceName = "YM2203";
        channel.ChannelName = "YM1 FM" + std::to_string(ch + 1);
        channel.NativeSampleRate = Ym2203DeviceAdapter::kOutputSampleRateHz;
        channels_.push_back(std::move(channel));
    }
    for (std::uint32_t ch = 0; ch < kSsgChannelsPerChip; ++ch) {
        LogicalChannel channel;
        channel.Id = kPsg0Base + ch;
        channel.DeviceName = "YM2203";
        channel.ChannelName = "YM1 PSG" + std::to_string(ch + 1);
        channel.NativeSampleRate = Ym2203DeviceAdapter::kOutputSampleRateHz;
        channels_.push_back(std::move(channel));
    }
    for (std::uint32_t ch = 0; ch < kFmChannelsPerChip; ++ch) {
        LogicalChannel channel;
        channel.Id = kFm1Base + ch;
        channel.DeviceName = "YM2203";
        channel.ChannelName = "YM2 FM" + std::to_string(ch + 1);
        channel.NativeSampleRate = Ym2203DeviceAdapter::kOutputSampleRateHz;
        channels_.push_back(std::move(channel));
    }
    for (std::uint32_t ch = 0; ch < kSsgChannelsPerChip; ++ch) {
        LogicalChannel channel;
        channel.Id = kPsg1Base + ch;
        channel.DeviceName = "YM2203";
        channel.ChannelName = "YM2 PSG" + std::to_string(ch + 1);
        channel.NativeSampleRate = Ym2203DeviceAdapter::kOutputSampleRateHz;
        channels_.push_back(std::move(channel));
    }
    {
        LogicalChannel channel;
        channel.Id = kMsm5205ChannelId;
        channel.DeviceName = "MSM5205";
        channel.ChannelName = "ADPCM";
        channel.NativeSampleRate = Msm5205DeviceAdapter::kOutputSampleRateHz;
        channels_.push_back(std::move(channel));
    }
    channels_registered_ = false;
    ym1_channel_scratch_.assign(Ym2203DeviceAdapter::kChannelCount, {});
    ym2_channel_scratch_.assign(Ym2203DeviceAdapter::kChannelCount, {});
    msm5205_channel_scratch_.clear();

    // Push the power-on pan/vol state (all-zero registers) into the new
    // channels_ through the same Apply* functions the I/O write paths use.
    ApplyFm0();
    ApplyFm1();
    ApplyPsg0(0);
    ApplyPsg0(1);
    ApplyPsg0(2);
    ApplyPsg1(0);
    ApplyPsg1(1);
    ApplyPsg1(2);
    ApplyDa();

    audiocpu_->Reset();
    adpcm_z80_->Reset();
    ym1_->Reset();
    ym2_->Reset();
    msm5205_->Reset();
    command_interface_->Reset();
    sound_memory_map_.SetBank(0);
    adpcm_command_ = 0;
    adpcm_nmi_enable_ = false;
    sound_sync_cursor_ = 0;
    block_base_cycles_ = 0;
    cumulative_ticks_processed_ = 0;
    cumulative_cycles_executed_ = 0;
    adpcm_cumulative_cycles_executed_ = 0;
    msm_samples_delivered_ = 0;

    RunBootWarmup();
    SendMasterBootHandshake();
}

void DariusMachine::Reset() {
    song_active_ = false;  // the next command is the first since this Reset(): no STOP needed
    audiocpu_->Reset();
    adpcm_z80_->Reset();
    ym1_->Reset();
    ym2_->Reset();
    msm5205_->Reset();
    command_interface_->Reset();
    sound_memory_map_.SetBank(0);
    adpcm_command_ = 0;
    adpcm_nmi_enable_ = false;
    channels_registered_ = false;
    sound_sync_cursor_ = 0;
    block_base_cycles_ = 0;
    // Restart the carry-forward baseline from zero, as in Initialize().
    cumulative_ticks_processed_ = 0;
    cumulative_cycles_executed_ = 0;
    adpcm_cumulative_cycles_executed_ = 0;
    msm_samples_delivered_ = 0;

    RunBootWarmup();
    SendMasterBootHandshake();
}

void DariusMachine::Shutdown() {
    // device_manager_ releases the devices; io_bus_/command_interface_ are
    // declared after it and so are destroyed first.
}

std::vector<LogicalChannel>& DariusMachine::LogicalChannels() { return channels_; }

void DariusMachine::Render(EmuDuration duration, LogicalChannelSink* sink) {
    if (sink != nullptr && !channels_registered_) {
        for (const LogicalChannel& channel : channels_) {
            sink->RegisterChannel(channel.Id);
        }
        channels_registered_ = true;
    }

    for (std::vector<float>& buffer : ym1_channel_scratch_) buffer.clear();
    for (std::vector<float>& buffer : ym2_channel_scratch_) buffer.clear();
    msm5205_channel_scratch_.clear();

    cumulative_ticks_processed_ += duration.ticks;
    sound_sync_cursor_ = 0;
    block_base_cycles_ = 0;

    // MSM5205/adpcm-Z80 NMI: for every MSM5205 sample due since the last Render()
    // call, first run BOTH Z80s up to that sample's canonical-tick boundary (so
    // the adpcm Z80 can service the NMI the previous sample's VCK asserted), then
    // decode one MSM5205 sample (which fires VCK for the next iteration).
    const std::uint64_t targetMsmSamples = cumulative_ticks_processed_ / kMsm5205SamplePeriodCanonicalTicks;
    while (msm_samples_delivered_ < targetMsmSamples) {
        ++msm_samples_delivered_;
        const std::uint64_t boundaryTicks = msm_samples_delivered_ * kMsm5205SamplePeriodCanonicalTicks;
        RunCpusUpToTicks(boundaryTicks);
        msm5205_->Render(Msm5205DeviceAdapter::kInputClocksPerSample, msm5205_channel_scratch_);
    }

    // Flush whatever cycle budget remains beyond the last delivered MSM5205
    // sample boundary (up to the full block).
    RunCpusUpToTicks(cumulative_ticks_processed_);

    // Final catch-up: advance ym1_/ym2_ to the end of the block.
    SyncSoundDevices(block_base_cycles_);

    if (sink != nullptr) {
        for (std::uint32_t ch = 0; ch < kFmChannelsPerChip; ++ch) {
            sink->WriteSamples(channels_[kFm0Base + ch].Id, ym1_channel_scratch_[ch].data(), ym1_channel_scratch_[ch].size());
        }
        for (std::uint32_t ch = 0; ch < kSsgChannelsPerChip; ++ch) {
            sink->WriteSamples(channels_[kPsg0Base + ch].Id, ym1_channel_scratch_[kFmChannelsPerChip + ch].data(),
                                ym1_channel_scratch_[kFmChannelsPerChip + ch].size());
        }
        for (std::uint32_t ch = 0; ch < kFmChannelsPerChip; ++ch) {
            sink->WriteSamples(channels_[kFm1Base + ch].Id, ym2_channel_scratch_[ch].data(), ym2_channel_scratch_[ch].size());
        }
        for (std::uint32_t ch = 0; ch < kSsgChannelsPerChip; ++ch) {
            sink->WriteSamples(channels_[kPsg1Base + ch].Id, ym2_channel_scratch_[kFmChannelsPerChip + ch].data(),
                                ym2_channel_scratch_[kFmChannelsPerChip + ch].size());
        }
        sink->WriteSamples(channels_[kMsm5205ChannelId].Id, msm5205_channel_scratch_.data(), msm5205_channel_scratch_.size());
    }
}

void DariusMachine::RunCpusUpToTicks(std::uint64_t absoluteCanonicalTicks) {
    const std::uint64_t targetCycles = clock_converter_.TicksToCycles(EmuDuration(absoluteCanonicalTicks));

    if (targetCycles > cumulative_cycles_executed_) {
        const std::uint64_t delta = targetCycles - cumulative_cycles_executed_;
        audiocpu_->Execute(delta);
        const std::uint64_t consumed = audiocpu_->CyclesConsumedThisCall();
        cumulative_cycles_executed_ += consumed;
        block_base_cycles_ += consumed;
    }

    if (targetCycles > adpcm_cumulative_cycles_executed_) {
        const std::uint64_t delta = targetCycles - adpcm_cumulative_cycles_executed_;
        adpcm_z80_->Execute(delta);
        adpcm_cumulative_cycles_executed_ += adpcm_z80_->CyclesConsumedThisCall();
    }
}

void DariusMachine::SyncSoundDevices(std::uint64_t audioCpuCycleMark) {
    if (audioCpuCycleMark <= sound_sync_cursor_) {
        return;
    }
    const std::uint64_t delta = audioCpuCycleMark - sound_sync_cursor_;
    sound_sync_cursor_ = audioCpuCycleMark;
    // ym1/ym2 input clock == audiocpu clock (4,000,000Hz) - no scaling.
    ym1_->Render(delta, ym1_channel_scratch_);
    ym2_->Render(delta, ym2_channel_scratch_);
}

void DariusMachine::RunBootWarmup() {
    static_assert(kBootWarmupFrames % kWarmupChunkFrames == 0,
                  "kBootWarmupFrames must be an exact multiple of kWarmupChunkFrames");
    for (std::uint64_t rendered = 0; rendered < kBootWarmupFrames; rendered += kWarmupChunkFrames) {
        Render(EmuDuration{kWarmupChunkFrames}, nullptr);
    }
}

void DariusMachine::SendMasterBootHandshake() {
    static_assert(kMasterHandshakeGapFrames % 256 == 0,
                  "kMasterHandshakeGapFrames must be an exact multiple of the warmup chunk");
    // A full render gap follows the send: the TC0140SYT/PC060HA mailbox is a
    // single two-nibble register, not a queue.
    command_interface_->Send(0xEF);
    for (std::uint64_t rendered = 0; rendered < kMasterHandshakeGapFrames; rendered += 256) {
        Render(EmuDuration{256}, nullptr);
    }
}

void DariusMachine::SendCommand(const MusicCommand& command) {
    // Send() drives the MASTER side (the 68000 is not emulated) through the
    // mode-0/mode-1 two-nibble sequence; audiocpu picks it up via its ROM code at
    // 0xB000/0xB001.
    //
    // The driver does not silence what is already playing when a new command
    // arrives (a looping SE such as 0x28 would keep sounding under the new
    // track), but its STOP (0x00) silences BGM and SE alike. So when a song is
    // already running, pass STOP first and let audiocpu drain it (a full
    // single-mailbox gap, rendered silently) before the requested command. The
    // first command after boot/Reset() needs no STOP. Called by the Player only
    // while the pump is stopped.
    if (song_active_ && command.Value != 0x00) {
        command_interface_->Send(0x00);
        for (std::uint64_t rendered = 0; rendered < kSwitchStopGapFrames; rendered += kWarmupChunkFrames) {
            Render(EmuDuration{kWarmupChunkFrames}, nullptr);
        }
    }
    command_interface_->Send(command.Value);
    // Any command counts, STOP included: the Player does not render after a
    // type="control" track, so that 0x00 may still be unprocessed when the next
    // command arrives, and the next switch must insert its own STOP + gap. Only
    // Reset() clears this.
    song_active_ = true;
}

void DariusMachine::OnPanOverrideCleared(std::uint32_t) {
    ApplyFm0();
    ApplyFm1();
    ApplyPsg0(0);
    ApplyPsg0(1);
    ApplyPsg0(2);
    ApplyPsg1(0);
    ApplyPsg1(1);
    ApplyPsg1(2);
    ApplyDa();
}

float DariusMachine::DefVol(std::uint8_t index4bit) {
    // m_def_vol[i] = 100 / 10^((32 - i*(32/15)) / 20): a 4-bit index -> a 0..100
    // logarithmic gain lookup table.
    const float i = static_cast<float>(index4bit & 0x0F);
    return 100.0f / std::pow(10.0f, (32.0f - i * (32.0f / 15.0f)) / 20.0f);
}

// The real driver's fm0_pan_w comment: "data 0x00:right 0xff:left". The linear
// map below sends 0x00 -> +1.0 (right) and 0xff -> -1.0 (left). PanOverridden is
// honored (a UI-set Pan is not clobbered by the automatic sync); RoutingGain is
// always updated (amplitude, not stereo position).
namespace {
float DariusPanFromByte(std::uint8_t panByte) { return 1.0f - 2.0f * (static_cast<float>(panByte) / 255.0f); }
}  // namespace

void DariusMachine::ApplyFm0() {
    const float pan = DariusPanFromByte(fm0_pan_);
    const float gain = DefVol(fm0_vol_) / 100.0f * kFmHwRoutingGain * kMasterCalibrationGain;
    for (std::uint32_t ch = 0; ch < kFmChannelsPerChip; ++ch) {
        LogicalChannel& channel = channels_[kFm0Base + ch];
        if (!channel.PanOverridden) channel.Pan = pan;
        channel.RoutingGain = gain;
    }
}

void DariusMachine::ApplyFm1() {
    const float pan = DariusPanFromByte(fm1_pan_);
    const float gain = DefVol(fm1_vol_) / 100.0f * kFmHwRoutingGain * kMasterCalibrationGain;
    for (std::uint32_t ch = 0; ch < kFmChannelsPerChip; ++ch) {
        LogicalChannel& channel = channels_[kFm1Base + ch];
        if (!channel.PanOverridden) channel.Pan = pan;
        channel.RoutingGain = gain;
    }
}

void DariusMachine::ApplyPsg0(int port) {
    const float pan = DariusPanFromByte(psg0_pan_);
    const float gain = DefVol(psg0_vol_[port]) / 100.0f * kSsgHwRoutingGain * kMasterCalibrationGain;
    LogicalChannel& channel = channels_[kPsg0Base + static_cast<std::uint32_t>(port)];
    if (!channel.PanOverridden) channel.Pan = pan;
    channel.RoutingGain = gain;
}

void DariusMachine::ApplyPsg1(int port) {
    const float pan = DariusPanFromByte(psg1_pan_);
    const float gain = DefVol(psg1_vol_[port]) / 100.0f * kSsgHwRoutingGain * kMasterCalibrationGain;
    LogicalChannel& channel = channels_[kPsg1Base + static_cast<std::uint32_t>(port)];
    if (!channel.PanOverridden) channel.Pan = pan;
    channel.RoutingGain = gain;
}

void DariusMachine::ApplyDa() {
    // da_pan_w drives MSM5205's L/R through two independent gains (left =
    // def_vol[pan&0xf], right = def_vol[(pan>>4)&0xf]). LogicalChannel has one
    // Pan+RoutingGain pair (L = RoutingGain*(1-Pan)/2, R = RoutingGain*(1+Pan)/2),
    // so solve for the equivalent: RoutingGain = (left+right)/100,
    // Pan = (right-left)/(left+right) (0 when both are 0).
    const float left = DefVol(static_cast<std::uint8_t>(da_pan_ & 0x0F));
    const float right = DefVol(static_cast<std::uint8_t>((da_pan_ >> 4) & 0x0F));
    LogicalChannel& channel = channels_[kMsm5205ChannelId];
    const float sum = left + right;
    channel.RoutingGain = sum / 100.0f;
    if (!channel.PanOverridden) {
        channel.Pan = sum > 0.0f ? (right - left) / sum : 0.0f;
    }
}
