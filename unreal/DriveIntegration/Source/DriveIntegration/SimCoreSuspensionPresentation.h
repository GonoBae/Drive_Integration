#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "SimCoreVehicleVisualProfile.h"
#include "SimCoreSuspensionPresentation.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;

namespace SimCoreSuspensionPresentation
{
	DRIVEINTEGRATION_API bool LinkTransform(const FVector& Start, const FVector& End,
		float RadiusCm, FTransform& OutTransform);
}

/** Chassis rails, control arms and dampers follow the existing wheel hubs.
 * These meshes illustrate the server suspension; they add no collision or forces. */
UCLASS()
class DRIVEINTEGRATION_API USimCoreSuspensionPresentation : public USceneComponent
{
	GENERATED_BODY()
public:
	USimCoreSuspensionPresentation();
	void Configure(SimCoreProtocol::ERuntimeVehicleClass VehicleClass);
	void SetWheelHub(int32 Index, USceneComponent* Hub);
	void UpdateLinks();
	int32 GetVisibleLinkCount() const;
private:
	void EnsureParts();
	void PlaceLink(int32 Index, const FVector& Start, const FVector& End, float RadiusCm);
	UPROPERTY()
	TObjectPtr<UStaticMesh> Cylinder;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseMaterial;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Links;
	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> WheelHubs;
	SimCoreVehicleVisualProfile::FProfile Profile;
};
