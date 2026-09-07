#pragma once

#include "CoreMinimal.h"
#include "SimCoreVirtualCityLayout.h"

/**
 * Asset-free, meter/ENU authoring source for the second small-city course.
 *
 * This is deliberately independent from BuildVirtualCity's v1 source.  A
 * future editor commandlet may persist it as signal_city_v2 without changing
 * L_VirtualCity or virtual_city_v1.
 */
namespace SimCoreSignalCity
{
	inline constexpr TCHAR MapId[] = TEXT("signal_city_v2");

	DRIVEINTEGRATION_API SimCoreVirtualCity::FLayout BuildLayout();
	/** Shared south-to-north centerline for road, paint, curbs and traffic. */
	DRIVEINTEGRATION_API TArray<FVector> BuildCollectorCenterline(bool bEast);
	DRIVEINTEGRATION_API TArray<FVector> OffsetCollectorCenterline(
		const TArray<FVector>& Centerline, double OffsetM);
	/** Last saved generation before road and traffic adopted a common curve. */
	DRIVEINTEGRATION_API SimCoreVirtualCity::FLayout BuildAlignedCollectorMarkingsV3Layout();
	/** First shared-curve generation, before outer-joint asphalt overlap correction. */
	DRIVEINTEGRATION_API SimCoreVirtualCity::FLayout BuildUnsealedCollectorCurvesV4Layout();
	/** Shared curves before the minimum-radius sidewalk correction. */
	DRIVEINTEGRATION_API SimCoreVirtualCity::FLayout BuildSealedCollectorCurvesV5Layout();
	/** Shared road geometry before the visible three-to-one lane merges. */
	DRIVEINTEGRATION_API SimCoreVirtualCity::FLayout BuildUnmergedCollectorLanesV6Layout();
	/** Exact prior generated geometry, only for guarded in-place traffic-lane migration. */
	DRIVEINTEGRATION_API SimCoreVirtualCity::FLayout BuildLegacySingleLaneLayout();
	/** Exact first three-lane generation, before sidewalk-safe crossing placement. */
	DRIVEINTEGRATION_API SimCoreVirtualCity::FLayout BuildInitialThreeLaneLayout();
	/** Exact safe-crossing layout before every public-road segment received complete lane boundaries. */
	DRIVEINTEGRATION_API SimCoreVirtualCity::FLayout BuildIncompleteLaneMarkingsLayout();
	/** Exact raised, chord-to-chord lane paint used by the first complete-lane migration. */
	DRIVEINTEGRATION_API SimCoreVirtualCity::FLayout BuildRaisedLaneMarkingsLayout();
	/** Exact natural-paint v2 geometry, including the crossed collector tapers, for guarded migration only. */
	DRIVEINTEGRATION_API SimCoreVirtualCity::FLayout BuildNaturalLaneMarkingsV2Layout();
}
