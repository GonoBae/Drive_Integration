// Copyright Epic Games, Inc. All Rights Reserved.


#include "DriveIntegrationGameModeBase.h"

#include "ExternalVehiclePawn.h"

ADriveIntegrationGameModeBase::ADriveIntegrationGameModeBase()
{
	DefaultPawnClass = AExternalVehiclePawn::StaticClass();
}
