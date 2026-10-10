// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "address_space.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// MemoryRegion - a fixed-size byte buffer that can be mapped into an
// AddressSpace. Zero-initialized at construction; ROM contents are loaded
// via RawData(), not Write8, since Write8 enforces read_only.
class MemoryRegion final : public AddressSpaceEndpoint {
public:
    explicit MemoryRegion(std::size_t size, bool read_only = false);

    std::uint8_t Read8(std::uint64_t offset) const override;
    void Write8(std::uint64_t offset, std::uint8_t value) override;

    std::size_t Size() const;
    bool IsReadOnly() const;

    // Direct access for setup code (e.g. loading initial contents); bypasses
    // the read-only check since it isn't going through the emulated bus.
    std::uint8_t* RawData();
    const std::uint8_t* RawData() const;

private:
    std::vector<std::uint8_t> buffer_;
    bool read_only_;
};
