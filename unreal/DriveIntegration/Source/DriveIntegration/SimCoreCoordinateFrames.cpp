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

float InputAxisToCanonicalSteering(float RightPositiveInput)
{
	return -RightPositiveInput;
}

float CanonicalSteeringToUnrealYawDegrees(float LeftPositiveSteeringRadians)
{
	return -FMath::RadiansToDegrees(LeftPositiveSteeringRadians);
}

FRotator BuildUnrealActorRotation(
	float HeadingDegrees,
	float PitchDegrees,
	float RollDegrees,
	float BodyYawRateRadPerSecond,
	const FVector3d& AngularVelocityBody,
	double PredictionSeconds)
{
	const double PitchRadians = FMath::DegreesToRadians(PitchDegrees);
	const double RollRadians = FMath::DegreesToRadians(RollDegrees);
	// angular_velocity_body is a true RH-FLU vector, not a bag of Euler
	// derivatives. Recover the Euler rates used by this presentation transform
	// so compound pitch+roll+yaw prediction does not introduce cross-axis drift.
	// The legacy scalar remains body-Z angular rate by schema contract, so it is
	// only a gimbal fallback; it is not the navigation Euler yaw rate at a
	// compound attitude.
	const double PitchRateRadPerSecond =
		-AngularVelocityBody.Y * FMath::Cos(RollRadians)
		+ AngularVelocityBody.Z * FMath::Sin(RollRadians);
	const double PitchCosine = FMath::Cos(PitchRadians);
	const double CanonicalNavigationYawRateRadPerSecond =
		FMath::Abs(PitchCosine) > 1.0e-6
		? (AngularVelocityBody.Y * FMath::Sin(RollRadians)
			+ AngularVelocityBody.Z * FMath::Cos(RollRadians)) / PitchCosine
		: BodyYawRateRadPerSecond;
	const double RollRateRadPerSecond = AngularVelocityBody.X
		- CanonicalNavigationYawRateRadPerSecond * FMath::Sin(PitchRadians);
	return FRotator(
		PitchDegrees + FMath::RadiansToDegrees(
			PitchRateRadPerSecond * PredictionSeconds),
		HeadingDegrees - FMath::RadiansToDegrees(
			CanonicalNavigationYawRateRadPerSecond * PredictionSeconds),
		RollDegrees + FMath::RadiansToDegrees(
			RollRateRadPerSecond * PredictionSeconds));
}
}
