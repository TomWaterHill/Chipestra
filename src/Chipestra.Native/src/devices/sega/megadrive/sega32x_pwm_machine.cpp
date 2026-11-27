// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill

#include "sega32x_pwm_machine.h"

#include "devices/sega/megadrive/megadrive_machine.h"
#include "resource_hash.h"

#include "pugixml/pugixml.hpp"

#include <cmath>
#include <algorithm>
#include <charconv>
#include <cstdio>
#include <stdexcept>

namespace {

constexpr float kDcBlockHz = 30.0f;

bool ParseNumber(std::string_view text, std::uint64_t& value) {
    int base = 10;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) { base = 16; text.remove_prefix(2); }
    if (text.empty()) return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, base);
    return result.ec == std::errc() && result.ptr == text.data() + text.size();
}

bool Fail(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
    return false;
}

}  // namespace

bool ParseSega32xPwmDriverConfig(std::string_view driverXml, Sega32xPwmDriverConfig& out, std::string* error) {
    out = Sega32xPwmDriverConfig{};
    pugi::xml_document doc;
    const pugi::xml_parse_result parsed = doc.load_buffer(driverXml.data(), driverXml.size(), pugi::parse_default, pugi::encoding_utf8);
    if (!parsed) return Fail(error, std::string("<driver>: ") + parsed.description());
    const pugi::xml_node node = doc.child("driver");
    if (!node) return Fail(error, "no <driver> element");
    auto number = [&](const char* name, std::uint64_t maxValue, std::uint64_t& value) {
        const pugi::xml_attribute attribute = node.attribute(name);
        if (!attribute) return Fail(error, std::string("<driver ") + name + "> is required");
        if (!ParseNumber(attribute.value(), value) || value > maxValue) return Fail(error, std::string("<driver ") + name + ">: expected a number up to " + std::to_string(maxValue));
        return true;
    };
    std::uint64_t value = 0;
    if (node.attribute("sh2_event_ring")) {
        if (!number("sh2_event_ring", 0x1FFE, value)) return false;
        out.eventRing = true;
        out.eventRingAddress = static_cast<std::uint16_t>(value);
    } else if (node.attribute("sh2_request_word")) {
        if (!number("sh2_request_word", 0x2E, value) || (value & 1) != 0 || value < 0x20) return Fail(error, "<driver sh2_request_word>: expected an even offset from 0x20 to 0x2E");
        out.requestWord = true;
        out.requestWordOffset = static_cast<std::uint32_t>(value);
        if (node.attribute("sh2_request_flag")) {
            if (!number("sh2_request_flag", 0x1FFF, value)) return false;
            out.requestFlag = static_cast<std::uint16_t>(value);
        }
    } else if (node.attribute("sh2_sample_table")) {
        if (!number("sh2_sample_table", 0xFFFFFFull, value)) return false;
        out.sampleTable = true;
        out.sampleTableOffset = static_cast<std::uint32_t>(value);
        if (node.attribute("sh2_sample_command")) {
            if (!number("sh2_sample_command", 0xFF, value)) return false;
            out.sampleCommand = static_cast<std::uint8_t>(value);
        }
        if (node.attribute("sh2_sample_stop")) {
            if (!number("sh2_sample_stop", 0xFF, value)) return false;
            out.sampleStop = static_cast<std::uint8_t>(value);
        }
    } else if (node.attribute("sh2_comm_word")) {
        // the 68000 asks the master processor for a sound by writing one communication word
        if (!number("sh2_comm_word", 0x2E, value) || (value & 1) != 0) return Fail(error, "<driver sh2_comm_word>: expected an even offset from 0x00 to 0x2E");
        out.commWordOffset = static_cast<std::uint32_t>(value);
        out.commWord = true;
        if (node.attribute("sh2_comm_base")) {
            if (!number("sh2_comm_base", 0xFFFF, value)) return false;
            out.commBase = static_cast<std::uint16_t>(value);
        }
    } else {
        if (!number("sh2_queue", 0xFFFFFFFFull, value)) return false;
        out.queueAddress = static_cast<std::uint32_t>(value);
        if (!number("sh2_command_address", 0xFFFFFFFFull, value)) return false;
        out.commandAddress = static_cast<std::uint32_t>(value);
        if (!number("sh2_command", 0xFF, value)) return false;
        out.command = static_cast<std::uint8_t>(value);
        if (node.attribute("sh2_mailbox")) {
            if (!number("sh2_mailbox", 0xFFFFFFull, value)) return false;
            out.mailbox = true;
            out.mailboxAddress = static_cast<std::uint32_t>(value);
        }
    }
    if (node.attribute("sh2_warmup_ms")) {
        if (!number("sh2_warmup_ms", 5000, value) || value < 50) return Fail(error, "<driver sh2_warmup_ms>: expected 50 to 5000");
        out.warmupMilliseconds = static_cast<std::uint32_t>(value);
    }
    if (const pugi::xml_attribute script = node.attribute("sh2_script")) {
        // "<frame>:<offset>=<value>[,<offset>=<value>...]" separated by spaces: at that video frame after the request, the words are written and the slave is interrupted
        std::string_view text = script.value();
        const char* usage = "<driver sh2_script>: expected steps like 0:0x28=0x8989 separated by spaces (a frame number, then offsets 0x20 to 0x2E with 16-bit values)";
        while (!text.empty()) {
            while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
            if (text.empty()) break;
            std::size_t end = text.find(' ');
            if (end == std::string_view::npos) end = text.size();
            std::string_view step = text.substr(0, end);
            text.remove_prefix(end);
            const std::size_t colon = step.find(':');
            std::uint64_t frame = 0;
            std::string_view when = colon == std::string_view::npos ? std::string_view() : step.substr(0, colon);
            std::uint64_t interrupt = 2;
            if (const std::size_t slash = when.find('/'); slash != std::string_view::npos) {
                if (!ParseNumber(when.substr(slash + 1), interrupt) || interrupt < 1 || interrupt > 3) return Fail(error, usage);
                when = when.substr(0, slash);
            }
            if (colon == std::string_view::npos || !ParseNumber(when, frame) || frame > 100000) return Fail(error, usage);
            Sega32xPwmDriverConfig::ScriptStep entry;
            entry.interrupt = static_cast<std::uint16_t>(interrupt);
            entry.frame = static_cast<std::uint32_t>(frame);
            step.remove_prefix(colon + 1);
            while (!step.empty()) {
                std::size_t comma = step.find(',');
                if (comma == std::string_view::npos) comma = step.size();
                const std::string_view pair = step.substr(0, comma);
                const std::size_t equals = pair.find('=');
                std::uint64_t offset = 0, word = 0;
                if (equals == std::string_view::npos || !ParseNumber(pair.substr(0, equals), offset) || !ParseNumber(pair.substr(equals + 1), word) || offset < 0x20 || offset > 0x2E || (offset & 1) != 0 || word > 0xFFFF)
                    return Fail(error, usage);
                entry.words.push_back({static_cast<std::uint32_t>(offset), static_cast<std::uint16_t>(word)});
                step.remove_prefix(std::min(comma + 1, step.size()));
            }
            if (entry.words.empty()) return Fail(error, usage);
            out.script.push_back(std::move(entry));
        }
        if (out.script.empty()) return Fail(error, usage);
    }
    if (const pugi::xml_attribute words = node.attribute("sh2_start_words")) {
        std::string_view text = words.value();
        while (!text.empty()) {
            while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
            std::size_t end = text.find(' ');
            if (end == std::string_view::npos) end = text.size();
            std::uint64_t word = 0;
            if (end == 0 || out.startWords.size() >= 4 || !ParseNumber(text.substr(0, end), word) || word > 0xFFFF)
                return Fail(error, "<driver sh2_start_words>: expected up to four words as 0xNNNN separated by spaces");
            out.startWords.push_back(static_cast<std::uint16_t>(word));
            text.remove_prefix(end);
        }
    }
    if (const pugi::xml_attribute halt = node.attribute("sh2_halt_master")) {
        const std::string_view text = halt.value();
        if (text != "true" && text != "false") return Fail(error, "<driver sh2_halt_master>: expected \"true\" or \"false\"");
        out.haltMaster = text == "true";
    }
    if (const pugi::xml_attribute crc = node.attribute("cart_crc32")) {
        std::string_view text = crc.value();
        std::uint32_t crcValue = 0;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), crcValue, 16);
        if (text.empty() || text.size() > 8 || result.ec != std::errc() || result.ptr != text.data() + text.size())
            return Fail(error, "<driver cart_crc32>: expected a CRC-32 in hexadecimal");
        out.cartridgeCrc32 = crcValue;
    }
    return true;
}

Sega32xPwmMachineInputs AssembleSega32xPwmMachineInputs(const MachineResources& resources) {
    Sega32xPwmMachineInputs inputs;
    if (resources.Count() == 0) return inputs;
    const ResolvedResource* rom = resources.Find(kMegaDriveCartridgeResource);
    if (rom == nullptr) throw std::runtime_error(std::string("32X: required resource \"") + kMegaDriveCartridgeResource + "\" is missing");
    const ResolvedResource* driver = resources.Find(kMegaDriveDriverResource);
    if (driver == nullptr) throw std::runtime_error(std::string("32X: required resource \"") + kMegaDriveDriverResource + "\" is missing (the game XML needs a <driver> element)");
    std::string error;
    if (!ParseSega32xPwmDriverConfig(std::string_view(reinterpret_cast<const char*>(driver->Data()), driver->Size()), inputs.driver, &error)) throw std::runtime_error("32X: " + error);
    inputs.cartridge.assign(rom->Data(), rom->Data() + rom->Size());
    if (inputs.driver.cartridgeCrc32.has_value()) {
        const std::uint32_t found = Crc32(inputs.cartridge.data(), inputs.cartridge.size());
        if (found != *inputs.driver.cartridgeCrc32) {
            char text[160];
            std::snprintf(text, sizeof(text), "32X: the cartridge image has CRC-32 %08x, but <driver cart_crc32> is %08x: a different dump or a damaged file", found, *inputs.driver.cartridgeCrc32);
            throw std::runtime_error(text);
        }
    }
    return inputs;
}

Sega32xPwmMachine::Sega32xPwmMachine(std::vector<std::uint8_t> cartridge, Sega32xPwmDriverConfig driver) : cartridge_(std::move(cartridge)), driver_(std::move(driver)) {
    for (int side = 0; side < 2; ++side) {
        LogicalChannel channel;
        channel.Id = ChannelId(static_cast<std::uint32_t>(channels_.size()));
        channel.DeviceName = "315-5818";
        channel.ChannelName = side == 0 ? "Left" : "Right";
        channel.NativeSampleRate = kOutputRate;
        channel.RoutingGain = kRoutingGain;
        channel.Pan = side == 0 ? -1.0f : 1.0f;
        // not in an analog filter group: the 32X's PWM output does not pass the Mega Drive's own low-pass stage
        channels_.push_back(std::move(channel));
    }
}

void Sega32xPwmMachine::Initialize() {
    if (cartridge_.empty()) return;
    ColdStart();
}

void Sega32xPwmMachine::Reset() {
    if (cartridge_.empty()) return;
    ColdStart();
}

void Sega32xPwmMachine::ColdStart() {
    scriptQueue_.clear();
    system_ = std::make_unique<Sega32xSystem>(cartridge_);
    if (!system_->BootFromHeader()) throw std::runtime_error("32X: the cartridge header does not describe an SH-2 program");
    const double clock = Sega32xPwm::kNtscClockHz;
    // the 68000's part of the start-up: both processors wait for the words "M_OK" and "S_OK" to be cleared
    system_->RunTo(static_cast<std::uint64_t>(clock * 0.05));
    for (unsigned offset = 0x20; offset < 0x28; offset += 2) system_->M68kWrite(offset, 0);
    for (std::size_t i = 0; i < driver_.startWords.size(); ++i) system_->M68kWrite(0x20 + 2 * static_cast<unsigned>(i), driver_.startWords[i]);
    const std::uint64_t warmup = static_cast<std::uint64_t>(clock * driver_.warmupMilliseconds / 1000.0);
    system_->RunTo(warmup);
    // drain the output made so far (the idle level stays held)
    {
        const std::size_t frames = static_cast<std::size_t>(static_cast<double>(system_->Now()) * kOutputRate / clock);
        std::vector<float> l(frames), r(frames);
        system_->RenderPwm(0, static_cast<double>(kOutputRate), l.data(), r.data(), frames);
    }
    renderedFrames_ = 0;
    warmupEnd_ = system_->Now();
    if (driver_.haltMaster) system_->HaltMaster();   // the sounds are started by this Machine (SendCommand); the game's own master program only idles
    commandSent_ = false;
    for (int i = 0; i < 2; ++i) dcPrevInput_[i] = dcPrevOutput_[i] = lowPass_[i][0] = lowPass_[i][1] = sallenKey_[i][0] = sallenKey_[i][1] = mixLowPass_[i][0] = mixLowPass_[i][1] = 0;
    // the high-pass starts at the held level so the idle level is not heard as a step
    idleLevelPending_ = true;
}

void Sega32xPwmMachine::SendCommand(const MusicCommand& command) {
    if (!system_) return;
    const bool control = (command.Auxiliary & 0x80) != 0;
    if (commandSent_ || control) ColdStart();   // the voices of an earlier request would go on playing
    if (control) return;
    if (!driver_.script.empty()) {
        // the start-up sound: the steps of the game's script, one video frame (59.92 Hz) apart in the way the game's 68000 sends them
        const double frameCycles = Sega32xPwm::kNtscClockHz / 59.92;
        scriptQueue_.clear();
        for (std::size_t i = 0; i < driver_.script.size(); ++i)
            scriptQueue_.emplace_back(system_->Now() + static_cast<std::uint64_t>(static_cast<double>(driver_.script[i].frame) * frameCycles), i);
        commandSent_ = true;
        return;
    }
    if (driver_.sampleTable) {
        const std::size_t at = static_cast<std::size_t>(driver_.sampleTableOffset) + 8u * command.Value;
        if (at + 8 > cartridge_.size()) return;
        auto be32 = [&](std::size_t o) { return (static_cast<std::uint32_t>(cartridge_[o]) << 24) | (static_cast<std::uint32_t>(cartridge_[o + 1]) << 16) | (static_cast<std::uint32_t>(cartridge_[o + 2]) << 8) | cartridge_[o + 3]; };
        const std::uint32_t descriptor = be32(at), priority = be32(at + 4);
        // the 68000 waits for the slave to take the earlier command (it clears the command byte)
        for (int guard = 0; guard < 400 && (system_->M68kRead(0x20) & 0xFF00) != 0; ++guard) system_->RunTo(system_->Now() + 200);
        system_->M68kWrite(0x24, static_cast<std::uint16_t>(descriptor >> 16));
        system_->M68kWrite(0x26, static_cast<std::uint16_t>(descriptor));
        system_->M68kWrite(0x28, static_cast<std::uint16_t>(priority >> 16));
        system_->M68kWrite(0x20, static_cast<std::uint16_t>(driver_.sampleCommand << 8), 0xFF00);
        system_->M68kWrite(0x02, 0x0101);   // both processors, as the game does
        // then the master's main-loop command 12 (command 0x08, argument 12): the master tells the slave to start the sound that was requested
        for (int guard = 0; guard < 400 && (system_->M68kRead(0x20) & 0xFF00) != 0; ++guard) system_->RunTo(system_->Now() + 200);
        system_->M68kWrite(0x24, 0x000C);
        system_->M68kWrite(0x20, 0x0800, 0xFF00);
        system_->M68kWrite(0x02, 0x0101);
        commandSent_ = true;
        return;
    }
    if (driver_.commWord) {
        system_->M68kWrite(driver_.commWordOffset + 0x20, static_cast<std::uint16_t>(driver_.commBase | command.Value));
        commandSent_ = true;
        return;
    }
    system_->Write(0, driver_.queueAddress + 2, command.Value, 2);
    system_->Write(0, driver_.queueAddress, 2, 2);
    system_->Write(0, driver_.commandAddress, driver_.command, 1);
    commandSent_ = true;
}

void Sega32xPwmMachine::SubmitSamples(const std::uint8_t* numbers, std::size_t count) {
    if (!system_ || count == 0 || count > 4 || driver_.commWord || driver_.eventRing) return;
    // the slave sets the byte count of the queue to 0 when it has taken the sounds in it
    for (int guard = 0; guard < 200 && system_->Read(0, driver_.queueAddress, 2) != 0; ++guard) system_->RunTo(system_->Now() + 200);
    for (std::size_t i = 0; i < count; ++i) system_->Write(0, driver_.queueAddress + 2 + static_cast<std::uint32_t>(2 * i), numbers[i], 2);
    system_->Write(0, driver_.queueAddress, static_cast<std::uint32_t>(2 * count), 2);
    system_->Write(0, driver_.commandAddress, driver_.command, 1);
    commandSent_ = true;
}

void Sega32xPwmMachine::SubmitRequestWord(std::uint16_t word) {
    if (!system_ || !driver_.requestWord) return;
    // the 68000 waits for the request word to be cleared (the slave clears it when it has taken the request)
    for (int guard = 0; guard < 200 && system_->M68kRead(driver_.requestWordOffset) != 0; ++guard) system_->RunTo(system_->Now() + 200);
    system_->M68kWrite(driver_.requestWordOffset, word);
    system_->M68kWrite(0x02, 0x0002);
    commandSent_ = true;
}

void Sega32xPwmMachine::SubmitEvent(const std::uint8_t* bytes, std::size_t count) {
    if (!system_ || count == 0 || count > 3) return;
    // the previous event must be taken first (the slave clears the high byte of the first communication word)
    for (int guard = 0; guard < 200 && (system_->M68kRead(0x20) & 0xFF00) != 0; ++guard) system_->RunTo(system_->Now() + 200);
    system_->M68kWrite(0x22, 0, 0xFF00);
    system_->M68kWrite(0x28, static_cast<std::uint16_t>(bytes[0] << 8), 0xFF00);
    if (count >= 2) system_->M68kWrite(0x28, bytes[1], 0x00FF);
    if (count >= 3) system_->M68kWrite(0x2A, static_cast<std::uint16_t>(bytes[2] << 8), 0xFF00);
    system_->M68kWrite(0x20, 0x0100, 0xFF00);
    system_->M68kWrite(0x02, 0x0002);
}

void Sega32xPwmMachine::LowPass(float* samples, std::size_t count, float state[2], float sampleRate, float cutoffHz) {
    const float k = std::tan(3.14159265358979f * cutoffHz / sampleRate);
    const float b = k / (1.0f + k), a = (1.0f - k) / (1.0f + k);
    for (std::size_t i = 0; i < count; ++i) {
        const float x = samples[i];
        state[0] = b * (x + state[1]) + a * state[0];
        state[1] = x;
        samples[i] = state[0];
    }
}

void Sega32xPwmMachine::LowPass2(float* samples, std::size_t count, float state[2], float sampleRate, float cutoffHz, float q) {
    const float w0 = 2.0f * 3.14159265358979f * cutoffHz / sampleRate;
    const float c = std::cos(w0), alpha = std::sin(w0) / (2.0f * q);
    const float a0 = 1.0f + alpha;
    const float b0 = (1.0f - c) / 2.0f / a0, b1 = (1.0f - c) / a0, a1 = -2.0f * c / a0, a2 = (1.0f - alpha) / a0;
    // transposed direct form II
    for (std::size_t i = 0; i < count; ++i) {
        const float x = samples[i];
        const float y = b0 * x + state[0];
        state[0] = b1 * x - a1 * y + state[1];
        state[1] = b0 * x - a2 * y;
        samples[i] = y;
    }
}

void Sega32xPwmMachine::Render(EmuDuration duration, LogicalChannelSink* sink) {
    if (sink != nullptr && !channelsRegistered_) {
        for (const LogicalChannel& channel : channels_) sink->RegisterChannel(channel.Id);
        channelsRegistered_ = true;
    }
    if (!system_) return;
    const double clock = Sega32xPwm::kNtscClockHz;
    const std::size_t frames = static_cast<std::size_t>(duration.ticks);
    if (frames == 0) return;
    const double startCycle = static_cast<double>(warmupEnd_) + static_cast<double>(renderedFrames_) * clock / kOutputRate;
    renderedFrames_ += frames;
    const std::uint64_t target = warmupEnd_ + static_cast<std::uint64_t>(static_cast<double>(renderedFrames_) * clock / kOutputRate);
    for (auto it = scriptQueue_.begin(); it != scriptQueue_.end();) {
        if (it->first < target) {
            system_->RunTo(it->first);
            for (const auto& word : driver_.script[it->second].words) system_->M68kWrite(word.offset, word.value);
            system_->M68kWrite(0x02, driver_.script[it->second].interrupt);   // 1 the master, 2 the slave
            it = scriptQueue_.erase(it);
        } else {
            ++it;
        }
    }
    system_->RunTo(target);
    left_.assign(frames, 0.0f);
    right_.assign(frames, 0.0f);
    system_->RenderPwm(startCycle, static_cast<double>(kOutputRate), left_.data(), right_.data(), frames);
    const float r = 1.0f - 2.0f * 3.14159265358979f * kDcBlockHz / static_cast<float>(kOutputRate);
    float* sides[2] = {left_.data(), right_.data()};
    for (int side = 0; side < 2; ++side) {
        float* samples = sides[side];
        if (idleLevelPending_) { dcPrevInput_[side] = samples[0]; dcPrevOutput_[side] = 0; lowPass_[side][0] = lowPass_[side][1] = samples[0]; }
        if (idleLevelPending_) {   // the filters start at the held level (a unity-gain low-pass settles to it)
            std::vector<float> settle(4096, samples[0]);
            sallenKey_[side][0] = sallenKey_[side][1] = 0;
            LowPass2(settle.data(), settle.size(), sallenKey_[side], static_cast<float>(kOutputRate), kSallenKeyHz, kSallenKeyQ);
        }
        LowPass(samples, frames, lowPass_[side], static_cast<float>(kOutputRate), kOutputFilterHz);
        LowPass2(samples, frames, sallenKey_[side], static_cast<float>(kOutputRate), kSallenKeyHz, kSallenKeyQ);
        if (idleLevelPending_) mixLowPass_[side][0] = mixLowPass_[side][1] = samples[0];
        LowPass(samples, frames, mixLowPass_[side], static_cast<float>(kOutputRate), kMixLowPassHz);
        for (std::size_t i = 0; i < frames; ++i) {
            const float out = samples[i] - dcPrevInput_[side] + r * dcPrevOutput_[side];
            dcPrevInput_[side] = samples[i];
            dcPrevOutput_[side] = out;
            samples[i] = out;
        }
        if (sink != nullptr) sink->WriteSamples(channels_[side].Id, samples, frames);
    }
    idleLevelPending_ = false;
}
