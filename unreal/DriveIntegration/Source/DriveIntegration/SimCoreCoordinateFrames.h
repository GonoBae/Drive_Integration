#pragma once

#include "CoreMinimal.h"

namespace SimCoreCoordinateFrames
{
	/**
	 * Schema-v2 Euler attitude. Heading is North=0/clockwise-positive; pitch is
	 * nose-up positive and roll is left-up positive. Values stay in degrees for
	 * wire compatibility, while all quaternion work is performed in double.
	 */
	struct FCanonicalAttitudeDegrees
	{
		double HeadingClockwise = 0.0;
		double PitchNoseUp = 0.0;
		double RollLeftUp = 0.0;
	};

	// The prototype Unreal world uses X=North, Y=East, Z=Up in centimeters.
	// Position is the only vector quantity that includes PresentationOffset.
	DRIVEINTEGRATION_API FVector MapEnuPositionMetersToUnrealCentimeters(
		const FVector3d& PositionEnuMeters,
		const FVector& PresentationOffsetCentimeters);
	DRIVEINTEGRATION_API FVector3d UnrealPositionCentimetersToMapEnuMeters(
		const FVector& PositionUnrealCentimeters,
		const FVector& PresentationOffsetCentimeters);

	// Backward-compatible name used by existing presentation call sites.
	DRIVEINTEGRATION_API FVector EnuMetersToUnrealCentimeters(
		const FVector3d& PositionEnuMeters,
		const FVector& PresentationOffsetCentimeters);

	// Unit-preserving polar basis map A: (East,North,Up) -> (North,East,Up).
	// det(A)=-1; displacement/direction use these helpers without an origin.
	DRIVEINTEGRATION_API FVector3d MapEnuPolarVectorToUnrealWorld(
		const FVector3d& VectorEnu);
	DRIVEINTEGRATION_API FVector3d UnrealWorldPolarVectorToMapEnu(
		const FVector3d& VectorUnreal);
	DRIVEINTEGRATION_API FVector3d MapEnuVelocityMetersPerSecondToUnrealCentimetersPerSecond(
		const FVector3d& VelocityEnuMetersPerSecond);
	DRIVEINTEGRATION_API FVector3d UnrealVelocityCentimetersPerSecondToMapEnuMetersPerSecond(
		const FVector3d& VelocityUnrealCentimetersPerSecond);
	DRIVEINTEGRATION_API FVector3d MapEnuAccelerationMetersPerSecondSquaredToUnrealCentimetersPerSecondSquared(
		const FVector3d& AccelerationEnuMetersPerSecondSquared);
	DRIVEINTEGRATION_API FVector3d UnrealAccelerationCentimetersPerSecondSquaredToMapEnuMetersPerSecondSquared(
		const FVector3d& AccelerationUnrealCentimetersPerSecondSquared);

	// Angular velocity is axial, so the reflected world basis uses det(A)A.
	// Radians per second are unchanged.
	DRIVEINTEGRATION_API FVector3d MapEnuAxialAngularVelocityToUnrealWorld(
		const FVector3d& AngularVelocityEnuRadiansPerSecond);
	DRIVEINTEGRATION_API FVector3d UnrealWorldAxialAngularVelocityToMapEnu(
		const FVector3d& AngularVelocityUnrealRadiansPerSecond);

	// base_link is RH FLU; Unreal Actor local coordinates are FRU. Polar vectors
	// use B=diag(1,-1,1), while axial vectors use det(B)B.
	DRIVEINTEGRATION_API FVector3d BodyFluPolarVectorToUnrealActor(
		const FVector3d& VectorBodyFlu);
	DRIVEINTEGRATION_API FVector3d UnrealActorPolarVectorToBodyFlu(
		const FVector3d& VectorUnrealActor);
	DRIVEINTEGRATION_API FVector3d BodyFluAxialAngularVelocityToUnrealActor(
		const FVector3d& AngularVelocityBodyFluRadiansPerSecond);
	DRIVEINTEGRATION_API FVector3d UnrealActorAxialAngularVelocityToBodyFlu(
		const FVector3d& AngularVelocityUnrealActorRadiansPerSecond);

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

	DRIVEINTEGRATION_API double NavigationHeadingDegreesToEnuYawRadians(
		double HeadingClockwiseDegrees);
	DRIVEINTEGRATION_API double EnuYawRadiansToNavigationHeadingDegrees(
		double YawCounterClockwiseRadians);

	// FQuat4d is used as a scalar-last quaternion container on both sides of the
	// reflection boundary. MapEnuFromBaseLink is a proper RH rotation. The Actor
	// quaternion is obtained from R_ue=A*R_enu*B^-1, never component sign guesses.
	DRIVEINTEGRATION_API FQuat4d CanonicalAttitudeToMapEnuQuaternion(
		const FCanonicalAttitudeDegrees& Attitude);
	DRIVEINTEGRATION_API FCanonicalAttitudeDegrees MapEnuQuaternionToCanonicalAttitude(
		const FQuat4d& MapEnuFromBaseLink);
	DRIVEINTEGRATION_API FQuat4d MapEnuQuaternionToUnrealActorQuaternion(
		const FQuat4d& MapEnuFromBaseLink);
	DRIVEINTEGRATION_API FQuat4d UnrealActorQuaternionToMapEnuQuaternion(
		const FQuat4d& UnrealWorldFromActor);
	DRIVEINTEGRATION_API FQuat4d CanonicalAttitudeToUnrealActorQuaternion(
		const FCanonicalAttitudeDegrees& Attitude);

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
