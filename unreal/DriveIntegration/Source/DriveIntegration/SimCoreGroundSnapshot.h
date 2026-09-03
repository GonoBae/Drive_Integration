#pragma once

#include "CoreMinimal.h"

class AActor;
class UWorld;

/**
 * Unreal-owned collision measurement and serialization boundary.
 *
 * The editor samples the authored collision here. SimCore consumes the
 * immutable result and remains solely responsible for vehicle physics.
 */
namespace SimCoreGroundSnapshot
{
	inline constexpr int64 HardMaximumSampleCount = 2000000;

	/** Append-only IDs shared with C++ GroundSurfaceMaterialId. */
	enum class ESurfaceMaterialId : uint32
	{
		Default = 0,
		Asphalt = 1,
		LowFriction = 2,
		Rough = 3,
	};

	struct FBuildRequest
	{
		UWorld* World = nullptr;
		const AActor* IgnoredActor = nullptr;
		const AActor* GroundActor = nullptr;
		FVector SamplingCenterWorldCm = FVector::ZeroVector;
		FQuat SamplingYaw = FQuat::Identity;
		FVector MapOriginWorldCm = FVector::ZeroVector;
		double HorizontalExtentXCm = 0.0;
		double HorizontalExtentYCm = 0.0;
		double SampleSpacingCm = 0.0;
		double TraceStartZCm = 0.0;
		double TraceEndZCm = 0.0;
		double MinimumGroundNormalZ = 0.0;
		double MaximumCellHeightDeltaCm = 0.0;
		uint8 AsphaltPhysicalSurface = 1;
		uint8 LowFrictionPhysicalSurface = 2;
		uint8 RoughPhysicalSurface = 3;
		float DefaultFrictionMultiplier = 1.0f;
		float AsphaltFrictionMultiplier = 1.0f;
		float LowFrictionFrictionMultiplier = 0.35f;
		float RoughFrictionMultiplier = 1.10f;
		int64 MaximumSampleCount = HardMaximumSampleCount;
		bool bApplyHeightDiscontinuityFilter = false;
	};

	struct FBuildResult
	{
		TArray<uint8> Binary;
		int32 Columns = 0;
		int32 Rows = 0;
		int32 ValidSampleCount = 0;
		int32 DrivableCellCount = 0;
		int32 DefaultMaterialCellCount = 0;
		int32 AsphaltMaterialCellCount = 0;
		int32 LowFrictionMaterialCellCount = 0;
		int32 RoughMaterialCellCount = 0;
		int32 SkippedMissingCellCount = 0;
		int32 SkippedDiscontinuousCellCount = 0;
		double StepXCm = 0.0;
		double StepYCm = 0.0;
		FVector MinimumEnuM = FVector::ZeroVector;
		FVector MaximumEnuM = FVector::ZeroVector;
		bool bMapOriginCovered = false;
	};

	/** Build a validated little-endian SIMGHF2 collision snapshot. */
	bool Build(
		const FBuildRequest& Request,
		FBuildResult& OutResult,
		FString& OutError);
}
