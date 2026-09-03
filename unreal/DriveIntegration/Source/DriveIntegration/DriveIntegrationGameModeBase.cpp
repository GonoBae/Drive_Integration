// Copyright Epic Games, Inc. All Rights Reserved.


#include "DriveIntegrationGameModeBase.h"

#include "ExternalVehiclePawn.h"
#include "SimCoreInstrumentCluster.h"

ADriveIntegrationGameModeBase::ADriveIntegrationGameModeBase()
{
	DefaultPawnClass = AExternalVehiclePawn::StaticClass();
	HUDClass = ASimCoreInstrumentClusterHud::StaticClass();
}
