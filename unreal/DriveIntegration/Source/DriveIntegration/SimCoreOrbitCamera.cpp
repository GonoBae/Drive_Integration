#include "SimCoreOrbitCamera.h"

namespace SimCoreOrbitCamera
{
	namespace
	{
		float FiniteOrZero(float Value)
		{
			return FMath::IsFinite(Value) ? Value : 0.0f;
		}

		void AddAngles(FState& State, float YawDegrees, float PitchDegrees)
		{
			State.YawOffsetDegrees = FRotator::NormalizeAxis(
				FRotator::NormalizeAxis(FiniteOrZero(State.YawOffsetDegrees))
					+ FRotator::NormalizeAxis(FiniteOrZero(YawDegrees)));
			State.PitchDegrees = FMath::Clamp(
				(FMath::IsFinite(State.PitchDegrees) ? State.PitchDegrees : DefaultPitchDegrees)
					+ FiniteOrZero(PitchDegrees), MinPitchDegrees, MaxPitchDegrees);
		}
	}

	EMode NextMode(const EMode Mode)
	{
		switch (Mode)
		{
		case EMode::Follow: return EMode::Driver;
		case EMode::Driver: return EMode::FixedRear;
		default: return EMode::Follow;
		}
	}

	FVehicleMounts GetVehicleMounts(const SimCoreProtocol::ERuntimeVehicleClass VehicleClass)
	{
		FVehicleMounts Mounts;
		switch (VehicleClass)
		{
		case SimCoreProtocol::ERuntimeVehicleClass::Compact:
			Mounts.DriverLocationCm = Mounts.DriverLocationCm * FVector(0.79, 0.90, 0.92)
				+ FVector(-4.0, 0.0, -1.5);
			Mounts.FixedTargetHeightCm = 130.0f;
			Mounts.FixedDistanceCm = 530.0f;
			break;
		case SimCoreProtocol::ERuntimeVehicleClass::Truck:
			Mounts.DriverLocationCm = FVector(95.0, -34.0, 145.0);
			Mounts.FixedTargetHeightCm = 220.0f;
			Mounts.FixedDistanceCm = 850.0f;
			Mounts.bDriverUsesCabFrontFallback = false;
			break;
		case SimCoreProtocol::ERuntimeVehicleClass::Motorcycle:
			Mounts.DriverLocationCm = FVector(-10.0, 0.0, 125.0);
			Mounts.FixedTargetHeightCm = 130.0f;
			Mounts.FixedDistanceCm = 450.0f;
			break;
		default:
			break;
		}
		return Mounts;
	}

	void AddDriverMouseDelta(FDriverLook& State, float YawDelta, float PitchDelta,
		float DegreesPerInputUnit)
	{
		const float Scale = FMath::Max(0.0f, FiniteOrZero(DegreesPerInputUnit));
		State.YawDegrees = FMath::Clamp(FiniteOrZero(State.YawDegrees)
			+ FiniteOrZero(YawDelta) * Scale, -75.0f, 75.0f);
		State.PitchDegrees = FMath::Clamp(FiniteOrZero(State.PitchDegrees)
			+ FiniteOrZero(PitchDelta) * Scale, -40.0f, 30.0f);
	}

	void AddDriverGamepadRate(FDriverLook& State, float YawAxis, float PitchAxis,
		float DegreesPerSecond, float DeltaSeconds)
	{
		const float Scale = FMath::Max(0.0f, FiniteOrZero(DegreesPerSecond))
			* FMath::Max(0.0f, FiniteOrZero(DeltaSeconds));
		AddDriverMouseDelta(State, FMath::Clamp(FiniteOrZero(YawAxis), -1.0f, 1.0f),
			FMath::Clamp(FiniteOrZero(PitchAxis), -1.0f, 1.0f), Scale);
	}

	FRotator BuildBodyRelativeRotation(const FDriverLook& State, const FQuat& VehicleRotation)
	{
		FDriverLook Clamped = State;
		AddDriverMouseDelta(Clamped, 0.0f, 0.0f, 0.0f);
		const FQuat Body = VehicleRotation.ContainsNaN() || !VehicleRotation.IsNormalized()
			? FQuat::Identity : VehicleRotation;
		return (Body * FRotator(Clamped.PitchDegrees, Clamped.YawDegrees, 0.0f).Quaternion()).Rotator();
	}

	void AddMouseDelta(FState& State, float YawDelta, float PitchDelta, float DegreesPerInputUnit)
	{
		const float Scale = FMath::Max(0.0f, FiniteOrZero(DegreesPerInputUnit));
		// Mouse values are accumulated displacement this frame, not a rate.
		AddAngles(State, FiniteOrZero(YawDelta) * Scale, FiniteOrZero(PitchDelta) * Scale);
	}

	void AddGamepadRate(FState& State, float YawAxis, float PitchAxis, float DegreesPerSecond, float DeltaSeconds)
	{
		const float Scale = FMath::Max(0.0f, FiniteOrZero(DegreesPerSecond))
			* FMath::Max(0.0f, FiniteOrZero(DeltaSeconds));
		AddAngles(State, FMath::Clamp(FiniteOrZero(YawAxis), -1.0f, 1.0f) * Scale,
			FMath::Clamp(FiniteOrZero(PitchAxis), -1.0f, 1.0f) * Scale);
	}

	void AddZoom(FState& State, float WheelDelta, float DistancePerInputUnitCm)
	{
		const float CurrentDistance = FMath::IsFinite(State.DistanceCm) ? State.DistanceCm : DefaultDistanceCm;
		const float Delta = FiniteOrZero(WheelDelta) * FMath::Max(0.0f, FiniteOrZero(DistancePerInputUnitCm));
		State.DistanceCm = FMath::Clamp(CurrentDistance - FiniteOrZero(Delta), MinDistanceCm, MaxDistanceCm);
	}

	void ResetView(FState& State)
	{
		State = FState{};
	}

	FRotator BuildWorldRotation(const FState& State, float VehicleYawDegrees)
	{
		const float Pitch = FMath::Clamp(FMath::IsFinite(State.PitchDegrees)
			? State.PitchDegrees : DefaultPitchDegrees, MinPitchDegrees, MaxPitchDegrees);
		return FRotator(Pitch, FRotator::NormalizeAxis(
			FRotator::NormalizeAxis(FiniteOrZero(VehicleYawDegrees))
				+ FRotator::NormalizeAxis(FiniteOrZero(State.YawOffsetDegrees))), 0.0f);
	}
}
