#pragma once

#include "CoreMinimal.h"
#include "SimCoreProtocol.h"

namespace SimCorePresentation
{
	inline constexpr int32 VehicleWheelCount = 4;

	struct FWheelGroundPresentationSample
	{
		FVector RelativeCenterLocationCm = FVector::ZeroVector;
		FVector RelativeContactNormal = FVector::UpVector;
		bool bHasGroundContact = false;
	};

	struct FVehiclePresentationSample
	{
		FVector ActorLocation = FVector::ZeroVector;
		FRotator ActorRotation = FRotator::ZeroRotator;
		float FrontAxleAngularSpeedRadPerSecond = 0.0f;
		float RearAxleAngularSpeedRadPerSecond = 0.0f;
		bool bStateStale = false;
		TStaticArray<FWheelGroundPresentationSample, VehicleWheelCount> Wheels;
	};

	struct FRuntimeEntityPresentationSample
	{
		FVector ActorLocation = FVector::ZeroVector;
		FRotator ActorRotation = FRotator::ZeroRotator;
		FVector ActorScale = FVector::OneVector;
	};

	DRIVEINTEGRATION_API FVehiclePresentationSample BuildVehicleSample(
		const SimCoreProtocol::FVehicleState& State,
		float StateAgeSeconds,
		float MaxExtrapolationSeconds,
		float StateStaleTimeoutSeconds,
		float VisualWheelStopSpeedMps,
		float VisualTireRadiusMeters,
		const FVector& PresentationOffsetCentimeters);

	DRIVEINTEGRATION_API bool BuildRuntimeEntitySample(
		const SimCoreProtocol::FVehicleState& State,
		float StateAgeSeconds,
		float MaxExtrapolationSeconds,
		const FVector& PresentationOffsetCentimeters,
		FRuntimeEntityPresentationSample& OutSample);

	DRIVEINTEGRATION_API float AdvanceWheelSpinDegrees(
		float CurrentSpinDegrees,
		float AngularSpeedRadPerSecond,
		float DeltaSeconds);

	DRIVEINTEGRATION_API FRotator BuildWheelPivotRelativeRotation(
		const SimCoreProtocol::FVehicleState::FWheelState& WheelState,
		const FVector& RelativeContactNormal);

	DRIVEINTEGRATION_API FRotator BuildWheelSpinRelativeRotation(
		float AxleSpinDegrees);
}
