#include "coordinates/body_frame_adapter.hpp"

#include <iostream>

namespace {

void test_left_positive_steering_crosses_the_solver_boundary_once()
{
    constexpr float canonical_left = 0.4f;
    constexpr float solver_right =
        simcore_host::BodyFrameAdapter::canonical_steering_to_solver(canonical_left);

    static_assert(solver_right == -0.4f);
    static_assert(
        simcore_host::BodyFrameAdapter::solver_steering_to_canonical(solver_right)
        == canonical_left);
}

void test_solver_lateral_and_yaw_values_publish_as_flu()
{
    constexpr float solver_right_speed = 2.f;
    constexpr float solver_clockwise_yaw = 0.3f;

    static_assert(
        simcore_host::BodyFrameAdapter::solver_lateral_to_canonical(solver_right_speed)
        == -2.f);
    static_assert(
        simcore_host::BodyFrameAdapter::solver_heading_rate_to_canonical_yaw_rate(
            solver_clockwise_yaw)
        == -0.3f);
    static_assert(
        simcore_host::BodyFrameAdapter::canonical_yaw_rate_to_heading_rate(-0.3f)
        == solver_clockwise_yaw);
}

} // namespace

int main()
{
    test_left_positive_steering_crosses_the_solver_boundary_once();
    test_solver_lateral_and_yaw_values_publish_as_flu();
    std::cout << "body_frame_adapter_tests: all tests passed\n";
    return 0;
}
