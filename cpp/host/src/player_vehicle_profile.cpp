#include "player_vehicle_profile.hpp"
#include "vehicle_catalog/runtime_vehicle_catalog.hpp"

namespace simcore_host {

const char* runtime_vehicle_class_name(RuntimeVehicleClass vehicle_class)
{
    switch (vehicle_class) {
    case RuntimeVehicleClass::Unspecified: return "unspecified";
    case RuntimeVehicleClass::Sedan: return "sedan";
    case RuntimeVehicleClass::Compact: return "compact";
    case RuntimeVehicleClass::Truck: return "truck";
    case RuntimeVehicleClass::Motorcycle: return "motorcycle";
    }
    return "invalid";
}

VehicleParameters make_player_vehicle_parameters(
    const VehicleParameters& sedan, RuntimeVehicleClass vehicle_class)
{
    return default_runtime_vehicle_catalog().player_parameters(sedan, vehicle_class);
}

} // namespace simcore_host
