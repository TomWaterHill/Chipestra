// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "chipestra_abi.h"
#include "resource_resolver.h"
#include "sega32x_system.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The settings of a Super 32X game whose sampled sounds are played by an SH-2 through the PWM unit (read from the game
// definition XML's <driver> element).
//
//   <driver sh2_queue="0x06008774" sh2_command_address="0x20004005" sh2_command="4" sh2_warmup_ms="300" cart_crc32="b5de9626"/>
//
//   sh2_queue            the SH-2 address of the game's queue of sound requests: a word with the number of bytes that follow,
//                        then one word for each sound (the game's own sample number)
//   sh2_command_address  the SH-2 address of the byte through which the game's master tells the slave what to do next
//   sh2_command          the value that makes the slave start the sounds in the queue
//   sh2_warmup_ms        the time the two SH-2 run after the start before the first request (the programs set the PWM unit up
//                        and go idle); the state is rebuilt this way at every switch
//   sh2_comm_word        instead of the three attributes above: the offset (0x00 to 0x2E) of the communication word the 68000 writes to ask
//                        the master processor for a sound; the word is sh2_comm_base (default 0) or the track's command
//   sh2_request_word     instead of the attributes above: the offset (0x20 to 0x2E, from the adapter registers at 0xA15100) of the communication word in which the game's 68000 hands
//                        every sound request that it gives its Z80 sound driver to the slave processor too (the word is 0x0100 plus the request's number;
//                        the slave's command interrupt follows); the slave plays the PWM sounds that go with the requests
//   sh2_request_flag     (with sh2_request_word) the Z80 RAM address of the byte that the Z80 driver sets when it has taken a request. The game's
//                        68000 looks at it and clears it in its vertical-blank handler and tells the slave: 0x0203 when it was set, then 0x0200
//                        (a frame's end); the slave starts the requested sounds only after the 0x0203
//   sh2_sample_table     instead of the attributes above: the file offset of a table of the game's sampled sounds, 8 bytes for each number: the SH-2 address of the sound's
//                        descriptor (a long) and the priority (a long whose upper word is the one the 68000 passes). The track's command is the number. Playing
//                        one does what the game's 68000 does: the descriptor address goes into the communication words 0x24/0x26, the priority into 0x28, the
//                        command byte into word 0x20 and the slave's command interrupt is raised (sh2_sample_command, default 0x3C). Stopping the sound (and a
//                        switch to another) is the command sh2_sample_stop (default 0x40).
//   sh2_start_words      up to four words (hexadecimal, separated by spaces) that the 68000 writes into the communication words 0x20, 0x22, ... after it has cleared the
//                        BIOS's words and before the programs run on: a game whose master program waits for a word of the game's own to start
//   sh2_halt_master      "true" stops the master SH-2 after that start-up (for a game whose master program only idles once
//                        the sounds are requested by this Machine); default "false"
//   sh2_mailbox          (with sh2_queue) the 68000 RAM address of two words in which the game's sound driver leaves the numbers of
//                        sampled sounds that go with its music (the low byte of each word; 0 is no sound). The game's own code takes the
//                        two numbers once a frame and hands them to the master processor; the combined Machine does the same
struct Sega32xPwmDriverConfig {
    std::uint32_t queueAddress = 0;
    std::uint32_t commandAddress = 0;
    std::uint8_t command = 0;
    std::uint32_t warmupMilliseconds = 300;
    bool haltMaster = false;
    bool mailbox = false;                // the music's sampled sounds come from two words in the 68000's RAM (see `sh2_mailbox`)
    std::uint32_t mailboxAddress = 0;
    bool eventRing = false;              // the sound events come from the Z80's ring buffer (see Sega32xMusicMachine)
    std::uint16_t eventRingAddress = 0;  // the Z80 RAM address of the ring buffer's write index; the events follow at +1
    std::uint16_t requestFlag = 0;       // the Z80 RAM byte that the Z80 driver sets when it takes a request (see `sh2_request_flag`)
    bool requestWord = false;            // every request of the Z80 driver is also told to the slave (see `sh2_request_word`)
    std::uint32_t requestWordOffset = 0;
    std::vector<std::uint16_t> startWords;
    bool sampleTable = false;            // the sounds are the entries of a table in the cartridge (see `sh2_sample_table`)
    std::uint32_t sampleTableOffset = 0;
    std::uint8_t sampleCommand = 0x3C;
    std::uint8_t sampleStop = 0x40;
    bool commWord = false;               // the request is a communication word instead of the queue
    // `sh2_script`: the start-up sound of the game (the logo) is a series of messages of the 68000 to the slave processor
    struct ScriptWord { std::uint32_t offset = 0; std::uint16_t value = 0; };
    struct ScriptStep { std::uint32_t frame = 0; std::uint16_t interrupt = 2; std::vector<ScriptWord> words; };
    std::vector<ScriptStep> script;
    std::uint32_t commWordOffset = 0;
    std::uint16_t commBase = 0;
    std::optional<std::uint32_t> cartridgeCrc32;
};

bool ParseSega32xPwmDriverConfig(std::string_view driverXml, Sega32xPwmDriverConfig& out, std::string* error);

struct Sega32xPwmMachineInputs {
    std::vector<std::uint8_t> cartridge;
    Sega32xPwmDriverConfig driver;
};
Sega32xPwmMachineInputs AssembleSega32xPwmMachineInputs(const MachineResources& resources);

// The sampled sounds of a Super 32X game: the cartridge's two SH-2 programs run (without the 68000 and without the video part)
// and the PWM unit's output is the sound. The 68000's part of the start-up is the Machine's: it clears the two words the BIOS
// leaves for the 68000 to acknowledge. A request is the game's own sample number (the number the master would put into the
// slave's queue). The Mega Drive side's music (the YM2612 and the PSG) is not part of this Machine.
class Sega32xPwmMachine : public IMachine {
public:
    Sega32xPwmMachine(std::vector<std::uint8_t> cartridge, Sega32xPwmDriverConfig driver);

    void Initialize() override;
    void Reset() override;
    void Shutdown() override {}
    std::vector<LogicalChannel>& LogicalChannels() override { return channels_; }
    void Render(EmuDuration duration, LogicalChannelSink* sink) override;
    void SendCommand(const MusicCommand& command) override;
    bool AcceptsCommand(const MusicCommand& command) const override { return command.Kind != MusicCommandKind::Wsg; }

    Sega32xSystem& SystemForTest() { return *system_; }
    // Hands one sound event of the game's 68000 (1 to 3 bytes: the command, then its parameters) to the slave processor the way the 68000
    // does: the bytes go into the communication words, the first word is set to "busy" and the slave's command interrupt is raised.
    // The slave clears the busy byte when it has taken the event; a following event waits for that.
    void SubmitEvent(const std::uint8_t* bytes, std::size_t count);
    // Puts sampled sounds (the game's own sample numbers, 1 to 4 of them) into the slave's queue without disturbing the voices that already play,
    // and starts them; waits for the slave to take the earlier queue first.
    void SubmitSamples(const std::uint8_t* numbers, std::size_t count);
    bool UsesMailbox() const { return driver_.mailbox; }
    std::uint32_t MailboxAddress() const { return driver_.mailboxAddress; }
    // Tells the slave a sound request of the game's 68000 (the number it gives its Z80 sound driver) the way the 68000 does: 0x0100 plus the
    // number goes into the request word and the slave's command interrupt is raised; waits for the slave to take an earlier one first.
    void SubmitRequest(std::uint8_t number) { SubmitRequestWord(static_cast<std::uint16_t>(0x0100 | number)); }
    void SubmitRequestWord(std::uint16_t word);
    std::uint16_t RequestFlagAddress() const { return driver_.requestFlag; }
    bool UsesRequestWord() const { return driver_.requestWord; }
    // The slave's synthesizer runs from the start (the sounds come with the Z80 driver's events or with the 68000's mailbox or requests).
    bool RunsFromTheStart() const { return driver_.eventRing || driver_.mailbox || driver_.requestWord; }
    bool UsesEventRing() const { return driver_.eventRing; }
    std::uint16_t EventRingAddress() const { return driver_.eventRingAddress; }

    // The output rate of the two channels (the canonical rate: nothing is resampled).
    static constexpr std::uint32_t kOutputRate = 192000;
    // The level of the PWM sound relative to the Mega Drive's sound: a value chosen from the board's circuit (a ratio of 0.84 at equal
    // swings) and from level measurements, not an exact circuit value.
    static constexpr float kRoutingGain = 1.0f;
    // The 32X board's input network of the PWM sound (service manual, schematic 7-4): from each PWM output 8.2 kOhm in series, 3 kOhm and
    // 3300 pF to ground, then 0.1 uF into the sound mixer IC. Without the mixer's input impedance the network is a one-pole low-pass at
    // 1 / (2 pi (8.2k // 3k) 3300 pF) = 21.96 kHz.
    static constexpr float kOutputFilterHz = 21960.0f;
    // The sound mixer IC (315-5684) around it (the IC's internal resistors from the Genesis 2 VA3 service manual, p.13; the capacitors on
    // its pins from the 32X service manual, schematic 7-4): the FM input path of each channel is a unity-gain Sallen-Key low-pass with
    // two 10 kOhm resistors, a 2200 pF capacitor between LFB and FM LP OUT and a 1000 pF capacitor on LPF, then an inverting mixer
    // (27 kOhm feedback) and a first-order low-pass of 27 kOhm with 680 pF (LP IN to MIX LP OUT) that everything passes.
    static constexpr float kSallenKeyHz = 10730.0f;         // 1 / (2 pi 10k sqrt(2200 pF 1000 pF))
    static constexpr float kSallenKeyQ = 0.7416f;           // sqrt(2200 pF / 1000 pF) / 2
    static constexpr float kMixLowPassHz = 8668.6f;         // 1 / (2 pi 27k 680 pF)
    // The gain of a PWM channel relative to the Mega Drive's sound path (ROM IN, 47 kOhm into the mixer) at equal full-scale swings:
    // the input network's 3k / (8.2k + 3k) = 0.268, times 47k over the FM input resistor into the mixer (15 kOhm for both channels).
    static constexpr float kCircuitGainLeft = 0.2679f * 47.0f / 15.0f;
    static constexpr float kCircuitGainRight = 0.2679f * 47.0f / 15.0f;
    // A second-order low-pass (bilinear transform, pre-warped at the cut-off; the gain at the cut-off is Q).
    static void LowPass2(float* samples, std::size_t count, float state[2], float sampleRate, float cutoffHz, float q);
    // Runs the one-pole low-pass over `count` samples at `sampleRate` (state carries over between calls).
    // `state` holds the last output and the last input (bilinear transform, so the gain at the cut-off is exactly -3 dB).
    static void LowPass(float* samples, std::size_t count, float state[2], float sampleRate, float cutoffHz);

private:
    void ColdStart();

    std::vector<std::uint8_t> cartridge_;
    Sega32xPwmDriverConfig driver_;
    std::unique_ptr<Sega32xSystem> system_;
    std::vector<LogicalChannel> channels_;
    bool channelsRegistered_ = false;
    bool commandSent_ = false;
    std::vector<std::pair<std::uint64_t, std::size_t>> scriptQueue_;   // the steps of the script that are still to be told: (cycle, step)
    std::uint64_t renderedFrames_ = 0;
    std::uint64_t warmupEnd_ = 0;
    bool idleLevelPending_ = true;
    float dcPrevInput_[2] = {0, 0}, dcPrevOutput_[2] = {0, 0};
    float lowPass_[2][2] = {{0, 0}, {0, 0}};
    float sallenKey_[2][2] = {{0, 0}, {0, 0}};
    float mixLowPass_[2][2] = {{0, 0}, {0, 0}};
    std::vector<float> left_, right_;
};
