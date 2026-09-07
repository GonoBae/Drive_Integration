#pragma once

#include "CoreMinimal.h"
#include "SimCoreProtocol.h"

// Presentation-only state: these values never enter a ControlCommand or alter
// the authoritative vehicle transform. Mouse deltas and gamepad rates are
// deliberately separate so orbit speed does not depend on rendering FPS.
namespace SimCoreOrbitCamera
{
	enum class EMode : uint8
	{
		Follow,
		Driver,
		FixedRear
	};

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

	struct FDriverLook
	{
		float YawDegrees = 0.0f;
		float PitchDegrees = 0.0f;
	};

	struct FVehicleMounts
	{
		FVector DriverLocationCm = FVector(-20.0, -34.0, 82.0);
		float FixedTargetHeightCm = 145.0f;
		float FixedDistanceCm = 600.0f;
		// The generated truck cab has no hollow interior yet. Its forward cab
		// view must stay outside the solid shell instead of looking through it.
		bool bDriverUsesCabFrontFallback = false;
	};

	DRIVEINTEGRATION_API EMode NextMode(EMode Mode);
	DRIVEINTEGRATION_API FVehicleMounts GetVehicleMounts(
		SimCoreProtocol::ERuntimeVehicleClass VehicleClass);
	DRIVEINTEGRATION_API void AddDriverMouseDelta(
		FDriverLook& State, float YawDelta, float PitchDelta, float DegreesPerInputUnit);
	DRIVEINTEGRATION_API void AddDriverGamepadRate(
		FDriverLook& State, float YawAxis, float PitchAxis, float DegreesPerSecond, float DeltaSeconds);
	DRIVEINTEGRATION_API FRotator BuildBodyRelativeRotation(
		const FDriverLook& State, const FQuat& VehicleRotation);

	DRIVEINTEGRATION_API void AddMouseDelta(
		FState& State, float YawDelta, float PitchDelta, float DegreesPerInputUnit);
	DRIVEINTEGRATION_API void AddGamepadRate(
		FState& State, float YawAxis, float PitchAxis, float DegreesPerSecond, float DeltaSeconds);
	DRIVEINTEGRATION_API void AddZoom(FState& State, float WheelDelta, float DistancePerInputUnitCm);
	DRIVEINTEGRATION_API void ResetView(FState& State);
	DRIVEINTEGRATION_API FRotator BuildWorldRotation(const FState& State, float VehicleYawDegrees);
}
