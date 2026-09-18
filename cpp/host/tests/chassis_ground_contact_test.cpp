#include "physics/chassis_ground_contact.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

simcore_host::ChassisContactBox sedan_box()
{
    return {
        0.335,
        2.15,
        0.94,
        0.625,
        1500.0,
        700.0,
        2200.0,
        2850.0};
}

void test_upright_floor_contact_preserves_clearance()
{
    simcore_host::FlatGroundQuery ground;
    simcore_host::ChassisGroundPose pose;
    pose.up_m = -0.10;
    pose.root_up_velocity_mps = -3.0;
    const auto result = simcore_host::resolve_chassis_ground_contact(
        pose, sedan_box(), ground);
    require(result.corrected, "upright penetrating shell must be corrected");
    require(result.maximum_penetration_m > 0.38,
            "upright fixture must exercise a material penetration");
    require(result.pose.up_m >= 0.29 - 1e-5,
            "upright chassis bottom must remain above flat ground");
    require(result.pose.root_up_velocity_mps >= -1e-6,
            "floor impulse must remove downward chassis velocity");
}

void test_roof_contact_blocks_upside_down_penetration()
{
    simcore_host::FlatGroundQuery ground;
    simcore_host::ChassisGroundPose pose;
    pose.up_m = 0.40;
    pose.roll_rad = std::numbers::pi_v<double>;
    pose.root_up_velocity_mps = -5.0;
    const auto result = simcore_host::resolve_chassis_ground_contact(
        pose, sedan_box(), ground);
    require(result.corrected, "upside-down roof penetration must be corrected");
    require(result.pose.up_m >= 0.96 - 1e-5,
            "roof must support an upside-down chassis above the ground");
    require(result.contact_count >= 4,
            "upside-down body must contact through its roof shell");
    require(result.pose.root_up_velocity_mps >= -1e-6,
            "roof impact must remove downward chassis velocity");
}

void test_side_contact_blocks_ninety_degree_roll()
{
    simcore_host::FlatGroundQuery ground;
    simcore_host::ChassisGroundPose pose;
    pose.up_m = 0.30;
    pose.roll_rad = std::numbers::pi_v<double> * 0.5;
    pose.roll_rate_rad_s = 2.0;
    const auto result = simcore_host::resolve_chassis_ground_contact(
        pose, sedan_box(), ground);
    require(result.corrected, "side-shell penetration must be corrected");
    require(result.pose.up_m >= 0.94 - 1e-4,
            "side shell must support a ninety-degree rolled chassis");
    require(std::isfinite(result.pose.roll_rate_rad_s),
            "side impact angular response must stay finite");
}

void test_clear_shell_is_unchanged()
{
    simcore_host::FlatGroundQuery ground;
    simcore_host::ChassisGroundPose pose;
    pose.up_m = 3.0;
    pose.pitch_rad = 0.2;
    pose.roll_rad = -0.3;
    pose.root_up_velocity_mps = 0.4;
    const auto result = simcore_host::resolve_chassis_ground_contact(
        pose, sedan_box(), ground);
    require(!result.corrected && result.contact_count == 0,
            "clear body shell must not create synthetic ground contact");
    require(std::abs(result.pose.up_m - pose.up_m) < 1e-12
            && std::abs(result.pose.pitch_rad - pose.pitch_rad) < 1e-12
            && std::abs(result.pose.roll_rad - pose.roll_rad) < 1e-12,
            "clear body shell pose must remain unchanged");
}

class TruckEndLedgeGroundQuery final : public simcore_host::GroundQuery {
public:
    TruckEndLedgeGroundQuery(double heading_rad, bool behind)
        : heading_rad_(heading_rad), behind_(behind) {}

    std::optional<simcore_host::GroundHit> query_down(
        const simcore_host::GroundQueryRequest& request) const override
    {
        const double forward = std::sin(heading_rad_) * request.origin_enu.east_m
            + std::cos(heading_rad_) * request.origin_enu.north_m;
        if (behind_ ? forward >= -3.20 : forward <= 3.00) {
            return std::nullopt;
        }
        const double distance = request.origin_enu.up_m - 0.55;
        if (distance < 0.0 || distance > request.max_distance_m) {
            return std::nullopt;
        }
        return simcore_host::GroundHit{
            {request.origin_enu.east_m, request.origin_enu.north_m, 0.55},
            {0.0, 0.0, 1.0}, distance};
    }

private:
    double heading_rad_;
    bool behind_;
};

void test_truck_shell_contacts_its_authored_offset_rear_edge()
{
    const simcore_host::ChassisContactBox truck{
        0.705, 3.10, 1.05, 0.965, 6200.0, 4500.0, 12000.0, 14500.0, -0.15};
    for (const double heading : {0.0, std::numbers::pi_v<double> * 0.5}) {
        simcore_host::ChassisGroundPose pose;
        pose.up_m = 0.75;
        pose.heading_rad = heading;
        const TruckEndLedgeGroundQuery rear_ledge(heading, true);
        const auto rear = simcore_host::resolve_chassis_ground_contact(
            pose, truck, rear_ledge);
        require(rear.corrected && std::abs(rear.maximum_penetration_m - 0.06) < 1e-6,
            "truck rear at -3.25 m must meet the ledge with its 0.49 m underbody");

        const TruckEndLedgeGroundQuery front_ledge(heading, false);
        const auto front = simcore_host::resolve_chassis_ground_contact(
            pose, truck, front_ledge);
        require(!front.corrected && front.contact_count == 0,
            "terrain beyond the authored 2.95 m truck nose must not contact a phantom shell");
    }
    auto invalid = truck;
    invalid.center_forward_offset_m = std::numeric_limits<double>::quiet_NaN();
    bool rejected = false;
    try {
        (void)simcore_host::resolve_chassis_ground_contact(
            {}, invalid, simcore_host::FlatGroundQuery{});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "nonfinite shell offsets must fail before querying terrain");
}

} // namespace

int main()
{
    try {
        test_upright_floor_contact_preserves_clearance();
        test_roof_contact_blocks_upside_down_penetration();
        test_side_contact_blocks_ninety_degree_roll();
        test_clear_shell_is_unchanged();
        test_truck_shell_contacts_its_authored_offset_rear_edge();
        std::cout << "chassis_ground_contact_tests: all tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "chassis_ground_contact_tests: " << error.what() << '\n';
        return 1;
    }
}
