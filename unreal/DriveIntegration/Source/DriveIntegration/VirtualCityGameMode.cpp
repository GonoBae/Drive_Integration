#include "VirtualCityGameMode.h"

#include "SimCoreClientComponent.h"

AVirtualCityVehiclePawn::AVirtualCityVehiclePawn()
{
	SimCoreClient->MapPackageDirectory = TEXT("../../map_packages/virtual_city_v1");
	SimCoreClient->SourceId = TEXT("unreal-virtual-city");
}

AVirtualCityGameMode::AVirtualCityGameMode()
{
	DefaultPawnClass = AVirtualCityVehiclePawn::StaticClass();
}
