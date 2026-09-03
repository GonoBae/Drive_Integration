#pragma once

#include "CoreMinimal.h"
#include "SimCoreProtocol.h"

namespace SimCoreClientDiagnostics
{
	struct FHealthDisplay
	{
		FString Status = TEXT("Unknown");
		FString Reason;
		FColor Color = FColor(255, 205, 80);
		bool bAuthoritative = false;
	};

	/** Display policy only; it must never gate outgoing control or lease recovery. */
	DRIVEINTEGRATION_API FHealthDisplay EvaluateServerHealth(
		const SimCoreProtocol::FServerHealth& Health,
		bool bConnectionReady,
		bool bHasSnapshot,
		double LocalSnapshotAgeSeconds,
		double StaleTimeoutSeconds);

	/** Host-wide EStop is read-only health, never authority to accept another play's pose. */
	class DRIVEINTEGRATION_API FGlobalEstopHealthCache
	{
	public:
		bool Observe(
			const SimCoreProtocol::FVehicleState& Snapshot,
			uint64 IncomingGeneration,
			uint64 CurrentGeneration,
			bool bVerifiedHello,
			const FString& ExpectedMapChecksum,
			double ReceiveTimeSeconds);
		void Reset();
		bool HasEstopHealth() const { return Health.bPresent; }
		const SimCoreProtocol::FServerHealth& GetHealth() const { return Health; }
		double GetReceiveTimeSeconds() const { return ReceiveTimeSeconds; }
		uint64 GetSequence() const { return LastSequence; }
		bool CanUsePlaySnapshot(uint64 Sequence) const
		{
			return BoundGeneration != 0 && Sequence == LastSequence;
		}

	private:
		SimCoreProtocol::FServerHealth Health;
		FString BoundMapChecksum;
		uint64 BoundGeneration = 0;
		uint64 LastSequence = 0;
		double ReceiveTimeSeconds = 0.0;
	};
}
