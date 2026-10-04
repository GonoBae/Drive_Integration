#include "SimCoreLiveGroundPatch.h"
#include "SimCoreGroundQueryShadow.h"
#include "SimCoreCoordinateFrames.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

namespace {
using namespace SimCoreProtocol;

double PlaneHeight(const FGroundProbeResult& Reference, const FVector3d& Point)
{
    const auto Delta = Point-Reference.PointEnu;
    const auto& Normal = Reference.NormalEnu;
    return Reference.PointEnu.Z-(Delta.X*Normal.X+Delta.Y*Normal.Y)/Normal.Z;
}

// Corner heights may identify a candidate step, but never its boundary.
// Two collision traces must measure the same dividing wall before it is sent.
bool StepCorners(const FGroundQueryResult& Result, int32 Cell,
    const FGroundProbeResult* (&Corners)[4], bool (&High)[4], double& MidLevel)
{
    const int32 Index = (Cell/24)*25 + Cell%24;
    Corners[0] = &Result.Probes[Index]; Corners[1] = &Result.Probes[Index+1];
    Corners[2] = &Result.Probes[Index+25]; Corners[3] = &Result.Probes[Index+26];
    double Low = 0, Upper = 0, Levels[4];
    bool bSamePlane = true;
    for (int32 I=0; I<4; ++I)
    {
        const auto* Corner = Corners[I];
        if (Corner->Status != EGroundProbeStatus::Hit || Corner->NormalEnu.Z < .995
            || FVector3d::DotProduct(Corner->NormalEnu, Corners[0]->NormalEnu) < .995) return false;
        // Remove the common road grade before comparing the two surface levels.
        Levels[I] = Corner->PointEnu.Z-PlaneHeight(*Corners[0],Corner->PointEnu);
        Low = FMath::Min(Low, Levels[I]);
        Upper = FMath::Max(Upper, Levels[I]);
        bSamePlane &= FMath::Abs(Levels[I]) <= .025;
    }
    if (bSamePlane || Upper-Low <= .025 || Upper-Low > .5) return false;
    MidLevel = (Low+Upper)*.5;
    for (int32 I=0; I<4; ++I)
    {
        High[I] = Levels[I] > MidLevel;
        if (FMath::Abs(Levels[I]-(High[I] ? Upper : Low)) > .01) return false;
    }
    return true;
}

bool TraceBoundary(UWorld* World, const AActor* IgnoredActor, const FVector& MapOriginCm,
    const FGroundProbeResult* const (&Corners)[4], const bool (&High)[4], double MidLevel,
    FGroundQueryResult::FCellBoundary& Boundary)
{
    constexpr int32 Edges[4][2] = {{0,1},{0,2},{1,3},{2,3}};
    int32 CrossingEdges[2] = {-1,-1}, Crossings = 0;
    for (int32 Edge=0; Edge<4; ++Edge)
        if (High[Edges[Edge][0]] != High[Edges[Edge][1]])
        {
            if (Crossings == 2) return false; // checkerboard heights cannot define one divider
            CrossingEdges[Crossings++] = Edge;
        }
    if (Crossings != 2) return false;
    FCollisionObjectQueryParams Objects;
    Objects.AddObjectTypesToQuery(ECC_WorldStatic);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(SimCoreLiveGroundBoundary), true);
    if (IgnoredActor) Params.AddIgnoredActor(IgnoredActor);
    FVector3d Points[2], Normals[2];
    for (int32 Crossing=0; Crossing<2; ++Crossing)
    {
        const auto& Edge = Edges[CrossingEdges[Crossing]];
        FVector3d Low = Corners[High[Edge[0]] ? Edge[1] : Edge[0]]->PointEnu;
        FVector3d Upper = Corners[High[Edge[0]] ? Edge[0] : Edge[1]]->PointEnu;
        Low.Z = PlaneHeight(*Corners[0],Low)+MidLevel;
        Upper.Z = PlaneHeight(*Corners[0],Upper)+MidLevel;
        FHitResult Hit;
        const FVector Start = SimCoreCoordinateFrames::MapEnuPositionMetersToUnrealCentimeters(Low, MapOriginCm);
        const FVector End = SimCoreCoordinateFrames::MapEnuPositionMetersToUnrealCentimeters(Upper, MapOriginCm);
        if (!World->LineTraceSingleByObjectType(Hit, Start, End, Objects, Params)
            || !Hit.bBlockingHit || Hit.bStartPenetrating || FMath::Abs(Hit.ImpactNormal.Z) > .1) return false;
        Points[Crossing] = SimCoreCoordinateFrames::UnrealPositionCentimetersToMapEnuMeters(Hit.ImpactPoint, MapOriginCm);
        auto Normal = SimCoreCoordinateFrames::UnrealWorldPolarVectorToMapEnu(Hit.ImpactNormal);
        Normal.Z = 0;
        if (!Normal.Normalize()) return false;
        Normals[Crossing] = Normal;
        const auto& Point = Points[Crossing];
        if (Point.ContainsNaN() || Point.X < Corners[0]->PointEnu.X-.001
            || Point.X > Corners[1]->PointEnu.X+.001 || Point.Y < Corners[0]->PointEnu.Y-.001
            || Point.Y > Corners[2]->PointEnu.Y+.001
            || FVector3d::DotProduct(Normal, Upper-Low) >= -.001) return false;
        for (int32 I=0; I<4; ++I)
        {
            const double Side = FVector3d::DotProduct(Normal, Corners[I]->PointEnu-Point);
            if (High[I] ? Side > .001 : Side < -.001) return false;
        }
    }
    if ((Points[1]-Points[0]).SizeSquared() < .000025
        || FVector3d::DotProduct(Normals[0], Normals[1]) < .999
        || FMath::Abs(FVector3d::DotProduct(Normals[0], Points[1]-Points[0])) > .005
        || FMath::Abs(FVector3d::DotProduct(Normals[1], Points[1]-Points[0])) > .005) return false;
    Boundary.PointEnu = Points[0];
    Boundary.NormalEnu = Normals[0];
    return true;
}
}

void SimCoreLiveGroundPatch::FSampler::Reset()
{
    Active = {}; Result = {}; Cursor = 0; BoundaryCursor = 0; bComplete = false;
}

void SimCoreLiveGroundPatch::FSampler::PollWorld(UWorld* World, const AActor* IgnoredActor, const FVector& MapOriginCm)
{
    if (WorldRevision.Poll(World, IgnoredActor, MapOriginCm)) Reset();
}

const SimCoreProtocol::FGroundQueryResult* SimCoreLiveGroundPatch::FSampler::Tick(UWorld* World,
    const AActor* IgnoredActor, const FVector& MapOriginCm, const SimCoreProtocol::FGroundQueryBatch& Request)
{
    PollWorld(World, IgnoredActor, MapOriginCm);
    return TickObservedRevision(World,IgnoredActor,MapOriginCm,Request,GetWorldRevision(),IsWorldReady());
}

const SimCoreProtocol::FGroundQueryResult* SimCoreLiveGroundPatch::FSampler::TickObservedRevision(UWorld* World,
    const AActor* IgnoredActor, const FVector& MapOriginCm, const SimCoreProtocol::FGroundQueryBatch& Request,
    uint64 ObservedRevision, bool bObservedWorldReady)
{
    using namespace SimCoreProtocol;
    if (Active.WorldRevision != ObservedRevision || !bObservedWorldReady) Reset();
    if (!IsInGameThread() || !World || !IsValidGroundQueryBatch(Request) || Request.OriginsEnu.Num() != 1
        || Request.QueryDistancesMeters.Num() != 1 || Request.QueryDistancesMeters[0] != 32
        || Request.MaxDistanceMeters != 0 || Request.PatchStepEnu == FVector3d::ZeroVector
        || MapOriginCm.ContainsNaN() || !bObservedWorldReady || !ObservedRevision
        || Request.WorldRevision != ObservedRevision) return nullptr;
    if (Active.RequestId != Request.RequestId || Active.EnvironmentEpoch != Request.EnvironmentEpoch)
    {
        Active = Request;
        Result = {};
        Result.RequestId = Request.RequestId;
        Result.EnvironmentEpoch = Request.EnvironmentEpoch;
        Result.ReferenceSimulationTimeNs = Request.ReferenceSimulationTimeNs;
        Result.WorldRevision = Request.WorldRevision;
        Cursor = 0;
        BoundaryCursor = 0;
        bComplete = false;
    }
    if (bComplete) return nullptr;
    const bool bMeasuringGrid = Cursor < Count;
    // At most 128 traces per game frame, in two valid 64-ray batches.
    for (int32 Slice=0; Slice<2 && Cursor<Count; ++Slice)
    {
        FGroundQueryBatch Batch = Active;
        Batch.OriginsEnu.Reset();
        Batch.QueryDistancesMeters.Reset();
        Batch.PatchStepEnu = FVector3d::ZeroVector;
        Batch.WorldRevision = 0;
        const int32 End = FMath::Min(Cursor+64, Count);
        for (; Cursor<End; ++Cursor)
        {
            FVector3d Origin = Active.OriginsEnu[0];
            Origin.X += (Cursor%Side-Side/2)*Active.PatchStepEnu.X;
            Origin.Y += (Cursor/Side-Side/2)*Active.PatchStepEnu.Y;
            Batch.OriginsEnu.Add(Origin);
            Batch.QueryDistancesMeters.Add(32);
        }
        auto Partial = SimCoreGroundQueryShadow::Measure(World, IgnoredActor, MapOriginCm, Batch);
        if (Partial.Probes.Num() != Batch.OriginsEnu.Num()) { Reset(); return nullptr; }
        Result.MeasurementDurationMs += Partial.MeasurementDurationMs;
        Result.Probes.Append(MoveTemp(Partial.Probes));
    }
    // Keep downward ground and sidewall trace work in separate game frames.
    if (bMeasuringGrid) return nullptr;
    const double BoundaryStarted = FPlatformTime::Seconds();
    int32 TraceBudget = 128;
    while (BoundaryCursor < CellCount && Result.Boundaries.Num() < MaxBoundaries && TraceBudget >= 2)
    {
        const int32 Cell = BoundaryCursor++;
        const FGroundProbeResult* Corners[4]; bool High[4]; double MidLevel;
        if (!StepCorners(Result, Cell, Corners, High, MidLevel)) continue;
        TraceBudget -= 2;
        FGroundQueryResult::FCellBoundary Boundary;
        Boundary.CellIndex = Cell;
        if (TraceBoundary(World, IgnoredActor, MapOriginCm, Corners, High, MidLevel, Boundary))
            Result.Boundaries.Add(Boundary);
    }
    Result.MeasurementDurationMs += (FPlatformTime::Seconds()-BoundaryStarted)*1000.0;
    if (BoundaryCursor < CellCount && Result.Boundaries.Num() < MaxBoundaries) return nullptr;
    bComplete = true;
    return &Result;
}
