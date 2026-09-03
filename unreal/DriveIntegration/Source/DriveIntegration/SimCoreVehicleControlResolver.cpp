#include "SimCoreVehicleControlResolver.h"

namespace SimCoreVehicleControlResolver
{
FOutput Resolve(const FInput& Input)
{
	FOutput Output;
	Output.Steering = Input.Steering;
	Output.bSideBrake = Input.bSideBrake;
	Output.Gear = Input.SelectedGear;

	const bool bForwardPressed = Input.ForwardPedal > KINDA_SMALL_NUMBER;
	const bool bReversePressed = Input.ReversePedal > KINDA_SMALL_NUMBER;
	if (bForwardPressed && bReversePressed)
	{
		// Conflicting pedals can never create drive torque.
		Output.Brake = FMath::Max(Input.ForwardPedal, Input.ReversePedal);
	}
	else if (bForwardPressed)
	{
		if (Output.Gear == SimCoreProtocol::EVehicleGear::Drive)
		{
			// W remains usable with a stale state only when it cannot change gear.
			Output.Throttle = !Input.bHasFreshAuthoritativeState
				|| Input.AuthoritativeLongitudinalSpeedMps
					>= -Input.DirectionChangeSpeedMps
				? Input.ForwardPedal
				: 0.0f;
			Output.Brake = Output.Throttle > 0.0f ? 0.0f : Input.ForwardPedal;
		}
		else if (Input.bHasFreshAuthoritativeState
			&& Input.AuthoritativeLongitudinalSpeedMps
				>= -Input.DirectionChangeSpeedMps)
		{
			Output.Gear = SimCoreProtocol::EVehicleGear::Drive;
			Output.Throttle = Input.ForwardPedal;
		}
		else
		{
			// While reversing, W is the service brake until nearly stopped.
			Output.Brake = Input.ForwardPedal;
		}
	}
	else if (bReversePressed)
	{
		if (Output.Gear == SimCoreProtocol::EVehicleGear::Reverse)
		{
			Output.Throttle = !Input.bHasFreshAuthoritativeState
				|| Input.AuthoritativeLongitudinalSpeedMps
					<= Input.DirectionChangeSpeedMps
				? Input.ReversePedal
				: 0.0f;
			Output.Brake = Output.Throttle > 0.0f ? 0.0f : Input.ReversePedal;
		}
		else if (Input.bHasFreshAuthoritativeState
			&& Input.AuthoritativeLongitudinalSpeedMps
				<= Input.DirectionChangeSpeedMps)
		{
			Output.Gear = SimCoreProtocol::EVehicleGear::Reverse;
			Output.Throttle = Input.ReversePedal;
		}
		else
		{
			// While moving forward, S is the service brake until nearly stopped.
			Output.Brake = Input.ReversePedal;
		}
	}

	return Output;
}
}
