#include "SimCoreVehicleVisualApplication.h"

#include "Components/StaticMeshComponent.h"
#include "SimCoreDriverPresentation.h"
#include "SimCoreSuspensionPresentation.h"
#include "SimCoreTurnSignals.h"
#include "SimCoreVehicleVisualProfile.h"

namespace SimCoreVehicleVisualApplication
{
FBodySelection SelectBody(SimCoreProtocol::ERuntimeVehicleClass VehicleClass,
	const FBodyMeshes& Meshes)
{
	using SimCoreProtocol::ERuntimeVehicleClass;
	if (VehicleClass == ERuntimeVehicleClass::Unspecified)
	{
		VehicleClass = ERuntimeVehicleClass::Sedan;
	}
	UStaticMesh* BodyMesh = nullptr;
	switch (VehicleClass)
	{
	case ERuntimeVehicleClass::Sedan: BodyMesh = Meshes.Sedan; break;
	case ERuntimeVehicleClass::Compact: BodyMesh = Meshes.Compact; break;
	case ERuntimeVehicleClass::Truck: BodyMesh = Meshes.Truck; break;
	case ERuntimeVehicleClass::Motorcycle: BodyMesh = Meshes.Motorcycle; break;
	default: break;
	}
	return {VehicleClass, BodyMesh};
}

void ReplaceBodyMesh(UStaticMeshComponent& Body, UStaticMesh& Mesh)
{
	Body.EmptyOverrideMaterials();
	Body.SetStaticMesh(&Mesh);
	Body.SetRelativeTransform(FTransform::Identity);
	Body.SetVisibility(true);
}

void ConfigureAttachments(const SimCoreVehicleVisualProfile::FProfile& Profile,
	USimCoreTurnSignals& TurnSignals, USimCoreDriverPresentation& Driver,
	USimCoreSuspensionPresentation& Suspension)
{
	TurnSignals.SetLampPositions(MakeArrayView(Profile.LampLocationsCm));
	Driver.ConfigureVehicleClass(Profile.VehicleClass);
	Suspension.Configure(Profile.VehicleClass);
}
}
