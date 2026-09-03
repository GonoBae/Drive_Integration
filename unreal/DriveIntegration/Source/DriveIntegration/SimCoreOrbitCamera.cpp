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
