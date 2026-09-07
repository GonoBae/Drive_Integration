#pragma once

#include "CoreMinimal.h"

/** Versioned traffic authoring for the two-controller signal-city course. */
namespace SimCoreSignalCity
{
	enum class ETrafficSignalKind : uint8
	{
		Vehicle,
		Pedestrian,
	};

	struct FTrafficLaneChange
	{
		uint32 TargetLaneId = 0;
		double SourceBeginM = 0.0;
		double SourceEndM = 0.0;
		double TargetBeginM = 0.0;
		double TargetEndM = 0.0;
	};

	struct FTrafficLane
	{
		uint32 Id = 0;
		double WidthM = 3.0;
		double SpeedLimitMps = 8.333333;
		// Controls entry beyond this lane's endpoint, not travel along the lane.
		uint32 SignalGroupId = 0;
		bool bTerminal = false;
		TArray<FVector> PointsEnuM;
		TArray<uint32> Successors;
		// Optional v2 extension. Arc-length windows exclude intersection/merge fans.
		TArray<FTrafficLaneChange> LaneChanges;
	};

	struct FTrafficSignal
	{
		uint32 Id = 0;
		uint32 ControllerId = 0;
		uint32 GroupId = 0;
		FVector PositionEnuM = FVector::ZeroVector;
		double HeadingDegrees = 0.0;
		ETrafficSignalKind Kind = ETrafficSignalKind::Vehicle;
	};

	struct FSignalPhase
	{
		uint32 DurationMs = 0;
		TArray<uint32> GreenGroups;
		TArray<uint32> YellowGroups;
	};

	struct FSignalPlan
	{
		uint32 Id = 0;
		uint32 OffsetMs = 0;
		TArray<uint32> Groups;
		TArray<FSignalPhase> Phases;
	};

	struct FTrafficLayout
	{
		TArray<FTrafficLane> Lanes;
		TArray<FTrafficSignal> Signals;
		TArray<FSignalPlan> SignalPlans;
	};

	DRIVEINTEGRATION_API FTrafficLayout BuildTrafficLayout();
	DRIVEINTEGRATION_API bool ValidateTrafficLayout(
		const FTrafficLayout& Layout, FString& OutError);
	/** Stable numeric ID/key ordering, invariant JSON numbers, LF, no timestamps. */
	DRIVEINTEGRATION_API bool SerializeTrafficLayout(
		const FTrafficLayout& Layout, const FString& SourceMapChecksum,
		FString& OutJson, FString& OutError);
}
