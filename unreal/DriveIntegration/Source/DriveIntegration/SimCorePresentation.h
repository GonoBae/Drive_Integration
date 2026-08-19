#pragma once

#include "CoreMinimal.h"
#include "SimCoreProtocol.h"

namespace SimCorePresentation
{
	struct FVehiclePresentationSample
	{
		FVector ActorLocation = FVector::ZeroVector;
		FRotator ActorRotation = FRotator::ZeroRotator;
		float FrontAxleAngularSpeedRadPerSecond = 0.0f;
		float RearAxleAngularSpeedRadPerSecond = 0.0f;
		bool bStateStale = false;
	};

	DRIVEINTEGRATION_API FVehiclePresentationSample BuildVehicleSample(
		const SimCoreProtocol::FVehicleState& State,
		float StateAgeSeconds,
		float MaxExtrapolationSeconds,
		float StateStaleTimeoutSeconds,
		float VisualWheelStopSpeedMps,
		const FVector& PresentationOffsetCentimeters);

	DRIVEINTEGRATION_API float AdvanceWheelSpinDegrees(
		float CurrentSpinDegrees,
		float AngularSpeedRadPerSecond,
		float DeltaSeconds);

	DRIVEINTEGRATION_API FRotator BuildWheelRelativeRotation(
		const SimCoreProtocol::FVehicleState::FWheelState& WheelState,
		float AxleSpinDegrees);
}
