// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Model1Machine and MultiPcmDeviceAdapter native tests. The synthetic ROM programs below are hand-assembled 68000 code (no
// real ROM data). The real-ROM tests run only when CHIPESTRA_ROM_ROOT and CHIPESTRA_REFERENCE_DIR are set.

#include "../tests/definition_xml_locator.h"
#include "model1_machine_tests.h"

#include "default_machine_catalog.h"
#include "devices/sega/model1/model1_machine.h"
#include "devices/sound/multipcm/multipcm_device_adapter.h"
#include "machine_registry.h"
#include "mixer.h"
#include "resource_resolver.h"
#include "test_support.h"
#include "chipestra_abi.h"
#include "xml_definition_parser.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string_view>
#include <string>
#include <vector>

namespace {

const char* GetEnvVar(const char* name) {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)  // getenv is fine for a read-only test lookup
#endif
    return std::getenv(name);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
}

constexpr std::uint64_t kTicksPerSecond = kNativeRuntimeTickRateHz;

void Put16(std::vector<std::uint8_t>& rom, std::size_t at, std::uint16_t value) {
    rom[at] = static_cast<std::uint8_t>(value >> 8);
    rom[at + 1] = static_cast<std::uint8_t>(value);
}

void Put32(std::vector<std::uint8_t>& rom, std::size_t at, std::uint32_t value) {
    Put16(rom, at, static_cast<std::uint16_t>(value >> 16));
    Put16(rom, at + 2, static_cast<std::uint16_t>(value));
}

// The start of every synthetic program: the stack pointer and the start address; the serial port's interrupt (level 2)
// goes to 0x200.
std::vector<std::uint8_t> NewProgram() {
    std::vector<std::uint8_t> rom(0xC0000, 0x00);
    Put32(rom, 0x0, 0xF0FFFE);
    Put32(rom, 0x4, 0x100);
    Put32(rom, 0x68, 0x200);
    return rom;
}

// MOVE.B #value,address.L
void MoveByteToAbsolute(std::vector<std::uint8_t>& rom, std::size_t& at, std::uint8_t value, std::uint32_t address) {
    Put16(rom, at, 0x13FC);
    Put16(rom, at + 2, value);
    Put32(rom, at + 4, address);
    at += 8;
}

Model1DriverConfig DefaultConfig() {
    Model1DriverConfig config;
    config.messages.assign(8, {});
    config.messages[0] = {"AE", "10", "$S"};
    config.messages[2] = {"9E", "$S", "7F"};
    config.warmupMilliseconds = 5;
    return config;
}

std::unique_ptr<Model1Machine> MakeMachine(std::vector<std::uint8_t> program, std::vector<std::uint8_t> pcm1 = {}, std::vector<std::uint8_t> pcm2 = {},
                                           Model1DriverConfig config = DefaultConfig()) {
    auto machine = std::make_unique<Model1Machine>(std::move(program), std::move(pcm1), std::move(pcm2), std::move(config));
    machine->Initialize();
    return machine;
}

void RenderSeconds(IMachine& machine, LogicalChannelSink& sink, double seconds) {
    const std::uint64_t ticks = static_cast<std::uint64_t>(seconds * static_cast<double>(kTicksPerSecond));
    for (std::uint64_t done = 0; done < ticks; done += Mixer::kBlockSizeFrames) {
        sink.ClearAll();
        machine.Render(EmuDuration{Mixer::kBlockSizeFrames}, &sink);
    }
}

std::string DriverXml(const std::string& attributes) { return "<driver " + attributes + "/>"; }

std::string ReadWholeFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// The <driver .../> element of a game definition's text.
std::string ExtractDriverElement(const std::string& text) {
    const std::size_t start = text.find("<driver");
    if (start == std::string::npos) return {};
    const std::size_t end = text.find("/>", start);
    if (end == std::string::npos) return {};
    return text.substr(start, end + 2 - start);
}

struct RecordedWrite {
    double seconds = 0.0;
    std::uint32_t chip = 0;
    std::uint32_t value = 0;
};

std::vector<std::pair<double, std::uint8_t>> ReadMidiLog(const std::filesystem::path& path) {
    std::vector<std::pair<double, std::uint8_t>> bytes;
    std::ifstream in(path);
    double seconds = 0.0;
    std::string hex;
    while (in >> seconds >> hex) bytes.emplace_back(seconds, static_cast<std::uint8_t>(std::stoul(hex, nullptr, 16)));
    return bytes;
}

std::vector<RecordedWrite> ReadPcmLog(const std::filesystem::path& path) {
    std::vector<RecordedWrite> writes;
    std::ifstream in(path);
    double seconds = 0.0;
    unsigned chip = 0, offset = 0;
    std::string hex;
    while (in >> seconds >> chip >> offset >> hex) writes.push_back({seconds, chip, static_cast<std::uint32_t>(std::stoul(hex, nullptr, 16))});
    return writes;
}

}  // namespace

void Model1MachineRegisteredInDefaultCatalog() {
    std::printf("Model1MachineRegisteredInDefaultCatalog\n");
    const MachineDefinition* definition = DefaultMachineRegistry().Find(MachineId("SEGA_MODEL1"));
    Check(definition != nullptr, "SEGA_MODEL1 is registered");
    if (definition == nullptr) return;
    Check(definition->Category == MachineCategory::Arcade && definition->Manufacturer == "Sega" && definition->DisplayName == "Model 1",
          "Model 1 is an arcade board of Sega");
    Check(definition->Variants.size() == 1 && definition->Variants[0].Id.Value() == "MODEL1_BASE", "its one variant is MODEL1_BASE");
    std::unique_ptr<IMachine> machine = DefaultMachineRegistry().Create(MachineId("SEGA_MODEL1"), VariantId("MODEL1_BASE"), MachineResources{});
    Check(machine != nullptr, "an empty resource list builds a Machine that cannot play");
    if (!machine) return;
    machine->Initialize();
    Check(machine->LogicalChannels().size() == Model1Machine::kChannelCount, "the Machine has 28 + 28 MultiPCM voices and 7 YM3438 channels");
    Check(machine->LogicalChannels()[0].DeviceName == "YMW258-F #1" && machine->LogicalChannels()[28].DeviceName == "YMW258-F #2" &&
              machine->LogicalChannels()[56].DeviceName == "YM3438",
          "the channels are grouped by chip");
}

void Model1DriverConfigIsParsedAndChecked() {
    std::printf("Model1DriverConfigIsParsedAndChecked\n");
    Model1DriverConfig config;
    std::string error;
    Check(ParseModel1DriverConfig(DriverXml("midi=\"AE 10 $S\" midi2=\"0x9E $S 7F\" requests=\"1 2\" warmup_ms=\"250\""), config, &error),
          "a driver with a default message, a second message and a warm-up time parses");
    Check(config.messages.size() == 8 && config.messages[0].size() == 3 && config.messages[0][2] == "$S", "the default message is three tokens");
    Check(config.messages[2].size() == 3 && config.messages[2][0] == "0x9E" && config.messages[1].empty(), "midi2 is the second message and midi1 is empty");
    Check(config.warmupMilliseconds == 250, "warmup_ms is 250");
    Check(!ParseModel1DriverConfig(DriverXml("warmup_ms=\"250\""), config, &error), "the default message is required");
    Check(!ParseModel1DriverConfig(DriverXml("midi=\"AE 1G $S\""), config, &error), "a message byte must be hexadecimal");
    Check(!ParseModel1DriverConfig(DriverXml("midi=\"AE 123 $S\""), config, &error), "a message byte has at most two digits");
    Check(!ParseModel1DriverConfig(DriverXml("midi=\"AE 10 $S\" midi3=\"zz\""), config, &error), "every message is checked");
    Check(!ParseModel1DriverConfig(DriverXml("midi=\"AE 10 $S\" warmup_ms=\"20000\""), config, &error), "warmup_ms above 10000 is refused");
    Check(!ParseModel1DriverConfig("<notdriver midi=\"AE\"/>", config, &error), "the element must be <driver>");
    Check(!ParseModel1DriverConfig("<driver", config, &error), "a damaged element is refused");
}

namespace {

// One MultiPCM voice playing a constant 8-bit sample (0x40) at the output rate.
struct VoiceFixture {
    std::vector<std::uint8_t> rom = std::vector<std::uint8_t>(0x1000, 0x00);
    std::unique_ptr<MultiPcmDeviceAdapter> chip;

    VoiceFixture() {
        // Sample table entry 0: start 0x000100, 8-bit, loop at 0, 16 samples long, fastest attack, no decay.
        rom[0] = 0x00; rom[1] = 0x01; rom[2] = 0x00;
        rom[3] = 0x00; rom[4] = 0x00;
        rom[5] = 0xFF; rom[6] = 0xF0;
        rom[7] = 0x00;
        rom[8] = 0xF0; rom[9] = 0x00; rom[10] = 0xF0; rom[11] = 0x00;
        for (int i = 0; i < 16; ++i) rom[0x100 + i] = 0x40;
        chip = std::make_unique<MultiPcmDeviceAdapter>(10'000'000, [this](std::uint32_t address) { return address < rom.size() ? rom[address] : std::uint8_t{0}; });
    }

    void Write(std::uint8_t slot, std::uint8_t reg, std::uint8_t value) {
        chip->Write(1, slot);
        chip->Write(2, reg);
        chip->Write(0, value);
    }

    void StartVoice(std::uint8_t slot, std::uint8_t pan) {
        Write(slot, 0, static_cast<std::uint8_t>(pan << 4));
        Write(slot, 1, 0x00);        // sample 0
        Write(slot, 2, 0x00);
        Write(slot, 3, 0x10);        // octave 1, pitch 0: one sample per output sample
        Write(slot, 5, 0x01);        // level 0, no interpolation
        Write(slot, 4, 0x80);        // key on
    }
};

}  // namespace

void MultiPcmAdapterPlaysAnEightBitSampleAndFollowsItsRegisters() {
    std::printf("MultiPcmAdapterPlaysAnEightBitSampleAndFollowsItsRegisters\n");
    VoiceFixture fixture;
    std::vector<std::vector<float>> out;
    Check(fixture.chip->Render(224 * 4, out) == 4, "224 clocks make one output sample (four of them for 896 clocks)");
    bool silent = true;
    for (const auto& voice : out) for (float v : voice) if (v != 0.0f) silent = false;
    Check(silent, "a chip that has not been written makes no sound");
    Check(!fixture.chip->VoicePlaying(0), "no voice plays at first");

    fixture.StartVoice(0, 0);
    Check(fixture.chip->VoicePlaying(0), "key on starts voice 1");
    for (auto& voice : out) voice.clear();
    fixture.chip->Render(224 * 64, out);
    float peak = 0.0f;
    for (float v : out[0]) peak = std::max(peak, std::fabs(v));
    Check(peak > 0.9f && peak < 1.1f, "a full-scale 8-bit sample at level 0 and centre pan gives a stored value near 1.0");
    float others = 0.0f;
    for (std::size_t v = 1; v < out.size(); ++v) for (float x : out[v]) others = std::max(others, std::fabs(x));
    Check(others == 0.0f, "the other voices stay silent");
    Check(std::fabs(fixture.chip->VoiceGainLeft(0) - fixture.chip->VoiceGainRight(0)) < 1e-6f && fixture.chip->VoiceGainLeft(0) > 0.2f,
          "a centre pan gives equal left and right factors");

    // Key off: the sample has release register 0 (the slowest), so the voice goes on for a while and then fades.
    fixture.Write(0, 4, 0x00);
    Check(fixture.chip->VoicePlaying(0), "a key off with a release rate keeps the voice in its release");

    // Same input, same output.
    VoiceFixture other;
    other.StartVoice(0, 0);
    std::vector<std::vector<float>> a, b;
    VoiceFixture first;
    first.StartVoice(0, 0);
    first.chip->Render(224 * 200, a);
    other.chip->Render(224 * 200, b);
    Check(a[0] == b[0], "two chips with the same writes make the same samples");

    // Save and restore.
    VoiceFixture saved;
    saved.StartVoice(3, 0);
    std::vector<std::vector<float>> warm;
    saved.chip->Render(224 * 10, warm);
    const auto state = saved.chip->SaveState();
    std::vector<std::vector<float>> c, d;
    saved.chip->Render(224 * 50, c);
    saved.chip->RestoreState(*state);
    saved.chip->Render(224 * 50, d);
    Check(c[3] == d[3] && !c[3].empty(), "a restored chip repeats the samples it made after the save");
}

void MultiPcmAdapterPanTableFollowsTheChip() {
    std::printf("MultiPcmAdapterPanTableFollowsTheChip\n");
    std::vector<std::vector<float>> out;
    auto gains = [&](std::uint8_t pan, float& left, float& right) {
        VoiceFixture fixture;
        fixture.StartVoice(0, pan);
        std::vector<std::vector<float>> scratch;
        fixture.chip->Render(224 * 4, scratch);
        left = fixture.chip->VoiceGainLeft(0);
        right = fixture.chip->VoiceGainRight(0);
    };
    float left = 0, right = 0;
    gains(0x0, left, right);
    Check(left > 0.0f && left == right, "pan 0 is the centre");
    const float centre = left;
    gains(0x4, left, right);
    Check(right == centre && left < centre && left > 0.0f, "pan 4 lowers the left side");
    gains(0x7, left, right);
    Check(right == centre && left == 0.0f, "pan 7 is the far right");
    gains(0xC, left, right);
    Check(left == centre && right < centre && right > 0.0f, "pan 12 lowers the right side");
    gains(0xF, left, right);
    Check(left == centre && right < centre && right > left * 0.5f, "pan 15 lowers the right side the least");
    gains(0x9, left, right);
    Check(left == centre && right == 0.0f, "pan 9 is the far left");
    gains(0x8, left, right);
    Check(left == 0.0f && right == 0.0f, "pan 8 is silent");
}

void Model1MachineSerialPortDeliversTheBytesToTheProgram() {
    std::printf("Model1MachineSerialPortDeliversTheBytesToTheProgram\n");
    // The program: interrupts on, then wait. The level 2 handler stores each received byte at 0xF00010 on.
    std::vector<std::uint8_t> program = NewProgram();
    Put16(program, 0x100, 0x46FC); Put16(program, 0x102, 0x2000);                                            // MOVE #$2000,SR
    Put16(program, 0x104, 0x4BF9); Put32(program, 0x106, 0xF00010);                                          // LEA $F00010.L,A5
    Put16(program, 0x10A, 0x60FE);                                                                           // BRA.S *
    Put16(program, 0x200, 0x1AF9); Put32(program, 0x202, 0xC20001);                                          // MOVE.B $C20001.L,(A5)+
    Put16(program, 0x206, 0x4E73);                                                                           // RTE
    std::unique_ptr<Model1Machine> machine = MakeMachine(program);
    LogicalChannelSink sink;
    RenderSeconds(*machine, sink, 0.01);
    machine->SendMidiBytesForTest({0xAE, 0x10, 0x13, 0x81});
    RenderSeconds(*machine, sink, 0.02);
    Check(machine->BytesReceivedForTest() == 4, "four bytes arrive at the serial port");
    Check(machine->RamForTest(0x10) == 0xAE && machine->RamForTest(0x11) == 0x10 && machine->RamForTest(0x12) == 0x13 && machine->RamForTest(0x13) == 0x81,
          "the interrupt handler read the four bytes in order");
    Check(machine->OverrunsForTest() == 0, "the bytes come a byte time apart, so none is overrun");

    // Bytes sent at once still come one byte time (320 microseconds) apart.
    std::vector<std::uint64_t> times;
    std::unique_ptr<Model1Machine> second = MakeMachine(program);
    second->SetMidiReadObserverForTest([&](std::uint64_t cycles, std::uint8_t) { times.push_back(cycles); });
    second->SendMidiBytesForTest({1, 2, 3});
    RenderSeconds(*second, sink, 0.01);
    Check(times.size() == 3 && times[1] - times[0] >= Model1Machine::kByteCycles && times[2] - times[1] >= Model1Machine::kByteCycles,
          "the reads of three bytes sent at once are at least one byte time (3200 cycles) apart");

    // A program that never reads: the second byte overruns the first.
    std::vector<std::uint8_t> deaf = NewProgram();
    Put16(deaf, 0x100, 0x46FC); Put16(deaf, 0x102, 0x2700); Put16(deaf, 0x104, 0x60FE);   // MOVE #$2700,SR; BRA.S *
    std::unique_ptr<Model1Machine> overrun = MakeMachine(deaf);
    overrun->SendMidiBytesForTest({1, 2, 3});
    RenderSeconds(*overrun, sink, 0.01);
    Check(overrun->OverrunsForTest() == 2, "a program that does not read lets the next two bytes overrun");
}

void Model1MachineMultiPcmWritesAndSampleBanksReachTheChips() {
    std::printf("Model1MachineMultiPcmWritesAndSampleBanksReachTheChips\n");
    std::vector<std::uint8_t> program = NewProgram();
    std::size_t at = 0x100;
    MoveByteToAbsolute(program, at, 0x02, 0xC50001);   // the sample bank of chip 1 is 2
    MoveByteToAbsolute(program, at, 0x03, 0xC70001);   // the sample bank of chip 2 is 3
    // Voice 1 of chip 1: pan 0, sample 0, pitch 0 at octave 1, level 0 (set directly), key on.
    MoveByteToAbsolute(program, at, 0x00, 0xC40003);
    const std::uint8_t registers[][2] = {{0, 0x00}, {1, 0x00}, {2, 0x00}, {3, 0x10}, {5, 0x01}, {4, 0x80}};
    for (const auto& pair : registers) {
        MoveByteToAbsolute(program, at, pair[0], 0xC40005);
        MoveByteToAbsolute(program, at, pair[1], 0xC40001);
    }
    Put16(program, at, 0x60FE);   // BRA.S *

    std::vector<std::uint8_t> pcm1(0x400000, 0x00), pcm2(0x400000, 0x00);
    // The sample table of chip 1 (fixed first megabyte): entry 0 points at 0x000100, 16 samples of 0x40.
    pcm1[1] = 0x01;
    pcm1[5] = 0xFF; pcm1[6] = 0xF0;
    pcm1[8] = 0xF0; pcm1[10] = 0xF0;
    for (int i = 0; i < 16; ++i) pcm1[0x100 + i] = 0x40;
    pcm1[2 * 0x100000 + 5] = 0xAB;   // bank 2, address 0x100005
    pcm2[3 * 0x100000 + 7] = 0xCD;   // bank 3, address 0x100007
    pcm2[0x200000 + 1] = 0xEE;       // bank 2, not selected
    std::unique_ptr<Model1Machine> machine = MakeMachine(program, pcm1, pcm2);

    LogicalChannelSink sink;
    RenderSeconds(*machine, sink, 0.02);
    Check(machine->PcmForTest(0).VoicePlaying(0), "the writes of the program started voice 1 of the first MultiPCM");
    Check(!machine->PcmForTest(1).VoicePlaying(0), "the second MultiPCM was not written");
    Check(machine->SampleByteForTest(0, 0x100005) == 0xAB, "chip 1 reads bank 2 above the first megabyte");
    Check(machine->SampleByteForTest(1, 0x100007) == 0xCD, "chip 2 reads bank 3 above the first megabyte");
    Check(machine->SampleByteForTest(1, 0x100001) == 0x00, "chip 2 does not read the bank it is not set to");
    Check(machine->SampleByteForTest(0, 0x5) == pcm1[5], "the first megabyte is never banked");
    Check(machine->SampleByteForTest(0, 0x200000) == 0, "above two megabytes there is nothing");

    // The sound reaches the sink: chip 1 voice 1 is loud, the rest silent.
    sink.ClearAll();
    machine->Render(EmuDuration{Mixer::kBlockSizeFrames * 8}, &sink);
    float loud = 0.0f, quiet = 0.0f;
    const LogicalChannelBuffer& voice = sink.ChannelBuffer(machine->LogicalChannels()[0].Id);
    for (std::size_t i = 0; i < voice.Size(); ++i) loud = std::max(loud, std::fabs(voice.Data()[i]));
    for (std::uint32_t ch = 1; ch < Model1Machine::kYmBase; ++ch) {
        const LogicalChannelBuffer& buffer = sink.ChannelBuffer(machine->LogicalChannels()[ch].Id);
        for (std::size_t i = 0; i < buffer.Size(); ++i) quiet = std::max(quiet, std::fabs(buffer.Data()[i]));
    }
    Check(loud > 0.9f, "voice 1 of the first MultiPCM reaches its LogicalChannel");
    Check(quiet == 0.0f, "no other MultiPCM voice makes a sound");
    Check(machine->LogicalChannels()[0].Pan == 0.0f, "its pan follows the chip's pan (the centre)");
    Check(machine->LogicalChannels()[0].RoutingGain == Model1Machine::kPcmRoutingGain, "a MultiPCM voice goes to the speakers at the original's 0.5 times the board gain");
}

void Model1MachineSendsATrackAndRestartsForTheNextOne() {
    std::printf("Model1MachineSendsATrackAndRestartsForTheNextOne\n");
    // The program stores each byte it receives in the next byte of RAM from 0xF00010 (as in the serial port test).
    std::vector<std::uint8_t> program = NewProgram();
    Put16(program, 0x100, 0x46FC); Put16(program, 0x102, 0x2000);
    Put16(program, 0x104, 0x4BF9); Put32(program, 0x106, 0xF00010);
    Put16(program, 0x10A, 0x60FE);
    Put16(program, 0x200, 0x1AF9); Put32(program, 0x202, 0xC20001);
    Put16(program, 0x206, 0x4E73);
    std::unique_ptr<Model1Machine> machine = MakeMachine(program);
    LogicalChannelSink sink;
    auto send = [&](std::uint8_t value, std::uint8_t request, bool control = false) {
        MusicCommand command;
        command.Kind = MusicCommandKind::Raw;
        command.Value = value;
        command.Auxiliary = static_cast<std::uint8_t>((request << 4) | (control ? 0x80 : 0x00));
        machine->SendCommand(command);
    };
    send(0x13, 0);
    RenderSeconds(*machine, sink, 0.02);
    Check(machine->RamForTest(0x10) == 0xAE && machine->RamForTest(0x11) == 0x10 && machine->RamForTest(0x12) == 0x13,
          "a track with no request sends the default message with the command in place of $S");
    send(0x2A, 2);
    RenderSeconds(*machine, sink, 0.02);
    Check(machine->RamForTest(0x10) == 0x9E && machine->RamForTest(0x11) == 0x2A && machine->RamForTest(0x12) == 0x7F,
          "the second track sends the message its request names, after the sound board started again (the first message is gone)");
    Check(machine->BytesReceivedForTest() == 3, "the board that started again has received only the second message");
    send(0x00, 0, true);
    RenderSeconds(*machine, sink, 0.02);
    Check(machine->BytesReceivedForTest() == 0, "a control track (Music Stop) only starts the board again and sends nothing");
    MusicCommand wsg;
    wsg.Kind = MusicCommandKind::Wsg;
    Check(!machine->AcceptsCommand(wsg), "a WSG request is not accepted");
    MusicCommand raw;
    Check(machine->AcceptsCommand(raw), "a raw request is accepted");
}


void Model1MachineYmChannelsCarryNoDirectCurrent() {
    std::printf("Model1MachineYmChannelsCarryNoDirectCurrent\n");
    // The YM3438 adds a constant to the output of every channel even when nothing plays; the Machine removes it.
    std::unique_ptr<Model1Machine> machine = MakeMachine(NewProgram());
    LogicalChannelSink sink;
    RenderSeconds(*machine, sink, 0.3);
    float largest = 0.0f;
    std::size_t samples = 0;
    for (const LogicalChannel& channel : machine->LogicalChannels()) {
        if (channel.DeviceName != "YM3438") continue;
        const LogicalChannelBuffer& buffer = sink.ChannelBuffer(channel.Id);
        for (std::size_t i = 0; i < buffer.Size(); ++i) largest = std::max(largest, std::fabs(buffer.Data()[i]));
        samples += buffer.Size();
    }
    Check(samples > 0, "the YM3438 channels were rendered");
    Check(largest < 1.0e-4f, "a silent YM3438 channel has no direct current (it would be 0.0026 without the blocker)");
}
// Runs a program that starts voice 1 at total level `tlFirst` and voice 2 at `tlSecond` (a negative value leaves voice 2 off) on the first MultiPCM,
// renders, and lets the Mixer measure the block.
static std::unique_ptr<Model1Machine> MachineWithIdleVoiceTest(int tlFirst, int tlSecond, LogicalChannelSink& sink) {
    std::vector<std::uint8_t> program = NewProgram();
    std::size_t at = 0x100;
    for (int voice = 0; voice < 2; ++voice) {
        const int tl = voice == 0 ? tlFirst : tlSecond;
        if (tl < 0) continue;
        MoveByteToAbsolute(program, at, static_cast<std::uint8_t>(voice), 0xC40003);
        // pan 0, sample 0, pitch at octave 1, level written directly (bit 0), key on
        const std::uint8_t registers[][2] = {{0, 0x00}, {1, 0x00}, {2, 0x00}, {3, 0x10}, {5, static_cast<std::uint8_t>((tl << 1) | 1)}, {4, 0x80}};
        for (const auto& pair : registers) {
            MoveByteToAbsolute(program, at, pair[0], 0xC40005);
            MoveByteToAbsolute(program, at, pair[1], 0xC40001);
        }
    }
    Put16(program, at, 0x60FE);   // BRA.S *
    std::vector<std::uint8_t> pcm1(0x400000, 0x00), pcm2(0x400000, 0x00);
    pcm1[1] = 0x01;
    pcm1[5] = 0xFF; pcm1[6] = 0xF0;
    pcm1[8] = 0xF0; pcm1[10] = 0xF0;
    for (int i = 0; i < 16; ++i) pcm1[0x100 + i] = 0x40;
    std::unique_ptr<Model1Machine> machine = MakeMachine(program, pcm1, pcm2);
    RenderSeconds(*machine, sink, 0.3);
    sink.ClearAll();
    machine->Render(EmuDuration{Mixer::kBlockSizeFrames * 8}, &sink);
    Mixer mixer;
    std::vector<StereoFrame> out;
    mixer.MixBlock(sink, machine->LogicalChannels(), Mixer::kBlockSizeFrames * 8, out);
    return machine;
}

void Model1MachineIdleVoiceAloneIsJudgedAsTheEndOfTheSong() {
    std::printf("Model1MachineIdleVoiceAloneIsJudgedAsTheEndOfTheSong\n");
    LogicalChannelSink sink;
    std::unique_ptr<Model1Machine> machine = MachineWithIdleVoiceTest(127, -1, sink);
    Check(machine->PcmForTest(0).VoicePlaying(0) && machine->PcmForTest(0).VoiceTotalLevel(0) == 127, "(precondition) voice 1 plays at total level 127");
    const LogicalChannel& idle = machine->LogicalChannels()[0];
    Check(idle.ExcludeFromSongEnd, "a voice that stays at total level 127 is left out of the judgement of the song's end");
    Check(idle.Peak > 0.0f, "its sound and its meter are unchanged (it still sounds, about -60 dB)");
    float largest = 0.0f;
    for (const LogicalChannel& channel : machine->LogicalChannels()) largest = std::max(largest, channel.SourcePeak);
    Check(largest < 0.001f, "with only the idle voice sounding, no channel is above the silence threshold (0.001), so the song is judged to have ended");
    Check(idle.SourcePeak == 0.0f, "the idle voice itself reports a source peak of 0");
}

void Model1MachineVoiceBelowTotalLevel127KeepsTheSongGoing() {
    std::printf("Model1MachineVoiceBelowTotalLevel127KeepsTheSongGoing\n");
    LogicalChannelSink sink;
    std::unique_ptr<Model1Machine> machine = MachineWithIdleVoiceTest(127, 0x10, sink);
    const auto& channels = machine->LogicalChannels();
    Check(channels[0].ExcludeFromSongEnd && !channels[1].ExcludeFromSongEnd, "only the voice at total level 127 is left out; a voice at level 16 is not");
    Check(channels[1].SourcePeak > 0.001f, "the voice below 127 keeps its source peak above the threshold, so the song is not judged to have ended");
    LogicalChannelSink loudSink;
    std::unique_ptr<Model1Machine> alone = MachineWithIdleVoiceTest(126, -1, loudSink);
    Check(!alone->LogicalChannels()[0].ExcludeFromSongEnd, "a voice at level 126 is not left out");
}
void Model1ViaDefinitionXmlRealRomMatchesTheOriginal() {
    std::printf("Model1ViaDefinitionXmlRealRomMatchesTheOriginal\n");
    const char* romRootEnv = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRootEnv == nullptr || romRootEnv[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set - real-ROM bring-up not exercised\n");
        return;
    }
    const char* referenceEnv = GetEnvVar("CHIPESTRA_REFERENCE_DIR");
    for (const char* game : {"vf", "vr"}) {
        const auto xmlPath = FindDefinitionXml(std::string(game) + "_arcade.xml");
        Check(xmlPath.has_value(), "the game's XML is in the repository's xml folder (run from the repo root)");
        if (!xmlPath) continue;
        const std::string text = ReadWholeFile(*xmlPath);
        const XmlParseResult parsed = ParseGameDefinition(text);
        Check(parsed.Status == XmlParseStatus::Ok, "the game's XML parses");
        if (parsed.Status != XmlParseStatus::Ok) continue;
        const GameDefinition& definition = parsed.Definition;
        Check(definition.Machine == "SEGA_MODEL1" && definition.Variant == "MODEL1_BASE", "the XML names SEGA_MODEL1 / MODEL1_BASE");
        std::vector<RequiredResource> required;
        for (const RomResourceDefinition& rom : definition.Resources) {
            RequiredResource r;
            r.LogicalName = rom.Name;
            r.AcceptedNames = {rom.File};
            r.SizeBytes = rom.SizeBytes;
            r.Crc32 = rom.Crc32;
            r.Sha1 = rom.Sha1;
            if (!definition.Archive.empty()) r.Archive = definition.Archive;
            required.push_back(std::move(r));
        }
        const ResourceResolver resolver{std::filesystem::path(romRootEnv)};
        ResourceResolutionResult resolved = resolver.Resolve(required);
        Check(resolved.Ok, "every ROM of the XML resolves under CHIPESTRA_ROM_ROOT");
        if (!resolved.Ok) {
            for (const auto& f : resolved.Failures) std::printf("    resolve fail '%s' code %d (%s)\n", f.LogicalName.c_str(), static_cast<int>(f.Result), f.Detail.c_str());
            continue;
        }
        const std::string driverXml = ExtractDriverElement(text);
        resolved.Resources.Add(ResolvedResource("driver_xml", std::vector<std::uint8_t>(driverXml.begin(), driverXml.end())));

        // Every track of the XML sounds (the first seconds are not silent).
        std::size_t silent = 0, checked = 0;
        for (const TrackDefinition& track : definition.Tracks) {
            if (track.IsControlCommand) continue;
            if (++checked > 6) break;   // a few tracks: the whole list is checked with the diagnostic tool
            std::unique_ptr<IMachine> machine = DefaultMachineRegistry().Create(MachineId(definition.Machine), VariantId(definition.Variant), resolved.Resources);
            if (!machine) { ++silent; continue; }
            machine->Initialize();
            MusicCommand command;
            command.Kind = MusicCommandKind::Raw;
            command.Value = static_cast<std::uint8_t>(std::strtoul(track.Command.c_str(), nullptr, 0));
            command.Auxiliary = track.Auxiliary;
            machine->SendCommand(command);
            LogicalChannelSink sink;
            float peak = 0.0f;
            const std::uint64_t blocks = kTicksPerSecond * 3 / Mixer::kBlockSizeFrames;
            for (std::uint64_t b = 0; b < blocks; ++b) {
                sink.ClearAll();
                machine->Render(EmuDuration{Mixer::kBlockSizeFrames}, &sink);
                for (const LogicalChannel& channel : machine->LogicalChannels()) {
                    const LogicalChannelBuffer& buffer = sink.ChannelBuffer(channel.Id);
                    for (std::size_t i = 0; i < buffer.Size(); ++i) peak = std::max(peak, std::fabs(buffer.Data()[i] * channel.RoutingGain));
                }
            }
            if (peak < 0.01f) ++silent;
        }
        Check(silent == 0, "the first tracks of the XML make a sound in their first three seconds");

        // The driver is ready for messages (its interrupt mask is below the serial port's level) well within warmup_ms.
        {
            Model1MachineInputs readyInputs = AssembleModel1MachineInputs(resolved.Resources);
            const std::uint32_t warmupMilliseconds = readyInputs.driver.warmupMilliseconds;
            readyInputs.driver.warmupMilliseconds = 0;
            auto cold = std::make_unique<Model1Machine>(std::move(readyInputs.program), std::move(readyInputs.pcm1), std::move(readyInputs.pcm2), std::move(readyInputs.driver));
            cold->Initialize();
            LogicalChannelSink readySink;
            double readyAt = -1.0;
            for (int step = 0; step < 400 && readyAt < 0.0; ++step) {
                readySink.ClearAll();
                cold->Render(EmuDuration{kTicksPerSecond / 1000}, &readySink);   // 1 ms
                if (((cold->CpuForTest().Snapshot().sr >> 8) & 7) < 2) readyAt = static_cast<double>(step + 1) / 1000.0;
            }
            std::printf("  %s: the driver takes interrupts from the serial port %.0f ms after the power-on; warmup_ms is %u\n", game, readyAt * 1000.0, warmupMilliseconds);
            Check(readyAt > 0.0 && readyAt * 1000.0 * 2.0 <= static_cast<double>(warmupMilliseconds), "warmup_ms is at least twice the time the driver needs to start");
        }

        // The recording of the original's serial bytes and MultiPCM writes (CHIPESTRA_REFERENCE_DIR/model1/<game>/real_game).
        if (referenceEnv == nullptr || referenceEnv[0] == '\0') {
            std::printf("  SKIP: CHIPESTRA_REFERENCE_DIR is not set - the MultiPCM writes are not compared with the original's\n");
            continue;
        }
        const std::filesystem::path folder = std::filesystem::path(referenceEnv) / "model1" / game / "real_game";
        std::error_code ec;
        if (!std::filesystem::exists(folder / "midi.txt", ec) || !std::filesystem::exists(folder / "pcm_writes.txt", ec)) {
            std::printf("  SKIP: %s has no recording\n", folder.string().c_str());
            continue;
        }
        const auto midi = ReadMidiLog(folder / "midi.txt");
        const auto expected = ReadPcmLog(folder / "pcm_writes.txt");
        Check(!midi.empty() && !expected.empty(), "the recording has serial bytes and writes");

        Model1MachineInputs inputs = AssembleModel1MachineInputs(resolved.Resources);
        inputs.driver.warmupMilliseconds = 0;   // the recording starts at the power-on
        auto machine = std::make_unique<Model1Machine>(std::move(inputs.program), std::move(inputs.pcm1), std::move(inputs.pcm2), std::move(inputs.driver));
        std::vector<RecordedWrite> actual;
        machine->SetPcmWriteObserverForTest([&](std::uint64_t cycles, std::uint32_t chip, std::uint32_t, std::uint8_t value) {
            actual.push_back({static_cast<double>(cycles) / 1.0e7, chip, value});
        });
        machine->Initialize();
        for (const auto& entry : midi) machine->QueueMidiByteAtForTest(static_cast<std::uint64_t>(entry.first * 1.0e7 + 0.5), entry.second);
        const double last = expected.back().seconds + 0.05;
        LogicalChannelSink sink;
        RenderSeconds(*machine, sink, last);
        std::size_t compared = 0, wrongValues = 0;
        double worstSkew = 0.0;
        for (std::size_t i = 0; i < expected.size() && i < actual.size(); ++i, ++compared) {
            if (expected[i].chip != actual[i].chip || expected[i].value != actual[i].value) ++wrongValues;
            worstSkew = std::max(worstSkew, std::fabs(expected[i].seconds - actual[i].seconds));
        }
        std::printf("  %s: %zu recorded MultiPCM writes, %zu made, %zu values differ, worst time difference %.3f ms\n", game, expected.size(), actual.size(), wrongValues, worstSkew * 1000.0);
        Check(actual.size() >= expected.size(), "the Machine makes every write the original made in that time");
        Check(wrongValues == 0, "the chip and the value of every MultiPCM write are the original's");
        // A byte that arrives within a few microseconds of the driver's timer tick is taken at that tick or at the next one (the original's
        // serial port shifts a byte out in time with its own bit clock), so a note can start one tick (1.15 milliseconds) apart.
        const double allowed = std::string_view(game) == "vr" ? 0.0005 : 0.0025;
        Check(worstSkew < allowed, "every write is within the allowed time of the original's (half a millisecond; 2.5 milliseconds where a note can start a tick apart)");
    }
}
