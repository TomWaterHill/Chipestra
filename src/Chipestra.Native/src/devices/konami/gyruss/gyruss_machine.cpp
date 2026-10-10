// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "gyruss_machine.h"

#include "address_space.h"

#include <algorithm>
#include <string>
#include <utility>

// Dispatches the sound Z80's scattered addresses: the soundlatch read (0x8000
// on the program space) and, on the separate Z80 I/O space, ay1-5 register
// access, i8039_irq_w and the soundlatch2 write. Program space and I/O space
// are different buses on the hardware and are wired through separate callback
// pairs (SetMemoryCallbacks / SetIoCallbacks) in Initialize().
class GyrussMachine::IoBus final : public AddressSpaceEndpoint {
public:
    explicit IoBus(GyrussMachine& owner) : owner_(owner) {}

    std::uint8_t Read8(std::uint64_t offset) const override {
        if (offset == 0) {
            // The only address this endpoint services on the program space.
            return owner_.command_interface_->ReadLatch();
        }
        return 0xFF;
    }
    void Write8(std::uint64_t, std::uint8_t) override {}  // soundlatch read is program-space read-only

private:
    GyrussMachine& owner_;
};

GyrussMachine::GyrussMachine(std::vector<std::uint8_t> z80_rom, std::vector<std::uint8_t> i8039_rom)
    : z80_rom_(std::move(z80_rom)),
      i8039_rom_(std::move(i8039_rom)),
      clock_converter_(kNativeRuntimeTickRateHz, kZ80ClockRate),
      i8039_clock_converter_(kNativeRuntimeTickRateHz, kI8039ClockRate) {}

GyrussMachine::~GyrussMachine() = default;

void GyrussMachine::Initialize() {
    sound_memory_map_.LoadRom(z80_rom_.data(), z80_rom_.size());

    auto z80 = std::make_unique<MameDerivedZ80Adapter>();
    z80_ = z80.get();
    device_manager_.Add(std::move(z80));

    auto i8039 = std::make_unique<MameDerivedI8039Adapter>();
    i8039_ = i8039.get();
    device_manager_.Add(std::move(i8039));

    for (std::size_t i = 0; i < ay_.size(); ++i) {
        std::unique_ptr<Ay8910DeviceAdapter> ay;
        if (i == 2) {
            // ay3's Port A read: a free-running hardware counter (LS90
            // divide-by-10 outputs) that the sound driver's boot code polls
            // before it ever reaches command dispatch.
            static constexpr std::uint8_t kTimerTable[10] = {0x00, 0x01, 0x02, 0x03, 0x04,
                                                              0x09, 0x0A, 0x0B, 0x0A, 0x0D};
            ay = std::make_unique<Ay8910DeviceAdapter>(
                nullptr, nullptr,
                [this]() -> std::uint8_t {
                    const std::uint64_t totalCycles = cumulative_cycles_executed_ + z80_->CyclesConsumedThisCall();
                    return kTimerTable[(totalCycles / 1024) % 10];
                },
                nullptr, kAy8910LoadResistanceOhms);
        } else {
            Ay8910DeviceAdapter::PortWriteFn portB;
            if (i < 2) {
                // ay1/ay2 Port B selects their channels' RC filter capacitors.
                const int chip = static_cast<int>(i);
                portB = [this, chip](std::uint8_t value) { OnAyPortBWrite(chip, value); };
            }
            ay = std::make_unique<Ay8910DeviceAdapter>(nullptr, std::move(portB), nullptr, nullptr, kAy8910LoadResistanceOhms);
        }
        ay_[i] = ay.get();
        device_manager_.Add(std::move(ay));
    }

    command_interface_ = std::make_unique<GyrussCommandInterface>(*z80_);

    io_bus_ = std::make_unique<IoBus>(*this);
    sound_memory_map_.MapDevice(GyrussSoundMemoryMap::kSoundLatchAddress, 1, io_bus_.get());

    z80_->SetMemoryCallbacks(
        [this](std::uint16_t addr) { return sound_memory_map_.Read8(addr); },
        [this](std::uint16_t addr, std::uint8_t value) { sound_memory_map_.Write8(addr, value); });

    // Z80 I/O map: ay1-5 register access (0x00-0x12), i8039_irq_w (0x14),
    // soundlatch2 write (0x18). Everything else reads 0xFF and ignores writes.
    z80_->SetIoCallbacks(
        [this](std::uint8_t port) -> std::uint8_t {
            switch (port) {
                case 0x01:
                    SyncSoundDevices(CurrentBlockCycles());
                    return ay_[0]->ReadData();
                case 0x05:
                    SyncSoundDevices(CurrentBlockCycles());
                    return ay_[1]->ReadData();
                case 0x09:
                    SyncSoundDevices(CurrentBlockCycles());
                    return ay_[2]->ReadData();
                case 0x0D:
                    SyncSoundDevices(CurrentBlockCycles());
                    return ay_[3]->ReadData();
                case 0x11:
                    SyncSoundDevices(CurrentBlockCycles());
                    return ay_[4]->ReadData();
                default:
                    return 0xFF;
            }
        },
        [this](std::uint8_t port, std::uint8_t value) {
            switch (port) {
                case 0x00:
                    SyncSoundDevices(CurrentBlockCycles());
                    ay_[0]->WriteAddress(value);
                    break;
                case 0x02:
                    SyncSoundDevices(CurrentBlockCycles());
                    ay_[0]->WriteData(value);
                    break;
                case 0x04:
                    SyncSoundDevices(CurrentBlockCycles());
                    ay_[1]->WriteAddress(value);
                    break;
                case 0x06:
                    SyncSoundDevices(CurrentBlockCycles());
                    ay_[1]->WriteData(value);
                    break;
                case 0x08:
                    SyncSoundDevices(CurrentBlockCycles());
                    ay_[2]->WriteAddress(value);
                    break;
                case 0x0A:
                    SyncSoundDevices(CurrentBlockCycles());
                    ay_[2]->WriteData(value);
                    break;
                case 0x0C:
                    SyncSoundDevices(CurrentBlockCycles());
                    ay_[3]->WriteAddress(value);
                    break;
                case 0x0E:
                    SyncSoundDevices(CurrentBlockCycles());
                    ay_[3]->WriteData(value);
                    break;
                case 0x10:
                    SyncSoundDevices(CurrentBlockCycles());
                    ay_[4]->WriteAddress(value);
                    break;
                case 0x12:
                    SyncSoundDevices(CurrentBlockCycles());
                    ay_[4]->WriteData(value);
                    break;
                case 0x14:
                    // i8039_irq_w: asserts the i8039's external IRQ; the i8039
                    // clears it itself via a Port 2 write (OnI8039Port2Write).
                    i8039_irq_asserted_ = true;
                    i8039_->SetInputLine(I8039InputLine::Irq, I8039InputLineState::Assert);
                    break;
                case 0x18:
                    soundlatch2_ = value;
                    break;
                default:
                    break;
            }
        });

    i8039_->SetProgramMemoryCallback(
        [this](std::uint16_t addr) -> std::uint8_t {
            return addr < i8039_rom_.size() ? i8039_rom_[addr] : 0;
        });
    // i8039 external bus: every address reads soundlatch2; nothing is written.
    i8039_->SetBusCallbacks([this](std::uint8_t) -> std::uint8_t { return soundlatch2_; },
                              [](std::uint8_t, std::uint8_t) {});
    i8039_->SetPort1Callbacks(nullptr, [this](std::uint8_t value) { OnDacWrite(value); });
    i8039_->SetPort2Callbacks(nullptr, [this](std::uint8_t value) { OnI8039Port2Write(value); });
    // The i8039's T0/T1 test pins are not wired on this board (left at the core's default).

    // LogicalChannels: ay1..ay5's 3 tone generators each (ids 0-2, 3-5, 6-8,
    // 9-11, 12-14), then the DAC (id 15). Registered with the sink lazily on
    // Render()'s first call.
    channels_.clear();
    for (int chip = 0; chip < 5; ++chip) {
        for (std::uint32_t ch = 0; ch < Ay8910DeviceAdapter::kChannelCount; ++ch) {
            LogicalChannel channel;
            channel.Id = static_cast<std::uint32_t>(chip) * Ay8910DeviceAdapter::kChannelCount + ch;
            channel.DeviceName = "AY8910";
            channel.ChannelName = "AY" + std::to_string(chip + 1) + " " + std::to_string(ch + 1);
            channel.NativeSampleRate =
                static_cast<std::uint32_t>(kAy8910ClockRate.hz / Ay8910DeviceAdapter::kInputClocksPerSample);
            // Per-chip gain from MAME's DISCRETE mixer description (see kAy1RoutingGain).
            switch (chip) {
                case 0:
                    channel.RoutingGain = kAy1RoutingGain;
                    break;
                case 1:
                    channel.RoutingGain = kAy2RoutingGain;
                    break;
                case 4:
                    channel.RoutingGain = kAy5RoutingGain;
                    break;
                default:
                    channel.RoutingGain = kAy34RoutingGain;
                    break;
            }
            // Hard L/R split by chip: in MAME's gyruss.cpp, ay1/ay3/ay4 feed
            // the right DISCRETE_MIXER5 bus (konami_right_mixer_desc) and
            // ay2/ay5/DAC the left one (konami_left_mixer_desc). The buses
            // are separate with no crossfeed, so Pan is -1.0/+1.0.
            switch (chip) {
                case 0:  // ay1
                case 2:  // ay3
                case 3:  // ay4
                    channel.Pan = 1.0f;
                    break;
                case 1:  // ay2
                case 4:  // ay5
                    channel.Pan = -1.0f;
                    break;
                default:
                    break;
            }
            channels_.push_back(std::move(channel));
        }
    }
    {
        LogicalChannel channel;
        channel.Id = 5 * Ay8910DeviceAdapter::kChannelCount;
        channel.DeviceName = "i8039";
        channel.ChannelName = "DAC";
        // OnDacWrite()/Render() already produce samples on the canonical
        // 192kHz timeline, so no resampling is needed.
        channel.NativeSampleRate = kNativeRuntimeTickRateHz;
        channel.RoutingGain = kDacRoutingGain;
        // DAC feeds the left bus together with ay2/ay5.
        channel.Pan = -1.0f;
        channels_.push_back(std::move(channel));
    }
    channels_registered_ = false;

    // Mixer-output low-pass (see mixer_lpf_): right bus ay1/ay3/ay4, left bus ay2/ay5/DAC.
    for (std::size_t chip = 0; chip < 5; ++chip) {
        const bool left = (chip == 1 || chip == 4);
        for (std::size_t ch = 0; ch < Ay8910DeviceAdapter::kChannelCount; ++ch) {
            mixer_lpf_[chip * Ay8910DeviceAdapter::kChannelCount + ch].Configure(
                left ? GyrussMixerLowPass::LeftBusOhms() : GyrussMixerLowPass::RightBusOhms(), kAyOutputRateHz);
        }
    }
    mixer_lpf_[15].Configure(GyrussMixerLowPass::LeftBusOhms(), static_cast<double>(kNativeRuntimeTickRateHz));

    for (auto& chipScratch : ay_channel_scratch_) {
        chipScratch.assign(Ay8910DeviceAdapter::kChannelCount, {});
    }

    RunBootWarmup();
}

void GyrussMachine::Reset() {
    z80_->Reset();
    i8039_->Reset();
    for (auto& chip : ay_rc_filters_) {
        for (GyrussAyRcFilter& filter : chip) filter.Reset();
    }
    for (auto& values : ay_port_b_values_) values.clear();
    // Each AY's own reset reports Port B as an input (0xFF), as MAME's does,
    // which switches both capacitors in until the driver writes Port B.
    for (auto* ay : ay_) ay->Reset();
    command_interface_.reset();
    command_interface_ = std::make_unique<GyrussCommandInterface>(*z80_);
    soundlatch2_ = 0;
    i8039_irq_asserted_ = false;
    dac_current_value_ = 0.0f;
    dac_hold_before_block_ = 0.0f;
    dac_write_events_.clear();
    dc_block_state_.fill(DcBlockState{});
    for (GyrussMixerLowPass& lpf : mixer_lpf_) lpf.Reset();
    cumulative_ticks_processed_ = 0;
    cumulative_cycles_executed_ = 0;
    i8039_cumulative_cycles_executed_ = 0;
    sound_sync_cursor_ = 0;
    block_base_cycles_ = 0;
    irq_awaiting_ack_ = false;
    RunBootWarmup();
}

void GyrussMachine::Shutdown() {}

std::vector<LogicalChannel>& GyrussMachine::LogicalChannels() { return channels_; }

void GyrussMachine::SendCommand(const MusicCommand& command) { command_interface_->Send(command); }

void GyrussMachine::OnPanOverrideCleared(std::uint32_t) {
    // Re-applies every channel's fixed hard-L/R Pan from Initialize().
    for (std::uint32_t chip = 0; chip < 5; ++chip) {
        const float pan = (chip == 1 || chip == 4) ? -1.0f : 1.0f;
        for (std::uint32_t ch = 0; ch < Ay8910DeviceAdapter::kChannelCount; ++ch) {
            LogicalChannel& channel = channels_[chip * Ay8910DeviceAdapter::kChannelCount + ch];
            if (!channel.PanOverridden) channel.Pan = pan;
        }
    }
    LogicalChannel& dac = channels_[5 * Ay8910DeviceAdapter::kChannelCount];
    if (!dac.PanOverridden) dac.Pan = -1.0f;
}

void GyrussMachine::ApplyDcBlock(std::vector<float>& buffer, DcBlockState& state) {
    for (float& sample : buffer) {
        const float output = sample - state.prevInput + kDcBlockR * state.prevOutput;
        state.prevInput = sample;
        state.prevOutput = output;
        sample = output;
    }
}

void GyrussMachine::OnDacWrite(std::uint8_t value) {
    // Backing counters for DrainDacWriteStatsForTest().
    ++dac_write_count_debug_;
    dac_write_min_debug_ = std::min(dac_write_min_debug_, value);
    dac_write_max_debug_ = std::max(dac_write_max_debug_, value);

    // Unsigned 8-bit sample -> signed [-1,1], scaled by kDacFullScale.
    dac_current_value_ = (static_cast<float>(value) - 128.0f) / 128.0f * kDacFullScale;

    // Record where in this block (i8039 cycles since the Execute() call began)
    // the write happened; the callback runs after the instruction's cycles are
    // counted, so CyclesConsumedThisCall() already includes it.
    dac_write_events_.push_back({i8039_->CyclesConsumedThisCall(), dac_current_value_});
}

void GyrussMachine::OnAyPortBWrite(int chip, std::uint8_t value) {
    for (std::size_t ch = 0; ch < 3; ++ch) {
        ay_rc_filters_[static_cast<std::size_t>(chip)][ch].SetSwitches(static_cast<std::uint8_t>(value >> (2 * ch)));
    }
    auto& values = ay_port_b_values_[static_cast<std::size_t>(chip)];
    if (values.empty() || values.back() != value) {
        if (values.size() < 256) values.push_back(value);
    }
}

void GyrussMachine::OnI8039Port2Write(std::uint8_t) {
    if (i8039_irq_asserted_) {
        i8039_irq_asserted_ = false;
        i8039_->SetInputLine(I8039InputLine::Irq, I8039InputLineState::Clear);
    }
}

void GyrussMachine::Render(EmuDuration duration, LogicalChannelSink* sink) {
    if (sink != nullptr && !channels_registered_) {
        for (const LogicalChannel& channel : channels_) {
            sink->RegisterChannel(channel.Id);
        }
        channels_registered_ = true;
    }

    for (auto& chipScratch : ay_channel_scratch_) {
        for (std::vector<float>& buffer : chipScratch) buffer.clear();
    }
    // Cleared every call, including sink-less warm-up calls.
    dac_write_events_.clear();

    // Asserts the command IRQ if a Send() armed one; an assertion not yet taken
    // in an earlier block carries forward.
    if (command_interface_->Tick()) {
        irq_awaiting_ack_ = true;
    }

    cumulative_ticks_processed_ += duration.ticks;

    const std::uint64_t targetCumulativeCycles = clock_converter_.TicksToCycles(EmuDuration(cumulative_ticks_processed_));
    const std::uint64_t cycles =
        targetCumulativeCycles > cumulative_cycles_executed_ ? targetCumulativeCycles - cumulative_cycles_executed_ : 0;
    sound_sync_cursor_ = 0;
    block_base_cycles_ = 0;
    std::uint64_t cyclesRemaining = cycles;

    if (irq_awaiting_ack_) {
        // Prime one atomic unit of Z80 execution at a time until the IRQ is taken,
        // then drop the line immediately; never hold it for the rest of the block.
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
        // else: interrupts were still disabled when the budget ran out; the line
        // stays asserted and a later block retries (like a level-triggered line).
    }

    if (cyclesRemaining > 0) {
        z80_->Execute(cyclesRemaining);
        const std::uint64_t consumed = z80_->CyclesConsumedThisCall();
        cumulative_cycles_executed_ += consumed;
        block_base_cycles_ += consumed;
    }

    SyncSoundDevices(block_base_cycles_);

    // i8039: block-granularity sync is enough for the Z80-to-i8039 handoff.
    const std::uint64_t i8039TargetCycles = i8039_clock_converter_.TicksToCycles(EmuDuration(cumulative_ticks_processed_));
    if (i8039TargetCycles > i8039_cumulative_cycles_executed_) {
        i8039_->Execute(i8039TargetCycles - i8039_cumulative_cycles_executed_);
        i8039_cumulative_cycles_executed_ = i8039TargetCycles;
    }

    if (sink != nullptr) {
        // AY8910 x5: DC-block each channel (the AY8910 volume-0 DC bias must not
        // reach the mix), then the mixer low-pass. ay1/ay2's RC low-pass has already
        // run in SyncSoundDevices().
        for (int chip = 0; chip < 5; ++chip) {
            for (std::uint32_t ch = 0; ch < Ay8910DeviceAdapter::kChannelCount; ++ch) {
                const auto channelIndex = static_cast<std::size_t>(chip) * Ay8910DeviceAdapter::kChannelCount + ch;
                ApplyDcBlock(ay_channel_scratch_[static_cast<std::size_t>(chip)][ch], dc_block_state_[channelIndex]);
                auto& samples = ay_channel_scratch_[static_cast<std::size_t>(chip)][ch];
                mixer_lpf_[channelIndex].Process(samples.data(), samples.size());
                sink->WriteSamples(channels_[channelIndex].Id, ay_channel_scratch_[static_cast<std::size_t>(chip)][ch].data(),
                                    ay_channel_scratch_[static_cast<std::size_t>(chip)][ch].size());
            }
        }

        // DAC: replay every Port 1 write recorded this block at its own offset
        // (i8039 cycles -> canonical ticks via the fixed 192000/8'000'000 ratio;
        // exact placement is not needed at this precision). Ticks before the first
        // write hold dac_hold_before_block_, the value at the end of the previous
        // block.
        dac_channel_scratch_.assign(static_cast<std::size_t>(duration.ticks), 0.0f);
        {
            constexpr double kTicksPerI8039Cycle =
                static_cast<double>(kNativeRuntimeTickRateHz) / static_cast<double>(kI8039ClockRate.hz);
            std::size_t filledTo = 0;
            float carry = dac_hold_before_block_;
            for (const DacWriteEvent& ev : dac_write_events_) {
                auto tickOffset = static_cast<std::size_t>(static_cast<double>(ev.i8039CyclesInBlock) * kTicksPerI8039Cycle);
                tickOffset = std::min(tickOffset, dac_channel_scratch_.size());
                for (std::size_t i = filledTo; i < tickOffset; ++i) dac_channel_scratch_[i] = carry;
                filledTo = tickOffset;
                carry = ev.sample;
            }
            for (std::size_t i = filledTo; i < dac_channel_scratch_.size(); ++i) dac_channel_scratch_[i] = carry;
            dac_hold_before_block_ = carry;
        }
        ApplyDcBlock(dac_channel_scratch_, dc_block_state_[5 * Ay8910DeviceAdapter::kChannelCount]);
        mixer_lpf_[5 * Ay8910DeviceAdapter::kChannelCount].Process(dac_channel_scratch_.data(), dac_channel_scratch_.size());
        sink->WriteSamples(channels_[5 * Ay8910DeviceAdapter::kChannelCount].Id, dac_channel_scratch_.data(),
                            dac_channel_scratch_.size());
    }
}

void GyrussMachine::SyncSoundDevices(std::uint64_t z80CycleMark) {
    if (z80CycleMark <= sound_sync_cursor_) return;
    const std::uint64_t delta = z80CycleMark - sound_sync_cursor_;
    sound_sync_cursor_ = z80CycleMark;
    // The AY8910 clock is exactly half the Z80 clock, so no ClockConverter is needed.
    for (int chip = 0; chip < 5; ++chip) {
        auto& scratch = ay_channel_scratch_[static_cast<std::size_t>(chip)];
        const std::size_t before = scratch[0].size();
        ay_[static_cast<std::size_t>(chip)]->Render(delta / 2, scratch);
        if (chip < 2) {
            // ay1/ay2: RC low-pass on each channel, with the Port B setting in
            // effect for this stretch (a Port B write lands after this sync).
            for (std::size_t ch = 0; ch < scratch.size(); ++ch) {
                ay_rc_filters_[static_cast<std::size_t>(chip)][ch].Process(scratch[ch].data() + before,
                                                                          scratch[ch].size() - before);
            }
        }
    }
}

void GyrussMachine::RunBootWarmup() {
    for (std::uint64_t rendered = 0; rendered < kBootWarmupFrames; rendered += kWarmupChunkFrames) {
        Render(EmuDuration{kWarmupChunkFrames}, nullptr);
    }
}

MameDerivedZ80Adapter::RegisterSnapshot GyrussMachine::Z80Snapshot() const { return z80_->Snapshot(); }
MameDerivedI8039Adapter::RegisterSnapshot GyrussMachine::I8039Snapshot() const { return i8039_->Snapshot(); }
std::uint8_t GyrussMachine::ReadZ80Memory(std::uint16_t address) const { return sound_memory_map_.Read8(address); }

GyrussMachine::DacWriteStatsForTest GyrussMachine::DrainDacWriteStatsForTest() {
    DacWriteStatsForTest stats{dac_write_count_debug_, dac_write_min_debug_, dac_write_max_debug_};
    dac_write_count_debug_ = 0;
    dac_write_min_debug_ = 255;
    dac_write_max_debug_ = 0;
    return stats;
}
