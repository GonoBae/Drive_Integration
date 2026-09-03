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
}
