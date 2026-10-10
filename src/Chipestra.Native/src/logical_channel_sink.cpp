// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Implements LogicalChannelBuffer/LogicalChannelSink, declared in chipestra_abi.h
// alongside IMachine::Render's signature (see that header for why).

#include "chipestra_abi.h"

#include <stdexcept>

void LogicalChannelBuffer::Append(const float* samples, std::size_t count) {
    samples_.insert(samples_.end(), samples, samples + count);
}

void LogicalChannelBuffer::Clear() {
    samples_.clear();
}

std::size_t LogicalChannelBuffer::Size() const {
    return samples_.size();
}

const float* LogicalChannelBuffer::Data() const {
    return samples_.data();
}

float LogicalChannelBuffer::operator[](std::size_t index) const {
    return samples_[index];
}

void LogicalChannelSink::RegisterChannel(ChannelId channel) {
    auto [it, inserted] = buffers_.try_emplace(channel);
    if (!inserted) {
        throw std::logic_error("LogicalChannelSink::RegisterChannel: channel already registered");
    }
}

bool LogicalChannelSink::HasChannel(ChannelId channel) const {
    return buffers_.find(channel) != buffers_.end();
}

void LogicalChannelSink::WriteSamples(ChannelId channel, const float* samples, std::size_t count) {
    auto it = buffers_.find(channel);
    if (it == buffers_.end()) {
        throw std::out_of_range("LogicalChannelSink::WriteSamples: channel was never registered");
    }
    it->second.Append(samples, count);
}

const LogicalChannelBuffer& LogicalChannelSink::ChannelBuffer(ChannelId channel) const {
    auto it = buffers_.find(channel);
    if (it == buffers_.end()) {
        throw std::out_of_range("LogicalChannelSink::ChannelBuffer: channel was never registered");
    }
    return it->second;
}

void LogicalChannelSink::ClearAll() {
    for (auto& [id, buffer] : buffers_) {
        buffer.Clear();
    }
}
