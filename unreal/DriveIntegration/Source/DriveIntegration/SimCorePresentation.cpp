#include "SimCorePresentation.h"

#include "Math/RotationMatrix.h"
#include "SimCoreCoordinateFrames.h"

namespace
{
bool IsFiniteVector(const FVector3d& Vector)
{
	return FMath::IsFinite(Vector.X)
		&& FMath::IsFinite(Vector.Y)
		&& FMath::IsFinite(Vector.Z);
}

bool TryNormalizeContactNormal(
	const SimCoreProtocol::FVehicleState::FWheelState& WheelState,
	FVector3d& OutNormalEnu)
{
	if (!WheelState.bInContact || !IsFiniteVector(WheelState.ContactNormalEnu))
	{
		return false;
	}
	const double LengthSquared = WheelState.ContactNormalEnu.SquaredLength();
	if (!FMath::IsFinite(LengthSquared) || LengthSquared <= UE_DOUBLE_SMALL_NUMBER)
	{
		return false;
	}
	OutNormalEnu = WheelState.ContactNormalEnu / FMath::Sqrt(LengthSquared);
	return OutNormalEnu.Z > UE_DOUBLE_KINDA_SMALL_NUMBER;
}
}

namespace SimCorePresentation
{
FVehiclePresentationSample BuildVehicleSample(
	const SimCoreProtocol::FVehicleState& State,
	float StateAgeSeconds,
	float MaxExtrapolationSeconds,
	float StateStaleTimeoutSeconds,
	float VisualWheelStopSpeedMps,
	float VisualTireRadiusMeters,
	const FVector& PresentationOffsetCentimeters)
{
	FVehiclePresentationSample Sample;
	const double ClampedStateAgeSeconds = FMath::Max(0.0, static_cast<double>(StateAgeSeconds));
	const double PredictionSeconds = FMath::Min(
		ClampedStateAgeSeconds,
		FMath::Max(0.0, static_cast<double>(MaxExtrapolationSeconds)));
	const FVector2d VelocityEnu = SimCoreCoordinateFrames::BodyFluVelocityToEnu(
		State.LinearVelocityBody,
		State.HeadingDegrees);
	FVector3d AverageContactNormalEnu = FVector3d::ZeroVector;
	int32 ContactNormalCount = 0;
	for (const SimCoreProtocol::FVehicleState::FWheelState& WheelState : State.Wheels)
	{
		FVector3d ContactNormalEnu;
		if (TryNormalizeContactNormal(WheelState, ContactNormalEnu))
		{
			AverageContactNormalEnu += ContactNormalEnu;
			++ContactNormalCount;
		}
	}
	double VerticalVelocityEnu = 0.0;
	if (ContactNormalCount > 0)
	{
		AverageContactNormalEnu.Normalize();
		if (AverageContactNormalEnu.Z > UE_DOUBLE_KINDA_SMALL_NUMBER)
		{
			// The host publishes body X/Y in its ground-tangent frame and body Z
			// as zero. Project the horizontal prediction onto the supporting plane
			// so the whole visual vehicle does not float between slope snapshots.
			VerticalVelocityEnu = -(
				AverageContactNormalEnu.X * VelocityEnu.X
				+ AverageContactNormalEnu.Y * VelocityEnu.Y)
				/ AverageContactNormalEnu.Z;
		}
	}
	const FVector3d PredictionOffsetEnu(
		VelocityEnu.X * PredictionSeconds,
		VelocityEnu.Y * PredictionSeconds,
		VerticalVelocityEnu * PredictionSeconds);
	const FVector3d PredictedPositionEnu = State.PositionEnu + PredictionOffsetEnu;

	Sample.ActorLocation = SimCoreCoordinateFrames::EnuMetersToUnrealCentimeters(
		PredictedPositionEnu,
		PresentationOffsetCentimeters);
	Sample.ActorRotation = SimCoreCoordinateFrames::BuildUnrealActorRotation(
		State.HeadingDegrees,
		State.PitchDegrees,
		State.RollDegrees,
		State.YawRateRad,
		State.AngularVelocityBody,
		PredictionSeconds);
	Sample.bStateStale = ClampedStateAgeSeconds > FMath::Max(
		0.0,
		static_cast<double>(StateStaleTimeoutSeconds));

	// Contact patches and the chassis snapshot share one authoritative time.
	// Build wheel centres in that snapshot's actor-local frame, then the limited
	// actor extrapolation carries the entire rigid visual consistently.
	const FVector SnapshotActorLocation =
		SimCoreCoordinateFrames::EnuMetersToUnrealCentimeters(
			State.PositionEnu,
			PresentationOffsetCentimeters);
	const FRotator SnapshotActorRotation =
		SimCoreCoordinateFrames::BuildUnrealActorRotation(
			State.HeadingDegrees,
			State.PitchDegrees,
			State.RollDegrees,
			State.YawRateRad,
			State.AngularVelocityBody,
			0.0);
	const FTransform SnapshotActorTransform(
		SnapshotActorRotation,
		SnapshotActorLocation,
		FVector::OneVector);

	int32 FrontWheelCount = 0;
	int32 RearWheelCount = 0;
	for (const SimCoreProtocol::FVehicleState::FWheelState& WheelState : State.Wheels)
	{
		if (WheelState.WheelIndex < VehicleWheelCount
			&& !Sample.Wheels[WheelState.WheelIndex].bHasGroundContact
			&& IsFiniteVector(WheelState.ContactPointEnu))
		{
			FVector3d ContactNormalEnu;
			if (TryNormalizeContactNormal(WheelState, ContactNormalEnu))
			{
				const double SafeTireRadiusMeters = FMath::Max(
					static_cast<double>(VisualTireRadiusMeters),
					UE_DOUBLE_KINDA_SMALL_NUMBER);
				// contact_point_enu is the true tangent patch. Adding the unit
				// normal times radius reconstructs the solver's wheel centre.
				const FVector3d WheelCenterEnu = WheelState.ContactPointEnu
					+ ContactNormalEnu * SafeTireRadiusMeters;
				const FVector WheelCenterWorldCm =
					SimCoreCoordinateFrames::EnuMetersToUnrealCentimeters(
						WheelCenterEnu,
						PresentationOffsetCentimeters);
				const FVector WorldContactNormal(
					SimCoreCoordinateFrames::MapEnuPolarVectorToUnrealWorld(
						ContactNormalEnu));
				const FVector RelativeCenterLocationCm =
					SnapshotActorTransform.InverseTransformPosition(
						WheelCenterWorldCm);
				const FVector RelativeContactNormal =
					SnapshotActorTransform.InverseTransformVectorNoScale(
						WorldContactNormal).GetSafeNormal();
				// Reject malformed remote contact data before it can teleport a
				// component far away from the vehicle. Five metres supports the
				// current sedan and leaves ample room for future vehicle classes.
				if (!RelativeCenterLocationCm.ContainsNaN()
					&& !RelativeContactNormal.ContainsNaN()
					&& RelativeContactNormal.Z > KINDA_SMALL_NUMBER
					&& RelativeCenterLocationCm.SizeSquared()
						<= FMath::Square(500.0f))
				{
					FWheelGroundPresentationSample& WheelSample =
						Sample.Wheels[WheelState.WheelIndex];
					WheelSample.RelativeCenterLocationCm =
						RelativeCenterLocationCm;
					WheelSample.RelativeContactNormal =
						RelativeContactNormal;
					WheelSample.bHasGroundContact = true;
				}
			}
		}

		// A wheel that temporarily loses terrain contact may free-spin much
		// faster than the chassis. Do not let that value contaminate the visual
		// speed of both wheels through the axle average.
		if (!WheelState.bInContact)
		{
			continue;
		}

		if (WheelState.WheelIndex < 2)
		{
			Sample.FrontAxleAngularSpeedRadPerSecond += WheelState.AngularSpeedRad;
			++FrontWheelCount;
		}
		else if (WheelState.WheelIndex < 4)
		{
			Sample.RearAxleAngularSpeedRadPerSecond += WheelState.AngularSpeedRad;
			++RearWheelCount;
		}
	}

	if (FrontWheelCount > 0)
	{
		Sample.FrontAxleAngularSpeedRadPerSecond /= FrontWheelCount;
	}
	if (RearWheelCount > 0)
	{
		Sample.RearAxleAngularSpeedRadPerSecond /= RearWheelCount;
	}

	if (Sample.bStateStale || FMath::Abs(State.SpeedMps) <= VisualWheelStopSpeedMps)
	{
		Sample.FrontAxleAngularSpeedRadPerSecond = 0.0f;
		Sample.RearAxleAngularSpeedRadPerSecond = 0.0f;
	}
	else
	{
		// Preserve small physical tire slip while preventing a transient contact
		// loss from looking like a wheel-speed bug. State.SpeedMps carries its
		// sign, so the same bound also keeps reverse rotation visually correct.
		const float SafeTireRadiusMeters = FMath::Max(
			VisualTireRadiusMeters,
			KINDA_SMALL_NUMBER);
		const float RoadAngularSpeedRadPerSecond =
			State.SpeedMps / SafeTireRadiusMeters;
		const float AllowedVisualSlipRadPerSecond = FMath::Max(
			0.10f,
			FMath::Abs(RoadAngularSpeedRadPerSecond) * 0.03f);
		const float MinimumVisualSpeed =
			RoadAngularSpeedRadPerSecond - AllowedVisualSlipRadPerSecond;
		const float MaximumVisualSpeed =
			RoadAngularSpeedRadPerSecond + AllowedVisualSlipRadPerSecond;

		if (FrontWheelCount == 0)
		{
			Sample.FrontAxleAngularSpeedRadPerSecond =
				RoadAngularSpeedRadPerSecond;
		}
		if (RearWheelCount == 0)
		{
			Sample.RearAxleAngularSpeedRadPerSecond =
				RoadAngularSpeedRadPerSecond;
		}
		Sample.FrontAxleAngularSpeedRadPerSecond = FMath::Clamp(
			Sample.FrontAxleAngularSpeedRadPerSecond,
			MinimumVisualSpeed,
			MaximumVisualSpeed);
		Sample.RearAxleAngularSpeedRadPerSecond = FMath::Clamp(
			Sample.RearAxleAngularSpeedRadPerSecond,
			MinimumVisualSpeed,
			MaximumVisualSpeed);
	}

	return Sample;
}

bool BuildRuntimeEntitySample(
	const SimCoreProtocol::FVehicleState& State,
	float StateAgeSeconds,
	float MaxExtrapolationSeconds,
	const FVector& PresentationOffsetCentimeters,
	FRuntimeEntityPresentationSample& OutSample)
{
	if (State.EntityKind != SimCoreProtocol::EEntityKind::NpcVehicle
		&& State.EntityKind != SimCoreProtocol::EEntityKind::Pedestrian)
	{
		return false;
	}

	const double PredictionSeconds = FMath::Min(
		FMath::Max(0.0, static_cast<double>(StateAgeSeconds)),
		FMath::Max(0.0, static_cast<double>(MaxExtrapolationSeconds)));
	const FVector3d PredictedPositionEnu = State.PositionEnu
		+ State.LinearVelocityEnu * PredictionSeconds;
	OutSample.ActorLocation = SimCoreCoordinateFrames::EnuMetersToUnrealCentimeters(
		PredictedPositionEnu,
		PresentationOffsetCentimeters);
	OutSample.ActorRotation = SimCoreCoordinateFrames::BuildUnrealActorRotation(
		State.HeadingDegrees,
		State.PitchDegrees,
		State.RollDegrees,
		State.YawRateRad,
		State.AngularVelocityBody,
		PredictionSeconds);

	if (State.EntityKind == SimCoreProtocol::EEntityKind::NpcVehicle)
	{
		OutSample.ActorScale = FVector(
			State.CollisionHalfLengthMeters * 2.0f,
			State.CollisionHalfWidthMeters * 2.0f,
			State.CollisionHalfHeightMeters * 2.0f);
	}
	else
	{
		OutSample.ActorScale = FVector(
			State.CollisionRadiusMeters * 2.0f,
			State.CollisionRadiusMeters * 2.0f,
			State.CollisionHalfHeightMeters * 2.0f);
	}
	return !OutSample.ActorLocation.ContainsNaN()
		&& !OutSample.ActorRotation.ContainsNaN()
		&& !OutSample.ActorScale.ContainsNaN()
		&& OutSample.ActorScale.X > 0.0
		&& OutSample.ActorScale.Y > 0.0
		&& OutSample.ActorScale.Z > 0.0;
}

float AdvanceWheelSpinDegrees(
	float CurrentSpinDegrees,
	float AngularSpeedRadPerSecond,
	float DeltaSeconds)
{
	return FMath::Fmod(
		CurrentSpinDegrees + FMath::RadiansToDegrees(AngularSpeedRadPerSecond) * DeltaSeconds,
		360.0f);
}

FRotator BuildWheelPivotRelativeRotation(
	const SimCoreProtocol::FVehicleState::FWheelState& WheelState,
	const FVector& RelativeContactNormal)
{
	const FVector SurfaceNormal = RelativeContactNormal.GetSafeNormal(
		KINDA_SMALL_NUMBER,
		FVector::UpVector);
	FVector SurfaceForward = FVector::ForwardVector
		- FVector::DotProduct(FVector::ForwardVector, SurfaceNormal)
			* SurfaceNormal;
	SurfaceForward = SurfaceForward.GetSafeNormal(
		KINDA_SMALL_NUMBER,
		FVector::ForwardVector);
	const float SteeringRadians = FMath::DegreesToRadians(
		SimCoreCoordinateFrames::CanonicalSteeringToUnrealYawDegrees(
			WheelState.SteeringAngleRad));
	SurfaceForward = FQuat(SurfaceNormal, SteeringRadians)
		.RotateVector(SurfaceForward)
		.GetSafeNormal();
	return FRotationMatrix::MakeFromXZ(
		SurfaceForward,
		SurfaceNormal).Rotator();
}

FRotator BuildWheelSpinRelativeRotation(float AxleSpinDegrees)
{
	return FRotator(-AxleSpinDegrees, 0.0f, 0.0f);
}
}
