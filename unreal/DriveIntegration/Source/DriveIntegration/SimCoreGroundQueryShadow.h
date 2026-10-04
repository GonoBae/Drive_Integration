#pragma once
#include "SimCoreProtocol.h"
class UWorld;
class AActor;

namespace SimCoreGroundQueryShadow
{
	/** Game-thread WorldStatic measurements only; no force application or file writes. */
	DRIVEINTEGRATION_API SimCoreProtocol::FGroundQueryResult Measure(UWorld* World,
		const AActor* IgnoredActor, const FVector& MapOriginCm, const SimCoreProtocol::FGroundQueryBatch& Batch);
}
