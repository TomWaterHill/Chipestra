// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;

namespace Chipestra.App.ViewModels
{
    /// <summary>
    /// Peak-hold-then-decay tracking for one meter bar: remembers the loudest peak seen, holds
    /// it steady for <see cref="HoldSeconds"/>, then decays it at <see cref="DecayDbPerSecond"/>
    /// until a new, louder peak arrives or it catches down to the live signal. Pure state machine
    /// driven by the elapsed time the caller's poll tick measured; it has no timer of its own.
    /// </summary>
    public sealed class PeakHoldTracker
    {
        private const double HoldSeconds = 1.5;
        private const double DecayDbPerSecond = 24.0;

        private double _heldLinear;
        private double _holdRemainingSeconds;

        /// <summary>The current held value, as a linear amplitude.</summary>
        public double Value => _heldLinear;

        /// <summary>
        /// Advances the hold/decay state by <paramref name="elapsedSeconds"/>
        /// given this tick's live peak (linear amplitude, sign ignored).
        /// </summary>
        public void Update(float currentPeak, double elapsedSeconds)
        {
            double magnitude = Math.Abs(currentPeak);
            if (magnitude >= _heldLinear)
            {
                _heldLinear = magnitude;
                _holdRemainingSeconds = HoldSeconds;
                return;
            }

            // A single call's elapsed time can outlast the remaining hold time - consume the hold
            // first, then spend the leftover on decay in the same call.
            double remainingElapsed = Math.Max(0.0, elapsedSeconds);
            if (_holdRemainingSeconds > 0.0)
            {
                double consumed = Math.Min(_holdRemainingSeconds, remainingElapsed);
                _holdRemainingSeconds -= consumed;
                remainingElapsed -= consumed;
                if (remainingElapsed <= 0.0)
                {
                    return;
                }
            }

            double decayedDb = MeterScale.LinearToDb((float)_heldLinear) - DecayDbPerSecond * remainingElapsed;
            _heldLinear = Math.Max(magnitude, MeterScale.DbToLinear(decayedDb));
        }

        /// <summary>Drops back to silence with no hold pending - used when a Channel Strip is (re)seeded from Native.</summary>
        public void Reset()
        {
            _heldLinear = 0.0;
            _holdRemainingSeconds = 0.0;
        }
    }
}
