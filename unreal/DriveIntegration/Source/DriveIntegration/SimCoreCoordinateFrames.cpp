#include "SimCoreCoordinateFrames.h"

namespace SimCoreCoordinateFrames
{
FVector EnuMetersToUnrealCentimeters(
	const FVector3d& PositionEnuMeters,
	const FVector& PresentationOffsetCentimeters)
{
	return FVector(
		PositionEnuMeters.Y * 100.0,
		PositionEnuMeters.X * 100.0,
		PositionEnuMeters.Z * 100.0) + PresentationOffsetCentimeters;
}

FVector2d BodyFluVelocityToEnu(
	const FVector3d& LinearVelocityBody,
	double HeadingDegrees)
{
	const double HeadingRadians = FMath::DegreesToRadians(HeadingDegrees);
	const double ForwardSpeed = LinearVelocityBody.X;
	const double LeftSpeed = LinearVelocityBody.Y;
	const double NorthVelocity =
		ForwardSpeed * FMath::Cos(HeadingRadians) + LeftSpeed * FMath::Sin(HeadingRadians);
	const double EastVelocity =
		ForwardSpeed * FMath::Sin(HeadingRadians) - LeftSpeed * FMath::Cos(HeadingRadians);
	return FVector2d(EastVelocity, NorthVelocity);
}

FRotator BuildUnrealActorRotation(
	float HeadingDegrees,
	float PitchDegrees,
	float RollDegrees,
	float NavigationYawRateRadPerSecond,
	const FVector3d& AngularVelocityBody,
	double PredictionSeconds)
{
	return FRotator(
		PitchDegrees + FMath::RadiansToDegrees(AngularVelocityBody.Y * PredictionSeconds),
		HeadingDegrees + FMath::RadiansToDegrees(
			NavigationYawRateRadPerSecond * PredictionSeconds),
		-RollDegrees - FMath::RadiansToDegrees(AngularVelocityBody.X * PredictionSeconds));
}
}
