#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "SimCoreProtocol.h"
#include "SimCoreDriverPresentation.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPoseableMeshComponent;
class UProceduralMeshComponent;
class USkeletalMesh;
class UStaticMesh;
class UStaticMeshComponent;
class USimCoreDeformableBody;

namespace SimCoreDamagePresentation
{
	struct FZoneWeights;
}

namespace SimCoreDriverPresentation
{
	/** Bounded presentation target derived only from accepted authoritative damage. */
	struct FTarget
	{
		float InjuryAlpha = 0.0f;
		float LateralLeanDegrees = 0.0f;
		float ProtestAlpha = 0.0f;
	};

	/**
	 * Deterministic staging for the lightweight exit presentation. The driver
	 * never translates through a closed door: it opens first, remains open while
	 * the driver crosses the sill, then closes before the protest loop begins.
	 */
	struct FExitSequencePose
	{
		float DoorOpenAlpha = 0.0f;
		float DriverExitAlpha = 0.0f;
		float ProtestGestureAlpha = 0.0f;
	};

	DRIVEINTEGRATION_API FTarget BuildTarget(
		const SimCoreProtocol::FVehicleState& State);
	DRIVEINTEGRATION_API float AdvanceTowards(
		float Current, float Target, float DeltaSeconds, float ResponsePerSecond);
	DRIVEINTEGRATION_API FExitSequencePose EvaluateExitSequence(float SequenceAlpha);
}

/**
 * Shared Ego/NPC interior presentation. Manny is posed in the driver seat with
 * both hands on an authored steering wheel. Collision authority remains on the
 * host; this component only blends the visible driver toward the wheel.
 */
UCLASS(ClassGroup=(SimCore), meta=(BlueprintSpawnableComponent))
class DRIVEINTEGRATION_API USimCoreDriverPresentation : public USceneComponent
{
	GENERATED_BODY()

public:
	USimCoreDriverPresentation();

	void ApplyAuthoritativeState(const SimCoreProtocol::FVehicleState& State);
	/** Mirrors the body's existing bounded dent weights onto the separated door MIDs. */
	void ApplyDoorDamage(const SimCoreDamagePresentation::FZoneWeights& Weights);
	void SetPresentationEnabled(bool bEnabled);
	/** Advances only the bounded visual blend; exposed for deterministic QA. */
	void AdvancePresentation(float DeltaSeconds);

	float GetTargetInjuryAlpha() const { return TargetInjuryAlpha; }
	float GetCurrentInjuryAlpha() const { return CurrentInjuryAlpha; }
	float GetCurrentProtestAlpha() const { return CurrentProtestAlpha; }
	float GetCurrentDoorOpenAlpha() const { return CurrentDoorOpenAlpha; }
	float GetCurrentDriverExitAlpha() const { return CurrentDriverExitAlpha; }
	float GetCurrentGestureAlpha() const { return CurrentGestureAlpha; }
	int32 GetInteriorPanelCount() const { return InteriorPanels.Num(); }
	UPoseableMeshComponent* GetDriverMesh() const { return DriverMesh; }
	UProceduralMeshComponent* GetSteeringWheel() const { return SteeringWheel; }
	USceneComponent* GetDriverDoorPivot() const { return DriverDoorPivot; }
	UStaticMeshComponent* GetDriverDoorPanel() const { return DriverDoorPanel; }
	UStaticMeshComponent* GetDriverDoorAperture() const { return DriverDoorAperture; }
	int32 GetDriverDoorDamageMaterialCount() const { return DriverDoorDamageMaterials.Num(); }
	USimCoreDeformableBody* GetDriverDoorDeformableBody() const { return DriverDoorDeformableBody; }
	bool HasDriverAssets() const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimCore|Driver",
		meta=(ClampMin="0.1", ClampMax="20.0"))
	float PoseResponsePerSecond = 5.0f;

protected:
	virtual void OnRegister() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	void EnsureVisualComponents();
	void ApplyPose();
	void ConfigureInteriorMesh(UStaticMeshComponent* Mesh,
		bool bApplyInteriorMaterial = true) const;
	void BuildSteeringWheel();

	UPROPERTY()
	TObjectPtr<USkeletalMesh> DriverSkeletalMesh;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> InteriorBaseMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> DoorPaintMaterial;
	UPROPERTY()
	TObjectPtr<UStaticMesh> DriverDoorStaticMesh;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> InteriorMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DoorMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UPoseableMeshComponent> DriverMesh;
	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> SteeringWheel;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> SeatCushion;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> SeatBack;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Dashboard;
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> DriverDoorPivot;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> DriverDoorPanel;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> DriverDoorAperture;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> DriverDoorDamageMaterials;
	UPROPERTY(Transient)
	TObjectPtr<USimCoreDeformableBody> DriverDoorDeformableBody;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> InteriorPanels;

	FTransform DriverModelTransform = FTransform::Identity;
	float TargetInjuryAlpha = 0.0f;
	float TargetLateralLeanDegrees = 0.0f;
	float CurrentInjuryAlpha = 0.0f;
	float CurrentLateralLeanDegrees = 0.0f;
	float TargetProtestAlpha = 0.0f;
	float CurrentProtestAlpha = 0.0f;
	float CurrentDoorOpenAlpha = 0.0f;
	float CurrentDriverExitAlpha = 0.0f;
	float CurrentGestureAlpha = 0.0f;
	float ProtestAnimationTime = 0.0f;
	bool bPresentationEnabled = true;
};
