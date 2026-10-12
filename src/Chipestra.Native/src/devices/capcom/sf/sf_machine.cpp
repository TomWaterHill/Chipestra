// license:BSD-3-Clause
// copyright-holders:Olivier Galibert
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Portions derived from MAME (src/mame/capcom/sf.cpp); see THIRD-PARTY-NOTICES.md.
#include "devices/capcom/sf/sf_machine.h"

#include <algorithm>
#include <string>
#include <utility>

SfMachine::SfMachine(std::vector<std::uint8_t> music_rom, std::vector<std::uint8_t> sample_image)
    : music_rom_(std::move(music_rom)),
      sample_image_(std::move(sample_image)),
      clock_converter_(kNativeRuntimeTickRateHz, kZ80ClockRate) {}

SfMachine::~SfMachine() = default;

void SfMachine::Initialize() {
    memory_map_.LoadMusicRom(music_rom_.data(), music_rom_.size());
    memory_map_.LoadSampleImage(sample_image_.data(), sample_image_.size());

    auto musicCpu = std::make_unique<MameDerivedZ80Adapter>();
    music_cpu_ = musicCpu.get();
    device_manager_.Add(std::move(musicCpu));
    auto samplesCpu = std::make_unique<MameDerivedZ80Adapter>();
    samples_cpu_ = samplesCpu.get();
    device_manager_.Add(std::move(samplesCpu));

    // The YM2151's IRQ goes to Z80 #1's INT line (level).
    auto ym2151 = std::make_unique<Ym2151DeviceAdapter>([this](bool asserted) {
        music_cpu_->SetInputLine(Z80InputLine::Irq, asserted ? Z80InputLineState::Assert : Z80InputLineState::Clear);
    });
    ym2151_ = ym2151.get();
    device_manager_.Add(std::move(ym2151));

    // Slave mode: Z80 #2 clocks every sample in by writing the chip.
    for (std::uint32_t i = 0; i < kMsm5205Count; ++i) {
        auto msm = std::make_unique<Msm5205DeviceAdapter>(nullptr, Msm5205DeviceAdapter::Mode::kSlave);
        msm_[i] = msm.get();
        device_manager_.Add(std::move(msm));
    }

    memory_map_.SetLatchRead([this]() { return latch_; });
    memory_map_.SetYm2151(
        [this]() {
            SyncYm2151(CurrentBlockCycles());
            return ym2151_->ReadStatus();
        },
        [this](std::uint8_t offset, std::uint8_t value) {
            SyncYm2151(CurrentBlockCycles());
            if (offset == 0) {
                ym2151_->WriteAddress(value);
            } else {
                ym2151_->WriteData(value);
            }
        });

    music_cpu_->SetMemoryCallbacks([this](std::uint16_t address) { return memory_map_.ReadMusic(address); },
                                   [this](std::uint16_t address, std::uint8_t value) {
                                       memory_map_.WriteMusic(address, value);
                                   });
    // Z80 #1 has no I/O port space.
    music_cpu_->SetIoCallbacks([](std::uint8_t) -> std::uint8_t { return 0x00; }, [](std::uint8_t, std::uint8_t) {});

    // Z80 #2 has no RAM: memory writes are dropped.
    samples_cpu_->SetMemoryCallbacks([this](std::uint16_t address) { return memory_map_.ReadSamples(address); },
                                     [](std::uint16_t, std::uint8_t) {});
    samples_cpu_->SetIoCallbacks(
        [this](std::uint8_t port) -> std::uint8_t { return port == 0x01 ? latch_ : std::uint8_t{0x00}; },
        [this](std::uint8_t port, std::uint8_t value) {
            if (port == 0x00 || port == 0x01) {
                Msm5205DeviceAdapter& msm = *msm_[port];
                // Bit 7 is the RESET pin, the low nibble is the data; the write also pulses VCLK.
                msm.SlaveWrite(std::max(SamplesCpuMsmClock(), msm.RenderedClocks()), value);
                ++msm_write_count_;
            } else if (port == 0x02) {
                memory_map_.SetSampleBank(value);
            }
        });

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
    for (std::uint32_t i = 0; i < kMsm5205Count; ++i) {
        LogicalChannel channel;
        channel.Id = kMsm5205Base + i;
        channel.DeviceName = "MSM5205-" + std::to_string(i + 1);
        channel.ChannelName = "ADPCM";
        channel.NativeSampleRate = Msm5205DeviceAdapter::kSlaveOutputSampleRateHz;
        channel.RoutingGain = 2.0f * kMsm5205RoutingGain;
        channels_.push_back(std::move(channel));
    }
    ym2151_channel_scratch_.assign(Ym2151DeviceAdapter::kChannelCount, {});

    ResetState();
    RunWarmup();
}

void SfMachine::ResetState() {
    memory_map_.ResetState();
    music_cpu_->Reset();
    samples_cpu_->Reset();
    ym2151_->Reset();
    for (Msm5205DeviceAdapter* msm : msm_) {
        msm->Reset();
    }
    latch_ = 0;
    pending_command_valid_ = false;
    pending_command_ = 0;
    command_sent_since_reset_ = false;
    cumulative_ticks_processed_ = 0;
    music_cycles_executed_ = 0;
    samples_cycles_executed_ = 0;
    frames_delivered_ = 0;
    samples_irq_count_ = 0;
    msm_write_count_ = 0;
    samples_exec_base_cycles_ = 0;
    sound_sync_cursor_ = 0;
    block_base_cycles_ = 0;
    channels_registered_ = false;
}

void SfMachine::RunWarmup() {
    static_assert(kBootWarmupTicks % kWarmupChunkTicks == 0, "warm-up must be a whole number of render chunks");
    for (std::uint64_t rendered = 0; rendered < kBootWarmupTicks; rendered += kWarmupChunkTicks) {
        Render(EmuDuration{kWarmupChunkTicks}, nullptr);
    }
}

void SfMachine::Reset() {
    // Back to the power-on state, RAM included.
    ++reset_count_;
    ResetState();
    RunWarmup();
}

void SfMachine::Shutdown() {}

std::vector<LogicalChannel>& SfMachine::LogicalChannels() { return channels_; }

std::uint64_t SfMachine::SamplesCpuMsmClock() const {
    return (samples_exec_base_cycles_ + samples_cpu_->CyclesConsumedThisCall()) * kMsm5205ClockHz / kZ80ClockRate.hz;
}

void SfMachine::SyncYm2151(std::uint64_t music_cpu_cycle_mark) {
    if (music_cpu_cycle_mark <= sound_sync_cursor_) {
        return;
    }
    const std::uint64_t delta = music_cpu_cycle_mark - sound_sync_cursor_;
    sound_sync_cursor_ = music_cpu_cycle_mark;
    // The YM2151 runs from the Z80 clock: one chip input clock per Z80 cycle.
    ym2151_->Render(delta, ym2151_channel_scratch_);
}

void SfMachine::RunSamplesCpuUpToCycles(std::uint64_t targetCycles) {
    for (;;) {
        const std::uint64_t nextIrq = (samples_irq_count_ + 1) * kZ80ClockRate.hz / kSamplesIrqHz;
        if (samples_cycles_executed_ >= nextIrq) {
            samples_cpu_->AssertIrqHoldLine();
            ++samples_irq_count_;
            continue;
        }
        if (samples_cycles_executed_ >= targetCycles) {
            break;
        }
        samples_exec_base_cycles_ = samples_cycles_executed_;
        samples_cpu_->Execute(std::min(nextIrq, targetCycles) - samples_cycles_executed_);
        samples_cycles_executed_ += samples_cpu_->CyclesConsumedThisCall();
    }
}

void SfMachine::RunCpusUpToTicks(std::uint64_t absoluteTicks) {
    const std::uint64_t targetCycles = clock_converter_.TicksToCycles(EmuDuration(absoluteTicks));
    if (targetCycles > music_cycles_executed_) {
        music_cpu_->Execute(targetCycles - music_cycles_executed_);
        const std::uint64_t consumed = music_cpu_->CyclesConsumedThisCall();
        music_cycles_executed_ += consumed;
        block_base_cycles_ += consumed;
    }
    RunSamplesCpuUpToCycles(targetCycles);
}

void SfMachine::DeliverLatchFrame() {
    latch_ = pending_command_valid_ ? pending_command_ : kIdleLatchValue;
    pending_command_valid_ = false;
    ++frames_delivered_;
    // The write strobe pulses Z80 #1's NMI (edge-triggered: assert then release at once).
    music_cpu_->SetInputLine(Z80InputLine::Nmi, Z80InputLineState::Assert);
    music_cpu_->SetInputLine(Z80InputLine::Nmi, Z80InputLineState::Clear);
}

void SfMachine::Render(EmuDuration duration, LogicalChannelSink* sink) {
    if (sink != nullptr && !channels_registered_) {
        for (const LogicalChannel& channel : channels_) {
            if (!sink->HasChannel(channel.Id)) sink->RegisterChannel(channel.Id);  // a Reset() re-registers into a reused sink
        }
        channels_registered_ = true;
    }

    for (std::vector<float>& buffer : ym2151_channel_scratch_) buffer.clear();
    for (std::vector<float>& buffer : msm_channel_scratch_) buffer.clear();
    sound_sync_cursor_ = 0;
    block_base_cycles_ = 0;

    const std::uint64_t end = cumulative_ticks_processed_ + duration.ticks;
    while (cumulative_ticks_processed_ < end) {
        const std::uint64_t nextFrame = (frames_delivered_ + 1) * kFramePeriodTicks;
        const std::uint64_t stop = std::min(end, nextFrame);
        RunCpusUpToTicks(stop);
        cumulative_ticks_processed_ = stop;
        if (stop == nextFrame) {
            DeliverLatchFrame();
        }
    }

    // Final catch-up: bring the YM2151 and the MSM5205s to the end of the block.
    SyncYm2151(block_base_cycles_);
    const std::uint64_t msmTarget = cumulative_ticks_processed_ * kMsm5205InputClocksPerTick;
    for (std::uint32_t i = 0; i < kMsm5205Count; ++i) {
        const std::uint64_t rendered = msm_[i]->RenderedClocks();
        if (msmTarget > rendered) {
            msm_[i]->Render(msmTarget - rendered, msm_channel_scratch_[i]);
        }
    }
    UpdateChannelStereoRouting();

    if (sink != nullptr) {
        for (std::uint32_t ch = 0; ch < Ym2151DeviceAdapter::kChannelCount; ++ch) {
            sink->WriteSamples(channels_[kYm2151Base + ch].Id, ym2151_channel_scratch_[ch].data(),
                               ym2151_channel_scratch_[ch].size());
        }
        for (std::uint32_t i = 0; i < kMsm5205Count; ++i) {
            sink->WriteSamples(channels_[kMsm5205Base + i].Id, msm_channel_scratch_[i].data(),
                               msm_channel_scratch_[i].size());
        }
    }
}

void SfMachine::UpdateChannelStereoRouting() {
    // YM2151: the left/right output enables of each channel pick the speaker (each at the chip's route gain).
    for (std::uint32_t ch = 0; ch < Ym2151DeviceAdapter::kChannelCount; ++ch) {
        const bool left = ym2151_->ChannelOutputsLeft(ch);
        const bool right = ym2151_->ChannelOutputsRight(ch);
        LogicalChannel& channel = channels_[kYm2151Base + ch];
        channel.RoutingGain = kYm2151RoutingGain * static_cast<float>((left ? 1 : 0) + (right ? 1 : 0));
        if (!channel.PanOverridden) {
            channel.Pan = (left == right) ? 0.0f : (left ? -1.0f : 1.0f);
        }
    }
    // MSM5205: mono into both speakers.
    for (std::uint32_t i = 0; i < kMsm5205Count; ++i) {
        LogicalChannel& channel = channels_[kMsm5205Base + i];
        channel.RoutingGain = 2.0f * kMsm5205RoutingGain;
        if (!channel.PanOverridden) {
            channel.Pan = 0.0f;
        }
    }
}

void SfMachine::SendCommand(const MusicCommand& command) {
    // This driver has no command that silences both chips (0x00 does nothing, 0x3f stops the YM2151 only and
    // 0x80 the MSM5205s only), so a stop - and any new command after an earlier one - starts from a Reset(),
    // which silences everything at once without rendering. A stop is the command 0x00 (the Music Stop
    // track): reset only. The very first command after Initialize()/Reset() needs no reset. Channel
    // Mute/Gain/Pan live in channels_, which Reset() does not touch.
    if (command.Value == 0x00) {
        Reset();
        return;
    }
    if (command_sent_since_reset_) {
        Reset();
    }
    // The command takes the place of the idle value in the next frame's latch write.
    pending_command_ = command.Value;
    pending_command_valid_ = true;
    command_sent_since_reset_ = true;
}

bool SfMachine::AcceptsCommand(const MusicCommand& command) const { return command.Kind != MusicCommandKind::Wsg; }
