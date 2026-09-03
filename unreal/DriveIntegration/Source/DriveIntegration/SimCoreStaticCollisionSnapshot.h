#pragma once

#include "CoreMinimal.h"

class UWorld;

/** Pure measurement/serialization boundary shared by editor Bake and automation. */
namespace SimCoreStaticCollisionSnapshot
{
	/** Serialize registered SimCore markers without writing files or showing a dialog. */
	DRIVEINTEGRATION_API bool BuildCsv(
		UWorld* World,
		const FVector& MapOriginWorldCm,
		FString& OutCsv,
		int32& OutColliderCount,
		FString& OutError);
}
