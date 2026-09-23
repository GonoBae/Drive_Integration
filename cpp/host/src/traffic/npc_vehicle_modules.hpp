#pragma once

#include "physics/vehicle_parameters.hpp"
#include "traffic/npc_lane_follower.hpp"

namespace simcore_host {

// Reduced NPC longitudinal dynamics. It shares parts with Ego, not Ego's
// four-contact suspension/slip solver. Suspension selections remain metadata.
class NpcVehicleModules final : public NpcLongitudinalDynamics {
public:
    explicit NpcVehicleModules(VehicleParameters parameters, double mass_kg);
    [[nodiscard]] static bool selected(const VehicleParameters& parameters);
    void set_road_context(GroundSurfaceMaterialId material, double friction_multiplier,
        double grade, double curvature_per_m);
    void set_direction(int direction);
    [[nodiscard]] double braking_limit_mps2(double requested) const override;
    [[nodiscard]] double corner_speed_limit_mps() const;
    double speed_after_step(double current_speed, double desired_speed,
        double acceleration_limit, double braking_limit, double dt_seconds) override;
    void idle(double dt_seconds);
    [[nodiscard]] const std::optional<PowertrainState>& powertrain() const { return powertrain_; }
    [[nodiscard]] double fuel_percent() const;
    [[nodiscard]] int direction() const { return direction_; }

private:
    [[nodiscard]] double axle_grip_force(std::size_t axle, double speed) const;
    [[nodiscard]] double resistance_force(double speed) const;
    VehicleParameters parameters_;
    double mass_kg_;
    bool tire_modules_;
    std::optional<PowertrainState> powertrain_;
    double surface_scale_ = 1.0;
    double grade_ = 0.0;
    double curvature_per_m_ = 0.0;
    int direction_ = 1;
};

} // namespace simcore_host
