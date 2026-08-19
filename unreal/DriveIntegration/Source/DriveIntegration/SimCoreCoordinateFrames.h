#pragma once

#include "CoreMinimal.h"

namespace SimCoreCoordinateFrames
{
	// The prototype Unreal world uses X=North, Y=East, Z=Up in centimeters.
	DRIVEINTEGRATION_API FVector EnuMetersToUnrealCentimeters(
		const FVector3d& PositionEnuMeters,
		const FVector& PresentationOffsetCentimeters);

	// Converts base_link FLU velocity into the navigation ENU frame.
	// The returned vector is ordered X=East, Y=North.
	DRIVEINTEGRATION_API FVector2d BodyFluVelocityToEnu(
		const FVector3d& LinearVelocityBody,
		double HeadingDegrees);

	// This preserves the current schema-v1 attitude semantics. Steering and yaw
	// sign migration from ADR-011 must be performed as a separate schema change.
	DRIVEINTEGRATION_API FRotator BuildUnrealActorRotation(
		float HeadingDegrees,
		float PitchDegrees,
		float RollDegrees,
		float NavigationYawRateRadPerSecond,
		const FVector3d& AngularVelocityBody,
		double PredictionSeconds);
}
