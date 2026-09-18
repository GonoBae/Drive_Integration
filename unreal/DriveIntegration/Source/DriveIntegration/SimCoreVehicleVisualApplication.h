#pragma once

#include "CoreMinimal.h"
#include "SimCoreProtocol.h"

class UStaticMesh;
class UStaticMeshComponent;
class USimCoreTurnSignals;
class USimCoreDriverPresentation;
class USimCoreSuspensionPresentation;
namespace SimCoreVehicleVisualProfile { struct FProfile; }

namespace SimCoreVehicleVisualApplication
{
// Borrowed assets; the calling Actor retains their UObject ownership.
struct FBodyMeshes
{
	UStaticMesh* Sedan;
	UStaticMesh* Compact;
	UStaticMesh* Truck;
	UStaticMesh* Motorcycle;
};

struct FBodySelection
{
	SimCoreProtocol::ERuntimeVehicleClass VehicleClass;
	UStaticMesh* BodyMesh;
};

FBodySelection SelectBody(SimCoreProtocol::ERuntimeVehicleClass VehicleClass,
	const FBodyMeshes& Meshes);
void ReplaceBodyMesh(UStaticMeshComponent& Body, UStaticMesh& Mesh);

// Callers retain wheel topology, collision origins and damage-reset ordering.
void ConfigureAttachments(const SimCoreVehicleVisualProfile::FProfile& Profile,
	USimCoreTurnSignals& TurnSignals, USimCoreDriverPresentation& Driver,
	USimCoreSuspensionPresentation& Suspension);
}
