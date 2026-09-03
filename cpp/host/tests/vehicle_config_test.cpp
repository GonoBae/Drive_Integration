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

std::string tracked_configuration_bytes()
{
    std::ifstream source(SIMCORE_TEST_VEHICLE_CONFIG_PATH, std::ios::binary);
    require(source.good(), "tracked vehicle configuration must be readable");
    return {std::istreambuf_iterator<char>(source), std::istreambuf_iterator<char>()};
}

std::size_t parameter_offset(const std::string& bytes, const std::string& key)
{
    const std::string prefix = key + "=";
    if (bytes.starts_with(prefix)) {
        return 0;
    }
    const auto offset = bytes.find("\n" + prefix);
    require(offset != std::string::npos,
            "test fixture must contain the exact parameter to replace");
    return offset + 1;
}

std::string with_parameter_value(
    std::string bytes, const std::string& key, const std::string& value)
{
    const auto offset = parameter_offset(bytes, key);
    const auto value_offset = offset + key.size() + 1;
    const auto end = bytes.find_first_of("\r\n", value_offset);
    bytes.replace(value_offset,
                  (end == std::string::npos ? bytes.size() : end) - value_offset,
                  value);
    return bytes;
}

std::string without_parameter(std::string bytes, const std::string& key)
{
    const auto offset = parameter_offset(bytes, key);
    const auto end = bytes.find('\n', offset);
    bytes.erase(offset, (end == std::string::npos ? bytes.size() : end + 1) - offset);
    return bytes;
}

simcore_host::LoadedVehicleParameters load_test_configuration(const std::string& bytes)
{
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto temp_path = std::filesystem::temp_directory_path()
        / ("simcore-invalid-vehicle-" + unique + ".cfg");
    {
        std::ofstream output(temp_path, std::ios::binary);
        output << bytes;
        require(output.good(), "invalid vehicle test fixture must be writable");
    }

    try {
        const auto loaded = simcore_host::load_vehicle_parameters(temp_path);
        std::error_code ignored;
        std::filesystem::remove(temp_path, ignored);
        return loaded;
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temp_path, ignored);
        throw;
    }
}

std::string configuration_rejection(const std::string& bytes)
{
    try {
        (void)load_test_configuration(bytes);
    } catch (const std::runtime_error& error) {
        return error.what();
    }
    return {};
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
    require(loaded.parameters.max_steering_angle_rad == 0.61086524f
            && loaded.parameters.steering_rate_rad_s == 1.80f
            && loaded.parameters.steering_return_rate_rad_s == 2.20f,
            "tracked speed-independent steering rack parameters must load");
    require(simcore_host::kVehicleConfigFormatVersion == 7,
            "independent front/rear tire stiffness must use vehicle schema version 7");
    require(loaded.parameters.front_tire_corner_stiffness_n_rad == 60000.f
                && loaded.parameters.rear_tire_corner_stiffness_n_rad == 50000.f,
            "tracked mild-understeer front/rear per-tire stiffnesses must load");
    require(loaded.parameters.lateral_grip_priority == 0.95f
            && loaded.parameters.traction_control_slip_target == 0.08f
            && loaded.parameters.traction_control_full_cut_slip == 0.12f,
            "tracked lateral-priority traction control values must load");
    require(loaded.parameters.low_speed_slip_reference_mps == 1.5f,
            "tracked low-speed slip regularization must load");
    require(loaded.parameters.surface_default_friction_scale == 1.f
            && loaded.parameters.surface_asphalt_friction_scale == 1.f
            && loaded.parameters.surface_low_friction_scale == 1.f
            && loaded.parameters.surface_rough_friction_scale == 1.f,
            "tracked vehicle material profile scales must load");
    require(loaded.checksum.starts_with("fnv1a64:")
            && loaded.checksum.size() == 24,
            "vehicle configuration must expose a stable checksum");
    require(valid_vehicle_parameters(loaded.parameters),
            "tracked vehicle configuration must pass physical validation");
}

void test_unknown_parameter_is_rejected()
{
    const auto rejection = configuration_rejection(
        tracked_configuration_bytes() + "\nmisspelled_mass=1500\n");
    require(rejection.find("Unknown vehicle parameter 'misspelled_mass'")
                != std::string::npos,
            "unknown vehicle parameters must fail closed");
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
    parameters.steering_return_rate_rad_s = 0.f;
    require(!valid_vehicle_parameters(parameters),
            "zero steering return rate must fail physical validation");

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
    parameters.surface_low_friction_scale = 0.f;
    require(!valid_vehicle_parameters(parameters),
            "zero surface friction profile scale must fail validation");

    parameters = valid;
    parameters.surface_rough_friction_scale = 4.01f;
    require(!valid_vehicle_parameters(parameters),
            "an excessive surface friction profile scale must fail validation");

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
    for (const auto* version : {"4", "5", "6"}) {
        auto bytes = with_parameter_value(
            tracked_configuration_bytes(), "format_version", version);
        bytes = without_parameter(bytes, "front_tire_corner_stiffness_n_rad");
        bytes = without_parameter(bytes, "rear_tire_corner_stiffness_n_rad");
        bytes += "\ntire_corner_stiffness_n_rad=55000\n";
        if (std::string(version) != "6") {
            bytes += "comfortable_lateral_accel_mps2=8.5\n";
        }
        const auto rejection = configuration_rejection(bytes);
        require(rejection.find("Unsupported vehicle configuration format_version")
                    != std::string::npos,
                "legacy schema must be rejected before its removed key is checked");
        if (std::string(version) == "5" || std::string(version) == "6") {
            require(rejection.find("front_tire_corner_stiffness_n_rad") != std::string::npos
                        && rejection.find("rear_tire_corner_stiffness_n_rad") != std::string::npos
                        && rejection.find("copying the old per-tire value unchanged into both fields")
                            != std::string::npos
                        && rejection.find("set format_version=7") != std::string::npos,
                    "v5/v6 rejection must explain value-preserving stiffness migration");
        }
        if (std::string(version) == "5") {
            require(rejection.find("remove comfortable_lateral_accel_mps2") != std::string::npos,
                    "v5 rejection must explain the explicit steering migration");
        } else if (std::string(version) == "6") {
            require(rejection.find("comfortable_lateral_accel_mps2") == std::string::npos,
                    "v6 migration must not request removal of an already absent v5 parameter");
        }
    }
}

void test_removed_steering_assist_key_is_rejected()
{
    const auto rejection = configuration_rejection(
        tracked_configuration_bytes() + "\ncomfortable_lateral_accel_mps2=8.5\n");
    require(rejection.find("Unknown vehicle parameter 'comfortable_lateral_accel_mps2'")
                != std::string::npos,
            "v7 must not silently retain or ignore the removed steering assist");
}

void test_schema_version_validation_remains_strict()
{
    for (const auto* version : {"0", "8", "7.0", "7.1", "7.00000001", "7e0",
                                "nan", "inf", "7junk"}) {
        require(!configuration_rejection(with_parameter_value(
                    tracked_configuration_bytes(), "format_version", version)).empty(),
                "schema version must be an exact supported integer token");
    }
    auto without_version = tracked_configuration_bytes();
    const auto version_offset = without_version.find("format_version=7");
    require(version_offset != std::string::npos,
            "tracked configuration must declare version 7");
    without_version.erase(version_offset, std::string("format_version=7").size());
    require(configuration_rejection(without_version)
                == "Missing vehicle parameter 'format_version'",
            "missing schema version must not fall back to the compiled schema");
    require(configuration_rejection(
                tracked_configuration_bytes() + "\nformat_version=7\n")
                == "Duplicate vehicle parameter 'format_version'",
            "duplicate schema versions must fail closed");
}

void test_steering_fields_remain_required_and_finite()
{
    auto bytes = tracked_configuration_bytes();
    const std::string field = "steering_rate_rad_s=1.80";
    const auto offset = bytes.find(field);
    require(offset != std::string::npos, "tracked config must declare rack response");
    bytes.erase(offset, field.size());
    require(configuration_rejection(bytes)
                == "Missing vehicle parameter 'steering_rate_rad_s'",
            "v7 must require the physical steering rack response");
    for (const auto* key : {"max_steering_angle_rad", "steering_rate_rad_s",
                            "steering_return_rate_rad_s"}) {
        require(!configuration_rejection(with_parameter_value(
                    tracked_configuration_bytes(), key, "nan")).empty(),
                "v7 steering fields must reject non-finite values");
    }
}

void test_physical_steering_is_not_coupled_to_a_grip_envelope()
{
    auto parameters = simcore_host::load_vehicle_parameters(
        SIMCORE_TEST_VEHICLE_CONFIG_PATH).parameters;
    parameters.tire_friction = 0.10f;
    require(valid_vehicle_parameters(parameters),
            "valid low-grip tires must not be rejected by a steering-assist envelope");
    require(parameters.max_steering_angle_rad == 0.61086524f,
            "a low-grip profile must retain the mechanical steering lock");
}

void test_axle_specific_tire_stiffnesses_load_independently()
{
    auto bytes = with_parameter_value(tracked_configuration_bytes(),
        "front_tire_corner_stiffness_n_rad", "57500");
    bytes = with_parameter_value(bytes, "rear_tire_corner_stiffness_n_rad", "48500");
    const auto loaded = load_test_configuration(bytes);
    require(loaded.parameters.front_tire_corner_stiffness_n_rad == 57500.f
                && loaded.parameters.rear_tire_corner_stiffness_n_rad == 48500.f,
            "front and rear tire stiffnesses must load without copying or averaging");
    require(loaded.parameters.tire_friction == 1.05f
                && loaded.parameters.max_steering_angle_rad == 0.61086524f,
            "stiffness calibration must not alter friction or mechanical steering lock");
}

void test_axle_specific_tire_stiffness_schema_is_strict()
{
    require(configuration_rejection(tracked_configuration_bytes()
                + "\ntire_corner_stiffness_n_rad=55000\n")
                == "Unknown vehicle parameter 'tire_corner_stiffness_n_rad'",
            "v7 must reject the legacy shared stiffness even when both new fields exist");
    for (const auto* key : {"front_tire_corner_stiffness_n_rad",
                            "rear_tire_corner_stiffness_n_rad"}) {
        require(configuration_rejection(without_parameter(tracked_configuration_bytes(), key))
                    == std::string("Missing vehicle parameter '") + key + "'",
                "both independent tire stiffness fields are mandatory");
        require(configuration_rejection(tracked_configuration_bytes()
                    + "\n" + key + "=55000\n")
                    == std::string("Duplicate vehicle parameter '") + key + "'",
                "duplicate tire stiffness must fail closed");
        for (const auto* value : {"0", "-1", "nan", "inf"}) {
            require(!configuration_rejection(with_parameter_value(
                        tracked_configuration_bytes(), key, value)).empty(),
                    "each axle tire stiffness must be positive and finite");
        }
    }
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
        test_removed_steering_assist_key_is_rejected();
        test_schema_version_validation_remains_strict();
        test_steering_fields_remain_required_and_finite();
        test_physical_steering_is_not_coupled_to_a_grip_envelope();
        test_axle_specific_tire_stiffnesses_load_independently();
        test_axle_specific_tire_stiffness_schema_is_strict();
        std::cout << "vehicle_config_tests: all tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "vehicle_config_tests: " << error.what() << '\n';
        return 1;
    }
}
