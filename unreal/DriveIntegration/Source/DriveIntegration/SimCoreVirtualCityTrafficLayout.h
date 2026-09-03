#pragma once

#include "CoreMinimal.h"

/** Authored right-hand traffic topology; deliberately independent of the QA DriveRoute. */
namespace SimCoreVirtualCity
{
	namespace TrafficLaneIds
	{
		inline constexpr uint32 MainEastApproach = 100;
		inline constexpr uint32 MainEastConnector = 110;
		inline constexpr uint32 MainEastExit = 120;
		inline constexpr uint32 MainWestApproach = 200;
		inline constexpr uint32 MainWestConnector = 210;
		inline constexpr uint32 MainWestExit = 220;
		inline constexpr uint32 MainWestToBranch = 300;
		inline constexpr uint32 BranchIncoming = 310;
		inline constexpr uint32 BranchToWest = 320;
		inline constexpr uint32 BranchToEast = 330;
		inline constexpr uint32 BranchTerminal = 340;
	}

	struct FTrafficLane
	{
		uint32 Id = 0;
		double WidthM = 3.0;
		double SpeedLimitMps = 8.333333;
		// A nonzero group controls the END of this approach, not its connector.
		uint32 SignalGroupId = 0;
		bool bTerminal = false;
		TArray<FVector> PointsEnuM;
		TArray<uint32> Successors;
	};

	struct FTrafficSignal
	{
		uint32 Id = 0;
		uint32 GroupId = 0;
		// Pole base on collision ground, not the elevated lens position.
		FVector PositionEnuM = FVector::ZeroVector;
		// Direction of approaching traffic: North=0, East=90. Lenses face backwards.
		double HeadingDegrees = 0.0;
	};

	struct FTrafficLayout
	{
		TArray<FTrafficLane> Lanes;
		TArray<FTrafficSignal> Signals;
	};

	/** Two directional loops, explicit T connectors, and an unconnected terminal bay.
	 * Group 1: main straight + westbound right turn only. Group 2: branch incoming.
	 * No main eastbound left turn and no synthetic bay U-turn are authored.
	 * Signal/stop-line presentation is NoCollision; it adds no SimCore static bodies.
	 */
	DRIVEINTEGRATION_API FTrafficLayout BuildTrafficLayout();
	DRIVEINTEGRATION_API bool ValidateTrafficLayout(const FTrafficLayout& Layout, FString& OutError);
	/** Stable numeric ID/key ordering, invariant JSON numbers, LF, no timestamps. */
	DRIVEINTEGRATION_API bool SerializeTrafficLayout(const FTrafficLayout& Layout,
		const FString& SourceMapChecksum, FString& OutJson, FString& OutError);
}
