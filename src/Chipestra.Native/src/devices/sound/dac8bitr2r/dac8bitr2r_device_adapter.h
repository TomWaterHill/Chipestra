// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include <cstdint>
#include <vector>

// Models one 8-bit R2R DAC driven directly by a CPU: the CPU writes a raw byte
// whenever its program decides to, and the analog output holds that value
// until the next write. There is no sample-rate register or internal clock
// (a bare resistor ladder, not a wavetable/ADPCM device).
//
// Several writes can land in the same render block, and holding only the last
// one would collapse the real waveform. So every write is recorded with its
// in-block cycle offset and replayed at the correct tick position when the
// block is filled; the final value carries into the next block as the held
// starting point.
//
// Gain (the board's 4-step output gain control, 0.25/0.5/0.75/1.0) scales the
// DAC's analog output stage, not the digital write: the gain current at
// Reconstruct() time is applied to every write recorded in that block. Gain
// changes are rare, so this is close enough.
class Dac8BitR2RDeviceAdapter {
public:
    void SetGain(float gain) { gain_ = gain; }
    float Gain() const { return gain_; }

    // Returns the DAC to a defined starting state: silent (no held sample), unity gain.
    void Reset() {
        gain_ = 1.0f;
        holdBeforeBlock_ = 0.0f;
        events_.clear();
    }

    // Called once per Render() block, before any RecordWrite() calls for
    // that block (also for warm-up blocks with no sink), so stale cycle
    // offsets never leak into a later block.
    void BeginBlock() { events_.clear(); }

    // Called from inside the driving CPU's Execute() at the moment it writes
    // to this DAC; cyclesInBlock should already include that write's own
    // instruction. This class only records what it is given.
    void RecordWrite(std::uint64_t cyclesInBlock, std::uint8_t rawByte) { events_.push_back({cyclesInBlock, rawByte}); }

    // Fills `out` (resized to tickCount) by replaying every RecordWrite()
    // call since the last BeginBlock() at its correct tick position
    // (converted from the driving CPU's own cycle domain via
    // ticksPerCycle), holding the previous block's final value for any
    // ticks before this block's first recorded write, and updates the
    // internal hold value carried into the next block's Reconstruct() call.
    void Reconstruct(std::vector<float>& out, std::size_t tickCount, double ticksPerCycle);

private:
    struct WriteEvent {
        std::uint64_t cyclesInBlock;
        std::uint8_t rawByte;
    };

    float gain_ = 1.0f;
    float holdBeforeBlock_ = 0.0f;
    std::vector<WriteEvent> events_;
};
