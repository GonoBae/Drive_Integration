#pragma once
#include "CoreMinimal.h"

class UWorld;
class AActor;

namespace SimCoreLiveGroundPatch {
// A revision identifies the collision state observed on the game thread. It is
// not a snapshot of asynchronous physics or a replacement for network fencing.
class DRIVEINTEGRATION_API FWorldRevision {
public:
    static constexpr int32 MaxPrimitives = 8192;
    static constexpr int32 MaxInstances = 65536;
    explicit FWorldRevision(int32 PrimitiveBudget = MaxPrimitives, int32 InstanceBudget = MaxInstances)
        : PrimitiveLimit(FMath::Clamp(PrimitiveBudget,0,MaxPrimitives)),
          InstanceLimit(FMath::Clamp(InstanceBudget,0,MaxInstances)) {}
    bool Poll(UWorld* World, const AActor* IgnoredActor, const FVector& MapOriginCm);
    void MarkChanged() { bExplicitChange = true; }
    uint64 GetRevision() const { return Revision; }
    bool IsReady() const { return bReady; }
private:
    TArray<uint64> Previous;
    TWeakObjectPtr<UWorld> PreviousWorld;
    FVector PreviousOrigin = FVector::ZeroVector;
    uint64 Revision = 0;
    bool bReady = false;
    bool bExplicitChange = false;
    int32 PrimitiveLimit, InstanceLimit;
};
}
