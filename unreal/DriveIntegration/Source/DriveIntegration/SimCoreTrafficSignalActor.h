#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SimCoreProtocol.h"
#include "SimCoreTrafficSignalActor.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class USceneComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

namespace SimCoreTrafficSignals
{
	inline constexpr double SnapshotFreshnessSeconds = 0.1;

	struct FDisplayState
	{
		SimCoreProtocol::ETrafficSignalAspect Aspect = SimCoreProtocol::ETrafficSignalAspect::Unknown;
		SimCoreProtocol::ETrafficSignalKind Kind = SimCoreProtocol::ETrafficSignalKind::Vehicle;
		bool bVerified = false;
		bool bOutOfService = false;
		float RemainingSeconds = 0.0f;
		// UNKNOWN is physically all-red, never a dark/inferred green indication.
		bool bRed = true;
		bool bYellow = false;
		bool bGreen = false;
		bool IsPedestrian() const { return Kind == SimCoreProtocol::ETrafficSignalKind::Pedestrian; }
	};

	/** No phase clock: local time may only revoke permission, never advance a light. */
	DRIVEINTEGRATION_API FDisplayState EvaluateDisplay(
		const SimCoreProtocol::FTrafficSignalState& Signal,
		bool bNetworkReady,
		bool bAcceptedSnapshot,
		double SnapshotAgeSeconds);
	DRIVEINTEGRATION_API FTransform BuildPoleTransform(
		const SimCoreProtocol::FTrafficSignalState& Signal,
		const FVector& PresentationOffsetCm = FVector::ZeroVector);
	DRIVEINTEGRATION_API FTransform BuildDamagedPoleTransform(
		const SimCoreProtocol::FStructureState& Structure,
		const FVector& PresentationOffsetCm = FVector::ZeroVector);
}

/** Ephemeral game-world presentation, never collision or map authoring geometry. */
UCLASS(NotPlaceable, Transient)
class DRIVEINTEGRATION_API ASimCoreTrafficSignalActor : public AActor
{
	GENERATED_BODY()

public:
	ASimCoreTrafficSignalActor();

	void ApplyAuthoritativeSignal(const SimCoreProtocol::FTrafficSignalState& Signal,
		bool bNetworkReady, bool bAcceptedSnapshot, double SnapshotAgeSeconds,
		const FVector& PresentationOffsetCm = FVector::ZeroVector);
	void SetFailSafe();
	void ApplyStructureDamage(const SimCoreProtocol::FStructureState& Structure,
		const FVector& PresentationOffsetCm = FVector::ZeroVector);
	void ClearStructureDamage();
	const SimCoreTrafficSignals::FDisplayState& GetDisplayState() const { return Display; }
	uint32 GetSignalId() const { return SignalId; }
	UStaticMeshComponent* GetLamp(int32 Index) const;
	FVector GetLensFacingDirection() const { return -GetActorForwardVector(); }
	FString GetStatusText() const;

protected:
	virtual void PostInitializeComponents() override;

private:
	void ApplyDisplay();
	void InitializeMaterials();

	UPROPERTY(VisibleAnywhere, Category="SimCore|Traffic Signals")
	TObjectPtr<USceneComponent> SignalRoot;
	UPROPERTY(VisibleAnywhere, Category="SimCore|Traffic Signals")
	TObjectPtr<UStaticMeshComponent> Pole;
	UPROPERTY(VisibleAnywhere, Category="SimCore|Traffic Signals")
	TObjectPtr<UStaticMeshComponent> Head;
	UPROPERTY(VisibleAnywhere, Category="SimCore|Traffic Signals")
	TArray<TObjectPtr<UStaticMeshComponent>> Lamps;
	UPROPERTY(VisibleAnywhere, Category="SimCore|Traffic Signals")
	TObjectPtr<UTextRenderComponent> StatusLabel;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> ColorMaterial;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> LampMaterials;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HousingMaterial;

	SimCoreTrafficSignals::FDisplayState Display;
	uint32 SignalId = 0;
	uint32 GroupId = 0;
	bool bHasStructureDamage = false;
	bool bBroken = false;
};
