#include "SimCorePresentation.h"

#include "SimCoreCoordinateFrames.h"

namespace SimCorePresentation
{
FVehiclePresentationSample BuildVehicleSample(
	const SimCoreProtocol::FVehicleState& State,
	float StateAgeSeconds,
	float MaxExtrapolationSeconds,
	float StateStaleTimeoutSeconds,
	float VisualWheelStopSpeedMps,
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
	const FVector3d PredictedPositionEnu = State.PositionEnu + FVector3d(
		VelocityEnu.X * PredictionSeconds,
		VelocityEnu.Y * PredictionSeconds,
		0.0);

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

	int32 FrontWheelCount = 0;
	int32 RearWheelCount = 0;
	for (const SimCoreProtocol::FVehicleState::FWheelState& WheelState : State.Wheels)
	{
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

	return Sample;
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

FRotator BuildWheelRelativeRotation(
	const SimCoreProtocol::FVehicleState::FWheelState& WheelState,
	float AxleSpinDegrees)
{
	return FRotator(
		-AxleSpinDegrees,
		SimCoreCoordinateFrames::CanonicalSteeringToUnrealYawDegrees(
			WheelState.SteeringAngleRad),
		0.0f);
}
}
