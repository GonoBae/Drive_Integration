#include "SignalCityGameMode.h"

#include "SimCoreClientComponent.h"

ASignalCityVehiclePawn::ASignalCityVehiclePawn()
{
	SimCoreClient->MapPackageDirectory = TEXT("../../map_packages/signal_city_v2");
	SimCoreClient->SourceId = TEXT("unreal-signal-city");
}

ASignalCityGameMode::ASignalCityGameMode()
{
	DefaultPawnClass = ASignalCityVehiclePawn::StaticClass();
}
