#pragma once

#include "CoreMinimal.h"
#include "SimCoreProtocol.h"

namespace SimCoreDamagePresentation
{
	inline constexpr float DamagePercentForFullDent = 35.0f;

	/**
	 * Presentation-only WPO weights. Each value is independently bounded to
	 * [0, 1], so impacts from a later side do not move an earlier dent.
	 */
	struct FZoneWeights
	{
		float Front = 0.0f;
		float Rear = 0.0f;
		float Left = 0.0f;
		float Right = 0.0f;
		float Roof = 0.0f;
		float Underbody = 0.0f;
		// Runtime deformation uses authoritative contacts, not broad panel weights.
		bool bContactLocal = false;
		TArray<SimCoreProtocol::FVehicleDentPatch> Dents;

		bool IsNearlyZero() const;
		float Get(SimCoreProtocol::EVehicleDamageZone Zone) const;
	};

	/** Client-side accumulator for additive collision events in one PIE play session. */
	struct FAccumulator
	{
		FZoneWeights Weights;
		FString PlaySessionId;
		float LastDamagePercent = 0.0f;
		uint32 LastCollisionEventSequence = 0;
		bool bInitialized = false;
	};

	/**
	 * Consumes one accepted authoritative state. Returns true only when material
	 * parameters need refreshing. A reset, play-session change, decreasing event
	 * sequence, or decreasing total damage clears all presentation-only dents.
	 */
	DRIVEINTEGRATION_API bool Advance(
		FAccumulator& Accumulator,
		const SimCoreProtocol::FVehicleState& State);

	/** Scalar parameter shared by every generated Sedan body material. */
	DRIVEINTEGRATION_API FName MaterialParameterName(
		SimCoreProtocol::EVehicleDamageZone Zone);
}
