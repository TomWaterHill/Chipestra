// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "address_space.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

AddressSpace::AddressSpace(UnmappedReadHandler on_unmapped_read, UnmappedWriteHandler on_unmapped_write)
    : on_unmapped_read_(std::move(on_unmapped_read)), on_unmapped_write_(std::move(on_unmapped_write)) {}

void AddressSpace::Map(std::uint64_t start, std::uint64_t size, AddressSpaceEndpoint* endpoint) {
    if (size == 0 || endpoint == nullptr) {
        throw std::invalid_argument("AddressSpace::Map: size must be non-zero and endpoint non-null");
    }

    std::uint64_t end = start + size;  // exclusive
    for (const Mapping& existing : mappings_) {
        std::uint64_t existing_end = existing.start + existing.size;
        bool overlaps = start < existing_end && existing.start < end;
        if (overlaps) {
            throw std::logic_error("AddressSpace::Map: range overlaps an already-mapped range");
        }
    }

    mappings_.push_back(Mapping{start, size, endpoint});
    std::sort(mappings_.begin(), mappings_.end(),
              [](const Mapping& a, const Mapping& b) { return a.start < b.start; });
}

const AddressSpace::Mapping* AddressSpace::FindMapping(std::uint64_t address) const {
    for (const Mapping& mapping : mappings_) {
        if (address >= mapping.start && address < mapping.start + mapping.size) {
            return &mapping;
        }
    }
    return nullptr;
}

std::uint8_t AddressSpace::Read8(std::uint64_t address) const {
    const Mapping* mapping = FindMapping(address);
    if (mapping == nullptr) {
        return on_unmapped_read_(address);
    }
    return mapping->endpoint->Read8(address - mapping->start);
}

void AddressSpace::Write8(std::uint64_t address, std::uint8_t value) {
    const Mapping* mapping = FindMapping(address);
    if (mapping == nullptr) {
        on_unmapped_write_(address, value);
        return;
    }
    mapping->endpoint->Write8(address - mapping->start, value);
}
