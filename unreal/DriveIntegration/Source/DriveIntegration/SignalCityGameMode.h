#pragma once

#include "CoreMinimal.h"
#include "DriveIntegrationGameModeBase.h"
#include "ExternalVehiclePawn.h"
#include "SignalCityGameMode.generated.h"

/** Map-specific client identity for the versioned signal-city course. */
UCLASS()
class DRIVEINTEGRATION_API ASignalCityVehiclePawn : public AExternalVehiclePawn
{
	GENERATED_BODY()

public:
	ASignalCityVehiclePawn();
};

UCLASS()
class DRIVEINTEGRATION_API ASignalCityGameMode : public ADriveIntegrationGameModeBase
{
	GENERATED_BODY()

public:
	ASignalCityGameMode();
};
