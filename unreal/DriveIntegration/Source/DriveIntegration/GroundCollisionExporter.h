#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GroundCollisionExporter.generated.h"

class UBoxComponent;
class USceneComponent;

/**
 * Editor-only authoring bridge that samples Unreal WorldStatic collision and
 * bakes the upper ground surface and explicit SimCore static-collider markers
 * into a SimCore MapPackage.
 *
 * The runtime vehicle remains server-authoritative. This actor deliberately
 * performs an offline bake instead of feeding delayed render-world traces back
 * into the 60 Hz physics loop.
 */
UCLASS(NotBlueprintable, meta=(DisplayName="SimCore Ground Collision Exporter"))
class DRIVEINTEGRATION_API AGroundCollisionExporter : public AActor
{
	GENERATED_BODY()

public:
	AGroundCollisionExporter();
	virtual void OnConstruction(const FTransform& Transform) override;

	/** Replace both collision CSV payloads, then commit manifest.cfg last. */
	UFUNCTION(CallInEditor, Category="SimCore|Ground Export", meta=(DisplayName="Bake Ground + Static Collision To MapPackage"))
	void ExportGroundSurface();

	/** Move the sampling box XY center to the SimCore ENU spawn origin. */
	UFUNCTION(CallInEditor, Category="SimCore|Ground Export", meta=(DisplayName="Center Sampling On Map Origin"))
	void CenterSamplingOnMapOrigin();

	/**
	 * Align the sampling box yaw with Ground Actor and expand it to contain the
	 * actor's complete component bounds in world space.
	 */
	UFUNCTION(CallInEditor, Category="SimCore|Ground Export", meta=(DisplayName="Fit Sampling Bounds To Ground Actor"))
	void FitSamplingBoundsToGroundActor();

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	UPROPERTY(VisibleAnywhere, Category="SimCore|Ground Export")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Visible editor-only preview of the collision sampling volume. */
	UPROPERTY(VisibleAnywhere, Category="SimCore|Ground Export")
	TObjectPtr<UBoxComponent> SamplingBounds;

	/**
	 * Optional actor filter. Assign the Landscape here when another floor or
	 * overhead WorldStatic collider overlaps the sampling volume.
	 */
	UPROPERTY(EditInstanceOnly, Category="SimCore|Ground Export")
	TObjectPtr<AActor> GroundActor;

	/** Half size of the sampled corridor in Unreal centimeters. */
	UPROPERTY(EditAnywhere, Category="SimCore|Ground Export", meta=(ClampMin="100.0", UIMin="100.0"))
	FVector2D HorizontalExtentCm = FVector2D(3000.0, 1500.0);

	/** Extra clearance added on every side by Fit Sampling Bounds To Ground Actor. */
	UPROPERTY(EditAnywhere, Category="SimCore|Ground Export", meta=(ClampMin="0.0", UIMin="0.0"))
	float GroundBoundsPaddingCm = 100.0f;

	/** Maximum spacing between collision samples in Unreal centimeters. */
	UPROPERTY(EditAnywhere, Category="SimCore|Ground Export", meta=(ClampMin="25.0", UIMin="25.0"))
	float SampleSpacingCm = 100.0f;

	/** Vertical trace distance above the exporter actor. */
	UPROPERTY(EditAnywhere, Category="SimCore|Ground Export", meta=(ClampMin="100.0", UIMin="100.0"))
	float TraceAboveCm = 5000.0f;

	/** Vertical trace distance below the exporter actor. */
	UPROPERTY(EditAnywhere, Category="SimCore|Ground Export", meta=(ClampMin="100.0", UIMin="100.0"))
	float TraceBelowCm = 5000.0f;

	/**
	 * When Ground Actor is not assigned, cells with a larger corner-to-corner
	 * height jump are omitted instead of bridging a wall, hole, or unrelated
	 * collider as drivable ground. A filtered Ground Actor is treated as one
	 * continuous authored surface, so steep Landscape cells are retained.
	 */
	UPROPERTY(EditAnywhere, Category="SimCore|Ground Export", meta=(ClampMin="1.0", UIMin="1.0"))
	float MaxCellHeightDeltaCm = 100.0f;

	/** Reject near-vertical hits. One means horizontal upward-facing ground. */
	UPROPERTY(EditAnywhere, Category="SimCore|Ground Export", meta=(ClampMin="0.01", ClampMax="1.0"))
	float MinimumGroundNormalZ = 0.1f;

	/**
	 * Package directory relative to the Unreal project directory. Export is
	 * restricted to this repository's map_packages directory.
	 */
	UPROPERTY(EditAnywhere, Category="SimCore|Ground Export")
	FString MapPackageDirectory = TEXT("../../map_packages/landscape_local_v1");

	/** Surface label written into each exported triangle. */
	UPROPERTY(EditAnywhere, Category="SimCore|Ground Export")
	FName SurfaceId = TEXT("landscape_ground");

	/** Unreal world position that corresponds to SimCore ENU (0, 0, 0). */
	UPROPERTY(EditAnywhere, Category="SimCore|Ground Export")
	FVector MapOriginWorldCm = FVector::ZeroVector;

private:
	void UpdateBoundsVisualization();
};
