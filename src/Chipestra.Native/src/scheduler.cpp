// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "scheduler.h"

#include <algorithm>

namespace {

// Orders entries so the earliest one sorts first: by `when`, then by
// `sequence` (registration order) to break ties deterministically.
bool IsEarlier(const Scheduler::Entry& a, const Scheduler::Entry& b) {
    if (a.when != b.when) {
        return a.when < b.when;
    }
    return a.sequence < b.sequence;
}

}  // namespace

Scheduler::EventId Scheduler::ScheduleAt(EmuTime when) {
    EventId id = next_id_++;
    entries_.push_back(Entry{when, next_sequence_++, id});
    return id;
}

void Scheduler::Cancel(EventId id) {
    auto it = std::find_if(entries_.begin(), entries_.end(),
                            [id](const Entry& e) { return e.id == id; });
    if (it != entries_.end()) {
        entries_.erase(it);
    }
}

bool Scheduler::HasPendingEvent() const {
    return !entries_.empty();
}

EmuTime Scheduler::NextEventTime() const {
    return std::min_element(entries_.begin(), entries_.end(), IsEarlier)->when;
}

Scheduler::EventId Scheduler::PopNextEvent() {
    auto it = std::min_element(entries_.begin(), entries_.end(), IsEarlier);
    EventId id = it->id;
    entries_.erase(it);
    return id;
}
