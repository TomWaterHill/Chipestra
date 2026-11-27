// license:BSD-3-Clause
// copyright-holders:R. Belmont
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Portions derived from MAME (src/mame/shared/segam1audio.cpp); see THIRD-PARTY-NOTICES.md.
#pragma once

#include "chipestra_abi.h"
#include "clock.h"
#include "devices/cpu/m68000/mame_derived_m68000_adapter.h"
#include "devices/sound/multipcm/multipcm_device_adapter.h"
#include "devices/sound/ym2612/ym2612_device_adapter.h"
#include "resource_resolver.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// The settings of a Model 1 game (read from the game definition XML's <driver> element).
//
//   <driver midi="AE 10 $S" midi2="9E $S 7F" requests="1 2" warmup_ms="400"/>
//
//   midi       the bytes the game's main processor sends the sound board to start a track: hexadecimal bytes, with $S standing for the track's command
//              value
//   midi1 ... midi7   other messages, for the tracks that name one with request="..." (the n-th entry of `requests` selects midiN)
//   requests   the numbers the tracks' request="..." can name (any distinct numbers; the position in the list is the message number)
//   warmup_ms  how long (in milliseconds of the sound board's time) the Machine lets the sound 68000 start up before it
//              sends the first message
struct Model1DriverConfig {
    std::vector<std::vector<std::string>> messages;   // [0] = `midi`, [n] = `midiN` (empty when not given)
    std::uint32_t warmupMilliseconds = 400;
};

bool ParseModel1DriverConfig(std::string_view driverXml, Model1DriverConfig& out, std::string* error);

// The Sega Model 1 sound board: a 68000 at 10MHz with 64KB of RAM, two MultiPCM sound chips (one each for music and sound
// effects, each with a banked 4MB sample ROM), a YM3438 that the driver uses as its timer, and a serial port (an 8251 at
// the MIDI rate of 31250 baud) through which the game's main processor sends the driver MIDI messages. The main processor
// is not emulated: the Machine sends the bytes a track needs, one every 320 microseconds, the way the main processor's
// serial port does.
//
// Time: one counter of 68000 cycles (10,000,000 a second). Each MultiPCM makes a sample every 224 cycles; the YM3438 runs
// at 8MHz (4 cycles of the 68000 are 5 of its clocks).
//
// Switching: a command sent after another one starts the sound board again from power-on (the driver has no command that
// silences all voices).
//
// LogicalChannels: MultiPCM 1 voices 1-28 (ids 0-27), MultiPCM 2 voices 1-28 (ids 28-55), YM3438 FM 1-6 and DAC (ids 56-62).
class Model1Machine final : public IMachine {
public:
    static constexpr std::uint32_t kCpuHz = 10'000'000;
    static constexpr std::uint32_t kPcmClockHz = 10'000'000;
    static constexpr std::uint32_t kYmClockHz = 8'000'000;
    static constexpr std::uint32_t kPcmVoices = MultiPcmDeviceAdapter::kVoiceCount;
    static constexpr std::uint32_t kPcm1Base = 0;
    static constexpr std::uint32_t kPcm2Base = kPcmVoices;
    static constexpr std::uint32_t kYmBase = 2 * kPcmVoices;
    static constexpr std::uint32_t kChannelCount = kYmBase + Ym2612DeviceAdapter::kChannelCount;
    // The original routes the MultiPCM voices to both speakers at 0.5 and the YM3438 at 0.30. Both are multiplied by 0.55 here: the
    // loudest of the songs of the two games (Virtua Fighter's first, a peak of 1.72 at the original's gains) then stays just below full scale
    // instead of being clipped by the output.
    static constexpr float kBoardGain = 0.55f;
    static constexpr float kPcmRoutingGain = 0.5f * kBoardGain;
    static constexpr float kYmRoutingGain = 0.30f * kBoardGain;
    // A voice at this total level (the register's largest attenuation, about -60 dB) is held by the sound driver as an idle voice.
    static constexpr std::uint32_t kIdleTotalLevel = 127;
    // One byte of the serial line (a start bit, eight data bits and a stop bit at 31250 baud).
    static constexpr std::uint64_t kByteCycles = kCpuHz / 31250 * 10;

    // `program`: the sound 68000's program as the board sees it (0x0 to 0x3FFFF, and the upper 128KB again at 0x80000),
    // in 68000 byte order. `pcm1`, `pcm2`: the sample ROMs of the two MultiPCMs (up to 4MB each).
    Model1Machine(std::vector<std::uint8_t> program, std::vector<std::uint8_t> pcm1, std::vector<std::uint8_t> pcm2,
                  Model1DriverConfig driver);
    ~Model1Machine() override;

    void Initialize() override;
    void Reset() override;
    void Shutdown() override;
    std::vector<LogicalChannel>& LogicalChannels() override { return channels_; }
    void Render(EmuDuration duration, LogicalChannelSink* sink) override;
    void SendCommand(const MusicCommand& command) override;
    bool AcceptsCommand(const MusicCommand& command) const override;

    // ---- test and diagnostic observation points ----
    MameDerivedM68000Adapter& CpuForTest() { return *cpu_; }
    MultiPcmDeviceAdapter& PcmForTest(std::uint32_t index) { return *pcm_[index]; }
    Ym2612DeviceAdapter& YmForTest() { return *ym_; }
    // Queues bytes for the serial port as the main processor would send them.
    void SendMidiBytesForTest(const std::vector<std::uint8_t>& bytes);
    // Queues one byte that arrives at the serial port at `cycle` (or as soon after as the line is free).
    void QueueMidiByteAtForTest(std::uint64_t cycle, std::uint8_t value) { midi_queue_.push_back({cycle, value}); }
    std::uint64_t CyclesForTest() const { return cycles_; }
    std::uint8_t SampleByteForTest(std::uint32_t chip, std::uint32_t address) const { return SampleByte(chip, address); }
    std::uint8_t RamForTest(std::uint32_t offset) const { return ram_[offset & 0xFFFF]; }
    std::uint64_t BytesReceivedForTest() const { return bytes_received_; }
    std::uint64_t OverrunsForTest() const { return overruns_; }
    // Called for every write to a MultiPCM (chip 0 or 1: offset 0 data, 1 voice, 2 register) with the cycle time.
    using PcmWriteObserver = std::function<void(std::uint64_t cycles, std::uint32_t chip, std::uint32_t offset, std::uint8_t value)>;
    void SetPcmWriteObserverForTest(PcmWriteObserver observer) { pcm_observer_ = std::move(observer); }
    // Called for every write to the YM3438 (offset 0-3) with the cycle time.
    using YmWriteObserver = std::function<void(std::uint64_t cycles, std::uint32_t offset, std::uint8_t value)>;
    void SetYmWriteObserverForTest(YmWriteObserver observer) { ym_write_observer_ = std::move(observer); }
    // Called for every byte the 68000 reads from the serial port's data register.
    using MidiReadObserver = std::function<void(std::uint64_t cycles, std::uint8_t value)>;
    void SetMidiReadObserverForTest(MidiReadObserver observer) { midi_observer_ = std::move(observer); }

private:
    // A one-pole DC blocker (about 30 Hz at the chip's stream rate). The YM3438 adds a constant to every channel even when it is
    // silent (the output stage's discontinuity); the board's analog output stage does not pass DC.
    struct DcBlocker {
        float prevInput = 0.0f;
        float prevOutput = 0.0f;
        void Process(float* samples, std::size_t count, float r) {
            for (std::size_t i = 0; i < count; ++i) {
                const float out = samples[i] - prevInput + r * prevOutput;
                prevInput = samples[i];
                prevOutput = out;
                samples[i] = out;
            }
        }
    };

    void ResetState();
    void RunTo(std::uint64_t targetCycles);
    std::uint64_t AccessCycles() const { return block_base_ + cpu_->CyclesConsumedThisCall(); }
    void SyncPcm(std::uint32_t chip, std::uint64_t cycles);
    void SyncYm(std::uint64_t cycles);
    void SyncAll(std::uint64_t cycles);
    void BlockYmDc();
    void UpdateInterruptLevel();
    void DeliverByte(std::uint8_t value);
    void BuildMessage(const MusicCommand& command, std::vector<std::uint8_t>& out) const;

    std::uint8_t Read(std::uint32_t address);
    void Write(std::uint32_t address, std::uint8_t value);
    std::uint8_t SampleByte(std::uint32_t chip, std::uint32_t address) const;
    void UpdateStereoRouting();

    std::vector<std::uint8_t> program_;
    std::vector<std::uint8_t> pcm_rom_[2];
    Model1DriverConfig driver_;
    std::array<std::uint8_t, 0x10000> ram_{};
    std::unique_ptr<MameDerivedM68000Adapter> cpu_;
    std::unique_ptr<MultiPcmDeviceAdapter> pcm_[2];
    std::unique_ptr<Ym2612DeviceAdapter> ym_;
    ClockConverter clock_converter_;

    std::uint32_t pcm_bank_[2] = {0, 0};
    // The serial port: the byte in the receive register, and the errors.
    std::uint8_t rx_data_ = 0;
    bool rx_ready_ = false;
    bool rx_overrun_ = false;
    struct MidiByte { std::uint64_t cycle; std::uint8_t value; };
    std::vector<MidiByte> midi_queue_;         // bytes waiting to be sent, in order, with the earliest cycle each can arrive
    std::size_t midi_position_ = 0;
    std::uint64_t next_byte_cycle_ = 0;        // when the next byte of midi_queue_ arrives
    std::uint64_t bytes_received_ = 0;
    std::uint64_t overruns_ = 0;

    std::uint64_t cycles_ = 0;                 // cycles of the 68000 run so far (the time base)
    std::uint64_t block_base_ = 0;             // cycles_ at the start of the running Execute() call
    std::uint64_t cumulative_ticks_ = 0;
    std::uint64_t pcm_cursor_[2] = {0, 0};     // cycles the MultiPCMs have been made up to
    std::uint64_t ym_cursor_ticks_ = 0;        // the YM3438's time in ticks of 1/40MHz made up to
    std::uint64_t ym_clocks_made_ = 0;
    bool command_sent_since_reset_ = false;
    bool keep_sound_ = false;                  // false while warming up: the chips are brought up to date but their samples are dropped

    std::vector<LogicalChannel> channels_;
    bool channels_registered_ = false;
    std::vector<std::vector<float>> pcm_scratch_[2];
    std::vector<std::vector<float>> ym_scratch_;
    std::array<DcBlocker, Ym2612DeviceAdapter::kChannelCount> dc_{};
    PcmWriteObserver pcm_observer_;
    YmWriteObserver ym_write_observer_;
    MidiReadObserver midi_observer_;
};

struct Model1MachineInputs {
    std::vector<std::uint8_t> program;
    std::vector<std::uint8_t> pcm1, pcm2;
    Model1DriverConfig driver;
};

// Builds the Machine's inputs from the resolved resources: "sndcpu0" (and "sndcpu1" when the game has a second program
// ROM) are the 68000's program ROMs as they come from the file (each 16-bit word byte-swapped), "pcm1_0"/"pcm1_1" and
// "pcm2_0"/"pcm2_1" the 2MB halves of the sample ROMs, and "driver_xml" the <driver> element. With no resources the
// inputs are empty (a Machine that cannot play).
Model1MachineInputs AssembleModel1MachineInputs(const MachineResources& resources);
