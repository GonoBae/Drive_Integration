#include "SimCorePedestrianPresentationActor.h"

#include "Animation/AnimSequence.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "SimCoreCoordinateFrames.h"
#include "SimCorePresentation.h"
#include "SimCoreTrafficSignalActor.h"
#include "UObject/ConstructorHelpers.h"

bool SimCorePedestrianPresentation::BuildModelTransform(
	const FBox& Bounds, float CapsuleHalfHeightMeters, FTransform& OutTransform)
{
	OutTransform = FTransform::Identity;
	if (!Bounds.IsValid || Bounds.Min.ContainsNaN() || Bounds.Max.ContainsNaN()
		|| !FMath::IsFinite(CapsuleHalfHeightMeters) || CapsuleHalfHeightMeters <= 0.0f
		|| Bounds.GetSize().GetMin() <= KINDA_SMALL_NUMBER) return false;
	const double Scale = CapsuleHalfHeightMeters * 200.0 / Bounds.GetSize().Z;
	// Template characters face +Y, whereas the authoritative actor's forward is +X.
	const FQuat Facing = FRotator(0.0, -90.0, 0.0).Quaternion();
	const FVector Centre = Bounds.GetCenter();
	FVector Offset = Facing.RotateVector(FVector(-Centre.X * Scale, -Centre.Y * Scale, 0.0));
	Offset.Z = -CapsuleHalfHeightMeters * 100.0 - Bounds.Min.Z * Scale;
	OutTransform = FTransform(Facing, Offset, FVector(Scale));
	return !OutTransform.ContainsNaN();
}

FVector SimCorePedestrianPresentation::BuildImpactLaunchVelocity(
	const SimCoreProtocol::FVehicleState& State, const FVector& FallbackForward)
{
	if (!State.bPedestrianDowned || !FMath::IsFinite(State.LastImpactImpulseNs) || State.LastImpactImpulseNs <= 0.0f
		|| State.ImpactDirectionEnu.ContainsNaN()) return FVector::ZeroVector;
	(void)FallbackForward;
	const FVector ServerVelocity(SimCoreCoordinateFrames::MapEnuPolarVectorToUnrealWorld(State.LinearVelocityEnu) * 100.0);
	if (ServerVelocity.ContainsNaN()) return FVector::ZeroVector;
	// A horizontal bumper impulse must not invent a second upward/forward kick.
	// The authoritative trajectory already contains all centre-of-mass momentum.
	return ServerVelocity.GetClampedToMaxSize(6000.0);
}

FVector SimCorePedestrianPresentation::BuildImpactAngularVelocity(const SimCoreProtocol::FVehicleState& State)
{
	if (!State.bPedestrianDowned || State.AngularVelocityBody.ContainsNaN()) return FVector::ZeroVector;
	const FQuat Rotation(SimCoreCoordinateFrames::CanonicalAttitudeToUnrealActorQuaternion(
		{State.HeadingDegrees, State.PitchDegrees, State.RollDegrees}));
	const FVector Local(SimCoreCoordinateFrames::BodyFluAxialAngularVelocityToUnrealActor(State.AngularVelocityBody));
	return Rotation.RotateVector(Local).GetClampedToMaxSize(7.0);
}

float SimCorePedestrianPresentation::BuildSettledGroundAnchorStep(
	float GroundZCm, float LowestBodyZCm, float DeltaSeconds,
	float& InOutDownwardSpeedCmPerSecond)
{
	if (!FMath::IsFinite(GroundZCm) || !FMath::IsFinite(LowestBodyZCm)
		|| !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0f
		|| !FMath::IsFinite(InOutDownwardSpeedCmPerSecond)
		|| InOutDownwardSpeedCmPerSecond < 0.0f)
	{
		InOutDownwardSpeedCmPerSecond = 0.0f;
		return 0.0f;
	}
	const float GapCm = LowestBodyZCm - GroundZCm;
	if (GapCm <= GroundContactToleranceCm)
	{
		InOutDownwardSpeedCmPerSecond = 0.0f;
		return 0.0f;
	}
	// Do not snap a suspended skeleton down. Accelerate its visual anchor using
	// the same earth gravity as Chaos, with a bounded terminal correction speed.
	InOutDownwardSpeedCmPerSecond = FMath::Min(
		InOutDownwardSpeedCmPerSecond + 980.0f * DeltaSeconds, 350.0f);
	return -FMath::Min(GapCm, InOutDownwardSpeedCmPerSecond * DeltaSeconds);
}

bool SimCorePedestrianPresentation::QueryLocalGroundSupport(
	UWorld* World, const AActor* IgnoredActor, const FBox& BodyBounds,
	float ExpectedGroundZCm, FVector& OutSurfacePoint, FVector& OutSurfaceNormal)
{
	OutSurfacePoint = FVector::ZeroVector;
	OutSurfaceNormal = FVector::UpVector;
	if (!World || !BodyBounds.IsValid || BodyBounds.Min.ContainsNaN()
		|| BodyBounds.Max.ContainsNaN() || !FMath::IsFinite(ExpectedGroundZCm))
	{
		return false;
	}
	const FVector Centre = BodyBounds.GetCenter();
	const double TraceStartZ = FMath::Max(
		static_cast<double>(BodyBounds.Max.Z + 10.0),
		static_cast<double>(ExpectedGroundZCm + 100.0f));
	const double TraceEndZ = FMath::Min(
		static_cast<double>(BodyBounds.Min.Z - 150.0),
		static_cast<double>(ExpectedGroundZCm - 150.0f));
	FCollisionObjectQueryParams ObjectQuery;
	ObjectQuery.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(SimCorePedestrianLocalGround), false);
	if (IgnoredActor) Query.AddIgnoredActor(IgnoredActor);
	TArray<FHitResult> Hits;
	if (!World->LineTraceMultiByObjectType(
		Hits, FVector(Centre.X, Centre.Y, TraceStartZ),
		FVector(Centre.X, Centre.Y, TraceEndZ), ObjectQuery, Query))
	{
		return false;
	}
	for (const FHitResult& Hit : Hits)
	{
		const FVector Normal = Hit.ImpactNormal.GetSafeNormal();
		if (!Hit.bBlockingHit || Hit.ImpactPoint.ContainsNaN() || Normal.ContainsNaN()
			|| Normal.Z < MinimumGroundNormalZ
			|| Hit.ImpactPoint.Z > BodyBounds.Max.Z + 10.0)
		{
			continue;
		}
		OutSurfacePoint = Hit.ImpactPoint;
		OutSurfaceNormal = Normal;
		return true;
	}
	return false;
}

FVector SimCorePedestrianPresentation::BuildLocalGroundDepenetrationStep(
	float SurfaceZCm, float LowestBodyZCm, const FVector& SurfaceNormal,
	float DeltaSeconds)
{
	if (!FMath::IsFinite(SurfaceZCm) || !FMath::IsFinite(LowestBodyZCm)
		|| !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0f
		|| SurfaceNormal.ContainsNaN())
	{
		return FVector::ZeroVector;
	}
	const FVector Normal = SurfaceNormal.GetSafeNormal();
	const float PenetrationCm = SurfaceZCm - LowestBodyZCm;
	if (Normal.Z < MinimumGroundNormalZ || PenetrationCm <= 0.0f)
	{
		return FVector::ZeroVector;
	}
	// Resolve along the measured support normal, but cap the correction so a
	// body already below a query-only surface never teleports upward like a UFO.
	const float RequiredDistanceCm = PenetrationCm / Normal.Z;
	const float MaximumDistanceCm = 350.0f * DeltaSeconds;
	return Normal * FMath::Min(RequiredDistanceCm, MaximumDistanceCm);
}

FVector SimCorePedestrianPresentation::RemoveVelocityIntoGround(
	const FVector& VelocityCmPerSecond, const FVector& SurfaceNormal)
{
	if (VelocityCmPerSecond.ContainsNaN() || SurfaceNormal.ContainsNaN())
	{
		return FVector::ZeroVector;
	}
	const FVector Normal = SurfaceNormal.GetSafeNormal();
	if (Normal.Z < MinimumGroundNormalZ) return VelocityCmPerSecond;
	const double InwardSpeed = FVector::DotProduct(VelocityCmPerSecond, Normal);
	return InwardSpeed < 0.0
		? VelocityCmPerSecond - Normal * InwardSpeed
		: VelocityCmPerSecond;
}

ASimCorePedestrianPresentationActor::ASimCorePedestrianPresentationActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetActorEnableCollision(false);
	Tags.Add(TEXT("SimCoreRuntimeEntity"));
	Tags.Add(TEXT("SimCoreRuntimePedestrian"));
	PedestrianRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PedestrianRoot"));
	SetRootComponent(PedestrianRoot);
	PedestrianRoot->SetMobility(EComponentMobility::Movable);
	CharacterMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CharacterMesh"));
	CharacterMesh->SetupAttachment(PedestrianRoot);
	CharacterMesh->SetMobility(EComponentMobility::Movable);
	CharacterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CharacterMesh->SetGenerateOverlapEvents(false);
	CharacterMesh->SetCanEverAffectNavigation(false);
	CharacterMesh->SetSimulatePhysics(false);
	CharacterMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	CharacterMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> Manny(
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> Quinn(
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> Idle(
		TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> Walk(
		TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Walk/MF_Unarmed_Walk_Fwd.MF_Unarmed_Walk_Fwd"));
	MannyMesh = Manny.Object;
	QuinnMesh = Quinn.Object;
	IdleAnimation = Idle.Object;
	WalkAnimation = Walk.Object;
	CharacterMesh->SetSkeletalMesh(MannyMesh);
}

bool ASimCorePedestrianPresentationActor::HasHumanoidAssets() const
{
	return MannyMesh && QuinnMesh && IdleAnimation && WalkAnimation
		&& IdleAnimation->GetPlayLength() > 0.0f && WalkAnimation->GetPlayLength() > 0.0f;
}

bool ASimCorePedestrianPresentationActor::HasRagdollPhysicsAsset() const
{
	const UPhysicsAsset* Asset = CharacterMesh ? CharacterMesh->GetPhysicsAsset() : nullptr;
	return Asset && Asset->SkeletalBodySetups.Num() >= 10 && Asset->ConstraintSetup.Num() >= 8
		&& CharacterMesh->GetBoneIndex(TEXT("pelvis")) != INDEX_NONE;
}

bool ASimCorePedestrianPresentationActor::StartRagdoll(
	const SimCoreProtocol::FVehicleState& State, bool bLaunch)
{
	if (!HasRagdollPhysicsAsset() || !GetWorld() || !GetWorld()->GetPhysicsScene()) return false;
	if (bLaunch && bRagdollActive && !FMath::IsNearlyZero(RagdollGroundAnchorOffsetCm))
	{
		CharacterMesh->AddWorldOffset(FVector(0.0, 0.0, -RagdollGroundAnchorOffsetCm),
			false, nullptr, ETeleportType::TeleportPhysics);
		RagdollGroundAnchorOffsetCm = 0.0f;
		GroundAnchorDownwardSpeedCmPerSecond = 0.0f;
	}
	if (!bRagdollActive)
	{
		CharacterMesh->PlayAnimation(IdleAnimation, true);
		CharacterMesh->SetPosition(0.0f, false);
		CharacterMesh->SetPlayRate(0.0f);
		CurrentAnimation = IdleAnimation;
		CharacterMesh->bPauseAnims = false;
		CharacterMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		CharacterMesh->RefreshBoneTransforms();
		// The authoritative point is the pelvis, not the animated feet origin.
		// Keep the imported model's standing scale even when the collision OBB is flat.
		const FVector PelvisInModel = CharacterMesh->GetSocketTransform(TEXT("pelvis"), RTS_Component).GetLocation();
		RagdollModelTransform = StandingModelTransform;
		RagdollModelTransform.AddToTranslation(-StandingModelTransform.TransformPosition(PelvisInModel));
		CharacterMesh->SetRelativeTransform(RagdollModelTransform, false, nullptr, ETeleportType::TeleportPhysics);
		// No queries/overlaps and no contact against vehicles, characters or dynamic
		// props. Only static scenery can support this client-only articulated visual.
		SetActorEnableCollision(true);
		CharacterMesh->SetCollisionObjectType(ECC_PhysicsBody);
		CharacterMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		CharacterMesh->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
		CharacterMesh->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
		CharacterMesh->SetNotifyRigidBodyCollision(false);
		CharacterMesh->SetEnableGravity(true);
		CharacterMesh->SetSimulatePhysics(false);
		CharacterMesh->SetAllBodiesSimulatePhysics(false);
		// Only the pelvis remains kinematic. Its server trajectory/tilt carries the
		// collision authority; unconstrained animation no longer moves the limbs.
		CharacterMesh->SetAllBodiesBelowSimulatePhysics(TEXT("pelvis"), true, false);
		CharacterMesh->SetAllBodiesBelowPhysicsBlendWeight(TEXT("pelvis"), 1.0f, false, false);
		bRagdollActive = true;
	}
	bRagdollPaused = false;
	CharacterMesh->bPauseAnims = false;
	bRecoveringRagdoll = false;
	RagdollElapsedSeconds = 0.0f;
	RagdollRecoverySeconds = 0.0f;
	CurrentRecoveryPoseAlpha = 0.0f;
	RecoveryWalkDelayRemainingSeconds = 0.0f;
	GroundAnchorDownwardSpeedCmPerSecond = 0.0f;
	bWalking = false;
	AnimationPlayRate = 0.0f;
	CharacterMesh->SetAllBodiesBelowPhysicsBlendWeight(TEXT("pelvis"), 1.0f, false, false);
	CharacterMesh->SetEnableGravity(true);
	CharacterMesh->WakeAllRigidBodies();
	if (bLaunch)
	{
		const FVector Launch = SimCorePedestrianPresentation::BuildImpactLaunchVelocity(State, GetActorForwardVector());
		CharacterMesh->SetAllPhysicsLinearVelocity(Launch);
		const FVector AngularVelocity = SimCorePedestrianPresentation::BuildImpactAngularVelocity(State);
		CharacterMesh->SetAllPhysicsAngularVelocityInRadians(AngularVelocity);
		const FVector PelvisLocation = CharacterMesh->GetSocketLocation(TEXT("pelvis"));
		// v(point) = v(CG) + omega x r. Lower-body strikes move the legs ahead
		// while the torso rotates back onto the hood, instead of throwing every
		// limb outward regardless of the signed authoritative impact torque.
		for (FBodyInstance* Body : CharacterMesh->Bodies)
		{
			if (!Body || !Body->IsValidBodyInstance() || !Body->IsInstanceSimulatingPhysics()) continue;
			const FVector Arm = Body->GetUnrealWorldTransform().GetLocation() - PelvisLocation;
			Body->SetLinearVelocity((Launch + FVector::CrossProduct(AngularVelocity, Arm)).GetClampedToMaxSize(7000.0), false);
		}
		++RagdollLaunchCount;
	}
	else
	{
		// Reconstruct the received downed/airborne pose without a new launch kick.
		CharacterMesh->SetAllPhysicsLinearVelocity(FVector(
			SimCoreCoordinateFrames::MapEnuPolarVectorToUnrealWorld(State.LinearVelocityEnu) * 100.0));
		CharacterMesh->SetAllPhysicsAngularVelocityInRadians(FVector::ZeroVector);
	}
	return true;
}

void ASimCorePedestrianPresentationActor::StopRagdoll(
	const bool bBeginLocomotionDelay)
{
	if (!bRagdollActive)
	{
		if (!bBeginLocomotionDelay)
		{
			CurrentRecoveryPoseAlpha = 0.0f;
			RecoveryWalkDelayRemainingSeconds = 0.0f;
		}
		return;
	}
	CharacterMesh->SetAllPhysicsLinearVelocity(FVector::ZeroVector);
	CharacterMesh->SetAllPhysicsAngularVelocityInRadians(FVector::ZeroVector);
	CharacterMesh->SetSimulatePhysics(false);
	CharacterMesh->SetAllBodiesSimulatePhysics(false);
	CharacterMesh->SetAllBodiesPhysicsBlendWeight(0.0f);
	CharacterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetActorEnableCollision(false);
	CharacterMesh->AttachToComponent(PedestrianRoot, FAttachmentTransformRules::KeepWorldTransform);
	CharacterMesh->SetRelativeTransform(StandingModelTransform, false, nullptr, ETeleportType::TeleportPhysics);
	CharacterMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	CurrentAnimation = nullptr;
	bRagdollActive = false;
	bRagdollPaused = false;
	bRecoveringRagdoll = false;
	RagdollElapsedSeconds = 0.0f;
	RagdollRecoverySeconds = 0.0f;
	CurrentRecoveryPoseAlpha = bBeginLocomotionDelay ? 1.0f : 0.0f;
	RecoveryWalkDelayRemainingSeconds = bBeginLocomotionDelay
		? SimCorePedestrianPresentation::RecoveryWalkDelaySeconds : 0.0f;
	RagdollGroundAnchorOffsetCm = 0.0f;
	GroundAnchorDownwardSpeedCmPerSecond = 0.0f;
}

void ASimCorePedestrianPresentationActor::UpdateRagdoll(
	const SimCoreProtocol::FVehicleState& State, float DeltaSeconds, bool bMotionAllowed)
{
	if (!bMotionAllowed)
	{
		CharacterMesh->SetEnableGravity(false);
		CharacterMesh->SetAllPhysicsLinearVelocity(FVector::ZeroVector);
		CharacterMesh->SetAllPhysicsAngularVelocityInRadians(FVector::ZeroVector);
		CharacterMesh->PutAllRigidBodiesToSleep();
		CharacterMesh->bPauseAnims = true;
		bRagdollPaused = true;
		return;
	}
	if (bRagdollPaused)
	{
		CharacterMesh->SetEnableGravity(true);
		CharacterMesh->WakeAllRigidBodies();
		CharacterMesh->bPauseAnims = false;
		bRagdollPaused = false;
	}
	const float Step = FMath::Min(DeltaSeconds, 0.05f);
	RagdollElapsedSeconds += Step;
	const auto Phase = State.RuntimeRecoveryPhase;
	const bool bMayRecover = !State.bPedestrianDowned && !State.bPedestrianAirborne
		&& (Phase == SimCoreProtocol::ERuntimeRecoveryPhase::Recovering
			|| Phase == SimCoreProtocol::ERuntimeRecoveryPhase::Driving);
	if (bMayRecover && RagdollElapsedSeconds >= 2.0f)
	{
		if (!bRecoveringRagdoll)
		{
			bRecoveringRagdoll = true;
			RagdollRecoverySeconds = 0.0f;
			CurrentRecoveryPoseAlpha = 0.0f;
			RecoveryStartModelTransform = CharacterMesh->GetRelativeTransform();
		}
		RagdollRecoverySeconds += Step;
		// First arrest residual limb movement on the measured resting pose. Then
		// ease both the physics blend and mesh anchor toward the authored standing
		// pose. This removes the old last-frame transform snap that looked like a
		// floating body suddenly becoming a walking mannequin.
		CharacterMesh->SetEnableGravity(false);
		CharacterMesh->SetAllPhysicsLinearVelocity(FVector::ZeroVector);
		CharacterMesh->SetAllPhysicsAngularVelocityInRadians(FVector::ZeroVector);
		CharacterMesh->PutAllRigidBodiesToSleep();
		if (RagdollRecoverySeconds <=
			SimCorePedestrianPresentation::RagdollRecoverySettleSeconds)
		{
			CharacterMesh->SetAllBodiesBelowPhysicsBlendWeight(
				TEXT("pelvis"), 1.0f, false, false);
			return;
		}
		const float LinearAlpha = FMath::Clamp(
			(RagdollRecoverySeconds
				- SimCorePedestrianPresentation::RagdollRecoverySettleSeconds)
			/ SimCorePedestrianPresentation::RagdollRecoveryBlendSeconds,
			0.0f, 1.0f);
		CurrentRecoveryPoseAlpha = FMath::SmoothStep(0.0f, 1.0f, LinearAlpha);
		CharacterMesh->SetAllBodiesBelowPhysicsBlendWeight(TEXT("pelvis"),
			1.0f - CurrentRecoveryPoseAlpha, false, false);
		FTransform RecoveryTransform;
		RecoveryTransform.Blend(RecoveryStartModelTransform,
			StandingModelTransform, CurrentRecoveryPoseAlpha);
		CharacterMesh->SetRelativeTransform(RecoveryTransform, false, nullptr,
			ETeleportType::TeleportPhysics);
		if (LinearAlpha >= 1.0f) StopRagdoll(true);
		return;
	}
	if (bRecoveringRagdoll)
	{
		// A new severe contact can interrupt recovery without restoring locomotion.
		bRecoveringRagdoll = false;
		RagdollRecoverySeconds = 0.0f;
		CurrentRecoveryPoseAlpha = 0.0f;
		CharacterMesh->SetAllBodiesBelowPhysicsBlendWeight(TEXT("pelvis"), 1.0f, false, false);
		CharacterMesh->SetEnableGravity(true);
	}
	// The airborne centre is not a ground height. Never lift every body by a
	// fabricated floor that rises with the person, or it will stick to the hood.
	if (State.bPedestrianAirborne)
	{
		GroundAnchorDownwardSpeedCmPerSecond = 0.0f;
		return;
	}
	// Query-only roads cannot support Chaos limbs through rigid-body contacts.
	// Measure the actual WorldStatic surface under each body so slopes, curbs and
	// uneven road pieces do not get flattened to the actor-centre ground height.
	struct FBodyGroundContact
	{
		FBodyInstance* Body = nullptr;
		FBox Bounds{ForceInit};
		FVector SurfacePoint = FVector::ZeroVector;
		FVector SurfaceNormal = FVector::UpVector;
	};
	TArray<FBodyGroundContact, TInlineAllocator<32>> GroundContacts;
	const float ExpectedGroundZ = GetActorLocation().Z
		- State.CollisionHalfHeightMeters * 100.0f;
	float NearestLocalGroundGapCm = TNumericLimits<float>::Max();
	for (FBodyInstance* Body : CharacterMesh->Bodies)
	{
		if (!Body || !Body->IsValidBodyInstance()) continue;
		FBodyGroundContact Contact;
		Contact.Body = Body;
		Contact.Bounds = Body->GetBodyBounds();
		if (!SimCorePedestrianPresentation::QueryLocalGroundSupport(
			GetWorld(), this, Contact.Bounds, ExpectedGroundZ,
			Contact.SurfacePoint, Contact.SurfaceNormal))
		{
			continue;
		}
		NearestLocalGroundGapCm = FMath::Min(NearestLocalGroundGapCm,
			static_cast<float>(Contact.Bounds.Min.Z - Contact.SurfacePoint.Z));
		GroundContacts.Add(MoveTemp(Contact));
	}
	const bool bAuthoritativeBodySettled = State.bPedestrianDowned
		&& FMath::Abs(State.LinearVelocityEnu.Z) <= 0.05
		&& State.AngularVelocityBody.SizeSquared() <= 0.04;
	float AnchorStep = 0.0f;
	if (bAuthoritativeBodySettled && GroundContacts.Num() > 0
		&& FMath::IsFinite(NearestLocalGroundGapCm))
	{
		// The helper consumes a gap; zero is a convenient local reference plane.
		AnchorStep = SimCorePedestrianPresentation::BuildSettledGroundAnchorStep(
			0.0f, NearestLocalGroundGapCm, Step,
			GroundAnchorDownwardSpeedCmPerSecond);
		if (!FMath::IsNearlyZero(AnchorStep))
		{
			CharacterMesh->AddWorldOffset(FVector(0.0, 0.0, AnchorStep),
				false, nullptr, ETeleportType::TeleportPhysics);
			RagdollGroundAnchorOffsetCm += AnchorStep;
		}
	}
	else
	{
		GroundAnchorDownwardSpeedCmPerSecond = 0.0f;
	}
	for (const FBodyGroundContact& Contact : GroundContacts)
	{
		FBodyInstance* Body = Contact.Body;
		if (!Body || !Body->IsValidBodyInstance() || !Body->IsInstanceSimulatingPhysics()) continue;
		float MinimumBodyZ = static_cast<float>(Contact.Bounds.Min.Z) + AnchorStep;
		FVector Velocity = Body->GetUnrealWorldVelocity();
		const FVector Depenetration = SimCorePedestrianPresentation::BuildLocalGroundDepenetrationStep(
			static_cast<float>(Contact.SurfacePoint.Z), MinimumBodyZ,
			Contact.SurfaceNormal, Step);
		if (!Depenetration.IsNearlyZero())
		{
			FTransform BodyTransform = Body->GetUnrealWorldTransform();
			BodyTransform.AddToTranslation(Depenetration);
			Body->SetBodyTransform(BodyTransform, ETeleportType::TeleportPhysics);
			MinimumBodyZ += Depenetration.Z;
		}
		if (MinimumBodyZ <= Contact.SurfacePoint.Z
				+ SimCorePedestrianPresentation::GroundContactToleranceCm
			&& FVector::DotProduct(Velocity, Contact.SurfaceNormal) < 0.0)
		{
			Body->SetLinearVelocity(
				SimCorePedestrianPresentation::RemoveVelocityIntoGround(
					Velocity, Contact.SurfaceNormal), false);
		}
	}
}

void ASimCorePedestrianPresentationActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopRagdoll();
	Super::EndPlay(EndPlayReason);
}

void ASimCorePedestrianPresentationActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	const bool bGameWorld = GetWorld() && GetWorld()->IsGameWorld();
	SetActorHiddenInGame(!bGameWorld);
#if WITH_EDITOR
	SetIsTemporarilyHiddenInEditor(!bGameWorld);
#endif
}

bool ASimCorePedestrianPresentationActor::ApplySnapshot(
	const SimCoreProtocol::FVehicleState& State, float SnapshotAgeSeconds, float DeltaSeconds,
	bool bMotionAllowed, float MaxExtrapolationSeconds, const FVector& PresentationOffsetCm)
{
	const bool bValid = GetWorld() && GetWorld()->IsGameWorld() && HasHumanoidAssets()
		&& State.EntityKind == SimCoreProtocol::EEntityKind::Pedestrian && State.EntityId != 0
		&& FMath::IsFinite(SnapshotAgeSeconds) && SnapshotAgeSeconds >= 0.0f
		&& SnapshotAgeSeconds <= SimCoreTrafficSignals::SnapshotFreshnessSeconds
		&& FMath::IsFinite(DeltaSeconds) && DeltaSeconds >= 0.0f
		&& FMath::IsFinite(MaxExtrapolationSeconds) && MaxExtrapolationSeconds >= 0.0f;
	SimCorePresentation::FRuntimeEntityPresentationSample Sample;
	if (!bValid || !SimCorePresentation::BuildRuntimeEntitySample(State,
		bMotionAllowed ? SnapshotAgeSeconds : 0.0f, MaxExtrapolationSeconds, PresentationOffsetCm, Sample))
	{
		bNeedsDownedReconstruction |= bRagdollActive;
		StopRagdoll();
		CharacterMesh->bPauseAnims = true;
		SetActorHiddenInGame(true);
		return false;
	}
	USkeletalMesh* SelectedMesh = (State.EntityId % 2 == 0) ? MannyMesh.Get() : QuinnMesh.Get();
	FTransform ModelTransform;
	if (!SimCorePedestrianPresentation::BuildModelTransform(
		SelectedMesh->GetImportedBounds().GetBox(),
		SimCorePedestrianPresentation::StandingHalfHeightMeters, ModelTransform))
	{
		bNeedsDownedReconstruction |= bRagdollActive;
		StopRagdoll();
		CharacterMesh->bPauseAnims = true;
		SetActorHiddenInGame(true);
		return false;
	}
	const bool bIdentityChanged = bReceivedSnapshot && (EntityId != State.EntityId
		|| PresentedPlaySessionId != State.PlaySessionId || PresentedMapChecksum != State.MapPackageChecksum);
	if (bIdentityChanged)
	{
		StopRagdoll();
		LastCollisionEventSequence = 0;
		RagdollLaunchCount = 0;
		bReceivedSnapshot = false;
		bLastAuthoritativeDowned = false;
		bNeedsDownedReconstruction = false;
	}
	if (CharacterMesh->GetSkeletalMeshAsset() != SelectedMesh)
	{
		StopRagdoll();
		CharacterMesh->SetSkeletalMesh(SelectedMesh);
		CurrentAnimation = nullptr;
	}
	EntityId = State.EntityId;
	PresentedPlaySessionId = State.PlaySessionId;
	PresentedMapChecksum = State.MapPackageChecksum;
	StandingModelTransform = ModelTransform;
	const bool bHadSnapshot = bReceivedSnapshot;
	bReceivedSnapshot = true;
	SetActorHiddenInGame(false);
	SetActorLocationAndRotation(Sample.ActorLocation, Sample.ActorRotation,
		false, nullptr, bRagdollActive ? ETeleportType::None : ETeleportType::TeleportPhysics);
	SetActorScale3D(FVector::OneVector);
	if (!bRagdollActive) CharacterMesh->SetRelativeTransform(ModelTransform);
	const bool bBecameDowned = State.bPedestrianDowned && !bLastAuthoritativeDowned;
	if (!State.bPedestrianDowned)
	{
		// An ordinary collision event may only be a standing shove. Remember it,
		// but a later same-event downed transition still starts the ragdoll below.
		LastCollisionEventSequence = FMath::Max(LastCollisionEventSequence, State.CollisionEventSequence);
		bNeedsDownedReconstruction = false;
	}
	const bool bNewImpact = State.bPedestrianDowned && State.CollisionEventSequence > 0
		&& (State.CollisionEventSequence > LastCollisionEventSequence || bBecameDowned)
		&& State.LastImpactImpulseNs > 0.0f;
	const bool bRestoreDownedPose = State.bPedestrianDowned && !bRagdollActive
		&& State.CollisionEventSequence > 0;
	if (bMotionAllowed && (bNewImpact || bRestoreDownedPose))
	{
		if (StartRagdoll(State, bNewImpact && bHadSnapshot && !bNeedsDownedReconstruction))
		{
			LastCollisionEventSequence = State.CollisionEventSequence;
			bNeedsDownedReconstruction = false;
		}
	}
	bLastAuthoritativeDowned = State.bPedestrianDowned;
	if (bRagdollActive)
	{
		UpdateRagdoll(State, DeltaSeconds, bMotionAllowed);
		if (bRagdollActive) return true;
	}
	if (bMotionAllowed && RecoveryWalkDelayRemainingSeconds > 0.0f
		&& FMath::IsFinite(DeltaSeconds) && DeltaSeconds > 0.0f)
	{
		RecoveryWalkDelayRemainingSeconds = FMath::Max(
			0.0f, RecoveryWalkDelayRemainingSeconds - FMath::Min(DeltaSeconds, 0.05f));
	}
	const bool bUpright = Sample.ActorRotation.Quaternion().GetUpVector().Z
		> FMath::Cos(FMath::DegreesToRadians(20.0));
	const double Speed = FMath::Sqrt(FMath::Square(State.LinearVelocityEnu.X)
		+ FMath::Square(State.LinearVelocityEnu.Y));
	const bool bLocomotionAllowed = State.RuntimeRecoveryPhase == SimCoreProtocol::ERuntimeRecoveryPhase::Driving
		|| State.RuntimeRecoveryPhase == SimCoreProtocol::ERuntimeRecoveryPhase::Recovering;
	bWalking = bMotionAllowed && bUpright && bLocomotionAllowed
		&& RecoveryWalkDelayRemainingSeconds <= 0.0f && Speed > 0.08;
	UAnimSequence* Animation = bWalking ? WalkAnimation.Get() : IdleAnimation.Get();
	if (CurrentAnimation != Animation)
	{
		CharacterMesh->PlayAnimation(Animation, true);
		// Stable per-entity phases keep nearby people from walking in lockstep.
		CharacterMesh->SetPosition(FMath::Fmod(EntityId * 0.137f, Animation->GetPlayLength()), false);
		CurrentAnimation = Animation;
	}
	AnimationPlayRate = bWalking ? FMath::Clamp(static_cast<float>(Speed)
		/ SimCorePedestrianPresentation::WalkReferenceSpeedMps, 0.05f, 2.0f) : 1.0f;
	CharacterMesh->bPauseAnims = !bMotionAllowed || !bUpright;
	if (CharacterMesh->bPauseAnims) AnimationPlayRate = 0.0f;
	CharacterMesh->SetPlayRate(AnimationPlayRate);
	return true;
}
