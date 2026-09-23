#include "vehicle_catalog/runtime_vehicle_catalog.hpp"
#include "vehicle_catalog/json_document.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iterator>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

#ifndef SIMCORE_DEFAULT_RUNTIME_VEHICLE_CATALOG_PATH
#error SIMCORE_DEFAULT_RUNTIME_VEHICLE_CATALOG_PATH must identify the shared runtime catalog
#endif

namespace simcore_host {
namespace {

using Value = vehicle_catalog::json::Value;
constexpr std::string_view custom_loadout_prefix = "parts_v1_";
using ModuleIds = std::array<std::optional<std::string>, 8>;

class Node {
public:
    Node(const Value& value, std::string path) : value_(value), path_(std::move(path)) {}
    [[noreturn]] void fail(const std::string& reason) const { throw std::runtime_error(path_ + ": " + reason); }
    const auto& object() const {
        if (value_.kind_case() != Value::kStructValue) fail("expected object");
        return value_.struct_value().fields();
    }
    void fields(std::initializer_list<std::string_view> expected,
                std::initializer_list<std::string_view> optional = {}) const {
        const auto& entries = object();
        for (const auto& [key, value] : entries) {
            if (std::find(expected.begin(), expected.end(), key) == expected.end()
                && std::find(optional.begin(), optional.end(), key) == optional.end()) fail("unknown field '" + key + "'");
        }
        for (const auto key : expected) {
            if (!entries.contains(std::string(key))) fail("missing field '" + std::string(key) + "'");
        }
    }
    Node operator[](const std::string& key) const {
        const auto& entries = object();
        const auto found = entries.find(key);
        if (found == entries.end()) fail("missing field '" + key + "'");
        return {found->second, path_ + "." + key};
    }
    std::string text() const {
        if (value_.kind_case() != Value::kStringValue) fail("expected string");
        const auto& value = value_.string_value();
        if (value.empty() || value.size() > 256 || value.find_first_of("\r\n\t") != std::string::npos
            || value.find('\0') != std::string::npos) fail("invalid string");
        return value;
    }
    std::string id() const {
        const auto value = text();
        if (value.size() > 64 || value.front() < 'a' || value.front() > 'z'
            || value.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos) fail("invalid snake_case ID");
        return value;
    }
    double number(double low = -std::numeric_limits<float>::max(), double high = std::numeric_limits<float>::max()) const {
        if (value_.kind_case() != Value::kNumberValue) fail("expected number");
        const double number = value_.number_value();
        if (!std::isfinite(number) || number < low || number > high) fail("number out of range");
        return number;
    }
    float floating() const {
        const double source = number();
        const float result = static_cast<float>(source);
        if (!std::isfinite(result) || (source != 0 && result == 0)) fail("number outside finite float range");
        return result;
    }
    bool boolean() const {
        if (value_.kind_case() != Value::kBoolValue) fail("expected boolean");
        return value_.bool_value();
    }
    bool is_null() const { return value_.kind_case() == Value::kNullValue; }
    std::vector<Node> list(std::size_t size) const {
        return list(size, size);
    }
    std::vector<Node> list(std::size_t minimum, std::size_t maximum) const {
        if (value_.kind_case() != Value::kListValue
            || static_cast<std::size_t>(value_.list_value().values_size()) < minimum
            || static_cast<std::size_t>(value_.list_value().values_size()) > maximum) fail("unexpected array shape");
        const auto size = static_cast<std::size_t>(value_.list_value().values_size());
        std::vector<Node> result;
        for (std::size_t i = 0; i < size; ++i)
            result.emplace_back(value_.list_value().values(static_cast<int>(i)), path_ + "[" + std::to_string(i) + "]");
        return result;
    }
private:
    const Value& value_;
    std::string path_;
};

float& parameter(VehicleParameters& parameters, const std::string& name)
{
    static const std::map<std::string, float VehicleParameters::*> fields{
        {"mass_kg", &VehicleParameters::mass_kg},
        {"wheelbase_m", &VehicleParameters::wheelbase_m},
        {"max_steering_angle_rad", &VehicleParameters::max_steering_angle_rad},
        {"steering_rate_rad_s", &VehicleParameters::steering_rate_rad_s},
        {"steering_return_rate_rad_s", &VehicleParameters::steering_return_rate_rad_s},
        {"max_drive_force_n", &VehicleParameters::max_drive_force_n},
        {"max_reverse_force_n", &VehicleParameters::max_reverse_force_n},
        {"max_drive_power_w", &VehicleParameters::max_drive_power_w},
        {"drive_force_rise_rate_n_per_s", &VehicleParameters::drive_force_rise_rate_n_per_s},
        {"drive_force_fall_rate_n_per_s", &VehicleParameters::drive_force_fall_rate_n_per_s},
        {"max_service_brake_n", &VehicleParameters::max_service_brake_n},
        {"max_handbrake_force_n", &VehicleParameters::max_handbrake_force_n},
        {"rolling_resistance_coeff", &VehicleParameters::rolling_resistance_coeff},
        {"drivetrain_drag_n_per_mps", &VehicleParameters::drivetrain_drag_n_per_mps},
        {"drag_coefficient", &VehicleParameters::drag_coefficient},
        {"frontal_area_m2", &VehicleParameters::frontal_area_m2},
        {"air_density_kg_m3", &VehicleParameters::air_density_kg_m3},
        {"tire_radius_m", &VehicleParameters::tire_radius_m},
        {"final_drive_ratio", &VehicleParameters::final_drive_ratio},
        {"drive_gear_ratio", &VehicleParameters::drive_gear_ratio},
        {"reverse_gear_ratio", &VehicleParameters::reverse_gear_ratio},
        {"max_forward_speed_mps", &VehicleParameters::max_forward_speed_mps},
        {"max_reverse_speed_mps", &VehicleParameters::max_reverse_speed_mps},
        {"idle_rpm", &VehicleParameters::idle_rpm},
        {"max_rpm", &VehicleParameters::max_rpm},
        {"fuel_rate_percent_s", &VehicleParameters::fuel_rate_percent_s},
        {"front_track_m", &VehicleParameters::front_track_m},
        {"rear_track_m", &VehicleParameters::rear_track_m},
        {"cg_height_m", &VehicleParameters::cg_height_m},
        {"front_static_load_fraction", &VehicleParameters::front_static_load_fraction},
        {"front_drive_torque_fraction", &VehicleParameters::front_drive_torque_fraction},
        {"front_service_brake_fraction", &VehicleParameters::front_service_brake_fraction},
        {"yaw_inertia_kg_m2", &VehicleParameters::yaw_inertia_kg_m2},
        {"pitch_inertia_kg_m2", &VehicleParameters::pitch_inertia_kg_m2},
        {"roll_inertia_kg_m2", &VehicleParameters::roll_inertia_kg_m2},
        {"attitude_spring_n_m_rad", &VehicleParameters::attitude_spring_n_m_rad},
        {"attitude_damping_n_m_s_rad", &VehicleParameters::attitude_damping_n_m_s_rad},
        {"front_tire_corner_stiffness_n_rad", &VehicleParameters::front_tire_corner_stiffness_n_rad},
        {"rear_tire_corner_stiffness_n_rad", &VehicleParameters::rear_tire_corner_stiffness_n_rad},
        {"tire_longitudinal_stiffness_n", &VehicleParameters::tire_longitudinal_stiffness_n},
        {"tire_friction", &VehicleParameters::tire_friction},
        {"surface_default_friction_scale", &VehicleParameters::surface_default_friction_scale},
        {"surface_asphalt_friction_scale", &VehicleParameters::surface_asphalt_friction_scale},
        {"surface_low_friction_scale", &VehicleParameters::surface_low_friction_scale},
        {"surface_rough_friction_scale", &VehicleParameters::surface_rough_friction_scale},
        {"lateral_grip_priority", &VehicleParameters::lateral_grip_priority},
        {"traction_control_slip_target", &VehicleParameters::traction_control_slip_target},
        {"traction_control_full_cut_slip", &VehicleParameters::traction_control_full_cut_slip},
        {"wheel_inertia_kg_m2", &VehicleParameters::wheel_inertia_kg_m2},
        {"wheel_free_spin_damping_n_m_s", &VehicleParameters::wheel_free_spin_damping_n_m_s},
        {"low_speed_slip_reference_mps", &VehicleParameters::low_speed_slip_reference_mps},
        {"collision_body_overhang_m", &VehicleParameters::collision_body_overhang_m},
        {"collision_body_side_padding_m", &VehicleParameters::collision_body_side_padding_m},
        {"collision_body_half_height_m", &VehicleParameters::collision_body_half_height_m},
        {"collision_body_center_forward_offset_m", &VehicleParameters::collision_body_center_forward_offset_m},
        {"collision_body_ground_clearance_m", &VehicleParameters::collision_body_ground_clearance_m},
        {"chassis_shell_center_up_offset_m", &VehicleParameters::chassis_shell_center_up_offset_m},
        {"chassis_shell_half_height_m", &VehicleParameters::chassis_shell_half_height_m},
    };
    const auto found = fields.find(name);
    if (found != fields.end()) return parameters.*found->second;
    static const std::map<std::string, float SuspensionParameters::*> suspension_fields{
        {"suspension_rest_length_m", &SuspensionParameters::rest_length_m},
        {"suspension_max_compression_m", &SuspensionParameters::max_compression_m},
        {"suspension_max_extension_m", &SuspensionParameters::max_extension_m},
        {"suspension_spring_rate_n_per_m", &SuspensionParameters::spring_rate_n_per_m},
        {"suspension_damper_rate_n_s_per_m", &SuspensionParameters::damper_rate_n_s_per_m},
        {"suspension_max_force_n", &SuspensionParameters::max_force_n}
    };
    const auto nested = suspension_fields.find(name);
    if (nested != suspension_fields.end()) return parameters.suspension.*nested->second;
    throw std::runtime_error("unknown player override '" + name + "'");
}

RuntimeNpcProfile npc_profile(const Node& n)
{
    n.fields({"name", "half_length_m", "half_width_m", "half_height_m", "mass_kg", "yaw_inertia_kg_m2",
        "maximum_reaction_speed_mps", "speed_scale", "acceleration_mps2", "braking_mps2",
        "tumble_contact_below_cg_m", "ground_clearance_m", "minimum_turn_radius_m"});
    return {n["name"].id(), n["half_length_m"].number(0.01, 30), n["half_width_m"].number(0.01, 10),
        n["half_height_m"].number(0.01, 10), n["mass_kg"].number(1, 1000000), n["yaw_inertia_kg_m2"].number(0.01, 1e9),
        n["maximum_reaction_speed_mps"].number(0.01, 1000), n["speed_scale"].number(0.01, 10),
        n["acceleration_mps2"].number(0.01, 100), n["braking_mps2"].number(0.01, 100),
        n["tumble_contact_below_cg_m"].number(0, 10), n["ground_clearance_m"].number(0, 10),
        n["minimum_turn_radius_m"].number(0.01, 1000)};
}

void vector3(const Node& node, bool scale = false)
{
    for (const auto& coordinate : node.list(3)) (void)coordinate.number(scale ? 0.001 : -100000, scale ? 100 : 100000);
}

void validate_visual(const Node& node)
{
    node.fields({"wheel_origins_cm", "wheel_scales", "lamp_locations_cm", "exhaust_location_cm",
        "driver_translation_cm", "driver_scale", "half_height_m", "cg_height_m",
        "collision_body_forward_offset_m", "collision_ground_clearance_m", "npc_collision_ground_clearance_m"});
    for (const auto* key : {"wheel_origins_cm", "wheel_scales", "lamp_locations_cm"}) {
        for (const auto& value : node[key].list(4)) vector3(value, std::string_view(key) == "wheel_scales");
    }
    vector3(node["exhaust_location_cm"]);
    vector3(node["driver_translation_cm"]);
    vector3(node["driver_scale"], true);
    (void)node["half_height_m"].number(0.01, 20);
    (void)node["cg_height_m"].number(0.01, 20);
    (void)node["collision_body_forward_offset_m"].number(-20, 20);
    (void)node["collision_ground_clearance_m"].number(0, 20);
    (void)node["npc_collision_ground_clearance_m"].number(0, 20);
}

RuntimeVehicleClass vehicle_class(const Node& node)
{
    const double number = node.number(1, 4);
    if (std::floor(number) != number) node.fail("expected integer class");
    return static_cast<RuntimeVehicleClass>(static_cast<unsigned>(number));
}

RuntimeAxleModuleSelection axle_selection(const Node& axle)
{
    axle.fields({"tire", "suspension"});
    RuntimeAxleModuleSelection result;
    if (!axle["tire"].is_null()) result.tire = axle["tire"].id();
    if (!axle["suspension"].is_null()) result.suspension = axle["suspension"].id();
    return result;
}

std::optional<RuntimePowertrainModuleSelection> powertrain_selection(const Node& modules)
{
    if (modules.is_null()) return std::nullopt;
    modules.fields({"engine", "transmission", "drivetrain", "fuel_tank",
        "initial_fuel_l", "upshift_rpm", "downshift_rpm", "shift_duration_s"});
    return RuntimePowertrainModuleSelection{
        modules["engine"].id(), modules["transmission"].id(), modules["drivetrain"].id(), modules["fuel_tank"].id(),
        modules["initial_fuel_l"].number(0, 5000), modules["upshift_rpm"].number(0, 30000),
        modules["downshift_rpm"].number(0, 30000), modules["shift_duration_s"].number(0, 10)};
}

RuntimeVehicleLoadout loadout_definition(const Node& node)
{
    node.fields({"schema_version", "id", "name", "vehicle_class", "axle_modules", "powertrain_modules"});
    (void)node["schema_version"].number(1, 1);
    if (node["id"].id().starts_with(custom_loadout_prefix)) node["id"].fail("reserved custom loadout prefix");
    node["axle_modules"].fields({"front", "rear"});
    return {node["id"].id(), node["name"].text(), vehicle_class(node["vehicle_class"]),
        axle_selection(node["axle_modules"]["front"]), axle_selection(node["axle_modules"]["rear"]),
        powertrain_selection(node["powertrain_modules"])};
}

RuntimeVehicleProfile profile(const Node& node)
{
    node.fields({"schema_version", "id", "vehicle_class", "player_overrides", "scale_suspension_by_mass",
        "rest_length_cg_tire_offset_m", "npc", "visual"}, {"axle_modules", "powertrain_modules"});
    (void)node["schema_version"].number(1, 1);
    RuntimeVehicleProfile result{};
    result.id = node["id"].id();
    result.vehicle_class = vehicle_class(node["vehicle_class"]);
    result.scale_suspension_by_mass = node["scale_suspension_by_mass"].boolean();
    const auto offset = node["rest_length_cg_tire_offset_m"];
    if (!offset.is_null()) result.rest_length_cg_tire_offset_m = offset.floating();
    VehicleParameters probe;
    for (const auto& [key, value] : node["player_overrides"].object()) {
        const auto override = node["player_overrides"][key];
        if (key == "single_track") result.single_track_override = override.boolean();
        else {
            try { (void)parameter(probe, key); }
            catch (const std::exception& error) { override.fail(error.what()); }
            result.player_overrides.emplace(key, override.floating());
        }
    }
    if (result.scale_suspension_by_mass) {
        for (const auto* key : {"suspension_spring_rate_n_per_m", "suspension_damper_rate_n_s_per_m", "suspension_max_force_n"}) {
            if (result.player_overrides.contains(key)) node.fail("mass scaling conflicts with explicit suspension override");
        }
    }
    if (result.rest_length_cg_tire_offset_m && result.player_overrides.contains("suspension_rest_length_m"))
        node.fail("derived rest length conflicts with explicit suspension override");
    result.npc = npc_profile(node["npc"]);
    validate_visual(node["visual"]);
    if (node.object().contains("axle_modules")) {
        const auto modules = node["axle_modules"];
        modules.fields({"front", "rear"});
        result.front_axle_modules = axle_selection(modules["front"]);
        result.rear_axle_modules = axle_selection(modules["rear"]);
    }
    if (node.object().contains("powertrain_modules")) result.powertrain_modules = powertrain_selection(node["powertrain_modules"]);
    return result;
}

std::string path_key(const std::filesystem::path& path)
{
    auto result = path.generic_string();
#ifdef _WIN32
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 'a' - 'A') : static_cast<char>(c);
    });
#endif
    return result;
}

std::filesystem::path checked_path(const std::filesystem::path& root, const Node& node)
{
    const auto name = node.text();
    if (name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/_-.") != std::string::npos
        || name.find("//") != std::string::npos)
        node.fail("catalog paths must use ASCII letters, digits, slash, underscore, hyphen or dot");
    const std::filesystem::path relative(name);
    if (relative.is_absolute() || relative.has_root_name() || relative.has_root_directory()
        || name.find_first_of(":\\") != std::string::npos || relative.extension() != ".json") node.fail("expected relative .json path");
    for (const auto& element : relative) if (element == "." || element == "..") node.fail("path traversal forbidden");
    const auto path = std::filesystem::canonical(root / relative);
    if (!path_key(path).starts_with(path_key(root) + "/") || !std::filesystem::is_regular_file(path))
        node.fail("profile escapes catalog directory or is not a regular file");
    return path;
}

std::string checksum(const std::string& manifest, const std::array<std::pair<std::string, std::string>, 4>& sources,
                     const std::map<std::string, std::string>& part_sources,
                     const std::map<std::string, std::string>& loadout_sources)
{
    std::uint64_t hash = 14695981039346656037ull;
    const auto feed = [&hash](std::string_view bytes) {
        for (const unsigned char byte : bytes) { hash ^= byte; hash *= 1099511628211ull; }
    };
    const auto field = [&feed](std::string_view bytes) { feed(std::to_string(bytes.size())); feed(":"); feed(bytes); };
    feed("runtime-vehicle-catalog-v1");
    field(manifest);
    for (const auto& [path, bytes] : sources) { field(path); field(bytes); }
    for (const auto& [path, bytes] : part_sources) { field(path); field(bytes); }
    for (const auto& [path, bytes] : loadout_sources) { field(path); field(bytes); }
    std::ostringstream result;
    result << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << hash;
    return result.str();
}

float module_float(double value, const std::string& context)
{
    const float converted = static_cast<float>(value);
    if (!std::isfinite(converted) || (value != 0 && converted == 0))
        throw std::invalid_argument(context + ": module value outside finite float range");
    return converted;
}

template<typename Specification>
const Specification& selected_part(const RuntimeVehicleCatalog& catalog, const std::string& id, const std::string& context)
{
    const auto found = catalog.parts().find(id);
    if (found == catalog.parts().end()) throw std::invalid_argument(context + ": unknown part '" + id + "'");
    const auto* result = std::get_if<Specification>(&found->second.specification);
    if (!result) throw std::invalid_argument(context + ": wrong part kind for '" + id + "'");
    return *result;
}

ModuleIds loadout_module_ids(const RuntimeVehicleLoadout& loadout)
{
    if (!loadout.powertrain_modules)
        throw std::invalid_argument("custom loadout requires a base preset with complete powertrain policy");
    const auto& powertrain = *loadout.powertrain_modules;
    return {loadout.front_axle_modules.tire, loadout.front_axle_modules.suspension,
        loadout.rear_axle_modules.tire, loadout.rear_axle_modules.suspension,
        powertrain.engine, powertrain.transmission, powertrain.drivetrain, powertrain.fuel_tank};
}

void set_custom_modules(const RuntimeVehicleCatalog& catalog, RuntimeVehicleLoadout& loadout, const ModuleIds& ids)
{
    const std::string context = "custom loadout '" + loadout.id + "'";
    for (std::size_t slot = 0; slot < ids.size(); ++slot) {
        if (!ids[slot]) {
            if (slot >= 4) throw std::invalid_argument(context + ": powertrain part cannot be null");
            continue;
        }
        switch (slot) {
        case 0: case 2: (void)selected_part<vehicle_catalog::Tire>(catalog, *ids[slot], context); break;
        case 1: case 3: (void)selected_part<vehicle_catalog::Suspension>(catalog, *ids[slot], context); break;
        case 4: (void)selected_part<vehicle_catalog::Engine>(catalog, *ids[slot], context); break;
        case 5: (void)selected_part<vehicle_catalog::Transmission>(catalog, *ids[slot], context); break;
        case 6: (void)selected_part<vehicle_catalog::Drivetrain>(catalog, *ids[slot], context); break;
        case 7: (void)selected_part<vehicle_catalog::FuelTank>(catalog, *ids[slot], context); break;
        }
    }
    loadout.front_axle_modules = {ids[0], ids[1]};
    loadout.rear_axle_modules = {ids[2], ids[3]};
    auto& powertrain = *loadout.powertrain_modules;
    powertrain.engine = *ids[4];
    powertrain.transmission = *ids[5];
    powertrain.drivetrain = *ids[6];
    powertrain.fuel_tank = *ids[7];
}

unsigned custom_index(std::string_view id, std::size_t position)
{
    unsigned result = 0;
    for (std::size_t offset = 0; offset < 2; ++offset) {
        const char digit = id[position + offset];
        if (!((digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f')))
            throw std::invalid_argument("custom loadout indexes require lowercase hexadecimal");
        result = result * 16 + (digit <= '9' ? digit - '0' : digit - 'a' + 10);
    }
    return result;
}

void append_custom_index(std::string& id, std::size_t index)
{
    constexpr char digits[] = "0123456789abcdef";
    id.push_back(digits[(index >> 4) & 15]);
    id.push_back(digits[index & 15]);
}

void apply_axle_modules(const RuntimeVehicleCatalog& catalog, const RuntimeVehicleProfile& profile,
                        VehicleParameters& result, bool front)
{
    const auto& selection = front ? profile.front_axle_modules : profile.rear_axle_modules;
    auto& contact = front ? result.front_axle_contact : result.rear_axle_contact;
    const std::string context = "runtime profile '" + profile.id + "' " + (front ? "front" : "rear");
    const double axle_fraction = front ? result.front_static_load_fraction : 1.0 - result.front_static_load_fraction;
    const double physical_wheel_load = result.mass_kg * axle_fraction * 9.80665 / (result.single_track ? 1.0 : 2.0);
    // Motorcycle front/rear contacts are represented by two paired wire slots.
    // Parts describe one physical wheel; extensive contact values divide once.
    const double slot_share = result.single_track ? 0.5 : 1.0;
    if (selection.tire) {
        const auto& tire = selected_part<vehicle_catalog::Tire>(catalog, *selection.tire, context + ".tire");
        if (tire.rated_load_n < physical_wheel_load)
            throw std::invalid_argument(context + ": tire rated_load_n is below physical-wheel static load");
        const double track = front ? result.front_track_m : result.rear_track_m;
        if (tire.radius_m * 2 >= result.wheelbase_m || tire.width_m >= track)
            throw std::invalid_argument(context + ": tire dimensions exceed supported axle geometry");
        contact.tire = AxleTireParameters{
            module_float(tire.radius_m, context), module_float(tire.friction_coefficient, context),
            module_float(tire.longitudinal_stiffness_n * slot_share, context),
            module_float(tire.cornering_stiffness_n_rad * slot_share, context),
            module_float(tire.rolling_resistance_coefficient, context)};
    }
    if (selection.suspension) {
        const auto& suspension = selected_part<vehicle_catalog::Suspension>(catalog, *selection.suspension, context + ".suspension");
        if (suspension.max_force_n < physical_wheel_load)
            throw std::invalid_argument(context + ": suspension max_force_n is below physical-wheel static load");
        if (suspension.spring_rate_n_per_m * suspension.max_compression_m < physical_wheel_load)
            throw std::invalid_argument(context + ": suspension compression capacity is below physical-wheel static load");
        contact.suspension = SuspensionParameters{
            module_float(suspension.rest_length_m, context), module_float(suspension.max_compression_m, context),
            module_float(suspension.max_extension_m, context), module_float(suspension.spring_rate_n_per_m * slot_share, context),
            module_float(suspension.damper_rate_n_s_per_m * slot_share, context),
            module_float(suspension.max_force_n * slot_share, context)};
    }
    // Part mass/inertia and rim metadata remain authoring/fit data. This stage
    // changes contact behavior only, not the already validated chassis mass.
}

void apply_powertrain_modules(const RuntimeVehicleCatalog& catalog, const RuntimeVehicleProfile& profile,
                              VehicleParameters& result)
{
    if (!profile.powertrain_modules) return;
    const auto& selection = *profile.powertrain_modules;
    const std::string context = "runtime profile '" + profile.id + "' powertrain";
    const auto& engine = selected_part<vehicle_catalog::Engine>(catalog, selection.engine, context + ".engine");
    const auto& transmission = selected_part<vehicle_catalog::Transmission>(catalog, selection.transmission, context + ".transmission");
    const auto& drivetrain = selected_part<vehicle_catalog::Drivetrain>(catalog, selection.drivetrain, context + ".drivetrain");
    const auto& tank = selected_part<vehicle_catalog::FuelTank>(catalog, selection.fuel_tank, context + ".fuel_tank");
    if (engine.fuel_type != tank.fuel_type)
        throw std::invalid_argument(context + ": engine and fuel tank fuel_type mismatch");
    const double peak_torque = std::max_element(engine.torque_curve.begin(), engine.torque_curve.end(),
        [](const auto& first, const auto& second) { return first.torque_nm < second.torque_nm; })->torque_nm;
    if (peak_torque > transmission.max_input_torque_nm)
        throw std::invalid_argument(context + ": engine peak torque exceeds transmission max_input_torque_nm");
    if (selection.initial_fuel_l < 0 || selection.initial_fuel_l > tank.capacity_l)
        throw std::invalid_argument(context + ": initial_fuel_l must be between zero and tank capacity");
    if (!(engine.idle_rpm < selection.downshift_rpm && selection.downshift_rpm < selection.upshift_rpm
        && selection.upshift_rpm < engine.max_rpm) || selection.shift_duration_s <= 0 || selection.shift_duration_s > 10)
        throw std::invalid_argument(context + ": shift policy requires idle < downshift < upshift < max RPM and positive duration");

    PowertrainParameters parameters;
    parameters.engine = EngineParameters{
        module_float(engine.idle_rpm, context), module_float(engine.max_rpm, context),
        module_float(engine.rotational_inertia_kg_m2, context), module_float(engine.response_time_s, context),
        module_float(engine.idle_fuel_lph, context), module_float(engine.bsfc_g_per_kwh, context), {}};
    for (const auto& point : engine.torque_curve)
        parameters.engine.torque_curve.push_back({module_float(point.rpm, context), module_float(point.torque_nm, context)});
    parameters.transmission = TransmissionParameters{{}, module_float(transmission.reverse_ratio, context),
        module_float(transmission.max_input_torque_nm, context), module_float(transmission.efficiency, context),
        module_float(selection.upshift_rpm, context), module_float(selection.downshift_rpm, context),
        module_float(selection.shift_duration_s, context)};
    for (const double ratio : transmission.forward_ratios)
        parameters.transmission.forward_ratios.push_back(module_float(ratio, context));
    parameters.drivetrain = DrivetrainParameters{module_float(drivetrain.final_drive_ratio, context),
        module_float(drivetrain.front_torque_fraction, context), module_float(drivetrain.efficiency, context)};
    parameters.fuel_tank = FuelTankParameters{module_float(tank.capacity_l, context), module_float(selection.initial_fuel_l, context),
        module_float(tank.fuel_density_kg_l, context)};
    if (!valid_powertrain_parameters(parameters))
        throw std::invalid_argument(context + ": invalid resolved powertrain parameters");
    result.powertrain = std::move(parameters);
    result.front_drive_torque_fraction = result.powertrain->drivetrain.front_torque_fraction;
    // The selected modules drive torque/RPM/fuel through the new model. Legacy
    // force, RPM and percentage-fuel scalars remain unchanged and are bypassed.
}

} // namespace

const RuntimeVehicleProfile& RuntimeVehicleCatalog::at(RuntimeVehicleClass vehicle_class) const
{
    if (vehicle_class == RuntimeVehicleClass::Unspecified) vehicle_class = RuntimeVehicleClass::Sedan;
    const auto index = static_cast<unsigned>(vehicle_class);
    if (index < 1 || index > profiles_.size()) throw std::invalid_argument("unsupported runtime vehicle class");
    return profiles_[index - 1];
}

RuntimeVehicleLoadout RuntimeVehicleCatalog::loadout(std::string_view id, RuntimeVehicleClass vehicle_class) const
{
    const auto& profile = at(vehicle_class);
    if (id.starts_with(custom_loadout_prefix)) {
        if (id.size() != 28 || id[11] != '_') throw std::invalid_argument("malformed custom loadout ID");
        const auto base_index = custom_index(id, 9);
        if (base_index >= loadouts_.size()) throw std::invalid_argument("custom loadout base index out of range");
        auto base = loadouts_.begin();
        std::advance(base, base_index);
        if (base->second.vehicle_class != profile.vehicle_class)
            throw std::invalid_argument("custom loadout does not match vehicle class");
        const auto original_ids = loadout_module_ids(base->second);
        ModuleIds ids;
        for (std::size_t slot = 0; slot < ids.size(); ++slot) {
            const auto index = custom_index(id, 12 + slot * 2);
            if (index == 0xff) {
                if (slot >= 4) throw std::invalid_argument("custom loadout powertrain part cannot be null");
            } else {
                if (index >= parts_.size()) throw std::invalid_argument("custom loadout part index out of range");
                auto part = parts_.begin();
                std::advance(part, index);
                ids[slot] = part->first;
            }
        }
        if (ids == original_ids) throw std::invalid_argument("redundant custom loadout must use named base ID");
        auto result = base->second;
        result.id = id;
        result.name += " - Custom";
        set_custom_modules(*this, result, ids);
        return result;
    }
    const auto found = loadouts_.find(std::string(id));
    if (found == loadouts_.end()) throw std::invalid_argument("unknown runtime loadout '" + std::string(id) + "'");
    if (found->second.vehicle_class != profile.vehicle_class)
        throw std::invalid_argument("runtime loadout '" + std::string(id) + "' does not match vehicle class");
    return found->second;
}

std::string RuntimeVehicleCatalog::custom_loadout_id(std::string_view base_id, const ModuleIds& ids) const
{
    const auto base = loadouts_.find(std::string(base_id));
    if (base == loadouts_.end()) throw std::invalid_argument("custom loadout requires a named base preset");
    const auto original_ids = loadout_module_ids(base->second);
    auto selected = base->second;
    set_custom_modules(*this, selected, ids);
    std::string id(base_id);
    if (ids != original_ids) {
        id = custom_loadout_prefix;
        append_custom_index(id, static_cast<std::size_t>(std::distance(loadouts_.begin(), base)));
        id.push_back('_');
        for (const auto& part : ids) {
            const auto index = part ? static_cast<std::size_t>(std::distance(parts_.begin(), parts_.find(*part))) : 0xff;
            append_custom_index(id, index);
        }
    }
    // The ID is only a compact selection, never an authorization to bypass
    // the normal part/shift/fuel/static-load checks for effective parameters.
    (void)player_parameters(VehicleParameters{}, base->second.vehicle_class, id);
    return id;
}

VehicleParameters RuntimeVehicleCatalog::player_parameters(const VehicleParameters& sedan, RuntimeVehicleClass vehicle_class,
                                                           std::string_view loadout_id) const
{
    if (!valid_vehicle_parameters(sedan)) throw std::invalid_argument("configured sedan profile is invalid");
    auto profile = at(vehicle_class);
    VehicleParameters result = sedan;
    if (!loadout_id.empty()) {
        const auto& selected = loadout(loadout_id, vehicle_class);
        profile.id = selected.id;
        profile.front_axle_modules = selected.front_axle_modules;
        profile.rear_axle_modules = selected.rear_axle_modules;
        profile.powertrain_modules = selected.powertrain_modules;
        // An explicit preset is complete: null returns to the scalar baseline,
        // not to an earlier preset or the class profile's optional modules.
        result.front_axle_contact = {};
        result.rear_axle_contact = {};
        result.powertrain.reset();
    }
    for (const auto& [name, value] : profile.player_overrides) parameter(result, name) = value;
    if (profile.single_track_override) result.single_track = *profile.single_track_override;
    if (profile.scale_suspension_by_mass) {
        const float scale = result.mass_kg / sedan.mass_kg;
        result.suspension.spring_rate_n_per_m = sedan.suspension.spring_rate_n_per_m * scale;
        result.suspension.damper_rate_n_s_per_m = sedan.suspension.damper_rate_n_s_per_m * scale;
        result.suspension.max_force_n = sedan.suspension.max_force_n * scale;
    }
    if (profile.rest_length_cg_tire_offset_m)
        result.suspension.rest_length_m = result.cg_height_m - result.tire_radius_m + *profile.rest_length_cg_tire_offset_m;
    apply_axle_modules(*this, profile, result, true);
    apply_axle_modules(*this, profile, result, false);
    apply_powertrain_modules(*this, profile, result);
    if (!valid_vehicle_parameters(result)) throw std::invalid_argument("runtime profile '" + profile.id + "' produces invalid player parameters");
    return result;
}

RuntimeVehicleCatalog load_runtime_vehicle_catalog(const std::filesystem::path& manifest)
{
    const auto manifest_path = std::filesystem::canonical(manifest);
    const auto root = manifest_path.parent_path();
    const auto manifest_bytes = vehicle_catalog::json::read_bytes(manifest_path);
    const auto document = vehicle_catalog::json::parse(manifest_bytes, manifest_path.string());
    const Node node(document, manifest_path.string());
    node.fields({"schema_version", "profiles"}, {"parts", "loadouts"});
    (void)node["schema_version"].number(1, 1);
    RuntimeVehicleCatalog result;
    std::set<std::string> files{path_key(manifest_path)}, ids;
    std::array<bool, 4> classes{};
    std::array<std::pair<std::string, std::string>, 4> sources;
    std::map<std::string, std::string> part_sources;
    std::map<std::string, std::string> loadout_sources;
    std::size_t total_bytes = manifest_bytes.size();
    for (const auto& source : node["profiles"].list(4)) {
        const auto path = checked_path(root, source);
        if (!files.insert(path_key(path)).second) source.fail("duplicate profile file");
        const auto bytes = vehicle_catalog::json::read_bytes(path);
        total_bytes += bytes.size();
        const auto parsed = vehicle_catalog::json::parse(bytes, path.string());
        auto value = profile(Node(parsed, path.string()));
        const auto index = static_cast<unsigned>(value.vehicle_class) - 1;
        if (classes[index]) source.fail("duplicate vehicle_class");
        if (!ids.insert(value.id).second) source.fail("duplicate profile ID");
        classes[index] = true;
        sources[index] = {source.text(), bytes};
        result.profiles_[index] = std::move(value);
    }
    if (node.object().contains("parts")) {
        for (const auto& source : node["parts"].list(0, 64)) {
            const auto path = checked_path(root, source);
            if (!files.insert(path_key(path)).second) source.fail("duplicate catalog file");
            const auto bytes = vehicle_catalog::json::read_bytes(path);
            total_bytes += bytes.size();
            if (total_bytes > 16 * 1024 * 1024) source.fail("runtime catalog exceeds 16 MiB");
            auto part = vehicle_catalog::parse_part_definition(bytes, path.string());
            if (!std::holds_alternative<vehicle_catalog::Tire>(part.specification)
                && !std::holds_alternative<vehicle_catalog::Suspension>(part.specification)
                && !std::holds_alternative<vehicle_catalog::Engine>(part.specification)
                && !std::holds_alternative<vehicle_catalog::Transmission>(part.specification)
                && !std::holds_alternative<vehicle_catalog::Drivetrain>(part.specification)
                && !std::holds_alternative<vehicle_catalog::FuelTank>(part.specification))
                source.fail("unsupported runtime part kind; contact and powertrain modules only");
            if (!ids.insert(part.id).second) source.fail("duplicate catalog ID '" + part.id + "'");
            part_sources.emplace(source.text(), bytes);
            const auto id = part.id;
            result.parts_.emplace(id, std::move(part));
        }
    }
    if (node.object().contains("loadouts")) {
        for (const auto& source : node["loadouts"].list(0, 64)) {
            const auto path = checked_path(root, source);
            if (!files.insert(path_key(path)).second) source.fail("duplicate catalog file");
            const auto bytes = vehicle_catalog::json::read_bytes(path);
            total_bytes += bytes.size();
            if (total_bytes > 16 * 1024 * 1024) source.fail("runtime catalog exceeds 16 MiB");
            const auto parsed = vehicle_catalog::json::parse(bytes, path.string());
            auto value = loadout_definition(Node(parsed, path.string()));
            if (!ids.insert(value.id).second) source.fail("duplicate catalog ID '" + value.id + "'");
            loadout_sources.emplace(source.text(), bytes);
            const auto id = value.id;
            result.loadouts_.emplace(id, std::move(value));
        }
    }
    result.checksum_ = checksum(manifest_bytes, sources, part_sources, loadout_sources);
    for (const auto& value : result.fleet()) (void)result.player_parameters(VehicleParameters{}, value.vehicle_class);
    for (const auto& [id, value] : result.loadouts()) (void)result.player_parameters(VehicleParameters{}, value.vehicle_class, id);
    return result;
}

const RuntimeVehicleCatalog& default_runtime_vehicle_catalog()
{
    static const RuntimeVehicleCatalog catalog = load_runtime_vehicle_catalog(SIMCORE_DEFAULT_RUNTIME_VEHICLE_CATALOG_PATH);
    return catalog;
}

} // namespace simcore_host
