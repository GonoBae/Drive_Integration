#pragma once

#include "physics/vehicle_parameters.hpp"
#include "protocol/vehicle_messages.hpp"
#include "vehicle_catalog/vehicle_catalog.hpp"

#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace simcore_host {

struct RuntimeNpcProfile {
    std::string name;
    double half_length_m, half_width_m, half_height_m;
    double mass_kg, yaw_inertia_kg_m2, maximum_reaction_speed_mps;
    double speed_scale, acceleration_mps2, braking_mps2;
    double tumble_contact_below_cg_m, ground_clearance_m, minimum_turn_radius_m;
};

struct RuntimeAxleModuleSelection {
    std::optional<std::string> tire;
    std::optional<std::string> suspension;
};

struct RuntimePowertrainModuleSelection {
    std::string engine, transmission, drivetrain, fuel_tank;
    double initial_fuel_l, upshift_rpm, downshift_rpm, shift_duration_s;
};

struct RuntimeVehicleProfile {
    std::string id;
    RuntimeVehicleClass vehicle_class;
    std::map<std::string, float> player_overrides;
    std::optional<bool> single_track_override;
    bool scale_suspension_by_mass;
    std::optional<float> rest_length_cg_tire_offset_m;
    RuntimeNpcProfile npc;
    RuntimeAxleModuleSelection front_axle_modules, rear_axle_modules;
    std::optional<RuntimePowertrainModuleSelection> powertrain_modules;
};

struct RuntimeVehicleLoadout {
    std::string id, name;
    RuntimeVehicleClass vehicle_class;
    RuntimeAxleModuleSelection front_axle_modules, rear_axle_modules;
    std::optional<RuntimePowertrainModuleSelection> powertrain_modules;
};

class RuntimeVehicleCatalog {
public:
    [[nodiscard]] const RuntimeVehicleProfile& at(RuntimeVehicleClass vehicle_class) const;
    [[nodiscard]] const std::array<RuntimeVehicleProfile, 4>& fleet() const { return profiles_; }
    [[nodiscard]] const std::string& checksum() const { return checksum_; }
    [[nodiscard]] const std::map<std::string, vehicle_catalog::PartDefinition>& parts() const { return parts_; }
    [[nodiscard]] const std::map<std::string, RuntimeVehicleLoadout>& loadouts() const { return loadouts_; }
    [[nodiscard]] RuntimeVehicleLoadout loadout(std::string_view id, RuntimeVehicleClass vehicle_class) const;
    // Session-local encoding tied to this catalog's checksum. Slot order:
    // front tire/suspension, rear tire/suspension, engine/transmission/drivetrain/tank.
    [[nodiscard]] std::string custom_loadout_id(std::string_view base_id,
        const std::array<std::optional<std::string>, 8>& parts) const;
    [[nodiscard]] VehicleParameters player_parameters(
        const VehicleParameters& sedan, RuntimeVehicleClass vehicle_class, std::string_view loadout_id = {}) const;

private:
    RuntimeVehicleCatalog() = default;
    std::array<RuntimeVehicleProfile, 4> profiles_;
    std::map<std::string, vehicle_catalog::PartDefinition> parts_;
    std::map<std::string, RuntimeVehicleLoadout> loadouts_;
    std::string checksum_;
    friend RuntimeVehicleCatalog load_runtime_vehicle_catalog(const std::filesystem::path& manifest);
};

[[nodiscard]] RuntimeVehicleCatalog load_runtime_vehicle_catalog(const std::filesystem::path& manifest);
[[nodiscard]] const RuntimeVehicleCatalog& default_runtime_vehicle_catalog();

} // namespace simcore_host
