#include "vehicle_catalog/vehicle_catalog.hpp"
#include "vehicle_catalog/runtime_vehicle_catalog.hpp"
#include "physics/vehicle_config.hpp"

#include <iomanip>
#include <algorithm>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv)
{
    if ((argc == 4 || argc == 5) && std::string(argv[1]) == "--runtime") {
        try {
            const auto catalog = simcore_host::load_runtime_vehicle_catalog(argv[2]);
            const auto base = simcore_host::load_vehicle_parameters(argv[3]);
            std::cout << "Runtime catalog VALID: " << catalog.checksum() << "\n";
            const auto report = [&](const simcore_host::RuntimeVehicleProfile& definition, std::string_view loadout_id) {
                const auto p = catalog.player_parameters(base.parameters, definition.vehicle_class, loadout_id);
                std::cout << definition.id << " class=" << static_cast<unsigned>(definition.vehicle_class)
                          << " mass=" << p.mass_kg << "kg wheelbase=" << p.wheelbase_m
                          << "m tire=" << p.tire_radius_m << "m legacy_drive=" << p.max_drive_force_n
                          << "N legacy_power=" << p.max_drive_power_w / 1000 << "kW npc_turn="
                          << definition.npc.minimum_turn_radius_m << "m\n";
                const auto report_axle = [&](const char* name, std::size_t wheel,
                        const simcore_host::RuntimeAxleModuleSelection& modules) {
                    const auto tire = resolved_tire_parameters(p, wheel);
                    const auto& suspension = resolved_suspension_parameters(p, wheel);
                    std::cout << "  " << name << " tire=" << modules.tire.value_or("<inherited>")
                              << " suspension=" << modules.suspension.value_or("<inherited>")
                              << " radius=" << tire.radius_m << "m friction=" << tire.friction_coefficient
                              << " spring=" << suspension.spring_rate_n_per_m << "N/m damping="
                              << suspension.damper_rate_n_s_per_m << "Ns/m per contact slot\n";
                };
                report_axle("front", 0, definition.front_axle_modules);
                report_axle("rear", 2, definition.rear_axle_modules);
                if (definition.powertrain_modules && p.powertrain) {
                    const auto& selected = *definition.powertrain_modules;
                    const auto& powertrain = *p.powertrain;
                    const auto peak = std::max_element(powertrain.engine.torque_curve.begin(), powertrain.engine.torque_curve.end(),
                        [](const auto& first, const auto& second) { return first.torque_nm < second.torque_nm; });
                    std::cout << "  powertrain engine=" << selected.engine << " transmission=" << selected.transmission
                              << " drivetrain=" << selected.drivetrain << " fuel_tank=" << selected.fuel_tank << '\n'
                              << "  engine_curve_peak=" << peak->torque_nm << "Nm RPM=" << powertrain.engine.idle_rpm
                              << ".." << powertrain.engine.max_rpm << " gears=" << powertrain.transmission.forward_ratios.size()
                              << " final_drive=" << powertrain.drivetrain.final_drive_ratio << " front_torque_share="
                              << powertrain.drivetrain.front_torque_fraction << '\n'
                              << "  fuel=" << powertrain.fuel_tank.initial_fuel_l << "/" << powertrain.fuel_tank.capacity_l
                              << "L idle_fuel=" << powertrain.engine.idle_fuel_lph << "L/h BSFC=" << powertrain.engine.bsfc_g_per_kwh
                              << "g/kWh shift_down/up=" << powertrain.transmission.downshift_rpm << "/"
                              << powertrain.transmission.upshift_rpm << "rpm duration=" << powertrain.transmission.shift_duration_s
                              << "s (legacy force/power caps bypassed)\n";
                } else {
                    std::cout << "  powertrain=<legacy>\n";
                }
            };
            const auto report_loadout = [&](const std::string& id) {
                const auto found = catalog.loadouts().find(id);
                if (found == catalog.loadouts().end()) throw std::invalid_argument("unknown runtime loadout '" + id + "'");
                const auto& selected = found->second;
                auto definition = catalog.at(selected.vehicle_class);
                definition.id = selected.id;
                definition.front_axle_modules = selected.front_axle_modules;
                definition.rear_axle_modules = selected.rear_axle_modules;
                definition.powertrain_modules = selected.powertrain_modules;
                std::cout << "Loadout: " << selected.name << '\n';
                report(definition, selected.id);
            };
            if (argc == 5) report_loadout(argv[4]);
            else {
                for (const auto& definition : catalog.fleet()) report(definition, {});
                for (const auto& [id, definition] : catalog.loadouts()) report_loadout(id);
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "Runtime catalog INVALID: " << error.what() << '\n';
            return 1;
        }
    }
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: simcore_vehicle_catalog <catalog.json> [assembly_id]\n"
                  << "   or: simcore_vehicle_catalog --runtime <catalog.json> <vehicle_sedan.cfg> [loadout_id]\n";
        return 2;
    }
    try {
        const auto catalog = simcore_host::vehicle_catalog::load_catalog(argv[1]);
        std::cout << "Catalog VALID: " << catalog.vehicles().size() << " chassis, "
                  << catalog.parts().size() << " parts, " << catalog.assemblies().size()
                  << " assemblies; " << catalog.checksum() << '\n';
        std::cout << "Authoring validation only; these definitions are not connected to live driving yet.\n";
        std::cout << std::fixed << std::setprecision(3);
        const auto report = [&](const std::string& id) {
            const auto vehicle = catalog.resolve(id);
            std::cout << "\n" << id << " [" << vehicle.vehicle_id << ", " << vehicle.category
                      << ", " << vehicle.dynamics_model << "]\n"
                      << "  dry mass: " << vehicle.mass.dry_mass_kg << " kg; wheelbase: " << vehicle.wheelbase_m << " m\n"
                      << "  CG FLU: " << vehicle.mass.center_of_mass_m[0] << ", " << vehicle.mass.center_of_mass_m[1]
                      << ", " << vehicle.mass.center_of_mass_m[2] << " m; front static share: " << vehicle.front_static_load_fraction << '\n'
                      << "  engine: " << vehicle.peak_engine_torque_nm << " Nm / " << vehicle.peak_engine_power_w / 1000 << " kW\n"
                      << "  front/rear tire radius: " << vehicle.front_axle.tire.radius_m << " / " << vehicle.rear_axle.tire.radius_m << " m\n"
                      << "  front/rear tire friction: " << vehicle.front_axle.tire.friction_coefficient << " / " << vehicle.rear_axle.tire.friction_coefficient << '\n'
                      << "  front/rear spring: " << vehicle.front_axle.suspension.spring_rate_n_per_m << " / " << vehicle.rear_axle.suspension.spring_rate_n_per_m << " N/m\n";
        };
        if (argc == 3) { report(argv[2]); }
        else { for (const auto& [id, definition] : catalog.assemblies()) { report(id); } }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Catalog INVALID: " << error.what() << '\n';
        return 1;
    }
}
