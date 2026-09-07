#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SimCoreProtocol.h"
#include "SimCoreDamagePresentation.h"
#include "SimCoreNpcPresentationActor.generated.h"

class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class USimCoreDriverPresentation;
class USimCoreVehicleHornComponent;

namespace SimCoreNpcPresentation
{
	/** v1 OBB bottom is 10 cm above the server's ground anchor. No UE ground query. */
	inline constexpr double ObbBottomAboveGroundCm = 10.0;
	// The server's tilted collision envelope changes size, the authored car does not.
	inline constexpr float AuthoredBodyHalfHeightMeters = 0.75f;
	DRIVEINTEGRATION_API bool BuildAuthoredModelOffset(
		const FBox& AuthoredBounds, float ObbHalfHeightMeters, FVector& OutOffsetCm);
}

/** Server-driven visual only: no controller, collision, terrain solve or local route. */
UCLASS(NotPlaceable, Transient)
class DRIVEINTEGRATION_API ASimCoreNpcPresentationActor : public AActor
{
	GENERATED_BODY()

public:
	ASimCoreNpcPresentationActor();
	bool ApplySnapshot(const SimCoreProtocol::FVehicleState& State, float SnapshotAgeSeconds,
		float DeltaSeconds, bool bMotionAllowed, float MaxExtrapolationSeconds,
		const FVector& PresentationOffsetCm);
	bool HasAuthoredSedan() const { return bHasAuthoredSedan; }
	bool HasAuthoredFleet() const { return bHasAuthoredFleet; }
	SimCoreProtocol::ERuntimeVehicleClass GetRuntimeVehicleClass() const { return RuntimeVehicleClass; }
	int32 GetVisibleWheelCount() const;
	uint32 GetEntityId() const { return EntityId; }
	UStaticMeshComponent* GetBody() const { return Body; }
	class USimCoreDeformableBody* GetDeformableBody() const { return DeformableBody; }
	USimCoreDriverPresentation* GetDriverPresentation() const { return DriverPresentation; }
	UStaticMeshComponent* GetWheel(int32 Index) const;
	USceneComponent* GetModelRoot() const { return ModelRoot; }
	float GetWheelSpinDegrees() const { return WheelSpinDegrees; }
	const FBox& GetAuthoredBounds() const { return AuthoredBounds; }

protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<class USimCoreDeformableBody> DeformableBody;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<class USimCoreTurnSignals> TurnSignals;
	UPROPERTY(VisibleAnywhere, Category="SimCore|NPC Presentation")
	TObjectPtr<USimCoreVehicleHornComponent> VehicleHorn;
	UPROPERTY(VisibleAnywhere, Category="SimCore|NPC Presentation")
	TObjectPtr<USimCoreDriverPresentation> DriverPresentation;
	void UpdateDamage(const SimCoreProtocol::FVehicleState& State);
	bool ConfigureVehicleClass(SimCoreProtocol::ERuntimeVehicleClass VehicleClass);
	SimCoreDamagePresentation::FAccumulator DamageAccumulator;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> DamageMaterials;
	UPROPERTY(VisibleAnywhere, Category="SimCore|NPC Presentation")
	TObjectPtr<USceneComponent> NpcRoot;
	UPROPERTY(VisibleAnywhere, Category="SimCore|NPC Presentation")
	TObjectPtr<USceneComponent> ModelRoot;
	UPROPERTY(VisibleAnywhere, Category="SimCore|NPC Presentation")
	TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY(VisibleAnywhere, Category="SimCore|NPC Presentation")
	TArray<TObjectPtr<UStaticMeshComponent>> Wheels;
	UPROPERTY()
	TObjectPtr<UStaticMesh> SedanBodyMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> CompactBodyMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> TruckBodyMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> MotorcycleBodyMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> SharedWheelMesh;
	FBox AuthoredBounds = FBox(ForceInit);
	bool bHasAuthoredSedan = false;
	bool bHasAuthoredFleet = false;
	SimCoreProtocol::ERuntimeVehicleClass RuntimeVehicleClass =
		SimCoreProtocol::ERuntimeVehicleClass::Unspecified;
	uint32 EntityId = 0;
	float WheelSpinDegrees = 0.0f;
	float TireRadiusMeters = 0.32f;
	// Stable authored ride height for the selected model. The server collision
	// envelope can change its projected half-height while a vehicle tumbles; that
	// must not translate or stretch the visual model between snapshots.
	float PresentationHalfHeightMeters =
		SimCoreNpcPresentation::AuthoredBodyHalfHeightMeters;
	FString HornPlaySessionId;
	FString HornMapPackageChecksum;
	bool bHornSequenceInitialized = false;
};
