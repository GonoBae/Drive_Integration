#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SimCoreProtocol.h"
#include "SimCorePedestrianPresentationActor.generated.h"

class UAnimSequence;
class USceneComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class UWorld;

namespace SimCorePedestrianPresentation
{
	/** Fit the authored standing character to the server capsule; feet stay at its bottom. */
	DRIVEINTEGRATION_API bool BuildModelTransform(
		const FBox& Bounds, float CapsuleHalfHeightMeters, FTransform& OutTransform);
	/** Initial limb velocity in UE cm/s; pelvis translation belongs to the server. */
	DRIVEINTEGRATION_API FVector BuildImpactLaunchVelocity(
		const SimCoreProtocol::FVehicleState& State, const FVector& FallbackForward);
	DRIVEINTEGRATION_API FVector BuildImpactAngularVelocity(const SimCoreProtocol::FVehicleState& State);
	/** Gravity-limited visual correction for a settled ragdoll above a query-only road. */
	DRIVEINTEGRATION_API float BuildSettledGroundAnchorStep(
		float GroundZCm, float LowestBodyZCm, float DeltaSeconds,
		float& InOutDownwardSpeedCmPerSecond);
	/** Query the upward-facing WorldStatic support directly below one ragdoll body. */
	DRIVEINTEGRATION_API bool QueryLocalGroundSupport(
		UWorld* World, const AActor* IgnoredActor, const FBox& BodyBounds,
		float ExpectedGroundZCm, FVector& OutSurfacePoint, FVector& OutSurfaceNormal);
	/** Bounded penetration recovery along the measured local surface normal. */
	DRIVEINTEGRATION_API FVector BuildLocalGroundDepenetrationStep(
		float SurfaceZCm, float LowestBodyZCm, const FVector& SurfaceNormal,
		float DeltaSeconds);
	/** Remove only velocity directed into a measured local support plane. */
	DRIVEINTEGRATION_API FVector RemoveVelocityIntoGround(
		const FVector& VelocityCmPerSecond, const FVector& SurfaceNormal);
	inline constexpr float WalkReferenceSpeedMps = 1.4f;
	inline constexpr float StandingHalfHeightMeters = 0.9f;
	inline constexpr float RagdollRecoverySettleSeconds = 0.25f;
	inline constexpr float RagdollRecoveryBlendSeconds = 1.35f;
	inline constexpr float RecoveryWalkDelaySeconds = 0.55f;
	inline constexpr float GroundContactToleranceCm = 1.5f;
	inline constexpr float MinimumGroundNormalZ = 0.25f;
}

/** Server-driven +X-forward humanoid. Skeletal impact physics is visual only, never sent back. */
UCLASS(NotPlaceable, Transient)
class DRIVEINTEGRATION_API ASimCorePedestrianPresentationActor : public AActor
{
	GENERATED_BODY()

public:
	ASimCorePedestrianPresentationActor();
	bool ApplySnapshot(const SimCoreProtocol::FVehicleState& State, float SnapshotAgeSeconds,
		float DeltaSeconds, bool bMotionAllowed, float MaxExtrapolationSeconds,
		const FVector& PresentationOffsetCm);
	bool HasHumanoidAssets() const;
	bool HasRagdollPhysicsAsset() const;
	USkeletalMeshComponent* GetCharacterMesh() const { return CharacterMesh; }
	bool IsWalking() const { return bWalking; }
	float GetAnimationPlayRate() const { return AnimationPlayRate; }
	uint32 GetEntityId() const { return EntityId; }
	bool IsRagdollActive() const { return bRagdollActive; }
	bool IsRecoveringRagdoll() const { return bRecoveringRagdoll; }
	uint32 GetRagdollLaunchCount() const { return RagdollLaunchCount; }
	float GetRagdollGroundAnchorOffsetCm() const { return RagdollGroundAnchorOffsetCm; }
	float GetRecoveryPoseAlpha() const { return CurrentRecoveryPoseAlpha; }
	float GetRecoveryWalkDelaySeconds() const { return RecoveryWalkDelayRemainingSeconds; }

protected:
	virtual void PostInitializeComponents() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	bool StartRagdoll(const SimCoreProtocol::FVehicleState& State, bool bLaunch);
	void UpdateRagdoll(const SimCoreProtocol::FVehicleState& State, float DeltaSeconds,
		bool bMotionAllowed);
	void StopRagdoll(bool bBeginLocomotionDelay = false);
	UPROPERTY(VisibleAnywhere, Category="SimCore|Pedestrian Presentation")
	TObjectPtr<USceneComponent> PedestrianRoot;
	UPROPERTY(VisibleAnywhere, Category="SimCore|Pedestrian Presentation")
	TObjectPtr<USkeletalMeshComponent> CharacterMesh;
	UPROPERTY()
	TObjectPtr<USkeletalMesh> MannyMesh;
	UPROPERTY()
	TObjectPtr<USkeletalMesh> QuinnMesh;
	UPROPERTY()
	TObjectPtr<UAnimSequence> IdleAnimation;
	UPROPERTY()
	TObjectPtr<UAnimSequence> WalkAnimation;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> CurrentAnimation;
	uint32 EntityId = 0;
	uint32 LastCollisionEventSequence = 0;
	uint32 RagdollLaunchCount = 0;
	FString PresentedPlaySessionId;
	FString PresentedMapChecksum;
	FTransform StandingModelTransform;
	FTransform RagdollModelTransform;
	FTransform RecoveryStartModelTransform;
	bool bReceivedSnapshot = false;
	bool bLastAuthoritativeDowned = false;
	bool bRagdollActive = false;
	bool bNeedsDownedReconstruction = false;
	bool bRagdollPaused = false;
	bool bRecoveringRagdoll = false;
	float RagdollElapsedSeconds = 0.0f;
	float RagdollRecoverySeconds = 0.0f;
	float CurrentRecoveryPoseAlpha = 0.0f;
	float RecoveryWalkDelayRemainingSeconds = 0.0f;
	float RagdollGroundAnchorOffsetCm = 0.0f;
	float GroundAnchorDownwardSpeedCmPerSecond = 0.0f;
	bool bWalking = false;
	float AnimationPlayRate = 0.0f;
};
