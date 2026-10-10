// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "bounded_frame_queue.h"

#include <algorithm>

BoundedFrameQueue::BoundedFrameQueue(std::size_t capacityFrames) : buffer_(capacityFrames) {}

std::size_t BoundedFrameQueue::Push(const StereoFrame* frames, std::size_t count) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::size_t capacity = buffer_.size();
    std::size_t free_space = capacity - count_;
    std::size_t to_push = std::min(count, free_space);

    std::size_t write_index = (head_ + count_) % capacity;
    for (std::size_t i = 0; i < to_push; ++i) {
        buffer_[(write_index + i) % capacity] = frames[i];
    }
    count_ += to_push;
    return to_push;
}

std::size_t BoundedFrameQueue::Pop(StereoFrame* out, std::size_t maxCount) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::size_t capacity = buffer_.size();
    std::size_t to_pop = std::min(maxCount, count_);

    for (std::size_t i = 0; i < to_pop; ++i) {
        out[i] = buffer_[(head_ + i) % capacity];
    }
    head_ = (head_ + to_pop) % capacity;
    count_ -= to_pop;
    return to_pop;
}

std::size_t BoundedFrameQueue::Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_;
}

std::size_t BoundedFrameQueue::Capacity() const {
    // buffer_.size() never changes after construction; no lock needed.
    return buffer_.size();
}

void BoundedFrameQueue::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    head_ = 0;
    count_ = 0;
}
