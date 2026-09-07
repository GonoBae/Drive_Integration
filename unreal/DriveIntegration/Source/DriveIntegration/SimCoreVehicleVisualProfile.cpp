#include "SimCoreVehicleVisualProfile.h"

#include "SimCoreSedanVisualContract.h"

namespace SimCoreVehicleVisualProfile
{
bool FProfile::IsWheelVisible(const int32 Index) const
{
	return Index >= 0 && Index < 4
		&& (VehicleClass != SimCoreProtocol::ERuntimeVehicleClass::Motorcycle || Index == 0 || Index == 2);
}

bool Resolve(SimCoreProtocol::ERuntimeVehicleClass VehicleClass, FProfile& OutProfile)
{
	using SimCoreProtocol::ERuntimeVehicleClass;
	if (VehicleClass == ERuntimeVehicleClass::Unspecified) VehicleClass = ERuntimeVehicleClass::Sedan;
	OutProfile = {};
	OutProfile.VehicleClass = VehicleClass;
	const TConstArrayView<FVector> SedanWheels = SimCoreSedanVisualContract::WheelOriginsCm();
	switch (VehicleClass)
	{
	case ERuntimeVehicleClass::Sedan:
	case ERuntimeVehicleClass::Compact:
	{
		const bool bCompact = VehicleClass == ERuntimeVehicleClass::Compact;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const FVector& Origin = SedanWheels[Index];
			const FVector Lamp = SimCoreSedanVisualContract::TurnSignalLensPointCm(
				Index < 2, Index % 2 == 0, 0.5, 0.5);
			OutProfile.WheelOriginsCm[Index] = bCompact
				? FVector(Origin.X * 0.79, Origin.Y * 0.90, Origin.Z * 0.92 - 1.5) : Origin;
			OutProfile.LampLocationsCm[Index] = bCompact
				? FVector(Lamp.X * 0.79 - 4.0, Lamp.Y * 0.90, Lamp.Z * 0.92 - 1.5) : Lamp;
			OutProfile.WheelScales[Index] = bCompact ? FVector(0.86, 0.82, 0.86) : FVector::OneVector;
		}
		if (bCompact)
		{
			OutProfile.HalfHeightMeters = 0.70f;
			OutProfile.CgHeightMeters = 0.48f;
			OutProfile.ExhaustLocationCm = FVector(-170.0, 48.0, -22.0);
			OutProfile.DriverTransform = FTransform(FQuat::Identity,
				FVector(-4.0, 0.0, -1.5), FVector(0.79, 0.90, 0.92));
		}
		return true;
	}
	case ERuntimeVehicleClass::Truck:
		OutProfile.HalfHeightMeters = 1.25f;
		OutProfile.CgHeightMeters = 0.75f;
		OutProfile.ExhaustLocationCm = FVector(-290.0, 86.0, -15.0);
		OutProfile.DriverTransform = FTransform(FQuat::Identity,
			FVector(110.0, 0.0, 62.0), FVector(0.8, 1.0, 1.0));
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const bool bFront = Index < 2;
			const double Side = Index % 2 == 0 ? -1.0 : 1.0;
			OutProfile.WheelOriginsCm[Index] = FVector(bFront ? 190.0 : -195.0, Side * 102.0, -28.0);
			OutProfile.WheelScales[Index] = FVector(1.22, 1.08, 1.22);
			OutProfile.LampLocationsCm[Index] = FVector(bFront ? 240.0 : -294.0, Side * 96.0, bFront ? 58.0 : 64.0);
		}
		return true;
	case ERuntimeVehicleClass::Motorcycle:
		OutProfile.HalfHeightMeters = 0.68f;
		OutProfile.CgHeightMeters = 0.42f;
		OutProfile.ExhaustLocationCm = FVector(-102.0, 18.0, 18.0);
		OutProfile.WheelOriginsCm[0] = FVector(99.0, 0.0, -8.0);
		OutProfile.WheelOriginsCm[2] = FVector(-82.0, 0.0, -8.0);
		OutProfile.WheelScales[0] = OutProfile.WheelScales[2] = FVector(1.05, 0.42, 1.05);
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const bool bFront = Index < 2;
			const double Side = Index % 2 == 0 ? -1.0 : 1.0;
			OutProfile.LampLocationsCm[Index] = FVector(bFront ? 93.0 : -94.0,
				Side * (bFront ? 29.0 : 27.0), bFront ? 61.0 : 47.0);
		}
		return true;
	default:
		return false;
	}
}
}
