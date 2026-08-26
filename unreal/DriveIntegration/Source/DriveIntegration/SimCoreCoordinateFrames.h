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

	// Unreal/device X input and local yaw are right-positive. Schema v2 road
	// steering is left-positive, so each boundary applies exactly one negation.
	DRIVEINTEGRATION_API float InputAxisToCanonicalSteering(float RightPositiveInput);
	DRIVEINTEGRATION_API float CanonicalSteeringToUnrealYawDegrees(
		float LeftPositiveSteeringRadians);

	// Navigation heading is clockwise-positive while schema-v2 angular velocity
	// is a true right-handed FLU vector and scalar yaw_rate remains its body-Z
	// component. At level attitude, nose-up pitch is negative body-Y and left-up
	// roll is positive body-X; compound attitude is converted back to Euler rates
	// before the limited presentation prediction.
	DRIVEINTEGRATION_API FRotator BuildUnrealActorRotation(
		float HeadingDegrees,
		float PitchDegrees,
		float RollDegrees,
		float BodyYawRateRadPerSecond,
		const FVector3d& AngularVelocityBody,
		double PredictionSeconds);
}
