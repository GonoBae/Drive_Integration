#include "SimCoreVehicleVisualProfile.h"

#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCoreVehicleCatalog, Log, All);

namespace
{
struct FLiveCatalog
{
	SimCoreVehicleVisualProfile::FCatalog Catalog;
	FString Error;
	bool bValid = false;
	FLiveCatalog()
	{
		bValid = SimCoreVehicleVisualProfile::LoadCatalog(
			FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("VehicleCatalog/catalog.json")), Catalog, Error);
		if (!bValid) UE_LOG(LogSimCoreVehicleCatalog, Error, TEXT("Vehicle catalog rejected: %s"), *Error);
	}
};

const FLiveCatalog& LiveCatalog()
{
	static const FLiveCatalog Snapshot;
	return Snapshot;
}
}

namespace SimCoreVehicleVisualProfile
{
bool FProfile::IsWheelVisible(const int32 Index) const
{
	return Index >= 0 && Index < 4
		&& (VehicleClass != SimCoreProtocol::ERuntimeVehicleClass::Motorcycle || Index == 0 || Index == 2);
}

float FProfile::PlayerWheelRadiusMeters(const int32 Index, const double MeshRadiusMeters) const
{
	if (Index < 0 || Index >= 4 || !FMath::IsFinite(MeshRadiusMeters) || MeshRadiusMeters <= 0) return 0.0f;
	const TOptional<float>& Selected = PlayerAxleTireRadiusMeters[Index / 2];
	return Selected.IsSet() ? Selected.GetValue() : static_cast<float>(MeshRadiusMeters * WheelScales[Index].Z);
}

FVector FProfile::PlayerWheelScale(const int32 Index, const double MeshRadiusMeters) const
{
	if (Index < 0 || Index >= 4) return FVector::OneVector;
	FVector Result = WheelScales[Index];
	if (PlayerAxleTireRadiusMeters[Index / 2].IsSet() && FMath::IsFinite(MeshRadiusMeters)
		&& MeshRadiusMeters > 0 && Result.Z > 0)
	{
		const double RadiusRatio = PlayerAxleTireRadiusMeters[Index / 2].GetValue() / (MeshRadiusMeters * Result.Z);
		Result.X *= RadiusRatio;
		Result.Z *= RadiusRatio;
	}
	return Result;
}

bool ResolveFromCatalog(const FCatalog& Catalog,
	SimCoreProtocol::ERuntimeVehicleClass VehicleClass, FProfile& OutProfile)
{
	OutProfile = {};
	if (VehicleClass == SimCoreProtocol::ERuntimeVehicleClass::Unspecified)
		VehicleClass = SimCoreProtocol::ERuntimeVehicleClass::Sedan;
	const int32 Index = static_cast<int32>(VehicleClass) - 1;
	if (Index < 0 || Index >= 4 || Catalog.Checksum.IsEmpty()) return false;
	OutProfile = Catalog.Profiles[Index];
	return true;
}

bool Resolve(const SimCoreProtocol::ERuntimeVehicleClass VehicleClass, FProfile& OutProfile)
{
	const auto& Snapshot = LiveCatalog();
	OutProfile = {};
	return Snapshot.bValid && ResolveFromCatalog(Snapshot.Catalog, VehicleClass, OutProfile);
}

bool ResolveFromCatalog(const FCatalog& Catalog, const SimCoreProtocol::ERuntimeVehicleClass VehicleClass,
	const FString& LoadoutId, FProfile& OutProfile)
{
	if (LoadoutId.IsEmpty()) return ResolveFromCatalog(Catalog, VehicleClass, OutProfile);
	OutProfile = {};
	if (Catalog.Checksum.IsEmpty()) return false;
	if (LoadoutId.StartsWith(TEXT("parts_v1_"), ESearchCase::CaseSensitive))
	{
		FPartsDraft Draft;
		FString CanonicalId, Error;
		return MakePartsDraftFromCatalog(Catalog, VehicleClass, LoadoutId, Draft, Error)
			&& ResolvePartsDraftFromCatalog(Catalog, VehicleClass, Draft, CanonicalId, OutProfile, Error)
			&& CanonicalId == LoadoutId;
	}
	for (const FLoadout& Loadout : Catalog.Loadouts)
		if (Loadout.Id == LoadoutId && Loadout.Profile.VehicleClass == VehicleClass)
		{
			OutProfile = Loadout.Profile;
			return true;
		}
	return false;
}

bool Resolve(const SimCoreProtocol::ERuntimeVehicleClass VehicleClass, const FString& LoadoutId, FProfile& OutProfile)
{
	const auto& Snapshot = LiveCatalog();
	OutProfile = {};
	return Snapshot.bValid && ResolveFromCatalog(Snapshot.Catalog, VehicleClass, LoadoutId, OutProfile);
}

TArray<FLoadout> LoadoutChoices(const SimCoreProtocol::ERuntimeVehicleClass VehicleClass)
{
	TArray<FLoadout> Result;
	const auto& Snapshot = LiveCatalog();
	FProfile Profile;
	if (!Snapshot.bValid || !ResolveFromCatalog(Snapshot.Catalog, VehicleClass, Profile)) return Result;
	Result.Add({FString(), TEXT("Vehicle default"), Profile});
	for (const FLoadout& Loadout : Snapshot.Catalog.Loadouts)
		if (Loadout.Profile.VehicleClass == Profile.VehicleClass) Result.Add(Loadout);
	return Result;
}

bool MakePartsDraft(const SimCoreProtocol::ERuntimeVehicleClass VehicleClass,
	const FString& LoadoutId, FPartsDraft& OutDraft, FString& OutError)
{
	const auto& Snapshot = LiveCatalog();
	OutDraft = {};
	OutError = Snapshot.Error;
	return Snapshot.bValid && MakePartsDraftFromCatalog(Snapshot.Catalog, VehicleClass, LoadoutId, OutDraft, OutError);
}

TArray<FPartChoice> PartChoices(const FPartsDraft& Draft, const int32 Slot)
{
	const auto& Snapshot = LiveCatalog();
	return Snapshot.bValid ? PartChoicesFromCatalog(Snapshot.Catalog, Draft, Slot) : TArray<FPartChoice>();
}

bool ResolvePartsDraft(const SimCoreProtocol::ERuntimeVehicleClass VehicleClass,
	const FPartsDraft& Draft, FString& OutId, FProfile& OutProfile, FString& OutError)
{
	const auto& Snapshot = LiveCatalog();
	OutId.Reset();
	OutProfile = {};
	OutError = Snapshot.Error;
	return Snapshot.bValid
		&& ResolvePartsDraftFromCatalog(Snapshot.Catalog, VehicleClass, Draft, OutId, OutProfile, OutError);
}

bool CatalogChecksum(FString& OutChecksum, FString& OutError)
{
	const auto& Snapshot = LiveCatalog();
	OutChecksum = Snapshot.bValid ? Snapshot.Catalog.Checksum : FString();
	OutError = Snapshot.Error;
	return Snapshot.bValid;
}

bool CatalogCapability(FString& OutCapability, FString& OutError)
{
	FString Checksum;
	OutCapability.Reset();
	if (!CatalogChecksum(Checksum, OutError)) return false;
	OutCapability = TEXT("vehicle-catalog-") + Checksum.Replace(TEXT(":"), TEXT("-"));
	return true;
}

bool ValidateServerCatalog(const TArray<FString>& Capabilities, FString& OutError)
{
	FString Expected;
	if (!CatalogCapability(Expected, OutError)) return false;
	int32 CatalogCount = 0;
	for (const FString& Capability : Capabilities)
	{
		if (!Capability.StartsWith(TEXT("vehicle-catalog-"))) continue;
		if (++CatalogCount > 1 || Capability != Expected)
		{
			OutError = TEXT("server vehicle catalog differs from this client; synchronize Config/VehicleCatalog and restart both programs");
			return false;
		}
	}
	// Old servers do not advertise a catalog. Updated servers require the matching
	// capability from this client before accepting a reset or controls.
	OutError.Reset();
	return true;
}

FVector CollisionCenterOffsetCm(const FProfile& Profile, const float CollisionHalfHeightMeters)
{
	return FVector(Profile.CollisionBodyForwardOffsetMeters * 100.0, 0.0,
		(Profile.CollisionGroundClearanceMeters + CollisionHalfHeightMeters
			- Profile.CgHeightMeters) * 100.0);
}
}
