#include "simulation_clock.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>

int main()
{
    using namespace std::chrono_literals;
    using Clock = SimulationClock::Clock;
    const auto start = Clock::time_point{};
    SimulationClock clock(60.0, start);

    const auto first_deadline = clock.next_deadline();
    clock.advance(first_deadline);
    if (clock.tick_index() != 1 || clock.simulation_time_ns() == 0) {
        throw std::runtime_error("clock must advance exactly one fixed tick");
    }
    const auto second_deadline = clock.next_deadline();
    if (second_deadline <= first_deadline) {
        throw std::runtime_error("deadline must advance monotonically");
    }
    clock.advance(second_deadline + 20ms);
    if (clock.overrun_count() != 1) {
        throw std::runtime_error("late completion must record an overrun");
    }

    std::cout << "simulation_clock_tests: all tests passed\n";
    return 0;
}
