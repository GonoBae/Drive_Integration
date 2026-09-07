#pragma once

#include "physics/vehicle_physics.hpp"
#include "protocol/vehicle_messages.hpp"

namespace simcore_host {

/** Builds one validated Ego dynamics/collision profile from the configured sedan baseline. */
VehicleParameters make_player_vehicle_parameters(
    const VehicleParameters& sedan,
    RuntimeVehicleClass vehicle_class);

const char* runtime_vehicle_class_name(RuntimeVehicleClass vehicle_class);

} // namespace simcore_host
