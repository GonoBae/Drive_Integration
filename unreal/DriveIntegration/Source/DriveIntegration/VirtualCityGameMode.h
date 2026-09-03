#pragma once

#include "CoreMinimal.h"
#include "DriveIntegrationGameModeBase.h"
#include "ExternalVehiclePawn.h"
#include "VirtualCityGameMode.generated.h"

/** Map-specific client identity; the Landscape pawn keeps its existing defaults. */
UCLASS()
class DRIVEINTEGRATION_API AVirtualCityVehiclePawn : public AExternalVehiclePawn
{
	GENERATED_BODY()

public:
	AVirtualCityVehiclePawn();
};

UCLASS()
class DRIVEINTEGRATION_API AVirtualCityGameMode : public ADriveIntegrationGameModeBase
{
	GENERATED_BODY()

public:
	AVirtualCityGameMode();
};
