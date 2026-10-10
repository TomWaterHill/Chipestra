// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "playback_pump.h"

#include "mixer.h"

#include <chrono>
#include <cstdint>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#endif

namespace {

using SteadyClock = std::chrono::steady_clock;

// One Mixer block's worth of wall-clock time. The Machine renders exactly
// kBlockSizeFrames canonical (192kHz) frames per iteration, so the loop must
// call render_block_() at this cadence for the downstream endpoint to be fed
// in real time. 256 / 192000 s = 1333.33 us.
constexpr auto kBlockPeriod =
    std::chrono::nanoseconds((static_cast<std::int64_t>(Mixer::kBlockSizeFrames) * 1'000'000'000LL) /
                             static_cast<std::int64_t>(Mixer::kCanonicalSampleRateHz));

// If the loop falls this far behind its schedule (a debugger break, a long GC
// pause on a managed caller, the machine hiccuping) we drop the backlog rather
// than fire a catch-up burst that would overflow the endpoint queue. A short
// silence is less bad than a flood of stale audio.
constexpr auto kMaxCatchUp = kBlockPeriod * 4;

// High-resolution wait. std::this_thread::sleep_for(1.3ms) is rounded up to
// the Windows timer tick (~15.6ms), which would starve the audio endpoint. A
// CREATE_WAITABLE_TIMER_HIGH_RESOLUTION timer (Win10 1803+) waits accurately
// with no process-global side effect; timeBeginPeriod(1) is the fallback for
// older Windows, and being process-global it is scoped to the pump's lifetime.
class BlockWaiter {
public:
    BlockWaiter() {
#if defined(_WIN32)
        timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (timer_ == nullptr) {
            timeBeginPeriod(1);
            raised_timer_period_ = true;
        }
#endif
    }

    ~BlockWaiter() {
#if defined(_WIN32)
        if (timer_ != nullptr) {
            CloseHandle(timer_);
        }
        if (raised_timer_period_) {
            timeEndPeriod(1);
        }
#endif
    }

    BlockWaiter(const BlockWaiter&) = delete;
    BlockWaiter& operator=(const BlockWaiter&) = delete;

    // Blocks until `deadline` (steady_clock). Returns immediately if already
    // past it.
    void WaitUntil(SteadyClock::time_point deadline) {
        const auto now = SteadyClock::now();
        if (now >= deadline) {
            return;
        }
        const auto remaining = deadline - now;
#if defined(_WIN32)
        if (timer_ != nullptr) {
            LARGE_INTEGER due;
            // negative == relative, in 100 ns units
            due.QuadPart = -(std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count() / 100);
            if (due.QuadPart >= 0) {
                due.QuadPart = -1;
            }
            if (SetWaitableTimerEx(timer_, &due, 0, nullptr, nullptr, nullptr, 0)) {
                WaitForSingleObject(timer_, INFINITE);
                return;
            }
        }
#endif
        std::this_thread::sleep_for(remaining);
    }

private:
#if defined(_WIN32)
    HANDLE timer_ = nullptr;
    bool raised_timer_period_ = false;
#endif
};

}  // namespace

PlaybackPump::PlaybackPump(RenderBlockFn renderBlock) : render_block_(std::move(renderBlock)) {}

PlaybackPump::~PlaybackPump() {
    Stop();
}

void PlaybackPump::Start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) {
        return;
    }
    running_ = true;
    paused_ = false;
    stop_requested_ = false;
    thread_ = std::thread(&PlaybackPump::Run, this);
}

void PlaybackPump::Pause() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_) {
        return;
    }
    paused_ = true;
    cv_.notify_all();
}

void PlaybackPump::Resume() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ || !paused_) {
        return;
    }
    paused_ = false;
    cv_.notify_all();
}

void PlaybackPump::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) {
            return;
        }
        stop_requested_ = true;
        paused_ = false;  // wake the loop if it's currently paused
        cv_.notify_all();
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
}

bool PlaybackPump::IsRunning() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

bool PlaybackPump::IsPaused() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return paused_;
}

void PlaybackPump::Run() {
    BlockWaiter waiter;

    // Monotonic schedule: each iteration is due kBlockPeriod after the last.
    // When we wake late, the deficit is absorbed by not waiting on the next
    // iteration(s) rather than by drifting, so the long-run cadence is exactly
    // real time. `have_schedule` is false until the first render and is reset
    // after every pause so a resume does not fire a catch-up burst.
    SteadyClock::time_point next_deadline{};
    bool have_schedule = false;

    while (true) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (paused_ || stop_requested_) {
                have_schedule = false;
                cv_.wait(lock, [this] { return !paused_ || stop_requested_; });
            }
            if (stop_requested_) {
                return;
            }
        }

        try {
            render_block_();
        } catch (...) {
            // A single bad block must not take down the whole process (an
            // uncaught exception on this thread would call std::terminate).
            // The next iteration simply tries again.
        }

        const auto now = SteadyClock::now();
        if (!have_schedule) {
            next_deadline = now + kBlockPeriod;
            have_schedule = true;
        } else {
            next_deadline += kBlockPeriod;
            if (now - next_deadline > kMaxCatchUp) {
                next_deadline = now + kBlockPeriod;  // drop the backlog
            }
        }

        if (SteadyClock::now() < next_deadline) {
            waiter.WaitUntil(next_deadline);
        }
        // else: behind schedule - loop immediately to catch up (bounded above)
    }
}
