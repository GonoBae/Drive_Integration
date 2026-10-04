#pragma once
#include "SimCoreProtocol.h"

class UWorld;
class AActor;

namespace SimCoreWorldObstacleQuery {
// The supported volume contract is an upright, measured collision box. An
// unsupported shape makes the whole reply incomplete; it is never omitted.
DRIVEINTEGRATION_API SimCoreProtocol::FWorldObstacleResult Measure(UWorld* World,
    const AActor* IgnoredActor, const FVector& MapOriginCm,
    const SimCoreProtocol::FWorldObstacleRequest& Request);
}
