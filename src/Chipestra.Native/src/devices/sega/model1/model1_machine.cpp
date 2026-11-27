// license:BSD-3-Clause
// copyright-holders:R. Belmont
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Portions derived from MAME (src/mame/shared/segam1audio.cpp); see THIRD-PARTY-NOTICES.md.
#include "devices/sega/model1/model1_machine.h"

#include "pugixml/pugixml.hpp"

#include <algorithm>
#include <charconv>
#include <stdexcept>

namespace {

constexpr std::uint32_t kProgramRegionSize = 0xC0000;
constexpr std::uint32_t kSampleRegionSize = 0x400000;
constexpr std::uint32_t kBankSize = 0x100000;

bool Fail(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
    return false;
}

bool ParseHexByte(std::string_view text, std::uint8_t& value) {
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) text.remove_prefix(2);
    unsigned parsed = 0;
    if (text.empty() || text.size() > 2) return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed, 16);
    if (result.ec != std::errc() || result.ptr != text.data() + text.size()) return false;
    value = static_cast<std::uint8_t>(parsed);
    return true;
}

std::vector<std::string_view> SplitSpaces(std::string_view list) {
    std::vector<std::string_view> parts;
    std::size_t i = 0;
    while (i < list.size()) {
        while (i < list.size() && list[i] == ' ') ++i;
        const std::size_t start = i;
        while (i < list.size() && list[i] != ' ') ++i;
        if (i > start) parts.push_back(list.substr(start, i - start));
    }
    return parts;
}

}  // namespace

bool ParseModel1DriverConfig(std::string_view driverXml, Model1DriverConfig& out, std::string* error) {
    out = Model1DriverConfig{};
    pugi::xml_document doc;
    const pugi::xml_parse_result parsed = doc.load_buffer(driverXml.data(), driverXml.size(), pugi::parse_default, pugi::encoding_utf8);
    if (!parsed) return Fail(error, std::string("<driver>: ") + parsed.description());
    const pugi::xml_node node = doc.child("driver");
    if (!node) return Fail(error, "no <driver> element");

    out.messages.assign(8, {});
    for (int n = 0; n <= 7; ++n) {
        const std::string name = n == 0 ? "midi" : "midi" + std::to_string(n);
        const pugi::xml_attribute attribute = node.attribute(name.c_str());
        if (!attribute) continue;
        for (std::string_view token : SplitSpaces(attribute.value())) {
            std::uint8_t byte = 0;
            if (token != "$S" && !ParseHexByte(token, byte))
                return Fail(error, "<driver " + name + ">: expected hexadecimal bytes and $S separated by spaces");
            out.messages[n].emplace_back(token);
        }
    }
    if (out.messages[0].empty()) return Fail(error, "<driver midi> is required (the bytes that start a track)");
    if (const pugi::xml_attribute warmup = node.attribute("warmup_ms")) {
        const std::string_view text = warmup.value();
        std::uint32_t value = 0;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value, 10);
        if (text.empty() || result.ec != std::errc() || result.ptr != text.data() + text.size() || value > 10000)
            return Fail(error, "<driver warmup_ms>: expected a number of milliseconds (0-10000)");
        out.warmupMilliseconds = value;
    }
    return true;
}

Model1MachineInputs AssembleModel1MachineInputs(const MachineResources& resources) {
    Model1MachineInputs inputs;
    if (resources.Count() == 0) return inputs;

    const ResolvedResource* program0 = resources.Find("sndcpu0");
    if (program0 == nullptr) throw std::runtime_error("Model 1: required resource \"sndcpu0\" is missing");
    // The program ROMs are loaded as 16-bit words with the bytes of each word swapped.
    inputs.program.assign(kProgramRegionSize, 0);
    auto load = [&](const ResolvedResource& rom, std::size_t at) {
        for (std::size_t i = 0; i + 1 < rom.Size() && at + i + 1 < inputs.program.size(); i += 2) {
            inputs.program[at + i] = rom.Data()[i + 1];
            inputs.program[at + i + 1] = rom.Data()[i];
        }
    };
    load(*program0, 0);
    if (const ResolvedResource* program1 = resources.Find("sndcpu1")) {
        load(*program1, 0x20000);
        load(*program1, 0x80000);   // the upper ROM socket is seen a second time at 0x80000
    }

    auto assembleSamples = [&](const char* first, const char* second, std::vector<std::uint8_t>& out) {
        const ResolvedResource* a = resources.Find(first);
        const ResolvedResource* b = resources.Find(second);
        if (a == nullptr && b == nullptr) return;
        out.assign(kSampleRegionSize, 0);
        if (a != nullptr) std::copy_n(a->Data(), std::min<std::size_t>(a->Size(), 0x200000), out.begin());
        if (b != nullptr) std::copy_n(b->Data(), std::min<std::size_t>(b->Size(), 0x200000), out.begin() + 0x200000);
    };
    assembleSamples("pcm1_0", "pcm1_1", inputs.pcm1);
    assembleSamples("pcm2_0", "pcm2_1", inputs.pcm2);

    const ResolvedResource* driver = resources.Find("driver_xml");
    if (driver == nullptr) throw std::runtime_error("Model 1: the game XML needs a <driver> element");
    std::string error;
    if (!ParseModel1DriverConfig(std::string_view(reinterpret_cast<const char*>(driver->Data()), driver->Size()), inputs.driver, &error))
        throw std::runtime_error("Model 1: " + error);
    return inputs;
}

Model1Machine::Model1Machine(std::vector<std::uint8_t> program, std::vector<std::uint8_t> pcm1, std::vector<std::uint8_t> pcm2,
                             Model1DriverConfig driver)
    : program_(std::move(program)),
      driver_(std::move(driver)),
      clock_converter_(kNativeRuntimeTickRateHz, ClockRate{kCpuHz}) {
    pcm_rom_[0] = std::move(pcm1);
    pcm_rom_[1] = std::move(pcm2);
}

Model1Machine::~Model1Machine() = default;

std::uint8_t Model1Machine::SampleByte(std::uint32_t chip, std::uint32_t address) const {
    const std::vector<std::uint8_t>& rom = pcm_rom_[chip];
    std::uint32_t index;
    if (address < kBankSize) {
        index = address;
    } else if (address < 2 * kBankSize) {
        index = pcm_bank_[chip] * kBankSize + (address - kBankSize);
    } else {
        return 0;
    }
    return index < rom.size() ? rom[index] : std::uint8_t{0};
}

void Model1Machine::Initialize() {
    cpu_ = std::make_unique<MameDerivedM68000Adapter>();
    cpu_->SetStandardTimings(true);
    cpu_->SetMemoryCallbacks([this](std::uint32_t address) { return Read(address); },
                             [this](std::uint32_t address, std::uint8_t value) { Write(address, value); });
    for (std::uint32_t chip = 0; chip < 2; ++chip) {
        pcm_[chip] = std::make_unique<MultiPcmDeviceAdapter>(
            kPcmClockHz, [this, chip](std::uint32_t address) { return SampleByte(chip, address); });
    }
    // The YM3438's timers and busy flag count in ticks of 1/40MHz, five of them to a chip clock (8MHz).
    ym_ = std::make_unique<Ym2612DeviceAdapter>(5);

    channels_.clear();
    for (std::uint32_t chip = 0; chip < 2; ++chip) {
        for (std::uint32_t v = 0; v < kPcmVoices; ++v) {
            LogicalChannel channel;
            channel.Id = (chip == 0 ? kPcm1Base : kPcm2Base) + v;
            channel.DeviceName = chip == 0 ? "YMW258-F #1" : "YMW258-F #2";
            channel.ChannelName = "PCM " + std::to_string(v + 1);
            channel.NativeSampleRate = (kPcmClockHz + MultiPcmDeviceAdapter::kClockDivider / 2) / MultiPcmDeviceAdapter::kClockDivider;
            channel.RoutingGain = kPcmRoutingGain;
            channels_.push_back(std::move(channel));
        }
    }
    for (std::uint32_t ch = 0; ch < Ym2612DeviceAdapter::kChannelCount; ++ch) {
        LogicalChannel channel;
        channel.Id = kYmBase + ch;
        channel.DeviceName = "YM3438";
        channel.ChannelName = ch == Ym2612DeviceAdapter::kDacChannel ? "DAC" : "FM" + std::to_string(ch + 1);
        channel.NativeSampleRate = (kYmClockHz + Ym2612DeviceAdapter::kInputClocksPerSample / 2) / Ym2612DeviceAdapter::kInputClocksPerSample;
        channel.RoutingGain = kYmRoutingGain;
        channels_.push_back(std::move(channel));
    }
    pcm_scratch_[0].assign(kPcmVoices, {});
    pcm_scratch_[1].assign(kPcmVoices, {});
    ym_scratch_.assign(Ym2612DeviceAdapter::kChannelCount, {});

    Reset();
}

void Model1Machine::ResetState() {
    ram_.fill(0);
    pcm_bank_[0] = pcm_bank_[1] = 0;
    rx_data_ = 0;
    rx_ready_ = false;
    rx_overrun_ = false;
    midi_queue_.clear();
    midi_position_ = 0;
    next_byte_cycle_ = 0;
    bytes_received_ = 0;
    overruns_ = 0;
    cycles_ = 0;
    block_base_ = 0;
    cumulative_ticks_ = 0;
    pcm_cursor_[0] = pcm_cursor_[1] = 0;
    ym_cursor_ticks_ = 0;
    ym_clocks_made_ = 0;
    command_sent_since_reset_ = false;
    channels_registered_ = false;
    for (auto& chip : pcm_scratch_) for (auto& buffer : chip) buffer.clear();
    for (auto& buffer : ym_scratch_) buffer.clear();
    dc_.fill(DcBlocker{});
    pcm_[0]->Reset();
    pcm_[1]->Reset();
    ym_->Reset();
    cpu_->SetInterruptLevel(0);
    cpu_->Reset();
}

void Model1Machine::Reset() {
    ResetState();
    // The driver starts itself up (it clears its RAM and sets the chips up) before it takes the first message.
    RunTo(static_cast<std::uint64_t>(driver_.warmupMilliseconds) * (kCpuHz / 1000));
    SyncAll(cycles_);
    BlockYmDc();  // the blocker has settled on the chip's constant output when the first message arrives
    for (auto& chip : pcm_scratch_) for (auto& buffer : chip) buffer.clear();
    for (auto& buffer : ym_scratch_) buffer.clear();
}

void Model1Machine::Shutdown() {}

void Model1Machine::UpdateInterruptLevel() {
    cpu_->SetInterruptLevel(rx_ready_ ? 2 : 0);
}

void Model1Machine::DeliverByte(std::uint8_t value) {
    if (rx_ready_) {
        rx_overrun_ = true;   // the 68000 has not read the previous byte
        ++overruns_;
    }
    rx_data_ = value;
    rx_ready_ = true;
    ++bytes_received_;
    UpdateInterruptLevel();
}

void Model1Machine::SyncPcm(std::uint32_t chip, std::uint64_t cycles) {
    if (cycles <= pcm_cursor_[chip]) return;
    const std::uint64_t delta = cycles - pcm_cursor_[chip];
    pcm_cursor_[chip] = cycles;
    pcm_[chip]->Render(delta, pcm_scratch_[chip]);
}

void Model1Machine::SyncYm(std::uint64_t cycles) {
    // 4 cycles of the 68000 (10MHz) are 5 clocks of the YM3438 (8MHz); one tick is 1/40MHz.
    const std::uint64_t ticks = cycles * 4;
    const std::uint64_t clocks = ticks / 5;
    if (clocks > ym_clocks_made_) {
        ym_->Render(clocks - ym_clocks_made_, ym_scratch_);
        ym_clocks_made_ = clocks;
    }
    if (ticks > ym_cursor_ticks_) ym_cursor_ticks_ = ticks;
    ym_->SetAccessTime(ym_cursor_ticks_);
}

void Model1Machine::SyncAll(std::uint64_t cycles) {
    SyncPcm(0, cycles);
    SyncPcm(1, cycles);
    SyncYm(cycles);
}

std::uint8_t Model1Machine::Read(std::uint32_t address) {
    address &= 0xFFFFFF;
    if (address < 0x40000) return address < program_.size() ? program_[address] : std::uint8_t{0};
    if (address >= 0x80000 && address < 0xA0000) {
        const std::uint32_t index = 0x20000 + (address - 0x80000);
        return index < program_.size() ? program_[index] : std::uint8_t{0};
    }
    if (address >= 0xF00000 && address < 0xF10000) return ram_[address - 0xF00000];
    if (address >= 0xC20000 && address < 0xC20004) {
        if (address == 0xC20001) {   // the receive register
            const std::uint8_t value = rx_data_;
            rx_ready_ = false;
            UpdateInterruptLevel();
            if (midi_observer_) midi_observer_(AccessCycles(), value);
            return value;
        }
        if (address == 0xC20003) {   // the status register: bit 0 transmitter ready, 1 receiver ready, 2 transmitter empty, 4 overrun error
            return static_cast<std::uint8_t>(0x05 | (rx_ready_ ? 0x02 : 0x00) | (rx_overrun_ ? 0x10 : 0x00));
        }
        return 0;
    }
    if (address >= 0xD00000 && address < 0xD00008) {
        if ((address & 7) == 1) {   // the status register
            SyncYm(AccessCycles());
            return ym_->ReadStatus();
        }
        return 0;
    }
    return 0;
}

void Model1Machine::Write(std::uint32_t address, std::uint8_t value) {
    address &= 0xFFFFFF;
    if (address >= 0xF00000 && address < 0xF10000) {
        ram_[address - 0xF00000] = value;
        return;
    }
    if (address >= 0xC20000 && address < 0xC20004) {
        if (address == 0xC20003) {   // the command register: bit 4 resets the error flags, bit 6 resets the port
            if (value & 0x50) rx_overrun_ = false;
        }
        return;
    }
    if ((address >= 0xC40000 && address < 0xC40008) || (address >= 0xC60000 && address < 0xC60008)) {
        if ((address & 1) == 0) return;
        const std::uint32_t chip = address >= 0xC60000 ? 1 : 0;
        const std::uint32_t offset = (address & 7) >> 1;
        if (offset > 2) return;
        const std::uint64_t now = AccessCycles();
        SyncPcm(chip, now);
        if (pcm_observer_) pcm_observer_(now, chip, offset, value);
        pcm_[chip]->Write(offset, value);
        return;
    }
    if (address == 0xC50001 || address == 0xC70001) {   // the sample ROM bank of the MultiPCM
        const std::uint32_t chip = address == 0xC70001 ? 1 : 0;
        SyncPcm(chip, AccessCycles());
        pcm_bank_[chip] = value & 3;
        return;
    }
    if (address >= 0xD00000 && address < 0xD00008) {
        if ((address & 1) == 0) return;
        SyncYm(AccessCycles());
        if (ym_write_observer_) ym_write_observer_(AccessCycles(), (address & 7) >> 1, value);
        ym_->Write((address & 7) >> 1, value);
        return;
    }
}

void Model1Machine::RunTo(std::uint64_t targetCycles) {
    while (cycles_ < targetCycles) {
        std::uint64_t stop = targetCycles;
        if (midi_position_ < midi_queue_.size()) {
            const std::uint64_t due = std::max(midi_queue_[midi_position_].cycle, next_byte_cycle_);
            if (due > cycles_) stop = std::min(stop, due);
        }
        if (stop > cycles_) {
            block_base_ = cycles_;
            cpu_->Execute(stop - cycles_);
            // A 68000 that has stopped (a STOP waiting for an interrupt, or a double bus fault) consumes no cycles: the time passes all the same.
            const std::uint64_t consumed = cpu_->CyclesConsumedThisCall();
            cycles_ += consumed != 0 ? consumed : stop - cycles_;
        }
        while (midi_position_ < midi_queue_.size() && cycles_ >= std::max(midi_queue_[midi_position_].cycle, next_byte_cycle_)) {
            DeliverByte(midi_queue_[midi_position_++].value);
            next_byte_cycle_ = cycles_ + kByteCycles;
        }
    }
}

void Model1Machine::UpdateStereoRouting() {
    for (std::uint32_t chip = 0; chip < 2; ++chip) {
        for (std::uint32_t v = 0; v < kPcmVoices; ++v) {
            LogicalChannel& channel = channels_[(chip == 0 ? kPcm1Base : kPcm2Base) + v];
            const float left = pcm_[chip]->VoiceGainLeft(v);
            const float right = pcm_[chip]->VoiceGainRight(v);
            const float sum = left + right;
            if (sum > 0.0f && !channel.PanOverridden) channel.Pan = (right - left) / sum;
            // A voice that stays on at the largest attenuation is the driver's idle voice, not part of the music.
            channel.ExcludeFromSongEnd = pcm_[chip]->VoicePlaying(v) && pcm_[chip]->VoiceTotalLevel(v) >= kIdleTotalLevel;
        }
    }
    for (std::uint32_t ch = 0; ch < Ym2612DeviceAdapter::kChannelCount; ++ch) {
        const bool left = ym_->ChannelOutputsLeft(ch);
        const bool right = ym_->ChannelOutputsRight(ch);
        LogicalChannel& channel = channels_[kYmBase + ch];
        channel.RoutingGain = kYmRoutingGain * static_cast<float>((left ? 1 : 0) + (right ? 1 : 0));
        if (!channel.PanOverridden) channel.Pan = (left == right) ? 0.0f : (left ? -1.0f : 1.0f);
    }
}

void Model1Machine::Render(EmuDuration duration, LogicalChannelSink* sink) {
    if (sink != nullptr && !channels_registered_) {
        for (const LogicalChannel& channel : channels_) {
            if (!sink->HasChannel(channel.Id)) sink->RegisterChannel(channel.Id);
        }
        channels_registered_ = true;
    }

    cumulative_ticks_ += duration.ticks;
    const std::uint64_t target = clock_converter_.TicksToCycles(EmuDuration(cumulative_ticks_));
    RunTo(target);
    SyncAll(std::max(target, cycles_));
    UpdateStereoRouting();

    BlockYmDc();

    if (sink != nullptr) {
        for (std::uint32_t chip = 0; chip < 2; ++chip) {
            for (std::uint32_t v = 0; v < kPcmVoices; ++v) {
                const std::vector<float>& buffer = pcm_scratch_[chip][v];
                sink->WriteSamples(channels_[(chip == 0 ? kPcm1Base : kPcm2Base) + v].Id, buffer.data(), buffer.size());
            }
        }
        for (std::uint32_t ch = 0; ch < Ym2612DeviceAdapter::kChannelCount; ++ch) {
            sink->WriteSamples(channels_[kYmBase + ch].Id, ym_scratch_[ch].data(), ym_scratch_[ch].size());
        }
    }
    for (auto& chip : pcm_scratch_) for (auto& buffer : chip) buffer.clear();
    for (auto& buffer : ym_scratch_) buffer.clear();
}

void Model1Machine::BuildMessage(const MusicCommand& command, std::vector<std::uint8_t>& out) const {
    const unsigned number = (command.Auxiliary >> 4) & 7;
    const std::vector<std::string>& tokens = (number >= 1 && !driver_.messages[number].empty()) ? driver_.messages[number] : driver_.messages[0];
    for (const std::string& token : tokens) {
        if (token == "$S") {
            out.push_back(command.Value);
        } else {
            std::uint8_t byte = 0;
            ParseHexByte(token, byte);
            out.push_back(byte);
        }
    }
}

void Model1Machine::SendMidiBytesForTest(const std::vector<std::uint8_t>& bytes) {
    if (midi_position_ >= midi_queue_.size()) {
        midi_queue_.clear();
        midi_position_ = 0;
    }
    for (std::uint8_t byte : bytes) midi_queue_.push_back({cycles_, byte});
}

void Model1Machine::SendCommand(const MusicCommand& command) {
    // A track that stops the music (bit 7 of Auxiliary marks a control track), and any command after an earlier one,
    // start the sound board again from power-on: the driver has no command that silences every voice.
    if ((command.Auxiliary & 0x80) != 0) {
        Reset();
        return;
    }
    if (command_sent_since_reset_) Reset();
    std::vector<std::uint8_t> message;
    BuildMessage(command, message);
    SendMidiBytesForTest(message);
    command_sent_since_reset_ = true;
}

bool Model1Machine::AcceptsCommand(const MusicCommand& command) const {
    return command.Kind != MusicCommandKind::Wsg && command.Kind != MusicCommandKind::Effect;
}

void Model1Machine::BlockYmDc() {
    const float r = 1.0f - 2.0f * 3.14159265358979f * 30.0f / static_cast<float>(channels_[kYmBase].NativeSampleRate);
    for (std::uint32_t ch = 0; ch < Ym2612DeviceAdapter::kChannelCount; ++ch) {
        dc_[ch].Process(ym_scratch_[ch].data(), ym_scratch_[ch].size(), r);
    }
}
