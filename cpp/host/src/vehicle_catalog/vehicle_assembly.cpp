#include "vehicle_catalog/vehicle_catalog.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace simcore_host::vehicle_catalog {
namespace {

struct InstalledMass { MassProperties properties; Vec3 position_m; };

class AssemblyResolver {
public:
    AssemblyResolver(const Catalog& catalog, const AssemblyDefinition& assembly)
        : catalog_(catalog), assembly_(assembly) {}

    [[noreturn]] void fail(const std::string& reason) const
    { throw std::runtime_error("Assembly '" + assembly_.id + "': " + reason); }

    template<typename Spec>
    const PartDefinition& part(const std::string& id, const std::string& slot,
                               const std::string& interface_id = {}) const
    {
        const auto found = catalog_.parts().find(id);
        if (found == catalog_.parts().end()) { fail(slot + ": unknown part '" + id + "'"); }
        if (!std::holds_alternative<Spec>(found->second.specification)) {
            fail(slot + ": wrong part kind for '" + id + "'");
        }
        if (!interface_id.empty() && found->second.interface_id != interface_id) {
            fail(slot + ": incompatible interface for '" + id + "', expected '" + interface_id + "'");
        }
        return found->second;
    }

    template<typename Spec>
    Spec install(const std::string& id, const char* slot, const Mount& mount)
    {
        const auto& module = part<Spec>(id, slot, mount.interface_id);
        masses_.push_back({module.mass, mount.position_m});
        return std::get<Spec>(module.specification);
    }

    ResolvedAxle install_axle(const AxleAssembly& assembly, const AxleDefinition& geometry,
                              const std::string& slot, unsigned count)
    {
        const auto& wheel = part<Wheel>(assembly.wheel, slot + ".wheel", geometry.wheel_interface);
        const auto& tire = part<Tire>(assembly.tire, slot + ".tire");
        const auto& suspension = part<Suspension>(assembly.suspension, slot + ".suspension", geometry.suspension_interface);
        const auto& brake = part<Brake>(assembly.brake, slot + ".brake", geometry.brake_interface);
        ResolvedAxle result{count, geometry, std::get<Wheel>(wheel.specification),
            std::get<Tire>(tire.specification), std::get<Suspension>(suspension.specification),
            std::get<Brake>(brake.specification)};
        if (std::abs(result.wheel.rim_diameter_m - result.tire.rim_diameter_m) > 1e-6
            || result.wheel.rim_width_m < result.tire.min_rim_width_m
            || result.wheel.rim_width_m > result.tire.max_rim_width_m) {
            fail(slot + ": tire and wheel rim dimensions are incompatible");
        }
        if (result.tire.radius_m < geometry.min_tire_radius_m
            || result.tire.radius_m > geometry.max_tire_radius_m
            || result.tire.width_m > geometry.max_tire_width_m) {
            fail(slot + ": tire exceeds chassis clearance limits");
        }
        for (unsigned i = 0; i < count; ++i) {
            const Vec3 position{geometry.x_m, count == 1 ? 0 : (i == 0 ? 0.5 : -0.5) * geometry.track_m,
                                geometry.wheel_center_z_m};
            for (const auto* module : {&wheel, &tire, &suspension, &brake}) {
                masses_.push_back({module->mass, position});
            }
        }
        return result;
    }

    ResolvedAssembly run()
    {
        const auto found = catalog_.vehicles().find(assembly_.vehicle_id);
        if (found == catalog_.vehicles().end()) { fail("unknown vehicle '" + assembly_.vehicle_id + "'"); }
        const auto& vehicle = found->second;
        masses_.push_back({vehicle.chassis_mass, {0, 0, 0}});
        ResolvedAssembly result{};
        result.id = assembly_.id;
        result.vehicle_id = vehicle.id;
        result.catalog_checksum = catalog_.checksum();
        result.category = vehicle.category;
        result.dynamics_model = vehicle.dynamics_model;
        result.body_dimensions_m = vehicle.body_dimensions_m;
        result.wheelbase_m = vehicle.front_axle.x_m - vehicle.rear_axle.x_m;
        result.drag_coefficient = vehicle.drag_coefficient;
        result.frontal_area_m2 = vehicle.frontal_area_m2;
        result.engine = install<Engine>(assembly_.engine, "engine", vehicle.engine);
        result.transmission = install<Transmission>(assembly_.transmission, "transmission", vehicle.transmission);
        result.drivetrain = install<Drivetrain>(assembly_.drivetrain, "drivetrain", vehicle.drivetrain);
        result.steering = install<Steering>(assembly_.steering, "steering", vehicle.steering);
        result.fuel_tank = install<FuelTank>(assembly_.fuel_tank, "fuel_tank", vehicle.fuel_tank);
        const unsigned count = vehicle.dynamics_model == "single_track" ? 1 : 2;
        result.front_axle = install_axle(assembly_.front_axle, vehicle.front_axle, "front_axle", count);
        result.rear_axle = install_axle(assembly_.rear_axle, vehicle.rear_axle, "rear_axle", count);
        result.mass = combine_mass();
        result.front_static_load_fraction = (result.mass.center_of_mass_m[0] - vehicle.rear_axle.x_m) / result.wheelbase_m;
        if (result.front_static_load_fraction <= 0 || result.front_static_load_fraction >= 1
            || result.mass.center_of_mass_m[2] <= 0) {
            fail("assembled centre of mass must be above ground and between axles");
        }
        for (const auto& point : result.engine.torque_curve) {
            result.peak_engine_torque_nm = std::max(result.peak_engine_torque_nm, point.torque_nm);
        }
        result.peak_engine_power_w = peak_power(result.engine);
        if (result.peak_engine_torque_nm > result.transmission.max_input_torque_nm) {
            fail("engine peak torque exceeds transmission max_input_torque_nm");
        }
        if (result.engine.fuel_type != result.fuel_tank.fuel_type) { fail("engine and fuel_tank fuel_type mismatch"); }
        const double effective_track = result.front_static_load_fraction * vehicle.front_axle.track_m
            + (1 - result.front_static_load_fraction) * vehicle.rear_axle.track_m;
        const double cg_y = result.mass.center_of_mass_m[1];
        if ((count == 1 && std::abs(cg_y) > 1e-6)
            || (count == 2 && std::abs(cg_y) >= 0.5 * effective_track)) {
            fail("assembled centre of mass is outside wheel support polygon");
        }
        // A statically admissible distribution on level ground. Suspension roll
        // stiffness and dynamic load transfer are outside this authoring check.
        const double heaviest_side_fraction = count == 1 ? 1 : 0.5 + std::abs(cg_y) / effective_track;
        validate_static_load(result.front_axle, result.mass.dry_mass_kg * result.front_static_load_fraction,
                             heaviest_side_fraction, "front_axle");
        validate_static_load(result.rear_axle, result.mass.dry_mass_kg * (1 - result.front_static_load_fraction),
                             heaviest_side_fraction, "rear_axle");
        return result;
    }

private:
    AssembledMass combine_mass() const
    {
        AssembledMass result{};
        for (const auto& installed : masses_) {
            const auto& mass = installed.properties;
            result.dry_mass_kg += mass.mass_kg;
            for (int axis = 0; axis < 3; ++axis) {
                result.center_of_mass_m[axis] += mass.mass_kg * (installed.position_m[axis] + mass.center_of_mass_m[axis]);
            }
        }
        for (auto& coordinate : result.center_of_mass_m) { coordinate /= result.dry_mass_kg; }
        for (const auto& installed : masses_) {
            const auto& mass = installed.properties;
            Vec3 offset{};
            double squared_distance = 0;
            for (int axis = 0; axis < 3; ++axis) {
                offset[axis] = installed.position_m[axis] + mass.center_of_mass_m[axis] - result.center_of_mass_m[axis];
                squared_distance += offset[axis] * offset[axis];
            }
            for (int row = 0; row < 3; ++row) {
                for (int column = 0; column < 3; ++column) {
                    double term = -mass.mass_kg * offset[row] * offset[column];
                    if (row == column) { term += mass.inertia_diagonal_kg_m2[row] + mass.mass_kg * squared_distance; }
                    result.inertia_tensor_kg_m2[row * 3 + column] += term;
                }
            }
        }
        return result;
    }

    static double peak_power(const Engine& engine)
    {
        // Torque is linear between samples. RPM * torque can peak inside a segment.
        double peak = 0;
        const auto consider = [&peak](double rpm, double torque) {
            peak = std::max(peak, rpm * torque * 2 * std::numbers::pi / 60);
        };
        for (std::size_t i = 1; i < engine.torque_curve.size(); ++i) {
            const auto a = engine.torque_curve[i - 1];
            const auto b = engine.torque_curve[i];
            consider(a.rpm, a.torque_nm);
            consider(b.rpm, b.torque_nm);
            const double slope = (b.torque_nm - a.torque_nm) / (b.rpm - a.rpm);
            if (slope < 0) {
                const double intercept = a.torque_nm - slope * a.rpm;
                const double rpm = -intercept / (2 * slope);
                if (rpm > a.rpm && rpm < b.rpm) { consider(rpm, slope * rpm + intercept); }
            }
        }
        return peak;
    }

    void validate_static_load(const ResolvedAxle& axle, double axle_mass, double side_fraction, const char* slot) const
    {
        const double load = axle_mass * 9.80665 * side_fraction;
        if (load > axle.tire.rated_load_n) { fail(std::string(slot) + ": dry static load exceeds tire rated_load_n"); }
        if (load > axle.suspension.max_force_n) { fail(std::string(slot) + ": dry static load exceeds suspension max_force_n"); }
        if (load > axle.suspension.spring_rate_n_per_m * axle.suspension.max_compression_m) {
            fail(std::string(slot) + ": dry static load exceeds spring compression capacity (no preload)");
        }
    }

    const Catalog& catalog_;
    const AssemblyDefinition& assembly_;
    std::vector<InstalledMass> masses_;
};

} // namespace

ResolvedAssembly Catalog::resolve(const std::string& assembly_id) const
{
    const auto found = assemblies_.find(assembly_id);
    if (found == assemblies_.end()) { throw std::runtime_error("Unknown assembly '" + assembly_id + "'"); }
    return AssemblyResolver(*this, found->second).run();
}

} // namespace simcore_host::vehicle_catalog
