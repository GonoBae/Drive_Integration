#include "SimCoreWorldObstacleQuery.h"
#include "SimCoreWorldCollisionFilter.h"
#include "SimCoreCoordinateFrames.h"
#include "SimCoreSignalCityLayout.h"
#include "SimCoreNpcPresentationActor.h"
#include "SimCoreStructureDamageActor.h"
#include "SimCoreTrafficSignalActor.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Misc/SecureHash.h"
#include "ActorEditorUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCoreWorldObstacles, Log, All);

namespace {
using namespace SimCoreProtocol;
struct FAuthoringMetadata { bool bGroundOnly = false; uint32 Semantic = 2; };
const TMap<FName,FAuthoringMetadata>& SignalCityMetadata()
{
    static const auto Metadata=[] {
        TMap<FName,FAuthoringMetadata> Out;
        for (const auto& Box : SimCoreSignalCity::BuildLayout().Boxes)
            Out.Add(Box.Id,{Box.bGround && !Box.bStaticCollider,
                Box.Palette == SimCoreVirtualCity::EPalette::Curb ? 1u
                : Box.Palette == SimCoreVirtualCity::EPalette::Barrier ? 2u : 0u});
        return Out;
    }();
    return Metadata;
}

FString StableComponentId(const UPrimitiveComponent* Primitive)
{
    const FTCHARToUTF8 Path(*Primitive->GetPathName());
    FSHAHash Hash; FSHA1::HashBuffer(Path.Get(),Path.Length(),Hash.Hash);
    return TEXT("ue_")+Hash.ToString();
}

void ReportUnsupported(UPrimitiveComponent* Primitive, bool bRegisteredSemantic, bool bSignalCity, const TCHAR* Reason)
{
    static double LastReport = -1;
    const double Now=FPlatformTime::Seconds();
    if (Now-LastReport<1) return;
    LastReport=Now;
    const auto* Mesh=Cast<UStaticMeshComponent>(Primitive);
    UBodySetup* Setup=Primitive->GetBodySetup();
    UE_LOG(LogSimCoreWorldObstacles, Log,
        TEXT("Unsupported collider reason=%s component=%s type=%s mesh=%s registry=%d city=%d boxes=%d shapes=%d rotation=%s scale=%s"),
        Reason,*Primitive->GetPathName(),*Primitive->GetClass()->GetName(),
        Mesh && Mesh->GetStaticMesh() ? *Mesh->GetStaticMesh()->GetPathName() : TEXT("none"),
        bRegisteredSemantic,bSignalCity,Setup ? Setup->AggGeom.BoxElems.Num() : -1,
        Setup ? Setup->AggGeom.GetElementCount() : -1,*Primitive->GetComponentRotation().ToString(),
        *Primitive->GetComponentScale().ToString());
}

bool ExtractBox(UPrimitiveComponent* Primitive, const FVector& MapOriginCm, FWorldStaticBox& Out)
{
    FVector LocalCentre = FVector::ZeroVector, LocalHalfExtent;
    FQuat LocalRotation = FQuat::Identity;
    UBodySetup* Setup = Primitive->GetBodySetup();
    if (const auto* Box=Cast<UBoxComponent>(Primitive)) LocalHalfExtent=Box->GetUnscaledBoxExtent();
    else if (const auto* Mesh=Cast<UStaticMeshComponent>(Primitive)) {
        if (Mesh->IsA<UInstancedStaticMeshComponent>() || !Mesh->GetStaticMesh() || !Setup
            || Setup->AggGeom.BoxElems.Num()!=1 || Setup->AggGeom.GetElementCount()!=1
            || (Mesh->GetStaticMesh()->GetPathName()!=TEXT("/Engine/BasicShapes/Cube.Cube")
                && Setup->GetCollisionTraceFlag()!=CTF_UseSimpleAsComplex)) return false;
        const auto& BodyBox=Setup->AggGeom.BoxElems[0];
        LocalCentre=BodyBox.Center; LocalHalfExtent=FVector(BodyBox.X,BodyBox.Y,BodyBox.Z)*.5; LocalRotation=BodyBox.Rotation.Quaternion();
    } else return false;
    const auto& Transform=Primitive->GetComponentTransform();
    FVector X=Transform.TransformVector(LocalRotation.GetAxisX());
    FVector Y=Transform.TransformVector(LocalRotation.GetAxisY());
    FVector Z=Transform.TransformVector(LocalRotation.GetAxisZ());
    const FVector Scale(X.Size(),Y.Size(),Z.Size());
    if (Scale.GetMin()<=UE_DOUBLE_SMALL_NUMBER || !X.Normalize() || !Y.Normalize() || !Z.Normalize()
        || FMath::Abs(X.Z)>.00001 || FMath::Abs(Y.Z)>.00001 || FMath::Abs(Z.Z)<.99999
        || FMath::Abs(FVector::DotProduct(X,Y))>.00001
        || FMath::Abs(FVector::DotProduct(X,Z))>.00001 || FMath::Abs(FVector::DotProduct(Y,Z))>.00001) return false;
    Out.CentreEnu=SimCoreCoordinateFrames::UnrealPositionCentimetersToMapEnuMeters(Transform.TransformPosition(LocalCentre),MapOriginCm);
    Out.HalfExtentEnu=LocalHalfExtent*Scale*.01;
    Out.HeadingRad=FMath::Atan2(X.Y,X.X); // navigation heading: North=0, clockwise positive
    if (Out.CentreEnu.ContainsNaN() || Out.HalfExtentEnu.ContainsNaN() || Out.HalfExtentEnu.GetMin()<=0) return false;
    // No explicit physical material keeps the generated-course contact values.
    const UPhysicalMaterial* Material=Primitive->BodyInstance.GetPhysMaterialOverride();
    if (!Material && Setup) Material=Setup->PhysMaterial;
    if (Material) {
        Out.Friction=Material->Friction; Out.Restitution=Material->Restitution;
        if (!FMath::IsFinite(Out.Friction) || Out.Friction<0 || Out.Friction>4
            || !FMath::IsFinite(Out.Restitution) || Out.Restitution<0 || Out.Restitution>1) return false;
    }
    return true;
}
}

SimCoreProtocol::FWorldObstacleResult SimCoreWorldObstacleQuery::Measure(UWorld* World,
    const AActor* IgnoredActor, const FVector& MapOriginCm, const FWorldObstacleRequest& Request)
{
    FWorldObstacleResult Result;
    Result.RequestId=Request.RequestId; Result.EnvironmentEpoch=Request.EnvironmentEpoch;
    Result.ReferenceSimulationTimeNs=Request.ReferenceSimulationTimeNs; Result.WorldRevision=Request.WorldRevision;
    if (!IsInGameThread() || !IsValid(World) || MapOriginCm.ContainsNaN()
        || !IsValidWorldObstacleRequest(Request) || !Request.WorldRevision) return Result;
    const FVector Centre=SimCoreCoordinateFrames::MapEnuPositionMetersToUnrealCentimeters(Request.CentreEnu,MapOriginCm);
    const FVector Extent=SimCoreCoordinateFrames::MapEnuPolarVectorToUnrealWorld(Request.HalfExtentEnu)*100;
    const FBox Bounds(Centre-Extent,Centre+Extent);
    bool bSignalCity=false;
    int32 ActorsVisited=0, ComponentsVisited=0;
    for (const auto* Level : World->GetLevels()) if (Level) for (AActor* Actor : Level->Actors) {
        if (++ActorsVisited>32768) { Result.Status=EWorldObstacleStatus::Overflow; return Result; }
        if (IsValid(Actor) && Actor->ActorHasTag(TEXT("SimCore.SignalCity.Generated.v2"))
            && Actor->ActorHasTag(TEXT("SimCore.SignalCity.KoreanNorthDistrict.v8"))) bSignalCity=true;
    }
    const auto* Metadata=bSignalCity ? &SignalCityMetadata() : nullptr;
    TSet<FString> ColliderIds;
    for (const auto* Level : World->GetLevels()) if (Level) for (AActor* Actor : Level->Actors)
    {
        if (!IsValid(Actor) || Actor==IgnoredActor || FActorEditorUtils::IsABuilderBrush(Actor) || !Actor->GetActorEnableCollision()
            || Actor->IsA<ASimCoreNpcPresentationActor>() || Actor->IsA<ASimCoreStructureDamageActor>()
            || Actor->IsA<ASimCoreTrafficSignalActor>()) continue;
        const bool bGroundActor=Actor->ActorHasTag(TEXT("SimCore.SignalCity.Generated.v2"))
            && Actor->ActorHasTag(TEXT("SimCore.SignalCity.KoreanNorthDistrict.v8"));
        for (auto* Component : Actor->GetComponents()) {
            if (++ComponentsVisited>131072) { Result.Status=EWorldObstacleStatus::Overflow; Result.Colliders.Reset(); return Result; }
            auto* Primitive=Cast<UPrimitiveComponent>(Component);
            if (!IsValid(Primitive) || !Primitive->IsRegistered() || !Primitive->IsQueryCollisionEnabled()
                || Primitive->GetCollisionObjectType()!=ECC_WorldStatic || !Bounds.Intersect(Primitive->Bounds.GetBox())) continue;
            if (SimCoreWorldQuery::IsRenderOnlyDebugComponent(Primitive)) continue;
            const FName Name=bGroundActor ? Primitive->GetFName() : Actor->GetFName();
            const auto* Authored=Metadata ? Metadata->Find(Name) : nullptr;
            if (Authored && Authored->bGroundOnly) continue;
            // A dedicated opt-in ground-only tag is distinct from surface tags:
            // Rough/Asphalt also appear on curb colliders and never suppress them.
            if (!Authored && Primitive->ComponentHasTag(TEXT("SimCore.GroundOnly"))) continue;
            FWorldStaticBox Box;
            Box.ColliderId=Authored ? Name.ToString() : StableComponentId(Primitive);
            Box.Semantic=Authored ? Authored->Semantic : 2;
            if (!ExtractBox(Primitive,MapOriginCm,Box)) {
                ReportUnsupported(Primitive,Authored!=nullptr,bSignalCity,TEXT("collision-shape"));
                Result.Colliders.Reset(); return Result;
            }
            const double Sine=FMath::Abs(FMath::Sin(Box.HeadingRad)), Cosine=FMath::Abs(FMath::Cos(Box.HeadingRad));
            const FVector BoxExtent=(FVector(Box.HalfExtentEnu.X*Cosine+Box.HalfExtentEnu.Y*Sine,
                Box.HalfExtentEnu.X*Sine+Box.HalfExtentEnu.Y*Cosine,Box.HalfExtentEnu.Z))*100;
            const FVector BoxCentre=SimCoreCoordinateFrames::MapEnuPositionMetersToUnrealCentimeters(Box.CentreEnu,MapOriginCm);
            if (!Bounds.Intersect(FBox(BoxCentre-BoxExtent,BoxCentre+BoxExtent))) continue;
            if (ColliderIds.Contains(Box.ColliderId)) {
                ReportUnsupported(Primitive,Authored!=nullptr,bSignalCity,TEXT("duplicate-id"));
                Result.Colliders.Reset(); return Result;
            }
            ColliderIds.Add(Box.ColliderId);
            if (Result.Colliders.Num()>=128) { Result.Status=EWorldObstacleStatus::Overflow; Result.Colliders.Reset(); return Result; }
            Result.Colliders.Add(MoveTemp(Box));
        }
    }
    Result.Status=EWorldObstacleStatus::Complete;
    return Result;
}
