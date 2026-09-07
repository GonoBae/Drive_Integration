#include "SimCoreClientComponent.h"

#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "SimCoreCoordinateFrames.h"
#include "SimCoreNpcPresentationActor.h"
#include "SimCorePedestrianPresentationActor.h"
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
			else
			{
				Actor = World->SpawnActor<ASimCorePedestrianPresentationActor>(
					ASimCorePedestrianPresentationActor::StaticClass(), FTransform::Identity, Parameters);
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
		else if (ASimCorePedestrianPresentationActor* Pedestrian = Cast<ASimCorePedestrianPresentationActor>(Actor))
		{
			bApplied = Pedestrian->ApplySnapshot(Entity, static_cast<float>(Age), DeltaSeconds,
				bMotionAllowed, RuntimeEntityMaxExtrapolationSeconds, RuntimeEntityPresentationOffsetCm);
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

void USimCoreClientComponent::DrawRuntimeEntityDebug() const
{
	UWorld* World = GetWorld();
	const double Age = FPlatformTime::Seconds() - RuntimeEntityReceiveTimeSeconds;
	if (!World || !World->IsGameWorld() || !bHasState || !IsConnected()
		|| !FMath::IsFinite(Age) || Age < 0.0 || Age > SimCoreTrafficSignals::SnapshotFreshnessSeconds) return;
	for (const auto& Pair : RuntimeEntityStates)
	{
		const auto& State = Pair.Value;
		const AActor* Actor = RuntimeEntityActors.FindRef(Pair.Key).Get();
		if (!Actor || Actor->IsHidden()) continue;
		const FVector Centre = Actor->GetActorLocation();
		const bool bPedestrian = State.EntityKind == SimCoreProtocol::EEntityKind::Pedestrian;
		if (bPedestrian && !State.bPedestrianDowned)
		{
			DrawDebugCapsule(World, Centre, State.CollisionHalfHeightMeters * 100.0f,
				State.CollisionRadiusMeters * 100.0f, FQuat::Identity, FColor::Cyan,
				false, -1.0f, 0, 1.5f);
		}
		else
		{
			// Downed extents are the server's already pitch/roll-projected vertical
			// OBB. Applying visual pitch a second time would draw a standing ghost.
			const FQuat CollisionRotation = bPedestrian
				? FRotator(0.0, State.HeadingDegrees, 0.0).Quaternion() : Actor->GetActorQuat();
			DrawDebugBox(World, Centre,
				FVector(State.CollisionHalfLengthMeters, State.CollisionHalfWidthMeters,
					State.CollisionHalfHeightMeters) * 100.0,
				CollisionRotation, State.bPedestrianAirborne ? FColor::Yellow : FColor::Cyan,
				false, -1.0f, 0, 1.5f);
		}
		DrawDebugSphere(World, Centre, 5.0f, 8, FColor::White, false, -1.0f);
		DrawDebugString(World, Centre + FVector::UpVector * 115.0,
			FString::Printf(TEXT("%u %s event %u"), State.EntityId,
				State.bPedestrianAirborne ? TEXT("AIRBORNE")
					: (State.bPedestrianDowned ? TEXT("DOWNED") : TEXT("UPRIGHT")),
				State.CollisionEventSequence), nullptr, FColor::White, 0.0f, false, 0.8f);
	}
}
