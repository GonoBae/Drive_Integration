#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

class SimulationClock {
public:
    using Clock = std::chrono::steady_clock;

    explicit SimulationClock(double frequency_hz, Clock::time_point start = Clock::now())
        : period_(make_period(frequency_hz))
        , next_deadline_(start + period_)
    {}

    Clock::time_point next_deadline() const { return next_deadline_; }
    std::uint64_t tick_index() const { return tick_index_; }
    std::uint32_t overrun_count() const { return overrun_count_; }

    // Reset simulation-relative time without moving the already-armed wall
    // clock deadline. This keeps the fixed-step timer monotonic and avoids a
    // cancel/reschedule race when a reset arrives between ticks.
    void reset_elapsed()
    {
        tick_index_ = 0;
        overrun_count_ = 0;
    }

    void advance(Clock::time_point completed_at)
    {
        ++tick_index_;
        next_deadline_ += period_;
        if (completed_at > next_deadline_) {
            // Skip missed wall-clock deadlines instead of issuing a burst of
            // immediate callbacks. Simulation time still advances by exactly
            // one fixed step, so overload slows the simulation rather than
            // creating a catch-up spiral.
            const auto skipped_periods =
                (completed_at - next_deadline_) / period_ + 1;
            next_deadline_ += period_ * skipped_periods;

            const auto remaining = std::numeric_limits<std::uint32_t>::max()
                                 - overrun_count_;
            const auto skipped = static_cast<std::uint64_t>(skipped_periods);
            overrun_count_ += static_cast<std::uint32_t>(
                std::min<std::uint64_t>(skipped, remaining));
        }
    }

    std::uint64_t simulation_time_ns() const
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(period_).count())
            * tick_index_;
    }

private:
    static Clock::duration make_period(double frequency_hz)
    {
        if (!std::isfinite(frequency_hz) || frequency_hz <= 0.0) {
            throw std::invalid_argument("Simulation frequency must be finite and positive");
        }

        const auto period = std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(1.0 / frequency_hz));
        if (period <= Clock::duration::zero()) {
            throw std::invalid_argument("Simulation frequency exceeds clock resolution");
        }
        return period;
    }

    Clock::duration period_;
    Clock::time_point next_deadline_;
    std::uint64_t tick_index_ = 0;
    std::uint32_t overrun_count_ = 0;
};
