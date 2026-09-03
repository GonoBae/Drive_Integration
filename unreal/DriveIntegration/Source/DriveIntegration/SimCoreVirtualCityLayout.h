#pragma once

#include "CoreMinimal.h"

/** Meter-based, asset-free blockout shared by the editor builder and its tests. */
namespace SimCoreVirtualCity
{
	enum class EPalette : uint8
	{
		Ground, Road, Sidewalk, Curb, Building, Glass, Marking, Yellow, Barrier, Grass
	};

	struct FBox
	{
		FName Id;
		// ENU=(East,North,Up); Unreal world=(North,East,Up)*100.
		FVector CenterEnuM = FVector::ZeroVector;
		// Box-local X=forward length, Y=right width, Z=height.
		FVector SizeM = FVector::OneVector;
		double HeadingDegrees = 0.0; // North=0, East=90.
		double PitchDegrees = 0.0; // Positive raises the forward end.
		EPalette Palette = EPalette::Ground;
		bool bGround = false;
		bool bStaticCollider = false;
	};

	struct FCheckpoint
	{
		FVector PositionEnuM = FVector::ZeroVector;
		double HeadingDegrees = 0.0;
	};

	struct FLayout
	{
		TArray<FBox> Boxes;
		TArray<FCheckpoint> DriveRoute;
		FVector GroundCenterEnuM = FVector::ZeroVector;
		// Ordered North then East to match Unreal horizontal X,Y extents.
		FVector2D GroundHalfExtentNorthEastM = FVector2D::ZeroVector;
	};

	DRIVEINTEGRATION_API FLayout BuildLayout();
}
