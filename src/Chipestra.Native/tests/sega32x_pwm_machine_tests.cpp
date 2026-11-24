// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill

#include "sega32x_pwm_machine_tests.h"

#include "default_machine_catalog.h"
#include "definition_xml_locator.h"
#include "devices/nintendo/nes/nes_driver_config.h"
#include "devices/sega/megadrive/megadrive_machine.h"
#include "devices/sega/megadrive/sega32x_pwm_machine.h"
#include "devices/sega/megadrive/sega32x_music_machine.h"
#include "devices/sega/megadrive/sega32x_system.h"
#include "machine_registry.h"
#include "resource_resolver.h"
#include "test_support.h"
#include "xml_definition_parser.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

const char* GetEnvVar(const char* name) { return std::getenv(name); }

std::string DriverXml(const std::string& attributes) { return "<driver " + attributes + "/>"; }

struct Output {
    std::vector<float> left, right;
    double peak = 0;
    std::uint64_t digest = 1469598103934665603ull;
};

void Collect(IMachine& machine, LogicalChannelSink& sink, std::uint64_t frames, std::uint64_t block, Output& out) {
    while (frames > 0) {
        const std::uint64_t n = std::min(frames, block);
        sink.ClearAll();
        machine.Render(EmuDuration(n), &sink);
        const LogicalChannelBuffer& l = sink.ChannelBuffer(ChannelId(0));
        const LogicalChannelBuffer& r = sink.ChannelBuffer(ChannelId(1));
        for (std::size_t i = 0; i < l.Size(); ++i) {
            const float lv = l[i], rv = r[i];
            out.left.push_back(lv);
            out.right.push_back(rv);
            out.peak = std::max<double>(out.peak, std::fabs(lv));
            std::uint32_t bits;
            std::memcpy(&bits, &lv, 4);
            out.digest = (out.digest ^ bits) * 1099511628211ull;
        }
        frames -= n;
    }
}

}  // namespace

void Sega32xPwmOutputFilterIsTheBoardsInputNetwork() {
    std::printf("Sega32xPwmOutputFilterIsTheBoardsInputNetwork\n");
    const float fs = 192000.0f;
    auto gainAt = [&](float hz) {
        std::vector<float> x(19200);
        for (std::size_t i = 0; i < x.size(); ++i) x[i] = std::sin(2.0f * 3.14159265358979f * hz * static_cast<float>(i) / fs);
        float state[2] = {0, 0};
        Sega32xPwmMachine::LowPass(x.data(), x.size(), state, fs, Sega32xPwmMachine::kOutputFilterHz);
        double peak = 0;
        for (std::size_t i = 9600; i < x.size(); ++i) peak = std::max(peak, static_cast<double>(std::fabs(x[i])));
        return peak;
    };
    Check(std::fabs(Sega32xPwmMachine::kOutputFilterHz - 1.0f / (2.0f * 3.14159265358979f * (8200.0f * 3000.0f / 11200.0f) * 3.3e-9f)) < 20.0f, "the cut-off is 1 / (2 pi (8.2k // 3k) 3300 pF)");
    Check(std::fabs(gainAt(Sega32xPwmMachine::kOutputFilterHz) - 0.7071) < 0.01, "the gain at the cut-off is -3 dB");
    Check(gainAt(1000.0f) > 0.995, "a 1 kHz tone passes");
    Check(std::fabs(gainAt(0.5f * Sega32xPwmMachine::kOutputFilterHz) - 0.894) < 0.02 && std::fabs(gainAt(2.0f * Sega32xPwmMachine::kOutputFilterHz) - 0.447) < 0.06,
          "half the cut-off passes at 0.894 and twice the cut-off at about 0.45 (one pole; the bilinear transform bends it a little near the Nyquist rate)");
    float dc[2] = {0.5f, 0.5f};
    std::vector<float> flat(2000, 0.5f);
    Sega32xPwmMachine::LowPass(flat.data(), flat.size(), dc, fs, Sega32xPwmMachine::kOutputFilterHz);
    Check(std::fabs(flat.back() - 0.5f) < 1e-4f, "a constant level is kept");
}

void Sega32xPwmMixerFiltersAreTheBoardsCircuit() {
    std::printf("Sega32xPwmMixerFiltersAreTheBoardsCircuit\n");
    const double pi = 3.14159265358979;
    // the cut-offs follow from the parts (32X service manual 7-4 and the IC's resistors, Genesis 2 VA3 manual p.13)
    Check(std::fabs(Sega32xPwmMachine::kSallenKeyHz - 1.0 / (2 * pi * 10e3 * std::sqrt(2200e-12 * 1000e-12))) < 5.0, "the Sallen-Key cut-off is 1 / (2 pi 10k sqrt(2200 pF 1000 pF)) = 10.73 kHz");
    Check(std::fabs(Sega32xPwmMachine::kSallenKeyQ - 0.5 * std::sqrt(2200.0 / 1000.0)) < 1e-3, "its Q is sqrt(2200 / 1000) / 2 = 0.742");
    Check(std::fabs(Sega32xPwmMachine::kMixLowPassHz - 1.0 / (2 * pi * 27e3 * 680e-12)) < 2.0, "the first-order low-pass after the mixer is 1 / (2 pi 27k 680 pF) = 8.67 kHz");
    const float fs = 192000.0f;
    auto gainAt = [&](float hz) {
        std::vector<float> x(38400);
        for (std::size_t i = 0; i < x.size(); ++i) x[i] = std::sin(2.0f * 3.14159265358979f * hz * static_cast<float>(i) / fs);
        float state[2] = {0, 0};
        Sega32xPwmMachine::LowPass2(x.data(), x.size(), state, fs, Sega32xPwmMachine::kSallenKeyHz, Sega32xPwmMachine::kSallenKeyQ);
        double peak = 0;
        for (std::size_t i = 19200; i < x.size(); ++i) peak = std::max(peak, static_cast<double>(std::fabs(x[i])));
        return peak;
    };
    auto analog = [&](double hz) {
        const double s = hz / Sega32xPwmMachine::kSallenKeyHz, q = Sega32xPwmMachine::kSallenKeyQ;
        return 1.0 / std::sqrt((1.0 - s * s) * (1.0 - s * s) + (s / q) * (s / q));
    };
    Check(std::fabs(gainAt(100.0f) - 1.0) < 0.005, "the Sallen-Key stage has unity gain at low frequencies");
    Check(std::fabs(gainAt(Sega32xPwmMachine::kSallenKeyHz) - Sega32xPwmMachine::kSallenKeyQ) < 0.01, "and the gain Q at its cut-off");
    bool matches = true;
    for (float hz : {2000.0f, 5000.0f, 8000.0f, 12000.0f, 16000.0f}) matches = matches && std::fabs(gainAt(hz) - analog(hz)) < 0.03 * analog(hz) + 0.002;
    Check(matches, "its response follows the second-order analog response");
    Check(std::fabs(Sega32xPwmMachine::kCircuitGainLeft - 0.2679 * 47.0 / 15.0) < 1e-4 && std::fabs(Sega32xPwmMachine::kCircuitGainRight - 0.2679 * 47.0 / 15.0) < 1e-4, "the PWM-to-Mega Drive gain ratio is the input network times 47k over the 15k FM resistor, the same for both channels");
}

void Sega32xPwmDriverConfigIsParsedAndChecked() {
    std::printf("Sega32xPwmDriverConfigIsParsedAndChecked\n");
    Sega32xPwmDriverConfig config;
    std::string error;
    Check(ParseSega32xPwmDriverConfig(DriverXml("sh2_queue=\"0x06008774\" sh2_command_address=\"0x20004005\" sh2_command=\"4\" sh2_warmup_ms=\"250\" cart_crc32=\"b5de9626\""), config, &error) &&
              config.queueAddress == 0x06008774 && config.commandAddress == 0x20004005 && config.command == 4 && config.warmupMilliseconds == 250 && config.cartridgeCrc32 == 0xB5DE9626u,
          "a complete <driver> parses and the numbers are read as written");
    Check(!ParseSega32xPwmDriverConfig(DriverXml("sh2_command_address=\"0x20004005\" sh2_command=\"4\""), config, &error) && error.find("sh2_queue") != std::string::npos, "sh2_queue is required, and the error names it");
    Check(!ParseSega32xPwmDriverConfig(DriverXml("sh2_queue=\"0x06008774\" sh2_command_address=\"0x20004005\" sh2_command=\"300\""), config, &error) && error.find("sh2_command") != std::string::npos, "a command above 255 is refused");
    Check(!ParseSega32xPwmDriverConfig(DriverXml("sh2_queue=\"0x06008774\" sh2_command_address=\"0x20004005\" sh2_command=\"4\" sh2_warmup_ms=\"5\""), config, &error) && error.find("sh2_warmup_ms") != std::string::npos, "a warm-up shorter than 50 ms is refused");
    Check(config.haltMaster == false || ParseSega32xPwmDriverConfig(DriverXml("sh2_queue=\"1\" sh2_command_address=\"2\" sh2_command=\"4\""), config, &error) && !config.haltMaster, "the master is not halted unless the <driver> says so");
    Check(ParseSega32xPwmDriverConfig(DriverXml("sh2_queue=\"1\" sh2_command_address=\"2\" sh2_command=\"4\" sh2_halt_master=\"true\""), config, &error) && config.haltMaster, "sh2_halt_master=\"true\" halts the master");
    Check(!ParseSega32xPwmDriverConfig(DriverXml("sh2_queue=\"1\" sh2_command_address=\"2\" sh2_command=\"4\" sh2_halt_master=\"yes\""), config, &error) && error.find("sh2_halt_master") != std::string::npos, "sh2_halt_master accepts only true or false");
    Check(DefaultMachineRegistry().Create(MachineId("SEGA_MEGADRIVE"), VariantId("MEGADRIVE_32X_PWM"), MachineResources{}) != nullptr, "the variant MEGADRIVE_32X_PWM is registered (without resources: not playable)");
}

void Sega32xPwmMachinePlaysTheGamesSamples() {
    std::printf("Sega32xPwmMachinePlaysTheGamesSamples\n");
    const char* romRoot = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRoot == nullptr) {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is needed for the real cartridge\n");
        return;
    }
    const auto xmlPath = FindDefinitionXml("vf.xml");
    Check(xmlPath.has_value(), "vf.xml is present (run from the repo root)");
    if (!xmlPath) return;
    std::ifstream xmlFile(*xmlPath, std::ios::binary);
    std::ostringstream xmlText;
    xmlText << xmlFile.rdbuf();
    const XmlParseResult parsed = ParseGameDefinition(xmlText.str());
    Check(parsed.Status == XmlParseStatus::Ok, "vf.xml parses");
    if (parsed.Status != XmlParseStatus::Ok) return;
    const ResourceResolver resolver{std::filesystem::path(romRoot)};
    ResourceResolutionResult res = resolver.Resolve(parsed.Definition.BuildRequiredResources());
    Check(res.Ok, "the cartridge is found under CHIPESTRA_ROM_ROOT");
    if (!res.Ok) return;
    const std::string driverXml = ExtractDriverElementXml(xmlText.str());
    res.Resources.Add(ResolvedResource("driver_xml", std::vector<std::uint8_t>(driverXml.begin(), driverXml.end())));

    auto play = [&](int id, std::uint64_t block, double seconds, bool stopAfter) {
        std::unique_ptr<IMachine> machine = DefaultMachineRegistry().Create(MachineId(parsed.Definition.Machine), VariantId("MEGADRIVE_32X_PWM"), res.Resources);
        machine->Initialize();
        LogicalChannelSink sink;
        Output out;
        Collect(*machine, sink, 4800, 4800, out);   // 25 ms of the idle state
        if (id >= 0) {
            MusicCommand command;
            command.Kind = MusicCommandKind::Raw;
            command.Value = static_cast<std::uint8_t>(id);
            machine->SendCommand(command);
        }
        Output sound;
        Collect(*machine, sink, static_cast<std::uint64_t>(seconds * 192000), block, sound);
        if (stopAfter) {
            MusicCommand stop;
            stop.Kind = MusicCommandKind::Raw;
            stop.Value = 0;
            stop.Auxiliary = 0x80;
            machine->SendCommand(stop);
            Output after;
            Collect(*machine, sink, 48000, 4800, after);
            sound.peak = after.peak;   // reuse: the peak after the stop
        }
        return sound;
    };

    const Output idle = play(-1, 4800, 0.5, false);
    Check(idle.peak < 0.002, "without a request the output is idle (no sound after the start-up level is removed)");
    const Output hit = play(12, 4800, 1.0, false);
    Check(hit.peak > 0.01, "a request makes sound: the slave SH-2 starts a voice and the PWM unit plays it");
    const Output again = play(12, 4800, 1.0, false);
    Check(again.digest == hit.digest, "the same request twice gives the same samples");
    {   // the output has passed the board's input low-pass: the steps of the PWM carrier (22 kHz) are rounded off
        double jump = 0, energy = 0;
        for (std::size_t i = 1; i < hit.left.size(); ++i) {
            const double d = static_cast<double>(hit.left[i]) - hit.left[i - 1];
            jump += d * d;
            energy += static_cast<double>(hit.left[i]) * hit.left[i];
        }
        Check(jump / energy < 0.0017, "the carrier steps of the PWM unit are smoothed by the whole filter chain of the board (input network, Sallen-Key, mixer low-pass)");
    }
    const Output otherBlocks = play(12, 1234, 1.0, false);
    Check(otherBlocks.digest == hit.digest, "and the same samples whatever the size of the blocks it is rendered in");
    const Output other = play(36, 4800, 1.0, false);
    Check(other.peak > 0.005 && other.digest != hit.digest, "another sample number gives another sound");
    bool sameSides = true;
    for (std::size_t i = 0; i < hit.left.size(); ++i) if (hit.left[i] != hit.right[i]) { sameSides = false; break; }
    Check(sameSides, "the game writes the mono register: both outputs carry the same signal");
    {   // a second request starts from the same state as a first one: the earlier voices do not go on
        auto make = [&]() {
            std::unique_ptr<IMachine> machine = DefaultMachineRegistry().Create(MachineId(parsed.Definition.Machine), VariantId("MEGADRIVE_32X_PWM"), res.Resources);
            machine->Initialize();
            return machine;
        };
        auto request = [&](IMachine& machine, int id) {
            MusicCommand command;
            command.Kind = MusicCommandKind::Raw;
            command.Value = static_cast<std::uint8_t>(id);
            machine.SendCommand(command);
        };
        std::unique_ptr<IMachine> a = make(), b = make();
        LogicalChannelSink sinkA, sinkB;
        Output ignored, secondA, onlyB;
        request(*a, 12);
        Collect(*a, sinkA, 57600, 4800, ignored);
        request(*a, 36);
        Collect(*a, sinkA, 192000, 4800, secondA);
        request(*b, 36);
        Collect(*b, sinkB, 192000, 4800, onlyB);
        Check(secondA.digest == onlyB.digest && secondA.peak > 0.005, "a request after another one gives exactly what it gives on a fresh start");
    }
    const Output stopped = play(12, 4800, 0.5, true);
    Check(stopped.peak < 0.002, "Music Stop (a control track) silences the output");
}

void Sega32xPwmMachineRunsFasterThanRealTime() {
    std::printf("Sega32xPwmMachineRunsFasterThanRealTime\n");
    const char* romRoot = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRoot == nullptr) {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is needed for the real cartridge\n");
        return;
    }
    const auto xmlPath = FindDefinitionXml("vf.xml");
    if (!xmlPath) { Check(false, "vf.xml is present"); return; }
    std::ifstream xmlFile(*xmlPath, std::ios::binary);
    std::ostringstream xmlText;
    xmlText << xmlFile.rdbuf();
    const XmlParseResult parsed = ParseGameDefinition(xmlText.str());
    if (parsed.Status != XmlParseStatus::Ok) { Check(false, "vf.xml parses"); return; }
    const ResourceResolver resolver{std::filesystem::path(romRoot)};
    ResourceResolutionResult res = resolver.Resolve(parsed.Definition.BuildRequiredResources());
    if (!res.Ok) { Check(false, "the cartridge is found"); return; }
    const std::string driverXml = ExtractDriverElementXml(xmlText.str());
    res.Resources.Add(ResolvedResource("driver_xml", std::vector<std::uint8_t>(driverXml.begin(), driverXml.end())));

    {   // a sample plays in about a third of its own time at most, rendered in the blocks the playback pump asks for (a slower machine drops audio)
        std::unique_ptr<IMachine> machine = DefaultMachineRegistry().Create(MachineId(parsed.Definition.Machine), VariantId("MEGADRIVE_32X_PWM"), res.Resources);
        machine->Initialize();
        MusicCommand command;
        command.Kind = MusicCommandKind::Raw;
        command.Value = 37;
        machine->SendCommand(command);
        LogicalChannelSink sink;
        Output out;
        const auto start = std::chrono::steady_clock::now();
        Collect(*machine, sink, 2 * 192000ull, 256, out);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::printf("  2 s of sound made in %.2f s\n", seconds);
        Check(out.peak > 0.01, "the sample is audible");
        Check(seconds < 0.7, "two seconds of a PWM sound are made in a third of their time or less");
    }

    {   // the master SH-2 only idles under the game's own program: the sound is the same whether it runs or not
        const ResolvedResource* cartridge = res.Resources.Find("rom");
        Check(cartridge != nullptr, "the cartridge resource is there");
        if (cartridge == nullptr) return;
        const std::vector<std::uint8_t> bytes(cartridge->Data(), cartridge->Data() + cartridge->Size());
        auto digestOf = [&](bool haltMaster, int id) {
            Sega32xSystem system(bytes);
            if (!system.BootFromHeader()) return std::uint64_t{0};
            const double clock = Sega32xPwm::kNtscClockHz;
            system.RunTo(static_cast<std::uint64_t>(clock * 0.05));
            for (unsigned offset = 0x20; offset < 0x28; offset += 2) system.M68kWrite(offset, 0);
            system.RunTo(static_cast<std::uint64_t>(clock * 0.3));
            const std::size_t warmupFrames = static_cast<std::size_t>(static_cast<double>(system.Now()) * 48000.0 / clock);
            std::vector<float> l(warmupFrames), r(warmupFrames);
            system.RenderPwm(0, 48000.0, l.data(), r.data(), warmupFrames);
            if (haltMaster) system.HaltMaster();
            const std::uint64_t from = system.Now();
            system.Write(0, 0x06008776, static_cast<std::uint32_t>(id), 2);
            system.Write(0, 0x06008774, 2, 2);
            system.Write(0, 0x20004005, 4, 1);
            system.RunTo(from + static_cast<std::uint64_t>(clock * 1.2));
            const std::size_t frames = 48000;
            std::vector<float> left(frames), right(frames);
            system.RenderPwm(static_cast<double>(from), 48000.0, left.data(), right.data(), frames);
            std::uint64_t digest = 1469598103934665603ull;
            for (float v : left) { std::uint32_t bits; std::memcpy(&bits, &v, 4); digest = (digest ^ bits) * 1099511628211ull; }
            return digest;
        };
        for (int id : {12, 37}) {
            const std::uint64_t running = digestOf(false, id);
            const std::uint64_t halted = digestOf(true, id);
            Check(running != 0 && running == halted, "the PWM output is the same with the master SH-2 stopped");
        }
        Check(digestOf(true, 12) != digestOf(true, 37), "and different samples still differ");
    }
}

void Sega32xMusicMachinePlaysBothUnits() {
    std::printf("Sega32xMusicMachinePlaysBothUnits\n");
    const char* romRoot = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRoot == nullptr) {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is needed for the real cartridge\n");
        return;
    }
    const auto xmlPath = FindDefinitionXml("vf.xml");
    Check(xmlPath.has_value(), "vf.xml is present (run from the repo root)");
    if (!xmlPath) return;
    std::ifstream xmlFile(*xmlPath, std::ios::binary);
    std::ostringstream xmlText;
    xmlText << xmlFile.rdbuf();
    const XmlParseResult parsed = ParseGameDefinition(xmlText.str());
    Check(parsed.Status == XmlParseStatus::Ok && parsed.Definition.Variant == "MEGADRIVE_32X_MUSIC", "vf.xml parses and is the combined variant");
    if (parsed.Status != XmlParseStatus::Ok) return;
    const ResourceResolver resolver{std::filesystem::path(romRoot)};
    ResourceResolutionResult res = resolver.Resolve(parsed.Definition.BuildRequiredResources());
    Check(res.Ok, "the cartridge is found under CHIPESTRA_ROM_ROOT");
    if (!res.Ok) return;
    const std::string driverXml = ExtractDriverElementXml(xmlText.str());
    res.Resources.Add(ResolvedResource("driver_xml", std::vector<std::uint8_t>(driverXml.begin(), driverXml.end())));

    const TrackDefinition* song = nullptr;
    const TrackDefinition* pwm = nullptr;
    for (const TrackDefinition& track : parsed.Definition.Tracks) {
        if (track.Id == "BGM_85") song = &track;
        if (track.Id == "PWM_12") pwm = &track;
    }
    Check(song != nullptr && pwm != nullptr && (pwm->Auxiliary & 0x40) != 0 && (song->Auxiliary & 0x40) == 0, "a track with unit=\"pwm\" carries the unit bit and a song does not");
    if (song == nullptr || pwm == nullptr) return;

    auto command = [](const TrackDefinition& track, std::uint8_t extra) {
        MusicCommand c;
        c.Kind = MusicCommandKind::Raw;
        c.Value = static_cast<std::uint8_t>(std::strtoul(track.Command.c_str(), nullptr, 0));
        c.Auxiliary = static_cast<std::uint8_t>(track.Auxiliary | extra);
        return c;
    };
    auto make = [&]() {
        std::unique_ptr<IMachine> machine = DefaultMachineRegistry().Create(MachineId(parsed.Definition.Machine), VariantId(parsed.Definition.Variant), res.Resources);
        machine->Initialize();
        return machine;
    };
    // the energy of the Mega Drive channels (all but the last two) and of the PWM channels
    struct Energy { double md = 0, pwm = 0; std::uint64_t digest = 1469598103934665603ull; };
    auto run = [&](IMachine& machine, LogicalChannelSink& sink, double seconds, std::uint64_t block) {
        Energy e;
        const std::size_t count = machine.LogicalChannels().size();
        std::uint64_t frames = static_cast<std::uint64_t>(seconds * 192000);
        while (frames > 0) {
            const std::uint64_t n = std::min(frames, block);
            sink.ClearAll();
            machine.Render(EmuDuration(n), &sink);
            for (std::size_t c = 0; c < count; ++c) {
                const LogicalChannelBuffer& b = sink.ChannelBuffer(ChannelId(static_cast<std::uint32_t>(c)));
                double sum = 0;
                for (std::size_t i = 0; i < b.Size(); ++i) {
                    sum += static_cast<double>(b[i]) * b[i];
                    std::uint32_t bits;
                    const float v = b[i];
                    std::memcpy(&bits, &v, 4);
                    e.digest = (e.digest ^ bits) * 1099511628211ull;
                }
                (c + 2 >= count ? e.pwm : e.md) += sum;
            }
            frames -= n;
        }
        return e;
    };

    {   // a song of the Mega Drive side plays and the PWM channels stay silent
        std::unique_ptr<IMachine> machine = make();
        LogicalChannelSink sink;
        Check(machine->LogicalChannels().size() == 10 + 2 || machine->LogicalChannels().size() > 2, "the channels of both units are in one list");
        machine->SendCommand(command(*song, 0));
        const Energy e = run(*machine, sink, 2.0, 4800);
        Check(e.md > 1e-3, "a song plays on the Mega Drive channels (the PWM channels carry only the sampled sounds that the driver asks for)");
        const Energy again = [&]() { std::unique_ptr<IMachine> m = make(); LogicalChannelSink s; m->SendCommand(command(*song, 0)); return run(*m, s, 2.0, 4800); }();
        Check(again.digest == e.digest, "the same request gives the same samples");
    }
    {   // a PWM sound after the song silences the song; layered, the song goes on
        std::unique_ptr<IMachine> machine = make();
        LogicalChannelSink sink;
        machine->SendCommand(command(*song, 0));
        const Energy before = run(*machine, sink, 1.0, 4800);
        machine->SendCommand(command(*pwm, 0));
        const Energy replaced = run(*machine, sink, 1.0, 4800);
        Check(replaced.pwm > 1e-3 && replaced.md < before.md * 0.001, "a PWM sound after a song replaces the song");
        std::unique_ptr<IMachine> layered = make();
        LogicalChannelSink sink2;
        layered->SendCommand(command(*song, 0));
        const Energy songAlone = run(*layered, sink2, 1.0, 4800);
        layered->SendCommand(command(*pwm, Sega32xMusicMachine::kLayerBit));
        const Energy both = run(*layered, sink2, 1.0, 4800);
        Check(both.pwm > 1e-3 && both.md > songAlone.md * 0.2, "with the layer bit the song goes on under the PWM sound");
    }
    {   // a song after a PWM sound silences the PWM channels
        std::unique_ptr<IMachine> machine = make();
        LogicalChannelSink sink;
        machine->SendCommand(command(*pwm, 0));
        run(*machine, sink, 0.5, 4800);
        machine->SendCommand(command(*song, 0));
        const Energy e = run(*machine, sink, 1.0, 4800);
        std::unique_ptr<IMachine> alone = make();
        LogicalChannelSink aloneSink;
        alone->SendCommand(command(*song, 0));
        const Energy songAlone = run(*alone, aloneSink, 1.0, 4800);
        Check(e.md > 1e-3 && std::abs(e.pwm - songAlone.pwm) <= songAlone.pwm * 0.01 + 1e-9, "a song after a PWM sound replaces it");
    }
}

namespace {

// The 68000's writes to the YM2612 and the PSG (values in order) of a song request injected into the running game in MAME:
// lines "YM <port> <value> <time>" and "PSG <value> <time>" (CHIPESTRA_REFERENCE_DIR/32x/vf/song_<id>.txt).
struct SongLog {
    std::vector<std::pair<int, int>> ym;
    std::vector<int> psg;
};

bool LoadSongLog(const std::filesystem::path& path, SongLog& out) {
    std::ifstream in(path);
    if (!in) return false;
    std::string kind;
    while (in >> kind) {
        if (kind == "YM") { int port = 0, value = 0; double t = 0; in >> port >> value >> t; out.ym.emplace_back(port, value); }
        else if (kind == "PSG") { int value = 0; double t = 0; in >> value >> t; out.psg.push_back(value); }
        else { std::string rest; std::getline(in, rest); }
    }
    return true;
}

}  // namespace

void Sega32xMusicMachineMatchesTheGamesSongWrites() {
    std::printf("Sega32xMusicMachineMatchesTheGamesSongWrites\n");
    const char* romRoot = GetEnvVar("CHIPESTRA_ROM_ROOT");
    const char* referenceDir = GetEnvVar("CHIPESTRA_REFERENCE_DIR");
    if (romRoot == nullptr || referenceDir == nullptr) {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT and CHIPESTRA_REFERENCE_DIR are needed\n");
        return;
    }
    const auto xmlPath = FindDefinitionXml("vf.xml");
    if (!xmlPath) { Check(false, "vf.xml is present"); return; }
    std::ifstream xmlFile(*xmlPath, std::ios::binary);
    std::ostringstream xmlText;
    xmlText << xmlFile.rdbuf();
    const XmlParseResult parsed = ParseGameDefinition(xmlText.str());
    if (parsed.Status != XmlParseStatus::Ok) { Check(false, "vf.xml parses"); return; }
    const ResourceResolver resolver{std::filesystem::path(romRoot)};
    ResourceResolutionResult res = resolver.Resolve(parsed.Definition.BuildRequiredResources());
    if (!res.Ok) { Check(false, "the cartridge is found"); return; }
    const std::string driverXml = ExtractDriverElementXml(xmlText.str());
    res.Resources.Add(ResolvedResource("driver_xml", std::vector<std::uint8_t>(driverXml.begin(), driverXml.end())));

    struct Case { const char* id; int number; std::size_t ymCount, psgCount; };
    // the first part of each log is the song; the end of a log is where the demo of the game itself does something else
    const Case cases[] = {{"BGM_85", 0x85, 6400, 2150}, {"BGM_8B", 0x8B, 4400, 1200}};
    for (const Case& c : cases) {
        char name[64];
        std::snprintf(name, sizeof(name), "32x/vf/song_%02X.txt", c.number);
        SongLog log;
        if (!LoadSongLog(std::filesystem::path(referenceDir) / name, log)) { std::printf("  SKIP: %s is not there\n", name); continue; }
        std::unique_ptr<IMachine> machine = DefaultMachineRegistry().Create(MachineId(parsed.Definition.Machine), VariantId(parsed.Definition.Variant), res.Resources);
        machine->Initialize();
        auto* both = dynamic_cast<Sega32xMusicMachine*>(machine.get());
        Check(both != nullptr, "the machine is the combined one");
        if (both == nullptr) return;
        SongLog ours;
        bool logging = false;
        both->MegaDriveForTest().SetChipWriteObserverForTest([&](std::uint64_t, char cpu, char kind, std::uint32_t port, std::uint8_t value) {
            if (!logging || cpu != '6') return;
            if (kind == 'Y') ours.ym.emplace_back(static_cast<int>(port), static_cast<int>(value));
            else ours.psg.push_back(static_cast<int>(value));
        });
        for (const TrackDefinition& track : parsed.Definition.Tracks) {
            if (track.Id != c.id) continue;
            MusicCommand command;
            command.Kind = MusicCommandKind::Raw;
            command.Value = static_cast<std::uint8_t>(std::strtoul(track.Command.c_str(), nullptr, 0));
            command.Auxiliary = track.Auxiliary;
            machine->SendCommand(command);
        }
        logging = true;
        LogicalChannelSink sink;
        Output ignored;
        Collect(*machine, sink, 20 * 192000ull, 4800, ignored);
        // the log of the game starts with the writes the game's own music made in the frame of the request (the PSG's first two)
        const std::size_t skipPsg = 2;
        bool ymEqual = ours.ym.size() >= c.ymCount && log.ym.size() >= c.ymCount;
        for (std::size_t i = 0; ymEqual && i < c.ymCount; ++i) ymEqual = ours.ym[i] == log.ym[i];
        bool psgEqual = ours.psg.size() >= c.psgCount && log.psg.size() >= c.psgCount + skipPsg;
        for (std::size_t i = 0; psgEqual && i < c.psgCount; ++i) psgEqual = ours.psg[i] == log.psg[skipPsg + i];
        char text[160];
        std::snprintf(text, sizeof(text), "song %02X: the YM2612 writes (%zu, in order) are those of the original", c.number, c.ymCount);
        Check(ymEqual, text);
        std::snprintf(text, sizeof(text), "song %02X: the PSG writes (%zu, in order) are those of the original", c.number, c.psgCount);
        Check(psgEqual, text);
    }
}

void Sega32xMusicMachinePlaysTheStartJinglesOfTheMaster() {
    std::printf("Sega32xMusicMachinePlaysTheStartJinglesOfTheMaster\n");
    const char* romRoot = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRoot == nullptr) {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is needed for the real cartridge\n");
        return;
    }
    const auto xmlPath = FindDefinitionXml("vrdx.xml");
    Check(xmlPath.has_value(), "vrdx.xml is present (run from the repo root)");
    if (!xmlPath) return;
    std::ifstream xmlFile(*xmlPath, std::ios::binary);
    std::ostringstream xmlText;
    xmlText << xmlFile.rdbuf();
    const XmlParseResult parsed = ParseGameDefinition(xmlText.str());
    Check(parsed.Status == XmlParseStatus::Ok && parsed.Definition.Variant == "MEGADRIVE_32X_MUSIC", "vrdx.xml parses and is the combined variant");
    if (parsed.Status != XmlParseStatus::Ok) return;
    const ResourceResolver resolver{std::filesystem::path(romRoot)};
    ResourceResolutionResult res = resolver.Resolve(parsed.Definition.BuildRequiredResources());
    Check(res.Ok, "the cartridge is found under CHIPESTRA_ROM_ROOT");
    if (!res.Ok) return;
    const std::string driverXml = ExtractDriverElementXml(xmlText.str());
    res.Resources.Add(ResolvedResource("driver_xml", std::vector<std::uint8_t>(driverXml.begin(), driverXml.end())));

    // the energy of the PWM channels (the last two) in each half second of a track
    auto halves = [&](const char* id) {
        std::vector<double> energy(12, 0.0);
        const TrackDefinition* track = nullptr;
        for (const TrackDefinition& t : parsed.Definition.Tracks)
            if (t.Id == id) track = &t;
        Check(track != nullptr && (track->Auxiliary & 0x40) != 0, "the jingle track exists and is played by the PWM unit");
        if (track == nullptr) return energy;
        std::unique_ptr<IMachine> machine = DefaultMachineRegistry().Create(MachineId(parsed.Definition.Machine), VariantId(parsed.Definition.Variant), res.Resources);
        machine->Initialize();
        MusicCommand c;
        c.Kind = MusicCommandKind::Raw;
        c.Value = static_cast<std::uint8_t>(std::strtoul(track->Command.c_str(), nullptr, 0));
        c.Auxiliary = track->Auxiliary;
        machine->SendCommand(c);
        LogicalChannelSink sink;
        const std::size_t count = machine->LogicalChannels().size();
        const std::uint64_t block = 256;
        std::uint64_t done = 0;
        while (done < 6 * 192000) {
            sink.ClearAll();
            machine->Render(EmuDuration(block), &sink);
            for (std::size_t ch = count - 2; ch < count; ++ch) {
                const LogicalChannelBuffer& b = sink.ChannelBuffer(ChannelId(static_cast<std::uint32_t>(ch)));
                double sum = 0;
                for (std::size_t i = 0; i < b.Size(); ++i) sum += static_cast<double>(b[i]) * b[i];
                energy[static_cast<std::size_t>(done / 96000)] += sum;
            }
            done += block;
        }
        return energy;
    };
    for (const char* id : {"PWM_06", "PWM_07"}) {
        const std::vector<double> e = halves(id);
        Check(e[3] > 0.5 && e[4] > 0.5 && e[5] > 0.5, "the master plays the jingle through the PWM unit (the second after the request)");
        Check(e[11] < e[4] * 1e-3 && e[10] < e[4] * 1e-3, "the jingle ends by itself, within about 5 seconds");
    }
}

// The Z80 sound driver of Space Harrier (Super 32X) writes the events of the music for the SH-2 synthesizer into a ring buffer in its RAM, and the
// game's 68000 hands them to the slave. The request 0xAD (the reference is MAME's record of the events the 68000 passed on; the events of the request
// 0x81 that the game made 13 ms later are left out).
void Sega32xZ80MachineSendsTheEventsOfTheOriginal() {
    std::printf("Sega32xZ80MachineSendsTheEventsOfTheOriginal\n");
    const char* romRoot = GetEnvVar("CHIPESTRA_ROM_ROOT");
    const char* referenceDir = GetEnvVar("CHIPESTRA_REFERENCE_DIR");
    if (romRoot == nullptr || referenceDir == nullptr) {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT and CHIPESTRA_REFERENCE_DIR are needed\n");
        return;
    }
    const auto xmlPath = FindDefinitionXml("sharrier.xml");
    Check(xmlPath.has_value(), "sharrier.xml is present (run from the repo root)");
    if (!xmlPath) return;
    std::ifstream xmlFile(*xmlPath, std::ios::binary);
    std::ostringstream xmlText;
    xmlText << xmlFile.rdbuf();
    const XmlParseResult parsed = ParseGameDefinition(xmlText.str());
    Check(parsed.Status == XmlParseStatus::Ok && parsed.Definition.Variant == "MEGADRIVE_32X_Z80", "sharrier.xml parses and is the Z80 variant");
    if (parsed.Status != XmlParseStatus::Ok) return;
    const ResourceResolver resolver{std::filesystem::path(romRoot)};
    ResourceResolutionResult res = resolver.Resolve(parsed.Definition.BuildRequiredResources());
    Check(res.Ok, "the cartridge is found under CHIPESTRA_ROM_ROOT");
    if (!res.Ok) return;
    const std::string driverXml = ExtractDriverElementXml(xmlText.str());
    res.Resources.Add(ResolvedResource("driver_xml", std::vector<std::uint8_t>(driverXml.begin(), driverXml.end())));

    struct Event { double time; std::vector<int> bytes; };
    std::vector<Event> expected;
    {
        std::ifstream in(std::filesystem::path(referenceDir) / "32x" / "sharrier" / "events_AD.txt");
        Check(in.good(), "the recording of the original's events is present");
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream words(line);
            Event e;
            words >> e.time;
            std::string byte;
            while (words >> byte) e.bytes.push_back(static_cast<int>(std::strtoul(byte.c_str(), nullptr, 16)));
            if (e.bytes.size() == 1 && (e.bytes[0] == 0x85 || e.bytes[0] == 0x86)) continue;
            expected.push_back(e);
        }
    }
    if (expected.empty()) return;

    std::unique_ptr<IMachine> machine = DefaultMachineRegistry().Create(MachineId(parsed.Definition.Machine), VariantId(parsed.Definition.Variant), res.Resources);
    auto* both = dynamic_cast<Sega32xMusicMachine*>(machine.get());
    Check(both != nullptr, "the Machine is the combined 32X Machine");
    if (both == nullptr) return;
    std::vector<Event> found;
    both->SetEventObserverForTest([&](std::uint64_t ticks, const std::uint8_t* bytes, std::size_t count) {
        Event e;
        e.time = static_cast<double>(ticks) / 192000.0;
        for (std::size_t i = 0; i < count; ++i) e.bytes.push_back(bytes[i]);
        found.push_back(e);
    });
    machine->Initialize();
    MusicCommand request;
    request.Kind = MusicCommandKind::Raw;
    request.Value = 0xAD;
    machine->SendCommand(request);
    LogicalChannelSink sink;
    for (std::uint64_t done = 0; done < 3 * 192000; done += 256) {
        sink.ClearAll();
        machine->Render(EmuDuration(256), &sink);
    }
    std::size_t same = 0;
    double worst = 0;
    for (std::size_t i = 0; i < expected.size() && i < found.size(); ++i) {
        if (expected[i].bytes != found[i].bytes) break;
        worst = std::max(worst, std::abs(expected[i].time - found[i].time));
        ++same;
    }
    std::printf("  %zu of %zu events as in the original, the largest difference of time %.1f ms\n", same, expected.size(), worst * 1000.0);
    Check(found.size() >= expected.size() && same == expected.size(), "the events come in the original's order with the original's bytes");
    Check(worst < 0.035, "and in the original's time (within 35 ms: the 68000 passes them on once a frame)");
}

// After Burner Complete (Super 32X): the Z80 driver of the cartridge plays the music; it takes a step only when the game's 68000 has added one to a
// byte in the Z80's RAM (<driver frame_counter>), and reads its data through the 32X windows.
void Sega32xZ80GamePlaysItsSongs() {
    std::printf("Sega32xZ80GamePlaysItsSongs\n");
    const char* romRoot = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRoot == nullptr) {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is needed for the real cartridge\n");
        return;
    }
    const auto xmlPath = FindDefinitionXml("aburner.xml");
    Check(xmlPath.has_value(), "aburner.xml is present (run from the repo root)");
    if (!xmlPath) return;
    std::ifstream xmlFile(*xmlPath, std::ios::binary);
    std::ostringstream xmlText;
    xmlText << xmlFile.rdbuf();
    const XmlParseResult parsed = ParseGameDefinition(xmlText.str());
    Check(parsed.Status == XmlParseStatus::Ok, "aburner.xml parses");
    if (parsed.Status != XmlParseStatus::Ok) return;
    const ResourceResolver resolver{std::filesystem::path(romRoot)};
    ResourceResolutionResult res = resolver.Resolve(parsed.Definition.BuildRequiredResources());
    Check(res.Ok, "the cartridge is found under CHIPESTRA_ROM_ROOT");
    if (!res.Ok) return;
    const std::string driverXml = ExtractDriverElementXml(xmlText.str());
    res.Resources.Add(ResolvedResource("driver_xml", std::vector<std::uint8_t>(driverXml.begin(), driverXml.end())));
    auto energyOf = [&](std::uint8_t value, std::size_t from, std::size_t to, std::uint64_t block) {
        std::unique_ptr<IMachine> machine = DefaultMachineRegistry().Create(MachineId(parsed.Definition.Machine), VariantId(parsed.Definition.Variant), res.Resources);
        machine->Initialize();
        MusicCommand c;
        c.Kind = MusicCommandKind::Raw;
        c.Value = value;
        machine->SendCommand(c);
        LogicalChannelSink sink;
        double energy = 0;
        const std::size_t count = machine->LogicalChannels().size();
        std::uint64_t done = 0;
        while (done < static_cast<std::uint64_t>(to) * 192000) {
            sink.ClearAll();
            machine->Render(EmuDuration(block), &sink);
            if (done >= static_cast<std::uint64_t>(from) * 192000)
                for (std::size_t ch = 0; ch < count; ++ch) {
                    if (machine->LogicalChannels()[ch].DeviceName == "PWM") continue;   // the Mega Drive side only: the Z80 driver's own sound
                    const LogicalChannelBuffer& b = sink.ChannelBuffer(ChannelId(static_cast<std::uint32_t>(ch)));
                    for (std::size_t i = 0; i < b.Size(); ++i) energy += static_cast<double>(b[i]) * b[i];
                }
            done += block;
        }
        return energy;
    };
    const double song = energyOf(0x91, 2, 4, 256);
    Check(song > 1.0, "the song 0x91 sounds (the driver steps with the frame counter and reads its data through the 32X window)");
    const double bigBlocks = energyOf(0x91, 2, 4, 1024);
    std::printf("  energy %.6f (blocks of 256), %.6f (1024)\n", song, bigBlocks);
    Check(std::abs(bigBlocks - song) < song * 0.02, "the sound hardly depends on the size of the blocks");
    Check(energyOf(0x05, 2, 4, 256) < song * 1e-3, "a request the driver does not know makes no song");
}

// After Burner Complete: the game's 68000 tells the slave processor every sound request that it gives the Z80 sound driver (a word in the communication registers
// and the slave's command interrupt), and in each vertical-blank handler 0x0203 (the Z80 took a request) and 0x0200. The slave plays PWM sounds for the requests.
void Sega32xMusicMachineTellsTheSlaveTheZ80DriversRequests() {
    std::printf("Sega32xMusicMachineTellsTheSlaveTheZ80DriversRequests\n");
    const char* romRoot = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRoot == nullptr) {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is needed for the real cartridge\n");
        return;
    }
    const auto xmlPath = FindDefinitionXml("aburner.xml");
    Check(xmlPath.has_value(), "aburner.xml is present (run from the repo root)");
    if (!xmlPath) return;
    std::ifstream xmlFile(*xmlPath, std::ios::binary);
    std::ostringstream xmlText;
    xmlText << xmlFile.rdbuf();
    const XmlParseResult parsed = ParseGameDefinition(xmlText.str());
    Check(parsed.Status == XmlParseStatus::Ok, "aburner.xml parses");
    if (parsed.Status != XmlParseStatus::Ok) return;
    const ResourceResolver resolver{std::filesystem::path(romRoot)};
    ResourceResolutionResult res = resolver.Resolve(parsed.Definition.BuildRequiredResources());
    Check(res.Ok, "the cartridge is found under CHIPESTRA_ROM_ROOT");
    if (!res.Ok) return;
    const std::string driverXml = ExtractDriverElementXml(xmlText.str());
    res.Resources.Add(ResolvedResource("driver_xml", std::vector<std::uint8_t>(driverXml.begin(), driverXml.end())));

    std::unique_ptr<IMachine> machine = DefaultMachineRegistry().Create(MachineId(parsed.Definition.Machine), VariantId(parsed.Definition.Variant), res.Resources);
    machine->Initialize();
    LogicalChannelSink sink;
    // the PWM channels' own output (before the mixer): the energy before the request and in the seconds after it
    double before = 0.0, after = 0.0, beforeCount = 0.0, afterCount = 0.0;
    auto render = [&](std::uint64_t ticks, bool requested) {
        for (std::uint64_t done = 0; done < ticks; done += 256) {
            sink.ClearAll();
            machine->Render(EmuDuration(256), &sink);
            for (const LogicalChannel& c : machine->LogicalChannels()) {
                if (c.DeviceName != "PWM") continue;
                const LogicalChannelBuffer& buffer = sink.ChannelBuffer(c.Id);
                for (std::size_t i = 0; i < buffer.Size(); ++i) {
                    const double v = buffer.Data()[i];
                    if (requested) { after += v * v; afterCount += 1.0; } else { before += v * v; beforeCount += 1.0; }
                }
            }
        }
    };
    render(192000, false);
    MusicCommand request;
    request.Kind = MusicCommandKind::Raw;
    request.Value = 0x93;
    machine->SendCommand(request);
    render(5 * 192000, true);
    const double beforeRms = beforeCount > 0.0 ? std::sqrt(before / beforeCount) : 0.0;
    const double afterRms = afterCount > 0.0 ? std::sqrt(after / afterCount) : 0.0;
    std::printf("  PWM output: %.5f before the request, %.5f in the 5 s after it\n", beforeRms, afterRms);
    Check(beforeRms < 0.001, "the PWM unit is silent before a request");
    Check(afterRms > 0.01, "the PWM unit sounds after the request (the slave was told the request and the Z80's taking of it)");
}

// The game's 68000 adds to the driver's frame counter twice a frame, so the Z80 driver steps at 120 Hz. The reference is the original's timing of the song 0x92:
// the key-ons of the first bars fall 0.30 s, 0.60 s and 0.90 s after the first one.
void Sega32xZ80DriverTempoMatchesTheOriginal() {
    std::printf("Sega32xZ80DriverTempoMatchesTheOriginal\n");
    const char* romRoot = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRoot == nullptr) {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is needed for the real cartridge\n");
        return;
    }
    const auto xmlPath = FindDefinitionXml("aburner.xml");
    Check(xmlPath.has_value(), "aburner.xml is present (run from the repo root)");
    if (!xmlPath) return;
    std::ifstream xmlFile(*xmlPath, std::ios::binary);
    std::ostringstream xmlText;
    xmlText << xmlFile.rdbuf();
    const XmlParseResult parsed = ParseGameDefinition(xmlText.str());
    Check(parsed.Status == XmlParseStatus::Ok, "aburner.xml parses");
    if (parsed.Status != XmlParseStatus::Ok) return;
    const ResourceResolver resolver{std::filesystem::path(romRoot)};
    ResourceResolutionResult res = resolver.Resolve(parsed.Definition.BuildRequiredResources());
    Check(res.Ok, "the cartridge is found under CHIPESTRA_ROM_ROOT");
    if (!res.Ok) return;
    const std::string driverXml = ExtractDriverElementXml(xmlText.str());
    res.Resources.Add(ResolvedResource("driver_xml", std::vector<std::uint8_t>(driverXml.begin(), driverXml.end())));
    std::unique_ptr<IMachine> machine = DefaultMachineRegistry().Create(MachineId(parsed.Definition.Machine), VariantId(parsed.Definition.Variant), res.Resources);
    machine->Initialize();
    auto* both = dynamic_cast<Sega32xMusicMachine*>(machine.get());
    Check(both != nullptr, "the machine is a Super 32X music machine");
    if (both == nullptr) return;
    std::vector<double> keyOns;
    int reg = -1;
    const double hz = static_cast<double>(both->Z80MegaDriveForTest().TimingForTest().z80ClockHz);
    both->Z80MegaDriveForTest().SetChipWriteObserverForTest([&](std::uint64_t cycle, char kind, std::uint32_t port, std::uint8_t value) {
        if (kind != 'Y') return;
        if (port == 0) reg = value;
        else if (port == 1 && reg == 0x28 && (value >> 4) != 0) keyOns.push_back(static_cast<double>(cycle) / hz);
    });
    MusicCommand request;
    request.Kind = MusicCommandKind::Raw;
    request.Value = 0x92;
    machine->SendCommand(request);
    LogicalChannelSink sink;
    std::uint64_t frameEndsAtStart = 0;
    for (std::uint64_t done = 0; done < 6 * 192000; done += 256) {
        sink.ClearAll();
        machine->Render(EmuDuration(256), &sink);
        if (done == 0) frameEndsAtStart = both->FrameEndsToldForTest();   // after the first block has caught up with the frames of the start
    }
    Check(keyOns.size() > 20, "the song plays notes");
    if (keyOns.size() <= 20) return;
    const double gap6 = keyOns[6] - keyOns[0], gap10 = keyOns[10] - keyOns[0], gap16 = keyOns[16] - keyOns[0];
    std::printf("  key-on gaps: %.3f %.3f %.3f s (the original: 0.30 0.60 0.90)\n", gap6, gap10, gap16);
    Check(std::fabs(gap6 - 0.30) < 0.01 && std::fabs(gap10 - 0.60) < 0.01 && std::fabs(gap16 - 0.90) < 0.01, "the Z80 driver steps at the original's tempo (twice a frame)");
    // the slave's PWM sequencer runs on the frame-end words 0x0200 that the same handler tells it: about 120 a second in the original (MAME: 10.003 s, 10.010 s, 10.020 s, 10.027 s ...)
    const double frameEnds = static_cast<double>(both->FrameEndsToldForTest() - frameEndsAtStart);
    std::printf("  frame-end words in 6 s: %.0f (the original: about 720)\n", frameEnds);
    Check(frameEnds >= 715.0 && frameEnds <= 725.0, "the slave is told the frame end twice a frame");
}

// Star Wars Arcade: the game's own sound driver runs on the 68000 (start-up routines of the game, then the sound routine once a frame) and the Z80 plays the
// YM2612. The reference is the original's YM2612 register writes of the Z80 (without the timer register 0x27 and the DAC register 0x2A) after the 68000 has
// started a song or an effect: the same values in the same order, the same times (the writes are those before the game itself stopped the sound).
void Sega32xStarWarsArcadeMatchesTheOriginal() {
    std::printf("Sega32xStarWarsArcadeMatchesTheOriginal\n");
    const char* romRoot = GetEnvVar("CHIPESTRA_ROM_ROOT");
    const char* referenceDir = GetEnvVar("CHIPESTRA_REFERENCE_DIR");
    if (romRoot == nullptr || referenceDir == nullptr) {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT and CHIPESTRA_REFERENCE_DIR are needed\n");
        return;
    }
    const auto xmlPath = FindDefinitionXml("swa.xml");
    Check(xmlPath.has_value(), "swa.xml is present (run from the repo root)");
    if (!xmlPath) return;
    std::ifstream xmlFile(*xmlPath, std::ios::binary);
    std::ostringstream xmlText;
    xmlText << xmlFile.rdbuf();
    const XmlParseResult parsed = ParseGameDefinition(xmlText.str());
    Check(parsed.Status == XmlParseStatus::Ok, "swa.xml parses");
    if (parsed.Status != XmlParseStatus::Ok) return;
    const ResourceResolver resolver{std::filesystem::path(romRoot)};
    ResourceResolutionResult res = resolver.Resolve(parsed.Definition.BuildRequiredResources());
    Check(res.Ok, "the cartridge is found under CHIPESTRA_ROM_ROOT");
    if (!res.Ok) return;
    const std::string driverXml = ExtractDriverElementXml(xmlText.str());
    res.Resources.Add(ResolvedResource("driver_xml", std::vector<std::uint8_t>(driverXml.begin(), driverXml.end())));

    struct Reference { const char* file; int number; };
    const Reference references[] = {{"song_01.txt", 1}, {"song_02.txt", 2}, {"song_03.txt", 3}, {"song_04.txt", 4}, {"song_06.txt", 6},
                                    {"effect_0D.txt", 0x0D}, {"effect_10.txt", 0x10}, {"effect_14.txt", 0x14}, {"effect_19.txt", 0x19}, {"effect_1E.txt", 0x1E}, {"effect_28.txt", 0x28}};
    const std::filesystem::path dir = std::filesystem::path(referenceDir) / "32x" / "swa" / "real_game";
    for (const Reference& ref : references) {
        std::ifstream in(dir / ref.file);
        if (!in) { std::printf("  SKIP: %s is not in CHIPESTRA_REFERENCE_DIR\n", ref.file); continue; }
        struct Write { double time; int part; int reg; int value; };
        std::vector<Write> expected;
        for (std::string line; std::getline(in, line);) {
            if (line.empty() || line[0] == '#') continue;
            double t; int part, reg, value;
            if (std::sscanf(line.c_str(), "%lf %d %x %x", &t, &part, &reg, &value) == 4) expected.push_back({t, part, reg, value});
        }
        std::unique_ptr<IMachine> machine = DefaultMachineRegistry().Create(MachineId(parsed.Definition.Machine), VariantId(parsed.Definition.Variant), res.Resources);
        machine->Initialize();
        auto* both = dynamic_cast<Sega32xMusicMachine*>(machine.get());
        Check(both != nullptr, "the machine is a Super 32X music machine");
        if (both == nullptr) return;
        const double hz = static_cast<double>(both->MegaDriveForTest().TimingForTest().masterClockHz);
        std::vector<Write> got;
        int latch[2] = {-1, -1};
        both->MegaDriveForTest().SetChipWriteObserverForTest([&](std::uint64_t ticks, char cpu, char kind, std::uint32_t port, std::uint8_t value) {
            if (cpu != 'Z' || kind != 'Y') return;
            const int part = static_cast<int>(port >> 1);
            if ((port & 1) == 0) { latch[part] = value; return; }
            const int reg = latch[part];
            if (part == 0 && (reg == 0x27 || reg == 0x2A)) return;
            got.push_back({static_cast<double>(ticks) / hz, part, reg, value});
        });
        MusicCommand request;
        request.Kind = MusicCommandKind::Raw;
        request.Value = static_cast<std::uint8_t>(ref.number);
        machine->SendCommand(request);
        got.clear();
        LogicalChannelSink sink;
        for (std::uint64_t done = 0; done < 3 * 192000; done += 256) {
            sink.ClearAll();
            machine->Render(EmuDuration(256), &sink);
        }
        // the first register write of the reference marks the start; everything after it has to agree
        std::size_t start = got.size();
        for (std::size_t i = 0; i < got.size() && !expected.empty(); ++i)
            if (got[i].part == expected[0].part && got[i].reg == expected[0].reg && got[i].value == expected[0].value) { start = i; break; }
        char text[200];
        std::snprintf(text, sizeof(text), "%s: the first register write of the original is made (%zu writes in 3 s)", ref.file, got.size());
        Check(start < got.size(), text);
        if (start >= got.size()) continue;
        const std::size_t available = got.size() - start;
        std::size_t same = 0;
        double worst = 0.0;
        while (same < expected.size() && same < available && got[start + same].part == expected[same].part && got[start + same].reg == expected[same].reg &&
               got[start + same].value == expected[same].value) {
            worst = std::max(worst, std::fabs((got[start + same].time - got[start].time) - expected[same].time));
            ++same;
        }
        std::snprintf(text, sizeof(text), "%s: %zu of %zu register writes have the original's values and order (the largest time difference %.2f ms)", ref.file, same, expected.size(), worst * 1000.0);
        Check(same == expected.size(), text);
        Check(worst < 0.005, (std::string(ref.file) + ": the writes keep the original's times (within 5 ms)").c_str());
    }

    // the sampled effect 0x0C: the first DAC bytes (register 0x2A) after its first register write (part 1, register 0xB6) are the original's
    std::ifstream dacIn(dir / "effect_0C_dac.txt");
    if (!dacIn) { std::printf("  SKIP: effect_0C_dac.txt is not in CHIPESTRA_REFERENCE_DIR\n"); return; }
    std::vector<std::pair<double, int>> expectedDac;
    for (std::string line; std::getline(dacIn, line);) {
        double t; int value;
        if (!line.empty() && line[0] != '#' && std::sscanf(line.c_str(), "%lf %x", &t, &value) == 2) expectedDac.emplace_back(t, value);
    }
    std::unique_ptr<IMachine> machine = DefaultMachineRegistry().Create(MachineId(parsed.Definition.Machine), VariantId(parsed.Definition.Variant), res.Resources);
    machine->Initialize();
    auto* both = dynamic_cast<Sega32xMusicMachine*>(machine.get());
    if (both == nullptr) return;
    const double hz = static_cast<double>(both->MegaDriveForTest().TimingForTest().masterClockHz);
    std::vector<std::pair<double, int>> dac;
    double startTime = -1.0;
    int latch[2] = {-1, -1};
    both->MegaDriveForTest().SetChipWriteObserverForTest([&](std::uint64_t ticks, char cpu, char kind, std::uint32_t port, std::uint8_t value) {
        if (cpu != 'Z' || kind != 'Y') return;
        const int part = static_cast<int>(port >> 1);
        if ((port & 1) == 0) { latch[part] = value; return; }
        const double now = static_cast<double>(ticks) / hz;
        if (part == 1 && latch[1] == 0xB6 && value == 0xC0 && startTime < 0.0) startTime = now;
        if (part == 0 && latch[0] == 0x2A && startTime >= 0.0) dac.emplace_back(now - startTime, value);
    });
    MusicCommand request;
    request.Kind = MusicCommandKind::Raw;
    request.Value = 0x0C;
    machine->SendCommand(request);
    LogicalChannelSink sink;
    for (std::uint64_t done = 0; done < 192000; done += 256) {
        sink.ClearAll();
        machine->Render(EmuDuration(256), &sink);
    }
    std::size_t sameDac = 0;
    double worstDac = 0.0;
    while (sameDac < expectedDac.size() && sameDac < dac.size() && dac[sameDac].second == expectedDac[sameDac].second) {
        worstDac = std::max(worstDac, std::fabs(dac[sameDac].first - expectedDac[sameDac].first));
        ++sameDac;
    }
    char text[200];
    std::snprintf(text, sizeof(text), "effect 0x0C: %zu of %zu DAC bytes are the original's (the largest time difference %.2f ms)", sameDac, expectedDac.size(), worstDac * 1000.0);
    Check(!expectedDac.empty() && sameDac == expectedDac.size() && worstDac < 0.001, text);
}

// Star Wars Arcade: the long sampled sounds (the demo's music and the effects and voices of the game) are played by the slave SH-2 through the PWM unit. The game's
// 68000 asks for one with two commands for the master SH-2 (the descriptor address from a table in the cartridge; then the start), and the slave's sample player writes
// a value to the PWM mono register at about 22 kHz. The reference is the original's sequence of those values after the request.
void Sega32xStarWarsArcadePwmMatchesTheOriginal() {
    std::printf("Sega32xStarWarsArcadePwmMatchesTheOriginal\n");
    const char* romRoot = GetEnvVar("CHIPESTRA_ROM_ROOT");
    const char* referenceDir = GetEnvVar("CHIPESTRA_REFERENCE_DIR");
    if (romRoot == nullptr || referenceDir == nullptr) {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT and CHIPESTRA_REFERENCE_DIR are needed\n");
        return;
    }
    const auto xmlPath = FindDefinitionXml("swa.xml");
    Check(xmlPath.has_value(), "swa.xml is present (run from the repo root)");
    if (!xmlPath) return;
    std::ifstream xmlFile(*xmlPath, std::ios::binary);
    std::ostringstream xmlText;
    xmlText << xmlFile.rdbuf();
    const XmlParseResult parsed = ParseGameDefinition(xmlText.str());
    if (parsed.Status != XmlParseStatus::Ok) { Check(false, "swa.xml parses"); return; }
    const ResourceResolver resolver{std::filesystem::path(romRoot)};
    ResourceResolutionResult res = resolver.Resolve(parsed.Definition.BuildRequiredResources());
    Check(res.Ok, "the cartridge is found under CHIPESTRA_ROM_ROOT");
    if (!res.Ok) return;
    const std::string driverXml = ExtractDriverElementXml(xmlText.str());
    res.Resources.Add(ResolvedResource("driver_xml", std::vector<std::uint8_t>(driverXml.begin(), driverXml.end())));

    struct Reference { const char* file; int number; };
    const Reference references[] = {{"pwm_02.txt", 2}, {"pwm_03.txt", 3}, {"pwm_24.txt", 0x24}, {"pwm_29.txt", 0x29}};
    const std::filesystem::path dir = std::filesystem::path(referenceDir) / "32x" / "swa" / "real_game";
    for (const Reference& ref : references) {
        std::ifstream in(dir / ref.file);
        if (!in) { std::printf("  SKIP: %s is not in CHIPESTRA_REFERENCE_DIR\n", ref.file); continue; }
        std::vector<int> expected;
        for (std::string line; std::getline(in, line);) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream values(line);
            for (int v; values >> v;) expected.push_back(v);
        }
        std::unique_ptr<IMachine> machine = DefaultMachineRegistry().Create(MachineId(parsed.Definition.Machine), VariantId(parsed.Definition.Variant), res.Resources);
        machine->Initialize();
        auto* both = dynamic_cast<Sega32xMusicMachine*>(machine.get());
        Check(both != nullptr, "the machine is a Super 32X music machine");
        if (both == nullptr) return;
        std::vector<int> got;
        both->PwmForTest().SystemForTest().SetWriteObserverForTest([&](int cpu, std::uint32_t address, std::uint32_t value, int) {
            if (cpu == 1 && (address & 0xFFFF) == 0x4038) got.push_back(static_cast<int>(value & 0xFFFF));
        });
        MusicCommand request;
        request.Kind = ref.number == 2 ? MusicCommandKind::Raw : MusicCommandKind::Effect;
        request.Value = static_cast<std::uint8_t>(ref.number);
        request.Auxiliary = 0x40;   // the PWM unit
        machine->SendCommand(request);
        LogicalChannelSink sink;
        for (std::uint64_t done = 0; done < 3 * 192000; done += 256) {
            sink.ClearAll();
            machine->Render(EmuDuration(256), &sink);
        }
        // the reference starts at a sample of the original; find it in what the Machine wrote (the first 60 values)
        std::size_t start = got.size();
        for (std::size_t i = 0; i + 60 <= got.size() && expected.size() >= 60 && start == got.size(); ++i)
            if (std::equal(expected.begin(), expected.begin() + 60, got.begin() + static_cast<std::ptrdiff_t>(i))) start = i;
        char text[200];
        std::snprintf(text, sizeof(text), "%s: the original's first values are found in the Machine's PWM values (%zu values in 3 s)", ref.file, got.size());
        Check(start < got.size(), text);
        if (start >= got.size()) continue;
        std::size_t same = 0;
        while (same < expected.size() && start + same < got.size() && got[start + same] == expected[same]) ++same;
        std::snprintf(text, sizeof(text), "%s: %zu of %zu values equal the original's", ref.file, same, expected.size());
        Check(same == expected.size(), text);
    }
}

// The SEGA logo of Space Harrier and After Burner is made by the slave processor from the messages that the game's 68000 sends during its start-up animation. The
// reference is the slave's left PWM values in MAME while the logo sounds; the Machine's values have the same level in every second. `writesPerSample` is how often the
// slave writes the left register for one sample (three halfword writes in Space Harrier, one longword in After Burner).
static void CheckStartUpLogo(const char* xmlName, const char* referenceGame, std::uint64_t writesPerSample, std::uint64_t seconds) {
    std::printf("  start-up logo of %s\n", xmlName);
    const char* romRoot = GetEnvVar("CHIPESTRA_ROM_ROOT");
    const char* referenceDir = GetEnvVar("CHIPESTRA_REFERENCE_DIR");
    if (romRoot == nullptr || referenceDir == nullptr) {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT and CHIPESTRA_REFERENCE_DIR are needed\n");
        return;
    }
    std::ifstream in(std::filesystem::path(referenceDir) / "32x" / referenceGame / "real_game" / "logo_pwm_left.txt");
    if (!in) { std::printf("  SKIP: logo_pwm_left.txt is not in CHIPESTRA_REFERENCE_DIR\n"); return; }
    std::vector<int> expected;
    for (std::string line; std::getline(in, line);) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream values(line);
        for (int v; values >> v;) expected.push_back(v);
    }
    const auto xmlPath = FindDefinitionXml(xmlName);
    Check(xmlPath.has_value(), "the game definition is present (run from the repo root)");
    if (!xmlPath) return;
    std::ifstream xmlFile(*xmlPath, std::ios::binary);
    std::ostringstream xmlText;
    xmlText << xmlFile.rdbuf();
    const XmlParseResult parsed = ParseGameDefinition(xmlText.str());
    if (parsed.Status != XmlParseStatus::Ok) { Check(false, "the game definition parses"); return; }
    const ResourceResolver resolver{std::filesystem::path(romRoot)};
    ResourceResolutionResult res = resolver.Resolve(parsed.Definition.BuildRequiredResources());
    Check(res.Ok, "the cartridge is found under CHIPESTRA_ROM_ROOT");
    if (!res.Ok) return;
    const std::string driverXml = ExtractDriverElementXml(xmlText.str());
    res.Resources.Add(ResolvedResource("driver_xml", std::vector<std::uint8_t>(driverXml.begin(), driverXml.end())));
    std::unique_ptr<IMachine> machine = DefaultMachineRegistry().Create(MachineId(parsed.Definition.Machine), VariantId(parsed.Definition.Variant), res.Resources);
    machine->Initialize();
    auto* both = dynamic_cast<Sega32xMusicMachine*>(machine.get());
    Check(both != nullptr, "the machine is a Super 32X music machine");
    if (both == nullptr) return;
    std::vector<int> got;
    std::uint64_t writes = 0;
    Sega32xSystem* system = &both->PwmForTest().SystemForTest();
    system->SetWriteObserverForTest([&](int cpu, std::uint32_t address, std::uint32_t value, int) {
        // the left value is the halfword written (or the high half of a longword that also holds the right value); a sample is written writesPerSample times in a row
        if (cpu == 1 && (address & 0xFFFF) == 0x4034 && (writes++ % writesPerSample) == 0) got.push_back(static_cast<int>(value > 0xFFFF ? value >> 16 : value));
    });
    MusicCommand request;
    request.Kind = MusicCommandKind::Raw;
    request.Value = 0;
    request.Auxiliary = 0x40;   // the PWM unit
    machine->SendCommand(request);
    LogicalChannelSink sink;
    for (std::uint64_t done = 0; done < seconds * 192000; done += 256) {
        sink.ClearAll();
        machine->Render(EmuDuration(256), &sink);
    }
    // the reference starts a few values before the first change of the logo; find its first 16 values in what the Machine wrote
    std::size_t start = got.size();
    for (std::size_t i = 0; i + 16 <= got.size() && expected.size() >= 16 && start == got.size(); ++i)
        if (std::equal(expected.begin(), expected.begin() + 16, got.begin() + static_cast<std::ptrdiff_t>(i))) start = i;
    Check(start < got.size(), "the logo's first values are found in the Machine's PWM values");
    if (start >= got.size()) return;
    const std::vector<int> mine(got.begin() + static_cast<std::ptrdiff_t>(start), got.end());
    // the level of the whole logo (the rms of the values around the idle level 737) equals the original's within 1 %, and that of each second within 5 %
    double all = 0.0, allRef = 0.0;
    for (std::size_t block = 0; block * 15625 < expected.size() && (block + 1) * 15625 <= mine.size(); ++block) {
        double a = 0.0, b = 0.0;
        for (std::size_t i = block * 15625; i < (block + 1) * 15625 && i < expected.size(); ++i) {
            a += static_cast<double>(mine[i] - 737) * (mine[i] - 737);
            b += static_cast<double>(expected[i] - 737) * (expected[i] - 737);
        }
        all += a;
        allRef += b;
        char text[160];
        std::snprintf(text, sizeof(text), "second %zu of the logo: the level is the original's (%.1f, %.1f)", block + 1, std::sqrt(a / 15625.0), std::sqrt(b / 15625.0));
        Check(b > 0.0 && std::fabs(std::sqrt(a / b) - 1.0) < 0.05, text);
    }
    char total[160];
    std::snprintf(total, sizeof(total), "the level of the whole logo is the original's (ratio %.3f)", allRef > 0.0 ? std::sqrt(all / allRef) : 0.0);
    Check(allRef > 0.0 && std::fabs(std::sqrt(all / allRef) - 1.0) < 0.01, total);
    // the logo ends: nothing is written after it but the idle level
    bool silentAfter = true;
    for (std::size_t i = expected.size() + 400; i < mine.size(); ++i) silentAfter = silentAfter && mine[i] == 737;
    Check(silentAfter, "after the logo the PWM unit is idle");
}

void Sega32xSpaceHarrierLogoMatchesTheOriginal() {
    std::printf("Sega32xSpaceHarrierLogoMatchesTheOriginal\n");
    CheckStartUpLogo("sharrier.xml", "sharrier", 3, 5);
}

void Sega32xAfterBurnerLogoMatchesTheOriginal() {
    std::printf("Sega32xAfterBurnerLogoMatchesTheOriginal\n");
    CheckStartUpLogo("aburner.xml", "aburner", 1, 6);
}

// The sound driver of Virtua Fighter (in the 68000's RAM) leaves the numbers of the sampled sounds of its music (the drums) in two words of its work area; the
// game's own code hands them to the master processor once a frame. The reference is the master's queue in MAME while the song 0x82 plays in the attract mode.
void Sega32xMusicMachineHandsTheDriversSamplesToThePwmUnit() {
    std::printf("Sega32xMusicMachineHandsTheDriversSamplesToThePwmUnit\n");
    const char* romRoot = GetEnvVar("CHIPESTRA_ROM_ROOT");
    const char* referenceDir = GetEnvVar("CHIPESTRA_REFERENCE_DIR");
    if (romRoot == nullptr || referenceDir == nullptr) {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT and CHIPESTRA_REFERENCE_DIR are needed\n");
        return;
    }
    const auto xmlPath = FindDefinitionXml("vf.xml");
    Check(xmlPath.has_value(), "vf.xml is present (run from the repo root)");
    if (!xmlPath) return;
    std::ifstream xmlFile(*xmlPath, std::ios::binary);
    std::ostringstream xmlText;
    xmlText << xmlFile.rdbuf();
    const XmlParseResult parsed = ParseGameDefinition(xmlText.str());
    Check(parsed.Status == XmlParseStatus::Ok, "vf.xml parses");
    if (parsed.Status != XmlParseStatus::Ok) return;
    const ResourceResolver resolver{std::filesystem::path(romRoot)};
    ResourceResolutionResult res = resolver.Resolve(parsed.Definition.BuildRequiredResources());
    Check(res.Ok, "the cartridge is found under CHIPESTRA_ROM_ROOT");
    if (!res.Ok) return;
    const std::string driverXml = ExtractDriverElementXml(xmlText.str());
    res.Resources.Add(ResolvedResource("driver_xml", std::vector<std::uint8_t>(driverXml.begin(), driverXml.end())));

    struct Entry { double time; int number; };
    std::vector<Entry> expected;
    {
        std::ifstream in(std::filesystem::path(referenceDir) / "32x" / "vf" / "real_game" / "song82_pwm_queue.txt");
        Check(in.good(), "the recording of the original's queue is present");
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream words(line);
            Entry e;
            std::string number;
            words >> e.time >> number;
            e.number = static_cast<int>(std::strtoul(number.c_str(), nullptr, 16));
            expected.push_back(e);
        }
    }
    if (expected.empty()) return;

    std::unique_ptr<IMachine> machine = DefaultMachineRegistry().Create(MachineId(parsed.Definition.Machine), VariantId(parsed.Definition.Variant), res.Resources);
    auto* both = dynamic_cast<Sega32xMusicMachine*>(machine.get());
    Check(both != nullptr, "the Machine is the combined 32X Machine");
    if (both == nullptr) return;
    std::vector<Entry> found;
    both->SetEventObserverForTest([&](std::uint64_t ticks, const std::uint8_t* bytes, std::size_t count) {
        if (count == 1) found.push_back(Entry{static_cast<double>(ticks) / 192000.0, bytes[0]});
    });
    machine->Initialize();
    MusicCommand request;
    request.Kind = MusicCommandKind::Raw;
    request.Value = 0x82;
    machine->SendCommand(request);
    LogicalChannelSink sink;
    // the PWM channels' own output (before the mixer), one energy sum per 10 ms, to see that the handed-over sounds are heard and not only handed over
    std::vector<double> pwmEnergy(22 * 100 + 2, 0.0);
    std::vector<double> pwmCount(22 * 100 + 2, 0.0);
    for (std::uint64_t done = 0; done < 22 * 192000; done += 256) {
        sink.ClearAll();
        machine->Render(EmuDuration(256), &sink);
        for (const LogicalChannel& c : machine->LogicalChannels()) {
            if (c.DeviceName != "PWM") continue;
            const LogicalChannelBuffer& buffer = sink.ChannelBuffer(c.Id);
            for (std::size_t i = 0; i < buffer.Size(); ++i) {
                const std::size_t slot = static_cast<std::size_t>((done + i) / 1920);
                if (slot >= pwmEnergy.size()) continue;
                pwmEnergy[slot] += static_cast<double>(buffer.Data()[i]) * buffer.Data()[i];
                pwmCount[slot] += 1.0;
            }
        }
    }
    // the drum (the number 0x24, or 0x31 which the slave starts as 0x24) is heard: the PWM output after each hit (0.02 s to 0.14 s) is well above the silence before the first one
    std::size_t hits = 0, heard = 0;
    for (const Entry& f : found) {
        if (f.number != 0x24 && f.number != 0x31) continue;
        if (f.time < 3.0 || f.time > 20.0) continue;
        double energy = 0.0, count = 0.0;
        for (std::size_t slot = static_cast<std::size_t>(f.time * 100.0) + 2; slot < static_cast<std::size_t>(f.time * 100.0) + 14 && slot < pwmEnergy.size(); ++slot) {
            energy += pwmEnergy[slot];
            count += pwmCount[slot];
        }
        ++hits;
        if (count > 0.0 && std::sqrt(energy / count) > 0.004) ++heard;
    }
    std::printf("  %zu of the %zu drum hits are heard in the PWM output\n", heard, hits);
    Check(hits >= 10, "the drum is handed over many times");
    Check(heard * 10 >= hits * 9, "the PWM output is not silent after the drum hits");
    // every number that the driver hands over is in the original's record, within a few frames of time (the game's code does it once a frame); the slave starts the numbers 0x31 and 0x32 as 0x24 and 0x26
    std::size_t matched = 0;
    for (const Entry& f : found) {
        for (const Entry& e : expected) {
            if ((e.number == f.number || e.number == (f.number == 0x31 ? 0x24 : f.number == 0x32 ? 0x26 : f.number)) && std::abs(e.time - f.time) < 0.05) { ++matched; break; }
        }
    }
    std::printf("  %zu of the %zu numbers handed over are in the original's queue (the queue has %zu in all)\n", matched, found.size(), expected.size());
    Check(found.size() >= 40, "the driver hands over the sounds of the music (the drums)");
    Check(matched == found.size(), "each of them is in the original's queue at about the same time");
}
