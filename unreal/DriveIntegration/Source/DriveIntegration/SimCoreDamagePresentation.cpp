#include "SimCoreDamagePresentation.h"

namespace SimCoreDamagePresentation
{
namespace
{
float* MutableWeight(FZoneWeights& Weights, const SimCoreProtocol::EVehicleDamageZone Zone)
{
	switch (Zone)
	{
	case SimCoreProtocol::EVehicleDamageZone::Front: return &Weights.Front;
	case SimCoreProtocol::EVehicleDamageZone::Rear: return &Weights.Rear;
	case SimCoreProtocol::EVehicleDamageZone::Left: return &Weights.Left;
	case SimCoreProtocol::EVehicleDamageZone::Right: return &Weights.Right;
	case SimCoreProtocol::EVehicleDamageZone::Roof: return &Weights.Roof;
	case SimCoreProtocol::EVehicleDamageZone::Underbody: return &Weights.Underbody;
	case SimCoreProtocol::EVehicleDamageZone::None:
	default: return nullptr;
	}
}

void AddDamage(
	FZoneWeights& Weights,
	const SimCoreProtocol::EVehicleDamageZone Zone,
	const float DamagePercent)
{
	float* Weight = MutableWeight(Weights, Zone);
	if (!Weight || !FMath::IsFinite(DamagePercent) || DamagePercent <= 0.0f)
	{
		return;
	}
	*Weight = FMath::Clamp(
		*Weight + DamagePercent / DamagePercentForFullDent,
		0.0f,
		1.0f);
}

void ResetAndSeed(
	FAccumulator& Accumulator,
	const SimCoreProtocol::FVehicleState& State,
	const float DamagePercent)
{
	Accumulator.Weights = {};
	Accumulator.PlaySessionId = State.PlaySessionId;
	Accumulator.LastDamagePercent = DamagePercent;
	Accumulator.LastCollisionEventSequence = State.CollisionEventSequence;
	Accumulator.bInitialized = true;
	AddDamage(Accumulator.Weights, State.DamageZone, DamagePercent);
}
}

bool FZoneWeights::IsNearlyZero() const
{
	return FMath::IsNearlyZero(Front)
		&& FMath::IsNearlyZero(Rear)
		&& FMath::IsNearlyZero(Left)
		&& FMath::IsNearlyZero(Right)
		&& FMath::IsNearlyZero(Roof)
		&& FMath::IsNearlyZero(Underbody);
}

float FZoneWeights::Get(const SimCoreProtocol::EVehicleDamageZone Zone) const
{
	switch (Zone)
	{
	case SimCoreProtocol::EVehicleDamageZone::Front: return Front;
	case SimCoreProtocol::EVehicleDamageZone::Rear: return Rear;
	case SimCoreProtocol::EVehicleDamageZone::Left: return Left;
	case SimCoreProtocol::EVehicleDamageZone::Right: return Right;
	case SimCoreProtocol::EVehicleDamageZone::Roof: return Roof;
	case SimCoreProtocol::EVehicleDamageZone::Underbody: return Underbody;
	case SimCoreProtocol::EVehicleDamageZone::None:
	default: return 0.0f;
	}
}

bool Advance(
	FAccumulator& Accumulator,
	const SimCoreProtocol::FVehicleState& State)
{
	if (!FMath::IsFinite(State.DamagePercent))
	{
		return false;
	}
	const float DamagePercent = FMath::Clamp(State.DamagePercent, 0.0f, 100.0f);
	if (!Accumulator.bInitialized)
	{
		ResetAndSeed(Accumulator, State, DamagePercent);
		return !Accumulator.Weights.IsNearlyZero();
	}

	const bool bSessionChanged = Accumulator.PlaySessionId != State.PlaySessionId;
	const bool bSequenceRestarted =
		State.CollisionEventSequence < Accumulator.LastCollisionEventSequence;
	const bool bDamageDecreased =
		DamagePercent + KINDA_SMALL_NUMBER < Accumulator.LastDamagePercent;
	if (bSessionChanged || bSequenceRestarted || bDamageDecreased)
	{
		const bool bHadDent = !Accumulator.Weights.IsNearlyZero();
		ResetAndSeed(Accumulator, State, DamagePercent);
		return bHadDent || !Accumulator.Weights.IsNearlyZero();
	}

	if (State.CollisionEventSequence == Accumulator.LastCollisionEventSequence)
	{
		return false;
	}

	const float DeltaDamage = FMath::Max(
		0.0f, DamagePercent - Accumulator.LastDamagePercent);
	const float PreviousWeight = Accumulator.Weights.Get(State.DamageZone);
	AddDamage(Accumulator.Weights, State.DamageZone, DeltaDamage);
	Accumulator.LastDamagePercent = DamagePercent;
	Accumulator.LastCollisionEventSequence = State.CollisionEventSequence;
	Accumulator.PlaySessionId = State.PlaySessionId;
	return !FMath::IsNearlyEqual(
		PreviousWeight,
		Accumulator.Weights.Get(State.DamageZone));
}

FName MaterialParameterName(const SimCoreProtocol::EVehicleDamageZone Zone)
{
	switch (Zone)
	{
	case SimCoreProtocol::EVehicleDamageZone::Front: return TEXT("DentFront");
	case SimCoreProtocol::EVehicleDamageZone::Rear: return TEXT("DentRear");
	case SimCoreProtocol::EVehicleDamageZone::Left: return TEXT("DentLeft");
	case SimCoreProtocol::EVehicleDamageZone::Right: return TEXT("DentRight");
	case SimCoreProtocol::EVehicleDamageZone::Roof: return TEXT("DentRoof");
	case SimCoreProtocol::EVehicleDamageZone::Underbody: return TEXT("DentUnderbody");
	case SimCoreProtocol::EVehicleDamageZone::None:
	default: return NAME_None;
	}
}
}
