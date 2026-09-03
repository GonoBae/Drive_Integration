#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GroundCollisionExporter.generated.h"

class UBoxComponent;
class USceneComponent;

/**
 * Editor-only authoring bridge that samples Unreal WorldStatic collision and
 * measures the upper ground surface into a high-resolution binary heightfield
 * snapshot and bakes explicit SimCore static-collider markers into a
 * SimCore MapPackage.
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

	/** Replace heightfield, sentinel, and static collision payloads, then commit manifest.cfg last. */
	UFUNCTION(CallInEditor, Category="SimCore|Ground Export", meta=(DisplayName="Bake Ground + Static Collision To MapPackage"))
	void ExportGroundSurface();

	/** Configure a generated authoring scene without reflection or editor UI. */
	void ConfigureForAuthoring(AActor* InGroundActor,
		const FString& InMapPackageDirectory, const FVector& InMapOriginWorldCm,
		const FVector& InSamplingCenterWorldCm, const FVector2D& InHorizontalExtentCm,
		float InSampleSpacingCm = 100.0f, float InGroundBoundsPaddingCm = 0.0f);

	/** Same production transaction as the editor button; reports failure to automation. */
	bool ExportGroundSurfaceUnattended();

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

	/**
	 * Spacing of the Unreal collision measurements stored in the heightfield
	 * snapshot. This is intentionally a new property: actors baked by the old
	 * triangle exporter may have persisted its automatically degraded 507 cm
	 * spacing, while the heightfield format can retain the 100 cm default.
	 */
	UPROPERTY(EditAnywhere, Category="SimCore|Ground Export", meta=(ClampMin="25.0", UIMin="25.0"))
	float HeightfieldSampleSpacingCm = 100.0f;

	/**
	 * Explicit editor-work bound for one bake. The binary format has a hard
	 * two-million-sample ceiling; lower this value to fail earlier on a large
	 * authoring region instead of silently reducing terrain resolution.
	 */
	UPROPERTY(EditAnywhere, Category="SimCore|Ground Export", meta=(ClampMin="4", ClampMax="2000000", UIMin="4", UIMax="2000000"))
	int32 MaxHeightfieldSampleCount = 2000000;

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

	/** Physical Surface assignments used when no SimCore.Surface.* tag overrides the hit. */
	UPROPERTY(EditAnywhere, Category="SimCore|Ground Export|Materials")
	TEnumAsByte<EPhysicalSurface> AsphaltPhysicalSurface = SurfaceType1;

	UPROPERTY(EditAnywhere, Category="SimCore|Ground Export|Materials")
	TEnumAsByte<EPhysicalSurface> LowFrictionPhysicalSurface = SurfaceType2;

	UPROPERTY(EditAnywhere, Category="SimCore|Ground Export|Materials")
	TEnumAsByte<EPhysicalSurface> RoughPhysicalSurface = SurfaceType3;

	/** Terrain properties baked into SIMGHF2; server vehicle profile scales remain separate. */
	UPROPERTY(EditAnywhere, Category="SimCore|Ground Export|Materials", meta=(ClampMin="0.05", ClampMax="4.0"))
	float DefaultFrictionMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, Category="SimCore|Ground Export|Materials", meta=(ClampMin="0.05", ClampMax="4.0"))
	float AsphaltFrictionMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, Category="SimCore|Ground Export|Materials", meta=(ClampMin="0.05", ClampMax="4.0"))
	float LowFrictionFrictionMultiplier = 0.35f;

	UPROPERTY(EditAnywhere, Category="SimCore|Ground Export|Materials", meta=(ClampMin="0.05", ClampMax="4.0"))
	float RoughFrictionMultiplier = 1.10f;

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
	bool bLastExportSucceeded = false;
	bool bSuppressExportDialogs = false;
	void UpdateBoundsVisualization();

#if WITH_EDITOR
	/** Apply the complete colliding-component fit without forcing a modal dialog. */
	bool FitSamplingBoundsToGroundActorInternal(
		bool bShowResultDialog,
		FString& OutResult);
#endif
};
