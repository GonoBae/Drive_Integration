#pragma once
#include "SimCoreProtocol.h"
#include "SimCoreWorldRevision.h"

class UWorld;
class AActor;
namespace SimCoreLiveGroundPatch {
inline constexpr int32 Side = 25;
inline constexpr int32 Count = Side*Side;
inline constexpr double SpacingM = 0.5;
inline constexpr int32 CellCount = (Side-1)*(Side-1);
inline constexpr int32 MaxBoundaries = 128;
// A scan spans several frames. Nothing is sent until the grid and bounded
// sidewall verification pass complete; absent boundaries remain unconfirmed.
// Scene changes invalidate partial scans; the server fences installed patches
// when it receives the revision notice. This is not an atomic network snapshot.
class DRIVEINTEGRATION_API FSampler {
public:
    const SimCoreProtocol::FGroundQueryResult* Tick(UWorld* World, const AActor* IgnoredActor,
        const FVector& MapOriginCm, const SimCoreProtocol::FGroundQueryBatch& Request);
    // Secondary tile jobs share the ego sampler's observation from this game
    // frame, avoiding another full-scene fingerprint traversal.
    const SimCoreProtocol::FGroundQueryResult* TickObservedRevision(UWorld* World, const AActor* IgnoredActor,
        const FVector& MapOriginCm, const SimCoreProtocol::FGroundQueryBatch& Request,
        uint64 ObservedRevision, bool bObservedWorldReady);
    void Reset();
    void PollWorld(UWorld* World, const AActor* IgnoredActor, const FVector& MapOriginCm);
    void NotifyWorldChanged() { WorldRevision.MarkChanged(); }
    uint64 GetWorldRevision() const { return WorldRevision.GetRevision(); }
    bool IsWorldReady() const { return WorldRevision.IsReady(); }
private:
    FWorldRevision WorldRevision;
    SimCoreProtocol::FGroundQueryBatch Active;
    SimCoreProtocol::FGroundQueryResult Result;
    int32 Cursor = 0;
    int32 BoundaryCursor = 0;
    bool bComplete = false;
};
}
