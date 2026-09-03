#include "SimCoreSedanVisualContract.h"

namespace
{
	const FVector WheelOrigins[] = {
		FVector(121.5, -79.0, -23.0),
		FVector(121.5, 79.0, -23.0),
		FVector(-148.5, -79.0, -23.0),
		FVector(-148.5, 79.0, -23.0),
	};
	static_assert(UE_ARRAY_COUNT(WheelOrigins) == SimCoreSedanVisualContract::WheelCount);
}

namespace SimCoreSedanVisualContract
{
const TCHAR* BodyPackagePath()
{
	return TEXT("/Game/Vehicles/Sedan/SM_SedanBody");
}

const TCHAR* BodyObjectPath()
{
	return TEXT("/Game/Vehicles/Sedan/SM_SedanBody.SM_SedanBody");
}

const TCHAR* WheelPackagePath()
{
	return TEXT("/Game/Vehicles/Sedan/SM_SedanWheel");
}

const TCHAR* WheelObjectPath()
{
	return TEXT("/Game/Vehicles/Sedan/SM_SedanWheel.SM_SedanWheel");
}

TConstArrayView<FVector> WheelOriginsCm()
{
	return MakeArrayView(WheelOrigins);
}
}
