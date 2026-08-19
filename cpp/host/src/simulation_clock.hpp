#pragma once

#include <chrono>
#include <cstdint>

class SimulationClock {
public:
    using Clock = std::chrono::steady_clock;

    explicit SimulationClock(double frequency_hz, Clock::time_point start = Clock::now())
        : period_(std::chrono::duration_cast<Clock::duration>(
              std::chrono::duration<double>(1.0 / frequency_hz)))
        , next_deadline_(start + period_)
    {}

    Clock::time_point next_deadline() const { return next_deadline_; }
    std::uint64_t tick_index() const { return tick_index_; }
    std::uint32_t overrun_count() const { return overrun_count_; }

    void advance(Clock::time_point completed_at)
    {
        ++tick_index_;
        next_deadline_ += period_;
        if (completed_at > next_deadline_) {
            ++overrun_count_;
        }
    }

    std::uint64_t simulation_time_ns() const
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(period_).count())
            * tick_index_;
    }

private:
    Clock::duration period_;
    Clock::time_point next_deadline_;
    std::uint64_t tick_index_ = 0;
    std::uint32_t overrun_count_ = 0;
};
