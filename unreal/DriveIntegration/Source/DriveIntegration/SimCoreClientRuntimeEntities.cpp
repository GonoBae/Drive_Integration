#include "SimCoreClientComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "SimCoreNpcPresentationActor.h"
#include "SimCorePresentation.h"
#include "SimCoreTrafficSignalActor.h"

void USimCoreClientComponent::SyncRuntimeProxyActors(
	const TArray<SimCoreProtocol::FVehicleState>& Entities,
	double ReceiveTimeSeconds)
{
	// Only the accepted Hello/map/play/generation/sequence path calls this.
	// Cache one authoritative snapshot, then predict its presentation per tick.
	RuntimeEntityStates.Reset();
	RuntimeEntityReceiveTimeSeconds = ReceiveTimeSeconds;
	TSet<uint32> SeenEntityIds;
	for (const SimCoreProtocol::FVehicleState& Entity : Entities)
	{
		if (Entity.EntityId == static_cast<uint32>(FMath::Max(ControlledEntityId, 1))
			|| (Entity.EntityKind != SimCoreProtocol::EEntityKind::NpcVehicle
				&& Entity.EntityKind != SimCoreProtocol::EEntityKind::Pedestrian))
		{
			continue;
		}
		SeenEntityIds.Add(Entity.EntityId);
		RuntimeEntityStates.Add(Entity.EntityId, Entity);
		AActor* Actor = RuntimeEntityActors.FindRef(Entity.EntityId).Get();
		const SimCoreProtocol::EEntityKind* ExistingKind =
			RuntimeEntityActorKinds.Find(Entity.EntityId);
		if (Actor != nullptr && (!ExistingKind || *ExistingKind != Entity.EntityKind))
		{
			Actor->Destroy();
			RuntimeEntityActors.Remove(Entity.EntityId);
			RuntimeEntityActorKinds.Remove(Entity.EntityId);
		}
	}

	TArray<uint32> ExistingEntityIds;
	RuntimeEntityActors.GetKeys(ExistingEntityIds);
	for (const uint32 EntityId : ExistingEntityIds)
	{
		if (SeenEntityIds.Contains(EntityId))
		{
			continue;
		}
		if (AActor* Actor = RuntimeEntityActors.FindRef(EntityId).Get())
		{
			Actor->Destroy();
		}
		RuntimeEntityActors.Remove(EntityId);
		RuntimeEntityActorKinds.Remove(EntityId);
	}
	TickRuntimeProxyActors(0.0f);
}

void USimCoreClientComponent::TickRuntimeProxyActors(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	const double Age = FPlatformTime::Seconds() - RuntimeEntityReceiveTimeSeconds;
	if (!bShowRuntimeEntities || !World || !World->IsGameWorld() || !bHasState
		|| !IsConnected() || !bProtocolHandshakeComplete || !bMapHandshakeComplete
		|| !FMath::IsFinite(Age) || Age < 0.0
		|| Age > SimCoreTrafficSignals::SnapshotFreshnessSeconds)
	{
		DestroyRuntimeProxyActors();
		return;
	}
	// A stopped/unknown lease may still supply a fresh authoritative pose, but
	// cannot drive prediction or wheel spin. Health never grants NPC authority.
	const auto& Health = LatestState.ServerHealth;
	const bool bMotionAllowed = Health.bPresent && Health.bHasControlCommand
		&& Health.Status == SimCoreProtocol::EServerHealthStatus::Active
		&& !GlobalEstopHealthCache.HasEstopHealth()
		&& GlobalEstopHealthCache.CanUsePlaySnapshot(LatestState.Sequence);
	for (const auto& Pair : RuntimeEntityStates)
	{
		const auto& Entity = Pair.Value;
		AActor* Actor = RuntimeEntityActors.FindRef(Pair.Key).Get();
		if (!Actor)
		{
			FActorSpawnParameters Parameters;
			Parameters.Owner = GetOwner();
			Parameters.ObjectFlags |= RF_Transient;
			Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			if (Entity.EntityKind == SimCoreProtocol::EEntityKind::NpcVehicle)
			{
				Actor = World->SpawnActor<ASimCoreNpcPresentationActor>(
					ASimCoreNpcPresentationActor::StaticClass(), FTransform::Identity, Parameters);
			}
			else if (RuntimePedestrianMesh)
			{
				AStaticMeshActor* Pedestrian = World->SpawnActor<AStaticMeshActor>(
					AStaticMeshActor::StaticClass(), FTransform::Identity, Parameters);
				if (Pedestrian)
				{
					auto* Mesh = Pedestrian->GetStaticMeshComponent();
					Mesh->SetMobility(EComponentMobility::Movable);
					Mesh->SetStaticMesh(RuntimePedestrianMesh);
					Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					Mesh->SetGenerateOverlapEvents(false);
					Mesh->SetCanEverAffectNavigation(false);
					Mesh->SetSimulatePhysics(false);
					Pedestrian->SetActorEnableCollision(false);
					Pedestrian->Tags.AddUnique(TEXT("SimCoreRuntimeEntity"));
					Actor = Pedestrian;
				}
			}
			if (Actor)
			{
				RuntimeEntityActors.Add(Pair.Key, Actor);
				RuntimeEntityActorKinds.Add(Pair.Key, Entity.EntityKind);
			}
		}
		if (!Actor) continue;
		bool bApplied = false;
		if (ASimCoreNpcPresentationActor* Npc = Cast<ASimCoreNpcPresentationActor>(Actor))
		{
			bApplied = Npc->ApplySnapshot(Entity, static_cast<float>(Age), DeltaSeconds,
				bMotionAllowed, RuntimeEntityMaxExtrapolationSeconds, RuntimeEntityPresentationOffsetCm);
		}
		else
		{
			SimCorePresentation::FRuntimeEntityPresentationSample Sample;
			bApplied = SimCorePresentation::BuildRuntimeEntitySample(Entity,
				bMotionAllowed ? static_cast<float>(Age) : 0.0f,
				RuntimeEntityMaxExtrapolationSeconds, RuntimeEntityPresentationOffsetCm, Sample);
			if (bApplied)
			{
				Actor->SetActorLocationAndRotation(Sample.ActorLocation, Sample.ActorRotation,
					false, nullptr, ETeleportType::TeleportPhysics);
				Actor->SetActorScale3D(Sample.ActorScale);
			}
		}
		if (!bApplied)
		{
			Actor->Destroy();
			RuntimeEntityActors.Remove(Pair.Key);
			RuntimeEntityActorKinds.Remove(Pair.Key);
		}
	}
}

void USimCoreClientComponent::DestroyRuntimeProxyActors()
{
	for (const TPair<uint32, TWeakObjectPtr<AActor>>& Pair
		: RuntimeEntityActors)
	{
		if (AActor* Actor = Pair.Value.Get())
		{
			Actor->Destroy();
		}
	}
	RuntimeEntityActors.Reset();
	RuntimeEntityActorKinds.Reset();
	RuntimeEntityStates.Reset();
	RuntimeEntityReceiveTimeSeconds = 0.0;
}
