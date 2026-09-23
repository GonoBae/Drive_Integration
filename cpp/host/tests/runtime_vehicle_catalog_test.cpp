#include "vehicle_catalog/runtime_vehicle_catalog.hpp"
#include "physics/vehicle_config.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef SIMCORE_TEST_RUNTIME_VEHICLE_CATALOG_PATH
#error SIMCORE_TEST_RUNTIME_VEHICLE_CATALOG_PATH must identify the shared runtime manifest
#endif
#ifndef SIMCORE_TEST_VEHICLE_CONFIG_PATH
#error SIMCORE_TEST_VEHICLE_CONFIG_PATH must identify the sedan baseline configuration
#endif

// Frozen pre-catalog implementation: a test-only oracle for unchanged driving.
namespace legacy_profile_oracle {
using simcore_host::RuntimeVehicleClass;
void scale_suspension(VehicleParameters& result, const VehicleParameters& sedan)
{
    const float scale = result.mass_kg / sedan.mass_kg;
    result.suspension.spring_rate_n_per_m = sedan.suspension.spring_rate_n_per_m * scale;
    result.suspension.damper_rate_n_s_per_m = sedan.suspension.damper_rate_n_s_per_m * scale;
    result.suspension.max_force_n = sedan.suspension.max_force_n * scale;
}
VehicleParameters make_player_vehicle_parameters(
    const VehicleParameters& sedan,
    RuntimeVehicleClass vehicle_class)
{
    if (!valid_vehicle_parameters(sedan)) {
        throw std::invalid_argument("configured sedan profile is invalid");
    }
    if (vehicle_class == RuntimeVehicleClass::Unspecified) {
        vehicle_class = RuntimeVehicleClass::Sedan;
    }

    VehicleParameters result = sedan;
    switch (vehicle_class) {
    case RuntimeVehicleClass::Sedan:
        break;
    case RuntimeVehicleClass::Compact:
        result.mass_kg = 1050.f;
        result.wheelbase_m = 2.133f;
        result.max_steering_angle_rad = 0.6632251f; // 38 degrees
        result.max_drive_force_n = 5200.f;
        result.max_reverse_force_n = 3100.f;
        result.max_drive_power_w = 80000.f;
        result.max_service_brake_n = 14500.f;
        result.max_handbrake_force_n = 16500.f;
        result.drag_coefficient = 0.31f;
        result.frontal_area_m2 = 1.90f;
        result.tire_radius_m = 0.275f;
        result.max_forward_speed_mps = 45.f;
        result.front_track_m = 1.422f;
        result.rear_track_m = 1.422f;
        result.cg_height_m = 0.48f;
        result.yaw_inertia_kg_m2 = 1450.f;
        result.pitch_inertia_kg_m2 = 1250.f;
        result.roll_inertia_kg_m2 = 450.f;
        result.front_tire_corner_stiffness_n_rad = 45000.f;
        result.rear_tire_corner_stiffness_n_rad = 39000.f;
        result.tire_longitudinal_stiffness_n = 68000.f;
        result.wheel_inertia_kg_m2 = 1.2f;
        result.collision_body_overhang_m = 0.6835f;
        result.collision_body_side_padding_m = 0.149f;
        result.collision_body_half_height_m = 0.70f;
        result.chassis_shell_center_up_offset_m = 0.30f;
        result.chassis_shell_half_height_m = 0.56f;
        scale_suspension(result, sedan);
        break;
    case RuntimeVehicleClass::Truck:
        result.mass_kg = 6200.f;
        result.wheelbase_m = 3.85f;
        result.max_steering_angle_rad = 0.4886922f; // 28 degrees
        result.steering_rate_rad_s = 1.25f;
        result.steering_return_rate_rad_s = 1.55f;
        result.max_drive_force_n = 18000.f;
        result.max_reverse_force_n = 11000.f;
        result.max_drive_power_w = 180000.f;
        result.drive_force_rise_rate_n_per_s = 30000.f;
        result.drive_force_fall_rate_n_per_s = 50000.f;
        result.max_service_brake_n = 65000.f;
        result.max_handbrake_force_n = 72000.f;
        result.drivetrain_drag_n_per_mps = 100.f;
        result.drag_coefficient = 0.58f;
        result.frontal_area_m2 = 7.2f;
        result.tire_radius_m = 0.390f;
        result.max_forward_speed_mps = 28.f;
        result.max_reverse_speed_mps = 6.f;
        result.front_track_m = 1.90f;
        result.rear_track_m = 1.90f;
        result.cg_height_m = 0.75f;
        result.front_static_load_fraction = 0.58f;
        result.yaw_inertia_kg_m2 = 14500.f;
        result.pitch_inertia_kg_m2 = 12000.f;
        result.roll_inertia_kg_m2 = 4500.f;
        result.front_tire_corner_stiffness_n_rad = 180000.f;
        result.rear_tire_corner_stiffness_n_rad = 160000.f;
        result.tire_longitudinal_stiffness_n = 260000.f;
        result.tire_friction = 0.95f;
        result.wheel_inertia_kg_m2 = 5.5f;
        result.wheel_free_spin_damping_n_m_s = 8.0f;
        // Truck BODY authored bounds in metres: forward [-3.25, 2.95],
        // right [-1.05, 1.05], up [-0.26, 1.67], relative to the player CG.
        result.collision_body_overhang_m = 1.175f;
        result.collision_body_side_padding_m = 0.10f;
        result.collision_body_half_height_m = 0.965f;
        result.collision_body_center_forward_offset_m = -0.15f;
        result.collision_body_ground_clearance_m = 0.49f;
        result.chassis_shell_center_up_offset_m = 0.705f;
        result.chassis_shell_half_height_m = 0.965f;
        scale_suspension(result, sedan);
        break;
    case RuntimeVehicleClass::Motorcycle:
        result.single_track = true;
        result.mass_kg = 240.f;
        result.wheelbase_m = 1.81f;
        result.max_steering_angle_rad = 0.5585054f; // 32 degrees
        result.steering_rate_rad_s = 2.4f;
        result.steering_return_rate_rad_s = 2.8f;
        result.max_drive_force_n = 1800.f;
        result.max_reverse_force_n = 900.f;
        result.max_drive_power_w = 55000.f;
        result.drive_force_rise_rate_n_per_s = 8000.f;
        result.drive_force_fall_rate_n_per_s = 12000.f;
        result.max_service_brake_n = 3200.f;
        result.max_handbrake_force_n = 3600.f;
        result.drivetrain_drag_n_per_mps = 12.f;
        result.drag_coefficient = 0.55f;
        result.frontal_area_m2 = 0.70f;
        result.tire_radius_m = 0.336f;
        result.max_forward_speed_mps = 50.f;
        result.front_track_m = 0.60f;
        result.rear_track_m = 0.60f;
        result.cg_height_m = 0.42f;
        result.front_static_load_fraction = 0.82f / 1.81f;
        result.front_drive_torque_fraction = 0.f;
        result.yaw_inertia_kg_m2 = 210.f;
        result.pitch_inertia_kg_m2 = 180.f;
        result.roll_inertia_kg_m2 = 70.f;
        result.front_tire_corner_stiffness_n_rad = 12000.f;
        result.rear_tire_corner_stiffness_n_rad = 10500.f;
        result.tire_longitudinal_stiffness_n = 18000.f;
        result.tire_friction = 1.10f;
        result.wheel_inertia_kg_m2 = 0.55f;
        result.wheel_free_spin_damping_n_m_s = 1.4f;
        result.collision_body_overhang_m = 0.195f;
        result.collision_body_side_padding_m = 0.12f;
        result.collision_body_half_height_m = 0.68f;
        result.chassis_shell_center_up_offset_m = 0.20f;
        result.chassis_shell_half_height_m = 0.36f;
        scale_suspension(result, sedan);
        result.suspension.rest_length_m = result.cg_height_m - result.tire_radius_m + 0.12f;
        result.suspension.max_compression_m = 0.16f;
        break;
    case RuntimeVehicleClass::Unspecified:
        break;
    default:
        throw std::invalid_argument("unsupported player vehicle class");
    }
    if (!valid_vehicle_parameters(result)) {
        throw std::invalid_argument("selected player vehicle profile is invalid");
    }
    return result;
}
} // namespace legacy_profile_oracle

namespace {
namespace fs = std::filesystem;
using namespace simcore_host;

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

void compare(const VehicleParameters& actual, const VehicleParameters& expected, const std::string& context)
{
    require(actual.powertrain.has_value() == expected.powertrain.has_value(),
        context + ".powertrain differs from legacy inheritance");
    require(actual.front_axle_contact.tire.has_value() == expected.front_axle_contact.tire.has_value()
        && actual.rear_axle_contact.tire.has_value() == expected.rear_axle_contact.tire.has_value()
        && actual.front_axle_contact.suspension.has_value() == expected.front_axle_contact.suspension.has_value()
        && actual.rear_axle_contact.suspension.has_value() == expected.rear_axle_contact.suspension.has_value(),
        context + ".axle modules differ from legacy inheritance");
    require(actual.single_track == expected.single_track, context + ".single_track differs from legacy");
    require(actual.mass_kg == expected.mass_kg, context + ".mass_kg differs from legacy");
    require(actual.wheelbase_m == expected.wheelbase_m, context + ".wheelbase_m differs from legacy");
    require(actual.max_steering_angle_rad == expected.max_steering_angle_rad, context + ".max_steering_angle_rad differs from legacy");
    require(actual.steering_rate_rad_s == expected.steering_rate_rad_s, context + ".steering_rate_rad_s differs from legacy");
    require(actual.steering_return_rate_rad_s == expected.steering_return_rate_rad_s, context + ".steering_return_rate_rad_s differs from legacy");
    require(actual.max_drive_force_n == expected.max_drive_force_n, context + ".max_drive_force_n differs from legacy");
    require(actual.max_reverse_force_n == expected.max_reverse_force_n, context + ".max_reverse_force_n differs from legacy");
    require(actual.max_drive_power_w == expected.max_drive_power_w, context + ".max_drive_power_w differs from legacy");
    require(actual.drive_force_rise_rate_n_per_s == expected.drive_force_rise_rate_n_per_s, context + ".drive_force_rise_rate_n_per_s differs from legacy");
    require(actual.drive_force_fall_rate_n_per_s == expected.drive_force_fall_rate_n_per_s, context + ".drive_force_fall_rate_n_per_s differs from legacy");
    require(actual.max_service_brake_n == expected.max_service_brake_n, context + ".max_service_brake_n differs from legacy");
    require(actual.max_handbrake_force_n == expected.max_handbrake_force_n, context + ".max_handbrake_force_n differs from legacy");
    require(actual.rolling_resistance_coeff == expected.rolling_resistance_coeff, context + ".rolling_resistance_coeff differs from legacy");
    require(actual.drivetrain_drag_n_per_mps == expected.drivetrain_drag_n_per_mps, context + ".drivetrain_drag_n_per_mps differs from legacy");
    require(actual.drag_coefficient == expected.drag_coefficient, context + ".drag_coefficient differs from legacy");
    require(actual.frontal_area_m2 == expected.frontal_area_m2, context + ".frontal_area_m2 differs from legacy");
    require(actual.air_density_kg_m3 == expected.air_density_kg_m3, context + ".air_density_kg_m3 differs from legacy");
    require(actual.tire_radius_m == expected.tire_radius_m, context + ".tire_radius_m differs from legacy");
    require(actual.final_drive_ratio == expected.final_drive_ratio, context + ".final_drive_ratio differs from legacy");
    require(actual.drive_gear_ratio == expected.drive_gear_ratio, context + ".drive_gear_ratio differs from legacy");
    require(actual.reverse_gear_ratio == expected.reverse_gear_ratio, context + ".reverse_gear_ratio differs from legacy");
    require(actual.max_forward_speed_mps == expected.max_forward_speed_mps, context + ".max_forward_speed_mps differs from legacy");
    require(actual.max_reverse_speed_mps == expected.max_reverse_speed_mps, context + ".max_reverse_speed_mps differs from legacy");
    require(actual.idle_rpm == expected.idle_rpm, context + ".idle_rpm differs from legacy");
    require(actual.max_rpm == expected.max_rpm, context + ".max_rpm differs from legacy");
    require(actual.fuel_rate_percent_s == expected.fuel_rate_percent_s, context + ".fuel_rate_percent_s differs from legacy");
    require(actual.front_track_m == expected.front_track_m, context + ".front_track_m differs from legacy");
    require(actual.rear_track_m == expected.rear_track_m, context + ".rear_track_m differs from legacy");
    require(actual.cg_height_m == expected.cg_height_m, context + ".cg_height_m differs from legacy");
    require(actual.front_static_load_fraction == expected.front_static_load_fraction, context + ".front_static_load_fraction differs from legacy");
    require(actual.front_drive_torque_fraction == expected.front_drive_torque_fraction, context + ".front_drive_torque_fraction differs from legacy");
    require(actual.front_service_brake_fraction == expected.front_service_brake_fraction, context + ".front_service_brake_fraction differs from legacy");
    require(actual.yaw_inertia_kg_m2 == expected.yaw_inertia_kg_m2, context + ".yaw_inertia_kg_m2 differs from legacy");
    require(actual.pitch_inertia_kg_m2 == expected.pitch_inertia_kg_m2, context + ".pitch_inertia_kg_m2 differs from legacy");
    require(actual.roll_inertia_kg_m2 == expected.roll_inertia_kg_m2, context + ".roll_inertia_kg_m2 differs from legacy");
    require(actual.attitude_spring_n_m_rad == expected.attitude_spring_n_m_rad, context + ".attitude_spring_n_m_rad differs from legacy");
    require(actual.attitude_damping_n_m_s_rad == expected.attitude_damping_n_m_s_rad, context + ".attitude_damping_n_m_s_rad differs from legacy");
    require(actual.front_tire_corner_stiffness_n_rad == expected.front_tire_corner_stiffness_n_rad, context + ".front_tire_corner_stiffness_n_rad differs from legacy");
    require(actual.rear_tire_corner_stiffness_n_rad == expected.rear_tire_corner_stiffness_n_rad, context + ".rear_tire_corner_stiffness_n_rad differs from legacy");
    require(actual.tire_longitudinal_stiffness_n == expected.tire_longitudinal_stiffness_n, context + ".tire_longitudinal_stiffness_n differs from legacy");
    require(actual.tire_friction == expected.tire_friction, context + ".tire_friction differs from legacy");
    require(actual.surface_default_friction_scale == expected.surface_default_friction_scale, context + ".surface_default_friction_scale differs from legacy");
    require(actual.surface_asphalt_friction_scale == expected.surface_asphalt_friction_scale, context + ".surface_asphalt_friction_scale differs from legacy");
    require(actual.surface_low_friction_scale == expected.surface_low_friction_scale, context + ".surface_low_friction_scale differs from legacy");
    require(actual.surface_rough_friction_scale == expected.surface_rough_friction_scale, context + ".surface_rough_friction_scale differs from legacy");
    require(actual.lateral_grip_priority == expected.lateral_grip_priority, context + ".lateral_grip_priority differs from legacy");
    require(actual.traction_control_slip_target == expected.traction_control_slip_target, context + ".traction_control_slip_target differs from legacy");
    require(actual.traction_control_full_cut_slip == expected.traction_control_full_cut_slip, context + ".traction_control_full_cut_slip differs from legacy");
    require(actual.wheel_inertia_kg_m2 == expected.wheel_inertia_kg_m2, context + ".wheel_inertia_kg_m2 differs from legacy");
    require(actual.wheel_free_spin_damping_n_m_s == expected.wheel_free_spin_damping_n_m_s, context + ".wheel_free_spin_damping_n_m_s differs from legacy");
    require(actual.low_speed_slip_reference_mps == expected.low_speed_slip_reference_mps, context + ".low_speed_slip_reference_mps differs from legacy");
    require(actual.collision_body_overhang_m == expected.collision_body_overhang_m, context + ".collision_body_overhang_m differs from legacy");
    require(actual.collision_body_side_padding_m == expected.collision_body_side_padding_m, context + ".collision_body_side_padding_m differs from legacy");
    require(actual.collision_body_half_height_m == expected.collision_body_half_height_m, context + ".collision_body_half_height_m differs from legacy");
    require(actual.collision_body_center_forward_offset_m == expected.collision_body_center_forward_offset_m, context + ".collision_body_center_forward_offset_m differs from legacy");
    require(actual.collision_body_ground_clearance_m == expected.collision_body_ground_clearance_m, context + ".collision_body_ground_clearance_m differs from legacy");
    require(actual.chassis_shell_center_up_offset_m == expected.chassis_shell_center_up_offset_m, context + ".chassis_shell_center_up_offset_m differs from legacy");
    require(actual.chassis_shell_half_height_m == expected.chassis_shell_half_height_m, context + ".chassis_shell_half_height_m differs from legacy");
    require(actual.suspension.rest_length_m == expected.suspension.rest_length_m, context + ".suspension.rest_length_m differs");
    require(actual.suspension.max_compression_m == expected.suspension.max_compression_m, context + ".suspension.max_compression_m differs");
    require(actual.suspension.max_extension_m == expected.suspension.max_extension_m, context + ".suspension.max_extension_m differs");
    require(actual.suspension.spring_rate_n_per_m == expected.suspension.spring_rate_n_per_m, context + ".suspension.spring_rate differs");
    require(actual.suspension.damper_rate_n_s_per_m == expected.suspension.damper_rate_n_s_per_m, context + ".suspension.damper_rate differs");
    require(actual.suspension.max_force_n == expected.suspension.max_force_n, context + ".suspension.max_force differs");
}

std::string read(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    require(input.good(), "fixture read failed");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

class Fixture {
public:
    Fixture()
    {
        parent_ = fs::canonical(fs::temp_directory_path());
        const auto seed = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::random_device random;
        for (int attempt = 0; attempt < 32; ++attempt) {
            const auto path = parent_ / ("simcore-runtime-catalog-" + seed + "-" + std::to_string(random()) + "-" + std::to_string(attempt));
            if (fs::create_directory(path)) { root_ = path; break; }
        }
        require(!root_.empty(), "could not create unique runtime catalog fixture");
        try {
            const auto source = fs::canonical(SIMCORE_TEST_RUNTIME_VEHICLE_CATALOG_PATH).parent_path();
            for (const auto& entry : fs::recursive_directory_iterator(source)) {
                if (!entry.is_regular_file()) continue;
                const auto target = root_ / entry.path().lexically_relative(source);
                fs::create_directories(target.parent_path());
                fs::copy_file(entry.path(), target);
            }
        } catch (...) { cleanup(); throw; }
    }
    ~Fixture() { cleanup(); }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    RuntimeVehicleCatalog load() const { return load_runtime_vehicle_catalog(root_ / "catalog.json"); }
    void write(const char* relative, const std::string& bytes)
    {
        std::ofstream output(root_ / relative, std::ios::binary | std::ios::trunc);
        output << bytes;
        output.close();
        require(output.good(), "fixture write failed");
    }
    void replace(const char* relative, const std::string& before, const std::string& after)
    {
        auto bytes = read(root_ / relative);
        const auto position = bytes.find(before);
        require(position != std::string::npos, "missing fixture mutation anchor: " + before);
        bytes.replace(position, before.size(), after);
        std::ofstream output(root_ / relative, std::ios::binary | std::ios::trunc);
        output << bytes;
        output.close();
        require(output.good(), "fixture mutation failed");
    }
    void reject(const char* context) const
    {
        std::string error;
        try { (void)load(); }
        catch (const std::exception& failure) { error = failure.what(); }
        require(!error.empty() && error.find(context) != std::string::npos,
            std::string("expected rejection containing ") + context + "; received " + error);
    }
private:
    void cleanup() noexcept
    {
        if (root_.empty() || root_.parent_path() != parent_ || !root_.filename().string().starts_with("simcore-runtime-catalog-")) return;
        std::error_code ignored;
        fs::remove_all(root_, ignored);
    }
    fs::path parent_, root_;
};

void test_parameter_parity()
{
    const auto catalog = load_runtime_vehicle_catalog(SIMCORE_TEST_RUNTIME_VEHICLE_CATALOG_PATH);
    std::vector<VehicleParameters> baselines{VehicleParameters{}, load_vehicle_parameters(SIMCORE_TEST_VEHICLE_CONFIG_PATH).parameters};
    auto custom = baselines.back();
    custom.mass_kg = 1800.f;
    custom.fuel_rate_percent_s = 0.007f;
    custom.tire_friction = 0.88f;
    custom.reverse_gear_ratio = 4.2f;
    custom.front_static_load_fraction = 0.53f;
    custom.suspension.spring_rate_n_per_m = 40000.f;
    custom.suspension.damper_rate_n_s_per_m = 4100.f;
    custom.suspension.max_force_n = 15000.f;
    baselines.push_back(custom);
    for (std::size_t i = 0; i < baselines.size(); ++i) {
        for (const auto type : {RuntimeVehicleClass::Unspecified, RuntimeVehicleClass::Sedan,
                RuntimeVehicleClass::Compact, RuntimeVehicleClass::Truck, RuntimeVehicleClass::Motorcycle}) {
            compare(catalog.player_parameters(baselines[i], type),
                legacy_profile_oracle::make_player_vehicle_parameters(baselines[i], type),
                "baseline " + std::to_string(i) + " class " + std::to_string(static_cast<int>(type)));
        }
    }
}

void test_npc_parity()
{
    const std::array<RuntimeNpcProfile, 4> expected{{
        {"sedan", 2.20, 1.00, 0.75, 1500.0, 2600.0, 30.0, 1.00, 1.50, 3.00, 0.35, 0.10, 4.8},
        {"compact", 1.75, 0.86, 0.70, 1050.0, 1450.0, 32.0, 1.08, 1.90, 3.40, 0.32, 0.10, 4.0},
        {"truck", 3.10, 1.05, 0.965, 6200.0, 14500.0, 20.0, 0.72, 0.85, 2.20, 0.65, 0.411071, 8.5},
        {"motorcycle", 1.10, 0.42, 0.68, 240.0, 210.0, 36.0, 1.12, 2.30, 4.00, 0.38, 0.10, 2.6}
    }};
    const auto catalog = load_runtime_vehicle_catalog(SIMCORE_TEST_RUNTIME_VEHICLE_CATALOG_PATH);
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const auto& profile = catalog.fleet()[i];
        const auto& actual = profile.npc;
        const auto& before = expected[i];
        require(static_cast<unsigned>(profile.vehicle_class) == i + 1, "fleet must follow class order");
        require(actual.name == before.name, "NPC name differs");
        require(actual.half_length_m == before.half_length_m && actual.half_width_m == before.half_width_m
            && actual.half_height_m == before.half_height_m && actual.mass_kg == before.mass_kg
            && actual.yaw_inertia_kg_m2 == before.yaw_inertia_kg_m2
            && actual.maximum_reaction_speed_mps == before.maximum_reaction_speed_mps
            && actual.speed_scale == before.speed_scale && actual.acceleration_mps2 == before.acceleration_mps2
            && actual.braking_mps2 == before.braking_mps2 && actual.tumble_contact_below_cg_m == before.tumble_contact_below_cg_m
            && actual.ground_clearance_m == before.ground_clearance_m && actual.minimum_turn_radius_m == before.minimum_turn_radius_m,
            "NPC numeric parameters differ from legacy");
    }
}

void select_axle(Fixture& fixture, const char* profile, const char* axle,
                 const char* tire, const char* suspension)
{
    const std::string before = std::string("\"") + axle
        + "\": {\n      \"tire\": null,\n      \"suspension\": null\n    }";
    const std::string after = std::string("\"") + axle + "\": {\n      \"tire\": "
        + (tire ? std::string("\"") + tire + "\"" : "null") + ",\n      \"suspension\": "
        + (suspension ? std::string("\"") + suspension + "\"" : "null") + "\n    }";
    fixture.replace(profile, before, after);
}

void test_selected_front_modules()
{
    Fixture fixture;
    select_axle(fixture, "profiles/sedan.json", "front", "tire_sedan_comfort", "suspension_sedan_comfort");
    const auto catalog = fixture.load();
    const VehicleParameters base;
    const auto actual = catalog.player_parameters(base, RuntimeVehicleClass::Sedan);
    require(actual.front_axle_contact.tire && actual.front_axle_contact.suspension
        && !actual.rear_axle_contact.tire && !actual.rear_axle_contact.suspension,
        "front module selection must not leak into the rear axle");
    const auto front = resolved_tire_parameters(actual, 0);
    const auto rear = resolved_tire_parameters(actual, 2);
    require(front.friction_coefficient == 0.98f && front.cornering_stiffness_n_rad == 55000.f
        && front.longitudinal_stiffness_n == 85000.f && front.rolling_resistance_coefficient == 0.013f,
        "selected tire fields must reach the actual front contact parameters");
    require(rear.friction_coefficient == base.tire_friction
        && rear.cornering_stiffness_n_rad == base.rear_tire_corner_stiffness_n_rad,
        "unselected rear tire must keep the baseline values");
    require(resolved_suspension_parameters(actual, 0).spring_rate_n_per_m == 27000.f
        && resolved_suspension_parameters(actual, 0).damper_rate_n_s_per_m == 4100.f
        && resolved_suspension_parameters(actual, 0).max_compression_m == 0.18f,
        "selected suspension fields must reach the actual front contact parameters");
    require(actual.mass_kg == base.mass_kg && actual.yaw_inertia_kg_m2 == base.yaw_inertia_kg_m2
        && actual.pitch_inertia_kg_m2 == base.pitch_inertia_kg_m2 && actual.roll_inertia_kg_m2 == base.roll_inertia_kg_m2,
        "contact-only module selection must not pretend to recompute chassis mass or inertia");
}

void test_standard_modules_match_legacy()
{
    Fixture fixture;
    select_axle(fixture, "profiles/sedan.json", "front", "tire_sedan_front_standard", "suspension_sedan_standard");
    select_axle(fixture, "profiles/sedan.json", "rear", "tire_sedan_rear_standard", "suspension_sedan_standard");
    const VehicleParameters base;
    const auto actual = fixture.load().player_parameters(base, RuntimeVehicleClass::Sedan);
    for (const std::size_t index : {0U, 1U, 2U, 3U}) {
        const auto expected_tire = resolved_tire_parameters(base, index);
        const auto tire = resolved_tire_parameters(actual, index);
        const auto& suspension = resolved_suspension_parameters(actual, index);
        require(tire.radius_m == expected_tire.radius_m && tire.friction_coefficient == expected_tire.friction_coefficient
            && tire.longitudinal_stiffness_n == expected_tire.longitudinal_stiffness_n
            && tire.cornering_stiffness_n_rad == expected_tire.cornering_stiffness_n_rad
            && tire.rolling_resistance_coefficient == expected_tire.rolling_resistance_coefficient,
            "baseline module tire conversion must preserve legacy per-contact values exactly");
        require(suspension.rest_length_m == base.suspension.rest_length_m
            && suspension.max_compression_m == base.suspension.max_compression_m
            && suspension.max_extension_m == base.suspension.max_extension_m
            && suspension.spring_rate_n_per_m == base.suspension.spring_rate_n_per_m
            && suspension.damper_rate_n_s_per_m == base.suspension.damper_rate_n_s_per_m
            && suspension.max_force_n == base.suspension.max_force_n,
            "baseline module suspension conversion must preserve all six legacy values exactly");
    }
}

void test_single_track_module_conversion()
{
    Fixture fixture;
    select_axle(fixture, "profiles/motorcycle.json", "front", "tire_sedan_front_standard", "suspension_sedan_standard");
    const auto actual = fixture.load().player_parameters(VehicleParameters{}, RuntimeVehicleClass::Motorcycle);
    for (const std::size_t index : {0U, 1U}) {
        const auto tire = resolved_tire_parameters(actual, index);
        const auto& suspension = resolved_suspension_parameters(actual, index);
        require(tire.longitudinal_stiffness_n == 45000.f && tire.cornering_stiffness_n_rad == 30000.f,
            "paired motorcycle slots must each receive half the physical tire stiffness");
        require(tire.radius_m == 0.32f && tire.friction_coefficient == 1.05f && tire.rolling_resistance_coefficient == 0.015f,
            "intensive tire properties must not be halved for paired motorcycle slots");
        require(suspension.spring_rate_n_per_m == 30645.78125f * 0.5f
            && suspension.damper_rate_n_s_per_m == 1750.f && suspension.max_force_n == 6000.f
            && suspension.rest_length_m == 0.35f && suspension.max_compression_m == 0.15f,
            "physical motorcycle suspension force values divide once; travel must stay unchanged");
    }
}

void select_powertrain(Fixture& fixture, const char* profile = "profiles/sedan.json")
{
    fixture.replace(profile, "\"powertrain_modules\": null", R"("powertrain_modules": {
    "engine": "engine_sedan_gasoline_2_0",
    "transmission": "transmission_sedan_automatic_6speed",
    "drivetrain": "drivetrain_sedan_rwd",
    "fuel_tank": "fuel_tank_sedan_50l",
    "initial_fuel_l": 35,
    "upshift_rpm": 5200,
    "downshift_rpm": 2000,
    "shift_duration_s": 0.25
  })");
}

void test_powertrain_module_mapping()
{
    Fixture fixture;
    select_powertrain(fixture);
    const auto catalog = fixture.load();
    const VehicleParameters base;
    const auto actual = catalog.player_parameters(base, RuntimeVehicleClass::Sedan);
    require(actual.powertrain.has_value(), "selected powertrain must reach live vehicle parameters");
    const auto& selected = *actual.powertrain;
    require(selected.engine.torque_curve.size() == 7 && selected.engine.torque_curve[3].torque_nm == 200.f
        && selected.engine.torque_curve.front().rpm == 800.f && selected.engine.torque_curve.back().rpm == 6500.f
        && selected.engine.rotational_inertia_kg_m2 == 0.22f && selected.engine.response_time_s == 0.15f
        && selected.engine.idle_fuel_lph == 0.8f && selected.engine.bsfc_g_per_kwh == 270.f,
        "engine curve, rotational response and fuel model must map from the selected part");
    require(selected.transmission.forward_ratios.size() == 6 && selected.transmission.forward_ratios.front() == 3.63f
        && selected.transmission.forward_ratios.back() == 0.69f && selected.transmission.reverse_ratio == 3.27f
        && selected.transmission.max_input_torque_nm == 300.f && selected.transmission.efficiency == 0.94f
        && selected.transmission.downshift_rpm == 2000.f && selected.transmission.upshift_rpm == 5200.f
        && selected.transmission.shift_duration_s == 0.25f, "gears and the complete authored shift policy must map");
    require(selected.drivetrain.final_drive_ratio == 3.5f && selected.drivetrain.efficiency == 0.96f
        && selected.drivetrain.front_torque_fraction == 0.f
        && selected.fuel_tank.initial_fuel_l == 35.f && selected.fuel_tank.capacity_l == 50.f
        && selected.fuel_tank.fuel_density_kg_l == 0.745f, "drivetrain and litre-based fuel state must map");
    require(actual.mass_kg == base.mass_kg && actual.yaw_inertia_kg_m2 == base.yaw_inertia_kg_m2
        && actual.cg_height_m == base.cg_height_m, "powertrain modules must not silently recompute chassis mass");
    require(actual.max_drive_force_n == base.max_drive_force_n && actual.max_drive_power_w == base.max_drive_power_w
        && actual.max_rpm == base.max_rpm && actual.fuel_rate_percent_s == base.fuel_rate_percent_s,
        "legacy power and fuel scalars must not be overwritten by the new optional model");
    require(catalog.checksum() != default_runtime_vehicle_catalog().checksum(), "module selection must change replay/catalog identity");
}

std::array<std::optional<std::string>, 8> module_ids(const RuntimeVehicleLoadout& loadout)
{
    const auto& powertrain = loadout.powertrain_modules.value();
    return {loadout.front_axle_modules.tire, loadout.front_axle_modules.suspension,
        loadout.rear_axle_modules.tire, loadout.rear_axle_modules.suspension,
        powertrain.engine, powertrain.transmission, powertrain.drivetrain, powertrain.fuel_tank};
}

void reject_operation(const std::function<void()>& operation, std::string_view expected)
{
    std::string message;
    try { operation(); }
    catch (const std::exception& error) { message = error.what(); }
    require(!message.empty() && message.find(expected) != std::string::npos,
        "expected rejection containing '" + std::string(expected) + "'; received '" + message + "'");
}

using Test = std::pair<const char*, std::function<void()>>;
std::vector<Test> tests()
{
    return {
        {"custom selection golden ID and immutable resolution", [] {
            const auto& catalog = default_runtime_vehicle_catalog();
            const auto base = catalog.loadout("sedan_modular_standard", RuntimeVehicleClass::Sedan);
            auto slots = module_ids(base);
            slots[0] = "tire_sedan_comfort";
            const auto id = catalog.custom_loadout_id(base.id, slots);
            require(id == "parts_v1_01_0504070401080002" && id.size() == 28,
                "custom ID must follow cross-language ASCII sorting and fixed slot encoding");
            auto selected = catalog.loadout(id, RuntimeVehicleClass::Sedan);
            require(selected.id == id && module_ids(selected) == slots
                && selected.vehicle_class == base.vehicle_class && selected.powertrain_modules->initial_fuel_l == 35,
                "decoder must recover complete parts and preserve the base policy");
            const auto effective = catalog.player_parameters(VehicleParameters{}, RuntimeVehicleClass::Sedan, id);
            require(resolved_tire_parameters(effective, 0).friction_coefficient == 0.98f
                && resolved_tire_parameters(effective, 2).friction_coefficient == 1.05f && effective.powertrain,
                "custom front-only selection must flow through normal module mapping");
            selected.powertrain_modules->initial_fuel_l = 1;
            require(catalog.loadout(id, RuntimeVehicleClass::Sedan).powertrain_modules->initial_fuel_l == 35
                && catalog.loadout(base.id, RuntimeVehicleClass::Sedan).front_axle_modules.tire == "tire_sedan_front_standard"
                && catalog.loadouts().size() == 2, "custom resolution must not mutate or cache entries in the catalog");
        }},
        {"unchanged custom selection canonicalizes to named preset", [] {
            const auto& catalog = default_runtime_vehicle_catalog();
            for (const auto& [id, preset] : catalog.loadouts())
                require(catalog.custom_loadout_id(id, module_ids(preset)) == id,
                    "unchanged selection must keep its named identity");
            for (const auto* redundant : {"parts_v1_01_0604070401080002", "parts_v1_00_0503050301080002"})
                reject_operation([&] { (void)catalog.loadout(redundant, RuntimeVehicleClass::Sedan); }, "redundant");
        }},
        {"optional custom axle slots return to scalar baseline", [] {
            const auto& catalog = default_runtime_vehicle_catalog();
            auto slots = module_ids(catalog.loadout("sedan_modular_standard", RuntimeVehicleClass::Sedan));
            for (std::size_t slot = 0; slot < 4; ++slot) slots[slot].reset();
            const auto id = catalog.custom_loadout_id("sedan_modular_standard", slots);
            require(id == "parts_v1_01_ffffffff01080002", "ff must encode precisely the optional axle slots");
            VehicleParameters base;
            base.tire_friction = 0.89f;
            base.suspension.damper_rate_n_s_per_m = 4200.f;
            const auto effective = catalog.player_parameters(base, RuntimeVehicleClass::Sedan, id);
            require(!effective.front_axle_contact.tire && !effective.rear_axle_contact.tire
                && !effective.front_axle_contact.suspension && !effective.rear_axle_contact.suspension
                && effective.powertrain && resolved_tire_parameters(effective, 0).friction_coefficient == 0.89f
                && resolved_suspension_parameters(effective, 2).damper_rate_n_s_per_m == 4200.f,
                "null axle slots must not inherit named preset parts");
        }},
        {"custom decoder rejects malformed versions and indexes", [] {
            const auto& catalog = default_runtime_vehicle_catalog();
            for (const auto* id : {"parts_v2_01_0504070401080002", "parts_v1_01_050407040108000",
                    "parts_v1_01_05040704010800020", "parts_v1_01x0504070401080002",
                    "parts_v1_0A_0504070401080002", "parts_v1_01_g504070401080002",
                    "parts_v1_ff_0504070401080002", "parts_v1_40_0504070401080002",
                    "parts_v1_01_0904070401080002", "parts_v1_01_fe04070401080002",
                    "parts_v1_01_05040704ff080002", "parts_v1_01_0504070401ff0002",
                    "parts_v1_01_050407040108ff02", "parts_v1_01_05040704010800ff",
                    "parts_v1_01_FF04070401080002"}) {
                reject_operation([&] { (void)catalog.loadout(id, RuntimeVehicleClass::Sedan); }, "");
            }
        }},
        {"custom decoder enforces class and each slot kind", [] {
            const auto& catalog = default_runtime_vehicle_catalog();
            reject_operation([&] { (void)catalog.loadout("parts_v1_01_0504070401080002", RuntimeVehicleClass::Truck); },
                "vehicle class");
            const std::string golden = "parts_v1_01_0504070401080002";
            for (std::size_t slot = 0; slot < 8; ++slot) {
                auto id = golden;
                // Engine is wrong for every slot except engine; use tire there.
                id.replace(12 + slot * 2, 2, slot == 4 ? "05" : "01");
                reject_operation([&] { (void)catalog.loadout(id, RuntimeVehicleClass::Sedan); }, "wrong part kind");
            }
        }},
        {"custom encoder rejects unknown parts and mandatory nulls", [] {
            const auto& catalog = default_runtime_vehicle_catalog();
            const auto base = catalog.loadout("sedan_modular_standard", RuntimeVehicleClass::Sedan);
            const auto original = module_ids(base);
            auto ids = original;
            ids[0] = "missing_tire";
            reject_operation([&] { (void)catalog.custom_loadout_id(base.id, ids); }, "unknown part");
            ids[0] = "suspension_sedan_standard";
            reject_operation([&] { (void)catalog.custom_loadout_id(base.id, ids); }, "wrong part kind");
            for (std::size_t slot = 4; slot < 8; ++slot) {
                ids = original;
                ids[slot].reset();
                reject_operation([&] { (void)catalog.custom_loadout_id(base.id, ids); }, "cannot be null");
            }
            for (const char* id : {"missing", "parts_v1_01_0504070401080002"})
                reject_operation([&] { (void)catalog.custom_loadout_id(id, original); }, "named base");
        }},
        {"custom selection requires complete base powertrain policy", [] {
            Fixture fixture;
            fixture.write("loadouts/sedan_modular_standard.json", R"({"schema_version":1,"id":"sedan_modular_standard","name":"Legacy preset","vehicle_class":1,"axle_modules":{"front":{"tire":null,"suspension":null},"rear":{"tire":null,"suspension":null}},"powertrain_modules":null})");
            const auto catalog = fixture.load();
            const auto ids = module_ids(default_runtime_vehicle_catalog().loadout("sedan_modular_standard", RuntimeVehicleClass::Sedan));
            reject_operation([&] { (void)catalog.custom_loadout_id("sedan_modular_standard", ids); }, "complete powertrain policy");
            reject_operation([&] { (void)catalog.loadout("parts_v1_01_0504070401080002", RuntimeVehicleClass::Sedan); },
                "complete powertrain policy");
        }},
        {"authored custom prefix is reserved", [] {
            Fixture fixture;
            fixture.replace("loadouts/sedan_modular_standard.json", "\"id\": \"sedan_modular_standard\"",
                "\"id\": \"parts_v1_example\"");
            fixture.reject("reserved custom loadout prefix");
        }},
        {"custom parts retain effective static load checks", [] {
            Fixture fixture;
            fixture.write("parts/weak_tire.json", R"({"schema_version":1,"id":"weak_tire","name":"Under-rated tire","kind":"tire","interface_id":"tire_16in","mass":{"mass_kg":10,"center_of_mass_m":[0,0,0],"inertia_diagonal_kg_m2":[0.5,0.9,0.5]},"specification":{"radius_m":0.32,"width_m":0.215,"rim_diameter_m":0.4064,"min_rim_width_m":0.17,"max_rim_width_m":0.20,"friction_coefficient":1,"longitudinal_stiffness_n":90000,"cornering_stiffness_n_rad":60000,"rolling_resistance_coefficient":0.015,"rated_load_n":100}})");
            fixture.replace("catalog.json", "\"parts\": [", "\"parts\": [\"parts/weak_tire.json\",");
            const auto catalog = fixture.load();
            auto ids = module_ids(catalog.loadout("sedan_modular_standard", RuntimeVehicleClass::Sedan));
            ids[0] = "weak_tire";
            reject_operation([&] { (void)catalog.custom_loadout_id("sedan_modular_standard", ids); }, "static load");
            VehicleParameters heavy;
            heavy.mass_kg = 10000.f;
            reject_operation([&] { (void)catalog.player_parameters(heavy, RuntimeVehicleClass::Sedan,
                "parts_v1_01_0504070401080002"); }, "static load");
        }},
        {"custom parts inherit base fuel and shift policy exactly", [] {
            Fixture fixture;
            fixture.replace("loadouts/sedan_modular_standard.json", "\"initial_fuel_l\": 35", "\"initial_fuel_l\": 21");
            fixture.replace("loadouts/sedan_modular_standard.json", "\"upshift_rpm\": 5200", "\"upshift_rpm\": 4800");
            fixture.replace("loadouts/sedan_modular_standard.json", "\"downshift_rpm\": 2000", "\"downshift_rpm\": 1600");
            fixture.replace("loadouts/sedan_modular_standard.json", "\"shift_duration_s\": 0.25", "\"shift_duration_s\": 0.4");
            const auto catalog = fixture.load();
            auto ids = module_ids(catalog.loadout("sedan_modular_standard", RuntimeVehicleClass::Sedan));
            ids[0] = "tire_sedan_comfort";
            const auto encoded = catalog.custom_loadout_id("sedan_modular_standard", ids);
            const auto parameters = catalog.player_parameters(VehicleParameters{}, RuntimeVehicleClass::Sedan, encoded);
            require(parameters.powertrain->fuel_tank.initial_fuel_l == 21.f
                && parameters.powertrain->transmission.upshift_rpm == 4800.f
                && parameters.powertrain->transmission.downshift_rpm == 1600.f
                && parameters.powertrain->transmission.shift_duration_s == 0.4f,
                "custom ID must not manufacture or retain policy from another preset");
        }},
        {"custom indexes ignore manifest ordering", [] {
            Fixture fixture;
            fixture.replace("catalog.json", "\"loadouts/sedan_modular_standard.json\",\n    \"loadouts/sedan_modular_comfort.json\"",
                "\"loadouts/sedan_modular_comfort.json\",\n    \"loadouts/sedan_modular_standard.json\"");
            fixture.replace("catalog.json", "\"parts/tire_sedan_front_standard.json\",\n    \"parts/tire_sedan_rear_standard.json\"",
                "\"parts/tire_sedan_rear_standard.json\",\n    \"parts/tire_sedan_front_standard.json\"");
            const auto catalog = fixture.load();
            auto ids = module_ids(catalog.loadout("sedan_modular_standard", RuntimeVehicleClass::Sedan));
            ids[0] = "tire_sedan_comfort";
            require(catalog.custom_loadout_id("sedan_modular_standard", ids) == "parts_v1_01_0504070401080002",
                "indexes must follow ASCII ID ordering rather than manifest/path ordering");
            require(catalog.checksum() != default_runtime_vehicle_catalog().checksum(),
                "different manifest bytes must still have different handshake identity");
        }},
        {"complete named loadouts resolve without changing defaults", [] {
            const auto catalog = load_runtime_vehicle_catalog(SIMCORE_TEST_RUNTIME_VEHICLE_CATALOG_PATH);
            require(catalog.loadouts().size() == 2, "expected two authored sedan presets");
            const auto& standard = catalog.loadout("sedan_modular_standard", RuntimeVehicleClass::Sedan);
            require(standard.name == "Sedan - Modular Standard" && standard.vehicle_class == RuntimeVehicleClass::Sedan,
                "loadout identity/display name/class must be available to the selector");
            const auto base = load_vehicle_parameters(SIMCORE_TEST_VEHICLE_CONFIG_PATH).parameters;
            const auto normal = catalog.player_parameters(base, RuntimeVehicleClass::Sedan, standard.id);
            const auto comfort = catalog.player_parameters(base, RuntimeVehicleClass::Sedan, "sedan_modular_comfort");
            require(normal.powertrain && comfort.powertrain && normal.powertrain->fuel_tank.initial_fuel_l == 35.f
                && comfort.powertrain->fuel_tank.initial_fuel_l == 35.f, "complete preset must include its powertrain");
            for (const auto wheel : {0u, 2u}) {
                require(resolved_tire_parameters(normal, wheel).friction_coefficient == 1.05f
                    && resolved_tire_parameters(comfort, wheel).friction_coefficient == 0.98f
                    && resolved_suspension_parameters(normal, wheel).spring_rate_n_per_m == 30645.78125f
                    && resolved_suspension_parameters(comfort, wheel).spring_rate_n_per_m == 27000.f,
                    "standard and comfort must change both axles through the shared module mapper");
            }
            require(normal.mass_kg == base.mass_kg && comfort.mass_kg == base.mass_kg,
                "preset selection must not add authoring metadata mass");
            compare(catalog.player_parameters(base, RuntimeVehicleClass::Sedan), base, "default after preset resolution");
            require(catalog.loadout(standard.id, RuntimeVehicleClass::Unspecified).id == standard.id,
                "unspecified class must retain its sedan alias");
        }},
        {"loadout class and unknown ID requests rejected", [] {
            const auto& catalog = default_runtime_vehicle_catalog();
            for (const auto& [id, type] : std::vector<std::pair<std::string, RuntimeVehicleClass>>{
                    {"missing", RuntimeVehicleClass::Sedan}, {"sedan_modular_standard", RuntimeVehicleClass::Truck},
                    {"../sedan_modular_standard", RuntimeVehicleClass::Sedan}}) {
                bool rejected = false;
                try { (void)catalog.player_parameters(VehicleParameters{}, type, id); }
                catch (const std::invalid_argument&) { rejected = true; }
                require(rejected, "invalid loadout request must not silently fall back to another vehicle/preset");
            }
        }},
        {"null loadout selections replace profile and previous modules", [] {
            Fixture fixture;
            select_powertrain(fixture);
            select_axle(fixture, "profiles/sedan.json", "front", "tire_sedan_comfort", "suspension_sedan_comfort");
            select_axle(fixture, "profiles/sedan.json", "rear", "tire_sedan_comfort", "suspension_sedan_comfort");
            fixture.write("loadouts/sedan_modular_standard.json", R"({"schema_version":1,"id":"sedan_modular_standard","name":"Baseline preset","vehicle_class":1,"axle_modules":{"front":{"tire":null,"suspension":null},"rear":{"tire":null,"suspension":null}},"powertrain_modules":null})");
            const auto catalog = fixture.load();
            VehicleParameters base;
            base.tire_friction = 0.91f;
            base.suspension.damper_rate_n_s_per_m = 3900.f;
            const auto selected = catalog.player_parameters(base, RuntimeVehicleClass::Sedan);
            require(selected.powertrain && selected.front_axle_contact.tire && selected.rear_axle_contact.suspension,
                "fixture profile must have selected modules before overriding");
            compare(catalog.player_parameters(base, RuntimeVehicleClass::Sedan, "sedan_modular_standard"), base,
                "explicit null loadout overrides profile");
            auto previous = base;
            previous.front_axle_contact = selected.front_axle_contact;
            previous.rear_axle_contact = selected.rear_axle_contact;
            previous.powertrain = selected.powertrain;
            compare(catalog.player_parameters(previous, RuntimeVehicleClass::Sedan, "sedan_modular_standard"), base,
                "explicit null loadout clears previously resolved modules");
        }},
        {"loadout schema fields and types strict", [] {
            for (const auto& mutation : std::vector<std::pair<std::string, std::string>>{
                    {"\"schema_version\": 1", "\"schema_version\": 2"},
                    {"\"name\": \"Sedan - Modular Standard\",", ""},
                    {"\"name\": \"Sedan - Modular Standard\"", "\"name\": 1"},
                    {"\"vehicle_class\": 1", "\"vehicle_class\": 1.5"},
                    {"\"schema_version\": 1", "\"schema_version\": 1, \"extra\": true"},
                    {"\"schema_version\": 1", "\"schema_version\": 1, \"schema_version\": 1"},
                    {"\"axle_modules\": {", "\"axle_modules\": { \"extra\": null,"},
                    {"\"initial_fuel_l\": 35,", ""}}) {
                Fixture fixture;
                fixture.replace("loadouts/sedan_modular_standard.json", mutation.first, mutation.second);
                fixture.reject("");
            }
        }},
        {"loadout references and compatibility validated before publication", [] {
            for (const auto& mutation : std::vector<std::pair<std::string, std::string>>{
                    {"\"tire_sedan_front_standard\"", "\"missing_tire\""},
                    {"\"tire_sedan_front_standard\"", "\"suspension_sedan_standard\""},
                    {"\"engine_sedan_gasoline_2_0\"", "\"missing_engine\""},
                    {"\"initial_fuel_l\": 35", "\"initial_fuel_l\": 51"},
                    {"\"downshift_rpm\": 2000", "\"downshift_rpm\": 800"},
                    {"\"vehicle_class\": 1", "\"vehicle_class\": 3"}}) {
                Fixture fixture;
                fixture.replace("loadouts/sedan_modular_standard.json", mutation.first, mutation.second);
                fixture.reject("");
            }
        }},
        {"loadout validates the actual custom baseline", [] {
            auto heavy = VehicleParameters{};
            heavy.mass_kg = 10000.f;
            require(valid_vehicle_parameters(heavy), "fixture baseline must otherwise be valid");
            bool rejected = false;
            try { (void)default_runtime_vehicle_catalog().player_parameters(heavy, RuntimeVehicleClass::Sedan, "sedan_modular_standard"); }
            catch (const std::invalid_argument& error) { rejected = std::string(error.what()).find("static load") != std::string::npos; }
            require(rejected, "loadout's tire ratings must be checked against requested baseline mass");
        }},
        {"duplicate loadout IDs share global namespace", [] {
            for (const char* id : {"sedan_modular_comfort", "sedan_standard", "tire_sedan_front_standard"}) {
                Fixture fixture;
                fixture.replace("loadouts/sedan_modular_standard.json", "\"id\": \"sedan_modular_standard\"",
                    std::string("\"id\": \"") + id + "\"");
                fixture.reject("duplicate catalog ID");
            }
        }},
        {"duplicate loadout and shared source files rejected", [] {
            for (const char* path : {"loadouts/sedan_modular_comfort.json", "parts/tire_sedan_front_standard.json", "profiles/sedan.json"}) {
                Fixture fixture;
                fixture.replace("catalog.json", "loadouts/sedan_modular_standard.json", path);
                fixture.reject("duplicate catalog file");
            }
        }},
        {"loadout source paths confined to catalog", [] {
            for (const auto* path : {"../sedan_modular_standard.json", "loadouts/../sedan_modular_standard.json",
                    "loadouts\\sedan_modular_standard.json", "/loadouts/sedan_modular_standard.json"}) {
                Fixture fixture;
                fixture.replace("catalog.json", "loadouts/sedan_modular_standard.json", path);
                fixture.reject("");
            }
        }},
        {"loadout list count and type bounded", [] {
            for (const auto* list : {"null", "{}", "true"}) {
                Fixture fixture;
                fixture.write("catalog.json", std::string(R"({"schema_version":1,"profiles":["profiles/sedan.json","profiles/compact.json","profiles/truck.json","profiles/motorcycle.json"],"loadouts":)") + list + "}");
                fixture.reject("array shape");
            }
            Fixture fixture;
            std::string paths;
            for (int i = 0; i < 65; ++i) paths += (i ? "," : "") + std::string("\"loadouts/sedan_modular_standard.json\"");
            fixture.write("catalog.json", std::string(R"({"schema_version":1,"profiles":["profiles/sedan.json","profiles/compact.json","profiles/truck.json","profiles/motorcycle.json"],"loadouts":[)") + paths + "]}");
            fixture.reject("array shape");
        }},
        {"loadout raw bytes affect identity but snapshots stay immutable", [] {
            Fixture fixture;
            const auto first = fixture.load();
            require(first.checksum() == default_runtime_vehicle_catalog().checksum(), "relocation must not affect loadout identity");
            fixture.replace("loadouts/sedan_modular_standard.json", "\"initial_fuel_l\": 35", "\"initial_fuel_l\": 25");
            const auto second = fixture.load();
            require(first.checksum() != second.checksum(), "loadout bytes must participate in the handshake/replay identity");
            require(first.player_parameters(VehicleParameters{}, RuntimeVehicleClass::Sedan, "sedan_modular_standard").powertrain->fuel_tank.initial_fuel_l == 35.f
                && second.player_parameters(VehicleParameters{}, RuntimeVehicleClass::Sedan, "sedan_modular_standard").powertrain->fuel_tank.initial_fuel_l == 25.f,
                "later loadout edits must not mutate accepted catalog snapshots");
        }},
        {"bit-exact player parameter parity", test_parameter_parity},
        {"exact NPC fleet parity", test_npc_parity},
        {"front-only contact module selection", test_selected_front_modules},
        {"legacy-calibrated standard module conversion", test_standard_modules_match_legacy},
        {"paired motorcycle module conversion", test_single_track_module_conversion},
        {"complete powertrain module mapping", test_powertrain_module_mapping},
        {"powertrain null and omission preserve defaults", [] {
            Fixture fixture;
            fixture.replace("profiles/sedan.json", ",\n  \"powertrain_modules\": null", "");
            const auto catalog = fixture.load();
            compare(catalog.player_parameters(VehicleParameters{}, RuntimeVehicleClass::Sedan), VehicleParameters{}, "omitted powertrain");
            for (const auto& profile : catalog.fleet())
                require(!catalog.player_parameters(VehicleParameters{}, profile.vehicle_class).powertrain,
                    "unselected powertrain must not manufacture fallback parts");
        }},
        {"partial powertrain selection rejected", [] {
            Fixture fixture;
            fixture.replace("profiles/sedan.json", "\"powertrain_modules\": null",
                "\"powertrain_modules\": {\"engine\":\"engine_sedan_gasoline_2_0\"}");
            fixture.reject("missing field");
        }},
        {"powertrain object type preserved", [] {
            for (const auto* value : {"true", "[]", "1", "\"legacy\""}) {
                Fixture fixture;
                fixture.replace("profiles/sedan.json", "\"powertrain_modules\": null",
                    std::string("\"powertrain_modules\": ") + value);
                fixture.reject("expected object");
            }
        }},
        {"unknown powertrain field rejected", [] {
            Fixture fixture;
            select_powertrain(fixture);
            fixture.replace("profiles/sedan.json", "\"initial_fuel_l\": 35", "\"initial_fuel_l\": 35, \"unknown\": 1");
            fixture.reject("unknown field");
        }},
        {"missing powertrain part rejected", [] {
            Fixture fixture;
            select_powertrain(fixture);
            fixture.replace("profiles/sedan.json", "\"engine\": \"engine_sedan_gasoline_2_0\"", "\"engine\": \"missing_engine\"");
            fixture.reject("unknown part");
        }},
        {"wrong powertrain part kind rejected", [] {
            Fixture fixture;
            select_powertrain(fixture);
            fixture.replace("profiles/sedan.json", "\"engine\": \"engine_sedan_gasoline_2_0\"", "\"engine\": \"tire_sedan_front_standard\"");
            fixture.reject("wrong part kind");
        }},
        {"engine and tank fuel compatibility", [] {
            Fixture fixture;
            select_powertrain(fixture);
            fixture.replace("parts/fuel_tank_sedan_50l.json", "\"fuel_type\": \"gasoline\"", "\"fuel_type\": \"diesel\"");
            fixture.reject("fuel_type mismatch");
        }},
        {"gearbox torque capacity", [] {
            Fixture fixture;
            select_powertrain(fixture);
            fixture.replace("parts/transmission_sedan_automatic_6speed.json", "\"max_input_torque_nm\": 300", "\"max_input_torque_nm\": 190");
            fixture.reject("engine peak torque exceeds");
        }},
        {"powertrain shift policy limits", [] {
            for (const auto& mutation : std::vector<std::pair<std::string, std::string>>{
                    {"\"downshift_rpm\": 2000", "\"downshift_rpm\": 800"},
                    {"\"downshift_rpm\": 2000", "\"downshift_rpm\": 5200"},
                    {"\"upshift_rpm\": 5200", "\"upshift_rpm\": 6500"},
                    {"\"shift_duration_s\": 0.25", "\"shift_duration_s\": 0"},
                    {"\"shift_duration_s\": 0.25", "\"shift_duration_s\": 10.01"}}) {
                Fixture fixture;
                select_powertrain(fixture);
                fixture.replace("profiles/sedan.json", mutation.first, mutation.second);
                fixture.reject("shift");
            }
        }},
        {"initial fuel capacity and type", [] {
            for (const auto* fuel : {"-1", "51", "5001", "\"35\""}) {
                Fixture fixture;
                select_powertrain(fixture);
                fixture.replace("profiles/sedan.json", "\"initial_fuel_l\": 35", std::string("\"initial_fuel_l\": ") + fuel);
                fixture.reject("initial_fuel_l");
            }
            for (const auto* fuel : {"0", "50"}) {
                Fixture fixture;
                select_powertrain(fixture);
                fixture.replace("profiles/sedan.json", "\"initial_fuel_l\": 35", std::string("\"initial_fuel_l\": ") + fuel);
                require(fixture.load().player_parameters(VehicleParameters{}, RuntimeVehicleClass::Sedan).powertrain.has_value(),
                    "empty and full tanks must remain valid initial states");
            }
        }},
        {"engine curve coverage and ordering", [] {
            for (const auto& mutation : std::vector<std::pair<std::string, std::string>>{
                    {"\"idle_rpm\": 800", "\"idle_rpm\": 850"},
                    {"\"rpm\": 1500", "\"rpm\": 800"}}) {
                Fixture fixture;
                select_powertrain(fixture);
                fixture.replace("parts/engine_sedan_gasoline_2_0.json", mutation.first, mutation.second);
                fixture.reject("torque curve");
            }
        }},
        {"gear ratio ordering remains strict", [] {
            Fixture fixture;
            select_powertrain(fixture);
            fixture.replace("parts/transmission_sedan_automatic_6speed.json", "3.63,\n      2.09", "3.63,\n      4.0");
            fixture.reject("strictly decrease");
        }},
        {"powertrain float underflow rejected", [] {
            Fixture fixture;
            select_powertrain(fixture);
            fixture.replace("parts/engine_sedan_gasoline_2_0.json", "\"idle_fuel_lph\": 0.8", "\"idle_fuel_lph\": 1e-300");
            fixture.reject("finite float range");
        }},
        {"selected torque split feeds legacy tire distribution", [] {
            Fixture fixture;
            select_powertrain(fixture);
            fixture.replace("parts/drivetrain_sedan_rwd.json", "\"front_torque_fraction\": 0", "\"front_torque_fraction\": 0.4");
            const auto parameters = fixture.load().player_parameters(VehicleParameters{}, RuntimeVehicleClass::Sedan);
            require(parameters.front_drive_torque_fraction == 0.4f
                && parameters.powertrain->drivetrain.front_torque_fraction == 0.4f,
                "both tire-force allocation and the powertrain model must agree on the selected torque split");
        }},
        {"powertrain snapshot and identity remain immutable", [] {
            Fixture fixture;
            select_powertrain(fixture);
            const auto before = fixture.load();
            fixture.replace("profiles/sedan.json", "\"initial_fuel_l\": 35", "\"initial_fuel_l\": 25");
            const auto after = fixture.load();
            require(before.checksum() != after.checksum(), "fuel/shift setup changes must change catalog identity");
            require(before.player_parameters(VehicleParameters{}, RuntimeVehicleClass::Sedan).powertrain->fuel_tank.initial_fuel_l == 35.f
                && after.player_parameters(VehicleParameters{}, RuntimeVehicleClass::Sedan).powertrain->fuel_tank.initial_fuel_l == 25.f,
                "source edits must not mutate an already loaded runtime snapshot");
        }},
        {"modules apply after legacy mass scaling", [] {
            Fixture fixture;
            select_axle(fixture, "profiles/compact.json", "front", nullptr, "suspension_sedan_comfort");
            const auto parameters = fixture.load().player_parameters(VehicleParameters{}, RuntimeVehicleClass::Compact);
            require(resolved_suspension_parameters(parameters, 0).spring_rate_n_per_m == 27000.f,
                "explicit module spring must not be multiplied by the old compact mass ratio");
            require(resolved_suspension_parameters(parameters, 2).spring_rate_n_per_m
                == parameters.suspension.spring_rate_n_per_m,
                "unselected rear suspension must still use the old compact mass ratio");
        }},
        {"unselected parts included in checksum", [] {
            Fixture fixture;
            const auto before = fixture.load();
            require(before.parts().size() == 9, "tracked runtime catalog must expose five contact and four powertrain modules");
            fixture.replace("parts/tire_sedan_comfort.json", "\"friction_coefficient\": 0.98", "\"friction_coefficient\": 0.97");
            const auto after = fixture.load();
            require(before.checksum() != after.checksum(), "all declared part bytes must participate in catalog identity");
            compare(after.player_parameters(VehicleParameters{}, RuntimeVehicleClass::Sedan), VehicleParameters{},
                "unselected module change");
        }},
        {"legacy manifest without parts", [] {
            Fixture fixture;
            fixture.write("catalog.json", R"({"schema_version":1,"profiles":["profiles/sedan.json","profiles/compact.json","profiles/truck.json","profiles/motorcycle.json"]})");
            const auto legacy = fixture.load();
            require(legacy.parts().empty(), "legacy manifest must not require external parts");
            compare(legacy.player_parameters(VehicleParameters{}, RuntimeVehicleClass::Sedan), VehicleParameters{}, "legacy manifest");
        }},
        {"unknown module reference rejected", [] {
            Fixture fixture;
            select_axle(fixture, "profiles/sedan.json", "front", "missing_tire", nullptr);
            fixture.reject("unknown part");
        }},
        {"wrong module kind rejected", [] {
            Fixture fixture;
            select_axle(fixture, "profiles/sedan.json", "front", "suspension_sedan_standard", nullptr);
            fixture.reject("wrong part kind");
        }},
        {"duplicate part file rejected", [] {
            Fixture fixture;
            fixture.replace("catalog.json", "parts/tire_sedan_rear_standard.json", "parts/tire_sedan_front_standard.json");
            fixture.reject("duplicate catalog file");
        }},
        {"duplicate part ID rejected", [] {
            Fixture fixture;
            fixture.replace("parts/tire_sedan_rear_standard.json", "\"id\": \"tire_sedan_rear_standard\"",
                "\"id\": \"tire_sedan_front_standard\"");
            fixture.reject("duplicate catalog ID");
        }},
        {"strict module selection schema", [] {
            Fixture fixture;
            fixture.replace("profiles/sedan.json", "\"tire\": null", "\"tires\": null");
            fixture.reject("unknown field");
        }},
        {"unsupported runtime part kind rejected", [] {
            Fixture fixture;
            fixture.write("parts/tire_sedan_front_standard.json", R"({"schema_version":1,"id":"brake_test","name":"Unsupported brake","kind":"brake","interface_id":"brake_test","mass":{"mass_kg":1,"center_of_mass_m":[0,0,0],"inertia_diagonal_kg_m2":[1,1,1]},"specification":{"max_torque_nm":100,"handbrake_torque_nm":0}})");
            fixture.reject("unsupported runtime part kind");
        }},
        {"tire static rating checked before paired-slot conversion", [] {
            Fixture fixture;
            select_axle(fixture, "profiles/motorcycle.json", "front", "tire_sedan_front_standard", nullptr);
            fixture.replace("parts/tire_sedan_front_standard.json", "\"rated_load_n\": 7000", "\"rated_load_n\": 700");
            fixture.reject("physical-wheel static load");
        }},
        {"suspension maximum force rating", [] {
            Fixture fixture;
            select_axle(fixture, "profiles/sedan.json", "front", nullptr, "suspension_sedan_standard");
            fixture.replace("parts/suspension_sedan_standard.json", "\"max_force_n\": 12000", "\"max_force_n\": 2000");
            fixture.reject("max_force_n");
        }},
        {"suspension compression capacity", [] {
            Fixture fixture;
            select_axle(fixture, "profiles/sedan.json", "front", nullptr, "suspension_sedan_standard");
            fixture.replace("parts/suspension_sedan_standard.json", "\"max_compression_m\": 0.15", "\"max_compression_m\": 0.01");
            fixture.reject("compression capacity");
        }},
        {"supported tire geometry", [] {
            Fixture fixture;
            select_axle(fixture, "profiles/sedan.json", "front", "tire_sedan_front_standard", nullptr);
            fixture.replace("parts/tire_sedan_front_standard.json", "\"radius_m\": 0.32", "\"radius_m\": 2");
            fixture.reject("supported axle geometry");
        }},
        {"module float underflow rejected", [] {
            Fixture fixture;
            select_axle(fixture, "profiles/sedan.json", "front", "tire_sedan_front_standard", nullptr);
            fixture.replace("parts/tire_sedan_front_standard.json", "\"rolling_resistance_coefficient\": 0.015",
                "\"rolling_resistance_coefficient\": 1e-300");
            fixture.reject("finite float range");
        }},
        {"default immutable snapshot", [] {
            const auto& first = default_runtime_vehicle_catalog();
            require(&first == &default_runtime_vehicle_catalog(), "default catalog must retain one immutable snapshot");
            require(first.at(RuntimeVehicleClass::Unspecified).id == first.at(RuntimeVehicleClass::Sedan).id, "unspecified must alias sedan");
        }},
        {"checksum includes all source bytes", [] {
            const auto baseline = load_runtime_vehicle_catalog(SIMCORE_TEST_RUNTIME_VEHICLE_CATALOG_PATH).checksum();
            Fixture fixture;
            require(fixture.load().checksum() == baseline, "absolute catalog location must not affect checksum");
            fixture.replace("profiles/sedan.json", "\"half_length_m\": 2.2", "\"half_length_m\": 2.21");
            require(fixture.load().checksum() != baseline, "profile bytes must affect checksum");
        }},
        {"manifest checksum and deterministic fleet", [] {
            Fixture fixture;
            const auto before = fixture.load().checksum();
            fixture.replace("catalog.json", "\"profiles/sedan.json\"", "\"profiles/compact.json\"");
            fixture.replace("catalog.json", "\"profiles/compact.json\",\n    \"profiles/compact.json\"",
                "\"profiles/compact.json\",\n    \"profiles/sedan.json\"");
            const auto changed = fixture.load();
            require(changed.checksum() != before, "exact manifest bytes must participate in checksum");
            require(changed.fleet()[0].vehicle_class == RuntimeVehicleClass::Sedan, "manifest order must not reorder fleet");
        }},
        {"unknown override rejected", [] {
            Fixture fixture;
            fixture.replace("profiles/compact.json", "\"mass_kg\": 1050", "\"mass_kkg\": 1050");
            fixture.reject("unknown player override");
        }},
        {"override number type preserved", [] {
            Fixture fixture;
            fixture.replace("profiles/compact.json", "\"mass_kg\": 1050", "\"mass_kg\": \"1050\"");
            fixture.reject("expected number");
        }},
        {"single-track boolean type preserved", [] {
            Fixture fixture;
            fixture.replace("profiles/motorcycle.json", "\"single_track\": true", "\"single_track\": 1");
            fixture.reject("expected boolean");
        }},
        {"invalid effective physics rejected", [] {
            Fixture fixture;
            fixture.replace("profiles/compact.json", "\"mass_kg\": 1050", "\"mass_kg\": -1");
            fixture.reject("invalid player parameters");
        }},
        {"duplicate class rejected", [] {
            Fixture fixture;
            fixture.replace("profiles/compact.json", "\"vehicle_class\": 2", "\"vehicle_class\": 1");
            fixture.reject("duplicate vehicle_class");
        }},
        {"unknown profile field rejected", [] {
            Fixture fixture;
            fixture.replace("profiles/compact.json", "\"scale_suspension_by_mass\": true", "\"scale_suspension_by_mass\": true, \"extra\": 1");
            fixture.reject("unknown field");
        }},
        {"malformed visual rejected", [] {
            Fixture fixture;
            fixture.replace("profiles/sedan.json", "\"driver_scale\": [\n      1,\n      1,\n      1\n    ]",
                "\"driver_scale\": [1, 1]");
            fixture.reject("array shape");
        }},
        {"path traversal rejected", [] {
            Fixture fixture;
            fixture.replace("catalog.json", "profiles/sedan.json", "../sedan.json");
            fixture.reject("traversal");
        }},
        {"invalid baseline rejected", [] {
            VehicleParameters invalid;
            invalid.mass_kg = 0;
            bool rejected = false;
            try { (void)default_runtime_vehicle_catalog().player_parameters(invalid, RuntimeVehicleClass::Sedan); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected, "invalid baseline must be rejected");
        }},
        {"unsupported class rejected", [] {
            bool rejected = false;
            try { (void)default_runtime_vehicle_catalog().at(static_cast<RuntimeVehicleClass>(255)); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected, "unknown class must not silently select sedan");
        }}
    };
}
} // namespace

int main()
{
    std::size_t count = 0;
    for (const auto& [name, test] : tests()) {
        try { test(); ++count; }
        catch (const std::exception& error) {
            std::cerr << "runtime_vehicle_catalog_tests: FAILED " << name << ": " << error.what() << '\n';
            return 1;
        }
    }
    std::cout << "runtime_vehicle_catalog_tests: " << count << " cases passed\n";
    return 0;
}
