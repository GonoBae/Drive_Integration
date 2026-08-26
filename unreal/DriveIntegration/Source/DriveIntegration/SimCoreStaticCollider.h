#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SimCoreStaticCollider.generated.h"

class UBoxComponent;

/** Semantic consumed by the strict SimCore static_colliders.csv loader. */
UENUM(BlueprintType)
enum class ESimCoreStaticColliderSemantic : uint8
{
	Wall UMETA(DisplayName = "Wall"),
	Curb UMETA(DisplayName = "Curb"),
	Barrier UMETA(DisplayName = "Barrier"),
};

/**
 * Editor-only authoring marker for one authoritative SimCore OBB prism.
 *
 * The box is deliberately not an Unreal runtime collision body. SimCore owns
 * Ego collision response; this actor gives the offline exporter an explicit,
 * deterministic shape, ID, semantic, and material to bake into MapPackage.
 */
UCLASS(NotBlueprintable, meta=(DisplayName="SimCore Static Collider"))
class DRIVEINTEGRATION_API ASimCoreStaticCollider : public AActor
{
	GENERATED_BODY()

public:
	ASimCoreStaticCollider();
	virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Preview and authored dimensions of the exported OBB prism. */
	UPROPERTY(VisibleAnywhere, Category="SimCore|Static Collision")
	TObjectPtr<UBoxComponent> CollisionBounds;

	/** Stable ASCII ID: letters, digits, underscore, hyphen, and dot only. */
	UPROPERTY(EditInstanceOnly, Category="SimCore|Static Collision")
	FString ColliderId;

	UPROPERTY(EditAnywhere, Category="SimCore|Static Collision")
	ESimCoreStaticColliderSemantic Semantic = ESimCoreStaticColliderSemantic::Barrier;

	UPROPERTY(EditAnywhere, Category="SimCore|Static Collision", meta=(ClampMin="0.0", UIMin="0.0"))
	float Friction = 0.8f;

	UPROPERTY(EditAnywhere, Category="SimCore|Static Collision", meta=(ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="1.0"))
	float Restitution = 0.0f;

	/** Disabled markers remain in the level but are omitted from the package. */
	UPROPERTY(EditAnywhere, Category="SimCore|Static Collision")
	bool bExportEnabled = true;

private:
	void UpdatePreview();
};
