// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "chipestra_abi.h"

#include <cstdint>
#include <vector>

// Scheduler - a generic "what fires next, and when" mechanism over
// EmuTime. It knows nothing about devices or how to run them; it only
// manages event registration and retrieval of the next pending event,
// deterministically: given the same sequence of ScheduleAt calls,
// PopNextEvent always returns events in the same order, with ties (equal
// EmuTime) broken by registration order (FIFO) rather than any
// unspecified/hash-based order.
class Scheduler {
public:
    using EventId = std::uint64_t;

    // Registers an event to fire at absolute time `when`. Returns an id that
    // can be used to cancel it later; ids are never reused within one
    // Scheduler's lifetime.
    EventId ScheduleAt(EmuTime when);

    // Cancels a scheduled event. No-op if `id` is unknown or has
    // already been popped.
    void Cancel(EventId id);

    bool HasPendingEvent() const;

    // The time of the earliest pending event. Precondition: HasPendingEvent().
    EmuTime NextEventTime() const;

    // Removes and returns the id of the earliest pending event (the one
    // NextEventTime() reports). Precondition: HasPendingEvent().
    EventId PopNextEvent();

    // Exposed only so scheduler.cpp's ordering comparator can name this
    // type; not part of the class's logical public interface.
    struct Entry {
        EmuTime when;
        std::uint64_t sequence;  // registration order; breaks ties at equal `when`
        EventId id;
    };

private:
    std::vector<Entry> entries_;
    EventId next_id_ = 1;
    std::uint64_t next_sequence_ = 0;
};
