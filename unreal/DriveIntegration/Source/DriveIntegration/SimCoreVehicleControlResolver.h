#pragma once

#include "CoreMinimal.h"
#include "SimCoreProtocol.h"

namespace SimCoreVehicleControlResolver
{
	struct FInput
	{
		float ForwardPedal = 0.0f;
		float ReversePedal = 0.0f;
		float Steering = 0.0f;
		bool bSideBrake = false;
		SimCoreProtocol::EVehicleGear SelectedGear =
			SimCoreProtocol::EVehicleGear::Drive;
		bool bHasFreshAuthoritativeState = false;
		float AuthoritativeLongitudinalSpeedMps = 0.0f;
		float DirectionChangeSpeedMps = 0.20f;
	};

	struct FOutput
	{
		float Throttle = 0.0f;
		float Brake = 0.0f;
		float Steering = 0.0f;
		bool bSideBrake = false;
		SimCoreProtocol::EVehicleGear Gear =
			SimCoreProtocol::EVehicleGear::Drive;
	};

	/** Pure W/S pedal arbitration and near-standstill direction selection. */
	DRIVEINTEGRATION_API FOutput Resolve(const FInput& Input);
}
