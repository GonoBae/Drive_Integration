#include "vehicle_catalog/vehicle_catalog.hpp"
#include "vehicle_catalog/json_document.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace simcore_host::vehicle_catalog {
namespace {

using Value = json::Value;

class Node {
public:
    Node(const Value& value, std::string path) : value_(value), path_(std::move(path)) {}

    [[noreturn]] void fail(const std::string& reason) const
    { throw std::runtime_error(path_ + ": " + reason); }

    void fields(std::initializer_list<std::string_view> expected) const
    {
        if (value_.kind_case() != Value::kStructValue) { fail("expected object"); }
        const auto& fields = value_.struct_value().fields();
        for (const auto& [key, value] : fields) {
            if (std::find(expected.begin(), expected.end(), key) == expected.end()) {
                fail("unknown field '" + key + "'");
            }
        }
        for (const auto key : expected) {
            if (!fields.contains(std::string(key))) { fail("missing field '" + std::string(key) + "'"); }
        }
    }

    Node operator[](const char* key) const
    {
        if (value_.kind_case() != Value::kStructValue) { fail("expected object"); }
        const auto& fields = value_.struct_value().fields();
        const auto found = fields.find(key);
        if (found == fields.end()) { fail("missing field '" + std::string(key) + "'"); }
        return Node(found->second, path_ + "." + key);
    }

    std::vector<Node> list(std::size_t min, std::size_t max) const
    {
        if (value_.kind_case() != Value::kListValue) { fail("expected array"); }
        const auto count = static_cast<std::size_t>(value_.list_value().values_size());
        if (count < min || count > max) { fail("array length out of range"); }
        std::vector<Node> result;
        for (std::size_t i = 0; i < count; ++i) {
            result.emplace_back(value_.list_value().values(static_cast<int>(i)),
                                path_ + "[" + std::to_string(i) + "]");
        }
        return result;
    }

    std::string text() const
    {
        if (value_.kind_case() != Value::kStringValue) { fail("expected string"); }
        const auto& text = value_.string_value();
        if (text.empty() || text.size() > 256 || text.find_first_of("\r\n\t") != std::string::npos
            || text.find('\0') != std::string::npos) { fail("invalid string"); }
        return text;
    }

    std::string id() const
    {
        const auto result = text();
        if (result.size() > 64 || result.front() < 'a' || result.front() > 'z'
            || result.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos) {
            fail("ID must use lowercase snake_case (maximum 64 characters)");
        }
        return result;
    }

    std::string choice(std::initializer_list<std::string_view> choices) const
    {
        const auto result = text();
        if (std::find(choices.begin(), choices.end(), result) == choices.end()) { fail("unsupported value '" + result + "'"); }
        return result;
    }

    double number(double min, double max) const
    {
        if (value_.kind_case() != Value::kNumberValue) { fail("expected number"); }
        const double number = value_.number_value();
        if (!std::isfinite(number) || number < min || number > max) { fail("number out of range"); }
        return number;
    }

    Vec3 vector(double min, double max) const
    {
        const auto elements = list(3, 3);
        return {elements[0].number(min, max), elements[1].number(min, max), elements[2].number(min, max)};
    }

private:
    const Value& value_;
    std::string path_;
};

void version(const Node& node)
{ (void)node["schema_version"].number(kSchemaVersion, kSchemaVersion); }

MassProperties mass(const Node& node)
{
    node.fields({"mass_kg", "center_of_mass_m", "inertia_diagonal_kg_m2"});
    MassProperties result{node["mass_kg"].number(0.001, 1000000),
                          node["center_of_mass_m"].vector(-100, 100),
                          node["inertia_diagonal_kg_m2"].vector(0.000001, 1e9)};
    const auto& inertia = result.inertia_diagonal_kg_m2;
    for (int i = 0; i < 3; ++i) {
        if (inertia[i] > inertia[(i + 1) % 3] + inertia[(i + 2) % 3] + 1e-9) {
            node.fail("inertia diagonal violates rigid-body triangle inequality");
        }
    }
    return result;
}

Mount mount(const Node& node)
{
    node.fields({"interface_id", "position_m"});
    return {node["interface_id"].id(), node["position_m"].vector(-100, 100)};
}

AxleDefinition axle(const Node& node)
{
    node.fields({"x_m", "track_m", "wheel_center_z_m", "min_tire_radius_m", "max_tire_radius_m",
                 "max_tire_width_m", "wheel_interface", "suspension_interface", "brake_interface"});
    AxleDefinition result{
        node["x_m"].number(-100, 100), node["track_m"].number(0, 10),
        node["wheel_center_z_m"].number(0.05, 5), node["min_tire_radius_m"].number(0.05, 5),
        node["max_tire_radius_m"].number(0.05, 5), node["max_tire_width_m"].number(0.01, 2),
        node["wheel_interface"].id(), node["suspension_interface"].id(), node["brake_interface"].id()};
    if (result.min_tire_radius_m > result.max_tire_radius_m) { node.fail("inverted tire radius range"); }
    return result;
}

VehicleDefinition vehicle(const Node& node)
{
    node.fields({"schema_version", "id", "name", "category", "dynamics_model", "chassis_mass",
                 "body_dimensions_m", "drag_coefficient", "frontal_area_m2", "mounts", "front_axle", "rear_axle"});
    version(node);
    const auto mounts = node["mounts"];
    mounts.fields({"engine", "transmission", "drivetrain", "steering", "fuel_tank"});
    VehicleDefinition result{
        node["id"].id(), node["name"].text(),
        node["category"].choice({"sedan", "suv", "compact", "truck", "bus", "motorcycle"}),
        node["dynamics_model"].choice({"four_wheel", "single_track"}),
        mass(node["chassis_mass"]), node["body_dimensions_m"].vector(0.1, 30),
        node["drag_coefficient"].number(0.01, 3), node["frontal_area_m2"].number(0.01, 50),
        mount(mounts["engine"]), mount(mounts["transmission"]), mount(mounts["drivetrain"]),
        mount(mounts["steering"]), mount(mounts["fuel_tank"]), axle(node["front_axle"]), axle(node["rear_axle"])};
    if (result.front_axle.x_m <= result.rear_axle.x_m
        || result.front_axle.x_m - result.rear_axle.x_m > result.body_dimensions_m[0]) {
        node.fail("axles must form a positive wheelbase within body length");
    }
    const bool single = result.dynamics_model == "single_track";
    if ((single && (result.front_axle.track_m != 0 || result.rear_axle.track_m != 0))
        || (!single && (result.front_axle.track_m < 0.1 || result.rear_axle.track_m < 0.1))) {
        node.fail("axle track does not match dynamics_model");
    }
    return result;
}

Engine engine(const Node& node)
{
    node.fields({"idle_rpm", "max_rpm", "rotational_inertia_kg_m2", "response_time_s", "idle_fuel_lph",
                 "bsfc_g_per_kwh", "fuel_type", "torque_curve"});
    Engine result{node["idle_rpm"].number(100, 10000), node["max_rpm"].number(101, 30000),
        node["rotational_inertia_kg_m2"].number(0.0001, 1000), node["response_time_s"].number(0.001, 10),
        node["idle_fuel_lph"].number(0, 100), node["bsfc_g_per_kwh"].number(1, 2000),
        node["fuel_type"].choice({"gasoline", "diesel"}), {}};
    if (result.idle_rpm >= result.max_rpm) { node.fail("idle RPM must be below max RPM"); }
    double previous = -1;
    double max_torque = 0;
    for (const auto point : node["torque_curve"].list(2, 128)) {
        point.fields({"rpm", "torque_nm"});
        const double rpm = point["rpm"].number(0, 30000);
        const double torque = point["torque_nm"].number(0, 100000);
        if (rpm <= previous) { point.fail("torque curve RPM must strictly increase"); }
        result.torque_curve.push_back({rpm, torque});
        previous = rpm;
        max_torque = std::max(max_torque, torque);
    }
    if (result.torque_curve.front().rpm != result.idle_rpm
        || result.torque_curve.back().rpm != result.max_rpm || max_torque == 0) {
        node.fail("torque curve must span idle_rpm through max_rpm and produce torque");
    }
    return result;
}

PartSpec specification(const std::string& kind, const Node& n)
{
    if (kind == "engine") { return engine(n); }
    if (kind == "transmission") {
        n.fields({"forward_ratios", "reverse_ratio", "max_input_torque_nm", "efficiency"});
        Transmission result{{}, n["reverse_ratio"].number(0.01, 100),
            n["max_input_torque_nm"].number(0.01, 100000), n["efficiency"].number(0.01, 1)};
        double previous = 101;
        for (const auto ratio_node : n["forward_ratios"].list(1, 20)) {
            const double ratio = ratio_node.number(0.01, 100);
            if (ratio >= previous) { ratio_node.fail("forward gear ratios must strictly decrease"); }
            result.forward_ratios.push_back(ratio);
            previous = ratio;
        }
        return result;
    }
    if (kind == "drivetrain") {
        n.fields({"final_drive_ratio", "front_torque_fraction", "efficiency"});
        return Drivetrain{n["final_drive_ratio"].number(0.01, 100),
            n["front_torque_fraction"].number(0, 1), n["efficiency"].number(0.01, 1)};
    }
    if (kind == "wheel") {
        n.fields({"rim_diameter_m", "rim_width_m", "rotational_inertia_kg_m2"});
        return Wheel{n["rim_diameter_m"].number(0.05, 3), n["rim_width_m"].number(0.01, 2),
                     n["rotational_inertia_kg_m2"].number(0.0001, 1000)};
    }
    if (kind == "tire") {
        n.fields({"radius_m", "width_m", "rim_diameter_m", "min_rim_width_m", "max_rim_width_m",
                  "friction_coefficient", "longitudinal_stiffness_n", "cornering_stiffness_n_rad",
                  "rolling_resistance_coefficient", "rated_load_n"});
        Tire result{n["radius_m"].number(0.05, 5), n["width_m"].number(0.01, 2),
            n["rim_diameter_m"].number(0.05, 3), n["min_rim_width_m"].number(0.01, 2),
            n["max_rim_width_m"].number(0.01, 2), n["friction_coefficient"].number(0.01, 5),
            n["longitudinal_stiffness_n"].number(1, 1e8), n["cornering_stiffness_n_rad"].number(1, 1e8),
            n["rolling_resistance_coefficient"].number(0, 1), n["rated_load_n"].number(1, 1e7)};
        if (result.radius_m * 2 <= result.rim_diameter_m || result.min_rim_width_m > result.max_rim_width_m) {
            n.fail("invalid tire sidewall or rim width range");
        }
        return result;
    }
    if (kind == "suspension") {
        n.fields({"rest_length_m", "max_compression_m", "max_extension_m", "spring_rate_n_per_m",
                  "damper_rate_n_s_per_m", "max_force_n"});
        Suspension result{n["rest_length_m"].number(0.001, 5), n["max_compression_m"].number(0, 5),
            n["max_extension_m"].number(0, 5), n["spring_rate_n_per_m"].number(1, 1e8),
            n["damper_rate_n_s_per_m"].number(0, 1e7), n["max_force_n"].number(1, 1e8)};
        if (result.max_compression_m >= result.rest_length_m) { n.fail("compression must be shorter than rest length"); }
        return result;
    }
    if (kind == "brake") {
        n.fields({"max_torque_nm", "handbrake_torque_nm"});
        return Brake{n["max_torque_nm"].number(0.01, 1e6), n["handbrake_torque_nm"].number(0, 1e6)};
    }
    if (kind == "steering") {
        n.fields({"max_angle_rad", "rate_rad_s", "return_rate_rad_s"});
        return Steering{n["max_angle_rad"].number(0.001, 1.5), n["rate_rad_s"].number(0.001, 20),
                        n["return_rate_rad_s"].number(0.001, 20)};
    }
    if (kind == "fuel_tank") {
        n.fields({"capacity_l", "fuel_density_kg_l", "fuel_type"});
        return FuelTank{n["capacity_l"].number(0.01, 5000), n["fuel_density_kg_l"].number(0.1, 2),
                        n["fuel_type"].choice({"gasoline", "diesel"})};
    }
    n.fail("unsupported part kind '" + kind + "'");
}

PartDefinition part(const Node& node)
{
    node.fields({"schema_version", "id", "name", "kind", "interface_id", "mass", "specification"});
    version(node);
    return {node["id"].id(), node["name"].text(), node["interface_id"].id(), mass(node["mass"]),
            specification(node["kind"].id(), node["specification"])};
}

AxleAssembly axle_assembly(const Node& node)
{
    node.fields({"wheel", "tire", "suspension", "brake"});
    return {node["wheel"].id(), node["tire"].id(), node["suspension"].id(), node["brake"].id()};
}

AssemblyDefinition assembly(const Node& node)
{
    node.fields({"schema_version", "id", "name", "vehicle_id", "engine", "transmission", "drivetrain",
                 "steering", "fuel_tank", "front_axle", "rear_axle"});
    version(node);
    return {node["id"].id(), node["name"].text(), node["vehicle_id"].id(), node["engine"].id(),
        node["transmission"].id(), node["drivetrain"].id(), node["steering"].id(), node["fuel_tank"].id(),
        axle_assembly(node["front_axle"]), axle_assembly(node["rear_axle"])};
}

std::string path_key(const std::filesystem::path& path)
{
    auto key = path.generic_string();
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 'a' - 'A') : static_cast<char>(c);
    });
#endif
    return key;
}

std::filesystem::path source_path(const std::filesystem::path& root, const Node& name)
{
    const auto text = name.text();
    const std::filesystem::path relative(text);
    if (relative.is_absolute() || relative.has_root_name() || relative.has_root_directory()
        || text.find_first_of(":\\") != std::string::npos || relative.extension() != ".json") {
        name.fail("expected relative .json path using forward slashes");
    }
    for (const auto& segment : relative) {
        if (segment == ".." || segment == ".") { name.fail("path traversal is forbidden"); }
    }
    const auto resolved = std::filesystem::canonical(root / relative);
    const auto root_key = path_key(root) + "/";
    if (!path_key(resolved).starts_with(root_key)) { name.fail("file escapes catalog directory"); }
    if (!std::filesystem::is_regular_file(resolved)) { name.fail("expected regular file"); }
    return resolved;
}

std::string checksum(const std::map<std::string, std::string>& sources)
{
    std::uint64_t value = 14695981039346656037ull;
    const auto feed = [&value](std::string_view bytes) {
        for (const unsigned char byte : bytes) { value ^= byte; value *= 1099511628211ull; }
    };
    feed("vehicle-catalog-v1");
    for (const auto& [name, bytes] : sources) {
        feed(std::to_string(name.size())); feed(":"); feed(name);
        feed(std::to_string(bytes.size())); feed(":"); feed(bytes);
    }
    std::ostringstream output;
    output << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << value;
    return output.str();
}

} // namespace

PartDefinition parse_part_definition(std::string_view bytes, const std::string& context)
{
    const auto document = json::parse(bytes, context);
    return part(Node(document, context));
}

Catalog load_catalog(const std::filesystem::path& manifest)
{
    const auto manifest_path = std::filesystem::canonical(manifest);
    const auto root = manifest_path.parent_path();
    const auto manifest_bytes = json::read_bytes(manifest_path);
    const auto document = json::parse(manifest_bytes, manifest_path.string());
    const Node node(document, manifest_path.string());
    node.fields({"schema_version", "vehicles", "parts", "assemblies"});
    version(node);
    Catalog result;
    std::set<std::string> loaded_paths{path_key(manifest_path)};
    std::map<std::string, std::string> sources;
    std::size_t total_bytes = manifest_bytes.size();
    const auto load_group = [&](const char* group, auto parser, auto& destination) {
        for (const auto file : node[group].list(1, 256)) {
            if (loaded_paths.size() > 256) { file.fail("catalog exceeds 256 definition files"); }
            const auto path = source_path(root, file);
            if (!loaded_paths.insert(path_key(path)).second) { file.fail("duplicate source file"); }
            const auto bytes = json::read_bytes(path);
            total_bytes += bytes.size();
            if (total_bytes > 16 * 1024 * 1024) { file.fail("catalog exceeds 16 MiB"); }
            sources.emplace(std::string(group) + "/" + path.lexically_relative(root).generic_string(), bytes);
            const auto data = json::parse(bytes, path.string());
            auto definition = parser(Node(data, path.string()));
            const auto id = definition.id;
            if (!destination.emplace(id, std::move(definition)).second) {
                file.fail("duplicate ID '" + id + "' in " + group);
            }
        }
    };
    load_group("vehicles", vehicle, result.vehicles_);
    load_group("parts", part, result.parts_);
    load_group("assemblies", assembly, result.assemblies_);
    result.checksum_ = checksum(sources);
    for (const auto& [id, definition] : result.assemblies_) { (void)result.resolve(id); }
    return result;
}

} // namespace simcore_host::vehicle_catalog
