// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

// AddressSpaceEndpoint - anything an AddressSpace can map an address
// range onto: a MemoryRegion or a device's own memory-mapped registers.
// AddressSpace only talks to this interface, never to concrete types.
class AddressSpaceEndpoint {
public:
    virtual ~AddressSpaceEndpoint() = default;

    // `offset` is relative to the start of whatever range this endpoint was
    // mapped to, not an absolute AddressSpace address.
    virtual std::uint8_t Read8(std::uint64_t offset) const = 0;
    virtual void Write8(std::uint64_t offset, std::uint8_t value) = 0;
};

// AddressSpace - maps non-overlapping [start, start+size) address
// ranges onto AddressSpaceEndpoint instances. Mappings are non-owning
// whoever constructs the AddressSpace (a Machine, via its
// DeviceManager) owns the endpoints' lifetime and must outlive this
// AddressSpace.
//
// Unmapped-address behavior is never hardcoded here: the two handlers passed
// at construction decide what happens (open bus, a fixed value, throwing, a
// log-and-ignore policy, ...), since different boards want different
// behavior and the runtime must not special-case any of them.
class AddressSpace {
public:
    using UnmappedReadHandler = std::function<std::uint8_t(std::uint64_t address)>;
    using UnmappedWriteHandler = std::function<void(std::uint64_t address, std::uint8_t value)>;

    AddressSpace(UnmappedReadHandler on_unmapped_read, UnmappedWriteHandler on_unmapped_write);

    // Maps [start, start + size) to `endpoint`, with offset 0 into
    // `endpoint` corresponding to address `start`.
    //
    // Throws std::invalid_argument if size == 0 or endpoint == nullptr.
    // Throws std::logic_error if the range overlaps any range already
    // mapped.
    void Map(std::uint64_t start, std::uint64_t size, AddressSpaceEndpoint* endpoint);

    // Dispatches to the mapped endpoint's Read8/Write8 at the appropriate
    // local offset, or to the unmapped-access handler if `address` falls
    // outside every mapped range.
    std::uint8_t Read8(std::uint64_t address) const;
    void Write8(std::uint64_t address, std::uint8_t value);

private:
    struct Mapping {
        std::uint64_t start;
        std::uint64_t size;
        AddressSpaceEndpoint* endpoint;
    };

    const Mapping* FindMapping(std::uint64_t address) const;

    std::vector<Mapping> mappings_;  // kept sorted by `start`
    UnmappedReadHandler on_unmapped_read_;
    UnmappedWriteHandler on_unmapped_write_;
};
