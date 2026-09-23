#include "vehicle_catalog/vehicle_catalog.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <numbers>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef SIMCORE_TEST_VEHICLE_CATALOG_PATH
#error SIMCORE_TEST_VEHICLE_CATALOG_PATH must identify the tracked catalog manifest
#endif

namespace {

namespace fs = std::filesystem;
namespace catalog = simcore_host::vehicle_catalog;

constexpr const char* chassis = "chassis/sedan_reference.json";
constexpr const char* standard = "assemblies/sedan_standard.json";
constexpr const char* comfort = "assemblies/sedan_comfort.json";
constexpr const char* engine = "parts/engine_gasoline_2_0.json";
constexpr const char* transmission = "parts/transmission_automatic_6speed.json";
constexpr const char* wheel = "parts/wheel_alloy_16in.json";
constexpr const char* tire = "parts/tire_road_standard.json";
constexpr const char* suspension = "parts/suspension_standard.json";
constexpr const char* comfort_suspension = "parts/suspension_comfort.json";
constexpr const char* comfort_tire = "parts/tire_road_comfort.json";
constexpr const char* tank = "parts/fuel_tank_50l.json";

void require(bool condition, const std::string& message)
{
    if (!condition) { throw std::runtime_error(message); }
}

void near(double actual, double expected, const std::string& message)
{
    require(std::abs(actual - expected) < 1e-8, message);
}

std::string read(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    require(input.good(), "fixture must be readable: " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

class Fixture {
public:
    Fixture()
    {
        temp_parent_ = fs::canonical(fs::temp_directory_path());
        const auto seed = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::random_device random;
        for (int attempt = 0; attempt < 32; ++attempt) {
            const auto candidate = temp_parent_ / ("simcore-vehicle-catalog-" + seed + "-"
                + std::to_string(random()) + "-" + std::to_string(attempt));
            if (fs::create_directory(candidate)) { root_ = candidate; break; }
        }
        require(!root_.empty(), "could not create unique catalog fixture directory");
        try {
            const auto source_root = fs::canonical(SIMCORE_TEST_VEHICLE_CATALOG_PATH).parent_path();
            for (const auto& entry : fs::recursive_directory_iterator(source_root)) {
                if (!entry.is_regular_file()) { continue; }
                const auto target = root_ / entry.path().lexically_relative(source_root);
                fs::create_directories(target.parent_path());
                fs::copy_file(entry.path(), target);
            }
        } catch (...) {
            clean();
            throw;
        }
    }

    ~Fixture() { clean(); }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    catalog::Catalog load() const { return catalog::load_catalog(root_ / "catalog.json"); }

    void write(const std::string& relative, const std::string& bytes)
    {
        const fs::path path(relative);
        require(!path.is_absolute() && !path.has_root_name(), "fixture writes must be relative");
        for (const auto& segment : path) {
            require(segment != "..", "fixture writes must stay inside their directory");
        }
        std::ofstream output(root_ / path, std::ios::binary | std::ios::trunc);
        require(output.good(), "fixture must be writable: " + relative);
        output << bytes;
        output.close();
        require(output.good(), "fixture write must finish: " + relative);
    }

    void replace(const std::string& relative, const std::string& before, const std::string& after)
    {
        auto bytes = read(root_ / relative);
        const auto matching_endings = [&bytes](const std::string& text) {
            if (bytes.find("\r\n") == std::string::npos) { return text; }
            std::string converted;
            for (std::size_t i = 0; i < text.size(); ++i) {
                if (text[i] == '\n' && (i == 0 || text[i - 1] != '\r')) { converted += '\r'; }
                converted += text[i];
            }
            return converted;
        };
        const auto search = matching_endings(before);
        const auto replacement = matching_endings(after);
        const auto offset = bytes.find(search);
        require(offset != std::string::npos, "mutation anchor missing in " + relative + ": " + before);
        bytes.replace(offset, search.size(), replacement);
        write(relative, bytes);
    }

    void reject(const std::string& context) const
    {
        std::string rejection;
        try { (void)load(); }
        catch (const std::exception& error) { rejection = error.what(); }
        require(!rejection.empty(), "invalid catalog must be rejected");
        require(rejection.find(context) != std::string::npos,
            "rejection must identify " + context + "; received: " + rejection);
    }

private:
    void clean() noexcept
    {
        // Only the uniquely created child of the captured temporary directory
        // belongs to this fixture, including when fixture construction fails.
        if (root_.empty() || root_.parent_path() != temp_parent_
            || !root_.filename().string().starts_with("simcore-vehicle-catalog-")) { return; }
        std::error_code ignored;
        fs::remove_all(root_, ignored);
    }

    fs::path root_, temp_parent_;
};

void rejected_mutation(const char* path, const std::string& before,
                       const std::string& after, const char* context)
{
    Fixture fixture;
    fixture.replace(path, before, after);
    fixture.reject(context);
}

void test_valid_catalog()
{
    const auto data = catalog::load_catalog(SIMCORE_TEST_VEHICLE_CATALOG_PATH);
    require(data.vehicles().size() == 1 && data.parts().size() == 11 && data.assemblies().size() == 2,
        "tracked catalog must contain one chassis, eleven parts and two assemblies");
    require(data.checksum().starts_with("fnv1a64:") && data.checksum().size() == 24,
        "catalog must expose a stable content checksum");
    const auto resolved = data.resolve("sedan_standard");
    require(resolved.vehicle_id == "sedan_reference" && resolved.category == "sedan"
            && resolved.dynamics_model == "four_wheel", "assembly must retain definition identity");
    require(resolved.catalog_checksum == data.checksum(), "resolved assembly must retain catalog checksum");
    require(resolved.front_axle.wheel_count == 2 && resolved.rear_axle.wheel_count == 2,
        "four-wheel assembly must install two wheels per axle");
    near(resolved.wheelbase_m, 2.7, "wheelbase must derive from axle positions");
    near(resolved.peak_engine_torque_nm, 200.0, "peak torque must derive from the curve");
    require(resolved.peak_engine_power_w > 100000 && resolved.peak_engine_power_w < 110000,
        "peak power must derive from torque times angular speed");
    near(resolved.engine.idle_rpm, 800.0, "engine module must resolve");
    near(resolved.fuel_tank.capacity_l, 50.0, "tank module must resolve without adding fuel mass");
}

void test_mass_properties()
{
    const auto resolved = catalog::load_catalog(SIMCORE_TEST_VEHICLE_CATALOG_PATH).resolve("sedan_standard");
    near(resolved.mass.dry_mass_kg, 1470.0, "every module must contribute mass exactly once per installation");
    near(resolved.mass.center_of_mass_m[0], 182.5 / 1470.0, "longitudinal CG must use module mount positions");
    near(resolved.mass.center_of_mass_m[1], 4.0 / 1470.0, "lateral CG must retain asymmetric steering mass");
    near(resolved.mass.center_of_mass_m[2], 748.9 / 1470.0, "vertical CG must use chassis and all module heights");
    near(resolved.front_static_load_fraction, (1.35 + 182.5 / 1470.0) / 2.7,
        "static axle load must derive from the assembled CG");

    const auto& tensor = resolved.mass.inertia_tensor_kg_m2;
    for (int row = 0; row < 3; ++row) {
        require(std::isfinite(tensor[row * 3 + row]) && tensor[row * 3 + row] > 0,
            "assembled inertia diagonal must be finite and positive");
        for (int column = 0; column < 3; ++column) {
            near(tensor[row * 3 + column], tensor[column * 3 + row], "inertia tensor must be symmetric");
        }
    }
    require(tensor[0] > 600 && tensor[4] > 1800 && tensor[8] > 2300,
        "module inertia and parallel-axis terms must contribute");
    const double leading_minor = tensor[0] * tensor[4] - tensor[1] * tensor[3];
    const double determinant = tensor[0] * (tensor[4] * tensor[8] - tensor[5] * tensor[7])
        - tensor[1] * (tensor[3] * tensor[8] - tensor[5] * tensor[6])
        + tensor[2] * (tensor[3] * tensor[7] - tensor[4] * tensor[6]);
    require(leading_minor > 0 && determinant > 0, "assembled tensor must be positive definite");
    require(std::abs(tensor[1]) > 0.01 && std::abs(tensor[2]) > 0.01,
        "asymmetric mounting must retain off-diagonal inertia terms");
}

void test_part_swapping()
{
    const auto data = catalog::load_catalog(SIMCORE_TEST_VEHICLE_CATALOG_PATH);
    auto normal = data.resolve("sedan_standard");
    const auto softer = data.resolve("sedan_comfort");
    require(normal.vehicle_id == softer.vehicle_id, "loadouts must share one chassis");
    near(normal.mass.dry_mass_kg, softer.mass.dry_mass_kg, "equal-mass parts must preserve dry mass");
    near(normal.peak_engine_torque_nm, softer.peak_engine_torque_nm, "tire swap must not replace the engine");
    require(softer.front_axle.tire.friction_coefficient < normal.front_axle.tire.friction_coefficient
            && softer.rear_axle.tire.cornering_stiffness_n_rad < normal.rear_axle.tire.cornering_stiffness_n_rad,
        "each axle must resolve its selected tire");
    require(softer.front_axle.suspension.spring_rate_n_per_m < normal.front_axle.suspension.spring_rate_n_per_m
            && softer.rear_axle.suspension.damper_rate_n_s_per_m < normal.rear_axle.suspension.damper_rate_n_s_per_m,
        "each axle must resolve its selected suspension");
    normal.engine.idle_rpm = 1;
    near(data.resolve("sedan_standard").engine.idle_rpm, 800.0,
        "resolved instance edits must not mutate shared catalog definitions");
}

void test_deterministic_checksum()
{
    const auto baseline = catalog::load_catalog(SIMCORE_TEST_VEHICLE_CATALOG_PATH).checksum();
    Fixture fixture;
    require(fixture.load().checksum() == baseline, "catalog checksum must not depend on absolute directory");
    fixture.replace("catalog.json", "\"assemblies/sedan_standard.json\", \"assemblies/sedan_comfort.json\"",
        "\"assemblies/sedan_comfort.json\", \"assemblies/sedan_standard.json\"");
    fixture.replace("catalog.json", "\"parts/tire_road_standard.json\",\n    \"parts/tire_road_comfort.json\"",
        "\"parts/tire_road_comfort.json\",\n    \"parts/tire_road_standard.json\"");
    require(fixture.load().checksum() == baseline, "manifest enumeration order must not change checksum");
    fixture.replace(tire, "\"friction_coefficient\": 0.95", "\"friction_coefficient\": 0.96");
    require(fixture.load().checksum() != baseline, "part content changes must change checksum");
}

void raise_spring_capacity(Fixture& fixture)
{
    fixture.replace(suspension, "\"spring_rate_n_per_m\": 35000.0", "\"spring_rate_n_per_m\": 100000.0");
    fixture.replace(comfort_suspension, "\"spring_rate_n_per_m\": 30000.0", "\"spring_rate_n_per_m\": 100000.0");
}

void test_single_track_assembly()
{
    Fixture fixture;
    fixture.replace(chassis, "\"dynamics_model\": \"four_wheel\"", "\"dynamics_model\": \"single_track\"");
    fixture.replace(chassis, "\"track_m\": 1.58", "\"track_m\": 0.0");
    fixture.replace(chassis, "\"track_m\": 1.58", "\"track_m\": 0.0");
    fixture.replace(chassis, "[0.8, 0.4, 0.75]", "[0.8, 0.0, 0.75]");
    // This deliberately reuses the heavy reference chassis. Raise capacities
    // so the topology test is independent of ordinary motorcycle sizing.
    for (const auto* path : {tire, comfort_tire}) {
        fixture.replace(path, "\"rated_load_n\": 7000.0", "\"rated_load_n\": 10000.0");
    }
    raise_spring_capacity(fixture);
    const auto resolved = fixture.load().resolve("sedan_standard");
    require(resolved.front_axle.wheel_count == 1 && resolved.rear_axle.wheel_count == 1,
        "single-track topology must install exactly two physical wheel assemblies");
    near(resolved.mass.dry_mass_kg, 1390.0, "single-track parts must not be counted as four physical wheels");
    near(resolved.mass.center_of_mass_m[1], 0.0, "single-track assembly must balance around its support line");
    require(resolved.category == "sedan", "category labels must not override the selected dynamics topology");
}

void test_power_peak_inside_curve_segment()
{
    Fixture fixture;
    fixture.replace(engine, "{\"rpm\": 6500.0, \"torque_nm\": 140.0}",
        "{\"rpm\": 6500.0, \"torque_nm\": 150.0}");
    const auto resolved = fixture.load().resolve("sedan_standard");
    // On the final segment T(rpm) = 345 - 0.03 * rpm. The derivative
    // of rpm*T(rpm) vanishes at 5750 rpm, strictly between its samples.
    const double expected = 5750.0 * 172.5 * 2.0 * std::numbers::pi / 60.0;
    near(resolved.peak_engine_power_w, expected, "power peak must include the interior of interpolated torque segments");
    near(resolved.peak_engine_torque_nm, 200.0, "interior power peak must not change peak torque validation");
}

using Test = std::pair<const char*, std::function<void()>>;

std::vector<Test> tests()
{
    return {
        {"valid catalog", test_valid_catalog},
        {"assembled mass and inertia", test_mass_properties},
        {"shared parts and independent loadouts", test_part_swapping},
        {"deterministic content checksum", test_deterministic_checksum},
        {"single-track physical wheel count", test_single_track_assembly},
        {"interpolated engine power peak", test_power_peak_inside_curve_segment},
        {"category labels do not select physics", [] {
            for (const auto* category : {"suv", "compact", "truck", "bus", "motorcycle"}) {
                Fixture fixture;
                fixture.replace(chassis, "\"category\": \"sedan\"",
                    std::string("\"category\": \"") + category + "\"");
                const auto resolved = fixture.load().resolve("sedan_standard");
                require(resolved.category == category && resolved.dynamics_model == "four_wheel",
                    "body categories must be metadata independent from the supported dynamics model");
                near(resolved.mass.dry_mass_kg, 1470.0, "category name alone must not change installed part masses");
            }
        }},
        {"lateral CG outside support polygon", [] {
            rejected_mutation(chassis, "[0.0, 0.0, 0.55]", "[0.0, 1.5, 0.55]", "support polygon");
        }},
        {"heavy-side tire load", [] {
            Fixture fixture;
            fixture.replace(chassis, "[0.0, 0.0, 0.55]", "[0.0, 0.98, 0.55]");
            raise_spring_capacity(fixture);
            fixture.reject("rated_load_n");
        }},
        {"spring travel capacity", [] {
            rejected_mutation(suspension, "\"spring_rate_n_per_m\": 35000.0",
                "\"spring_rate_n_per_m\": 20000.0", "spring compression capacity");
        }},
        {"unknown field", [] {
            rejected_mutation(engine, "\"idle_rpm\":", "\"idle_rpmm\":", "unknown field");
        }},
        {"missing field", [] {
            rejected_mutation(engine, "\"idle_rpm\": 800.0,", "", "missing field 'idle_rpm'");
        }},
        {"number type fidelity", [] {
            for (const auto* value : {"\"800.0\"", "true", "null"}) {
                rejected_mutation(engine, "\"idle_rpm\": 800.0", std::string("\"idle_rpm\": ") + value,
                    "idle_rpm");
            }
        }},
        {"negative mass", [] {
            rejected_mutation(engine, "\"mass_kg\": 160.0", "\"mass_kg\": -160.0", "mass_kg");
        }},
        {"nonfinite number", [] {
            rejected_mutation(engine, "\"mass_kg\": 160.0", "\"mass_kg\": 1e999", "engine_gasoline_2_0.json");
        }},
        {"schema versions", [] {
            for (const auto* value : {"0", "2", "1.5", "\"1\""}) {
                rejected_mutation("catalog.json", "\"schema_version\": 1",
                    std::string("\"schema_version\": ") + value, "schema_version");
            }
            rejected_mutation(engine, "\"schema_version\": 1", "\"schema_version\": 2", "schema_version");
        }},
        {"duplicate JSON keys", [] {
            rejected_mutation(engine, "\"idle_rpm\": 800.0", "\"idle_rpm\": 800.0, \"idle_rpm\": 900.0", "invalid JSON");
            rejected_mutation(engine, "\"idle_rpm\": 800.0", "\"idle_rpm\": 800.0, \"idle_\\u0072pm\": 900.0", "invalid JSON");
        }},
        {"unsupported part kind", [] {
            rejected_mutation(engine, "\"kind\": \"engine\"", "\"kind\": \"reactor\"", "unsupported part kind");
        }},
        {"missing referenced part", [] {
            rejected_mutation(standard, "\"engine\": \"engine_gasoline_2_0\"", "\"engine\": \"missing_engine\"", "missing_engine");
        }},
        {"wrong part kind", [] {
            rejected_mutation(standard, "\"engine\": \"engine_gasoline_2_0\"", "\"engine\": \"fuel_tank_50l\"", "fuel_tank_50l");
        }},
        {"missing chassis", [] {
            rejected_mutation(standard, "\"vehicle_id\": \"sedan_reference\"", "\"vehicle_id\": \"missing_chassis\"", "missing_chassis");
        }},
        {"hub interface mismatch", [] {
            rejected_mutation(wheel, "\"interface_id\": \"wheel_hub_sedan\"", "\"interface_id\": \"wheel_hub_truck\"", "wheel");
        }},
        {"rim diameter mismatch", [] {
            rejected_mutation(wheel, "\"rim_diameter_m\": 0.4064", "\"rim_diameter_m\": 0.45", "rim");
        }},
        {"rim width mismatch", [] {
            rejected_mutation(wheel, "\"rim_width_m\": 0.18", "\"rim_width_m\": 0.22", "rim");
        }},
        {"tire radius clearance", [] {
            rejected_mutation(tire, "\"radius_m\": 0.32", "\"radius_m\": 0.36", "tire");
        }},
        {"tire width clearance", [] {
            rejected_mutation(tire, "\"width_m\": 0.215", "\"width_m\": 0.25", "tire");
        }},
        {"transmission torque rating", [] {
            rejected_mutation(transmission, "\"max_input_torque_nm\": 300.0", "\"max_input_torque_nm\": 150.0", "torque");
        }},
        {"fuel compatibility", [] {
            rejected_mutation(tank, "\"fuel_type\": \"gasoline\"", "\"fuel_type\": \"diesel\"", "fuel");
        }},
        {"manifest traversal", [] {
            rejected_mutation("catalog.json", "chassis/sedan_reference.json", "../sedan_reference.json", "traversal");
        }},
        {"manifest absolute path", [] {
            rejected_mutation("catalog.json", "chassis/sedan_reference.json", "C:/sedan_reference.json", "relative");
        }},
        {"manifest duplicate file", [] {
            rejected_mutation("catalog.json", "\"chassis/sedan_reference.json\"",
                "\"chassis/sedan_reference.json\", \"chassis/sedan_reference.json\"", "duplicate source file");
        }},
        {"duplicate definition ID", [] {
            rejected_mutation(comfort, "\"id\": \"sedan_comfort\"", "\"id\": \"sedan_standard\"", "duplicate ID");
        }},
        {"invalid rigid-body inertia", [] {
            rejected_mutation(engine, "[10.0, 14.0, 14.0]", "[100.0, 14.0, 14.0]", "triangle inequality");
        }},
        {"torque curve ordering", [] {
            rejected_mutation(engine, "\"rpm\": 1500.0", "\"rpm\": 800.0", "strictly increase");
        }},
        {"torque curve coverage", [] {
            rejected_mutation(engine, "\"rpm\": 6500.0", "\"rpm\": 6400.0", "span");
        }},
        {"gear ratio ordering", [] {
            rejected_mutation(transmission, "[3.63, 2.09, 1.35, 1.0, 0.82, 0.69]",
                "[3.63, 4.0, 1.35, 1.0, 0.82, 0.69]", "strictly decrease");
        }},
        {"suspension travel", [] {
            rejected_mutation(suspension, "\"max_compression_m\": 0.12", "\"max_compression_m\": 0.31", "compression");
        }},
        {"dynamics topology", [] {
            rejected_mutation(chassis, "\"dynamics_model\": \"four_wheel\"", "\"dynamics_model\": \"single_track\"", "track");
        }},
        {"all assemblies validated atomically", [] {
            rejected_mutation(comfort, "\"engine\": \"engine_gasoline_2_0\"", "\"engine\": \"missing_engine\"", "missing_engine");
        }},
        {"unknown assembly lookup", [] {
            const auto data = catalog::load_catalog(SIMCORE_TEST_VEHICLE_CATALOG_PATH);
            bool rejected = false;
            try { (void)data.resolve("missing_assembly"); }
            catch (const std::exception&) { rejected = true; }
            require(rejected, "unknown assembly lookup must not fall back to a default vehicle");
        }}
    };
}

} // namespace

int main()
{
    std::size_t passed = 0;
    for (const auto& [name, test] : tests()) {
        try {
            test();
            ++passed;
        } catch (const std::exception& error) {
            std::cerr << "vehicle_catalog_tests: FAILED " << name << ": " << error.what() << '\n';
            return 1;
        }
    }
    std::cout << "vehicle_catalog_tests: " << passed << " cases passed\n";
    return 0;
}
