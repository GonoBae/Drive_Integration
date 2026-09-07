#pragma once

#include "CoreMinimal.h"
#include "SimCoreProtocol.h"

namespace SimCoreVehicleVisualProfile
{
// Mesh-local centimetres. Physics dimensions remain in the server configuration.
struct FProfile
{
	SimCoreProtocol::ERuntimeVehicleClass VehicleClass = SimCoreProtocol::ERuntimeVehicleClass::Sedan;
	FVector WheelOriginsCm[4] = {FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector};
	FVector WheelScales[4] = {FVector::OneVector, FVector::OneVector, FVector::OneVector, FVector::OneVector};
	FVector LampLocationsCm[4] = {FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector};
	FVector ExhaustLocationCm = FVector(-221.0, 55.0, -25.0);
	FTransform DriverTransform = FTransform::Identity;
	float HalfHeightMeters = 0.75f;
	float CgHeightMeters = 0.55f;

	bool IsWheelVisible(int32 Index) const;
};

bool Resolve(SimCoreProtocol::ERuntimeVehicleClass VehicleClass, FProfile& OutProfile);
}
