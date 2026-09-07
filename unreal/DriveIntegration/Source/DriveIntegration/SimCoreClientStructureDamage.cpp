#include "SimCoreClientComponent.h"

#include "Engine/World.h"
#include "SimCoreStructureDamageActor.h"
#include "SimCoreTrafficSignalActor.h"

void USimCoreClientComponent::TickStructureDamage(float /*DeltaSeconds*/)
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || !bHasState)
	{
		DestroyStructureDamageActors();
		return;
	}
	if (PresentedStructureMapChecksum != LatestState.MapPackageChecksum)
	{
		DestroyStructureDamageActors();
		PresentedStructureMapChecksum = LatestState.MapPackageChecksum;
	}
	const bool bReady = IsConnected() && bProtocolHandshakeComplete && bMapHandshakeComplete
		&& !GlobalEstopHealthCache.HasEstopHealth();
	const double Age = FPlatformTime::Seconds() - LatestStateReceiveTimeSeconds;
	if (!bReady || !bTrafficSnapshotAccepted || !FMath::IsFinite(Age) || Age < 0.0
		|| Age > SimCoreTrafficSignals::SnapshotFreshnessSeconds) return;
	TSet<FString> Seen;
	for (const SimCoreProtocol::FStructureState& Structure : LatestState.Structures)
	{
		if (Structure.Kind != SimCoreProtocol::EStructureKind::Building || Structure.DamagePercent <= 0.0f
			|| !SimCoreProtocol::IsValidStructureState(Structure)) continue;
		Seen.Add(Structure.ColliderId);
		ASimCoreStructureDamageActor* Actor = StructureDamageActors.FindRef(Structure.ColliderId).Get();
		if (!Actor && StructureDamageActors.Num() < 32)
		{
			FActorSpawnParameters Parameters;
			Parameters.Owner = GetOwner();
			Parameters.ObjectFlags |= RF_Transient;
			Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Actor = World->SpawnActor<ASimCoreStructureDamageActor>(
				ASimCoreStructureDamageActor::StaticClass(), FTransform::Identity, Parameters);
			if (Actor) StructureDamageActors.Add(Structure.ColliderId, Actor);
		}
		if (Actor) Actor->ApplyAuthoritativeDamage(Structure, RuntimeEntityPresentationOffsetCm);
	}
	for (auto It = StructureDamageActors.CreateIterator(); It; ++It)
	{
		if (!Seen.Contains(It.Key()))
		{
			if (ASimCoreStructureDamageActor* Actor = It.Value().Get()) Actor->Destroy();
			It.RemoveCurrent();
		}
	}
}

void USimCoreClientComponent::DestroyStructureDamageActors()
{
	for (const auto& Pair : StructureDamageActors)
	{
		if (ASimCoreStructureDamageActor* Actor = Pair.Value.Get()) Actor->Destroy();
	}
	StructureDamageActors.Reset();
	PresentedStructureMapChecksum.Reset();
}
