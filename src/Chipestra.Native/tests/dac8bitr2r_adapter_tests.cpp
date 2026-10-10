// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "dac8bitr2r_adapter_tests.h"

#include "devices/sound/dac8bitr2r/dac8bitr2r_device_adapter.h"
#include "test_support.h"

#include <cmath>
#include <vector>

namespace {
constexpr double kTicksPerCycle = 2.0;  // arbitrary, simple ratio for hand-checkable tick offsets
}

void Dac8BitR2RSingleWriteHoldsForRestOfBlock() {
    Dac8BitR2RDeviceAdapter dac;
    dac.BeginBlock();
    dac.RecordWrite(0, 0xFF);  // max byte -> (255-128)/128 = 0.9921875

    std::vector<float> out;
    dac.Reconstruct(out, 8, kTicksPerCycle);
    bool allHeld = true;
    for (float v : out) {
        if (std::fabs(v - (255.0f - 128.0f) / 128.0f) > 1e-6f) allHeld = false;
    }
    Check(out.size() == 8, "Reconstruct resizes output to the requested tick count");
    Check(allHeld, "a single write at tick 0 holds for the entire block");
}

void Dac8BitR2RMultipleWritesLandAtCorrectTickOffsets() {
    Dac8BitR2RDeviceAdapter dac;
    dac.BeginBlock();
    // cycle 0 -> tick 0, cycle 2 -> tick 4, cycle 3 -> tick 6 (kTicksPerCycle=2.0)
    dac.RecordWrite(0, 128);  // (128-128)/128 = 0.0
    dac.RecordWrite(2, 255);  // ~0.9921875
    dac.RecordWrite(3, 0);    // (0-128)/128 = -1.0

    std::vector<float> out;
    dac.Reconstruct(out, 8, kTicksPerCycle);
    Check(out.size() == 8, "output sized to requested tick count");
    Check(std::fabs(out[0] - 0.0f) < 1e-6f, "ticks 0-3 hold the first write's value (0.0)");
    Check(std::fabs(out[3] - 0.0f) < 1e-6f, "tick 3 still holds the first write's value");
    Check(std::fabs(out[4] - (255.0f - 128.0f) / 128.0f) < 1e-6f, "tick 4 (cycle 2) picks up the second write");
    Check(std::fabs(out[5] - (255.0f - 128.0f) / 128.0f) < 1e-6f, "tick 5 still holds the second write's value");
    Check(std::fabs(out[6] - (-1.0f)) < 1e-6f, "tick 6 (cycle 3) picks up the third write (-1.0)");
    Check(std::fabs(out[7] - (-1.0f)) < 1e-6f, "tick 7 holds the third write's value through the end of the block");
}

void Dac8BitR2RNoWriteHoldsPreviousBlocksFinalValue() {
    Dac8BitR2RDeviceAdapter dac;
    dac.BeginBlock();
    dac.RecordWrite(0, 200);
    std::vector<float> firstBlock;
    dac.Reconstruct(firstBlock, 4, kTicksPerCycle);
    const float expected = (200.0f - 128.0f) / 128.0f;

    dac.BeginBlock();  // no RecordWrite() calls this block - matches a real R2R DAC that never auto-resets to 0
    std::vector<float> secondBlock;
    dac.Reconstruct(secondBlock, 4, kTicksPerCycle);

    bool allHeld = true;
    for (float v : secondBlock) {
        if (std::fabs(v - expected) > 1e-6f) allHeld = false;
    }
    Check(allHeld, "a block with zero writes holds the previous block's final value throughout (no reset to 0)");
}

void Dac8BitR2RGainScalesOutputInFourSteps() {
    Dac8BitR2RDeviceAdapter dac;
    const float steps[] = {0.25f, 0.5f, 0.75f, 1.0f};
    for (float gain : steps) {
        dac.SetGain(gain);
        Check(dac.Gain() == gain, "Gain() reports back the value SetGain() was given");
        dac.BeginBlock();
        dac.RecordWrite(0, 255);
        std::vector<float> out;
        dac.Reconstruct(out, 1, kTicksPerCycle);
        const float expected = (255.0f - 128.0f) / 128.0f * gain;
        Check(std::fabs(out[0] - expected) < 1e-6f, "current gain (dac_gain_w's own 4-step scale) is applied at Reconstruct() time");
    }
}

void Dac8BitR2RTwoInstancesAreIndependent() {
    Dac8BitR2RDeviceAdapter dac0;
    Dac8BitR2RDeviceAdapter dac1;
    dac0.SetGain(1.0f);
    dac1.SetGain(0.25f);
    dac0.BeginBlock();
    dac1.BeginBlock();
    dac0.RecordWrite(0, 255);
    dac1.RecordWrite(0, 255);

    std::vector<float> out0;
    std::vector<float> out1;
    dac0.Reconstruct(out0, 1, kTicksPerCycle);
    dac1.Reconstruct(out1, 1, kTicksPerCycle);
    Check(std::fabs(out0[0] - (255.0f - 128.0f) / 128.0f) < 1e-6f, "dac0's own gain (1.0) applies to dac0's own output");
    Check(std::fabs(out1[0] - (255.0f - 128.0f) / 128.0f * 0.25f) < 1e-6f, "dac1's own gain (0.25) applies independently to dac1's own output");
}

void Dac8BitR2RReconstructIsDeterministic() {
    auto run = [] {
        Dac8BitR2RDeviceAdapter dac;
        dac.SetGain(0.75f);
        std::vector<float> block1, block2, block3;
        dac.BeginBlock();
        dac.RecordWrite(0, 10);
        dac.RecordWrite(5, 200);
        dac.Reconstruct(block1, 16, kTicksPerCycle);
        dac.BeginBlock();
        dac.RecordWrite(1, 90);
        dac.Reconstruct(block2, 16, kTicksPerCycle);
        dac.BeginBlock();  // no writes - holds block2's final value
        dac.Reconstruct(block3, 16, kTicksPerCycle);

        std::vector<float> all;
        all.insert(all.end(), block1.begin(), block1.end());
        all.insert(all.end(), block2.begin(), block2.end());
        all.insert(all.end(), block3.begin(), block3.end());
        return all;
    };
    std::vector<float> a = run();
    std::vector<float> b = run();
    Check(a == b, "two independent instances fed the identical multi-block write sequence produce bit-identical output");
}
