#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace simcore_host::vehicle_catalog {

inline constexpr int kSchemaVersion = 1;
using Vec3 = std::array<double, 3>;

// FLU, metres, kilograms. Component axes are parallel to chassis axes in v1.
// Chassis mass excludes every installed module, fuel, occupants and payload.
struct MassProperties {
    double mass_kg;
    Vec3 center_of_mass_m;
    Vec3 inertia_diagonal_kg_m2; // About the component's own centre of mass.
};

struct TorquePoint { double rpm; double torque_nm; };
struct Engine {
    double idle_rpm, max_rpm, rotational_inertia_kg_m2, response_time_s;
    double idle_fuel_lph, bsfc_g_per_kwh;
    std::string fuel_type;
    std::vector<TorquePoint> torque_curve;
};
struct Transmission {
    std::vector<double> forward_ratios;
    double reverse_ratio, max_input_torque_nm, efficiency;
};
struct Drivetrain { double final_drive_ratio, front_torque_fraction, efficiency; };
struct Wheel { double rim_diameter_m, rim_width_m, rotational_inertia_kg_m2; };
struct Tire {
    double radius_m, width_m, rim_diameter_m, min_rim_width_m, max_rim_width_m;
    double friction_coefficient, longitudinal_stiffness_n, cornering_stiffness_n_rad;
    double rolling_resistance_coefficient, rated_load_n;
};
struct Suspension {
    double rest_length_m, max_compression_m, max_extension_m;
    double spring_rate_n_per_m, damper_rate_n_s_per_m, max_force_n;
};
struct Brake { double max_torque_nm, handbrake_torque_nm; };
struct Steering { double max_angle_rad, rate_rad_s, return_rate_rad_s; };
struct FuelTank { double capacity_l, fuel_density_kg_l; std::string fuel_type; };
using PartSpec = std::variant<Engine, Transmission, Drivetrain, Wheel, Tire,
                              Suspension, Brake, Steering, FuelTank>;

struct PartDefinition {
    std::string id, name, interface_id;
    MassProperties mass;
    PartSpec specification;
};
struct Mount { std::string interface_id; Vec3 position_m; };
struct AxleDefinition {
    double x_m, track_m, wheel_center_z_m;
    double min_tire_radius_m, max_tire_radius_m, max_tire_width_m;
    std::string wheel_interface, suspension_interface, brake_interface;
};
struct VehicleDefinition {
    std::string id, name, category, dynamics_model;
    MassProperties chassis_mass;
    Vec3 body_dimensions_m; // length, width, height; not a collision box contract.
    double drag_coefficient, frontal_area_m2;
    Mount engine, transmission, drivetrain, steering, fuel_tank;
    AxleDefinition front_axle, rear_axle;
};
struct AxleAssembly { std::string wheel, tire, suspension, brake; };
struct AssemblyDefinition {
    std::string id, name, vehicle_id;
    std::string engine, transmission, drivetrain, steering, fuel_tank;
    AxleAssembly front_axle, rear_axle;
};
struct ResolvedAxle {
    unsigned wheel_count;
    AxleDefinition geometry;
    Wheel wheel;
    Tire tire;
    Suspension suspension;
    Brake brake;
};
struct AssembledMass {
    double dry_mass_kg;
    Vec3 center_of_mass_m;
    // Row-major full tensor about the assembled CG, including parallel-axis terms.
    std::array<double, 9> inertia_tensor_kg_m2;
};
struct ResolvedAssembly {
    std::string id, vehicle_id, catalog_checksum, category, dynamics_model;
    AssembledMass mass;
    Vec3 body_dimensions_m;
    double wheelbase_m, front_static_load_fraction, drag_coefficient, frontal_area_m2;
    double peak_engine_torque_nm, peak_engine_power_w;
    Engine engine;
    Transmission transmission;
    Drivetrain drivetrain;
    Steering steering;
    FuelTank fuel_tank;
    ResolvedAxle front_axle, rear_axle;
};

class Catalog {
public:
    [[nodiscard]] const auto& vehicles() const { return vehicles_; }
    [[nodiscard]] const auto& parts() const { return parts_; }
    [[nodiscard]] const auto& assemblies() const { return assemblies_; }
    [[nodiscard]] const std::string& checksum() const { return checksum_; }
    [[nodiscard]] ResolvedAssembly resolve(const std::string& assembly_id) const;

private:
    std::map<std::string, VehicleDefinition> vehicles_;
    std::map<std::string, PartDefinition> parts_;
    std::map<std::string, AssemblyDefinition> assemblies_;
    std::string checksum_;
    friend Catalog load_catalog(const std::filesystem::path& manifest);
};

// Returns a complete validated snapshot or throws; no partially published catalog.
// This v1 authoring API deliberately does not replace live VehicleParameters yet.
[[nodiscard]] Catalog load_catalog(const std::filesystem::path& manifest);

// Parses the same strict part schema from bytes already included in a caller's
// immutable catalog snapshot; no second filesystem read is performed.
[[nodiscard]] PartDefinition parse_part_definition(std::string_view bytes, const std::string& context);

} // namespace simcore_host::vehicle_catalog
