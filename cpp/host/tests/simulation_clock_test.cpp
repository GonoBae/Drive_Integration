#include "simulation_clock.hpp"

#include <chrono>
#include <limits>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_fixed_step_and_overrun_tracking()
{
    using namespace std::chrono_literals;
    using Clock = SimulationClock::Clock;
    const auto start = Clock::time_point{};
    SimulationClock clock(60.0, start);

    const auto first_deadline = clock.next_deadline();
    clock.advance(first_deadline);
    require(clock.tick_index() == 1 && clock.simulation_time_ns() != 0,
            "clock must advance exactly one fixed tick");
    const auto second_deadline = clock.next_deadline();
    require(second_deadline > first_deadline,
            "deadline must advance monotonically");
    clock.advance(second_deadline + 20ms);
    require(clock.overrun_count() == 1,
            "late completion must record an overrun");
    require(clock.next_deadline() > second_deadline + 20ms,
            "late completion must skip to a future deadline");
}

void test_large_overrun_does_not_create_catch_up_deadlines()
{
    using namespace std::chrono_literals;
    using Clock = SimulationClock::Clock;
    const auto start = Clock::time_point{};
    SimulationClock clock(60.0, start);

    const auto completed_at = start + 1s;
    clock.advance(completed_at);

    require(clock.tick_index() == 1,
            "one callback must advance one simulation tick");
    require(clock.next_deadline() > completed_at,
            "the next callback must never be scheduled in the past");
    require(clock.overrun_count() > 1,
            "all skipped wall-clock deadlines must be counted");
}

void test_reset_elapsed_preserves_the_fixed_step_deadline()
{
    using namespace std::chrono_literals;
    using Clock = SimulationClock::Clock;
    const auto start = Clock::time_point{};
    SimulationClock clock(60.0, start);

    clock.advance(start + 1s);
    require(clock.tick_index() == 1 && clock.overrun_count() > 0,
            "test setup must advance simulation time and record overruns");
    const auto scheduled_deadline = clock.next_deadline();

    clock.reset_elapsed();

    require(clock.tick_index() == 0 && clock.simulation_time_ns() == 0,
            "PIE reset must restart authoritative simulation time at zero");
    require(clock.overrun_count() == 0,
            "PIE reset must clear the prior run's timing diagnostics");
    require(clock.next_deadline() == scheduled_deadline,
            "elapsed-time reset must not disturb the live timer cadence");

    clock.advance(scheduled_deadline);
    require(clock.tick_index() == 1 && clock.simulation_time_ns() != 0,
            "the first post-reset callback must advance exactly one tick");
}

void test_invalid_frequency_is_rejected()
{
    using Clock = SimulationClock::Clock;
    const auto start = Clock::time_point{};

    for (const double frequency : {0.0, -1.0,
             std::numeric_limits<double>::infinity()}) {
        bool threw = false;
        try {
            SimulationClock clock(frequency, start);
            (void)clock;
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        require(threw, "invalid frequency must throw invalid_argument");
    }
}

} // namespace

int main()
{
    test_fixed_step_and_overrun_tracking();
    test_large_overrun_does_not_create_catch_up_deadlines();
    test_reset_elapsed_preserves_the_fixed_step_deadline();
    test_invalid_frequency_is_rejected();

    std::cout << "simulation_clock_tests: all tests passed\n";
    return 0;
}
