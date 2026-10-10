// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "memory_region.h"

#include <stdexcept>

MemoryRegion::MemoryRegion(std::size_t size, bool read_only)
    : buffer_(size, 0), read_only_(read_only) {}

std::uint8_t MemoryRegion::Read8(std::uint64_t offset) const {
    if (offset >= buffer_.size()) {
        throw std::out_of_range("MemoryRegion::Read8: offset out of range");
    }
    return buffer_[offset];
}

void MemoryRegion::Write8(std::uint64_t offset, std::uint8_t value) {
    if (read_only_) {
        throw std::logic_error("MemoryRegion::Write8: region is read-only");
    }
    if (offset >= buffer_.size()) {
        throw std::out_of_range("MemoryRegion::Write8: offset out of range");
    }
    buffer_[offset] = value;
}

std::size_t MemoryRegion::Size() const {
    return buffer_.size();
}

bool MemoryRegion::IsReadOnly() const {
    return read_only_;
}

std::uint8_t* MemoryRegion::RawData() {
    return buffer_.data();
}

const std::uint8_t* MemoryRegion::RawData() const {
    return buffer_.data();
}
