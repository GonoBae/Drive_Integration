#include "SimCoreClientDiagnostics.h"

SimCoreClientDiagnostics::FHealthDisplay SimCoreClientDiagnostics::EvaluateServerHealth(
	const SimCoreProtocol::FServerHealth& Health,
	bool bConnectionReady,
	bool bHasSnapshot,
	double LocalSnapshotAgeSeconds,
	double StaleTimeoutSeconds)
{
	FHealthDisplay Display;
	if (!bConnectionReady)
	{
		Display.Reason = TEXT("Connection / Hello / map handshake not ready");
		return Display;
	}
	if (!bHasSnapshot)
	{
		Display.Reason = TEXT("Waiting for this play session's authoritative snapshot");
		return Display;
	}
	if (!FMath::IsFinite(LocalSnapshotAgeSeconds) || LocalSnapshotAgeSeconds < 0.0
		|| !FMath::IsFinite(StaleTimeoutSeconds) || StaleTimeoutSeconds <= 0.0
		|| LocalSnapshotAgeSeconds > StaleTimeoutSeconds)
	{
		Display.Status = TEXT("Stale");
		Display.Reason = TEXT("No fresh snapshot; current server safety state is unknown");
		return Display;
	}
	if (!Health.bPresent)
	{
		Display.Reason = TEXT("Server did not provide WorldState Health");
		return Display;
	}
	using SimCoreProtocol::EServerHealthStatus;
	switch (Health.Status)
	{
	case EServerHealthStatus::AwaitingReset: Display.Status = TEXT("AwaitingReset"); break;
	case EServerHealthStatus::AwaitingControl: Display.Status = TEXT("AwaitingControl"); break;
	case EServerHealthStatus::Active:
		if (!Health.bHasControlCommand)
		{
			Display.Reason = TEXT("Inconsistent Health: active without an accepted command");
			return Display;
		}
		Display.Status = TEXT("Active");
		Display.Color = FColor(80, 220, 120);
		break;
	case EServerHealthStatus::SafeStop:
		Display.Status = TEXT("SafeStop");
		Display.Color = FColor(255, 80, 80);
		break;
	case EServerHealthStatus::ReconnectRequired:
		Display.Status = TEXT("ReconnectRequired");
		Display.Color = FColor(255, 80, 80);
		break;
	case EServerHealthStatus::EstopLatched:
		Display.Status = TEXT("EStopLatched");
		Display.Color = FColor(255, 80, 80);
		break;
	case EServerHealthStatus::Unknown:
	default:
		Display.Reason = TEXT("Unsupported or missing server Health status");
		return Display;
	}
	Display.bAuthoritative = true;
	Display.Reason = Health.Message.IsEmpty()
		? TEXT("No additional server reason") : Health.Message;
	return Display;
}

bool SimCoreClientDiagnostics::FGlobalEstopHealthCache::Observe(
	const SimCoreProtocol::FVehicleState& Snapshot,
	uint64 IncomingGeneration,
	uint64 CurrentGeneration,
	bool bVerifiedHello,
	const FString& ExpectedMapChecksum,
	double InReceiveTimeSeconds)
{
	// Validate everything before advancing the high-water mark: an invalid
	// generation/map or an old frame must not poison or refresh this cache.
	if (CurrentGeneration == 0 || IncomingGeneration != CurrentGeneration
		|| !bVerifiedHello || ExpectedMapChecksum.IsEmpty()
		|| !Snapshot.MapPackageChecksum.Equals(ExpectedMapChecksum, ESearchCase::CaseSensitive)
		|| (BoundGeneration != 0 && BoundGeneration != CurrentGeneration)
		|| (!BoundMapChecksum.IsEmpty()
			&& !BoundMapChecksum.Equals(ExpectedMapChecksum, ESearchCase::CaseSensitive))
		|| Snapshot.Sequence == 0 || Snapshot.Sequence <= LastSequence
		|| !FMath::IsFinite(InReceiveTimeSeconds) || InReceiveTimeSeconds < 0.0)
	{
		return false;
	}
	BoundGeneration = CurrentGeneration;
	BoundMapChecksum = ExpectedMapChecksum;
	LastSequence = Snapshot.Sequence;
	Health = {};
	ReceiveTimeSeconds = 0.0;
	if (Snapshot.ServerHealth.bPresent
		&& Snapshot.ServerHealth.Status == SimCoreProtocol::EServerHealthStatus::EstopLatched)
	{
		Health = Snapshot.ServerHealth;
		ReceiveTimeSeconds = InReceiveTimeSeconds;
	}
	// A newer non-EStop frame can invalidate the cache, but can never install
	// Active (or a pose) from another play lifetime into this read-only path.
	return true;
}

void SimCoreClientDiagnostics::FGlobalEstopHealthCache::Reset()
{
	*this = FGlobalEstopHealthCache{};
}
