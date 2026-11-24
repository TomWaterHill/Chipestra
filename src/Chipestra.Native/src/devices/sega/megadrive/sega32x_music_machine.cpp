// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill

#include "devices/sega/megadrive/sega32x_music_machine.h"

#include <utility>

Sega32xMusicMachine::Sega32xMusicMachine(std::unique_ptr<MegaDrive68kMachine> megaDrive, std::unique_ptr<Sega32xPwmMachine> pwm)
    : megaDrive_(std::move(megaDrive)), pwm_(std::move(pwm)) {
    md68k_ = static_cast<MegaDrive68kMachine*>(megaDrive_.get());
    Setup();
}

Sega32xMusicMachine::Sega32xMusicMachine(std::unique_ptr<MegaDriveMachine> megaDrive, std::unique_ptr<Sega32xPwmMachine> pwm)
    : megaDrive_(std::move(megaDrive)), pwm_(std::move(pwm)) {
    z80md_ = static_cast<MegaDriveMachine*>(megaDrive_.get());
    Setup();
}

void Sega32xMusicMachine::Setup() {
    for (const LogicalChannel& channel : megaDrive_->LogicalChannels()) channels_.push_back(channel);
    megaDriveChannelCount_ = channels_.size();
    for (const LogicalChannel& channel : pwm_->LogicalChannels()) channels_.push_back(channel);
    for (std::size_t i = 0; i < channels_.size(); ++i) channels_[i].Id = ChannelId(static_cast<std::uint32_t>(i));
    // with events from the Z80 the PWM synthesizer plays whatever the Z80 asks for, from the start
    pwmActive_ = pwm_->RunsFromTheStart();
}

void Sega32xMusicMachine::ForwardMailbox() {
    if (md68k_ == nullptr || !pwm_->UsesMailbox()) return;
    // The game's own code (once a frame, after the sound driver's call) takes the two sample numbers that the driver leaves in its work area, hands them
    // to the master processor and clears the words; the higher-addressed number goes first.
    const std::uint32_t at = pwm_->MailboxAddress();
    const std::uint8_t first = md68k_->RamForTest(at + 3);
    const std::uint8_t second = md68k_->RamForTest(at + 1);
    if (first == 0 && second == 0) return;
    for (std::uint32_t i = 0; i < 4; ++i) md68k_->PokeRamForTest(at + i, 0);
    std::uint8_t numbers[2];
    std::size_t count = 0;
    if (first != 0) numbers[count++] = first;
    if (second != 0) numbers[count++] = second;
    if (eventObserver_) for (std::size_t i = 0; i < count; ++i) eventObserver_(renderedTicks_, &numbers[i], 1);
    pwm_->SubmitSamples(numbers, count);
}

void Sega32xMusicMachine::ForwardRequestSync() {
    if (z80md_ == nullptr || !pwm_->UsesRequestWord() || pwm_->RequestFlagAddress() == 0) return;
    if (!megaDriveActive_) {   // a sound of the PWM unit alone (the start-up logo): the game's 68000 is not in its frame loop, the slave is not told the ends of frames
        syncedFrames_ = z80md_->HandlerRunsGiven();
        return;
    }
    // The game's handler, each time it adds one to the Z80's frame counter (twice a frame with frame_counter_extra): when the Z80 driver has taken a request
    // (it sets the flag byte) the handler clears the flag and tells the slave 0x0203; then it tells the slave 0x0200, the end of the frame.
    while (syncedFrames_ < z80md_->HandlerRunsGiven()) {
        ++syncedFrames_;
        const std::uint16_t flag = pwm_->RequestFlagAddress();
        if (z80md_->Z80RamForTest(flag) != 0) {
            z80md_->PokeZ80Ram(flag, 0);
            pwm_->SubmitRequestWord(0x0203);
        }
        pwm_->SubmitRequestWord(0x0200);
        ++frameEndsTold_;
    }
}

void Sega32xMusicMachine::ForwardEvents() {
    if (z80md_ == nullptr || !pwm_->UsesEventRing()) return;
    const std::uint16_t base = pwm_->EventRingAddress();
    const std::uint8_t write = z80md_->Z80RamForTest(base);
    while (eventReadIndex_ != write) {
        const std::uint8_t command = z80md_->Z80RamForTest(static_cast<std::uint16_t>(base + 1 + eventReadIndex_));
        // the lengths of the events, as the game's 68000 takes them apart: 0x7F has one parameter, 0x70 to 0x75 two, the commands below 0x70,
        // 0x80 to 0x88, 0x90 to 0x95 and 0xA0 to 0xA2 none; any other byte is not an event (the 68000's loop would hang)
        std::size_t length = 0;
        if (command == 0x7F) length = 2;
        else if (command >= 0x70 && command <= 0x75) length = 3;
        else if (command < 0x70 || (command >= 0x80 && command <= 0x88) || (command >= 0x90 && command <= 0x95) || (command >= 0xA0 && command <= 0xA2)) length = 1;
        const std::uint8_t available = static_cast<std::uint8_t>(write - eventReadIndex_);
        if (length == 0) { ++eventReadIndex_; continue; }
        if (available < length) break;   // the Z80 has not written the whole event yet
        std::uint8_t bytes[3] = {command, 0, 0};
        for (std::size_t i = 1; i < length; ++i) bytes[i] = z80md_->Z80RamForTest(static_cast<std::uint16_t>(base + 1 + static_cast<std::uint8_t>(eventReadIndex_ + i)));
        if (eventObserver_) eventObserver_(renderedTicks_, bytes, length);
        pwm_->SubmitEvent(bytes, length);
        eventReadIndex_ = static_cast<std::uint8_t>(eventReadIndex_ + length);
    }
}

void Sega32xMusicMachine::Initialize() {
    megaDrive_->Initialize();
    pwm_->Initialize();
}

void Sega32xMusicMachine::Reset() {
    megaDrive_->Reset();
    pwm_->Reset();
    pwmActive_ = pwm_->RunsFromTheStart();
    megaDriveActive_ = false;
}

void Sega32xMusicMachine::Shutdown() {
    megaDrive_->Shutdown();
    pwm_->Shutdown();
}

void Sega32xMusicMachine::PushChannelState() {
    // the user's settings travel down to the part Machines (the Mega Drive's reads the pan override)
    std::vector<LogicalChannel>& mdChannels = megaDrive_->LogicalChannels();
    for (std::size_t i = 0; i < mdChannels.size(); ++i) {
        mdChannels[i].PanOverridden = channels_[i].PanOverridden;
        if (channels_[i].PanOverridden) mdChannels[i].Pan = channels_[i].Pan;
    }
}

void Sega32xMusicMachine::PullChannelState() {
    // what the part Machines work out for themselves (the Mega Drive's pan and routing gain follow the driver's state)
    const std::vector<LogicalChannel>& mdChannels = megaDrive_->LogicalChannels();
    for (std::size_t i = 0; i < mdChannels.size(); ++i) {
        channels_[i].RoutingGain = mdChannels[i].RoutingGain;
        if (!channels_[i].PanOverridden) channels_[i].Pan = mdChannels[i].Pan;
    }
}

void Sega32xMusicMachine::SendCommand(const MusicCommand& command) {
    const bool control = command.Kind == MusicCommandKind::Stop || (command.Auxiliary & 0x80) != 0;
    if (control) {
        MusicCommand stop;
        stop.Kind = MusicCommandKind::Stop;
        megaDrive_->SendCommand(stop);
        MusicCommand silence = command;
        silence.Auxiliary = 0x80;
        if (pwmActive_) pwm_->SendCommand(silence);
        pwmActive_ = pwm_->RunsFromTheStart();   // the synthesizer of a Z80 driver's events, or of a 68000 driver's mailbox, always runs
        if (z80md_ != nullptr && pwm_->UsesEventRing()) eventReadIndex_ = z80md_->Z80RamForTest(pwm_->EventRingAddress());
        megaDriveActive_ = false;
        return;
    }
    const bool layer = (command.Auxiliary & kLayerBit) != 0;
    if (z80md_ != nullptr && (pwm_->UsesEventRing() || pwm_->UsesRequestWord()) && (command.Auxiliary & kPwmUnitBit) == 0) {
        // one Z80 driver makes everything: start the slave afresh (its voices of an earlier request would go on), then the request
        MusicCommand silence;
        silence.Kind = MusicCommandKind::Raw;
        silence.Auxiliary = 0x80;
        if (!layer) pwm_->SendCommand(silence);
        MusicCommand request = command;
        request.Auxiliary = static_cast<std::uint8_t>(command.Auxiliary & 0x9F);
        megaDrive_->SendCommand(request);
        megaDriveActive_ = true;
        if (pwm_->UsesRequestWord()) pwm_->SubmitRequest(request.Value);   // the game's 68000 tells the slave the request too
        else if (!layer) eventReadIndex_ = static_cast<std::uint8_t>(z80md_->Z80RamForTest(pwm_->EventRingAddress()));
        return;
    }
    if ((command.Auxiliary & kPwmUnitBit) != 0) {
        if (!layer && megaDriveActive_) {
            MusicCommand stop;
            stop.Kind = MusicCommandKind::Stop;
            megaDrive_->SendCommand(stop);
            megaDriveActive_ = false;
        }
        MusicCommand request = command;
        request.Auxiliary = static_cast<std::uint8_t>(command.Auxiliary & 0x1F);
        pwm_->SendCommand(request);
        pwmActive_ = true;
    } else {
        if (!layer && pwmActive_) {
            MusicCommand silence;
            silence.Kind = MusicCommandKind::Raw;
            silence.Auxiliary = 0x80;
            pwm_->SendCommand(silence);
            pwmActive_ = pwm_->UsesMailbox();
        }
        MusicCommand request = command;
        request.Auxiliary = static_cast<std::uint8_t>(command.Auxiliary & 0x9F);
        megaDrive_->SendCommand(request);
        megaDriveActive_ = true;
    }
}

void Sega32xMusicMachine::Render(EmuDuration duration, LogicalChannelSink* sink) {
    if (sink != nullptr && !registered_) {
        for (const LogicalChannel& channel : channels_) sink->RegisterChannel(channel.Id);
        registered_ = true;
    }
    PushChannelState();
    megaDriveSink_.ClearAll();
    pwmSink_.ClearAll();
    megaDrive_->Render(duration, sink != nullptr ? &megaDriveSink_ : nullptr);
    ForwardEvents();
    ForwardMailbox();
    ForwardRequestSync();
    renderedTicks_ += duration.ticks;
    if (pwmActive_) pwm_->Render(duration, sink != nullptr ? &pwmSink_ : nullptr);
    PullChannelState();
    if (sink == nullptr) return;
    for (std::size_t i = 0; i < megaDriveChannelCount_; ++i) {
        const LogicalChannelBuffer& buffer = megaDriveSink_.ChannelBuffer(megaDrive_->LogicalChannels()[i].Id);
        if (buffer.Size() != 0) sink->WriteSamples(channels_[i].Id, buffer.Data(), buffer.Size());
    }
    const std::size_t pwmCount = channels_.size() - megaDriveChannelCount_;
    for (std::size_t i = 0; i < pwmCount; ++i) {
        const ChannelId id = channels_[megaDriveChannelCount_ + i].Id;
        if (pwmActive_) {
            const LogicalChannelBuffer& buffer = pwmSink_.ChannelBuffer(pwm_->LogicalChannels()[i].Id);
            if (buffer.Size() != 0) sink->WriteSamples(id, buffer.Data(), buffer.Size());
        } else {
            const std::vector<float> silence(static_cast<std::size_t>(duration.ticks), 0.0f);
            sink->WriteSamples(id, silence.data(), silence.size());
        }
    }
}
