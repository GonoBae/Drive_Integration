#include "SimCoreGroundQueryShadow.h"
#include "SimCoreGroundSnapshot.h"
#include "SimCoreCoordinateFrames.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

SimCoreProtocol::FGroundQueryResult SimCoreGroundQueryShadow::Measure(UWorld* World,
	const AActor* IgnoredActor, const FVector& MapOriginCm, const SimCoreProtocol::FGroundQueryBatch& Batch)
{
	using namespace SimCoreProtocol;
	FGroundQueryResult Result;
	Result.RequestId = Batch.RequestId;
	Result.EnvironmentEpoch = Batch.EnvironmentEpoch;
	Result.ReferenceSimulationTimeNs = Batch.ReferenceSimulationTimeNs;
	const double Started = FPlatformTime::Seconds();
	if (!IsInGameThread() || !World || !IsValidGroundQueryBatch(Batch)
		|| MapOriginCm.ContainsNaN()) return Result;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SimCoreGroundShadow), true);
	Params.bReturnPhysicalMaterial = true;
	if (IgnoredActor) Params.AddIgnoredActor(IgnoredActor);
	for (int32 Index = 0; Index < Batch.OriginsEnu.Num(); ++Index)
	{
		const FVector3d& Origin = Batch.OriginsEnu[Index];
		const double Depth = Batch.QueryDistancesMeters.IsEmpty() ? Batch.MaxDistanceMeters : Batch.QueryDistancesMeters[Index];
		FGroundProbeResult Probe;
		if (!Origin.ContainsNaN() && Origin.GetAbsMax() <= 1e6)
		{
			const FVector Start = SimCoreCoordinateFrames::MapEnuPositionMetersToUnrealCentimeters(Origin, MapOriginCm);
			const FVector End = Start - FVector(0,0,Depth*100.0);
			FHitResult Hit;
			if (!World->LineTraceSingleByObjectType(Hit, Start, End, Objects, Params)) Probe.Status = EGroundProbeStatus::NoHit;
			else if (Hit.bBlockingHit && !Hit.bStartPenetrating && Hit.ImpactNormal.Z > 0
				&& SimCoreGroundSnapshot::ClassifyDefaultSurface(Hit, Probe.SurfaceMaterialId))
			{
				Probe.Status = EGroundProbeStatus::Hit;
				Probe.PointEnu = SimCoreCoordinateFrames::UnrealPositionCentimetersToMapEnuMeters(Hit.ImpactPoint, MapOriginCm);
				Probe.NormalEnu = SimCoreCoordinateFrames::UnrealWorldPolarVectorToMapEnu(Hit.ImpactNormal.GetSafeNormal());
			}
		}
		Result.Probes.Add(Probe);
	}
	Result.MeasurementDurationMs = (FPlatformTime::Seconds()-Started)*1000.0;
	return Result;
}
