#pragma once

#include "CoreMinimal.h"

// Presentation-only state: these values never enter a ControlCommand or alter
// the authoritative vehicle transform. Mouse deltas and gamepad rates are
// deliberately separate so orbit speed does not depend on rendering FPS.
namespace SimCoreOrbitCamera
{
	inline constexpr float DefaultPitchDegrees = -15.0f;
	inline constexpr float DefaultDistanceCm = 600.0f;
	inline constexpr float MinPitchDegrees = -65.0f;
	inline constexpr float MaxPitchDegrees = -5.0f;
	inline constexpr float MinDistanceCm = 350.0f;
	inline constexpr float MaxDistanceCm = 1000.0f;

	struct FState
	{
		float YawOffsetDegrees = 0.0f;
		float PitchDegrees = DefaultPitchDegrees;
		float DistanceCm = DefaultDistanceCm;
	};

	DRIVEINTEGRATION_API void AddMouseDelta(
		FState& State, float YawDelta, float PitchDelta, float DegreesPerInputUnit);
	DRIVEINTEGRATION_API void AddGamepadRate(
		FState& State, float YawAxis, float PitchAxis, float DegreesPerSecond, float DeltaSeconds);
	DRIVEINTEGRATION_API void AddZoom(FState& State, float WheelDelta, float DistancePerInputUnitCm);
	DRIVEINTEGRATION_API void ResetView(FState& State);
	DRIVEINTEGRATION_API FRotator BuildWorldRotation(const FState& State, float VehicleYawDegrees);
}
