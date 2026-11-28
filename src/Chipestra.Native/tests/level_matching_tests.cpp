// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "level_matching_tests.h"

#include "chipestra_abi.h"
#include "level_matching.h"
#include "mixer.h"
#include "test_support.h"
#include "xml_definition_parser.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kFrames = Mixer::kBlockSizeFrames;

std::vector<StereoFrame> MixConstant(float value, const Mixer::LevelMatching* level, float* sourcePeak = nullptr) {
    LogicalChannelSink sink;
    sink.RegisterChannel(0);
    std::vector<float> samples(kFrames, value);
    sink.WriteSamples(0, samples.data(), samples.size());
    std::vector<LogicalChannel> channels(1);
    channels[0].Id = 0;
    Mixer mixer;
    std::vector<StereoFrame> out;
    mixer.MixBlock(sink, channels, kFrames, out, level);
    if (sourcePeak != nullptr) *sourcePeak = channels[0].SourcePeak;
    return out;
}

bool Near(float a, float b, float tolerance = 1e-5f) { return std::fabs(a - b) <= tolerance; }

}  // namespace

void LevelMatchingGainRaisesAndLowersTheMix() {
    std::printf("LevelMatchingGainRaisesAndLowersTheMix\n");
    // pan 0 gives each side 0.5 of the sample: a sample of 0.2 is 0.1 on each side without level matching
    const std::vector<StereoFrame> plain = MixConstant(0.2f, nullptr);
    Check(Near(plain[10].left, 0.1f), "without level matching the sample passes as before");

    Mixer::LevelMatching up;
    up.gainStart = up.gainEnd = LevelMatchingRamp::DbToGain(6.0f);
    const std::vector<StereoFrame> raised = MixConstant(0.2f, &up);
    Check(Near(raised[10].left, 0.1f * 1.9952623f), "+6 dB raises the mix by 6 dB");
    Mixer::LevelMatching down;
    down.gainStart = down.gainEnd = LevelMatchingRamp::DbToGain(-12.0f);
    const std::vector<StereoFrame> lowered = MixConstant(0.2f, &down);
    Check(Near(lowered[10].left, 0.1f * 0.25118864f), "-12 dB lowers the mix by 12 dB");
    Mixer::LevelMatching unity;
    const std::vector<StereoFrame> same = MixConstant(0.2f, &unity);
    Check(std::memcmp(&same[10], &plain[10], sizeof(StereoFrame)) == 0, "0 dB with the limiter is the same bits for a quiet signal");
    Check(LevelMatchingRamp::DbToGain(0.0f) == 1.0f, "0 dB is exactly a gain of 1");
}

void LevelMatchingLimiterKeepsTheMixBelowFullScale() {
    std::printf("LevelMatchingLimiterKeepsTheMixBelowFullScale\n");
    Mixer::LevelMatching loud;
    loud.gainStart = loud.gainEnd = LevelMatchingRamp::DbToGain(20.0f);
    const std::vector<StereoFrame> out = MixConstant(0.3f, &loud);
    Check(out[10].left < 1.0f && out[10].left > Mixer::kLimiterThreshold, "a mix far above full scale is limited below 1.0, above the knee");
    // without the limiter the hard clamp takes it
    Mixer::LevelMatching noLimiter = loud;
    noLimiter.limiter = false;
    Check(MixConstant(0.3f, &noLimiter)[10].left == 1.0f, "without the limiter the clamp gives exactly 1.0");

    float previous = 0.0f;
    bool monotonic = true, below = true, symmetric = true;
    for (float x = 0.0f; x < 50.0f; x += 0.01f) {
        const float y = Mixer::Limit(x);
        monotonic = monotonic && y >= previous;
        below = below && y <= 1.0f;
        symmetric = symmetric && Mixer::Limit(-x) == -y;
        previous = y;
    }
    Check(monotonic, "the limiter is monotonic");
    Check(below, "the limiter never exceeds 1.0");
    Check(symmetric, "the limiter is odd symmetric");
    Check(Near(Mixer::Limit(Mixer::kLimiterThreshold + 1e-4f), Mixer::kLimiterThreshold + 1e-4f, 1e-6f),
          "the limiter is continuous at the knee");
}

void LevelMatchingLimiterIsDeterministicAndLeavesQuietSignalsAlone() {
    std::printf("LevelMatchingLimiterIsDeterministicAndLeavesQuietSignalsAlone\n");
    bool quietUnchanged = true;
    for (float x = -0.89f; x <= 0.89f; x += 0.001f) quietUnchanged = quietUnchanged && Mixer::Limit(x) == x;
    Check(quietUnchanged, "a sample up to -1 dBFS passes the limiter unchanged");

    auto run = []() {
        LevelMatchingRamp ramp;
        ramp.Reset(false, 0.0f);
        std::vector<std::uint32_t> bits;
        for (int block = 0; block < 60; ++block) {
            Mixer::LevelMatching lm;
            const bool on = ramp.NextBlock(true, 9.5f, lm);
            // a deterministic input with peaks that need the limiter
            LogicalChannelSink sink;
            sink.RegisterChannel(0);
            std::vector<float> samples(kFrames);
            for (std::size_t i = 0; i < kFrames; ++i) samples[i] = 0.9f * std::sin(0.05f * static_cast<float>(i + block * kFrames));
            sink.WriteSamples(0, samples.data(), samples.size());
            std::vector<LogicalChannel> channels(1);
            channels[0].Id = 0;
            Mixer mixer;
            std::vector<StereoFrame> out;
            mixer.MixBlock(sink, channels, kFrames, out, on ? &lm : nullptr);
            for (const StereoFrame& f : out) {
                std::uint32_t l, r;
                std::memcpy(&l, &f.left, 4);
                std::memcpy(&r, &f.right, 4);
                bits.push_back(l);
                bits.push_back(r);
            }
        }
        return bits;
    };
    Check(run() == run(), "the same input gives the same output, bit for bit, through the ramp and the limiter");
}

void LevelMatchingRampSwitchesWithoutAStepAndEndsInBypass() {
    std::printf("LevelMatchingRampSwitchesWithoutAStepAndEndsInBypass\n");
    LevelMatchingRamp ramp;
    Mixer::LevelMatching lm;
    Check(!ramp.NextBlock(false, 7.0f, lm), "off: no level matching is applied, whatever the game's gain is");

    // switch on: the gain climbs by at most kRampDbPerBlock a block and is continuous from block to block
    float previousEnd = 1.0f;
    int blocks = 0;
    bool continuous = true, stepSmall = true;
    for (; blocks < 200; ++blocks) {
        ramp.NextBlock(true, 7.0f, lm);
        continuous = continuous && lm.gainStart == previousEnd;
        const float stepDb = 20.0f * std::log10(lm.gainEnd / lm.gainStart);
        stepSmall = stepSmall && stepDb <= LevelMatchingRamp::kRampDbPerBlock + 1e-3f;
        previousEnd = lm.gainEnd;
        if (lm.gainEnd == lm.gainStart && blocks > 0) break;
    }
    Check(continuous, "the gain of a block starts where the last one ended");
    Check(stepSmall, "the gain moves by 0.4 dB or less per block");
    Check(Near(20.0f * std::log10(previousEnd), 7.0f, 1e-3f) && blocks >= 17 && blocks <= 19,
          "it reaches the game's gain after about 18 blocks (7 dB / 0.4 dB)");

    // switch off: it ramps down and then goes back to the bypass
    int down = 0;
    while (ramp.NextBlock(false, 7.0f, lm) && down < 200) ++down;
    Check(down >= 17 && down <= 19, "switching off ramps back to 0 dB in about 18 blocks");
    Check(!ramp.NextBlock(false, 7.0f, lm), "and then the Mixer is called without level matching again");

    // a jump at the start of playback
    ramp.Reset(true, -5.0f);
    ramp.NextBlock(true, -5.0f, lm);
    Check(Near(20.0f * std::log10(lm.gainStart), -5.0f, 1e-3f), "Reset starts at the game's gain, without a ramp");
}

void LevelMatchingDoesNotChangeTheSongEndJudgement() {
    std::printf("LevelMatchingDoesNotChangeTheSongEndJudgement\n");
    float plainPeak = 0.0f, matchedPeak = 0.0f;
    MixConstant(0.3f, nullptr, &plainPeak);
    Mixer::LevelMatching up;
    up.gainStart = up.gainEnd = LevelMatchingRamp::DbToGain(10.0f);
    MixConstant(0.3f, &up, &matchedPeak);
    Check(plainPeak == matchedPeak && plainPeak > 0.0f, "SourcePeak (the song-end judgement) is the same with level matching");
}

void LevelMatchingGameDefinitionAttributeAndAbiFunction() {
    std::printf("LevelMatchingGameDefinitionAttributeAndAbiFunction\n");
    auto parse = [](const char* attribute, float& gain) {
        const std::string xml = std::string("<chipestra><game id=\"t\" title=\"T\" machine=\"M\" variant=\"V\" ") + attribute +
                                "><resources/><tracks><track id=\"1\" title=\"S\" command=\"1\"/></tracks></game></chipestra>";
        ChipestraGameDefinitionHandle handle = nullptr;
        char text[256] = {};
        const ChipestraResult r = chipestra_parse_game_definition(xml.c_str(), static_cast<uint32_t>(xml.size()), &handle, text, sizeof(text));
        gain = 99.0f;
        if (r == CHIPESTRA_OK && handle != nullptr) {
            ChipestraGameInfo info{};
            chipestra_game_definition_get_info(handle, &info);
            gain = info.level_gain_db;
            chipestra_free_game_definition(handle);
        }
        return r;
    };
    float gain = 0.0f;
    Check(parse("", gain) == CHIPESTRA_OK && gain == 0.0f, "a game without level_gain_db has 0 dB");
    Check(parse("level_gain_db=\"-7.5\"", gain) == CHIPESTRA_OK && gain == -7.5f, "level_gain_db=-7.5 is read");
    Check(parse("level_gain_db=\"+4.25\"", gain) == CHIPESTRA_OK && gain == 4.25f, "level_gain_db=+4.25 is read");
    Check(parse("level_gain_db=\"loud\"", gain) != CHIPESTRA_OK, "a text is refused");
    Check(parse("level_gain_db=\"31\"", gain) != CHIPESTRA_OK, "a gain beyond 30 dB is refused");
    Check(parse("level_gain_db=\"\"", gain) != CHIPESTRA_OK, "an empty value is refused");

    ChipestraHandle handle = nullptr;
    Check(chipestra_create_instance(&handle) == CHIPESTRA_OK, "setup: an instance");
    Check(chipestra_set_level_matching(handle, 1, -12.0f) == CHIPESTRA_OK, "chipestra_set_level_matching accepts a gain");
    Check(chipestra_set_level_matching(handle, 0, 0.0f) == CHIPESTRA_OK, "and the off state");
    Check(chipestra_set_level_matching(handle, 1, 31.0f) == CHIPESTRA_E_INVALID_ARGUMENT, "a gain beyond 30 dB is an invalid argument");
    Check(chipestra_set_level_matching(handle, 1, std::nanf("")) == CHIPESTRA_E_INVALID_ARGUMENT, "NaN is an invalid argument");
    Check(chipestra_set_level_matching(nullptr, 1, 0.0f) == CHIPESTRA_E_INVALID_ARGUMENT, "a null handle is an invalid argument");
    chipestra_destroy_instance(handle);
    Check(chipestra_set_level_matching(handle, 1, 0.0f) == CHIPESTRA_E_INVALID_STATE, "a destroyed instance is an invalid state");
}
