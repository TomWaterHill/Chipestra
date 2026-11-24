// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "chipestra_abi.h"
#include "devices/sega/megadrive/megadrive68k_machine.h"
#include "devices/sega/megadrive/megadrive_machine.h"
#include "devices/sega/megadrive/sega32x_pwm_machine.h"

#include <functional>
#include <memory>
#include <vector>

// A Super 32X game whose music and effects are made on the Mega Drive side (the sound driver on the 68000: YM2612 and PSG)
// and whose sampled sounds the cartridge's SH-2 programs play through the 32X's PWM unit. The Machine is the two Machines
// that already do each of these (MegaDrive68kMachine and Sega32xPwmMachine), run side by side on the same cartridge, with
// the channels of both in one list: the Mega Drive's channels first, then the two PWM channels.
//
// One <driver> element carries the settings of both (see 4d and 4e of docs/xml-format.md). A track names the unit that plays it
// with `unit="pwm"` (the track's Auxiliary bit 6); without it the track is a request of the Mega Drive's sound driver.
//
// Switching: a request for one unit silences the other (a jukebox plays one thing at a time). Bit 5 of the Auxiliary byte
// (not set by any XML attribute) asks for the request to be added to what the other unit is playing, as in the game.
class Sega32xMusicMachine final : public IMachine {
public:
    static constexpr std::uint8_t kPwmUnitBit = 0x40;
    static constexpr std::uint8_t kLayerBit = 0x20;

    Sega32xMusicMachine(std::unique_ptr<MegaDrive68kMachine> megaDrive, std::unique_ptr<Sega32xPwmMachine> pwm);
    // The variant whose Mega Drive side is a Z80 sound driver (MegaDriveMachine) that makes the music for both units: besides the YM2612 and
    // the PSG it writes events for the PWM synthesizer into a ring buffer in its RAM (the game's 68000 passes them on to the slave SH-2;
    // here the Machine does, see <driver sh2_event_ring>).
    Sega32xMusicMachine(std::unique_ptr<MegaDriveMachine> megaDrive, std::unique_ptr<Sega32xPwmMachine> pwm);

    void Initialize() override;
    void Reset() override;
    void Shutdown() override;
    std::vector<LogicalChannel>& LogicalChannels() override { return channels_; }
    MegaDrive68kMachine& MegaDriveForTest() { return *md68k_; }
    MegaDriveMachine& Z80MegaDriveForTest() { return *z80md_; }
    bool HasZ80DriverForTest() const { return z80md_ != nullptr; }
    Sega32xPwmMachine& PwmForTest() { return *pwm_; }
    void Render(EmuDuration duration, LogicalChannelSink* sink) override;
    // Test-only: called for every event passed to the slave (the time in canonical ticks since the start, the bytes).
    using EventObserver = std::function<void(std::uint64_t ticks, const std::uint8_t* bytes, std::size_t count)>;
    void SetEventObserverForTest(EventObserver observer) { eventObserver_ = std::move(observer); }
    void SendCommand(const MusicCommand& command) override;
    bool AcceptsCommand(const MusicCommand& command) const override { return command.Kind != MusicCommandKind::Wsg; }

    // How many frame-end words (0x0200) the imitated vertical-blank handler has told the slave so far.
    std::uint64_t FrameEndsToldForTest() const { return frameEndsTold_; }

private:
    void PushChannelState();
    void PullChannelState();
    void ForwardEvents();
    void ForwardMailbox();
    void ForwardRequestSync();
    void Setup();

    std::unique_ptr<IMachine> megaDrive_;
    MegaDrive68kMachine* md68k_ = nullptr;
    MegaDriveMachine* z80md_ = nullptr;
    std::uint8_t eventReadIndex_ = 0;
    std::uint64_t frameEndsTold_ = 0;  // the 0x0200 words told to the slave
    std::uint64_t syncedFrames_ = 0;   // the frames for which the game's vertical-blank handler has been imitated (sh2_request_flag)
    std::uint64_t renderedTicks_ = 0;
    EventObserver eventObserver_;
    std::unique_ptr<Sega32xPwmMachine> pwm_;
    std::vector<LogicalChannel> channels_;
    std::size_t megaDriveChannelCount_ = 0;
    LogicalChannelSink megaDriveSink_, pwmSink_;
    bool registered_ = false;
    bool pwmActive_ = false;
    bool megaDriveActive_ = false;
};

