#include "physics/vehicle_config.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace simcore_host {
namespace {

std::string trim(std::string_view value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(first, last - first + 1));
}

float parse_finite_float(const std::string& key, const std::string& text)
{
    errno = 0;
    char* end = nullptr;
    const float value = std::strtof(text.c_str(), &end);
    if (errno == ERANGE || end == text.c_str() || *end != '\0'
        || !std::isfinite(value)) {
        throw std::runtime_error(
            "Invalid finite float for vehicle parameter '" + key + "': " + text);
    }
    return value;
}

std::string fnv1a_checksum(const std::string& bytes)
{
    std::uint64_t hash = 14695981039346656037ull;
    for (const unsigned char byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    std::ostringstream output;
    output << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << hash;
    return output.str();
}

} // namespace

LoadedVehicleParameters load_vehicle_parameters(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error(
            "Vehicle configuration not found: " + path.string());
    }

    const std::string bytes{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    std::istringstream lines(bytes);
    std::unordered_map<std::string, std::string> values;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(lines, line)) {
        ++line_number;
        const auto comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            throw std::runtime_error(
                "Vehicle configuration line " + std::to_string(line_number)
                + " must use key=value");
        }
        const std::string key = trim(std::string_view(line).substr(0, separator));
        const std::string value = trim(std::string_view(line).substr(separator + 1));
        if (key.empty() || value.empty()) {
            throw std::runtime_error(
                "Vehicle configuration line " + std::to_string(line_number)
                + " has an empty key or value");
        }
        if (!values.emplace(key, value).second) {
            throw std::runtime_error(
                "Duplicate vehicle parameter '" + key + "'");
        }
    }

    const std::unordered_set<std::string> expected_keys{
        "format_version",
        "mass_kg",
        "wheelbase_m",
        "max_steering_angle_rad",
        "steering_rate_rad_s",
        "steering_return_rate_rad_s",
        "comfortable_lateral_accel_mps2",
        "max_drive_force_n",
        "max_reverse_force_n",
        "max_drive_power_w",
        "drive_force_rise_rate_n_per_s",
        "drive_force_fall_rate_n_per_s",
        "max_service_brake_n",
        "max_handbrake_force_n",
        "rolling_resistance_coeff",
        "drivetrain_drag_n_per_mps",
        "drag_coefficient",
        "frontal_area_m2",
        "air_density_kg_m3",
        "tire_radius_m",
        "final_drive_ratio",
        "drive_gear_ratio",
        "reverse_gear_ratio",
        "max_forward_speed_mps",
        "max_reverse_speed_mps",
        "idle_rpm",
        "max_rpm",
        "fuel_rate_percent_s",
        "front_track_m",
        "rear_track_m",
        "cg_height_m",
        "front_static_load_fraction",
        "front_drive_torque_fraction",
        "front_service_brake_fraction",
        "yaw_inertia_kg_m2",
        "pitch_inertia_kg_m2",
        "roll_inertia_kg_m2",
        "attitude_spring_n_m_rad",
        "attitude_damping_n_m_s_rad",
        "tire_corner_stiffness_n_rad",
        "tire_longitudinal_stiffness_n",
        "tire_friction",
        "lateral_grip_priority",
        "traction_control_slip_target",
        "traction_control_full_cut_slip",
        "wheel_inertia_kg_m2",
        "wheel_free_spin_damping_n_m_s",
        "low_speed_slip_reference_mps",
        "suspension.rest_length_m",
        "suspension.max_compression_m",
        "suspension.max_extension_m",
        "suspension.spring_rate_n_per_m",
        "suspension.damper_rate_n_s_per_m",
        "suspension.max_force_n",
    };

    for (const auto& [key, value] : values) {
        if (!expected_keys.contains(key)) {
            throw std::runtime_error("Unknown vehicle parameter '" + key + "'");
        }
    }
    for (const auto& key : expected_keys) {
        if (!values.contains(key)) {
            throw std::runtime_error("Missing vehicle parameter '" + key + "'");
        }
    }

    const float format_version = parse_finite_float(
        "format_version", values.at("format_version"));
    if (format_version != static_cast<float>(kVehicleConfigFormatVersion)) {
        throw std::runtime_error(
            "Unsupported vehicle configuration format_version: "
            + values.at("format_version"));
    }

    VehicleParameters parameters;
#define SIMCORE_LOAD_PARAMETER(field) \
    parameters.field = parse_finite_float(#field, values.at(#field))
    SIMCORE_LOAD_PARAMETER(mass_kg);
    SIMCORE_LOAD_PARAMETER(wheelbase_m);
    SIMCORE_LOAD_PARAMETER(max_steering_angle_rad);
    SIMCORE_LOAD_PARAMETER(steering_rate_rad_s);
    SIMCORE_LOAD_PARAMETER(steering_return_rate_rad_s);
    SIMCORE_LOAD_PARAMETER(comfortable_lateral_accel_mps2);
    SIMCORE_LOAD_PARAMETER(max_drive_force_n);
    SIMCORE_LOAD_PARAMETER(max_reverse_force_n);
    SIMCORE_LOAD_PARAMETER(max_drive_power_w);
    SIMCORE_LOAD_PARAMETER(drive_force_rise_rate_n_per_s);
    SIMCORE_LOAD_PARAMETER(drive_force_fall_rate_n_per_s);
    SIMCORE_LOAD_PARAMETER(max_service_brake_n);
    SIMCORE_LOAD_PARAMETER(max_handbrake_force_n);
    SIMCORE_LOAD_PARAMETER(rolling_resistance_coeff);
    SIMCORE_LOAD_PARAMETER(drivetrain_drag_n_per_mps);
    SIMCORE_LOAD_PARAMETER(drag_coefficient);
    SIMCORE_LOAD_PARAMETER(frontal_area_m2);
    SIMCORE_LOAD_PARAMETER(air_density_kg_m3);
    SIMCORE_LOAD_PARAMETER(tire_radius_m);
    SIMCORE_LOAD_PARAMETER(final_drive_ratio);
    SIMCORE_LOAD_PARAMETER(drive_gear_ratio);
    SIMCORE_LOAD_PARAMETER(reverse_gear_ratio);
    SIMCORE_LOAD_PARAMETER(max_forward_speed_mps);
    SIMCORE_LOAD_PARAMETER(max_reverse_speed_mps);
    SIMCORE_LOAD_PARAMETER(idle_rpm);
    SIMCORE_LOAD_PARAMETER(max_rpm);
    SIMCORE_LOAD_PARAMETER(fuel_rate_percent_s);
    SIMCORE_LOAD_PARAMETER(front_track_m);
    SIMCORE_LOAD_PARAMETER(rear_track_m);
    SIMCORE_LOAD_PARAMETER(cg_height_m);
    SIMCORE_LOAD_PARAMETER(front_static_load_fraction);
    SIMCORE_LOAD_PARAMETER(front_drive_torque_fraction);
    SIMCORE_LOAD_PARAMETER(front_service_brake_fraction);
    SIMCORE_LOAD_PARAMETER(yaw_inertia_kg_m2);
    SIMCORE_LOAD_PARAMETER(pitch_inertia_kg_m2);
    SIMCORE_LOAD_PARAMETER(roll_inertia_kg_m2);
    SIMCORE_LOAD_PARAMETER(attitude_spring_n_m_rad);
    SIMCORE_LOAD_PARAMETER(attitude_damping_n_m_s_rad);
    SIMCORE_LOAD_PARAMETER(tire_corner_stiffness_n_rad);
    SIMCORE_LOAD_PARAMETER(tire_longitudinal_stiffness_n);
    SIMCORE_LOAD_PARAMETER(tire_friction);
    SIMCORE_LOAD_PARAMETER(lateral_grip_priority);
    SIMCORE_LOAD_PARAMETER(traction_control_slip_target);
    SIMCORE_LOAD_PARAMETER(traction_control_full_cut_slip);
    SIMCORE_LOAD_PARAMETER(wheel_inertia_kg_m2);
    SIMCORE_LOAD_PARAMETER(wheel_free_spin_damping_n_m_s);
    SIMCORE_LOAD_PARAMETER(low_speed_slip_reference_mps);
#undef SIMCORE_LOAD_PARAMETER

    parameters.suspension.rest_length_m = parse_finite_float(
        "suspension.rest_length_m", values.at("suspension.rest_length_m"));
    parameters.suspension.max_compression_m = parse_finite_float(
        "suspension.max_compression_m", values.at("suspension.max_compression_m"));
    parameters.suspension.max_extension_m = parse_finite_float(
        "suspension.max_extension_m", values.at("suspension.max_extension_m"));
    parameters.suspension.spring_rate_n_per_m = parse_finite_float(
        "suspension.spring_rate_n_per_m", values.at("suspension.spring_rate_n_per_m"));
    parameters.suspension.damper_rate_n_s_per_m = parse_finite_float(
        "suspension.damper_rate_n_s_per_m", values.at("suspension.damper_rate_n_s_per_m"));
    parameters.suspension.max_force_n = parse_finite_float(
        "suspension.max_force_n", values.at("suspension.max_force_n"));

    if (!valid_vehicle_parameters(parameters)) {
        throw std::runtime_error(
            "Vehicle configuration contains an invalid physical parameter set");
    }

    return {parameters, std::filesystem::absolute(path), fnv1a_checksum(bytes)};
}

} // namespace simcore_host
