// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Native-only lifecycle/determinism tests.
//
// Render() and the audio output pipeline have no full C ABI exposure without
// a real UI driving them, so most determinism checks call the chipestra_* ABI
// functions and internal types directly from native code. Built and run as part
// of the Native step of tools/verify.ps1.
//
// Plain assert-style checks are used deliberately. The deterministic checks do
// not wait on wall-clock time.

#include "chipestra_abi.h"

#include "address_space.h"
#include "bounded_frame_queue.h"
#include "clock.h"
#include "device_manager.h"
#include "machine_registry.h"
#include "memory_region.h"
#include "mixer.h"
#include "endpoint_resampler.h"
#include "null_audio_output.h"
#include "resource_resolver.h"
#include "scheduler.h"
#include "test_machine.h"
#include "wasapi_shared_output.h"

#include "afterburner2_reference_tests.h"
#include "ay8910_adapter_tests.h"
#include "game_definition_tests.h"
#include "gradius_machine_tests.h"
#include "gradius_reference_tests.h"
#include "k005289_adapter_tests.h"
#include "vlm5030_adapter_tests.h"
#include "resource_resolver_tests.h"
#include "sample_rate_converter_tests.h"
#include "segapcm_adapter_tests.h"
#include "outrun_machine_tests.h"
#include "outrun_reference_tests.h"
#include "test_support.h"
#include "xboard_machine_tests.h"
#include "ym2151_adapter_tests.h"
#include "ym2203_adapter_tests.h"
#include "msm5205_adapter_tests.h"
#include "darius_command_interface_tests.h"
#include "darius_machine_tests.h"
#include "darius_reference_tests.h"
#include "ym2610_adapter_tests.h"
#include "ninja_warriors_machine_tests.h"
#include "ninja_warriors_reference_tests.h"
#include "metalblack_machine_tests.h"
#include "metalblack_reference_tests.h"
#include "z80_adapter_tests.h"
#include "m68000_adapter_tests.h"
#include "i8039_adapter_tests.h"
#include "h6280_adapter_tests.h"
#include "m6809_adapter_tests.h"
#include "hd63701_adapter_tests.h"
#include "okim6295_adapter_tests.h"
#include "gyruss_machine_tests.h"
#include "bubble_system_bios_tests.h"
#include "gyruss_reference_tests.h"
#include "vapor_trail_machine_tests.h"
#include "vapor_trail_reference_tests.h"
#include "cus30_adapter_tests.h"
#include "dac8bitr2r_adapter_tests.h"
#include "dragon_spirit_machine_tests.h"
#include "sf_machine_tests.h"
#include "sf_memory_map_tests.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

bool NearlyEqual(float a, float b) {
    float diff = a - b;
    if (diff < 0.0f) {
        diff = -diff;
    }
    return diff < 1e-6f;
}

// Waits (bounded) for `handle`'s selected output to report at least
// `minFrames` submitted. PlaybackPump renders asynchronously on its own thread,
// so no single call both renders and reports; bounded polling with short sleeps
// observes progress without making the deterministic checks time-dependent.
bool WaitForSubmittedFrames(ChipestraHandle handle, std::uint64_t minFrames, int timeoutMs) {
    constexpr int kPollIntervalMs = 5;
    for (int waited = 0; waited <= timeoutMs; waited += kPollIntervalMs) {
        ChipestraAudioOutputDiagnostics diagnostics{};
        if (chipestra_get_audio_output_diagnostics(handle, &diagnostics) == CHIPESTRA_OK &&
            diagnostics.submitted_frame_count >= minFrames) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
    }
    return false;
}

// Part A: TestMachine's own determinism, independent of the C ABI/instance lifecycle.
void TestMachineDeterminism() {
    std::printf("TestMachineDeterminism\n");

    constexpr std::uint64_t kRenderCount = 10;

    TestMachine machine;
    machine.Initialize();
    Check(machine.LogicalChannels().size() == 1, "LogicalChannels().size() == 1");

    for (int iteration = 0; iteration < 3; ++iteration) {
        machine.Reset();
        Check(machine.RenderCallCount() == 0, "RenderCallCount() == 0 right after Reset()");
        for (std::uint64_t i = 0; i < kRenderCount; ++i) {
            machine.Render(EmuDuration(1), nullptr);
        }
        Check(machine.RenderCallCount() == kRenderCount,
              "RenderCallCount() == kRenderCount after kRenderCount Render() calls");
    }

    machine.Shutdown();
}

// Part B: the full Created -> MachineLoaded -> Created ABI cycle, repeated,
// must return identical result codes every time.
void AbiCycleDeterminism() {
    std::printf("AbiCycleDeterminism\n");

    for (int iteration = 0; iteration < 3; ++iteration) {
        ChipestraHandle handle = nullptr;
        Check(chipestra_create_instance(&handle) == CHIPESTRA_OK, "chipestra_create_instance() == CHIPESTRA_OK");
        Check(handle != nullptr, "handle != nullptr after create");

        Check(chipestra_load_machine(handle, "TEST_MACHINE", "STANDARD", nullptr, 0) == CHIPESTRA_OK,
              "chipestra_load_machine(TEST_MACHINE) == CHIPESTRA_OK");
        Check(chipestra_reset_machine(handle) == CHIPESTRA_OK, "chipestra_reset_machine() == CHIPESTRA_OK");
        Check(chipestra_unload_machine(handle) == CHIPESTRA_OK, "chipestra_unload_machine() == CHIPESTRA_OK");

        chipestra_destroy_instance(handle);
    }
}

// Part C: invalid state transitions and an unknown machine_id are rejected
// with the documented error codes, not a crash.
void InvalidTransitionsAndUnknownMachine() {
    std::printf("InvalidTransitionsAndUnknownMachine\n");

    ChipestraHandle handle = nullptr;
    Check(chipestra_create_instance(&handle) == CHIPESTRA_OK, "setup: chipestra_create_instance() == CHIPESTRA_OK");

    // Created state: Reset/Unload are not valid yet.
    Check(chipestra_reset_machine(handle) == CHIPESTRA_E_INVALID_STATE,
          "chipestra_reset_machine() on Created == CHIPESTRA_E_INVALID_STATE");
    Check(chipestra_unload_machine(handle) == CHIPESTRA_E_INVALID_STATE,
          "chipestra_unload_machine() on Created == CHIPESTRA_E_INVALID_STATE");

    // Unknown machine_id.
    Check(chipestra_load_machine(handle, "NO_SUCH_MACHINE", "STANDARD", nullptr, 0) == CHIPESTRA_E_NOT_FOUND,
          "chipestra_load_machine(unknown id) == CHIPESTRA_E_NOT_FOUND");

    // Null-argument safety.
    Check(chipestra_load_machine(nullptr, "TEST_MACHINE", "STANDARD", nullptr, 0) == CHIPESTRA_E_INVALID_ARGUMENT,
          "chipestra_load_machine(null handle) == CHIPESTRA_E_INVALID_ARGUMENT");
    Check(chipestra_load_machine(handle, nullptr, "STANDARD", nullptr, 0) == CHIPESTRA_E_INVALID_ARGUMENT,
          "chipestra_load_machine(null machine_id) == CHIPESTRA_E_INVALID_ARGUMENT");
    Check(chipestra_load_machine(handle, "TEST_MACHINE", nullptr, nullptr, 0) == CHIPESTRA_E_INVALID_ARGUMENT,
          "chipestra_load_machine(null variant_id) == CHIPESTRA_E_INVALID_ARGUMENT");
    Check(chipestra_unload_machine(nullptr) == CHIPESTRA_E_INVALID_ARGUMENT,
          "chipestra_unload_machine(null handle) == CHIPESTRA_E_INVALID_ARGUMENT");
    Check(chipestra_reset_machine(nullptr) == CHIPESTRA_E_INVALID_ARGUMENT,
          "chipestra_reset_machine(null handle) == CHIPESTRA_E_INVALID_ARGUMENT");

    // Now actually load, then check the reverse invalid transition.
    Check(chipestra_load_machine(handle, "TEST_MACHINE", "STANDARD", nullptr, 0) == CHIPESTRA_OK, "setup: chipestra_load_machine() == CHIPESTRA_OK");
    Check(chipestra_load_machine(handle, "TEST_MACHINE", "STANDARD", nullptr, 0) == CHIPESTRA_E_INVALID_STATE,
          "chipestra_load_machine() on MachineLoaded == CHIPESTRA_E_INVALID_STATE");

    // MachineLoaded -> Dead is a valid transition (destroy without unload
    // first); the registry-based liveness check must then reject any further
    // use of the handle rather than touching freed memory.
    chipestra_destroy_instance(handle);
    Check(chipestra_reset_machine(handle) == CHIPESTRA_E_INVALID_STATE,
          "chipestra_reset_machine() on a destroyed handle == CHIPESTRA_E_INVALID_STATE");
    Check(chipestra_load_machine(handle, "TEST_MACHINE", "STANDARD", nullptr, 0) == CHIPESTRA_E_INVALID_STATE,
          "chipestra_load_machine() on a destroyed handle == CHIPESTRA_E_INVALID_STATE");
    Check(chipestra_unload_machine(handle) == CHIPESTRA_E_INVALID_STATE,
          "chipestra_unload_machine() on a destroyed handle == CHIPESTRA_E_INVALID_STATE");
}

// Part D: ClockConverter round-trip, for several (globalTicksPerSecond,
// deviceRate) patterns, run repeatedly to confirm the conversion is both exact
// and deterministic.
void ClockConverterRoundTrip() {
    std::printf("ClockConverterRoundTrip\n");

    struct Pattern {
        std::uint64_t global_ticks_per_second;
        std::uint64_t device_hz;
    };
    const Pattern patterns[] = {
        {4'000'000, 1'000'000},   // 4 ticks per cycle
        {44'100 * 512, 44'100},   // 512 ticks per cycle
        {1'000'000, 4'000'000},   // 1 tick per 4 cycles
        {3'579'545, 3'579'545},   // 1:1
    };

    for (const Pattern& pattern : patterns) {
        ClockConverter converter(pattern.global_ticks_per_second, ClockRate(pattern.device_hz));

        // 4 and 1000 are both multiples of 4, which is the largest reduced
        // ticks-per-cycle denominator among the patterns above (pattern 3:
        // 1 tick per 4 cycles) - required for cycles -> ticks -> cycles to
        // round-trip exactly rather than lose precision to integer division
        // (documented on ClockConverter itself).
        for (std::uint64_t cycles : {std::uint64_t{0}, std::uint64_t{4}, std::uint64_t{1000}}) {
            for (int run = 0; run < 3; ++run) {
                EmuDuration ticks = converter.CyclesToTicks(cycles);
                std::uint64_t round_tripped = converter.TicksToCycles(ticks);
                Check(round_tripped == cycles, "CyclesToTicks/TicksToCycles round-trips exactly");
            }
        }
    }
}

// Part E: Scheduler orders events deterministically - by time, then by
// registration order among ties - across repeated runs.
void SchedulerDeterminism() {
    std::printf("SchedulerDeterminism\n");

    for (int run = 0; run < 3; ++run) {
        Scheduler scheduler;
        Scheduler::EventId a = scheduler.ScheduleAt(EmuTime(100));
        Scheduler::EventId b = scheduler.ScheduleAt(EmuTime(50));
        Scheduler::EventId c = scheduler.ScheduleAt(EmuTime(50));  // ties with b, registered after
        Scheduler::EventId d = scheduler.ScheduleAt(EmuTime(200));

        Check(scheduler.HasPendingEvent(), "HasPendingEvent() true after scheduling");
        Check(scheduler.NextEventTime() == EmuTime(50), "NextEventTime() == earliest (50)");

        Scheduler::EventId first = scheduler.PopNextEvent();
        Scheduler::EventId second = scheduler.PopNextEvent();
        Scheduler::EventId third = scheduler.PopNextEvent();
        Scheduler::EventId fourth = scheduler.PopNextEvent();

        Check(first == b, "pop order: b (t=50, registered first) before c");
        Check(second == c, "pop order: c (t=50, registered second)");
        Check(third == a, "pop order: a (t=100)");
        Check(fourth == d, "pop order: d (t=200) last");
        Check(!scheduler.HasPendingEvent(), "HasPendingEvent() false once drained");
    }

    // Cancel() removes a pending event without disturbing the rest.
    Scheduler scheduler;
    Scheduler::EventId keep = scheduler.ScheduleAt(EmuTime(10));
    Scheduler::EventId cancelled = scheduler.ScheduleAt(EmuTime(5));
    scheduler.Cancel(cancelled);
    Check(scheduler.NextEventTime() == EmuTime(10), "Cancel() removes the cancelled event");
    Check(scheduler.PopNextEvent() == keep, "surviving event still pops correctly after Cancel()");
}

// Part F: AddressSpace maps MemoryRegions, rejects overlapping registrations,
// and defers unmapped-access behavior to the caller-supplied handlers.
void AddressSpaceBehavior() {
    std::printf("AddressSpaceBehavior\n");

    MemoryRegion ram(0x1000, /*read_only=*/false);
    MemoryRegion rom(0x1000, /*read_only=*/true);

    int unmapped_read_calls = 0;
    int unmapped_write_calls = 0;
    AddressSpace space(
        [&unmapped_read_calls](std::uint64_t) {
            ++unmapped_read_calls;
            return std::uint8_t{0xFF};
        },
        [&unmapped_write_calls](std::uint64_t, std::uint8_t) { ++unmapped_write_calls; });

    space.Map(0x0000, 0x1000, &ram);
    space.Map(0x1000, 0x1000, &rom);

    bool overlap_rejected = false;
    try {
        space.Map(0x0800, 0x100, &ram);  // overlaps the RAM mapping above
    } catch (const std::logic_error&) {
        overlap_rejected = true;
    }
    Check(overlap_rejected, "Map() throws on an overlapping range");

    space.Write8(0x0010, 0x42);
    Check(space.Read8(0x0010) == 0x42, "mapped RAM read reflects a prior mapped write");

    bool rom_write_rejected = false;
    try {
        space.Write8(0x1000, 0x99);  // ROM region is read-only
    } catch (const std::logic_error&) {
        rom_write_rejected = true;
    }
    Check(rom_write_rejected, "writing a read-only-mapped region throws");

    Check(unmapped_read_calls == 0, "setup: no unmapped reads yet");
    (void)space.Read8(0x5000);  // well past both mappings
    Check(unmapped_read_calls == 1, "unmapped read dispatches to the caller-supplied handler");

    Check(unmapped_write_calls == 0, "setup: no unmapped writes yet");
    space.Write8(0x5000, 0x77);
    Check(unmapped_write_calls == 1, "unmapped write dispatches to the caller-supplied handler");
}

// Part G: DeviceManager exclusively owns Devices (no shared_ptr) and hands out
// non-owning references.
struct DummyDevice final : IDevice {};

void DeviceManagerOwnership() {
    std::printf("DeviceManagerOwnership\n");

    DeviceManager manager;
    Check(manager.Count() == 0, "DeviceManager starts empty");

    IDevice* first = manager.Add(std::make_unique<DummyDevice>());
    IDevice* second = manager.Add(std::make_unique<DummyDevice>());

    Check(manager.Count() == 2, "Count() == 2 after adding two devices");
    Check(first != nullptr && second != nullptr && first != second,
          "Add() returns distinct non-null non-owning references");
    // manager going out of scope destroys both owned devices; nothing else
    // holds ownership, so there is no double-free/leak to detect here beyond
    // this not crashing.
}

// Part H: linear pan law matches the documented formula at several pan values,
// including the two named extremes.
void MixerPanLaw() {
    std::printf("MixerPanLaw\n");

    const std::size_t kFrames = 4;
    const float kSampleValue = 1.0f;

    struct PanCase {
        float pan;
        float expected_left;
        float expected_right;
    };
    const PanCase cases[] = {
        {-1.0f, 1.0f, 0.0f},   // pan=-1 -> right ch=0
        {-0.5f, 0.75f, 0.25f},
        {0.0f, 0.5f, 0.5f},    // pan=0 -> left/right equal
        {0.5f, 0.25f, 0.75f},
        {1.0f, 0.0f, 1.0f},    // pan=+1 -> left ch=0
    };

    for (const PanCase& c : cases) {
        LogicalChannelSink sink;
        sink.RegisterChannel(0);
        std::vector<float> samples(kFrames, kSampleValue);
        sink.WriteSamples(0, samples.data(), samples.size());

        std::vector<LogicalChannel> channels(1);
        channels[0].Id = 0;
        channels[0].Pan = c.pan;

        Mixer mixer;
        std::vector<StereoFrame> out;
        mixer.MixBlock(sink, channels, kFrames, out);

        bool all_match = true;
        for (const StereoFrame& frame : out) {
            all_match = all_match && NearlyEqual(frame.left, c.expected_left) &&
                        NearlyEqual(frame.right, c.expected_right);
        }
        Check(all_match, "pan law matches expected L/R gains for this pan value");
    }
}

// Part I: a muted channel contributes nothing to the mix, and its Peak/RMS
// reflect that (zero).
void MixerMute() {
    std::printf("MixerMute\n");

    const std::size_t kFrames = 4;

    LogicalChannelSink sink;
    sink.RegisterChannel(0);
    std::vector<float> samples(kFrames, 1.0f);
    sink.WriteSamples(0, samples.data(), samples.size());

    std::vector<LogicalChannel> channels(1);
    channels[0].Id = 0;
    channels[0].Pan = 0.0f;
    channels[0].Mute = true;

    Mixer mixer;
    std::vector<StereoFrame> out;
    mixer.MixBlock(sink, channels, kFrames, out);

    bool all_silent = true;
    for (const StereoFrame& frame : out) {
        all_silent = all_silent && frame.left == 0.0f && frame.right == 0.0f;
    }
    Check(all_silent, "muted channel contributes zero to every output frame");
    Check(channels[0].Peak == 0.0f, "muted channel's Peak is zero");
    Check(channels[0].RMS == 0.0f, "muted channel's RMS is zero");
}

// Part J: two channels' contributions sum linearly.
void MixerLinearSummation() {
    std::printf("MixerLinearSummation\n");

    const std::size_t kFrames = 4;

    LogicalChannelSink sink;
    sink.RegisterChannel(0);
    sink.RegisterChannel(1);
    std::vector<float> samples_a(kFrames, 1.0f);
    std::vector<float> samples_b(kFrames, 0.5f);
    sink.WriteSamples(0, samples_a.data(), samples_a.size());
    sink.WriteSamples(1, samples_b.data(), samples_b.size());

    std::vector<LogicalChannel> channels(2);
    channels[0].Id = 0;
    channels[1].Id = 1;
    // Both channels default to Pan=0, Gain=1, Mute=false.

    Mixer mixer;
    std::vector<StereoFrame> out;
    mixer.MixBlock(sink, channels, kFrames, out);

    // pan=0 -> each channel contributes half its sample to each side:
    // 0.5*1.0 + 0.5*0.5 == 0.75.
    bool all_match = true;
    for (const StereoFrame& frame : out) {
        all_match = all_match && NearlyEqual(frame.left, 0.75f) && NearlyEqual(frame.right, 0.75f);
    }
    Check(all_match, "two channels' contributions sum linearly");
}

// Part K: TestMachine's Render() output flows through a real
// LogicalChannelSink into the Mixer end to end.
void TestMachineToMixerIntegration() {
    std::printf("TestMachineToMixerIntegration\n");

    const std::size_t kFrames = 8;

    TestMachine machine;
    machine.Initialize();
    LogicalChannelSink sink;
    machine.Render(EmuDuration(kFrames), &sink);

    Check(sink.HasChannel(TestMachine::kChannelId), "TestMachine registered its channel in the sink");
    Check(sink.ChannelBuffer(TestMachine::kChannelId).Size() == kFrames,
          "TestMachine wrote exactly kFrames samples");

    std::vector<LogicalChannel> channels(1);
    channels[0].Id = TestMachine::kChannelId;

    Mixer mixer;
    std::vector<StereoFrame> out;
    mixer.MixBlock(sink, channels, kFrames, out);

    // TestMachine's fixed pattern is {0.5, -0.5, 0.25, -0.25} repeating;
    // pan=0 halves it on each side.
    const float kExpectedPattern[4] = {0.25f, -0.25f, 0.125f, -0.125f};
    bool all_match = true;
    for (std::size_t i = 0; i < kFrames; ++i) {
        float expected = kExpectedPattern[i % 4];
        all_match = all_match && NearlyEqual(out[i].left, expected) && NearlyEqual(out[i].right, expected);
    }
    Check(all_match, "Mixer output matches TestMachine's fixed pattern (pan=0)");
}

// Part L: the full TestMachine -> Sink -> Mixer pipeline produces identical
// StereoFrame output across repeated runs, for the same fixed pan/mute/gain
// configuration and render duration.
void MixerPipelineDeterminism() {
    std::printf("MixerPipelineDeterminism\n");

    std::vector<StereoFrame> first_run_output;

    for (int run = 0; run < 3; ++run) {
        TestMachine machine;
        machine.Initialize();
        LogicalChannelSink sink;
        machine.Render(EmuDuration(Mixer::kBlockSizeFrames), &sink);

        std::vector<LogicalChannel> channels(1);
        channels[0].Id = TestMachine::kChannelId;
        channels[0].Pan = 0.3f;
        channels[0].Gain = 0.8f;

        Mixer mixer;
        std::vector<StereoFrame> out;
        mixer.MixBlock(sink, channels, Mixer::kBlockSizeFrames, out);

        if (run == 0) {
            first_run_output = out;
            Check(out.size() == Mixer::kBlockSizeFrames, "output has kBlockSizeFrames frames");
            continue;
        }

        bool matches = out.size() == first_run_output.size();
        for (std::size_t i = 0; matches && i < out.size(); ++i) {
            matches = out[i].left == first_run_output[i].left && out[i].right == first_run_output[i].right;
        }
        Check(matches, "run reproduces the first run's output exactly");
    }
}

// EndpointResampler - the canonical 192kHz -> endpoint-rate SRC used inside
// WasapiSharedOutput. Tested here (no real device needed) so the down-rate path
// is covered even on a dev box whose endpoint runs at 192kHz.
void EndpointResamplerBehavior() {
    std::printf("EndpointResamplerBehavior\n");

    const std::size_t block = Mixer::kBlockSizeFrames;  // 256
    std::vector<StereoFrame> canon(block);

    // 1. passthrough at the canonical rate: identity copy, same count.
    {
        EndpointResampler r(Mixer::kCanonicalSampleRateHz);
        Check(r.IsPassthrough(), "endpoint SRC: 192kHz endpoint is passthrough");
        for (std::size_t i = 0; i < block; ++i) canon[i] = StereoFrame{0.25f, -0.5f};
        const auto& out = r.ProcessBlock(canon.data(), block);
        Check(out.size() == block && out[0].left == 0.25f && out[0].right == -0.5f,
              "endpoint SRC: passthrough returns the block unchanged");
    }

    // 2. 48kHz: exactly 64 output frames per 256-frame block, and a 1kHz sine
    //    stays 1kHz (counted via zero crossings over ~1s of output).
    {
        EndpointResampler r(48000);
        Check(!r.IsPassthrough(), "endpoint SRC: 48kHz endpoint resamples");
        std::size_t totalOut = 0;
        std::uint64_t phase = 0;
        int crossings = 0;
        float prev = 0.0f;
        bool havePrev = false;
        for (int b = 0; b < 750; ++b) {  // ~1s of canonical audio
            for (std::size_t i = 0; i < block; ++i, ++phase) {
                const float s = std::sin(2.0f * 3.14159265f * 1000.0f * static_cast<float>(phase) /
                                         static_cast<float>(Mixer::kCanonicalSampleRateHz));
                canon[i] = StereoFrame{s, s};
            }
            const auto& out = r.ProcessBlock(canon.data(), block);
            if (b == 0) Check(out.size() == 64, "endpoint SRC: 256 canonical frames -> 64 at 48kHz");
            totalOut += out.size();
            for (const StereoFrame& f : out) {
                if (havePrev && ((prev < 0.0f) != (f.left < 0.0f))) ++crossings;
                prev = f.left;
                havePrev = true;
            }
        }
        Check(totalOut == 750 * 64, "endpoint SRC: 48kHz output frame count is exact over many blocks");
        // 1kHz -> ~2000 zero crossings/sec; allow slack for the window edges.
        Check(crossings >= 1960 && crossings <= 2040, "endpoint SRC: 48kHz output holds the 1kHz tone");
    }

    // 3. 44.1kHz: non-integer ratio - output count carries the remainder, so
    //    the long-run total tracks the rate to within a frame or two.
    {
        EndpointResampler r(44100);
        std::size_t totalOut = 0;
        for (int b = 0; b < 400; ++b) {
            for (std::size_t i = 0; i < block; ++i) canon[i] = StereoFrame{0.1f, 0.1f};
            totalOut += r.ProcessBlock(canon.data(), block).size();
        }
        const double expected = 400.0 * block * 44100.0 / Mixer::kCanonicalSampleRateHz;
        Check(std::fabs(static_cast<double>(totalOut) - expected) <= 2.0,
              "endpoint SRC: 44.1kHz total output frame count tracks the rate (fractional carry)");
    }

    // 4. determinism: same input sequence -> identical output.
    {
        auto run = [&](std::vector<StereoFrame>& acc) {
            EndpointResampler r(48000);
            for (int b = 0; b < 50; ++b) {
                for (std::size_t i = 0; i < block; ++i)
                    canon[i] = StereoFrame{static_cast<float>((b + i) % 7) * 0.1f - 0.3f, 0.2f};
                const auto& out = r.ProcessBlock(canon.data(), block);
                acc.insert(acc.end(), out.begin(), out.end());
            }
        };
        std::vector<StereoFrame> a, c;
        run(a);
        run(c);
        bool same = a.size() == c.size();
        for (std::size_t i = 0; same && i < a.size(); ++i)
            same = a[i].left == c[i].left && a[i].right == c[i].right;
        Check(same, "endpoint SRC: deterministic across runs");
    }
}

// Part M: NullAudioOutput opens no real device and does nothing but count
// submitted frames.
void NullAudioOutputBehavior() {
    std::printf("NullAudioOutputBehavior\n");

    NullAudioOutput output;
    output.Initialize();
    output.Start();

    Check(output.SubmittedFrameCount() == 0, "SubmittedFrameCount() == 0 before any submission");

    StereoFrame frames[4] = {};
    output.SubmitStereo(frames, 4);
    Check(output.SubmittedFrameCount() == 4, "SubmittedFrameCount() == 4 after submitting 4 frames");

    output.SubmitStereo(frames, 4);
    Check(output.SubmittedFrameCount() == 8, "SubmittedFrameCount() accumulates across calls");

    output.Stop();
    output.Shutdown();
}

// Part N: the full Created -> MachineLoaded -> Ready -> Playing -> Paused ->
// Playing -> Ready -> ... -> Dead cycle through the C ABI, plus the invalid
// transitions guarded along the way. PlaybackPump owns rendering once
// chipestra_start_playback is called, so progress is observed by polling
// diagnostics (WaitForSubmittedFrames) rather than a synchronous call.
void PlaybackStateMachine() {
    std::printf("PlaybackStateMachine\n");

    ChipestraHandle handle = nullptr;
    Check(chipestra_create_instance(&handle) == CHIPESTRA_OK, "setup: chipestra_create_instance() == CHIPESTRA_OK");
    Check(chipestra_load_machine(handle, "TEST_MACHINE", "STANDARD", nullptr, 0) == CHIPESTRA_OK, "setup: chipestra_load_machine() == CHIPESTRA_OK");

    Check(chipestra_start_playback(handle) == CHIPESTRA_E_INVALID_STATE,
          "chipestra_start_playback() before Ready == CHIPESTRA_E_INVALID_STATE");
    Check(chipestra_start_audio_output(handle) == CHIPESTRA_E_INVALID_STATE,
          "chipestra_start_audio_output() before an output is selected == CHIPESTRA_E_INVALID_STATE");

    Check(chipestra_select_null_output(handle) == CHIPESTRA_OK, "chipestra_select_null_output() == CHIPESTRA_OK");
    Check(chipestra_select_null_output(handle) == CHIPESTRA_E_INVALID_STATE,
          "chipestra_select_null_output() again with one already selected == CHIPESTRA_E_INVALID_STATE");

    Check(chipestra_start_audio_output(handle) == CHIPESTRA_OK,
          "chipestra_start_audio_output() == CHIPESTRA_OK (MachineLoaded -> Ready)");
    Check(chipestra_start_audio_output(handle) == CHIPESTRA_E_INVALID_STATE,
          "chipestra_start_audio_output() again while Ready == CHIPESTRA_E_INVALID_STATE");

    Check(chipestra_pause_playback(handle) == CHIPESTRA_E_INVALID_STATE,
          "chipestra_pause_playback() before Playing == CHIPESTRA_E_INVALID_STATE");
    Check(chipestra_stop_playback(handle) == CHIPESTRA_E_INVALID_STATE,
          "chipestra_stop_playback() before Playing == CHIPESTRA_E_INVALID_STATE");

    Check(chipestra_start_playback(handle) == CHIPESTRA_OK, "chipestra_start_playback() == CHIPESTRA_OK (Ready -> Playing)");
    Check(chipestra_start_playback(handle) == CHIPESTRA_E_INVALID_STATE,
          "chipestra_start_playback() again while Playing == CHIPESTRA_E_INVALID_STATE");
    Check(WaitForSubmittedFrames(handle, Mixer::kBlockSizeFrames, 1000),
          "PlaybackPump renders and submits at least one block within the timeout");

    // chipestra_get_machine_channel_count is safe to call while Playing,
    // concurrently with the PlaybackPump thread actively rendering (see its
    // comment in chipestra_abi.h).
    uint32_t playingChannelCount = 0;
    Check(chipestra_get_machine_channel_count(handle, &playingChannelCount) == CHIPESTRA_OK && playingChannelCount == 1,
          "chipestra_get_machine_channel_count() while Playing == CHIPESTRA_OK, TestMachine has 1 channel");

    Check(chipestra_pause_playback(handle) == CHIPESTRA_OK, "chipestra_pause_playback() == CHIPESTRA_OK (Playing -> Paused)");
    Check(chipestra_pause_playback(handle) == CHIPESTRA_E_INVALID_STATE,
          "chipestra_pause_playback() again while Paused == CHIPESTRA_E_INVALID_STATE");
    Check(chipestra_start_playback(handle) == CHIPESTRA_E_INVALID_STATE,
          "chipestra_start_playback() while Paused == CHIPESTRA_E_INVALID_STATE");

    // chipestra_pause_playback() only signals the worker; a render already in
    // flight at that exact moment still completes (at most one block's
    // worth). Let that settle before taking the baseline, so the two
    // snapshots below are both unambiguously "after pause".
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ChipestraAudioOutputDiagnostics paused_diagnostics{};
    chipestra_get_audio_output_diagnostics(handle, &paused_diagnostics);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ChipestraAudioOutputDiagnostics still_paused_diagnostics{};
    chipestra_get_audio_output_diagnostics(handle, &still_paused_diagnostics);
    Check(still_paused_diagnostics.submitted_frame_count == paused_diagnostics.submitted_frame_count,
          "no new frames are submitted while Paused");

    Check(chipestra_resume_playback(handle) == CHIPESTRA_OK, "chipestra_resume_playback() == CHIPESTRA_OK (Paused -> Playing)");
    Check(chipestra_resume_playback(handle) == CHIPESTRA_E_INVALID_STATE,
          "chipestra_resume_playback() again while Playing == CHIPESTRA_E_INVALID_STATE");
    Check(WaitForSubmittedFrames(handle, paused_diagnostics.submitted_frame_count + Mixer::kBlockSizeFrames, 1000),
          "playback resumes rendering after chipestra_resume_playback()");

    Check(chipestra_stop_playback(handle) == CHIPESTRA_OK, "chipestra_stop_playback() == CHIPESTRA_OK (Playing -> Ready)");
    Check(chipestra_stop_playback(handle) == CHIPESTRA_E_INVALID_STATE,
          "chipestra_stop_playback() again while Ready == CHIPESTRA_E_INVALID_STATE");

    ChipestraAudioOutputDiagnostics stopped_diagnostics{};
    chipestra_get_audio_output_diagnostics(handle, &stopped_diagnostics);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ChipestraAudioOutputDiagnostics still_stopped_diagnostics{};
    chipestra_get_audio_output_diagnostics(handle, &still_stopped_diagnostics);
    Check(still_stopped_diagnostics.submitted_frame_count == stopped_diagnostics.submitted_frame_count,
          "no new frames are submitted after chipestra_stop_playback() - the worker thread actually stopped");

    Check(chipestra_stop_audio_output(handle) == CHIPESTRA_OK,
          "chipestra_stop_audio_output() == CHIPESTRA_OK (Ready -> MachineLoaded)");
    Check(chipestra_stop_audio_output(handle) == CHIPESTRA_E_INVALID_STATE,
          "chipestra_stop_audio_output() again == CHIPESTRA_E_INVALID_STATE (already MachineLoaded)");

    Check(chipestra_unload_machine(handle) == CHIPESTRA_OK, "chipestra_unload_machine() == CHIPESTRA_OK after stopping output");

    chipestra_destroy_instance(handle);
}

// chipestra_send_command's state gating - valid outside of Playing (the
// PlaybackPump thread owns the Machine once playback starts), rejected during
// Playing, and argument-validated independent of state.
void SendCommandAbi() {
    std::printf("SendCommandAbi\n");

    Check(chipestra_send_command(nullptr, 2, 0, 0) == CHIPESTRA_E_INVALID_ARGUMENT,
          "send_command(null handle) == CHIPESTRA_E_INVALID_ARGUMENT");

    ChipestraHandle handle = nullptr;
    Check(chipestra_create_instance(&handle) == CHIPESTRA_OK, "setup: chipestra_create_instance() == CHIPESTRA_OK");

    Check(chipestra_send_command(handle, 2, 0x92, 0) == CHIPESTRA_E_INVALID_STATE,
          "send_command before a Machine is loaded (Created) == CHIPESTRA_E_INVALID_STATE");

    Check(chipestra_load_machine(handle, "TEST_MACHINE", "STANDARD", nullptr, 0) == CHIPESTRA_OK, "setup: chipestra_load_machine() == CHIPESTRA_OK");
    Check(chipestra_send_command(handle, 4, 0, 0) == CHIPESTRA_E_INVALID_ARGUMENT,
          "send_command(kind=4, out of MusicCommandKind range) == CHIPESTRA_E_INVALID_ARGUMENT");
    Check(chipestra_send_command(handle, 255, 0, 0) == CHIPESTRA_E_INVALID_ARGUMENT,
          "send_command(kind=255) == CHIPESTRA_E_INVALID_ARGUMENT");
    Check(chipestra_send_command(handle, 3, 0, 0) == CHIPESTRA_E_UNSUPPORTED_COMMAND,
          "send_command(kind=Wsg) to a Machine that has no WSG requests == CHIPESTRA_E_UNSUPPORTED_COMMAND");
    Check(chipestra_send_command(handle, 2, 0x92, 0) == CHIPESTRA_OK, "send_command(Raw) while MachineLoaded == CHIPESTRA_OK");

    Check(chipestra_select_null_output(handle) == CHIPESTRA_OK, "setup: chipestra_select_null_output() == CHIPESTRA_OK");
    Check(chipestra_start_audio_output(handle) == CHIPESTRA_OK, "setup: chipestra_start_audio_output() == CHIPESTRA_OK (-> Ready)");
    Check(chipestra_send_command(handle, 0, 1, 0) == CHIPESTRA_OK, "send_command(TrackSelect) while Ready == CHIPESTRA_OK");

    Check(chipestra_start_playback(handle) == CHIPESTRA_OK, "setup: chipestra_start_playback() == CHIPESTRA_OK (-> Playing)");
    Check(chipestra_send_command(handle, 2, 0x93, 0) == CHIPESTRA_E_INVALID_STATE,
          "send_command while Playing == CHIPESTRA_E_INVALID_STATE (pump thread owns the Machine)");

    Check(chipestra_pause_playback(handle) == CHIPESTRA_OK, "setup: chipestra_pause_playback() == CHIPESTRA_OK (-> Paused)");
    Check(chipestra_send_command(handle, 1, 0, 0) == CHIPESTRA_OK, "send_command(Stop) while Paused == CHIPESTRA_OK");

    chipestra_stop_playback(handle);
    chipestra_stop_audio_output(handle);
    chipestra_unload_machine(handle);
    chipestra_destroy_instance(handle);
}

// Part O: the Playback API's null-handle safety.
void PlaybackNullArgumentSafety() {
    std::printf("PlaybackNullArgumentSafety\n");

    Check(chipestra_start_playback(nullptr) == CHIPESTRA_E_INVALID_ARGUMENT,
          "chipestra_start_playback(null handle) == CHIPESTRA_E_INVALID_ARGUMENT");
    Check(chipestra_pause_playback(nullptr) == CHIPESTRA_E_INVALID_ARGUMENT,
          "chipestra_pause_playback(null handle) == CHIPESTRA_E_INVALID_ARGUMENT");
    Check(chipestra_resume_playback(nullptr) == CHIPESTRA_E_INVALID_ARGUMENT,
          "chipestra_resume_playback(null handle) == CHIPESTRA_E_INVALID_ARGUMENT");
    Check(chipestra_stop_playback(nullptr) == CHIPESTRA_E_INVALID_ARGUMENT,
          "chipestra_stop_playback(null handle) == CHIPESTRA_E_INVALID_ARGUMENT");
}

// Part P: Stop/output-switch resets worker/queue/counters consistently, and
// repeated start/stop playback cycles neither crash nor leave the worker thread
// running.
void PlaybackStopStartCyclesAreClean() {
    std::printf("PlaybackStopStartCyclesAreClean\n");

    ChipestraHandle handle = nullptr;
    chipestra_create_instance(&handle);
    chipestra_load_machine(handle, "TEST_MACHINE", "STANDARD", nullptr, 0);
    chipestra_select_null_output(handle);
    chipestra_start_audio_output(handle);

    for (int cycle = 0; cycle < 3; ++cycle) {
        Check(chipestra_start_playback(handle) == CHIPESTRA_OK, "chipestra_start_playback() == CHIPESTRA_OK each cycle");
        Check(WaitForSubmittedFrames(handle, Mixer::kBlockSizeFrames, 1000),
              "playback renders at least one block each cycle");
        Check(chipestra_stop_playback(handle) == CHIPESTRA_OK, "chipestra_stop_playback() == CHIPESTRA_OK each cycle");
    }

    // Switching outputs: a freshly-selected output is a brand new
    // IAudioOutput instance, so its counters start at 0 - proving the
    // switch doesn't inherit stale state from the one it replaced.
    Check(chipestra_stop_audio_output(handle) == CHIPESTRA_OK, "chipestra_stop_audio_output() == CHIPESTRA_OK before switching");
    Check(chipestra_select_null_output(handle) == CHIPESTRA_OK, "chipestra_select_null_output() == CHIPESTRA_OK (fresh output)");
    Check(chipestra_start_audio_output(handle) == CHIPESTRA_OK, "chipestra_start_audio_output() == CHIPESTRA_OK (fresh output)");

    ChipestraAudioOutputDiagnostics fresh_diagnostics{};
    chipestra_get_audio_output_diagnostics(handle, &fresh_diagnostics);
    Check(fresh_diagnostics.submitted_frame_count == 0,
          "a freshly-selected output's SubmittedFrameCount starts at 0");

    chipestra_destroy_instance(handle);
}

// Part Q: BoundedFrameQueue push/pop bookkeeping and the "drop the tail, never
// block, count what was dropped" overflow policy. Fully deterministic - no real
// audio device involved.
void BoundedFrameQueueBehavior() {
    std::printf("BoundedFrameQueueBehavior\n");

    BoundedFrameQueue queue(4);
    Check(queue.Capacity() == 4, "Capacity() == 4");
    Check(queue.Size() == 0, "Size() == 0 initially");

    StereoFrame three[3] = {{1, 1}, {2, 2}, {3, 3}};
    std::size_t pushed = queue.Push(three, 3);
    Check(pushed == 3, "Push() of 3 into capacity-4 queue accepts all 3");
    Check(queue.Size() == 3, "Size() == 3 after pushing 3");

    // Only 1 slot free; pushing 2 more should accept 1 and report 1 short.
    StereoFrame two[2] = {{4, 4}, {5, 5}};
    pushed = queue.Push(two, 2);
    Check(pushed == 1, "Push() onto an almost-full queue accepts only what fits (drop, not block)");
    Check(queue.Size() == 4, "Size() == Capacity() once full");

    StereoFrame popped[4] = {};
    std::size_t popped_count = queue.Pop(popped, 4);
    Check(popped_count == 4, "Pop() drains everything that was actually enqueued");
    Check(popped[0].left == 1 && popped[1].left == 2 && popped[2].left == 3 && popped[3].left == 4,
          "Pop() preserves FIFO order, including across the wraparound this sequence causes");

    Check(queue.Size() == 0, "Size() == 0 after draining");
    queue.Push(three, 3);
    queue.Clear();
    Check(queue.Size() == 0, "Clear() empties the queue");
}

// Part R: WasapiSharedOutput against the real default render device. This stays
// integration-level: if this machine has no default render endpoint,
// Initialize() throwing is treated as "skip, don't fail the suite" - the point
// is that when a device IS available, the pipeline doesn't crash and the
// diagnostics move the way they should.
void WasapiSharedOutputBehavior() {
    std::printf("WasapiSharedOutputBehavior\n");

    WasapiSharedOutput output;
    try {
        output.Initialize();
    } catch (const std::exception& ex) {
        std::printf("  SKIP: WasapiSharedOutput::Initialize() threw (%s) - no usable render device on "
                    "this machine; skipping the WASAPI integration checks\n",
                    ex.what());
        return;
    }

    ChipestraAudioOutputDiagnostics initial = output.GetDiagnostics();
    Check(initial.buffer_frame_capacity > 0, "BufferFrameCapacity > 0 after Initialize()");
    Check(initial.submitted_frame_count == 0, "SubmittedFrameCount == 0 before Start()");

    output.Start();

    std::vector<StereoFrame> block(Mixer::kBlockSizeFrames, StereoFrame{0.0f, 0.0f});
    output.SubmitStereo(block.data(), block.size());

    Check(output.SubmittedFrameCount() == Mixer::kBlockSizeFrames,
          "SubmittedFrameCount() == kBlockSizeFrames after one SubmitStereo() call");

    ChipestraAudioOutputDiagnostics after_submit = output.GetDiagnostics();
    Check(after_submit.worker_wake_count >= 1, "WorkerWakeCount increases on SubmitStereo()");

    // Force an overflow: one call whose resampled (endpoint-rate) size exceeds
    // the queue capacity (buffer_frame_capacity * 4). SubmitStereo() resamples
    // canonical 192kHz -> endpoint rate internally, so scale the canonical count
    // up by the rate ratio, then double it for margin.
    const double rate_ratio =
        static_cast<double>(Mixer::kCanonicalSampleRateHz) / static_cast<double>(std::max(1u, output.EndpointSampleRateHz()));
    std::size_t oversized_count =
        static_cast<std::size_t>(static_cast<double>(initial.buffer_frame_capacity) * 4.0 * rate_ratio) * 2 + 1000;
    std::vector<StereoFrame> oversized(oversized_count, StereoFrame{0.0f, 0.0f});
    output.SubmitStereo(oversized.data(), oversized.size());
    ChipestraAudioOutputDiagnostics after_overflow = output.GetDiagnostics();
    Check(after_overflow.queue_overflow_count > 0,
          "QueueOverflowCount increases when a submission exceeds queue capacity");

    output.Stop();
    output.Shutdown();
    // Reaching here without an exception/crash across Initialize -> Start ->
    // SubmitStereo (normal and oversized) -> Stop -> Shutdown is itself the main
    // thing this test proves.
    Check(true, "Initialize/Start/Submit/Stop/Shutdown completed without crashing");
}

// Part S: the WASAPI output ABI functions, exercised the same way as the Null
// Output ones - tolerant of "no device".
void WasapiOutputAbi() {
    std::printf("WasapiOutputAbi\n");

    ChipestraHandle handle = nullptr;
    chipestra_create_instance(&handle);
    chipestra_load_machine(handle, "TEST_MACHINE", "STANDARD", nullptr, 0);

    ChipestraResult select_result = chipestra_select_wasapi_shared_output(handle, 0);
    if (select_result != CHIPESTRA_OK) {
        Check(select_result == CHIPESTRA_E_INTERNAL,
              "chipestra_select_wasapi_shared_output() fails cleanly (CHIPESTRA_E_INTERNAL) with no usable device");
        std::printf("  SKIP: no usable WASAPI render device; skipping the rest of WasapiOutputAbi\n");
        chipestra_destroy_instance(handle);
        return;
    }
    Check(true, "chipestra_select_wasapi_shared_output() == CHIPESTRA_OK");

    Check(chipestra_select_wasapi_shared_output(handle, 0) == CHIPESTRA_E_INVALID_STATE,
          "chipestra_select_wasapi_shared_output() again with one already selected == CHIPESTRA_E_INVALID_STATE");

    Check(chipestra_start_audio_output(handle) == CHIPESTRA_OK, "chipestra_start_audio_output() == CHIPESTRA_OK (WASAPI)");

    ChipestraAudioOutputDiagnostics diagnostics{};
    Check(chipestra_get_audio_output_diagnostics(handle, &diagnostics) == CHIPESTRA_OK,
          "chipestra_get_audio_output_diagnostics() == CHIPESTRA_OK once Ready");
    Check(diagnostics.buffer_frame_capacity > 0, "diagnostics.buffer_frame_capacity > 0");

    Check(chipestra_start_playback(handle) == CHIPESTRA_OK, "chipestra_start_playback() over WASAPI == CHIPESTRA_OK");
    Check(WaitForSubmittedFrames(handle, Mixer::kBlockSizeFrames, 1000),
          "playback over WASAPI submits at least kBlockSizeFrames within the timeout");
    Check(chipestra_stop_playback(handle) == CHIPESTRA_OK, "chipestra_stop_playback() over WASAPI == CHIPESTRA_OK");

    Check(chipestra_stop_audio_output(handle) == CHIPESTRA_OK, "chipestra_stop_audio_output() == CHIPESTRA_OK (WASAPI)");
    Check(chipestra_get_audio_output_diagnostics(handle, &diagnostics) == CHIPESTRA_E_INVALID_STATE,
          "chipestra_get_audio_output_diagnostics() after stop == CHIPESTRA_E_INVALID_STATE (no output selected)");

    chipestra_destroy_instance(handle);
}

void AudioOutputDiagnosticsNullArgumentSafety() {
    std::printf("AudioOutputDiagnosticsNullArgumentSafety\n");

    ChipestraAudioOutputDiagnostics diagnostics{};
    Check(chipestra_get_audio_output_diagnostics(nullptr, &diagnostics) == CHIPESTRA_E_INVALID_ARGUMENT,
          "chipestra_get_audio_output_diagnostics(null handle) == CHIPESTRA_E_INVALID_ARGUMENT");
    Check(chipestra_select_wasapi_shared_output(nullptr, 0) == CHIPESTRA_E_INVALID_ARGUMENT,
          "chipestra_select_wasapi_shared_output(null handle) == CHIPESTRA_E_INVALID_ARGUMENT");

    ChipestraHandle handle = nullptr;
    chipestra_create_instance(&handle);
    chipestra_load_machine(handle, "TEST_MACHINE", "STANDARD", nullptr, 0);
    Check(chipestra_get_audio_output_diagnostics(handle, nullptr) == CHIPESTRA_E_INVALID_ARGUMENT,
          "chipestra_get_audio_output_diagnostics(null out_diagnostics) == CHIPESTRA_E_INVALID_ARGUMENT");
    chipestra_destroy_instance(handle);
}

// Output-rate selection: the rate argument's validation, the audio-output-format
// getter's argument/state handling, and (when a device exists) that the reported
// format is self-consistent. Tolerant of "no device", like WasapiOutputAbi.
void OutputRateSelectionAbi() {
    std::printf("OutputRateSelectionAbi\n");

    Check(chipestra_get_api_version() == 4u, "chipestra_get_api_version() == 4");

    ChipestraAudioOutputFormat format{};
    Check(chipestra_get_audio_output_format(nullptr, &format) == CHIPESTRA_E_INVALID_ARGUMENT,
          "chipestra_get_audio_output_format(null handle) == CHIPESTRA_E_INVALID_ARGUMENT");

    ChipestraHandle handle = nullptr;
    chipestra_create_instance(&handle);
    chipestra_load_machine(handle, "TEST_MACHINE", "STANDARD", nullptr, 0);
    Check(chipestra_get_audio_output_format(handle, nullptr) == CHIPESTRA_E_INVALID_ARGUMENT,
          "chipestra_get_audio_output_format(null out_format) == CHIPESTRA_E_INVALID_ARGUMENT");
    Check(chipestra_get_audio_output_format(handle, &format) == CHIPESTRA_E_INVALID_STATE,
          "chipestra_get_audio_output_format() with no output selected and nothing rejected == CHIPESTRA_E_INVALID_STATE");
    Check(chipestra_select_wasapi_shared_output(handle, 12345) == CHIPESTRA_E_INVALID_ARGUMENT,
          "chipestra_select_wasapi_shared_output(unsupported rate 12345) == CHIPESTRA_E_INVALID_ARGUMENT");

    ChipestraResult select_result = chipestra_select_wasapi_shared_output(handle, 44100);
    if (select_result == CHIPESTRA_OK) {
        Check(chipestra_get_audio_output_format(handle, &format) == CHIPESTRA_OK,
              "chipestra_get_audio_output_format() == CHIPESTRA_OK once a WASAPI output is selected");
        Check(format.requested_rate_hz == 44100u, "format.requested_rate_hz echoes the request");
        Check(format.endpoint_rate_hz > 0 && format.channels > 0 && format.bits_per_sample > 0,
              "format reports a real endpoint rate/channels/bit depth");
        Check((format.selection_honored == 1u) == (format.endpoint_rate_hz == 44100u),
              "selection_honored is 1 exactly when the endpoint rate equals the request");
        Check(format.sample_format == CHIPESTRA_SAMPLE_FORMAT_FLOAT || format.sample_format == CHIPESTRA_SAMPLE_FORMAT_PCM,
              "an accepted endpoint is float or PCM");
    } else {
        Check(select_result == CHIPESTRA_E_INTERNAL || select_result == CHIPESTRA_E_UNSUPPORTED_FORMAT,
              "chipestra_select_wasapi_shared_output() failure is CHIPESTRA_E_INTERNAL or CHIPESTRA_E_UNSUPPORTED_FORMAT");
        if (select_result == CHIPESTRA_E_UNSUPPORTED_FORMAT) {
            Check(chipestra_get_audio_output_format(handle, &format) == CHIPESTRA_OK && format.endpoint_rate_hz > 0,
                  "the rejected format is retrievable after CHIPESTRA_E_UNSUPPORTED_FORMAT");
        }
        std::printf("  SKIP: no usable WASAPI render device; skipping the endpoint-format checks\n");
    }
    chipestra_destroy_instance(handle);
}

// Part T: MachineRegistry's own Register/Find/Enumerate/Create semantics,
// tested directly against a throwaway registry - fully deterministic, no C ABI/
// instance involved.
void MachineRegistryBehavior() {
    std::printf("MachineRegistryBehavior\n");

    MachineRegistry registry;

    MachineDefinition def1;
    def1.Id = MachineId("MACHINE_ONE");
    def1.DisplayName = "Machine One";
    def1.Category = MachineCategory::Arcade;
    def1.Variants.push_back(MachineVariant{VariantId("V1"), "Variant 1"});
    def1.Factory = [](const VariantId&, const MachineResources&) -> std::unique_ptr<IMachine> {
        return std::make_unique<TestMachine>();
    };
    registry.Register(def1);

    MachineDefinition def2;
    def2.Id = MachineId("MACHINE_TWO");
    def2.DisplayName = "Machine Two";
    def2.Category = MachineCategory::Console;
    def2.Variants.push_back(MachineVariant{VariantId("V2A"), "Variant 2A"});
    def2.Variants.push_back(MachineVariant{VariantId("V2B"), "Variant 2B"});
    def2.Factory = [](const VariantId&, const MachineResources&) -> std::unique_ptr<IMachine> {
        return std::make_unique<TestMachine>();
    };
    registry.Register(def2);

    Check(registry.Enumerate().size() == 2, "Enumerate() returns both registered definitions");

    const MachineDefinition* found = registry.Find(MachineId("MACHINE_ONE"));
    Check(found != nullptr && found->DisplayName == "Machine One", "Find() locates a registered machine by id");
    Check(registry.Find(MachineId("UNKNOWN")) == nullptr, "Find() returns nullptr for an unregistered id");

    MachineResources noResources;
    Check(registry.Create(MachineId("MACHINE_ONE"), VariantId("V1"), noResources) != nullptr,
          "Create() succeeds for a correct machine/variant pair");
    Check(registry.Create(MachineId("MACHINE_TWO"), VariantId("V2B"), noResources) != nullptr,
          "Create() succeeds for the second machine's second variant");

    Check(registry.Create(MachineId("MACHINE_ONE"), VariantId("WRONG_VARIANT"), noResources) == nullptr,
          "Create() fails for a known machine with an unknown variant (exact match only, no fuzzy match)");
    Check(registry.Create(MachineId("UNKNOWN_MACHINE"), VariantId("V1"), noResources) == nullptr,
          "Create() fails for an unknown machine id");

    bool threw = false;
    try {
        registry.Register(def1);
    } catch (const std::logic_error&) {
        threw = true;
    }
    Check(threw, "Register() rejects re-registering an already-registered MachineId");
}

// Part U: the Registry group over the C ABI, against the real
// DefaultMachineRegistry (the same machine_id goes through real registry lookup).
void MachineRegistryAbi() {
    std::printf("MachineRegistryAbi\n");

    Check(chipestra_get_machine_count() >= 1, "chipestra_get_machine_count() reports at least the default TestMachine");

    ChipestraMachineInfo info{};
    Check(chipestra_get_machine_info(0, &info) == CHIPESTRA_OK, "chipestra_get_machine_info(0) == CHIPESTRA_OK");
    Check(std::strcmp(info.machine_id, "TEST_MACHINE") == 0, "info.machine_id == \"TEST_MACHINE\"");
    Check(std::strcmp(info.category, "ARCADE") == 0, "info.category == \"ARCADE\"");
    Check(info.variant_count >= 1, "info.variant_count >= 1");
    // manufacturer is appended to ChipestraMachineInfo (fields are only ever appended);
    // TestMachine's is "Chipestra (synthetic)" (default_machine_catalog.cpp), just needs
    // to be non-empty here.
    Check(info.manufacturer[0] != '\0', "info.manufacturer is populated (appended field)");

    Check(chipestra_get_machine_info(chipestra_get_machine_count(), &info) == CHIPESTRA_E_NOT_FOUND,
          "chipestra_get_machine_info(out-of-range index) == CHIPESTRA_E_NOT_FOUND");
    Check(chipestra_get_machine_info(0, nullptr) == CHIPESTRA_E_INVALID_ARGUMENT,
          "chipestra_get_machine_info(null out_info) == CHIPESTRA_E_INVALID_ARGUMENT");

    ChipestraHandle handle = nullptr;
    chipestra_create_instance(&handle);

    // chipestra_get_machine_channel_count - CHIPESTRA_E_INVALID_STATE with no Machine
    // loaded yet (Created), CHIPESTRA_OK (and the real count) once one is. TestMachine
    // builds its single-entry LogicalChannels() vector at construction, and the
    // vector's size is fixed from Initialize() onward for every Machine, so this is
    // valid even while Playing (see this function's comment in chipestra_abi.h).
    uint32_t channelCount = 0;
    Check(chipestra_get_machine_channel_count(handle, &channelCount) == CHIPESTRA_E_INVALID_STATE,
          "chipestra_get_machine_channel_count() on Created state == CHIPESTRA_E_INVALID_STATE");
    Check(chipestra_get_machine_channel_count(nullptr, &channelCount) == CHIPESTRA_E_INVALID_ARGUMENT,
          "chipestra_get_machine_channel_count(null handle) == CHIPESTRA_E_INVALID_ARGUMENT");

    Check(chipestra_load_machine(handle, "TEST_MACHINE", "STANDARD", nullptr, 0) == CHIPESTRA_OK,
          "chipestra_load_machine(TEST_MACHINE, STANDARD) == CHIPESTRA_OK");
    Check(chipestra_get_machine_channel_count(handle, &channelCount) == CHIPESTRA_OK && channelCount == 1,
          "chipestra_get_machine_channel_count() on MachineLoaded state == CHIPESTRA_OK, TestMachine has 1 channel");
    Check(chipestra_get_machine_channel_count(handle, nullptr) == CHIPESTRA_E_INVALID_ARGUMENT,
          "chipestra_get_machine_channel_count(null out_count) == CHIPESTRA_E_INVALID_ARGUMENT");
    Check(chipestra_unload_machine(handle) == CHIPESTRA_OK, "setup: chipestra_unload_machine() == CHIPESTRA_OK");

    Check(chipestra_load_machine(handle, "TEST_MACHINE", "WRONG_VARIANT", nullptr, 0) == CHIPESTRA_E_NOT_FOUND,
          "chipestra_load_machine(known machine, unknown variant) == CHIPESTRA_E_NOT_FOUND");
    Check(chipestra_load_machine(handle, "NO_SUCH_MACHINE", "STANDARD", nullptr, 0) == CHIPESTRA_E_NOT_FOUND,
          "chipestra_load_machine(unknown machine, valid-looking variant) == CHIPESTRA_E_NOT_FOUND");

    chipestra_destroy_instance(handle);
}

// Part V: Channel Meter group's get/set round-trip, index bounds, state gating,
// and null-argument safety - the plumbing itself, independent of whether the
// values actually reach the Mixer (Part W; the Mixer's own math is covered by
// MixerPanLaw/MixerMute).
void ChannelMeterInfoAndSettersRoundTrip() {
    std::printf("ChannelMeterInfoAndSettersRoundTrip\n");

    ChipestraHandle handle = nullptr;
    Check(chipestra_create_instance(&handle) == CHIPESTRA_OK, "setup: chipestra_create_instance() == CHIPESTRA_OK");

    // Created state: every Channel Meter function rejects before a Machine is loaded.
    ChipestraLogicalChannelInfo info{};
    Check(chipestra_get_logical_channel_info(handle, 0, &info) == CHIPESTRA_E_INVALID_STATE,
          "chipestra_get_logical_channel_info() on Created == CHIPESTRA_E_INVALID_STATE");
    Check(chipestra_set_logical_channel_muted(handle, 0, 1) == CHIPESTRA_E_INVALID_STATE,
          "chipestra_set_logical_channel_muted() on Created == CHIPESTRA_E_INVALID_STATE");
    Check(chipestra_set_logical_channel_pan(handle, 0, 0.5f) == CHIPESTRA_E_INVALID_STATE,
          "chipestra_set_logical_channel_pan() on Created == CHIPESTRA_E_INVALID_STATE");
    Check(chipestra_clear_logical_channel_pan_override(handle, 0) == CHIPESTRA_E_INVALID_STATE,
          "chipestra_clear_logical_channel_pan_override() on Created == CHIPESTRA_E_INVALID_STATE");
    Check(chipestra_set_logical_channel_gain(handle, 0, 0.5f) == CHIPESTRA_E_INVALID_STATE,
          "chipestra_set_logical_channel_gain() on Created == CHIPESTRA_E_INVALID_STATE");
    ChipestraMasterState masterState{};
    Check(chipestra_get_master_state(handle, &masterState) == CHIPESTRA_E_INVALID_STATE,
          "chipestra_get_master_state() on Created == CHIPESTRA_E_INVALID_STATE");
    Check(chipestra_set_master_gain(handle, 0.5f) == CHIPESTRA_E_INVALID_STATE,
          "chipestra_set_master_gain() on Created == CHIPESTRA_E_INVALID_STATE");
    Check(chipestra_set_master_muted(handle, 1) == CHIPESTRA_E_INVALID_STATE,
          "chipestra_set_master_muted() on Created == CHIPESTRA_E_INVALID_STATE");

    Check(chipestra_load_machine(handle, "TEST_MACHINE", "STANDARD", nullptr, 0) == CHIPESTRA_OK,
          "setup: chipestra_load_machine() == CHIPESTRA_OK");

    // Defaults, straight from TestMachine's LogicalChannel aggregate-init
    // (test_machine.h): Mute=false, Pan=0, Gain=1.0, Peak=RMS=0 (nothing
    // rendered yet).
    Check(chipestra_get_logical_channel_info(handle, 0, &info) == CHIPESTRA_OK,
          "chipestra_get_logical_channel_info(0) on MachineLoaded == CHIPESTRA_OK");
    Check(info.channel_id == 0, "info.channel_id == TestMachine::kChannelId");
    Check(std::strcmp(info.device_name, "TEST_MACHINE") == 0, "info.device_name == \"TEST_MACHINE\"");
    Check(std::strcmp(info.channel_name, "Channel 0") == 0, "info.channel_name == \"Channel 0\"");
    Check(info.muted == 0, "info.muted defaults to false");
    Check(info.pan == 0.0f, "info.pan defaults to 0.0 (center)");
    Check(info.gain == 1.0f, "info.gain defaults to 1.0 (unity)");
    Check(info.peak == 0.0f && info.rms == 0.0f, "info.peak/rms are 0 before any render");
    Check(info.peak_left == 0.0f && info.peak_right == 0.0f && info.rms_left == 0.0f && info.rms_right == 0.0f,
          "info.peak_left/peak_right/rms_left/rms_right are 0 before any render");

    // Round-trip the setters.
    Check(chipestra_set_logical_channel_muted(handle, 0, 1) == CHIPESTRA_OK, "chipestra_set_logical_channel_muted(0, true) == CHIPESTRA_OK");
    Check(chipestra_set_logical_channel_pan(handle, 0, -0.75f) == CHIPESTRA_OK, "chipestra_set_logical_channel_pan(0, -0.75) == CHIPESTRA_OK");
    Check(chipestra_set_logical_channel_gain(handle, 0, 0.25f) == CHIPESTRA_OK, "chipestra_set_logical_channel_gain(0, 0.25) == CHIPESTRA_OK");
    Check(chipestra_get_logical_channel_info(handle, 0, &info) == CHIPESTRA_OK, "re-read: chipestra_get_logical_channel_info() == CHIPESTRA_OK");
    Check(info.muted == 1, "re-read: muted reflects the setter");
    Check(info.pan == -0.75f, "re-read: pan reflects the setter");
    Check(info.gain == 0.25f, "re-read: gain reflects the setter");

    // chipestra_clear_logical_channel_pan_override resets Pan to center
    // (TestMachine has no auto-sync mechanism to resume, so 0.0 is the only
    // observable post-clear value at the ABI level; the auto-sync-resumes case is
    // covered by XBoardMachineChannelPanAutoSyncsUnlessOverridden).
    Check(chipestra_clear_logical_channel_pan_override(handle, 0) == CHIPESTRA_OK, "chipestra_clear_logical_channel_pan_override(0) == CHIPESTRA_OK");
    Check(chipestra_get_logical_channel_info(handle, 0, &info) == CHIPESTRA_OK, "re-read after clear: chipestra_get_logical_channel_info() == CHIPESTRA_OK");
    Check(info.pan == 0.0f, "re-read after clear: pan reset to center");

    // Master section round-trip.
    Check(chipestra_get_master_state(handle, &masterState) == CHIPESTRA_OK, "chipestra_get_master_state() on MachineLoaded == CHIPESTRA_OK");
    Check(masterState.gain == 1.0f && masterState.muted == 0, "master defaults: gain=1.0, unmuted");
    Check(chipestra_set_master_gain(handle, 0.4f) == CHIPESTRA_OK, "chipestra_set_master_gain(0.4) == CHIPESTRA_OK");
    Check(chipestra_set_master_muted(handle, 1) == CHIPESTRA_OK, "chipestra_set_master_muted(true) == CHIPESTRA_OK");
    Check(chipestra_get_master_state(handle, &masterState) == CHIPESTRA_OK, "re-read: chipestra_get_master_state() == CHIPESTRA_OK");
    Check(masterState.gain == 0.4f, "re-read: master gain reflects the setter");
    Check(masterState.muted == 1, "re-read: master muted reflects the setter");

    // Index out of range (TestMachine has exactly 1 channel, index 0).
    Check(chipestra_get_logical_channel_info(handle, 1, &info) == CHIPESTRA_E_NOT_FOUND,
          "chipestra_get_logical_channel_info(1) == CHIPESTRA_E_NOT_FOUND (only index 0 exists)");
    Check(chipestra_set_logical_channel_muted(handle, 1, 0) == CHIPESTRA_E_NOT_FOUND,
          "chipestra_set_logical_channel_muted(1) == CHIPESTRA_E_NOT_FOUND");
    Check(chipestra_set_logical_channel_pan(handle, 1, 0.0f) == CHIPESTRA_E_NOT_FOUND,
          "chipestra_set_logical_channel_pan(1) == CHIPESTRA_E_NOT_FOUND");
    Check(chipestra_clear_logical_channel_pan_override(handle, 1) == CHIPESTRA_E_NOT_FOUND,
          "chipestra_clear_logical_channel_pan_override(1) == CHIPESTRA_E_NOT_FOUND");
    Check(chipestra_set_logical_channel_gain(handle, 1, 1.0f) == CHIPESTRA_E_NOT_FOUND,
          "chipestra_set_logical_channel_gain(1) == CHIPESTRA_E_NOT_FOUND");

    // Null-argument safety.
    Check(chipestra_get_logical_channel_info(nullptr, 0, &info) == CHIPESTRA_E_INVALID_ARGUMENT,
          "chipestra_get_logical_channel_info(null handle) == CHIPESTRA_E_INVALID_ARGUMENT");
    Check(chipestra_get_logical_channel_info(handle, 0, nullptr) == CHIPESTRA_E_INVALID_ARGUMENT,
          "chipestra_get_logical_channel_info(null out_info) == CHIPESTRA_E_INVALID_ARGUMENT");
    Check(chipestra_set_logical_channel_muted(nullptr, 0, 0) == CHIPESTRA_E_INVALID_ARGUMENT,
          "chipestra_set_logical_channel_muted(null handle) == CHIPESTRA_E_INVALID_ARGUMENT");
    Check(chipestra_set_logical_channel_pan(nullptr, 0, 0.0f) == CHIPESTRA_E_INVALID_ARGUMENT,
          "chipestra_set_logical_channel_pan(null handle) == CHIPESTRA_E_INVALID_ARGUMENT");
    Check(chipestra_clear_logical_channel_pan_override(nullptr, 0) == CHIPESTRA_E_INVALID_ARGUMENT,
          "chipestra_clear_logical_channel_pan_override(null handle) == CHIPESTRA_E_INVALID_ARGUMENT");
    Check(chipestra_set_logical_channel_gain(nullptr, 0, 0.0f) == CHIPESTRA_E_INVALID_ARGUMENT,
          "chipestra_set_logical_channel_gain(null handle) == CHIPESTRA_E_INVALID_ARGUMENT");
    Check(chipestra_get_master_state(nullptr, &masterState) == CHIPESTRA_E_INVALID_ARGUMENT,
          "chipestra_get_master_state(null handle) == CHIPESTRA_E_INVALID_ARGUMENT");
    Check(chipestra_get_master_state(handle, nullptr) == CHIPESTRA_E_INVALID_ARGUMENT,
          "chipestra_get_master_state(null out_state) == CHIPESTRA_E_INVALID_ARGUMENT");
    Check(chipestra_set_master_gain(nullptr, 1.0f) == CHIPESTRA_E_INVALID_ARGUMENT,
          "chipestra_set_master_gain(null handle) == CHIPESTRA_E_INVALID_ARGUMENT");
    Check(chipestra_set_master_muted(nullptr, 0) == CHIPESTRA_E_INVALID_ARGUMENT,
          "chipestra_set_master_muted(null handle) == CHIPESTRA_E_INVALID_ARGUMENT");

    chipestra_unload_machine(handle);
    chipestra_destroy_instance(handle);
}

// Part W: Channel Meter/master values actually reach the real Mixer output during
// Playing, observed through chipestra_get_master_state's L/R meter (the only
// sample-content window the public ABI exposes). The pipeline's own math is
// covered by MixerPanLaw and TestMachineToMixerIntegration; this proves the
// Channel Meter/master ABI plumbing reaches that same pipeline.
// A new command starts a new song: the channel level readings (peak/rms/source_peak) are only rewritten
// by the next rendered block, so chipestra_send_command must clear the previous song's values - else the
// auto-stop-by-silence check takes them for the new song's sound.
void SendCommandClearsChannelLevelReadings() {
    std::printf("SendCommandClearsChannelLevelReadings\n");

    ChipestraHandle handle = nullptr;
    Check(chipestra_create_instance(&handle) == CHIPESTRA_OK, "setup: chipestra_create_instance() == CHIPESTRA_OK");
    Check(chipestra_load_machine(handle, "TEST_MACHINE", "STANDARD", nullptr, 0) == CHIPESTRA_OK, "setup: chipestra_load_machine() == CHIPESTRA_OK");
    Check(chipestra_select_null_output(handle) == CHIPESTRA_OK, "setup: chipestra_select_null_output() == CHIPESTRA_OK");
    Check(chipestra_start_audio_output(handle) == CHIPESTRA_OK, "setup: chipestra_start_audio_output() == CHIPESTRA_OK");
    Check(chipestra_start_playback(handle) == CHIPESTRA_OK, "setup: chipestra_start_playback() == CHIPESTRA_OK");
    Check(WaitForSubmittedFrames(handle, Mixer::kBlockSizeFrames * 3, 2000), "setup: playback renders a few blocks");
    Check(chipestra_stop_playback(handle) == CHIPESTRA_OK, "setup: chipestra_stop_playback() == CHIPESTRA_OK");

    ChipestraLogicalChannelInfo info{};
    Check(chipestra_get_logical_channel_info(handle, 0, &info) == CHIPESTRA_OK, "before: info == CHIPESTRA_OK");
    Check(info.peak > 0.0f && info.peak_left > 0.0f && info.source_peak > 0.0f, "(precondition) the rendered song left nonzero level readings");

    Check(chipestra_send_command(handle, 2, 0x11, 0) == CHIPESTRA_OK, "chipestra_send_command() == CHIPESTRA_OK");
    Check(chipestra_get_logical_channel_info(handle, 0, &info) == CHIPESTRA_OK, "after: info == CHIPESTRA_OK");
    Check(info.peak == 0.0f && info.rms == 0.0f && info.peak_left == 0.0f && info.peak_right == 0.0f &&
              info.rms_left == 0.0f && info.rms_right == 0.0f && info.source_peak == 0.0f,
          "a new command clears the previous song's level readings (peak/rms/L/R/source_peak all 0)");

    Check(chipestra_stop_audio_output(handle) == CHIPESTRA_OK, "teardown: chipestra_stop_audio_output() == CHIPESTRA_OK");
    Check(chipestra_unload_machine(handle) == CHIPESTRA_OK, "teardown: chipestra_unload_machine() == CHIPESTRA_OK");
    chipestra_destroy_instance(handle);
}

void ChannelMeterAndMasterAffectMixedOutput() {
    std::printf("ChannelMeterAndMasterAffectMixedOutput\n");

    ChipestraHandle handle = nullptr;
    Check(chipestra_create_instance(&handle) == CHIPESTRA_OK, "setup: chipestra_create_instance() == CHIPESTRA_OK");
    Check(chipestra_load_machine(handle, "TEST_MACHINE", "STANDARD", nullptr, 0) == CHIPESTRA_OK, "setup: chipestra_load_machine() == CHIPESTRA_OK");
    Check(chipestra_select_null_output(handle) == CHIPESTRA_OK, "setup: chipestra_select_null_output() == CHIPESTRA_OK");
    Check(chipestra_start_audio_output(handle) == CHIPESTRA_OK, "setup: chipestra_start_audio_output() == CHIPESTRA_OK");
    Check(chipestra_start_playback(handle) == CHIPESTRA_OK, "setup: chipestra_start_playback() == CHIPESTRA_OK");

    // Waits for at least `extraBlocks` more blocks beyond whatever has
    // already been submitted, so the next chipestra_get_master_state() read is
    // guaranteed to reflect a block rendered *after* the parameter change
    // that was just made (not a stale one from before it).
    auto waitForFreshBlocks = [&](int extraBlocks) -> bool {
        ChipestraAudioOutputDiagnostics diagnostics{};
        chipestra_get_audio_output_diagnostics(handle, &diagnostics);
        const std::uint64_t target = diagnostics.submitted_frame_count + Mixer::kBlockSizeFrames * static_cast<std::uint64_t>(extraBlocks);
        return WaitForSubmittedFrames(handle, target, 2000);
    };

    // Baseline (pan=0/gain=1.0 defaults): TestMachine's first sample is 0.5,
    // halved by Mixer's pan=0 law to 0.25 on each side (matches
    // TestMachineToMixerIntegration's kExpectedPattern[0]), so peak_left/peak_right
    // should each read ~0.25 (the block may start mid-pattern, but peak is still
    // bounded by the pattern's max magnitude, 0.5*0.5=0.25).
    Check(waitForFreshBlocks(2), "baseline: playback renders fresh blocks");
    ChipestraMasterState state{};
    Check(chipestra_get_master_state(handle, &state) == CHIPESTRA_OK, "baseline: chipestra_get_master_state() == CHIPESTRA_OK");
    Check(NearlyEqual(state.peak_left, 0.25f) && NearlyEqual(state.peak_right, 0.25f),
          "baseline: peak_left/peak_right match TestMachine+Mixer's known pan=0 pattern (~0.25)");

    // With a single channel, its own per-channel L/R peak should match the master
    // L/R meter exactly at this point (nothing else in the mix, no master-stage
    // gain applied yet).
    ChipestraLogicalChannelInfo channelInfo{};
    Check(chipestra_get_logical_channel_info(handle, 0, &channelInfo) == CHIPESTRA_OK,
          "baseline: chipestra_get_logical_channel_info() == CHIPESTRA_OK");
    Check(NearlyEqual(channelInfo.peak_left, 0.25f) && NearlyEqual(channelInfo.peak_right, 0.25f),
          "baseline: info.peak_left/peak_right match the same pan=0 pattern (~0.25) as the master meter");

    // Channel gain: halving LogicalChannel::Gain halves the mixed peak.
    Check(chipestra_set_logical_channel_gain(handle, 0, 0.5f) == CHIPESTRA_OK, "chipestra_set_logical_channel_gain(0, 0.5) == CHIPESTRA_OK");
    Check(waitForFreshBlocks(2), "after channel gain change: playback renders fresh blocks");
    Check(chipestra_get_master_state(handle, &state) == CHIPESTRA_OK, "after channel gain change: chipestra_get_master_state() == CHIPESTRA_OK");
    Check(NearlyEqual(state.peak_left, 0.125f) && NearlyEqual(state.peak_right, 0.125f),
          "channel gain: halving Gain halves the real mixed peak (0.25 -> 0.125)");

    // source_peak is the sound before the user's Mute/Gain/Pan, so it does not
    // follow the Gain change above (peak does), and survives Mute.
    ChipestraLogicalChannelInfo halfGainInfo{};
    Check(chipestra_get_logical_channel_info(handle, 0, &halfGainInfo) == CHIPESTRA_OK,
          "v4: chipestra_get_logical_channel_info() after gain change == CHIPESTRA_OK");
    Check(NearlyEqual(halfGainInfo.source_peak, channelInfo.source_peak) && halfGainInfo.source_peak > 0.1f,
          "v4: info.source_peak is unchanged by the channel Gain (the baseline value, nonzero)");
    Check(NearlyEqual(channelInfo.source_peak, 0.5f),
          "v4: info.source_peak is the raw pre-pan sample peak (0.5) while peak_left is the post-pan 0.25");
    Check(chipestra_set_logical_channel_muted(handle, 0, 1) == CHIPESTRA_OK, "v4: mute channel 0");
    Check(waitForFreshBlocks(2), "v4: after mute: playback renders fresh blocks");
    ChipestraLogicalChannelInfo mutedInfo{};
    Check(chipestra_get_logical_channel_info(handle, 0, &mutedInfo) == CHIPESTRA_OK, "v4: info while muted == CHIPESTRA_OK");
    Check(mutedInfo.peak_left == 0.0f && NearlyEqual(mutedInfo.source_peak, channelInfo.source_peak),
          "v4: muting zeroes peak_left but source_peak still reports the song's sound");
    Check(chipestra_set_logical_channel_muted(handle, 0, 0) == CHIPESTRA_OK, "v4: unmute channel 0");
    Check(waitForFreshBlocks(2), "v4: after unmute: playback renders fresh blocks");

    // Channel pan: full-left (-1) sends everything to the left channel only
    // (Mixer's linear pan law, MixerPanLaw's own -1.0 case: right ch = 0).
    Check(chipestra_set_logical_channel_pan(handle, 0, -1.0f) == CHIPESTRA_OK, "chipestra_set_logical_channel_pan(0, -1.0) == CHIPESTRA_OK");
    Check(waitForFreshBlocks(2), "after pan change: playback renders fresh blocks");
    Check(chipestra_get_master_state(handle, &state) == CHIPESTRA_OK, "after pan change: chipestra_get_master_state() == CHIPESTRA_OK");
    Check(NearlyEqual(state.peak_right, 0.0f), "channel pan: full-left pan silences the right channel entirely");
    Check(state.peak_left > 0.0f, "channel pan: left channel still carries the (gain-halved) signal");

    // The same full-left pan is directly observable per-channel too, proving the
    // L/R split is tracked per source channel and not just reconstructible from
    // the (single-channel) master meter.
    Check(chipestra_get_logical_channel_info(handle, 0, &channelInfo) == CHIPESTRA_OK,
          "after pan change: chipestra_get_logical_channel_info() == CHIPESTRA_OK");
    Check(NearlyEqual(channelInfo.peak_right, 0.0f), "channel pan: info.peak_right is silenced by full-left pan");
    Check(channelInfo.peak_left > 0.0f, "channel pan: info.peak_left still carries the signal");

    // Channel mute: reported as a completely silent block.
    Check(chipestra_set_logical_channel_muted(handle, 0, 1) == CHIPESTRA_OK, "chipestra_set_logical_channel_muted(0, true) == CHIPESTRA_OK");
    Check(waitForFreshBlocks(2), "after channel mute: playback renders fresh blocks");
    Check(chipestra_get_master_state(handle, &state) == CHIPESTRA_OK, "after channel mute: chipestra_get_master_state() == CHIPESTRA_OK");
    Check(state.peak_left == 0.0f && state.peak_right == 0.0f, "channel mute: silences the real mixed output exactly");

    // Restore channel state, then exercise the master stage independently.
    Check(chipestra_set_logical_channel_muted(handle, 0, 0) == CHIPESTRA_OK, "restore: unmute channel 0");
    Check(chipestra_set_logical_channel_pan(handle, 0, 0.0f) == CHIPESTRA_OK, "restore: re-center channel 0's pan");
    Check(chipestra_set_logical_channel_gain(handle, 0, 1.0f) == CHIPESTRA_OK, "restore: reset channel 0's gain to unity");

    // Master gain: a second, independent multiplicative stage after MixBlock.
    Check(chipestra_set_master_gain(handle, 0.5f) == CHIPESTRA_OK, "chipestra_set_master_gain(0.5) == CHIPESTRA_OK");
    Check(waitForFreshBlocks(2), "after master gain change: playback renders fresh blocks");
    Check(chipestra_get_master_state(handle, &state) == CHIPESTRA_OK, "after master gain change: chipestra_get_master_state() == CHIPESTRA_OK");
    Check(NearlyEqual(state.peak_left, 0.125f) && NearlyEqual(state.peak_right, 0.125f),
          "master gain: 0.5 halves the restored ~0.25 baseline peak to ~0.125");

    // Master mute: silences the final output regardless of channel state.
    Check(chipestra_set_master_muted(handle, 1) == CHIPESTRA_OK, "chipestra_set_master_muted(true) == CHIPESTRA_OK");
    Check(waitForFreshBlocks(2), "after master mute: playback renders fresh blocks");
    Check(chipestra_get_master_state(handle, &state) == CHIPESTRA_OK, "after master mute: chipestra_get_master_state() == CHIPESTRA_OK");
    Check(state.peak_left == 0.0f && state.peak_right == 0.0f, "master mute: silences the real mixed output exactly");

    chipestra_stop_playback(handle);
    chipestra_stop_audio_output(handle);
    chipestra_unload_machine(handle);
    chipestra_destroy_instance(handle);
}

}  // namespace

int main() {
    // Unbuffered stdout: if a check ever hard-crashes the process (rather
    // than just failing), the last line printed is the test it died in
    // instead of being lost in a stale buffer.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    TestMachineDeterminism();
    AbiCycleDeterminism();
    InvalidTransitionsAndUnknownMachine();
    ClockConverterRoundTrip();
    SchedulerDeterminism();
    AddressSpaceBehavior();
    DeviceManagerOwnership();
    MixerPanLaw();
    MixerMute();
    MixerLinearSummation();
    TestMachineToMixerIntegration();
    MixerPipelineDeterminism();
    EndpointResamplerBehavior();
    NullAudioOutputBehavior();
    PlaybackStateMachine();
    SendCommandAbi();
    PlaybackNullArgumentSafety();
    PlaybackStopStartCyclesAreClean();
    BoundedFrameQueueBehavior();
    WasapiSharedOutputBehavior();
    WasapiOutputAbi();
    AudioOutputDiagnosticsNullArgumentSafety();
    OutputRateSelectionAbi();
    MachineRegistryBehavior();
    MachineRegistryAbi();
    ChannelMeterInfoAndSettersRoundTrip();
    SendCommandClearsChannelLevelReadings();
    ChannelMeterAndMasterAffectMixedOutput();

    ResourceResolverLooseFileResolved();
    ResourceResolverMissing();
    ResourceResolverAmbiguous();
    ResourceResolverHashMismatch();
    ResourceResolverUnspecifiedHashNotRequired();
    ResourceResolverCorrectHashAccepted();
    ResourceResolverZipEntryResolved();
    ResourceResolverLooseAndZipBothCandidates();
    ResourceResolverSizeFilterNarrowsCandidates();
    ResourceResolverOptionalResourceMissingDoesNotBlock();
    ResourceResolverRejectsOversizedZipEntry();
    ResourceResolverDeterministicAcrossRepeatedResolves();
    ZipIndexRejectsPathTraversal();
    ZipIndexRejectsUnsupportedCompressionMethod();
    ZipIndexEnforcesUncompressedSizeCap();

    ResourceResolverArchiveScopeDisambiguatesZips();
    ResourceResolverArchiveScopeDisambiguatesFolders();
    ResourceResolverArchiveScopeExcludesOtherArchives();
    ResourceResolverArchiveScopeWorksWithoutHash();
    ResourceResolverNoArchiveRemainsAmbiguousAcrossArchives();
    ResourceResolverArchiveScopeDeterministicAcrossRepeatedResolves();
    ResourceResolverArchiveScopeSameNameAtDifferentPathsIsAmbiguousWithPathDetail();

    ParseGameDefinitionValidXml();
    ParseGameDefinitionMissingGameAttribute();
    ParseGameDefinitionMissingRomAttribute();
    ParseGameDefinitionMissingTrackAttribute();
    ParseGameDefinitionMissingTrackIdAttribute();
    ParseGameDefinitionManufacturerIsOptional();
    ParseGameDefinitionOptionalRomAttributesAreOptional();
    ParseGameDefinitionCrc32IsParsedAsHex();
    ParseGameDefinitionNoGameElement();
    ParseGameDefinitionRejectsOldRootElement();
    ParseGameDefinitionMalformedXml();
    ParseGameDefinitionRejectsDoctype();
    ParseGameDefinitionRejectsOversizedInput();
    ParseGameDefinitionDeterministicAcrossRepeatedParses();
    GameDefinitionBuildsRequiredResourcesForResolver();
    ParseGameDefinitionArchiveIsOptional();
    ParseGameDefinitionArchiveIsParsedWhenPresent();
    GameDefinitionBuildsRequiredResourcesWithArchiveScope();
    ParseGameDefinitionTrackTypeIsOptional();
    ParseGameDefinitionTrackTypeControlWhenPresent();
    ParseGameDefinitionTrackTypeWsgWhenPresent();
    ParseGameDefinitionTrackTypeUnknownValueIsRejected();
    ParseGameDefinitionTrackLoopsIsOptional();
    ParseGameDefinitionTrackLoopsFalseWhenPresent();

    Z80BasicLoadAndCycleCounting();
    Z80IncDecBoundaryFlags();
    Z80DjnzDoesNotAffectFlags();
    Z80AddCarryOverflowFlags();
    Z80AddHl16BitFlags();
    Z80SbcAdcHl16BitZeroFlag();
    Z80JumpSkipsInterveningCode();
    Z80CallAndReturnRoundTrip();
    Z80PushPopAfRoundTripsFlags();
    Z80LdAiAndLdArReadTheCorrectRegister();
    Z80CbBitSetResOnMemory();
    Z80IndexedIxLoadAndStore();
    Z80IndexedLoadStoreKeepsRealHlOperand();
    Z80NmiServicedCorrectly();
    Z80MaskableInterruptImOneWithEiDelay();
    Z80MaskableInterruptImTwoVectorTable();
    Z80MaskableInterruptImZeroCall();
    Z80MaskableInterruptImZeroJp();
    Z80MaskableInterruptImZeroRst();
    Z80MaskableInterruptImZeroEi();
    Z80MaskableInterruptImZeroUnhandledByteDoesNothing();
    Z80HaltWakesOnInterrupt();
    Z80AddressSpaceIntegration();
    Z80ExecuteIsDeterministic();

    I8039BasicMovAndCycleCounting();
    I8039AddCarryAuxCarryFlags();
    I8039JumpWithinPage();
    I8039DjnzLoop();
    I8039CallAndReturnRoundTrip();
    I8039Port1OutputAndReadback();
    I8039ExternalBusMovx();
    I8039TimerOverflowServicesInterrupt();
    I8039ExternalIrqServicedAndRetrRestoresPsw();
    I8039MemoryBankSelectAffectsJump();
    H6280ResetState();
    H6280LoadImmediateFlagsAndCycles();
    H6280MemoryMapperTamTma();
    H6280ZeroPageAndStackUseMapperRegister1();
    H6280AddressingModesLoadFromMappedRam();
    H6280BlockTransferTiiCyclesAndData();
    H6280BlockTransferTinTddTiaTai();
    H6280BranchesAndBitBranches();
    H6280DecimalAndBinaryArithmeticFlags();
    H6280TransferFlagAndRegisterSwaps();
    H6280BitOpsTstTsbTrbRmbSmb();
    H6280StackSubroutinesAndTransfers();
    H6280InternalIoPageIsNotForwarded();
    H6280InterruptsVectorsMaskPriorityAndRti();
    H6280TimerFiresTiqAndScaleDoublesPeriod();
    H6280SpeedModeCshCsl();
    H6280CyclesConsumedThisCallAdvancesInsideExecute();
    H6280DeterministicAcrossRuns();
    M6809ResetState();
    M6809ImmediateLoadFlagsAndCycles();
    M6809AddSubFlagsOverflowAndCarry();
    M6809DirectAndExtendedAddressing();
    M6809IndexedAddressingVariants();
    M6809ShiftAndRotateOps();
    M6809IncDecNegComClr();
    M6809SixteenBitLoadStoreAndArithmetic();
    M6809BranchesShortAndLong();
    M6809JsrBsrRts();
    M6809PushPullMultipleRegisters();
    M6809LeaSetsFlagsAndLdsTracking();
    M6809TfrAndExg();
    M6809SwiAndRtiRoundTrip();
    M6809NmiMaskedUntilFirstStackWrite();
    M6809FirqPushesPartialState();
    M6809IrqMaskingAndPriorityOverFirq();
    M6809DaaAfterBcdAddition();
    M6809MulOperation();
    M6809SexSignExtends();
    M6809SyncWaitsForPendingInterrupt();
    M6809CwaiPushesStateAndWaits();
    M6809CyclesConsumedThisCallAdvancesInsideExecute();
    M6809DeterministicAcrossRuns();

    Hd63701ResetState();
    Hd63701ImmediateLoadFlagsAndCycles();
    Hd63701AddSubAdcSbcFlags();
    Hd63701DirectIndexedExtendedAddressing();
    Hd63701ShiftAndRotateOps();
    Hd63701IncDecNegComClrAccumulatorAndMemory();
    Hd63701SixteenBitLoadStoreAndArithmetic();
    Hd63701ConditionalBranches();
    Hd63701JsrBsrRts();
    Hd63701PushPullStack();
    Hd63701SwiAndRtiRoundTrip();
    Hd63701NmiEdgeTriggeredAndPriorityOverIrq1();
    Hd63701Irq1MaskedByCcI();
    Hd63701WaiPushesStateAndWaitsForInterrupt();
    Hd63701SlpWaitsWithoutPrePush();
    Hd63701TapDelaysInterruptRecognitionByOneInstruction();
    Hd63701CliDelaysInterruptRecognitionByOneInstruction();
    Hd63701DaaAfterBcdAddition();
    Hd63701MulOperation();
    Hd63701TrapOnIllegalOpcode();
    Hd63701AimOimEimTimBitManipulation();
    Hd63701XgdxAndUndocumentedOpcodes();
    Hd63701PortGpioReadWrite();
    Hd63701TimerGeneratesIrq2OnOutputCompare();
    Hd63701CyclesConsumedThisCallAdvancesInsideExecute();
    Hd63701DeterministicAcrossRuns();
    Okim6295ResetAndIdleStatus();
    Okim6295Pin7LowUsesDivisor165();
    Okim6295StartAndStopVoice();
    Okim6295InvalidStartStopIsIgnored();
    Okim6295AlreadyPlayingVoiceIgnoresNewStart();
    Okim6295RenderProducesOneSamplePerDivisorClocks();
    Okim6295VoicePlaysExactSampleCountThenStops();
    Okim6295MultipleVoicesIndependent();
    Okim6295VolumeAttenuationReducesAmplitude();
    Okim6295ResetDoesNotClearPendingPhraseHalf();
    Okim6295DeterministicAcrossInstances();

    GyrussMachineRegisteredInDefaultCatalog();
    GyrussMachineExposes16LogicalChannels();
    GyrussMachineBootSequenceReadsAndWrites();
    GyrussMachineCommandInjectionWritesAy1Registers();
    GyrussMachineCommandDispatchTriggersI8039Dac();
    GyrussMachineOnPanOverrideClearedResyncsPan();
    GyrussAyRcFilterCutoffsMatchComponentValues();
    GyrussAyRcFilterBypassDcAndReset();
    GyrussMachineAyFilterFollowsPortB();
    GyrussMixerLowPassMatchesMixerDescription();
    GyrussMixerLowPassResetClearsState();
    GyrussDefinitionXmlIsValid();
    GyrussViaDefinitionXmlRealRomBringUp();
    GyrussChannelMeterAbiDuringRealPlaybackReportsPerChannelPeak();

    VaporTrailMachineRegisteredInDefaultCatalog();
    VaporTrailMachineExposes22LogicalChannels();
    VaporTrailMachineBootHandshakeLeavesNoSongActive();
    VaporTrailMachineSendCommandAssertsIrq1AndLatchesByte();
    VaporTrailMachineReadingLatchClearsIrq1();
    VaporTrailMachineYm2151IrqReachesH6280Irq2();
    VaporTrailMachineDevicesReachableViaMemoryMap();
    VaporTrailMachineRenderIsDeterministicAcrossInstances();
    VaporTrailMachineSendCommandGatesSongSwitchWithStop();
    VaporTrailMachineSoundDeviceSyncTracksCumulativeCyclesAcrossBusAccesses();

    Cus30WaveRamIsPlainReadWrite();
    Cus30VoiceRegisterDecodeSetsFrequencyVolumeWaveformSelect();
    Cus30Register04SetsNextVoiceNoiseSw();
    Cus30Register04WrapsFromVoice7ToVoice0();
    Cus30SharedRamIsPlainReadWrite();
    Cus30Register5ReturnsLiveCounterPosition();
    Cus30RenderIsSilentWhenBothVolumesZero();
    Cus30RenderCounterFreezesWhileSilent();
    Cus30RenderProducesNonzeroSampleWhenActive();
    Cus30ResetClearsVoiceStateAndMemory();

    Dac8BitR2RSingleWriteHoldsForRestOfBlock();
    Dac8BitR2RMultipleWritesLandAtCorrectTickOffsets();
    Dac8BitR2RNoWriteHoldsPreviousBlocksFinalValue();
    Dac8BitR2RGainScalesOutputInFourSteps();
    Dac8BitR2RTwoInstancesAreIndependent();
    Dac8BitR2RReconstructIsDeterministic();

    SfMemoryMapMusicCpuRomRamAndDevices();
    SfMemoryMapSamplesCpuBankWindow();
    SfMemoryMapShortImagesAreZeroPadded();
    SfMemoryMapWithoutDevicesReadsZero();
    SfMachineRegisteredInDefaultCatalog();
    SfRomLayoutAssemblesTheSampleImage();
    SfSamplesCpuHasNoRam();
    SfSamplesCpuBankIsSelectedByIoPort();
    SfSamplesIrqRuns8000TimesPerSecondAsAHoldLine();
    SfMsmWritesFromTheSamplesCpuReachBothChips();
    SfCommandLatchAndNmiAt60Hz();
    SfSwitchingSongsMatchesAFreshMachine();
    SfMusicStopIsAResetAndNothingIsSent();
    SfResetKeepsChannelMuteGainPan();
    SfAcceptsRawButNotWsgCommands();
    SfStereoRoutingFollowsTheYm2151OutputEnables();
    SfViaDefinitionXmlRealRomBringUp();
    SfRealRomSwitchAndStopMatchAFreshMachine();
    SfResamplingFeedCompensatesYm2151AndMsm5205GroupDelay();
    DragonSpiritMachineRegisteredInDefaultCatalog();
    DragonSpiritMachineExposes18LogicalChannels();
    DragonSpiritMachineSendCommandWritesTriRam();
    DragonSpiritMachineSendCommandIsReentrantSafe();
    DragonSpiritMachineVblankIrqAssertsAndCanBeAcked();
    DragonSpiritMachineBankSwitchSelectsAmongEightBanks();
    DragonSpiritMachineDevicesReachableViaMemoryMap();
    DragonSpiritMachineRenderIsDeterministicAcrossInstances();
    DragonSpiritMachineMcuBootsAlongsideAudiocpu();
    DragonSpiritMachineTriRamIsSharedBetweenAudiocpuAndMcu();
    DragonSpiritMachineMcuVblankIrq1AssertsAndCanBeAcked();
    DragonSpiritMachineMcuBankSwitchSelectsAmongTwentyFourBanks();
    DragonSpiritMachineDac0Dac1ReceiveValuesViaMcuBusWrites();
    DragonSpiritAssembleSoundRomsThrowsWhenRequiredResourceMissing();
    DragonSpiritAssembleSoundRomsMirrorsVoice0AcrossChip0();
    DragonSpiritAssembleSoundRomsPlacesVoiceChipsAtCorrectOffsets();
    DragonSpiritViaDefinitionXmlRealRomBringUp();
    DragonSpiritFirstCommandAfterInitializeDoesNotReset();
    DragonSpiritLaterCommandsReset();
    DragonSpiritResetKeepsChannelMuteGainPan();
    DragonSpiritSwitchingSongsMatchesAFreshMachine();
    DragonSpiritWsgWritesTheRequestTableAndLeavesTriRamAlone();
    DragonSpiritAcceptsWsgOnlyUpToTheLastNumber();
    DragonSpiritWsgAfterACommandResetsFirst();
    DragonSpiritWsgRealRomPlaysEveryListedNumber();

    VaporTrailDefinitionXmlIsValid();
    VaporTrailViaDefinitionXmlRealRomBringUp();

    BubbleSystemBiosVariantIsRegisteredAndNeedsOnlySoundResources();
    BubbleSystemBiosModeNeverRunsMainCpuAndPulsesSoundNmiPerFrame();
    BubbleSystemBiosModeIsDeterministic();
    BubbleSystemBiosModeDeliversPlayerCommandsToSoundZ80();
    BubbleSystemBiosModeSendCommandRestartsBoardFromPowerOn();
    BubbleSystemBiosModeStaleCommandDoesNotLeakIntoNextColdRestart();
    BubbleSystemPublishesOnlyTheBiosOnlyEntry();
    BubbleSystemBiosDefinitionXmlIsValid();
    BubbleSystemBiosViaDefinitionXmlRealRomBringUp();

    M68000ResetVectorLoadsInitialSpAndPc();
    M68000MoveqAndAddSetsFlagsCorrectly();
    M68000SubtractionSetsCarryAndOverflow();
    M68000BranchAndSubroutineCallRoundTrips();
    M68000DbccLoopCountsDownCorrectly();
    M68000AutovectorLevel7InterruptTakesExpectedFrame();
    M68000MaskedInterruptIsNotTakenUntilUnmasked();
    M68000MovemPredecrementAndPostincrementRoundTrip();
    M68000ShiftAndRotateMatchStandardSemantics();
    M68000ExecuteIsDeterministic();
    M68000MovepTransfersOddBytesOnly();
    M68000AbcdSbcdNbcdPerformDecimalCorrection();
    M68000AddqSubqWordToAddressRegisterUsesFull32Bits();

    XBoardMachineBootSequenceReadsAndWrites();
    XBoardMachineCommandInjectionTriggersNmi();
    XBoardMachineRepeatedCommandsReArmNmi();
    XBoardMachineRegisteredInDefaultCatalog();
    XBoardMachineClockCarryForwardBoundsLongRunDrift();
    XBoardMachineChannelPanAutoSyncsUnlessOverridden();

    OutRunMachineBootSequenceReadsAndWrites();
    OutRunMachineCommandInjectionTriggersNmi();
    OutRunMachineRepeatedCommandsReArmNmi();
    OutRunMachineRegisteredInDefaultCatalog();

    Ym2151RegisterAccessAndStatusBusy();
    Ym2151TimerAFiresAfterProgrammedPeriod();
    Ym2151TimerBFiresAfterProgrammedPeriod();
    Ym2151TimerIrqAssertsAndClears();
    Ym2151ChannelOutputReachesPerChannelBuffers();
    Ym2151RenderIsDeterministicAndLosesNoChipTime();
    XBoardMachineYm2151IrqAndCommandNmiDoNotConflict();

    Ym2203RegisterAccessAndStatusBusy();
    Ym2203TimerAFiresAfterProgrammedPeriod();
    Ym2203TimerBFiresAfterProgrammedPeriod();
    Ym2203ChannelOutputReachesPerChannelBuffers();
    Ym2203RenderIsDeterministicAndLosesNoChipTime();

    Msm5205RenderProducesOneSamplePerPeriodAndCarriesPartialClocks();
    Msm5205VckHandlerFiresBeforeThatSamplesDecode();
    Msm5205ResetPinForcesSilenceRegardlessOfData();
    Msm5205RepeatedStrongNibbleProducesGrowingNonSilentSignal();
    Msm5205SlaveDecodesOncePerWriteAtTheWriteTime();
    Msm5205SlaveResetBitAndDataNibble();
    Msm5205SlaveWriteTimingIsIndependentOfRenderChunking();

    DariusCommandInterfaceSlaveNmiGatedByEnableFlag();
    DariusCommandInterfaceSendDeliversByteViaTwoNibbles();
    DariusCommandInterfaceSlaveReadClearsStatusAndNmi();

    DariusMachineConstructsAndRegistersThirteenChannels();
    DariusMachineTwoZ80sCoordinateViaSharedAdpcmLatch();
    DariusMachineCommandInjectionDeliversViaPc060haToAudiocpuNmi();
    DariusMachineRepeatedCommandsReAssertNmi();
    DariusMachineFirstCommandAfterBootSendsNoStop();
    DariusMachineSwitchSendsStopThenTheCommand();
    DariusMachineResetForgetsTheActiveSong();
    DariusMachineRegisteredInDefaultCatalog();
    DariusMachineOnPanOverrideClearedResyncsPan();

    DariusDefinitionXmlIsValid();
    DariusViaDefinitionXmlRealRomBringUp();
    DariusAudiocpuPollingDiagnostic();
    DariusSongSwitchDoesNotMixSongs();
    DariusSwitchFromBgmToSeStopsTheBgm();
    DariusSongSwitchThroughUnrenderedStopDoesNotMixSongs();

    Ym2610RegisterAccessAndStatusBusy();
    Ym2610FmChannelOutputReachesPerChannelBuffers();
    Ym2610FmHighBankChannelsHonourPanRegister();
    Ym2610AdpcmAChannelPlaysSampleFromExternalRom();
    Ym2610AdpcmBChannelPlaysSampleFromExternalRom();
    Ym2610SsgChannelOutputReachesPerChannelBuffers();
    Ym2610RenderIsDeterministicAndLosesNoChipTime();

    NinjaWarriorsMachineConstructsAndRegistersFourteenChannels();
    NinjaWarriorsMachineCommandInjectionDeliversViaTc0140sytToAudiocpuNmi();
    NinjaWarriorsMachineRepeatedCommandsReAssertNmi();
    NinjaWarriorsMachineRegisteredInDefaultCatalog();
    NinjaWarriorsMachineFmAdpcmChannelsStayCenteredUnlessOverridden();

    NinjaWarriorsDefinitionXmlIsValid();
    NinjaWarriorsViaDefinitionXmlRealRomBringUp();
    NinjaWarriorsNoStrayVoiceAfterBoot();
    NinjaWarriorsAudiocpuPollingDiagnostic();
    NinjaWarriorsYm2610RegisterTraceDiagnostic();
    NinjaWarriorsKeyOnSweepDiagnostic();

    MetalBlackMachineConstructsAndRegistersFourteenChannels();
    MetalBlackMachineCommandInjectionDeliversViaTc0140sytToAudiocpuNmi();
    MetalBlackMachineRegisteredInDefaultCatalog();

    MetalBlackDefinitionXmlIsValid();
    MetalBlackViaDefinitionXmlRealRomBringUp();
    MetalBlackSongSwitchDoesNotMixSongs();
    MetalBlackAudiocpuPollingDiagnostic();
    MetalBlackYm2610RegisterTraceDiagnostic();
    MetalBlackKeyOnSweepDiagnostic();

    SegaPcmVoiceRegisterReadback();
    SegaPcmVoiceEnableAndBankSelect();
    SegaPcmLoopAndStopAtEndAddress();
    SegaPcmVoiceChannelOutputAndPan();
    SegaPcmRenderIsDeterministicAndLosesNoMasterClocks();
    XBoardMachineSegaPcmAudioReachesMixer();

    Ay8910RegisterSemanticsUpdateInternalState();
    Ay8910PortWriteCallbackFiresOnEnableEdge();
    Ay8910ToneGeneratorOutputMatchesProgrammedPeriod();
    Ay8910NoiseGeneratorAdvancesThePseudoRandomSequence();
    Ay8910EnvelopeGeneratorFollowsAKnownShape();
    Ay8910ChannelOutputReachesPerChannelBuffers();
    Ay8910LegacyNormalizedMixHasNoDcOffsetAndMatchesMameLevels();
    Ay8910RenderIsDeterministicAndLosesNoChipTime();

    GradiusMachineBootSequenceReadsAndWrites();
    GradiusMachineCommandInjectionTriggersIrq();
    GradiusMachineRepeatedCommandsReAssertIrq();
    GradiusMachineRegisteredInDefaultCatalog();
    GradiusMachineMainCpuSharedRamBridgeReachesSoundZ80();
    GradiusMachineMainCpuSharedRamBridgeDropsEvenByteWrite();
    GradiusMachineMainCpuSoundlatchAndOutlatchReachZ80Irq();

    K005289RegisterSemanticsUpdateInternalState();
    K005289WaveformGenerationMatchesProgrammedFrequencyAndProm();
    K005289ChannelOutputReachesPerVoiceBuffers();

    Vlm5030RegisterAndPinSemanticsUpdateInternalState();
    Vlm5030FrameParsingDecodesKnownBitPackedVoiceData();
    Vlm5030ChannelOutputReachesItsBuffer();
    Vlm5030FallingEnergyInterpolationStaysWithinTheFrameEnergies();

    SrcPreservesDcLevel();
    SrcPreservesSineAmplitude();
    SrcBlockBoundaryIsContinuous();
    SrcIsDeterministic();
    SrcHandlesNonIntegerRatioAndProducesRequestedCount();
    SrcAttenuatesImagesOnUpsample();
    SrcAntiAliasPreservesDcOnDownsample();
    SrcAntiAliasFrequencyResponse();
    SoxrSrcPreservesDcLevel();
    SoxrSrcAntiAliasFrequencyResponse();
    SoxrSrcIsDeterministic();
    SoxrSrcSteadyStateAcrossManySmallBlocksNeedsNoRepeatedPadding();
    ResamplingMixerFeedPassthroughAndResample();
    XBoardMachineYm2151ReachesMixerThroughResampler();
    ResamplingMixerFeedCompensatesGroupDelayAcrossDifferentNativeRates();

    AfterBurner2DefinitionXmlIsValid();
    AfterBurner2ReferenceBringUp();
    ChannelMeterAbiDuringRealPlaybackReportsNonZeroPeak();
    OutRunDefinitionXmlIsValid();
    OutRunTrackSwitchReferenceBringUp();
    GradiusAy8910AndK005289RealRomBringUp();
    GradiusDefinitionXmlIsValid();
    GradiusViaDefinitionXmlRealRomBringUp();

    if (g_failures == 0) {
        std::printf("native lifecycle tests: all checks passed\n");
        return 0;
    }
    std::printf("native lifecycle tests: %d check(s) FAILED\n", g_failures);
    return 1;
}
