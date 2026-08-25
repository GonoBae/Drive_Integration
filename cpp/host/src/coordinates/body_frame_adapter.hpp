#pragma once

namespace simcore_host {

// The public vehicle contract uses right-handed FLU coordinates:
// X=forward, Y=left, Z=up. The current planar solver keeps a private
// right-positive lateral/steering convention. All sign changes between those
// conventions must pass through this boundary.
class BodyFrameAdapter final {
public:
    static constexpr float canonical_steering_to_solver(float left_positive)
    {
        return -left_positive;
    }

    static constexpr float solver_steering_to_canonical(float right_positive)
    {
        return -right_positive;
    }

    static constexpr float solver_lateral_to_canonical(float right_positive)
    {
        return -right_positive;
    }

    static constexpr float solver_yaw_rate_to_canonical(float clockwise_positive)
    {
        return -clockwise_positive;
    }

    static constexpr float canonical_yaw_rate_to_heading_rate(float left_positive)
    {
        return -left_positive;
    }
};

} // namespace simcore_host
