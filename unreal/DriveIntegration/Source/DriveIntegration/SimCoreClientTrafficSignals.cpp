#include "SimCoreClientComponent.h"

#include "Engine/World.h"
#include "SimCoreTrafficSignalActor.h"

void USimCoreClientComponent::TickTrafficSignals()
{
	UWorld* World = GetWorld();
	if (!bShowRuntimeTrafficSignals || !World || !World->IsGameWorld() || !bHasState)
	{
		DestroyTrafficSignalActors();
		return;
	}
	const bool bReady = IsConnected() && bProtocolHandshakeComplete && bMapHandshakeComplete
		&& !GlobalEstopHealthCache.HasEstopHealth();
	const bool bAccepted = bTrafficSnapshotAccepted
		&& SimCoreProtocol::IsValidTrafficNetworkChecksum(LatestState.TrafficNetworkChecksum);
	const double Age = FPlatformTime::Seconds() - LatestStateReceiveTimeSeconds;
	// Replacing a network invalidates every old head, even when it reuses IDs.
	if (!PresentedTrafficNetworkChecksum.Equals(LatestState.TrafficNetworkChecksum, ESearchCase::CaseSensitive))
	{
		DestroyTrafficSignalActors();
		PresentedTrafficNetworkChecksum = LatestState.TrafficNetworkChecksum;
	}
	TSet<uint32> Seen;
	for (const SimCoreProtocol::FTrafficSignalState& Signal : LatestState.TrafficSignals)
	{
		Seen.Add(Signal.SignalId);
		ASimCoreTrafficSignalActor* Actor = TrafficSignalActors.FindRef(Signal.SignalId).Get();
		if (!Actor && bReady && bAccepted)
		{
			FActorSpawnParameters Parameters;
			Parameters.Owner = GetOwner();
			Parameters.ObjectFlags |= RF_Transient;
			Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Actor = World->SpawnActor<ASimCoreTrafficSignalActor>(
				ASimCoreTrafficSignalActor::StaticClass(), FTransform::Identity, Parameters);
			if (Actor) TrafficSignalActors.Add(Signal.SignalId, Actor);
		}
		if (Actor)
		{
			Actor->ApplyAuthoritativeSignal(Signal, bReady, bAccepted, Age, RuntimeEntityPresentationOffsetCm);
		}
	}
	for (auto It = TrafficSignalActors.CreateIterator(); It; ++It)
	{
		if (!Seen.Contains(It.Key()))
		{
			if (ASimCoreTrafficSignalActor* Actor = It.Value().Get()) Actor->Destroy();
			It.RemoveCurrent();
		}
	}
}

void USimCoreClientComponent::InvalidateTrafficSignals()
{
	bTrafficSnapshotAccepted = false;
	for (const auto& Pair : TrafficSignalActors)
	{
		if (ASimCoreTrafficSignalActor* Actor = Pair.Value.Get()) Actor->SetFailSafe();
	}
}

void USimCoreClientComponent::DestroyTrafficSignalActors()
{
	for (const auto& Pair : TrafficSignalActors)
	{
		if (ASimCoreTrafficSignalActor* Actor = Pair.Value.Get())
		{
			Actor->SetFailSafe();
			Actor->Destroy();
		}
	}
	TrafficSignalActors.Reset();
	PresentedTrafficNetworkChecksum.Reset();
}

FString USimCoreClientComponent::BuildTrafficSignalStatusText() const
{
	if (!bHasState || LatestState.TrafficSignals.IsEmpty()) return TEXT("signals: none");
	const bool bReady = IsConnected() && bProtocolHandshakeComplete && bMapHandshakeComplete
		&& !GlobalEstopHealthCache.HasEstopHealth();
	const bool bAccepted = bTrafficSnapshotAccepted
		&& SimCoreProtocol::IsValidTrafficNetworkChecksum(LatestState.TrafficNetworkChecksum);
	const double Age = FPlatformTime::Seconds() - LatestStateReceiveTimeSeconds;
	FString Text = TEXT("signals:");
	for (int32 Index = 0; Index < FMath::Min(LatestState.TrafficSignals.Num(), 4); ++Index)
	{
		const auto& Signal = LatestState.TrafficSignals[Index];
		const auto Display = SimCoreTrafficSignals::EvaluateDisplay(Signal, bReady, bAccepted, Age);
		Text += Display.bVerified
			? FString::Printf(TEXT(" S%u/G%u %s %.1fs"), Signal.SignalId, Signal.GroupId,
				Display.bGreen ? TEXT("GREEN") : Display.bYellow ? TEXT("YELLOW") : TEXT("RED"),
				Display.RemainingSeconds)
			: FString::Printf(TEXT(" S%u/G%u RED(unverified/stale)"), Signal.SignalId, Signal.GroupId);
	}
	return Text;
}
