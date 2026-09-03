#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SimCoreProtocol.h"
#include "SimCoreNpcPresentationActor.generated.h"

class USceneComponent;
class UStaticMeshComponent;

namespace SimCoreNpcPresentation
{
	/** v1 OBB bottom is 10 cm above the server's ground anchor. No UE ground query. */
	inline constexpr double ObbBottomAboveGroundCm = 10.0;
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
	uint32 GetEntityId() const { return EntityId; }
	UStaticMeshComponent* GetBody() const { return Body; }
	UStaticMeshComponent* GetWheel(int32 Index) const;
	USceneComponent* GetModelRoot() const { return ModelRoot; }
	float GetWheelSpinDegrees() const { return WheelSpinDegrees; }
	const FBox& GetAuthoredBounds() const { return AuthoredBounds; }

protected:
	virtual void PostInitializeComponents() override;

private:
	UPROPERTY(VisibleAnywhere, Category="SimCore|NPC Presentation")
	TObjectPtr<USceneComponent> NpcRoot;
	UPROPERTY(VisibleAnywhere, Category="SimCore|NPC Presentation")
	TObjectPtr<USceneComponent> ModelRoot;
	UPROPERTY(VisibleAnywhere, Category="SimCore|NPC Presentation")
	TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY(VisibleAnywhere, Category="SimCore|NPC Presentation")
	TArray<TObjectPtr<UStaticMeshComponent>> Wheels;
	FBox AuthoredBounds = FBox(ForceInit);
	bool bHasAuthoredSedan = false;
	uint32 EntityId = 0;
	float WheelSpinDegrees = 0.0f;
	float TireRadiusMeters = 0.32f;
};
