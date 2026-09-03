#include "SimCoreCoordinateFrames.h"

#include <cmath>

namespace
{
struct FCoordinateMatrix3d
{
	double M[3][3]{};
};

double NormalizePositiveRadians(double Radians)
{
	const double FullTurn = 2.0 * UE_DOUBLE_PI;
	Radians = std::fmod(Radians, FullTurn);
	return Radians < 0.0 ? Radians + FullTurn : Radians;
}

FQuat4d NormalizeQuaternion(const FQuat4d& Quaternion)
{
	const double Length = std::hypot(
		std::hypot(Quaternion.X, Quaternion.Y),
		std::hypot(Quaternion.Z, Quaternion.W));
	if (!FMath::IsFinite(Length) || Length <= 0.0)
	{
		return FQuat4d(0.0, 0.0, 0.0, 1.0);
	}
	FQuat4d Result(
		Quaternion.X / Length,
		Quaternion.Y / Length,
		Quaternion.Z / Length,
		Quaternion.W / Length);
	if (Result.W < 0.0)
	{
		Result.X = -Result.X;
		Result.Y = -Result.Y;
		Result.Z = -Result.Z;
		Result.W = -Result.W;
	}
	return Result;
}

FCoordinateMatrix3d QuaternionToRotation(const FQuat4d& Input)
{
	const FQuat4d Q = NormalizeQuaternion(Input);
	const double XX = Q.X * Q.X;
	const double YY = Q.Y * Q.Y;
	const double ZZ = Q.Z * Q.Z;
	const double XY = Q.X * Q.Y;
	const double XZ = Q.X * Q.Z;
	const double YZ = Q.Y * Q.Z;
	const double XW = Q.X * Q.W;
	const double YW = Q.Y * Q.W;
	const double ZW = Q.Z * Q.W;
	return {{
		{1.0 - 2.0 * (YY + ZZ), 2.0 * (XY - ZW), 2.0 * (XZ + YW)},
		{2.0 * (XY + ZW), 1.0 - 2.0 * (XX + ZZ), 2.0 * (YZ - XW)},
		{2.0 * (XZ - YW), 2.0 * (YZ + XW), 1.0 - 2.0 * (XX + YY)},
	}};
}

FQuat4d RotationToQuaternion(const FCoordinateMatrix3d& Rotation)
{
	FQuat4d Result(0.0, 0.0, 0.0, 1.0);
	const double Trace = Rotation.M[0][0] + Rotation.M[1][1] + Rotation.M[2][2];
	if (Trace > 0.0)
	{
		const double Scale = 2.0 * std::sqrt(Trace + 1.0);
		Result = FQuat4d(
			(Rotation.M[2][1] - Rotation.M[1][2]) / Scale,
			(Rotation.M[0][2] - Rotation.M[2][0]) / Scale,
			(Rotation.M[1][0] - Rotation.M[0][1]) / Scale,
			0.25 * Scale);
	}
	else if (Rotation.M[0][0] > Rotation.M[1][1]
		&& Rotation.M[0][0] > Rotation.M[2][2])
	{
		const double Scale = 2.0 * std::sqrt(
			1.0 + Rotation.M[0][0] - Rotation.M[1][1] - Rotation.M[2][2]);
		Result = FQuat4d(
			0.25 * Scale,
			(Rotation.M[0][1] + Rotation.M[1][0]) / Scale,
			(Rotation.M[0][2] + Rotation.M[2][0]) / Scale,
			(Rotation.M[2][1] - Rotation.M[1][2]) / Scale);
	}
	else if (Rotation.M[1][1] > Rotation.M[2][2])
	{
		const double Scale = 2.0 * std::sqrt(
			1.0 + Rotation.M[1][1] - Rotation.M[0][0] - Rotation.M[2][2]);
		Result = FQuat4d(
			(Rotation.M[0][1] + Rotation.M[1][0]) / Scale,
			0.25 * Scale,
			(Rotation.M[1][2] + Rotation.M[2][1]) / Scale,
			(Rotation.M[0][2] - Rotation.M[2][0]) / Scale);
	}
	else
	{
		const double Scale = 2.0 * std::sqrt(
			1.0 + Rotation.M[2][2] - Rotation.M[0][0] - Rotation.M[1][1]);
		Result = FQuat4d(
			(Rotation.M[0][2] + Rotation.M[2][0]) / Scale,
			(Rotation.M[1][2] + Rotation.M[2][1]) / Scale,
			0.25 * Scale,
			(Rotation.M[1][0] - Rotation.M[0][1]) / Scale);
	}
	return NormalizeQuaternion(Result);
}

FCoordinateMatrix3d ChangeOrientationBasis(const FCoordinateMatrix3d& Rotation)
{
	// R_unreal_world_from_actor = A * R_map_enu_from_base_link * B^-1.
	// A swaps East/North rows; B^-1=B flips the FLU-left column to Actor-right.
	return {{
		{Rotation.M[1][0], -Rotation.M[1][1], Rotation.M[1][2]},
		{Rotation.M[0][0], -Rotation.M[0][1], Rotation.M[0][2]},
		{Rotation.M[2][0], -Rotation.M[2][1], Rotation.M[2][2]},
	}};
}
}

namespace SimCoreCoordinateFrames
{
FVector3d MapEnuPolarVectorToUnrealWorld(const FVector3d& VectorEnu)
{
	return FVector3d(VectorEnu.Y, VectorEnu.X, VectorEnu.Z);
}

FVector3d UnrealWorldPolarVectorToMapEnu(const FVector3d& VectorUnreal)
{
	return FVector3d(VectorUnreal.Y, VectorUnreal.X, VectorUnreal.Z);
}

FVector MapEnuPositionMetersToUnrealCentimeters(
	const FVector3d& PositionEnuMeters,
	const FVector& PresentationOffsetCentimeters)
{
	return FVector(MapEnuPolarVectorToUnrealWorld(PositionEnuMeters) * 100.0)
		+ PresentationOffsetCentimeters;
}

FVector3d UnrealPositionCentimetersToMapEnuMeters(
	const FVector& PositionUnrealCentimeters,
	const FVector& PresentationOffsetCentimeters)
{
	return UnrealWorldPolarVectorToMapEnu(
		FVector3d(PositionUnrealCentimeters - PresentationOffsetCentimeters) / 100.0);
}

FVector EnuMetersToUnrealCentimeters(
	const FVector3d& PositionEnuMeters,
	const FVector& PresentationOffsetCentimeters)
{
	return MapEnuPositionMetersToUnrealCentimeters(
		PositionEnuMeters,
		PresentationOffsetCentimeters);
}

FVector3d MapEnuVelocityMetersPerSecondToUnrealCentimetersPerSecond(
	const FVector3d& VelocityEnuMetersPerSecond)
{
	return MapEnuPolarVectorToUnrealWorld(VelocityEnuMetersPerSecond) * 100.0;
}

FVector3d UnrealVelocityCentimetersPerSecondToMapEnuMetersPerSecond(
	const FVector3d& VelocityUnrealCentimetersPerSecond)
{
	return UnrealWorldPolarVectorToMapEnu(
		VelocityUnrealCentimetersPerSecond / 100.0);
}

FVector3d MapEnuAccelerationMetersPerSecondSquaredToUnrealCentimetersPerSecondSquared(
	const FVector3d& AccelerationEnuMetersPerSecondSquared)
{
	return MapEnuVelocityMetersPerSecondToUnrealCentimetersPerSecond(
		AccelerationEnuMetersPerSecondSquared);
}

FVector3d UnrealAccelerationCentimetersPerSecondSquaredToMapEnuMetersPerSecondSquared(
	const FVector3d& AccelerationUnrealCentimetersPerSecondSquared)
{
	return UnrealVelocityCentimetersPerSecondToMapEnuMetersPerSecond(
		AccelerationUnrealCentimetersPerSecondSquared);
}

FVector3d MapEnuAxialAngularVelocityToUnrealWorld(
	const FVector3d& AngularVelocityEnuRadiansPerSecond)
{
	return FVector3d(
		-AngularVelocityEnuRadiansPerSecond.Y,
		-AngularVelocityEnuRadiansPerSecond.X,
		-AngularVelocityEnuRadiansPerSecond.Z);
}

FVector3d UnrealWorldAxialAngularVelocityToMapEnu(
	const FVector3d& AngularVelocityUnrealRadiansPerSecond)
{
	return FVector3d(
		-AngularVelocityUnrealRadiansPerSecond.Y,
		-AngularVelocityUnrealRadiansPerSecond.X,
		-AngularVelocityUnrealRadiansPerSecond.Z);
}

FVector3d BodyFluPolarVectorToUnrealActor(const FVector3d& VectorBodyFlu)
{
	return FVector3d(VectorBodyFlu.X, -VectorBodyFlu.Y, VectorBodyFlu.Z);
}

FVector3d UnrealActorPolarVectorToBodyFlu(const FVector3d& VectorUnrealActor)
{
	return FVector3d(VectorUnrealActor.X, -VectorUnrealActor.Y, VectorUnrealActor.Z);
}

FVector3d BodyFluAxialAngularVelocityToUnrealActor(
	const FVector3d& AngularVelocityBodyFluRadiansPerSecond)
{
	return FVector3d(
		-AngularVelocityBodyFluRadiansPerSecond.X,
		AngularVelocityBodyFluRadiansPerSecond.Y,
		-AngularVelocityBodyFluRadiansPerSecond.Z);
}

FVector3d UnrealActorAxialAngularVelocityToBodyFlu(
	const FVector3d& AngularVelocityUnrealActorRadiansPerSecond)
{
	return FVector3d(
		-AngularVelocityUnrealActorRadiansPerSecond.X,
		AngularVelocityUnrealActorRadiansPerSecond.Y,
		-AngularVelocityUnrealActorRadiansPerSecond.Z);
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

double NavigationHeadingDegreesToEnuYawRadians(double HeadingClockwiseDegrees)
{
	return NormalizePositiveRadians(
		0.5 * UE_DOUBLE_PI - FMath::DegreesToRadians(HeadingClockwiseDegrees));
}

double EnuYawRadiansToNavigationHeadingDegrees(double YawCounterClockwiseRadians)
{
	return FMath::RadiansToDegrees(NormalizePositiveRadians(
		0.5 * UE_DOUBLE_PI - YawCounterClockwiseRadians));
}

FQuat4d CanonicalAttitudeToMapEnuQuaternion(
	const FCanonicalAttitudeDegrees& Attitude)
{
	const double Yaw = NavigationHeadingDegreesToEnuYawRadians(
		Attitude.HeadingClockwise);
	const double Pitch = -FMath::DegreesToRadians(Attitude.PitchNoseUp);
	const double Roll = FMath::DegreesToRadians(Attitude.RollLeftUp);
	const double CY = FMath::Cos(Yaw);
	const double SY = FMath::Sin(Yaw);
	const double CP = FMath::Cos(Pitch);
	const double SP = FMath::Sin(Pitch);
	const double CR = FMath::Cos(Roll);
	const double SR = FMath::Sin(Roll);
	const FCoordinateMatrix3d Rotation{{
		{CY * CP, CY * SP * SR - SY * CR, CY * SP * CR + SY * SR},
		{SY * CP, SY * SP * SR + CY * CR, SY * SP * CR - CY * SR},
		{-SP, CP * SR, CP * CR},
	}};
	return RotationToQuaternion(Rotation);
}

FCanonicalAttitudeDegrees MapEnuQuaternionToCanonicalAttitude(
	const FQuat4d& MapEnuFromBaseLink)
{
	const FCoordinateMatrix3d Rotation = QuaternionToRotation(MapEnuFromBaseLink);
	const double PitchNoseUp = FMath::Asin(FMath::Clamp(
		Rotation.M[2][0], -1.0, 1.0));
	const double YawCounterClockwise = FMath::Atan2(
		Rotation.M[1][0], Rotation.M[0][0]);
	return {
		EnuYawRadiansToNavigationHeadingDegrees(YawCounterClockwise),
		FMath::RadiansToDegrees(PitchNoseUp),
		FMath::RadiansToDegrees(FMath::Atan2(
			Rotation.M[2][1], Rotation.M[2][2])),
	};
}

FQuat4d MapEnuQuaternionToUnrealActorQuaternion(
	const FQuat4d& MapEnuFromBaseLink)
{
	return RotationToQuaternion(ChangeOrientationBasis(
		QuaternionToRotation(MapEnuFromBaseLink)));
}

FQuat4d UnrealActorQuaternionToMapEnuQuaternion(
	const FQuat4d& UnrealWorldFromActor)
{
	// A^-1=A and B^-1=B, so this basis operation is involutory.
	return RotationToQuaternion(ChangeOrientationBasis(
		QuaternionToRotation(UnrealWorldFromActor)));
}

FQuat4d CanonicalAttitudeToUnrealActorQuaternion(
	const FCanonicalAttitudeDegrees& Attitude)
{
	return MapEnuQuaternionToUnrealActorQuaternion(
		CanonicalAttitudeToMapEnuQuaternion(Attitude));
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
	const FCanonicalAttitudeDegrees PredictedAttitude{
		HeadingDegrees - FMath::RadiansToDegrees(
			CanonicalNavigationYawRateRadPerSecond * PredictionSeconds),
		PitchDegrees + FMath::RadiansToDegrees(
			PitchRateRadPerSecond * PredictionSeconds),
		RollDegrees + FMath::RadiansToDegrees(
			RollRateRadPerSecond * PredictionSeconds),
	};
	return CanonicalAttitudeToUnrealActorQuaternion(PredictedAttitude).Rotator();
}
}
