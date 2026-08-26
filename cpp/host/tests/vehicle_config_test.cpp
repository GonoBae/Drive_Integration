#include "physics/vehicle_config.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#ifndef SIMCORE_TEST_VEHICLE_CONFIG_PATH
#error SIMCORE_TEST_VEHICLE_CONFIG_PATH must identify the tracked R1 vehicle config
#endif

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_tracked_configuration_is_complete_and_valid()
{
    const auto loaded = simcore_host::load_vehicle_parameters(
        SIMCORE_TEST_VEHICLE_CONFIG_PATH);
    require(loaded.parameters.mass_kg == 1500.f,
            "tracked vehicle mass must load from the external file");
    require(loaded.parameters.suspension.rest_length_m == 0.35f,
            "tracked suspension values must load from the external file");
    require(loaded.parameters.front_static_load_fraction == 0.55f,
            "tracked static front load fraction must load from the external file");
    require(loaded.parameters.front_drive_torque_fraction == 0.f,
            "tracked drivetrain configuration must remain rear-wheel drive");
    require(loaded.parameters.front_service_brake_fraction == 0.65f,
            "tracked front service-brake bias must load from the external file");
    require(loaded.parameters.max_drive_force_n == 6000.f
            && loaded.parameters.max_drive_power_w == 100000.f,
            "tracked sedan drive force and power limits must load");
    require(loaded.parameters.drive_force_rise_rate_n_per_s == 18000.f
            && loaded.parameters.drive_force_fall_rate_n_per_s == 36000.f,
            "tracked powertrain response rates must load");
    require(loaded.parameters.lateral_grip_priority == 0.95f
            && loaded.parameters.traction_control_slip_target == 0.08f
            && loaded.parameters.traction_control_full_cut_slip == 0.12f,
            "tracked lateral-priority traction control values must load");
    require(loaded.parameters.low_speed_slip_reference_mps == 1.5f,
            "tracked low-speed slip regularization must load");
    require(loaded.checksum.starts_with("fnv1a64:")
            && loaded.checksum.size() == 24,
            "vehicle configuration must expose a stable checksum");
    require(valid_vehicle_parameters(loaded.parameters),
            "tracked vehicle configuration must pass physical validation");
}

void test_unknown_parameter_is_rejected()
{
    const auto source_path = std::filesystem::path(
        SIMCORE_TEST_VEHICLE_CONFIG_PATH);
    std::ifstream source(source_path, std::ios::binary);
    const std::string bytes{
        std::istreambuf_iterator<char>(source), std::istreambuf_iterator<char>()};
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto temp_path = std::filesystem::temp_directory_path()
        / ("simcore-invalid-vehicle-" + unique + ".cfg");
    {
        std::ofstream output(temp_path, std::ios::binary);
        output << bytes << "\nmisspelled_mass=1500\n";
    }

    bool rejected = false;
    try {
        (void)simcore_host::load_vehicle_parameters(temp_path);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    std::error_code ignored;
    std::filesystem::remove(temp_path, ignored);
    require(rejected, "unknown vehicle parameters must fail closed");
}

void test_invalid_physical_value_is_rejected()
{
    auto parameters = simcore_host::load_vehicle_parameters(
        SIMCORE_TEST_VEHICLE_CONFIG_PATH).parameters;
    parameters.mass_kg = -1.f;
    require(!valid_vehicle_parameters(parameters),
            "negative mass must fail physical validation");
}

void test_invalid_fraction_values_are_rejected()
{
    const auto valid = simcore_host::load_vehicle_parameters(
        SIMCORE_TEST_VEHICLE_CONFIG_PATH).parameters;

    auto parameters = valid;
    parameters.front_static_load_fraction = 0.f;
    require(!valid_vehicle_parameters(parameters),
            "zero static front load fraction must fail physical validation");
    parameters = valid;
    parameters.front_static_load_fraction = 1.f;
    require(!valid_vehicle_parameters(parameters),
            "unit static front load fraction must fail physical validation");

    parameters = valid;
    parameters.front_drive_torque_fraction = -0.01f;
    require(!valid_vehicle_parameters(parameters),
            "negative front drive torque fraction must fail physical validation");
    parameters = valid;
    parameters.front_drive_torque_fraction = 1.01f;
    require(!valid_vehicle_parameters(parameters),
            "front drive torque fraction above one must fail physical validation");

    parameters = valid;
    parameters.front_service_brake_fraction = -0.01f;
    require(!valid_vehicle_parameters(parameters),
            "negative front service-brake fraction must fail physical validation");
    parameters = valid;
    parameters.front_service_brake_fraction = 1.01f;
    require(!valid_vehicle_parameters(parameters),
            "front service-brake fraction above one must fail physical validation");
}

void test_invalid_driver_response_values_are_rejected()
{
    const auto valid = simcore_host::load_vehicle_parameters(
        SIMCORE_TEST_VEHICLE_CONFIG_PATH).parameters;

    auto parameters = valid;
    parameters.steering_rate_rad_s = 0.f;
    require(!valid_vehicle_parameters(parameters),
            "zero steering rate must fail physical validation");

    parameters = valid;
    parameters.max_drive_power_w = 0.f;
    require(!valid_vehicle_parameters(parameters),
            "zero driveline power must fail physical validation");

    parameters = valid;
    parameters.drive_force_rise_rate_n_per_s = 0.f;
    require(!valid_vehicle_parameters(parameters),
            "zero powertrain force rise rate must fail physical validation");

    parameters = valid;
    parameters.lateral_grip_priority = 1.01f;
    require(!valid_vehicle_parameters(parameters),
            "lateral grip priority above one must fail physical validation");

    parameters = valid;
    parameters.traction_control_full_cut_slip =
        parameters.traction_control_slip_target;
    require(!valid_vehicle_parameters(parameters),
            "traction-control full-cut slip must exceed its target");

    parameters = valid;
    parameters.drivetrain_drag_n_per_mps = -1.f;
    require(!valid_vehicle_parameters(parameters),
            "negative driveline drag must fail physical validation");

    parameters = valid;
    parameters.low_speed_slip_reference_mps = 0.f;
    require(!valid_vehicle_parameters(parameters),
            "zero low-speed slip reference must fail physical validation");

    parameters = valid;
    parameters.attitude_spring_n_m_rad = 1.f;
    require(!valid_vehicle_parameters(parameters),
            "a synthetic body-to-ground attitude spring must be rejected");

    parameters = valid;
    parameters.attitude_damping_n_m_s_rad = 1.f;
    require(!valid_vehicle_parameters(parameters),
            "synthetic attitude damping must be rejected");
}

void test_legacy_format_version_is_rejected()
{
    const auto source_path = std::filesystem::path(
        SIMCORE_TEST_VEHICLE_CONFIG_PATH);
    std::ifstream source(source_path, std::ios::binary);
    std::string bytes{
        std::istreambuf_iterator<char>(source), std::istreambuf_iterator<char>()};
    const std::string current_version = "format_version=4";
    const auto version_offset = bytes.find(current_version);
    require(version_offset != std::string::npos,
            "tracked configuration must declare format version 4");
    bytes.replace(version_offset, current_version.size(), "format_version=3");

    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto temp_path = std::filesystem::temp_directory_path()
        / ("simcore-legacy-vehicle-" + unique + ".cfg");
    {
        std::ofstream output(temp_path, std::ios::binary);
        output << bytes;
    }

    bool rejected = false;
    try {
        (void)simcore_host::load_vehicle_parameters(temp_path);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    std::error_code ignored;
    std::filesystem::remove(temp_path, ignored);
    require(rejected, "legacy vehicle configuration format must fail closed");
}

} // namespace

int main()
{
    try {
        test_tracked_configuration_is_complete_and_valid();
        test_unknown_parameter_is_rejected();
        test_invalid_physical_value_is_rejected();
        test_invalid_fraction_values_are_rejected();
        test_invalid_driver_response_values_are_rejected();
        test_legacy_format_version_is_rejected();
        std::cout << "vehicle_config_tests: all tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "vehicle_config_tests: " << error.what() << '\n';
        return 1;
    }
}
