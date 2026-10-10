// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

// PlaybackPump - the one thread that touches a Machine's mutable state and an
// IAudioOutput once playback has started. Runs a loop of "render one Mixer
// block, submit it" via the caller-supplied `RenderBlockFn`, so the pump itself
// knows nothing about Machine/Mixer/IAudioOutput and only handles
// running/pausing/stopping the loop.
//
// This is the only extra thread added for audio: WasapiSharedOutput drains its
// queue synchronously inside SubmitStereo(), called from this same thread.
class PlaybackPump {
public:
    using RenderBlockFn = std::function<void()>;

    explicit PlaybackPump(RenderBlockFn renderBlock);
    ~PlaybackPump();

    PlaybackPump(const PlaybackPump&) = delete;
    PlaybackPump& operator=(const PlaybackPump&) = delete;

    // Starts the worker thread, which immediately begins looping
    // (calling renderBlock() repeatedly, pacing itself to roughly one
    // Mixer block's wall-clock duration per iteration) until Pause()/Stop().
    // No-op if already running.
    void Start();

    // Pauses the loop: the worker thread stays alive but stops calling
    // renderBlock() until Resume(). No-op if not running or already paused.
    void Pause();

    // Resumes a paused loop. No-op if not running or not paused.
    void Resume();

    // Stops the loop and joins the worker thread. Safe to call from any
    // state, including before Start() or after an earlier Stop() (no-op
    // both times).
    void Stop();

    bool IsRunning() const;
    bool IsPaused() const;

private:
    void Run();

    RenderBlockFn render_block_;
    std::thread thread_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool running_ = false;
    bool paused_ = false;
    bool stop_requested_ = false;
};
