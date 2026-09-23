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
	FVector ExhaustLocationCm = FVector::ZeroVector;
	FTransform DriverTransform = FTransform::Identity;
	float HalfHeightMeters = 0.0f;
	float CgHeightMeters = 0.0f;
	float CollisionBodyForwardOffsetMeters = 0.0f;
	float CollisionGroundClearanceMeters = 0.0f;
	float NpcCollisionGroundClearanceMeters = 0.0f;
	// Both player and NPC loadouts use these axle overrides. Baseline arrays
	// retain authored sizing; null preserves it without new visual constants.
	TOptional<float> PlayerAxleTireRadiusMeters[2];
	TArray<FString> ModuleDetails;

	bool IsWheelVisible(int32 Index) const;
	float PlayerWheelRadiusMeters(int32 Index, double MeshRadiusMeters) const;
	FVector PlayerWheelScale(int32 Index, double MeshRadiusMeters) const;
};

struct FLoadout
{
	FString Id;
	FString Name;
	FProfile Profile;
};

struct FCatalogSource;

struct FCatalog
{
	FProfile Profiles[4];
	FString VehicleIds[4];
	FString Checksum;
	TArray<FLoadout> Loadouts;
	// Immutable validated definitions used by custom selections. Not serialized.
	TSharedPtr<const FCatalogSource> Source;
};

inline constexpr int32 PartsSlotCount = 8;
struct FPartsDraft
{
	FString BaseLoadoutId;
	// Front tire/suspension, rear tire/suspension, engine, transmission,
	// drivetrain, tank. Empty means the scalar baseline on axle slots only.
	TArray<FString> PartIds;
};

struct FPartChoice
{
	FString Id;
	FString Name;
	FString Summary;
};

// Atomic, bounded loading for tools/tests. The live catalog is captured once per
// process; restart both programs after editing a shared vehicle definition.
bool LoadCatalog(const FString& ManifestPath, FCatalog& OutCatalog, FString& OutError);
bool CatalogChecksum(FString& OutChecksum, FString& OutError);
bool CatalogCapability(FString& OutCapability, FString& OutError);
bool ValidateServerCatalog(const TArray<FString>& Capabilities, FString& OutError);
bool ResolveFromCatalog(const FCatalog& Catalog,
	SimCoreProtocol::ERuntimeVehicleClass VehicleClass, FProfile& OutProfile);

bool Resolve(SimCoreProtocol::ERuntimeVehicleClass VehicleClass, FProfile& OutProfile);
bool ResolveFromCatalog(const FCatalog& Catalog, SimCoreProtocol::ERuntimeVehicleClass VehicleClass,
	const FString& LoadoutId, FProfile& OutProfile);
bool Resolve(SimCoreProtocol::ERuntimeVehicleClass VehicleClass, const FString& LoadoutId, FProfile& OutProfile);
TArray<FLoadout> LoadoutChoices(SimCoreProtocol::ERuntimeVehicleClass VehicleClass);
FString PartSlotName(int32 Slot);
bool MakePartsDraft(SimCoreProtocol::ERuntimeVehicleClass VehicleClass,
	const FString& LoadoutId, FPartsDraft& OutDraft, FString& OutError);
bool MakePartsDraftFromCatalog(const FCatalog& Catalog, SimCoreProtocol::ERuntimeVehicleClass VehicleClass,
	const FString& LoadoutId, FPartsDraft& OutDraft, FString& OutError);
TArray<FPartChoice> PartChoices(const FPartsDraft& Draft, int32 Slot);
TArray<FPartChoice> PartChoicesFromCatalog(const FCatalog& Catalog, const FPartsDraft& Draft, int32 Slot);
bool ResolvePartsDraft(SimCoreProtocol::ERuntimeVehicleClass VehicleClass,
	const FPartsDraft& Draft, FString& OutId, FProfile& OutProfile, FString& OutError);
bool ResolvePartsDraftFromCatalog(const FCatalog& Catalog, SimCoreProtocol::ERuntimeVehicleClass VehicleClass,
	const FPartsDraft& Draft, FString& OutId, FProfile& OutProfile, FString& OutError);
FVector CollisionCenterOffsetCm(const FProfile& Profile, float CollisionHalfHeightMeters);
}
