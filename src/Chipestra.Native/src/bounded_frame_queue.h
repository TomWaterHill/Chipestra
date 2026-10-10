// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "mixer.h"

#include <cstddef>
#include <mutex>
#include <vector>

// BoundedFrameQueue - a fixed-capacity ring buffer of StereoFrame between the
// thread that produces audio (PlaybackPump) and the path that drains it into a
// real device. Mutex-protected rather than lock-free, for correctness first.
//
// Overflow policy: Push() never blocks the producer. A full queue drops the
// frames that don't fit, and every dropped frame is reported back to the caller
// so it can be counted (QueueOverflowCount).
class BoundedFrameQueue {
public:
    explicit BoundedFrameQueue(std::size_t capacityFrames);

    // Pushes as many of `frames`/`count` as fit. Returns the number of
    // frames actually enqueued; `count - returned value` is how many were
    // dropped due to a full queue.
    std::size_t Push(const StereoFrame* frames, std::size_t count);

    // Pops up to `maxCount` frames into `out` (which must have room for at
    // least `maxCount`). Returns the number of frames actually popped.
    std::size_t Pop(StereoFrame* out, std::size_t maxCount);

    std::size_t Size() const;
    std::size_t Capacity() const;
    void Clear();

private:
    mutable std::mutex mutex_;
    std::vector<StereoFrame> buffer_;  // ring storage, buffer_.size() == capacity
    std::size_t head_ = 0;             // index of the oldest queued frame
    std::size_t count_ = 0;            // number of currently queued frames
};
